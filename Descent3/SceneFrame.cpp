// Descent 3MASTERED - GPL-3.0-or-later
#include "SceneFeedback.h"
#include "object.h"
#include "room.h"
#include "game.h"
#include "descent.h"
#include "AIMain.h"
#include "TelComAutoMap.h"
#include "terrain.h"
#include "weather.h"

namespace {
struct ObservationStamp { uint64_t scene = 0, frame = 0; uint32_t handle = 0; bool valid = false; };
ObservationStamp ObjectObservations[MAX_OBJECTS];
struct ObservationSink {
    void validateRoom(int32_t id) const {
        if (id < 0 || id >= MAX_ROOMS || !Rooms[id].used)
            throw std::invalid_argument("Observed room retired before game commit");
    }
    void validateObject(uint32_t handle) const {
        if (!ObjGet(handle)) throw std::invalid_argument("Observed object retired before game commit");
    }
    void apply(const SceneFrameFeedback& result,uint64_t game_frame) const noexcept {
        for (auto room : result.automap_rooms) AutomapVisMap[room] = 1;
        for (auto room : result.effect_rooms) Rooms[room].last_render_time = Gametime;
        if (result.terrain) Last_terrain_render_time = Gametime;
        AI_NumRendered = 0;
        const auto mode = GetFunctionMode();
        for (auto handle : result.objects) {
            auto* object = ObjGet(handle); // preflight validated the complete transaction
            object->flags |= OF_RENDERED;
            object->renderframe = static_cast<ushort>(game_frame & 65535);
            ObjectObservations[OBJNUM(object)] = {result.scene_lifetime,game_frame,handle,true};
            if (object->control_type == CT_AI && (mode == GAME_MODE || mode == EDITOR_GAME_MODE))
                AI_RenderedList[AI_NumRendered++] = OBJNUM(object);
        }
        if (result.main_view_present)
            SetWeatherMainViewObservation(result.main_exterior_visible,result.main_camera_owner);
    }
};
SceneFeedbackCommit Receipt;
}

bool GameObjectWasObservedLastFrame(uint64_t scene,uint64_t frame,uint32_t handle) {
    const auto index = handle & HANDLE_OBJNUM_MASK;
    if (!frame || index >= MAX_OBJECTS) return false;
    const auto& stamp = ObjectObservations[index];
    return stamp.valid && stamp.scene == scene && stamp.frame == frame-1 && stamp.handle == handle && ObjGet(int(handle));
}

bool CommitGameVisibility(const d3scene::Presentation& frame,const SceneFrameFeedback& result,uint64_t game_frame) {
    if (frame.data().scene_lifetime != d3scene::assets().lifetime() || game_frame != uint64_t(FrameCount))
        throw std::invalid_argument("Game observations belong to a retired scene or logical frame");
    ObservationSink sink;
    return Receipt.commit(frame,result,game_frame,sink);
}
