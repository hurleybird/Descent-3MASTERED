// Descent 3MASTERED - GPL-3.0-or-later
#include "SceneModuleContext.h"
#include "SceneCanvas.h"
#include "SceneExtraction.h"
#include "../renderer/SceneGeometry.h"
#include "renderer.h"
#include "hud.h"
#include <algorithm>
#include <cmath>
#include <cstring>
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
int narrow(int64_t value) {
    if (value < std::numeric_limits<int32_t>::min() || value > std::numeric_limits<int32_t>::max())
        throw std::invalid_argument("Module canvas coordinate overflow");
    return int(value);
}
d3scene::Vec3 rgb(uint32_t color) { return {((color>>16)&255)/255.f,((color>>8)&255)/255.f,(color&255)/255.f}; }
}
ModuleSceneScope::ModuleSceneScope(d3scene::PresentationRef frame) {
    if (!frame) throw std::invalid_argument("Module scene scope requires a published frame");
    if (active) throw std::logic_error("Nested module scene publication");
    active = std::move(frame);
}
ModuleSceneScope::~ModuleSceneScope() { active.reset(); }
uint64_t AllocateModuleCanvasIdentity() {
    static uint64_t next = 0x4d43410000000000ull;
    if (next >= 0x4d4341ffffffffc0ull) throw std::overflow_error("Module canvas identities exhausted");
    const auto result = next; next += 64; return result;
}
d3scene::PresentationRef ModuleScenePresentation() {
    if (!active) throw std::logic_error("Module scene request outside a published render interval");
    return active;
}

ModuleCanvasScope::ModuleCanvasScope(d3scene::FrameData& target,uint64_t collection,int32_t& order,
    d3scene::Rect viewport,uint64_t identity,std::vector<d3scene::ViewImageRequest>& images,
    std::vector<d3scene::ViewImageInput>& inputs,bool inspection,uint32_t samples,const SceneTextStyle& hud_style)
    : canvas_(target),world_(ModuleScenePresentation()),collection_(collection),identity_(identity),
      order_(order),viewport_(viewport),inspection_(inspection),samples_(samples),images_(images),inputs_(inputs) {
    if (canvas) throw std::logic_error("Nested module canvas capture");
    if (!collection || !identity || identity > std::numeric_limits<uint64_t>::max()-32 ||
        !validRectangle(viewport) || order < 0 || order > std::numeric_limits<int32_t>::max()-32 ||
        (samples != 0 && samples != 2 && samples != 4 && samples != 8) ||
        target.scene_lifetime != world_->data().scene_lifetime || target.sequence <= world_->data().sequence)
        throw std::invalid_argument("Invalid module canvas publication");
    frames_[0] = viewport;
    hud_style_ = hud_style; hud_style_.texture_filter = d3scene::TextureFilter::Nearest;
    text_style_ = hud_style_; text_style_.scale = 1; text_style_.color = {};
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
void ModuleCanvasScope::complete() {
    if (depth_ != 1) throw std::logic_error("Unbalanced module frame interval");
    flushText();
}
void ModuleCanvasScope::command() {
    if (ModuleScenePresentation() != world_) throw std::logic_error("Module canvas outlived its world interval");
    if (commands_ == 4096) throw std::length_error("Module callback exceeded its canvas command budget");
    ++commands_;
}
void ModuleCanvasScope::setTextColor(uint32_t color) { text_style_.color = rgb(color); }
int ModuleCanvasScope::textWidth(const char* value) const { return MeasureSceneText(value,text_style_).width; }
int ModuleCanvasScope::hudTextWidth(const char* value) const {
    return int(MeasureSceneText(value,hud_style_).width*float(DEFAULT_HUD_WIDTH)/viewport_.width+.5f);
}
int ModuleCanvasScope::hudTextHeight(const char* value) const {
    return int(MeasureSceneText(value,hud_style_).height*float(DEFAULT_HUD_HEIGHT)/viewport_.height+.5f);
}
void ModuleCanvasScope::queueText(int x,int y,const char* value,const SceneTextStyle& source,bool centered) {
    if (!value) throw std::invalid_argument("Null module text");
    auto style = source;
    const auto length = std::strlen(value);
    if (length >= text_bytes_.size()) throw std::length_error("Module string exceeds text arena");
    if (text_count_ == text_records_.size() || length+1 > text_bytes_.size()-text_used_) flushText();
    command();
    const auto rectangle = frame();
    const int dx = narrow(int64_t(rectangle.x)-viewport_.x),dy = narrow(int64_t(rectangle.y)-viewport_.y);
    style.clip = {dx,dy,rectangle.width,rectangle.height};
    const auto extent = MeasureSceneText(value,style);
    const int left = narrow(int64_t(dx)+x+(centered ? (rectangle.width-extent.width)/2 : 0));
    const int top = narrow(int64_t(dy)+y);
    narrow(int64_t(left)+extent.width); narrow(int64_t(top)+extent.height);
    text_records_[text_count_++] = {left,top,uint32_t(text_used_),style};
    std::memcpy(text_bytes_.data()+text_used_,value,length+1); text_used_ += length+1;
}
void ModuleCanvasScope::text(int x,int y,const char* value,bool centered) { queueText(x,y,value,text_style_,centered); }
void ModuleCanvasScope::flushText() {
    for (size_t i = 0; i < text_count_; ++i) {
        const auto& text = text_records_[i];
        PrepareSceneText(canvas_,collection_,order_,text.x,text.y,text_bytes_.data()+text.offset,text.style);
    }
    text_used_ = text_count_ = 0;
    // Match grtext_Flush/Reset without changing any global text/renderer state.
    text_style_.alpha = 1; text_style_.scale = 1; text_style_.color = {}; text_style_.additive = false;
}
void ModuleCanvasScope::hudText(uint32_t color,uint8_t alpha,int saturation,int x,int y,const char* value,bool centered,bool screen_space) {
    if (saturation < 0 || saturation > 8) throw std::invalid_argument("Invalid module text saturation");
    text_style_ = hud_style_; text_style_.color = rgb(color); text_style_.alpha = alpha/255.f;
    text_style_.additive = saturation != 0;
    const auto style = text_style_;
    const int dx = centered ? 0 : screen_space ? x : int(x*float(viewport_.width)/DEFAULT_HUD_WIDTH);
    const int dy = screen_space ? y : int(y*float(viewport_.height)/DEFAULT_HUD_HEIGHT);
    for (int i = 0; i <= saturation; ++i) queueText(dx,dy,value,style,centered);
}
void ModuleCanvasScope::setAlphaType(int type) {
    if (type != AT_ALWAYS && type != AT_CONSTANT && type != AT_TEXTURE && type != AT_CONSTANT_TEXTURE && type != AT_SATURATE_TEXTURE)
        throw std::invalid_argument("Unsupported module canvas alpha mode");
    alpha_type_ = type;
}
void ModuleCanvasScope::setLighting(int lighting) {
    if (lighting != LS_NONE && lighting != LS_FLAT_GOURAUD && lighting != LS_GOURAUD)
        throw std::invalid_argument("Unsupported module canvas lighting");
    lighting_ = lighting;
}
void ModuleCanvasScope::quad(d3scene::Rect rectangle,SceneQuadStyle style,bool texture_alpha) {
    command();
    if (rectangle.width <= 0 || rectangle.height <= 0) return;
    const auto current = frame();
    const int64_t right = int64_t(rectangle.x)+rectangle.width,bottom = int64_t(rectangle.y)+rectangle.height;
    const int left = (std::max)(0,rectangle.x),top = (std::max)(0,rectangle.y);
    const int clipped_right = int((std::min)(int64_t(current.width),right));
    const int clipped_bottom = int((std::min)(int64_t(current.height),bottom));
    if (clipped_right <= left || clipped_bottom <= top) return;
    const auto uv = [&](int x,int y) { return d3scene::Vec2{
        style.uv_min.x+(style.uv_max.x-style.uv_min.x)*float(int64_t(x)-rectangle.x)/rectangle.width,
        style.uv_min.y+(style.uv_max.y-style.uv_min.y)*float(int64_t(y)-rectangle.y)/rectangle.height}; };
    const auto minimum = uv(left,top),maximum = uv(clipped_right,clipped_bottom);
    style.uv_min = minimum; style.uv_max = maximum;
    PrepareSceneQuad(canvas_,collection_,order_,{narrow(int64_t(current.x)-viewport_.x+left),
        narrow(int64_t(current.y)-viewport_.y+top),clipped_right-left,clipped_bottom-top},style);
    auto& surface = canvas_.meshes.back().surface;
    surface.texture_alpha = texture_alpha; surface.texture_wrap = wrap_;
}
void ModuleCanvasScope::bitmap(d3scene::Rect rectangle,int bitmap,d3scene::Vec2 uv0,d3scene::Vec2 uv1,int color,const float* alphas) {
    SceneQuadStyle style; style.bitmap = bitmap; style.uv_min = uv0; style.uv_max = uv1;
    style.texture_filter = filtering_;
    style.alpha = alpha_type_ == AT_ALWAYS || alpha_type_ == AT_TEXTURE ? 1 : raster_alpha_/255.f;
    style.additive = alpha_type_ == AT_SATURATE_TEXTURE;
    if (color != -1 && lighting_ != LS_NONE) style.color = rgb(uint32_t(color));
    // The supported modes do not include ATF_VERTEX. Like the raster API,
    // ignore its optional corner array unless a vertex-alpha mode is selected.
    (void)alphas;
    quad(rectangle,style,alpha_type_ != AT_ALWAYS && alpha_type_ != AT_CONSTANT);
}
void ModuleCanvasScope::hudQuad(int x,int y,int w,int h,int bitmap,d3scene::Vec2 uv0,d3scene::Vec2 uv1,uint8_t alpha,int saturation) {
    if (saturation < 0 || saturation > 8) throw std::invalid_argument("Invalid module quad saturation");
    raster_alpha_ = alpha; alpha_type_ = saturation ? AT_SATURATE_TEXTURE : AT_CONSTANT_TEXTURE; lighting_ = LS_NONE; wrap_ = false;
    const float sx = float(viewport_.width)/DEFAULT_HUD_WIDTH,sy = float(viewport_.height)/DEFAULT_HUD_HEIGHT;
    for (int i = 0; i <= saturation; ++i)
        this->bitmap({int(x*sx),int(y*sy),int(w*sx),int(h*sy)},bitmap,uv0,uv1);
}
void ModuleCanvasScope::fill(d3scene::Rect rectangle,uint32_t color) {
    SceneQuadStyle style; style.color = rgb(color); style.alpha = raster_alpha_/255.f;
    style.additive = alpha_type_ == AT_SATURATE_TEXTURE;
    quad(rectangle,style,false);
}
void ModuleCanvasScope::line(int x,int y,int right,int bottom) {
    command();
    const auto current = frame();
    double t0 = 0,t1 = 1,dx = double(right)-x,dy = double(bottom)-y;
    const auto clip = [&](double p,double q) {
        if (p == 0) return q >= 0;
        const double t = q/p;
        if (p < 0) { if (t > t1) return false; t0 = (std::max)(t0,t); }
        else { if (t < t0) return false; t1 = (std::min)(t1,t); }
        return true;
    };
    if (!clip(-dx,x) || !clip(dx,current.width-1.-x) || !clip(-dy,y) || !clip(dy,current.height-1.-y)) return;
    const float ox = float(int64_t(current.x)-viewport_.x),oy = float(int64_t(current.y)-viewport_.y);
    const d3scene::Vec3 a{float(x+dx*t0)+ox,float(y+dy*t0)+oy,.5f},b{float(x+dx*t1)+ox,float(y+dy*t1)+oy,.5f};
    if (a.x == b.x && a.y == b.y) return;
    d3scene::PreparedMesh mesh; mesh.asset = d3scene::assets().strokeMesh(); mesh.collection = collection_;
    mesh.world = d3scene::axisBillboardPlacement(a,b,0); mesh.raster_stroke_width = 1;
    mesh.surface.composition_order = order_++; mesh.surface.unlit = mesh.surface.two_sided = true;
    mesh.surface.medium_transport = false; mesh.surface.depth = d3scene::DepthMode::Disabled;
    mesh.surface.blend = d3scene::BlendMode::Alpha; mesh.surface.alpha = raster_alpha_/255.f;
    mesh.surface.color_modulation = rgb(flat_color_);
    canvas_.meshes.push_back(std::move(mesh));
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
