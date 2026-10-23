// Descent 3MASTERED - GPL-3.0-or-later
#include "../../Descent3/GameAuxiliaryViews.h"
#include "../../renderer/SceneHistory.h"
#include "../../renderer/SceneCompositing.h"
#include <limits>
#include <cstring>
#include <stdexcept>

int runAuxiliaryViewTests() {
    using namespace d3scene;
    int checks = 0;
    const auto check = [&](bool valid) { ++checks; if (!valid) throw std::runtime_error("Auxiliary view frame contract"); };
    GameAuxiliarySlot live, saved, base; base.objhandle = 2048; base.flags = SVF_REARVIEW;
    std::strcpy(base.label,"Rear");
    OpenAuxiliarySlot(live,saved,base);
    check(live.objhandle == 2048 && saved.objhandle == -1);
    auto popup = base; popup.objhandle = 2049; popup.flags = SVF_POPUP|SVF_TIMED; popup.timer = 1;
    OpenAuxiliarySlot(live,saved,popup);
    check(live.objhandle == 2049 && saved.objhandle == 2048);
    popup.objhandle = 4097; OpenAuxiliarySlot(live,saved,popup);
    check(saved.objhandle == 2048);
    check(CloseAuxiliarySlot(live,saved) && live.objhandle == 2048 && saved.objhandle == -1);
    OpenAuxiliarySlot(live,saved,popup); OpenAuxiliarySlot(live,saved,base);
    check(saved.objhandle == -1 && !CloseAuxiliarySlot(live,saved)); // explicit replacement cancels stale restoration
    live = popup; live.gun_num = 5; ObserveAuxiliarySource(live,false);
    check((live.flags&SVF_STATIC) && live.timer == SmallViewStaticTime && live.gun_num == -1);
    check(!AdvanceAuxiliaryTimer(live,false,.1f) && live.timer == SmallViewStaticTime);
    check(!AdvanceAuxiliaryTimer(live,true,.1f)); const float remaining = live.timer;
    ObserveAuxiliarySource(live,false); check(live.timer == remaining); // no replenished static on replay/dead source
    check(!AdvanceAuxiliaryTimer(live,true,0) && live.timer == remaining);
    check(!AdvanceAuxiliaryTimer(live,true,.1f)); check(AdvanceAuxiliaryTimer(live,true,.1f));
    live = base; check(!AdvanceAuxiliaryTimer(live,true,10) && live.timer == 0);
    bool rejected = false; try { AdvanceAuxiliaryTimer(live,true,-1); } catch (const std::invalid_argument&) { rejected = true; }
    check(rejected);
    check(ValidAuxiliaryRequest(SVF_POPUP|SVF_CROSSHAIRS,0,0,-1));
    check(!ValidAuxiliaryRequest(SVF_STATIC,0,1,-1) && !ValidAuxiliaryRequest(SVF_TIMED,1,1,-1));
    check(!ValidAuxiliaryRequest(0,-1,1,-1) && !ValidAuxiliaryRequest(0,0,-1,-1) && !ValidAuxiliaryRequest(0,0,1,-2));
    check(!ValidAuxiliaryRequest(0,0,std::numeric_limits<float>::quiet_NaN(),-1));
    const auto left = AuxiliaryWindowRectangle({0,0,640,480},SVW_LEFT,false);
    const auto right = AuxiliaryWindowRectangle({0,0,640,480},SVW_RIGHT,false);
    check(left.x == 46 && left.y == 340 && left.width == 120 && left.height == 120);
    check(right.x == 472 && right.y == 340 && right.width == 120);
    const auto larger = AuxiliaryWindowRectangle({40,20,640,480},SVW_LEFT,true);
    check(larger.x == 71 && larger.y == 345 && larger.width == 150 && larger.height == 150);
    check(!AuxiliaryWindowRectangle({0,0,0,480},SVW_LEFT,false).width);
    AuxiliaryFrameReceipt clock;
    check(clock.begin(7,0)); check(!clock.begin(7,0)); check(clock.begin(7,1));
    rejected = false; try { clock.begin(7,0); } catch (const std::logic_error&) { rejected = true; } check(rejected);
    check(clock.begin(8,0)); clock.reset(); check(clock.begin(8,0));
    GameAuxiliaryFrame snapshot; snapshot.count = 1; snapshot.views[0].owner = 2048; std::strcpy(snapshot.views[0].label,"Rear");
    auto held = snapshot; snapshot.views[0].owner = 4096; snapshot.views[0].label[0] = 'X';
    check(held.views[0].owner == 2048 && !std::strcmp(held.views[0].label,"Rear"));
    View view; view.kind = ViewKind::Auxiliary; view.history_key = 3; view.viewport = view.projection_anchor = {0,0,100,100};
    view.motion_composition = View::MotionComposition::Stationary; validateView(view);
    auto invalid = view; invalid.kind = ViewKind::Main; rejected = false;
    try { validateView(invalid); } catch (const std::invalid_argument&) { rejected = true; } check(rejected);
    invalid = view; invalid.motion_composition = static_cast<View::MotionComposition>(99); rejected = false;
    try { validateView(invalid); } catch (const std::invalid_argument&) { rejected = true; } check(rejected);
    FrameData frame; frame.scene_lifetime = 7; frame.sequence = 1;
    ViewHistory history; history.commit(history.prepare(frame,view,prepareCamera(view)));
    frame.temporal_predecessor = 1; frame.sequence = 2;
    check(history.prepare(frame,view,prepareCamera(view)).previous_valid);
    view.motion_composition = View::MotionComposition::Preserve;
    check(!history.prepare(frame,view,prepareCamera(view)).previous_valid);
    view.kind = ViewKind::Preview; view.ordering = ViewOrdering::Compositing; view.collection = 5;
    view.motion_composition = View::MotionComposition::Stationary;
    ForegroundLayer layer; layer.scene_views.push_back(view); rejected = false;
    try { validateForeground(layer); } catch (const std::invalid_argument&) { rejected = true; } check(rejected);
    layer.composition = ForegroundComposition::InScene; validateForeground(layer); check(true);
    return checks;
}
