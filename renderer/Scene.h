// Descent 3MASTERED - GPL-3.0-or-later
#pragma once
#include "SceneLookup.h"

// The scene boundary deliberately has no game, OpenGL, g3, or renderer-state
// dependencies. Owned values cross it; pointers into the simulation do not.
#include <cstdint>
#include <memory>
#include <vector>
#include <utility>
#include <array>
#include <unordered_map>
#include <stdexcept>

struct ModelAnimation;
namespace d3scene {

struct Vec2 { float x = 0, y = 0; };
struct Vec3 { float x = 0, y = 0, z = 0; };
struct Plane { Vec3 normal; float constant = 0; };
struct Transform {
    Vec3 position;
    Vec3 right{1, 0, 0}, up{0, 1, 0}, forward{0, 0, 1};
};
struct Bounds { Vec3 minimum, maximum; };
Vec3 transformPoint(const Transform&, Vec3);
Vec3 transformDirection(const Transform&, Vec3);
Transform compose(const Transform& parent, const Transform& local);
Bounds transformBounds(const Transform&,const Bounds&);

struct AssetId {
    uint64_t lifetime = 0;
    uint64_t revision = 0;
    uint32_t slot = 0;
};
bool operator==(AssetId a, AssetId b);
bool operator!=(AssetId a, AssetId b);

// Authored polygon topology is retained, not recovered from camera-clipped
// triangles. Triangulation and GPU buffers belong to the renderer.
struct MeshCorner {
    uint32_t vertex = 0;
    float u = 0, v = 0, light_u = 0, light_v = 0;
    uint8_t alpha = 255;
};
struct MeshPolygon {
    Vec3 normal;
    std::vector<MeshCorner> corners;
    bool bounds_volume = true; // false for floating trigger sheets, not room walls
};
struct MeshGeometry {
    std::vector<Vec3> vertices;
    std::vector<Vec3> vertex_normals; // empty means authored flat polygon normals
    std::vector<Vec3> vertex_colors; // empty means white; explicit authored Gouraud color
    std::vector<Vec2> raster_offsets; // optional raster-pixel offsets for batched background markers
    std::vector<MeshPolygon> polygons;
};
bool operator==(const MeshGeometry& a, const MeshGeometry& b);
class MeshAsset final {
public:
    AssetId id() const { return id_; }
    const MeshGeometry& geometry() const { return geometry_; }
private:
    friend class SceneAssets;
    MeshAsset(AssetId id,MeshGeometry geometry) : id_(id), geometry_(std::move(geometry)) {}
    AssetId id_;
    MeshGeometry geometry_;
};
using MeshAssetRef = std::shared_ptr<const MeshAsset>;

struct VolumeGrid {
    Vec3 origin;
    uint32_t width = 0, height = 0, depth = 0;
    float spacing = 10;
    std::vector<uint8_t> colors;
};
bool operator==(const VolumeGrid&,const VolumeGrid&);

class RoomAsset final {
public:
    AssetId id() const { return id_; }
    const MeshGeometry& geometry() const { return geometry_; }
    const VolumeGrid& volume() const { return volume_; }
    const Bounds& bounds() const { return bounds_; }
private:
    friend class SceneAssets;
    RoomAsset(AssetId id, MeshGeometry geometry, VolumeGrid volume);
    AssetId id_;
    MeshGeometry geometry_;
    VolumeGrid volume_;
    Bounds bounds_;
};
using RoomAssetRef = std::shared_ptr<const RoomAsset>;

struct ModelPart {
    MeshGeometry geometry;
    std::vector<int32_t> polygon_materials;
    std::vector<Vec3> polygon_colors;
    int32_t parent = -1;
    Vec3 offset;
    uint32_t authored_flags = 0;
    // Compiled once with the model. Equivalent face appearances share one
    // instance record; geometry and per-face lighting remain independent.
    std::vector<uint32_t> surface_for_polygon, surface_polygons;
    Bounds local_bounds;
    float motion_radius = 0; // conservative root-space radius across all animation tracks
    bool hierarchy_hidden = false; // authored traversal exclusion, compiled with the asset
};
struct ModelGeometry {
    std::shared_ptr<const ModelAnimation> animation;
    std::vector<ModelPart> parts;
    std::vector<int32_t> textures;
    // Compiled local-to-model transforms for an unanimated rest pose. Empty
    // means evaluation is required (e.g. an autonomous rotating submodel).
    std::vector<Transform> rest_pose;
    bool pose_uses_clock = false;
};
bool operator==(const ModelGeometry&, const ModelGeometry&);
class ModelAsset final {
public:
    AssetId id() const { return id_; }
    const ModelGeometry& geometry() const { return geometry_; }
private:
    friend class SceneAssets;
    ModelAsset(AssetId id, ModelGeometry geometry);
    AssetId id_;
    ModelGeometry geometry_;
};
using ModelAssetRef = std::shared_ptr<const ModelAsset>;

enum class ImageFormat : uint8_t { ARGB1555, ARGB4444, RGBA8 };
struct ImageMip {
    uint32_t width = 0, height = 0;
    std::vector<uint8_t> pixels;
};
struct ImageSource;
class ImageAsset final {
public:
    AssetId id() const { return id_; }
    ImageFormat format() const { return format_; }
    const std::vector<ImageMip>& mips() const { return mips_; }
    uint32_t generatedLevels() const { return generated_levels_; }
    uint32_t paddedExtent() const { return padded_extent_; }
private:
    friend class SceneAssets;
    ImageAsset(AssetId,const ImageSource&);
    AssetId id_;
    ImageFormat format_;
    std::vector<ImageMip> mips_;
    uint32_t generated_levels_ = 0;
    uint32_t padded_extent_ = 0;
};
using ImageAssetRef = std::shared_ptr<const ImageAsset>;
// Authored reflection anchors are baked appearance data, not dynamic emitters.
// Positions and optional corner normals are world-space for room surfaces.
struct ReflectionSource { Vec3 position, color; };
// Baked reflected lobes are not point lights: each virtual center encodes a
// reflected direction at its corner. Never reflect it a second time.
struct ReflectionCorner {
    std::array<ReflectionSource,4> lobes{};
    Vec3 baked_modulation{1,1,1};
};
struct ReflectionData {
    std::vector<ReflectionSource> sources;
    std::vector<Vec3> corner_normals;
    std::vector<ReflectionCorner> field_corners;
};
class ReflectionAsset final {
public:
    AssetId id() const { return id_; }
    const ReflectionData& data() const { return data_; }
private:
    friend class SceneAssets;
    ReflectionAsset(AssetId id,ReflectionData data) : id_(id),data_(std::move(data)) {}
    AssetId id_;
    ReflectionData data_;
};
using ReflectionAssetRef = std::shared_ptr<const ReflectionAsset>;
struct TerrainGrid;
class TerrainAsset;
using TerrainAssetRef = std::shared_ptr<const TerrainAsset>;
struct TerrainOcclusionData;
class TerrainOcclusionAsset;
using TerrainOcclusionRef = std::shared_ptr<const TerrainOcclusionAsset>;
class FogAsset;
struct RoomState;
using FogAssetRef = std::shared_ptr<const FogAsset>;

// Immutable revisions keep already-published frames alive across level unload,
// slot reuse, and geometry changes. Reset never invalidates an existing frame.
class SceneAssets final {
public:
    SceneAssets();
    void reset();
    RoomAssetRef room(uint32_t slot) const;
    RoomAssetRef publishRoom(uint32_t slot, MeshGeometry geometry, VolumeGrid volume = {});
    void removeRoom(uint32_t slot);
    ModelAssetRef model(uint32_t slot) const;
    ModelAssetRef publishModel(uint32_t slot, ModelGeometry geometry);
    void removeModel(uint32_t slot);
    ImageAssetRef image(uint32_t slot) const;
    ImageAssetRef image(uint32_t slot,uint64_t content_revision) const;
    ImageAssetRef publishImage(uint32_t slot,const ImageSource&);
    ReflectionAssetRef publishReflection(uint32_t slot,ReflectionData);
    TerrainAssetRef terrain() const { return terrain_; }
    TerrainAssetRef publishTerrain(const TerrainGrid&);
    TerrainOcclusionRef terrainOcclusion() const { return terrain_occlusion_; }
    TerrainOcclusionRef publishTerrainOcclusion(const TerrainOcclusionData&);
    MeshAssetRef publishMesh(uint32_t,MeshGeometry);
    // Canonical topology belongs to the scene lifetime, not to each sprite or
    // text run. Slots 2 and 258..384 are reserved for these primitives.
    MeshAssetRef quadMesh(uint32_t count = 1);
    FogAssetRef publishFog(const std::vector<RoomState>&);
    uint64_t lifetime() const { return lifetime_; }
private:
    uint64_t lifetime_ = 0;
    uint64_t revision_ = 0;
    std::vector<RoomAssetRef> rooms_;
    std::vector<ModelAssetRef> models_;
    std::vector<ImageAssetRef> images_;
    std::vector<uint64_t> image_source_revisions_;
    // Reflection identities span authored faces, compiled fields and shared
    // environment sources. Storage must scale with live assets, not max ID.
    std::unordered_map<uint32_t,ReflectionAssetRef> reflections_;
    TerrainAssetRef terrain_;
    TerrainOcclusionRef terrain_occlusion_;
    std::vector<MeshAssetRef> meshes_;
    std::array<MeshAssetRef,128> quad_meshes_{};
    FogAssetRef fog_;
};

enum class RenderLayer : uint8_t { Background, World };
enum class BlendMode : uint8_t { Solid, Alpha, Additive, Multiply };
enum class DepthMode : uint8_t { Automatic, ReadWrite, ReadOnly, Disabled };
enum class TextureAlpha : uint8_t { Ignore, Cutout, Continuous };
enum class OcclusionClass : uint8_t { Default, Terrain, Model, MineRock, Mine };
struct MaterialPolicy {
    uint64_t planar_slot = 0; // compiled layered reflection; alpha belongs to its surface film
    RenderLayer layer = RenderLayer::World;
    BlendMode blend = BlendMode::Solid;
    DepthMode depth = DepthMode::ReadWrite; // fully resolved; never Automatic
    TextureAlpha texture_alpha = TextureAlpha::Ignore;
    float alpha_cutoff = .01f;
    bool blended() const { return blend != BlendMode::Solid; }
    bool testsDepth() const { return depth != DepthMode::Disabled; }
    bool writesDepth() const { return depth == DepthMode::ReadWrite; }
    bool writesSolidDepth() const { return writesDepth() && !blended(); }
    bool requiresLatePass() const { return !writesSolidDepth(); }
    bool allowsBehind() const { return !planar_slot && (blended() || texture_alpha != TextureAlpha::Ignore); }
};
// These are authoring bindings, not alternate material implementations. The
// material compiler must resolve them before execution is enabled.
struct SurfaceBinding {
    // Assigned at publication; stable within this immutable presentation.
    uint32_t compiled_index = 0;
    MaterialPolicy material; // compiled by publication, shared by all consumers
    OcclusionClass occlusion_class = OcclusionClass::Default;
    ReflectionAssetRef reflection;
    // Authored specular response, independent of draw ordering and mutable
    // game texture tables. Zero strength disables this lobe.
    float specular_strength = 0, specular_exponent = 6;
    float specular_lightmap_mix = 0, specular_alpha_strength = 1;
    float static_specular_strength = 1;
    int specular_bitmap = -1;
    uint32_t specular_image = 0; // stable resource binding; zero means no image
    int32_t color_texture = -1, specular_texture = -1; // authored image-sequence sources
    Vec2 scroll_period; // seconds per UV repeat; zero disables that axis
    float authored_alpha = 1; // pre-texture alpha, retained for format-policy changes
    RenderLayer layer = RenderLayer::World;
    bool unlit = false, two_sided = false;
    BlendMode blend = BlendMode::Solid; // non-solid modes explicitly request compositing
    DepthMode depth = DepthMode::Automatic;
    int32_t composition_order = 0; // only consulted by explicit compositing views
    // Optional repeating horizontal coverage in raster pixels: period, phase,
    // covered fraction. Zero period disables it; no application/widget identity.
    Vec3 raster_stripe;
    bool texture_wrap = true;
    float soft_depth_range = 0; // zero disables intersection fading; positive requires opaque depth
    // Authored translucent effect depth in eye units, not a geometry/fog shift.
    // Excluded from opaque depth and temporal surface products.
    float effect_depth_offset = 0;
    std::vector<uint32_t> light_indices;
    std::vector<Vec2> baked_uvs; // optional per-instance corner UVs, never shared model scratch
    Vec3 ambient_color{1,1,1}, ambient_direction{0,-1,0};
    bool directional_ambient = false;
    float lighting_modulation = 1;
    Vec3 base_color{1, 1, 1};
    Vec3 color_modulation{1,1,1};
    int32_t bitmap = -1, lightmap_bitmap = -1;
    uint32_t color_image = 0, baked_image = 0;
    ImageFormat color_format = ImageFormat::ARGB1555;
    ImageFormat specular_format = ImageFormat::ARGB1555;
    uint64_t view_image_slot = 0; // view-local input slot, never a global bitmap/target
    bool lightmap_wrap = false;
    float lightmap_u_scale = 1, lightmap_v_scale = 1;
    float u_offset = 0, v_offset = 0, alpha = 1;
    Vec2 texture_scale{1,1};
    bool draw_surface = true, texture_alpha = false;
    int32_t texture = -1, lightmap = -1, special = -1, portal = -1;
    uint32_t authored_flags = 0;
    uint8_t light_multiple = 0;
};
MaterialPolicy compileMaterialPolicy(const SurfaceBinding&, const MeshPolygon&);
// Instance radiance/opacity never changes immutable material identity.
struct SurfaceModulation { Vec3 ambient{1,1,1}, color{1,1,1}; float alpha = 1; };
SurfaceModulation modulatedSurface(const SurfaceBinding&,const SurfaceModulation&);
MaterialPolicy modulatedMaterialPolicy(const SurfaceBinding&,float alpha);
struct TerrainState {
    TerrainAssetRef asset;
    TerrainOcclusionRef occlusion; // null means authored table is unavailable/invalidated
    float observation_distance = 0; // world units; zero means not published
    std::vector<SurfaceBinding> materials;
    std::vector<std::vector<SurfaceBinding>> patches;
};
enum class MeshAlignment : uint8_t { World, Camera, FacingEye, CameraUpright, Axis };
struct PreparedMesh {
    MeshAssetRef asset;
    uint64_t collection = 0; // zero is the world; other collections are isolated view content
    std::vector<Vec3> vertices; // owned frame-local positions; empty uses authored geometry
    std::vector<Vec2> texture_uvs; // optional per-vertex base UVs; split vertices at seams
    SurfaceBinding surface;
    Transform world;
    Vec3 camera_translation; // sky follows the eye with authored half-height parallax
    float eye_distance = 0; // positive projects the world anchor onto a view-centered shell
    Vec2 size_fade_range; // eye-depth start/end; zero disables view-local size fade
    // Optional authored detail cutoff, tested against the nearest depth of an
    // explicit local reference volume, not the current polygon/mesh center.
    float maximum_depth = 0;
    Bounds depth_reference;
    MeshAlignment alignment = MeshAlignment::World;
    // Axis alignment preserves world.up (half the segment, toward its start)
    // and uses the length of world.right as radius. Facing is view-local.
    int32_t room = -1;
    int32_t receiver_polygon = -1; // optional room-surface visibility dependency (decals)
    uint32_t owner = 0;
    bool has_owner = false; // same allocation-generation identity as ObjectState
    uint64_t model_instance = 0; // optional dependency on a prepared model, not a game object
    int32_t model_part = -1; // sprite position is local to this part; its basis remains camera-local
    int32_t model_variant = -1;
};
struct Portal {
    int32_t polygon = -1, destination_room = -1, destination_portal = -1;
    int32_t combine_master = -1;
    uint32_t flags = 0;
    bool traversable = true;
    bool two_sided = false; // aperture policy, independent of surface winding
};
class RoomPortals {
    using Storage = std::vector<Portal>;
    std::shared_ptr<Storage> values_;
    bool sealed_ = false;
    Storage& edit() {
        if (!values_) values_ = std::make_shared<Storage>();
        else if (sealed_ || values_.use_count() != 1) values_ = std::make_shared<Storage>(*values_);
        sealed_ = false; return *values_;
    }
    void seal() { sealed_ = true; }
    friend class Presentation;
public:
    RoomPortals() = default;
    RoomPortals(std::initializer_list<Portal> values) : values_(std::make_shared<Storage>(values)) {}
    RoomPortals(const RoomPortals& other) : values_(!other.values_ || other.sealed_ ? other.values_ :
        std::make_shared<Storage>(*other.values_)),sealed_(other.sealed_) {}
    RoomPortals& operator=(const RoomPortals& other) { if (this != &other) { RoomPortals copy(other); *this = std::move(copy); } return *this; }
    RoomPortals(RoomPortals&&) = default;
    RoomPortals& operator=(RoomPortals&&) = default;
    const Storage& read() const { static const Storage empty; return values_ ? *values_ : empty; }
    size_t size() const { return read().size(); }
    bool empty() const { return read().empty(); }
    const Portal& operator[](size_t i) const { return read()[i]; }
    Portal& operator[](size_t i) { return edit()[i]; }
    auto begin() const { return read().begin(); }
    auto end() const { return read().end(); }
    auto begin() { return edit().begin(); }
    auto end() { return edit().end(); }
    void push_back(Portal portal) { edit().push_back(portal); }
};
struct PlanarSurface {
    Plane plane;
    std::vector<uint32_t> polygons;
};
// Published room appearance is shared across frames. Mutable builders are
// deep-copied at publication; editing a published table first detaches it.
// This preserves held frames even when a builder retains element references.
class RoomSurfaces {
public:
    struct ImageDependency { int handle; bool baked; uint32_t binding; ImageFormat format; bool wrap; float u_scale, v_scale; bool classify_alpha = false; int texture = -1; };
private:
    using Storage = std::vector<SurfaceBinding>;
    std::shared_ptr<Storage> values_;
    AssetId publication_;
    uint32_t publication_part_ = UINT32_MAX;
    bool publication_palette_ = false;
    std::shared_ptr<const std::vector<ImageDependency>> images_;
    std::shared_ptr<const std::vector<uint64_t>> reflections_;
    std::shared_ptr<const std::vector<PlanarSurface>> planar_;
    uint64_t required_light_count_ = 0;
    void edit() {
        if (!values_) values_ = std::make_shared<Storage>();
        if (publication_.revision || values_.use_count() != 1) values_ = std::make_shared<Storage>(*values_);
        publication_ = {};
        images_.reset();
        reflections_.reset(); required_light_count_ = 0;
        planar_.reset();
    }
    void seal(AssetId asset,uint32_t part = UINT32_MAX,bool palette = false,const std::vector<PlanarSurface>* planar = nullptr);
    friend class Presentation;
public:
    RoomSurfaces() = default;
    RoomSurfaces(const RoomSurfaces& other) : values_(!other.values_ || other.publication_.revision ? other.values_ :
        std::make_shared<Storage>(*other.values_)), publication_(other.publication_), publication_part_(other.publication_part_),
        publication_palette_(other.publication_palette_), images_(other.images_), reflections_(other.reflections_), planar_(other.planar_), required_light_count_(other.required_light_count_) {}
    RoomSurfaces& operator=(const RoomSurfaces& other) {
        if (this != &other) { RoomSurfaces copy(other); *this = std::move(copy); } return *this;
    }
    RoomSurfaces(RoomSurfaces&&) = default;
    RoomSurfaces& operator=(RoomSurfaces&&) = default;
    bool publishedFor(AssetId asset,uint32_t part = UINT32_MAX,bool palette = false) const {
        return publication_.revision && publication_ == asset && publication_part_ == part && publication_palette_ == palette;
    }
    bool published() const { return publication_.revision != 0; }
    bool publishedForRoom(AssetId asset,const std::vector<PlanarSurface>& planar) const;
    const std::vector<ImageDependency>& images() const { static const std::vector<ImageDependency> empty; return images_ ? *images_ : empty; }
    const std::shared_ptr<const std::vector<ImageDependency>>& imageDependencies() const { return images_; }
    const std::vector<uint64_t>& reflections() const { static const std::vector<uint64_t> empty; return reflections_ ? *reflections_ : empty; }
    uint64_t requiredLightCount() const { return required_light_count_; }
    size_t size() const { return read().size(); }
    bool empty() const { return read().empty(); }
    const Storage& read() const { static const Storage empty; return values_ ? *values_ : empty; }
    const SurfaceBinding& operator[](size_t i) const { return read()[i]; }
    SurfaceBinding& operator[](size_t i) { edit(); return (*values_)[i]; }
    const SurfaceBinding& at(size_t i) const { return read().at(i); }
    SurfaceBinding& at(size_t i) { edit(); return values_->at(i); }
    Storage::const_iterator begin() const { return read().begin(); }
    Storage::const_iterator end() const { return read().end(); }
    Storage::iterator begin() { edit(); return values_->begin(); }
    Storage::iterator end() { edit(); return values_->end(); }
    const SurfaceBinding* data() const { return read().data(); }
    SurfaceBinding* data() { edit(); return values_->data(); }
    void clear() { values_.reset(); images_.reset(); reflections_.reset(); planar_.reset(); required_light_count_ = 0; publication_ = {}; }
    void reserve(size_t n) { edit(); values_->reserve(n); }
    void resize(size_t n) { edit(); values_->resize(n); }
    void push_back(const SurfaceBinding& value) { edit(); values_->push_back(value); }
};
struct RoomState {
    int32_t number = -1;
    uint64_t source_material_revision = 0;
    int32_t source_mirror_face = -1;
    RoomAssetRef asset;
    Bounds bounds;
    RoomSurfaces surfaces;
    uint32_t surface_base = 0;
    bool uses_room_illumination = false;
    bool light_influences_prepared = false;
    std::vector<uint32_t> light_candidates; // conservative room set; refined after visibility
    RoomPortals portals;
    uint32_t authored_flags = 0;
    float fog_depth = 0;
    Vec3 fog_color;
    uint8_t pulse_time = 0, pulse_offset = 0;
    std::vector<PlanarSurface> mirrors;
    float light_scalar = 1;
    bool blocks_portals = false, exterior = false;
    bool observation_unclipped = false; // authored door-room admission
    bool fog_enabled = false;
};
struct FogTriangle { Vec3 a, b, c, inward; };
struct FogNode {
    Bounds bounds;
    uint32_t first = 0, count = 0, escape = 0;
};
struct FogVolume {
    std::vector<int32_t> rooms;
    std::vector<FogTriangle> boundary;
    Vec3 color;
    float depth = 1;
    Bounds bounds;
    std::vector<FogNode> nodes; // depth-first, stackless traversal; no query depth cap
};
struct ExteriorFog {
    bool enabled = false;
    Vec3 color;
    float start = 0, end = 1;
};

struct PreparedModelPart {
    uint32_t compiled_index = 0;
    // Model-space transforms, composed once from the complete hierarchy.
    Transform current, previous;
    bool draw = true;
    bool has_previous = false;
    Bounds render_bounds; // world-space, compiled from this pose during freeze
    bool has_render_bounds = false;
    std::vector<Vec3> vertices, previous_vertices; // empty means immutable authored positions
    RoomSurfaces surfaces;
    bool material_palette = false;
    uint32_t surface_base = 0;
    bool light_influences_prepared = false;
    std::array<float,16> appearance_inputs{};
    std::vector<uint32_t> light_candidates; // conservative part set; refined after visibility
    const SurfaceBinding& surface(const ModelPart& asset,size_t polygon) const {
        return surfaces[material_palette ? asset.surface_for_polygon[polygon] : polygon];
    }
};
// Stable part bindings can outlive a frame. Publication seals the table;
// mutation detaches it, so held presentations never observe a later edit.
class ModelParts {
public:
    struct Summary {
        AssetId asset;
        uint64_t surface_count = 0, required_light_count = 0;
        float motion_radius = 0;
        bool has_geometry = false;
        std::vector<RoomSurfaces::ImageDependency> images;
    };
private:
    using Storage = std::vector<PreparedModelPart>;
    std::shared_ptr<Storage> values_;
    std::shared_ptr<const Summary> publication_;
    Storage& edit() {
        if (!values_) values_ = std::make_shared<Storage>();
        else if (publication_ || values_.use_count() != 1) values_ = std::make_shared<Storage>(*values_);
        publication_ = {}; return *values_;
    }
    void seal(const ModelAsset& asset);
    friend class Presentation;
public:
    ModelParts() = default;
    ModelParts(const ModelParts& other) : values_(!other.values_ || other.publication_ ? other.values_ :
        std::make_shared<Storage>(*other.values_)),publication_(other.publication_) {}
    ModelParts& operator=(const ModelParts& other) { if (this != &other) { ModelParts copy(other); *this = std::move(copy); } return *this; }
    ModelParts(ModelParts&&) = default;
    ModelParts& operator=(ModelParts&&) = default;
    const Storage& read() const { static const Storage empty; return values_ ? *values_ : empty; }
    size_t size() const { return read().size(); }
    bool empty() const { return read().empty(); }
    bool publishedFor(AssetId asset) const { return publication_ && publication_->asset == asset; }
    const Summary* summary() const { return publication_.get(); }
    const PreparedModelPart& operator[](size_t i) const { return read()[i]; }
    PreparedModelPart& operator[](size_t i) { return edit()[i]; }
    auto begin() const { return read().begin(); }
    auto end() const { return read().end(); }
    auto begin() { return edit().begin(); }
    auto end() { return edit().end(); }
    const PreparedModelPart* data() const { return read().data(); }
    void clear() { values_.reset(); publication_ = {}; }
    void reserve(size_t n) { edit().reserve(n); }
    void resize(size_t n) { edit().resize(n); }
    void push_back(PreparedModelPart value) { edit().push_back(std::move(value)); }
};
// Producer inputs for a complete model binding table. Pose, placement and
// instance modulation do not participate in the lifetime of that table.
struct ModelBindingInputs {
    std::array<float,8> appearance{};
    uint64_t baked_layout_revision = 0;
    uint32_t subobject_mask = 0;
    bool material_palette = false, valid = false;
    bool operator==(const ModelBindingInputs& other) const {
        return valid && other.valid && appearance == other.appearance &&
            baked_layout_revision == other.baked_layout_revision &&
            subobject_mask == other.subobject_mask && material_palette == other.material_palette;
    }
};
struct PreparedModel {
    ModelAssetRef asset;
    ModelBindingInputs binding_inputs;
    SurfaceModulation modulation;
    uint64_t pose_source_revision = 0;
    std::shared_ptr<const std::vector<float>> pose_inputs;
    std::shared_ptr<const std::vector<float>> previous_pose_inputs;
    bool deferred_pose = false, previous_pose_valid = false;
    double previous_pose_time = 0;
    float deformation = 0, previous_deformation = 0;
    uint32_t deformation_owner = 0, resolved_index = 0, surface_base = 0, part_base = 0;
    uint32_t previous_draw_mask = UINT32_MAX;
    bool light_influences_prepared = false;
    std::vector<uint32_t> light_candidates;
    ModelParts parts;
    Bounds render_bounds;
    bool has_render_bounds = false;
};
// Game observation/camera identity includes allocation generation, not just its
// slot. Render geometry is independently published in ModelInstance/PreparedMesh.
struct ObjectState {
    uint32_t handle = 0;
    bool visible = true; // presentation eligibility, independent of a camera query
    int32_t room = -1;
    uint8_t type = 0, render_type = 0, lighting_type = 0;
    uint16_t authored_id = 0;
    Transform current, previous;
    bool has_previous = false;
    float radius = 0;
    // Simulation-observation volume, deliberately independent of posed model
    // bounds, LOD, triangle visibility and renderer pass membership.
    Bounds observation_bounds;
    float observation_radius = 0;
    bool observation_eligible = false, observation_unbounded = false;
    bool observation_front_to_back = false;
    std::vector<uint32_t> camera_hidden_objects; // additional allocation-generation identities in this camera rig
};
// A renderable model is not a simulation object. Ownership is optional (e.g.
// cockpit/preview), while identity and pose belong to this presentation domain.
struct ModelInstance {
    uint64_t identity = 0; // stable nonzero presentation identity; unique within a scene
    uint64_t collection = 0;
    uint32_t owner = 0;
    bool has_owner = false, visible = true;
    int32_t room = -1;
    Transform current, previous;
    bool has_previous = false;
    // All eligible variants exist before any camera query. LOD selection is
    // view-local; prepared attachments identify their owning variant as well.
    uint32_t model_begin = 0, model_count = 0;
    Vec2 lod_distances;
    std::array<uint32_t,3> lod_models{{0,0,0}};
    Bounds render_bounds; // union of every variant, never the simulation radius
    bool has_render_bounds = false;
};

enum class LightSource : uint8_t { Object, Effect };
struct LightId {
    LightSource source = LightSource::Object;
    uint64_t owner = 0;
    uint32_t channel = 0;
};
struct Light {
    LightId id;
    int32_t room = -1;
    Vec3 position, color, direction{0,0,1};
    float radius = 0, falloff = 1, cone = 0;
    float specular_strength = 1;
    bool directional = false, headlight = false;
};

enum class PendingDomain : uint32_t {
    Materials = 1u << 0, ObjectPreparation = 1u << 1,
    Terrain = 1u << 2, Effects = 1u << 3, Lights = 1u << 4,
    Decals = 1u << 5, Cockpit = 1u << 6
};
constexpr uint32_t AllPendingDomains = (1u << 7) - 1;

enum class LightingQuality : uint8_t { Vertex, Pixel };
struct CameraPerturbation {
    uint32_t owner = 0;
    Transform rotation; // world-space rotation only; never moves simulation geometry
};
class Presentation;
using PresentationRef = std::shared_ptr<const Presentation>;

enum class ViewKind { Main, Auxiliary, Mirror, SaveThumbnail, Automap, Preview };
enum class ViewTarget { Scene, Display };
enum class ViewOrdering { Depth, Compositing };
enum class ViewProjection { Perspective, Orthographic };
enum class FogPathOrigin { Camera, ClipPlane };
enum class ReflectionPolicy { Images, SurfaceFilm };
struct Rect { int32_t x = 0, y = 0, width = 0, height = 0; };
enum Feedback : uint8_t { NoFeedback = 0, Automap = 1, ObjectVisibility = 2, EffectEligibility = 4 };
struct ViewImageInput { uint64_t slot = 0, image = 0; };
inline bool operator==(const ViewImageInput& a,const ViewImageInput& b) { return a.slot == b.slot && a.image == b.image; }
// A room number alone can silently select replacement topology. Selections are
// qualified by the immutable asset revision and validated against the frame.
struct RoomSurfaceId { AssetId room; uint32_t polygon = 0; };
inline bool operator==(const RoomSurfaceId& a,const RoomSurfaceId& b) {
    return a.room == b.room && a.polygon == b.polygon;
}
struct View {
    // Explicit diagnostic execution, never a fallback selected by capability.
    bool geometry_inspection = false;
    ViewKind kind = ViewKind::Main;
    ViewTarget target = ViewTarget::Scene;
    ViewOrdering ordering = ViewOrdering::Depth;
    ViewProjection projection = ViewProjection::Perspective;
    Vec2 orthographic_size{1,1}, orthographic_depth{0,1};
    bool clear_depth = false; // scoped to this view's raster viewport
    uint64_t collection = 0; // selects content, independently of camera/history/target
    uint64_t history_key = 0;
    Transform camera;
    bool has_clip_plane = false;
    Plane clip_plane; // world space; retain normal.point + constant >= 0
    FogPathOrigin fog_path_origin = FogPathOrigin::Camera;
    ReflectionPolicy reflections = ReflectionPolicy::Images;
    uint32_t camera_owner = 0;
    uint32_t camera_variant = 0; // discontinuous source mode, e.g. forward/rear
    bool attached_camera = false;
    int32_t room = -1;
    bool exclude_camera_rig = false;
    std::vector<uint32_t> excluded_objects;
    std::vector<RoomSurfaceId> excluded_surfaces;
    std::vector<ViewImageInput> image_inputs;
    Rect viewport, projection_anchor;
    float zoom = 1;
    bool dynamic_lighting = true;
    bool portal_visibility = true;
    bool exterior_origin = false; // explicit exterior root, not an unknown room fallback
    uint8_t feedback = NoFeedback;
    bool camera_cut = false;
};
void validateView(const View& view);
struct ViewImageRequest {
    uint64_t image = 0;
    View view;
    uint32_t scale = 1, samples = 0;
};
void validateViewImage(const ViewImageRequest&);
// Produces an independently owned color/depth image. It cannot publish the
// main presentation's AO/motion/protection products or gameplay observations.
// All declared dependencies are checked and scheduled before executing any
// producer. Inputs cannot silently reuse a previous presentation's image.
void renderViewImages(const Presentation&,const std::vector<ViewImageRequest>&);

// A result of visibility, not a side effect of drawing a depth/color pass.
// Commit policy remains game-owned; replay does not mutate the simulation.
struct VisibilityReport {
    uint64_t frame_sequence = 0, scene_lifetime = 0, view_history_key = 0;
    uint8_t feedback = NoFeedback;
    std::vector<int32_t> rooms;
    std::vector<uint32_t> objects;
    bool terrain = false;
};

// Renderer-owned scene publication; separate from context-dependent GL assets.
// The application is single-threaded today. Concurrent extraction will require
// an explicit publisher, not unsynchronized access to this store.
SceneAssets& assets();
VisibilityReport renderView(const Presentation& frame, const View& view);
// Ordered independent foreground views share a freshly cleared scene-resolution
// image/depth buffer, then resolve once over the completed world. Display views
// follow that resolve (e.g. an authored late canopy or native-resolution UI).
struct ForegroundLayer {
    std::vector<View> scene_views;
    std::vector<View> display_views;
};
void renderForeground(const Presentation& frame, const ForegroundLayer& layer);

// Game layout publishes camera, placement and ordering alongside geometry.
// Execution selects a named layer; it never consults live window/HUD state.
enum class ForegroundPhase { WorldOverlay, FinalOverlay };
struct PreparedForeground {
    uint64_t identity = 0;
    ForegroundLayer layer;
    int32_t order = 0;
    ForegroundPhase phase = ForegroundPhase::WorldOverlay;
};
template<class T> struct ModelRange {
    T* first = nullptr;
    size_t count = 0;
    T* begin() const { return first; }
    T* end() const { return count ? first+count : first; }
    T* data() const { return first; }
    size_t size() const { return count; }
    bool empty() const { return count == 0; }
    T& operator[](size_t index) const { return first[index]; }
    T& at(size_t index) const { if (index >= count) throw std::out_of_range("Model variant"); return first[index]; }
};
struct FrameData {
    uint32_t compiled_surface_count = 0, compiled_part_count = 0;
    uint64_t sequence = 0, scene_lifetime = 0;
    double simulation_time = 0;
    float delta_time = 0;
    bool paused = false;
    bool observation_inputs_prepared = false;
    Vec3 player_control_delta; // realized player-controlled translation, not camera motion
    float afterburner_factor = 0;
    LightingQuality lighting_quality = LightingQuality::Pixel;
    uint32_t pending_domains = AllPendingDomains;
    std::vector<RoomState> rooms;
    std::vector<ObjectState> objects;
    std::vector<ModelInstance> model_instances;
    std::vector<PreparedModel> model_variants;
    ModelRange<PreparedModel> variants(const ModelInstance& instance) {
        if (instance.model_begin > model_variants.size() || instance.model_count > model_variants.size()-instance.model_begin)
            throw std::invalid_argument("Invalid model variant range");
        return {instance.model_count ? model_variants.data()+instance.model_begin : nullptr,instance.model_count};
    }
    ModelRange<const PreparedModel> variants(const ModelInstance& instance) const {
        if (instance.model_begin > model_variants.size() || instance.model_count > model_variants.size()-instance.model_begin)
            throw std::invalid_argument("Invalid model variant range");
        return {instance.model_count ? model_variants.data()+instance.model_begin : nullptr,instance.model_count};
    }
    ModelRange<PreparedModel> modelsFor(size_t instance) { return variants(model_instances.at(instance)); }
    ModelRange<const PreparedModel> modelsFor(size_t instance) const { return variants(model_instances.at(instance)); }
    void appendModel(ModelInstance& instance,PreparedModel model) {
        if (model_variants.size() >= UINT32_MAX) throw std::invalid_argument("Too many model variants");
        if (!instance.model_count) instance.model_begin = uint32_t(model_variants.size());
        if (uint64_t(instance.model_begin)+instance.model_count != model_variants.size())
            throw std::invalid_argument("Model variants must be appended contiguously");
        model_variants.push_back(std::move(model)); ++instance.model_count;
    }
    // After filtering instances, compact their ranges without reallocating or
    // keeping orphaned variants. Instance ordering remains the storage order.
    void compactModels() {
        size_t write = 0;
        for (auto& instance : model_instances) {
            const auto range = variants(instance);
            if (instance.model_begin < write) throw std::invalid_argument("Overlapping model ranges");
            for (size_t i = 0; i < range.size(); ++i)
                if (write+i != instance.model_begin+i) model_variants[write+i] = std::move(range[i]);
            instance.model_begin = uint32_t(write); write += range.size();
        }
        model_variants.resize(write);
    }
    std::vector<Light> lights;
    // Per-frame immutable pixel revisions. Surface tables name stable slots,
    // never own a pixel revision that would invalidate their material storage.
    struct ImageBinding { uint32_t binding; ImageAssetRef asset; };
    std::vector<ImageBinding> images;
    std::vector<CameraPerturbation> camera_perturbations;
    TerrainState terrain;
    std::vector<PreparedMesh> meshes;
    std::vector<PreparedForeground> foregrounds;
    bool sky_enabled = false;
    Vec3 sky_color;
    FogAssetRef fog;
    ExteriorFog exterior_fog;
};

struct SceneIndexSpan {
    const uint32_t* first = nullptr;
    size_t count = 0;
    const uint32_t* begin() const { return first; }
    const uint32_t* end() const { return count ? first+count : first; }
};
struct SceneCollection {
    SceneIndexSpan models,meshes;
    bool has_receivers = false;
};
class Presentation final {
public:
    // Copies at the publication boundary so a caller retaining a mutable
    // reference cannot change an already-published frame.
    static PresentationRef freeze(const FrameData& data);
    // Consumes an exclusively-owned builder. The caller relinquishes the data
    // and all mutable element references; use freeze when aliases may remain.
    static PresentationRef publish(FrameData&& data);
    const FrameData& data() const { return data_; }
    const ImageAsset& image(uint32_t binding) const;
    // Pure derived pose, memoized for this immutable presentation. Requests
    // from auxiliary views never read game state or depend on the main view.
    const PreparedModel& resolvedModel(size_t instance,size_t variant) const;
    const ForegroundLayer* foreground(uint64_t identity) const;
    SceneCollection collection(uint64_t identity) const;
    const SceneLookup<int32_t,size_t>& roomIndices() const { return room_indices_; }
    const SceneLookup<uint32_t,size_t>& objectIndices() const { return object_indices_; }
    const SceneLookup<uint64_t,size_t>& modelIndices() const { return model_indices_; }
    void requireExecutable() const;
private:
    explicit Presentation(FrameData&& data) : data_(std::move(data)) {}
    static PresentationRef complete(FrameData data);
    FrameData data_;
    SceneLookup<int32_t,size_t> room_indices_;
    std::vector<uint32_t> image_slots_; // packed frame-owned name -> image index, zero means empty
    mutable std::vector<std::unique_ptr<PreparedModel>> resolved_models_;
    SceneLookup<uint32_t,size_t> object_indices_;
    SceneLookup<uint64_t,size_t> model_indices_;
    struct CollectionRange {
        uint32_t model_begin = 0,model_count = 0,mesh_begin = 0,mesh_count = 0;
        bool has_receivers = false;
    };
    SceneLookup<uint64_t,size_t> collection_indices_;
    std::vector<CollectionRange> collections_;
    std::vector<uint32_t> collection_models_,collection_meshes_;
};

} // namespace d3scene
