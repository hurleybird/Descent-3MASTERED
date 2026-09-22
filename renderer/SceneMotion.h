// Descent 3MASTERED - GPL-3.0-or-later
#pragma once
#include "SceneGeometry.h"
#include "SceneView.h"
namespace d3scene {
struct PositionPair { Vec3 current, previous; };
inline std::vector<PositionPair> preparedPositionPairs(const MeshGeometry& mesh,
    const std::vector<Vec3>& current_positions,const std::vector<Vec3>& previous_positions) {
    std::vector<PositionPair> pairs;
    if (current_positions.empty() && previous_positions.empty()) return pairs;
    const MeshView current(mesh,current_positions), previous(mesh,previous_positions);
    for (const auto& polygon : mesh.polygons) forEachTriangleCorner(polygon.corners.size(),[&](size_t corner) {
        const auto index = polygon.corners[corner].vertex;
        pairs.push_back({current.positions[index],previous.positions[index]});
    });
    return pairs;
}
inline std::vector<PositionPair> modelPositionPairs(const MeshGeometry& mesh,const PreparedModelPart& part) {
    return preparedPositionPairs(mesh,part.vertices,part.previous_vertices);
}
struct SurfaceMotion {
    Transform previous;
    uint32_t identity = 0;
    bool valid = false;
};
inline SurfaceMotion surfaceMotion(const Presentation& frame,const VisibleSurface& item,bool camera_history) {
    SurfaceMotion result; result.previous = item.world;
    if (!item.model) return result; // static world uses camera reconstruction
    const auto& object = frame.data().objects[item.object_index];
    const auto& part = object.models[item.model_variant].parts[item.part_index];
    result.previous = compose(object.previous,part.previous);
    // Preserve the canvas adapter's zero/static and high-bit blur-policy ABI.
    result.identity = (object.handle+1u)&0x7fffffffu;
    if (!result.identity) result.identity = 1;
    result.valid = camera_history && object.has_previous && part.has_previous;
    return result;
}
struct MotionSample { Vec2 velocity; bool valid = false; };
inline MotionSample projectMotion(ClipPosition current,ClipPosition previous,bool history) {
    if (!history || current.w <= 1.e-6 || previous.w <= 1.e-6) return {};
    return {{float((current.x/current.w-previous.x/previous.w)*.5),
        float((current.y/current.w-previous.y/previous.w)*.5)},true};
}
}
