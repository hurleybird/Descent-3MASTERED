// Descent 3MASTERED - GPL-3.0-or-later
#include "SceneView.h"
#include "SceneGeometry.h"
#include "ScenePortals.h"
#include "SceneTerrain.h"
#include <unordered_map>
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
bool polygonOutside(const Camera& camera, MeshView geometry, const MeshPolygon& polygon, const Transform& world,PortalWindow window) {
    // A separating frustum plane proves rejection. Not finding one deliberately
    // keeps possible corner false positives: never reject a large polygon just
    // because all of its vertices lie outside different planes.
    for (const auto& plane : camera.planes) {
        bool all_outside = true;
        for (const auto& corner : polygon.corners) {
            const auto point = transformPoint(world,geometry.positions[corner.vertex]);
            if (distance(plane,point) >= -tolerance(plane,point)) { all_outside = false; break; }
        }
        if (all_outside) return true;
    }
    // Homogeneous aperture tests share the exact projection used by execution.
    // A union window can admit extra geometry, but cannot omit either opening.
    bool left = true, right = true, bottom = true, top = true;
    for (const auto& corner : polygon.corners) {
        const auto p = project(camera,transformPoint(world,geometry.positions[corner.vertex]));
        const double epsilon = 2.e-6*(1+std::abs(p.x)+std::abs(p.y)+std::abs(p.w));
        left = left && p.x < window.left*p.w-epsilon;
        right = right && p.x > window.right*p.w+epsilon;
        bottom = bottom && p.y < window.bottom*p.w-epsilon;
        top = top && p.y > window.top*p.w+epsilon;
    }
    if (left || right || bottom || top) return true;
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
View resolveView(const Presentation& frame, View view) {
    if (view.attached_camera) for (const auto& effect : frame.data().camera_perturbations) {
        if (effect.owner != view.camera_owner) continue;
        view.camera.right = transformDirection(effect.rotation,view.camera.right);
        view.camera.up = transformDirection(effect.rotation,view.camera.up);
        view.camera.forward = transformDirection(effect.rotation,view.camera.forward);
    }
    view.attached_camera = false; // resolved views cannot apply an effect twice
    return view;
}
ViewVisibility queryView(const Presentation& frame, const View& request) {
    const auto view = resolveView(frame,request);
    ViewVisibility result;
    result.camera = prepareCamera(view);
    result.frame_sequence = frame.data().sequence;
    result.scene_lifetime = frame.data().scene_lifetime;
    result.history_key = view.history_key;
    const auto& rooms = frame.data().rooms;
    const auto windows = queryPortalWindows(frame,view,result.camera);
    std::unordered_map<int32_t,size_t> room_indices;
    for (size_t i = 0; i < rooms.size(); ++i) room_indices.emplace(rooms[i].number,i);
    auto consider = [&](MeshView geometry, const SurfaceBinding& surface, VisibleSurface item,PortalWindow window) {
        if (!surface.draw_surface || (!item.model && surface.bitmap < 0)) return;
        ++result.stats.surfaces_considered;
        const auto& polygon = geometry.topology.polygons[item.polygon_index];
        const auto point = transformPoint(item.world,geometry.positions[polygon.corners[0].vertex]);
        const auto normal = transformDirection(item.world,polygon.normal);
        if (dot(normal,subtract(view.camera.position,point)) < 0) {
            ++result.stats.surfaces_backfacing;
            return;
        }
        if (polygonOutside(result.camera,geometry,polygon,item.world,window)) {
            ++result.stats.surfaces_outside;
            return;
        }
        item.blended = surface.material.blended();
        item.depth = static_cast<float>(dot(view.camera.forward,subtract(point,view.camera.position)));
        result.surfaces.push_back(item);
    };
    for (size_t r = 0; r < rooms.size(); ++r) {
        ++result.stats.rooms_considered;
        if (!windows[r].visible) continue;
        result.room_indices.push_back(static_cast<uint32_t>(r));
        const auto& room = rooms[r];
        const auto& geometry = room.asset->geometry();
        for (size_t f = 0; f < geometry.polygons.size(); ++f) {
            consider(MeshView(geometry),room.surfaces[f],{static_cast<uint32_t>(r),static_cast<uint32_t>(f)},windows[r]);
        }
    }
    for (size_t i = 0; i < frame.data().objects.size(); ++i) {
        const auto& object = frame.data().objects[i];
        if (view.exclude_object && object.handle == view.excluded_object) continue;
        PortalWindow window{-1,-1,1,1,true};
        const auto room = room_indices.find(object.room);
        if (room != room_indices.end()) window = windows[room->second];
        if (!window.visible) continue;
        if (sphereIntersects(result.camera,object.current.position,object.radius))
            result.object_indices.push_back(static_cast<uint32_t>(i));
        if (!object.model) continue;
        // Until deformed/animated bounds are compiled, a source object sphere
        // is not proof that its posed mesh is offscreen. Test authored polygons.
        for (size_t part = 0; part < object.parts.size(); ++part) {
            if (!object.parts[part].draw) continue;
            const auto world = compose(object.current,object.parts[part].current);
            const auto& geometry = object.model->geometry().parts[part].geometry;
            for (size_t face = 0; face < geometry.polygons.size(); ++face) {
                VisibleSurface item;
                item.model = true; item.object_index = static_cast<uint32_t>(i);
                item.part_index = static_cast<uint32_t>(part); item.polygon_index = static_cast<uint32_t>(face);
                item.world = world;
                consider(MeshView(geometry,object.parts[part].vertices),object.parts[part].surfaces[face],item,window);
            }
        }
    }
    if (frame.data().terrain.asset) {
        PortalWindow exterior_window{};
        if (!view.portal_visibility || !room_indices.count(view.room)) exterior_window = {-1,-1,1,1,true};
        else for (size_t room = 0; room < rooms.size(); ++room) if (rooms[room].exterior && windows[room].visible) {
            const auto& window = windows[room];
            if (!exterior_window.visible) exterior_window = window;
            else exterior_window = {std::min(exterior_window.left,window.left),std::min(exterior_window.bottom,window.bottom),
                std::max(exterior_window.right,window.right),std::max(exterior_window.top,window.top),true};
        }
        if (exterior_window.visible) {
            const auto& terrain = frame.data().terrain;
            for (size_t patch_index = 0; patch_index < terrain.asset->patches().size(); ++patch_index) {
                const auto& patch = terrain.asset->patches()[patch_index];
                MeshGeometry hull; MeshPolygon polygon;
                for (uint32_t corner = 0; corner < 8; ++corner) {
                    hull.vertices.push_back({corner&1 ? patch.bounds.maximum.x : patch.bounds.minimum.x,
                        corner&2 ? patch.bounds.maximum.y : patch.bounds.minimum.y,corner&4 ? patch.bounds.maximum.z : patch.bounds.minimum.z});
                    polygon.corners.push_back({corner});
                }
                if (polygonOutside(result.camera,MeshView(hull),polygon,{},exterior_window)) continue;
                const Vec3 center{(patch.bounds.minimum.x+patch.bounds.maximum.x)*.5f,
                    (patch.bounds.minimum.y+patch.bounds.maximum.y)*.5f,(patch.bounds.minimum.z+patch.bounds.maximum.z)*.5f};
                for (size_t batch = 0; batch < patch.batches.size(); ++batch) {
                    const auto& surface = terrain.patches[patch_index][batch];
                    if (!surface.draw_surface) continue;
                    VisibleSurface item; item.terrain = true;
                    item.terrain_patch = static_cast<uint32_t>(patch_index); item.terrain_batch = static_cast<uint32_t>(batch);
                    item.blended = surface.material.blended();
                    item.depth = static_cast<float>(dot(view.camera.forward,subtract(center,view.camera.position)));
                    result.surfaces.push_back(item);
                }
            }
        }
    }
    std::stable_sort(result.surfaces.begin(),result.surfaces.end(),[](const VisibleSurface& a,const VisibleSurface& b) {
        if (a.blended != b.blended) return !a.blended;
        return a.blended ? a.depth > b.depth : a.depth < b.depth;
    });
    return result;
}
} // namespace d3scene
