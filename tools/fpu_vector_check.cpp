// Checks hg::Fpu against hardware-captured EE FPU results: the `.expected` files of
// unknownbrackets/ps2autotests (ISC; captured by running the tests on a PS2). The
// files are read from a directory given on the command line and are not kept in this
// repository. Named CF_ operands are substituted with the suite's constant values. Output: per operation, checked/mismatch counts and the first mismatches.
//
//   fpu_vector_check <dir containing arithmetic.expected muldiv.expected sqrt.expected>
#include "hg/fpu.hpp"
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace {
// Named operands used by the vector files (values from the test suite's shared.h).
std::string substitute(std::string line) {
    static const std::pair<const char*,const char*> names[]={
        {"CF_MAX_MANTISSA","3fffffff/"},{"CF_MAX_EXP","7f800001/"},{"CF_MIN_EXP","00000001/"},
        {"CF_NEGZERO","80000000/"},{"CF_NEGONE","bf800000/"},{"CF_GARBAGE1","00001337/"},{"CF_GARBAGE2","deadbeef/"},
        {"CF_ZERO","00000000/"},{"CF_ONE","3f800000/"},{"CF_MAX","7fffffff/"},{"CF_MIN","ffffffff/"}};
    for(const auto& [name,value]:names)
        for(std::size_t at;(at=line.find(name))!=std::string::npos;)line.replace(at,std::string(name).size(),value);
    return line;
}
// Pulls every "xxxxxxxx/" hex word from a line, in order.
std::vector<std::uint32_t> words(const std::string& raw,bool& named) {
    const auto line=substitute(raw);
    std::vector<std::uint32_t> out;named=line.find("CF_")!=std::string::npos;
    for(std::size_t i=0;i+9<=line.size();++i) {
        if(line[i+8]!='/')continue;
        bool hex=true;for(int k=0;k<8;++k)hex=hex&&std::isxdigit(static_cast<unsigned char>(line[i+k]));
        if(!hex||(i&&std::isxdigit(static_cast<unsigned char>(line[i-1]))))continue;
        out.push_back(std::uint32_t(std::stoul(line.substr(i,8),nullptr,16)));i+=8;
    }
    return out;
}
struct Tally {int checked=0,mismatched=0;std::vector<std::string> examples;};
}

int main(int argc,char** argv) {
    if(argc<2){std::cerr<<"usage: fpu_vector_check <dir>\n";return 2;}
    std::map<std::string,Tally> tally;
    for(const char* name:{"arithmetic","muldiv","sqrt"}) {
        std::ifstream in(std::string(argv[1])+"/"+name+".expected");
        if(!in){std::cerr<<"missing "<<name<<".expected\n";return 2;}
        std::string line;
        while(std::getline(in,line)) {
            if(line.size()<3||line[0]!=' ')continue;
            std::istringstream head(line);std::string op;head>>op;
            bool named=false;const auto w=words(line,named);
            if(named)continue;
            hg::Fpu f;std::uint32_t got=0,want=0;bool known=true;
            if((op=="add"||op=="sub"||op=="mul"||op=="div")&&w.size()==3) {
                want=w[2];
                if(op=="add")got=f.add(w[0],w[1]);
                else if(op=="sub")got=f.add(w[0],w[1]^0x80000000u);
                else if(op=="mul")got=f.multiply(w[0],w[1]);
                else got=f.divide(w[0],w[1],true);
            } else if((op=="madd"||op=="msub")&&w.size()==5) {   // (acc) fs, ft: result (acc after)
                f.acc=w[0];want=w[3];got=op=="madd"?f.madd(w[1],w[2]):f.msub(w[1],w[2]);
            } else if(op=="sqrt"&&w.size()==2) {
                want=w[1];
                try{got=f.sqrt_exact(w[0]);}catch(const std::exception&){known=false;}
            } else known=false;
            if(!known)continue;
            auto& t=tally[op];++t.checked;
            if(got!=want) {
                ++t.mismatched;
                if(t.examples.size()<6){char b[96];std::snprintf(b,sizeof b,"   got %08x  hw %08x",got,want);t.examples.push_back(line+b);}
            }
        }
    }
    if(argc>2) {   // tools/hwtest/fpu_capture.c RESULTS.BIN from a real PS2 (12 words per case)
        std::ifstream in(argv[2],std::ios::binary);
        in.seekg(0,std::ios::end);const auto bytes=std::size_t(in.tellg());in.seekg(0);
        std::vector<std::uint32_t> w(bytes/4);
        in.read(reinterpret_cast<char*>(w.data()),std::streamsize(w.size()*4));
        for(std::size_t i=0;i+12<=w.size();i+=12) {
            const auto* c=&w[i];
            const auto check=[&](const char* op,std::uint32_t got,std::uint32_t want){
                auto& t=tally[std::string("hw ")+op];++t.checked;
                if(got!=want){++t.mismatched;if(t.examples.size()<4){char b[96];std::snprintf(b,sizeof b,"  %08x %08x: got %08x hw %08x",c[0],c[1],got,want);t.examples.push_back(b);}}
            };
            hg::Fpu f;
            check("add",f.add(c[0],c[8]),c[9]);
            check("sub",f.add(c[0],c[8]^0x80000000u),c[10]);
            f.acc=c[2];check("madd(hw product)",f.add(f.acc,c[3]),c[6]);
            check("mul",f.multiply(c[0],c[1]),c[3]);
            check("div",f.divide(c[0],c[1],true),c[5]);
            check("sqrt",f.sqrt_exact(c[0]&0x7fffffffu),c[11]);
        }
    }
    if(argc>3) {   // tools/hwtest/vu0_capture.c VU0.BIN (16 words per case); a PCSX2 dry run checks HG-DIAG-126
        std::ifstream in(argv[3],std::ios::binary);
        in.seekg(0,std::ios::end);const auto bytes=std::size_t(in.tellg());in.seekg(0);
        std::vector<std::uint32_t> w(bytes/4);
        in.read(reinterpret_cast<char*>(w.data()),std::streamsize(w.size()*4));
        for(std::size_t i=0;i+16<=w.size();i+=16) {
            const auto* c=&w[i];const auto b=c[1]&0x7fffffffu;
            const auto check=[&](const char* op,std::uint32_t got,std::uint32_t want){
                auto& t=tally[std::string("vu0 ")+op];++t.checked;
                if(got!=want){++t.mismatched;if(t.examples.size()<4){char s[96];std::snprintf(s,sizeof s,"  %08x %08x: got %08x file %08x",c[0],b,got,want);t.examples.push_back(s);}}
            };
            hg::Fpu f;
            check("add",f.add(c[0],b),c[3]);
            check("sub",f.add(c[0],b^0x80000000u),c[4]);
            check("madd(file product)",f.add(c[2],c[6]),c[5]);
            check("add exact-chop (HG-DIAG-126)",f.add_exact_chop(c[0],b),c[3]);
            check("sub exact-chop (HG-DIAG-126)",f.add_exact_chop(c[0],b^0x80000000u),c[4]);
            check("madd exact-chop (HG-DIAG-126)",f.add_exact_chop(c[2],c[6]),c[5]);
        }
    }
    for(const auto& [op,t]:tally) {
        std::cout<<op<<": "<<t.checked<<" checked, "<<t.mismatched<<" mismatched\n";
        for(const auto& e:t.examples)std::cout<<e<<'\n';
    }
    return 0;
}
