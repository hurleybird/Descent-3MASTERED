// Descent 3MASTERED - GPL-3.0-or-later
#include "../../Descent3/SceneCanvas.h"
#include "grtext.h"
#include <stdexcept>
#include <string>

// Read-only font atlas fixture. The real canvas publisher links without any
// grtext buffer, renderer state, draw function or game-window globals.
int grfont_GetCharWidth(int,int ch) { return ch == ' ' ? 3 : 6; }
int grfont_GetHeight(int) { return 8; }
int grfont_GetTracking(int) { return 0; }
int grfont_GetKernedSpacing(int,int a,int b) { return a == 'A' && b == 'B' ? -1 : 0; }
bool grfont_GetGlyph(int,int ch,tFontGlyph& glyph) {
    glyph = {}; glyph.bitmap = ch == 'Z' ? 1 : 0;
    glyph.width = grfont_GetCharWidth(0,ch); glyph.height = 8;
    glyph.uv_width = .1f; glyph.uv_height = .2f;
    return ch >= 32;
}

int runCanvasTests() {
    int checks = 0;
    const auto check = [&](bool value,const char* message) { ++checks; if (!value) throw std::runtime_error(message); };
    SceneTextStyle style; style.font = 0;
    const auto extent = MeasureSceneText("AB",style);
    check(extent.width == 12 && extent.height == 9,"Canvas text metrics lost kerning or spacing");
    check(MeasureSceneText("AB\nA",style).height == 18 && MeasureSceneText("AB\nA",style).width == 12,
        "Canvas multiline measurement is not the maximum line extent");
    check(MeasureSceneText("A\tB",style).width == 22,"Canvas tab measurement is incorrect");
    check(MeasureSceneText("\2\3A",style).width == 18,"Canvas positioning control became a glyph");
    d3scene::FrameData data; data.sequence = 1; data.scene_lifetime = d3scene::assets().lifetime();
    int32_t order = 0;
    PrepareSceneText(data,7,order,0,0,"AB",style);
    check(data.meshes.size() == 1 && data.meshes[0].vertices.size() == 8 && data.meshes[0].vertices[4].x == 6,
        "Canvas text execution disagrees with its metrics");
    const auto held = d3scene::Presentation::freeze(data);
    data.meshes.clear(); order = 0;
    PrepareSceneText(data,7,order,0,0,"A\1\x10\x20\x30" "BZ",style);
    check(data.meshes.size() == 3 && data.meshes[1].surface.color_modulation.y == 32/255.f &&
        data.meshes[1].vertices[0].x == 7 && data.meshes[2].surface.bitmap == 1,
        "Canvas color/atlas changes did not split owned glyph batches");
    check(held->data().meshes[0].vertices.size() == 8 && held->data().meshes[0].surface.color_modulation.y == 1,
        "Later text preparation mutated a held frame");
    data.meshes.clear(); order = 0;
    PrepareSceneText(data,7,order,0,0,std::string(129,'A').c_str(),style);
    check(data.meshes.size() == 2 && data.meshes[0].vertices.size() == 512 && data.meshes[1].vertices.size() == 4 && order == 2,
        "Long text exceeded canonical batch capacity or lost order");
    style.scale = 1.5f;
    check(MeasureSceneText("AB",style).width == 18 && MeasureSceneText("AB",style).height == 13,
        "Scaled text changed tracking instead of glyph metrics");
    for (const char* invalid : {"\1", "\1\2", "\2"}) {
        bool rejected = false;
        try { MeasureSceneText(invalid,style); } catch (const std::invalid_argument&) { rejected = true; }
        check(rejected,"Truncated text control accepted");
    }
    style.tab_columns = 0;
    bool rejected = false;
    try { MeasureSceneText("A",style); } catch (const std::invalid_argument&) { rejected = true; }
    check(rejected,"Zero-width tab policy accepted");
    data.meshes.clear(); order = 0;
    SceneQuadStyle quad; quad.alpha = .5f; quad.uv_min = {1,0}; quad.uv_max = {0,1};
    PrepareSceneQuad(data,7,order,{10,20,30,40},quad);
    PrepareSceneQuad(data,7,order,{0,0,0,10},quad);
    check(data.meshes.size() == 1 && data.meshes[0].vertices[2].x == 40 &&
        data.meshes[0].texture_uvs[0].x == 1 && data.meshes[0].texture_uvs[1].x == 0,
        "Canvas quad lost reflected UVs or emitted empty geometry");
    const auto canvas = MakeSceneCanvasView(7,{20,30,800,600},d3scene::ViewTarget::Display,true);
    check(canvas.viewport.x == 20 && canvas.projection_anchor.x == 0 && canvas.collection == 7 &&
        canvas.ordering == d3scene::ViewOrdering::Compositing && canvas.camera.up.y == -1,
        "Canvas inherited implicit viewport or world projection");
    data.foregrounds.push_back({7,{{},{canvas}}});
    const auto published = d3scene::Presentation::freeze(data);
    data.foregrounds[0].layer.display_views[0].viewport.x = 90;
    check(published->foreground(7) && published->foreground(7)->display_views[0].viewport.x == 20 &&
        !published->foreground(8),"Foreground placement was not owned by its presentation");
    const auto rejectForeground = [&] {
        bool failed = false;
        try { d3scene::Presentation::freeze(data); } catch (const std::exception&) { failed = true; }
        check(failed,"Invalid foreground publication accepted");
    };
    data.foregrounds.push_back(data.foregrounds.front()); rejectForeground(); data.foregrounds.pop_back();
    data.foregrounds[0].identity = 0; rejectForeground(); data.foregrounds[0].identity = 7;
    auto& display = data.foregrounds[0].layer.display_views[0];
    display.target = d3scene::ViewTarget::Scene; rejectForeground(); display.target = d3scene::ViewTarget::Display;
    display.feedback = d3scene::Automap; rejectForeground(); display.feedback = d3scene::NoFeedback;
    display.viewport.width = 0; rejectForeground();
    return checks;
}
