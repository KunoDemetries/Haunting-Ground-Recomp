#pragma once
// Room data formats. The evidence for each layout is in docs/WORLD_EDITOR.md;
// editor/hgworld (Python) is the reference parser these mirror.
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace hgw {

inline std::uint32_t rd32(const std::vector<std::uint8_t>& d,std::size_t o) {
    if(o+4>d.size())throw std::runtime_error("read past end of data");
    return d[o]|d[o+1]<<8|d[o+2]<<16|std::uint32_t(d[o+3])<<24;
}
inline void wrf(std::vector<std::uint8_t>& d,std::size_t o,float value) {
    if(o+4>d.size())throw std::runtime_error("write past end of data");
    std::uint32_t bits;std::memcpy(&bits,&value,4);
    for(int i=0;i<4;++i)d[o+i]=std::uint8_t(bits>>(8*i));
}
inline float rdf(const std::vector<std::uint8_t>& d,std::size_t o) {
    const auto bits=rd32(d,o);float value;std::memcpy(&value,&bits,4);return value;
}

constexpr int room_limit=0x110;   // loader 0x120720 rejects ids >= 0x110
constexpr int room_slots=17;

inline std::string room_path(int room) {
    char text[40];
    std::snprintf(text,sizeof text,"/ST_%03X/ST_%03X.PAC;1",room&~7,room);
    return text;
}

// Room PAC: 0x800-byte table of section offsets (0 = absent).
struct RoomPac {
    std::vector<std::uint8_t> data;
    std::array<std::uint32_t,room_slots> offset{},size{};
    explicit RoomPac(std::vector<std::uint8_t> bytes):data(std::move(bytes)) {
        if(data.size()<0x800)throw std::runtime_error("room PAC shorter than its header");
        for(int slot=0;slot<room_slots;++slot)offset[slot]=rd32(data,slot*4u);
        for(std::size_t i=room_slots*4;i<0x800;i+=4)
            if(rd32(data,i))throw std::runtime_error("room PAC uses an unknown slot");
        for(int slot=0;slot<room_slots;++slot) {
            const auto o=offset[slot];
            if(!o)continue;
            if(o%0x20||o<0x800||o>data.size())throw std::runtime_error("room PAC section offset out of range");
            std::uint32_t end=std::uint32_t(data.size());
            for(const auto other:offset)if(other>o&&other<end)end=other;
            size[slot]=end-o;
        }
    }
    std::vector<std::uint8_t> section(int slot) const {
        if(!offset[slot])return {};
        return {data.begin()+offset[slot],data.begin()+offset[slot]+size[slot]};
    }
};

// Texture bank (.TEX files and room slot 9).
struct Texture {int width=0,height=0;std::vector<std::uint32_t> rgba;};

inline std::vector<Texture> parse_textures(const std::vector<std::uint8_t>& d) {
    std::vector<Texture> result;
    if(d.empty())return result;
    const auto count=rd32(d,0);
    if(!count||count>4096||16+count*16>d.size())throw std::runtime_error("implausible texture count");
    for(std::uint32_t i=0;i<count;++i) {
        const std::size_t entry=16+i*16;
        const auto psm=rd32(d,entry);
        const int width=d[entry+4]|d[entry+5]<<8,height=d[entry+6]|d[entry+7]<<8;
        const auto start=entry+rd32(d,entry+12);
        if(psm!=0x13&&psm!=0x14)throw std::runtime_error("unsupported texture pixel format");
        const std::size_t index_bytes=std::size_t(width)*height*(psm==0x13?8:4)/8;
        if(rd32(d,entry+8)!=(0x40u<<16|index_bytes/16))throw std::runtime_error("texture size word mismatch");
        if(start+index_bytes+1024>d.size())throw std::runtime_error("texture data past end of bank");
        std::array<std::uint32_t,256> palette{};
        for(unsigned n=0;n<256;++n) {
            // GS CSM1 storage: entries 8..15 and 16..23 of each 32-block are swapped.
            const unsigned stored=psm==0x13?((n&~0x18u)|((n&8u)<<1)|((n&16u)>>1)):n;
            const auto* p=&d[start+index_bytes+stored*4];
            const unsigned alpha=std::min(255u,p[3]*255u/0x80u);
            palette[n]=p[0]|p[1]<<8|p[2]<<16|alpha<<24;
        }
        Texture t;t.width=width;t.height=height;t.rgba.resize(std::size_t(width)*height);
        for(std::size_t n=0;n<t.rgba.size();++n) {
            const unsigned index=psm==0x13?d[start+n]:(d[start+n/2]>>((n&1)*4))&15;
            t.rgba[n]=palette[index];
        }
        result.push_back(std::move(t));
    }
    return result;
}

// Room geometry (slot 3, block 0): mesh records with triangle strips.
struct Vertex {float x,y,z,u,v;std::uint8_t r,g,b,a;};
// `offset` = record start within its slot; the matrix translation is at offset+16+{12,28,44}.
struct Mesh {std::uint32_t offset=0,texture=0,flags=0;std::vector<Vertex> triangles;float translation[3]{};};

// Slot 3 starts with eight block offsets; every used block (0, 1, 2, 4, 5 on the
// disc) holds the same mesh records. Blocks 1, 2 and 5 carry translucent vertex alpha.
constexpr int geometry_blocks=8;

// Reads mesh records from `start` up to a 16-byte 0xFF terminator before `end`; returns the
// offset just past the terminator.
inline std::size_t parse_mesh_chain(const std::vector<std::uint8_t>& d,std::size_t start,std::size_t end,
                                    std::vector<Mesh>& meshes) {
    const auto pad=[](std::size_t n){return (n+15)/16*16;};
    std::size_t offset=start;
    for(;;) {
        if(offset+16>end)throw std::runtime_error("room geometry has no terminator");
        if(rd32(d,offset)==0xffffffffu&&rd32(d,offset+4)==0xffffffffu)break;
        const auto count=rd32(d,offset),texture=rd32(d,offset+4),flags=rd32(d,offset+8);
        if(!count||count>=0x10000)throw std::runtime_error("implausible mesh record header");
        float m[16];
        for(int i=0;i<16;++i)m[i]=rdf(d,offset+16+i*4u);
        const std::size_t uv=offset+80,colour=uv+pad(count*8),position=colour+pad(count*4);
        if(position+count*16>end)throw std::runtime_error("mesh record runs past its block");
        std::vector<Vertex> vertices(count);
        std::vector<std::uint32_t> strip(count);
        for(std::uint32_t i=0;i<count;++i) {
            const float x=rdf(d,position+i*16),y=rdf(d,position+i*16+4),z=rdf(d,position+i*16+8);
            strip[i]=rd32(d,position+i*16+12);
            auto& v=vertices[i];
            v.x=m[0]*x+m[1]*y+m[2]*z+m[3];v.y=m[4]*x+m[5]*y+m[6]*z+m[7];v.z=m[8]*x+m[9]*y+m[10]*z+m[11];
            v.u=rdf(d,uv+i*8);v.v=rdf(d,uv+i*8+4);
            v.r=d[colour+i*4];v.g=d[colour+i*4+1];v.b=d[colour+i*4+2];v.a=d[colour+i*4+3];
        }
        Mesh mesh;mesh.offset=std::uint32_t(offset);mesh.texture=texture;mesh.flags=flags;
        mesh.translation[0]=m[3];mesh.translation[1]=m[7];mesh.translation[2]=m[11];
        int run=0;
        for(std::uint32_t i=0;i<count;++i) {
            if(strip[i]==0x8000) {run=(i==0||strip[i-1]==0x7fff)?1:run+1;continue;}
            if(strip[i]!=0x7fff||run<2)throw std::runtime_error("unexpected triangle strip flag");
            ++run;
            const bool forward=run%2;
            mesh.triangles.push_back(vertices[forward?i-2:i-1]);
            mesh.triangles.push_back(vertices[forward?i-1:i-2]);
            mesh.triangles.push_back(vertices[i]);
        }
        meshes.push_back(std::move(mesh));
        offset=position+count*16;
    }
    return offset+16;
}

inline std::vector<Mesh> parse_meshes(const std::vector<std::uint8_t>& d,int block=0) {
    std::vector<Mesh> meshes;
    if(d.size()<32)return meshes;
    std::uint32_t start=rd32(d,block*4u),end=std::uint32_t(d.size());
    if(!start)return meshes;
    for(int b=0;b<geometry_blocks;++b) {const auto o=rd32(d,b*4u);if(o>start&&o<end)end=o;}
    parse_mesh_chain(d,start,end,meshes);
    return meshes;
}

// Slot 8: door models, one terminator-closed mesh chain per slot-7 portal (131 of 132
// rooms match; room 0x2D has one chain and no portal). Starts at the first table offset;
// later table entries may point at the previous chain's terminator, so chains are walked.
inline std::vector<std::vector<Mesh>> parse_door_models(const std::vector<std::uint8_t>& d) {
    std::vector<std::vector<Mesh>> doors;
    if(d.size()<32)return doors;
    std::size_t offset=rd32(d,0);
    const auto terminator=[&](std::size_t o){return o+16<=d.size()&&rd32(d,o)==0xffffffffu&&rd32(d,o+4)==0xffffffffu
                                                   &&rd32(d,o+8)==0xffffffffu&&rd32(d,o+12)==0xffffffffu;};
    while(offset+16<=d.size()) {
        bool empty=true;for(std::size_t i=0;i<16;++i)if(d[offset+i])empty=false;
        if(empty||terminator(offset))break;
        doors.emplace_back();
        offset=parse_mesh_chain(d,offset,d.size(),doors.back());
    }
    return doors;
}

// Fixed cameras (slot 5): pairs of {eye xyz, fov} {target xyz, 0}, closed by 0xFFFFFFFF.
struct Camera {float eye[3],target[3],fov;std::size_t offset=0;};

inline std::vector<Camera> parse_cameras(const std::vector<std::uint8_t>& d) {
    std::vector<Camera> cameras;
    for(std::size_t o=0;o+32<=d.size();o+=32) {
        if(rd32(d,o)==0xffffffffu)break;
        Camera c{{rdf(d,o),rdf(d,o+4),rdf(d,o+8)},{rdf(d,o+16),rdf(d,o+20),rdf(d,o+24)},rdf(d,o+12),o};
        cameras.push_back(c);
    }
    return cameras;
}
}

namespace hgw {
// Navigation mesh (slot 0) with per-triangle flag bytes (slot 16) and border-edge list
// (slot 1). Evidence: setup routine 0x17CC00 reads slot 0 as u32 count + 0x50-byte
// triangles and one slot-16 byte per triangle (bit0 -> runtime flag 0x4000, bit1 ->
// 0x80000 in word +0x3C); neighbour distances equal centroid distances exactly; 14,717 of
// 14,728 slot-1 records name an edge whose neighbour is -1.
struct NavTriangle {float v[3][3];std::int32_t neighbour[3];float cost[3];std::uint32_t attribute;std::uint8_t flags;};
struct NavEdge {std::uint32_t triangle,edge,edge_again,kind;};
struct NavMesh {std::vector<NavTriangle> triangles;std::vector<NavEdge> edges;};

inline NavMesh parse_navigation(const std::vector<std::uint8_t>& s0,const std::vector<std::uint8_t>& s1,
                                const std::vector<std::uint8_t>& s16) {
    NavMesh nav;
    if(s0.size()>=16) {
        const auto count=rd32(s0,0);
        if(16+std::size_t(count)*0x50>s0.size())throw std::runtime_error("navigation mesh runs past its slot");
        for(std::uint32_t i=0;i<count;++i) {
            const std::size_t o=16+std::size_t(i)*0x50;
            NavTriangle t{};
            for(int k=0;k<3;++k)for(int j=0;j<3;++j)t.v[k][j]=rdf(s0,o+k*16+j*4);
            for(int k=0;k<3;++k){t.neighbour[k]=std::int32_t(rd32(s0,o+48+k*4));t.cost[k]=rdf(s0,o+64+k*4);}
            t.attribute=rd32(s0,o+76);
            // Some rooms' slot 16 is shorter than the triangle count (the game reads past it).
            t.flags=i<s16.size()?s16[i]:0;
            nav.triangles.push_back(t);
        }
    }
    if(s1.size()>=16) {
        const auto count=rd32(s1,0);
        if(16+std::size_t(count)*16>s1.size())throw std::runtime_error("navigation edge list runs past its slot");
        for(std::uint32_t i=0;i<count;++i) {
            const std::size_t o=16+std::size_t(i)*16;
            nav.edges.push_back({rd32(s1,o),rd32(s1,o+4),rd32(s1,o+8),rd32(s1,o+12)});
        }
    }
    return nav;
}
}

namespace hgw {
// Slot 2: offset table (u32, in 4-byte units; 0 = absent entry) ending where the lowest
// record starts. Record = {u32 kind, u32, f32 value, u32} + 2..5 points {x,y,z,1};
// kind 1 = closed polygon, kind 0 = 2-point line (all 163 rooms). Purpose unproven;
// `height` holds the f32 value (8, 12, 58, 78, 0, -2), which the overlay extrudes by.
struct Shape {int index=0;std::uint32_t kind=0;float height=0;std::vector<std::array<float,3>> points;std::size_t offset=0;};

inline std::vector<Shape> parse_shapes(const std::vector<std::uint8_t>& d) {
    std::vector<Shape> shapes;
    if(d.size()<4)return shapes;
    std::size_t end=d.size();
    std::vector<std::size_t> table;
    for(std::size_t i=0;i*4<end;++i) {
        const std::size_t o=std::size_t(rd32(d,i*4))*4;
        table.push_back(o);
        if(o)end=std::min(end,o);
    }
    std::vector<std::size_t> starts;
    for(const auto o:table)if(o)starts.push_back(o);
    for(std::size_t i=0;i<table.size();++i) {
        const auto o=table[i];
        if(!o)continue;
        std::size_t stop=d.size();
        for(const auto s:starts)if(s>o&&s<stop)stop=s;
        if(o%16||stop<o+16||(stop-o)%16)throw std::runtime_error("slot 2 record misaligned");
        Shape shape;shape.index=int(i);shape.kind=rd32(d,o);shape.height=rdf(d,o+8);shape.offset=o;
        for(std::size_t p=o+16;p<stop;p+=16)shape.points.push_back({rdf(d,p),rdf(d,p+4),rdf(d,p+8)});
        shapes.push_back(std::move(shape));
    }
    return shapes;
}

// Slot 4: {u32 count, ambient rgb floats} then 0x30-byte lights
// {pos xyz,1}{rgb, intensity}{range, 1/range, range^2, 0}. Purpose "lights" probable.
struct Light {float position[3],colour[3],intensity,range;std::size_t offset=0;};
struct Lighting {float ambient[3]{};std::vector<Light> lights;};

inline Lighting parse_lights(const std::vector<std::uint8_t>& d) {
    Lighting l;
    if(d.size()<16)return l;
    const auto count=rd32(d,0);
    if(16+std::size_t(count)*0x30>d.size())throw std::runtime_error("light list runs past its slot");
    for(int i=0;i<3;++i)l.ambient[i]=rdf(d,4+i*4);
    for(std::uint32_t i=0;i<count;++i) {
        const std::size_t o=16+std::size_t(i)*0x30;
        l.lights.push_back({{rdf(d,o),rdf(d,o+4),rdf(d,o+8)},{rdf(d,o+16),rdf(d,o+20),rdf(d,o+24)},rdf(d,o+28),rdf(d,o+32),o});
    }
    return l;
}

// Slot 7: byte-offset table ending at the lowest record. Record = {u32 nav triangle,
// point A xyz, point B xyz, u32, f32 angle (cardinal), u32, u32 count, count nav
// triangle indices}. Across 269 records the id is in its own list and B lies on a
// listed triangle. Probable doors/exits (A door, B stand point); unproven.
struct Portal {std::uint32_t triangle=0;float a[3]{},b[3]{},angle=0;std::vector<std::uint32_t> area;std::size_t offset=0;};

inline std::vector<Portal> parse_portals(const std::vector<std::uint8_t>& d) {
    std::vector<Portal> portals;
    if(d.size()<4)return portals;
    std::size_t end=d.size();
    std::vector<std::size_t> offsets;
    for(std::size_t i=0;i*4<end;++i) {
        const std::size_t o=rd32(d,i*4);
        if(o){offsets.push_back(o);end=std::min(end,o);}
    }
    for(const auto o:offsets) {
        Portal p;p.triangle=rd32(d,o);p.offset=o;
        for(int k=0;k<3;++k){p.a[k]=rdf(d,o+4+k*4);p.b[k]=rdf(d,o+16+k*4);}
        p.angle=rdf(d,o+32);
        const auto count=rd32(d,o+40);
        if(o+44+std::size_t(count)*4>d.size())throw std::runtime_error("slot 7 record runs past its slot");
        for(std::uint32_t k=0;k<count;++k)p.area.push_back(rd32(d,o+44+k*4));
        portals.push_back(std::move(p));
    }
    return portals;
}
}

namespace hgw {
// Door connections: a static table in the executable at 0x3DCFC0, read by the door-graph
// object's methods 0x21C610 (other side's room), 0x21C6A0 (other side's door number) and
// 0x21C550 (entry -> room; entries < 0x190). Entry = two sides {u16 room, u8 door, u8 0,
// u16 value} + u32 flags; the table ends at room 0xFFFF (303 entries). Door number k of
// a room is slot-7 portal k when k < portal count (266 of 269 portals are listed);
// higher numbers are exits without a door model. `value` and `flags` are not yet known.
constexpr std::uint32_t door_table_address=0x3DCFC0;
constexpr std::uint32_t door_table_limit=0x190;

struct DoorSide {std::uint16_t room=0;std::uint8_t door=0;std::uint16_t value=0;};
struct DoorLink {DoorSide side[2];std::uint32_t flags=0;};

inline std::vector<DoorLink> parse_door_table(const std::uint8_t* t,std::size_t available) {
    std::vector<DoorLink> links;
    if(!t)return links;
    for(std::uint32_t i=0;i<door_table_limit&&(i+1)*16<=available;++i) {
        const auto* e=t+i*16;
        const auto room=[&](int o){return std::uint16_t(e[o]|e[o+1]<<8);};
        if(room(0)==0xffff)break;
        DoorLink link;
        for(int k=0;k<2;++k){link.side[k].room=room(k*6);link.side[k].door=e[k*6+2];link.side[k].value=room(k*6+4);}
        link.flags=e[12]|e[13]<<8|e[14]<<16|std::uint32_t(e[15])<<24;
        links.push_back(link);
    }
    return links;
}

// The other side of door `door` in `room`, if the table lists it.
inline const DoorSide* door_destination(const std::vector<DoorLink>& links,int room,int door,const DoorLink** link_out=nullptr) {
    for(const auto& l:links)for(int k=0;k<2;++k)
        if(l.side[k].room==room&&l.side[k].door==door){if(link_out)*link_out=&l;return &l.side[k^1];}
    return nullptr;
}
}

namespace hgw {
// Slot 6: camera paths. Header {u32 count, u32 0, count x {u32 channels, u32 keys}},
// padded to 16 bytes, then per path `channels` x `keys` 32-byte keys
// {f32 time, f32 value, f32 in dt, f32 in dvalue, f32 out dt, f32 out dvalue, u32 mode, u32}.
// All 154 rooms with slot 6 fit exactly and use 6 channels (eye xyz, target xyz); 219 of
// 430 paths start exactly at one of the room's slot-5 cameras. Mode 3 everywhere seen;
// the handles are a third of the key interval, so keys are drawn as cubic Beziers.
// What drives a path (time, or the player's position) is not known yet.
struct PathKey {float time,value,in_dt,in_dv,out_dt,out_dv;std::uint32_t mode;};
struct CameraPath {std::vector<std::vector<PathKey>> channels;std::size_t offset=0;};

inline std::vector<CameraPath> parse_camera_paths(const std::vector<std::uint8_t>& d) {
    std::vector<CameraPath> paths;
    if(d.size()<8)return paths;
    const auto count=rd32(d,0);
    if(count>64||8+std::size_t(count)*8>d.size())throw std::runtime_error("implausible camera path count");
    std::size_t o=(8+std::size_t(count)*8+15)/16*16;
    for(std::uint32_t i=0;i<count;++i) {
        const auto channels=rd32(d,8+i*8),keys=rd32(d,12+i*8);
        if(channels>16||keys>4096||o+std::size_t(channels)*keys*32>d.size())throw std::runtime_error("camera path runs past its slot");
        CameraPath path;path.offset=o;
        for(std::uint32_t c=0;c<channels;++c) {
            std::vector<PathKey> list;
            for(std::uint32_t k=0;k<keys;++k,o+=32)
                list.push_back({rdf(d,o),rdf(d,o+4),rdf(d,o+8),rdf(d,o+12),rdf(d,o+16),rdf(d,o+20),rd32(d,o+24)});
            path.channels.push_back(std::move(list));
        }
        paths.push_back(std::move(path));
    }
    return paths;
}

// Value of one channel at fraction u in [0,1] of the segment after key k (cubic Bezier).
inline float path_value(const std::vector<PathKey>& keys,std::size_t k,float u) {
    if(k+1>=keys.size())return keys.back().value;
    const float p0=keys[k].value,p1=p0+keys[k].out_dv,p3=keys[k+1].value,p2=p3+keys[k+1].in_dv,v=1-u;
    return v*v*v*p0+3*v*v*u*p1+3*v*u*u*p2+u*u*u*p3;
}
}
