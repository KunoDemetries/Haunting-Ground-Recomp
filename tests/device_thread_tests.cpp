// Synthetic equivalence tests for the host device thread (device_link.hpp):
// the same EE-visible operations applied synchronously and through the device
// thread must give identical guest-visible results at every observation point.
#include "hg/runtime.hpp"
#include "hg/device_thread.hpp"
#include "hg/gif.hpp"
#include "hg/vu1_spec.hpp"
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
int failures=0;
#define CHECK(condition) do{if(!(condition)){++failures;std::cerr<<"Failed line "<<__LINE__<<": "#condition"\n";}}while(0)

constexpr std::uint32_t source=0x00100000u;

void set_test_environment(const char* name,const char* value) {
#ifdef _WIN32
    const auto result=_putenv_s(name,value?value:"");
#else
    const auto result=value?setenv(name,value,1):unsetenv(name);
#endif
    if(result!=0)throw std::runtime_error("failed to update test environment");
}

struct Pair {
    std::unique_ptr<hg::State> sync=std::make_unique<hg::State>();
    std::unique_ptr<hg::State> dev=std::make_unique<hg::State>();
    std::unique_ptr<hg::DeviceLink> link;
    Pair() {
        for(auto* s:{sync.get(),dev.get()})s->dmac.control=1;
        link=hg::start_device_thread(dev->vif1,dev->gif,dev->gs,{});
        dev->device=link.get();
    }
    ~Pair() {if(link)link->stop();}
    template<class F> void both(F&& f) {f(*sync);f(*dev);}
};

void put_words(hg::State& s,std::uint32_t at,const std::vector<std::uint32_t>& words) {
    for(std::size_t n=0;n<words.size();++n)std::memcpy(s.ram.data()+at+n*4,&words[n],4);
}
// Normal-mode VIF1 (channel 1) or GIF (channel 2) transfer from RAM.
void start(hg::State& s,unsigned channel,std::uint32_t qwords) {
    auto& c=s.dmac.channels[channel];c.madr=source;c.qwc=qwords;c.chcr=0x101;
}
std::vector<std::uint32_t> pad_to_qwords(std::vector<std::uint32_t> words) {
    while(words.size()%4)words.push_back(0);
    return words;
}
// VIF1 stream: STCYCL, UNPACK V4-32 x2, MARK, then DIRECT carrying one PACKED A+D GIF packet.
std::vector<std::uint32_t> vif_stream(const std::vector<std::pair<std::uint64_t,std::uint64_t>>& ad) {
    std::vector<std::uint32_t> w{0x01000404u,0x6c020000u};
    for(std::uint32_t n=0;n<8;++n)w.push_back(0x3f800000u+n);
    w.push_back(0x07001234u);
    while(w.size()%4!=3)w.push_back(0);
    w.push_back(0x50000000u|std::uint32_t(ad.size()+1));
    const std::uint64_t tag=std::uint64_t(ad.size())|(1ull<<15)|(1ull<<60);
    w.push_back(std::uint32_t(tag));w.push_back(std::uint32_t(tag>>32));w.push_back(0xeu);w.push_back(0);
    for(const auto& [data,reg]:ad) {
        w.push_back(std::uint32_t(data));w.push_back(std::uint32_t(data>>32));
        w.push_back(std::uint32_t(reg));w.push_back(std::uint32_t(reg>>32));
    }
    return pad_to_qwords(w);
}
std::string fault_text(const std::function<void()>& work) {
    try {work();} catch(const std::exception& e) {return e.what();}
    return {};
}

// Pump one qword at a time and compare the EE-observable interrupt state after
// each one, clearing INTC between steps so repeated raises are also compared.
void pump_compare(Pair& p,unsigned channel) {
    for(;;) {
        bool a=false,b=false;
        if(channel==1){a=p.sync->pump_vif1();b=p.dev->pump_vif1();}
        else {a=p.sync->pump_gif();b=p.dev->pump_gif();}
        CHECK(a==b);
        CHECK(p.sync->intc.status==p.dev->intc.status);
        p.both([](hg::State& s){s.intc.status=0;});
        if(!a||!b)break;
    }
}
void compare_device_state(Pair& p) {
    p.dev->device->join();
    const auto& a=*p.sync;const auto& b=*p.dev;
    CHECK(a.vif1.vu_mem==b.vif1.vu_mem);
    CHECK(a.vif1.vu_mem_defined==b.vif1.vu_mem_defined);
    CHECK(a.vif1.mark==b.vif1.mark && a.vif1.mark_detected==b.vif1.mark_detected);
    CHECK(a.gs.value==b.gs.value);
    CHECK(a.gs.privileged_csr_events==b.gs.privileged_csr_events);
    CHECK(a.gs.privileged_signal_id==b.gs.privileged_signal_id);
    CHECK(a.gs.privileged_label_id==b.gs.privileged_label_id);
    CHECK(a.gs.privileged_imr==b.gs.privileged_imr);
    CHECK(a.dmac.channels[1].chcr==b.dmac.channels[1].chcr && a.dmac.status==b.dmac.status);
}

void masked_and_unmasked_signal() {
    for(const std::uint64_t imr:{0x1f00ull,0x1e00ull,0x1c00ull}) {
        Pair p;
        p.both([&](hg::State& s){s.store(0x12001010u,8,imr);});
        const auto words=vif_stream({{(0xffffffffull<<32)|0x55ull,0x60},{0x77ull,0x62},{0,0x61}});
        p.both([&](hg::State& s){put_words(s,source,words);start(s,1,std::uint32_t(words.size()/4));});
        pump_compare(p,1);
        compare_device_state(p);
        // CSR is a device observation: both must report SIGNAL/FINISH.
        std::uint64_t csr[2]{};
        csr[0]=p.sync->load(0x12001000u,8,false);csr[1]=p.dev->load(0x12001000u,8,false);
        CHECK(csr[0]==csr[1] && (csr[0]&3)==3);
        if(imr!=0x1f00)CHECK(p.dev->device->unmasked_interrupt_joins>0);
        else CHECK(p.dev->device->unmasked_interrupt_joins==0);
        // Clearing the events through CSR and raising VSINT keeps the mirror exact.
        p.both([](hg::State& s){s.store(0x12001000u,8,3);s.store(0x12001010u,8,0x1700);s.gs_raise_csr_events(8);});
        CHECK(p.sync->intc.status==p.dev->intc.status && (p.sync->intc.status&1));
        compare_device_state(p);
        CHECK(p.sync->gs_imr()==p.dev->gs_imr());
    }
}

void path3_and_registers() {
    Pair p;
    std::vector<std::uint32_t> w;
    const std::uint64_t tag=2ull|(1ull<<15)|(1ull<<60);
    w={std::uint32_t(tag),std::uint32_t(tag>>32),0xe,0, 0x99,0,0x62,0, 0x5ull,0,0x60,0};
    p.both([&](hg::State& s){
        put_words(s,source,w);start(s,2,3);
        s.store(0x10003c30u,4,0x4321);       // VIF1 MARK
        s.store(0x12000070u,8,0x1234);      // DISPFB1
    });
    pump_compare(p,2);
    compare_device_state(p);
    CHECK(p.sync->load(0x10003c30u,4,false)==p.dev->load(0x10003c30u,4,false));
    CHECK(p.sync->load(0x10003020u,4,false)==p.dev->load(0x10003020u,4,false));
    p.dev->device->join();
    CHECK(p.sync->gs.privileged_dispfb==p.dev->gs.privileged_dispfb);
    // GS reset through CSR runs synchronously on the device thread.
    p.both([](hg::State& s){s.store(0x12001000u,8,1ull<<9);});
    compare_device_state(p);
    CHECK(p.dev->device->imr==0x1f00 && p.dev->device->csr_events==0);
}

void failure_is_reported_at_next_observation() {
    Pair p;
    const auto words=pad_to_qwords({0x80000000u});
    p.both([&](hg::State& s){put_words(s,source,words);start(s,1,1);});
    const auto expected=fault_text([&]{while(p.sync->pump_vif1()){}});
    CHECK(!expected.empty());
    CHECK(fault_text([&]{while(p.dev->pump_vif1()){}}).empty());
    const auto reported=fault_text([&]{p.dev->load(0x10003c00u,4,false);});
    CHECK(reported==expected);
    // The failure stays sticky and stop() returns it.
    CHECK(p.link->stop()!=nullptr);
}

void ring_wraps_under_backpressure() {
    Pair p;
    // 1.25M qwords of VIF NOPs exceed the 1M-entry ring, exercising wait_space.
    const std::uint32_t qwords=1250000;
    p.both([&](hg::State& s){std::memset(s.ram.data()+source,0,std::size_t(qwords)*16);start(s,1,qwords);});
    while(p.sync->pump_vif1()){}
    while(p.dev->pump_vif1()){}
    compare_device_state(p);
    CHECK(p.sync->load(0x10003c00u,4,false)==p.dev->load(0x10003c00u,4,false));
}

// XGKICK through MSCAL: entry 0 kicks a complete A+D packet; entry 8 marks the
// packet's last qword undefined, so XGKICK faults after submitting the prefix.
void kick_executor(hg::Vif1Path& v,std::uint16_t entry,hg::GifPath& gif,hg::GsRegisterState& gs) {
    const std::uint64_t tag=2ull|(1ull<<15)|(1ull<<60);
    const std::uint64_t q[3][2]={{tag,0xe},{(0xffffffffull<<32)|(0x42u+entry),0x62},{(0xffffffffull<<32)|(7u+entry),0x60}};
    for(unsigned n=0;n<3;++n) {
        v.vu_mem[n*4]=std::uint32_t(q[n][0]);v.vu_mem[n*4+1]=std::uint32_t(q[n][0]>>32);
        v.vu_mem[n*4+2]=std::uint32_t(q[n][1]);v.vu_mem[n*4+3]=std::uint32_t(q[n][1]>>32);
        v.vu_mem_defined[n]=0x0f;
    }
    if(entry==8)v.vu_mem_defined[2]=0;
    v.vu1.vi[1]=0;
    v.xgkick(1,gif,gs);
}
void xgkick_blocks() {
    for(const bool faulting:{false,true}) {
        Pair p;
        p.both([&](hg::State& s){
            s.vif1.vu1_executor=&kick_executor;
            const auto words=pad_to_qwords({0x14000000u,0x00000000u,faulting?0x14000001u:0u});
            put_words(s,source,words);start(s,1,1);
        });
        const auto expected=fault_text([&]{while(p.sync->pump_vif1()){}});
        CHECK(fault_text([&]{while(p.dev->pump_vif1()){}}).empty());
        const auto reported=fault_text([&]{p.dev->load(0x10003c00u,4,false);});
        if(faulting) {
            CHECK(!expected.empty() && reported==expected);
            CHECK(p.link->stop()!=nullptr);
            CHECK(p.sync->gif.pending==p.dev->gif.pending && !p.sync->gif.pending.empty());
            CHECK(p.sync->gs.privileged_label_id==p.dev->gs.privileged_label_id);
        } else {
            CHECK(expected.empty() && reported.empty());
            compare_device_state(p);
            CHECK(p.dev->gs.privileged_label_id==0x42 && p.dev->gif.pending.empty());
        }
    }
}

// The B-stage proxy GifPath tracks only packet boundaries; it must agree with
// the real assembler's pending.empty() (gif_packet_size rules) after every qword.
struct CollectForward final : hg::GifForward {
    std::size_t count=0;
    void forward_qword(std::uint64_t,std::uint64_t) override {++count;}
    void forward_words(const std::uint32_t*,std::size_t qwords) override {count+=qwords;}
};
void proxy_packet_boundaries() {
    CollectForward sink;hg::GifPath proxy;proxy.forward=&sink;hg::GsRegisterState unused;
    std::vector<std::uint8_t> reference;
    std::uint64_t seed=0x9e3779b97f4a7c15ull;
    const auto next=[&]{seed^=seed<<13;seed^=seed>>7;seed^=seed<<17;return seed;};
    std::size_t submitted=0,idle_points=0;
    for(unsigned packet=0;packet<20000;++packet) {
        if(packet%997==0) {
            proxy.write_control(1);reference.clear();
            CHECK(proxy.packet_idle());
        }
        const auto tags=1+next()%3;
        for(unsigned t=0;t<tags;++t) {
            const auto nloop=(next()%5==0)?0:next()%6;
            const auto format=next()%4,nreg=next()%16;
            const bool eop=t+1==tags;
            const std::uint64_t tag=nloop|(eop?1ull<<15:0)|(std::uint64_t(format)<<58)|(std::uint64_t(nreg)<<60);
            const auto regs=nreg?nreg:16;
            const auto data=nloop==0?0:format>=2?nloop:format==0?nloop*regs:(nloop*regs+1)/2;
            for(std::uint64_t q=0;q<=data;++q) {
                const std::uint64_t low=q?next():tag,high=next();
                proxy.submit_qword(low,high,unused);++submitted;
                for(unsigned i=0;i<8;++i)reference.push_back(std::uint8_t(low>>(8*i)));
                for(unsigned i=0;i<8;++i)reference.push_back(std::uint8_t(high>>(8*i)));
                while(const auto size=hg::gif_packet_size(reference.data(),reference.size()))
                    reference.erase(reference.begin(),reference.begin()+std::ptrdiff_t(*size));
                CHECK(proxy.packet_idle()==reference.empty());
                if(reference.empty())++idle_points;
            }
        }
    }
    CHECK(sink.count==submitted && idle_points>1000);
}

void ordered_closures() {
    Pair p;
    std::vector<int> order;
    p.dev->device->post([&]{order.push_back(1);});
    p.dev->device->push(hg::DeviceKind::rasterize,0,0);
    p.dev->device->run_sync([&]{order.push_back(2);});
    CHECK((order==std::vector<int>{1,2}));
}
}

// Speculative VU1 activations (HG_VU_WORKERS): a synthetic program declaring its
// written registers like the AOT emitter. Batches depend on the init matrix (VF1),
// on clip history from the previous batch, sometimes on a shared memory counter
// written by the previous batch, and kick a GIF packet; DIRECT packets interleave.
constexpr std::uint32_t spec_fault_marker=0x0000dead;
void spec_executor(hg::Vif1Path& v,std::uint16_t entry,hg::GifPath& gif,hg::GsRegisterState& gs) {
    auto& u=v.vu1;
    if(!entry) {
        u.spec_written_vf[0]|=0xf0;
        u.advance_pipeline(1);u.load_qword(v.vu_mem,v.vu_mem_defined,1,0,0,15);u.produced_vf(1,15);
        u.fcset(0);u.tpc=8;return;
    }
    u.spec_written_vf[0]|=0xfff00f00ull;
    u.spec_written_misc|=(1u<<1)|(1u<<2)|(1u<<3)|(1u<<hg::vu1_spec_q_bit);
    u.advance_pipeline(1);u.write_vi(2,std::uint16_t(v.top));u.produced_vi(2,1);
    u.advance_pipeline(1);u.require_vi(2);
    u.load_qword(v.vu_mem,v.vu_mem_defined,2,2,0,15);u.produced_vf(2,15);
    u.advance_pipeline(1);u.require_vf(2,15);u.require_vf(1,15);
    for(unsigned lane=0;lane<4;++lane)u.write_vf(2,lane,u.read_vf(2,lane)+u.read_vf(1,lane));
    u.produced_vf(2,15);
    // Clip history: VF7.x exceeds |w| when the input's x is odd.
    for(unsigned lane=0;lane<4;++lane)u.write_vf(7,lane,lane==3?0x3f800000u:(lane==0&&(u.read_vf(2,0)-u.read_vf(1,0))&1)?0x40000000u:0x3f000000u);
    u.produced_vf(7,15);u.advance_pipeline(4);u.require_vf(7,15);
    u.clip_test(7,7);
    u.fcand(0x000fff);u.produced_vi(1,1);u.advance_pipeline(1);u.require_vi(1);
    u.write_vi(3,u.read_vi(1));u.produced_vi(3,1);
    if(u.read_vf(2,1)&1) { // Shared counter in vector 200 (memory dependency chain).
        u.spec_written_misc|=1u<<4;
        u.load_integer(v.vu_mem,v.vu_mem_defined,4,0,200,8);
        u.produced_vi(4,4);u.advance_pipeline(1);u.require_vi(4);
        u.write_vi(4,std::uint16_t(u.read_vi(4)+1));u.produced_vi(4,1);
        u.store_integer(v.vu_mem,v.vu_mem_defined,4,0,200,8);
    }
    // Partial-lane traffic on shared vector 300: SQ.xyz here, V3 UNPACKs in the stream.
    if(u.read_vf(2,1)&2) {
        u.spec_written_vf[0]|=0xf00000000ull;
        u.load_qword(v.vu_mem,v.vu_mem_defined,8,0,300,15);u.produced_vf(8,15);
    }
    if(u.read_vf(2,1)&8)u.store_qword(v.vu_mem,v.vu_mem_defined,2,0,300,14);
    u.wait_q();u.divide(2,1,0,3);
    u.store_qword(v.vu_mem,v.vu_mem_defined,2,2,1,15);
    const std::uint64_t tag=1ull|(1ull<<15)|(1ull<<60);
    const std::uint64_t data=(std::uint64_t(u.read_vi(3)+u.read_vi(4)*2u)<<32)|u.read_vf(2,0);
    for(unsigned lane=0;lane<4;++lane) {
        u.write_vf(5,lane,lane==0?std::uint32_t(tag):lane==1?std::uint32_t(tag>>32):lane==2?0xeu:0u);
        u.write_vf(6,lane,lane==0?std::uint32_t(data):lane==1?std::uint32_t(data>>32):lane==2?0x62u:0u);
    }
    u.produced_vf(5,15);u.produced_vf(6,15);
    u.advance_pipeline(4);u.require_vf(5,15);u.require_vf(6,15);
    u.store_qword(v.vu_mem,v.vu_mem_defined,5,2,2,15);
    u.store_qword(v.vu_mem,v.vu_mem_defined,6,2,3,15);
    u.write_vi(1,std::uint16_t(v.top+2));u.produced_vi(1,1);u.advance_pipeline(1);u.require_vi(1);
    v.xgkick(1,gif,gs);
    if(u.read_vf(2,2)-u.read_vf(1,2)==spec_fault_marker)throw std::runtime_error("synthetic VU1 fault");
    u.tpc=(u.read_vf(2,1)&4)?0x08:0x10; // MSCNT continuation varies.
}
hg::Vu1SpecInfo spec_info(const hg::Vif1Path&,std::uint16_t entry) {
    if(!entry)return {0,0,0,true}; // Init: no inputs; runs inline when jobs are in flight.
    if(entry!=8&&entry!=0x10)return {};
    return {0xf0,0,0,true};
}
std::vector<std::uint32_t> spec_stream(bool faulting) {
    std::vector<std::uint32_t> w{0x01000404u,0x03000020u,0x02000040u};
    const auto unpack=[&](std::uint32_t code,std::array<std::uint32_t,4> v){w.push_back(code);w.insert(w.end(),v.begin(),v.end());};
    unpack(0x6c0100c8u,{0,0,0,0});
    for(unsigned k=0;k<40;++k) {
        if(k%9==0) {unpack(0x6c010000u,{k*7u,3u,k,0x3f800000u});w.push_back(0x14000000u);}
        const auto clip_bits=(k%4==3||k%6==1)?2u*k+1u:2u*k; // Some batches leave nonzero clip history.
        unpack(0x6c018000u,{clip_bits,((k%3==0)?1u:2u)|((k%7==5)?4u:0u)|((k%5==2)?8u:0u),faulting&&k==23?spec_fault_marker:k,0x3f800000u});
        w.push_back(k%9==0||k%5==2?0x14000001u:0x17000000u); // MSCAL or MSCNT.
        if(k%5==3){w.push_back(0x6801012cu);w.insert(w.end(),{k,k+1,k+2});} // UNPACK V3-32 to 300.
        if(k%4==1) { // DIRECT A+D LABEL write between kicks.
            while(w.size()%4!=3)w.push_back(0);
            w.push_back(0x50000002u);
            const std::uint64_t tag=1ull|(1ull<<15)|(1ull<<60);
            w.insert(w.end(),{std::uint32_t(tag),std::uint32_t(tag>>32),0xeu,0u,0x1000u+k,0xffffffffu,0x62u,0u});
        }
    }
    return pad_to_qwords(w);
}
void speculative_vu1() {
    for(const bool faulting:{false,true})for(const char* workers:{"1","2","3"}) {
        set_test_environment("HG_VU_WORKERS",workers);
        const auto saved=hg::vu1_spec_info_query;
        hg::vu1_spec_info_query=&spec_info;
        Pair p;
        p.both([&](hg::State& s){s.vif1.vu1_executor=&spec_executor;});
        // The device thread captured the executor at start; restart it with the synthetic one.
        p.link->stop();p.link=hg::start_device_thread(p.dev->vif1,p.dev->gif,p.dev->gs,{});p.dev->device=p.link.get();
        set_test_environment("HG_VU_WORKERS",nullptr);
        const auto words=spec_stream(faulting);
        p.both([&](hg::State& s){put_words(s,source,words);start(s,1,std::uint32_t(words.size()/4));});
        const auto expected=fault_text([&]{while(p.sync->pump_vif1()){}});
        CHECK(fault_text([&]{while(p.dev->pump_vif1()){}}).empty());
        const auto reported=fault_text([&]{p.dev->load(0x10003c00u,4,false);});
        CHECK(reported==expected);
        CHECK(faulting!=expected.empty());
        if(faulting)p.link->stop();
        else compare_device_state(p);
        const auto& a=p.sync->vif1;const auto& b=p.dev->vif1;
        CHECK(a.vu_mem==b.vu_mem && a.vu_mem_defined==b.vu_mem_defined);
        const auto& x=a.vu1;const auto& y=b.vu1;
        CHECK(x.vf==y.vf && x.vf_defined==y.vf_defined && x.vi==y.vi && x.acc==y.acc && x.acc_defined==y.acc_defined);
        CHECK(x.clip==y.clip && x.status==y.status && x.mac==y.mac && x.q==y.q && x.i==y.i && x.p==y.p);
        CHECK(x.pending_q==y.pending_q && x.pending_p==y.pending_p && x.q_pending==y.q_pending && x.p_pending==y.p_pending);
        CHECK(x.q_cycles_remaining==y.q_cycles_remaining && x.p_cycles_remaining==y.p_cycles_remaining && x.tpc==y.tpc);
        CHECK(x.issue_cycle==y.issue_cycle && x.vf_ready==y.vf_ready && x.vi_ready==y.vi_ready);
        CHECK(p.sync->gs.privileged_label_id==p.dev->gs.privileged_label_id);
        CHECK(p.sync->gif.pending==p.dev->gif.pending);
        hg::vu1_spec_info_query=saved;
    }
}

// Lane-granular memory inputs: even batches SQ.xyzw vector 400, odd batches
// SQ.xyz it and then LQ.xyzw, so only W comes from the in-flight predecessor.
// Each batch records the loaded VF9 in its own vector; no other dependencies.
void spec_lane_executor(hg::Vif1Path& v,std::uint16_t,hg::GifPath&,hg::GsRegisterState&) {
    auto& u=v.vu1;
    u.spec_written_vf[0]|=0xf00f000f00ull;
    u.spec_written_misc|=(1u<<2)|(1u<<5);
    u.advance_pipeline(1);u.write_vi(2,std::uint16_t(v.top));u.produced_vi(2,1);
    u.advance_pipeline(1);u.require_vi(2);
    u.load_qword(v.vu_mem,v.vu_mem_defined,2,2,0,15);u.produced_vf(2,15);
    u.advance_pipeline(4);u.require_vf(2,15);
    for(unsigned lane=0;lane<4;++lane)u.write_vf(10,lane,lane==3?u.read_vf(2,1)*7u+1u:u.read_vf(2,lane));
    u.produced_vf(10,15);u.advance_pipeline(4);u.require_vf(10,15);
    u.store_qword(v.vu_mem,v.vu_mem_defined,10,0,400,(u.read_vf(2,0)&1)?14:15);
    u.load_qword(v.vu_mem,v.vu_mem_defined,9,0,400,15);u.produced_vf(9,15);
    u.write_vi(5,std::uint16_t(500+u.read_vf(2,1)));u.produced_vi(5,1);
    u.advance_pipeline(4);u.require_vf(9,15);u.require_vi(5);
    u.store_qword(v.vu_mem,v.vu_mem_defined,9,5,0,15);
    u.tpc=8;
}
hg::Vu1SpecInfo spec_lane_info(const hg::Vif1Path&,std::uint16_t entry) {
    if(entry!=8)return {};
    return {0,0,0,true};
}
void speculative_vu1_partial_lanes() {
    for(const char* workers:{"1","2","3"}) {
        set_test_environment("HG_VU_WORKERS",workers);
        const auto saved=hg::vu1_spec_info_query;
        hg::vu1_spec_info_query=&spec_lane_info;
        Pair p;
        p.both([&](hg::State& s){s.vif1.vu1_executor=&spec_lane_executor;});
        p.link->stop();p.link=hg::start_device_thread(p.dev->vif1,p.dev->gif,p.dev->gs,{});p.dev->device=p.link.get();
        set_test_environment("HG_VU_WORKERS",nullptr);
        std::vector<std::uint32_t> w{0x01000404u,0x03000020u,0x02000040u};
        for(unsigned k=0;k<64;++k) {
            w.push_back(0x6c018000u);w.insert(w.end(),{k,k,k*3u,0x3f800000u});
            w.push_back(0x14000001u);
        }
        const auto words=pad_to_qwords(w);
        p.both([&](hg::State& s){put_words(s,source,words);start(s,1,std::uint32_t(words.size()/4));});
        const auto expected=fault_text([&]{while(p.sync->pump_vif1()){}});
        CHECK(expected.empty());
        CHECK(fault_text([&]{while(p.dev->pump_vif1()){}}).empty());
        CHECK(fault_text([&]{p.dev->load(0x10003c00u,4,false);}).empty());
        compare_device_state(p);
        const auto& a=p.sync->vif1;const auto& b=p.dev->vif1;
        CHECK(a.vu_mem==b.vu_mem && a.vu_mem_defined==b.vu_mem_defined);
        CHECK(a.vu1.vf==b.vu1.vf && a.vu1.vi==b.vu1.vi);
        hg::vu1_spec_info_query=saved;
    }
}

int main() {
    masked_and_unmasked_signal();
    path3_and_registers();
    failure_is_reported_at_next_observation();
    ring_wraps_under_backpressure();
    ordered_closures();
    proxy_packet_boundaries();
    xgkick_blocks();
    speculative_vu1();
    speculative_vu1_partial_lanes();
    if(failures){std::cerr<<failures<<" device thread checks failed\n";return 1;}
    std::cout<<"Device thread tests passed\n";
    return 0;
}
