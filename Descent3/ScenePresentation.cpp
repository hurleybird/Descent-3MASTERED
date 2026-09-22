// Descent 3MASTERED - GPL-3.0-or-later
#include "SceneExtraction.h"
#include "SceneMaterialBindings.h"
#include "bitmap.h"
#include <cmath>
#include "game.h"
#include "gameloop.h"
#include "gametexture.h"
#include "room.h"
#include "lightmap_info.h"
#include "lightmap.h"
#include "args.h"
#include "renderer.h"
#include "pserror.h"

bool SceneGeometryInspection() { return FindArg("-scene-inspection") != 0; }

namespace {
// One material resolver for all prepared mesh surfaces. Source-domain rules
// are explicit authoring conventions, not state inferred from a draw call.
void resolveMaterial(d3scene::SurfaceBinding& surface, float time, bool model) {
    if (surface.texture < 0 && model) return; // authored flat-color polygon
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
    PrepareGameModelInstances(data, previous_models ? &previous_models->data() : nullptr);
    static uint64_t material_lifetime = 0;
    if (material_lifetime != data.scene_lifetime) {
        ResetSplitSpecularTexturePairCache();
        material_lifetime = data.scene_lifetime;
    }
    for (auto& room : data.rooms) for (auto& surface : room.surfaces) {
        resolveMaterial(surface,static_cast<float>(data.simulation_time),false);
        surface.draw_surface = !(surface.authored_flags & FF_FLOATING_TRIG);
        if (surface.portal >= 0 && surface.portal < static_cast<int>(room.portals.size()))
            surface.draw_surface = surface.draw_surface && (room.portals[surface.portal].flags & PF_RENDER_FACES);
    }
    for (auto& object : data.objects) for (auto& part : object.parts) for (auto& surface : part.surfaces)
        resolveMaterial(surface,static_cast<float>(data.simulation_time),true);
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
