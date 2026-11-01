#include "hg/gs.hpp"
#include "hg/gs_acceleration.hpp"
#include "hg/gs_math.hpp"
#include "hg/gs_triangle_acceleration.hpp"
#include "hg/native_indexed_draw.hpp" // HG-ARCH-002: passive contract, no active native draw dispatch.
#include "hg/native_psmt8_pages.hpp" // HG-ARCH-004: passive page-footprint contract; no runtime dispatch.
#include "hg/native_psmt8_texture.hpp" // HG-ARCH-005: passive byte-staging contract; no runtime dispatch.
#include "hg/native_psmt8_cache.hpp" // HG-ARCH-006: passive epoch/cache contract; no production installs.
#include "hg/gs_raster_pages.hpp"
#include <algorithm>
#include <cstdio>
#include <chrono>
#include <bitset>
#include <cmath>
#include <cstring>

namespace hg {

namespace {
// Comparison switch only; both paths execute the same validated complete packet.
constexpr bool stream_gif_packets=false;
constexpr bool exact_run_clut_visibility=false;


// Derived from this project's structural decoder, which remains the reference.
// Validate the entire packet before any GS mutation, then consume transfers in
// their original order without materializing a second vector of command records.
void apply_complete_gif_packet(const std::uint8_t* bytes,std::size_t size,GsRegisterState& gs) {
    if(!bytes&&size)throw std::runtime_error("null GIF input");
    const auto complete_size=gif_packet_size(bytes,size);
    if(!complete_size)throw std::runtime_error("truncated GIF packet");
    if(*complete_size!=size)throw std::runtime_error("trailing GIF packet data");
    std::size_t offset=0;
    bool done=false;
    while(!done) {
        detail::gif_need(size,offset,16,"tag");
        const auto tag=detail::gif_u64le(bytes+offset);
        const auto descriptors=detail::gif_u64le(bytes+offset+8);
        offset+=16;
        const std::size_t nloop=tag&0x7fffu;
        const bool eop=(tag&(1ull<<15))!=0;
        const unsigned format=unsigned((tag>>58)&3u);
        const std::size_t encoded_nreg=(tag>>60)&15u,nreg=encoded_nreg?encoded_nreg:16;
        if(!nloop){done=eop;continue;}
        // Q resets on the first real transfer, not on an empty or all-NOP tag.
        bool reset_pending=true;
        const auto reset_q=[&] {if(reset_pending){gs.gif_q=0x3f800000;reset_pending=false;}};
        if((tag&(1ull<<46))&&format==0) {
            reset_q();gs.write_ad(0,(tag>>47)&0x7ffu);
        }
        if(format>=2) {
            detail::gif_need(size,offset,nloop*16,"IMAGE data");
            reset_q();
            for(std::size_t i=0;i<nloop;++i,offset+=16)
                gs.write_image_qword(detail::gif_u64le(bytes+offset),detail::gif_u64le(bytes+offset+8));
        } else {
            const auto entries=nloop*nreg;
            const auto data_bytes=format==0?entries*16:((entries+1)/2)*16;
            detail::gif_need(size,offset,data_bytes,format==0?"PACKED data":"REGLIST data");
            unsigned reg=0;
            for(std::size_t i=0;i<entries;++i) {
                const auto descriptor=std::uint8_t((descriptors>>(reg*4))&15u);
                if(++reg==nreg)reg=0; // Exactly i % nreg, without a variable divide.
                const auto at=offset+i*(format==0?16:8);
                if(descriptor==15)continue;
                reset_q();
                const auto low=detail::gif_u64le(bytes+at);
                if(format==0)gs.write_packed(descriptor,low,detail::gif_u64le(bytes+at+8));
                else gs.write_reglist(descriptor,low);
            }
            offset+=data_bytes;
        }
        done=eop;
    }
}
}

void GsRegisterState::read_clut_source(std::array<std::uint32_t,256>& source,unsigned count,unsigned cpsm,
                                      std::uint32_t cbp,std::uint32_t width,bool csm,bool four,
                                      std::uint32_t start_x,std::uint32_t start_y) const {
    if constexpr(!exact_run_clut_visibility) {
        for(unsigned index=0;index<count;++index) {
            const auto x=csm?start_x+index:four?(index&7):((index&7)|((index&0x10)>>1));
            const auto y=csm?start_y:four?(index>>3):(((index&8)>>3)|((index&0xe0)>>4));
            source[index]=read_transfer_pixel(cpsm,cbp*64,width,x,y);
        }
        return;
    }
    std::array<std::uint32_t,256> words{},unique{},values{};
    std::array<std::uint8_t,256> shifts{};
    for(unsigned index=0;index<count;++index) {
        const auto x=csm?start_x+index:four?(index&7):((index&7)|((index&0x10)>>1));
        const auto y=csm?start_y:four?(index>>3):(((index&8)>>3)|((index&0xe0)>>4));
        const auto word=cpsm==0?psmct32_word(cbp*64,width,x,y):
            cpsm==2?psmct16_word(cbp*64,width,x,y):psmct16s_word(cbp*64,width,x,y);
        words[index]=unique[index]=word;shifts[index]=std::uint8_t(cpsm==0?0:((x&8)?16:0));
    }
    std::sort(unique.begin(),unique.begin()+count);
    const auto unique_end=std::unique(unique.begin(),unique.begin()+count);
    const auto unique_count=std::size_t(unique_end-unique.begin());
    for(std::size_t first=0;first<unique_count;) {
        auto last=first+1;
        while(last<unique_count&&unique[last]==unique[last-1]+1)++last;
        const auto* visible=vram.read_span(unique[first],last-first);
        for(auto n=first;n<last;++n)values[n]=visible[n-first];
        first=last;
    }
    for(unsigned index=0;index<count;++index) {
        const auto at=std::lower_bound(unique.begin(),unique.begin()+unique_count,words[index])-unique.begin();
        const auto raw=values[std::size_t(at)];
        source[index]=cpsm==0?raw:std::uint16_t(raw>>shifts[index]);
    }
}

namespace {
void apply_gif_packet(const std::uint8_t* bytes,std::size_t size,GsRegisterState& gs) {
    if constexpr(stream_gif_packets)apply_complete_gif_packet(bytes,size,gs);
    else {
        const auto packet=decode_gif_packet(bytes,size);
        apply_gif_register_transfers(packet,gs);
    }
}
#if (defined(__BYTE_ORDER__)&&__BYTE_ORDER__==__ORDER_LITTLE_ENDIAN__)||defined(_M_X64)||defined(_M_IX86)||defined(_M_ARM64)
constexpr bool host_little_endian=true;
#else
constexpr bool host_little_endian=false;
#endif
}

void GifPath::submit_qword(std::uint64_t low,std::uint64_t high,GsRegisterState& gs) {
    if(forward) {track_forward(low);forward->forward_qword(low,high);return;}
    const auto offset=pending.size();
    pending.resize(offset+16);
    for(unsigned i=0;i!=8;++i) {
        pending[offset+i]=std::uint8_t(low>>(i*8));
        pending[offset+8+i]=std::uint8_t(high>>(i*8));
    }
    if(pending.size()>max_pending)throw std::runtime_error("GIF packet exceeds bounded path capacity");
    const auto apply=[&](std::size_t packet_size){apply_gif_packet(pending.data(),packet_size,gs);};
    // Bytes placed in pending other than by this path (tests) were not tracked.
    if(offset&&!forward_in_packet)rescan_pending=true;
    if(!rescan_pending) {
        // gif_packet_size rules, one qword at a time: pending ends exactly at the
        // packet end when the tracker leaves the packet.
        track_forward(low);
        if(forward_in_packet)return;
        rescan_pending=true;
        apply(pending.size());
        pending.clear();rescan_pending=false;
        return;
    }
    while(const auto packet_size=gif_packet_size(pending.data(),pending.size())) {
        apply(*packet_size);
        pending.erase(pending.begin(),pending.begin()+*packet_size);
    }
    if(pending.empty()) {
        rescan_pending=false;
        forward_in_packet=false;forward_expect_tag=true;forward_eop=false;forward_remaining=0;
    }
}

void GifPath::submit_words(const std::uint32_t* words,std::size_t qwords,GsRegisterState& gs) {
    if(forward) {
        for(std::size_t n=0;n<qwords;++n)track_forward(std::uint64_t(words[n*4])|(std::uint64_t(words[n*4+1])<<32));
        forward->forward_words(words,qwords);
        return;
    }
    const auto low=[&](std::size_t n){return std::uint64_t(words[n*4])|(std::uint64_t(words[n*4+1])<<32);};
    const auto high=[&](std::size_t n){return std::uint64_t(words[n*4+2])|(std::uint64_t(words[n*4+3])<<32);};
    if constexpr(!host_little_endian) {
        for(std::size_t n=0;n<qwords;++n)submit_qword(low(n),high(n),gs);
        return;
    }
    // Same as submit_qword per qword: a packet starting with pending empty is
    // tracked in the caller's buffer and applied from it when complete; an
    // incomplete suffix (or a faulting packet) is left in pending as before.
    const auto* bytes=reinterpret_cast<const std::uint8_t*>(words);
    for(std::size_t n=0;n<qwords;) {
        if(!pending.empty()||rescan_pending){submit_qword(low(n),high(n),gs);++n;continue;}
        const auto start=n,limit=std::min(qwords,start+max_pending/16);
        while(n<limit) {track_forward(low(n));++n;if(!forward_in_packet)break;}
        if(forward_in_packet){pending.assign(bytes+start*16,bytes+n*16);continue;}
        try {apply_gif_packet(bytes+start*16,(n-start)*16,gs);}
        catch(...) {pending.assign(bytes+start*16,bytes+n*16);rescan_pending=true;throw;}
    }
}

bool GsRegisterState::write_image_psmt8_fast(std::uint64_t low,std::uint64_t high) {
    const auto total=std::uint64_t(value[0x52]&0xfff)*((value[0x52]>>32)&0xfff);
    const auto width=std::uint32_t((value[0x50]>>48)&0x3f)*64;
    if(transfer_remaining<16||!transfer_width||!width||total<transfer_remaining||vram.size()!=1024*1024)return false;
    const auto written=total-transfer_remaining;
    const auto column=std::uint32_t(written%transfer_width);
    const auto x=transfer_x+column,y=transfer_y+std::uint32_t(written/transfer_width);
    if((x&15)||transfer_width-column<16)return false;
    const auto base=std::uint32_t((value[0x50]>>32)&0x3fff)*64;
    const auto first=psmt8_word(base,width,x,y);
    // HG-DIAG-017: masked stores preserve the other byte lanes, including
    // GPU-owned contents. A rejecting observer retains the checked CPU path.
    const auto shift=((y&2)>>1)*8,mask=0x00ff00ffu<<shift;
    std::array<std::uint32_t,8> indices{},values{};
    for(unsigned i=0;i<8;++i){
        indices[i]=(first&~15u)+((first&15)^((i&1)+((i&6)*2)));
        const auto pair=std::uint32_t((low>>(i*8))&255)|(std::uint32_t((high>>(i*8))&255)<<16);
        values[i]=pair<<shift;
    }
    if(!vram.try_masked_write(indices.data(),values.data(),8,mask)) {
        auto* words=vram.write_span(first&~15u,16);
        for(unsigned i=0;i<8;++i){auto& word=words[indices[i]-(first&~15u)];word=(word&~mask)|values[i];}
    }
    transfer_remaining-=16;
    if(!transfer_remaining){gif_to_vram_active=false;host_transfer_completed=true;completed_transfer_format=0x13;}
    return true;
}

void GsRegisterState::write_image_transfers(const GifTransfer* transfers,std::size_t count) {
    if(!transfers&&count)throw std::runtime_error("null GS IMAGE transfer run");
    constexpr std::size_t bulk_threshold=256;
    const auto format=unsigned((value[0x50]>>56)&0x3f);
    if(format!=0x13||count<bulk_threshold) {
        for(std::size_t n=0;n<count;++n)write_image_qword(transfers[n].low,transfers[n].high);
        return;
    }
    const auto total=std::uint64_t(value[0x52]&0xfff)*((value[0x52]>>32)&0xfff);
    const auto width=std::uint32_t((value[0x50]>>48)&0x3f)*64;
    const auto base=std::uint32_t((value[0x50]>>32)&0x3fff)*64;
    std::bitset<512> discard_pages,discarded_pages;
    const auto rectangle_width=std::uint32_t(value[0x52]&0xfff);
    const auto rectangle_height=std::uint32_t((value[0x52]>>32)&0xfff);
    if(transfer_remaining==total&&std::uint64_t(count)*16>=total&&base%2048==0&&
       width%128==0&&transfer_x%128==0&&transfer_y%64==0&&rectangle_width%128==0&&
       rectangle_height%64==0&&transfer_x+rectangle_width<=2048&&transfer_y+rectangle_height<=2048) {
        for(auto y=transfer_y;y<transfer_y+rectangle_height;y+=64)
            for(auto x=transfer_x;x<transfer_x+rectangle_width;x+=128)
                discard_pages.set(psmt8_word(base,width,x,y)/2048);
    }
    std::size_t n=0;
    while(n<count) {
        if(completed_host_payload()){++n;continue;}
        if(!gif_to_vram_active||transfer_remaining<16||!transfer_width||!width||
           total<transfer_remaining||vram.size()!=1024*1024) {
            write_image_qword(transfers[n].low,transfers[n].high);++n;continue;
        }
        const auto written=total-transfer_remaining;
        const auto column=std::uint32_t(written%transfer_width);
        const auto x=transfer_x+column,y=transfer_y+std::uint32_t(written/transfer_width);
        if((x&15)||transfer_width-column<16) {
            write_image_qword(transfers[n].low,transfers[n].high);++n;continue;
        }
        const auto first=psmt8_word(base,width,x,y),page=first/2048;
        auto* page_words=discard_pages[page]&&!discarded_pages[page]
            ?vram.write_discard_span(page*2048,2048):vram.write_span(page*2048,2048);
        discarded_pages.set(page);
        do {
            const auto low=transfers[n].low,high=transfers[n].high;
            const auto current_written=total-transfer_remaining;
            const auto current_column=std::uint32_t(current_written%transfer_width);
            const auto current_x=transfer_x+current_column;
            const auto current_y=transfer_y+std::uint32_t(current_written/transfer_width);
            if((current_x&15)||transfer_width-current_column<16)break;
            const auto current_first=psmt8_word(base,width,current_x,current_y);
            if(current_first/2048!=page)break;
            const auto shift=((current_y&2)>>1)*8,mask=0x00ff00ffu<<shift;
            for(unsigned i=0;i<8;++i) {
                const auto index=(current_first&~15u)+((current_first&15)^((i&1)+((i&6)*2)));
                const auto pair=std::uint32_t((low>>(i*8))&255)|(std::uint32_t((high>>(i*8))&255)<<16);
                auto& word=page_words[index-page*2048];
                word=(word&~mask)|(pair<<shift);
            }
            transfer_remaining-=16;++n;
            if(!transfer_remaining) {
                gif_to_vram_active=false;host_transfer_completed=true;completed_transfer_format=0x13;
                break;
            }
        } while(n<count);
    }
}

GsDisplayImage GsRegisterState::display_image() const {
        const auto enabled=unsigned(privileged_pmode&3);
        if(enabled!=1&&enabled!=2)
            throw std::runtime_error(enabled?"GS dual-circuit display merge is unimplemented":"GS display circuit is disabled");
        const auto circuit=enabled==1?0u:1u;
        const auto frame=privileged_dispfb[circuit],display=privileged_display[circuit];
        const auto base=std::uint32_t(frame&0x1ff)*2048;
        const auto buffer_width=std::uint32_t((frame>>9)&0x3f)*64;
        const auto format=unsigned((frame>>15)&0x1f);
        const auto source_x=std::uint32_t((frame>>32)&0x7ff),source_y=std::uint32_t((frame>>43)&0x7ff);
        const auto magnify_x=std::uint32_t((display>>23)&0xf)+1;
        const auto magnify_y=std::uint32_t((display>>27)&3)+1;
        const auto display_width=std::uint32_t((display>>32)&0xfff)+1;
        const auto display_height=std::uint32_t((display>>44)&0x7ff)+1;
        if(display_width%magnify_x||display_height%magnify_y)
            throw std::runtime_error("GS DISPLAY dimensions are not integral source pixels");
        const auto width=display_width/magnify_x,height=display_height/magnify_y;
        if(!width||!height||std::uint64_t(width)*height>1024u*1024u)
            throw std::runtime_error("GS display image exceeds bounded capacity");
        if(format!=0&&format!=1&&format!=2&&format!=10)
            throw std::runtime_error("unsupported GS display pixel format");
        if(!buffer_width)throw std::runtime_error("invalid GS display buffer width");
        GsDisplayImage result{width,height,std::uint32_t(display&0xfff),std::uint32_t((display>>12)&0x7ff),
                              magnify_x,magnify_y,std::vector<std::uint32_t>(std::size_t(width)*height)};
        // HG-DIAG-017: materialize committed GPU data once, without flushing
        // the guest pending draw list or marking any CPU page writable.
        if(vram.size()!=1024*1024)throw std::runtime_error("GS local memory is not initialized");
        // All supported display formats partition VRAM into aligned 2048-word
        // pages. Select rectangle tiles before their in-page permutations.
        const unsigned page_height=(format==0||format==1)?32:64;
        std::bitset<512> selected;
        for(unsigned y=source_y;y<source_y+height;y=(y/page_height+1)*page_height)
            for(unsigned x=source_x;x<source_x+width;x=(x/64+1)*64)
                selected.set((base/2048+(x&2047)/64+((y&2047)/page_height)*(buffer_width/64))&511);
        std::array<const std::uint32_t*,512> page_words{};
        for(unsigned first=0;first<512;){
            if(!selected[first]){++first;continue;}
            unsigned end=first+1;while(end<512&&selected[end])++end;
            const auto* words=vram.read_span(first*2048,(end-first)*2048);
            for(unsigned page=first;page<end;++page)page_words[page]=words+(page-first)*2048;
            first=end;
        }
        const auto read_word=[&](unsigned word){return page_words[word/2048][word%2048];};
        const auto expand=[](std::uint32_t value){return (value<<3)|(value>>2);};
        for(std::uint32_t y=0;y<height;++y)for(std::uint32_t x=0;x<width;++x) {
            std::uint32_t pixel;
            if(format==0)pixel=read_word(psmct32_word(base,buffer_width,source_x+x,source_y+y));
            else if(format==1)pixel=(read_word(psmct32_word(base,buffer_width,source_x+x,source_y+y))&0xffffffu)|0x80000000u;
            else {
                const auto word=format==2?psmct16_word(base,buffer_width,source_x+x,source_y+y):
                                           psmct16s_word(base,buffer_width,source_x+x,source_y+y);
                const auto packed=std::uint16_t(read_word(word)>>(((source_x+x)&8)?16:0));
                pixel=expand(packed&31)|(expand((packed>>5)&31)<<8)|(expand((packed>>10)&31)<<16)|
                      ((packed&0x8000)?0x80000000u:0u);
            }
            result.rgba[std::size_t(y)*width+x]=pixel;
        }
        return result;
    }

namespace {
// Draw-local immutable setup. Construct only inside an ordered CPU memory scope;
// texels remain live, so framebuffer/texture feedback keeps the scalar order.
struct DrawTexture {
    const GsRegisterState& gs;
    const std::uint32_t* words=nullptr;
    unsigned context,format=0,base=0,stride=0,function=0,clut_base=0;
    bool enabled=false,linear,tcc=false;
    struct Axis {
        unsigned mode,size,minimum,maximum;
        unsigned wrap(std::int32_t coordinate) const {
            if(mode==0)return std::uint32_t(coordinate)&(size-1);
            if(mode==1)return coordinate<0?0:std::uint32_t(coordinate)>=size?size-1:unsigned(coordinate);
            if(mode==2)return coordinate<int(minimum)?minimum:coordinate>int(maximum)?maximum:unsigned(coordinate);
            return (std::uint32_t(coordinate)&minimum)|maximum;
        }
    } u{},v{};
    DrawTexture(const GsRegisterState& state,unsigned ctx,bool filtering):gs(state),context(ctx),linear(filtering) {
        const auto tex0=gs.value[6+ctx],clamp=gs.value[8+ctx];
        format=unsigned((tex0>>20)&63);const auto tw=unsigned((tex0>>26)&15),th=unsigned((tex0>>30)&15);
        stride=unsigned((tex0>>14)&63)*64;
        if(tw>10||th>10||!stride||gs.vram.size()!=1024*1024)return;
        if(format!=0&&format!=0x13&&format!=0x14&&format!=0x1b)return;
        if(format==0x14) {
            if(((tex0>>51)&31)||((tex0>>56)&31)>=16)return; // CT32 CLUT, CSM1.
            clut_base=unsigned((tex0>>56)&15)*16;
        } else if(format!=0&&((tex0>>51)&0x3ff))return; // CT32 CLUT, CSM1, CSA=0 only.
        u={unsigned(clamp&3),1u<<tw,unsigned((clamp>>4)&1023),unsigned((clamp>>14)&1023)};
        v={unsigned((clamp>>2)&3),1u<<th,unsigned((clamp>>24)&1023),unsigned((clamp>>34)&1023)};
        if((u.mode==2&&u.minimum>u.maximum)||(v.mode==2&&v.minimum>v.maximum))return;
        base=unsigned(tex0&0x3fff)*64;function=unsigned((tex0>>35)&3);tcc=(tex0&(1ull<<34))!=0;
        words=gs.vram.data();enabled=true;
    }
    std::uint32_t sample(std::int32_t sx,std::int32_t sy) const {
        const auto x=u.wrap(sx),y=v.wrap(sy);
        std::uint32_t texel;
        if(format==0x13) {
            // Factor this project's PSMT8 page/block/column formula. The Y
            // permutation flips bit3 of the low word, so combine with XOR.
            static constexpr auto xs=[] {
                std::array<std::uint16_t,128> result{};
                for(unsigned i=0;i<128;++i)result[i]=std::uint16_t(
                    (i&1)+(i&6)*2+(i&16)*4+(i&32)*8+(i&64)*16);
                return result;
            }();
            static constexpr auto ys=[] {
                std::array<std::uint16_t,64> result{};
                for(unsigned i=0;i<64;++i)result[i]=std::uint16_t(
                    (i&1)*2+(((i>>1)^(i>>2))&1)*8+(i&12)*4+(i&16)*8+(i&32)*16);
                return result;
            }();
            const auto address=(base+((x>>7)+(y>>6)*(stride>>7))*2048+(xs[x&127]^ys[y&63]))&0xfffffu;
            const auto lane=((y&2)>>1)|((x&8)>>2);
            texel=(words[address]>>(lane*8))&255;
        } else if(format==0x14) {
            const auto address=GsRegisterState::psmt4_word(base,stride,x,y);
            const auto lane=((y&2)>>1)|((x&24)>>2);
            texel=clut_base+((words[address]>>(lane*4))&15);
        } else {
            texel=words[GsRegisterState::psmct32_word(base,stride,x,y)];
            if(format==0)return texel;
            texel>>=24;
        }
        if(!gs.clut_valid[texel]||!gs.clut_valid[texel+256])
            throw std::runtime_error("GS indexed texture uses an unloaded CLUT entry");
        return std::uint32_t(gs.clut[texel])|(std::uint32_t(gs.clut[texel+256])<<16);
    }
    std::uint32_t shade(std::int32_t x,std::int32_t y,std::uint32_t fragment) const {
        if(!enabled)return linear?gs.shade_linear_tex0(context,x,y,fragment):gs.shade_point_tex0(context,x,y,fragment);
        std::uint32_t texel;
        if(!linear)texel=sample(x,y);
        else {
            const auto lower=[](std::int32_t c) {const auto n=std::int64_t(c)-8;return std::int32_t(n>=0?n/16:-((-n+15)/16));};
            const auto sx=lower(x),sy=lower(y);
            const auto a=unsigned(std::int64_t(x)-8-std::int64_t(sx)*16),b=unsigned(std::int64_t(y)-8-std::int64_t(sy)*16);
            const auto c0=sample(sx,sy),c1=a?sample(sx+1,sy):0,c2=b?sample(sx,sy+1):0,c3=a&&b?sample(sx+1,sy+1):0;
            const auto w0=(16-a)*(16-b),w1=a*(16-b),w2=(16-a)*b,w3=a*b;
            const auto rb=(c0&0x00ff00ffu)*w0+(c1&0x00ff00ffu)*w1+(c2&0x00ff00ffu)*w2+(c3&0x00ff00ffu)*w3;
            const auto ga=((c0>>8)&0x00ff00ffu)*w0+((c1>>8)&0x00ff00ffu)*w1+((c2>>8)&0x00ff00ffu)*w2+((c3>>8)&0x00ff00ffu)*w3;
            texel=((rb>>8)&0x00ff00ffu)|(ga&0xff00ff00u);
        }
        return gs_texture_function(texel,fragment,function,tcc);
    }
};
struct PerspectiveSum {
    bool negative=false,zero=true;
    std::uint64_t magnitude=0;
    int scale=0;
    std::uint64_t high=0;
};
struct WideMagnitude {std::uint64_t low=0,high=0;};
int compare_wide(WideMagnitude a,WideMagnitude b) {
    if(a.high!=b.high)return a.high<b.high?-1:1;
    if(a.low!=b.low)return a.low<b.low?-1:1;
    return 0;
}
WideMagnitude subtract_wide(WideMagnitude a,WideMagnitude b) {
    return {a.low-b.low,a.high-b.high-std::uint64_t(a.low<b.low)};
}
bool add_wide(WideMagnitude& a,WideMagnitude b) {
    const auto low=a.low+b.low,carry=std::uint64_t(low<a.low);
    const auto maximum=(std::numeric_limits<std::uint64_t>::max)();
    if(b.high>maximum-carry||a.high>maximum-b.high-carry)return false;
    a={low,a.high+b.high+carry};return true;
}
bool shift_wide_left(WideMagnitude& value,unsigned shift) {
    if(!shift)return true;
    if(shift>=128)return !value.low&&!value.high;
    if(shift>=64) {
        if(value.high||(shift>64&&value.low>>(128-shift)))return false;
        value={0,value.low<<(shift-64)};return true;
    }
    if(value.high>>(64-shift))return false;
    value={value.low<<shift,(value.high<<shift)|(value.low>>(64-shift))};return true;
}
bool shift_wide_right(WideMagnitude& value,unsigned shift) {
    if(!shift)return false;
    if(shift>=128) {const bool discarded=value.low||value.high;value={};return discarded;}
    if(shift>=64) {
        const auto right=shift-64;
        const bool discarded=value.low||(right&&((value.high&((std::uint64_t(1)<<right)-1))!=0));
        value={value.high>>right,0};return discarded;
    }
    const bool discarded=(value.low&((std::uint64_t(1)<<shift)-1))!=0;
    value={(value.low>>shift)|(value.high<<(64-shift)),value.high>>shift};return discarded;
}
WideMagnitude multiply_u64_u24(std::uint64_t value,std::uint32_t multiplier) {
    const auto low=(value&0xffffffffu)*multiplier;
    const auto upper=(value>>32)*multiplier;
    const auto shifted=upper<<32,result=low+shifted;
    return {result,(upper>>32)+std::uint64_t(result<low)};
}
bool multiply_wide_u32(WideMagnitude value,std::uint32_t multiplier,WideMagnitude& result) {
    const auto a=multiply_u64_u24(value.low,multiplier);
    const auto b=multiply_u64_u24(value.high,multiplier);
    if(b.high)return false;
    const auto high=a.high+b.low;
    if(high<a.high)return false;
    result={a.low,high};return true;
}
std::int32_t perspective_from_sums(const PerspectiveSum& numerator_sum,
                                 const PerspectiveSum& denominator_sum,unsigned dimension_shift) {
    if(denominator_sum.zero)throw std::runtime_error("GS perspective STQ division by zero");
    if(numerator_sum.zero)return 0;
    if(numerator_sum.high||denominator_sum.high) {
        WideMagnitude numerator{numerator_sum.magnitude,numerator_sum.high};
        const WideMagnitude denominator{denominator_sum.magnitude,denominator_sum.high};
        auto shift=numerator_sum.scale-denominator_sum.scale+int(dimension_shift);
        bool has_remainder=false;
        if(shift<0) {has_remainder=shift_wide_right(numerator,unsigned(-shift));shift=0;}
        else if(!shift_wide_left(numerator,unsigned(shift)))
            throw std::runtime_error("GS perspective STQ exponent spread requires validation");
        const bool negative=numerator_sum.negative!=denominator_sum.negative;
        const std::uint64_t limit=negative?0x80000000ull:0x7fffffffull;
        std::uint64_t quotient=0,remainder=0;
        if(!denominator.high) {
            if(!gs_divide_128(numerator.high,numerator.low,denominator.low,quotient,remainder)||quotient>limit)
                throw std::runtime_error("GS perspective STQ coordinate outside signed texel range");
            has_remainder=has_remainder||remainder!=0;
        } else {
            std::uint32_t lower=0,upper=std::uint32_t(limit);
            while(lower<upper) {
                const auto middle=lower+(upper-lower+1)/2;
                WideMagnitude product{};
                if(!multiply_wide_u32(denominator,middle,product)||compare_wide(product,numerator)>0)
                    upper=middle-1;
                else lower=middle;
            }
            quotient=lower;WideMagnitude product{};
            if(!multiply_wide_u32(denominator,lower,product))
                throw std::runtime_error("GS perspective STQ coordinate outside signed texel range");
            const auto rest=subtract_wide(numerator,product);
            has_remainder=has_remainder||rest.low||rest.high;
        }
        auto magnitude=quotient;
        if(negative&&has_remainder) {
            if(magnitude==limit)throw std::runtime_error("GS perspective STQ coordinate outside signed texel range");
            ++magnitude;
        }
        return negative?std::int32_t(-std::int64_t(magnitude)):std::int32_t(magnitude);
    }
    const auto numerator=numerator_sum.magnitude,denominator=denominator_sum.magnitude;
    const auto shift=numerator_sum.scale-denominator_sum.scale+int(dimension_shift);
    const bool negative=numerator_sum.negative!=denominator_sum.negative;
    const std::uint64_t limit=negative?0x80000000ull:0x7fffffffull;

    // Divide before applying the power-of-two exponent. For positive shifts,
    // extend the quotient one bit at a time while doubling only the remainder.
    // This is exact long division and never constructs the potentially >64-bit
    // numerator that caused false overflow on clipped perspective triangles.
    std::uint64_t quotient=0,remainder=0;
    bool has_remainder=false;
    if(shift>=0&&gs_shifted_divide(numerator,denominator,unsigned(shift),quotient,remainder)) {
        // Exact native division also covers a two-word shifted numerator when
        // the quotient fits. The helper guards the host instruction's bounds.
        has_remainder=remainder!=0;
    } else if(shift>=0) {
        quotient=numerator/denominator;remainder=numerator%denominator;
        if(quotient>limit)throw std::runtime_error("GS perspective STQ coordinate outside signed texel range");
        for(int bit=0;bit<shift;++bit) {
            const bool carry=remainder>=denominator-remainder;
            if(carry)remainder-=denominator-remainder;
            else remainder+=remainder;
            if(quotient>limit/2)throw std::runtime_error("GS perspective STQ coordinate outside signed texel range");
            quotient*=2;
            if(carry) {
                if(quotient==limit)throw std::runtime_error("GS perspective STQ coordinate outside signed texel range");
                ++quotient;
            }
        }
        has_remainder=remainder!=0;
    } else {
        quotient=numerator/denominator;remainder=numerator%denominator;has_remainder=remainder!=0;
        const auto right=unsigned(-shift);
        if(right>=64) {
            quotient=0;has_remainder=numerator!=0;
        } else {
            const auto mask=(std::uint64_t(1)<<right)-1;
            has_remainder=has_remainder||(quotient&mask)!=0;
            quotient>>=right;
        }
    }
    auto magnitude=quotient;
    if(negative&&has_remainder) {
        if(magnitude==limit)throw std::runtime_error("GS perspective STQ coordinate outside signed texel range");
        ++magnitude;
    }
    if(magnitude>limit)throw std::runtime_error("GS perspective STQ coordinate outside signed texel range");
    return negative?std::int32_t(-std::int64_t(magnitude)):std::int32_t(magnitude);
}

enum class PreparedPerspectiveFailure {none,dimension,nonfinite,shift,product};
struct PreparedCoordinate {
    std::array<std::int64_t,3> coefficient{};
    int scale=0;
    PreparedPerspectiveFailure failure=PreparedPerspectiveFailure::none;
    bool prepare(const std::array<std::uint32_t,3>& values,std::uint64_t area,bool require_area_bound=true) {
        std::array<std::uint32_t,3> mantissa{};
        std::array<int,3> exponent{};bool any=false;
        for(unsigned i=0;i<3;++i) {
            const auto e=(values[i]>>23)&255,fraction=values[i]&0x7fffff;
            if(e==255){failure=PreparedPerspectiveFailure::nonfinite;return false;} // Let the checked scalar path report the fault.
            mantissa[i]=e?(fraction|0x800000u):fraction;
            exponent[i]=e?int(e)-150:-149;
            if(mantissa[i]){if(!any||exponent[i]<scale)scale=exponent[i];any=true;}
        }
        constexpr auto limit=std::uint64_t((std::numeric_limits<std::int64_t>::max)());
        for(unsigned i=0;i<3;++i)if(mantissa[i]) {
            const auto shift=unsigned(exponent[i]-scale);
            if(shift>=63||mantissa[i]>(limit>>shift)){failure=PreparedPerspectiveFailure::shift;return false;}
            const auto magnitude=std::uint64_t(mantissa[i])<<shift;
            // Nonnegative covered weights sum to area. This bounds every
            // product and every partial signed sum, including zero-weight edges.
            if(require_area_bound&&!gs_unsigned_product_fits_i64(magnitude,area)){failure=PreparedPerspectiveFailure::product;return false;}
            coefficient[i]=(values[i]>>31)?-std::int64_t(magnitude):std::int64_t(magnitude);
        }
        return true;
    }
    PerspectiveSum sum(std::int64_t a,std::int64_t b,std::int64_t c) const {
        const auto total=coefficient[0]*a+coefficient[1]*b+coefficient[2]*c;
        if(!total)return {};
        return {total<0,false,std::uint64_t(total<0?-total:total),scale};
    }
};
struct PreparedTriangleStq {
    PreparedCoordinate s,t,q;
    bool ready=false;unsigned shift_u=0,shift_v=0;
    PreparedPerspectiveFailure failure=PreparedPerspectiveFailure::none;
    void prepare(const GsVertex& a,const GsVertex& b,const GsVertex& c,
                 std::uint64_t area,std::uint64_t tex0,bool linear,bool require_area_bound=true) {
        const auto tw=unsigned((tex0>>26)&15),th=unsigned((tex0>>30)&15);
        if(tw>10||th>10){failure=PreparedPerspectiveFailure::dimension;return;}
        shift_u=tw+(linear?4:0);shift_v=th+(linear?4:0);
        if(!s.prepare({std::uint32_t(a.st),std::uint32_t(b.st),std::uint32_t(c.st)},area,require_area_bound)){failure=s.failure;return;}
        if(!t.prepare({std::uint32_t(a.st>>32),std::uint32_t(b.st>>32),std::uint32_t(c.st>>32)},area,require_area_bound)){failure=t.failure;return;}
        if(!q.prepare({a.q,b.q,c.q},area,require_area_bound)){failure=q.failure;return;}
        ready=true;
    }
    std::pair<std::int32_t,std::int32_t> sample(std::int64_t a,std::int64_t b,std::int64_t c) const {
        const auto denominator=q.sum(a,b,c);
        return {perspective_from_sums(s.sum(a,b,c),denominator,shift_u),
                perspective_from_sums(t.sum(a,b,c),denominator,shift_v)};
    }
};
}

std::int32_t gs_stq_perspective_texel_coordinate(const std::array<std::uint32_t,3>& coordinates,
                                                  const std::array<std::uint32_t,3>& qs,
                                                  const std::array<std::int64_t,3>& weights,
                                                  std::uint32_t size,unsigned fractional_bits) {
    if(!size||size>1024||(size&(size-1)))throw std::runtime_error("invalid GS perspective STQ dimension");
    if(fractional_bits!=0&&fractional_bits!=4)throw std::runtime_error("invalid GS texture coordinate precision");
    struct Part {bool negative,zero;std::uint32_t mantissa;int scale;};
    const auto decode=[](std::uint32_t bits) {
        const auto exponent=(bits>>23)&0xffu,fraction=bits&0x7fffffu;
        if(exponent==0xff)throw std::runtime_error("non-finite GS STQ coordinate");
        if(!exponent)return Part{(bits>>31)!=0,fraction==0,fraction,-149};
        return Part{(bits>>31)!=0,false,fraction|0x800000u,int(exponent)-150};
    };
    using Sum=PerspectiveSum;
    const auto weighted_sum=[&](const std::array<std::uint32_t,3>& values) {
        std::array<Part,3> parts{decode(values[0]),decode(values[1]),decode(values[2])};
        bool any=false;int common_scale=0;
        for(unsigned index=0;index!=3;++index)if(weights[index]&&!parts[index].zero) {
            if(!any||parts[index].scale<common_scale)common_scale=parts[index].scale;
            any=true;
        }
        if(!any)return Sum{};
        std::int64_t total=0;bool needs_wide=false;
        for(unsigned index=0;index!=3;++index)if(weights[index]&&!parts[index].zero) {
            if(weights[index]==(std::numeric_limits<std::int64_t>::min)())
                throw std::runtime_error("GS perspective STQ weight overflow");
            const bool negative=parts[index].negative!=(weights[index]<0);
            const auto weight=std::uint64_t(weights[index]<0?-weights[index]:weights[index]);
            // A decoded mantissa is strictly below 2^24. Normal GS edge
            // weights fit this conservative bound, avoiding an integer divide
            // per operand while retaining the exact check for larger weights.
            constexpr auto safe_weight=std::uint64_t((std::numeric_limits<std::int64_t>::max)())>>24;
            if(weight>safe_weight&&weight>std::uint64_t((std::numeric_limits<std::int64_t>::max)())/parts[index].mantissa) {
                needs_wide=true;break;
            }
            auto magnitude=weight*parts[index].mantissa;
            const auto shift=unsigned(parts[index].scale-common_scale);
            if(shift>=63||magnitude>(std::uint64_t((std::numeric_limits<std::int64_t>::max)())>>shift)) {
                needs_wide=true;break;
            }
            const auto term=std::int64_t(magnitude<<shift);
            if(negative) {
                if(total<(std::numeric_limits<std::int64_t>::min)()+term) {needs_wide=true;break;}
                total-=term;
            } else {
                if(total>(std::numeric_limits<std::int64_t>::max)()-term) {needs_wide=true;break;}
                total+=term;
            }
        }
        if(needs_wide) {
            WideMagnitude total_wide{};bool total_negative=false;
            for(unsigned index=0;index!=3;++index)if(weights[index]&&!parts[index].zero) {
                if(weights[index]==(std::numeric_limits<std::int64_t>::min)())
                    throw std::runtime_error("GS perspective STQ weight overflow");
                const bool negative=parts[index].negative!=(weights[index]<0);
                const auto weight=std::uint64_t(weights[index]<0?-weights[index]:weights[index]);
                auto term=multiply_u64_u24(weight,parts[index].mantissa);
                if(!shift_wide_left(term,unsigned(parts[index].scale-common_scale)))
                    throw std::runtime_error("GS perspective STQ exponent spread requires validation");
                if(!term.low&&!term.high)continue;
                if(!total_wide.low&&!total_wide.high) {total_wide=term;total_negative=negative;continue;}
                if(total_negative==negative) {
                    if(!add_wide(total_wide,term))throw std::runtime_error("GS perspective STQ sum overflow");
                } else {
                    const auto comparison=compare_wide(total_wide,term);
                    if(!comparison){total_wide={};total_negative=false;}
                    else if(comparison>0)total_wide=subtract_wide(total_wide,term);
                    else {total_wide=subtract_wide(term,total_wide);total_negative=negative;}
                }
            }
            if(!total_wide.low&&!total_wide.high)return Sum{};
            return Sum{total_negative,false,total_wide.low,common_scale,total_wide.high};
        }
        if(!total)return Sum{};
        if(total==(std::numeric_limits<std::int64_t>::min)())
            throw std::runtime_error("GS perspective STQ sum overflow");
        return Sum{total<0,false,std::uint64_t(total<0?-total:total),common_scale};
    };
    const auto numerator_sum=weighted_sum(coordinates),denominator_sum=weighted_sum(qs);
    unsigned dimension_shift=fractional_bits;
    for(auto dimension=size;dimension>1;dimension>>=1)++dimension_shift;
    return perspective_from_sums(numerator_sum,denominator_sum,dimension_shift);
}

bool GsRegisterState::linear_texture_filter(unsigned context) const {
    if(context>1)throw std::runtime_error("invalid GS texture context");
    const auto tex1=value[0x14+context];
    const auto magnify=unsigned((tex1>>5)&1),minify=unsigned((tex1>>6)&7);
    if(minify>1||minify!=magnify)
        throw std::runtime_error("unimplemented GS texture LOD/filter selection");
    return magnify!=0;
}

std::uint32_t GsRegisterState::shade_linear_tex0(unsigned context,std::int32_t u_fixed,
                                               std::int32_t v_fixed,std::uint32_t fragment) const {
    // GS Manual6.0 pp28/58: 12.4 texel coordinates have centers at n+0.5.
    // Wrap each of the four neighbors independently, and retain live VRAM reads.
    const auto lower=[](std::int64_t coordinate) {
        coordinate-=8;
        return std::int32_t(coordinate>=0?coordinate/16:-((-coordinate+15)/16));
    };
    const auto u=lower(u_fixed),v=lower(v_fixed);
    const auto a=unsigned(std::int64_t(u_fixed)-8-std::int64_t(u)*16);
    const auto b=unsigned(std::int64_t(v_fixed)-8-std::int64_t(v)*16);
    if(!a&&!b)return shade_point_tex0(context,u,v,fragment);
    const std::array<std::uint32_t,4> colors{
        point_sample_tex0(context,u,v),a?point_sample_tex0(context,u+1,v):0,
        b?point_sample_tex0(context,u,v+1):0,a&&b?point_sample_tex0(context,u+1,v+1):0};
    const std::array<unsigned,4> weights{(16-a)*(16-b),a*(16-b),(16-a)*b,a*b};
    std::uint32_t texel=0;
    for(unsigned shift=0;shift<32;shift+=8) {
        unsigned sum=0;
        for(unsigned i=0;i<4;++i)sum+=((colors[i]>>shift)&255)*weights[i];
        texel|=(sum>>8)<<shift;
    }
    const auto tex0=value[6+context];
    return gs_texture_function(texel,fragment,unsigned((tex0>>35)&3),(tex0&(1ull<<34))!=0);
}

std::uint32_t gs_wrap_texture_coordinate(std::int32_t coordinate,std::uint32_t size,
                                         std::uint32_t mode,std::uint32_t minimum,
                                         std::uint32_t maximum) {
    if(!size||size>1024)throw std::runtime_error("invalid GS texture dimension");
    switch(mode) {
    case 0: { // REPEAT uses the nominal TEX0 dimension.
        const auto wrapped=std::int64_t(coordinate)%std::int64_t(size);
        return std::uint32_t(wrapped<0?wrapped+size:wrapped);
    }
    case 1: // CLAMP uses the nominal texture edge.
        return coordinate<0?0:coordinate>=std::int64_t(size)?size-1:std::uint32_t(coordinate);
    case 2: // REGION_CLAMP uses the independent 10-bit MIN/MAX fields.
        if(minimum>maximum||maximum>1023)throw std::runtime_error("invalid GS REGION_CLAMP limits");
        return coordinate<std::int64_t(minimum)?minimum:
               coordinate>std::int64_t(maximum)?maximum:std::uint32_t(coordinate);
    case 3: // REGION_REPEAT: MIN is mask, MAX is fixed bits; both are 10-bit.
        if(minimum>1023||maximum>1023)throw std::runtime_error("invalid GS REGION_REPEAT fields");
        return (std::uint32_t(coordinate)&minimum)|maximum;
    default: throw std::runtime_error("invalid GS texture wrap mode");
    }
}

std::uint32_t GsRegisterState::texel_from_tex0(unsigned context,std::uint32_t x,std::uint32_t y) const {
    if(context>1)throw std::runtime_error("invalid GS texture context");
    const auto tex0=value[6+context];
    const auto base=std::uint32_t(tex0&0x3fff)*64;
    const auto width_words=std::uint32_t((tex0>>14)&0x3f)*64;
    const auto psm=std::uint32_t((tex0>>20)&0x3f);
    const auto tw=unsigned((tex0>>26)&0xf),th=unsigned((tex0>>30)&0xf);
    if(tw>10||th>10||!width_words)throw std::runtime_error("invalid TEX0 dimensions");
    // Indexed addressing retains whole 128-pixel pages per row, including
    // odd TBW units. Independent synthetic sampling observations and the
    // physical-marker regressions cover the truncation/alias cases.
    const std::uint32_t width=1u<<tw,height=1u<<th;
    if(std::uint64_t(width)*height>1024u*1024u)throw std::runtime_error("TEX0 texture exceeds bounded extraction capacity");
    // UV and CLAMP operate in a 10-bit integer coordinate domain. REGION_CLAMP
    // and REGION_REPEAT may intentionally select texels outside nominal TW/TH;
    // TBP0/TBW plus the storage layout still determine the resulting VRAM address.
    if(x>=1024||y>=1024)throw std::runtime_error("TEX0 texel outside 10-bit coordinate range");
    // TEXA controls the expanded alpha of 24-bit/RGBA16 texels (including
    // RGBA16 values reached through a CLUT). See GS User's Manual 3.4.6.
    const auto texa=value[0x3b];
    const auto ta0=std::uint32_t(texa&0xff),ta1=std::uint32_t((texa>>32)&0xff);
    const auto aem=(texa&(1ull<<15))!=0;
    const auto rgba16=[=](std::uint16_t packed) {
        const auto expand=[](std::uint16_t channel) {return std::uint32_t((channel<<3)|(channel>>2));};
        const auto rgb=packed&0x7fff;
        return expand(packed&31)|(expand((packed>>5)&31)<<8)|(expand((packed>>10)&31)<<16)|
               ((aem&&!rgb)?0:((packed&0x8000)?ta1:ta0)<<24);
    };
    const auto rgba24=[=](std::uint32_t packed) {
        const auto rgb=packed&0xffffff;
        return rgb|((aem&&!rgb)?0:ta0<<24);
    };
    const auto indexed_clut=[&](std::uint8_t index) {
        const auto cpsm=std::uint32_t((tex0>>51)&0xf),csm=std::uint32_t((tex0>>55)&1),csa=std::uint32_t((tex0>>56)&0x1f);
        if(cpsm!=0&&cpsm!=2&&cpsm!=10)throw std::runtime_error("unsupported GS CLUT pixel format");
        if(csm&&(cpsm==0||csa))throw std::runtime_error("CSM2 requires a 16-bit CLUT and CSA=0");
        const auto entry=csa*16+index,capacity=cpsm==0?256u:512u;
        if(entry>=capacity)throw std::runtime_error("GS CLUT index exceeds temporary-buffer capacity");
        if(!clut_valid[entry]||(cpsm==0&&!clut_valid[entry+256]))
            throw std::runtime_error("GS indexed texture uses an unloaded CLUT entry");
        if(cpsm==0)return std::uint32_t(clut[entry])|(std::uint32_t(clut[entry+256])<<16);
        return rgba16(clut[entry]);
    };
    if(psm==0)return pixel32(base,width_words,x,y);
    if(psm==1)return rgba24(pixel32(base,width_words,x,y));
    if(psm==2)return rgba16(pixel16(base,width_words,x,y));
    if(psm==10)return rgba16(pixel16s(base,width_words,x,y));
    if(psm==0x13)return indexed_clut(pixel8(base,width_words,x,y));
    if(psm==0x14)return indexed_clut(pixel4(base,width_words,x,y));
    if(psm==0x1b)return indexed_clut(pixel32(base,width_words,x,y)>>24);
    if(psm==0x24)return indexed_clut((pixel32(base,width_words,x,y)>>24)&15);
    if(psm==0x2c)return indexed_clut(pixel32(base,width_words,x,y)>>28);
    // PSMZ24 uses the independently implemented Z32/Z24 local-memory swizzle.
    // Its 24 stored bits enter the same 24-bit TEXA expansion as PSMCT24.
    if(psm==0x31)return rgba24(read_transfer_pixel(0x31,base,width_words,x,y));
    throw std::runtime_error("TEX0 requires unimplemented texture format");
}

void GsRegisterState::write_frame_pixel(unsigned context,std::uint32_t x,std::uint32_t y,
                                        const GsBlendResult& color,std::uint32_t additional_mask) {
    if(context>1)throw std::runtime_error("invalid GS frame context");
    const auto frame=value[0x4c+context];
    const auto psm=std::uint32_t((frame>>24)&0x3f);
    if(psm!=0&&psm!=1&&psm!=2&&psm!=10&&psm!=0x31)
        throw std::runtime_error("unimplemented GS frame-buffer pixel format");
    const auto base=std::uint32_t(frame&0x1ff)*2048;
    const auto width=std::uint32_t((frame>>16)&0x3f)*64;
    if(!width)throw std::runtime_error("invalid GS frame-buffer width");
    const auto convert=[&](std::int32_t channel) {
        if(value[0x46]&1)return std::uint32_t(channel<0?0:channel>255?255:channel);
        return std::uint32_t(channel)&0xff;
    };
    const auto rgb=convert(color.r)|(convert(color.g)<<8)|(convert(color.b)<<16);
    if(psm==2||psm==10) {
        const auto dither=[&] {
            if(!(value[0x45]&1))return 0;
            const auto raw=std::uint32_t((value[0x44]>>(((y&3)*4+(x&3))*4))&7);
            return raw&4?std::int32_t(raw)-8:std::int32_t(raw);
        }();
        const auto convert_dithered=[&](std::int32_t channel) {
            channel+=dither;
            if(value[0x46]&1)return std::uint32_t(channel<0?0:channel>255?255:channel);
            return std::uint32_t(channel)&0xff;
        };
        const auto rgb5=(convert_dithered(color.r)>>3&31)|((convert_dithered(color.g)>>3&31)<<5)|
                        ((convert_dithered(color.b)>>3&31)<<10);
        const auto packed=rgb5|
                          (((color.a&0x80)||(value[0x4a+context]&1))?0x8000u:0u);
        ensure_vram();
        auto& word=vram[(psm==2?psmct16_word:psmct16s_word)(base,width,x,y)];
        const auto shift=(x&8)?16:0;
        const auto source_mask=std::uint32_t(frame>>32)|additional_mask;
        const auto mask=std::uint16_t(((source_mask>>3)&0x1f)|((source_mask>>6)&0x3e0)|
                                      ((source_mask>>9)&0x7c00)|((source_mask>>16)&0x8000));
        const auto old=std::uint16_t(word>>shift);
        const auto written=std::uint16_t((old&mask)|(packed&~mask));
        word=(word&~(0xffffu<<shift))|(std::uint32_t(written)<<shift);
        return;
    }
    const auto alpha=std::uint32_t(color.a)|((value[0x4a+context]&1)?0x80u:0u);
    const auto pixel=rgb|(alpha<<24);
    ensure_vram();
    const auto word=psm==0x31?psmz32_word(base,width,x,y):psmct32_word(base,width,x,y);
    if(psm==1||psm==0x31) {
        // FRAME permits PSMZ24. It has the same 24-bit payload as PSMCT24,
        // with the documented Z24 page/block order; the unused byte is kept.
        const auto mask=(std::uint32_t(frame>>32)|additional_mask)&0xffffffu;
        vram[word]=(vram[word]&0xff000000u)|(vram[word]&mask)|(rgb&~mask);
    } else {
        const auto mask=std::uint32_t(frame>>32)|additional_mask;
        vram[word]=(vram[word]&mask)|(pixel&~mask);
    }
}

namespace {
// Both entry paths retain their checked tests. Between a successful depth test
// and this commit no guest state changes, so the validated destination is reusable.
void blend_frame_pixel(GsRegisterState& gs,unsigned context,std::uint32_t x,std::uint32_t y,
                       std::uint32_t source,std::uint32_t destination,
                       std::uint16_t primitive_state,std::uint32_t additional_mask) {
    const auto effective_primitive=primitive_state==0xffff?std::uint16_t(gs.value[0]):primitive_state;
    const auto blend=(effective_primitive&(1u<<6))!=0 && (!(gs.value[0x49]&1)||(source&0x80000000u)!=0);
    const auto color=blend?gs_alpha_blend(source,destination,gs.value[0x42+context]):
        GsBlendResult{std::int32_t(source&0xff),std::int32_t((source>>8)&0xff),
                      std::int32_t((source>>16)&0xff),std::uint8_t(source>>24)};
    gs.write_frame_pixel(context,x,y,color,additional_mask);
}
}

bool GsRegisterState::draw_frame_pixel(unsigned context,std::uint32_t x,std::uint32_t y,
                                       std::uint32_t source,std::uint16_t primitive_state) {
    if(context>1)throw std::runtime_error("invalid GS frame context");
    const auto frame=value[0x4c+context];
    const auto psm=std::uint32_t((frame>>24)&0x3f);
    if(psm!=0&&psm!=1&&psm!=2&&psm!=10&&psm!=0x31)
        throw std::runtime_error("unimplemented GS frame-buffer pixel format");
    const auto base=std::uint32_t(frame&0x1ff)*2048,width=std::uint32_t((frame>>16)&0x3f)*64;
    if(!width)throw std::runtime_error("invalid GS frame-buffer width");
    ensure_vram();
    const auto expand=[](std::uint32_t channel) {return (channel<<3)|(channel>>2);};
    const auto destination=[&] {
        if(psm==2||psm==10) {
            const auto packed=psm==2?pixel16(base,width,x,y):pixel16s(base,width,x,y);
            return expand(packed&31)|(expand((packed>>5)&31)<<8)|(expand((packed>>10)&31)<<16)|
                   ((packed&0x8000)?0x80000000u:0u);
        }
        const auto word=psm==0x31?psmz32_word(base,width,x,y):psmct32_word(base,width,x,y);
        const auto stored=vram[word];
        return (psm==1||psm==0x31)?(stored&0xffffffu)|0x80000000u:stored;
    }();
    const auto test=value[0x47+context];
    const auto alpha_pass=gs_alpha_test(std::uint8_t(source>>24),test);
    const auto fail_action=std::uint32_t((test>>12)&3);
    if(!alpha_pass&&(fail_action==0||fail_action==2))return false;
    // The real PSMZ24 FRAME path reached by Haunting Ground has DATE disabled.
    // Keep unsupported destination-alpha combinations explicit until separately proven.
    if(!gs_destination_alpha_test((destination&(1u<<31))!=0,psm==0x31?1u:psm,test))return false;
    blend_frame_pixel(*this,context,x,y,source,destination,primitive_state,
                      !alpha_pass&&fail_action==3&&psm==0?0xff000000u:0u);
    return true;
}

bool GsRegisterState::draw_depth_frame_pixel(unsigned context,std::uint32_t x,std::uint32_t y,
                                             std::uint32_t source,std::uint32_t z,
                                             std::uint16_t primitive_state) {
    if(context>1)throw std::runtime_error("invalid GS frame context");
    const auto scanmask=std::uint32_t(value[0x22]&3);
    if((scanmask==2&&(y&1)==0)||(scanmask==3&&(y&1)!=0))return false;
    const auto test=value[0x47+context];
    const auto alpha_pass=gs_alpha_test(std::uint8_t(source>>24),test);
    const auto fail_action=std::uint32_t((test>>12)&3);
    const auto frame=value[0x4c+context];
    const auto psm=std::uint32_t((frame>>24)&0x3f);
    if(psm!=0&&psm!=1&&psm!=2&&psm!=10&&psm!=0x31)
        throw std::runtime_error("unimplemented GS frame-buffer pixel format");
    const auto width=std::uint32_t((frame>>16)&0x3f)*64;
    if(!width)throw std::runtime_error("invalid GS frame-buffer width");
    if(vram.size()!=1024*1024)throw std::runtime_error("GS local memory is not initialized");
    const auto base=std::uint32_t(frame&0x1ff)*2048;
    const auto expand=[](std::uint32_t channel) {return (channel<<3)|(channel>>2);};
    const auto destination=[&] {
        if(psm==2||psm==10) {
            const auto packed=psm==2?pixel16(base,width,x,y):pixel16s(base,width,x,y);
            return expand(packed&31)|(expand((packed>>5)&31)<<8)|(expand((packed>>10)&31)<<16)|
                   ((packed&0x8000)?0x80000000u:0u);
        }
        const auto stored=vram[(psm==0x31?psmz32_word:psmct32_word)(base,width,x,y)];
        return (psm==1||psm==0x31)?(stored&0xffffffu)|0x80000000u:stored;
    }();
    if(!gs_destination_alpha_test((destination&(1u<<31))!=0,psm==0x31?1u:psm,test))return false;
    if(!gs_depth_test(z,read_z_pixel(context,x,y),test))return false;
    if(!alpha_pass) {
        if(fail_action==0)return false;
        if(fail_action==2) {
            write_z_pixel(context,x,y,z);
            return true;
        }
        blend_frame_pixel(*this,context,x,y,source,destination,primitive_state,
                          fail_action==3&&psm==0?0xff000000u:0u);
        return true;
    }
    blend_frame_pixel(*this,context,x,y,source,destination,primitive_state,0);
    write_z_pixel(context,x,y,z);
    return true;
}

void GsRegisterState::write_packed(std::uint8_t descriptor,std::uint64_t low,std::uint64_t high) {
    switch(descriptor) {
    case 0: write_ad(0,low&0x7ff);return;
    case 1: {
        const auto rgba=(low&0xff)|((low>>24)&0xff00)|
                        ((high&0xff)<<16)|((high>>8)&0xff000000);
        write_ad(1,(std::uint64_t(gif_q)<<32)|rgba);return;
    }
    case 2:
        write_ad(2,low);gif_q=std::uint32_t(high);return;
    case 3:
        write_ad(3,(low&0x3fff)|((low>>16)&0x3fff0000));return;
    case 8: // EE User's Manual 7.2.3: CLAMP_1 uses the PACKED lower 64 bits directly.
        write_ad(8,low);return;
    case 0xa:write_ad(0xa,(high>>36)&0xff);return;
    case 0xe: {
        const auto address=std::uint8_t(high);
        // GS HWREG is the 64-bit GIF->VRAM data port. IMAGE format is only a
        // denser shortcut for two HWREG writes; PACKED A+D may write it
        // directly as Haunting Ground does for small palette uploads.
        if(address==0x54) {
            write_hwreg(low);
            value[address]=low;
            record_draw_environment_write(address,low);
            written[address]=true;
            return;
        }
        write_ad(address,low);return;
    }
    case 4: {
        // EE User's Manual 7.3.2, p154: PACKED Z occupies bits 68..91
        // and F occupies bits 100..107, unlike the GS register layout.
        const auto data=(low&0xffff)|(((low>>32)&0xffff)<<16)|
                        (((high>>4)&0xffffff)<<32)|(((high>>36)&0xff)<<56);
        write_ad((high&(1ull<<47))?0xc:4,data);return;
    }
    case 5: {
        const auto data=(low&0xffff)|(((low>>32)&0xffff)<<16)|(high<<32);
        write_ad((high&(1ull<<47))?0xd:5,data);return;
    }
    case 0xb:throw std::runtime_error("reserved GIF PACKED register descriptor");
    case 0xc:write_ad(0xc,low);return;
    case 0xd:write_ad(0xd,low);return;
    default: {
        char message[192]{};
        std::snprintf(message,sizeof(message),
                      "unsupported GIF PACKED register descriptor 0x%02x low=0x%016llx high=0x%016llx",
                      unsigned(descriptor),static_cast<unsigned long long>(low),
                      static_cast<unsigned long long>(high));
        throw std::runtime_error(message);
    }
    }
}

void GsRegisterState::write_reglist(std::uint8_t descriptor,std::uint64_t data) {
    switch(descriptor) {
    case 0:write_ad(0,data&0x7ff);return;
    case 1:write_ad(1,data);return;
    case 2:write_ad(2,data);return;
    case 3:write_ad(3,data&0x3fff3fffull);return;
    case 6:case 7:case 8:case 9:
        write_ad(descriptor,data);return;
    case 0xa:write_ad(0xa,data&0xff);return;
    case 4:case 5:case 0xc:case 0xd:
        write_ad(descriptor,data);return;
    case 0xe:return;
    default:throw std::runtime_error("unsupported GIF REGLIST register descriptor");
    }
}

bool GsRegisterState::rasterize_point(const GsDraw& draw) {
    if(gs_sprite_accelerator)gs_sprite_accelerator->flush();
    GsCpuMemoryScope cpu_memory(vram);
    if(draw.primitive!=0||draw.count!=1)throw std::runtime_error("invalid GS point draw");
    const auto context=unsigned((draw.prim_state>>9)&1);
    const auto vertex=draw.vertices[0];
    const auto nearest=[](std::int32_t fixed) {
        return fixed>=0?(fixed+8)/16:-((-fixed+8)/16);
    };
    const auto x=nearest(std::int32_t(vertex.x)-std::int32_t(draw.xyoffset&0xffff));
    const auto y=nearest(std::int32_t(vertex.y)-std::int32_t((draw.xyoffset>>32)&0xffff));
    const auto scissor=value[0x40+context];
    const auto x0=std::int32_t(scissor&0x7ff),x1=std::int32_t((scissor>>16)&0x7ff);
    const auto y0=std::int32_t((scissor>>32)&0x7ff),y1=std::int32_t((scissor>>48)&0x7ff);
    if(x<x0||x>x1||y<y0||y>y1)return false;
    std::uint32_t source=vertex.rgba;
    if(draw.prim_state&(1u<<4)) {
        const bool linear=linear_texture_filter(context);
        std::int32_t u,v;
        if(draw.prim_state&(1u<<8)) {
            const unsigned shift=linear?0:4;
            u=std::int32_t((vertex.uv&0x3fff)>>shift);v=std::int32_t(((vertex.uv>>16)&0x3fff)>>shift);
        } else {
            const auto tex0=value[6+context];
            u=linear?gs_stq_perspective_texel_coordinate({std::uint32_t(vertex.st),0,0},{vertex.q,0,0},{1,0,0},1u<<((tex0>>26)&0xf),4):
                     gs_stq_texel_coordinate(std::uint32_t(vertex.st),vertex.q,1u<<((tex0>>26)&0xf));
            v=linear?gs_stq_perspective_texel_coordinate({std::uint32_t(vertex.st>>32),0,0},{vertex.q,0,0},{1,0,0},1u<<((tex0>>30)&0xf),4):
                     gs_stq_texel_coordinate(std::uint32_t(vertex.st>>32),vertex.q,1u<<((tex0>>30)&0xf));
        }
        source=linear?shade_linear_tex0(context,u,v,vertex.rgba):shade_point_tex0(context,u,v,vertex.rgba);
    }
    if(draw.prim_state&(1u<<5))source=gs_apply_fog(source,std::uint8_t(vertex.fog),std::uint32_t(value[0x3d]));
    return draw_depth_frame_pixel(context,std::uint32_t(x),std::uint32_t(y),source,vertex.z,draw.prim_state);
}

bool GsRegisterState::rasterize_sprite(const GsDraw& draw) {
    if(draw.primitive!=6||draw.count!=2)throw std::runtime_error("invalid GS sprite draw");
    const auto textured=(draw.prim_state&(1u<<4))!=0;
    const auto& first=draw.vertices[0];
    const auto& second=draw.vertices[1];
    const auto fst=(draw.prim_state&(1u<<8))!=0;
    // Sprite shading is fixed flat and antialiasing is fixed off; IIP and
    // AA1 therefore do not override the drawing-kick vertex's attributes.
    const auto context=unsigned((draw.prim_state>>9)&1);
    const bool linear=textured&&linear_texture_filter(context);
    const auto offset=draw.xyoffset;
    const auto ceil_fixed=[](std::int32_t value) {
        return value>=0 ? (value+15)/16 : -((-value)/16);
    };
    const auto ax_fixed=std::int32_t(first.x)-std::int32_t(offset&0xffff);
    const auto ay_fixed=std::int32_t(first.y)-std::int32_t((offset>>32)&0xffff);
    const auto bx_fixed=std::int32_t(second.x)-std::int32_t(offset&0xffff);
    const auto by_fixed=std::int32_t(second.y)-std::int32_t((offset>>32)&0xffff);
    const auto ax=ceil_fixed(ax_fixed),ay=ceil_fixed(ay_fixed);
    const auto bx=ceil_fixed(bx_fixed),by=ceil_fixed(by_fixed);
    const auto left=ax<bx?ax:bx, right=ax<bx?bx:ax;
    const auto top=ay<by?ay:by, bottom=ay<by?by:ay;
    const auto scissor_value=value[0x40+context];
    const auto x0=std::int32_t(scissor_value&0x7ff), x1=std::int32_t((scissor_value>>16)&0x7ff);
    const auto y0=std::int32_t((scissor_value>>32)&0x7ff), y1=std::int32_t((scissor_value>>48)&0x7ff);
    const auto u0=std::int64_t(first.uv&0x3fff),v0=std::int64_t((first.uv>>16)&0x3fff);
    const auto u1=std::int64_t(second.uv&0x3fff),v1=std::int64_t((second.uv>>16)&0x3fff);
    const auto interpolate=[](std::int64_t first_value,std::int64_t second_value,
                              std::int64_t first_position,std::int64_t second_position,
                              std::int64_t sample) {
        if(first_position==second_position)return first_value;
        return first_value+(sample-first_position)*(second_value-first_value)/(second_position-first_position);
    };
    const auto first_x=std::max(left,x0),last_x=std::min(right,x1+1);
    const auto first_y=std::max(top,y0),last_y=std::min(bottom,y1+1);
    if(first_x>=last_x||first_y>=last_y)return false;
    // Fixed UV sprite coordinates are separable: U depends only on X and
    // V only on Y. Reuse the exact integer interpolation without changing
    // pixel order (which matters for overlapping texture/frame storage).
    std::array<std::int32_t,2048> fixed_u{};
    if(textured&&fst)for(auto x=first_x;x<last_x;++x)
        fixed_u[unsigned(x-first_x)]=std::int32_t(interpolate(u0,u1,ax_fixed,bx_fixed,std::int64_t(x)*16)>>(linear?0:4));
    // Reduce the existing pixel pipeline only when its tests always pass,
    // depth is masked and blending is disabled. RGB24 preserves the high
    // byte and ignores FBA. Keep live texture reads and row-major writes.
    const auto frame=value[0x4c+context],test=value[0x47+context],zbuf=value[0x4e + context];
    const auto frame_width=std::uint32_t((frame>>16)&0x3f)*64;
    const auto zpsm=unsigned((zbuf>>24)&15);
    const bool direct_rgb24=((frame>>24)&63)==1 && frame_width &&
        vram.size()==1024*1024 && !(draw.prim_state&(1u<<6)) &&
        !(value[0x22]&3) && !(test&0x4001) && (test&0x70000)==0x30000 &&
        (zbuf&(1ull<<32)) && (zpsm==0||zpsm==1||zpsm==2||zpsm==10);
    const auto frame_base=std::uint32_t(frame&511)*2048;
    const auto frame_mask=std::uint32_t(frame>>32)|0xff000000u;
    // Neutral RGBA modulation of a CT32 texture is an identity. With a
    // valid REGION_CLAMP on both axes, texture addresses separate into
    // X-only and Y-only additions in the existing CT32 layout. Cache
    // addresses, never texels: each read must still observe earlier writes
    // when the framebuffer overlaps the texture (including VRAM wrap).
    const auto tex0=value[6+context],clamp=value[8+context];
    const auto texture_width=std::uint32_t((tex0>>14)&63)*64;
    const auto tw=unsigned((tex0>>26)&15),th=unsigned((tex0>>30)&15);
    const auto min_u=std::uint32_t((clamp>>4)&1023),max_u=std::uint32_t((clamp>>14)&1023);
    const auto min_v=std::uint32_t((clamp>>24)&1023),max_v=std::uint32_t((clamp>>34)&1023);
    // Exact CT32 sprite specialization. Only addresses and immutable draw state
    // are prepared; row-major reads/writes preserve render-to-texture feedback.
    const bool ct32_sprite=textured&&fst&&!(draw.prim_state&(1u<<5))&&
        ((frame>>24)&63)==0&&((tex0>>20)&63)==0&&frame_width&&texture_width&&
        tw<=10&&th<=10&&vram.size()==1024*1024&&!(value[0x22]&3)&&
        !(test&0x4001)&&(test&0x70000)==0x30000&&(zbuf&(1ull<<32))&&
        (zpsm==0||zpsm==1||zpsm==2||zpsm==10);
    // HG-DIAG-016: exact separable sprite formats. No cached texture contents;
    // the backend proves source/output and frame/depth pages do not overlap.
    const auto sprite_source=unsigned((tex0>>20)&63),sprite_destination=unsigned((frame>>24)&63);
    const bool half_frame=sprite_destination==2||sprite_destination==10;
    if(gs_sprite_accelerator&&!ct32_sprite&&(!textured||fst)&&!(draw.prim_state&(1u<<5))&&
       !(value[0x22]&3)&&(test&0x10000)&&((test>>17)&3)&&zpsm<=1&&
       (sprite_destination<=2||sprite_destination==10||sprite_destination==49)&&frame_width&&vram.size()==1024*1024&&
       last_x<=std::int32_t(frame_width)&&
       std::uint64_t(frame_width)*((last_y+31)/32)*32<=1024*1024&&
       // Small PSMT8/PSMT4 sprites measured cheaper on the GPU than a CPU
       // fallback's flush/readback; other classes keep the batching threshold.
       (std::uint64_t(last_x-first_x)*(last_y-first_y)>=4096||
        (textured&&(sprite_source==0x13||sprite_source==0x14)))&&
       (!textured||(texture_width&&tw<=10&&th<=10&&
         (sprite_source<=2||sprite_source==10||sprite_source==0x13||sprite_source==0x14||
          sprite_source==27||sprite_source==49)))) {
        std::array<GsSpriteAxis,2048> columns,rows;
        std::array<std::uint32_t,256> palette{};
        const auto lower=[](std::int32_t fixed){const auto shifted=std::int64_t(fixed)-8;
            return std::int32_t(shifted>=0?shifted/16:-((-shifted+15)/16));};
        const auto source_address=sprite_source==2?psmct16_word:sprite_source==10?psmct16s_word:
            sprite_source==49?psmz32_word:psmct32_word;
        const auto destination_address=sprite_destination==2?psmct16_word:
            sprite_destination==10?psmct16s_word:sprite_destination==49?psmz32_word:psmct32_word;
        const auto texture_base=std::uint32_t(tex0&16383)*64;
        // PSMT8/PSMT4 addresses are not separable, so their axes carry wrapped
        // texel coordinates and the backend applies the same swizzle per texel.
        const bool indexed_source=sprite_source==0x13||sprite_source==0x14;
        bool valid=true;
        if(textured&&(sprite_source==27||indexed_source)) {
            // Same scope as DrawTexture: CT32 CLUT, CSM1; PSMT4 selects CSA<16.
            const bool four=sprite_source==0x14;
            valid=four?(((tex0>>51)&31)==0&&((tex0>>56)&31)<16):((tex0>>51)&0x3ff)==0;
            const auto clut_base=four?unsigned((tex0>>56)&15)*16:0u,entries=four?16u:256u;
            for(unsigned i=0;i<entries&&valid;++i){
                const auto entry=clut_base+i;
                valid=clut_valid[entry]&&clut_valid[entry+256];
                palette[i]=std::uint32_t(clut[entry])|(std::uint32_t(clut[entry+256])<<16);
            }
        }
        try {
            for(auto x=first_x;x<last_x;++x){
                auto& c=columns[unsigned(x-first_x)];c={0,0,destination_address(0,frame_width,unsigned(x),0),0};
                if(!textured)continue;
                const auto fixed=fixed_u[unsigned(x-first_x)],u=linear?lower(fixed):fixed;
                c.fraction=linear?std::uint32_t(std::int64_t(fixed)-8-std::int64_t(u)*16):0u;
                const auto address=[&](std::int32_t sample){
                    const auto wrapped=gs_wrap_texture_coordinate(sample,1u<<tw,unsigned(clamp&3),min_u,max_u);
                    if(indexed_source)return wrapped;
                    return source_address(0,texture_width,wrapped,0)|
                        ((sprite_source==2||sprite_source==10)?((wrapped&8u)<<28):0u);};
                c.source0=address(u);c.source1=c.fraction?address(u+1):c.source0;
            }
            for(auto y=first_y;y<last_y;++y){
                auto& r=rows[unsigned(y-first_y)];
                const auto destination_row=(destination_address(frame_base,frame_width,0,unsigned(y))-
                    (sprite_destination==49?1536u:0u))&0xfffffu;
                r={0,0,destination_row,0};
                if(!textured)continue;
                const auto fixed=std::int32_t(interpolate(v0,v1,ay_fixed,by_fixed,std::int64_t(y)*16)>>(linear?0:4));
                const auto v=linear?lower(fixed):fixed;
                r.fraction=linear?std::uint32_t(std::int64_t(fixed)-8-std::int64_t(v)*16):0u;
                const auto address=[&](std::int32_t sample){
                    const auto wrapped=gs_wrap_texture_coordinate(sample,1u<<th,unsigned((clamp>>2)&3),min_v,max_v);
                    if(indexed_source)return wrapped;
                    return (source_address(texture_base,texture_width,0,wrapped)-(sprite_source==49?1536u:0u))&0xfffffu;};
                r.source0=address(v);r.source1=r.fraction?address(v+1):r.source0;
            }
        }catch(const std::runtime_error&){valid=false;} // Preserve original partial-write/fault order on fallback.
        if(valid) {
            const auto alpha=value[0x42+context];
            GsSpriteJob job{std::uint32_t(last_x-first_x),std::uint32_t(last_y-first_y),second.rgba,
                unsigned((tex0>>35)&3),unsigned((tex0>>34)&1)|unsigned((value[0x46]&1)<<1)|
                unsigned((value[0x4a+context]&1)<<2)|unsigned((value[0x49]&1)<<3)|
                ((draw.prim_state&64)?16u:0u)|(textured?0u:32u),
                std::uint32_t(alpha),std::uint32_t(alpha>>32)&255,std::uint32_t(frame>>32),columns.data(),rows.data()};
            job.source_format=sprite_source;job.destination_format=sprite_destination;job.texa=value[0x3b];
            job.first_x=unsigned(first_x);job.first_y=unsigned(first_y);job.frame_width=frame_width;
            job.zbase=std::uint32_t(zbuf&511)*2048;job.zformat=zpsm;job.z=second.z;
            job.test=test;job.dimx=value[0x44];job.dither=(value[0x45]&1)!=0;job.zwrite=!(zbuf&(1ull<<32));
            if(textured&&(sprite_source==27||indexed_source))job.palette=palette.data();
            job.texture_base=texture_base;job.texture_width=texture_width;
            if(gs_sprite_accelerator->render(job,vram))return true;
        }
    }
    if(ct32_sprite) {
        std::array<GsSpriteAxis,2048> columns;
        const auto lower=[](std::int32_t fixed) {
            const auto shifted=std::int64_t(fixed)-8;
            return std::int32_t(shifted>=0?shifted/16:-((-shifted+15)/16));
        };
        const auto wrap_u=[&](std::int32_t u) {
            return gs_wrap_texture_coordinate(u,1u<<tw,std::uint32_t(clamp&3),min_u,max_u);
        };
        const auto wrap_v=[&](std::int32_t v) {
            return gs_wrap_texture_coordinate(v,1u<<th,std::uint32_t((clamp>>2)&3),min_v,max_v);
        };
        for(auto x=first_x;x<last_x;++x) {
            const auto fixed=fixed_u[unsigned(x-first_x)];
            const auto u=linear?lower(fixed):fixed;
            const auto a=linear?std::uint32_t(std::int64_t(fixed)-8-std::int64_t(u)*16):0u;
            const auto u0_address=psmct32_word(0,texture_width,wrap_u(u),0);
            columns[unsigned(x-first_x)]={u0_address,
                a?psmct32_word(0,texture_width,wrap_u(u+1),0):u0_address,
                psmct32_word(0,frame_width,std::uint32_t(x),0),a};
        }
        const auto texture_base=std::uint32_t(tex0&16383)*64;
        const auto write_mask=std::uint32_t(frame>>32);
        const bool clamp_color=(value[0x46]&1)!=0;
        const bool fba=(value[0x4a+context]&1)!=0,pabe=(value[0x49]&1)!=0;
        const bool blend_enabled=(draw.prim_state&(1u<<6))!=0;
        const auto alpha=value[0x42+context];
        const auto texture_function=std::uint32_t((tex0>>35)&3);
        const bool texture_alpha=(tex0&(1ull<<34))!=0;
        // HG-DIAG-016: optional synchronous GPU path. Prove destination mapping
        // is injective; the backend additionally rejects texture/frame alias.
        if(gs_sprite_accelerator && last_x<=std::int32_t(frame_width) &&
           std::uint64_t(frame_width)*((last_y+31)/32)*32<=1024*1024 &&
           std::uint64_t(last_x-first_x)*(last_y-first_y)>=2048) {
            std::array<GsSpriteAxis,2048> rows;
            bool valid=true;
            try {
                for(auto y=first_y;y<last_y;++y) {
                    const auto fixed=std::int32_t(interpolate(v0,v1,ay_fixed,by_fixed,std::int64_t(y)*16)>>(linear?0:4));
                    const auto v=linear?lower(fixed):fixed;
                    const auto b=linear?std::uint32_t(std::int64_t(fixed)-8-std::int64_t(v)*16):0u;
                    const auto r0=psmct32_word(texture_base,texture_width,0,wrap_v(v));
                    rows[unsigned(y-first_y)]={r0,b?psmct32_word(texture_base,texture_width,0,wrap_v(v+1)):r0,
                        psmct32_word(frame_base,frame_width,0,std::uint32_t(y)),b};
                }
            } catch(const std::runtime_error&) {valid=false;} // Original path preserves partial writes/fault order.
            if(valid) {
                const GsSpriteJob job{std::uint32_t(last_x-first_x),std::uint32_t(last_y-first_y),
                    second.rgba,texture_function,std::uint32_t(texture_alpha)|(std::uint32_t(clamp_color)<<1)|
                    (std::uint32_t(fba)<<2)|(std::uint32_t(pabe)<<3)|(std::uint32_t(blend_enabled)<<4),
                    std::uint32_t(alpha),std::uint32_t(alpha>>32)&255,std::uint32_t(frame>>32),columns.data(),rows.data()};
                if(gs_sprite_accelerator->render(job,vram))return true;
            }
        }
        if(gs_sprite_accelerator)gs_sprite_accelerator->flush();
        const auto ct32_write_pages=gs_raster_write_pages(*this,context,first_x,first_y,last_x,last_y);
        GsCpuMemoryScope cpu_memory(vram,gs_raster_texture_pages(*this,context),ct32_write_pages);
        const auto convert=[&](std::int32_t channel) {
            return clamp_color?std::uint32_t(channel<0?0:channel>255?255:channel):std::uint32_t(channel)&255;
        };
        for(auto y=first_y;y<last_y;++y) {
            const auto fixed=std::int32_t(interpolate(v0,v1,ay_fixed,by_fixed,std::int64_t(y)*16)>>(linear?0:4));
            const auto v=linear?lower(fixed):fixed;
            const auto b=linear?std::uint32_t(std::int64_t(fixed)-8-std::int64_t(v)*16):0u;
            const auto row0=psmct32_word(texture_base,texture_width,0,wrap_v(v));
            const auto row1=b?psmct32_word(texture_base,texture_width,0,wrap_v(v+1)):row0;
            const auto destination_row=psmct32_word(frame_base,frame_width,0,std::uint32_t(y));
            for(auto x=first_x;x<last_x;++x) {
                const auto& column=columns[unsigned(x-first_x)];
                const auto a=column.fraction;
                auto source=vram[(row0+column.source0)&0xfffffu];
                if(a||b) {
                    const auto c1=a?vram[(row0+column.source1)&0xfffffu]:0;
                    const auto c2=b?vram[(row1+column.source0)&0xfffffu]:0;
                    const auto c3=a&&b?vram[(row1+column.source1)&0xfffffu]:0;
                    const auto w0=(16-a)*(16-b),w1=a*(16-b),w2=(16-a)*b,w3=a*b;
                    // Two independent 16-bit lanes. Each weighted channel sum
                    // is <=255*256, so neither addition nor scaling crosses lanes.
                    constexpr std::uint32_t lanes=0x00ff00ffu;
                    const auto rb=(source&lanes)*w0+(c1&lanes)*w1+(c2&lanes)*w2+(c3&lanes)*w3;
                    const auto ga=((source>>8)&lanes)*w0+((c1>>8)&lanes)*w1+
                                  ((c2>>8)&lanes)*w2+((c3>>8)&lanes)*w3;
                    source=((rb>>8)&lanes)|(ga&0xff00ff00u);
                }
                source=gs_texture_function(source,second.rgba,texture_function,texture_alpha);
                auto& destination=vram[(destination_row+column.destination)&0xfffffu];
                const auto color=blend_enabled&&(!pabe||(source&0x80000000u))?
                    gs_alpha_blend(source,destination,alpha):
                    GsBlendResult{std::int32_t(source&255),std::int32_t((source>>8)&255),
                                  std::int32_t((source>>16)&255),std::uint8_t(source>>24)};
                const auto packed=convert(color.r)|(convert(color.g)<<8)|(convert(color.b)<<16)|
                                  ((std::uint32_t(color.a)|(fba?128u:0u))<<24);
                destination=(destination&write_mask)|(packed&~write_mask);
            }
        }
        return true;
    }
    if(gs_sprite_accelerator)gs_sprite_accelerator->flush();
    const auto sprite_write_pages=gs_raster_write_pages(*this,context,first_x,first_y,last_x,last_y);
    const auto sprite_read_pages=textured?gs_raster_texture_pages(*this,context):std::bitset<512>{};
    GsCpuMemoryScope cpu_memory(vram,sprite_read_pages,sprite_write_pages);
    const DrawTexture texture(*this,context,linear);
    if(direct_rgb24&&textured&&fst&&!linear&&!(draw.prim_state&(1u<<5))&&
       second.rgba==0x80808080u&&((tex0>>20)&63)==0&&((tex0>>34)&7)==1&&
       texture_width&&tw<=10&&th<=10&&(clamp&15)==10&&
       min_u<=max_u&&max_u<(1u<<tw)&&min_v<=max_v&&max_v<(1u<<th)) {
        const auto clamp_axis=[](std::int32_t n,std::uint32_t low,std::uint32_t high) {
            return n<std::int32_t(low)?low:n>std::int32_t(high)?high:std::uint32_t(n);
        };
        for(auto x=first_x;x<last_x;++x) {
            auto& u=fixed_u[unsigned(x-first_x)];
            u=std::int32_t(psmct32_word(0,texture_width,clamp_axis(u,min_u,max_u),0));
        }
        const auto texture_base=std::uint32_t(tex0&16383)*64;
        for(auto y=first_y;y<last_y;++y) {
            const auto v=std::int32_t(interpolate(v0,v1,ay_fixed,by_fixed,std::int64_t(y)*16)>>4);
            const auto row=psmct32_word(texture_base,texture_width,0,clamp_axis(v,min_v,max_v));
            for(auto x=first_x;x<last_x;++x) {
                const auto source=vram[(row+std::uint32_t(fixed_u[unsigned(x-first_x)]))&0xfffffu];
                auto& destination=vram[psmct32_word(frame_base,frame_width,std::uint32_t(x),std::uint32_t(y))];
                destination=(destination&frame_mask)|(source&~frame_mask);
            }
        }
        return true;
    }
    bool drawn=false;
    for(auto y=first_y;y<last_y;++y) {
        const auto fixed_v=textured&&fst?std::int32_t(interpolate(v0,v1,ay_fixed,by_fixed,std::int64_t(y)*16)>>(linear?0:4)):0;
        for(auto x=first_x;x<last_x;++x) {
            auto source=second.rgba;
            if(textured) {
                std::int32_t u,v;
                if(fst) {
                    u=fixed_u[unsigned(x-first_x)];v=fixed_v;
                } else {
                    const auto weights=[](std::int64_t start,std::int64_t end,std::int64_t sample) {
                        if(end>start)return std::pair<std::uint64_t,std::uint64_t>{
                            std::uint64_t(end-sample),std::uint64_t(sample-start)};
                        return std::pair<std::uint64_t,std::uint64_t>{
                            std::uint64_t(sample-end),std::uint64_t(start-sample)};
                    };
                    const auto wx=weights(ax_fixed,bx_fixed,std::int64_t(x)*16);
                    const auto wy=weights(ay_fixed,by_fixed,std::int64_t(y)*16);
                    const auto tex0=value[6+context];
                    u=gs_stq_perspective_texel_coordinate(
                        {std::uint32_t(first.st),std::uint32_t(second.st),0},
                        {first.q,second.q,0},{std::int64_t(wx.first),std::int64_t(wx.second),0},1u<<((tex0>>26)&0xf),linear?4:0);
                    v=gs_stq_perspective_texel_coordinate(
                        {std::uint32_t(first.st>>32),std::uint32_t(second.st>>32),0},
                        {first.q,second.q,0},{std::int64_t(wy.first),std::int64_t(wy.second),0},1u<<((tex0>>30)&0xf),linear?4:0);
                }
                source=sprite_source==0x14?texture.shade(u,v,source):
                    (linear?shade_linear_tex0(context,u,v,source):shade_point_tex0(context,u,v,source));
            }
            if(draw.prim_state&(1u<<5))source=gs_apply_fog(source,std::uint8_t(second.fog),std::uint32_t(value[0x3d]));
            if(direct_rgb24) {
                auto& destination=vram[psmct32_word(frame_base,frame_width,std::uint32_t(x),std::uint32_t(y))];
                destination=(destination&frame_mask)|(source&~frame_mask);drawn=true;
            } else drawn=draw_depth_frame_pixel(context,std::uint32_t(x),std::uint32_t(y),source,second.z,draw.prim_state)||drawn;
        }
    }
    return drawn;
}

bool GsRegisterState::rasterize_line(const GsDraw& draw) {
    if(gs_sprite_accelerator)gs_sprite_accelerator->flush();
    GsCpuMemoryScope cpu_memory(vram);
    if((draw.primitive!=1&&draw.primitive!=2)||draw.count!=2)
        throw std::runtime_error("invalid GS line draw");
    if(draw.prim_state&(1u<<7))throw std::runtime_error("unimplemented GS line antialiasing");
    const auto& first=draw.vertices[0];
    const auto& second=draw.vertices[1];
    const auto textured=(draw.prim_state&(1u<<4))!=0;
    const auto fst=(draw.prim_state&(1u<<8))!=0;
    const auto gouraud=(draw.prim_state&(1u<<3))!=0;
    const auto context=unsigned((draw.prim_state>>9)&1);
    const bool linear=textured&&linear_texture_filter(context);
    const auto offset=draw.xyoffset;
    const auto ax=std::int64_t(first.x)-std::int64_t(offset&0xffff);
    const auto ay=std::int64_t(first.y)-std::int64_t((offset>>32)&0xffff);
    const auto bx=std::int64_t(second.x)-std::int64_t(offset&0xffff);
    const auto by=std::int64_t(second.y)-std::int64_t((offset>>32)&0xffff);
    if(ax==bx&&ay==by)return false;
    const auto dx=bx-ax,dy=by-ay;
    const auto magnitude=[](std::int64_t value){return value<0?-value:value;};
    const bool x_major=magnitude(dx)>=magnitude(dy);
    const auto floor_fixed=[](std::int64_t value) {return value>=0?value/16:-((-value+15)/16);};
    const auto ceil_fixed=[](std::int64_t value) {return value>=0?(value+15)/16:-((-value)/16);};
    const auto min=[](std::int64_t a,std::int64_t b) {return a<b?a:b;};
    const auto max=[](std::int64_t a,std::int64_t b) {return a>b?a:b;};
    // Transform |x-cx|+|y-cy| <= 8 into two axis-aligned intervals. A
    // pixel is drawn only when the segment exits its diamond before t=1;
    // merely ending inside the final diamond must not paint that endpoint
    // pixel, and leaves it for a possible continuation segment.
    const auto crosses_diamond=[&](std::int64_t cx,std::int64_t cy) {
        struct Fraction {std::int64_t numerator=0,denominator=1;};
        const auto less=[](Fraction a,Fraction b) {
            return a.numerator*b.denominator<b.numerator*a.denominator;
        };
        const auto less_equal=[&](Fraction a,Fraction b) {return !less(b,a);};
        Fraction low{},high{};bool has_low=false,has_high=false,valid=true;
        const auto clip=[&](std::int64_t initial,std::int64_t delta,std::int64_t center) {
            const auto lower=center-8,upper=center+8;
            if(!delta)return initial>=lower&&initial<=upper;
            const auto denominator=delta<0?-delta:delta;
            Fraction a{lower-initial,denominator},b{upper-initial,denominator};
            if(delta<0) {a.numerator=-a.numerator;b.numerator=-b.numerator;}
            if(less(b,a)) {const auto swap=a;a=b;b=swap;}
            if(!has_low||less(low,a)) {low=a;has_low=true;}
            if(!has_high||less(b,high)) {high=b;has_high=true;}
            valid=!has_low||!has_high||less_equal(low,high);
            return valid;
        };
        const auto ux=ax+ay,uy=bx+by,vx=ax-ay,vy=bx-by;
        if(!clip(ux,uy-ux,cx+cy)||!clip(vx,vy-vx,cx-cy)||!valid||!has_high)return false;
        const Fraction zero{0,1},one{1,1};
        return less_equal(zero,high)&&(!has_low||less(low,one))&&less(high,one);
    };
    const auto scissor=value[0x40+context];
    const auto x0=std::int64_t(scissor&0x7ff),x1=std::int64_t((scissor>>16)&0x7ff);
    const auto y0=std::int64_t((scissor>>32)&0x7ff),y1=std::int64_t((scissor>>48)&0x7ff);
    const auto left=max(x0,floor_fixed(min(ax,bx)-8)),right=min(x1,ceil_fixed(max(ax,bx)+8));
    const auto top=max(y0,floor_fixed(min(ay,by)-8)),bottom=min(y1,ceil_fixed(max(ay,by)+8));
    bool drawn=false;
    for(auto y=top;y<=bottom;++y)for(auto x=left;x<=right;++x)
        if(crosses_diamond(x*16,y*16)) {
            const auto position=x_major?std::int64_t(x)*16:std::int64_t(y)*16;
            const auto start=x_major?ax:ay,end=x_major?bx:by;
            const auto interpolate=[&](std::int64_t first_value,std::int64_t second_value) {
                return first_value+(position-start)*(second_value-first_value)/(end-start);
            };
            auto source=second.rgba; // Flat shading takes the drawing-kick vertex.
            if(gouraud) {
                source=0;
                for(unsigned shift=0;shift<32;shift+=8)
                    source|=std::uint32_t(interpolate((first.rgba>>shift)&0xff,(second.rgba>>shift)&0xff))<<shift;
            }
            if(textured) {
                std::int32_t u,v;
                if(fst) {
                    u=std::int32_t(interpolate(first.uv&0x3fff,second.uv&0x3fff)>>(linear?0:4));
                    v=std::int32_t(interpolate((first.uv>>16)&0x3fff,(second.uv>>16)&0x3fff)>>(linear?0:4));
                } else {
                    auto first_weight=end-position,second_weight=position-start;
                    if(end<start) {first_weight=-first_weight;second_weight=-second_weight;}
                    const auto tex0=value[6+context];
                    u=gs_stq_perspective_texel_coordinate(
                        {std::uint32_t(first.st),std::uint32_t(second.st),0},{first.q,second.q,0},
                        {first_weight,second_weight,0},1u<<((tex0>>26)&0xf),linear?4:0);
                    v=gs_stq_perspective_texel_coordinate(
                        {std::uint32_t(first.st>>32),std::uint32_t(second.st>>32),0},{first.q,second.q,0},
                        {first_weight,second_weight,0},1u<<((tex0>>30)&0xf),linear?4:0);
                }
                source=linear?shade_linear_tex0(context,u,v,source):shade_point_tex0(context,u,v,source);
            }
            if(draw.prim_state&(1u<<5))
                source=gs_apply_fog(source,std::uint8_t(interpolate(first.fog,second.fog)),std::uint32_t(value[0x3d]));
            const auto z=std::uint32_t(interpolate(first.z,second.z));
            drawn=draw_depth_frame_pixel(context,std::uint32_t(x),std::uint32_t(y),source,z,draw.prim_state)||drawn;
        }
    return drawn;
}

bool GsRegisterState::rasterize_triangle(const GsDraw& draw) {
    if((draw.primitive<3||draw.primitive>5)||draw.count!=3)throw std::runtime_error("invalid GS triangle draw");
    const auto textured=(draw.prim_state&(1u<<4))!=0;
    const auto fst=(draw.prim_state&(1u<<8))!=0;
    if(draw.prim_state&(1u<<7))throw std::runtime_error("prohibited GS triangle antialiasing");
    auto a=draw.vertices[0],b=draw.vertices[1],c=draw.vertices[2];
    const auto flat_fragment=c.rgba;
    const auto gouraud=(draw.prim_state&(1u<<3))!=0;
    const auto offset=draw.xyoffset;
    struct Point { std::int64_t x,y; };
    const auto point=[&](const GsVertex& vertex) {
        return Point{std::int64_t(vertex.x)-std::int64_t(offset&0xffff),
                     std::int64_t(vertex.y)-std::int64_t((offset>>32)&0xffff)};
    };
    auto pa=point(a),pb=point(b),pc=point(c);
    const auto edge=[](Point first,Point second,Point sample) {
        return (second.x-first.x)*(sample.y-first.y)-(second.y-first.y)*(sample.x-first.x);
    };
    auto area=edge(pa,pb,pc);
    if(!area)return false;
    if(area<0) {const auto point_swap=pb;pb=pc;pc=point_swap;const auto vertex_swap=b;b=c;c=vertex_swap;area=-area;}
    const auto top_left=[](Point first,Point second) {
        const auto dx=second.x-first.x,dy=second.y-first.y;
        return dy<0 || (dy==0&&dx>0);
    };
    const auto covered=[&](Point first,Point second,Point sample) {
        const auto value=edge(first,second,sample);
        return value>0 || (value==0&&top_left(first,second));
    };
    const auto floor_fixed=[](std::int64_t value) {return value>=0?value/16:-((-value+15)/16);};
    const auto ceil_fixed=[](std::int64_t value) {return value>=0?(value+15)/16:-((-value)/16);};
    const auto min3=[](std::int64_t x,std::int64_t y,std::int64_t z) {return x<y?(x<z?x:z):(y<z?y:z);};
    const auto max3=[](std::int64_t x,std::int64_t y,std::int64_t z) {return x>y?(x>z?x:z):(y>z?y:z);};
    const auto left=ceil_fixed(min3(pa.x,pb.x,pc.x)),right=floor_fixed(max3(pa.x,pb.x,pc.x));
    const auto top=ceil_fixed(min3(pa.y,pb.y,pc.y)),bottom=floor_fixed(max3(pa.y,pb.y,pc.y));
    const auto context=unsigned((draw.prim_state>>9)&1);
    const bool linear=textured&&linear_texture_filter(context);
    PreparedTriangleStq perspective;
    if(textured&&!fst)perspective.prepare(a,b,c,std::uint64_t(area),value[6+context],linear);
    const GsTriangleQuotient divide{std::uint64_t(area)};
    const auto scissor=value[0x40+context];
    const auto x0=std::int64_t(scissor&0x7ff),x1=std::int64_t((scissor>>16)&0x7ff);
    const auto y0=std::int64_t((scissor>>32)&0x7ff),y1=std::int64_t((scissor>>48)&0x7ff);
    const auto clipped_left=std::max(left,x0),clipped_right=std::min(right,x1);
    const auto clipped_top=std::max(top,y0),clipped_bottom=std::min(bottom,y1);
    auto first_covered_x=clipped_left,first_covered_y=clipped_top;bool has_sample=false;
    for(;first_covered_y<=clipped_bottom&&!has_sample;++first_covered_y) {
        for(first_covered_x=clipped_left;first_covered_x<=clipped_right;++first_covered_x) {
            const Point sample{first_covered_x*16,first_covered_y*16};
            if(covered(pa,pb,sample)&&covered(pb,pc,sample)&&covered(pc,pa,sample)){has_sample=true;break;}
        }
    }
    if(!has_sample)return false;
    --first_covered_y;
    // HG-PERF-120: the exact own-pixel PSMT8H feedback class is detected before
    // the GPU attempt; the GPU tile shader reads each pixel's own frame word in
    // per-pixel submission order, exactly as the scalar path below does.
    bool cpu_self_feedback=false;std::uint32_t feedback_frame_base=0,feedback_frame_width=0;
    unsigned feedback_function=0;bool feedback_tcc=false;
    if(textured&&!fst&&!linear&&perspective.ready) {
        const auto tex0=value[6+context],frame=value[0x4c+context],clamp=value[8+context];
        const auto format=unsigned((tex0>>20)&63),frame_format=unsigned((frame>>24)&63);
        const auto tw=unsigned((tex0>>26)&15),th=unsigned((tex0>>30)&15);
        const auto texture_width=unsigned((tex0>>14)&63)*64,frame_width=unsigned((frame>>16)&63)*64;
        const auto texture_base=unsigned(tex0&0x3fff)*64,frame_base=unsigned(frame&511)*2048;
        const auto identity_axis=[](unsigned mode,unsigned size,unsigned minimum,unsigned maximum,
                                    std::int64_t first,std::int64_t last) {
            if(first<0||last<first)return false;
            const auto low=std::uint64_t(first),high=std::uint64_t(last);
            if(mode<=1)return high<size;
            if(mode==2)return low>=minimum&&high<=maximum;
            return false; // REGION_REPEAT needs a stronger interval proof; retain generic fallback.
        };
        const auto u_mode=unsigned(clamp&3),v_mode=unsigned((clamp>>2)&3);
        const auto u_min=unsigned((clamp>>4)&1023),u_max=unsigned((clamp>>14)&1023);
        const auto v_min=unsigned((clamp>>24)&1023),v_max=unsigned((clamp>>34)&1023);
        if(format==0x1b&&frame_format==0&&tw<=10&&th<=10&&texture_width&&texture_width==frame_width&&
           texture_base==frame_base&&((tex0>>51)&0x3ff)==0&&a.q==0x3f800000&&b.q==0x3f800000&&c.q==0x3f800000&&
           clipped_right<std::int64_t(frame_width)&&
           identity_axis(u_mode,1u<<tw,u_min,u_max,clipped_left,clipped_right)&&
           identity_axis(v_mode,1u<<th,v_min,v_max,first_covered_y,clipped_bottom)) {
            const auto vertex_ok=[&](const GsVertex& vertex,Point position) {
                const auto u=gs_stq_perspective_texel_coordinate({std::uint32_t(vertex.st),0,0},{vertex.q,0,0},
                    {1,0,0},1u<<tw,4);
                const auto v=gs_stq_perspective_texel_coordinate({std::uint32_t(vertex.st>>32),0,0},{vertex.q,0,0},
                    {1,0,0},1u<<th,4);
                const auto dx=std::int64_t(u)-position.x,dy=std::int64_t(v)-position.y;
                return dx>=0&&dx<=15&&dy>=0&&dy<=15;
            };
            try {cpu_self_feedback=vertex_ok(a,pa)&&vertex_ok(b,pb)&&vertex_ok(c,pc);}
            catch(const std::runtime_error&) {cpu_self_feedback=false;}
            if(cpu_self_feedback){feedback_frame_base=frame_base;feedback_frame_width=frame_width;
                feedback_function=unsigned((tex0>>35)&3);feedback_tcc=(tex0&(1ull<<34))!=0;}
        }
    }
    const bool wide_product_candidate=gs_triangle_accelerator&&textured&&!fst&&!perspective.ready&&
        perspective.failure==PreparedPerspectiveFailure::product&&gs_triangle_accelerator->accepts_native_float_stq();
    if(gs_triangle_accelerator&&(!textured||(!fst&&(perspective.ready||wide_product_candidate)))&&!(draw.prim_state&(1u<<5))) {
        const auto submit=[&] {
            const auto frame=value[0x4c+context],zbuf=value[0x4e + context],test=value[0x47+context];
            const auto tex0=value[6+context],clamp=value[8+context],alpha=value[0x42+context];
            const auto width=unsigned((frame>>16)&63)*64,format=unsigned((tex0>>20)&63);
            const auto zformat=unsigned((zbuf>>24)&15);
            const auto l=std::max(left,x0),r=std::min(right,x1)+1,t=std::max(top,y0),bott=std::min(bottom,y1)+1;
            if(l>=r||t>=bott||!width||r>width||std::uint64_t(width)*((bott+31)/32)*32>1024*1024)
                return false;
            if(((frame>>24)&63)!=0||zformat>1||vram.size()!=1024*1024||(value[0x22]&3))
                return false;
            if((test&0x4000)||!(test&0x10000)||((test>>17)&3)==0)
                return false;
            if((test&1)&&((test>>12)&3)!=0) // HG-PERF-116: any ATST, AFAIL KEEP only.
                return false;
            if(textured&&(format!=0&&format!=0x13&&format!=0x14&&format!=0x1b))
                return false;
            // CT32 CLUT, CSM1; PSMT4 may select CSA<16 (same scope as DrawTexture).
            if(textured&&(!((tex0>>14)&63)||(format==0x14?(((tex0>>51)&31)||((tex0>>56)&31)>=16):
               (format!=0&&((tex0>>51)&0x3ff)))))return false;
            if(textured&&(((clamp&3)==2&&((clamp>>4)&1023)>((clamp>>14)&1023))||
               (((clamp>>2)&3)==2&&((clamp>>24)&1023)>((clamp>>34)&1023))))
                return false;
            if(draw.prim_state&64)for(unsigned s=0;s<8;s+=2)if(((alpha>>s)&3)==3)
                return false;
            GsTriangleJob job;auto& d=job.data;bool prepacked_native_stq=false;
            const auto put64=[&](unsigned at,std::uint64_t n){d[at]=std::uint32_t(n);d[at+1]=std::uint32_t(n>>32);};
            if(textured) {
            PreparedTriangleStq wide_perspective;const PreparedTriangleStq* packed=&perspective;
            const auto top_bit=[](std::uint64_t n){int bit=-1;while(n){++bit;n>>=1;}return bit;};
            if(!perspective.ready) {
                wide_perspective.prepare(a,b,c,std::uint64_t(area),tex0,linear,false);
                if(!wide_perspective.ready)return false;
                for(const auto q:wide_perspective.q.coefficient)if(q<=0)return false;
                // The scalar helper widens coefficient*weight arithmetic to 128 bits.
                // Prove its worst covered sum and any numerator scale-up remain in range.
                const int area_bit=top_bit(std::uint64_t(area));
                const auto weighted_top=[&](const PreparedCoordinate& coordinate){
                    int top=-1;for(const auto n:coordinate.coefficient)if(n)
                        top=std::max(top,top_bit(std::uint64_t(n<0?-n:n))+area_bit+1);
                    return top;
                };
                if(weighted_top(wide_perspective.q)>=128)return false;
                const auto ratio_wide_safe=[&](const PreparedCoordinate& coordinate,unsigned dimension){
                    const auto top=weighted_top(coordinate);
                    const int shift=coordinate.scale-wide_perspective.q.scale+int(dimension);
                    return top<0||shift<=0||top+shift<128;
                };
                if(!ratio_wide_safe(wide_perspective.s,wide_perspective.shift_u)||
                   !ratio_wide_safe(wide_perspective.t,wide_perspective.shift_v))return false;
                const auto weights_for=[](unsigned vertex){std::array<std::int64_t,3> w{};w[vertex]=1;return w;};
                try {
                    for(unsigned i=0;i<3;++i) {
                        const auto weights=weights_for(i);
                        (void)gs_stq_perspective_texel_coordinate(
                            {std::uint32_t(a.st),std::uint32_t(b.st),std::uint32_t(c.st)},
                            {a.q,b.q,c.q},weights,1u<<((tex0>>26)&15),linear?4:0);
                        (void)gs_stq_perspective_texel_coordinate(
                            {std::uint32_t(a.st>>32),std::uint32_t(b.st>>32),std::uint32_t(c.st>>32)},
                            {a.q,b.q,c.q},weights,1u<<((tex0>>30)&15),linear?4:0);
                    }
                } catch(const std::runtime_error&) {return false;}
                packed=&wide_perspective;prepacked_native_stq=true;
            }
            const auto coordinate_shift=[&](const PreparedCoordinate& coordinate,unsigned dimension){
                if(coordinate.coefficient==std::array<std::int64_t,3>{})return 0;
                return coordinate.scale-packed->q.scale+int(dimension);
            };
            if(prepacked_native_stq) {
                int peak=-10000;
                const auto observe=[&](const PreparedCoordinate& coordinate,unsigned dimension){
                    for(const auto n:coordinate.coefficient)if(n) {
                        const auto magnitude=std::uint64_t(n<0?-n:n);
                        peak=std::max(peak,coordinate.scale+int(dimension)+top_bit(magnitude));
                    }
                };
                observe(packed->s,packed->shift_u);observe(packed->t,packed->shift_v);observe(packed->q,0);
                if(peak==-10000)return false;
                const int normalization=peak-30;
                const auto put_float=[&](const PreparedCoordinate& coordinate,unsigned dimension,unsigned at) {
                    const int exponent=coordinate.scale+int(dimension)-normalization;
                    for(unsigned i=0;i<3;++i) {
                        const auto n=coordinate.coefficient[i];
                        const float value=std::ldexp(static_cast<float>(n),exponent);
                        if(!std::isfinite(value)||(n&&value==0.0f))return false;
                        std::memcpy(&d[at+i*2],&value,sizeof(value));d[at+i*2+1]=0;
                    }
                    return true;
                };
                if(!put_float(packed->s,packed->shift_u,GsTriangleJob::S)||
                   !put_float(packed->t,packed->shift_v,GsTriangleJob::T)||
                   !put_float(packed->q,0,GsTriangleJob::Q))return false;
            } else {
                const auto extra=std::max({0,-coordinate_shift(packed->s,packed->shift_u),
                                           -coordinate_shift(packed->t,packed->shift_v)});
                if(extra>=63)return false;
                const auto coefficient_limit=std::uint64_t((std::numeric_limits<std::int64_t>::max)())/std::uint64_t(area);
                const auto prepare_numerator=[&](const PreparedCoordinate& coordinate,unsigned at,unsigned dimension) {
                    const int shift=coordinate_shift(coordinate,dimension)+extra;
                    if(shift<0||shift>=63)return false;
                    const auto limit=std::uint64_t((std::numeric_limits<std::int64_t>::max)())/std::uint64_t(area);
                    for(unsigned i=0;i<3;++i){const auto n=coordinate.coefficient[i];const auto magnitude=std::uint64_t(n<0?-n:n);
                        if(magnitude>(limit>>shift))return false;
                        const auto scaled=std::int64_t(magnitude<<shift);
                        put64(at+i*2,std::uint64_t(n<0?-scaled:scaled));}
                    return true;
                };
                for(unsigned i=0;i<3;++i)if(packed->q.coefficient[i]<=0||
                    std::uint64_t(packed->q.coefficient[i])>(coefficient_limit>>extra))
                        return false;
                if(!prepare_numerator(packed->s,GsTriangleJob::S,packed->shift_u)||
                   !prepare_numerator(packed->t,GsTriangleJob::T,packed->shift_v))
                    return false;
                // Same-sign Q makes every interior ratio a convex combination of
                // vertex ratios. Check signed texel bounds at the vertices first.
                for(unsigned i=0;i<3;++i){
                    const auto q=std::uint64_t(packed->q.coefficient[i])<<extra;put64(GsTriangleJob::Q+i*2,q);
                    for(unsigned at:{unsigned(GsTriangleJob::S),unsigned(GsTriangleJob::T)}) {
                        const auto n=std::int64_t(std::uint64_t(d[at+i*2])|(std::uint64_t(d[at+i*2+1])<<32));
                        if(n<0){if(std::uint64_t(-n)/q>0x80000000ull||
                            (std::uint64_t(-n)/q==0x80000000ull&&std::uint64_t(-n)%q))
                                return false;}
                        else if(std::uint64_t(n)/q>0x7fffffffull)
                            return false;
                    }
                }
            }
            if(format!=0){
                const auto clut_base=format==0x14?unsigned((tex0>>56)&15)*16:0u,entries=format==0x14?16u:256u;
                for(unsigned i=0;i<entries;++i){
                    const auto entry=clut_base+i;
                    if(!clut_valid[entry]||!clut_valid[entry+256])
                        return false;
                    job.palette[i]=std::uint32_t(clut[entry])|(std::uint32_t(clut[entry+256])<<16);
                }
            }
            }
            d[0]=std::uint32_t(pa.x);d[1]=std::uint32_t(pa.y);d[2]=std::uint32_t(pb.x);d[3]=std::uint32_t(pb.y);
            d[4]=std::uint32_t(pc.x);d[5]=std::uint32_t(pc.y);d[6]=unsigned(l);d[7]=unsigned(t);d[8]=unsigned(r);d[9]=unsigned(bott);
            d[10]=unsigned(frame&511)*2048;d[11]=width;d[12]=unsigned(zbuf&511)*2048;d[13]=zformat?0xffffffu:0xffffffffu;
            d[14]=std::uint32_t(frame>>32);d[15]=(gouraud?1:0)|(linear?2:0)|((tex0&(1ull<<34))?4:0)|
                ((draw.prim_state&64)?8:0)|((value[0x46]&1)?16:0)|((value[0x4a+context]&1)?32:0)|
                ((value[0x49]&1)?64:0)|((zbuf&(1ull<<32))?0:128)|((test&1)?256:0)|
                (textured?0:GsTriangleJob::untextured_flag)|(prepacked_native_stq?GsTriangleJob::native_float_stq_flag:0)|
                (cpu_self_feedback?GsTriangleJob::self_feedback_flag:0);
            d[16]=unsigned((test>>17)&3);d[17]=unsigned((test>>4)&255);d[18]=std::uint32_t(alpha);d[19]=unsigned((alpha>>32)&255);
            d[20]=unsigned(tex0&0x3fff)*64;d[21]=unsigned((tex0>>14)&63)*64;d[22]=format;
            d[23]=1u<<((tex0>>26)&15);d[24]=1u<<((tex0>>30)&15);d[25]=std::uint32_t(clamp);d[26]=std::uint32_t(clamp>>32);
            d[27]=unsigned((test>>1)&7);d[28]=unsigned((tex0>>35)&3);d[29]=flat_fragment;
            d[30]=a.rgba;d[31]=b.rgba;d[32]=c.rgba;d[33]=a.z;d[34]=b.z;d[35]=c.z;put64(36,std::uint64_t(area));
            return gs_triangle_accelerator->render_triangle(job,vram);
        };
        const auto submitted=submit();
        if(submitted)return true;
    }
    if(gs_sprite_accelerator)gs_sprite_accelerator->flush();
    const auto triangle_write_pages=gs_raster_write_pages(*this,context,clipped_left,first_covered_y,clipped_right+1,clipped_bottom+1);
    const auto triangle_read_pages=cpu_self_feedback?triangle_write_pages:
        textured?gs_raster_texture_pages(*this,context):std::bitset<512>{};
    GsCpuMemoryScope cpu_memory(vram,triangle_read_pages,triangle_write_pages);
    const DrawTexture texture(*this,context,linear);
    // Flat shading takes the RGBAQ state immediately preceding this draw's
    // XYZ2 kick, which is the final triangle vertex captured here.
    bool drawn=false;
    for(auto y=first_covered_y;y<=clipped_bottom;++y) for(auto x=y==first_covered_y?first_covered_x:clipped_left;x<=clipped_right;++x) {
        const Point sample{x*16,y*16};
        if(covered(pa,pb,sample)&&covered(pb,pc,sample)&&covered(pc,pa,sample)) {
            auto source=flat_fragment;
            const auto wa=edge(pb,pc,sample),wb=edge(pc,pa,sample),wc=edge(pa,pb,sample);
            const auto interpolate=[&](std::int64_t av,std::int64_t bv,std::int64_t cv) {
                return std::int64_t(divide(std::uint64_t(wa*av+wb*bv+wc*cv)));
            };
            // GS X/Y fields are 16-bit, so triangle area is at most
            // 65535^2. Covered nonnegative weights sum to that area;
            // therefore the weighted uint32 sum fits exactly in uint64.
            const auto interpolate_u32=[&](std::uint32_t av,std::uint32_t bv,std::uint32_t cv) {
                const auto sum=std::uint64_t(wa)*av+std::uint64_t(wb)*bv+std::uint64_t(wc)*cv;
                return divide(sum);
            };
            if(gouraud) {
                source=0;
                for(unsigned shift=0;shift<32;shift+=8)
                    source|=std::uint32_t(interpolate((a.rgba>>shift)&0xff,(b.rgba>>shift)&0xff,
                                                     (c.rgba>>shift)&0xff))<<shift;
            } else source=flat_fragment;
            if(textured) {
                if(cpu_self_feedback) {
                    const auto word=psmct32_word(feedback_frame_base,feedback_frame_width,std::uint32_t(x),std::uint32_t(y));
                    const auto index=std::uint8_t(vram[word]>>24);
                    if(!clut_valid[index]||!clut_valid[index+256])
                        throw std::runtime_error("GS indexed texture uses an unloaded CLUT entry");
                    const auto texel=std::uint32_t(clut[index])|(std::uint32_t(clut[index+256])<<16);
                    source=gs_texture_function(texel,source,feedback_function,feedback_tcc);
                } else {
                    std::int32_t u,v;
                    if(fst) {
                        const auto component=[&](unsigned shift) {
                            const auto av=std::int64_t((a.uv>>shift)&0x3fff),bv=std::int64_t((b.uv>>shift)&0x3fff);
                            const auto cv=std::int64_t((c.uv>>shift)&0x3fff);
                            return interpolate(av,bv,cv);
                        };
                        u=std::int32_t(component(0)>>(linear?0:4));v=std::int32_t(component(16)>>(linear?0:4));
                    } else if(perspective.ready) {
                        const auto uv=perspective.sample(wa,wb,wc);u=uv.first;v=uv.second;
                    } else {
                        const auto tex0=value[6+context];
                        u=gs_stq_perspective_texel_coordinate(
                            {std::uint32_t(a.st),std::uint32_t(b.st),std::uint32_t(c.st)},
                            {a.q,b.q,c.q},{wa,wb,wc},1u<<((tex0>>26)&0xf),linear?4:0);
                        v=gs_stq_perspective_texel_coordinate(
                            {std::uint32_t(a.st>>32),std::uint32_t(b.st>>32),std::uint32_t(c.st>>32)},
                            {a.q,b.q,c.q},{wa,wb,wc},1u<<((tex0>>30)&0xf),linear?4:0);
                    }
                    source=texture.shade(u,v,source);
                }
            }
            if(draw.prim_state&(1u<<5))
                source=gs_apply_fog(source,std::uint8_t(interpolate(a.fog,b.fog,c.fog)),std::uint32_t(value[0x3d]));
            const auto z=interpolate_u32(a.z,b.z,c.z);
            drawn=draw_depth_frame_pixel(context,std::uint32_t(x),std::uint32_t(y),source,z,draw.prim_state)||drawn;
        }
    }
    return drawn;
}

bool GsRegisterState::rasterize_draw_current(const GsDraw& draw) {
    const auto primitive=[&] {
        if(draw.primitive==0)return rasterize_point(draw);
        if(draw.primitive==1||draw.primitive==2)return rasterize_line(draw);
        if(draw.primitive>=3&&draw.primitive<=5)return rasterize_triangle(draw);
        if(draw.primitive==6)return rasterize_sprite(draw);
        throw std::runtime_error("unimplemented GS primitive rasterizer");
    };
    const auto dispatch=[&] {
        if(!profile_raster)return primitive();
        // HG-DIAG-005: opt-in host-only timing, including implicit flush sites.
        const auto start=std::chrono::steady_clock::now();
        const auto result=primitive();
        raster_nanoseconds[draw.primitive]+=std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now()-start).count();
        ++raster_calls[draw.primitive];
        return result;
    };
    return dispatch();
}

bool GsRegisterState::rasterize_draw(const GsDraw& draw) {
    if(!draw.environment_captured)return rasterize_draw_current(draw);
    const auto live=value;
    value=materialize_draw_environment(draw);
    try {const auto result=rasterize_draw_current(draw);value=live;return result;}
    catch(...) {value=live;throw;}
}

std::size_t GsRegisterState::rasterize_pending_draws() {
    std::size_t completed=0;
    auto* accelerator=gs_sprite_accelerator;
    if(accelerator)accelerator->begin_batch();
    const auto live=value;
    if(draw_environment_storage.empty()) {
        draw_environment_storage.resize(2);draw_environment_base()=live;raster_environment()=live;
    }
    const auto first=pending_draw_index();
    if(!raster_environment_active||raster_environment_draw_index!=first) {
        raster_environment()=draw_environment_base();raster_environment_event_count=0;
        raster_environment_draw_index=0;raster_environment_active=true;
        while(raster_environment_draw_index<first) {
            const auto& earlier=draws[raster_environment_draw_index];
            if(earlier.explicit_environment)raster_environment()=*earlier.explicit_environment;
            else if(earlier.environment_event_count!=(std::numeric_limits<std::uint32_t>::max)()) {
                if(earlier.environment_event_count<raster_environment_event_count||
                   earlier.environment_event_count>draw_environment_events.size())
                    throw std::runtime_error("invalid prior GS draw environment cursor");
                while(raster_environment_event_count<earlier.environment_event_count) {
                    const auto& change=draw_environment_events[raster_environment_event_count++];
                    raster_environment()[change.address]=change.value;
                }
            } else raster_environment()=live;
            ++raster_environment_draw_index;
        }
    }
    value=raster_environment();
    std::size_t event=raster_environment_event_count;
    bool runtime_environment=true;
    try {
    for(auto pending=first;pending<draws.size();++pending) {
        const auto& draw=draws[pending];
        if(draw.explicit_environment){value=*draw.explicit_environment;runtime_environment=false;}
        else if(draw.environment_event_count!=(std::numeric_limits<std::uint32_t>::max)()) {
            if(!runtime_environment){value=draw_environment_base();event=0;runtime_environment=true;}
            if(draw.environment_event_count<event||draw.environment_event_count>draw_environment_events.size())
                throw std::runtime_error("invalid ordered GS draw environment cursor");
            while(event<draw.environment_event_count) {
                const auto& change=draw_environment_events[event++];value[change.address]=change.value;
            }
        } else {value=live;runtime_environment=false;}
        // Advance only after the draw returns; an explicit unsupported
        // rasterizer fault leaves the queue position available to inspect.
        rasterize_draw_current(draw);
        // HG-ARCH-003: read-only, completed-draw shadow seam. No callback on faults.
        if(native_indexed_draw_observer)
            native_indexed_draw_observer->observe_completed_draw(draw,value,*this);
        ++rasterized_draw_count;
        ++completed;
        raster_environment()=value;raster_environment_event_count=event;
        raster_environment_draw_index=pending+1;
        raster_environment_active=runtime_environment;
    }
    } catch(...) {value=live;if(accelerator)accelerator->end_batch();throw;}
    value=live;
    if(accelerator)accelerator->end_batch();
    return completed;
}

} // namespace hg
