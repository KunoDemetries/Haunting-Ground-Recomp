#pragma once
#include "hg/gs.hpp"
#include <iostream>
#include <string>
#include <vector>

namespace gif_stream_regression {
struct Qword { std::uint64_t low,high; };
inline bool same_vertex(const hg::GsVertex& a,const hg::GsVertex& b) {
    return a.x==b.x&&a.y==b.y&&a.z==b.z&&a.rgba==b.rgba&&a.fog==b.fog&&
           a.st==b.st&&a.uv==b.uv&&a.q==b.q;
}
inline bool same_state(const hg::GsRegisterState& a,const hg::GsRegisterState& b) {
    if(a.value!=b.value||a.written!=b.written||a.gif_q!=b.gif_q||a.vram!=b.vram||
       a.privileged_imr!=b.privileged_imr||a.privileged_pmode!=b.privileged_pmode||
       a.privileged_smode2!=b.privileged_smode2||a.privileged_bgcolor!=b.privileged_bgcolor||
       a.privileged_dispfb!=b.privileged_dispfb||a.privileged_display!=b.privileged_display||
       a.privileged_busdir!=b.privileged_busdir||a.privileged_csr_events!=b.privileged_csr_events||
       a.privileged_signal_id!=b.privileged_signal_id||a.privileged_label_id!=b.privileged_label_id||
       a.crtc_configured!=b.crtc_configured||a.crtc_interlace!=b.crtc_interlace||
       a.crtc_mode!=b.crtc_mode||a.crtc_frame!=b.crtc_frame||
       a.rasterized_draw_count!=b.rasterized_draw_count||a.retired_draw_count!=b.retired_draw_count||
       a.clut!=b.clut||a.clut_valid!=b.clut_valid||a.clut_cbp!=b.clut_cbp||
       a.clut_cbp_valid!=b.clut_cbp_valid||a.clut_load_count!=b.clut_load_count||
       a.gif_to_vram_active!=b.gif_to_vram_active||a.host_transfer_completed!=b.host_transfer_completed||
       a.completed_transfer_format!=b.completed_transfer_format||a.transfer_x!=b.transfer_x||
       a.transfer_y!=b.transfer_y||a.transfer_width!=b.transfer_width||a.transfer_remaining!=b.transfer_remaining||
       a.vertex_queue.size()!=b.vertex_queue.size()||a.draws.size()!=b.draws.size())return false;
    for(std::size_t n=0;n<a.vertex_queue.size();++n)
        if(!same_vertex(a.vertex_queue[n],b.vertex_queue[n]))return false;
    for(std::size_t n=0;n<a.draws.size();++n) {
        const auto& x=a.draws[n];const auto& y=b.draws[n];
        if(x.primitive!=y.primitive||x.prim_state!=y.prim_state||x.xyoffset!=y.xyoffset||
           x.count!=y.count||a.materialize_draw_environment(x)!=b.materialize_draw_environment(y)||
           x.environment_captured!=y.environment_captured)return false;
        for(unsigned i=0;i<3;++i)if(!same_vertex(x.vertices[i],y.vertices[i]))return false;
    }
    return true;
}
// Frozen pre-streaming path. This deliberately uses the original materializing
// decoder rather than the candidate's packet consumer.
inline void reference_submit(hg::GifPath& path,Qword q,hg::GsRegisterState& gs) {
    const auto offset=path.pending.size();path.pending.resize(offset+16);
    for(unsigned i=0;i<8;++i) {
        path.pending[offset+i]=std::uint8_t(q.low>>(i*8));
        path.pending[offset+8+i]=std::uint8_t(q.high>>(i*8));
    }
    if(path.pending.size()>hg::GifPath::max_pending)
        throw std::runtime_error("GIF packet exceeds bounded path capacity");
    while(const auto size=hg::gif_packet_size(path.pending.data(),path.pending.size())) {
        const auto packet=hg::decode_gif_packet(path.pending.data(),*size);
        hg::apply_gif_register_transfers(packet,gs);
        path.pending.erase(path.pending.begin(),path.pending.begin()+*size);
    }
}
template<class F> std::string fault(F call) {
    try {call();return {};}
    catch(const std::exception& e){return e.what();}
}
inline hg::GsRegisterState initial_state() {
    hg::GsRegisterState gs;
    gs.write_ad(0,6);gs.write_ad(0x4c,1ull<<16);gs.write_ad(0x4e,1ull<<32);
    gs.write_ad(0x47,0x30000);gs.write_ad(0x40,(15ull<<16)|(15ull<<48));gs.write_ad(0x46,1);
    gs.gif_q=0x40000000;return gs;
}
inline std::uint64_t tag(unsigned format,unsigned nreg,unsigned loops,bool pre,bool eop) {
    return loops|(eop?0x8000ull:0)|(pre?1ull<<46:0)|(6ull<<47)|
           (std::uint64_t(format)<<58)|(std::uint64_t(nreg&15)<<60);
}
inline bool run_case(const std::vector<Qword>& words,const hg::GsRegisterState& initial,std::size_t& checks) {
    hg::GifPath actual,reference;hg::GsRegisterState a=initial,b=initial;
    actual.pending.reserve(3);reference.pending.reserve(71);
    for(const auto q:words) {
        const auto af=fault([&]{actual.submit_qword(q.low,q.high,a);});
        const auto bf=fault([&]{reference_submit(reference,q,b);});
        ++checks;
        if(af!=bf||actual.pending!=reference.pending||!same_state(a,b)) {
            std::cerr<<"GIF stream mismatch at checkpoint "<<checks<<" actual="<<af<<" reference="<<bf<<'\n';return false;
        }
        if(!af.empty())break;
    }
    // Pending suffixes do not commit early; rasterization and any explicit fault
    // must preserve identical pixels, draw history and partial state.
    const auto af=fault([&]{a.rasterize_pending_draws();});
    const auto bf=fault([&]{b.rasterize_pending_draws();});
    ++checks;
    if(af!=bf||!same_state(a,b))return false;
    actual.write_control(1);reference.write_control(1);
    return actual.pending==reference.pending&&same_state(a,b);
}
inline int run() {
    std::size_t checks=0;
    const auto initial=initial_state();
    // Every NREG encoding, including zero=16; odd REGLIST padding; PRE on/off;
    // NLOOP zero; leading NOPs and repeated descriptor cycles.
    for(unsigned format=0;format<2;++format)for(unsigned nreg=1;nreg<=16;++nreg)
    for(unsigned loops=0;loops<5;++loops)for(bool pre:{false,true}) {
        const unsigned safe[]{15,10,1,3};std::uint64_t descriptors=0;
        for(unsigned i=0;i<nreg;++i)descriptors|=std::uint64_t(safe[i%4])<<(i*4);
        std::vector<Qword> words{{tag(format,nreg,loops,pre,true),descriptors}};
        const auto count=format==0?loops*nreg:(loops*nreg+1)/2;
        for(unsigned i=0;i<count;++i)words.push_back({0x3f80000080402010ull+i,0x012345670000003dull+i});
        if(!run_case(words,initial,checks))return 1;
    }
    // All individual descriptor values, including unsupported values. Faults
    // happen after any earlier valid writes, with the complete packet retained.
    for(unsigned format=0;format<2;++format)for(unsigned descriptor=0;descriptor<16;++descriptor)
    for(bool pre:{false,true}) {
        std::vector<Qword> words{{tag(format,2,2,pre,true),10ull|(std::uint64_t(descriptor)<<4)}};
        for(unsigned i=0;i<(format==0?4u:2u);++i)words.push_back({0x0000000800000010ull+i,0x3d});
        if(!run_case(words,initial,checks))return 1;
    }
    // Multiple tags in one EOP packet; empty PRE tag and all-NOP tag must not
    // prematurely reset Q. A later real ST/RGBA transfer still has its reset.
    for(bool pre:{false,true}) {
        std::vector<Qword> words{{tag(0,1,0,true,false),0},
            {tag(0,1,2,false,false),15},{123,456},{789,1011},
            {tag(0,2,2,pre,false),0x12},{0,0x40000000},{0x80808080,0},
            {0,0x3f800000},{0x12345678,0}, {tag(1,1,0,true,true),0}};
        if(!run_case(words,initial,checks))return 1;
    }
    // IMAGE and IMAGE2, completion padding, and faults without an active upload.
    for(unsigned mode:{2u,3u})for(unsigned format:{0u,1u,2u,0x13u})
    for(unsigned count:{0u,1u,2u,3u,4u,5u,16u}) {
        auto gs=initial_state();gs.write_ad(0x50,(1ull<<48)|(std::uint64_t(format)<<56));
        gs.write_ad(0x51,0);gs.write_ad(0x52,8ull|(2ull<<32));gs.write_ad(0x53,0);
        std::vector<Qword> words{{tag(mode,1,count,true,true),0}};
        for(unsigned n=0;n<count;++n)words.push_back({0x1122334455667788ull+n,0xabcdef9080706050ull-n});
        if(!run_case(words,gs,checks))return 1;
        if(!run_case(words,initial,checks))return 1;
    }
    { // Multi-tag IMAGE packet must stay on the original materializing boundary.
        auto gs=initial_state();gs.write_ad(0x50,1ull<<48);
        gs.write_ad(0x51,0);gs.write_ad(0x52,4ull|(2ull<<32));gs.write_ad(0x53,0);
        const std::vector<Qword> words{{tag(2,1,1,true,false),0},
            {0x1122334455667788ull,0xabcdef9080706050ull},
            {tag(3,1,1,true,true),0},
            {0x8877665544332211ull,0x1020304050607080ull}};
        if(!run_case(words,gs,checks))return 1;
    }
    // Render a sprite via A+D, then FINISH, followed optionally by an invalid
    // register. This checks visible pixels already committed before the fault.
    for(bool invalid:{false,true}) {
        const auto xy=[](unsigned x,unsigned y){return std::uint64_t(x*16)|(std::uint64_t(y*16)<<16);};
        std::vector<Qword> words{{tag(0,1,invalid?6:5,false,true),14},
            {6,0},{0x3f80000080443322ull,1},{xy(1,1),5},{xy(8,8),5},{0,0x61}};
        if(invalid)words.push_back({123,0x80});
        if(!run_case(words,initial,checks))return 1;
    }
    { // Two SIGNALs fault at the same operation after preserving the first ID.
        const std::vector<Qword> words{{tag(0,1,3,false,true),14},
            {0xffffffff12345678ull,0x60},{123,0x3d},{0xffffffff87654321ull,0x60}};
        if(!run_case(words,initial,checks))return 1;
    }
    { // Reset while a multi-tag packet is incomplete; GS state is not reset.
        hg::GifPath a,b;auto x=initial_state(),y=x;
        const Qword header{tag(0,1,4,false,true),14};
        a.submit_qword(header.low,header.high,x);reference_submit(b,header,y);
        a.submit_qword(123,0x3d,x);reference_submit(b,{123,0x3d},y);
        if(!same_state(x,initial)||a.pending!=b.pending)return 1;
        a.write_control(1);b.write_control(1);
        a.submit_qword(0x8000,0,x);reference_submit(b,{0x8000,0},y);
        if(a.pending!=b.pending||!same_state(x,y))return 1;++checks;
    }
    { // Capacity check precedes any parse and retains the newly appended qword.
        hg::GifPath a,b;auto x=initial_state(),y=x;
        a.pending.resize(hg::GifPath::max_pending);b.pending=a.pending;
        const auto af=fault([&]{a.submit_qword(123,456,x);});
        const auto bf=fault([&]{reference_submit(b,{123,456},y);});
        if(af.empty()||af!=bf||a.pending!=b.pending||!same_state(x,y))return 1;++checks;
    }
    std::cout<<"GIF complete-packet streaming: "<<checks<<" state/fault checkpoints passed\n";
    return 0;
}
} // namespace gif_stream_regression
