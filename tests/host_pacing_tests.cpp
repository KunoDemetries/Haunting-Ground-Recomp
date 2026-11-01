#include "hg/host_pacing.hpp"
#include "hg/host_wait.hpp"
#include <algorithm>
#include <array>
#include <iostream>
#include <numeric>
#include <stdexcept>

using Pacer=hg::HostPlaybackPacer;
static auto at(std::int64_t us) {return Pacer::Clock::time_point{}+std::chrono::microseconds(us);}
static std::int64_t us(Pacer::Clock::time_point t) {
    return std::chrono::duration_cast<std::chrono::microseconds>(t.time_since_epoch()).count();
}
static void require(bool value,const char* message) {
    if(!value)throw std::runtime_error(message);
}
int main() {
    try {
        using FrameLimiter=hg::HostFrameLimiter30;
        FrameLimiter frame_limiter;
        const auto frame_start=FrameLimiter::Clock::time_point{};
        require(frame_limiter.due(frame_start),"first locked frame should be due");
        frame_limiter.presented(frame_start);
        require(!frame_limiter.due(frame_start+std::chrono::milliseconds(1)),"frame became due before 30 Hz deadline");
        require(frame_limiter.due(frame_start+FrameLimiter::period),"second locked frame missed 30 Hz deadline");
        frame_limiter.presented(frame_start+FrameLimiter::period+std::chrono::milliseconds(1));
        require(!frame_limiter.due(frame_start+FrameLimiter::period*2-std::chrono::nanoseconds(1)),
                "presentation jitter moved the absolute cadence");
        const auto stalled=frame_start+std::chrono::seconds(1);
        require(frame_limiter.due(stalled),"long-stalled frame was not due");
        frame_limiter.presented(stalled);
        require(frame_limiter.rebases()==1&&!frame_limiter.due(stalled+std::chrono::milliseconds(1)),
                "long frame stall created a catch-up burst");
        frame_limiter.reset();
        require(frame_limiter.due(stalled+std::chrono::milliseconds(1)),"toggle reset retained an old deadline");

        Pacer p(at(0));
        require(!p.due(9999) && p.due(10000),"pacing check boundary incorrect");
        require(p.deadline(10000,at(2500))==at(10000),"fast work must wait for modeled time");
        require(p.deadline(20000,at(20500))==at(20000),"small scheduling jitter changed the timeline");
        require(p.deadline(30000,at(23000))==at(30000),"absolute deadline accumulated timer drift");
        require(p.deadline(40000,at(190000))==at(190000),"slow work incorrectly requests more waiting");
        require(p.rebases()==1,"large host stall did not discard catch-up debt");
        require(p.deadline(50000,at(192000))==at(200000),"later fast work catches up through a stalled scene");
        bool rejected=false;
        try {p.deadline(49000,at(101000));}catch(const std::invalid_argument&){rejected=true;}
        require(rejected,"backwards modeled time was accepted");

        // A fake host alternates cheap and expensive work. Every modeled step
        // executes exactly once. Real sleeps are replaced by reaching a deadline
        // plus 1ms scheduling jitter; the cheap final scene must remain real time.
        Pacer simulation(at(0));std::int64_t host=0,last_scene_host=0;
        for(std::uint64_t guest=1000;guest<=12000000;guest+=1000) {
            host+=guest<=4000000?500:guest<=8000000?1300:500;
            if(simulation.due(guest)) {
                const auto target=us(simulation.deadline(guest,at(host)));
                if(target>host)host=target+1000;
            }
            if(guest==4000000)require(host>=4000000 && host<=4002000,"fast playback or jitter drift");
            if(guest==8000000)last_scene_host=host;
        }
        require(host-last_scene_host>=4000000-std::int64_t(Pacer::max_lag_us) && host-last_scene_host<=4002000,
                "later fast scene repaid unbounded timing debt");
        require(simulation.rebases()>0,"slow-work scenario was not exercised");
        // Frame decoding is bursty even when average capacity is sufficient:
        // 23ms +2ms +2ms of work per30ms modeled time. A10ms rebase threshold
        // incorrectly stretches every such frame to43ms. Preserve deadlines
        // across those temporary deficits; only sustained lag may rebase.
        Pacer burst(at(0));std::int64_t burst_host=0;
        for(std::uint64_t tick=1;tick<=300;++tick) {
            burst_host+=tick%3==1?23000:2000;
            const auto target=us(burst.deadline(tick*10000,at(burst_host)));
            if(target>burst_host)burst_host=target;
        }
        require(burst_host==3000000 && burst.rebases()==0,"decoding bursts accumulate artificial delay");
        hg::HostDeadlineWaiter disabled(false);
        rejected=false;
        try {disabled.wait_until(Pacer::Clock::now());}catch(const std::logic_error&){rejected=true;}
        require(rejected,"disabled waiter silently accepted a wait");
        hg::HostDeadlineWaiter waiter(true);
        waiter.wait_until(Pacer::Clock::now()-std::chrono::milliseconds(1));
        // Exercise actual platform wakeups as well as fake-clock policy. Timing
        // distributions are evidence, not fragile scheduler-performance asserts.
        // Both wait methods must return no earlier than the requested deadline.
        for(const bool precise:{false,true}) {
            std::array<double,32> elapsed{};
            for(auto& duration:elapsed) {
                const auto start=Pacer::Clock::now(),target=start+std::chrono::milliseconds(2);
                if(precise)waiter.wait_until(target);else std::this_thread::sleep_until(target);
                const auto end=Pacer::Clock::now();
                require(end>=target,"platform wait returned before its deadline");
                duration=std::chrono::duration<double,std::micro>(end-start).count();
            }
            std::sort(elapsed.begin(),elapsed.end());
            std::cout<<"Wait probe backend="<<(precise?waiter.backend():"std::sleep_until")
                     <<" requested_us=2000 samples="<<elapsed.size()<<" median_us="
                     <<(elapsed[15]+elapsed[16])/2<<" max_us="<<elapsed.back()
                     <<" mean_us="<<std::accumulate(elapsed.begin(),elapsed.end(),0.0)/elapsed.size()<<'\n';
        }
        std::cout<<"Host pacing: 30 Hz frame lock, deadlines, jitter, stall recovery and 12000 modeled steps passed\n";
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
