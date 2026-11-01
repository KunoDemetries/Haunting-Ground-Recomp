#pragma once
#include "hg/gs.hpp"
#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <utility>

namespace hg {

// HG-ARCH-002: first-party renderer-neutral PSMT8 draw *preflight*.
// This is NOT a native rasterizer or a proof of texture/VRAM non-aliasing.
// The GS frontend still validates packets, CLUT loads, faults and render ordering.
struct NativePaletteSnapshot {
    std::uint64_t generation=0;
    // HG-ARCH-010: process-wide unique snapshot identity for GPU residency;
    // generation alone is per-GS-instance and cannot key a shared cache.
    std::uint64_t identity=0;
    std::array<std::uint32_t,256> rgba{};
};
inline std::uint64_t native_next_palette_identity() noexcept {
    static std::atomic<std::uint64_t> next{0};
    return ++next;
}

// Cache this by GS clut_load_count at the future rendering seam. A successful
// palette load has already drained prior draws, read the original VRAM source,
// and populated these independently validated, host-endian 16-bit halves.
inline std::shared_ptr<const NativePaletteSnapshot> native_psmt8_palette_snapshot(
        const GsRegisterState& gs) {
    NativePaletteSnapshot snapshot;
    snapshot.generation=gs.clut_load_count;
    for(unsigned i=0;i<256;++i) {
        if(!gs.clut_valid[i]||!gs.clut_valid[i+256])return {};
        snapshot.rgba[i]=std::uint32_t(gs.clut[i])|(std::uint32_t(gs.clut[i+256])<<16);
    }
    snapshot.identity=native_next_palette_identity();
    return std::make_shared<const NativePaletteSnapshot>(std::move(snapshot));
}

struct NativeIndexedDraw {
    // Snapshotted draw data: never retains a borrowed GsDraw or mutable GS register pointer.
    std::uint64_t original_order=0;
    std::uint64_t palette_generation=0;
    std::array<GsVertex,3> vertex{};
    std::uint64_t xyoffset=0;
    std::uint64_t frame=0,zbuf=0,tex0=0,clamp=0,test=0,alpha=0,scissor=0;
    std::uint64_t texa=0,colclamp=0,fba=0,pabe=0;
    std::uint16_t prim_state=0;
    std::uint8_t primitive=0;
    std::shared_ptr<const NativePaletteSnapshot> palette;
    // Explicit safety fence: no draw may execute on native GL until a separate
    // independently verified VRAM source/destination alias and ordering proof exists.
    bool requires_vram_hazard_proof=true;
    bool requires_native_pixel_parity_proof=true;
};

// A conservative original-state eligibility gate, not GPU execution permission.
// Presently only PSMT8+CT32 framebuffer, CSM1/CT32 CLUT, alpha-test
// NOTEQUAL or GEQUAL with AFAIL KEEP, depth-tested, no blend/DATE/FBMSK/fog/AA.
// The future backend must additionally validate texture page epochs, CLUT
// publication, precise GS pixel/depth/alpha behavior and all guest observations.
inline std::optional<NativeIndexedDraw> prepare_native_psmt8_draw(
        const GsDraw& draw,const GsDraw::Environment& regs,
        std::shared_ptr<const NativePaletteSnapshot> palette,
        std::uint64_t original_order) {
    if(!palette||draw.primitive<3||draw.primitive>5||draw.count!=3||
       !(draw.prim_state&16u)||(draw.prim_state&(32u|64u|128u)))return std::nullopt;
    const auto context=unsigned((draw.prim_state>>9)&1u);
    const auto frame=regs[0x4c+context],zbuf=regs[0x4e+context];
    const auto tex0=regs[6+context],test=regs[0x47+context];
    if(((frame>>24)&63u)!=0u||(frame>>32)!=0u)return std::nullopt;
    if(((tex0>>20)&63u)!=0x13u||((tex0>>14)&63u)==0u)return std::nullopt;
    // CT32 palette / CSM1 / CSA=0. Do not silently substitute one CLUT mode.
    if(((tex0>>51)&0x3ffu)!=0u)return std::nullopt;
    if((test&1u)==0u||(test&0x4000u)!=0u||((test>>12)&3u)!=0u)return std::nullopt;
    const auto atst=unsigned((test>>1)&7u);
    if(atst!=5u&&atst!=7u)return std::nullopt;
    if((test&0x10000u)==0u||((test>>17)&3u)<2u)return std::nullopt;
    if(((zbuf>>24)&15u)>1u||(regs[0x22]&3u)!=0u)return std::nullopt;

    NativeIndexedDraw packet;
    packet.original_order=original_order;
    packet.palette_generation=palette->generation;
    packet.vertex=draw.vertices;
    packet.xyoffset=draw.xyoffset;
    packet.frame=frame;packet.zbuf=zbuf;packet.tex0=tex0;
    packet.clamp=regs[8+context];packet.test=test;packet.alpha=regs[0x42+context];
    packet.scissor=regs[0x40+context];packet.texa=regs[0x3b];
    packet.colclamp=regs[0x46];packet.fba=regs[0x4a+context];packet.pabe=regs[0x49];
    packet.prim_state=draw.prim_state;packet.primitive=draw.primitive;
    packet.palette=std::move(palette);
    return packet;
}

// Optional *post-raster* draw-state seam. The reference/compute GS renderer
// always runs first and retains its exact faults, ordering, pixels and clocks.
// Native implementations may use this hook for shadow packet verification;
// it does NOT authorize replacing pixel execution or skipping any GS work.
struct NativeIndexedDrawObserver {
    virtual ~NativeIndexedDrawObserver()=default;
    virtual void observe_completed_draw(const GsDraw&,
        const GsDraw::Environment&,const GsRegisterState&) noexcept=0;
};
inline thread_local NativeIndexedDrawObserver* native_indexed_draw_observer=nullptr;

} // namespace hg
