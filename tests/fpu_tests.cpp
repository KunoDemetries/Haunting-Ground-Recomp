#include "hg/runtime.hpp"
#include "hg/fpu_add4.hpp"
#include <iostream>
#if defined(_M_X64) || defined(__SSE2__)
#include <emmintrin.h>
#endif
#define CHECK(x) do { if (!(x)) { std::cerr << "Failed line " << __LINE__ << ": " << #x << '\n'; return 1; } } while (0)
int main() {
    {
        std::uint32_t random=0x7b261ae9;unsigned checked=0;
        const auto next=[&]{random^=random<<13;random^=random>>17;random^=random<<5;return random;};
        const std::array<std::uint32_t,4> mantissas{0,1,0x3fffff,0x7fffff};
        for(unsigned rounding=0;rounding<4;++rounding) {
#if defined(_M_X64) || defined(__SSE2__)
            const auto saved_csr=_mm_getcsr();_mm_setcsr((saved_csr&~0x603fu)|(rounding<<13));
#endif
            for(unsigned iteration=0;iteration<1000000;++iteration) {
                std::array<std::uint32_t,4> a{},result{};auto b=next();
                for(unsigned lane=0;lane<4;++lane)a[lane]=next();
                if(iteration<256*256*4) {
                    const unsigned ea=iteration&255,eb=(iteration>>8)&255,m=iteration>>16;
                    b=(b&0x80000000u)|(eb<<23)|mantissas[m];
                    for(unsigned lane=0;lane<4;++lane)a[lane]=(a[lane]&0x80000000u)|(ea<<23)|mantissas[lane];
                }
                unsigned under=99,over=99;
                if(!hg::try_fpu_product4(a,b,result,under,over))continue;
                unsigned expected_under=0,expected_over=0;
                for(unsigned lane=0;lane<4;++lane) {
                    const auto p=hg::Fpu::product(a[lane],b);CHECK(result[lane]==p.bits);++checked;
                    if(p.underflow)expected_under|=1u<<lane;if(p.overflow)expected_over|=1u<<lane;
                }
                CHECK(under==expected_under && over==expected_over);
            }
#if defined(_M_X64) || defined(__SSE2__)
            CHECK((_mm_getcsr()&0x3f)==0);_mm_setcsr(saved_csr);
#endif
        }
        std::cout<<checked<<" packed integer product comparisons passed (zero means ISA unavailable)\n";
    }
    {
        std::uint32_t random=0x2a6b1947;unsigned checked=0;
        const auto next=[&]{random^=random<<13;random^=random>>17;random^=random<<5;return random;};
        for(unsigned rounding=0;rounding<4;++rounding) {
#if defined(_M_X64) || defined(__SSE2__)
        const auto saved_csr=_mm_getcsr();
        _mm_setcsr((saved_csr&~0x603fu)|(rounding<<13));
#endif
        for(unsigned iteration=0;iteration<1000000;++iteration) {
            std::array<std::uint32_t,4> a{},b{},result{};
            for(unsigned lane=0;lane<4;++lane) {
                a[lane]=next();b[lane]=next();
                switch(iteration%8) {
                case 0:b[lane]=a[lane]^0x80000000u;break; // Exact cancellation.
                case 1:b[lane]=(a[lane]^0x80000000u)+1;break;
                case 2:a[lane]&=0x807fffff;b[lane]&=0x807fffff;break;
                case 3:a[lane]=(a[lane]&0x80ffffffu)|0x00800000u;b[lane]=(b[lane]&0x80ffffffu)|0x00800000u;break;
                case 4:a[lane]|=0x7f800000u;b[lane]|=0x7f800000u;break;
                case 5:b[lane]=(b[lane]&0x807fffffu)|(a[lane]&0x7f800000u);break;
                default:break;
                }
            }
            unsigned under=99,over=99;
            if(!hg::try_fpu_add4(a,b,result,under,over))break;
            for(unsigned lane=0;lane<4;++lane){
                hg::Fpu reference;const auto expected=reference.add(a[lane],b[lane]);
                CHECK(result[lane]==expected);
                CHECK(bool(under&(1u<<lane))==bool(reference.control&0x4000));
                CHECK(bool(over&(1u<<lane))==bool(reference.control&0x8000));++checked;
            }
        }
#if defined(_M_X64) || defined(__SSE2__)
        CHECK((_mm_getcsr()&0x3f)==0);
        _mm_setcsr(saved_csr);
#endif
        }
        std::cout<<checked<<" packed integer addition comparisons passed (zero means ISA unavailable)\n";
    }
    { // Combined host call must retain two separate exact guest operations.
        std::uint32_t random=0x61793a5bu;std::uint64_t checked=0;
        const auto next=[&](){random^=random<<13;random^=random>>17;random^=random<<5;return random;};
        for(unsigned rounding=0;rounding<4;++rounding){
#if defined(_M_X64) || defined(__SSE2__)
            const auto saved_csr=_mm_getcsr();_mm_setcsr((saved_csr&~0x603fu)|(rounding<<13));
#endif
            for(unsigned iteration=0;iteration<250000;++iteration){
                std::array<std::uint32_t,4> a{},acc{},result{};auto b=next();
                if(iteration%8==0)b&=0x807fffffu;
                for(unsigned lane=0;lane<4;++lane){a[lane]=next();acc[lane]=next();
                    if(iteration%8==1)a[lane]|=0x7f800000u;
                    if(iteration%8==2)a[lane]&=0x807fffffu;
                    if(iteration%8==3)acc[lane]=hg::Fpu::product(a[lane],b).bits^0x80000000u;
                }
                const auto original=acc;unsigned under=99,over=99;
                const bool alias=iteration&1;
                if(!hg::try_fpu_madd4(a,b,acc,alias?acc:result,under,over))break;
                for(unsigned lane=0;lane<4;++lane){
                    const auto product=hg::Fpu::product(a[lane],b);hg::Fpu reference;
                    const auto expected=reference.add(original[lane],product.bits);
                    CHECK((alias?acc:result)[lane]==expected);
                    CHECK(bool(under&(1u<<lane))==(product.underflow||bool(reference.control&0x4000)));
                    CHECK(bool(over&(1u<<lane))==(product.overflow||bool(reference.control&0x8000)));++checked;
                }
            }
#if defined(_M_X64) || defined(__SSE2__)
            CHECK((_mm_getcsr()&0x3f)==0);_mm_setcsr(saved_csr);
#endif
        }
        std::cout<<checked<<" combined exact MADD comparisons passed\n";
    }
    hg::Fpu f;
    CHECK(f.add(0, 0) == 0);
    CHECK(f.add(0x80000000, 0) == 0);
    CHECK(f.add(0x80000000, 0x80000000) == 0x80000000);
    CHECK(f.add(0x80000001, 0x80000100) == 0x80000000);
    CHECK(f.add(0x3f800000, 0x3f800000) == 0x40000000); // 1+1=2
    CHECK(f.add(0x3f800000, 0x3f000000) == 0x3fc00000); // 1+0.5=1.5
    CHECK(f.add(0x3f800000, 0xbf800000) == 0); // cancellation
    CHECK(f.add(0x3f800000, 0xbf000000) == 0x3f000000);
    CHECK(f.add(0xbf800000, 0x3f000000) == 0xbf000000);
    CHECK(f.add(0x7f800000, 0) == 0x7f800000); // exponent 255 is finite
    CHECK(f.add(0x7fffffff, 0x7fffffff) == 0x7fffffff);
    CHECK((f.control & 0x8010) == 0x8010);
    CHECK(f.add(0x00800001, 0x80800000) == 0); // positive underflow
    CHECK((f.control & 0xc018) == 0x4018); // O clears; SO stays
    CHECK(f.add(0x80800001, 0x00800000) == 0x80000000);
    f.write_control(~0u); CHECK(f.control == 0x0183c079);
    f.write_control(0); CHECK(f.control == 0x01000001);
    hg::State s; s.pc = 0x1234; s.fpr[1] = 0x3f800000; s.fpr[2] = 0x33800000;
    s.add_float(1, 2, 0, true);
    CHECK(s.fpu.acc == 0x3f800000); // No GRS bits: the aligned 2^-24 term is chopped.
    s.gpr[1] = {0x12345678, 0xabcdef01};
    CHECK(f.madd(0x42a00000, 0xbf000000) == 0xc2200000); // ACC is 0 after control-only tests: 80 * -0.5.
    f.acc=0xc1800000; CHECK(f.madd(0x42a00000,0xbf000000)==0xc2600000);
    CHECK(f.msub(0x42a00000,0xbf000000)==0x41c00000);
    s.fpr[3]=0x40000000;s.fpr[4]=0x40400000;s.fpu.acc=0x42c60000;
    s.mula_float(3,4);CHECK(s.fpu.acc==0x40c00000); // MULA replaces ACC with 2*3=6.
    s.madda_float(3,4);CHECK(s.fpu.acc==0x41400000); // MADDA accumulates 6+6=12.
    s.msuba_float(3,4);CHECK(s.fpu.acc==0x40c00000); // MSUBA accumulates 12-6=6.
    s.madda_float(3,4);CHECK(s.fpu.acc==0x41400000);
    s.fpr[5]=0xdeadbeef;s.msub_float(3,4,5);
    CHECK(s.fpr[5]==0x40c00000 && s.fpu.acc==0x41400000); // MSUB writes fd=12-6, preserving ACC.
    f.acc=0;CHECK(f.madd(0x3f800001,0x3f800001)==0x3f800002); // Product low bit is chopped.
    CHECK(f.multiply(0x3fc00000,0x3fc00000)==0x40100000); // Wide normalization: 1.5 * 1.5.
    CHECK(f.multiply(0x7fffffff,0x40000000)==0x7fffffff && (f.control&0x8010u)==0x8010u);
    CHECK(f.multiply(0x00800000,0x3f000000)==0 && (f.control&0x4008u)==0x4008u);
    CHECK(f.multiply(0x80000001,0x3f800000)==0x80000000); // Exponent-zero signed zero.
    CHECK(f.divide(0x42a00000,0x3f000000)==0x43200000);
    CHECK(f.divide(0x3f800000,0x40400000)==0x3eaaaaaa); // 1/3, chopped (nearest would end AB).
    CHECK(f.divide(0xbf800000,0x40400000)==0xbeaaaaaa); // Toward zero for negatives too.
    CHECK(f.divide(0x40000000,0x40400000)==0x3f2aaaaa); // Other normalization branch.
    CHECK(f.divide(0x7f800000,0x3f800000)==0x7f800000); // EE exponent 255 is finite.
    // HG-DIAG-126 oracle profile: PCSX2's VU0 add (exact sum, chop) vs the console's add.
    CHECK(f.add(0xc57f9101,0x3b800200)==0xc57f90f1 && f.add_exact_chop(0xc57f9101,0x3b800200)==0xc57f90f0);
    CHECK(f.add(0xc680a61c,0x4a000000)==0x49fdfd68 && f.add_exact_chop(0xc680a61c,0x4a000000)==0x49fdfd67);
    CHECK(f.add_exact_chop(0x3f800000,0xbf800000)==0 && f.add_exact_chop(0x3f800000,0x33800000)==0x3f800000);
    CHECK(f.add_exact_chop(0x3f800000,0xa0000000)==0x3f7fffff); // tiny borrow crosses the binade.
    // COP1 DIV.S rounds to nearest (closest available model; docs/SOURCES.md 2026-10-25).
    CHECK(f.divide(0x3f800000,0x40400000,true)==0x3eaaaaab);
    CHECK(f.divide(0x40000000,0x40400000,true)==0x3f2aaaab);
    CHECK(f.divide(0x3f800000,0x3fa00000,true)==0x3f4ccccd); // 1/1.25, as on the console and PCSX2.
    CHECK(f.divide(0x3f800000,0x3f000000,true)==0x40000000); // exact quotients stay exact.
    f.write_control(0);
    CHECK(f.divide(0x3f800000,0x80000001)==0xffffffff && (f.control&0x3c078)==0x10020);
    CHECK(f.divide(0x80000000,0x80000100)==0x7fffffff && (f.control&0x3c078)==0x20060);
    CHECK(f.divide(0x80000001,0x3f800000)==0x80000000 && (f.control&0x3c078)==0x60);
    CHECK(f.divide(0x80800000,0x40000000)==0x80000000 && (f.control&0x3c078)==0x4068);
    CHECK(f.divide(0x7fffffff,0x3f000000)==0x7fffffff && (f.control&0x3c078)==0x8078);
    CHECK(f.divide(0x3f800000,0x3f800000)==0x3f800000 && (f.control&0x3c078)==0x78);
    CHECK(f.sqrt_exact(0x41100000)==0x40400000); // sqrt(9) = 3.
    CHECK(f.sqrt_exact(0x40800000)==0x40000000); // odd unbiased exponent: sqrt(4) = 2.
    CHECK(f.sqrt_exact(0x80000001)==0); // exponent-zero input: +0 (hardware sqrt(-0) = +0, ps2autotests).
    // COP1 add keeps one guard bit: results captured on a real PS2 (tools/hwtest, 2026-10-24).
    CHECK(f.add(0x3c80938cu,0xb97fd4feu)==0x3c7d27c5u);
    CHECK(f.add(0xc172efb2u,0x35ffffffu)==0xc172efb0u);
    CHECK(f.add(0x386fbcb4u,0xac0631e5u)==0x386fbcb3u);
    CHECK(f.add(0xbec710d7u,0x3a7ffffbu)==0xbec690d7u);
    CHECK(f.add(0x38800080u,0xb1006b41u)==0x387ffeffu);
    CHECK(f.sqrt_exact(0x40000000)==0x3fb504f3); // Measured original startup input2.
    CHECK(f.sqrt_exact(0x3f000000)==0x3f3504f3); // Same significand, negative odd exponent.
    f.write_control(0x3c078);CHECK(f.sqrt_exact(0x40000000)==0x3fb504f3);
    CHECK(f.control==0x100c079); // Clear I/D causes; retain U/O and sticky flags.
    CHECK(f.sqrt_exact(0x40a00000)==0x400f1bbd); // Round upward, unlike sqrt(2).
    CHECK(f.sqrt_exact(0x3f800001)==0x3f800000);
    CHECK(f.sqrt_exact(0x00800000)==0x20000000);
    CHECK(f.sqrt_exact(0x7f7fffff)==0x5f7fffff);
    // EE Core User's Manual Table 8-4: negative -> sqrt(|x|) with I and SI set.
    f.write_control(0x8010);CHECK(f.sqrt_exact(0xc1100000)==0x40400000); // sqrt(-9) = 3.
    CHECK((f.control&0x30040u)==0x20040u && (f.control&0x8010u)==0x8010u);
    CHECK(f.sqrt_exact(0x40800000)==0x40000000 && (f.control&0x30000u)==0 && (f.control&0x40u)); // I clears, SI sticks.
    CHECK(f.sqrt_exact(0x7f800000)==0x5f800000); // Exponent 255 is a normal value on the EE.
    CHECK(f.cvt_s_w_exact(123)==0x42f60000 && f.cvt_s_w_exact(0xffffff85)==0xc2f60000);
    bool convert_fault=false;try {f.cvt_s_w_exact(0x01000001);}catch(const std::runtime_error&){convert_fault=true;}CHECK(convert_fault);
    CHECK(f.cvt_w_s(0x40200000)==2 && f.cvt_w_s(0xc0200000)==0xfffffffe && f.cvt_w_s(0x4f000000)==0x7fffffff);
    f.compare(0x80000000,0,false);CHECK((f.control&0x00800000u)!=0);
    f.compare(0xc0000000,0x3f800000,true);CHECK((f.control&0x00800000u)!=0);
    f.compare(0x3f800000,0xc0000000,true);CHECK((f.control&0x00800000u)==0);
    // Clamp boundaries, both signs, equal zeros and unchanged exception flags.
    const auto flags=f.control&~0x00800000u;
    for(const auto pair: {std::array<std::uint32_t,3>{0x3f800000,0x3f800000,1},
                         {0x3f000000,0x3f800000,1},{0x40000000,0x3f800000,0},
                         {0xc0000000,0xbf800000,1},{0xbf800000,0xc0000000,0},
                         {0x80000000,0,1},{0,0x80000000,1}}) {
        f.compare(pair[0],pair[1],true,true);
        CHECK(bool(f.control&0x00800000u)==bool(pair[2]));
        CHECK((f.control&~0x00800000u)==flags);
    }
    s.store_quad(0x10f, 1); s.load_quad(0x107, 2);
    CHECK(s.gpr[2].lo == 0x12345678 && s.gpr[2].hi == 0xabcdef01);
    s.store_quad(0x101, 0); CHECK(s.load(0x100, 8, false) == 0);
    return 0;
}
