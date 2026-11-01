#pragma once
// Structural room edits (add/remove objects). A room PAC is split into its 17 sections,
// one section is changed, and the file is rebuilt with fresh section offsets: original
// section order and 0x20 alignment are kept, slots that shared one offset keep sharing,
// absent slots stay absent. The room loader reads a whole PAC into a 0x2A0000-byte
// buffer (0x120720), so rebuilt rooms larger than that are refused.
#include "room.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace hgw {

constexpr std::size_t room_buffer_limit=0x2A0000;

struct PacSections {
    std::array<std::vector<std::uint8_t>,room_slots> data;
    std::array<bool,room_slots> present{};
    std::array<int,room_slots> alias{};          // slot whose data this slot shares, or -1
    std::array<std::uint32_t,room_slots> order{}; // original offset, to keep section order
};

inline PacSections split_pac(const std::vector<std::uint8_t>& bytes) {
    const RoomPac pac(bytes);
    PacSections s;
    for(int k=0;k<room_slots;++k) {
        s.present[k]=pac.offset[k]!=0;s.order[k]=pac.offset[k];s.alias[k]=-1;
        if(!s.present[k])continue;
        for(int j=0;j<k;++j)if(s.present[j]&&pac.offset[j]==pac.offset[k]){s.alias[k]=j;break;}
        if(s.alias[k]<0)s.data[k]=pac.section(k);
    }
    return s;
}

inline std::vector<std::uint8_t> join_pac(const PacSections& s) {
    std::array<int,room_slots> slots{};
    for(int k=0;k<room_slots;++k)slots[k]=k;
    std::stable_sort(slots.begin(),slots.end(),[&](int a,int b){return s.order[a]<s.order[b];});
    std::vector<std::uint8_t> out(0x800,0);
    std::array<std::uint32_t,room_slots> offset{};
    for(const int k:slots) {
        if(!s.present[k]||s.alias[k]>=0)continue;
        offset[k]=std::uint32_t(out.size());
        out.insert(out.end(),s.data[k].begin(),s.data[k].end());
        while(out.size()%0x20)out.push_back(0);
    }
    for(int k=0;k<room_slots;++k) {
        if(!s.present[k])continue;
        const auto o=s.alias[k]>=0?offset[s.alias[k]]:offset[k];
        for(int i=0;i<4;++i)out[k*4+i]=std::uint8_t(o>>(8*i));
    }
    if(out.size()>room_buffer_limit)
        throw std::runtime_error("room would be "+std::to_string(out.size())+" bytes; the game's room buffer holds "+
                                 std::to_string(room_buffer_limit));
    return out;
}

inline void put32(std::vector<std::uint8_t>& d,std::size_t o,std::uint32_t v) {
    if(o+4>d.size())throw std::runtime_error("write past end of section");
    for(int i=0;i<4;++i)d[o+i]=std::uint8_t(v>>(8*i));
}

// ---- slot 3 / room meshes ------------------------------------------------------------
inline std::size_t mesh_record_size(const std::vector<std::uint8_t>& d,std::size_t o) {
    const auto pad=[](std::size_t n){return (n+15)/16*16;};
    const std::size_t count=rd32(d,o);
    return 80+pad(count*8)+pad(count*4)+count*16;
}

// Block (0..7) whose record run contains `record`.
inline int mesh_block(const std::vector<std::uint8_t>& d,std::size_t record) {
    int best=-1;std::uint32_t start=0;
    for(int b=0;b<geometry_blocks;++b){const auto o=rd32(d,b*4u);if(o&&o<=record&&o>=start){start=o;best=b;}}
    if(best<0)throw std::runtime_error("mesh record is not inside a geometry block");
    return best;
}

// Shifts the slot-3 block table for blocks starting after `at`.
inline void shift_blocks(std::vector<std::uint8_t>& d,std::size_t at,std::int64_t delta) {
    for(int b=0;b<geometry_blocks;++b){const auto o=rd32(d,b*4u);if(o>at)put32(d,b*4u,std::uint32_t(o+delta));}
}

// Copies the mesh record at `record` to the end of its block (existing mesh indices are
// unchanged) and moves the copy by `delta`; returns the copy's record offset.
inline std::size_t duplicate_mesh(std::vector<std::uint8_t>& d,std::size_t record,const float delta[3]) {
    const int block=mesh_block(d,record);
    std::size_t end=rd32(d,block*4u);
    while(!(rd32(d,end)==0xffffffffu&&rd32(d,end+4)==0xffffffffu))end+=mesh_record_size(d,end);
    const auto size=mesh_record_size(d,record);
    std::vector<std::uint8_t> copy(d.begin()+record,d.begin()+record+size);
    for(int k=0;k<3;++k)wrf(copy,16+12+k*16,rdf(copy,16+12+k*16)+delta[k]);
    d.insert(d.begin()+end,copy.begin(),copy.end());
    shift_blocks(d,end,std::int64_t(size));
    return end;
}

inline void remove_mesh(std::vector<std::uint8_t>& d,std::size_t record) {
    (void)mesh_block(d,record);
    const auto size=mesh_record_size(d,record);
    d.erase(d.begin()+record,d.begin()+record+size);
    shift_blocks(d,record,-std::int64_t(size));
}

// ---- slot 4 / lights -----------------------------------------------------------------
inline std::size_t duplicate_light(std::vector<std::uint8_t>& d,std::size_t record,const float delta[3]) {
    const auto count=rd32(d,0);
    const std::size_t end=16+std::size_t(count)*0x30;
    std::vector<std::uint8_t> copy(d.begin()+record,d.begin()+record+0x30);
    for(int k=0;k<3;++k)wrf(copy,k*4,rdf(copy,k*4)+delta[k]);
    d.insert(d.begin()+end,copy.begin(),copy.end());
    put32(d,0,count+1);
    return end;
}

inline void remove_light(std::vector<std::uint8_t>& d,std::size_t record) {
    d.erase(d.begin()+record,d.begin()+record+0x30);
    put32(d,0,rd32(d,0)-1);
}

// ---- slot 5 / fixed cameras ----------------------------------------------------------
inline std::size_t duplicate_camera(std::vector<std::uint8_t>& d,std::size_t record,const float delta[3]) {
    std::size_t end=0;
    while(end+32<=d.size()&&rd32(d,end)!=0xffffffffu)end+=32;
    std::vector<std::uint8_t> copy(d.begin()+record,d.begin()+record+32);
    for(int k=0;k<3;++k){wrf(copy,k*4,rdf(copy,k*4)+delta[k]);wrf(copy,16+k*4,rdf(copy,16+k*4)+delta[k]);}
    d.insert(d.begin()+end,copy.begin(),copy.end());
    return end;
}

inline void remove_camera(std::vector<std::uint8_t>& d,std::size_t record) {
    d.erase(d.begin()+record,d.begin()+record+32);
}

// ---- slot 2 / trigger shapes ---------------------------------------------------------
// Table of u32 offsets (4-byte units) ending at the lowest record. Removal clears the
// table entry so other shapes keep their numbers; duplication appends a record and a
// table entry (growing the table by 16 bytes, and shifting records, if needed).
inline std::size_t shape_table_end(const std::vector<std::uint8_t>& d) {
    std::size_t end=d.size();
    for(std::size_t i=0;i*4<end;++i){const std::size_t o=std::size_t(rd32(d,i*4))*4;if(o)end=std::min(end,o);}
    return end;
}

inline int duplicate_shape(std::vector<std::uint8_t>& d,int index,const float delta[3]) {
    std::size_t table_end=shape_table_end(d);
    const std::size_t entries=table_end/4;
    const std::size_t record=std::size_t(rd32(d,index*4u))*4;
    if(!record)throw std::runtime_error("shape entry is empty");
    std::size_t stop=d.size();
    for(std::size_t i=0;i<entries;++i){const std::size_t o=std::size_t(rd32(d,i*4))*4;if(o>record&&o<stop)stop=o;}
    std::vector<std::uint8_t> copy(d.begin()+record,d.begin()+stop);
    for(std::size_t p=16;p<copy.size();p+=16)for(int k=0;k<3;++k)wrf(copy,p+k*4,rdf(copy,p+k*4)+delta[k]);
    // Reuse a free table slot (zero entry) if one exists, else grow the table.
    std::size_t slot=entries;
    for(std::size_t i=0;i<entries;++i)if(!rd32(d,i*4)){slot=i;break;}
    if(slot==entries) {
        d.insert(d.begin()+table_end,16,0);
        for(std::size_t i=0;i<entries;++i)if(const auto v=rd32(d,i*4))put32(d,i*4,v+4);
        table_end+=16;
    }
    // Trim trailing slot padding so the new record follows the last one.
    std::size_t data_end=table_end;
    for(std::size_t i=0;i*4<table_end;++i) {
        const std::size_t o=std::size_t(rd32(d,i*4))*4;
        if(!o)continue;
        std::size_t s=d.size();
        for(std::size_t j=0;j*4<table_end;++j){const std::size_t q=std::size_t(rd32(d,j*4))*4;if(q>o&&q<s)s=q;}
        if(s==d.size()) {   // the last record: its end is where its points stop being {x,y,z,1}
            std::size_t e=o+16;
            while(e+16<=d.size()&&rdf(d,e+12)==1.0f)e+=16;
            s=e;
        }
        data_end=std::max(data_end,s);
    }
    d.resize(data_end);
    put32(d,slot*4,std::uint32_t(data_end/4));
    d.insert(d.end(),copy.begin(),copy.end());
    return int(slot);
}

inline void remove_shape(std::vector<std::uint8_t>& d,int index) {
    if(!rd32(d,index*4u))throw std::runtime_error("shape entry is already empty");
    // Never clear the entry holding the lowest record: it marks where the table ends.
    put32(d,index*4u,0);
    if(shape_table_end(d)<std::size_t(index+1)*4)throw std::runtime_error("cannot remove the shape that bounds the table");
}

// ---- cross-room copy / paste of meshes --------------------------------------------------
// A copied mesh carries its texture. All 167 room texture banks store each texture's
// data contiguously in entry order right after the entry table (zero tail), so a bank is
// rebuilt cleanly when a texture is appended. Pastes stay inside the envelope the
// shipped game already uses: at most 16 textures and 1,064,960 bytes of texture data in
// one room (room 0x2A), and the 0x2A0000-byte room buffer.
constexpr std::size_t bank_texture_limit=16;
constexpr std::size_t bank_bytes_limit=1064960;

struct MeshClip {
    std::vector<std::uint8_t> record;
    int block=0;
    std::vector<std::uint8_t> texture_entry;   // 16-byte entry (offset field ignored)
    std::vector<std::uint8_t> texture_data;    // indices + palette
    int source_room=-1;
    bool valid() const {return !record.empty();}
};

struct BankTexture {std::vector<std::uint8_t> entry,data;};

inline std::vector<BankTexture> bank_textures(const std::vector<std::uint8_t>& bank) {
    std::vector<BankTexture> result;
    if(bank.size()<16)return result;
    const auto count=rd32(bank,0);
    for(std::uint32_t i=0;i<count;++i) {
        const std::size_t entry=16+i*16;
        const auto psm=rd32(bank,entry);
        const std::size_t width=bank[entry+4]|bank[entry+5]<<8,height=bank[entry+6]|bank[entry+7]<<8;
        const std::size_t bytes=width*height*(psm==0x13?8:4)/8+1024;
        const std::size_t start=entry+rd32(bank,entry+12);
        if(start+bytes>bank.size())throw std::runtime_error("texture data past end of bank");
        BankTexture t;t.entry.assign(bank.begin()+entry,bank.begin()+entry+12);
        t.data.assign(bank.begin()+start,bank.begin()+start+bytes);
        result.push_back(std::move(t));
    }
    return result;
}

inline std::vector<std::uint8_t> build_bank(const std::vector<BankTexture>& textures) {
    std::vector<std::uint8_t> bank(16+16*textures.size(),0);
    put32(bank,0,std::uint32_t(textures.size()));
    for(std::size_t i=0;i<textures.size();++i) {
        const std::size_t entry=16+i*16;
        std::copy(textures[i].entry.begin(),textures[i].entry.begin()+12,bank.begin()+entry);
        put32(bank,entry+12,std::uint32_t(bank.size()-entry));
        bank.insert(bank.end(),textures[i].data.begin(),textures[i].data.end());
    }
    while(bank.size()%16)bank.push_back(0);
    return bank;
}

inline MeshClip copy_mesh(const std::vector<std::uint8_t>& pac_bytes,std::size_t record,int room) {
    const RoomPac pac(pac_bytes);
    const auto geometry=pac.section(3);
    MeshClip clip;clip.source_room=room;
    clip.block=mesh_block(geometry,record);
    clip.record.assign(geometry.begin()+record,geometry.begin()+record+mesh_record_size(geometry,record));
    const auto texture=rd32(clip.record,4);
    const auto textures=bank_textures(pac.section(9));
    if(texture<textures.size()){clip.texture_entry=textures[texture].entry;clip.texture_data=textures[texture].data;}
    return clip;
}

// Adds the clip's texture to the bank unless an identical one exists; returns its index,
// or 0xFFFFFFFF when the clip has no texture.
inline std::uint32_t paste_texture(std::vector<std::uint8_t>& bank,const MeshClip& clip) {
    if(clip.texture_data.empty())return 0xffffffffu;
    auto textures=bank_textures(bank);
    for(std::size_t i=0;i<textures.size();++i)
        if(textures[i].entry==clip.texture_entry&&textures[i].data==clip.texture_data)return std::uint32_t(i);
    std::size_t bytes=clip.texture_data.size();
    for(const auto& t:textures)bytes+=t.data.size();
    if(textures.size()+1>bank_texture_limit)throw std::runtime_error("this room already has the maximum of 16 textures the game uses");
    if(bytes>bank_bytes_limit)throw std::runtime_error("the room's textures would exceed the largest texture bank the game uses");
    textures.push_back({clip.texture_entry,clip.texture_data});
    bank=build_bank(textures);
    return std::uint32_t(textures.size()-1);
}

// Appends the clip's mesh to block `block` (or block 0 if the room lacks it), sets its
// texture index and places it at `at`; returns the new record offset.
inline std::size_t paste_mesh(std::vector<std::uint8_t>& geometry,const MeshClip& clip,std::uint32_t texture,const float at[3]) {
    int block=clip.block;
    if(!rd32(geometry,block*4u))block=0;
    std::size_t end=rd32(geometry,block*4u);
    if(!end)throw std::runtime_error("room has no geometry block to paste into");
    while(!(rd32(geometry,end)==0xffffffffu&&rd32(geometry,end+4)==0xffffffffu))end+=mesh_record_size(geometry,end);
    auto record=clip.record;
    put32(record,4,texture);
    // Header flags with a high byte of 0x15..0x1C mark meshes the room's events show or hide
    // (e.g. a vase and its broken pieces in room 0x00); in another room that group may be
    // hidden, so pasted meshes are made unconditional by clearing that byte.
    put32(record,8,rd32(record,8)&0x00ffffffu);
    for(int k=0;k<3;++k)wrf(record,16+12+k*16,at[k]);
    geometry.insert(geometry.begin()+end,record.begin(),record.end());
    shift_blocks(geometry,end,std::int64_t(record.size()));
    return end;
}
}
