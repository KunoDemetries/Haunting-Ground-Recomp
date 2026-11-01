#include "hg/image.hpp"
#include "hg/native_host.hpp"
#include "hg/diagnostic_input.hpp"
#include "hg/autopilot.hpp"
#include "hg/iop_image.hpp"
#include "hg/system.hpp"
#include "hg/iop_interrupt.hpp"
#include "hg/ee_interrupt.hpp"
#include "hg/display_timing.hpp"
#include "hg/ee_timer_clock.hpp"
#include "hg/ee_thread.hpp"
#include "hg/iop_thread.hpp"
#include "hg/iop_reboot.hpp"
#include "hg/diagnostic_profile.hpp"
#include "hg/diagnostic_history.hpp"
#include "hg/diagnostic_wav.hpp"
#include "hg/host_audio.hpp"
#include "hg/symbols.hpp"
#include "hg/host_pacing.hpp"
#include "hg/host_wait.hpp"
#include "hg/execution_clock.hpp"
#include "hg/device_thread.hpp"
#include "hg/host_sampler.hpp"
#include "hg/gs_acceleration.hpp"
#include "hg/gs_triangle_acceleration.hpp"
#include <sstream>
#include <iostream>
#include <ctime>
#include <fstream>
#include <chrono>
#include <memory>
#include <utility>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <exception>
#include <cstdlib>

namespace {
void write_vu1_capture(const hg::Vif1Path& v,const std::filesystem::path& target) {
    // HG-DIAG-006: retain the exact host-side VU1 MicroMem image that
    // produced an AOT identity fault. This is read-only diagnostic state.
    std::ofstream vu1_micro(target.string()+".vu1-micro.bin",std::ios::binary|std::ios::trunc);
    vu1_micro.write(reinterpret_cast<const char*>(v.vu_micro_mem.data()),
                    std::streamsize(v.vu_micro_mem.size()*sizeof(std::uint64_t)));
    if(!vu1_micro)throw std::runtime_error("cannot write external VU1 MicroMem dump");
    std::ofstream vu1_data(target.string()+".vu1-data.bin",std::ios::binary|std::ios::trunc);
    vu1_data.write(reinterpret_cast<const char*>(v.vu_mem.data()),
                   std::streamsize(v.vu_mem.size()*sizeof(std::uint32_t)));
    if(!vu1_data)throw std::runtime_error("cannot write external VU1 data-memory dump");
    std::ofstream vu1_defined(target.string()+".vu1-defined.bin",std::ios::binary|std::ios::trunc);
    vu1_defined.write(reinterpret_cast<const char*>(v.vu_mem_defined.data()),
                      std::streamsize(v.vu_mem_defined.size()));
    if(!vu1_defined)throw std::runtime_error("cannot write external VU1 defined-mask dump");
    std::ofstream vu1_state(target.string()+".vu1-state.json",std::ios::trunc);
    vu1_state<<"{\"vi\":[";
    for(std::size_t n=0;n<v.vu1.vi.size();++n)vu1_state<<(n?",":"")<<v.vu1.vi[n];
    vu1_state<<"],\"vf\":[";
    for(std::size_t r=0;r<v.vu1.vf.size();++r) {
        if(r)vu1_state<<',';vu1_state<<'[';
        for(unsigned lane=0;lane<4;++lane)vu1_state<<(lane?",":"")<<v.vu1.vf[r][lane];
        vu1_state<<']';
    }
    vu1_state<<"],\"vf_defined\":[";
    for(std::size_t n=0;n<v.vu1.vf_defined.size();++n)vu1_state<<(n?",":"")<<unsigned(v.vu1.vf_defined[n]);
    vu1_state<<"],\"acc\":[";
    for(unsigned lane=0;lane<4;++lane)vu1_state<<(lane?",":"")<<v.vu1.acc[lane];
    vu1_state<<"],\"acc_defined\":"<<unsigned(v.vu1.acc_defined)
             <<",\"clip\":"<<v.vu1.clip<<",\"status\":"<<v.vu1.status
             <<",\"mac\":"<<v.vu1.mac<<",\"q\":"<<v.vu1.q
             <<",\"i\":"<<v.vu1.i<<",\"p\":"<<v.vu1.p
             <<",\"pending_q\":"<<v.vu1.pending_q<<",\"pending_p\":"<<v.vu1.pending_p
             <<",\"q_cycles_remaining\":"<<v.vu1.q_cycles_remaining
             <<",\"p_cycles_remaining\":"<<v.vu1.p_cycles_remaining
             <<",\"q_pending\":"<<(v.vu1.q_pending?1:0)
             <<",\"p_pending\":"<<(v.vu1.p_pending?1:0)
             <<",\"tpc\":"<<v.vu1.tpc
             <<",\"top\":"<<v.top<<",\"tops\":"<<v.tops
             <<",\"itop\":"<<v.itop<<",\"issue_cycle\":"<<v.vu1.issue_cycle<<",\"vf_ready\":[";
    for(std::size_t r=0;r<v.vu1.vf_ready.size();++r) {
        if(r)vu1_state<<',';vu1_state<<'[';
        for(unsigned lane=0;lane<4;++lane)vu1_state<<(lane?",":"")<<v.vu1.vf_ready[r][lane];
        vu1_state<<']';
    }
    vu1_state<<"],\"vi_ready\":[";
    for(std::size_t n=0;n<v.vu1.vi_ready.size();++n)vu1_state<<(n?",":"")<<v.vu1.vi_ready[n];
    vu1_state<<"]}";
    if(!vu1_state)throw std::runtime_error("cannot write external VU1 state dump");
}
// HG-DIAG-006: bounded, explicit before/after AOT observations. No guest writes.
struct Vu1Capture {
    std::filesystem::path prefix;
    std::uint64_t tex0=0;
    unsigned count=0;
    hg::Vu1AotExecutor execute=nullptr;
} vu1_capture;
struct Vu1OverlapSnapshot {
    hg::Vif1Path work;
    hg::Vu1MemoryWriteTracker vif_writes;
    std::uint64_t micro_generation=~std::uint64_t(0);
    bool initialized=false;

    void begin_drain(hg::Vif1Path& live) {
        // External VIF/FIFO work may have changed VU data between drains. Pay one
        // bounded synchronization copy per drain, never one per MSCAL/MSCNT.
        work.vu_mem=live.vu_mem;work.vu_mem_defined=live.vu_mem_defined;
        work.vu_micro_mem=live.vu_micro_mem;micro_generation=live.host_micro_generation;
        work.vu1=live.vu1;
        vif_writes.clear();
        live.host_vif_memory_tracker=&vif_writes;
        initialized=true;
    }
    void sync_uncontended_vif(hg::Vif1Path& live) {
        // Main-thread VIF work before the next queued VU has no concurrent VU
        // writer. Bring only those touched locations into the persistent work copy.
        for(const auto raw:vif_writes.word_indices) {
            const auto index=std::size_t(raw);work.vu_mem[index]=live.vu_mem[index];
        }
        for(const auto raw:vif_writes.vector_indices) {
            const auto vector=std::size_t(raw);work.vu_mem_defined[vector]=live.vu_mem_defined[vector];
        }
        vif_writes.clear();
    }
    void capture(hg::Vif1Path& live) {
        if(!initialized)begin_drain(live);
        sync_uncontended_vif(live);
        if(micro_generation!=live.host_micro_generation) {
            work.vu_micro_mem=live.vu_micro_mem;micro_generation=live.host_micro_generation;
        }
        work.pending.clear();work.pending_phase=0;work.pending_phase_breaks.clear();
        work.row=live.row;work.column=live.column;
        work.vu1=live.vu1;work.vu1_executor=live.vu1_executor;
        work.host_overlap_enabled=work.host_vu_pending=work.host_vu_running=work.host_vu_stalled=false;
        work.host_vu_entry=0;work.host_vif_memory_tracker=nullptr;
        work.host_micro_generation=live.host_micro_generation;
        work.cycle=live.cycle;work.offset=live.offset;work.base=live.base;work.itops=live.itops;
        work.itop=live.itop;work.tops=live.tops;work.top=live.top;work.mode=live.mode;
        work.mark=live.mark;work.mask=live.mask;work.dbf=live.dbf;work.path3_masked=live.path3_masked;
        work.mark_detected=live.mark_detected;work.error_mask=live.error_mask;
    }
    void merge(hg::Vif1Path& live) {
        live.vu1=work.vu1;
        hg::merge_vu1_overlap_memory(live.vu_mem,live.vu_mem_defined,
                                     work.vu_mem,work.vu_mem_defined,vif_writes);
        vif_writes.clear();
    }
    void end_drain(hg::Vif1Path& live) {
        live.host_vif_memory_tracker=nullptr;
        vif_writes.clear();
    }
};

class Vif1Feeder {
    std::mutex mutex;
    std::condition_variable condition;
    std::thread thread;
    hg::State* state=nullptr;
    bool stop=false,requested=false,done=true;
    std::exception_ptr error;
    void loop() {
        std::unique_lock<std::mutex> lock(mutex);
        for(;;) {
            condition.wait(lock,[&]{return stop||requested;});
            if(stop)return;
            auto* current=state;requested=false;lock.unlock();
            std::exception_ptr failure;
            try {while(!current->vif1.host_vu_stalled && current->pump_vif1()) {}}
            catch(...) {failure=std::current_exception();}
            lock.lock();error=failure;done=true;condition.notify_all();
        }
    }
public:
    Vif1Feeder():thread([this]{loop();}) {}
    Vif1Feeder(const Vif1Feeder&)=delete;
    Vif1Feeder& operator=(const Vif1Feeder&)=delete;
    ~Vif1Feeder() {
        {std::lock_guard<std::mutex> lock(mutex);stop=true;condition.notify_all();}
        if(thread.joinable())thread.join();
    }
    void start(hg::State& next) {
        std::lock_guard<std::mutex> lock(mutex);
        if(!done||requested)throw std::runtime_error("VIF1 overlap feeder started while busy");
        state=&next;error=nullptr;done=false;requested=true;condition.notify_all();
    }
    void wait() {
        std::unique_lock<std::mutex> lock(mutex);
        condition.wait(lock,[&]{return done;});
        const auto failure=error;lock.unlock();
        if(failure)std::rethrow_exception(failure);
    }
};

void drain_vif1_overlap(hg::State& ee,Vif1Feeder& feeder,Vu1OverlapSnapshot& snapshot,bool enabled) {
    if(!enabled) {while(ee.pump_vif1()) {} return;}
    auto& live=ee.vif1;live.host_overlap_enabled=true;
    try {
        snapshot.begin_drain(live);
        for(;;) {
            while(!live.host_vu_pending && ee.pump_vif1()) {}
            if(!live.host_vu_pending)break;
            const auto entry=live.host_vu_entry;
            live.host_vu_pending=false;live.host_vu_stalled=false;live.host_vu_running=true;
            snapshot.capture(live);
            std::exception_ptr vu_error,feeder_error;
            feeder.start(ee);
            try {snapshot.work.vu1_executor(snapshot.work,entry,ee.gif,ee.gs);}
            catch(...) {vu_error=std::current_exception();}
            try {feeder.wait();}catch(...) {feeder_error=std::current_exception();}
            snapshot.merge(live);
            live.host_vu_running=false;
            ee.sync_gs_interrupt();
            if(vu_error)std::rethrow_exception(vu_error);
            if(feeder_error)std::rethrow_exception(feeder_error);
            if(live.host_vu_stalled) {
                live.host_vu_stalled=false;
                live.process_pending(ee.gif,ee.gs);ee.sync_gs_interrupt();
            }
        }
        snapshot.end_drain(live);
        live.host_overlap_enabled=false;live.host_vu_pending=live.host_vu_running=live.host_vu_stalled=false;
    } catch(...) {
        snapshot.end_drain(live);
        live.host_overlap_enabled=false;live.host_vu_pending=live.host_vu_running=live.host_vu_stalled=false;
        throw;
    }
}

void observe_vu1(hg::Vif1Path& v,std::uint16_t entry,hg::GifPath& gif,hg::GsRegisterState& gs) {
    const bool selected=gs.value[6]==vu1_capture.tex0 || gs.value[7]==vu1_capture.tex0;
    std::filesystem::path target;
    if(selected) {
        target=vu1_capture.prefix.string()+"-"+std::to_string(vu1_capture.count++);
        write_vu1_capture(v,target.string()+"-before");
        std::ofstream metadata(target.string()+".json");
        metadata<<"{\"entry\":"<<entry<<",\"tex0\":["<<gs.value[6]<<','<<gs.value[7]
                <<"],\"draw_count\":"<<gs.retired_draw_count+gs.draws.size()<<"}";
        if(!metadata)throw std::runtime_error("cannot write VU1 call metadata");
    }
    vu1_capture.execute(v,entry,gif,gs);
    if(selected) {
        write_vu1_capture(v,target.string()+"-after");
        if(vu1_capture.count==64)v.vu1_executor=vu1_capture.execute;
    }
}
constexpr std::uint32_t iop_return=0x1f0000;
void begin_iop(hg::IopState& s,std::uint32_t entry,std::uint32_t a0=0,std::uint32_t a1=0,std::uint32_t a2=0,std::uint32_t stack_top=0x1ef000) {
    s.gpr.fill(0);s.pending_reg=s.next_reg=0;s.pc=entry;
    s.w(4,a0);s.w(5,a1);s.w(6,a2);s.w(29,stack_top);s.w(31,iop_return);
}
void invoke_iop(hg::IopState& s,std::uint32_t entry,std::uint32_t a0=0,std::uint32_t a1=0,std::uint32_t a2=0,std::uint32_t stack_top=0x1ef000) {
    begin_iop(s,entry,a0,a1,a2,stack_top);
    for(unsigned n=0;n<10000 && s.pc!=iop_return;++n)hg::run_iop(s,1);
    if(s.pc!=iop_return)throw hg::IopFault(s.pc,"IOP bootstrap call exceeded budget");
}
}
int hg::run_native_game(int argc,char** argv,hg::NativeHost* host) {
    bool profile_enabled=false,profile_gs=false,sync_devices=false;
    bool frame_rate_probe=false; // HG-DIAG-114: original-writer rate without --profile's synchronous devices.
    bool realtime=false;
    bool input_replay_wait_live=false;
    // Input-replay checkpoints: --fast-replay skips host pacing (never guest work)
    // until the scheduled replay ends; F5 writes every applied input so far.
    bool fast_replay=false;
    std::filesystem::path checkpoint_file;
    std::filesystem::path autopilot_file;   // HG-DIAG-123
    std::uint32_t stop_word_address=0,stop_word_value=0;bool stop_word=false;   // HG-DIAG-124
    // HG-DIAG-125: controller snapshots applied when an EE word first equals a value
    // (e.g. a game frame counter), so a scripted input lands on the same game frame here
    // and in the oracle.
    struct WordInput {std::uint32_t address=0,value=0;std::string pad;bool done=false;};
    std::vector<WordInput> word_inputs;
    // True only while a --fast-replay schedule is still being replayed. Host
    // pacing and host speaker output (which blocks on device buffers) pause;
    // guest SPU2 state, clocks and every scheduled input are unchanged.
    bool fast_replay_active=false;
    // Virtual memory card (slot 1). --memory-card-after-replay keeps it
    // unplugged while a checkpoint replays (matching its card-less history),
    // then inserts it when live play begins, like a hot-plugged card.
    std::filesystem::path memory_card_file;
    bool memory_card_after_replay=false;
    std::uint64_t memory_card_insert_at=0; // Repro: insert at this exact slice (0 = unused).
    bool issue_slot_clock=false;
    bool verify=false,verify_services=false,verify_loadfile=false,verify_cdvd=false,verify_reboot=false,stop_on_loadfile_error=false;
    std::filesystem::path toc_record;
    bool capture_vu1_tex0_supplied=false;
    std::uint32_t watch_iop_word=0x0009a920u;
    std::filesystem::path dump_iop;
    std::filesystem::path preview_file;
    std::filesystem::path input_file;
    std::filesystem::path input_events_file;
    std::filesystem::path input_log;
    std::filesystem::path spu2_input_wav;
    std::filesystem::path spu2_mix_wav; // HG-DIAG-120: final SPU2 mix capture.
    std::filesystem::path spu2_sync_log;
    int spu2_input_core=-1;
    int spu2_output_core=-1;
    // HG-DIAG-006: explicit guest-slice input makes performance replays repeatable.
    constexpr std::uint64_t max_run_slices=1000000000000ull; // ~11 days at 1M slices/s.
    struct InputEvent {std::uint64_t slice;std::string command;};
    constexpr std::size_t max_input_events=1000000; // Long checkpoint histories (analog input) exceed thousands of events.
    std::vector<InputEvent> input_events;
    const auto append_input_event=[&](const std::string& stamp,const std::string& command) {
        try {
            std::size_t used=0;
            if(stamp.empty() || stamp.find_first_not_of("0123456789")!=std::string::npos)return false;
            const auto value=std::stoull(stamp,&used);
            hg::HostInput pad;
            if(used!=stamp.size() || value>=max_run_slices || input_events.size()>=max_input_events ||
               (!input_events.empty() && value<=input_events.back().slice) ||
               (command!="none" && command!="left" && command!="cross" && command!="start" &&
                !hg::parse_diagnostic_pad(command,pad)))return false;
            input_events.push_back({std::uint64_t(value),command});return true;
        } catch(...) { return false; }
    };
    std::array<std::uint32_t,hg::IopState::max_trace_watches> watch_iop_pcs{};
    std::size_t watch_iop_pc_count=0;
    std::array<std::uint32_t,hg::State::max_trace_watches> watch_ee_pcs{};
    std::size_t watch_ee_pc_count=0;
    std::uint64_t slices=200000;
    unsigned checkpoint_every=0;
    unsigned cross_at=0;
    unsigned left_at=0;
    unsigned iop_worker_budget=1;
    std::filesystem::path elf="Haunting Ground (USA)/SLUS_210.75",modules="Haunting Ground (USA)/MODULES",disc="Haunting Ground (USA)/Haunting Ground (USA).iso";
    for(int n=1;n<argc;++n) {
        const std::string arg=argv[n];
        if(arg=="--verify-sif")verify=true;
        else if(arg=="--verify-loadfile"){verify_loadfile=true;slices=2000000;}
        else if(arg=="--verify-reboot"){verify_reboot=true;slices=20000000;}
        else if(arg=="--verify-cdvd"){verify_cdvd=true;slices=20000000;}
        else if(arg=="--verify-services"){verify_services=true;slices=10000000;}
        else if(arg=="--stop-on-loadfile-error")stop_on_loadfile_error=true;
        else if(arg=="--iop-worker-budget" && n+1<argc) {
            try {
                std::size_t parsed=0;const auto value=std::stoul(argv[++n],&parsed,0);
                if(parsed!=std::string(argv[n]).size() || !value || value>128)return 1;
                iop_worker_budget=unsigned(value);
            }catch(...){return 1;}
        }
        else if(arg=="--watch-iop-word" && n+1<argc) {
            try {
                std::size_t parsed=0;const auto value=std::stoul(argv[++n],&parsed,0);
                if(parsed!=std::string(argv[n]).size() || value>0x1ffffcu || value%4)return 1;
                watch_iop_word=static_cast<std::uint32_t>(value);
            } catch(...) {return 1;}
        }
        else if(arg=="--watch-iop-pc" && n+1<argc) {
            try {
                std::size_t parsed=0;
                const auto value=std::stoul(argv[++n],&parsed,0);
                if(parsed!=std::string(argv[n]).size() || value>0xffffffffu) return 1;
                const auto address=static_cast<std::uint32_t>(value);
                bool duplicate=false;
                for(std::size_t item=0;item<watch_iop_pc_count;++item)duplicate|=watch_iop_pcs[item]==address;
                if(!address || (!duplicate && watch_iop_pc_count==watch_iop_pcs.size()))return 1;
                if(!duplicate)watch_iop_pcs[watch_iop_pc_count++]=address;
            } catch(...) { return 1; }
        }
        else if(arg=="--watch-ee-pc" && n+1<argc) {
            try {
                std::size_t parsed=0;const auto value=std::stoul(argv[++n],&parsed,0);
                if(parsed!=std::string(argv[n]).size() || value>0xffffffffu)return 1;
                const auto address=static_cast<std::uint32_t>(value);bool duplicate=false;
                for(std::size_t item=0;item<watch_ee_pc_count;++item)duplicate|=watch_ee_pcs[item]==address;
                if(!address || (!duplicate && watch_ee_pc_count==watch_ee_pcs.size()))return 1;
                if(!duplicate)watch_ee_pcs[watch_ee_pc_count++]=address;
            } catch(...) {return 1;}
        }
        else if((arg=="--press-cross-at"||arg=="--press-left-at") && n+1<argc) {
            try {
                std::size_t used=0;const auto value=std::stoul(argv[++n],&used);
                if(used!=std::string(argv[n]).size() || !value || value>499750000)return 1;
                (arg=="--press-cross-at"?cross_at:left_at)=unsigned(value);
            }catch(...){return 1;}
        }
        else if(arg=="--input-at" && n+2<argc) {
            if(!input_events_file.empty())return 1;
            const std::string stamp=argv[++n],command=argv[++n];
            if(!append_input_event(stamp,command))return 1;
        }
        else if(arg=="--input-events-file" && n+1<argc) {
            if(!input_events_file.empty() || !input_events.empty())return 1;
            input_events_file=argv[++n];std::ifstream input(input_events_file,std::ios::binary);
            if(!input)return 1;
            std::string line;
            while(std::getline(input,line)) {
                if(!line.empty() && line.back()=='\r')line.pop_back();
                const auto split=line.find(' ');
                if(split==std::string::npos || split==0 || split+1==line.size() ||
                   line.find(' ',split+1)!=std::string::npos ||
                   !append_input_event(line.substr(0,split),line.substr(split+1)))return 1;
            }
            if(!input.eof() || input_events.empty())return 1;
        }
        else if(arg=="--input-replay-wait-live")input_replay_wait_live=true;
        else if(arg=="--fast-replay")fast_replay=true;
        else if(arg=="--memory-card" && n+1<argc)memory_card_file=argv[++n];
        else if(arg=="--memory-card-after-replay")memory_card_after_replay=true;
        else if(arg=="--memory-card-insert-at" && n+1<argc)memory_card_insert_at=std::strtoull(argv[++n],nullptr,10);
        else if(arg=="--checkpoint-file" && n+1<argc)checkpoint_file=argv[++n];
        else if(arg=="--autopilot" && n+1<argc)autopilot_file=argv[++n];
        else if(arg=="--input-on-ee-word" && n+3<argc) {
            WordInput w;w.address=std::uint32_t(std::stoul(argv[++n],nullptr,0));w.value=std::uint32_t(std::stoul(argv[++n],nullptr,0));
            w.pad=argv[++n];hg::HostInput check;
            if(!hg::parse_diagnostic_pad(w.pad,check))return 1;
            word_inputs.push_back(w);
        }
        else if(arg=="--stop-at-ee-word" && n+2<argc){stop_word_address=std::uint32_t(std::stoul(argv[++n],nullptr,0));stop_word_value=std::uint32_t(std::stoul(argv[++n],nullptr,0));stop_word=true;}
        else if(arg=="--capture-vu1" && n+1<argc)vu1_capture.prefix=argv[++n];
        else if(arg=="--capture-vu1-tex0" && n+1<argc) {
            try {
                const std::string value=argv[++n];std::size_t used=0;
                if(value.empty()||value[0]=='-')return 1;
                vu1_capture.tex0=std::stoull(value,&used,0);
                if(used!=value.size())return 1;
                capture_vu1_tex0_supplied=true;
            }catch(...){return 1;}
        }
        else if(arg=="--profile")profile_enabled=true;
        else if(arg=="--frame-rate-probe")frame_rate_probe=true;
        else if(arg=="--profile-gs")profile_gs=true;
        else if(arg=="--sync-devices")sync_devices=true;
        else if(arg=="--realtime")realtime=true;
        else if(arg=="--clock-profile" && n+1<argc) {
            const std::string value=argv[++n];
            if(value=="issue-slots")issue_slot_clock=true;
            else if(value!="legacy")return 1;
        }
        else if(arg=="--checkpoint-every" && n+1<argc){try{checkpoint_every=std::stoul(argv[++n]);}catch(...){return 1;}if(!checkpoint_every || checkpoint_every>500000000)return 1;}
        else if(arg=="--slices" && n+1<argc){try{slices=std::stoull(argv[++n]);}catch(...){return 1;}if(!slices || slices>max_run_slices)return 1;}
        else if(arg=="--elf" && n+1<argc)elf=argv[++n];
        else if(arg=="--modules" && n+1<argc)modules=argv[++n];
        else if(arg=="--disc" && n+1<argc)disc=argv[++n];
        else if(arg=="--toc-record" && n+1<argc)toc_record=argv[++n];
        else if(arg=="--dump-iop" && n+1<argc)dump_iop=argv[++n];
        else if(arg=="--preview-file" && n+1<argc)preview_file=argv[++n];
        else if(arg=="--input-file" && n+1<argc)input_file=argv[++n];
        else if(arg=="--input-log" && n+1<argc)input_log=argv[++n];
        else if(arg=="--spu2-input-wav" && n+1<argc)spu2_input_wav=argv[++n];
        else if(arg=="--spu2-mix-wav" && n+1<argc)spu2_mix_wav=argv[++n];
        else if(arg=="--spu2-sync-log" && n+1<argc)spu2_sync_log=argv[++n];
        else if(arg=="--spu2-input-core" && n+1<argc) {
            const std::string value=argv[++n];
            if(value=="0")spu2_input_core=0;
            else if(value=="1")spu2_input_core=1;
            else return 1;
        }
        else if(arg=="--spu2-output-core" && n+1<argc) {
            const std::string value=argv[++n];
            if(value=="0")spu2_output_core=0;
            else if(value=="1")spu2_output_core=1;
            else return 1;
        }
        else {std::cerr<<"Usage: hg_system_diagnostic [--profile] [--profile-gs] [--realtime] [--capture-vu1 external-prefix --capture-vu1-tex0 value] [--clock-profile legacy|issue-slots] [--verify-sif|--verify-services|--verify-cdvd|--stop-on-loadfile-error] [--watch-ee-pc address] [--watch-iop-pc address] [--watch-iop-word aligned-address] [--slices count] [--checkpoint-every slices] [--press-cross-at slice] [--press-left-at slice] [--iop-worker-budget 1..128] [--elf path] [--modules directory] [--disc path] [--toc-record file] [--dump-iop external-path] [--preview-file external.ppm] [--input-file external.txt] [--input-events-file external.txt] [--input-replay-wait-live] [--input-log external.txt] [--input-at slice none|left|cross|start|pad:buttons,4axes,12pressures] [--spu2-input-wav external.wav --spu2-input-core 0|1] [--spu2-output-core 0|1] [--spu2-sync-log external.csv]\n";return 1;}
    }
    if((!input_file.empty() && (left_at || cross_at || !input_events.empty())) ||
       (!input_events.empty() && (left_at || cross_at))) {
        std::cerr<<"Use one diagnostic input source: input-file, input-at, or button pulses\n";return 1;
    }
    if(!input_events.empty() && input_events.back().slice>=slices) {
        std::cerr<<"Diagnostic input event is outside the run budget\n";return 1;
    }
    if(input_replay_wait_live && (input_events.empty() || !host)) {
        std::cerr<<"Replay-ready wait requires scheduled input and a native host\n";return 1;
    }
    if(spu2_input_wav.empty()!=(spu2_input_core<0)) {
        std::cerr<<"SPU2 input WAV capture requires both --spu2-input-wav and --spu2-input-core 0|1\n";return 1;
    }
    if(!spu2_mix_wav.empty()) {
        spu2_mix_wav=std::filesystem::absolute(spu2_mix_wav).lexically_normal();
        const auto relative=spu2_mix_wav.lexically_relative(std::filesystem::current_path().lexically_normal());
        if(relative.empty() || *relative.begin()!="..") {
            std::cerr<<"SPU2 mix WAV must be outside the repository\n";return 1;
        }
    }
    if(!spu2_input_wav.empty()) {
        spu2_input_wav=std::filesystem::absolute(spu2_input_wav).lexically_normal();
        const auto relative=spu2_input_wav.lexically_relative(std::filesystem::current_path().lexically_normal());
        if(relative.empty() || *relative.begin()!="..") {
            std::cerr<<"SPU2 input WAV must be outside the repository\n";return 1;
        }
    }
    if(!spu2_sync_log.empty()) {
        spu2_sync_log=std::filesystem::absolute(spu2_sync_log).lexically_normal();
        const auto relative=spu2_sync_log.lexically_relative(std::filesystem::current_path().lexically_normal());
        if(relative.empty() || *relative.begin()!="..") {
            std::cerr<<"SPU2 sync log must be outside the repository\n";return 1;
        }
    }
    if(!preview_file.empty()) {
        preview_file=std::filesystem::absolute(preview_file).lexically_normal();
        const auto relative=preview_file.lexically_relative(std::filesystem::current_path().lexically_normal());
        if(relative.empty() || *relative.begin()!="..") {
            std::cerr<<"Preview file must be outside the repository\n";return 1;
        }
    }
    if(!input_log.empty()) {
        input_log=std::filesystem::absolute(input_log).lexically_normal();
        const auto relative=input_log.lexically_relative(std::filesystem::current_path().lexically_normal());
        if(relative.empty() || *relative.begin()!="..") {
            std::cerr<<"Input log must be outside the repository\n";return 1;
        }
    }
    hg::State ee;hg::EeDmaInterruptDispatcher ee_dma_interrupt;hg::EeIntcInterruptDispatcher ee_intc_interrupt;hg::DisplayTiming display_timing;hg::IopState iop;iop.sif=ee.sif;
    // HG-PERF-118 preview file writer; declared before the device thread owner
    // so posted preview closures can never outlive it.
    struct PreviewWriter {
        std::mutex mutex;
        std::condition_variable wake,idle;
        bool pending=false,busy=false,stopping=false;
        unsigned width=0,height=0;
        std::vector<std::uint32_t> image;
        std::exception_ptr error;
        std::thread thread;
        void rethrow() {
            std::lock_guard<std::mutex> lock(mutex);
            if(error){const auto failure=error;error=nullptr;std::rethrow_exception(failure);}
        }
        // Waits until no frame is queued or being written (synchronous publication).
        void drain() {std::unique_lock<std::mutex> lock(mutex);idle.wait(lock,[&]{return !pending&&!busy;});}
        void stop() {
            {std::lock_guard<std::mutex> lock(mutex);stopping=true;}
            wake.notify_all();if(thread.joinable())thread.join();
        }
        ~PreviewWriter(){stop();}
    } preview_writer;
    // Host device thread (device_link.hpp). Declared after `ee` so it always
    // drains and stops before the state it owns is destroyed.
    struct DeviceOwner {
        hg::State& ee;hg::NativeHost* host;
        std::unique_ptr<hg::DeviceLink> link;bool moved_context=false;
        std::exception_ptr stop() {
            if(!link)return nullptr;
            auto failure=link->stop();ee.device=nullptr;link.reset();
            if(moved_context){host->gs_context(true);moved_context=false;}
            return failure;
        }
        // Exit-path diagnostics read device state directly afterwards.
        void stop_reporting(const char* already) {
            const auto failure=stop();
            if(!failure)return;
            try {std::rethrow_exception(failure);}
            catch(const std::exception& e) {if(!already||std::string(e.what())!=already)std::cerr<<"Device thread failure: "<<e.what()<<'\n';}
            catch(...) {std::cerr<<"Device thread failure: unknown\n";}
        }
        void post_or_run(std::function<void()> work) {
            if(link)link->post(std::move(work));else work();
        }
        ~DeviceOwner() {stop();}
    } device_owner{ee,host};
    ee.gs.profile_raster=profile_gs;
    const auto report_gs_profile=[&] {
        if(!profile_gs)return;
        for(unsigned kind=0;kind<7;++kind)
            std::cerr<<"GS profile primitive="<<kind<<" calls="<<ee.gs.raster_calls[kind]
                     <<" host_ns="<<ee.gs.raster_nanoseconds[kind]<<'\n';
    };
    hg::EeTimerClock ee_timer_clock;
    if(vu1_capture.prefix.empty()==capture_vu1_tex0_supplied) {
        std::cerr<<"VU1 capture requires both external prefix and TEX0 filter\n";return 1;
    }
    if(!vu1_capture.prefix.empty()) {
        vu1_capture.prefix=std::filesystem::absolute(vu1_capture.prefix).lexically_normal();
        const auto relative=vu1_capture.prefix.lexically_relative(std::filesystem::current_path().lexically_normal());
        if(relative.empty() || *relative.begin()!="..") {
            std::cerr<<"VU1 capture must be outside the repository\n";return 1;
        }
    }
    struct Spu2RawStats {
        std::uint64_t frames=0;
        std::uint64_t nonzero_samples=0;
        std::uint64_t hash=1469598103934665603ull;
        int min_left=32767,max_left=-32768,min_right=32767,max_right=-32768;
    };
    std::array<Spu2RawStats,2> spu2_raw_stats{};
    std::unique_ptr<hg::DiagnosticWavWriter> spu2_wav,spu2_mix_writer;
    std::unique_ptr<hg::HostAudioOutput> spu2_output;
    // HG-DIAG-014: read existing host counters without flushing or changing PCM.
    const auto report_speaker=[&](const char* stage) {
        if(!profile_enabled || !spu2_output)return;
        std::cerr<<"SPU2 speaker "<<stage<<" core="<<spu2_output_core
            <<" frames="<<spu2_output->frames()<<" underruns="<<spu2_output->underruns()
            <<" max_queued_buffers="<<spu2_output->max_queued_buffers()
            <<" input_peak="<<spu2_output->input_peak()<<" output_peak="<<spu2_output->output_peak()<<'\n';
    };
    const auto signed_sample=[](std::uint16_t value) {return value<0x8000u?int(value):int(value)-0x10000;};
    const auto observe_spu2=[&](unsigned core,std::uint16_t left,std::uint16_t right) {
        if(core>=spu2_raw_stats.size())throw std::runtime_error("invalid SPU2 input core");
        auto& stats=spu2_raw_stats[core];++stats.frames;
        stats.nonzero_samples+=(left!=0)+(right!=0);
        const int l=signed_sample(left),r=signed_sample(right);
        if(l<stats.min_left)stats.min_left=l;if(l>stats.max_left)stats.max_left=l;
        if(r<stats.min_right)stats.min_right=r;if(r>stats.max_right)stats.max_right=r;
        for(const auto byte:{std::uint8_t(left),std::uint8_t(left>>8),std::uint8_t(right),std::uint8_t(right>>8)}) {
            stats.hash^=byte;stats.hash*=1099511628211ull;
        }
        if(spu2_wav && int(core)==spu2_input_core)spu2_wav->append(left,right);
    };
    const auto report_spu2_capture=[&]() {
        if(!spu2_wav)return;
        for(unsigned core=0;core<spu2_raw_stats.size();++core) {
            const auto& stats=spu2_raw_stats[core];
            std::cerr<<"SPU2 raw core "<<core<<": frames="<<stats.frames<<" nonzero_samples="<<stats.nonzero_samples;
            if(stats.frames)std::cerr<<" L="<<stats.min_left<<".."<<stats.max_left<<" R="<<stats.min_right<<".."<<stats.max_right;
            std::cerr<<" fnv64=0x"<<std::hex<<stats.hash<<std::dec<<'\n';
        }
        std::cerr<<"SPU2 raw WAV core "<<spu2_input_core<<": "<<spu2_input_wav.string()<<" frames="<<spu2_wav->frames()<<'\n';
    };
    // HG-PERF-118: the PPM encode/write/rename of --preview-file runs on a host
    // writer thread (latest frame wins; the preview is sampled). The ordered
    // committed-VRAM readback and host presentation stay at the original point.
    // Final/fault publication is synchronous, so the retained file is exact.
    const auto write_preview_file=[&](unsigned width,unsigned height,const std::vector<std::uint32_t>& image) {
        const auto pending=std::filesystem::path(preview_file.string()+".next");
        std::ofstream pixels(pending,std::ios::binary|std::ios::trunc);
        pixels<<"P6\n"<<width<<' '<<height<<"\n255\n";
        std::vector<char> rgb(image.size()*3);
        for(std::size_t n=0;n<image.size();++n) {
            const auto rgba=image[n];
            rgb[n*3]=char(rgba);rgb[n*3+1]=char(rgba>>8);rgb[n*3+2]=char(rgba>>16);
        }
        pixels.write(rgb.data(),std::streamsize(rgb.size()));
        pixels.close();
        if(!pixels)throw std::runtime_error("cannot write diagnostic preview");
        // A reader retains its last complete frame during this replacement gap.
        std::error_code preview_error;
        for(unsigned attempt=0;attempt<100;++attempt) {
            std::filesystem::remove(preview_file,preview_error);
            if(!preview_error)break;
            // A Windows reader may hold the old frame open briefly. Preserve
            // the completed pending image and retry publication, not execution.
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        if(preview_error)throw std::filesystem::filesystem_error("publish preview",preview_file,preview_error);
        std::filesystem::rename(pending,preview_file);
    };
    const auto queue_preview_file=[&](unsigned width,unsigned height,std::vector<std::uint32_t>&& image) {
        if(!preview_writer.thread.joinable())
            preview_writer.thread=std::thread([&] {
                std::unique_lock<std::mutex> lock(preview_writer.mutex);
                for(;;) {
                    preview_writer.wake.wait(lock,[&]{return preview_writer.pending||preview_writer.stopping;});
                    if(preview_writer.stopping)break; // Final frames are published synchronously.
                    auto frame=std::move(preview_writer.image);
                    const auto w=preview_writer.width,h=preview_writer.height;
                    preview_writer.pending=false;preview_writer.busy=true;lock.unlock();
                    std::exception_ptr failure;
                    try{write_preview_file(w,h,frame);}catch(...){failure=std::current_exception();}
                    lock.lock();preview_writer.busy=false;
                    if(failure&&!preview_writer.error)preview_writer.error=failure;
                    preview_writer.idle.notify_all();
                }
            });
        preview_writer.rethrow();
        {
            std::lock_guard<std::mutex> lock(preview_writer.mutex);
            preview_writer.width=width;preview_writer.height=height;
            preview_writer.image=std::move(image);preview_writer.pending=true;
        }
        preview_writer.wake.notify_one();
    };
    const auto publish_preview=[&](bool synchronous) {
        if(preview_file.empty()&&!host)return;
        // Observe only committed VRAM. Presentation must not flush guest draws.
        if(host&&!(ee.gs.privileged_pmode&3))return; // Scanout is explicitly disabled.
        hg::GsDisplayImage display;
        try {display=ee.gs.display_image();}
        catch(const std::runtime_error&) {
            if(host)throw; // A real host must report unsupported enabled scanout.
            return; // The diagnostic preview may not have a supported scanout yet.
        }
        if(host) {
            // Direct in-process presentation observes committed pixels only.
            // Optional diagnostic file publication remains independent.
            if(preview_file.empty()) {
                host->present(display.width,display.height,std::move(display.rgba));return;
            }
            host->present(display.width,display.height,std::vector<std::uint32_t>(display.rgba));
        }
        if(synchronous) {
            preview_writer.drain();preview_writer.rethrow();
            write_preview_file(display.width,display.height,display.rgba);
        } else queue_preview_file(display.width,display.height,std::move(display.rgba));
    };
    iop.ram_write_trace_address=watch_iop_word;
    auto dump_iop_state=[&](const char* reason="fault") {
        // A failed host scanout must not throw again before the fault dump.
        if(!host||std::string(reason)!="fault")publish_preview(true);
        if(dump_iop.empty())return;
        const auto target=std::filesystem::absolute(dump_iop).lexically_normal();
        const auto root=std::filesystem::current_path().lexically_normal();
        const auto relative=target.lexically_relative(root);
        if(relative.empty() || (*relative.begin()!=".." && *relative.begin()!="..\\"))
            throw std::runtime_error("IOP diagnostic dump must be outside the repository");
        std::ofstream output(target,std::ios::binary|std::ios::trunc);
        output.write(reinterpret_cast<const char*>(iop.ram.data()),iop.ram.size());
        if(!output)throw std::runtime_error("cannot write external IOP diagnostic dump");
        std::ofstream ee_memory(target.string()+".ee-ram.bin",std::ios::binary|std::ios::trunc);
        ee_memory.write(reinterpret_cast<const char*>(ee.ram.data()),ee.ram.size());
        if(!ee_memory)throw std::runtime_error("cannot write external EE diagnostic dump");
        write_vu1_capture(ee.vif1,target);
        std::ofstream vram(target.string()+".gs-vram.bin",std::ios::binary|std::ios::trunc);
        for(const auto word:ee.gs.vram)for(unsigned byte=0;byte<4;++byte)vram.put(char(word>>(byte*8)));
        if(!vram)throw std::runtime_error("cannot write GS memory capture");
        std::ofstream draws(target.string()+".gs.json",std::ios::trunc);
        draws<<"{\"pmode\":"<<ee.gs.privileged_pmode<<",\"dispfb\":["<<ee.gs.privileged_dispfb[0]<<','<<ee.gs.privileged_dispfb[1]
             <<"],\"display\":["<<ee.gs.privileged_display[0]<<','<<ee.gs.privileged_display[1]<<"],\"rasterized\":"<<ee.gs.rasterized_draw_count
             <<",\"retired\":"<<ee.gs.retired_draw_count<<",\"first_pending\":"<<ee.gs.pending_draw_index()
             <<",\"clut_load_count\":"<<ee.gs.clut_load_count<<",\"registers\":[";
        for(std::size_t n=0;n<ee.gs.value.size();++n)draws<<(n?",":"")<<ee.gs.value[n];
        draws<<"],\"draws\":[";
        std::cerr<<"Live GS transfer BITBLTBUF=0x"<<std::hex<<ee.gs.value[0x50]
                 <<" TRXPOS=0x"<<ee.gs.value[0x51]<<" TRXREG=0x"<<ee.gs.value[0x52]
                 <<" TRXDIR=0x"<<ee.gs.value[0x53]<<std::dec<<'\n';
        auto serialized_environment=ee.gs.draw_environment_active?
            ee.gs.draw_environment_base():ee.gs.value;
        std::size_t serialized_event=0;
        for(std::size_t n=0;n<ee.gs.draws.size();++n) {
            const auto& draw=ee.gs.draws[n];if(n)draws<<',';
            const GsDraw::Environment* environment=&ee.gs.value;
            if(draw.explicit_environment)environment=draw.explicit_environment.get();
            else if(draw.environment_event_count!=(std::numeric_limits<std::uint32_t>::max)()) {
                if(draw.environment_event_count<serialized_event||
                   draw.environment_event_count>ee.gs.draw_environment_events.size())
                    throw std::runtime_error("invalid serialized GS draw environment cursor");
                while(serialized_event<draw.environment_event_count) {
                    const auto& change=ee.gs.draw_environment_events[serialized_event++];
                    serialized_environment[change.address]=change.value;
                }
                environment=&serialized_environment;
            }
            draws<<"{\"prim\":"<<draw.prim_state<<",\"xyoffset\":"<<draw.xyoffset<<",\"environment\":[";
            for(unsigned r=0;r<environment->size();++r){if(r)draws<<',';draws<<(*environment)[r];}
            draws<<"],\"vertices\":[";
            for(unsigned v=0;v<draw.count;++v) {
                if(v)draws<<',';const auto& p=draw.vertices[v];
                draws<<'['<<p.x<<','<<p.y<<','<<p.z<<','<<p.rgba<<','<<p.uv<<','<<p.st<<','<<p.q<<','<<p.fog<<']';
            }
            draws<<"]}";
        }
        draws<<"]}";
        if(!draws)throw std::runtime_error("cannot write GS draw capture");
        // Capture the currently committed display memory without flushing or
        // otherwise changing guest rendering state during diagnostics.
        try {
            const auto display=ee.gs.display_image();
            std::ofstream pixels(target.string()+".display.ppm",std::ios::binary|std::ios::trunc);
            pixels<<"P6\n"<<display.width<<' '<<display.height<<"\n255\n";
            for(const auto rgba:display.rgba) {
                const char rgb[]{char(rgba),char(rgba>>8),char(rgba>>16)};
                pixels.write(rgb,3);
            }
            if(!pixels)throw std::runtime_error("cannot write display capture");
            std::cerr<<"Display capture "<<display.width<<'x'<<display.height
                     <<" draws="<<ee.gs.draws.size()<<" rasterized="<<ee.gs.rasterized_draw_count
                     <<" retired="<<ee.gs.retired_draw_count<<" first_pending="<<ee.gs.pending_draw_index()<<'\n';
        } catch(const std::runtime_error& e) {
            std::cerr<<"Display capture unavailable: "<<e.what()<<'\n';
        }
        std::ofstream console(target.string()+".console.txt",std::ios::binary|std::ios::trunc);
        console<<"[native retained console; discarded bytes="<<iop.console_discarded_bytes<<"]\n"<<iop.console_output;
        if(!console)throw std::runtime_error("cannot write external IOP console dump");
        const auto metadata_path=std::filesystem::path(target.string()+".json");
        std::ofstream metadata(metadata_path,std::ios::trunc);
        metadata<<"{\n  \"schema_version\": 1,\n  \"kind\": \"native-iop-"<<reason<<"\",\n"
                <<"  \"ram_size\": "<<iop.ram.size()<<",\n"
                <<"  \"pc\": "<<iop.pc<<",\n  \"hi\": "<<iop.hi<<",\n  \"lo\": "<<iop.lo<<",\n"
                <<"  \"pending_reg\": "<<iop.pending_reg<<",\n  \"pending_value\": "<<iop.pending_value<<",\n"
                <<"  \"next_reg\": "<<iop.next_reg<<",\n  \"next_value\": "<<iop.next_value<<",\n"
                <<"  \"current_thread\": "<<iop.current_thread<<",\n"
                <<"  \"iop_worker_budget\": "<<iop_worker_budget<<",\n"
                <<"  \"virtual_time_us\": "<<iop.virtual_time_us<<",\n  \"gpr\": [";
        for(std::size_t n=0;n<iop.gpr.size();++n)metadata<<(n?", ":"")<<iop.gpr[n];
        metadata<<"],\n  \"ee\": {\"pc\":"<<ee.pc<<",\"gpr\":[";
        for(std::size_t n=0;n<ee.gpr.size();++n)
            metadata<<(n?",":"")<<'['<<ee.gpr[n].lo<<','<<ee.gpr[n].hi<<']';
        metadata<<"],\"timer_fraction\":"<<ee_timer_clock.fractional_cycles<<",\"timers\":[";
        for(unsigned n=0;n<4;++n) {
            const auto& t=ee.timers.timers[n];
            metadata<<(n?",":"")<<"{\"count\":"<<t.count<<",\"mode\":"<<t.mode
                    <<",\"compare\":"<<t.compare<<",\"phase\":"<<t.phase<<'}';
        }
        metadata<<"],\"dmac_status\":"<<ee.dmac.status<<",\"dma\":[";
        for(unsigned n=0;n<ee.dmac.channels.size();++n) {
            const auto& c=ee.dmac.channels[n];
            metadata<<(n?",":"")<<"{\"chcr\":"<<c.chcr<<",\"madr\":"<<c.madr
                    <<",\"qwc\":"<<c.qwc<<",\"tadr\":"<<c.tadr<<'}';
        }
        metadata<<"],\"ipu\":{\"control\":"<<ee.ipu.control()<<",\"bit_position\":"<<ee.ipu.bit_position<<",\"input\":[";
        for(unsigned n=0;n<ee.ipu.input_count;++n)
            metadata<<(n?",":"")<<'['<<ee.ipu.input[n].low<<','<<ee.ipu.input[n].high<<']';
        metadata<<"],\"position\":"<<ee.ipu.position()<<",\"pending\":"<<ee.ipu.pending
                <<",\"forward\":"<<ee.ipu.forward<<",\"table_bytes\":"<<ee.ipu.table_bytes
                <<",\"output_count\":"<<ee.ipu.output_count<<",\"status\":"<<ee.ipu.status
                <<",\"csc_remaining\":"<<ee.ipu.csc_remaining<<",\"csc_bytes\":"<<ee.ipu.csc_bytes
                <<",\"csc_pixel\":"<<ee.ipu.csc_pixel<<",\"block\":"<<ee.ipu.block
                <<",\"output_cursor\":"<<ee.ipu.output_cursor
                <<",\"result\":"<<ee.ipu.result<<",\"top\":"<<ee.ipu.top<<",\"buffered\":[";
        for(unsigned n=0;n<ee.ipu.buffered_count;++n)
            metadata<<(n?",":"")<<'['<<ee.ipu.buffered[n].low<<','<<ee.ipu.buffered[n].high<<']';
        metadata<<"]}},\n  \"cdvd\": {\"command\": "<<unsigned(iop.cdvd_n_command)
                <<", \"read_pending\": "<<iop.cdvd_read_pending<<", \"lba\": "<<iop.cdvd_lba
                <<", \"sectors\": "<<iop.cdvd_sectors<<", \"irq_pending\": "<<iop.cdvd_irq_pending
                <<", \"interrupt_status\": "<<unsigned(iop.cdvd_interrupt_status)
                <<", \"error\": "<<unsigned(iop.cdvd_error)<<", \"status\": "<<unsigned(iop.cdvd_drive_status)
                <<", \"dma\": ["<<iop.dmac.channel[3][0]<<','<<iop.dmac.channel[3][1]<<','<<iop.dmac.channel[3][2]
                <<"], \"mmio\": [";
        const auto cdvd_trace_count=std::min(iop.cdvd_mmio_trace_cursor,iop.cdvd_mmio_trace.size());
        for(std::size_t n=0;n<cdvd_trace_count;++n) {
            const auto& item=iop.cdvd_mmio_trace[(iop.cdvd_mmio_trace_cursor+iop.cdvd_mmio_trace.size()-cdvd_trace_count+n)%iop.cdvd_mmio_trace.size()];
            metadata<<(n?",":"")<<"{\"pc\":"<<item.pc<<",\"address\":"<<item.address
                    <<",\"value\":"<<item.value<<",\"write\":"<<item.write<<",\"count\":"<<item.count<<'}';
        }
        metadata<<"]},\n  \"threads\": [";
        for(std::size_t n=0;n<iop.threads.size();++n) {
            const auto& t=iop.threads[n];
            metadata<<(n?",":"")<<"{\"id\":"<<n+1<<",\"pc\":"<<t.context.pc
                    <<",\"ready\":"<<t.ready<<",\"priority\":"<<t.priority
                    <<",\"wait_event\":"<<t.wait_event<<",\"wait_bits\":"<<t.wait_event_bits
                    <<",\"wait_mode\":"<<t.wait_event_mode<<",\"ra\":"<<t.context.gpr[31]<<'}';
        }
        metadata<<"],\n  \"events\": [";
        for(std::size_t n=0;n<iop.event_flags.size();++n) {
            const auto& event=iop.event_flags[n];
            const auto signal=n+2<iop.last_event_signal.size()?iop.last_event_signal[n+2]:hg::IopState::EventTrace{};
            metadata<<(n?",":"")<<"{\"id\":"<<n+2<<",\"bits\":"<<event.bits
                    <<",\"last_signal_pc\":"<<signal.pc<<",\"last_signal_ra\":"<<signal.return_pc
                    <<",\"last_signal_bits\":"<<signal.bits<<'}';
        }
        metadata<<"],\n  \"spu2_input\": [";
        for(unsigned core=0;core<2;++core) {
            const auto& input=iop.spu2_input.cores[core];const auto dma=core?0x1f801500u:0x1f8010c0u;
            if(core)metadata<<',';
            metadata<<"{\"core\": "<<core<<", \"enabled\": "<<input.enabled
                    <<", \"read_frame\": "<<input.read_frame<<", \"frames\": "<<input.frames
                    <<", \"ready\": ["<<input.ready[0]<<", "<<input.ready[1]<<"]"
                    <<", \"madr\": "<<iop.dmac.read(dma,4)<<", \"bcr\": "<<iop.dmac.read(dma+4,4)
                    <<", \"chcr\": "<<iop.dmac.read(dma+8,4)<<", \"remaining\": "<<iop.spu2_input_dma[core].remaining<<'}';
        }
        metadata<<"],\n  \"sio2\": {\"requested_control\": "<<iop.sio2_last_control_write<<", \"registers\": [";
        for(std::size_t n=0;n<iop.sio2_registers.size();++n)metadata<<(n?", ":"")<<iop.sio2_registers[n];
        metadata<<"], \"input_fifo\": [";
        for(std::size_t n=0;n<iop.sio2_input_fifo.size();++n)metadata<<(n?", ":"")<<unsigned(iop.sio2_input_fifo[n]);
        metadata<<"]},\n  \"ram_write_watch\": "<<iop.ram_write_trace_address<<", \"ram_write_history\": [";
        const auto writes=std::min(iop.ram_write_trace_cursor,iop.ram_write_trace.size());
        for(std::size_t n=0;n<writes;++n) {
            const auto& r=iop.ram_write_trace[(iop.ram_write_trace_cursor+iop.ram_write_trace.size()-writes+n)%iop.ram_write_trace.size()];
            if(n)metadata<<',';
            metadata<<"{\"pc\":"<<r.pc<<",\"ra\":"<<r.return_pc<<",\"address\":"<<r.address
                    <<",\"before\":"<<r.before<<",\"after\":"<<r.after<<",\"size\":"<<r.size<<",\"thread\":"<<r.thread<<'}';
        }
        metadata<<"],\n  \"trace_watches\": [";
        for(std::size_t watch=0;watch<iop.trace_watch_count;++watch) {
            const auto& selected=iop.trace_watches[watch];
            if(watch)metadata<<',';
            metadata<<"\n    {\"watch_pc\": "<<selected.pc<<", \"history\": [";
            const auto count=std::min(selected.cursor,selected.history.size());
            for(std::size_t n=0;n<count;++n) {
                const auto& record=selected.history[(selected.cursor+selected.history.size()-count+n)%selected.history.size()];
                if(n)metadata<<',';
                metadata<<"{\"pc\": "<<record.pc<<", \"v0\": "<<record.v0<<", \"v1\": "<<record.v1
                        <<", \"a0\": "<<record.a0<<", \"a1\": "<<record.a1<<", \"a2\": "<<record.a2
                        <<", \"a3\": "<<record.a3<<", \"t0\": "<<record.t0<<", \"t4\": "<<record.t4
                        <<", \"s0\": "<<record.s0<<", \"s1\": "<<record.s1<<", \"s2\": "<<record.s2
                        <<", \"s3\": "<<record.s3<<", \"s4\": "<<record.s4<<", \"s5\": "<<record.s5
                        <<", \"s6\": "<<record.s6<<", \"s7\": "<<record.s7<<", \"gp\": "<<record.gp
                        <<", \"sp\": "<<record.sp<<", \"fp\": "<<record.fp<<", \"ra\": "<<record.return_pc<<'}';
            }
            metadata<<"]}";
        }
        metadata<<"\n  ]\n}\n";
        if(!metadata)throw std::runtime_error("cannot write external IOP diagnostic metadata");
        std::cerr<<"External IOP RAM dump: "<<target.string()<<" ("<<iop.ram.size()<<" bytes)\n";
        std::cerr<<"External IOP state metadata: "<<metadata_path.string()<<"\n";
    };
    // If no target is supplied, use the already-proven MODLOAD error mapping.
    // Watches remain diagnostic-only and are retained across an IOP reboot.
    if(!watch_iop_pc_count && stop_on_loadfile_error)watch_iop_pcs[watch_iop_pc_count++]=0x000ab294u;
    if(!watch_ee_pc_count && stop_on_loadfile_error) {
        // Keep the synchronous SIFRPC lifecycle in one failing-startup capture:
        // call construction, completion dispatch, semaphore wait and wake path.
        for(const auto address:{0x002700e8u,0x0026fa60u,0x0026faf8u,0x0026fb00u,0x002702a4u})
            watch_ee_pcs[watch_ee_pc_count++]=address;
    }
    for(std::size_t item=0;item<watch_iop_pc_count;++item)
        if(!iop.add_trace_watch(watch_iop_pcs[item]))throw std::runtime_error("invalid IOP trace watch configuration");
    if(!spu2_sync_log.empty()) {
        // HG-DIAG-013: the movie caller keeps its output frame live across
        // 0x1d3210. At 0x242ad8 both caller-local results have just been
        // loaded into v0/v1, avoiding any later reuse of the stack frame.
        // 0x1d3134 is the normal 0x1d30c8 formula point after both original
        // latency helpers have returned: s0=0x1cd0e0 result, v0=0x1d3c58
        // result, s1=wrapper+0x2c and s2=the audio object.
        // 0x1e1d14 is the callback-A jalr itself.  At that point a0 still
        // contains the callback object and v0 contains the virtual target;
        // the callee is free to clobber a0 before control returns at 0x1e1d1c.
        for(const auto address:{0x00242ad8u,0x001d3134u,0x001e1d14u,
                                0x001e4010u,0x001e41b0u,0x001e4318u,
                                0x001e32b8u,0x001e34a0u,0x001e3660u}) {
            bool duplicate=false;
            for(std::size_t item=0;item<watch_ee_pc_count;++item)duplicate|=watch_ee_pcs[item]==address;
            if(!duplicate) {
                if(watch_ee_pc_count==watch_ee_pcs.size())throw std::runtime_error("no EE trace-watch slot available for SPU2 sync log");
                watch_ee_pcs[watch_ee_pc_count++]=address;
            }
        }
        // HG-DIAG-013: the verified live movie callback-A object is 0x3d4040
        // and this deterministic replay's active list node is 0x3ccc60;
        // callback B reads 0x3d0128+0x0c.
        // Preserve the default SIF semaphore watch in slot 0 and observe only
        // these three source words while the explicit sync log is enabled.
        ee.ram_write_trace_addresses[1]=0x003d4058u;
        ee.ram_write_trace_addresses[2]=0x003ccc6cu;
        ee.ram_write_trace_addresses[3]=0x003d0134u;
    }
    for(std::size_t item=0;item<watch_ee_pc_count;++item)
        if(!ee.add_trace_watch(watch_ee_pcs[item]))throw std::runtime_error("invalid EE trace watch configuration");
    hg::IopRebootRequest reboot;
    auto configure_game_modules=[](hg::IopState& state) {
        state.configure_static_iop_module("MODULES/MCSERV.IRX",hg::compiled_iop_module_base("mcserv"),
                                       hg::compiled_iop_module_memory_size("mcserv"),hg::compiled_iop_module_allocation_prefix("mcserv"));
        state.configure_static_iop_module("MODULES/MCMAN.IRX",hg::compiled_iop_module_base("mcman"),
                                       hg::compiled_iop_module_memory_size("mcman"),hg::compiled_iop_module_allocation_prefix("mcman"));
        state.configure_static_iop_module("MODULES/SIO2MAN.IRX",hg::compiled_iop_module_base("sio2man"),
                                       hg::compiled_iop_module_memory_size("sio2man"),hg::compiled_iop_module_allocation_prefix("sio2man"));
        state.configure_static_iop_module("MODULES/SIO2D.IRX",hg::compiled_iop_module_base("sio2d"),
                                       hg::compiled_iop_module_memory_size("sio2d"),hg::compiled_iop_module_allocation_prefix("sio2d"));
        state.configure_static_iop_module("MODULES/DBCMAN.IRX",hg::compiled_iop_module_base("dbcman"),
                                       hg::compiled_iop_module_memory_size("dbcman"),hg::compiled_iop_module_allocation_prefix("dbcman"));
        state.configure_static_iop_module("MODULES/LIBSD.IRX",hg::compiled_iop_module_base("libsd"),
                                       hg::compiled_iop_module_memory_size("libsd"),hg::compiled_iop_module_allocation_prefix("libsd"));
        state.configure_static_iop_module("MODULES/CRI_ADXI.IRX",hg::compiled_iop_module_base("cri_adxi"),
                                       hg::compiled_iop_module_memory_size("cri_adxi"),hg::compiled_iop_module_allocation_prefix("cri_adxi"));
        state.configure_static_iop_module("MODULES/MODHSYN.IRX",hg::compiled_iop_module_base("modhsyn"),
                                       hg::compiled_iop_module_memory_size("modhsyn"),hg::compiled_iop_module_allocation_prefix("modhsyn"));
        state.configure_static_iop_module("MODULES/MODMIDI.IRX",hg::compiled_iop_module_base("modmidi"),
                                       hg::compiled_iop_module_memory_size("modmidi"),hg::compiled_iop_module_allocation_prefix("modmidi"));
        state.configure_static_iop_module("MODULES/MODMSIN.IRX",hg::compiled_iop_module_base("modmsin"),
                                       hg::compiled_iop_module_memory_size("modmsin"),hg::compiled_iop_module_allocation_prefix("modmsin"));
        state.configure_static_iop_module("MODULES/SNDDRV.IRX",hg::compiled_iop_module_base("snddrv"),
                                       hg::compiled_iop_module_memory_size("snddrv"),hg::compiled_iop_module_allocation_prefix("snddrv"));
        state.configure_static_iop_module("MODULES/DS2O_S1.IRX",hg::compiled_iop_module_base("ds2o_s1"),
                                       hg::compiled_iop_module_memory_size("ds2o_s1"),hg::compiled_iop_module_allocation_prefix("ds2o_s1"));
    };
    try {
        if(!spu2_sync_log.empty() && !hg::compiled_ee_instruction_trace())
            throw std::runtime_error("SPU2 sync log requires an EE instruction-history build");
        if(!spu2_input_wav.empty())spu2_wav=std::make_unique<hg::DiagnosticWavWriter>(spu2_input_wav);
        if(!spu2_mix_wav.empty())spu2_mix_writer=std::make_unique<hg::DiagnosticWavWriter>(spu2_mix_wav);
        if(spu2_output_core>=0)spu2_output=std::make_unique<hg::HostAudioOutput>();
        std::ofstream spu2_sync,spu2_sync_events;
        std::ofstream host_input_log;
        if(!input_log.empty()) {
            host_input_log.open(input_log,std::ios::binary|std::ios::trunc);
            if(!host_input_log)throw std::runtime_error("cannot open input log");
        }
        std::size_t spu2_sync_watch_cursor=0,spu2_sync_formula_cursor=0,spu2_sync_callback_a_cursor=0;
        std::size_t spu2_sync_ram_write_cursor=0;
        bool spu2_sync_dma_state_valid=false;
        std::uint64_t spu2_sync_dma_count=0,spu2_sync_dma_bytes=0;
        std::uint32_t spu2_sync_dma_madr=0,spu2_sync_dma_bcr=0,spu2_sync_dma_chcr=0,spu2_sync_dma_remaining=0;
        constexpr std::array<std::uint32_t,6> spu2_sync_method_pcs{
            0x001e4010u,0x001e41b0u,0x001e4318u,0x001e32b8u,0x001e34a0u,0x001e3660u};
        std::array<std::size_t,spu2_sync_method_pcs.size()> spu2_sync_method_cursors{};
        if(!spu2_sync_log.empty()) {
            spu2_sync.open(spu2_sync_log,std::ios::binary|std::ios::trunc);
            if(!spu2_sync)throw std::runtime_error("cannot open SPU2 sync log");
            const auto events_path=std::filesystem::path(spu2_sync_log.string()+".events.csv");
            spu2_sync_events.open(events_path,std::ios::binary|std::ios::trunc);
            if(!spu2_sync_events)throw std::runtime_error("cannot open SPU2 sync event log");
            spu2_sync_events<<"kind,slice,guest_us,pc,ra,a0,a1,a2,a3,watched_word,address,before,after,size,thread,spu2_dma_count,spu2_dma_bytes,core0_frames,core0_phase,core0_read_frame,core0_ready0,core0_ready1,core0_write_half,core0_madr,core0_bcr,core0_chcr,core0_remaining\n";
            spu2_sync<<"slice,guest_us,guest_position,guest_output_34,formula_path,formula_wrapper_2c,latency_1cd0e0,latency_1d3c58,callback_1e1cd8_valid,callback_1e1cd8_raw,callback_1e1cd8_object,callback_1e1cd8_target,callback_a_state4,callback_a_list_head,callback_a_list_count,callback_a_list_sum,callback_a_list_matches_raw,callback_a_list_truncated,callback_a_node0,callback_a_node0_next,callback_a_node0_value,callback_a_node1,callback_a_node1_next,callback_a_node1_value,callback_a_node2,callback_a_node2_next,callback_a_node2_value,callback_a_node3,callback_a_node3_next,callback_a_node3_value,callback_a_root_writer_valid,callback_a_root_writer_pc,callback_a_root_writer_ra,callback_a_root_writer_before,callback_a_root_writer_after,callback_a_node_writer_valid,callback_a_node_writer_pc,callback_a_node_writer_ra,callback_a_node_writer_before,callback_a_node_writer_after,callback_1d3c58_valid,callback_1d3c58_raw,callback_1d3c58_object,callback_1d3c58_target,callback_b_source_word,callback_b_writer_valid,callback_b_writer_pc,callback_b_writer_ra,callback_b_writer_before,callback_b_writer_after,state,object,wrapper,inner,wrapper_2c,inner_14_getter,inner_18,inner_1c_u16,offset_88,offset_a4,offset_a8,display_count,core0_frames,core0_phase,core0_read_frame,core0_ready0,core0_ready1,core0_write_half,core0_madr,core0_bcr,core0_chcr,core0_remaining,core1_frames,core1_phase,core1_read_frame,core1_ready0,core1_ready1,core1_write_half,core1_madr,core1_bcr,core1_chcr,core1_remaining\n";
        }
        std::array<std::uint8_t,2064> supplied_toc{};
        bool has_supplied_toc=false;
        if(!toc_record.empty()) {
            std::ifstream input(toc_record,std::ios::binary);
            input.seekg(0,std::ios::end);const auto size=input?input.tellg():std::streampos(-1);
            if(size!=std::streampos(supplied_toc.size()))throw std::runtime_error("CDVD TOC record must be exactly 2064 bytes");
            input.seekg(0);input.read(reinterpret_cast<char*>(supplied_toc.data()),supplied_toc.size());
            if(!input)throw std::runtime_error("cannot read supplied CDVD TOC record");
            has_supplied_toc=true;
        }
        hg::load_elf_file(ee,elf,hg::compiled_image_sha256());hg::configure_compiled_image(ee);
        if(!vu1_capture.prefix.empty()) {
            vu1_capture.execute=ee.vif1.vu1_executor;
            if(!vu1_capture.execute)throw std::runtime_error("VU1 capture requires a compiled AOT program");
            ee.vif1.vu1_executor=&observe_vu1;
        }
        ee.protect_kernel_memory=true;ee.boot.enabled=true;
        hg::load_compiled_iop(iop,modules);
        iop.configure_cdvd_image(disc);
        configure_game_modules(iop);
        // Native RTC snapshot in UTC; original command supplies its BCD result.
        const auto now=std::time(nullptr);std::tm utc{};
#ifdef _WIN32
        if(gmtime_s(&utc,&now))throw std::runtime_error("cannot initialize native RTC");
#else
        if(!gmtime_r(&now,&utc))throw std::runtime_error("cannot initialize native RTC");
#endif
        auto bcd=[](int n){return std::uint8_t((n/10)*16+n%10);};
        iop.rtc_bcd={0,bcd(utc.tm_sec),bcd(utc.tm_min),bcd(utc.tm_hour),0,bcd(utc.tm_mday),bcd(utc.tm_mon+1),bcd((utc.tm_year+1900)%100)};
        iop.rtc_configured=true;
        const auto loadcore=hg::compiled_iop_module_entry("rom_loadcore");
        auto initialize_core=[&]() {
        // The real IOP boot initializes SYSMEM before LOADCORE.  The AOT image
        // is already laid out in IOP RAM, so reserve that occupied range from
        // the original allocator before any module can request heap storage.
        invoke_iop(iop,hg::compiled_iop_module_entry("rom_sysmem"),static_cast<std::uint32_t>(iop.ram.size()));
        const auto allocator=iop.load(0x000950c4u,4,false);
        if(!allocator || allocator+0x100u>iop.ram.size())
            throw hg::IopFault(iop.pc,"ROM SYSMEM did not initialize its allocator");
        const auto heap_floor=allocator+0x100u;
        const auto high_water=(hg::compiled_iop_high_water_mark()+0xffu)&~0xffu;
        if(high_water<=heap_floor || high_water>iop.ram.size())
            throw hg::IopFault(iop.pc,"static IOP image has invalid SYSMEM reservation range");
        const auto reservation_size=high_water-heap_floor;
        invoke_iop(iop,hg::compiled_iop_module_export("rom_sysmem","sysmem",4),0,reservation_size,0);
        if(iop.r(2)!=heap_floor)
            throw hg::IopFault(iop.pc,"ROM SYSMEM could not reserve the static IOP image");
        // Keep the bootstrap, interrupt and nested allocator-call stacks out
        // of the same original allocator used by SIF heap clients.
        const auto sysmem_alloc=hg::compiled_iop_module_export("rom_sysmem","sysmem",4);
        const auto sysmem_free=hg::compiled_iop_module_export("rom_sysmem","sysmem",5);
        invoke_iop(iop,sysmem_alloc,1,0x20000,0);
        const auto reserved=iop.r(2);
        if(!reserved || reserved>0x1ec000 || std::uint64_t(reserved)+0x20000<0x1f4000)
            throw hg::IopFault(iop.pc,"ROM SYSMEM could not reserve native call stacks");
        iop.system_memory_service=[sysmem_alloc,sysmem_free](hg::IopState& state,unsigned ordinal,
            std::uint32_t a0,std::uint32_t a1,std::uint32_t a2) {
            if(ordinal!=4 && ordinal!=5)throw hg::IopFault(state.pc,"unsupported original SYSMEM service");
            const auto saved=state.save_cpu();
            // Execute only the already compiled original allocator. Its
            // synchronous body does not run the cooperative scheduler or DMA.
            invoke_iop(state,ordinal==4?sysmem_alloc:sysmem_free,a0,a1,a2,0x1f4000);
            const auto result=state.r(2);state.restore_cpu(saved);return result;
        };
        iop.pc=loadcore+0xcc;hg::run_iop(iop,4);
        for(const auto& pair:{std::pair<const char*,const char*>{"rom_sifman","loadcore"},
                              {"rom_sifcmd","loadcore"},{"rom_sifcmd","sifman"},{"rom_sifcmd","thbase"},{"rom_sifcmd","thevent"}}) {
            const auto library=std::string(pair.second);
            const auto provider=library=="loadcore"?"rom_loadcore":(library=="thbase" || library=="thevent")?"rom_threadman":"rom_sifman";
            invoke_iop(iop,loadcore+0x11d8,hg::compiled_iop_table(pair.first,pair.second,true),
                       hg::compiled_iop_table(provider,pair.second,false));
        }
        for(const char* library:{"loadcore","sysmem","sysclib","stdio"}) {
            const auto provider=std::string(library)=="loadcore"?"rom_loadcore":std::string(library)=="sysmem"?"rom_sysmem":std::string(library)=="sysclib"?"rom_sysclib":"rom_stdio";
            invoke_iop(iop,loadcore+0x11d8,hg::compiled_iop_table("rom_threadman",library,true),
                       hg::compiled_iop_table(provider,library,false));
        }
        invoke_iop(iop,loadcore+0x11d8,hg::compiled_iop_table("rom_threadman","timrman",true),
                   hg::compiled_iop_table("rom_timemani","timrman",false));
        invoke_iop(iop,hg::compiled_iop_module_entry("rom_threadman"));
        invoke_iop(iop,hg::compiled_iop_module_entry("rom_sifman"));
        begin_iop(iop,hg::compiled_iop_module_entry("rom_sifcmd"));
        };
        initialize_core();
        std::uint64_t transfers=0;
        bool transport_ready=false,command_started=false,fileio_started=false,cdvd_started=false;
        unsigned reboot_count=0;
        bool reboot_rpc_cleared=false;
        bool stopped_on_loadfile_error=false;
        std::uint32_t stopped_loadfile_result=0;
        std::array<hg::IopState::PcTrace,64> pre_sif0_irq_trace{};
        std::size_t pre_sif0_irq_trace_cursor=0;
        struct EeRpcCompletionSnapshot {
            std::uint32_t packet=0,client=0;
            bool packet_valid=false,client_valid=false;
            std::array<std::uint32_t,11> packet_words{};
            std::array<std::uint32_t,14> client_words{};
        };
        std::array<EeRpcCompletionSnapshot,16> ee_rpc_completion_snapshots{};
        std::size_t ee_rpc_completion_snapshot_cursor=0,ee_rpc_completion_watch_cursor=0;
        struct Sif0CompletionTransferSnapshot {
            bool valid=false,ee_destination_valid=false;
            std::array<std::uint32_t,4> tag{};
            std::array<std::uint32_t,16> payload{};
            unsigned payload_words=0,total_words=0;
            std::uint32_t ee_chcr=0,ee_madr=0,ee_qwc=0,ee_tadr=0,ee_status=0,ee_mask=0;
            std::uint64_t ee_completion_count=0;
            std::array<std::uint32_t,16> ee_destination_words{};
        };
        Sif0CompletionTransferSnapshot snddrv_completion_transfer{};
        hg::IopDmac::Sif0Record snddrv_result_transfer{},snddrv_packet_transfer{};
        bool snddrv_result_transfer_valid=false,snddrv_packet_transfer_valid=false;
        hg::IopDmac::Sif0Record snddrv_rpc_result_transfer{},snddrv_rpc_command_transfer{};
        hg::IopDmac::Sif1Record snddrv_rpc_request_transfer{};
        bool snddrv_rpc_result_transfer_valid=false,snddrv_rpc_command_transfer_valid=false;
        bool snddrv_rpc_request_transfer_valid=false;
        struct EeSnddrvReceiveSnapshot {
            bool valid=false;
            std::array<std::uint32_t,16> command{};
            std::uint32_t chcr=0,madr=0,qwc=0,tadr=0,status=0,mask=0;
            std::uint64_t completion_count=0;
        };
        EeSnddrvReceiveSnapshot snddrv_ee_post_receive{},snddrv_ee_callback_entry{};
        struct TargetSif0RecordSnapshot {hg::IopDmac::Sif0Record record{};std::uint32_t ee_semaphore=0;};
        std::array<TargetSif0RecordSnapshot,8> snddrv_submit_records{};
        std::size_t snddrv_submit_record_count=0,snddrv_submit_history_cursor=0;
        bool snddrv_submit_seen=false,snddrv_submit_capture_active=false;
        std::uint32_t snddrv_submit_entry_semaphore=0;
        std::array<std::uint32_t,4> snddrv_submit_channel9{};
        struct SubmitTimingSnapshot {std::uint32_t submission_id=0,ee_semaphore=0;std::uint64_t time=0;};
        std::array<SubmitTimingSnapshot,32> sifman_submit_timings{};
        std::size_t sifman_submit_timing_count=0;
        hg::DiagnosticReturnRefresh<32> sifman_return_refresh;
        struct SendReturnTimingSnapshot {hg::IopState::PcTrace trace{};std::uint32_t ee_semaphore=0;std::uint64_t time=0;};
        std::array<SendReturnTimingSnapshot,16> send_return_timings{};
        std::size_t send_return_timing_count=0;
        auto capture_ee_words=[&](std::uint32_t address,auto& words) {
            if(address%4 || std::uint64_t(address)+words.size()*4>ee.ram.size())return false;
            for(std::size_t word=0;word<words.size();++word) {
                const auto offset=address+std::uint32_t(word*4);
                words[word]=std::uint32_t(ee.ram[offset])|(std::uint32_t(ee.ram[offset+1])<<8)|
                            (std::uint32_t(ee.ram[offset+2])<<16)|(std::uint32_t(ee.ram[offset+3])<<24);
            }
            return true;
        };
        // Realtime needs the independent frame probe, not per-slice phase sampling.
        // Keep detailed profiling for unpaced attribution runs only.
        hg::DiagnosticProfile host_profile(profile_enabled && !realtime);
        hg::DiagnosticDiscProfile disc_profile(profile_enabled);
        hg::ExecutionClock execution_clock(issue_slot_clock);
        bool preview_due=false;
        auto next_preview=std::chrono::steady_clock::now();
        bool csc_active=false;
        unsigned csc_sequence=0;
        hg::HostDeadlineWaiter host_waiter(realtime);
        const auto playback_start=std::chrono::steady_clock::now();
        // HG-DIAG-115: optional host stack sampler over a checkpoint window.
        hg::host_sampler_start_if_requested();
        hg::host_sampler_register_current("EE");
        const auto sample_env=[](const char* name)->std::uint64_t {
            const char* value=std::getenv(name);return value?std::strtoull(value,nullptr,10):0;
        };
        const auto sample_from=sample_env("HG_SAMPLE_FROM"),sample_to=sample_env("HG_SAMPLE_TO");
        if(hg::host_sampler_enabled()&&!sample_from)hg::host_sampler_set_active(true);
        // Diagnostic-only qualification of original display-synchronized state.
        // Original 0x1bef84 copies the cause-2 counter into object+0x1c only
        // after 0x1beef0 observes enough display edges.  This remains a frame
        // candidate until dynamic writer/caller/render relationships are proven.
        struct OriginalFrameCandidateProbe {
            bool enabled=false,armed=false,have_boundary=false,have_gs=false,report_started=false;
            std::uint32_t object=0,watched_word=0,active_caller=0,first_caller=0,last_caller=0,last_seen_object=0;
            std::uint64_t trace_cursor=0,writes=0,lost_trace=0,unexpected_writers=0,invalid_stack=0;
            std::uint64_t caller_mismatches=0,object_mismatches=0,counter_delta_two=0,counter_delta_other=0;
            std::uint64_t vblank_delta_two=0,vblank_delta_other=0,first_guest_us=0,last_guest_us=0;
            std::uint64_t first_vblank=0,last_vblank=0,report_writes=0;
            std::uint64_t dispfb_changes=0,display_changes=0,raster_advanced_boundaries=0,retired_advanced_boundaries=0;
            std::uint64_t raster_delta=0,retired_delta=0,raster_resets=0,retired_resets=0,last_rasterized=0,last_retired=0;
            std::array<std::uint64_t,2> last_dispfb{},last_display{};
            std::chrono::steady_clock::time_point first_host{},last_host{},report_host{};
        } frame_probe;
        frame_probe.enabled=(profile_enabled||frame_rate_probe) && spu2_sync_log.empty() && ee.ram_write_trace_addresses[3]==0;
        const auto observe_original_frame_candidate=[&]() {
            if(!frame_probe.enabled)return;
            // s0 and the saved caller at sp+0x10 belong to 0x1beef0 while PC
            // remains inside that original function.  Use them only as provenance.
            if(ee.pc>=0x001beef0u && ee.pc<0x001bf078u) {
                const auto object=std::uint32_t(ee.r(16));
                if(object && std::uint64_t(hg::physical_address(object))+0x20<=ee.ram.size()) {
                    if(!frame_probe.armed) {
                        frame_probe.object=object;
                        frame_probe.last_seen_object=object;
                        frame_probe.watched_word=hg::physical_address(object+0x1cu);
                        ee.ram_write_trace_addresses[3]=frame_probe.watched_word;
                        frame_probe.trace_cursor=ee.ram_write_trace_cursor;
                        frame_probe.armed=true;
                    } else if(object!=frame_probe.last_seen_object) {
                        ++frame_probe.object_mismatches;frame_probe.last_seen_object=object;
                    }
                    const auto sp=std::uint32_t(ee.r(29));
                    if((sp&7u)==0 && std::uint64_t(hg::physical_address(sp))+24<=ee.ram.size())
                        frame_probe.active_caller=std::uint32_t(ee.load(sp+16,8,false));
                    else ++frame_probe.invalid_stack;
                }
            }
            if(!frame_probe.armed)return;
            const auto first_available=ee.ram_write_trace_cursor>ee.ram_write_trace.size()?
                ee.ram_write_trace_cursor-ee.ram_write_trace.size():0;
            if(frame_probe.trace_cursor<first_available) {
                frame_probe.lost_trace+=first_available-frame_probe.trace_cursor;
                frame_probe.trace_cursor=first_available;
            }
            while(frame_probe.trace_cursor<ee.ram_write_trace_cursor) {
                const auto& write=ee.ram_write_trace[frame_probe.trace_cursor%ee.ram_write_trace.size()];
                ++frame_probe.trace_cursor;
                if(write.address>frame_probe.watched_word+3u ||
                   std::uint64_t(write.address)+write.size<=frame_probe.watched_word)continue;
                ++frame_probe.writes;
                if(write.pc!=0x001bef84u)++frame_probe.unexpected_writers;
                const auto counter_delta=std::uint32_t(write.after-write.before);
                if(counter_delta==2)++frame_probe.counter_delta_two;else ++frame_probe.counter_delta_other;
                const auto now=std::chrono::steady_clock::now();
                const auto vblank=iop.vblank_start_count;
                if(!frame_probe.have_boundary) {
                    frame_probe.have_boundary=true;frame_probe.first_host=now;
                    frame_probe.first_guest_us=iop.virtual_time_us;frame_probe.first_vblank=vblank;
                    frame_probe.first_caller=frame_probe.active_caller;
                } else {
                    const auto vblank_delta=vblank-frame_probe.last_vblank;
                    if(vblank_delta==2)++frame_probe.vblank_delta_two;else ++frame_probe.vblank_delta_other;
                    if(frame_probe.first_caller && frame_probe.active_caller &&
                       frame_probe.active_caller!=frame_probe.first_caller)++frame_probe.caller_mismatches;
                }
                frame_probe.last_host=now;frame_probe.last_guest_us=iop.virtual_time_us;frame_probe.last_vblank=vblank;
                frame_probe.last_caller=frame_probe.active_caller;
                device_owner.post_or_run([&] {
                    const auto rasterized=ee.gs.rasterized_draw_count,retired=ee.gs.retired_draw_count;
                    if(frame_probe.have_gs) {
                        if(ee.gs.privileged_dispfb!=frame_probe.last_dispfb)++frame_probe.dispfb_changes;
                        if(ee.gs.privileged_display!=frame_probe.last_display)++frame_probe.display_changes;
                        if(rasterized>=frame_probe.last_rasterized) {
                            const auto delta=rasterized-frame_probe.last_rasterized;
                            frame_probe.raster_delta+=delta;if(delta)++frame_probe.raster_advanced_boundaries;
                        } else ++frame_probe.raster_resets;
                        if(retired>=frame_probe.last_retired) {
                            const auto delta=retired-frame_probe.last_retired;
                            frame_probe.retired_delta+=delta;if(delta)++frame_probe.retired_advanced_boundaries;
                        } else ++frame_probe.retired_resets;
                    }
                    frame_probe.last_dispfb=ee.gs.privileged_dispfb;frame_probe.last_display=ee.gs.privileged_display;
                    frame_probe.last_rasterized=rasterized;frame_probe.last_retired=retired;frame_probe.have_gs=true;
                });
            }
        };
        const auto report_original_frame_candidate=[&](const char* stage) {
            if(!frame_probe.enabled)return;
            const auto now=std::chrono::steady_clock::now();
            const auto interval_start=frame_probe.report_started?frame_probe.report_host:playback_start;
            const auto interval_seconds=std::chrono::duration<double>(now-interval_start).count();
            const auto interval_writes=frame_probe.writes-frame_probe.report_writes;
            const auto host_span=frame_probe.have_boundary?
                std::chrono::duration<double>(frame_probe.last_host-frame_probe.first_host).count():0.0;
            const auto guest_span=frame_probe.last_guest_us>=frame_probe.first_guest_us?
                double(frame_probe.last_guest_us-frame_probe.first_guest_us)/1000000.0:0.0;
            const auto intervals=frame_probe.writes>1?frame_probe.writes-1:0;
            std::ostringstream line;
            line<<"Original frame candidate stage="<<stage
                <<" writes="<<frame_probe.writes<<" interval_writes="<<interval_writes
                <<" interval_host_seconds="<<interval_seconds
                <<" interval_hz="<<(interval_seconds?double(interval_writes)/interval_seconds:0.0)
                <<" host_span_hz="<<(host_span?double(intervals)/host_span:0.0)
                <<" guest_span_hz="<<(guest_span?double(intervals)/guest_span:0.0)
                <<" writer_1bef84="<<(frame_probe.writes-frame_probe.unexpected_writers)
                <<" unexpected_writers="<<frame_probe.unexpected_writers<<" lost_trace="<<frame_probe.lost_trace
                <<" counter_delta2="<<frame_probe.counter_delta_two<<" counter_other="<<frame_probe.counter_delta_other
                <<" vblank_delta2="<<frame_probe.vblank_delta_two<<" vblank_other="<<frame_probe.vblank_delta_other
                <<" caller_mismatches="<<frame_probe.caller_mismatches<<" object_mismatches="<<frame_probe.object_mismatches
                <<" invalid_stack="<<frame_probe.invalid_stack
                <<" guest_first_us="<<frame_probe.first_guest_us<<" guest_last_us="<<frame_probe.last_guest_us
                <<" vblank_first="<<frame_probe.first_vblank<<" vblank_last="<<frame_probe.last_vblank
                <<" object=0x"<<std::hex<<frame_probe.object<<" word=0x"<<frame_probe.watched_word
                <<" caller=0x"<<frame_probe.first_caller<<" last_caller=0x"<<frame_probe.last_caller<<std::dec;
            // Display/raster fields belong to the device thread when one runs.
            device_owner.post_or_run([&frame_probe,&ee,text=std::move(line).str()] {
            std::cerr<<text
                <<" dispfb_changes="<<frame_probe.dispfb_changes<<" display_changes="<<frame_probe.display_changes
                <<" raster_advanced_boundaries="<<frame_probe.raster_advanced_boundaries
                <<" retired_advanced_boundaries="<<frame_probe.retired_advanced_boundaries
                <<" raster_delta="<<frame_probe.raster_delta<<" retired_delta="<<frame_probe.retired_delta
                <<" raster_resets="<<frame_probe.raster_resets<<" retired_resets="<<frame_probe.retired_resets
                <<std::hex<<" pmode=0x"<<ee.gs.privileged_pmode
                <<" dispfb=0x"<<ee.gs.privileged_dispfb[0]<<",0x"<<ee.gs.privileged_dispfb[1]
                <<" display=0x"<<ee.gs.privileged_display[0]<<",0x"<<ee.gs.privileged_display[1]<<std::dec<<'\n';
            });
            frame_probe.report_started=true;frame_probe.report_host=now;frame_probe.report_writes=frame_probe.writes;
        };
        hg::HostPlaybackPacer host_pacer(playback_start);
        std::uint64_t pacing_time_us=0,pacing_waits=0,pacing_requested_us=0;
        std::uint64_t pacing_actual_us=0,pacing_max_late_us=0;
        std::uint64_t raster_host_ns=0,raster_profile_draws=0;
        std::size_t input_event_cursor=0;
        fast_replay_active=fast_replay&&!input_events.empty();
        if(!memory_card_file.empty()) {
            iop.memory_card=std::make_shared<hg::VirtualMemoryCard>();
            iop.memory_card->open(memory_card_file);
            iop.memory_card->present=!memory_card_insert_at&&!(memory_card_after_replay&&!input_events.empty());
            std::cerr<<"Virtual memory card "<<(iop.memory_card->present?"inserted":"waiting for replay end")
                     <<": "<<memory_card_file.string()<<'\n';
        }
        // Host-only checkpoint history: every input command applied, at its slice.
        // Replaying it from boot reproduces the guest deterministically.
        std::vector<std::pair<std::uint64_t,std::string>> applied_inputs;
        const auto record_input=[&](std::uint64_t at,const std::string& command) {
            if(checkpoint_file.empty())return;
            if(!applied_inputs.empty()&&applied_inputs.back().first==at)applied_inputs.back().second=command;
            else applied_inputs.emplace_back(at,command);
        };
        const auto write_checkpoint=[&](std::uint64_t at) {
            // The final line re-applies the current input at `at`, so a
            // --input-replay-wait-live resume pauses exactly at this slice.
            auto lines=applied_inputs;
            const auto current=lines.empty()?hg::format_diagnostic_pad(hg::HostInput{}):lines.back().second;
            if(lines.empty()||lines.back().first<at)lines.emplace_back(at,current);
            const auto pending=std::filesystem::path(checkpoint_file.string()+".next");
            {
                std::ofstream out(pending,std::ios::binary|std::ios::trunc);
                for(const auto& [stamp,command]:lines)out<<stamp<<' '<<command<<'\n';
                if(!out)throw std::runtime_error("cannot write checkpoint");
            }
            std::error_code error;
            std::filesystem::rename(pending,checkpoint_file,error);
            if(error){std::filesystem::remove(checkpoint_file,error);std::filesystem::rename(pending,checkpoint_file);}
            std::cerr<<"Checkpoint saved at slice "<<at<<" ("<<lines.size()<<" input events): "<<checkpoint_file.string()<<'\n';
            if(host&&host->notice)host->notice("Checkpoint saved");
        };
        const auto apply_input=[&](const std::string& command,std::uint64_t slice) {
            if(command.compare(0,4,"pad:")==0) {
                hg::HostInput pad;
                if(!hg::parse_diagnostic_pad(command,pad))throw std::runtime_error("invalid diagnostic pad snapshot");
                iop.sio2_buttons[0]=pad.buttons;
                iop.sio2_controllers[0].axes=pad.axes;
                iop.sio2_controllers[0].pressures=pad.pressures;
                std::cerr<<"Diagnostic input "<<command<<" at slice "<<slice<<'\n';
                return;
            }
            const std::uint16_t buttons=command=="left"?0xff7f:command=="cross"?0xbfff:command=="start"?0xfff7:0xffff;
            if(iop.sio2_buttons[0]!=buttons) {
                iop.sio2_buttons[0]=buttons;
                iop.sio2_controllers[0].pressures[1]=command=="left"?255:0;
                iop.sio2_controllers[0].pressures[6]=command=="cross"?255:0;
                std::cerr<<"Diagnostic input "<<command<<" at slice "<<slice<<'\n';
            }
        };
        auto next_input_slice=input_events.empty()?~std::uint64_t{0}:input_events[0].slice;
        // HG-DIAG-123: the autopilot owns the controller after any replayed events, until
        // its script ends; its snapshots are recorded like live input.
        std::unique_ptr<hg::Autopilot> autopilot;
        if(!autopilot_file.empty()){autopilot=std::make_unique<hg::Autopilot>();autopilot->load(autopilot_file.string());
            std::cerr<<"HG-DIAG-123 autopilot script loaded: "<<autopilot_file.string()<<'\n';}
        hg::HostInput autopilot_last;bool autopilot_applied=false;
        std::cerr<<"Execution clock profile: "<<(issue_slot_clock?"experimental issue-slots":"legacy diagnostic")<<'\n';
        std::cerr<<"Host playback: "<<(realtime?"real time (10ms checks, 100ms catch-up bound)":"unlimited")<<'\n';
        if(realtime)std::cerr<<"Host wait backend: "<<host_waiter.backend()<<'\n';
        // The device thread needs the GS OpenGL context when accelerators exist.
        // Diagnostics that observe device state per call keep the synchronous path.
        // HG_SYNC_DEVICES=1 keeps the synchronous path for comparison runs whose
        // command line is fixed (the replay verifier); --sync-devices elsewhere.
        if(const char* value=std::getenv("HG_SYNC_DEVICES"))sync_devices=sync_devices||std::string(value)=="1";
        if(!sync_devices && vu1_capture.prefix.empty() && !profile_gs && !(profile_enabled && !realtime) &&
           (!hg::gs_sprite_accelerator || (host && host->gs_context))) {
            std::function<void(bool)> context;
            if(hg::gs_sprite_accelerator){host->gs_context(false);context=host->gs_context;device_owner.moved_context=true;}
            device_owner.link=hg::start_device_thread(ee.vif1,ee.gif,ee.gs,std::move(context));
            ee.device=device_owner.link.get();
            ee.device->imr=ee.gs.privileged_imr;ee.device->csr_events=ee.gs.privileged_csr_events;
            ee.device->busdir=ee.gs.privileged_busdir;
            std::cerr<<"Host device thread: VIF1/VU1/GIF/GS\n";
        }
        for(std::uint64_t slice=0;slice<slices;++slice) {
            // HG-DIAG-005: GS reset restores device defaults. Re-arm this
            // host-only observer at the next quantum; counters are since reset.
            if(profile_gs)ee.gs.profile_raster=true;
            // HG-DIAG-006: a visible deterministic route owns the controller
            // until its final scheduled snapshot. Live keyboard/gamepad input
            // resumes on the next poll, allowing an exact replay to hand control
            // back to the player without restarting or overwriting the route.
            for(auto& w:word_inputs) {   // HG-DIAG-125
                if(w.done||w.address+4>ee.ram.size())continue;
                const std::uint32_t word=std::uint32_t(ee.ram[w.address])|std::uint32_t(ee.ram[w.address+1])<<8|
                    std::uint32_t(ee.ram[w.address+2])<<16|std::uint32_t(ee.ram[w.address+3])<<24;
                if(word!=w.value)continue;
                w.done=true;apply_input(w.pad,slice);record_input(slice,w.pad);
            }
            // HG-DIAG-124: end the run (with its normal dumps) once an EE RAM word equals a
            // value, e.g. a game frame counter, so state is compared at the same game frame.
            // Checked every slice (~300 cycles) and the stop PC is reported, so the oracle
            // can be run to the same instruction.
            if(stop_word&&stop_word_address+4<=ee.ram.size()) {
                const std::uint32_t word=std::uint32_t(ee.ram[stop_word_address])|std::uint32_t(ee.ram[stop_word_address+1])<<8|
                    std::uint32_t(ee.ram[stop_word_address+2])<<16|std::uint32_t(ee.ram[stop_word_address+3])<<24;
                // HG_WATCH_WORDS="addr,addr,...", HG_WATCH_EVERY=K: print those EE words each
                // time the stop word (a frame counter) reaches a new multiple of K.
                static const auto watch_words=[]{
                    std::vector<std::uint32_t> out;
                    if(const char* list=std::getenv("HG_WATCH_WORDS")) {
                        std::stringstream text(list);std::string item;
                        while(std::getline(text,item,','))if(!item.empty())out.push_back(std::uint32_t(std::stoul(item,nullptr,0)));
                    }
                    return out;
                }();
                static const std::uint32_t watch_every=[]{const char* v=std::getenv("HG_WATCH_EVERY");return v?std::uint32_t(std::stoul(v)):0u;}();
                static std::uint32_t watch_last=~0u;
                if(watch_every&&!watch_words.empty()&&word!=watch_last&&word<0x100000u&&word%watch_every==0) {
                    watch_last=word;std::cerr<<"HG-DIAG-124 watch frame="<<word;
                    for(const auto a:watch_words)if(a+4<=ee.ram.size())
                        std::cerr<<' '<<std::hex<<(std::uint32_t(ee.ram[a])|std::uint32_t(ee.ram[a+1])<<8|std::uint32_t(ee.ram[a+2])<<16|std::uint32_t(ee.ram[a+3])<<24)<<std::dec;
                    std::cerr<<'\n';
                }
                {   // HG-DIAG-131 (HG_EE_WRITE_WATCH=hex[,hex[,hex]]): print EE stores to those words live.
                    static const std::array<std::uint32_t,3> ee_watch=[]{
                        std::array<std::uint32_t,3> w{};const char* v=std::getenv("HG_EE_WRITE_WATCH");
                        for(unsigned n=0;v && *v && n<3;++n){char* end=nullptr;w[n]=std::uint32_t(std::strtoul(v,&end,16));v=end && *end==','?end+1:nullptr;}
                        return w;}();
                    static std::size_t ee_watch_cursor=0;
                    if(ee_watch[0]) {
                        for(unsigned n=0;n<3;++n)if(ee_watch[n])ee.ram_write_trace_addresses[1+n]=ee_watch[n];
                        const auto first=ee.ram_write_trace_cursor>ee.ram_write_trace.size()?ee.ram_write_trace_cursor-ee.ram_write_trace.size():0;
                        if(ee_watch_cursor<first)ee_watch_cursor=first;
                        for(;ee_watch_cursor<ee.ram_write_trace_cursor;++ee_watch_cursor) {
                            const auto& write=ee.ram_write_trace[ee_watch_cursor%ee.ram_write_trace.size()];
                            bool hit=false;for(const auto w:ee_watch)hit|=w && write.address<=w+3u && std::uint64_t(write.address)+write.size>w;
                            if(!hit)continue;
                            std::fprintf(stderr,"HG-DIAG-131 EE write slice=%llu pc=0x%x ra=0x%x addr=0x%x before=0x%x after=0x%x size=%u thread=%u spu2_frames=%llu\n",
                                static_cast<unsigned long long>(slice),write.pc,write.ra,write.address,write.before,write.after,write.size,write.thread,
                                static_cast<unsigned long long>(iop.spu2_input_frames));
                        }
                    }
                }
                {   // HG-DIAG-132 (HG_EE_PC_LOG=1, trace build, with --watch-ee-pc): print each new
                    // watch hit with arguments; a2 is read as a {ptr,len} descriptor when in RAM.
                    static const bool pc_log=std::getenv("HG_EE_PC_LOG")!=nullptr;
                    static std::array<std::size_t,hg::State::max_trace_watches> seen{};
                    if(pc_log)for(std::size_t n=0;n<ee.trace_watches.size();++n) {
                        const auto& watch=ee.trace_watches[n];
                        if(watch.cursor==seen[n])continue;
                        seen[n]=watch.cursor;
                        const auto& hit=watch.history[(watch.cursor+watch.history.size()-1)%watch.history.size()];
                        const auto word=[&](std::uint32_t address)->std::uint32_t {
                            address&=0x1ffffffu;if(address+4>ee.ram.size())return 0;
                            return std::uint32_t(ee.ram[address])|std::uint32_t(ee.ram[address+1])<<8|std::uint32_t(ee.ram[address+2])<<16|std::uint32_t(ee.ram[address+3])<<24;};
                        std::fprintf(stderr,"HG-DIAG-132 slice=%llu pc=0x%x ra=0x%x a0=0x%x a1=0x%x a2=0x%x a3=0x%x desc=[0x%x,0x%x] t0=0x%x s0=0x%x s1=0x%x s2=0x%x\n",
                            static_cast<unsigned long long>(slice),hit.pc,hit.ra,hit.a0,hit.a1,hit.a2,hit.a3,word(hit.a2),word(hit.a2+4),hit.t0,hit.s0,hit.s1,hit.s2);
                    }
                }
                {   // HG-DIAG-132 IOP side (HG_IOP_PC_LOG=1, trace build, --watch-iop-pc): new hits with thread.
                    static const bool iop_pc_log=std::getenv("HG_IOP_PC_LOG")!=nullptr;
                    static std::array<std::size_t,hg::IopState::max_trace_watches> iop_seen{};
                    if(iop_pc_log)for(std::size_t n=0;n<iop.trace_watch_count;++n) {
                        const auto& watch=iop.trace_watches[n];
                        if(watch.cursor==iop_seen[n])continue;
                        iop_seen[n]=watch.cursor;
                        const auto& hit=watch.history[(watch.cursor+watch.history.size()-1)%watch.history.size()];
                        std::fprintf(stderr,"HG-DIAG-132 IOP slice=%llu pc=0x%x ra=0x%x thread=%u a0=0x%x a1=0x%x a2=0x%x a3=0x%x s0=0x%x s1=0x%x\n",
                            static_cast<unsigned long long>(slice),hit.pc,hit.return_pc,hit.thread,hit.a0,hit.a1,hit.a2,hit.a3,hit.s0,hit.s1);
                    }
                }
                static const bool stop_word_trace=std::getenv("HG_STOP_WORD_TRACE")!=nullptr;
                if(stop_word_trace&&(slice&32767)==0)std::cerr<<"HG-DIAG-124 trace slice="<<slice<<" word="<<word<<std::endl;
                if(word==stop_word_value) {   // exact: the address may hold other data before the counter starts
                    std::cerr<<"HG-DIAG-124 EE word 0x"<<std::hex<<stop_word_address<<" = "<<std::dec<<word<<" at slice "<<slice<<" pc=0x"<<std::hex<<ee.pc<<std::dec<<"; stopping\n";
                    if(std::getenv("HG_STOP_SPU2")) {   // voice ENVX per core at the stop (HG-DIAG-124)
                        for(unsigned core=0;core<2;++core) {
                            std::cerr<<"HG-DIAG-124 core"<<core<<" ENVX";
                            for(unsigned voice=0;voice<24;++voice)
                                std::cerr<<' '<<std::hex<<iop.spu2_registers[(core*0x400u+voice*0x10u+0xau)/2]<<std::dec;
                            std::cerr<<'\n';
                        }
                    }
                    break;
                }
            }
            const bool autopilot_owns=autopilot&&!autopilot->done()&&(input_events.empty()||input_event_cursor>=input_events.size());
            if(autopilot_owns&&(slice&1023)==0) {
                const auto state=hg::autopilot_read_state(ee.ram);
                hg::HostInput pad;
                try {pad=autopilot->step(state);}
                catch(const std::exception& error) {   // explicit stop of the script, not of the game
                    std::cerr<<"HG-DIAG-123 autopilot stopped: "<<error.what()<<" (slice "<<slice<<"); live input enabled\n";
                    if(host&&host->notice)host->notice("Autopilot stopped - see log");
                    autopilot.reset();
                }
                static const bool autopilot_trace=std::getenv("HG_AUTOPILOT_TRACE")!=nullptr;
                if(autopilot&&autopilot_trace&&(slice&16383)==0)std::cerr<<"HG-DIAG-123 trace slice="<<slice<<' '<<autopilot->trace(state)<<std::endl;
                if(autopilot&&!autopilot->last_message().empty()) {
                    std::cerr<<autopilot->last_message()<<" (slice "<<slice<<")\n";
                    if(host&&host->notice)host->notice(autopilot->last_message());
                }
                if(!autopilot_applied||!(pad==autopilot_last)) {
                    iop.sio2_buttons[0]=pad.buttons;
                    iop.sio2_controllers[0].axes=pad.axes;
                    iop.sio2_controllers[0].pressures=pad.pressures;
                    record_input(slice,hg::format_diagnostic_pad(pad));
                    if(host_input_log){host_input_log<<slice<<' '<<hg::format_diagnostic_pad(pad)<<'\n';host_input_log.flush();}
                    autopilot_last=pad;autopilot_applied=true;
                }
                if(autopilot&&autopilot->done())std::cerr<<"HG-DIAG-123 autopilot script finished at slice "<<slice<<"; live input enabled\n";
            }
            if(host&&host->closing()&&autopilot_owns&&(slice&1023)==0) {
                report_original_frame_candidate("close");
                if(spu2_wav){spu2_wav->finalize();report_spu2_capture();}
                return 0;
            }
            if(host&&!autopilot_owns&&(input_events.empty() || input_event_cursor>=input_events.size())&&(slice&1023)==0) {
                if(host->closing()) {
                    report_original_frame_candidate("close");
                    if(spu2_wav){spu2_wav->finalize();report_spu2_capture();}
                    return 0;
                }
                hg::HostInput input;
                if(!checkpoint_file.empty()&&host->checkpoint_requested&&host->checkpoint_requested())
                    write_checkpoint(slice);
                if(host->input(input)) {
                    record_input(slice,hg::format_diagnostic_pad(input));
                    iop.sio2_buttons[0]=input.buttons;
                    iop.sio2_controllers[0].axes=input.axes;
                    iop.sio2_controllers[0].pressures=input.pressures;
                    if(host_input_log) {
                        host_input_log<<slice<<' '<<hg::format_diagnostic_pad(input)<<'\n';
                        host_input_log.flush();
                        if(!host_input_log)throw std::runtime_error("cannot write input log");
                    }
                    // HG-DIAG-006: capture only delivered game-controller changes,
                    // at their guest slice, to reproduce interactive faults.
                    if(profile_enabled) {
                        std::cerr<<"Host input slice="<<slice<<" buttons="<<input.buttons<<" axes=";
                        for(unsigned n=0;n<input.axes.size();++n)
                            std::cerr<<(n?",":"")<<unsigned(input.axes[n]);
                        std::cerr<<" pressures=";
                        for(unsigned n=0;n<input.pressures.size();++n)
                            std::cerr<<(n?",":"")<<unsigned(input.pressures[n]);
                        std::cerr<<'\n';
                    }
                }
            }
            if(memory_card_insert_at&&slice==memory_card_insert_at&&iop.memory_card&&!iop.memory_card->present) {
                iop.memory_card->present=true;
                std::cerr<<"Virtual memory card inserted at slice "<<slice<<'\n';
            }
            const auto clock_tick=execution_clock.quantum();
            // HG-DIAG-012: pace the host before profiling/executing this quantum.
            // Every guest quantum and its original input event still execute.
            // Fast replay: host pacing only; every guest quantum and input still runs.
            // The pacer re-bases itself (>100 ms lag) once paced play resumes.
            if(realtime && !fast_replay_active) {
                pacing_time_us+=clock_tick.microseconds;
                if(host_pacer.due(pacing_time_us)) {
                    const auto now=std::chrono::steady_clock::now();
                    const auto target=host_pacer.deadline(pacing_time_us,now);
                    if(target>now) {
                        ++pacing_waits;
                        pacing_requested_us+=std::chrono::duration_cast<std::chrono::microseconds>(target-now).count();
                        host_waiter.wait_until(target);
                        const auto woke=std::chrono::steady_clock::now();
                        pacing_actual_us+=std::chrono::duration_cast<std::chrono::microseconds>(woke-now).count();
                        pacing_max_late_us=std::max(pacing_max_late_us,std::uint64_t(
                            std::chrono::duration_cast<std::chrono::microseconds>(woke-target).count()));
                    }
                }
            }
            hg::DiagnosticProfile::Slice profile_slice(host_profile,slice,ee.pc);
            if(checkpoint_every && slice && slice%checkpoint_every==0) {
                host_profile.report();report_gs_profile();
                report_speaker("checkpoint");
                report_original_frame_candidate("checkpoint");
                if(hg::host_sampler_enabled()) {
                    if(sample_from&&slice==sample_from)hg::host_sampler_set_active(true);
                    if(sample_to&&slice==sample_to)hg::host_sampler_set_active(false);
                }
                if(profile_enabled)std::cerr<<"Raster service host_ns="<<raster_host_ns
                    <<" draws="<<raster_profile_draws<<'\n';
                // HG-DIAG-012: localize deliberate waiting separately from
                // execution cost without adding a per-quantum clock query.
                if(realtime && profile_enabled)std::cerr<<"Host pacing checkpoint slice="<<slice
                    <<" guest_us="<<pacing_time_us<<" waits="<<pacing_waits
                    <<" requested_us="<<pacing_requested_us<<" actual_us="<<pacing_actual_us
                    <<" max_late_us="<<pacing_max_late_us<<" rebases="<<host_pacer.rebases()<<'\n';
            }
            if((host||!preview_file.empty()) && ((!issue_slot_clock && slice%1000000==0) || preview_due)) {
                device_owner.post_or_run([&]{publish_preview(false);});preview_due=false;
            }
            if(slice==next_input_slice) {
                apply_input(input_events[input_event_cursor].command,slice);
                record_input(slice,input_events[input_event_cursor].command);
                ++input_event_cursor;
                next_input_slice=input_event_cursor<input_events.size()?input_events[input_event_cursor].slice:~std::uint64_t{0};
                if(input_event_cursor==input_events.size()) {
                    fast_replay_active=false;
                    if(iop.memory_card&&!iop.memory_card->present) {
                        iop.memory_card->present=true;
                        std::cerr<<"Virtual memory card inserted at slice "<<slice<<'\n';
                    }
                }
                if(host && input_event_cursor==input_events.size()) {
                    if(input_replay_wait_live) {
                        hg::HostInput discarded;
                        while(host->input(discarded)) {}
                        std::cerr<<"Diagnostic input replay ready at slice "<<slice
                                 <<"; guest paused until live controller input\n";
                        if(host->notice)host->notice("Checkpoint loaded - paused, press any game key");
                        hg::HostInput live;
                        while(!host->closing() && !host->input(live))
                            std::this_thread::sleep_for(std::chrono::milliseconds(1));
                        if(host->closing())return 0;
                        record_input(slice,hg::format_diagnostic_pad(live));
                        iop.sio2_buttons[0]=live.buttons;
                        iop.sio2_controllers[0].axes=live.axes;
                        iop.sio2_controllers[0].pressures=live.pressures;
                        if(host_input_log) {
                            host_input_log<<slice<<' '<<hg::format_diagnostic_pad(live)<<'\n';
                            host_input_log.flush();
                            if(!host_input_log)throw std::runtime_error("cannot write input log");
                        }
                        std::cerr<<"Diagnostic input replay released to live controller at slice "<<slice<<'\n';
                    } else std::cerr<<"Diagnostic input replay complete; live host input enabled after slice "<<slice<<'\n';
                }
            }
            if(!input_file.empty() && slice%10000==0) {
                // Windows can briefly reject opens while the host atomically
                // replaces a command. Retry only opening; malformed contents
                // remain an explicit error and no guest execution is skipped.
                std::ifstream input;
                for(unsigned attempt=0;attempt<4 && !input.is_open();++attempt) {
                    input.clear();input.open(input_file,std::ios::binary|std::ios::ate);
                    if(!input.is_open())std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
                if(!input.is_open())throw std::runtime_error("cannot open diagnostic input file");
                if(input.tellg()>32)throw std::runtime_error("diagnostic input file is too large");
                input.seekg(0);std::string command,extra;
                if(!(input>>command) || (input>>extra) ||
                   (command!="none" && command!="left" && command!="cross" && command!="start"))
                    throw std::runtime_error("input file must contain none, left, cross, or start");
                apply_input(command,slice);
            }
            const auto pulse_button=[&](unsigned at,std::uint16_t mask,unsigned pressure,const char* name) {
                if(!at||(slice!=at&&slice!=at+250000))return;
                const bool pressed=slice==at;
                // Active-low button bits are independent; overlapping pulses
                // must not release another button or overwrite its pressure.
                if(pressed)iop.sio2_buttons[0]&=std::uint16_t(~mask);
                else iop.sio2_buttons[0]|=mask;
                iop.sio2_controllers[0].pressures[pressure]=pressed?0xff:0;
                std::cerr<<"Diagnostic "<<name<<' '<<(pressed?"pressed":"released")<<" at slice "<<slice<<'\n';
            };
            pulse_button(left_at,0x0080,1,"Left");
            pulse_button(cross_at,0x4000,6,"Cross");
            if(checkpoint_every && slice%checkpoint_every==0) {
                std::ostringstream line;
                line<<"Checkpoint slice="<<std::dec<<slice<<" EE=0x"<<std::hex<<ee.pc
                    <<" IOP=0x"<<iop.pc<<" VIF1_CHCR=0x"<<ee.dmac.channels[1].chcr
                    <<" GIF_CHCR=0x"<<ee.dmac.channels[2].chcr<<std::dec;
                device_owner.post_or_run([&ee,text=line.str()] {
                    std::cerr<<text<<" GS_draws="<<ee.gs.draws.size()<<" rasterized="<<ee.gs.rasterized_draw_count
                             <<" retired="<<ee.gs.retired_draw_count<<'\n';
                });
            }
            profile_slice.mark(1);
            iop.virtual_time_us+=clock_tick.microseconds;
            if(clock_tick.microseconds) {
            ee_timer_clock.advance_us(ee.timers,ee.intc,clock_tick.microseconds);
            iop.advance_timers(clock_tick.microseconds);
            if(spu2_wav || spu2_output || spu2_mix_writer) {
                // HG-DIAG-013: observe the already-modeled raw sound-input stream.
                // Preserve the existing bounded history while copying one explicit core to WAV.
                iop.advance_spu2(clock_tick.microseconds,[&](unsigned core,std::uint16_t left,std::uint16_t right) {
                    iop.spu2_input_history[iop.spu2_input_frames++%iop.spu2_input_history.size()]={core,left,right};
                    observe_spu2(core,left,right);
                },[&](hg::Spu2Voices::Frame frame) {
                    // Speaker: final SPU2 mix (core 1 output: voices + sound-data input via core 0).
                    if(spu2_output && !fast_replay_active) {
                        spu2_output->append(std::uint16_t(frame.left),std::uint16_t(frame.right));
                        // HG-DIAG-127: log host speaker underruns as they happen (host-only;
                        // a starved buffer is heard as crackle). One compare per frame.
                        static std::uint64_t reported_underruns=0;
                        if(spu2_output->underruns()!=reported_underruns) {
                            reported_underruns=spu2_output->underruns();
                            std::cerr<<"Host audio underrun total="<<reported_underruns<<" at guest_seconds="
                                     <<spu2_output->frames()/48000.0<<" slice="<<slice<<'\n';
                        }
                    }
                    if(spu2_mix_writer)spu2_mix_writer->append(std::uint16_t(frame.left),std::uint16_t(frame.right));
                });
            } else iop.advance_spu2(clock_tick.microseconds);
            display_timing.configure(ee.gs.crtc_configured,ee.gs.crtc_interlace,ee.gs.crtc_mode,ee.gs.crtc_frame);
            display_timing.advance_us(clock_tick.microseconds,[&](bool start){
                ee.intc.raise(start?2:3);
                if(start){ee.gs_raise_csr_events(8);iop.signal_vblank_start();}
                if(start && issue_slot_clock && (host||!preview_file.empty())) {
                    const auto now=std::chrono::steady_clock::now();
                    if(now>=next_preview){preview_due=true;next_preview=now+std::chrono::milliseconds(16);}
                }
            });
            }
            profile_slice.mark(2);
            if(!ee_dma_interrupt.active() && !ee_intc_interrupt.active()) {
                auto run_ee=[&](hg::State& s,std::uint64_t count){if(issue_slot_clock)hg::run_burst(s,count);else hg::run(s,count);};
                if(!ee.boot.thread_ready)run_ee(ee,issue_slot_clock?clock_tick.ee_budget:transport_ready?1:1000);
                else hg::service_ee_thread(ee,run_ee,clock_tick.ee_budget);
            }
            observe_original_frame_candidate();
            if(spu2_sync_events.is_open()) {
                // HG-DIAG-013: drain the bounded watched-write ring every guest
                // slice so early queue/ring mutations cannot roll out before the
                // much less frequent movie-sync callback. Device state does not
                // advance between the EE run above and this observation point.
                const auto& core0=iop.spu2_input.cores[0];constexpr std::uint32_t dma0=0x1f8010c0u;
                const auto emit_event_state=[&]() {
                    spu2_sync_events<<','<<iop.spu2_dma_count<<','<<iop.spu2_dma_bytes<<','
                        <<core0.frames<<','<<core0.phase<<','<<core0.read_frame<<','
                        <<core0.ready[0]<<','<<core0.ready[1]<<','<<core0.write_half<<','
                        <<iop.dmac.read(dma0,4)<<','<<iop.dmac.read(dma0+4,4)<<','<<iop.dmac.read(dma0+8,4)<<','
                        <<iop.spu2_input_dma[0].remaining<<'\n';
                };
                const auto dma_madr=iop.dmac.read(dma0,4),dma_bcr=iop.dmac.read(dma0+4,4),dma_chcr=iop.dmac.read(dma0+8,4);
                const auto dma_remaining=iop.spu2_input_dma[0].remaining;
                if(!spu2_sync_dma_state_valid || iop.spu2_dma_count!=spu2_sync_dma_count ||
                   iop.spu2_dma_bytes!=spu2_sync_dma_bytes || dma_madr!=spu2_sync_dma_madr ||
                   dma_bcr!=spu2_sync_dma_bcr || dma_chcr!=spu2_sync_dma_chcr || dma_remaining!=spu2_sync_dma_remaining) {
                    spu2_sync_events<<"spu2,"<<slice<<','<<iop.virtual_time_us<<",0,0,0,0,0,0,0,0,0,0,0,0";
                    emit_event_state();
                    spu2_sync_dma_state_valid=true;spu2_sync_dma_count=iop.spu2_dma_count;spu2_sync_dma_bytes=iop.spu2_dma_bytes;
                    spu2_sync_dma_madr=dma_madr;spu2_sync_dma_bcr=dma_bcr;spu2_sync_dma_chcr=dma_chcr;spu2_sync_dma_remaining=dma_remaining;
                }
                const auto first_write=ee.ram_write_trace_cursor>ee.ram_write_trace.size()
                    ?ee.ram_write_trace_cursor-ee.ram_write_trace.size():0;
                if(spu2_sync_ram_write_cursor<first_write)spu2_sync_ram_write_cursor=first_write;
                while(spu2_sync_ram_write_cursor<ee.ram_write_trace_cursor) {
                    const auto& write=ee.ram_write_trace[spu2_sync_ram_write_cursor%ee.ram_write_trace.size()];
                    ++spu2_sync_ram_write_cursor;
                    std::uint32_t watched_word=0;
                    for(const auto target:{0x003d4058u,0x003ccc6cu,0x003d0134u})
                        if(write.address<=target+3u && std::uint64_t(write.address)+write.size>target){watched_word=target;break;}
                    if(!watched_word)continue;
                    spu2_sync_events<<"write,"<<slice<<','<<iop.virtual_time_us<<','<<write.pc<<','<<write.ra
                        <<",0,0,0,0,"<<watched_word<<','<<write.address<<','<<write.before<<','<<write.after<<','
                        <<write.size<<','<<write.thread;
                    emit_event_state();
                }
                for(std::size_t method=0;method<spu2_sync_method_pcs.size();++method) {
                    for(const auto& watch:ee.trace_watches)if(watch.pc==spu2_sync_method_pcs[method] &&
                                                               watch.cursor!=spu2_sync_method_cursors[method]) {
                        spu2_sync_method_cursors[method]=watch.cursor;
                        const auto& call=watch.history[(watch.cursor-1)%watch.history.size()];
                        spu2_sync_events<<"call,"<<slice<<','<<iop.virtual_time_us<<','<<call.pc<<','<<call.ra<<','
                            <<call.a0<<','<<call.a1<<','<<call.a2<<','<<call.a3<<",0,0,0,0,0,0";
                        emit_event_state();
                        break;
                    }
                }
                if(!spu2_sync_events)throw std::runtime_error("cannot write SPU2 sync event log");
            }
            if(spu2_sync) {
                for(const auto& selected:ee.trace_watches)if(selected.pc==0x00242ad8u && selected.cursor!=spu2_sync_watch_cursor) {
                    spu2_sync_watch_cursor=selected.cursor;
                    const auto& record=selected.history[(selected.cursor-1)%selected.history.size()];
                    // 0x242a90 saves caller a1 in s0; 0x242ab8 passes that
                    // object as a0 to 0x1d3210. 0x242ad0/0x242ad4 load the
                    // completed sp+0x34/sp+0x30 outputs into v1/v0 respectively.
                    const auto object=record.s0;
                    const auto wrapper=std::uint32_t(ee.load(object+4u,4,false));
                    const auto inner=std::uint32_t(ee.load(wrapper+4u,4,false));
                    const auto guest_position=record.v0;
                    const auto guest_output_34=record.v1;
                    const auto state=std::uint32_t(ee.load(object+1u,1,false));
                    bool formula_path=false,callback_1e1cd8_valid=false,callback_1d3c58_valid=false;
                    std::uint32_t formula_wrapper_2c=0,latency_1cd0e0=0,latency_1d3c58=0;
                    std::uint32_t callback_1e1cd8_raw=0,callback_1e1cd8_object=0,callback_1e1cd8_target=0;
                    std::uint32_t callback_1d3c58_raw=0,callback_1d3c58_object=0,callback_1d3c58_target=0;
                    for(const auto& formula_watch:ee.trace_watches)if(formula_watch.pc==0x001d3134u) {
                        const bool fresh=formula_watch.cursor!=spu2_sync_formula_cursor && formula_watch.cursor<selected.cursor;
                        if(fresh) {
                            spu2_sync_formula_cursor=formula_watch.cursor;
                            const auto& formula=formula_watch.history[(formula_watch.cursor-1)%formula_watch.history.size()];
                            formula_path=formula.s2==object;
                            if(formula_path) {
                                formula_wrapper_2c=formula.s1;
                                latency_1cd0e0=formula.s0;
                                latency_1d3c58=formula.v0;
                                // These are the actual callback returns observed inside
                                // the two original helpers. Stop at each helper callsite
                                // so an older callback cannot be mistaken for this call.
                                for(std::size_t n=1;n<formula_watch.history.size();++n) {
                                    const auto& prior=formula_watch.history[(formula_watch.cursor+formula_watch.history.size()-1-n)%formula_watch.history.size()];
                                    if(prior.pc==0x001d3ca0u) {
                                        callback_1d3c58_valid=true;callback_1d3c58_raw=prior.v0;callback_1d3c58_object=prior.a0;
                                        const auto table=std::uint32_t(ee.load(prior.a0,4,false));
                                        callback_1d3c58_target=std::uint32_t(ee.load(table+0x24u,4,false));
                                        break;
                                    }
                                    if(prior.pc==0x001d312cu)break;
                                }
                                for(std::size_t n=1;n<formula_watch.history.size();++n) {
                                    const auto& prior=formula_watch.history[(formula_watch.cursor+formula_watch.history.size()-1-n)%formula_watch.history.size()];
                                    if(prior.pc==0x001e1d1cu) {
                                        callback_1e1cd8_valid=true;callback_1e1cd8_raw=prior.v0;
                                        break;
                                    }
                                    if(prior.pc==0x001d311cu)break;
                                }
                                if(callback_1e1cd8_valid) {
                                    for(const auto& callback_watch:ee.trace_watches)if(callback_watch.pc==0x001e1d14u) {
                                        const bool fresh_callback=callback_watch.cursor!=spu2_sync_callback_a_cursor &&
                                                                  callback_watch.cursor<formula_watch.cursor;
                                        if(fresh_callback) {
                                            const auto& callback=callback_watch.history[(callback_watch.cursor-1)%callback_watch.history.size()];
                                            callback_1e1cd8_object=callback.a0;
                                            callback_1e1cd8_target=callback.v0;
                                            spu2_sync_callback_a_cursor=callback_watch.cursor;
                                        }
                                        break;
                                    }
                                }
                            }
                        }
                        break;
                    }
                    // HG-DIAG-013: 0x1e3f68's live selector-0 path sums +0x0c
                    // across the list rooted at callback object +0x18. Capture
                    // that exact source state so the original callback return can
                    // be checked independently against the modeled AutoDMA phase.
                    std::int32_t callback_a_state4=0;
                    std::uint32_t callback_a_list_head=0,callback_a_list_count=0,callback_a_list_sum=0;
                    bool callback_a_list_matches_raw=false,callback_a_list_truncated=false;
                    std::array<std::array<std::uint32_t,3>,4> callback_a_nodes{};
                    if(callback_1e1cd8_valid && callback_1e1cd8_object &&
                       std::uint64_t(callback_1e1cd8_object)+0x1cu<=ee.ram.size()) {
                        callback_a_state4=std::int8_t(ee.load(callback_1e1cd8_object+4u,1,false));
                        callback_a_list_head=std::uint32_t(ee.load(callback_1e1cd8_object+0x18u,4,false));
                        auto node=callback_a_list_head;
                        constexpr std::uint32_t max_callback_a_nodes=64;
                        while(node && callback_a_list_count<max_callback_a_nodes) {
                            if(node%4 || std::uint64_t(node)+0x10u>ee.ram.size()) {
                                callback_a_list_truncated=true;break;
                            }
                            const auto next=std::uint32_t(ee.load(node,4,false));
                            const auto value=std::uint32_t(ee.load(node+0x0cu,4,false));
                            if(callback_a_list_count<callback_a_nodes.size())
                                callback_a_nodes[callback_a_list_count]={node,next,value};
                            callback_a_list_sum+=value;++callback_a_list_count;node=next;
                        }
                        if(node)callback_a_list_truncated=true;
                        callback_a_list_matches_raw=!callback_a_list_truncated && callback_a_list_sum==callback_1e1cd8_raw;
                    }
                    std::uint32_t callback_b_source_word=0;
                    if(callback_1d3c58_valid && callback_1d3c58_object &&
                       std::uint64_t(callback_1d3c58_object)+0x10u<=ee.ram.size())
                        callback_b_source_word=std::uint32_t(ee.load(callback_1d3c58_object+0x0cu,4,false));
                    auto latest_writer=[&](std::uint32_t target) {
                        std::pair<bool,hg::State::RamWriteTrace> result{};
                        const auto count=std::min(ee.ram_write_trace_cursor,ee.ram_write_trace.size());
                        for(std::size_t n=0;n<count;++n) {
                            const auto& candidate=ee.ram_write_trace[(ee.ram_write_trace_cursor+ee.ram_write_trace.size()-1-n)%ee.ram_write_trace.size()];
                            if(candidate.address<=target+3 && std::uint64_t(candidate.address)+candidate.size>target) {
                                result={true,candidate};break;
                            }
                        }
                        return result;
                    };
                    const auto callback_a_root_writer=latest_writer(0x003d4058u);
                    std::pair<bool,hg::State::RamWriteTrace> callback_a_node_writer{};
                    if(callback_a_list_head)callback_a_node_writer=latest_writer(callback_a_list_head+0x0cu);
                    const auto callback_b_writer=latest_writer(0x003d0134u);
                    const auto dma0=0x1f8010c0u,dma1=0x1f801500u;
                    const auto& core0=iop.spu2_input.cores[0];const auto& core1=iop.spu2_input.cores[1];
                    spu2_sync<<slice<<','<<iop.virtual_time_us<<','<<guest_position<<','<<guest_output_34<<','
                        <<formula_path<<','<<formula_wrapper_2c<<','<<latency_1cd0e0<<','<<latency_1d3c58<<','
                        <<callback_1e1cd8_valid<<','<<callback_1e1cd8_raw<<','<<callback_1e1cd8_object<<','<<callback_1e1cd8_target<<','
                        <<callback_a_state4<<','<<callback_a_list_head<<','<<callback_a_list_count<<','<<callback_a_list_sum<<','
                        <<callback_a_list_matches_raw<<','<<callback_a_list_truncated<<','
                        <<callback_a_nodes[0][0]<<','<<callback_a_nodes[0][1]<<','<<callback_a_nodes[0][2]<<','
                        <<callback_a_nodes[1][0]<<','<<callback_a_nodes[1][1]<<','<<callback_a_nodes[1][2]<<','
                        <<callback_a_nodes[2][0]<<','<<callback_a_nodes[2][1]<<','<<callback_a_nodes[2][2]<<','
                        <<callback_a_nodes[3][0]<<','<<callback_a_nodes[3][1]<<','<<callback_a_nodes[3][2]<<','
                        <<callback_a_root_writer.first<<','<<callback_a_root_writer.second.pc<<','<<callback_a_root_writer.second.ra<<','
                        <<callback_a_root_writer.second.before<<','<<callback_a_root_writer.second.after<<','
                        <<callback_a_node_writer.first<<','<<callback_a_node_writer.second.pc<<','<<callback_a_node_writer.second.ra<<','
                        <<callback_a_node_writer.second.before<<','<<callback_a_node_writer.second.after<<','
                        <<callback_1d3c58_valid<<','<<callback_1d3c58_raw<<','<<callback_1d3c58_object<<','<<callback_1d3c58_target<<','
                        <<callback_b_source_word<<','<<callback_b_writer.first<<','<<callback_b_writer.second.pc<<','<<callback_b_writer.second.ra<<','
                        <<callback_b_writer.second.before<<','<<callback_b_writer.second.after<<','
                        <<state<<','<<object<<','<<wrapper<<','<<inner<<','<<ee.load(wrapper+0x2cu,4,false)<<','
                        <<ee.load(inner+0x14u,4,false)<<','<<ee.load(inner+0x18u,4,false)<<','<<ee.load(inner+0x1cu,2,false)<<','
                        <<ee.load(object+0x88u,4,false)<<','<<ee.load(object+0xa4u,4,false)<<','<<ee.load(object+0xa8u,4,false)<<','
                        <<ee.load(0x003b7230u,4,false)<<','
                        <<core0.frames<<','<<core0.phase<<','<<core0.read_frame<<','<<core0.ready[0]<<','<<core0.ready[1]<<','<<core0.write_half<<','
                        <<iop.dmac.read(dma0,4)<<','<<iop.dmac.read(dma0+4,4)<<','<<iop.dmac.read(dma0+8,4)<<','<<iop.spu2_input_dma[0].remaining<<','
                        <<core1.frames<<','<<core1.phase<<','<<core1.read_frame<<','<<core1.ready[0]<<','<<core1.ready[1]<<','<<core1.write_half<<','
                        <<iop.dmac.read(dma1,4)<<','<<iop.dmac.read(dma1+4,4)<<','<<iop.dmac.read(dma1+8,4)<<','<<iop.spu2_input_dma[1].remaining<<'\n';
                    if(!spu2_sync)throw std::runtime_error("cannot write SPU2 sync log");
                }
            }
            profile_slice.mark(3);
            if(profile_enabled) {
                const bool active=ee.ipu.csc_remaining!=0;
                if(active!=csc_active) {
                    if(active)++csc_sequence;
                    std::cerr<<"CSC "<<(active?"begin":"end")<<" sequence="<<csc_sequence
                        <<" guest_us="<<iop.virtual_time_us<<" host_seconds="
                        <<std::chrono::duration<double>(std::chrono::steady_clock::now()-playback_start).count()
                        <<" macroblocks_remaining="<<ee.ipu.csc_remaining<<'\n';
                    csc_active=active;
                }
            }
            for(const auto& selected:ee.trace_watches)if(selected.pc==0x0026fa60u && selected.cursor!=ee_rpc_completion_watch_cursor) {
                ee_rpc_completion_watch_cursor=selected.cursor;
                const auto& record=selected.history[(selected.cursor-1)%selected.history.size()];
                auto& snapshot=ee_rpc_completion_snapshots[ee_rpc_completion_snapshot_cursor++%ee_rpc_completion_snapshots.size()];
                snapshot={};snapshot.packet=record.a0;
                snapshot.packet_valid=capture_ee_words(snapshot.packet,snapshot.packet_words);
                if(snapshot.packet_valid) {
                    snapshot.client=snapshot.packet_words[7];
                    snapshot.client_valid=capture_ee_words(snapshot.client,snapshot.client_words);
                }
            }
            ee.pump_scratchpad(8);ee.pump_scratchpad(9);
            // EE manual pp41-43: peripheral DMA arbitrates in eight-qword
            // slices. The old one-qword/us diagnostic imposed a16MB/s ceiling.
            // This bounded functional quantum still stops on real FIFO pressure;
            // exact bus-cycle arbitration remains outside the experimental clock.
            for(unsigned n=0;n<(issue_slot_clock?8u:1u);++n)if(!ee.pump_ipu_input())break;
            profile_slice.mark(4);
            for(unsigned n=0;n<(issue_slot_clock?8u:1u);++n)if(!ee.pump_ipu_output())break;
            profile_slice.mark(5);
            while(ee.pump_vif1()) {}
            while(ee.pump_gif()) {}
            profile_slice.mark(6);
            // GS manual pp38-43: a drawing kick starts rendering; FINISH is
            // not required to execute it. Service submitted work independently
            // of preview reads, in original submission order. This functional
            // quantum does not model the pixel pipeline's exact latency.
            try {
                if(ee.device) {
                    // Same position in the submitted sequence as the synchronous
                    // call; skipped only when nothing reached the device since.
                    if(ee.device->raster_due){ee.device->push(hg::DeviceKind::rasterize,0,0);ee.device->raster_due=false;}
                    ee.device->publish();
                } else if(profile_enabled && !realtime && ee.gs.pending_draw_index()<ee.gs.draws.size()) {
                    const auto started=std::chrono::steady_clock::now();
                    raster_profile_draws+=ee.gs.rasterize_pending_draws();
                    raster_host_ns+=std::chrono::duration_cast<std::chrono::nanoseconds>(
                        std::chrono::steady_clock::now()-started).count();
                } else ee.gs.rasterize_pending_draws();
            }
            catch(const std::runtime_error& e){throw hg::Fault(ee.pc,e.what());}
            if(reboot_count && ee.boot.sif_system_registers[2]==0)reboot_rpc_cleared=true;
            if(!transport_ready && (ee.dmac.channels[5].chcr&0x100)) {
                hg::initialize_sif_transport(ee,iop);transport_ready=true;
            }
            profile_slice.mark(7);
            if(issue_slot_clock && iop.pc!=iop_return)hg::run_iop_burst(iop,clock_tick.iop_budget);
            else for(unsigned n=0;n<(transport_ready?1u:100u) && clock_tick.iop_due && iop.pc!=iop_return;++n)hg::run_iop(iop,1);
            if(iop.cdvd_dma_ready()) {
                const auto request=iop.cdvd_dma_request();
                if(request.kind==hg::IopState::CdvdTransferKind::toc) {
                    if(has_supplied_toc) {iop.complete_cdvd_toc_dma(supplied_toc);continue;}
                    const auto& channel=iop.dmac.channel[3];
                    throw hg::IopFault(iop.pc,"CDVD GetToc requires an independently supplied 2064-byte DMA record (--toc-record): MADR="+std::to_string(channel[0])+
                                               " BCR="+std::to_string(channel[1])+" CHCR="+std::to_string(channel[2]));
                }
                std::array<std::uint8_t,2064> record{};
                record[0]=0x20;const auto sector=request.lba+0x30000u;
                record[1]=std::uint8_t(sector>>16);record[2]=std::uint8_t(sector>>8);record[3]=std::uint8_t(sector);
                std::ifstream input(disc,std::ios::binary);const auto offset=std::uint64_t(request.lba)*2048;
                input.seekg(0,std::ios::end);const auto size=input?input.tellg():std::streampos(-1);
                if(size<0 || offset+2048>std::uint64_t(size))throw hg::IopFault(iop.pc,"CDVD sector outside configured local image");
                input.seekg(std::streamoff(offset));input.read(reinterpret_cast<char*>(record.data()+12),2048);
                if(!input)throw hg::IopFault(iop.pc,"short CDVD sector read from configured local image");
                iop.complete_cdvd_dma(record);
                disc_profile.consumed(request.lba,iop.virtual_time_us);
            }
            if(iop.pc==iop_return && !command_started) {
                if(iop.r(2)!=0 || !ee.sif->sub_address)throw hg::IopFault(iop.pc,"SIFCMD bootstrap did not publish a receive buffer");
                // Original RPC initialization calls the command initializer,
                // which publishes CMDINIT and waits for the EE command.
                begin_iop(iop,hg::compiled_iop_module_export("rom_sifcmd","sifcmd",14));
                iop.interrupts_enabled=true;command_started=true;
            }
            const auto worker_trace_before=iop.pc_trace_cursor;
            // Normal cooperative scheduling remains one instruction per outer
            // slice. Once an IOP SIF0 source DMA is armed, however, let its
            // sending worker run through the short post-send path into the
            // original blocking wait before asynchronous transport can deliver
            // the reply. If it blocks, give the newly selected lower-priority
            // worker one boundary in the same host slice.
            if(fileio_started && clock_tick.iop_due) {
                auto run_worker=[](hg::IopState& s,std::uint64_t count){hg::run_iop(s,count);};
                if(issue_slot_clock)hg::service_iop_thread(iop,[](hg::IopState& s,std::uint64_t count){hg::run_iop_burst(s,count);},clock_tick.iop_budget,true);
                else hg::service_iop_threads(iop,run_worker,iop_worker_budget);
                if(iop.dmac.channel[1][2]&0x01000000u) {
                    hg::service_iop_thread(iop,run_worker,64);
                    hg::service_iop_thread(iop,run_worker);
                }
            }
            profile_slice.mark(8);
            if(iop.pc_trace_cursor>worker_trace_before) {
                const auto& record=iop.pc_trace[(iop.pc_trace_cursor-1)%iop.pc_trace.size()];
                if(record.pc==0x00099964u && send_return_timing_count<send_return_timings.size())
                    send_return_timings[send_return_timing_count++]={record,std::uint32_t(ee.load(0x0198cbc8u,4,false)),iop.virtual_time_us};
            }
            // HG-DIAG-004: insertion and pending returns are the only changes
            // relevant to these consumers; retain their original scan order.
            const bool submit_history_changed=sifman_return_refresh.needs_scan(
                iop.sifman_submit_traces,iop.sifman_submit_trace_cursor);
            if(submit_history_changed) {
                const auto submit_count=std::min(iop.sifman_submit_trace_cursor,iop.sifman_submit_traces.size());
                for(std::size_t n=0;n<submit_count && sifman_submit_timing_count<sifman_submit_timings.size();++n) {
                    const auto& trace=iop.sifman_submit_traces[(iop.sifman_submit_trace_cursor+iop.sifman_submit_traces.size()-submit_count+n)%iop.sifman_submit_traces.size()];
                    if(!trace.valid || !trace.return_seen)continue;
                    bool known=false;for(std::size_t t=0;t<sifman_submit_timing_count;++t)known|=sifman_submit_timings[t].submission_id==trace.submission_id;
                    if(!known)sifman_submit_timings[sifman_submit_timing_count++]={trace.submission_id,std::uint32_t(ee.load(0x0198cbc8u,4,false)),iop.virtual_time_us};
                }
            }
            if(submit_history_changed && !snddrv_submit_seen) {
                const auto submit_count=std::min(iop.sifman_submit_trace_cursor,iop.sifman_submit_traces.size());
                for(std::size_t n=0;n<submit_count;++n) {
                    const auto& trace=iop.sifman_submit_traces[(iop.sifman_submit_trace_cursor+iop.sifman_submit_traces.size()-1-n)%iop.sifman_submit_traces.size()];
                    if(!trace.valid || !trace.return_seen || trace.submission_id!=0x00440002u)continue;
                    snddrv_submit_seen=true;snddrv_submit_capture_active=true;
                    snddrv_submit_history_cursor=iop.dmac.sif0_history_cursor;
                    snddrv_submit_entry_semaphore=ee.load(0x0198cbc8u,4,false);
                    snddrv_submit_channel9=iop.dmac.channel[1];
                    break;
                }
            }
            // FILEIO's completed bind is the dependency boundary for the
            // original CDVDMAN/CDVDFSV startup.  Do that startup before
            // consuming the next SIF packet: the EE's next bind targets the
            // CDVDFSV server (0x80000592), not LOADFILE (0x80000006).
            // Deferring one service pass preserves the real packet and lets
            // its original server registration become visible first.
            profile_slice.mark(9);
            const bool bootstrap_cdvd_before_sif=!verify_services && fileio_started && !cdvd_started &&
                (reboot_count || iop.load(0x000b1008u,4,false)==0x80000003u);
            const auto sif0_history_before=iop.dmac.sif0_history_cursor;
            const auto sif1_history_before=iop.dmac.sif1_history_cursor;
            if(!bootstrap_cdvd_before_sif)while(hg::service_sif(ee,iop))++transfers;
            if(iop.dmac.sif1_history_cursor>sif1_history_before) {
                const auto first=std::max(sif1_history_before,iop.dmac.sif1_history_cursor-iop.dmac.sif1_history.size());
                for(auto cursor=first;cursor<iop.dmac.sif1_history_cursor;++cursor) {
                    const auto& record=iop.dmac.sif1_history[cursor%iop.dmac.sif1_history.size()];
                    bool has_client=false,has_rpc_call=false;
                    for(unsigned word=0;word<record.captured_words;++word) {
                        has_client|=record.payload[word]==0x019756c0u;
                        has_rpc_call|=record.payload[word]==0x8000000au;
                    }
                    if(has_client && has_rpc_call) {
                        snddrv_rpc_request_transfer=record;snddrv_rpc_request_transfer_valid=true;
                    }
                }
            }
            if(iop.dmac.sif0_history_cursor>sif0_history_before) {
                const auto first=std::max(sif0_history_before,iop.dmac.sif0_history_cursor-iop.dmac.sif0_history.size());
                for(auto cursor=first;cursor<iop.dmac.sif0_history_cursor;++cursor) {
                    const auto& record=iop.dmac.sif0_history[cursor%iop.dmac.sif0_history.size()];
                    if(record.tag[3]==0x01973ec0u) {
                        snddrv_rpc_result_transfer=record;snddrv_rpc_result_transfer_valid=true;
                    }
                    if(record.payload_words>=9 && record.payload[2]==0x80000008u &&
                       record.payload[7]==0x019756c0u && record.payload[8]==0x8000000au) {
                        snddrv_rpc_command_transfer=record;snddrv_rpc_command_transfer_valid=true;
                        if(!snddrv_ee_post_receive.valid) {
                            snddrv_ee_post_receive.valid=capture_ee_words(0x01989480u,snddrv_ee_post_receive.command);
                            const auto& channel=ee.dmac.channels[5];
                            snddrv_ee_post_receive.chcr=channel.chcr;snddrv_ee_post_receive.madr=channel.madr;
                            snddrv_ee_post_receive.qwc=channel.qwc;snddrv_ee_post_receive.tadr=channel.tadr;
                            snddrv_ee_post_receive.status=ee.dmac.status;snddrv_ee_post_receive.mask=ee.dmac.mask;
                            snddrv_ee_post_receive.completion_count=ee.dmac.sif0_completion_count;
                        }
                    }
                }
            }
            if(snddrv_submit_capture_active && iop.dmac.sif0_history_cursor>snddrv_submit_history_cursor) {
                const auto first=std::max(snddrv_submit_history_cursor,iop.dmac.sif0_history_cursor-iop.dmac.sif0_history.size());
                for(auto cursor=first;cursor<iop.dmac.sif0_history_cursor;++cursor) {
                    const auto& record=iop.dmac.sif0_history[cursor%iop.dmac.sif0_history.size()];
                    if(snddrv_submit_record_count<snddrv_submit_records.size())
                        snddrv_submit_records[snddrv_submit_record_count++]={record,std::uint32_t(ee.load(0x0198cbc8u,4,false))};
                    if(record.tag[0]==0x8009b000u && record.tag[1]==0x10u && record.tag[2]==0x90000004u && record.tag[3]==0x01989480u)
                        snddrv_submit_capture_active=false;
                }
                snddrv_submit_history_cursor=iop.dmac.sif0_history_cursor;
            }
            if(!snddrv_completion_transfer.valid && iop.dmac.sif0_history_cursor>sif0_history_before) {
                const auto first=std::max(sif0_history_before,iop.dmac.sif0_history_cursor-iop.dmac.sif0_history.size());
                for(auto cursor=first;cursor<iop.dmac.sif0_history_cursor && !snddrv_completion_transfer.valid;++cursor) {
                    const auto& record=iop.dmac.sif0_history[cursor%iop.dmac.sif0_history.size()];
                    if(!snddrv_result_transfer_valid && record.tag[3]==0x0198c9c0u) {
                        snddrv_result_transfer=record;snddrv_result_transfer_valid=true;
                    }
                    if(!snddrv_packet_transfer_valid && record.tag[0]==0x8009b000u && record.tag[1]==0x10u &&
                       record.tag[2]==0x90000004u && record.tag[3]==0x01989480u) {
                        snddrv_packet_transfer=record;snddrv_packet_transfer_valid=true;
                    }
                    if(record.payload_words<9 || record.payload[2]!=0x80000008u || record.payload[7]!=0x0198cbc0u || record.payload[8]!=0x8000000au ||
                       ee.load(0x0198cbc8u,4,false)!=10u)continue;
                    snddrv_completion_transfer.valid=true;
                    snddrv_completion_transfer.tag=record.tag;
                    snddrv_completion_transfer.payload=record.payload;
                    snddrv_completion_transfer.payload_words=record.payload_words;
                    snddrv_completion_transfer.total_words=record.total_words;
                    const auto& channel=ee.dmac.channels[5];
                    snddrv_completion_transfer.ee_chcr=channel.chcr;
                    snddrv_completion_transfer.ee_madr=channel.madr;
                    snddrv_completion_transfer.ee_qwc=channel.qwc;
                    snddrv_completion_transfer.ee_tadr=channel.tadr;
                    snddrv_completion_transfer.ee_status=ee.dmac.status;
                    snddrv_completion_transfer.ee_mask=ee.dmac.mask;
                    snddrv_completion_transfer.ee_completion_count=ee.dmac.sif0_completion_count;
                    snddrv_completion_transfer.ee_destination_valid=capture_ee_words(record.tag[3],snddrv_completion_transfer.ee_destination_words);
                }
            }
            profile_slice.mark(10);
            if(iop.dmac.completed_channels&(1u<<9)) {
                pre_sif0_irq_trace=iop.pc_trace;pre_sif0_irq_trace_cursor=iop.pc_trace_cursor;
            }
            if(clock_tick.iop_due) {
            hg::service_iop_dma_interrupt(iop,[&](hg::IopState& s,std::uint64_t count){
                if(s.pc==hg::IopRebootRequest::gateway)reboot.receive(s);
                else hg::run_iop(s,count);
            });
            hg::service_iop_cdvd_interrupt(iop,[&](hg::IopState& s,std::uint64_t count){hg::run_iop(s,count);});
            hg::service_iop_sio2_interrupt(iop,[&](hg::IopState& s,std::uint64_t count){hg::run_iop(s,count);});
            hg::service_iop_timer_interrupt(iop,[&](hg::IopState& s,std::uint64_t count){hg::run_iop(s,count);});
            }
            if(stop_on_loadfile_error) {
                const auto count=std::min(iop.dmac.sif0_history_cursor,iop.dmac.sif0_history.size());
                for(std::size_t n=0;n<count;++n) {
                    const auto& record=iop.dmac.sif0_history[(iop.dmac.sif0_history_cursor+iop.dmac.sif0_history.size()-count+n)%iop.dmac.sif0_history.size()];
                    if(record.payload_words && (record.payload[0]&0x80000000u)) {
                        stopped_on_loadfile_error=true;stopped_loadfile_result=record.payload[0];break;
                    }
                }
                if(stopped_on_loadfile_error) {
                    iop.last_sif0_error_trace=pre_sif0_irq_trace;
                    iop.last_sif0_error_trace_cursor=pre_sif0_irq_trace_cursor;
                    break;
                }
            }
            if(reboot.pending) {
                if(reboot.mode!=0 || reboot.arguments!=R"(rom0:UDNL cdrom0:\MODULES\IOPRP300.IMG;1)")
                    throw hg::IopFault(iop.pc,"unsupported IOP reboot image request: "+reboot.arguments);
                if(ee.sif->pending_main() || ee.sif->pending_sub() || (ee.dmac.channels[6].chcr&0x100))
                    throw hg::IopFault(iop.pc,"IOP reboot needs drained transport");
                // Verify and load all selected original image members before replacing
                // live IOP state. EE RAM and its CPU context survive this reset.
                hg::IopState fresh;
                hg::load_compiled_iop(fresh,modules);
                fresh.configure_cdvd_image(disc);
                configure_game_modules(fresh);
                fresh.trace_watches=iop.trace_watches;fresh.trace_watch_count=iop.trace_watch_count;
                fresh.ram_write_trace_address=iop.ram_write_trace_address;
                fresh.rtc_bcd=iop.rtc_bcd;fresh.rtc_configured=iop.rtc_configured;
                fresh.virtual_time_us=iop.virtual_time_us;
                fresh.memory_card=iop.memory_card; // The card is a peripheral; it survives IOP reboot.
                fresh.sif=ee.sif;iop=std::move(fresh);
                ee.sif->sub_address=0;ee.sif->sub_flags=0;ee.sif->iop_control=0;
                reboot={};++reboot_count;reboot_rpc_cleared=false;
                command_started=fileio_started=cdvd_started=false;
                initialize_core();
                continue;
            }
            if(!ee_dma_interrupt.active())
                ee_intc_interrupt.service(ee,[](hg::State& s,std::uint64_t count){hg::run(s,count);},clock_tick.ee_budget);
            ee_dma_interrupt.service(ee,[&](hg::State& s,std::uint64_t count){
                if(s.pc==0x0026f568u && snddrv_ee_post_receive.valid && !snddrv_ee_callback_entry.valid) {
                    snddrv_ee_callback_entry.valid=capture_ee_words(0x01989480u,snddrv_ee_callback_entry.command);
                    const auto& channel=s.dmac.channels[5];
                    snddrv_ee_callback_entry.chcr=channel.chcr;snddrv_ee_callback_entry.madr=channel.madr;
                    snddrv_ee_callback_entry.qwc=channel.qwc;snddrv_ee_callback_entry.tadr=channel.tadr;
                    snddrv_ee_callback_entry.status=s.dmac.status;snddrv_ee_callback_entry.mask=s.dmac.mask;
                    snddrv_ee_callback_entry.completion_count=s.dmac.sif0_completion_count;
                }
                hg::run(s,count);
            },clock_tick.ee_budget);
            if(verify_reboot && reboot_count==1 && reboot_rpc_cleared && cdvd_started && (iop.sif->sub_flags&0x40000)
               && ee.boot.sif_system_registers[2]==1 && iop.pc==iop_return && iop.threads.size()>=8) {
                if(!iop.interrupt_handlers[43].registered)
                    throw std::runtime_error("IOP reboot service-state invariants failed");
                std::cout<<"IOP reboot verified: checked image reload, original service entries and EESYNC completion, renewed EE/IOP RPC handshake\n";
                return 0;
            }
            if(verify && ee.boot.sif_system_registers[2]==1 && iop.pc==iop_return) {
                if((iop.system_event_bits&0x900)!=0x900 || ee.boot.sif_transfer_id!=2 || transfers!=8 ||
                   ee.sif->pending_main() || ee.sif->pending_sub() || !(ee.dmac.channels[5].chcr&0x100))
                    throw std::runtime_error("SIF handshake completion invariants failed");
                std::cout<<"Original EE/IOP SIF RPC handshake verified: two commands, reply, both DMA callbacks, event wakeups\n";
                return 0;
            }
            if(verify_cdvd && cdvd_started && ee.load(0x47bc8c,4,false)!=0) {
                const auto server=std::uint32_t(ee.load(0x47bc8c,4,false));
                if(server>=iop.ram.size() || iop.load(server,4,false)!=0x80000592)
                    throw std::runtime_error("CD/DVD RPC server bind pointer mismatch");
                std::cout<<"Original CDVDMAN/CDVDFSV startup and EE CD/DVD RPC bind verified through DMA\n";
                return 0;
            }
            if(verify_services && fileio_started && ee.load(0x198c824,4,false)!=0) {
                if(ee.load(0x198c824,4,false)!=0xb1008 || iop.load(0xb1008,4,false)!=0x80000003)
                    throw std::runtime_error("IOP heap server bind pointer mismatch");
                std::cout<<"Original FILEIO heap server registered and EE RPC bind completed through DMA\n";
                return 0;
            }
            if(verify_loadfile && iop.load(0xd6d38,4,false)!=0 && iop.load(0xd6d3c,4,false)!=0 && iop.load(0xd6d40,4,false)!=0) {
                if(iop.load(0xd6d38,4,false)!=0x80000006 || iop.load(0xd6d3c,4,false)!=0xd557c ||
                   iop.load(0xd6d40,4,false)!=0xd6d80)
                    throw std::runtime_error("LOADFILE RPC server record mismatch");
                std::cout<<"Original LOADFILE RPC server registered through its native worker\n";
                return 0;
            }
            if(!verify && !verify_services && fileio_started && !cdvd_started &&
               (reboot_count || iop.load(0x000b1008u,4,false)==0x80000003u)) {
                const auto bootstrap_cpu=iop.save_cpu();
                // Nested host bootstrap calls must not overwrite the suspended
                // initializer's stack. Allocate through the checked IOP allocator.
                const auto nested_stack=iop.allocate_thread_stack(0x2000);
                auto invoke_nested=[&](hg::IopState& state,std::uint32_t entry,std::uint32_t a0=0,std::uint32_t a1=0,std::uint32_t a2=0) {
                    invoke_iop(state,entry,a0,a1,a2,nested_stack+0x2000);
                };
                for(const auto& library:{"loadcore","sysclib","thbase","sysmem","stdio","thevent","thsemap","ioman"}) {
                    const auto name=std::string(library);
                    const auto provider=name=="loadcore"?"rom_loadcore":name=="sysclib"?"rom_sysclib":name=="sysmem"?"rom_sysmem":name=="stdio"?"rom_stdio":name=="ioman"?"rom_ioman":"rom_threadman";
                    invoke_nested(iop,loadcore+0x11d8,hg::compiled_iop_table("rom_cdvdman",library,true),hg::compiled_iop_table(provider,library,false));
                }
                iop.current_thread=1;
                invoke_nested(iop,hg::compiled_iop_module_entry("rom_cdvdman"));
                if(iop.r(2)!=0)throw hg::IopFault(iop.pc,"original CDVDMAN initialization failed");
                for(const auto& library:{"loadcore","sysclib","thbase","sysmem","stdio","sifcmd","sifman","cdvdman","thevent"}) {
                    const auto name=std::string(library);
                    const auto provider=name=="loadcore"?"rom_loadcore":name=="sysclib"?"rom_sysclib":name=="sysmem"?"rom_sysmem":name=="stdio"?"rom_stdio":name=="sifcmd"?"rom_sifcmd":name=="sifman"?"rom_sifman":name=="cdvdman"?"rom_cdvdman":"rom_threadman";
                    invoke_nested(iop,loadcore+0x11d8,hg::compiled_iop_table("rom_cdvdfsv",library,true),hg::compiled_iop_table(provider,library,false));
                }
                invoke_nested(iop,hg::compiled_iop_module_entry("rom_cdvdfsv"));
                if(iop.r(2)!=0 && iop.r(2)!=2)throw hg::IopFault(iop.pc,"original CDVDFSV initialization failed");
                if(reboot_count) {
                    for(const auto& library:{"sifman","loadcore","ioman","sysmem"}) {
                        const auto name=std::string(library);
                        const auto provider=name=="sifman"?"rom_sifman":name=="loadcore"?"rom_loadcore":name=="ioman"?"rom_ioman":"rom_sysmem";
                        invoke_nested(iop,loadcore+0x11d8,hg::compiled_iop_table("rom_eesync",library,true),hg::compiled_iop_table(provider,library,false));
                    }
                    invoke_nested(iop,hg::compiled_iop_module_entry("rom_eesync"));
                    if(iop.r(2)!=0)throw hg::IopFault(iop.pc,"original EESYNC initialization failed");
                }
                iop.current_thread=0;cdvd_started=true;iop.restore_cpu(bootstrap_cpu);iop.release_thread_stack(nested_stack,0x2000);
            }
            if(!verify && !fileio_started && ((reboot_count && command_started) || (ee.boot.sif_system_registers[2]==1 && iop.pc==iop_return))) {
                const auto bootstrap_cpu=iop.save_cpu();
                // Nested host bootstrap calls must not overwrite the suspended
                // initializer's stack. Allocate through the checked IOP allocator.
                const auto nested_stack=iop.allocate_thread_stack(0x2000);
                auto invoke_nested=[&](hg::IopState& state,std::uint32_t entry,std::uint32_t a0=0,std::uint32_t a1=0,std::uint32_t a2=0) {
                    invoke_iop(state,entry,a0,a1,a2,nested_stack+0x2000);
                };
                // Register the native reboot service through original AddCmdHandler.
                begin_iop(iop,hg::compiled_iop_module_export("rom_sifcmd","sifcmd",10),0x80000003,hg::IopRebootRequest::gateway,0,nested_stack+0x2000);
                iop.w(6,0);
                for(unsigned n=0;n<10000 && iop.pc!=iop_return;++n)hg::run_iop(iop,1);
                if(iop.pc!=iop_return)throw hg::IopFault(iop.pc,"reboot handler registration exceeded budget");
                for(const auto& library:{"sysmem","loadcore","thbase","sifman","sifcmd","thevent","stdio","thsemap","ioman"})
                    invoke_nested(iop,loadcore+0x11d8,hg::compiled_iop_table("rom_fileio",library,true),
                               hg::compiled_iop_table(std::string(library)=="sysmem"?"rom_sysmem":std::string(library)=="loadcore"?"rom_loadcore":std::string(library)=="sifman"?"rom_sifman":std::string(library)=="sifcmd"?"rom_sifcmd":std::string(library)=="stdio"?"rom_stdio":std::string(library)=="ioman"?"rom_ioman":"rom_threadman",library,false));
                for(const auto& library:{"loadcore","sysclib","thbase","sysmem"})
                    invoke_nested(iop,loadcore+0x11d8,hg::compiled_iop_table("rom_ioman",library,true),
                               hg::compiled_iop_table(std::string(library)=="loadcore"?"rom_loadcore":std::string(library)=="sysclib"?"rom_sysclib":std::string(library)=="sysmem"?"rom_sysmem":"rom_threadman",library,false));
                // Explicit native bootstrap thread, separate from scheduled FILEIO workers.
                iop.threads.push_back({hg::compiled_iop_module_entry("rom_ioman"),0x2000,96,0x02000000,0});
                iop.threads[0].initialized=true;iop.threads[0].stack_base=0x1ed000;iop.current_thread=1;
                invoke_nested(iop,hg::compiled_iop_module_entry("rom_ioman"));
                if(iop.r(2)!=0)throw hg::IopFault(iop.pc,"original IOMAN initialization failed");
                // Load the original MODLOAD/LOADFILE services before their
                // clients can issue requests; native INTRMAN imports are
                // AOT-bound.
                for(const auto& library:{"sysmem","loadcore","sysclib","stdio","ioman","thbase","thevent","thsemap"}) {
                    const auto name=std::string(library);
                    const auto provider=name=="sysmem"?"rom_sysmem":name=="loadcore"?"rom_loadcore":name=="sysclib"?"rom_sysclib":name=="stdio"?"rom_stdio":name=="ioman"?"rom_ioman":"rom_threadman";
                    invoke_nested(iop,loadcore+0x11d8,hg::compiled_iop_table("rom_modload",library,true),hg::compiled_iop_table(provider,library,false));
                }
                invoke_nested(iop,hg::compiled_iop_module_entry("rom_modload"));
                if(iop.r(2)!=0)throw hg::IopFault(iop.pc,"original MODLOAD initialization failed");
                for(const auto& library:{"sysmem","loadcore","sysclib","stdio","ioman","sifman","sifcmd","modload","thbase"}) {
                    const auto name=std::string(library);
                    const auto provider=name=="sysmem"?"rom_sysmem":name=="loadcore"?"rom_loadcore":name=="sysclib"?"rom_sysclib":name=="stdio"?"rom_stdio":name=="ioman"?"rom_ioman":name=="sifman"?"rom_sifman":name=="sifcmd"?"rom_sifcmd":name=="modload"?"rom_modload":"rom_threadman";
                    invoke_nested(iop,loadcore+0x11d8,hg::compiled_iop_table("rom_loadfile",library,true),hg::compiled_iop_table(provider,library,false));
                }
                invoke_nested(iop,hg::compiled_iop_module_entry("rom_loadfile"));
                if(iop.r(2)!=0)throw hg::IopFault(iop.pc,"original LOADFILE initialization failed");
                const auto threads_before_fileio=iop.threads.size();
                invoke_nested(iop,hg::compiled_iop_module_entry("rom_fileio"));
                if(iop.r(2)!=0 || iop.threads.size()!=threads_before_fileio+2 || !iop.threads[threads_before_fileio].ready || !iop.threads[threads_before_fileio+1].ready)
                    throw hg::IopFault(iop.pc,"original FILEIO did not start its two workers");
                iop.current_thread=0;fileio_started=true;iop.restore_cpu(bootstrap_cpu);iop.release_thread_stack(nested_stack,0x2000);

            }
        }
        if(ee.device) {
            std::cerr<<"Host device thread joins="<<ee.device->joins<<" unmasked_interrupt_joins="<<ee.device->unmasked_interrupt_joins<<'\n';
            if(const auto failure=device_owner.stop()) {
                try {std::rethrow_exception(failure);}
                catch(const hg::Fault&) {throw;}
                catch(const std::runtime_error& e) {throw hg::Fault(ee.pc,e.what());}
            }
        }
        if(realtime)std::cout<<"Host pacing: waits="<<pacing_waits<<" requested_us="<<pacing_requested_us
                            <<" actual_us="<<pacing_actual_us<<" max_late_us="<<pacing_max_late_us
                            <<" rebases="<<host_pacer.rebases()<<'\n';
        if(stopped_on_loadfile_error)std::cout<<"Stopped at original LOADFILE result "<<std::int32_t(stopped_loadfile_result)<<"; EE pc=0x";
        else std::cout<<"Cooperative startup budget reached; EE pc=0x";
        std::cout<<std::hex<<ee.pc
                 <<" IOP pc=0x"<<iop.pc<<" reboot count="<<reboot_count<<" MSFLAG=0x"<<ee.sif->main_flags
                 <<" SMFLAG=0x"<<ee.sif->sub_flags<<" heap server=0x"<<iop.load(0xb1008,4,false)
                 <<" loadfile server=0x"<<iop.load(0xd6d38,4,false)<<" IOP events=0x"<<iop.system_event_bits
                 <<std::dec<<" transport steps="<<transfers<<" IOP interrupts="<<iop.interrupts_enabled<<" locked="<<iop.locked_thread
                 <<" irq enable=0x"<<std::hex<<iop.last_interrupt_enable_pc<<" disable=0x"<<iop.last_interrupt_disable_pc<<" suspend=0x"<<iop.last_interrupt_suspend_pc
                 <<" resume=0x"<<iop.last_interrupt_resume_pc<<" token=0x"<<iop.last_interrupt_resume_token<<std::dec<<'\n';
        std::cout<<"EE wait context: v0=0x"<<std::hex<<ee.r(2)<<" v1=0x"<<ee.r(3)<<" a0=0x"<<ee.r(4)<<" a1=0x"<<ee.r(5)
                 <<" ra=0x"<<ee.r(31)<<" transfer=0x"<<ee.last_transfer_pc<<std::dec<<'\n';
        std::cout<<"EE scheduler: current="<<ee.boot.current_thread<<" ready="<<ee.boot.thread_ready
                 <<" interrupt="<<ee.boot.in_interrupt<<" dma_active="<<ee_dma_interrupt.active()<<'\n';
        std::cout<<"EE INTC: status=0x"<<std::hex<<ee.intc.status<<" mask=0x"<<ee.intc.mask
                 <<" cp0_status=0x"<<ee.cp0_status<<std::dec<<'\n';
        for(const auto& handler:ee.boot.interrupt_handlers)if(handler.callback)
            std::cout<<"EE INTC handler: cause="<<handler.cause<<" callback=0x"<<std::hex<<handler.callback
                     <<" arg=0x"<<handler.arg<<std::dec<<'\n';
        std::cout<<"EE display: configured="<<ee.gs.crtc_configured<<" interlace="<<ee.gs.crtc_interlace
                 <<" mode="<<ee.gs.crtc_mode<<" frame="<<ee.gs.crtc_frame
                 <<" pmode=0x"<<std::hex<<ee.gs.privileged_pmode<<" smode2=0x"<<ee.gs.privileged_smode2
                 <<std::dec<<'\n';
        std::cout<<"EE DECI2 diagnostics: kputs="<<ee.boot.deci2_kputs_calls
                 <<" bytes="<<ee.boot.deci2_output.size()<<'\n';
        if(!ee.boot.deci2_output.empty())
            std::cout<<"EE DECI2 retained output:\n"<<ee.boot.deci2_output<<'\n';
        std::cout<<"EE SifSetDma history:";
        const auto sif_dma_count=std::min(ee.boot.sif_dma_trace_cursor,ee.boot.sif_dma_trace.size());
        for(std::size_t n=0;n<sif_dma_count;++n) {
            const auto& record=ee.boot.sif_dma_trace[(ee.boot.sif_dma_trace_cursor+ee.boot.sif_dma_trace.size()-sif_dma_count+n)%ee.boot.sif_dma_trace.size()];
            std::cout<<" [pc=0x"<<std::hex<<record.pc;
            for(unsigned item=0;item<record.count;++item) {
                const auto& descriptor=record.descriptors[item];
                std::cout<<" {src=0x"<<descriptor[0]<<",dst=0x"<<descriptor[1]
                         <<",size=0x"<<descriptor[2]<<",attr=0x"<<descriptor[3]<<"}";
            }
            std::cout<<']';
        }
        std::cout<<std::dec<<'\n';
        const auto dump_ee_words=[&](const char* label,std::uint32_t address,unsigned words) {
            std::cout<<label<<":";
            for(unsigned word=0;word<words;++word)
                std::cout<<" 0x"<<std::hex<<ee.load(address+word*4,4,false);
            std::cout<<std::dec<<'\n';
        };
        dump_ee_words("EE FILEIO RPC client",0x0198c800u,16);
        dump_ee_words("EE DBCMAN RPC command",0x004821c0u,16);
        dump_ee_words("EE DBCMAN RPC payload",0x004822c0u,16);
        dump_ee_words("EE DBCMAN RPC tracking",0x004828f0u,16);
        dump_ee_words("EE FILEIO heap result",0x0198c840u,4);
        std::cout<<"LOADFILE server record:";for(unsigned word=0;word<4;++word)std::cout<<" 0x"<<std::hex<<iop.load(0xd6d38+word*4,4,false);std::cout<<std::dec<<'\n';
        // The native SIFRPC routines maintain two independently linked lists:
        // active queues and each queue's registered servers.  Keep the dump
        // bounded and read-only so a failed bind can be compared with the
        // already working FILEIO registration in a single diagnostic pass.
        const auto dump_iop_words=[&](const char* label,std::uint32_t address,unsigned words) {
            std::cout<<label<<":";
            for(unsigned word=0;word<words;++word)
                std::cout<<" 0x"<<std::hex<<iop.load(address+word*4,4,false);
            std::cout<<std::dec<<'\n';
        };
        dump_iop_words("SIFRPC active queues",0x0009b828u,4);
        dump_iop_words("LOADFILE RPC queue",0x000d6d20u,8);
        dump_iop_words("LOADFILE RPC server",0x000d6d38u,18);
        dump_iop_words("FILEIO RPC server",0x000b1008u,18);
        // SNDDRV's original workers register these two service records before
        // sleeping in RpcLoop.  Retain them in the ordinary connected-state
        // dump so an EE bind wait can be distinguished from missing IOP
        // registration without changing any RPC state.
        dump_iop_words("SNDDRV RPC queue 77777777",0x00092da8u,6);
        dump_iop_words("SNDDRV RPC server 77777777",0x00092dc0u,18);
        dump_iop_words("SNDDRV RPC request buffer",0x00091480u,8);
        dump_iop_words("SNDDRV RPC queue 77777778",0x00092e04u,6);
        dump_iop_words("SNDDRV RPC server 77777778",0x00092e1cu,18);
        dump_iop_words("DBCMAN RPC server 0",0x00028de0u,18);
        dump_iop_words("DBCMAN RPC server 1",0x00028e28u,18);
        dump_iop_words("DBCMAN RPC server 2",0x00028e70u,18);
        dump_iop_words("DBCMAN RPC server 3",0x00028eb4u,18);
        std::cout<<"IOP event flags:";
        for(std::size_t n=0;n<iop.event_flags.size();++n)std::cout<<" "<<n+2<<"=0x"<<std::hex<<iop.event_flags[n].bits;
        std::cout<<std::dec<<'\n';
        // Original IOMAN's bounded descriptor slots are useful provenance for
        // MODLOAD errors.  Read-only reporting does not manufacture a device
        // or descriptor; nonzero entries identify what the original close
        // path was able to allocate before it failed.
        std::cout<<"IOMAN descriptor slots:";
        for(unsigned slot=0;slot<32;++slot) {
            const auto value=iop.load(0x000a8068u+slot*16,4,false);
            if(value)std::cout<<" "<<slot<<"=0x"<<std::hex<<value;
        }
        std::cout<<std::dec<<'\n';
        std::cout<<"IOMAN drivers:";
        std::uint32_t driver=iop.load(0x000a805cu,4,false);
        for(unsigned count=0;driver && count<16;++count) {
            const auto descriptor=iop.load(driver+4,4,false),name=descriptor?iop.load(descriptor,4,false):0;
            std::cout<<" [0x"<<std::hex<<driver<<" ";
            for(unsigned n=0;name && n<32;++n) {
                const auto byte=iop.load(name+n,1,false);if(!byte)break;
                std::cout<<(byte>=0x20 && byte<=0x7e?char(byte):'?');
            }
            std::cout<<']';driver=iop.load(driver,4,false);
        }
        std::cout<<std::dec<<'\n';
        std::cout<<"IOP event trace:";
        const auto event_count=std::min(iop.event_trace_cursor,iop.event_trace.size());
        for(std::size_t n=0;n<event_count;++n) {
            const auto& item=iop.event_trace[(iop.event_trace_cursor+ iop.event_trace.size()-event_count+n)%iop.event_trace.size()];
            std::cout<<" "<<item.operation<<"@0x"<<std::hex<<item.pc<<"/0x"<<item.return_pc<<" id="<<item.id
                     <<" bits=0x"<<item.bits<<" 0x"<<item.before<<"->0x"<<item.after;
        }
        std::cout<<std::dec<<'\n';
        std::cout<<"IOP DMA: ch3=0x"<<std::hex<<iop.dmac.channel[3][2]<<" madr3=0x"<<iop.dmac.channel[3][0]
                 <<" bcr3=0x"<<iop.dmac.channel[3][1]<<" ch9=0x"<<iop.dmac.channel[1][2]<<" ch10=0x"<<iop.dmac.channel[2][2]
                 <<" completed=0x"<<iop.dmac.completed_channels<<" mask=0x"<<iop.interrupt_mask
                 <<" arm9=0x"<<iop.last_sif0_arm_pc<<"/0x"<<iop.last_sif0_arm_return_pc
                 <<" FIFOs="<<std::dec<<iop.sif->pending_main()<<"/"<<iop.sif->pending_sub()<<'\n';
        std::cout<<"IOP CDVD MMIO:";
        const auto cdvd_mmio_count=std::min(iop.cdvd_mmio_trace_cursor,iop.cdvd_mmio_trace.size());
        for(std::size_t n=0;n<cdvd_mmio_count;++n) {
            const auto& item=iop.cdvd_mmio_trace[(iop.cdvd_mmio_trace_cursor+iop.cdvd_mmio_trace.size()-cdvd_mmio_count+n)%iop.cdvd_mmio_trace.size()];
            std::cout<<" "<<(item.write?'W':'R')<<"@0x"<<std::hex<<item.pc<<"[0x"<<item.address<<"/"<<unsigned(item.size)<<"]=0x"<<item.value<<" x"<<std::dec<<item.count;
        }
        std::cout<<std::dec<<'\n';
        std::cout<<"IOP SIF0 last tag:";for(const auto word:iop.dmac.last_sif0_tag)std::cout<<" 0x"<<std::hex<<word;
        std::cout<<" payload:";for(unsigned n=0;n<iop.dmac.last_sif0_payload_words;++n)std::cout<<" 0x"<<iop.dmac.last_sif0_payload[n];std::cout<<std::dec<<'\n';
        std::cout<<"IOP SIF0 tag history:";
        const auto tag_count=std::min(iop.dmac.sif0_history_cursor,iop.dmac.sif0_history.size());
        for(std::size_t n=0;n<tag_count;++n) {
            const auto& record=iop.dmac.sif0_history[(iop.dmac.sif0_history_cursor+iop.dmac.sif0_history.size()-tag_count+n)%iop.dmac.sif0_history.size()];
            std::cout<<" [";for(const auto word:record.tag)std::cout<<" 0x"<<std::hex<<word;
            std::cout<<";";for(unsigned word=0;word<record.payload_words;++word)std::cout<<" 0x"<<record.payload[word];
            if(record.truncated)std::cout<<" ... (words=0x"<<record.total_words<<")";
            std::cout<<"]";
        }
        std::cout<<std::dec<<'\n';
        std::cout<<"IOP SIF1 history:";
        const auto receive_count=std::min(iop.dmac.sif1_history_cursor,iop.dmac.sif1_history.size());
        for(std::size_t n=0;n<receive_count;++n) {
            const auto& record=iop.dmac.sif1_history[(iop.dmac.sif1_history_cursor+iop.dmac.sif1_history.size()-receive_count+n)%iop.dmac.sif1_history.size()];
            std::cout<<" [0x"<<std::hex<<record.address<<" words=0x"<<record.count<<";";
            for(unsigned word=0;word<record.captured_words;++word)std::cout<<" 0x"<<record.payload[word];
            if(record.truncated)std::cout<<" ...";
            std::cout<<"]";
        }
        std::cout<<std::dec<<'\n';
        std::cout<<"IOP PC history:";
        const auto pc_count=std::min(iop.pc_trace_cursor,iop.pc_trace.size());
        for(std::size_t n=0;n<pc_count;++n) {
            const auto& record=iop.pc_trace[(iop.pc_trace_cursor+iop.pc_trace.size()-pc_count+n)%iop.pc_trace.size()];
            std::cout<<" [0x"<<std::hex<<record.pc<<" ra=0x"<<record.return_pc
                     <<" a0=0x"<<record.a0<<" a1=0x"<<record.a1<<" a2=0x"<<record.a2<<" a3=0x"<<record.a3
                     <<" v0=0x"<<record.v0<<" s0=0x"<<record.s0<<']';
        }
        std::cout<<std::dec<<'\n';
        std::cout<<"IOP 0x"<<std::hex<<iop.ram_write_trace_address<<" write trace:";
        const auto ram_write_count=std::min(iop.ram_write_trace_cursor,iop.ram_write_trace.size());
        for(std::size_t n=0;n<ram_write_count;++n) {
            const auto& record=iop.ram_write_trace[(iop.ram_write_trace_cursor+iop.ram_write_trace.size()-ram_write_count+n)%iop.ram_write_trace.size()];
            std::cout<<" [pc=0x"<<std::hex<<record.pc<<" ra=0x"<<record.return_pc<<" addr=0x"<<record.address
                     <<" size="<<std::dec<<record.size<<" thread="<<record.thread
                     <<" 0x"<<std::hex<<record.before<<"->0x"<<record.after<<']';
        }
        std::cout<<std::dec<<'\n';
        const auto sifman_submit_count=std::min(iop.sifman_submit_trace_cursor,iop.sifman_submit_traces.size());
        for(std::size_t submit=0;submit<sifman_submit_count;++submit) {
            const auto& trace=iop.sifman_submit_traces[(iop.sifman_submit_trace_cursor+iop.sifman_submit_traces.size()-sifman_submit_count+submit)%iop.sifman_submit_traces.size()];
            if(!trace.valid)continue;
            std::cout<<"SIFMAN submission: pc=0x"<<std::hex<<trace.pc<<" ra=0x"<<trace.return_pc
                     <<" ptr=0x"<<trace.descriptor<<" count=0x"<<trace.count<<" a2=0x"<<trace.a2<<" a3=0x"<<trace.a3
                     <<" thread="<<std::dec<<trace.thread<<" ch9={madr=0x"<<std::hex<<trace.channel9[0]
                     <<",bcr=0x"<<trace.channel9[1]<<",chcr=0x"<<trace.channel9[2]<<",tadr=0x"<<trace.channel9[3]
                     <<"} completed=0x"<<trace.completed_channels;
            if(trace.descriptors_valid) {
                std::cout<<" descriptors:";
                for(std::size_t word=0;word<std::size_t(trace.count)*4;++word)std::cout<<" 0x"<<trace.descriptors[word];
            } else std::cout<<" descriptors=<outside IOP RAM>";
            if(trace.arm_seen) {
                std::cout<<" arm={madr=0x"<<trace.arm_channel9[0]<<",bcr=0x"<<trace.arm_channel9[1]
                         <<",chcr=0x"<<trace.arm_channel9[2]<<",tadr=0x"<<trace.arm_channel9[3]<<"}";
                if(trace.arm_tags_valid) {
                    std::cout<<" tags:";for(std::size_t word=0;word<std::size_t(trace.count)*4;++word)std::cout<<" 0x"<<trace.arm_tags[word];
                } else std::cout<<" tags=<outside IOP RAM>";
            } else std::cout<<" arm=<not observed>";
            if(trace.return_seen)std::cout<<" id=0x"<<trace.submission_id;
            else std::cout<<" id=<not observed>";
            std::cout<<std::dec<<'\n';
        }
        if(iop.last_sifman_two_descriptor_trace.valid) {
            const auto& trace=iop.last_sifman_two_descriptor_trace;
            std::cout<<"Last SIFMAN two-descriptor submission: pc=0x"<<std::hex<<trace.pc<<" ra=0x"<<trace.return_pc
                     <<" ptr=0x"<<trace.descriptor<<" thread="<<std::dec<<trace.thread
                     <<" ch9={madr=0x"<<std::hex<<trace.channel9[0]<<",bcr=0x"<<trace.channel9[1]
                     <<",chcr=0x"<<trace.channel9[2]<<",tadr=0x"<<trace.channel9[3]<<"} completed=0x"<<trace.completed_channels
                     <<" descriptors:";
            for(const auto word:trace.descriptors)std::cout<<" 0x"<<word;
            if(trace.arm_seen) {
                std::cout<<" arm={madr=0x"<<trace.arm_channel9[0]<<",bcr=0x"<<trace.arm_channel9[1]
                         <<",chcr=0x"<<trace.arm_channel9[2]<<",tadr=0x"<<trace.arm_channel9[3]<<"}";
                if(trace.arm_tags_valid){std::cout<<" tags:";for(const auto word:trace.arm_tags)std::cout<<" 0x"<<word;}
                else std::cout<<" tags=<outside IOP RAM>";
            } else std::cout<<" arm=<not observed>";
            if(trace.return_seen)std::cout<<" id=0x"<<trace.submission_id;
            else std::cout<<" id=<not observed>";
            std::cout<<std::dec<<'\n';
        }
        std::cout<<"SIFMAN return timing:";
        for(std::size_t n=0;n<sifman_submit_timing_count;++n)
            std::cout<<" [id=0x"<<std::hex<<sifman_submit_timings[n].submission_id<<std::dec
                     <<" sema="<<sifman_submit_timings[n].ee_semaphore<<" time="<<sifman_submit_timings[n].time<<']';
        std::cout<<'\n';
        std::cout<<"SIFCMD 0x99964 timing:";
        for(std::size_t n=0;n<send_return_timing_count;++n) {
            const auto& snapshot=send_return_timings[n];
            std::cout<<" [v0=0x"<<std::hex<<snapshot.trace.v0<<" ra=0x"<<snapshot.trace.return_pc
                     <<" s0=0x"<<snapshot.trace.s0<<std::dec<<" sema="<<snapshot.ee_semaphore
                     <<" time="<<snapshot.time<<']';
        }
        std::cout<<'\n';
        if(snddrv_submit_seen) {
            std::cout<<"SNDDRV submission 0x00440002: entry_sema="<<snddrv_submit_entry_semaphore
                     <<" ch9={madr=0x"<<std::hex<<snddrv_submit_channel9[0]<<",bcr=0x"<<snddrv_submit_channel9[1]
                     <<",chcr=0x"<<snddrv_submit_channel9[2]<<",tadr=0x"<<snddrv_submit_channel9[3]<<"}";
            for(std::size_t n=0;n<snddrv_submit_record_count;++n) {
                const auto& snapshot=snddrv_submit_records[n];
                std::cout<<" [sema="<<std::dec<<snapshot.ee_semaphore<<" tag";
                for(const auto word:snapshot.record.tag)std::cout<<" 0x"<<std::hex<<word;
                std::cout<<" payload";
                for(unsigned word=0;word<snapshot.record.payload_words;++word)std::cout<<" 0x"<<snapshot.record.payload[word];
                std::cout<<"]";
            }
            std::cout<<std::dec<<'\n';
        }
        const auto dump_snddrv_sif0_record=[&](const char* label,const hg::IopDmac::Sif0Record& record,bool valid) {
            std::cout<<label<<":";
            if(!valid){std::cout<<" <not observed>\n";return;}
            std::cout<<" tag";for(const auto word:record.tag)std::cout<<" 0x"<<std::hex<<word;
            std::cout<<" payload";for(unsigned word=0;word<record.payload_words;++word)std::cout<<" 0x"<<record.payload[word];
            std::cout<<" total=0x"<<record.total_words<<std::dec<<'\n';
        };
        dump_snddrv_sif0_record("SNDDRV result SIF0 record",snddrv_result_transfer,snddrv_result_transfer_valid);
        dump_snddrv_sif0_record("SNDDRV completion SIF0 record",snddrv_packet_transfer,snddrv_packet_transfer_valid);
        dump_snddrv_sif0_record("SNDDRV RPC result SIF0 record",snddrv_rpc_result_transfer,snddrv_rpc_result_transfer_valid);
        dump_snddrv_sif0_record("SNDDRV RPC command SIF0 record",snddrv_rpc_command_transfer,snddrv_rpc_command_transfer_valid);
        std::cout<<"SNDDRV RPC request SIF1 record:";
        if(snddrv_rpc_request_transfer_valid) {
            std::cout<<" addr=0x"<<std::hex<<snddrv_rpc_request_transfer.address<<" words=0x"<<snddrv_rpc_request_transfer.count<<" payload";
            for(unsigned word=0;word<snddrv_rpc_request_transfer.captured_words;++word)
                std::cout<<" 0x"<<snddrv_rpc_request_transfer.payload[word];
            std::cout<<" received=0x"<<snddrv_rpc_request_transfer.received_words;
        } else std::cout<<" <not observed>";
        std::cout<<std::dec<<'\n';
        const auto& event14=iop.last_event_signal[14];
        std::cout<<"IOP event14 signals: "<<iop.event_signal_count[14]<<" last=S@0x"<<std::hex<<event14.pc<<"/0x"<<event14.return_pc
                 <<" bits=0x"<<event14.bits<<" 0x"<<event14.before<<"->0x"<<event14.after<<std::dec<<'\n';
        const auto dump_ee_snddrv_receive=[&](const char* label,const EeSnddrvReceiveSnapshot& snapshot) {
            std::cout<<label<<":";
            if(!snapshot.valid){std::cout<<" <not observed>\n";return;}
            std::cout<<" ch5={chcr=0x"<<std::hex<<snapshot.chcr<<",madr=0x"<<snapshot.madr
                     <<",qwc=0x"<<snapshot.qwc<<",tadr=0x"<<snapshot.tadr<<"} dstat=0x"<<snapshot.status
                     <<" dmask=0x"<<snapshot.mask<<" complete="<<std::dec<<snapshot.completion_count<<" command";
            for(const auto word:snapshot.command)std::cout<<" 0x"<<std::hex<<word;
            std::cout<<std::dec<<'\n';
        };
        dump_ee_snddrv_receive("EE SNDDRV post-receive",snddrv_ee_post_receive);
        dump_ee_snddrv_receive("EE SNDDRV callback-entry",snddrv_ee_callback_entry);
        if(iop.last_sif0_error_trace_cursor) {
            std::cout<<"IOP PC history at LOADFILE error:";
            const auto error_pc_count=std::min(iop.last_sif0_error_trace_cursor,iop.last_sif0_error_trace.size());
            for(std::size_t n=0;n<error_pc_count;++n) {
                const auto& record=iop.last_sif0_error_trace[(iop.last_sif0_error_trace_cursor+iop.last_sif0_error_trace.size()-error_pc_count+n)%iop.last_sif0_error_trace.size()];
                std::cout<<" [0x"<<std::hex<<record.pc<<" ra=0x"<<record.return_pc
                         <<" a0=0x"<<record.a0<<" a1=0x"<<record.a1<<" a2=0x"<<record.a2<<" a3=0x"<<record.a3
                         <<" v0=0x"<<record.v0<<" s0=0x"<<record.s0<<" thread="<<std::dec<<record.thread<<std::hex<<']';
            }
            std::cout<<std::dec<<'\n';
        }
        for(std::size_t watch=0;watch<iop.trace_watch_count;++watch)if(iop.trace_watches[watch].cursor) {
            const auto& selected=iop.trace_watches[watch];
            std::cout<<"IOP PC history at watch point 0x"<<std::hex<<selected.pc<<":";
            const auto watch_count=std::min(selected.cursor,selected.history.size());
            for(std::size_t n=0;n<watch_count;++n) {
                const auto& record=selected.history[(selected.cursor+selected.history.size()-watch_count+n)%selected.history.size()];
                std::cout<<" [0x"<<std::hex<<record.pc<<" ra=0x"<<record.return_pc
                         <<" a0=0x"<<record.a0<<" a1=0x"<<record.a1<<" a2=0x"<<record.a2<<" a3=0x"<<record.a3
                         <<" v0=0x"<<record.v0<<" s0=0x"<<record.s0<<" thread="<<std::dec<<record.thread<<std::hex<<']';
            }
            std::cout<<std::dec<<'\n';
        }
        for(const auto& selected:ee.trace_watches)if(selected.cursor) {
            std::cout<<"EE PC history at watch point 0x"<<std::hex<<selected.pc<<":";
            const auto count=std::min(selected.cursor,selected.history.size());
            for(std::size_t n=0;n<count;++n) {
                const auto& record=selected.history[(selected.cursor+selected.history.size()-count+n)%selected.history.size()];
                std::cout<<" [0x"<<record.pc<<" ra=0x"<<record.ra<<" a0=0x"<<record.a0<<" a1=0x"<<record.a1
                         <<" a2=0x"<<record.a2<<" a3=0x"<<record.a3<<" v0=0x"<<record.v0<<" v1=0x"<<record.v1
                         <<" t0=0x"<<record.t0<<" t4=0x"<<record.t4<<" s0=0x"<<record.s0<<" sp=0x"<<record.sp<<']';
            }
            std::cout<<std::dec<<'\n';
        }
        std::cout<<"EE 0x198cbc8 write trace:";
        const auto ee_ram_write_count=std::min(ee.ram_write_trace_cursor,ee.ram_write_trace.size());
        for(std::size_t n=0;n<ee_ram_write_count;++n) {
            const auto& record=ee.ram_write_trace[(ee.ram_write_trace_cursor+ee.ram_write_trace.size()-ee_ram_write_count+n)%ee.ram_write_trace.size()];
            std::cout<<" [pc=0x"<<std::hex<<record.pc<<" ra=0x"<<record.ra<<" addr=0x"<<record.address
                     <<" size="<<std::dec<<record.size<<" thread="<<record.thread
                     <<" 0x"<<std::hex<<record.before<<"->0x"<<record.after<<']';
        }
        std::cout<<std::dec<<'\n';
        const auto completion_count=std::min(ee_rpc_completion_snapshot_cursor,ee_rpc_completion_snapshots.size());
        for(std::size_t n=0;n<completion_count;++n) {
            const auto& snapshot=ee_rpc_completion_snapshots[(ee_rpc_completion_snapshot_cursor+ee_rpc_completion_snapshots.size()-completion_count+n)%ee_rpc_completion_snapshots.size()];
            std::cout<<"EE SIFRPC completion snapshot packet=0x"<<std::hex<<snapshot.packet;
            if(snapshot.packet_valid)for(const auto word:snapshot.packet_words)std::cout<<" 0x"<<word;
            else std::cout<<" <outside EE RAM>";
            std::cout<<" client=0x"<<snapshot.client;
            if(snapshot.client_valid)for(const auto word:snapshot.client_words)std::cout<<" 0x"<<word;
            else std::cout<<" <outside EE RAM>";
            std::cout<<std::dec<<'\n';
        }
        if(snddrv_completion_transfer.valid) {
            std::cout<<"SNDDRV SIF0 completion transfer tag:";
            for(const auto word:snddrv_completion_transfer.tag)std::cout<<" 0x"<<std::hex<<word;
            std::cout<<" payload:";
            for(unsigned word=0;word<snddrv_completion_transfer.payload_words;++word)
                std::cout<<" 0x"<<snddrv_completion_transfer.payload[word];
            std::cout<<" total_words=0x"<<snddrv_completion_transfer.total_words
                     <<" EE={chcr=0x"<<snddrv_completion_transfer.ee_chcr
                     <<",madr=0x"<<snddrv_completion_transfer.ee_madr
                     <<",qwc=0x"<<snddrv_completion_transfer.ee_qwc
                     <<",tadr=0x"<<snddrv_completion_transfer.ee_tadr
                     <<",status=0x"<<snddrv_completion_transfer.ee_status
                     <<",mask=0x"<<snddrv_completion_transfer.ee_mask
                     <<",complete="<<std::dec<<snddrv_completion_transfer.ee_completion_count<<"}";
            if(snddrv_completion_transfer.ee_destination_valid) {
                std::cout<<" destination:";
                for(const auto word:snddrv_completion_transfer.ee_destination_words)std::cout<<" 0x"<<std::hex<<word;
            } else std::cout<<" destination=<outside EE RAM>";
            std::cout<<std::dec<<'\n';
        }
        if(0xd6d80u+12<=iop.ram.size()) {
            std::cout<<"LOADFILE request path: ";
            for(unsigned n=8;n<512;++n) {
                const auto byte=iop.load(0xd6d80u+n,1,false);if(!byte)break;
                std::cout<<(byte>=0x20 && byte<=0x7e?char(byte):'?');
            }
            std::cout<<'\n';
        }
        const auto& ee_sif0=ee.dmac.channels[5];
        std::cout<<"EE SIF0: chcr=0x"<<std::hex<<ee_sif0.chcr<<" qwc=0x"<<ee_sif0.qwc<<" madr=0x"<<ee_sif0.madr
                 <<" tadr=0x"<<ee_sif0.tadr<<" complete="<<std::dec<<ee.dmac.sif0_completion_count
                 <<" dstat=0x"<<std::hex<<ee.dmac.status<<" dmask=0x"<<ee.dmac.mask<<std::dec<<" handlers:";
        for(const auto& handler:ee.boot.dma_handlers)if(handler.callback)std::cout<<" "<<handler.cause<<"@0x"<<std::hex<<handler.callback<<std::dec;
        std::cout<<" semaphores:";
        for(std::size_t n=0;n<ee.boot.semaphores.size();++n)
            if(ee.boot.semaphores[n].maximum)std::cout<<" "<<n+1<<"="<<ee.boot.semaphores[n].count<<"/"<<ee.boot.semaphores[n].maximum;
        std::cout<<'\n';
        std::cout<<"EE syscall 0x42 table entry: 0x"<<std::hex
                 <<ee.load(hg::BootServices::table+0x42u*4u,4,false)
                 <<" native=0x"<<(hg::BootServices::gateway+0x42u*4u)<<std::dec<<'\n';
        std::cout<<"EE semaphore trace:";
        const auto semaphore_trace_count=std::min(ee.boot.semaphore_trace_cursor,ee.boot.semaphore_trace.size());
        for(std::size_t n=0;n<semaphore_trace_count;++n) {
            const auto& trace=ee.boot.semaphore_trace[(ee.boot.semaphore_trace_cursor+ee.boot.semaphore_trace.size()-semaphore_trace_count+n)%ee.boot.semaphore_trace.size()];
            if(trace.id!=10u)continue;
            std::cout<<" ["<<trace.operation<<" id="<<trace.id<<" 0x"<<std::hex<<trace.before<<"->0x"<<trace.after
                     <<" pc=0x"<<trace.pc<<" handler=0x"<<trace.handler<<std::dec<<" thread="<<trace.current_thread
                     <<" irq="<<trace.in_interrupt<<" blocked="<<trace.blocked<<']';
        }
        std::cout<<std::dec<<'\n';
        const auto dump_semaphore_sticky=[&](const char* label,const hg::BootServices::SemaphoreTrace& trace) {
            std::cout<<label<<": ["<<trace.operation<<" id="<<trace.id<<" 0x"<<std::hex<<trace.before<<"->0x"<<trace.after
                     <<" pc=0x"<<trace.pc<<" handler=0x"<<trace.handler<<std::dec<<" thread="<<trace.current_thread
                     <<" irq="<<trace.in_interrupt<<" blocked="<<trace.blocked<<"]\n";
        };
        dump_semaphore_sticky("EE last semaphore signal",ee.boot.last_semaphore_signal);
        dump_semaphore_sticky("EE last successful semaphore wait",ee.boot.last_semaphore_wait_success);
        std::cout<<"EE SNDDRV client current:";
        for(unsigned word=0;word<14;++word)std::cout<<" 0x"<<std::hex<<ee.load(0x019756c0u+word*4,4,false);
        std::cout<<std::dec<<'\n';
        const auto& ee_sif1=ee.dmac.channels[6];
        std::cout<<"EE SIF1: chcr=0x"<<std::hex<<ee_sif1.chcr<<" qwc=0x"<<ee_sif1.qwc<<" madr=0x"<<ee_sif1.madr
                 <<" tadr=0x"<<ee_sif1.tadr<<" packet="<<std::dec<<ee.dmac.sif1_packet
                 <<" end="<<ee.dmac.sif1_end<<" pending-to-IOP="<<ee.sif->pending_sub()<<'\n';
        if(ee_sif1.tadr%16==0 && std::uint64_t(ee_sif1.tadr)+16<=ee.ram.size()) {
            std::cout<<"EE SIF1 source tag:";
            for(unsigned word=0;word<4;++word)std::cout<<" 0x"<<std::hex<<ee.load(ee_sif1.tadr+word*4,4,false);
            std::cout<<std::dec<<'\n';
        }
        std::cout<<"IOP SIF1 receive: words="<<iop.dmac.receive_words<<" end="<<iop.dmac.receive_end
                 <<" irq="<<iop.dmac.receive_irq<<" completed="<<((iop.dmac.completed_channels&(1u<<10))!=0)<<'\n';
        std::cout<<"IOP timers:";for(unsigned n=0;n<iop.timers.size();++n)
            std::cout<<" "<<n<<"={count=0x"<<std::hex<<iop.timers[n].count<<",mode=0x"<<iop.timers[n].mode<<",target=0x"<<iop.timers[n].target<<"}";
        std::cout<<std::dec<<'\n';
        std::cout<<"SPU2 input: DMA completions="<<iop.spu2_dma_count<<" bytes="<<iop.spu2_dma_bytes
                 <<" raw frames="<<iop.spu2_input_frames<<" history truncated="
                 <<(iop.spu2_input_frames>iop.spu2_input_history.size()?iop.spu2_input_frames-iop.spu2_input_history.size():0)<<'\n';
        if(spu2_wav){spu2_wav->finalize();report_spu2_capture();}
        if(spu2_mix_writer){spu2_mix_writer->finalize();std::cerr<<"SPU2 mix WAV frames="<<spu2_mix_writer->frames()<<" voice ticks="<<iop.spu2_voices.ticks()<<'\n';}
        if(spu2_output){spu2_output->flush();std::cout<<"SPU2 speaker output: core="<<spu2_output_core
            <<" frames="<<spu2_output->frames()<<" underruns="<<spu2_output->underruns()
            <<" max_queued_buffers="<<spu2_output->max_queued_buffers()
            <<" input_peak="<<spu2_output->input_peak()<<" output_peak="<<spu2_output->output_peak()
            <<" gain="<<hg::HostAudioOutput::gain_numerator<<'/'<<hg::HostAudioOutput::gain_denominator<<'\n';}
        std::cout<<"IOP dispatch queue:";std::uint32_t queue=0xa54e0;
        for(unsigned n=0;n<8 && queue;iop.load(queue,4,false),++n) {
            if(queue>=iop.ram.size() || std::uint64_t(queue)+0x20>iop.ram.size()) {std::cout<<" invalid=0x"<<std::hex<<queue;break;}
            std::cout<<" [0x"<<std::hex<<queue<<" next=0x"<<iop.load(queue,4,false)
                     <<" fn=0x"<<iop.load(queue+0x18,4,false)<<" arg=0x"<<iop.load(queue+0x1c,4,false)<<"]";
            queue=iop.load(queue,4,false);
        }
        std::cout<<std::dec<<'\n';
        for(std::size_t n=0;n<ee.boot.threads.size();++n) {
            const auto& t=ee.boot.threads[n];
            std::cout<<"EE thread "<<n+1<<" entry=0x"<<std::hex<<t.entry<<" pc=0x"<<t.context.pc
                     <<" status=0x"<<t.status<<" wait=0x"<<t.wait_type<<" id=0x"<<t.wait_id
                     <<std::dec<<" priority="<<t.priority<<" initialized="<<t.initialized<<'\n';
        }
        hg::host_sampler_stop_and_write();
        if(iop.memory_card) {
            std::cerr<<"Virtual memory card commands:";
            for(unsigned c=0;c<256;++c)if(iop.memory_card->command_counts[c])
                std::cerr<<" 0x"<<std::hex<<c<<std::dec<<'='<<iop.memory_card->command_counts[c];
            std::cerr<<'\n';
        }
        report_original_frame_candidate(stopped_on_loadfile_error?"loadfile-stop":"budget");
        report_gs_profile();
        dump_iop_state(stopped_on_loadfile_error?"loadfile-stop":"budget");
        for(std::size_t n=0;n<iop.threads.size();++n) {
            const auto& t=iop.threads[n];
            std::cout<<"IOP thread "<<n+1<<" entry=0x"<<std::hex<<t.entry<<" pc=0x"<<t.context.pc
                     <<" ra=0x"<<t.context.gpr[31]<<" v0=0x"<<t.context.gpr[2]<<" a0=0x"<<t.context.gpr[4]
                     <<" s0=0x"<<t.context.gpr[16]<<" s3=0x"<<t.context.gpr[19]
                     <<" s4=0x"<<t.context.gpr[20]<<std::dec<<" priority="<<t.priority
                     <<" ready="<<t.ready<<" sleeping="<<t.sleeping<<" delayed="<<t.delayed<<" deadline="<<t.wake_deadline<<'\n';
            if(t.context.pc==0xa1360 || t.context.pc==0xa1260 || t.context.pc==0xd22c4)
                std::cout<<"  native call a0=0x"<<std::hex<<t.context.gpr[4]<<" a1=0x"<<t.context.gpr[5]<<" a2=0x"<<t.context.gpr[6]<<" a3=0x"<<t.context.gpr[7]<<std::dec<<'\n';
        }
        return 2;
    }catch(const hg::IopFault& e){device_owner.stop_reporting(nullptr);if(host&&host->stopped)host->stopped(e.what());dump_iop_state();if(spu2_wav){spu2_wav->finalize();report_spu2_capture();}
        const auto trace_count=std::min(iop.pc_trace_cursor,iop.pc_trace.size());
        for(std::size_t n=0;n<trace_count;++n) {
            const auto& r=iop.pc_trace[(iop.pc_trace_cursor+iop.pc_trace.size()-trace_count+n)%iop.pc_trace.size()];
            std::cerr<<"Fault IOP trace pc=0x"<<std::hex<<r.pc<<" ra=0x"<<r.return_pc<<" sp=0x"<<r.sp<<" thread="<<std::dec<<r.thread<<'\n';
        }
        std::cerr<<"Display mode: configured="<<ee.gs.crtc_configured<<" interlace="<<ee.gs.crtc_interlace
                 <<" mode="<<ee.gs.crtc_mode<<" frame="<<ee.gs.crtc_frame<<'\n';std::cerr<<"IOP stopped at 0x"<<std::hex<<e.pc<<": "<<e.what()
        <<"; a0=0x"<<iop.r(4)<<" a1=0x"<<iop.r(5)<<" a2=0x"<<iop.r(6)<<" sp=0x"<<iop.r(29)<<" ra=0x"<<iop.r(31)<<" SIF0 TADR=0x"<<iop.dmac.channel[1][3]<<" MADR=0x"<<iop.dmac.channel[1][0]<<'\n';
        if(std::string(e.what()).find("worker stacks exhausted")!=std::string::npos) {
            std::cerr<<"IOP worker stacks top=0x"<<iop.thread_stack_top;
            for(std::size_t n=0;n<iop.threads.size();++n) {
                const auto& t=iop.threads[n];
                std::cerr<<" ["<<std::dec<<n+1<<std::hex<<" entry=0x"<<t.entry<<" size=0x"<<t.stack_size
                         <<" base=0x"<<t.stack_base<<" initialized="<<t.initialized<<"]";
            }
            std::cerr<<'\n';
        }
        const auto tag=iop.dmac.channel[1][3];
        if(tag%4==0 && std::uint64_t(tag)+16<=iop.ram.size()) {
            std::cerr<<"IOP source tag:";for(unsigned n=0;n<4;++n)std::cerr<<" 0x"<<std::hex<<iop.load(tag+n*4,4,false);
            std::cerr<<" SMFLAG=0x"<<iop.sif->sub_flags<<'\n';
        }
    }
    catch(const hg::Fault& e){device_owner.stop_reporting(e.what());if(host&&host->stopped)host->stopped(e.what());dump_iop_state();if(spu2_wav){spu2_wav->finalize();report_spu2_capture();}std::cerr<<"EE stopped at 0x"<<std::hex<<e.pc<<hg::describe_ee_pc(std::uint32_t(e.pc))<<": "<<e.what()
        <<"; v0=0x"<<ee.r(2)<<" a0=0x"<<ee.r(4)<<" a1=0x"<<ee.r(5)<<" a2=0x"<<ee.r(6)
        <<" t0=0x"<<ee.r(8)<<" sp=0x"<<ee.r(29)<<" ra=0x"<<ee.r(31)<<'\n';
        if(ee.vif1.trace_vifcodes) { // HG-DIAG-116
            const auto count=std::min(ee.vif1.vif_trace_cursor,ee.vif1.vif_trace.size());
            for(std::size_t n=0;n<count;++n) {
                const auto& t=ee.vif1.vif_trace[(ee.vif1.vif_trace_cursor-count+n)%ee.vif1.vif_trace.size()];
                std::cerr<<"VIF trace "<<std::dec<<n<<" code=0x"<<std::hex<<t.code<<" dest=0x"<<t.destination
                    <<" cycle=0x"<<t.cycle<<" mode="<<t.mode<<" mask=0x"<<t.mask<<" tops=0x"<<t.tops<<" top=0x"<<t.top
                    <<" base=0x"<<t.base<<" offset=0x"<<t.offset<<" dbf="<<t.dbf<<std::dec<<'\n';
            }
        }
        std::cerr<<"EE fault scheduler current="<<std::dec<<ee.boot.current_thread<<" interrupt="<<ee.boot.in_interrupt
                 <<" dma="<<ee_dma_interrupt.active()<<" intc="<<ee_intc_interrupt.active()<<'\n';
        // HG-DIAG-001: exit-only arithmetic evidence; no normal-path sampling.
        for(unsigned n=0;n<32;++n)
            std::cerr<<"VF"<<n<<"=0x"<<std::hex<<ee.vu0.vf[n].hi<<':'<<ee.vu0.vf[n].lo<<std::dec<<'\n';
        std::cerr<<"VU0 ACC";
        for(const auto value:ee.vu0.acc)std::cerr<<" 0x"<<std::hex<<value;
        std::cerr<<" MAC=0x"<<ee.vu0.mac<<" status=0x"<<ee.vu0.status<<std::dec<<'\n';
        for(std::size_t n=0;n<ee.boot.threads.size();++n) {
            const auto& t=ee.boot.threads[n];
            std::cerr<<"EE fault thread "<<n+1<<" status="<<t.status<<" wait="<<t.wait_type<<" id="<<t.wait_id
                     <<" pc=0x"<<std::hex<<t.context.pc<<std::dec<<'\n';
        }
        if(std::uint32_t(ee.r(3))==0x77 && ee.r(5)<=4 && ee.r(4)+ee.r(5)*16<=ee.ram.size())
            for(unsigned n=0;n<ee.r(5);++n){std::cerr<<"DMA descriptor "<<n;for(unsigned j=0;j<4;++j)std::cerr<<" 0x"<<std::hex<<ee.load(std::uint32_t(ee.r(4))+n*16+j*4,4,false);std::cerr<<'\n';}
    }
    catch(const std::exception& e){device_owner.stop_reporting(e.what());if(host&&host->stopped)host->stopped(e.what());if(spu2_wav){try{spu2_wav->finalize();report_spu2_capture();}catch(...) {}}std::cerr<<e.what()<<'\n';}
    report_speaker("fault");
    return 2;
}
