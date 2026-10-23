// Descent 3MASTERED - GPL-3.0-or-later
#include "Scene.h"
#include "../model/ModelPose.h"
#include "SceneCompositing.h"
#include "SceneTerrain.h"
#include "SceneFog.h"
#include "SceneGeometry.h"
#include "SceneDependencies.h"
#include "SceneLighting.h"
#include <atomic>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <unordered_set>
#include <utility>
#include <map>
#include <tuple>
#include <algorithm>
#include <thread>

namespace d3scene {
struct ReportStoragePool {
    struct Buffers { std::vector<int32_t> rooms; std::vector<uint32_t> objects; };
    std::array<Buffers,16> spare;
    size_t count = 0;
    const std::thread::id publisher = std::this_thread::get_id();
};
VisibilityReport::VisibilityReport() {
    static thread_local auto pool = std::make_shared<ReportStoragePool>();
    storage_pool_ = pool;
    if (pool->count) {
        auto& buffers = pool->spare[--pool->count];
        rooms.swap(buffers.rooms); objects.swap(buffers.objects);
    }
}
VisibilityReport::~VisibilityReport() {
    const auto& pool = storage_pool_;
    if (!pool || pool->publisher != std::this_thread::get_id() || pool->count == pool->spare.size()) return;
    auto& buffers = pool->spare[pool->count++];
    rooms.clear(); objects.clear(); rooms.swap(buffers.rooms); objects.swap(buffers.objects);
}
VisibilityReport::VisibilityReport(const VisibilityReport& other) : VisibilityReport() { *this = other; }
VisibilityReport& VisibilityReport::operator=(const VisibilityReport& other) {
    if (this != &other) {
        frame_sequence = other.frame_sequence; scene_lifetime = other.scene_lifetime;
        view_history_key = other.view_history_key; feedback = other.feedback; terrain = other.terrain;
        rooms = other.rooms; objects = other.objects;
    }
    return *this;
}
VisibilityReport& VisibilityReport::operator=(VisibilityReport&& other) noexcept {
    // A renderer's empty report is often replaced by game eligibility. Swap
    // its spare capacity into the retiring temporary instead of freeing it.
    if (this != &other) {
        std::swap(frame_sequence,other.frame_sequence); std::swap(scene_lifetime,other.scene_lifetime);
        std::swap(view_history_key,other.view_history_key); std::swap(feedback,other.feedback); std::swap(terrain,other.terrain);
        rooms.swap(other.rooms); objects.swap(other.objects); storage_pool_.swap(other.storage_pool_);
    }
    return *this;
}
// Foreground layers are frame-owned, but their released view-list capacity is
// reusable. Local HUD/cockpit/effect builders must not allocate those same
// short lists every frame. Held copies remain independent; foreign-thread
// destruction never writes the publisher's pool.
struct ForegroundStoragePool {
    struct Buffers { std::vector<View> scene, display; };
    std::array<Buffers,32> spare;
    size_t count = 0;
    const std::thread::id publisher = std::this_thread::get_id();
};
ForegroundLayer::ForegroundLayer() {
    static thread_local auto pool = std::make_shared<ForegroundStoragePool>();
    storage_pool_ = pool;
    if (pool->count) {
        auto& buffers = pool->spare[--pool->count];
        scene_views.swap(buffers.scene); display_views.swap(buffers.display);
    }
}
ForegroundLayer::ForegroundLayer(std::initializer_list<View> scene,std::initializer_list<View> display) : ForegroundLayer() {
    scene_views.assign(scene); display_views.assign(display);
}
ForegroundLayer::~ForegroundLayer() {
    const auto& pool = storage_pool_;
    if (!pool || pool->publisher != std::this_thread::get_id() || pool->count == pool->spare.size()) return;
    auto& buffers = pool->spare[pool->count++];
    scene_views.clear(); display_views.clear();
    scene_views.swap(buffers.scene); display_views.swap(buffers.display);
}
// Compilation storage follows the snapshot lifetime, just like its input
// arrays. Recycle only after the last reader releases the presentation.
struct FrameCompilationStorage {
    SceneIdentityIndex<int32_t> room_indices_;
    SceneIdentityIndex<uint32_t> object_indices_;
    SceneIdentityIndex<uint64_t> model_indices_;
    SceneLookup<uint64_t,size_t> collection_indices_;
    std::vector<uint32_t> image_slots_;
    std::vector<ResolvedModel> resolved_models_;
    std::vector<PreparedModelPart> resolved_parts_;
    size_t resolved_part_count_ = 0;
    std::vector<uint32_t> resolved_model_indices_;
    struct CollectionRange {
        uint32_t model_begin = 0,model_count = 0,mesh_begin = 0,mesh_count = 0;
        bool has_receivers = false;
    };
    std::vector<CollectionRange> collections_;
    std::vector<uint32_t> collection_models_,collection_meshes_;
    // Asset-free layout proofs survive recycling, like the identity indexes.
    // Payload changes do not change ordered collection membership.
    std::vector<std::pair<uint64_t,uint64_t>> collection_model_keys_;
    std::vector<std::tuple<uint64_t,uint64_t,bool>> collection_mesh_keys_;
    bool collections_valid_ = false;
    void clear() {
        image_slots_.clear(); resolved_models_.clear(); releasePoses(); resolved_model_indices_.clear();
    }
    void releasePoses() {
        // Only dead snapshots enter the pool. Retain vertex buffer capacity,
        // but never retain the frame's material owners in its spare slots.
        for (size_t i = 0; i < resolved_part_count_; ++i) {
            auto& part = resolved_parts_[i];
            part.surfaces.clear();
            part.vertices.clear(); part.previous_vertices.clear();
        }
        resolved_part_count_ = 0;
    }
};
const SceneLookup<int32_t,size_t>& Presentation::roomIndices() const { return storage_->room_indices_.lookup(); }
const SceneLookup<uint32_t,size_t>& Presentation::objectIndices() const { return storage_->object_indices_.lookup(); }
const SceneLookup<uint64_t,size_t>& Presentation::modelIndices() const { return storage_->model_indices_.lookup(); }
bool RoomSurfaces::publishedForRoom(AssetId asset,const std::vector<PlanarSurface>& planar) const {
    if (!publishedFor(asset)) return false;
    if (table_->planar.size() != planar.size()) return false;
    for (size_t i = 0; i < planar.size(); ++i) {
        const auto& a = table_->planar[i]; const auto& b = planar[i];
        if (a.plane.normal.x != b.plane.normal.x || a.plane.normal.y != b.plane.normal.y ||
            a.plane.normal.z != b.plane.normal.z || a.plane.constant != b.plane.constant || a.polygons != b.polygons) return false;
    }
    return true;
}
void RoomSurfaces::seal(AssetId asset,uint32_t part,bool palette,const std::vector<PlanarSurface>* planar) {
    if (!table_) table_ = std::make_shared<Table>();
    else if (table_.use_count() != 1) table_ = std::make_shared<Table>(table_->values);
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
    uint64_t required_light_count = 0;
    for (const auto& surface : read()) {
        for (auto light : surface.light_indices) required_light_count = (std::max)(required_light_count,uint64_t(light)+1);
        if (surface.reflection && surface.specular_strength > 0) reflections.push_back(surface.reflection->id().revision);
        if (surface.bitmap >= 0) image({surface.bitmap,false,surface.color_image,surface.color_format,false,1,1,true,surface.color_texture});
        if (surface.specular_bitmap >= 0) image({surface.specular_bitmap,false,surface.specular_image,surface.specular_format,false,1,1,true,surface.specular_texture});
        if (surface.lightmap_bitmap >= 0) image({surface.lightmap_bitmap,true,surface.baked_image,ImageFormat::ARGB1555,surface.lightmap_wrap,surface.lightmap_u_scale,surface.lightmap_v_scale});
    }
    auto images = std::make_shared<const std::vector<ImageDependency>>(unique);
    // Dependency scratch contains names/metadata, never frame image ownership.
    unique.clear();
    std::sort(reflections.begin(),reflections.end());
    reflections.erase(std::unique(reflections.begin(),reflections.end()),reflections.end());
    auto reflected = std::make_shared<const std::vector<uint64_t>>(reflections.begin(),reflections.end());
    auto planes = planar ? *planar : std::vector<PlanarSurface>{};
    table_->images = std::move(images); table_->reflections = std::move(reflected);
    table_->planar = std::move(planes); table_->required_light_count = required_light_count;
    table_->part = part; table_->palette = palette; table_->publication = asset;
}
void ModelParts::seal(const ModelAsset& asset) {
    if (publishedFor(asset.id())) return;
    if (!table_) table_ = std::make_shared<Table>();
    else if (table_.use_count() != 1) table_ = std::make_shared<Table>(table_->values);
    Summary compiled;
    auto* summary = &compiled;
    summary->asset = asset.id();
    std::vector<RoomSurfaces::ImageDependency> images;
    std::vector<uint64_t> reflections;
    for (size_t i = 0; i < size(); ++i) {
        const auto& part = read()[i];
        const auto& geometry = asset.geometry().parts[i];
        if (part.draw && i < 32) summary->drawn_parts |= uint32_t(1)<<i;
        summary->surface_count += part.surfaces.size();
        summary->required_light_count = std::max(summary->required_light_count,part.surfaces.requiredLightCount());
        summary->required_candidate_count = std::max(summary->required_candidate_count,
            uint64_t(part.light_candidates.offset)+part.light_candidates.count);
        if (part.draw && !geometry.geometry.polygons.empty()) {
            summary->has_geometry = true;
            summary->motion_radius = std::max(summary->motion_radius,geometry.motion_radius);
            if (!asset.geometry().rest_pose.empty()) {
                const auto bounds = transformBounds(asset.geometry().rest_pose[i],geometry.local_bounds);
                if (!summary->has_rest_bounds) { summary->rest_bounds = bounds; summary->has_rest_bounds = true; }
                else {
                    auto& b = summary->rest_bounds;
                    b.minimum = {std::min(b.minimum.x,bounds.minimum.x),std::min(b.minimum.y,bounds.minimum.y),std::min(b.minimum.z,bounds.minimum.z)};
                    b.maximum = {std::max(b.maximum.x,bounds.maximum.x),std::max(b.maximum.y,bounds.maximum.y),std::max(b.maximum.z,bounds.maximum.z)};
                }
            }
        }
        images.insert(images.end(),part.surfaces.images().begin(),part.surfaces.images().end());
        reflections.insert(reflections.end(),part.surfaces.reflections().begin(),part.surfaces.reflections().end());
    }
    const auto key = [](const RoomSurfaces::ImageDependency& d) {
        return std::make_tuple(d.binding,d.handle,d.texture,d.baked,d.format,d.wrap,d.u_scale,d.v_scale,d.classify_alpha);
    };
    std::sort(images.begin(),images.end(),[&](const auto& a,const auto& b) { return key(a)<key(b); });
    images.erase(std::unique(images.begin(),images.end(),[&](const auto& a,const auto& b) { return key(a)==key(b); }),images.end());
    summary->images = std::make_shared<const std::vector<RoomSurfaces::ImageDependency>>(std::move(images));
    std::sort(reflections.begin(),reflections.end());
    reflections.erase(std::unique(reflections.begin(),reflections.end()),reflections.end());
    summary->reflections = std::make_shared<const std::vector<uint64_t>>(std::move(reflections));
    table_->publication = std::move(compiled); table_->sealed = true;
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
    require(surface.texture_filter == TextureFilter::Default || surface.texture_filter == TextureFilter::Nearest ||
        surface.texture_filter == TextureFilter::Linear,"Invalid surface texture filtering");
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
    // Each output component is monotonic in each input coordinate. Select its
    // two support corners directly. Unlike center/extents arithmetic this keeps
    // transformPoint's operation order, including its rounding at large offsets.
    const auto axis = [&](float a,float b,float c,float translation,float& minimum,float& maximum) {
        minimum = (a*(a >= 0 ? local.minimum.x : local.maximum.x)+
            b*(b >= 0 ? local.minimum.y : local.maximum.y)+
            c*(c >= 0 ? local.minimum.z : local.maximum.z))+translation;
        maximum = (a*(a >= 0 ? local.maximum.x : local.minimum.x)+
            b*(b >= 0 ? local.maximum.y : local.minimum.y)+
            c*(c >= 0 ? local.maximum.z : local.minimum.z))+translation;
    };
    axis(transform.right.x,transform.up.x,transform.forward.x,transform.position.x,result.minimum.x,result.maximum.x);
    axis(transform.right.y,transform.up.y,transform.forward.y,transform.position.y,result.minimum.y,result.maximum.y);
    axis(transform.right.z,transform.up.z,transform.forward.z,transform.position.z,result.minimum.z,result.maximum.z);
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
    producer_states_.clear();
    rooms_.clear();
    models_.clear();
    images_.clear();
    image_source_revisions_.clear();
    reflections_.clear();
    terrain_.reset();
    terrain_occlusion_.reset();
    meshes_.clear();
    quad_meshes_.fill({});
    fading_stroke_mesh_.reset();
    stroke_mesh_.reset(); radial_disk_mesh_.reset(); gradient_quad_mesh_.reset();
    fog_.reset();
    lifetime_ = lifetime;
    revision_ = 0;
}
void SceneAssets::retainProducerState(uint64_t lifetime,std::shared_ptr<void> state) {
    require(lifetime == lifetime_ && state != nullptr,"Producer cache belongs to a retired scene");
    producer_states_.push_back(std::move(state));
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
        if (part.authored_flags & (SOF_FACING|SOF_GLOW|SOF_THRUSTER))
            view_attachments_.push_back(uint32_t(&part-geometry_.parts.data()));
        if (!part.geometry.vertices.empty()) {
            part.local_bounds.minimum = part.local_bounds.maximum = part.geometry.vertices.front();
            for (const auto p : part.geometry.vertices) {
                auto& b = part.local_bounds;
                b.minimum = {std::min(b.minimum.x,p.x),std::min(b.minimum.y,p.y),std::min(b.minimum.z,p.z)};
                b.maximum = {std::max(b.maximum.x,p.x),std::max(b.maximum.y,p.y),std::max(b.maximum.z,p.z)};
            }
        }
        std::map<std::tuple<int,float,float,float,bool>,uint32_t> palette;
        part.surface_for_polygon.clear(); part.surface_polygons.clear(); part.surface_groups.clear();
        for (size_t face = 0; face < part.geometry.polygons.size(); ++face) {
            bool fractional = false;
            for (const auto& corner : part.geometry.polygons[face].corners) fractional = fractional || corner.alpha < 255;
            const auto color = part.polygon_colors[face];
            const auto key = std::make_tuple(part.polygon_materials[face],color.x,color.y,color.z,fractional);
            const auto inserted = palette.emplace(key,uint32_t(part.surface_polygons.size()));
            if (inserted.second) {
                part.surface_polygons.push_back(uint32_t(face));
                part.surface_groups.emplace_back();
            }
            part.surface_for_polygon.push_back(inserted.first->second);
            part.surface_groups[inserted.first->second].push_back(uint32_t(face));
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
MeshAssetRef SceneAssets::fadingStrokeMesh() {
    if (!fading_stroke_mesh_) fading_stroke_mesh_ = publishMesh(9,unitFadingStroke());
    return fading_stroke_mesh_;
}
MeshAssetRef SceneAssets::strokeMesh() {
    if (!stroke_mesh_) {
        auto geometry = unitFadingStroke();
        for (auto& corner : geometry.polygons[0].corners) corner.alpha = 255;
        stroke_mesh_ = publishMesh(10,std::move(geometry));
    }
    return stroke_mesh_;
}
MeshAssetRef SceneAssets::radialDiskMesh() {
    if (!radial_disk_mesh_) radial_disk_mesh_ = publishMesh(11,billboardRing(0,255,0,16));
    return radial_disk_mesh_;
}
MeshAssetRef SceneAssets::gradientQuadMesh() {
    if (!gradient_quad_mesh_) {
        auto geometry = unitBillboardQuad();
        for (auto& corner : geometry.polygons[0].corners) corner.alpha = corner.vertex == 0 || corner.vertex == 3 ? 0 : 255;
        gradient_quad_mesh_ = publishMesh(12,std::move(geometry));
    }
    return gradient_quad_mesh_;
}
MeshAsset::MeshAsset(AssetId id,MeshGeometry geometry) : id_(id), geometry_(std::move(geometry)) {
    for (const auto& polygon : geometry_.polygons)
        for (const auto& corner : polygon.corners)
            if (corner.alpha < 255) { vertex_alpha_ = true; return; }
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
MeshAssetRef SceneAssets::createMesh(MeshGeometry geometry) {
    validateGeometry(geometry);
    if (revision_ == std::numeric_limits<uint64_t>::max()) throw std::overflow_error("Scene revision exhausted");
    MeshAssetRef replacement(new MeshAsset({lifetime_,revision_+1,UINT32_MAX},std::move(geometry)));
    ++revision_;
    return replacement;
}
SurfaceModulation modulatedSurface(const SurfaceBinding& surface,const SurfaceModulation& instance) {
    const auto multiply = [](Vec3 a,Vec3 b) { return Vec3{a.x*b.x,a.y*b.y,a.z*b.z}; };
    return {multiply(surface.ambient_color,instance.ambient),multiply(surface.color_modulation,instance.color),surface.alpha*instance.alpha};
}
MaterialPolicy modulatedMaterialPolicy(const SurfaceBinding& surface,float alpha) {
    auto policy = surface.material;
    if (alpha < 1 && !policy.blended()) {
        policy.blend = BlendMode::Alpha;
        if (surface.depth == DepthMode::Automatic)
            policy.depth = surface.layer == RenderLayer::Background ? DepthMode::Disabled : DepthMode::ReadOnly;
    }
    return policy;
}
namespace {
MaterialPolicy materialPolicy(const SurfaceBinding& surface, bool vertex_alpha) {
    MaterialPolicy result;
    result.layer = surface.layer;
    result.medium_transport = surface.medium_transport && surface.layer == RenderLayer::World;
    if (surface.texture_alpha || surface.blend == BlendMode::Additive || surface.blend == BlendMode::Multiply) {
        result.texture_alpha = surface.view_image_slot || (surface.color_image && surface.color_format != ImageFormat::ARGB1555) ?
            TextureAlpha::Continuous : TextureAlpha::Cutout;
    }
    const bool fractional = vertex_alpha || surface.alpha < 1 || result.texture_alpha == TextureAlpha::Continuous || surface.soft_depth_range > 0;
    result.blend = surface.blend != BlendMode::Solid ? surface.blend : fractional ? BlendMode::Alpha : BlendMode::Solid;
    result.depth = surface.depth != DepthMode::Automatic ? surface.depth :
        surface.layer == RenderLayer::Background ? DepthMode::Disabled :
        result.blended() ? DepthMode::ReadOnly : DepthMode::ReadWrite;
    return result;
}
}
MaterialPolicy compileMaterialPolicy(const SurfaceBinding& surface, const MeshPolygon& polygon) {
    return materialPolicy(surface,std::any_of(polygon.corners.begin(),polygon.corners.end(),
        [](const MeshCorner& corner) { return corner.alpha < 255; }));
}

const ForegroundLayer* Presentation::foreground(uint64_t identity) const {
    for (const auto& foreground : data_.foregrounds)
        if (foreground.identity == identity) return &foreground.layer;
    return nullptr;
}

namespace {
void includeModelPoint(Bounds& bounds,bool& valid,Vec3 point) {
        require(finite(point),"Non-finite posed model position");
        if (!valid) { bounds = {point,point}; valid = true; return; }
        bounds.minimum = {std::min(bounds.minimum.x,point.x),std::min(bounds.minimum.y,point.y),std::min(bounds.minimum.z,point.z)};
        bounds.maximum = {std::max(bounds.maximum.x,point.x),std::max(bounds.maximum.y,point.y),std::max(bounds.maximum.z,point.z)};
}
void compilePartBounds(const Transform& placement,const ModelPart& part,PreparedModelPart& pose) {
    pose.has_render_bounds = false;
    if (!pose.draw || part.geometry.polygons.empty()) return;
    const auto world = compose(placement,pose.current);
    if (pose.vertices.empty()) {
        pose.render_bounds = transformBounds(world,part.local_bounds); pose.has_render_bounds = true;
    } else for (const auto vertex : pose.vertices)
        includeModelPoint(pose.render_bounds,pose.has_render_bounds,transformPoint(world,vertex));
}
void compileModelBounds(const FrameData& frame,const Transform& placement,PreparedModel& model) {
    model.has_render_bounds = false;
    if (model.deferred_pose) {
        if (!model.composed_pose.empty()) {
            const auto transforms = frame.modelTransforms(model.composed_pose);
            for (size_t i = 0; i < model.parts.size(); ++i) {
                const auto& geometry = model.asset->geometry().parts[i];
                if (!model.parts.read()[i].draw || geometry.geometry.polygons.empty()) continue;
                auto local = geometry.local_bounds;
                const float amount = std::abs(model.deformation);
                const Vec3 expansion{std::max(std::abs(local.minimum.x),std::abs(local.maximum.x))*amount,
                    std::max(std::abs(local.minimum.y),std::abs(local.maximum.y))*amount,
                    std::max(std::abs(local.minimum.z),std::abs(local.maximum.z))*amount};
                local.minimum = {local.minimum.x-expansion.x,local.minimum.y-expansion.y,local.minimum.z-expansion.z};
                local.maximum = {local.maximum.x+expansion.x,local.maximum.y+expansion.y,local.maximum.z+expansion.z};
                const auto bounds = transformBounds(compose(placement,transforms[i]),local);
                includeModelPoint(model.render_bounds,model.has_render_bounds,bounds.minimum);
                includeModelPoint(model.render_bounds,model.has_render_bounds,bounds.maximum);
            }
            return;
        }
        const auto* summary = model.parts.publishedFor(model.asset->id()) ? model.parts.summary() : nullptr;
        if (summary && summary->has_rest_bounds && model.pose_inputs.empty() && model.deformation == 0) {
            model.render_bounds = transformBounds(placement,summary->rest_bounds);
            model.has_render_bounds = true;
            return;
        }
        float radius = 0;
        if (summary) {
            radius = summary->motion_radius*(1+std::abs(model.deformation));
            model.has_render_bounds = summary->has_geometry;
        } else for (size_t i = 0; i < model.parts.size(); ++i) {
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
        compilePartBounds(placement,model.asset->geometry().parts[index],pose);
        if (pose.has_render_bounds) {
            includeModelPoint(model.render_bounds,model.has_render_bounds,pose.render_bounds.minimum);
            includeModelPoint(model.render_bounds,model.has_render_bounds,pose.render_bounds.maximum);
        }
    }
}
}
const ResolvedModel& Presentation::resolvedModel(size_t instance,size_t variant) const {
    const auto& object = data_.model_instances.at(instance);
    const auto& source = data_.variants(object).at(variant);
    const auto& ambient = data_.ambientFor(object);
    auto& cached = storage_->resolved_model_indices_.at(source.resolved_index);
    if (cached != UINT32_MAX) return storage_->resolved_models_[cached];
    ResolvedModel resolved;
    resolved.asset = source.asset; resolved.modulation = source.modulation;
    resolved.parts = {source.parts.data(),source.parts.size()};
    resolved.render_bounds = source.render_bounds; resolved.has_render_bounds = source.has_render_bounds;
    resolved.modulation.ambient = resolveModelAmbient(ambient,object.current.position,source.modulation.ambient);
    if (source.deferred_pose) {
        const auto* animation = source.asset->geometry().animation.get();
        const auto composed = data_.modelTransforms(source.composed_pose);
        const auto previous_composed = data_.modelTransforms(source.previous_composed_pose);
        const auto input = [&](PoseInputRange range) { return data_.poseInputs(range).data(); };
        const auto& rest = source.asset->geometry().rest_pose;
        const bool current_rest = source.pose_inputs.empty() && !rest.empty();
        const bool previous_rest = source.previous_pose_inputs.empty() && !rest.empty();
        ModelTransforms current,previous;
        if (composed.empty() && !current_rest) current = ComposeModelPose(*animation,EvaluateModelPose(*animation,input(source.pose_inputs),float(data_.simulation_time)));
        if (source.previous_pose_valid && previous_composed.empty() && !previous_rest)
            previous = ComposeModelPose(*animation,EvaluateModelPose(*animation,input(source.previous_pose_inputs),float(source.previous_pose_time)));
        const auto value = [](const ModelSubmodelTransform& t) {
            const auto v = [](const vector& p) { return Vec3{p.x,p.y,p.z}; };
            return Transform{v(t.translation),v(t.orientation.rvec),v(t.orientation.uvec),v(t.orientation.fvec)};
        };
        auto& poses = storage_->resolved_parts_;
        const auto begin = storage_->resolved_part_count_;
        try {
            const auto end = begin+source.parts.size();
            if (poses.size() < end) poses.resize(end);
            std::copy(source.parts.begin(),source.parts.end(),poses.begin()+begin);
            resolved.has_render_bounds = false;
            uint32_t surface_base = source.surface_base;
            for (size_t i = 0; i < source.parts.size(); ++i) {
                auto& part = poses[begin+i];
                part.compiled_index = source.part_base+uint32_t(i);
                part.surface_base = surface_base; surface_base += uint32_t(part.surfaces.size());
                part.has_previous = source.previous_pose_valid && (source.previous_draw_mask & (uint32_t(1)<<i));
                part.light_influences_prepared = source.light_influences_prepared;
                part.light_candidates = source.light_candidates;
                part.current = !composed.empty() ? composed[i] : current_rest ? rest[i] : value(current.submodels[i]);
                part.previous = !source.previous_pose_valid ? part.current : !previous_composed.empty() ? previous_composed[i] :
                    previous_rest ? rest[i] : value(previous.submodels[i]);
                const auto& geometry = source.asset->geometry().parts[i].geometry;
                deformMeshInto(geometry,source.deformation,data_.simulation_time,source.deformation_owner,uint32_t(i),part.vertices);
                if (source.previous_pose_valid)
                    deformMeshInto(geometry,source.previous_deformation,source.previous_pose_time,source.deformation_owner,uint32_t(i),part.previous_vertices);
                else part.previous_vertices = part.vertices;
                compilePartBounds(object.current,source.asset->geometry().parts[i],part);
                if (part.has_render_bounds) {
                    includeModelPoint(resolved.render_bounds,resolved.has_render_bounds,part.render_bounds.minimum);
                    includeModelPoint(resolved.render_bounds,resolved.has_render_bounds,part.render_bounds.maximum);
                }
            }
            resolved.parts = {source.parts.empty() ? nullptr : poses.data()+begin,source.parts.size()};
            storage_->resolved_part_count_ = end;
        } catch (...) {
            for (size_t i = begin; i < poses.size(); ++i) {
                poses[i].surfaces.clear();
                poses[i].vertices.clear(); poses[i].previous_vertices.clear();
            }
            throw;
        }
    }
    // Publication reserves the maximum number of resolvable variants. Appending
    // demanded values therefore never moves an earlier view's returned model.
    storage_->resolved_models_.push_back(std::move(resolved));
    cached = uint32_t(storage_->resolved_models_.size()-1);
    return storage_->resolved_models_.back();
}
SceneCollection Presentation::collection(uint64_t identity) const {
    const auto* found = storage_->collection_indices_.find(identity);
    if (!found) return {};
    const auto& range = storage_->collections_[found->second];
    return {{range.model_count ? storage_->collection_models_.data()+range.model_begin : nullptr,range.model_count},
        {range.mesh_count ? storage_->collection_meshes_.data()+range.mesh_begin : nullptr,range.mesh_count},range.has_receivers};
}
struct FrameStoragePool {
    std::array<FrameData,2> spare;
    size_t count = 0;
    const std::thread::id publisher = std::this_thread::get_id();
};
FrameData acquireFrameData() {
    static thread_local auto pool = std::make_shared<FrameStoragePool>();
    FrameData data;
    if (pool->count) data = std::move(pool->spare[--pool->count]);
    data.storage_pool_ = pool;
    return data;
}
Presentation::~Presentation() {
    data_.compilation_storage_ = std::move(storage_);
}
FrameData::~FrameData() {
    // The snapshot owns its pool reference, so teardown is safe even if its
    // publishing thread has exited. Foreign-thread releases free normally;
    // only the publisher accesses spare storage. Spares never own the pool.
    const auto pool = std::move(storage_pool_);
    if (!pool || pool->publisher != std::this_thread::get_id() || pool->count == pool->spare.size()) return;
    FrameData clean;
    const auto reclaim = [](auto& source,auto& destination) {
        source.clear(); destination.swap(source);
    };
    reclaim(rooms,clean.rooms);
    reclaim(objects,clean.objects);
    reclaim(model_instances,clean.model_instances);
    reclaim(model_variants,clean.model_variants);
    reclaim(model_pose_inputs,clean.model_pose_inputs);
    reclaim(model_transforms,clean.model_transforms);
    reclaim(model_ambient_inputs,clean.model_ambient_inputs);
    reclaim(lights,clean.lights);
    reclaim(light_candidate_indices,clean.light_candidate_indices);
    reclaim(images,clean.images);
    reclaim(camera_perturbations,clean.camera_perturbations);
    reclaim(meshes,clean.meshes);
    reclaim(foregrounds,clean.foregrounds);
    reclaim(view_regions,clean.view_regions);
    reclaim(terrain.materials,clean.terrain.materials);
    reclaim(terrain.patches,clean.terrain.patches);
    // Drop frame-owned pose/asset references now; retain only allocation
    // capacity and asset-free identity indexes. Each ordered inventory is
    // checked exactly before its index can be reused by a subsequent frame.
    if (compilation_storage_ && compilation_storage_.unique()) {
        compilation_storage_->resolved_models_.clear();
        compilation_storage_->releasePoses();
        clean.compilation_storage_ = std::move(compilation_storage_);
    }
    pool->spare[pool->count++] = std::move(clean);
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
    const auto index = storage_->image_slots_[imageSlot(data_.images,storage_->image_slots_,binding)];
    if (!index)
        throw std::invalid_argument("Missing frame image binding");
    return *data_.images[index-1].asset;
}
PresentationRef Presentation::publish(FrameData&& data) {
    return complete(std::move(data));
}
PresentationRef Presentation::complete(FrameData data) {
    auto storage = std::move(data.compilation_storage_);
    if (!storage || !storage.unique()) storage = std::make_shared<FrameCompilationStorage>();
    storage->clear();
    require(data.sequence != 0 && data.scene_lifetime != 0, "Unidentified presentation frame");
    require(data.temporal_predecessor < data.sequence,"Invalid presentation predecessor");
    require(std::isfinite(data.simulation_time) && std::isfinite(data.delta_time) && data.delta_time >= 0,
        "Invalid presentation clock");
    require(finite(data.player_control_delta) && std::isfinite(data.afterburner_factor) &&
        data.afterburner_factor >= 0 && data.afterburner_factor <= 1,"Invalid presentation motion inputs");
    require((data.pending_domains & ~AllPendingDomains) == 0, "Unknown pending scene domain");
    // These are publication checks, not lookup tables. Accumulate contiguous
    // keys and check adjacent sorted values instead of allocating tree/hash
    // nodes for every emitter and overlay on every frame. No borrowed state
    // survives the call; an exception is cleared by the next publication.
    static thread_local std::vector<uint64_t> foreground_ids;
    static thread_local std::vector<std::pair<ForegroundPhase,int32_t>> foreground_order;
    static thread_local std::vector<std::tuple<LightSource,uint64_t,uint32_t>> emitters;
    static thread_local std::vector<uint32_t> camera_owners;
    foreground_ids.clear(); foreground_order.clear(); emitters.clear(); camera_owners.clear();
    const auto unique = [](auto& keys,const char* message) {
        std::sort(keys.begin(),keys.end());
        require(std::adjacent_find(keys.begin(),keys.end()) == keys.end(),message);
    };
    for (const auto& foreground : data.foregrounds) {
        require(foreground.identity,"Invalid or duplicate foreground identity");
        foreground_ids.push_back(foreground.identity);
        require(foreground.phase == ForegroundPhase::AfterMainWorld ||
            foreground.phase == ForegroundPhase::AfterWorldViews ||
            foreground.phase == ForegroundPhase::WorldOverlay || foreground.phase == ForegroundPhase::PostHUD ||
            foreground.phase == ForegroundPhase::FinalOverlay,
            "Unknown foreground phase");
        foreground_order.emplace_back(foreground.phase,foreground.order);
        validateForeground(foreground.layer);
        for (const auto& view : foreground.layer.scene_views) validateViewImageInputs(data,view);
        for (const auto& view : foreground.layer.display_views) validateViewImageInputs(data,view);
    }
    unique(foreground_ids,"Invalid or duplicate foreground identity");
    unique(foreground_order,"Ambiguous foreground order");
    require(data.lighting_quality == LightingQuality::Vertex || data.lighting_quality == LightingQuality::Pixel,
        "Unknown lighting quality");
    for (const auto& effect : data.camera_perturbations) {
        camera_owners.push_back(effect.owner);
        require(finite(effect.rotation) && effect.rotation.position.x == 0 &&
            effect.rotation.position.y == 0 && effect.rotation.position.z == 0,"Invalid camera perturbation");
    }
    unique(camera_owners,"Duplicate camera perturbation owner");
    for (const auto& light : data.lights) {
        emitters.emplace_back(light.id.source,light.id.owner,light.id.channel);
        require(finite(light.position) && finite(light.color) && finite(light.direction) &&
            std::isfinite(light.specular_strength) && light.specular_strength >= 0 &&
            std::isfinite(light.radius) && light.radius > 0 && std::isfinite(light.falloff) && light.falloff > 0 &&
            std::isfinite(light.cone) && light.cone >= -1 && light.cone < 1, "Invalid scene light");
        const auto& n = light.direction;
        require(!light.directional || std::abs(n.x*n.x+n.y*n.y+n.z*n.z-1) < .001f, "Scene light direction is not normalized");
    }
    unique(emitters,"Duplicate scene emitter identity");
    for (const auto& image : data.images)
        require(image.binding && image.asset && image.asset->id().lifetime == data.scene_lifetime,"Stale image asset in frame");
    // The compiled hash index validates uniqueness and serves every lookup;
    // canonicalizing producer order would sort the same inventory every frame.
    require(data.images.size() < UINT32_MAX/2,"Too many frame images");
    size_t image_capacity = 1;
    while (image_capacity < data.images.size()*2) image_capacity *= 2;
    auto& image_slots = storage->image_slots_; image_slots.assign(image_capacity,0);
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
    // Frame bindings are validated against the union of retained requirements,
    // not repeatedly against every instance/part that shares those bindings.
    // Only immutable requirement lists are retained; actual image ownership and
    // formats are checked anew for this frame, including held/replayed frames.
    const auto dependency_order = [](const RoomSurfaces::ImageDependency& a,const RoomSurfaces::ImageDependency& b) {
        return std::make_tuple(a.binding,a.classify_alpha,a.format) < std::make_tuple(b.binding,b.classify_alpha,b.format);
    };
    static thread_local RetainedDependencies<RoomSurfaces::ImageDependency,decltype(dependency_order)> image_requirements(dependency_order);
    static thread_local uint64_t requirement_lifetime = 0;
    if (requirement_lifetime != data.scene_lifetime) {
        image_requirements.clear(); requirement_lifetime = data.scene_lifetime;
    }
    image_requirements.begin();
    for (auto index : data.light_candidate_indices)
        require(index < data.lights.size(),"Candidate table references missing emitter");
    for (float value : data.model_pose_inputs)
        require(std::isfinite(value) && value >= 0 && value <= 1,"Invalid animation input");
    data.has_reflection_fields = false;
    const auto validateIndices = [&](const SurfaceBinding& surface) {
        data.has_reflection_fields = data.has_reflection_fields || (surface.reflection && surface.specular_strength > 0);
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
    const auto validateViewStyle = [&](const MeshViewStyle& style) {
        require(style.match == CameraMatch::Any || style.match == CameraMatch::Only || style.match == CameraMatch::Except,
            "Invalid primitive camera selection");
        require(style.camera_kinds == UINT32_MAX || !(style.camera_kinds & ~((1u<<6)-1)),"Invalid primitive camera-kind mask");
        const float length = style.directional_opacity.x*style.directional_opacity.x+
            style.directional_opacity.y*style.directional_opacity.y+style.directional_opacity.z*style.directional_opacity.z;
        require(finite(style.directional_opacity) && (length == 0 || std::abs(length-1) < .001f),
            "Invalid primitive view opacity axis");
    };
    for (const auto& mesh : data.meshes) {
        require(mesh.raster_domain == RasterDomain::Surface || mesh.raster_domain == RasterDomain::SoftVolume,
            "Invalid prepared mesh raster domain");
        require(mesh.asset && mesh.asset->id().lifetime == data.scene_lifetime,"Stale generic mesh");
        validateViewStyle(mesh.view_style);
        validateViewStyle(mesh.primary_view_style);
        require(!mesh.alternate_collection || mesh.alternate_collection != mesh.collection,"Duplicate mesh collection audience");
        require(!mesh.alternate_collection || mesh.receiver_polygon < 0,"Surface receivers cannot be isolated into an alternate audience");
        data.lightCandidates(mesh.light_candidates);
        require(mesh.vertices.empty() || mesh.vertices.size() == mesh.asset->geometry().vertices.size(),
            "Incomplete prepared mesh positions");
        for (const auto& vertex : mesh.vertices) require(finite(vertex),"Invalid prepared mesh position");
        require(mesh.texture_uvs.empty() || mesh.texture_uvs.size() == mesh.asset->geometry().vertices.size(),
            "Incomplete prepared mesh texture coordinates");
        for (const auto uv : mesh.texture_uvs) require(std::isfinite(uv.x) && std::isfinite(uv.y),"Invalid prepared texture coordinate");
        require(std::isfinite(mesh.raster_stroke_width) && mesh.raster_stroke_width >= 0,"Invalid stroke width");
        if (mesh.raster_stroke_width > 0) {
            const auto policy = materialPolicy(mesh.surface,mesh.asset->hasVertexAlpha());
            require(!mesh.asset->geometry().raster_offsets.empty() && mesh.alignment == MeshAlignment::World &&
                !mesh.model_instance && mesh.raster_domain == RasterDomain::Surface &&
                mesh.eye_distance == 0 && mesh.size_fade_range.y == 0 &&
                mesh.surface.unlit && policy.blended() && !policy.writesDepth(),
                "Raster strokes require unlit non-depth-writing world endpoints");
        }
        require(mesh.asset->geometry().raster_offsets.empty() || mesh.surface.layer == RenderLayer::Background || mesh.raster_stroke_width > 0,
            "Raster markers require a background layer");
        require(finite(mesh.world) && finite(mesh.camera_translation),"Invalid generic mesh transform");
        require(!mesh.has_previous_world || finite(mesh.previous_world),"Invalid previous mesh placement");
        const auto& trail = mesh.raster_trail;
        require(std::isfinite(trail.minimum_delta) && trail.minimum_delta >= 0 &&
            std::isfinite(trail.length) && trail.length >= 0 && trail.length <= 1 &&
            std::isfinite(trail.minimum_alpha) && trail.minimum_alpha >= 0 && trail.minimum_alpha <= 1 &&
            std::isfinite(trail.alpha_falloff) && trail.alpha_falloff >= 0 &&
            std::isfinite(trail.reference_length) && trail.reference_length > 0,"Invalid raster trail");
        require(trail.length == 0 || (!mesh.asset->geometry().raster_offsets.empty() &&
            mesh.raster_stroke_width == 0 && mesh.surface.unlit && !mesh.surface.medium_transport &&
            mesh.alignment == MeshAlignment::World && !mesh.model_instance),"Raster trail requires unlit retained raster markers");
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
            mesh.alignment == MeshAlignment::Axis || mesh.alignment == MeshAlignment::Velocity,"Invalid mesh alignment");
        const auto& motion = mesh.billboard_motion;
        require(finite(motion.velocity) && std::isfinite(motion.bank) &&
            std::isfinite(motion.speed_stretch) && motion.speed_stretch >= 0 &&
            std::isfinite(motion.maximum_stretch) && motion.maximum_stretch >= 0 &&
            std::isfinite(motion.proximity_range) && motion.proximity_range >= 0,"Invalid motion billboard input");
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
        for (const auto& patch : data.terrain.patches) data.lightCandidates(patch.light_candidates);
    } else require(data.terrain.materials.empty() && data.terrain.patches.empty(),"Terrain materials have no grid");
    storage->room_indices_.assign(data.rooms,[](const RoomState& room) { return room.number; });
    const auto& rooms = storage->room_indices_.lookup();
    data.has_planar_surfaces = false;
    for (size_t i=0;i<data.view_regions.size();++i) {
        const auto& region=data.view_regions[i];
        require(region.identity && region.rectangle.width > 0 && region.rectangle.height > 0,"Invalid view region");
        for (size_t j=0;j<i;++j) require(data.view_regions[j].identity != region.identity,"Duplicate view region identity");
    }
    for (const auto& room : data.rooms) {
        data.has_planar_surfaces = data.has_planar_surfaces || !room.mirrors.empty();
        require(room.number >= 0,"Invalid scene room identity");
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
            image_requirements.add(room.surfaces.imageDependencies());
            const auto& reflections = room.surfaces.reflectionDependencies();
            data.has_reflection_fields = data.has_reflection_fields || (reflections && !reflections->empty());
        }
        data.lightCandidates(room.light_candidates);
        require(finite(room.fog_color) && std::isfinite(room.fog_depth), "Invalid room fog");
        require(std::isfinite(room.light_scalar) && room.light_scalar >= 0, "Invalid room illumination");
        // A sealed portal table owns the exact surface table against which
        // its polygon references were checked. Editing either detaches it.
        if (!room.portals.publishedFor(room.surfaces))
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
    storage->object_indices_.assign(data.objects,[](const ObjectState& object) { return object.handle; });
    const auto& objects = storage->object_indices_.lookup();
    const auto ownerIndex = [&](bool has_owner,uint32_t handle,size_t hint) {
        if (!has_owner) return size_t(-1);
        // Producers already walking the owned object inventory may supply its
        // ordinal. It is only a hint: reordering and allocation-generation
        // changes must still resolve against this presentation's identities.
        if (hint < data.objects.size() && data.objects[hint].handle == handle) return hint;
        const auto* owner = objects.find(handle);
        require(owner != nullptr,"Render owner is not part of this presentation");
        return owner->second;
    };
    for (const auto& object : data.objects) {
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
    storage->model_indices_.assign(data.model_instances,[](const ModelInstance& model) { return model.identity; });
    const auto& model_instances = storage->model_indices_.lookup();
    for (const auto& object : data.objects)
        for (const auto hidden : object.camera_hidden_objects)
            require(objects.count(hidden) != 0,"Camera rig references a missing object allocation");
    for (const auto& ambient : data.model_ambient_inputs) {
        require(ambient.source == ModelAmbientInput::Source::Constant ||
            ambient.source == ModelAmbientInput::Source::RoomVolume || ambient.source == ModelAmbientInput::Source::Terrain,
            "Invalid ambient source");
        require(finite(ambient.previous_position) && std::isfinite(ambient.previous_weight) &&
            std::isfinite(ambient.minimum) && ambient.minimum >= 0 && std::isfinite(ambient.multiplier) &&
            std::isfinite(ambient.volume_scalar) && ambient.volume_scalar >= 0 &&
            std::isfinite(ambient.previous_scalar) && ambient.previous_scalar >= 0,
            "Invalid ambient inputs");
        for (const auto* volume : {ambient.volume.get(),ambient.previous_volume.get()})
            require(!volume || volume->id().lifetime == data.scene_lifetime,"Stale ambient volume");
        require(ambient.source != ModelAmbientInput::Source::Terrain || ambient.terrain,"Missing ambient terrain");
        require(!ambient.terrain || ambient.terrain->id().lifetime == data.scene_lifetime,"Stale ambient terrain");
    }
    for (const auto& transform : data.model_transforms)
        require(finite(transform),"Invalid composed model transform");
    size_t model_end = 0;
    for (auto& object : data.model_instances) {
        require(std::isfinite(object.deformation_response.vertices) && object.deformation_response.vertices >= 0 &&
            std::isfinite(object.deformation_response.translation) && object.deformation_response.translation >= 0,
            "Invalid model deformation response");
        require(object.identity,"Absent model instance identity");
        object.compiled_owner = ownerIndex(object.has_owner,object.owner,object.compiled_owner);
        validateViewStyle(object.view_style);
        validateViewStyle(object.primary_view_style);
        require(!object.alternate_collection || object.alternate_collection != object.collection,"Duplicate model collection audience");
        if (object.view_style.match != CameraMatch::Any || object.view_style.directional_opacity.x != 0 ||
            object.view_style.directional_opacity.y != 0 || object.view_style.directional_opacity.z != 0)
            ownerIndex(true,object.view_style.camera_owner,size_t(-1));
        if (object.primary_view_style.match != CameraMatch::Any || object.primary_view_style.directional_opacity.x != 0 ||
            object.primary_view_style.directional_opacity.y != 0 || object.primary_view_style.directional_opacity.z != 0)
            ownerIndex(true,object.primary_view_style.camera_owner,size_t(-1));
        require(finite(object.current) && finite(object.previous),"Invalid model placement");
        data.ambientFor(object);
        require(object.model_count && object.model_begin == model_end,"Model variants must partition frame storage");
        const auto variants = data.variants(object);
        model_end += variants.size();
        require(std::isfinite(object.lod_distances.x) && std::isfinite(object.lod_distances.y) &&
            object.lod_distances.x >= 0 && object.lod_distances.y >= 0,"Invalid model LOD distances");
        for (const auto index : object.lod_models)
            require(index < variants.size(),"LOD references an absent model variant");
        for (const auto& model : variants) {
            require(model.asset && model.asset->id().lifetime == data.scene_lifetime, "Stale model asset in frame");
            require(finite(model.modulation.ambient) && finite(model.modulation.color) &&
                std::isfinite(model.modulation.alpha) && model.modulation.alpha >= 0 && model.modulation.alpha <= 1,
                "Invalid model instance modulation");
            require(model.deferred_pose || (model.composed_pose.empty() && model.previous_composed_pose.empty()),
                "Composed transforms require deferred execution");
            if (model.deferred_pose) {
                require(model.parts.size() <= MAX_SUBOBJECTS,"Too many deferred model parts");
                require(!model.composed_pose.empty() || model.asset->geometry().animation != nullptr,"Deferred pose has no owned animation");
                require(model.composed_pose.empty() == model.previous_composed_pose.empty() ||
                    (!model.composed_pose.empty() && !model.previous_pose_valid),"Incomplete composed pose history");
                for (const auto range : {model.composed_pose,model.previous_composed_pose}) {
                    const auto transforms = data.modelTransforms(range);
                    require(transforms.empty() || transforms.size() == model.parts.size(),"Incomplete composed pose");
                }
                require(model.composed_pose.empty() || (model.pose_inputs.empty() && model.previous_pose_inputs.empty()),
                    "Ambiguous model pose inputs");
                require(std::isfinite(model.deformation) && std::isfinite(model.previous_deformation) &&
                    std::isfinite(model.previous_pose_time),"Invalid deferred pose inputs");
                for (const auto range : {model.pose_inputs,model.previous_pose_inputs}) {
                    const auto input = data.poseInputs(range);
                    require(input.empty() || input.size() == model.asset->geometry().parts.size(),"Incomplete animation inputs");
                }
            }
            require(model.parts.size() == model.asset->geometry().parts.size(), "Incomplete object pose");
            data.lightCandidates(model.light_candidates);
            const bool retained_parts = model.parts.publishedFor(model.asset->id());
            if (retained_parts) {
                const auto& summary = *model.parts.summary();
                require(summary.required_light_count <= data.lights.size(),"Model references missing emitter");
                require(summary.required_candidate_count <= data.light_candidate_indices.size(),"Model references missing candidate range");
                image_requirements.add(summary.images);
                data.has_reflection_fields = data.has_reflection_fields || (summary.reflections && !summary.reflections->empty());
                continue;
            }
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
                data.lightCandidates(part.light_candidates);
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
                    image_requirements.add(part.surfaces.imageDependencies());
                }
            }
        }
    }
    require(model_end == data.model_variants.size(),"Unowned model variants in frame storage");
    image_requirements.finish();
    for (const auto& dependency : image_requirements.values()) validateDependency(dependency);
    for (auto& mesh : data.meshes) {
        require(std::isfinite(mesh.deformation_response.vertices) && mesh.deformation_response.vertices >= 0 &&
            std::isfinite(mesh.deformation_response.translation) && mesh.deformation_response.translation >= 0,
            "Invalid mesh deformation response");
        mesh.compiled_owner = ownerIndex(mesh.has_owner,mesh.owner,mesh.compiled_owner);
        if (mesh.view_style.match != CameraMatch::Any || mesh.view_style.directional_opacity.x != 0 ||
            mesh.view_style.directional_opacity.y != 0 || mesh.view_style.directional_opacity.z != 0)
            ownerIndex(true,mesh.view_style.camera_owner,size_t(-1));
        if (mesh.primary_view_style.match != CameraMatch::Any || mesh.primary_view_style.directional_opacity.x != 0 ||
            mesh.primary_view_style.directional_opacity.y != 0 || mesh.primary_view_style.directional_opacity.z != 0)
            ownerIndex(true,mesh.primary_view_style.camera_owner,size_t(-1));
        const auto* attachment = mesh.model_instance ? model_instances.find(mesh.model_instance) : nullptr;
        mesh.compiled_model = attachment ? attachment->second : size_t(-1);
        require(mesh.model_variant >= -1 && (mesh.model_instance == 0 ? mesh.model_variant == -1 :
            (attachment && mesh.model_variant >= 0 &&
                size_t(mesh.model_variant) < data.model_instances[mesh.compiled_model].model_count &&
                (mesh.collection == data.model_instances[mesh.compiled_model].collection ||
                    (data.model_instances[mesh.compiled_model].alternate_collection &&
                        mesh.collection == data.model_instances[mesh.compiled_model].alternate_collection)) &&
                (!mesh.alternate_collection || mesh.alternate_collection == data.model_instances[mesh.compiled_model].collection ||
                    mesh.alternate_collection == data.model_instances[mesh.compiled_model].alternate_collection))),
            "Model attachment references an absent instance variant");
        require(mesh.model_part >= -1 && (mesh.model_part < 0 || (mesh.model_instance &&
            size_t(mesh.model_part) < data.modelsFor(mesh.compiled_model)[mesh.model_variant].parts.size())),
            "Model attachment references an absent pose part");
    }
    std::shared_ptr<Presentation> prepared(new Presentation(std::move(data),std::move(storage)));
    // Compile domain membership once. Cockpit/HUD/module views must not walk
    // every world object and effect merely to discard their collection IDs.
    require(prepared->data_.model_instances.size() <= UINT32_MAX/2 && prepared->data_.meshes.size() <= UINT32_MAX/2,
        "Too many scene collection members");
    auto& membership = *prepared->storage_;
    bool same_collections = membership.collections_valid_ &&
        membership.collection_model_keys_.size() == prepared->data_.model_instances.size() &&
        membership.collection_mesh_keys_.size() == prepared->data_.meshes.size();
    for (size_t i = 0; same_collections && i < prepared->data_.model_instances.size(); ++i)
        same_collections = membership.collection_model_keys_[i] == std::make_pair(prepared->data_.model_instances[i].collection,
            prepared->data_.model_instances[i].alternate_collection);
    for (size_t i = 0; same_collections && i < prepared->data_.meshes.size(); ++i) {
        const auto& mesh = prepared->data_.meshes[i];
        same_collections = membership.collection_mesh_keys_[i] == std::make_tuple(mesh.collection,mesh.alternate_collection,mesh.receiver_polygon >= 0);
    }
    if (!same_collections) {
    membership.collections_valid_ = false;
    membership.collection_indices_.clear(); membership.collections_.clear();
    membership.collection_model_keys_.clear(); membership.collection_mesh_keys_.clear();
    membership.collection_model_keys_.reserve(prepared->data_.model_instances.size());
    membership.collection_mesh_keys_.reserve(prepared->data_.meshes.size());
    const auto domain = [&](uint64_t identity) -> FrameCompilationStorage::CollectionRange& {
        const auto found = prepared->storage_->collection_indices_.emplace(identity,prepared->storage_->collections_.size());
        if (found.second) prepared->storage_->collections_.emplace_back();
        return prepared->storage_->collections_[found.first->second];
    };
    for (const auto& model : prepared->data_.model_instances) {
        ++domain(model.collection).model_count;
        if (model.alternate_collection) ++domain(model.alternate_collection).model_count;
    }
    for (const auto& mesh : prepared->data_.meshes) {
        auto& range = domain(mesh.collection); ++range.mesh_count;
        range.has_receivers = range.has_receivers || mesh.receiver_polygon >= 0;
        if (mesh.alternate_collection) ++domain(mesh.alternate_collection).mesh_count;
    }
    uint32_t model_begin = 0,mesh_begin = 0;
    for (auto& range : prepared->storage_->collections_) {
        range.model_begin = model_begin; model_begin += range.model_count; range.model_count = 0;
        range.mesh_begin = mesh_begin; mesh_begin += range.mesh_count; range.mesh_count = 0;
    }
    prepared->storage_->collection_models_.resize(model_begin); prepared->storage_->collection_meshes_.resize(mesh_begin);
    for (uint32_t i = 0; i < prepared->data_.model_instances.size(); ++i) {
        const auto& model = prepared->data_.model_instances[i];
        auto& range = domain(model.collection);
        prepared->storage_->collection_models_[range.model_begin+range.model_count++] = i;
        if (model.alternate_collection) {
            auto& alternate = domain(model.alternate_collection);
            prepared->storage_->collection_models_[alternate.model_begin+alternate.model_count++] = i;
        }
        membership.collection_model_keys_.emplace_back(model.collection,model.alternate_collection);
    }
    for (uint32_t i = 0; i < prepared->data_.meshes.size(); ++i) {
        auto& range = domain(prepared->data_.meshes[i].collection);
        prepared->storage_->collection_meshes_[range.mesh_begin+range.mesh_count++] = i;
        const auto& mesh = prepared->data_.meshes[i];
        if (mesh.alternate_collection) {
            auto& alternate = domain(mesh.alternate_collection);
            prepared->storage_->collection_meshes_[alternate.mesh_begin+alternate.mesh_count++] = i;
        }
        membership.collection_mesh_keys_.emplace_back(mesh.collection,mesh.alternate_collection,mesh.receiver_polygon >= 0);
    }
    membership.collections_valid_ = true;
    }
    auto& surface_count = prepared->data_.compiled_surface_count;
    surface_count = 0;
    prepared->data_.compiled_part_count = 0;
    const auto index_surface = [&](SurfaceBinding& surface) { surface.compiled_index = surface_count++; };
    for (auto& room : prepared->data_.rooms) {
        room.surface_base = surface_count;
        surface_count += static_cast<uint32_t>(room.surfaces.size());
    }
    for (size_t i = 0; i < prepared->data_.terrain.patches.size(); ++i) {
        auto& patch = prepared->data_.terrain.patches[i];
        const auto count = prepared->data_.terrain.asset->patches()[i].batches.size();
        require(count <= UINT32_MAX-surface_count,"Too many terrain batches");
        patch.surface_base = surface_count; surface_count += uint32_t(count);
    }
    for (auto& mesh : prepared->data_.meshes) index_surface(mesh.surface);
    std::sort(prepared->data_.foregrounds.begin(),prepared->data_.foregrounds.end(),
        [](const PreparedForeground& a,const PreparedForeground& b) {
            return a.phase != b.phase ? a.phase < b.phase : a.order < b.order;
        });
    for (auto& mesh : prepared->data_.meshes) {
        mesh.surface.material = materialPolicy(mesh.surface,mesh.asset->hasVertexAlpha());
    }
    for (auto& surface : prepared->data_.terrain.materials)
        surface.material = compileMaterialPolicy(surface,MeshPolygon{});
    for (auto& room : prepared->data_.rooms) {
        if (!room.surfaces.publishedForRoom(room.asset->id(),room.mirrors)) {
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
        if (!room.portals.publishedFor(room.surfaces)) {
            for (size_t i = 0; i < room.portals.size(); ++i) {
                const auto& portal = room.portals.read()[i];
                if (!portal.surface_controls_visibility) continue;
                const bool traversable = room.surfaces.read()[portal.polygon].material.allowsBehind();
                if (portal.traversable != traversable) room.portals[i].traversable = traversable;
            }
            room.portals.seal(room.surfaces);
        }
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
        for (auto& model : prepared->data_.variants(object)) {
            // Assign submission addresses in the same ordered walk that
            // compiles bounds and material tables, not another whole-model pass.
            model.surface_base = surface_count;
            model.part_base = prepared->data_.compiled_part_count;
            require(model.parts.size() <= UINT32_MAX-model.part_base,"Too many scene model parts");
            prepared->data_.compiled_part_count += uint32_t(model.parts.size());
            if (model.deferred_pose && model.parts.publishedFor(model.asset->id())) {
                require(model.parts.summary()->surface_count <= UINT32_MAX-surface_count,"Too many scene surfaces");
                surface_count += uint32_t(model.parts.summary()->surface_count);
            } else for (size_t p = 0; p < model.parts.size(); ++p) {
                if (!model.deferred_pose) {
                    model.parts[p].surface_base = surface_count;
                    model.parts[p].compiled_index = model.part_base+uint32_t(p);
                }
                surface_count += uint32_t(model.parts.read()[p].surfaces.size());
            }
            model.resolved_index = resolved_index++;
            compileModelBounds(prepared->data_,object.current,model);
            if (model.has_render_bounds) {
                include(object.render_bounds,object.has_render_bounds,model.render_bounds.minimum);
                include(object.render_bounds,object.has_render_bounds,model.render_bounds.maximum);
            }
            if (model.parts.publishedFor(model.asset->id())) continue;
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
            model.parts.seal(*model.asset);
        }
    }
    prepared->storage_->resolved_models_.reserve(resolved_index);
    prepared->storage_->resolved_parts_.reserve(prepared->data_.compiled_part_count);
    prepared->storage_->resolved_model_indices_.assign(resolved_index,UINT32_MAX);
    return prepared;
}
void Presentation::requireExecutable() const {
    if (data_.pending_domains != 0)
        throw std::logic_error("Renderer migration incomplete: presentation contains unprepared domains");
}
void validateViewImageInputs(const FrameData& frame,const View& view) {
    for (const auto& input : view.image_inputs) if (input.producer)
        require(input.producer->data().scene_lifetime == frame.scene_lifetime &&
            input.producer->data().sequence < frame.sequence,
            "External image requires an earlier publication in the same scene lifetime");
}
void validateView(const View& view) {
    require(view.motion_composition == View::MotionComposition::Preserve || view.motion_composition == View::MotionComposition::Stationary,
        "Invalid view motion composition");
    require(view.motion_composition != View::MotionComposition::Stationary ||
        (view.target == ViewTarget::Scene && (view.kind != ViewKind::Main || view.collection)),
        "Stationary motion requires an independent in-scene layer");
    require(uint32_t(view.kind) <= uint32_t(ViewKind::Preview),"Invalid camera kind");
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
    require(finite(view.camera) && finite(view.camera_velocity) && std::isfinite(view.zoom) && view.zoom > 0, "Invalid view camera");
    require(finite(view.world_deformation.displacement),"Invalid view world deformation");
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
    const auto& coverage = view.coverage;
    require((coverage.x == 0 && coverage.y == 0 && coverage.width == 0 && coverage.height == 0) ||
        (view.feedback == NoFeedback && view.kind != ViewKind::Main &&
         coverage.x >= 0 && coverage.y >= 0 && coverage.width > 0 && coverage.height > 0 &&
         int64_t(coverage.x)+coverage.width <= view.viewport.width && int64_t(coverage.y)+coverage.height <= view.viewport.height),
        "Invalid view coverage extent or feedback ownership");
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
