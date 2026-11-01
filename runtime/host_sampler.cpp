// HG-DIAG-115: see host_sampler.hpp. Diagnostic only; inactive by default.
#include "hg/host_sampler.hpp"
#if defined(_WIN32) && defined(_M_X64)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <psapi.h>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace hg {
namespace {
struct Target {std::string name;HANDLE handle=nullptr;ULONG_PTR stack_high=0;DWORD id=0;};
constexpr unsigned max_depth=40;
constexpr std::size_t stack_bytes=128*1024;
std::mutex targets_mutex;
std::vector<Target> targets;
std::atomic<bool> enabled{false},active{false},stopping{false};
std::thread sampler;
std::string output;
std::vector<std::map<std::vector<std::uint64_t>,std::uint64_t>> stacks; // Per target.
std::vector<std::uint64_t> attempts,failures;
std::uint64_t ticks=0;
alignas(16) unsigned char stack_copy[stack_bytes];

// Unwind against the copied stack. Exceptions from malformed frames end the walk.
unsigned unwind(CONTEXT context,std::uint64_t original_rsp,std::size_t copied,std::uint64_t* pcs) {
    const auto base=reinterpret_cast<std::uint64_t>(stack_copy);
    const auto delta=base-original_rsp;
    if(context.Rbp>=original_rsp&&context.Rbp<original_rsp+copied)context.Rbp+=delta;
    context.Rsp+=delta;
    unsigned depth=0;
    pcs[depth++]=context.Rip;
    __try {
        while(depth<max_depth) {
            DWORD64 image=0;
            auto* entry=RtlLookupFunctionEntry(context.Rip,&image,nullptr);
            if(!entry) {
                if(context.Rsp<base||context.Rsp+8>base+copied)break;
                context.Rip=*reinterpret_cast<const std::uint64_t*>(context.Rsp);context.Rsp+=8;
            } else {
                void* handler=nullptr;DWORD64 frame=0;
                RtlVirtualUnwind(UNW_FLAG_NHANDLER,image,context.Rip,entry,&context,&handler,&frame,nullptr);
            }
            if(!context.Rip||context.Rsp<base||context.Rsp>base+copied)break;
            pcs[depth++]=context.Rip;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return depth;
}

void sample_loop() {
    SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_HIGHEST);
    HANDLE timer=CreateWaitableTimerExW(nullptr,nullptr,0x2/*HIGH_RESOLUTION*/,TIMER_ALL_ACCESS);
    if(!timer)timer=CreateWaitableTimerW(nullptr,FALSE,nullptr);
    LARGE_INTEGER due;due.QuadPart=-10000; // 1 ms relative.
    std::vector<Target> snapshot;
    std::uint64_t pcs[max_depth];
    while(!stopping.load(std::memory_order_relaxed)) {
        SetWaitableTimer(timer,&due,0,nullptr,nullptr,FALSE);
        WaitForSingleObject(timer,INFINITE);
        if(!active.load(std::memory_order_relaxed))continue;
        {std::lock_guard<std::mutex> lock(targets_mutex);snapshot=targets;}
        if(stacks.size()<snapshot.size()){stacks.resize(snapshot.size());attempts.resize(snapshot.size());failures.resize(snapshot.size());}
        ++ticks;
        for(std::size_t t=0;t<snapshot.size();++t) {
            auto& target=snapshot[t];++attempts[t];
            if(SuspendThread(target.handle)==DWORD(-1)){++failures[t];continue;}
            CONTEXT context{};context.ContextFlags=CONTEXT_FULL;
            std::size_t copied=0;std::uint64_t rsp=0;
            if(GetThreadContext(target.handle,&context)) {
                rsp=context.Rsp;
                if(rsp&&rsp<target.stack_high) {
                    copied=std::size_t(target.stack_high-rsp);
                    if(copied>stack_bytes)copied=stack_bytes;
                    std::memcpy(stack_copy,reinterpret_cast<const void*>(rsp),copied);
                }
            }
            ResumeThread(target.handle);
            if(!copied){++failures[t];continue;}
            const auto depth=unwind(context,rsp,copied,pcs);
            ++stacks[t][std::vector<std::uint64_t>(pcs,pcs+depth)];
        }
    }
    CloseHandle(timer);
}
}

void host_sampler_register_current(const char* name) noexcept {
    if(!enabled.load())return;
    try {
        Target target;target.name=name;target.id=GetCurrentThreadId();
        ULONG_PTR low=0,high=0;GetCurrentThreadStackLimits(&low,&high);target.stack_high=high;
        if(!DuplicateHandle(GetCurrentProcess(),GetCurrentThread(),GetCurrentProcess(),&target.handle,
                            THREAD_SUSPEND_RESUME|THREAD_GET_CONTEXT|THREAD_QUERY_INFORMATION,FALSE,0))return;
        std::lock_guard<std::mutex> lock(targets_mutex);
        // Disambiguate repeated names (VU workers, restarted stages).
        unsigned same=0;for(const auto& t:targets)if(t.name.rfind(target.name,0)==0)++same;
        if(same)target.name+="#"+std::to_string(same);
        targets.push_back(std::move(target));
    } catch(...) {}
}
void host_sampler_start_if_requested() {
    const char* file=std::getenv("HG_SAMPLE_FILE");
    if(!file||!*file||enabled.load())return;
    output=file;enabled.store(true);
    sampler=std::thread(sample_loop);
    std::fprintf(stderr,"HG-DIAG-115 host sampler armed: %s\n",file);
}
void host_sampler_set_active(bool value) noexcept {
    if(!enabled.load())return;
    active.store(value);
    std::fprintf(stderr,"HG-DIAG-115 host sampler %s\n",value?"active":"paused");
}
bool host_sampler_enabled() noexcept {return enabled.load();}
void host_sampler_stop_and_write() noexcept {
    if(!enabled.load()||stopping.exchange(true))return;
    active.store(false);
    if(sampler.joinable())sampler.join();
    FILE* out=std::fopen(output.c_str(),"w");
    if(!out)return;
    std::fprintf(out,"ticks %llu\n",(unsigned long long)ticks);
    // Module table lets the offline resolver map driver and system frames.
    HMODULE modules[512];DWORD needed=0;
    if(K32EnumProcessModules(GetCurrentProcess(),modules,sizeof(modules),&needed))
        for(DWORD i=0;i<needed/sizeof(HMODULE)&&i<512;++i) {
            char name[MAX_PATH]{};MODULEINFO info{};
            K32GetModuleFileNameExA(GetCurrentProcess(),modules[i],name,MAX_PATH);
            K32GetModuleInformation(GetCurrentProcess(),modules[i],&info,sizeof(info));
            std::fprintf(out,"module %llx %lx %s\n",(unsigned long long)reinterpret_cast<std::uint64_t>(info.lpBaseOfDll),
                         (unsigned long)info.SizeOfImage,name);
        }
    std::lock_guard<std::mutex> lock(targets_mutex);
    for(std::size_t t=0;t<targets.size();++t) {
        std::fprintf(out,"thread %zu %s attempts=%llu failures=%llu\n",t,targets[t].name.c_str(),
                     (unsigned long long)(t<attempts.size()?attempts[t]:0),(unsigned long long)(t<failures.size()?failures[t]:0));
        if(t>=stacks.size())continue;
        for(const auto& [stack,count]:stacks[t]) {
            std::fprintf(out,"s %zu %llu",t,(unsigned long long)count);
            for(const auto pc:stack)std::fprintf(out," %llx",(unsigned long long)pc);
            std::fputc('\n',out);
        }
    }
    std::fclose(out);
    std::fprintf(stderr,"HG-DIAG-115 host sampler wrote %s ticks=%llu\n",output.c_str(),(unsigned long long)ticks);
}
}
#else
namespace hg {
void host_sampler_register_current(const char*) noexcept {}
void host_sampler_start_if_requested() {}
void host_sampler_set_active(bool) noexcept {}
bool host_sampler_enabled() noexcept {return false;}
void host_sampler_stop_and_write() noexcept {}
}
#endif
