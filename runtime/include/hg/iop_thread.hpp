#pragma once
#include "hg/iop.hpp"
namespace hg {
// Native cooperative scheduling at complete translated instruction boundaries.
// CreateThread normally reserves a checked high-side SYSMEM block. Manually
// seeded diagnostic workers use the same allocator lazily on first dispatch.
template<class Run> bool service_iop_thread(IopState& s,Run run,std::uint64_t quantum=1,bool native_burst=false) {
    // CPU interrupt masking prevents asynchronous delivery, not execution of a
    // selected cooperative thread.  Original THREADMAN critical sections may
    // intentionally restore a masked CPU state before their worker blocks.
    if(s.in_interrupt || s.current_thread || !quantum)return false;
    auto chosen=s.threads.size();std::uint32_t priority=127;
    const auto thread_count=s.threads.size();
    auto index=thread_count?s.thread_cursor%thread_count:0;
    for(std::size_t n=0;n<thread_count;++n) {
        auto& t=s.threads[index];
        if(t.delayed && s.virtual_time_us>=t.wake_deadline){t.delayed=false;t.ready=true;}
        if(t.ready && t.priority<priority){chosen=index;priority=t.priority;}
        if(++index==thread_count)index=0;
    }
    // THREADMAN has no time slicing: a running worker keeps the CPU until it blocks,
    // yields or a strictly higher-priority worker becomes ready. Rotating equal
    // priorities every quantum let CRI's report thread run between its copy thread's
    // left and right blocks (right-channel music static, docs/PROGRESS.md 2026-10-25).
    if(s.owner_thread && s.owner_thread<=thread_count) {
        const auto& owner=s.threads[s.owner_thread-1];
        if(owner.ready && !(owner.delayed) && owner.priority<=priority)chosen=s.owner_thread-1;
    }
    if(s.locked_thread)chosen=s.locked_thread-1;
    if(chosen==s.threads.size())return false;
    if(!s.threads[chosen].initialized) {
        auto& t=s.threads[chosen];
        if(!t.stack_base)t.stack_base=s.allocate_thread_stack(t.stack_size);
        t.context.pc=t.entry;t.context.gpr[4]=t.argument;
        t.context.gpr[29]=t.stack_base+t.stack_size-16;t.context.gpr[31]=0x1f0020;
        t.context.interrupts_enabled=s.interrupts_enabled;
        t.initialized=true;
    }
    // The native scheduler's reserved return address denotes a worker that
    // completed outside an explicit block.  Retire it before dispatch rather
    // than attempting to translate the host-only sentinel as guest code.
    if(s.threads[chosen].context.pc==0x1f0020) {
        s.threads[chosen].ready=false;s.thread_cursor=(chosen+1)%s.threads.size();return false;
    }
    const auto reschedules_before=s.thread_reschedule_count;
    const auto saved=s.save_cpu();s.current_thread=std::uint32_t(chosen+1);
    s.restore_cpu(s.threads[chosen].context);
    for(std::uint64_t step=0;step<quantum;++step) {
        if(native_burst){run(s,quantum);break;}
        const auto reschedules=s.thread_reschedule_count;
        run(s,1);
        // Native blocking services change the selected thread's runnable state.
        // A THREADMAN context-transfer request is also an explicit scheduling
        // boundary.  Stop the quantum at either boundary before executing more
        // guest instructions from the old owner.
        if(chosen>=s.threads.size() || !s.threads[chosen].ready || s.pc==0x1f0020 ||
           s.thread_reschedule_count!=reschedules)break;
    }
    // Running code may append threads, invalidating earlier vector references.
    s.threads[chosen].context=s.save_cpu();
    if(s.pc==0x1f0020)s.threads[chosen].ready=false;
    // A masked worker retains ownership only while it is runnable.  Blocking on
    // an original wait boundary releases the CPU to another ready worker;
    // interrupt delivery itself remains masked.
    s.locked_thread=(!s.interrupts_enabled && s.threads[chosen].ready)?std::uint32_t(chosen+1):0u;
    // Ownership survives only an uninterrupted quantum; blocking, exiting and explicit
    // reschedule requests (threadman_reschedule clears owner_thread) release it.
    const bool still_running=s.threads[chosen].ready && s.pc!=0x1f0020;
    if(!still_running)s.owner_thread=0;
    else if(s.thread_reschedule_count==reschedules_before)s.owner_thread=std::uint32_t(chosen+1);
    s.restore_cpu(saved);s.current_thread=0;s.thread_cursor=(chosen+1)%s.threads.size();
    return true;
}

// Give the cooperative scheduler several complete instruction boundaries before
// the connected host services asynchronous peripherals. A high-priority worker
// that blocks during the burst immediately leaves the next boundary available to
// the highest-priority remaining ready worker, matching THREADMAN's observable
// fixed-priority handoff without inventing a wakeup or RPC result.
template<class Run> std::uint64_t service_iop_threads(IopState& s,Run run,std::uint64_t budget,std::uint64_t quantum=1) {
    std::uint64_t dispatched=0;
    for(std::uint64_t step=0;step<budget;++step)if(service_iop_thread(s,run,quantum))++dispatched;
    return dispatched;
}
}
