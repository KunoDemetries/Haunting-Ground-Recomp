#pragma once
#include "hg/gs_memory.hpp"
#include <array>

namespace hg {
// HG-DIAG-018: host-prepared bounded integer triangle. Words are an explicit
// uint32 SSBO wire layout, avoiding compiler-dependent struct packing.
struct GsTriangleJob {
    static constexpr std::uint32_t untextured_flag=512,native_float_stq_flag=1024;
    // HG-PERF-120: PSMT8H texel is the own pixel's frame alpha byte (identity UV).
    static constexpr std::uint32_t self_feedback_flag=2048;
    enum : unsigned {
        XY=0, LEFT=6, TOP=7, RIGHT=8, BOTTOM=9,
        FRAME=10, WIDTH=11, DEPTH=12, DEPTH_MASK=13, FRAME_MASK=14,
        FLAGS=15, ZTEST=16, ALPHA_REF=17, ALPHA=18, ALPHA_FIXED=19,
        TEXTURE=20, TEXTURE_WIDTH=21, FORMAT=22, SIZE_U=23, SIZE_V=24,
        CLAMP_LO=25, CLAMP_HI=26, ALPHA_TEST=27, FUNCTION=28, FLAT=29,
        COLORS=30, Z=33, AREA=36, S=38, T=44, Q=50, PALETTE=56, WORDS=64
    };
    std::array<std::uint32_t,WORDS> data{};
    std::array<std::uint32_t,256> palette{};
};
struct GsTriangleAccelerator {
    virtual ~GsTriangleAccelerator()=default;
    // Accepted only inside a pending-draw batch, whose caller discards the
    // scalar drawn/not-drawn result. False leaves this draw for the CPU.
    virtual bool render_triangle(const GsTriangleJob&,GsLocalMemory&)=0;
    // Conservative capability: only a backend whose relaxed shader consumes the
    // existing float STQ wire flag may accept producer-prepacked coefficients.
    virtual bool accepts_native_float_stq() const noexcept {return false;}
};
inline thread_local GsTriangleAccelerator* gs_triangle_accelerator=nullptr;
}
