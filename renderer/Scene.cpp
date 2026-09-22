// Descent 3MASTERED - GPL-3.0-or-later
#include "Scene.h"
#include <atomic>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <unordered_set>
#include <utility>
#include <set>
#include <tuple>

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
void validateSurface(const SurfaceBinding& surface) {
    require(finite(surface.base_color) && std::isfinite(surface.alpha) && surface.alpha >= 0 && surface.alpha <= 1 &&
        std::isfinite(surface.u_offset) && std::isfinite(surface.v_offset) &&
        std::isfinite(surface.lightmap_u_scale) && std::isfinite(surface.lightmap_v_scale), "Invalid scene material values");
}
void validateGeometry(const MeshGeometry& geometry) {
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
Vec3 transformDirection(const Transform& t, Vec3 p) {
    return {t.right.x*p.x+t.up.x*p.y+t.forward.x*p.z,
        t.right.y*p.x+t.up.y*p.y+t.forward.y*p.z, t.right.z*p.x+t.up.z*p.y+t.forward.z*p.z};
}
Vec3 transformPoint(const Transform& t, Vec3 p) {
    const auto r = transformDirection(t,p);
    return {r.x+t.position.x,r.y+t.position.y,r.z+t.position.z};
}
Transform compose(const Transform& parent, const Transform& local) {
    return {transformPoint(parent,local.position),transformDirection(parent,local.right),
        transformDirection(parent,local.up),transformDirection(parent,local.forward)};
}
bool operator!=(AssetId a, AssetId b) { return !(a == b); }
bool operator==(const MeshGeometry& a, const MeshGeometry& b) {
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

RoomAsset::RoomAsset(AssetId id, MeshGeometry geometry) : id_(id), geometry_(std::move(geometry)) {}
SceneAssets::SceneAssets() { reset(); }
void SceneAssets::reset() {
    const uint64_t lifetime = NextLifetime.fetch_add(1);
    if (!lifetime || lifetime == std::numeric_limits<uint64_t>::max())
        throw std::overflow_error("Scene lifetime exhausted");
    rooms_.clear();
    models_.clear();
    lifetime_ = lifetime;
    revision_ = 0;
}
RoomAssetRef SceneAssets::room(uint32_t slot) const {
    return slot < rooms_.size() ? rooms_[slot] : nullptr;
}
RoomAssetRef SceneAssets::publishRoom(uint32_t slot, MeshGeometry geometry) {
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

bool operator==(const ModelGeometry& a, const ModelGeometry& b) {
    if (a.textures != b.textures || a.parts.size() != b.parts.size()) return false;
    for (size_t i = 0; i < a.parts.size(); ++i) {
        const auto& x = a.parts[i]; const auto& y = b.parts[i];
        if (!(x.geometry == y.geometry) || x.parent != y.parent || !equal(x.offset, y.offset) ||
            x.authored_flags != y.authored_flags || x.polygon_materials != y.polygon_materials ||
            x.vertex_normals.size() != y.vertex_normals.size() || x.polygon_colors.size() != y.polygon_colors.size()) return false;
        for (size_t j = 0; j < x.vertex_normals.size(); ++j)
            if (!equal(x.vertex_normals[j], y.vertex_normals[j])) return false;
        for (size_t j = 0; j < x.polygon_colors.size(); ++j)
            if (!equal(x.polygon_colors[j], y.polygon_colors[j])) return false;
    }
    return true;
}
ModelAsset::ModelAsset(AssetId id, ModelGeometry geometry) : id_(id), geometry_(std::move(geometry)) {}
ModelAssetRef SceneAssets::model(uint32_t slot) const {
    return slot < models_.size() ? models_[slot] : nullptr;
}
ModelAssetRef SceneAssets::publishModel(uint32_t slot, ModelGeometry geometry) {
    for (size_t i = 0; i < geometry.parts.size(); ++i) {
        const auto& part = geometry.parts[i];
        validateGeometry(part.geometry);
        require(finite(part.offset), "Invalid model offset");
        require(part.vertex_normals.size() == part.geometry.vertices.size(), "Incomplete model vertex normals");
        for (const auto& normal : part.vertex_normals) require(finite(normal), "Invalid model normal");
        require(part.polygon_materials.size() == part.geometry.polygons.size() &&
            part.polygon_colors.size() == part.geometry.polygons.size(), "Incomplete model materials");
        for (size_t j = 0; j < part.polygon_materials.size(); ++j) {
            const int material = part.polygon_materials[j];
            require(material == -1 || (material >= 0 && static_cast<size_t>(material) < geometry.textures.size()),
                "Model polygon references missing material");
            require(finite(part.polygon_colors[j]), "Invalid model polygon color");
        }
        int ancestor = static_cast<int>(i);
        size_t steps = 0;
        while (ancestor != -1) {
            require(ancestor >= 0 && static_cast<size_t>(ancestor) < geometry.parts.size(), "Invalid model parent");
            require(++steps <= geometry.parts.size(), "Cyclic scene model hierarchy");
            ancestor = geometry.parts[ancestor].parent;
        }
    }
    const auto previous = model(slot);
    if (previous && previous->geometry() == geometry) return previous;
    if (revision_ == std::numeric_limits<uint64_t>::max()) throw std::overflow_error("Scene revision exhausted");
    ModelAssetRef replacement(new ModelAsset({lifetime_, revision_ + 1, slot}, std::move(geometry)));
    if (slot >= models_.size()) models_.resize(static_cast<size_t>(slot) + 1);
    models_[slot] = replacement;
    ++revision_;
    return replacement;
}
void SceneAssets::removeModel(uint32_t slot) {
    if (slot < models_.size()) models_[slot].reset();
}

PresentationRef Presentation::freeze(const FrameData& data) {
    require(data.sequence != 0 && data.scene_lifetime != 0, "Unidentified presentation frame");
    require(std::isfinite(data.simulation_time) && std::isfinite(data.delta_time) && data.delta_time >= 0,
        "Invalid presentation clock");
    require((data.pending_domains & ~AllPendingDomains) == 0, "Unknown pending scene domain");
    std::set<std::tuple<LightSource,uint64_t,uint32_t>> emitters;
    for (const auto& light : data.lights) {
        require(emitters.emplace(light.id.source,light.id.owner,light.id.channel).second, "Duplicate scene emitter identity");
        require(finite(light.position) && finite(light.color) && finite(light.direction) &&
            std::isfinite(light.radius) && light.radius > 0 && std::isfinite(light.falloff) && light.falloff > 0 &&
            std::isfinite(light.cone) && light.cone >= -1 && light.cone < 1, "Invalid scene light");
        const auto& n = light.direction;
        require(!light.directional || std::abs(n.x*n.x+n.y*n.y+n.z*n.z-1) < .001f, "Scene light direction is not normalized");
    }
    const auto validateIndices = [&](const SurfaceBinding& surface) {
        for (auto index : surface.light_indices) require(index < data.lights.size(), "Surface references missing emitter");
    };
    std::unordered_set<int32_t> rooms;
    for (const auto& room : data.rooms) {
        require(room.number >= 0 && rooms.insert(room.number).second, "Duplicate or invalid scene room");
        require(room.asset && room.asset->id().lifetime == data.scene_lifetime &&
            room.asset->id().slot == static_cast<uint32_t>(room.number), "Room asset identity mismatch");
        require(room.surfaces.size() == room.asset->geometry().polygons.size(), "Incomplete room surface bindings");
        for (const auto& surface : room.surfaces) validateSurface(surface);
        for (const auto& surface : room.surfaces) validateIndices(surface);
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
        if (object.model) {
            require(object.model->id().lifetime == data.scene_lifetime, "Stale model asset in frame");
            require(object.parts.size() == object.model->geometry().parts.size(), "Incomplete object pose");
            for (size_t i = 0; i < object.parts.size(); ++i) {
                const auto& part = object.parts[i];
                require(finite(part.current) && finite(part.previous), "Invalid model instance transform");
                require(part.surfaces.size() == object.model->geometry().parts[i].geometry.polygons.size(),
                    "Incomplete model instance materials");
                for (const auto& surface : part.surfaces) validateSurface(surface);
                for (const auto& surface : part.surfaces) validateIndices(surface);
            }
        } else require(object.parts.empty(), "Model pose has no asset");
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
