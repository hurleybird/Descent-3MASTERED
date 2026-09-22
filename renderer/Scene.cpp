// Descent 3MASTERED - GPL-3.0-or-later
#include "Scene.h"
#include "SceneTerrain.h"
#include "SceneFog.h"
#include <atomic>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <unordered_set>
#include <utility>
#include <set>
#include <tuple>
#include <algorithm>

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
    require(std::isfinite(surface.specular_strength) && surface.specular_strength >= 0 &&
        std::isfinite(surface.static_specular_strength) && surface.static_specular_strength >= 0 &&
        std::isfinite(surface.specular_exponent) && surface.specular_exponent >= 1 && surface.specular_exponent <= 128 &&
        std::isfinite(surface.specular_lightmap_mix) && surface.specular_lightmap_mix >= 0 && surface.specular_lightmap_mix <= 1 &&
        std::isfinite(surface.specular_alpha_strength) && surface.specular_alpha_strength >= 0,"Invalid specular material response");
    require(static_cast<unsigned>(surface.occlusion_class) <= static_cast<unsigned>(OcclusionClass::Mine),
        "Unknown surface occlusion class");
    require(std::isfinite(surface.soft_depth_range) && surface.soft_depth_range >= 0,"Invalid soft intersection range");
    require(std::isfinite(surface.effect_depth_offset) && (surface.effect_depth_offset == 0 ||
        (surface.unlit && surface.blend != BlendMode::Solid && surface.layer == RenderLayer::World)),
        "Depth offset requires an unlit translucent world effect");
    require(surface.blend == BlendMode::Solid || surface.blend == BlendMode::Alpha || surface.blend == BlendMode::Additive ||
        surface.blend == BlendMode::Multiply,"Invalid material blend mode");
    require(surface.layer == RenderLayer::World || surface.layer == RenderLayer::Background,"Invalid material layer");
    require(finite(surface.base_color) && finite(surface.color_modulation) && finite(surface.ambient_color) && finite(surface.ambient_direction) &&
        std::isfinite(surface.alpha) && surface.alpha >= 0 && surface.alpha <= 1 &&
        std::isfinite(surface.u_offset) && std::isfinite(surface.v_offset) &&
        std::isfinite(surface.texture_scale.x) && std::isfinite(surface.texture_scale.y) &&
        std::isfinite(surface.lighting_modulation) && surface.lighting_modulation >= 0 &&
        std::isfinite(surface.lightmap_u_scale) && std::isfinite(surface.lightmap_v_scale), "Invalid scene material values");
}
void validateGeometry(const MeshGeometry& geometry) {
    require(geometry.raster_offsets.empty() || geometry.raster_offsets.size() == geometry.vertices.size(),"Incomplete raster offsets");
    for (const auto offset : geometry.raster_offsets) require(std::isfinite(offset.x) && std::isfinite(offset.y),"Invalid raster offset");
    require(geometry.vertex_colors.empty() || geometry.vertex_colors.size() == geometry.vertices.size(),"Incomplete mesh colors");
    for (const auto& color : geometry.vertex_colors) require(finite(color),"Invalid mesh color");
    require(geometry.vertex_normals.empty() || geometry.vertex_normals.size() == geometry.vertices.size(), "Incomplete mesh normals");
    for (const auto& normal : geometry.vertex_normals) require(finite(normal), "Non-finite mesh normal");
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
    if (a.raster_offsets.size() != b.raster_offsets.size()) return false;
    for (size_t i = 0; i < a.raster_offsets.size(); ++i)
        if (a.raster_offsets[i].x != b.raster_offsets[i].x || a.raster_offsets[i].y != b.raster_offsets[i].y) return false;
    if (a.vertex_colors.size() != b.vertex_colors.size()) return false;
    for (size_t i = 0; i < a.vertex_colors.size(); ++i) if (!equal(a.vertex_colors[i],b.vertex_colors[i])) return false;
    if (a.vertex_normals.size() != b.vertex_normals.size()) return false;
    for (size_t i = 0; i < a.vertex_normals.size(); ++i)
        if (!equal(a.vertex_normals[i],b.vertex_normals[i])) return false;
    if (a.vertices.size() != b.vertices.size() || a.polygons.size() != b.polygons.size()) return false;
    for (size_t i = 0; i < a.vertices.size(); ++i)
        if (!equal(a.vertices[i], b.vertices[i])) return false;
    for (size_t i = 0; i < a.polygons.size(); ++i) {
        const auto& x = a.polygons[i]; const auto& y = b.polygons[i];
        if (!equal(x.normal, y.normal) || x.corners.size() != y.corners.size() || x.bounds_volume != y.bounds_volume) return false;
        for (size_t j = 0; j < x.corners.size(); ++j) {
            const auto& c = x.corners[j]; const auto& d = y.corners[j];
            if (c.vertex != d.vertex || c.u != d.u || c.v != d.v || c.light_u != d.light_u ||
                c.light_v != d.light_v || c.alpha != d.alpha) return false;
        }
    }
    return true;
}

bool operator==(const VolumeGrid& a,const VolumeGrid& b) {
    return equal(a.origin,b.origin) && a.width == b.width && a.height == b.height && a.depth == b.depth &&
        a.spacing == b.spacing && a.colors == b.colors;
}
RoomAsset::RoomAsset(AssetId id, MeshGeometry geometry, VolumeGrid volume)
    : id_(id), geometry_(std::move(geometry)), volume_(std::move(volume)) {
    if (geometry_.vertices.empty()) return;
    bounds_.minimum = bounds_.maximum = geometry_.vertices.front();
    for (const auto p : geometry_.vertices) {
        bounds_.minimum = {std::min(bounds_.minimum.x,p.x),std::min(bounds_.minimum.y,p.y),std::min(bounds_.minimum.z,p.z)};
        bounds_.maximum = {std::max(bounds_.maximum.x,p.x),std::max(bounds_.maximum.y,p.y),std::max(bounds_.maximum.z,p.z)};
    }
}
SceneAssets::SceneAssets() { reset(); }
void SceneAssets::reset() {
    const uint64_t lifetime = NextLifetime.fetch_add(1);
    if (!lifetime || lifetime == std::numeric_limits<uint64_t>::max())
        throw std::overflow_error("Scene lifetime exhausted");
    rooms_.clear();
    models_.clear();
    images_.clear();
    reflections_.clear();
    terrain_.reset();
    meshes_.clear();
    fog_.reset();
    lifetime_ = lifetime;
    revision_ = 0;
}
RoomAssetRef SceneAssets::room(uint32_t slot) const {
    return slot < rooms_.size() ? rooms_[slot] : nullptr;
}
ReflectionAssetRef SceneAssets::publishReflection(uint32_t slot,ReflectionData data) {
    for (const auto& source : data.sources)
        require(finite(source.position) && finite(source.color) && source.color.x >= 0 && source.color.y >= 0 && source.color.z >= 0,
            "Invalid authored reflection source");
    for (const auto& normal : data.corner_normals) require(finite(normal),"Invalid reflection normal");
    for (const auto& corner : data.field_corners) {
        require(finite(corner.baked_modulation),"Invalid reflection field modulation");
        for (const auto& lobe : corner.lobes) require(finite(lobe.position) && finite(lobe.color) &&
            lobe.color.x >= 0 && lobe.color.y >= 0 && lobe.color.z >= 0,"Invalid reflected field lobe");
    }
    const auto found = reflections_.find(slot);
    const auto previous = found != reflections_.end() ? found->second : nullptr;
    if (previous) {
        const auto& old = previous->data();
        bool same = old.sources.size() == data.sources.size() && old.corner_normals.size() == data.corner_normals.size() &&
            old.field_corners.size() == data.field_corners.size();
        for (size_t i = 0; same && i < old.sources.size(); ++i)
            same = equal(old.sources[i].position,data.sources[i].position) && equal(old.sources[i].color,data.sources[i].color);
        for (size_t i = 0; same && i < old.corner_normals.size(); ++i) same = equal(old.corner_normals[i],data.corner_normals[i]);
        for (size_t i = 0; same && i < old.field_corners.size(); ++i) {
            same = equal(old.field_corners[i].baked_modulation,data.field_corners[i].baked_modulation);
            for (size_t lobe = 0; same && lobe < 4; ++lobe)
                same = equal(old.field_corners[i].lobes[lobe].position,data.field_corners[i].lobes[lobe].position) &&
                    equal(old.field_corners[i].lobes[lobe].color,data.field_corners[i].lobes[lobe].color);
        }
        if (same) return previous;
    }
    if (revision_ == std::numeric_limits<uint64_t>::max()) throw std::overflow_error("Scene revision exhausted");
    ReflectionAssetRef replacement(new ReflectionAsset({lifetime_,revision_+1,slot},std::move(data)));
    reflections_[slot] = replacement;
    ++revision_;
    return replacement;
}
RoomAssetRef SceneAssets::publishRoom(uint32_t slot, MeshGeometry geometry, VolumeGrid volume) {
    validateGeometry(geometry);
    require(geometry.raster_offsets.empty(),"Room geometry cannot use raster markers");
    require(finite(volume.origin) && std::isfinite(volume.spacing) && volume.spacing > 0, "Invalid volume grid placement");
    if (!volume.colors.empty())
        require(volume.width && volume.height && volume.depth &&
            uint64_t(volume.width)*volume.height <= volume.colors.size() &&
            uint64_t(volume.width)*volume.height*volume.depth == volume.colors.size(), "Invalid volume grid dimensions");
    const auto previous = room(slot);
    if (previous && previous->geometry() == geometry && previous->volume() == volume) return previous;
    if (revision_ == std::numeric_limits<uint64_t>::max())
        throw std::overflow_error("Scene revision exhausted");
    RoomAssetRef replacement(new RoomAsset({lifetime_, revision_ + 1, slot}, std::move(geometry),std::move(volume)));
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
            x.polygon_colors.size() != y.polygon_colors.size()) return false;
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
        require(part.geometry.raster_offsets.empty(),"Model geometry cannot use raster markers");
        require(finite(part.offset), "Invalid model offset");
        require(part.geometry.vertex_normals.size() == part.geometry.vertices.size(), "Incomplete model vertex normals");
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

MeshAssetRef SceneAssets::publishMesh(uint32_t slot,MeshGeometry geometry) {
    validateGeometry(geometry);
    if (slot < meshes_.size() && meshes_[slot] && meshes_[slot]->geometry() == geometry) return meshes_[slot];
    if (revision_ == std::numeric_limits<uint64_t>::max()) throw std::overflow_error("Scene revision exhausted");
    MeshAssetRef replacement(new MeshAsset({lifetime_,revision_+1,slot},std::move(geometry)));
    if (meshes_.size() <= slot) meshes_.resize(size_t(slot)+1);
    meshes_[slot] = replacement; ++revision_;
    return replacement;
}
MaterialPolicy compileMaterialPolicy(const SurfaceBinding& surface, const MeshPolygon& polygon) {
    MaterialPolicy result;
    result.layer = surface.layer;
    if (surface.texture_alpha || surface.blend == BlendMode::Additive || surface.blend == BlendMode::Multiply) {
        result.texture_alpha = surface.color_image && surface.color_image->format() != ImageFormat::ARGB1555 ?
            TextureAlpha::Continuous : TextureAlpha::Cutout;
    }
    bool fractional = surface.alpha < 1 || result.texture_alpha == TextureAlpha::Continuous || surface.soft_depth_range > 0;
    for (const auto& corner : polygon.corners) fractional = fractional || corner.alpha < 255;
    result.blend = surface.blend != BlendMode::Solid ? surface.blend : fractional ? BlendMode::Alpha : BlendMode::Solid;
    return result;
}

PresentationRef Presentation::freeze(const FrameData& data) {
    require(data.sequence != 0 && data.scene_lifetime != 0, "Unidentified presentation frame");
    require(std::isfinite(data.simulation_time) && std::isfinite(data.delta_time) && data.delta_time >= 0,
        "Invalid presentation clock");
    require(finite(data.player_control_delta) && std::isfinite(data.afterburner_factor) &&
        data.afterburner_factor >= 0 && data.afterburner_factor <= 1,"Invalid presentation motion inputs");
    require((data.pending_domains & ~AllPendingDomains) == 0, "Unknown pending scene domain");
    require(data.lighting_quality == LightingQuality::Vertex || data.lighting_quality == LightingQuality::Pixel,
        "Unknown lighting quality");
    std::set<std::tuple<LightSource,uint64_t,uint32_t>> emitters;
    std::unordered_set<uint32_t> camera_owners;
    for (const auto& effect : data.camera_perturbations) {
        require(camera_owners.insert(effect.owner).second,"Duplicate camera perturbation owner");
        require(finite(effect.rotation) && effect.rotation.position.x == 0 &&
            effect.rotation.position.y == 0 && effect.rotation.position.z == 0,"Invalid camera perturbation");
    }
    for (const auto& light : data.lights) {
        require(emitters.emplace(light.id.source,light.id.owner,light.id.channel).second, "Duplicate scene emitter identity");
        require(finite(light.position) && finite(light.color) && finite(light.direction) &&
            std::isfinite(light.specular_strength) && light.specular_strength >= 0 &&
            std::isfinite(light.radius) && light.radius > 0 && std::isfinite(light.falloff) && light.falloff > 0 &&
            std::isfinite(light.cone) && light.cone >= -1 && light.cone < 1, "Invalid scene light");
        const auto& n = light.direction;
        require(!light.directional || std::abs(n.x*n.x+n.y*n.y+n.z*n.z-1) < .001f, "Scene light direction is not normalized");
    }
    const auto validateIndices = [&](const SurfaceBinding& surface) {
        require(!surface.reflection || surface.reflection->id().lifetime == data.scene_lifetime,"Stale reflection asset in frame");
        for (auto index : surface.light_indices) require(index < data.lights.size(), "Surface references missing emitter");
        for (const auto& image : {surface.color_image,surface.baked_image,surface.specular_image})
            require(!image || image->id().lifetime == data.scene_lifetime,"Stale image asset in frame");
    };
    require(finite(data.sky_color),"Invalid sky color");
    require(finite(data.exterior_fog.color) && std::isfinite(data.exterior_fog.start) && std::isfinite(data.exterior_fog.end) &&
        (!data.exterior_fog.enabled || data.exterior_fog.end > data.exterior_fog.start),"Invalid exterior fog");
    require(!data.fog || data.fog->id().lifetime == data.scene_lifetime,"Stale fog asset");
    for (const auto& volume : fogVolumes(data)) {
        require(finite(volume.color) && std::isfinite(volume.depth) && volume.depth > 0,"Invalid fog volume");
        require(finite(volume.bounds.minimum) && finite(volume.bounds.maximum),"Invalid fog bounds");
        require(volume.boundary.empty() == volume.nodes.empty(),"Missing fog spatial index");
        for (size_t index = 0; index < volume.nodes.size(); ++index) {
            const auto& node = volume.nodes[index];
            require(finite(node.bounds.minimum) && finite(node.bounds.maximum) && node.escape > index && node.escape <= volume.nodes.size() &&
                uint64_t(node.first)+node.count <= volume.boundary.size(),"Invalid fog spatial node");
        }
        for (const auto& triangle : volume.boundary)
            require(finite(triangle.a) && finite(triangle.b) && finite(triangle.c) && finite(triangle.inward),"Invalid fog boundary");
    }
    for (const auto& mesh : data.meshes) {
        require(mesh.asset && mesh.asset->id().lifetime == data.scene_lifetime,"Stale generic mesh");
        require(mesh.asset->geometry().raster_offsets.empty() || mesh.surface.layer == RenderLayer::Background,
            "Raster markers require a background layer");
        require(finite(mesh.world) && finite(mesh.camera_translation),"Invalid generic mesh transform");
        require(std::isfinite(mesh.eye_distance) && mesh.eye_distance >= 0,"Invalid view-relative mesh distance");
        require(std::isfinite(mesh.size_fade_range.x) && std::isfinite(mesh.size_fade_range.y) &&
            ((mesh.size_fade_range.x == 0 && mesh.size_fade_range.y == 0) ||
                (mesh.size_fade_range.x >= 0 && mesh.size_fade_range.y > mesh.size_fade_range.x)),"Invalid mesh size fade range");
        require(mesh.alignment == MeshAlignment::World || mesh.alignment == MeshAlignment::Camera ||
            mesh.alignment == MeshAlignment::FacingEye || mesh.alignment == MeshAlignment::CameraUpright ||
            mesh.alignment == MeshAlignment::Axis,"Invalid mesh alignment");
        validateSurface(mesh.surface); validateIndices(mesh.surface);
    }
    if (data.terrain.asset) {
        require(data.terrain.asset->id().lifetime == data.scene_lifetime,"Stale terrain asset");
        require(data.terrain.materials.size() == data.terrain.asset->grid().materials.size(),"Incomplete terrain materials");
        for (const auto& surface : data.terrain.materials) { validateSurface(surface); validateIndices(surface); }
        require(data.terrain.patches.size() == data.terrain.asset->patches().size(),"Incomplete terrain patch bindings");
        for (size_t patch = 0; patch < data.terrain.patches.size(); ++patch) {
            require(data.terrain.patches[patch].size() == data.terrain.asset->patches()[patch].batches.size(),"Incomplete terrain batch bindings");
            for (const auto& surface : data.terrain.patches[patch]) { validateSurface(surface); validateIndices(surface); }
        }
    } else require(data.terrain.materials.empty() && data.terrain.patches.empty(),"Terrain materials have no grid");
    std::unordered_map<int32_t,const RoomState*> rooms;
    for (const auto& room : data.rooms) {
        require(room.number >= 0 && rooms.emplace(room.number,&room).second, "Duplicate or invalid scene room");
        require(room.asset && room.asset->id().lifetime == data.scene_lifetime &&
            room.asset->id().slot == static_cast<uint32_t>(room.number), "Room asset identity mismatch");
        require(room.surfaces.size() == room.asset->geometry().polygons.size(), "Incomplete room surface bindings");
        for (const auto& surface : room.surfaces) validateSurface(surface);
        for (const auto& surface : room.surfaces) validateIndices(surface);
        for (size_t face = 0; face < room.surfaces.size(); ++face) {
            const auto& reflection = room.surfaces[face].reflection;
            require(!reflection || reflection->data().corner_normals.empty() ||
                reflection->data().corner_normals.size() == room.asset->geometry().polygons[face].corners.size(),
                "Incomplete room reflection normals");
            require(!reflection || reflection->data().field_corners.empty() ||
                reflection->data().field_corners.size() == room.asset->geometry().polygons[face].corners.size(),
                "Incomplete room reflection field");
        }
        require(finite(room.fog_color) && std::isfinite(room.fog_depth), "Invalid room fog");
        require(std::isfinite(room.light_scalar) && room.light_scalar >= 0, "Invalid room illumination");
        for (const auto& portal : room.portals)
            require(portal.polygon >= 0 && static_cast<size_t>(portal.polygon) < room.surfaces.size(),
                "Portal references missing polygon");
    }
    for (const auto& mesh : data.meshes) {
        require(mesh.receiver_polygon >= -1,"Invalid mesh receiver polygon");
        if (mesh.receiver_polygon < 0) continue;
        const auto room = rooms.find(mesh.room);
        require(room != rooms.end() && static_cast<size_t>(mesh.receiver_polygon) < room->second->surfaces.size() &&
            mesh.surface.layer == RenderLayer::World,"Mesh receiver is not a world surface");
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
                const size_t vertex_count = object.model->geometry().parts[i].geometry.vertices.size();
                for (const auto* positions : {&part.vertices,&part.previous_vertices}) {
                    require(positions->empty() || positions->size() == vertex_count,"Incomplete prepared mesh positions");
                    for (const auto v : *positions) require(finite(v),"Non-finite prepared mesh position");
                }
                require(part.surfaces.size() == object.model->geometry().parts[i].geometry.polygons.size(),
                    "Incomplete model instance materials");
                for (const auto& surface : part.surfaces) validateSurface(surface);
                for (const auto& surface : part.surfaces) validateIndices(surface);
                for (size_t face = 0; face < part.surfaces.size(); ++face) {
                    const auto& uv = part.surfaces[face].baked_uvs;
                    require(uv.empty() || uv.size() == object.model->geometry().parts[i].geometry.polygons[face].corners.size(),
                        "Incomplete instance lightmap UVs");
                    for (const auto& corner : uv) require(std::isfinite(corner.x) && std::isfinite(corner.y), "Invalid instance lightmap UVs");
                }
            }
        } else require(object.parts.empty(), "Model pose has no asset");
    }
    for (const auto& mesh : data.meshes)
        require(!mesh.has_owner || objects.count(mesh.owner),"Mesh owner is not part of this presentation");
    std::shared_ptr<Presentation> prepared(new Presentation(data));
    for (auto& mesh : prepared->data_.meshes) {
        mesh.surface.material = compileMaterialPolicy(mesh.surface,MeshPolygon{});
        for (const auto& polygon : mesh.asset->geometry().polygons) {
            const auto policy = compileMaterialPolicy(mesh.surface,polygon);
            if (policy.blended()) mesh.surface.material = policy;
        }
    }
    for (auto& surface : prepared->data_.terrain.materials)
        surface.material = compileMaterialPolicy(surface,MeshPolygon{});
    for (auto& patch : prepared->data_.terrain.patches) for (auto& surface : patch)
        surface.material = compileMaterialPolicy(surface,MeshPolygon{});
    for (auto& room : prepared->data_.rooms)
        for (size_t face = 0; face < room.surfaces.size(); ++face)
            room.surfaces[face].material = compileMaterialPolicy(room.surfaces[face],room.asset->geometry().polygons[face]);
    for (auto& object : prepared->data_.objects) {
        object.has_render_bounds = false;
        const auto include = [](Bounds& bounds,bool& valid,Vec3 point) {
            require(finite(point),"Non-finite posed model position");
            if (!valid) { bounds = {point,point}; valid = true; return; }
            bounds.minimum = {(std::min)(bounds.minimum.x,point.x),(std::min)(bounds.minimum.y,point.y),(std::min)(bounds.minimum.z,point.z)};
            bounds.maximum = {(std::max)(bounds.maximum.x,point.x),(std::max)(bounds.maximum.y,point.y),(std::max)(bounds.maximum.z,point.z)};
        };
        for (size_t part = 0; part < object.parts.size(); ++part) {
            auto& pose = object.parts[part];
            pose.has_render_bounds = false;
            const auto& geometry = object.model->geometry().parts[part].geometry;
            const auto& positions = pose.vertices.empty() ? geometry.vertices : pose.vertices;
            const auto world = compose(object.current,pose.current);
            if (pose.draw && !geometry.polygons.empty()) {
                for (const auto vertex : positions) include(pose.render_bounds,pose.has_render_bounds,transformPoint(world,vertex));
                if (pose.has_render_bounds) {
                    include(object.render_bounds,object.has_render_bounds,pose.render_bounds.minimum);
                    include(object.render_bounds,object.has_render_bounds,pose.render_bounds.maximum);
                }
            }
            for (size_t face = 0; face < object.parts[part].surfaces.size(); ++face)
                object.parts[part].surfaces[face].material = compileMaterialPolicy(object.parts[part].surfaces[face],
                    object.model->geometry().parts[part].geometry.polygons[face]);
        }
    }
    return prepared;
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
