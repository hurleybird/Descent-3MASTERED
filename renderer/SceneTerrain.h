// Descent 3MASTERED - GPL-3.0-or-later
#pragma once
#include "Scene.h"
#include <array>
namespace d3scene {
struct TerrainMaterial {
    int32_t texture = -1, lightmap = -1;
};
struct TerrainCell {
    uint32_t material = 0;
    uint8_t rotation = 16; // low nibble quarter-turn, high nibble tile count
    bool visible = true;
    uint32_t clip_plane = 0; // zero means none; otherwise one-based plane index
};
struct TerrainClipPlane { Vec3 normal; float constant = 0; };
struct TerrainGrid {
    uint32_t width = 0, depth = 0;
    float spacing = 16;
    std::vector<float> heights; // width * depth; no view-dependent LOD scratch
    std::vector<TerrainCell> cells; // (width-1) * (depth-1), row-major
    std::vector<TerrainMaterial> materials;
    std::vector<TerrainClipPlane> clip_planes;
    std::vector<uint8_t> sky_visibility; // eight height bits per grid sample
};
bool operator==(const TerrainGrid&,const TerrainGrid&);
struct TerrainBatch {
    uint32_t material = 0;
    MeshGeometry geometry;
};
struct TerrainPatch {
    Bounds bounds;
    std::vector<TerrainBatch> batches;
};
class TerrainAsset final {
public:
    AssetId id() const { return id_; }
    const TerrainGrid& grid() const { return grid_; }
    const std::vector<TerrainPatch>& patches() const { return patches_; }
private:
    friend class SceneAssets;
    TerrainAsset(AssetId,const TerrainGrid&);
    AssetId id_;
    TerrainGrid grid_;
    std::vector<TerrainPatch> patches_;
};
struct TerrainVertex {
    Vec3 position, normal;
    Vec2 uv, baked_uv;
};
// One canonical cell expansion for visibility, lighting and GPU upload. Retains
// the authored diagonal, 8-cell texture tiling and 128-cell lightmap quadrants.
std::vector<TerrainVertex> terrainCellTriangles(const TerrainGrid&,uint32_t cell);
void prepareTerrainPatches(TerrainState&);
struct TerrainExposureView {
    const uint8_t* values;
    uint32_t width, depth;
    float spacing, height_step;
};
float sampleTerrainExposure(TerrainExposureView,Vec3,int32_t cell);
float sampleTerrainExposure(const TerrainGrid&,Vec3,int32_t cell);
}
