#pragma once
#include "hg/native_indexed_draw.hpp"
#include <algorithm>
#include <bitset>
#include <cstdint>
#include <limits>

namespace hg {

// HG-ARCH-004: a conservative, renderer-private page screen for native PSMT8
// candidates. False is safe rejection. True never grants GL drawing permission:
// texture page freshness, prior work fences, exact GS coverage and pixel-parity
// evidence are STILL required. No reads of VRAM or changes to GS state.
struct NativePsmt8PageFootprint {
    std::bitset<512> texture_reads{},frame_touches{},depth_touches{};
    bool supported=false;
    bool screen_has_area=false;
    bool source_disjoint_from_destinations=false;
};

inline NativePsmt8PageFootprint native_psmt8_page_footprint(const NativeIndexedDraw& draw) noexcept {
    NativePsmt8PageFootprint result;
    if(((draw.tex0>>20)&63u)!=0x13u||((draw.frame>>24)&63u)!=0u||
       ((draw.zbuf>>24)&15u)>1u||draw.primitive<3||draw.primitive>5)return result;

    const unsigned tw=unsigned((draw.tex0>>26)&15u),th=unsigned((draw.tex0>>30)&15u);
    const unsigned stride=unsigned((draw.tex0>>14)&63u)*64u;
    const unsigned frame_width=unsigned((draw.frame>>16)&63u)*64u;
    if(tw>10||th>10||!stride||!frame_width)return result;
    const unsigned u_mode=unsigned(draw.clamp&3u),v_mode=unsigned((draw.clamp>>2)&3u);
    const unsigned umin=unsigned((draw.clamp>>4)&1023u),umax=unsigned((draw.clamp>>14)&1023u);
    const unsigned vmin=unsigned((draw.clamp>>24)&1023u),vmax=unsigned((draw.clamp>>34)&1023u);
    if((u_mode==2&&umin>umax)||(v_mode==2&&vmin>vmax))return result;
    const auto maximum=[](unsigned mode,unsigned size,unsigned low,unsigned high) {
        return mode<2?size-1:mode==2?high:(low|high);
    };
    const unsigned max_u=maximum(u_mode,1u<<tw,umin,umax);
    const unsigned max_v=maximum(v_mode,1u<<th,vmin,vmax);
    // An indexed GS physical tile contains 2048 *words*, and 128x64 texels.
    // TBP0 has only 64-word alignment: a tile may cross physical 2048-word page
    // boundaries. Mark BOTH pages, even when the tile straddles VRAM wrap.
    const std::uint64_t base=std::uint64_t(draw.tex0&0x3fffull)*64u;
    for(unsigned ty=0;ty<=max_v/64;++ty)for(unsigned tx=0;tx<=max_u/128;++tx) {
        const auto page=std::uint64_t(tx)+std::uint64_t(ty)*(stride/128u);
        const auto start=unsigned((base+page*2048u)&0xfffffu);
        result.texture_reads.set(start/2048u);
        result.texture_reads.set(((start+2047u)&0xfffffu)/2048u);
    }
    // The GS tests pixel centers at integer-pixel positions (16 fixed steps).
    // Over-include the clipped integer bounding rectangle; never use a guessed
    // triangle interior or omit Z reads. All 3 supplied vertices are already
    // the full original triangle record.
    const auto ox=std::int64_t(draw.xyoffset&0xffffu);
    const auto oy=std::int64_t((draw.xyoffset>>32)&0xffffu);
    std::int64_t xmin=std::numeric_limits<std::int64_t>::max(),xmax=std::numeric_limits<std::int64_t>::min();
    std::int64_t ymin=std::numeric_limits<std::int64_t>::max(),ymax=std::numeric_limits<std::int64_t>::min();
    for(const auto& v:draw.vertex) {
        const auto x=std::int64_t(v.x)-ox,y=std::int64_t(v.y)-oy;
        xmin=std::min(xmin,x);xmax=std::max(xmax,x);
        ymin=std::min(ymin,y);ymax=std::max(ymax,y);
    }
    const auto floor16=[](std::int64_t a) {return a>=0?a/16:-((-a+15)/16);};
    const auto ceil16=[](std::int64_t a) {return a>=0?(a+15)/16:-((-a)/16);};
    const auto left=std::max({std::int64_t(0),ceil16(xmin),std::int64_t(draw.scissor&0x7ffu)});
    const auto top=std::max({std::int64_t(0),ceil16(ymin),std::int64_t((draw.scissor>>32)&0x7ffu)});
    const auto right=std::min({std::int64_t(2048),floor16(xmax)+1,
                              std::int64_t((draw.scissor>>16)&0x7ffu)+1});
    const auto bottom=std::min({std::int64_t(2048),floor16(ymax)+1,
                               std::int64_t((draw.scissor>>48)&0x7ffu)+1});
    result.supported=true;
    if(left>=right||top>=bottom)return result; // Nothing can be rasterized.
    result.screen_has_area=true;
    // CT32 FRAME and Z32/Z24 use distinct tile layouts. Both use whole
    // 2048-word physical pages with 64x32 pixels. Mark all possible writes
    // and depth tests (even when the Z write-mask is enabled).
    const unsigned frame_base=unsigned(draw.frame&511u)*2048u;
    const unsigned depth_base=unsigned(draw.zbuf&511u)*2048u;
    for(auto ty=unsigned(top/32);ty<=unsigned((bottom-1)/32);++ty)
        for(auto tx=unsigned(left/64);tx<=unsigned((right-1)/64);++tx) {
            result.frame_touches.set(GsRegisterState::psmct32_word(
                frame_base,frame_width,tx*64u,ty*32u)/2048u);
            result.depth_touches.set(GsRegisterState::psmz32_word(
                depth_base,frame_width,tx*64u,ty*32u)/2048u);
        }
    result.source_disjoint_from_destinations=
        (result.texture_reads&(result.frame_touches|result.depth_touches)).none();
    return result;
}

} // namespace hg
