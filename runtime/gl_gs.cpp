#include "opengl_support.hpp"
#include "gl_gs.hpp"
#include "gl_triangle_shader.hpp"
#include "hg/gs_triangle_acceleration.hpp"
#include "hg/native_psmt8_cache.hpp" // HG-ARCH-008: optional resident epoch observer.
#include <bitset>
#include <chrono>
#include <cstring>

namespace hg {
namespace {
constexpr GLenum storage=0x90d2,stream_draw=0x88e0,compute_shader=0x91b9;
using BindBase=void(APIENTRYP)(GLenum,GLuint,GLuint);
using SubData=void(APIENTRYP)(GLenum,std::ptrdiff_t,std::ptrdiff_t,const void*);
using GetData=void(APIENTRYP)(GLenum,std::ptrdiff_t,std::ptrdiff_t,void*);
using Dispatch=void(APIENTRYP)(GLuint,GLuint,GLuint);
using Barrier=void(APIENTRYP)(GLbitfield);
using Uniform1ui=void(APIENTRYP)(GLint,GLuint);
// Independent port of this project's CT32 sprite pixel pipeline. Host setup
// supplies exact addresses/fractions; no normalized texture sampling is used.
constexpr const char* source=R"GLSL(#version 430 core
layout(local_size_x=16,local_size_y=16) in;
layout(std430,binding=0) buffer Vram {uint mem[];};
layout(std430,binding=1) readonly buffer Job {uint data[];};
uniform uint job_base;
uvec4 shape,state,extra,geometry,depth,other;
uvec4 read4(uint p){return uvec4(data[p],data[p+1],data[p+2],data[p+3]);}
uint byte_at(uint c,uint s){return (c>>s)&255u;}
uint tfx(uint t){
    uint ta=t>>24,fa=shape.z>>24,result=0;
    for(uint s=0;s<24;s+=8){
        uint product=min((byte_at(t,s)*byte_at(shape.z,s))>>7,255u);
        uint c=shape.w==1?byte_at(t,s):shape.w>=2?min(product+fa,255u):product;
        result|=c<<s;
    }
    uint a=(state.x&1u)==0?fa:shape.w==0?min((ta*fa)>>7,255u):
        (shape.w==1||shape.w==3)?ta:min(ta+fa,255u);
    return result|(a<<24);
}
int color(uint selector,uint src,uint dst,uint shift){
    return selector==0?int(byte_at(src,shift)):selector==1?int(byte_at(dst,shift)):0;
}
bool alpha_test_pass(uint alpha,uint test){
    if((test&1u)==0u)return true;
    uint mode=(test>>1)&7u,reference=(test>>4)&255u;
    if(mode==0u)return false;if(mode==1u)return true;if(mode==2u)return alpha<reference;
    if(mode==3u)return alpha<=reference;if(mode==4u)return alpha==reference;
    if(mode==5u)return alpha>=reference;if(mode==6u)return alpha>reference;return alpha!=reference;
}
uint expand16(uint bits,bool is_source){
    uint r=bits&31u,g=(bits>>5)&31u,b=(bits>>10)&31u;
    uint alpha=is_source?((bits&32768u)!=0?extra.w&255u:extra.z&255u):((bits&32768u)!=0?128u:0u);
    if(is_source&&(extra.z&32768u)!=0&&(bits&32767u)==0)alpha=0;
    return ((r<<3)|(r>>2))|(((g<<3)|(g>>2))<<8)|(((b<<3)|(b>>2))<<16)|(alpha<<24);
}
uint texel(uint address){
    uint bits=mem[address&0xfffffu];
    if(extra.x==2u||extra.x==10u)return expand16((bits>>((address>>31)*16u))&65535u,true);
    if(extra.x==1u||extra.x==49u){uint rgb=bits&0xffffffu;return rgb|(((extra.z&32768u)!=0&&rgb==0)?0u:(extra.z&255u)<<24);}
    if(extra.x==27u)return data[other.y+(bits>>24)];
    return bits;
}
// PSMT8/PSMT4: same page/block/column/lane layout as psmt8_word/psmt4_word.
uint indexed_texel(uint x,uint y){
    uint base=data[other.y+256u],width=data[other.y+257u],column=(y&15u)>>2,block,address,index;
    uint row=(y&3u)^((column&1u)*2u);
    uint pixel=((x&1u)+(x&6u)*2u+(row&1u)*2u)^((row&2u)*4u);
    if(extra.x==19u){
        uint bx=(x>>4)&7u,by=(y>>4)&3u;
        block=(bx&1u)+(bx&2u)*2u+(bx&4u)*4u+(by&1u)*2u+(by&2u)*4u;
        address=(base+((x>>7)+(y>>6)*(width>>7))*2048u+block*64u+column*16u+pixel)&0xfffffu;
        index=(mem[address]>>((((y&2u)>>1)|((x&8u)>>2))*8u))&255u;
    }else{
        uint bx=(x>>5)&3u,by=(y>>4)&7u;
        block=(bx&1u)*2u+(bx&2u)*4u+(by&1u)+(by&2u)*2u+(by&4u)*4u;
        address=(base+((x>>7)+(y>>7)*(width>>7))*2048u+block*64u+column*16u+pixel)&0xfffffu;
        index=(mem[address]>>((((y&2u)>>1)|((x&24u)>>2))*4u))&15u;
    }
    return data[other.y+index];
}
uint fetch(uint r,uint c){return extra.x==19u||extra.x==20u?indexed_texel(c,r):texel(r+c);}
uint zaddress(uint x,uint y){
    uint bx=(x>>3)&7u,by=(y>>3)&3u;
    uint block=((bx&1u)+(bx&2u)*2u+(bx&4u)*4u+(by&1u)*2u+(by&2u)*4u)^24u;
    return (geometry.w+((x>>6)+(y>>5)*(geometry.z>>6))*2048u+block*64u+((y&7u)>>1)*16u+(x&1u)+(x&6u)*2u+(y&1u)*2u)&0xfffffu;
}
void pixel(uvec2 xy){
    uvec4 col=read4(job_base+24+xy.x*4),row=read4(job_base+24+(shape.x+xy.y)*4);uint a=col.w,b=row.w;
    uint src=shape.z;
    if((state.x&32u)==0){
    src=fetch(row.x,col.x);
    if(a!=0||b!=0){
        uint c1=a!=0?fetch(row.x,col.y):0u;
        uint c2=b!=0?fetch(row.y,col.x):0u;
        uint c3=a!=0&&b!=0?fetch(row.y,col.y):0u;
        uint w0=(16u-a)*(16u-b),w1=a*(16u-b),w2=(16u-a)*b,w3=a*b;
        uint rb=(src&0x00ff00ffu)*w0+(c1&0x00ff00ffu)*w1+(c2&0x00ff00ffu)*w2+(c3&0x00ff00ffu)*w3;
        uint ga=((src>>8)&0x00ff00ffu)*w0+((c1>>8)&0x00ff00ffu)*w1+((c2>>8)&0x00ff00ffu)*w2+((c3>>8)&0x00ff00ffu)*w3;
        src=((rb>>8)&0x00ff00ffu)|(ga&0xff00ff00u);
    }
    src=tfx(src);
    }
    bool alpha_ok=alpha_test_pass(src>>24,depth.y);uint afail=(depth.y>>12)&3u;
    if(!alpha_ok&&afail==0u)return;
    bool do_frame=alpha_ok||afail==1u||afail==3u;
    bool do_z=other.w!=0u&&(alpha_ok||afail==2u);
    uint address=(row.z+col.z)&0xfffffu,oldword=mem[address],dst=oldword,result_color=0;
    uvec2 location=geometry.xy+xy;bool halfword=extra.y==2u||extra.y==10u;
    uint halfshift=(location.x&8u)!=0?16u:0u;
    if(halfword)dst=expand16((oldword>>halfshift)&65535u,false);
    else if(extra.y==1u||extra.y==49u)dst=(dst&0xffffffu)|0x80000000u;
    if((depth.y&0x4000u)!=0&&extra.y!=1u&&extra.y!=49u&&((dst>>31)!=((depth.y>>15)&1u)))return;
    uint ztest=(depth.y>>17)&3u,za=0,oldz=0,zmask=depth.x==1u?0xffffffu:0xffffffffu;
    if(ztest>=2u||other.w!=0u){
        za=zaddress(location.x,location.y);oldz=mem[za];uint storedz=oldz&zmask;
        if((ztest==2u&&other.z<storedz)||(ztest==3u&&other.z<=storedz))return;
    }
    if(do_frame){
    int dither=0;
    if(halfword&&other.x!=0u){uint shift=((location.y&3u)*4u+(location.x&3u))*4u;
        uint raw=(shift<32u?depth.z>>shift:depth.w>>(shift-32u))&7u;dither=int(raw)-((raw&4u)!=0?8:0);}
    bool blend=(state.x&16u)!=0&&((state.x&8u)==0||(src&0x80000000u)!=0);
    for(uint s=0;s<24;s+=8){
        int c=int(byte_at(src,s));
        if(blend){
            uint sel=(state.y>>4)&3u;
            int amount=int(sel==0?src>>24:sel==1?dst>>24:state.z);
            int product=(color(state.y&3u,src,dst,s)-color((state.y>>2)&3u,src,dst,s))*amount;
            c=(product>=0?product/128:-((-product+127)/128))+color((state.y>>6)&3u,src,dst,s);
        }
        c+=dither;
        result_color|=((state.x&2u)!=0?uint(clamp(c,0,255)):uint(c)&255u)<<s;
    }
    result_color|=(src|((state.x&4u)!=0?0x80000000u:0u))&0xff000000u;
    if(halfword){
        uint bits=((result_color>>3)&31u)|((result_color>>6)&992u)|((result_color>>9)&31744u)|((result_color>>16)&32768u);
        uint mask=((state.w>>3)&31u)|((state.w>>6)&992u)|((state.w>>9)&31744u)|((state.w>>16)&32768u);
        uint result=((oldword>>halfshift)&mask)|(bits&~mask);
        mem[address]=(oldword&~(65535u<<halfshift))|((result&65535u)<<halfshift);
    }else{uint mask=state.w|((extra.y==1u||extra.y==49u)?0xff000000u:0u);
        if(!alpha_ok&&afail==3u&&extra.y==0u)mask|=0xff000000u;
        mem[address]=(oldword&mask)|(result_color&~mask);}
    }
    if(do_z)mem[za]=(oldz&~zmask)|(other.z&zmask);
}
void main(){
    shape=read4(job_base);state=read4(job_base+4);extra=read4(job_base+8);
    geometry=read4(job_base+12);depth=read4(job_base+16);other=read4(job_base+20);
    uvec2 xy=gl_GlobalInvocationID.xy;if(xy.x>=shape.x||xy.y>=shape.y)return;
    bool halfword=extra.y==2u||extra.y==10u;
    // One invocation owns both16-bit lanes of a word. No inter-invocation
    // read/modify/write race, even when a scissor starts or ends mid-pair.
    if(halfword&&((geometry.x+xy.x)&8u)!=0&&xy.x>=8u)return;
    pixel(xy);
    if(halfword&&((geometry.x+xy.x)&8u)==0&&xy.x+8u<shape.x)pixel(xy+uvec2(8,0));
})GLSL";

// HG-DIAG-017: one unique word per invocation; merged host masks retain exact
// last-writer behavior without racing separate byte writes to the same word.
constexpr const char* masked_source=R"GLSL(#version 430 core
layout(local_size_x=64) in;
layout(std430,binding=0) buffer Memory {uint words[];};
layout(std430,binding=1) readonly buffer Jobs {uint jobs[];};
uniform uint job_count;
void main(){uint i=gl_GlobalInvocationID.x;if(i>=job_count)return;
 uint at=jobs[i*3],mask=jobs[i*3+1],value=jobs[i*3+2];
 words[at]=(words[at]&~mask)|(value&mask);
}
)GLSL";
class Accelerator final:public GsSpriteAccelerator,public GsTriangleAccelerator {
    ProbeGl gl;
    BindBase bind_base=gl_proc<BindBase>("glBindBufferBase");
    SubData sub_data=gl_proc<SubData>("glBufferSubData");
    GetData get_data=gl_proc<GetData>("glGetBufferSubData");
    Dispatch dispatch=gl_proc<Dispatch>("glDispatchCompute");
    Barrier barrier=gl_proc<Barrier>("glMemoryBarrier");
    Uniform1ui uniform1ui=gl_proc<Uniform1ui>("glUniform1ui");
    GLuint program=0,triangle_program=0,masked_program=0,buffers[3]{};
    GLint masked_count_location=-1;
    bool masked_queued=false;
    std::vector<std::uint32_t> masked_index;
    std::uint64_t masked_words=0,masked_batches=0;
    GLint base_location=-1;
    std::array<std::uint32_t,24+4096*4+258> job_data{};
    struct Command {GLuint offset,width,height;};
    std::vector<Command> commands;
    std::vector<std::uint32_t> queued_data;
    std::vector<std::uint32_t> triangle_data,tile_data;
    std::array<std::vector<std::uint32_t>,4096> tile_jobs;
    std::array<std::uint32_t,3> triangle_mapping{};
    std::bitset<512> triangle_frames,triangle_depths,triangle_textures;
    unsigned triangle_columns=0,triangle_rows=0,tile_entries=0;
    std::uint64_t triangle_draws=0,triangle_batches=0,native_float_triangles=0;
    std::bitset<512> queued_writes;
    std::bitset<512> queued_reads,cpu_dirty,gpu_dirty;
    // HG-DIAG-017: exact GPU-content mirror, valid only after a host transfer.
    // Broad CPU scopes may dirty unchanged pages; never rely on a hash match.
    std::vector<std::uint32_t> uploaded_copy;
    std::bitset<512> uploaded_valid;
    std::uint32_t* queued_vram=nullptr;
    std::uint32_t* resident_vram=nullptr;
    bool resident=false,native_relaxed=false;
    // HG-PERF-115: resident flushes defer glGetError to the next coherence point
    // (download/finish/owner switch/teardown), before any CPU-visible GPU output.
    bool gl_errors_unchecked=false;
    void check_gl_errors(const char* failure) {
        gl_errors_unchecked=false;
        if(glGetError()!=GL_NO_ERROR){
            if(native_epochs)native_epochs->invalidate_unknown();
            throw std::runtime_error(failure);
        }
    }
    std::shared_ptr<NativeVramPageEpochs> native_epochs; // Null in production.
    struct Observer final:GsMemoryObserver {
        Accelerator* target;
        explicit Observer(Accelerator* owner):target(owner){}
        void access(std::uint32_t* p,std::size_t first,std::size_t count,bool write) override {
            if(target)target->cpu_access(p,first,count,write);
        }
        bool scoped_access(std::uint32_t* p,const std::bitset<512>& reads,const std::bitset<512>& writes) override {
            return target&&target->cpu_scope(p,reads,writes);
        }
        bool masked_write(std::uint32_t* p,const std::uint32_t* indices,const std::uint32_t* values,std::size_t count,std::uint32_t mask) override {
            return target&&target->masked_write(p,indices,values,count,mask);
        }
        bool discard_write(std::uint32_t* p,std::size_t first,std::size_t count) override {
            return target&&target->discard_write(p,first,count);
        }
        void forget(std::uint32_t* p) noexcept override {if(target)target->forget_memory(p);}
    };
    std::shared_ptr<Observer> observer;
    unsigned batch_depth=0;
    std::uint64_t draws=0,pixels=0,alias_rejections=0,flushes=0,flush_ns=0;
    std::uint64_t uploaded_bytes=0,downloaded_bytes=0,readback_ns=0;
    std::uint64_t bulk_cpu_scopes=0,bulk_dirty_scopes=0,bulk_dirty_pages=0;
    static std::bitset<512> pages(std::size_t first,std::size_t count) {
        if(first>1024*1024||count>1024*1024-first)throw std::runtime_error("GS coherence range outside VRAM");
        std::bitset<512> result;
        if(count)for(auto p=first/2048;p<=(first+count-1)/2048;++p)result.set(p);
        return result;
    }
    void download(const std::bitset<512>& requested) {
        const auto dirty=requested&gpu_dirty;if(dirty.none())return;
        const auto start=std::chrono::steady_clock::now();
        barrier(0x200);gl.bind_buffer(storage,buffers[0]);
        for(unsigned p=0;p<512;) {
            if(!dirty[p]){++p;continue;}
            const auto first=p;while(p<512&&dirty[p])++p;
            get_data(storage,first*8192,(p-first)*8192,resident_vram+first*2048);
            std::memcpy(uploaded_copy.data()+first*2048,resident_vram+first*2048,(p-first)*8192);
            for(auto page=first;page<p;++page)uploaded_valid.set(page);
            downloaded_bytes+=std::uint64_t(p-first)*8192;
        }
        check_gl_errors(gl_errors_unchecked?"GS compute dispatch/readback failed":"GS resident readback failed");
        // The readback, unlike glFlush, is a completed visibility fence for
        // every pending producer of these exact pages. Multiple enqueues may
        // collapse to this single physical transfer.
        if(native_epochs)native_epochs->gpu_completed_visible_pages(dirty);
        gpu_dirty&=~dirty;
        readback_ns+=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count();
    }
    bool cpu_scope(std::uint32_t* p,const std::bitset<512>& reads,const std::bitset<512>& writes) {
        if(!resident||p!=resident_vram)return false;
        ++bulk_cpu_scopes;
        const auto visible=reads|writes;
        if((visible&queued_writes).any()||(writes&queued_reads).any())flush();
        const auto dirty=visible&gpu_dirty;
        if(dirty.any()){++bulk_dirty_scopes;bulk_dirty_pages+=dirty.count();}
        download(visible);cpu_dirty|=writes;
        if(native_epochs)native_epochs->cpu_mutable_pages(writes);
        return true;
    }
    void cpu_access(std::uint32_t* p,std::size_t first,std::size_t count,bool write) {
        if(p!=resident_vram)return;
        if(first>1024*1024||count>1024*1024-first)throw std::runtime_error("GS CPU access outside VRAM");
        if(count&&first/2048==(first+count-1)/2048) {
            const auto page=first/2048;
            // HG-DIAG-017: disjoint CPU writes commute with queued GPU work.
            // Order every real dependency before a read or read/modify/write.
            if(queued_writes[page]||(write&&queued_reads[page]))flush();
            if(gpu_dirty[page]){std::bitset<512> one;one.set(page);download(one);}
            if(write){cpu_dirty.set(page);if(native_epochs)native_epochs->cpu_mutable_access(first,count);}
            return;
        }
        const auto affected=pages(first,count);
        if((affected&queued_writes).any()||(write&&(affected&queued_reads).any()))flush();
        download(affected);if(write){
            cpu_dirty|=affected;
            if(native_epochs)native_epochs->cpu_mutable_access(first,count);
        }
    }
    void clear_masked() noexcept {
        if(!masked_queued)return;
        for(std::size_t i=0;i<queued_data.size();i+=3)masked_index[queued_data[i]]=0;
        masked_queued=false;
    }
    bool masked_write(std::uint32_t* p,const std::uint32_t* indices,const std::uint32_t* values,std::size_t count,std::uint32_t mask) {
        if(!resident||p!=resident_vram||!count||count>16)return false;
        const auto page=indices[0]/2048;
        for(std::size_t i=0;i<count;++i)if(indices[i]>=1024*1024||indices[i]/2048!=page)return false;
        if(!gpu_dirty[page]&&!queued_writes[page])return false;
        // Prior draws precede the upload. Subsequent writes to the same word
        // combine masks; no two shader invocations perform competing RMWs.
        if(!commands.empty()||!triangle_data.empty()||queued_data.size()+count*3>1024*1024)flush();
        if(masked_index.empty())masked_index.resize(1024*1024);
        if(native_epochs){std::bitset<512> dirty;dirty.set(page);native_epochs->gpu_queued_write(dirty);}
        masked_queued=true;queued_vram=p;queued_writes.set(page);queued_reads.set(page);
        for(std::size_t i=0;i<count;++i){
            auto& slot=masked_index[indices[i]];
            if(!slot){slot=std::uint32_t(queued_data.size()+1);queued_data.push_back(indices[i]);queued_data.push_back(mask);queued_data.push_back(values[i]&mask);}
            else {const auto at=slot-1;queued_data[at+1]|=mask;queued_data[at+2]=(queued_data[at+2]&~mask)|(values[i]&mask);}
        }
        masked_words+=count;return true;
    }
    bool discard_write(std::uint32_t* p,std::size_t first,std::size_t count) {
        if(!resident||p!=resident_vram||!count||first%2048||count%2048||first+count>1024*1024)return false;
        const auto affected=pages(first,count);
        if((affected&queued_writes).any()||(affected&queued_reads).any())flush();
        gpu_dirty&=~affected;cpu_dirty|=affected;uploaded_valid&=~affected;
        // HG-ARCH-009: queued users were submitted above; old GPU output for
        // these pages is never downloaded, so stop counting it as pending.
        if(native_epochs){native_epochs->gpu_superseded_pages(affected);native_epochs->cpu_mutable_pages(affected);}
        return true;
    }
    void forget_memory(std::uint32_t* p) noexcept {
        // Destroying/replacing a memory owner discards its unobservable storage.
        // Submitted GL commands own their buffer storage, never this host pointer.
        if(p!=resident_vram)return;
        if(native_epochs)native_epochs->invalidate_unknown();
        if(queued_vram==p){clear_masked();commands.clear();queued_data.clear();clear_triangles();queued_reads.reset();queued_writes.reset();queued_vram=nullptr;}
        resident_vram=nullptr;cpu_dirty.reset();gpu_dirty.reset();uploaded_valid.reset();
    }
    void select_memory(std::uint32_t* p) {
        if(p==resident_vram)return;
        flush();download(gpu_dirty);
        if(gl_errors_unchecked)check_gl_errors("GS compute dispatch/readback failed");
        resident_vram=p;cpu_dirty.set();gpu_dirty.reset();
        uploaded_copy.resize(1024*1024);uploaded_valid.reset();
        // A new owner is fully CPU-visible after old-owner flush/download;
        // CPU write notifications are already installed on this observer.
        if(native_epochs)native_epochs->begin_after_coherent_fence();
    }
    void clear_triangles() {
        triangle_data.clear();for(auto& tile:tile_jobs)tile.clear();
        triangle_frames.reset();triangle_depths.reset();triangle_textures.reset();triangle_rows=0;tile_entries=0;
    }
public:
    bool accepts_native_float_stq() const noexcept override {return false;}
    explicit Accelerator(bool keep_resident,bool triangles,bool relaxed,std::shared_ptr<NativeVramPageEpochs> epochs):
        resident(keep_resident),native_relaxed(relaxed),native_epochs(std::move(epochs)),observer(std::make_shared<Observer>(this)) {
        if(native_epochs&&!resident)throw std::invalid_argument("Native page epochs require resident GL");
        const auto shader=compile_shader(gl,compute_shader,source);
        program=gl.create_program();gl.attach_shader(program,shader);gl.link_program(program);gl.delete_shader(shader);
        GLint okay=0;gl.get_program_iv(program,GL_LINK_STATUS_,&okay);
        if(!okay){std::array<char,2048> log{};gl.get_program_log(program,GLsizei(log.size()),nullptr,log.data());
            gl.delete_program(program);program=0;throw std::runtime_error(std::string("GS compute link: ")+log.data());}
        base_location=gl.get_uniform(program,"job_base");
        if(base_location<0)throw std::runtime_error("GS compute job offset unavailable");
        if(triangles) {
            const auto shader=compile_shader(gl,compute_shader,hg_triangle_shader);
            triangle_program=gl.create_program();gl.attach_shader(triangle_program,shader);gl.link_program(triangle_program);gl.delete_shader(shader);
            gl.get_program_iv(triangle_program,GL_LINK_STATUS_,&okay);
            if(!okay){std::array<char,4096> log{};gl.get_program_log(triangle_program,GLsizei(log.size()),nullptr,log.data());
                throw std::runtime_error(std::string("GS triangle compute link: ")+log.data());}
        }
        if(resident) {
            const auto shader=compile_shader(gl,compute_shader,masked_source);
            masked_program=gl.create_program();gl.attach_shader(masked_program,shader);gl.link_program(masked_program);gl.delete_shader(shader);
            gl.get_program_iv(masked_program,GL_LINK_STATUS_,&okay);
            if(!okay)throw std::runtime_error("GS masked transfer compute link failed");
            masked_count_location=gl.get_uniform(masked_program,"job_count");
            if(masked_count_location<0)throw std::runtime_error("GS masked transfer count unavailable");
        }
        gl.gen_buffers(3,buffers);
        gl.bind_buffer(storage,buffers[0]);gl.buffer_data(storage,4*1024*1024,nullptr,stream_draw);
        gl.bind_buffer(storage,buffers[1]);gl.buffer_data(storage,4*1024*1024,nullptr,stream_draw);
        gl.bind_buffer(storage,buffers[2]);gl.buffer_data(storage,4*1024*1024,nullptr,stream_draw);
        if(glGetError()!=GL_NO_ERROR)throw std::runtime_error("GS compute buffer allocation failed");
    }
    ~Accelerator() override {
        try {finish();}catch(const std::exception& e){std::cerr<<"GS coherence cleanup failed: "<<e.what()<<'\n';std::terminate();}
        observer->target=nullptr;
        if(native_epochs)native_epochs->invalidate_unknown(); // No longer observing CPU writes.
        std::cerr<<"GS GPU sprites: draws="<<draws<<" pixels="<<pixels<<" alias_rejections="<<alias_rejections
                 <<" flushes="<<flushes<<" flush_ns="<<flush_ns<<" upload_bytes="<<uploaded_bytes
                 <<" download_bytes="<<downloaded_bytes<<" readback_ns="<<readback_ns
                 <<" bulk_cpu_scopes="<<bulk_cpu_scopes<<" bulk_dirty_scopes="<<bulk_dirty_scopes
                 <<" bulk_dirty_pages="<<bulk_dirty_pages<<'\n';
        if(triangle_program)std::cerr<<"GS GPU triangles: draws="<<triangle_draws<<" batches="<<triangle_batches<<" native_float_stq="<<native_float_triangles<<'\n';
        if(masked_program)gl.delete_program(masked_program);
        std::cerr<<"GS GPU masked transfers: words="<<masked_words<<" batches="<<masked_batches<<'\n';
        gl.delete_buffers(3,buffers);if(program)gl.delete_program(program);if(triangle_program)gl.delete_program(triangle_program);
    }
    void begin_batch() override {++batch_depth;}
    void end_batch() override {if(batch_depth)--batch_depth;if(!resident)flush();}
    void finish() override {
        flush();
        if(resident){download(gpu_dirty);if(gl_errors_unchecked)check_gl_errors("GS compute dispatch/readback failed");}
    }
    void flush() override {
        if(commands.empty()&&triangle_data.empty()&&!masked_queued)return;
        const auto start=std::chrono::steady_clock::now();
        gl.bind_buffer(storage,buffers[0]);
        if(resident) {
            const auto needed=cpu_dirty&(queued_reads|queued_writes);
            auto upload=needed;
            for(unsigned p=0;p<512;++p)if(upload[p]&&uploaded_valid[p]&&
                std::memcmp(uploaded_copy.data()+p*2048,queued_vram+p*2048,8192)==0)upload.reset(p);
            for(unsigned p=0;p<512;) {
                if(!upload[p]){++p;continue;}
                const auto first=p;while(p<512&&upload[p])++p;
                sub_data(storage,first*8192,(p-first)*8192,queued_vram+first*2048);
                std::memcpy(uploaded_copy.data()+first*2048,queued_vram+first*2048,(p-first)*8192);
                for(auto page=first;page<p;++page)uploaded_valid.set(page);
                uploaded_bytes+=std::uint64_t(p-first)*8192;
            }
            cpu_dirty&=~needed;
        } else {sub_data(storage,0,4*1024*1024,queued_vram);uploaded_bytes+=4*1024*1024;}
        bind_base(storage,0,buffers[0]);
        const auto& payload=triangle_data.empty()?queued_data:triangle_data;
        gl.bind_buffer(storage,buffers[1]);sub_data(storage,0,payload.size()*4,payload.data());bind_base(storage,1,buffers[1]);
        if(masked_queued) {
            const auto count=GLuint(queued_data.size()/3);gl.use_program(masked_program);
            uniform1ui(masked_count_location,count);dispatch((count+63)/64,1,1);barrier(0x2000);++masked_batches;
        } else if(!triangle_data.empty()) {
            const auto count=triangle_columns*triangle_rows;tile_data.clear();tile_data.resize(count*2);
            for(unsigned i=0;i<count;++i){tile_data[i*2]=GLuint(tile_data.size());tile_data[i*2+1]=GLuint(tile_jobs[i].size());
                tile_data.insert(tile_data.end(),tile_jobs[i].begin(),tile_jobs[i].end());}
            gl.bind_buffer(storage,buffers[2]);sub_data(storage,0,tile_data.size()*4,tile_data.data());bind_base(storage,2,buffers[2]);
            gl.use_program(triangle_program);dispatch(triangle_columns,triangle_rows,1);barrier(0x2000);++triangle_batches;
        } else {
            gl.use_program(program);
            for(const auto& command:commands) {
                uniform1ui(base_location,command.offset);dispatch((command.width+15)/16,(command.height+15)/16,1);
                barrier(0x2000); // GL_SHADER_STORAGE_BARRIER_BIT: preserve draw order/feedback across dispatches.
            }
        }
        barrier(0x00000200); // GL_BUFFER_UPDATE_BARRIER_BIT: subsequent CPU buffer readback.
        gl.bind_buffer(storage,buffers[0]);
        // Resident work is only read back at a later coherence point; submit it
        // now so the GPU overlaps guest execution instead of starting at the wait.
        if(resident)glFlush();
        if(resident){gpu_dirty|=queued_writes;cpu_dirty&=~queued_writes;uploaded_valid&=~queued_writes;}
        else for(unsigned page=0;page<512;) {
            if(!queued_writes[page]){++page;continue;}
            const auto first=page;while(page<512&&queued_writes[page])++page;
            get_data(storage,first*8192,(page-first)*8192,queued_vram+first*2048);
            downloaded_bytes+=std::uint64_t(page-first)*8192;
        }
        clear_masked();commands.clear();queued_data.clear();if(!triangle_data.empty())clear_triangles();queued_reads.reset();queued_writes.reset();queued_vram=nullptr;
        // HG-PERF-115: the resident post-glFlush poll blocked stage C in the
        // driver (~14.5% of its samples); nonresident flushes read back here.
        if(resident)gl_errors_unchecked=true;
        else check_gl_errors("GS compute dispatch/readback failed");
        ++flushes;flush_ns+=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count();
    }
    bool render(const GsSpriteJob& job,GsLocalMemory& memory) override {
        if(job.frame_width&&!batch_depth)return false; // Per-pixel rejection cannot preserve the scalar bool result.
        if(masked_queued||!triangle_data.empty())flush();
        if(!job.width||!job.height||job.width>2048||job.height>2048)throw std::runtime_error("invalid GPU sprite extent");
        if(job.flags&16)for(unsigned s=0;s<8;s+=2)if(((job.alpha>>s)&3)==3)return false;
        // Conservative page ranges include all live texture reads. Reject even
        // page-only alias: cross-invocation feedback is not order-independent.
        std::bitset<512> reads,writes,depth_pages;
        std::uint32_t min_s=0xffffffffu,max_s=0,min_d=0xffffffffu,max_d=0;
        for(unsigned x=0;x<job.width;++x) {
            const auto& c=job.columns[x];
            min_s=std::min(min_s,std::min(c.source0&0xfffffu,c.source1&0xfffffu));max_s=std::max(max_s,std::max(c.source0&0xfffffu,c.source1&0xfffffu));
            min_d=std::min(min_d,c.destination);max_d=std::max(max_d,c.destination);
        }
        const auto mark=[](std::bitset<512>& set,std::uint32_t lo,std::uint32_t hi) {
            for(auto page=lo/2048;page<=hi/2048;++page)set.set(page&511);
        };
        const bool indexed=!(job.flags&32)&&(job.source_format==0x13||job.source_format==0x14);
        if(indexed) {
            // Axes are wrapped texel coordinates; mark every whole texture page
            // covering their bounding box (PSMT8 pages 128x64, PSMT4 128x128).
            std::uint32_t min_v=0xffffffffu,max_v=0;
            for(unsigned y=0;y<job.height;++y){const auto& r=job.rows[y];
                min_v=std::min({min_v,r.source0,r.source1});max_v=std::max({max_v,r.source0,r.source1});}
            const auto page_height=job.source_format==0x13?64u:128u,pages_per_row=job.texture_width/128;
            for(auto py=min_v/page_height;py<=max_v/page_height;++py)for(auto px=min_s/128;px<=max_s/128;++px) {
                const auto first=job.texture_base+(px+py*pages_per_row)*2048;
                mark(reads,first,first+2047);
            }
        }
        for(unsigned y=0;y<job.height;++y) {
            const auto& r=job.rows[y];
            if(!(job.flags&32)&&!indexed) {
                mark(reads,r.source0+min_s,r.source0+max_s);
                if(r.fraction)mark(reads,r.source1+min_s,r.source1+max_s);
            }
            mark(writes,r.destination+min_d,r.destination+max_d);
        }
        if(job.frame_width&&(((job.test>>17)&3)>=2||job.zwrite)) {
            for(unsigned y=job.first_y/32;y<=(job.first_y+job.height-1)/32;++y)
                mark(depth_pages,job.zbase+(y*(job.frame_width/64)+job.first_x/64)*2048,
                     job.zbase+(y*(job.frame_width/64)+(job.first_x+job.width-1)/64)*2048+2047);
            if((depth_pages&writes).any()){++alias_rejections;return false;}
            if(job.zwrite)writes|=depth_pages;
        }
        const auto independent_halfword_copy=[&] {
            // A bounded in-place case with immutable texture bits: each pixel
            // reads the other half of its own word and no other pixel writes
            // that word. Prove equality from the separable address components.
            if((job.source_format!=2&&job.source_format!=10)||
               (job.destination_format!=2&&job.destination_format!=10)||job.width>8)return false;
            const auto delta=(job.columns[0].source0-job.columns[0].destination)&0xfffffu;
            for(unsigned x=0;x<job.width;++x){
                const auto& col=job.columns[x];
                if(col.fraction||((col.source0-col.destination)&0xfffffu)!=delta||
                   (col.source0>>31)==(((job.first_x+x)>>3)&1))return false;
                for(unsigned earlier=0;earlier<x;++earlier)
                    if(job.columns[earlier].destination==col.destination)return false;
            }
            for(unsigned y=0;y<job.height;++y){const auto& row=job.rows[y];
                if(row.fraction||((row.destination-row.source0)&0xfffffu)!=delta)return false;}
            return true;
        };
        if((reads&writes).any()&&!independent_halfword_copy()){++alias_rejections;return false;}
        reads|=depth_pages;
        if(resident)memory.observe(observer);
        auto* vram=resident?memory.backend_data():memory.data();
        if(resident)select_memory(vram);
        job_data[0]=job.width;job_data[1]=job.height;job_data[2]=job.rgba;job_data[3]=job.function;
        job_data[4]=job.flags;job_data[5]=job.alpha;job_data[6]=job.alpha_fixed;job_data[7]=job.write_mask;
        job_data[8]=job.source_format;job_data[9]=job.destination_format;
        job_data[10]=std::uint32_t(job.texa);job_data[11]=std::uint32_t(job.texa>>32);
        job_data[12]=job.first_x;job_data[13]=job.first_y;job_data[14]=job.frame_width;job_data[15]=job.zbase;
        job_data[16]=job.zformat;job_data[17]=std::uint32_t(job.test);
        job_data[18]=std::uint32_t(job.dimx);job_data[19]=std::uint32_t(job.dimx>>32);
        job_data[20]=job.dither;job_data[22]=job.z;job_data[23]=job.zwrite;
        static_assert(sizeof(GsSpriteAxis)==16);
        std::memcpy(job_data.data()+24,job.columns,job.width*16);
        std::memcpy(job_data.data()+24+job.width*4,job.rows,job.height*16);
        const auto palette_offset=24+(job.width+job.height)*4;
        const auto words=palette_offset+(job.palette?(indexed?258:256):0);
        if(job.palette)std::memcpy(job_data.data()+palette_offset,job.palette,256*4);
        if(indexed){job_data[palette_offset+256]=job.texture_base;job_data[palette_offset+257]=job.texture_width;}
        if((queued_vram&&queued_vram!=vram)||queued_data.size()+words>1024*1024||commands.size()>=256)flush();
        job_data[21]=GLuint(queued_data.size()+palette_offset);
        if(native_epochs)native_epochs->gpu_queued_write(writes); // Before original GPU dispatch.
        queued_vram=vram;queued_writes|=writes;queued_reads|=reads;
        commands.push_back({GLuint(queued_data.size()),job.width,job.height});
        queued_data.insert(queued_data.end(),job_data.begin(),job_data.begin()+words);
        ++draws;pixels+=std::uint64_t(job.width)*job.height;
        if(!batch_depth)flush();return true;
    }
    bool render_triangle(const GsTriangleJob& job,GsLocalMemory& memory) override {
        if(!triangle_program||!batch_depth)return false;
        if(masked_queued)flush();
        const auto& d=job.data;
        std::bitset<512> reads,frames,depths;
        const auto mark=[](std::bitset<512>& set,std::uint32_t first,std::uint32_t last){
            for(auto page=first/2048;page<=last/2048;++page)set.set(page&511);
        };
        const auto rectangle=[&](std::bitset<512>& set,unsigned base){
            for(unsigned y=d[7]/32;y<=(d[9]-1)/32;++y)
                mark(set,base+(y*(d[11]/64)+d[6]/64)*2048,
                     base+(y*(d[11]/64)+(d[8]-1)/64)*2048+2047);
        };
        rectangle(frames,d[10]);rectangle(depths,d[12]);
        if((frames&depths).any())return false;
        if(!(d[15]&GsTriangleJob::untextured_flag)) {
        const auto coordinate_max=[](unsigned mode,unsigned size,unsigned lo,unsigned hi){
            return mode<2?size-1:mode==2?hi:lo|hi;
        };
        const std::uint64_t clamp=d[25]|(std::uint64_t(d[26])<<32);
        const auto maxx=coordinate_max(unsigned(clamp&3),d[23],unsigned((clamp>>4)&1023),unsigned((clamp>>14)&1023));
        const auto maxy=coordinate_max(unsigned((clamp>>2)&3),d[24],unsigned((clamp>>24)&1023),unsigned((clamp>>34)&1023));
        const unsigned xs=(d[22]==0x13||d[22]==0x14)?7:6,ys=d[22]==0x14?7:d[22]==0x13?6:5;
        mark(reads,d[20],d[20]+((maxx>>xs)+(maxy>>ys)*(d[21]>>xs))*2048+2047);
        }
        const auto writes=frames|((d[15]&128)?depths:std::bitset<512>{});
        // HG-PERF-120: an own-pixel feedback job reads only the frame word its
        // invocation later writes; tile lists keep per-pixel submission order.
        if(d[15]&GsTriangleJob::self_feedback_flag)reads.reset();
        if((reads&writes).any())return false;
        const std::array<std::uint32_t,3> mapping{d[10],d[11],d[12]};
        const bool needs_flush=!commands.empty()||(!triangle_data.empty()&&(mapping!=triangle_mapping||
            (reads&queued_writes).any()||(writes&triangle_textures).any()||
            (frames&triangle_depths).any()||(depths&triangle_frames).any()))||
            triangle_data.size()>=1024*320||tile_entries>=512*1024;
        if(needs_flush)flush();
        if(resident)memory.observe(observer);
        auto* vram=resident?memory.backend_data():memory.data();
        if(resident)select_memory(vram);
        if(queued_vram&&queued_vram!=vram)flush();
        if(native_epochs)native_epochs->gpu_queued_write(writes); // Include depth writes.
        queued_vram=vram;queued_reads|=reads|depths;queued_writes|=writes;
        triangle_mapping=mapping;triangle_frames|=frames;triangle_depths|=depths;triangle_textures|=reads;
        const auto offset=GLuint(triangle_data.size());
        triangle_data.insert(triangle_data.end(),d.begin(),d.end());
        if(native_relaxed&&!(d[GsTriangleJob::FLAGS]&GsTriangleJob::untextured_flag)) {
            const bool prepacked=(d[GsTriangleJob::FLAGS]&GsTriangleJob::native_float_stq_flag)!=0;
            triangle_data[offset+GsTriangleJob::FLAGS]|=GsTriangleJob::native_float_stq_flag;
            const unsigned coefficients[]{GsTriangleJob::S,GsTriangleJob::S+2,GsTriangleJob::S+4,
                GsTriangleJob::T,GsTriangleJob::T+2,GsTriangleJob::T+4,
                GsTriangleJob::Q,GsTriangleJob::Q+2,GsTriangleJob::Q+4};
            if(!prepacked)for(const auto at:coefficients) {
                const auto encoded=std::uint64_t(triangle_data[offset+at])|
                    (std::uint64_t(triangle_data[offset+at+1])<<32);
                std::int64_t coefficient=0;std::memcpy(&coefficient,&encoded,sizeof(coefficient));
                const float approximation=static_cast<float>(coefficient);
                std::memcpy(&triangle_data[offset+at],&approximation,sizeof(approximation));
                triangle_data[offset+at+1]=0;
            }
            ++native_float_triangles;
        }
        triangle_data[offset+GsTriangleJob::PALETTE]=GLuint(triangle_data.size());
        triangle_data.insert(triangle_data.end(),job.palette.begin(),job.palette.end());
        triangle_columns=d[11]/16;triangle_rows=std::max(triangle_rows,(d[9]+15)/16);
        for(unsigned y=d[7]/16;y<=(d[9]-1)/16;++y)for(unsigned x=d[6]/16;x<=(d[8]-1)/16;++x){
            tile_jobs[y*triangle_columns+x].push_back(offset);++tile_entries;}
        ++triangle_draws;return true;
    }
};
}
std::unique_ptr<GsSpriteAccelerator> make_gl_sprite_accelerator(bool resident,bool triangles,bool native_relaxed,
        std::shared_ptr<NativeVramPageEpochs> native_epochs){
    return std::make_unique<Accelerator>(resident,triangles,native_relaxed,std::move(native_epochs));
}
}

