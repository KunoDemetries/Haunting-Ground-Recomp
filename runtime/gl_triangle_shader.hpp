#pragma once
// Independent integer port of our GS triangle path. The host proves the
// coefficient and address bounds before submitting a job (HG-DIAG-018).
inline constexpr const char* hg_triangle_shader=R"GLSL(#version 430 core
#extension GL_ARB_gpu_shader_int64 : require
layout(local_size_x=16,local_size_y=16) in;
layout(std430,binding=0) buffer Vram {uint mem[];};
layout(std430,binding=1) readonly buffer Jobs {uint data[];};
layout(std430,binding=2) readonly buffer Tiles {uint bins[];};
uint j;
uint d(uint p){return data[j+p];}
uint64_t wide(uint p){return uint64_t(d(p))|(uint64_t(d(p+1))<<32);}
int64_t signed_wide(uint p){return int64_t(wide(p));}
int64_t edge(ivec2 a,ivec2 b,ivec2 p){return int64_t(b.x-a.x)*int64_t(p.y-a.y)-int64_t(b.y-a.y)*int64_t(p.x-a.x);}
bool covered(int64_t w,ivec2 a,ivec2 b){return w>0||(w==0&&(b.y<a.y||(b.y==a.y&&b.x>a.x)));}
uint byte_at(uint c,uint s){return (c>>s)&255u;}
uint address32(uint base,uint width,uint x,uint y){
    uint bx=(x>>3)&7u,by=(y>>3)&3u;
    uint block=(bx&1u)+(bx&2u)*2u+(bx&4u)*4u+(by&1u)*2u+(by&2u)*4u;
    return (base+((x>>6)+(y>>5)*(width>>6))*2048u+block*64u+((y&7u)>>1)*16u+(x&1u)+(x&6u)*2u+(y&1u)*2u)&0xfffffu;
}
uint wrap_coord(int c,uint size,uint mode,uint lo,uint hi){
    if(mode==0)return uint(c)&(size-1u);
    if(mode==1)return uint(clamp(c,0,int(size)-1));
    if(mode==2)return uint(clamp(c,int(lo),int(hi)));
    return (uint(c)&lo)|hi;
}
uint sample_texel(int sx,int sy){
    uint cl=d(25),ch=d(26);
    uint x=wrap_coord(sx,d(23),cl&3u,(cl>>4)&1023u,(cl>>14)&1023u);
    uint y=wrap_coord(sy,d(24),(cl>>2)&3u,((cl>>24)|(ch<<8))&1023u,(ch>>2)&1023u);
    uint texel;
    if(d(22)==19u){
        uint bx=(x>>4)&7u,by=(y>>4)&3u,column=(y&15u)>>2;
        uint block=(bx&1u)+(bx&2u)*2u+(bx&4u)*4u+(by&1u)*2u+(by&2u)*4u;
        uint row=(y&3u)^((column&1u)*2u);
        uint pixel=((x&1u)+(x&6u)*2u+(row&1u)*2u)^((row&2u)*4u);
        uint address=(d(20)+((x>>7)+(y>>6)*(d(21)>>7))*2048u+block*64u+column*16u+pixel)&0xfffffu;
        texel=(mem[address]>>((((y&2u)>>1)|((x&8u)>>2))*8u))&255u;
    }else if(d(22)==20u){
        // PSMT4: same layout as GsRegisterState::psmt4_word; CSA applied host-side.
        uint bx=(x>>5)&3u,by=(y>>4)&7u,column=(y&15u)>>2;
        uint block=(bx&1u)*2u+(bx&2u)*4u+(by&1u)+(by&2u)*2u+(by&4u)*4u;
        uint row=(y&3u)^((column&1u)*2u);
        uint pixel=((x&1u)+(x&6u)*2u+(row&1u)*2u)^((row&2u)*4u);
        uint address=(d(20)+((x>>7)+(y>>7)*(d(21)>>7))*2048u+block*64u+column*16u+pixel)&0xfffffu;
        texel=(mem[address]>>((((y&2u)>>1)|((x&24u)>>2))*4u))&15u;
    }else{
        texel=mem[address32(d(20),d(21),x,y)];
        if(d(22)==0)return texel;
        texel>>=24;
    }
    return data[d(56)+texel];
}
int ratio(uint p,i64vec3 w,int64_t q){
    int64_t n=signed_wide(p)*w.x+signed_wide(p+2)*w.y+signed_wide(p+4)*w.z;
    int64_t result=n/q;
    if(n<0&&n%q!=0)--result;
    return int(result);
}
float native_coefficient(uint p){return uintBitsToFloat(d(p));}
int native_ratio(uint p,vec3 w,float q){
    float n=native_coefficient(p)*w.x+native_coefficient(p+2)*w.y+native_coefficient(p+4)*w.z;
    return int(floor(n/q));
}
uint texture_function(uint texel,uint fragment);
uint texture_color(int x,int y,uint fragment){
    uint texel;
    if((d(15)&2u)==0)texel=sample_texel(x,y);
    else{
        int64_t xx=int64_t(x)-8,yy=int64_t(y)-8;
        int u=int(xx>=0?xx/16:-((-xx+15)/16)),v=int(yy>=0?yy/16:-((-yy+15)/16));
        uint a=uint(xx-int64_t(u)*16),b=uint(yy-int64_t(v)*16);
        uint c0=sample_texel(u,v),c1=a!=0?sample_texel(u+1,v):0u,c2=b!=0?sample_texel(u,v+1):0u,c3=a!=0&&b!=0?sample_texel(u+1,v+1):0u;
        uint w0=(16u-a)*(16u-b),w1=a*(16u-b),w2=(16u-a)*b,w3=a*b;
        uint rb=(c0&0x00ff00ffu)*w0+(c1&0x00ff00ffu)*w1+(c2&0x00ff00ffu)*w2+(c3&0x00ff00ffu)*w3;
        uint ga=((c0>>8)&0x00ff00ffu)*w0+((c1>>8)&0x00ff00ffu)*w1+((c2>>8)&0x00ff00ffu)*w2+((c3>>8)&0x00ff00ffu)*w3;
        texel=((rb>>8)&0x00ff00ffu)|(ga&0xff00ff00u);
    }
    return texture_function(texel,fragment);
}
uint texture_function(uint texel,uint fragment){
    uint fa=fragment>>24,ta=texel>>24,f=d(28),result=0;
    for(uint s=0;s<24;s+=8){
        uint product=min((byte_at(texel,s)*byte_at(fragment,s))>>7,255u);
        result|=(f==1?byte_at(texel,s):f>=2?min(product+fa,255u):product)<<s;
    }
    uint alpha=(d(15)&4u)==0?fa:f==0?min((ta*fa)>>7,255u):(f==1||f==3)?ta:min(ta+fa,255u);
    return result|(alpha<<24);
}
int color(uint selector,uint src,uint dst,uint shift){return selector==0?int(byte_at(src,shift)):selector==1?int(byte_at(dst,shift)):0;}
void main(){
    uint tile=gl_WorkGroupID.y*gl_NumWorkGroups.x+gl_WorkGroupID.x;
    uint begin=bins[tile*2],count=bins[tile*2+1];uvec2 pixel=gl_GlobalInvocationID.xy;
    for(uint k=0;k<count;++k){
        j=bins[begin+k];
        if(pixel.x<d(6)||pixel.y<d(7)||pixel.x>=d(8)||pixel.y>=d(9))continue;
        ivec2 a=ivec2(int(d(0)),int(d(1))),b=ivec2(int(d(2)),int(d(3))),c=ivec2(int(d(4)),int(d(5))),p=ivec2(pixel)*16;
        i64vec3 w=i64vec3(edge(b,c,p),edge(c,a,p),edge(a,b,p));
        if(!covered(w.x,b,c)||!covered(w.y,c,a)||!covered(w.z,a,b))continue;
        uint64_t area=wide(36);uint fragment=d(29);
        if((d(15)&1u)!=0){
            fragment=0;
            for(uint s=0;s<32;s+=8)fragment|=uint((uint64_t(w.x)*byte_at(d(30),s)+uint64_t(w.y)*byte_at(d(31),s)+uint64_t(w.z)*byte_at(d(32),s))/area)<<s;
        }
        uint src=fragment;
        // Job flag512: TME is off, so no texture state or texture memory is read.
        if((d(15)&512u)==0){
            if((d(15)&2048u)!=0){
                // HG-PERF-120: own-pixel PSMT8H feedback; earlier jobs for this pixel already ran.
                src=texture_function(data[d(56)+(mem[address32(d(10),d(11),pixel.x,pixel.y)]>>24)],fragment);
            }else if((d(15)&1024u)!=0){
                vec3 wf=vec3(float(w.x),float(w.y),float(w.z));
                float q=native_coefficient(50)*wf.x+native_coefficient(52)*wf.y+native_coefficient(54)*wf.z;
                src=texture_color(native_ratio(38,wf,q),native_ratio(44,wf,q),fragment);
            }else{
                int64_t q=signed_wide(50)*w.x+signed_wide(52)*w.y+signed_wide(54)*w.z;
                src=texture_color(ratio(38,w,q),ratio(44,w,q),fragment);
            }
        }
        // HG-PERF-116: all eight GS ATST comparisons, AFAIL=KEEP (host-checked).
        if((d(15)&256u)!=0){
            uint a=src>>24,r=d(17),m=d(27);
            bool pass=m==1u||(m==2u&&a<r)||(m==3u&&a<=r)||(m==4u&&a==r)||(m==5u&&a>=r)||(m==6u&&a>r)||(m==7u&&a!=r);
            if(!pass)continue;
        }
        uint z=uint((uint64_t(w.x)*d(33)+uint64_t(w.y)*d(34)+uint64_t(w.z)*d(35))/area);
        uint za=address32(d(12),d(11),pixel.x,pixel.y)^1536u;
        uint oldz=mem[za];uint storedz=oldz&d(13);
        if((d(16)==2&&z<storedz)||(d(16)==3&&z<=storedz))continue;
        uint fa=address32(d(10),d(11),pixel.x,pixel.y),dst=mem[fa],result=0;
        bool blend=(d(15)&8u)!=0&&((d(15)&64u)==0||(src&0x80000000u)!=0);
        for(uint s=0;s<24;s+=8){
            int channel=int(byte_at(src,s));
            if(blend){uint sel=(d(18)>>4)&3u;int amount=int(sel==0?src>>24:sel==1?dst>>24:d(19));
                int product=(color(d(18)&3u,src,dst,s)-color((d(18)>>2)&3u,src,dst,s))*amount;
                channel=(product>=0?product/128:-((-product+127)/128))+color((d(18)>>6)&3u,src,dst,s);}
            result|=((d(15)&16u)!=0?uint(clamp(channel,0,255)):uint(channel)&255u)<<s;
        }
        result|=(src|((d(15)&32u)!=0?0x80000000u:0u))&0xff000000u;
        mem[fa]=(dst&d(14))|(result&~d(14));
        if((d(15)&128u)!=0)mem[za]=(oldz&~d(13))|(z&d(13));
    }
})GLSL";
