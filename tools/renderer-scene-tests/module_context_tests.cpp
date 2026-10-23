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
    {
        ModuleSceneScope world_scope(first);
        d3scene::FrameData output; output.sequence = 4; output.scene_lifetime = data.scene_lifetime;
        std::vector<d3scene::ViewImageRequest> requests;
        std::vector<d3scene::ViewImageInput> sources;
        int32_t next_order = 0;
        SceneTextStyle text_style; text_style.font = 0;
        ModuleCanvasScope capture(output,70,next_order,{100,80,640,480},2000,requests,sources,true,0,text_style);
        capture.setTextColor(0x00ff00); capture.setTextAlpha(128);
        char text[] = "AB";
        capture.text(1,2,text); text[0] = 'Z';
        check(output.meshes.empty(),"Module Printf emitted text before its flush boundary");
        capture.setTextColor(0x0000ff); capture.setTextAlpha(64);
        capture.fill({0,0,80,20},0xff0000);
        capture.flushText();
        check(output.meshes.size() == 2 && output.meshes[0].surface.composition_order == 0 &&
            output.meshes[1].surface.composition_order == 1 && output.meshes[1].surface.bitmap == 0 &&
            output.meshes[1].surface.alpha == 128/255.f && output.meshes[1].surface.color_modulation.y == 1 &&
            capture.textAlpha() == 255 && capture.textFont() == 0,
            "Module text borrowed later bytes/style or lost flush/reset ordering");
        check(capture.hudTextWidth("AB") == 12 && capture.hudTextHeight("AB") == 9,
            "Module HUD metrics lost their virtual coordinate space");
        capture.setFiltering(false);
        capture.hudQuad(10,20,30,40,5,{0,0},{1,1},100,0);
        check(output.meshes.back().vertices[0].x == 10 && output.meshes.back().vertices[0].y == 20 &&
            output.meshes.back().surface.alpha == 100/255.f &&
            output.meshes.back().surface.texture_filter == d3scene::TextureFilter::Nearest,
            "Module HUD bitmap lost scale, alpha or captured filtering");
        capture.startFrame({110,100,4,4});
        capture.setTextColor(0xffffff); capture.text(-2,-1,"A"); capture.flushText();
        const auto& clipped = output.meshes.back();
        const auto& clipped_positions = clipped.vertices.empty() ? clipped.asset->geometry().vertices : clipped.vertices;
        check(clipped_positions[0].x == 10 && clipped_positions[0].y == 20 &&
            clipped_positions[2].x == 14 && clipped_positions[2].y == 24,
            "Nested module text escaped its captured clipping rectangle");
        capture.bitmap({-2,-1,8,8},5,{0,0},{1,1});
        const auto& bitmap = output.meshes.back();
        check(bitmap.vertices[0].x == 10 && bitmap.vertices[0].y == 20 &&
            bitmap.texture_uvs[0].x == .25f && bitmap.texture_uvs[0].y == .125f &&
            bitmap.texture_uvs[2].x == .75f && bitmap.texture_uvs[2].y == .625f,
            "Nested module bitmap clipping failed to crop its texture coordinates");
        capture.setFlatColor(0xffffff); capture.line(-10,2,10,2);
        check(output.meshes.back().raster_stroke_width == 1 && output.meshes.back().surface.depth == d3scene::DepthMode::Disabled &&
            !output.meshes.back().surface.medium_transport,"Module line reused a legacy GPU draw or world optics");
        capture.endFrame(); capture.complete();
        const auto count = output.meshes.size();
        rejected = false;
        try { capture.setAlphaType(99); } catch (const std::invalid_argument&) { rejected = true; }
        check(rejected && output.meshes.size() == count,"Unsupported module alpha mode silently changed output");
    }
    return checks;
}
