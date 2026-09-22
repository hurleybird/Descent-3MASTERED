// Descent 3MASTERED - GPL-3.0-or-later
#include "SceneGeometry.h"
#include "SceneLighting.h"
#include <cmath>
#include <stdexcept>
#include <algorithm>
namespace d3scene {
std::vector<GlowSlice> thrusterSlices(float radius,float length) {
    if (!std::isfinite(radius) || !std::isfinite(length)) throw std::invalid_argument("Invalid thruster profile");
    std::vector<GlowSlice> result;
    if (length < .1f || radius <= .05f) return result;
    // The 100-slice maximum and three slices/unit are the original authored
    // appearance, not a renderer submission/storage limit.
    const int divisions = static_cast<int>((std::min)(100.f,length*3));
    if (!divisions) return result;
    const float radius_step = radius/divisions, distance_step = length/divisions;
    float distance = 0;
    for (int i = 0; i < divisions && radius > .05f; ++i) {
        result.push_back({distance,radius});
        radius -= radius_step; distance += distance_step;
    }
    std::reverse(result.begin(),result.end());
    return result;
}
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
MeshGeometry unitQuadBatch(uint32_t count) {
    if (!count || count > 128) throw std::invalid_argument("Invalid canonical quad batch size");
    MeshGeometry result;
    const auto quad = unitBillboardQuad();
    for (uint32_t index = 0; index < count; ++index) {
        result.vertices.insert(result.vertices.end(),quad.vertices.begin(),quad.vertices.end());
        auto polygon = quad.polygons[0];
        for (auto& corner : polygon.corners) corner.vertex += index*4;
        result.polygons.push_back(std::move(polygon));
    }
    return result;
}
std::vector<Vec2> preparedTextureCoordinates(const PreparedMesh& mesh) {
    if (mesh.texture_uvs.empty()) return {};
    if (!mesh.asset || mesh.texture_uvs.size() != mesh.asset->geometry().vertices.size())
        throw std::invalid_argument("Prepared UVs do not match mesh topology");
    std::vector<Vec2> result;
    for (const auto& polygon : mesh.asset->geometry().polygons)
        forEachTriangleCorner(polygon.corners.size(),[&](size_t corner) {
            result.push_back(mesh.texture_uvs.at(polygon.corners[corner].vertex));
        });
    return result;
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
