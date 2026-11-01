#pragma once
#include <cstdint>

namespace hg {
// HG-DIAG-115: opt-in host-thread stack sampler for performance attribution.
// Enabled only when HG_SAMPLE_FILE names an output file. Observes host threads
// (EE, device stages, VU workers); never reads or changes guest state, clocks,
// ordering or faults. While a target is suspended the sampler only copies its
// registers and top of stack: no allocation, locks or unwinding occur then.
// Windows x64 only; other hosts compile to no-ops.
void host_sampler_register_current(const char* name) noexcept;
void host_sampler_start_if_requested();
// Samples are recorded only while active (e.g. a fixed checkpoint window).
void host_sampler_set_active(bool active) noexcept;
bool host_sampler_enabled() noexcept;
void host_sampler_stop_and_write() noexcept;
}
