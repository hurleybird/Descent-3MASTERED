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

d3scene::PresentationRef PrepareGamePresentation(bool advance_history) {
    const auto source = ExtractGamePresentation(Gametime, Frametime, Game_paused, advance_history);
    auto data = source->data();
    static uint64_t material_lifetime = 0;
    if (material_lifetime != data.scene_lifetime) {
        ResetSplitSpecularTexturePairCache();
        material_lifetime = data.scene_lifetime;
    }
    for (auto& room : data.rooms) {
        for (auto& surface : room.surfaces) {
            if (surface.texture < 0 || surface.texture >= MAX_TEXTURES || !GameTextures[surface.texture].used)
                Error("Scene material references missing texture %d", surface.texture);
            int base_texture = surface.texture;
            const auto& authored = GameTextures[surface.texture];
            if ((surface.authored_flags & FF_DESTROYED) && (authored.flags & TF_DESTROYABLE))
                base_texture = authored.destroy_handle;
            else if (SplitSpecularTexturePathEnabled()) {
                const auto pair = FindSplitSpecularTexturePair(surface.texture);
                if (pair.valid) base_texture = pair.base_tmap;
            }
            if (base_texture < 0 || base_texture >= MAX_TEXTURES || !GameTextures[base_texture].used)
                Error("Scene material references missing base texture %d", base_texture);
            const auto& texture = GameTextures[base_texture];
            surface.bitmap = GetTextureBitmap(base_texture, 0);
            surface.u_offset = authored.slide_u != 0 ? std::fmod(Gametime / authored.slide_u, 1.f) : 0;
            surface.v_offset = authored.slide_v != 0 ? std::fmod(Gametime / authored.slide_v, 1.f) : 0;
            surface.alpha = texture.alpha;
            surface.additive = (texture.flags & TF_SATURATE) != 0;
            surface.texture_alpha = (texture.flags & TF_TMAP2) != 0 && bm_format(surface.bitmap) != BITMAP_FORMAT_4444;
            surface.draw_surface = !(surface.authored_flags & FF_FLOATING_TRIG);
            if (surface.portal >= 0 && surface.portal < static_cast<int>(room.portals.size()))
                surface.draw_surface = surface.draw_surface && (room.portals[surface.portal].flags & PF_RENDER_FACES);
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
    // Pending domains deliberately remain set: this is not a completed material
    // compiler or a claim of full scene support.
    return d3scene::Presentation::freeze(data);
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
