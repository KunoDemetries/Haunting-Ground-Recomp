#pragma once
// Object library: every room's placeable objects, for dropping into any room.
// Rooms store their scenery as separate mesh records; an "object" here is a group of
// small records (each < 25 units across) whose bounding boxes touch, kept when the group
// is < 30 units across, has >= 16 triangles and <= 64 parts. Walls and floors chain into
// large groups and are left out. Identical objects (same records apart from placement,
// same textures) found in several rooms are listed once with every room they appear in.
#include "edit.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace hgw {

struct LibraryObject {
    int room=-1;                              // room the entry is taken from
    std::vector<std::uint32_t> records;       // slot-3 record offsets in that room
    std::vector<int> rooms;                   // every room containing an identical object
    int parts=0,triangles=0,textures=0;
    float size[3]{};
    std::string label;
    std::string model,model_texture;          // character entries: PCK and TEX paths (room = -1)
};

inline std::uint64_t fnv(const std::uint8_t* p,std::size_t n,std::uint64_t h=1469598103934665603ull) {
    for(std::size_t i=0;i<n;++i){h^=p[i];h*=1099511628211ull;}
    return h;
}

struct MeshBox {std::size_t record;int block;float lo[3],hi[3];int triangles;std::uint32_t texture;};

inline std::vector<MeshBox> room_mesh_boxes(const std::vector<std::uint8_t>& geometry) {
    std::vector<MeshBox> boxes;
    for(int b=0;b<geometry_blocks;++b)for(const auto& m:parse_meshes(geometry,b)) {
        if(m.triangles.empty())continue;
        MeshBox box{m.offset,b,{1e30f,1e30f,1e30f},{-1e30f,-1e30f,-1e30f},int(m.triangles.size()/3),m.texture};
        for(const auto& v:m.triangles) {
            const float p[3]={v.x,v.y,v.z};
            for(int k=0;k<3;++k){box.lo[k]=std::min(box.lo[k],p[k]);box.hi[k]=std::max(box.hi[k],p[k]);}
        }
        boxes.push_back(box);
    }
    return boxes;
}

inline std::vector<LibraryObject> build_library(const std::vector<int>& rooms,
                                                const std::function<std::vector<std::uint8_t>(int)>& read_room) {
    std::vector<LibraryObject> objects;
    std::map<std::uint64_t,std::size_t> seen;
    const auto extent=[](const float lo[3],const float hi[3]){return std::max({hi[0]-lo[0],hi[1]-lo[1],hi[2]-lo[2]});};
    for(const int room:rooms) {
        std::vector<std::uint8_t> bytes;
        try {bytes=read_room(room);} catch(const std::exception&) {continue;}
        const RoomPac pac(bytes);
        const auto geometry=pac.section(3);
        if(geometry.empty())continue;
        std::vector<MeshBox> boxes;
        std::vector<BankTexture> bank;
        try {boxes=room_mesh_boxes(geometry);bank=bank_textures(pac.section(9));} catch(const std::exception&) {continue;}
        std::vector<int> small;
        for(int i=0;i<int(boxes.size());++i)if(extent(boxes[i].lo,boxes[i].hi)<25.0f)small.push_back(i);
        std::vector<int> parent(boxes.size());
        for(std::size_t i=0;i<parent.size();++i)parent[i]=int(i);
        const std::function<int(int)> find=[&](int i){while(parent[i]!=i){parent[i]=parent[parent[i]];i=parent[i];}return i;};
        constexpr float gap=0.3f;
        for(std::size_t a=0;a<small.size();++a)for(std::size_t b=a+1;b<small.size();++b) {
            const auto& A=boxes[small[a]];const auto& B=boxes[small[b]];
            bool touch=true;
            for(int k=0;k<3;++k)if(A.lo[k]-gap>B.hi[k]||B.lo[k]-gap>A.hi[k])touch=false;
            if(touch)parent[find(small[a])]=find(small[b]);
        }
        std::map<int,std::vector<int>> groups;
        for(const int i:small)groups[find(i)].push_back(i);
        for(auto& [root,members]:groups) {
            float lo[3]={1e30f,1e30f,1e30f},hi[3]={-1e30f,-1e30f,-1e30f};int triangles=0;
            for(const int i:members){triangles+=boxes[i].triangles;for(int k=0;k<3;++k){lo[k]=std::min(lo[k],boxes[i].lo[k]);hi[k]=std::max(hi[k],boxes[i].hi[k]);}}
            if(extent(lo,hi)>=30.0f||triangles<16||members.size()>64)continue;
            // Fingerprint: each part's record without placement/texture index, its texture
            // bytes and its offset from the group's centre.
            const float centre[3]={(lo[0]+hi[0])/2,lo[1],(lo[2]+hi[2])/2};
            std::vector<std::uint64_t> parts;std::vector<std::uint32_t> textures;
            for(const int i:members) {
                const auto size=mesh_record_size(geometry,boxes[i].record);
                std::vector<std::uint8_t> record(geometry.begin()+boxes[i].record,geometry.begin()+boxes[i].record+size);
                float rel[3];
                for(int k=0;k<3;++k){rel[k]=rdf(record,16+12+k*16)-centre[k];wrf(record,16+12+k*16,0);}
                put32(record,4,0);
                std::uint64_t h=fnv(record.data(),record.size());
                const auto t=boxes[i].texture;
                if(t<bank.size()){h=fnv(bank[t].data.data(),bank[t].data.size(),h);if(std::find(textures.begin(),textures.end(),t)==textures.end())textures.push_back(t);}
                std::int32_t q[3];for(int k=0;k<3;++k)q[k]=std::int32_t(std::lround(rel[k]*10));
                h=fnv(reinterpret_cast<const std::uint8_t*>(q),sizeof q,h);
                parts.push_back(h);
            }
            std::sort(parts.begin(),parts.end());
            const auto key=fnv(reinterpret_cast<const std::uint8_t*>(parts.data()),parts.size()*8);
            const auto found=seen.find(key);
            if(found!=seen.end()){auto& o=objects[found->second];if(o.rooms.back()!=room)o.rooms.push_back(room);continue;}
            LibraryObject object;object.room=room;object.rooms={room};
            std::sort(members.begin(),members.end(),[&](int a,int b){return boxes[a].record<boxes[b].record;});
            for(const int i:members)object.records.push_back(std::uint32_t(boxes[i].record));
            object.parts=int(members.size());object.triangles=triangles;object.textures=int(textures.size());
            for(int k=0;k<3;++k)object.size[k]=hi[k]-lo[k];
            seen[key]=objects.size();
            objects.push_back(std::move(object));
        }
    }
    for(std::size_t i=0;i<objects.size();++i) {
        auto& o=objects[i];
        char text[160];
        std::snprintf(text,sizeof text,"#%-4zu ROOM %03X  %4d TRIS  %.0fX%.0fX%.0f",i,o.room,o.triangles,o.size[0],o.size[1],o.size[2]);
        o.label=text;
        if(o.rooms.size()>1)o.label+="  +"+std::to_string(o.rooms.size()-1);
    }
    return objects;
}

// A library object ready to paste: its parts with offsets from the group's floor centre.
struct ObjectClip {
    std::vector<MeshClip> parts;
    std::vector<std::array<float,3>> offset;   // part translation minus the group's floor centre
    bool valid() const {return !parts.empty();}
};

inline ObjectClip copy_object(const std::vector<std::uint8_t>& room_bytes,const std::vector<std::uint32_t>& records,int room) {
    const RoomPac pac(room_bytes);
    const auto geometry=pac.section(3);
    const auto boxes=room_mesh_boxes(geometry);
    float lo[3]={1e30f,1e30f,1e30f},hi[3]={-1e30f,-1e30f,-1e30f};
    for(const auto& b:boxes)if(std::find(records.begin(),records.end(),b.record)!=records.end())
        for(int k=0;k<3;++k){lo[k]=std::min(lo[k],b.lo[k]);hi[k]=std::max(hi[k],b.hi[k]);}
    const float centre[3]={(lo[0]+hi[0])/2,lo[1],(lo[2]+hi[2])/2};
    ObjectClip clip;
    for(const auto record:records) {
        auto part=copy_mesh(room_bytes,record,room);
        std::array<float,3> rel{};
        for(int k=0;k<3;++k)rel[k]=rdf(part.record,16+12+k*16)-centre[k];
        clip.parts.push_back(std::move(part));clip.offset.push_back(rel);
    }
    return clip;
}

// Pastes every part (textures added once each) with its floor centre at `at`; returns the
// new record offsets in slot 3.
inline std::vector<std::size_t> paste_object(PacSections& sections,const ObjectClip& clip,const float at[3]) {
    if(!sections.present[3]||!sections.present[9]||sections.alias[3]>=0||sections.alias[9]>=0)
        throw std::runtime_error("this room has no geometry or texture bank to paste into");
    std::vector<std::size_t> placed;
    for(std::size_t i=0;i<clip.parts.size();++i) {
        const auto texture=paste_texture(sections.data[9],clip.parts[i]);
        const float where[3]={at[0]+clip.offset[i][0],at[1]+clip.offset[i][1],at[2]+clip.offset[i][2]};
        const auto record=paste_mesh(sections.data[3],clip.parts[i],texture,where);
        for(auto& p:placed)if(p>=record)p+=clip.parts[i].record.size();   // later blocks shifted
        placed.push_back(record);
    }
    return placed;
}
}
