// Descent 3MASTERED - GPL-3.0-or-later
// Hidden, explicitly armed integration fixtures. No mutation in ordinary play.
#include "GameCameraCapture.h"
#include "SceneExtraction.h"
#include "SceneCanvas.h"
#include "SmallViews.h"
#include "gameloop.h"
#include "player.h"
#include "object.h"
#include "objinfo.h"
#include "weapon.h"
#include "args.h"
#include "pserror.h"

namespace {
bool active(const char* option) {
    // Legacy FindArg takes char*, but only compares the supplied option.
    return AutomatedCaptureFrameIndex() >= 0 && SceneGeometryInspection() && FindArg(const_cast<char*>(option));
}
}
void RunGameCameraCaptureTransitions() {
    const bool observer_fixture = active("-scene-observer-routing-probe");
    const bool guided_fixture = active("-scene-guided-routing-probe");
    if (observer_fixture && guided_fixture) Error("Camera routing fixtures are mutually exclusive");
    if (observer_fixture) {
    	static int target = OBJECT_HANDLE_NONE;
    	const int index = AutomatedCaptureFrameIndex();
    	if (index == 0) {
    		ResetSmallViews();
    		auto position = Player_object->pos+Player_object->orient.fvec*8;
    		const int created = ObjCreate(OBJ_ROBOT,GENOBJ_GUIDEBOT,Player_object->roomnum,&position,&Player_object->orient);
    		if (created < 0) Error("Observer fixture could not create its target");
    		Objects[created].control_type = CT_NONE; Objects[created].movement_type = MT_NONE;
    		target = Objects[created].handle;
    		CreateSmallView(SVW_LEFT,target,SVF_CROSSHAIRS,0,0,-1,"Target");
    		CreateSmallView(SVW_RIGHT,Player_object->handle,SVF_REARVIEW,0,0,-1,"Rear");
    	}
    	if (index == 5) PlayerSwitchToObserver(Player_num,OBSERVER_MODE_ROAM);
    	if (index == 15) {
    		auto* source = ObjGet(target); if (!source) Error("Observer fixture target retired");
    		PlayerSwitchToObserver(Player_num,OBSERVER_MODE_PIGGYBACK,OBJNUM(source));
    	}
    	if (index == 20) {
    		auto* source = ObjGet(target); if (!source || GetPlayerObserverTarget(Player_num) != source)
    			Error("Observer did not follow its original allocation");
    		const int old_slot = OBJNUM(source); auto position = source->pos; const auto orientation = source->orient;
    		const int room = source->roomnum;
    		ObjDelete(old_slot);
    		const int created = ObjCreate(OBJ_ROBOT,GENOBJ_GUIDEBOT,room,&position,&orientation);
    		if (created != old_slot || Objects[created].handle == target || GetPlayerObserverTarget(Player_num))
    			Error("Observer followed a recycled target allocation");
    		Objects[created].control_type = CT_NONE; Objects[created].movement_type = MT_NONE;
    		AutomatedCaptureLog("scene observer replacement old=%d new=%d same_slot=1 followed=0",target,Objects[created].handle);
    	}
    	if (index == 21 && (Players[Player_num].piggy_objnum != -1 || Player_object->control_type != CT_FLYING))
    		Error("Observer did not recover roaming control after target retirement");
    	if (index == 25) PlayerStopObserving(Player_num);
    }
    // Bounded hidden fixture: use a real weapon object but hold its motion to
    // isolate monitor ownership transitions from flight/collision behavior.
    if (guided_fixture) {
    	static int owner = OBJECT_HANDLE_NONE;
    	const int index = AutomatedCaptureFrameIndex();
    	if (index == 0) {
    		ResetSmallViews();
    		const int weapon = FindWeaponName("Guided");
    		const int created = weapon < 0 ? -1 : FireWeaponFromObject(Player_object,weapon,-1,true,false);
    		if (created < 0) Error("Guided routing fixture could not create its weapon");
    		auto& missile = Objects[created];
    		missile.movement_type = MT_NONE; missile.control_type = CT_NONE; missile.lifeleft = 100;
    		Players[Player_num].guided_obj = &missile; owner = missile.handle;
    		CreateSmallView(SVW_LEFT,owner,SVF_CROSSHAIRS,0,0,-1,"Guided");
    	}
    	if (index < 35 && !ObjGet(owner)) Error("Guided routing source retired before fixture completion");
    	if (index == 10) CreateSmallView(SVW_LEFT,Player_object->handle,SVF_POPUP|SVF_REARVIEW,0,0,-1,"Rear popup");
    	if (index == 15) CloseSmallView(SVW_LEFT);
    	if (index == 20) CreateSmallView(SVW_RIGHT,owner,SVF_CROSSHAIRS,0,0,-1,"Guided");
    	if (index == 25) CloseSmallView(SVW_LEFT);
    	if (index == 30) CreateSmallView(SVW_RIGHT,Player_object->handle,SVF_REARVIEW,0,0,-1,"Rear");
    	if (index == 35) ReleaseGuidedMissile(Player_num);
    	const bool in_monitor = index < 10 || (index >= 15 && index < 30);
    	if (GuidedMissileInSmallView() != in_monitor) Error("Guided camera routing disagrees with live monitor ownership");
    }
}
void ValidateGameCameraCapture(const GameMainView& main_view,const HUDCanvasLayout& hud_layout) {
    const bool observer_fixture = active("-scene-observer-routing-probe");
    const bool guided_fixture = active("-scene-guided-routing-probe");
    if (guided_fixture) {
    	const int index = AutomatedCaptureFrameIndex();
    	const bool full = (index >= 10 && index < 15) || (index >= 30 && index < 35);
    	const auto* expected = full ? Players[Player_num].guided_obj : Player_object;
    	if (!expected || main_view.world.camera_owner != uint32_t(expected->handle))
    		Error("Guided routing selected the wrong physical main camera");
    	if (index % 5 == 0) AutomatedCaptureLog("scene guided routing frame=%d small=%d main_guided=%d owner=%u",
    		index,GuidedMissileInSmallView(),full,main_view.world.camera_owner);
    }
    if (observer_fixture) {
    	const int index = AutomatedCaptureFrameIndex();
    	const bool observing = index >= 5 && index < 25;
    	if (main_view.player_first_person == observing || (observing && hud_layout.cockpit))
    		Error("Observer source inherited ship-local presentation");
    	if (index % 5 == 0) AutomatedCaptureLog("scene observer routing frame=%d observing=%d ship_local=%d cockpit=%d",
    		index,observing,main_view.player_first_person,hud_layout.cockpit);
    }
    if (guided_fixture) {
    	const int index = AutomatedCaptureFrameIndex();
    	const bool full = (index >= 10 && index < 15) || (index >= 30 && index < 35);
    	if ((hud_layout.aim == HUDAimMode::Guided) != full)
    		Error("Guided HUD disagrees with the physical main camera");
    }
}
void ValidateGameCameraCapture(const GameAuxiliarySchedule& auxiliary_schedule) {
    const bool observer_fixture = active("-scene-observer-routing-probe");
    if (observer_fixture && Player_object->type == OBJ_OBSERVER) {
    	if (auxiliary_schedule.count != 2) Error("Observer lost its auxiliary cameras");
    	for (unsigned i = 0; i < auxiliary_schedule.count; ++i)
    		if (auxiliary_schedule.views[i].embedded) Error("Observer camera requested a nonexistent cockpit monitor");
    }
}

