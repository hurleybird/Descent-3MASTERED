// Descent 3MASTERED - GPL-3.0-or-later
#include "SceneTerrain.h"
#include <cmath>
#include <limits>
#include <stdexcept>
#include <algorithm>
#include <map>
namespace d3scene {
float sampleTerrainExposure(TerrainExposureView grid,Vec3 position,int32_t cell) {
    if (!grid.values || !grid.width || !grid.depth || grid.spacing <= 0 || grid.height_step <= 0) return 1;
    const float x = position.x/grid.spacing, z = position.z/grid.spacing;
    const float y = std::max(0.f,position.y/grid.height_step);
    const float fx = x-std::trunc(x), fz = z-std::trunc(z), fy = y-std::floor(y);
    if (fx < 0 || fx > 1 || fz < 0 || fz > 1) return .5f;
    const int height = y >= 8 ? 8 : static_cast<int>(y);
    const auto sample = [&](int64_t index,int bit) {
        return index < 0 || uint64_t(index) >= uint64_t(grid.width)*grid.depth || bit >= 8 ? 1.f :
            (grid.values[index] & (1u<<bit) ? 1.f : 0.f);
    };
    const auto layer = [&](int bit) {
        const float left = (1-fz)*sample(cell,bit)+fz*sample(int64_t(cell)+grid.width,bit);
        const float right = (1-fz)*sample(int64_t(cell)+1,bit)+fz*sample(int64_t(cell)+grid.width+1,bit);
        return (1-fx)*left+fx*right;
    };
    return (1-fy)*layer(height)+fy*layer(height+1);
}
float sampleTerrainExposure(const TerrainGrid& grid,Vec3 position,int32_t cell) {
    // Preserve the authored legacy height sampling interval: int(350 / 8) = 43.
    return sampleTerrainExposure({grid.sky_visibility.empty() ? nullptr : grid.sky_visibility.data(),grid.width,grid.depth,grid.spacing,43},position,cell);
}
void prepareTerrainPatches(TerrainState& terrain) {
    terrain.patches.clear();
    if (!terrain.asset) return;
    for (const auto& patch : terrain.asset->patches()) {
        std::vector<SurfaceBinding> surfaces;
        for (const auto& batch : patch.batches) surfaces.push_back(terrain.materials.at(batch.material));
        terrain.patches.push_back(std::move(surfaces));
    }
}
TerrainAsset::TerrainAsset(AssetId id,const TerrainGrid& grid) : id_(id), grid_(grid) {
    constexpr uint32_t patch_size = 16;
    for (uint32_t z = 0; z < grid.depth-1; z += patch_size) for (uint32_t x = 0; x < grid.width-1; x += patch_size) {
        TerrainPatch patch;
        patch.bounds.minimum = {x*grid.spacing,std::numeric_limits<float>::max(),z*grid.spacing};
        patch.bounds.maximum = {std::min(x+patch_size,grid.width-1)*grid.spacing,
            -std::numeric_limits<float>::max(),std::min(z+patch_size,grid.depth-1)*grid.spacing};
        std::map<uint32_t,size_t> batches;
        for (uint32_t cz = z; cz < std::min(z+patch_size,grid.depth-1); ++cz)
            for (uint32_t cx = x; cx < std::min(x+patch_size,grid.width-1); ++cx) {
                const uint32_t index = cz*(grid.width-1)+cx;
                const auto& cell = grid.cells[index];
                if (!cell.visible) continue;
                const auto vertices = terrainCellTriangles(grid,index);
                if (vertices.empty()) continue;
                auto found = batches.find(cell.material);
                if (found == batches.end()) {
                    found = batches.emplace(cell.material,patch.batches.size()).first;
                    patch.batches.push_back({cell.material,{}});
                }
                auto& mesh = patch.batches[found->second].geometry;
                for (size_t triangle = 0; triangle < vertices.size(); triangle += 3) {
                    MeshPolygon polygon; polygon.normal = vertices[triangle].normal;
                    for (size_t corner = 0; corner < 3; ++corner) {
                        const auto& v = vertices[triangle+corner];
                        polygon.corners.push_back({static_cast<uint32_t>(mesh.vertices.size()),v.uv.x,v.uv.y,v.baked_uv.x,v.baked_uv.y,255});
                        mesh.vertices.push_back(v.position);
                        patch.bounds.minimum.y = std::min(patch.bounds.minimum.y,v.position.y);
                        patch.bounds.maximum.y = std::max(patch.bounds.maximum.y,v.position.y);
                    }
                    mesh.polygons.push_back(std::move(polygon));
                }
            }
        if (!patch.batches.empty()) patches_.push_back(std::move(patch));
    }
}
bool operator==(const TerrainGrid& a,const TerrainGrid& b) {
    if (a.width != b.width || a.depth != b.depth || a.spacing != b.spacing ||
        a.heights != b.heights || a.cells.size() != b.cells.size() || a.materials.size() != b.materials.size() ||
        a.clip_planes.size() != b.clip_planes.size() || a.sky_visibility != b.sky_visibility) return false;
    for (size_t i = 0; i < a.cells.size(); ++i) {
        const auto& x = a.cells[i]; const auto& y = b.cells[i];
        if (x.material != y.material || x.rotation != y.rotation || x.visible != y.visible || x.clip_plane != y.clip_plane) return false;
    }
    for (size_t i = 0; i < a.materials.size(); ++i)
        if (a.materials[i].texture != b.materials[i].texture || a.materials[i].lightmap != b.materials[i].lightmap) return false;
    for (size_t i = 0; i < a.clip_planes.size(); ++i) {
        const auto& x = a.clip_planes[i]; const auto& y = b.clip_planes[i];
        if (x.normal.x != y.normal.x || x.normal.y != y.normal.y || x.normal.z != y.normal.z || x.constant != y.constant) return false;
    }
    return true;
}
TerrainAssetRef SceneAssets::publishTerrain(const TerrainGrid& grid) {
    if (grid.width < 2 || grid.depth < 2 || !std::isfinite(grid.spacing) || grid.spacing <= 0 ||
        uint64_t(grid.width)*grid.depth != grid.heights.size() ||
        uint64_t(grid.width-1)*(grid.depth-1) != grid.cells.size())
        throw std::invalid_argument("Invalid terrain grid layout");
    if (!grid.sky_visibility.empty() && grid.sky_visibility.size() != grid.heights.size())
        throw std::invalid_argument("Incomplete terrain exposure field");
    for (const auto height : grid.heights)
        if (!std::isfinite(height)) throw std::invalid_argument("Non-finite terrain height");
    for (const auto& cell : grid.cells)
        if (cell.material >= grid.materials.size() || cell.clip_plane > grid.clip_planes.size()) throw std::invalid_argument("Missing terrain material/clip plane");
    for (const auto& plane : grid.clip_planes)
        if (!std::isfinite(plane.normal.x) || !std::isfinite(plane.normal.y) || !std::isfinite(plane.normal.z) || !std::isfinite(plane.constant) ||
            plane.normal.x*plane.normal.x+plane.normal.y*plane.normal.y+plane.normal.z*plane.normal.z <= 0)
            throw std::invalid_argument("Invalid terrain clip plane");
    if (terrain_ && terrain_->grid() == grid) return terrain_;
    if (revision_ == std::numeric_limits<uint64_t>::max()) throw std::overflow_error("Scene revision exhausted");
    TerrainAssetRef replacement(new TerrainAsset({lifetime_,revision_+1,0},grid));
    terrain_ = replacement; ++revision_;
    return replacement;
}
std::vector<TerrainVertex> terrainCellTriangles(const TerrainGrid& grid,uint32_t cell_index) {
    if (cell_index >= grid.cells.size() || grid.width < 2) throw std::out_of_range("Terrain cell");
    const auto& cell = grid.cells[cell_index];
    const uint32_t x = cell_index%(grid.width-1), z = cell_index/(grid.width-1);
    const uint32_t dx[4] = {0,1,1,0}, dz[4] = {1,1,0,0};
    const uint32_t corners[6] = {0,3,1,3,2,1};
    std::array<TerrainVertex,4> vertices{};
    for (uint32_t c = 0; c < 4; ++c) {
        auto& vertex = vertices[c];
        vertex.position = {(x+dx[c])*grid.spacing,grid.heights.at((z+dz[c])*grid.width+x+dx[c]),(z+dz[c])*grid.spacing};
        const float u = ((x&7u)+dx[c])*.125f, v = (7u-(z&7u)+1-dz[c])*.125f;
        const float tile = float(cell.rotation>>4);
        switch (cell.rotation&15) {
        case 1: vertex.uv = {(1-v)*tile,u*tile}; break;
        case 2: vertex.uv = {(1-u)*tile,(1-v)*tile}; break;
        case 3: vertex.uv = {v*tile,(1-u)*tile}; break;
        default: vertex.uv = {u*tile,v*tile}; break;
        }
        vertex.baked_uv = {((x&127u)+dx[c])/128.f,(128u-(z&127u)-dz[c])/128.f};
    }
    std::array<TerrainVertex,6> output{};
    for (size_t i = 0; i < 6; ++i) output[i] = vertices[corners[i]];
    for (size_t triangle = 0; triangle < 6; triangle += 3) {
        const auto a = output[triangle].position, b = output[triangle+1].position, c = output[triangle+2].position;
        const Vec3 u{c.x-a.x,c.y-a.y,c.z-a.z}, v{b.x-a.x,b.y-a.y,b.z-a.z};
        Vec3 n{u.y*v.z-u.z*v.y,u.z*v.x-u.x*v.z,u.x*v.y-u.y*v.x};
        const float length = std::sqrt(n.x*n.x+n.y*n.y+n.z*n.z);
        n = {n.x/length,n.y/length,n.z/length};
        for (size_t corner = 0; corner < 3; ++corner) output[triangle+corner].normal = n;
    }
    if (!cell.clip_plane) return {output.begin(),output.end()};
    const auto& plane = grid.clip_planes.at(cell.clip_plane-1);
    const auto distance = [&](const TerrainVertex& v) {
        return double(v.position.x)*plane.normal.x+double(v.position.y)*plane.normal.y+double(v.position.z)*plane.normal.z+plane.constant+.005;
    };
    const auto mix = [](float a,float b,double t) { return static_cast<float>(a+(b-a)*t); };
    std::vector<TerrainVertex> triangles;
    for (size_t triangle = 0; triangle < 6; triangle += 3) {
        std::vector<TerrainVertex> clipped;
        auto previous = output[triangle+2]; double previous_distance = distance(previous);
        for (size_t corner = 0; corner < 3; ++corner) {
            const auto current = output[triangle+corner]; const double current_distance = distance(current);
            if ((previous_distance >= 0) != (current_distance >= 0)) {
                const double t = previous_distance/(previous_distance-current_distance);
                TerrainVertex crossing;
                crossing.position = {mix(previous.position.x,current.position.x,t),mix(previous.position.y,current.position.y,t),mix(previous.position.z,current.position.z,t)};
                crossing.normal = current.normal;
                crossing.uv = {mix(previous.uv.x,current.uv.x,t),mix(previous.uv.y,current.uv.y,t)};
                crossing.baked_uv = {mix(previous.baked_uv.x,current.baked_uv.x,t),mix(previous.baked_uv.y,current.baked_uv.y,t)};
                clipped.push_back(crossing);
            }
            if (current_distance >= 0) clipped.push_back(current);
            previous = current; previous_distance = current_distance;
        }
        for (size_t corner = 1; corner+1 < clipped.size(); ++corner) {
            triangles.push_back(clipped[0]); triangles.push_back(clipped[corner]); triangles.push_back(clipped[corner+1]);
        }
    }
    return triangles;
}
}
