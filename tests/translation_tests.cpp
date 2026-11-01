#include "hg/runtime.hpp"
#include "hg/image.hpp"
#include <iostream>
#define CHECK(x) do { if (!(x)) { std::cerr << "Failed line " << __LINE__ << ": " << #x << '\n'; return 1; } } while (0)
int main() {
    // Static scalar access covers every encoded destination and source alias.
    // Poison zero-register storage so accidental direct reads/writes are visible.
    for(unsigned destination=0;destination<32;++destination) {
        hg::State scalar;
        for(unsigned n=0;n<32;++n)scalar.gpr[n]={0xfffffff07fffffffULL+n,0x1234567800000000ULL+n};
        auto expected=scalar.gpr;
        scalar.pc=0x20000+destination*0x20;
        auto read=[&](unsigned n){return n?expected[n].lo:std::uint64_t(0);};
        for(unsigned step=0;step<6;++step) {
            const auto before=read(destination);
            std::uint64_t value=before;
            if(step==0)value=before+read(31);
            if(step==1)value=hg::sx32(std::uint32_t(before-1));
            if(step==2)value=0x8001;
            if(step==3)value=hg::sx32(std::uint32_t(before)<<3);
            if(destination)expected[destination].lo=value;
            hg::run(scalar,1);
            CHECK(scalar.pc==0x20000+destination*0x20+(step+1)*4);
            for(unsigned n=0;n<32;++n)CHECK(scalar.gpr[n].lo==expected[n].lo && scalar.gpr[n].hi==expected[n].hi);
        }
    }
    {
        hg::State zero;zero.gpr[0]={123,456};
        for(const auto entry:{0x1b000u,0x1b010u,0x1b020u}) {
            zero.pc=entry;zero.w(1,0);zero.w(4,1);
            bool fault=false;
            try{hg::run(zero,1);}catch(const hg::Fault& e){fault=e.pc==(entry==0x1b000u?entry:entry+4);}
            CHECK(fault && zero.gpr[0].lo==123 && zero.gpr[0].hi==456);
        }
        zero.pc=0x1b000;zero.w(4,0x12000000); // GS privileged words require a 64-bit access, even when discarded.
        bool fault=false;
        try{hg::run(zero,1);}catch(const hg::Fault& e){fault=e.pc==0x1b000;}
        CHECK(fault);
        zero.pc=0x1b000;zero.w(4,0x100);zero.store(0x100,4,0x87654321);hg::run(zero,1);
        CHECK(zero.pc==0x1b004 && zero.gpr[0].lo==123 && zero.gpr[0].hi==456);
        zero.pc=0x1b020;zero.w(1,1);zero.w(4,1);hg::run(zero,1);
        CHECK(zero.pc==0x1b028); // Annulled invalid load must remain unexecuted.
        zero.pc=0x1b004;zero.w(4,0x4000);hg::run(zero,1);
        CHECK(zero.pc==0x4000 && zero.r(5)==7 && zero.gpr[0].lo==123 && zero.gpr[0].hi==456);
    }
    // Every budget cut through static loops, J/JAL and likely branches must
    // match individual checked dispatches, including delay slots and tracing.
    for(unsigned budget=0;budget<64;++budget)for(bool burst:{false,true}) {
        hg::State batched,stepped;batched.pc=stepped.pc=0x1a000;
        batched.w(1,3);stepped.w(1,3);
        batched.add_trace_watch(0x1a000);stepped.add_trace_watch(0x1a000);
        if(burst)hg::run_burst(batched,budget);else hg::run(batched,budget);
        for(unsigned n=0;n<budget;++n)hg::run(stepped,1);
        CHECK(batched.pc==stepped.pc && batched.last_transfer_pc==stepped.last_transfer_pc);
        CHECK(batched.pc_trace_cursor==stepped.pc_trace_cursor);
        for(unsigned n=0;n<batched.pc_trace.size();++n)CHECK(batched.pc_trace[n].pc==stepped.pc_trace[n].pc);
        CHECK(batched.trace_watches[0].cursor==stepped.trace_watches[0].cursor);
        for(unsigned n=0;n<32;++n)CHECK(batched.gpr[n].lo==stepped.gpr[n].lo && batched.gpr[n].hi==stepped.gpr[n].hi);
    }
    hg::State s;
    s.pc=0x19000;s.w(4,0x1000b000);s.w(2,1);s.dmac.channels[3].chcr=0x100;
    hg::run_burst(s,100);CHECK(s.pc==0x19000 && s.r(2)==1 && s.dmac.channels[3].chcr==0x100);
    s.dmac.channels[3].chcr=0;hg::run_burst(s,4);CHECK(s.pc==0x19014 && s.r(2)==0);
    s.pc=0x19000;s.w(2,1);s.w(4,0x400);s.store(0x400,4,0x100);
    CHECK(s.add_trace_watch(0x18000));
    const auto dma_ram_trace=s.pc_trace_cursor;hg::run_burst(s,4);
    CHECK(s.pc==0x19000 && s.pc_trace_cursor==dma_ram_trace+4);
    s.w(4,0x1000b000);s.dmac.channels[3].chcr=0x100;CHECK(s.add_trace_watch(0x19004));
    const auto dma_watch_trace=s.pc_trace_cursor;hg::run_burst(s,4);
    CHECK(s.pc==0x19000 && s.pc_trace_cursor==dma_watch_trace+4);s.trace_watches.clear();s.dmac.channels[3].chcr=0;
    s.pc=0x18000;s.w(31,0xff003000);hg::run_burst(s,100);
    CHECK(s.pc==0xff003000 && s.r(6)==9);
    s.pc=0x18000;s.w(31,0x4000);s.w(1,10);hg::run_burst(s,2);
    CHECK(s.pc==0x4004 && s.r(2)==11);
    s.pc=0x17000;s.w(3,0);s.w(4,0x400);s.w(5,10);s.ram[0x400]=0;
    hg::run_burst(s,100);CHECK(s.pc==0x17000 && s.r(3)==0 && s.r(5)==10);
    s.ram[0x400]=1;hg::run_burst(s,3);
    CHECK(s.pc==0x17010 && s.r(3)==1 && s.r(5)==10);
    hg::run_burst(s,1);CHECK(s.r(5)==11);
    s.pc=0x17000;s.w(3,0);s.ram[0x400]=0;
    CHECK(s.add_trace_watch(0x17004));
    const auto before_poll_trace=s.pc_trace_cursor;
    hg::run_burst(s,3);CHECK(s.pc==0x17000 && s.pc_trace_cursor==before_poll_trace+3);
    s.trace_watches.clear();
    // Dispatch partitions must preserve budgets, cross-page delay slots and faults.
    s.pc=0x10ffc;s.w(1,1);s.w(2,1);s.w(3,0);s.w(4,0);
    hg::run(s,1);CHECK(s.pc==0x12000 && s.r(3)==5 && s.r(4)==0);
    hg::run(s,1);CHECK(s.pc==0x12004 && s.r(4)==7);
    s.pc=0x12ffc;s.w(1,0);s.w(2,0);
    hg::run(s,2);CHECK(s.pc==0x13004 && s.r(1)==1 && s.r(2)==2);
    s.pc=0x14ffc;s.w(1,1);s.w(2,2);
    hg::run(s,1);CHECK(s.pc==0x15004);
    for(const auto missing:{0x12004u,0x99000u}) {
        s.pc=missing;bool missing_fault=false;
        try{hg::run(s,1);}catch(const hg::Fault& f){missing_fault=f.pc==missing;}
        CHECK(missing_fault);
    }
    s.pc = 0x1000; s.w(1, 7); s.w(2, 7);
    hg::run(s, 1); CHECK(s.pc == 0x1010 && s.r(1) == 9);
    hg::run(s, 1); CHECK(s.r(3) == 22);
    s.pc = 0x1000; s.w(1, 6); s.w(2, 7);
    hg::run(s, 1); CHECK(s.pc == 0x1008 && s.r(1) == 9);
    s.pc = 0x2000; s.w(1, 6); s.w(2, 7); s.w(3, 5);
    hg::run(s, 1); CHECK(s.pc == 0x2008 && s.r(3) == 5);
    s.pc = 0x3000; s.w(4, 0x1010);
    hg::run(s, 1); CHECK(s.pc == 0x1010 && s.r(4) == 0 && s.r(31) == 0x3008);
    s.pc = 0x4000; s.w(1, 0x7fffffff); s.gpr[2].hi = 123;
    hg::run(s, 1); CHECK(s.r(2) == 0xffffffff80000000ull && s.gpr[2].hi == 123);
    s.pc = 0x5000; s.w(4, 0x100);
    hg::run(s, 2); CHECK(s.r(3) == 0xffffffff80000000ull);
    s.pc = 0x6000; s.w(4, 1);
    bool faulted = false;
    try { hg::run(s, 1); } catch (const hg::Fault& f) { faulted = f.pc == 0x6004; }
    CHECK(faulted);
    s.pc = 0x7000; s.w(4, 0x1010);
    hg::run(s, 1); CHECK(s.pc == 0x1010 && s.r(4) == 0x7008);
    s.pc = 0x8000; s.w(1, 6); s.w(2, 7);
    hg::run(s, 1); CHECK(s.pc == 0x8008);
    s.pc = 0x9000; s.gpr[1] = {0xffffffff, 5}; s.gpr[2] = {2, 6};
    hg::run(s, 5);
    CHECK(s.r(3) == 0xffffffff && s.gpr[3].hi == 11);
    CHECK(s.hi == 0xffffffff && s.lo1 == 0xffffffff);
    CHECK(s.sa == 32 && s.fpr[7] == 0xffffffff);
    s.pc=0xa000; s.w(1,64); s.w(2,0x8000000000000001ull);
    hg::run(s,3);
    CHECK(s.r(3)==0x8000000000000001ull && s.r(4)==~0ull && s.r(5)==0x100000000ull);
    s.pc=0xb000; s.w(1,~0ull); s.w(6,7); hg::run(s,1);
    CHECK(s.pc==0xb008 && s.r(6)==7);
    s.pc=0xc000; s.w(1,44); s.w(2,0); s.w(3,55); s.w(4,66); s.gpr[3].hi=77;
    hg::run(s,2); CHECK(s.r(3)==44 && s.r(4)==66 && s.gpr[3].hi==77);
    s.pc=0xd000; faulted=false;
    try {hg::run(s,1);} catch(const hg::Fault&) {faulted=true;}
    CHECK(faulted);
    s.store(0xd000,4,0x24050009); hg::run(s,1); CHECK(s.r(5)==9);
    s.pc=0xd000; s.store(0xd000,4,0); faulted=false;
    try {hg::run(s,1);} catch(const hg::Fault&) {faulted=true;}
    CHECK(faulted);
    s.pc=0xe000; s.w(1,0x80001000); s.cp0_status=6; s.w(5,11);
    hg::run(s,3);
    CHECK(s.r(2)==0xffffffff80001000ull && s.pc==0x80001000 && s.cp0_status==2 && s.r(5)==11);
    s.pc=0xe008; s.cp0_epc=0x1000; hg::run(s,1);
    CHECK(s.pc==0x1000 && s.cp0_status==0 && s.r(5)==11);
    s.pc=0xf000; s.w(1,~0ull); s.w(2,2); hg::run(s,1);
    CHECK(s.lo==0x7fffffff && s.hi==1);
    s.pc=0xf004; s.w(1,hg::sx32(0xfffffff9)); s.w(2,3); hg::run(s,1);
    CHECK(s.lo==hg::sx32(0xfffffffe) && s.hi==~0ull);
    for(const auto& operands: {std::array<int,4>{7,3,2,1}, {-7,3,-2,-1},
                              {7,-3,-2,1}, {-7,-3,2,-1},
                              {std::numeric_limits<int>::min(),-1,std::numeric_limits<int>::min(),0}}) {
        s.pc=0xf008;s.w(1,hg::sx32(std::uint32_t(operands[0])));s.w(2,hg::sx32(std::uint32_t(operands[1])));
        hg::run(s,1);
        CHECK(s.lo1==hg::sx32(std::uint32_t(operands[2])) && s.hi1==hg::sx32(std::uint32_t(operands[3])));
        CHECK(s.lo==hg::sx32(0xfffffffe) && s.hi==~0ull);
    }
    s.pc=0xf00c;s.w(1,~0ull);s.w(2,1);hg::run(s,1);
    CHECK(s.lo1==~0ull && s.hi1==0 && s.lo==hg::sx32(0xfffffffe) && s.hi==~0ull);
    s.pc=0xf00c;s.w(2,2);hg::run(s,1);CHECK(s.lo1==0x7fffffff && s.hi1==1);
    for(const auto& operands: {std::array<std::uint64_t,2>{7,0}, {0x100000007ull,3}, {7,0x100000003ull}}) {
        s.pc=0xf008;s.w(1,operands[0]);s.w(2,operands[1]);faulted=false;
        try {hg::run(s,1);} catch(const hg::Fault&) {faulted=true;}
        CHECK(faulted && s.lo1==0x7fffffff && s.hi1==1);
        CHECK(s.lo==hg::sx32(0xfffffffe) && s.hi==~0ull);
    }
    s.pc=0xf200; s.w(1,1); s.w(2,1); s.lo=~0ull; s.hi=0; hg::run(s,1);
    CHECK(s.lo==0 && s.hi==1 && s.r(1)==0);
    s.w(1,1); s.lo1=~0ull; s.hi1=~0ull; hg::run(s,1);
    CHECK(s.lo1==0 && s.hi1==0 && s.r(3)==0 && s.hi==1);
    s.pc=0xf300; s.lo=0xdeadbeef12345678ull; s.hi=0xffeeddccabcdef01ull;
    s.lo1=0xaabbccdd01020304ull; s.hi1=0x4433221180808080ull; hg::run(s,1);
    CHECK(s.gpr[3].lo==0xabcdef0112345678ull && s.gpr[3].hi==0x8080808001020304ull);
    s.pc=0xf400; s.w(1,0x100); s.store(0x104,4,0xff800001); hg::run(s,2);
    CHECK(s.fpr[2]==0xff800001 && s.load(0x108,4,false)==0xff800001);
    s.pc=0xf500; s.gpr[1]={0,0x807fff0000000000ull}; s.gpr[2]={1,0x0101ff0100000000ull};
    hg::run(s,1); CHECK(s.gpr[1].lo==255 && s.gpr[1].hi==0x7f7e00ff00000000ull);
    hg::run(s,1); CHECK(s.gpr[2].lo==~255ull && s.gpr[2].hi==~0x7f7e00ff00000000ull);
    hg::run(s,1); CHECK(s.gpr[3].lo==0 && s.gpr[3].hi==0);
    s.pc=0xf004; s.w(1,hg::sx32(0x80000000)); s.w(2,~0ull); hg::run(s,1);
    CHECK(s.lo==hg::sx32(0x80000000) && s.hi==0);
    s.pc=0xf000; s.w(2,0); faulted=false;
    try {hg::run(s,1);} catch(const hg::Fault&) {faulted=true;}
    CHECK(faulted && s.lo==hg::sx32(0x80000000) && s.hi==0);
    s.pc=0xf100; s.w(1,~0ull); s.w(2,2); s.gpr[1].hi=77; hg::run(s,1);
    CHECK(s.lo==hg::sx32(0xfffffffe) && s.hi==~0ull && s.r(1)==s.lo && s.gpr[1].hi==77);
    hg::run(s,1); CHECK(s.lo1==hg::sx32(0xfffffffc) && s.hi1==1 && s.r(3)==s.lo1);
    CHECK(s.lo==hg::sx32(0xfffffffe) && s.hi==~0ull);
    s.pc=0xf600;s.w(1,0x80001001);s.store(0x1040,4,0x12345678);
    const auto cache_epoch=s.boot.data_cache_epoch;hg::run(s,1);
    CHECK(s.pc==0xf604 && s.boot.data_cache_epoch==cache_epoch+1 && s.load(0x1040,4,false)==0x12345678);
    s.pc=0xf600;s.w(1,0xa0001000);faulted=false;
    try{hg::run(s,1);}catch(const hg::Fault&){faulted=true;}CHECK(faulted);
    CHECK(s.boot.data_cache_epoch==cache_epoch+1);
    s.pc=0xf600;s.w(1,0x20001001);hg::run(s,1);
    CHECK(s.boot.data_cache_epoch==cache_epoch+2 && s.load(0x1040,4,false)==0x12345678);
    s.pc=0xf700;s.hi1=0x8877665544332211ull;s.lo1=0x1020304050607080ull;
    s.gpr[1]={0x0000000200000001ull,0x8000000000000000ull};s.gpr[2]={0xffffffff00000003ull,0x0000000100000001ull};hg::run(s,3);
    CHECK(s.r(4)==0x8877665544332211ull && s.r(5)==0x1020304050607080ull);
    CHECK(s.gpr[6].lo==0x00000003fffffffeull && s.gpr[6].hi==0x7fffffffffffffffull);
    s.pc=0xf724;s.gpr[1]={0x0001000200030004ull,0x0005000600070008ull};s.gpr[2]={0x0002000100040003ull,0x0006000500080007ull};hg::run(s,1);
    CHECK(s.gpr[9].lo==0xffff0001ffff0001ull && s.gpr[9].hi==0xffff0001ffff0001ull);
    s.pc=0xf728;s.gpr[1]={0x7f807f807f807f80ull,0x7f807f807f807f80ull};s.gpr[2]={0xff01ff01ff01ff01ull,0xff01ff01ff01ff01ull};hg::run(s,1);
    CHECK(s.gpr[10].lo==0x7f807f807f807f80ull && s.gpr[10].hi==0x7f807f807f807f80ull);
    s.pc=0xf72c;s.gpr[1]={0x7fff80007fff8000ull,0x7fff80007fff8000ull};s.gpr[2]={0xffff0001ffff0001ull,0xffff0001ffff0001ull};hg::run(s,1);
    CHECK(s.gpr[11].lo==0x7fff80007fff8000ull && s.gpr[11].hi==0x7fff80007fff8000ull);
    s.pc=0xf730;s.gpr[1]={0x7fffffff80000000ull,0x7fffffff80000000ull};s.gpr[2]={0xffffffff00000001ull,0xffffffff00000001ull};hg::run(s,1);
    CHECK(s.gpr[12].lo==0x7fffffff80000000ull && s.gpr[12].hi==0x7fffffff80000000ull);
    s.pc=0xf734;s.gpr[1]={0xffff0001ffff0001ull,0xffff0001ffff0001ull};s.gpr[2]={0x0001ffff0001ffffull,0x0001ffff0001ffffull};hg::run(s,1);
    CHECK(s.gpr[13].lo==0xffffffffffffffffull && s.gpr[13].hi==0xffffffffffffffffull);
    s.pc=0xf73c;s.gpr[1]={0x7f807f807f807f80ull,0x7f807f807f807f80ull};s.gpr[2]={0x01ff01ff01ff01ffull,0x01ff01ff01ff01ffull};hg::run(s,1);
    CHECK(s.gpr[14].lo==0x7f807f807f807f80ull && s.gpr[14].hi==0x7f807f807f807f80ull);
    s.pc=0xf748;s.vu0.vf[7]={0x1111111122222222ull,0x3333333344444444ull};s.vu0.vf[9]={0xaaaaaaaabbbbbbbbull,0xccccccccddddddddull};hg::run(s,1);
    CHECK(s.vu0.vf[9].lo==0xaaaaaaaa22222222ull && s.vu0.vf[9].hi==0xcccccccc44444444ull);
    s.pc=0xf74c;s.vu0.vf[10]={0xaaaaaaaabbbbbbbbull,0xccccccccddddddddull};hg::run(s,1);
    CHECK(s.vu0.vf[10].lo==0x4444444411111111ull && s.vu0.vf[10].hi==0x2222222233333333ull);
    s.pc=0xf750;s.vu0.vf[7]={0x80000001bf800000ull,0x40000000ffc00000ull};s.vu0.vf[9]={0xaaaaaaaabbbbbbbbull,0xccccccccddddddddull};hg::run(s,1);
    CHECK(s.vu0.vf[9].lo==0xaaaaaaaa3f800000ull && s.vu0.vf[9].hi==0xcccccccc7fc00000ull);
    s.pc=0xf740;s.gpr[1]={0x7fff80007fff8000ull,0x7fff80007fff8000ull};s.gpr[2]={0x0001ffff0001ffffull,0x0001ffff0001ffffull};hg::run(s,1);
    CHECK(s.gpr[15].lo==0x7fff80007fff8000ull && s.gpr[15].hi==0x7fff80007fff8000ull);
    s.pc=0xf744;s.gpr[1]={0x7fffffff80000000ull,0x7fffffff80000000ull};s.gpr[2]={0x00000001ffffffffull,0x00000001ffffffffull};hg::run(s,1);
    CHECK(s.gpr[16].lo==0x7fffffff80000000ull && s.gpr[16].hi==0x7fffffff80000000ull);
    s.pc=0xf70c;s.gpr[1]={0xff01020304050607ull,0x08090a0b0c0d0e0full};s.gpr[2]={0x0102030405060708ull,0xfffefdfcfbfaf9f8ull};hg::run(s,1);
    CHECK(s.gpr[7].lo==0xff030507090b0d0full && s.gpr[7].hi==0xffffffffffffffffull);
    s.pc=0xf710;s.w(1,7);s.w(2,9);hg::run(s,1);CHECK(s.r(8)==hg::sx32(0xfffffffe));
    s.pc=0xf710;s.w(1,0x7fffffff);s.w(2,hg::sx32(0xffffffff));faulted=false;
    try {hg::run(s,1);}catch(const hg::Fault&){faulted=true;}CHECK(faulted && s.r(8)==hg::sx32(0xfffffffe));
    s.pc=0xf714;s.vu0.vf[28]={0x0123456789abcdefull,0xfedcba9876543210ull};hg::run(s,1);
    CHECK(s.gpr[4].lo==0x0123456789abcdefull && s.gpr[4].hi==0xfedcba9876543210ull);
    s.pc=0xf718;s.gpr[4]={0x1020304050607080ull,0x90a0b0c0d0e0f000ull};hg::run(s,1);
    CHECK(s.vu0.vf[27].lo==0x1020304050607080ull && s.vu0.vf[27].hi==0x90a0b0c0d0e0f000ull);
    s.pc=0xf71c;s.w(6,0x101);s.vu0.vf[16]={0x1122334455667788ull,0x99aabbccddeeff00ull};hg::run(s,1);
    CHECK(s.load(0x100,8,false)==0x1122334455667788ull && s.load(0x108,8,false)==0x99aabbccddeeff00ull);
    s.pc=0xf720;s.vu0.vf[17]={};hg::run(s,1);
    CHECK(s.vu0.vf[17].lo==0x1122334455667788ull && s.vu0.vf[17].hi==0x99aabbccddeeff00ull);
    s.pc=0xf760;s.gpr[1]={0xffffffffffffffffull,0x0123456789abcdefull};
    const auto pref_gpr=s.gpr;const auto pref_hi=s.hi;const auto pref_lo=s.lo;
    const auto pref_hi1=s.hi1;const auto pref_lo1=s.lo1;
    const auto pref_cache_epoch=s.boot.data_cache_epoch;
    s.store(0x180,4,0x5aa55aa5);hg::run(s,4);
    CHECK(s.pc==0xf770 && s.boot.data_cache_epoch==pref_cache_epoch);
    CHECK(s.hi==pref_hi && s.lo==pref_lo && s.hi1==pref_hi1 && s.lo1==pref_lo1);
    for(unsigned n=0;n<32;++n)CHECK(s.gpr[n].lo==pref_gpr[n].lo && s.gpr[n].hi==pref_gpr[n].hi);
    CHECK(s.load(0x180,4,false)==0x5aa55aa5);
    s.pc=0xf770;s.gpr[1]={0xffffffffffffffffull,0x0123456789abcdefull};s.cp0_tag_lo=0xa5a55a5a;
    const auto dxin_gpr=s.gpr;const auto dxin_hi=s.hi;const auto dxin_lo=s.lo;
    const auto dxin_hi1=s.hi1;const auto dxin_lo1=s.lo1;const auto dxin_cache_epoch=s.boot.data_cache_epoch;
    s.store(0x184,4,0xa55aa55a);hg::run(s,2);
    CHECK(s.pc==0xf778 && s.cp0_tag_lo==0xa5a55a5a && s.boot.data_cache_epoch==dxin_cache_epoch);
    CHECK(s.hi==dxin_hi && s.lo==dxin_lo && s.hi1==dxin_hi1 && s.lo1==dxin_lo1);
    for(unsigned n=0;n<32;++n)CHECK(s.gpr[n].lo==dxin_gpr[n].lo && s.gpr[n].hi==dxin_gpr[n].hi);
    CHECK(s.load(0x184,4,false)==0xa55aa55a);
    s.pc=0xf800;s.fpr[1]=0x40600000;s.fpr[2]=0x3fa00000;hg::run(s,1);CHECK(s.fpr[3]==0x40100000);
    s.pc=0xf804;s.fpr[2]=0x42a00000;s.fpr[3]=0xbf000000;s.fpu.acc=0xc1800000;hg::run(s,1);CHECK(s.fpr[7]==0xc2600000);
    s.pc=0xf808;s.fpr[1]=0x42a00000;s.fpr[2]=0xbf000000;hg::run(s,1);CHECK(s.fpr[4]==0xc2200000);
    s.pc=0xf80c;s.fpr[1]=0x42a00000;s.fpr[2]=0x3f000000;hg::run(s,1);CHECK(s.fpr[5]==0x43200000);
    s.pc=0xf810;s.fpr[1]=123;hg::run(s,1);CHECK(s.fpr[6]==0x42f60000);
    s.pc=0xf814;s.fpr[1]=0xc1234567;hg::run(s,1);CHECK(s.fpr[7]==0xc1234567);
    s.pc=0xf818;s.fpr[1]=0x41200000;hg::run(s,1);CHECK(s.fpr[8]==0xc1200000);
    s.pc=0xf81c;s.fpr[1]=0xc0200000;hg::run(s,1);CHECK(s.fpr[9]==0xfffffffe);
    s.pc=0xf820;s.fpr[1]=0x42a00000;s.fpr[2]=0xbf000000;s.fpu.acc=0xc1800000;hg::run(s,1);CHECK(s.fpu.acc==0xc2600000);
    s.pc=0xf824;s.fpr[1]=0x42a00000;s.fpr[2]=0xbf000000;s.fpu.acc=0xc1800000;hg::run(s,1);CHECK(s.fpr[10]==0x41c00000);
    s.pc=0xf828;s.fpr[1]=0x80000000;s.fpr[2]=0;hg::run(s,1);CHECK((s.fpu.control&0x00800000u)!=0);
    s.pc=0xf82c;hg::run(s,1);CHECK(s.pc==0xf834);
    s.pc=0xf838;s.fpr[4]=0x41100000;hg::run(s,1);CHECK(s.fpr[11]==0x40400000);
    s.pc=0xe010;s.w(1,7);hg::run(s,2);CHECK(s.cp0_wired==7 && s.r(2)==7);
    s.pc=0xe018;s.cp0_tag_lo=0x81234567;hg::run(s,1);CHECK(s.r(2)==hg::sx32(0x81234567));
    s.pc=0xf900;s.w(1,24);s.gpr[2].hi=0x12345678;hg::run(s,1);CHECK(s.r(2)==20 && s.gpr[2].hi==0x12345678);
    s.pc=0xf900;s.w(1,hg::sx32(0x80000000));faulted=false;try{hg::run(s,1);}catch(const hg::Fault&){faulted=true;}CHECK(faulted && s.r(2)==20);
    s.pc=0xf904;s.w(1,0x7fffffff);faulted=false;try{hg::run(s,1);}catch(const hg::Fault&){faulted=true;}CHECK(faulted && s.r(2)==20);
    s.pc=0xf904;s.w(1,0x80000000);faulted=false;try{hg::run(s,1);}catch(const hg::Fault&){faulted=true;}CHECK(faulted && s.r(2)==20);
    s.pc=0xf904;s.w(1,hg::sx32(0xffffffff));hg::run(s,1);CHECK(s.r(2)==0);
    const hg::Register ma{0x80007fff0000ffffull,0x1234fedc80010001ull},mb{0x7fff8000ffff0000ull,0x4321abcd80000002ull};
    s.pc=0xf908;s.gpr[1]=ma;s.gpr[2]=mb;hg::run(s,1);CHECK(s.gpr[1].lo==0x7fff7fff00000000ull && s.gpr[1].hi==0x4321fedc80010002ull);
    s.pc=0xf90c;s.gpr[1]=ma;s.gpr[2]=mb;hg::run(s,1);CHECK(s.gpr[2].lo==0x80008000ffffffffull && s.gpr[2].hi==0x1234abcd80000001ull);
    s.pc=0xf910;s.gpr[1]={0xaa01bb02cc03dd04ull,0xee05ff0600071108ull};s.gpr[2]={0xaa11bb12cc13dd14ull,0xee15ff1600171118ull};hg::run(s,1);
    CHECK(s.gpr[1].lo==0x1516171811121314ull && s.gpr[1].hi==0x0506070801020304ull);
    s.pc=0xf914;s.gpr[1]={0xffff80007fff0001ull,0x1234abcdffff0000ull};s.gpr[2]={0x000180000001ffffull,0xedcc543300010001ull};hg::run(s,1);
    CHECK(s.gpr[1].lo==0x0000000080000000ull && s.gpr[1].hi==1);
    for(unsigned offset=0;offset<16;++offset) {
        s.pc=0xf918;s.sa=offset*8;
        s.gpr[2]={0x0706050403020100ull,0x0f0e0d0c0b0a0908ull};s.gpr[1]={0x1716151413121110ull,0x1f1e1d1c1b1a1918ull};hg::run(s,1);
        for(unsigned n=0;n<16;++n)CHECK(((n<8?s.gpr[1].lo:s.gpr[1].hi)>>((n%8)*8)&255)==n+offset);
    }
    const hg::Register exa{0x0706050403020100ull,0x0f0e0d0c0b0a0908ull},exb{0x1716151413121110ull,0x1f1e1d1c1b1a1918ull};
    s.pc=0xf91c;s.gpr[1]=exa;s.gpr[2]=exb;hg::run(s,1);CHECK(s.gpr[1].lo==0x0313021201110010ull && s.gpr[1].hi==0x0717061605150414ull);
    s.pc=0xf920;s.gpr[1]=exa;s.gpr[2]=exb;hg::run(s,1);CHECK(s.gpr[2].lo==0x0b1b0a1a09190818ull && s.gpr[2].hi==0x0f1f0e1e0d1d0c1cull);
    s.pc=0xf924;s.gpr[1]=ma;s.gpr[2]=mb;hg::run(s,1);CHECK(s.gpr[1].lo==0x0000ffffffff0000ull && s.gpr[1].hi==0x0000ffffffff0000ull);
    s.pc=0xf928;s.gpr[1]={0xffff80007fff0001ull,0xffff80007fff0001ull};hg::run(s,1);CHECK(s.gpr[1].lo==0xfffe0000fffe0002ull && s.gpr[1].hi==s.gpr[1].lo);
    s.pc=0xf92c;s.gpr[1]={0xffff80007fff0001ull,0xffff80007fff0001ull};hg::run(s,1);CHECK(s.gpr[1].lo==0x0001000100000000ull && s.gpr[1].hi==s.gpr[1].lo);
    s.pc=0xf930;s.gpr[1]={0xffff80007fff0001ull,0xffff80007fff0001ull};hg::run(s,1);CHECK(s.gpr[1].lo==0xffffffff00000000ull && s.gpr[1].hi==s.gpr[1].lo);
    s.pc=0x21000;s.w(4,0x4000);
    s.vu0.vf[1]={0x400000003f800000ull,0x4100000040800000ull}; // 1,2,4,8
    s.vu0.vf[2]={0x4040000040000000ull,0x40a0000040800000ull}; // 2,3,4,5
    s.vu0.acc={};hg::run(s,0);CHECK(s.pc==0x21000 && s.vu0.acc[0]==0);
    hg::run(s,1);CHECK(s.pc==0x21004 && s.vu0.acc[0]==0x40000000 && s.vu0.acc[3]==0x41800000);
    hg::run(s,2);CHECK(s.pc==0x2100c && s.vu0.acc[0]==0x41100000 && s.vu0.acc[3]==0x42900000);
    hg::run(s,1);CHECK(s.pc==0x4000);
    CHECK(s.vu0.vf[2].lo==0x41e0000041600000ull && s.vu0.vf[2].hi==0x42e0000042600000ull); // 14,28,56,112
    CHECK(s.vu0.acc[0]==0x41100000 && s.vu0.acc[3]==0x42900000); // Final MADD preserves ACC.
    s.pc=0x2100c;s.vu0.vf[1].lo=0x008000003f800000ull;
    s.vu0.vf[2].hi=0x3f00000040800000ull; // Underflow in the delay slot's second lane.
    s.vu0.status=0x30;s.vu0.mac=0xffff;
    const auto supported_acc=s.vu0.acc;
    hg::run(s,1);CHECK(s.pc==0x4000 && s.vu0.acc==supported_acc);
    CHECK(std::uint32_t(s.vu0.vf[2].lo>>32)==supported_acc[1]);
    CHECK(s.vu0.mac==0 && s.vu0.status==0x170); // Product Z/U are sticky; final sums are normal.
    // The same translated delay slot supports documented product underflow
    // with zero ACC: the final sum is +0 and supplies current Z.
    s.pc=0x2100c;s.vu0.acc[1]=0;s.vu0.vf[2].hi=0x3f00000040800000ull;
    hg::run(s,1);CHECK(s.pc==0x4000);
    CHECK(std::uint32_t(s.vu0.vf[2].lo>>32)==0);
    CHECK(s.vu0.mac==4 && s.vu0.status==0x171);
    s.pc=0x22000;s.gpr[0]={0xabcdef1234567890ull,0xfedcba0987654321ull};s.w(4,0x4000);
    s.gpr[1]={0x2222222211111111ull,0x4444444433333333ull};
    s.gpr[2]={0x6666666655555555ull,0x8888888877777777ull};
    hg::run(s,0);CHECK(s.pc==0x22000 && s.gpr[1].lo==0x2222222211111111ull);
    hg::run(s,1);CHECK(s.pc==0x22004);
    CHECK(s.gpr[1].lo==0x1111111155555555ull && s.gpr[1].hi==0x2222222266666666ull);
    hg::run(s,1);CHECK(s.pc==0x22008);
    CHECK(s.gpr[2].lo==0x6666666677777777ull && s.gpr[2].hi==0x2222222288888888ull);
    hg::run(s,1);CHECK(s.pc==0x4000 && s.gpr[4].lo==0x77777777 && s.gpr[4].hi==0x66666666);
    CHECK(s.gpr[0].lo==0xabcdef1234567890ull && s.gpr[0].hi==0xfedcba0987654321ull);
    s.pc=0x23000;s.w(4,0x4000);
    s.vu0.vf[1]={0xbfc000003fc00000ull,0xbf4000003f400000ull};
    s.vu0.status=0xabc;s.vu0.mac=0x5678;
    hg::run(s,0);CHECK(s.pc==0x23000);
    hg::run(s,1);CHECK(s.pc==0x23004);
    CHECK(s.vu0.vf[2].lo==0xffffffe800000018ull && s.vu0.vf[2].hi==0xfffffff40000000cull);
    hg::run(s,1);CHECK(s.pc==0x4000);
    CHECK(s.vu0.vf[2].lo==s.vu0.vf[1].lo && s.vu0.vf[2].hi==s.vu0.vf[1].hi);
    CHECK(s.vu0.status==0xabc && s.vu0.mac==0x5678);
    s.pc = 0xdeadbeec;
    faulted = false;
    try { hg::run(s, 1); } catch (const hg::Fault&) { faulted = true; }
    CHECK(faulted);
    return 0;
}
