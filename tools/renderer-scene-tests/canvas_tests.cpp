// Descent 3MASTERED - GPL-3.0-or-later
#include "../../Descent3/SceneCanvas.h"
#include "grtext.h"
#include "../../renderer/SceneGeometry.h"
#include "../../renderer/SceneCompositing.h"
#include "../../Descent3/gameevent.h"
#include <stdexcept>
#include <string>

// Read-only font atlas fixture. The real canvas publisher links without any
// grtext buffer, renderer state, draw function or game-window globals.
namespace { uint64_t FontRevision = 1; int FontWidth = 6, GlyphCalls = 0; }
uint64_t grfont_LayoutRevision() { return FontRevision; }
int grfont_GetCharWidth(int,int ch) { return ch == ' ' ? 3 : FontWidth; }
int grfont_GetHeight(int) { return 8; }
int grfont_GetTracking(int) { return 0; }
int grfont_GetKernedSpacing(int,int a,int b) { return a == 'A' && b == 'B' ? -1 : 0; }
bool grfont_GetGlyph(int,int ch,tFontGlyph& glyph) {
    ++GlyphCalls;
    glyph = {}; glyph.bitmap = ch == 'Z' ? 1 : 0;
    glyph.width = grfont_GetCharWidth(0,ch); glyph.height = 8;
    glyph.uv_width = .1f; glyph.uv_height = .2f;
    return ch >= 32;
}

int runCanvasTests() {
    int checks = 0;
    const auto check = [&](bool value,const char* message) { ++checks; if (!value) throw std::runtime_error(message); };
    SceneTextStyle style; style.font = 0;
    HUDCanvasRequest hud; hud.world = {0,0,1280,720}; hud.display = hud.world;
    hud.mode = HUDViewMode::Ship; hud.item_mask = 123; hud.show_cockpit = true;
    auto hud_layout = CompileHUDCanvasLayout(hud);
    check(hud_layout.primary.x == 160 && hud_layout.primary.width == 960 && !hud_layout.auxiliary &&
        hud_layout.aim == HUDAimMode::Ship && hud_layout.item_mask == 123 && hud_layout.cockpit,"Ship HUD did not compile its centered authored canvas");
    hud.world = {256,144,768,432}; hud_layout = CompileHUDCanvasLayout(hud);
    check(hud_layout.primary.x == 352 && hud_layout.primary.width == 576 && hud_layout.auxiliary &&
        hud_layout.display.width == 1280,"Reduced HUD changed the auxiliary display extent");
    hud.show_ship_reticle = false;
    check(CompileHUDCanvasLayout(hud).aim == HUDAimMode::Hidden,"Ship reticle toggle ignored");
    for (auto mode : {HUDViewMode::Guided,HUDViewMode::Zoom,HUDViewMode::Rear,HUDViewMode::Hidden}) {
        hud.mode = mode; hud_layout = CompileHUDCanvasLayout(hud);
        check(!hud_layout.item_mask && !hud_layout.auxiliary && !hud_layout.cockpit,"Special view inherited normal HUD items/cockpit");
        const auto aim = mode == HUDViewMode::Guided ? HUDAimMode::Guided : mode == HUDViewMode::Zoom ? HUDAimMode::Zoom : HUDAimMode::Hidden;
        check(hud_layout.aim == aim,"Special aim mode incorrectly followed the ship reticle toggle");
    }
    hud.centered_four_three = false;
    for (auto mode : {HUDViewMode::Letterbox,HUDViewMode::Observer}) {
        hud.mode = mode; hud_layout = CompileHUDCanvasLayout(hud);
        check(hud_layout.primary.x == 256 && hud_layout.primary.width == 768 && hud_layout.item_mask == 123 &&
            hud_layout.aim == HUDAimMode::Hidden && !hud_layout.cockpit,"Non-ship HUD inherited a ship aspect/reticle/cockpit");
    }
    bool invalid_hud = false; hud.mode = static_cast<HUDViewMode>(99);
    try { CompileHUDCanvasLayout(hud); } catch (const std::invalid_argument&) { invalid_hud = true; }
    check(invalid_hud,"Unknown HUD mode accepted");
    invalid_hud = false; hud.mode = HUDViewMode::Ship; hud.display.height = 0;
    try { CompileHUDCanvasLayout(hud); } catch (const std::invalid_argument&) { invalid_hud = true; }
    check(invalid_hud,"Zero-height HUD layout accepted");
    const auto extent = MeasureSceneText("AB",style);
    check(extent.width == 12 && extent.height == 9,"Canvas text metrics lost kerning or spacing");
    check(MeasureSceneText("AB\nA",style).height == 18 && MeasureSceneText("AB\nA",style).width == 12,
        "Canvas multiline measurement is not the maximum line extent");
    check(MeasureSceneText("A\tB",style).width == 22,"Canvas tab measurement is incorrect");
    check(MeasureSceneText("\2\3A",style).width == 18,"Canvas positioning control became a glyph");
    d3scene::FrameData data; data.sequence = 1; data.scene_lifetime = d3scene::assets().lifetime();
    int32_t order = 0;
    PrepareSceneText(data,7,order,0,0,"AB",style);
    const auto positions = [](const d3scene::PreparedMesh& mesh) -> const std::vector<d3scene::Vec3>& {
        return mesh.vertices.empty() ? mesh.asset->geometry().vertices : mesh.vertices;
    };
    check(data.meshes.size() == 1 && positions(data.meshes[0]).size() == 8 && positions(data.meshes[0])[4].x == 6,
        "Canvas text execution disagrees with its metrics");
    check(data.meshes[0].vertices.empty() && data.meshes[0].texture_uvs.empty() &&
        data.meshes[0].asset->geometry().polygons[0].corners[2].v == .2f,
        "Retained text still requires transient positions/UVs");
    const auto held = d3scene::Presentation::freeze(data);
    check(!held->data().meshes[0].surface.medium_transport && !held->data().meshes[0].surface.material.medium_transport,
        "Canvas text inherited world medium transport");
    {
        d3scene::SurfaceBinding world_surface;
        check(d3scene::compileMaterialPolicy(world_surface,{}).medium_transport,
            "World material lost its default medium transport");
        world_surface.medium_transport = false;
        check(!d3scene::compileMaterialPolicy(world_surface,{}).medium_transport,
            "Explicit material transport policy was ignored");
    }
    {
        const auto asset = data.meshes[0].asset;
        const int glyph_calls = GlyphCalls;
        data.meshes.clear(); order = 17;
        auto changed_style = style; changed_style.alpha = .4f; changed_style.additive = true;
        PrepareSceneText(data,9,order,0,0,"AB",changed_style);
        check(data.meshes[0].asset == asset && GlyphCalls == glyph_calls && data.meshes[0].collection == 9 &&
            data.meshes[0].surface.alpha == .4f && data.meshes[0].surface.blend == d3scene::BlendMode::Additive &&
            data.meshes[0].surface.composition_order == 17 && order == 18,
            "Text cache rebuilt geometry or retained instance composition state");
        ++FontRevision; FontWidth = 7;
        data.meshes.clear(); order = 0;
        PrepareSceneText(data,7,order,0,0,"AB",style);
        check(data.meshes[0].asset != asset && positions(data.meshes[0])[4].x == 7 &&
            positions(held->data().meshes[0])[4].x == 6,"Font metric edit did not invalidate held text layout safely");
        ++FontRevision; FontWidth = 6;
        for (int i = 0; i < 80; ++i) {
            data.meshes.clear(); order = 0;
            PrepareSceneText(data,7,order,i*2,0,("Label "+std::to_string(i)).c_str(),style);
        }
        check(positions(held->data().meshes[0])[4].x == 6,"Text cache eviction mutated a held mesh");
    }
    data.meshes.clear(); order = 0;
    PrepareSceneText(data,7,order,5,10,"A\tB",style);
    check(positions(data.meshes[0])[4].x == 21 && positions(data.meshes[0])[6].x-5 == MeasureSceneText("A\tB",style).width,
        "Moving a canvas string changed its tab layout relative to measurement");
    data.meshes.clear(); order = 0;
    PrepareSceneText(data,7,order,0,0,"A\1\x10\x20\x30" "BZ",style);
    check(data.meshes.size() == 3 && data.meshes[1].surface.color_modulation.y == 32/255.f &&
        positions(data.meshes[1])[0].x == 7 && data.meshes[2].surface.bitmap == 1,
        "Canvas color/atlas changes did not split owned glyph batches");
    check(positions(held->data().meshes[0]).size() == 8 && held->data().meshes[0].surface.color_modulation.y == 1,
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
    display.viewport.width = 800;
    display.kind = d3scene::ViewKind::Main; rejectForeground(); display.kind = d3scene::ViewKind::Preview;
    data.foregrounds[0].phase = static_cast<d3scene::ForegroundPhase>(99); rejectForeground();
    data.foregrounds[0].phase = d3scene::ForegroundPhase::WorldOverlay;
    auto extra = data.foregrounds[0]; extra.identity = 8;
    data.foregrounds.push_back(extra); rejectForeground();
    data.foregrounds[0].order = 20;
    data.foregrounds[1].order = 10;
    extra.identity = 9; extra.order = -1; extra.phase = d3scene::ForegroundPhase::FinalOverlay;
    data.foregrounds.push_back(extra);
    const auto scheduled = d3scene::Presentation::freeze(data);
    check(scheduled->data().foregrounds[0].identity == 8 && scheduled->data().foregrounds[1].identity == 7 &&
        scheduled->data().foregrounds[2].identity == 9,"Foreground execution order depends on publication call order");
    check(data.foregrounds[0].identity == 7,"Foreground compilation mutated the caller's plan");
    {
        d3scene::FrameData overlay; overlay.sequence = 1; overlay.scene_lifetime = d3scene::assets().lifetime();
        std::vector<FrameScreenBlend> events{{1,0,0,.25f},{0,0,1,.5f}};
        PrepareFrameBlendCanvas(overlay,41,{20,30,800,600},events,true);
        check(overlay.meshes.size() == 2 && overlay.meshes[0].asset == overlay.meshes[1].asset &&
            overlay.meshes[0].vertices.empty() && overlay.meshes[0].texture_uvs.empty(),
            "Screen blend allocated per-event quad geometry");
        check(overlay.foregrounds.size() == 1 && overlay.foregrounds[0].phase == d3scene::ForegroundPhase::AfterMainWorld &&
            overlay.foregrounds[0].layer.composition == d3scene::ForegroundComposition::InScene,
            "Screen blend completed world presentation or entered cockpit isolation");
        const auto retained = d3scene::Presentation::freeze(overlay);
        events[0].alpha = 1; overlay.meshes.clear();
        check(retained->data().meshes[0].surface.alpha == .25f,
            "Screen overlay retained mutable callback data");
        const auto point = d3scene::transformPoint(retained->data().meshes[0].world,{-1,1,0});
        check(point.x == 0 && point.y == 0,"Screen blend placement lost its local canvas origin");
        auto invalid_layer = retained->data().foregrounds[0].layer;
        invalid_layer.display_views.push_back(canvas);
        bool failed = false;
        try { d3scene::validateForeground(invalid_layer); } catch (const std::invalid_argument&) { failed = true; }
        check(failed,"In-scene canvas accepted a display-phase consumer");
        invalid_layer.display_views.clear(); invalid_layer.scene_views[0].clear_depth = true;
        failed = false;
        try { d3scene::validateForeground(invalid_layer); } catch (const std::invalid_argument&) { failed = true; }
        check(failed,"In-scene canvas accepted a world depth clear");
    }
    return checks;
}
