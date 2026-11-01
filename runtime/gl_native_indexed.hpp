#pragma once
#include "hg/native_indexed_draw.hpp"
#include "hg/native_psmt8_texture.hpp"
#include <cstdint>
#include <memory>
#include <vector>

namespace hg {
// HG-ARCH-010: native OpenGL residency for staged PSMT8 indices (GL_R8UI) and
// CT32 palette snapshots. Requires a current OpenGL 4.3 context owned by the
// calling thread. It performs NO frame/depth writes and is not installed in
// the game: shadow sampling exists only to prove texel/palette parity against
// the first-party GS sampler before any native draw path may use it.
struct NativeIndexedGlStats {
    std::uint64_t texture_uploads=0,texture_hits=0,palette_uploads=0,palette_hits=0;
    std::uint64_t uploaded_bytes=0;
};
class NativeIndexedGlResidency {
public:
    NativeIndexedGlResidency();
    ~NativeIndexedGlResidency();
    NativeIndexedGlResidency(const NativeIndexedGlResidency&)=delete;
    NativeIndexedGlResidency& operator=(const NativeIndexedGlResidency&)=delete;
    // Returns a GL texture name valid until eviction (bounded LRU) or teardown.
    // content_id==0 (uncached staging) always uploads into a scratch texture.
    unsigned texture(const NativePsmt8LinearTexture& image);
    // Returns a GL buffer name holding 256 packed CT32 entries.
    unsigned palette(const NativePaletteSnapshot& snapshot);
    // Shadow verification only: point-sample (u,v) pairs through the GS CLAMP
    // register with nominal TEX0 TW/TH, then index -> palette. Blocking readback.
    std::vector<std::uint32_t> point_sample(const NativePsmt8LinearTexture& image,
        const NativePaletteSnapshot& snapshot,std::uint64_t clamp,
        const std::vector<std::int32_t>& uv);
    const NativeIndexedGlStats& stats() const noexcept;
private:
    struct State;
    std::unique_ptr<State> state_;
};
}
