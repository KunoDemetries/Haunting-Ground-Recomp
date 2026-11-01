#pragma once
// Character and creature models (`O_xxx/xxx_nnn.PCK` + the folder's `.TEX`) turned into
// static room geometry for the object library. Layout (docs/WORLD_EDITOR.md, "Character
// models"); every one of the 115 PCK files on the disc follows it:
//   u32 section count (4), section offsets (0 = absent).
//   Section 0: u32 bones, u32 skinned table, u32 rigid table; bones are 0x70 records from +0x30.
//     Skinned table: u32 count, 12 bytes, count x 0x30 records; record-relative offsets
//       w[1] {int32 x3 row, pad} + count x int16 xyz deltas (position = row + running sum, /4096),
//       w[2] int16 UV pairs (/32768), w[6] one strip byte per vertex (1 = restart, 0 = draw),
//       w[7] texture, w[8] 1 = translucent pass, w[0] vertex count.
//     Rigid table: same, 0x20 records {count, positions, UV, normals, strip bytes, texture, ?, bone}.
//   Section 1 (morph meshes: faces and hands): u32 count, 12 bytes, count x 0x40 entries;
//     entry-relative offsets {kind, count, UV, strip, frame table, bone, ?, ?, translucent, ...,
//     int32 pivot xyz at +0x30}. Kind 0x10 (faces) has int32 strip words (negative = restart),
//     kind 0xA (hands) strip bytes. Frame 0 = int16 xyz /4096 + pivot at table + table[0].
//     Morph meshes use texture 0 (every model on the disc renders correctly with it).
// Positions are already in model space (bind pose, feet at y=0), in room units (Fiona is
// 16.9 tall, room doors 22).
#include "disc.hpp"
#include "edit.hpp"
#include "library.hpp"
#include <algorithm>
#include <cstdio>
#include <functional>
#include <array>
#include <cstdint>
#include <cstring>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace hgw {

struct CharacterPart {
    std::uint32_t texture=0;
    bool translucent=false;
    std::vector<std::array<float,3>> position;
    std::vector<std::array<float,2>> uv;
    std::vector<std::uint8_t> restart;        // 1 = start a new strip at this vertex (no triangle)
};

struct CharacterModel {
    std::vector<CharacterPart> parts;          // merged per (texture, translucent)
    float lo[3]{1e30f,1e30f,1e30f},hi[3]{-1e30f,-1e30f,-1e30f};
    int triangles=0;
};

namespace detail {
inline std::int16_t rd16s(const std::vector<std::uint8_t>& d,std::size_t o) {
    return std::int16_t(d[o]|d[o+1]<<8);
}
inline void need(const std::vector<std::uint8_t>& d,std::size_t end,const char* what) {
    if(end>d.size())throw std::runtime_error(std::string("character model: ")+what+" runs past the file");
}
}

inline CharacterModel parse_character(const std::vector<std::uint8_t>& d) {
    using detail::need;using detail::rd16s;
    need(d,0x14,"header");
    const auto sections=rd32(d,0);
    if(sections<2||sections>8)throw std::runtime_error("character model: unexpected section count");
    std::map<std::pair<std::uint32_t,bool>,CharacterPart> merged;
    const auto add=[&](std::uint32_t texture,bool translucent,const std::vector<std::array<float,3>>& p,
                       const std::vector<std::array<float,2>>& uv,const std::vector<std::uint8_t>& restart) {
        if(p.size()<3)return;
        if(!restart[0]||!restart[1])throw std::runtime_error("character model: strip does not start with a restart");
        auto& part=merged[{texture,translucent}];
        part.texture=texture;part.translucent=translucent;
        part.position.insert(part.position.end(),p.begin(),p.end());
        part.uv.insert(part.uv.end(),uv.begin(),uv.end());
        part.restart.insert(part.restart.end(),restart.begin(),restart.end());
    };
    // Running-sum positions: int32 row, 4 bytes pad, then int16 deltas.
    const auto positions=[&](std::size_t at,std::uint32_t n) {
        need(d,at+16+std::size_t(n)*6,"positions");
        std::int64_t acc[3]={std::int32_t(rd32(d,at)),std::int32_t(rd32(d,at+4)),std::int32_t(rd32(d,at+8))};
        std::vector<std::array<float,3>> p(n);
        for(std::uint32_t i=0;i<n;++i)for(int k=0;k<3;++k){acc[k]+=rd16s(d,at+16+i*6u+k*2u);p[i][k]=float(acc[k])/4096.0f;}
        return p;
    };
    const auto uvs=[&](std::size_t at,std::uint32_t n) {
        need(d,at+std::size_t(n)*4,"texture coordinates");
        std::vector<std::array<float,2>> uv(n);
        for(std::uint32_t i=0;i<n;++i){uv[i][0]=rd16s(d,at+i*4u)/32768.0f;uv[i][1]=rd16s(d,at+i*4u+2)/32768.0f;}
        return uv;
    };
    const auto strip_bytes=[&](std::size_t at,std::uint32_t n) {
        need(d,at+n,"strip flags");
        std::vector<std::uint8_t> r(d.begin()+std::ptrdiff_t(at),d.begin()+std::ptrdiff_t(at+n));
        for(auto& f:r){if(f>1)throw std::runtime_error("character model: unknown strip flag");}
        return r;
    };
    const std::size_t s0=rd32(d,4);
    need(d,s0+12,"section 0");
    {   // skinned groups
        const std::size_t table=s0+rd32(d,s0+4);
        need(d,table+16,"group table");
        const auto count=rd32(d,table);
        if(count>512)throw std::runtime_error("character model: implausible group count");
        for(std::uint32_t g=0;g<count;++g) {
            const std::size_t rec=table+16+g*0x30u;
            need(d,rec+0x30,"group record");
            const auto n=rd32(d,rec);
            if(n>0x10000)throw std::runtime_error("character model: implausible vertex count");
            add(rd32(d,rec+28),rd32(d,rec+32)!=0,positions(rec+rd32(d,rec+4),n),uvs(rec+rd32(d,rec+8),n),strip_bytes(rec+rd32(d,rec+24),n));
        }
    }
    if(rd32(d,s0+8)) {   // rigid groups (eyes, earrings, whole props such as the wheelchair)
        const std::size_t table=s0+rd32(d,s0+8);
        need(d,table+16,"rigid table");
        const auto count=rd32(d,table);
        if(count>512)throw std::runtime_error("character model: implausible rigid count");
        for(std::uint32_t g=0;g<count;++g) {
            const std::size_t rec=table+16+g*0x20u;
            need(d,rec+0x20,"rigid record");
            const auto n=rd32(d,rec);
            if(!n)continue;
            if(n>0x10000)throw std::runtime_error("character model: implausible vertex count");
            add(rd32(d,rec+20),false,positions(rec+rd32(d,rec+4),n),uvs(rec+rd32(d,rec+8),n),strip_bytes(rec+rd32(d,rec+16),n));
        }
    }
    if(const std::size_t s1=rd32(d,8)) {   // morph meshes, bind frame
        need(d,s1+16,"morph section");
        const auto count=rd32(d,s1);
        if(count>64)throw std::runtime_error("character model: implausible morph count");
        for(std::uint32_t e=0;e<count;++e) {
            const std::size_t ent=s1+16+e*0x40u;
            need(d,ent+0x40,"morph entry");
            const auto kind=rd32(d,ent),n=rd32(d,ent+4);
            if(!n)continue;
            if(kind!=0x10&&kind!=0xa)throw std::runtime_error("character model: unknown morph kind");
            if(n>0x10000)throw std::runtime_error("character model: implausible vertex count");
            const float pivot[3]={std::int32_t(rd32(d,ent+0x30))/4096.0f,std::int32_t(rd32(d,ent+0x34))/4096.0f,std::int32_t(rd32(d,ent+0x38))/4096.0f};
            const std::size_t frames=ent+rd32(d,ent+16);
            need(d,frames+4,"morph frame table");
            const std::size_t frame=frames+rd32(d,frames);
            need(d,frame+std::size_t(n)*6,"morph frame");
            std::vector<std::array<float,3>> p(n);
            for(std::uint32_t i=0;i<n;++i)for(int k=0;k<3;++k)p[i][k]=rd16s(d,frame+i*6u+k*2u)/4096.0f+pivot[k];
            std::vector<std::uint8_t> restart(n);
            const std::size_t strip=ent+rd32(d,ent+12);
            if(kind==0x10) {
                need(d,strip+std::size_t(n)*4,"morph strip words");
                for(std::uint32_t i=0;i<n;++i)restart[i]=std::int32_t(rd32(d,strip+i*4u))<0;
            } else restart=strip_bytes(strip,n);
            add(0,rd32(d,ent+32)!=0,p,uvs(ent+rd32(d,ent+8),n),restart);
        }
    }
    CharacterModel model;
    for(auto& [key,part]:merged) {
        for(std::size_t i=0;i<part.position.size();++i) {
            for(int k=0;k<3;++k){model.lo[k]=std::min(model.lo[k],part.position[i][k]);model.hi[k]=std::max(model.hi[k],part.position[i][k]);}
            if(!part.restart[i])++model.triangles;
        }
        model.parts.push_back(std::move(part));
    }
    return model;
}

// Builds a pasteable object: one mesh record per part, vertices relative to the model's
// floor centre, vertex colour `colour` (PS2 modulate, 0x80 = 1.0; rooms are prelit, so the
// caller passes the target room's typical colour). Opaque parts go to block 0, translucent
// ones (hair cards, eyelashes) to block 1 with flags 1, the most common block-1 header.
inline ObjectClip character_clip(const CharacterModel& model,const std::vector<std::uint8_t>& tex_file,
                                 const std::array<std::uint8_t,4>& colour) {
    const auto textures=bank_textures(tex_file);
    const float centre[3]={(model.lo[0]+model.hi[0])/2,model.lo[1],(model.lo[2]+model.hi[2])/2};
    const auto pad=[](std::size_t n){return (n+15)/16*16;};
    ObjectClip clip;
    for(const auto& part:model.parts) {
        const std::size_t n=part.position.size();
        const std::size_t uv=80,rgba=uv+pad(n*8),xyz=rgba+pad(n*4);
        MeshClip mesh;
        mesh.record.assign(xyz+n*16,0);
        put32(mesh.record,0,std::uint32_t(n));
        put32(mesh.record,8,part.translucent?1u:0u);
        for(int k=0;k<4;++k)wrf(mesh.record,16+k*20u,1.0f);   // identity matrix
        for(std::size_t i=0;i<n;++i) {
            wrf(mesh.record,uv+i*8,part.uv[i][0]);wrf(mesh.record,uv+i*8+4,part.uv[i][1]);
            std::memcpy(&mesh.record[rgba+i*4],colour.data(),4);
            for(int k=0;k<3;++k)wrf(mesh.record,xyz+i*16+k*4u,part.position[i][k]-centre[k]);
            put32(mesh.record,xyz+i*16+12,part.restart[i]?0x8000u:0x7fffu);
        }
        mesh.block=part.translucent?1:0;
        if(part.texture>=textures.size())throw std::runtime_error("character model uses a texture its .TEX does not have");
        mesh.texture_entry=textures[part.texture].entry;mesh.texture_data=textures[part.texture].data;
        clip.parts.push_back(std::move(mesh));clip.offset.push_back({0,0,0});
    }
    return clip;
}

// Median vertex colour of a room's opaque geometry (falls back to 0x80 grey).
inline std::array<std::uint8_t,4> room_typical_colour(const std::vector<std::uint8_t>& geometry) {
    std::vector<int> c[3];
    try {
        for(const auto& m:parse_meshes(geometry,0))for(const auto& v:m.triangles){c[0].push_back(v.r);c[1].push_back(v.g);c[2].push_back(v.b);}
    } catch(const std::exception&) {}
    std::array<std::uint8_t,4> result{0x80,0x80,0x80,0x80};
    if(c[0].empty())return result;
    for(int k=0;k<3;++k){std::nth_element(c[k].begin(),c[k].begin()+std::ptrdiff_t(c[k].size()/2),c[k].end());result[k]=std::uint8_t(c[k][c[k].size()/2]);}
    return result;
}
}

namespace hgw {
// Mesh form (room winding convention) for previews and the viewer.
inline std::vector<Mesh> character_meshes(const CharacterModel& model) {
    std::vector<Mesh> meshes;
    for(const auto& part:model.parts) {
        Mesh mesh;mesh.texture=part.texture;
        int run=0;
        for(std::size_t i=0;i<part.position.size();++i) {
            if(part.restart[i]){run=(i==0||!part.restart[i-1])?1:run+1;continue;}
            if(run<2)continue;
            ++run;
            const bool forward=run%2;
            for(const std::size_t j:{forward?i-2:i-1,forward?i-1:i-2,i}) {
                Vertex v{};v.x=part.position[j][0];v.y=part.position[j][1];v.z=part.position[j][2];
                v.u=part.uv[j][0];v.v=part.uv[j][1];v.r=v.g=v.b=v.a=0x80;
                mesh.triangles.push_back(v);
            }
        }
        meshes.push_back(std::move(mesh));
    }
    return meshes;
}

// Character names shown in the library where the model is unmistakable from its render.
inline const char* character_name(const std::string& code) {
    if(code.size()==3&&code[0]=='F'&&code[1]=='I')return "FIONA";
    if(code=="DB0"||code=="DB1"||code=="DB2")return "DEBILITAS";
    if(code=="DNL")return "DANIELLA";
    if(code=="HEW")return "HEWIE";
    return "";
}

// One library entry per distinct character model on the disc. A model uses the .TEX of
// the same name, else its folder's xxx_000.TEX (e.g. FIN_001..006 share FIN_000.TEX).
inline std::vector<LibraryObject> build_character_library(const std::map<std::string,DiscEntry>& entries,
                                                          const std::function<std::vector<std::uint8_t>(const std::string&)>& read) {
    std::vector<LibraryObject> objects;
    std::map<std::uint64_t,bool> seen;
    for(const auto& [path,entry]:entries) {
        const auto dot=path.rfind(".PCK;1");
        if(entry.directory||dot==std::string::npos||dot+6!=path.size())continue;
        const auto slash=path.rfind('/');
        const std::string stem=path.substr(slash+1,dot-slash-1);          // e.g. FIN_000
        const std::string code=stem.substr(0,stem.find('_'));
        std::string tex=path.substr(0,dot)+".TEX;1";
        if(!entries.count(tex))tex=path.substr(0,slash+1)+code+"_000.TEX;1";
        if(!entries.count(tex))continue;
        try {
            const auto bytes=read(path);
            const auto model=parse_character(bytes);
            if(!model.triangles)continue;
            const auto tex_bytes=read(tex);
            std::uint64_t h=fnv(tex_bytes.data(),tex_bytes.size());
            for(const auto& part:model.parts)
                h=fnv(reinterpret_cast<const std::uint8_t*>(part.position.data()),part.position.size()*sizeof part.position[0],h);
            if(seen.count(h))continue;
            seen[h]=true;
            LibraryObject o;o.model=path;o.model_texture=tex;o.parts=int(model.parts.size());o.triangles=model.triangles;
            for(int k=0;k<3;++k)o.size[k]=model.hi[k]-model.lo[k];
            char text[160];
            std::snprintf(text,sizeof text,"CHAR %-8s %-9s %4d TRIS  %.0fX%.0fX%.0f",stem.c_str(),character_name(code),o.triangles,o.size[0],o.size[1],o.size[2]);
            o.label=text;
            objects.push_back(std::move(o));
        } catch(const std::exception&) {}
    }
    return objects;
}
}
