// Descent 3MASTERED - GPL-3.0-or-later
#include "SceneView.h"
#include "SceneGeometry.h"
#include "ScenePortals.h"
#include "SceneTerrain.h"
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <limits>
#include <set>
#include <thread>

namespace d3scene {
struct ViewStoragePool {
    struct Buffers {
        std::vector<VisibleSurface> surfaces;
        std::vector<uint32_t> objects,rooms;
    };
    std::array<Buffers,4> spare;
    size_t count = 0;
    const std::thread::id publisher = std::this_thread::get_id();
};
ViewVisibility::ViewVisibility() {
    static thread_local auto pool = std::make_shared<ViewStoragePool>();
    storage_pool_ = pool;
    if (pool->count) {
        auto& buffers = pool->spare[--pool->count];
        surfaces.swap(buffers.surfaces); object_indices.swap(buffers.objects); room_indices.swap(buffers.rooms);
    }
}
ViewVisibility::~ViewVisibility() {
    // Only released storage returns to its preparing thread. Held/copied views
    // own independent vectors; foreign-thread destruction never touches spares.
    const auto& pool = storage_pool_;
    if (!pool || pool->publisher != std::this_thread::get_id() || pool->count == pool->spare.size()) return;
    auto& buffers = pool->spare[pool->count++];
    surfaces.clear(); object_indices.clear(); room_indices.clear();
    surfaces.swap(buffers.surfaces); object_indices.swap(buffers.objects); room_indices.swap(buffers.rooms);
}
static ViewVisibility queryResolvedView(const Presentation&,const View&);
PreparedView prepareView(const Presentation& frame,const View& request) {
    auto view = resolveView(frame,request);
    auto visibility = queryResolvedView(frame,view);
    return PreparedView(frame.shared_from_this(),std::move(view),std::move(visibility));
}
Rect rasterViewport(Rect logical, int32_t raster_height, int32_t scale, int32_t offset_x, int32_t offset_y) {
    if (raster_height <= 0 || scale <= 0 || logical.width <= 0 || logical.height <= 0)
        throw std::invalid_argument("Invalid raster viewport extent");
    const auto narrow = [](int64_t value) {
        if (value < (std::numeric_limits<int32_t>::min)() || value > (std::numeric_limits<int32_t>::max)())
            throw std::invalid_argument("Raster viewport overflow");
        return static_cast<int32_t>(value);
    };
    return {narrow((int64_t(logical.x)+offset_x)*scale),
        narrow(int64_t(raster_height)-(int64_t(logical.y)+logical.height+offset_y)*scale),
        narrow(int64_t(logical.width)*scale),narrow(int64_t(logical.height)*scale)};
}
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
}
bool boundsOutside(const Camera& camera,const Bounds& bounds,PortalWindow window) {
    for (const auto& plane : camera.planes) {
        // The support corner maximizes signed distance plus our tolerance:
        // n*x + epsilon*abs(n*x) is monotonic with the sign of n for epsilon<1.
        // Testing all eight corners against this plane is therefore redundant.
        const Vec3 support{plane.normal.x >= 0 ? bounds.maximum.x : bounds.minimum.x,
            plane.normal.y >= 0 ? bounds.maximum.y : bounds.minimum.y,
            plane.normal.z >= 0 ? bounds.maximum.z : bounds.minimum.z};
        if (distance(plane,support) < -tolerance(plane,support)) return true;
    }
    std::array<Vec3,8> corners;
    for (unsigned i = 0; i < corners.size(); ++i)
        corners[i] = {i&1 ? bounds.maximum.x : bounds.minimum.x,
            i&2 ? bounds.maximum.y : bounds.minimum.y,i&4 ? bounds.maximum.z : bounds.minimum.z};
    bool left = true, right = true, bottom = true, top = true;
    for (const auto point : corners) {
        const auto p = project(camera,point);
        const double epsilon = 2.e-6*(1+std::abs(p.x)+std::abs(p.y)+std::abs(p.w));
        left = left && p.x < window.left*p.w-epsilon;
        right = right && p.x > window.right*p.w+epsilon;
        bottom = bottom && p.y < window.bottom*p.w-epsilon;
        top = top && p.y > window.top*p.w+epsilon;
        if (!left && !right && !bottom && !top) return false;
    }
    return left || right || bottom || top;
}
namespace {
struct ProjectedVertex { Vec3 world; uint16_t outside = UINT16_MAX; uint64_t generation = 0; };
struct ProjectionScratch { std::vector<ProjectedVertex> vertices; uint64_t generation = 0; };
// One cache per geometry placement/aperture, not per face or clip plane.
// Scratch retains capacity only; generation separates every view and placement.
class ProjectedMesh {
    const Camera& camera_;
    MeshView geometry_;
    Transform placement_;
    PortalWindow window_;
    ProjectionScratch& scratch_;
    uint64_t generation_;
public:
    ProjectedMesh(const Camera& camera,MeshView geometry,Transform placement,PortalWindow window,ProjectionScratch& scratch)
        : camera_(camera),geometry_(geometry),placement_(placement),window_(window),scratch_(scratch) {
        if (++scratch_.generation == 0) {
            for (auto& vertex : scratch_.vertices) vertex.generation = 0;
            ++scratch_.generation;
        }
        generation_ = scratch_.generation;
        if (scratch_.vertices.size() < geometry.positions.size()) scratch_.vertices.resize(geometry.positions.size());
    }
    const ProjectedVertex& vertex(uint32_t index) {
        auto& result = scratch_.vertices[index];
        if (result.generation == generation_) return result;
        result.world = transformPoint(placement_,geometry_.positions[index]);
        result.outside = UINT16_MAX; // uncomputed; valid clip codes use ten bits
        result.generation = generation_;
        return result;
    }
    uint16_t clipCode(uint32_t index) {
        // A plane-side/backface query needs only the world point. Do not pay
        // projection and aperture tests until a surviving polygon asks for them.
        vertex(index);
        auto& result = scratch_.vertices[index];
        if (result.outside != UINT16_MAX) return result.outside;
        uint16_t outside = 0;
        for (size_t i = 0; i < camera_.planes.size(); ++i) {
            const auto& plane = camera_.planes[i];
            if (distance(plane,result.world) < -tolerance(plane,result.world)) outside |= uint16_t(1)<<i;
        }
        const auto p = project(camera_,result.world);
        const double epsilon = 2.e-6*(1+std::abs(p.x)+std::abs(p.y)+std::abs(p.w));
        if (p.x < window_.left*p.w-epsilon) outside |= 1<<6;
        if (p.x > window_.right*p.w+epsilon) outside |= 1<<7;
        if (p.y < window_.bottom*p.w-epsilon) outside |= 1<<8;
        if (p.y > window_.top*p.w+epsilon) outside |= 1<<9;
        result.outside = outside;
        return outside;
    }
    bool outside(const MeshPolygon& polygon) {
        // AND, not OR: vertices outside different planes do not prove that
        // the polygon is outside. Keep the original conservative predicate.
        uint16_t common = (1<<10)-1;
        for (const auto& corner : polygon.corners) {
            common &= clipCode(corner.vertex);
            if (!common) return false;
        }
        return true;
    }
};
}

Camera prepareCamera(const View& view) {
    validateView(view);
    Camera camera;
    camera.transform = view.camera;
    // Keep operation order identical to the first verified GPU projection.
    const float aspect = static_cast<float>(view.projection_anchor.width)/view.projection_anchor.height;
    const float zoom = view.zoom*.75f;
    const bool orthographic = view.projection == ViewProjection::Orthographic;
    const float sx = orthographic ? 2/view.orthographic_size.x : (aspect <= 1 ? 1 : 1/aspect)/zoom;
    const float sy = orthographic ? 2/view.orthographic_size.y : (aspect <= 1 ? aspect : 1)/zoom;
    const float width = static_cast<float>(view.viewport.width), height = static_cast<float>(view.viewport.height);
    auto& p = camera.projection;
    p.x_scale = sx*view.projection_anchor.width/width;
    p.y_scale = sy*view.projection_anchor.height/height;
    p.x_offset = (2.f*view.projection_anchor.x+view.projection_anchor.width)/width-1;
    p.y_offset = 1-(2.f*view.projection_anchor.y+view.projection_anchor.height)/height;
    if (orthographic) {
        p.near_distance = view.orthographic_depth.x;
        p.orthographic_depth_scale = 2/(view.orthographic_depth.y-view.orthographic_depth.x);
        if (!(p.orthographic_depth_scale > 0) || !std::isfinite(p.orthographic_depth_scale))
            throw std::invalid_argument("Invalid orthographic depth scale");
    }
    if (!std::isfinite(p.x_scale) || !std::isfinite(p.y_scale) ||
        !std::isfinite(p.x_offset) || !std::isfinite(p.y_offset))
        throw std::invalid_argument("Non-finite camera projection");
    const auto& t = camera.transform;
    if (orthographic) camera.planes = {{
        plane(combine(t.right,p.x_scale,{},0),t.position,1+p.x_offset),
        plane(combine(t.right,-p.x_scale,{},0),t.position,1-p.x_offset),
        plane(combine(t.up,p.y_scale,{},0),t.position,1+p.y_offset),
        plane(combine(t.up,-p.y_scale,{},0),t.position,1-p.y_offset),
        plane(t.forward,t.position,-p.near_distance)
    }};
    else camera.planes = {{
        plane(combine(t.right,p.x_scale,t.forward,1+p.x_offset),t.position),
        plane(combine(t.right,-p.x_scale,t.forward,1-p.x_offset),t.position),
        plane(combine(t.up,p.y_scale,t.forward,1+p.y_offset),t.position),
        plane(combine(t.up,-p.y_scale,t.forward,1-p.y_offset),t.position),
        plane(t.forward,t.position,-p.near_distance)
    }};
    if (view.has_clip_plane) camera.planes[5] = view.clip_plane;
    return camera;
}
bool reversesWinding(const Transform& camera) {
    const Vec3 cross{camera.right.y*camera.up.z-camera.right.z*camera.up.y,
        camera.right.z*camera.up.x-camera.right.x*camera.up.z,
        camera.right.x*camera.up.y-camera.right.y*camera.up.x};
    return dot(cross,camera.forward) < 0;
}
View reflectedView(const Presentation& frame,View parent,Plane mirror,uint64_t history_key,int32_t room) {
    validateView(parent);
    if (parent.collection || parent.target != ViewTarget::Scene || parent.ordering != ViewOrdering::Depth ||
        parent.projection != ViewProjection::Perspective || parent.has_clip_plane || parent.kind == ViewKind::Mirror ||
        !history_key || history_key == parent.history_key)
        throw std::invalid_argument("Unsupported reflected view parent or identity");
    bool found = false;
    for (const auto& candidate : frame.data().rooms) found = found || candidate.number == room;
    if (!found) throw std::invalid_argument("Reflected view has no published spatial root");
    const double length = std::sqrt(dot(mirror.normal,mirror.normal));
    if (!(length > 0) || !std::isfinite(length) || !std::isfinite(mirror.constant))
        throw std::invalid_argument("Invalid mirror plane");
    mirror.normal = combine(mirror.normal,static_cast<float>(1/length),{},0);
    mirror.constant = static_cast<float>(mirror.constant/length);
    // Resolve attachment/perturbation before reflecting, exactly once.
    // Exclusion policy remains explicit; a mirror producer can include the
    // camera rig by clearing exclude_camera_rig on its unresolved request.
    auto result = resolveView(frame,parent);
    if (distance(mirror,result.camera.position) < 0) {
        mirror.normal = combine(mirror.normal,-1,{},0); mirror.constant = -mirror.constant;
    }
    const auto reflect_direction = [&](Vec3 direction) {
        return combine(direction,1,mirror.normal,static_cast<float>(-2*dot(mirror.normal,direction)));
    };
    result.camera.position = combine(result.camera.position,1,mirror.normal,
        static_cast<float>(-2*distance(mirror,result.camera.position)));
    result.camera.right = reflect_direction(result.camera.right);
    result.camera.up = reflect_direction(result.camera.up);
    result.camera.forward = reflect_direction(result.camera.forward);
    result.kind = ViewKind::Mirror; result.history_key = history_key;
    result.feedback = NoFeedback; result.room = room; result.exterior_origin = false;
    result.has_clip_plane = true; result.clip_plane = mirror;
    result.fog_path_origin = FogPathOrigin::ClipPlane;
    result.reflections = ReflectionPolicy::SurfaceFilm;
    validateView(result);
    return result;
}
ViewMatrices viewMatrices(const Camera& camera) {
    ViewMatrices result;
    const auto& p = camera.projection;
    const auto& t = camera.transform;
    result.projection = {{p.x_scale,0,0,0, 0,p.y_scale,0,0,
        p.x_offset,p.y_offset,1,1, 0,0,-2*p.near_distance,0}};
    if (p.orthographic_depth_scale > 0)
        result.projection = {{p.x_scale,0,0,0, 0,p.y_scale,0,0,
            0,0,p.orthographic_depth_scale,0, p.x_offset,p.y_offset,-1-p.near_distance*p.orthographic_depth_scale,1}};
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
    const bool orthographic = p.orthographic_depth_scale > 0;
    const float z = orthographic ? p.near_distance+2*depth/p.orthographic_depth_scale : p.near_distance/(1-depth);
    const float w = orthographic ? 1 : z;
    return transformPoint(camera.transform,{w*(x-p.x_offset)/p.x_scale,w*(y-p.y_offset)/p.y_scale,z});
}
Transform resolveMeshTransform(const PreparedMesh& instance,const Camera& camera,const Transform* attachment) {
    auto world = instance.world;
    if (attachment) world.position = transformPoint(*attachment,world.position);
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
    if (instance.size_fade_range.y > 0) {
        const float depth = static_cast<float>(dot(camera.transform.forward,subtract(world.position,camera.transform.position)));
        const float scale = std::max(0.f,std::min(1.f,(instance.size_fade_range.y-depth)/
            (instance.size_fade_range.y-instance.size_fade_range.x)));
        world.right = combine(world.right,scale,{},0);
        world.up = combine(world.up,scale,{},0);
    }
    return world;
}
ClipPosition project(const Camera& camera, Vec3 world) {
    const auto p = subtract(world,camera.transform.position);
    const double x = dot(p,camera.transform.right), y = dot(p,camera.transform.up), z = dot(p,camera.transform.forward);
    const auto& projection = camera.projection;
    if (projection.orthographic_depth_scale > 0)
        return {x*projection.x_scale+projection.x_offset,y*projection.y_scale+projection.y_offset,
            (z-projection.near_distance)*projection.orthographic_depth_scale-1,1};
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
    validateView(view);
    std::sort(view.image_inputs.begin(),view.image_inputs.end(),[](const ViewImageInput& a,const ViewImageInput& b) { return a.slot < b.slot; });
    for (const auto& selection : view.excluded_surfaces) {
        const auto room = std::find_if(frame.data().rooms.begin(),frame.data().rooms.end(),
            [&](const RoomState& candidate) { return candidate.asset->id() == selection.room; });
        if (room == frame.data().rooms.end() || selection.polygon >= room->asset->geometry().polygons.size())
            throw std::invalid_argument("View surface selection has stale or missing geometry");
    }
    std::sort(view.excluded_surfaces.begin(),view.excluded_surfaces.end(),[](const RoomSurfaceId& a,const RoomSurfaceId& b) {
        if (a.room.lifetime != b.room.lifetime) return a.room.lifetime < b.room.lifetime;
        if (a.room.revision != b.room.revision) return a.room.revision < b.room.revision;
        if (a.room.slot != b.room.slot) return a.room.slot < b.room.slot;
        return a.polygon < b.polygon;
    });
    view.excluded_surfaces.erase(std::unique(view.excluded_surfaces.begin(),view.excluded_surfaces.end()),view.excluded_surfaces.end());
    if (view.exclude_camera_rig) {
        const auto owner = std::find_if(frame.data().objects.begin(),frame.data().objects.end(),
            [&](const ObjectState& object) { return object.handle == view.camera_owner; });
        if (owner == frame.data().objects.end()) throw std::invalid_argument("Camera rig owner is not in this presentation");
        view.excluded_objects.push_back(owner->handle);
        view.excluded_objects.insert(view.excluded_objects.end(),owner->camera_hidden_objects.begin(),owner->camera_hidden_objects.end());
        view.exclude_camera_rig = false;
    }
    std::sort(view.excluded_objects.begin(),view.excluded_objects.end());
    view.excluded_objects.erase(std::unique(view.excluded_objects.begin(),view.excluded_objects.end()),view.excluded_objects.end());
    if (view.attached_camera) for (const auto& effect : frame.data().camera_perturbations) {
        if (effect.owner != view.camera_owner) continue;
        view.camera.right = transformDirection(effect.rotation,view.camera.right);
        view.camera.up = transformDirection(effect.rotation,view.camera.up);
        view.camera.forward = transformDirection(effect.rotation,view.camera.forward);
    }
    view.attached_camera = false; // resolved views cannot apply an effect twice
    return view;
}
bool excludesObject(const View& view,uint32_t handle) {
    return std::find(view.excluded_objects.begin(),view.excluded_objects.end(),handle) != view.excluded_objects.end();
}
bool excludesSurface(const View& view,RoomSurfaceId surface) {
    return std::find(view.excluded_surfaces.begin(),view.excluded_surfaces.end(),surface) != view.excluded_surfaces.end();
}
uint64_t viewImageSource(const View& view,uint64_t slot) {
    for (const auto& input : view.image_inputs) if (input.slot == slot) return input.image;
    throw std::invalid_argument("View has no source for its surface image slot");
}
std::vector<size_t> orderViewImages(const std::vector<ViewImageRequest>& images) {
    std::unordered_map<uint64_t,size_t> producers;
    for (size_t i = 0; i < images.size(); ++i) {
        validateViewImage(images[i]);
        if (!producers.emplace(images[i].image,i).second)
            throw std::invalid_argument("View image graph has duplicate producers");
    }
    std::vector<std::vector<size_t>> consumers(images.size());
    std::vector<size_t> remaining(images.size());
    std::set<size_t> ready;
    for (size_t i = 0; i < images.size(); ++i) {
        std::unordered_set<size_t> dependencies;
        for (const auto& input : images[i].view.image_inputs) {
            const auto source = producers.find(input.image);
            if (source == producers.end()) throw std::invalid_argument("View image graph has a missing producer");
            dependencies.insert(source->second);
        }
        remaining[i] = dependencies.size();
        for (const auto source : dependencies) consumers[source].push_back(i);
        if (dependencies.empty()) ready.insert(i);
    }
    std::vector<size_t> order;
    while (!ready.empty()) {
        const auto source = *ready.begin(); ready.erase(ready.begin()); order.push_back(source);
        for (const auto consumer : consumers[source]) if (!--remaining[consumer]) ready.insert(consumer);
    }
    if (order.size() != images.size()) throw std::invalid_argument("View image graph contains a dependency cycle");
    return order;
}
uint32_t selectModelVariant(const ModelInstance& object,const Camera& camera) {
    const auto depth = dot(camera.transform.forward,subtract(object.current.position,camera.transform.position));
    return object.lod_models[depth < object.lod_distances.x ? 0 : depth < object.lod_distances.y ? 1 : 2];
}
PlanarViewPlan planPlanarViews(const Presentation& frame,const View& parent,uint32_t samples) {
    validateView(parent);
    if (samples != 0 && samples != 2 && samples != 4 && samples != 8)
        throw std::invalid_argument("Invalid planar image sample count");
    PlanarViewPlan plan{prepareView(frame,parent),{}};
    if (parent.reflections == ReflectionPolicy::SurfaceFilm || parent.collection) return plan;
    if (parent.has_clip_plane || parent.kind == ViewKind::Mirror)
        throw std::invalid_argument("Reflected producers must declare terminal surface-film policy");
    const auto& visible = plan.consumer.visibility();
    std::unordered_set<uint64_t> slots;
    for (const auto& surface : visible.surfaces) {
        const auto slot = visibleSurfaceBinding(frame,surface).material.planar_slot;
        if (slot) slots.insert(slot);
    }
    if (slots.empty()) return plan;
    std::unordered_set<uint64_t> identities;
    identities.insert(parent.history_key);
    for (const auto& room : frame.data().rooms) for (const auto& group : room.mirrors) {
        const auto slot = room.surfaces[group.polygons.front()].material.planar_slot;
        if (!slots.count(slot)) continue;
        // Stable across camera movement and asset revision; revision-qualified
        // child selection invalidates history when topology actually changes.
        uint64_t identity = (parent.history_key^0x504c414e41520001ull)*1099511628211ull;
        identity = (identity^slot)*1099511628211ull;
        if (!identity || !identities.insert(identity).second)
            throw std::invalid_argument("Planar image identity collision");
        auto source = parent; source.exclude_camera_rig = false;
        source.reflections = ReflectionPolicy::SurfaceFilm;
        auto child = reflectedView(frame,source,group.plane,identity,room.number);
        for (auto polygon : group.polygons) child.excluded_surfaces.push_back({room.asset->id(),polygon});
        child.viewport.x = child.viewport.y = 0;
        ViewImageRequest image; image.image = identity; image.view = std::move(child); image.samples = samples;
        validateViewImage(image);
        plan.images.push_back(std::move(image));
        plan.consumer.view_.image_inputs.push_back({slot,identity});
    }
    validateView(plan.consumer.view_);
    std::sort(plan.consumer.view_.image_inputs.begin(),plan.consumer.view_.image_inputs.end(),
        [](const ViewImageInput& a,const ViewImageInput& b) { return a.slot < b.slot; });
    return plan;
}
ViewVisibility queryView(const Presentation& frame, const View& request) {
    const auto view = resolveView(frame,request);
    return queryResolvedView(frame,view);
}
static ViewVisibility queryResolvedView(const Presentation& frame,const View& view) {
    ViewVisibility result;
    result.ordering = view.ordering;
    result.camera = prepareCamera(view);
    result.frame_sequence = frame.data().sequence;
    result.scene_lifetime = frame.data().scene_lifetime;
    result.history_key = view.history_key;
    const auto& rooms = frame.data().rooms;
    const bool world_content = view.collection == 0;
    const auto collection = frame.collection(view.collection);
    PortalVisibility spatial;
    if (world_content) spatial = queryPortalVisibility(frame,view,result.camera);
    const auto& windows = spatial.rooms;
    const auto& room_indices = frame.roomIndices();
    const auto& exterior_window = spatial.exterior;
    result.exterior_visible = exterior_window.visible;
    const auto spatialWindow = [&](int32_t room) {
        if (!world_content) return PortalWindow{-1,-1,1,1,true};
        const auto found = room_indices.find(room);
        if (found != room_indices.end()) return windows[found->second];
        // Nonnegative values identify rooms; -1 is unbound content. Other
        // negative memberships identify exterior cells, not unbound geometry.
        if (room < -1) return exterior_window;
        return PortalWindow{-1,-1,1,1,true};
    };
    static thread_local ProjectionScratch projection_scratch;
    auto consider = [&](MeshView geometry,ProjectedMesh& projected, const SurfaceBinding& surface, VisibleSurface item,float alpha = 1) {
        if (!surface.draw_surface) return;
        ++result.stats.surfaces_considered;
        const auto& polygon = geometry.topology.polygons[item.polygon_index];
        const auto point = projected.vertex(polygon.corners[0].vertex).world;
        const auto normal = transformDirection(item.world,polygon.normal);
        if (dot(normal,subtract(view.camera.position,point)) < 0) {
            ++result.stats.surfaces_backfacing;
            return;
        }
        if (projected.outside(polygon)) {
            ++result.stats.surfaces_outside;
            return;
        }
        const auto policy = modulatedMaterialPolicy(surface,alpha);
        item.blended = policy.blended();
        item.late = policy.requiresLatePass();
        item.depth = static_cast<float>(dot(view.camera.forward,subtract(point,view.camera.position)));
        result.surfaces.push_back(item);
    };
    for (size_t r = 0; r < rooms.size(); ++r) {
        if (!world_content) break;
        ++result.stats.rooms_considered;
        if (!windows[r].visible) continue;
        result.room_indices.push_back(static_cast<uint32_t>(r));
        const auto& room = rooms[r];
        const auto& geometry = room.asset->geometry();
        ProjectedMesh projected(result.camera,MeshView(geometry),{},windows[r],projection_scratch);
        for (size_t f = 0; f < geometry.polygons.size(); ++f) {
            if (excludesSurface(view,{room.asset->id(),static_cast<uint32_t>(f)})) continue;
            consider(MeshView(geometry),projected,room.surfaces[f],{static_cast<uint32_t>(r),static_cast<uint32_t>(f)});
        }
    }
    const auto& owner_indices = frame.objectIndices();
    const auto& instances = frame.modelIndices();
    static thread_local std::vector<bool> model_owners,visible_owners;
    model_owners.assign(world_content ? frame.data().objects.size() : 0,false);
    visible_owners.assign(model_owners.size(),false);
    if (world_content) for (const auto index : collection.models) {
        const auto& instance = frame.data().model_instances[index];
        if (instance.has_owner) model_owners[owner_indices.at(instance.owner)] = true;
    }
    const auto eligible = [&](const ModelInstance& instance) {
        return instance.collection == view.collection && instance.visible && (!instance.has_owner || (frame.data().objects[owner_indices.at(instance.owner)].visible &&
            !excludesObject(view,instance.owner)));
    };
    for (size_t i = 0; i < frame.data().objects.size(); ++i) {
        const auto& object = frame.data().objects[i];
        if (!world_content) break;
        if (model_owners[i]) continue;
        if (!object.visible) continue;
        if (excludesObject(view,object.handle)) continue;
        const auto window = spatialWindow(object.room);
        if (!window.visible) continue;
        if (sphereIntersects(result.camera,object.current.position,object.radius))
            result.object_indices.push_back(static_cast<uint32_t>(i));
    }
    for (const auto i : collection.models) {
        const auto& object = frame.data().model_instances[i];
        if (!eligible(object)) continue;
        const auto window = spatialWindow(object.room);
        if (!window.visible || !object.has_render_bounds || boundsOutside(result.camera,object.render_bounds,window)) continue;
        const uint32_t variant = selectModelVariant(object,result.camera);
        const auto& model = frame.resolvedModel(i,variant);
        if (!model.has_render_bounds || boundsOutside(result.camera,model.render_bounds,window)) continue;
        if (world_content && object.has_owner) {
            const auto index = owner_indices.at(object.owner);
            if (!visible_owners[index]) result.object_indices.push_back(uint32_t(index));
            visible_owners[index] = true;
        }
        // Publication compiled these bounds from the same pose/deformation as
        // execution. The simulation radius is not a rendering visibility bound.
        for (size_t part = 0; part < model.parts.size(); ++part) {
            if (!model.parts[part].draw) continue;
            if (!model.parts[part].has_render_bounds || boundsOutside(result.camera,model.parts[part].render_bounds,window)) continue;
            const auto world = compose(object.current,model.parts[part].current);
            const auto& geometry = model.asset->geometry().parts[part].geometry;
            const MeshView mesh(geometry,model.parts[part].vertices);
            ProjectedMesh projected(result.camera,mesh,world,window,projection_scratch);
            for (size_t face = 0; face < geometry.polygons.size(); ++face) {
                VisibleSurface item;
                item.model = true; item.model_instance_index = static_cast<uint32_t>(i);
                item.model_variant = variant;
                item.part_index = static_cast<uint32_t>(part); item.polygon_index = static_cast<uint32_t>(face);
                item.world = world;
                consider(mesh,projected,model.parts[part].surface(model.asset->geometry().parts[part],face),item,model.modulation.alpha);
            }
        }
    }
    {
        if (exterior_window.visible && frame.data().terrain.asset) {
            const auto& terrain = frame.data().terrain;
            for (size_t patch_index = 0; patch_index < terrain.asset->patches().size(); ++patch_index) {
                const auto& patch = terrain.asset->patches()[patch_index];
                if (boundsOutside(result.camera,patch.bounds,exterior_window)) continue;
                const Vec3 center{(patch.bounds.minimum.x+patch.bounds.maximum.x)*.5f,
                    (patch.bounds.minimum.y+patch.bounds.maximum.y)*.5f,(patch.bounds.minimum.z+patch.bounds.maximum.z)*.5f};
                for (size_t batch = 0; batch < patch.batches.size(); ++batch) {
                    const auto& surface = terrain.surface(patch_index,batch);
                    if (!surface.draw_surface) continue;
                    VisibleSurface item; item.terrain = true;
                    item.terrain_patch = static_cast<uint32_t>(patch_index); item.terrain_batch = static_cast<uint32_t>(batch);
                    item.blended = surface.material.blended();
                    item.late = surface.material.requiresLatePass();
                    item.depth = static_cast<float>(dot(view.camera.forward,subtract(center,view.camera.position)));
                    result.surfaces.push_back(item);
                }
            }
        }
    }
    static thread_local std::vector<uint64_t> visible_receivers;
    visible_receivers.clear();
    // Room surfaces were emitted in room/polygon order. A compact ordered list
    // serves receiver membership without allocating a hash node per surface.
    if (collection.has_receivers) for (const auto& surface : result.surfaces) if (!surface.model && !surface.terrain && !surface.mesh)
        visible_receivers.push_back((uint64_t(surface.room_index)<<32)|surface.polygon_index);
    for (const auto index : collection.meshes) {
        const auto& instance = frame.data().meshes[index];
        if (instance.collection != view.collection) continue;
        if (instance.receiver_polygon >= 0 && !std::binary_search(visible_receivers.begin(),visible_receivers.end(),
            (uint64_t(room_indices.at(instance.room))<<32)|uint32_t(instance.receiver_polygon))) continue;
        if (instance.has_owner && (excludesObject(view,instance.owner) ||
            !frame.data().objects[owner_indices.at(instance.owner)].visible)) continue;
        if (instance.model_instance) {
            const auto& model = frame.data().model_instances[instances.at(instance.model_instance)];
            if (!eligible(model) || uint32_t(instance.model_variant) != selectModelVariant(model,result.camera)) continue;
        }
        const auto& geometry = instance.asset->geometry();
        if (!instance.surface.draw_surface || geometry.polygons.empty() ||
            (instance.surface.material.layer == RenderLayer::Background && !result.exterior_visible)) continue;
        PortalWindow window{-1,-1,1,1,true};
        if (instance.surface.material.layer == RenderLayer::World && view.portal_visibility) {
            window = spatialWindow(instance.room);
            if (!window.visible) continue;
        }
        VisibleSurface item; item.mesh = true; item.mesh_index = static_cast<uint32_t>(index);
        Transform attachment;
        if (instance.model_part >= 0) {
            const auto index = instances.at(instance.model_instance);
            const auto& model = frame.resolvedModel(index,size_t(instance.model_variant));
            attachment = compose(frame.data().model_instances[index].current,model.parts[instance.model_part].current);
        }
        item.world = resolveMeshTransform(instance,result.camera,instance.model_part >= 0 ? &attachment : nullptr);
        if (instance.maximum_depth > 0) {
            const auto& b = instance.depth_reference;
            bool beyond = true;
            for (unsigned corner = 0; corner < 8; ++corner) {
                const auto p = transformPoint(item.world,{corner&1 ? b.maximum.x : b.minimum.x,
                    corner&2 ? b.maximum.y : b.minimum.y,corner&4 ? b.maximum.z : b.minimum.z});
                beyond = beyond && dot(view.camera.forward,subtract(p,view.camera.position)) > instance.maximum_depth;
            }
            if (beyond) continue;
        }
        if (instance.size_fade_range.y > 0 && dot(item.world.right,item.world.right) == 0) continue;
        // Pixel-expanded markers can straddle a clip boundary while their
        // centers are outside. Submit the batch; homogeneous GPU clipping sees
        // the expanded vertices and is authoritative for this representation.
        bool visible = !geometry.raster_offsets.empty();
        ProjectedMesh projected(result.camera,MeshView(geometry,instance.vertices),item.world,window,projection_scratch);
        if (!visible) for (const auto& polygon : geometry.polygons)
            if (!projected.outside(polygon)) { visible = true; break; }
        if (!visible) continue;
        item.layer = instance.surface.material.layer;
        item.blended = instance.surface.material.blended();
        item.late = instance.surface.material.requiresLatePass();
        item.depth = static_cast<float>(dot(view.camera.forward,subtract(item.world.position,view.camera.position)));
        result.surfaces.push_back(item);
    }
    std::sort(result.object_indices.begin(),result.object_indices.end());
    orderViewSurfaces(frame,result);
    return result;
}
void orderViewSurfaces(const Presentation& frame,ViewVisibility& result) {
    struct Key { uint32_t source; int32_t composition; float depth; uint32_t group; };
    static thread_local std::vector<Key> keys;
    const auto size = result.surfaces.size();
    if (size > UINT32_MAX) throw std::length_error("Too many view surfaces");
    keys.resize(size);
    const bool compositing = result.ordering == ViewOrdering::Compositing;
    for (size_t i = 0; i < size; ++i) {
        const auto& surface = result.surfaces[i];
        const uint32_t group = surface.layer == RenderLayer::Background ? 0 : surface.late ? 2 : 1;
        keys[i] = {uint32_t(i),compositing ? visibleSurfaceBinding(frame,surface).composition_order : 0,
            surface.depth,compositing ? 0 : group};
    }
    std::sort(keys.begin(),keys.end(),[](const Key& a,const Key& b) {
        if (a.group != b.group) return a.group < b.group;
        if (a.composition != b.composition) return a.composition < b.composition;
        if (a.group && a.depth != b.depth) return a.group == 2 ? a.depth > b.depth : a.depth < b.depth;
        return a.source < b.source; // exactly preserve equal-key authored order
    });
    // Apply new-index -> source-index cycles once. This needs one record of
    // temporary storage, not stable_sort's allocation and repeated wide moves.
    for (size_t first = 0; first < size; ++first) {
        if (keys[first].source == first) continue;
        auto saved = std::move(result.surfaces[first]);
        size_t at = first;
        while (keys[at].source != first) {
            const auto next = keys[at].source;
            result.surfaces[at] = std::move(result.surfaces[next]);
            keys[at].source = uint32_t(at); at = next;
        }
        result.surfaces[at] = std::move(saved); keys[at].source = uint32_t(at);
    }
}
const SurfaceBinding& visibleSurfaceBinding(const Presentation& frame,const VisibleSurface& item) {
    const auto& data = frame.data();
    if (item.mesh) return data.meshes[item.mesh_index].surface;
    if (item.terrain) return data.terrain.surface(item.terrain_patch,item.terrain_batch);
    if (item.model) {
        const auto& model = frame.resolvedModel(item.model_instance_index,item.model_variant);
        return model.parts[item.part_index].surface(model.asset->geometry().parts[item.part_index],item.polygon_index);
    }
    return data.rooms[item.room_index].surfaces[item.polygon_index];
}
MaterialPolicy visibleSurfacePolicy(const Presentation& frame,const VisibleSurface& item) {
    return modulatedMaterialPolicy(visibleSurfaceBinding(frame,item),item.model ?
        frame.data().modelsFor(item.model_instance_index)[item.model_variant].modulation.alpha : 1);
}
std::vector<ViewPass> buildViewPasses(const Presentation& frame,const ViewVisibility& view,ViewProducts products) {
    if (view.ordering == ViewOrdering::Compositing) {
        if (products.opaque_scene || products.occlusion)
            throw std::invalid_argument("Compositing surfaces cannot export opaque world products");
        for (const auto& item : view.surfaces) {
            const auto& surface = visibleSurfaceBinding(frame,item);
            if (visibleSurfacePolicy(frame,item).testsDepth() || surface.soft_depth_range > 0 || item.layer == RenderLayer::Background)
                throw std::invalid_argument("Compositing surfaces require explicit depth-independent coverage");
        }
        if (view.surfaces.empty()) return {};
        return {{ViewPassKind::CompositeSurfaces,0,static_cast<uint32_t>(view.surfaces.size())}};
    }
    if (products.occlusion && !products.opaque_scene)
        throw std::invalid_argument("World occlusion requires an opaque scene product");
    std::vector<ViewPass> passes;
    uint32_t index = 0;
    while (index < view.surfaces.size() && view.surfaces[index].layer == RenderLayer::Background) ++index;
    if (index) passes.push_back({ViewPassKind::Background,0,index});
    const auto opaque = index;
    while (index < view.surfaces.size() && !view.surfaces[index].late) ++index;
    if (products.occlusion) {
        if (index > opaque) passes.push_back({ViewPassKind::DepthSurface,opaque,index});
        passes.push_back({ViewPassKind::ExportOpaqueScene,index,index});
        passes.push_back({ViewPassKind::EvaluateWorldOcclusion,index,index});
    }
    if (index > opaque) passes.push_back({ViewPassKind::OpaqueWorld,opaque,index});
    bool depth_required = false;
    for (size_t i = index; i < view.surfaces.size(); ++i)
        depth_required = depth_required || visibleSurfaceBinding(frame,view.surfaces[i]).soft_depth_range > 0;
    if (depth_required) passes.push_back({ViewPassKind::SnapshotOpaqueDepth,index,index});
    if (products.opaque_scene && !products.occlusion) passes.push_back({ViewPassKind::ExportOpaqueScene,index,index});
    if (index < view.surfaces.size()) passes.push_back({ViewPassKind::LateWorld,index,static_cast<uint32_t>(view.surfaces.size())});
    return passes;
}
} // namespace d3scene
