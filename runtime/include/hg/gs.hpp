#pragma once

#include "hg/gif.hpp"
#include "hg/gs_memory.hpp"
#include <array>
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace hg {

struct GsVertex {
    std::uint32_t x=0,y=0,z=0,rgba=0,fog=0;
    std::uint64_t st=0;
    std::uint32_t uv=0;
    std::uint32_t q=0x3f800000;
};
struct GsDraw {
    std::uint8_t primitive=0;
    std::uint16_t prim_state=0;
    std::uint64_t xyoffset=0;
    std::array<GsVertex,3> vertices{};
    std::uint8_t count=0;
    using Environment=std::array<std::uint64_t,0x80>;
    // Runtime draws use the ordered state-event cursor.  The nullable owned
    // snapshot is retained for isolated callers/tests that construct a draw.
    std::shared_ptr<Environment> explicit_environment;
    std::uint32_t environment_event_count=(std::numeric_limits<std::uint32_t>::max)();
    bool environment_captured=false;
};
struct GsDrawEnvironmentEvent {
    std::uint64_t value=0;
    std::uint8_t address=0;
};
struct GsTextureImage {
    std::uint32_t width=0,height=0;
    std::vector<std::uint32_t> rgba;
};
struct GsDisplayImage {
    std::uint32_t width=0,height=0,dx=0,dy=0,magnify_x=1,magnify_y=1;
    std::vector<std::uint32_t> rgba;
};

// Resolve an integer texel coordinate using one CLAMP axis.  This is the
// documented point-sampling coordinate operation; filtering remains a later
// renderer step.
std::uint32_t gs_wrap_texture_coordinate(std::int32_t coordinate,std::uint32_t size,
                                         std::uint32_t mode,std::uint32_t minimum,
                                         std::uint32_t maximum);

// Resolve one normalized STQ coordinate to an integer texel without host
// floating-point. IEEE single-precision operands are converted to an exact
// mantissa/exponent ratio; non-finite, zero-Q, and int32 overflow are explicit.
inline std::int32_t gs_stq_texel_coordinate(std::uint32_t coordinate,std::uint32_t q,std::uint32_t size) {
    if(!size||size>1024)throw std::runtime_error("invalid GS STQ texture dimension");
    const auto decode=[](std::uint32_t bits) {
        const auto exponent=(bits>>23)&0xffu,fraction=bits&0x7fffffu;
        if(exponent==0xff)throw std::runtime_error("non-finite GS STQ coordinate");
        struct Part {bool negative,zero;std::uint32_t mantissa;int scale;};
        if(!exponent)return Part{(bits>>31)!=0,fraction==0,fraction,-149};
        return Part{(bits>>31)!=0,false,fraction|0x800000u,int(exponent)-150};
    };
    const auto s=decode(coordinate),divisor=decode(q);
    if(divisor.zero)throw std::runtime_error("GS STQ division by zero");
    if(s.zero)return 0;
    auto numerator=std::uint64_t(s.mantissa)*size,denominator=std::uint64_t(divisor.mantissa);
    const auto shift=s.scale-divisor.scale;
    if(shift>=0) {
        if(shift>=64||numerator>(~std::uint64_t(0)>>shift))throw std::runtime_error("GS STQ coordinate overflow");
        numerator<<=shift;
    } else {
        const auto right=-shift;
        if(right>=64||denominator>(~std::uint64_t(0)>>right))return s.negative!=divisor.negative?-1:0;
        denominator<<=right;
    }
    const auto quotient=numerator/denominator,remainder=numerator%denominator;
    const bool negative=s.negative!=divisor.negative;
    const auto magnitude=quotient+(negative&&remainder?1:0);
    if((!negative&&magnitude>0x7fffffffull)||(negative&&magnitude>0x80000000ull))
        throw std::runtime_error("GS STQ coordinate outside signed texel range");
    return negative?std::int32_t(-std::int64_t(magnitude)):std::int32_t(magnitude);
}

// Resolve a barycentrically weighted normalized coordinate when the vertices
// share Q. The weight denominator cancels against the subsequent Q division,
// so the complete result remains an exact integer ratio. A zero third weight
// gives the two-endpoint linear case used by sprites.
inline std::int32_t gs_stq_weighted_texel_coordinate(std::uint32_t first,std::uint32_t second,
                                                    std::uint32_t q,std::int64_t first_weight,
                                                    std::int64_t second_weight,std::uint32_t size,
                                                    std::uint32_t third=0,std::int64_t third_weight=0) {
    if(!size||size>1024||(!first_weight&&!second_weight&&!third_weight))
        throw std::runtime_error("invalid GS weighted STQ interpolation");
    struct Part {bool negative,zero;std::uint32_t mantissa;int scale;};
    const auto decode=[](std::uint32_t bits) {
        const auto exponent=(bits>>23)&0xffu,fraction=bits&0x7fffffu;
        if(exponent==0xff)throw std::runtime_error("non-finite GS STQ coordinate");
        if(!exponent)return Part{(bits>>31)!=0,fraction==0,fraction,-149};
        return Part{(bits>>31)!=0,false,fraction|0x800000u,int(exponent)-150};
    };
    const auto a=decode(first),b=decode(second),c=decode(third),divisor=decode(q);
    if(divisor.zero)throw std::runtime_error("GS STQ division by zero");
    if(a.zero&&b.zero&&c.zero)return 0;
    auto common_scale=a.zero?b.zero?c.scale:b.scale:a.scale;
    if(!b.zero&&b.scale<common_scale)common_scale=b.scale;
    if(!c.zero&&c.scale<common_scale)common_scale=c.scale;
    std::int64_t sum=0;
    const auto accumulate=[&](Part part,std::int64_t weight) {
        if(part.zero||!weight)return;
        if(weight==(std::numeric_limits<std::int64_t>::min)())
            throw std::runtime_error("GS STQ interpolation overflow");
        const auto weight_negative=weight<0;
        const auto weight_magnitude=std::uint64_t(weight_negative?-weight:weight);
        if(weight_magnitude>std::uint64_t((std::numeric_limits<std::int64_t>::max)())/part.mantissa)
            throw std::runtime_error("GS STQ interpolation overflow");
        auto magnitude=weight_magnitude*part.mantissa;
        const auto shift=unsigned(part.scale-common_scale);
        if(shift>=63||magnitude>(std::uint64_t((std::numeric_limits<std::int64_t>::max)())>>shift))
            throw std::runtime_error("GS STQ interpolation exponent spread requires validation");
        magnitude<<=shift;
        const auto term=std::int64_t(magnitude);
        if(part.negative!=weight_negative) {
            if(sum<(std::numeric_limits<std::int64_t>::min)()+term)
                throw std::runtime_error("GS STQ interpolation overflow");
            sum-=term;
        } else {
            if(sum>(std::numeric_limits<std::int64_t>::max)()-term)
                throw std::runtime_error("GS STQ interpolation overflow");
            sum+=term;
        }
    };
    accumulate(a,first_weight);accumulate(b,second_weight);accumulate(c,third_weight);
    if(!sum)return 0;
    if(sum==(std::numeric_limits<std::int64_t>::min)())
        throw std::runtime_error("GS STQ interpolation overflow");
    const bool sum_negative=sum<0;
    auto numerator=std::uint64_t(sum_negative?-sum:sum);
    if((second_weight>0&&first_weight>(std::numeric_limits<std::int64_t>::max)()-second_weight)||
       (second_weight<0&&first_weight<(std::numeric_limits<std::int64_t>::min)()-second_weight))
        throw std::runtime_error("GS STQ interpolation denominator overflow");
    const auto pair_weight=first_weight+second_weight;
    if((third_weight>0&&pair_weight>(std::numeric_limits<std::int64_t>::max)()-third_weight)||
       (third_weight<0&&pair_weight<(std::numeric_limits<std::int64_t>::min)()-third_weight))
        throw std::runtime_error("GS STQ interpolation denominator overflow");
    const auto signed_weight_sum=pair_weight+third_weight;
    if(signed_weight_sum<=0)throw std::runtime_error("invalid GS STQ interpolation weight sum");
    const auto weight_sum=std::uint64_t(signed_weight_sum);
    if(weight_sum>~std::uint64_t(0)/divisor.mantissa)
        throw std::runtime_error("GS STQ interpolation denominator overflow");
    auto denominator=weight_sum*divisor.mantissa;
    if(numerator>~std::uint64_t(0)/size)throw std::runtime_error("GS STQ coordinate overflow");
    numerator*=size;
    const auto shift=common_scale-divisor.scale;
    if(shift>=0) {
        if(shift>=64||numerator>(~std::uint64_t(0)>>shift))throw std::runtime_error("GS STQ coordinate overflow");
        numerator<<=shift;
    } else {
        const auto right=-shift;
        if(right>=64||denominator>(~std::uint64_t(0)>>right))return sum_negative!=divisor.negative?-1:0;
        denominator<<=right;
    }
    const auto quotient=numerator/denominator,remainder=numerator%denominator;
    const bool negative=sum_negative!=divisor.negative;
    const auto magnitude=quotient+(negative&&remainder?1:0);
    if((!negative&&magnitude>0x7fffffffull)||(negative&&magnitude>0x80000000ull))
        throw std::runtime_error("GS STQ coordinate outside signed texel range");
    return negative?std::int32_t(-std::int64_t(magnitude)):std::int32_t(magnitude);
}

// Perspective form for two- or three-vertex primitives. Linear/barycentric
// normalization cancels between numerator and denominator:
//     (sum(w*S)/sum(w)) / (sum(w*Q)/sum(w)) == sum(w*S)/sum(w*Q).
// Both weighted sums are retained as exact signed mantissa/exponent integers.
std::int32_t gs_stq_perspective_texel_coordinate(const std::array<std::uint32_t,3>& coordinates,
                                                  const std::array<std::uint32_t,3>& qs,
                                                  const std::array<std::int64_t,3>& weights,
                                                  std::uint32_t size,unsigned fractional_bits=0);

// GS texture-function stage (TEX0.TFX/TCC), operating on the already expanded
// 8-bit RGBA texture color and the interpolated fragment color.  The GS uses
// a 0x80 unity multiplier, hence (a*b)>>7 rather than host normalized math.
inline std::uint32_t gs_texture_function(std::uint32_t texel,std::uint32_t fragment,
                                         std::uint32_t function,bool rgba) {
    if(function>3)throw std::runtime_error("invalid GS texture function");
    const auto byte=[](std::uint32_t value,unsigned shift) {return (value>>shift)&0xff;};
    // GS User's Manual 6.0 section3.4.9: texture products saturate to8 bits.
    const auto multiply=[](std::uint32_t a,std::uint32_t b) {
        const auto product=(a*b)>>7;
        return product>0xff?0xff:product;
    };
    const auto clamp=[](std::uint32_t value) {return value>0xff?0xff:value;};
    const auto ta=byte(texel,24),fa=byte(fragment,24);
    std::uint32_t result=0;
    for(unsigned shift=0;shift!=24;shift+=8) {
        const auto product=multiply(byte(texel,shift),byte(fragment,shift));
        const auto color=function==1?byte(texel,shift):
                         function>=2?clamp(product+fa):product;
        result|=color<<shift;
    }
    const auto alpha=!rgba?fa:
        function==0?multiply(ta,fa):function==1||function==3?ta:clamp(ta+fa);
    return result|(alpha<<24);
}

struct GsBlendResult { std::int32_t r=0,g=0,b=0; std::uint8_t a=0; };

// ALPHA_1/2 calculates RGB only.  Keep its potentially out-of-range result
// intact: GS applies RGB clamping later, while writing the frame buffer.
inline GsBlendResult gs_alpha_blend(std::uint32_t source,std::uint32_t destination,
                                    std::uint64_t alpha) {
    const auto byte=[](std::uint32_t value,unsigned shift) {return std::uint32_t((value>>shift)&0xff);};
    const auto source_alpha=byte(source,24),destination_alpha=byte(destination,24);
    const auto select_color=[&](std::uint32_t selector,unsigned shift) {
        if(selector==0)return std::int32_t(byte(source,shift));
        if(selector==1)return std::int32_t(byte(destination,shift));
        if(selector==2)return std::int32_t(0);
        throw std::runtime_error("reserved GS ALPHA color selector");
    };
    const auto select_alpha=[&](std::uint32_t selector) {
        if(selector==0)return std::int32_t(source_alpha);
        if(selector==1)return std::int32_t(destination_alpha);
        if(selector==2)return std::int32_t((alpha>>32)&0xff);
        throw std::runtime_error("reserved GS ALPHA alpha selector");
    };
    const auto multiply=[](std::int32_t color,std::int32_t amount) {
        const auto value=color*amount;
        return value>=0?value/128:-((-value+127)/128); // Explicit arithmetic >>7.
    };
    const auto blend_channel=[&](unsigned shift) {
        const auto a=select_color(std::uint32_t(alpha&3),shift);
        const auto b=select_color(std::uint32_t((alpha>>2)&3),shift);
        const auto c=select_alpha(std::uint32_t((alpha>>4)&3));
        const auto d=select_color(std::uint32_t((alpha>>6)&3),shift);
        return multiply(a-b,c)+d;
    };
    return {blend_channel(0),blend_channel(8),blend_channel(16),std::uint8_t(source_alpha)};
}

// FOGCOL blends each texture-function output RGB channel by the DDA fog
// coefficient F.  The GS defines both products as an unsigned >>8; alpha is
// passed through unchanged.
inline std::uint32_t gs_apply_fog(std::uint32_t source,std::uint8_t fog,std::uint32_t fogcol) {
    const auto channel=[&](unsigned shift) {
        const auto source_channel=(source>>shift)&0xff;
        const auto fog_channel=(fogcol>>shift)&0xff;
        return ((std::uint32_t(fog)*source_channel)>>8)+
               ((std::uint32_t(0xff-fog)*fog_channel)>>8);
    };
    return channel(0)|(channel(8)<<8)|(channel(16)<<16)|(source&0xff000000u);
}

inline bool gs_compare_test(std::uint32_t value,std::uint32_t reference,std::uint32_t mode) {
    switch(mode) {
    case 0: return false; // NEVER
    case 1: return true;  // ALWAYS
    case 2: return value<reference;
    case 3: return value<=reference;
    case 4: return value==reference;
    case 5: return value>=reference;
    case 6: return value>reference;
    case 7: return value!=reference;
    default: throw std::runtime_error("invalid GS comparison mode");
    }
}

inline bool gs_alpha_test(std::uint8_t alpha,std::uint64_t test) {
    return !(test&1) || gs_compare_test(alpha,std::uint32_t((test>>4)&0xff),std::uint32_t((test>>1)&7));
}

// destination_alpha is the destination's stored alpha bit: bit 7 for
// PSMCT32 and the sole alpha bit for PSMCT16. RGB24 always passes DATE.
inline bool gs_destination_alpha_test(bool destination_alpha,std::uint32_t frame_psm,std::uint64_t test) {
    if(!(test&(1ull<<14))||frame_psm==1)return true;
    if(frame_psm!=0&&frame_psm!=2&&frame_psm!=10)throw std::runtime_error("unsupported GS destination-alpha frame format");
    return destination_alpha==((test&(1ull<<15))!=0);
}

inline bool gs_depth_test(std::uint32_t source,std::uint32_t destination,std::uint64_t test) {
    if(!(test&(1ull<<16)))throw std::runtime_error("prohibited GS depth-test disablement");
    const auto mode=std::uint32_t((test>>17)&3);
    if(mode==0)return false; // NEVER
    if(mode==1)return true;  // ALWAYS
    return mode==2?source>=destination:source>destination;
}

// Minimal GS-side transport state.  This deliberately records raw A+D writes;
// register-specific rendering and VRAM effects remain separate implementation
// work and cannot be inferred from a packet parser.
struct GsRegisterState {
    std::uint64_t privileged_imr=0x1f00; // GS reset state: all five event interrupts masked.
    std::uint64_t privileged_pmode=0,privileged_smode2=0,privileged_bgcolor=0;
    std::array<std::uint64_t,2> privileged_dispfb{},privileged_display{};
    std::uint64_t privileged_busdir=0;
    std::uint32_t privileged_csr_events=0,privileged_signal_id=0,privileged_label_id=0;
    bool crtc_configured=false;
    std::uint32_t crtc_interlace=0,crtc_mode=0,crtc_frame=0;
    std::array<std::uint64_t, 0x80> value{};
    std::array<bool, 0x80> written{};
    std::uint32_t gif_q=0x3f800000; // GIF's documented per-tag initial Q = 1.0f.
    std::vector<GsVertex> vertex_queue;
    std::vector<GsDraw> draws;
    // Two heap-backed snapshots keep the large register arrays out of every
    // GsRegisterState stack frame while preserving ordinary deep-copy value semantics.
    std::vector<GsDraw::Environment> draw_environment_storage;
    std::vector<GsDrawEnvironmentEvent> draw_environment_events;
    bool draw_environment_active=false;
    std::size_t raster_environment_event_count=0,raster_environment_draw_index=0;
    bool raster_environment_active=false;

    GsDraw::Environment& draw_environment_base() {return draw_environment_storage.at(0);}
    const GsDraw::Environment& draw_environment_base() const {return draw_environment_storage.at(0);}
    GsDraw::Environment& raster_environment() {return draw_environment_storage.at(1);}
    const GsDraw::Environment& raster_environment() const {return draw_environment_storage.at(1);}
    // Cumulative counts since GS reset; draws[0] follows retired_draw_count.
    // Completed history may be retired, but pending records must stay ordered.
    std::uint64_t rasterized_draw_count=0,retired_draw_count=0;
    // HG-DIAG-005: disabled by default; no guest state or timing substitutions.
    bool profile_raster=false;
    std::array<std::uint64_t,7> raster_calls{},raster_nanoseconds{};
    static constexpr std::size_t max_draws=65536;
    // GS local memory is 4 MiB / one million 32-bit words. PSMCT32's physical
    // page/block/column layout is kept here, rather than pretending transfers
    // use a linear host framebuffer.
    GsLocalMemory vram;
    // GS User's Manual 3.4.7: one shared 1 KiB CLUT temporary buffer.
    // PSMCT32 pairs halfword n with n+256; PSMCT16/16S use one halfword.
    std::array<std::uint16_t,512> clut{};
    std::array<bool,512> clut_valid{};
    std::array<std::uint32_t,2> clut_cbp{};
    std::array<bool,2> clut_cbp_valid{};
    std::uint64_t clut_load_count=0;
    bool gif_to_vram_active=false;
    bool host_transfer_completed=false;
    unsigned completed_transfer_format=0;
    std::uint32_t transfer_x=0, transfer_y=0, transfer_width=0, transfer_remaining=0;

    void record_draw_environment_write(std::uint8_t address,std::uint64_t data) {
        if(draw_environment_active)draw_environment_events.push_back({data,address});
    }

    GsDraw::Environment materialize_draw_environment(const GsDraw& draw) const {
        if(draw.explicit_environment)return *draw.explicit_environment;
        if(draw.environment_event_count==(std::numeric_limits<std::uint32_t>::max)())return value;
        if(draw.environment_event_count>draw_environment_events.size())
            throw std::runtime_error("invalid GS draw environment cursor");
        auto result=draw_environment_base();
        for(std::size_t n=0;n<draw.environment_event_count;++n) {
            const auto& event=draw_environment_events[n];result[event.address]=event.value;
        }
        return result;
    }

    void write_ad(std::uint8_t address, std::uint64_t data) {
        if (address >= value.size()) throw std::runtime_error("unsupported GS A+D register address");
        if(address==0x60&&(privileged_csr_events&1))
            throw std::runtime_error("second GS SIGNAL requires pending-event stall handling");
        if(address==0x61&&gif_to_vram_active)
            throw std::runtime_error("GS FINISH during incomplete host-to-local transfer");
        if(address==6||address==7)load_clut_from_tex0(data);
        if(address==0x16||address==0x17) {
            constexpr auto mask=(std::uint64_t{0x3f}<<20)|(~std::uint64_t{0}<<37);
            load_clut_from_tex0((value[6+(address-0x16)]&~mask)|(data&mask));
        }
        value[address] = data;
        record_draw_environment_write(address,data);
        written[address] = true;
        if(address==0x60) {
            const auto mask=std::uint32_t(data>>32),id=std::uint32_t(data);
            privileged_signal_id=(privileged_signal_id&~mask)|(id&mask);privileged_csr_events|=1;
        }
        if(address==0x61) {rasterize_pending_draws();privileged_csr_events|=2;}
        if(address==0x62) {
            const auto mask=std::uint32_t(data>>32),id=std::uint32_t(data);
            privileged_label_id=(privileged_label_id&~mask)|(id&mask);
        }
        if(address==0x16||address==0x17) { // TEX2_1/2 update their TEX0 subset.
            constexpr auto tex2_mask=(std::uint64_t{0x3f}<<20)|(~std::uint64_t{0}<<37);
            const auto tex0=std::uint8_t(6+(address-0x16));
            value[tex0]=(value[tex0]&~tex2_mask)|(data&tex2_mask);
            record_draw_environment_write(tex0,value[tex0]);
            written[tex0]=true;
        }
        if(address==0) vertex_queue.clear(); // PRIM initializes the documented queue.
        if(address==4) kick_vertex(data,true,true);   // XYZF2
        if(address==5) kick_vertex(data,false,true);  // XYZ2
        if(address==0xc) kick_vertex(data,true,false); // XYZF3
        if(address==0xd) kick_vertex(data,false,false);// XYZ3
        if (address == 0x53) begin_transfer(data);
    }

    std::size_t pending_draw_index() const {
        if(rasterized_draw_count<retired_draw_count ||
           rasterized_draw_count-retired_draw_count>draws.size())
            throw std::runtime_error("invalid GS draw-history accounting");
        return std::size_t(rasterized_draw_count-retired_draw_count);
    }

    std::size_t retire_rasterized_draws() {
        const auto completed=pending_draw_index();
        draws.erase(draws.begin(),draws.begin()+completed);
        retired_draw_count=rasterized_draw_count;
        std::uint32_t first=(std::numeric_limits<std::uint32_t>::max)();
        for(const auto& draw:draws)if(!draw.explicit_environment&&
            draw.environment_event_count!=(std::numeric_limits<std::uint32_t>::max)())
            first=std::min(first,draw.environment_event_count);
        if(first==(std::numeric_limits<std::uint32_t>::max)()) {
            draw_environment_events.clear();draw_environment_storage.clear();
            draw_environment_active=false;raster_environment_active=false;
        } else if(first) {
            if(first>draw_environment_events.size())throw std::runtime_error("invalid retained GS draw environment cursor");
            for(std::size_t n=0;n<first;++n) {
                const auto& event=draw_environment_events[n];draw_environment_base()[event.address]=event.value;
            }
            draw_environment_events.erase(draw_environment_events.begin(),draw_environment_events.begin()+first);
            for(auto& draw:draws)if(!draw.explicit_environment&&
                draw.environment_event_count!=(std::numeric_limits<std::uint32_t>::max)())draw.environment_event_count-=first;
            raster_environment()=draw_environment_base();raster_environment_event_count=0;
            raster_environment_draw_index=0;raster_environment_active=true;
        } else {
            raster_environment()=draw_environment_base();raster_environment_event_count=0;
            raster_environment_draw_index=0;raster_environment_active=true;
        }
        return completed;
    }

    void emit_draw(std::uint8_t primitive,const GsVertex& a,const GsVertex& b,const GsVertex& c,unsigned count) {
        if(draws.size()>=max_draws)retire_rasterized_draws();
        if(draws.size()>=max_draws)throw std::runtime_error("GS primitive queue exceeds bounded capacity");
        // PRIM always supplies PRIM[2:0]. PRMODECONT.AC selects whether the
        // remaining drawing attributes come from PRIM or PRMODE.
        const auto prim_controls=!written[0x1a]||(value[0x1a]&1)!=0;
        const auto attributes=prim_controls?value[0]:value[0x1b];
        const auto prim_state=std::uint16_t((value[0]&7)|(attributes&0x7f8));
        const auto context=unsigned((prim_state>>9)&1);
        GsDraw draw{};
        draw.primitive=primitive;draw.prim_state=prim_state;draw.xyoffset=value[0x18+context];
        draw.vertices={a,b,c};draw.count=std::uint8_t(count);
        if(!draw_environment_active) {
            draw_environment_storage.resize(2);draw_environment_base()=value;raster_environment()=value;
            draw_environment_events.clear();draw_environment_active=true;
            raster_environment_event_count=0;raster_environment_draw_index=0;raster_environment_active=true;
        }
        if(draw_environment_events.size()>(std::numeric_limits<std::uint32_t>::max)())
            throw std::runtime_error("GS draw environment stream exceeds bounded capacity");
        draw.environment_event_count=std::uint32_t(draw_environment_events.size());draw.environment_captured=true;
        draws.push_back(std::move(draw));
    }
    void kick_vertex(std::uint64_t data,bool has_fog,bool draw) {
        const auto primitive=std::uint8_t(value[0]&7);
        if(primitive==7)throw std::runtime_error("prohibited GS primitive type");
        GsVertex vertex{};
        vertex.x=std::uint32_t(data)&0xffff;vertex.y=std::uint32_t(data>>16)&0xffff;
        vertex.z=has_fog?std::uint32_t(data>>32)&0xffffff:std::uint32_t(data>>32);
        vertex.rgba=std::uint32_t(value[1]);vertex.st=value[2];vertex.uv=std::uint32_t(value[3]);
        vertex.q=std::uint32_t(value[1]>>32);
        vertex.fog=has_fog?std::uint32_t(data>>56)&0xff:std::uint32_t(value[0xa])&0xff;
        vertex_queue.push_back(vertex);
        if(!draw)return;
        switch(primitive) {
        case 0: emit_draw(primitive,vertex,{}, {},1);vertex_queue.clear();return;
        case 1: case 6:
            if(vertex_queue.size()>=2) {emit_draw(primitive,vertex_queue[0],vertex_queue[1],{},2);vertex_queue.clear();}return;
        case 2:
            if(vertex_queue.size()>=2) {emit_draw(primitive,vertex_queue[vertex_queue.size()-2],vertex_queue.back(),{},2);vertex_queue.erase(vertex_queue.begin(),vertex_queue.end()-1);}return;
        case 3:
            if(vertex_queue.size()>=3) {emit_draw(primitive,vertex_queue[0],vertex_queue[1],vertex_queue[2],3);vertex_queue.clear();}return;
        case 4:
            if(vertex_queue.size()>=3) {emit_draw(primitive,vertex_queue[vertex_queue.size()-3],vertex_queue[vertex_queue.size()-2],vertex_queue.back(),3);vertex_queue.erase(vertex_queue.begin(),vertex_queue.end()-2);}return;
        case 5:
            if(vertex_queue.size()>=3) {emit_draw(primitive,vertex_queue[0],vertex_queue[vertex_queue.size()-2],vertex_queue.back(),3);const auto first=vertex_queue.front(),last=vertex_queue.back();vertex_queue={first,last};}return;
        default:throw std::runtime_error("prohibited GS primitive type");
        }
    }

    void write_packed(std::uint8_t descriptor, std::uint64_t low, std::uint64_t high);

    // REGLIST carries one 64-bit register datum per descriptor. Unlike PACKED,
    // it has no separate address byte, so only descriptors with an unambiguous
    // GS register destination are accepted here. Vertex descriptors must not
    // accidentally turn into state-only writes: their write is also a draw kick.
    void write_reglist(std::uint8_t descriptor, std::uint64_t data);

    void ensure_vram() {
        if (vram.empty()) vram.resize(1024 * 1024);
        if (vram.size() != 1024 * 1024) throw std::runtime_error("invalid GS local-memory allocation");
    }

    static bool privileged_contains(std::uint32_t address) {
        return address>=0x12000000u&&address<0x14000000u;
    }
    std::uint64_t read_privileged(std::uint32_t address) const {
        if(!privileged_contains(address)||address%16)
            throw std::runtime_error("invalid GS privileged register address");
        const auto reg=(address&0x1000u)|(address&0x3f0u);
        if(reg==0x1000)return privileged_csr_events|(1ull<<14);
        if(reg==0x1080)return privileged_signal_id|(std::uint64_t(privileged_label_id)<<32);
        throw std::runtime_error("unimplemented GS privileged register read");
    }
    bool interrupt_pending() const {
        return (privileged_csr_events&(~std::uint32_t(privileged_imr>>8))&0x1f)!=0;
    }
    void reset_system() {
        // Restore the native initial-state profile. The consulted manual does not
        // enumerate every register default. Local memory contents are retained.
        GsRegisterState initial;
        initial.vram=std::move(vram);
        *this=std::move(initial);
    }
    void write_privileged(std::uint32_t address,std::uint64_t data) {
        if(!privileged_contains(address)||address%16)
            throw std::runtime_error("invalid GS privileged register address");
        // EE address bits 12 and 9:4 form the GS's seven-bit privileged
        // register address; higher regions are documented mirrors.
        const auto reg=(address&0x1000u)|(address&0x3f0u);
        switch(reg) {
        case 0x000:privileged_pmode=data;return;
        case 0x020:privileged_smode2=data;return;
        case 0x070:privileged_dispfb[0]=data;return;
        case 0x080:privileged_display[0]=data;return;
        case 0x090:privileged_dispfb[1]=data;return;
        case 0x0a0:privileged_display[1]=data;return;
        case 0x0e0:privileged_bgcolor=data;return;
        case 0x1000:
            if(data&(1ull<<8))throw std::runtime_error("unimplemented GS CSR FLUSH");
            if(data&(1ull<<9)){reset_system();return;}
            privileged_csr_events&=~(std::uint32_t(data)&0x1f);return;
        case 0x1010:privileged_imr=data;return;
        case 0x1040:
            if(data&~1ull)throw std::runtime_error("invalid GS BUSDIR value");
            privileged_busdir=data;return;
        default:throw std::runtime_error("unimplemented GS privileged register write");
        }
    }

    GsDisplayImage display_image() const;

    static std::uint32_t psmct32_word(std::uint32_t base_words, std::uint32_t buffer_width,
                                      std::uint32_t x, std::uint32_t y) {
        if (!buffer_width || buffer_width % 64) throw std::runtime_error("invalid PSMCT32 buffer width");
        // Factor our existing page/block/column formula into independent X/Y
        // offsets. These small compile-time tables avoid rebuilding the same
        // interleaved bits for every texture, depth and framebuffer access.
        static constexpr auto x_offsets=[] {
            std::array<std::uint16_t,64> offsets{};
            for(unsigned i=0;i<64;++i)
                offsets[i]=std::uint16_t((i&1)+(i&6)*2+(i&8)*8+(i&16)*16+(i&32)*32);
            return offsets;
        }();
        static constexpr auto y_offsets=[] {
            std::array<std::uint16_t,32> offsets{};
            for(unsigned i=0;i<32;++i)
                offsets[i]=std::uint16_t((i&1)*2+(i&6)*8+(i&8)*16+(i&16)*32);
            return offsets;
        }();
        x &= 2047; y &= 2047;
        const std::uint32_t page = (x / 64) + (y / 32) * (buffer_width / 64);
        return (base_words + page * 2048 + x_offsets[x&63] + y_offsets[y&31]) & 0xfffffu;
    }

    static std::uint32_t psmct16_word(std::uint32_t base_words, std::uint32_t buffer_width,
                                      std::uint32_t x, std::uint32_t y) {
        if (!buffer_width || buffer_width % 64) throw std::runtime_error("invalid PSMCT16 buffer width");
        x &= 2047; y &= 2047;
        const std::uint32_t page = (x / 64) + (y / 64) * (buffer_width / 64);
        const std::uint32_t block_x = (x / 16) & 3, block_y = (y / 8) & 7;
        const std::uint32_t block = (block_x & 1) * 2 + (block_x & 2) * 4 +
                                    (block_y & 1) + (block_y & 2) * 2 + (block_y & 4) * 4;
        const std::uint32_t column = (y & 7) / 2;
        const std::uint32_t pixel = (x & 1) + ((x & 6) * 2) + ((y & 1) * 2);
        return (base_words + page * 2048 + block * 64 + column * 16 + pixel) & 0xfffffu;
    }

    static std::uint32_t psmct16s_word(std::uint32_t base_words,std::uint32_t buffer_width,
                                       std::uint32_t x,std::uint32_t y) {
        if(!buffer_width||buffer_width%64)throw std::runtime_error("invalid PSMCT16S buffer width");
        x&=2047;y&=2047;
        const std::uint32_t page=(x/64)+(y/64)*(buffer_width/64);
        static constexpr std::uint8_t blocks[32]={
            0,2,16,18,1,3,17,19,8,10,24,26,9,11,25,27,
            4,6,20,22,5,7,21,23,12,14,28,30,13,15,29,31};
        const auto block=blocks[((y/8)&7)*4+((x/16)&3)];
        const auto column=(y&7)/2;
        const auto pixel=(x&1)+((x&6)*2)+((y&1)*2);
        return (base_words+page*2048+block*64+column*16+pixel)&0xfffffu;
    }

    static std::uint32_t psmz32_word(std::uint32_t base_words,std::uint32_t buffer_width,
                                     std::uint32_t x,std::uint32_t y) {
        if(!buffer_width||buffer_width%64)throw std::runtime_error("invalid PSMZ32 buffer width");
        x&=2047;y&=2047;
        const std::uint32_t page=(x/64)+(y/32)*(buffer_width/64);
        // GS Manual 8.3.1: Z32 uses a different block order from CT32 while
        // retaining its 8x8 block and 8x2 column pixel ordering.
        static constexpr std::uint8_t blocks[32]={
            24,25,28,29,8,9,12,13,26,27,30,31,10,11,14,15,
            16,17,20,21,0,1,4,5,18,19,22,23,2,3,6,7};
        const auto block=blocks[((y/8)&3)*8+((x/8)&7)];
        const auto column=(y&7)/2;
        const auto pixel=(x&1)+((x&6)*2)+((y&1)*2);
        return (base_words+page*2048+block*64+column*16+pixel)&0xfffffu;
    }

    static std::uint32_t psmz16_word(std::uint32_t base_words,std::uint32_t buffer_width,
                                     std::uint32_t x,std::uint32_t y) {
        if(!buffer_width||buffer_width%64)throw std::runtime_error("invalid PSMZ16 buffer width");
        x&=2047;y&=2047;
        const std::uint32_t page=(x/64)+(y/64)*(buffer_width/64);
        // GS Manual 8.3.2: Z16 changes the CT16 8x4 block order, while the
        // 16x2 column and its two 16-bit lanes retain their normal ordering.
        static constexpr std::uint8_t blocks[32]={
            24,26,16,18,25,27,17,19,28,30,20,22,29,31,21,23,
            8,10,0,2,9,11,1,3,12,14,4,6,13,15,5,7};
        const auto block=blocks[((y/8)&7)*4+((x/16)&3)];
        const auto column=(y&7)/2;
        const auto pixel=(x&1)+((x&6)*2)+((y&1)*2);
        return (base_words+page*2048+block*64+column*16+pixel)&0xfffffu;
    }

    static std::uint32_t psmz16s_word(std::uint32_t base_words,std::uint32_t buffer_width,
                                      std::uint32_t x,std::uint32_t y) {
        if(!buffer_width||buffer_width%64)throw std::runtime_error("invalid PSMZ16S buffer width");
        x&=2047;y&=2047;
        const std::uint32_t page=(x/64)+(y/64)*(buffer_width/64);
        static constexpr std::uint8_t blocks[32]={
            24,26,8,10,25,27,9,11,16,18,0,2,17,19,1,3,
            28,30,12,14,29,31,13,15,20,22,4,6,21,23,5,7};
        const auto block=blocks[((y/8)&7)*4+((x/16)&3)];
        const auto column=(y&7)/2;
        const auto pixel=(x&1)+((x&6)*2)+((y&1)*2);
        return (base_words+page*2048+block*64+column*16+pixel)&0xfffffu;
    }

    static std::uint32_t psmt8_word(std::uint32_t base_words, std::uint32_t buffer_width,
                                    std::uint32_t x, std::uint32_t y) {
        if (!buffer_width || buffer_width % 64) throw std::runtime_error("invalid PSMT8 buffer width");
        x &= 2047; y &= 2047;
        // BITBLTBUF uses 64-pixel units even for 128-pixel indexed pages.
        // Keep the whole-page row stride: the independent DBW=11 upload
        // probe observes five pages, not six or alternating half-pages.
        // See docs/ORACLE.md; TEX0's stricter alignment is checked separately.
        const std::uint32_t page = (x / 128) + (y / 64) * (buffer_width / 128);
        const std::uint32_t block_x = (x / 16) & 7, block_y = (y / 16) & 3;
        const std::uint32_t block = (block_x & 1) + ((block_x & 2) * 2) + ((block_x & 4) * 4) +
                                    ((block_y & 1) * 2) + ((block_y & 2) * 4);
        const std::uint32_t column = (y & 15) / 4;
        const std::uint32_t row = (y & 3) ^ ((column & 1) ? 2 : 0);
        const std::uint32_t pixel = ((x & 1) + ((x & 6) * 2) + ((row & 1) * 2)) ^ ((row & 2) * 4);
        return (base_words + page * 2048 + block * 64 + column * 16 + pixel) & 0xfffffu;
    }

    static std::uint32_t psmt4_word(std::uint32_t base_words, std::uint32_t buffer_width,
                                    std::uint32_t x, std::uint32_t y) {
        if (!buffer_width || buffer_width % 64) throw std::runtime_error("invalid PSMT4 buffer width");
        x &= 2047; y &= 2047;
        const std::uint32_t page = (x / 128) + (y / 128) * (buffer_width / 128);
        const std::uint32_t block_x = (x / 32) & 3, block_y = (y / 16) & 7;
        const std::uint32_t block = (block_x & 1) * 2 + (block_x & 2) * 4 +
                                    (block_y & 1) + (block_y & 2) * 2 + (block_y & 4) * 4;
        const std::uint32_t column = (y & 15) / 4;
        const std::uint32_t row = (y & 3) ^ ((column & 1) ? 2 : 0);
        const std::uint32_t pixel = ((x & 1) + ((x & 6) * 2) + ((row & 1) * 2)) ^ ((row & 2) * 4);
        return (base_words + page * 2048 + block * 64 + column * 16 + pixel) & 0xfffffu;
    }

    std::uint32_t pixel32(std::uint32_t base_words, std::uint32_t buffer_width,
                          std::uint32_t x, std::uint32_t y) const {
        if (vram.size() != 1024 * 1024) throw std::runtime_error("GS local memory is not initialized");
        return vram[psmct32_word(base_words, buffer_width, x, y)];
    }
    std::uint16_t pixel16(std::uint32_t base_words, std::uint32_t buffer_width,
                          std::uint32_t x, std::uint32_t y) const {
        if (vram.size() != 1024 * 1024) throw std::runtime_error("GS local memory is not initialized");
        const auto word = vram[psmct16_word(base_words, buffer_width, x, y)];
        return std::uint16_t(word >> ((x & 8) ? 16 : 0));
    }
    std::uint16_t pixel16s(std::uint32_t base_words,std::uint32_t buffer_width,
                           std::uint32_t x,std::uint32_t y) const {
        if(vram.size()!=1024*1024)throw std::runtime_error("GS local memory is not initialized");
        const auto word=vram[psmct16s_word(base_words,buffer_width,x,y)];
        return std::uint16_t(word>>((x&8)?16:0));
    }
    std::uint8_t pixel8(std::uint32_t base_words, std::uint32_t buffer_width,
                        std::uint32_t x, std::uint32_t y) const {
        if (vram.size() != 1024 * 1024) throw std::runtime_error("GS local memory is not initialized");
        const auto word = vram[psmt8_word(base_words, buffer_width, x, y)];
        const auto lane = ((y & 2) >> 1) | ((x & 8) >> 2);
        return std::uint8_t(word >> (lane * 8));
    }
    std::uint8_t pixel4(std::uint32_t base_words, std::uint32_t buffer_width,
                        std::uint32_t x, std::uint32_t y) const {
        if (vram.size() != 1024 * 1024) throw std::runtime_error("GS local memory is not initialized");
        const auto word = vram[psmt4_word(base_words, buffer_width, x, y)];
        const auto lane = ((y & 2) >> 1) | ((x & 24) >> 2);
        return std::uint8_t(word >> (lane * 4)) & 0xf;
    }

    static unsigned transfer_pixel_depth(unsigned format) {
        switch(format) {
        case 0: case 0x30:return 32;
        case 1: case 0x31:return 24;
        case 2: case 10: case 0x32: case 0x3a:return 16;
        case 0x13: case 0x1b:return 8;
        case 0x14: case 0x24: case 0x2c:return 4;
        default:throw std::runtime_error("unsupported GS transfer pixel format");
        }
    }
    static void validate_transfer_buffer_width(unsigned format,std::uint32_t width) {
        (void)transfer_pixel_depth(format);
        if(!width||width>2048||width%64)
            throw std::runtime_error("invalid GS transfer buffer width");
    }
    static unsigned transfer_x_alignment(unsigned format) {
        return format==0x13||format==0x1b?2u:
               format==0x14||format==0x24||format==0x2c?4u:1u;
    }
    std::uint32_t read_transfer_pixel(unsigned format,std::uint32_t base,std::uint32_t width,
                                      std::uint32_t x,std::uint32_t y) const {
        if(vram.size()!=1024*1024)throw std::runtime_error("GS local memory is not initialized");
        if(format==0||format==1)return vram[psmct32_word(base,width,x,y)]&(format==1?0xffffffu:0xffffffffu);
        if(format==0x30||format==0x31)return vram[psmz32_word(base,width,x,y)]&(format==0x31?0xffffffu:0xffffffffu);
        if(format==2||format==10||format==0x32||format==0x3a) {
            const auto word=(format==2?psmct16_word:format==10?psmct16s_word:
                             format==0x32?psmz16_word:psmz16s_word)(base,width,x,y);
            return std::uint16_t(vram[word]>>((x&8)?16:0));
        }
        if(format==0x13)return pixel8(base,width,x,y);
        if(format==0x14)return pixel4(base,width,x,y);
        const auto word=psmct32_word(base,width,x,y);
        if(format==0x1b)return vram[word]>>24;
        if(format==0x24)return (vram[word]>>24)&15;
        if(format==0x2c)return vram[word]>>28;
        throw std::runtime_error("unsupported GS transfer pixel format");
    }
    void write_transfer_pixel(unsigned format,std::uint32_t base,std::uint32_t width,
                              std::uint32_t x,std::uint32_t y,std::uint32_t pixel) {
        if(format==0||format==1||format==0x30||format==0x31) {
            const auto word=(format<0x30?psmct32_word:psmz32_word)(base,width,x,y);
            const auto mask=(format==1||format==0x31)?0xff000000u:0u;
            vram[word]=(vram[word]&mask)|(pixel&~mask);return;
        }
        if(format==2||format==10||format==0x32||format==0x3a) {
            const auto word=(format==2?psmct16_word:format==10?psmct16s_word:
                             format==0x32?psmz16_word:psmz16s_word)(base,width,x,y);
            const auto shift=(x&8)?16:0;
            vram[word]=(vram[word]&~(0xffffu<<shift))|((pixel&0xffffu)<<shift);return;
        }
        if(format==0x13) {
            auto& word=vram[psmt8_word(base,width,x,y)];const auto lane=((y&2)>>1)|((x&8)>>2);
            word=(word&~(0xffu<<(lane*8)))|((pixel&0xffu)<<(lane*8));return;
        }
        if(format==0x14) {
            auto& word=vram[psmt4_word(base,width,x,y)];const auto lane=((y&2)>>1)|((x&24)>>2);
            word=(word&~(0xfu<<(lane*4)))|((pixel&15u)<<(lane*4));return;
        }
        auto& word=vram[psmct32_word(base,width,x,y)];
        if(format==0x1b)word=(word&0xffffffu)|((pixel&0xffu)<<24);
        else if(format==0x24)word=(word&0xf0ffffffu)|((pixel&15u)<<24);
        else if(format==0x2c)word=(word&0x0fffffffu)|((pixel&15u)<<28);
        else throw std::runtime_error("unsupported GS transfer pixel format");
    }

    void read_clut_source(std::array<std::uint32_t,256>& source,unsigned count,unsigned cpsm,
                          std::uint32_t cbp,std::uint32_t width,bool csm,bool four,
                          std::uint32_t start_x,std::uint32_t start_y) const;

    // TEX0/TEX2 accesses load a palette from VRAM into the shared temporary
    // buffer. CSA selects the destination, never the source VRAM coordinates.
    // See GS User's Manual 2.7, 3.4.7 and TEX0/TEX2 register descriptions.
    void load_clut_from_tex0(std::uint64_t tex0) {
        const auto psm=unsigned((tex0>>20)&0x3f);
        const bool four=psm==0x14||psm==0x24||psm==0x2c;
        if(!four&&psm!=0x13&&psm!=0x1b)return;
        const auto cld=unsigned(tex0>>61);
        if(!cld)return;
        if(cld>5)throw std::runtime_error("reserved GS CLUT load control");
        const auto cpsm=unsigned((tex0>>51)&15),csm=unsigned((tex0>>55)&1);
        const auto csa=unsigned((tex0>>56)&31),cbp=std::uint32_t((tex0>>37)&0x3fff);
        if(cpsm!=0&&cpsm!=2&&cpsm!=10)throw std::runtime_error("unsupported GS CLUT pixel format");
        if(csm&&(cpsm==0||csa))throw std::runtime_error("CSM2 requires a 16-bit CLUT and CSA=0");
        const auto count=four?16u:256u,offset=csa*16,capacity=cpsm==0?256u:512u;
        if(offset+count>capacity)throw std::runtime_error("GS CLUT load exceeds temporary-buffer capacity");
        if(cld>=4) {
            if(csm)throw std::runtime_error("conditional GS CLUT load requires CSM1");
            const auto slot=cld-4;
            if(!clut_cbp_valid[slot])throw std::runtime_error("GS CLUT compare register has not been initialized");
            if(clut_cbp[slot]==cbp)return;
        }
        std::uint32_t width=64,start_x=0,start_y=0;
        if(csm) {
            const auto texclut=value[0x1c];
            width=std::uint32_t(texclut&63)*64;
            start_x=std::uint32_t((texclut>>6)&63)*16;
            start_y=std::uint32_t((texclut>>12)&1023);
            if(!width||start_x+count>width)throw std::runtime_error("CSM2 CLUT load exceeds source row");
        }
        if(gif_to_vram_active)throw std::runtime_error("GS CLUT load during incomplete host-to-local transfer");
        // Older draws must see their old palette, and may themselves write the
        // new palette's VRAM source. Flush before both source reads and mutation.
        rasterize_pending_draws();
        ensure_vram();
        std::array<std::uint32_t,256> source{};
        read_clut_source(source,count,cpsm,cbp,width,csm,four,start_x,start_y);
        for(unsigned index=0;index<count;++index) {
            const auto entry=offset+index;
            clut[entry]=std::uint16_t(source[index]);clut_valid[entry]=true;
            if(cpsm==0) {clut[entry+256]=std::uint16_t(source[index]>>16);clut_valid[entry+256]=true;}
        }
        if(cld>=2) {
            const auto slot=(cld==2||cld==4)?0u:1u;
            clut_cbp[slot]=cbp;clut_cbp_valid[slot]=true;
        }
        ++clut_load_count;
    }

    // Direct-color texels read VRAM; indexed texels read the palette retained
    // by the most recent applicable TEX0/TEX2 load, with live TEXA expansion.
    std::uint32_t texel_from_tex0(unsigned context,std::uint32_t x,std::uint32_t y) const;
    GsTextureImage texture_from_tex0(unsigned context) const {
        if(context>1)throw std::runtime_error("invalid GS texture context");
        const auto tex0=value[6+context];
        const auto tw=unsigned((tex0>>26)&0xf),th=unsigned((tex0>>30)&0xf);
        if(tw>10||th>10)throw std::runtime_error("invalid TEX0 dimensions");
        const std::uint32_t width=1u<<tw,height=1u<<th;
        GsTextureImage image{width,height,std::vector<std::uint32_t>(std::size_t(width)*height)};
        for(std::uint32_t y=0;y<height;++y)for(std::uint32_t x=0;x<width;++x)
            image.rgba[std::size_t(y)*width+x]=texel_from_tex0(context,x,y);
        return image;
    }

    // Point-sample TEX0 after applying the context's CLAMP_1/2 fields.  The
    // single-texel decoder shares format/CLUT/TEXA checks with extraction.
    // Read live VRAM on every sample, including render-to-texture feedback.
    std::uint32_t point_sample_tex0(unsigned context,std::int32_t u,std::int32_t v) const {
        if(context>1)throw std::runtime_error("invalid GS texture context");
        const auto tex0=value[6+context];
        const auto tw=unsigned((tex0>>26)&0xf),th=unsigned((tex0>>30)&0xf);
        if(tw>10||th>10)throw std::runtime_error("invalid TEX0 dimensions");
        const auto width=1u<<tw,height=1u<<th;
        const auto clamp=value[8+context];
        const auto x=gs_wrap_texture_coordinate(u,width,std::uint32_t(clamp&3),
                                                std::uint32_t((clamp>>4)&0x3ff),std::uint32_t((clamp>>14)&0x3ff));
        const auto y=gs_wrap_texture_coordinate(v,height,std::uint32_t((clamp>>2)&3),
                                                std::uint32_t((clamp>>24)&0x3ff),std::uint32_t((clamp>>34)&0x3ff));
        return texel_from_tex0(context,x,y);
    }

    // Point-sample and execute TEX0's texture function against an interpolated
    // fragment color. This is deliberately a GS-side operation; host backends
    // receive its result only after their remaining pixel-pipeline work exists.
    std::uint32_t shade_point_tex0(unsigned context,std::int32_t u,std::int32_t v,
                                   std::uint32_t fragment) const {
        if(context>1)throw std::runtime_error("invalid GS texture context");
        const auto tex0=value[6+context];
        return gs_texture_function(point_sample_tex0(context,u,v),fragment,
                                   std::uint32_t((tex0>>35)&3),(tex0&(1ull<<34))!=0);
    }

    // Equal nearest/linear minification and magnification filters do not need
    // LOD selection. Other TEX1 modes remain explicit until their LOD path exists.
    bool linear_texture_filter(unsigned context) const;
    std::uint32_t shade_linear_tex0(unsigned context,std::int32_t u_fixed,std::int32_t v_fixed,
                                    std::uint32_t fragment) const;

    // Commit an already-tested/blended pixel through the checked FRAME formats.
    // Kept out of this header so new framebuffer-format work rebuilds hg_gs only.
    void write_frame_pixel(unsigned context,std::uint32_t x,std::uint32_t y,const GsBlendResult& color,
                           std::uint32_t additional_mask=0);

    std::uint32_t read_z_pixel(unsigned context,std::uint32_t x,std::uint32_t y) const {
        if(context>1)throw std::runtime_error("invalid GS Z-buffer context");
        const auto frame=value[0x4c+context],zbuf=value[0x4e + context];
        const auto width=std::uint32_t((frame>>16)&0x3f)*64;
        if(!width)throw std::runtime_error("invalid GS Z-buffer width");
        const auto psm=std::uint32_t((zbuf>>24)&0xf);
        if(psm!=0&&psm!=1&&psm!=2&&psm!=10)throw std::runtime_error("unimplemented GS Z-buffer pixel format");
        if(vram.size()!=1024*1024)throw std::runtime_error("GS local memory is not initialized");
        if(psm==2||psm==10) {
            const auto word=vram[(psm==2?psmz16_word:psmz16s_word)(std::uint32_t(zbuf&0x1ff)*2048,width,x,y)];
            return std::uint16_t(word>>((x&8)?16:0));
        }
        const auto stored=vram[psmz32_word(std::uint32_t(zbuf&0x1ff)*2048,width,x,y)];
        return psm==1?stored&0xffffffu:stored;
    }

    void write_z_pixel(unsigned context,std::uint32_t x,std::uint32_t y,std::uint32_t z) {
        if(context>1)throw std::runtime_error("invalid GS Z-buffer context");
        const auto frame=value[0x4c+context],zbuf=value[0x4e + context];
        const auto width=std::uint32_t((frame>>16)&0x3f)*64;
        if(!width)throw std::runtime_error("invalid GS Z-buffer width");
        const auto psm=std::uint32_t((zbuf>>24)&0xf);
        if(psm!=0&&psm!=1&&psm!=2&&psm!=10)throw std::runtime_error("unimplemented GS Z-buffer pixel format");
        if(zbuf&(1ull<<32))return;
        ensure_vram();
        if(psm==2||psm==10) {
            auto& word=vram[(psm==2?psmz16_word:psmz16s_word)(std::uint32_t(zbuf&0x1ff)*2048,width,x,y)];
            const auto shift=(x&8)?16:0;
            word=(word&~(0xffffu<<shift))|(std::uint32_t(std::uint16_t(z))<<shift);
            return;
        }
        auto& stored=vram[psmz32_word(std::uint32_t(zbuf&0x1ff)*2048,width,x,y)];
        if(psm==1)stored=(stored&0xff000000u)|(z&0xffffffu);else stored=z;
    }

    // Frame-buffer portion of the GS pixel path. Kept in runtime/gs.cpp so
    // framebuffer-format additions do not invalidate every translated shard.
    bool draw_frame_pixel(unsigned context,std::uint32_t x,std::uint32_t y,std::uint32_t source,
                          std::uint16_t primitive_state=0xffff);

    // Normal primitive pixel path through TEST, checked FRAME formats, and
    // PSMZ32/24/16 depth. The implementation lives with the frame writer.
    bool draw_depth_frame_pixel(unsigned context,std::uint32_t x,std::uint32_t y,
                                std::uint32_t source,std::uint32_t z,std::uint16_t primitive_state=0xffff);

    // Rasterize the GS point primitive through the checked normal pixel path.
    // The GS point rule selects the nearest window-coordinate pixel. Because a
    // point has one vertex, STQ resolves directly from that vertex without an
    // interpolation step; multi-vertex perspective interpolation stays separate.
    bool rasterize_point(const GsDraw& draw);

    // A sprite is an axis-aligned rectangle: its top/left edges are included
    // and bottom/right edges excluded. Its second vertex supplies Z. Keep its
    // FST sprites linearly interpolate their 10.4 UV endpoints along the two
    // rectangle axes. STQ sprites interpolate S/T/Q on those axes and resolve
    // the perspective ratio exactly without host floating point.
    bool rasterize_sprite(const GsDraw& draw);

    // The GS line rule is the half-open segment through the diamond assigned
    // to each pixel: the start is included and the endpoint is excluded.  This
    // implements the documented coverage geometry before the ordinary scissor
    // and pixel stages. Rational interval clipping avoids host floating point;
    // the dominant-axis DDA supplies every varying attribute.
    bool rasterize_line(const GsDraw& draw);

    // The GS triangle DDA includes a pixel center on top/left edges and
    // excludes it on bottom/right edges. Integer edge weights drive FST UV,
    // Gouraud RGBA, and fog interpolation from the same covered sample.
    // Varying depth and perspective STQ use these same exact weights.
    bool rasterize_triangle(const GsDraw& draw);

    bool rasterize_draw_current(const GsDraw& draw);

    bool rasterize_draw(const GsDraw& draw);

    std::size_t rasterize_pending_draws();

    void begin_transfer(std::uint64_t direction) {
        const auto xdir=unsigned(direction&3);
        host_transfer_completed=false;
        if(xdir==3) { // Transmission deactivated; an in-flight host upload ends.
            gif_to_vram_active=false;transfer_remaining=0;return;
        }
        if(xdir==1)throw std::runtime_error("unsupported GS local-to-host transfer");
        const auto bitblt = value[0x50], position = value[0x51], region = value[0x52];
        const auto format = (bitblt >> 56) & 0x3f;
        if (xdir==0 && format != 0 && format != 1 && format != 2 && format != 10 &&
            format != 0x30 && format != 0x31 && format != 0x32 && format != 0x3a &&
            format != 0x13 && format != 0x14 && format != 0x1b && format != 0x24 && format != 0x2c)
            throw std::runtime_error("unsupported GS destination pixel format");
        const auto width = std::uint32_t(region & 0xfff), height = std::uint32_t((region >> 32) & 0xfff);
        if (!width || !height) throw std::runtime_error("invalid GS transfer dimensions");
        if (std::uint64_t(width) * height > 1024ull * 1024ull)
            throw std::runtime_error("GS transfer exceeds local-memory capacity");
        if(xdir==0) {
            const auto destination_width=std::uint32_t((bitblt>>48)&0x3f)*64;
            const auto destination_x=std::uint32_t((position>>32)&0x7ff);
            validate_transfer_buffer_width(unsigned(format),destination_width);
            const auto depth=transfer_pixel_depth(unsigned(format));
            const auto width_alignment=depth>=32?2u:depth==24?8u:depth==16?4u:8u;
            if(width%width_alignment||destination_x%transfer_x_alignment(unsigned(format)))
                throw std::runtime_error("invalid GS host-to-local transfer alignment");
        }
        // Drawing and host-to-local transfers share GS local memory.  Commit
        // every earlier primitive before IMAGE payload can modify its frame,
        // depth, texture, or CLUT storage; later primitives remain deferred.
        rasterize_pending_draws();
        if(xdir==2) {
            const auto source_format=unsigned((bitblt>>24)&0x3f);
            const auto depth=transfer_pixel_depth(source_format);
            if(transfer_pixel_depth(unsigned(format))!=depth)
                throw std::runtime_error("GS local-to-local pixel depths differ");
            const auto source_width=std::uint32_t((bitblt>>16)&0x3f)*64;
            const auto destination_width=std::uint32_t((bitblt>>48)&0x3f)*64;
            validate_transfer_buffer_width(source_format,source_width);
            validate_transfer_buffer_width(unsigned(format),destination_width);
            const auto width_alignment=depth>=32?2u:depth==24?8u:depth==16?4u:8u;
            if(width%width_alignment)throw std::runtime_error("invalid GS local-to-local transfer width alignment");
            ensure_vram();
            const auto source_base=std::uint32_t(bitblt&0x3fff)*64;
            const auto destination_base=std::uint32_t((bitblt>>32)&0x3fff)*64;
            const auto source_x=std::uint32_t(position&0x7ff),source_y=std::uint32_t((position>>16)&0x7ff);
            const auto destination_x=std::uint32_t((position>>32)&0x7ff),destination_y=std::uint32_t((position>>48)&0x7ff);
            if(source_x%transfer_x_alignment(source_format)||
               destination_x%transfer_x_alignment(unsigned(format)))
                throw std::runtime_error("invalid GS local-to-local start X alignment");
            const auto order=unsigned((position>>59)&3);
            for(std::uint32_t row=0;row<height;++row)for(std::uint32_t column=0;column<width;++column) {
                const auto dx=(order&2)?width-1-column:column;
                const auto dy=(order&1)?height-1-row:row;
                const auto sx=(source_x+dx)&0x7ff,sy=(source_y+dy)&0x7ff;
                const auto tx=(destination_x+dx)&0x7ff,ty=(destination_y+dy)&0x7ff;
                const auto pixel=read_transfer_pixel(source_format,source_base,source_width,sx,sy);
                write_transfer_pixel(unsigned(format),destination_base,destination_width,tx,ty,pixel);
            }
            gif_to_vram_active=false;transfer_remaining=0;return;
        }
        transfer_x = std::uint32_t((position >> 32) & 0x7ff);
        transfer_y = std::uint32_t((position >> 48) & 0x7ff);
        transfer_width = width;
        transfer_remaining = width * height;
        ensure_vram();
        gif_to_vram_active = true;
    }

    void write_hwreg(std::uint64_t data) {
        if(completed_host_payload())return;
        if (!gif_to_vram_active) throw std::runtime_error("GS HWREG write without GIF-to-VRAM transfer");
        const auto bitblt = value[0x50];
        const auto base_words = std::uint32_t((bitblt >> 32) & 0x3fffu) * 64;
        const auto buffer_width = std::uint32_t((bitblt >> 48) & 0x3fu) * 64;
        const auto format = unsigned((bitblt >> 56) & 0x3f);
        if (format != 0 && format != 2 && format != 10 && format!=0x30 && format!=0x32 && format!=0x3a)
            throw std::runtime_error("GS HWREG format requires packed-image handling");
        const unsigned bits = transfer_pixel_depth(format);
        for (unsigned half = 0; half != 64 / bits; ++half) {
            // The last HWREG may contain padding beyond an odd-sized region.
            if (!transfer_remaining) return;
            const auto written_pixels = std::uint64_t(value[0x52] & 0xfff) *
                                        std::uint64_t((value[0x52] >> 32) & 0xfff) - transfer_remaining;
            const auto x = transfer_x + std::uint32_t(written_pixels % transfer_width);
            const auto y = transfer_y + std::uint32_t(written_pixels / transfer_width);
            write_transfer_pixel(format,base_words,buffer_width,x,y,std::uint32_t(data>>(half*bits)));
            if (--transfer_remaining == 0) {
                gif_to_vram_active=false;host_transfer_completed=true;completed_transfer_format=format;
            }
        }
    }

    bool completed_host_payload() const {
        // GS fixed-size rectangle, measured synthetic exact/double upload
        // profile (ORACLE.md). Never accept data before a valid completion or
        // after cancellation. Unmeasured pixel formats remain explicit faults.
        const auto format=unsigned((value[0x50]>>56)&0x3f);
        return host_transfer_completed && !gif_to_vram_active && !transfer_remaining &&
            format==completed_transfer_format && (format==0||format==2||format==0x13||format==0x14);
    }
    bool write_image_psmt8_fast(std::uint64_t low,std::uint64_t high);
    void write_image_transfers(const GifTransfer* transfers,std::size_t count);
    void write_image_qword(std::uint64_t low, std::uint64_t high) {
        if(completed_host_payload())return;
        const auto format = unsigned((value[0x50] >> 56) & 0x3f);
        if (format != 1 && format != 0x31 && format != 0x13 && format != 0x14 && format != 0x1b && format != 0x24 && format != 0x2c) {
            write_hwreg(low);
            if (gif_to_vram_active) write_hwreg(high);
            return;
        }
        if (!gif_to_vram_active) throw std::runtime_error("GS IMAGE data without GIF-to-VRAM transfer");
        if(format==0x13&&write_image_psmt8_fast(low,high))return;
        std::array<std::uint8_t, 16> bytes{};
        for (unsigned i = 0; i != 8; ++i) {
            bytes[i] = std::uint8_t(low >> (i * 8));
            bytes[i + 8] = std::uint8_t(high >> (i * 8));
        }
        const auto bitblt = value[0x50];
        const auto base_words = std::uint32_t((bitblt >> 32) & 0x3fffu) * 64;
        const auto buffer_width = std::uint32_t((bitblt >> 48) & 0x3fu) * 64;
        const unsigned count = (format == 1||format==0x31) ? 5 : (format == 0x13 || format == 0x1b) ? 16 : 32;
        for (unsigned pixel = 0; pixel != count && transfer_remaining; ++pixel) {
            const auto written_pixels = std::uint64_t(value[0x52] & 0xfff) *
                                        std::uint64_t((value[0x52] >> 32) & 0xfff) - transfer_remaining;
            const auto x = transfer_x + std::uint32_t(written_pixels % transfer_width);
            const auto y = transfer_y + std::uint32_t(written_pixels / transfer_width);
            if (format == 1||format==0x31) {
                const auto rgb = std::uint32_t(bytes[pixel * 3]) |
                                 (std::uint32_t(bytes[pixel * 3 + 1]) << 8) |
                                 (std::uint32_t(bytes[pixel * 3 + 2]) << 16);
                write_transfer_pixel(format,base_words,buffer_width,x,y,rgb);
            } else if (format == 0x13) {
                auto& word = vram[psmt8_word(base_words, buffer_width, x, y)];
                const auto lane = ((y & 2) >> 1) | ((x & 8) >> 2);
                word = (word & ~(0xffu << (lane * 8))) | (std::uint32_t(bytes[pixel]) << (lane * 8));
            } else if (format == 0x1b) {
                auto& word=vram[psmct32_word(base_words,buffer_width,x,y)];
                word=(word&0x00ffffffu)|(std::uint32_t(bytes[pixel])<<24);
            } else {
                const auto nibble = (bytes[pixel / 2] >> ((pixel & 1) * 4)) & 0xf;
                if(format==0x14) {
                    auto& word = vram[psmt4_word(base_words, buffer_width, x, y)];
                    const auto lane = ((y & 2) >> 1) | ((x & 24) >> 2);
                    word = (word & ~(0xfu << (lane * 4))) | (std::uint32_t(nibble) << (lane * 4));
                } else {
                    auto& word=vram[psmct32_word(base_words,buffer_width,x,y)];
                    const auto shift=format==0x24?24:28;
                    const auto mask=0xfu<<shift;
                    word=(word&~mask)|(std::uint32_t(nibble)<<shift);
                }
            }
            if (--transfer_remaining == 0) {
                gif_to_vram_active=false;host_transfer_completed=true;completed_transfer_format=format;
            }
        }
    }
};

// Commit only operations with defined register addressing at this stage.  A+D
// carries its GS address in bits 64-71 of a PACKED quadword. PRE carries a
// documented PRIM write. Other descriptors remain explicit boundaries until
// their vertex/register data formats are implemented.
inline void apply_gif_register_transfers(const GifPacket& packet, GsRegisterState& gs) {
    for (std::size_t index=0;index<packet.transfers.size();) {
        const auto& transfer=packet.transfers[index];
        if(transfer.reset_q)gs.gif_q=0x3f800000;
        if (transfer.kind == GifTransferKind::Prim) {
            gs.write_ad(0, transfer.low);
        } else if (transfer.kind == GifTransferKind::Packed) {
            gs.write_packed(transfer.descriptor, transfer.low, transfer.high);
        } else if (transfer.kind == GifTransferKind::Reglist) {
            gs.write_reglist(transfer.descriptor, transfer.low);
        } else if (transfer.kind == GifTransferKind::Image) {
            auto end=index+1;
            while(end<packet.transfers.size()&&packet.transfers[end].kind==GifTransferKind::Image&&
                  !packet.transfers[end].reset_q)++end;
            gs.write_image_transfers(packet.transfers.data()+index,end-index);
            index=end;continue;
        } else {
            throw std::runtime_error("GIF transfer requires unimplemented GS descriptor handling");
        }
        ++index;
    }
}

// A GIF path accepts DMA qwords without assuming packet alignment. Complete
// packets are decoded and committed in order; a bounded suffix remains queued.
// Host device-thread hand-off (device_thread.cpp): receives GIF qwords in order.
struct GifForward {
    virtual void forward_qword(std::uint64_t low,std::uint64_t high)=0;
    // Little-endian 32-bit words, four per qword.
    virtual void forward_words(const std::uint32_t* words,std::size_t qwords)=0;
protected:
    ~GifForward()=default;
};

struct GifPath {
    std::vector<std::uint8_t> pending;
    // When set, this path is a proxy on the VIF1/VU1 host thread: qwords go to
    // the GS thread's real GifPath and only packet boundaries are tracked here,
    // by the same rules as gif_packet_size, so packet_idle() equals pending.empty()
    // of the real path at the same point in the stream.
    GifForward* forward=nullptr;
    // The boundary tracker below also finds packet ends on the real path, so a
    // packet is walked once when complete instead of on every qword. After a
    // packet application faults, the path rescans `pending` as before until empty.
    bool forward_in_packet=false,forward_expect_tag=true,forward_eop=false,rescan_pending=false;
    std::uint64_t forward_remaining=0;
    bool packet_idle() const {return forward?!forward_in_packet:pending.empty();}
    void track_forward(std::uint64_t low) {
        // gif_packet_size rules: tags until an EOP tag's data are complete.
        if(forward_expect_tag) {
            const auto nloop=low&0x7fffu;const bool eop=(low&(1ull<<15))!=0;
            forward_in_packet=true;
            if(!nloop) {if(eop)forward_in_packet=false;}
            else {
                const auto format=unsigned((low>>58)&3u);
                const auto encoded_nreg=(low>>60)&0xfu,nreg=encoded_nreg?encoded_nreg:16;
                forward_remaining=format>=2?nloop:format==0?nloop*nreg:(nloop*nreg+1)/2;
                forward_expect_tag=false;forward_eop=eop;
            }
        } else if(--forward_remaining==0) {
            forward_expect_tag=true;
            if(forward_eop)forward_in_packet=false;
        }
    }
    std::uint32_t read_status(bool vif_path3_masked,bool reverse) const {
        // Complete packets are committed synchronously. A retained suffix is
        // not a hardware FIFO and has no independently modeled active path.
        if(!packet_idle())throw std::runtime_error("GIF STAT during incomplete packet requires path/FIFO modeling");
        return (vif_path3_masked?2u:0u)|(reverse?0x1000u:0u);
    }
    void write_control(std::uint32_t value) {
        if(value!=1)throw std::runtime_error("unsupported GIF CTRL stop/restart operation");
        // Reset the GIF transport/parser, not the receiving GS registers/VRAM.
        pending.clear();rescan_pending=false;
        forward_in_packet=false;forward_expect_tag=true;forward_eop=false;forward_remaining=0;
    }
    static constexpr std::size_t max_pending = 8 * 1024 * 1024 + 16;

    void submit_qword(std::uint64_t low, std::uint64_t high, GsRegisterState& gs);
    // Same as submit_qword for each qword in order (four little-endian words each).
    void submit_words(const std::uint32_t* words, std::size_t qwords, GsRegisterState& gs);
};

} // namespace hg
