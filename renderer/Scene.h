// Descent 3MASTERED - GPL-3.0-or-later
#pragma once

// The scene boundary deliberately has no game, OpenGL, g3, or renderer-state
// dependencies. Owned values cross it; pointers into the simulation do not.
#include <cstdint>
#include <memory>
#include <vector>
#include <utility>
#include <array>
#include <unordered_map>

namespace d3scene {

struct Vec2 { float x = 0, y = 0; };
struct Vec3 { float x = 0, y = 0, z = 0; };
struct Transform {
    Vec3 position;
    Vec3 right{1, 0, 0}, up{0, 1, 0}, forward{0, 0, 1};
};
struct Bounds { Vec3 minimum, maximum; };
Vec3 transformPoint(const Transform&, Vec3);
Vec3 transformDirection(const Transform&, Vec3);
Transform compose(const Transform& parent, const Transform& local);

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
};
struct ModelGeometry {
    std::vector<ModelPart> parts;
    std::vector<int32_t> textures;
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
    ImageAssetRef publishImage(uint32_t slot,const ImageSource&);
    ReflectionAssetRef publishReflection(uint32_t slot,ReflectionData);
    TerrainAssetRef terrain() const { return terrain_; }
    TerrainAssetRef publishTerrain(const TerrainGrid&);
    MeshAssetRef publishMesh(uint32_t,MeshGeometry);
    FogAssetRef publishFog(const std::vector<RoomState>&);
    uint64_t lifetime() const { return lifetime_; }
private:
    uint64_t lifetime_ = 0;
    uint64_t revision_ = 0;
    std::vector<RoomAssetRef> rooms_;
    std::vector<ModelAssetRef> models_;
    std::vector<ImageAssetRef> images_;
    // Reflection identities span authored faces, compiled fields and shared
    // environment sources. Storage must scale with live assets, not max ID.
    std::unordered_map<uint32_t,ReflectionAssetRef> reflections_;
    TerrainAssetRef terrain_;
    std::vector<MeshAssetRef> meshes_;
    FogAssetRef fog_;
};

enum class RenderLayer : uint8_t { Background, World };
enum class BlendMode : uint8_t { Solid, Alpha, Additive, Multiply };
enum class DepthMode : uint8_t { Automatic, ReadWrite, ReadOnly, Disabled };
enum class TextureAlpha : uint8_t { Ignore, Cutout, Continuous };
enum class OcclusionClass : uint8_t { Default, Terrain, Model, MineRock, Mine };
struct MaterialPolicy {
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
    bool allowsBehind() const { return blended() || texture_alpha != TextureAlpha::Ignore; }
};
// These are authoring bindings, not alternate material implementations. The
// material compiler must resolve them before execution is enabled.
struct SurfaceBinding {
    MaterialPolicy material; // compiled by publication, shared by all consumers
    OcclusionClass occlusion_class = OcclusionClass::Default;
    ReflectionAssetRef reflection;
    // Authored specular response, independent of draw ordering and mutable
    // game texture tables. Zero strength disables this lobe.
    float specular_strength = 0, specular_exponent = 6;
    float specular_lightmap_mix = 0, specular_alpha_strength = 1;
    float static_specular_strength = 1;
    int specular_bitmap = -1;
    ImageAssetRef specular_image;
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
    ImageAssetRef color_image, baked_image;
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
struct TerrainState {
    TerrainAssetRef asset;
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
    MeshAlignment alignment = MeshAlignment::World;
    // Axis alignment preserves world.up (half the segment, toward its start)
    // and uses the length of world.right as radius. Facing is view-local.
    int32_t room = -1;
    int32_t receiver_polygon = -1; // optional room-surface visibility dependency (decals)
    uint32_t owner = 0;
    bool has_owner = false; // same allocation-generation identity as ObjectState
    uint64_t model_instance = 0; // optional dependency on a prepared model, not a game object
    int32_t model_variant = -1;
};
struct Portal {
    int32_t polygon = -1, destination_room = -1, destination_portal = -1;
    int32_t combine_master = -1;
    uint32_t flags = 0;
    bool traversable = true;
    bool two_sided = false; // aperture policy, independent of surface winding
};
struct RoomState {
    int32_t number = -1;
    RoomAssetRef asset;
    Bounds bounds;
    std::vector<SurfaceBinding> surfaces;
    std::vector<Portal> portals;
    uint32_t authored_flags = 0;
    float fog_depth = 0;
    Vec3 fog_color;
    uint8_t pulse_time = 0, pulse_offset = 0;
    int32_t mirror_polygon = -1;
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
    // Model-space transforms, composed once from the complete hierarchy.
    Transform current, previous;
    bool draw = true;
    bool has_previous = false;
    Bounds render_bounds; // world-space, compiled from this pose during freeze
    bool has_render_bounds = false;
    std::vector<Vec3> vertices, previous_vertices; // empty means immutable authored positions
    std::vector<SurfaceBinding> surfaces;
};
struct PreparedModel {
    ModelAssetRef asset;
    std::vector<PreparedModelPart> parts;
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
    std::vector<PreparedModel> models;
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
struct FrameData {
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
    std::vector<Light> lights;
    std::vector<CameraPerturbation> camera_perturbations;
    TerrainState terrain;
    std::vector<PreparedMesh> meshes;
    bool sky_enabled = false;
    Vec3 sky_color;
    FogAssetRef fog;
    ExteriorFog exterior_fog;
};

class Presentation final {
public:
    // Copies at the publication boundary so a caller retaining a mutable
    // reference cannot change an already-published frame.
    static std::shared_ptr<const Presentation> freeze(const FrameData& data);
    const FrameData& data() const { return data_; }
    void requireExecutable() const;
private:
    explicit Presentation(const FrameData& data) : data_(data) {}
    FrameData data_;
};
using PresentationRef = std::shared_ptr<const Presentation>;

enum class ViewKind { Main, Auxiliary, Mirror, SaveThumbnail, Automap, Preview };
enum class ViewTarget { Scene, Display };
enum class ViewOrdering { Depth, Compositing };
enum class ViewProjection { Perspective, Orthographic };
struct Rect { int32_t x = 0, y = 0, width = 0, height = 0; };
enum Feedback : uint8_t { NoFeedback = 0, Automap = 1, ObjectVisibility = 2, EffectEligibility = 4 };
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
    uint32_t camera_owner = 0;
    uint32_t camera_variant = 0; // discontinuous source mode, e.g. forward/rear
    bool attached_camera = false;
    int32_t room = -1;
    bool exclude_camera_rig = false;
    std::vector<uint32_t> excluded_objects;
    Rect viewport, projection_anchor;
    float zoom = 1;
    bool dynamic_lighting = true;
    bool portal_visibility = true;
    bool exterior_origin = false; // explicit exterior root, not an unknown room fallback
    uint8_t feedback = NoFeedback;
    bool camera_cut = false;
};
void validateView(const View& view);

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

} // namespace d3scene
