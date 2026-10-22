// Descent 3MASTERED - GPL-3.0-or-later
#include "../../Descent3/SceneFeedback.h"
#include "../../Descent3/SceneExtraction.h"
#include "../../Descent3/object.h"
#include "../../Descent3/room.h"
#include "../../Descent3/game.h"
#include "../../Descent3/descent.h"
#include "../../Descent3/AIMain.h"
#include "../../Descent3/TelComAutoMap.h"
#include "../../Descent3/terrain.h"
#include "../../Descent3/weather.h"

// Link the real game-owned commit against a minimal live simulation fixture.
// The only substituted boundaries are object lookup, function mode and weather
// delivery; no GL, renderer callback or RNG implementation is linked here.
int FrameCount = 0, AI_NumRendered = 0;
int AI_RenderedList[MAX_OBJECTS]{};
float Gametime = 0, Last_terrain_render_time = 0;
ubyte AutomapVisMap[MAX_ROOMS]{};
static int WeatherDeliveries = 0;
static bool WeatherExterior = false;
static uint32_t WeatherOwner = 0;
function_mode GetFunctionMode() { return GAME_MODE; }
object* ObjGet(int handle) {
    if (handle == OBJECT_HANDLE_NONE || handle == OBJECT_HANDLE_BAD) return nullptr;
    const auto index = uint32_t(handle) & HANDLE_OBJNUM_MASK;
    return index < MAX_OBJECTS && Objects[index].type != OBJ_NONE && Objects[index].handle == handle ?
        &Objects[index] : nullptr;
}
void SetWeatherMainViewObservation(bool visible,uint32_t owner) {
    ++WeatherDeliveries; WeatherExterior = visible; WeatherOwner = owner;
}

int runGameFrameTests() {
    using namespace d3scene;
    int checks = 0;
    const auto check = [&](bool valid) {
        ++checks; if (!valid) throw std::runtime_error("Game-owned observation commit");
    };
    const auto rejects = [&](const auto& action) {
        bool failed = false;
        try { action(); } catch (const std::logic_error&) { failed = true; }
        check(failed);
    };
    ResetGamePresentationScene();
    Rooms[7] = {}; Rooms[7].used = 1;
    Highest_object_index = 1;
    for (int i = 0; i < 2; ++i) {
        Objects[i] = {}; Objects[i].type = i ? OBJ_POWERUP : OBJ_ROBOT;
        Objects[i].handle = HANDLE_COUNT_INCREMENT | i; Objects[i].roomnum = 7;
        Objects[i].control_type = i ? CT_NONE : CT_AI;
        Objects[i].orient.rvec.x = Objects[i].orient.uvec.y = Objects[i].orient.fvec.z = 1;
    }
    FrameCount = 65535; Gametime = 12;
    const auto frame = ExtractGamePresentation(Gametime,.016f,false);
    VisibilityReport report;
    report.frame_sequence = frame->data().sequence; report.scene_lifetime = frame->data().scene_lifetime;
    report.view_history_key = 1; report.feedback = Automap | ObjectVisibility | EffectEligibility;
    report.rooms = {7}; report.objects = {uint32_t(Objects[1].handle),uint32_t(Objects[0].handle),uint32_t(Objects[0].handle)};
    report.terrain = true;
    SceneFeedbackTransaction transaction(*frame); transaction.add(report); transaction.add(report);
    auto result = transaction.seal(); result.main_view_present = true;
    result.main_exterior_visible = false; result.main_camera_owner = Objects[1].handle;
    check(CommitGameVisibility(*frame,result,FrameCount));
    check(AutomapVisMap[7] == 1 && Rooms[7].last_render_time == Gametime && Last_terrain_render_time == Gametime);
    check(AI_NumRendered == 1 && AI_RenderedList[0] == 0);
    check((Objects[0].flags & OF_RENDERED) && (Objects[1].flags & OF_RENDERED));
    check(Objects[0].renderframe == 65535 && Objects[1].renderframe == 65535);
    check(WeatherDeliveries == 1 && !WeatherExterior && WeatherOwner == uint32_t(Objects[1].handle));
    check(!CommitGameVisibility(*frame,result,FrameCount));
    check(WeatherDeliveries == 1 && AI_NumRendered == 1);

    // An independent publication is not a logical gameplay tick.
    const auto thumbnail = ExtractGamePresentation(Gametime,0,false,false);
    ++FrameCount; Gametime = 13;
    const auto next = ExtractGamePresentation(Gametime,.016f,false);
    auto next_result = result; next_result.frame_sequence = next->data().sequence;
    next_result.main_exterior_visible = true;
    const auto old_handle = Objects[0].handle;
    Objects[0].handle += HANDLE_COUNT_INCREMENT;
    AutomapVisMap[7] = 0; Rooms[7].last_render_time = -1;
    rejects([&] { CommitGameVisibility(*next,next_result,FrameCount); });
    check(AutomapVisMap[7] == 0 && Rooms[7].last_render_time == -1 && WeatherDeliveries == 1);
    Objects[0].handle = old_handle;
    check(CommitGameVisibility(*next,next_result,FrameCount));
    check(Objects[0].renderframe == 0 && WeatherDeliveries == 2 && WeatherExterior);
    rejects([&] { CommitGameVisibility(*frame,result,FrameCount); });
    auto wrong = next_result; ++wrong.frame_sequence;
    rejects([&] { CommitGameVisibility(*next,wrong,FrameCount); });
    ResetGamePresentationScene();
    rejects([&] { CommitGameVisibility(*next,next_result,FrameCount); });
    Rooms[7] = {}; Objects[0] = {}; Objects[1] = {}; Highest_object_index = -1;
    AI_NumRendered = 0; FrameCount = 0; Gametime = 0;
    return checks;
}
