// Descent 3MASTERED - GPL-3.0-or-later
#pragma once
#include "../renderer/Scene.h"

struct SceneTextStyle {
    int font = -1;
    float scale = 1, alpha = 1;
    d3scene::Vec3 color{1,1,1};
    bool additive = false;
};
struct SceneQuadStyle {
    int bitmap = -1;
    float alpha = 1;
    d3scene::Vec3 color{1,1,1};
    d3scene::Vec2 uv_min{0,0}, uv_max{1,1};
    bool additive = false;
};
void PrepareSceneQuad(d3scene::FrameData&,uint64_t collection,int32_t& next_order,
    d3scene::Rect rectangle,const SceneQuadStyle&);
d3scene::View MakeSceneCanvasView(uint64_t collection,d3scene::Rect viewport,
    d3scene::ViewTarget target,bool inspection);
// Plain legacy-byte text in logical top-left canvas coordinates. Atlas lookup
// and layout happen during publication; the renderer receives ordinary quads.
// Images are frozen by the same image publication pass as other scene content.
void PrepareSceneText(d3scene::FrameData&,uint64_t collection,int32_t& next_order,
    int x,int y,const char* text,const SceneTextStyle&);
