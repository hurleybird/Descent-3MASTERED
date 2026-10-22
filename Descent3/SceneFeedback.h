// Descent 3MASTERED - GPL-3.0-or-later
#pragma once
#include "../renderer/Scene.h"
#include "../renderer/SceneView.h"
#include <stdexcept>
#include <algorithm>
#include <thread>
#include <limits>

struct SceneObservationInspection {
    d3scene::VisibilityReport report;
    bool exterior_pending = false, mirrors_pending = false;
};
// Candidate compatibility query, not a permission to commit gameplay state.
// Exterior and reflected observations still require runtime qualification.
SceneObservationInspection InspectSceneObservations(const d3scene::Presentation&,d3scene::View);
// Inspection of an executable view reuses its resolved camera and apertures.
// Requested domains describe the diagnostic report; they do not authorize commit.
SceneObservationInspection InspectSceneObservations(const d3scene::PreparedView&,uint8_t feedback);

// Effect admission is conservative room visibility, not an AI observation or
// proof that a surface produced pixels. Reuse the immutable portal query that
// prepared the view; never walk the scene a second time to keep emitters alive.
d3scene::VisibilityReport CollectSceneEffectEligibility(const d3scene::PreparedView&);

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
    struct Replay {
        uint8_t feedback = 0;
        bool terrain = false;
        size_t rooms = 0, room_count = 0, objects = 0, object_count = 0;
    };
    struct Storage {
        SceneFrameFeedback result;
        std::vector<uint8_t> room_domains, object_seen;
        d3scene::SceneLookup<uint64_t,size_t> view_indices;
        std::vector<Replay> views;
        std::vector<int32_t> replay_rooms;
        std::vector<uint32_t> replay_objects;
        void clear() {
            result.frame_sequence = result.scene_lifetime = 0; result.terrain = false;
            result.automap_rooms.clear(); result.effect_rooms.clear(); result.objects.clear();
            room_domains.clear(); object_seen.clear(); view_indices.clear(); views.clear();
            replay_rooms.clear(); replay_objects.clear();
        }
    };
    struct Pool {
        std::array<Storage,4> spare;
        size_t count = 0;
        const std::thread::id publisher = std::this_thread::get_id();
    };
    d3scene::PresentationRef frame_;
    Storage storage_;
    std::shared_ptr<Pool> pool_;
    bool sealed_ = false;
public:
    SceneFeedbackTransaction(const SceneFeedbackTransaction&) = delete;
    SceneFeedbackTransaction& operator=(const SceneFeedbackTransaction&) = delete;
    explicit SceneFeedbackTransaction(const d3scene::Presentation& frame) : frame_(frame.shared_from_this()) {
        static thread_local auto pool = std::make_shared<Pool>();
        pool_ = pool;
        if (pool->count) std::swap(storage_,pool->spare[--pool->count]);
        storage_.room_domains.assign(frame.data().rooms.size(),0);
        storage_.object_seen.assign(frame.data().objects.size(),0);
        storage_.result.frame_sequence = frame.data().sequence;
        storage_.result.scene_lifetime = frame.data().scene_lifetime;
    }
    ~SceneFeedbackTransaction() {
        if (pool_->publisher != std::this_thread::get_id() || pool_->count == pool_->spare.size()) return;
        storage_.clear(); std::swap(storage_,pool_->spare[pool_->count++]);
    }
    void add(const d3scene::VisibilityReport& report) {
        if (sealed_) throw std::logic_error("Observation transaction is sealed");
        const auto require = [](bool valid) {
            if (!valid) throw std::invalid_argument("Invalid or stale view observations");
        };
        auto& state = storage_;
        auto& result = state.result;
        require(report.frame_sequence == result.frame_sequence &&
            report.scene_lifetime == result.scene_lifetime && report.view_history_key != 0);
        require((report.feedback & ~(d3scene::Automap | d3scene::ObjectVisibility | d3scene::EffectEligibility)) == 0);
        require(report.rooms.empty() || (report.feedback & (d3scene::Automap | d3scene::EffectEligibility)));
        require(report.objects.empty() || (report.feedback & d3scene::ObjectVisibility));
        require(!report.terrain || (report.feedback & d3scene::EffectEligibility));
        // Validate the entire report before accepting any of its domains.
        // The immutable presentation already owns these identity indices. Do
        // not rebuild node-allocated catalogues of the whole level per frame.
        for (const auto room : report.rooms) require(frame_->roomIndices().find(room) != frame_->roomIndices().end());
        for (const auto object : report.objects) require(frame_->objectIndices().find(object) != frame_->objectIndices().end());
        const auto previous = state.view_indices.find(report.view_history_key);
        if (previous != state.view_indices.end()) {
            const auto& old = state.views[previous->second];
            require(old.feedback == report.feedback && old.room_count == report.rooms.size() &&
                old.object_count == report.objects.size() && old.terrain == report.terrain &&
                std::equal(report.rooms.begin(),report.rooms.end(),state.replay_rooms.begin()+old.rooms) &&
                std::equal(report.objects.begin(),report.objects.end(),state.replay_objects.begin()+old.objects));
            return; // exact replay is idempotent; conflicting camera results are a bug
        }
        // All growth precedes acceptance. Retain flat storage, never allocate a
        // node per observed room/object or a nested report vector per view.
        const auto reserve_more = [](auto& values,size_t extra) {
            const auto limit = std::numeric_limits<size_t>::max();
            if (extra > limit-values.size()) throw std::length_error("Observation storage overflow");
            const auto needed = values.size()+extra;
            if (needed > values.capacity()) values.reserve(std::max(needed,
                values.capacity() <= limit/2 ? values.capacity()*2 : needed));
        };
        reserve_more(state.views,1); state.view_indices.reserve(state.views.size()+1);
        reserve_more(state.replay_rooms,report.rooms.size()); reserve_more(state.replay_objects,report.objects.size());
        const auto extra_rooms = [&](size_t size) { return std::min(report.rooms.size(),state.room_domains.size()-size); };
        if (report.feedback & d3scene::Automap) reserve_more(result.automap_rooms,extra_rooms(result.automap_rooms.size()));
        if (report.feedback & d3scene::EffectEligibility) reserve_more(result.effect_rooms,extra_rooms(result.effect_rooms.size()));
        reserve_more(result.objects,std::min(report.objects.size(),state.object_seen.size()-result.objects.size()));
        const Replay replay{report.feedback,report.terrain,state.replay_rooms.size(),report.rooms.size(),
            state.replay_objects.size(),report.objects.size()};
        state.view_indices.emplace(report.view_history_key,state.views.size());
        state.views.push_back(replay);
        state.replay_rooms.insert(state.replay_rooms.end(),report.rooms.begin(),report.rooms.end());
        state.replay_objects.insert(state.replay_objects.end(),report.objects.begin(),report.objects.end());
        for (const auto room : report.rooms) {
            auto& domains = state.room_domains[frame_->roomIndices().at(room)];
            if ((report.feedback & d3scene::Automap) && !(domains & d3scene::Automap)) result.automap_rooms.push_back(room);
            if ((report.feedback & d3scene::EffectEligibility) && !(domains & d3scene::EffectEligibility)) result.effect_rooms.push_back(room);
            domains |= report.feedback & (d3scene::Automap | d3scene::EffectEligibility);
        }
        // First observation order is retained. Sorting allocation handles here
        // would silently reorder AI notifications and potentially their RNG.
        for (const auto object : report.objects) {
            auto& seen = state.object_seen[frame_->objectIndices().at(object)];
            if (!seen) { seen = 1; result.objects.push_back(object); }
        }
        result.terrain = result.terrain || report.terrain;
    }
    const SceneFrameFeedback& seal() {
        if (sealed_) throw std::logic_error("Observation transaction already sealed");
        sealed_ = true;
        return storage_.result;
    }
};
