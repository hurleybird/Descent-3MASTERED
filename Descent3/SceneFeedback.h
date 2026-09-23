// Descent 3MASTERED - GPL-3.0-or-later
#pragma once
#include "../renderer/Scene.h"
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

struct SceneObservationInspection {
    d3scene::VisibilityReport report;
    bool exterior_pending = false, mirrors_pending = false;
};
// Candidate compatibility query, not a permission to commit gameplay state.
// Full exterior qualification and reflected observations remain coverage gaps.
SceneObservationInspection InspectSceneObservations(const d3scene::Presentation&,d3scene::View);

// Game-owned observation transaction. It deliberately does not interpret GPU
// draw items as observations, or write Rooms/Objects while collecting views.
// Domain sets remain separate: an automap-only view must not refresh effect
// eligibility for its rooms through another view's feedback flags.
struct SceneFrameFeedback {
    uint64_t frame_sequence = 0, scene_lifetime = 0;
    std::vector<int32_t> automap_rooms, effect_rooms;
    std::vector<uint32_t> objects;
    bool terrain = false;
};

class SceneFeedbackTransaction {
    const d3scene::Presentation& frame_;
    SceneFrameFeedback result_;
    std::unordered_set<int32_t> valid_rooms_, automap_, effects_;
    std::unordered_set<uint32_t> valid_objects_, objects_;
    std::unordered_map<uint64_t,d3scene::VisibilityReport> views_;
    bool sealed_ = false;
    bool catalogued_ = false;
public:
    SceneFeedbackTransaction(const SceneFeedbackTransaction&) = delete;
    SceneFeedbackTransaction& operator=(const SceneFeedbackTransaction&) = delete;
    explicit SceneFeedbackTransaction(const d3scene::Presentation& frame) : frame_(frame) {
        result_.frame_sequence = frame.data().sequence;
        result_.scene_lifetime = frame.data().scene_lifetime;
    }
    void add(const d3scene::VisibilityReport& report) {
        if (sealed_) throw std::logic_error("Observation transaction is sealed");
        const auto require = [](bool valid) {
            if (!valid) throw std::invalid_argument("Invalid or stale view observations");
        };
        require(report.frame_sequence == result_.frame_sequence &&
            report.scene_lifetime == result_.scene_lifetime && report.view_history_key != 0);
        require((report.feedback & ~(d3scene::Automap | d3scene::ObjectVisibility | d3scene::EffectEligibility)) == 0);
        require(report.rooms.empty() || (report.feedback & (d3scene::Automap | d3scene::EffectEligibility)));
        require(report.objects.empty() || (report.feedback & d3scene::ObjectVisibility));
        require(!report.terrain || (report.feedback & d3scene::EffectEligibility));
        if (!catalogued_ && (!report.rooms.empty() || !report.objects.empty())) {
            for (const auto& room : frame_.data().rooms) valid_rooms_.insert(room.number);
            for (const auto& object : frame_.data().objects) valid_objects_.insert(object.handle);
            catalogued_ = true;
        }
        // Validate the entire report before accepting any of its domains.
        for (const auto room : report.rooms) require(valid_rooms_.count(room) != 0);
        for (const auto object : report.objects) require(valid_objects_.count(object) != 0);
        const auto previous = views_.find(report.view_history_key);
        if (previous != views_.end()) {
            const auto& old = previous->second;
            require(old.feedback == report.feedback && old.rooms == report.rooms &&
                old.objects == report.objects && old.terrain == report.terrain);
            return; // exact replay is idempotent; conflicting camera results are a bug
        }
        views_.emplace(report.view_history_key,report);
        for (const auto room : report.rooms) {
            if ((report.feedback & d3scene::Automap) && automap_.insert(room).second)
                result_.automap_rooms.push_back(room);
            if ((report.feedback & d3scene::EffectEligibility) && effects_.insert(room).second)
                result_.effect_rooms.push_back(room);
        }
        // First observation order is retained. Sorting allocation handles here
        // would silently reorder AI notifications and potentially their RNG.
        for (const auto object : report.objects) if (objects_.insert(object).second)
            result_.objects.push_back(object);
        result_.terrain = result_.terrain || report.terrain;
    }
    const SceneFrameFeedback& seal() {
        if (sealed_) throw std::logic_error("Observation transaction already sealed");
        sealed_ = true;
        return result_;
    }
};
