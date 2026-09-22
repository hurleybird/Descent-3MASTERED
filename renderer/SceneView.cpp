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
bool boundsOutside(const Camera& camera,const Bounds& bounds,PortalWindow window) {
    std::array<Vec3,8> corners;
    for (unsigned i = 0; i < corners.size(); ++i)
        corners[i] = {i&1 ? bounds.maximum.x : bounds.minimum.x,
            i&2 ? bounds.maximum.y : bounds.minimum.y,i&4 ? bounds.maximum.z : bounds.minimum.z};
    for (const auto& plane : camera.planes) {
        bool outside = true;
        for (const auto point : corners)
            if (distance(plane,point) >= -tolerance(plane,point)) { outside = false; break; }
        if (outside) return true;
    }
    bool left = true, right = true, bottom = true, top = true;
    for (const auto point : corners) {
        const auto p = project(camera,point);
        const double epsilon = 2.e-6*(1+std::abs(p.x)+std::abs(p.y)+std::abs(p.w));
        left = left && p.x < window.left*p.w-epsilon;
        right = right && p.x > window.right*p.w+epsilon;
        bottom = bottom && p.y < window.bottom*p.w-epsilon;
        top = top && p.y > window.top*p.w+epsilon;
    }
    return left || right || bottom || top;
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
ViewMatrices viewMatrices(const Camera& camera) {
    ViewMatrices result;
    const auto& p = camera.projection;
    const auto& t = camera.transform;
    result.projection = {{p.x_scale,0,0,0, 0,p.y_scale,0,0,
        p.x_offset,p.y_offset,1,1, 0,0,-2*p.near_distance,0}};
    result.inverse_view = {{t.right.x,t.right.y,t.right.z,0, t.up.x,t.up.y,t.up.z,0,
        t.forward.x,t.forward.y,t.forward.z,0, t.position.x,t.position.y,t.position.z,1}};
    const std::array<float,16> world_view{{t.right.x,t.up.x,t.forward.x,0,
        t.right.y,t.up.y,t.forward.y,0, t.right.z,t.up.z,t.forward.z,0,
        float(-dot(t.right,t.position)),float(-dot(t.up,t.position)),float(-dot(t.forward,t.position)),1}};
    for (int column = 0; column < 4; ++column) for (int row = 0; row < 4; ++row)
        for (int inner = 0; inner < 4; ++inner)
            result.view_projection[column*4+row] += result.projection[inner*4+row]*world_view[column*4+inner];
    return result;
}
Vec3 unproject(const Camera& camera,float x,float y,float depth) {
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(depth) || depth < 0 || depth >= 1)
        throw std::invalid_argument("Cannot unproject a non-finite or background depth sample");
    const auto& p = camera.projection;
    const float z = p.near_distance/(1-depth);
    return transformPoint(camera.transform,{z*(x-p.x_offset)/p.x_scale,z*(y-p.y_offset)/p.y_scale,z});
}
Transform resolveMeshTransform(const PreparedMesh& instance,const Camera& camera) {
    auto world = instance.world;
    auto direction = camera.transform.forward;
    if (instance.eye_distance > 0 || instance.alignment == MeshAlignment::FacingEye) {
        direction = subtract(world.position,camera.transform.position);
        const auto length = std::sqrt(dot(direction,direction));
        direction = length > 1.e-8 ? combine(direction,static_cast<float>(1/length),{},0) : camera.transform.forward;
    }
    if (instance.eye_distance > 0)
        world.position = combine(camera.transform.position,1,direction,instance.eye_distance);
    if (instance.alignment == MeshAlignment::Axis) {
        // end-start cross (start-eye), equivalent using the center because
        // moving along the segment does not change the perpendicular.
        const auto axis = world.up;
        const auto to_eye = subtract(camera.transform.position,world.position);
        Vec3 side{axis.y*to_eye.z-axis.z*to_eye.y,axis.z*to_eye.x-axis.x*to_eye.z,axis.x*to_eye.y-axis.y*to_eye.x};
        const double length2 = dot(side,side);
        const double radius = std::sqrt(dot(world.right,world.right));
        // An end-on or zero-length rod has zero projected area. Collapse it;
        // never normalize zero or choose a camera-dependent random meridian.
        world.right = length2 > 1.e-16 ? combine(side,static_cast<float>(radius/std::sqrt(length2)),{},0) : Vec3{};
    } else if (instance.alignment == MeshAlignment::Camera) {
        world.right = transformDirection(camera.transform,world.right);
        world.up = transformDirection(camera.transform,world.up);
        world.forward = transformDirection(camera.transform,world.forward);
    } else if (instance.alignment == MeshAlignment::FacingEye || instance.alignment == MeshAlignment::CameraUpright) {
        if (instance.alignment == MeshAlignment::CameraUpright) direction = camera.transform.forward;
        Transform facing;
        facing.forward = direction;
        if (direction.x == 0 && direction.z == 0) {
            // At a camera pole, bank is undefined. Preserve that view's basis,
            // rather than snapping an upright overlay to an arbitrary meridian.
            if (instance.alignment == MeshAlignment::CameraUpright) facing = camera.transform;
            else { facing.right = {1,0,0}; facing.up = {0,0,direction.y < 0 ? 1.f : -1.f}; }
        } else {
            const float inverse = 1/std::sqrt(direction.x*direction.x+direction.z*direction.z);
            facing.right = {direction.z*inverse,0,-direction.x*inverse};
            facing.up = {direction.y*facing.right.z,direction.z*facing.right.x-direction.x*facing.right.z,-direction.y*facing.right.x};
        }
        world.right = transformDirection(facing,world.right);
        world.up = transformDirection(facing,world.up);
        world.forward = transformDirection(facing,world.forward);
    }
    world.position.x += instance.camera_translation.x*camera.transform.position.x;
    world.position.y += instance.camera_translation.y*camera.transform.position.y;
    world.position.z += instance.camera_translation.z*camera.transform.position.z;
    return world;
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
        if (!object.visible) continue;
        if (view.exclude_object && object.handle == view.excluded_object) continue;
        PortalWindow window{-1,-1,1,1,true};
        const auto room = room_indices.find(object.room);
        if (room != room_indices.end()) window = windows[room->second];
        if (!window.visible) continue;
        if (object.model && (!object.has_render_bounds || boundsOutside(result.camera,object.render_bounds,window))) continue;
        if (object.model || sphereIntersects(result.camera,object.current.position,object.radius))
            result.object_indices.push_back(static_cast<uint32_t>(i));
        if (!object.model) continue;
        // Publication compiled these bounds from the same pose/deformation as
        // execution. The simulation radius is not a rendering visibility bound.
        for (size_t part = 0; part < object.parts.size(); ++part) {
            if (!object.parts[part].draw) continue;
            if (!object.parts[part].has_render_bounds || boundsOutside(result.camera,object.parts[part].render_bounds,window)) continue;
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
    {
        PortalWindow exterior_window{};
        if (!view.portal_visibility || !room_indices.count(view.room)) exterior_window = {-1,-1,1,1,true};
        else for (size_t room = 0; room < rooms.size(); ++room) if (rooms[room].exterior && windows[room].visible) {
            const auto& window = windows[room];
            if (!exterior_window.visible) exterior_window = window;
            else exterior_window = {std::min(exterior_window.left,window.left),std::min(exterior_window.bottom,window.bottom),
                std::max(exterior_window.right,window.right),std::max(exterior_window.top,window.top),true};
        }
        result.exterior_visible = exterior_window.visible;
        if (exterior_window.visible && frame.data().terrain.asset) {
            const auto& terrain = frame.data().terrain;
            for (size_t patch_index = 0; patch_index < terrain.asset->patches().size(); ++patch_index) {
                const auto& patch = terrain.asset->patches()[patch_index];
                if (boundsOutside(result.camera,patch.bounds,exterior_window)) continue;
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
    std::unordered_map<uint32_t,const ObjectState*> owners;
    for (const auto& object : frame.data().objects) owners.emplace(object.handle,&object);
    for (size_t index = 0; index < frame.data().meshes.size(); ++index) {
        const auto& instance = frame.data().meshes[index];
        if (instance.has_owner && ((view.exclude_object && instance.owner == view.excluded_object) ||
            !owners.at(instance.owner)->visible)) continue;
        const auto& geometry = instance.asset->geometry();
        if (!instance.surface.draw_surface || geometry.polygons.empty() ||
            (instance.surface.material.layer == RenderLayer::Background && !result.exterior_visible)) continue;
        VisibleSurface item; item.mesh = true; item.mesh_index = static_cast<uint32_t>(index);
        item.world = resolveMeshTransform(instance,result.camera);
        PortalWindow window{-1,-1,1,1,true};
        if (instance.surface.material.layer == RenderLayer::World && view.portal_visibility && room_indices.count(instance.room)) {
            window = windows[room_indices.at(instance.room)];
            if (!window.visible) continue;
        }
        // Pixel-expanded markers can straddle a clip boundary while their
        // centers are outside. Submit the batch; homogeneous GPU clipping sees
        // the expanded vertices and is authoritative for this representation.
        bool visible = !geometry.raster_offsets.empty();
        if (!visible) for (const auto& polygon : geometry.polygons)
            if (!polygonOutside(result.camera,MeshView(geometry),polygon,item.world,window)) { visible = true; break; }
        if (!visible) continue;
        item.layer = instance.surface.material.layer;
        item.blended = instance.surface.material.blended();
        item.depth = static_cast<float>(dot(view.camera.forward,subtract(item.world.position,view.camera.position)));
        result.surfaces.push_back(item);
    }
    std::stable_sort(result.surfaces.begin(),result.surfaces.end(),[](const VisibleSurface& a,const VisibleSurface& b) {
        if (a.layer != b.layer) return a.layer == RenderLayer::Background;
        if (a.layer == RenderLayer::Background) return false; // retain authored compositing order
        if (a.blended != b.blended) return !a.blended;
        return a.blended ? a.depth > b.depth : a.depth < b.depth;
    });
    return result;
}
const SurfaceBinding& visibleSurfaceBinding(const Presentation& frame,const VisibleSurface& item) {
    const auto& data = frame.data();
    if (item.mesh) return data.meshes[item.mesh_index].surface;
    if (item.terrain) return data.terrain.patches[item.terrain_patch][item.terrain_batch];
    if (item.model) return data.objects[item.object_index].parts[item.part_index].surfaces[item.polygon_index];
    return data.rooms[item.room_index].surfaces[item.polygon_index];
}
std::vector<ViewPass> buildViewPasses(const Presentation& frame,const ViewVisibility& view,ViewProducts products) {
    if (products.occlusion_radiance && !products.opaque_scene)
        throw std::invalid_argument("World occlusion requires an opaque scene product");
    std::vector<ViewPass> passes;
    uint32_t index = 0;
    while (index < view.surfaces.size() && view.surfaces[index].layer == RenderLayer::Background) ++index;
    if (index) passes.push_back({ViewPassKind::Background,0,index});
    const auto opaque = index;
    while (index < view.surfaces.size() && !view.surfaces[index].blended) ++index;
    if (index > opaque) passes.push_back({ViewPassKind::OpaqueWorld,opaque,index});
    bool depth_required = false;
    for (size_t i = index; i < view.surfaces.size(); ++i)
        depth_required = depth_required || visibleSurfaceBinding(frame,view.surfaces[i]).soft_depth_range > 0;
    if (depth_required) passes.push_back({ViewPassKind::SnapshotOpaqueDepth,index,index});
    if (products.opaque_scene) passes.push_back({ViewPassKind::ExportOpaqueScene,index,index});
    if (index < view.surfaces.size()) passes.push_back({ViewPassKind::BlendedWorld,index,static_cast<uint32_t>(view.surfaces.size())});
    if (products.occlusion_radiance) passes.push_back({ViewPassKind::CompositeWorldOcclusion,index,index});
    return passes;
}
} // namespace d3scene
