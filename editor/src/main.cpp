// Haunting Ground world editor: standalone room viewer (stage 1 of docs/WORLD_EDITOR.md).
// Reads the user's own disc image read-only; shares no code with the game runtime.
#include "gl.hpp"
#include "disc.hpp"
#include "room.hpp"
#include "pick.hpp"
#include "edit.hpp"
#include "library.hpp"
#include "character.hpp"
#include "navedit.hpp"
#include "launch.hpp"
#include "ui.hpp"
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <cmath>
#include <iostream>
#include <functional>
#include <limits>
#include <cctype>
#include <map>
#include <memory>
#include <sstream>

namespace {
using namespace hgw;

const char* vertex_source=R"(#version 330 core
layout(location=0) in vec3 position;
layout(location=1) in vec2 uv;
layout(location=2) in vec4 colour;
uniform mat4 view_projection;
out vec2 v_uv; out vec4 v_colour;
void main(){gl_Position=view_projection*vec4(position,1.0);v_uv=uv;v_colour=colour;})";

// PS2 MODULATE: texel * vertex colour, where vertex 0x80 means 1.0.
const char* fragment_source=R"(#version 330 core
in vec2 v_uv; in vec4 v_colour;
uniform sampler2D image; uniform int textured; uniform int blended; uniform float brightness;
out vec4 result;
void main(){
  vec4 t=textured!=0?texture(image,v_uv):vec4(0.75,0.75,0.75,1.0);
  float a=blended!=0?t.a*clamp(v_colour.a*(255.0/128.0),0.0,1.0):1.0;
  if(blended==0&&t.a<0.25||a<0.004)discard;
  result=vec4(clamp(t.rgb*v_colour.rgb*(255.0/128.0)*brightness,0.0,1.0),a);
})";

struct Mat4 {float m[16]{};};
Mat4 multiply(const Mat4& a,const Mat4& b) {
    Mat4 r;
    for(int c=0;c<4;++c)for(int row=0;row<4;++row) {
        float s=0;for(int k=0;k<4;++k)s+=a.m[k*4+row]*b.m[c*4+k];
        r.m[c*4+row]=s;
    }
    return r;
}
Mat4 perspective(float fov_y,float aspect,float near_plane,float far_plane) {
    Mat4 r;const float f=1.0f/std::tan(fov_y/2);
    r.m[0]=f/aspect;r.m[5]=f;r.m[10]=(far_plane+near_plane)/(near_plane-far_plane);r.m[11]=-1;
    r.m[14]=2*far_plane*near_plane/(near_plane-far_plane);
    return r;
}
// Game space is Y-up (the slot-5 fixed cameras render upright).
Mat4 look(const float eye[3],float yaw,float pitch,float out_forward[3],float out_right[3]) {
    const float f[3]={std::cos(pitch)*std::sin(yaw),std::sin(pitch),-std::cos(pitch)*std::cos(yaw)};
    float r[3]={-f[2],0,f[0]};
    const float rl=std::sqrt(r[0]*r[0]+r[2]*r[2]);r[0]/=rl;r[2]/=rl;
    const float u[3]={r[1]*f[2]-r[2]*f[1],r[2]*f[0]-r[0]*f[2],r[0]*f[1]-r[1]*f[0]};
    for(int i=0;i<3;++i){out_forward[i]=f[i];out_right[i]=r[i];}
    Mat4 v;
    v.m[0]=r[0];v.m[4]=r[1];v.m[8]=r[2];
    v.m[1]=u[0];v.m[5]=u[1];v.m[9]=u[2];
    v.m[2]=-f[0];v.m[6]=-f[1];v.m[10]=-f[2];
    v.m[12]=-(r[0]*eye[0]+r[1]*eye[1]+r[2]*eye[2]);
    v.m[13]=-(u[0]*eye[0]+u[1]*eye[1]+u[2]*eye[2]);
    v.m[14]=f[0]*eye[0]+f[1]*eye[1]+f[2]*eye[2];
    v.m[15]=1;
    return v;
}

GLuint compile(Gl& gl,GLenum kind,const char* source) {
    const GLuint shader=gl.CreateShader(kind);
    gl.ShaderSource(shader,1,&source,nullptr);gl.CompileShader(shader);
    GLint ok=0;gl.GetShaderiv(shader,compile_status,&ok);
    if(!ok){char log[2048]{};gl.GetShaderInfoLog(shader,sizeof log,nullptr,log);throw std::runtime_error(log);}
    return shader;
}

bool interactive=true;
double scroll_input=0;          // mouse-wheel notches since the last frame
std::string typed_input;        // characters typed since the last frame   // false in --screenshot runs: report errors without a dialog

struct Batch {GLuint texture=0;GLint first=0;GLsizei count=0;bool blended=false;};

struct LoadedRoom {
    int id=-1;
    GLuint vao=0,vbo=0;
    std::vector<GLuint> textures;
    std::vector<Batch> batches;
    std::vector<Camera> cameras;
    // Parsed data kept for picking.
    NavMesh nav;std::vector<Shape> shape_list;std::vector<Portal> portal_list;Lighting lighting;
    std::vector<CameraPath> paths;
    std::vector<Mesh> room_meshes;   // slot-3 meshes in slot order (all blocks), for picking/moving
    std::vector<std::size_t> mesh_slot_offset;   // record offset within slot 3, per room_meshes entry
    GLuint pick_vao=0,pick_vbo=0;GLsizei pick_lines=0;
    std::array<std::uint32_t,room_slots> slot_offset{};   // for in-place edits of the PAC bytes
    std::size_t triangles=0,meshes=0;
    float centre[3]{};
    // Navigation overlay: translucent walkable triangles, then border-edge lines.
    GLuint nav_vao=0,nav_vbo=0;GLsizei nav_fill=0,nav_lines=0;std::size_t nav_triangles=0,nav_edges=0;
    // Objects overlay: slot-2 shapes, slot-7 portals, slot-4 lights, slot-5 cameras.
    GLuint obj_vao=0,obj_vbo=0;GLsizei obj_fill=0,obj_lines=0;std::size_t shapes=0,portals=0,lights=0;
    void release(Gl& gl) {
        if(vbo)gl.DeleteBuffers(1,&vbo);
        if(vao)gl.DeleteVertexArrays(1,&vao);
        if(nav_vbo)gl.DeleteBuffers(1,&nav_vbo);
        if(nav_vao)gl.DeleteVertexArrays(1,&nav_vao);
        if(obj_vbo)gl.DeleteBuffers(1,&obj_vbo);
        if(obj_vao)gl.DeleteVertexArrays(1,&obj_vao);
        if(pick_vbo)gl.DeleteBuffers(1,&pick_vbo);
        if(pick_vao)gl.DeleteVertexArrays(1,&pick_vao);
        if(!textures.empty())glDeleteTextures(GLsizei(textures.size()),textures.data());
        *this={};
    }
};

void attach_layout(Gl& gl) {
    gl.EnableVertexAttribArray(0);gl.VertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,sizeof(Vertex),reinterpret_cast<void*>(offsetof(Vertex,x)));
    gl.EnableVertexAttribArray(1);gl.VertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,sizeof(Vertex),reinterpret_cast<void*>(offsetof(Vertex,u)));
    gl.EnableVertexAttribArray(2);gl.VertexAttribPointer(2,4,GL_UNSIGNED_BYTE,GL_TRUE,sizeof(Vertex),reinterpret_cast<void*>(offsetof(Vertex,r)));
}

// Colours use the shader's PS2 scale (0x80 = full); distinct hues per attribute value.
Vertex overlay_vertex(const float p[3],std::uint32_t rgb,std::uint8_t alpha,float lift) {
    return {p[0],p[1]+lift,p[2],0,0,std::uint8_t((rgb>>16&255)/2),std::uint8_t((rgb>>8&255)/2),std::uint8_t((rgb&255)/2),alpha};
}

void build_navigation_overlay(Gl& gl,LoadedRoom& room,const NavMesh& nav) {
    static const std::uint32_t palette[]={0x4fc3f7,0x81c784,0xffb74d,0xba68c8,0xe57373,0xfff176,0x4db6ac,0xf06292,0xa1887f,0x90a4ae};
    std::vector<Vertex> data;
    for(const auto& t:nav.triangles) {
        std::uint32_t colour=palette[(t.attribute&0xff)%10];
        if(t.neighbour[0]<0&&t.neighbour[1]<0&&t.neighbour[2]<0)colour=0x801818;   // unreachable (cut)
        else if(t.flags&1)colour=0xffffff;       // slot-16 bit 0 (runtime flag 0x4000)
        if(t.flags&2)colour=0x202020;            // slot-16 bit 1 (runtime flag 0x80000)
        for(int k=0;k<3;++k)data.push_back(overlay_vertex(t.v[k],colour,0x40,0.3f));
    }
    room.nav_fill=GLsizei(data.size());
    static const std::uint32_t edge_colour[]={0xff2020,0x20ff20,0x2080ff,0xffffff};
    for(const auto& e:nav.edges) {
        if(e.triangle>=nav.triangles.size()||e.edge>2)continue;
        const auto& t=nav.triangles[e.triangle];
        const auto colour=edge_colour[std::min<std::uint32_t>(e.kind,3)];
        data.push_back(overlay_vertex(t.v[e.edge],colour,0x80,0.6f));
        data.push_back(overlay_vertex(t.v[(e.edge+1)%3],colour,0x80,0.6f));
    }
    room.nav_lines=GLsizei(data.size())-room.nav_fill;
    room.nav_triangles=nav.triangles.size();room.nav_edges=nav.edges.size();
    gl.GenVertexArrays(1,&room.nav_vao);gl.BindVertexArray(room.nav_vao);
    gl.GenBuffers(1,&room.nav_vbo);gl.BindBuffer(array_buffer,room.nav_vbo);
    gl.BufferData(array_buffer,std::ptrdiff_t(data.size()*sizeof(Vertex)),data.data(),static_draw);
    attach_layout(gl);
}

void build_object_overlay(Gl& gl,LoadedRoom& room,const NavMesh& nav,const std::vector<Shape>& shapes,
                          const std::vector<Portal>& portals,const Lighting& lighting) {
    std::vector<Vertex> fill,lines;
    const auto line=[&](const float a[3],const float b[3],std::uint32_t c){
        lines.push_back(overlay_vertex(a,c,0x80,0));lines.push_back(overlay_vertex(b,c,0x80,0));};
    // Portal trigger areas as translucent magenta triangles.
    for(const auto& p:portals)for(const auto t:p.area) {
        if(t>=nav.triangles.size())continue;
        for(int k=0;k<3;++k)fill.push_back(overlay_vertex(nav.triangles[t].v[k],0xff40ff,0x50,0.4f));
    }
    for(const auto& sh:shapes) {
        const std::uint32_t c=sh.kind==1?0xffee40:0xff9020;
        const std::size_t n=sh.points.size();
        const std::size_t edges=sh.kind==1?n:n-1;
        for(std::size_t i=0;i<edges;++i) {
            const auto& a=sh.points[i];const auto& b=sh.points[(i+1)%n];
            const float a0[3]={a[0],a[1],a[2]},b0[3]={b[0],b[1],b[2]};
            const float a1[3]={a[0],a[1]+sh.height,a[2]},b1[3]={b[0],b[1]+sh.height,b[2]};
            line(a0,b0,c);line(a1,b1,c);line(a0,a1,c);
            if(sh.kind!=1&&i+1==edges)line(b0,b1,c);
        }
    }
    for(const auto& p:portals) {
        const float lift_a[3]={p.a[0],p.a[1]+6,p.a[2]},lift_b[3]={p.b[0],p.b[1]+6,p.b[2]};
        line(p.a,lift_a,0xff40ff);line(lift_a,lift_b,0xff40ff);line(p.b,lift_b,0xff40ff);
    }
    for(const auto& l:lighting.lights) {
        float m=std::max({l.colour[0],l.colour[1],l.colour[2],1.0f});
        const std::uint32_t c=std::uint32_t(std::min(255.0f,l.colour[0]/m*255))<<16|std::uint32_t(std::min(255.0f,l.colour[1]/m*255))<<8|std::uint32_t(std::min(255.0f,l.colour[2]/m*255));
        for(int axis=0;axis<3;++axis) {
            float a[3]={l.position[0],l.position[1],l.position[2]},b[3]={a[0],a[1],a[2]};
            a[axis]-=2;b[axis]+=2;line(a,b,c);
        }
        for(int i=0;i<32;++i) {   // range circle
            const float t0=i*6.2831853f/32,t1=(i+1)*6.2831853f/32;
            const float a[3]={l.position[0]+l.range*std::cos(t0),l.position[1],l.position[2]+l.range*std::sin(t0)};
            const float b[3]={l.position[0]+l.range*std::cos(t1),l.position[1],l.position[2]+l.range*std::sin(t1)};
            line(a,b,c);
        }
    }
    for(const auto& path:room.paths) {   // eye path bright cyan, target path dark cyan
        if(path.channels.size()<6)continue;
        for(int which=0;which<2;++which) {
            const auto& cx=path.channels[which*3];
            for(std::size_t k=0;k+1<cx.size();++k)for(int step=0;step<8;++step) {
                float a[3],b[3];
                for(int axis=0;axis<3;++axis) {
                    const auto& ch=path.channels[which*3+axis];
                    a[axis]=path_value(ch,k,step/8.0f);b[axis]=path_value(ch,k,(step+1)/8.0f);
                }
                line(a,b,which?0x208080:0x40ffff);
            }
        }
    }
    for(const auto& cam:room.cameras) {
        line(cam.eye,cam.target,0x40ffff);
        for(int axis=0;axis<3;++axis) {
            float a[3]={cam.eye[0],cam.eye[1],cam.eye[2]},b[3]={a[0],a[1],a[2]};
            a[axis]-=1.5f;b[axis]+=1.5f;line(a,b,0x40ffff);
        }
    }
    room.obj_fill=GLsizei(fill.size());room.obj_lines=GLsizei(lines.size());
    room.shapes=shapes.size();room.portals=portals.size();room.lights=lighting.lights.size();
    fill.insert(fill.end(),lines.begin(),lines.end());
    gl.GenVertexArrays(1,&room.obj_vao);gl.BindVertexArray(room.obj_vao);
    gl.GenBuffers(1,&room.obj_vbo);gl.BindBuffer(array_buffer,room.obj_vbo);
    gl.BufferData(array_buffer,std::ptrdiff_t(fill.size()*sizeof(Vertex)),fill.data(),static_draw);
    attach_layout(gl);
}

LoadedRoom load_room(Gl& gl,const std::vector<std::uint8_t>& bytes,int id) {
    LoadedRoom room;room.id=id;
    const RoomPac pac(bytes);
    room.slot_offset=pac.offset;
    const auto geometry=pac.section(3);
    std::vector<std::vector<Mesh>> blocks;
    for(int b=0;b<geometry_blocks;++b)blocks.push_back(parse_meshes(geometry,b));
    for(const auto& block:blocks)for(const auto& mesh:block){room.room_meshes.push_back(mesh);room.mesh_slot_offset.push_back(mesh.offset);}
    // Door models (slot 8) are in door-local space; door k is placed at portal k's point A,
    // rotated about Y by its angle. Of the four A/B x +/-angle conventions only this one
    // seats the door in its frame (room 0x21, camera 3).
    {
        const auto portals=parse_portals(pac.section(7));
        auto doors=parse_door_models(pac.section(8));
        for(std::size_t k=0;k<doors.size()&&k<portals.size();++k) {
            const auto& p=portals[k];
            const float c=std::cos(p.angle),s=std::sin(p.angle);
            const float* at=p.a;
            for(auto& mesh:doors[k]) {
                for(auto& v:mesh.triangles) {
                    const float x=v.x,z=v.z;
                    v.x=at[0]+x*c+z*s;v.y=at[1]+v.y;v.z=at[2]-x*s+z*c;
                }
                blocks[0].push_back(std::move(mesh));
            }
        }
    }
    const auto textures=parse_textures(pac.section(9));
    room.cameras=parse_cameras(pac.section(5));
    const auto nav=parse_navigation(pac.section(0),pac.section(1),pac.section(16));
    for(const auto& block:blocks)room.meshes+=block.size();
    for(const auto& t:textures) {
        GLuint name=0;glGenTextures(1,&name);glBindTexture(GL_TEXTURE_2D,name);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,t.width,t.height,0,GL_RGBA,GL_UNSIGNED_BYTE,t.rgba.data());
        gl.GenerateMipmap(GL_TEXTURE_2D);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        room.textures.push_back(name);
    }
    std::vector<Vertex> all;
    double sum[3]{};
    // Opaque blocks first, then the translucent ones; draws grouped by texture.
    for(const bool translucent:{false,true})for(int b=0;b<geometry_blocks;++b) {
        if((b==1||b==2||b==5)!=translucent)continue;
        for(std::uint32_t t=0;t<=room.textures.size();++t) {
            Batch batch;batch.texture=t<room.textures.size()?room.textures[t]:0;batch.first=GLint(all.size());batch.blended=translucent;
            for(const auto& mesh:blocks[b]) {
                const bool match=t<room.textures.size()?mesh.texture==t:mesh.texture>=room.textures.size();
                if(match)all.insert(all.end(),mesh.triangles.begin(),mesh.triangles.end());
            }
            batch.count=GLsizei(all.size())-batch.first;
            if(batch.count)room.batches.push_back(batch);
        }
    }
    for(const auto& v:all){sum[0]+=v.x;sum[1]+=v.y;sum[2]+=v.z;}
    room.triangles=all.size()/3;
    if(!all.empty())for(int i=0;i<3;++i)room.centre[i]=float(sum[i]/all.size());
    gl.GenVertexArrays(1,&room.vao);gl.BindVertexArray(room.vao);
    gl.GenBuffers(1,&room.vbo);gl.BindBuffer(array_buffer,room.vbo);
    gl.BufferData(array_buffer,std::ptrdiff_t(all.size()*sizeof(Vertex)),all.data(),static_draw);
    gl.EnableVertexAttribArray(0);gl.VertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,sizeof(Vertex),reinterpret_cast<void*>(offsetof(Vertex,x)));
    gl.EnableVertexAttribArray(1);gl.VertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,sizeof(Vertex),reinterpret_cast<void*>(offsetof(Vertex,u)));
    gl.EnableVertexAttribArray(2);gl.VertexAttribPointer(2,4,GL_UNSIGNED_BYTE,GL_TRUE,sizeof(Vertex),reinterpret_cast<void*>(offsetof(Vertex,r)));
    build_navigation_overlay(gl,room,nav);
    room.nav=nav;room.shape_list=parse_shapes(pac.section(2));room.portal_list=parse_portals(pac.section(7));
    room.lighting=parse_lights(pac.section(4));
    room.paths=parse_camera_paths(pac.section(6));
    build_object_overlay(gl,room,nav,room.shape_list,room.portal_list,room.lighting);
    return room;
}

std::filesystem::path find_disc(int argc,char** argv) {
    for(int i=1;i<argc;++i) {
        const std::string a=argv[i];
        if(a.rfind("--",0)!=0)return a;
        if(a=="--room"||a=="--camera"||a=="--screenshot"||a=="--mods"||a=="--move"||a=="--game")++i;
        else if(a=="--save"||a=="--export")continue;
        else if(a=="--export-to")++i;
        else if(a=="--pick")i+=2;
        else if(a=="--nav"||a=="--top"||a=="--check-all"||a=="--no-geometry"||a=="--objects"||a=="--library"||a=="--lib-list"||a=="--play-test")continue;
        else if(a=="--lib-select")++i;
    }
    const std::filesystem::path candidates[]={
        "Haunting Ground (USA)/DATA.CVM","Haunting Ground (USA)/Haunting Ground (USA).iso",
        "../Haunting Ground (USA)/DATA.CVM","../Haunting Ground (USA)/Haunting Ground (USA).iso"};
    for(const auto& c:candidates)if(std::filesystem::exists(c))return c;
    throw std::runtime_error("pass the path to DATA.CVM or the game's .iso");
}

struct Viewer {
    float eye[3]{0,20,0},yaw=0,pitch=-0.3f;
    bool wireframe=false,textured=true,translucent=true,navigation=false,geometry=true,objects=false;
    float brightness=1.0f;
    int camera=-1;
    void use_camera(const Camera& c) {
        eye[0]=c.eye[0];eye[1]=c.eye[1];eye[2]=c.eye[2];
        const float d[3]={c.target[0]-c.eye[0],c.target[1]-c.eye[1],c.target[2]-c.eye[2]};
        const float h=std::sqrt(d[0]*d[0]+d[2]*d[2]);
        yaw=std::atan2(d[0],-d[2]);pitch=std::atan2(d[1],h);
    }
};
}

int main(int argc,char** argv) try {
    int start_room=0x21,start_camera=0;
    std::filesystem::path screenshot;   // render one frame to a PPM file and exit (testing)
    // Saved room edits; the game cannot load them until a host file-override hook exists.
    std::filesystem::path mods_dir;
    if(const char* local=std::getenv("LOCALAPPDATA"))mods_dir=std::filesystem::path(local)/"HauntingGroundRecomp"/"mods";
    else mods_dir="hg-world-editor-mods";
    std::filesystem::path game_override;   // --game PATH (default: found next to the editor/disc)
    std::filesystem::path export_target=mods_dir.parent_path()/"modded-disc"/"Haunting Ground (USA).iso";
    std::vector<std::string> move_script;   // --move "kind index dx dy dz" then --save (testing)
    bool save_after=false;
    for(int i=1;i+1<argc;++i) {
        const std::string a=argv[i];
        if(a=="--room")start_room=std::stoi(argv[i+1],nullptr,16);
        else if(a=="--camera")start_camera=std::stoi(argv[i+1])-1;
        else if(a=="--screenshot"){screenshot=argv[i+1];interactive=false;}
        else if(a=="--mods")mods_dir=argv[i+1];
        else if(a=="--game")game_override=argv[i+1];
        else if(a=="--move")move_script.push_back(argv[i+1]);
    }
    bool start_nav=false,start_top=false,viewer_geometry=true,start_objects=false,pick_request=false;
    double pick_x=0.5,pick_y=0.5;   // --pick NX NY: one simulated click (testing), 0..1 of the window
    for(int i=1;i+2<argc;++i)if(std::string(argv[i])=="--pick"){pick_request=true;pick_x=std::stod(argv[i+1]);pick_y=std::stod(argv[i+2]);}
    bool export_after=false;
    for(int i=1;i<argc;++i){if(std::string(argv[i])=="--save")save_after=true;if(std::string(argv[i])=="--export")export_after=true;}
    for(int i=1;i+1<argc;++i)if(std::string(argv[i])=="--export-to")export_target=argv[i+1];
    const auto save_room=[&](int id,const std::vector<std::uint8_t>& bytes)->std::string {
        try {
            const auto path=room_path(id);
            const auto target=mods_dir/path.substr(1,path.size()-3);   // "/ST_020/ST_021.PAC;1" -> ST_020/ST_021.PAC
            std::filesystem::create_directories(target.parent_path());
            std::ofstream out(target,std::ios::binary|std::ios::trunc);
            out.write(reinterpret_cast<const char*>(bytes.data()),std::streamsize(bytes.size()));
            if(!out)throw std::runtime_error("write failed");
            return "saved "+target.string();
        } catch(const std::exception& e){return std::string("save failed: ")+e.what();}
    };
    for(int i=1;i<argc;++i){const std::string a=argv[i];if(a=="--nav")start_nav=true;if(a=="--top")start_top=true;if(a=="--no-geometry")viewer_geometry=false;if(a=="--objects")start_objects=true;}
    const auto disc_path=find_disc(argc,argv);
    const Disc disc(disc_path);
    const auto& volume=disc.data();
    std::vector<int> rooms;
    for(int id=0;id<room_limit;++id)if(volume.contains(room_path(id)))rooms.push_back(id);
    if(rooms.empty())throw std::runtime_error("no room files found on the disc");
    std::vector<DoorLink> door_links;
    for(std::size_t bytes=door_table_limit*16;bytes&&door_links.empty();bytes-=16)
        if(const auto* table=elf_address(disc.executable(),door_table_address,bytes)){door_links=parse_door_table(table,bytes);break;}
    for(int i=1;i<argc;++i)if(std::string(argv[i])=="--check-all") {
        // Parse every room without a window; cross-checked against editor/hgworld.
        std::size_t triangles=0,textures=0,cameras=0,nav_triangles=0,nav_edges=0,shapes=0,portals=0,lights=0,doors=0,linked=0,paths=0;int failures=0;
        for(const int id:rooms) try {
            const auto bytes=volume.read_file(room_path(id));
            if(join_pac(split_pac(bytes))!=bytes)throw std::runtime_error("section rebuild does not reproduce the file");
            {   const RoomPac check(bytes);const auto s0=check.section(0);
                if(!s0.empty()) {
                    const auto saved=save_nav(load_nav(s0,check.section(16)));
                    if(!std::equal(saved.begin(),saved.end(),s0.begin()))throw std::runtime_error("navigation serializer does not reproduce slot 0");
                }
            }
            const RoomPac pac(bytes);
            for(int b=0;b<geometry_blocks;++b)
                for(const auto& m:parse_meshes(pac.section(3),b))triangles+=m.triangles.size()/3;
            textures+=parse_textures(pac.section(9)).size();
            cameras+=parse_cameras(pac.section(5)).size();
            const auto nav=parse_navigation(pac.section(0),pac.section(1),pac.section(16));
            nav_triangles+=nav.triangles.size();nav_edges+=nav.edges.size();
            for(const auto& door:parse_door_models(pac.section(8))){++doors;for(const auto& m:door)triangles+=m.triangles.size()/3;}
            const auto room_portals=parse_portals(pac.section(7));
            for(std::size_t k=0;k<room_portals.size();++k)if(door_destination(door_links,id,int(k)))++linked;
            shapes+=parse_shapes(pac.section(2)).size();portals+=room_portals.size();
            paths+=parse_camera_paths(pac.section(6)).size();
            lights+=parse_lights(pac.section(4)).lights.size();
        } catch(const std::exception& e){++failures;std::cerr<<"room "<<std::hex<<id<<std::dec<<": "<<e.what()<<'\n';}
        std::cout<<rooms.size()<<" rooms, "<<triangles<<" triangles, "<<textures<<" textures, "
                 <<cameras<<" cameras, "<<nav_triangles<<" nav triangles, "<<nav_edges<<" nav edges, "<<shapes<<" shapes, "<<portals<<" portals ("<<linked<<" linked by "<<door_links.size()<<" door-table entries), "<<doors<<" door models, "<<lights<<" lights, "<<paths<<" camera paths, "
                 <<failures<<" failures\n";
        return failures?1:0;
    }

    if(!glfwInit())throw std::runtime_error("GLFW initialisation failed");
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,3);glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,3);
    glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_SAMPLES,4);
    GLFWwindow* window=glfwCreateWindow(1280,800,"Haunting Ground World Editor",nullptr,nullptr);
    if(!window)throw std::runtime_error("cannot create an OpenGL 3.3 window");
    glfwMakeContextCurrent(window);glfwSwapInterval(1);
    glfwSetScrollCallback(window,[](GLFWwindow*,double,double dy){scroll_input+=dy;});
    glfwSetCharCallback(window,[](GLFWwindow*,unsigned codepoint){if(codepoint>=32&&codepoint<127)typed_input.push_back(char(codepoint));});
    Gl gl;
    const GLuint program=gl.CreateProgram();
    gl.AttachShader(program,compile(gl,vertex_shader,vertex_source));
    gl.AttachShader(program,compile(gl,fragment_shader,fragment_source));
    gl.LinkProgram(program);
    GLint linked=0;gl.GetProgramiv(program,link_status,&linked);
    if(!linked)throw std::runtime_error("shader link failed");
    const GLint u_vp=gl.GetUniformLocation(program,"view_projection"),u_textured=gl.GetUniformLocation(program,"textured"),
        u_bright=gl.GetUniformLocation(program,"brightness"),u_image=gl.GetUniformLocation(program,"image"),
        u_blended=gl.GetUniformLocation(program,"blended");

    std::size_t room_index=0;
    for(std::size_t i=0;i<rooms.size();++i)if(rooms[i]==start_room)room_index=i;
    Viewer viewer;
    LoadedRoom room;
    std::string load_error,status;
    // Editing: modified room files live here until saved. Edits patch floats in place, so
    // a room file's size and layout never change.
    std::map<int,std::vector<std::uint8_t>> edited;
    std::vector<std::uint8_t> room_bytes;
    std::vector<std::vector<std::uint8_t>> undo;
    const auto build=[&](bool keep_view) {
        room.release(gl);load_error.clear();
        try {room=load_room(gl,room_bytes,rooms[room_index]);}
        catch(const std::exception& e){load_error=e.what();room.id=rooms[room_index];}
        if(keep_view)return;
        if(!room.cameras.empty()){viewer.camera=0;viewer.use_camera(room.cameras[0]);}
        else {viewer.camera=-1;viewer.eye[0]=room.centre[0];viewer.eye[1]=room.centre[1]+20;viewer.eye[2]=room.centre[2]+60;}
    };
    // Previously saved edits (valid room files within the room buffer) are picked up again.
    if(std::filesystem::exists(mods_dir))for(const int id:rooms) {
        const auto path=room_path(id);
        const auto saved=mods_dir/path.substr(1,path.size()-3);
        if(!std::filesystem::exists(saved)||std::filesystem::file_size(saved)>room_buffer_limit)continue;
        std::ifstream in(saved,std::ios::binary);
        std::vector<std::uint8_t> bytes(std::istreambuf_iterator<char>(in),{});
        try {(void)RoomPac(bytes);} catch(const std::exception&) {continue;}   // not a room file: ignore
        edited[id]=std::move(bytes);
    }
    int original_cameras=0;
    const auto open_room=[&](std::size_t index) {
        room_index=index;undo.clear();status.clear();
        original_cameras=int(parse_cameras(RoomPac(volume.read_file(room_path(rooms[index]))).section(5)).size());
        const auto found=edited.find(rooms[index]);
        room_bytes=found!=edited.end()?found->second:volume.read_file(room_path(rooms[index]));
        build(false);
    };
    open_room(room_index);
    if(start_camera>0&&start_camera<int(room.cameras.size())){viewer.camera=start_camera;viewer.use_camera(room.cameras[start_camera]);}
    viewer.navigation=start_nav;viewer.geometry=viewer_geometry;viewer.objects=start_objects;
    if(start_top){viewer.camera=-1;viewer.eye[0]=room.centre[0];viewer.eye[1]=room.centre[1]+220;viewer.eye[2]=room.centre[2];viewer.yaw=0;viewer.pitch=-1.5f;}
    if(!screenshot.empty())glfwHideWindow(window);
    bool script_pending=!move_script.empty()||save_after||export_after;
    for(int i=1;i<argc;++i)if(std::string(argv[i])=="--play-test") {
        const auto l=find_game(disc.iso().empty()?disc_path:disc.iso(),std::filesystem::path(argv[0]),game_override);
        std::cout<<"game "<<l.exe.string()<<"\nworking "<<l.working.string()<<"\ntoc "<<l.toc.string()<<"\ncard "<<l.card.string()<<'\n';
        return 0;
    }

    std::array<bool,GLFW_KEY_LAST+1> held{};
    const auto pressed=[&](int key){const bool now=glfwGetKey(window,key)==GLFW_PRESS;const bool edge=now&&!held[key];held[key]=now;return edge;};
    double last_x=0,last_y=0;bool dragging=false,left_held=false;
    std::string selection;int selection_room=-1,selected_destination=-1;
    Kind selected_kind=Kind::none;int selected_index=-1;
    std::vector<int> selected_group;   // meshes moved/copied/removed together (Kind::mesh)
    const auto scene_for=[&]{
        PickScene scene{&room.nav,&room.shape_list,&room.portal_list,&room.lighting,&room.cameras,&door_links,room.id,viewer.navigation,viewer.objects};
        scene.meshes=&room.room_meshes;scene.with_meshes=viewer.objects&&!viewer.navigation&&viewer.geometry;
        return scene;
    };
    const auto upload_highlight=[&](const std::vector<std::array<Vec3,2>>& segments) {
        std::vector<Vertex> lines;
        for(const auto& seg:segments)for(const auto& p:seg){const float q[3]={p[0],p[1],p[2]};lines.push_back(overlay_vertex(q,0xffffff,0x80,0.05f));}
        if(!room.pick_vao){gl.GenVertexArrays(1,&room.pick_vao);gl.BindVertexArray(room.pick_vao);gl.GenBuffers(1,&room.pick_vbo);gl.BindBuffer(array_buffer,room.pick_vbo);attach_layout(gl);}
        gl.BindVertexArray(room.pick_vao);gl.BindBuffer(array_buffer,room.pick_vbo);
        gl.BufferData(array_buffer,std::ptrdiff_t(lines.size()*sizeof(Vertex)),lines.data(),static_draw);
        room.pick_lines=GLsizei(lines.size());
    };
    const auto show_selection=[&](const Selection& chosen) {
        selected_kind=chosen.kind;selected_index=chosen.index;selected_destination=chosen.destination_room;
        selected_group.clear();if(chosen.kind==Kind::mesh)selected_group.push_back(chosen.index);
        selection=chosen.text.empty()?std::string("nothing under the cursor"):chosen.text;
        upload_highlight(chosen.lines);
    };
    // Selects several meshes as one object.
    const auto select_group=[&](std::vector<int> group) {
        group.erase(std::remove_if(group.begin(),group.end(),[&](int i){return i<0||i>=int(room.room_meshes.size());}),group.end());
        if(group.empty()){selected_kind=Kind::none;selected_group.clear();selection.clear();room.pick_lines=0;return;}
        if(group.size()==1){show_selection(describe(scene_for(),Kind::mesh,group[0]));return;}
        std::vector<std::array<Vec3,2>> lines;int triangles=0;
        for(const int i:group){const auto part=describe(scene_for(),Kind::mesh,i);lines.insert(lines.end(),part.lines.begin(),part.lines.end());
                               triangles+=int(room.room_meshes[i].triangles.size()/3);}
        selected_kind=Kind::mesh;selected_index=group[0];selected_group=group;selected_destination=-1;
        selection="object of "+std::to_string(group.size())+" meshes, "+std::to_string(triangles)+" triangles (moves, copies and deletes together)";
        upload_highlight(lines);
    };
    // Moves the selected object by `d` in the room bytes, then rebuilds the room.
    const auto move_selection=[&](Vec3 d) {
        if(selected_kind==Kind::none||selected_kind==Kind::nav||!load_error.empty())return;
        undo.push_back(room_bytes);if(undo.size()>50)undo.erase(undo.begin());
        const auto shift=[&](std::size_t o){for(int k=0;k<3;++k)wrf(room_bytes,o+k*4,rdf(room_bytes,o+k*4)+d[k]);};
        const auto& so=room.slot_offset;
        switch(selected_kind) {
        case Kind::light: shift(so[4]+room.lighting.lights.at(selected_index).offset);break;
        case Kind::camera: {const auto o=so[5]+room.cameras.at(selected_index).offset;shift(o);shift(o+16);break;}
        case Kind::door: {const auto o=so[7]+room.portal_list.at(selected_index).offset;shift(o+4);shift(o+16);break;}
        case Kind::mesh: for(const int i:selected_group) {   // matrix translation (rows x/y/z, column 3)
            const auto o=so[3]+room.mesh_slot_offset.at(i)+16;
            for(int k=0;k<3;++k)wrf(room_bytes,o+12+k*16,rdf(room_bytes,o+12+k*16)+d[k]);
        } break;
        case Kind::shape: {
            const auto& sh=room.shape_list.at(selected_index);
            for(std::size_t k=0;k<sh.points.size();++k)shift(so[2]+sh.offset+16+k*16);
            break;
        }
        default: break;
        }
        edited[room.id]=room_bytes;status="modified (Ctrl+S save, Ctrl+Z undo)";
        const auto kind=selected_kind;const int index=selected_index;const auto group=selected_group;
        build(true);
        if(kind==Kind::mesh)select_group(group);else show_selection(describe(scene_for(),kind,index));
    };
    // Rebuilds the room after changing one section; the file may grow or shrink.
    const auto restructure_all=[&](const std::function<void(PacSections&)>& change)->bool {
        try {
            auto sections=split_pac(room_bytes);
            change(sections);
            for(auto& data:sections.data)while(data.size()%16)data.push_back(0);
            auto rebuilt=join_pac(sections);
            undo.push_back(room_bytes);if(undo.size()>50)undo.erase(undo.begin());
            room_bytes=std::move(rebuilt);edited[room.id]=room_bytes;
            build(true);
            status="modified, room now "+std::to_string(room_bytes.size())+" bytes (Ctrl+S save, Ctrl+Z undo)";
            return true;
        } catch(const std::exception& e){status=std::string("not changed: ")+e.what();return false;}
    };
    const auto restructure=[&](int slot,const std::function<void(std::vector<std::uint8_t>&)>& change)->bool {
        return restructure_all([&](PacSections& sections){
            if(!sections.present[slot]||sections.alias[slot]>=0)throw std::runtime_error("that room section is missing or shared");
            change(sections.data[slot]);
        });
    };
    ObjectClip clipboard;
    const auto group_records=[&]{std::vector<std::uint32_t> r;for(const int i:selected_group)r.push_back(std::uint32_t(room.mesh_slot_offset.at(i)));return r;};
    const auto copy_selection=[&] {
        if(selected_kind!=Kind::mesh){status="select a room mesh to copy (objects overlay on, navigation overlay off)";return;}
        try {
            clipboard=copy_object(room_bytes,group_records(),room.id);
            std::ostringstream text;text<<"copied "<<clipboard.parts.size()<<" mesh(es) from room "<<std::hex<<std::uppercase<<room.id
                                        <<" - open any room and press Ctrl+V to paste";
            status=text.str();
        } catch(const std::exception& e){status=std::string("copy failed: ")+e.what();}
    };
    // Pastes an object with its floor centre at `at` and selects the new meshes.
    const auto paste_object_at=[&](const ObjectClip& object,Vec3 at)->bool {
        const float where[3]={at[0],at[1],at[2]};
        std::vector<std::size_t> placed;
        if(!restructure_all([&](PacSections& sections){placed=paste_object(sections,object,where);}))return false;
        std::vector<int> group;
        for(std::size_t i=0;i<room.mesh_slot_offset.size();++i)
            if(std::find(placed.begin(),placed.end(),room.mesh_slot_offset[i])!=placed.end())group.push_back(int(i));
        select_group(group);
        return true;
    };
    const auto paste_clipboard=[&](Vec3 at) {
        if(!clipboard.valid()){status="nothing copied yet (select a mesh, Ctrl+C)";return;}
        paste_object_at(clipboard,at);
    };
    const auto duplicate_selection=[&](Vec3 offset) {
        const float delta[3]={offset[0],offset[1],offset[2]};
        std::size_t placed=0;int new_index=-1;
        switch(selected_kind) {
        case Kind::mesh: {
            if(selected_group.size()>1) {   // whole object: copy and paste it beside itself
                try {
                    const auto object=copy_object(room_bytes,group_records(),room.id);
                    float lo[3]={1e30f,1e30f,1e30f},hi[3]={-1e30f,-1e30f,-1e30f};
                    for(const int i:selected_group)for(const auto& v:room.room_meshes[i].triangles){
                        const float q[3]={v.x,v.y,v.z};for(int k=0;k<3;++k){lo[k]=std::min(lo[k],q[k]);hi[k]=std::max(hi[k],q[k]);}}
                    paste_object_at(object,{(lo[0]+hi[0])/2+delta[0],lo[1]+delta[1],(lo[2]+hi[2])/2+delta[2]});
                } catch(const std::exception& e){status=std::string("not changed: ")+e.what();}
                return;
            }
            const auto record=room.mesh_slot_offset.at(selected_index);
            if(!restructure(3,[&](auto& d){placed=duplicate_mesh(d,record,delta);}))return;
            for(std::size_t i=0;i<room.mesh_slot_offset.size();++i)if(room.mesh_slot_offset[i]==placed)new_index=int(i);
            break;
        }
        case Kind::light: {
            const auto record=room.lighting.lights.at(selected_index).offset;
            const int count=int(room.lighting.lights.size());
            if(!restructure(4,[&](auto& d){duplicate_light(d,record,delta);}))return;
            new_index=count;break;
        }
        case Kind::camera: {
            const auto record=room.cameras.at(selected_index).offset;
            const int count=int(room.cameras.size());
            if(!restructure(5,[&](auto& d){duplicate_camera(d,record,delta);}))return;
            new_index=count;break;
        }
        case Kind::shape: {
            const int table_index=room.shape_list.at(selected_index).index;int slot_index=-1;
            if(!restructure(2,[&](auto& d){slot_index=duplicate_shape(d,table_index,delta);}))return;
            for(std::size_t i=0;i<room.shape_list.size();++i)if(room.shape_list[i].index==slot_index)new_index=int(i);
            break;
        }
        case Kind::door: status="doors cannot be added: door numbers are tied to the executable's door table";return;
        default: status="select a mesh, light, camera or trigger shape first (objects overlay: O)";return;
        }
        if(new_index>=0)show_selection(describe(scene_for(),selected_kind,new_index));
    };
    const auto remove_selection=[&] {
        bool done=false;
        switch(selected_kind) {
        case Kind::mesh: {
            auto records=group_records();std::sort(records.rbegin(),records.rend());   // last first: offsets stay valid
            done=restructure(3,[&](auto& d){for(const auto r:records)remove_mesh(d,r);});break;
        }
        case Kind::light: {const auto record=room.lighting.lights.at(selected_index).offset;done=restructure(4,[&](auto& d){remove_light(d,record);});break;}
        case Kind::camera: {
            if(selected_index<original_cameras){status="only cameras added in the editor can be removed (the room's own cameras are referenced by number)";return;}
            const auto record=room.cameras.at(selected_index).offset;done=restructure(5,[&](auto& d){remove_camera(d,record);});break;
        }
        case Kind::shape: {const int table_index=room.shape_list.at(selected_index).index;done=restructure(2,[&](auto& d){remove_shape(d,table_index);});break;}
        case Kind::door: status="doors cannot be removed: door numbers are tied to the executable's door table";return;
        default: status="select something to remove first (objects overlay: O)";return;
        }
        if(done){selection.clear();room.pick_lines=0;selected_kind=Kind::none;selected_destination=-1;selected_group.clear();}
    };
    // Blocks the walkable floor under the selected mesh group (footprint = convex hull of its
    // vertices seen from above, grown by 1 unit for the player's width).
    const auto cut_floor_under_selection=[&](bool open_floor) {
        if(selected_kind!=Kind::mesh||selected_group.empty()){status="select an object first (objects overlay on, navigation off), then K";return;}
        std::vector<std::array<float,2>> points;float y_low=1e30f;
        for(const int i:selected_group)for(const auto& v:room.room_meshes[i].triangles){points.push_back({v.x,v.z});y_low=std::min(y_low,v.y);}
        auto footprint=convex_hull(points);
        if(footprint.size()<3){status="the selection has no footprint";return;}
        expand(footprint,1.0f);   // same margin for cutting and re-opening
        CutResult result;
        const bool ok=restructure_all([&](PacSections& sections){
            if(!sections.present[0]||sections.alias[0]>=0)throw std::runtime_error("this room has no navigation mesh");
            const auto before=load_nav(sections.data[0],sections.present[16]?sections.data[16]:std::vector<std::uint8_t>{});
            auto nav=before;
            if(open_floor) {
                const auto filled=fill_footprint(nav,footprint,y_low-3.0f,y_low+3.0f);
                if(!filled.added&&!filled.reconnected)throw std::runtime_error("no cut floor or wall notch under the selection to open");
                result.blocked=filled.added;result.split=filled.reconnected;
            } else {
                result=cut_footprint(nav,footprint,y_low-3.0f,y_low+3.0f);
                if(!result.blocked)throw std::runtime_error("no walkable floor under the selection (it may be on furniture or off the floor)");
            }
            sections.data[0]=save_nav(nav);
            if(sections.present[16]&&sections.alias[16]<0)sections.data[16]=save_flags(nav);
            if(sections.present[1]&&sections.alias[1]<0)sections.data[1]=save_border_edges(nav,sections.data[1],before);
            const auto children=children_of(nav);
            if(sections.present[7]&&sections.alias[7]<0)sections.data[7]=extend_portals(sections.data[7],children);
            if(sections.present[14]&&sections.alias[14]<0)sections.data[14]=extend_groups(sections.data[14],children);
        });
        if(ok&&open_floor)status="floor opened: "+std::to_string(result.blocked)+" triangles added, "+std::to_string(result.split)+
                     " cut triangles reconnected (N shows the floor; Ctrl+Z undoes)";
        else if(ok)status="floor cut: "+std::to_string(result.blocked)+" triangles blocked, "+std::to_string(result.split)+
                     " edges split (N shows the floor; Ctrl+Z undoes)";
    };
    // ------------------------------------------------------------ object library (L)
    Ui ui(gl);
    bool library_open=false,library_built=false,library_search=false;
    std::vector<LibraryObject> library;
    std::vector<int> library_view;             // indices into `library` matching the filter
    std::string library_filter;
    int library_selected=-1,library_scroll=0,library_last_row=-1;
    double library_last_click=-1;
    const auto refresh_library_view=[&] {
        library_view.clear();
        std::string f=library_filter;for(auto& c:f)c=char(std::toupper(static_cast<unsigned char>(c)));
        for(int i=0;i<int(library.size());++i)if(f.empty()||library[i].label.find(f)!=std::string::npos)library_view.push_back(i);
        library_scroll=0;
    };
    const auto ensure_library=[&] {
        if(library_built)return;
        glfwSetWindowTitle(window,"HG World Editor | building the object library from all rooms...");
        // Always from the disc copies, so record offsets stay valid whatever is being edited.
        library=build_library(rooms,[&](int id){return volume.read_file(room_path(id));});
        // Characters and creatures follow the room objects (room object numbers stay stable).
        for(auto& c:build_character_library(volume.entries(),[&](const std::string& path){return volume.read_file(path);})) {
            char number[16];std::snprintf(number,sizeof number,"#%-4zu ",library.size());
            c.label=number+c.label;
            library.push_back(std::move(c));
        }
        library_built=true;refresh_library_view();
    };
    struct Preview {GLuint vao=0,vbo=0;std::vector<GLuint> textures;std::vector<Batch> batches;float centre[3]{},radius=1;int object=-1;};
    Preview preview;
    const auto release_preview=[&] {
        if(preview.vbo)gl.DeleteBuffers(1,&preview.vbo);
        if(preview.vao)gl.DeleteVertexArrays(1,&preview.vao);
        if(!preview.textures.empty())glDeleteTextures(GLsizei(preview.textures.size()),preview.textures.data());
        preview=Preview{};
    };
    const auto load_preview=[&](int object) {
        release_preview();
        if(object<0||object>=int(library.size()))return;
        try {
            const auto& entry=library[object];
            std::vector<Mesh> parts;
            std::vector<Texture> textures;
            if(!entry.model.empty()) {
                parts=character_meshes(parse_character(volume.read_file(entry.model)));
                textures=parse_textures(volume.read_file(entry.model_texture));
            } else {
                const RoomPac pac(volume.read_file(room_path(entry.room)));
                const auto geometry=pac.section(3);
                for(int b=0;b<geometry_blocks;++b)for(auto& m:parse_meshes(geometry,b))
                    if(std::find(entry.records.begin(),entry.records.end(),m.offset)!=entry.records.end())parts.push_back(std::move(m));
                textures=parse_textures(pac.section(9));
            }
            std::vector<Vertex> all;float lo[3]={1e30f,1e30f,1e30f},hi[3]={-1e30f,-1e30f,-1e30f};
            for(std::uint32_t t=0;t<=textures.size();++t) {
                Batch batch;batch.first=GLint(all.size());
                for(const auto& m:parts)if(t<textures.size()?m.texture==t:m.texture>=textures.size())all.insert(all.end(),m.triangles.begin(),m.triangles.end());
                batch.count=GLsizei(all.size())-batch.first;
                if(!batch.count)continue;
                if(t<textures.size()) {
                    GLuint name=0;glGenTextures(1,&name);glBindTexture(GL_TEXTURE_2D,name);
                    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,textures[t].width,textures[t].height,0,GL_RGBA,GL_UNSIGNED_BYTE,textures[t].rgba.data());
                    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
                    preview.textures.push_back(name);batch.texture=name;
                }
                preview.batches.push_back(batch);
            }
            for(const auto& v:all){const float q[3]={v.x,v.y,v.z};for(int k=0;k<3;++k){lo[k]=std::min(lo[k],q[k]);hi[k]=std::max(hi[k],q[k]);}}
            for(int k=0;k<3;++k)preview.centre[k]=(lo[k]+hi[k])/2;
            preview.radius=std::max(1.0f,std::sqrt((hi[0]-lo[0])*(hi[0]-lo[0])+(hi[1]-lo[1])*(hi[1]-lo[1])+(hi[2]-lo[2])*(hi[2]-lo[2]))/2);
            gl.GenVertexArrays(1,&preview.vao);gl.BindVertexArray(preview.vao);
            gl.GenBuffers(1,&preview.vbo);gl.BindBuffer(array_buffer,preview.vbo);
            gl.BufferData(array_buffer,std::ptrdiff_t(all.size()*sizeof(Vertex)),all.data(),static_draw);
            attach_layout(gl);
            preview.object=object;
        } catch(const std::exception& e){status=std::string("preview failed: ")+e.what();}
    };
    // Where a dropped object goes: the room surface under the screen centre (or 20 units ahead).
    const auto drop_point=[&](const float forward_dir[3])->Vec3 {
        const Vec3 o=v3(viewer.eye),d=v3(forward_dir);
        float best=std::numeric_limits<float>::max(),t=0;
        for(const auto& m:room.room_meshes)for(std::size_t i=0;i+2<m.triangles.size();i+=3) {
            const auto& a=m.triangles[i];const auto& b=m.triangles[i+1];const auto& c=m.triangles[i+2];
            if(ray_triangle({o,d},{a.x,a.y,a.z},{b.x,b.y,b.z},{c.x,c.y,c.z},t)&&t>1.0f&&t<best)best=t;
        }
        return add(o,scale(d,best<5000.0f?best:20.0f));
    };
    // A library entry as a pasteable clip; characters take the current room's typical vertex
    // colour so they are lit like the room's own (prelit) geometry.
    const auto library_clip=[&](int object)->ObjectClip {
        const auto& entry=library.at(object);
        if(!entry.model.empty())
            return character_clip(parse_character(volume.read_file(entry.model)),volume.read_file(entry.model_texture),
                                  room_typical_colour(RoomPac(room_bytes).section(3)));
        return copy_object(volume.read_file(room_path(entry.room)),entry.records,entry.room);
    };
    const auto place_library_object=[&](int object,const float forward_dir[3]) {
        if(object<0||object>=int(library.size())){status="pick an object in the library first";return;}
        try {
            const auto clip=library_clip(object);
            if(paste_object_at(clip,drop_point(forward_dir)))
                status="placed library object #"+std::to_string(object)+" ("+std::to_string(clip.parts.size())+
                       " meshes) - arrows/R/F move it, Del removes it, Ctrl+S saves";
        } catch(const std::exception& e){status=std::string("not placed: ")+e.what();}
    };
    for(int i=1;i<argc;++i)if(std::string(argv[i])=="--library"){library_open=true;ensure_library();}
    for(int i=1;i<argc;++i)if(std::string(argv[i])=="--lib-list"){ensure_library();for(const auto& o:library)if(!o.model.empty())std::cout<<o.label<<'\n';}
    for(int i=1;i+1<argc;++i)if(std::string(argv[i])=="--lib-select"){library_selected=std::stoi(argv[i+1]);load_preview(library_selected);
        std::cout<<"library: "<<library.size()<<" objects; #"<<library_selected<<": "<<(library_selected<int(library.size())?library[library_selected].label:std::string("-"))
                 <<" | preview batches "<<preview.batches.size()<<" centre ("<<preview.centre[0]<<", "<<preview.centre[1]<<", "<<preview.centre[2]
                 <<") radius "<<preview.radius<<(status.empty()?"":" | "+status)<<'\n';}
    // View ray through the centre of the part of the window not covered by the library panel.
    constexpr int library_panel_w=560;
    const auto drop_direction=[&](const float f[3],const float r[3],float out[3]) {
        int w,h;glfwGetWindowSize(window,&w,&h);
        const double px=library_open?(library_panel_w+w)/2.0:w/2.0;
        const float fov_drop=(viewer.camera>=0?room.cameras[viewer.camera].fov:60.0f)*3.14159265f/180.0f;
        const float th=std::tan(fov_drop/2),aspect=h?float(w)/h:1.0f,nx=float(2*px/std::max(w,1)-1);
        const Vec3 fv=v3(f),rv=v3(r),uv=cross(rv,fv);
        Vec3 d=add(fv,scale(rv,nx*th*aspect));d=scale(d,1.0f/std::sqrt(dot(d,d)));
        (void)uv;out[0]=d[0];out[1]=d[1];out[2]=d[2];
    };
    auto previous=std::chrono::steady_clock::now();
    double title_timer=0;
    while(!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        const auto now=std::chrono::steady_clock::now();
        const float dt=std::min(0.1f,std::chrono::duration<float>(now-previous).count());previous=now;
        const std::string typed=typed_input;typed_input.clear();
        const double wheel=scroll_input;scroll_input=0;
        if(library_search) {   // typing into the library filter
            bool changed=false;
            for(const char c:typed)if(c!='/'||!library_filter.empty()){library_filter.push_back(c);changed=true;}
            if(pressed(GLFW_KEY_BACKSPACE)&&!library_filter.empty()){library_filter.pop_back();changed=true;}
            if(pressed(GLFW_KEY_ENTER)||pressed(GLFW_KEY_ESCAPE))library_search=false;
            if(changed)refresh_library_view();
            for(int key=GLFW_KEY_SPACE;key<=GLFW_KEY_LAST;++key)held[key]=glfwGetKey(window,key)==GLFW_PRESS;   // swallow hotkeys
        }
        if(pressed(GLFW_KEY_L)){library_open=!library_open;if(library_open)ensure_library();}
        if(library_open&&pressed(GLFW_KEY_SLASH)){library_search=true;library_filter.clear();refresh_library_view();}
        if(pressed(GLFW_KEY_ESCAPE)){if(library_open)library_open=false;else glfwSetWindowShouldClose(window,1);}
        if(pressed(GLFW_KEY_PAGE_DOWN)||pressed(GLFW_KEY_RIGHT_BRACKET))open_room((room_index+1)%rooms.size());
        if(pressed(GLFW_KEY_PAGE_UP)||pressed(GLFW_KEY_LEFT_BRACKET))open_room((room_index+rooms.size()-1)%rooms.size());
        if(pressed(GLFW_KEY_TAB))viewer.wireframe=!viewer.wireframe;
        if(pressed(GLFW_KEY_T))viewer.textured=!viewer.textured;
        if(pressed(GLFW_KEY_X))viewer.translucent=!viewer.translucent;
        if(pressed(GLFW_KEY_N))viewer.navigation=!viewer.navigation;
        if(pressed(GLFW_KEY_G))viewer.geometry=!viewer.geometry;
        if(pressed(GLFW_KEY_O))viewer.objects=!viewer.objects;
        if(pressed(GLFW_KEY_B))viewer.brightness=viewer.brightness>1.5f?1.0f:2.0f;
        for(int n=0;n<9;++n)if(pressed(GLFW_KEY_1+n)&&n<int(room.cameras.size())){viewer.camera=n;viewer.use_camera(room.cameras[n]);}
        double x,y;glfwGetCursorPos(window,&x,&y);
        if(glfwGetMouseButton(window,GLFW_MOUSE_BUTTON_RIGHT)==GLFW_PRESS) {
            if(dragging){viewer.yaw+=float(x-last_x)*0.004f;viewer.pitch=std::clamp(viewer.pitch-float(y-last_y)*0.004f,-1.55f,1.55f);viewer.camera=-1;}
            dragging=true;
        } else dragging=false;
        last_x=x;last_y=y;
        float forward[3],right[3];
        (void)look(viewer.eye,viewer.yaw,viewer.pitch,forward,right);
        if(room.id!=selection_room){selection.clear();room.pick_lines=0;selection_room=room.id;selected_destination=-1;selected_kind=Kind::none;}
        const bool ctrl=glfwGetKey(window,GLFW_KEY_LEFT_CONTROL)==GLFW_PRESS||glfwGetKey(window,GLFW_KEY_RIGHT_CONTROL)==GLFW_PRESS;
        const bool shift_key=glfwGetKey(window,GLFW_KEY_LEFT_SHIFT)==GLFW_PRESS||glfwGetKey(window,GLFW_KEY_RIGHT_SHIFT)==GLFW_PRESS;
        if(library_open&&!library_view.empty()) {   // arrows walk the library list
            const auto at=std::find(library_view.begin(),library_view.end(),library_selected);
            int row=at==library_view.end()?-1:int(at-library_view.begin());
            if(pressed(GLFW_KEY_DOWN))row=std::min(int(library_view.size())-1,row+1);
            if(pressed(GLFW_KEY_UP))row=std::max(0,row-1);
            if(row>=0&&library_view[row]!=library_selected){library_selected=library_view[row];load_preview(library_selected);}
            if(pressed(GLFW_KEY_ENTER)){float dir[3];drop_direction(forward,right,dir);place_library_object(library_selected,dir);}
        }
        if(!library_open) {   // Move the selection: arrows along the view direction on the floor plane, R/F up/down.
            const float step=shift_key?5.0f:0.5f;
            Vec3 fwd{forward[0],0,forward[2]};const float fl=std::sqrt(fwd[0]*fwd[0]+fwd[2]*fwd[2]);
            if(fl>1e-4f)fwd=scale(fwd,1.0f/fl);
            const Vec3 rgt{right[0],0,right[2]};
            if(pressed(GLFW_KEY_UP))move_selection(scale(fwd,step));
            if(pressed(GLFW_KEY_DOWN))move_selection(scale(fwd,-step));
            if(pressed(GLFW_KEY_RIGHT))move_selection(scale(rgt,step));
            if(pressed(GLFW_KEY_LEFT))move_selection(scale(rgt,-step));
            if(pressed(GLFW_KEY_R))move_selection({0,step,0});
            if(pressed(GLFW_KEY_F))move_selection({0,-step,0});
        }
        if(ctrl&&pressed(GLFW_KEY_Z)&&!undo.empty()) {
            room_bytes=undo.back();undo.pop_back();edited[room.id]=room_bytes;
            const auto kind=selected_kind;const int index=selected_index;
            build(true);status=undo.empty()?"undone":"undone (more with Ctrl+Z)";
            if(kind!=Kind::none)show_selection(describe(scene_for(),kind,index));
        }
        if(ctrl&&pressed(GLFW_KEY_S))status=save_room(room.id,room_bytes);
        if(pressed(GLFW_KEY_INSERT)||(ctrl&&pressed(GLFW_KEY_D))) {
            const Vec3 r{right[0],0,right[2]};
            duplicate_selection(scale(r,3.0f));
        }
        if(pressed(GLFW_KEY_DELETE))remove_selection();
        if(pressed(GLFW_KEY_K)){const auto group=selected_group;cut_floor_under_selection(false);if(selected_kind==Kind::mesh)select_group(group);}
        if(pressed(GLFW_KEY_J)){const auto group=selected_group;cut_floor_under_selection(true);if(selected_kind==Kind::mesh)select_group(group);}
        if(ctrl&&pressed(GLFW_KEY_C))copy_selection();
        if(ctrl&&pressed(GLFW_KEY_V))paste_clipboard(add(v3(viewer.eye),scale(v3(forward),20.0f)));
        if(pressed(GLFW_KEY_F5)) {   // export, then start the game on the modded disc
            glfwSetWindowTitle(window,"HG World Editor | exporting and starting the game...");
            try {
                const auto launch=find_game(disc.iso().empty()?disc_path:disc.iso(),std::filesystem::path(argv[0]),game_override);
                export_disc(disc,edited,room_path,export_target);
                start_game(launch,export_target);
                status="game started on the modded disc ("+launch.exe.string()+"); your memory card is "+launch.card.string();
            } catch(const std::exception& e){status=std::string("could not play: ")+e.what();}
        }
        if(ctrl&&pressed(GLFW_KEY_E)) {
            glfwSetWindowTitle(window,"HG World Editor | exporting the modded disc image (the first export copies the whole ISO)...");
            try {status=export_disc(disc,edited,room_path,export_target);}
            catch(const std::exception& e){status=std::string("export failed: ")+e.what();}
        }
        if(!library_open&&pressed(GLFW_KEY_ENTER)&&selected_destination>=0)
            for(std::size_t i=0;i<rooms.size();++i)if(rooms[i]==selected_destination){open_room(i);break;}
        bool click=glfwGetMouseButton(window,GLFW_MOUSE_BUTTON_LEFT)==GLFW_PRESS;
        int ww,wh;glfwGetWindowSize(window,&ww,&wh);
        if(pick_request){click=true;left_held=false;x=pick_x*ww;y=pick_y*wh;}
        // Library panel layout (window pixels).
        const int panel_w=library_panel_w,list_top=96,preview_h=std::min(320,wh/2);
        const int list_bottom=wh-preview_h-36,row_h=ui.line_height();
        const int visible_rows=std::max(1,(list_bottom-list_top)/row_h);
        const bool over_panel=library_open&&x<panel_w;
        if(over_panel&&wheel!=0)
            library_scroll=std::clamp(library_scroll-int(wheel*3),0,std::max(0,int(library_view.size())-visible_rows));
        if(over_panel&&click&&!left_held&&y>=list_top&&y<list_bottom) {
            const int row=library_scroll+int((y-list_top)/row_h);
            if(row<int(library_view.size())) {
                const double t=glfwGetTime();
                if(library_view[row]!=library_selected){library_selected=library_view[row];load_preview(library_selected);}
                else if(row==library_last_row&&t-library_last_click<0.4){float dir[3];drop_direction(forward,right,dir);place_library_object(library_selected,dir);}
                library_last_row=row;library_last_click=t;
            }
        }
        if(click&&!left_held&&!over_panel&&(viewer.objects||viewer.navigation)) {
            const float fov_pick=(viewer.camera>=0?room.cameras[viewer.camera].fov:60.0f)*3.14159265f/180.0f;
            const float th=std::tan(fov_pick/2),aspect=wh?float(ww)/wh:1.0f;
            const float nx=float(2*x/std::max(ww,1)-1),ny=float(1-2*y/std::max(wh,1));
            const Vec3 f=v3(forward),r=v3(right),u=cross(r,f);
            Vec3 d=add(f,add(scale(r,nx*th*aspect),scale(u,ny*th)));
            d=scale(d,1.0f/std::sqrt(dot(d,d)));
            const auto chosen=pick({v3(viewer.eye),d},scene_for());
            if(shift_key&&chosen.kind==Kind::mesh&&selected_kind==Kind::mesh) {   // Shift+click: add/remove from the object
                auto group=selected_group;
                const auto at=std::find(group.begin(),group.end(),chosen.index);
                if(at==group.end())group.push_back(chosen.index);else group.erase(at);
                select_group(group);
            } else show_selection(chosen);
            if(pick_request)std::cout<<"pick: "<<selection<<'\n';
        }
        pick_request=false;
        left_held=click;
        if(script_pending) {   // --move "light|camera|door|shape INDEX DX DY DZ" (testing)
            script_pending=false;
            for(const auto& line:move_script) {
                std::istringstream in(line);std::string kind;int index=0;Vec3 d{};
                std::string verb="move";
                if(line.rfind("copyfrom ",0)==0) {
                    std::string ignored;int source=0,index=0;in>>ignored>>std::hex>>source>>ignored>>std::dec>>index;
                    const auto bytes=volume.read_file(room_path(source));
                    const RoomPac source_pac(bytes);const auto geometry=source_pac.section(3);
                    std::vector<std::size_t> records;
                    for(int b=0;b<geometry_blocks;++b)for(const auto& m:parse_meshes(geometry,b))records.push_back(m.offset);
                    clipboard=copy_object(bytes,{std::uint32_t(records.at(index))},source);
                    std::cout<<"copied mesh "<<index<<" of room "<<std::hex<<source<<std::dec<<" ("<<clipboard.parts[0].record.size()
                             <<" bytes, texture "<<clipboard.parts[0].texture_data.size()<<" bytes)\n";
                    continue;
                }
                if(line.rfind("paste ",0)==0) {
                    std::string ignored;Vec3 at{};in>>ignored>>at[0]>>at[1]>>at[2];
                    paste_clipboard(at);std::cout<<"pasted: "<<selection<<" | "<<status<<'\n';
                    continue;
                }
                if(line.rfind("place ",0)==0) {
                    std::string ignored;int object=0;Vec3 at{};in>>ignored>>object;
                    ensure_library();
                    if(in>>at[0]>>at[1]>>at[2]) {   // explicit floor-centre position (testing)
                        try {
                            paste_object_at(library_clip(object),at);
                        } catch(const std::exception& e){status=std::string("not placed: ")+e.what();}
                    } else {float dir[3];drop_direction(forward,right,dir);place_library_object(object,dir);}
                    std::cout<<"place: "<<selection<<" | "<<status<<'\n';
                    continue;
                }
                if(line=="play"){   // same as F5 (testing)
                    try {
                        const auto launch=find_game(disc.iso().empty()?disc_path:disc.iso(),std::filesystem::path(argv[0]),game_override);
                        std::cout<<export_disc(disc,edited,room_path,export_target)<<'\n';
                        start_game(launch,export_target);std::cout<<"play: started "<<launch.exe.string()<<'\n';
                    } catch(const std::exception& e){std::cout<<"play failed: "<<e.what()<<'\n';}
                    continue;
                }
                if(line.rfind("group ",0)==0) {   // select several meshes as one object (testing)
                    std::string ignored;in>>ignored;std::vector<int> group;int index;
                    while(in>>index)group.push_back(index);
                    select_group(group);std::cout<<"group: "<<selection<<'\n';continue;
                }
                if(line=="openfloor"){cut_floor_under_selection(true);std::cout<<"openfloor | "<<status<<'\n';continue;}
                if(line=="cutfloor"){cut_floor_under_selection(false);std::cout<<"cutfloor | "<<status<<'\n';continue;}
                if(line.rfind("dup ",0)==0||line.rfind("remove ",0)==0)in>>verb;
                in>>kind>>index;
                if(verb=="move")in>>d[0]>>d[1]>>d[2];
                const Kind k=kind=="light"?Kind::light:kind=="camera"?Kind::camera:kind=="door"?Kind::door:kind=="shape"?Kind::shape:kind=="mesh"?Kind::mesh:Kind::none;
                if(k==Kind::none||(verb=="move"&&!in))throw std::runtime_error("bad --move: "+line);
                show_selection(describe(scene_for(),k,index));
                if(verb=="dup"){duplicate_selection({3,0,0});std::cout<<"duplicated: "<<selection<<" | "<<status<<'\n';}
                else if(verb=="remove"){remove_selection();std::cout<<"removed | "<<status<<'\n';}
                else {move_selection(d);std::cout<<"moved: "<<selection<<'\n';}
            }
            if(save_after)std::cout<<save_room(room.id,room_bytes)<<'\n';
            if(export_after) {
                try {std::cout<<export_disc(disc,edited,room_path,export_target)<<'\n';}
                catch(const std::exception& e){std::cout<<"export failed: "<<e.what()<<'\n';}
            }
        }
        const float speed=(library_search?0.0f:1.0f)*(glfwGetKey(window,GLFW_KEY_LEFT_SHIFT)==GLFW_PRESS?120.0f:35.0f)*dt;
        const auto move=[&](const float d[3],float s){for(int i=0;i<3;++i)viewer.eye[i]+=d[i]*s;viewer.camera=-1;};
        if(glfwGetKey(window,GLFW_KEY_W)==GLFW_PRESS)move(forward,speed);
        if(glfwGetKey(window,GLFW_KEY_S)==GLFW_PRESS)move(forward,-speed);
        if(glfwGetKey(window,GLFW_KEY_D)==GLFW_PRESS)move(right,speed);
        if(glfwGetKey(window,GLFW_KEY_A)==GLFW_PRESS)move(right,-speed);
        const float up[3]={0,1,0};
        if(glfwGetKey(window,GLFW_KEY_E)==GLFW_PRESS||glfwGetKey(window,GLFW_KEY_SPACE)==GLFW_PRESS)move(up,speed);
        if(glfwGetKey(window,GLFW_KEY_Q)==GLFW_PRESS||glfwGetKey(window,GLFW_KEY_LEFT_CONTROL)==GLFW_PRESS)move(up,-speed);

        int width,height;glfwGetFramebufferSize(window,&width,&height);
        glViewport(0,0,width,height);
        glClearColor(0.08f,0.08f,0.1f,1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
        glEnable(GL_DEPTH_TEST);
        glPolygonMode(GL_FRONT_AND_BACK,viewer.wireframe?GL_LINE:GL_FILL);
        const float fov=(viewer.camera>=0?room.cameras[viewer.camera].fov:60.0f)*3.14159265f/180.0f;
        const auto vp=multiply(perspective(fov,height?float(width)/height:1.0f,0.5f,4000.0f),look(viewer.eye,viewer.yaw,viewer.pitch,forward,right));
        gl.UseProgram(program);
        gl.UniformMatrix4fv(u_vp,1,GL_FALSE,vp.m);gl.Uniform1f(u_bright,viewer.brightness);gl.Uniform1i(u_image,0);
        gl.BindVertexArray(room.vao);gl.ActiveTexture(texture0);
        for(const auto& b:room.batches) {
            if(!viewer.geometry||(b.blended&&!viewer.translucent))continue;
            if(b.blended){glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);glDepthMask(GL_FALSE);}
            else {glDisable(GL_BLEND);glDepthMask(GL_TRUE);}
            gl.Uniform1i(u_blended,b.blended?1:0);
            gl.Uniform1i(u_textured,viewer.textured&&b.texture?1:0);
            glBindTexture(GL_TEXTURE_2D,b.texture);
            glDrawArrays(GL_TRIANGLES,b.first,b.count);
        }
        if(viewer.navigation&&room.nav_vao) {
            glPolygonMode(GL_FRONT_AND_BACK,GL_FILL);
            glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);glDepthMask(GL_FALSE);
            gl.Uniform1i(u_textured,0);gl.Uniform1i(u_blended,1);gl.Uniform1f(u_bright,1.33f);
            gl.BindVertexArray(room.nav_vao);
            glDrawArrays(GL_TRIANGLES,0,room.nav_fill);
            glDisable(GL_DEPTH_TEST);glLineWidth(2.0f);
            glDrawArrays(GL_LINES,room.nav_fill,room.nav_lines);
            glEnable(GL_DEPTH_TEST);
        }
        if(viewer.objects&&room.obj_vao) {
            glPolygonMode(GL_FRONT_AND_BACK,GL_FILL);
            glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);glDepthMask(GL_FALSE);
            gl.Uniform1i(u_textured,0);gl.Uniform1i(u_blended,1);gl.Uniform1f(u_bright,1.33f);
            gl.BindVertexArray(room.obj_vao);
            glDrawArrays(GL_TRIANGLES,0,room.obj_fill);
            glDisable(GL_DEPTH_TEST);glLineWidth(1.5f);
            glDrawArrays(GL_LINES,room.obj_fill,room.obj_lines);
            glEnable(GL_DEPTH_TEST);
        }
        if(room.pick_lines&&(viewer.objects||viewer.navigation)) {
            glDisable(GL_DEPTH_TEST);glLineWidth(3.0f);
            gl.Uniform1i(u_textured,0);gl.Uniform1i(u_blended,1);gl.Uniform1f(u_bright,2.0f);
            gl.BindVertexArray(room.pick_vao);
            glDrawArrays(GL_LINES,0,room.pick_lines);
            glEnable(GL_DEPTH_TEST);glLineWidth(1.0f);
        }
        if(library_open) {
            // Panel, list and the rotating preview of the selected object.
            const std::uint32_t text_c=ui_rgba(225,225,230),dim=ui_rgba(150,150,160),accent=ui_rgba(120,200,255);
            ui.rect(0,0,float(panel_w),float(wh),ui_rgba(14,14,20,232));
            ui.text(12,10,"OBJECT LIBRARY  "+std::to_string(library.size())+" OBJECTS",accent);
            ui.text(12,32,library_search?("SEARCH: "+library_filter+"_"):(library_filter.empty()?"/ TO SEARCH (E.G. ROOM 021, CHAR, FIONA)":"FILTER: "+library_filter+"  (/ TO CHANGE)"),
                    library_search?ui_rgba(255,230,120):dim,float(panel_w-24));
            ui.text(12,54,"CLICK: SELECT   ENTER/DOUBLE-CLICK: DROP",dim,float(panel_w-24));
            ui.text(12,72,"AT THE CROSSHAIR.  ARROWS: LIST  L/ESC: CLOSE",dim,float(panel_w-24));
            for(int r=0;r<visible_rows&&library_scroll+r<int(library_view.size());++r) {
                const int index=library_view[library_scroll+r];
                const float ry=float(list_top+r*row_h);
                const bool hover=x<panel_w&&y>=ry&&y<ry+row_h;
                if(index==library_selected)ui.rect(6,ry-2,float(panel_w-12),float(row_h),ui_rgba(40,80,130,255));
                else if(hover)ui.rect(6,ry-2,float(panel_w-12),float(row_h),ui_rgba(40,40,55,255));
                ui.text(12,ry+2,library[index].label,text_c,float(panel_w-24));
            }
            if(library_view.size()>std::size_t(visible_rows)) {   // scroll bar
                const float track=float(list_bottom-list_top),h=std::max(20.0f,track*visible_rows/library_view.size());
                const float top=list_top+(track-h)*library_scroll/std::max(1,int(library_view.size())-visible_rows);
                ui.rect(float(panel_w-6),top,4,h,ui_rgba(90,90,110));
            }
            const int py=wh-preview_h-6;
            ui.rect(10,float(py),float(panel_w-20),float(preview_h-4),ui_rgba(28,28,36,255));
            if(library_selected>=0&&library_selected<int(library.size())) {
                std::ostringstream where;where<<library[library_selected].parts<<" PARTS, "<<std::hex<<std::uppercase;
                const auto& model=library[library_selected].model;
                if(!model.empty())where<<"CHARACTER MODEL "<<model.substr(0,model.find(';'))<<" (STATIC POSE)";
                else where<<"IN ROOM";
                const auto& rs=library[library_selected].rooms;if(!rs.empty())where<<(rs.size()>1?"S ":" ");
                for(std::size_t i=0;i<rs.size()&&i<12;++i)where<<(i?", ":"")<<rs[i];
                if(rs.size()>12)where<<"...";
                ui.text(12,float(py-24),where.str(),accent,float(panel_w-24));
            }
            ui.draw(ww,wh);
            if(preview.vao) {   // rotating 3D preview inside the box
                const int pw=panel_w-24,ph=preview_h-8,px=12,py_gl=height-(py+2+ph);
                glViewport(px,py_gl,pw,ph);glEnable(GL_SCISSOR_TEST);glScissor(px,py_gl,pw,ph);
                glDepthMask(GL_TRUE);glClear(GL_DEPTH_BUFFER_BIT);glEnable(GL_DEPTH_TEST);glDisable(GL_BLEND);
                const float spin=float(glfwGetTime())*0.6f;
                const float eye[3]={preview.centre[0]+std::sin(spin)*preview.radius*2.6f,preview.centre[1]+preview.radius*1.1f,
                                    preview.centre[2]-std::cos(spin)*preview.radius*2.6f};
                const float dx=preview.centre[0]-eye[0],dy=preview.centre[1]-eye[1],dz=preview.centre[2]-eye[2];
                float pf[3],pr[3];
                const auto pview=look(eye,std::atan2(dx,-dz),std::atan2(dy,std::sqrt(dx*dx+dz*dz)),pf,pr);
                const auto pvp=multiply(perspective(0.8f,float(pw)/ph,0.1f,preview.radius*20),pview);
                gl.UseProgram(program);gl.UniformMatrix4fv(u_vp,1,GL_FALSE,pvp.m);gl.Uniform1f(u_bright,1.5f);
                gl.Uniform1i(u_blended,0);gl.BindVertexArray(preview.vao);
                for(const auto& b:preview.batches){gl.Uniform1i(u_textured,b.texture?1:0);glBindTexture(GL_TEXTURE_2D,b.texture);glDrawArrays(GL_TRIANGLES,b.first,b.count);}
                glDisable(GL_SCISSOR_TEST);glViewport(0,0,width,height);
            }
            // Crosshair where Enter drops the object.
            const float cx=float((panel_w+ww)/2);
            ui.rect(cx-8,float(wh/2),16,2,ui_rgba(255,255,255,200));ui.rect(cx,float(wh/2-8),2,16,ui_rgba(255,255,255,200));
        }
        if(!status.empty()||!selection.empty()) {   // status bar
            const float by=float(wh-22);
            ui.rect(library_open?float(panel_w):0,by-4,float(ww),26,ui_rgba(10,10,14,200));
            ui.text((library_open?panel_w:0)+10.0f,by,status.empty()?selection:status,ui_rgba(230,230,235),float(ww-(library_open?panel_w:0)-20));
        }
        ui.draw(ww,wh);
        if(!screenshot.empty()) {
            std::vector<std::uint8_t> pixels(std::size_t(width)*height*3);
            glPixelStorei(GL_PACK_ALIGNMENT,1);
            glReadPixels(0,0,width,height,GL_RGB,GL_UNSIGNED_BYTE,pixels.data());
            std::ofstream out(screenshot,std::ios::binary);
            out<<"P6\n"<<width<<' '<<height<<"\n255\n";
            for(int row=height-1;row>=0;--row)out.write(reinterpret_cast<const char*>(&pixels[std::size_t(row)*width*3]),width*3);
            if(!out)throw std::runtime_error("cannot write screenshot");
            if(!load_error.empty())throw std::runtime_error(load_error);
            break;
        }
        glDepthMask(GL_TRUE);
        glfwSwapBuffers(window);
        if((title_timer-=dt)<=0) {
            title_timer=0.25;
            std::ostringstream title;
            title<<"HG World Editor | room "<<std::hex<<std::uppercase<<room.id<<std::dec<<" ("<<room_index+1<<'/'<<rooms.size()<<")";
            if(!load_error.empty())title<<" | cannot show: "<<load_error;
            else title<<" | "<<room.meshes<<" meshes, "<<room.triangles<<" tris, "<<room.textures.size()<<" textures";
            if(!status.empty())title<<" | "<<status;
            if(!selection.empty()&&(viewer.objects||viewer.navigation))title<<" | "<<selection;
            if(viewer.objects) {
                title<<" | "<<room.shapes<<" shapes, "<<room.lights<<" lights | exits:"<<std::hex<<std::uppercase;
                bool any=false;
                for(int door=0;door<8;++door)if(const auto* other=door_destination(door_links,room.id,door)) {
                    title<<' '<<door<<"->"<<other->room<<(door<int(room.portals)?"":"*");any=true;
                }
                title<<std::dec<<(any?" (* = no door model)":" none listed");
            }
            if(viewer.navigation)title<<" | nav "<<room.nav_triangles<<" tris, "<<room.nav_edges<<" border edges";
            title<<" | camera "<<(viewer.camera>=0?std::to_string(viewer.camera+1):std::string("free"))<<'/'<<room.cameras.size()
                 <<" | PgUp/PgDn room, 1-9 camera, RMB look, WASD/QE move, Tab wire, T tex, X translucent, N navigation, O objects, LMB pick, Enter follow door, arrows/R/F move selection, L library, Ins/Del add/remove, Ctrl+C/V copy/paste, Shift+click group, K cut / J open floor under selection, Ctrl+S save, Ctrl+E export disc, F5 play modded, G geometry, B bright";
            glfwSetWindowTitle(window,title.str().c_str());
        }
    }
    room.release(gl);
    glfwTerminate();
    return 0;
} catch(const std::exception& e) {
    std::cerr<<"World editor: "<<e.what()<<'\n';
#ifdef _WIN32
    if(interactive)MessageBoxA(nullptr,e.what(),"HG World Editor",MB_ICONERROR);
#endif
    return 1;
}
