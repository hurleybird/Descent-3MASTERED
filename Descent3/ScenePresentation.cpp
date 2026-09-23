// Descent 3MASTERED - GPL-3.0-or-later
#include "SceneExtraction.h"
#include "SceneMaterialBindings.h"
#include "SceneReflectionBake.h"
#include "SceneCanvas.h"
#include "SceneFeedback.h"
#include "../renderer/SceneGeometry.h"
#include "../renderer/SceneView.h"
#include "grtext.h"
#include "gamefont.h"
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
#include <stdexcept>
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
#include "terrain.h"

bool SceneGeometryInspection() { return FindArg("-scene-inspection") != 0; }

d3scene::View MakeSceneCanvasView(uint64_t collection,d3scene::Rect viewport,
    d3scene::ViewTarget target,bool inspection) {
    d3scene::View view;
    view.geometry_inspection = inspection; view.kind = d3scene::ViewKind::Preview;
    view.collection = view.history_key = collection;
    view.viewport = viewport; view.projection_anchor = {0,0,viewport.width,viewport.height};
    view.target = target; view.ordering = d3scene::ViewOrdering::Compositing;
    view.projection = d3scene::ViewProjection::Orthographic;
    view.orthographic_size = {float(viewport.width),float(viewport.height)};
    view.camera.position = {viewport.width*.5f,viewport.height*.5f,0};
    view.camera.up = {0,-1,0}; view.dynamic_lighting = false; view.portal_visibility = false;
    d3scene::validateView(view);
    return view;
}

void PrepareSceneQuad(d3scene::FrameData& frame,uint64_t collection,int32_t& next_order,
    d3scene::Rect rectangle,const SceneQuadStyle& style) {
    if (rectangle.width <= 0 || rectangle.height <= 0) return;
    d3scene::PreparedMesh quad;
    quad.asset = d3scene::assets().publishMesh(2,d3scene::unitBillboardQuad());
    quad.collection = collection;
    const float left = float(rectangle.x), top = float(rectangle.y);
    const float right = left+rectangle.width, bottom = top+rectangle.height;
    quad.vertices = {{left,top,.5f},{right,top,.5f},{right,bottom,.5f},{left,bottom,.5f}};
    quad.texture_uvs = {style.uv_min,{style.uv_max.x,style.uv_min.y},style.uv_max,
        {style.uv_min.x,style.uv_max.y}};
    auto& surface = quad.surface;
    surface.composition_order = next_order++; surface.bitmap = style.bitmap;
    surface.texture_alpha = true; surface.texture_wrap = false;
    surface.unlit = true; surface.two_sided = true; surface.depth = d3scene::DepthMode::Disabled;
    surface.blend = style.additive ? d3scene::BlendMode::Additive : d3scene::BlendMode::Alpha;
    surface.alpha = style.alpha; surface.color_modulation = style.color;
    frame.meshes.push_back(std::move(quad));
}

void PrepareSceneText(d3scene::FrameData& frame,uint64_t collection,int32_t& next_order,
    int x,int y,const char* text,const SceneTextStyle& style) {
    if (!text || style.font < 0 || !std::isfinite(style.scale) || style.scale <= 0)
        throw std::invalid_argument("Invalid prepared text input");
    const int origin = x, spacing = 1+grfont_GetTracking(style.font);
    d3scene::PreparedMesh run;
    const auto flush = [&] {
        if (run.vertices.empty()) return;
        const auto count = static_cast<uint32_t>(run.vertices.size()/4);
        // Game mesh slots 257..384 are canonical quad-count templates. UVs and
        // positions are frame attributes, never new assets for changing text.
        run.asset = d3scene::assets().publishMesh(256+count,d3scene::unitQuadBatch(count));
        frame.meshes.push_back(std::move(run)); run = {};
    };
    for (const auto* cursor = reinterpret_cast<const unsigned char*>(text); *cursor; ++cursor) {
        if (*cursor == '\n') { x = origin; y += int(grfont_GetHeight(style.font)*style.scale)+1; continue; }
        if (*cursor == ' ') { x += int(grfont_GetCharWidth(style.font,' ')*style.scale)+spacing; continue; }
        tFontGlyph glyph;
        const bool found = grfont_GetGlyph(style.font,*cursor,glyph);
        if (found && glyph.width > 0 && glyph.height > 0) {
            const int right = int(x+glyph.width*style.scale), bottom = int(y+glyph.height*style.scale);
            if (right > x && bottom > y) {
                if (!run.vertices.empty() && (run.surface.bitmap != glyph.bitmap || run.vertices.size() == 128*4)) flush();
                if (run.vertices.empty()) {
                    run.collection = collection;
                    auto& surface = run.surface;
                    surface.composition_order = next_order++;
                    surface.bitmap = glyph.bitmap; surface.texture_alpha = true; surface.texture_wrap = false;
                    surface.unlit = true; surface.two_sided = true; surface.depth = d3scene::DepthMode::Disabled;
                    surface.blend = style.additive ? d3scene::BlendMode::Additive : d3scene::BlendMode::Alpha;
                    surface.alpha = style.alpha; surface.color_modulation = style.color;
                }
                run.vertices.insert(run.vertices.end(),{{float(x),float(y),.5f},{float(right),float(y),.5f},
                    {float(right),float(bottom),.5f},{float(x),float(bottom),.5f}});
                run.texture_uvs.insert(run.texture_uvs.end(),{{glyph.u,glyph.v},{glyph.u+glyph.uv_width,glyph.v},
                    {glyph.u+glyph.uv_width,glyph.v+glyph.uv_height},{glyph.u,glyph.v+glyph.uv_height}});
            }
        }
        x += (found ? int(glyph.width*style.scale) : 1)+spacing+
            grfont_GetKernedSpacing(style.font,*cursor,cursor[1]);
    }
    flush();
}

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
    for (auto& object : data.model_instances) for (auto& model : object.models) for (auto& part : model.parts) for (auto& surface : part.surfaces) prepare(surface);
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
    for (const auto& instance : data.model_instances) for (const auto& model : instance.models) {
        uint64_t hash = 14695981039346656037ull;
        const auto number = [&](float value) {
            uint32_t bits; std::memcpy(&bits,&value,sizeof(bits));
            for (int byte = 0; byte < 4; ++byte) { hash ^= (bits>>(byte*8))&255; hash *= 1099511628211ull; }
        };
        const auto vector = [&](d3scene::Vec3 v) { number(v.x); number(v.y); number(v.z); };
        const auto transform = [&](const d3scene::Transform& t) { vector(t.position); vector(t.right); vector(t.up); vector(t.forward); };
        transform(instance.current);
        for (const auto& part : model.parts) {
            transform(part.current);
            for (const auto& surface : part.surfaces) if (surface.texture >= 0) textures.insert(surface.texture);
        }
        const auto owner = std::find_if(data.objects.begin(),data.objects.end(),
            [&](const d3scene::ObjectState& object) { return instance.has_owner && object.handle == instance.owner; });
        const unsigned type = owner == data.objects.end() ? OBJ_NONE : owner->type;
        AutomatedCaptureLog("scene pose object=%u model=%u name=%s type=%u hash=%llu position=%.9g,%.9g,%.9g",
            instance.owner,model.asset->id().slot,Poly_models[model.asset->id().slot].name,type,hash,
            instance.current.position.x,instance.current.position.y,instance.current.position.z);
        if (type == OBJ_PLAYER)
            AutomatedCaptureLog("scene player direction=%.9g,%.9g,%.9g",
                instance.current.forward.x,instance.current.forward.y,instance.current.forward.z);
    }
    for (int texture : textures) {
        // Resolve was already performed. Reading the source bitmap here must not
        // call GetTextureBitmap again (which can advance procedural state).
        const d3scene::SurfaceBinding* binding = nullptr;
        for (const auto& instance : data.model_instances) for (const auto& model : instance.models) for (const auto& part : model.parts)
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
    const auto control_delta = GetPlayerControlledTranslationDelta();
    data.player_control_delta = {control_delta.x,control_delta.y,control_delta.z};
    data.afterburner_factor = (std::max)(0.f,(std::min)(1.f,Render_afterburner_visual_factor));
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
    // Exterior content historically reflects the primary celestial anchor in
    // white, independently of the satellite's visible tint/halo. Compile that
    // appearance convention once; execution consumes the same reflection asset
    // contract as interior materials, never the mutable terrain sky table.
    d3scene::ReflectionData exterior_reflection_data;
    const auto& primary_anchor = Terrain_sky.satellite_vectors[0];
    exterior_reflection_data.sources.push_back({{primary_anchor.x,primary_anchor.y,primary_anchor.z},{1,1,1}});
    const auto exterior_reflection = d3scene::assets().publishReflection(UINT32_MAX,std::move(exterior_reflection_data));
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
        if (room.exterior && surface.specular_strength > 0) surface.reflection = exterior_reflection;
        surface.lighting_modulation = room.light_scalar;
        // Authored missing textures suppress game room faces at publication.
        // The geometry executor also supports flat and view-image surfaces;
        // it must not infer eligibility from a legacy bitmap sentinel.
        surface.draw_surface = !(surface.authored_flags & FF_FLOATING_TRIG) && surface.bitmap >= 0;
        if (surface.portal >= 0 && surface.portal < static_cast<int>(room.portals.size()))
            surface.draw_surface = surface.draw_surface && (room.portals[surface.portal].flags & PF_RENDER_FACES);
      }
      if (Detail_settings.Mirrored_surfaces && source.mirror_face >= 0 && source.mirror_face < source.num_faces) {
          const int texture = source.faces[source.mirror_face].tmap;
          if (texture >= 0 && texture < MAX_TEXTURES && GameTextures[texture].used && (GameTextures[texture].flags & TF_ALPHA)) {
              std::vector<uint32_t> polygons;
              for (size_t face = 0; face < room.surfaces.size(); ++face)
                  if (room.surfaces[face].texture == texture && room.surfaces[face].draw_surface)
                      polygons.push_back(static_cast<uint32_t>(face));
              room.mirrors = d3scene::compilePlanarSurfaces(room.asset->geometry(),std::move(polygons));
              if (!room.mirrors.empty() && (AutomatedCapturePending() || AutomatedCaptureFrameIndex() == 0))
                  AutomatedCaptureLog("scene mirror surfaces room=%d groups=%u",room.number,(unsigned)room.mirrors.size());
          }
      }
    }
    PrepareGameTerrain(data);
    PrepareGameSky(data);
    PrepareGameEffects(data);
    PrepareGameModelInstances(data, previous_models ? &previous_models->data() : nullptr);
    PrepareGameObjectEffects(data);
    PrepareGameScorches(data);
    if (SceneGeometryInspection() && FindArg("-scene-cockpit-probe")) PrepareGameCockpit(data,advance_history);
    if (SceneGeometryInspection() && FindArg("-scene-reticle-probe")) PrepareGameReticle(data);
    if (SceneGeometryInspection()) {
        SceneTextStyle style; style.font = HUD_FONT; style.color = {1,220/255.f,80/255.f}; style.additive = true;
        int order = 0;
        for (int pass = 0; pass < 2; ++pass) PrepareSceneText(data,SceneInspectionBanner,order,12,12,
            "RENDERER MIGRATION TEST",style);
        for (int pass = 0; pass < 2; ++pass) PrepareSceneText(data,SceneInspectionBanner,order,12,28,
            "Incomplete - not yet qualified for gameplay",style);
    }
    if (SceneGeometryInspection() && FindArg("-scene-view-image-probe")) {
        int order = 0; SceneQuadStyle style; style.uv_min = {0,1}; style.uv_max = {1,0};
        PrepareSceneQuad(data,SceneViewImageProbe,order,{16,72,320,180},style);
        data.meshes.back().surface.view_image_slot = 1;
        PrepareSceneQuad(data,SceneViewImageProbe+1,order,{0,0,320,180},style);
        data.meshes.back().surface.view_image_slot = 1;
    }
    if (SceneGeometryInspection() && FindArg("-scene-standalone-model-probe") && Player_object) {
        const auto owner = uint32_t(Player_object->handle);
        unsigned detached = 0;
        for (auto& instance : data.model_instances) if (instance.has_owner && instance.owner == owner) {
            instance.has_owner = false; ++detached;
            if (FindArg("-scene-model-collection-probe")) instance.collection = 1;
            for (auto& mesh : data.meshes) if (mesh.model_instance == instance.identity) {
                mesh.has_owner = false; mesh.collection = instance.collection;
            }
        }
        if (!detached) Error("Standalone model fixture found no player model");
        if (AutomatedCapturePending() || AutomatedCaptureFrameIndex() == 0)
            AutomatedCaptureLog("scene standalone model fixture instances=%u",detached);
    }
    for (auto& surface : data.terrain.materials) resolveMaterial(surface,static_cast<float>(data.simulation_time),false,true);
    for (auto& object : data.model_instances) for (auto& model : object.models) for (auto& part : model.parts) for (auto& surface : part.surfaces)
        resolveMaterial(surface,static_cast<float>(data.simulation_time),true);
    prepareImages(data);
    d3scene::prepareTerrainPatches(data.terrain);
    PrepareGameSceneLights(data);
    for (auto& room : data.rooms) for (auto& portal : room.portals) {
        // Exterior door apertures have authored winding conventions that are
        // not interior-facing portal planes. Compile this once, not in queries.
        portal.two_sided = room.exterior && portal.destination_room >= 0 &&
            portal.destination_room < MAX_ROOMS && (Rooms[portal.destination_room].flags & RF_DOOR);
        const auto& surface = room.surfaces[portal.polygon];
        const auto material = d3scene::compileMaterialPolicy(surface,room.asset->geometry().polygons[portal.polygon]);
        portal.traversable = !(portal.flags & PF_RENDER_FACES) || material.allowsBehind() ||
            (GameTextures[surface.texture].flags & TF_PROCEDURAL);
    }
    data.fog = d3scene::assets().publishFog(data.rooms);
    if (SceneGeometryInspection() && FindArg("-scene-lod-probe") && Player_object) {
        // Isolated authored-model inspection: keep every player variant and
        // its prepared attachments, but no room that could occlude a far camera.
        const auto owner = uint32_t(Player_object->handle);
        data.objects.erase(std::remove_if(data.objects.begin(),data.objects.end(),
            [&](const d3scene::ObjectState& object) { return object.handle != owner; }),data.objects.end());
        data.model_instances.erase(std::remove_if(data.model_instances.begin(),data.model_instances.end(),
            [&](const d3scene::ModelInstance& instance) { return !instance.has_owner || instance.owner != owner; }),data.model_instances.end());
        data.meshes.erase(std::remove_if(data.meshes.begin(),data.meshes.end(),
            [&](const d3scene::PreparedMesh& mesh) { return mesh.collection == 0 && (!mesh.has_owner || mesh.owner != owner); }),data.meshes.end());
        data.rooms.clear(); data.terrain = {}; data.fog.reset(); data.exterior_fog = {};
        data.sky_enabled = false;
    }
    logPreparedModels(data);
    // No domain is declared complete by a diagnostic subset of its consumers.
    auto prepared = d3scene::Presentation::freeze(data);
    if (ps_rand_get_state() != simulation_random)
        Error("Presentation preparation consumed simulation randomness");
    if (advance_history) previous_models = prepared;
    return prepared;
}

d3scene::VisibilityReport RenderGameSceneView(const d3scene::Presentation& frame, d3scene::View view) {
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
        view.attached_camera = false; view.room = -1; view.exterior_origin = true;
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
    const int lod_probe = FindArg("-scene-lod-probe");
    if ((FindArg("-scene-model-effects-probe") || lod_probe) && Player_object) {
        for (const auto& object : frame.data().model_instances) if (object.identity == uint64_t(uint32_t(Player_object->handle))+1) {
            const auto& pose = object.current;
            float distance = Player_object->size*4+2;
            if (lod_probe) {
                const auto* argument = GetArg(lod_probe+1);
                if (!argument || argument[0] < '0' || argument[0] > '2' || argument[1])
                    Error("LOD fixture requires tier 0, 1 or 2");
                const int tier = argument[0]-'0';
                const float close_depth = distance*std::sqrt(1.f+.45f*.45f);
                const float depth = tier == 0 ? (std::min)(close_depth,object.lod_distances.x*.5f) :
                    tier == 1 ? (object.lod_distances.x+object.lod_distances.y)*.5f :
                    (std::max)(object.lod_distances.x,object.lod_distances.y)*1.1f;
                if (!(depth > 0) || object.models.empty()) Error("LOD fixture player has no authored LOD range");
                distance = depth/std::sqrt(1.f+.45f*.45f);
                view.zoom *= .4f*close_depth/depth;
                AutomatedCaptureLog("scene LOD fixture tier=%d variants=%u depth=%.6g thresholds=%.6g,%.6g",
                    tier,unsigned(object.models.size()),depth,object.lod_distances.x,object.lod_distances.y);
            }
            view.camera = pose;
            view.camera.position = {pose.position.x-pose.forward.x*distance+pose.right.x*distance*.45f,
                pose.position.y-pose.forward.y*distance+pose.right.y*distance*.45f,
                pose.position.z-pose.forward.z*distance+pose.right.z*distance*.45f};
            vector toward{pose.position.x-view.camera.position.x,pose.position.y-view.camera.position.y,
                pose.position.z-view.camera.position.z};
            vector up{pose.up.x,pose.up.y,pose.up.z};
            matrix facing; vm_VectorToMatrix(&facing,&toward,&up,nullptr);
            view.camera.right = {facing.rvec.x,facing.rvec.y,facing.rvec.z};
            view.camera.up = {facing.uvec.x,facing.uvec.y,facing.uvec.z};
            view.camera.forward = {facing.fvec.x,facing.fvec.y,facing.fvec.z};
            view.room = object.room; view.attached_camera = false; view.exclude_camera_rig = false; view.excluded_objects.clear();
            view.exterior_origin = object.room != -1 && ROOMNUM_OUTSIDE(object.room);
            break;
        }
    }
    if (FindArg("-scene-scorch-view-probe")) {
        for (const auto& mesh : frame.data().meshes) if (mesh.receiver_polygon >= 0) {
            for (const auto& room : frame.data().rooms) if (room.number == mesh.room) {
                const auto normal = room.asset->geometry().polygons[mesh.receiver_polygon].normal;
                vector forward{-normal.x,-normal.y,-normal.z};
                matrix facing; vm_VectorToMatrix(&facing,&forward,nullptr,nullptr);
                view.camera.position = {mesh.world.position.x+normal.x*12,mesh.world.position.y+normal.y*12,
                    mesh.world.position.z+normal.z*12};
                view.camera.right = {facing.rvec.x,facing.rvec.y,facing.rvec.z};
                view.camera.up = {facing.uvec.x,facing.uvec.y,facing.uvec.z};
                view.camera.forward = {facing.fvec.x,facing.fvec.y,facing.fvec.z};
                view.room = room.number; view.exterior_origin = false; view.attached_camera = false;
                break;
            }
            break;
        }
    }
    if (FindArg("-scene-mirror-camera-probe") && view.kind == d3scene::ViewKind::Main) {
        bool found = false;
        for (const auto& room : frame.data().rooms) {
            const auto& geometry = room.asset->geometry();
            if (room.mirrors.empty()) continue;
            const auto& mirror = room.mirrors.front();
            const auto polygon = mirror.polygons.front();
            const auto& face = geometry.polygons[polygon];
            if (face.corners.empty()) continue;
            d3scene::Vec3 center;
            for (const auto& corner : face.corners) {
                const auto& p = geometry.vertices[corner.vertex];
                center.x += p.x/face.corners.size(); center.y += p.y/face.corners.size(); center.z += p.z/face.corners.size();
            }
            const auto n = mirror.plane.normal;
            vector forward{-n.x,-n.y,-n.z}; matrix basis;
            vm_VectorToMatrix(&basis,&forward,nullptr,nullptr);
            view.camera.position = {center.x+n.x*20,center.y+n.y*20,center.z+n.z*20};
            view.camera.right = {basis.rvec.x,basis.rvec.y,basis.rvec.z};
            view.camera.up = {basis.uvec.x,basis.uvec.y,basis.uvec.z};
            view.camera.forward = {basis.fvec.x,basis.fvec.y,basis.fvec.z};
            view.attached_camera = false; view.exclude_camera_rig = false;
            view = d3scene::reflectedView(frame,view,mirror.plane,0x4d4952524f525001ull,room.number);
            for (const auto face : mirror.polygons) view.excluded_surfaces.push_back({room.asset->id(),face});
            // Inspect the reflected image with main-view image products. This
            // fixture is NOT mirror-surface composition or gameplay feedback.
            view.kind = d3scene::ViewKind::Main;
            if (AutomatedCapturePending() || AutomatedCaptureFrameIndex() == 0)
                AutomatedCaptureLog("scene mirror camera room=%d polygon=%u groups=%u reversed=%d clip=1",
                    room.number,polygon,(unsigned)room.mirrors.size(),d3scene::reversesWinding(view.camera));
            found = true; break;
        }
        if (!found) Error("Mirror camera fixture requires an authored mirror");
    }
    // Diagnostic views do not mark rooms explored or tell AI it was observed.
    // The gameplay feedback compiler barrier remains until the normal executor
    // and its visibility policy have been migrated.
    if (FindArg("-scene-view-image-probe") && view.kind == d3scene::ViewKind::Main) {
        d3scene::ViewImageRequest image;
        image.image = SceneViewImageProbe; image.view = view;
        image.view.kind = d3scene::ViewKind::Preview; image.view.feedback = d3scene::NoFeedback;
        image.view.history_key = SceneViewImageProbe;
        image.view.viewport = image.view.projection_anchor = {0,0,320,180};
        image.samples = 4;
        d3scene::ViewImageRequest copy;
        copy.image = SceneViewImageProbe+1;
        copy.view = MakeSceneCanvasView(SceneViewImageProbe+1,{0,0,320,180},d3scene::ViewTarget::Scene,true);
        copy.view.image_inputs.push_back({1,image.image});
        // Deliberately reversed producer order: the graph, not caller order,
        // owns the dependency from this canvas image to the scene image.
        d3scene::renderViewImages(frame,{copy,image});
    }
    auto observations = d3scene::renderView(frame, view);
    if (FindArg("-scene-observation-probe") &&
        (view.kind == d3scene::ViewKind::Main || view.kind == d3scene::ViewKind::Auxiliary)) {
        auto inspection = view;
        inspection.feedback = d3scene::Automap | d3scene::ObjectVisibility | d3scene::EffectEligibility;
        const auto candidate = InspectSceneObservations(frame,inspection);
        observations = candidate.report; // collected for inspection only; never committed
        if (AutomatedCapturePending() || AutomatedCaptureFrameIndex() == 0)
            AutomatedCaptureLog("scene observation candidate rooms=%u objects=%u exterior_pending=%d mirrors_pending=%d",
                (unsigned)observations.rooms.size(),(unsigned)observations.objects.size(),
                candidate.exterior_pending,candidate.mirrors_pending);
    }
    if (view.kind == d3scene::ViewKind::Main && FindArg("-scene-model-collection-probe")) {
        // Exercise a second content collection in the same frozen frame. No
        // second preparation or temporary simulation model/object is involved.
        auto preview = view;
        preview.kind = d3scene::ViewKind::Preview; preview.collection = 1;
        preview.history_key = 0x434f4c4c454354ull;
        if (FindArg("-scene-display-view-probe")) {
            if (!rend_BeginPostPresentFrame()) Error("Display view fixture could not complete the world");
            preview.target = d3scene::ViewTarget::Display;
            preview.clear_depth = true;
            preview.viewport = {view.projection_anchor.width/2,view.projection_anchor.height/2,
                view.projection_anchor.width/2,view.projection_anchor.height/2};
            preview.projection_anchor = {0,0,preview.viewport.width,preview.viewport.height};
        }
        d3scene::renderView(frame,preview);
        if (AutomatedCapturePending() || AutomatedCaptureFrameIndex() == 0)
            AutomatedCaptureLog("scene model collection fixture world=0 preview=1");
        if (FindArg("-scene-display-view-probe") && (AutomatedCapturePending() || AutomatedCaptureFrameIndex() == 0))
            AutomatedCaptureLog("scene display view fixture target=display viewport=bottom_right");
    }
    return observations;
}

void RenderPreparedViewImageProbe(const d3scene::Presentation& frame) {
    d3scene::ForegroundLayer layer;
    layer.display_views.push_back(MakeSceneCanvasView(SceneViewImageProbe,{0,0,Max_window_w,Max_window_h},
        d3scene::ViewTarget::Display,true));
    layer.display_views.back().image_inputs.push_back({1,SceneViewImageProbe+1});
    d3scene::renderForeground(frame,layer);
}
void RenderPreparedInspectionBanner(const d3scene::Presentation& frame) {
    d3scene::ForegroundLayer layer;
    layer.display_views.push_back(MakeSceneCanvasView(SceneInspectionBanner,{0,0,Max_window_w,Max_window_h},
        d3scene::ViewTarget::Display,true));
    d3scene::renderForeground(frame,layer);
}
void RenderPreparedCockpit(const d3scene::Presentation& frame, d3scene::Rect viewport, float zoom) {
    if (!SceneGeometryInspection()) Error("Cockpit migration has not passed gameplay acceptance");
    bool base = false, layered = false, late = false, gauges = false, monitors = false, text = false;
    for (const auto& instance : frame.data().model_instances) {
        base = base || instance.collection == SceneCockpitBase;
        layered = layered || instance.collection == SceneCockpitAfterGauges;
        late = late || instance.collection == SceneCockpitLate;
    }
    for (const auto& mesh : frame.data().meshes) {
        gauges = gauges || mesh.collection == SceneCockpitGauges;
        monitors = monitors || mesh.collection == SceneCockpitMonitorBacking;
        text = text || mesh.collection == SceneCockpitText;
    }
    if (!base && !layered && !late) Error("Cockpit fixture has no prepared cockpit model");
    d3scene::View view;
    view.geometry_inspection = true; view.kind = d3scene::ViewKind::Preview;
    view.viewport = viewport; view.projection_anchor = {0,0,viewport.width,viewport.height};
    view.zoom = zoom; view.dynamic_lighting = false; view.portal_visibility = false;
    d3scene::ForegroundLayer layer;
    if (monitors) {
        view.collection = view.history_key = SceneCockpitMonitorBacking;
        view.ordering = d3scene::ViewOrdering::Compositing;
        layer.scene_views.push_back(view);
        view.ordering = d3scene::ViewOrdering::Depth;
    }
    if (base) {
        view.collection = view.history_key = SceneCockpitBase;
        layer.scene_views.push_back(view);
    }
    if (gauges) {
        view.collection = view.history_key = SceneCockpitGauges;
        view.ordering = d3scene::ViewOrdering::Compositing;
        layer.scene_views.push_back(view);
        view.ordering = d3scene::ViewOrdering::Depth;
    }
    if (text) {
        auto canvas = MakeSceneCanvasView(SceneCockpitText,viewport,d3scene::ViewTarget::Scene,true);
        layer.scene_views.push_back(canvas);
    }
    if (layered) {
        view.collection = view.history_key = SceneCockpitAfterGauges;
        layer.scene_views.push_back(view);
    }
    if (late) {
        view.collection = view.history_key = SceneCockpitLate;
        view.target = d3scene::ViewTarget::Display;
        layer.display_views.push_back(view);
    }
    d3scene::renderForeground(frame,layer);
    if (AutomatedCapturePending() || AutomatedCaptureFrameIndex() == 0)
        AutomatedCaptureLog("scene cockpit composed base=%d late=%d gauges=%d text=%d",base,late,gauges,text);
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
