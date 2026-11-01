#pragma once
// Walkable-floor editing: cut an object's footprint out of a room's navigation mesh.
//
// Facts used (docs/WORLD_EDITOR.md, "Room layout slots"): slot 0 = u32 count, 12 bytes,
// then 0x50-byte triangles {3 x {x,y,z,1}} {3 x i32 neighbour, u32 runtime} {3 x f32 cost,
// u32 attribute}; neighbour k is across the edge v[k] -> v[k+1] and every link has a back
// link (all 218,536 links on the disc); cost k = distance between the two centroids.
// Slot 16 holds one flag byte per triangle, slot 1 the border-edge list, slot 7 portals
// with triangle lists, slot 14 switchable triangle groups.
//
// Existing triangle numbers never change (other data and possibly event code refer to
// them): a split keeps the original number for one half and appends the other half;
// triangles inside the footprint are disconnected from their neighbours (made
// unreachable) instead of deleted. New pieces inherit their parent's attribute and flag
// byte and are added wherever the parent is listed (portal lists, switchable groups).
#include "edit.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <set>
#include <vector>

namespace hgw {

struct EditTri {
    float v[3][3];
    std::int32_t nb[3];
    std::uint32_t runtime=0,attribute=0;
    std::uint8_t flags=0;
    int parent=-1;          // original triangle this piece came from
    bool blocked=false;
    float cost[3]{};        // stored costs; kept unless the triangle or a neighbour changed
    bool dirty=false;       // geometry or links changed: costs are recomputed
};

struct NavEdit {
    std::vector<EditTri> tris;
    std::size_t original=0;
    std::vector<std::uint8_t> header;   // slot-0 bytes 4..15, kept as found
};

inline NavEdit load_nav(const std::vector<std::uint8_t>& s0,const std::vector<std::uint8_t>& s16) {
    NavEdit nav;
    const auto count=rd32(s0,0);
    nav.header.assign(s0.begin()+4,s0.begin()+16);
    for(std::uint32_t i=0;i<count;++i) {
        const std::size_t o=16+std::size_t(i)*0x50;
        EditTri t{};
        for(int k=0;k<3;++k)for(int j=0;j<3;++j)t.v[k][j]=rdf(s0,o+k*16+j*4);
        for(int k=0;k<3;++k){t.nb[k]=std::int32_t(rd32(s0,o+48+k*4));t.cost[k]=rdf(s0,o+64+k*4);}
        t.runtime=rd32(s0,o+60);t.attribute=rd32(s0,o+76);
        t.flags=i<s16.size()?s16[i]:0;t.parent=int(i);
        nav.tris.push_back(t);
    }
    nav.original=nav.tris.size();
    return nav;
}

inline void centroid(const EditTri& t,float c[3]) {for(int j=0;j<3;++j)c[j]=(t.v[0][j]+t.v[1][j]+t.v[2][j])/3;}

inline std::vector<std::uint8_t> save_nav(const NavEdit& nav) {
    std::vector<std::uint8_t> s0(16+nav.tris.size()*0x50,0);
    put32(s0,0,std::uint32_t(nav.tris.size()));
    std::copy(nav.header.begin(),nav.header.end(),s0.begin()+4);
    for(std::size_t i=0;i<nav.tris.size();++i) {
        const auto& t=nav.tris[i];const std::size_t o=16+i*0x50;
        float ci[3];centroid(t,ci);
        for(int k=0;k<3;++k){for(int j=0;j<3;++j)wrf(s0,o+k*16+j*4,t.v[k][j]);wrf(s0,o+k*16+12,1.0f);}
        for(int k=0;k<3;++k) {
            put32(s0,o+48+k*4,std::uint32_t(t.nb[k]));
            // Cost = centroid distance (matches the stored values to float rounding);
            // border edges store 0. Unchanged edges keep their stored bits.
            float cost=0;
            if(t.nb[k]>=0) {
                if(!t.dirty&&!nav.tris[t.nb[k]].dirty)cost=t.cost[k];
                else {float cn[3];centroid(nav.tris[t.nb[k]],cn);cost=std::sqrt((ci[0]-cn[0])*(ci[0]-cn[0])+(ci[1]-cn[1])*(ci[1]-cn[1])+(ci[2]-cn[2])*(ci[2]-cn[2]));}
            }
            wrf(s0,o+64+k*4,cost);
        }
        put32(s0,o+60,t.runtime);put32(s0,o+76,t.attribute);
    }
    return s0;
}

// Splits the edge `e` of triangle `ti` at its midpoint, on both sides (the mesh stays
// conforming). Returns the new triangle numbers created.
inline std::vector<int> split_edge(NavEdit& nav,int ti,int e) {
    std::vector<int> created;
    const int other=nav.tris[ti].nb[e];
    float m[3];
    for(int j=0;j<3;++j)m[j]=(nav.tris[ti].v[e][j]+nav.tris[ti].v[(e+1)%3][j])/2;
    // Splits triangle t (edge k = a->b, opposite c) into (a,m,c) [keeps index] and (m,b,c) [new].
    const auto halve=[&](int t,int k)->int {
        EditTri original=nav.tris[t];
        const int a=k,b=(k+1)%3,c=(k+2)%3;
        EditTri first=original,second=original;
        // first: a, m, c  -> edges: a-m (part of old k), m-c (internal), c-a (old edge c)
        std::copy(original.v[a],original.v[a]+3,first.v[0]);std::copy(m,m+3,first.v[1]);std::copy(original.v[c],original.v[c]+3,first.v[2]);
        // second: m, b, c -> edges: m-b (part of old k), b-c (old edge b), c-m (internal)
        std::copy(m,m+3,second.v[0]);std::copy(original.v[b],original.v[b]+3,second.v[1]);std::copy(original.v[c],original.v[c]+3,second.v[2]);
        const int second_index=int(nav.tris.size());
        first.nb[0]=-2;first.nb[1]=second_index;first.nb[2]=original.nb[c];
        second.nb[0]=-2;second.nb[1]=original.nb[b];second.nb[2]=t;
        second.parent=original.parent;first.dirty=second.dirty=true;
        nav.tris[t]=first;nav.tris.push_back(second);
        // the old neighbour across edge b now borders the second half
        if(original.nb[b]>=0){nav.tris[original.nb[b]].dirty=true;for(auto& n:nav.tris[original.nb[b]].nb)if(n==t){n=second_index;break;}}
        if(original.nb[c]>=0)nav.tris[original.nb[c]].dirty=true;   // its neighbour's centroid moved
        return second_index;
    };
    const int s1=halve(ti,e);created.push_back(s1);
    if(other>=0) {
        int k=-1;
        for(int j=0;j<3;++j)if(nav.tris[other].nb[j]==ti){k=j;break;}
        const int s2=halve(other,k);created.push_back(s2);
        // ti edge a-m (first, edge 0) faces other's second half (m'-b' where b'==a);
        // ti second (m-b) faces other's first half (a'-m' where a'==b).
        nav.tris[ti].nb[0]=s2;nav.tris[s2].nb[0]=ti;
        nav.tris[s1].nb[0]=other;nav.tris[other].nb[0]=s1;
    } else {
        nav.tris[ti].nb[0]=-1;nav.tris[s1].nb[0]=-1;
    }
    return created;
}

inline bool point_in_convex(const std::vector<std::array<float,2>>& poly,float x,float z) {
    bool pos=false,neg=false;
    for(std::size_t i=0;i<poly.size();++i) {
        const auto& a=poly[i];const auto& b=poly[(i+1)%poly.size()];
        const float c=(b[0]-a[0])*(z-a[1])-(b[1]-a[1])*(x-a[0]);
        if(c>0)pos=true;if(c<0)neg=true;
    }
    return !(pos&&neg);
}

inline std::vector<std::array<float,2>> convex_hull(std::vector<std::array<float,2>> p) {
    std::sort(p.begin(),p.end());p.erase(std::unique(p.begin(),p.end()),p.end());
    if(p.size()<3)return p;
    std::vector<std::array<float,2>> h(2*p.size());std::size_t k=0;
    const auto cross=[](const std::array<float,2>& o,const std::array<float,2>& a,const std::array<float,2>& b){
        return (a[0]-o[0])*(b[1]-o[1])-(a[1]-o[1])*(b[0]-o[0]);};
    for(std::size_t i=0;i<p.size();++i){while(k>=2&&cross(h[k-2],h[k-1],p[i])<=0)--k;h[k++]=p[i];}
    for(std::size_t i=p.size()-1,t=k+1;i>0;--i){while(k>=t&&cross(h[k-2],h[k-1],p[i-1])<=0)--k;h[k++]=p[i-1];}
    h.resize(k-1);
    return h;
}

// Grows a convex polygon outward by `margin` (moves each vertex away from the centre).
inline void expand(std::vector<std::array<float,2>>& poly,float margin) {
    float cx=0,cz=0;for(const auto& p:poly){cx+=p[0];cz+=p[1];}cx/=poly.size();cz/=poly.size();
    for(auto& p:poly){const float dx=p[0]-cx,dz=p[1]-cz,l=std::sqrt(dx*dx+dz*dz);if(l>1e-4f){p[0]+=dx/l*margin;p[1]+=dz/l*margin;}}
}

struct CutResult {int split=0,blocked=0,created=0;};

// Blocks the floor under `footprint` (convex, XZ) for triangles whose height lies within
// [y_low, y_high]. Triangles straddling the outline are split until their edges are
// shorter than `resolution`.
inline CutResult cut_footprint(NavEdit& nav,const std::vector<std::array<float,2>>& footprint,float y_low,float y_high,
                               float resolution=1.5f) {
    CutResult result;
    float lo[2]={1e30f,1e30f},hi[2]={-1e30f,-1e30f};
    for(const auto& p:footprint){lo[0]=std::min(lo[0],p[0]);lo[1]=std::min(lo[1],p[1]);hi[0]=std::max(hi[0],p[0]);hi[1]=std::max(hi[1],p[1]);}
    const auto relevant=[&](const EditTri& t) {
        if(t.blocked)return false;
        float tlo[2]={1e30f,1e30f},thi[2]={-1e30f,-1e30f},ylo=1e30f,yhi=-1e30f;
        for(const auto& v:t.v){tlo[0]=std::min(tlo[0],v[0]);tlo[1]=std::min(tlo[1],v[2]);thi[0]=std::max(thi[0],v[0]);thi[1]=std::max(thi[1],v[2]);
                               ylo=std::min(ylo,v[1]);yhi=std::max(yhi,v[1]);}
        return thi[0]>=lo[0]&&tlo[0]<=hi[0]&&thi[1]>=lo[1]&&tlo[1]<=hi[1]&&yhi>=y_low&&ylo<=y_high;
    };
    const auto straddles=[&](const EditTri& t) {
        int in=0;for(const auto& v:t.v)if(point_in_convex(footprint,v[0],v[2]))++in;
        float c[3];centroid(t,c);
        const bool centre=point_in_convex(footprint,c[0],c[2]);
        if(in==3)return false;
        if(in>0||centre)return true;
        for(const auto& p:footprint){   // footprint corner inside the triangle
            std::vector<std::array<float,2>> tri={{t.v[0][0],t.v[0][2]},{t.v[1][0],t.v[1][2]},{t.v[2][0],t.v[2][2]}};
            if(point_in_convex(tri,p[0],p[1]))return true;
        }
        return false;
    };
    const auto edge_length=[](const EditTri& t,int k){float s=0;for(int j=0;j<3;j+=2){const float d=t.v[k][j]-t.v[(k+1)%3][j];s+=d*d;}return std::sqrt(s);};
    for(int pass=0;pass<4000;++pass) {
        int target=-1,edge=-1;float longest=resolution;
        for(int i=0;i<int(nav.tris.size());++i) {
            const auto& t=nav.tris[i];
            if(!relevant(t)||!straddles(t))continue;
            for(int k=0;k<3;++k){const float l=edge_length(t,k);if(l>longest){longest=l;target=i;edge=k;}}
        }
        if(target<0)break;
        result.created+=int(split_edge(nav,target,edge).size());++result.split;
    }
    for(int i=0;i<int(nav.tris.size());++i) {
        auto& t=nav.tris[i];
        if(!relevant(t))continue;
        float c[3];centroid(t,c);
        if(!point_in_convex(footprint,c[0],c[2]))continue;
        for(auto& n:t.nb){if(n>=0){nav.tris[n].dirty=true;for(auto& back:nav.tris[n].nb)if(back==i)back=-1;}n=-1;}
        t.blocked=true;t.dirty=true;++result.blocked;
    }
    return result;
}

// Rebuilds slot 1 (border edges) after a cut: every open edge of a reachable triangle gets a
// record; edges that lie along an original border keep that record's kind, new ones get 0.
inline std::vector<std::uint8_t> save_border_edges(const NavEdit& nav,const std::vector<std::uint8_t>& old_s1,
                                                   const NavEdit& before) {
    struct Seg {float a[2],b[2];std::uint32_t kind;};
    std::vector<Seg> old;
    if(old_s1.size()>=16)for(std::uint32_t i=0;i<rd32(old_s1,0);++i) {
        const std::size_t o=16+std::size_t(i)*16;
        const auto t=rd32(old_s1,o),e=rd32(old_s1,o+4),kind=rd32(old_s1,o+12);
        if(t>=before.tris.size()||e>2)continue;
        const auto& tri=before.tris[t];
        old.push_back({{tri.v[e][0],tri.v[e][2]},{tri.v[(e+1)%3][0],tri.v[(e+1)%3][2]},kind});
    }
    const auto on_segment=[](const Seg& s,const float p[2]) {
        const float dx=s.b[0]-s.a[0],dz=s.b[1]-s.a[1],l2=dx*dx+dz*dz;
        if(l2<1e-8f)return false;
        const float t=((p[0]-s.a[0])*dx+(p[1]-s.a[1])*dz)/l2;
        if(t<-1e-3f||t>1+1e-3f)return false;
        const float ex=s.a[0]+t*dx-p[0],ez=s.a[1]+t*dz-p[1];
        return ex*ex+ez*ez<1e-4f;
    };
    std::vector<std::array<std::uint32_t,4>> records;
    for(std::size_t i=0;i<nav.tris.size();++i) {
        const auto& t=nav.tris[i];
        if(t.blocked)continue;
        for(int k=0;k<3;++k) {
            if(t.nb[k]>=0)continue;
            const float a[2]={t.v[k][0],t.v[k][2]},b[2]={t.v[(k+1)%3][0],t.v[(k+1)%3][2]},mid[2]={(a[0]+b[0])/2,(a[1]+b[1])/2};
            std::uint32_t kind=0;
            for(const auto& s:old)if(on_segment(s,a)&&on_segment(s,b)&&on_segment(s,mid)){kind=s.kind;break;}
            records.push_back({std::uint32_t(i),std::uint32_t(k),std::uint32_t(k),kind});
        }
    }
    std::vector<std::uint8_t> s1(16+records.size()*16,0);
    put32(s1,0,std::uint32_t(records.size()));
    for(std::size_t i=0;i<records.size();++i)for(int j=0;j<4;++j)put32(s1,16+i*16+j*4,records[i][j]);
    return s1;
}

// Children (appended pieces) of each original triangle number.
inline std::map<int,std::vector<int>> children_of(const NavEdit& nav) {
    std::map<int,std::vector<int>> children;
    for(std::size_t i=nav.original;i<nav.tris.size();++i)children[nav.tris[i].parent].push_back(int(i));
    return children;
}

// Appends children wherever their parent is listed in slot 7 portal lists.
inline std::vector<std::uint8_t> extend_portals(const std::vector<std::uint8_t>& s7,const std::map<int,std::vector<int>>& children) {
    if(s7.size()<4||children.empty())return s7;
    std::size_t end=s7.size();std::vector<std::size_t> offsets;
    for(std::size_t i=0;i*4<end;++i){const std::size_t o=rd32(s7,i*4);if(o){offsets.push_back(o);end=std::min(end,o);}}
    const std::size_t table_bytes=end;
    struct Rec {std::vector<std::uint8_t> head;std::vector<std::uint32_t> a,b;std::size_t order;};
    std::vector<Rec> recs;
    for(std::size_t r=0;r<offsets.size();++r) {
        const auto o=offsets[r];Rec rec;rec.order=r;
        rec.head.assign(s7.begin()+o,s7.begin()+o+40);
        const auto n1=rd32(s7,o+40);for(std::uint32_t k=0;k<n1;++k)rec.a.push_back(rd32(s7,o+44+k*4));
        const std::size_t o2=o+44+std::size_t(n1)*4;
        const auto n2=o2+4<=s7.size()?rd32(s7,o2):0;for(std::uint32_t k=0;k<n2;++k)rec.b.push_back(rd32(s7,o2+4+k*4));
        recs.push_back(rec);
    }
    const auto grow=[&](std::vector<std::uint32_t>& list){
        const auto copy=list;for(const auto t:copy){const auto f=children.find(int(t));if(f!=children.end())for(const int c:f->second)list.push_back(std::uint32_t(c));}};
    for(auto& rec:recs){grow(rec.a);grow(rec.b);}
    std::vector<std::uint8_t> out(table_bytes,0);
    for(std::size_t r=0;r<recs.size();++r) {
        while(out.size()%4)out.push_back(0);
        put32(out,r*4,std::uint32_t(out.size()));
        const auto& rec=recs[r];
        out.insert(out.end(),rec.head.begin(),rec.head.end());
        const auto push=[&](std::uint32_t v){for(int i=0;i<4;++i)out.push_back(std::uint8_t(v>>(8*i)));};
        push(std::uint32_t(rec.a.size()));for(const auto t:rec.a)push(t);
        push(std::uint32_t(rec.b.size()));for(const auto t:rec.b)push(t);
    }
    return out;
}

// Appends children to slot 14 groups that list their parent.
inline std::vector<std::uint8_t> extend_groups(const std::vector<std::uint8_t>& s14,const std::map<int,std::vector<int>>& children) {
    if(s14.size()<4||children.empty())return s14;
    const auto count=rd32(s14,0);
    std::vector<std::vector<std::uint32_t>> groups;
    for(std::uint32_t g=0;g<count;++g) {
        const std::size_t o=rd32(s14,4+g*4);const auto n=rd32(s14,o);
        std::vector<std::uint32_t> list;for(std::uint32_t k=0;k<n;++k)list.push_back(rd32(s14,o+4+k*4));
        const auto copy=list;for(const auto t:copy){const auto f=children.find(int(t));if(f!=children.end())for(const int c:f->second)list.push_back(std::uint32_t(c));}
        groups.push_back(list);
    }
    std::vector<std::uint8_t> out(4+groups.size()*4,0);
    put32(out,0,count);
    for(std::size_t g=0;g<groups.size();++g) {
        put32(out,4+g*4,std::uint32_t(out.size()));
        const auto push=[&](std::uint32_t v){for(int i=0;i<4;++i)out.push_back(std::uint8_t(v>>(8*i)));};
        push(std::uint32_t(groups[g].size()));for(const auto t:groups[g])push(t);
    }
    return out;
}

inline std::vector<std::uint8_t> save_flags(const NavEdit& nav) {
    std::vector<std::uint8_t> s16;
    for(const auto& t:nav.tris)s16.push_back(t.flags);
    return s16;
}

// ---- re-opening floor -----------------------------------------------------------------
// All 77,754 disc triangles have negative signed area (b-a)x(c-a) in XZ; new ones match.
inline float signed_area_xz(const float a[3],const float b[3],const float c[3]) {
    return (b[0]-a[0])*(c[2]-a[2])-(b[2]-a[2])*(c[0]-a[0]);
}

struct FillResult {int reconnected=0,added=0;};

inline bool same_point(const float a[3],const float b[3]) {return a[0]==b[0]&&a[1]==b[1]&&a[2]==b[2];}

// Links every open edge of triangle `i` to a triangle with the matching open edge.
inline int link_open_edges(NavEdit& nav,int i) {
    int links=0;
    for(int k=0;k<3;++k) {
        if(nav.tris[i].nb[k]>=0)continue;
        const float* a=nav.tris[i].v[k];const float* b=nav.tris[i].v[(k+1)%3];
        for(int j=0;j<int(nav.tris.size())&&nav.tris[i].nb[k]<0;++j) {
            if(j==i||nav.tris[j].blocked)continue;
            for(int m=0;m<3;++m)if(nav.tris[j].nb[m]<0&&same_point(nav.tris[j].v[m],b)&&same_point(nav.tris[j].v[(m+1)%3],a)) {
                nav.tris[i].nb[k]=j;nav.tris[j].nb[m]=i;nav.tris[i].dirty=nav.tris[j].dirty=true;++links;break;
            }
        }
    }
    return links;
}

// Opens floor under `footprint` (convex, XZ) at heights [y_low, y_high]:
// 1) triangles disconnected by an earlier cut inside the footprint are reconnected;
// 2) the open wall edges inside the footprint (grown by 0.75) are chained; a closed loop
//    (hole) is filled as is, an open chain (notch against the outline) is closed with a
//    straight segment; the polygon is ear-clipped into triangles that join the chain's
//    triangles along their edges and copy the attribute/flag byte of the first one.
inline FillResult fill_footprint(NavEdit& nav,std::vector<std::array<float,2>> footprint,float y_low,float y_high) {
    FillResult result;
    const auto inside=[&](const float v[3]){return point_in_convex(footprint,v[0],v[2])&&v[1]>=y_low&&v[1]<=y_high;};
    std::vector<int> isolated;   // unblock them all first so they can link to each other
    for(int i=0;i<int(nav.tris.size());++i) {
        auto& t=nav.tris[i];
        if(!(t.nb[0]<0&&t.nb[1]<0&&t.nb[2]<0))continue;
        float c[3];centroid(t,c);
        if(!inside(c))continue;
        t.blocked=false;t.dirty=true;isolated.push_back(i);
    }
    for(const int i:isolated)link_open_edges(nav,i);
    for(const int i:isolated) {
        auto& t=nav.tris[i];
        if(t.nb[0]<0&&t.nb[1]<0&&t.nb[2]<0)t.blocked=true;else ++result.reconnected;
    }
    expand(footprint,0.75f);
    // Open edges (walkable side = owning triangle) with both ends in the footprint.
    struct Edge {int tri,k;const float* a;const float* b;bool used=false;};
    std::vector<Edge> edges;
    for(int i=0;i<int(nav.tris.size());++i) {
        const auto& t=nav.tris[i];
        if(t.blocked)continue;
        for(int k=0;k<3;++k)if(t.nb[k]<0&&inside(t.v[k])&&inside(t.v[(k+1)%3]))edges.push_back({i,k,t.v[k],t.v[(k+1)%3]});
    }
    while(true) {
        // Start a chain at an edge whose start is not the end of another unused edge.
        int start=-1;
        for(int e=0;e<int(edges.size())&&start<0;++e) {
            if(edges[e].used)continue;
            bool has_pred=false;
            for(const auto& f:edges)if(!f.used&&&f!=&edges[e]&&same_point(f.b,edges[e].a)){has_pred=true;break;}
            if(!has_pred)start=e;
        }
        if(start<0)for(int e=0;e<int(edges.size());++e)if(!edges[e].used){start=e;break;}
        if(start<0)break;
        std::vector<int> chain{start};edges[start].used=true;
        bool closed=false;
        for(;;) {
            const float* tail=edges[chain.back()].b;
            if(same_point(tail,edges[chain.front()].a)){closed=true;break;}
            int next=-1;
            for(int e=0;e<int(edges.size());++e)if(!edges[e].used&&same_point(edges[e].a,tail)){next=e;break;}
            if(next<0)break;
            edges[next].used=true;chain.push_back(next);
        }
        if(chain.size()<2&&!closed)continue;
        // The hole lies on the far side of the walkable triangles' edges, so the fill
        // polygon runs the chain backwards.
        std::vector<const float*> poly;std::vector<int> owner;   // owner[i]: chain edge for poly edge i->i+1, -1 = closing
        for(int c=int(chain.size())-1;c>=0;--c){poly.push_back(edges[chain[c]].b);owner.push_back(chain[c]);}
        if(!closed){poly.push_back(edges[chain.front()].a);owner.push_back(-1);}
        if(poly.size()<3)continue;
        // Ear clipping in XZ, keeping the game's (negative) winding.
        double area=0;
        for(std::size_t i=0;i<poly.size();++i){const float* p=poly[i];const float* q=poly[(i+1)%poly.size()];area+=double(p[0])*q[2]-double(q[0])*p[2];}
        std::vector<int> ring(poly.size());for(std::size_t i=0;i<ring.size();++i)ring[i]=int(i);
        // Walking the walls backwards encloses the missing floor with the game's (negative)
        // winding. A positive area means the closed chain wraps walkable floor instead (a
        // U-shaped wall run, or the room's outer wall): nothing to fill there.
        if(area>=0)continue;
        {   // and the polygon must not lie on existing walkable floor
            float cx=0,cz=0;for(const auto* p:poly){cx+=p[0];cz+=p[2];}cx/=float(poly.size());cz/=float(poly.size());
            bool covered=false;
            for(const auto& t:nav.tris) {
                if(t.blocked)continue;
                std::vector<std::array<float,2>> tri={{t.v[0][0],t.v[0][2]},{t.v[1][0],t.v[1][2]},{t.v[2][0],t.v[2][2]}};
                if(point_in_convex(tri,cx,cz)&&std::fabs(t.v[0][1]-poly[0][1])<3.0f){covered=true;break;}
            }
            if(covered)continue;
        }
        const bool flip=false;
        std::vector<std::array<int,3>> made;
        int guard=0;
        while(ring.size()>3&&guard++<10000) {
            bool clipped=false;
            for(std::size_t i=0;i<ring.size();++i) {
                const int ia=ring[(i+ring.size()-1)%ring.size()],ib=ring[i],ic=ring[(i+1)%ring.size()];
                float s=signed_area_xz(poly[ia],poly[ib],poly[ic]);if(flip)s=-s;
                if(s>=0)continue;   // reflex or degenerate
                bool empty=true;
                for(const int j:ring) {
                    if(j==ia||j==ib||j==ic)continue;
                    std::vector<std::array<float,2>> tri={{poly[ia][0],poly[ia][2]},{poly[ib][0],poly[ib][2]},{poly[ic][0],poly[ic][2]}};
                    if(point_in_convex(tri,poly[j][0],poly[j][2])){empty=false;break;}
                }
                if(!empty)continue;
                made.push_back({ia,ib,ic});ring.erase(ring.begin()+std::ptrdiff_t(i));clipped=true;break;
            }
            if(!clipped)break;
        }
        if(ring.size()==3)made.push_back({ring[0],ring[1],ring[2]});
        if(made.empty())continue;
        const auto& source=nav.tris[edges[chain.front()].tri];
        const int first=int(nav.tris.size());
        for(const auto& m:made) {
            EditTri t{};
            const int order[3]={m[0],flip?m[2]:m[1],flip?m[1]:m[2]};
            for(int k=0;k<3;++k)std::copy(poly[order[k]],poly[order[k]]+3,t.v[k]);
            t.nb[0]=t.nb[1]=t.nb[2]=-1;t.attribute=source.attribute;t.flags=source.flags;t.parent=-1;t.dirty=true;
            nav.tris.push_back(t);
        }
        // Join new triangles to each other and to the chain's triangles.
        for(int i=first;i<int(nav.tris.size());++i)result.added+=0,link_open_edges(nav,i);
        result.added+=int(made.size());
    }
    return result;
}
}
