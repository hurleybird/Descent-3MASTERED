// Descent 3MASTERED - GPL-3.0-or-later
#pragma once
#include "SceneGeometry.h"
#include "SceneView.h"
namespace d3scene {
struct PositionPair { Vec3 current, previous; };
inline std::vector<PositionPair> modelPositionPairs(const MeshGeometry& mesh,const PreparedModelPart& part) {
    std::vector<PositionPair> pairs;
    if (part.vertices.empty() && part.previous_vertices.empty()) return pairs;
    const MeshView current(mesh,part.vertices), previous(mesh,part.previous_vertices);
    for (const auto& polygon : mesh.polygons) forEachTriangleCorner(polygon.corners.size(),[&](size_t corner) {
        const auto index = polygon.corners[corner].vertex;
        pairs.push_back({current.positions[index],previous.positions[index]});
    });
    return pairs;
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
    const auto& part = object.parts[item.part_index];
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
