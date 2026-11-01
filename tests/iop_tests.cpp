#include "hg/iop.hpp"
#include "hg/iop_thread.hpp"
#include "hg/iop_interrupt.hpp"
#include "hg/iop_reboot.hpp"
#include <iostream>
#include <memory>
namespace hg {void run_iop(IopState&,std::uint64_t);void run_iop_burst(IopState&,std::uint64_t);bool compiled_iop_instruction_trace();}
#ifndef HG_TEST_IOP_TRACE
#define HG_TEST_IOP_TRACE 1
#endif
#define CHECK(x) do {if(!(x)){std::cerr<<"Failed line "<<__LINE__<<": "<<#x<<'\n';return 1;}}while(0)
#if defined(_MSC_VER)
__declspec(noinline)
#else
__attribute__((noinline))
#endif
int test_multi_record_dvd() {
    std::array<std::uint8_t,2064> record{};
    auto multi_dvd_storage=std::make_unique<hg::IopState>();
    auto& multi_dvd=*multi_dvd_storage;
    multi_dvd.cdvd_n_parameters={3,0,0,0,2,0,0,0,0,2,0};
    multi_dvd.submit_cdvd_n_command(8);
    multi_dvd.dmac.channel[3]={0x3000,0x0056000c,0x41000200};
    multi_dvd.enable_interrupt(35);
    record[12]=0xb1;multi_dvd.complete_cdvd_dma(record);
    CHECK(multi_dvd.cdvd_dma_ready() && multi_dvd.cdvd_dma_request().lba==4);
    CHECK(multi_dvd.dmac.channel[3][0]==0x3810 && multi_dvd.dmac.channel[3][1]==0x002b000c);
    CHECK(!multi_dvd.cdvd_irq_pending && !(multi_dvd.dmac.completed_channels&(1u<<3)));
    record[12]=0xb2;multi_dvd.complete_cdvd_dma(record);
    CHECK(!multi_dvd.cdvd_dma_ready() && multi_dvd.dmac.channel[3][1]==12);
    CHECK(multi_dvd.load(0x300c,1,false)==0xb1 && multi_dvd.load(0x381c,1,false)==0xb2);
    CHECK(multi_dvd.cdvd_irq_pending && (multi_dvd.dmac.completed_channels&(1u<<3)));
    // A two-sector command split across two independently armed descriptors
    // raises DMA completion for each, but drive completion only for the last.
    multi_dvd.cdvd_irq_pending=false;multi_dvd.cdvd_interrupt_status=0;multi_dvd.dmac.completed_channels=0;
    multi_dvd.cdvd_n_parameters={5,0,0,0,2,0,0,0,0,2,0};multi_dvd.submit_cdvd_n_command(8);
    multi_dvd.dmac.channel[3]={0x5000,0x002b000c,0x41000200};
    multi_dvd.complete_cdvd_dma(record);
    CHECK(multi_dvd.cdvd_read_pending && multi_dvd.cdvd_sectors==1 && !multi_dvd.cdvd_dma_ready());
    CHECK((multi_dvd.dmac.completed_channels&(1u<<3)) && !multi_dvd.cdvd_irq_pending && multi_dvd.cdvd_interrupt_status==0);
    CHECK(multi_dvd.cdvd_drive_status==6);
    multi_dvd.dmac.completed_channels=0;
    multi_dvd.dmac.channel[3]={0x6000,0x002b000c,0x41000200};multi_dvd.complete_cdvd_dma(record);
    CHECK(!multi_dvd.cdvd_read_pending && multi_dvd.cdvd_irq_pending && multi_dvd.cdvd_interrupt_status==3);
    CHECK(multi_dvd.load(0x500c,1,false)==0xb2 && multi_dvd.load(0x600c,1,false)==0xb2);
    // Reject a descriptor exceeding the remaining command before writing RAM.
    multi_dvd.cdvd_n_parameters={3,0,0,0,1,0,0,0,0,2,0};
    multi_dvd.submit_cdvd_n_command(8);
    multi_dvd.dmac.channel[3]={0x4000,0x0056000c,0x41000200};
    bool excessive_dvd=false;try{multi_dvd.complete_cdvd_dma(record);}catch(const hg::IopFault&){excessive_dvd=true;}
    CHECK(excessive_dvd && multi_dvd.load(0x400c,1,false)==0 && multi_dvd.cdvd_lba==3);
    return 0;
}
int main() {
    CHECK(hg::compiled_iop_instruction_trace()==bool(HG_TEST_IOP_TRACE));
    {
        auto event=std::make_unique<hg::IopState>();auto& s=*event;
        s.pc=0x9c610;s.current_thread=1;s.gpr[4]=0x300;s.gpr[5]=2;s.gpr[31]=0x1234;
        s.pending_reg=4;s.pending_value=0x400;
        for(unsigned n=0;n<8;++n)s.store(0x300+4*n,4,0x8000+n);
        const auto ram_before=s.ram;
        hg::run_iop(s,1);
        CHECK(s.sifman_submit_trace_cursor==1 && s.pc==0x9c614 && s.gpr[4]==0x400 && !s.pending_reg);
        const auto& trace=s.sifman_submit_traces[0];
        CHECK(trace.valid && trace.descriptors_valid && trace.descriptor==0x300 && trace.count==2 && trace.thread==1);
        CHECK(trace.return_pc==0x1234 && trace.descriptors[0]==0x8000 && trace.descriptors[7]==0x8007);
        s.pc=0x9c838;s.gpr[2]=0x440002;s.pending_reg=2;s.pending_value=0x55;
        hg::run_iop(s,1);
        CHECK(trace.return_seen && trace.submission_id==0x440002 && s.gpr[2]==0x55 && !s.pending_reg);
        CHECK(s.last_sifman_two_descriptor_trace.submission_id==0x440002);
        CHECK(s.ram==ram_before && s.virtual_time_us==0 && s.pc_trace_cursor==(HG_TEST_IOP_TRACE?2u:0u));
        // Unsupported PCs still fault in a compiled page and outside all pages.
        for(const auto pc:{0x9c800u,0xdead000u}) {
            s.pc=pc;bool rejected=false;
            try{hg::run_iop(s,1);}catch(const hg::IopFault& e){rejected=e.pc==pc;}
            CHECK(rejected);
        }
        s.pc=0x9c610;s.pending_reg=0;s.gpr[4]=0x300;
        CHECK(s.add_trace_watch(s.pc));
        bool rejected=false;
        try{hg::run_iop(s,1);}catch(const hg::IopFault& e){
            rejected=std::string(e.what()).find("instruction history is disabled")!=std::string::npos;
        }
        CHECK(rejected==!HG_TEST_IOP_TRACE);
        if(HG_TEST_IOP_TRACE)CHECK(s.trace_watches[0].cursor==s.pc_trace_cursor);
        else CHECK(s.pc==0x9c610 && s.sifman_submit_trace_cursor==1 && s.pc_trace_cursor==0);
    }
    for(unsigned cursor=0;cursor<5;++cursor)for(unsigned mask=0;mask<32;++mask) {
        auto scheduled=std::make_unique<hg::IopState>();auto& s=*scheduled;
        s.threads.resize(5);s.thread_cursor=cursor;s.virtual_time_us=10;
        unsigned expected=0,priority=127;
        for(unsigned n=0;n<5;++n) {
            auto& t=s.threads[n];t.ready=(mask&(1u<<n))!=0;t.priority=10+n%3;
            t.delayed=n%2!=0;t.wake_deadline=n==1?10:11;
            t.initialized=true;t.context.pc=0x1000;t.context.interrupts_enabled=true;
        }
        for(unsigned offset=0;offset<5;++offset) {
            const auto n=(cursor+offset)%5;const auto& t=s.threads[n];
            if((t.ready||(t.delayed&&t.wake_deadline<=10)) && t.priority<priority){expected=n+1;priority=t.priority;}
        }
        unsigned selected=0;bool woke_before_run=false;
        const auto ran=hg::service_iop_thread(s,[&](hg::IopState& active,std::uint64_t){
            selected=active.current_thread;woke_before_run=!active.threads[1].delayed&&active.threads[1].ready&&active.threads[3].delayed;
        });
        CHECK(selected==expected && ran==(expected!=0) && woke_before_run);
    }
    {
        auto wait=std::make_unique<hg::IopState>();
        wait->threads.push_back({0x11000,0x1000,40,0,0});
        wait->current_thread=1;wait->pc=0x11000;wait->gpr[31]=0x90000;
        // A blocked native adapter must end the whole run budget, not just
        // its dispatch partition. The return PC deliberately has no code.
        const auto trace_before=wait->pc_trace_cursor;
        hg::run_iop(*wait,100);
        CHECK(wait->pc==0x11000 && wait->pc_trace_cursor==trace_before+HG_TEST_IOP_TRACE);
        CHECK(!wait->threads[0].ready && wait->threads[0].waiting_vblank_start);
        wait->signal_vblank_start();hg::run_iop(*wait,1);
        CHECK(wait->pc==0x90000 && wait->r(2)==0);
        wait->pc=0xe000;wait->gpr[31]=0x90000;
        hg::run_iop_burst(*wait,100);CHECK(wait->pc==0x90000);
        wait->pc=0x100c;wait->w(31,0x1f0000);hg::run_iop_burst(*wait,100);
        CHECK(wait->pc==0x1f0000 && wait->r(6)==9);
        wait->pc=0x100c;wait->w(31,0x3010);hg::run_iop_burst(*wait,2);
        CHECK(wait->pc==0x3014 && wait->r(3)==1);
    }
    {
        auto card=std::make_unique<hg::IopState>();
        card->store(0x3000,4,0x1181);card->store(0x3ffc,4,0x12345678);card->store(0x4090,4,0x12345678);
        card->store(0x1f808200,4,0x100472);
        CHECK(card->set_slice_dma(11,0x3000,36,1,1)==1);
        CHECK(card->set_slice_dma(12,0x4000,36,1,0)==1);
        CHECK(card->dmac.channel[4][2]==0x201 && card->dmac.channel[5][2]==0x800200);
        card->start_dma(11);card->start_dma(12);
        CHECK(!card->sio2_irq_pending && card->load(0x4000,4,false)==0);
        bool rejected=false;try{card->store(0x1f801540,4,0x5000);}catch(const hg::IopFault&){rejected=true;}
        CHECK(rejected && card->dmac.channel[4][0]==0x3000);
        card->store(0x1f808268,4,0x3bd);
        CHECK(card->sio2_irq_pending && card->load(0x1f80826c,4,false)==0x1d100);
        CHECK(card->dmac.channel[4][0]==0x3090 && card->dmac.channel[5][0]==0x4090);
        CHECK(card->dmac.channel[4][1]==36 && card->dmac.channel[5][1]==36);
        CHECK(!(card->dmac.channel[4][2]&0x1000000) && !(card->dmac.channel[5][2]&0x1000000));
        CHECK(card->load(0x4000,4,false)==0xffffffff && card->load(0x408c,4,false)==0xffffffff);
        CHECK(card->load(0x3ffc,4,false)==0x12345678 && card->load(0x4090,4,false)==0x12345678);
        card->sio2_irq_pending=false;card->store(0x1f808268,4,0x3bc);
        card->store(0x3000,4,0xf381);card->store(0x1f808200,4,0x140573);
        card->set_slice_dma(11,0x3000,36,1,1);card->set_slice_dma(12,0x4000,36,1,0);
        card->start_dma(11);card->start_dma(12);card->store(0x1f808268,4,0x3bd);
        CHECK(card->sio2_irq_pending && card->sio2_input_fifo.size()==5 && card->sio2_registers[27]==0x1d100);
        card->sio2_irq_pending=false;card->store(0x1f808268,4,0x3bc);
        card->set_slice_dma(11,0x3000,36,1,1);card->set_slice_dma(12,0x1ffff0,36,1,0);
        card->start_dma(11);card->start_dma(12);
        rejected=false;try{card->store(0x1f808268,4,0x3bd);}catch(const hg::IopFault&){rejected=true;}
        CHECK(rejected && !card->sio2_irq_pending && card->dmac.channel[4][0]==0x3000);
        card->store(0x1f808268,4,0x3bc);card->store(0x1f808200,4,0xc0342);
        for(const auto byte:{0x81,0x52,0})card->store(0x1f808260,1,byte);
        card->store(0x1f808268,4,0x3bd);
        CHECK(card->sio2_irq_pending && card->sio2_registers[27]==0x1d100);
        for(unsigned n=0;n<3;++n)CHECK(card->load(0x1f808264,1,false)==0xff);
    }
    {
        auto placement=std::make_unique<hg::IopState>();
        placement->cdvd_files={{"A.IRX",0,64},{"B.IRX",1,64}};
        placement->configure_static_iop_module("A.IRX",0x20000,0xff0,0x30);
        bool overlap=false;
        try{placement->configure_static_iop_module("B.IRX",0x21000,0x100,0x30);}
        catch(const std::runtime_error&){overlap=true;}
        CHECK(overlap && placement->static_iop_module_plans.size()==1);
        placement->configure_static_iop_module("B.IRX",0x21020,0x100,0x30);
        CHECK(placement->static_iop_module_plans.size()==2);
        overlap=false;
        try{placement->configure_buffered_iop_module("other",0x21120,0x100,0x30,{1});}
        catch(const std::runtime_error&){overlap=true;}
        CHECK(overlap && placement->static_iop_module_plans.size()==2);
    }
    {
        auto buffer=std::make_unique<hg::IopState>();
        buffer->configure_buffered_iop_module("synthetic",0xe0030,0x90,0x30,{1,2,3,4});
        buffer->store(0x4000,4,0x04030201);buffer->pending_iop_module_path="PREVIOUS.IRX";
        buffer->pc=0xe024;buffer->w(4,0x4000);buffer->w(29,0x9000);
        hg::run_iop(*buffer,1);
        CHECK(buffer->pc==0xe028 && buffer->r(29)==0x8fb0);
        CHECK(buffer->pending_iop_module_path=="@buffer/synthetic");
        CHECK(buffer->alloc_system_memory(0,0xc0,0)==0xe0000);
        CHECK(buffer->link_static_iop_module(0xe0030,0x70)==0);
        bool rejected=false;try{buffer->prepare_buffered_module(0x4000,0,0);}catch(const hg::IopFault&){rejected=true;}CHECK(rejected);
        CHECK(buffer->free_system_memory(0xe0000)==0);
        buffer->prepare_buffered_module(0x4000,0,0);
        CHECK(buffer->alloc_system_memory(0,0xc0,0)==0xe0000);
        buffer->free_system_memory(0xe0000);buffer->pending_iop_module_path="UNCHANGED";
        buffer->store(0x4003,1,5);rejected=false;
        try{buffer->prepare_buffered_module(0x4000,0,0);}catch(const hg::IopFault&){rejected=true;}CHECK(rejected);
        CHECK(buffer->pending_iop_module_path=="UNCHANGED");
        rejected=false;try{buffer->prepare_buffered_module(0x4000,1,0);}catch(const hg::IopFault&){rejected=true;}CHECK(rejected);
    }
    { // IOP MTC0 Status is retained; unmodeled CP0 registers still fault.
        auto cp0_storage=std::make_unique<hg::IopState>();auto& cp0=*cp0_storage; cp0.write_cop0(12,0x00400001);
        CHECK(cp0.read_cop0(12)==0x00400001);
        bool fault=false;try {cp0.write_cop0(15,0);}catch(const hg::IopFault&){fault=true;}
        CHECK(fault);
    }
    {
        auto sub_storage=std::make_unique<hg::IopState>();auto& sub=*sub_storage;hg::IopRebootRequest request;
        sub.in_interrupt=true;sub.w(4,0x1000);sub.w(31,0x1800);
        sub.store(0x1008,4,0x80000003);sub.store(0x1010,4,3);
        sub.store(0x1018,1,'a');sub.store(0x1019,1,0);sub.store(0x101a,1,'c');
        bool rejected=false;try{request.receive(sub);}catch(const hg::IopFault&){rejected=true;}
        CHECK(rejected && !request.pending && request.arguments.empty());
        sub.store(0x1019,1,'b');request.receive(sub);
        CHECK(request.pending && request.arguments=="abc" && request.mode==0 && sub.pc==0x1800);
    }
    auto s_storage=std::make_unique<hg::IopState>();auto& s=*s_storage;s.pc=0x1000;s.w(4,0x200);s.w(31,0x1800);s.store(0x200,4,5);
    hg::run_iop(s,4);
    CHECK(s.pc==0x1800 && s.r(5)==12 && s.r(6)==9 && !s.pending_reg);
    s.pc=0x2000;s.w(2,0x99);bool fault=false;
    try{hg::run_iop(s,2);}catch(const hg::IopFault& e){fault=e.pc==0x2004;}
    CHECK(fault && s.gpr[2]==0x99 && s.pending_reg==2);
    s.finish_instruction();CHECK(s.r(2)==5);
    auto cancel_storage=std::make_unique<hg::IopState>();auto& cancel=*cancel_storage;cancel.gpr[2]=0x11;cancel.load_register(2,0x22);cancel.finish_instruction();
    cancel.w(2,0x33);cancel.finish_instruction();CHECK(cancel.r(2)==0x33 && !cancel.pending_reg);
    cancel.load_register(2,0x44);cancel.finish_instruction();cancel.load_register(2,0x55);cancel.finish_instruction();
    CHECK(cancel.gpr[2]==0x33 && cancel.pending_reg==2);cancel.finish_instruction();CHECK(cancel.r(2)==0x55);
    // Host-only PC provenance must observe the committed register file without
    // turning a pending MIPS-I load into an artificial guest hazard.
    auto trace_storage=std::make_unique<hg::IopState>();auto& trace=*trace_storage;trace.pc=0x2100;trace.gpr[31]=0x2200;trace.gpr[4]=0x2300;trace.gpr[2]=0x2400;trace.gpr[19]=0x2600;trace.pending_reg=2;
    trace.trace_pc();CHECK(trace.pc_trace[0].pc==0x2100 && trace.pc_trace[0].return_pc==0x2200 &&
                           trace.pc_trace[0].a0==0x2300 && trace.pc_trace[0].v0==0x2400 &&
                           trace.pc_trace[0].s3==0x2600);
    CHECK(trace.add_trace_watch(0x2104) && trace.add_trace_watch(0x2108) && trace.add_trace_watch(0x2104));
    trace.pc=0x2104;trace.gpr[2]=0x2500;trace.trace_pc();
    CHECK(trace.trace_watches[0].cursor==2 && trace.trace_watches[0].history[1].pc==0x2104 && trace.trace_watches[0].history[1].v0==0x2500);
    trace.pc=0x2108;trace.trace_pc();CHECK(trace.trace_watches[1].cursor==3 && trace.trace_watches[1].history[2].pc==0x2108);
    for(std::size_t n=2;n<hg::IopState::max_trace_watches;++n)CHECK(trace.add_trace_watch(0x3000+std::uint32_t(n*4)));
    CHECK(trace.trace_watch_count==hg::IopState::max_trace_watches &&
          trace.add_trace_watch(0x2104) && !trace.add_trace_watch(0x5000));
    s.pc=0x3000;s.w(2,0x88);hg::run_iop(s,1);
    CHECK(s.pc==0x3010 && s.gpr[2]==0x88 && s.pending_reg==2);
    hg::run_iop(s,2);CHECK(s.r(5)==6);
    s.pc=0x4000;hg::run_iop(s,2);CHECK(s.pc==0x4010 && s.r(3)==5);
    s.pc=0x5000;s.w(4,0x6000);hg::run_iop(s,1);
    CHECK(s.pc==0x6000 && s.r(4)==0x5008 && s.r(3)==0x5008);
    s.pc=0x7000;fault=false;try{hg::run_iop(s,1);}catch(const hg::IopFault&){fault=true;}CHECK(fault);
    s.store(0xa0000200,4,0xffeeddcc);CHECK(s.load(0x200,1,true)==0xffffffcc);
    CHECK(s.load(0x80000202,2,false)==0xffee);
    CHECK(hg::iop_sra(0x80000000,31)==0xffffffff && hg::iop_sra(0x12345678,0)==0x12345678);
    fault=false;try{s.load(0x200000,4,false);}catch(const hg::IopFault&){fault=true;}CHECK(fault);
    s.pc=0x8000;s.w(4,0xfffffffb);s.w(5,3);hg::run_iop(s,1);
    CHECK(s.lo==0xfffffff1 && s.hi==0xffffffff);
    hg::run_iop(s,1);CHECK(s.lo==0xfffffff1 && s.hi==2);
    hg::run_iop(s,1);CHECK(s.lo==0xffffffff && s.hi==0xfffffffe);
    hg::run_iop(s,1);CHECK(s.lo==0x55555553 && s.hi==2);
    s.pc=0x9000;hg::run_iop(s,1);CHECK(s.pc==0x900c && s.lo==0xffffffff && s.hi==0xfffffffe);
    s.w(5,0);s.pc=0x9000;fault=false;
    try{hg::run_iop(s,1);}catch(const hg::IopFault& e){fault=e.pc==0x9004;}
    CHECK(fault && s.lo==0xffffffff && s.hi==0xfffffffe);
    s.pc=0x8008;s.w(4,0x80000000);s.w(5,0xffffffff);fault=false;
    try{hg::run_iop(s,1);}catch(const hg::IopFault&){fault=true;}CHECK(fault);
    s.pc=0xa000;s.w(4,0);s.w(5,0x7fffffff);hg::run_iop(s,1);CHECK(s.r(2)==0xffffffff);
    hg::run_iop(s,1);CHECK(s.r(2)==0x7fffffff);
    hg::run_iop(s,1);CHECK(s.r(2)==0x80000001);
    s.pc=0xa008;s.w(4,0x80000000);s.w(5,1);fault=false;
    try{hg::run_iop(s,1);}catch(const hg::IopFault&){fault=true;}CHECK(fault && s.r(2)==0x80000001);
    // Overflow is checked even when the destination is the zero register.
    s.pc=0xb000;s.w(4,0x7fffffff);s.w(5,1);fault=false;
    try{hg::run_iop(s,1);}catch(const hg::IopFault& e){fault=e.pc==0xb004;}CHECK(fault);
    s.pc=0xc000;fault=false;try{hg::run_iop(s,1);}catch(const hg::IopFault&){fault=true;}CHECK(fault);
    s.store(0xc000,4,0x08000400);s.store(0xc004,4,0x2400000c);hg::run_iop(s,1);CHECK(s.pc==0x1000);
    s.pc=0xc000;s.store(0xc004,4,0x2402000c);fault=false;
    try{hg::run_iop(s,1);}catch(const hg::IopFault&){fault=true;}CHECK(fault && s.pc==0xc000);
    s.pc=0xd000;s.w(2,0x55);hg::run_iop(s,1);CHECK(s.gpr[2]==0x55 && s.pending_reg==2);
    hg::run_iop(s,2);CHECK(s.r(3)==0x1f);
    CHECK(s.load(0xbf801450,4,false)==1 && s.load(0xbd000060,4,false)==0);
    s.store(0xbd000040,4,0x40);CHECK(s.load(0xbd000040,4,false)==0x40);
    s.store(0xbd000040,4,0x20);CHECK(s.load(0xbd000040,4,false)==0x20);
    fault=false;try{s.store(0xbd000040,4,0x60);}catch(const hg::IopFault&){fault=true;}CHECK(fault);
    fault=false;try{s.store(0xbf801450,4,0);}catch(const hg::IopFault&){fault=true;}CHECK(fault);
    s.w(31,0x1f0000);s.pc=0xe000;hg::run_iop(s,1);CHECK(s.pc==0x1f0000 && s.instruction_cache_epoch==1);
    s.pc=0xe004;hg::run_iop(s,1);CHECK(s.data_cache_epoch==1 && s.instruction_cache_epoch==1);
    s.interrupts_enabled=true;s.w(4,0x300);s.pc=0xe008;hg::run_iop(s,1);
    CHECK(!s.interrupts_enabled && s.load(0x300,4,false)==1 && s.r(2)==0);
    s.w(4,0x304);s.pc=0xe008;hg::run_iop(s,1);
    CHECK(!s.interrupts_enabled && s.load(0x304,4,false)==0 && s.r(2)==std::uint32_t(-102));
    s.w(4,0);s.pc=0xe00c;hg::run_iop(s,1);CHECK(!s.interrupts_enabled);
    s.w(4,1);s.pc=0xe00c;hg::run_iop(s,1);CHECK(s.interrupts_enabled);
    s.interrupt_mask=std::uint64_t(1)<<42;s.pc=0xe01c;hg::run_iop(s,1);
    CHECK(!s.interrupts_enabled && s.r(2)==0 && s.interrupt_mask==(std::uint64_t(1)<<42));
    s.interrupts_enabled=true;s.in_interrupt=true;s.pc=0xe01c;hg::run_iop(s,1);
    CHECK(!s.interrupts_enabled && s.r(2)==0);s.in_interrupt=false;
    s.w(4,42);s.w(5,1);s.w(6,0x1000);s.w(7,0x2500);s.pc=0xe010;hg::run_iop(s,1);
    CHECK(s.r(2)==0 && s.interrupt_handlers[42].callback==0x1000 && s.interrupt_handlers[42].argument==0x2500);
    s.pc=0xe010;hg::run_iop(s,1);CHECK(s.r(2)==std::uint32_t(-104));
    s.pc=0xe018;hg::run_iop(s,1);CHECK(s.interrupt_mask==(std::uint64_t(1)<<42));
    s.enable_interrupt(16);CHECK(s.disable_interrupt(16,0)==0 && !(s.interrupt_mask&(std::uint64_t(1)<<16)));
    s.pc=0xe014;hg::run_iop(s,1);CHECK(!s.interrupt_handlers[42].registered);
    s.pc=0xe014;hg::run_iop(s,1);CHECK(s.r(2)==std::uint32_t(-105));
    s.set_new_context_callback(0x1000);s.set_should_preempt_callback(0x2000);
    CHECK(s.new_context_callback==0x1000 && s.should_preempt_callback==0x2000);
    s.set_secrman_callback(true,0x3000);s.set_secrman_callback(false,0x4000);
    CHECK(s.secrman_mc_command_callback==0x3000 && s.secrman_mc_devid_callback==0x4000);
    s.set_secrman_callback(false,0);CHECK(s.secrman_mc_devid_callback==0);
    fault=false;try{s.set_secrman_callback(true,3);}catch(const hg::IopFault&){fault=true;}CHECK(fault);
    s.store(0xbf801570,4,0x8800);CHECK(s.load(0x1f801570,4,false)==0x8800);
    // DMACMAN priority/enable services change only the documented DPCR nibbles.
    s.set_dma_priority(11,5);CHECK(s.dmac.dpcr2==0x0058800);
    s.set_dma_channel_enabled(11,true);CHECK(s.dmac.dpcr2==0x00d8800);
    s.set_dma_channel_enabled(11,false);CHECK(s.dmac.dpcr2==0x0058800);
    s.set_dma_priority(6,7);CHECK((s.dmac.dpcr>>24)==7);
    fault=false;try{s.set_dma_priority(13,1);}catch(const hg::IopFault&){fault=true;}CHECK(fault);
    fault=false;try{s.set_dma_priority(11,8);}catch(const hg::IopFault&){fault=true;}CHECK(fault);
    s.store(0xbf8014a4,2,0);CHECK(s.load(0x1f8014a4,2,false)==0x400);
    s.store(0xbf8014a0,4,0x12345678);s.store(0xbf8014a8,4,0x87654321);
    CHECK(s.load(0x1f8014a0,4,false)==0x12345678 && s.load(0x1f8014a8,4,false)==0x87654321);
    s.timers[5].mode|=0x1800;CHECK(s.load(0x1f8014a4,2,false)==0x1c00 && s.timers[5].mode==0x400);
    s.store(0xbf8014a4,2,0x50);s.store(0xbf8014a8,4,36);s.advance_timers(1);
    CHECK(s.timers[5].count==36 && (s.timers[5].mode&0x800) && (s.timer_irq_pending&(1u<<5)));
    s.store(0xbf801534,4,0x12340000);s.store(0xbf801534,2,0x20);
    CHECK(s.load(0xbf801534,4,false)==0x12340020);
    fault=false;try{s.store(0xbf801538,4,0x41000200);}catch(const hg::IopFault&){fault=true;}
    CHECK(fault && s.load(0xbf801538,4,false)==0);
    CHECK(s.load(0xbd000020,4,false)==0);
    auto irq_storage=std::make_unique<hg::IopState>();auto& irq=*irq_storage;
    CHECK(!irq.wait_event_flag(1,0x100,0,0));
    CHECK(irq.set_event_flag(1,0x80)==0 && !irq.wait_event_flag(1,0x100,0,0));
    irq.set_event_flag(1,0x100);CHECK(irq.wait_event_flag(1,0x100,0,0x200));
    CHECK(irq.load(0x200,4,false)==0x180 && irq.system_event_bits==0x180);
    CHECK(irq.clear_event_flag(1,~0x80u)==0 && irq.system_event_bits==0x100);
    fault=false;irq.in_interrupt=true;try{irq.clear_event_flag(1,0);}catch(const hg::IopFault&){fault=true;}
    CHECK(fault);irq.in_interrupt=false;
    irq.store(0x300,4,0);irq.store(0x304,4,0);irq.store(0x308,4,1);irq.store(0x30c,4,1);
    const auto full_sema=irq.create_semaphore(0x300);
    CHECK(irq.signal_semaphore(full_sema)==std::uint32_t(-420) && irq.semaphore(full_sema).count==1);
    irq.register_interrupt(43,1,0x1000,0x3456);irq.enable_interrupt(0x22b);
    irq.pc=0x2000;irq.gpr.fill(0x1234);irq.hi=5;irq.lo=6;
    irq.pending_reg=2;irq.pending_value=0xabcdef;irq.dmac.completed_channels=1u<<10;
    auto callback=[](hg::IopState& x,std::uint64_t) {
        if(x.pc!=0x1000 || x.r(4)!=0x3456 || x.interrupts_enabled)throw std::runtime_error("bad IRQ entry context");
        auto target=x.r(31);x.gpr.fill(0xaaaa);x.w(2,1);x.pc=target;x.hi=x.lo=0;
        x.set_event_flag(1,0x400);
    };
    CHECK(!hg::service_iop_dma_interrupt(irq,callback));
    irq.interrupts_enabled=true;CHECK(hg::service_iop_dma_interrupt(irq,callback));
    CHECK(irq.pc==0x2000 && irq.gpr[2]==0x1234 && irq.hi==5 && irq.lo==6);
    CHECK(irq.pending_reg==2 && irq.pending_value==0xabcdef && irq.interrupts_enabled);
    CHECK(irq.system_event_bits==0x500 && irq.dmac.completed_channels==0);
    CHECK(!hg::service_iop_dma_interrupt(irq,callback));
    // The two original LIBSD registrations route SPU2 DMA channel 4 to IRQ36
    // and channel 7 to IRQ40.  Exercise the actual synchronous SPU2 DMA path,
    // then verify that cooperative IRQ delivery runs the registered callback.
    auto spu_irq_storage=std::make_unique<hg::IopState>();auto& spu_irq=*spu_irq_storage;spu_irq.interrupts_enabled=true;
    CHECK(spu_irq.register_interrupt(36,1,0x1000,0x36)==0);
    CHECK(spu_irq.register_interrupt(40,1,0x1000,0x40)==0);
    CHECK(spu_irq.enable_interrupt(36)==0 && spu_irq.enable_interrupt(40)==0);
    for(unsigned n=0;n<64;++n)spu_irq.store(0x1200+n,1,n+1);
    spu_irq.store(0x1f9001a8,2,0);spu_irq.store(0x1f9001aa,2,8);
    spu_irq.store(0x1f8010c0,4,0x1200);spu_irq.store(0x1f8010c4,4,0x00020004);
    spu_irq.store(0x1f8010c8,4,0x01000201);
    unsigned spu_irq_seen=0;
    auto spu_callback=[&](hg::IopState& x,std::uint64_t) {
        spu_irq_seen=std::uint32_t(x.r(4));x.set_event_flag(1,spu_irq_seen==0x36?1u:2u);
        x.w(2,1);x.pc=x.r(31);
    };
    CHECK((spu_irq.dmac.completed_channels&(1u<<4)) && hg::service_iop_dma_interrupt(spu_irq,spu_callback));
    CHECK(spu_irq_seen==0x36 && spu_irq.system_event_bits==1 && !(spu_irq.dmac.completed_channels&(1u<<4)));
    spu_irq.store(0x1f9005a8,2,0);spu_irq.store(0x1f9005aa,2,0x10);
    spu_irq.store(0x1f801500,4,0x1220);spu_irq.store(0x1f801504,4,0x00020004);
    spu_irq.store(0x1f801508,4,0x01000201);
    CHECK((spu_irq.dmac.completed_channels&(1u<<7)) && hg::service_iop_dma_interrupt(spu_irq,spu_callback));
    CHECK(spu_irq_seen==0x40 && spu_irq.system_event_bits==3 && !(spu_irq.dmac.completed_channels&(1u<<7)));
    // The one-shot DMA descriptor above is too small for an AutoDMA stereo
    // half. Reject it before publishing bytes, register completion, or an
    // interrupt; the unrelated core's control does not select this mode.
    for(unsigned core=0;core<2;++core) {
        const auto dma=core?0x1f801500u:0x1f8010c0u;
        const auto control=0x1f9001b0u+core*0x400u;
        spu_irq.store(dma,4,0x1200);spu_irq.store(dma+4,4,0x00020004);
        spu_irq.store(control,2,1u<<core);
        const auto before_ram=spu_irq.spu2_ram;
        const auto before_count=spu_irq.spu2_dma_count;
        const auto before_bytes=spu_irq.spu2_dma_bytes;
        const auto before_irq=spu_irq.dmac.completed_channels;
        const auto before_chcr=spu_irq.dmac.read(dma+8,4);
        bool rejected=false;
        try {spu_irq.store(dma+8,4,0x01000201);}catch(const hg::IopFault& e) {
            rejected=std::string(e.what()).find("SPU2 AutoDMA")!=std::string::npos;
        }
        CHECK(rejected && spu_irq.spu2_ram==before_ram);
        CHECK(spu_irq.spu2_dma_count==before_count && spu_irq.spu2_dma_bytes==before_bytes);
        CHECK(spu_irq.dmac.completed_channels==before_irq && spu_irq.dmac.read(dma+8,4)==before_chcr);
        CHECK(spu_irq.dmac.read(dma,4)==0x1200 && spu_irq.dmac.read(dma+4,4)==0x00020004);
        spu_irq.store(control,2,0);
        const auto other_control=0x1f9001b0u+(1-core)*0x400u;
        spu_irq.store(other_control,2,1u<<(1-core));
        spu_irq.store(dma+8,4,0x01000201);
        CHECK(spu_irq.spu2_dma_count==before_count+1);
        spu_irq.store(other_control,2,0);
    }
    auto cdvd_irq_storage=std::make_unique<hg::IopState>();auto& cdvd_irq=*cdvd_irq_storage;cdvd_irq.interrupts_enabled=true;cdvd_irq.register_interrupt(2,1,0x1000,0x55);cdvd_irq.enable_interrupt(2);cdvd_irq.cdvd_irq_pending=true;
    auto cdvd_callback=[](hg::IopState& x,std::uint64_t) {if(x.pc!=0x1000 || x.r(4)!=0x55)throw std::runtime_error("bad CDVD IRQ context");x.w(2,1);x.pc=x.r(31);};
    CHECK(hg::service_iop_cdvd_interrupt(cdvd_irq,cdvd_callback) && !cdvd_irq.cdvd_irq_pending && cdvd_irq.interrupts_enabled);
    auto timer_irq_storage=std::make_unique<hg::IopState>();auto& timer_irq=*timer_irq_storage;timer_irq.interrupts_enabled=true;timer_irq.register_interrupt(16,1,0x1000,0x77);timer_irq.enable_interrupt(16);timer_irq.timer_irq_pending=1u<<5;
    auto timer_callback=[](hg::IopState& x,std::uint64_t) {if(x.pc!=0x1000 || x.r(4)!=0x77)throw std::runtime_error("bad timer IRQ context");x.w(2,1);x.pc=x.r(31);};
    CHECK(hg::service_iop_timer_interrupt(timer_irq,timer_callback) && !timer_irq.timer_irq_pending && timer_irq.interrupts_enabled);
    auto task_storage=std::make_unique<hg::IopState>();auto& task=*task_storage;task.interrupts_enabled=true;task.pc=0x4000;
    task.store(0xbf801404,4,0xbf900000);task.store(0xbf80140c,4,0xbf900800);
    CHECK(task.load(0xbf801404,4,false)==0xbf900000 && task.load(0xbf80140c,4,false)==0xbf900800);
    task.store(0xbf801014,4,0x200b31e1);task.store(0xbf801414,4,0x200b31e1);
    CHECK(task.load(0xbf801014,4,false)==0x200b31e1 && task.load(0xbf801414,4,false)==0x200b31e1);
    task.store(0xbf9007c0,2,0);task.store(0xbf9007c6,2,0x900);task.store(0xbf9007c8,2,0x200);task.store(0xbf9007ca,2,8);
    CHECK(task.load(0xbf9007c0,2,false)==0 && task.load(0xbf9007c6,2,false)==0x900 && task.load(0xbf9007c8,2,false)==0x200 && task.load(0xbf9007ca,2,false)==8);
    task.store(0x1000,4,0x02000000);task.store(0x1008,4,0x5000);
    task.store(0x100c,4,0x1000);task.store(0x1010,4,96);
    CHECK(task.create_thread(0x1000)==1);CHECK(task.create_thread(0x1000)==2);
    CHECK(task.threads[0].stack_base==0x17e000 && task.threads[1].stack_base==0x17d000 && task.thread_stack_top==0x17d000);
    task.start_thread(1,11);task.start_thread(2,22);
    task.current_thread=1;task.locked_thread=1;task.pc=0xa465c;
    task.threadman_reschedule(7);
    CHECK(task.thread_cursor==1 && task.locked_thread==0 && task.last_thread_reschedule_pc==0xa465c);
    CHECK(task.last_thread_reschedule_mode==1 && task.thread_reschedule_count==1);
    task.current_thread=0;task.thread_cursor=0;task.pc=0x4000;
    task.in_interrupt=true;CHECK(task.get_thread_id()==std::uint32_t(-100));
    task.current_thread=1;CHECK(task.get_thread_id()==std::uint32_t(-100));
    task.in_interrupt=false;CHECK(task.get_thread_id()==1);task.current_thread=0;
    CHECK(task.change_thread_priority(0,126)==0 && task.bootstrap_thread_priority==126);
    unsigned seen=0;
    auto worker=[&](hg::IopState& x,std::uint64_t){
        seen=x.get_thread_id();x.w(8,x.r(4));x.pc+=4;
        if(seen==1 && x.pc==0x5004)x.interrupts_enabled=false;
        else {x.interrupts_enabled=true;x.sleep_thread();}
    };
    CHECK(hg::service_iop_thread(task,worker) && seen==1 && task.locked_thread==1);
    CHECK(task.pc==0x4000 && task.gpr[8]==0 && task.threads[0].context.gpr[8]==11);
    CHECK(hg::service_iop_thread(task,worker) && seen==1 && task.locked_thread==0);
    CHECK(hg::service_iop_thread(task,worker) && seen==2);
    CHECK(!hg::service_iop_thread(task,worker));
    auto finished_storage=std::make_unique<hg::IopState>();auto& finished=*finished_storage;finished.threads.push_back({0x5000,0x1000,80,0x02000000,0,0,true});
    finished.threads[0].initialized=true;finished.threads[0].context.pc=0x1f0020;
    CHECK(!hg::service_iop_thread(finished,[](hg::IopState&,std::uint64_t){throw std::runtime_error("completed worker dispatched");}) && !finished.threads[0].ready);
    finished.threads[0].stack_base=0x1fe000;finished.thread_stack_top=0x1fe000;
    CHECK(finished.start_thread(1,0x1234)==0);
    bool restarted=false;
    CHECK(hg::service_iop_thread(finished,[&](hg::IopState& x,std::uint64_t){
        restarted=x.pc==0x5000 && x.r(4)==0x1234 && x.r(29)==0x1feff0;x.pc=0x1f0020;
    }));
    CHECK(restarted && finished.threads[0].stack_base==0x1fe000 && finished.thread_stack_top==0x1fe000);
    CHECK(finished.start_thread(1,0x5678)==0);
    unsigned exit_steps=0;
    CHECK(hg::service_iop_thread(finished,[&](hg::IopState& x,std::uint64_t){
        ++exit_steps;x.pc=0xe020;hg::run_iop(x,1);
    },8));
    CHECK(exit_steps==1 && !finished.threads[0].ready && finished.threads[0].context.pc==0x1f0020);
    CHECK(finished.threads[0].live && finished.threads[0].stack_base==0x1fe000);
    bool exit_rejected=false;try{finished.exit_thread();}catch(const hg::IopFault&){exit_rejected=true;}CHECK(exit_rejected);
    CHECK(finished.delete_thread(1)==0 && !finished.threads[0].live);
    CHECK(finished.free_thread_stacks.size()==1 && finished.free_thread_stacks[0].address==0x1fe000 && finished.free_thread_stacks[0].size==0x1000);
    finished.store(0x1000,4,0x02000000);finished.store(0x1008,4,0x6000);
    finished.store(0x100c,4,0x800);finished.store(0x1010,4,80);
    CHECK(finished.create_thread(0x1000)==1 && finished.start_thread(1,0x55)==0);
    CHECK(hg::service_iop_thread(finished,[&](hg::IopState& x,std::uint64_t){
        if(x.pc!=0x6000 || x.r(4)!=0x55 || x.r(29)!=0x1fe7f0)throw std::runtime_error("bad reused worker stack");x.pc=0x1f0020;
    }));
    CHECK(finished.threads[0].stack_base==0x1fe000 && finished.free_thread_stacks.size()==1);
    task.threads[0].ready=true;task.interrupts_enabled=false;task.locked_thread=0;
    CHECK(hg::service_iop_thread(task,worker) && seen==1 && task.threads[0].sleeping);
    task.wakeup_thread(1);CHECK(hg::service_iop_thread(task,worker) && seen==1);
    auto clock_storage=std::make_unique<hg::IopState>();auto& clock=*clock_storage;clock.rtc_configured=true;clock.rtc_bcd={0,0x58,0x59,0x23,0,0x31,0x12,0x26};
    auto heap_storage=std::make_unique<hg::IopState>();auto& heap=*heap_storage;heap.pc=0x1234;const auto heap_id=heap.create_heap(0x800,1);
    CHECK(heap_id==0x180000 && heap.heap_total_free_size(heap_id)==0x7f0);
    const auto first=heap.alloc_heap_memory(heap_id,5),second=heap.alloc_heap_memory(heap_id,8);
    CHECK(first==0x180010 && second==0x180018 && heap.heap_total_free_size(heap_id)==0x7e0);
    CHECK(heap.free_heap_memory(heap_id,first)==0 && heap.free_heap_memory(heap_id,first)==std::uint32_t(-1));
    heap.delete_heap(heap_id);fault=false;try{heap.heap_total_free_size(heap_id);}catch(const hg::IopFault&){fault=true;}CHECK(fault);
    CHECK(clock.load(0xbf40200f,1,false)==0x14);
    CHECK(clock.cdvd_mmio_trace_cursor==1 && !clock.cdvd_mmio_trace[0].write && clock.cdvd_mmio_trace[0].address==0x1f40200f);
    CHECK(clock.load(0xbf40200f,1,false)==0x14 && clock.cdvd_mmio_trace_cursor==1 && clock.cdvd_mmio_trace[0].count==2);
    CHECK(clock.load(0xbf402013,1,false)==0 && clock.cdvd_mmio_trace_cursor==2);
    CHECK(clock.cdvd_mmio_trace[1].address==0x1f402013 && !clock.cdvd_mmio_trace[1].write);
    clock.store(0xbf402006,1,0x7e);CHECK(clock.load(0xbf402006,1,false)==0x7e);
    CHECK(clock.cdvd_mmio_trace[2].write && clock.cdvd_mmio_trace[2].value==0x7e);
    CHECK(clock.load(0xbf402017,1,false)==0x40);
    clock.store(0xbf402016,1,8);CHECK(clock.load(0xbf402017,1,false)==0);
    for(auto byte:clock.rtc_bcd)CHECK(clock.load(0xbf402018,1,false)==byte);
    CHECK(clock.load(0xbf402017,1,false)==0x40 && clock.load(0xbf402018,1,false)==0x26);
    clock.cdvd_interrupt_status=5;clock.store(0xbf402008,1,1);CHECK(clock.load(0xbf402008,1,false)==4);
    hg::IopDmac cdvd_dma;cdvd_dma.write(0x1f8010b0,4,0x1f000);cdvd_dma.write(0x1f8010b4,4,0x20);
    cdvd_dma.write(0x1f8010b8,4,0x41000200);CHECK(cdvd_dma.read(0x1f8010b8,4)==0x41000200);
    bool cdvd_active_fault=false;try {cdvd_dma.write(0x1f8010b0,4,0x1f100);} catch(const std::runtime_error&) {cdvd_active_fault=true;}CHECK(cdvd_active_fault);
    auto dvd_storage=std::make_unique<hg::IopState>();auto& dvd=*dvd_storage;dvd.pc=0x4200;
    dvd.cdvd_error=0x84;
    CHECK(dvd.load(0xbf40200a,1,false)==0x0a && dvd.load(0xbf40200b,1,false)==0x0a);
    dvd.store(0xbf402005,1,0);dvd.store(0xbf402004,1,9);
    CHECK(dvd.cdvd_read_pending && dvd.cdvd_transfer_kind==hg::IopState::CdvdTransferKind::toc && dvd.cdvd_drive_status==0x06);
    dvd.store(0xbf8010b0,4,0x1000);dvd.store(0xbf8010b4,4,0x00810004);dvd.store(0xbf8010b8,4,0x41000200);
    CHECK(dvd.cdvd_dma_ready());const auto toc_request=dvd.cdvd_dma_request();CHECK(toc_request.destination==0x1000 && toc_request.kind==hg::IopState::CdvdTransferKind::toc);
    auto bad_toc_storage=std::make_unique<hg::IopState>();auto& bad_toc=*bad_toc_storage;bad_toc.pc=0x4200;bad_toc.store(0xbf402005,1,0);bad_toc.store(0xbf402004,1,9);
    bad_toc.store(0xbf8010b0,4,0x1000);bad_toc.store(0xbf8010b4,4,0x10);bad_toc.store(0xbf8010b8,4,0x41000200);
    bool bad_toc_fault=false;try {bad_toc.cdvd_dma_request();} catch(const hg::IopFault&) {bad_toc_fault=true;}CHECK(bad_toc_fault);
    auto read_dvd_storage=std::make_unique<hg::IopState>();auto& read_dvd=*read_dvd_storage;read_dvd.pc=0x4200;
    read_dvd.cdvd_error=0x7e;
    for(auto byte:std::array<std::uint8_t,11>{1,0,0,0,1,0,0,0,16,2,0})read_dvd.store(0xbf402005,1,byte);
    read_dvd.store(0xbf402004,1,8);read_dvd.store(0xbf8010b0,4,0x1000);read_dvd.store(0xbf8010b4,4,0x00810004);read_dvd.store(0xbf8010b8,4,0x41000200);
    CHECK(read_dvd.cdvd_dma_ready());const auto request=read_dvd.cdvd_dma_request();CHECK(request.lba==1 && request.destination==0x1000);
    // Equivalent 43 blocks of 12 words, as built by the original DVD path.
    read_dvd.dmac.channel[3][1]=0x002b000c;
    CHECK(read_dvd.cdvd_dma_request().destination==0x1000);
    read_dvd.dmac.channel[3][1]=0x002a000c;
    bool short_dvd=false;try{read_dvd.cdvd_dma_request();}catch(const hg::IopFault&){short_dvd=true;}
    CHECK(short_dvd);read_dvd.dmac.channel[3][1]=0x002b000c;
    CHECK(read_dvd.register_interrupt(35,1,0x1000,0x456)==0);
    CHECK(read_dvd.enable_interrupt(35)==0);
    std::array<std::uint8_t,2064> record{};record[0]=0x20;record[12]=0x5a;read_dvd.complete_cdvd_dma(record);
    CHECK(!read_dvd.cdvd_dma_ready() && read_dvd.load(0x1000,1,false)==0x20 && read_dvd.load(0x100c,1,false)==0x5a && read_dvd.cdvd_interrupt_status==3 && read_dvd.cdvd_irq_pending);
    CHECK(read_dvd.load(0xbf402006,1,false)==0);
    CHECK(read_dvd.dmac.completed_channels&(1u<<3));
    read_dvd.interrupts_enabled=false;
    unsigned dvd_interrupts=0;
    auto dvd_callback=[&](hg::IopState& x,std::uint64_t) {
        ++dvd_interrupts;
        if(x.r(4)!=0x456 || x.load(0x100c,1,false)!=0x5a)throw hg::IopFault(x.pc,"DVD IRQ precedes payload");
        x.w(2,1);x.pc=x.r(31);
    };
    CHECK(!hg::service_iop_dma_interrupt(read_dvd,dvd_callback));
    read_dvd.interrupts_enabled=true;
    CHECK(hg::service_iop_dma_interrupt(read_dvd,dvd_callback) && dvd_interrupts==1);
    CHECK(!hg::service_iop_dma_interrupt(read_dvd,dvd_callback));
    for(const auto packet:std::vector<std::vector<std::uint8_t>>{
        {1,0,0,0,1,0,0,0}, {1,0,0,0,1,0,0,0,16,3,0},
        {1,0,0,0,1,0,0,0,16,2,1}, {1,0,0,0,1,0,0,0,1,2,0}}) {
        read_dvd.cdvd_n_parameters=packet;
        bool rejected=false;try{read_dvd.submit_cdvd_n_command(8);}catch(const hg::IopFault&){rejected=true;}
        CHECK(rejected && !read_dvd.cdvd_read_pending && read_dvd.cdvd_n_parameters==packet);
    }
    // Archive reads use retry0 but retain the real DMA payload and completion path.
    read_dvd.cdvd_n_parameters={2,0,0,0,1,0,0,0,0,2,0};
    read_dvd.submit_cdvd_n_command(8);
    read_dvd.store(0xbf8010b0,4,0x2000);
    read_dvd.store(0xbf8010b4,4,0x002b000c);
    read_dvd.store(0xbf8010b8,4,0x41000200);
    CHECK(read_dvd.cdvd_dma_request().lba==2);
    record[12]=0xa7;read_dvd.complete_cdvd_dma(record);
    CHECK(!read_dvd.cdvd_dma_ready() && read_dvd.load(0x200c,1,false)==0xa7);
    CHECK(read_dvd.cdvd_irq_pending && read_dvd.cdvd_error==0);
    // The real single-layer TOC path selects x4 CAV (0x80 | speed 3).
    read_dvd.cdvd_irq_pending=false;
    read_dvd.cdvd_n_parameters={16,0,0,0,1,0,0,0,16,0x83,0};
    read_dvd.submit_cdvd_n_command(8);
    CHECK(read_dvd.cdvd_lba==16 && read_dvd.cdvd_sectors==1 && read_dvd.cdvd_read_pending);
    CHECK(test_multi_record_dvd()==0);
    std::array<std::uint8_t,2064> toc_record{};toc_record[3]=0xf2;toc_record[1023]=0x5a;dvd.complete_cdvd_toc_dma(toc_record);
    CHECK(!(dvd.dmac.completed_channels&(1u<<3)));
    dvd.enable_interrupt(35);
    CHECK(!(dvd.dmac.completed_channels&(1u<<3)));
    CHECK(!dvd.cdvd_dma_ready() && dvd.load(0x1003,1,false)==0xf2 && dvd.load(0x13ff,1,false)==0x5a && dvd.cdvd_interrupt_status==3 && dvd.cdvd_irq_pending);
    CHECK(dvd.load(0xbf402006,1,false)==0);
    for(unsigned offset=0;offset<4;++offset) {
        auto unaligned_storage=std::make_unique<hg::IopState>();auto& unaligned=*unaligned_storage;unaligned.pc=0xf000;unaligned.w(4,0x100+offset);unaligned.w(5,0x200+offset);
        for(unsigned n=0;n<8;++n){unaligned.store(0x100+n,1,n+1);unaligned.store(0x200+n,1,0xaa);}
        hg::run_iop(unaligned,6);
        const auto expected=(offset+1)|((offset+2)<<8)|((offset+3)<<16)|((offset+4)<<24);
        CHECK(unaligned.r(8)==expected);
        for(unsigned n=0;n<8;++n)CHECK(unaligned.load(0x200+n,1,false)==(n>=offset && n<offset+4?n+1:0xaa));
    }
    {
        auto state=std::make_unique<hg::IopState>();auto& e=*state;
        e.store(0x100,4,2);e.store(0x104,4,0x1234);e.store(0x108,4,8);
        const auto id=e.create_event_flag(0x100);
        e.threads.push_back({0x5000,0x1000,80,0,0});e.current_thread=1;
        CHECK(!e.wait_event_flag(id,0x20,0,0));
        e.set_event_flag(id,0x20);
        CHECK(e.refer_event_status(id,0x80000200)==0);
        CHECK(e.load(0x200,4,false)==2 && e.load(0x204,4,false)==0x1234);
        CHECK(e.load(0x208,4,false)==8 && e.load(0x20c,4,false)==0x28);
        CHECK(e.load(0x210,4,false)==0); // Signaling removes the satisfied waiter immediately.
        CHECK(e.wait_event_flag(id,0x20,0,0));e.in_interrupt=true;
        CHECK(e.irefer_event_status(id,0x200)==0 && e.load(0x210,4,false)==0);
        bool rejected=false;try{e.refer_event_status(id,0x200);}catch(const hg::IopFault&){rejected=true;}
        CHECK(rejected);e.store(0x1ffffc,4,0xabcdef);
        rejected=false;try{e.irefer_event_status(id,0x1ffffc);}catch(const hg::IopFault&){rejected=true;}
        CHECK(rejected && e.load(0x1ffffc,4,false)==0xabcdef);
        rejected=false;try{e.irefer_event_status(999,0x200);}catch(const hg::IopFault&){rejected=true;}
        CHECK(rejected);
    }
    auto events_storage=std::make_unique<hg::IopState>();auto& events=*events_storage;events.interrupts_enabled=true;
    events.threads.push_back({0x5000,0x1000,80,0x02000000,0,0,true});
    events.threads.push_back({0x6000,0x1000,81,0x02000000,0,0,true});
    bool high_resumed=false;
    auto event_worker=[&](hg::IopState& x,std::uint64_t){
        if(x.current_thread==1){if(x.wait_event_flag(1,3,0,0)){high_resumed=true;x.sleep_thread();}}
        else {x.set_event_flag(1,3);x.sleep_thread();}
    };
    CHECK(hg::service_iop_thread(events,event_worker) && !events.threads[0].ready);
    CHECK(hg::service_iop_thread(events,event_worker) && events.threads[0].ready);
    CHECK(hg::service_iop_thread(events,event_worker) && high_resumed && !events.threads[0].wait_event);
    events.set_event_flag(1,0x80);events.current_thread=1;
    CHECK(events.wait_event_flag(1,0x84,0x11,0x300));
    CHECK(events.load(0x300,4,false)==0x83 && events.system_event_bits==0);
    events.current_thread=0;
    // A granted wait owns its result immediately. Later flag changes cannot
    // revoke the wakeup or consume a second completion before resumption.
    events.current_thread=1;CHECK(!events.wait_event_flag(1,0x20,0x10,0x304));
    events.current_thread=0;events.set_event_flag(1,0x20);
    CHECK(events.system_event_bits==0 && events.load(0x304,4,false)==0x20);
    events.set_event_flag(1,0x40);
    events.current_thread=1;CHECK(events.wait_event_flag(1,0x20,0x10,0x304));
    CHECK(events.system_event_bits==0x40 && events.load(0x304,4,false)==0x20);
    events.current_thread=0;
    // A bounded CPU burst must let a lower-priority worker run after the
    // high-priority worker reaches a real wait, before any external producer
    // supplies the event that wakes it again.
    auto burst_storage=std::make_unique<hg::IopState>();auto& burst=*burst_storage;burst.interrupts_enabled=true;
    burst.threads.push_back({0x7000,0x1000,10,0x02000000,0,0,true});
    burst.threads.push_back({0x8000,0x1000,20,0x02000000,0,0,true});
    unsigned high_steps=0;bool low_ran=false;
    auto burst_worker=[&](hg::IopState& x,std::uint64_t){
        if(x.current_thread==1) {
            if(++high_steps<3)x.pc+=4;
            else x.wait_event_flag(1,1,0,0);
        } else {low_ran=true;x.sleep_thread();}
    };
    CHECK(hg::service_iop_threads(burst,burst_worker,4)==4);
    CHECK(high_steps==3 && !burst.threads[0].ready && burst.threads[0].wait_event==1 && low_ran);
    burst.set_event_flag(1,1);CHECK(burst.threads[0].ready);

    // A multi-instruction quantum keeps one selected context loaded, but must
    // stop at the exact instruction where that owner blocks and let the next
    // dispatch select a lower-priority ready thread.
    auto quantum_storage=std::make_unique<hg::IopState>();auto& quantum=*quantum_storage;quantum.interrupts_enabled=true;
    quantum.threads.push_back({0x7000,0x1000,20,0x02000000,0,0,true});
    quantum.threads.push_back({0x8000,0x1000,80,0x02000000,0,0,true});
    unsigned quantum_high=0,quantum_low=0;
    auto quantum_worker=[&](hg::IopState& x,std::uint64_t){
        if(x.current_thread==1) {
            ++quantum_high;
            if(quantum_high==3)x.wait_event_flag(1,1,0,0);else x.pc+=4;
        } else {++quantum_low;x.sleep_thread();}
    };
    CHECK(hg::service_iop_threads(quantum,quantum_worker,2,64)==2);
    CHECK(quantum_high==3 && quantum_low==1 && !quantum.threads[0].ready && quantum.threads[0].wait_event==1);
    // Reject unsupported commands before mutating the queued packet.
    auto sio_storage=std::make_unique<hg::IopState>();auto& sio=*sio_storage;
    sio.store(0x1f808200,4,0x1234);sio.store(0x1f808260,1,0x42);
    bool sio_fault=false;
    try{sio.store(0x1f808268,4,0x3bd);}catch(const hg::IopFault&){sio_fault=true;}
    CHECK(sio_fault && sio.sio2_input_fifo.size()==1 && sio.sio2_input_fifo[0]==0x42);
    CHECK(sio.load(0x1f808200,4,false)==0x1234 && sio.load(0x1f808268,4,false)==0);
    CHECK(sio.sio2_last_control_write==0x3bd);
    {
        auto pad_storage=std::make_unique<hg::IopState>();auto& pad=*pad_storage;pad.store(0x1f808200,4,0x00140540);
        for(auto b:{1,0x42,0,0,0})pad.store(0x1f808260,1,b);
        pad.sio2_buttons[0]=0xbff7; // Cross and Start pressed.
        pad.store(0x1f808268,4,0x3bd);
        CHECK(pad.sio2_irq_pending && pad.load(0x1f80826c,4,false)==0x1100);
        CHECK(pad.load(0x1f808268,4,false)==0x3bc);
        CHECK(!hg::service_iop_sio2_interrupt(pad,[](auto&,auto){}));
        CHECK(pad.sio2_irq_pending);
        pad.interrupts_enabled=true;pad.interrupt_mask=std::uint64_t(1)<<17;
        pad.interrupt_handlers[17]={true,1,0x1200,0x1234};
        pad.pc=0x4560;pad.w(4,0x99);
        CHECK(hg::service_iop_sio2_interrupt(pad,[](auto& cpu,auto){
            if(cpu.pc!=0x1200 || cpu.r(4)!=0x1234)throw hg::IopFault(cpu.pc,"bad SIO2 callback");
            cpu.w(2,1);cpu.pc=cpu.r(31);
        }));
        CHECK(!pad.sio2_irq_pending && pad.pc==0x4560 && pad.r(4)==0x99);
        for(auto b:{0xff,0x41,0x5a,0xf7,0xbf})CHECK(pad.load(0x1f808264,1,false)==unsigned(b));
        bool empty=false;try{pad.load(0x1f808264,1,false);}catch(const hg::IopFault&){empty=true;}CHECK(empty);
    }
    {
        auto video_storage=std::make_unique<hg::IopState>();auto& video=*video_storage;
        video.threads.push_back({0x1000,0x1000,40,0,0});
        video.threads.push_back({0x2000,0x1000,41,0,0});
        video.signal_vblank_start(); // An old edge must not satisfy a new wait.
        video.current_thread=1;CHECK(!video.wait_vblank_start());
        video.current_thread=2;CHECK(!video.wait_vblank_start());
        CHECK(!video.threads[0].ready && !video.threads[1].ready);
        video.virtual_time_us=1000000; // Time alone cannot manufacture an edge.
        CHECK(!video.wait_vblank_start());
        video.signal_vblank_start();
        CHECK(video.threads[0].ready && video.threads[1].ready);
        CHECK(video.wait_vblank_start());CHECK(!video.wait_vblank_start());
        video.current_thread=1;CHECK(video.wait_vblank_start());
        CHECK(!video.wait_vblank_start());
        CHECK(video.vblank_start_count==2);
        video.in_interrupt=true;
        bool rejected=false;try{video.wait_vblank_start();}catch(const hg::IopFault&){rejected=true;}
        CHECK(rejected);
    }
    {
        auto ports_storage=std::make_unique<hg::IopState>();
        auto& ports=*ports_storage;
        auto poll=[&](unsigned port) {
            ports.sio2_irq_pending=false;
            ports.store(0x1f808268,4,0x3bc);
            ports.store(0x1f808200,4,0x00140540|port);
            for(auto b:{1,0x42,0,0,0})ports.store(0x1f808260,1,b);
            ports.store(0x1f808268,4,0x3bd);
        };
        poll(1);CHECK(ports.sio2_irq_pending && ports.load(0x1f80826c,4,false)==0x1d100);
        for(unsigned n=0;n<5;++n)CHECK(ports.load(0x1f808264,1,false)==0xff);
        ports.sio2_connected[1]=true;ports.sio2_buttons[1]=0xffef;
        poll(1);CHECK(ports.load(0x1f80826c,4,false)==0x1100);
        CHECK(ports.sio2_output_fifo==std::vector<std::uint8_t>({0xff,0x41,0x5a,0xef,0xff}));
        poll(0);CHECK(ports.sio2_output_fifo==std::vector<std::uint8_t>({0xff,0x41,0x5a,0xff,0xff}));
        bool bad_port=false;try{poll(2);}catch(const hg::IopFault&){bad_port=true;}
        CHECK(bad_port && !ports.sio2_irq_pending && ports.sio2_output_fifo.empty());
    }
    {
        auto shared=std::make_unique<hg::IopState>();
        std::uint32_t next=0x123000,high=0x1e0000;unsigned allocations=0,releases=0;
        bool service_arguments_valid=true;
        shared->system_memory_service=[&](hg::IopState&,unsigned ordinal,std::uint32_t a0,std::uint32_t a1,std::uint32_t a2) {
            service_arguments_valid&=a2==0;
            if(ordinal==5){++releases;return std::uint32_t(0);}
            service_arguments_valid&=ordinal==4 && a0<=1 && a1 && a1%16==0;++allocations;
            if(a0){high-=a1;return high;}
            const auto result=next;next+=a1;return result;
        };
        // A live peer-owned audio buffer below the shared allocator cursor
        // must survive module scratch, worker and heap allocations.
        std::fill(shared->ram.begin()+0x121b00,shared->ram.begin()+0x122400,0x5a);
        const auto scratch=shared->alloc_system_memory(0,0x3001,0);
        CHECK(scratch==0x123000 && allocations==1);
        shared->configure_buffered_iop_module("probe",0xe0030,0xfd0,0x30,{1,2,3});
        shared->pending_iop_module_path="@buffer/probe";
        const auto stack=shared->allocate_thread_stack(0x1000);
        const auto arena=shared->create_heap(0x1000,1);
        CHECK(stack==0x1df000 && arena==0x126010 && allocations==3);
        CHECK(!shared->static_iop_module_plans[0].allocated);
        CHECK(std::all_of(shared->ram.begin()+0x121b00,shared->ram.begin()+0x122400,[](auto b){return b==0x5a;}));
        shared->release_thread_stack(stack,0x1000);
        CHECK(shared->allocate_thread_stack(0x1000)==stack && allocations==3);
        shared->delete_heap(arena);CHECK(releases==1);
        CHECK(shared->free_system_memory(scratch)==0 && releases==2);
        const auto module=shared->alloc_system_memory(0,0x1000,0);
        CHECK(module==0xe0000 && allocations==3);
        CHECK(shared->free_system_memory(module)==0 && releases==2);
        CHECK(service_arguments_valid);
    }
    {
        auto console=std::make_unique<hg::IopState>();
        console->console_output.assign(65535,'x');
        console->store(0x1000,1,'A');console->store(0x1001,1,'B');console->store(0x1002,1,0);
        CHECK(console->print_console(0x1000)==2);
        CHECK(console->console_output.size()==65536 && console->console_discarded_bytes==1);
        CHECK(console->console_output.substr(65534)=="AB");
        CHECK(console->print_console(0x1000)==2 && console->console_discarded_bytes==3);
    }
}
