// Descent 3MASTERED - GPL-3.0-or-later
#pragma once
#include "../renderer/Scene.h"
#include <array>
#include <algorithm>
#include <cmath>
#include <stdexcept>

struct GameRoomCorona {
    d3scene::AssetId geometry;
    uint64_t appearance = 0, materials = 0;
    int room = -1, polygon = -1, bitmap = -1;
    d3scene::Vec3 center, normal, face_point, color;
    float size = 0;
    bool fast = false;
};
inline bool SameGameCorona(const GameRoomCorona& a,const GameRoomCorona& b) {
    return a.geometry == b.geometry && a.appearance == b.appearance && a.materials == b.materials && a.polygon == b.polygon;
}
constexpr unsigned GameRoomCoronaLimit = 100;
struct GameRoomCoronaBatch {
    unsigned count = 0;
    std::array<GameRoomCorona,GameRoomCoronaLimit> values;
};
struct GameRoomCoronaExposure {
    GameRoomCorona source;
    float fade = 0;
};
class GameRoomCoronaClock {
    struct Slot { GameRoomCorona source; float fade = 0; bool used = false, seen = false; };
    std::array<Slot,GameRoomCoronaLimit> slots_;
    std::array<GameRoomCoronaExposure,GameRoomCoronaLimit> output_;
    uint64_t scene_ = 0, frame_ = 0;
    bool valid_ = false;
    unsigned count_ = 0;
public:
    unsigned count() const { return count_; }
    const GameRoomCoronaExposure& operator[](unsigned i) const { return output_.at(i); }
    bool advance(uint64_t scene,uint64_t frame,float delta,const GameRoomCoronaBatch& candidates) {
        if (!std::isfinite(delta) || delta < 0 || candidates.count > GameRoomCoronaLimit)
            throw std::invalid_argument("Invalid corona exposure clock");
        for (unsigned i = 0; i < candidates.count; ++i) {
            const auto& c = candidates.values[i];
            if (c.geometry.lifetime != scene || c.polygon < 0 || c.room < 0 || !std::isfinite(c.size) || !(c.size > 0))
                throw std::invalid_argument("Invalid corona source identity");
        }
        if (valid_ && scene_ == scene) {
            if (frame_ == frame) return false;
            if (frame < frame_) throw std::logic_error("Corona exposure frame reordered");
        } else { slots_ = {}; }
        for (auto& slot : slots_) slot.seen = false;
        for (unsigned i = 0; i < candidates.count; ++i) {
            const auto& c = candidates.values[i];
            auto found = std::find_if(slots_.begin(),slots_.end(),[&](const Slot& s) { return s.used && SameGameCorona(s.source,c); });
            // A new revision of a face retires its old fade, rather than
            // drawing a ghost from replaced room geometry or appearance.
            if (found == slots_.end()) found = std::find_if(slots_.begin(),slots_.end(),[&](const Slot& s) {
                return s.used && s.source.room == c.room && s.source.polygon == c.polygon;
            });
            if (found == slots_.end()) found = std::find_if(slots_.begin(),slots_.end(),[](const Slot& s) { return !s.used; });
            if (found == slots_.end()) continue;
            if (!found->used || !SameGameCorona(found->source,c)) *found = {};
            found->source = c; found->used = found->seen = true;
        }
        count_ = 0;
        for (auto& slot : slots_) if (slot.used) {
            // Preserve the old pre-draw exposure: a newly admitted glow starts
            // at zero, then gains four units/second for the next logical frame.
            output_[count_++] = {slot.source,slot.fade};
            slot.fade = (std::max)(0.f,(std::min)(1.f,slot.fade+(slot.seen ? 4 : -4)*delta));
            if (!slot.seen && slot.fade == 0) slot.used = false;
        }
        scene_ = scene; frame_ = frame; valid_ = true;
        return true;
    }
};
inline float GameCoronaOpacity(const GameRoomCorona& source,d3scene::Vec3 eye,float fade,float room_light) {
    const auto length = [](d3scene::Vec3 v) { return std::sqrt(v.x*v.x+v.y*v.y+v.z*v.z); };
    const d3scene::Vec3 facing{eye.x-source.face_point.x,eye.y-source.face_point.y,eye.z-source.face_point.z};
    const float magnitude = length(facing);
    if (!(magnitude > 0)) return 0;
    float scalar = 2*(facing.x*source.normal.x+facing.y*source.normal.y+facing.z*source.normal.z)/magnitude;
    const float distance = length({eye.x-source.center.x,eye.y-source.center.y,eye.z-source.center.z});
    if (!(scalar > 0) || !(distance >= source.size*5)) return 0;
    scalar *= (std::min)(1.f,(distance-source.size*5)/(source.size*15));
    return (std::min)(1.f,scalar*fade)*.4f*room_light;
}
namespace d3scene { class PreparedView; }
// Called by the game-owned main-view schedule, before its GPU execution.
void CaptureGameRoomCoronaView(const d3scene::PreparedView&);
void InvalidateGameRoomCoronaMaterials();
void AdvanceGameRoomCoronaFixture();
// Replays only the already-owned descriptions, before auxiliary world windows.
void RenderGameRoomCoronas(const d3scene::Presentation& world);
