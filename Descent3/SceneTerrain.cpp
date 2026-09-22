// Descent 3MASTERED - GPL-3.0-or-later
#include "SceneExtraction.h"
#include "SceneMaterialBindings.h"
#include "gametexture.h"
#include "config.h"
#include "../renderer/SceneTerrain.h"
#include "terrain.h"
#include "lightmap.h"
#include "pserror.h"
#include <algorithm>
#include <map>
#include <cmath>
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
void PrepareGameSky(d3scene::FrameData& frame) {
    const auto color = [](ddgr_color c) -> d3scene::Vec3 {
        return {GR_COLOR_RED(c)/255.f,GR_COLOR_GREEN(c)/255.f,GR_COLOR_BLUE(c)/255.f};
    };
    frame.sky_enabled = true;
    frame.sky_color = color(Terrain_sky.sky_color);
    frame.exterior_fog.color = color(Terrain_sky.fog_color);
    frame.exterior_fog.end = 200.f*TERRAIN_SIZE;
    frame.exterior_fog.start = frame.exterior_fog.end*(std::max)(0.f,(std::min)(.9999f,Terrain_sky.fog_scalar));
    frame.exterior_fog.enabled = Detail_settings.Fog_enabled && (Terrain_sky.flags & TF_FOG);
    d3scene::Transform rotation;
    if ((Terrain_sky.flags & TF_ROTATE_SKY) && Terrain_sky.rotate_rate > 0) {
        // A view never advances the sky. All views evaluate the same simulation
        // phase, without progressively rotating the level's authored vectors.
        const double phase = std::fmod(frame.simulation_time*Terrain_sky.rotate_rate*(65536.0/360.0),65536.0);
        matrix m;
        vm_AnglesToMatrix(&m,0,static_cast<angle>(phase),0);
        vm_TransposeMatrix(&m);
        rotation.right = {m.rvec.x,m.rvec.y,m.rvec.z};
        rotation.up = {m.uvec.x,m.uvec.y,m.uvec.z};
        rotation.forward = {m.fvec.x,m.fvec.y,m.fvec.z};
    }
    const auto publish = [&](uint32_t slot,d3scene::MeshGeometry geometry,int bitmap) {
        d3scene::PreparedMesh mesh;
        mesh.asset = d3scene::assets().publishMesh(slot,std::move(geometry));
        mesh.world = rotation;
        mesh.camera_translation = {1,.5f,1};
        mesh.surface.layer = d3scene::RenderLayer::Background;
        mesh.surface.unlit = true;
        mesh.surface.two_sided = true;
        mesh.surface.bitmap = bitmap;
        mesh.surface.texture_alpha = bitmap >= 0;
        frame.meshes.push_back(std::move(mesh));
    };
    const auto polygon = [&](d3scene::MeshGeometry& geometry,std::initializer_list<std::pair<int,int>> corners,bool gradient) {
        d3scene::MeshPolygon face;
        face.normal = {0,1,0};
        for (const auto& corner : corners) {
            const int t = corner.first, ring = corner.second;
            const auto& position = Terrain_sky.horizon_vectors[t][ring];
            d3scene::MeshCorner vertex;
            vertex.vertex = static_cast<uint32_t>(geometry.vertices.size());
            if (!gradient) { vertex.u = Terrain_sky.horizon_u[t][ring]; vertex.v = Terrain_sky.horizon_v[t][ring]; }
            geometry.vertices.push_back({position.x,position.y,position.z});
            if (gradient) geometry.vertex_colors.push_back(color(ring == 4 ? Terrain_sky.sky_color : Terrain_sky.horizon_color));
            face.corners.push_back(vertex);
        }
        geometry.polygons.push_back(std::move(face));
    };
    if (Terrain_sky.textured) {
        d3scene::MeshGeometry dome;
        for (int t = 0; t < MAX_HORIZON_PIECES; ++t) {
            const int next = (t+1)%MAX_HORIZON_PIECES;
            polygon(dome,{{t,0},{next,1},{t,1}},false);
            for (int ring = 1; ring < 4; ++ring)
                polygon(dome,{{t,ring},{next,ring},{next,ring+1},{t,ring+1}},false);
        }
        const int bitmap = GetTextureBitmapAtTime(Terrain_sky.dome_texture,0,static_cast<float>(frame.simulation_time),false);
        publish(0,std::move(dome),GetHighResolutionSkyBitmap(Terrain_sky.dome_texture,bitmap));
    }
    d3scene::MeshGeometry horizon;
    for (int t = 0; t < MAX_HORIZON_PIECES; ++t)
        polygon(horizon,{{t,4},{(t+1)%MAX_HORIZON_PIECES,4},{(t+1)%MAX_HORIZON_PIECES,5},{t,5}},true);
    publish(1,std::move(horizon),-1);
}
// Simulation (including outdoor damage) and presentation share one authored
// exposure sampler. The adapter borrows data only for this call.
float GetTerrainDynamicScalar(vector* position,int cell) {
    return d3scene::sampleTerrainExposure({Terrain_dynamic_table,TERRAIN_WIDTH,TERRAIN_DEPTH,TERRAIN_SIZE,int(MAX_TERRAIN_HEIGHT/8)},
        {position->x,position->y,position->z},cell);
}
