#pragma once
#include <array>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace hg {
// Virtual 8 MB PS2 memory card on the SIO2 bus (device byte 0x81).
// Protocol per the project's SIO2 hardware reference (ps2tek "Memory Cards"):
// replies mirror the request byte-for-byte and start at byte 2; 0x2B acks and
// the (settable, default 0x55) terminator closes each reply. Raw pages are
// 512 data + 16 ECC bytes; the card stores ECC bytes exactly as the original
// MCMAN driver writes them. Erase fills a 16-page block with 0xFF (NAND).
// The backing file is the raw image; every modified page/block is written
// through immediately. Unknown commands fail explicitly.
class VirtualMemoryCard {
public:
    static constexpr std::size_t page_bytes=512,ecc_bytes=16,raw_page_bytes=page_bytes+ecc_bytes;
    static constexpr std::size_t pages=16384,block_pages=16;
    static constexpr std::size_t image_bytes=raw_page_bytes*pages; // 8,650,752
    bool present=false;
    std::array<std::uint64_t,256> command_counts{}; // Host diagnostics only.

    void open(const std::filesystem::path& file) {
        path_=file;
        data_.assign(image_bytes,0xff);
        if(std::filesystem::exists(file)) {
            if(std::filesystem::file_size(file)!=image_bytes)
                throw std::runtime_error("memory card image must be exactly 8650752 bytes: "+file.string());
            std::ifstream in(file,std::ios::binary);
            in.read(reinterpret_cast<char*>(data_.data()),std::streamsize(image_bytes));
            if(!in)throw std::runtime_error("cannot read memory card image: "+file.string());
        } else {
            std::ofstream out(file,std::ios::binary|std::ios::trunc);
            out.write(reinterpret_cast<const char*>(data_.data()),std::streamsize(image_bytes));
            if(!out)throw std::runtime_error("cannot create memory card image: "+file.string());
        }
        terminator_=0x55;
        file_.close();
        file_.open(file,std::ios::binary|std::ios::in|std::ios::out);
        if(!file_)throw std::runtime_error("cannot open memory card image for writing: "+file.string());
    }
    // Copies of IopState share the card through shared_ptr; the handle is not copied.
    VirtualMemoryCard()=default;
    VirtualMemoryCard(const VirtualMemoryCard&)=delete;
    VirtualMemoryCard& operator=(const VirtualMemoryCard&)=delete;
    ~VirtualMemoryCard(){flush();}
    void flush() noexcept {if(file_.is_open())file_.flush();}
    bool configured() const noexcept {return !path_.empty();}

    // One SIO2 transfer: request bytes in, equally long reply out.
    std::vector<std::uint8_t> exchange(const std::vector<std::uint8_t>& packet) {
        const auto n=packet.size();
        if(n<2||packet[0]!=0x81)throw std::runtime_error("memory card transfer without device byte 0x81");
        std::vector<std::uint8_t> reply(n,0xff);
        if(!command_counts[packet[1]]++) // Host diagnostic: first use of each command.
            std::fprintf(stderr,"Virtual memory card first command 0x%02x length %zu\n",unsigned(packet[1]),n);
        const auto ack_tail=[&]{
            if(n<4)throw std::runtime_error("memory card command packet too short");
            reply[n-2]=0x2b;reply[n-1]=terminator_;
        };
        switch(packet[1]) {
        case 0x11:case 0xbf:case 0xf3:case 0xf7:ack_tail();break;
        case 0x12:case 0x81:flush();ack_tail();break; // Commit / read-write end: persist writes.
        case 0x21:case 0x22:case 0x23: {
            if(n<7)throw std::runtime_error("memory card address command too short");
            const std::uint32_t page=std::uint32_t(packet[2])|(std::uint32_t(packet[3])<<8)|
                                     (std::uint32_t(packet[4])<<16)|(std::uint32_t(packet[5])<<24);
            if(page>=pages)throw std::runtime_error("memory card page address outside the 8 MB card");
            if(packet[1]==0x21)erase_page_=page;
            else {stream_page_=page;stream_offset_=0;}
            ack_tail();break;
        }
        case 0x82: { // Erase the 16-page block selected by 0x21.
            const auto first=std::size_t(erase_page_)/block_pages*block_pages;
            std::fill(data_.begin()+first*raw_page_bytes,data_.begin()+(first+block_pages)*raw_page_bytes,0xff);
            write_through(first*raw_page_bytes,block_pages*raw_page_bytes);
            flush();ack_tail();break;
        }
        case 0x26: { // Specs: 512-byte sectors, 16-sector erase blocks, 16384 sectors.
            if(n<13)throw std::runtime_error("memory card get-specs packet too short");
            static constexpr std::array<std::uint8_t,8> specs{{0x00,0x02,0x10,0x00,0x00,0x40,0x00,0x00}};
            reply[2]=0x2b;std::uint8_t sum=0;
            for(std::size_t i=0;i<specs.size();++i){reply[3+i]=specs[i];sum^=specs[i];}
            reply[11]=sum;reply[12]=terminator_;break;
        }
        case 0x27: {
            if(n<5)throw std::runtime_error("memory card set-terminator packet too short");
            reply[3]=0x2b;reply[4]=terminator_;terminator_=packet[2];break;
        }
        case 0x28: {
            if(n<5)throw std::runtime_error("memory card get-terminator packet too short");
            reply[2]=0x2b;reply[3]=terminator_;reply[4]=0x55;break;
        }
        case 0x42: { // Write up to 128 bytes at the 0x22 stream pointer.
            const std::size_t size=packet[2];
            if(size>128||n<4+size)throw std::runtime_error("memory card write packet size mismatch");
            const auto at=stream_position(size);
            std::copy(packet.begin()+3,packet.begin()+3+std::ptrdiff_t(size),data_.begin()+std::ptrdiff_t(at));
            write_through(at,size);stream_offset_+=size;
            reply[2]=0x2b;reply[3]=terminator_;reply[n-1]=terminator_;break;
        }
        case 0x43: { // Read up to 128 bytes at the 0x23 stream pointer.
            const std::size_t size=packet[2];
            if(size>128||n<6+size)throw std::runtime_error("memory card read packet size mismatch");
            const auto at=stream_position(size);
            reply[2]=0x2b;reply[3]=terminator_;std::uint8_t sum=0;
            for(std::size_t i=0;i<size;++i){reply[4+i]=data_[at+i];sum^=data_[at+i];}
            reply[4+size]=sum;reply[5+size]=terminator_;stream_offset_+=size;break;
        }
        default:
            throw std::runtime_error("unsupported memory card command 0x"+hex(packet[1]));
        }
        return reply;
    }
private:
    std::filesystem::path path_;
    std::vector<std::uint8_t> data_;
    std::uint8_t terminator_=0x55;
    std::uint32_t erase_page_=0,stream_page_=0;
    std::size_t stream_offset_=0;
    static std::string hex(unsigned v){const char* d="0123456789abcdef";return std::string{d[(v>>4)&15],d[v&15]};}
    std::size_t stream_position(std::size_t size) const {
        const auto at=std::size_t(stream_page_)*raw_page_bytes+stream_offset_;
        if(at+size>image_bytes)throw std::runtime_error("memory card stream past the end of the card");
        return at;
    }
    std::fstream file_;
    void write_through(std::size_t at,std::size_t size) {
        // HG: one persistent handle. Reopening per 128-byte chunk made saving
        // crawl (~2 FPS) on Windows; data is flushed at the card commit points.
        file_.seekp(std::streamoff(at));
        file_.write(reinterpret_cast<const char*>(data_.data()+at),std::streamsize(size));
        if(!file_)throw std::runtime_error("cannot write memory card image: "+path_.string());
    }
};
}
