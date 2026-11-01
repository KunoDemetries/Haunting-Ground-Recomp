#pragma once
// HG-DIAG-123: autopilot. Synthetic controller input computed from game state read in EE
// RAM, for playing routes unattended (user request 2026-10-21: play through the game by
// itself). Opt-in (`--autopilot script`); it only writes the virtual controller the way a
// recorded input event does, and every snapshot it applies is recorded like live input, so
// checkpoints/input logs replay it exactly. Not normal behaviour: see docs/DIAGNOSTICS.md.
//
// Game state (verified against RAM captures of the pinned route, see PROGRESS 2026-10-21):
//   *(0x44F818) = Fiona; +0x10 f32 x,y,z; +0x30 s32 room; +0x54 f32 yaw, forward = (sin, cos).
//   *(0x44E4B8) = camera; +0x40 eye, +0x50 target, +0x70 unit forward = normalize(target-eye).
// Control is camera-relative: a stick angle phi (0 = up, clockwise) makes her head for
// world yaw = camera_yaw - phi, camera_yaw = atan2(fx, fz) (checked against the headings
// she settled on in a traced run of room 0x21).
//
// Script, one command per line (# comments):
//   goto X Z [radius]     walk to a point in the current room (default radius 0.6)
//   door X Z              walk to a door point, then press Circle until the room changes
//   push X Z ROOM         walk toward the point until the room id becomes ROOM
//   press BUTTON [polls]  hold cross|circle|square|triangle|start|select|l1|r1 (default 3)
//   wait POLLS            neutral controller
//   waitroom ROOM         neutral until the room id is ROOM
//   run 0|1               hold Cross (run) while walking
//   timeout POLLS         per-command limit (default 4000); expiry stops with an error
#include "native_host.hpp"
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace hg {

struct AutopilotState {
    bool valid=false;
    float x=0,y=0,z=0,yaw=0,camera_yaw=0;
    std::uint32_t room=0;
};

template<class Ram>
inline AutopilotState autopilot_read_state(const Ram& ram) {
    const auto u32=[&](std::uint32_t a)->std::uint32_t {
        if(std::uint64_t(a)+4>ram.size())return 0;
        return std::uint32_t(ram[a])|std::uint32_t(ram[a+1])<<8|std::uint32_t(ram[a+2])<<16|std::uint32_t(ram[a+3])<<24;
    };
    const auto f32=[&](std::uint32_t a){const auto v=u32(a);float f;std::memcpy(&f,&v,4);return f;};
    AutopilotState s;
    const auto fiona=u32(0x44F818)&0x1ffffffu;
    if(!fiona||std::uint64_t(fiona)+0x60>ram.size()||u32(fiona)!=0x46AAB0u)return s;
    s.valid=true;s.x=f32(fiona+0x10);s.y=f32(fiona+0x14);s.z=f32(fiona+0x18);
    s.room=u32(fiona+0x30);s.yaw=f32(fiona+0x54);
    const auto camera=u32(0x44E4B8)&0x1ffffffu;
    if(!camera||std::uint64_t(camera)+0x80>ram.size())return AutopilotState{};
    const float fx=f32(camera+0x70),fz=f32(camera+0x78);
    if(!std::isfinite(fx)||!std::isfinite(fz)||fx*fx+fz*fz<1e-6f)return AutopilotState{};
    s.camera_yaw=std::atan2(fx,fz);
    if(!std::isfinite(s.x)||!std::isfinite(s.z)||!std::isfinite(s.yaw))s.valid=false;
    return s;
}

class Autopilot {
public:
    struct Command {std::string op;float x=0,z=0,r=0;std::uint32_t value=0;std::uint32_t timeout=4000;};

    void load(const std::string& path) {
        std::ifstream in(path);
        if(!in)throw std::runtime_error("cannot open autopilot script "+path);
        std::string line;std::uint32_t timeout=4000;int number=0;
        while(std::getline(in,line)) {
            ++number;
            if(const auto hash=line.find('#');hash!=std::string::npos)line.resize(hash);
            std::istringstream words(line);Command c;
            if(!(words>>c.op))continue;
            const auto bad=[&]{return std::runtime_error("autopilot script line "+std::to_string(number)+": "+line);};
            if(c.op=="goto"){if(!(words>>c.x>>c.z))throw bad();if(!(words>>c.r))c.r=0.6f;}
            else if(c.op=="door"){if(!(words>>c.x>>c.z))throw bad();c.r=1.5f;}
            else if(c.op=="push"){std::string room;if(!(words>>c.x>>c.z>>room))throw bad();c.value=std::uint32_t(std::stoul(room,nullptr,16));}
            else if(c.op=="press"){std::string b;if(!(words>>b))throw bad();c.value=button(b);if(!c.value)throw bad();if(!(words>>c.r))c.r=3;}
            else if(c.op=="wait"){if(!(words>>c.r))throw bad();}
            else if(c.op=="waitroom"){std::string room;if(!(words>>room))throw bad();c.value=std::uint32_t(std::stoul(room,nullptr,16));}
            else if(c.op=="run"){if(!(words>>c.value))throw bad();}
            else if(c.op=="timeout"){if(!(words>>timeout))throw bad();continue;}
            else throw bad();
            c.timeout=timeout;
            script_.push_back(c);
        }
        if(script_.empty())throw std::runtime_error("autopilot script is empty");
    }
    bool done() const {return index_>=script_.size();}
    std::size_t index() const {return index_;}
    const std::string& last_message() const {return message_;}
    // One-line state for HG_AUTOPILOT_TRACE.
    std::string trace(const AutopilotState& s) const {
        std::ostringstream o;
        o<<"cmd="<<index_+1<<" room="<<std::hex<<s.room<<std::dec<<" x="<<s.x<<" z="<<s.z<<" yaw="<<s.yaw
         <<" camera_yaw="<<s.camera_yaw<<" phi="<<last_phi_;
        return o.str();
    }

    // One poll: returns the controller snapshot to apply. Throws when a command times out.
    HostInput step(const AutopilotState& s) {
        HostInput pad;message_.clear();
        if(done()||!s.valid)return pad;
        auto& c=script_[index_];
        if(++polls_>c.timeout)throw std::runtime_error("autopilot command "+std::to_string(index_+1)+" ("+c.op+") timed out at room "+
            hex(s.room)+" x="+std::to_string(s.x)+" z="+std::to_string(s.z));
        camera_yaw_=s.camera_yaw;
        if(c.op=="goto"||c.op=="door"||c.op=="push") {
            if(c.op=="push"&&s.room==c.value)return next("reached room "+hex(s.room));
            const float dx=c.x-s.x,dz=c.z-s.z,dist=std::sqrt(dx*dx+dz*dz);
            if(c.op=="door"&&start_room_==~0u)start_room_=s.room;
            if(c.op=="door"&&s.room!=start_room_)return next("door led to room "+hex(s.room));
            if(dist<c.r&&c.op!="push") {
                if(c.op=="goto")return next("at "+std::to_string(s.x)+","+std::to_string(s.z));
                // door: face it and press Circle every other poll
                steer(pad,std::atan2(dx,dz),0.3f);
                if(polls_&2){pad.buttons&=std::uint16_t(~(1u<<13));pad.pressures[5]=255;}
                return pad;
            }
            steer(pad,std::atan2(dx,dz),1.0f);
            if(run_){pad.buttons&=std::uint16_t(~(1u<<14));pad.pressures[6]=255;}
            return pad;
        }
        if(c.op=="press") {
            if(polls_>std::uint32_t(c.r)*2)return next("pressed");
            if(polls_<=std::uint32_t(c.r))apply_button(pad,c.value);
            return pad;
        }
        if(c.op=="wait"){if(polls_>=std::uint32_t(c.r))return next("waited");return pad;}
        if(c.op=="waitroom"){if(s.room==c.value)return next("room "+hex(s.room));return pad;}
        if(c.op=="run"){run_=c.value!=0;return next(run_?"running":"walking");}
        return pad;
    }

private:
    static std::string hex(std::uint32_t v){std::ostringstream o;o<<std::hex<<v;return o.str();}
    static std::uint32_t button(const std::string& b) {
        static const std::pair<const char*,unsigned> names[]={{"cross",14},{"circle",13},{"square",15},{"triangle",12},
            {"start",3},{"select",0},{"l1",10},{"r1",11},{"l2",8},{"r2",9}};
        for(const auto& [name,bit]:names)if(b==name)return 1u<<bit|0x10000u;
        return 0;
    }
    static void apply_button(HostInput& pad,std::uint32_t value) {
        const unsigned mask=value&0xffffu;pad.buttons&=std::uint16_t(~mask);
        static const int pressure[16]={-1,-1,-1,-1,-1,-1,-1,-1,10,11,8,9,4,5,6,7};
        for(unsigned bit=0;bit<16;++bit)if(mask>>bit&1u&&pressure[bit]>=0)pad.pressures[pressure[bit]]=255;
    }
    HostInput next(std::string message) {
        message_="autopilot "+std::to_string(index_+1)+" "+script_[index_].op+": "+message;
        ++index_;polls_=0;start_room_=~0u;
        return HostInput{};
    }
    void steer(HostInput& pad,float heading,float amount) {
        const float phi=camera_yaw_-heading;
        const float sx=std::sin(phi)*amount,sy=-std::cos(phi)*amount;
        pad.axes[2]=std::uint8_t(std::lround(127.5f+sx*127.0f));
        pad.axes[3]=std::uint8_t(std::lround(127.5f+sy*127.0f));
        last_phi_=phi;last_amount_=amount;
    }
    static float wrap(float a){while(a>3.14159265f)a-=6.2831853f;while(a<-3.14159265f)a+=6.2831853f;return a;}

    std::vector<Command> script_;
    std::size_t index_=0;std::uint32_t polls_=0,start_room_=~0u;
    bool run_=false;
    float camera_yaw_=0,last_phi_=0,last_amount_=0;
    std::string message_;
};
}
