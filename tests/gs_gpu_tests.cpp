#include "opengl_support.hpp"
#include "gl_gs.hpp"
#include "hg/gs.hpp"
#include "hg/gs_triangle_acceleration.hpp"
#include "hg/native_psmt8_cache.hpp"
#include "gl_native_indexed.hpp"
#include <algorithm>
#include <initializer_list>
#include <iostream>
#include <optional>

namespace {
// HG-ARCH-009: drive the optional HG-ARCH-008 page-epoch hooks through real
// resident GL queues, submission, readback, CPU scopes and faults. The tracker
// is observation-only; every step also compares VRAM with the scalar renderer.
void epoch_require(bool ok,const char* what) {
    if(!ok)throw std::runtime_error(std::string("HG-ARCH-009/010 native renderer gate: ")+what);
}
std::bitset<512> page_set(std::initializer_list<unsigned> pages) {
    std::bitset<512> result;for(const auto p:pages)result.set(p);return result;
}
std::bitset<512> pending_pages(const hg::NativeVramPageEpochs& epochs) {
    std::bitset<512> result;
    for(unsigned p=0;p<512;++p)if(epochs.gpu_pending(page_set({p})))result.set(p);
    return result;
}
unsigned native_epoch_gate() {
    unsigned cases=0;
    auto epochs=std::make_shared<hg::NativeVramPageEpochs>();
    auto gpu=hg::make_gl_sprite_accelerator(true,true,false,epochs);
    struct CountedTriangles final:hg::GsTriangleAccelerator {
        hg::GsTriangleAccelerator& target;unsigned accepted=0;
        explicit CountedTriangles(hg::GsTriangleAccelerator& backend):target(backend){}
        bool render_triangle(const hg::GsTriangleJob& job,hg::GsLocalMemory& memory) override {
            const bool result=target.render_triangle(job,memory);accepted+=result;return result;
        }
    } triangles(*dynamic_cast<hg::GsTriangleAccelerator*>(gpu.get()));
    const auto on_gpu=[&](auto&& operation) {
        hg::gs_sprite_accelerator=gpu.get();hg::gs_triangle_accelerator=&triangles;
        try{operation();}catch(...){hg::gs_sprite_accelerator=nullptr;hg::gs_triangle_accelerator=nullptr;throw;}
        hg::gs_sprite_accelerator=nullptr;hg::gs_triangle_accelerator=nullptr;
    };
    hg::GsRegisterState initial;initial.ensure_vram();
    for(unsigned i=0;i<initial.vram.size();++i)initial.vram[i]=0x6b43a9b5u*i+(i>>11);
    initial.value[0x4c]=2ull<<16;initial.value[0x4e]=1ull<<32;
    initial.value[0x47]=0x30000;initial.value[0x46]=1;
    initial.value[0x40]=(127ull<<16)|(95ull<<48);
    initial.value[6]=8192ull|(2ull<<14)|(7ull<<26)|(7ull<<30)|(1ull<<34);
    initial.value[8]=5;
    hg::GsDraw draw{};draw.primitive=6;draw.count=2;draw.prim_state=0x116;
    draw.vertices[1].x=2048;draw.vertices[1].y=1536;
    draw.vertices[1].uv=2048|(1536u<<16);draw.vertices[1].rgba=0x80808080;
    const auto texture=page_set({256}); // TBP0 8192 blocks.

    auto cpu=initial,gs=initial;
    epoch_require(!epochs->tracking_complete(),"trusted before first resident use");
    cpu.rasterize_sprite(draw);
    gpu->begin_batch();on_gpu([&]{gs.rasterize_sprite(draw);});
    epoch_require(epochs->tracking_complete(),"no coherent baseline after first resident draw");
    const auto written=pending_pages(*epochs);
    epoch_require(written[0]&&written[1]&&!written[256],"queued sprite output/texture pending set");
    const auto texture_stamp=epochs->stamp(texture);
    epoch_require(texture_stamp.has_value(),"unwritten texture page refused a stamp");
    epoch_require(!epochs->stamp(written),"pending GPU output was stamped");
    ++cases;
    gpu->flush(); // Submission plus glFlush only.
    epoch_require(pending_pages(*epochs)==written,"glFlush submission treated as GPU completion");
    ++cases;
    const auto other=page_set({300});
    const auto other_before=epochs->stamp(other);
    epoch_require(other_before.has_value(),"idle page refused a stamp");
    gs.vram[300*2048+5]=0x12345678u;cpu.vram[300*2048+5]=0x12345678u;
    epoch_require(!epochs->matches(*other_before),"CPU write did not advance its page");
    epoch_require(epochs->matches(*texture_stamp),"disjoint CPU write invalidated another page");
    epoch_require(pending_pages(*epochs)==written,"disjoint CPU write completed GPU work");
    ++cases;
    {
        const auto& view=static_cast<const hg::GsLocalMemory&>(gs.vram);
        const auto* resident=view.read_span(0,2048);
        const auto* reference=static_cast<const hg::GsLocalMemory&>(cpu.vram).read_span(0,2048);
        epoch_require(std::equal(resident,resident+2048,reference),"single-page resident readback mismatch");
        auto remaining=written;remaining.reset(0);
        epoch_require(pending_pages(*epochs)==remaining,"single-page readback completed the wrong pages");
        epoch_require(epochs->stamp(page_set({0})).has_value(),"completed page refused a stamp");
        ++cases;
    }
    gpu->end_batch();gpu->finish();
    epoch_require(pending_pages(*epochs).none(),"finish left pending GPU writes");
    const auto completed=epochs->stamp(written);
    epoch_require(completed.has_value(),"finished output refused a stamp");
    epoch_require(cpu.vram==gs.vram,"VRAM mismatch after finish");
    ++cases;

    cpu.rasterize_sprite(draw);
    gpu->begin_batch();on_gpu([&]{gs.rasterize_sprite(draw);});
    epoch_require(!epochs->matches(*completed),"requeued GPU write kept the old stamp");
    gpu->end_batch();gpu->finish();
    epoch_require(!epochs->matches(*completed),"completion revalidated a superseded stamp");
    epoch_require(epochs->stamp(written).has_value()&&cpu.vram==gs.vram,"requeue completion");
    ++cases;

    // Masked upload into a submitted, not yet downloaded page.
    cpu.rasterize_sprite(draw);
    gpu->begin_batch();on_gpu([&]{gs.rasterize_sprite(draw);});gpu->flush();
    {
        const auto before=epochs->sequence();
        const std::uint32_t indices[2]={7,9},values[2]={0xa1b2c3d4u,0x0badf00du},mask=0x00ff00ffu;
        epoch_require(gs.vram.try_masked_write(indices,values,2,mask),"resident masked write not accepted");
        for(unsigned i=0;i<2;++i)cpu.vram[indices[i]]=(cpu.vram[indices[i]]&~mask)|(values[i]&mask);
        epoch_require(epochs->sequence()>before&&epochs->gpu_pending(page_set({0})),"masked GPU write not tracked");
    }
    gpu->end_batch();gpu->finish();
    epoch_require(pending_pages(*epochs).none()&&cpu.vram==gs.vram,"masked write completion");
    ++cases;

    // Complete-page replacement discards submitted GPU output for that page.
    cpu.rasterize_sprite(draw);
    gpu->begin_batch();on_gpu([&]{gs.rasterize_sprite(draw);});gpu->flush();
    {
        auto* replacement=gs.vram.write_discard_span(2048,2048);
        auto* reference=cpu.vram.write_span(2048,2048);
        for(unsigned i=0;i<2048;++i)replacement[i]=reference[i]=0x5a5a0000u+i;
        epoch_require(!epochs->gpu_pending(page_set({1})),"discarded page still pending");
        epoch_require(epochs->gpu_pending(page_set({0})),"discard completed a neighbouring page");
        const auto replaced=epochs->stamp(page_set({1}));
        epoch_require(replaced.has_value(),"discarded page refused a stamp");
        gpu->end_batch();gpu->finish();
        epoch_require(epochs->matches(*replaced),"later completion changed the replaced page");
        epoch_require(pending_pages(*epochs).none()&&cpu.vram==gs.vram,"discard completion");
        ++cases;
    }

    // An ordinary CPU primitive orders queued GPU output through a raster scope.
    {
        cpu.rasterize_sprite(draw);
        gpu->begin_batch();on_gpu([&]{gs.rasterize_sprite(draw);});
        const auto queued=epochs->sequence();
        hg::GsDraw point{};point.primitive=0;point.count=1;point.prim_state=0;
        point.vertices[0].x=32;point.vertices[0].y=32;point.vertices[0].rgba=0xf1b29374;
        cpu.draws.push_back(point);cpu.rasterize_pending_draws();
        gs.draws.push_back(point);on_gpu([&]{gs.rasterize_pending_draws();});
        const auto after=epochs->stamp(page_set({0}));
        epoch_require(epochs->tracking_complete()&&after&&after->generations[0]>queued,"CPU raster scope not tracked");
        gpu->end_batch();gpu->finish();
        epoch_require(pending_pages(*epochs).none()&&cpu.vram==gs.vram,"CPU raster scope completion");
        ++cases;
    }

    // Untextured GPU triangle with separate depth pages.
    {
        cpu.value[0x4e]=gs.value[0x4e]=(1ull<<32)|128;
        hg::GsDraw triangle{};triangle.primitive=3;triangle.count=3;triangle.prim_state=0;
        triangle.vertices[0].x=3*16;triangle.vertices[0].y=9*16;
        triangle.vertices[1].x=120*16;triangle.vertices[1].y=19*16;
        triangle.vertices[2].x=27*16;triangle.vertices[2].y=90*16;
        for(unsigned i=0;i<3;++i){triangle.vertices[i].rgba=0x7189d53fu*(i+1);triangle.vertices[i].z=0x10203040u+i;}
        const auto before=epochs->stamp(written);
        epoch_require(before.has_value(),"triangle baseline");
        cpu.draws.push_back(triangle);cpu.rasterize_pending_draws();
        triangles.accepted=0;gs.draws.push_back(triangle);on_gpu([&]{gs.rasterize_pending_draws();});
        epoch_require(triangles.accepted==1,"triangle fixture did not reach the GPU");
        const auto pending=pending_pages(*epochs);
        epoch_require(pending[0]&&!pending[128]&&!epochs->matches(*before),"queued triangle pages");
        gpu->finish();
        epoch_require(pending_pages(*epochs).none()&&cpu.vram==gs.vram,"triangle completion");
        cpu.value[0x4e]=gs.value[0x4e]=initial.value[0x4e];
        ++cases;
    }

    // Owner swap: old stamps never match the new memory's baseline.
    const auto old_owner=epochs->stamp(written);
    epoch_require(old_owner.has_value(),"old owner stamp");
    {
        auto second_cpu=initial;std::optional<hg::GsRegisterState> second(initial);
        second_cpu.rasterize_sprite(draw);
        gpu->begin_batch();on_gpu([&]{second->rasterize_sprite(draw);});
        epoch_require(epochs->tracking_complete()&&!epochs->matches(*old_owner),"owner swap kept old stamps");
        epoch_require(pending_pages(*epochs)==written,"new owner pending set");
        gpu->end_batch();gpu->finish();
        epoch_require(second_cpu.vram==second->vram&&cpu.vram==gs.vram,"owner swap VRAM");
        ++cases;
        second.reset(); // Destroying the resident owner forgets its storage.
        epoch_require(!epochs->tracking_complete(),"destroyed owner left trust");
        ++cases;
    }

    // Explicit GL error after queued work revokes trust and still faults.
    {
        auto third_cpu=initial,third=initial;third_cpu.rasterize_sprite(draw);
        gpu->begin_batch();on_gpu([&]{third.rasterize_sprite(draw);});
        epoch_require(epochs->tracking_complete(),"new owner baseline after forget");
        glEnable(0x7fff); // Deliberate GL_INVALID_ENUM.
        // HG-PERF-115: resident flushes defer the poll; the fault must surface
        // no later than the next coherence point (here finish), never be lost.
        bool faulted=false;
        try{gpu->flush();gpu->finish();}catch(const std::runtime_error&){faulted=true;}
        gpu->end_batch();
        epoch_require(faulted&&!epochs->tracking_complete(),"GL error did not fault and revoke trust");
        epoch_require(third_cpu.vram==third.vram,"VRAM after GL error");
        ++cases;
    }

    // Accelerator teardown stops observation.
    {
        auto fourth_cpu=initial,fourth=initial;fourth_cpu.rasterize_sprite(draw);
        gpu->begin_batch();on_gpu([&]{fourth.rasterize_sprite(draw);});gpu->end_batch();
        epoch_require(epochs->tracking_complete(),"baseline before teardown");
        gpu.reset();
        epoch_require(!epochs->tracking_complete(),"accelerator teardown left trust");
        epoch_require(fourth_cpu.vram==fourth.vram,"teardown VRAM");
        fourth.vram[0]=7;epoch_require(fourth.vram[0]==7,"post-teardown CPU access");
        ++cases;
    }
    return cases;
}

// HG-ARCH-010: GPU-resident PSMT8 indices + CT32 palette point samples must
// equal the first-party GS point sampler exactly. No frame or depth writes.
unsigned native_texture_gate() {
    unsigned comparisons=0;
    hg::GsRegisterState gs;gs.ensure_vram();
    {
        std::uint32_t seed=0x76a5d481u;
        auto* words=gs.vram.write_span(0,gs.vram.size());
        for(std::size_t i=0;i<gs.vram.size();++i){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;words[i]=seed;}
    }
    for(unsigned i=0;i<256;++i){const auto rgba=0x9e3779b9u*(i+1);
        gs.clut[i]=std::uint16_t(rgba);gs.clut[i+256]=std::uint16_t(rgba>>16);
        gs.clut_valid[i]=gs.clut_valid[i+256]=true;}
    gs.clut_load_count=41;
    auto palette=hg::native_psmt8_palette_snapshot(gs);
    epoch_require(palette&&palette->identity,"palette snapshot identity");
    hg::NativeVramPageEpochs epochs;
    auto observer=std::make_shared<hg::NativeEpochTestMemoryObserver>(epochs); // Test-only: no GL accelerator here.
    gs.vram.observe(observer);
    epoch_require(epochs.begin_after_coherent_fence(),"texture gate baseline");
    hg::NativePsmt8TextureCache cache(epochs,gs.vram);
    hg::NativeIndexedGlResidency residency;
    struct Fixture{unsigned tbp,tbw,tw,th,umode,vmode,umin,umax,vmin,vmax;};
    const Fixture fixtures[]={
        {0,2,7,6,0,0,0,0,0,0},{1,3,6,6,1,1,0,0,0,0},{0x3fff,1,7,6,0,1,0,0,0,0},
        {17,4,8,8,0,0,0,0,0,0},{120,6,6,6,2,2,11,1000,5,70},
        {133,8,6,6,3,3,0x155,0x2aa,0x066,0x299},{511,3,9,7,1,0,0,0,0,0},
        {8191,8,10,10,0,0,0,0,0,0},{64,2,5,4,2,3,3,29,0x00f,0x010},{900,5,7,7,3,2,0x07f,0x080,0,127}};
    const std::int32_t probes[]={-70000,-1025,-129,-65,-1,0,1,7,8,15,31,63,64,127,128,255,511,1000,1023,1024,4097,70001};
    for(const auto& f:fixtures) {
        hg::NativeIndexedDraw d;
        d.tex0=std::uint64_t(f.tbp)|(std::uint64_t(f.tbw)<<14)|(std::uint64_t(0x13)<<20)|
               (std::uint64_t(f.tw)<<26)|(std::uint64_t(f.th)<<30);
        d.clamp=std::uint64_t(f.umode)|(std::uint64_t(f.vmode)<<2)|(std::uint64_t(f.umin)<<4)|
                (std::uint64_t(f.umax)<<14)|(std::uint64_t(f.vmin)<<24)|(std::uint64_t(f.vmax)<<34);
        gs.value[6]=d.tex0;gs.value[8]=d.clamp; // Context 0, CT32/CSM1/CSA=0 CLUT.
        const auto* staged=cache.get_or_stage(d,gs.vram);
        epoch_require(staged&&staged->content_id,"texture gate staging");
        std::vector<std::int32_t> uv;
        for(const auto v:probes)for(const auto u:probes){uv.push_back(u);uv.push_back(v);}
        if(f.tw<=7&&f.th<=7) // Whole nominal envelope for small textures.
            for(std::int32_t y=0;y<(1<<f.th);++y)for(std::int32_t x=0;x<(1<<f.tw);++x){uv.push_back(x);uv.push_back(y);}
        const auto uploads=residency.stats().texture_uploads;
        const auto gpu=residency.point_sample(*staged,*palette,d.clamp,uv);
        epoch_require(residency.stats().texture_uploads==uploads+1,"new staged content was not uploaded");
        for(std::size_t i=0;i<gpu.size();++i) {
            const auto expected=gs.point_sample_tex0(0,uv[i*2],uv[i*2+1]);
            if(gpu[i]!=expected)
                throw std::runtime_error("HG-ARCH-010 GPU indexed sample mismatch tbp="+std::to_string(f.tbp)+
                    " u="+std::to_string(uv[i*2])+" v="+std::to_string(uv[i*2+1]));
            ++comparisons;
        }
        // Same epoch-verified content: cache hit, identical identity, no upload.
        const auto* again=cache.get_or_stage(d,gs.vram);
        epoch_require(again&&again->content_id==staged->content_id,"cache hit changed content identity");
        const auto hits=residency.stats().texture_hits;
        epoch_require(residency.point_sample(*again,*palette,d.clamp,uv)==gpu&&
                      residency.stats().texture_uploads==uploads+1&&residency.stats().texture_hits==hits+1,
                      "resident texture was not reused");
    }
    { // A CPU write to a source page restages new content; the GPU follows it.
        const auto& f=fixtures[0];
        hg::NativeIndexedDraw d;
        d.tex0=std::uint64_t(f.tbp)|(std::uint64_t(f.tbw)<<14)|(std::uint64_t(0x13)<<20)|
               (std::uint64_t(f.tw)<<26)|(std::uint64_t(f.th)<<30);
        d.clamp=0;gs.value[6]=d.tex0;gs.value[8]=0;
        const auto* old_content=cache.get_or_stage(d,gs.vram);
        epoch_require(old_content!=nullptr,"restage fixture staging");
        const auto old_id=old_content->content_id;
        const std::vector<std::int32_t> before_uv{5,3};
        // Make the pre-mutation content resident right before the write, so a
        // residency keyed by anything weaker than content identity is caught.
        const auto before=residency.point_sample(*old_content,*palette,0,before_uv);
        epoch_require(before[0]==gs.point_sample_tex0(0,5,3),"pre-mutation texel parity");
        const auto word=hg::GsRegisterState::psmt8_word(0,128,5,3);
        const unsigned lane=((3u&2u)>>1)|((5u&8u)>>2);
        gs.vram[word]^=0xffu<<(lane*8u); // Flip texel (5,3)'s index byte.
        const auto* fresh=cache.get_or_stage(d,gs.vram);
        epoch_require(fresh&&fresh->content_id!=old_id,"source mutation reused stale content");
        const auto uploads=residency.stats().texture_uploads;
        const std::vector<std::int32_t> uv{5,3,6,3};
        const auto gpu=residency.point_sample(*fresh,*palette,0,uv);
        epoch_require(residency.stats().texture_uploads==uploads+1,"restaged content was not uploaded");
        epoch_require(gpu[0]==gs.point_sample_tex0(0,5,3)&&gpu[1]==gs.point_sample_tex0(0,6,3)&&gpu[0]!=before[0],
                      "mutated texel parity");
        comparisons+=2;
        // A new palette load mints a new identity and is uploaded once.
        for(unsigned i=0;i<256;++i){gs.clut[i]^=0x5a5au;gs.clut_valid[i]=true;}
        ++gs.clut_load_count;
        const auto reloaded=hg::native_psmt8_palette_snapshot(gs);
        epoch_require(reloaded&&reloaded->identity!=palette->identity,"palette reload identity");
        const auto palette_uploads=residency.stats().palette_uploads;
        const auto again=residency.point_sample(*fresh,*reloaded,0,uv);
        epoch_require(residency.stats().palette_uploads==palette_uploads+1,"reloaded palette was not uploaded");
        epoch_require(again[0]==gs.point_sample_tex0(0,5,3)&&again[1]==gs.point_sample_tex0(0,6,3),"reloaded palette parity");
        comparisons+=2;
    }
    gs.vram.observe(nullptr);
    const auto& stats=residency.stats();
    std::cout<<"HG-ARCH-010 resident indexed textures: uploads="<<stats.texture_uploads<<" hits="<<stats.texture_hits
             <<" palette_uploads="<<stats.palette_uploads<<" palette_hits="<<stats.palette_hits
             <<" bytes="<<stats.uploaded_bytes<<'\n';
    return comparisons;
}
}

int main(int argc,char** argv) {
    GLFWwindow* window=nullptr;
    try {
        if(!glfwInit())throw std::runtime_error("GLFW init");
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,4);glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,3);
        glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE);glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);
        window=glfwCreateWindow(16,16,"GS differential tests",nullptr,nullptr);
        if(!window)throw std::runtime_error("OpenGL4.3 unavailable");
        glfwMakeContextCurrent(window);
        std::cout<<"Renderer: "<<glGetString(GL_RENDERER)<<" version "<<glGetString(GL_VERSION)<<'\n';
        bool resident=false,triangles=false,epochs_only=false,texture_only=false;
        for(int i=1;i<argc;++i){resident|=std::string(argv[i])=="--resident";triangles|=std::string(argv[i])=="--triangles";
            epochs_only|=std::string(argv[i])=="--native-epochs";texture_only|=std::string(argv[i])=="--native-texture";}
        {
            const auto texels=native_texture_gate();
            std::cout<<texels<<" HG-ARCH-010 GPU indexed texel/palette comparisons passed\n";
            if(texture_only){glfwDestroyWindow(window);glfwTerminate();return 0;}
        }
        if(epochs_only||resident) {
            const auto gate=native_epoch_gate();
            std::cout<<gate<<" HG-ARCH-009 resident native page-epoch cases passed\n";
            if(epochs_only){glfwDestroyWindow(window);glfwTerminate();return 0;}
        }
        auto gpu=hg::make_gl_sprite_accelerator(resident,triangles);unsigned cases=0;
        const auto capture=[](hg::GsDraw& draw,const hg::GsDraw::Environment& environment) {
            draw.environment_captured=true;
            draw.explicit_environment=std::make_shared<hg::GsDraw::Environment>(environment);
        };
        struct CountedTriangles final:hg::GsTriangleAccelerator {
            hg::GsTriangleAccelerator& target;unsigned accepted=0,attempted=0;
            explicit CountedTriangles(hg::GsTriangleAccelerator& backend):target(backend){}
            bool render_triangle(const hg::GsTriangleJob& job,hg::GsLocalMemory& memory) override {
                ++attempted;const bool result=target.render_triangle(job,memory);accepted+=result;return result;
            }
            bool accepts_native_float_stq() const noexcept override {return target.accepts_native_float_stq();}
        } triangle_counter(*dynamic_cast<hg::GsTriangleAccelerator*>(gpu.get()));
        if(triangles)for(unsigned variant=0;variant<192;++variant) {
            const bool untextured=variant>=96;
            hg::GsRegisterState cpu;cpu.ensure_vram();
            for(unsigned i=0;i<cpu.vram.size();++i)cpu.vram[i]=i*0x97a54321u+(i>>9);
            const unsigned format=variant%3==0?0:variant%3==1?((variant&64)?0x14:0x13):0x1b,wrap=(variant/3)%4;
            cpu.value[0x4c]=(2ull<<16)|((variant&32)?511:0)|((variant&64)?0x00f00f00ull<<32:0);
            cpu.value[0x4e]=32|((variant&1)?1ull<<24:0)|((variant&8)?1ull<<32:0);
            // HG-PERF-116: cover every ATST mode and varied AREF with AFAIL KEEP.
            cpu.value[0x47]=0x10000|((1ull+(variant%3))<<17)|
                ((variant&4)?(1ull|(std::uint64_t((variant>>3)&7)<<1)|(std::uint64_t((variant*37u)&255u)<<4)):0);
            cpu.value[0x40]=(127ull<<16)|(95ull<<48);
            cpu.value[6]=8192ull|(2ull<<14)|(std::uint64_t(format)<<20)|(7ull<<26)|(7ull<<30)|
                ((variant&16)?0:1ull<<34)|(std::uint64_t(variant%4)<<35)|(format==0x14?std::uint64_t(variant&15)<<56:0);
            // PSMT4 pages are 128x128: use a 256x256 texture with two pages per row.
            if(format==0x14)cpu.value[6]=(cpu.value[6]&~((63ull<<14)|(15ull<<26)|(15ull<<30)))|(4ull<<14)|(8ull<<26)|(8ull<<30);
            cpu.value[0x14]=(variant&1)?0x60:0;
            cpu.value[8]=wrap|(std::uint64_t(wrap)<<2)|(7ull<<4)|(95ull<<14)|(3ull<<24)|(79ull<<34);
            cpu.value[0x46]=variant&1;cpu.value[0x49]=(variant>>1)&1;cpu.value[0x4a]=(variant>>2)&1;
            cpu.value[0x42]=0x44ull|(193ull<<32);
            for(unsigned i=0;i<256;++i){const auto rgba=0x19537bc1u*i;
                cpu.clut[i]=std::uint16_t(rgba);cpu.clut[i+256]=std::uint16_t(rgba>>16);
                cpu.clut_valid[i]=cpu.clut_valid[i+256]=true;}
            if(untextured) {
                // TME-off must ignore invalid texture format/width/CLUT/Q state.
                cpu.value[6]=~0ull;cpu.value[8]=~0ull;cpu.clut_valid.fill(false);
            }
            for(unsigned n=0;n<8;++n){
                hg::GsDraw draw{};draw.primitive=3;draw.count=3;
                draw.prim_state=(untextured?((variant&32)?256:0):0x10)|((variant&2)?8:0)|((variant&4)?64:0);
                draw.vertices[0].x=unsigned(n*16+3);draw.vertices[0].y=9;
                draw.vertices[1].x=2013;draw.vertices[1].y=unsigned(19+n*16);
                draw.vertices[2].x=unsigned(27+n*16);draw.vertices[2].y=1517;
                for(unsigned i=0;i<3;++i){draw.vertices[i].rgba=0x7189d53fu*(i+n+1);draw.vertices[i].z=0x519ca731u*(i+n+1);
                    draw.vertices[i].q=i==1?0x40000000:0x3f800000;
                    draw.vertices[i].st=std::uint64_t(i==1?0x3fc00000:0xbe800000)|
                        (std::uint64_t(i==2?0x3f800000:0x3e800000)<<32);}
                if(variant&32)for(unsigned i=0;i<3;++i)
                    draw.vertices[i].st=(draw.vertices[i].st&0xffffffff00000000ull)|(i==1?0x3a000000u:0xb9800000u);
                if(untextured)for(auto& vertex:draw.vertices)vertex.q=0;
                if(variant&16)std::swap(draw.vertices[1],draw.vertices[2]);
                cpu.draws.push_back(draw);
            }
            auto accelerated=cpu;
            hg::gs_sprite_accelerator=nullptr;hg::gs_triangle_accelerator=nullptr;cpu.rasterize_pending_draws();
            triangle_counter.accepted=0;
            hg::gs_sprite_accelerator=gpu.get();hg::gs_triangle_accelerator=&triangle_counter;
            if(variant&1){
                const auto tail=std::vector<hg::GsDraw>(accelerated.draws.begin()+4,accelerated.draws.end());
                accelerated.draws.resize(4);accelerated.rasterize_pending_draws();
                accelerated.draws.insert(accelerated.draws.end(),tail.begin(),tail.end());
            }
            accelerated.rasterize_pending_draws();hg::gs_triangle_accelerator=nullptr;hg::gs_sprite_accelerator=nullptr;
            if(cpu.vram!=accelerated.vram)throw std::runtime_error("GPU triangle differential mismatch case "+std::to_string(variant));
            if(untextured&&!triangle_counter.accepted)throw std::runtime_error("Untextured triangle test did not exercise GPU");
            if(format==0x14&&!untextured&&!triangle_counter.accepted)throw std::runtime_error("PSMT4 triangle test did not exercise GPU "+std::to_string(variant));
            ++cases;
            if(variant%96<8) {
                // Follow one GPU batch with a different texture/frame mapping,
                // a CPU primitive, and a fault. Observe pending resident writes
                // through the ordinary memory owner and copy boundaries.
                auto mixed=cpu;mixed.draws.clear();mixed.retired_draw_count=0;mixed.rasterized_draw_count=0;
                auto first=cpu.draws.front();capture(first,cpu.value);
                mixed.draws.push_back(first);
                auto second=first;second.explicit_environment=std::make_shared<hg::GsDraw::Environment>(*first.explicit_environment);
                (*second.explicit_environment)[6]=(2ull<<14)|(7ull<<26)|(7ull<<30)|(1ull<<34);
                (*second.explicit_environment)[0x4c]=(2ull<<16)|96;(*second.explicit_environment)[0x4e]=128;
                mixed.draws.push_back(second);
                hg::GsDraw point{};point.primitive=0;point.count=1;point.prim_state=0;capture(point,*second.explicit_environment);
                point.vertices[0].x=32;point.vertices[0].y=32;point.vertices[0].rgba=0xf1b29374;point.vertices[0].z=0xffffffff;
                mixed.draws.push_back(point);
                if(variant&1){auto invalid=first;invalid.prim_state|=128;mixed.draws.push_back(invalid);}
                auto reference=mixed;std::string cpu_error,gpu_error;
                try{reference.rasterize_pending_draws();}catch(const std::runtime_error& e){cpu_error=e.what();}
                hg::gs_sprite_accelerator=gpu.get();hg::gs_triangle_accelerator=dynamic_cast<hg::GsTriangleAccelerator*>(gpu.get());
                try{mixed.rasterize_pending_draws();}catch(const std::runtime_error& e){gpu_error=e.what();}
                hg::gs_sprite_accelerator=nullptr;hg::gs_triangle_accelerator=nullptr;
                auto copied=mixed;
                if(cpu_error!=gpu_error||copied.vram!=reference.vram||mixed.rasterized_draw_count!=reference.rasterized_draw_count)
                    throw std::runtime_error("GPU triangle ordering/fault mismatch "+std::to_string(variant));
                ++cases;
            }
        }
        struct CountedSprites final:hg::GsSpriteAccelerator {
            hg::GsSpriteAccelerator& target;unsigned accepted=0;
            explicit CountedSprites(hg::GsSpriteAccelerator& backend):target(backend){}
            bool render(const hg::GsSpriteJob& job,hg::GsLocalMemory& memory) override {
                const auto result=target.render(job,memory);accepted+=result;return result;}
            void begin_batch() override {target.begin_batch();}
            void end_batch() override {target.end_batch();}
            void flush() override {target.flush();}
            void finish() override {target.finish();}
        } counted(*gpu);
        for(unsigned format:{0u,2u,10u,49u})for(unsigned shifted=0;shifted<2;++shifted) {
            hg::GsRegisterState initial;initial.ensure_vram();
            for(unsigned i=0;i<initial.vram.size();++i)initial.vram[i]=0x81739badu*i+17;
            initial.value[0x4e]=(1ull<<32)|400;initial.value[0x47]=0x30000;
            initial.value[0x46]=1;initial.value[0x40]=(127ull<<16)|(95ull<<48);
            initial.value[8]=5;initial.value[0x3b]=0x80|(0x80ull<<32);
            initial.value[0x42]=0x44ull|(97ull<<32);
            auto cpu=initial,accelerated=initial;
            const auto sequence=[&](hg::GsRegisterState& state) {
                const unsigned destinations[]={0,32,64,128,96,96,511};
                const unsigned sources[]={8192,8192,0,3072,8192,8192,8192};
                for(unsigned n=0;n<7;++n) {
                    // Independent mixed-size draws, then RAW, WAR, WAW and wrap.
                    state.value[0x4c]=(2ull<<16)|(std::uint64_t(format)<<24)|destinations[n];
                    state.value[6]=sources[n]|(2ull<<14)|(7ull<<26)|(7ull<<30)|(1ull<<34);
                    hg::GsDraw draw{};draw.primitive=6;draw.count=2;draw.prim_state=0x156;
                    draw.vertices[0].x=shifted*5*16;draw.vertices[0].y=shifted*3*16;
                    draw.vertices[1].x=(n&1?112:128)*16;draw.vertices[1].y=(n&1?80:96)*16;
                    draw.vertices[1].uv=draw.vertices[1].x|(draw.vertices[1].y<<16);
                    draw.vertices[1].rgba=0x80808080;state.rasterize_sprite(draw);
                }
            };
            sequence(cpu);counted.accepted=0;hg::gs_sprite_accelerator=&counted;
            counted.begin_batch();sequence(accelerated);counted.end_batch();hg::gs_sprite_accelerator=nullptr;
            if(counted.accepted!=7)throw std::runtime_error("sprite wave fixture did not reach GPU");
            if(cpu.vram!=accelerated.vram)throw std::runtime_error("sprite wave dependency mismatch");
            ++cases;
        }
        const unsigned source_formats[]={0,1,2,10,0x13,0x14,27,49,255},destination_formats[]={0,1,2,10,49};
        for(auto source_format:source_formats)for(auto destination_format:destination_formats)for(unsigned variant=0;variant<16;++variant) {
            hg::GsRegisterState cpu;cpu.ensure_vram();
            for(unsigned i=0;i<cpu.vram.size();++i)cpu.vram[i]=i*0x97a54321u+(i>>9);
            cpu.value[0x4c]=(2ull<<16)|(std::uint64_t(destination_format)<<24)|
                ((variant&8)?(0x81f00700ull<<32)|510ull:0);
            cpu.value[0x4e]=64|((variant&1)?1ull<<24:0)|((variant&2)?1ull<<32:0);
            cpu.value[0x47]=0x10000|((1ull+variant%3)<<17)|((variant&4)?0x4000:0)|((variant&8)?0x8000:0);
            cpu.value[0x40]=std::uint64_t(variant)|(119ull<<16)|(3ull<<32)|(103ull<<48);
            cpu.value[6]=8192ull|(2ull<<14)|(std::uint64_t(source_format==255?0:source_format)<<20)|
                (7ull<<26)|(7ull<<30)|((variant&2)?0:1ull<<34)|(std::uint64_t(variant%4)<<35)|
                (source_format==0x14?std::uint64_t(variant&15)<<56:0);
            const bool wide_indexed=(source_format==0x13||source_format==0x14)&&(variant&4);
            // Multi-page PSMT8/PSMT4 layouts, including an odd page count per row.
            if(wide_indexed)cpu.value[6]=(cpu.value[6]&~((63ull<<14)|(15ull<<26)|(15ull<<30)))|
                (std::uint64_t((variant&8)?6:8)<<14)|(9ull<<26)|(9ull<<30);
            cpu.value[0x14]=(variant&1)?0x60:0;const auto wrap=variant%4;
            cpu.value[8]=wrap|(std::uint64_t(wrap)<<2)|(7ull<<4)|(95ull<<14)|(3ull<<24)|(79ull<<34);
            cpu.value[0x3b]=0x39|(0xc7ull<<32)|((variant&4)?0x8000:0);
            cpu.value[0x44]=0x7310526473105264ull;cpu.value[0x45]=variant&1;
            cpu.value[0x46]=variant&1;cpu.value[0x49]=(variant>>1)&1;cpu.value[0x4a]=(variant>>2)&1;
            cpu.value[0x42]=0x44ull|(193ull<<32);
            for(unsigned i=0;i<256;++i){const auto rgba=0x19537bc1u*i;
                cpu.clut[i]=std::uint16_t(rgba);cpu.clut[i+256]=std::uint16_t(rgba>>16);
                cpu.clut_valid[i]=cpu.clut_valid[i+256]=true;}
            hg::GsDraw draw{};draw.primitive=6;draw.count=2;draw.prim_state=0x106|(source_format==255?0:16)|((variant&2)?64:0);
            draw.vertices[0].x=3;draw.vertices[0].y=9;draw.vertices[0].uv=24|(40u<<16);
            draw.vertices[1].x=1997;draw.vertices[1].y=1741;draw.vertices[1].uv=1991|(1737u<<16);
            draw.vertices[1].rgba=0xc19357ef;draw.vertices[1].z=0x519ca731u*(variant+1);
            if(wide_indexed)draw.vertices[1].uv=7000|(6000u<<16);
            // Below the generic 4096-pixel threshold; PSMT8/PSMT4 remain GPU-eligible.
            if((source_format==0x13||source_format==0x14)&&variant==3){draw.vertices[1].x=643;draw.vertices[1].y=649;}
            if(variant&8){std::swap(draw.vertices[0].uv,draw.vertices[1].uv);}
            cpu.draws.push_back(draw);auto accelerated=cpu;const auto accepted_before=counted.accepted;
            cpu.rasterize_pending_draws();hg::gs_sprite_accelerator=&counted;
            accelerated.rasterize_pending_draws();hg::gs_sprite_accelerator=nullptr;
            const auto name=std::to_string(source_format)+"/"+std::to_string(destination_format)+"/"+std::to_string(variant);
            if(cpu.vram!=accelerated.vram) {
                for(unsigned i=0;i<cpu.vram.size();++i)if(cpu.vram[i]!=accelerated.vram[i]){
                    std::cerr<<"word="<<i<<" cpu="<<cpu.vram[i]<<" gpu="<<accelerated.vram[i]<<'\n';break;}
                throw std::runtime_error("GPU sprite format mismatch "+name);
            }
            if(counted.accepted==accepted_before)throw std::runtime_error("GPU sprite format never accelerated "+name);
            ++cases;
            if(variant<2) {
                auto aliased=accelerated;aliased.draws.clear();aliased.retired_draw_count=0;aliased.rasterized_draw_count=0;
                aliased.value[0x4c]&=~511ull;
                if(variant==0&&source_format!=255)aliased.value[6]&=~16383ull;
                else {aliased.value[0x4e]&=~(511ull|(1ull<<32));}
                aliased.draws.push_back(draw);auto reference=aliased;const auto before=counted.accepted;
                reference.rasterize_pending_draws();hg::gs_sprite_accelerator=&counted;
                aliased.rasterize_pending_draws();hg::gs_sprite_accelerator=nullptr;
                if(reference.vram!=aliased.vram||counted.accepted!=before)
                    throw std::runtime_error("GPU sprite alias fallback mismatch "+name);
                ++cases;
            }
            if((source_format==27||source_format==0x13||source_format==0x14)&&variant==0) {
                auto fault=accelerated;fault.draws.clear();fault.retired_draw_count=0;fault.rasterized_draw_count=0;
                fault.clut_valid.fill(false);fault.draws.push_back(draw);auto reference=fault;
                std::string cpu_error,gpu_error;
                try{reference.rasterize_pending_draws();}catch(const std::runtime_error& e){cpu_error=e.what();}
                hg::gs_sprite_accelerator=&counted;
                try{fault.rasterize_pending_draws();}catch(const std::runtime_error& e){gpu_error=e.what();}
                hg::gs_sprite_accelerator=nullptr;
                if(cpu_error.empty()||cpu_error!=gpu_error||reference.vram!=fault.vram)
                    throw std::runtime_error("GPU sprite CLUT fault mismatch "+name);
                ++cases;
            }
        }
        for(unsigned format: {2u,10u})for(unsigned variant=0;variant<16;++variant) {
            hg::GsRegisterState cpu;cpu.ensure_vram();
            for(unsigned i=0;i<cpu.vram.size();++i)cpu.vram[i]=i*0x97a54321u+(i>>9);
            const unsigned destination_x=(variant&1)?0:8,source_x=destination_x^8;
            const unsigned base=(variant&8)?510:384;
            cpu.value[0x4c]=base|(1ull<<16)|(std::uint64_t(format)<<24)|((variant&2)?0x8731a50full<<32:0);
            cpu.value[0x4e]=64|(1ull<<24)|(1ull<<32);
            cpu.value[0x47]=0x30000|((variant&4)?0x4000:0)|((variant&8)?0x8000:0);
            cpu.value[0x40]=(63ull<<16)|(895ull<<48);
            cpu.value[6]=(base*32)|(1ull<<14)|(std::uint64_t(format)<<20)|(6ull<<26)|(10ull<<30)|(1ull<<34);
            cpu.value[0x14]=0x60;cpu.value[8]=5;cpu.value[0x3b]=0x39|(0xc7ull<<32);
            cpu.value[0x46]=1;cpu.value[0x42]=0x44;cpu.value[0x49]=(variant>>1)&1;
            hg::GsDraw draw{};draw.primitive=6;draw.count=2;draw.prim_state=0x116|((variant&2)?64:0);
            draw.vertices[0].x=destination_x*16;draw.vertices[0].uv=(source_x*16+8)|(8u<<16);
            draw.vertices[1].x=(destination_x+8)*16;draw.vertices[1].y=896*16;
            draw.vertices[1].uv=((source_x+8)*16+8)|((896*16+8u)<<16);draw.vertices[1].rgba=0xb391578f;
            cpu.draws.push_back(draw);auto accelerated=cpu;const auto before=counted.accepted;
            cpu.rasterize_pending_draws();hg::gs_sprite_accelerator=&counted;
            accelerated.rasterize_pending_draws();hg::gs_sprite_accelerator=nullptr;
            if(cpu.vram!=accelerated.vram||counted.accepted==before)
                throw std::runtime_error("GPU opposite-lane copy mismatch "+std::to_string(format)+"/"+std::to_string(variant));
            ++cases;
            if(variant<4){
                auto feedback=accelerated;feedback.draws.clear();feedback.retired_draw_count=0;feedback.rasterized_draw_count=0;
                auto unsafe=draw;
                if(variant==0){unsafe.vertices[0].uv+=1;unsafe.vertices[1].uv+=1;} // Fractional neighbor.
                if(variant==1){unsafe.vertices[0].uv+=16u<<16;unsafe.vertices[1].uv+=16u<<16;} // Other row.
                if(variant==2){unsafe.vertices[0].uv+=16;unsafe.vertices[1].uv+=16;} // Other word.
                if(variant==3){ // The source lane itself is written.
                    unsafe.vertices[0].uv=(unsafe.vertices[0].uv&0xffff0000u)|(destination_x*16+8);
                    unsafe.vertices[1].uv=(unsafe.vertices[1].uv&0xffff0000u)|((destination_x+8)*16+8);
                }
                feedback.draws.push_back(unsafe);auto reference=feedback;const auto prior=counted.accepted;
                reference.rasterize_pending_draws();hg::gs_sprite_accelerator=&counted;
                feedback.rasterize_pending_draws();hg::gs_sprite_accelerator=nullptr;
                if(reference.vram!=feedback.vram||counted.accepted!=prior)
                    throw std::runtime_error("GPU opposite-lane rejection mismatch");
                ++cases;
            }
        }
        if(triangles) { // HG-PERF-106: product-overflow STQ must reach only native-relaxed triangles.
            auto relaxed_gpu=hg::make_gl_sprite_accelerator(resident,true,true);
            CountedTriangles counted(*dynamic_cast<hg::GsTriangleAccelerator*>(relaxed_gpu.get()));
            hg::GsRegisterState initial;initial.ensure_vram();
            for(unsigned i=0;i<initial.vram.size();++i)initial.vram[i]=i*0x97a54321u+(i>>9);
            initial.value[0x4c]=2ull<<16;initial.value[0x4e]=32ull|(1ull<<32);initial.value[0x47]=0x30000;
            initial.value[0x40]=(127ull<<16)|(95ull<<48);
            initial.value[6]=8192ull|(2ull<<14)|(7ull<<26)|(7ull<<30)|(1ull<<34);
            initial.value[8]=5;initial.value[0x42]=0x44ull|(193ull<<32);
            hg::GsDraw draw{};draw.primitive=3;draw.count=3;draw.prim_state=0x10;
            draw.vertices[0].x=3;draw.vertices[0].y=9;
            draw.vertices[1].x=2013;draw.vertices[1].y=19;
            draw.vertices[2].x=27;draw.vertices[2].y=1517;
            const std::uint32_t s[3]={0x30800000u,0x3f800000u,0x3f000000u};
            const std::uint32_t t[3]={0x3e800000u,0x3f000000u,0x3f400000u};
            for(unsigned i=0;i<3;++i){draw.vertices[i].rgba=0x80808080u;draw.vertices[i].z=0x10203040u+i;
                draw.vertices[i].q=0x3f800000u;draw.vertices[i].st=std::uint64_t(s[i])|(std::uint64_t(t[i])<<32);}
            auto cpu=initial,accelerated=initial;cpu.draws.push_back(draw);accelerated.draws.push_back(draw);
            hg::gs_sprite_accelerator=nullptr;hg::gs_triangle_accelerator=nullptr;cpu.rasterize_pending_draws();
            counted.accepted=0;hg::gs_sprite_accelerator=relaxed_gpu.get();hg::gs_triangle_accelerator=&counted;
            accelerated.rasterize_pending_draws();hg::gs_triangle_accelerator=nullptr;hg::gs_sprite_accelerator=nullptr;
            // HG-PERF-106/HG-FAIL-063 left native-float STQ disabled in the
            // backend; then this product-overflow draw must stay on the CPU.
            if(counted.accepts_native_float_stq()) {
                if(!counted.attempted)throw std::runtime_error("wide STQ fixture was not submitted to native-relaxed triangle backend");
                if(!counted.accepted)throw std::runtime_error("wide STQ fixture was rejected by native-relaxed triangle backend");
            } else if(counted.attempted)throw std::runtime_error("wide STQ fixture reached a backend without native-float STQ");
            if(cpu.vram!=accelerated.vram)throw std::runtime_error("wide STQ native-relaxed differential mismatch");
            ++cases;
            auto invalid=draw;invalid.vertices[2].st=(invalid.vertices[2].st&0xffffffffull)|(std::uint64_t(0x7fc00000u)<<32);
            auto reference=initial,fallback=initial;reference.draws.push_back(invalid);fallback.draws.push_back(invalid);
            bool reference_fault=false,fallback_fault=false;
            try {reference.rasterize_pending_draws();} catch(const std::runtime_error&) {reference_fault=true;}
            counted.accepted=0;hg::gs_sprite_accelerator=relaxed_gpu.get();hg::gs_triangle_accelerator=&counted;
            try {fallback.rasterize_pending_draws();} catch(const std::runtime_error&) {fallback_fault=true;}
            hg::gs_triangle_accelerator=nullptr;hg::gs_sprite_accelerator=nullptr;
            if(reference_fault!=fallback_fault||counted.accepted)
                throw std::runtime_error("wide STQ invalid-coordinate fallback mismatch");
            ++cases;
        }
        if(triangles)for(unsigned variant=0;variant<16;++variant) {
            // HG-PERF-120: own-pixel PSMT8H feedback (texture base == frame base,
            // identity UV, q=1). Overlapping triangles must read each pixel's frame
            // alpha after earlier triangles wrote it, exactly like the scalar path.
            hg::GsRegisterState cpu;cpu.ensure_vram();
            for(unsigned i=0;i<cpu.vram.size();++i)cpu.vram[i]=i*0x6d2b79f5u+(i>>7)*0x1000193u;
            cpu.value[0x4c]=(2ull<<16)|((variant&4)?(0x00ff0000ull<<32):0); // FBP 0, 128 wide, optional FBMSK.
            cpu.value[0x4e]=128ull|(1ull<<32);cpu.value[0x47]=0x30000;
            cpu.value[0x40]=(127ull<<16)|(95ull<<48);
            cpu.value[6]=0ull|(2ull<<14)|(0x1bull<<20)|(7ull<<26)|(7ull<<30)|((variant&1)?1ull<<34:0)|(std::uint64_t((variant>>1)&3)<<35);
            cpu.value[8]=1|(1ull<<2); // CLAMP both axes.
            cpu.value[0x42]=0x44ull|(97ull<<32);
            for(unsigned i=0;i<256;++i){const auto rgba=0x2545f491u*(i+3);
                cpu.clut[i]=std::uint16_t(rgba);cpu.clut[i+256]=std::uint16_t(rgba>>16);
                cpu.clut_valid[i]=cpu.clut_valid[i+256]=true;}
            const auto st=[](unsigned pixel){
                const float value=float(pixel*16+8)/float(128*16);std::uint32_t bits;std::memcpy(&bits,&value,4);return bits;};
            const unsigned shapes[3][6]={{2,3,120,10,30,90},{10,80,118,4,64,92},{0,0,127,0,0,95}};
            for(unsigned n=0;n<3;++n){
                hg::GsDraw draw{};draw.primitive=3;draw.count=3;draw.prim_state=0x10|((variant&8)?0x48:0);
                for(unsigned i=0;i<3;++i){
                    const unsigned x=shapes[n][i*2],y=shapes[n][i*2+1];
                    draw.vertices[i].x=x*16;draw.vertices[i].y=y*16;draw.vertices[i].q=0x3f800000u;
                    draw.vertices[i].st=std::uint64_t(st(x))|(std::uint64_t(st(y))<<32);
                    draw.vertices[i].rgba=0x80604020u+0x01020304u*(i+n);draw.vertices[i].z=0x100+n;
                }
                cpu.draws.push_back(draw);
            }
            auto accelerated=cpu;
            hg::gs_sprite_accelerator=nullptr;hg::gs_triangle_accelerator=nullptr;cpu.rasterize_pending_draws();
            triangle_counter.accepted=0;
            hg::gs_sprite_accelerator=gpu.get();hg::gs_triangle_accelerator=&triangle_counter;
            accelerated.rasterize_pending_draws();hg::gs_triangle_accelerator=nullptr;hg::gs_sprite_accelerator=nullptr;
            if(cpu.vram!=accelerated.vram)throw std::runtime_error("HG-PERF-120 self-feedback mismatch "+std::to_string(variant));
            if(!triangle_counter.accepted)throw std::runtime_error("HG-PERF-120 self-feedback did not reach the GPU "+std::to_string(variant));
            ++cases;
        }
        for(unsigned variant=0;variant<192;++variant) {
            hg::GsRegisterState cpu;cpu.ensure_vram();
            for(unsigned i=0;i<cpu.vram.size();++i)cpu.vram[i]=i*0x97a54321u+(i>>9);
            const unsigned filter=variant&1,wrap=(variant>>1)&3,function=(variant>>3)&3;
            const unsigned blend=(variant>>5)%3;
            cpu.value[0x4c]=(4ull<<16)|((variant&64)?(0x00f00f00ull<<32)|480ull:0);
            cpu.value[0x4e]=1ull<<32;cpu.value[0x47]=0x30000;
            cpu.value[0x40]=(255ull<<16)|(191ull<<48);
            cpu.value[6]=8192ull|(4ull<<14)|(8ull<<26)|(8ull<<30)|
                ((variant&128)?0:1ull<<34)|(std::uint64_t(function)<<35);
            cpu.value[0x14]=(std::uint64_t(filter)<<5)|(std::uint64_t(filter)<<6);
            cpu.value[8]=wrap|(std::uint64_t(wrap)<<2)|(7ull<<4)|(191ull<<14)|(3ull<<24)|(159ull<<34);
            cpu.value[0x46]=variant&1;cpu.value[0x49]=(variant>>1)&1;cpu.value[0x4a]=(variant>>2)&1;
            cpu.value[0x42]=0x44ull|(193ull<<32);
            if(blend==2)cpu.value[0x42]=0x9aull|(255ull<<32);
            hg::GsDraw draw{};draw.primitive=6;draw.count=2;draw.prim_state=0x116|(blend?64:0);
            draw.vertices[0].x=3;draw.vertices[0].y=9;draw.vertices[0].uv=24|(40u<<16);
            draw.vertices[1].x=4093;draw.vertices[1].y=3061;
            draw.vertices[1].uv=4071|(3057u<<16);draw.vertices[1].rgba=0xc19357efu;
            if(variant&32)std::swap(draw.vertices[0],draw.vertices[1]);
            auto accelerated=cpu;
            hg::gs_sprite_accelerator=nullptr;cpu.rasterize_sprite(draw);
            hg::gs_sprite_accelerator=gpu.get();accelerated.rasterize_sprite(draw);hg::gs_sprite_accelerator=nullptr;
            if(static_cast<const hg::GsRegisterState&>(accelerated).vram[0]!=cpu.vram[0])
                throw std::runtime_error("GPU single-word read mismatch");
            if(cpu.vram!=accelerated.vram)throw std::runtime_error("GPU differential mismatch case "+std::to_string(variant));
            ++cases;
            if(variant<8) {
                const auto texel=hg::GsRegisterState::psmct32_word(8192*64,256,10,10);
                cpu.vram[texel]=0xff12ab34;accelerated.vram[texel]=0xff12ab34;
                cpu.rasterize_sprite(draw);hg::gs_sprite_accelerator=gpu.get();
                accelerated.rasterize_sprite(draw);hg::gs_sprite_accelerator=nullptr;
                const auto registers=cpu.value;
                cpu.reset_system();accelerated.reset_system();
                if(cpu.vram!=accelerated.vram)throw std::runtime_error("GPU CPU-write/reset preservation mismatch");
                ++cases;
                // Restore draw state for the other differential fixtures below.
                cpu.value=registers;accelerated.value=registers;
            }
            if(variant<16) {
                // Consecutive dispatches read earlier GPU results, then a CPU
                // point and an aliased sprite force visibility before fallback.
                auto batch=cpu;
                auto queued=draw;capture(queued,batch.value);
                batch.draws.push_back(queued);
                queued.explicit_environment=std::make_shared<hg::GsDraw::Environment>(*queued.explicit_environment);
                (*queued.explicit_environment)[6]&=~16383ull;
                (*queued.explicit_environment)[0x4c]|=256;
                batch.draws.push_back(queued);
                hg::GsDraw point{};point.primitive=0;point.count=1;point.prim_state=0;
                point.vertices[0].x=32;point.vertices[0].y=32;point.vertices[0].rgba=0xf1b29374;
                capture(point,batch.value);
                batch.draws.push_back(point);
                queued.explicit_environment=std::make_shared<hg::GsDraw::Environment>(*queued.explicit_environment);
                (*queued.explicit_environment)[6]=((*queued.explicit_environment)[6]&~16383ull)|8192ull;
                batch.draws.push_back(queued); // Source and frame now overlap: CPU fallback.
                auto reference=batch;reference.rasterize_pending_draws();
                hg::gs_sprite_accelerator=gpu.get();batch.rasterize_pending_draws();hg::gs_sprite_accelerator=nullptr;
                if(reference.vram!=batch.vram||reference.rasterized_draw_count!=batch.rasterized_draw_count)
                    throw std::runtime_error("GPU batch ordering mismatch "+std::to_string(variant));
                ++cases;
                auto fault=cpu;fault.draws.clear();
                capture(queued,cpu.value);fault.draws.push_back(queued);
                hg::GsDraw invalid{};invalid.primitive=7;fault.draws.push_back(invalid);
                auto fault_reference=fault;bool cpu_failed=false,gpu_failed=false;
                try{fault_reference.rasterize_pending_draws();}catch(const std::runtime_error&){cpu_failed=true;}
                hg::gs_sprite_accelerator=gpu.get();
                try{fault.rasterize_pending_draws();}catch(const std::runtime_error&){gpu_failed=true;}
                hg::gs_sprite_accelerator=nullptr;
                if(!cpu_failed||!gpu_failed||fault.vram!=fault_reference.vram||
                   fault.rasterized_draw_count!=fault_reference.rasterized_draw_count)
                    throw std::runtime_error("GPU fault boundary mismatch");
                ++cases;
            }
            if(variant<8) {
                // Aliased source must use the row-major CPU path unchanged.
                cpu.value[6]&=~16383ull;accelerated=cpu;
                cpu.rasterize_sprite(draw);hg::gs_sprite_accelerator=gpu.get();
                accelerated.rasterize_sprite(draw);hg::gs_sprite_accelerator=nullptr;
                if(cpu.vram!=accelerated.vram)throw std::runtime_error("GPU feedback fallback mismatch");
                ++cases;
            }
        }
        for(unsigned dependency=0;dependency<13;++dependency) {
            // A CPU write between service batches can be disjoint, overwrite an
            // earlier texture input, or overwrite an earlier pending output.
            // The next draw consumes that CPU write in all three cases.
            hg::GsRegisterState cpu;cpu.ensure_vram();
            for(unsigned i=0;i<cpu.vram.size();++i)cpu.vram[i]=0x9371b9a5u*i;
            cpu.value[0x4c]=2ull<<16;cpu.value[0x4e]=1ull<<32;
            cpu.value[0x47]=0x30000;cpu.value[0x46]=1;
            cpu.value[0x40]=(127ull<<16)|(95ull<<48);
            cpu.value[6]=8192ull|(2ull<<14)|(7ull<<26)|(7ull<<30)|(1ull<<34);
            cpu.value[8]=5;
            hg::GsDraw draw{};draw.primitive=6;draw.count=2;draw.prim_state=0x116;
            draw.vertices[1].x=2048;draw.vertices[1].y=1536;
            draw.vertices[1].uv=2048|(1536u<<16);draw.vertices[1].rgba=0x80808080;
            auto accelerated=cpu;
            const unsigned kind=dependency<4?dependency:(dependency-4)%3;
            const unsigned block=kind==0?4096:kind==1?8192:0;
            const auto write_word=block*64;
            const auto second=[&](hg::GsRegisterState& state){
                if(dependency<4)state.vram[write_word]=0xff193b75;
                else if(dependency<7){
                    auto* span=state.vram.write_span(write_word,16);
                    for(unsigned i=0;i<16;++i)span[i]=0xff193b75+i;
                }else if(dependency<10){
                    state.value[0x50]=(std::uint64_t(block)<<32)|(2ull<<48)|(0x13ull<<56);
                    state.value[0x52]=16|(1ull<<32);state.transfer_x=0;state.transfer_y=0;
                    state.transfer_width=16;state.transfer_remaining=16;
                    state.gif_to_vram_active=true;state.host_transfer_completed=false;
                    state.write_image_qword(0x123456789abcdef0ull,0xfedcba9876543210ull);
                }else{
                    auto* span=state.vram.write_span(write_word+2040,16);
                    for(unsigned i=0;i<16;++i)span[i]=0xff193b75+i;
                }
                state.value[6]=(state.value[6]&~16383ull)|block;
                state.value[0x4c]|=384;
                state.rasterize_sprite(draw);
            };
            cpu.rasterize_sprite(draw);second(cpu);
            hg::gs_sprite_accelerator=gpu.get();gpu->begin_batch();
            accelerated.rasterize_sprite(draw);gpu->end_batch();
            if(dependency==3)for(unsigned format:{0u,1u,2u,10u}) {
                // Read committed GPU output without executing any guest draws.
                // The wrap fixture crosses both coordinate and physical VRAM ends.
                const unsigned base=511,offset=2040;
                accelerated.privileged_pmode=1;
                accelerated.privileged_dispfb[0]=base|(2ull<<9)|(std::uint64_t(format)<<15)|
                    (std::uint64_t(offset)<<32)|(std::uint64_t(offset)<<43);
                accelerated.privileged_display[0]=(134ull<<32)|(69ull<<44);
                hg::GsDraw uncommitted{};uncommitted.primitive=7;
                accelerated.draws.push_back(uncommitted);
                const auto retired=accelerated.rasterized_draw_count;
                const auto image=accelerated.display_image();
                if(accelerated.draws.size()!=1||accelerated.rasterized_draw_count!=retired)
                    throw std::runtime_error("scanout executed an uncommitted guest draw");
                accelerated.draws.clear();
                // Compare conversion against the existing scalar readers.
                const auto& scalar=accelerated;
                for(unsigned y=0;y<70;++y)for(unsigned x=0;x<135;++x) {
                    std::uint32_t expected;
                    if(format==0||format==1) {
                        expected=scalar.pixel32(base*2048,128,offset+x,offset+y);
                        if(format==1)expected=(expected&0xffffffu)|0x80000000u;
                    } else {
                        const auto p=format==2?scalar.pixel16(base*2048,128,offset+x,offset+y):
                                              scalar.pixel16s(base*2048,128,offset+x,offset+y);
                        const auto expand=[](unsigned v){return (v<<3)|(v>>2);};
                        expected=expand(p&31)|(expand((p>>5)&31)<<8)|(expand((p>>10)&31)<<16)|
                            ((p&0x8000)?0x80000000u:0u);
                    }
                    if(image.rgba[y*135+x]!=expected)throw std::runtime_error("bulk scanout pixel mismatch");
                }
                ++cases;
            }
            // Synchronous mode commits at service return; resident mode can
            // keep this work queued and must observe the subsequent CPU write.
            gpu->begin_batch();second(accelerated);gpu->end_batch();
            hg::gs_sprite_accelerator=nullptr;
            if(cpu.vram!=accelerated.vram)throw std::runtime_error("GPU CPU write dependency mismatch "+std::to_string(dependency));
            ++cases;
        }
        for(unsigned dependency=0;dependency<4;++dependency) {
            hg::GsRegisterState initial;initial.ensure_vram();
            for(unsigned i=0;i<initial.vram.size();++i)initial.vram[i]=0x81a35719u*i;
            initial.value[0x4c]=2ull<<16;initial.value[0x4e]=1ull<<32;
            initial.value[0x47]=0x30000;initial.value[0x46]=1;
            initial.value[0x40]=(127ull<<16)|(95ull<<48);
            initial.value[6]=8192ull|(2ull<<14)|(7ull<<26)|(7ull<<30)|(1ull<<34);
            initial.value[8]=5;
            hg::GsDraw draw{};draw.primitive=6;draw.count=2;draw.prim_state=0x116;
            draw.vertices[1].x=2048;draw.vertices[1].y=1536;
            draw.vertices[1].uv=2048|(1536u<<16);draw.vertices[1].rgba=0x80808080;
            auto cpu=initial,accelerated=initial;
            const auto sequence=[&](hg::GsRegisterState& state) {
                state.rasterize_sprite(draw);
                // This 8x8 sprite is deliberately below the GPU size threshold.
                // Exercise disjoint work, GPU output reads, GPU input writes,
                // and GPU output writes before the service batch has ended.
                state.value[0x4c]=(2ull<<16)|(dependency==2?256:dependency==3?0:384);
                state.value[0x4e]=(1ull<<32)|400;
                state.value[6]=(state.value[6]&~16383ull)|(dependency==1?0:4096);
                auto cpu_draw=draw;cpu_draw.vertices[1].x=cpu_draw.vertices[1].y=128;
                cpu_draw.vertices[1].uv=128|(128u<<16);state.rasterize_sprite(cpu_draw);
                state.value[0x4c]=(2ull<<16)|64;
                state.value[6]=initial.value[6];state.rasterize_sprite(draw);
            };
            sequence(cpu);
            hg::gs_sprite_accelerator=gpu.get();gpu->begin_batch();
            sequence(accelerated);gpu->end_batch();hg::gs_sprite_accelerator=nullptr;
            if(cpu.vram!=accelerated.vram)throw std::runtime_error("bounded CPU fallback dependency mismatch "+std::to_string(dependency));
            ++cases;
        }
        for(unsigned variant=0;variant<24;++variant) {
            hg::GsRegisterState initial;initial.ensure_vram();
            for(unsigned i=0;i<initial.vram.size();++i)initial.vram[i]=i*0x3197abefu+0x715923a1u;
            initial.value[0x4c]=2ull<<16;initial.value[0x4e]=(1ull<<32)|128;
            initial.value[0x47]=0x30000;initial.value[0x46]=1;
            initial.value[0x40]=(127ull<<16)|(95ull<<48);
            initial.value[6]=8192ull|(2ull<<14)|(7ull<<26)|(7ull<<30)|(1ull<<34);
            initial.value[8]=5;
            hg::GsDraw draw{};draw.primitive=6;draw.count=2;draw.prim_state=0x116;
            draw.vertices[1].x=2048;draw.vertices[1].y=1536;
            draw.vertices[1].uv=2048|(1536u<<16);draw.vertices[1].rgba=0x80808080;
            auto cpu=initial,accelerated=initial;cpu.rasterize_sprite(draw);
            hg::gs_sprite_accelerator=gpu.get();gpu->begin_batch();accelerated.rasterize_sprite(draw);gpu->end_batch();hg::gs_sprite_accelerator=nullptr;
            // Force actual resident backend acceptance, merge repeated arbitrary
            // masks and duplicate indices, then exercise the PSMT8 caller.
            const std::uint32_t indices[4]={0,3,0,15};
            for(unsigned step=0;step<8;++step){
                const auto mask=step&1?0x0f00f0ffu:0xf0ff0f00u;
                const std::uint32_t values[4]={0x12345678u+step,0x87654321u-step,0x391757abu*step,~step};
                const bool accepted=accelerated.vram.try_masked_write(indices,values,4,mask);
                if(resident&&!accepted)throw std::runtime_error("resident masked fixture did not reach GPU");
                for(unsigned i=0;i<4;++i){
                    cpu.vram[indices[i]]=(cpu.vram[indices[i]]&~mask)|(values[i]&mask);
                    if(!accepted)accelerated.vram[indices[i]]=(accelerated.vram[indices[i]]&~mask)|(values[i]&mask);
                }
            }
            const auto transfer=[&](hg::GsRegisterState& state){
                for(unsigned pass=0;pass<3;++pass){
                    state.value[0x50]=(2ull<<48)|(0x13ull<<56);
                    state.value[0x52]=32|(4ull<<32);state.transfer_x=(variant&1)?16:0;state.transfer_y=variant%4;
                    state.transfer_width=32;state.transfer_remaining=128;
                    state.gif_to_vram_active=true;state.host_transfer_completed=false;
                    for(unsigned n=0;n<(variant&2?3u:8u);++n){
                        state.write_image_qword(0x3197abef715923a1ull*(n+pass+1),0xfedcba9876543210ull+n);
                        if((variant&4)&&n==1){const auto& memory=state.vram;volatile auto observed=memory[0];(void)observed;}
                    }
                }
            };
            transfer(cpu);transfer(accelerated);
            if(variant==0) {
                std::vector<hg::GifTransfer> image(520);
                for(std::size_t n=0;n<image.size();++n)image[n]={hg::GifTransferKind::Image,0,
                    0x718293a4b5c6d7e8ull+n*0x0101010101010101ull,
                    0x192a3b4c5d6e7f80ull+n*0x0303030303030303ull,n==0};
                const auto configure=[](hg::GsRegisterState& state) {
                    state.value[0x50]=(4ull<<32)|(8ull<<48)|(0x13ull<<56);
                    state.value[0x52]=256ull|(32ull<<32);state.transfer_x=2032;state.transfer_y=2046;
                    state.transfer_width=256;state.transfer_remaining=8192;
                    state.gif_to_vram_active=true;state.host_transfer_completed=false;
                };
                configure(cpu);configure(accelerated);
                for(const auto& qword:image)cpu.write_image_qword(qword.low,qword.high);
                accelerated.write_image_transfers(image.data(),image.size());
            }
            if(cpu.transfer_remaining!=accelerated.transfer_remaining||cpu.gif_to_vram_active!=accelerated.gif_to_vram_active||
               cpu.host_transfer_completed!=accelerated.host_transfer_completed)throw std::runtime_error("masked transfer progress mismatch");
            if(variant&8){
                cpu.value[6]=(2ull<<14)|(7ull<<26)|(7ull<<30)|(1ull<<34);cpu.value[0x4c]=(2ull<<16)|64;
                accelerated.value[6]=cpu.value[6];accelerated.value[0x4c]=cpu.value[0x4c];
                cpu.rasterize_sprite(draw);hg::gs_sprite_accelerator=gpu.get();gpu->begin_batch();
                accelerated.rasterize_sprite(draw);gpu->end_batch();hg::gs_sprite_accelerator=nullptr;
            }
            auto copied=accelerated;
            if(cpu.vram!=copied.vram||cpu.vram!=accelerated.vram)throw std::runtime_error("masked transfer dependency mismatch "+std::to_string(variant));
            ++cases;
        }
        { // Memory may outlive its accelerator: teardown must materialize writes
          // and leave the shared observer proxy safe for later CPU access.
            hg::GsRegisterState surviving;surviving.ensure_vram();
            surviving.value[0x4c]=4ull<<16;surviving.value[0x4e]=1ull<<32;
            surviving.value[0x47]=0x30000;surviving.value[0x46]=1;
            surviving.value[0x40]=(255ull<<16)|(191ull<<48);
            surviving.value[6]=8192ull|(4ull<<14)|(8ull<<26)|(8ull<<30)|(1ull<<34);
            surviving.value[8]=5;
            std::fill(surviving.vram.begin()+8192*64,surviving.vram.begin()+8192*64+65536,0x819357e1);
            hg::GsDraw draw{};draw.primitive=6;draw.count=2;draw.prim_state=0x116;
            draw.vertices[1].x=4096;draw.vertices[1].y=3072;draw.vertices[1].uv=4096|(3072u<<16);
            draw.vertices[1].rgba=0x80808080;
            auto reference=surviving;reference.rasterize_sprite(draw);
            auto temporary=hg::make_gl_sprite_accelerator(true);hg::gs_sprite_accelerator=temporary.get();
            surviving.rasterize_sprite(draw);hg::gs_sprite_accelerator=nullptr;
            const std::uint32_t at=0,value=0x7159a3c7u,mask=0xff00ff00u;
            if(!surviving.vram.try_masked_write(&at,&value,1,mask))throw std::runtime_error("teardown masked upload not accepted");
            reference.vram[0]=(reference.vram[0]&~mask)|(value&mask);
            temporary.reset();
            if(surviving.vram!=reference.vram)throw std::runtime_error("GPU observer lifetime mismatch");
            surviving.vram[0]=7;if(surviving.vram[0]!=7)throw std::runtime_error("inactive GPU observer access");
            ++cases;
        }
        gpu->finish();gpu.reset();std::cout<<cases<<" GPU/CPU cases passed\n";
        glfwDestroyWindow(window);glfwTerminate();return 0;
    }catch(const std::exception& e){hg::gs_sprite_accelerator=nullptr;std::cerr<<e.what()<<'\n';
        if(window)glfwDestroyWindow(window);glfwTerminate();return 1;}
}
