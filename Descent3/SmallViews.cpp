/*
* Descent 3
* Copyright (C) 2024 Parallax Software
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "SmallViews.h"
#include "SceneExtraction.h"
#include "SceneFeedback.h"
#include "SceneCanvas.h"
#include "game.h"
#include "object.h"
#include "terrain.h"
#include "gameloop.h"
#include "weapon.h"
#include "findintersection.h"
#include "config.h"
#include "gauges.h"
#include "cockpit.h"
#include "player.h"
#include "gametexture.h"
#include "vclip.h"
#include "hud.h"
#include "gamefont.h"
#include "grdefs.h"
#include "pserror.h"
#include <algorithm>
#include <cstring>

namespace {
constexpr unsigned Count = 3;
std::array<GameAuxiliarySlot,Count> Small_views, Saved_views;
std::array<uint64_t,Count> Revisions{}, AdmittedRevisions{};
AuxiliaryFrameReceipt CaptureClock, ScheduleClock, TimerClock;
GameAuxiliaryFrame Captured;
GameAuxiliarySchedule Scheduled;
uint64_t SchedulePublication = 0;
int Guided_owner = OBJECT_HANDLE_NONE;
constexpr uint64_t DecorationCollection = 0x4155584445434f01ull;
void revise(unsigned slot) { if (!++Revisions[slot]) ++Revisions[slot]; }
int StaticBitmap(float timer,uint64_t scene) {
    static uint64_t lifetime = 0;
    static int texture = -1;
    if (lifetime != scene) { lifetime = scene; texture = -1; }
    if (texture < 0) texture = FindTextureName("Static");
    if (texture < 0 || texture >= MAX_TEXTURES || !GameTextures[texture].used) {
        Error("Auxiliary static texture is unavailable"); return -1;
    }
    const int clip = GameTextures[texture].bm_handle;
    if (clip < 0 || clip >= MAX_VCLIPS || !GameVClips[clip].used) {
        Error("Auxiliary static clip is invalid"); return -1;
    }
    PageInVClip(clip);
    const auto& source = GameVClips[clip];
    if (!source.frames || source.num_frames <= 0) { Error("Auxiliary static clip has no frames"); return -1; }
    const int frame = std::max(0,std::min(int(source.num_frames)-1,int(source.num_frames*(SmallViewStaticTime-timer)/SmallViewStaticTime)));
    return source.frames[frame];
}
int GunRoom(object& source,vector& point) {
    fvi_query query{}; fvi_info hit{};
    query.p0 = &source.pos; query.p1 = &point; query.startroom = source.roomnum;
    query.thisobjnum = OBJNUM(&source);
    fvi_FindIntersection(&query,&hit);
    return hit.hit_room == -1 ? source.roomnum : hit.hit_room;
}
}

bool Guided_missile_smallview = false;

int GetSmallViewer(int window) {
    return window >= 0 && window < int(Count) ? Small_views[window].objhandle : OBJECT_HANDLE_NONE;
}

int CreateSmallView(int window,int owner,int flags,float time,float zoom,int gun,char* label) {
    if (window < 0 || window >= int(Count) || window == SVW_CENTER || !ObjGet(owner) ||
        !ValidAuxiliaryRequest(flags,time,zoom,gun)) return -1;
    GameAuxiliarySlot next;
    next.objhandle = owner; next.flags = flags | (time != 0 ? SVF_TIMED : 0);
    next.timer = time; next.zoom = zoom == 0 ? D3_DEFAULT_ZOOM : zoom; next.gun_num = gun;
    std::strncpy(next.label,label ? label : "",sizeof(next.label)-1);
    OpenAuxiliarySlot(Small_views[window],Saved_views[window],next); revise(window);
    if (window == SVW_LEFT) Disable_primary_monitor = 1;
    if (window == SVW_RIGHT) Disable_secondary_monitor = 1;
    if (ObjGet(owner) == Players[Player_num].guided_obj) {
        Guided_missile_smallview = true; Guided_owner = owner;
    }
    return window;
}

void CloseSmallView(int window) {
    if (window < 0 || window >= int(Count)) return;
    if (Small_views[window].objhandle == Guided_owner) {
        Guided_owner = OBJECT_HANDLE_NONE; Guided_missile_smallview = false;
    }
    const bool restored = CloseAuxiliarySlot(Small_views[window],Saved_views[window]);
    revise(window);
    if (!restored) {
        Clear_screen = 4;
        if (window == SVW_LEFT) Disable_primary_monitor = 0;
        if (window == SVW_RIGHT) Disable_secondary_monitor = 0;
    }
}

void ClosePopupView(int window) {
    if (window >= 0 && window < int(Count) && (Small_views[window].flags & SVF_POPUP)) CloseSmallView(window);
}

void ResetSmallViews() {
    for (unsigned i = 0; i < Count; ++i) { Small_views[i] = {}; Saved_views[i] = {}; revise(i); }
    Disable_primary_monitor = Disable_secondary_monitor = 0;
    Guided_missile_smallview = false; Guided_owner = OBJECT_HANDLE_NONE;
    AdmittedRevisions.fill(0);
    CaptureClock.reset(); ScheduleClock.reset(); TimerClock.reset();
    Captured = {}; Scheduled = {}; SchedulePublication = 0;
}

GameAuxiliaryFrame CaptureGameAuxiliaryViews(const GameMainView& main) {
    if (!std::isfinite(Frametime) || Frametime < 0 || !std::isfinite(Gametime))
        throw std::invalid_argument("Invalid auxiliary presentation clock");
    const uint64_t scene = d3scene::assets().lifetime(), frame = uint64_t(FrameCount);
    if (!CaptureClock.begin(scene,frame)) return Captured;
    GameAuxiliaryFrame result;
    result.scene = scene; result.frame = frame; result.delta = Frametime; result.time = Gametime;
    result.window = {Game_window_x,Game_window_y,Game_window_w,Game_window_h};
    result.cockpit_state = CockpitState(); result.alternative_cockpit = Cockpit_alt_mode;
    result.cockpit_visible = main.player_first_person;
    result.supersampling = ConfigNormalizeSupersamplingFactor(Render_preferred_state.supersampling_factor);
    result.hidden = GetHUDMode() == HUD_LETTERBOX;
    const float aspect = Max_window_w > 0 ? float(Game_window_w)/Max_window_w : 1;
    if (!result.hidden) for (unsigned i = 0; i < Count; ++i) {
        auto& slot = Small_views[i];
        if (slot.objhandle == OBJECT_HANDLE_NONE || i == SVW_CENTER) continue;
        auto* source = ObjGet(slot.objhandle);
        const bool alive = source && source->type != OBJ_DUMMY && !(source->flags & OF_DEAD) &&
            (!OBJECT_OUTSIDE(source) || source->pos.y < MAX_TERRAIN_HEIGHT);
        ObserveAuxiliarySource(slot,alive);
        if (result.cockpit_state == COCKPIT_STATE_QUASI) continue;
        GameAuxiliaryView captured; captured.slot = int(i); captured.revision = Revisions[i];
        captured.owner = uint32_t(slot.objhandle);
        captured.font_scale = aspect <= .6f ? .6f : aspect <= .8f ? .8f : 1;
        std::memcpy(captured.label,slot.label,sizeof(captured.label));
        if (!(slot.flags & SVF_STATIC)) {
            vector eye = source->pos; matrix orientation = source->orient; int room = source->roomnum;
            if (slot.gun_num != -1) {
                vector direction;
                if (!WeaponCalcGun(&eye,&direction,source,slot.gun_num)) ObserveAuxiliarySource(slot,false);
                else { vm_VectorToMatrix(&orientation,&direction,nullptr,nullptr); room = GunRoom(*source,eye); }
            }
            if (!(slot.flags & SVF_STATIC)) {
                const bool rear = (slot.flags & SVF_REARVIEW) && !(main.world.camera_owner == uint32_t(source->handle) &&
                    source->type == OBJ_PLAYER && main.world.camera_variant == 1);
                captured.world = MakeGameSceneView(d3scene::ViewKind::Auxiliary,i+2,source,eye,room,orientation,
                    slot.zoom,rear,{0,0,1,1},{0,0,1,1});
                captured.world.motion_composition = d3scene::View::MotionComposition::Stationary;
                captured.world.clear_depth = true; // independent world, scoped to its owned rectangle
            }
        }
        captured.flags = slot.flags; captured.timer = slot.timer;
        if (slot.flags & SVF_STATIC) captured.bitmap = StaticBitmap(slot.timer,scene);
        result.views[result.count++] = std::move(captured);
    }
    Captured = std::move(result);
    return Captured;
}

GameAuxiliarySchedule PrepareGameAuxiliarySchedule(const GameAuxiliaryFrame& input,const d3scene::Presentation& world) {
    if (input.scene != world.data().scene_lifetime || input.scene != d3scene::assets().lifetime() || input.frame != uint64_t(FrameCount))
        throw std::invalid_argument("Auxiliary requests belong to another game frame");
    if (!ScheduleClock.begin(input.scene,input.frame)) {
        if (SchedulePublication != world.data().sequence) throw std::logic_error("Auxiliary schedule changed publication");
        return Scheduled;
    }
    GameAuxiliarySchedule result;
    result.world_sequence = world.data().sequence;
    result.scene = input.scene; result.frame = input.frame; result.delta = input.delta; result.time = input.time;
    result.alternative_cockpit = input.alternative_cockpit; result.supersampling = input.supersampling;
    for (unsigned i = 0; i < input.count; ++i) {
        auto view = input.views[i];
        view.embedded = input.cockpit_visible && input.cockpit_state == COCKPIT_STATE_FUNCTIONAL;
        if (view.embedded) {
            const uint64_t region = view.slot == SVW_LEFT ? SceneCockpitPrimaryView : SceneCockpitSecondaryView;
            const auto found = std::find_if(world.data().view_regions.begin(),world.data().view_regions.end(),
                [&](const d3scene::ViewRegion& candidate) { return candidate.identity == region; });
            if (found == world.data().view_regions.end()) continue;
            view.rectangle = found->rectangle;
        } else {
            view.rectangle = AuxiliaryWindowRectangle(input.window,view.slot,(view.flags & SVF_BIGGER) != 0);
            view.outlined = true; view.outline = (view.flags & SVF_POPUP) ? GR_RGB(255,0,0) : GR_RGB(0,0,255);
        }
        const auto rectangle = view.rectangle;
        if (rectangle.width <= 0 || rectangle.height <= 0) continue;
        if (!(view.flags & SVF_STATIC)) {
            view.world.viewport = rectangle; view.world.projection_anchor = {0,0,rectangle.width,rectangle.height};
            view.world.camera_cut = AdmittedRevisions[view.slot] != view.revision;
            AdmittedRevisions[view.slot] = view.revision;
        }
        if (view.label[0]) QueuePostProcessHUDText(view.label,rectangle.x+rectangle.width/2,rectangle.y+2,true,
            HUD_FONT,view.font_scale,HUD_COLOR,255,0);
        result.admitted |= 1u<<view.slot;
        result.views[result.count++] = std::move(view);
    }
    Scheduled = std::move(result); SchedulePublication = world.data().sequence;
    return Scheduled;
}

bool CommitGameAuxiliaryTimers(const GameAuxiliarySchedule& schedule) {
    if (schedule.scene != d3scene::assets().lifetime() || schedule.frame != uint64_t(FrameCount) ||
        !std::isfinite(schedule.delta) || schedule.delta < 0)
        throw std::invalid_argument("Auxiliary timer commit belongs to another game frame");
    if (!TimerClock.begin(schedule.scene,schedule.frame)) return false;
    for (unsigned i = 0; i < schedule.count; ++i) {
        const auto& view = schedule.views[i];
        if (view.slot < 0 || view.slot >= int(Count) || Revisions[view.slot] != view.revision) continue;
        if (AdvanceAuxiliaryTimer(Small_views[view.slot],true,schedule.delta)) CloseSmallView(view.slot);
    }
    return true;
}

void DrawSmallViews(const d3scene::Presentation& world,SceneFeedbackTransaction& feedback,const GameAuxiliarySchedule& schedule) {
    if (schedule.scene != world.data().scene_lifetime || schedule.world_sequence != world.data().sequence)
        throw std::invalid_argument("Auxiliary schedule belongs to another publication");
    for (unsigned i = 0; i < schedule.count; ++i)
        if (!(schedule.views[i].flags & SVF_STATIC)) RenderGameSceneView(world,schedule.views[i].world,&feedback);
}

void PrepareGameAuxiliaryCanvas(d3scene::FrameData& frame,const GameAuxiliarySchedule& schedule) {
    if (schedule.scene != frame.scene_lifetime) throw std::invalid_argument("Foreign auxiliary canvas");
    d3scene::PreparedForeground foreground;
    foreground.identity = DecorationCollection; foreground.order = 34;
    foreground.phase = d3scene::ForegroundPhase::AfterWorldViews;
    foreground.layer.composition = d3scene::ForegroundComposition::InScene;
    for (unsigned i = 0; i < schedule.count; ++i) {
        const auto& view = schedule.views[i];
        const bool stripes = view.embedded && schedule.alternative_cockpit;
        if (!(view.flags & (SVF_STATIC|SVF_CROSSHAIRS)) && !view.outlined && !stripes) continue;
        const uint64_t collection = DecorationCollection+uint64_t(view.slot)+1;
        const int width = view.rectangle.width,height = view.rectangle.height;
        int32_t order = 0;
        const auto quad = [&](d3scene::Rect rectangle,d3scene::Vec3 color) {
            SceneQuadStyle style; style.color = color; PrepareSceneQuad(frame,collection,order,rectangle,style);
        };
        if (view.flags & SVF_STATIC) {
            SceneQuadStyle film; film.bitmap = view.bitmap; film.texture_filter = d3scene::TextureFilter::Nearest;
            PrepareSceneQuad(frame,collection,order,{0,0,width,height},film);
        }
        if (stripes) {
            SceneQuadStyle black; black.color = {}; black.alpha = .2f*.7f;
            PrepareSceneQuad(frame,collection,order,{0,0,width,height},black);
            frame.meshes.back().surface.raster_stripe = {3.f*schedule.supersampling,schedule.time*14,.5f};
        }
        if (view.outlined) {
            const d3scene::Vec3 color{GR_COLOR_RED(view.outline)/255.f,GR_COLOR_GREEN(view.outline)/255.f,GR_COLOR_BLUE(view.outline)/255.f};
            quad({0,0,width,1},color); quad({0,height-1,width,1},color);
            quad({0,0,1,height},color); quad({width-1,0,1,height},color);
        }
        if (view.flags & SVF_CROSSHAIRS) {
            quad({width/2-width/50,height/2,width/50*2+1,1},{0,1,0});
            quad({width/2,height/2-height/50,1,height/50*2+1},{0,1,0});
        }
        auto canvas = MakeSceneCanvasView(collection,view.rectangle,d3scene::ViewTarget::Scene,SceneGeometryInspection());
        canvas.motion_composition = d3scene::View::MotionComposition::Stationary;
        foreground.layer.scene_views.push_back(std::move(canvas));
    }
    if (!foreground.layer.scene_views.empty()) frame.foregrounds.push_back(std::move(foreground));
    if (AutomatedCapturePending()) AutomatedCaptureLog("scene auxiliary snapshot frame=%llu views=%u admitted=%u",
        static_cast<unsigned long long>(schedule.frame),schedule.count,schedule.admitted);
    if (AutomatedCapturePending()) for (unsigned i = 0; i < schedule.count; ++i) {
        const auto& view = schedule.views[i]; const auto r = view.rectangle;
        AutomatedCaptureLog("scene auxiliary value slot=%d owner=%u static=%d rear=%u rectangle=%d,%d,%d,%d timer=%.6g cut=%d",
            view.slot,view.owner,(view.flags&SVF_STATIC) != 0,view.world.camera_variant,r.x,r.y,r.width,r.height,view.timer,view.world.camera_cut);
    }
}
