#pragma once
#include "hg/native_psmt8_texture.hpp"
#include <array>
#include <atomic>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <list>
#include <memory>
#include <optional>
#include <utility>

namespace hg {

// HG-ARCH-006: renderer-private source-page generations. An epoch is valid
// ONLY after the caller establishes a fully coherent CPU/GPU baseline and
// observes EVERY later CPU mutation and pending GPU write. This class does not
// install itself into the game's existing GsMemoryObserver.
struct NativeVramPageStamp {
    std::bitset<512> pages{};
    std::array<std::uint64_t,512> generations{};
};

class NativeVramPageEpochs {
    std::array<std::uint64_t,512> generation_{};
    std::array<std::uint32_t,512> pending_gpu_writers_{};
    std::uint64_t sequence_=0;
    bool complete_=false;

    bool tick(const std::bitset<512>& pages) noexcept {
        if(!complete_||sequence_==(std::numeric_limits<std::uint64_t>::max)()) {
            invalidate_unknown();return false;
        }
        const auto serial=++sequence_;
        for(std::size_t p=0;p<512;++p)if(pages[p])generation_[p]=serial;
        return true;
    }
public:
    static constexpr std::size_t word_count=1024u*1024u;
    // Caller must have completed all old GPU draws and installed write
    // observation BEFORE this point. A new baseline invalidates every stamp.
    bool begin_after_coherent_fence() noexcept {
        if(sequence_==(std::numeric_limits<std::uint64_t>::max)()){
            invalidate_unknown();return false;
        }
        pending_gpu_writers_.fill(0);
        const auto serial=++sequence_;
        generation_.fill(serial);
        complete_=true;
        return true;
    }
    void invalidate_unknown() noexcept {
        complete_=false;
        pending_gpu_writers_.fill(0);
        // Keep sequence monotonic so no old source stamp can match a new
        // baseline after untracked writes, fault recovery, or observer teardown.
    }
    bool tracking_complete() const noexcept {return complete_;}
    std::uint64_t sequence() const noexcept {return sequence_;}
    bool cpu_mutable_access(std::size_t first,std::size_t count) noexcept {
        if(first>word_count||count>word_count-first){
            invalidate_unknown();return false;
        }
        if(!count)return complete_;
        std::bitset<512> pages;
        for(auto p=first/2048u;p<=(first+count-1u)/2048u;++p)pages.set(p);
        return tick(pages); // BEFORE writes (mutable ref may escape).
    }
    // Bulk CPU raster scopes expose exact physical destination pages rather
    // than many individual mutable references. Notify before writing pixels.
    bool cpu_mutable_pages(const std::bitset<512>& pages) noexcept {
        return pages.none()?complete_:tick(pages);
    }
    bool gpu_queued_write(const std::bitset<512>& pages) noexcept {
        if(!complete_)return false;
        for(std::size_t p=0;p<512;++p)
            if(pages[p]&&pending_gpu_writers_[p]==(std::numeric_limits<std::uint32_t>::max)()){
                invalidate_unknown();return false;
            }
        if(!tick(pages))return false; // Enqueue invalidates old cached textures.
        for(std::size_t p=0;p<512;++p)if(pages[p])++pending_gpu_writers_[p];
        return true;
    }
    bool gpu_completed_write(const std::bitset<512>& pages) noexcept {
        if(!complete_)return false;
        for(std::size_t p=0;p<512;++p)
            if(pages[p]&&!pending_gpu_writers_[p]){invalidate_unknown();return false;}
        for(std::size_t p=0;p<512;++p)if(pages[p])--pending_gpu_writers_[p];
        return true;
    }
    // GL resident readback has completed ALL submitted producer writes for
    // these pages, possibly several draws per page. This is NOT called on
    // glFlush or after mere command submission. Unknown completion is fatal
    // to cache trust and must not silently publish an old texture.
    bool gpu_completed_visible_pages(const std::bitset<512>& pages) noexcept {
        if(!complete_)return false;
        for(std::size_t p=0;p<512;++p)
            if(pages[p]&&!pending_gpu_writers_[p]){
                invalidate_unknown();return false;
            }
        for(std::size_t p=0;p<512;++p)if(pages[p])pending_gpu_writers_[p]=0;
        return true;
    }
    // HG-ARCH-009: complete-page CPU replacement after every queued user of
    // these pages was submitted. The CPU shadow is now authoritative, so the
    // superseded producers can no longer reach a CPU consumer; the caller must
    // ALSO report the replacement through cpu_mutable_pages (new generation).
    bool gpu_superseded_pages(const std::bitset<512>& pages) noexcept {
        if(!complete_)return false;
        for(std::size_t p=0;p<512;++p)if(pages[p])pending_gpu_writers_[p]=0;
        return true;
    }
    bool gpu_pending(const std::bitset<512>& pages) const noexcept {
        for(std::size_t p=0;p<512;++p)if(pages[p]&&pending_gpu_writers_[p])return true;
        return false;
    }
    std::optional<NativeVramPageStamp> stamp(const std::bitset<512>& pages) const noexcept {
        if(!complete_||pages.none()||gpu_pending(pages))return std::nullopt;
        NativeVramPageStamp result;
        result.pages=pages;
        for(std::size_t p=0;p<512;++p)if(pages[p])result.generations[p]=generation_[p];
        return result;
    }
    bool matches(const NativeVramPageStamp& saved) const noexcept {
        if(!complete_||saved.pages.none()||gpu_pending(saved.pages))return false;
        for(std::size_t p=0;p<512;++p)
            if(saved.pages[p]&&saved.generations[p]!=generation_[p])return false;
        return true;
    }
};

// Exact physical read footprint of the *staged bytes*, including the nominal
// extent even if REGION_CLAMP/REPEAT only permits a smaller sample envelope.
// Do not reuse the triangle's footprint: texture staging can read extra bytes.
struct NativePsmt8StageLayout {
    unsigned width=0,height=0;
    std::bitset<512> pages{};
};
inline std::optional<NativePsmt8StageLayout> native_psmt8_stage_layout(
        const NativeIndexedDraw& draw) noexcept {
    const auto tex0=draw.tex0,clamp=draw.clamp;
    if(((tex0>>20)&63u)!=0x13u)return std::nullopt;
    const unsigned tw=unsigned((tex0>>26)&15u),th=unsigned((tex0>>30)&15u);
    const unsigned stride=unsigned((tex0>>14)&63u)*64u;
    if(tw>10||th>10||!stride)return std::nullopt;
    const unsigned umode=unsigned(clamp&3u),vmode=unsigned((clamp>>2)&3u);
    const unsigned umin=unsigned((clamp>>4)&1023u),umax=unsigned((clamp>>14)&1023u);
    const unsigned vmin=unsigned((clamp>>24)&1023u),vmax=unsigned((clamp>>34)&1023u);
    if((umode==2&&umin>umax)||(vmode==2&&vmin>vmax))return std::nullopt;
    const auto maximum=[](unsigned mode,unsigned size,unsigned low,unsigned high){
        return mode<2?size-1u:mode==2?high:(low|high);
    };
    NativePsmt8StageLayout layout;
    layout.width=std::max(1u<<tw,maximum(umode,1u<<tw,umin,umax)+1u);
    layout.height=std::max(1u<<th,maximum(vmode,1u<<th,vmin,vmax)+1u);
    if(layout.width>1024u||layout.height>1024u)return std::nullopt;
    const std::uint64_t base=std::uint64_t(tex0&0x3fffu)*64u;
    for(unsigned ty=0;ty<=(layout.height-1u)/64u;++ty)
        for(unsigned tx=0;tx<=(layout.width-1u)/128u;++tx){
            const auto tile=std::uint64_t(tx)+std::uint64_t(ty)*(stride/128u);
            const unsigned word=unsigned((base+tile*2048u)&0xfffffu);
            layout.pages.set(word/2048u);
            layout.pages.set(((word+2047u)&0xfffffu)/2048u);
        }
    return layout;
}

// HG-ARCH-010: process-wide, never-reused identity for staged content.
inline std::uint64_t native_next_content_id() noexcept {
    static std::atomic<std::uint64_t> next{0};
    return ++next;
}

// A bounded, single-GS-owner CPU-side staging cache. It has NO host GL or
// gameplay installation; no observer composition, synchronization, guest
// readback or rendering permission is implied by a cache hit.
class NativePsmt8TextureCache {
    struct Entry {
        std::uint64_t tex0=0,clamp=0;
        NativeVramPageStamp version{};
        std::uint64_t verified_sequence=0;
        NativePsmt8LinearTexture image{};
    };
    const NativeVramPageEpochs& epochs_;
    const GsLocalMemory& memory_;
    std::list<Entry> entries_;
    std::uint64_t same_epoch_hits_=0;
    static constexpr std::size_t capacity_=4u;
public:
    NativePsmt8TextureCache(const NativeVramPageEpochs& epochs,const GsLocalMemory& memory):
        epochs_(epochs),memory_(memory){}
    std::uint64_t same_epoch_hits() const noexcept{return same_epoch_hits_;}
    std::size_t size() const noexcept{return entries_.size();}
    void clear() noexcept {entries_.clear();}
    // Returned reference is valid until eviction/clear. Do not retain across
    // mutations of this cache, a GS owner transition, or epoch-baseline changes.
    const NativePsmt8LinearTexture* lookup(const NativeIndexedDraw& draw) {
        if(!epochs_.tracking_complete())return nullptr;
        for(auto it=entries_.begin();it!=entries_.end();++it) {
            if(it->tex0!=draw.tex0||it->clamp!=draw.clamp)continue;
            const bool identical_epoch=it->verified_sequence==epochs_.sequence();
            // Every CPU write and GPU enqueue advances the global sequence
            // before mutation; completion never revalidates an old source.
            if(!identical_epoch&&!epochs_.matches(it->version))return nullptr;
            if(identical_epoch)++same_epoch_hits_;
            it->verified_sequence=epochs_.sequence();
            entries_.splice(entries_.begin(),entries_,it); // bounded LRU
            return &entries_.front().image;
        }
        return nullptr;
    }
    const NativePsmt8LinearTexture* get_or_stage(
            const NativeIndexedDraw& draw,const GsLocalMemory& memory) {
        if(&memory!=&memory_)return nullptr; // No reuse across guest GS VRAM instances.
        if(const auto* current=lookup(draw))return current;
        const auto layout=native_psmt8_stage_layout(draw);
        if(!layout||memory.size()!=NativeVramPageEpochs::word_count)return nullptr;
        const auto before=epochs_.stamp(layout->pages);
        if(!before)return nullptr;
        auto staged=native_psmt8_stage_texture(draw,memory);
        if(!staged||staged->width!=layout->width||
           staged->height!=layout->height||staged->source_pages!=layout->pages||
           !epochs_.matches(*before))return nullptr;
        staged->content_id=native_next_content_id();
        if(entries_.size()>=capacity_)entries_.pop_back();
        entries_.push_front(Entry{draw.tex0,draw.clamp,*before,epochs_.sequence(),std::move(*staged)});
        return &entries_.front().image;
    }
};

// Test-only observation adapter. Installing this over a resident GPU observer
// would DROP materialization and is forbidden; production must instead compose
// write notifications at its already-ordered CPU/GPU coherence boundaries.
class NativeEpochTestMemoryObserver final : public GsMemoryObserver {
    NativeVramPageEpochs& epochs_;
public:
    explicit NativeEpochTestMemoryObserver(NativeVramPageEpochs& epochs):epochs_(epochs){}
    void access(std::uint32_t*,std::size_t first,std::size_t count,bool write) override {
        if(write)epochs_.cpu_mutable_access(first,count);
    }
    void forget(std::uint32_t*) noexcept override {epochs_.invalidate_unknown();}
};

} // namespace hg
