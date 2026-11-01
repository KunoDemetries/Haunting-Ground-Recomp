// Host device threads for the coarse EE / device split (see device_link.hpp).
// Stage B applies VIF1 parsing and VU1 execution; stage C applies GIF packet
// assembly, GS work and host closures. Together they perform the synchronous
// path's VIF1/GIF/GS calls in exactly the original submission order.
#include "hg/device_thread.hpp"
#include "hg/gs_acceleration.hpp"
#include "hg/gs_triangle_acceleration.hpp"
#include "hg/host_sampler.hpp"
#include "hg/vif.hpp"
#include "hg/vu1_spec.hpp"
#include <algorithm>
#include <deque>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#if defined(_MSC_VER)
#include <intrin.h>
#endif
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#include <immintrin.h>
#define HG_DEVICE_PAUSE() _mm_pause()
#else
#define HG_DEVICE_PAUSE() std::this_thread::yield()
#endif

namespace hg {
namespace {
struct DeviceCall {
    std::function<void()> work;
    bool sync=false;
    bool transport=false; // Completed by stage B itself (VIF1/GIF transport state only).
    std::exception_ptr error;
    std::atomic<bool> done{false};
};

// Consumer side of one single-producer/single-consumer command ring.
// HG-DIAG-091: host wait-time totals, printed at stop only with HG_DEVICE_STATS=1.
// Clocks are read only around waits; no guest-state or ordering effect.
inline std::uint64_t now_ns() {
    return std::uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}
struct Consumer {
    std::atomic<std::uint64_t> idle_ns{0},sleeps{0},commands{0};
    std::mutex mutex;
    std::condition_variable wake;
    std::atomic<bool> sleeping{false},stopping{false},failed{false};
    std::exception_ptr error; // Written before failed is released.
    std::thread thread;
    void fail() {fail(std::current_exception());}
    void fail(std::exception_ptr failure) {
        if(!failed.load(std::memory_order_relaxed)) {
            error=failure;failed.store(true,std::memory_order_release);
        }
    }
    bool has_failed() const {return failed.load(std::memory_order_relaxed);}
    void notify() {
        if(sleeping.load(std::memory_order_seq_cst)) {
            std::lock_guard<std::mutex> lock(mutex);wake.notify_one();
        }
    }
    // execute(at) applies the command at ring position `at` and returns how many
    // entries it consumed (a command plus any inline payload entries). idle()
    // returns true when it made progress, so the ring is re-checked before sleeping.
    template<class Execute,class Idle>
    void run(std::atomic<std::uint64_t>& write_index,std::atomic<std::uint64_t>& read_index,
             Execute&& execute,Idle&& idle) {
        std::uint64_t at=0,stored=0;
        for(;;) {
            auto end=write_index.load(std::memory_order_acquire);
            if(at==end) {
                read_index.store(at,std::memory_order_release);
                if(idle())continue;
                const auto idle_start=now_ns();
                for(unsigned spin=0;spin<256 && at==end && !stopping.load(std::memory_order_relaxed);++spin) {
                    HG_DEVICE_PAUSE();end=write_index.load(std::memory_order_acquire);
                }
                if(at!=end){idle_ns+=now_ns()-idle_start;continue;}
                ++sleeps;
                sleeping.store(true,std::memory_order_seq_cst);
                if(write_index.load(std::memory_order_seq_cst)==at && !stopping.load(std::memory_order_seq_cst)) {
                    std::unique_lock<std::mutex> lock(mutex);
                    wake.wait(lock,[&] {
                        return write_index.load(std::memory_order_seq_cst)!=at||stopping.load(std::memory_order_seq_cst);
                    });
                }
                sleeping.store(false,std::memory_order_relaxed);
                idle_ns+=now_ns()-idle_start;
                if(stopping.load(std::memory_order_seq_cst) && write_index.load(std::memory_order_seq_cst)==at)break;
                continue;
            }
            commands+=end-at;
            while(at!=end) {
                at+=execute(at);
                if(at-stored>=256){read_index.store(at,std::memory_order_release);stored=at;}
            }
        }
        read_index.store(at,std::memory_order_release);
    }
    void stop() {
        stopping.store(true,std::memory_order_seq_cst);
        {std::lock_guard<std::mutex> lock(mutex);wake.notify_one();}
        if(thread.joinable())thread.join();
    }
};

// One speculative VU1 activation (see DeviceThread::dispatch). The worker runs
// the AOT program on a private copy of the state stage B predicted for it; stage B
// commits activations in original order, validating every input the copy could
// have differed in and re-executing exactly on any mismatch or fault.
inline unsigned lowest_bit(std::uint64_t bits) {
#if defined(_MSC_VER)
    unsigned long index=0;_BitScanForward64(&index,bits);return unsigned(index);
#else
    return unsigned(__builtin_ctzll(bits));
#endif
}
struct VuJob {
    struct Recorder final : GifForward {
        std::vector<std::uint32_t> words;
        void forward_qword(std::uint64_t low,std::uint64_t high) override {
            words.insert(words.end(),{std::uint32_t(low),std::uint32_t(low>>32),std::uint32_t(high),std::uint32_t(high>>32)});
        }
        void forward_words(const std::uint32_t* data,std::size_t qwords) override {words.insert(words.end(),data,data+qwords*4);}
    };
    Vif1Path work; // Persistent; vu_micro_mem refreshes by generation.
    std::uint64_t micro_generation=~std::uint64_t(0);
    Vu1MemoryWriteTracker undo; // Dispatch values for lanes the speculative run overwrites.
    Vu1State start; // Committed registers at dispatch: the prediction.
    std::uint32_t top=0;
    std::uint16_t entry=0;
    bool mscnt=false; // Entry predicted from TPC; validated against the committed TPC.
    Vu1SpecInfo info;
    // log: this run's reads/lane writes. unpack_after: VIF UNPACK lanes from this
    // dispatch until the next. dirty: every lane any run of this job wrote.
    Vu1AccessLog log,dirty;
    Vu1UnpackLog unpack_after;
    std::uint64_t seq=0,first_inflight=0; // Jobs [first_inflight,seq) were uncommitted at dispatch.
    Recorder recorder;
    GifPath gif; // Records XGKICK output; starts idle like the real path at dispatch.
    std::exception_ptr error;
    std::atomic<int> state{0}; // 0 free, 1 queued, 2 running, 3 done
};
// Every Vu1State field must be handled by DeviceThread::validate and merge.
static_assert(sizeof(void*)!=8 || sizeof(Vu1State)==1840,"Vu1State changed: update speculative VU1 validation/merge");

class DeviceThread final : public DeviceLink, GifForward {
    Vif1Path& vif1;
    GifPath& gif; // Real path, owned by stage C.
    GsRegisterState& gs;
    GifPath proxy; // Stage B's boundary-tracking proxy.
    std::function<void(bool)> context;
    GsSpriteAccelerator* sprite=gs_sprite_accelerator;
    GsTriangleAccelerator* triangle=gs_triangle_accelerator;
    Consumer b,c;
    // Stage B -> C ring (B produces).
    std::vector<DeviceCommand> c_ring=std::vector<DeviceCommand>(capacity);
    alignas(64) std::atomic<std::uint64_t> c_write{0};
    alignas(64) std::atomic<std::uint64_t> c_read{0};
    alignas(64) std::uint64_t c_produced=0,c_read_cache=0,c_published=0;
    // Sync-call completion (stage C -> EE thread).
    std::mutex done_mutex;
    std::condition_variable done_wake;
    std::atomic<bool> ee_waiting{false};
    bool stopped=false;
    std::atomic<std::uint64_t> b_job_wait_ns{0},b_inline_ns{0},b_steal_ns{0}; // HG-DIAG-091: B waiting on VU1 workers (stats only).
    std::uint64_t join_wait_ns=0,space_wait_ns=0,c_space_wait_ns=0,space_waits=0,transport_reads=0;
    void c_publish() {
        if(c_produced==c_published)return;
        c_write.store(c_produced,std::memory_order_seq_cst);c_published=c_produced;c.notify();
    }
    void c_push_raw(const DeviceCommand& command) {
        if(c_produced-c_read_cache>=capacity) {
            const auto start=now_ns();
            c_publish();
            while(c_produced-c_read.load(std::memory_order_acquire)>=capacity)std::this_thread::yield();
            c_read_cache=c_read.load(std::memory_order_acquire);
            c_space_wait_ns+=now_ns()-start;
        }
        c_ring[c_produced&mask]=command;++c_produced;
        if(c_produced-c_published>=512)c_publish();
    }
    // While speculative VU1 jobs are uncommitted, C-bound work waits behind them
    // in stage B's ordered output queue.
    void c_push(const DeviceCommand& command) {
        if(out_queue.empty())c_push_raw(command);
        else {OutEntry entry;entry.command=command;out_queue.push_back(std::move(entry));}
    }
    void forward_qword(std::uint64_t low,std::uint64_t high) override {
        c_push({low,high,std::uint32_t(DeviceKind::gif_qword),0});
    }
    using GifWords=std::vector<std::uint32_t>;
    void forward_words_owned(GifWords&& words) {
        if(words.empty())return;
        if(words.size()%4)throw std::logic_error("host GIF block is not qword aligned");
        auto block=std::make_unique<GifWords>(std::move(words));
        const auto qwords=block->size()/4;
        auto* raw=block.release();
        // One ordered ring command owns the complete contiguous payload. Stage C
        // consumes and destroys it after applying the existing GIF parser.
        c_push_raw({std::uint64_t(reinterpret_cast<std::uintptr_t>(raw)),qwords,
                    std::uint32_t(DeviceKind::gif_words),0});
    }
    void forward_words(const std::uint32_t* words,std::size_t qwords) override {
        if(!qwords)return;
        if(out_queue.empty()) {forward_words_raw(words,qwords);return;}
        OutEntry entry;entry.type=OutEntry::kind_words;entry.words.assign(words,words+qwords*4);
        out_queue.push_back(std::move(entry));
    }
    void forward_words_raw(const std::uint32_t* words,std::size_t qwords) {
        if(!qwords)return;
        GifWords block(words,words+qwords*4);
        forward_words_owned(std::move(block));
    }
    // ---- Speculative VU1 activations (HG_VU_WORKERS; HG-LEARN-077) ----
    struct OutEntry {
        enum Type:std::uint8_t {kind_command,kind_job,kind_words} type=kind_command;
        DeviceCommand command;
        std::vector<std::uint32_t> words;
    };
    std::deque<OutEntry> out_queue;
    Vu1AotExecutor executor=nullptr; // The real AOT executor.
    unsigned worker_count=0;
    std::uint64_t max_inflight=1,next_seq=0,committed_seq=0;
    std::vector<std::unique_ptr<VuJob>> slots;
    std::vector<std::thread> workers;
    std::mutex queue_mutex;
    std::condition_variable queue_wake;
    std::deque<VuJob*> job_queue;
    std::atomic<std::size_t> queued{0};
    bool workers_stopping=false;
    static constexpr std::uint32_t mac_sentinel=0xffff0000u; // Never a MAC value.
    enum Reason {reason_error,reason_memory,reason_vf,reason_misc,reason_clip,reason_status,reason_q,reason_p,reason_timing,reason_tpc,reason_count};
    // Committed end TPC per activation entry (entry/8), for MSCNT entry prediction.
    std::array<std::uint16_t,2048> tpc_after{};
    std::array<bool,2048> tpc_known{};
    bool mscnt_predicted=false;
    std::uint16_t committed_tpc=0; // R.tpc while MSCNT reads the prediction.
    // Stage B's predicted registers for the next dispatch: the committed state
    // plus inline-run init activations. view_tpc_known: view.tpc is a prediction.
    Vu1State view;
    bool view_tpc_known=false;
    bool view_exact_timing=true; // False once a worker batch was dispatched after the view.
    void record_tpc(std::uint16_t entry,std::uint16_t tpc) {tpc_after[(entry>>3)&2047]=tpc;tpc_known[(entry>>3)&2047]=true;}
    // HG-DIAG-091 counters (read by print_stats on the EE thread).
    std::array<std::atomic<std::uint64_t>,reason_count> reexecute_reasons{};
    std::atomic<std::uint64_t> spec_jobs{0},spec_valid{0},sync_activations{0},mscnt_drains{0},stolen_jobs{0},inline_jobs{0};
    static void count(std::atomic<std::uint64_t>& counter) {counter.fetch_add(1,std::memory_order_relaxed);}
    static inline thread_local DeviceThread* b_owner=nullptr;

    VuJob& slot(std::uint64_t seq) {return *slots[seq%slots.size()];}
    static void begin_spec(Vu1State& u,Vu1AccessLog& log,Vu1MemoryWriteTracker* undo=nullptr) {
        log=Vu1AccessLog{};log.undo=undo;u.access_log=&log;
        u.spec_written_vf={};u.spec_written_misc=0;
        u.spec_q_fresh=u.spec_p_fresh=false;u.spec_old_q_read=u.spec_old_p_read=false;
        u.spec_clip_shifts=0;u.spec_clip_set=false;u.spec_clip_old_read=0;
    }
    static void end_spec(Vu1State& u) {
        u.access_log=nullptr;u.spec_written_vf={};u.spec_written_misc=0;
        u.spec_q_fresh=u.spec_p_fresh=true;u.spec_old_q_read=u.spec_old_p_read=false;
        u.spec_clip_shifts=0;u.spec_clip_set=false;u.spec_clip_old_read=0;
    }
    void run_job(VuJob& job) {
        auto& w=job.work;
        job.undo.clear();
        // Status starts clear so its sticky bits are exactly this job's; VU code
        // never reads status. MAC starts at an impossible value to detect writes.
        w.vu1=job.start;begin_spec(w.vu1,job.log,&job.undo);w.vu1.mac=mac_sentinel;w.vu1.status=0;
        w.top=job.top;
        job.recorder.words.clear();job.gif=GifPath{};job.gif.forward=&job.recorder;
        job.error=nullptr;
        try {executor(w,job.entry,job.gif,gs);} catch(...) {job.error=std::current_exception();}
        w.vu1.access_log=nullptr;
    }
    void worker_loop() {
        for(;;) {
            VuJob* job=nullptr;
            {
                std::unique_lock<std::mutex> lock(queue_mutex);
                queue_wake.wait(lock,[&]{return workers_stopping||!job_queue.empty();});
                if(job_queue.empty())return;
                job=job_queue.front();job_queue.pop_front();queued.fetch_sub(1,std::memory_order_relaxed);
            }
            run_job(*job);job->state.store(3,std::memory_order_release);
        }
    }
    // Stage B runs queued jobs itself instead of only waiting.
    bool steal_job() {
        if(!queued.load(std::memory_order_relaxed))return false;
        VuJob* job=nullptr;
        {
            std::lock_guard<std::mutex> lock(queue_mutex);
            if(job_queue.empty())return false;
            job=job_queue.front();job_queue.pop_front();queued.fetch_sub(1,std::memory_order_relaxed);
        }
        const auto steal_start=stats?now_ns():0;
        run_job(*job);job->state.store(3,std::memory_order_release);count(stolen_jobs);
        if(stats)b_steal_ns.fetch_add(now_ns()-steal_start,std::memory_order_relaxed);
        return true;
    }
    void wait_job(VuJob& job) {
        for(unsigned spin=0;job.state.load(std::memory_order_acquire)!=3;) {
            if(steal_job())continue;
            const auto start=stats?now_ns():0;
            if(++spin<256)HG_DEVICE_PAUSE();else std::this_thread::yield();
            if(stats)b_job_wait_ns.fetch_add(now_ns()-start,std::memory_order_relaxed);
        }
    }
    // A predecessor's lane write reached the reader's snapshot only if a later
    // UNPACK replaced both its value and its definedness first.
    static bool overlaps(const Vu1AccessLog& writer,const Vu1AccessLog& reader,const Vu1UnpackLog& overwritten) {
        for(unsigned w=0;w<64;++w)if(reader.reads[w]&writer.writes[w]&~(overwritten.value[w]&overwritten.defined[w]))return true;
        return false;
    }
    Vu1UnpackLog unpack_between(std::uint64_t first,std::uint64_t end) {
        Vu1UnpackLog result;
        for(auto k=first;k<end;++k) {
            const auto& log=slot(k).unpack_after;
            for(unsigned w=0;w<64;++w){result.value[w]|=log.value[w];result.defined[w]|=log.defined[w];}
        }
        return result;
    }
    // Copies source's lanes in `lanes` into memory, except values (definedness)
    // a later UNPACK replaced.
    static void apply_lanes(std::array<std::uint32_t,4096>& memory,std::array<std::uint8_t,1024>& defined,
                            const Vif1Path& source,const std::array<std::uint64_t,64>& lanes,const Vu1UnpackLog& later) {
        for(unsigned word=0;word<64;++word)for(auto bits=lanes[word];bits;bits&=bits-1) {
            const auto bit=lowest_bit(bits),index=word*64+bit,vector=index>>2,lane=index&3;
            const auto mask=std::uint8_t(1u<<lane);
            if(!((later.value[word]>>bit)&1))memory[index]=source.vu_mem[index];
            if(!((later.defined[word]>>bit)&1))
                defined[vector]=std::uint8_t((defined[vector]&~mask)|(source.vu_mem_defined[vector]&mask));
        }
    }
    static std::uint64_t relative(std::uint64_t ready,std::uint64_t issue) {return ready>issue?ready-issue:0;}
    // Every input the job's predicted start state could differ in from the
    // committed state R that the original order gives it.
    int validate(VuJob& job,const Vu1State& R) {
        if(job.error)return reason_error;
        if(job.mscnt&&job.entry!=R.tpc)return reason_tpc;
        if(job.first_inflight<job.seq) {
            // Newest predecessor first, so `later` grows to unpack_between(i,job.seq).
            Vu1UnpackLog later;
            for(auto i=job.seq;i-->job.first_inflight;) {
                const auto& log=slot(i).unpack_after;
                for(unsigned w=0;w<64;++w){later.value[w]|=log.value[w];later.defined[w]|=log.defined[w];}
                if(overlaps(slot(i).dirty,job.log,later))return reason_memory;
            }
        }
        const auto& S=job.start;const auto& F=job.work.vu1;
        for(unsigned r=1;r<32;++r)for(unsigned lane=0;lane<4;++lane)if(job.info.vf_live(r,lane)) {
            if(R.vf[r][lane]!=S.vf[r][lane]||((R.vf_defined[r]^S.vf_defined[r])>>lane&1))return reason_vf;
            if(relative(R.vf_ready[r][lane],R.issue_cycle)!=relative(S.vf_ready[r][lane],S.issue_cycle))return reason_timing;
        }
        for(unsigned r=1;r<16;++r)if(job.info.misc>>r&1) {
            if(R.vi[r]!=S.vi[r])return reason_misc;
            if(relative(R.vi_ready[r],R.issue_cycle)!=relative(S.vi_ready[r],S.issue_cycle))return reason_timing;
        }
        for(unsigned lane=0;lane<4;++lane)if(job.info.misc>>(vu1_spec_acc_bit+lane)&1)
            if(R.acc[lane]!=S.acc[lane]||((R.acc_defined^S.acc_defined)>>lane&1))return reason_misc;
        if((job.info.misc>>vu1_spec_i_bit&1)&&R.i!=S.i)return reason_misc;
        if(!F.spec_clip_set&&((R.clip^S.clip)&F.spec_clip_old_read))return reason_clip;
        if(R.q_pending!=S.q_pending||R.q_cycles_remaining!=S.q_cycles_remaining||(R.q_pending&&R.pending_q!=S.pending_q))return reason_timing;
        if(R.p_pending!=S.p_pending||R.p_cycles_remaining!=S.p_cycles_remaining||(R.p_pending&&R.pending_p!=S.pending_p))return reason_timing;
        if(F.spec_old_q_read&&R.q!=S.q)return reason_q;
        if(F.spec_old_p_read&&R.p!=S.p)return reason_p;
        return -1;
    }
    // R becomes the state after the job: written registers from the job, the rest
    // from R, readiness/issue clocks rebased from the predicted to the real start.
    // Relies on the AOT contract that every VF/VI write is followed by its
    // produced_vf/produced_vi readiness update (vu_emit.py _pair_lines and the
    // fused loop's explicit readiness stores).
    static void merge(Vu1State& R,const Vu1State& S,const Vu1State& F) {
        const auto base=R.issue_cycle-S.issue_cycle;
        for(unsigned r=1;r<32;++r)for(unsigned lane=0;lane<4;++lane) {
            const auto bit=r*4+lane;
            if(!((F.spec_written_vf[bit>>6]>>(bit&63))&1))continue;
            const auto mask=std::uint8_t(1u<<lane);
            R.vf[r][lane]=F.vf[r][lane];
            R.vf_defined[r]=std::uint8_t((R.vf_defined[r]&~mask)|(F.vf_defined[r]&mask));
            R.vf_ready[r][lane]=F.vf_ready[r][lane]+base;
        }
        const auto misc=F.spec_written_misc;
        for(unsigned r=1;r<16;++r)if(misc>>r&1){R.vi[r]=F.vi[r];R.vi_ready[r]=F.vi_ready[r]+base;}
        for(unsigned lane=0;lane<4;++lane)if(misc>>(vu1_spec_acc_bit+lane)&1) {
            const auto mask=std::uint8_t(1u<<lane);
            R.acc[lane]=F.acc[lane];R.acc_defined=std::uint8_t((R.acc_defined&~mask)|(F.acc_defined&mask));
        }
        if(misc>>vu1_spec_i_bit&1)R.i=F.i;
        if((misc>>vu1_spec_q_bit&1)||S.q_pending) {
            R.q=F.q;R.pending_q=F.pending_q;R.q_pending=F.q_pending;R.q_cycles_remaining=F.q_cycles_remaining;
        }
        if((misc>>vu1_spec_p_bit&1)||S.p_pending) {
            R.p=F.p;R.pending_p=F.pending_p;R.p_pending=F.p_pending;R.p_cycles_remaining=F.p_cycles_remaining;
        }
        if(F.spec_clip_set||F.spec_clip_shifts>=4)R.clip=F.clip;
        else {
            const auto shift=6u*F.spec_clip_shifts;
            R.clip=((R.clip<<shift)|(F.clip&((1u<<shift)-1u)))&0x00ffffffu;
        }
        // Status: Z/S/U/O current bits follow the last FMAC (with MAC), I/D the
        // last DIV (Q group); sticky bits accumulate.
        const bool fmac=F.mac!=mac_sentinel,divided=(misc>>vu1_spec_q_bit&1)!=0;
        R.status=(fmac?F.status&15u:R.status&15u)|(divided?F.status&48u:R.status&48u)|((R.status|F.status)&0xfc0u)|(R.status&~0xfffu);
        if(fmac)R.mac=F.mac;
        R.tpc=F.tpc;R.issue_cycle=F.issue_cycle+base;
    }
    void flush_job_output(VuJob& job) {
        if(out_queue.empty()||out_queue.front().type!=OutEntry::kind_job)throw std::logic_error("host VU1 output queue out of order");
        out_queue.pop_front();
        if(!job.recorder.words.empty())forward_words_owned(std::move(job.recorder.words));
        while(!out_queue.empty()&&out_queue.front().type!=OutEntry::kind_job) {
            auto& entry=out_queue.front();
            if(entry.type==OutEntry::kind_words)forward_words_owned(std::move(entry.words));
            else c_push_raw(entry.command);
            out_queue.pop_front();
        }
    }
    // Commits the oldest job in original order. False after a VU1 fault, which
    // becomes stage B's failure with the exact sequential state at that point.
    bool commit_oldest() {
        auto& job=slot(committed_seq);
        wait_job(job);
        auto& R=vif1.vu1;
        const auto reason=validate(job,R);
        if(reason<0) {
            count(spec_valid);
            job.dirty.writes=job.log.writes;
            merge(R,job.start,job.work.vu1);
        } else {
            count(reexecute_reasons[std::size_t(reason)]);
            // Rebuild the exact memory at this activation: undo this job's first
            // speculative writes to recover its dispatch snapshot, then apply
            // each earlier uncommitted job's writes not overwritten by UNPACK.
            auto& w=job.work;
            for(const auto raw:job.undo.word_indices) {
                const auto index=std::size_t(raw);w.vu_mem[index]=job.undo.baseline_words[index];
            }
            for(const auto raw:job.undo.vector_indices) {
                const auto vector=std::size_t(raw);w.vu_mem_defined[vector]=job.undo.baseline_defined[vector];
            }
            for(auto i=job.first_inflight;i<job.seq;++i) {
                const auto& predecessor=slot(i);
                apply_lanes(w.vu_mem,w.vu_mem_defined,predecessor.work,predecessor.dirty.writes,unpack_between(i,job.seq));
            }
            job.dirty.writes=job.log.writes;
            w.vu1=R;begin_spec(w.vu1,job.log);
            w.top=job.top;
            job.recorder.words.clear();job.gif=GifPath{};job.gif.forward=&job.recorder;
            if(job.mscnt)job.entry=R.tpc; // MSCNT resumes at the committed TPC.
            std::exception_ptr failure;
            try {executor(w,job.entry,job.gif,gs);} catch(...) {failure=std::current_exception();}
            for(unsigned word=0;word<64;++word)job.dirty.writes[word]|=job.log.writes[word];
            end_spec(w.vu1);
            if(failure) {
                std::fprintf(stderr,"Host VU1 re-executed job fault: seq=%llu entry=0x%x mscnt=%d reason=%d committed_tpc=0x%x\n",
                             (unsigned long long)job.seq,unsigned(job.entry),int(job.mscnt),reason,unsigned(job.start.tpc));
                // Already-validated XGKICK output precedes the fault, as before.
                out_queue.pop_front();
                if(!job.recorder.words.empty())forward_words_owned(std::move(job.recorder.words));
                // Sequential state at the fault: this activation's memory and registers.
                const auto later=unpack_between(job.seq,next_seq);
                for(unsigned vector=0;vector<1024;++vector) {
                    const auto shift=(vector&15)*4,word=vector>>4;
                    if(!(((later.value[word]|later.defined[word]|job.dirty.writes[word])>>shift)&15))continue;
                    for(unsigned lane=0;lane<4;++lane)vif1.vu_mem[vector*4+lane]=w.vu_mem[vector*4+lane];
                    vif1.vu_mem_defined[vector]=w.vu_mem_defined[vector];
                }
                R=w.vu1;
                abandon_jobs();b.fail(failure);
                return false;
            }
            R=w.vu1;
        }
        // Memory: this job's lane writes, except what a later UNPACK replaced.
        apply_lanes(vif1.vu_mem,vif1.vu_mem_defined,job.work,job.dirty.writes,unpack_between(job.seq,next_seq));
        record_tpc(job.entry,R.tpc);
        flush_job_output(job);
        if(++committed_seq==next_seq)vif1_unpack_log=nullptr;
        return true;
    }
    // After a failure the original program stops: discard later jobs and output.
    void abandon_jobs() {
        for(auto seq=committed_seq+1;seq<next_seq;++seq)wait_job(slot(seq));
        out_queue.clear();committed_seq=next_seq;vif1_unpack_log=nullptr;
    }
    bool drain_jobs() {
        while(committed_seq<next_seq)if(!commit_oldest())return false;
        return !b.has_failed();
    }
    void dispatch(Vif1Path& v,std::uint16_t entry,GifPath& path,GsRegisterState& state) {
        const bool mscnt=mscnt_predicted;mscnt_predicted=false;
        if(mscnt)v.vu1.tpc=committed_tpc; // Before any commit can validate against it.
        const bool inflight=committed_seq<next_seq;
        if(!inflight){view=v.vu1;view_tpc_known=true;view_exact_timing=true;}
        Vu1SpecInfo info;
        // Init entries (0) load the next batches' inputs: in order when nothing
        // is in flight, else run inline here so later predictions include them.
        // XGKICK requires an idle GIF path, which a job assumes at its start.
        if((entry||inflight)&&path.packet_idle()&&vu1_spec_info_query)info=vu1_spec_info_query(v,entry);
        if(!info.known) {
            if(!drain_jobs())throw std::runtime_error("host VU1 activation after an earlier failure");
            if(mscnt)entry=v.vu1.tpc; // A predicted MSCNT entry: use the committed TPC.
            count(sync_activations);
            try {executor(v,entry,path,state);}
            catch(...) {
                std::fprintf(stderr,"Host VU1 in-order activation fault: entry=0x%x mscnt=%d inflight=%d\n",unsigned(entry),int(mscnt),int(inflight));
                throw;
            }
            record_tpc(entry,v.vu1.tpc);return;
        }
        while(next_seq-committed_seq>=max_inflight)
            if(!commit_oldest())throw std::runtime_error("host VU1 activation after an earlier failure");
        auto& job=slot(next_seq);
        if(job.micro_generation!=v.host_micro_generation) {
            job.work.vu_micro_mem=v.vu_micro_mem;job.micro_generation=v.host_micro_generation;
        }
        // Snapshot once, directly into the buffer the worker executes. First
        // writes are undo-logged only if exact re-execution becomes necessary.
        job.work.vu_mem=v.vu_mem;job.work.vu_mem_defined=v.vu_mem_defined;
        job.start=view;job.top=v.top;job.entry=entry;job.mscnt=mscnt;job.info=info;
        if(!view_exact_timing) {
            // An uncommitted batch runs for many cycles first: predict that every
            // register write latency has elapsed (validated at commit).
            auto& S=job.start;
            for(auto& lanes:S.vf_ready)for(auto& ready:lanes)ready=std::min(ready,S.issue_cycle);
            for(auto& ready:S.vi_ready)ready=std::min(ready,S.issue_cycle);
        }
        job.seq=next_seq;job.first_inflight=committed_seq;
        job.unpack_after=Vu1UnpackLog{};job.dirty=Vu1AccessLog{};
        vif1_unpack_log=&job.unpack_after;
        ++next_seq;count(spec_jobs);
        {OutEntry marker;marker.type=OutEntry::kind_job;out_queue.push_back(std::move(marker));}
        if(!entry) {
            // Inline init: its predicted result becomes the view (base clock 0).
            count(inline_jobs);
            const auto inline_start=stats?now_ns():0;
            run_job(job);job.state.store(3,std::memory_order_release);
            if(stats)b_inline_ns.fetch_add(now_ns()-inline_start,std::memory_order_relaxed);
            if(!job.error){merge(view,job.start,job.work.vu1);view_tpc_known=true;view_exact_timing=true;}
            else view_tpc_known=false;
            return;
        }
        view.tpc=tpc_after[(entry>>3)&2047];view_tpc_known=tpc_known[(entry>>3)&2047];view_exact_timing=false;
        job.state.store(1,std::memory_order_release);
        {std::lock_guard<std::mutex> lock(queue_mutex);job_queue.push_back(&job);queued.fetch_add(1,std::memory_order_relaxed);}
        queue_wake.notify_one();
    }
    static void dispatch_trampoline(Vif1Path& v,std::uint16_t entry,GifPath& path,GsRegisterState& state) {
        b_owner->dispatch(v,entry,path,state);
    }
    // MSCNT reads VU1 TPC next. With jobs uncommitted, predict it from the
    // view; the job validates it at commit and re-executes from the committed
    // TPC otherwise. R.tpc holds the prediction only until dispatch starts.
    static void mscnt_barrier() {
        auto& self=*b_owner;
        if(self.committed_seq<self.next_seq&&self.view_tpc_known) {
            // activate_mscnt reads the prediction; dispatch restores R.tpc first.
            self.committed_tpc=self.vif1.vu1.tpc;
            self.vif1.vu1.tpc=self.view.tpc;self.mscnt_predicted=true;return;
        }
        count(self.mscnt_drains);
        if(!self.drain_jobs())throw std::runtime_error("host VU1 activation after an earlier failure");
    }
    bool b_idle() {
        c_publish();
        if(committed_seq==next_seq)return false;
        if(slot(committed_seq).state.load(std::memory_order_acquire)==3) {commit_oldest();c_publish();return true;}
        if(!steal_job()) {
            const auto start=stats?now_ns():0;
            std::this_thread::yield();
            if(stats)b_job_wait_ns.fetch_add(now_ns()-start,std::memory_order_relaxed);
        }
        return true;
    }
    void complete(DeviceCall& call) {
        call.done.store(true,std::memory_order_seq_cst);
        if(ee_waiting.load(std::memory_order_seq_cst)) {
            std::lock_guard<std::mutex> lock(done_mutex);done_wake.notify_all();
        }
    }
    std::uint64_t execute_b(std::uint64_t at) {
        execute_b_command(ring[at&mask]);
        return 1;
    }
    void execute_b_command(const DeviceCommand& command) {
        const auto kind=DeviceKind(command.kind);
        // Host calls, transport reads and register writes observe or reset
        // VU1/VIF/GIF state: every earlier activation commits first.
        if(kind==DeviceKind::call||kind==DeviceKind::vif_register||kind==DeviceKind::gif_control)drain_jobs();
        if(kind==DeviceKind::call) {
            auto* call=reinterpret_cast<DeviceCall*>(command.a);
            if(call->transport) {
                try {call->work();} catch(...) {call->error=std::current_exception();}
                complete(*call);
            } else if(call->sync||!b.has_failed()) {c_push_raw(command);if(call->sync)c_publish();}
            else delete call;
            return;
        }
        // After a failure, the synchronous path would have stopped; apply nothing more.
        if(b.has_failed())return;
        try {
            switch(kind) {
            case DeviceKind::vif_qword:vif1.submit_qword(command.a,command.b,proxy,gs);break;
            case DeviceKind::vif_tag:vif1.submit_dma_tag(command.a,proxy,gs);break;
            case DeviceKind::vif_register:vif1.write_register(command.x,std::uint32_t(command.a));break;
            case DeviceKind::gif_qword:proxy.submit_qword(command.a,command.b,gs);break;
            case DeviceKind::gif_control:proxy.write_control(std::uint32_t(command.a));c_push(command);break;
            case DeviceKind::gs_privileged:case DeviceKind::gs_csr_events:case DeviceKind::gs_imr:
            case DeviceKind::rasterize:c_push(command);break;
            default:throw std::runtime_error("invalid host device command");
            }
        } catch(...) {
            // Earlier activations precede this failure in the original order.
            const auto failure=std::current_exception();
            drain_jobs();b.fail(failure);
        }
    }
    std::uint64_t execute_c(std::uint64_t at) {
        const auto& command=c_ring[at&mask];
        const auto kind=DeviceKind(command.kind);
        if(kind==DeviceKind::call) {
            auto* call=reinterpret_cast<DeviceCall*>(command.a);
            if(call->sync) {
                // Diagnostics, joins and dumps also run after a failure.
                try {if(call->work)call->work();} catch(...) {call->error=std::current_exception();}
                complete(*call);
                return 1;
            }
            if(!c.has_failed()) {try {call->work();} catch(...) {c.fail();}}
            delete call;return 1;
        }
        if(kind==DeviceKind::gif_words) {
            auto* raw=reinterpret_cast<GifWords*>(std::uintptr_t(command.a));
            std::unique_ptr<GifWords> words(raw);
            const auto qwords=std::size_t(command.b);
            if(!words || words->size()!=qwords*4) {
                try {throw std::logic_error("host GIF block ownership/size mismatch");} catch(...) {c.fail();}
                return 1;
            }
            if(!c.has_failed()) {
                try {gif.submit_words(words->data(),qwords,gs);} catch(...) {c.fail();}
            }
            return 1;
        }
        if(c.has_failed())return 1;
        try {
            switch(kind) {
            case DeviceKind::gif_qword:gif.submit_qword(command.a,command.b,gs);break;
            case DeviceKind::gif_control:gif.write_control(std::uint32_t(command.a));break;
            case DeviceKind::gs_privileged:gs.write_privileged(command.x,command.a);break;
            case DeviceKind::gs_csr_events:gs.privileged_csr_events|=command.x;break;
            case DeviceKind::gs_imr:gs.privileged_imr=command.a;break;
            case DeviceKind::rasterize:{
                gs.rasterize_pending_draws();
                break;}
            default:throw std::runtime_error("invalid host GS command");
            }
        } catch(...) {c.fail();}
        return 1;
    }
    // GS work precedes B's in the stream whenever both failed (B stops forwarding).
    void rethrow_failure() {
        if(c.failed.load(std::memory_order_acquire))std::rethrow_exception(c.error);
        if(b.failed.load(std::memory_order_acquire))std::rethrow_exception(b.error);
    }
    void publish_b() {
        write_index.store(produced,std::memory_order_seq_cst);published=produced;b.notify();
    }
    void wait_call(DeviceCall& call) {
        for(unsigned spin=0;spin<256;++spin) {
            if(call.done.load(std::memory_order_acquire))return;
            HG_DEVICE_PAUSE();
        }
        ee_waiting.store(true,std::memory_order_seq_cst);
        if(!call.done.load(std::memory_order_seq_cst)) {
            std::unique_lock<std::mutex> lock(done_mutex);
            done_wake.wait(lock,[&]{return call.done.load(std::memory_order_seq_cst);});
        }
        ee_waiting.store(false,std::memory_order_relaxed);
    }
    void sync_call(DeviceCall& call) {
        const bool due=raster_due;
        push(DeviceKind::call,0,std::uint64_t(reinterpret_cast<std::uintptr_t>(&call)));
        raster_due=due;
        const auto start=now_ns();
        publish_b();wait_call(call);
        join_wait_ns+=now_ns()-start;
        // HG-DIAG-091: periodic totals also from transport reads (joins are rare).
        if(stats && now_ns()>=next_stats_ns){print_stats("progress");next_stats_ns=now_ns()+500000000ull;}
    }
public:
    DeviceThread(Vif1Path& v,GifPath& g,GsRegisterState& s,std::function<void(bool)> context_switch)
        :vif1(v),gif(g),gs(s),context(std::move(context_switch)) {
        proxy.forward=this;
        // The real path may already hold a partial packet; mirror its boundary state.
        if(!gif.pending.empty())throw std::runtime_error("host device thread requires an idle GIF path");
        executor=vif1.vu1_executor;
        // HG-PERF-119: speculative VU1 workers commit in original order and
        // re-execute exactly on any mismatch (HG-LEARN-077); heavy gameplay needs
        // them to keep VU1 off the critical path. Default on with spare host
        // threads; HG_VU_WORKERS overrides (0 disables).
        if(const char* value=std::getenv("HG_VU_WORKERS"))worker_count=unsigned(std::strtoul(value,nullptr,10));
        else {
            const auto host_threads=std::thread::hardware_concurrency();
            worker_count=host_threads>=8?3u:host_threads>=6?2u:0u;
        }
        if(worker_count && executor && vu1_spec_info_query) {
            max_inflight=2ull*worker_count+1;
            if(const char* value=std::getenv("HG_VU_INFLIGHT"))max_inflight=std::max<std::uint64_t>(1,std::strtoull(value,nullptr,10));
            for(std::uint64_t n=0;n<2*max_inflight+2;++n)slots.push_back(std::make_unique<VuJob>());
            for(unsigned n=0;n<worker_count;++n)workers.emplace_back([this]{host_sampler_register_current("VU");worker_loop();});
            std::fprintf(stderr,"Host VU1 speculation workers=%u\n",worker_count);
            vif1.vu1_executor=&dispatch_trampoline;
        } else worker_count=0;
        c.thread=std::thread([this] {
            host_sampler_register_current("C"); // HG-DIAG-115 (no-op unless HG_SAMPLE_FILE).
            if(context)context(true);
            gs_sprite_accelerator=sprite;gs_triangle_accelerator=triangle;
            c.run(c_write,c_read,[this](std::uint64_t at){return execute_c(at);},[]{return false;});
            gs_triangle_accelerator=nullptr;gs_sprite_accelerator=nullptr;
            if(context)context(false);
        });
        b.thread=std::thread([this] {
            host_sampler_register_current("B");
            b_owner=this;
            if(worker_count)vif1_mscnt_barrier=&mscnt_barrier;
            b.run(write_index,read_index,[this](std::uint64_t at){return execute_b(at);},
                  [this]{return b_idle();});
            drain_jobs();
            c_publish();
            vif1_mscnt_barrier=nullptr;vif1_unpack_log=nullptr;b_owner=nullptr;
        });
    }
    ~DeviceThread() override {stop();}
    void publish_slow() override {
        publish_b();
        rethrow_failure();
    }
    void wait_space() override {
        const auto start=now_ns();
        publish_b();
        // Full ring: stage B is busy and publishes progress every 256 commands.
        while(produced-read_index.load(std::memory_order_acquire)>=capacity)std::this_thread::yield();
        read_cache=read_index.load(std::memory_order_acquire);
        space_wait_ns+=now_ns()-start;++space_waits;
    }
    bool stats=[]{const char* v=std::getenv("HG_DEVICE_STATS");return v&&v[0]=='1';}();
    std::uint64_t next_stats_ns=0;
    void print_stats(const char* label) {
        std::fprintf(stderr,"Host device stages %s: t=%.3f joins=%llu transport_reads=%llu join_wait_s=%.3f ring_full_waits=%llu ring_full_s=%.3f "
                     "c_ring_full_s=%.3f B_commands=%llu B_idle_s=%.3f B_sleeps=%llu C_commands=%llu C_idle_s=%.3f C_sleeps=%llu\n",
                     label,now_ns()/1e9,(unsigned long long)joins,(unsigned long long)transport_reads,join_wait_ns/1e9,(unsigned long long)space_waits,space_wait_ns/1e9,
                     c_space_wait_ns/1e9,(unsigned long long)b.commands.load(),b.idle_ns.load()/1e9,(unsigned long long)b.sleeps.load(),
                     (unsigned long long)c.commands.load(),c.idle_ns.load()/1e9,(unsigned long long)c.sleeps.load());
        if(worker_count) {
            const auto& r=reexecute_reasons;
            std::fprintf(stderr,"Host VU1 speculation %s: b_job_wait_s=%.3f b_inline_s=%.3f b_steal_s=%.3f workers=%u inflight=%llu jobs=%llu valid=%llu sync=%llu inline=%llu mscnt_drains=%llu stolen=%llu "
                         "reexec[error=%llu memory=%llu vf=%llu misc=%llu clip=%llu status=%llu q=%llu p=%llu timing=%llu tpc=%llu]\n",
                         label,b_job_wait_ns.load()/1e9,b_inline_ns.load()/1e9,b_steal_ns.load()/1e9,worker_count,(unsigned long long)max_inflight,(unsigned long long)spec_jobs.load(),(unsigned long long)spec_valid.load(),
                         (unsigned long long)sync_activations.load(),(unsigned long long)inline_jobs.load(),(unsigned long long)mscnt_drains.load(),(unsigned long long)stolen_jobs.load(),
                         (unsigned long long)r[0].load(),(unsigned long long)r[1].load(),(unsigned long long)r[2].load(),(unsigned long long)r[3].load(),
                         (unsigned long long)r[4].load(),(unsigned long long)r[5].load(),(unsigned long long)r[6].load(),(unsigned long long)r[7].load(),
                         (unsigned long long)r[8].load(),(unsigned long long)r[9].load());
        }
    }
    void join() override {
        ++joins;
        if(stats && now_ns()>=next_stats_ns){print_stats("progress");next_stats_ns=now_ns()+2000000000ull;}
        DeviceCall call;call.sync=true;sync_call(call);
        read_cache=read_index.load(std::memory_order_acquire);
        rethrow_failure();
    }
    void post(std::function<void()> work) override {
        const bool due=raster_due;
        auto* call=new DeviceCall;call->work=std::move(work);
        push(DeviceKind::call,0,std::uint64_t(reinterpret_cast<std::uintptr_t>(call)));
        raster_due=due;
    }
    void run_sync(const std::function<void()>& work) override {
        DeviceCall call;call.work=work;call.sync=true;sync_call(call);
        if(call.error)std::rethrow_exception(call.error);
    }
    std::uint32_t read_transport_register(std::uint32_t address,bool reverse) override {
        ++transport_reads;
        std::uint32_t value=0;
        DeviceCall call;call.sync=true;call.transport=true;
        call.work=[&] {
            if(b.failed.load(std::memory_order_acquire))std::rethrow_exception(b.error);
            value=address==0x10003020u?proxy.read_status(vif1.path3_masked,reverse):vif1.read_register(address);
        };
        sync_call(call);
        // A GS failure earlier in the stream, if already reached, is reported first.
        if(c.failed.load(std::memory_order_acquire))std::rethrow_exception(c.error);
        if(call.error)std::rethrow_exception(call.error);
        return value;
    }
    std::exception_ptr stop() override {
        if(!stopped) {
            stopped=true;
            publish_b();
            b.stop(); // Drains B, which publishes everything it forwarded.
            c.stop();
            if(!workers.empty()) {
                {std::lock_guard<std::mutex> lock(queue_mutex);workers_stopping=true;}
                queue_wake.notify_all();
                for(auto& worker:workers)worker.join();
                workers.clear();
                vif1.vu1_executor=executor;
            }
            if(stats)print_stats("final");
        }
        if(c.failed.load(std::memory_order_acquire))return c.error;
        return b.failed.load(std::memory_order_acquire)?b.error:nullptr;
    }
};
}

std::unique_ptr<DeviceLink> start_device_thread(Vif1Path& vif1,GifPath& gif,GsRegisterState& gs,
                                                std::function<void(bool)> context) {
    return std::make_unique<DeviceThread>(vif1,gif,gs,std::move(context));
}
}
