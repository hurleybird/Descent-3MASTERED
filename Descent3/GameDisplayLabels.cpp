// Descent 3MASTERED - GPL-3.0-or-later
#include "GameDisplayLabels.h"
#include "../renderer/SceneDeformation.h"
#include "SceneExtraction.h"
#include "SceneModuleContext.h"
#include "SceneFeedback.h"
#include "hud.h"
#include "object.h"
#include "player.h"
#include "multi.h"
#include "game.h"
#include "game2dll.h"
#include "findintersection.h"
#include "gamefont.h"
#include "config.h"
#include "gameloop.h"
#include "args.h"
#include "grdefs.h"

void QueueGamePlayerLabels(const d3scene::Presentation& world,const SceneFrameFeedback& receipt) {
    const bool fixture = FindArg("-scene-player-label-probe") && GameModuleCanvasIdentity();
    if ((!(Game_mode & GM_MULTI) && !fixture) || IsCleanGameplayScreenshotActive() || !(HudNameTan > 0)) return;
    if (!receipt.main_view_present || receipt.frame_sequence != world.data().sequence ||
        receipt.scene_lifetime != world.data().scene_lifetime) return;
    const auto& camera = receipt.main_camera;
    const auto* observer = ObjGet(int(receipt.main_camera_owner));
    if (!observer) return;
    const float aspect = float(receipt.main_viewport.width)/Max_window_w;
    const float scale = (aspect <= .6f ? .6f : aspect <= .8f ? .8f : 1.f)*ConfigNormalizeHudTextScale(Hud_text_scale);
    vector eye{camera.transform.position.x,camera.transform.position.y,camera.transform.position.z};
    unsigned names = 0, eligible = 0, projected = 0, unoccluded = 0;
    for (int slot = 0; slot < MAX_PLAYERS; ++slot) {
        if (slot == Player_num || NetPlayers[slot].sequence != NETSEQ_PLAYING || Players[slot].objnum < 0 ||
            Players[slot].objnum >= MAX_OBJECTS) continue;
        const auto* player = &Objects[Players[slot].objnum];
        if (player->type != OBJ_PLAYER || player->id != slot ||
            (player->effect_info && (player->effect_info->type_flags & EF_CLOAKED))) continue;
        const auto* found = world.objectIndices().find(uint32_t(player->handle));
        if (!found || std::binary_search(receipt.main_excluded_objects.begin(),receipt.main_excluded_objects.end(),uint32_t(player->handle))) continue;
        const auto& source = world.data().objects[found->second];
        if (!source.visible) continue;
        ++eligible;
        d3scene::Vec2 position;
        d3scene::ViewDeformationResponse response{0,0};
        if (const auto* body = world.modelIndices().find(GameObjectModelIdentity(source.handle)))
            response.translation = world.data().model_instances[body->second].deformation_response.translation;
        if (!ProjectGameLabel(camera,receipt.main_viewport,d3scene::deformViewPoint(source.current.position,receipt.main_deformation,response,source.handle),HudNameTan,position)) continue;
        ++projected;
        // The game decides semantic visibility and module color before canvas
        // publication. No raycast, callback or global g3 projection reaches GL.
        vector target{source.current.position.x,source.current.position.y,source.current.position.z};
        fvi_query query{}; fvi_info hit{};
        query.p0 = &eye; query.p1 = &target; query.startroom = receipt.main_camera_room;
        query.thisobjnum = OBJNUM(observer);
        query.flags = FQ_CHECK_OBJS|FQ_IGNORE_POWERUPS|FQ_IGNORE_WEAPONS|FQ_NO_RELINK;
        const int fate = fvi_FindIntersection(&query,&hit);
        if (fixture && AutomatedCapturePending()) AutomatedCaptureLog("scene label visibility slot=%d fate=%d hit=%d target=%d",slot,fate,hit.hit_object[0],OBJNUM(player));
        if (!GameLabelSightClear(fate,hit.hit_object[0],OBJNUM(player))) continue;
        ++unoccluded;
        dllinfo color{}; color.me_handle = player->handle; color.it_handle = OBJECT_HANDLE_NONE; color.iRet = -1;
        CallGameDLL(EVT_CLIENT_GETCOLOREDNAME,&color);
        if (color.iRet < 0) continue;
        QueuePostProcessHUDText(Players[slot].callsign,int(position.x),int(position.y),true,HUD_FONT,scale,color.iRet,255,0);
        ++names;
    }
    if (AutomatedCapturePending()) AutomatedCaptureLog("scene player labels count=%u owner=%u eligible=%u projected=%u unoccluded=%u",names,receipt.main_camera_owner,eligible,projected,unoccluded);
}
