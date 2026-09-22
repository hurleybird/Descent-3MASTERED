// Descent 3MASTERED - GPL-3.0-or-later
#pragma once
#include "../renderer/Scene.h"

struct SceneTextStyle {
    int font = -1;
    float scale = 1, alpha = 1;
    d3scene::Vec3 color{1,1,1};
    bool additive = false;
};
// Plain legacy-byte text in logical top-left canvas coordinates. Atlas lookup
// and layout happen during publication; the renderer receives ordinary quads.
// Images are frozen by the same image publication pass as other scene content.
void PrepareSceneText(d3scene::FrameData&,uint64_t collection,int32_t& next_order,
    int x,int y,const char* text,const SceneTextStyle&);
