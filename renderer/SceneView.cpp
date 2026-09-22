// Descent 3MASTERED - GPL-3.0-or-later
#include "SceneView.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace d3scene {
namespace {
double dot(Vec3 a, Vec3 b) { return double(a.x)*b.x + double(a.y)*b.y + double(a.z)*b.z; }
Vec3 subtract(Vec3 a, Vec3 b) { return {a.x-b.x, a.y-b.y, a.z-b.z}; }
Vec3 combine(Vec3 a, float x, Vec3 b, float y) {
    return {a.x*x+b.x*y, a.y*x+b.y*y, a.z*x+b.z*y};
}
Plane plane(Vec3 normal, Vec3 eye, float shift = 0) {
    const double length = std::sqrt(dot(normal,normal));
    if (!(length > 0) || !std::isfinite(length)) throw std::invalid_argument("Invalid camera plane");
    const float inverse = static_cast<float>(1/length);
    const auto unit = combine(normal,inverse,{},0);
    return {unit,static_cast<float>(-dot(unit,eye))+shift*inverse};
}
double distance(const Plane& plane, Vec3 point) { return dot(plane.normal,point)+plane.constant; }
double tolerance(const Plane& plane, Vec3 point) {
    // Float camera/geometry inputs and GPU float math can disagree at a boundary.
    // Scale the outward tolerance with cancellation magnitude, not distance alone.
    return 2e-6*(1+std::abs(double(plane.normal.x)*point.x)+
        std::abs(double(plane.normal.y)*point.y)+std::abs(double(plane.normal.z)*point.z)+std::abs(plane.constant));
}
bool polygonOutside(const Camera& camera, const RoomGeometry& geometry, const RoomPolygon& polygon) {
    // A separating frustum plane proves rejection. Not finding one deliberately
    // keeps possible corner false positives: never reject a large polygon just
    // because all of its vertices lie outside different planes.
    for (const auto& plane : camera.planes) {
        bool all_outside = true;
        for (const auto& corner : polygon.corners) {
            const auto& point = geometry.vertices[corner.vertex];
            if (distance(plane,point) >= -tolerance(plane,point)) { all_outside = false; break; }
        }
        if (all_outside) return true;
    }
    return false;
}
}

Camera prepareCamera(const View& view) {
    validateView(view);
    Camera camera;
    camera.transform = view.camera;
    // Keep operation order identical to the first verified GPU projection.
    const float aspect = static_cast<float>(view.projection_anchor.width)/view.projection_anchor.height;
    const float zoom = view.zoom*.75f;
    const float sx = (aspect <= 1 ? 1 : 1/aspect)/zoom;
    const float sy = (aspect <= 1 ? aspect : 1)/zoom;
    const float width = static_cast<float>(view.viewport.width), height = static_cast<float>(view.viewport.height);
    auto& p = camera.projection;
    p.x_scale = sx*view.projection_anchor.width/width;
    p.y_scale = sy*view.projection_anchor.height/height;
    p.x_offset = (2.f*view.projection_anchor.x+view.projection_anchor.width)/width-1;
    p.y_offset = 1-(2.f*view.projection_anchor.y+view.projection_anchor.height)/height;
    if (!std::isfinite(p.x_scale) || !std::isfinite(p.y_scale) ||
        !std::isfinite(p.x_offset) || !std::isfinite(p.y_offset))
        throw std::invalid_argument("Non-finite camera projection");
    const auto& t = camera.transform;
    camera.planes = {{
        plane(combine(t.right,p.x_scale,t.forward,1+p.x_offset),t.position),
        plane(combine(t.right,-p.x_scale,t.forward,1-p.x_offset),t.position),
        plane(combine(t.up,p.y_scale,t.forward,1+p.y_offset),t.position),
        plane(combine(t.up,-p.y_scale,t.forward,1-p.y_offset),t.position),
        plane(t.forward,t.position,-p.near_distance)
    }};
    return camera;
}
ClipPosition project(const Camera& camera, Vec3 world) {
    const auto p = subtract(world,camera.transform.position);
    const double x = dot(p,camera.transform.right), y = dot(p,camera.transform.up), z = dot(p,camera.transform.forward);
    const auto& projection = camera.projection;
    return {x*projection.x_scale+z*projection.x_offset, y*projection.y_scale+z*projection.y_offset,
        z-2*projection.near_distance,z};
}
bool sphereIntersects(const Camera& camera, Vec3 center, float radius) {
    if (!std::isfinite(radius) || radius < 0) throw std::invalid_argument("Invalid sphere radius");
    for (const auto& plane : camera.planes)
        if (distance(plane,center) < -radius-tolerance(plane,center)) return false;
    return true;
}
ViewVisibility queryView(const Presentation& frame, const View& view) {
    ViewVisibility result;
    result.camera = prepareCamera(view);
    result.frame_sequence = frame.data().sequence;
    result.scene_lifetime = frame.data().scene_lifetime;
    result.history_key = view.history_key;
    const auto& rooms = frame.data().rooms;
    for (size_t r = 0; r < rooms.size(); ++r) {
        ++result.stats.rooms_considered;
        const auto& room = rooms[r];
        const auto& geometry = room.asset->geometry();
        for (size_t f = 0; f < geometry.polygons.size(); ++f) {
            const auto& surface = room.surfaces[f];
            if (!surface.draw_surface || surface.bitmap < 0) continue;
            ++result.stats.surfaces_considered;
            const auto& polygon = geometry.polygons[f];
            const auto point = geometry.vertices[polygon.corners[0].vertex];
            if (dot(polygon.normal,subtract(view.camera.position,point)) < 0) {
                ++result.stats.surfaces_backfacing;
                continue;
            }
            if (polygonOutside(result.camera,geometry,polygon)) {
                ++result.stats.surfaces_outside;
                continue;
            }
            bool blended = surface.alpha < .999f || surface.additive;
            for (const auto& corner : polygon.corners) blended = blended || corner.alpha < 255;
            const float depth = static_cast<float>(dot(view.camera.forward,subtract(point,view.camera.position)));
            result.surfaces.push_back({static_cast<uint32_t>(r),static_cast<uint32_t>(f),depth,blended});
        }
    }
    std::stable_sort(result.surfaces.begin(),result.surfaces.end(),[](const VisibleSurface& a,const VisibleSurface& b) {
        if (a.blended != b.blended) return !a.blended;
        return a.blended ? a.depth > b.depth : a.depth < b.depth;
    });
    for (size_t i = 0; i < frame.data().objects.size(); ++i) {
        const auto& object = frame.data().objects[i];
        if (view.exclude_object && object.handle == view.excluded_object) continue;
        if (sphereIntersects(result.camera,object.current.position,object.radius))
            result.object_indices.push_back(static_cast<uint32_t>(i));
    }
    return result;
}
} // namespace d3scene
