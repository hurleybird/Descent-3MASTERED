// Descent 3MASTERED - GPL-3.0-or-later
#include "../../Descent3/GameVisualClock.h"
#include "../../Descent3/GameVisualEffects.h"
#include "../../Descent3/SceneFeedback.h"
#include "../../Descent3/SceneExtraction.h"
#include "object.h"
#include "objinfo.h"
#include "object_lighting.h"
#include "polymodel.h"
#include "room.h"
#include "game.h"
#include "viseffect.h"
#include "fireball.h"
#include <stdexcept>

// The real emitter is linked with game allocation/pose boundaries only. The
// existing test ps_rand() throws, so any shared gameplay RNG use fails here.
object_info Object_info[MAX_OBJECT_IDS]{};
poly_model Poly_models[MAX_POLY_MODELS]{};
float Frametime = 1.f/60;
namespace {
vis_effect Effects[4096]{};
int Created = 0, Poses = 0;
bool FailAllocation = false;
}
vis_effect* VisEffects = Effects;
int VisEffectCreate(ubyte type,ubyte id,int room,vector* position) {
    if (FailAllocation) return -1;
    if (Created >= 4096) throw std::runtime_error("Visual emitter fixture exhausted");
    auto& effect = Effects[Created]; effect = {};
    effect.type = type; effect.id = id; effect.roomnum = room; effect.pos = *position;
    effect.creation_time = Gametime;
    return Created++;
}
light_info* ObjGetLightInfo(object*) { return nullptr; }
int IsNonRenderableSubmodel(poly_model*,int) { return 0; }
void SetNormalizedTimeObj(object*,float*) { ++Poses; }
void GetPolyModelPointInWorld(vector* result,poly_model*,vector* position,matrix*,int,float*,vector* point,vector*) {
    *result = *position+*point;
}

int runGameVisualTests() {
    int checks = 0;
    const auto check = [&](bool valid) { ++checks; if (!valid) throw std::runtime_error("Game visual emitter contract"); };
    GameVisualClock clock;
    GameVisualEvent events[8];
    check(clock.consume(1,7,1.f,.016f,1,events,8) == 1 && events[0].tick == 60);
    check(clock.consume(1,7,1.f,.016f,1,events,8) == 0);
    check(clock.consume(1,7,1.05f,.05f,1,events,8) == 2); // float time is just below tick63
    check(clock.consume(1,7,1.2f,.016f,1,events,8) == 1); // gap: no backlog
    check(clock.consume(1,8,1.2f,.016f,1,events,8) == 1); // allocation recycled
    check(clock.consume(2,8,1.2f,.016f,1,events,8) == 1); // same handle, new scene
    check(clock.consume(2,8,.5f,.016f,1,events,8) == 1); // time rewound
    check(clock.consume(2,8,2.f,1.f,1,events,8) == 8 && events[0].tick == 113); // bounded catchup
    GameVisualClock sparkle_clock;
    check(sparkle_clock.consume(1,1,1.f,.016f,2,events,8) == 1);
    check(sparkle_clock.consume(1,1,1.02f,.02f,2,events,8) == 0);
    check(sparkle_clock.consume(1,1,1.04f,.02f,2,events,8) == 1 && events[0].tick == 62);
    clock.reset();
    check(clock.consume(2,8,2.02f,.02f,1,events,8) == 1); // explicit admission break

    ResetGamePresentationScene();
    const auto epoch = d3scene::assets().lifetime();
    Rooms[7] = {}; Rooms[7].used = 1;
    vector vertices[3]{{0,0,0},{1,0,0},{0,1,0}};
    bsp_info submodel{}; submodel.nverts = 3; submodel.verts = vertices;
    Poly_models[0].n_models = 1; Poly_models[0].submodel = &submodel;
    effect_info_s virus{}; virus.type_flags = EF_VIRUS_INFECTED;
    Highest_object_index = 2;
    for (int i = 0; i < 3; ++i) {
        auto& object = Objects[i]; object = {};
        object.type = i ? OBJ_ROBOT : OBJ_POWERUP;
        object.handle = HANDLE_COUNT_INCREMENT|i; object.roomnum = 7;
        object.render_type = RT_POLYOBJ; object.size = 1;
        object.orient.rvec.x = object.orient.uvec.y = object.orient.fvec.z = 1;
        if (i == 2) object.effect_info = &virus;
    }
    Object_info[0].hit_points = 100;
    Render_powerup_sparkles = true;
    Created = 0; Poses = 0; Gametime = 1;
    // Only actual committed observations may enable the next update. A world
    // publication/thumbnail alone does not enable emissions.
    FrameCount = 70000;
    const auto unobserved = AdvanceGameVisualEffects(epoch,FrameCount,false);
    check(unobserved.advanced && unobserved.objects == 0 && Created == 0);
    const auto observe = [&] {
        const auto frame = ExtractGamePresentation(Gametime,Frametime,false);
        SceneFrameFeedback report; report.scene_lifetime = epoch; report.frame_sequence = frame->data().sequence;
        report.objects = {uint32_t(Objects[0].handle),uint32_t(Objects[1].handle),uint32_t(Objects[2].handle)};
        CommitGameVisibility(*frame,report,FrameCount);
    };
    unsigned sparkles = 0,damage = 0,infected = 0;
    for (int tick = 0; tick < 180; ++tick) {
        observe(); ++FrameCount; Gametime += Frametime;
        const int before = Created;
        check(!AdvanceGameVisualEffects(epoch,FrameCount,true).advanced && Created == before);
        const auto stats = AdvanceGameVisualEffects(epoch,FrameCount,false);
        sparkles += stats.sparkles; damage += stats.damage; infected += stats.virus;
        check(stats.advanced && stats.objects == 3);
        const int emitted = Created;
        check(!AdvanceGameVisualEffects(epoch,FrameCount,false).advanced && Created == emitted); // replay/views
    }
    check(sparkles > 0 && damage > 0 && infected > 0 && Poses > 0);
    unsigned attached = 0;
    for (int i = 0; i < Created; ++i) {
        const auto& effect = Effects[i];
        check(effect.lifeleft > 0 && effect.lifeleft <= effect.lifetime);
        if (effect.flags & VF_ATTACHED) {
            ++attached;
            check(effect.attach_info.obj_handle == Objects[1].handle || effect.attach_info.obj_handle == Objects[2].handle);
            check(effect.attach_info.modelnum == 0 && effect.attach_info.vertnum < 3 && effect.attach_info.end_vertnum < 3);
            check(effect.pos.x == vertices[effect.attach_info.vertnum].x && effect.end_pos.y == vertices[effect.attach_info.end_vertnum].y);
        }
    }
    check(attached == damage+infected);
    ++FrameCount; Gametime += Frametime; // no observation: do not reuse a stale visibility report
    const int before = Created;
    check(AdvanceGameVisualEffects(epoch,FrameCount,false).objects == 0 && Created == before);
    bool rejected = false;
    try { AdvanceGameVisualEffects(epoch,FrameCount-1,false); } catch (const std::logic_error&) { rejected = true; }
    check(rejected);
    observe(); ++FrameCount; Gametime += Frametime;
    FailAllocation = true;
    check(AdvanceGameVisualEffects(epoch,FrameCount,false).advanced && Created == before);
    FailAllocation = false;
    ResetGamePresentationScene();
    check(AdvanceGameVisualEffects(d3scene::assets().lifetime(),0,false).objects == 0);
    for (int i = 0; i < 3; ++i) Objects[i] = {};
    Rooms[7] = {}; Poly_models[0] = {}; Object_info[0] = {};
    Highest_object_index = -1; FrameCount = 0; Gametime = 0; Render_powerup_sparkles = false;
    return checks;
}
