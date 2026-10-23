// Descent 3MASTERED - GPL-3.0-or-later
#include "GameElectricalWeapons.h"
#include "GameVisualClock.h"
#include "object.h"
#include "weapon.h"
#include "player.h"
#include "game.h"
#include "findintersection.h"
#include "viseffect.h"
#include "fireball.h"
#include "psrand.h"
#include <stdexcept>

namespace {
GameElectricalPulse Pulses[MAX_OBJECTS];
GameVisualClock Sparks[MAX_OBJECTS][MAX_WB_GUNPOINTS];
uint64_t LastScene = 0, LastFrame = 0;
bool Advanced = false;
}
const GameElectricalPulse* FindGameElectricalPulse(uint64_t scene,uint32_t owner) {
    const auto index = owner & HANDLE_OBJNUM_MASK;
    if (index >= MAX_OBJECTS) return nullptr;
    const auto& pulse = Pulses[index];
    return pulse.valid && pulse.scene == scene && pulse.owner == owner && ObjGet(int(owner)) ? &pulse : nullptr;
}
unsigned AdvanceGameElectricalWeapons(uint64_t scene,uint64_t frame,bool paused) {
    if (paused) return 0;
    if (Advanced && LastScene == scene) {
        if (frame == LastFrame) return 0;
        if (frame < LastFrame) throw std::logic_error("Electrical weapon frame reordered");
    }
    Advanced = true; LastScene = scene; LastFrame = frame;
    int omega = -2;
    unsigned resolved = 0;
    for (int i = 0; i <= Highest_object_index; ++i) {
        auto& pulse = Pulses[i]; pulse.valid = false;
        auto& weapon = Objects[i];
        if (weapon.type != OBJ_WEAPON || (weapon.flags & OF_DEAD) || weapon.id < 0 || weapon.id >= MAX_WEAPONS ||
            !(Weapons[weapon.id].flags & WF_ELECTRICAL)) continue;
        // A pulse lasts one simulation interval, including when it is outside
        // every camera. Draw calls no longer own its lifetime.
        weapon.lifeleft = 0;
        auto* parent = ObjGet(weapon.parent_handle);
        if (!parent || (parent->flags & (OF_DEAD|OF_DYING))) continue;
        if (parent->type == OBJ_PLAYER && (Players[parent->id].flags & (PLAYER_FLAGS_DYING|PLAYER_FLAGS_DEAD))) continue;
        vector direction;
        WeaponCalcGun(&pulse.source,&direction,parent,weapon.ctype.laser_info.src_gun_num);
        auto* target = ObjGet(weapon.ctype.laser_info.track_handle);
        pulse.target = target ? target->pos : pulse.source+weapon.orient.fvec*50;
        // Preserve the authored near-player contact correction, but apply it
        // to any player target rather than the current global viewer.
        if (target && target->type == OBJ_PLAYER) {
            auto back = pulse.source-pulse.target;
            const float length = vm_NormalizeVector(&back);
            if (length < .5f) continue;
            pulse.target += back;
        }
        fvi_query query{}; fvi_info hit{};
        query.p0 = &pulse.source; query.p1 = &pulse.target;
        query.startroom = weapon.roomnum; query.thisobjnum = -1;
        const int fate = fvi_FindIntersection(&query,&hit);
        if (fate == HIT_WALL || fate == HIT_TERRAIN) {
            pulse.target = hit.hit_pnt;
            const int gun = weapon.ctype.laser_info.src_gun_num;
            if (gun >= 0 && gun < MAX_WB_GUNPOINTS) {
                GameVisualEvent events[8];
                const int count = Sparks[OBJNUM(parent)][gun].consume(scene,parent->handle,Gametime,Frametime,1,events,8);
                for (int e = 0; e < count; ++e) {
                    PSRand random(uint32_t(parent->handle)*0x9e3779b9u ^ uint32_t(events[e].tick)*0x85ebca6bu ^ uint32_t(gun+1));
                    const int sparks = random()%4;
                    CreateRandomSparksWithRandom(random,sparks,&pulse.target,weapon.roomnum,COOL_SPARK_INDEX,1);
                }
            }
        }
        if (vm_VectorDistance(&pulse.source,&pulse.target) < .5f) continue;
        if (omega == -2) omega = FindWeaponName("Omega");
        pulse.scene = scene; pulse.owner = weapon.handle; pulse.source_owner = parent->handle;
        pulse.omega = weapon.id == omega; pulse.valid = true; ++resolved;
    }
    return resolved;
}
