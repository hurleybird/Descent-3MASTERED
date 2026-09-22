// Descent 3MASTERED - GPL-3.0-or-later
#include "SceneGeometry.h"
#include "SceneLighting.h"
#include <cmath>
#include <stdexcept>
namespace d3scene {
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
