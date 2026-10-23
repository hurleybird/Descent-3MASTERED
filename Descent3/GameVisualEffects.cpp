// Descent 3MASTERED - GPL-3.0-or-later
#include "GameVisualEffects.h"
#include "GameVisualClock.h"
#include "SceneFeedback.h"
#include "object.h"
#include "objinfo.h"
#include "object_lighting.h"
#include "polymodel.h"
#include "player.h"
#include "game.h"
#include "viseffect.h"
#include "fireball.h"
#include "psrand.h"
#include <stdexcept>

bool Render_powerup_sparkles = false;
namespace {
enum Channel { Sparkles, Damage, Virus, ChannelCount };
GameVisualClock Clocks[MAX_OBJECTS][ChannelCount];
uint64_t LastScene = 0, LastFrame = 0;
bool HasAdvanced = false;

PSRand randomFor(uint32_t owner,int tick,Channel channel) {
    // Event-local stream: adding a view, another emitter, or a failed allocation
    // cannot change this event or the simulation's shared random sequence.
    uint32_t key = owner*0x9e3779b9u ^ uint32_t(tick)*0x85ebca6bu ^ uint32_t(channel+1)*0xc2b2ae35u;
    key ^= key>>16; key *= 0x7feb352du; key ^= key>>15;
    return PSRand(key);
}
unsigned emitSparkles(object& obj,const GameVisualEvent& event) {
    auto random = randomFor(obj.handle,event.tick,Sparkles);
    unsigned created = 0;
    const int count = random()&7;
    for (int i = 0; i < count; ++i) {
        const int choice = random()%10;
        const int kind = choice == 0 ? HOT_SPARK_INDEX : choice == 1 ? COOL_SPARK_INDEX : GRAY_SPARK_INDEX;
        vector offset{float(random()%100-50),float(random()%100-80),float(random()%100-50)};
        vm_NormalizeVector(&offset);
        auto position = obj.last_pos+offset*(obj.size*.3f);
        vector velocity{float(random()%50-25),float(-10-random()%20),float(random()%50-25)};
        vm_NormalizeVector(&velocity); velocity *= 3.f+random()%4;
        const float size = .05f+(random()%10)*.008f;
        const float lifetime = .25f+(random()%10)*.05f;
        if (event.age >= lifetime) continue;
        const int index = VisEffectCreate(VIS_FIREBALL,kind,obj.roomnum,&position);
        if (index < 0) continue;
        auto& vis = VisEffects[index];
        vis.end_pos = vis.pos; vis.movement_type = MT_PHYSICS;
        vis.mass = 1000; vis.drag = .2f; vis.phys_flags |= PF_GRAVITY|PF_NO_COLLIDE;
        vis.velocity = velocity; vis.size = size;
        vis.flags |= VF_USES_LIFELEFT|VF_NO_SOFT_PARTICLES;
        vis.creation_time -= event.age; vis.lifetime = lifetime; vis.lifeleft = lifetime-event.age;
        const auto* light = ObjGetLightInfo(&obj);
        vis.lighting_color = light ? GR_RGB16(light->red_light1*255,light->green_light1*255,light->blue_light1*255) : GR_RGB16(255,255,255);
        ++created;
    }
    return created;
}
bool emitLightning(object& obj,poly_model& model,const GameVisualEvent& event,Channel channel,
    int damage_modulo,float* normalized_time,bool& pose_ready) {
    auto random = randomFor(obj.handle,event.tick,channel);
    const int modulo = channel == Virus ? (std::max)(1,int(20-16*(1.f-float(random()%RAND_MAX)/RAND_MAX))) : damage_modulo;
    if (random()%modulo || event.age >= 1 || model.n_models <= 0) return false;
    const int sub = random()%model.n_models;
    if (IsNonRenderableSubmodel(&model,sub)) return false;
    auto& geometry = model.submodel[sub];
    if (geometry.nverts <= 0 || !geometry.verts) return false;
    const int start = random()%geometry.nverts, end = random()%geometry.nverts;
    const int index = VisEffectCreate(VIS_FIREBALL,channel == Virus ? GRAY_LIGHTNING_BOLT_INDEX : LIGHTNING_BOLT_INDEX,obj.roomnum,&obj.pos);
    if (index < 0) return false;
    auto& vis = VisEffects[index];
    vis.lifeleft = 1-event.age; vis.lifetime = 1; vis.creation_time -= event.age;
    vis.velocity.x = .15f; vis.velocity.y = 3;
    vis.attach_info.obj_handle = obj.handle; vis.attach_info.subnum = sub;
    vis.attach_info.vertnum = start; vis.attach_info.end_vertnum = end;
    vis.attach_info.modelnum = obj.rtype.pobj_info.model_num;
    vis.flags = VF_USES_LIFELEFT|VF_EXPAND|VF_ATTACHED;
    if (channel == Virus) vis.lighting_color = GR_RGB16(40,250,40);
    // These are born after VisEffectMoveAll. Resolve the initial attachment now,
    // once per object pose, instead of publishing a zero-length first frame.
    if (!pose_ready) { SetNormalizedTimeObj(&obj,normalized_time); pose_ready = true; }
    GetPolyModelPointInWorld(&vis.pos,&model,&obj.pos,&obj.orient,sub,normalized_time,&geometry.verts[start],nullptr);
    GetPolyModelPointInWorld(&vis.end_pos,&model,&obj.pos,&obj.orient,sub,normalized_time,&geometry.verts[end],nullptr);
    return true;
}
}

GameVisualEmissionStats AdvanceGameVisualEffects(uint64_t scene,uint64_t frame,bool paused) {
    GameVisualEmissionStats stats;
    if (paused) return stats;
    if (HasAdvanced && LastScene == scene) {
        if (frame == LastFrame) return stats;
        if (frame < LastFrame) throw std::logic_error("Game visual effects frame reordered");
    }
    HasAdvanced = true; LastScene = scene; LastFrame = frame; stats.advanced = true;
    for (int i = 0; i <= Highest_object_index; ++i) {
        auto& obj = Objects[i];
        if (obj.type == OBJ_NONE || (obj.flags & OF_DEAD) || obj.render_type != RT_POLYOBJ ||
            !GameObjectWasObservedLastFrame(scene,frame,uint32_t(obj.handle))) {
            for (auto& clock : Clocks[i]) clock.reset();
            continue;
        }
        ++stats.objects;
        GameVisualEvent events[8];
        if (obj.type == OBJ_POWERUP && Render_powerup_sparkles) {
            const int count = Clocks[i][Sparkles].consume(scene,obj.handle,Gametime,Frametime,2,events,8);
            for (int e = 0; e < count; ++e) stats.sparkles += emitSparkles(obj,events[e]);
        } else Clocks[i][Sparkles].reset();
        if (obj.type != OBJ_PLAYER && obj.type != OBJ_ROBOT && !(obj.type == OBJ_BUILDING && obj.ai_info)) continue;
        const int model_id = obj.rtype.pobj_info.model_num;
        if (model_id < 0 || model_id >= MAX_POLY_MODELS) continue;
        auto& model = Poly_models[model_id];
        float normalized_time[MAX_SUBOBJECTS]{}; bool pose_ready = false;
        const float maximum = obj.type == OBJ_PLAYER ? INITIAL_SHIELDS :
            obj.id >= 0 && obj.id < MAX_OBJECT_IDS ? float(Object_info[obj.id].hit_points) : 0;
        if (maximum > 0 && obj.shields >= 0 && obj.shields < maximum*.3f) {
            const int modulo = (std::max)(1,int(20-12*(1-obj.shields/(maximum*.3f))));
            const int count = Clocks[i][Damage].consume(scene,obj.handle,Gametime,Frametime,1,events,8);
            for (int e = 0; e < count; ++e) if (emitLightning(obj,model,events[e],Damage,modulo,normalized_time,pose_ready)) ++stats.damage;
        } else Clocks[i][Damage].reset();
        if (obj.effect_info && (obj.effect_info->type_flags & EF_VIRUS_INFECTED)) {
            const int count = Clocks[i][Virus].consume(scene,obj.handle,Gametime,Frametime,1,events,8);
            for (int e = 0; e < count; ++e) if (emitLightning(obj,model,events[e],Virus,1,normalized_time,pose_ready)) ++stats.virus;
        } else Clocks[i][Virus].reset();
    }
    return stats;
}
