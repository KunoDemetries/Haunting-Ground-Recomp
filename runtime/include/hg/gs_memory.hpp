#pragma once
#include <cstddef>
#include <cstdint>
#include <bitset>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace hg {
#if defined(_MSC_VER)
#define HG_GS_MEMORY_INLINE __forceinline
#define HG_GS_MEMORY_NOINLINE __declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
#define HG_GS_MEMORY_INLINE inline __attribute__((always_inline))
#define HG_GS_MEMORY_NOINLINE __attribute__((noinline))
#else
#define HG_GS_MEMORY_INLINE inline
#define HG_GS_MEMORY_NOINLINE
#endif
// Optional host coherence boundary. No GPU dependency enters the guest runtime.
// A mutable reference conservatively counts as a write before it escapes.
struct GsMemoryObserver {
    virtual ~GsMemoryObserver()=default;
    virtual void access(std::uint32_t* words,std::size_t first,std::size_t count,bool write)=0;
    // Optional page-masked CPU raster synchronization. Returning true means
    // every requested read/write page is CPU-visible and writes are tracked;
    // false must leave coherence unchanged so the caller can use legacy access.
    virtual bool scoped_access(std::uint32_t*,const std::bitset<512>&,const std::bitset<512>&){return false;}
    // Optional exact masked stores. False retains the ordinary CPU path.
    virtual bool masked_write(std::uint32_t*,const std::uint32_t*,const std::uint32_t*,std::size_t,std::uint32_t){return false;}
    // Complete-page replacement may discard old GPU contents after ordering
    // earlier users. False retains ordinary materialize-before-write behavior.
    virtual bool discard_write(std::uint32_t*,std::size_t,std::size_t){return false;}
    virtual void forget(std::uint32_t* words) noexcept=0;
};
class GsCpuMemoryScope;
class GsLocalMemory {
    std::vector<std::uint32_t> words_;
    std::shared_ptr<GsMemoryObserver> observer_;
    HG_GS_MEMORY_NOINLINE void observed_access(std::size_t first,std::size_t count,bool write) const {
        observer_->access(const_cast<std::uint32_t*>(words_.data()),first,count,write);
    }
    HG_GS_MEMORY_INLINE void access(std::size_t first,std::size_t count,bool write) const {
        if(observer_&&count)observed_access(first,count,write);
    }
    void forget() noexcept {if(observer_&&!words_.empty())observer_->forget(words_.data());}
    friend class GsCpuMemoryScope;
public:
    GsLocalMemory()=default;
    GsLocalMemory(const GsLocalMemory& other) {other.access(0,other.size(),false);words_=other.words_;}
    GsLocalMemory(GsLocalMemory&& other) noexcept:
        words_(std::move(other.words_)),observer_(std::move(other.observer_)) {}
    GsLocalMemory& operator=(const GsLocalMemory& other) {
        if(this!=&other){GsLocalMemory copy(other);swap(copy);}return *this;
    }
    GsLocalMemory& operator=(GsLocalMemory&& other) noexcept {
        if(this!=&other){GsLocalMemory moved(std::move(other));swap(moved);}return *this;
    }
    ~GsLocalMemory(){forget();}
    void swap(GsLocalMemory& other) noexcept {words_.swap(other.words_);observer_.swap(other.observer_);}
    std::size_t size() const noexcept{return words_.size();}
    bool empty() const noexcept{return words_.empty();}
    void resize(std::size_t count) {
        if(count==size())return;
        access(0,size(),false);forget();words_.resize(count);
    }
    void assign(std::size_t count,std::uint32_t value) {
        forget();words_.assign(count,value);
    }
    HG_GS_MEMORY_INLINE std::uint32_t& operator[](std::size_t index){access(index,1,true);return words_[index];}
    HG_GS_MEMORY_INLINE const std::uint32_t& operator[](std::size_t index) const {access(index,1,false);return words_[index];}
    std::uint32_t* data(){access(0,size(),true);return words_.data();}
    std::uint32_t* write_span(std::size_t first,std::size_t count){
        if(first>size()||count>size()-first)throw std::out_of_range("GS writable span outside VRAM");
        access(first,count,true);
        return first?words_.data()+first:words_.data();
    }
    std::uint32_t* write_discard_span(std::size_t first,std::size_t count) {
        if(first>size()||count>size()-first)throw std::out_of_range("GS discard span outside VRAM");
        if(observer_&&count&&!observer_->discard_write(words_.data(),first,count))access(first,count,true);
        return first?words_.data()+first:words_.data();
    }
    bool try_masked_write(const std::uint32_t* indices,const std::uint32_t* values,std::size_t count,std::uint32_t mask) {
        if(!observer_||!count)return false;
        for(std::size_t i=0;i<count;++i)if(indices[i]>=size())throw std::out_of_range("GS masked write outside VRAM");
        return observer_->masked_write(words_.data(),indices,values,count,mask);
    }
    const std::uint32_t* data() const {access(0,size(),false);return words_.data();}
    const std::uint32_t* read_span(std::size_t first,std::size_t count) const {
        if(first>size()||count>size()-first)throw std::out_of_range("GS readable span outside VRAM");
        access(first,count,false);
        return first?words_.data()+first:words_.data();
    }
    auto begin(){access(0,size(),true);return words_.begin();}
    auto end(){access(0,size(),true);return words_.end();}
    auto begin() const {access(0,size(),false);return words_.begin();}
    auto end() const {access(0,size(),false);return words_.end();}
    bool operator==(const GsLocalMemory& other) const {
        access(0,size(),false);other.access(0,other.size(),false);return words_==other.words_;
    }
    bool operator!=(const GsLocalMemory& other) const{return !(*this==other);}
    void observe(std::shared_ptr<GsMemoryObserver> observer) {
        if(observer_==observer)return;
        access(0,size(),false);forget();observer_=std::move(observer);
    }
    // Backend-only escape hatch. Caller is responsible for ownership/visibility.
    std::uint32_t* backend_data() noexcept{return words_.data();}
};

// CPU raster loops already have ordered access to all pixels. Synchronize once,
// then avoid observer calls per pixel; restore even on an explicit render fault.
class GsCpuMemoryScope {
    GsLocalMemory& memory_;
    std::shared_ptr<GsMemoryObserver> saved_;
public:
    explicit GsCpuMemoryScope(GsLocalMemory& memory):memory_(memory) {
        memory_.access(0,memory_.size(),true);saved_=std::move(memory_.observer_);
    }
    GsCpuMemoryScope(GsLocalMemory& memory,const std::bitset<512>& writes):memory_(memory) {
        if(memory_.size()!=1024*1024||writes.all())memory_.access(0,memory_.size(),true);
        else {
            // HG-DIAG-017: keep coalesced full read visibility, but only mark
            // proven output pages dirty. No observer calls enter pixel loops.
            memory_.access(0,memory_.size(),false);
            for(std::size_t page=0;page<512;) {
                if(!writes[page]){++page;continue;}
                const auto first=page;while(page<512&&writes[page])++page;
                memory_.access(first*2048,(page-first)*2048,true);
            }
        }
        saved_=std::move(memory_.observer_);
    }
    GsCpuMemoryScope(GsLocalMemory& memory,const std::bitset<512>& reads,
                     const std::bitset<512>& writes):memory_(memory) {
        bool handled=false;
        if(memory_.size()==1024*1024&&memory_.observer_)
            handled=memory_.observer_->scoped_access(memory_.words_.data(),reads,writes);
        if(!handled) {
            // Preserve the old full-read contract for non-resident/unknown observers.
            memory_.access(0,memory_.size(),false);
            if(writes.all())memory_.access(0,memory_.size(),true);
            else for(std::size_t page=0;page<512;) {
                if(!writes[page]){++page;continue;}
                const auto first=page;while(page<512&&writes[page])++page;
                memory_.access(first*2048,(page-first)*2048,true);
            }
        }
        saved_=std::move(memory_.observer_);
    }
    ~GsCpuMemoryScope(){memory_.observer_=std::move(saved_);}
    GsCpuMemoryScope(const GsCpuMemoryScope&)=delete;
    GsCpuMemoryScope& operator=(const GsCpuMemoryScope&)=delete;
};
#undef HG_GS_MEMORY_INLINE
#undef HG_GS_MEMORY_NOINLINE
}
