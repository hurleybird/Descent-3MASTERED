// Descent 3MASTERED - GPL-3.0-or-later
#include "SceneModuleContext.h"
#include "SceneCanvas.h"
#include "SceneExtraction.h"
#include <stdexcept>
#include <limits>

namespace {
// Scope ownership is per calling thread, not renderer global state. Concurrent
// simulation extraction remains unsupported; this does not imply otherwise.
thread_local d3scene::PresentationRef active;
thread_local ModuleCanvasScope* canvas = nullptr;
bool validRectangle(d3scene::Rect r) {
    return r.width > 0 && r.height > 0 &&
        int64_t(r.x)+r.width <= std::numeric_limits<int32_t>::max() &&
        int64_t(r.y)+r.height <= std::numeric_limits<int32_t>::max();
}
}
ModuleSceneScope::ModuleSceneScope(d3scene::PresentationRef frame) {
    if (!frame) throw std::invalid_argument("Module scene scope requires a published frame");
    if (active) throw std::logic_error("Nested module scene publication");
    active = std::move(frame);
}
ModuleSceneScope::~ModuleSceneScope() { active.reset(); }
d3scene::PresentationRef ModuleScenePresentation() {
    if (!active) throw std::logic_error("Module scene request outside a published render interval");
    return active;
}

ModuleCanvasScope::ModuleCanvasScope(d3scene::FrameData& target,uint64_t collection,int32_t& order,
    d3scene::Rect viewport,uint64_t identity,std::vector<d3scene::ViewImageRequest>& images,
    std::vector<d3scene::ViewImageInput>& inputs,bool inspection,uint32_t samples)
    : canvas_(target),world_(ModuleScenePresentation()),collection_(collection),identity_(identity),
      order_(order),viewport_(viewport),inspection_(inspection),samples_(samples),images_(images),inputs_(inputs) {
    if (canvas) throw std::logic_error("Nested module canvas capture");
    if (!collection || !identity || identity > std::numeric_limits<uint64_t>::max()-32 ||
        !validRectangle(viewport) || order < 0 || order > std::numeric_limits<int32_t>::max()-32 ||
        (samples != 0 && samples != 2 && samples != 4 && samples != 8) ||
        target.scene_lifetime != world_->data().scene_lifetime || target.sequence <= world_->data().sequence)
        throw std::invalid_argument("Invalid module canvas publication");
    frames_[0] = viewport;
    canvas = this;
}
ModuleCanvasScope::~ModuleCanvasScope() { canvas = nullptr; }
ModuleCanvasScope* ModuleCanvasCapture() { return canvas; }
void ModuleCanvasScope::startFrame(d3scene::Rect rectangle) {
    if (!validRectangle(rectangle) || depth_ == frames_.size())
        throw std::invalid_argument("Invalid or excessive module frame nesting");
    frames_[depth_++] = rectangle;
}
void ModuleCanvasScope::endFrame() {
    if (depth_ == 1) throw std::logic_error("Module frame stack underflow");
    --depth_;
}
void ModuleCanvasScope::complete() const {
    if (depth_ != 1) throw std::logic_error("Unbalanced module frame interval");
}
void ModuleCanvasScope::preview(d3scene::View view) {
    if (ModuleScenePresentation() != world_) throw std::logic_error("Module canvas outlived its published world interval");
    if (previews_ == 32) throw std::length_error("Module callback exceeded its preview budget");
    const auto identity = identity_+previews_;
    for (const auto& image : images_) if (image.image == identity)
        throw std::invalid_argument("Module callbacks reused a preview identity");
    for (const auto& input : inputs_) if (input.slot == identity)
        throw std::invalid_argument("Module preview reused a canvas input slot");
    const auto rectangle = frame();
    const int64_t x = int64_t(rectangle.x)-viewport_.x,y = int64_t(rectangle.y)-viewport_.y;
    if (x < std::numeric_limits<int32_t>::min() || x > std::numeric_limits<int32_t>::max() ||
        y < std::numeric_limits<int32_t>::min() || y > std::numeric_limits<int32_t>::max())
        throw std::invalid_argument("Module preview placement overflow");
    view.kind = d3scene::ViewKind::Preview; view.feedback = d3scene::NoFeedback;
    view.geometry_inspection = inspection_;
    view.target = d3scene::ViewTarget::Scene; view.history_key = identity;
    view.viewport = view.projection_anchor = {0,0,rectangle.width,rectangle.height};
    d3scene::validateView(view);
    d3scene::ViewImageRequest image; image.image = identity; image.view = std::move(view);
    image.samples = samples_;
    d3scene::validateViewImage(image);
    SceneQuadStyle style; style.uv_min = {0,1}; style.uv_max = {1,0};
    PrepareSceneQuad(canvas_,collection_,order_,{int32_t(x),int32_t(y),
        rectangle.width,rectangle.height},style);
    auto& surface = canvas_.meshes.back().surface;
    surface.view_image_slot = identity; surface.texture_alpha = false;
    inputs_.push_back({identity,identity,world_});
    images_.push_back(std::move(image));
    ++previews_;
}

void RenderModuleSceneView(object* viewer,vector* eye,int room,matrix* orientation,float zoom,bool rear) {
    if (!canvas) throw std::logic_error("Module world preview requires a source-side canvas interval");
    if (!eye || !orientation) throw std::invalid_argument("Module preview has no camera");
    const auto rectangle = canvas->frame();
    canvas->preview(MakeGameSceneView(d3scene::ViewKind::Preview,1,viewer,*eye,room,*orientation,zoom,rear,
        {0,0,rectangle.width,rectangle.height},{0,0,rectangle.width,rectangle.height}));
}
