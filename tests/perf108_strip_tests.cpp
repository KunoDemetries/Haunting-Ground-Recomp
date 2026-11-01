#include "hg/gs.hpp"
#include <cstdint>
#include <iostream>

#define CHECK(x) do { if (!(x)) { std::cerr << "Failed line " << __LINE__ << ": " << #x << '\n'; return 1; } } while (0)

static std::uint64_t xyz(std::uint32_t x,std::uint32_t y,std::uint32_t z) {
    return std::uint64_t(x)|(std::uint64_t(y)<<16)|(std::uint64_t(z)<<32);
}
static std::uint64_t xyzf(std::uint32_t x,std::uint32_t y,std::uint32_t z,std::uint32_t fog) {
    return std::uint64_t(x)|(std::uint64_t(y)<<16)|
           (std::uint64_t(z&0xffffffu)<<32)|(std::uint64_t(fog&0xffu)<<56);
}

int main() {
    hg::GsRegisterState strip;strip.write_ad(0,4);
    strip.write_ad(5,xyz(1,11,101));strip.write_ad(5,xyz(2,12,102));strip.write_ad(5,xyz(3,13,103));
    CHECK(strip.draws.size()==1&&strip.draws[0].primitive==4&&
          strip.draws[0].vertices[0].x==1&&strip.draws[0].vertices[1].x==2&&strip.draws[0].vertices[2].x==3);
    strip.write_ad(5,xyz(4,14,104));
    CHECK(strip.draws.size()==2&&strip.draws[1].vertices[0].x==2&&
          strip.draws[1].vertices[1].x==3&&strip.draws[1].vertices[2].x==4);
    CHECK(strip.vertex_queue.empty());

    hg::GsRegisterState adc;adc.write_ad(0,4);
    adc.write_ad(0xd,xyz(10,20,30));
    adc.write_ad(0xd,xyz(11,21,31));
    adc.write_ad(0xc,xyzf(12,22,32,0x5a));
    CHECK(adc.draws.empty()&&adc.vertex_queue.empty());
    adc.write_ad(5,xyz(13,23,33));
    CHECK(adc.draws.size()==1&&adc.draws[0].vertices[0].x==11&&
          adc.draws[0].vertices[1].x==12&&adc.draws[0].vertices[2].x==13);
    CHECK(adc.draws[0].vertices[1].fog==0x5a);

    adc.write_ad(0,4);
    adc.write_ad(5,xyz(20,30,40));adc.write_ad(5,xyz(21,31,41));
    CHECK(adc.draws.size()==1);
    adc.write_ad(5,xyz(22,32,42));
    CHECK(adc.draws.size()==2&&adc.draws[1].vertices[0].x==20&&
          adc.draws[1].vertices[1].x==21&&adc.draws[1].vertices[2].x==22);
    CHECK(adc.vertex_queue.empty());

    std::cout << "HG-PERF-108 focused strip cases passed\n";
    return 0;
}
