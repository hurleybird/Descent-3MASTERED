// Descent 3MASTERED - GPL-3.0-or-later
#include "Scene.h"
#include <atomic>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace d3scene {
namespace {
std::atomic<uint64_t> NextLifetime{1};
bool finite(Vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
bool equal(Vec3 a, Vec3 b) { return a.x == b.x && a.y == b.y && a.z == b.z; }
bool finite(const Transform& t) {
    return finite(t.position) && finite(t.right) && finite(t.up) && finite(t.forward);
}
void require(bool condition, const char* message) {
    if (!condition) throw std::invalid_argument(message);
}
void validateGeometry(const RoomGeometry& geometry) {
    for (const auto& vertex : geometry.vertices)
        require(finite(vertex), "Non-finite scene vertex");
    for (const auto& polygon : geometry.polygons) {
        require(finite(polygon.normal), "Non-finite scene normal");
        require(polygon.corners.size() >= 3, "Scene polygon has fewer than three corners");
        for (const auto& corner : polygon.corners) {
            require(corner.vertex < geometry.vertices.size(), "Scene corner references missing vertex");
            require(std::isfinite(corner.u) && std::isfinite(corner.v) &&
                std::isfinite(corner.light_u) && std::isfinite(corner.light_v), "Non-finite scene UV");
        }
    }
}
}

bool operator==(AssetId a, AssetId b) {
    return a.lifetime == b.lifetime && a.revision == b.revision && a.slot == b.slot;
}
bool operator!=(AssetId a, AssetId b) { return !(a == b); }
bool operator==(const RoomGeometry& a, const RoomGeometry& b) {
    if (a.vertices.size() != b.vertices.size() || a.polygons.size() != b.polygons.size()) return false;
    for (size_t i = 0; i < a.vertices.size(); ++i)
        if (!equal(a.vertices[i], b.vertices[i])) return false;
    for (size_t i = 0; i < a.polygons.size(); ++i) {
        const auto& x = a.polygons[i]; const auto& y = b.polygons[i];
        if (!equal(x.normal, y.normal) || x.corners.size() != y.corners.size()) return false;
        for (size_t j = 0; j < x.corners.size(); ++j) {
            const auto& c = x.corners[j]; const auto& d = y.corners[j];
            if (c.vertex != d.vertex || c.u != d.u || c.v != d.v || c.light_u != d.light_u ||
                c.light_v != d.light_v || c.alpha != d.alpha) return false;
        }
    }
    return true;
}

RoomAsset::RoomAsset(AssetId id, RoomGeometry geometry) : id_(id), geometry_(std::move(geometry)) {}
SceneAssets::SceneAssets() { reset(); }
void SceneAssets::reset() {
    const uint64_t lifetime = NextLifetime.fetch_add(1);
    if (!lifetime || lifetime == std::numeric_limits<uint64_t>::max())
        throw std::overflow_error("Scene lifetime exhausted");
    rooms_.clear();
    lifetime_ = lifetime;
    revision_ = 0;
}
RoomAssetRef SceneAssets::room(uint32_t slot) const {
    return slot < rooms_.size() ? rooms_[slot] : nullptr;
}
RoomAssetRef SceneAssets::publishRoom(uint32_t slot, RoomGeometry geometry) {
    validateGeometry(geometry);
    const auto previous = room(slot);
    if (previous && previous->geometry() == geometry) return previous;
    if (revision_ == std::numeric_limits<uint64_t>::max())
        throw std::overflow_error("Scene revision exhausted");
    RoomAssetRef replacement(new RoomAsset({lifetime_, revision_ + 1, slot}, std::move(geometry)));
    if (slot >= rooms_.size()) rooms_.resize(static_cast<size_t>(slot) + 1);
    rooms_[slot] = replacement;
    ++revision_;
    return replacement;
}
void SceneAssets::removeRoom(uint32_t slot) {
    if (slot < rooms_.size()) rooms_[slot].reset();
}

PresentationRef Presentation::freeze(const FrameData& data) {
    require(data.sequence != 0 && data.scene_lifetime != 0, "Unidentified presentation frame");
    require(std::isfinite(data.simulation_time) && std::isfinite(data.delta_time) && data.delta_time >= 0,
        "Invalid presentation clock");
    require((data.pending_domains & ~AllPendingDomains) == 0, "Unknown pending scene domain");
    std::unordered_set<int32_t> rooms;
    for (const auto& room : data.rooms) {
        require(room.number >= 0 && rooms.insert(room.number).second, "Duplicate or invalid scene room");
        require(room.asset && room.asset->id().lifetime == data.scene_lifetime &&
            room.asset->id().slot == static_cast<uint32_t>(room.number), "Room asset identity mismatch");
        require(room.surfaces.size() == room.asset->geometry().polygons.size(), "Incomplete room surface bindings");
        require(finite(room.fog_color) && std::isfinite(room.fog_depth), "Invalid room fog");
        for (const auto& portal : room.portals)
            require(portal.polygon >= 0 && static_cast<size_t>(portal.polygon) < room.surfaces.size(),
                "Portal references missing polygon");
    }
    std::unordered_set<uint32_t> objects;
    for (const auto& object : data.objects) {
        require(objects.insert(object.handle).second, "Duplicate object lifetime in scene");
        require(finite(object.current) && finite(object.previous) && std::isfinite(object.radius) && object.radius >= 0,
            "Invalid object transform or radius");
    }
    return PresentationRef(new Presentation(data));
}
void Presentation::requireExecutable() const {
    if (data_.pending_domains != 0)
        throw std::logic_error("Renderer migration incomplete: presentation contains unprepared domains");
}
void validateView(const View& view) {
    require(view.history_key != 0, "View has no history identity");
    require(finite(view.camera) && std::isfinite(view.zoom) && view.zoom > 0, "Invalid view camera");
    require(view.viewport.width > 0 && view.viewport.height > 0 &&
        view.projection_anchor.width > 0 && view.projection_anchor.height > 0, "Invalid view extent");
    require((view.feedback & ~(Automap | ObjectVisibility | EffectEligibility)) == 0, "Invalid feedback policy");
}
// Constructed before main, destroyed after the game's atexit room cleanup.
// A function-local static first constructed during play would die BEFORE that
// cleanup and leave FreeAllRooms calling a destroyed scene store.
namespace { SceneAssets GlobalAssets; }
SceneAssets& assets() { return GlobalAssets; }
} // namespace d3scene
