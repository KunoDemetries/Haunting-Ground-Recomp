#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <utility>
#include <stdexcept>

namespace hg {
// IOP DMA configuration and the independently decoded SIFMAN source-chain subset.
// Completion requires transferring the actual packet into a bounded endpoint.
struct IopDmac {
    std::array<std::array<std::uint32_t,4>,9> channel{};
    std::uint32_t dpcr=0,dpcr2=0,dicr2=0;
    // DPCR holds channels 0..6; DPCR2 holds channels 7..12.  A nibble is
    // priority bits 0..2 plus its enable bit.  This models configuration
    // only: a transfer still requires a separately supported endpoint.
    static std::uint32_t& priority_control(IopDmac& dmac,std::uint32_t channel) {
        if(channel>12)throw std::runtime_error("unimplemented IOP DMA channel");
        return channel<7?dmac.dpcr:dmac.dpcr2;
    }
    static unsigned priority_shift(std::uint32_t channel) {
        if(channel>12)throw std::runtime_error("unimplemented IOP DMA channel");
        return (channel<7?channel:channel-7)*4;
    }
    void set_channel_priority(std::uint32_t channel,std::uint32_t priority) {
        if(priority>7)throw std::runtime_error("invalid IOP DMA priority");
        auto& control=priority_control(*this,channel);const auto shift=priority_shift(channel);
        control=(control&~(7u<<shift))|(priority<<shift);
    }
    void set_channel_enabled(std::uint32_t channel,bool enabled) {
        auto& control=priority_control(*this,channel);const auto shift=priority_shift(channel);
        control=enabled?control|(8u<<shift):control&~(8u<<shift);
    }
    std::uint32_t completed_channels=0;
    bool source_tag_loaded=false,source_header_sent=false,source_end=false,source_irq=false;
    std::uint32_t source_words=0,source_address=0;
    std::uint64_t source_header=0;
    std::array<std::uint32_t,4> source_lanes{};
    std::array<std::uint32_t,4> last_sif0_tag{},last_sif0_payload{};
    unsigned last_sif0_payload_words=0;
    struct Sif0Record {
        std::array<std::uint32_t,4> tag{};std::array<std::uint32_t,16> payload{};
        unsigned payload_words=0,total_words=0;bool truncated=false;
    };
    std::array<Sif0Record,16> sif0_history{};
    std::size_t sif0_history_cursor=0,sif0_active_record=0;
    std::uint32_t receive_words=0;
    bool receive_end=false,receive_irq=false;
    struct Sif1Record {
        std::uint32_t address=0,count=0,received_words=0,captured_words=0;
        bool truncated=false;
        std::array<std::uint32_t,64> payload{};
    };
    std::array<Sif1Record,16> sif1_history{};
    std::size_t sif1_history_cursor=0,sif1_active_record=0;
    // The final bank is DMAC2's documented otherwise-unused channel-shaped
    // register block. SIFMAN reads its MADR while determining DMA completion.
    static constexpr std::array<std::uint32_t,9> bases{0x1f8010a0,0x1f801520,0x1f801530,0x1f8010b0,0x1f801540,0x1f801550,0x1f801560,0x1f8010c0,0x1f801500};
    bool contains(std::uint32_t address) const {
        if(address==0x1f8010f0 || address==0x1f801570 || address==0x1f801574)return true;
        for(unsigned n=0;n<bases.size();++n)if(address>=bases[n] && address<bases[n]+(n==1?16:12))return true;
        return false;
    }
    std::uint32_t& word(std::uint32_t address) {
        if(address==0x1f8010f0)return dpcr;
        if(address==0x1f801570)return dpcr2;
        for(unsigned n=0;n<bases.size();++n)if(address>=bases[n] && address<bases[n]+(n==1?16:12))return channel[n][(address-bases[n])/4];
        throw std::runtime_error("unimplemented IOP DMA register");
    }
    void check(std::uint32_t address,unsigned size) const {
        if((size!=2 && size!=4) || address%size)throw std::runtime_error("invalid IOP DMA register width/alignment");
        if(size==2 && (address&15)!=4 && (address&15)!=6)throw std::runtime_error("only IOP BCR supports halfword access");
    }
    std::uint32_t read(std::uint32_t address,unsigned size) {
        check(address,size);
        if(address==0x1f801574 && size==4)
            return (dicr2&0x00ffffffu)|(((completed_channels>>7)&0x3fu)<<24);
        return (word(address&~3u)>>((address&3)*8))&(size==2?0xffffu:0xffffffffu);
    }
    void write(std::uint32_t address,unsigned size,std::uint32_t value) {
        check(address,size);
        if(address==0x1f801574 && size==4) {
            completed_channels&=~(((value>>24)&0x3fu)<<7);
            dicr2=value&0x00ffffffu;return;
        }
        auto& target=word(address&~3u);
        // Original SIFMAN may reassert an already-active CHCR after it has
        // rebuilt the same descriptor state.  It is an idempotent register
        // write, not a transfer restart or configuration mutation.
        if(size==4 && target==value)
            for(unsigned n=1;n<=3;++n)
                if(address>=bases[n] && address<bases[n]+(n==1?16:12) && (channel[n][2]&0x01000000))return;
        for(unsigned n=1;n<=3;++n)
            if(address>=bases[n] && address<bases[n]+(n==1?16:12) && (channel[n][2]&0x01000000))
                throw std::runtime_error("active IOP SIF configuration mutation unsupported");
        for(unsigned n=4;n<=5;++n)
            if(address>=bases[n] && address<bases[n]+12 && (channel[n][2]&0x01000000))
                throw std::runtime_error("active SIO2 DMA configuration mutation unsupported");
        if((address&15)==8 && (value&0x01000000)) {
            // CDVDMAN first arms channel 3, then separately programs a CDVD
            // command. Retain the observed arm state, but never complete or
            // fabricate a transfer until a bounded media endpoint exists.
            if(!((address==0x1f801528 && value==0x01000701) ||
                 (address==0x1f801538 && value==0x41000300) ||
                 (address==0x1f801548 && value==0x01000201) ||
                 (address==0x1f801558 && value==0x01800200) ||
                 (address==0x1f8010b8 && value==0x41000200)))
                throw std::runtime_error("IOP DMA mode requires an implemented endpoint");
            if(target&0x01000000)throw std::runtime_error("active IOP DMA restart unsupported");
            if(address==0x1f801528)source_tag_loaded=source_header_sent=false;
        }
        if(size==2) {const unsigned shift=(address&2)*8;target=(target&~(0xffffu<<shift))|((value&0xffffu)<<shift);}
        else target=value;
    }
    // Oracle-observed first EE submission carries destination/end/IRQ and a
    // word count in the low half of its first qword. No captured bytes are used.
    template<class WriteQuad>
    void receive_sif1(std::uint64_t low,std::uint64_t high,WriteQuad write) {
        auto& c=channel[2];
        if(c[2]!=0x41000300)throw std::runtime_error("IOP SIF1 receive without supported active channel");
        if(receive_words) {
            auto& record=sif1_history[sif1_active_record];
            const std::array<std::uint32_t,4> lanes{std::uint32_t(low),std::uint32_t(low>>32),std::uint32_t(high),std::uint32_t(high>>32)};
            record.received_words+=unsigned(lanes.size());
            if(record.captured_words<=record.payload.size()-lanes.size()) {
                for(unsigned lane=0;lane<lanes.size();++lane)record.payload[record.captured_words+lane]=lanes[lane];
                record.captured_words+=unsigned(lanes.size());
            } else record.truncated=true;
            write(c[0],low,high);c[0]+=16;receive_words-=4;
        } else {
            const auto address=std::uint32_t(low),count=std::uint32_t(low>>32);
            if(high || (address&0x3f000003) || (count&0xff000003))
                throw std::runtime_error("unsupported IOP SIF1 destination tag or partial qword");
            c[0]=address&0xffffff;receive_words=count;
            {   // HG-DIAG-130 (HG_SIF1_WATCH=lo-hi hex): log SIF1 packets whose destination overlaps.
                static const std::pair<std::uint32_t,std::uint32_t> watch=[]{
                    const char* v=std::getenv("HG_SIF1_WATCH");if(!v)return std::pair<std::uint32_t,std::uint32_t>{0,0};
                    char* end=nullptr;const auto lo=std::uint32_t(std::strtoul(v,&end,16));
                    const auto hi=end && *end=='-'?std::uint32_t(std::strtoul(end+1,nullptr,16)):lo+256;
                    return std::pair<std::uint32_t,std::uint32_t>{lo,hi};}();
                static std::uint64_t packets=0;++packets;
                if(watch.second && watch.second>c[0] && watch.first<c[0]+count*4)
                    std::fprintf(stderr,"HG-DIAG-130 SIF1 packet %llu dest=0x%x bytes=%u\n",static_cast<unsigned long long>(packets),c[0],count*4);
            }
            receive_end=(address&0x80000000)!=0;receive_irq=(address&0x40000000)!=0;
            sif1_active_record=sif1_history_cursor++%sif1_history.size();
            sif1_history[sif1_active_record]={c[0],count,0,0,false,{}};
        }
        if(!receive_words) {
            if(receive_end || receive_irq)completed_channels|=1u<<10;
            if(receive_end)c[2]&=~0x01000000u;
        }
    }
    // Original SIFMAN builds 16-byte tags: source/IRQ/end, word count, EE tag,
    // EE destination. Six-word RPC reply capture validates retained high lanes
    // for a final two-word fragment after at least one complete payload qword.
    template<class ReadWord,class PushQuad>
    bool pump_sif0(ReadWord read,PushQuad push) {
        auto& c=channel[1];
        if(!(c[2]&0x01000000))return false;
        if(!source_tag_loaded) {
            // Original module tag array is word-aligned (offset0x118c), not
            // necessarily qword-aligned like an EE source-chain tag.
            if(c[3]%4)throw std::runtime_error("unaligned IOP SIF0 source tag");
            const auto address=read(c[3]),count=read(c[3]+4),ee_tag=read(c[3]+8),destination=read(c[3]+12);
            if((address&0x3f000000) || (count&0xff000000) || (address&3) || !count)
                throw std::runtime_error("unsupported IOP SIF0 address/count or partial-qword payload");
            if((ee_tag&0x7fff0000)!=0x10000000 || (ee_tag&0xffff)!=((count+3)/4) || (destination&0xe0000000))
                throw std::runtime_error("IOP SIF0 tag does not match independently decoded profile");
            source_address=address&0xffffff;source_words=count;
            last_sif0_tag={address,count,ee_tag,destination};last_sif0_payload_words=0;
            sif0_active_record=sif0_history_cursor++%sif0_history.size();
            sif0_history[sif0_active_record]={last_sif0_tag,{}};
            source_end=(address&0x80000000)!=0;source_irq=(address&0x40000000)!=0;
            source_header=std::uint64_t(ee_tag)|(std::uint64_t(destination)<<32);
            source_tag_loaded=true;source_header_sent=false;
        }
        if(!source_header_sent) {
            if(!push(source_header,0))return false;
            source_header_sent=true;c[0]=source_address;c[3]+=16;
        } else if(source_words) {
            // Partial-qword RPC results use the same native retained-lane
            // policy. Only the six-word fragment has oracle padding validation;
            // unused final lanes remain an explicit compatibility choice.
            auto lanes=source_lanes;const unsigned words=source_words<4?source_words:4;
            for(unsigned n=0;n<words;++n)lanes[n]=read(c[0]+n*4);
            // Publish payload diagnostics only after the FIFO accepts it.
            // A rejected push leaves this payload pending for a retry.
            if(!push(std::uint64_t(lanes[0])|(std::uint64_t(lanes[1])<<32),std::uint64_t(lanes[2])|(std::uint64_t(lanes[3])<<32)))return false;
            last_sif0_payload=lanes;last_sif0_payload_words=words;
            auto& record=sif0_history[sif0_active_record];
            record.total_words+=words;
            const auto available=record.payload.size()-record.payload_words;
            const auto captured=std::min<std::size_t>(available,words);
            for(std::size_t n=0;n<captured;++n)record.payload[record.payload_words+n]=lanes[n];
            record.payload_words+=unsigned(captured);record.truncated|=captured!=words;
            source_lanes=lanes;c[0]+=words*4;source_words-=words;
        }
        if(!source_words) {
            if(source_irq || source_end)completed_channels|=1u<<9;
            if(source_end)c[2]&=~0x01000000u;
            source_tag_loaded=false;
        }
        return true;
    }
};
}
