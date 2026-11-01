#pragma once
#include <atomic>
#include <cstdint>
#include <exception>
#include <functional>
#include <vector>

namespace hg {
// Host scheduling only. While a device thread is attached, it owns the VIF1
// parser, VU1, GIF and GS state; the EE/IOP thread sends it the exact ordered
// sequence of inputs the synchronous path would apply and waits for it to drain
// before any guest observation of that state. No guest clock or ordering change.
enum class DeviceKind : std::uint32_t {
    vif_qword,vif_tag,gif_qword,gs_privileged,gs_csr_events,gs_imr,vif_register,gif_control,rasterize,call,
    gif_words // Internal: a = owned contiguous word-block pointer; b = qword count.
};
struct DeviceCommand {std::uint64_t a=0,b=0;std::uint32_t kind=0,x=0;};

class DeviceLink {
protected:
    static constexpr std::uint64_t capacity=std::uint64_t(1)<<20,mask=capacity-1;
    std::vector<DeviceCommand> ring=std::vector<DeviceCommand>(capacity);
    alignas(64) std::atomic<std::uint64_t> write_index{0};
    alignas(64) std::atomic<std::uint64_t> read_index{0};
    alignas(64) std::uint64_t produced=0,read_cache=0,published=0;
public:
    // EE-side mirror of the GS privileged fields the EE thread itself decides
    // from: IMR and the CSR event bits only it sets (VSINT) or clears. Device
    // work can set only SIGNAL/FINISH; while IMR leaves either unmasked, every
    // interrupt evaluation joins and reads the real GS state instead.
    std::uint64_t imr=0x1f00,busdir=0;
    std::uint32_t csr_events=0;
    // Device inputs pushed since the last rasterize request.
    bool raster_due=false;
    std::uint64_t unmasked_interrupt_joins=0,joins=0;
    virtual ~DeviceLink()=default;
    void push(DeviceKind kind,std::uint32_t x,std::uint64_t a,std::uint64_t b=0) {
        if(produced-read_cache>=capacity)wait_space();
        auto& command=ring[produced&mask];
        command.a=a;command.b=b;command.kind=std::uint32_t(kind);command.x=x;
        ++produced;raster_due=true;
        if(produced-published>=4096)publish_slow();
    }
    void publish() {if(produced!=published)publish_slow();}
    // Rethrows a device-thread failure (as recorded there) on the EE thread.
    virtual void publish_slow()=0;
    virtual void wait_space()=0;
    // Returns once every previously pushed command has completed.
    virtual void join()=0;
    // Ordered host closure on the device thread; it must not touch EE state.
    virtual void post(std::function<void()> work)=0;
    virtual void run_sync(const std::function<void()>& work)=0;
    // GIF STAT (0x10003020, with the EE-side BUSDIR) or a VIF1 register, read
    // once all previously submitted VIF1/GIF input has been parsed. Neither
    // depends on GS work, so this waits only for the VIF1/VU1 stage.
    virtual std::uint32_t read_transport_register(std::uint32_t address,bool reverse)=0;
    // Drains every command, ends the thread and returns its failure (if any).
    // Afterwards the owner may access the device state directly again.
    virtual std::exception_ptr stop()=0;
};
}
