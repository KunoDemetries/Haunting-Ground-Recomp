#pragma once
// Mouse picking for the inspector: nearest overlay object along the view ray.
#include "room.hpp"
#include <array>
#include <cmath>
#include <cstdio>
#include <limits>
#include <string>
#include <vector>

namespace hgw {

using Vec3=std::array<float,3>;
inline Vec3 v3(const float p[3]) {return {p[0],p[1],p[2]};}
inline Vec3 sub(Vec3 a,Vec3 b) {return {a[0]-b[0],a[1]-b[1],a[2]-b[2]};}
inline Vec3 add(Vec3 a,Vec3 b) {return {a[0]+b[0],a[1]+b[1],a[2]+b[2]};}
inline Vec3 scale(Vec3 a,float s) {return {a[0]*s,a[1]*s,a[2]*s};}
inline float dot(Vec3 a,Vec3 b) {return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];}
inline Vec3 cross(Vec3 a,Vec3 b) {return {a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};}

struct Ray {Vec3 origin,direction;};   // direction normalised

// Distance along the ray to its closest approach to segment ab, if within `radius`.
inline bool ray_segment(const Ray& r,Vec3 a,Vec3 b,float radius,float& t_out) {
    const Vec3 d=sub(b,a),w=sub(r.origin,a);
    const float aa=dot(r.direction,r.direction),bb=dot(r.direction,d),cc=dot(d,d),dd=dot(r.direction,w),ee=dot(d,w);
    const float den=aa*cc-bb*bb;
    float t=0,s=0;
    if(den>1e-9f){t=(bb*ee-cc*dd)/den;s=(aa*ee-bb*dd)/den;}
    else {s=cc>0?ee/cc:0;}
    s=std::clamp(s,0.0f,1.0f);
    t=std::max(0.0f,dot(sub(add(a,scale(d,s)),r.origin),r.direction));
    const Vec3 p=add(r.origin,scale(r.direction,t)),q=add(a,scale(d,s));
    const Vec3 gap=sub(p,q);
    if(dot(gap,gap)>radius*radius)return false;
    t_out=t;return true;
}

inline bool ray_triangle(const Ray& r,Vec3 a,Vec3 b,Vec3 c,float& t_out) {
    const Vec3 e1=sub(b,a),e2=sub(c,a),p=cross(r.direction,e2);
    const float det=dot(e1,p);
    if(std::fabs(det)<1e-9f)return false;
    const Vec3 s=sub(r.origin,a);
    const float u=dot(s,p)/det;if(u<0||u>1)return false;
    const Vec3 q=cross(s,e1);
    const float v=dot(r.direction,q)/det;if(v<0||u+v>1)return false;
    const float t=dot(e2,q)/det;if(t<0)return false;
    t_out=t;return true;
}

enum class Kind {none,light,camera,door,shape,nav,mesh};

struct Selection {
    Kind kind=Kind::none;
    int index=-1;
    std::string text;                       // shown in the title bar
    std::vector<std::array<Vec3,2>> lines;  // highlight
    float distance=std::numeric_limits<float>::max();
    int destination_room=-1;                // selected door's other side (Enter follows it)
};

struct PickScene {
    const NavMesh* nav=nullptr;
    const std::vector<Shape>* shapes=nullptr;
    const std::vector<Portal>* portals=nullptr;
    const Lighting* lighting=nullptr;
    const std::vector<Camera>* cameras=nullptr;
    const std::vector<DoorLink>* doors=nullptr;
    int room=-1;
    bool with_nav=false,with_objects=false;
    const std::vector<Mesh>* meshes=nullptr;   // slot-3 room meshes (world-space triangles)
    bool with_meshes=false;
};

inline std::vector<std::array<Vec3,2>> cross_lines(Vec3 p,float s) {
    return {{add(p,{-s,0,0}),add(p,{s,0,0})},{add(p,{0,-s,0}),add(p,{0,s,0})},{add(p,{0,0,-s}),add(p,{0,0,s})}};
}

inline std::vector<std::array<Vec3,2>> shape_lines(const Shape& sh) {
    std::vector<std::array<Vec3,2>> lines;
    const std::size_t n=sh.points.size(),edges=sh.kind==1?n:n-1;
    for(std::size_t i=0;i<edges;++i) {
        const Vec3 a=sh.points[i],b=sh.points[(i+1)%n],a1=add(a,{0,sh.height,0}),b1=add(b,{0,sh.height,0});
        lines.push_back({a,b});lines.push_back({a1,b1});lines.push_back({a,a1});lines.push_back({b,b1});
    }
    return lines;
}

inline std::array<Vec3,2> door_top(const Portal& p) {return {add(v3(p.a),{0,6,0}),add(v3(p.b),{0,6,0})};}

// Text, highlight and door destination for one object (used after picks and edits).
inline Selection describe(const PickScene& scene,Kind kind,int index) {
    Selection s;s.kind=kind;s.index=index;
    char text[320];
    switch(kind) {
    case Kind::light: {
        const auto& l=scene.lighting->lights.at(index);const Vec3 p=v3(l.position);
        std::snprintf(text,sizeof text,"light %d: pos (%.1f, %.1f, %.1f) colour (%.1f, %.1f, %.1f) intensity %.2f range %.1f",
            index,p[0],p[1],p[2],l.colour[0],l.colour[1],l.colour[2],l.intensity,l.range);
        s.lines=cross_lines(p,4);break;
    }
    case Kind::camera: {
        const auto& c=scene.cameras->at(index);
        std::snprintf(text,sizeof text,"camera %d: eye (%.1f, %.1f, %.1f) target (%.1f, %.1f, %.1f) fov %.0f",
            index+1,c.eye[0],c.eye[1],c.eye[2],c.target[0],c.target[1],c.target[2],c.fov);
        s.lines=cross_lines(v3(c.eye),3);s.lines.push_back({v3(c.eye),v3(c.target)});break;
    }
    case Kind::door: {
        const auto& p=scene.portals->at(index);const auto top=door_top(p);
        const DoorLink* link=nullptr;
        const auto* other=scene.doors?door_destination(*scene.doors,scene.room,index,&link):nullptr;
        char where[96]="destination not in the door table";
        if(other){std::snprintf(where,sizeof where,"leads to room %X door %u (flags 0x%x) - Enter to go",other->room,other->door,link->flags);s.destination_room=other->room;}
        std::snprintf(text,sizeof text,"door %d: %s | nav tri %u, A (%.1f, %.1f, %.1f) angle %.3f, %zu area tris",
            index,where,p.triangle,p.a[0],p.a[1],p.a[2],p.angle,p.area.size());
        s.lines={{v3(p.a),top[0]},{top[0],top[1]},{top[1],v3(p.b)}};break;
    }
    case Kind::shape: {
        const auto& sh=scene.shapes->at(index);
        std::snprintf(text,sizeof text,"shape %d: kind %u (%s), value %.1f, %zu points, first (%.1f, %.1f, %.1f)",
            sh.index,sh.kind,sh.kind==1?"polygon":"line",sh.height,sh.points.size(),sh.points[0][0],sh.points[0][1],sh.points[0][2]);
        s.lines=shape_lines(sh);break;
    }
    case Kind::nav: {
        const auto& tri=scene.nav->triangles.at(index);
        const Vec3 a=v3(tri.v[0]),b=v3(tri.v[1]),c=v3(tri.v[2]);
        std::snprintf(text,sizeof text,"nav tri %d: attribute 0x%x, flags %u, neighbours %d/%d/%d, costs %.2f/%.2f/%.2f",
            index,tri.attribute,tri.flags,tri.neighbour[0],tri.neighbour[1],tri.neighbour[2],tri.cost[0],tri.cost[1],tri.cost[2]);
        s.lines={{a,b},{b,c},{c,a}};break;
    }
    case Kind::mesh: {
        const auto& mesh=scene.meshes->at(index);
        std::snprintf(text,sizeof text,"mesh %d: %zu triangles, texture %d, flags 0x%x, placed at (%.1f, %.1f, %.1f)",
            index,mesh.triangles.size()/3,int(mesh.texture),mesh.flags,mesh.translation[0],mesh.translation[1],mesh.translation[2]);
        for(std::size_t i=0;i+2<mesh.triangles.size()&&s.lines.size()<6000;i+=3) {
            const auto& a=mesh.triangles[i];const auto& b=mesh.triangles[i+1];const auto& c=mesh.triangles[i+2];
            const Vec3 pa{a.x,a.y,a.z},pb{b.x,b.y,b.z},pc{c.x,c.y,c.z};
            s.lines.push_back({pa,pb});s.lines.push_back({pb,pc});s.lines.push_back({pc,pa});
        }
        break;
    }
    case Kind::none: return s;
    }
    s.text=text;
    return s;
}

inline Selection pick(const Ray& ray,const PickScene& scene) {
    Kind best_kind=Kind::none;int best_index=-1;float best=std::numeric_limits<float>::max();
    const auto offer=[&](float t,Kind kind,int index) {
        if(t<2.0f)return;   // the object the viewer stands on/at (e.g. the current camera's eye)
        if(t<best){best=t;best_kind=kind;best_index=index;}
    };
    float t=0;
    if(scene.with_objects) {
        for(std::size_t i=0;i<scene.lighting->lights.size();++i) {
            const Vec3 p=v3(scene.lighting->lights[i].position);
            if(ray_segment(ray,p,p,2.0f,t))offer(t,Kind::light,int(i));
        }
        for(std::size_t i=0;i<scene.cameras->size();++i) {
            const Vec3 e=v3((*scene.cameras)[i].eye);
            if(ray_segment(ray,e,e,2.0f,t))offer(t,Kind::camera,int(i));
        }
        for(std::size_t i=0;i<scene.portals->size();++i) {
            const auto& p=(*scene.portals)[i];const auto top=door_top(p);
            if(ray_segment(ray,v3(p.a),top[0],1.5f,t)||ray_segment(ray,top[0],top[1],1.5f,t)||ray_segment(ray,v3(p.b),top[1],1.5f,t))
                offer(t,Kind::door,int(i));
        }
        for(std::size_t k=0;k<scene.shapes->size();++k) {
            const auto& sh=(*scene.shapes)[k];
            float hit_t=std::numeric_limits<float>::max();
            for(const auto& seg:shape_lines(sh))if(ray_segment(ray,seg[0],seg[1],1.0f,t))hit_t=std::min(hit_t,t);
            const std::size_t n=sh.points.size();
            if(sh.kind==1&&n>=3)for(std::size_t i=1;i+1<n;++i)   // also the base polygon
                if(ray_triangle(ray,sh.points[0],sh.points[i],sh.points[i+1],t))hit_t=std::min(hit_t,t);
            if(hit_t<std::numeric_limits<float>::max())offer(hit_t,Kind::shape,int(k));
        }
    }
    if(scene.with_meshes&&scene.meshes) {
        for(std::size_t m=0;m<scene.meshes->size();++m) {
            const auto& tris=(*scene.meshes)[m].triangles;
            float hit_t=std::numeric_limits<float>::max();
            for(std::size_t i=0;i+2<tris.size();i+=3) {
                const Vec3 a{tris[i].x,tris[i].y,tris[i].z},b{tris[i+1].x,tris[i+1].y,tris[i+1].z},c{tris[i+2].x,tris[i+2].y,tris[i+2].z};
                if(ray_triangle(ray,a,b,c,t))hit_t=std::min(hit_t,t);
            }
            if(hit_t<std::numeric_limits<float>::max())offer(hit_t+0.25f,Kind::mesh,int(m));   // overlay objects win ties
        }
    }
    if(scene.with_nav) {
        for(std::size_t i=0;i<scene.nav->triangles.size();++i) {
            const auto& tri=scene.nav->triangles[i];
            if(ray_triangle(ray,v3(tri.v[0]),v3(tri.v[1]),v3(tri.v[2]),t))
                offer(t+0.5f,Kind::nav,int(i));   // overlay objects win ties with the floor
        }
    }
    auto result=describe(scene,best_kind,best_index);
    result.distance=best;
    return result;
}
}
