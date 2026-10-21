// Descent 3MASTERED - GPL-3.0-or-later
#include "SceneExtraction.h"
#include "SceneMaterialBindings.h"
#include "gametexture.h"
#include "config.h"
#include "../renderer/SceneTerrain.h"
#include "../renderer/SceneGeometry.h"
#include "terrain.h"
#include "lightmap.h"
#include "bitmap.h"
#include "gameloop.h"
#include "args.h"
#include "pserror.h"
#include <algorithm>
#include <map>
#include <cmath>
#include <cstring>
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
    if (int64_t(Terrain_checksum)+1 == Terrain_occlusion_checksum) {
        auto occlusion = assets.terrainOcclusion();
        const auto* bytes = reinterpret_cast<const uint8_t*>(Terrain_occlusion_map);
        if (!occlusion || occlusion->data().width != OCCLUSION_SIZE || occlusion->data().depth != OCCLUSION_SIZE ||
            occlusion->data().sector_size != TERRAIN_SIZE*OCCLUSION_SIZE ||
            occlusion->data().height_ceiling != MAX_TERRAIN_HEIGHT ||
            occlusion->data().visibility.size() != sizeof(Terrain_occlusion_map) ||
            !std::equal(occlusion->data().visibility.begin(),occlusion->data().visibility.end(),bytes)) {
            d3scene::TerrainOcclusionData source;
            source.width = source.depth = OCCLUSION_SIZE;
            source.sector_size = TERRAIN_SIZE*OCCLUSION_SIZE; source.height_ceiling = MAX_TERRAIN_HEIGHT;
            source.visibility.assign(bytes,bytes+sizeof(Terrain_occlusion_map));
            occlusion = assets.publishTerrainOcclusion(source);
        }
        frame.terrain.occlusion = occlusion;
    } else frame.terrain.occlusion.reset();
    frame.terrain.observation_distance = Detail_settings.Terrain_render_distance*(Render_extended_draw_distance ? 1.5f : 1.f);
    if (AutomatedCapturePending() || AutomatedCaptureFrameIndex() == 0)
        AutomatedCaptureLog("scene terrain visibility table=%d distance=%g",bool(frame.terrain.occlusion),frame.terrain.observation_distance);
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
        binding.occlusion_class = d3scene::OcclusionClass::Terrain;
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
    // Geometry belongs to the authored sky, not the presentation clock. Keep
    // its inputs alongside immutable assets; rotation/materials remain frame
    // data. Compare only geometry inputs (not mutable fog or rotation state).
    struct SkyGeometry {
        uint64_t lifetime = 0;
        terrain_sky source{};
        d3scene::MeshAssetRef dome, horizon, stars;
    };
    static SkyGeometry retained;
    if (retained.lifetime != frame.scene_lifetime) {
        retained = {};
        retained.lifetime = frame.scene_lifetime;
    }
    const auto& prior = retained.source;
    const bool same_horizon = std::memcmp(prior.horizon_vectors,Terrain_sky.horizon_vectors,sizeof(prior.horizon_vectors)) == 0;
    if (!same_horizon || std::memcmp(prior.horizon_u,Terrain_sky.horizon_u,sizeof(prior.horizon_u)) ||
        std::memcmp(prior.horizon_v,Terrain_sky.horizon_v,sizeof(prior.horizon_v))) retained.dome.reset();
    if (!same_horizon || prior.sky_color != Terrain_sky.sky_color || prior.horizon_color != Terrain_sky.horizon_color)
        retained.horizon.reset();
    if (std::memcmp(prior.star_vectors,Terrain_sky.star_vectors,sizeof(prior.star_vectors)) ||
        std::memcmp(prior.star_color,Terrain_sky.star_color,sizeof(prior.star_color))) retained.stars.reset();
    const auto color = [](ddgr_color c) -> d3scene::Vec3 {
        return {GR_COLOR_RED(c)/255.f,GR_COLOR_GREEN(c)/255.f,GR_COLOR_BLUE(c)/255.f};
    };
    frame.sky_enabled = true;
    frame.sky_color = color(Terrain_sky.sky_color);
    frame.exterior_fog.color = color(Terrain_sky.fog_color);
    frame.exterior_fog.end = 200.f*TERRAIN_SIZE;
    frame.exterior_fog.start = frame.exterior_fog.end*(std::max)(0.f,(std::min)(.9999f,Terrain_sky.fog_scalar));
    frame.exterior_fog.enabled = Detail_settings.Fog_enabled && (Terrain_sky.flags & TF_FOG);
    const auto skyRotation = [&](unsigned flag) {
      d3scene::Transform rotation;
      if ((Terrain_sky.flags & flag) && Terrain_sky.rotate_rate > 0) {
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
      return rotation;
    };
    const auto rotation = skyRotation(TF_ROTATE_SKY);
    const auto publish = [&](const d3scene::MeshAssetRef& asset,int bitmap) {
        d3scene::PreparedMesh mesh;
        mesh.asset = asset;
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
      if (!retained.dome) {
        d3scene::MeshGeometry dome;
        for (int t = 0; t < MAX_HORIZON_PIECES; ++t) {
            const int next = (t+1)%MAX_HORIZON_PIECES;
            polygon(dome,{{t,0},{next,1},{t,1}},false);
            for (int ring = 1; ring < 4; ++ring)
                polygon(dome,{{t,ring},{next,ring},{next,ring+1},{t,ring+1}},false);
        }
        retained.dome = d3scene::assets().publishMesh(0,std::move(dome));
      }
      const int bitmap = GetTextureBitmapAtTime(Terrain_sky.dome_texture,0,static_cast<float>(frame.simulation_time),false);
      publish(retained.dome,GetHighResolutionSkyBitmap(Terrain_sky.dome_texture,bitmap));
    }
    if (!retained.horizon) {
    d3scene::MeshGeometry horizon;
    for (int t = 0; t < MAX_HORIZON_PIECES; ++t)
        polygon(horizon,{{t,4},{(t+1)%MAX_HORIZON_PIECES,4},{(t+1)%MAX_HORIZON_PIECES,5},{t,5}},true);
    retained.horizon = d3scene::assets().publishMesh(1,std::move(horizon));
    }
    publish(retained.horizon,-1);
    if (Terrain_sky.flags & TF_STARS) {
      if (!retained.stars) {
        d3scene::MeshGeometry stars;
        const d3scene::Vec2 offsets[4] = {{-.5f,.5f},{.5f,.5f},{.5f,-.5f},{-.5f,-.5f}};
        for (unsigned index = 0; index < MAX_STARS; ++index) {
            const auto& position = Terrain_sky.star_vectors[index];
            d3scene::MeshPolygon marker; marker.normal = {0,0,-1};
            for (unsigned corner = 0; corner < 4; ++corner) {
                marker.corners.push_back({static_cast<uint32_t>(stars.vertices.size())});
                stars.vertices.push_back({position.x,position.y,position.z});
                stars.vertex_colors.push_back(color(Terrain_sky.star_color[index]));
                stars.raster_offsets.push_back(offsets[corner]);
            }
            stars.polygons.push_back(std::move(marker));
        }
        retained.stars = d3scene::assets().publishMesh(4,std::move(stars));
      }
        d3scene::PreparedMesh batch;
        batch.asset = retained.stars;
        batch.world = skyRotation(TF_ROTATE_STARS); batch.camera_translation = {1,1,1};
        batch.surface.layer = d3scene::RenderLayer::Background;
        batch.surface.unlit = true; batch.surface.two_sided = true;
        batch.surface.blend = d3scene::BlendMode::Alpha;
        frame.meshes.push_back(std::move(batch));
    }
    retained.source = Terrain_sky;
    if (Terrain_sky.flags & TF_SATELLITES) {
        const auto quad = d3scene::assets().quadMesh();
        for (unsigned index = 0; index < Terrain_sky.num_satellites && index < MAX_SATELLITES; ++index) {
            const int texture = Terrain_sky.satellite_texture[index];
            if (texture < 0 || texture >= MAX_TEXTURES || !GameTextures[texture].used) continue;
            const int bitmap = GetTextureBitmapAtTime(texture,0,static_cast<float>(frame.simulation_time));
            const float size = Terrain_sky.satellite_size[index];
            const int width = bm_w(bitmap,0), height = bm_h(bitmap,0);
            if (AutomatedCaptureFrameIndex() == 0)
                AutomatedCaptureLog("scene satellite index=%u texture=%d name=%s bitmap=%d bitmap_name=%s size=%dx%d flags=%u",
                    index,texture,GameTextures[texture].name,bitmap,GameBitmaps[bitmap].name,width,height,Terrain_sky.satellite_flags[index]);
            if (!(size > 0) || !(Terrain_sky.radius > 0) || width <= 0 || height <= 0) continue;
            d3scene::PreparedMesh satellite;
            satellite.asset = quad;
            const auto& position = Terrain_sky.satellite_vectors[index];
            satellite.world.position = {position.x,position.y,position.z};
            satellite.world.right = {size,0,0}; satellite.world.up = {0,size*height/width,0};
            satellite.eye_distance = Terrain_sky.radius*3;
            satellite.alignment = d3scene::MeshAlignment::FacingEye;
            satellite.surface.layer = d3scene::RenderLayer::Background;
            satellite.surface.unlit = true; satellite.surface.two_sided = true;
            satellite.surface.texture_wrap = false; satellite.surface.texture_alpha = true;
            satellite.surface.bitmap = bitmap;
            satellite.surface.alpha = static_cast<unsigned>((std::max)(0.f,(std::min)(1.f,GameTextures[texture].alpha))*255)/255.f;
            satellite.surface.blend = GameTextures[texture].flags & TF_SATURATE ? d3scene::BlendMode::Additive : d3scene::BlendMode::Alpha;
            if (Terrain_sky.satellite_flags[index] & TSF_HALO) {
                auto halo = satellite;
                halo.asset = d3scene::assets().publishMesh(3,d3scene::billboardRing(.3f,102,0));
                halo.alignment = d3scene::MeshAlignment::Camera;
                halo.world.right = {size*1.2f,0,0}; halo.world.up = {0,size*1.2f,0};
                halo.surface.bitmap = -1; halo.surface.texture_alpha = false;
                halo.surface.alpha = 1; halo.surface.blend = d3scene::BlendMode::Alpha;
                const float r = Terrain_sky.satellite_r[index], g = Terrain_sky.satellite_g[index], b = Terrain_sky.satellite_b[index];
                const float scale = (std::max)(1.f,(std::max)(r,(std::max)(g,b)));
                halo.surface.color_modulation = {r/scale,g/scale,b/scale};
                frame.meshes.push_back(std::move(halo));
            }
            frame.meshes.push_back(satellite);
            const bool atmosphere_probe = SceneGeometryInspection() && FindArg("-scene-atmosphere-probe");
            if ((Terrain_sky.satellite_flags[index] & TSF_ATMOSPHERE) || atmosphere_probe) {
                // This authored wash faces the view with bank removed. Keep the
                // bitmap as coverage, rather than adding an unmasked rectangle.
                satellite.alignment = d3scene::MeshAlignment::CameraUpright;
                satellite.surface.color_modulation = frame.sky_color;
                satellite.surface.alpha = .4f;
                satellite.surface.blend = d3scene::BlendMode::Additive;
                frame.meshes.push_back(std::move(satellite));
            }
        }
    }
}
// Simulation (including outdoor damage) and presentation share one authored
// exposure sampler. The adapter borrows data only for this call.
float GetTerrainDynamicScalar(vector* position,int cell) {
    return d3scene::sampleTerrainExposure({Terrain_dynamic_table,TERRAIN_WIDTH,TERRAIN_DEPTH,TERRAIN_SIZE,int(MAX_TERRAIN_HEIGHT/8)},
        {position->x,position->y,position->z},cell);
}
