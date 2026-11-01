#pragma once
// Read-only ECMA-119 reader for the user's disc: either DATA.CVM itself or the
// whole .iso (DATA.CVM is located inside it). Same rules as tools/hgtool/volume.py:
// unsupported filesystem features fail explicitly. No game files are bundled.
#include <cstdint>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <functional>
#include <set>
#include <iterator>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace hgw {

constexpr std::uint64_t block_size=2048;
constexpr std::uint64_t cvm_volume_origin=6144; // CVM header precedes the ISO volume

// `record_at` = absolute byte position of the entry's directory record in the image file.
struct DiscEntry {std::uint64_t extent=0,size=0;bool directory=false;std::uint64_t record_at=0;};

class Volume {
public:
    Volume(std::ifstream& file,std::uint64_t origin,std::uint64_t limit):file_(file),origin_(origin),limit_(limit) {
        std::vector<std::uint8_t> primary;
        for(std::uint64_t sector=16;;++sector) {
            if(sector>=80)throw std::runtime_error("volume descriptor terminator missing");
            auto d=read(sector*block_size,block_size);
            if(std::string(reinterpret_cast<char*>(&d[1]),5)!="CD001")throw std::runtime_error("not an ISO-9660 volume");
            if(d[0]==1&&primary.empty())primary=d;
            if(d[0]==255)break;
        }
        if(primary.empty())throw std::runtime_error("primary volume descriptor missing");
        if(both16(primary,128)!=block_size)throw std::runtime_error("only 2048-byte blocks are supported");
        blocks_=le32(primary.data()+80);
        const auto root=record(primary.data()+156,34);
        walk(root,"");
    }
    const std::map<std::string,DiscEntry>& entries() const {return entries_;}
    bool contains(const std::string& path) const {return entries_.count(path)!=0;}
    std::uint64_t file_offset(const std::string& path) const {return origin_+at(path).extent*block_size;}
    std::uint64_t origin() const {return origin_;}
    std::uint64_t blocks() const {return blocks_;}
    std::vector<std::uint8_t> read_file(const std::string& path) const {
        const auto& e=at(path);
        if(e.directory)throw std::runtime_error(path+" is a directory");
        return read(e.extent*block_size,e.size);
    }
    const DiscEntry& at(const std::string& path) const {
        const auto found=entries_.find(path);
        if(found==entries_.end())throw std::runtime_error("disc file not found: "+path);
        return found->second;
    }
private:
    static std::uint32_t le32(const std::uint8_t* p) {return p[0]|p[1]<<8|p[2]<<16|std::uint32_t(p[3])<<24;}
    static std::uint32_t both16(const std::vector<std::uint8_t>& d,std::size_t o) {return d[o]|d[o+1]<<8;}
    std::vector<std::uint8_t> read(std::uint64_t offset,std::uint64_t size) const {
        if(offset+size>limit_)throw std::runtime_error("volume read outside the disc image");
        std::vector<std::uint8_t> data(size);
        file_.clear();
        file_.seekg(static_cast<std::streamoff>(origin_+offset));
        file_.read(reinterpret_cast<char*>(data.data()),static_cast<std::streamsize>(size));
        if(!file_)throw std::runtime_error("short disc read");
        return data;
    }
    DiscEntry record(const std::uint8_t* r,std::size_t length) const {
        if(length<34||r[0]!=length||!r[32])throw std::runtime_error("malformed directory record");
        if(r[1]||r[26]||r[27]||(r[25]&~3))throw std::runtime_error("unsupported directory record features");
        return {le32(r+2),le32(r+10),(r[25]&2)!=0};
    }
    void walk(const DiscEntry& directory,const std::string& path) {
        if(++visited_>4096)throw std::runtime_error("directory traversal limit exceeded");
        const auto data=read(directory.extent*block_size,directory.size);
        for(std::size_t offset=0;offset<data.size();) {
            const std::size_t length=data[offset];
            if(!length) {offset=(offset/block_size+1)*block_size;continue;}
            if(offset%block_size+length>block_size)throw std::runtime_error("directory record crosses a sector");
            const auto* r=data.data()+offset;
            const auto item=record(r,length);
            offset+=length;
            const std::string name(reinterpret_cast<const char*>(r+33),r[32]);
            if(name.size()==1&&(name[0]==0||name[0]==1))continue;
            const auto full=path+"/"+name;
            entries_[full]=item;
            entries_[full].record_at=origin_+directory.extent*block_size+(offset-length);
            if(item.directory)walk(item,full);
        }
    }
    std::ifstream& file_;
    std::uint64_t origin_,limit_,blocks_=0;
    std::map<std::string,DiscEntry> entries_;
    int visited_=0;
};

// Owns the image file and exposes the game's DATA.CVM volume.
class Disc {
public:
    explicit Disc(const std::filesystem::path& path) {
        file_.open(path,std::ios::binary);
        if(!file_)throw std::runtime_error("cannot open "+path.string());
        const auto size=std::filesystem::file_size(path);
        auto extension=path.extension().string();
        for(auto& c:extension)c=char(std::tolower(static_cast<unsigned char>(c)));
        std::uint64_t cvm_offset=0,cvm_size=size;
        if(extension==".iso") {
            Volume outer(file_,0,size);
            cvm_offset=outer.file_offset("/DATA.CVM;1");
            cvm_size=outer.at("/DATA.CVM;1").size;
            if(outer.contains("/SLUS_210.75;1"))executable_=outer.read_file("/SLUS_210.75;1");
            iso_=path;iso_cvm_offset_=cvm_offset;
            iso_cvm_record_=outer.at("/DATA.CVM;1").record_at;iso_blocks_=outer.blocks();
        } else {
            // A sibling .iso lets edits be exported as a patched disc image.
            for(const auto& entry:std::filesystem::directory_iterator(path.parent_path())) {
                auto ext=entry.path().extension().string();
                for(auto& c:ext)c=char(std::tolower(static_cast<unsigned char>(c)));
                if(ext!=".iso")continue;
                std::ifstream iso(entry.path(),std::ios::binary);
                try {
                    Volume outer(iso,0,std::filesystem::file_size(entry.path()));
                    if(outer.contains("/DATA.CVM;1")&&outer.at("/DATA.CVM;1").size==size)
                        {iso_=entry.path();iso_cvm_offset_=outer.file_offset("/DATA.CVM;1");
                         iso_cvm_record_=outer.at("/DATA.CVM;1").record_at;iso_blocks_=outer.blocks();break;}
                } catch(const std::exception&) {}
            }
            // DATA.CVM given directly: the executable sits next to it in the user's dump.
            const auto elf=path.parent_path()/"SLUS_210.75";
            if(std::filesystem::exists(elf)) {
                std::ifstream in(elf,std::ios::binary);
                executable_.assign(std::istreambuf_iterator<char>(in),{});
            }
        }
        volume_=std::make_unique<Volume>(file_,cvm_offset+cvm_volume_origin,cvm_size-cvm_volume_origin);
    }
    Disc(const Disc&)=delete;Disc& operator=(const Disc&)=delete;
    const Volume& data() const {return *volume_;}
    // The game executable (empty if not found); read-only, used for static data tables.
    const std::vector<std::uint8_t>& executable() const {return executable_;}
    // The full disc image (input .iso, or one next to DATA.CVM); empty if none.
    const std::filesystem::path& iso() const {return iso_;}
    // Byte position of a DATA.CVM file inside the disc image.
    std::uint64_t iso_offset(const std::string& path) const {
        return iso_cvm_offset_+cvm_volume_origin+volume_->at(path).extent*block_size;
    }
    std::uint64_t iso_cvm_offset() const {return iso_cvm_offset_;}
    std::uint64_t iso_cvm_record() const {return iso_cvm_record_;}   // outer DATA.CVM directory record
    std::uint64_t iso_blocks() const {return iso_blocks_;}           // outer volume size in sectors
    // Inner directory record of a DATA.CVM file, as a position in the disc image.
    std::uint64_t iso_record(const std::string& path) const {
        return iso_cvm_offset_+volume_->at(path).record_at-volume_->origin()+cvm_volume_origin;
    }
private:
    mutable std::ifstream file_;
    std::unique_ptr<Volume> volume_;
    std::vector<std::uint8_t> executable_;
    std::filesystem::path iso_;
    std::uint64_t iso_cvm_offset_=0,iso_cvm_record_=0,iso_blocks_=0;
};

// Writes a copy of the disc image with edited rooms, so the unmodified game runs it with
// `--disc`. A room that still fits its original sectors is written in place (only its
// directory size changes); a larger one moves to free space after DATA.CVM's data, and the
// room's inner directory record, the inner volume size, the CVM header's size fields
// (0x1C total, 0x808 total-0x80C, 0x830 data; all big-endian) and DATA.CVM's outer
// directory size are updated. The free space ends where the outer volume ends (10,254
// sectors on the USA disc). Every export starts from the original image's metadata;
// rooms patched by an earlier export but no longer edited are restored.
inline std::string export_disc(const Disc& disc,const std::map<int,std::vector<std::uint8_t>>& edited,
                               const std::function<std::string(int)>& path_of,const std::filesystem::path& target) {
    if(disc.iso().empty())throw std::runtime_error("no .iso found to export from");
    // Never write into the user's original image.
    if(std::filesystem::exists(target)&&std::filesystem::equivalent(target,disc.iso()))
        throw std::runtime_error("refusing to export onto the original disc image");
    const auto original_size=std::filesystem::file_size(disc.iso());
    std::filesystem::create_directories(target.parent_path());
    if(!std::filesystem::exists(target)||std::filesystem::file_size(target)!=original_size)
        std::filesystem::copy_file(disc.iso(),target,std::filesystem::copy_options::overwrite_existing);
    const auto manifest=target.string()+".patched-rooms.txt";
    std::set<int> rooms;
    {std::ifstream in(manifest);int id;while(in>>std::hex>>id)rooms.insert(id);}
    for(const auto& [id,bytes]:edited)rooms.insert(id);
    std::fstream out(target,std::ios::binary|std::ios::in|std::ios::out);
    if(!out)throw std::runtime_error("cannot open "+target.string());
    const auto put=[&](std::uint64_t at,const void* data,std::size_t size){
        out.seekp(static_cast<std::streamoff>(at));out.write(static_cast<const char*>(data),std::streamsize(size));};
    const auto both32=[&](std::uint64_t at,std::uint32_t v){
        const std::uint8_t b[8]={std::uint8_t(v),std::uint8_t(v>>8),std::uint8_t(v>>16),std::uint8_t(v>>24),
                                 std::uint8_t(v>>24),std::uint8_t(v>>16),std::uint8_t(v>>8),std::uint8_t(v)};
        put(at,b,8);};
    const auto be=[&](std::uint64_t at,std::uint64_t v,int bytes){
        std::uint8_t b[8];for(int i=0;i<bytes;++i)b[i]=std::uint8_t(v>>(8*(bytes-1-i)));put(at,b,std::size_t(bytes));};
    const auto& volume=disc.data();
    const std::uint64_t original_blocks=volume.blocks();
    std::uint64_t next_free=original_blocks;   // relocated rooms go here (inner sector numbers)
    std::ofstream list(manifest,std::ios::trunc);
    int patched=0,restored=0,moved=0;
    for(const int id:rooms) {
        const auto path=path_of(id);
        const auto& entry=volume.at(path);
        const auto found=edited.find(id);
        const auto bytes=found!=edited.end()?found->second:volume.read_file(path);
        const std::uint64_t sectors=(bytes.size()+block_size-1)/block_size;
        std::uint64_t extent=entry.extent;
        if(sectors>(entry.size+block_size-1)/block_size) {
            extent=next_free;next_free+=sectors;++moved;
            const std::vector<std::uint8_t> zeros(sectors*block_size-bytes.size(),0);
            put(disc.iso_cvm_offset()+cvm_volume_origin+extent*block_size+bytes.size(),zeros.data(),zeros.size());
        }
        put(disc.iso_cvm_offset()+cvm_volume_origin+extent*block_size,bytes.data(),bytes.size());
        const auto record=disc.iso_record(path);
        both32(record+2,std::uint32_t(extent));
        both32(record+10,std::uint32_t(bytes.size()));
        if(found!=edited.end()){list<<std::hex<<id<<'\n';++patched;}else ++restored;
    }
    // Volume and container sizes, always rewritten from the original values.
    const std::uint64_t cvm=next_free*block_size+cvm_volume_origin;
    if(disc.iso_cvm_offset()+cvm>disc.iso_blocks()*block_size)
        throw std::runtime_error("grown rooms need more space than the disc has after DATA.CVM");
    both32(disc.iso_cvm_offset()+cvm_volume_origin+16*block_size+80,std::uint32_t(next_free));
    be(disc.iso_cvm_offset()+0x1c,cvm,8);
    be(disc.iso_cvm_offset()+0x808,cvm-0x80c,4);
    be(disc.iso_cvm_offset()+0x830,cvm-cvm_volume_origin,8);
    both32(disc.iso_cvm_record()+10,std::uint32_t(cvm));
    if(!out||!list)throw std::runtime_error("disc export write failed");
    return "exported "+std::to_string(patched)+" edited room(s)"+(moved?" ("+std::to_string(moved)+" moved to free space)":"")+
           (restored?" ("+std::to_string(restored)+" restored)":"")+" to "+target.string()+
           " - run hg_game with --disc \""+target.string()+"\"";
}

// Bytes at a virtual address of a little-endian ELF32 executable (PT_LOAD file bytes only).
inline const std::uint8_t* elf_address(const std::vector<std::uint8_t>& elf,std::uint32_t address,std::size_t size) {
    const auto u32=[&](std::size_t o){return std::uint32_t(elf[o]|elf[o+1]<<8|elf[o+2]<<16|std::uint32_t(elf[o+3])<<24);};
    const auto u16=[&](std::size_t o){return std::uint32_t(elf[o]|elf[o+1]<<8);};
    if(elf.size()<52||elf[0]!=0x7f||elf[1]!='E'||elf[2]!='L'||elf[3]!='F'||elf[4]!=1||elf[5]!=1)return nullptr;
    const auto phoff=u32(28),phentsize=u16(42),phnum=u16(44);
    for(std::uint32_t i=0;i<phnum;++i) {
        const std::size_t ph=phoff+std::size_t(i)*phentsize;
        if(ph+32>elf.size())return nullptr;
        if(u32(ph)!=1)continue;   // PT_LOAD
        const auto offset=u32(ph+4),vaddr=u32(ph+8),filesz=u32(ph+16);
        if(address>=vaddr&&std::uint64_t(address)+size<=std::uint64_t(vaddr)+filesz&&offset+(address-vaddr)+size<=elf.size())
            return elf.data()+offset+(address-vaddr);
    }
    return nullptr;
}
}
