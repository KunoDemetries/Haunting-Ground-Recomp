#include "hg/fpu_add4.hpp"
#if defined(_MSC_VER) && defined(_M_X64)
#include <intrin.h>
#endif
namespace hg {
void fpu_transform_avx2(const std::array<const std::uint32_t*,8>&,const std::array<std::uint32_t,4>&,VuTransformResult&);
bool fpu_madd4_er_impl(const std::uint32_t*,std::uint32_t,const std::uint32_t*,std::uint32_t*,unsigned&,unsigned&) noexcept;
void fpu_matrix4_avx2(const std::array<std::array<std::uint32_t,4>,4>&,const std::array<std::uint32_t,4>&,std::array<std::array<std::uint32_t,4>,4>&,std::array<unsigned,4>&,std::array<unsigned,4>&);
void fpu_add4_avx2(const std::uint32_t*,const std::uint32_t*,std::uint32_t*,unsigned&,unsigned&);
void fpu_product4_avx2(const std::uint32_t*,std::uint32_t,std::uint32_t*,unsigned&,unsigned&);
void fpu_madd4_avx2(const std::uint32_t*,std::uint32_t,const std::uint32_t*,std::uint32_t*,unsigned&,unsigned&);
namespace {
const bool available=[] {
#if defined(_MSC_VER) && defined(_M_X64)
    int info[4];__cpuid(info,0);if(info[0]<7)return false;
    __cpuidex(info,1,0);
    if((info[2]&0x18000000)!=0x18000000)return false; // AVX and OSXSAVE.
    if((_xgetbv(0)&6)!=6)return false;
    __cpuidex(info,7,0);return (info[1]&32)!=0;
#elif (defined(__GNUC__) || defined(__clang__)) && defined(__x86_64__)
    __builtin_cpu_init();return bool(__builtin_cpu_supports("avx2"));
#else
    return false;
#endif
}();
const bool er_available=[] {
    if(!available)return false;
#if defined(_MSC_VER) && defined(_M_X64)
    int info[4];__cpuidex(info,1,0);
    if((info[2]&0x18000000)!=0x18000000)return false;
    if((_xgetbv(0)&0xe6)!=0xe6)return false;
    __cpuidex(info,7,0);
    // /arch:AVX512 permits F, DQ, CD, BW, VL and associated AVX2/BMI operations.
    constexpr unsigned required=(1u<<16)|(1u<<17)|(1u<<28)|(1u<<30)|(1u<<31)|(1u<<5)|(1u<<3)|(1u<<8);
    return (unsigned(info[1])&required)==required;
#elif (defined(__GNUC__) || defined(__clang__)) && defined(__x86_64__)
    return __builtin_cpu_supports("avx512f")&&__builtin_cpu_supports("avx512dq")&&
           __builtin_cpu_supports("avx512bw")&&__builtin_cpu_supports("avx512vl")&&__builtin_cpu_supports("avx512cd");
#else
    return false;
#endif
}();
constexpr bool use_embedded_rounding_madd=false; // Retired: host IEEE adds do not match the measured hardware add.
}
bool try_fpu_transform(const std::array<const std::uint32_t*,8>& sources,const std::array<std::uint32_t,4>& point,VuTransformResult& result) {
    if(!available)return false;
    fpu_transform_avx2(sources,point,result);return true;
}
// The AVX-512 embedded-rounding MADD used host IEEE round-toward-zero adds, which the
// 2026-10-24/25 real-PS2 captures show differ from the hardware's one-guard-bit add;
// it is retired (always unavailable) rather than kept as a mismatching path.
bool fpu_madd4_er_available() noexcept {return false;}
bool try_fpu_madd4_er(const std::array<std::uint32_t,4>& a,std::uint32_t b,
                      const std::array<std::uint32_t,4>& acc,std::array<std::uint32_t,4>& result,
                      unsigned& under,unsigned& over) noexcept {
    (void)a;(void)b;(void)acc;(void)result;(void)under;(void)over;
    return false;
}
bool try_fpu_add4(const std::array<std::uint32_t,4>& a,const std::array<std::uint32_t,4>& b,
                  std::array<std::uint32_t,4>& result,unsigned& under,unsigned& over) {
    if(!available)return false;
    fpu_add4_avx2(a.data(),b.data(),result.data(),under,over);return true;
}
bool try_fpu_product4(const std::array<std::uint32_t,4>& a,std::uint32_t b,
                     std::array<std::uint32_t,4>& result,unsigned& under,unsigned& over) {
    if(!available)return false;
    fpu_product4_avx2(a.data(),b,result.data(),under,over);return true;
}
bool try_fpu_madd4(const std::array<std::uint32_t,4>& a,std::uint32_t b,const std::array<std::uint32_t,4>& acc,
                   std::array<std::uint32_t,4>& result,unsigned& under,unsigned& over) {
    if(!available)return false;
    if constexpr(use_embedded_rounding_madd)
        if(er_available&&fpu_madd4_er_impl(a.data(),b,acc.data(),result.data(),under,over))return true;
    fpu_madd4_avx2(a.data(),b,acc.data(),result.data(),under,over);return true;
}

bool try_fpu_matrix4(const std::array<std::array<std::uint32_t,4>,4>& sources,
                     const std::array<std::uint32_t,4>& scalars,
                     std::array<std::array<std::uint32_t,4>,4>& values,
                     std::array<unsigned,4>& under,std::array<unsigned,4>& over) {
    if(!available)return false;
    fpu_matrix4_avx2(sources,scalars,values,under,over);return true;
}
}
