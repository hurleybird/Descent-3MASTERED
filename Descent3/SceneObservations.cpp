// Descent 3MASTERED - GPL-3.0-or-later
#include "SceneFeedback.h"
#include "../renderer/ScenePortals.h"
#include "../renderer/SceneTerrain.h"
#include <algorithm>
#include <cmath>

d3scene::VisibilityReport CollectSceneEffectEligibility(const d3scene::PreparedView& prepared) {
    using namespace d3scene;
    const auto& frame = prepared.frame();
    const auto& view = prepared.view();
    VisibilityReport report;
    report.frame_sequence = frame.data().sequence;
    report.scene_lifetime = frame.data().scene_lifetime;
    report.view_history_key = view.history_key;
    // Preview, automap, clipped diagnostic, and foreground views cannot extend
    // simulation effect lifetime. Reflected-view admission is qualified separately.
    if ((view.kind != ViewKind::Main && view.kind != ViewKind::Auxiliary) ||
        view.collection != 0 || view.has_clip_plane || !view.excluded_surfaces.empty() ||
        view.target != ViewTarget::Scene || view.ordering != ViewOrdering::Depth ||
        view.projection != ViewProjection::Perspective || !view.portal_visibility)
        return report;
    report.feedback = EffectEligibility;
    const auto& visible = prepared.visibility();
    report.rooms.reserve(visible.room_indices.size());
    for (auto index : visible.room_indices) report.rooms.push_back(frame.data().rooms[index].number);
    report.terrain = visible.exterior_visible;
    return report;
}

static SceneObservationInspection inspectObservations(const d3scene::Presentation& frame,const d3scene::View& view,
    const d3scene::Camera& camera,const d3scene::PortalVisibility& spatial,uint8_t feedback) {
    using namespace d3scene;
    SceneObservationInspection result;
    auto& report = result.report;
    report.frame_sequence = frame.data().sequence; report.scene_lifetime = frame.data().scene_lifetime;
    report.view_history_key = view.history_key; report.feedback = feedback;
    if (feedback & ~(Automap | ObjectVisibility | EffectEligibility))
        throw std::invalid_argument("Unknown observation domains");
    if (!feedback) return result;
    const bool mirror = view.kind == ViewKind::Mirror;
    const bool mirror_contract = mirror && view.has_clip_plane && view.reflections == ReflectionPolicy::SurfaceFilm &&
        view.fog_path_origin == FogPathOrigin::ClipPlane && !(feedback & Automap);
    if (!frame.data().observation_inputs_prepared || view.collection != 0 ||
        (view.has_clip_plane && !mirror_contract) ||
        view.target != ViewTarget::Scene || view.ordering != ViewOrdering::Depth ||
        view.projection != ViewProjection::Perspective ||
        (view.kind != ViewKind::Main && view.kind != ViewKind::Auxiliary && !mirror_contract) || !view.portal_visibility)
        throw std::invalid_argument("Unsupported observation query contract");
    const auto outside_mirror = [&](const Bounds& bounds) {
        if (!mirror) return false;
        const auto& plane = view.clip_plane;
        const auto& n = plane.normal;
        const Vec3 support{n.x >= 0 ? bounds.maximum.x : bounds.minimum.x,
            n.y >= 0 ? bounds.maximum.y : bounds.minimum.y,n.z >= 0 ? bounds.maximum.z : bounds.minimum.z};
        return double(n.x)*support.x+double(n.y)*support.y+double(n.z)*support.z+plane.constant < 0;
    };
    const auto& windows = spatial.rooms;
    const auto& rooms = frame.roomIndices();
    if (!view.exterior_origin && rooms.find(view.room) == rooms.end())
        throw std::invalid_argument("Observation view has no spatial root");
    result.exterior_pending = spatial.exterior.visible;
    report.terrain = spatial.exterior.visible && (feedback & EffectEligibility);
    for (size_t i = 0; i < windows.size(); ++i) if (windows[i].visible) {
        const auto& room = frame.data().rooms[i];
        if (outside_mirror(room.asset->bounds())) continue;
        result.exterior_pending = result.exterior_pending || room.exterior;
        result.mirrors_pending = result.mirrors_pending || (!mirror && !room.mirrors.empty());
        if (feedback & (Automap | EffectEligibility)) report.rooms.push_back(room.number);
    }
    if (!(feedback & ObjectVisibility)) return result;
    struct Observed { uint32_t handle; float depth; bool front_to_back; size_t ordinal; };
    static thread_local std::vector<Observed> observed;
    observed.clear();
    for (const auto& object : frame.data().objects) {
        if (!object.observation_eligible || excludesObject(view,object.handle)) continue;
        if (outside_mirror(object.observation_bounds)) continue;
        const auto found = rooms.find(object.room);
        const bool exterior = object.room < -1;
        const RoomState* room = found == rooms.end() ? nullptr : &frame.data().rooms[found->second];
        if (exterior) {
            if (!spatial.exterior.visible) continue;
            if (!(frame.data().terrain.observation_distance > 0))
                throw std::invalid_argument("Exterior observation distance was not published");
        } else if (!room || !windows[found->second].visible || room->exterior) continue;
        const auto& p = object.current.position;
        const auto& eye = camera.transform.position;
        const auto& forward = camera.transform.forward;
        const float depth = (p.x-eye.x)*forward.x+(p.y-eye.y)*forward.y+(p.z-eye.z)*forward.z;
        if (exterior && frame.data().terrain.occlusion &&
            !terrainMaySeeSphere(*frame.data().terrain.occlusion,eye,p,object.radius)) continue;
        if (!object.observation_unbounded) {
            // Observation spheres reach the eye plane, not the geometry near
            // plane. A near-plane clipped object still counts as observed.
            if (depth < -object.observation_radius) continue;
            if (exterior && depth > frame.data().terrain.observation_distance+object.observation_radius) continue;
            bool outside = false;
            for (unsigned i = 0; i < 4; ++i) {
                const auto& plane = camera.planes[i];
                const double distance = double(plane.normal.x)*p.x+double(plane.normal.y)*p.y+
                    double(plane.normal.z)*p.z+plane.constant;
                outside = outside || distance < -object.observation_radius;
            }
            if (outside) continue;
        }
        if ((exterior && !object.observation_unbounded) ||
            (!exterior && (mirror || (room->number != view.room && !room->observation_unclipped)))) {
            const auto& bounds = object.observation_bounds;
            const auto& window = exterior ? spatial.exterior : windows[found->second];
            bool left = true, right = true, bottom = true, top = true, crosses_eye = false;
            for (unsigned i = 0; i < 8; ++i) {
                const auto clip = project(camera,{i&1 ? bounds.maximum.x : bounds.minimum.x,
                    i&2 ? bounds.maximum.y : bounds.minimum.y,i&4 ? bounds.maximum.z : bounds.minimum.z});
                if (clip.w <= 0) { crosses_eye = true; break; }
                left = left && clip.x < window.left*clip.w;
                right = right && clip.x > window.right*clip.w;
                bottom = bottom && clip.y < window.bottom*clip.w;
                top = top && clip.y > window.top*clip.w;
            }
            if (!crosses_eye && (left || right || bottom || top)) continue;
        }
        observed.push_back({object.handle,depth,object.observation_front_to_back,observed.size()});
    }
    // Legacy polygon observations were front-to-back; other objects followed
    // in the reverse postrender order. Equal depths retain publication order.
    std::sort(observed.begin(),observed.end(),[](const Observed& a,const Observed& b) {
        if (a.front_to_back != b.front_to_back) return a.front_to_back;
        if (a.depth == b.depth) return a.ordinal < b.ordinal;
        return a.front_to_back ? a.depth < b.depth : a.depth > b.depth;
    });
    for (const auto& object : observed) report.objects.push_back(object.handle);
    return result;
}

SceneObservationInspection InspectSceneObservations(const d3scene::Presentation& frame,d3scene::View request) {
    const auto view = d3scene::resolveView(frame,request);
    const auto camera = d3scene::prepareCamera(view);
    const auto spatial = d3scene::queryPortalVisibility(frame,view,camera);
    return inspectObservations(frame,view,camera,spatial,view.feedback);
}
SceneObservationInspection InspectSceneObservations(const d3scene::PreparedView& prepared,uint8_t feedback) {
    return inspectObservations(prepared.frame(),prepared.view(),prepared.visibility().camera,
        prepared.visibility().spatial,feedback);
}

bool SceneFeedbackTransaction::observe(const d3scene::PreparedView& prepared) {
    using namespace d3scene;
    const auto& view = prepared.view();
    const bool main = view.kind == ViewKind::Main;
    const bool mirror = view.kind == ViewKind::Mirror && view.has_clip_plane &&
        view.reflections == ReflectionPolicy::SurfaceFilm && view.fog_path_origin == FogPathOrigin::ClipPlane;
    if ((!main && view.kind != ViewKind::Auxiliary && !mirror) || view.collection != 0 ||
        (view.has_clip_plane && !mirror) || (!mirror && !view.excluded_surfaces.empty()) ||
        view.target != ViewTarget::Scene || view.ordering != ViewOrdering::Depth ||
        view.projection != ViewProjection::Perspective || !view.portal_visibility) return false;
    if (!view.exterior_origin && prepared.frame().roomIndices().find(view.room) == prepared.frame().roomIndices().end())
        return false; // isolated model/diagnostic collections have no gameplay spatial root
    auto& result = storage_.result;
    const auto same_vector = [](Vec3 a,Vec3 b) { return a.x == b.x && a.y == b.y && a.z == b.z; };
    const auto& camera = prepared.visibility().camera;
    const auto& prior = result.main_camera;
    const auto& p = camera.projection; const auto& q = prior.projection;
    const bool same_camera = same_vector(camera.transform.position,prior.transform.position) &&
        same_vector(camera.transform.right,prior.transform.right) && same_vector(camera.transform.up,prior.transform.up) &&
        same_vector(camera.transform.forward,prior.transform.forward) && same_vector(camera.velocity,prior.velocity) &&
        p.x_scale == q.x_scale && p.y_scale == q.y_scale && p.x_offset == q.x_offset && p.y_offset == q.y_offset &&
        p.near_distance == q.near_distance && p.orthographic_depth_scale == q.orthographic_depth_scale;
    const auto a = view.viewport, b = result.main_viewport;
    if (main && result.main_view_present &&
        (result.main_view_key != view.history_key || result.main_camera_owner != view.camera_owner ||
         result.main_camera_variant != view.camera_variant ||
         !same_camera || a.x != b.x || a.y != b.y || a.width != b.width || a.height != b.height ||
         result.main_camera_room != view.room || result.main_camera_cut != view.camera_cut ||
         result.main_excluded_objects != view.excluded_objects ||
         result.main_exterior_visible != prepared.visibility().exterior_visible))
        throw std::invalid_argument("One game frame cannot have conflicting main camera receipts");
    if (main) result.main_excluded_objects.reserve(view.excluded_objects.size());
    const uint8_t domains = ObjectVisibility | EffectEligibility | (mirror ? 0 : Automap);
    add(InspectSceneObservations(prepared,domains).report);
    if (main) {
        result.main_view_present = true; result.main_view_key = view.history_key;
        result.main_camera_owner = view.camera_owner; result.main_camera_variant = view.camera_variant;
        result.main_exterior_visible = prepared.visibility().exterior_visible;
        result.main_camera = camera; result.main_viewport = view.viewport;
        result.main_camera_room = view.room; result.main_camera_cut = view.camera_cut;
        result.main_excluded_objects.assign(view.excluded_objects.begin(),view.excluded_objects.end());
    }
    return true;
}
