// Descent 3MASTERED - GPL-3.0-or-later
#include "../../Descent3/SceneModuleContext.h"
#include "../../Descent3/SceneExtraction.h"
#include <stdexcept>
#include <type_traits>

int runModuleContextTests() {
    int checks = 0;
    const auto check = [&](bool value,const char* message) { ++checks; if (!value) throw std::runtime_error(message); };
    const auto noFrame = [&] {
        bool rejected = false;
        try { ModuleScenePresentation(); } catch (const std::logic_error&) { rejected = true; }
        check(rejected,"Module request retained an implicit last frame outside its interval");
    };
    noFrame();
    bool rejected = false;
    try { ModuleSceneScope invalid(nullptr); } catch (const std::invalid_argument&) { rejected = true; }
    check(rejected,"Empty module frame accepted");
    d3scene::FrameData data; data.sequence = 1; data.scene_lifetime = d3scene::assets().lifetime();
    auto first = d3scene::Presentation::freeze(data);
    ++data.sequence;
    auto second = d3scene::Presentation::freeze(data);
    d3scene::PresentationRef held;
    {
        ModuleSceneScope scope(first);
        held = ModuleScenePresentation();
        check(held == first && ModuleScenePresentation() == held,"Additional module views did not reuse the published frame");
        rejected = false;
        try { ModuleSceneScope nested(second); } catch (const std::logic_error&) { rejected = true; }
        check(rejected && ModuleScenePresentation() == first,"Nested publication replaced the active module frame");
    }
    noFrame();
    {
        ModuleSceneScope scope(second);
        check(ModuleScenePresentation() == second && held->data().sequence == 1,
            "A new render interval changed an already held frame");
    }
    noFrame();
    try { ModuleSceneScope scope(first); throw std::runtime_error("fixture unwind"); }
    catch (const std::runtime_error&) {}
    noFrame();
    using WorldViewABI = void (*)(object*,vector*,int,matrix*,float,bool);
    static_assert(std::is_same<decltype(&RenderModuleSceneView),WorldViewABI>::value,
        "Module world export must match the shipped six-argument ABI");
    d3scene::FrameData canvas_data; canvas_data.sequence = 3; canvas_data.scene_lifetime = data.scene_lifetime;
    std::vector<d3scene::ViewImageRequest> images;
    std::vector<d3scene::ViewImageInput> inputs;
    int32_t order = 0;
    {
        ModuleSceneScope world_scope(first);
        ModuleCanvasScope capture(canvas_data,50,order,{100,80,800,600},900,images,inputs,true,4);
        check(ModuleCanvasCapture() == &capture,"Source-side module canvas did not own its interval");
        capture.startFrame({160,120,200,100});
        vector eye{1,2,3}; matrix facing{};
        facing.rvec.x = facing.uvec.y = facing.fvec.z = 1;
        WorldViewABI world_view = &RenderModuleSceneView;
        world_view(nullptr,&eye,-1,&facing,1,false);
        eye.x = 7;
        capture.endFrame(); capture.startFrame({400,200,160,120});
        world_view(nullptr,&eye,-1,&facing,1,true);
        eye.x = 999;
        check(images.size() == 2 && images[0].image == 900 && images[1].image == 901 &&
            images[0].view.history_key != images[1].view.history_key &&
            images[0].view.viewport.width == 200 && images[1].view.viewport.height == 120 &&
            images[0].samples == 4 && images[1].samples == 4 &&
            images[0].view.camera.position.x == 1 && images[1].view.camera.position.x == 7 &&
            images[0].view.kind == d3scene::ViewKind::Preview && images[0].view.feedback == d3scene::NoFeedback,
            "Module previews lost independent identity, camera ownership, rectangle or feedback exclusion");
        check(canvas_data.meshes.size() == 2 && order == 2 &&
            canvas_data.meshes[0].vertices[0].x == 60 && canvas_data.meshes[0].vertices[0].y == 40 &&
            canvas_data.meshes[1].vertices[0].x == 300 &&
            !canvas_data.meshes[0].surface.texture_alpha && !canvas_data.meshes[0].surface.medium_transport &&
            inputs[0].producer == first && inputs[1].producer == first,
            "Module preview failed to preserve offset/order or exact frozen-world input");
        rejected = false;
        try { capture.complete(); } catch (const std::logic_error&) { rejected = true; }
        check(rejected,"Unbalanced module frame interval accepted");
        capture.endFrame(); capture.complete();
        rejected = false;
        try { capture.endFrame(); } catch (const std::logic_error&) { rejected = true; }
        check(rejected,"Module frame stack underflow accepted");
        rejected = false;
        try { ModuleCanvasScope nested(canvas_data,50,order,{0,0,10,10},950,images,inputs); }
        catch (const std::logic_error&) { rejected = true; }
        check(rejected && ModuleCanvasCapture() == &capture,"Nested module capture replaced the active source canvas");
        for (int i = 1; i < 16; ++i) capture.startFrame({0,0,16,16});
        rejected = false;
        try { capture.startFrame({0,0,16,16}); } catch (const std::invalid_argument&) { rejected = true; }
        check(rejected,"Module source frame nesting was unbounded");
        for (int i = 1; i < 16; ++i) capture.endFrame();
        for (int i = 2; i < 32; ++i) capture.preview(images[0].view);
        const auto mesh_count = canvas_data.meshes.size();
        rejected = false;
        try { capture.preview(images[0].view); } catch (const std::length_error&) { rejected = true; }
        check(rejected && canvas_data.meshes.size() == mesh_count && images.size() == 32 && inputs.size() == 32,
            "Module preview budget failed before mutating canvas output");
        capture.complete();
    }
    check(!ModuleCanvasCapture(),"Module canvas leaked beyond its source callback");
    rejected = false;
    try { RenderModuleSceneView(nullptr,nullptr,-1,nullptr,1,false); }
    catch (const std::logic_error&) { rejected = true; }
    check(rejected,"Out-of-scope module ABI request attempted an implicit world draw");
    return checks;
}
