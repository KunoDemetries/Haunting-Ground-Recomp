#pragma once
#include "hg/iop.hpp"
namespace hg {
// A runnable worker paused at a quantum boundary with interrupts masked
// (locked_thread) still owns the CPU: the outer idle context's enable flag
// must not let an asynchronous handler run inside its critical section.
inline bool iop_cpu_interruptible(const IopState& s) {return s.interrupts_enabled && !s.locked_thread;}
// Native cooperative IRQ entry: save the full guest CPU context, run only AOT
// code, and restore the interrupted context after a normal handler return.
template<class Run>
bool service_iop_dma_interrupt(IopState& s,Run run) {
    if(!iop_cpu_interruptible(s))return false;
    // Exact DMAC interrupt routes exercised by the retained devices.  LIBSD
    // registers 36/40 for SPU2 channels 4/7; SIFMAN registers 42/43 for SIF
    // channels 9/10.  Keep this table explicit until another channel is proven.
    // Original CDVDMAN registers IRQ35 for channel 3 at 0xb70b0.
    constexpr unsigned routes[][2]={{3u,35u},{4u,36u},{7u,40u},{9u,42u},{10u,43u}};
    for(const auto& route:routes) {
        const unsigned channel=route[0],irq=route[1];
        if(!(s.dmac.completed_channels&(1u<<channel)) || !(s.interrupt_mask&(std::uint64_t(1)<<irq)))continue;
        const auto handler=s.interrupt_handlers[irq];
        if(!handler.registered)throw IopFault(s.pc,"IOP DMA interrupt has no registered handler");
        const auto registers=s.gpr;
        const auto pc=s.pc,hi=s.hi,lo=s.lo,pending=s.pending_value,next=s.next_value;
        const auto pending_reg=s.pending_reg,next_reg=s.next_reg;
        constexpr std::uint32_t return_pc=0x1f0010;
        s.dmac.completed_channels&=~(1u<<channel);
        s.pending_reg=s.next_reg=0;s.pc=handler.callback;s.interrupts_enabled=false;s.in_interrupt=true;
        s.w(4,handler.argument);s.w(29,0x1ed000);s.w(31,return_pc);
        for(unsigned n=0;n<10000 && s.pc!=return_pc;++n)run(s,1);
        if(s.pc!=return_pc)throw IopFault(s.pc,"IOP DMA interrupt handler exceeded budget");
        const auto result=s.r(2);
        if(result>1)throw IopFault(s.pc,"unsupported IOP interrupt handler return");
        if(!result)s.interrupt_mask&=~(std::uint64_t(1)<<irq);
        s.gpr=registers;s.pc=pc;s.hi=hi;s.lo=lo;
        s.pending_reg=pending_reg;s.next_reg=next_reg;s.pending_value=pending;s.next_value=next;
        s.interrupts_enabled=true;s.in_interrupt=false;return true;
    }
    return false;
}
template<class Run>
bool service_iop_cdvd_interrupt(IopState& s,Run run) {
    constexpr unsigned irq=2;constexpr std::uint32_t return_pc=0x1f0010;
    if(!s.cdvd_irq_pending || !iop_cpu_interruptible(s) || !(s.interrupt_mask&(std::uint64_t(1)<<irq)))return false;
    const auto handler=s.interrupt_handlers[irq];
    if(!handler.registered)throw IopFault(s.pc,"CDVD completion has no registered IOP interrupt handler");
    const auto registers=s.gpr;const auto pc=s.pc,hi=s.hi,lo=s.lo,pending=s.pending_value,next=s.next_value;
    const auto pending_reg=s.pending_reg,next_reg=s.next_reg;
    s.cdvd_irq_pending=false;s.pending_reg=s.next_reg=0;s.pc=handler.callback;s.interrupts_enabled=false;s.in_interrupt=true;
    s.w(4,handler.argument);s.w(29,0x1ed000);s.w(31,return_pc);
    for(unsigned n=0;n<10000 && s.pc!=return_pc;++n)run(s,1);
    if(s.pc!=return_pc)throw IopFault(s.pc,"IOP CDVD interrupt handler exceeded budget");
    const auto result=s.r(2);if(result>1)throw IopFault(s.pc,"unsupported IOP CDVD interrupt handler return");
    if(!result)s.interrupt_mask&=~(std::uint64_t(1)<<irq);
    s.gpr=registers;s.pc=pc;s.hi=hi;s.lo=lo;s.pending_reg=pending_reg;s.next_reg=next_reg;s.pending_value=pending;s.next_value=next;
    s.interrupts_enabled=true;s.in_interrupt=false;return true;
}
template<class Run>
bool service_iop_sio2_interrupt(IopState& s,Run run) {
    constexpr unsigned irq=17;constexpr std::uint32_t return_pc=0x1f0010;
    if(!s.sio2_irq_pending || !iop_cpu_interruptible(s) || !(s.interrupt_mask&(std::uint64_t(1)<<irq)))return false;
    const auto handler=s.interrupt_handlers[irq];
    if(!handler.registered)throw IopFault(s.pc,"SIO2 completion has no registered IOP interrupt handler");
    const auto registers=s.gpr;const auto pc=s.pc,hi=s.hi,lo=s.lo,pending=s.pending_value,next=s.next_value;
    const auto pending_reg=s.pending_reg,next_reg=s.next_reg;
    s.sio2_irq_pending=false;s.pending_reg=s.next_reg=0;s.pc=handler.callback;s.interrupts_enabled=false;s.in_interrupt=true;
    s.w(4,handler.argument);s.w(29,0x1ed000);s.w(31,return_pc);
    for(unsigned n=0;n<10000 && s.pc!=return_pc;++n)run(s,1);
    if(s.pc!=return_pc)throw IopFault(s.pc,"IOP SIO2 interrupt handler exceeded budget");
    const auto result=s.r(2);if(result>1)throw IopFault(s.pc,"unsupported IOP SIO2 interrupt handler return");
    if(!result)s.interrupt_mask&=~(std::uint64_t(1)<<irq);
    s.gpr=registers;s.pc=pc;s.hi=hi;s.lo=lo;s.pending_reg=pending_reg;s.next_reg=next_reg;s.pending_value=pending;s.next_value=next;
    s.interrupts_enabled=true;s.in_interrupt=false;return true;
}
template<class Run>
bool service_iop_timer_interrupt(IopState& s,Run run) {
    constexpr std::uint32_t return_pc=0x1f0010;
    if(!iop_cpu_interruptible(s))return false;
    for(unsigned timer=0;timer<6;++timer) {
        const unsigned irq=timer<3?timer+4:timer+11;
        if(!(s.timer_irq_pending&(1u<<timer)) || !(s.interrupt_mask&(std::uint64_t(1)<<irq)))continue;
        const auto handler=s.interrupt_handlers[irq];if(!handler.registered)throw IopFault(s.pc,"IOP timer completion has no registered handler");
        const auto registers=s.gpr;const auto pc=s.pc,hi=s.hi,lo=s.lo,pending=s.pending_value,next=s.next_value;
        const auto pending_reg=s.pending_reg,next_reg=s.next_reg;
        s.timer_irq_pending&=~std::uint8_t(1u<<timer);s.pending_reg=s.next_reg=0;s.pc=handler.callback;s.interrupts_enabled=false;s.in_interrupt=true;
        s.w(4,handler.argument);s.w(29,0x1ed000);s.w(31,return_pc);
        for(unsigned n=0;n<10000 && s.pc!=return_pc;++n)run(s,1);
        if(s.pc!=return_pc)throw IopFault(s.pc,"IOP timer interrupt handler exceeded budget");
        const auto result=s.r(2);if(result>1)throw IopFault(s.pc,"unsupported IOP timer interrupt handler return");
        if(!result)s.interrupt_mask&=~(std::uint64_t(1)<<irq);
        s.gpr=registers;s.pc=pc;s.hi=hi;s.lo=lo;s.pending_reg=pending_reg;s.next_reg=next_reg;s.pending_value=pending;s.next_value=next;
        s.interrupts_enabled=true;s.in_interrupt=false;return true;
    }
    return false;
}
}
