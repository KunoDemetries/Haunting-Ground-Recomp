// Synthetic SPU2 voice synthesis checks (no game data).
#include "hg/iop.hpp"
#include <iostream>
#include <vector>
#define CHECK(x) do {if(!(x)){std::cerr<<"check failed at "<<__LINE__<<": "<<#x<<'\n';return 1;}}while(0)
namespace {
constexpr std::uint32_t core1=0x1f900400u;
void voice_reg(hg::IopState& s,std::uint32_t core_base,unsigned voice,unsigned offset,std::uint16_t value) {
    s.store(core_base+voice*0x10u+offset,2,value);
}
void voice_addr(hg::IopState& s,std::uint32_t core_base,unsigned voice,std::uint32_t halfwords) {
    s.store(core_base+0x1c0u+voice*0xcu,2,(halfwords>>16)&0x3fu);
    s.store(core_base+0x1c2u+voice*0xcu,2,halfwords&0xffffu);
}
// One ADPCM block of constant nibble value `nibble`, shift/filter byte 0x00.
void block(hg::IopState& s,std::uint32_t halfwords,std::uint8_t flags,std::uint8_t nibble) {
    auto* b=&s.spu2_ram[halfwords*2];
    b[0]=0x00;b[1]=flags;
    for(unsigned n=2;n<16;++n)b[n]=std::uint8_t(nibble|(nibble<<4));
}
std::int16_t envx(hg::IopState& s,std::uint32_t core_base,unsigned voice) {
    return std::int16_t(s.load(core_base+voice*0x10u+0xa,2,false));
}
}
int main() {
    // ADPCM: filter 0, shift 0 expands nibbles by 12 bits; filter 1 adds 60/64 of old.
    {
        std::uint8_t b[16]{0x00,0};for(unsigned n=2;n<16;++n)b[n]=0x71; // samples 1,7,1,7...
        std::int32_t old=0,older=0;std::int16_t out[28];
        hg::Spu2Voices::decode_block(b,old,older,out);
        CHECK(out[0]==0x1000 && out[1]==0x7000 && out[27]==0x7000);
        b[0]=0x14; // filter 1, shift 4: nibble<<8 + old*60/64.
        old=0x1000;older=0;hg::Spu2Voices::decode_block(b,old,older,out);
        CHECK(out[0]==0x100+((0x1000*60+32)>>6));
        b[0]=0x50;bool fault=false;try{hg::Spu2Voices::decode_block(b,old,older,out);}catch(const std::runtime_error&){fault=true;}
        CHECK(fault); // Filters 5..7 are not validated.
    }
    // Envelope: fastest linear attack (+7<<11 per Ts) saturates at 0x7fff after 3 steps.
    {
        std::int32_t level=0;std::uint32_t counter=0;hg::Spu2Voices::Rate r;
        for(int n=0;n<3;++n)CHECK(hg::Spu2Voices::envelope_step(level,counter,r));
        CHECK(level==0x7fff);
        r.shift=13;level=0;counter=0; // Increment 0x2000: one step every 4 Ts.
        int steps=0;for(int n=0;n<16;++n)steps+=hg::Spu2Voices::envelope_step(level,counter,r);
        CHECK(steps==4 && level==4*7);
        r={};r.shift=0x1f;r.step=3;level=0x1234;counter=0; // All-ones rate never steps.
        for(int n=0;n<100;++n)CHECK(!hg::Spu2Voices::envelope_step(level,counter,r));
        CHECK(level==0x1234);
        // Real-PS2 capture (SOURCES.md 2026-10-25): shift 1Fh never steps for steps 1..3,
        // shift 1Eh step 3 decreases by 5 once per 8000h Ts.
        for(unsigned step=1;step<4;++step) {
            r={};r.decrease=true;r.shift=0x1f;r.step=step;level=0x7ff7;counter=0;
            for(int n=0;n<200000;++n)CHECK(!hg::Spu2Voices::envelope_step(level,counter,r));
            CHECK(level==0x7ff7);
        }
        r={};r.decrease=true;r.shift=0x1e;r.step=3;level=0x7ff7;counter=0;
        steps=0;for(int n=0;n<3*0x8000;++n)steps+=hg::Spu2Voices::envelope_step(level,counter,r);
        CHECK(steps==3 && level==0x7fe8);
        // Release without the floor: 3FF7h - 4000h reads FFF7h for one Ts (hardware ENVX).
        r={};r.decrease=true;r.shift=0;level=0x3ff7;counter=0;
        CHECK(hg::Spu2Voices::envelope_step(level,counter,r,false) && std::uint16_t(level)==0xfff7);
        // Exponential decrease below 1000h keeps stepping (Step*Level>>15 rounds down);
        // C division toward zero used to freeze it at 0FFFh (found by the PCSX2 section check).
        r={};r.exponential=true;r.decrease=true;r.shift=0;level=0x0fff;counter=0;
        for(int n=0;n<200000 && level>0;++n)hg::Spu2Voices::envelope_step(level,counter,r);
        CHECK(level==0);
        r.shift=15;level=0x0fff;counter=0;   // the release rate the game's music voices use
        for(int n=0;n<2000000 && level>0;++n)hg::Spu2Voices::envelope_step(level,counter,r);
        CHECK(level==0);
    }
    // Looping voice on core 1: key-on, sustain, ENDX at the loop end, key-off release.
    {
        hg::IopState s;
        s.store(core1+0x19a,2,0xc000);s.store(core1+0x198,2,0x0c00); // Enabled; MSNDL/MSNDR.
        s.store(core1+0x188,2,1);s.store(core1+0x190,2,1);            // Voice 0 dry L/R.
        s.store(0x1f900788u,2,0x3fff);s.store(0x1f90078au,2,0x3fff);  // Core 1 MVOL.
        voice_reg(s,core1,0,0,0x3fff);voice_reg(s,core1,0,2,0x3fff);  // VOLL/VOLR.
        voice_reg(s,core1,0,4,0x1000);                                  // One sample per Ts.
        voice_reg(s,core1,0,6,0x000f);                                  // Fastest attack, DR 0, SL 15.
        voice_reg(s,core1,0,8,0x1fc0|0x00);                             // Sustain never steps; RR 0.
        block(s,0x3000,0x04|0x03,0x2);                                  // Loop start + end/repeat.
        voice_addr(s,core1,0,0x3000);
        s.store(core1+0x1a0,2,1);                                       // KON voice 0.
        int nonzero=0;
        for(int n=0;n<10;++n)s.advance_spu2(21,[](unsigned,std::uint16_t,std::uint16_t){},
            [&](hg::Spu2Voices::Frame f){nonzero+=f.left!=0 && f.left==f.right;});
        CHECK(nonzero>0 && envx(s,core1,0)>0);
        CHECK((s.load(core1+0x340,2,false)&1)==0);
        for(int n=0;n<40;++n)s.advance_spu2(21,[](unsigned,std::uint16_t,std::uint16_t){});
        CHECK((s.load(core1+0x340,2,false)&1)==1);                     // Passed the end block.
        CHECK(envx(s,core1,0)>0);                                       // Repeat keeps sounding.
        CHECK(s.load(core1+0x1c4,2,false)==0 && s.load(core1+0x1c6,2,false)==0x3000); // LSAX.
        s.store(core1+0x1a4,2,1);                                       // KOF voice 0.
        for(int n=0;n<200;++n)s.advance_spu2(21,[](unsigned,std::uint16_t,std::uint16_t){});
        CHECK(envx(s,core1,0)==0);
        s.store(core1+0x1a0,2,1);s.advance_spu2(21,[](unsigned,std::uint16_t,std::uint16_t){});
        CHECK((s.load(core1+0x340,2,false)&1)==0);                     // Key-on clears ENDX.
    }
    // One-shot (end + mute): ENDX set and ENVX forced to zero at the end block.
    {
        hg::IopState s;
        s.store(core1+0x19a,2,0xc000);
        voice_reg(s,core1,3,4,0x1000);voice_reg(s,core1,3,6,0x000f);voice_reg(s,core1,3,8,0x1fc0);
        block(s,0x4000,0x01,0x3);block(s,0x4008,0x00,0x0);
        voice_addr(s,core1,3,0x4000);s.store(core1+0x1a0,2,1u<<3);
        for(int n=0;n<10;++n)s.advance_spu2(21,[](unsigned,std::uint16_t,std::uint16_t){});
        CHECK(envx(s,core1,3)>0);
        for(int n=0;n<40;++n)s.advance_spu2(21,[](unsigned,std::uint16_t,std::uint16_t){});
        CHECK((s.load(core1+0x340,2,false)&(1u<<3))!=0 && envx(s,core1,3)==0);
    }
    // Reverb: a wet-only voice reaches the output only through the effect return,
    // delayed by the comb tap; the work area is written only with ATTR bit 7.
    for(const bool enabled:{true,false}) {
        hg::IopState s;
        s.store(core1+0x19a,2,enabled?0xc080:0xc000);
        s.store(core1+0x198,2,0x0300);                                  // MSNDEL/ER only (no dry).
        s.store(core1+0x18c,2,1);s.store(core1+0x194,2,1);            // Voice 0 wet L/R.
        s.store(0x1f900788u,2,0x3fff);s.store(0x1f90078au,2,0x3fff);  // MVOL.
        s.store(0x1f90078cu,2,0x7fff);s.store(0x1f90078eu,2,0x7fff);  // EVOL.
        s.store(core1+0x2e0,2,0x000f);s.store(core1+0x2e2,2,0x0000);  // ESA 0xF0000.
        s.store(core1+0x33c,2,0x000f);                                  // EEA 0xFFFFF.
        auto pair=[&](unsigned index,std::uint32_t value){s.store(core1+0x2e4+index*4,2,value>>16);s.store(core1+0x2e6+index*4,2,value&0xffff);};
        pair(2,0x100);pair(3,0x100);pair(10,0x100);pair(11,0x100);   // SAME/DIFF written at +0x100;
        pair(4,0);pair(5,0);pair(18,0x200);pair(19,0x200);pair(20,0x200);pair(21,0x200); // comb reads +0: 0x100-step delay.
        auto coef=[&](unsigned index,std::uint16_t value){s.store(0x1f90079cu+index*2,2,value);};
        coef(0,0x7fff);coef(1,0x7fff);coef(8,0x7fff);coef(9,0x7fff);   // vIIR, vCOMB1, vLIN, vRIN.
        voice_reg(s,core1,0,0,0x3fff);voice_reg(s,core1,0,2,0x3fff);voice_reg(s,core1,0,4,0x1000);
        voice_reg(s,core1,0,6,0x000f);voice_reg(s,core1,0,8,0x1fc0);
        block(s,0x3000,0x04|0x03,0x3);voice_addr(s,core1,0,0x3000);
        s.store(core1+0x1a0,2,1);
        int early=0,late=0;unsigned tick=0;
        for(int n=0;n<4000;++n)s.advance_spu2(21,[](unsigned,std::uint16_t,std::uint16_t){},
            [&](hg::Spu2Voices::Frame f){(tick++<100?early:late)+=f.left!=0;});
        bool written=false;
        for(std::size_t b=0xf0000u*2;b<0x100000u*2;++b)written=written||s.spu2_ram[b]!=0;
        CHECK(early==0);                       // No dry path; the tap delay is 0x100 half-rate steps.
        CHECK(written==enabled);
        CHECK(enabled?late>0:late==0);         // Disabled: the buffer stays silent.
    }
    // Voice IRQ address hits with ATTR IRQ enabled fail explicitly.
    {
        hg::IopState s;
        s.store(core1+0x19a,2,0xc040);s.store(core1+0x19c,2,0);s.store(core1+0x19e,2,0x5004);
        voice_reg(s,core1,0,4,0x1000);block(s,0x5000,0x03,0x1);voice_addr(s,core1,0,0x5000);
        s.store(core1+0x1a0,2,1);
        bool fault=false;try{s.advance_spu2(21,[](unsigned,std::uint16_t,std::uint16_t){});}catch(const hg::IopFault&){fault=true;}
        CHECK(fault);
    }
    std::cout<<"SPU2 voice synthesis checks passed\n";
    return 0;
}
