#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace hg {
struct HostInput {
    std::uint16_t buttons=0xffff;
    std::array<std::uint8_t,4> axes{{128,128,128,128}};
    std::array<std::uint8_t,12> pressures{};
    bool operator==(const HostInput& other) const {
        return buttons==other.buttons&&axes==other.axes&&pressures==other.pressures;
    }
};
// Runtime callbacks execute on its owning thread. The host owns synchronization;
// no OpenGL calls or guest-state access cross onto the window thread.
struct NativeHost {
    std::function<bool()> closing;
    std::function<bool(HostInput&)> input;
    std::function<void(unsigned,unsigned,std::vector<std::uint32_t>&&)> present;
    std::function<void(const std::string&)> stopped;
    // Optional: make the GS OpenGL context current on (true) / release it from
    // (false) the calling thread, so a host device thread can own GS work.
    std::function<void(bool)> gs_context;
    // Optional, host-only: true once per user checkpoint request (F5), and a
    // short status notice for the window title. Neither touches guest state.
    std::function<bool()> checkpoint_requested;
    std::function<void(const std::string&)> notice;
};
int run_native_game(int argc,char** argv,NativeHost* host=nullptr);
}
