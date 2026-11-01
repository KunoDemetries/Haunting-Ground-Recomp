#include "hg/gs_math.hpp"
#include "hg/gs.hpp"
#include "hg/vif.hpp"
#include "hg/vu_xyz.hpp"
#include "hg/presentation.hpp"
#include <array>
#include <cstring>
#include <iostream>

namespace hg {extern bool vif1_generic_unpack_only;} // HG-DIAG-089 comparison switch
#define CHECK(x) do { if (!(x)) { std::cerr << "Failed line " << __LINE__ << ": " << #x << '\n'; return 1; } } while (0)

static void put64(std::uint8_t* bytes, std::size_t offset, std::uint64_t value) {
    for (unsigned i = 0; i != 8; ++i) bytes[offset + i] = std::uint8_t(value >> (i * 8));
}

static int test_clut_cache() {
    using G=hg::GsRegisterState;
    const auto tex=[](unsigned psm,unsigned cpsm,unsigned cbp,unsigned csa,unsigned cld) {
        return (2ull<<14)|(std::uint64_t(psm)<<20)|(4ull<<26)|
               (std::uint64_t(cbp)<<37)|(std::uint64_t(cpsm)<<51)|
               (std::uint64_t(csa)<<56)|(std::uint64_t(cld)<<61);
    };
    const auto palette=[](G& gs,unsigned base,std::uint32_t color) {
        for(unsigned i=0;i<16;++i)gs.write_transfer_pixel(0,base,64,i%8,i/8,color+i);
    };
    for(const unsigned psm:{0x14u,0x24u,0x2cu}) {
        G gs;gs.ensure_vram();
        for(unsigned i=0;i<16;++i)gs.write_transfer_pixel(psm,0,128,i,0,i);
        palette(gs,1024,0x80112200);
        gs.write_ad(6,tex(psm,0,16,9,1));
        for(unsigned i=0;i<16;++i)CHECK(gs.texel_from_tex0(0,i,0)==0x80112200+i);
        // CLD=0 retains a loaded bank even after its original VRAM is changed
        // and CBP now names unrelated storage. CSA is not a VRAM row offset.
        palette(gs,1024,0x80334400);
        gs.write_ad(6,tex(psm,0,64,9,0));
        CHECK(gs.texel_from_tex0(0,15,0)==0x8011220f && gs.clut_load_count==1);
        gs.write_ad(6,tex(psm,0,16,0,1));
        CHECK(gs.texel_from_tex0(0,15,0)==0x8033440f);
        gs.write_ad(6,tex(psm,0,0,9,0));
        CHECK(gs.texel_from_tex0(0,15,0)==0x8011220f);
        // Context 2's TEX2 load updates the same cache, including bank 15.
        palette(gs,4096,0x80556600);
        gs.write_ad(7,tex(psm,0,0,0,0));
        gs.write_ad(0x17,tex(psm,0,64,15,1));
        gs.write_ad(6,tex(psm,0,0,15,0));
        CHECK(gs.texel_from_tex0(0,15,0)==0x8055660f);
    }
    for(unsigned slot=0;slot<2;++slot) {
        G gs;gs.ensure_vram();palette(gs,1024,0x80112200);palette(gs,4096,0x80556600);
        gs.write_ad(6,tex(0x14,0,16,9,2+slot));
        palette(gs,1024,0x80778800);
        gs.write_ad(0x16,tex(0x14,0,16,9,4+slot));
        CHECK(gs.clut_load_count==1 && gs.texel_from_tex0(0,0,0)==0x80112200);
        gs.write_ad(0x16,tex(0x14,0,64,9,4+slot));
        CHECK(gs.clut_load_count==2 && gs.texel_from_tex0(0,0,0)==0x80556600);
        CHECK(gs.clut_cbp[slot]==64);
        palette(gs,4096,0x8099aa00);
        gs.write_ad(0x16,tex(0x14,0,64,9,4+slot));
        CHECK(gs.clut_load_count==2 && gs.texel_from_tex0(0,0,0)==0x80556600);
    }
    for(const unsigned cpsm:{2u,10u}) {
        G gs;gs.ensure_vram();
        gs.write_ad(0x3b,(0x12ull<<32)|0x34);
        gs.write_transfer_pixel(0x14,0,128,0,0,15);
        gs.write_transfer_pixel(cpsm,1024,64,7,1,0x801f);
        gs.write_ad(6,tex(0x14,cpsm,16,31,1));
        CHECK(gs.texel_from_tex0(0,0,0)==0x120000ff);
        gs.write_ad(0x3b,(0x56ull<<32)|0x78); // TEXA expansion happens at sample time.
        CHECK(gs.texel_from_tex0(0,0,0)==0x560000ff);
    }
    { // GS manual p56: the two 16-bit planes form each 32-bit cache entry.
        G gs;gs.ensure_vram();palette(gs,1024,0x80112233);
        gs.write_ad(6,tex(0x14,0,16,0,1));
        gs.write_ad(6,tex(0x14,2,0,0,0));
        CHECK(gs.clut[0]==0x2233 && gs.clut[256]==0x8011);
        gs.write_transfer_pixel(2,4096,64,0,0,0x5566);
        gs.write_ad(7,tex(0x14,2,64,16,1)); // Replace only the upper halfword.
        gs.write_ad(6,tex(0x14,0,0,0,0));
        CHECK(gs.texel_from_tex0(0,0,0)==0x55662233);
    }
    { // An eight-bit palette fills 16 banks; later four-bit loads keep the rest.
        G gs;gs.ensure_vram();
        for(unsigned row=0;row<16;++row)for(unsigned col=0;col<16;++col) {
            const auto index=(col%8)+(col/8)*16+(row%2)*8+(row/2)*32;
            gs.write_transfer_pixel(0,1024,64,col,row,0x80000000u+index);
        }
        gs.write_ad(6,tex(0x13,0,16,0,1));
        gs.write_transfer_pixel(0x14,0,128,0,0,7);
        gs.write_ad(6,tex(0x14,0,0,9,0));
        CHECK(gs.texel_from_tex0(0,0,0)==0x80000097);
        palette(gs,4096,0x80778800);gs.write_ad(6,tex(0x14,0,64,0,1));
        gs.write_ad(6,tex(0x14,0,0,9,0));
        CHECK(gs.texel_from_tex0(0,0,0)==0x80000097);
    }
    { // The CLUT-load barrier must finish a queued draw using its old palette.
        G gs;gs.ensure_vram();palette(gs,1024,0x80112233);palette(gs,4096,0x80445566);
        gs.write_ad(0x4c,8ull|(1ull<<16));gs.write_ad(0x4e,31);
        gs.write_ad(0x47,(1ull<<16)|(1ull<<17));gs.write_ad(0x40,(7ull<<16)|(7ull<<48));
        const auto attributes=(1ull<<34)|(1ull<<35); // RGBA DECAL.
        gs.write_ad(6,tex(0x14,0,16,9,1)|attributes);
        gs.write_ad(0,(1ull<<4)|(1ull<<8)); // Textured point, UV coordinates.
        gs.write_ad(3,0);gs.write_ad(1,0x80808080);gs.write_ad(5,(1ull<<32)|(16ull<<16)|16);
        CHECK(gs.rasterized_draw_count==0);
        gs.write_ad(6,tex(0x14,0,64,9,1)|attributes);
        CHECK(gs.rasterized_draw_count==1);
        CHECK(gs.pixel32(8*2048,64,1,1)==0x80112233);
        gs.write_ad(5,(1ull<<32)|(16ull<<16)|32);gs.rasterize_pending_draws();
        CHECK(gs.pixel32(8*2048,64,2,1)==0x80445566);
    }
    { // Unsupported/undefined cache accesses remain explicit boundaries.
        G gs;gs.ensure_vram();gs.write_ad(6,tex(0x14,0,0,9,0));
        bool fault=false;try{(void)gs.texel_from_tex0(0,0,0);}catch(const std::runtime_error&){fault=true;}
        CHECK(fault);
        for(const auto invalid:{tex(0x14,0,0,16,1),tex(0x13,0,0,1,1),
                               tex(0x14,0,0,0,6),tex(0x14,0,0,0,7),tex(0x14,0,0,0,4)}) {
            fault=false;try{gs.write_ad(6,invalid);}catch(const std::runtime_error&){fault=true;}
            CHECK(fault && gs.clut_load_count==0);
        }
    }
    return 0;
}

static int test_indexed_transfer_stride() {
    using G=hg::GsRegisterState;
    // Independently assembled uploads observed through a read-only memory
    // oracle. These are physical byte offsets, not values from the accessor
    // under test. DBW1 and DBW3 also verify the observed alias/overwrite order.
    struct ObservedStride {
        unsigned units;
        std::array<unsigned,4> offsets,final_marker;
    };
    constexpr std::array<ObservedStride,4> observations{{
        {1, {0,0,0,0x2000}, {2,2,2,3}},
        {3, {0,0x2000,0x4000,0x4000}, {0,1,3,3}},
        {11,{0,0xa000,0x14000,0xc000}, {0,1,2,3}},
        {31,{0,0x1e000,0x3c000,0x20000}, {0,1,2,3}}
    }};
    constexpr std::array<unsigned,4> x{0,0,0,128},row{0,1,2,1};
    for(const auto& observed:observations)for(const unsigned format:{0x13u,0x14u}) {
        G gs;gs.ensure_vram();
        constexpr std::uint32_t sentinel=0x2468ace0;
        std::fill(gs.vram.begin(),gs.vram.end(),sentinel);
        const auto base_blocks=format==0x13?0x800u:0x1800u;
        const auto page_height=format==0x13?64u:128u;
        const auto tile_width=format==0x13?16u:32u;
        const std::array<unsigned,4> markers=format==0x13?
            std::array<unsigned,4>{0x31,0x53,0x75,0x97}:
            std::array<unsigned,4>{0x99,0xbb,0xdd,0xee};
        for(unsigned i=0;i<4;++i) {
            gs.write_ad(0x50,(std::uint64_t(base_blocks)<<32)|
                             (std::uint64_t(observed.units)<<48)|(std::uint64_t(format)<<56));
            gs.write_ad(0x51,(std::uint64_t(x[i])<<32)|(std::uint64_t(row[i]*page_height)<<48));
            gs.write_ad(0x52,tile_width|(16ull<<32));gs.write_ad(0x53,0);
            const auto data=std::uint64_t(markers[i])*0x0101010101010101ull;
            for(unsigned q=0;q<16;++q)gs.write_image_qword(data,data);
            CHECK(!gs.gif_to_vram_active && gs.transfer_remaining==0);
            const auto first=base_blocks*64+observed.offsets[i]/4;
            for(unsigned n=0;n<64;++n)CHECK(gs.vram[first+n]==markers[i]*0x01010101u);
        }
        for(unsigned i=0;i<4;++i) {
            const auto first=base_blocks*64+observed.offsets[i]/4;
            CHECK(gs.vram[first-1]==sentinel && gs.vram[first+64]==sentinel);
            const auto expected=markers[observed.final_marker[i]]*0x01010101u;
            for(unsigned n=0;n<64;++n)CHECK(gs.vram[first+n]==expected);
        }
        // The local-copy source must use that same odd stride across page rows.
        const auto destination_format=format==0x13?0x1bu:0x2cu;
        gs.write_ad(0x50,base_blocks|(std::uint64_t(observed.units)<<16)|(std::uint64_t(format)<<24)|
                          (0x3000ull<<32)|(1ull<<48)|(std::uint64_t(destination_format)<<56));
        gs.write_ad(0x51,std::uint64_t(page_height)<<16);
        gs.write_ad(0x52,8ull|(1ull<<32));gs.write_ad(0x53,2);
        const auto copied_marker=markers[observed.final_marker[1]];
        for(unsigned n=0;n<8;++n)CHECK(gs.read_transfer_pixel(destination_format,0x3000*64,64,n,0)==
                                      (format==0x13?copied_marker:copied_marker&15));
        for(const unsigned units:{0u,33u}) {
            gs.write_ad(0x50,(std::uint64_t(units)<<48)|(std::uint64_t(format)<<56));
            gs.write_ad(0x51,0);gs.write_ad(0x52,8ull|(1ull<<32));
            bool rejected=false;try{gs.write_ad(0x53,0);}catch(const std::runtime_error&){rejected=true;}
            CHECK(rejected && !gs.gif_to_vram_active && gs.vram[0]==sentinel);
        }
    }
    return 0;
}

static int test_indexed_sampling_stride() {
    using G=hg::GsRegisterState;
    // Fixed physical offsets and final palette indices from our independent
    // 56-sprite memory observation (ORACLE.md, 2026-09-12T17:18:40Z).
    // Seed raw VRAM blocks: neither expected offsets nor expected colors are
    // computed by the indexed accessor being tested. CLUT loading has its own
    // regressions; seed its known contents here to isolate sampling.
    struct SampleCase {
        unsigned units;
        std::array<unsigned,4> offsets,indices8,indices4;
    };
    constexpr std::array<SampleCase,7> cases{{
        {1,{0,0,0,0x2000},{0x75,0x75,0x75,0x97},{13,13,13,14}},
        {2,{0,0x2000,0x4000,0x4000},{0x31,0x53,0x97,0x97},{9,11,14,14}},
        {3,{0,0x2000,0x4000,0x4000},{0x31,0x53,0x97,0x97},{9,11,14,14}},
        {10,{0,0xa000,0x14000,0xc000},{0x31,0x53,0x75,0x97},{9,11,13,14}},
        {11,{0,0xa000,0x14000,0xc000},{0x31,0x53,0x75,0x97},{9,11,13,14}},
        {30,{0,0x1e000,0x3c000,0x20000},{0x31,0x53,0x75,0x97},{9,11,13,14}},
        {31,{0,0x1e000,0x3c000,0x20000},{0x31,0x53,0x75,0x97},{9,11,13,14}}
    }};
    const auto color=[](unsigned n){return 0x80000000u|n|((255-n)<<8)|((n^0x5a)<<16);};
    constexpr std::array<unsigned,4> x{2,2,2,130},row{0,1,2,1};
    for(const auto& sample:cases)for(const unsigned format:{0x13u,0x14u}) {
        G gs;gs.ensure_vram();
        constexpr std::uint32_t sentinel=0x44556677;
        std::fill(gs.vram.begin(),gs.vram.end(),sentinel);
        const auto base_blocks=format==0x13?0x800u:0x1800u;
        const auto page_height=format==0x13?64u:128u;
        const std::array<unsigned,4> markers=format==0x13?
            std::array<unsigned,4>{0x31,0x53,0x75,0x97}:
            std::array<unsigned,4>{0x99,0xbb,0xdd,0xee};
        for(unsigned i=0;i<4;++i)
            std::fill_n(gs.vram.begin()+base_blocks*64+sample.offsets[i]/4,64,markers[i]*0x01010101u);
        for(unsigned n=0;n<256;++n) {
            gs.clut[n]=std::uint16_t(color(n));gs.clut[n+256]=std::uint16_t(color(n)>>16);
        }
        gs.clut_valid.fill(true);
        const auto tex0=base_blocks|(std::uint64_t(sample.units)<<14)|(std::uint64_t(format)<<20)|
                        (9ull<<26)|(9ull<<30)|(1ull<<34)|(1ull<<35);
        gs.write_ad(6,tex0);gs.write_ad(7,tex0);
        gs.write_ad(0x4c,0x180|(1ull<<16));gs.write_ad(0x4e,0x1ff|(1ull<<32));
        gs.write_ad(0x47,3ull<<16);gs.write_ad(0x40,(63ull<<16)|(3ull<<48));
        gs.write_ad(0x46,1);gs.write_ad(0x14,1);gs.write_ad(1,0x3f80000080808080ull);
        gs.write_ad(0,0x116); // UV-textured one-pixel sprites, RGBA DECAL.
        const auto& indices=format==0x13?sample.indices8:sample.indices4;
        const auto image=gs.texture_from_tex0(0);
        CHECK(image.width==512 && image.height==512);
        for(unsigned i=0;i<4;++i) {
            const auto y=row[i]*page_height+2;
            const auto expected=color(indices[i]);
            for(unsigned context=0;context<2;++context) {
                CHECK(gs.texel_from_tex0(context,x[i],y)==expected);
                CHECK(gs.point_sample_tex0(context,x[i],y)==expected);
            }
            CHECK(image.rgba[y*512+x[i]]==expected);
            gs.write_ad(3,(x[i]*16+8)|(std::uint64_t(y*16+8)<<16));
            gs.write_ad(5,i*16);gs.write_ad(5,((i+1)*16)|(16ull<<16));
        }
        CHECK(gs.rasterize_pending_draws()==4);
        for(unsigned i=0;i<256;++i)
            CHECK(gs.pixel32(0x3000*64,64,i%64,i/64)==(i<4?color(indices[i]):sentinel));
        gs.value[6]&=~(63ull<<14);
        bool rejected=false;
        try{(void)gs.texel_from_tex0(0,0,0);}catch(const std::runtime_error&){rejected=true;}
        CHECK(rejected); // Zero-width textures remain invalid.
    }
    return 0;
}

static int test_draw_history_retirement() {
    using G=hg::GsRegisterState;
    const auto configure=[](G& gs) {
        gs.ensure_vram();
        gs.write_ad(0x4c,1ull<<16); // CT32 framebuffer, width64.
        gs.write_ad(0x4e,31); // Separate depth buffer.
        gs.write_ad(0x47,(1ull<<16)|(1ull<<17)); // ZTE, ALWAYS.
        gs.write_ad(0x40,(7ull<<16)|(7ull<<48));
        gs.write_ad(0,0); // Untextured point.
    };
    const auto point=[](G& gs,unsigned x,std::uint32_t color) {
        gs.write_ad(1,color);
        gs.write_ad(5,(1ull<<32)|(16ull<<16)|(std::uint64_t(x)<<4));
    };
    { // A full history can contain completed work followed by pending draws.
        G gs;configure(gs);
        for(std::size_t n=0;n<G::max_draws-2;++n)point(gs,1,0x80112233);
        CHECK(gs.rasterize_pending_draws()==G::max_draws-2);
        CHECK(gs.pixel32(0,64,1,1)==0x80112233);
        // Detect accidental replay of retired draws, independently of counters.
        gs.vram[G::psmct32_word(0,64,1,1)]=0x80445566;
        point(gs,2,0x80778899);point(gs,3,0x80aabbcc);
        const auto first=gs.materialize_draw_environment(gs.draws[gs.draws.size()-2]);
        const auto second=gs.materialize_draw_environment(gs.draws.back());
        const auto first_rgba=gs.draws[gs.draws.size()-2].vertices[0].rgba;
        const auto second_rgba=gs.draws.back().vertices[0].rgba;
        CHECK(gs.draws.size()==G::max_draws);
        point(gs,4,0x80ddeeff); // Retire only the completed prefix on capacity.
        CHECK(gs.draws.size()==3 && gs.retired_draw_count==G::max_draws-2);
        CHECK(gs.rasterized_draw_count==G::max_draws-2 && gs.pending_draw_index()==0);
        CHECK(gs.materialize_draw_environment(gs.draws[0])==first &&
              gs.materialize_draw_environment(gs.draws[1])==second);
        CHECK(gs.draws[0].vertices[0].rgba==first_rgba && gs.draws[0].vertices[0].x==32);
        CHECK(gs.draws[1].vertices[0].rgba==second_rgba && gs.draws[1].vertices[0].x==48);
        CHECK(gs.pixel32(0,64,2,1)==0 && gs.pixel32(0,64,3,1)==0 && gs.pixel32(0,64,4,1)==0);
        CHECK(gs.rasterize_pending_draws()==3 && gs.rasterized_draw_count==G::max_draws+1);
        CHECK(gs.pixel32(0,64,1,1)==0x80445566);
        CHECK(gs.pixel32(0,64,2,1)==0x80778899 && gs.pixel32(0,64,3,1)==0x80aabbcc);
        CHECK(gs.pixel32(0,64,4,1)==0x80ddeeff && gs.pixel32(0,64,0,1)==0);
        CHECK(gs.rasterize_pending_draws()==0);
        // A second wrap preserves cumulative accounting across both retirements.
        for(std::size_t n=0;n<G::max_draws-3;++n)point(gs,5,0x80abcdef);
        CHECK(gs.rasterize_pending_draws()==G::max_draws-3);
        CHECK(gs.rasterized_draw_count==2*G::max_draws-2);
        point(gs,6,0x80123456);
        CHECK(gs.draws.size()==1 && gs.retired_draw_count==2*G::max_draws-2);
        CHECK(gs.pending_draw_index()==0 && gs.pixel32(0,64,6,1)==0);
        CHECK(gs.rasterize_pending_draws()==1 && gs.rasterized_draw_count==2*G::max_draws-1);
        CHECK(gs.pixel32(0,64,6,1)==0x80123456 && gs.pixel32(0,64,1,1)==0x80445566);
        CHECK(gs.retire_rasterized_draws()==1 && gs.draws.empty());
        CHECK(gs.retire_rasterized_draws()==0 && gs.rasterize_pending_draws()==0);
    }
    { // Capacity remains an explicit fault when no completed history exists.
        G gs;
        for(std::size_t n=0;n<G::max_draws;++n)point(gs,1,0x80112233);
        bool fault=false;
        try {point(gs,2,0x80445566);}
        catch(const std::runtime_error& error) {
            fault=std::string(error.what())=="GS primitive queue exceeds bounded capacity";
        }
        CHECK(fault && gs.draws.size()==G::max_draws);
        CHECK(gs.rasterized_draw_count==0 && gs.retired_draw_count==0 && gs.vram.empty());
        CHECK(gs.draws.front().vertices[0].x==16 && gs.draws.back().vertices[0].x==16);
    }
    { // Retirement cannot hide an unsupported pending draw or skip past it.
        G gs;configure(gs);point(gs,1,0x80112233);
        CHECK(gs.rasterize_pending_draws()==1);
        gs.write_ad(0,3ull|(1ull<<7)); // Unsupported antialiased triangle.
        for(unsigned n=0;n<3;++n)
            gs.write_ad(5,(1ull<<32)|((std::uint64_t(n+1)*16)<<16)|16);
        gs.write_ad(0,0);point(gs,2,0x80445566);
        CHECK(gs.retire_rasterized_draws()==1 && gs.draws.size()==2);
        bool fault=false;
        try {(void)gs.rasterize_pending_draws();}
        catch(const std::runtime_error&) {fault=true;}
        CHECK(fault && gs.rasterized_draw_count==1 && gs.retired_draw_count==1);
        CHECK(gs.pending_draw_index()==0 && gs.draws.size()==2 && gs.draws[0].primitive==3);
        CHECK(gs.pixel32(0,64,1,1)==0x80112233 && gs.pixel32(0,64,2,1)==0);
    }
    return 0;
}


// Freeze the prior byte-at-a-time transport append; parser semantics stay shared.
static int test_qword_append_equivalence() {
    const auto append=[](auto& bytes,std::uint64_t low,std::uint64_t high) {
        for(unsigned i=0;i<8;++i)bytes.push_back(std::uint8_t(low>>(i*8)));
        for(unsigned i=0;i<8;++i)bytes.push_back(std::uint8_t(high>>(i*8)));
    };
    const auto fault=[](auto call) {
        try {call();return std::string();}
        catch(const std::runtime_error& e) {return std::string(e.what());}
    };
    unsigned cases=0;
    for(unsigned seed=0;seed<64;++seed) {
        hg::Vif1Path actual,reference;hg::GifPath ga,gr;
        hg::GsRegisterState sa,sr;
        actual.pending.reserve(seed%3);reference.pending.reserve(seed%5);
        const unsigned vectors=1+seed%16;
        std::vector<std::uint8_t> bytes(4+16*vectors);
        // Complete, unmasked V4-32 payload; final qword includes trailing NOPs.
        const std::uint32_t code=0x6c000000u|(vectors<<16)|(seed*7);
        for(unsigned i=0;i<4;++i)bytes[i]=std::uint8_t(code>>(8*i));
        for(std::size_t i=4;i<bytes.size();++i)bytes[i]=std::uint8_t(i*37+seed);
        bytes.resize((bytes.size()+15)&~std::size_t(15));
        for(std::size_t at=0;at<bytes.size();at+=16) {
            const auto low=hg::detail::gif_u64le(bytes.data()+at);
            const auto high=hg::detail::gif_u64le(bytes.data()+at+8);
            const auto a=fault([&]{actual.submit_qword(low,high,ga,sa);});
            const auto r=fault([&]{append(reference.pending,low,high);reference.process_pending(gr,sr);});
            CHECK(a==r);CHECK(actual.pending==reference.pending);
            CHECK(actual.pending_phase==reference.pending_phase);
            CHECK(actual.vu_mem==reference.vu_mem);
            CHECK(actual.vu_mem_defined==reference.vu_mem_defined);++cases;
        }
        // Interrupt fault retains the entire append. Capacity faults do likewise.
        actual.reset_interface();reference.reset_interface();
        if(seed&1) {actual.pending.resize(hg::Vif1Path::max_pending);reference.pending=actual.pending;}
        const auto a=fault([&]{actual.submit_qword(0x80000000,seed,ga,sa);});
        const auto r=fault([&]{append(reference.pending,0x80000000,seed);reference.process_pending(gr,sr);});
        CHECK(!a.empty()&&a==r);CHECK(actual.pending==reference.pending);++cases;
    }
    for(unsigned format=0;format<4;++format)for(unsigned count=0;count<17;++count) {
        hg::GifPath actual,reference;hg::GsRegisterState sa,sr;
        actual.pending.reserve(count%3);reference.pending.reserve(count%5);
        // PACKED A+D and REGLIST FOG; IMAGE deliberately faults without a transfer.
        const auto descriptor=format==0?0xeull:0xaull;
        const std::uint64_t tag=count|0x8000ull|(std::uint64_t(format)<<58)|(1ull<<60);
        bool stopped=false;
        for(unsigned qword=0;qword<=count&&!stopped;++qword) {
            const auto low=qword?std::uint64_t(qword*123):tag;
            const auto high=qword?0x3dull:descriptor;
            const auto a=fault([&]{actual.submit_qword(low,high,sa);});
            const auto r=fault([&]{
                append(reference.pending,low,high);
                if(reference.pending.size()>hg::GifPath::max_pending)
                    throw std::runtime_error("GIF packet exceeds bounded path capacity");
                while(const auto size=hg::gif_packet_size(reference.pending.data(),reference.pending.size())) {
                    const auto packet=hg::decode_gif_packet(reference.pending.data(),*size);
                    hg::apply_gif_register_transfers(packet,sr);
                    reference.pending.erase(reference.pending.begin(),reference.pending.begin()+*size);
                }
            });
            CHECK(a==r);CHECK(actual.pending==reference.pending);CHECK(sa.value==sr.value);
            CHECK(sa.gif_q==sr.gif_q);stopped=!a.empty();++cases;
        }
        actual.write_control(1);reference.write_control(1);CHECK(actual.pending==reference.pending);
    }
    {
        hg::GifPath actual,reference;hg::GsRegisterState gs;
        actual.pending.resize(hg::GifPath::max_pending);reference.pending=actual.pending;
        const auto a=fault([&]{actual.submit_qword(123,456,gs);});
        append(reference.pending,123,456);
        CHECK(a=="GIF packet exceeds bounded path capacity");CHECK(actual.pending==reference.pending);++cases;
    }
    std::cout<<"Qword append equivalence cases: "<<cases<<'\n';
    return 0;
}

int main() {
    CHECK(test_qword_append_equivalence()==0);
    { // Scanout must read exactly the rectangle's physical pages, read-only.
        struct ReadObserver: hg::GsMemoryObserver {
            std::array<bool,512> seen{};bool wrote=false;
            void access(std::uint32_t*,std::size_t first,std::size_t count,bool write) override {
                wrote|=write;
                for(auto page=first/2048;count&&page<=(first+count-1)/2048;++page)seen.at(page)=true;
            }
            void forget(std::uint32_t*) noexcept override {}
        };
        hg::GsRegisterState gs;gs.ensure_vram();
        for(unsigned i=0;i<gs.vram.size();++i)gs.vram[i]=i*0x9371b9a5u;
        const auto scalar=gs;
        const std::array<std::array<unsigned,4>,6> rectangles{{
            {0,0,1,1},{63,31,129,65},{2040,2047,135,70},
            {2047,2047,4096,2},{1,63,65,129},{17,17,640,448}}};
        for(unsigned format:{0u,1u,2u,10u})for(unsigned stride:{64u,192u,4032u})
        for(unsigned base:{0u,1u,511u})for(auto rectangle:rectangles){
            const auto sx=rectangle[0],sy=rectangle[1],w=rectangle[2],h=rectangle[3];
            gs.vram.observe(nullptr);
            auto observer=std::make_shared<ReadObserver>();gs.vram.observe(observer);
            gs.privileged_pmode=1;
            gs.privileged_dispfb[0]=base|(std::uint64_t(stride/64)<<9)|(std::uint64_t(format)<<15)|
                (std::uint64_t(sx)<<32)|(std::uint64_t(sy)<<43);
            gs.privileged_display[0]=(std::uint64_t(w-1)<<32)|(std::uint64_t(h-1)<<44);
            const auto image=gs.display_image();std::array<bool,512> required{};
            for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){
                const auto word=(format==0||format==1)?hg::GsRegisterState::psmct32_word(base*2048,stride,sx+x,sy+y):
                    format==2?hg::GsRegisterState::psmct16_word(base*2048,stride,sx+x,sy+y):
                              hg::GsRegisterState::psmct16s_word(base*2048,stride,sx+x,sy+y);
                required[word/2048]=true;auto pixel=scalar.vram[word];
                if(format==1)pixel=(pixel&0xffffffu)|0x80000000u;
                if(format==2||format==10){
                    const auto p=std::uint16_t(pixel>>(((sx+x)&8)?16:0));
                    const auto expand=[](unsigned v){return (v<<3)|(v>>2);};
                    pixel=expand(p&31)|(expand((p>>5)&31)<<8)|(expand((p>>10)&31)<<16)|((p&0x8000)?0x80000000u:0u);
                }
                CHECK(image.rgba[y*w+x]==pixel);
            }
            CHECK(observer->seen==required&&!observer->wrote);
        }
        gs.vram.observe(nullptr);
    }
    { // Differential IMAGE packing: preserved scalar pixel path is the reference.
        hg::GsRegisterState actual,reference;
        actual.vram.assign(1024*1024,0xa53c69e7);reference.vram=actual.vram;
        for(unsigned base:{0u,64u,1048512u})for(unsigned stride:{64u,192u,704u,4032u})
        for(unsigned x:{0u,8u,16u,112u,2032u,2040u})for(unsigned y:{0u,1u,2u,3u,4u,7u,8u,15u,31u,63u,2047u})
        for(unsigned width:{8u,16u,24u,32u}){
            actual.value[0x50]=(std::uint64_t(base/64)<<32)|(std::uint64_t(stride/64)<<48)|(0x13ull<<56);
            actual.value[0x52]=width|(3ull<<32);
            actual.transfer_x=x;actual.transfer_y=y;actual.transfer_width=width;
            actual.transfer_remaining=width*3;actual.gif_to_vram_active=true;actual.host_transfer_completed=false;
            unsigned written=0;
            while(actual.transfer_remaining){
                const auto low=0x718293a4b5c6d7e8ull+written,high=0x192a3b4c5d6e7f80ull+written;
                const auto count=std::min(16u,width*3-written);
                for(unsigned i=0;i<count;++i){
                    const auto px=x+(written+i)%width,py=y+(written+i)/width;
                    const auto value=std::uint32_t(((i<8?low:high)>>((i&7)*8))&255);
                    reference.write_transfer_pixel(0x13,base,stride,px,py,value);
                }
                actual.write_image_qword(low,high);
                for(unsigned i=0;i<count;++i){
                    const auto word=hg::GsRegisterState::psmt8_word(base,stride,x+(written+i)%width,y+(written+i)/width);
                    CHECK(actual.vram[word]==reference.vram[word]);
                }
                written+=count;
                CHECK(actual.transfer_remaining==width*3-written);
            }
            CHECK(actual.host_transfer_completed&&!actual.gif_to_vram_active&&actual.completed_transfer_format==0x13);
        }
        CHECK(actual.vram==reference.vram);
    }
    { // Large consecutive PSMT8 IMAGE runs materialize each destination page once.
        hg::GsRegisterState bulk,serial;
        bulk.vram.assign(1024*1024,0x5ac396e7);serial.vram=bulk.vram;
        const auto configure=[](hg::GsRegisterState& gs) {
            gs.value[0x50]=(4ull<<32)|(8ull<<48)|(0x13ull<<56);
            gs.value[0x52]=256ull|(32ull<<32);gs.transfer_x=2032;gs.transfer_y=2046;
            gs.transfer_width=256;gs.transfer_remaining=8192;gs.gif_to_vram_active=true;
        };
        configure(bulk);configure(serial);
        std::vector<hg::GifTransfer> transfers(520);
        for(std::size_t n=0;n<transfers.size();++n) {
            transfers[n]={hg::GifTransferKind::Image,0,
                0x718293a4b5c6d7e8ull+n*0x0101010101010101ull,
                0x192a3b4c5d6e7f80ull+n*0x0303030303030303ull,n==0};
            serial.write_image_qword(transfers[n].low,transfers[n].high);
        }
        bulk.write_image_transfers(transfers.data(),transfers.size());
        CHECK(bulk.vram==serial.vram&&bulk.transfer_remaining==serial.transfer_remaining);
        CHECK(bulk.gif_to_vram_active==serial.gif_to_vram_active&&
              bulk.host_transfer_completed==serial.host_transfer_completed);
    }
    { // Compare native wide division with independent bit-at-a-time arithmetic.
        std::uint64_t seed=0x975319ac6842fedbull;
        const auto next=[&](){seed=seed*6364136223846793005ull+1442695040888963407ull;return seed;};
        constexpr auto signed_limit=std::uint64_t(0x7fffffffffffffff);
        for(unsigned i=0;i<200000;++i) {
            const auto a=next(),b=next();
            const bool expected=!a||!b||a<=signed_limit/b;
            CHECK(hg::gs_unsigned_product_fits_i64(a,b)==expected);
        }
        CHECK(hg::gs_unsigned_product_fits_i64(signed_limit,1));
        CHECK(hg::gs_unsigned_product_fits_i64(1,signed_limit));
        CHECK(!hg::gs_unsigned_product_fits_i64(signed_limit,2));
        CHECK(!hg::gs_unsigned_product_fits_i64(~std::uint64_t(0),~std::uint64_t(0)));
        for(unsigned i=0;i<200000;++i) {
            const auto n=next(),d=next()|1;const auto shift=unsigned(next()&63);
            const auto high=shift?n>>(64-shift):0;
            std::uint64_t q=0,r=0;const auto ready=hg::gs_shifted_divide(n,d,shift,q,r);
            if(high>=d){CHECK(!ready);continue;}
            if(!ready){CHECK(high!=0);continue;} // Portable hosts retain checked fallback.
            auto expected_q=n/d,expected_r=n%d;
            for(unsigned bit=0;bit<shift;++bit) {
                const bool carry=expected_r>=d-expected_r;
                expected_r=carry?expected_r-(d-expected_r):expected_r+expected_r;
                expected_q=expected_q*2+unsigned(carry);
            }
            CHECK(q==expected_q&&r==expected_r);
        }
        std::uint64_t q=0,r=0;
        CHECK(!hg::gs_shifted_divide(1,0,0,q,r));
        CHECK(!hg::gs_shifted_divide(1,1,64,q,r));
        CHECK(hg::gs_shifted_divide(0,1,63,q,r)&&q==0&&r==0);
    }
    { // The estimate must return the exact native integer quotient, including
      // maxima and values immediately on either side of division boundaries.
        for(auto area:{1ull,2ull,3ull,7ull,255ull,65535ull,65536ull,65535ull*65535ull}) {
            const hg::GsTriangleQuotient divide(area);
            for(auto q:{0ull,1ull,255ull,65535ull,0x7fffffffull,0xfffffffeull,0xffffffffull})
            for(auto remainder:{0ull,1ull,area/2,area-1}) {
                const auto n=area*q+remainder;
                if(n<=area*0xffffffffull)CHECK(divide(n)==n/area);
            }
        }
        std::uint64_t random=0x123456789abcdef0ull;
        for(unsigned i=0;i<300000;++i) {
            random=random*6364136223846793005ull+1442695040888963407ull;
            const auto area=1+random%(65535ull*65535ull);
            const hg::GsTriangleQuotient divide(area);
            random=random*6364136223846793005ull+1442695040888963407ull;
            const auto n=random%(area*0xffffffffull+1);
            CHECK(divide(n)==n/area);
        }
    }

    { // Texel centers, four-neighbor RGBA interpolation, and independent wrapping.
        hg::GsRegisterState gs;gs.ensure_vram();
        gs.value[6]=(1ull<<14)|(3ull<<26)|(3ull<<30)|(1ull<<34)|(1ull<<35);
        const auto put=[&](unsigned x,unsigned y,std::uint32_t color) {
            gs.vram[hg::GsRegisterState::psmct32_word(0,64,x,y)]=color;
        };
        put(0,0,0x10203040);put(1,0,0x50607080);
        put(0,1,0x90a0b0c0);put(1,1,0xd0e0f000);put(7,0,0x8090a0b0);
        CHECK(gs.shade_linear_tex0(0,8,8,0)==0x10203040);
        CHECK(gs.shade_linear_tex0(0,24,8,0)==0x50607080);
        CHECK(gs.shade_linear_tex0(0,16,16,0)==0x70809060);
        CHECK(gs.shade_linear_tex0(0,0,8,0)==0x48586878);
        gs.value[8]=5; // Clamp both neighbors independently at the texture edge.
        CHECK(gs.shade_linear_tex0(0,0,8,0)==0x10203040);
        CHECK(!gs.linear_texture_filter(0));
        gs.value[0x14]=0x60;CHECK(gs.linear_texture_filter(0));
        gs.value[0x4c]=1|(1ull<<16);gs.value[0x4e]=1ull<<32;
        gs.value[0x47]=3ull<<16;gs.value[0x40]=(7ull<<16)|(7ull<<48);
        hg::GsDraw point;point.primitive=0;point.prim_state=0x110;point.count=1;
        point.vertices[0].x=16;point.vertices[0].y=16;point.vertices[0].uv=16|(16u<<16);
        CHECK(gs.rasterize_point(point));
        CHECK(gs.pixel32(2048,64,1,1)==0x70809060);
        for(auto mode:{0x20u,0x40u,0x80u}) {
            gs.value[0x14]=mode;bool rejected=false;
            try{(void)gs.linear_texture_filter(0);}catch(const std::runtime_error&){rejected=true;}
            CHECK(rejected);
        }
        CHECK(hg::gs_stq_perspective_texel_coordinate({0x3e000000,0,0},{0x3f800000,0,0},{1,0,0},8,4)==16);
        CHECK(hg::gs_stq_perspective_texel_coordinate({0xbe000000,0,0},{0x3f800000,0,0},{1,0,0},8,4)==-16);
        // Fragment-local setup must not retain texels or register state between
        // pixels: render-to-texture writes and TEXA changes remain observable.
        put(0,0,0);
        CHECK(gs.shade_linear_tex0(0,16,16,0)==0x6c788450);
        put(0,0,0x10203040);
        gs.value[6]|=1ull<<20; // CT24 alpha comes from the current TEXA.
        gs.value[0x3b]=0x20;
        CHECK(gs.shade_linear_tex0(0,16,16,0)==0x20809060);
        gs.value[0x3b]=0x60;
        CHECK(gs.shade_linear_tex0(0,16,16,0)==0x60809060);
    }
    { // Differential sprite check against the scalar pixel path. Overlapping
      // texture/frame storage makes stale sampling or reordered writes observable.
        hg::GsRegisterState seed;seed.ensure_vram();
        for(unsigned y=0;y<16;++y)for(unsigned x=0;x<64;++x)
            seed.vram[hg::GsRegisterState::psmct32_word(0,64,x,y)]=
                0x19e3a57bu*(1+x+64*y);
        seed.value[6]=(1ull<<14)|(3ull<<26)|(3ull<<30)|(1ull<<34);
        seed.value[0x4c]=1ull<<16;seed.value[0x4e]=(1ull<<32)|32;
        seed.value[0x47]=0x30000;seed.value[0x40]=(63ull<<16)|(31ull<<48);
        hg::GsDraw draw;draw.primitive=6;draw.count=2;draw.prim_state=0x150;
        draw.vertices[0].x=16;draw.vertices[0].y=16;draw.vertices[0].uv=3|(5u<<16);
        draw.vertices[1].x=144;draw.vertices[1].y=64;draw.vertices[1].uv=131|(53u<<16);
        draw.vertices[1].rgba=0x508040c0;draw.vertices[1].z=0x123456;
        for(bool linear:{false,true})for(unsigned mode=0;mode<4;++mode)
        for(auto alpha:{0x44ull,0x4000000068ull,0x2000000061ull})
        for(auto mask:{0u,0xff000000u,0x0f0ff0f0u}) {
            auto fast=seed,reference=seed;
            fast.value[8]=mode|(std::uint64_t(mode)<<2)|(1ull<<4)|(6ull<<14)|(1ull<<24)|(2ull<<34);
            fast.value[0x14]=linear?0x60:0;
            fast.value[0x4c]|=std::uint64_t(mask)<<32;
            fast.value[0x42]=alpha;fast.value[0x46]=mode&1;
            fast.value[0x4a]=(mode>>1)&1;fast.value[0x49]=(alpha==0x44);
            reference.value=fast.value;
            CHECK(fast.rasterize_sprite(draw));
            for(unsigned y=1;y<4;++y)for(unsigned x=1;x<9;++x) {
                const auto u=3+(x-1)*16,v=5+(y-1)*16;
                const auto source=linear?reference.shade_linear_tex0(0,u,v,draw.vertices[1].rgba):
                    reference.shade_point_tex0(0,u>>4,v>>4,draw.vertices[1].rgba);
                CHECK(reference.draw_depth_frame_pixel(0,x,y,source,draw.vertices[1].z,draw.prim_state));
            }
            CHECK(fast.vram==reference.vram);
        }
    }
    { // Prepared STQ must match the checked scalar path, including a zero-Q
      // stop and exponent-spread fallback at the same pixel with the same writes.
        hg::GsRegisterState seed;seed.ensure_vram();
        seed.value[6]=(1ull<<14)|(3ull<<26)|(3ull<<30)|(1ull<<34);
        seed.value[0x4c]=2|(1ull<<16);seed.value[0x4e]=32;
        seed.value[0x47]=0x30000;seed.value[0x40]=(7ull<<16)|(7ull<<48);seed.value[0x46]=1;
        for(unsigned i=0;i<1024*1024;++i)seed.vram[i]=0x97a54321u*(i+1);
        for(unsigned dimension:{3u,7u,10u})for(unsigned stride:{1u,2u,11u})
        for(unsigned format:{0u,0x13u,0x1bu})for(unsigned mode=0;mode<4;++mode)
        for(unsigned variant=0;variant<4;++variant)for(bool linear:{false,true}) {
            auto fast=seed,reference=seed;
            fast.value[6]=(std::uint64_t(stride)<<14)|(std::uint64_t(format)<<20)|
                (std::uint64_t(dimension)<<26)|(std::uint64_t(dimension)<<30)|(1ull<<34);
            fast.value[8]=mode|(std::uint64_t(mode)<<2)|(1ull<<4)|(6ull<<14)|(1ull<<24)|(2ull<<34);
            reference.value=fast.value;
            for(unsigned i=0;i<512;++i){fast.clut[i]=reference.clut[i]=std::uint16_t(i*137+59);
                fast.clut_valid[i]=reference.clut_valid[i]=(variant!=3);}
            fast.value[0x14]=reference.value[0x14]=linear?0x60:0;
            hg::GsDraw draw;draw.primitive=3;draw.count=3;draw.prim_state=0x18;
            draw.vertices[1].x=128;draw.vertices[2].y=128;
            std::array<std::uint32_t,3> ss{0xbf000000,0x3f000000,0x3fc00000};
            const std::array<std::uint32_t,3> tt{0x3e800000,0x3f400000,0x3f800000};
            std::array<std::uint32_t,3> qq{0x3f000000,0x3f800000,0x40000000};
            if(variant==1)ss={1,0x3f800000,0xbf800000};
            if(variant==2)qq={0x3f800000,0xbf800000,0x3f800000};
            const std::array<std::uint32_t,3> colors{0x008040ff,0xff40ff00,0x80808080};
            const std::array<std::uint32_t,3> depths{0,0xffffffff,0x80000000};
            for(unsigned i=0;i<3;++i){draw.vertices[i].st=ss[i]|(std::uint64_t(tt[i])<<32);
                draw.vertices[i].q=qq[i];draw.vertices[i].rgba=colors[i];draw.vertices[i].z=depths[i];}
            std::string fast_error,reference_error;
            try{CHECK(fast.rasterize_triangle(draw));}catch(const std::runtime_error& e){fast_error=e.what();}
            try {
                for(unsigned y=0;y<8;++y)for(unsigned x=0;x+y<8;++x) {
                    const std::array<std::int64_t,3> w{std::int64_t(8-x-y)*2048,std::int64_t(x)*2048,std::int64_t(y)*2048};
                    std::uint32_t color=0;
                    for(unsigned shift=0;shift<32;shift+=8)
                        color|=std::uint32_t((w[0]*((colors[0]>>shift)&255)+w[1]*((colors[1]>>shift)&255)+
                                             w[2]*((colors[2]>>shift)&255))/16384)<<shift;
                    const auto u=hg::gs_stq_perspective_texel_coordinate(ss,qq,w,1u<<dimension,linear?4:0);
                    const auto v=hg::gs_stq_perspective_texel_coordinate(tt,qq,w,1u<<dimension,linear?4:0);
                    color=linear?reference.shade_linear_tex0(0,u,v,color):reference.shade_point_tex0(0,u,v,color);
                    const auto z=std::uint32_t((std::uint64_t(w[1])*depths[1]+std::uint64_t(w[2])*depths[2])/16384);
                    CHECK(reference.draw_depth_frame_pixel(0,x,y,color,z,draw.prim_state));
                }
            }catch(const std::runtime_error& e){reference_error=e.what();}
            CHECK(fast_error==reference_error&&fast.vram==reference.vram);
            CHECK((variant==0||(variant==3&&format==0))==fast_error.empty());
        }
    }
    { // Exhaust the coordinate domain against the pre-optimization address formula.
        const auto reference=[](std::uint32_t base,std::uint32_t width,std::uint32_t x,std::uint32_t y) {
            x&=2047;y&=2047;
            const auto page=x/64+(y/32)*(width/64);
            const auto bx=(x/8)&7,by=(y/8)&3;
            const auto block=(bx&1)+(bx&2)*2+(bx&4)*4+(by&1)*2+(by&2)*4;
            return (base+page*2048+block*64+((y&7)/2)*16+(x&1)+(x&6)*2+(y&1)*2)&0xfffffu;
        };
        for(auto width:{64u,512u,2048u,0xffffffc0u})
            for(unsigned y=0;y<2048;++y)for(unsigned x=0;x<2048;++x)
                CHECK(hg::GsRegisterState::psmct32_word(0xfffe7,width,x,y)==reference(0xfffe7,width,x,y));
        CHECK(hg::GsRegisterState::psmct32_word(0xffffffff,512,0xffffffff,0xffffffff)==
              reference(0xffffffff,512,0xffffffff,0xffffffff));
        for(auto width:{0u,1u,63u,65u}) {
            bool rejected=false;
            try{(void)hg::GsRegisterState::psmct32_word(0,width,0,0);}
            catch(const std::runtime_error&){rejected=true;}
            CHECK(rejected);
        }
    }
    for(unsigned format:{0u,2u,0x13u,0x14u}) {
        hg::GsRegisterState gs;
        bool rejected=false;try{gs.write_image_qword(1,2);}catch(const std::runtime_error&){rejected=true;}CHECK(rejected);
        gs.write_ad(0x50,(1ull<<48)|(std::uint64_t(format)<<56));
        gs.write_ad(0x51,0);gs.write_ad(0x52,32ull|(4ull<<32));gs.write_ad(0x53,0);
        const unsigned bytes=128*hg::GsRegisterState::transfer_pixel_depth(format)/8;
        for(unsigned n=0;n<bytes/16;++n)gs.write_image_qword(0x123456789abcdef0ull,0xfedcba9876543210ull);
        CHECK(gs.host_transfer_completed && !gs.gif_to_vram_active && gs.transfer_remaining==0);
        const auto completed=gs.vram;
        for(unsigned n=0;n<bytes/16;++n)gs.write_image_qword(~0ull,~0ull);
        CHECK(gs.vram==completed);
        gs.write_ad(0x53,3);rejected=false;
        try{gs.write_image_qword(1,2);}catch(const std::runtime_error&){rejected=true;}CHECK(rejected);
        gs.write_ad(0x53,0);CHECK(gs.gif_to_vram_active && !gs.host_transfer_completed);
        gs.write_image_qword(0,0);CHECK(gs.vram!=completed);
    }
    CHECK(test_draw_history_retirement()==0);
    CHECK(test_indexed_sampling_stride()==0);
    CHECK(test_indexed_transfer_stride()==0);
    CHECK(test_clut_cache()==0);
    { // GS manual p162: each page is exactly 8192 bytes, without aliases.
        using G=hg::GsRegisterState;
        using Address=std::uint32_t(*)(std::uint32_t,std::uint32_t,std::uint32_t,std::uint32_t);
        const Address maps[]{G::psmct32_word,G::psmz32_word,G::psmct16_word,G::psmct16s_word,
                             G::psmz16_word,G::psmz16s_word,G::psmt8_word,G::psmt4_word};
        for(unsigned format=0;format<8;++format) {
            const unsigned bits=format<2?32:format<6?16:format==6?8:4;
            const unsigned width=format<6?64:128,height=65536/(bits*width);
            std::vector<bool> occupied(65536/bits);
            for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x) {
                const auto word=maps[format](0,width,x,y);
                const auto lane=bits==32?0:bits==16?unsigned(bool(x&8)):
                                bits==8?((y&2)>>1)|((x&8)>>2):((y&2)>>1)|((x&24)>>2);
                const auto slot=word*(32/bits)+lane;
                CHECK(slot<occupied.size() && !occupied[slot]);occupied[slot]=true;
                CHECK(maps[format](0,width,x+width,y)==word+2048);
            }
            for(const auto present:occupied)CHECK(present);
        }
    }
    CHECK(hg::gs_stq_texel_coordinate(0x3f000000,0x3f800000,4)==2);
    CHECK(hg::gs_stq_texel_coordinate(0xbe800000,0x3f800000,4)==-1);
    CHECK(hg::gs_stq_texel_coordinate(0x3e800000,0x3f000000,4)==2);
    bool invalid_stq=false;try{(void)hg::gs_stq_texel_coordinate(0x3f800000,0,4);}catch(const std::runtime_error&){invalid_stq=true;}
    CHECK(invalid_stq);
    invalid_stq=false;try{(void)hg::gs_stq_texel_coordinate(0x7f800000,0x3f800000,4);}catch(const std::runtime_error&){invalid_stq=true;}
    CHECK(invalid_stq);
    CHECK(hg::gs_stq_weighted_texel_coordinate(0,0x3f800000,0x3f800000,1,1,2)==1);
    CHECK(hg::gs_stq_weighted_texel_coordinate(0xbf800000,0,0x3f800000,1,1,2)==-1);
    CHECK(hg::gs_stq_weighted_texel_coordinate(0,0x3f800000,0x3f800000,3,-1,2)==-1);
    CHECK(hg::gs_stq_perspective_texel_coordinate({0,0x3f800000,0},{0x3f800000,0x3e800000,0},{1,1,0},4)==3);
    for(auto weight:{(std::int64_t(1)<<39)-1,std::int64_t(1)<<39,(std::int64_t(1)<<39)+1}) {
        for(auto sign:{-1,1})
            CHECK(hg::gs_stq_perspective_texel_coordinate(
                {0x3fffffff,0,0},{0x3fffffff,0,0},{weight*sign,0,0},1)==1);
    }
    CHECK(hg::gs_stq_perspective_texel_coordinate(
        {1,0,0},{1,0,0},{std::int64_t(1)<<40,0,0},1)==1);
    CHECK(hg::gs_stq_perspective_texel_coordinate(
        {0x3f800000,0,0},{0x3f800000,0,0},{std::int64_t(1)<<40,0,0},1)==1);
    // Real post-opening triangle: width/height 256 exactly cancels the S/Q
    // exponent gap. Multiplying by 256 first would spuriously need 65 bits.
    const std::array<std::int64_t,3> post_opening_weights{5255420,190310292,131318766};
    const std::array<std::uint32_t,3> post_opening_q{0x3c01fd28,0x3c010304,0x3c047c28};
    CHECK(hg::gs_stq_perspective_texel_coordinate(
        {0x383e0787,0x3b01abd5,0x3841adad},post_opening_q,post_opening_weights,256)==37);
    CHECK(hg::gs_stq_perspective_texel_coordinate(
        {0x3ba0b080,0x3b9f7b48,0x3bc3414e},post_opening_q,post_opening_weights,256)==170);
    // Later scene triangle: two vertices have Q=0, making S/Q large near the
    // clipped screen edge. The exact result is ordinary (4925) but an eager
    // numerator shift needs 66 bits. Exact long division must retain it.
    const std::array<std::int64_t,3> scene_weights{58892895,924868320,129474246};
    const std::array<std::uint32_t,3> scene_q{0,0x3c34ed3d,0};
    CHECK(hg::gs_stq_perspective_texel_coordinate(
        {0x3ede394e,0x3ae32152,0x3fa76e93},scene_q,scene_weights,256)==4925);
    CHECK(hg::gs_stq_perspective_texel_coordinate(
        {0,0x3b3ab693,0},scene_q,scene_weights,256)==66);
    { // A low-magnitude endpoint and a normal endpoint need a 65-bit exact
      // aligned sum, while the final perspective coordinate remains ordinary.
        const std::array<std::int64_t,3> weights{1,1,300000};
        const std::array<std::uint32_t,3> q{0x3c000000,0x3c000000,0x3c000000};
        CHECK(hg::gs_stq_perspective_texel_coordinate(
            {0x30000000,0x30000001,0x3b000000},q,weights,256)==63);
        CHECK(hg::gs_stq_perspective_texel_coordinate(
            {0xb0000000,0xb0000001,0xbb000000},q,weights,256)==-64);
        CHECK(hg::gs_stq_perspective_texel_coordinate(
            q,{0x30000000,0x30000001,0x3b000000},weights,1)==4);
    }
    invalid_stq=false;
    try{(void)hg::gs_stq_perspective_texel_coordinate(
        {1,0,0x7f7fffff},{0x3f800000,0,0x3f800000},{1,0,1},1);}
    catch(const std::runtime_error&){invalid_stq=true;}
    CHECK(invalid_stq); // Beyond the bounded 128-bit exact fallback remains explicit.
    { // A single enabled PCRTC circuit extracts its magnified source rectangle.
        hg::GsRegisterState gs;gs.ensure_vram();
        const auto base=2048u;
        for(unsigned y=0;y<2;++y)for(unsigned x=0;x<4;++x)
            gs.vram[hg::GsRegisterState::psmct32_word(base,64,x+2,y+3)]=0x80010200u+x+(y<<8);
        gs.write_privileged(0x12000000,1); // PMODE.EN1
        gs.write_privileged(0x12000070,1ull|(1ull<<9)|(2ull<<32)|(3ull<<43));
        gs.write_privileged(0x12000080,100ull|(20ull<<12)|(1ull<<23)|(1ull<<27)|(7ull<<32)|(3ull<<44));
        const auto image=gs.display_image();
        CHECK(image.width==4&&image.height==2&&image.dx==100&&image.dy==20);
        CHECK(image.magnify_x==2&&image.magnify_y==2&&image.rgba.size()==8);
        CHECK(image.rgba[0]==0x80010200&&image.rgba[7]==0x80010303);
        gs.write_privileged(0x12000000,3);bool rejected=false;
        try{(void)gs.display_image();}catch(const std::runtime_error&){rejected=true;}CHECK(rejected);
        gs.write_privileged(0x12000000,1);gs.write_privileged(0x12000080,1ull<<32); // 2x1, no magnification.
        gs.write_transfer_pixel(1,0,64,0,0,0x00112233);
        gs.write_privileged(0x12000070,(1ull<<9)|(1ull<<15));
        CHECK(gs.display_image().rgba[0]==0x80112233);
        gs.write_transfer_pixel(10,0,64,0,0,0x801f);
        gs.write_privileged(0x12000070,(1ull<<9)|(10ull<<15));
        CHECK(gs.display_image().rgba[0]==0x800000ff);
    }
    { // Published PSMCT32 page/block/column positions, independent of upload flow.
        CHECK(hg::GsRegisterState::psmct32_word(0, 64, 0, 0) == 0);
        CHECK(hg::GsRegisterState::psmct32_word(0, 64, 7, 0) == 13);
        CHECK(hg::GsRegisterState::psmct32_word(0, 64, 0, 1) == 2);
        CHECK(hg::GsRegisterState::psmct32_word(0, 64, 8, 0) == 64);
        CHECK(hg::GsRegisterState::psmct32_word(0, 64, 0, 8) == 128);
        CHECK(hg::GsRegisterState::psmct32_word(0, 64, 64, 0) == 2048);
        CHECK(hg::GsRegisterState::psmct16_word(0, 64, 0, 0) == 0);
        CHECK(hg::GsRegisterState::psmct16_word(0, 64, 15, 0) == 13);
        CHECK(hg::GsRegisterState::psmct16_word(0, 64, 0, 1) == 2);
        CHECK(hg::GsRegisterState::psmct16_word(0, 64, 16, 0) == 128);
        CHECK(hg::GsRegisterState::psmct16_word(0, 64, 0, 8) == 64);
        CHECK(hg::GsRegisterState::psmct16_word(0, 64, 0, 64) == 2048);
        CHECK(hg::GsRegisterState::psmt8_word(0, 128, 0, 0) == 0);
        CHECK(hg::GsRegisterState::psmt8_word(0, 128, 15, 0) == 13);
        CHECK(hg::GsRegisterState::psmt8_word(0, 128, 0, 4) == 24);
        CHECK(hg::GsRegisterState::psmt8_word(0, 128, 16, 0) == 64);
        CHECK(hg::GsRegisterState::psmt8_word(0, 128, 128, 0) == 2048);
        CHECK(hg::GsRegisterState::psmt4_word(0, 128, 0, 0) == 0);
        CHECK(hg::GsRegisterState::psmt4_word(0, 128, 31, 0) == 13);
        CHECK(hg::GsRegisterState::psmt4_word(0, 128, 0, 4) == 24);
        CHECK(hg::GsRegisterState::psmt4_word(0, 128, 32, 0) == 128);
        CHECK(hg::GsRegisterState::psmt4_word(0, 128, 0, 128) == 2048);
    }
    { // EOP PACKED A+D followed by a NOP: actual qword order and descriptor order are preserved.
        std::array<std::uint8_t, 48> bytes{};
        // Descriptors occupy the high tag qword: A+D then NOP.
        put64(bytes.data(), 0, 1 | (1ull << 15) | (2ull << 60));
        put64(bytes.data(), 8, 0xfe);
        put64(bytes.data(), 16, 0x1122334455667788ull);
        put64(bytes.data(), 24, 0x50);
        auto packet = hg::decode_gif_packet(bytes.data(), bytes.size());
        CHECK(packet.bytes == bytes.size() && packet.transfers.size() == 1);
        CHECK(packet.transfers[0].kind == hg::GifTransferKind::Packed);
        CHECK(packet.transfers[0].descriptor == 0xe && packet.transfers[0].low == 0x1122334455667788ull);
        CHECK(packet.transfers[0].high == 0x50);
        hg::GsRegisterState gs; hg::apply_gif_register_transfers(packet, gs);
        CHECK(gs.written[0x50] && gs.value[0x50] == 0x1122334455667788ull);
    }
    { // DMA qword boundaries may split a packet; only its completed EOP commits.
        std::array<std::uint8_t, 32> bytes{};
        put64(bytes.data(), 0, 1 | (1ull << 15) | (1ull << 60)); put64(bytes.data(), 8, 0xe);
        put64(bytes.data(), 16, 0x1234); put64(bytes.data(), 24, 0x3d);
        CHECK(!hg::gif_packet_size(bytes.data(), 16).has_value());
        hg::GifPath path; hg::GsRegisterState gs;
        path.submit_qword(0x1000000000008001ull, 0xe, gs);
        CHECK(!gs.written[0x3d] && path.pending.size() == 16);
        path.submit_qword(0x1234, 0x3d, gs);
        CHECK(gs.written[0x3d] && gs.value[0x3d] == 0x1234 && path.pending.empty());
    }
    { // REGLIST odd entries consume a padded final doubleword.
        std::array<std::uint8_t, 48> bytes{};
        put64(bytes.data(), 0, 1 | (1ull << 15) | (1ull << 58) | (3ull << 60));
        put64(bytes.data(), 8, 0x321);
        put64(bytes.data(), 16, 0x11);
        put64(bytes.data(), 24, 0x22);
        put64(bytes.data(), 32, 0x33);
        auto packet = hg::decode_gif_packet(bytes.data(), bytes.size());
        CHECK(packet.transfers.size() == 3 && packet.transfers[2].descriptor == 3);
        CHECK(packet.transfers[2].low == 0x33 && packet.bytes == bytes.size());
        hg::GsRegisterState gs; hg::apply_gif_register_transfers(packet, gs);
        CHECK(gs.value[1] == 0x11 && gs.value[2] == 0x22 && gs.value[3] == 0x33);
    }
    { // NLOOP zero ignores format/descriptors but still terminates on EOP.
        std::array<std::uint8_t, 16> bytes{};
        put64(bytes.data(), 0, 1ull << 15 | 1ull << 46 | 3ull << 58);
        auto packet = hg::decode_gif_packet(bytes.data(), bytes.size());
        CHECK(packet.transfers.empty());
    }
    { // A+D transfer setup followed by IMAGE stores PSMCT32 pixels in GS layout.
        std::array<std::uint8_t, 96> bytes{};
        put64(bytes.data(), 0, 4 | (1ull << 60)); put64(bytes.data(), 8, 0xe);
        put64(bytes.data(), 16, 1ull << 48); put64(bytes.data(), 24, 0x50);
        put64(bytes.data(), 32, 1ull << 32 | 2ull << 48); put64(bytes.data(), 40, 0x51);
        put64(bytes.data(), 48, 2 | 2ull << 32); put64(bytes.data(), 56, 0x52);
        put64(bytes.data(), 64, 0); put64(bytes.data(), 72, 0x53);
        put64(bytes.data(), 80, 1 | (1ull << 15) | (2ull << 58));
        put64(bytes.data(), 88, 0x8877665544332211ull);
        // IMAGE payload is one qword after its tag; grow the backing input for it.
        std::array<std::uint8_t, 112> complete{}; std::memcpy(complete.data(), bytes.data(), bytes.size());
        put64(complete.data(), 96, 0xffeeddccbbaa9988ull); put64(complete.data(), 104, 0x0123456701234567ull);
        auto packet = hg::decode_gif_packet(complete.data(), complete.size()); hg::GsRegisterState gs;
        hg::apply_gif_register_transfers(packet, gs);
        CHECK(gs.pixel32(0,64,1,2) == 0xbbaa9988 && gs.pixel32(0,64,2,2) == 0xffeeddcc);
        CHECK(gs.pixel32(0,64,1,3) == 0x01234567 && gs.pixel32(0,64,2,3) == 0x01234567);
        CHECK(!gs.gif_to_vram_active);
    }
    { // PACKED A+D may feed HWREG directly; IMAGE is only the denser shortcut.
        // Mirror the game's live setup order: BITBLTBUF, TRXREG, TRXPOS,
        // TRXDIR, then one 64-bit HWREG payload for a 2x1 CT32 rectangle.
        std::array<std::uint8_t,96> bytes{};
        put64(bytes.data(),0,5|(1ull<<15)|(1ull<<60));put64(bytes.data(),8,0xe);
        put64(bytes.data(),16,1ull<<48);put64(bytes.data(),24,0x50);
        put64(bytes.data(),32,2ull|(1ull<<32));put64(bytes.data(),40,0x52);
        put64(bytes.data(),48,0);put64(bytes.data(),56,0x51);
        put64(bytes.data(),64,0);put64(bytes.data(),72,0x53);
        put64(bytes.data(),80,0x2222222211111111ull);put64(bytes.data(),88,0x54);
        hg::GsRegisterState gs;
        hg::apply_gif_register_transfers(hg::decode_gif_packet(bytes.data(),bytes.size()),gs);
        CHECK(gs.pixel32(0,64,0,0)==0x11111111 && gs.pixel32(0,64,1,0)==0x22222222);
        CHECK(!gs.gif_to_vram_active && gs.transfer_remaining==0 && gs.host_transfer_completed);
        CHECK(gs.written[0x54] && gs.value[0x54]==0x2222222211111111ull);
    }
    { // PSMCT16 writes four little-endian halfwords per HWREG.
        std::array<std::uint8_t, 112> bytes{};
        put64(bytes.data(), 0, 4 | (1ull << 60)); put64(bytes.data(), 8, 0xe);
        put64(bytes.data(), 16, 1ull << 48 | 2ull << 56); put64(bytes.data(), 24, 0x50);
        put64(bytes.data(), 32, 15ull << 32 | 7ull << 48); put64(bytes.data(), 40, 0x51);
        put64(bytes.data(), 48, 4 | 2ull << 32); put64(bytes.data(), 56, 0x52);
        put64(bytes.data(), 64, 0); put64(bytes.data(), 72, 0x53);
        put64(bytes.data(), 80, 1 | (1ull << 15) | (2ull << 58));
        put64(bytes.data(), 96, 0x4444333322221111ull); put64(bytes.data(), 104, 0x8888777766665555ull);
        hg::GsRegisterState gs; hg::apply_gif_register_transfers(hg::decode_gif_packet(bytes.data(), bytes.size()), gs);
        CHECK(gs.pixel16(0,64,15,7) == 0x1111 && gs.pixel16(0,64,18,7) == 0x4444);
        CHECK(gs.pixel16(0,64,15,8) == 0x5555 && gs.pixel16(0,64,18,8) == 0x8888);
    }
    { // PSMCT32 local-to-local starts synchronously at TRXDIR and honors DIR.
        hg::GsRegisterState gs;gs.ensure_vram();
        gs.vram[hg::GsRegisterState::psmct32_word(0,64,0,0)]=0x11111111;
        gs.vram[hg::GsRegisterState::psmct32_word(0,64,1,0)]=0x22222222;
        gs.write_ad(0x50,(1ull<<16)|(256ull<<32)|(1ull<<48));
        gs.write_ad(0x51,4ull<<32);gs.write_ad(0x52,2ull|(1ull<<32));gs.write_ad(0x53,2);
        CHECK(gs.pixel32(8u*2048u,64,4,0)==0x11111111);
        CHECK(gs.pixel32(8u*2048u,64,5,0)==0x22222222 && !gs.gif_to_vram_active);
        // Right-to-left ordering preserves an overlapping one-pixel-right move.
        gs.write_ad(0x50,(1ull<<16)|(1ull<<48));
        gs.write_ad(0x51,(1ull<<32)|(2ull<<59));gs.write_ad(0x53,2);
        CHECK(gs.pixel32(0,64,1,0)==0x11111111 && gs.pixel32(0,64,2,0)==0x22222222);
        gs.write_ad(0x53,3);CHECK(!gs.gif_to_vram_active);
    }
    { // Host IMAGE packing also reaches the independently swizzled Z layouts.
        hg::GsRegisterState z32;z32.write_ad(0x50,(1ull<<48)|(0x30ull<<56));
        z32.write_ad(0x51,0);z32.write_ad(0x52,2ull|(1ull<<32));z32.write_ad(0x53,0);
        z32.write_hwreg(0x5566778811223344ull);
        CHECK(z32.read_transfer_pixel(0x30,0,64,0,0)==0x11223344);
        CHECK(z32.read_transfer_pixel(0x30,0,64,1,0)==0x55667788);
        for(const auto format:{0x32u,0x3au}) {
            hg::GsRegisterState z16;z16.write_ad(0x50,(1ull<<48)|(std::uint64_t(format)<<56));
            z16.write_ad(0x51,0);z16.write_ad(0x52,4ull|(1ull<<32));z16.write_ad(0x53,0);
            z16.write_hwreg(0x4444333322221111ull);
            CHECK(z16.read_transfer_pixel(format,0,64,0,0)==0x1111);
            CHECK(z16.read_transfer_pixel(format,0,64,3,0)==0x4444);
        }
        hg::GsRegisterState z24;z24.write_ad(0x50,(1ull<<48)|(0x31ull<<56));
        z24.write_ad(0x51,0);z24.write_ad(0x52,8ull|(1ull<<32));z24.write_ad(0x53,0);
        z24.write_image_qword(0x8877665544332211ull,0x00ffeeddccbbaa99ull);
        z24.write_image_qword(0x1817161514131211ull,0x00201f1e1d1c1b1aull);
        CHECK(z24.read_transfer_pixel(0x31,0,64,0,0)==0x332211);
        CHECK(z24.read_transfer_pixel(0x31,0,64,7,0)==0x1a1817);
    }
    { // Local copies may convert layouts when source/destination pixel depths match.
        struct CopyCase {unsigned source,destination,width,buffer_units;std::uint32_t mask;};
        for(const auto item:{CopyCase{0,0x30,2,1,0xffffffffu},CopyCase{1,0x31,8,1,0xffffffu},
                             CopyCase{2,10,4,1,0xffffu},CopyCase{0x13,0x1b,8,2,0xffu},
                             CopyCase{0x14,0x2c,8,2,0xfu}}) {
            hg::GsRegisterState gs;gs.ensure_vram();
            const auto buffer_width=item.buffer_units*64u;
            for(unsigned x=0;x<item.width;++x)
                gs.write_transfer_pixel(item.source,0,buffer_width,x,0,(0x12345670u+x)&item.mask);
            gs.write_ad(0x50,std::uint64_t(item.buffer_units)<<16|std::uint64_t(item.source)<<24|
                             256ull<<32|std::uint64_t(item.buffer_units)<<48|std::uint64_t(item.destination)<<56);
            gs.write_ad(0x51,0);gs.write_ad(0x52,item.width|(1ull<<32));gs.write_ad(0x53,2);
            for(unsigned x=0;x<item.width;++x)
                CHECK(gs.read_transfer_pixel(item.destination,8u*2048u,buffer_width,x,0)==
                      ((0x12345670u+x)&item.mask));
        }
        hg::GsRegisterState mismatch;mismatch.ensure_vram();
        mismatch.write_ad(0x50,(1ull<<16)|(2ull<<48)|(0x13ull<<56));
        mismatch.write_ad(0x51,0);mismatch.write_ad(0x52,8ull|(1ull<<32));
        bool rejected=false;try{mismatch.write_ad(0x53,2);}catch(const std::runtime_error&){rejected=true;}
        CHECK(rejected);
        hg::GsRegisterState invalid_width;invalid_width.ensure_vram();
        invalid_width.vram[0]=0x89abcdef;
        // A zero SBW remains invalid; one 64-pixel unit is a transfer width.
        invalid_width.write_ad(0x50,(0x13ull<<24)|(256ull<<32)|(1ull<<48)|(0x1bull<<56));
        invalid_width.write_ad(0x51,0);invalid_width.write_ad(0x52,8ull|(1ull<<32));
        rejected=false;try{invalid_width.write_ad(0x53,2);}catch(const std::runtime_error&){rejected=true;}
        CHECK(rejected && invalid_width.vram[0]==0x89abcdef);
    }
    { // PSMCT24 packs five RGB texels per IMAGE qword without alpha padding.
        std::array<std::uint8_t, 128> bytes{};
        put64(bytes.data(), 0, 4 | (1ull << 60)); put64(bytes.data(), 8, 0xe);
        put64(bytes.data(), 16, 1ull << 48 | 1ull << 56); put64(bytes.data(), 24, 0x50);
        put64(bytes.data(), 32, 0); put64(bytes.data(), 40, 0x51);
        put64(bytes.data(), 48, 8 | 1ull << 32); put64(bytes.data(), 56, 0x52);
        put64(bytes.data(), 64, 0); put64(bytes.data(), 72, 0x53);
        put64(bytes.data(), 80, 2 | (1ull << 15) | (2ull << 58));
        put64(bytes.data(), 96, 0x8877665544332211ull); put64(bytes.data(), 104, 0x00ffeeddccbbaa99ull);
        put64(bytes.data(),112,0x1817161514131211ull);put64(bytes.data(),120,0x00201f1e1d1c1b1aull);
        hg::GsRegisterState gs; hg::apply_gif_register_transfers(hg::decode_gif_packet(bytes.data(), bytes.size()), gs);
        CHECK((gs.pixel32(0,64,0,0) & 0xffffffu) == 0x332211 && (gs.pixel32(0,64,1,0) & 0xffffffu) == 0x665544);
        CHECK((gs.pixel32(0,64,2,0) & 0xffffffu) == 0x998877 && (gs.pixel32(0,64,3,0) & 0xffffffu) == 0xccbbaa);
        CHECK((gs.pixel32(0,64,4,0) & 0xffffffu) == 0xffeedd);
        CHECK((gs.pixel32(0,64,5,0) & 0xffffffu) == 0x131211 && (gs.pixel32(0,64,7,0)&0xffffffu)==0x1a1817);
    }
    { // PSMT8 packs sixteen indices per IMAGE qword and uses its own byte lanes.
        std::array<std::uint8_t, 112> bytes{};
        put64(bytes.data(), 0, 4 | (1ull << 60)); put64(bytes.data(), 8, 0xe);
        put64(bytes.data(), 16, 2ull << 48 | 0x13ull << 56); put64(bytes.data(), 24, 0x50);
        put64(bytes.data(), 32, 0); put64(bytes.data(), 40, 0x51);
        put64(bytes.data(), 48, 8 | 2ull << 32); put64(bytes.data(), 56, 0x52);
        put64(bytes.data(), 64, 0); put64(bytes.data(), 72, 0x53);
        put64(bytes.data(), 80, 1 | (1ull << 15) | (2ull << 58));
        put64(bytes.data(), 96, 0x0706050403020100ull); put64(bytes.data(), 104, 0x0f0e0d0c0b0a0908ull);
        hg::GsRegisterState gs; hg::apply_gif_register_transfers(hg::decode_gif_packet(bytes.data(), bytes.size()), gs);
        CHECK(gs.pixel8(0,128,0,0) == 0 && gs.pixel8(0,128,3,0) == 3);
        CHECK(gs.pixel8(0,128,0,0) == 0 && gs.pixel8(0,128,7,0) == 7);
        CHECK(gs.pixel8(0,128,0,1) == 8 && gs.pixel8(0,128,7,1) == 15);
    }
    { // PSMT4 packs thirty-two low-first nibble indices per IMAGE qword.
        std::array<std::uint8_t, 112> bytes{};
        put64(bytes.data(), 0, 4 | (1ull << 60)); put64(bytes.data(), 8, 0xe);
        put64(bytes.data(), 16, 2ull << 48 | 0x14ull << 56); put64(bytes.data(), 24, 0x50);
        put64(bytes.data(), 32, 0); put64(bytes.data(), 40, 0x51);
        put64(bytes.data(), 48, 8 | 4ull << 32); put64(bytes.data(), 56, 0x52);
        put64(bytes.data(), 64, 0); put64(bytes.data(), 72, 0x53);
        put64(bytes.data(), 80, 1 | (1ull << 15) | (2ull << 58));
        put64(bytes.data(), 96, 0xfedcba9876543210ull); put64(bytes.data(), 104, 0xfedcba9876543210ull);
        hg::GsRegisterState gs; hg::apply_gif_register_transfers(hg::decode_gif_packet(bytes.data(), bytes.size()), gs);
        CHECK(gs.pixel4(0,128,0,0) == 0 && gs.pixel4(0,128,3,0) == 3);
        CHECK(gs.pixel4(0,128,0,1) == 8 && gs.pixel4(0,128,7,1) == 15);
        CHECK(gs.pixel4(0,128,0,2) == 0 && gs.pixel4(0,128,7,3) == 15);
    }
    { // PSMCT16S IMAGE payload is ordinary packed little-endian halfwords.
        std::array<std::uint8_t, 112> bytes{};
        put64(bytes.data(), 0, 4 | (1ull << 60)); put64(bytes.data(), 8, 0xe);
        put64(bytes.data(), 16, 1ull << 48 | 10ull << 56); put64(bytes.data(), 24, 0x50);
        put64(bytes.data(), 32, 0); put64(bytes.data(), 40, 0x51);
        put64(bytes.data(), 48, 4 | 2ull << 32); put64(bytes.data(), 56, 0x52);
        put64(bytes.data(), 64, 0); put64(bytes.data(), 72, 0x53);
        put64(bytes.data(), 80, 1 | (1ull << 15) | (2ull << 58));
        put64(bytes.data(), 96, 0x4444333322221111ull); put64(bytes.data(), 104, 0x8888777766665555ull);
        hg::GsRegisterState gs; hg::apply_gif_register_transfers(hg::decode_gif_packet(bytes.data(), bytes.size()), gs);
        CHECK(gs.pixel16s(0,64,0,0) == 0x1111 && gs.pixel16s(0,64,1,0) == 0x2222);
        CHECK(gs.pixel16s(0,64,0,1) == 0x5555 && gs.pixel16s(0,64,3,1) == 0x8888);
    }
    { // PSMT8H IMAGE bytes populate the high CT32 byte lane.
        std::array<std::uint8_t, 112> bytes{};
        put64(bytes.data(), 0, 4 | (1ull << 60)); put64(bytes.data(), 8, 0xe);
        put64(bytes.data(), 16, 1ull << 48 | 0x1bull << 56); put64(bytes.data(), 24, 0x50);
        put64(bytes.data(), 32, 0); put64(bytes.data(), 40, 0x51);
        put64(bytes.data(), 48, 8 | 2ull << 32); put64(bytes.data(), 56, 0x52);
        put64(bytes.data(), 64, 0); put64(bytes.data(), 72, 0x53);
        put64(bytes.data(), 80, 1 | (1ull << 15) | (2ull << 58));
        put64(bytes.data(), 96, 0x0706050403020100ull); put64(bytes.data(), 104, 0x0f0e0d0c0b0a0908ull);
        hg::GsRegisterState gs; hg::apply_gif_register_transfers(hg::decode_gif_packet(bytes.data(), bytes.size()), gs);
        CHECK((gs.pixel32(0,64,0,0) >> 24) == 0 && (gs.pixel32(0,64,3,0) >> 24) == 3);
        CHECK((gs.pixel32(0,64,0,0) >> 24) == 0 && (gs.pixel32(0,64,7,0) >> 24) == 7);
        CHECK((gs.pixel32(0,64,0,1) >> 24) == 8 && (gs.pixel32(0,64,7,1) >> 24) == 15);
    }
    { // PSMT4HL/HH IMAGE nibbles are low-first in their separate CT32 lanes.
        for (const auto [psm, shift] : {std::pair<std::uint64_t, unsigned>{0x24, 24}, {0x2c, 28}}) {
            std::array<std::uint8_t, 112> bytes{};
            put64(bytes.data(), 0, 4 | (1ull << 60)); put64(bytes.data(), 8, 0xe);
            put64(bytes.data(), 16, 1ull << 48 | psm << 56); put64(bytes.data(), 24, 0x50);
            put64(bytes.data(), 32, 0); put64(bytes.data(), 40, 0x51);
            put64(bytes.data(), 48, 8 | 4ull << 32); put64(bytes.data(), 56, 0x52);
            put64(bytes.data(), 64, 0); put64(bytes.data(), 72, 0x53);
            put64(bytes.data(), 80, 1 | (1ull << 15) | (2ull << 58));
            put64(bytes.data(), 96, 0xfedcba9876543210ull); put64(bytes.data(), 104, 0xfedcba9876543210ull);
            hg::GsRegisterState gs; hg::apply_gif_register_transfers(hg::decode_gif_packet(bytes.data(), bytes.size()), gs);
            CHECK(((gs.pixel32(0,64,0,0) >> shift) & 0xf) == 0 && ((gs.pixel32(0,64,3,0) >> shift) & 0xf) == 3);
            CHECK(((gs.pixel32(0,64,0,1) >> shift) & 0xf) == 8 && ((gs.pixel32(0,64,7,1) >> shift) & 0xf) == 15);
            CHECK(((gs.pixel32(0,64,0,2) >> shift) & 0xf) == 0 && ((gs.pixel32(0,64,7,3) >> shift) & 0xf) == 15);
        }
    }
    { // PRE is a PRIM write; PACKED color uses the tag-reset GIF Q.
        std::array<std::uint8_t, 32> pre{};
        put64(pre.data(), 0, 1 | (1ull << 15) | (1ull << 46) | (0x53ull << 47) | (1ull << 60));
        put64(pre.data(), 8, 0xf);
        auto packet = hg::decode_gif_packet(pre.data(), pre.size()); hg::GsRegisterState gs;
        hg::apply_gif_register_transfers(packet, gs); CHECK(gs.written[0] && gs.value[0] == 0x53);
        std::array<std::uint8_t, 32> packed{}; bool rejected = false;
        put64(packed.data(), 0, 1 | (1ull << 15) | (1ull << 60)); put64(packed.data(), 8, 1);
        put64(packed.data(), 16, 0x0000007800000012ull); put64(packed.data(), 24, 0x00000056);
        hg::apply_gif_register_transfers(hg::decode_gif_packet(packed.data(), packed.size()), gs);
        CHECK(gs.value[1] == 0x3f80000000567812ull);
        // ST's Q applies to later RGBA in its tag, then the next GIFtag resets Q to 1.0.
        std::array<std::uint8_t,80> qtags{};
        put64(qtags.data(),0,1|(2ull<<60));put64(qtags.data(),8,0x12);
        put64(qtags.data(),16,0xabc);put64(qtags.data(),24,0x40400000);
        put64(qtags.data(),32,0x0000000200000001ull);put64(qtags.data(),40,0x00000003);
        put64(qtags.data(),48,1|(1ull<<15)|(1ull<<60));put64(qtags.data(),56,1);
        put64(qtags.data(),64,0x0000000600000004ull);put64(qtags.data(),72,0x00000005);
        const auto qpacket=hg::decode_gif_packet(qtags.data(),qtags.size());
        CHECK(qpacket.transfers.size()==3 && qpacket.transfers[0].reset_q && !qpacket.transfers[1].reset_q && qpacket.transfers[2].reset_q);
        hg::apply_gif_register_transfers(qpacket,gs);
        CHECK(gs.value[1] == 0x3f80000000050604ull);
        // PACKED descriptor 8 is CLAMP_1. The lower 64 bits are written
        // directly and the upper 64 bits are ignored.
        std::array<std::uint8_t,32> clamp_packed{};
        put64(clamp_packed.data(),0,1|(1ull<<15)|(1ull<<60));
        put64(clamp_packed.data(),8,8);
        put64(clamp_packed.data(),16,0x00000400e0801e0aull);
        put64(clamp_packed.data(),24,0xdeadbeefcafebabeull);
        hg::apply_gif_register_transfers(
            hg::decode_gif_packet(clamp_packed.data(),clamp_packed.size()),gs);
        CHECK(gs.written[8] && gs.value[8]==0x00000400e0801e0aull);
        put64(packed.data(), 8, 5);
        hg::apply_gif_register_transfers(hg::decode_gif_packet(packed.data(), packed.size()), gs);
        CHECK(gs.vertex_queue.size()==1);
        put64(packed.data(), 8, 0xb);
        try { hg::apply_gif_register_transfers(hg::decode_gif_packet(packed.data(), packed.size()), gs); }
        catch (const std::runtime_error&) { rejected = true; }
        CHECK(rejected);
    }
    { // Idle status follows completed commands; incomplete packets cannot lie idle.
        hg::Vif1Path vif; hg::GifPath gif; hg::GsRegisterState gs;
        CHECK(vif.read_register(0x10003c00)==0);
        vif.submit_qword(0x07001234,0,gif,gs);
        CHECK(vif.read_register(0x10003c00)==0x40);
        CHECK(vif.read_register(0x10003c30)==0x1234);
        vif.write_register(0x10003c30,0x5678);
        CHECK(vif.read_register(0x10003c00)==0);
        CHECK(vif.read_register(0x10003c30)==0x5678);
        vif.submit_qword(0,std::uint64_t(0x30000000u)<<32,gif,gs);
        bool rejected=false;
        try {vif.read_register(0x10003c00);}
        catch(const std::runtime_error&) {rejected=true;}
        CHECK(rejected);
        vif.submit_qword(0,0,gif,gs);
        CHECK(vif.read_register(0x10003c00)==0);
        vif.consume_state(7,1);
        vif.write_register(0x10003c10,1);
        CHECK(vif.read_register(0x10003c00)==0);
    }
    { // Sampling reads live VRAM without decoding the whole 1024x1024 texture.
        hg::GsRegisterState gs;
        gs.ensure_vram();
        gs.value[6]=(16ull<<14)|(10ull<<26)|(10ull<<30);
        const auto offset=hg::GsRegisterState::psmct32_word(0,1024,17,23);
        gs.vram[offset]=0x12345678;
        CHECK(gs.point_sample_tex0(0,17,23)==0x12345678);
        gs.vram[offset]=0xaabbccdd;
        CHECK(gs.point_sample_tex0(0,17,23)==0xaabbccdd);
        for(unsigned n=0;n<1024;++n)CHECK(gs.point_sample_tex0(0,17,23)==0xaabbccdd);
    }
    { // VIF1 forwards full DIRECT/DIRECTHL qwords through GIF PATH2.
        hg::Vif1Path vif; hg::GifPath gif; hg::GsRegisterState gs;
        // The VIFcode is the last word before its 128-bit payload: three NOPs,
        // then DIRECT (one unit), then an incomplete GIF A+D tag.
        vif.submit_qword(0, std::uint64_t(0x50000001u)<<32, gif, gs);
        vif.submit_qword(0x1000000000008001ull, 0xe, gif, gs);
        CHECK(!gs.written[0x3d]);
        vif.submit_qword(0, std::uint64_t(0x51000001u)<<32, gif, gs);
        vif.submit_qword(0x1234, 0x3d, gif, gs);
        CHECK(gs.written[0x3d] && gs.value[0x3d]==0x1234);
        bool rejected=false;
        try {vif.submit_qword(0,std::uint64_t(0x63000000u)<<32,gif,gs);}
        catch(const std::runtime_error&) {rejected=true;}
        CHECK(rejected);
        rejected=false;
        hg::Vif1Path vif_interrupt;
        try {vif_interrupt.submit_qword(0,std::uint64_t(0xd0000000u)<<32,gif,gs);}
        catch(const std::runtime_error&) {rejected=true;}
        CHECK(rejected);
        rejected=false;
        try {vif.submit_qword(0x14000000u,0,gif,gs);}
        catch(const std::runtime_error&) {rejected=true;}
        CHECK(rejected);
        hg::Vif1Path fills;
        fills.submit_qword(std::uint64_t(0x30000000u)|(std::uint64_t(0x11223344u)<<32),
                           std::uint64_t(0x55667788u)|(std::uint64_t(0x99aabbccu)<<32),gif,gs);
        CHECK(fills.row[0]==0 && fills.row[3]==0); // The fourth word is still pending.
        fills.submit_qword(0xddeeff00u,0,gif,gs);
        CHECK(fills.row[0]==0x11223344 && fills.row[1]==0x55667788 &&
              fills.row[2]==0x99aabbcc && fills.row[3]==0xddeeff00);
        fills.submit_qword(std::uint64_t(0x31000000u)|(std::uint64_t(1u)<<32),
                           std::uint64_t(2u)|(std::uint64_t(3u)<<32),gif,gs);
        fills.submit_qword(4u,0,gif,gs);
        CHECK(fills.column[0]==1 && fills.column[1]==2 && fills.column[2]==3 && fills.column[3]==4);
        hg::Vif1Path mask;
        mask.submit_qword(std::uint64_t(0x20000000u)|(std::uint64_t(0x1b1b1b1bu)<<32),0,gif,gs);
        CHECK(mask.mask==0x1b1b1b1b);
        // V4-32 UNPACK writes VU1 data memory and accepts a payload split at
        // the VIF DMA qword boundary; it does not start VU execution.
        hg::Vif1Path unpack;
        const std::uint32_t unpack_code=0x6c000001u|std::uint32_t(1u<<16);
        unpack.submit_qword(std::uint64_t(unpack_code)|(std::uint64_t(0x11223344u)<<32),
                            std::uint64_t(0x55667788u)|(std::uint64_t(0x99aabbccu)<<32),gif,gs);
        unpack.submit_qword(0xddeeff00u,0,gif,gs);
        CHECK(unpack.vu_mem[4]==0x11223344 && unpack.vu_mem[5]==0x55667788 &&
              unpack.vu_mem[6]==0x99aabbcc && unpack.vu_mem[7]==0xddeeff00);
        hg::Vif1Path unpack16;
        const std::uint32_t unpack16_code=0x6d000002u|std::uint32_t(1u<<16);
        unpack16.submit_qword(std::uint64_t(unpack16_code)|(std::uint64_t(0x0002ffffu)<<32),
                              std::uint64_t(0x7fff8000u),gif,gs);
        unpack16.submit_qword(0,0,gif,gs);
        CHECK(unpack16.vu_mem[8]==0xffffffff && unpack16.vu_mem[9]==2 &&
              unpack16.vu_mem[10]==0xffff8000 && unpack16.vu_mem[11]==0x7fff);
        hg::Vif1Path unpack8;
        const std::uint32_t unpack8_code=0x6e004003u|std::uint32_t(1u<<16); // USN, destination 3
        unpack8.submit_qword(std::uint64_t(unpack8_code)|(std::uint64_t(0xfe807f01u)<<32),0,gif,gs);
        CHECK(unpack8.vu_mem[12]==1 && unpack8.vu_mem[13]==0x7f &&
              unpack8.vu_mem[14]==0x80 && unpack8.vu_mem[15]==0xfe);
        hg::Vif1Path scalar16;
        scalar16.submit_qword(std::uint64_t(0x61010002u)|(std::uint64_t(0x8001u)<<32),0,gif,gs);
        CHECK(scalar16.vu_mem[8]==0xffff8001 && scalar16.vu_mem[9]==0xffff8001 &&
              scalar16.vu_mem[10]==0xffff8001 && scalar16.vu_mem[11]==0xffff8001);
        hg::Vif1Path v2; v2.mask=0xf0; // Z/W preserve: they are indeterminate in V2 input.
        v2.vu_mem[14]=0xaaaaaaaau;v2.vu_mem[15]=0xbbbbbbbbu;
        v2.submit_qword(std::uint64_t(0x74010003u)|(std::uint64_t(0x11223344u)<<32),0x55667788u,gif,gs);
        CHECK(v2.vu_mem[12]==0x11223344 && v2.vu_mem[13]==0x55667788 &&
              v2.vu_mem[14]==0xaaaaaaaa && v2.vu_mem[15]==0xbbbbbbbb);
        CHECK(v2.vu_mem_defined[3]==0x0f);
        hg::Vif1Path v3; v3.mask=0xc0; // W preserves its indeterminate V3 input.
        v3.vu_mem[19]=0xcccccccc;
        v3.submit_qword(std::uint64_t(0x7a010004u)|(std::uint64_t(0xff8001u)<<32),0,gif,gs);
        CHECK(v3.vu_mem[16]==1 && v3.vu_mem[17]==0xffffff80 && v3.vu_mem[18]==0xffffffff &&
              v3.vu_mem[19]==0xcccccccc);
        CHECK(v3.vu_mem_defined[4]==0x0f);
        hg::Vif1Path unguarded_v3; unguarded_v3.vu_mem[16]=0x12345678;
        unguarded_v3.vu_mem[19]=0x89abcdef;
        unguarded_v3.submit_qword(std::uint64_t(0x6a010004u)|(std::uint64_t(0xff8001u)<<32),0,gif,gs);
        CHECK(unguarded_v3.vu_mem[16]==1 && unguarded_v3.vu_mem[17]==0xffffff80 &&
              unguarded_v3.vu_mem[18]==0xffffffff && unguarded_v3.vu_mem[19]==0x89abcdef);
        CHECK(unguarded_v3.vu_mem_defined[4]==0x07);
        unguarded_v3.vu1.write_vi(1,4);
        unguarded_v3.vu1.load_qword(unguarded_v3.vu_mem,unguarded_v3.vu_mem_defined,2,1,0,0xf);
        CHECK(unguarded_v3.vu1.vf[2][0]==1 && unguarded_v3.vu1.vf[2][3]==0x89abcdef &&
              unguarded_v3.vu1.vf_defined[2]==0x07);
        hg::Vif1Path unguarded_v2_16; unguarded_v2_16.cycle=0x0103; // CL=3, WL=1.
        unguarded_v2_16.vu_mem[6]=0xaaaaaaaa;unguarded_v2_16.vu_mem[7]=0xbbbbbbbb;
        unguarded_v2_16.vu_mem[18]=0xcccccccc;unguarded_v2_16.vu_mem[19]=0xdddddddd;
        unguarded_v2_16.submit_qword(std::uint64_t(0x6502c001u)|(std::uint64_t(0x22221111u)<<32),
                                     std::uint64_t(0x44443333u),gif,gs);
        CHECK(unguarded_v2_16.vu_mem[4]==0x1111 && unguarded_v2_16.vu_mem[5]==0x2222 &&
              unguarded_v2_16.vu_mem[6]==0xaaaaaaaa && unguarded_v2_16.vu_mem[7]==0xbbbbbbbb);
        CHECK(unguarded_v2_16.vu_mem[16]==0x3333 && unguarded_v2_16.vu_mem[17]==0x4444 &&
              unguarded_v2_16.vu_mem[18]==0xcccccccc && unguarded_v2_16.vu_mem[19]==0xdddddddd);
        CHECK(unguarded_v2_16.vu_mem_defined[1]==0x03 && unguarded_v2_16.vu_mem_defined[4]==0x03);
        hg::Vif1Path unpack_cycle; unpack_cycle.cycle=0x0102; // CL=2, WL=1: skip every other vector.
        unpack_cycle.submit_qword(std::uint64_t(0x6e020008u)|(std::uint64_t(0x04030201u)<<32),
                                  0x08070605u,gif,gs);
        CHECK(unpack_cycle.vu_mem[32]==1 && unpack_cycle.vu_mem[35]==4 &&
              unpack_cycle.vu_mem[36]==0 && unpack_cycle.vu_mem[40]==5 && unpack_cycle.vu_mem[43]==8);
        hg::Vif1Path additive; additive.mode=1; additive.row={10,20,30,40};
        additive.submit_qword(std::uint64_t(0x6e010004u)|(std::uint64_t(0x04030201u)<<32),0,gif,gs);
        CHECK(additive.vu_mem[16]==11 && additive.vu_mem[17]==22 && additive.vu_mem[18]==33 && additive.vu_mem[19]==44);
        additive.mode=2; additive.row={10,20,30,40};
        additive.submit_qword(std::uint64_t(0x6e010005u)|(std::uint64_t(0x04030201u)<<32),0,gif,gs);
        CHECK(additive.vu_mem[20]==11 && additive.vu_mem[23]==44 && additive.row[0]==11 && additive.row[3]==44);
        additive.submit_qword(std::uint64_t(0x6e010006u)|(std::uint64_t(0x04030201u)<<32),0,gif,gs);
        CHECK(additive.vu_mem[24]==12 && additive.vu_mem[27]==48 && additive.row[0]==12 && additive.row[3]==48);
        hg::Vif1Path masked; masked.mask=0x39; // X=Row, Y=Col, Z=preserve, W=input.
        masked.row={10,20,30,40};masked.column={100,200,300,400};masked.vu_mem[10]=999;
        masked.submit_qword(std::uint64_t(0x7e010002u)|(std::uint64_t(0x04030201u)<<32),0,gif,gs);
        CHECK(masked.vu_mem[8]==10 && masked.vu_mem[9]==100 && masked.vu_mem[10]==999 && masked.vu_mem[11]==4);
        hg::Vif1Path filling; filling.cycle=0x0201; filling.mask=0x7900;
        filling.row={10,20,30,40};filling.column={100,200,300,400};filling.vu_mem[38]=999;
        filling.submit_qword(std::uint64_t(0x7e020008u)|(std::uint64_t(0x04030201u)<<32),0,gif,gs);
        CHECK(filling.vu_mem[32]==1 && filling.vu_mem[35]==4 && filling.vu_mem[36]==10 &&
              filling.vu_mem[37]==200 && filling.vu_mem[38]==999 && filling.vu_mem[39]==40);
        hg::Vif1Path bad_fill; bad_fill.cycle=0x0201;
        rejected=false;
        try {bad_fill.submit_qword(std::uint64_t(0x6e020008u)|(std::uint64_t(0x04030201u)<<32),0,gif,gs);}
        catch(const std::runtime_error&) {rejected=true;}
        CHECK(rejected);
        hg::Vif1Path rgba5;
        rgba5.submit_qword(std::uint64_t(0x6f010001u)|(std::uint64_t(0xfc1fu)<<32),0,gif,gs);
        CHECK(rgba5.vu_mem[4]==0xf8 && rgba5.vu_mem[5]==0 && rgba5.vu_mem[6]==0xf8 && rgba5.vu_mem[7]==0x80);
        { // HG-DIAG-089: straight-line UNPACK equals the generic loop, faults included.
            std::uint64_t seed=0x9e3779b97f4a7c15ull;
            const auto next=[&]{seed^=seed<<13;seed^=seed>>7;seed^=seed<<17;return seed;};
            const unsigned formats[]={0,1,2,4,5,6,8,9,10,12,13,14,15};
            unsigned fast_cases=0;
            for(unsigned trial=0;trial<4000;++trial) {
                const auto format=formats[next()%13];const bool masked=next()%3==0;
                const unsigned bits=format==15?16:(format%4==0)?32:(format%4==1)?16:8;
                const unsigned fields=format<=2?1:format==15?1:format/4+1;
                std::uint32_t cycle=std::uint32_t(next()%4?((next()%4+1)<<8)|(next()%4+1):next()&0xffff);
                const unsigned vectors=unsigned(next()%(next()%4?16:256))+1;
                const unsigned address=unsigned(next()%(next()%8?900:1024));
                const auto code=std::uint32_t(0x60u|(masked?0x10u:0u)|format)<<24|((vectors&255)<<16)|
                    (next()%4==0?0x4000u:0)|(next()%4==0?0x8000u:0)|address;
                auto cl=std::size_t(cycle&0xff),wl=std::size_t(cycle>>8);if(!cl)cl=256;if(!wl)wl=256;
                const auto inputs=cl>=wl?vectors:cl*(vectors/wl)+std::min<std::size_t>(vectors%wl,cl);
                const auto payload=((inputs*(format==15?2:fields*bits/8))+3)&~std::size_t(3);
                std::vector<std::uint8_t> bytes(4+payload);
                std::memcpy(bytes.data(),&code,4);
                for(std::size_t i=4;i<bytes.size();++i)bytes[i]=std::uint8_t(next());
                while(bytes.size()%16)bytes.push_back(0);
                hg::Vif1Path paths[2];
                const auto mask=next()%2?std::uint32_t(next()):0u;const auto mode=unsigned(next()%3);
                std::array<std::uint32_t,4> row{},column{};for(auto& value:row)value=std::uint32_t(next());
                for(auto& value:column)value=std::uint32_t(next());
                const auto tops=unsigned(next()%1024);
                for(auto& path:paths) {
                    path.cycle=cycle;path.mask=mask;path.mode=mode;path.row=row;path.column=column;path.tops=tops;
                    for(unsigned i=0;i<4096;++i)path.vu_mem[i]=i*0x2545f491u;
                    for(unsigned i=0;i<1024;++i)path.vu_mem_defined[i]=std::uint8_t(i&15);
                }
                std::string errors[2];
                for(unsigned which=0;which<2;++which) {
                    hg::vif1_generic_unpack_only=which==1;
                    try {
                        for(std::size_t at=0;at<bytes.size();at+=16) {
                            std::uint64_t low,high;std::memcpy(&low,bytes.data()+at,8);std::memcpy(&high,bytes.data()+at+8,8);
                            paths[which].submit_qword(low,high,gif,gs);
                        }
                    } catch(const std::runtime_error& e){errors[which]=e.what();}
                }
                hg::vif1_generic_unpack_only=false;
                CHECK(errors[0]==errors[1]);
                CHECK(paths[0].vu_mem==paths[1].vu_mem && paths[0].vu_mem_defined==paths[1].vu_mem_defined);
                CHECK(paths[0].row==paths[1].row && paths[0].pending==paths[1].pending);
                fast_cases+=format!=15&&cl>=wl&&(!masked||!mask)&&errors[0].empty();
            }
            CHECK(fast_cases>800);
        }
        hg::Vif1Path tops; tops.base=20; tops.consume_state(0x02,30);
        CHECK(tops.top==20 && tops.tops==20 && tops.offset==30 && !tops.dbf);
        tops.submit_qword(std::uint64_t(0x6e018004u)|(std::uint64_t(0x04030201u)<<32),0,gif,gs);
        CHECK(tops.vu_mem[96]==1 && tops.vu_mem[99]==4);
        hg::Vif1Path mscal;
        mscal.consume_state(0x03,8); // BASE
        mscal.consume_state(0x02,4); // OFFSET: TOP/TOPS=BASE, DBF=0
        mscal.consume_state(0x04,77); // ITOPS
        for(unsigned lane=0;lane!=4;++lane)mscal.vu_mem[8*4+lane]=0x100u+lane;
        mscal.vu1_executor=+[](hg::Vif1Path& v,std::uint16_t entry,hg::GifPath&,hg::GsRegisterState&) {
            if(entry!=0)throw std::runtime_error("unexpected synthetic VU1 entry");
            v.vu1.write_vi(1,std::uint16_t(v.top)); // XTOP VI1
            v.vu1.clip=0; // FCSET 0
            v.vu1.load_qword(v.vu_mem,v.vu_mem_defined,1,1,0,0xf); // LQ.xyzw VF1,0(VI1)
            v.vu1.tpc=0x10; // synthetic E-bit pair plus one delay pair
        };
        mscal.submit_qword(0x14000000u,0,gif,gs);
        CHECK(mscal.itop==77 && mscal.top==8 && mscal.tops==12 && mscal.dbf);
        CHECK(mscal.vu1.read_vi(1)==8 && mscal.vu1.vf[1][0]==0x100u &&
              mscal.vu1.vf[1][3]==0x103u && mscal.vu1.clip==0 && mscal.vu1.tpc==0x10);
        CHECK(mscal.vu1.vf_defined[1]==0x0f);
        mscal.submit_qword(0x14000000u,0,gif,gs);
        CHECK(mscal.top==12 && mscal.tops==8 && !mscal.dbf);
        mscal.vu1.tpc=0x60;
        mscal.vu1_executor=+[](hg::Vif1Path& v,std::uint16_t entry,hg::GifPath&,hg::GsRegisterState&) {
            if(entry!=0x60)throw std::runtime_error("unexpected synthetic VU1 continuation entry");
            v.vu1.tpc=0x98;
        };
        mscal.submit_qword(0x17000000u,0,gif,gs);
        CHECK(mscal.itop==77 && mscal.top==8 && mscal.tops==12 && mscal.dbf && mscal.vu1.tpc==0x98);
        { // Host overlap queues activation, permits UNPACK, but retains a real FLUSH boundary.
            hg::Vif1Path overlap;overlap.host_overlap_enabled=true;
            unsigned executed=0;
            overlap.vu1_executor=+[](hg::Vif1Path& v,std::uint16_t,hg::GifPath&,hg::GsRegisterState&) {
                v.vu1.write_vi(1,0x1234);
            };
            overlap.consume_state(0x03,0);overlap.consume_state(0x02,0x200);
            overlap.submit_qword(0x14000000u,0,gif,gs);
            CHECK(overlap.host_vu_pending && !overlap.host_vu_running && overlap.vu1.read_vi(1)==0);
            overlap.host_vu_pending=false;overlap.host_vu_running=true;
            // V1-32 one vector at address4: write X while the VU snapshot runs elsewhere.
            overlap.submit_qword(std::uint64_t(0x60010004u)|(std::uint64_t(0x11u)<<32),0,gif,gs);
            CHECK(!overlap.host_vu_stalled && overlap.vu_mem[16]==0x11u);
            overlap.submit_qword(0x11000000u,0,gif,gs);
            CHECK(overlap.host_vu_stalled && !overlap.pending.empty());
            overlap.host_vu_running=false;overlap.host_vu_stalled=false;
            overlap.process_pending(gif,gs);
            CHECK(overlap.pending.empty());
            (void)executed;
        }
        hg::Vif1Path no_mscnt_executor;
        no_mscnt_executor.vu1.tpc=0x60;
        rejected=false;
        try {no_mscnt_executor.submit_qword(0x17000000u,0,gif,gs);} catch(const std::runtime_error&) {rejected=true;}
        CHECK(rejected);
        { // Focused VU1 arithmetic/memory semantics used by the HG 95-pair program.
            hg::Vif1Path vu;
            auto& s=vu.vu1;
            s.write_vi(0,0xffff);CHECK(s.read_vi(0)==0);
            s.write_vi(15,0xffff);s.integer_add(2,15,2);CHECK(s.read_vi(2)==1);

            s.write_vf(3,2,0x1234abcd);s.mtir(4,3,2);CHECK(s.read_vi(4)==0xabcdu);
            s.vf[4][3]=0xdeadbeefu;s.vf_defined[4]&=std::uint8_t(~8u);s.move_vector(5,4,1);
            CHECK(s.vf[5][3]==0xdeadbeefu && !(s.vf_defined[5]&8u));
            s.vf_defined[3]&=std::uint8_t(~(1u<<1));
            rejected=false;
            try {s.mtir(4,3,1);} catch(const std::runtime_error&) {rejected=true;}
            CHECK(rejected);

            s.write_vi(5,10);
            s.vf[6]={0x10,0x20,0x30,0x40};s.vf_defined[6]=0x0d;
            vu.vu_mem_defined[10]=0;vu.vu_mem_defined[11]=0;
            s.store_qword(vu.vu_mem,vu.vu_mem_defined,6,5,1,0xa); // X/Z at vector 11.
            CHECK(vu.vu_mem[44]==0x10 && vu.vu_mem[46]==0x30 && vu.vu_mem_defined[11]==0x05);
            s.store_qword_increment(vu.vu_mem,vu.vu_mem_defined,6,5,0x5); // Y/W at vector 10.
            CHECK(s.read_vi(5)==11 && vu.vu_mem[41]==0x20 && vu.vu_mem[43]==0x40 &&
                  vu.vu_mem_defined[10]==0x08);
            s.write_vi(7,0xbeef);s.write_vi(8,12);
            s.store_integer(vu.vu_mem,vu.vu_mem_defined,7,8,-1,0x9);
            CHECK(vu.vu_mem[44]==0xbeef && vu.vu_mem[47]==0xbeef && vu.vu_mem_defined[11]==0x0d);
            vu.vu_mem[45]=0x1234cafeu;vu.vu_mem_defined[11]|=0x02;s.load_integer(vu.vu_mem,vu.vu_mem_defined,9,8,-1,4);
            CHECK(s.read_vi(9)==0xcafeu);
            rejected=false;try{s.load_integer(vu.vu_mem,vu.vu_mem_defined,9,8,-1,12);}catch(const std::runtime_error&){rejected=true;}
            CHECK(rejected);

            s.q=0x3f000000u;s.write_vf(9,0,0x40800000u);s.write_vf(10,0,0x40000000u);
            s.divide(9,10,0,0);CHECK(s.q_pending && s.q_cycles_remaining==7 && s.q==0x3f000000u);
            for(unsigned tick=0;tick!=6;++tick){s.tick_q();CHECK(s.q==0x3f000000u && s.q_pending);}
            s.tick_q();CHECK(!s.q_pending && s.q==0x40000000u);
            s.write_vf(9,0,0x40c00000u);s.divide(9,10,0,0);s.wait_q();
            CHECK(!s.q_pending && s.q_cycles_remaining==0 && s.q==0x40400000u);

            s.vf[11]={16u,0xffffffe0u,0x13579bdfu,0x2468ace0u};s.vf_defined[11]=0x0f;
            s.vf[12]={0,0,0xaaaaaaaau,0xbbbbbbbbu};s.vf_defined[12]=0x0f;
            s.convert_integer(12,11,0xc,4);
            CHECK(s.vf[12][0]==0x3f800000u && s.vf[12][1]==0xc0000000u &&
                  s.vf[12][2]==0xaaaaaaaau && s.vf[12][3]==0xbbbbbbbbu);
            s.vf[13]={0x3fc00000u,0xc0100000u,0,0};s.vf_defined[13]=0x0f;
            s.convert_fixed(14,13,0xc,4);
            CHECK(s.vf[14][0]==24u && s.vf[14][1]==std::uint32_t(-36));
            s.vf_defined[13]&=std::uint8_t(~1u);rejected=false;
            try {s.convert_fixed(14,13,8,4);} catch(const std::runtime_error&) {rejected=true;}
            CHECK(rejected);

            for(unsigned lane=0;lane!=4;++lane)s.write_vf(16,lane,0x3f800000u);
            s.write_vf(17,0,0x40000000u);s.write_vf(18,0,0x40400000u);
            s.multiply_acc(16,17,0xf,0,false); // 1*2 => ACC=2.
            s.multiply_acc(16,18,0xf,0,true);  // +1*3 => ACC=5.
            s.madd_vector(19,16,17,0xf,0);      // ACC + 1*2 => 7.
            for(unsigned lane=0;lane!=4;++lane)CHECK(s.acc[lane]==0x40a00000u && s.vf[19][lane]==0x40e00000u);
            s.multiply_vector(19,16,18,0xe,0);  // XYZ: 1*3 => 3; W retained.
            CHECK(s.vf[19][0]==0x40400000u && s.vf[19][1]==0x40400000u &&
                  s.vf[19][2]==0x40400000u && s.vf[19][3]==0x40e00000u);
            s.vf[24]={0x3f800000u,0x40000000u,0x40400000u,0x40800000u};s.vf_defined[24]=0x0f;
            s.vf[25]={0x40000000u,0x40000000u,0x40000000u,0x40000000u};s.vf_defined[25]=0x0f;
            s.add_vector(26,24,25,0xf,false);CHECK(s.vf[26][0]==0x40400000u && s.vf[26][3]==0x40c00000u);
            s.add_vector_broadcast(26,24,25,8,1,false);CHECK(s.vf[26][0]==0x40400000u);
            s.add_vector(26,24,25,8,true);CHECK(s.vf[26][0]==0xbf800000u);
            s.multiply_vector_lanes(26,24,25,0xf);CHECK(s.vf[26][2]==0x40c00000u);
            s.acc={0x3f800000u,0x3f800000u,0x3f800000u,0x3f800000u};s.acc_defined=0x0f;
            s.madd_vector_lanes(26,24,25,8);CHECK(s.vf[26][0]==0x40400000u);
            s.add_acc(24,25,8,3,false);CHECK(s.acc[0]==0x40400000u);

            s.p=0x40000000u;s.vf[27]={0x3f800000u,0,0,0};s.vf_defined[27]=0x0f;
            s.erleng(27);CHECK(s.p==0x40000000u && s.p_pending && s.p_cycles_remaining==24);
            for(unsigned tick=0;tick!=23;++tick){s.tick_p();CHECK(s.p==0x40000000u && s.p_pending);}
            s.tick_p();CHECK(!s.p_pending && s.p==0x3f800000u);
            s.move_from_p(28,1);CHECK(s.vf[28][3]==0x3f800000u);
            s.vf[27][0]=0x40800000u;s.erleng(27);for(unsigned tick=0;tick!=3;++tick)s.tick_p();
            s.vf[27][0]=0x3f800000u;s.erleng(27);CHECK(s.p==0x3e800000u && s.p_pending);
            s.q=0x40000000u;s.arithmetic_q(20,16,0xf,true);
            for(unsigned lane=0;lane!=4;++lane)CHECK(s.vf[20][lane]==0x40000000u);
            s.q=0x3f800000u;s.arithmetic_q(21,20,0xf,false);
            for(unsigned lane=0;lane!=4;++lane)CHECK(s.vf[21][lane]==0x40400000u);

            s.clip=1;s.vf[22]={0x40000000u,0xc0000000u,0,0};s.vf_defined[22]=0x0f;
            s.vf[23][3]=0x3f800000u;s.vf_defined[23]=0x0f;
            s.clip_test(22,23);CHECK(s.clip==0x49u);
            s.fcand(0x8);CHECK(s.read_vi(1)==1);s.fcand(0x20);CHECK(s.read_vi(1)==0);

            const std::size_t packet_vector=20;
            const std::array<std::uint64_t,4> packet_words={
                0x1000000000008001ull,0xeull,0x1234ull,0x3dull};
            for(unsigned qword=0;qword!=2;++qword) {
                vu.vu_mem[(packet_vector+qword)*4+0]=std::uint32_t(packet_words[qword*2]);
                vu.vu_mem[(packet_vector+qword)*4+1]=std::uint32_t(packet_words[qword*2]>>32);
                vu.vu_mem[(packet_vector+qword)*4+2]=std::uint32_t(packet_words[qword*2+1]);
                vu.vu_mem[(packet_vector+qword)*4+3]=std::uint32_t(packet_words[qword*2+1]>>32);
                vu.vu_mem_defined[packet_vector+qword]=0x0f;
            }
            s.write_vi(2,std::uint16_t(packet_vector));vu.xgkick(2,gif,gs);
            CHECK(gs.written[0x3d] && gs.value[0x3d]==0x1234 && gif.pending.empty());
            { // PACKED STQ consumes S/T/Q only; its fourth 32-bit word is padding.
                hg::Vif1Path packed;hg::GifPath packed_gif;hg::GsRegisterState packed_gs;
                constexpr std::size_t start=30;
                const std::uint64_t tag_low=1ull|(1ull<<15)|(1ull<<60);
                packed.vu_mem[start*4+0]=std::uint32_t(tag_low);
                packed.vu_mem[start*4+1]=std::uint32_t(tag_low>>32);
                packed.vu_mem[start*4+2]=2;packed.vu_mem[start*4+3]=0;
                packed.vu_mem_defined[start]=0x0f;
                packed.vu_mem[(start+1)*4+0]=0x3f000000u;
                packed.vu_mem[(start+1)*4+1]=0x3f400000u;
                packed.vu_mem[(start+1)*4+2]=0x3f800000u;
                packed.vu_mem_defined[start+1]=0x07;
                packed.vu1.write_vi(2,std::uint16_t(start));
                packed.xgkick(2,packed_gif,packed_gs);
                CHECK(packed_gif.pending.empty() && packed_gs.value[2]==0x3f4000003f000000ull &&
                      packed_gs.gif_q==0x3f800000u);
                packed.vu_mem_defined[start+1]=0x03;bool missing_q=false;
                try {packed.xgkick(2,packed_gif,packed_gs);} catch(const std::runtime_error&){missing_q=true;}
                CHECK(missing_q);
            }
            hg::Vif1Path bad_xgkick;bad_xgkick.vu1.write_vi(2,1024);rejected=false;
            try {bad_xgkick.xgkick(2,gif,gs);} catch(const std::runtime_error&) {rejected=true;}
            CHECK(rejected);
            bad_xgkick.vu1.write_vi(2,20);bad_xgkick.vu_mem_defined[20]=0;rejected=false;
            try {bad_xgkick.xgkick(2,gif,gs);} catch(const std::runtime_error&) {rejected=true;}
            CHECK(rejected);
        }
        // MPG stores 64-bit microinstructions across DMA qwords but does not
        // execute them; activation remains a separate checked boundary.
        hg::Vif1Path mpg;
        mpg.submit_qword(std::uint64_t(0)|(std::uint64_t(0x4a020003u)<<32),
                         std::uint64_t(0x11223344u)|(std::uint64_t(0x55667788u)<<32),gif,gs);
        mpg.submit_qword(std::uint64_t(0xddeeff00u)|(std::uint64_t(0x99aabbccu)<<32),0,gif,gs);
        CHECK(mpg.vu_micro_mem[3]==0x5566778811223344ull &&
              mpg.vu_micro_mem[4]==0x99aabbccddeeff00ull);
        rejected=false;
        try {hg::Vif1Path unaligned; unaligned.submit_qword(0x4a010000u,0,gif,gs);}
        catch(const std::runtime_error&) {rejected=true;}
        CHECK(rejected);
        rejected=false;
        try {mpg.submit_qword(0,std::uint64_t(0x4a01ffffu)<<32,gif,gs);} catch(const std::runtime_error&) {rejected=true;}
        CHECK(rejected);
        rejected=false;
        try {unpack.submit_qword(0x63000001u,0,gif,gs);} catch(const std::runtime_error&) {rejected=true;}
        CHECK(rejected);
    }
    { // PRIM resets the vertex queue; XYZ2 kicks a documented triangle draw.
        hg::GsRegisterState gs;
        gs.write_ad(0,3|(1u<<3)|(1u<<4)|(1u<<8));gs.write_ad(0x18,(16ull<<32)|16ull);gs.write_ad(1,0x11223344);gs.write_ad(2,0x55667788);gs.write_ad(3,(0x20u<<16)|0x10u);
        gs.write_ad(5,0x0000000300020001ull);gs.write_ad(5,0x0000000600050004ull);
        CHECK(gs.draws.empty());
        gs.write_ad(5,0x0000000900080007ull);
        CHECK(gs.draws.size()==1 && gs.draws[0].primitive==3 && gs.draws[0].prim_state==(3|(1u<<3)|(1u<<4)|(1u<<8)) && gs.draws[0].count==3);
        CHECK(gs.draws[0].vertices[0].x==1 && gs.draws[0].vertices[1].y==5 && gs.draws[0].vertices[2].z==9);
        const auto geometry=hg::presentation_geometry(gs.draws[0]);
        CHECK(geometry.topology==hg::HostTopology::triangles && geometry.textured && geometry.fixed_texture_coordinates && geometry.gouraud && geometry.vertices.size()==3);
        CHECK(geometry.vertices[0].x==-0.9375f && geometry.vertices[0].y==-0.875f && geometry.vertices[0].u==1.0f && geometry.vertices[0].v==2.0f && geometry.vertices[2].z==9.0f);
        gs.write_ad(0,2);gs.write_ad(0xc,0x0000000c000b000aull);
        CHECK(gs.vertex_queue.size()==1 && gs.draws.size()==1);
        hg::GsRegisterState modes;
        modes.write_ad(0,0);modes.write_ad(0x1a,0); // PRMODE supplies attributes, PRIM supplies type.
        modes.write_ad(0x1b,(1u<<4)|(1u<<8)|(1u<<9));modes.write_ad(0x19,0x1234);
        modes.write_ad(5,0);
        CHECK(modes.draws.size()==1&&modes.draws[0].primitive==0&&
              modes.draws[0].prim_state==((1u<<4)|(1u<<8)|(1u<<9))&&modes.draws[0].xyoffset==0x1234);
        modes.write_ad(0x1a,1);modes.write_ad(0,(1u<<5));modes.write_ad(5,0);
        CHECK(modes.draws.back().prim_state==(1u<<5));
    }
    { // TEX0 extracts checked PSMCT32/24 texels from swizzled GS local memory.
        hg::GsRegisterState gs;gs.ensure_vram();
        constexpr auto tex2_mask=(std::uint64_t{0x3f}<<20)|(~std::uint64_t{0}<<37);
        const std::uint64_t tex0_only=0x12345ull|(7ull<<14)|(6ull<<26)|(5ull<<30)|(1ull<<34)|(2ull<<35);
        const std::uint64_t tex2_fields=(0x14ull<<20)|(0x321ull<<37)|(10ull<<51)|(17ull<<56)|(3ull<<61);
        gs.write_ad(6,tex0_only);gs.write_ad(0x16,tex2_fields);
        CHECK(gs.value[6]==((tex0_only&~tex2_mask)|(tex2_fields&tex2_mask))&&gs.value[0x16]==tex2_fields);
        gs.write_ad(7,0x55aaull);gs.write_ad(0x17,1ull<<20);
        CHECK(gs.value[7]==((0x55aaull&~tex2_mask)|(1ull<<20))&&gs.value[6]!=gs.value[7]);
        // Set TA0/TA1 explicitly: RGB24 and RGBA16 alpha come from TEXA.
        gs.write_ad(0x3b,0xffull|(0xffull<<32));
        const auto p00=hg::GsRegisterState::psmct32_word(0,64,0,0),p10=hg::GsRegisterState::psmct32_word(0,64,1,0);
        const auto p01=hg::GsRegisterState::psmct32_word(0,64,0,1),p11=hg::GsRegisterState::psmct32_word(0,64,1,1);
        gs.vram[p00]=0x11223344;gs.vram[p10]=0x55667788;gs.vram[p01]=0x99aabbcc;gs.vram[p11]=0x00ddeeff;
        gs.write_ad(6,(1ull<<14)|(1ull<<26)|(1ull<<30));
        auto texture=gs.texture_from_tex0(0);
        CHECK(texture.width==2 && texture.height==2 && texture.rgba[0]==0x11223344 && texture.rgba[3]==0x00ddeeff);
        gs.write_ad(0x16,1ull<<20); // TEX2 changes PSM while preserving TEX0 dimensions/TBW.
        texture=gs.texture_from_tex0(0);
        CHECK(texture.rgba[0]==0xff223344 && texture.rgba[3]==0xffddeeff);
        const auto p16=hg::GsRegisterState::psmct16_word(0,64,0,0);
        gs.vram[p16]=0xfc1f; // R=31, G=0, B=31, A=1.
        gs.write_ad(6,(1ull<<14)|(2ull<<20)|(1ull<<26)|(1ull<<30));
        texture=gs.texture_from_tex0(0);
        CHECK(texture.rgba[0]==0xffff00ff);
        // TEXA selects alpha for RGB24/RGBA16; AEM makes an all-zero RGB
        // RGBA16 texel transparent without changing the non-zero case.
        gs.write_ad(0x3b,0x34ull<<32|0x12);
        gs.vram[p00]=0x00112233;
        gs.write_ad(6,(1ull<<14)|(1ull<<20)|(1ull<<26)|(1ull<<30));
        texture=gs.texture_from_tex0(0);
        CHECK(texture.rgba[0]==0x12112233);
        // PSMZ24 texture fetches use the documented Z24 swizzle and the same
        // 24-bit TEXA expansion as other 24-bit TEX0 storage.
        gs.write_transfer_pixel(0x31,0,64,0,0,0x00445566);
        gs.write_ad(6,(1ull<<14)|(0x31ull<<20)|(1ull<<26)|(1ull<<30));
        texture=gs.texture_from_tex0(0);
        CHECK(texture.rgba[0]==0x12445566);
        gs.vram[p16]=0x7c1f; // R=31, A=0.
        gs.write_ad(6,(1ull<<14)|(2ull<<20)|(1ull<<26)|(1ull<<30));
        texture=gs.texture_from_tex0(0);
        CHECK(texture.rgba[0]==0x12ff00ff);
        gs.vram[p16]=0xfc1f; // R=31, A=1.
        texture=gs.texture_from_tex0(0);
        CHECK(texture.rgba[0]==0x34ff00ff);
        gs.write_ad(0x3b,(0x34ull<<32)|0x12|(1ull<<15));
        gs.vram[p16]=0;
        texture=gs.texture_from_tex0(0);
        CHECK(texture.rgba[0]==0);
        gs.write_ad(0x3b,0xffull|(0xffull<<32));
        // CSM2 PSMCT16 CLUT maps PSMT8 indices through its documented row.
        const auto index_word=hg::GsRegisterState::psmt8_word(0,128,0,0);
        gs.vram[index_word]=3;
        const auto clut_word=hg::GsRegisterState::psmct16_word(1024,64,3,0);
        gs.vram[clut_word]=(gs.vram[clut_word]&0xffff0000u)|0x83e0; // G=31, B=0, R=0, A=1.
        gs.write_ad(0x1c,4); // CBW=256, COU=COV=0; the whole IDTEX8 row fits.
        gs.write_ad(6,(2ull<<14)|(0x13ull<<20)|(16ull<<37)|(2ull<<51)|(1ull<<55)|(1ull<<61));
        texture=gs.texture_from_tex0(0);
        CHECK(texture.rgba[0]==0xff00ff00);
        // PSMT4 uses the same CSM2 lookup, with a four-bit source index.
        const auto nibble_word=hg::GsRegisterState::psmt4_word(0,128,0,0);
        gs.vram[nibble_word]=(gs.vram[nibble_word]&~0xfu)|5u;
        const auto clut4_word=hg::GsRegisterState::psmct16_word(1024,64,5,0);
        gs.vram[clut4_word]=(gs.vram[clut4_word]&0xffff0000u)|0x801f;
        gs.write_ad(6,(2ull<<14)|(0x14ull<<20)|(16ull<<37)|(2ull<<51)|(1ull<<55)|(1ull<<61));
        texture=gs.texture_from_tex0(0);
        CHECK(texture.rgba[0]==0xff0000ff);
        // CSM2 accepts PSMCT16S CLUT storage using its separate swizzle.
        const auto clut16s_word=hg::GsRegisterState::psmct16s_word(1024,64,5,0);
        gs.vram[clut16s_word]=(gs.vram[clut16s_word]&0xffff0000u)|0x801f;
        gs.write_ad(6,(2ull<<14)|(0x14ull<<20)|(16ull<<37)|(10ull<<51)|(1ull<<55)|(1ull<<61));
        texture=gs.texture_from_tex0(0);
        CHECK(texture.rgba[0]==0xff0000ff);
        // PSMT8H/PSMT4HL/PSMT4HH retain their indices in CT32's documented
        // high-byte/high-nibble lanes and otherwise use the same CSM2 CLUT.
        gs.vram[p00]=(gs.vram[p00]&0x00ffffffu)|(3u<<24);
        gs.write_ad(6,(2ull<<14)|(0x1bull<<20)|(16ull<<37)|(2ull<<51)|(1ull<<55)|(1ull<<61));
        texture=gs.texture_from_tex0(0);
        CHECK(texture.rgba[0]==0xff00ff00);
        gs.vram[p00]=(gs.vram[p00]&0xf0ffffffu)|(5u<<24);
        gs.write_ad(6,(2ull<<14)|(0x24ull<<20)|(16ull<<37)|(2ull<<51)|(1ull<<55)|(1ull<<61));
        texture=gs.texture_from_tex0(0);
        CHECK(texture.rgba[0]==0xff0000ff);
        gs.vram[p00]=(gs.vram[p00]&0x0fffffffu)|(5u<<28);
        gs.write_ad(6,(2ull<<14)|(0x2cull<<20)|(16ull<<37)|(2ull<<51)|(1ull<<55)|(1ull<<61));
        texture=gs.texture_from_tex0(0);
        CHECK(texture.rgba[0]==0xff0000ff);
        // CSM1's IDTEX8 table is a 16x16 permutation, not a linear palette.
        gs.vram[index_word]=(gs.vram[index_word]&0xffffff00u)|0x28u;
        const auto csm1_16_word=hg::GsRegisterState::psmct16_word(1024,64,0,3);
        gs.vram[csm1_16_word]=(gs.vram[csm1_16_word]&0xffff0000u)|0xffe0;
        gs.write_ad(6,(2ull<<14)|(0x13ull<<20)|(16ull<<37)|(2ull<<51)|(1ull<<61));
        texture=gs.texture_from_tex0(0);
        CHECK(texture.rgba[0]==0xffffff00);
        const auto csm1_32_word=hg::GsRegisterState::psmct32_word(1024,64,0,3);
        gs.vram[csm1_32_word]=0x10203040;
        gs.write_ad(6,(2ull<<14)|(0x13ull<<20)|(16ull<<37)|(1ull<<61));
        texture=gs.texture_from_tex0(0);
        CHECK(texture.rgba[0]==0x10203040);
        // Point sampling applies all four documented CLAMP modes before the
        // checked TEX0 conversion path.
        for(std::uint32_t y=0;y<4;++y)for(std::uint32_t x=0;x<4;++x)
            gs.vram[hg::GsRegisterState::psmct32_word(0,64,x,y)]=0xff000000u|(y<<8)|x;
        gs.write_ad(6,(1ull<<14)|(2ull<<26)|(2ull<<30)); // 4x4 PSMCT32
        gs.write_ad(8,0); // REPEAT both axes
        CHECK(gs.point_sample_tex0(0,-1,-1)==0xff000303);
        gs.write_ad(8,1); // CLAMP U, REPEAT V
        CHECK(gs.point_sample_tex0(0,99,-1)==0xff000303);
        gs.write_ad(8,2|(1ull<<4)|(2ull<<14)); // REGION_CLAMP U=[1,2]
        CHECK(gs.point_sample_tex0(0,-9,0)==0xff000001);
        // REGION_CLAMP limits are independent 10-bit coordinates, not bounded
        // by TEX0.TW/TH. The game uses this for a 256-wide texture with MAXU=511.
        gs.vram[hg::GsRegisterState::psmct32_word(0,64,5,0)]=0xff000005;
        gs.write_ad(8,2|(7ull<<14)); // 4x4 TEX0, REGION_CLAMP U=[0,7].
        CHECK(gs.point_sample_tex0(0,5,0)==0xff000005);
        gs.write_ad(8,3|(1ull<<4)|(2ull<<14)); // REGION_REPEAT U=(u&1)|2
        CHECK(gs.point_sample_tex0(0,1,0)==0xff000003);
        gs.write_ad(6,(1ull<<14)|(2ull<<26)|(2ull<<30)|(1ull<<35)); // DECAL, TCC=RGB
        CHECK(gs.shade_point_tex0(0,1,0,0x55000000u)==0x55000003u);
        gs.write_ad(6,(1ull<<14)|(2ull<<26)|(2ull<<30)|(1ull<<34)|(1ull<<35)); // DECAL, TCC=RGBA
        CHECK(gs.shade_point_tex0(0,1,0,0x55000000u)==0xff000003u);
    }
    { // TEX0 texture functions use the GS's 0x80-unity multiplication rule.
        constexpr auto texel=0x40204080u,fragment=0x600ac880u;
        CHECK(hg::gs_texture_function(texel,fragment,0,false)==0x60026480u);
        CHECK(hg::gs_texture_function(texel,fragment,0,true)==0x30026480u);
        CHECK(hg::gs_texture_function(0xffffffffu,0xffffffffu,0,true)==0xffffffffu);
        // Saturation must not wrap alpha or spill a bright channel into its neighbor.
        CHECK(hg::gs_texture_function(0x000000ffu,0x808080ffu,0,false)==0x800000ffu);
        CHECK(hg::gs_texture_function(0x0000ff00u,0x8080ff80u,0,false)==0x8000ff00u);
        CHECK(hg::gs_texture_function(0x00ff0000u,0x80ff8080u,0,false)==0x80ff0000u);
        CHECK(hg::gs_texture_function(0xff000000u,0xff808080u,0,true)==0xff000000u);
        CHECK(hg::gs_texture_function(texel,fragment,1,false)==0x60204080u);
        CHECK(hg::gs_texture_function(texel,fragment,1,true)==0x40204080u);
        CHECK(hg::gs_texture_function(texel,fragment,2,false)==0x6062c4e0u);
        CHECK(hg::gs_texture_function(texel,fragment,2,true)==0xa062c4e0u);
        CHECK(hg::gs_texture_function(texel,fragment,3,false)==0x6062c4e0u);
        CHECK(hg::gs_texture_function(texel,fragment,3,true)==0x4062c4e0u);
    }
    { // ALPHA keeps unclamped RGB until the later framebuffer-write stage.
        const auto normal=hg::gs_alpha_blend(0x402864c8u,0x80f0a028u,0x44); // (Cs-Cd)*As+Cd
        CHECK(normal.r==120 && normal.g==130 && normal.b==140 && normal.a==0x40);
        const auto bright=hg::gs_alpha_blend(0x400000ffu,0,(0xffull<<32)|0xa8); // Cs*FIX + 0
        CHECK(bright.r==508 && bright.g==0 && bright.b==0 && bright.a==0x40);
    }
    { // Fog is RGB-only and uses two independent unsigned >>8 products.
        CHECK(hg::gs_apply_fog(0xa0804020,0xff,0x00112233)==0xa07f3f1f);
        CHECK(hg::gs_apply_fog(0xa0804020,0,0x00112233)==0xa0102132);
        CHECK(hg::gs_apply_fog(0xa0804020,0x80,0x00112233)==0xa0483029);
    }
    { // TEST's alpha, destination-alpha, and depth gates retain their GS modes.
        CHECK(!hg::gs_alpha_test(4,1)); // ATE + NEVER
        CHECK(hg::gs_alpha_test(4,1|(5ull<<1)|(4ull<<4))); // GEQUAL AREF
        CHECK(!hg::gs_alpha_test(3,1|(5ull<<1)|(4ull<<4)));
        CHECK(hg::gs_destination_alpha_test(false,0,1ull<<14));
        CHECK(!hg::gs_destination_alpha_test(true,0,1ull<<14));
        CHECK(hg::gs_destination_alpha_test(true,0,(1ull<<14)|(1ull<<15)));
        CHECK(hg::gs_destination_alpha_test(false,1,(1ull<<14)|(1ull<<15))); // RGB24 ignores DATE
        CHECK(!hg::gs_depth_test(4,5,1ull<<16)); // NEVER
        CHECK(hg::gs_depth_test(5,5,(1ull<<16)|(2ull<<17))); // GEQUAL
        CHECK(!hg::gs_depth_test(5,5,(1ull<<16)|(3ull<<17))); // GREATER
    }
    { // PSMCT32 framebuffer write clamps/corrects/masks after the pixel stages.
        hg::GsRegisterState gs; gs.ensure_vram();
        const auto location=hg::GsRegisterState::psmct32_word(0,64,0,0);
        gs.vram[location]=0x11223344;
        gs.write_ad(0x4c,(1ull<<16)|(0x0000ff00ull<<32)); // FBW=64, preserve G
        gs.write_ad(0x46,1); gs.write_ad(0x4a,1); // clamp RGB, force alpha bit 7
        gs.write_frame_pixel(0,0,0,{300,-2,20,1});
        CHECK(gs.vram[location]==0x811433ff);
        gs.write_ad(0x4c,1ull<<16); gs.write_ad(0x46,0); // RGB lower-eight-bit mask
        gs.write_frame_pixel(0,0,0,{-1,256,257,0});
        CHECK(gs.vram[location]==0x800100ff);
        gs.vram[location]=0xab010203;
        gs.write_ad(0x4c,(1ull<<16)|(1ull<<24)|(0x0000ff00ull<<32)); // RGB24, preserve G only
        gs.write_ad(0x46,1); gs.write_ad(0x4a,1); // FBA must not alter RGB24's unused alpha byte.
        gs.write_frame_pixel(0,0,0,{0x11,0x22,0x33,0x44});
        CHECK(gs.vram[location]==0xab330211);
        gs.write_ad(0x4c,(1ull<<16)|(1ull<<24));
        gs.write_ad(0x4e,0); gs.write_ad(0x47,(1ull<<16)|(1ull<<17)); // ZTE + ALWAYS
        CHECK(gs.draw_depth_frame_pixel(0,0,0,0x00112233,4));
        CHECK(gs.vram[location]==0xab112233 && gs.read_z_pixel(0,0,0)==4);
        gs.write_ad(0x4c,(1ull<<16)|(2ull<<24)); // PSMCT16, no FBMSK
        gs.write_ad(0x45,0); gs.write_ad(0x4a,0);
        gs.write_frame_pixel(0,0,0,{0xff,0x80,0x08,0x81});
        CHECK(gs.pixel16(0,64,0,0)==0x861f);
        gs.write_ad(0x4a,1); gs.write_frame_pixel(0,0,0,{0,0,0,0});
        CHECK(gs.pixel16(0,64,0,0)==0x8000);
        gs.write_ad(0x4a,0); gs.write_ad(0x45,0); gs.write_ad(0x4c,(1ull<<16)|(2ull<<24)|(1ull<<35)); // mask source R bit3
        gs.write_frame_pixel(0,0,0,{0,0,0,0});
        CHECK(gs.pixel16(0,64,0,0)==0); // The selected R bit was clear; alpha is not masked.
        gs.vram[hg::GsRegisterState::psmct16_word(0,64,0,0)]=1;
        gs.write_frame_pixel(0,0,0,{0,0,0,0});
        CHECK(gs.pixel16(0,64,0,0)==1);
        gs.write_ad(0x4c,(1ull<<16)|(2ull<<24));
        gs.write_ad(0x4a,0); gs.write_ad(0x44,3ull<<4); gs.write_ad(0x45,1); // DIMX(0,1)=+3
        gs.write_frame_pixel(0,1,0,{7,7,7,0});
        CHECK(gs.pixel16(0,64,1,0)==0x0421);
        gs.write_ad(0x45,0);
        const auto ct16s_location=hg::GsRegisterState::psmct16s_word(0,64,32,0);
        CHECK(ct16s_location!=hg::GsRegisterState::psmct16_word(0,64,32,0));
        gs.write_ad(0x4c,(1ull<<16)|(10ull<<24)); // PSMCT16S
        gs.write_frame_pixel(0,32,0,{0xff,0x80,0x08,0x81});
        CHECK(gs.pixel16s(0,64,32,0)==0x861f);
        gs.write_ad(0x4e,0); gs.write_ad(0x47,(1ull<<16)|(1ull<<17));
        CHECK(gs.draw_depth_frame_pixel(0,32,0,0x80112233,5));
        CHECK(gs.pixel16s(0,64,32,0)==0x8886 && gs.read_z_pixel(0,32,0)==5);
        gs.write_ad(0x4a,0); gs.write_ad(0x4e,0);
        CHECK(gs.draw_depth_frame_pixel(0,0,0,0x80112233,5));
        CHECK(gs.pixel16(0,64,0,0)==0x8886 && gs.read_z_pixel(0,0,0)==5);
        gs.write_ad(0x4c,1ull<<16); // Return later draw checks to PSMCT32.
        gs.write_ad(0,1ull<<6); gs.write_ad(0x42,0x44); // Cs*As + Cd*(1-As)
        gs.write_ad(0x47,0); gs.write_ad(0x49,1); // PABE on, no alpha test
        gs.vram[location]=0xff332211;
        CHECK(gs.draw_frame_pixel(0,0,0,0x000000aa) && gs.vram[location]==0x000000aa);
        gs.write_ad(0x49,0); gs.vram[location]=0xff332211;
        CHECK(gs.draw_frame_pixel(0,0,0,0x000000aa) && gs.vram[location]==0x00332211);
        gs.write_ad(0,0);gs.vram[location]=0xff332211;
        CHECK(gs.draw_frame_pixel(0,0,0,0x000000aa,1u<<6)&&gs.vram[location]==0x00332211);
        gs.write_ad(0,1u<<6);gs.vram[location]=0xff332211;
        CHECK(gs.draw_frame_pixel(0,0,0,0x000000aa,0)&&gs.vram[location]==0x000000aa);
        gs.write_ad(0,0);
        gs.vram[location]=0x77000000;
        gs.write_ad(0x46,1); gs.write_ad(0x4a,0); // ordinary clamped source color
        gs.write_ad(0x47,1|(3ull<<12)); // ATE NEVER, AFAIL=RGB_ONLY
        CHECK(gs.draw_frame_pixel(0,0,0,0x20030201) && gs.vram[location]==0x77030201);
        gs.write_ad(0x47,1); // AFAIL=KEEP
        CHECK(!gs.draw_frame_pixel(0,0,0,0xffaabbcc) && gs.vram[location]==0x77030201);
        gs.vram[location]=0xf7030201;
        gs.write_ad(0x47,1ull<<14); // DATE wants destination alpha bit clear
        CHECK(!gs.draw_frame_pixel(0,0,0,0xffaabbcc) && gs.vram[location]==0xf7030201);
        gs.write_ad(0x4e,0); // ZBP=0, PSMZ32, ZMSK=0
        const auto z_location=hg::GsRegisterState::psmz32_word(0,64,0,0);
        CHECK(z_location!=location); // PSMZ32 has its own documented block layout.
        gs.write_z_pixel(0,0,0,0x12345678);
        CHECK(gs.read_z_pixel(0,0,0)==0x12345678);
        gs.write_ad(0x4e,1ull<<32); // ZMSK leaves the existing depth intact.
        gs.write_z_pixel(0,0,0,0xabcdef01);
        CHECK(gs.read_z_pixel(0,0,0)==0x12345678);
        gs.write_ad(0x4e,1ull<<24); // PSMZ24 shares Z32 layout but stores low 24 bits.
        gs.vram[z_location]=0xaa000000;
        gs.write_z_pixel(0,0,0,0x12345678);
        CHECK(gs.vram[z_location]==0xaa345678 && gs.read_z_pixel(0,0,0)==0x345678);
        { // Real post-opening draw 16516: FRAME itself is PSMZ24 and Z writes are masked.
            hg::GsRegisterState real; real.ensure_vram();
            constexpr std::uint64_t frame=0x000000003104019cull;
            constexpr std::uint64_t zbuf=0x000000013100019cull;
            real.write_ad(0x4c,frame);
            real.write_ad(0x4e,zbuf);
            real.write_ad(0x47,0x30000); // ZTE + ALWAYS, matching the captured draw.
            real.write_ad(0x06,0x0000000667121400ull); // PSMZ24 TEX0 from the same sprite.
            real.write_ad(0x08,0x0000006fc007fc00aull);
            const auto base=std::uint32_t(frame&0x1ff)*2048;
            const auto width=std::uint32_t((frame>>16)&0x3f)*64;
            const auto z24_word=hg::GsRegisterState::psmz32_word(base,width,0,0);
            const auto ct24_word=hg::GsRegisterState::psmct32_word(base,width,0,0);
            CHECK(z24_word!=ct24_word);
            real.vram[z24_word]=0xab000000;
            real.vram[ct24_word]=0xcd556677;
            CHECK(real.draw_depth_frame_pixel(0,0,0,0x99112233,0x00abcdef,0x116));
            CHECK(real.vram[z24_word]==0xab112233); // 24-bit payload, Z swizzle, unused byte preserved.
            CHECK(real.vram[ct24_word]==0xcd556677);
            CHECK(real.read_z_pixel(0,0,0)==0x112233); // ZMSK kept the shared destination from a Z write.
        }
        const auto z16_location=hg::GsRegisterState::psmz16_word(0,64,0,0);
        CHECK(z16_location!=hg::GsRegisterState::psmct16_word(0,64,0,0));
        gs.write_ad(0x4e,2ull<<24); // PSMZ16
        gs.write_z_pixel(0,0,0,0x12345678);
        CHECK(gs.read_z_pixel(0,0,0)==0x5678);
        const auto z16s_location=hg::GsRegisterState::psmz16s_word(0,64,32,0);
        CHECK(z16s_location!=hg::GsRegisterState::psmz16_word(0,64,32,0));
        gs.write_ad(0x4e,10ull<<24); // PSMZ16S
        gs.write_z_pixel(0,0,0,0xabcdef01);
        CHECK(gs.read_z_pixel(0,0,0)==0xef01);
        gs.write_ad(0x4e,0); gs.write_z_pixel(0,0,0,4);
        gs.write_ad(0x47,(1ull<<16)|(2ull<<17)); // ZTE + GEQUAL
        CHECK(gs.draw_depth_frame_pixel(0,0,0,0x80112233,5));
        CHECK(gs.vram[location]==0x80112233 && gs.read_z_pixel(0,0,0)==5);
        gs.write_ad(0x47,(1ull<<16)|(3ull<<17)); // ZTE + GREATER
        CHECK(!gs.draw_depth_frame_pixel(0,0,0,0xffaabbcc,5));
        CHECK(gs.vram[location]==0x80112233 && gs.read_z_pixel(0,0,0)==5);
        gs.write_ad(0x47,(1ull<<16)|(1ull<<17)); // ZTE + ALWAYS
        gs.write_ad(0x22,2); // SCANMSK: prohibit even framebuffer rows for primitives.
        gs.write_z_pixel(0,0,0,0);
        CHECK(!gs.draw_depth_frame_pixel(0,0,0,0x80112233,5));
        CHECK(gs.vram[location]==0x80112233 && gs.read_z_pixel(0,0,0)==0);
        CHECK(gs.draw_depth_frame_pixel(0,0,1,0x80112233,5));
        gs.write_ad(0x22,3); // SCANMSK: prohibit odd framebuffer rows.
        CHECK(!gs.draw_depth_frame_pixel(0,0,1,0xffaabbcc,6));
        gs.write_ad(0x22,0);
        // Failed alpha tests still take the later pixel tests; AFAIL selects
        // which of the successfully-tested frame/depth outputs may commit.
        gs.vram[location]=0x7f000000; gs.write_z_pixel(0,0,0,1);
        gs.write_ad(0x47,1|(1ull<<12)|(1ull<<16)|(1ull<<17)); // NEVER, FB_ONLY, Z ALWAYS
        CHECK(gs.draw_depth_frame_pixel(0,0,0,0x20332211,6));
        CHECK(gs.vram[location]==0x20332211 && gs.read_z_pixel(0,0,0)==1);
        gs.vram[location]=0x7f000000; gs.write_z_pixel(0,0,0,1);
        gs.write_ad(0x47,1|(2ull<<12)|(1ull<<16)|(1ull<<17)); // NEVER, ZB_ONLY, Z ALWAYS
        CHECK(gs.draw_depth_frame_pixel(0,0,0,0x20332211,6));
        CHECK(gs.vram[location]==0x7f000000 && gs.read_z_pixel(0,0,0)==6);
        gs.vram[location]=0x7f000000; gs.write_z_pixel(0,0,0,1);
        gs.write_ad(0x47,1|(3ull<<12)|(1ull<<16)|(1ull<<17)); // NEVER, RGB_ONLY, Z ALWAYS
        CHECK(gs.draw_depth_frame_pixel(0,0,0,0x20332211,6));
        CHECK(gs.vram[location]==0x7f332211 && gs.read_z_pixel(0,0,0)==1);
        gs.write_ad(0x47,(1ull<<16)|(1ull<<17)); // Restore ATE off, Z ALWAYS for draws below.
        gs.write_ad(0x40,(7ull<<16)|(7ull<<48)); // SCAX/Y: inclusive 0..7
        hg::GsDraw point{}; point.primitive=0; point.count=1;
        point.vertices[0]={32,48,9,0xff112233}; // 2.0, 3.0 in 12.4 coordinates
        CHECK(gs.rasterize_draw(point));
        CHECK(gs.vram[hg::GsRegisterState::psmct32_word(0,64,2,3)]==0xff112233);
        CHECK(gs.read_z_pixel(0,2,3)==9);
        gs.write_ad(0x3d,0x00302010); point.prim_state=1u<<5; point.vertices[0].fog=0;
        point.vertices[0].x=48; point.vertices[0].y=48;
        CHECK(gs.rasterize_draw(point));
        CHECK(gs.vram[hg::GsRegisterState::psmct32_word(0,64,3,3)]==0xff2f1f0f);
        point.prim_state=0;
        point.vertices[0].x=128; // Outside the inclusive 0..7 scissor.
        CHECK(!gs.rasterize_draw(point));
        bool unsupported=false; point.primitive=3; point.count=3;point.prim_state=1u<<7;
        try { (void)gs.rasterize_draw(point); } catch(const std::runtime_error&) { unsupported=true; }
        CHECK(unsupported);
        point.primitive=0; point.count=1;point.prim_state=0; point.vertices[0].x=16; point.vertices[0].y=16;
        gs.draws.push_back(point);
        CHECK(gs.rasterize_pending_draws()==1 && gs.rasterized_draw_count==1);
        CHECK(gs.rasterize_pending_draws()==0);
        // A queued primitive owns the drawing environment present at its XYZ2
        // kick.  Later register writes must neither retime that draw nor be
        // lost when the deferred rasterizer temporarily installs the snapshot.
        hg::GsRegisterState captured{}; captured.ensure_vram();
        captured.write_ad(0x4c,1ull<<16); // FRAME_1: base 0, width 64, PSMCT32
        captured.write_ad(0x4e,31);       // Separate PSMZ32 depth buffer.
        captured.write_ad(0x47,(1ull<<16)|(1ull<<17)); // ZTE + ALWAYS
        captured.write_ad(0x40,(7ull<<16)|(7ull<<48)); // Inclusive 0..7 scissor.
        captured.write_ad(0,1ull<<5);     // Point with FGE.
        captured.write_ad(1,0xff000000);  // Opaque black RGBAQ.
        captured.write_ad(0xa,0);         // Full fog contribution.
        captured.write_ad(0x3d,0x000000ff); // Red FOGCOL at the kick.
        captured.write_ad(5,(1ull<<32)|(16ull<<16)|16ull);
        CHECK(captured.draws.size()==1 && captured.draws[0].environment_captured);
        captured.write_ad(0x3d,0x0000ff00); // Live environment becomes green...
        captured.write_ad(0x4c,8ull|(1ull<<16)); // ...and targets another frame base.
        CHECK(captured.rasterize_pending_draws()==1);
        CHECK(captured.vram[hg::GsRegisterState::psmct32_word(0,64,1,1)]==0xff0000fe);
        CHECK(captured.vram[hg::GsRegisterState::psmct32_word(8u*2048u,64,1,1)]==0);
        CHECK(captured.value[0x3d]==0x0000ff00 && captured.value[0x4c]==(8ull|(1ull<<16)));
        // TRXDIR is an ordering barrier: its IMAGE data must follow any draw
        // that touches the same local-memory word, even while draws are queued.
        captured.write_ad(5,(2ull<<32)|(16ull<<16)|32ull); // Green point at (2,1).
        CHECK(captured.draws.size()==2 && captured.rasterized_draw_count==1);
        captured.write_ad(0x50,(256ull<<32)|(1ull<<48)); // DBP maps to FRAME base 8.
        captured.write_ad(0x51,(2ull<<32)|(1ull<<48));
        captured.write_ad(0x52,2ull|(1ull<<32));
        captured.write_ad(0x53,0);
        CHECK(captured.rasterized_draw_count==2 && captured.gif_to_vram_active);
        captured.write_hwreg(0x11223344);
        CHECK(captured.rasterize_pending_draws()==0);
        CHECK(captured.vram[hg::GsRegisterState::psmct32_word(8u*2048u,64,2,1)]==0x11223344);
        hg::GsDraw sprite{}; sprite.primitive=6; sprite.count=2;
        sprite.vertices[0]={16,16,1,0xff123456}; // First sprite Z/color is not the flat draw-kick value.
        sprite.vertices[1]={48,48,10,0xff445566};
        CHECK(gs.rasterize_draw(sprite));
        for(std::uint32_t y=1;y<3;++y)for(std::uint32_t x=1;x<3;++x)
            CHECK(gs.vram[hg::GsRegisterState::psmct32_word(0,64,x,y)]==0xff445566);
        CHECK(gs.read_z_pixel(0,1,1)==10);
        sprite.prim_state=(1u<<3)|(1u<<7); // Sprite IIP/AA1 are fixed flat/off.
        sprite.vertices[0].x=80;sprite.vertices[0].y=16;sprite.vertices[0].rgba=0xff010203;
        sprite.vertices[1].x=112;sprite.vertices[1].y=48;sprite.vertices[1].rgba=0xffabcdef;
        CHECK(gs.rasterize_draw(sprite));
        for(std::uint32_t y=1;y<3;++y)for(std::uint32_t x=5;x<7;++x)
            CHECK(gs.vram[hg::GsRegisterState::psmct32_word(0,64,x,y)]==0xffabcdef);
        gs.write_ad(6,8ull|(1ull<<14)|(1ull<<26)|(1ull<<30)|(1ull<<34)|(1ull<<35)); // 2x2 CT32, TCC, DECAL
        const auto texture_base=8u*64u;
        gs.vram[hg::GsRegisterState::psmct32_word(texture_base,64,0,0)]=0xff000011;
        gs.vram[hg::GsRegisterState::psmct32_word(texture_base,64,1,0)]=0xff000022;
        gs.vram[hg::GsRegisterState::psmct32_word(texture_base,64,0,1)]=0xff000033;
        gs.vram[hg::GsRegisterState::psmct32_word(texture_base,64,1,1)]=0xff000044;
        hg::GsDraw textured_sprite{};textured_sprite.primitive=6;textured_sprite.count=2;
        textured_sprite.prim_state=(1u<<4)|(1u<<8);
        textured_sprite.vertices[0]={64,64,1,0xffabcdef,0,0,0};
        textured_sprite.vertices[1]={96,96,19,0xffabcdef,0,0,(32u<<16)|32u};
        CHECK(gs.rasterize_draw(textured_sprite));
        CHECK(gs.vram[hg::GsRegisterState::psmct32_word(0,64,4,4)]==0xff000011);
        CHECK(gs.vram[hg::GsRegisterState::psmct32_word(0,64,5,4)]==0xff000022);
        CHECK(gs.vram[hg::GsRegisterState::psmct32_word(0,64,4,5)]==0xff000033);
        CHECK(gs.vram[hg::GsRegisterState::psmct32_word(0,64,5,5)]==0xff000044);
        for(std::uint32_t y=4;y<6;++y)for(std::uint32_t x=4;x<6;++x)
            gs.vram[hg::GsRegisterState::psmct32_word(0,64,x,y)]=0;
        hg::GsDraw line{}; line.primitive=1; line.count=2;line.prim_state=1u<<3;
        line.vertices[0]={16,112,16,0xff001122};
        line.vertices[1]={64,112,19,0xff334455};
        CHECK(gs.rasterize_draw(line));
        CHECK(gs.vram[hg::GsRegisterState::psmct32_word(0,64,1,7)]==0xff001122);
        CHECK(gs.vram[hg::GsRegisterState::psmct32_word(0,64,2,7)]==0xff112233);
        CHECK(gs.vram[hg::GsRegisterState::psmct32_word(0,64,3,7)]==0xff223344);
        CHECK(gs.vram[hg::GsRegisterState::psmct32_word(0,64,4,7)]==0);
        CHECK(gs.read_z_pixel(0,1,7)==16&&gs.read_z_pixel(0,2,7)==17&&gs.read_z_pixel(0,3,7)==18);
        hg::GsDraw fog_line{};fog_line.primitive=1;fog_line.count=2;fog_line.prim_state=1u<<5;
        fog_line.vertices[0]={16,0,21,0xff102030,0};fog_line.vertices[1]={64,0,21,0xff102030,192};
        CHECK(gs.rasterize_draw(fog_line));
        CHECK(gs.vram[hg::GsRegisterState::psmct32_word(0,64,1,0)]==hg::gs_apply_fog(0xff102030,0,0x00302010));
        CHECK(gs.vram[hg::GsRegisterState::psmct32_word(0,64,2,0)]==hg::gs_apply_fog(0xff102030,64,0x00302010));
        CHECK(gs.vram[hg::GsRegisterState::psmct32_word(0,64,3,0)]==hg::gs_apply_fog(0xff102030,128,0x00302010));
        hg::GsDraw line_strip{}; line_strip.primitive=2; line_strip.count=2;line_strip.prim_state=1u<<3;
        line_strip.vertices[0]={96,16,17,0xff667788};
        line_strip.vertices[1]={96,64,20,0xff99aabb};
        CHECK(gs.rasterize_draw(line_strip));
        CHECK(gs.vram[hg::GsRegisterState::psmct32_word(0,64,6,1)]==0xff667788);
        CHECK(gs.vram[hg::GsRegisterState::psmct32_word(0,64,6,2)]==0xff778899);
        CHECK(gs.vram[hg::GsRegisterState::psmct32_word(0,64,6,3)]==0xff8899aa);
        CHECK(gs.vram[hg::GsRegisterState::psmct32_word(0,64,6,4)]==0);
        CHECK(gs.read_z_pixel(0,6,1)==17&&gs.read_z_pixel(0,6,2)==18&&gs.read_z_pixel(0,6,3)==19);
        hg::GsDraw diagonal{}; diagonal.primitive=1; diagonal.count=2;diagonal.prim_state=1u<<3;
        diagonal.vertices[0]={16,16,18,0xff99aabb};
        diagonal.vertices[1]={64,64,21,0xffccddee};
        CHECK(gs.rasterize_draw(diagonal));
        CHECK(gs.vram[hg::GsRegisterState::psmct32_word(0,64,1,1)]==0xff99aabb);
        CHECK(gs.vram[hg::GsRegisterState::psmct32_word(0,64,2,2)]==0xffaabbcc);
        CHECK(gs.vram[hg::GsRegisterState::psmct32_word(0,64,3,3)]==0xffbbccdd);
        CHECK(gs.vram[hg::GsRegisterState::psmct32_word(0,64,4,4)]==0);
        CHECK(gs.read_z_pixel(0,1,1)==18&&gs.read_z_pixel(0,2,2)==19&&gs.read_z_pixel(0,3,3)==20);
        for(std::uint32_t p=1;p<=4;++p)gs.vram[hg::GsRegisterState::psmct32_word(0,64,p,p)]=0;
        const auto diagonal_swap=diagonal.vertices[0];diagonal.vertices[0]=diagonal.vertices[1];diagonal.vertices[1]=diagonal_swap;
        CHECK(gs.rasterize_draw(diagonal));
        CHECK(gs.vram[hg::GsRegisterState::psmct32_word(0,64,4,4)]==0xffccddee);
        CHECK(gs.vram[hg::GsRegisterState::psmct32_word(0,64,3,3)]==0xffbbccdd);
        CHECK(gs.vram[hg::GsRegisterState::psmct32_word(0,64,2,2)]==0xffaabbcc);
        CHECK(gs.vram[hg::GsRegisterState::psmct32_word(0,64,1,1)]==0);
        for(std::uint32_t p=1;p<=4;++p)gs.vram[hg::GsRegisterState::psmct32_word(0,64,p,p)]=0;
        hg::GsDraw fractional_line{};fractional_line.primitive=1;fractional_line.count=2;
        fractional_line.vertices[0]={17,17,20,0xff556677};
        fractional_line.vertices[1]={65,65,20,0xff556677};
        CHECK(gs.rasterize_draw(fractional_line));
        CHECK(gs.vram[hg::GsRegisterState::psmct32_word(0,64,1,1)]==0xff556677);
        CHECK(gs.vram[hg::GsRegisterState::psmct32_word(0,64,2,2)]==0xff556677);
        CHECK(gs.vram[hg::GsRegisterState::psmct32_word(0,64,3,3)]==0xff556677);
        CHECK(gs.vram[hg::GsRegisterState::psmct32_word(0,64,4,4)]==0);
        hg::GsDraw first_triangle{}; first_triangle.primitive=3; first_triangle.count=3;first_triangle.prim_state=1u<<3;
        first_triangle.vertices[0]={16,16,10,0xff000001};
        first_triangle.vertices[1]={48,16,20,0xff000002};
        first_triangle.vertices[2]={16,48,30,0xff0000aa};
        CHECK(gs.rasterize_draw(first_triangle));
        CHECK(gs.vram[hg::GsRegisterState::psmct32_word(0,64,1,1)]==0xff000001);
        CHECK(gs.vram[hg::GsRegisterState::psmct32_word(0,64,2,1)]==0xff000001);
        CHECK(gs.vram[hg::GsRegisterState::psmct32_word(0,64,1,2)]==0xff000055);
        CHECK(gs.read_z_pixel(0,1,1)==10);
        CHECK(gs.read_z_pixel(0,2,1)==15);
        CHECK(gs.read_z_pixel(0,1,2)==20);
        hg::GsDraw wide_depth{};wide_depth.primitive=3;wide_depth.count=3;
        wide_depth.vertices[0]={0,0,0xffffffffu,0xff010203};
        wide_depth.vertices[1]={65535,0,0,0xff010203};
        wide_depth.vertices[2]={0,65535,0,0xff010203};
        CHECK(gs.rasterize_draw(wide_depth));
        CHECK(gs.read_z_pixel(0,1,1)==4292870111u);
        for(std::uint32_t y=0;y<8;++y)for(std::uint32_t x=0;x<8;++x)
            gs.vram[hg::GsRegisterState::psmct32_word(0,64,x,y)]=0;
        hg::GsDraw second_triangle{}; second_triangle.primitive=3; second_triangle.count=3;
        second_triangle.vertices[0]={48,16,12,0xffaa0000};
        second_triangle.vertices[1]={48,48,12,0xffaa0000};
        second_triangle.vertices[2]={16,48,12,0xffaa0000};
        CHECK(gs.rasterize_draw(second_triangle));
        CHECK(gs.vram[hg::GsRegisterState::psmct32_word(0,64,2,2)]==0xffaa0000);
        hg::GsDraw reverse_triangle{}; reverse_triangle.primitive=3; reverse_triangle.count=3;
        reverse_triangle.vertices[0]={16,64,13,0xff00aa00};
        reverse_triangle.vertices[1]={16,96,13,0xff00aa00};
        reverse_triangle.vertices[2]={48,64,13,0xff00aa00};
        CHECK(gs.rasterize_draw(reverse_triangle));
        CHECK(gs.vram[hg::GsRegisterState::psmct32_word(0,64,1,4)]==0xff00aa00);
        CHECK(gs.vram[hg::GsRegisterState::psmct32_word(0,64,2,4)]==0xff00aa00);
        hg::GsDraw strip_triangle{}; strip_triangle.primitive=4; strip_triangle.count=3;
        strip_triangle.vertices[0]={16,80,14,0xffaaaa00};
        strip_triangle.vertices[1]={48,80,14,0xffaaaa00};
        strip_triangle.vertices[2]={16,112,14,0xffaaaa00};
        CHECK(gs.rasterize_draw(strip_triangle));
        CHECK(gs.vram[hg::GsRegisterState::psmct32_word(0,64,1,5)]==0xffaaaa00);
        hg::GsDraw fog_triangle{}; fog_triangle.primitive=3; fog_triangle.count=3; fog_triangle.prim_state=1u<<5;
        fog_triangle.vertices[0]={16,96,15,0xff102030,0};
        fog_triangle.vertices[1]={48,96,15,0xff102030,128};
        fog_triangle.vertices[2]={16,112,15,0xff102030,255};
        CHECK(gs.rasterize_draw(fog_triangle));
        CHECK(gs.vram[hg::GsRegisterState::psmct32_word(0,64,1,6)]==0xff2f1f0f);
        CHECK(gs.vram[hg::GsRegisterState::psmct32_word(0,64,2,6)]==
              hg::gs_apply_fog(0xff102030,64,0x00302010));
        gs.write_ad(6,4ull|(1ull<<14)|(1ull<<26)|(1ull<<30)|(1ull<<34)|(1ull<<35)); // 2x2 CT32, TCC, DECAL
        gs.vram[hg::GsRegisterState::psmct32_word(256,64,0,0)]=0xff123456;
        gs.vram[hg::GsRegisterState::psmct32_word(256,64,1,0)]=0xff234567;
        gs.vram[hg::GsRegisterState::psmct32_word(256,64,0,1)]=0xff345678;
        gs.vram[hg::GsRegisterState::psmct32_word(256,64,1,1)]=0xff456789;
        hg::GsDraw stq_point{}; stq_point.primitive=0; stq_point.count=1;
        stq_point.prim_state=1u<<4; // TME, FST clear: normalized S/T divided by Q.
        stq_point.vertices[0]={112,112,15,0xff000000,0,
            std::uint64_t{0x3f000000}|(std::uint64_t{0x3f000000}<<32),0,0x3f800000};
        CHECK(gs.rasterize_draw(stq_point));
        CHECK(gs.vram[hg::GsRegisterState::psmct32_word(0,64,7,7)]==0xff456789);
        hg::GsDraw stq_sprite{}; stq_sprite.primitive=6; stq_sprite.count=2;
        stq_sprite.prim_state=1u<<4; // TME with FST clear and shared Q.
        stq_sprite.vertices[0]={0,0,15,0xff000000,0,0,0,0x3f800000};
        stq_sprite.vertices[1]={32,32,15,0xff000000,0,
            std::uint64_t{0x3f800000}|(std::uint64_t{0x3f800000}<<32),0,0x3f800000};
        CHECK(gs.rasterize_draw(stq_sprite));
        CHECK(gs.vram[hg::GsRegisterState::psmct32_word(0,64,0,0)]==0xff123456);
        CHECK(gs.vram[hg::GsRegisterState::psmct32_word(0,64,1,1)]==0xff456789);
        stq_sprite.vertices[1].q=0x3f000000;
        CHECK(gs.rasterize_draw(stq_sprite));
        CHECK(gs.vram[hg::GsRegisterState::psmct32_word(0,64,1,1)]==0xff456789);
        hg::GsDraw textured_line{};textured_line.primitive=1;textured_line.count=2;
        textured_line.prim_state=(1u<<4)|(1u<<8);
        textured_line.vertices[0]={80,48,15,0xff000000,0,0,0};
        textured_line.vertices[1]={112,80,15,0xff000000,0,0,(32u<<16)|32u};
        CHECK(gs.rasterize_draw(textured_line));
        CHECK(gs.vram[hg::GsRegisterState::psmct32_word(0,64,5,3)]==0xff123456);
        CHECK(gs.vram[hg::GsRegisterState::psmct32_word(0,64,6,4)]==0xff456789);
        hg::GsDraw stq_line{};stq_line.primitive=1;stq_line.count=2;
        stq_line.prim_state=1u<<4; // TME, FST clear, shared Q.
        stq_line.vertices[0]={80,48,15,0xff000000,0,0,0,0x3f800000};
        stq_line.vertices[1]={112,80,15,0xff000000,0,
            std::uint64_t{0x3f800000}|(std::uint64_t{0x3f800000}<<32),0,0x3f800000};
        CHECK(gs.rasterize_draw(stq_line));
        CHECK(gs.vram[hg::GsRegisterState::psmct32_word(0,64,5,3)]==0xff123456);
        CHECK(gs.vram[hg::GsRegisterState::psmct32_word(0,64,6,4)]==0xff456789);
        stq_line.vertices[1].q=0x3f000000;
        CHECK(gs.rasterize_draw(stq_line));
        CHECK(gs.vram[hg::GsRegisterState::psmct32_word(0,64,6,4)]==0xff456789);
        hg::GsDraw textured_triangle{}; textured_triangle.primitive=3; textured_triangle.count=3;
        textured_triangle.prim_state=(1u<<4)|(1u<<8);
        textured_triangle.vertices[0]={64,16,15,0xff000000,0,0,0};
        textured_triangle.vertices[1]={96,16,15,0xff000000,0,0,32};
        textured_triangle.vertices[2]={64,48,15,0xff000000,0,0,32u<<16};
        CHECK(gs.rasterize_draw(textured_triangle));
        CHECK(gs.vram[hg::GsRegisterState::psmct32_word(0,64,4,1)]==0xff123456);
        CHECK(gs.vram[hg::GsRegisterState::psmct32_word(0,64,5,1)]==0xff234567);
        CHECK(gs.vram[hg::GsRegisterState::psmct32_word(0,64,4,2)]==0xff345678);
        for(const auto location:std::array<std::pair<std::uint32_t,std::uint32_t>,3>{{{4,1},{5,1},{4,2}}})
            gs.vram[hg::GsRegisterState::psmct32_word(0,64,location.first,location.second)]=0;
        const auto textured_swap=textured_triangle.vertices[1];
        textured_triangle.vertices[1]=textured_triangle.vertices[2];textured_triangle.vertices[2]=textured_swap;
        CHECK(gs.rasterize_draw(textured_triangle));
        CHECK(gs.vram[hg::GsRegisterState::psmct32_word(0,64,4,1)]==0xff123456);
        CHECK(gs.vram[hg::GsRegisterState::psmct32_word(0,64,5,1)]==0xff234567);
        CHECK(gs.vram[hg::GsRegisterState::psmct32_word(0,64,4,2)]==0xff345678);
        hg::GsDraw stq_triangle{};stq_triangle.primitive=3;stq_triangle.count=3;
        stq_triangle.prim_state=1u<<4; // TME, FST clear, shared Q.
        stq_triangle.vertices[0]={64,16,15,0xff000000,0,0,0,0x3f800000};
        stq_triangle.vertices[1]={96,16,15,0xff000000,0,std::uint64_t{0x3f800000},0,0x3f800000};
        stq_triangle.vertices[2]={64,48,15,0xff000000,0,std::uint64_t{0x3f800000}<<32,0,0x3f800000};
        CHECK(gs.rasterize_draw(stq_triangle));
        CHECK(gs.vram[hg::GsRegisterState::psmct32_word(0,64,4,1)]==0xff123456);
        CHECK(gs.vram[hg::GsRegisterState::psmct32_word(0,64,5,1)]==0xff234567);
        CHECK(gs.vram[hg::GsRegisterState::psmct32_word(0,64,4,2)]==0xff345678);
        stq_triangle.vertices[2].q=0x3f000000;
        CHECK(gs.rasterize_draw(stq_triangle));
        CHECK(gs.vram[hg::GsRegisterState::psmct32_word(0,64,4,2)]==0xff345678);
    }
    { // RGB24 sprite reduction agrees with the general pipeline, including
      // masked writes and texture feedback from pixels written earlier.
        for(const auto mask:{0u,0x0055aa33u,0xffffffffu})for(bool textured:{false,true}) {
            hg::GsRegisterState gs;gs.ensure_vram();
            for(unsigned n=0;n<gs.vram.size();++n)gs.vram[n]=0xa7315800u+n*2654435761u;
            gs.value[0x4c]=(1ull<<24)|(1ull<<16)|(std::uint64_t(mask)<<32);
            gs.value[0x4e]=(1ull<<32)|(1ull<<24);
            gs.value[0x47]=0x30000;gs.value[0x46]=1;gs.value[0x4a]=1;
            gs.value[0x40]=(63ull<<16)|(7ull<<48);
            gs.value[6]=(1ull<<14)|(6ull<<26)|(3ull<<30)|(1ull<<34)|(1ull<<35);
            gs.value[8]=5;
            hg::GsDraw sprite{};sprite.primitive=6;sprite.count=2;
            sprite.prim_state=6|(1u<<8)|(textured?(1u<<4):0u);
            sprite.vertices[0]={16,0,0,0,0,0,0};
            sprite.vertices[1]={512,128,0,0x718352a4u,0,0,496u|(128u<<16)};
            auto reference=gs;
            for(unsigned y=0;y<8;++y)for(unsigned x=1;x<32;++x) {
                const auto color=textured?reference.shade_point_tex0(0,int(x)-1,int(y),sprite.vertices[1].rgba):sprite.vertices[1].rgba;
                CHECK(reference.draw_depth_frame_pixel(0,x,y,color,0,sprite.prim_state));
            }
            CHECK(gs.rasterize_sprite(sprite));CHECK(gs.vram==reference.vram);
        }
    }
    { // Neutral CT32 copies must agree with scalar sampling and the complete
      // pixel pipeline: clipping, reversed/subpixel endpoints, both contexts,
      // partial masks, overlapping live texture and end-of-VRAM wrap.
        for(unsigned context:{0u,1u})for(auto mask:{0u,0x0055aa33u,0xffffffffu})
        for(bool reverse:{false,true})for(bool wrap:{false,true}) {
            hg::GsRegisterState gs;gs.ensure_vram();
            for(unsigned n=0;n<gs.vram.size();++n)gs.vram[n]=0xb3729401u+n*2654435761u;
            gs.value[0x4c+context]=(wrap?511ull:0ull)|(2ull<<16)|(1ull<<24)|(std::uint64_t(mask)<<32);
            gs.value[0x4e + context]=1ull<<32;
            gs.value[0x47+context]=0x30000;gs.value[0x46]=1;
            gs.value[0x40+context]=9ull|(94ull<<16)|(7ull<<32)|(44ull<<48);
            gs.value[6+context]=(wrap?16370ull:0ull)|(2ull<<14)|(6ull<<26)|(5ull<<30)|(1ull<<34);
            gs.value[8+context]=10ull|(2ull<<4)|(61ull<<14)|(3ull<<24)|(29ull<<34);
            hg::GsDraw sprite{};sprite.primitive=6;sprite.count=2;
            sprite.prim_state=6|(1u<<4)|(1u<<8)|(context<<9);
            sprite.vertices[0]={83,51,0,0x80808080u,0,0,8u|(24u<<16)};
            sprite.vertices[1]={1625,793,0,0x80808080u,0,0,1912u|(1176u<<16)};
            if(reverse)std::swap(sprite.vertices[0],sprite.vertices[1]);
            auto reference=gs;
            const auto coordinate=[](std::int64_t a,std::int64_t b,std::int64_t p,
                                     std::int64_t q,std::int64_t sample) {
                return std::int32_t((a+(sample-p)*(b-a)/(q-p))>>4);
            };
            const auto& a=sprite.vertices[0];const auto& b=sprite.vertices[1];
            for(unsigned y=7;y<=44;++y)for(unsigned x=9;x<=94;++x) {
                const auto u=coordinate(a.uv&16383,b.uv&16383,a.x,b.x,x*16);
                const auto v=coordinate((a.uv>>16)&16383,(b.uv>>16)&16383,a.y,b.y,y*16);
                const auto color=reference.shade_point_tex0(context,u,v,b.rgba);
                CHECK(reference.draw_depth_frame_pixel(context,x,y,color,b.z,sprite.prim_state));
            }
            CHECK(gs.rasterize_sprite(sprite));CHECK(gs.vram==reference.vram);
        }
    }
    { // Prepared PSMT4 sampling versus original scalar sampling, with live aliases.
        for(unsigned scenario=0;scenario<128;++scenario) {
            const unsigned context=scenario&1,csa=(scenario>>1)&15,wrap=(scenario>>5)&3;
            const bool linear=(scenario&8)!=0,alias=(scenario&16)!=0;
            hg::GsRegisterState gs;gs.ensure_vram();
            for(unsigned n=0;n<gs.vram.size();++n)gs.vram[n]=0x7963c15bu+n*2654435761u;
            for(unsigned n=0;n<512;++n){gs.clut[n]=std::uint16_t(n*197+93);gs.clut_valid[n]=true;}
            gs.value[0x4c+context]=(2ull<<16)|(std::uint64_t(scenario&2?0x00aa5500u:0u)<<32);
            gs.value[0x4e + context]=1ull<<32;gs.value[0x47+context]=0x30000;gs.value[0x46]=1;
            gs.value[0x40+context]=(31ull<<16)|(15ull<<48);
            gs.value[6+context]=(alias?0ull:16370ull)|(3ull<<14)|(0x14ull<<20)|
                (5ull<<26)|(4ull<<30)|(1ull<<34)|(std::uint64_t((scenario>>3)&3)<<35)|(std::uint64_t(csa)<<56);
            gs.value[8+context]=wrap|(std::uint64_t(wrap)<<2)|(3ull<<4)|(27ull<<14)|(2ull<<24)|(13ull<<34);
            gs.value[0x14+context]=linear?0x60:0;
            hg::GsDraw sprite{};sprite.primitive=6;sprite.count=2;sprite.prim_state=0x116|(context<<9);
            sprite.vertices[0]={0,0,0,0,0,0,8u|(8u<<16)};
            sprite.vertices[1]={512,256,0,0x718352a4u,0,0,1032u|(520u<<16)};
            if(scenario>=112)gs.clut_valid[csa*16+7]=false; // Preserve partial writes on a later CLUT fault.
            auto reference=gs;std::string expected_fault,actual_fault;
            try {
                for(unsigned y=0;y<16;++y)for(unsigned x=0;x<32;++x){
                    auto u=int(8+x*32),v=int(8+y*32);if(!linear){u>>=4;v>>=4;}
                    auto color=linear?reference.shade_linear_tex0(context,u,v,sprite.vertices[1].rgba):
                        reference.shade_point_tex0(context,u,v,sprite.vertices[1].rgba);
                    reference.draw_depth_frame_pixel(context,x,y,color,0,sprite.prim_state);
                }
            } catch(const std::runtime_error& e){expected_fault=e.what();}
            try {gs.rasterize_sprite(sprite);}catch(const std::runtime_error& e){actual_fault=e.what();}
            CHECK(expected_fault==actual_fault);CHECK(gs.vram==reference.vram);
        }
    }
    { // Invalid optimized-copy candidates retain ordinary texture faults and
      // leave storage unchanged when the first sample is invalid.
        for(unsigned failure=0;failure<4;++failure) {
            hg::GsRegisterState gs;gs.ensure_vram();
            gs.value[0x4c]=(1ull<<16)|(1ull<<24);
            gs.value[0x4e]=1ull<<32;gs.value[0x47]=0x30000;
            gs.value[0x40]=(31ull<<16)|(7ull<<48);
            gs.value[6]=(1ull<<14)|(5ull<<26)|(3ull<<30)|(1ull<<34);
            gs.value[8]=10ull|(31ull<<14)|(7ull<<34);
            if(failure==0)gs.value[6]&=~(63ull<<14); // Zero stride.
            if(failure==1)gs.value[6]=(gs.value[6]&~(15ull<<26))|(11ull<<26);
            if(failure==2)gs.value[8]|=32ull<<4; // Inverted U clamp.
            if(failure==3)gs.value[8]|=8ull<<24; // Inverted V clamp.
            hg::GsDraw sprite{};sprite.primitive=6;sprite.count=2;sprite.prim_state=0x116;
            sprite.vertices[1]={512,128,0,0x80808080u,0,0,512u|(128u<<16)};
            const auto before=gs.vram;bool failed=false;
            try {gs.rasterize_sprite(sprite);}catch(const std::runtime_error&){failed=true;}
            CHECK(failed);CHECK(gs.vram==before);
        }
    }
    { // Frozen pre-fusion algorithm: compare values, flags, aliasing and fault state.
        struct ReferenceVu: hg::Vu1State {
    void reference_multiply_acc(unsigned source,unsigned other,unsigned dest,unsigned broadcast,bool add) {
        std::array<std::uint32_t,4> values{};
        std::array<bool,4> underflow{},overflow{};
        const auto scalar=read_vf(other,broadcast);
        for(unsigned lane=0;lane!=4;++lane)if(dest&(8u>>lane)) {
            const auto product=hg::Fpu::product(read_vf(source,lane),scalar);
            auto bits=product.bits;
            underflow[lane]=product.underflow;overflow[lane]=product.overflow;
            if(add) {
                if(!(acc_defined&(1u<<lane)))throw std::runtime_error("VU1 MADDA reads an undefined ACC lane");
                hg::Fpu arithmetic;bits=arithmetic.add(acc[lane],bits);
                underflow[lane]=underflow[lane]||bool(arithmetic.control&0x4000u);
                overflow[lane]=overflow[lane]||bool(arithmetic.control&0x8000u);
            }
            values[lane]=bits;
        }
        update_arithmetic_flags(values,dest,underflow,overflow);
        for(unsigned lane=0;lane!=4;++lane)if(dest&(8u>>lane)) {acc[lane]=values[lane];acc_defined|=std::uint8_t(1u<<lane);}
    }
    void reference_madd_vector(unsigned destination,unsigned source,unsigned other,unsigned dest,unsigned broadcast) {
        std::array<std::uint32_t,4> values{};
        std::array<bool,4> underflow{},overflow{};
        const auto scalar=read_vf(other,broadcast);
        for(unsigned lane=0;lane!=4;++lane)if(dest&(8u>>lane)) {
            if(!(acc_defined&(1u<<lane)))throw std::runtime_error("VU1 MADD reads an undefined ACC lane");
            const auto product=hg::Fpu::product(read_vf(source,lane),scalar);
            hg::Fpu arithmetic;values[lane]=arithmetic.add(acc[lane],product.bits);
            underflow[lane]=product.underflow||bool(arithmetic.control&0x4000u);
            overflow[lane]=product.overflow||bool(arithmetic.control&0x8000u);
        }
        update_arithmetic_flags(values,dest,underflow,overflow);
        for(unsigned lane=0;lane!=4;++lane)if(dest&(8u>>lane))write_vf(destination,lane,values[lane]);
    }
        };
        const std::array<std::uint32_t,12> edges{0,0x80000000u,1,0x80000001u,
            0x00800000u,0x80800001u,0x3f800000u,0xbf800000u,0x7fffffffu,
            0xffffffffu,0x3f800001u,0x33800000u};
        std::uint32_t random=0x594372a1u;
        const auto next=[&](){random^=random<<13;random^=random>>17;random^=random<<5;return random;};
        for(unsigned sample=0;sample<256;++sample)for(unsigned mask=0;mask<16;++mask)
        for(unsigned broadcast=0;broadcast<4;++broadcast)for(unsigned op=0;op<3;++op) {
            ReferenceVu reference;
            for(unsigned lane=0;lane<4;++lane) {
                reference.vf[1][lane]=sample&1?next():edges[next()%edges.size()];
                reference.vf[2][lane]=sample&1?next():edges[next()%edges.size()];
                reference.acc[lane]=sample&1?next():edges[next()%edges.size()];
            }
            reference.acc_defined=sample%7?15:unsigned(next()&15);
            reference.vf_defined[1]=sample%11?15:unsigned(next()&15);
            reference.vf_defined[2]=sample%13?15:unsigned(next()&15);
            reference.status=next();reference.mac=next();
            hg::Vu1State actual=reference;
            const unsigned source=sample%17?1:0,other=sample%19?2:0;
            const unsigned destination=sample%23?sample%3:32;
            std::string wanted_error,actual_error;
            try {
                if(op==2)reference.reference_madd_vector(destination,source,other,mask,broadcast);
                else reference.reference_multiply_acc(source,other,mask,broadcast,op==1);
            }catch(const std::runtime_error& e){wanted_error=e.what();}
            try {
                if(op==2)actual.madd_vector(destination,source,other,mask,broadcast);
                else actual.multiply_acc(source,other,mask,broadcast,op==1);
            }catch(const std::runtime_error& e){actual_error=e.what();}
            CHECK(wanted_error==actual_error);
            CHECK(actual.vf==reference.vf&&actual.vf_defined==reference.vf_defined);
            CHECK(actual.acc==reference.acc&&actual.acc_defined==reference.acc_defined);
            CHECK(actual.mac==reference.mac&&actual.status==reference.status);
            CHECK(actual.issue_cycle==reference.issue_cycle&&actual.vf_ready==reference.vf_ready);
        }
        // Full-lane specialization: arbitrary finite EE encodings, VF00 and
        // output/input aliases, all broadcasts and both accumulator operations.
        for(unsigned sample=0;sample<65536;++sample)for(unsigned op=0;op<3;++op) {
            ReferenceVu reference;
            for(unsigned lane=0;lane<4;++lane){
                reference.vf[0][lane]=next();reference.vf[1][lane]=next();
                reference.vf[2][lane]=next();reference.acc[lane]=next();
            }
            reference.acc_defined=15;reference.status=next();reference.mac=next();
            hg::Vu1State actual=reference;
            const unsigned source=sample%17?1:0,other=sample%19?2:0;
            const unsigned destination=sample%4==3?31:sample%4,broadcast=sample%4;
            if(op==2){reference.reference_madd_vector(destination,source,other,15,broadcast);
                actual.madd_vector(destination,source,other,15,broadcast);}
            else{reference.reference_multiply_acc(source,other,15,broadcast,op==1);
                actual.multiply_acc(source,other,15,broadcast,op==1);}
            CHECK(actual.vf==reference.vf&&actual.vf_defined==reference.vf_defined);
            CHECK(actual.acc==reference.acc&&actual.acc_defined==reference.acc_defined);
            CHECK(actual.mac==reference.mac&&actual.status==reference.status);
        }
        for(unsigned zeros=0;zeros<16;++zeros)for(unsigned signs=0;signs<16;++signs){
            ReferenceVu reference;reference.vf[2].fill(0x3f800000u);reference.status=0xa5a5a5a5;
            for(unsigned lane=0;lane<4;++lane)
                reference.vf[1][lane]=((zeros&(1u<<lane))?0u:0x3f800000u)|((signs&(1u<<lane))?0x80000000u:0u);
            hg::Vu1State actual=reference;
            reference.reference_multiply_acc(1,2,15,0,false);actual.multiply_acc(1,2,15,0,false);
            CHECK(actual.acc==reference.acc&&actual.mac==reference.mac&&actual.status==reference.status);
        }
        for(unsigned sample=0;sample<16384;++sample)for(unsigned op=0;op<3;++op){
            ReferenceVu reference;
            for(unsigned reg=0;reg<3;++reg){
                for(auto& value:reference.vf[reg])value=sample&1?next():edges[next()%edges.size()];
                reference.vf_defined[reg]=std::uint8_t(sample%8?0x87:next());
            }
            for(auto& value:reference.acc)value=next();
            reference.acc_defined=std::uint8_t(sample%8?0x87:next());reference.mac=next();reference.status=next();
            const unsigned source=sample%31==0?32:sample%17==0?0:1;
            const unsigned other=sample%37==0?32:sample%19==0?0:2;
            const unsigned destination=sample%29==0?32:sample%4==3?31:sample%4;
            const unsigned broadcast=sample%41==0?4:sample%4;
            hg::Vu1State actual=reference;std::string first_error,second_error;
            try{if(op==2)reference.reference_madd_vector(destination,source,other,14,broadcast);
                else reference.reference_multiply_acc(source,other,14,broadcast,op==1);
            }catch(const std::runtime_error& e){first_error=e.what();}
            try{if(op==2)hg::vu_xyz_madd(actual,destination,source,other,broadcast);
                else hg::vu_xyz_multiply_acc(actual,source,other,broadcast,op==1);
            }catch(const std::runtime_error& e){second_error=e.what();}
            CHECK(first_error==second_error);
            CHECK(actual.vf==reference.vf&&actual.vf_defined==reference.vf_defined);
            CHECK(actual.acc==reference.acc&&actual.acc_defined==reference.acc_defined);
            CHECK(actual.mac==reference.mac&&actual.status==reference.status);
        }
        hg::Vu1State guard;guard.acc_defined=15;
#if defined(_M_X64) || defined(__SSE2__)
        CHECK(hg::try_vu_full_multiply_acc(guard,1,2,0,false));
        CHECK(hg::try_vu_full_madd_vector(guard,1,1,2,0));
#else
        CHECK(!hg::try_vu_full_multiply_acc(guard,1,2,0,false));
#endif
        const auto prior=guard;
        CHECK(!hg::try_vu_full_multiply_acc(guard,32,2,0,false));
        CHECK(!hg::try_vu_full_multiply_acc(guard,1,2,4,false));
        CHECK(!hg::try_vu_full_madd_vector(guard,32,1,2,0));
        guard.vf_defined[1]=0;
        CHECK(!hg::try_vu_full_multiply_acc(guard,1,2,0,false));
        CHECK(guard.acc==prior.acc&&guard.acc_defined==prior.acc_defined);
        CHECK(guard.vf==prior.vf&&guard.mac==prior.mac&&guard.status==prior.status);
    }
    { // MAC groups retain lane order, masked-lane clearing and sticky STATUS.
        const std::array<std::uint32_t,4> numbers{0,0x80000000u,0x3f800000u,0xbf800000u};
        for(unsigned combination=0;combination<256;++combination)for(unsigned mask=0;mask<16;++mask)
        for(unsigned exceptions=0;exceptions<16;++exceptions) {
            hg::Vu1State s;s.status=0x5a5;s.mac=0xffff;
            std::array<std::uint32_t,4> values{};std::array<bool,4> underflow{},overflow{};
            unsigned zeros=0,signs=0,unders=0,overs=0;
            for(unsigned lane=0;lane<4;++lane){
                values[lane]=numbers[(combination>>(lane*2))&3];
                underflow[lane]=(exceptions&(1u<<lane))!=0;
                overflow[lane]=(exceptions&(8u>>lane))!=0;
                if(!(mask&(8u>>lane)))continue;
                if((values[lane]&0x7fffffffu)==0)zeros|=8u>>lane;
                if(values[lane]==0xbf800000u)signs|=8u>>lane;
                if(underflow[lane])unders|=8u>>lane;
                if(overflow[lane])overs|=8u>>lane;
            }
            const unsigned flags=(zeros?1:0)|(signs?2:0)|(unders?4:0)|(overs?8:0);
            s.update_arithmetic_flags(values,mask,underflow,overflow);
            CHECK(s.mac==(zeros|(signs<<4)|(unders<<8)|(overs<<12)));
            CHECK(s.status==((0x5a5u&~15u)|flags|(flags<<6)));
        }
    }
    { // Sony VU manual pp44-48: field hazards advance independently pending Q.
        hg::Vu1State s;
        s.q=0x3f000000u;s.write_vf(9,0,0x40800000u);s.write_vf(10,0,0x40000000u);
        s.advance_pipeline(1);s.divide(9,10,0,0);
        s.advance_pipeline(3);s.produced_vf(12,8); // X becomes ready at cycle8.
        s.advance_pipeline(3);
        CHECK(s.issue_cycle==7 && s.q==0x3f000000u && s.q_cycles_remaining==1);
        s.require_vf(12,4); // Y is independent of the pending X write.
        s.require_vf(0,15); // VF00 never hazards.
        CHECK(s.issue_cycle==7 && s.q_pending);
        s.require_vf(12,8);
        CHECK(s.issue_cycle==8 && s.q==0x40000000u && !s.q_pending);
        s.produced_vi(3,4);s.advance_pipeline(1);s.require_vi(3);
        CHECK(s.issue_cycle==12);
        s.produced_vi(4,1);s.advance_pipeline(1);s.require_vi(4);
        CHECK(s.issue_cycle==13);
        s.divide(9,10,0,0);s.wait_q();
        CHECK(s.issue_cycle==20 && !s.q_pending);
    }
    { // EE manual p154: packed XYZF2 drops each high word's low four bits.
        hg::GsRegisterState gs;
        gs.write_ad(0,0); // Point makes drawing-kick behavior observable.
        const auto low=0xabcd1234dead5678ull;
        const auto high=0xfffff5afcabcdefbull;
        gs.write_packed(4,low,high); // ADC=1: update XYZF3 without drawing.
        CHECK(gs.draws.empty() && gs.written[0xc]);
        CHECK(gs.vertex_queue.back().x==0x5678 && gs.vertex_queue.back().y==0x1234);
        CHECK(gs.vertex_queue.back().z==0xabcdef && gs.vertex_queue.back().fog==0x5a);
        gs.write_packed(4,low,high&~(1ull<<47));
        CHECK(gs.draws.size()==1 && gs.written[4]);
        CHECK(gs.draws.back().vertices[0].z==0xabcdef);
        CHECK(gs.draws.back().vertices[0].fog==0x5a);
    }
    { // Packed ADC selects XYZ3 (vertex-only); REGLIST ignores PRE and A+D.
        hg::GsRegisterState gs;
        gs.write_ad(0,0); // Point, so XYZ2 would draw immediately.
        gs.write_packed(2,0,0x40000000); // STQ updates GIF Q; RGBAQ snapshots it for the vertex.
        gs.write_packed(1,0,0);
        gs.write_packed(5,0x000200000001ull,1ull<<47);
        CHECK(gs.vertex_queue.size()==1 && gs.draws.empty() && gs.written[0xd]);
        CHECK(gs.vertex_queue[0].q==0x40000000);
        std::array<std::uint8_t,32> reglist{};
        put64(reglist.data(),0,1|(1ull<<15)|(1ull<<46)|(7ull<<47)|(1ull<<58)|(1ull<<60));
        put64(reglist.data(),8,0xe);put64(reglist.data(),16,0xdeadbeef);put64(reglist.data(),24,0);
        hg::apply_gif_register_transfers(hg::decode_gif_packet(reglist.data(),reglist.size()),gs);
        CHECK(gs.value[0]==0 && !gs.written[0xe]);
    }
    for (auto size : {std::size_t(0), std::size_t(15), std::size_t(32)}) {
        std::array<std::uint8_t, 48> bytes{}; bool rejected = false;
        try { (void)hg::decode_gif_packet(bytes.data(), size); } catch (const std::runtime_error&) { rejected = true; }
        CHECK(rejected);
    }
    return 0;
}
