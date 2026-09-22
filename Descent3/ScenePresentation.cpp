// Descent 3MASTERED - GPL-3.0-or-later
#include "SceneExtraction.h"
#include "SceneMaterialBindings.h"
#include "../renderer/SceneLighting.h"
#include "descent.h"
#include "bitmap.h"
#include <cmath>
#include <cstring>
#include <unordered_set>
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

bool SceneGeometryInspection() { return FindArg("-scene-inspection") != 0; }

namespace {
// Field-wise fingerprints diagnose state-vs-pixel variation; never hash padding
// or use diagnostic hashes as asset identities.
void logPreparedModels(const d3scene::FrameData& data) {
    if (!AutomatedCapturePending() && AutomatedCaptureFrameIndex() != 0) return;
    AutomatedCaptureLog("scene probe frame=%d time=%.9g",AutomatedCaptureFrameIndex(),data.simulation_time);
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
void resolveMaterial(d3scene::SurfaceBinding& surface, float time, bool model) {
    if (!(surface.texture < 0 && model)) { // flat polygons may still carry baked light
    if (surface.texture < 0 || surface.texture >= MAX_TEXTURES || !GameTextures[surface.texture].used)
        Error("Scene material references missing texture %d", surface.texture);
    int base_texture = surface.texture;
    const auto& authored = GameTextures[surface.texture];
    if (!model && (surface.authored_flags & FF_DESTROYED) && (authored.flags & TF_DESTROYABLE))
        base_texture = authored.destroy_handle;
    else if (SplitSpecularTexturePathEnabled()) {
        const auto pair = FindSplitSpecularTexturePair(surface.texture);
        if (pair.valid) base_texture = pair.base_tmap;
    }
    if (base_texture < 0 || base_texture >= MAX_TEXTURES || !GameTextures[base_texture].used)
        Error("Scene material references missing base texture %d", base_texture);
    const auto& texture = GameTextures[base_texture];
    surface.bitmap = GetTextureBitmap(base_texture, 0);
    surface.u_offset = authored.slide_u ? std::fmod(time / authored.slide_u, 1.f) : 0;
    surface.v_offset = authored.slide_v ? std::fmod(time / authored.slide_v, 1.f) : 0;
    surface.alpha = texture.alpha;
    surface.additive = (texture.flags & TF_SATURATE) != 0;
    // Room 4444 textures historically carry specular masks, while model alpha
    // can be authored in that format. Keep the distinction at compilation.
    surface.texture_alpha = (texture.flags & TF_TMAP2) != 0 &&
        (model || bm_format(surface.bitmap) != BITMAP_FORMAT_4444);
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
    const auto source = ExtractGamePresentation(Gametime, Frametime, Game_paused, advance_history);
    auto data = source->data();
    static d3scene::PresentationRef previous_models;
    static uint64_t material_lifetime = 0;
    if (material_lifetime != data.scene_lifetime) {
        ResetSplitSpecularTexturePairCache();
        material_lifetime = data.scene_lifetime;
    }
    for (auto& room : data.rooms) {
      room.light_scalar = d3scene::roomLightScalar(static_cast<float>(data.simulation_time),room.pulse_time,
          room.pulse_offset,room.number,(room.authored_flags & RF_STROBE) != 0,
          (room.authored_flags & RF_FLICKER) != 0,GetFunctionMode() == EDITOR_MODE);
      for (auto& surface : room.surfaces) {
        resolveMaterial(surface,static_cast<float>(data.simulation_time),false);
        surface.lighting_modulation = room.light_scalar;
        surface.draw_surface = !(surface.authored_flags & FF_FLOATING_TRIG);
        if (surface.portal >= 0 && surface.portal < static_cast<int>(room.portals.size()))
            surface.draw_surface = surface.draw_surface && (room.portals[surface.portal].flags & PF_RENDER_FACES);
      }
    }
    PrepareGameModelInstances(data, previous_models ? &previous_models->data() : nullptr);
    for (auto& object : data.objects) for (auto& part : object.parts) for (auto& surface : part.surfaces)
        resolveMaterial(surface,static_cast<float>(data.simulation_time),true);
    PrepareGameSceneLights(data);
    logPreparedModels(data);
    // No domain is declared complete by a diagnostic subset of its consumers.
    auto prepared = d3scene::Presentation::freeze(data);
    if (advance_history) previous_models = prepared;
    return prepared;
}

void RenderGameSceneView(const d3scene::Presentation& frame, d3scene::View view) {
    if (!SceneGeometryInspection())
        Error("Renderer migration is incomplete. This test build requires -scene-inspection; use the preserved Candidate for gameplay.");
    view.geometry_inspection = true;
    view.feedback = d3scene::NoFeedback;
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
