// Descent 3MASTERED - GPL-3.0-or-later
#pragma once
#include "Scene.h"
#include <array>

namespace d3scene {

// Logical top-left view rectangle to bottom-left raster coordinates. Target
// allocation owns scale/overscan; canvas state is not an input to execution.
Rect rasterViewport(Rect logical, int32_t raster_height, int32_t scale,
    int32_t offset_x = 0, int32_t offset_y = 0);

// Positive eye Z. Perspective retains legacy depth = 1-near/Z and no far
// plane; orthographic uses a finite linear depth range. The conservative query
// and GPU share this projection. Reverse-Z is not part of this migration.
struct Projection {
    float x_scale = 1, y_scale = 1, x_offset = 0, y_offset = 0;
    float near_distance = 1;
    float orthographic_depth_scale = 0; // zero: perspective; otherwise 2/(far-near)
};
struct ClipPosition { double x, y, z, w; };
struct Camera {
    Transform transform;
    Projection projection;
    std::array<Plane, 6> planes; // left, right, bottom, top, near, optional world clip
};
Camera prepareCamera(const View& view);
// Column-major matrices for downstream image-space consumers. They describe
// the resolved camera, not mutable legacy draw state.
struct ViewMatrices {
    std::array<float,16> projection{}, inverse_view{}, view_projection{};
};
ViewMatrices viewMatrices(const Camera&);
View resolveView(const Presentation&, View);
// Independent reflected camera, with the parent's physical half-space retained.
// Does not reflect/mutate scene vertices, poses, lights or simulation identity.
View reflectedView(const Presentation&,View parent,Plane mirror,uint64_t history_key,int32_t room);
bool reversesWinding(const Transform& camera);
bool excludesObject(const View&,uint32_t handle);
bool excludesSurface(const View&,RoomSurfaceId);
uint64_t viewImageSource(const View&,uint64_t slot);
std::vector<size_t> orderViewImages(const std::vector<ViewImageRequest>&);
struct PlanarViewPlan;
// Single-bounce image schedule from the consumer's visible planar groups.
// Child cameras include the camera rig, exclude their own film and render other
// mirrors as opaque authored films. No simulation or shared material mutation.
PlanarViewPlan planPlanarViews(const Presentation&,const View&,uint32_t samples = 0);
Transform resolveMeshTransform(const PreparedMesh&,const Camera&,const Transform* attachment = nullptr);
ClipPosition project(const Camera& camera, Vec3 world);
Vec3 unproject(const Camera& camera, float ndc_x, float ndc_y, float depth);
bool sphereIntersects(const Camera& camera, Vec3 center, float radius);
struct PortalWindow;
// Conservative axis-aligned bounds rejection against the camera and aperture.
bool boundsOutside(const Camera&,const Bounds&,PortalWindow);

struct VisibleSurface {
    uint32_t room_index = 0, polygon_index = 0;
    float depth = 0;
    bool blended = false;
    bool late = false; // pass membership is independent of color blending
    bool model = false;
    uint32_t model_instance_index = 0, part_index = 0;
    uint32_t model_variant = 0;
    Transform world;
    bool terrain = false;
    uint32_t terrain_patch = 0, terrain_batch = 0;
    bool mesh = false;
    uint32_t mesh_index = 0;
    RenderLayer layer = RenderLayer::World;
};
struct ViewQueryStats {
    uint32_t rooms_considered = 0, surfaces_considered = 0;
    uint32_t surfaces_backfacing = 0, surfaces_outside = 0;
};
struct ViewVisibility {
    ViewOrdering ordering = ViewOrdering::Depth;
    uint64_t frame_sequence = 0, scene_lifetime = 0, history_key = 0;
    Camera camera;
    std::vector<VisibleSurface> surfaces;
    std::vector<uint32_t> object_indices;
    std::vector<uint32_t> room_indices;
    ViewQueryStats stats;
    bool exterior_visible = false;
};
// Executable view preparation owns the exact immutable frame it queried.
// Consumers cannot replace its camera/collection while retaining old results.
// Image scheduling may bind producer images, which do not change visibility.
class PreparedView {
    PresentationRef frame_;
    View view_;
    ViewVisibility visibility_;
    PreparedView(PresentationRef frame,View view,ViewVisibility visibility) :
        frame_(std::move(frame)),view_(std::move(view)),visibility_(std::move(visibility)) {}
    friend PreparedView prepareView(const Presentation&,const View&);
    friend PlanarViewPlan planPlanarViews(const Presentation&,const View&,uint32_t);
public:
    const Presentation& frame() const { return *frame_; }
    const View& view() const { return view_; }
    const ViewVisibility& visibility() const { return visibility_; }
};
PreparedView prepareView(const Presentation&,const View&);
VisibilityReport renderView(const PreparedView&);
struct PlanarViewPlan { PreparedView consumer; std::vector<ViewImageRequest> images; };
enum class ViewPassKind { Background, OpaqueWorld, SnapshotOpaqueDepth, ExportOpaqueScene, LateWorld, EvaluateWorldOcclusion, CompositeSurfaces, DepthSurface };
struct ViewProducts { bool opaque_scene = false, occlusion = false; };
struct ViewPass {
    ViewPassKind kind;
    uint32_t begin = 0, end = 0; // half-open range in this view's visible surfaces
};
const SurfaceBinding& visibleSurfaceBinding(const Presentation&,const VisibleSurface&);
MaterialPolicy visibleSurfacePolicy(const Presentation&,const VisibleSurface&);
uint32_t selectModelVariant(const ModelInstance&,const Camera&);
std::vector<ViewPass> buildViewPasses(const Presentation&,const ViewVisibility&,ViewProducts products = {});

// Pure geometry query over published scene data, frusta and portal apertures.
// Results are view-local indices, never marks written into shared objects.
// Gameplay observations use a separate simulation-volume contract.
ViewVisibility queryView(const Presentation& frame, const View& view);

} // namespace d3scene
