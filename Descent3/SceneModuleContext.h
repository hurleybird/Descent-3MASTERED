// Descent 3MASTERED - GPL-3.0-or-later
#pragma once
#include "../renderer/Scene.h"
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

// Source-side canvas capture for one callback. Only values and owned world
// references escape it; no renderer state, source pointers or callbacks do.
class ModuleCanvasScope final {
public:
    ModuleCanvasScope(d3scene::FrameData&,uint64_t collection,int32_t& order,
        d3scene::Rect viewport,uint64_t preview_identity,
        std::vector<d3scene::ViewImageRequest>& images,std::vector<d3scene::ViewImageInput>& inputs,
        bool inspection = false,uint32_t samples = 0);
    ~ModuleCanvasScope();
    ModuleCanvasScope(const ModuleCanvasScope&) = delete;
    ModuleCanvasScope& operator=(const ModuleCanvasScope&) = delete;
    void startFrame(d3scene::Rect);
    void endFrame();
    d3scene::Rect frame() const { return frames_[depth_-1]; }
    void preview(d3scene::View);
    void complete() const;
private:
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
