#include "hg/vif.hpp"
#include "hg/vu1_spec.hpp"
#include <array>

namespace hg {
// HG-DIAG-089: comparison switch for tests only; production keeps the fast path.
bool vif1_generic_unpack_only=false;
Vu1SpecInfoQuery vu1_spec_info_query=nullptr;
thread_local Vu1UnpackLog* vif1_unpack_log=nullptr;
thread_local void (*vif1_mscnt_barrier)()=nullptr;
namespace {
// Straight-line UNPACK for the common case (no RGBA5, CL>=WL, every mask
// selector zero). Same vector order, fault point, sign/zero extension, STMOD
// row arithmetic and V2/V3 indeterminate-lane marking as the generic loop.
template<unsigned Bits,unsigned Fields,bool Scalar>
void unpack_linear(Vif1Path& v,const std::uint8_t* source,std::size_t vectors,std::size_t destination,
                   std::size_t cl,std::size_t wl,bool zero_extend) {
    constexpr unsigned bytes=Bits/8,unit=Scalar?bytes:Fields*bytes;
    for(std::size_t n=0;n<vectors;++n,source+=unit) {
        const auto vector=destination+cl*(n/wl)+n%wl;
        if(vector>=1024)throw std::runtime_error("VIF1 UNPACK exceeds VU1 data memory");
        auto defined=v.vu_mem_defined[vector];
        for(unsigned field=0;field!=4;++field) {
            if(vif1_unpack_log)vif1_unpack_log->lane(vector,field,Scalar||field<Fields);
            if(!Scalar&&field>=Fields){defined&=std::uint8_t(~(1u<<field));continue;}
            const auto* at=source+(Scalar?0:field*bytes);
            std::uint32_t value=at[0];
            if constexpr(Bits>=16)value|=std::uint32_t(at[1])<<8;
            if constexpr(Bits==32)value|=(std::uint32_t(at[2])<<16)|(std::uint32_t(at[3])<<24);
            if constexpr(Bits!=32)if(!zero_extend){constexpr auto sign=std::uint32_t(1)<<(Bits-1);value=(value^sign)-sign;}
            if(v.mode){value+=v.row[field];if(v.mode==2)v.row[field]=value;}
            v.vu_mem[vector*4+field]=value;defined|=std::uint8_t(1u<<field);
        }
        v.vu_mem_defined[vector]=defined;
    }
}
bool try_unpack_linear(Vif1Path& v,unsigned format,const std::uint8_t* source,std::size_t vectors,
                       std::size_t destination,std::size_t cl,std::size_t wl,bool zero_extend) {
    switch(format) {
    case 0x0:unpack_linear<32,1,true>(v,source,vectors,destination,cl,wl,zero_extend);return true;
    case 0x1:unpack_linear<16,1,true>(v,source,vectors,destination,cl,wl,zero_extend);return true;
    case 0x2:unpack_linear<8,1,true>(v,source,vectors,destination,cl,wl,zero_extend);return true;
    case 0x4:unpack_linear<32,2,false>(v,source,vectors,destination,cl,wl,zero_extend);return true;
    case 0x5:unpack_linear<16,2,false>(v,source,vectors,destination,cl,wl,zero_extend);return true;
    case 0x6:unpack_linear<8,2,false>(v,source,vectors,destination,cl,wl,zero_extend);return true;
    case 0x8:unpack_linear<32,3,false>(v,source,vectors,destination,cl,wl,zero_extend);return true;
    case 0x9:unpack_linear<16,3,false>(v,source,vectors,destination,cl,wl,zero_extend);return true;
    case 0xa:unpack_linear<8,3,false>(v,source,vectors,destination,cl,wl,zero_extend);return true;
    case 0xc:unpack_linear<32,4,false>(v,source,vectors,destination,cl,wl,zero_extend);return true;
    case 0xd:unpack_linear<16,4,false>(v,source,vectors,destination,cl,wl,zero_extend);return true;
    case 0xe:unpack_linear<8,4,false>(v,source,vectors,destination,cl,wl,zero_extend);return true;
    default:return false;
    }
}
}
// Kept out of the common VU/EE header so parser-only changes do not rebuild
// all translated game code. Command semantics remain in their original order.
void Vif1Path::process_pending(GifPath& gif,GsRegisterState& gs) {
        pending_checked=std::size_t(-1);
        if(pending.size()>max_pending)throw std::runtime_error("VIF1 packet exceeds bounded path capacity");
        std::size_t cursor=0,need=0;
        while(true) {
            if(pending.size()-cursor<4){need=cursor+4;break;}
            const auto code=word(pending,cursor);
            if(code&0x80000000u)throw std::runtime_error("VIF1 interrupt-control VIFcode requires interrupt delivery");
            const auto command=std::uint8_t(code>>24)&0x7f;
            const auto immediate=std::uint16_t(code);
            if(trace_vifcodes)record_vifcode(code,command>=0x60?(immediate&0x8000?tops:0)+(immediate&0x3ff):0xffffffffu);
            if(command==0x20) { // STMASK: one following word.
                if(pending.size()-cursor<8){need=cursor+8;break;}
                mask=word(pending,cursor+4);cursor+=8;continue;
            }
            if(command==0x30||command==0x31) { // STROW / STCOL, four following words.
                if(pending.size()-cursor<20){need=cursor+20;break;}
                auto& destination=command==0x30?row:column;
                for(unsigned n=0;n!=4;++n)destination[n]=word(pending,cursor+4+n*4);
                cursor+=20;continue;
            }
            if(command==0x4a) { // MPG: NUM following 64-bit microinstructions.
                const std::size_t instructions=(code>>16)&0xff ? (code>>16)&0xff : 256;
                const std::size_t destination=immediate;
                if(destination>vu_micro_mem.size() || instructions>vu_micro_mem.size()-destination)
                    throw std::runtime_error("VIF1 MPG exceeds VU1 microprogram memory");
                if(pending_byte_phase(cursor+4)%8)
                    throw std::runtime_error("VIF1 MPG payload is not 64-bit aligned");
                const std::size_t bytes=instructions*8;
                if(pending.size()-cursor<4+bytes){need=cursor+4+bytes;break;}
                ++host_micro_generation; // Host copies of MicroMem refresh on change.
                for(std::size_t n=0;n<instructions;++n) {
                    const auto data=cursor+4+n*8;
                    vu_micro_mem[destination+n]=std::uint64_t(word(pending,data))|
                                                 (std::uint64_t(word(pending,data+4))<<32);
                }
                cursor+=4+bytes;continue;
            }
            if(command>=0x60) { // UNPACK: documented scalar/vector forms.
                const auto format=command&0xf;
                const auto masked=(command&0x10)!=0;
                const bool rgba5=format==0xf;
                const unsigned bits=(format==0||format==0xc)?32:
                                    (format==1||format==0xd)?16:
                                    (format==2||format==0xe)?8:
                                    (format==4||format==8)?32:
                                    (format==5||format==9)?16:
                                    (format==6||format==10)?8:rgba5?16:0;
                if(!bits)throw std::runtime_error("unsupported VIF1 UNPACK format");
                const bool scalar=format<=2;
                const unsigned input_fields=scalar?1:(format>=4&&format<=6)?2:(format>=8&&format<=10)?3:4;
                const std::size_t vectors=(code>>16)&0xff ? (code>>16)&0xff : 256;
                const std::size_t destination=(immediate&0x8000?tops:0)+(immediate&0x3ff);
                auto cl=std::size_t(cycle&0xff),wl=std::size_t(cycle>>8);
                if(!cl)cl=256;if(!wl)wl=256;
                if(cl<wl&&!masked)throw std::runtime_error("VIF1 UNPACK fill-cycle requires an explicit mask");
                const auto input_vectors=cl>=wl?vectors:cl*(vectors/wl)+std::min(vectors%wl,cl);
                const auto unit_bytes=std::size_t(rgba5?2:input_fields*bits/8);
                const std::size_t bytes=(input_vectors*unit_bytes+3)&~std::size_t(3);
                if(pending.size()-cursor<4+bytes){need=cursor+4+bytes;break;}
                std::size_t data=cursor+4;
                if(!rgba5&&cl>=wl&&(!masked||!mask)&&!vif1_generic_unpack_only&&
                   try_unpack_linear(*this,format,pending.data()+data,vectors,destination,cl,wl,(immediate&(1u<<14))!=0)) {
                    cursor+=4+bytes;continue;
                }
                const auto extend=[&](std::uint32_t value) {
                    if(bits==32)return value;
                    if(immediate&(1u<<14))return value;
                    const auto sign=std::uint32_t(1)<<(bits-1);
                    return (value^sign)-sign;
                };
                for(std::size_t n=0;n<vectors;++n) {
                    const auto in_cycle=n%wl;
                    const auto has_input=cl>=wl||in_cycle<cl;
                    const auto vector=cl>=wl?destination+cl*(n/wl)+in_cycle:destination+n;
                    if(vector>=1024)throw std::runtime_error("VIF1 UNPACK exceeds VU1 data memory");
                    std::uint16_t packed=0;
                    if(has_input&&rgba5)packed=std::uint16_t(pending.at(data))|(std::uint16_t(pending.at(data+1))<<8);
                    for(unsigned field=0;field!=4;++field) {
                        const auto write_cycle=std::min<std::size_t>(in_cycle,3);
                        const auto selector=masked?unsigned((mask>>(2*(write_cycle*4+field)))&3):0;
                        if(selector==3)continue; // Documented masked write: retain VU memory.
                        // Every remaining path writes this lane or marks it undefined.
                        if(vif1_unpack_log)vif1_unpack_log->lane(vector,field,selector!=0||!(!has_input||(!scalar&&field>=input_fields)));
                        const auto defined_bit=std::uint8_t(1u<<field);
                        if(selector==1) {vu_mem[vector*4+field]=row[field];vu_mem_defined[vector]|=defined_bit;continue;}
                        if(selector==2) {vu_mem[vector*4+field]=column[write_cycle];vu_mem_defined[vector]|=defined_bit;continue;}
                        if(!has_input)
                            throw std::runtime_error("VIF1 UNPACK fill-cycle input selection without source remains unsupported");
                        if(!scalar&&field>=input_fields) {
                            // EE User's Manual: V2 leaves Z/W and V3 leaves W
                            // indeterminate. Preserve the backing bits but make
                            // their semantic undefinedness explicit for VU use.
                            vu_mem_defined[vector]&=std::uint8_t(~defined_bit);
                            continue;
                        }
                        if(rgba5) {
                            const auto channel=field==0?(packed&31):field==1?((packed>>5)&31):field==2?((packed>>10)&31):((packed>>15)&1);
                            vu_mem[vector*4+field]=channel<<(field==3?7:3);vu_mem_defined[vector]|=defined_bit;continue;
                        }
                        const auto byte_offset=data+(scalar?0:field*(bits/8));
                        std::uint32_t value=0;
                        for(unsigned byte=0;byte<bits/8;++byte)value|=std::uint32_t(pending.at(byte_offset+byte))<<(byte*8);
                        value=extend(value);
                        if(mode) {
                            value+=row[field];
                            if(mode==2)row[field]=value;
                        }
                        vu_mem[vector*4+field]=value;
                        vu_mem_defined[vector]|=defined_bit;
                    }
                    if(has_input)data+=unit_bytes;
                }
                cursor+=4+bytes;continue;
            }
            if(command==0x50||command==0x51) { // DIRECT / DIRECTHL
                const std::size_t units=immediate?immediate:65536,bytes=units*16;
                if(pending_byte_phase(cursor+4)%16)throw std::runtime_error("VIF1 DIRECT payload is not GIF-qword aligned");
                if(pending.size()-cursor<4+bytes){need=cursor+4+bytes;break;}
                // In order, in blocks: submit_words equals per-qword submission.
                std::array<std::uint32_t,256*4> block;
                std::size_t data=cursor+4;
                for(std::size_t unit=0;unit!=units;) {
                    const auto count=std::min<std::size_t>(units-unit,256);
                    for(std::size_t n=0;n<count*4;++n,data+=4)block[n]=word(pending,data);
                    gif.submit_words(block.data(),count,gs);
                    unit+=count;
                }
                cursor+=4+bytes;continue;
            }
            if(command==0x14) { // MSCAL: checked AOT VU1 entry only.
                activate_mscal(immediate,gif,gs);cursor+=4;
                continue;
            }
            if(command==0x17) { // MSCNT: resume the checked AOT VU1 program at saved TPC.
                if(vif1_mscnt_barrier)vif1_mscnt_barrier(); // Host: TPC must be committed.
                activate_mscnt(gif,gs);cursor+=4;
                continue;
            }
            consume_state(command,immediate);cursor+=4;
        }
        if(cursor)consume_pending_prefix(cursor);
        pending_need=need-cursor;pending_checked=pending.size();
}
} // namespace hg
