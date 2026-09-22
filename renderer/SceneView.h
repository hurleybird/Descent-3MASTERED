// Descent 3MASTERED - GPL-3.0-or-later
#pragma once
#include "Scene.h"
#include <array>

namespace d3scene {

// Positive eye Z, legacy depth = 1 - near/Z. This is the same homogeneous
// projection used by both conservative visibility and the GPU. No far plane;
// changing to reverse-Z is not part of this migration.
struct Projection {
    float x_scale = 1, y_scale = 1, x_offset = 0, y_offset = 0;
    float near_distance = 1;
};
struct ClipPosition { double x, y, z, w; };
struct Plane { Vec3 normal; float constant = 0; };
struct Camera {
    Transform transform;
    Projection projection;
    std::array<Plane, 5> planes; // left, right, bottom, top, near; world space
};
Camera prepareCamera(const View& view);
View resolveView(const Presentation&, View);
ClipPosition project(const Camera& camera, Vec3 world);
bool sphereIntersects(const Camera& camera, Vec3 center, float radius);

struct VisibleSurface {
    uint32_t room_index = 0, polygon_index = 0;
    float depth = 0;
    bool blended = false;
    bool model = false;
    uint32_t object_index = 0, part_index = 0;
    Transform world;
};
struct ViewQueryStats {
    uint32_t rooms_considered = 0, surfaces_considered = 0;
    uint32_t surfaces_backfacing = 0, surfaces_outside = 0;
};
struct ViewVisibility {
    uint64_t frame_sequence = 0, scene_lifetime = 0, history_key = 0;
    Camera camera;
    std::vector<VisibleSurface> surfaces;
    std::vector<uint32_t> object_indices;
    std::vector<uint32_t> room_indices;
    ViewQueryStats stats;
};

// Pure conservative frustum query over the complete published scene. Results
// are view-local indices, never marks written into shared faces/objects.
// This does not claim portal/occlusion visibility or gameplay observations.
// Portal traversal can refine this reference query once independently tested.
ViewVisibility queryView(const Presentation& frame, const View& view);

} // namespace d3scene
