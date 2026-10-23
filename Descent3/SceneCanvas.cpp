// Descent 3MASTERED - GPL-3.0-or-later
#include "SceneCanvas.h"
#include "gameevent.h"
#include "../renderer/SceneGeometry.h"
#include "grtext.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <string>

HUDCanvasLayout CompileHUDCanvasLayout(const HUDCanvasRequest& request) {
    if (request.world.width <= 0 || request.world.height <= 0 || request.display.width <= 0 || request.display.height <= 0)
        throw std::invalid_argument("Invalid HUD canvas extent");
    HUDCanvasLayout layout; layout.primary = request.world; layout.display = request.display;
    if (!std::isfinite(request.zoom_distance)) throw std::invalid_argument("Invalid HUD range");
    layout.aim_on_target = request.aim_on_target; layout.zoom_distance = request.zoom_distance;
    if (request.centered_four_three) {
        layout.primary.width = request.world.height*4/3;
        layout.primary.x += (request.world.width-layout.primary.width)/2;
    }
    switch (request.mode) {
    case HUDViewMode::Hidden: case HUDViewMode::Rear: return layout;
    case HUDViewMode::Guided: layout.aim = HUDAimMode::Guided; return layout;
    case HUDViewMode::Zoom: layout.aim = HUDAimMode::Zoom; return layout;
    case HUDViewMode::Ship:
        layout.cockpit = request.show_cockpit;
        layout.aim = request.show_ship_reticle ? HUDAimMode::Ship : HUDAimMode::Hidden; break;
    case HUDViewMode::Letterbox: case HUDViewMode::Observer: break;
    default: throw std::invalid_argument("Unknown HUD view mode");
    }
    layout.item_mask = request.item_mask;
    layout.auxiliary = float(request.world.height)/request.display.height <= .8f;
    return layout;
}

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
    quad.asset = d3scene::assets().quadMesh();
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

void PrepareFrameBlendCanvas(d3scene::FrameData& frame,uint64_t collection,d3scene::Rect viewport,
    const std::vector<FrameScreenBlend>& blends,bool inspection) {
    if (blends.empty()) return;
    const auto asset = d3scene::assets().quadMesh();
    int32_t order = 0;
    for (const auto& blend : blends) {
        if (!(blend.alpha > 0)) continue;
        d3scene::PreparedMesh quad;
        quad.asset = asset; quad.collection = collection;
        // Shared canonical geometry plus a placement. No per-event vertex/UV
        // vectors are allocated each frame just to draw the same rectangle.
        quad.world.position = {viewport.width*.5f,viewport.height*.5f,.5f};
        quad.world.right = {viewport.width*.5f,0,0};
        quad.world.up = {0,-viewport.height*.5f,0};
        quad.surface.composition_order = order++;
        quad.surface.unlit = quad.surface.two_sided = true;
        quad.surface.depth = d3scene::DepthMode::Disabled;
        quad.surface.blend = d3scene::BlendMode::Alpha;
        quad.surface.alpha = blend.alpha;
        quad.surface.color_modulation = {blend.red,blend.green,blend.blue};
        frame.meshes.push_back(std::move(quad));
    }
    if (!order) return;
    d3scene::PreparedForeground foreground;
    foreground.identity = collection; foreground.phase = d3scene::ForegroundPhase::AfterMainWorld;
    foreground.layer.composition = d3scene::ForegroundComposition::InScene;
    foreground.layer.scene_views.push_back(MakeSceneCanvasView(collection,viewport,d3scene::ViewTarget::Scene,inspection));
    frame.foregrounds.push_back(std::move(foreground));
}

namespace {
template<class Emit> SceneTextExtent layoutSceneText(const char* text,const SceneTextStyle& style,int x,int y,Emit emit) {
    if (!text || style.font < 0 || !std::isfinite(style.scale) || style.scale <= 0 || style.tab_columns <= 0)
        throw std::invalid_argument("Invalid prepared text input");
    const int origin = x, top = y, spacing = 1+grfont_GetTracking(style.font);
    const int line = int(grfont_GetHeight(style.font)*style.scale)+1;
    int width = 0;
    auto color = style.color;
    const auto* end = reinterpret_cast<const unsigned char*>(text)+std::strlen(text);
    for (auto* cursor = reinterpret_cast<const unsigned char*>(text); cursor < end; ++cursor) {
        if (*cursor == GR_COLOR_CHAR) {
            if (end-cursor < 4) throw std::invalid_argument("Truncated canvas text color");
            color = {cursor[1]/255.f,cursor[2]/255.f,cursor[3]/255.f}; cursor += 3; continue;
        }
        if (*cursor == GRTEXT_FORMAT_CHAR) {
            if (end-cursor < 2) throw std::invalid_argument("Truncated canvas text position");
            x = origin+cursor[1]*GRTEXT_FORMAT_SCALAR; ++cursor; continue;
        }
        if (*cursor == '\n') { width = (std::max)(width,x-origin-spacing); x = origin; y += line; continue; }
        if (*cursor == '\t') {
            const int tab = (int(grfont_GetCharWidth(style.font,' ')*style.scale)+spacing)*style.tab_columns;
            if (tab <= 0) throw std::invalid_argument("Invalid canvas tab width");
            x = origin+(x-origin+tab)/tab*tab; continue;
        }
        tFontGlyph glyph;
        const bool found = grfont_GetGlyph(style.font,*cursor,glyph);
        if (*cursor != ' ' && found && glyph.width > 0 && glyph.height > 0) emit(glyph,x,y,color);
        x += int(grfont_GetCharWidth(style.font,*cursor)*style.scale)+spacing+
            grfont_GetKernedSpacing(style.font,*cursor,cursor[1]);
    }
    return {(std::max)(width,(std::max)(0,x-origin-spacing)),y-top+line};
}
}
SceneTextExtent MeasureSceneText(const char* text,const SceneTextStyle& style) {
    return layoutSceneText(text,style,0,0,[](const tFontGlyph&,int,int,d3scene::Vec3) {});
}
void PrepareSceneText(d3scene::FrameData& frame,uint64_t collection,int32_t& next_order,
    int x,int y,const char* text,const SceneTextStyle& style) {
    if (!text || style.font < 0 || !std::isfinite(style.scale) || style.scale <= 0 || style.tab_columns <= 0)
        throw std::invalid_argument("Invalid prepared text input");
    // The source compiler owns a bounded cache. Execution sees ordinary mesh
    // assets, not a special text path. Font edits and scene resets invalidate
    // source keys; held presentations still own their old geometry/pixels.
    struct TextEntry {
        std::string text;
        int font = -1, x = 0, y = 0, tabs = 0;
        float scale = 0;
        d3scene::Vec3 color;
        uint64_t used = 0;
        std::vector<d3scene::PreparedMesh> runs;
    };
    static thread_local std::array<TextEntry,64> cache;
    static thread_local uint64_t lifetime = 0, font_revision = 0, clock = 0;
    const auto current_lifetime = d3scene::assets().lifetime(), current_font = grfont_LayoutRevision();
    if (lifetime != current_lifetime || font_revision != current_font) {
        for (auto& entry : cache) entry = {};
        lifetime = current_lifetime; font_revision = current_font; clock = 0;
    }
    const auto emit = [&](const std::vector<d3scene::PreparedMesh>& runs) {
        for (const auto& source : runs) {
            auto mesh = source;
            mesh.collection = collection; mesh.surface.composition_order = next_order++;
            mesh.surface.alpha = style.alpha;
            mesh.surface.blend = style.additive ? d3scene::BlendMode::Additive : d3scene::BlendMode::Alpha;
            frame.meshes.push_back(std::move(mesh));
        }
    };
    const size_t length = std::strlen(text);
    const bool cacheable = length <= 128;
    size_t replacement = 0;
    if (cacheable) for (size_t i = 0; i < cache.size(); ++i) {
        auto& entry = cache[i];
        if (entry.used && entry.font == style.font && entry.x == x && entry.y == y && entry.tabs == style.tab_columns &&
            entry.scale == style.scale && entry.color.x == style.color.x && entry.color.y == style.color.y &&
            entry.color.z == style.color.z && entry.text == text) {
            entry.used = ++clock; emit(entry.runs); return;
        }
        if (entry.used < cache[replacement].used) replacement = i;
    }
    std::vector<d3scene::PreparedMesh> compiled;
    d3scene::PreparedMesh run;
    const auto flush = [&] {
        if (run.vertices.empty()) return;
        const auto count = static_cast<uint32_t>(run.vertices.size()/4);
        // Long uncached text uses canonical quad-count templates; short stable
        // layouts publish owned positions/UVs once and retain their assets.
        if (cacheable) {
            auto geometry = d3scene::unitQuadBatch(count);
            geometry.vertices = std::move(run.vertices);
            for (auto& polygon : geometry.polygons) for (auto& corner : polygon.corners) {
                const auto uv = run.texture_uvs[corner.vertex]; corner.u = uv.x; corner.v = uv.y;
            }
            run.texture_uvs.clear();
            // Source mesh slots 512..8703 belong to these 64 bounded layouts,
            // at most 128 atlas/color runs each; automap starts at 65536.
            run.asset = d3scene::assets().publishMesh(512+uint32_t(replacement)*128+uint32_t(compiled.size()),std::move(geometry));
            compiled.push_back(std::move(run));
        } else {
            run.asset = d3scene::assets().quadMesh(count);
            frame.meshes.push_back(std::move(run));
        }
        run = {};
    };
    layoutSceneText(text,style,x,y,[&](const tFontGlyph& glyph,int x,int y,d3scene::Vec3 color) {
            const int right = int(x+glyph.width*style.scale), bottom = int(y+glyph.height*style.scale);
            if (right > x && bottom > y) {
                const auto old = run.surface.color_modulation;
                if (!run.vertices.empty() && (run.surface.bitmap != glyph.bitmap || run.vertices.size() == 128*4 ||
                    old.x != color.x || old.y != color.y || old.z != color.z)) flush();
                if (run.vertices.empty()) {
                    run.collection = collection;
                    auto& surface = run.surface;
                    if (!cacheable) surface.composition_order = next_order++;
                    surface.bitmap = glyph.bitmap; surface.texture_alpha = true; surface.texture_wrap = false;
                    surface.unlit = true; surface.two_sided = true; surface.depth = d3scene::DepthMode::Disabled;
                    surface.blend = style.additive ? d3scene::BlendMode::Additive : d3scene::BlendMode::Alpha;
                    surface.alpha = style.alpha; surface.color_modulation = color;
                }
                run.vertices.insert(run.vertices.end(),{{float(x),float(y),.5f},{float(right),float(y),.5f},
                    {float(right),float(bottom),.5f},{float(x),float(bottom),.5f}});
                run.texture_uvs.insert(run.texture_uvs.end(),{{glyph.u,glyph.v},{glyph.u+glyph.uv_width,glyph.v},
                    {glyph.u+glyph.uv_width,glyph.v+glyph.uv_height},{glyph.u,glyph.v+glyph.uv_height}});
            }
    });
    flush();
    if (cacheable) {
        auto& entry = cache[replacement];
        entry.text = text; entry.font = style.font; entry.x = x; entry.y = y;
        entry.tabs = style.tab_columns; entry.scale = style.scale; entry.color = style.color;
        entry.runs = std::move(compiled); entry.used = ++clock;
        emit(entry.runs);
    }
}
