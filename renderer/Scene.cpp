// Descent 3MASTERED - GPL-3.0-or-later
#include "Scene.h"
#include "../model/ModelPose.h"
#include "SceneCompositing.h"
#include "SceneTerrain.h"
#include "SceneFog.h"
#include "SceneGeometry.h"
#include <atomic>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <unordered_set>
#include <utility>
#include <set>
#include <map>
#include <tuple>
#include <algorithm>

namespace d3scene {
bool RoomSurfaces::publishedForRoom(AssetId asset,const std::vector<PlanarSurface>& planar) const {
    if (!publishedFor(asset)) return false;
    if (!planar_) return planar.empty();
    if (planar_->size() != planar.size()) return false;
    for (size_t i = 0; i < planar.size(); ++i) {
        const auto& a = (*planar_)[i]; const auto& b = planar[i];
        if (a.plane.normal.x != b.plane.normal.x || a.plane.normal.y != b.plane.normal.y ||
            a.plane.normal.z != b.plane.normal.z || a.plane.constant != b.plane.constant || a.polygons != b.polygons) return false;
    }
    return true;
}
void RoomSurfaces::seal(AssetId asset,uint32_t part,bool palette,const std::vector<PlanarSurface>* planar) {
    // Publication is a bulk operation. Reuse contiguous scratch storage rather
    // than allocating one tree node per image/reflection on the render thread.
    // Published tables own copies; subsequent calls cannot mutate held frames.
    thread_local std::vector<ImageDependency> unique;
    thread_local std::vector<uint64_t> reflections;
    unique.clear(); reflections.clear();
    const auto image = [&](ImageDependency dependency) {
        const auto key = std::make_tuple(dependency.handle,dependency.baked,dependency.texture);
        const auto at = std::lower_bound(unique.begin(),unique.end(),key,
            [](const ImageDependency& a,const std::tuple<int,bool,int>& b) { return std::make_tuple(a.handle,a.baked,a.texture) < b; });
        if (at == unique.end() || std::make_tuple(at->handle,at->baked,at->texture) != key) unique.insert(at,std::move(dependency));
        else if (dependency.classify_alpha) *at = dependency;
    };
    required_light_count_ = 0;
    for (const auto& surface : read()) {
        for (auto light : surface.light_indices) required_light_count_ = (std::max)(required_light_count_,uint64_t(light)+1);
        if (surface.reflection && surface.specular_strength > 0) reflections.push_back(surface.reflection->id().revision);
        if (surface.bitmap >= 0) image({surface.bitmap,false,surface.color_image,surface.color_format,false,1,1,true,surface.color_texture});
        if (surface.specular_bitmap >= 0) image({surface.specular_bitmap,false,surface.specular_image,surface.specular_format,false,1,1,true,surface.specular_texture});
        if (surface.lightmap_bitmap >= 0) image({surface.lightmap_bitmap,true,surface.baked_image,ImageFormat::ARGB1555,surface.lightmap_wrap,surface.lightmap_u_scale,surface.lightmap_v_scale});
    }
    images_ = std::make_shared<const std::vector<ImageDependency>>(unique);
    // Dependency scratch contains names/metadata, never frame image ownership.
    unique.clear();
    publication_ = asset; publication_part_ = part; publication_palette_ = palette;
    std::sort(reflections.begin(),reflections.end());
    reflections.erase(std::unique(reflections.begin(),reflections.end()),reflections.end());
    reflections_ = std::make_shared<const std::vector<uint64_t>>(reflections.begin(),reflections.end());
    planar_ = planar && !planar->empty() ? std::make_shared<const std::vector<PlanarSurface>>(*planar) : nullptr;
}
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
    require(!surface.view_image_slot || (!surface.color_image && surface.bitmap < 0),
        "A surface cannot sample both an asset and a view image");
    require(finite(surface.raster_stripe) && (surface.raster_stripe.x == 0 || surface.raster_stripe.x >= 1) &&
        surface.raster_stripe.z >= 0 && surface.raster_stripe.z <= 1,"Invalid raster stripe coverage");
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
    require(surface.depth == DepthMode::Automatic || surface.depth == DepthMode::ReadWrite ||
        surface.depth == DepthMode::ReadOnly || surface.depth == DepthMode::Disabled,"Invalid material depth policy");
    require(surface.layer != RenderLayer::Background || surface.depth == DepthMode::Automatic ||
        surface.depth == DepthMode::Disabled,"Background cannot participate in raster depth");
    require(finite(surface.base_color) && finite(surface.color_modulation) && finite(surface.ambient_color) && finite(surface.ambient_direction) &&
        std::isfinite(surface.alpha) && surface.alpha >= 0 && surface.alpha <= 1 &&
        std::isfinite(surface.u_offset) && std::isfinite(surface.v_offset) &&
        std::isfinite(surface.scroll_period.x) && std::isfinite(surface.scroll_period.y) &&
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
Bounds transformBounds(const Transform& transform,const Bounds& local) {
    Bounds result;
    result.minimum = result.maximum = transformPoint(transform,local.minimum);
    for (unsigned corner = 1; corner < 8; ++corner) {
        const auto point = transformPoint(transform,{corner&1 ? local.maximum.x : local.minimum.x,
            corner&2 ? local.maximum.y : local.minimum.y,corner&4 ? local.maximum.z : local.minimum.z});
        result.minimum = {std::min(result.minimum.x,point.x),std::min(result.minimum.y,point.y),std::min(result.minimum.z,point.z)};
        result.maximum = {std::max(result.maximum.x,point.x),std::max(result.maximum.y,point.y),std::max(result.maximum.z,point.z)};
    }
    return result;
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
    image_source_revisions_.clear();
    reflections_.clear();
    terrain_.reset();
    terrain_occlusion_.reset();
    meshes_.clear();
    quad_meshes_.fill({});
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
    if (bool(a.animation) != bool(b.animation) || (a.animation && !(*a.animation == *b.animation))) return false;
    if (a.pose_uses_clock != b.pose_uses_clock) return false;
    if (a.rest_pose.size() != b.rest_pose.size()) return false;
    for (size_t i = 0; i < a.rest_pose.size(); ++i) {
        const auto& x = a.rest_pose[i]; const auto& y = b.rest_pose[i];
        if (!equal(x.position,y.position) || !equal(x.right,y.right) || !equal(x.up,y.up) || !equal(x.forward,y.forward)) return false;
    }
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
ModelAsset::ModelAsset(AssetId id, ModelGeometry geometry) : id_(id), geometry_(std::move(geometry)) {
    for (auto& part : geometry_.parts) {
        if (!part.geometry.vertices.empty()) {
            part.local_bounds.minimum = part.local_bounds.maximum = part.geometry.vertices.front();
            for (const auto p : part.geometry.vertices) {
                auto& b = part.local_bounds;
                b.minimum = {std::min(b.minimum.x,p.x),std::min(b.minimum.y,p.y),std::min(b.minimum.z,p.z)};
                b.maximum = {std::max(b.maximum.x,p.x),std::max(b.maximum.y,p.y),std::max(b.maximum.z,p.z)};
            }
        }
        std::map<std::tuple<int,float,float,float,bool>,uint32_t> palette;
        part.surface_for_polygon.clear(); part.surface_polygons.clear();
        for (size_t face = 0; face < part.geometry.polygons.size(); ++face) {
            bool fractional = false;
            for (const auto& corner : part.geometry.polygons[face].corners) fractional = fractional || corner.alpha < 255;
            const auto color = part.polygon_colors[face];
            const auto key = std::make_tuple(part.polygon_materials[face],color.x,color.y,color.z,fractional);
            const auto inserted = palette.emplace(key,uint32_t(part.surface_polygons.size()));
            if (inserted.second) part.surface_polygons.push_back(uint32_t(face));
            part.surface_for_polygon.push_back(inserted.first->second);
        }
    }
    const auto length = [](Vec3 p) { return std::sqrt(p.x*p.x+p.y*p.y+p.z*p.z); };
    for (size_t i = 0; i < geometry_.parts.size(); ++i) {
        auto& part = geometry_.parts[i];
        float radius = 0;
        part.hierarchy_hidden = false;
        for (const auto vertex : part.geometry.vertices) radius = std::max(radius,length(vertex));
        for (int parent = int(i); parent != -1; parent = geometry_.parts[parent].parent) {
            const auto& ancestor = geometry_.parts[parent];
            part.hierarchy_hidden = part.hierarchy_hidden || ancestor.geometry.polygons.empty() ||
                (ancestor.authored_flags & (SOF_SHELL|SOF_FRONTFACE));
            radius += length(geometry_.parts[parent].offset);
            float translation = 0;
            if (geometry_.animation) for (const auto key : geometry_.animation->submodel[parent].keyframe_pos)
                translation = std::max(translation,length({key.x,key.y,key.z}));
            radius += translation;
        }
        require(std::isfinite(radius),"Invalid model motion extent");
        part.motion_radius = radius;
    }
}
ModelAssetRef SceneAssets::model(uint32_t slot) const {
    return slot < models_.size() ? models_[slot] : nullptr;
}
ModelAssetRef SceneAssets::publishModel(uint32_t slot, ModelGeometry geometry) {
    require(!geometry.animation || (geometry.animation->n_models >= 0 &&
        size_t(geometry.animation->n_models) == geometry.parts.size() && geometry.animation->submodel.size() == geometry.parts.size()),
        "Incomplete model animation hierarchy");
    require(geometry.rest_pose.empty() || geometry.rest_pose.size() == geometry.parts.size(),"Incomplete model rest pose");
    for (const auto& transform : geometry.rest_pose) require(finite(transform),"Invalid model rest pose");
    for (size_t i = 0; i < geometry.parts.size(); ++i) {
        const auto& part = geometry.parts[i];
        if (geometry.animation) {
            const auto& animated = geometry.animation->submodel[i];
            require(animated.parent == part.parent && animated.offset.x == part.offset.x &&
                animated.offset.y == part.offset.y && animated.offset.z == part.offset.z,"Animation and geometry hierarchy disagree");
        }
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
    // A caller may retain a mutable shared alias while building. Publication
    // severs it, just as it owns the topology passed by value.
    if (geometry.animation) geometry.animation = std::make_shared<const ModelAnimation>(*geometry.animation);
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

MeshAssetRef SceneAssets::quadMesh(uint32_t count) {
    if (!count || count > quad_meshes_.size()) throw std::invalid_argument("Invalid canonical quad batch size");
    auto& asset = quad_meshes_[count-1];
    if (!asset) asset = publishMesh(count == 1 ? 2 : 256+count,unitQuadBatch(count));
    return asset;
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
        result.texture_alpha = surface.view_image_slot || (surface.color_image && surface.color_format != ImageFormat::ARGB1555) ?
            TextureAlpha::Continuous : TextureAlpha::Cutout;
    }
    bool fractional = surface.alpha < 1 || result.texture_alpha == TextureAlpha::Continuous || surface.soft_depth_range > 0;
    for (const auto& corner : polygon.corners) fractional = fractional || corner.alpha < 255;
    result.blend = surface.blend != BlendMode::Solid ? surface.blend : fractional ? BlendMode::Alpha : BlendMode::Solid;
    result.depth = surface.depth != DepthMode::Automatic ? surface.depth :
        surface.layer == RenderLayer::Background ? DepthMode::Disabled :
        result.blended() ? DepthMode::ReadOnly : DepthMode::ReadWrite;
    return result;
}

const ForegroundLayer* Presentation::foreground(uint64_t identity) const {
    for (const auto& foreground : data_.foregrounds)
        if (foreground.identity == identity) return &foreground.layer;
    return nullptr;
}

namespace {
void compileModelBounds(const Transform& placement,PreparedModel& model) {
    const auto include = [](Bounds& bounds,bool& valid,Vec3 point) {
        require(finite(point),"Non-finite posed model position");
        if (!valid) { bounds = {point,point}; valid = true; return; }
        bounds.minimum = {std::min(bounds.minimum.x,point.x),std::min(bounds.minimum.y,point.y),std::min(bounds.minimum.z,point.z)};
        bounds.maximum = {std::max(bounds.maximum.x,point.x),std::max(bounds.maximum.y,point.y),std::max(bounds.maximum.z,point.z)};
    };
    model.has_render_bounds = false;
    if (model.deferred_pose) {
        float radius = 0;
        for (size_t i = 0; i < model.parts.size(); ++i) {
            const auto& pose = model.parts.read()[i];
            const auto& part = model.asset->geometry().parts[i];
            if (!pose.draw || part.geometry.polygons.empty()) continue;
            radius = std::max(radius,part.motion_radius*(1+std::abs(model.deformation)));
            model.has_render_bounds = true;
        }
        if (model.has_render_bounds)
            model.render_bounds = transformBounds(placement,{{-radius,-radius,-radius},{radius,radius,radius}});
        return;
    }
    for (size_t index = 0; index < model.parts.size(); ++index) {
        auto& pose = model.parts[index];
        const auto& part = model.asset->geometry().parts[index];
        pose.has_render_bounds = false;
        if (!pose.draw || part.geometry.polygons.empty()) continue;
        {
            const auto world = compose(placement,pose.current);
            if (pose.vertices.empty()) {
                pose.render_bounds = transformBounds(world,part.local_bounds); pose.has_render_bounds = true;
            } else for (const auto vertex : pose.vertices)
                include(pose.render_bounds,pose.has_render_bounds,transformPoint(world,vertex));
        }
        if (pose.has_render_bounds) {
            include(model.render_bounds,model.has_render_bounds,pose.render_bounds.minimum);
            include(model.render_bounds,model.has_render_bounds,pose.render_bounds.maximum);
        }
    }
}
}
const PreparedModel& Presentation::resolvedModel(size_t instance,size_t variant) const {
    const auto& object = data_.model_instances.at(instance);
    const auto& source = object.models.at(variant);
    if (!source.deferred_pose) return source;
    auto& cached = resolved_models_.at(source.resolved_index);
    if (cached) return *cached;
    auto result = std::make_unique<PreparedModel>(source);
    const auto& animation = *source.asset->geometry().animation;
    const auto input = [](const std::shared_ptr<const std::vector<float>>& values) { return values ? values->data() : nullptr; };
    const auto current = ComposeModelPose(animation,EvaluateModelPose(animation,input(source.pose_inputs),float(data_.simulation_time)));
    const auto previous = source.previous_pose_valid ?
        ComposeModelPose(animation,EvaluateModelPose(animation,input(source.previous_pose_inputs),float(source.previous_pose_time))) : current;
    const auto value = [](const ModelSubmodelTransform& t) {
        const auto v = [](const vector& p) { return Vec3{p.x,p.y,p.z}; };
        return Transform{v(t.translation),v(t.orientation.rvec),v(t.orientation.uvec),v(t.orientation.fvec)};
    };
    uint32_t surface_base = source.surface_base;
    for (size_t i = 0; i < result->parts.size(); ++i) {
        auto& part = result->parts[i];
        part.surface_base = surface_base; surface_base += uint32_t(part.surfaces.size());
        part.has_previous = source.previous_pose_valid && (source.previous_draw_mask & (uint32_t(1)<<i));
        part.light_influences_prepared = source.light_influences_prepared;
        part.light_candidates = source.light_candidates;
        part.current = value(current.submodels[i]); part.previous = value(previous.submodels[i]);
        const auto& geometry = source.asset->geometry().parts[i].geometry;
        part.vertices = deformMesh(geometry,source.deformation,data_.simulation_time,source.deformation_owner,uint32_t(i));
        part.previous_vertices = source.previous_pose_valid ?
            deformMesh(geometry,source.previous_deformation,source.previous_pose_time,source.deformation_owner,uint32_t(i)) : part.vertices;
    }
    result->deferred_pose = false;
    compileModelBounds(object.current,*result);
    cached = std::move(result);
    return *cached;
}
PresentationRef Presentation::freeze(const FrameData& data) {
    return complete(data);
}
namespace {
size_t imageHash(uint32_t binding) {
    binding ^= binding >> 16; binding *= 0x7feb352du;
    binding ^= binding >> 15; binding *= 0x846ca68bu;
    return binding ^ (binding >> 16);
}
size_t imageSlot(const std::vector<FrameData::ImageBinding>& images,
    const std::vector<uint32_t>& slots,uint32_t binding) {
    size_t slot = imageHash(binding) & (slots.size()-1);
    while (slots[slot] && images[slots[slot]-1].binding != binding)
        slot = (slot+1) & (slots.size()-1);
    return slot;
}
}
const ImageAsset& Presentation::image(uint32_t binding) const {
    if (!binding) throw std::invalid_argument("Absent scene image binding");
    const auto index = image_slots_[imageSlot(data_.images,image_slots_,binding)];
    if (!index)
        throw std::invalid_argument("Missing frame image binding");
    return *data_.images[index-1].asset;
}
PresentationRef Presentation::publish(FrameData&& data) {
    return complete(std::move(data));
}
PresentationRef Presentation::complete(FrameData data) {
    require(data.sequence != 0 && data.scene_lifetime != 0, "Unidentified presentation frame");
    require(std::isfinite(data.simulation_time) && std::isfinite(data.delta_time) && data.delta_time >= 0,
        "Invalid presentation clock");
    require(finite(data.player_control_delta) && std::isfinite(data.afterburner_factor) &&
        data.afterburner_factor >= 0 && data.afterburner_factor <= 1,"Invalid presentation motion inputs");
    require((data.pending_domains & ~AllPendingDomains) == 0, "Unknown pending scene domain");
    std::unordered_set<uint64_t> foreground_ids;
    std::set<std::pair<ForegroundPhase,int32_t>> foreground_order;
    for (const auto& foreground : data.foregrounds) {
        require(foreground.identity && foreground_ids.insert(foreground.identity).second,
            "Invalid or duplicate foreground identity");
        require(foreground.phase == ForegroundPhase::WorldOverlay || foreground.phase == ForegroundPhase::FinalOverlay,
            "Unknown foreground phase");
        require(foreground_order.emplace(foreground.phase,foreground.order).second,"Ambiguous foreground order");
        validateForeground(foreground.layer);
    }
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
    for (const auto& image : data.images)
        require(image.binding && image.asset && image.asset->id().lifetime == data.scene_lifetime,"Stale image asset in frame");
    std::sort(data.images.begin(),data.images.end(),[](const FrameData::ImageBinding& a,const FrameData::ImageBinding& b) { return a.binding < b.binding; });
    require(data.images.size() < UINT32_MAX/2,"Too many frame images");
    size_t image_capacity = 1;
    while (image_capacity < data.images.size()*2) image_capacity *= 2;
    std::vector<uint32_t> image_slots(image_capacity,0);
    for (size_t i = 0; i < data.images.size(); ++i) {
        auto& slot = image_slots[imageSlot(data.images,image_slots,data.images[i].binding)];
        require(!slot,"Duplicate frame image slot");
        slot = uint32_t(i+1);
    }
    const auto validateImage = [&](uint32_t binding) -> const ImageAsset* {
        if (!binding) return nullptr;
        const auto index = image_slots[imageSlot(data.images,image_slots,binding)];
        require(index != 0,"Missing frame image binding");
        return data.images[index-1].asset.get();
    };
    const auto validateDependency = [&](const RoomSurfaces::ImageDependency& dependency) {
        const auto* image = validateImage(dependency.binding);
        require(!image || !dependency.classify_alpha || image->format() == dependency.format,"Image format changed without material recompilation");
    };
    const auto validateIndices = [&](const SurfaceBinding& surface) {
        require(!surface.reflection || surface.reflection->id().lifetime == data.scene_lifetime,"Stale reflection asset in frame");
        for (auto index : surface.light_indices) require(index < data.lights.size(), "Surface references missing emitter");
        for (auto image : {surface.color_image,surface.baked_image,surface.specular_image}) validateImage(image);
        const auto* color = validateImage(surface.color_image);
        const auto* specular = validateImage(surface.specular_image);
        require(!color || color->format() == surface.color_format,"Image format changed without material recompilation");
        require(!specular || specular->format() == surface.specular_format,"Specular format changed without material recompilation");
    };
    require(finite(data.sky_color),"Invalid sky color");
    require(finite(data.exterior_fog.color) && std::isfinite(data.exterior_fog.start) && std::isfinite(data.exterior_fog.end) &&
        (!data.exterior_fog.enabled || data.exterior_fog.end > data.exterior_fog.start),"Invalid exterior fog");
    require(!data.fog || data.fog->id().lifetime == data.scene_lifetime,"Stale fog asset");
    // Fog topology is immutable and validated by its publishing owner. Only
    // the asset lifetime belongs to this frame's validation boundary.
    for (const auto& mesh : data.meshes) {
        require(mesh.asset && mesh.asset->id().lifetime == data.scene_lifetime,"Stale generic mesh");
        require(mesh.vertices.empty() || mesh.vertices.size() == mesh.asset->geometry().vertices.size(),
            "Incomplete prepared mesh positions");
        for (const auto& vertex : mesh.vertices) require(finite(vertex),"Invalid prepared mesh position");
        require(mesh.texture_uvs.empty() || mesh.texture_uvs.size() == mesh.asset->geometry().vertices.size(),
            "Incomplete prepared mesh texture coordinates");
        for (const auto uv : mesh.texture_uvs) require(std::isfinite(uv.x) && std::isfinite(uv.y),"Invalid prepared texture coordinate");
        require(mesh.asset->geometry().raster_offsets.empty() || mesh.surface.layer == RenderLayer::Background,
            "Raster markers require a background layer");
        require(finite(mesh.world) && finite(mesh.camera_translation),"Invalid generic mesh transform");
        require(std::isfinite(mesh.eye_distance) && mesh.eye_distance >= 0,"Invalid view-relative mesh distance");
        require(std::isfinite(mesh.maximum_depth) && mesh.maximum_depth >= 0,"Invalid mesh detail depth");
        if (mesh.maximum_depth > 0) {
            const auto& b = mesh.depth_reference;
            require(finite(b.minimum) && finite(b.maximum) && b.minimum.x <= b.maximum.x &&
                b.minimum.y <= b.maximum.y && b.minimum.z <= b.maximum.z,"Invalid mesh detail reference volume");
        }
        require(std::isfinite(mesh.size_fade_range.x) && std::isfinite(mesh.size_fade_range.y) &&
            ((mesh.size_fade_range.x == 0 && mesh.size_fade_range.y == 0) ||
                (mesh.size_fade_range.x >= 0 && mesh.size_fade_range.y > mesh.size_fade_range.x)),"Invalid mesh size fade range");
        require(mesh.alignment == MeshAlignment::World || mesh.alignment == MeshAlignment::Camera ||
            mesh.alignment == MeshAlignment::FacingEye || mesh.alignment == MeshAlignment::CameraUpright ||
            mesh.alignment == MeshAlignment::Axis,"Invalid mesh alignment");
        validateSurface(mesh.surface); validateIndices(mesh.surface);
    }
    require(!data.terrain.occlusion || data.terrain.occlusion->id().lifetime == data.scene_lifetime,
        "Stale terrain occlusion asset");
    require(std::isfinite(data.terrain.observation_distance) && data.terrain.observation_distance >= 0,
        "Invalid exterior observation distance");
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
    SceneLookup<int32_t,size_t> rooms;
    rooms.reserve(data.rooms.size());
    for (const auto& room : data.rooms) {
        require(room.number >= 0 && rooms.emplace(room.number,size_t(&room-data.rooms.data())).second, "Duplicate or invalid scene room");
        require(room.asset && room.asset->id().lifetime == data.scene_lifetime &&
            room.asset->id().slot == static_cast<uint32_t>(room.number), "Room asset identity mismatch");
        require(room.surfaces.size() == room.asset->geometry().polygons.size(), "Incomplete room surface bindings");
        const bool retained = room.surfaces.publishedForRoom(room.asset->id(),room.mirrors);
        if (!retained) {
        validatePlanarSurfaces(room.asset->geometry(),room.mirrors);
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
        } else {
            // Static asset ownership was validated when this table was sealed.
            // Explicit authored emitter indices still belong to this frame.
            require(room.surfaces.requiredLightCount() <= data.lights.size(),"Surface references missing emitter");
            for (const auto& dependency : room.surfaces.images()) validateDependency(dependency);
        }
        for (auto index : room.light_candidates)
            require(index < data.lights.size(),"Room references missing emitter");
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
        require(room != rooms.end() && static_cast<size_t>(mesh.receiver_polygon) < data.rooms[room->second].surfaces.size() &&
            mesh.collection == 0 && mesh.surface.layer == RenderLayer::World,"Mesh receiver is not a world surface");
    }
    SceneLookup<uint32_t,size_t> objects;
    objects.reserve(data.objects.size());
    for (const auto& object : data.objects) {
        require(objects.emplace(object.handle,size_t(&object-data.objects.data())).second, "Duplicate object lifetime in scene");
        require(finite(object.current) && finite(object.previous) && std::isfinite(object.radius) && object.radius >= 0,
            "Invalid object transform or radius");
        if (data.observation_inputs_prepared && object.observation_eligible) {
            const auto& b = object.observation_bounds;
            require(finite(b.minimum) && finite(b.maximum) && b.minimum.x <= b.maximum.x &&
                b.minimum.y <= b.maximum.y && b.minimum.z <= b.maximum.z &&
                std::isfinite(object.observation_radius) && object.observation_radius >= 0,
                "Invalid simulation observation volume");
        }
    }
    SceneLookup<uint64_t,size_t> model_instances;
    model_instances.reserve(data.model_instances.size());
    for (const auto& object : data.objects)
        for (const auto hidden : object.camera_hidden_objects)
            require(objects.count(hidden) != 0,"Camera rig references a missing object allocation");
    for (const auto& object : data.model_instances) {
        require(object.identity && model_instances.emplace(object.identity,size_t(&object-data.model_instances.data())).second,"Duplicate or absent model instance identity");
        require(!object.has_owner || objects.count(object.owner),"Model owner is not part of this presentation");
        require(finite(object.current) && finite(object.previous),"Invalid model placement");
        require(!object.models.empty(),"Model instance has no variants");
        require(std::isfinite(object.lod_distances.x) && std::isfinite(object.lod_distances.y) &&
            object.lod_distances.x >= 0 && object.lod_distances.y >= 0,"Invalid model LOD distances");
        for (const auto index : object.lod_models)
            require(object.models.empty() ? index == 0 : index < object.models.size(),"LOD references an absent model variant");
        for (const auto& model : object.models) {
            require(model.asset && model.asset->id().lifetime == data.scene_lifetime, "Stale model asset in frame");
            if (model.deferred_pose) {
                require(model.parts.size() <= MAX_SUBOBJECTS,"Too many deferred model parts");
                require(model.asset->geometry().animation != nullptr,"Deferred pose has no owned animation");
                require(std::isfinite(model.deformation) && std::isfinite(model.previous_deformation) &&
                    std::isfinite(model.previous_pose_time),"Invalid deferred pose inputs");
                for (const auto* input : {&model.pose_inputs,&model.previous_pose_inputs}) if (*input) {
                    require((*input)->size() == model.asset->geometry().parts.size(),"Incomplete animation inputs");
                    for (float value : **input) require(std::isfinite(value) && value >= 0 && value <= 1,"Invalid animation input");
                }
                for (auto light : model.light_candidates) require(light < data.lights.size(),"Missing deferred model light");
            }
            require(model.parts.size() == model.asset->geometry().parts.size(), "Incomplete object pose");
            for (size_t i = 0; i < model.parts.size(); ++i) {
                const auto& part = model.parts[i];
                require(finite(part.current) && finite(part.previous), "Invalid model instance transform");
                const size_t vertex_count = model.asset->geometry().parts[i].geometry.vertices.size();
                for (const auto* positions : {&part.vertices,&part.previous_vertices}) {
                    require(positions->empty() || positions->size() == vertex_count,"Incomplete prepared mesh positions");
                    for (const auto v : *positions) require(finite(v),"Non-finite prepared mesh position");
                }
                const auto& asset_part = model.asset->geometry().parts[i];
                require(part.surfaces.size() == (part.material_palette ? asset_part.surface_polygons.size() : asset_part.geometry.polygons.size()),
                    "Incomplete model instance materials");
                for (auto light : part.light_candidates) require(light < data.lights.size(),"Missing model light");
                if (!part.surfaces.publishedFor(model.asset->id(),uint32_t(i),part.material_palette)) {
                for (const auto& surface : part.surfaces) validateSurface(surface);
                for (const auto& surface : part.surfaces) validateIndices(surface);
                for (size_t face = 0; face < part.surfaces.size(); ++face) {
                    const auto& uv = part.surfaces[face].baked_uvs;
                    const auto polygon = part.material_palette ? asset_part.surface_polygons[face] : uint32_t(face);
                    require((!part.material_palette || uv.empty()) && (uv.empty() || uv.size() == asset_part.geometry.polygons[polygon].corners.size()),
                        "Incomplete instance lightmap UVs");
                    for (const auto& corner : uv) require(std::isfinite(corner.x) && std::isfinite(corner.y), "Invalid instance lightmap UVs");
                }
                } else {
                    require(part.surfaces.requiredLightCount() <= data.lights.size(),"Surface references missing emitter");
                    for (const auto& dependency : part.surfaces.images()) validateDependency(dependency);
                }
            }
        }
    }
    for (const auto& mesh : data.meshes) {
        require(!mesh.has_owner || objects.count(mesh.owner),"Mesh owner is not part of this presentation");
        require(mesh.model_variant >= -1 && (mesh.model_instance == 0 ? mesh.model_variant == -1 :
            (model_instances.count(mesh.model_instance) && mesh.model_variant >= 0 &&
                size_t(mesh.model_variant) < data.model_instances[model_instances.at(mesh.model_instance)].models.size() &&
                mesh.collection == data.model_instances[model_instances.at(mesh.model_instance)].collection)),
            "Model attachment references an absent instance variant");
        require(mesh.model_part >= -1 && (mesh.model_part < 0 || (mesh.model_instance &&
            size_t(mesh.model_part) < data.model_instances[model_instances.at(mesh.model_instance)].models[mesh.model_variant].parts.size())),
            "Model attachment references an absent pose part");
    }
    std::shared_ptr<Presentation> prepared(new Presentation(std::move(data)));
    prepared->room_indices_ = std::move(rooms);
    prepared->image_slots_ = std::move(image_slots);
    prepared->object_indices_ = std::move(objects);
    prepared->model_indices_ = std::move(model_instances);
    auto& surface_count = prepared->data_.compiled_surface_count;
    surface_count = 0;
    const auto index_surface = [&](SurfaceBinding& surface) { surface.compiled_index = surface_count++; };
    for (auto& room : prepared->data_.rooms) {
        room.surface_base = surface_count;
        surface_count += static_cast<uint32_t>(room.surfaces.size());
    }
    for (auto& surface : prepared->data_.terrain.materials) index_surface(surface);
    for (auto& patch : prepared->data_.terrain.patches) for (auto& surface : patch) index_surface(surface);
    for (auto& mesh : prepared->data_.meshes) index_surface(mesh.surface);
    for (auto& object : prepared->data_.model_instances) for (auto& model : object.models) {
        model.surface_base = surface_count;
        for (size_t p = 0; p < model.parts.size(); ++p) {
            if (!model.deferred_pose) model.parts[p].surface_base = surface_count;
            surface_count += uint32_t(model.parts.read()[p].surfaces.size());
        }
    }
    std::sort(prepared->data_.foregrounds.begin(),prepared->data_.foregrounds.end(),
        [](const PreparedForeground& a,const PreparedForeground& b) {
            return a.phase != b.phase ? a.phase < b.phase : a.order < b.order;
        });
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
    for (auto& room : prepared->data_.rooms) {
        if (room.surfaces.publishedForRoom(room.asset->id(),room.mirrors)) continue;
        for (size_t face = 0; face < room.surfaces.size(); ++face)
            room.surfaces[face].material = compileMaterialPolicy(room.surfaces[face],room.asset->geometry().polygons[face]);
        for (size_t group = 0; group < room.mirrors.size(); ++group) for (auto face : room.mirrors[group].polygons) {
            auto& surface = room.surfaces[face];
            require(surface.layer == RenderLayer::World && surface.soft_depth_range == 0 &&
                surface.effect_depth_offset == 0 && surface.raster_stripe.x == 0 &&
                (surface.blend == BlendMode::Solid || surface.blend == BlendMode::Alpha),
                "Planar reflection requires a solid surface film, not an effect");
            auto& policy = surface.material;
            policy.planar_slot = (uint64_t(uint32_t(room.number)+1)<<32)|uint64_t(group+1);
            policy.blend = BlendMode::Solid; policy.depth = DepthMode::ReadWrite;
            policy.texture_alpha = TextureAlpha::Continuous; policy.alpha_cutoff = 0;
        }
        room.surfaces.seal(room.asset->id(),UINT32_MAX,false,&room.mirrors);
    }
    uint32_t resolved_index = 0;
    for (auto& object : prepared->data_.model_instances) {
        object.has_render_bounds = false;
        const auto include = [](Bounds& bounds,bool& valid,Vec3 point) {
            require(finite(point),"Non-finite posed model position");
            if (!valid) { bounds = {point,point}; valid = true; return; }
            bounds.minimum = {(std::min)(bounds.minimum.x,point.x),(std::min)(bounds.minimum.y,point.y),(std::min)(bounds.minimum.z,point.z)};
            bounds.maximum = {(std::max)(bounds.maximum.x,point.x),(std::max)(bounds.maximum.y,point.y),(std::max)(bounds.maximum.z,point.z)};
        };
        for (auto& model : object.models) {
            model.resolved_index = resolved_index++;
            compileModelBounds(object.current,model);
            if (model.has_render_bounds) {
                include(object.render_bounds,object.has_render_bounds,model.render_bounds.minimum);
                include(object.render_bounds,object.has_render_bounds,model.render_bounds.maximum);
            }
            for (size_t part = 0; part < model.parts.size(); ++part) {
                const auto& input = model.parts.read()[part];
                const auto& geometry = model.asset->geometry().parts[part].geometry;
                if (!input.surfaces.publishedFor(model.asset->id(),uint32_t(part),input.material_palette)) {
                auto& pose = model.parts[part];
                for (size_t face = 0; face < pose.surfaces.size(); ++face) {
                    const auto polygon = pose.material_palette ? model.asset->geometry().parts[part].surface_polygons[face] : uint32_t(face);
                    pose.surfaces[face].material = compileMaterialPolicy(pose.surfaces[face],geometry.polygons[polygon]);
                }
                pose.surfaces.seal(model.asset->id(),uint32_t(part),pose.material_palette);
                }
            }
            model.parts.seal();
        }
    }
    prepared->resolved_models_.resize(resolved_index);
    return prepared;
}
void Presentation::requireExecutable() const {
    if (data_.pending_domains != 0)
        throw std::logic_error("Renderer migration incomplete: presentation contains unprepared domains");
}
void validateView(const View& view) {
    require(view.reflections == ReflectionPolicy::Images || view.reflections == ReflectionPolicy::SurfaceFilm,
        "Invalid planar reflection policy");
    require(view.fog_path_origin == FogPathOrigin::Camera || view.fog_path_origin == FogPathOrigin::ClipPlane,
        "Invalid fog path origin");
    require(view.fog_path_origin != FogPathOrigin::ClipPlane || view.has_clip_plane,
        "Fog path starting at a clip plane requires an explicit plane");
    require(view.projection == ViewProjection::Perspective || view.projection == ViewProjection::Orthographic,"Invalid view projection");
    require(view.projection != ViewProjection::Orthographic ||
        (view.kind != ViewKind::Main && view.feedback == NoFeedback &&
         std::isfinite(view.orthographic_size.x) && std::isfinite(view.orthographic_size.y) &&
         view.orthographic_size.x > 0 && view.orthographic_size.y > 0 &&
         std::isfinite(view.orthographic_depth.x) && std::isfinite(view.orthographic_depth.y) &&
         view.orthographic_depth.y > view.orthographic_depth.x),"Invalid orthographic view volume or ownership");
    require(view.ordering == ViewOrdering::Depth || view.ordering == ViewOrdering::Compositing, "Invalid view ordering");
    require(view.ordering != ViewOrdering::Compositing || (view.kind != ViewKind::Main && view.feedback == NoFeedback),
        "Compositing views cannot own main-world products or gameplay feedback");
    require(view.target == ViewTarget::Scene || view.target == ViewTarget::Display, "Invalid view target");
    require(view.target != ViewTarget::Display || (view.kind != ViewKind::Main && view.feedback == NoFeedback),
        "Display views cannot own main-world products or gameplay feedback");
    require(view.history_key != 0, "View has no history identity");
    require(view.excluded_surfaces.empty() || view.feedback == NoFeedback,
        "Selected geometry requires separately qualified gameplay feedback");
    std::unordered_set<uint64_t> image_slots;
    for (const auto& input : view.image_inputs)
        require(input.slot && input.image && image_slots.insert(input.slot).second,"Invalid or duplicate view image input");
    require(finite(view.camera) && std::isfinite(view.zoom) && view.zoom > 0, "Invalid view camera");
    if (view.has_clip_plane) {
        const auto n = view.clip_plane.normal;
        const double length2 = double(n.x)*n.x+double(n.y)*n.y+double(n.z)*n.z;
        require(finite(n) && std::isfinite(view.clip_plane.constant) && std::abs(length2-1) < .0001,
            "World clip plane must have a finite unit normal");
        require(view.projection == ViewProjection::Perspective && view.ordering == ViewOrdering::Depth &&
            view.target == ViewTarget::Scene && view.feedback == NoFeedback,
            "Clipped views require scene geometry and separately qualified feedback");
    }
    require(view.viewport.width > 0 && view.viewport.height > 0 &&
        view.projection_anchor.width > 0 && view.projection_anchor.height > 0, "Invalid view extent");
    require((view.feedback & ~(Automap | ObjectVisibility | EffectEligibility)) == 0, "Invalid feedback policy");
    require(view.collection == 0 || (view.kind != ViewKind::Main && view.feedback == NoFeedback),
        "Non-world collections cannot own main-world products or gameplay feedback");
}
void validateViewImage(const ViewImageRequest& request) {
    validateView(request.view);
    const auto& view = request.view;
    require(request.image && view.kind != ViewKind::Main && view.target == ViewTarget::Scene &&
        view.feedback == NoFeedback && view.viewport.x == 0 && view.viewport.y == 0,
        "Independent view image requires a named, local, observation-free camera");
    require(request.scale == 1 || request.scale == 2 || request.scale == 4,"Invalid view image scale");
    require(request.samples == 0 || request.samples == 2 || request.samples == 4 || request.samples == 8,
        "Invalid view image samples");
    require(uint64_t(view.viewport.width)*request.scale <= uint64_t((std::numeric_limits<int32_t>::max)()) &&
        uint64_t(view.viewport.height)*request.scale <= uint64_t((std::numeric_limits<int32_t>::max)()),
        "View image extent overflow");
}
// Constructed before main, destroyed after the game's atexit room cleanup.
// A function-local static first constructed during play would die BEFORE that
// cleanup and leave FreeAllRooms calling a destroyed scene store.
namespace { SceneAssets GlobalAssets; }
SceneAssets& assets() { return GlobalAssets; }
} // namespace d3scene
