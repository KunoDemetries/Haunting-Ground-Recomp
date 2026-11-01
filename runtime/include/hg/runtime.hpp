#pragma once
#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>
#include <atomic>
#include <memory>
#include <limits>
#include <cstdlib>
#include "hg/fpu.hpp"
#include "hg/timers.hpp"
#include "hg/intc.hpp"
#include "hg/console.hpp"
#include "hg/dmac.hpp"
#include "hg/ipu.hpp"
#include "hg/gs.hpp"
#include "hg/vif.hpp"
#include "hg/sif_link.hpp"
#include "hg/device_link.hpp"

namespace hg {
struct Fault : std::runtime_error {
    std::uint32_t pc;
    Fault(std::uint32_t at, const std::string& why) : std::runtime_error(why), pc(at) {}
};
struct Register { std::uint64_t lo = 0, hi = 0; };
enum class VuBroadcastProduct { multiply_accumulator, add_accumulator, add_vector };
struct Vu0State {
    // VU0 vector transfers and bounded macro arithmetic. Integer/control
    // transfers and microprogram execution still require explicit support.
    std::array<Register,32> vf{};
    std::uint16_t mac=0,status=0;
    std::uint32_t q=0,pending_q=0;
    bool q_pending=false;
    std::uint32_t fbrst=0;
    std::array<std::uint32_t,4> acc{};
};
struct Semaphore {
    std::uint32_t count=0, maximum=0, initial=0, attr=0, option=0;
};
struct KernelReturn { std::uint32_t pc; std::uint64_t ra; };
struct InterruptHandler { std::uint32_t cause,callback; std::uint64_t next,arg; };
struct EeCpuContext {
    std::array<Register,32> gpr{};
    std::array<std::uint32_t,32> fpr{};
    Fpu fpu{};
    std::uint64_t hi=0,lo=0,hi1=0,lo1=0;
    unsigned sa=0;
    std::uint32_t pc=0,cp0_status=0x70010001,cp0_epc=0,cp0_error_epc=0,cp0_wired=0,cp0_tag_lo=0;
};
struct Thread {
    std::uint32_t status=0x10, entry=0, stack=0, stack_size=0, gp=0;
    std::uint32_t initial_priority=0, priority=0, attr=0, option=0;
    std::uint32_t argument=0,wakeup_count=0,wait_type=0,wait_id=0;
    EeCpuContext context{};
    bool initialized=false;
    std::vector<KernelReturn> kernel_returns;
    std::uint32_t transfer_pc=0,granted_semaphore=0;
};
struct BootServices {
    bool enabled = false, thread_ready = false, heap_ready = false;
    bool in_interrupt=false;
    // Native ABI placement profile, validated against the USA boot observation.
    std::uint32_t stack_ceiling = 0x01fff000, initial_sp = 0x01ffed60;
    std::uint32_t stack_base = 0, heap_start = 0, heap_end = 0, root = 0, gp = 0;
    std::string executable_name = "cdrom0:\\SLUS_210.75;1";
    std::vector<Semaphore> semaphores;
    struct SemaphoreTrace {
        char operation=0;
        std::uint32_t id=0,before=0,after=0,pc=0,handler=0,current_thread=0;
        bool in_interrupt=false,blocked=false;
    };
    std::array<SemaphoreTrace,64> semaphore_trace{};
    std::size_t semaphore_trace_cursor=0;
    SemaphoreTrace last_semaphore_signal{},last_semaphore_wait_success{};
    bool table_ready = false;
    std::vector<KernelReturn> returns;
    std::vector<InterruptHandler> interrupt_handlers;
    std::vector<InterruptHandler> dma_handlers;
    std::vector<Thread> threads;
    std::uint32_t current_thread=0,thread_cursor=1;
    std::uint64_t data_cache_epoch=0, instruction_cache_epoch=0;
    std::array<std::uint32_t,3> sif_system_registers{};
    std::uint32_t sif_transfer_id=0,active_sif_transfer=0;
    struct SifDmaTrace {
        std::uint32_t pc=0,count=0;
        std::array<std::array<std::uint32_t,4>,2> descriptors{};
    };
    std::array<SifDmaTrace,16> sif_dma_trace{};
    std::size_t sif_dma_trace_cursor=0;
    std::string deci2_output;
    std::uint64_t deci2_kputs_calls=0;
    static constexpr std::uint32_t table = 0x10000;
    static constexpr std::uint32_t table_entries = 512;
    static constexpr std::uint32_t gateway = 0xff000000;
    static constexpr std::uint32_t return_gateway = 0xff001000;
};
inline std::uint64_t sx32(std::uint32_t v) {
    return (v & 0x80000000u) ? (0xffffffff00000000ull | v) : v;
}
inline std::uint32_t physical_address(std::uint32_t address) {
    // Native boot TLB profile for the RAM aliases constructed by the original
    // SIF buffer initialization. Bound each alias to the actual 32 MiB RAM size.
    if((address>=0x20000000u && address<0x22000000u) ||
       (address>=0x30000000u && address<0x32000000u))return address&0x1ffffffu;
    return address >= 0x80000000u && address < 0xc0000000u
        ? address & 0x1fffffffu : address;
}
inline bool signed_less(std::uint64_t a, std::uint64_t b) {
    return (a ^ 0x8000000000000000ull) < (b ^ 0x8000000000000000ull);
}
inline std::uint64_t sra32(std::uint32_t v, unsigned n) {
    const auto shifted = n == 0 ? v : ((v >> n) | ((v & 0x80000000u) ? (~0u << (32 - n)) : 0u));
    return sx32(shifted);
}
inline std::uint64_t sra64(std::uint64_t v, unsigned n) {
    n &= 63;
    return n==0 ? v : (v>>n) | ((v & (1ull<<63)) ? (~0ull << (64-n)) : 0);
}
// HG-DIAG-126: HG_PCSX2_VU0_ADD=1 swaps the VU0 macro adder for the PCSX2 oracle's
// (exact sum, then chop) so oracle comparisons can see past that known emulator
// difference. Off by default; the console's adder is Fpu::add (docs/DIAGNOSTICS.md).
inline bool pcsx2_vu0_add_profile() {
    static const bool enabled=[]{const char* v=std::getenv("HG_PCSX2_VU0_ADD");return v&&v[0]=='1';}();
    return enabled;
}
inline std::uint32_t vu0_add(Fpu& unit,std::uint32_t a,std::uint32_t b) {
    return pcsx2_vu0_add_profile()?unit.add_exact_chop(a,b):unit.add(a,b);
}
struct State {
    std::array<Register, 32> gpr{};
    Vu0State vu0{};
    std::uint64_t hi = 0, lo = 0, hi1 = 0, lo1 = 0;
    unsigned sa = 0;
    std::array<std::uint32_t, 32> fpr{};
    Fpu fpu;
    Timers timers;
    InterruptController intc;
    Dmac dmac;
    Ipu ipu;
    GsRegisterState gs;
    GifPath gif;
    Vif1Path vif1;
    // Non-null only while a host device thread owns gif/vif1/gs (device_link.hpp).
    DeviceLink* device=nullptr;
    std::shared_ptr<SifLink> sif=std::make_shared<SifLink>();
    std::uint32_t cp0_status=0x70010001;
    std::uint32_t cp0_epc=0, cp0_error_epc=0, cp0_wired=0, cp0_tag_lo=0;
    BootServices boot;
    ConsoleSettings console;
    bool protect_kernel_memory = false;
    std::vector<std::pair<std::uint32_t,std::uint32_t>> kernel_code_regions;
    std::uint32_t pc = 0;
    std::uint32_t last_transfer_pc=0; // Diagnostic provenance, not guest state.
    struct PcTrace {
        std::uint32_t pc=0,ra=0,v0=0,v1=0,a0=0,a1=0,a2=0,a3=0,t0=0,t4=0,s0=0,s1=0,s2=0,s3=0,s4=0,s5=0,s6=0,s7=0,gp=0,sp=0,fp=0;
    };
    struct PcTraceWatch {std::uint32_t pc=0;std::array<PcTrace,64> history{};std::size_t cursor=0;};
    static constexpr std::size_t max_trace_watches=64;
    std::array<PcTrace,64> pc_trace{};
    std::size_t pc_trace_cursor=0;
    std::vector<PcTraceWatch> trace_watches;
    struct RamWriteTrace {
        std::uint32_t pc=0,ra=0,address=0,before=0,after=0;
        unsigned size=0;std::uint32_t thread=0;
    };
    std::array<RamWriteTrace,64> ram_write_trace{};
    std::size_t ram_write_trace_cursor=0;
    // Diagnostic-only RAM words. Slot 0 preserves the long-standing SIF
    // semaphore watch; extra slots stay disabled unless a diagnostic enables
    // them explicitly.
    std::array<std::uint32_t,4> ram_write_trace_addresses{0x0198cbc8u,0,0,0};
    std::uint32_t ram_write_trace_word(std::uint32_t address,unsigned size) const {
        for(const auto watched:ram_write_trace_addresses)
            if(watched && address<=watched+3 && std::uint64_t(address)+size>watched)return watched;
        return 0;
    }
    bool add_trace_watch(std::uint32_t target) {
        if(!target)return false;
        for(const auto& watch:trace_watches)if(watch.pc==target)return true;
        if(trace_watches.size()==max_trace_watches)return false;
        trace_watches.push_back({});trace_watches.back().pc=target;return true;
    }
    void trace_pc() {
        if(trace_watches.empty())return;
        pc_trace[pc_trace_cursor++%pc_trace.size()]={pc,std::uint32_t(r(31)),std::uint32_t(r(2)),std::uint32_t(r(3)),
            std::uint32_t(r(4)),std::uint32_t(r(5)),std::uint32_t(r(6)),std::uint32_t(r(7)),std::uint32_t(r(8)),
            std::uint32_t(r(12)),std::uint32_t(r(16)),std::uint32_t(r(17)),std::uint32_t(r(18)),std::uint32_t(r(19)),
            std::uint32_t(r(20)),std::uint32_t(r(21)),std::uint32_t(r(22)),std::uint32_t(r(23)),std::uint32_t(r(28)),
            std::uint32_t(r(29)),std::uint32_t(r(30))};
        for(auto& watch:trace_watches)if(watch.pc==pc){watch.history=pc_trace;watch.cursor=pc_trace_cursor;}
    }
    std::vector<std::uint8_t> ram = std::vector<std::uint8_t>(32 * 1024 * 1024);
    std::array<std::uint8_t, 16384> scratch{};
    std::uint64_t r(unsigned i) const { return i ? gpr.at(i).lo : 0; }
    EeCpuContext save_cpu() const {
        return {gpr,fpr,fpu,hi,lo,hi1,lo1,sa,pc,cp0_status,cp0_epc,cp0_error_epc,cp0_wired,cp0_tag_lo};
    }
    void restore_cpu(const EeCpuContext& c) {
        gpr=c.gpr;fpr=c.fpr;fpu=c.fpu;hi=c.hi;lo=c.lo;hi1=c.hi1;lo1=c.lo1;sa=c.sa;pc=c.pc;
        cp0_status=c.cp0_status;cp0_epc=c.cp0_epc;cp0_error_epc=c.cp0_error_epc;cp0_wired=c.cp0_wired;cp0_tag_lo=c.cp0_tag_lo;
    }
    void receive_sif0(std::uint64_t low,std::uint64_t high) {
        try {
            dmac.receive_sif0(low,high,[this](std::uint32_t destination,std::uint64_t a,std::uint64_t b) {
                // DMA uses physical addresses and SPR selection, not CPU virtual aliases.
                const bool spr=(destination&0x80000000u)!=0;
                const auto offset=destination&0x7fffffffu;
                const auto size=spr ? scratch.size() : ram.size();
                if(offset%16 || std::uint64_t(offset)+16>size)
                    throw Fault(pc,"SIF0 DMA destination is outside implemented memory");
                auto* p=(spr ? scratch.data() : ram.data())+offset;
                for(unsigned n=0;n<8;++n) {p[n]=std::uint8_t(a>>(n*8));p[n+8]=std::uint8_t(b>>(n*8));}
            });
        } catch(const std::runtime_error& e) {throw Fault(pc,e.what());}
    }
    // EE User Manual pp43-44,73-78: fixed-direction normal SPR burst channels.
    // Validate the entire RAM span before mutation; SADR is a 14-bit byte offset.
    bool pump_scratchpad(unsigned channel) {
        if(channel!=8 && channel!=9)throw Fault(pc,"invalid SPR DMA channel");
        auto& c=dmac.channels[channel];
        if(!dmac.enabled(channel) || !(c.chcr&0x100))return false;
        const bool chain=channel==9 && (c.chcr&0xc)==4;
        if((c.chcr&0x70) || ((c.chcr&0xc) && !chain) || (dmac.control&0xfc))
            throw Fault(pc,"SPR DMA mode requires implementation");
        if(chain && !dmac.spr_input_packet) {
            if(c.tadr%16 || std::uint64_t(c.tadr)+16>ram.size())throw Fault(pc,"SPR tag outside RAM");
            std::uint32_t tag=0,address=0;
            for(unsigned n=0;n<4;++n){tag|=std::uint32_t(ram[c.tadr+n])<<(8*n);address|=std::uint32_t(ram[c.tadr+4+n])<<(8*n);}
            const unsigned id=(tag>>28)&7;
            if((tag&0x0c000000u) || (id!=0 && id!=1 && id!=2 && id!=3 && id!=7))throw Fault(pc,"SPR source tag requires unsupported priority/stall/stack");
            const unsigned count=tag&65535;
            const auto source=id==0 || id==3?address:c.tadr+16;
            if(source%16 || std::uint64_t(source)+count*16>ram.size())throw Fault(pc,"SPR source tag span outside RAM");
            c.chcr=(c.chcr&65535)|(tag&0xffff0000u);c.qwc=count;c.madr=source;
            c.tadr=id==2?address:(id==1 || id==7)?c.tadr+16+count*16:c.tadr+16;
            dmac.spr_input_end=id==0 || id==7 || ((tag&0x80000000u) && (c.chcr&0x80));
            dmac.spr_input_packet=true;
        }
        const auto bytes=std::uint64_t(c.qwc)*16;
        if(c.madr%16 || c.sadr%16 || c.sadr>=scratch.size() ||
           std::uint64_t(c.madr)+bytes>ram.size())
            throw Fault(pc,"SPR DMA span is outside implemented memory");
        for(std::uint32_t n=0;n<bytes;++n) {
            const auto spr=(c.sadr+n)&0x3fffu;
            if(channel==8)ram[c.madr+n]=scratch[spr];
            else scratch[spr]=ram[c.madr+n];
        }
        c.madr+=std::uint32_t(bytes);c.sadr=(c.sadr+std::uint32_t(bytes))&0x3fffu;
        c.qwc=0;
        if(!chain || dmac.spr_input_end){c.chcr&=~0x100u;dmac.raise(1u<<channel);}
        if(chain)dmac.spr_input_packet=false;
        return true;
    }
    void sync_gs_interrupt() {
        if(device) {
            // Device work can raise only SIGNAL/FINISH. If IMR masks both, the
            // EE-side mirror holds every bit that can be pending; otherwise use
            // the real state after all previously submitted work completes.
            if((~device->imr>>8)&3) {
                ++device->unmasked_interrupt_joins;device_join();
                if(gs.interrupt_pending())intc.raise(0);
            } else if(device->csr_events&~std::uint32_t(device->imr>>8)&0x1f)intc.raise(0);
            return;
        }
        if(gs.interrupt_pending())intc.raise(0);
    }
    void device_join() {
        try {device->join();}
        catch(const Fault&) {throw;}
        catch(const std::runtime_error& e) {throw Fault(pc,e.what());}
    }
    // Same validation, mirror updates and ordering as GsRegisterState::write_privileged.
    void device_write_privileged(std::uint32_t address,std::uint64_t data) {
        if(address%16)throw std::runtime_error("invalid GS privileged register address");
        switch((address&0x1000u)|(address&0x3f0u)) {
        case 0x000:case 0x020:case 0x070:case 0x080:case 0x090:case 0x0a0:case 0x0e0:break;
        case 0x1000:
            if(data&(1ull<<8))throw std::runtime_error("unimplemented GS CSR FLUSH");
            if(data&(1ull<<9)) {
                device->run_sync([&]{gs.write_privileged(address,data);});
                device->imr=gs.privileged_imr;device->csr_events=gs.privileged_csr_events;
                device->busdir=gs.privileged_busdir;return;
            }
            device->csr_events&=~(std::uint32_t(data)&0x1f);break;
        case 0x1010:device->imr=data;break;
        case 0x1040:if(data&~1ull)throw std::runtime_error("invalid GS BUSDIR value");device->busdir=data;break;
        default:throw std::runtime_error("unimplemented GS privileged register write");
        }
        device->push(DeviceKind::gs_privileged,address,data);
    }
    void gs_raise_csr_events(std::uint32_t bits) {
        if(device){device->csr_events|=bits;device->push(DeviceKind::gs_csr_events,bits,0);}
        else gs.privileged_csr_events|=bits;
        sync_gs_interrupt();
    }
    std::uint64_t gs_imr() const {return device?device->imr:gs.privileged_imr;}
    void gs_put_imr(std::uint64_t value) {
        if(device){device->imr=value;device->push(DeviceKind::gs_imr,0,value);}
        else gs.privileged_imr=value;
        sync_gs_interrupt();
    }
    bool pump_ipu_output() {
        auto& c=dmac.channels[3];
        if(!dmac.enabled(3) || !(c.chcr&0x100))return false;
        if(c.chcr&0x7c)throw Fault(pc,"IPU output DMA mode unsupported");
        if(dmac.control&0xfc)throw Fault(pc,"IPU output DMA stall/MFIFO mode unsupported");
        if(!c.qwc){c.chcr&=~0x100u;dmac.raise(1u<<3);return true;}
        if(!ipu.output_count)return false;
        const bool spr=(c.madr&0x80000000u)!=0;
        const auto offset=c.madr&0x7fffffffu;
        const auto size=spr?scratch.size():ram.size();
        if(offset%16 || std::uint64_t(offset)+16>size)
            throw Fault(pc,"IPU DMA destination is outside implemented memory");
        auto* p=(spr?scratch.data():ram.data())+offset;
        const auto q=ipu.output[0];
        for(unsigned n=0;n<8;++n){p[n]=std::uint8_t(q.low>>(n*8));p[n+8]=std::uint8_t(q.high>>(n*8));}
        ipu.pop_output();c.madr+=16;--c.qwc;
        if(!c.qwc){c.chcr&=~0x100u;dmac.raise(1u<<3);}
        return true;
    }
    bool pump_ipu_input() {
        if(ipu.input_count==ipu.input.size())return false;
        try {
            return dmac.pump_ipu_input([this](std::uint32_t source) {
                const bool spr=(source&0x80000000u)!=0;
                const auto offset=source&0x7fffffffu;
                const auto size=spr?scratch.size():ram.size();
                if(offset%16 || std::uint64_t(offset)+16>size)
                    throw Fault(pc,"IPU DMA source is outside implemented memory");
                const auto* p=(spr?scratch.data():ram.data())+offset;
                SifLink::Quad result{};
                for(unsigned n=0;n<8;++n){result.low|=std::uint64_t(p[n])<<(n*8);result.high|=std::uint64_t(p[n+8])<<(n*8);}
                return result;
            },[this](const SifLink::Quad& quad) {
                if(ipu.input_count==ipu.input.size())return false;
                ipu.push({quad.low,quad.high});return true;
            });
        } catch(const std::runtime_error& e){throw Fault(pc,e.what());}
    }
    bool pump_gif() {
        try {
            return dmac.pump_gif([this](std::uint32_t source) {
                const bool spr=(source&0x80000000u)!=0;
                const auto offset=source&0x7fffffffu;
                const auto size=spr?scratch.size():ram.size();
                if(offset%16 || std::uint64_t(offset)+16>size)
                    throw Fault(pc,"GIF DMA source is outside implemented memory");
                const auto* p=(spr?scratch.data():ram.data())+offset;
                SifLink::Quad result{};
                for(unsigned n=0;n<8;++n) {result.low|=std::uint64_t(p[n])<<(n*8);result.high|=std::uint64_t(p[n+8])<<(n*8);}
                return result;
            }, [this](const SifLink::Quad& quad) {
                if(device)device->push(DeviceKind::gif_qword,0,quad.low,quad.high);
                else gif.submit_qword(quad.low,quad.high,gs);
                sync_gs_interrupt();return true;
            });
        } catch(const std::runtime_error& e) {throw Fault(pc,e.what());}
    }
    std::size_t rasterize_gs_draws() {
        try {
            return gs.rasterize_pending_draws();
        } catch(const std::runtime_error& e) {throw Fault(pc,e.what());}
    }
    bool pump_vif1() {
        try {
            return dmac.pump_vif1([this](std::uint32_t source) {
                const bool spr=(source&0x80000000u)!=0;
                const auto offset=source&0x7fffffffu;
                const auto size=spr?scratch.size():ram.size();
                if(offset%16 || std::uint64_t(offset)+16>size)
                    throw Fault(pc,"VIF1 DMA source is outside implemented memory");
                const auto* p=(spr?scratch.data():ram.data())+offset;
                SifLink::Quad result{};
                for(unsigned n=0;n<8;++n) {result.low|=std::uint64_t(p[n])<<(n*8);result.high|=std::uint64_t(p[n+8])<<(n*8);}
                return result;
            },[this](const SifLink::Quad& quad) {
                if(device)device->push(DeviceKind::vif_qword,0,quad.low,quad.high);
                else vif1.submit_qword(quad.low,quad.high,gif,gs);
                sync_gs_interrupt();return true;
            },[this](std::uint64_t high) {
                if(device)device->push(DeviceKind::vif_tag,0,high);
                else vif1.submit_dma_tag(high,gif,gs);
                sync_gs_interrupt();return true;
            });
        } catch(const std::runtime_error& e) {throw Fault(pc,e.what());}
    }
    void w(unsigned i, std::uint64_t v) { if (i) gpr.at(i).lo = v; }
    void plogic(unsigned d, unsigned s, unsigned t, unsigned kind) {
        const Register a = s ? gpr.at(s) : Register{};
        const Register b = t ? gpr.at(t) : Register{};
        auto pair=[kind](std::uint64_t x,std::uint64_t y) {
            return kind==0 ? x&y : kind==1 ? x|y : kind==2 ? x^y : ~(x|y);
        };
        if (d) gpr.at(d) = {pair(a.lo,b.lo),pair(a.hi,b.hi)};
    }
    void pxor(unsigned d,unsigned s,unsigned t) {plogic(d,s,t,2);}
    void psubb(unsigned d,unsigned s,unsigned t) {
        const Register a=s ? gpr.at(s) : Register{}, b=t ? gpr.at(t) : Register{};
        auto pair=[](std::uint64_t x,std::uint64_t y) {
            std::uint64_t result=0;
            for(unsigned shift=0;shift<64;shift+=8)
                result|=(((x>>shift)-(y>>shift))&255)<<shift;
            return result;
        };
        if(d) gpr.at(d)={pair(a.lo,b.lo),pair(a.hi,b.hi)};
    }
    void psubh(unsigned d,unsigned s,unsigned t) {
        // PSUBH (EE instruction manual p271): eight independent modulo-16-bit
        // subtractions across the 128-bit GPR pair.
        const Register a=s ? gpr.at(s) : Register{}, b=t ? gpr.at(t) : Register{};
        auto pair=[](std::uint64_t x,std::uint64_t y) {
            std::uint64_t result=0;
            for(unsigned shift=0;shift<64;shift+=16)
                result|=(((x>>shift)-(y>>shift))&0xffffu)<<shift;
            return result;
        };
        if(d)gpr.at(d)={pair(a.lo,b.lo),pair(a.hi,b.hi)};
    }
    void paddh(unsigned d,unsigned s,unsigned t) {
        // EE Core Instruction Manual p161: eight modulo-16-bit lane sums.
        const Register a=s?gpr.at(s):Register{},b=t?gpr.at(t):Register{};
        auto pair=[](std::uint64_t x,std::uint64_t y) {
            std::uint64_t result=0;
            for(unsigned shift=0;shift<64;shift+=16)
                result|=(((x>>shift)+(y>>shift))&65535ull)<<shift;
            return result;
        };
        if(d)gpr.at(d)={pair(a.lo,b.lo),pair(a.hi,b.hi)};
    }
    void extend_bytes(unsigned d,unsigned s,unsigned t,bool upper) {
        const Register a=s?gpr.at(s):Register{},b=t?gpr.at(t):Register{};
        const auto x=upper?a.hi:a.lo,y=upper?b.hi:b.lo;Register out{};
        for(unsigned n=0;n<8;++n)
            (n<4?out.lo:out.hi)|=(((y>>(n*8))&255)|(((x>>(n*8))&255)<<8))<<((n%4)*16);
        if(d)gpr.at(d)=out;
    }
    void extend_words(unsigned d,unsigned s,unsigned t,bool upper) {
        // EE Core Instruction Set Manual v6.0 pp205/208. Snapshot both sources
        // before writing an aliased destination; the low output word comes from rt.
        const Register a=s?gpr.at(s):Register{},b=t?gpr.at(t):Register{};
        const auto x=upper?a.hi:a.lo,y=upper?b.hi:b.lo;
        const Register out{std::uint64_t(std::uint32_t(y))|(x<<32),
                           (y>>32)|(x&0xffffffff00000000ull)};
        if(d)gpr.at(d)=out;
    }
    void compare_halfwords(unsigned d,unsigned s,unsigned t) {
        const Register a=s?gpr.at(s):Register{},b=t?gpr.at(t):Register{};
        auto pair=[](std::uint64_t x,std::uint64_t y){
            std::uint64_t out=0;
            for(unsigned n=0;n<64;n+=16)if((((x>>n)&65535)^32768)>(((y>>n)&65535)^32768))out|=65535ull<<n;
            return out;
        };
        if(d)gpr.at(d)={pair(a.lo,b.lo),pair(a.hi,b.hi)};
    }
    void shift_halfwords(unsigned d,unsigned t,unsigned amount,unsigned kind) {
        if(amount>15 || kind>2)throw Fault(pc,"unsupported packed halfword shift");
        const Register a=t?gpr.at(t):Register{};
        auto pair=[=](std::uint64_t x){
            std::uint64_t out=0;
            for(unsigned n=0;n<64;n+=16) {
                const unsigned v=(x>>n)&65535;
                unsigned shifted=kind==0?v<<amount:v>>amount;
                if(kind==2 && amount && (v&32768))shifted|=65535u<<(16-amount);
                out|=std::uint64_t(shifted&65535)<<n;
            }
            return out;
        };
        if(d)gpr.at(d)={pair(a.lo),pair(a.hi)};
    }
    void funnel_quad(unsigned d,unsigned s,unsigned t) {
        if(sa>127 || (sa&7))throw Fault(pc,"QFSRV requires byte-aligned SA");
        const Register a=s?gpr.at(s):Register{},b=t?gpr.at(t):Register{};
        const std::uint64_t lanes[]={b.lo,b.hi,a.lo,a.hi};
        const unsigned word=sa/64,shift=sa%64;
        auto lane=[&](unsigned n){return shift?(lanes[word+n]>>shift)|(lanes[word+n+1]<<(64-shift)):lanes[word+n];};
        if(d)gpr.at(d)={lane(0),lane(1)};
    }
    void psubsat(unsigned d,unsigned s,unsigned t,unsigned bits) {
        const Register a=s ? gpr.at(s) : Register{}, b=t ? gpr.at(t) : Register{};
        auto pair=[bits](std::uint64_t x,std::uint64_t y) {
            const auto mask=bits==32 ? 0xffffffffull : (1ull<<bits)-1;
            const auto sign=1ull<<(bits-1);
            const auto minimum=-std::int64_t(sign), maximum=std::int64_t(sign-1);
            std::uint64_t result=0;
            for(unsigned shift=0;shift<64;shift+=bits) {
                const auto decode=[=](std::uint64_t value) {
                    value=(value>>shift)&mask;
                    return value&sign ? std::int64_t(value)-std::int64_t(1ull<<bits) : std::int64_t(value);
                };
                auto value=decode(x)-decode(y);
                if(value<minimum)value=minimum;else if(value>maximum)value=maximum;
                result|=(std::uint64_t(value)&mask)<<shift;
            }
            return result;
        };
        if(d)gpr.at(d)={pair(a.lo,b.lo),pair(a.hi,b.hi)};
    }
    void psubsb(unsigned d,unsigned s,unsigned t) { psubsat(d,s,t,8); }
    void psubsh(unsigned d,unsigned s,unsigned t) { psubsat(d,s,t,16); }
    void psubsw(unsigned d,unsigned s,unsigned t) { psubsat(d,s,t,32); }
    void paddsat(unsigned d,unsigned s,unsigned t,unsigned bits) {
        const Register a=s ? gpr.at(s) : Register{}, b=t ? gpr.at(t) : Register{};
        auto pair=[bits](std::uint64_t x,std::uint64_t y) {
            const auto mask=bits==32 ? 0xffffffffull : (1ull<<bits)-1;
            const auto sign=1ull<<(bits-1);
            const auto minimum=-std::int64_t(sign), maximum=std::int64_t(sign-1);
            std::uint64_t result=0;
            for(unsigned shift=0;shift<64;shift+=bits) {
                const auto decode=[=](std::uint64_t value) {
                    value=(value>>shift)&mask;
                    return value&sign ? std::int64_t(value)-std::int64_t(1ull<<bits) : std::int64_t(value);
                };
                auto value=decode(x)+decode(y);
                if(value<minimum)value=minimum;else if(value>maximum)value=maximum;
                result|=(std::uint64_t(value)&mask)<<shift;
            }
            return result;
        };
        if(d)gpr.at(d)={pair(a.lo,b.lo),pair(a.hi,b.hi)};
    }
    void paddsb(unsigned d,unsigned s,unsigned t) { paddsat(d,s,t,8); }
    void paddsh(unsigned d,unsigned s,unsigned t) { paddsat(d,s,t,16); }
    void paddsw(unsigned d,unsigned s,unsigned t) { paddsat(d,s,t,32); }
    void psubw(unsigned d,unsigned s,unsigned t) {
        const Register a=s ? gpr.at(s) : Register{}, b=t ? gpr.at(t) : Register{};
        auto pair=[](std::uint64_t x,std::uint64_t y) {
            const auto low=std::uint32_t(x)-std::uint32_t(y);
            const auto high=std::uint32_t(x>>32)-std::uint32_t(y>>32);
            return std::uint64_t(low)|(std::uint64_t(high)<<32);
        };
        if(d) gpr.at(d)={pair(a.lo,b.lo),pair(a.hi,b.hi)};
    }
    void pcpy(unsigned d,unsigned s,unsigned t,unsigned kind) {
        const auto a=s ? gpr.at(s) : Register{}, b=t ? gpr.at(t) : Register{};
        if(!d) return;
        if(kind==0) gpr.at(d)={(b.lo&65535)*0x0001000100010001ull,(b.hi&65535)*0x0001000100010001ull};
        else if(kind==1) gpr.at(d)={b.lo,a.lo};
        else gpr.at(d)={a.hi,b.hi};
    }
    void padduw(unsigned d, unsigned s, unsigned t) {
        const Register a = s ? gpr.at(s) : Register{};
        const Register b = t ? gpr.at(t) : Register{};
        auto pair = [](std::uint64_t x, std::uint64_t y) {
            const auto low = std::uint64_t(std::uint32_t(x)) + std::uint32_t(y);
            const auto high = (x >> 32) + (y >> 32);
            return (low > 0xffffffffull ? 0xffffffffull : low)
                | ((high > 0xffffffffull ? 0xffffffffull : high) << 32);
        };
        if (d) gpr.at(d) = {pair(a.lo, b.lo), pair(a.hi, b.hi)};
    }
    void padduh(unsigned d,unsigned s,unsigned t) {
        // PADDUH (EE instruction manual p170): eight unsigned halfword lanes
        // with independent saturation to 0xffff.
        const Register a=s ? gpr.at(s) : Register{}, b=t ? gpr.at(t) : Register{};
        auto pair=[](std::uint64_t x,std::uint64_t y) {
            std::uint64_t result=0;
            for(unsigned shift=0;shift<64;shift+=16) {
                const auto sum=((x>>shift)&0xffffu)+((y>>shift)&0xffffu);
                result|=std::uint64_t(sum>0xffffu?0xffffu:sum)<<shift;
            }
            return result;
        };
        if(d)gpr.at(d)={pair(a.lo,b.lo),pair(a.hi,b.hi)};
    }
    void paddub(unsigned d, unsigned s, unsigned t) {
        // PADDUB (EE instruction manual p168): add each of the sixteen
        // unsigned byte lanes and clamp each independently to 0xff.
        const Register a=s ? gpr.at(s) : Register{}, b=t ? gpr.at(t) : Register{};
        auto pair=[](std::uint64_t x,std::uint64_t y) {
            std::uint64_t result=0;
            for(unsigned shift=0;shift<64;shift+=8) {
                const auto sum=((x>>shift)&255u)+((y>>shift)&255u);
                result|=std::uint64_t(sum>255u?255u:sum)<<shift;
            }
            return result;
        };
        if(d) gpr.at(d)={pair(a.lo,b.lo),pair(a.hi,b.hi)};
    }
    void add_immediate_word(unsigned d,unsigned source,std::int32_t immediate) {
        const auto value=r(source);
        if(value!=sx32(std::uint32_t(value)))throw Fault(pc,"ADDI of noncanonical word is undefined");
        const auto sum=std::int64_t(std::int32_t(value))+immediate;
        if(sum<std::numeric_limits<std::int32_t>::min() || sum>std::numeric_limits<std::int32_t>::max())
            throw Fault(pc,"ADDI integer-overflow exception requires implementation");
        w(d,sx32(std::uint32_t(sum)));
    }
    void half_minmax(unsigned d,unsigned s,unsigned t,bool maximum) {
        const Register a=s?gpr.at(s):Register{},b=t?gpr.at(t):Register{};
        auto pair=[maximum](std::uint64_t x,std::uint64_t y) {
            std::uint64_t value=0;
            for(unsigned shift=0;shift<64;shift+=16) {
                const auto ax=std::uint16_t(x>>shift),by=std::uint16_t(y>>shift);
                const bool less=(ax^0x8000u)<(by^0x8000u);
                value|=std::uint64_t(less==maximum?by:ax)<<shift;
            }
            return value;
        };
        if(d)gpr.at(d)={pair(a.lo,b.lo),pair(a.hi,b.hi)};
    }
    void pack_bytes(unsigned d,unsigned s,unsigned t) {
        const Register a=s?gpr.at(s):Register{},b=t?gpr.at(t):Register{};
        auto pack=[](const Register& v) {
            std::uint64_t result=0;
            for(unsigned n=0;n<8;++n)result|=(((n<4?v.lo:v.hi)>>((n%4)*16))&255)<<(n*8);
            return result;
        };
        if(d)gpr.at(d)={pack(b),pack(a)};
    }
    void subtract_word(unsigned d,unsigned s,unsigned t) {
        // SUB (EE instruction manual p114).  The result only commits when the
        // operands are canonical word values and signed 32-bit subtraction
        // does not overflow; integer-overflow exception delivery is not yet a
        // modeled EE exception, so preserve the register and fault explicitly.
        const auto a=r(s), b=r(t);
        if(a!=sx32(std::uint32_t(a)) || b!=sx32(std::uint32_t(b)))
            throw Fault(pc,"SUB of noncanonical word operands is undefined");
        const auto x=std::int64_t(std::int32_t(a)), y=std::int64_t(std::int32_t(b));
        const auto result=x-y;
        if(result < std::numeric_limits<std::int32_t>::min() || result > std::numeric_limits<std::int32_t>::max())
            throw Fault(pc,"SUB integer-overflow exception requires implementation");
        w(d,sx32(std::uint32_t(result)));
    }
    void divide_word(unsigned source,unsigned divisor,bool is_unsigned,bool bank1=false) {
        const auto a=std::uint32_t(r(source)), b=std::uint32_t(r(divisor));
        if(r(source)!=sx32(a) || r(divisor)!=sx32(b))
            throw Fault(pc,"division of noncanonical word operands requires validation");
        if(!b) throw Fault(pc,"undefined divide-by-zero result requires validation");
        auto& quotient=bank1?lo1:lo;
        auto& remainder=bank1?hi1:hi;
        if(is_unsigned) {quotient=sx32(a/b); remainder=sx32(a%b);}
        else {
            const auto x=std::int64_t(a)-(a&0x80000000u ? 0x100000000ll:0ll);
            const auto y=std::int64_t(b)-(b&0x80000000u ? 0x100000000ll:0ll);
            // Widened division also defines INT_MIN/-1 without host UB.
            quotient=sx32(std::uint32_t(x/y)); remainder=sx32(std::uint32_t(x%y));
        }
    }
    void multiply_word(unsigned dest,unsigned left,unsigned right,bool is_unsigned,bool bank1,bool accumulate=false) {
        const auto a=std::uint32_t(r(left)), b=std::uint32_t(r(right));
        if(r(left)!=sx32(a) || r(right)!=sx32(b))
            throw Fault(pc,"multiplication of noncanonical word operands requires validation");
        std::uint64_t product;
        if(is_unsigned) product=std::uint64_t(a)*b;
        else {
            const auto x=std::int64_t(a)-(a&0x80000000u ? 0x100000000ll:0ll);
            const auto y=std::int64_t(b)-(b&0x80000000u ? 0x100000000ll:0ll);
            product=std::uint64_t(x*y);
        }
        if(accumulate) product+=(std::uint64_t(std::uint32_t(bank1?hi1:hi))<<32)
            | std::uint32_t(bank1?lo1:lo);
        const auto lower=sx32(std::uint32_t(product)), upper=sx32(std::uint32_t(product>>32));
        if(bank1) {lo1=lower; hi1=upper;} else {lo=lower; hi=upper;}
        w(dest,lower);
    }
    std::uint8_t* memory(std::uint32_t address, unsigned size) {
        if (size != 1 && size != 2 && size != 4 && size != 8)
            throw Fault(pc, "invalid scalar memory access size");
        if (address % size) throw Fault(pc, "unaligned memory access");
        if (address >= 0x70000000u && std::uint64_t(address) + size <= 0x70004000ull)
            return scratch.data() + (address - 0x70000000u);
        // Direct RAM and the explicitly configured native boot aliases.
        const auto physical = physical_address(address);
        if (protect_kernel_memory && physical < 0x00100000u
            && !(boot.table_ready && std::uint64_t(physical)+size<=0x80000)) {
            bool code_region=false;
            for(const auto& range:kernel_code_regions)
                if(physical>=range.first && std::uint64_t(physical)+size<=range.second) {code_region=true;break;}
            if(!code_region)throw Fault(pc, "guest kernel memory requires an explicit native mapping");
        }
        if (std::uint64_t(physical) + size > ram.size())
            throw Fault(pc, "unimplemented MMIO/TLB or out-of-range memory");
        return ram.data() + physical;
    }
    // AOT-known widths keep common user-RAM and scratchpad accesses bounded.
    // Protected kernel memory, devices, invalid spans/alignment and diagnostic
    // write watches retain the complete checked path below.
    template<unsigned Size,bool Sign> std::uint64_t load_scalar(std::uint32_t address) {
        static_assert(Size==1||Size==2||Size==4||Size==8);
        const auto physical=physical_address(address);
        if(physical>=0x00100000u && physical<0x02000000u && !(physical&(Size-1)) &&
           std::uint64_t(physical)+Size<=ram.size()) {
            const auto* p=ram.data()+physical;std::uint64_t value=0;
            for(unsigned n=0;n<Size;++n)value|=std::uint64_t(p[n])<<(8*n);
            if constexpr(Sign && Size<8)if(value&(1ull<<(Size*8-1)))value|=~0ull<<(Size*8);
            return value;
        }
        if(address>=0x70000000u && std::uint64_t(address)+Size<=0x70004000ull && !(address&(Size-1))) {
            const auto* p=scratch.data()+(address-0x70000000u);std::uint64_t value=0;
            for(unsigned n=0;n<Size;++n)value|=std::uint64_t(p[n])<<(8*n);
            if constexpr(Sign && Size<8)if(value&(1ull<<(Size*8-1)))value|=~0ull<<(Size*8);
            return value;
        }
        return load(address,Size,Sign);
    }
    template<unsigned Size> void store_scalar(std::uint32_t address,std::uint64_t value) {
        static_assert(Size==1||Size==2||Size==4||Size==8);
        const auto physical=physical_address(address);
        const auto watched=ram_write_trace_word(physical,Size);
        if(physical>=0x00100000u && physical<0x02000000u && !(physical&(Size-1)) &&
           std::uint64_t(physical)+Size<=ram.size() &&
           !watched) {
            auto* p=ram.data()+physical;
            for(unsigned n=0;n<Size;++n)p[n]=std::uint8_t(value>>(8*n));
            return;
        }
        if(address>=0x70000000u && std::uint64_t(address)+Size<=0x70004000ull && !(address&(Size-1))) {
            auto* p=scratch.data()+(address-0x70000000u);
            for(unsigned n=0;n<Size;++n)p[n]=std::uint8_t(value>>(8*n));
            return;
        }
        store(address,Size,value);
    }
    std::uint64_t load(std::uint32_t address, unsigned size, bool sign) {
        address = physical_address(address);
        // No implemented device occupies the physical 32 MiB RAM window.
        // Ordinary RAM still passes the same alignment, mapping and span checks.
        if(address>=0x02000000u) {
        if(address==0x10003020u) {
            if(size!=4)throw Fault(pc,"GIF STAT requires 32-bit access");
            try {
                if(device)return device->read_transport_register(address,device->busdir!=0);
                return gif.read_status(vif1.path3_masked,gs.privileged_busdir!=0);
            }
            catch(const std::runtime_error& e){throw Fault(pc,e.what());}
        }
        if(Vif1Path::register_contains(address)) {
            if(size!=4)throw Fault(pc,"VIF1 registers require 32-bit access");
            try {
                const auto v=device?device->read_transport_register(address,false):vif1.read_register(address);
                return sign?sx32(v):v;
            }
            catch(const std::runtime_error& e){throw Fault(pc,e.what());}
        }
        if(Ipu::register_contains(address)) {
            try {const auto v=ipu.read(address,size);return sign&&size==4?sx32(std::uint32_t(v)):v;}
            catch(const std::runtime_error& e){throw Fault(pc,e.what());}
        }
        if(GsRegisterState::privileged_contains(address)) {
            if(size!=8)throw Fault(pc,"GS privileged registers require 64-bit LD/SD width");
            try{if(device)device->join();return gs.read_privileged(address);}
            catch(const std::runtime_error& e){throw Fault(pc,e.what());}
        }
        if(Dmac::contains(address)) {
            if(size!=4)throw Fault(pc,"DMA register accesses require 32-bit width");
            try {const auto v=dmac.read(address);return sign ? sx32(v) : v;}
            catch(const std::runtime_error& e) {throw Fault(pc,e.what());}
        }
        if(InterruptController::contains(address)) {
            if(size!=4) throw Fault(pc,"INTC accesses require 32-bit width");
            const auto v=intc.read(address); return sign ? sx32(v) : v;
        }
        if(Timers::contains(address)) {
            if(size!=4) throw Fault(pc,"timer accesses currently require 32-bit width");
            try {const auto v=timers.read(address); return sign ? sx32(v) : v;}
            catch(const std::runtime_error& e) {throw Fault(pc,e.what());}
        }
        }
        auto p = memory(address, size);
        std::uint64_t v = 0;
        for (unsigned i = 0; i < size; ++i) v |= std::uint64_t(p[i]) << (8 * i);
        if (sign && size < 8 && (v & (1ull << (size * 8 - 1)))) v |= ~0ull << (size * 8);
        return v;
    }
    void store(std::uint32_t address, unsigned size, std::uint64_t value) {
        address = physical_address(address);
        if(address>=0x02000000u) {
        if(address==0x10003000u) {
            if(size!=4)throw Fault(pc,"GIF CTRL requires 32-bit access");
            try {
                if(device)device->push(DeviceKind::gif_control,0,std::uint32_t(value));
                else gif.write_control(std::uint32_t(value));
                return;
            }
            catch(const std::runtime_error& e){throw Fault(pc,e.what());}
        }
        if(Vif1Path::register_contains(address)) {
            if(size!=4)throw Fault(pc,"VIF1 registers require 32-bit access");
            try {
                if(device)device->push(DeviceKind::vif_register,address,std::uint32_t(value));
                else vif1.write_register(address,std::uint32_t(value));
                return;
            }
            catch(const std::runtime_error& e){throw Fault(pc,e.what());}
        }
        if(Ipu::register_contains(address)) {
            try {ipu.write(address,size,value);return;}
            catch(const std::runtime_error& e){throw Fault(pc,e.what());}
        }
        if(GsRegisterState::privileged_contains(address)) {
            if(size!=8)throw Fault(pc,"GS privileged registers require 64-bit LD/SD width");
            try {
                if(device)device_write_privileged(address,value);
                else gs.write_privileged(address,value);
                sync_gs_interrupt();return;
            }
            catch(const std::runtime_error& e){throw Fault(pc,e.what());}
        }
        if(Dmac::contains(address)) {
            if(size!=4)throw Fault(pc,"DMA register accesses require 32-bit width");
            try {dmac.write(address,std::uint32_t(value));return;}
            catch(const std::runtime_error& e) {throw Fault(pc,e.what());}
        }
        if(InterruptController::contains(address)) {
            if(size!=4) throw Fault(pc,"INTC accesses require 32-bit width");
            intc.write(address,std::uint32_t(value)); return;
        }
        if(Timers::contains(address)) {
            if(size!=4) throw Fault(pc,"timer accesses currently require 32-bit width");
            try {timers.write(address,std::uint32_t(value));return;}
            catch(const std::runtime_error& e) {throw Fault(pc,e.what());}
        }
        }
        const std::uint32_t traced_word=ram_write_trace_word(address,size);
        const bool trace_write=traced_word!=0;
        std::uint32_t before=0;
        if(trace_write)for(unsigned n=0;n<4;++n)before|=std::uint32_t(ram[traced_word+n])<<(n*8);
        auto p = memory(address, size);
        for (unsigned i = 0; i < size; ++i) p[i] = std::uint8_t(value >> (8 * i));
        if(trace_write) {
            std::uint32_t after=0;for(unsigned n=0;n<4;++n)after|=std::uint32_t(ram[traced_word+n])<<(n*8);
            if(after!=before)ram_write_trace[ram_write_trace_cursor++%ram_write_trace.size()]={pc,std::uint32_t(r(31)),address,before,after,size,boot.current_thread};
        }
    }
    // EE LDL/LDR and SDL/SDR use an aligned doubleword and merge the selected
    // little-endian lanes. The addressed doubleword is validated before any
    // register or memory change, so a bad alias cannot partially update state.
    void load_double_unaligned(unsigned reg,std::uint32_t address,bool left) {
        const auto aligned=address&~7u;
        const std::uint64_t word=load(aligned,8,false), old=reg?gpr.at(reg).lo:0;
        const auto shift=(left?7u-(address&7u):address&7u)*8u;
        const auto mask=left?(~std::uint64_t(0)<<shift):(~std::uint64_t(0)>>shift);
        w(reg,(old&~mask)|(left?(word<<shift):(word>>shift)));
    }
    void store_double_unaligned(std::uint32_t address,std::uint64_t value,bool left) {
        const auto aligned=address&~7u;
        const std::uint64_t old=load(aligned,8,false);
        const auto shift=(left?7u-(address&7u):address&7u)*8u;
        const auto mask=left?(~std::uint64_t(0)>>shift):(~std::uint64_t(0)<<shift);
        store(aligned,8,(old&~mask)|(left?(value>>shift):(value<<shift)));
    }
    void load_word_unaligned(unsigned reg,std::uint32_t address,bool left) {
        const auto aligned=address&~3u;
        const std::uint32_t word=std::uint32_t(load(aligned,4,false)), old=reg?std::uint32_t(gpr.at(reg).lo):0;
        const auto shift=(left?3u-(address&3u):address&3u)*8u;
        const std::uint32_t mask=left?(~std::uint32_t(0)<<shift):(~std::uint32_t(0)>>shift);
        w(reg,hg::sx32((old&~mask)|(left?(word<<shift):(word>>shift))));
    }
    void store_word_unaligned(std::uint32_t address,std::uint32_t value,bool left) {
        const auto aligned=address&~3u;
        const std::uint32_t old=std::uint32_t(load(aligned,4,false));
        const auto shift=(left?3u-(address&3u):address&3u)*8u;
        const std::uint32_t mask=left?(~std::uint32_t(0)>>shift):(~std::uint32_t(0)<<shift);
        store(aligned,4,(old&~mask)|(left?(value>>shift):(value<<shift)));
    }
    void store_quad(std::uint32_t address, unsigned reg) {
        address &= ~15u;
        const auto physical=physical_address(address);
        // Validate the entire ordinary RAM/scratch span before either half changes.
        // Fixed-width stores retain the diagnostic write-watch behavior.
        if((physical>=0x00100000u && physical<0x02000000u &&
            std::uint64_t(physical)+16<=ram.size()) ||
           (address>=0x70000000u && std::uint64_t(address)+16<=0x70004000ull)) {
            const auto value=reg?gpr.at(reg):Register{};
            store_scalar<8>(address,value.lo);store_scalar<8>(address+8,value.hi);
            return;
        }
        if(physical_address(address)==0x10005000u) {
            const auto value=reg?gpr.at(reg):Register{};
            try {
                if(device)device->push(DeviceKind::vif_qword,0,value.lo,value.hi);
                else vif1.submit_qword(value.lo,value.hi,gif,gs);
                sync_gs_interrupt();return;
            }
            catch(const std::runtime_error& e){throw Fault(pc,e.what());}
        }
        if(Ipu::input_fifo_contains(physical_address(address))) {
            const auto value=reg?gpr.at(reg):Register{};
            try {ipu.push({value.lo,value.hi});return;}
            catch(const std::runtime_error& e){throw Fault(pc,e.what());}
        }
        // Validate both halves before writing either, preserving atomic failure.
        memory(address, 8); memory(address + 8, 8);
        const auto value = reg ? gpr.at(reg) : Register{};
        store(address, 8, value.lo); store(address + 8, 8, value.hi);
    }
    void load_quad(std::uint32_t address, unsigned reg) {
        address &= ~15u;
        const Register value{load_scalar<8,false>(address), load_scalar<8,false>(address + 8)};
        if (reg) gpr.at(reg) = value;
    }
    std::uint32_t vu_control_read(unsigned reg) {
        if(reg==28)return vu0.fbrst;
        if(reg==29) {
            if(vu0.q_pending)throw Fault(pc,"VPU status polling needs timed Q completion");
            return 0; // Both VUs are Ready; microprogram activation is unsupported.
        }
        throw Fault(pc,"unimplemented VU control register read "+std::to_string(reg));
    }
    void vu_control_write(unsigned reg,std::uint32_t value) {
        if(reg!=28)throw Fault(pc,"unimplemented VU control register write "+std::to_string(reg));
        if(value&~0x0e0eu)throw Fault(pc,"unsupported VU force break or reserved FBRST bits");
        // VU manual pp21/203: reset returns control to Ready, preserves
        // vector/local-memory data, and reset bits always read zero.
        if(value&2){vu0.mac=0;vu0.status=0;vu0.q_pending=false;}
        // VU1 cannot have started: every microprogram activation still faults.
        vu0.fbrst=value&0x0c0cu;
    }
    void vu_sqrt(unsigned source,unsigned lane) {
        if(vu0.q_pending)throw Fault(pc,"VU Q producer requires synchronized completion");
        const auto& reg=vu0.vf.at(source);
        auto bits=source?std::uint32_t((lane<2?reg.lo:reg.hi)>>((lane&1)*32)):(lane==3?0x3f800000u:0u);
        const unsigned exponent=(bits>>23)&255;
        const unsigned invalid=(bits&0x80000000u) && exponent?16:0;
        vu0.status=std::uint16_t((vu0.status&~48u)|invalid|(invalid<<6));
        vu0.pending_q=0;
        if(exponent) {
            const unsigned odd=(exponent&1)^1;
            const auto radicand=std::uint64_t((bits&0x7fffffu)|0x800000u)<<(23+odd);
            std::uint64_t root=0;
            for(std::uint64_t bit=1ull<<23;bit;bit>>=1) {
                const auto candidate=root|bit;
                if(candidate*candidate<=radicand)root=candidate;
            }
            const auto result_exponent=(int(exponent)-127-int(odd))/2+127;
            vu0.pending_q=(std::uint32_t(result_exponent)<<23)|(std::uint32_t(root)&0x7fffffu);
        }
        vu0.q_pending=true;
    }
    void vu_waitq() {
        if(vu0.q_pending){vu0.q=vu0.pending_q;vu0.q_pending=false;}
    }
    void vu_divide(unsigned source,unsigned other,unsigned source_lane,unsigned other_lane) {
        if(vu0.q_pending)throw Fault(pc,"VU Q producer requires synchronized completion");
        const auto read=[&](unsigned reg,unsigned lane) {
            if(!reg)return lane==3?0x3f800000u:0u;
            const auto& value=vu0.vf.at(reg);
            return std::uint32_t((lane<2?value.lo:value.hi)>>((lane&1)*32));
        };
        Fpu arithmetic;vu0.pending_q=arithmetic.divide(read(source,source_lane),read(other,other_lane));
        const unsigned flags=((arithmetic.control&0x20000)?16:0)|((arithmetic.control&0x10000)?32:0);
        vu0.status=std::uint16_t((vu0.status&~48u)|flags|(flags<<6));vu0.q_pending=true;
    }
    void vu_arithmetic(unsigned destination,unsigned source,unsigned other,unsigned mask,bool multiply,unsigned broadcast) {
        if(broadcast==4 && vu0.q_pending)
            throw Fault(pc,"VU Q read requires WAITQ in the bounded macro profile");
        // VU manual pp26-29,39-42: 24-bit truncating arithmetic, exponent
        // saturation, per-lane MAC flags and accumulated status flags.
        const auto read=[&](unsigned reg,unsigned lane) {
            if(!reg)return lane==3?0x3f800000u:0u;
            const auto& value=vu0.vf.at(reg);
            return std::uint32_t((lane<2?value.lo:value.hi)>>((lane&1)*32));
        };
        std::array<std::uint32_t,4> results{};
        std::uint16_t mac=0;
        const bool subtract_broadcast=!multiply && broadcast>=8;
        for(unsigned lane=0;lane<4;++lane)if(mask&(8u>>lane)) {
            Fpu arithmetic;
            const auto a=read(source,lane);
            auto b=broadcast==4?vu0.q:read(other,broadcast>=8?broadcast-8:(multiply || broadcast==5 || broadcast==6)?lane:broadcast);
            if(!multiply && (broadcast==5 || subtract_broadcast))b^=0x80000000u;
            const auto bits=multiply?arithmetic.multiply(a,b):vu0_add(arithmetic,a,b);
            results[lane]=bits;
            const unsigned bit=3-lane;
            if(!(bits&0x7fffffffu))mac|=1u<<bit;
            if((bits&0x80000000u) && (bits&0x7fffffffu))mac|=1u<<(bit+4);
            if(arithmetic.control&0x4000)mac|=1u<<(bit+8);
            if(arithmetic.control&0x8000)mac|=1u<<(bit+12);
        }
        vu0.mac=mac;
        unsigned flags=0;
        for(unsigned group=0;group<4;++group)if(mac&(15u<<(group*4)))flags|=1u<<group;
        vu0.status=std::uint16_t((vu0.status&~15u)|flags|(flags<<6));
        if(destination)for(unsigned lane=0;lane<4;++lane)if(mask&(8u>>lane)) {
            auto& out=vu0.vf.at(destination);auto& half=lane<2?out.lo:out.hi;
            const auto shift=(lane&1)*32;
            half=(half&~(0xffffffffull<<shift))|(std::uint64_t(results[lane])<<shift);
        }
    }
    void vu_broadcast_product(unsigned destination,unsigned source,unsigned other,
                              unsigned mask,unsigned broadcast,VuBroadcastProduct operation) {
        // Independently derived from Sony VU manual pp26-29,39-42,88,92,
        // 273,277,303. Operands and instruction selection are compiled ahead
        // of time. This retains the existing bounded 24-bit arithmetic profile;
        // physical-hardware least-bit behavior remains unmeasured (SOURCES.md).
        const auto a=vu_vector(source),b=vu_vector(other);
        const auto lane=[](Register value,unsigned n) {
            return std::uint32_t((n<2?value.lo:value.hi)>>((n&1)*32));
        };
        const auto scalar=lane(b,broadcast);
        std::array<std::uint32_t,4> results{};
        unsigned mac=0,current=0,sticky=0;
        for(unsigned n=0;n<4;++n)if(mask&(8u>>n)) {
            const auto product=Fpu::product(lane(a,n),scalar);
            auto bits=product.bits;
            bool overflow=product.overflow,underflow=product.underflow;
            if(operation!=VuBroadcastProduct::multiply_accumulator) {
                const bool acc_max=(vu0.acc[n]&0x7fffffffu)==0x7fffffffu;
                const unsigned product_flags=unsigned(!(bits&0x7fffffffu))
                    |((bits&0x80000000u)&&(bits&0x7fffffffu)?2u:0u)
                    |(underflow?4u:0u)|(overflow?8u:0u);
                // VU6.0 pp40,42: multiplication flags from MADD contribute
                // only to sticky fields; current flags describe the final sum.
                sticky|=product_flags;
                if(acc_max) {
                    // Sony VU6.0 p42: canonical +/-MAX ACC is already an
                    // overflow result.  A normal or underflowed product keeps
                    // that ACC value; an overflowing product supplies its own
                    // saturated sign.  Product flags contribute only to the
                    // sticky fields, while the final +/-MAX sets current O/S.
                    if(!overflow)bits=vu0.acc[n];
                    underflow=false;overflow=true;
                } else if(overflow) {
                    // Product overflow supplies its clamped result directly;
                    // the following addition cannot recover discarded range.
                    underflow=false;
                } else {
                    // VU6.0 pp26,29: exponent 255 is finite 1.F*2^128, not
                    // IEEE infinity/NaN.  The exact integer adder therefore
                    // handles every nonsaturated ACC encoding, including that
                    // full exponent-255 class and final add overflow/underflow.
                    Fpu arithmetic;
                    bits=vu0_add(arithmetic,vu0.acc[n],bits);
                    underflow=bool(arithmetic.control&0x4000u);
                    overflow=bool(arithmetic.control&0x8000u);
                }
            }
            results[n]=bits;
            const unsigned flags=unsigned(!(bits&0x7fffffffu))
                |((bits&0x80000000u)&&(bits&0x7fffffffu)?2u:0u)
                |(underflow?4u:0u)|(overflow?8u:0u);
            current|=flags;
            for(unsigned group=0;group<4;++group)
                if(flags&(1u<<group))mac|=1u<<(group*4+3-n);
        }
        // Commit only after every selected lane succeeds. VF aliases therefore
        // read their original values, and an explicit fault cannot half-write
        // ACC, VF, MAC or status. Inactive MAC lanes clear; sticky bits persist.
        vu0.mac=std::uint16_t(mac);
        vu0.status=std::uint16_t((vu0.status&~15u)|current|((current|sticky)<<6));
        for(unsigned n=0;n<4;++n)if(mask&(8u>>n)) {
            if(operation!=VuBroadcastProduct::add_vector)vu0.acc[n]=results[n];
            else if(destination) {
                auto& out=vu0.vf.at(destination);auto& half=n<2?out.lo:out.hi;
                const auto shift=(n&1)*32;
                half=(half&~(0xffffffffull<<shift))|(std::uint64_t(results[n])<<shift);
            }
        }
    }
    void vu_outer(unsigned destination,unsigned source,unsigned other,bool subtract) {
        const auto a=vu_vector(source),b=vu_vector(other);
        const auto lane=[](Register value,unsigned n){return std::uint32_t((n<2?value.lo:value.hi)>>((n&1)*32));};
        const auto flags=[](std::uint32_t bits,bool overflow,bool underflow){
            return unsigned(!(bits&0x7fffffffu))|((bits&0x80000000u)&&(bits&0x7fffffffu)?2u:0u)|(underflow?4u:0u)|(overflow?8u:0u);
        };
        std::array<std::uint32_t,3> result{};unsigned mac=0,sticky=0;
        for(unsigned n=0;n<3;++n) {
            const auto product=Fpu::product(lane(a,(n+1)%3),lane(b,(n+2)%3));
            unsigned current=flags(product.bits,product.overflow,product.underflow);sticky|=current;
            if(subtract) {
                // OPMSUB = ACC - product, with the same exceptional rules as the MADD/MSUB
                // family (vu_broadcast_product; Sony VU6.0 pp26-29,40-42): a canonical
                // +/-MAX ACC stays unless the product overflows, an overflowing product
                // supplies its own (negated) saturated value, and every other encoding,
                // including the exponent-255 class and an underflowed (zero) product, goes
                // through the exact integer adder. Product flags only reach the sticky bits.
                const auto negated=product.bits^0x80000000u;
                if((vu0.acc[n]&0x7fffffffu)==0x7fffffffu) {
                    result[n]=product.overflow?negated:vu0.acc[n];
                    current=flags(result[n],true,false);
                } else if(product.overflow) {
                    result[n]=negated;
                    current=flags(result[n],true,false);
                } else {
                    Fpu arithmetic;result[n]=vu0_add(arithmetic,vu0.acc[n],negated);
                    current=flags(result[n],arithmetic.control&0x8000,arithmetic.control&0x4000);
                }
                sticky|=current;
            } else result[n]=product.bits;
            for(unsigned group=0;group<4;++group)if(current&(1u<<group))mac|=1u<<(group*4+3-n);
        }
        unsigned current=0;for(unsigned group=0;group<4;++group)if(mac&(15u<<(group*4)))current|=1u<<group;
        vu0.mac=std::uint16_t(mac);vu0.status=std::uint16_t((vu0.status&~15u)|current|(sticky<<6));
        for(unsigned n=0;n<3;++n) {
            if(!subtract)vu0.acc[n]=result[n];
            else if(destination) {
                auto& reg=vu0.vf.at(destination);auto& half=n<2?reg.lo:reg.hi;const auto shift=(n&1)*32;
                half=(half&~(0xffffffffull<<shift))|(std::uint64_t(result[n])<<shift);
            }
        }
    }
    Register vu_vector(unsigned source) const {
        return source?vu0.vf.at(source):Register{0,0x3f80000000000000ull};
    }
    void qmfc2(unsigned destination,unsigned source) {
        // QMFC2: quadword transfer from VU0 VF[source] to GPR[destination].
        if(destination)gpr.at(destination)=vu_vector(source);
    }
    void qmtc2(unsigned source,unsigned destination) {
        // QMTC2: quadword transfer from GPR[source] to VU0 VF[destination].
        if(destination)vu0.vf.at(destination)=source ? gpr.at(source) : Register{};
    }
    void vabs(unsigned destination,unsigned source,unsigned mask) {
        // Macro VABS clears the IEEE-754 sign bit in each selected x/y/z/w
        // field. This preserves NaN payload bits and avoids host FP behavior.
        if (!destination || !mask) return;
        const Register input=vu_vector(source);
        auto& output=vu0.vf.at(destination);
        for (unsigned lane=0; lane<4; ++lane) {
            if (!(mask & (8u >> lane))) continue;
            const auto shift=(lane & 1u)*32u;
            const auto source_half=lane<2 ? input.lo : input.hi;
            auto& output_half=lane<2 ? output.lo : output.hi;
            const auto field=(std::uint64_t(std::uint32_t(source_half>>shift)&0x7fffffffu))<<shift;
            output_half=(output_half&~(0xffffffffull<<shift))|field;
        }
    }
    void vmove(unsigned destination,unsigned source,unsigned mask) {
        // Macro VMOVE copies only the selected x/y/z/w 32-bit fields.  It is
        // deliberately a raw-bit operation: no host floating-point behavior
        // is involved and unselected destination fields are preserved.
        if (!destination || !mask) return; // VF00 is architecturally constant.
        const Register input = vu_vector(source);
        auto& output = vu0.vf.at(destination);
        for (unsigned lane=0; lane<4; ++lane) {
            if (!(mask & (8u >> lane))) continue;
            const auto shift=(lane & 1u)*32u;
            auto& half=lane<2 ? output.lo : output.hi;
            const auto source_half=lane<2 ? input.lo : input.hi;
            half=(half & ~(0xffffffffull << shift)) | (source_half & (0xffffffffull << shift));
        }
    }
    void vmr32(unsigned destination,unsigned source,unsigned mask) {
        // Macro VMR32 selects fields from a one-field right rotation: the
        // documented x/y/z/w destinations receive source y/z/w/x.
        if (!destination || !mask) return;
        const Register input = vu_vector(source);
        auto lane = [&input](unsigned index) {
            const auto half=index<2 ? input.lo : input.hi;
            return std::uint32_t(half >> ((index & 1u)*32u));
        };
        auto& output=vu0.vf.at(destination);
        for (unsigned lane_index=0; lane_index<4; ++lane_index) {
            if (!(mask & (8u >> lane_index))) continue;
            const auto shift=(lane_index & 1u)*32u;
            auto& half=lane_index<2 ? output.lo : output.hi;
            half=(half & ~(0xffffffffull << shift)) | (std::uint64_t(lane((lane_index+1)&3)) << shift);
        }
    }
    void vu_convert(unsigned destination,unsigned source,unsigned mask,unsigned fractional_bits,bool to_float) {
        // Sony VU manual pp26-28,77-84,253-256,264-267. Macro conversions
        // use the same independently derived integer-bit arithmetic as VU1.
        // Snapshot operands for aliases; masked lanes and all flags survive.
        if(fractional_bits!=0 && fractional_bits!=4 && fractional_bits!=12 && fractional_bits!=15)
            throw Fault(pc,"invalid VU conversion fractional width");
        const Register input=vu_vector(source);
        if(!destination || !mask)return;
        auto& output=vu0.vf.at(destination);
        for(unsigned lane=0;lane<4;++lane)if(mask&(8u>>lane)) {
            const auto shift=(lane&1u)*32u;
            const auto bits=std::uint32_t((lane<2?input.lo:input.hi)>>shift);
            const auto result=to_float?Vu1State::integer_to_float(bits,fractional_bits)
                                      :Vu1State::float_to_integer(bits,fractional_bits);
            auto& half=lane<2?output.lo:output.hi;
            half=(half&~(0xffffffffull<<shift))|(std::uint64_t(result)<<shift);
        }
    }
    void lqc2(std::uint32_t address,unsigned destination) {
        // LQC2: load a full quadword from EE memory into VU0 VF[destination].
        // Alignment follows LQ/SQ and both halves are checked before commit.
        address&=~15u;
        const Register value{load(address,8,false),load(address+8,8,false)};
        if(destination)vu0.vf.at(destination)=value;
    }
    void sqc2(std::uint32_t address,unsigned source) {
        // SQC2: store the full VU0 vector register to EE memory, aligned down
        // as with SQ/LQ. Validate both native RAM halves before either write.
        address&=~15u;memory(address,8);memory(address+8,8);
        const auto value=vu_vector(source);
        store(address,8,value.lo);store(address+8,8,value.hi);
    }
    void add_float(unsigned fs, unsigned ft, unsigned fd, bool accumulator) {
        try {
            const auto result = fpu.add(fpr.at(fs), fpr.at(ft));
            if (accumulator) fpu.acc = result; else fpr.at(fd) = result;
        } catch (const std::runtime_error& e) { throw Fault(pc, e.what()); }
    }
    void sub_float(unsigned fs, unsigned ft, unsigned fd) {
        try { fpr.at(fd)=fpu.add(fpr.at(fs),fpr.at(ft)^0x80000000u); }
        catch (const std::runtime_error& e) { throw Fault(pc, e.what()); }
    }
    void mul_float(unsigned fs,unsigned ft,unsigned fd) {
        try { fpr.at(fd)=fpu.multiply(fpr.at(fs),fpr.at(ft)); }
        catch (const std::runtime_error& e) { throw Fault(pc,e.what()); }
    }
    void div_float(unsigned fs,unsigned ft,unsigned fd) {
        try { fpr.at(fd)=fpu.divide(fpr.at(fs),fpr.at(ft),true); }
        catch (const std::runtime_error& e) { throw Fault(pc,e.what()); }
    }
    void sqrt_float(unsigned ft,unsigned fd) {
        try { fpr.at(fd)=fpu.sqrt_exact(fpr.at(ft)); }
        catch (const std::runtime_error& e) { throw Fault(pc,e.what()); }
    }
    void cvt_s_w(unsigned fs,unsigned fd) {
        try { fpr.at(fd)=fpu.cvt_s_w_exact(fpr.at(fs)); }
        catch (const std::runtime_error& e) { throw Fault(pc,e.what()); }
    }
    void cvt_w_s(unsigned fs,unsigned fd) { fpr.at(fd)=fpu.cvt_w_s(fpr.at(fs)); }
    void compare_float(unsigned fs,unsigned ft,bool ordered_less,bool or_equal=false) { fpu.compare(fpr.at(fs),fpr.at(ft),ordered_less,or_equal); }
    void madd_float(unsigned fs,unsigned ft,unsigned fd) {
        try { fpr.at(fd)=fpu.madd(fpr.at(fs),fpr.at(ft)); }
        catch (const std::runtime_error& e) { throw Fault(pc,e.what()); }
    }
    void mula_float(unsigned fs,unsigned ft) {
        try { fpu.acc=fpu.multiply(fpr.at(fs),fpr.at(ft)); }
        catch (const std::runtime_error& e) { throw Fault(pc,e.what()); }
    }
    void madda_float(unsigned fs,unsigned ft) {
        try { fpu.acc=fpu.madd(fpr.at(fs),fpr.at(ft)); }
        catch (const std::runtime_error& e) { throw Fault(pc,e.what()); }
    }
    void msuba_float(unsigned fs,unsigned ft) {
        try { fpu.acc=fpu.msub(fpr.at(fs),fpr.at(ft)); }
        catch (const std::runtime_error& e) { throw Fault(pc,e.what()); }
    }
    void msub_float(unsigned fs,unsigned ft,unsigned fd) {
        try { fpr.at(fd)=fpu.msub(fpr.at(fs),fpr.at(ft)); }
        catch (const std::runtime_error& e) { throw Fault(pc,e.what()); }
    }
    void cache_load_tag(std::uint32_t address) {
        // DXLTG (EE instruction manual p303) selects index[11:6]/way[0].
        // Native RAM has no dirty cache copy: every cache tag is invalid.
        // Unsupported tag-store operations cannot create a hidden valid line.
        if(physical_address(address)>=ram.size())throw Fault(pc,"cache tag lookup outside implemented RAM");
        cp0_tag_lo=0;
    }
    void cache_index_invalidate(std::uint32_t address) {
        // DXIN (EE Core Instruction Set Manual v6.0 p301) selects the data-cache
        // index from vAddr[11:6] and the way from vAddr[0], then clears Lock,
        // Valid and Dirty while preserving LRF. Native RAM is coherent and this
        // runtime models every cache line as already invalid/clean/unlocked, so
        // the exact architectural transition leaves the modeled state unchanged.
        // This is an index operation: do not translate or access guest memory.
        (void)address;
    }
    void cache_writeback(std::uint32_t address) {
        // CACHE DHWBIN (EE instruction manual p299). Native RAM is coherent;
        // retain store ordering. The original RPC sender also uses its0x20000000
        // RAM alias here. Although the hardware manual leaves that case undefined,
        // the native compatibility policy orders the same coherent RAM stores.
        if((address>=0xa0000000u && address<0xc0000000u) ||
           (address>=0x30000000u && address<0x40000000u) || physical_address(address)>=ram.size())
            throw Fault(pc,"cache writeback requires implemented cached RAM");
        memory(address&~63u,1);
        std::atomic_thread_fence(std::memory_order_seq_cst);++boot.data_cache_epoch;
    }
    void sync(unsigned) {
        // All implemented guest operations complete synchronously. This is an
        // ordering barrier; future asynchronous DMA/device queues must drain here.
        std::atomic_thread_fence(std::memory_order_seq_cst);
    }
    void require_instruction(std::uint32_t address,std::uint32_t expected) {
        // Integrity check of an ahead-of-time compiled overlay, not instruction
        // decoding. Loading different code here must never execute a stale body.
        if(load(address,4,false)!=expected) throw Fault(address,"static overlay bytes are missing or changed");
    }
    void set_interrupts(bool enable) {
        if((cp0_status&0x20006) || !(cp0_status&0x18)) {
            if(enable) cp0_status|=0x10000; else cp0_status&=~0x10000u;
        }
    }
    void exception_return() {
        if(cp0_status&4) {pc=cp0_error_epc; cp0_status&=~4u;}
        else {pc=cp0_epc; cp0_status&=~2u;}
    }
};
// Defined by build-time generated source, not a runtime instruction decoder.
void run(State&, std::uint64_t block_budget);
void kernel_call(State&);
bool return_from_kernel(State&);
}
