// Descent 3MASTERED - GPL-3.0-or-later
#pragma once
#include "SceneView.h"
#include <cmath>
#include <algorithm>
namespace d3scene {
inline bool rasterTrailActive(const PreparedMesh& mesh,const View& view,bool camera_history,bool paused) {
    return uint32_t(view.kind) < 32 && (mesh.raster_trail.camera_kinds&(1u<<uint32_t(view.kind))) &&
        view.projection == ViewProjection::Perspective && camera_history && !paused &&
        mesh.has_previous_world && mesh.raster_trail.length > 0;
}
struct RasterTrailVertex { ClipPosition clip{}; float alpha = 1; bool extended = false; };
inline RasterTrailVertex rasterTrailVertex(Vec3 head,Vec3 prior,const RasterTrail& style,Vec2 corner,
    const Projection& p,Vec2 pixel_scale) {
    RasterTrailVertex result;
    const auto clip = [&](Vec3 v) { return ClipPosition{v.x*p.x_scale+v.z*p.x_offset,v.y*p.y_scale+v.z*p.y_offset,v.z-2*p.near_distance,v.z}; };
    result.clip = clip(head);
    const float dx = prior.x-head.x,dy = prior.y-head.y,dz = prior.z-head.z;
    const float change = std::sqrt(dx*dx+dy*dy+dz*dz);
    if (!(style.length > 0 && change > style.minimum_delta && change > 0)) return result;
    const float fraction = (std::min)(1.f,change/style.reference_length);
    const float factor = style.length*fraction*style.reference_length/change;
    Vec3 tail{head.x+dx*factor,head.y+dy*factor,head.z+dz*factor};
    if (head.z <= p.near_distance && tail.z <= p.near_distance) return result;
    const float head_alpha = style.minimum_alpha+(1-style.minimum_alpha)*(std::max)(0.f,1-fraction*style.alpha_falloff);
    float from_alpha = head_alpha,to_alpha = 0;
    if (head.z < p.near_distance) {
        const float t = (p.near_distance-head.z)/(tail.z-head.z);
        head = {head.x+(tail.x-head.x)*t,head.y+(tail.y-head.y)*t,p.near_distance}; from_alpha *= 1-t;
    } else if (tail.z < p.near_distance) {
        const float t = (p.near_distance-tail.z)/(head.z-tail.z);
        tail = {tail.x+(head.x-tail.x)*t,tail.y+(head.y-tail.y)*t,p.near_distance}; to_alpha = head_alpha*t;
    }
    const auto a = clip(head),b = clip(tail);
    const double x = (b.x/b.w-a.x/a.w)/pixel_scale.x,y = (b.y/b.w-a.y/a.w)/pixel_scale.y;
    const double distance = std::sqrt(x*x+y*y);
    if (!(distance > 1.e-6)) return result;
    const float endpoint = corner.x+.5f;
    result.clip = {a.x+(b.x-a.x)*endpoint,a.y+(b.y-a.y)*endpoint,a.z+(b.z-a.z)*endpoint,a.w+(b.w-a.w)*endpoint};
    result.clip.x += -y/distance*corner.y*pixel_scale.x*result.clip.w;
    result.clip.y += x/distance*corner.y*pixel_scale.y*result.clip.w;
    result.alpha = from_alpha+(to_alpha-from_alpha)*endpoint; result.extended = true;
    return result;
}
inline const char* rasterTrailShader() { return R"GLSL(
vec4 trailProject(vec3 p,vec4 optics,float near_clip) {
    return vec4(p.xy*optics.xy+p.z*optics.zw,p.z-2.0*near_clip,p.z);
}
bool rasterTrailVertex(vec3 head,vec3 prior,vec4 style,float reference_length,vec2 corner,vec4 optics,
    float near_clip,vec2 pixel_scale,out vec4 result,out float opacity) {
    vec3 delta = prior-head; float change = length(delta);
    opacity = 1.0;
    if (!(style.y > 0.0 && change > style.x && change > 0.0)) return false;
    float fraction = min(1.0,change/reference_length);
    vec3 tail = head+delta*(style.y*fraction*reference_length/change);
    if (head.z <= near_clip && tail.z <= near_clip) return false;
    float head_alpha = mix(style.z,1.0,max(0.0,1.0-fraction*style.w));
    float from_alpha = head_alpha,to_alpha = 0.0;
    if (head.z < near_clip) {
        float t = (near_clip-head.z)/(tail.z-head.z);
        head = mix(head,tail,t); head.z = near_clip; from_alpha *= 1.0-t;
    } else if (tail.z < near_clip) {
        float t = (near_clip-tail.z)/(head.z-tail.z);
        tail = mix(tail,head,t); tail.z = near_clip; to_alpha = head_alpha*t;
    }
    vec4 a = trailProject(head,optics,near_clip),b = trailProject(tail,optics,near_clip);
    vec2 direction = (b.xy/b.w-a.xy/a.w)/pixel_scale;
    float extent = length(direction); if (!(extent > 1.e-6)) return false;
    result = mix(a,b,corner.x+.5);
    result.xy += vec2(-direction.y,direction.x)/extent*corner.y*pixel_scale*result.w;
    opacity = mix(from_alpha,to_alpha,corner.x+.5);
    return true;
}
)GLSL"; }
}
