#pragma once
#include "hg/memory_card.hpp"
#include "hg/controller.hpp"
#include "hg/iop_dmac.hpp"
#include "hg/sif_link.hpp"
#include "hg/spu2_input.hpp"
#include "hg/spu2_voices.hpp"
#include <array>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <stdexcept>
#include <string>
#include <memory>
#include <vector>

namespace hg {
struct IopFault:std::runtime_error {
    std::uint32_t pc;
    IopFault(std::uint32_t address,const std::string& message):std::runtime_error(message),pc(address){}
};
struct IopState {
    std::array<std::uint32_t,32> gpr{};
    std::uint32_t hi=0,lo=0,pc=0;
    // Native compatibility profile; processor identity independently observed
    // in the paused oracle register capture documented in docs/ORACLE.md.
    std::uint32_t processor_id=0x1f,cop0_status=0;
    std::uint32_t hardware_config=1,sif_bus_probe=0,spu2_mapping_0=0,spu2_mapping_1=0;
    std::uint32_t expansion_delay_1=0,spu2_delay=0;
    std::array<std::uint16_t,0x400> spu2_registers{};
    std::vector<std::uint8_t> spu2_ram=std::vector<std::uint8_t>(2*1024*1024);
    Spu2Voices spu2_voices; // Voice synthesis/mixing; owns ENVX/VOLX/NAX/LSAX/ENDX updates.
    std::uint64_t spu2_dma_count=0,spu2_dma_bytes=0;
    std::uint32_t last_spu2_dma_source=0,last_spu2_dma_destination=0;
    unsigned last_spu2_dma_core=0;
    Spu2Input spu2_input;
    std::array<bool,2> spu2_voice_transfer_complete{};
    struct Spu2InputDma {bool active=false;std::uint32_t source=0,total=0,remaining=0,block_words=0;};
    std::array<Spu2InputDma,2> spu2_input_dma{};
    struct Spu2InputFrame {unsigned core=0;std::uint16_t left=0,right=0;};
    // Bounded raw-input diagnostic history, explicitly separate from an audio
    // output device. Earlier frames are counted as truncated, never presented
    // as a complete audio capture or mixed SPU2 output.
    std::array<Spu2InputFrame,512> spu2_input_history{};
    std::uint64_t spu2_input_frames=0;
    static std::FILE* spu2_trace_file() {
        static std::FILE* const file=[]()->std::FILE* {
            const char* path=std::getenv("HG_SPU2_TRACE");
            return path&&*path?std::fopen(path,"w"):nullptr;
        }();
        return file;
    }
    static bool supported_spu2_register(std::uint32_t physical) {
        // The SPU2 exposes a halfword register file across both cores and its
        // shared digital block. DMA/sample transfer uses a separate checked
        // endpoint; retaining a register does not implement its audio effects.
        return physical>=0x1f900000 && physical<=0x1f9007ca && !(physical&1);
    }
    // A configured local DVD starts mounted, spun up, and paused: CDVD status
    // bits 1 and 3. This is hardware state, not a completed read command.
    std::uint8_t cdvd_interrupt_status=0,cdvd_error=0,cdvd_last_result=0,cdvd_last_s_command=0,cdvd_drive_status=0x0a,cdvd_sticky_status=0x0a;
    // The configured local input is a PS2 DVD. This is a mounted-media profile
    // for the CDVD disk-type register, not an assertion that read commands work.
    std::uint8_t cdvd_disk_type=0x14;
    // Original CDVDMAN reads only the low nibble of 0x1f402013 after DMA
    // completion. The synchronous native endpoint has no decoder work left,
    // so its explicit idle/completed compatibility state is zero.
    std::uint8_t cdvd_decoder_status=0;
    std::vector<std::uint8_t> cdvd_s_parameters,cdvd_s_results,cdvd_n_parameters;
    std::size_t cdvd_result_cursor=0;
    enum class CdvdTransferKind : std::uint8_t {none,read_dvd,toc};
    std::uint8_t cdvd_n_command=0;
    std::uint32_t cdvd_lba=0,cdvd_sectors=0;
    CdvdTransferKind cdvd_transfer_kind=CdvdTransferKind::none;
    bool cdvd_read_pending=false,cdvd_irq_pending=false;
    struct CdvdMmioTrace {std::uint32_t pc=0,address=0,value=0,count=0;std::uint8_t size=0;bool write=false;};
    std::array<CdvdMmioTrace,64> cdvd_mmio_trace{};
    std::size_t cdvd_mmio_trace_cursor=0;
    struct CdvdFileEntry {std::string path;std::uint32_t extent=0,size=0;};
    struct CdvdOpenFile {std::uint32_t descriptor=0,extent=0,size=0,position=0;bool active=false;};
    std::filesystem::path cdvd_image;
    std::vector<CdvdFileEntry> cdvd_files;
    std::array<CdvdOpenFile,16> cdvd_open_files{};
    struct StaticIopModulePlan {std::string path;std::uint32_t base=0,memory_size=0,allocation_prefix=0,file_size=0;bool image_buffer_seen=false,allocated=false;std::vector<std::uint8_t> buffer_image;};
    std::vector<StaticIopModulePlan> static_iop_module_plans;
    std::string pending_iop_module_path;
    struct SystemAllocation {std::uint32_t address=0,size=0;bool active=false,static_module=false;};
    std::vector<SystemAllocation> system_allocations;
    // Connected execution delegates ordinary storage to the original AOT
    // SYSMEM allocator, also used by SIF's IOP heap. Isolated fixtures retain
    // their bounded arenas. Static module allocations remain pre-reserved.
    std::function<std::uint32_t(IopState&,unsigned,std::uint32_t,std::uint32_t,std::uint32_t)> system_memory_service;
    std::uint32_t system_scratch_low=0x100000,system_scratch_high=0x17f000;
    void configure_buffered_iop_module(std::string identity,std::uint32_t base,std::uint32_t memory_size,std::uint32_t prefix,const std::vector<std::uint8_t>& image) {
        if(identity.empty() || image.empty() || image.size()>16*1024*1024 || !base || base%16 || !memory_size || prefix>base || prefix%16 || std::uint64_t(base)+memory_size>ram.size())
            throw std::runtime_error("invalid buffered IOP module plan");
        for(const auto& p:static_iop_module_plans)if(p.path=="@buffer/"+identity ||
            (base-prefix<std::uint64_t(p.base)+p.memory_size && p.base-p.allocation_prefix<std::uint64_t(base)+memory_size))
            throw std::runtime_error("overlapping buffered IOP module plan");
        static_iop_module_plans.push_back({"@buffer/"+identity,base,memory_size,prefix,std::uint32_t(image.size()),true,false,image});
    }
    void prepare_buffered_module(std::uint32_t buffer,std::uint32_t address,std::uint32_t offset) {
        if(address || offset)throw IopFault(pc,"buffered IOP explicit placement is not implemented");
        auto found=static_iop_module_plans.end();
        for(auto p=static_iop_module_plans.begin();p!=static_iop_module_plans.end();++p) {
            if(p->buffer_image.empty() || std::uint64_t(buffer)+p->buffer_image.size()>ram.size())continue;
            if(std::equal(p->buffer_image.begin(),p->buffer_image.end(),ram.begin()+buffer)) {
                if(found!=static_iop_module_plans.end())throw IopFault(pc,"ambiguous buffered IOP identity");
                found=p;
            }
        }
        if(found==static_iop_module_plans.end())throw IopFault(pc,"buffered IOP image has no verified static translation");
        if(found->allocated)throw IopFault(pc,"buffered IOP module is already allocated");
        pending_iop_module_path=found->path;found->image_buffer_seen=true;
    }
    void configure_static_iop_module(std::string path,std::uint32_t base,std::uint32_t memory_size,std::uint32_t allocation_prefix) {
        path=normalize_cdvd_path(std::move(path));
        const auto file=std::find_if(cdvd_files.begin(),cdvd_files.end(),[&](const auto& item){return item.path==path;});
        if(file==cdvd_files.end() || !base || base%16 || !memory_size || allocation_prefix>base || allocation_prefix%16 || std::uint64_t(base)+memory_size>ram.size())
            throw std::runtime_error("invalid static IOP module plan");
        for(const auto& p:static_iop_module_plans)if(p.path==path ||
            (base-allocation_prefix<std::uint64_t(p.base)+p.memory_size && p.base-p.allocation_prefix<std::uint64_t(base)+memory_size))
            throw std::runtime_error("overlapping static IOP module plan");
        static_iop_module_plans.push_back({path,base,memory_size,allocation_prefix,file->size});
    }
    static std::string normalize_cdvd_path(std::string path) {
        std::string result;result.reserve(path.size());
        for(char c:path) {
            if(c=='\\')c='/';
            if(c>='a' && c<='z')c=char(c-'a'+'A');
            if(c=='/' && (result.empty() || result.back()=='/'))continue;
            result.push_back(c);
        }
        while(!result.empty() && result.front()=='/')result.erase(result.begin());
        while(!result.empty() && result.back()=='/')result.pop_back();
        const auto separator=result.find_last_of('/');
        const auto version=result.find(';',separator==std::string::npos?0:separator+1);
        if(version!=std::string::npos)result.resize(version);
        return result;
    }
    void configure_cdvd_image(const std::filesystem::path& image) {
        constexpr std::uint64_t block=2048;
        std::ifstream input(image,std::ios::binary);input.seekg(0,std::ios::end);
        const auto end=input?input.tellg():std::streampos(-1);
        if(end<std::streampos(17*block))throw std::runtime_error("configured CDVD image is too small");
        const auto image_size=std::uint64_t(end);cdvd_files.clear();cdvd_open_files={};
        auto bytes=[&](std::uint64_t offset,std::size_t size) {
            if(offset+size>image_size)throw std::runtime_error("CDVD volume extent outside image");
            std::vector<std::uint8_t> result(size);input.clear();input.seekg(std::streamoff(offset));
            input.read(reinterpret_cast<char*>(result.data()),std::streamsize(size));
            if(!input)throw std::runtime_error("short read from configured CDVD image");
            return result;
        };
        auto le16=[](const std::uint8_t* p){return std::uint16_t(p[0])|(std::uint16_t(p[1])<<8);};
        auto le32=[](const std::uint8_t* p){return std::uint32_t(p[0])|(std::uint32_t(p[1])<<8)|(std::uint32_t(p[2])<<16)|(std::uint32_t(p[3])<<24);};
        auto be16=[](const std::uint8_t* p){return std::uint16_t(p[1])|(std::uint16_t(p[0])<<8);};
        auto be32=[](const std::uint8_t* p){return std::uint32_t(p[3])|(std::uint32_t(p[2])<<8)|(std::uint32_t(p[1])<<16)|(std::uint32_t(p[0])<<24);};
        std::vector<std::uint8_t> primary;
        for(std::uint32_t sector=16;sector<80;++sector) {
            auto descriptor=bytes(std::uint64_t(sector)*block,block);
            if(std::string(reinterpret_cast<const char*>(descriptor.data()+1),5)!="CD001" || descriptor[6]!=1)
                throw std::runtime_error("invalid ECMA-119 volume descriptor");
            if(descriptor[0]==1 && primary.empty())primary=descriptor;
            if(descriptor[0]==255)break;
        }
        if(primary.empty())throw std::runtime_error("CDVD primary volume descriptor missing");
        if(le16(primary.data()+128)!=block || be16(primary.data()+130)!=block)
            throw std::runtime_error("unsupported CDVD logical block size");
        const auto blocks=le32(primary.data()+80);
        if(be32(primary.data()+84)!=blocks || std::uint64_t(blocks)*block>image_size)
            throw std::runtime_error("invalid CDVD volume bounds");
        struct Directory {std::string path;std::uint32_t extent,size;};
        const auto* root=primary.data()+156;
        if(root[0]<34)throw std::runtime_error("invalid CDVD root directory record");
        std::vector<Directory> pending{{"",le32(root+2),le32(root+10)}};
        for(std::size_t cursor=0;cursor<pending.size();++cursor) {
            if(pending.size()>4096 || cdvd_files.size()>200000)throw std::runtime_error("CDVD volume index limit exceeded");
            const auto directory=pending[cursor];
            if(std::uint64_t(directory.extent)*block+directory.size>image_size)throw std::runtime_error("CDVD directory outside image");
            const auto data=bytes(std::uint64_t(directory.extent)*block,directory.size);
            for(std::size_t offset=0;offset<data.size();) {
                const auto length=data[offset];
                if(!length) {offset=((offset/block)+1)*block;continue;}
                if(length<34 || offset+length>data.size())throw std::runtime_error("invalid CDVD directory record");
                const auto* record=data.data()+offset;const auto name_length=record[32];
                if(33u+name_length>length)throw std::runtime_error("invalid CDVD directory name");
                const auto extent=le32(record+2),size=le32(record+10);
                if(be32(record+6)!=extent || be32(record+14)!=size || std::uint64_t(extent)*block+size>image_size)
                    throw std::runtime_error("invalid CDVD file extent");
                if(!(name_length==1 && (record[33]==0 || record[33]==1))) {
                    std::string name(reinterpret_cast<const char*>(record+33),name_length);
                    const auto path=normalize_cdvd_path(directory.path.empty()?name:directory.path+'/'+name);
                    if(record[25]&2)pending.push_back({path,extent,size});
                    else cdvd_files.push_back({path,extent,size});
                }
                offset+=length;
            }
        }
        cdvd_image=std::filesystem::absolute(image).lexically_normal();
    }
    std::string guest_cdvd_path(std::uint32_t address) const {
        if(address>=ram.size())throw IopFault(pc,"CDVD path outside IOP RAM");
        std::string result;
        for(unsigned n=0;n<256;++n) {
            if(std::uint64_t(address)+n>=ram.size())throw IopFault(pc,"unterminated CDVD path");
            const char c=char(ram[address+n]);if(!c)return normalize_cdvd_path(result);result.push_back(c);
        }
        throw IopFault(pc,"CDVD path exceeds native limit");
    }
    std::uint32_t cdvd_open(std::uint32_t descriptor,std::uint32_t name,std::uint32_t flags,std::uint32_t mode) {
        if(cdvd_image.empty())throw IopFault(pc,"CDVD image is not configured");
        if(descriptor%4 || std::uint64_t(descriptor)+16>ram.size())throw IopFault(pc,"invalid IOMAN CDVD descriptor");
        // IOMAN's variadic mode argument is unspecified for the observed
        // read-only two-argument open call.  It is meaningful only for a
        // create operation, which this endpoint rejects with every non-1 flag.
        if(flags!=1)throw IopFault(pc,"unsupported writable CDVD open");
        (void)mode;
        const auto path=guest_cdvd_path(name);
        const auto found=std::find_if(cdvd_files.begin(),cdvd_files.end(),[&](const auto& file){return file.path==path;});
        if(found==cdvd_files.end())return 0xfffffffeu;
        auto slot=std::find_if(cdvd_open_files.begin(),cdvd_open_files.end(),[](const auto& file){return !file.active;});
        if(slot==cdvd_open_files.end())return 0xffffffe8u;
        *slot={descriptor,found->extent,found->size,0,true};pending_iop_module_path=path;return 0;
    }
    CdvdOpenFile& cdvd_file(std::uint32_t descriptor) {
        auto found=std::find_if(cdvd_open_files.begin(),cdvd_open_files.end(),[&](const auto& file){return file.active && file.descriptor==descriptor;});
        if(found==cdvd_open_files.end())throw IopFault(pc,"unknown native CDVD file descriptor");
        return *found;
    }
    std::uint32_t cdvd_close(std::uint32_t descriptor) {cdvd_file(descriptor)={};return 0;}
    std::uint32_t cdvd_read(std::uint32_t descriptor,std::uint32_t destination,std::uint32_t count) {
        auto& file=cdvd_file(descriptor);const auto amount=std::min(count,file.size-file.position);
        if(std::uint64_t(destination)+amount>ram.size())throw IopFault(pc,"CDVD read destination outside IOP RAM");
        std::ifstream input(cdvd_image,std::ios::binary);input.seekg(std::uint64_t(file.extent)*2048+file.position);
        input.read(reinterpret_cast<char*>(ram.data()+destination),amount);
        if(!input)throw IopFault(pc,"short native CDVD file read");
        file.position+=amount;return amount;
    }
    std::uint32_t cdvd_lseek(std::uint32_t descriptor,std::uint32_t offset,std::uint32_t whence) {
        auto& file=cdvd_file(descriptor);std::int64_t position=whence==0?0:whence==1?file.position:whence==2?file.size:-1;
        if(position<0)throw IopFault(pc,"unsupported CDVD seek origin");
        position+=std::int32_t(offset);
        if(position<0 || std::uint64_t(position)>file.size)return 0xffffffedu;
        file.position=std::uint32_t(position);return file.position;
    }
    std::uint32_t alloc_system_memory(std::uint32_t mode,std::uint32_t size,std::uint32_t address) {
        if(!size)return 0;
        const auto aligned=(size+15u)&~15u;if(aligned<size)return 0;
        auto plan=std::find_if(static_iop_module_plans.begin(),static_iop_module_plans.end(),[&](const auto& item){return item.path==pending_iop_module_path;});
        if(plan!=static_iop_module_plans.end() && plan->image_buffer_seen && !plan->allocated && aligned==((plan->memory_size+plan->allocation_prefix+15u)&~15u)) {
            if(mode>1 || address)throw IopFault(pc,"static IOP module requested unsupported allocation mode");
            const auto allocation_base=plan->base-plan->allocation_prefix;
            plan->allocated=true;system_allocations.push_back({allocation_base,aligned,true,true});return allocation_base;
        }
        if(mode>1 || address)throw IopFault(pc,"unsupported native system-memory allocation mode");
        if(plan!=static_iop_module_plans.end() && !plan->image_buffer_seen && size==plan->file_size)plan->image_buffer_seen=true;
        return allocate_system_storage(mode,aligned);
    }
    std::uint32_t allocate_system_storage(std::uint32_t mode,std::uint32_t aligned) {
        if(mode>1 || !aligned || aligned%16)throw IopFault(pc,"invalid native storage allocation");
        std::uint32_t result=0;
        if(system_memory_service) {
            result=system_memory_service(*this,4,mode,aligned,0);
            if(!result)return 0;
            if(result%16 || std::uint64_t(result)+aligned>ram.size())
                throw IopFault(pc,"original SYSMEM returned an invalid allocation");
        } else if(mode==0) {
            result=system_scratch_low;
            if(std::uint64_t(result)+aligned>system_scratch_high)return 0;
            system_scratch_low+=aligned;
        } else {
            if(system_scratch_high<system_scratch_low+aligned)return 0;
            system_scratch_high-=aligned;result=system_scratch_high;
        }
        system_allocations.push_back({result,aligned,true,false});return result;
    }
    std::uint32_t free_system_memory(std::uint32_t address) {
        auto allocation=std::find_if(system_allocations.begin(),system_allocations.end(),[&](const auto& item){return item.active && item.address==address;});
        if(allocation==system_allocations.end())return 0xffffff55u;
        if(system_memory_service && !allocation->static_module) {
            const auto result=system_memory_service(*this,5,address,0,0);
            if(result)return result;
        }
        allocation->active=false;
        if(allocation->static_module)for(auto& plan:static_iop_module_plans)if(plan.base-plan.allocation_prefix==address) {
            plan.allocated=false;plan.image_buffer_seen=!plan.buffer_image.empty();
        }
        return 0;
    }
    std::uint32_t link_static_iop_module(std::uint32_t address,std::uint32_t size) {
        auto plan=std::find_if(static_iop_module_plans.begin(),static_iop_module_plans.end(),[&](const auto& item) {
            return item.path==pending_iop_module_path && item.allocated;
        });
        if(plan==static_iop_module_plans.end()) {
            std::string detail="LOADCORE link requested for an unplanned IOP module path="+pending_iop_module_path;
            for(const auto& item:static_iop_module_plans)
                detail+=" plan="+item.path+"/"+std::to_string(item.base)+"/"+std::to_string(item.memory_size)+"/"+(item.image_buffer_seen?"image":"no-image")+"/"+(item.allocated?"allocated":"not-allocated");
            for(const auto& item:system_allocations)
                detail+=" alloc="+std::to_string(item.address)+"/"+std::to_string(item.size)+"/"+(item.active?"active":"freed")+"/"+(item.static_module?"static":"scratch");
            throw IopFault(pc,detail);
        }
        if(address<plan->base || !size || std::uint64_t(address)+size>std::uint64_t(plan->base)+plan->memory_size)
            throw IopFault(pc,"LOADCORE link range is outside the static IOP module plan");
        return 0;
    }
    void trace_cdvd_mmio(std::uint32_t address,unsigned size,std::uint32_t value,bool write) {
        if(cdvd_mmio_trace_cursor) {
            auto& previous=cdvd_mmio_trace[(cdvd_mmio_trace_cursor-1)%cdvd_mmio_trace.size()];
            if(previous.pc==pc && previous.address==address && previous.value==value && previous.size==size && previous.write==write) {
                ++previous.count;return;
            }
        }
        cdvd_mmio_trace[cdvd_mmio_trace_cursor++%cdvd_mmio_trace.size()]={pc,address,value,1,static_cast<std::uint8_t>(size),write};
    }
    std::array<std::uint8_t,8> rtc_bcd{};
    bool rtc_configured=false;
    // Explicit native compatibility profile 1.0.0; not an emulator's device identity.
    std::array<std::uint8_t,4> cdvd_controller_version{0,1,0,0};
    void submit_cdvd_s_command(std::uint8_t command) {
        if(cdvd_result_cursor<cdvd_s_results.size())throw IopFault(pc,"CDVD command submitted with unread results");
        if(command==3) {
            if(cdvd_s_parameters!=std::vector<std::uint8_t>{0})throw IopFault(pc,"unsupported CDVD controller subcommand");
            cdvd_s_results.assign(cdvd_controller_version.begin(),cdvd_controller_version.end());
            cdvd_result_cursor=0;cdvd_last_s_command=command;cdvd_s_parameters.clear();return;
        }
        if(command==5) {
            if(!cdvd_s_parameters.empty())throw IopFault(pc,"UpdateStickyFlags expects no parameters");
            cdvd_sticky_status=cdvd_drive_status;cdvd_s_results={0};cdvd_result_cursor=0;cdvd_last_s_command=command;return;
        }
        if(command!=8)throw IopFault(pc,"CDVD S-command requires implementation: "+std::to_string(command));
        if(!rtc_configured || !cdvd_s_parameters.empty())throw IopFault(pc,"ReadRTC needs configured clock and zero parameters");
        cdvd_last_s_command=command;
        cdvd_s_results.assign(rtc_bcd.begin(),rtc_bcd.end());cdvd_result_cursor=0;
        cdvd_s_parameters.clear();
    }
    IopDmac dmac;
    std::array<std::uint32_t,33> sio2_registers{};
    std::vector<std::uint8_t> sio2_input_fifo,sio2_output_fifo;
    std::size_t sio2_output_cursor=0;
    std::uint32_t sio2_last_control_write=0; // Diagnostic request provenance.
    // Virtual DualShock2 pads start in digital mode; host inputs are active-low.
    std::array<Controller,2> sio2_controllers{};
    std::array<std::uint16_t,2> sio2_buttons{{0xffff,0xffff}};
    std::array<bool,2> sio2_connected{{true,false}}; // Native one-controller launch profile.
    bool sio2_irq_pending=false;
    // Optional virtual memory card in slot 1 (SIO2 port 2); null/absent keeps
    // the original disconnected-card behaviour byte for byte.
    std::shared_ptr<VirtualMemoryCard> memory_card;
    bool memory_card_present() const noexcept {return memory_card&&memory_card->present;}
    // Native SECRMAN service: the BIOS module is not available, so a present
    // virtual card authenticates (no MagicGate crypto is modelled); absent fails.
    std::uint32_t authenticate_card(std::uint32_t port,std::uint32_t slot,std::uint32_t) const noexcept {
        return memory_card_present()&&slot==0&&(port==0||port==2)?1u:0u;
    }
    void complete_sio2_card_dma();
    void complete_sio2_card_probe();
    void complete_sio2_poll();
    bool sio2_contains(std::uint32_t address) const {return address>=0x1f808200 && address<=0x1f808280;}
    std::uint32_t load_sio2(std::uint32_t address,unsigned size);
    void store_sio2(std::uint32_t address,unsigned size,std::uint32_t value);
    std::uint32_t last_sif0_arm_pc=0,last_sif0_arm_return_pc=0;
    std::shared_ptr<SifLink> sif=std::make_shared<SifLink>();
    std::uint32_t dma_register_address(std::uint32_t channel,unsigned offset) const {
        const auto base=channel==2?0x1f8010a0u:channel==3?0x1f8010b0u:channel==9?0x1f801520u:channel==10?0x1f801530u:channel==11?0x1f801540u:channel==12?0x1f801550u:0u;
        if(!base)throw IopFault(pc,"unimplemented DMACMAN channel");
        return base+offset;
    }
    void set_dma_priority(std::uint32_t channel,std::uint32_t priority) {
        try {dmac.set_channel_priority(channel,priority);}
        catch(const std::exception& e) {throw IopFault(pc,e.what());}
    }
    void set_dma_channel_enabled(std::uint32_t channel,bool enabled) {
        try {dmac.set_channel_enabled(channel,enabled);}
        catch(const std::exception& e) {throw IopFault(pc,e.what());}
    }
    std::uint32_t set_slice_dma(std::uint32_t channel,std::uint32_t address,std::uint32_t size,std::uint32_t count,std::uint32_t direction) {
        if(!size || size>0xffff || !count || count>0xffff || direction>1)return 0;
        store(dma_register_address(channel,0),4,address);
        store(dma_register_address(channel,4),4,size|(count<<16));
        // Original BIOS DMACMAN +0xd30..+0xd48 configures request mode;
        // +0xe84 (StartDMA) separately sets the start bit.
        const auto control=0x200u|direction|(direction?0u:0x800000u);
        store(dma_register_address(channel,8),4,control);
        return 1;
    }
    void start_dma(std::uint32_t channel) {
        const auto address=dma_register_address(channel,8);
        store(address,4,load(address,4,false)|0x01000000u);
    }
    bool pump_sif0() {
        try {
            const auto pumped=dmac.pump_sif0([&](std::uint32_t address) {
                if(std::uint64_t(address)+4>ram.size())throw IopFault(pc,"IOP DMA source outside RAM");
                return load(address,4,false);
            },[&](std::uint64_t low,std::uint64_t high){return sif->push_main({low,high});});
            if(pumped && dmac.last_sif0_payload_words && dmac.last_sif0_payload[0]==0xffffff35u) {
                last_sif0_error_trace=pc_trace;last_sif0_error_trace_cursor=pc_trace_cursor;
            }
            return pumped;
        }catch(const std::exception& e){throw IopFault(pc,e.what());}
    }
    bool interrupts_enabled=false;
    // Diagnostic provenance for native CPU-interrupt lifecycle boundaries.
    std::uint32_t last_interrupt_enable_pc=0,last_interrupt_disable_pc=0,last_interrupt_suspend_pc=0,last_interrupt_resume_pc=0,last_interrupt_resume_token=0;
    bool in_interrupt=false;
    struct CpuContext {
        std::array<std::uint32_t,32> gpr{};
        std::uint32_t pc=0,hi=0,lo=0,pending_value=0,next_value=0;
        unsigned pending_reg=0,next_reg=0;
        bool interrupts_enabled=false;
    };
    struct Thread {std::uint32_t entry,stack_size,priority,attr,option,argument=0;bool ready=false;
        std::uint64_t wake_deadline=0;bool delayed=false;
        bool initialized=false,sleeping=false;std::uint32_t wakeups=0,wait_semaphore=0,granted_semaphore=0,stack_base=0,wait_event=0,wait_event_bits=0,wait_event_mode=0;CpuContext context;
        bool live=true;
        bool waiting_vblank_start=false,vblank_start_granted=false;
        std::uint32_t wait_event_result=0,granted_event=0;
    };
    std::uint32_t current_thread=0,locked_thread=0,bootstrap_thread_priority=0,thread_stack_top=0x17f000;
    struct ThreadStackRange {std::uint32_t address,size;};
    std::vector<ThreadStackRange> free_thread_stacks;
    std::size_t thread_cursor=0;
    // THREADMAN owner: the last dispatched worker keeps the CPU against equal-priority
    // ready workers until it blocks, yields (reschedule) or exits; 0 = no owner.
    std::uint32_t owner_thread=0;
    // Host-only provenance for THREADMAN's BIOS selector-32 context-transfer
    // boundary. Guest thread contexts are owned by the native scheduler.
    std::uint32_t last_thread_reschedule_pc=0,last_thread_reschedule_mode=0;
    std::uint64_t thread_reschedule_count=0;
    std::uint64_t virtual_time_us=0;
    struct Timer {std::uint32_t count=0,mode=0,target=0;std::uint64_t clock_phase=0;};
    std::array<Timer,6> timers{};
    std::uint8_t timer_irq_pending=0;
    static int timer_register(std::uint32_t address) {
        for(unsigned timer=0;timer<6;++timer) {
            const auto base=timer<3?0x1f801100u+timer*0x10u:0x1f801480u+(timer-3)*0x10u;
            if(address>=base && address<=base+8 && (address-base)%4==0)return int(timer*3+(address-base)/4);
        }
        return -1;
    }
    void submit_cdvd_n_command(std::uint8_t command) {
        if(command==9 && cdvd_n_parameters==std::vector<std::uint8_t>{0}) {
            cdvd_n_command=command;cdvd_lba=0;cdvd_sectors=1;cdvd_transfer_kind=CdvdTransferKind::toc;cdvd_read_pending=true;
            cdvd_drive_status=0x06;cdvd_sticky_status|=cdvd_drive_status;cdvd_n_parameters.clear();return;
        }
        // Original CDVDMAN at 0xba9e8..0xbaa14 builds all eleven bytes:
        // LBA, count, retry count, converted spindle mode, and zero.
        // Startup uses retry16 for ISO discovery and retry0 for archive reads.
        // The original program selects either x2 CLV (2) or x4 CAV (0x83),
        // depending on the DVD directory profile. Both use the same successful
        // local-image transfer; physical seek/speed and error retries remain unsupported.
        if(command!=8 || cdvd_n_parameters.size()!=11 ||
           (cdvd_n_parameters[8]!=16 && cdvd_n_parameters[8]!=0) ||
           (cdvd_n_parameters[9]!=2 && cdvd_n_parameters[9]!=0x83) || cdvd_n_parameters[10]!=0) {
            std::string detail="unsupported CDVD N-command "+std::to_string(command)+" with "+std::to_string(cdvd_n_parameters.size())+" parameters";
            for(const auto value:cdvd_n_parameters)detail+=" "+std::to_string(value);
            throw IopFault(pc,detail);
        }
        auto word=[&](unsigned offset) {return std::uint32_t(cdvd_n_parameters[offset])|(std::uint32_t(cdvd_n_parameters[offset+1])<<8)|
            (std::uint32_t(cdvd_n_parameters[offset+2])<<16)|(std::uint32_t(cdvd_n_parameters[offset+3])<<24);};
        const auto lba=word(0),sectors=word(4);
        if(!sectors || sectors>0x10000)throw IopFault(pc,"unsupported CDVD ReadDvd sector count");
        cdvd_n_command=command;cdvd_lba=lba;cdvd_sectors=sectors;cdvd_transfer_kind=CdvdTransferKind::read_dvd;cdvd_read_pending=true;
        cdvd_drive_status=0x06;cdvd_sticky_status|=cdvd_drive_status;cdvd_n_parameters.clear();
    }
    struct CdvdDmaRequest {std::uint32_t lba,destination;CdvdTransferKind kind;};
    bool cdvd_dma_ready() const {
        return cdvd_read_pending && (dmac.channel[3][2]&0x01000000)!=0;
    }
    CdvdDmaRequest cdvd_dma_request() const {
        if(!cdvd_dma_ready())throw IopFault(pc,"CDVD DMA request without active read");
        const auto& channel=dmac.channel[3];
        // GetToc was independently observed with the same one-record channel-3
        // shape as ReadDvd.  Do not let an arbitrary active channel become an
        // implicit native device transfer while its payload remains unsupported.
        if(cdvd_transfer_kind==CdvdTransferKind::toc) {
            if(channel[2]!=0x41000200 || channel[1]!=0x00810004 || channel[0]%4 || std::uint64_t(channel[0])+2064>ram.size())
                throw IopFault(pc,"unsupported active CDVD GetToc DMA descriptor");
            return {cdvd_lba,channel[0],cdvd_transfer_kind};
        }
        if(cdvd_transfer_kind!=CdvdTransferKind::read_dvd)throw IopFault(pc,"unknown active CDVD transfer kind");
        const auto block_words=channel[1]&0xffffu;
        const auto words=std::uint64_t(block_words)*(channel[1]>>16);
        // Original CDVDMAN builds 43 blocks of 12 words per DVD record.
        // Accept complete records while retaining a descriptor across sectors.
        if(channel[2]!=0x41000200 || !block_words || 516%block_words || !words || words%516 ||
           words/516>cdvd_sectors || channel[0]%4 || std::uint64_t(channel[0])+words*4>ram.size())
            throw IopFault(pc,"unsupported active CDVD DMA descriptor: MADR="+std::to_string(channel[0])+
                           " BCR="+std::to_string(channel[1])+" CHCR="+std::to_string(channel[2]));
        return {cdvd_lba,channel[0],cdvd_transfer_kind};
    }
    void complete_cdvd_dma_record(const std::array<std::uint8_t,2064>& record,CdvdTransferKind expected_kind) {
        const auto request=cdvd_dma_request();
        if(request.kind!=expected_kind)throw IopFault(pc,"CDVD completion transfer kind mismatch");
        if(!cdvd_sectors)throw IopFault(pc,"CDVD sector count underflow");
        auto& channel=dmac.channel[3];
        const auto block_words=channel[1]&0xffffu;
        const auto remaining_blocks=(channel[1]>>16)-516/block_words;
        std::copy(record.begin(),record.end(),ram.begin()+request.destination);
        channel[0]+=2064;channel[1]=(remaining_blocks<<16)|block_words;
        ++cdvd_lba;--cdvd_sectors;cdvd_read_pending=cdvd_sectors!=0;
        if(!cdvd_read_pending)cdvd_transfer_kind=CdvdTransferKind::none;
        // Sync-mode1 decrements the block count; STR and completion stay pending
        // until the whole descriptor has transferred, not each 2064-byte record.
        if(remaining_blocks)return;
        channel[2]&=~0x01000000u;
        // Disabled DMA sources do not latch completion for a later transfer.
        if(interrupt_mask&(std::uint64_t(1)<<35))dmac.completed_channels|=1u<<3;
        // A DMA buffer may cover only part of the N-command. Its channel IRQ
        // lets the original worker consume/rearm that buffer. A drive IRQ here
        // would enter CDVDMAN b5b18 and disable IRQ35 before remaining sectors.
        if(cdvd_read_pending)return;
        cdvd_drive_status=0x02;cdvd_sticky_status|=cdvd_drive_status;
        // This checked completion supplies a successful transfer. Publish its
        // result before the IRQ, rather than retaining an earlier error byte.
        cdvd_error=0;
        cdvd_interrupt_status=3;cdvd_irq_pending=true;
    }
    void complete_cdvd_dma(const std::array<std::uint8_t,2064>& record) {
        complete_cdvd_dma_record(record,CdvdTransferKind::read_dvd);
    }
    void complete_cdvd_toc_dma(const std::array<std::uint8_t,2064>& record) {
        // The payload remains opaque here.  The caller must have acquired this
        // exact CDVD DMA record independently; this method only commits the
        // already-validated transfer and its documented completion state.
        complete_cdvd_dma_record(record,CdvdTransferKind::toc);
    }
    std::uint32_t load_timer(std::uint32_t address,unsigned size) {
        const int encoded=timer_register(address);if(encoded<0)throw IopFault(pc,"invalid IOP timer register");
        const unsigned timer=unsigned(encoded)/3,field=unsigned(encoded)%3;
        if(size!=2 && size!=4)throw IopFault(pc,"unsupported IOP timer register width");
        auto& item=timers[timer];std::uint32_t value=field==0?item.count:field==1?item.mode:item.target;
        if(field==1)item.mode&=~0x1800u; // Reading mode acknowledges compare/overflow flags.
        if(timer<3 || field==1)value&=0xffff;
        return value;
    }
    void store_timer(std::uint32_t address,unsigned size,std::uint32_t value) {
        const int encoded=timer_register(address);if(encoded<0)throw IopFault(pc,"invalid IOP timer register");
        const unsigned timer=unsigned(encoded)/3,field=unsigned(encoded)%3;
        if(size!=2 && size!=4)throw IopFault(pc,"unsupported IOP timer register width");
        if(size==2)value&=0xffff;
        auto& item=timers[timer];auto merge=[&](std::uint32_t old) {return size==2?(old&0xffff0000u)|value:value;};
        if(field==0)item.count=merge(item.count);
        else if(field==2)item.target=merge(item.target);
        else {item.count=0;item.clock_phase=0;item.mode=(value&0x61ffu)|0x0400u;}
        if(timer<3) {item.count&=0xffff;item.target&=0xffff;}
        // HG-DIAG-122: optional host-only trace of timer configuration writes.
        static const bool trace=[]{const char* e=std::getenv("HG_IOP_TIMER_TRACE");return e&&*e=='1';}();
        if(trace && field)std::fprintf(stderr,"HG-DIAG-122 timer%u %s=0x%x mode=0x%x target=0x%x pc=0x%x t=%lluus\n",timer,
            field==1?"mode":"target",unsigned(value),unsigned(item.mode),unsigned(item.target),unsigned(pc),
            static_cast<unsigned long long>(virtual_time_us));
    }
    void advance_timers(std::uint32_t microseconds) {
        constexpr std::uint64_t iop_clock=36864000,per_second=1000000;
        for(unsigned index=0;index<timers.size();++index) {
            auto& item=timers[index];if(!(item.mode&(0x10u|0x20u)))continue;
            const unsigned prescale=index>=4?std::array<unsigned,4>{1,8,16,256}[(item.mode>>13)&3]:index==2 && (item.mode&0x200)?8:1;
            const auto period=per_second*prescale;
            item.clock_phase+=std::uint64_t(microseconds)*iop_clock;
            const auto ticks=item.clock_phase/period;item.clock_phase%=period;
            if(!ticks)continue;
            const std::uint64_t width=index<3?0x10000ull:0x100000000ull;
            const std::uint64_t old=item.count,next=old+ticks;
            bool fire=false;
            if((item.mode&0x08) && item.target && old<=item.target) {
                // Mode bit 3: the counter resets to 0 after reaching the target
                // (psx-spx root counters; IOP timers share the layout), so a
                // repeating target IRQ fires every target+1 ticks.
                const std::uint64_t target=item.target,period=target+1;
                if(old<target?next>=target:next>=target+period) {item.mode|=0x800;if(item.mode&0x10)fire=true;}
                item.count=std::uint32_t(next<=target?next:(next-target-1)%period);
            } else {
                if((item.mode&0x10) && old<item.target && next>=item.target) {item.mode|=0x800;fire=true;}
                if((item.mode&0x20) && next>=width) {item.mode|=0x1000;fire=true;}
                item.count=std::uint32_t(next%width);
            }
            if(fire) {timer_irq_pending|=std::uint8_t(1u<<index);if(!(item.mode&0x40))item.mode&=~0x400u;}
        }
    }
    CpuContext save_cpu() const {return {gpr,pc,hi,lo,pending_value,next_value,pending_reg,next_reg,interrupts_enabled};}
    void restore_cpu(const CpuContext& c) {
        gpr=c.gpr;pc=c.pc;hi=c.hi;lo=c.lo;pending_value=c.pending_value;next_value=c.next_value;
        pending_reg=c.pending_reg;next_reg=c.next_reg;interrupts_enabled=c.interrupts_enabled;
    }
    std::vector<Thread> threads;
    // Guest-RAM-backed HEAPLIB arenas.  Metadata is host-side so allocator
    // bookkeeping cannot collide with original module data; every returned
    // address is nevertheless a checked IOP RAM address.
    struct HeapAllocation {std::uint32_t address,size;bool free;};
    struct Heap {std::uint32_t address,size;bool live;std::vector<HeapAllocation> allocations;};
    std::vector<Heap> heaps;
    std::uint32_t heap_arena_next=0x180000;
    static constexpr std::uint32_t heap_arena_end=0x1ef000;
    Heap& heap(std::uint32_t address) {
        for(auto& item:heaps)if(item.live && item.address==address)return item;
        throw IopFault(pc,"invalid native HEAPLIB handle");
    }
    std::uint32_t create_heap(std::uint32_t size,std::uint32_t flag) {
        if(!size || flag!=1 || size%16 || size>ram.size() ||
           (!system_memory_service && (size>heap_arena_end || heap_arena_next>heap_arena_end-size)))
            throw IopFault(pc,"unsupported/exhausted native HEAPLIB CreateHeap profile");
        const auto address=system_memory_service?allocate_system_storage(0,size):heap_arena_next;
        if(!address)throw IopFault(pc,"original SYSMEM exhausted for HEAPLIB arena");
        if(!system_memory_service)heap_arena_next+=size;
        heaps.push_back({address,size,true,{{address+16,size-16,true}}});
        return address;
    }
    void delete_heap(std::uint32_t address) {
        auto& current=heap(address);
        if(system_memory_service && free_system_memory(address))throw IopFault(pc,"original SYSMEM rejected HEAPLIB release");
        current.live=false;
    }
    std::uint32_t alloc_heap_memory(std::uint32_t address,std::uint32_t size) {
        if(!size || size>0x7ffffffcu)throw IopFault(pc,"invalid native HEAPLIB allocation size");
        auto& current=heap(address);const auto aligned=(size+3)&~3u;
        for(auto& block:current.allocations)if(block.free && block.size>=aligned) {
            const auto result=block.address,remaining=block.size-aligned;
            block.size=aligned;block.free=false;
            if(remaining)current.allocations.push_back({result+aligned,remaining,true});
            return result;
        }
        return 0;
    }
    std::uint32_t free_heap_memory(std::uint32_t address,std::uint32_t allocation) {
        auto& current=heap(address);
        for(auto& block:current.allocations)if(!block.free && block.address==allocation) {
            block.free=true;return 0;
        }
        return std::uint32_t(-1);
    }
    std::uint32_t heap_total_free_size(std::uint32_t address) {
        std::uint32_t total=0;for(const auto& block:heap(address).allocations)if(block.free)total+=block.size;
        return total;
    }
    std::uint32_t create_thread(std::uint32_t descriptor) {
        if(in_interrupt || descriptor%4 || std::uint64_t(descriptor)+20>ram.size())
            throw IopFault(pc,"invalid IOP CreateThread descriptor/context");
        const auto attr=load(descriptor,4,false),option=load(descriptor+4,4,false);
        const auto entry=load(descriptor+8,4,false),size=load(descriptor+12,4,false),priority=load(descriptor+16,4,false);
        if(attr!=0x02000000 || !entry || entry%4 || entry>=ram.size() || size<0x130 || size%16 || size>0x10000 || !priority || priority>126)
            throw IopFault(pc,"unsupported native IOP thread profile");
        auto slot=threads.size();for(std::size_t index=0;index<threads.size();++index)if(!threads[index].live){slot=index;break;}
        if(slot==threads.size() && threads.size()>=64)throw IopFault(pc,"exhausted native IOP thread table");
        Thread thread{entry,size,priority,attr,option};thread.stack_base=allocate_thread_stack(size);
        if(slot<threads.size())threads[slot]=thread;else threads.push_back(thread);
        return std::uint32_t(slot+1);
    }
    std::uint32_t allocate_thread_stack(std::uint32_t size) {
        const auto allocation_size=(size+0xffu)&~0xffu;
        if(allocation_size<size)throw IopFault(pc,"native IOP worker stack size overflow");
        for(std::size_t index=0;index<free_thread_stacks.size();++index)if(free_thread_stacks[index].size>=allocation_size) {
            auto& block=free_thread_stacks[index];const auto result=block.address;
            block.address+=allocation_size;block.size-=allocation_size;
            if(!block.size)free_thread_stacks.erase(free_thread_stacks.begin()+index);
            return result;
        }
        if(system_memory_service) {
            const auto result=allocate_system_storage(1,allocation_size);
            if(!result)throw IopFault(pc,"original SYSMEM exhausted for worker stack");
            thread_stack_top=result;return result;
        }
        if(system_scratch_high<system_scratch_low+allocation_size)
            throw IopFault(pc,"native IOP worker stacks exhausted");
        system_scratch_high-=allocation_size;thread_stack_top=system_scratch_high;
        system_allocations.push_back({system_scratch_high,allocation_size,true,false});
        return system_scratch_high;
    }
    void release_thread_stack(std::uint32_t address,std::uint32_t size) {
        const auto allocation_size=(size+0xffu)&~0xffu;
        if(!address || !size || allocation_size<size || address%16 || std::uint64_t(address)+allocation_size>ram.size())
            throw IopFault(pc,"invalid native IOP worker stack release");
        const auto allocation=std::find_if(system_allocations.begin(),system_allocations.end(),[&](const auto& item){return item.active && !item.static_module && item.address==address && item.size==allocation_size;});
        if(allocation!=system_allocations.end())allocation->active=false;
        free_thread_stacks.push_back({address,allocation_size});
        std::sort(free_thread_stacks.begin(),free_thread_stacks.end(),[](const auto& a,const auto& b){return a.address<b.address;});
        for(std::size_t index=1;index<free_thread_stacks.size();) {
            auto& previous=free_thread_stacks[index-1];const auto current=free_thread_stacks[index];
            if(std::uint64_t(previous.address)+previous.size==current.address) {
                previous.size+=current.size;free_thread_stacks.erase(free_thread_stacks.begin()+index);
            } else ++index;
        }
    }
    std::uint32_t delete_thread(std::uint32_t id) {
        if(in_interrupt || !id || id>threads.size() || id==current_thread || !threads[id-1].live)
            throw IopFault(pc,"invalid native IOP DeleteThread");
        auto& thread=threads[id-1];
        if(thread.ready || thread.sleeping || thread.delayed || thread.wait_semaphore || thread.granted_semaphore || thread.wait_event ||
           (thread.initialized && thread.context.pc!=0x1f0020))
            throw IopFault(pc,"DeleteThread target is not dormant");
        if(thread.stack_base)release_thread_stack(thread.stack_base,thread.stack_size);
        thread=Thread{};thread.live=false;return 0;
    }
    std::uint32_t start_thread(std::uint32_t id,std::uint32_t argument) {
        if(in_interrupt || !id || id>threads.size() || !threads[id-1].live || threads[id-1].ready)
            throw IopFault(pc,"invalid native IOP StartThread");
        auto& thread=threads[id-1];
        if(thread.initialized) {
            if(thread.context.pc!=0x1f0020 || thread.sleeping || thread.delayed || thread.wait_semaphore || thread.granted_semaphore || thread.wait_event)
                throw IopFault(pc,"StartThread target is not dormant");
            // A returned worker becomes dormant and may be started again. Keep
            // its reserved stack, but rebuild the guest CPU context so the new
            // argument reaches the original entry rather than the return sentinel.
            thread.context={};thread.initialized=false;thread.wakeups=0;
        }
        thread.argument=argument;thread.ready=true;return 0;
    }
    void exit_thread() {
        if(in_interrupt || !current_thread || current_thread>threads.size() || !threads[current_thread-1].live)
            throw IopFault(pc,"ExitThread outside native IOP worker");
        auto& t=threads[current_thread-1];
        if(!t.ready || t.sleeping || t.delayed || t.wait_semaphore || t.granted_semaphore || t.wait_event)
            throw IopFault(pc,"ExitThread from non-running IOP worker");
        t.ready=false;t.wakeups=0;locked_thread=0;owner_thread=0;
        // Same dormant state as a returned worker: retain its ID and stack for
        // a subsequent StartThread or DeleteThread, never resume the caller.
        pc=0x1f0020;
    }
    void threadman_reschedule(std::uint32_t interrupt_mode) {
        // The original wrapper treats a3 as a boolean when choosing its two
        // scheduler slots. Preserve that ABI and make the transfer observable.
        last_thread_reschedule_pc=pc;last_thread_reschedule_mode=interrupt_mode?1u:0u;
        if(thread_reschedule_count==~std::uint64_t{0})throw IopFault(pc,"IOP reschedule counter overflow");
        ++thread_reschedule_count;
        locked_thread=0;owner_thread=0;
        // service_iop_thread starts at this cursor on the next complete AOT
        // boundary. Advancing past the current owner models the callback's
        // selection of another equal-priority runnable context.
        if(current_thread) {
            if(current_thread>threads.size())throw IopFault(pc,"invalid native IOP reschedule owner");
            thread_cursor=current_thread%threads.size();
        }
    }
    std::uint32_t get_thread_id() const {
        // Original THREADMAN 0x9feb0/0x9fed4 returns -100 when
        // QueryIntrContext is nonzero, before consulting the thread handle.
        if(in_interrupt)return std::uint32_t(-100);
        if(!current_thread)throw IopFault(pc,"GetThreadId outside native worker");
        return current_thread;
    }
    std::uint32_t change_thread_priority(std::uint32_t id,std::uint32_t priority) {
        if(!id)id=current_thread;
        if(!id && !in_interrupt && priority && priority<=126) {bootstrap_thread_priority=priority;return 0;}
        if(in_interrupt || !id || id>threads.size() || !threads[id-1].live || !priority || priority>126)throw IopFault(pc,"invalid native IOP thread priority");
        threads[id-1].priority=priority;return 0;
    }
    std::uint32_t refer_thread_status(std::uint32_t id,std::uint32_t address) {
        if(!id)id=current_thread;
        if(!id || id>threads.size() || !threads[id-1].live || address%4 || std::uint64_t(address)+68>ram.size())
            throw IopFault(pc,"invalid native IOP thread status request");
        const auto& t=threads[id-1];
        const auto status=t.sleeping || t.wait_semaphore || t.wait_event || t.delayed?4u:id==current_thread?1u:t.ready?2u:16u;
        const std::array<std::uint32_t,17> fields{t.attr,t.option,status,t.entry,
            t.stack_base,t.stack_size,0,t.priority,t.priority,
            t.sleeping?1u:t.delayed?2u:t.wait_semaphore?3u:t.wait_event?4u:0u,t.wait_event?t.wait_event:t.wait_semaphore,t.wakeups,0,0,0,0,0};
        for(unsigned n=0;n<fields.size();++n)store(address+n*4,4,fields[n]);
        return 0;
    }
    std::uint32_t delay_thread(std::uint32_t microseconds) {
        if(in_interrupt || !current_thread || !microseconds || virtual_time_us+microseconds<virtual_time_us)
            throw IopFault(pc,"invalid native IOP timed wait");
        auto& t=threads[current_thread-1];t.wake_deadline=virtual_time_us+microseconds;t.delayed=true;t.ready=false;
        return 0;
    }
    std::uint32_t sleep_thread() {
        if(!current_thread || in_interrupt)throw IopFault(pc,"SleepThread outside native worker");
        auto& t=threads[current_thread-1];
        if(t.wakeups)--t.wakeups;else {t.sleeping=true;t.ready=false;}
        return 0;
    }
    std::uint32_t wakeup_thread(std::uint32_t id) {
        if(!id || id>threads.size() || !threads[id-1].live || !threads[id-1].initialized)throw IopFault(pc,"invalid native IOP wake target");
        auto& t=threads[id-1];
        if(t.sleeping){t.sleeping=false;t.ready=true;}
        else {if(t.wakeups==0xffffffffu)throw IopFault(pc,"IOP wake counter overflow");++t.wakeups;}
        return 0;
    }
    void enable_cpu_interrupts() {
        if(in_interrupt)throw IopFault(pc,"CpuEnableIntr from interrupt context");
        interrupts_enabled=true;last_interrupt_enable_pc=pc;
    }
    std::uint32_t disable_cpu_interrupts() {
        // INTRMAN CpuDisableIntr changes only the CPU interrupt-enable state;
        // registered handlers and their per-cause masks remain intact.
        interrupts_enabled=false;last_interrupt_disable_pc=pc;return 0;
    }
    struct Semaphore {std::uint32_t attr,option,count,maximum;};
    std::vector<Semaphore> semaphores;
    std::uint32_t create_semaphore(std::uint32_t address) {
        if(in_interrupt || address%4 || std::uint64_t(address)+16>ram.size())throw IopFault(pc,"invalid IOP semaphore descriptor");
        const auto attr=load(address,4,false),option=load(address+4,4,false),count=load(address+8,4,false),maximum=load(address+12,4,false);
        if(attr>1 || !maximum || maximum>0x7fffffffu || count>maximum || semaphores.size()>=256)throw IopFault(pc,"unsupported IOP semaphore profile");
        semaphores.push_back({attr,option,count,maximum});return std::uint32_t(semaphores.size());
    }
    Semaphore& semaphore(std::uint32_t id) {
        if(!id || id>semaphores.size())throw IopFault(pc,"invalid IOP semaphore identifier");
        return semaphores[id-1];
    }
    std::uint32_t signal_semaphore(std::uint32_t id) {
        auto& sem=semaphore(id);
        // A signal handed directly to a blocked waiter is consumed by that
        // wait. The resumed thread continues past WaitSema, so retaining the
        // same token in sem.count would duplicate it and can overflow a
        // max-count-one semaphore on the next legitimate signal.
        for(auto& t:threads)if(t.wait_semaphore==id){t.wait_semaphore=0;t.granted_semaphore=id;t.ready=true;return 0;}
        // Original THREADMAN iSignalSema at relocated 0xa1b70 compares the
        // current count against max and, when full, leaves the count intact
        // and returns -420 (0xfffffe5c).
        if(sem.count==sem.maximum)return std::uint32_t(-420);
        ++sem.count;
        return 0;
    }
    bool wait_semaphore(std::uint32_t id) {
        if(in_interrupt || !current_thread)throw IopFault(pc,"IOP semaphore wait outside worker context");
        auto& sem=semaphore(id);
        auto& t=threads[current_thread-1];
        if(t.granted_semaphore==id){t.granted_semaphore=0;return true;}
        if(sem.count){--sem.count;return true;}
        for(const auto& t:threads)if(t.wait_semaphore==id)throw IopFault(pc,"contended IOP semaphore ordering requires validation");
        t.wait_semaphore=id;t.ready=false;return false;
    }
    std::string console_output;
    std::uint64_t console_discarded_bytes=0;
    std::uint32_t print_console(std::uint32_t address) {
        auto string_at=[&](std::uint32_t pointer) {
            std::string result;
            for(unsigned n=0;n<4096;++n) {
                if(std::uint64_t(pointer)+n>=ram.size())throw IopFault(pc,"IOP console string outside RAM");
                const auto c=char(load(pointer+n,1,false));if(!c)return result;result+=c;
            }
            throw IopFault(pc,"unterminated IOP console string");
        };
        const auto format=string_at(address);std::string message;unsigned argument=0;
        for(std::size_t n=0;n<format.size();++n) {
            if(format[n]!='%'){message+=format[n];continue;}
            if(++n==format.size())throw IopFault(pc,"incomplete IOP printf conversion");
            const auto spec=format[n];if(spec=='%'){message+='%';continue;}
            if(spec!='s' && spec!='d' && spec!='u' && spec!='x' && spec!='c')throw IopFault(pc,"unsupported IOP printf conversion");
            const auto value=argument<3?r(5+argument):load(r(29)+16+(argument-3)*4,4,false);++argument;
            if(spec=='s')message+=string_at(value);
            else if(spec=='d')message+=std::to_string(std::int32_t(value));
            else if(spec=='u')message+=std::to_string(value);
            else if(spec=='c')message+=char(value);
            else {
                std::string digits;auto remaining=value;
                do{digits.insert(digits.begin(),"0123456789abcdef"[remaining&15]);remaining>>=4;}while(remaining);
                message+=digits;
            }
        }
        constexpr std::size_t capacity=65536;
        const auto total=console_output.size()+message.size();
        if(total>capacity) {
            const auto discard=total-capacity;console_discarded_bytes+=discard;
            if(message.size()>=capacity)console_output=message.substr(message.size()-capacity);
            else {console_output.erase(0,discard);console_output+=message;}
        } else console_output+=message;
        return std::uint32_t(message.size());
    }
    std::uint32_t query_interrupt_context() const {return in_interrupt?1u:0u;}
    struct InterruptHandler {bool registered=false;std::uint32_t mode=0,callback=0,argument=0;};
    std::array<InterruptHandler,64> interrupt_handlers{};
    std::uint64_t interrupt_mask=0;
    std::array<std::uint32_t,64> interrupt_options{};
    std::uint32_t new_context_callback=0,should_preempt_callback=0;
    std::uint32_t secrman_mc_command_callback=0,secrman_mc_devid_callback=0;
    void set_secrman_callback(bool command,std::uint32_t callback) {
        if(callback && (callback%4 || callback>=ram.size()))
            throw IopFault(pc,"invalid native SECRMAN memory-card callback");
        (command?secrman_mc_command_callback:secrman_mc_devid_callback)=callback;
    }
    void set_new_context_callback(std::uint32_t callback) {
        if(!callback || callback%4 || callback>=ram.size())throw IopFault(pc,"invalid native INTRMAN context-switch callback");
        new_context_callback=callback;
    }
    void set_should_preempt_callback(std::uint32_t callback) {
        if(!callback || callback%4 || callback>=ram.size())throw IopFault(pc,"invalid native INTRMAN preemption callback");
        should_preempt_callback=callback;
    }
    std::uint32_t system_event_bits=0;
    struct EventFlag {std::uint32_t attr,option,initial,bits;};
    std::vector<EventFlag> event_flags;
    // A bounded provenance ring avoids iterative diagnostic reruns when an
    // original worker blocks: it identifies the last producer/consumer edge
    // without retaining game data or changing guest-visible event state.
    struct EventTrace {std::uint32_t pc=0,return_pc=0,id=0,bits=0,before=0,after=0;char operation=0;};
    std::array<EventTrace,32> event_trace{};
    std::size_t event_trace_cursor=0;
    std::array<std::uint64_t,256> event_signal_count{};
    std::array<EventTrace,256> last_event_signal{};
    void trace_event(char operation,std::uint32_t id,std::uint32_t bits,std::uint32_t before,std::uint32_t after) {
        const EventTrace trace{pc,r(31),id,bits,before,after,operation};
        event_trace[event_trace_cursor++%event_trace.size()]=trace;
        if(operation=='S' && id<event_signal_count.size()) {
            ++event_signal_count[id];last_event_signal[id]=trace;
        }
    }
    // Instruction provenance is intentionally bounded and host-only.  It
    // permits one startup slice to identify a failing original call chain
    // without a trace file, capture, or change to guest execution.
    struct PcTrace {
        std::uint32_t pc=0,return_pc=0,a0=0,a1=0,a2=0,a3=0,v0=0,s0=0;
        std::uint32_t v1=0,t0=0,t4=0,s1=0,s2=0,s3=0,s4=0,s5=0,s6=0,s7=0,gp=0,sp=0,fp=0;
        std::uint32_t thread=0;
    };
    std::array<PcTrace,64> pc_trace{};
    std::size_t pc_trace_cursor=0;
    // Diagnostics may nominate several translated PCs as read-only snapshot
    // boundaries. Production execution leaves the list empty; no guest state
    // changes. Keeping distinct rings makes one startup run answer related
    // branch hypotheses without sequential trace/rebuild experiments.
    struct PcTraceWatch {std::uint32_t pc=0;std::array<PcTrace,64> history{};std::size_t cursor=0;};
    static constexpr std::size_t max_trace_watches=64;
    std::vector<PcTraceWatch> trace_watches;
    std::size_t trace_watch_count=0;
    bool add_trace_watch(std::uint32_t target) {
        if(!target)return false;
        for(std::size_t n=0;n<trace_watch_count;++n)if(trace_watches[n].pc==target)return true;
        if(trace_watch_count==max_trace_watches)return false;
        trace_watches.push_back({});trace_watches.back().pc=target;++trace_watch_count;return true;
    }
    std::array<PcTrace,64> last_sif0_error_trace{};
    std::size_t last_sif0_error_trace_cursor=0;
    struct RamWriteTrace {
        std::uint32_t pc=0,return_pc=0,address=0,before=0,after=0;
        unsigned size=0;std::size_t thread=0;
    };
    std::array<RamWriteTrace,16> ram_write_trace{};
    std::uint32_t ram_write_trace_address=0x0009a920u;
    std::size_t ram_write_trace_cursor=0;
    struct SifmanSubmitTrace {
        bool valid=false,descriptors_valid=false,arm_seen=false,arm_tags_valid=false,return_seen=false;
        std::uint32_t pc=0,return_pc=0,descriptor=0,count=0,a2=0,a3=0;
        std::array<std::uint32_t,8> descriptors{};
        std::array<std::uint32_t,4> channel9{},arm_channel9{};
        std::array<std::uint32_t,8> arm_tags{};
        std::uint32_t completed_channels=0;
        std::uint32_t submission_id=0;
        std::size_t thread=0;
    };
    std::array<SifmanSubmitTrace,32> sifman_submit_traces{};
    std::size_t sifman_submit_trace_cursor=0;
    SifmanSubmitTrace last_sifman_two_descriptor_trace{};
    // HG-DIAG-002: retain event snapshots when per-instruction history is off.
    // These committed-register reads must not create a delayed-load hazard.
    void trace_sifman_pc() {
        if(pc==0x0009c610u && (gpr[5]==1u || gpr[5]==2u)) {
            const auto descriptor=gpr[4];
            const auto descriptor_words=std::size_t(gpr[5])*4;
            if(descriptor%4==0 && std::uint64_t(descriptor)+descriptor_words*4<=ram.size()) {
                std::array<std::uint32_t,8> words{};
                for(std::size_t word=0;word<descriptor_words;++word) {
                    const auto address=descriptor+std::uint32_t(word*4);
                    words[word]=std::uint32_t(ram[address])|(std::uint32_t(ram[address+1])<<8)|
                                (std::uint32_t(ram[address+2])<<16)|(std::uint32_t(ram[address+3])<<24);
                }
                auto& trace=sifman_submit_traces[sifman_submit_trace_cursor++%sifman_submit_traces.size()];
                trace={};
                trace.valid=true;trace.descriptors_valid=true;trace.pc=pc;trace.return_pc=gpr[31];
                trace.descriptor=descriptor;trace.count=gpr[5];trace.a2=gpr[6];trace.a3=gpr[7];
                trace.descriptors=words;trace.channel9=dmac.channel[1];trace.completed_channels=dmac.completed_channels;
                trace.thread=current_thread;
            }
        }
        if(pc==0x0009c838u) {
            const auto count=std::min(sifman_submit_trace_cursor,sifman_submit_traces.size());
            for(std::size_t n=0;n<count;++n) {
                auto& trace=sifman_submit_traces[(sifman_submit_trace_cursor+sifman_submit_traces.size()-1-n)%sifman_submit_traces.size()];
                if(trace.valid && !trace.return_seen && current_thread==trace.thread) {
                    trace.return_seen=true;trace.submission_id=gpr[2];
                    if(trace.count==2u)last_sifman_two_descriptor_trace=trace;
                    break;
                }
            }
        }
    }
    void trace_pc() {
        pc_trace[pc_trace_cursor++%pc_trace.size()]={pc,gpr[31],gpr[4],gpr[5],gpr[6],gpr[7],gpr[2],gpr[16],
                                                    gpr[3],gpr[8],gpr[12],gpr[17],gpr[18],gpr[19],gpr[20],gpr[21],
                                                    gpr[22],gpr[23],gpr[28],gpr[29],gpr[30],current_thread};
        trace_sifman_pc();
        for(std::size_t n=0;n<trace_watch_count;++n)if(trace_watches[n].pc==pc) {
            trace_watches[n].history=pc_trace;trace_watches[n].cursor=pc_trace_cursor;
        }
    }
    std::uint32_t create_event_flag(std::uint32_t descriptor) {
        if(in_interrupt)throw IopFault(pc,"CreateEventFlag from interrupt context");
        const auto attr=load(descriptor,4,false),option=load(descriptor+4,4,false),bits=load(descriptor+8,4,false);
        if((attr!=0 && attr!=2) || event_flags.size()>=255)throw IopFault(pc,"unsupported/exhausted native IOP event flags");
        event_flags.push_back({attr,option,bits,bits});return std::uint32_t(event_flags.size()+1);
    }
    std::uint32_t& event_bits(std::uint32_t id) {
        if(id==1)return system_event_bits;
        if(id<2 || id-2>=event_flags.size())throw IopFault(pc,"invalid native IOP event flag identifier");
        return event_flags[id-2].bits;
    }
    std::uint32_t get_system_status_flag() const {return 1;}
    std::uint32_t event_status(std::uint32_t id,std::uint32_t address,bool interrupt) {
        if(in_interrupt!=interrupt)throw IopFault(pc,"event status query in wrong context");
        if(id<2 || id-2>=event_flags.size())throw IopFault(pc,"unmapped event status identifier");
        // Original THREADMAN +0x36a0: attr, option, initial, current, waiters.
        const auto physical=(address>=0x80000000 && address<0xc0000000)?address&0x1fffffffu:address;
        if(physical%4 || std::uint64_t(physical)+20>ram.size())throw IopFault(pc,"invalid event status destination");
        const auto& event=event_flags[id-2];std::uint32_t waiters=0;
        for(const auto& thread:threads)if(thread.live && thread.wait_event==id)++waiters;
        const std::array<std::uint32_t,5> words{{event.attr,event.option,event.initial,event.bits,waiters}};
        for(unsigned n=0;n<words.size();++n)store(address+n*4,4,words[n]);
        return 0;
    }
    std::uint32_t refer_event_status(std::uint32_t id,std::uint32_t address) {return event_status(id,address,false);}
    std::uint32_t irefer_event_status(std::uint32_t id,std::uint32_t address) {return event_status(id,address,true);}
    std::uint32_t set_event_flag(std::uint32_t id,std::uint32_t bits) {
        auto& current=event_bits(id);const auto before=current;current|=bits;trace_event('S',id,bits,before,current);
        for(auto& t:threads)if(t.wait_event==id && ((t.wait_event_mode&1)?(current&t.wait_event_bits)!=0:(current&t.wait_event_bits)==t.wait_event_bits)) {
            // Original iSetEventFlag a11d8 captures the result and a11f0
            // clears WEF_CLEAR before the waiter is made runnable.
            if(t.wait_event_result)store(t.wait_event_result,4,current);
            if(t.wait_event_mode&0x10)current=0;
            t.granted_event=id;t.wait_event=0;t.ready=true;
        }
        return 0;
    }
    // Wait for a new display edge. Past edges are not banked as wake tokens.
    std::uint64_t vblank_start_count=0;
    bool wait_vblank_start() {
        if(in_interrupt || !current_thread || current_thread>threads.size())
            throw IopFault(pc,"WaitVblankStart requires a native thread context");
        auto& t=threads[current_thread-1];
        if(t.vblank_start_granted) {
            t.vblank_start_granted=false;t.waiting_vblank_start=false;return true;
        }
        t.waiting_vblank_start=true;t.ready=false;return false;
    }
    void signal_vblank_start() {
        ++vblank_start_count;
        for(auto& t:threads)if(t.live && t.waiting_vblank_start) {
            t.waiting_vblank_start=false;t.vblank_start_granted=true;t.ready=true;
        }
    }
    std::uint32_t wait_vblank_end() {
        constexpr std::uint32_t field_us=16667;
        return delay_thread(field_us-std::uint32_t(virtual_time_us%field_us));
    }
    std::uint32_t clear_event_flag(std::uint32_t id,std::uint32_t bits) {
        if(in_interrupt)throw IopFault(pc,"ClearEventFlag from interrupt context");
        auto& current=event_bits(id);const auto before=current;current&=bits;trace_event('C',id,bits,before,current);return 0;
    }
    bool wait_event_flag(std::uint32_t id,std::uint32_t bits,std::uint32_t mode,std::uint32_t result) {
        if(!bits || (mode&~0x11u) || in_interrupt)throw IopFault(pc,"unsupported IOP event wait");
        if(current_thread && threads[current_thread-1].granted_event) {
            auto& t=threads[current_thread-1];
            if(t.granted_event!=id || t.wait_event_bits!=bits || t.wait_event_mode!=mode || t.wait_event_result!=result)
                throw IopFault(pc,"IOP granted event resumed with different wait arguments");
            t.granted_event=0;t.wait_event_bits=0;t.wait_event_result=0;return true;
        }
        const auto current=event_bits(id);
        const bool ready=(mode&1)?(current&bits)!=0:(current&bits)==bits;
        if(!ready) {
            trace_event('W',id,bits,current,current);
            if(current_thread) {
                auto& t=threads[current_thread-1];t.wait_event=id;t.wait_event_bits=bits;t.wait_event_mode=mode;t.wait_event_result=result;t.ready=false;
            }
            return false;
        }
        if(result)store(result,4,current);
        if(mode&0x10)event_bits(id)=0; // Original THREADMAN clears all bits after capturing result.
        if(current_thread){threads[current_thread-1].wait_event=0;threads[current_thread-1].wait_event_bits=0;}
        return true;
    }
    static bool valid_interrupt(std::uint32_t irq) {return irq<=25 || (irq>=32 && irq<=46) || irq==62 || irq==63;}
    std::uint32_t register_interrupt(std::uint32_t irq,std::uint32_t mode,std::uint32_t callback,std::uint32_t argument) {
        if(!valid_interrupt(irq))return std::uint32_t(-101);
        if(mode>2)return std::uint32_t(-405);
        if(!callback || callback%4 || callback>=ram.size())return std::uint32_t(-1);
        if(interrupt_handlers[irq].registered)return std::uint32_t(-104);
        interrupt_handlers[irq]={true,mode,callback,argument};return 0;
    }
    std::uint32_t release_interrupt(std::uint32_t irq) {
        if(!valid_interrupt(irq))return std::uint32_t(-101);
        if(!interrupt_handlers[irq].registered)return std::uint32_t(-105);
        interrupt_handlers[irq]={};return 0;
    }
    std::uint32_t enable_interrupt(std::uint32_t irq) {
        const auto options=irq&~0xffu;irq&=0xff;
        if(options && !(options==0x200 && irq>=32 && irq<=46))
            throw IopFault(pc,"unsupported IOP interrupt enable options");
        if(!valid_interrupt(irq))return std::uint32_t(-101);
        interrupt_options[irq]=options;
        interrupt_mask|=std::uint64_t(1)<<irq;return 0;
    }
    std::uint32_t disable_interrupt(std::uint32_t irq,std::uint32_t result) {
        if(!valid_interrupt(irq))return std::uint32_t(-101);
        if(result)store(result,4,irq);
        interrupt_mask&=~(std::uint64_t(1)<<irq);return 0;
    }
    std::uint32_t suspend_interrupts(std::uint32_t saved_address) {
        if(!saved_address)throw IopFault(pc,"null interrupt-state output pointer");
        const auto previous=interrupts_enabled;
        store(saved_address,4,previous?1u:0u);
        interrupts_enabled=false;last_interrupt_suspend_pc=pc;
        return previous?0u:std::uint32_t(-102); // Public KE_CPUDI; saved state remains valid.
    }
    std::uint32_t resume_interrupts(std::uint32_t saved) {
        if(saved>1)throw IopFault(pc,"unknown native IOP interrupt-state token");
        interrupts_enabled=saved!=0;last_interrupt_resume_pc=pc;last_interrupt_resume_token=saved;return 0;
    }
    std::uint64_t instruction_cache_epoch=0,data_cache_epoch=0;
    void flush_cache(bool instruction) {
        std::atomic_thread_fence(std::memory_order_seq_cst);
        if(instruction)++instruction_cache_epoch;else ++data_cache_epoch;
    }
    std::vector<std::uint8_t> ram=std::vector<std::uint8_t>(2*1024*1024);
    unsigned pending_reg=0,next_reg=0;
    std::uint32_t pending_value=0,next_value=0;
    void check_hazard(unsigned reg) const {
        if(reg && reg==pending_reg)
            throw IopFault(pc,"IOP load-delay dependency requires hardware validation");
    }
    std::uint32_t read_cop0(unsigned reg) const {
        if(reg==15)return processor_id;
        if(reg==12)return cop0_status;
        throw IopFault(pc,"unimplemented IOP coprocessor register");
    }
    void write_cop0(unsigned reg,std::uint32_t value) {
        if(reg==12) {cop0_status=value;return;}
        throw IopFault(pc,"unimplemented IOP coprocessor register write");
    }
    std::uint32_t r(unsigned reg) const {check_hazard(reg);return reg?gpr.at(reg):0;}
    void w(unsigned reg,std::uint32_t value) {
        if(!reg)return;
        // On the R3000, a non-load write in the load-delay slot wins over and
        // cancels the older delayed write to the same register.
        if(pending_reg==reg)pending_reg=0;
        gpr.at(reg)=value;
    }
    void load_register(unsigned reg,std::uint32_t value) {
        // Consecutive loads to one destination likewise cancel the older load;
        // the newer value remains delayed for the following instruction.
        if(reg && pending_reg==reg)pending_reg=0;
        next_reg=reg;next_value=value;
    }
    void finish_instruction() {
        if(pending_reg)gpr.at(pending_reg)=pending_value;
        pending_reg=next_reg;pending_value=next_value;next_reg=0;
    }
    static std::int64_t signed_word(std::uint32_t value) {
        return value&0x80000000u ? std::int64_t(value)-0x100000000ll : value;
    }
    std::uint32_t checked_arithmetic(std::uint32_t a,std::uint32_t b,bool subtract) const {
        const auto result=subtract ? signed_word(a)-signed_word(b) : signed_word(a)+signed_word(b);
        if(result< -0x80000000ll || result>0x7fffffffll)
            throw IopFault(pc,"IOP arithmetic overflow; CPU exception delivery not implemented");
        return std::uint32_t(result);
    }
    void multiply(std::uint32_t a,std::uint32_t b,bool sign) {
        const auto product=sign ? std::uint64_t(signed_word(a)*signed_word(b)) : std::uint64_t(a)*b;
        lo=std::uint32_t(product);hi=std::uint32_t(product>>32);
    }
    void divide(std::uint32_t a,std::uint32_t b,bool sign) {
        if(!b || (sign && a==0x80000000u && b==0xffffffffu))
            throw IopFault(pc,"undefined IOP division result requires hardware validation");
        if(sign) {
            const auto left=signed_word(a),right=signed_word(b);
            lo=std::uint32_t(left/right);hi=std::uint32_t(left%right);
        } else {lo=a/b;hi=a%b;}
    }
    std::uint8_t* memory(std::uint32_t address,unsigned size) {
        if(size!=1 && size!=2 && size!=4)throw IopFault(pc,"invalid IOP memory width");
        if(address%size)throw IopFault(pc,"unaligned IOP memory access");
        if(address>=0x80000000 && address<0xc0000000)address&=0x1fffffff;
        if(std::uint64_t(address)+size>ram.size())
            throw IopFault(pc,"unimplemented IOP memory/device address="+std::to_string(address)+
                              " width="+std::to_string(size));
        return ram.data()+address;
    }
    void load_unaligned(unsigned reg,std::uint32_t address,bool left) {
        const auto previous=reg && pending_reg==reg?pending_value:r(reg); // MIPS-I unaligned-merge forwarding exception.
        memory(address&~3u,4); // Unaligned device transactions are not modeled.
        const auto value=load(address&~3u,4,false);
        const unsigned shift=(left?3-(address&3):(address&3))*8;
        const auto mask=left?0xffffffffu<<shift:0xffffffffu>>shift;
        next_reg=reg;next_value=(previous&~mask)|(left?value<<shift:value>>shift);
    }
    void store_unaligned(std::uint32_t address,std::uint32_t value,bool left) {
        auto* bytes=memory(address&~3u,4); // Validate whole write before changing bytes.
        const unsigned lane=address&3;
        if(left)for(unsigned n=0;n<=lane;++n)bytes[n]=std::uint8_t(value>>((3-lane+n)*8));
        else for(unsigned n=lane;n<4;++n)bytes[n]=std::uint8_t(value>>((n-lane)*8));
    }
    std::uint32_t load(std::uint32_t address,unsigned size,bool sign) {
        const auto physical=(address>=0x80000000 && address<0xc0000000)?address&0x1fffffff:address;
        // Read-only native platform probes. Other device accesses and all writes
        // remain unsupported until their state transitions are implemented.
        if(size==1 && physical==0x1f402005){const auto value=cdvd_read_pending?0xc0u:0x40u;trace_cdvd_mmio(physical,size,value,false);return value;}
        if(size==1 && physical==0x1f402004){trace_cdvd_mmio(physical,size,cdvd_n_command,false);return cdvd_n_command;}
        if(size==1 && physical==0x1f402006){trace_cdvd_mmio(physical,size,cdvd_error,false);return cdvd_error;}
        if(size==1 && physical==0x1f40200a){trace_cdvd_mmio(physical,size,cdvd_drive_status,false);return cdvd_drive_status;}
        if(size==1 && physical==0x1f40200b){trace_cdvd_mmio(physical,size,cdvd_sticky_status,false);return cdvd_sticky_status;}
        if(size==1 && physical==0x1f40200f){trace_cdvd_mmio(physical,size,cdvd_disk_type,false);return cdvd_disk_type;}
        if(size==1 && physical==0x1f402013){trace_cdvd_mmio(physical,size,cdvd_decoder_status,false);return cdvd_decoder_status;}
        if(size==1 && physical==0x1f402008){trace_cdvd_mmio(physical,size,cdvd_interrupt_status,false);return cdvd_interrupt_status;}
        if(size==1 && physical==0x1f402016){trace_cdvd_mmio(physical,size,cdvd_last_s_command,false);return cdvd_last_s_command;}
        if(size==1 && physical==0x1f402018) {
            if(cdvd_result_cursor<cdvd_s_results.size())cdvd_last_result=cdvd_s_results[cdvd_result_cursor++];
            trace_cdvd_mmio(physical,size,cdvd_last_result,false);return cdvd_last_result; // Empty drain reads preserve the latch.
        }
        if(size==1 && physical==0x1f402017){const auto value=cdvd_result_cursor<cdvd_s_results.size()?0u:0x40u;trace_cdvd_mmio(physical,size,value,false);return value;}
        if(size==4 && physical==0x1f801450)return hardware_config;
        if(size==4 && physical==0x1f801404)return spu2_mapping_0;
        if(size==4 && physical==0x1f80140c)return spu2_mapping_1;
        if(size==4 && physical==0x1f801014)return expansion_delay_1;
        if(size==4 && physical==0x1f801414)return spu2_delay;
        if(size==2 && (physical==0x1f900344u || physical==0x1f900744u)) {
            // Original LIBSD polls bit 7 after voice DMA and before clearing
            // ATTR transfer mode. Expose only actual committed voice transfers;
            // this is not a general STATX/DREQ hardware implementation.
            const unsigned core=physical==0x1f900344u?0:1;
            return spu2_voice_transfer_complete[core]?0x80u:0u;
        }
        if(size==2 && supported_spu2_register(physical))return spu2_registers[(physical-0x1f900000)/2];
        if(size==4 && physical==0x1d000060)return sif_bus_probe;
        if(size==4 && physical==0x1d000020)return sif->main_flags;
        if(size==4 && physical==0x1d000000)return sif->main_address;
        if(size==4 && physical==0x1d000010)return sif->sub_address;
        if(size==4 && physical==0x1d000030)return sif->sub_flags;
        if(size==4 && physical==0x1d000040)return sif->iop_control;
        if(timer_register(physical)>=0)return load_timer(physical,size);
        if(dmac.contains(physical)) {
            try{return dmac.read(physical,size);}catch(const std::exception& e){throw IopFault(pc,e.what());}
        }
        if(sio2_contains(physical))return load_sio2(physical,size);
        const auto* p=memory(address,size);std::uint32_t value=0;
        for(unsigned n=0;n<size;++n)value|=std::uint32_t(p[n])<<(n*8);
        if(sign && size<4 && (value&(1u<<(size*8-1))))value|=~0u<<(size*8);
        return value;
    }
    void pump_spu2_input(unsigned core) {
        auto& transfer=spu2_input_dma[core];const auto dma_base=core?0x1f801500u:0x1f8010c0u;
        while(transfer.active && spu2_input.can_fill(core)) {
            const auto source=dmac.read(dma_base,4);
            const auto expected_source=transfer.source+transfer.total-transfer.remaining;
            const auto expected_bcr=((transfer.remaining/(transfer.block_words*4u))<<16)|transfer.block_words;
            if(source!=expected_source || dmac.read(dma_base+4,4)!=expected_bcr ||
               dmac.read(dma_base+8,4)!=0x01000201u)
                throw IopFault(pc,"SPU2 active input DMA registers changed");
            const auto destination=Spu2Input::sample_address(core,0,spu2_input.cores[core].write_half*256);
            // HG-DIAG-129 (HG_SPU2_INPUT_RACE=1): report source bytes that changed after the
            // previous half was copied (the copy is instantaneous; hardware DMA is not).
            {
                static const bool race=std::getenv("HG_SPU2_INPUT_RACE")!=nullptr;
                static std::array<std::array<std::uint8_t,1024>,2> copied{};static std::array<std::uint32_t,2> copied_from{};
                if(race) {
                    if(copied_from[core]) {
                        unsigned first=1024,last=0,count=0;
                        for(unsigned n=0;n<1024;++n)if(ram[copied_from[core]+n]!=copied[core][n]){if(first==1024)first=n;last=n;++count;}
                        if(count)std::fprintf(stderr,"HG-DIAG-129 core%u source 0x%x changed after copy: %u bytes in [%u,%u]\n",core,copied_from[core],count,first,last);
                    }
                    for(unsigned n=0;n<1024;++n)copied[core][n]=ram[source+n];
                    copied_from[core]=source;
                }
                if(std::getenv("HG_SIF1_WATCH"))std::fprintf(stderr,"HG-DIAG-130 SPU2 input fill core%u source=0x%x half=%u\n",core,source,spu2_input.cores[core].write_half);
            }
            spu2_input.fill(core,[&](unsigned offset){return ram[source+offset];},
                                [&](std::uint32_t address,std::uint8_t byte){spu2_ram[address]=byte;});
            transfer.remaining-=Spu2Input::bytes_per_half;
            dmac.word(dma_base)=source+Spu2Input::bytes_per_half;
            dmac.word(dma_base+4)=((transfer.remaining/(transfer.block_words*4u))<<16)|transfer.block_words;
            spu2_dma_bytes+=Spu2Input::bytes_per_half;
            last_spu2_dma_source=source;last_spu2_dma_destination=destination;last_spu2_dma_core=core;
            if(!transfer.remaining) {
                transfer.active=false;dmac.word(dma_base+8)&=~0x01000000u;
                dmac.completed_channels|=1u<<(core?7:4);++spu2_dma_count;
            }
        }
    }
    template<class Emit,class Mix> void advance_spu2(std::uint32_t microseconds,Emit emit,Mix mix) {
        try {
            spu2_input.advance(microseconds,[&](std::uint32_t address) {
                return std::uint16_t(spu2_ram[address]|(std::uint16_t(spu2_ram[address+1])<<8));
            },[&](unsigned core,std::uint16_t left,std::uint16_t right) {
                spu2_voices.input_sample(core,left,right);emit(core,left,right);
            },[&](unsigned core){pump_spu2_input(core);});
            spu2_voices.advance(microseconds,spu2_registers,spu2_ram,mix);
        }catch(const IopFault&){throw;}catch(const std::runtime_error& e){throw IopFault(pc,e.what());}
    }
    template<class Emit> void advance_spu2(std::uint32_t microseconds,Emit emit) {
        advance_spu2(microseconds,emit,[](Spu2Voices::Frame){});
    }
    void advance_spu2(std::uint32_t microseconds) {
        advance_spu2(microseconds,[&](unsigned core,std::uint16_t left,std::uint16_t right) {
            spu2_input_history[spu2_input_frames++%spu2_input_history.size()]={core,left,right};
        });
    }
    bool complete_spu2_dma(std::uint32_t physical,unsigned size,std::uint32_t value) {
        if(size!=4 || (physical!=0x1f8010c8 && physical!=0x1f801508))return false;
        const unsigned core=physical==0x1f8010c8?0:1;
        if(!(value&0x01000000u)) {spu2_input_dma[core].active=false;return false;}
        if(value!=0x01000201u)throw IopFault(pc,"unsupported SPU2 DMA mode");
        if(spu2_input_dma[core].active)throw IopFault(pc,"SPU2 input DMA already active");
        const auto auto_dma=spu2_registers[(core*0x400u+0x1b0u)/2];
        const std::uint32_t dma_base=core?0x1f801500u:0x1f8010c0u;
        const auto source=dmac.read(dma_base,4),bcr=dmac.read(dma_base+4,4);
        const std::uint64_t words=std::uint64_t(bcr&0xffffu)*(bcr>>16),bytes=words*4;
        if(auto_dma) {
            if(auto_dma!=(1u<<core) || !spu2_input.cores[core].enabled)
                throw IopFault(pc,"unsupported SPU2 AutoDMA control");
            // LIBSD uses 16-word DMA blocks. Whole stereo buffer halves are
            // required until partial AutoDMA requests have independent evidence.
            if((bcr&0xffffu)!=16 || !bytes || bytes%Spu2Input::bytes_per_half || source%16 ||
               std::uint64_t(source)+bytes>ram.size())throw IopFault(pc,"unsupported SPU2 AutoDMA source/size");
            spu2_voice_transfer_complete[core]=false;
            spu2_input_dma[core]={true,source,std::uint32_t(bytes),std::uint32_t(bytes),16};
            dmac.word(dma_base+8)=value;pump_spu2_input(core);return true;
        }
        const auto register_base=core*0x400u;
        const auto destination_units=(std::uint32_t(spu2_registers[(register_base+0x1a8)/2]&0x3fu)<<16)|
                                     spu2_registers[(register_base+0x1aa)/2];
        const std::uint64_t destination=std::uint64_t(destination_units)*2;
        if(!bytes || std::uint64_t(source)+bytes>ram.size() || destination+bytes>spu2_ram.size())
            throw IopFault(pc,"SPU2 DMA range is outside retained memory");
        std::copy_n(ram.data()+source,std::size_t(bytes),spu2_ram.data()+destination);
        spu2_voice_transfer_complete[core]=true;
        dmac.word(dma_base)=source+std::uint32_t(bytes);
        // LIBSD's original DMA interrupt callback reloads only the high
        // halfword (block count) of BCR before rearming CHCR.  The low
        // halfword (block size) therefore survives completion.
        dmac.word(dma_base+4)=bcr&0xffffu;
        dmac.word(dma_base+8)=value&~0x01000000u;
        dmac.completed_channels|=1u<<(core?7:4);
        ++spu2_dma_count;spu2_dma_bytes+=bytes;
        last_spu2_dma_source=source;last_spu2_dma_destination=std::uint32_t(destination);last_spu2_dma_core=core;
        return true;
    }
    void store(std::uint32_t address,unsigned size,std::uint32_t value) {
        const auto physical=(address>=0x80000000 && address<0xc0000000)?address&0x1fffffff:address;
        if(size==1 && physical>=0x1f402004 && physical<=0x1f402018)trace_cdvd_mmio(physical,size,value,true);
        if(size==1 && physical==0x1f402006){cdvd_error=std::uint8_t(value);return;}
        if(size==1 && physical==0x1f402005) {
            if(cdvd_n_parameters.size()>=16)throw IopFault(pc,"CDVD N-command parameter overflow");
            cdvd_n_parameters.push_back(std::uint8_t(value));return;
        }
        if(size==1 && physical==0x1f402004){submit_cdvd_n_command(std::uint8_t(value));return;}
        if(size==1 && physical==0x1f402008){cdvd_interrupt_status&=~value;return;}
        if(size==1 && physical==0x1f402017) {
            if(cdvd_s_parameters.size()>=16)throw IopFault(pc,"CDVD S-command parameter overflow");
            cdvd_s_parameters.push_back(std::uint8_t(value));return;
        }
        if(size==1 && physical==0x1f402016){submit_cdvd_s_command(std::uint8_t(value));return;}
        if(size==4 && physical==0x1f801450 && value==hardware_config)return;
        if(size==4 && physical==0x1f801404) {
            if(value!=0xbf900000u)throw IopFault(pc,"unsupported SPU2 core-0 mapping");
            spu2_mapping_0=value;return;
        }
        if(size==4 && physical==0x1f80140c) {
            if(value!=0xbf900800u)throw IopFault(pc,"unsupported SPU2 core-1 mapping");
            spu2_mapping_1=value;return;
        }
        if(size==4 && (physical==0x1f801014 || physical==0x1f801414)) {
            if(value!=0x200b31e1u)throw IopFault(pc,"unsupported SPU2 bus-delay configuration");
            (physical==0x1f801014?expansion_delay_1:spu2_delay)=value;return;
        }
        if(size==2 && (physical==0x1f900344u || physical==0x1f900744u))
            throw IopFault(pc,"SPU2 transfer status is read-only");
        if(size==2 && supported_spu2_register(physical)) {
            // HG-DIAG-118: optional host-only SPU2 register write trace.
            if(spu2_trace_file())std::fprintf(spu2_trace_file(),"%llu %03x %04x\n",
                static_cast<unsigned long long>(virtual_time_us),unsigned(physical-0x1f900000),unsigned(value&0xffffu));
            if(physical==0x1f90019au || physical==0x1f90059au) {
                const unsigned core=physical==0x1f90019au?0:1;
                if((value&0x30u)!=(spu2_registers[(physical-0x1f900000)/2]&0x30u))
                    spu2_voice_transfer_complete[core]=false;
            }
            if(physical==0x1f9001b0u || physical==0x1f9005b0u) {
                const unsigned core=physical==0x1f9001b0u?0:1;
                if(value && value!=(1u<<core))throw IopFault(pc,"unsupported SPU2 AutoDMA control");
                if(spu2_input_dma[core].active && !value)throw IopFault(pc,"stop SPU2 input DMA before disabling input");
                if(spu2_registers[(physical-0x1f900000)/2]!=std::uint16_t(value))spu2_voices.input_reset(core);
                spu2_input.enable(core,value!=0);
            }
            spu2_voices.register_written(physical-0x1f900000,std::uint16_t(value));
            spu2_registers[(physical-0x1f900000)/2]=std::uint16_t(value);return;
        }
        if(size==4 && physical==0x1d000010){sif->sub_address=value;return;}
        if(size==4 && physical==0x1d000020){sif->main_flags&=~value;return;}
        if(size==4 && physical==0x1d000030){sif->sub_flags|=value;return;}
        if(size==4 && physical==0x1d000040) {
            // Original SIFMAN issues the observed 0x40 then 0x20 command
            // sequence and polls the latter state.  This register retains the
            // most recent supported command; it is not a one-way bit latch.
            if(value!=0x40 && value!=0x20)throw IopFault(pc,"unsupported SIF control transition");
            sif->iop_control=value;return;
        }
        if(timer_register(physical)>=0){store_timer(physical,size,value);return;}
        if(complete_spu2_dma(physical,size,value))return;
        if(dmac.contains(physical)) {
            if(physical==0x1f801528 && size==4 && (value&0x01000000u)) {
                last_sif0_arm_pc=pc;last_sif0_arm_return_pc=r(31);
            }
            try{
                dmac.write(physical,size,value);
                if(physical==0x1f801528 && size==4 && (value&0x01000000u) && pc==0x0009c810u) {
                    const auto count=std::min(sifman_submit_trace_cursor,sifman_submit_traces.size());
                    for(std::size_t n=0;n<count;++n) {
                        auto& trace=sifman_submit_traces[(sifman_submit_trace_cursor+sifman_submit_traces.size()-1-n)%sifman_submit_traces.size()];
                        if(!trace.valid || trace.arm_seen || trace.return_seen || current_thread!=trace.thread)continue;
                        trace.arm_seen=true;trace.arm_channel9=dmac.channel[1];
                        const auto tag=trace.arm_channel9[3];
                        const auto tag_words=std::size_t(trace.count)*4;
                        if(tag%4==0 && std::uint64_t(tag)+tag_words*4<=ram.size()) {
                            for(std::size_t word=0;word<tag_words;++word) {
                                const auto address=tag+std::uint32_t(word*4);
                                trace.arm_tags[word]=std::uint32_t(ram[address])|(std::uint32_t(ram[address+1])<<8)|
                                                     (std::uint32_t(ram[address+2])<<16)|(std::uint32_t(ram[address+3])<<24);
                            }
                            trace.arm_tags_valid=true;
                        }
                        break;
                    }
                }
                return;
            }catch(const std::exception& e){
                std::string message=e.what();
                if(message.find("SIF configuration")!=std::string::npos) {
                    // Detail for active-channel mutation stops: who armed SIF0 and the
                    // interrupt/thread context of the mutating store.
                    char detail[256];
                    std::snprintf(detail,sizeof detail,"; register=0x%08x value=0x%x size=%u ch9_chcr=0x%08x armed_by_pc=0x%x ra=0x%x"
                        " interrupts_enabled=%d in_interrupt=%d thread=%u",unsigned(physical),unsigned(value),size,
                        unsigned(dmac.channel[1][2]),unsigned(last_sif0_arm_pc),unsigned(last_sif0_arm_return_pc),
                        int(interrupts_enabled),int(in_interrupt),unsigned(current_thread));
                    message+=detail;
                }
                throw IopFault(pc,message);
            }
        }
        if(sio2_contains(physical)){store_sio2(physical,size,value);return;}
        const std::uint32_t traced_word=ram_write_trace_address;
        const bool trace_write=std::uint64_t(traced_word)+4<=ram.size() && physical<=traced_word+3 && std::uint64_t(physical)+size>traced_word;
        std::uint32_t before=0;
        if(trace_write)for(unsigned n=0;n<4;++n)before|=std::uint32_t(ram[traced_word+n])<<(n*8);
        auto* p=memory(address,size);
        for(unsigned n=0;n<size;++n)p[n]=std::uint8_t(value>>(n*8));
        if(trace_write) {
            std::uint32_t after=0;for(unsigned n=0;n<4;++n)after|=std::uint32_t(ram[traced_word+n])<<(n*8);
            if(after!=before)ram_write_trace[ram_write_trace_cursor++%ram_write_trace.size()]={pc,std::uint32_t(r(31)),physical,before,after,size,current_thread};
        }
    }
};
inline bool iop_signed_less(std::uint32_t a,std::uint32_t b) {
    return (a^0x80000000u)<(b^0x80000000u);
}
inline std::uint32_t iop_sra(std::uint32_t value,unsigned amount) {
    amount&=31;
    return amount ? (value>>amount)|((value&0x80000000u)?~0u<<(32-amount):0) : value;
}
}
