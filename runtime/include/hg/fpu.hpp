#pragma once
#include <cstdint>
#include <stdexcept>
#include <utility>

// Small integer arithmetic helpers are hot in statically compiled VU programs.
// Preserve their exact operations while exposing constant operands to the host compiler.
#if defined(_MSC_VER)
#define HG_FPU_INLINE __forceinline
#elif defined(__GNUC__) || defined(__clang__)
#define HG_FPU_INLINE inline __attribute__((always_inline))
#else
#define HG_FPU_INLINE inline
#endif

namespace hg {
struct Fpu {
    std::uint32_t acc = 0;
    std::uint32_t control = 0x01000001;
    static constexpr std::uint32_t writable = 0x0083c078;
    void write_control(std::uint32_t value) { control = (value & writable) | 0x01000001; }
    // Floating add shared by the EE FPU and the VUs, measured on a real PS2
    // (tools/hwtest/fpu_capture.c and vu0_capture.c, 2026-10-24/25; docs/SOURCES.md):
    // exponent-zero inputs are signed zero, the smaller operand is aligned keeping exactly
    // ONE guard bit, the sum is normalized and chopped, overflow saturates to Fmax and
    // underflow flushes to signed zero. Reproduces every captured COP1 ADD/SUB (16,384
    // each), VU0 VADD/VSUB (16,384 each) and, with the hardware product, COP1 MADD and VU0
    // VMADD; zero or two guard bits do not. VU1 shares the VU0 FMAC design (inferred).
    std::uint32_t add(std::uint32_t a, std::uint32_t b) {
        int ea = int((a >> 23) & 255), eb = int((b >> 23) & 255);
        if (!ea) a &= 0x80000000u;
        if (!eb) b &= 0x80000000u;
        std::uint32_t result = 0;
        bool overflow = false, underflow = false;
        if (!ea && !eb) result = a & b;
        else if (!ea) result = b;
        else if (!eb) result = a;
        else {
            if ((a & 0x7fffffffu) < (b & 0x7fffffffu)) { std::swap(a, b); std::swap(ea, eb); }
            const auto ma = ((a & 0x7fffffu) | 0x800000u) << 1;   // one guard bit
            auto mb = ((b & 0x7fffffu) | 0x800000u) << 1;
            const unsigned delta = unsigned(ea - eb);
            mb = delta >= 25 ? 0 : mb >> delta;
            std::uint32_t m = ((a ^ b) & 0x80000000u) ? ma - mb : ma + mb;
            if (m) {
                if (m & 0x2000000u) { m >>= 1; ++ea; }
                while (m < 0x1000000u && ea > 0) { m <<= 1; --ea; }
                const auto sign = a & 0x80000000u;
                overflow = ea > 255;
                underflow = ea <= 0;
                result = overflow ? sign | 0x7fffffffu : underflow ? sign
                    : sign | (std::uint32_t(ea) << 23) | ((m >> 1) & 0x7fffffu);
            }
        }
        control &= ~0xc000u;
        if (overflow) control |= 0x8010u;
        if (underflow) control |= 0x4008u;
        return result;
    }
    // HG-DIAG-126 (opt-in oracle profile, not console behavior): the PCSX2 oracle's
    // VU0 add takes the exact sum and truncates it (16,384/16,384 PCSX2 VU0 results;
    // the console keeps one guard bit, see add()). Same exponent/flag rules as add().
    std::uint32_t add_exact_chop(std::uint32_t a, std::uint32_t b) {
        int ea = int((a >> 23) & 255), eb = int((b >> 23) & 255);
        if (!ea) a &= 0x80000000u;
        if (!eb) b &= 0x80000000u;
        std::uint32_t result = 0;
        bool overflow = false, underflow = false;
        if (!ea && !eb) result = a & b;
        else if (!ea) result = b;
        else if (!eb) result = a;
        else {
            if ((a & 0x7fffffffu) < (b & 0x7fffffffu)) { std::swap(a, b); std::swap(ea, eb); }
            // 30 extra bits: cancellation needs delta <= 1 (exact); beyond 30 the lost
            // bits only matter as a borrow when subtracting, kept as a sticky unit.
            const auto ma = std::uint64_t((a & 0x7fffffu) | 0x800000u) << 30;
            const auto wide = std::uint64_t((b & 0x7fffffu) | 0x800000u) << 30;
            const unsigned delta = unsigned(ea - eb);
            const auto mb = delta >= 60 ? 0 : wide >> delta;
            const bool sticky = delta >= 60 || (mb << delta) != wide;
            const bool sub = ((a ^ b) & 0x80000000u) != 0;
            const auto m = sub ? ma - mb - (sticky ? 1 : 0) : ma + mb;
            if (m) {
                int top = 63; while (!((m >> top) & 1)) --top;
                ea += top - 53;
                const auto sign = a & 0x80000000u;
                overflow = ea > 255;
                underflow = ea <= 0;
                result = overflow ? sign | 0x7fffffffu : underflow ? sign
                    : sign | (std::uint32_t(ea) << 23) | (std::uint32_t(m >> (top - 23)) & 0x7fffffu);
            }
        }
        control &= ~0xc000u;
        if (overflow) control |= 0x8010u;
        if (underflow) control |= 0x4008u;
        return result;
    }
    struct Product { std::uint32_t bits; bool overflow; bool underflow; };
    static HG_FPU_INLINE Product product(std::uint32_t a, std::uint32_t b) {
        // EE Core manual pp156,162,164-165: exponent-zero inputs are signed
        // zero, multiplication chops discarded significand bits, exponent
        // overflow saturates to Fmax, and underflow flushes to signed zero.
        const auto ea=(a>>23)&255u, eb=(b>>23)&255u;
        const auto sign=(a^b)&0x80000000u;
        if(!ea || !eb)return {sign,false,false};
        const auto product=std::uint64_t((a&0x7fffffu)|0x800000u)*((b&0x7fffffu)|0x800000u);
        const bool wide=(product&(1ull<<47))!=0;
        const unsigned shift=wide?24:23;
        const int exponent=int(ea)+int(eb)-127+(wide?1:0);
        if(exponent<=0)return {sign,false,true};
        if(exponent>255)return {sign|0x7fffffffu,true,false};
        return {sign|(std::uint32_t(exponent)<<23)|(std::uint32_t(product>>shift)&0x7fffffu),false,false};
    }
    std::uint32_t multiply(std::uint32_t a, std::uint32_t b) {
        const auto value=product(a,b);
        control&=~0xc000u;
        if(value.overflow)control|=0x8010u;
        if(value.underflow)control|=0x4008u;
        return value.bits;
    }
    std::uint32_t madd(std::uint32_t fs,std::uint32_t ft) {
        const auto value=product(fs,ft);
        control&=~0xc000u;
        if(value.underflow) { control|=0x8u; return acc; }
        if(value.overflow) { control|=0x8010u; return value.bits; }
        if(((acc>>23)&255u)==255) { control|=0x8010u; return acc; }
        return add(acc,value.bits);
    }
    std::uint32_t msub(std::uint32_t fs,std::uint32_t ft) {
        auto value=product(fs,ft);
        value.bits^=0x80000000u;
        control&=~0xc000u;
        if(value.underflow) { control|=0x8u; return acc; }
        if(value.overflow) { control|=0x8010u; return value.bits; }
        if(((acc>>23)&255u)==255) { control|=0x8010u; return acc; }
        return add(acc,value.bits);
    }
    std::uint32_t divide(std::uint32_t a,std::uint32_t b,bool nearest=false) {
        // EE Core pp156,158-165; DIV.S p357. Integer significand division
        // without host FP. The console's divider is neither chop nor nearest
        // (exact chop or +1, rule still being fitted; docs/SOURCES.md 2026-10-25).
        // COP1 DIV.S passes nearest: it matches 85% of 32,768 captured hardware
        // quotients (chop 60%) and equals the PCSX2 oracle's FPU division.
        // VU DIV keeps chop, as PCSX2 does for VU0/VU1.
        const auto ea=(a>>23)&255u, eb=(b>>23)&255u;
        const auto sign=(a^b)&0x80000000u;
        control&=~0x3c000u;
        if(!eb) {
            control|=ea?0x10020u:0x20040u;
            return sign|0x7fffffffu;
        }
        if(!ea)return sign;
        const auto ma=(a&0x7fffffu)|0x800000u, mb=(b&0x7fffffu)|0x800000u;
        const bool ge=ma>=mb;const unsigned shift=ge?23:24;
        const auto numerator=std::uint64_t(ma)<<shift;
        auto quotient=numerator/mb;
        // Rounding up never reaches 2^24: the largest quotient, 0xffffff, is exact.
        if(nearest&&2*(numerator%mb)>=mb)++quotient;
        const int exponent=int(ea)-int(eb)+127-(ge?0:1);
        if(exponent<=0) {control|=0x4008u;return sign;}
        if(exponent>255) {control|=0x8010u;return sign|0x7fffffffu;}
        return sign|(std::uint32_t(exponent)<<23)|(std::uint32_t(quotient)&0x7fffffu);
    }
    std::uint32_t sqrt_exact(std::uint32_t value) {
        // SQRT.S (EE instruction manual p376).  The EE treats exponent-zero
        // values as signed zero.  For normal positive values, compute only
        // the independently measured positive-normal oracle profile. A 2048-input
        // synthetic batch agrees with nearest rounding (docs/ORACLE.md). This is
        // reference-profile evidence, not a claim of physical EE rounding parity.
        const auto exponent=(value>>23)&255u;
        // Hardware-captured result (ps2autotests ee_fpu sqrt.expected): sqrt(-0) = +0.
        // Exponent-zero inputs are zero on the EE, so the whole class gives +0.
        if(!exponent)return 0;
        // EE Core User's Manual v6.0 Table 8-4 (p162): the square root of a
        // negative number is the square root of its absolute value with I=1
        // (sticky SI with it, as for 0/0 in Table 8-5). The EE has no Inf/NaN,
        // so exponent 255 is an ordinary normal and uses the same integer root.
        const bool negative=(value&0x80000000u)!=0;
        const auto mantissa=(value&0x7fffffu)|0x800000u;
        const bool odd_unbiased=((exponent-127u)&1u)!=0;
        const auto radicand=std::uint64_t(mantissa)<<(odd_unbiased?24:23);
        std::uint64_t root=0;
        for(std::uint64_t bit=1ull<<23;bit;bit>>=1) {
            const auto candidate=root|bit;
            if(candidate*candidate<=radicand)root=candidate;
        }
        // The midpoint squared is root^2 + root + 1/4. The integer radicand
        // cannot tie, so compare the integer remainder without host FP.
        if(radicand-root*root>root)++root;
        const int unbiased=int(exponent)-127;
        int output_exponent=127+(unbiased-(odd_unbiased?1:0))/2;
        if(root==0x1000000u) {root>>=1;++output_exponent;}
        control&=~0x30000u; // SQRT clears I/D cause bits, retains sticky and U/O.
        if(negative)control|=0x20040u;
        return (std::uint32_t(output_exponent)<<23)|(std::uint32_t(root)&0x7fffffu);
    }
    std::uint32_t cvt_s_w_exact(std::uint32_t value) {
        // CVT.S.W converts a signed 32-bit FPR word without consulting host FP.
        const auto signed_value=std::int64_t(std::int32_t(value));
        if(!signed_value)return 0;
        const auto sign=signed_value<0?0x80000000u:0u;
        const auto magnitude=std::uint64_t(signed_value<0?-signed_value:signed_value);
        unsigned top=0;for(auto probe=magnitude;probe>>=1;)++top;
        std::uint64_t mantissa;
        if(top<=23)mantissa=magnitude<<(23-top);
        else {
            const unsigned shift=top-23;
            if(magnitude&((1ull<<shift)-1))throw std::runtime_error("EE FPU CVT.S.W rounding requires validation");
            mantissa=magnitude>>shift;
        }
        return sign|((top+127u)<<23)|(std::uint32_t(mantissa)&0x7fffffu);
    }
    std::uint32_t cvt_w_s(std::uint32_t value) {
        // CVT.W.S (EE instruction manual p356): truncate toward zero, then
        // clamp only when the biased exponent exceeds 0x9d.
        const auto exponent=(value>>23)&255u;
        if(!exponent)return 0;
        if(exponent>0x9d)return value&0x80000000u?0x80000000u:0x7fffffffu;
        const auto mantissa=(value&0x7fffffu)|0x800000u;
        const auto magnitude=exponent>=150?std::uint64_t(mantissa)<<(exponent-150):mantissa>>(150-exponent);
        return value&0x80000000u?std::uint32_t(-std::int64_t(magnitude)):std::uint32_t(magnitude);
    }
    void compare(std::uint32_t a,std::uint32_t b,bool ordered_less,bool or_equal=false) {
        // Exact C.EQ.S/C.LT.S/C.LE.S comparisons, with equal signed zeros.
        // The EE has no NaN representation (instruction manual pp349/351/352).
        if(!(a&0x7fffffffu))a=0;if(!(b&0x7fffffffu))b=0;
        bool result;
        if(!ordered_less)result=a==b;
        else if((a^b)&0x80000000u)result=(a&0x80000000u)!=0;
        else if(a&0x80000000u)result=(a&0x7fffffffu)>(b&0x7fffffffu);
        else result=(a&0x7fffffffu)<(b&0x7fffffffu);
        if(or_equal && a==b)result=true;
        control=(control&~0x00800000u)|(result?0x00800000u:0);
    }
};
}
#undef HG_FPU_INLINE
