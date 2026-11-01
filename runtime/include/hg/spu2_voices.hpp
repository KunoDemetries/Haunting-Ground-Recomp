#pragma once
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <vector>

namespace hg {
// SPU2 voice synthesis and core mixing (48 kHz, one step per Ts).
//
// Sources (docs/SOURCES.md, "SPU2 voice synthesis"):
// - Sony SPU2 Overview Manual v6.0: voice pipeline, PITCH (0x1000 = source
//   rate), ADSR1/ADSR2 and VOL field layouts, ENVX/VOLX/ENDX/NAX/LSAX semantics,
//   KON/KOF, VMIX/MMIX routing, sound-data input areas.
// - psx-spx (nocash) SPU chapter: SPU-ADPCM block/flag format and filter
//   tables, pitch counter and 4-point Gaussian table (hardware constants),
//   envelope step algorithm, noise generator, sweep volume. SPU2 voices keep the
//   PS1 SPU voice hardware at 48 kHz instead of 44.1 kHz.
// Register offsets were confirmed against the original game's own writes
// (HG-DIAG-118 trace). Internal mixing order is renderer-style host output;
// the guest-visible results are ENVX, VOLXL/R, NAXH/L, LSAXH/L and ENDX0/1.
// Reverb: psx-spx "SPU Reverb Formula" network at half rate (24 kHz) with the
// documented 39-tap resampling FIR, work area ESA..EEA in local memory; SPU2
// address registers are the PS1 list as 22 (H,L) pairs at 0x2E4..0x33A and the
// ten coefficients follow MVOLX at 0x774 (order confirmed by the game's PS1
// "Hall" preset). Left and right are computed in the same half-rate step.
// Not modeled (HG-DIAG-119): the sound-data output areas. Voice IRQ address
// hits with ATTR IRQ enabled fail explicitly.
class Spu2Voices {
public:
    static constexpr unsigned voices_per_core=24;
    using Registers=std::array<std::uint16_t,0x400>;
    struct Frame {std::int16_t left=0,right=0;};

    // Register-write hooks (offset relative to 0x1f900000).
    void register_written(std::uint32_t offset,std::uint16_t value) {
        const unsigned core=offset>=0x400&&offset<0x760?1:0;
        const auto r=offset-core*0x400;
        if(offset>=0x760)return;
        auto& c=cores_[core];
        if(r==0x2e0||r==0x2e2)c.reverb.reset=true; // Writing ESA restarts the buffer address.
        if(r==0x1a0)c.pending_kon|=value;
        else if(r==0x1a2)c.pending_kon|=std::uint32_t(value&0xffu)<<16;
        else if(r==0x1a4)c.pending_kof|=value;
        else if(r==0x1a6)c.pending_kof|=std::uint32_t(value&0xffu)<<16;
        else if(r<0x180 && (r&0xf)==0xa) { // ENVX: software may set the level.
            c.voice[r>>4].level=std::int16_t(value);
        } else if(r>=0x1c0 && r<0x1c0+voices_per_core*0xc && ((r-0x1c0)%0xc==4 || (r-0x1c0)%0xc==6)) {
            auto& v=c.voice[(r-0x1c0)/0xc];
            if(v.since_key_on>=4)v.loop_written=true; // Manual: ignored within 4 Ts of key-on.
        }
    }
    void input_sample(unsigned core,std::uint16_t left,std::uint16_t right) {
        auto& q=cores_.at(core).input;
        if(q.count==q.frames.size())throw std::runtime_error("SPU2 sound-data input queue overflow");
        q.frames[(q.head+q.count)%q.frames.size()]={std::int16_t(left),std::int16_t(right)};++q.count;
    }
    void input_reset(unsigned core){cores_.at(core).input={};}

    // Advance guest time; emit one final (core 1) frame per Ts.
    template<class Emit>
    void advance(std::uint32_t microseconds,Registers& regs,std::vector<std::uint8_t>& ram,Emit emit) {
        phase_+=std::uint64_t(microseconds)*48000u;
        while(phase_>=1000000u) {phase_-=1000000u;emit(step(regs,ram));}
    }
    Frame step(Registers& regs,std::vector<std::uint8_t>& ram) {
        Frame external{};
        for(unsigned core=0;core<2;++core)external=step_core(core,regs,ram,external);
        ++ticks_;
        if(voice_stats_enabled() && ticks_%48000u==0) {
            for(unsigned core=0;core<2;++core)for(unsigned n=0;n<voices_per_core;++n) {
                const auto& v=cores_[core].voice[n];
                if(!v.started || (!v.level && !voice_peak_[core][n]))continue;
                std::fprintf(stderr,"HG-DIAG-121 t=%llus c%u v%02u phase=%u level=%d peak=%d addr=%05x loop=%05x endx=%u vol=%d/%d adsr=%04x/%04x\n",
                    static_cast<unsigned long long>(ticks_/48000u),core,n,unsigned(v.phase),int(v.level),int(voice_peak_[core][n]),
                    unsigned(v.address),unsigned(v.loop_address),unsigned((cores_[core].endx>>n)&1u),int(v.volume[0].level),int(v.volume[1].level),
                    unsigned(reg(regs,core,n*0x10u+6)),unsigned(reg(regs,core,n*0x10u+8)));
                voice_peak_[core][n]=0;
            }
        }
        return external;
    }
    std::uint64_t ticks() const noexcept {return ticks_;}
    std::uint64_t input_underruns() const noexcept {return input_underruns_;}

    // Exposed for tests: one ADPCM block (16 bytes) into 28 samples.
    static void decode_block(const std::uint8_t* block,std::int32_t& old,std::int32_t& older,std::int16_t* out) {
        static constexpr std::array<std::int32_t,5> pos{{0,60,115,98,122}};
        static constexpr std::array<std::int32_t,5> neg{{0,0,-52,-55,-60}};
        auto shift=block[0]&0xfu;
        const auto filter=(block[0]>>4)&0x7u;
        if(filter>4)throw std::runtime_error("SPU2 ADPCM filter 5..7 requires validated hardware behavior");
        if(shift>12)shift=9; // psx-spx: reserved shifts 13..15 act as 9.
        for(unsigned n=0;n<28;++n) {
            const auto nibble=(block[2+n/2]>>((n&1)*4))&0xfu;
            const std::int32_t t=std::int32_t(std::int16_t(std::uint16_t(nibble<<12)))>>shift;
            std::int32_t s=t+((old*pos[filter]+older*neg[filter]+32)>>6);
            if(s>0x7fff)s=0x7fff;if(s<-0x8000)s=-0x8000;
            out[n]=std::int16_t(s);older=old;old=s;
        }
    }
    struct Rate {bool exponential=false,decrease=false,negative=false;unsigned shift=0,step=0;};
    // psx-spx "Envelope Operation"; one call per Ts. Returns true when a step applied.
    // Real-PS2 capture (tools/hwtest/spu2probe, SOURCES.md 2026-10-25): shift 1Fh never steps
    // for step values 1..3 (rates 7Dh..7Fh held 7FF7h for 2 s), shifts 1Dh/1Eh step once per
    // 8000h Ts. `floor` false lets a release go below zero for one Ts (ENVX read FFF7h).
    static bool envelope_step(std::int32_t& level,std::uint32_t& counter,const Rate& r,bool floor=true) {
        std::int32_t step=7-std::int32_t(r.step);
        if(r.decrease!=r.negative)step=~step;
        step=r.shift<11?step*(1<<(11-r.shift)):step;
        std::uint32_t increment=r.shift>11?0x8000u>>(r.shift-11):0x8000u;
        if(r.exponential && !r.decrease && level>0x6000) {
            if(r.shift<10)step/=4;
            else if(r.shift>=11)increment/=4;
            else {step/=2;increment/=2;}
        } else if(r.exponential && r.decrease) {
            // Step*Level/8000h as an arithmetic shift (rounds toward minus infinity). C
            // division would round the negative product toward zero, making the step 0 once
            // |step*level| < 8000h, so a release with step -8 froze at 0FFFh and the voice
            // never ended; the oracle (PCSX2) shows such levels continuing down to 0.
            step=std::int32_t((std::int64_t(step)*level)>>15);
        }
        if(r.shift!=0x1fu && increment==0)increment=1;
        counter+=increment;
        if(!(counter&0x8000u))return false;
        counter=0; // Step period = 0x8000/increment Ts.
        level+=step;
        if(!r.decrease){if(level>0x7fff)level=0x7fff;if(level<-0x8000)level=-0x8000;}
        else if(r.negative){if(level<-0x8000)level=-0x8000;if(level>0)level=0;}
        else if(floor && level<0)level=0;
        return true;
    }
private:
    enum class Phase:std::uint8_t {off,attack,decay,sustain,release};
    struct Sweep {std::int32_t level=0;std::uint32_t counter=0;};
    struct Voice {
        bool started=false,loop_written=false,block_valid=false;
        Phase phase=Phase::off;
        std::uint32_t address=0,loop_address=0,counter=0,since_key_on=0;
        std::int32_t level=0;std::uint32_t env_counter=0;
        std::int32_t old=0,older=0;
        std::array<std::int16_t,31> samples{}; // [0..2] carry from the previous block.
        std::uint8_t flags=0;
        std::array<Sweep,2> volume{};
        std::int16_t outx=0;
    };
    // Input frames produced earlier in the same advance call (up to ~170 ms per call).
    struct InputQueue {std::array<Frame,8192> frames{};unsigned head=0,count=0;};
    struct Core {
        std::array<Voice,voices_per_core> voice{};
        std::uint32_t pending_kon=0,pending_kof=0,endx=0;
        std::int32_t noise_timer=0;std::uint16_t noise_level=0;
        std::array<Sweep,2> master{};
        InputQueue input{};
        struct Reverb {
            bool reset=true,odd=false;std::uint32_t cursor=0;
            std::array<std::int16_t,39> in_l{},in_r{},up_l{},up_r{};unsigned pos=0;
        } reverb{};
    };
    std::array<Core,2> cores_{};
    std::uint64_t phase_=0,ticks_=0,input_underruns_=0;
    // HG-DIAG-121: optional per-voice activity report (HG_SPU2_VOICE_STATS=1), host-only.
    static bool voice_stats_enabled() {static const bool on=[]{const char* e=std::getenv("HG_SPU2_VOICE_STATS");return e&&*e=='1';}();return on;}
    std::array<std::array<std::int32_t,voices_per_core>,2> voice_peak_{};

    static std::uint16_t& reg(Registers& regs,unsigned core,std::uint32_t offset) {return regs[(core*0x400u+offset)/2];}
    static std::int32_t clamp16(std::int32_t v){return v>0x7fff?0x7fff:v<-0x8000?-0x8000:v;}
    static const std::array<std::int16_t,512>& gauss();

    // VOLL/VOLR/MVOL: direct mode or sweep (Overview pp42-43; psx-spx sweep).
    static void volume_step(Sweep& s,std::uint16_t value) {
        if(!(value&0x8000u)) {s.level=std::int32_t(std::int16_t(std::uint16_t(value<<1)));s.counter=0;return;}
        Rate r;r.exponential=(value&0x4000u)!=0;r.decrease=(value&0x2000u)!=0;r.negative=(value&0x1000u)!=0;
        r.shift=(value>>2)&0x1fu;r.step=value&3u;
        envelope_step(s.level,s.counter,r);
    }
    void load_block(Voice& v,const std::vector<std::uint8_t>& ram,unsigned core,const Registers& regs) {
        const auto byte=std::size_t(v.address&0xfffffu)*2;
        if(byte+16>ram.size())throw std::runtime_error("SPU2 voice address outside local memory");
        const auto attr=regs[(core*0x400u+0x19a)/2];
        if(attr&0x40u) {
            const auto irq=(std::uint32_t(regs[(core*0x400u+0x19c)/2]&0x3fu)<<16)|regs[(core*0x400u+0x19e)/2];
            if(irq>=(v.address&0xfffffu) && irq<(v.address&0xfffffu)+8)
                throw std::runtime_error("SPU2 voice IRQ address hit requires interrupt implementation");
        }
        v.samples[0]=v.samples[28];v.samples[1]=v.samples[29];v.samples[2]=v.samples[30];
        decode_block(&ram[byte],v.old,v.older,&v.samples[3]);
        v.flags=ram[byte+1];
        if((v.flags&4u) && !v.loop_written)v.loop_address=v.address;
        v.block_valid=true;
    }
    static constexpr std::array<std::int32_t,39> reverb_fir{{ // psx-spx resampling FIR.
        -0x0001,0,0x0002,0,-0x000a,0,0x0023,0,-0x0067,0,0x010a,0,-0x0268,0,0x0534,0,
        -0x0b90,0,0x2806,0x4000,0x2806,0,-0x0b90,0,0x0534,0,-0x0268,0,0x010a,0,-0x0067,0,
        0x0023,0,-0x000a,0,0x0002,0,-0x0001}};
    static std::int32_t fir(const std::array<std::int16_t,39>& ring,unsigned pos) {
        std::int64_t sum=0; // pos = index of the oldest sample.
        for(unsigned k=0;k<39;++k)sum+=std::int64_t(reverb_fir[k])*ring[(pos+k)%39];
        return std::int32_t(sum>>15);
    }
    // One 48 kHz reverb tick: resample the wet input, run the network every
    // other tick, and return the upsampled effect output (before EVOL).
    Frame reverb_tick(unsigned core,Registers& regs,std::vector<std::uint8_t>& ram,std::int32_t wet_l,std::int32_t wet_r) {
        auto& rv=cores_[core].reverb;
        const auto pair=[&](unsigned index){
            const auto o=0x2e4u+index*4u;
            return (std::uint32_t(reg(regs,core,o)&0x3fu)<<16)|reg(regs,core,o+2);
        };
        const std::uint32_t esa=(std::uint32_t(reg(regs,core,0x2e0)&0x3fu)<<16)|reg(regs,core,0x2e2);
        const std::uint32_t eea=(std::uint32_t(reg(regs,core,0x33c)&0x3fu)<<16)|0xffffu;
        if(rv.reset){rv.cursor=esa;rv.reset=false;}
        rv.in_l[rv.pos]=std::int16_t(clamp16(wet_l));rv.in_r[rv.pos]=std::int16_t(clamp16(wet_r));
        std::int16_t out_l=0,out_r=0;
        if(!rv.odd && eea>esa && eea<0x100000u) {
            const std::uint32_t size=eea-esa+1;
            if(rv.cursor<esa||rv.cursor>eea)rv.cursor=esa;
            const bool write_enabled=(reg(regs,core,0x19a)&0x80u)!=0;
            const auto at=[&](std::int64_t offset)->std::size_t{
                std::int64_t rel=(std::int64_t(rv.cursor-esa)+offset)%std::int64_t(size);
                if(rel<0)rel+=size;
                return std::size_t(esa+std::uint32_t(rel))*2;
            };
            const auto rd=[&](std::int64_t offset){const auto b=at(offset);return std::int32_t(std::int16_t(std::uint16_t(ram[b]|(ram[b+1]<<8))));};
            const auto wr=[&](std::int64_t offset,std::int32_t v){
                if(!write_enabled)return;
                const auto b=at(offset);const auto u=std::uint16_t(std::int16_t(clamp16(v)));
                ram[b]=std::uint8_t(u);ram[b+1]=std::uint8_t(u>>8);
            };
            const auto coef=[&](unsigned index){return std::int32_t(std::int16_t(regs[(0x774u+core*0x28u+index*2u)/2]));};
            const auto mul=[](std::int32_t a,std::int32_t b){return std::int32_t((std::int64_t(a)*b)>>15);};
            const std::int32_t vIIR=coef(0),vCOMB1=coef(1),vCOMB2=coef(2),vCOMB3=coef(3),vCOMB4=coef(4),
                               vWALL=coef(5),vAPF1=coef(6),vAPF2=coef(7),vLIN=coef(8),vRIN=coef(9);
            const std::int64_t dAPF1=pair(0),dAPF2=pair(1),mLSAME=pair(2),mRSAME=pair(3),mLCOMB1=pair(4),mRCOMB1=pair(5),
                mLCOMB2=pair(6),mRCOMB2=pair(7),dLSAME=pair(8),dRSAME=pair(9),mLDIFF=pair(10),mRDIFF=pair(11),
                mLCOMB3=pair(12),mRCOMB3=pair(13),mLCOMB4=pair(14),mRCOMB4=pair(15),dLDIFF=pair(16),dRDIFF=pair(17),
                mLAPF1=pair(18),mRAPF1=pair(19),mLAPF2=pair(20),mRAPF2=pair(21);
            const std::int32_t lin=mul(vLIN,clamp16(fir(rv.in_l,(rv.pos+1)%39))),rin=mul(vRIN,clamp16(fir(rv.in_r,(rv.pos+1)%39)));
            // Same-side and different-side reflections ([x-1] is one halfword back).
            const auto iir=[&](std::int64_t dst,std::int32_t input,std::int64_t src){
                const auto prev=rd(dst-1);
                wr(dst,mul(input+mul(rd(src),vWALL)-prev,vIIR)+prev);
            };
            iir(mLSAME,lin,dLSAME);iir(mRSAME,rin,dRSAME);
            iir(mLDIFF,lin,dRDIFF);iir(mRDIFF,rin,dLDIFF);
            std::int32_t l=mul(vCOMB1,rd(mLCOMB1))+mul(vCOMB2,rd(mLCOMB2))+mul(vCOMB3,rd(mLCOMB3))+mul(vCOMB4,rd(mLCOMB4));
            std::int32_t r=mul(vCOMB1,rd(mRCOMB1))+mul(vCOMB2,rd(mRCOMB2))+mul(vCOMB3,rd(mRCOMB3))+mul(vCOMB4,rd(mRCOMB4));
            const auto apf=[&](std::int32_t x,std::int64_t m,std::int64_t d,std::int32_t v){
                const auto delayed=rd(m-d);
                x=clamp16(x-mul(v,delayed));wr(m,x);
                return clamp16(mul(x,v)+delayed);
            };
            l=apf(l,mLAPF1,dAPF1,vAPF1);r=apf(r,mRAPF1,dAPF1,vAPF1);
            l=apf(l,mLAPF2,dAPF2,vAPF2);r=apf(r,mRAPF2,dAPF2,vAPF2);
            out_l=std::int16_t(clamp16(l));out_r=std::int16_t(clamp16(r));
            rv.cursor=rv.cursor+1>eea?esa:rv.cursor+1;
        }
        // Zero-stuffed upsampling through the same FIR (x2 gain).
        rv.up_l[rv.pos]=out_l;rv.up_r[rv.pos]=out_r;
        const auto oldest=(rv.pos+1)%39;
        Frame result{std::int16_t(clamp16(fir(rv.up_l,oldest)*2)),std::int16_t(clamp16(fir(rv.up_r,oldest)*2))};
        rv.pos=oldest;rv.odd=!rv.odd;
        return result;
    }
    Frame step_core(unsigned core,Registers& regs,std::vector<std::uint8_t>& ram,Frame external) {
        auto& c=cores_[core];
        const auto attr=reg(regs,core,0x19a);
        // Key on/off latched since the previous Ts.
        for(unsigned n=0;n<voices_per_core;++n) {
            auto& v=c.voice[n];const auto bit=1u<<n;
            if(c.pending_kon&bit) {
                const auto base=0x1c0u+n*0xcu;
                v.address=((std::uint32_t(reg(regs,core,base)&0x3fu)<<16)|reg(regs,core,base+2))&~7u;
                v.loop_address=v.address;v.loop_written=false;v.block_valid=false;v.started=true;
                v.counter=0;v.old=v.older=0;v.samples.fill(0);v.since_key_on=0;
                v.phase=Phase::attack;v.level=0;v.env_counter=0;
                c.endx&=~bit;
            } else if((c.pending_kof&bit) && v.phase!=Phase::off) {
                v.phase=Phase::release;v.env_counter=0;
            }
        }
        c.pending_kon=c.pending_kof=0;
        // Noise generator (ATTR 13..10 shift, 9..8 step).
        {
            const unsigned shift=(attr>>10)&0xfu,nstep=((attr>>8)&3u)+4u;
            const auto parity=((c.noise_level>>15)^(c.noise_level>>12)^(c.noise_level>>11)^(c.noise_level>>10)^1u)&1u;
            c.noise_timer-=std::int32_t(nstep);
            if(c.noise_timer<0) {
                c.noise_level=std::uint16_t(c.noise_level*2u+parity);
                c.noise_timer+=0x20000>>shift;
                if(c.noise_timer<0)c.noise_timer+=0x20000>>shift;
            }
        }
        const auto mask=[&](std::uint32_t lo){return std::uint32_t(reg(regs,core,lo))|(std::uint32_t(reg(regs,core,lo+2)&0xffu)<<16);};
        const auto pmon=mask(0x180),non=mask(0x184);
        const auto vmixl=mask(0x188),vmixel=mask(0x18c),vmixr=mask(0x190),vmixer=mask(0x194);
        std::int32_t dry_l=0,dry_r=0,wet_l=0,wet_r=0;
        std::int16_t previous_outx=0;
        for(unsigned n=0;n<voices_per_core;++n) {
            auto& v=c.voice[n];const auto bit=1u<<n;
            const auto base=n*0x10u;
            const auto this_previous=previous_outx;previous_outx=v.outx; // OUTX of voice n-1, 1 Ts before.
            if(!v.started){v.outx=0;continue;}
            if(!v.block_valid)load_block(v,ram,core,regs);
            const unsigned index=v.counter>>12,i=(v.counter>>4)&0xffu;
            std::int32_t sample;
            if(non&bit)sample=std::int16_t(c.noise_level);
            else {
                const auto& g=gauss();
                sample =(g[0x0ff-i]*std::int32_t(v.samples[index]))>>15;
                sample+=(g[0x1ff-i]*std::int32_t(v.samples[index+1]))>>15;
                sample+=(g[0x100+i]*std::int32_t(v.samples[index+2]))>>15;
                sample+=(g[0x000+i]*std::int32_t(v.samples[index+3]))>>15;
            }
            // Envelope.
            const auto adsr1=reg(regs,core,base+6),adsr2=reg(regs,core,base+8);
            switch(v.phase) {
            case Phase::attack: {
                Rate r;r.exponential=(adsr1&0x8000u)!=0;r.shift=(adsr1>>10)&0x1fu;r.step=(adsr1>>8)&3u;
                envelope_step(v.level,v.env_counter,r);
                if(v.level>=0x7fff){v.phase=Phase::decay;v.env_counter=0;}
                break;
            }
            case Phase::decay: {
                Rate r;r.exponential=true;r.decrease=true;r.shift=(adsr1>>4)&0xfu;r.step=0;
                // Hardware: the sustain check follows an applied step, so sustain level Fh
                // still takes one decay step (7FFFh -> 7FF7h at decay shift Fh).
                if(envelope_step(v.level,v.env_counter,r) && v.level<=std::int32_t(((adsr1&0xfu)+1u)*0x800u)){v.phase=Phase::sustain;v.env_counter=0;}
                break;
            }
            case Phase::sustain: {
                Rate r;r.exponential=(adsr2&0x8000u)!=0;r.decrease=(adsr2&0x4000u)!=0;r.shift=(adsr2>>8)&0x1fu;r.step=(adsr2>>6)&3u;
                envelope_step(v.level,v.env_counter,r);
                break;
            }
            case Phase::release: {
                Rate r;r.exponential=(adsr2&0x20u)!=0;r.decrease=true;r.shift=adsr2&0x1fu;r.step=0;
                // Hardware: a linear release may read negative for one Ts (3FF7h -> FFF7h -> 0).
                if(v.level<0){v.level=0;v.phase=Phase::off;break;}
                envelope_step(v.level,v.env_counter,r,false);
                if(v.level==0)v.phase=Phase::off;
                break;
            }
            case Phase::off:break;
            }
            v.outx=std::int16_t(clamp16((sample*v.level)>>15));
            if(voice_stats_enabled()) {const auto a=v.outx<0?-std::int32_t(v.outx):std::int32_t(v.outx);if(a>voice_peak_[core][n])voice_peak_[core][n]=a;}
            volume_step(v.volume[0],reg(regs,core,base+0));
            volume_step(v.volume[1],reg(regs,core,base+2));
            const auto l=(std::int32_t(v.outx)*v.volume[0].level)>>15,r=(std::int32_t(v.outx)*v.volume[1].level)>>15;
            if(vmixl&bit)dry_l+=l;if(vmixr&bit)dry_r+=r;
            if(vmixel&bit)wet_l+=l;if(vmixer&bit)wet_r+=r;
            // Pitch counter advance (PMON uses voice n-1's previous OUTX).
            std::uint32_t pitch=reg(regs,core,base+4);
            if(n && (pmon&bit)) {
                const auto factor=std::int32_t(this_previous)+0x8000;
                pitch=std::uint32_t((std::int32_t(std::int16_t(std::uint16_t(pitch)))*factor)>>15)&0xffffu;
            }
            if(pitch>0x3fffu)pitch=0x4000u;
            v.counter+=pitch;
            while((v.counter>>12)>=28) {
                v.counter-=28u<<12;
                if(v.flags&1u) {
                    c.endx|=bit;
                    v.address=v.loop_written?
                        (((std::uint32_t(reg(regs,core,0x1c0u+n*0xcu+4)&0x3fu)<<16)|reg(regs,core,0x1c0u+n*0xcu+6))&~7u):v.loop_address;
                    if(!(v.flags&2u)){v.phase=Phase::release;v.level=0;v.env_counter=0;}
                } else v.address=(v.address+8u)&0xfffffu;
                load_block(v,ram,core,regs);
            }
            if(v.since_key_on<4)++v.since_key_on;
            // Guest-visible voice state.
            reg(regs,core,base+0xa)=std::uint16_t(v.level);
            reg(regs,core,base+0xc)=std::uint16_t(v.volume[0].level);
            reg(regs,core,base+0xe)=std::uint16_t(v.volume[1].level);
            const auto next=v.address+1u+((v.counter>>12)>>2);
            reg(regs,core,0x1c0u+n*0xcu+8)=std::uint16_t((next>>16)&0x3fu);
            reg(regs,core,0x1c0u+n*0xcu+10)=std::uint16_t(next);
            if(!v.loop_written) {
                reg(regs,core,0x1c0u+n*0xcu+4)=std::uint16_t((v.loop_address>>16)&0x3fu);
                reg(regs,core,0x1c0u+n*0xcu+6)=std::uint16_t(v.loop_address);
            }
        }
        reg(regs,core,0x340)=std::uint16_t(c.endx);
        reg(regs,core,0x342)=std::uint16_t(c.endx>>16);
        // Sound-data input for this Ts.
        Frame in{};
        if(c.input.count){in=c.input.frames[c.input.head];c.input.head=(c.input.head+1)%c.input.frames.size();--c.input.count;}
        const auto vol=[&](std::uint32_t offset){return std::int32_t(std::int16_t(regs[(0x760u+core*0x28u+offset)/2]));};
        const auto mmix=reg(regs,core,0x198);
        std::int32_t out_l=0,out_r=0;
        if(mmix&0x800u)out_l+=dry_l;
        if(mmix&0x400u)out_r+=dry_r;
        if(mmix&0x080u)out_l+=(std::int32_t(in.left)*vol(0xc))>>15;
        if(mmix&0x040u)out_r+=(std::int32_t(in.right)*vol(0xe))>>15;
        if(mmix&0x008u)out_l+=(std::int32_t(external.left)*vol(0x8))>>15;
        if(mmix&0x004u)out_r+=(std::int32_t(external.right)*vol(0xa))>>15;
        // Effect path per MMIX: voice wet (MSNDEL/ER), sound-data input (MINEL/ER),
        // external input (SINEL/ER); return through EVOL.
        if(!(mmix&0x200u))wet_l=0;
        if(!(mmix&0x100u))wet_r=0;
        if(mmix&0x020u)wet_l+=(std::int32_t(in.left)*vol(0xc))>>15;
        if(mmix&0x010u)wet_r+=(std::int32_t(in.right)*vol(0xe))>>15;
        if(mmix&0x002u)wet_l+=(std::int32_t(external.left)*vol(0x8))>>15;
        if(mmix&0x001u)wet_r+=(std::int32_t(external.right)*vol(0xa))>>15;
        const auto effect=reverb_tick(core,regs,ram,wet_l,wet_r);
        out_l+=(std::int32_t(effect.left)*vol(0x4))>>15;
        out_r+=(std::int32_t(effect.right)*vol(0x6))>>15;
        volume_step(c.master[0],regs[(0x760u+core*0x28u)/2]);
        volume_step(c.master[1],regs[(0x760u+core*0x28u+2)/2]);
        regs[(0x760u+core*0x28u+0x10)/2]=std::uint16_t(c.master[0].level);
        regs[(0x760u+core*0x28u+0x12)/2]=std::uint16_t(c.master[1].level);
        Frame result{};
        if((attr&0xc000u)==0xc000u) { // Core enabled and unmuted (psx-spx SPUCNT bits 15/14).
            result.left=std::int16_t(clamp16((clamp16(out_l)*c.master[0].level)>>15));
            result.right=std::int16_t(clamp16((clamp16(out_r)*c.master[1].level)>>15));
        }
        return result;
    }
};

inline const std::array<std::int16_t,512>& Spu2Voices::gauss() {
    // psx-spx "4-Point Gaussian Interpolation" hardware table (gauss[000h..1FFh]).
    static constexpr std::array<std::int16_t,512> table{{
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    0,0,0,0,0,0,0,1,1,1,1,2,2,2,3,3,
    3,4,4,5,5,6,7,7,8,9,9,10,11,12,13,14,
    15,16,17,18,19,21,22,24,25,27,28,30,32,33,35,37,
    39,41,44,46,48,51,53,56,58,61,64,67,70,73,77,80,
    84,87,91,95,99,103,107,111,116,120,125,130,135,140,145,150,
    156,161,167,173,179,186,192,199,205,212,219,227,234,242,250,257,
    266,274,283,291,300,309,319,328,338,348,358,369,379,390,401,412,
    424,436,448,460,473,485,498,512,525,539,553,567,582,597,612,627,
    643,659,675,692,708,726,743,761,779,797,816,835,854,874,894,914,
    935,956,977,999,1020,1043,1066,1089,1112,1136,1160,1184,1209,1234,1260,1286,
    1312,1339,1366,1394,1422,1450,1479,1508,1537,1567,1598,1628,1660,1691,1723,1756,
    1789,1822,1856,1890,1924,1959,1995,2031,2067,2104,2141,2179,2217,2256,2295,2334,
    2374,2415,2456,2497,2539,2582,2624,2668,2712,2756,2801,2846,2892,2938,2985,3032,
    3079,3128,3176,3225,3275,3325,3376,3427,3479,3531,3584,3637,3691,3745,3799,3855,
    3910,3967,4023,4081,4138,4197,4255,4315,4374,4435,4495,4557,4619,4681,4744,4807,
    4871,4935,5000,5065,5131,5197,5264,5332,5399,5468,5536,5606,5676,5746,5817,5888,
    5959,6032,6104,6177,6251,6325,6400,6475,6550,6626,6702,6779,6856,6934,7012,7091,
    7170,7249,7329,7409,7490,7571,7653,7735,7817,7900,7983,8066,8150,8234,8319,8404,
    8489,8575,8661,8748,8834,8922,9009,9097,9185,9273,9362,9451,9541,9630,9720,9811,
    9901,9992,10083,10174,10266,10358,10450,10542,10635,10727,10820,10913,11007,11100,11194,11288,
    11382,11476,11571,11665,11760,11855,11950,12045,12140,12236,12331,12427,12522,12618,12714,12809,
    12905,13001,13097,13193,13289,13385,13481,13577,13673,13769,13865,13961,14056,14152,14248,14343,
    14439,14534,14630,14725,14820,14915,15010,15104,15199,15293,15387,15481,15575,15669,15762,15855,
    15948,16041,16133,16226,16317,16409,16500,16592,16682,16773,16863,16953,17042,17131,17220,17308,
    17396,17484,17571,17658,17744,17830,17916,18001,18086,18170,18254,18337,18420,18502,18584,18665,
    18746,18826,18905,18985,19063,19141,19219,19295,19372,19447,19522,19597,19671,19744,19816,19888,
    19959,20030,20100,20169,20238,20306,20373,20439,20505,20570,20634,20698,20760,20822,20884,20944,
    21004,21063,21121,21178,21235,21290,21345,21399,21452,21505,21556,21607,21657,21706,21754,21801,
    21848,21893,21938,21982,22025,22066,22107,22148,22187,22225,22262,22299,22334,22369,22402,22435,
    22467,22498,22527,22556,22584,22611,22637,22662,22686,22709,22731,22752,22772,22791,22809,22826,
    22842,22857,22872,22885,22897,22908,22918,22927,22935,22942,22948,22953,22957,22960,22962,22963
    }};
    return table;
}
}
