#pragma once
#include <chrono>
#include <cstdint>
#include <stdexcept>

namespace hg {
// HG-DIAG-012: host-only wall-clock deadlines for explicit --realtime viewing.
// The caller supplies elapsed modeled time; no guest clock, work budget, device
// state or input is modified here. Check at 10ms intervals (less than one NTSC
// field). Absolute deadlines absorb ordinary timer jitter and decoding bursts.
// When host work falls more than 100ms behind, discard that debt instead of speeding through
// a later, cheaper scene to catch up. Sleeping belongs to the caller so this
// policy can be tested with a deterministic clock.
class HostPlaybackPacer {
public:
    using Clock=std::chrono::steady_clock;
    static constexpr std::uint64_t interval_us=10000;
    static constexpr std::uint64_t max_lag_us=100000;
    explicit HostPlaybackPacer(Clock::time_point start):origin_(start) {}
    bool due(std::uint64_t elapsed_us) const {return elapsed_us>=next_check_us_;}
    Clock::time_point deadline(std::uint64_t elapsed_us,Clock::time_point now) {
        if(elapsed_us<last_elapsed_us_)
            throw std::invalid_argument("host pacing requires monotonic modeled time");
        last_elapsed_us_=elapsed_us;
        next_check_us_=elapsed_us+interval_us;
        auto target=origin_+std::chrono::microseconds(elapsed_us);
        if(now>target+std::chrono::microseconds(max_lag_us)) {
            origin_+=now-target;
            target=now;
            ++rebases_;
        }
        return target;
    }
    std::uint64_t rebases() const {return rebases_;}
private:
    Clock::time_point origin_;
    std::uint64_t next_check_us_=interval_us,last_elapsed_us_=0,rebases_=0;
};

// UI presentation cadence only. No guest clock, input, event or rendering work
// is changed; the existing latest-frame UI handoff coalesces until due.
class HostFrameLimiter30 {
public:
    using Clock=std::chrono::steady_clock;
    static constexpr auto period=std::chrono::nanoseconds(1000000000ll/30);
    static constexpr auto max_lag=std::chrono::milliseconds(100);
    bool due(Clock::time_point now) const noexcept {return !primed_||now>=next_;}
    void presented(Clock::time_point now) noexcept {
        if(!primed_) {next_=now+period;primed_=true;return;}
        if(now>next_+max_lag) {next_=now+period;++rebases_;return;}
        next_+=period;
    }
    void reset() noexcept {primed_=false;}
    std::uint64_t rebases() const noexcept {return rebases_;}
private:
    Clock::time_point next_{};
    bool primed_=false;
    std::uint64_t rebases_=0;
};
}
