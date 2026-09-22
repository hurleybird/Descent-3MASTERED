// Descent 3MASTERED - GPL-3.0-or-later
#include "SceneGeometry.h"
#include "SceneLighting.h"
#include <cmath>
#include <stdexcept>
namespace d3scene {
Transform axisBillboardPlacement(Vec3 start,Vec3 end,float radius) {
    if (!std::isfinite(radius) || radius < 0 || !std::isfinite(start.x) || !std::isfinite(start.y) ||
        !std::isfinite(start.z) || !std::isfinite(end.x) || !std::isfinite(end.y) || !std::isfinite(end.z))
        throw std::invalid_argument("Invalid axis billboard placement");
    Transform placement;
    placement.position = {start.x*.5f+end.x*.5f,start.y*.5f+end.y*.5f,start.z*.5f+end.z*.5f};
    placement.up = {start.x*.5f-end.x*.5f,start.y*.5f-end.y*.5f,start.z*.5f-end.z*.5f};
    placement.right = {radius,0,0};
    return placement;
}
MeshGeometry unitBillboardQuad() {
    MeshGeometry mesh;
    mesh.vertices = {{-1,1,0},{1,1,0},{1,-1,0},{-1,-1,0}};
    MeshPolygon face; face.normal = {0,0,-1};
    face.corners = {{0,0,0},{1,1,0},{2,1,1},{3,0,1}};
    mesh.polygons.push_back(std::move(face));
    return mesh;
}
MeshGeometry billboardRing(float inner_ratio,uint8_t inner_alpha,uint8_t outer_alpha,uint32_t segments) {
    if (!std::isfinite(inner_ratio) || inner_ratio < 0 || inner_ratio > 1 || segments < 3 || segments > 256)
        throw std::invalid_argument("Invalid billboard ring");
    MeshGeometry mesh;
    for (uint32_t i = 0; i < segments; ++i) {
        const double angle = i*(6.283185307179586/segments);
        const float x = static_cast<float>(std::cos(angle)), y = static_cast<float>(std::sin(angle));
        mesh.vertices.push_back({x,y,0}); mesh.vertices.push_back({x*inner_ratio,y*inner_ratio,0});
    }
    for (uint32_t i = 0; i < segments; ++i) {
        const uint32_t next = (i+1)%segments;
        MeshPolygon face; face.normal = {0,0,-1};
        face.corners = {{i*2,0,0,0,0,outer_alpha},{next*2,0,0,0,0,outer_alpha},
            {next*2+1,0,0,0,0,inner_alpha},{i*2+1,0,0,0,0,inner_alpha}};
        mesh.polygons.push_back(std::move(face));
    }
    return mesh;
}
std::vector<Vec3> deformMesh(const MeshGeometry& mesh,float range,double time,uint32_t owner,uint32_t part) {
    if (!std::isfinite(range) || !std::isfinite(time)) throw std::invalid_argument("Invalid mesh deformation");
    std::vector<Vec3> positions;
    if (range == 0) return positions;
    positions.reserve(mesh.vertices.size());
    for (size_t i = 0; i < mesh.vertices.size(); ++i) {
        const float jitter = (static_cast<int>(visualNoise(time,owner,part*0x9e3779b9u+static_cast<uint32_t>(i))%1000)-500)/500.f;
        const float scale = 1+range*jitter;
        const auto v = mesh.vertices[i];
        positions.push_back({v.x*scale,v.y*scale,v.z*scale});
    }
    return positions;
}
}
