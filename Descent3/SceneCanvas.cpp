// Descent 3MASTERED - GPL-3.0-or-later
#include "SceneCanvas.h"
#include "../renderer/SceneGeometry.h"
#include "grtext.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

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
            x = (x+tab)/tab*tab; continue;
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
    d3scene::PreparedMesh run;
    const auto flush = [&] {
        if (run.vertices.empty()) return;
        const auto count = static_cast<uint32_t>(run.vertices.size()/4);
        // Game mesh slots 257..384 are canonical quad-count templates. UVs and
        // positions are frame attributes, never new assets for changing text.
        run.asset = d3scene::assets().publishMesh(256+count,d3scene::unitQuadBatch(count));
        frame.meshes.push_back(std::move(run)); run = {};
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
                    surface.composition_order = next_order++;
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
}
