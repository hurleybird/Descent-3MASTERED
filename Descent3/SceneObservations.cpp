// Descent 3MASTERED - GPL-3.0-or-later
#include "SceneFeedback.h"
#include "../renderer/ScenePortals.h"
#include <algorithm>
#include <cmath>

SceneObservationInspection InspectSceneObservations(const d3scene::Presentation& frame,d3scene::View request) {
    using namespace d3scene;
    const auto view = resolveView(frame,request);
    SceneObservationInspection result;
    auto& report = result.report;
    report.frame_sequence = frame.data().sequence; report.scene_lifetime = frame.data().scene_lifetime;
    report.view_history_key = view.history_key; report.feedback = view.feedback;
    if (!view.feedback) return result;
    if (!frame.data().observation_inputs_prepared || view.collection != 0 ||
        view.target != ViewTarget::Scene || view.ordering != ViewOrdering::Depth ||
        view.projection != ViewProjection::Perspective ||
        (view.kind != ViewKind::Main && view.kind != ViewKind::Auxiliary) || !view.portal_visibility)
        throw std::invalid_argument("Unsupported observation query contract");
    const auto camera = prepareCamera(view);
    const auto windows = queryPortalWindows(frame,view,camera);
    std::unordered_map<int32_t,size_t> rooms;
    for (size_t i = 0; i < frame.data().rooms.size(); ++i) rooms.emplace(frame.data().rooms[i].number,i);
    if (!view.exterior_origin && !rooms.count(view.room))
        throw std::invalid_argument("Observation view has no spatial root");
    result.exterior_pending = view.exterior_origin;
    for (size_t i = 0; i < windows.size(); ++i) if (windows[i].visible) {
        const auto& room = frame.data().rooms[i];
        result.exterior_pending = result.exterior_pending || room.exterior;
        result.mirrors_pending = result.mirrors_pending || room.mirror_polygon >= 0;
        if (view.feedback & (Automap | EffectEligibility)) report.rooms.push_back(room.number);
    }
    if (!(view.feedback & ObjectVisibility)) return result;
    struct Observed { uint32_t handle; float depth; bool front_to_back; };
    std::vector<Observed> observed;
    for (const auto& object : frame.data().objects) {
        if (!object.observation_eligible || (view.exclude_object && object.handle == view.excluded_object)) continue;
        const auto found = rooms.find(object.room);
        if (found == rooms.end()) continue;
        const size_t room_index = found->second;
        if (!windows[room_index].visible) continue;
        const auto& room = frame.data().rooms[room_index];
        if (room.exterior) continue; // do not replace terrain occlusion with an all-visible guess
        const auto& p = object.current.position;
        const auto& eye = camera.transform.position;
        const auto& forward = camera.transform.forward;
        const float depth = (p.x-eye.x)*forward.x+(p.y-eye.y)*forward.y+(p.z-eye.z)*forward.z;
        if (!object.observation_unbounded) {
            // Observation spheres reach the eye plane, not the geometry near
            // plane. A near-plane clipped object still counts as observed.
            if (depth < -object.observation_radius) continue;
            bool outside = false;
            for (unsigned i = 0; i < 4; ++i) {
                const auto& plane = camera.planes[i];
                const double distance = double(plane.normal.x)*p.x+double(plane.normal.y)*p.y+
                    double(plane.normal.z)*p.z+plane.constant;
                outside = outside || distance < -object.observation_radius;
            }
            if (outside) continue;
        }
        if (room.number != view.room && !room.observation_unclipped) {
            const auto& bounds = object.observation_bounds;
            const auto& window = windows[room_index];
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
        observed.push_back({object.handle,depth,object.observation_front_to_back});
    }
    // Legacy polygon observations were front-to-back; other objects followed
    // in the reverse postrender order. Equal depths retain publication order.
    std::stable_sort(observed.begin(),observed.end(),[](const Observed& a,const Observed& b) {
        if (a.front_to_back != b.front_to_back) return a.front_to_back;
        return a.front_to_back ? a.depth < b.depth : a.depth > b.depth;
    });
    for (const auto& object : observed) report.objects.push_back(object.handle);
    return result;
}
