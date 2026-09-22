// Descent 3MASTERED - GPL-3.0-or-later
#include "SceneExtraction.h"
#include "SceneMaterialBindings.h"
#include "SceneReflectionBake.h"
#include "../renderer/SceneLighting.h"
#include "../renderer/SceneImages.h"
#include "../renderer/SceneTerrain.h"
#include "../renderer/SceneFog.h"
#include "config.h"
#include "descent.h"
#include "bitmap.h"
#include <cmath>
#include <algorithm>
#include <cstring>
#include <unordered_set>
#include <unordered_map>
#include "game.h"
#include "gameloop.h"
#include "gametexture.h"
#include "room.h"
#include "lightmap_info.h"
#include "lightmap.h"
#include "args.h"
#include "renderer.h"
#include "pserror.h"
#include "polymodel.h"
#include "door.h"
#include "doorway.h"
#include "psrand.h"
#include "damage.h"
#include "player.h"
#include "special_face.h"

bool SceneGeometryInspection() { return FindArg("-scene-inspection") != 0; }

d3scene::OcclusionClass SceneRoomOcclusionClass(int texture, bool exterior) {
    if (exterior) return d3scene::OcclusionClass::Default;
    // Compatibility interpretation of legacy content, resolved once upstream.
    // The GPU receives a semantic class and never guesses from geometry type.
    if (texture >= 0 && texture < MAX_TEXTURES && GameTextures[texture].used) {
        const auto& source = GameTextures[texture];
        if (source.flags & TF_RUBBLE) return d3scene::OcclusionClass::MineRock;
        for (const char* name = source.name; *name; ++name)
            if (!strnicmp(name,"rock",4) || !strnicmp(name,"rubble",6))
                return d3scene::OcclusionClass::MineRock;
    }
    return d3scene::OcclusionClass::Mine;
}

namespace {
void prepareImages(d3scene::FrameData& data) {
    std::unordered_map<int,d3scene::ImageAssetRef> images;
    const auto image = [&](int handle,bool baked) -> d3scene::ImageAssetRef {
        const int slot = handle+(baked ? MAX_BITMAPS : 0);
        const auto found = images.find(slot);
        if (found != images.end()) return found->second;
        d3scene::ImageSource source;
        if (baked) {
            const auto& lightmap = GameLightmaps[handle];
            source.padded_extent = lightmap.square_res;
            source.mips.push_back({lightmap.width,lightmap.height,reinterpret_cast<const uint8_t*>(lm_data(handle)),
                size_t(lightmap.width)*lightmap.height*2});
        } else {
            const int width = bm_w(handle,0), height = bm_h(handle,0); // make resident before borrowing data
            const auto* rgba = bm_data_truecolor(handle);
            if (rgba) {
                source.format = d3scene::ImageFormat::RGBA8;
                source.mips.push_back({static_cast<uint32_t>(width),static_cast<uint32_t>(height),rgba,size_t(width)*height*4});
                if (bm_mipped(handle)) source.generated_levels = NUM_MIP_LEVELS;
            } else {
                source.format = bm_format(handle) == BITMAP_FORMAT_4444 ? d3scene::ImageFormat::ARGB4444 : d3scene::ImageFormat::ARGB1555;
                const int levels = bm_mipped(handle) ? NUM_MIP_LEVELS : 1;
                for (int mip = 0; mip < levels; ++mip) {
                    const int w = bm_w(handle,mip), h = bm_h(handle,mip);
                    if (w < 1 || h < 1) break;
                    source.mips.push_back({static_cast<uint32_t>(w),static_cast<uint32_t>(h),
                        reinterpret_cast<const uint8_t*>(bm_data(handle,mip)),size_t(w)*h*2});
                }
            }
        }
        auto asset = d3scene::assets().publishImage(slot,source);
        images.emplace(slot,asset);
        return asset;
    };
    const auto prepare = [&](d3scene::SurfaceBinding& surface) {
        if (surface.bitmap >= 0) surface.color_image = image(surface.bitmap,false);
        if (surface.specular_bitmap >= 0) surface.specular_image = image(surface.specular_bitmap,false);
        if (surface.lightmap_bitmap >= 0) {
            surface.baked_image = image(surface.lightmap_bitmap,true);
            surface.lightmap_wrap = (GameLightmaps[surface.lightmap_bitmap].flags & LF_WRAP) != 0;
        }
    };
    for (auto& room : data.rooms) for (auto& surface : room.surfaces) prepare(surface);
    for (auto& object : data.objects) for (auto& part : object.parts) for (auto& surface : part.surfaces) prepare(surface);
    for (auto& surface : data.terrain.materials) prepare(surface);
    for (auto& mesh : data.meshes) prepare(mesh.surface);
}
// Field-wise fingerprints diagnose state-vs-pixel variation; never hash padding
// or use diagnostic hashes as asset identities.
void logPreparedModels(const d3scene::FrameData& data) {
    if (!AutomatedCapturePending() && AutomatedCaptureFrameIndex() != 0) return;
    AutomatedCaptureLog("scene probe frame=%d time=%.9g",AutomatedCaptureFrameIndex(),data.simulation_time);
    unsigned reflection_surfaces = 0, reflection_sources = 0, smooth_reflections = 0;
    for (const auto& room : data.rooms) for (const auto& surface : room.surfaces) if (surface.specular_strength > 0 && surface.reflection) {
        ++reflection_surfaces;
        reflection_sources += static_cast<unsigned>(surface.reflection->data().sources.size());
        smooth_reflections += !surface.reflection->data().corner_normals.empty();
    }
    AutomatedCaptureLog("scene authored reflections surfaces=%u sources=%u smooth=%u",reflection_surfaces,reflection_sources,smooth_reflections);
    size_t fog_triangles = 0;
    for (const auto& volume : d3scene::fogVolumes(data)) fog_triangles += volume.boundary.size();
    AutomatedCaptureLog("scene fog volumes=%u triangles=%u exterior=%d",static_cast<unsigned>(d3scene::fogVolumes(data).size()),
        static_cast<unsigned>(fog_triangles),data.exterior_fog.enabled ? 1 : 0);
    for (const auto& effect : data.camera_perturbations)
        AutomatedCaptureLog("scene camera perturbation owner=%u forward=%.9g,%.9g,%.9g",effect.owner,
            effect.rotation.forward.x,effect.rotation.forward.y,effect.rotation.forward.z);
    std::unordered_set<int> textures;
    for (const auto& instance : data.objects) if (instance.model) {
        uint64_t hash = 14695981039346656037ull;
        const auto number = [&](float value) {
            uint32_t bits; std::memcpy(&bits,&value,sizeof(bits));
            for (int byte = 0; byte < 4; ++byte) { hash ^= (bits>>(byte*8))&255; hash *= 1099511628211ull; }
        };
        const auto vector = [&](d3scene::Vec3 v) { number(v.x); number(v.y); number(v.z); };
        const auto transform = [&](const d3scene::Transform& t) { vector(t.position); vector(t.right); vector(t.up); vector(t.forward); };
        transform(instance.current);
        for (const auto& part : instance.parts) {
            transform(part.current);
            for (const auto& surface : part.surfaces) if (surface.texture >= 0) textures.insert(surface.texture);
        }
        AutomatedCaptureLog("scene pose object=%u model=%u name=%s type=%u hash=%llu position=%.9g,%.9g,%.9g",
            instance.handle,instance.model->id().slot,Poly_models[instance.model->id().slot].name,instance.type,hash,
            instance.current.position.x,instance.current.position.y,instance.current.position.z);
        if (instance.type == OBJ_PLAYER)
            AutomatedCaptureLog("scene player direction=%.9g,%.9g,%.9g",
                instance.current.forward.x,instance.current.forward.y,instance.current.forward.z);
    }
    for (int texture : textures) {
        // Resolve was already performed. Reading the source bitmap here must not
        // call GetTextureBitmap again (which can advance procedural state).
        const d3scene::SurfaceBinding* binding = nullptr;
        for (const auto& instance : data.objects) for (const auto& part : instance.parts)
            for (const auto& surface : part.surfaces) if (surface.texture == texture) binding = &surface;
        if (!binding || binding->bitmap < 0) continue;
        const int bitmap = binding->bitmap;
        const auto* pixels = bm_data(bitmap,0);
        uint64_t hash = 14695981039346656037ull;
        const int count = bm_w(bitmap,0)*bm_h(bitmap,0);
        for (int i = 0; i < count; ++i) { hash ^= pixels[i]; hash *= 1099511628211ull; }
        AutomatedCaptureLog("scene texture id=%d name=%s bitmap=%d flags=%u pixels=%llu",
            texture,GameTextures[texture].name,bitmap,GameTextures[texture].flags,hash);
    }
}
// One material resolver for all prepared mesh surfaces. Source-domain rules
// are explicit authoring conventions, not state inferred from a draw call.
void resolveMaterial(d3scene::SurfaceBinding& surface, float time, bool model, bool exterior = false) {
    if (!(surface.texture < 0 && model)) { // flat polygons may still carry baked light
    if (surface.texture < 0 || surface.texture >= MAX_TEXTURES || !GameTextures[surface.texture].used)
        Error("Scene material references missing texture %d", surface.texture);
    int base_texture = surface.texture;
    const auto& authored = GameTextures[surface.texture];
    const auto pair = SplitSpecularTexturePathEnabled() ? FindSplitSpecularTexturePair(surface.texture) : SplitSpecularTexturePair{};
    if (!model && (surface.authored_flags & FF_DESTROYED) && (authored.flags & TF_DESTROYABLE))
        base_texture = authored.destroy_handle;
    else if (pair.valid) base_texture = pair.base_tmap;
    if (base_texture < 0 || base_texture >= MAX_TEXTURES || !GameTextures[base_texture].used)
        Error("Scene material references missing base texture %d", base_texture);
    const auto& texture = GameTextures[base_texture];
    surface.bitmap = GetTextureBitmapAtTime(base_texture, 0, time);
    surface.u_offset = authored.slide_u ? std::fmod(time / authored.slide_u, 1.f) : 0;
    surface.v_offset = authored.slide_v ? std::fmod(time / authored.slide_v, 1.f) : 0;
    surface.alpha *= texture.alpha;
    surface.blend = (texture.flags & TF_SATURATE) ? d3scene::BlendMode::Additive : d3scene::BlendMode::Solid;
    // Room 4444 textures historically carry specular masks, while model alpha
    // can be authored in that format. Keep the distinction at compilation.
    surface.texture_alpha = (texture.flags & TF_TMAP2) != 0 &&
        (model || bm_format(surface.bitmap) != BITMAP_FORMAT_4444);
    if (!model && Detail_settings.Specular_lighting) {
        int specular_texture = surface.texture;
        int specular_bitmap = -1;
        if ((surface.authored_flags & FF_DESTROYED) && (authored.flags & TF_DESTROYABLE))
            specular_texture = authored.destroy_handle;
        else if (pair.valid) { specular_texture = pair.spec_tmap; specular_bitmap = pair.spec_bm; }
        if (specular_texture >= 0 && specular_texture < MAX_TEXTURES && GameTextures[specular_texture].used) {
            const auto& specular = GameTextures[specular_texture];
            if (specular_bitmap < 0) specular_bitmap = GetTextureBitmapAtTime(specular_texture,0,time);
            const bool opaque_mask = bm_format(specular_bitmap) == BITMAP_FORMAT_4444 && texture.alpha >= 1 &&
                surface.blend == d3scene::BlendMode::Solid && surface.lightmap >= 0 && (surface.authored_flags & FF_LIGHTMAP);
            const bool authored_path = (authored.flags & TF_SPECULAR) && (surface.special >= 0 || exterior);
            const bool generated_path = !exterior && opaque_mask && (pair.valid ||
                (Render_preferred_state.per_pixel_lighting && Render_per_pixel_field_static_specular));
            if (authored_path || generated_path) {
            surface.specular_bitmap = specular_bitmap;
            const float exponent = (specular.flags & TF_PLASTIC) ? 14.f : ((specular.flags & TF_MARBLE) ? 4.f : 6.f);
            surface.specular_exponent = (std::max)(1.f,(std::min)(128.f,std::floor(exponent*ConfigNormalizePerPixelSpecularSharpness(Render_per_pixel_specular_sharpness)+.5f)));
            surface.specular_strength = Render_per_pixel_specular_strength*specular.reflectivity*1.5f;
            surface.static_specular_strength = Render_per_pixel_static_specular_strength;
            surface.specular_lightmap_mix = Render_per_pixel_specular_lightmap_mix;
            surface.specular_alpha_strength = Render_per_pixel_specular_alpha_strength;
            }
        }
    }
    }
    if (surface.lightmap >= 0) {
        const int handle = LightmapInfo[surface.lightmap].lm_handle;
        if (handle >= 0 && handle < MAX_LIGHTMAPS && GameLightmaps[handle].square_res > 0) {
            surface.lightmap_bitmap = handle;
            surface.lightmap_u_scale = static_cast<float>(GameLightmaps[handle].width) / GameLightmaps[handle].square_res;
            surface.lightmap_v_scale = static_cast<float>(GameLightmaps[handle].height) / GameLightmaps[handle].square_res;
        }
    }
}
}

d3scene::PresentationRef PrepareGamePresentation(bool advance_history) {
    const unsigned int simulation_random = ps_rand_get_state();
    const auto source = ExtractGamePresentation(Gametime, Frametime, Game_paused, advance_history);
    auto data = source->data();
    data.lighting_quality = Render_preferred_state.per_pixel_lighting ?
        d3scene::LightingQuality::Pixel : d3scene::LightingQuality::Vertex;
    if (Player_object) {
        const auto rotation = PlayerCameraShakeRotation(data.simulation_time,Player_object->handle,data.paused);
        const auto vector = [](const ::vector& v) { return d3scene::Vec3{v.x,v.y,v.z}; };
        data.camera_perturbations.push_back({static_cast<uint32_t>(Player_object->handle),
            {{},vector(rotation.rvec),vector(rotation.uvec),vector(rotation.fvec)}});
    }
    static d3scene::PresentationRef previous_models;
    static uint64_t material_lifetime = 0;
    if (material_lifetime != data.scene_lifetime) {
        ResetSplitSpecularTexturePairCache();
        material_lifetime = data.scene_lifetime;
    }
    // Capture mutable authoring tables at the game/renderer boundary. Retained
    // presentations keep their own revision even if a face is edited or freed.
    std::unordered_map<int,d3scene::ReflectionAssetRef> reflections;
    uint32_t reflection_slot = MAX_SPECIAL_FACES;
    for (auto& room : data.rooms) {
      room.exterior = (room.authored_flags & RF_EXTERNAL) != 0;
      room.fog_enabled = Detail_settings.Fog_enabled && (room.authored_flags & RF_FOG) && GetFunctionMode() != EDITOR_MODE;
      const auto& source = Rooms[room.number];
      room.blocks_portals = (source.flags & RF_DOOR) && source.doorway_data &&
          DoorwayGetPosition(&Rooms[room.number]) == 0 && !(Doors[source.doorway_data->doornum].flags & DF_SEETHROUGH);
      room.light_scalar = d3scene::roomLightScalar(static_cast<float>(data.simulation_time),room.pulse_time,
          room.pulse_offset,room.number,(room.authored_flags & RF_STROBE) != 0,
          (room.authored_flags & RF_FLICKER) != 0,GetFunctionMode() == EDITOR_MODE);
      for (auto& surface : room.surfaces) {
        const uint32_t baked_reflection_slot = reflection_slot++;
        if (surface.special >= 0 && surface.special < MAX_SPECIAL_FACES) {
            const auto& authored = SpecialFaces[surface.special];
            if (authored.used && authored.type == SFT_SPECULAR) {
                auto found = reflections.find(surface.special);
                if (found == reflections.end()) {
                    d3scene::ReflectionData reflection;
                    if (authored.num && !authored.spec_instance)
                        Error("Scene reflection %d has no sources",surface.special);
                    for (unsigned i = 0; i < authored.num; ++i) {
                        const auto& source = authored.spec_instance[i];
                        const auto color = source.bright_color;
                        reflection.sources.push_back({{source.bright_center.x,source.bright_center.y,source.bright_center.z},
                            {float((color >> 10)&31)/31.f,float((color >> 5)&31)/31.f,float(color&31)/31.f}});
                    }
                    if ((authored.flags & SFF_SPEC_SMOOTH) && authored.vertnorms) {
                        const size_t face = &surface-room.surfaces.data();
                        const size_t count = room.asset->geometry().polygons[face].corners.size();
                        for (size_t i = 0; i < count; ++i) {
                            const auto& n = authored.vertnorms[i];
                            reflection.corner_normals.push_back({n.x,n.y,n.z});
                        }
                    }
                    found = reflections.emplace(surface.special,d3scene::assets().publishReflection(surface.special,std::move(reflection))).first;
                }
                surface.reflection = found->second;
            }
        }
        if (!room.exterior && data.lighting_quality == d3scene::LightingQuality::Pixel) {
            const auto baked = PublishRoomReflectionBake(room.number,static_cast<int>(&surface-room.surfaces.data()),
                baked_reflection_slot,Render_per_pixel_field_static_specular,Render_split_specular_textures);
            if (baked) surface.reflection = baked;
        }
        surface.occlusion_class = SceneRoomOcclusionClass(surface.texture,room.exterior);
        resolveMaterial(surface,static_cast<float>(data.simulation_time),false,room.exterior);
        surface.lighting_modulation = room.light_scalar;
        surface.draw_surface = !(surface.authored_flags & FF_FLOATING_TRIG);
        if (surface.portal >= 0 && surface.portal < static_cast<int>(room.portals.size()))
            surface.draw_surface = surface.draw_surface && (room.portals[surface.portal].flags & PF_RENDER_FACES);
      }
    }
    PrepareGameTerrain(data);
    PrepareGameSky(data);
    PrepareGameEffects(data);
    PrepareGameModelInstances(data, previous_models ? &previous_models->data() : nullptr);
    for (auto& surface : data.terrain.materials) resolveMaterial(surface,static_cast<float>(data.simulation_time),false,true);
    for (auto& object : data.objects) for (auto& part : object.parts) for (auto& surface : part.surfaces)
        resolveMaterial(surface,static_cast<float>(data.simulation_time),true);
    prepareImages(data);
    d3scene::prepareTerrainPatches(data.terrain);
    PrepareGameSceneLights(data);
    for (auto& room : data.rooms) for (auto& portal : room.portals) {
        const auto& surface = room.surfaces[portal.polygon];
        const auto material = d3scene::compileMaterialPolicy(surface,room.asset->geometry().polygons[portal.polygon]);
        portal.traversable = !(portal.flags & PF_RENDER_FACES) || material.allowsBehind() ||
            (GameTextures[surface.texture].flags & TF_PROCEDURAL);
    }
    data.fog = d3scene::assets().publishFog(data.rooms);
    logPreparedModels(data);
    // No domain is declared complete by a diagnostic subset of its consumers.
    auto prepared = d3scene::Presentation::freeze(data);
    if (ps_rand_get_state() != simulation_random)
        Error("Presentation preparation consumed simulation randomness");
    if (advance_history) previous_models = prepared;
    return prepared;
}

void RenderGameSceneView(const d3scene::Presentation& frame, d3scene::View view) {
    if (!SceneGeometryInspection())
        Error("Renderer migration is incomplete. This test build requires -scene-inspection; use the preserved Candidate for gameplay.");
    view.geometry_inspection = true;
    view.feedback = d3scene::NoFeedback;
    if (FindArg("-scene-history-probe")) {
        const int index = AutomatedCaptureFrameIndex();
        const float angle = .006f*index+(index >= 30 ? .35f : 0.f);
        const float c = std::cos(angle), s = std::sin(angle);
        const auto right = view.camera.right, forward = view.camera.forward;
        view.camera.right = {right.x*c-forward.x*s,right.y*c-forward.y*s,right.z*c-forward.z*s};
        view.camera.forward = {forward.x*c+right.x*s,forward.y*c+right.y*s,forward.z*c+right.z*s};
        view.camera_variant = index >= 30 ? 2 : 0;
        view.camera_cut = index == 30;
        AutomatedCaptureLog("scene history fixture frame=%d cut=%d variant=%u",index,view.camera_cut,view.camera_variant);
    }
    if ((FindArg("-scene-terrain-probe") || FindArg("-scene-sky-probe") || FindArg("-scene-satellite-probe")) && frame.data().terrain.asset) {
        const auto& grid = frame.data().terrain.asset->grid();
        const uint32_t x = grid.width/2, z = grid.depth/4;
        view.camera.position = {x*grid.spacing,grid.heights[z*grid.width+x]+120,z*grid.spacing};
        view.camera.right = {1,0,0}; view.camera.up = {0,.8f,.6f}; view.camera.forward = {0,-.6f,.8f};
        view.attached_camera = false; view.room = -1;
        if (FindArg("-scene-sky-probe")) {
            view.camera.position.y += 180;
            view.camera.up = {0,.9797959f,-.2f}; view.camera.forward = {0,.2f,.9797959f};
        }
    }
    if (FindArg("-scene-satellite-probe")) {
        for (const auto& mesh : frame.data().meshes) if (mesh.eye_distance > 0 && mesh.alignment == d3scene::MeshAlignment::FacingEye) {
            vector direction{mesh.world.position.x-view.camera.position.x,mesh.world.position.y-view.camera.position.y,
                mesh.world.position.z-view.camera.position.z};
            if (vm_GetMagnitude(&direction) <= .0001f) continue;
            matrix orientation; vm_VectorToMatrix(&orientation,&direction,nullptr,nullptr);
            view.camera.right = {orientation.rvec.x,orientation.rvec.y,orientation.rvec.z};
            view.camera.up = {orientation.uvec.x,orientation.uvec.y,orientation.uvec.z};
            view.camera.forward = {orientation.fvec.x,orientation.fvec.y,orientation.fvec.z};
            break;
        }
    }
    // Diagnostic views do not mark rooms explored or tell AI it was observed.
    // The gameplay feedback compiler barrier remains until the normal executor
    // and its visibility policy have been migrated.
    d3scene::renderView(frame, view);
}

void RenderModuleSceneView(object* viewer, vector* eye, int room, matrix* orientation,
    float zoom, bool rear, int x, int y, int w, int h) {
    if (!eye || !orientation) Error("Module scene view has no camera");
    int width, height;
    rend_GetProjectionParameters(&width, &height);
    if (w <= 0 || h <= 0) { x = y = 0; w = width; h = height; }
    const auto frame = PrepareGamePresentation(false);
    const auto view = MakeGameSceneView(d3scene::ViewKind::Preview, 0x100000000ull,
        viewer, *eye, room, *orientation, zoom, rear, {0, 0, width, height}, {x, y, w, h});
    RenderGameSceneView(*frame, view);
}
