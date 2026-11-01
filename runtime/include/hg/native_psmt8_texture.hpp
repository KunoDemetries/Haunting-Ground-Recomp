#pragma once
#include "hg/native_psmt8_pages.hpp"
#include <algorithm>
#include <array>
#include <bitset>
#include <cstdint>
#include <optional>
#include <vector>

namespace hg {

// HG-ARCH-005: first-party PSMT8 indices in row-major storage. This is an
// off-path texture source parity/staging helper, NOT an active GPU renderer.
// Integer GL_R8UI sampling could consume the indices after the GL backend
// separately proves upload coherence, exact GS address/alpha/depth behavior,
// and the same source page epoch as the guest.
struct NativePsmt8LinearTexture {
    unsigned width=0,height=0;
    std::uint64_t tex0=0;
    std::bitset<512> source_pages{};
    std::vector<std::uint8_t> indices;
    bool requires_texture_epoch_proof=true;
    // HG-ARCH-010: nonzero only when an epoch-verified cache staged these
    // bytes; GPU residency may reuse an upload solely by this identity.
    std::uint64_t content_id=0;

    std::uint8_t sample_unwrapped(unsigned x,unsigned y) const {
        if(x>=width||y>=height)throw std::out_of_range("native PSMT8 staging coordinate");
        return indices[std::size_t(y)*width+x];
    }
};

// This stages only the physical VRAM pages actually needed by the indexed
// texture. Each first touch uses the existing GS memory observer/readback
// contract; it never bypasses pending GPU writes. CPU-visible page snapshots
// are NOT permission to reuse this texture without the separate source epochs
// and ordered native-GPU upload/consumer proof. Never stage on every draw.
inline std::optional<NativePsmt8LinearTexture> native_psmt8_stage_texture(
        const NativeIndexedDraw& draw,const GsLocalMemory& memory) {
    const auto tex0=draw.tex0,clamp=draw.clamp;
    if(((tex0>>20)&63u)!=0x13u||memory.size()!=1024u*1024u)return std::nullopt;
    const unsigned tw=unsigned((tex0>>26)&15u),th=unsigned((tex0>>30)&15u);
    const unsigned stride=unsigned((tex0>>14)&63u)*64u;
    if(tw>10||th>10||!stride)return std::nullopt;
    const unsigned umode=unsigned(clamp&3),vmode=unsigned((clamp>>2)&3);
    const unsigned umin=unsigned((clamp>>4)&1023),umax=unsigned((clamp>>14)&1023);
    const unsigned vmin=unsigned((clamp>>24)&1023),vmax=unsigned((clamp>>34)&1023);
    if((umode==2&&umin>umax)||(vmode==2&&vmin>vmax))return std::nullopt;
    const auto max_possible=[](unsigned mode,unsigned size,unsigned low,unsigned high) {
        return mode<2?size-1u:mode==2?high:(low|high);
    };
    NativePsmt8LinearTexture image;
    // The GS CLAMP register may legally select coordinates outside nominal TW/TH.
    // Record both nominal and external addressing in the staging envelope.
    image.width=std::max(1u<<tw,max_possible(umode,1u<<tw,umin,umax)+1u);
    image.height=std::max(1u<<th,max_possible(vmode,1u<<th,vmin,vmax)+1u);
    if(image.width>1024u||image.height>1024u)return std::nullopt;
    image.tex0=tex0;
    image.indices.resize(std::size_t(image.width)*image.height);
    const std::uint32_t base=std::uint32_t(tex0&0x3fffu)*64u;
    // Fetch each physical 8 KiB page at most once through the preexisting
    // observer, so a cache miss cannot force an unrelated full 4 MiB readback.
    // GS storage stays owned by memory; do not retain these pointers afterward.
    std::array<const std::uint32_t*,512> coherent_pages{};
    for(unsigned y=0;y<image.height;++y)
        for(unsigned x=0;x<image.width;++x) {
            const auto addr=GsRegisterState::psmt8_word(base,stride,x,y);
            const unsigned page=addr/2048u;
            auto& page_words=coherent_pages[page];
            if(!page_words)page_words=memory.read_span(std::size_t(page)*2048u,2048u);
            const unsigned lane=((y&2u)>>1)|((x&8u)>>2);
            image.indices[std::size_t(y)*image.width+x]=
                std::uint8_t(page_words[addr%2048u]>>(lane*8u));
        }
    // Enumerate every complete physical indexed 128x64 tile that the
    // *staged* envelope could have read, including non-page-aligned TBP0.
    for(unsigned ty=0;ty<=(image.height-1u)/64u;++ty)
        for(unsigned tx=0;tx<=(image.width-1u)/128u;++tx) {
            const std::uint64_t tile=std::uint64_t(tx)+std::uint64_t(ty)*(stride/128u);
            const unsigned start=unsigned((std::uint64_t(base)+tile*2048u)&0xfffffu);
            image.source_pages.set(start/2048u);
            image.source_pages.set(((start+2047u)&0xfffffu)/2048u);
        }
    return image;
}

} // namespace hg
