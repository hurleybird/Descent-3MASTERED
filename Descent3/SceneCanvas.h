// Descent 3MASTERED - GPL-3.0-or-later
#pragma once
#include "../renderer/Scene.h"

enum class HUDViewMode { Hidden, Ship, Guided, Zoom, Rear, Letterbox, Observer };
enum class HUDAimMode { Hidden, Ship, Guided, Zoom };
struct HUDCanvasRequest {
    d3scene::Rect world, display;
    HUDViewMode mode = HUDViewMode::Hidden;
    bool centered_four_three = true, show_ship_reticle = true;
    uint16_t item_mask = 0;
    bool aim_on_target = false;
    float zoom_distance = 0;
};
struct HUDCanvasLayout {
    d3scene::Rect primary, display;
    HUDAimMode aim = HUDAimMode::Hidden;
    uint16_t item_mask = 0;
    bool auxiliary = false;
    bool aim_on_target = false;
    float zoom_distance = 0;
};
HUDCanvasLayout CompileHUDCanvasLayout(const HUDCanvasRequest&);
HUDCanvasLayout CaptureGameHUDLayout();

struct SceneTextStyle {
    int font = -1;
    float scale = 1, alpha = 1;
    d3scene::Vec3 color{1,1,1};
    bool additive = false;
    int tab_columns = 4;
};
struct SceneTextExtent { int width = 0, height = 0; };
SceneTextExtent MeasureSceneText(const char*,const SceneTextStyle&);
SceneTextStyle MakeHUDTextStyle(d3scene::Rect viewport);
void PrepareHUDMessageCanvas(d3scene::FrameData&,uint64_t collection,d3scene::Rect viewport,bool auxiliary,bool advance);
struct tHUDItem;
bool PrepareHUDItemCanvas(d3scene::FrameData&,uint64_t collection,d3scene::Rect viewport,
    tHUDItem*,int count,uint16_t mask,int small_filter,bool advance,bool deferred_score = false);
struct SceneQuadStyle {
    int bitmap = -1;
    float alpha = 1;
    d3scene::Vec3 color{1,1,1};
    d3scene::Vec2 uv_min{0,0}, uv_max{1,1};
    bool additive = false;
};
void PrepareSceneQuad(d3scene::FrameData&,uint64_t collection,int32_t& next_order,
    d3scene::Rect rectangle,const SceneQuadStyle&);
struct FrameScreenBlend;
void PrepareFrameBlendCanvas(d3scene::FrameData&,uint64_t collection,d3scene::Rect viewport,
    const std::vector<FrameScreenBlend>&,bool inspection);
d3scene::View MakeSceneCanvasView(uint64_t collection,d3scene::Rect viewport,
    d3scene::ViewTarget target,bool inspection);
// Plain legacy-byte text in logical top-left canvas coordinates. Atlas lookup
// and layout happen during publication; the renderer receives ordinary quads.
// Images are frozen by the same image publication pass as other scene content.
void PrepareSceneText(d3scene::FrameData&,uint64_t collection,int32_t& next_order,
    int x,int y,const char* text,const SceneTextStyle&);
