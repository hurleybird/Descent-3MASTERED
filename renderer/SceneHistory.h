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
