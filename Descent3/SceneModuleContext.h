// Descent 3MASTERED - GPL-3.0-or-later
#pragma once
#include "SceneCanvas.h"
#include <array>

// The legacy module ABI cannot carry a presentation argument. Its translator
// borrows the game's published frame only for the enclosing render interval.
// It is never allowed to extract a second frame or retain an implicit last frame.
class ModuleSceneScope final {
public:
    explicit ModuleSceneScope(d3scene::PresentationRef);
    ~ModuleSceneScope();
    ModuleSceneScope(const ModuleSceneScope&) = delete;
    ModuleSceneScope& operator=(const ModuleSceneScope&) = delete;
};
d3scene::PresentationRef ModuleScenePresentation();
uint64_t AllocateModuleCanvasIdentity();
uint64_t GameModuleCanvasIdentity(); // zero when no game module owns HUD callbacks

// Source-side canvas capture for one callback. Only values and owned world
// references escape it; no renderer state, source pointers or callbacks do.
class ModuleCanvasScope final {
public:
    ModuleCanvasScope(d3scene::FrameData&,uint64_t collection,int32_t& order,
        d3scene::Rect viewport,uint64_t preview_identity,
        std::vector<d3scene::ViewImageRequest>& images,std::vector<d3scene::ViewImageInput>& inputs,
        bool inspection = false,uint32_t samples = 0,const SceneTextStyle& hud_style = {});
    ~ModuleCanvasScope();
    ModuleCanvasScope(const ModuleCanvasScope&) = delete;
    ModuleCanvasScope& operator=(const ModuleCanvasScope&) = delete;
    void startFrame(d3scene::Rect);
    void endFrame();
    d3scene::Rect frame() const { return frames_[depth_-1]; }
    void preview(d3scene::View);
    void complete();
    void flushText();
    void text(int x,int y,const char*,bool centered = false);
    int textWidth(const char*) const;
    void hudText(uint32_t color,uint8_t alpha,int saturation,int x,int y,const char*,bool centered = false,bool screen_space = false);
    int hudTextWidth(const char*) const;
    int hudTextHeight(const char*) const;
    void hudQuad(int x,int y,int w,int h,int bitmap,d3scene::Vec2 uv0,d3scene::Vec2 uv1,uint8_t alpha,int saturation);
    void bitmap(d3scene::Rect,int bitmap,d3scene::Vec2 uv0,d3scene::Vec2 uv1,int color = -1,const float* alphas = nullptr);
    void fill(d3scene::Rect,uint32_t color);
    void line(int x,int y,int right,int bottom);
    void setTextColor(uint32_t color);
    void setTextAlpha(uint8_t alpha) { text_style_.alpha = alpha/255.f; }
    uint8_t textAlpha() const { return uint8_t(text_style_.alpha*255+.5f); }
    void setTextFont(int font) { text_style_.font = font; }
    int textFont() const { return text_style_.font; }
    void setFlatColor(uint32_t color) { flat_color_ = color; }
    void setAlphaValue(uint8_t alpha) { raster_alpha_ = alpha; }
    void setAlphaType(int);
    void setLighting(int);
    void setFiltering(bool filtering) { filtering_ = filtering ? d3scene::TextureFilter::Linear : d3scene::TextureFilter::Nearest; }
    void setWrap(bool repeat) { wrap_ = repeat; }
private:
    void quad(d3scene::Rect,SceneQuadStyle,bool texture_alpha = true);
    void queueText(int,int,const char*,const SceneTextStyle&,bool);
    void command();
    struct TextRecord { int x = 0,y = 0; uint32_t offset = 0; SceneTextStyle style; };
    std::array<char,16384> text_bytes_{};
    std::array<TextRecord,512> text_records_{};
    size_t text_used_ = 0,text_count_ = 0;
    uint32_t commands_ = 0,flat_color_ = 0xffffff;
    uint8_t raster_alpha_ = 255;
    int alpha_type_ = 3,lighting_ = 0;
    bool wrap_ = false;
    d3scene::TextureFilter filtering_ = d3scene::TextureFilter::Default;
    SceneTextStyle hud_style_,text_style_;
    d3scene::FrameData& canvas_;
    d3scene::PresentationRef world_;
    uint64_t collection_,identity_;
    int32_t& order_;
    d3scene::Rect viewport_;
    std::array<d3scene::Rect,16> frames_{};
    size_t depth_ = 1,previews_ = 0;
    bool inspection_ = false;
    uint32_t samples_ = 0;
    std::vector<d3scene::ViewImageRequest>& images_;
    std::vector<d3scene::ViewImageInput>& inputs_;
};
ModuleCanvasScope* ModuleCanvasCapture();
struct object;
void ExerciseModuleSceneViewAPI(object*,bool resized = false);
