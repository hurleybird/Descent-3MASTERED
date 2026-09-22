// Descent 3MASTERED - GPL-3.0-or-later
#include "SceneExtraction.h"
#include "../renderer/SceneTerrain.h"
#include "terrain.h"
#include "lightmap.h"
#include "pserror.h"
#include <algorithm>
#include <map>
namespace {
float height(int index) { return (std::max)(0.f,(std::min)(Terrain_seg[index].y,MAX_TERRAIN_HEIGHT)); }
const terrain_tex_segment& terrainTexture(const terrain_segment& cell) {
    if (cell.texseg_index < 0 || cell.texseg_index >= TERRAIN_TEX_WIDTH*TERRAIN_TEX_DEPTH)
        Error("Invalid terrain texture segment %d",cell.texseg_index);
    return Terrain_tex_seg[cell.texseg_index];
}
int lightmap(const terrain_segment& cell) {
    if (cell.lm_quad >= 4) Error("Invalid terrain lightmap quadrant %d",cell.lm_quad);
    return TerrainLightmaps[cell.lm_quad];
}
bool matches(const d3scene::TerrainGrid& grid) {
    if (grid.width != TERRAIN_WIDTH || grid.depth != TERRAIN_DEPTH || grid.spacing != TERRAIN_SIZE) return false;
    if (grid.sky_visibility.size() != TERRAIN_WIDTH*TERRAIN_DEPTH ||
        !std::equal(grid.sky_visibility.begin(),grid.sky_visibility.end(),Terrain_dynamic_table)) return false;
    if (grid.clip_planes.size() != Num_terrain_render_clip_planes) return false;
    for (int i = 0; i < Num_terrain_render_clip_planes; ++i) {
        const auto& plane = Terrain_render_clip_planes[i]; const auto& owned = grid.clip_planes[i];
        if (owned.normal.x != plane.normal.x || owned.normal.y != plane.normal.y || owned.normal.z != plane.normal.z ||
            owned.constant != -(plane.normal*plane.point)) return false;
    }
    for (int i = 0; i < TERRAIN_WIDTH*TERRAIN_DEPTH; ++i)
        if (grid.heights[i] != height(i)) return false;
    for (int z = 0; z < TERRAIN_DEPTH-1; ++z) for (int x = 0; x < TERRAIN_WIDTH-1; ++x) {
        const auto& source = Terrain_seg[z*TERRAIN_WIDTH+x];
        const auto& material = terrainTexture(source);
        const auto& cell = grid.cells[z*(TERRAIN_WIDTH-1)+x];
        const auto& binding = grid.materials[cell.material];
        if (cell.visible != ((source.flags&TF_INVISIBLE) == 0) || cell.rotation != material.rotation || cell.clip_plane != Terrain_cell_clip_plane[z*TERRAIN_WIDTH+x] ||
            binding.texture != material.tex_index || binding.lightmap != lightmap(source)) return false;
    }
    return true;
}
}
void PrepareGameTerrain(d3scene::FrameData& frame) {
    auto& assets = d3scene::assets();
    auto asset = assets.terrain();
    if (!asset || !matches(asset->grid())) {
        d3scene::TerrainGrid grid;
        grid.width = TERRAIN_WIDTH; grid.depth = TERRAIN_DEPTH; grid.spacing = TERRAIN_SIZE;
        grid.sky_visibility.assign(Terrain_dynamic_table,Terrain_dynamic_table+TERRAIN_WIDTH*TERRAIN_DEPTH);
        if (Num_terrain_render_clip_planes < 0 || Num_terrain_render_clip_planes > MAX_TERRAIN_RENDER_CLIP_PLANES)
            Error("Invalid terrain clipping plane count");
        for (int i = 0; i < Num_terrain_render_clip_planes; ++i) {
            const auto& plane = Terrain_render_clip_planes[i];
            grid.clip_planes.push_back({{plane.normal.x,plane.normal.y,plane.normal.z},-(plane.normal*plane.point)});
        }
        grid.heights.reserve(TERRAIN_WIDTH*TERRAIN_DEPTH);
        for (int i = 0; i < TERRAIN_WIDTH*TERRAIN_DEPTH; ++i) grid.heights.push_back(height(i));
        std::map<std::pair<int,int>,uint32_t> materials;
        for (int z = 0; z < TERRAIN_DEPTH-1; ++z) for (int x = 0; x < TERRAIN_WIDTH-1; ++x) {
            const auto& source = Terrain_seg[z*TERRAIN_WIDTH+x];
            const auto& material = terrainTexture(source);
            const auto key = std::make_pair(int(material.tex_index),lightmap(source));
            auto found = materials.find(key);
            if (found == materials.end()) {
                const auto index = static_cast<uint32_t>(grid.materials.size());
                grid.materials.push_back({key.first,key.second});
                found = materials.emplace(key,index).first;
            }
            grid.cells.push_back({found->second,material.rotation,(source.flags&TF_INVISIBLE) == 0,Terrain_cell_clip_plane[z*TERRAIN_WIDTH+x]});
        }
        asset = assets.publishTerrain(grid);
    }
    frame.terrain.asset = asset;
    for (const auto& material : asset->grid().materials) {
        d3scene::SurfaceBinding binding;
        binding.texture = material.texture;
        binding.lightmap_bitmap = material.lightmap;
        if (material.lightmap >= 0 && material.lightmap < MAX_LIGHTMAPS) {
            const auto& image = GameLightmaps[material.lightmap];
            if (image.square_res) {
                binding.lightmap_u_scale = float(image.width)/image.square_res;
                binding.lightmap_v_scale = float(image.height)/image.square_res;
            }
        }
        frame.terrain.materials.push_back(binding);
    }
}
// Simulation (including outdoor damage) and presentation share one authored
// exposure sampler. The adapter borrows data only for this call.
float GetTerrainDynamicScalar(vector* position,int cell) {
    return d3scene::sampleTerrainExposure({Terrain_dynamic_table,TERRAIN_WIDTH,TERRAIN_DEPTH,TERRAIN_SIZE,int(MAX_TERRAIN_HEIGHT/8)},
        {position->x,position->y,position->z},cell);
}
