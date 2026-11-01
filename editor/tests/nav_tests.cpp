// Synthetic tests for the walkable-floor editor (no game data).
#include "../src/navedit.hpp"
#include <cstdio>
#include <cstdlib>

using namespace hgw;

static int failures=0;
#define CHECK(cond) do{if(!(cond)){std::printf("FAIL %s:%d %s\n",__FILE__,__LINE__,#cond);++failures;}}while(0)

// 4x4 grid of 2-unit squares at y=0, two triangles each, game winding (negative area),
// skipping the cells listed in `missing`.
static NavEdit grid(std::initializer_list<std::pair<int,int>> missing) {
    NavEdit nav;nav.header.assign(12,0);
    for(int gx=0;gx<4;++gx)for(int gz=0;gz<4;++gz) {
        bool skip=false;for(const auto& m:missing)if(m.first==gx&&m.second==gz)skip=true;
        if(skip)continue;
        const float x0=gx*2.0f,x1=x0+2,z0=gz*2.0f,z1=z0+2;
        const float a[3]={x0,0,z0},b[3]={x1,0,z0},c[3]={x1,0,z1},d[3]={x0,0,z1};
        for(const auto& tri:{std::array<const float*,3>{a,c,b},std::array<const float*,3>{a,d,c}}) {
            EditTri t{};for(int k=0;k<3;++k)for(int j=0;j<3;++j)t.v[k][j]=tri[k][j];
            t.nb[0]=t.nb[1]=t.nb[2]=-1;t.parent=int(nav.tris.size());t.attribute=7;
            nav.tris.push_back(t);
        }
    }
    for(int i=0;i<int(nav.tris.size());++i)link_open_edges(nav,i);
    nav.original=nav.tris.size();
    for(auto& t:nav.tris)t.dirty=false;
    return nav;
}

static void check_mesh(const NavEdit& nav) {
    for(int i=0;i<int(nav.tris.size());++i) {
        const auto& t=nav.tris[i];
        CHECK(signed_area_xz(t.v[0],t.v[1],t.v[2])<0);
        for(int k=0;k<3;++k) {
            const int n=t.nb[k];
            if(n<0)continue;
            bool back=false,shared=false;
            for(int m=0;m<3;++m)if(nav.tris[n].nb[m]==i){back=true;shared=same_point(nav.tris[n].v[m],t.v[(k+1)%3])&&same_point(nav.tris[n].v[(m+1)%3],t.v[k]);}
            CHECK(back);CHECK(shared);
        }
    }
}

static int open_edges(const NavEdit& nav) {
    int n=0;for(const auto& t:nav.tris)if(!t.blocked)for(int k=0;k<3;++k)if(t.nb[k]<0)++n;
    return n;
}

int main() {
    {   // the full grid has only its outer outline open: 4 sides x 4 cells
        auto nav=grid({});check_mesh(nav);CHECK(open_edges(nav)==16);
    }
    {   // a missing interior cell (hole) is filled and the outline is all that stays open
        auto nav=grid({{1,1}});CHECK(open_edges(nav)==20);
        const std::vector<std::array<float,2>> footprint={{2.2f,2.2f},{3.8f,2.2f},{3.8f,3.8f},{2.2f,3.8f}};
        const auto r=fill_footprint(nav,footprint,-1,1);
        CHECK(r.added==2);check_mesh(nav);CHECK(open_edges(nav)==16);
        CHECK(nav.tris.back().attribute==7);
    }
    {   // a missing outline cell (notch) is filled; the closing segment becomes outline
        auto nav=grid({{1,0}});CHECK(open_edges(nav)==18);
        const std::vector<std::array<float,2>> footprint={{2.2f,0.2f},{3.8f,0.2f},{3.8f,1.8f},{2.2f,1.8f}};
        const auto r=fill_footprint(nav,footprint,-1,1);
        CHECK(r.added==2);check_mesh(nav);CHECK(open_edges(nav)==16);
    }
    {   // the room's own outline is never "filled" (it wraps walkable floor)
        auto nav=grid({});
        const std::vector<std::array<float,2>> footprint={{-1,-1},{9,-1},{9,9},{-1,9}};
        const auto r=fill_footprint(nav,footprint,-1,1);
        CHECK(r.added==0);CHECK(open_edges(nav)==16);
    }
    {   // cutting then opening restores every link
        auto nav=grid({});
        const std::vector<std::array<float,2>> footprint={{3,3},{5,3},{5,5},{3,5}};
        const auto cut=cut_footprint(nav,footprint,-1,1,0.8f);
        CHECK(cut.blocked>0);check_mesh(nav);CHECK(open_edges(nav)>16);
        const auto opened=fill_footprint(nav,footprint,-1,1);
        CHECK(opened.reconnected==cut.blocked);CHECK(opened.added==0);check_mesh(nav);CHECK(open_edges(nav)==16);
        const auto bytes=save_nav(nav);const auto again=load_nav(bytes,save_flags(nav));
        CHECK(again.tris.size()==nav.tris.size());
    }
    std::printf(failures?"%d failure(s)\n":"all navigation tests passed\n",failures);
    return failures?1:0;
}
