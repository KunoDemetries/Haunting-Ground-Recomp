#include "opengl_support.hpp"
#include "gl_native_indexed.hpp"
#include <list>

namespace hg {
namespace {
constexpr GLenum storage=0x90d2,stream_draw=0x88e0,compute_shader=0x91b9;
constexpr GLenum r8ui=0x8232,red_integer=0x8d94,texture0=0x84c0;
using BindBase=void(APIENTRYP)(GLenum,GLuint,GLuint);
using SubData=void(APIENTRYP)(GLenum,std::ptrdiff_t,std::ptrdiff_t,const void*);
using GetData=void(APIENTRYP)(GLenum,std::ptrdiff_t,std::ptrdiff_t,void*);
using Dispatch=void(APIENTRYP)(GLuint,GLuint,GLuint);
using Barrier=void(APIENTRYP)(GLbitfield);
using Uniform1ui=void(APIENTRYP)(GLint,GLuint);
using Uniform4ui=void(APIENTRYP)(GLint,GLuint,GLuint,GLuint,GLuint);
using TexStorage2D=void(APIENTRYP)(GLenum,GLsizei,GLenum,GLsizei,GLsizei);
using ActiveTexture=void(APIENTRYP)(GLenum);
// Independently written from this project's gs_wrap_texture_coordinate and
// point_sample_tex0 (PSMT8, CT32 CLUT, CSM1, CSA=0). REPEAT sizes are the
// power-of-two TEX0 dimensions, so the two's-complement mask equals the
// CPU's non-negative modulo for every int32 coordinate.
constexpr const char* sampler_source=R"GLSL(#version 430 core
layout(local_size_x=64) in;
layout(binding=0) uniform usampler2D indices;
layout(std430,binding=1) readonly buffer Palette {uint palette[];};
layout(std430,binding=2) readonly buffer Coordinates {int uv[];};
layout(std430,binding=3) writeonly buffer Result {uint rgba[];};
uniform uint sample_count;
uniform uvec4 wrap_u,wrap_v; // mode,nominal size,minimum,maximum
uint wrap(int c,uvec4 w){
    if(w.x==0u)return uint(c)&(w.y-1u);
    if(w.x==1u)return c<0?0u:(c>=int(w.y)?w.y-1u:uint(c));
    if(w.x==2u)return c<int(w.z)?w.z:(c>int(w.w)?w.w:uint(c));
    return (uint(c)&w.z)|w.w;
}
void main(){
    uint i=gl_GlobalInvocationID.x;if(i>=sample_count)return;
    uint x=wrap(uv[i*2u],wrap_u),y=wrap(uv[i*2u+1u],wrap_v);
    rgba[i]=palette[texelFetch(indices,ivec2(x,y),0).r];
})GLSL";
struct TextureEntry {std::uint64_t id;GLuint name;};
struct PaletteEntry {std::uint64_t id;GLuint name;};
constexpr std::size_t texture_capacity=8,palette_capacity=8;
}

struct NativeIndexedGlResidency::State {
    ProbeGl gl;
    BindBase bind_base=gl_proc<BindBase>("glBindBufferBase");
    SubData sub_data=gl_proc<SubData>("glBufferSubData");
    GetData get_data=gl_proc<GetData>("glGetBufferSubData");
    Dispatch dispatch=gl_proc<Dispatch>("glDispatchCompute");
    Barrier barrier=gl_proc<Barrier>("glMemoryBarrier");
    Uniform1ui uniform1ui=gl_proc<Uniform1ui>("glUniform1ui");
    Uniform4ui uniform4ui=gl_proc<Uniform4ui>("glUniform4ui");
    TexStorage2D tex_storage=gl_proc<TexStorage2D>("glTexStorage2D");
    ActiveTexture active_texture=gl_proc<ActiveTexture>("glActiveTexture");
    GLuint program=0,coordinates=0,results=0,scratch_texture=0,scratch_palette=0;
    GLint count_location=-1,wrap_u_location=-1,wrap_v_location=-1;
    std::list<TextureEntry> textures;
    std::list<PaletteEntry> palettes;
    NativeIndexedGlStats stats;

    void check(const char* what) {
        if(glGetError()!=GL_NO_ERROR)throw std::runtime_error(std::string("native indexed GL failure: ")+what);
    }
    GLuint upload_texture(const NativePsmt8LinearTexture& image) {
        if(!image.width||!image.height||image.width>1024||image.height>1024||
           image.indices.size()!=std::size_t(image.width)*image.height)
            throw std::runtime_error("invalid native PSMT8 staging for GL upload");
        GLuint name=0;glGenTextures(1,&name);
        active_texture(texture0);glBindTexture(GL_TEXTURE_2D,name);
        tex_storage(GL_TEXTURE_2D,1,r8ui,GLsizei(image.width),GLsizei(image.height));
        // Integer textures are incomplete with filtering; texelFetch only.
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        GLint alignment=4,row_length=0;
        glGetIntegerv(GL_UNPACK_ALIGNMENT,&alignment);glGetIntegerv(GL_UNPACK_ROW_LENGTH,&row_length);
        glPixelStorei(GL_UNPACK_ALIGNMENT,1);glPixelStorei(GL_UNPACK_ROW_LENGTH,0);
        glTexSubImage2D(GL_TEXTURE_2D,0,0,0,GLsizei(image.width),GLsizei(image.height),
                        red_integer,GL_UNSIGNED_BYTE,image.indices.data());
        glPixelStorei(GL_UNPACK_ALIGNMENT,alignment);glPixelStorei(GL_UNPACK_ROW_LENGTH,row_length);
        if(glGetError()!=GL_NO_ERROR){glDeleteTextures(1,&name);throw std::runtime_error("native indexed GL texture upload failed");}
        ++stats.texture_uploads;stats.uploaded_bytes+=image.indices.size();
        return name;
    }
    GLuint upload_palette(const NativePaletteSnapshot& snapshot) {
        GLuint name=0;gl.gen_buffers(1,&name);gl.bind_buffer(storage,name);
        gl.buffer_data(storage,sizeof(snapshot.rgba),snapshot.rgba.data(),stream_draw);
        if(glGetError()!=GL_NO_ERROR){gl.delete_buffers(1,&name);throw std::runtime_error("native indexed GL palette upload failed");}
        ++stats.palette_uploads;stats.uploaded_bytes+=sizeof(snapshot.rgba);
        return name;
    }
};

NativeIndexedGlResidency::NativeIndexedGlResidency():state_(std::make_unique<State>()) {
    auto& s=*state_;
    const auto shader=compile_shader(s.gl,compute_shader,sampler_source);
    s.program=s.gl.create_program();s.gl.attach_shader(s.program,shader);s.gl.link_program(s.program);s.gl.delete_shader(shader);
    GLint okay=0;s.gl.get_program_iv(s.program,GL_LINK_STATUS_,&okay);
    if(!okay){std::array<char,2048> log{};s.gl.get_program_log(s.program,GLsizei(log.size()),nullptr,log.data());
        s.gl.delete_program(s.program);throw std::runtime_error(std::string("native indexed sampler link: ")+log.data());}
    s.count_location=s.gl.get_uniform(s.program,"sample_count");
    s.wrap_u_location=s.gl.get_uniform(s.program,"wrap_u");
    s.wrap_v_location=s.gl.get_uniform(s.program,"wrap_v");
    if(s.count_location<0||s.wrap_u_location<0||s.wrap_v_location<0)
        throw std::runtime_error("native indexed sampler uniforms unavailable");
    GLuint buffers[2]{};s.gl.gen_buffers(2,buffers);s.coordinates=buffers[0];s.results=buffers[1];
    s.check("sampler setup");
}
NativeIndexedGlResidency::~NativeIndexedGlResidency() {
    auto& s=*state_;
    for(auto& entry:s.textures)glDeleteTextures(1,&entry.name);
    for(auto& entry:s.palettes)s.gl.delete_buffers(1,&entry.name);
    if(s.scratch_texture)glDeleteTextures(1,&s.scratch_texture);
    if(s.scratch_palette)s.gl.delete_buffers(1,&s.scratch_palette);
    GLuint buffers[2]{s.coordinates,s.results};s.gl.delete_buffers(2,buffers);
    if(s.program)s.gl.delete_program(s.program);
}
const NativeIndexedGlStats& NativeIndexedGlResidency::stats() const noexcept {return state_->stats;}

unsigned NativeIndexedGlResidency::texture(const NativePsmt8LinearTexture& image) {
    auto& s=*state_;
    if(!image.content_id) {
        if(s.scratch_texture){glDeleteTextures(1,&s.scratch_texture);s.scratch_texture=0;}
        s.scratch_texture=s.upload_texture(image);return s.scratch_texture;
    }
    for(auto it=s.textures.begin();it!=s.textures.end();++it)
        if(it->id==image.content_id){++s.stats.texture_hits;s.textures.splice(s.textures.begin(),s.textures,it);return s.textures.front().name;}
    const auto name=s.upload_texture(image);
    if(s.textures.size()>=texture_capacity){glDeleteTextures(1,&s.textures.back().name);s.textures.pop_back();}
    s.textures.push_front({image.content_id,name});
    return name;
}
unsigned NativeIndexedGlResidency::palette(const NativePaletteSnapshot& snapshot) {
    auto& s=*state_;
    if(!snapshot.identity) {
        if(s.scratch_palette){s.gl.delete_buffers(1,&s.scratch_palette);s.scratch_palette=0;}
        s.scratch_palette=s.upload_palette(snapshot);return s.scratch_palette;
    }
    for(auto it=s.palettes.begin();it!=s.palettes.end();++it)
        if(it->id==snapshot.identity){++s.stats.palette_hits;s.palettes.splice(s.palettes.begin(),s.palettes,it);return s.palettes.front().name;}
    const auto name=s.upload_palette(snapshot);
    if(s.palettes.size()>=palette_capacity){s.gl.delete_buffers(1,&s.palettes.back().name);s.palettes.pop_back();}
    s.palettes.push_front({snapshot.identity,name});
    return name;
}

std::vector<std::uint32_t> NativeIndexedGlResidency::point_sample(const NativePsmt8LinearTexture& image,
        const NativePaletteSnapshot& snapshot,std::uint64_t clamp,const std::vector<std::int32_t>& uv) {
    auto& s=*state_;
    if(uv.size()%2)throw std::invalid_argument("native indexed sample coordinates must be pairs");
    const unsigned tw=unsigned((image.tex0>>26)&15u),th=unsigned((image.tex0>>30)&15u);
    if(((image.tex0>>20)&63u)!=0x13u||tw>10||th>10)throw std::runtime_error("native indexed sampler requires PSMT8 TEX0");
    const auto axis=[](unsigned mode,unsigned size,unsigned low,unsigned high,unsigned extent) {
        if(mode==2&&low>high)throw std::runtime_error("invalid GS REGION_CLAMP limits");
        // Every reachable wrapped coordinate must lie inside the staged envelope.
        const unsigned reach=mode<2?size-1u:mode==2?high:(low|high);
        if(reach>=extent)throw std::runtime_error("GS CLAMP reaches outside native staged texture");
        return std::array<GLuint,4>{mode,size,low,high};
    };
    const auto u=axis(unsigned(clamp&3u),1u<<tw,unsigned((clamp>>4)&1023u),unsigned((clamp>>14)&1023u),image.width);
    const auto v=axis(unsigned((clamp>>2)&3u),1u<<th,unsigned((clamp>>24)&1023u),unsigned((clamp>>34)&1023u),image.height);
    const auto texture_name=texture(image);
    const auto palette_name=palette(snapshot);
    const auto count=GLuint(uv.size()/2);
    std::vector<std::uint32_t> result(count);
    if(!count)return result;
    s.gl.bind_buffer(storage,s.coordinates);s.gl.buffer_data(storage,uv.size()*4,uv.data(),stream_draw);
    s.gl.bind_buffer(storage,s.results);s.gl.buffer_data(storage,std::size_t(count)*4,nullptr,stream_draw);
    s.bind_base(storage,1,palette_name);s.bind_base(storage,2,s.coordinates);s.bind_base(storage,3,s.results);
    s.active_texture(texture0);glBindTexture(GL_TEXTURE_2D,texture_name);
    s.gl.use_program(s.program);
    s.uniform1ui(s.count_location,count);
    s.uniform4ui(s.wrap_u_location,u[0],u[1],u[2],u[3]);
    s.uniform4ui(s.wrap_v_location,v[0],v[1],v[2],v[3]);
    s.dispatch((count+63)/64,1,1);
    s.barrier(0x200); // GL_BUFFER_UPDATE_BARRIER_BIT before the CPU readback.
    s.gl.bind_buffer(storage,s.results);s.get_data(storage,0,std::ptrdiff_t(count)*4,result.data());
    s.check("shadow point sample");
    return result;
}
}
