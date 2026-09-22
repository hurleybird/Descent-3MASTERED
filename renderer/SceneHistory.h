// Descent 3MASTERED - GPL-3.0-or-later
#pragma once
#include "SceneView.h"
#include <map>
#include <stdexcept>

namespace d3scene {
struct ViewHistorySample {
    uint64_t sequence = 0, lifetime = 0;
    View view;
    ViewMatrices current, previous;
    bool previous_valid = false;
};
// Image-space consumers receive a complete camera pair and its time/movement
// inputs. They may not reconstruct these from the most recent canvas draw.
struct ImageMotion {
    ViewHistorySample camera;
    float delta_time = 0, afterburner_factor = 0;
    Vec3 player_control_delta;
    bool valid = false, paused = false;
};
inline ImageMotion prepareImageMotion(const FrameData& frame,const ViewHistorySample& camera,
    const ImageMotion& completed) {
    if (camera.sequence != frame.sequence || camera.lifetime != frame.scene_lifetime ||
        camera.view.kind != ViewKind::Main)
        throw std::logic_error("Image motion requires this presentation's main camera");
    ImageMotion result;
    result.camera = camera; result.delta_time = frame.delta_time;
    result.afterburner_factor = frame.afterburner_factor;
    result.player_control_delta = frame.player_control_delta;
    result.valid = true; result.paused = frame.paused;
    // Pause repeats the last exposure, but never borrows it across a camera
    // switch, resize, cut, reload or changed paused camera.
    const auto& prior = completed.camera;
    if (frame.paused && completed.valid && !camera.view.camera_cut &&
        prior.lifetime == camera.lifetime && prior.sequence <= camera.sequence && camera.sequence-prior.sequence <= 1 &&
        prior.view.history_key == camera.view.history_key &&
        prior.view.camera_owner == camera.view.camera_owner &&
        prior.view.camera_variant == camera.view.camera_variant &&
        prior.view.viewport.x == camera.view.viewport.x && prior.view.viewport.y == camera.view.viewport.y &&
        prior.view.viewport.width == camera.view.viewport.width && prior.view.viewport.height == camera.view.viewport.height &&
        prior.view.projection_anchor.x == camera.view.projection_anchor.x && prior.view.projection_anchor.y == camera.view.projection_anchor.y &&
        prior.view.projection_anchor.width == camera.view.projection_anchor.width && prior.view.projection_anchor.height == camera.view.projection_anchor.height &&
        prior.current.projection == camera.current.projection &&
        prior.current.view_projection == camera.current.view_projection) {
        result.camera.previous = prior.previous;
        result.camera.previous_valid = prior.previous_valid;
        result.delta_time = completed.delta_time;
        result.afterburner_factor = completed.afterburner_factor;
        result.player_control_delta = completed.player_control_delta;
    }
    return result;
}
// History advances only when a view has completed. Querying another view,
// constructing a preview, or replaying a pass cannot replace its predecessor.
class ViewHistory {
    std::map<std::pair<ViewKind,uint64_t>,ViewHistorySample> completed_;
    static bool sameRect(Rect a,Rect b) {
        return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
    }
    static bool compatible(const ViewHistorySample& a,const ViewHistorySample& b) {
        return a.lifetime == b.lifetime && a.view.camera_owner == b.view.camera_owner &&
            a.view.camera_variant == b.view.camera_variant &&
            sameRect(a.view.viewport,b.view.viewport) && sameRect(a.view.projection_anchor,b.view.projection_anchor) &&
            a.current.projection == b.current.projection;
    }
public:
    void clear() { completed_.clear(); }
    ViewHistorySample prepare(const FrameData& frame,const View& view,const Camera& camera) const {
        ViewHistorySample result;
        result.sequence = frame.sequence; result.lifetime = frame.scene_lifetime;
        result.view = view; result.current = viewMatrices(camera); result.previous = result.current;
        const auto found = completed_.find({view.kind,view.history_key});
        if (found == completed_.end()) return result;
        const auto& prior = found->second;
        if (prior.lifetime == result.lifetime && prior.sequence == result.sequence) {
            if (!compatible(prior,result) || prior.current.view_projection != result.current.view_projection ||
                prior.view.camera_cut != result.view.camera_cut)
                throw std::logic_error("A view history key was reused for a different camera in one presentation");
            result.previous = prior.previous; result.previous_valid = prior.previous_valid;
        } else if (!view.camera_cut && prior.sequence < result.sequence &&
            result.sequence-prior.sequence == 1 && compatible(prior,result)) {
            result.previous = prior.current; result.previous_valid = true;
        }
        return result;
    }
    void commit(const ViewHistorySample& sample) {
        const auto key = std::make_pair(sample.view.kind,sample.view.history_key);
        const auto found = completed_.find(key);
        if (found != completed_.end() && found->second.lifetime == sample.lifetime && found->second.sequence > sample.sequence)
            throw std::logic_error("Cannot advance view history with an older presentation");
        completed_[key] = sample;
    }
};
}
