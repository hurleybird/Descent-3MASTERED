// Descent 3MASTERED - GPL-3.0-or-later
#pragma once

// The scene boundary deliberately has no game, OpenGL, g3, or renderer-state
// dependencies. Owned values cross it; pointers into the simulation do not.
#include <cstdint>
#include <memory>
#include <vector>

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
};
struct MeshGeometry {
    std::vector<Vec3> vertices;
    std::vector<Vec3> vertex_normals; // empty means authored flat polygon normals
    std::vector<MeshPolygon> polygons;
};
bool operator==(const MeshGeometry& a, const MeshGeometry& b);

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
private:
    friend class SceneAssets;
    RoomAsset(AssetId id, MeshGeometry geometry, VolumeGrid volume);
    AssetId id_;
    MeshGeometry geometry_;
    VolumeGrid volume_;
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
    uint64_t lifetime() const { return lifetime_; }
private:
    uint64_t lifetime_ = 0;
    uint64_t revision_ = 0;
    std::vector<RoomAssetRef> rooms_;
    std::vector<ModelAssetRef> models_;
    std::vector<ImageAssetRef> images_;
};

// These are authoring bindings, not alternate material implementations. The
// material compiler must resolve them before execution is enabled.
struct SurfaceBinding {
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
    bool draw_surface = true, additive = false, texture_alpha = false;
    int32_t texture = -1, lightmap = -1, special = -1, portal = -1;
    uint32_t authored_flags = 0;
    uint8_t light_multiple = 0;
};
struct Portal {
    int32_t polygon = -1, destination_room = -1, destination_portal = -1;
    int32_t combine_master = -1;
    uint32_t flags = 0;
    bool traversable = true;
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
};

struct PreparedModelPart {
    // Model-space transforms, composed once from the complete hierarchy.
    Transform current, previous;
    bool draw = true;
    std::vector<Vec3> vertices, previous_vertices; // empty means immutable authored positions
    std::vector<SurfaceBinding> surfaces;
};
// Object identity includes the game's allocation generation, not just its slot.
// Preparation will replace source classifications with asset/pose/material
// records. Unprepared objects MUST NOT be sent to a legacy draw fallback.
struct ObjectState {
    uint32_t handle = 0;
    int32_t room = -1;
    uint8_t type = 0, render_type = 0, lighting_type = 0;
    uint16_t authored_id = 0;
    Transform current, previous;
    bool has_previous = false;
    float radius = 0;
    ModelAssetRef model;
    std::vector<PreparedModelPart> parts;
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
    bool directional = false, headlight = false;
};

enum class PendingDomain : uint32_t {
    Materials = 1u << 0, ObjectPreparation = 1u << 1,
    Terrain = 1u << 2, Effects = 1u << 3, Lights = 1u << 4,
    Decals = 1u << 5, Cockpit = 1u << 6
};
constexpr uint32_t AllPendingDomains = (1u << 7) - 1;

struct FrameData {
    uint64_t sequence = 0, scene_lifetime = 0;
    double simulation_time = 0;
    float delta_time = 0;
    bool paused = false;
    uint32_t pending_domains = AllPendingDomains;
    std::vector<RoomState> rooms;
    std::vector<ObjectState> objects;
    std::vector<Light> lights;
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
struct Rect { int32_t x = 0, y = 0, width = 0, height = 0; };
enum Feedback : uint8_t { NoFeedback = 0, Automap = 1, ObjectVisibility = 2, EffectEligibility = 4 };
struct View {
    // Explicit diagnostic execution, never a fallback selected by capability.
    bool geometry_inspection = false;
    ViewKind kind = ViewKind::Main;
    uint64_t history_key = 0;
    Transform camera;
    int32_t room = -1;
    uint32_t excluded_object = 0;
    bool exclude_object = false;
    Rect viewport, projection_anchor;
    float zoom = 1;
    bool dynamic_lighting = true;
    bool portal_visibility = true;
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

} // namespace d3scene
