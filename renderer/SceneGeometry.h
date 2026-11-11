// Descent 3MASTERED - GPL-3.0-or-later
#pragma once
#include "Scene.h"
namespace d3scene {
std::vector<PlanarSurface> compilePlanarSurfaces(const MeshGeometry&,std::vector<uint32_t> polygons);
void validatePlanarSurfaces(const MeshGeometry&,const std::vector<PlanarSurface>&);
Transform axisBillboardPlacement(Vec3 start,Vec3 end,float radius);
struct GlowSlice { float distance = 0, radius = 0; };
// Authored D3 cone profile, emitted tail-to-base without a mutable draw loop.
std::vector<GlowSlice> thrusterSlices(float radius,float length);
// A view into one published frame, never into simulation scratch. All geometry
// consumers use these positions, including conservative visibility and lighting.
struct MeshView {
    const MeshGeometry& topology;
    const std::vector<Vec3>& positions;
    explicit MeshView(const MeshGeometry& mesh) : topology(mesh), positions(mesh.vertices) {}
    MeshView(const MeshGeometry& mesh,const std::vector<Vec3>& prepared)
        : topology(mesh), positions(prepared.empty() ? mesh.vertices : prepared) {}
};
template<class Consumer> void forEachTriangleCorner(size_t corners, Consumer consume) {
    for (size_t triangle = 1; triangle+1 < corners; ++triangle) {
        consume(0); consume(triangle); consume(triangle+1);
    }
}
// Index an already expanded polygon fan without shading its duplicated corners
// again. Every vertex-side stream uses the same forEachTriangleCorner expansion,
// so the reused corner has identical geometry, UV, normal and reflection data.
inline void appendExpandedFanIndices(uint32_t base,uint32_t vertices,std::vector<uint32_t>& indices) {
    if (vertices%3 || uint64_t(base)+vertices>uint64_t(UINT32_MAX)+1)
        throw std::invalid_argument("Invalid expanded polygon range");
    for (uint32_t triangle=0;triangle<vertices/3;++triangle) {
        indices.push_back(base);
        indices.push_back(base+(triangle ? triangle*3-1 : 1));
        indices.push_back(base+triangle*3+2);
    }
}
// Preserves D3's radial jitter shape, but replaces draw-order RNG consumption
// with explicit time/instance/part inputs. No view can advance another's shape.
std::vector<Vec3> deformMesh(const MeshGeometry&,float range,double time,uint32_t owner,uint32_t part);
void deformMeshInto(const MeshGeometry&,float range,double time,uint32_t owner,uint32_t part,std::vector<Vec3>&);
MeshGeometry unitBillboardQuad();
MeshGeometry unitFadingStroke();
MeshGeometry unitQuadBatch(uint32_t count);
void appendPreparedTextureCoordinates(const PreparedMesh&,std::vector<Vec2>&);
MeshGeometry billboardRing(float inner_ratio,uint8_t inner_alpha,uint8_t outer_alpha,uint32_t segments = 32);
}
