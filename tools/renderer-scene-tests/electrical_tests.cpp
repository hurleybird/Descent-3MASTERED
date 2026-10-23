// Descent 3MASTERED - GPL-3.0-or-later
#include "../../Descent3/GameElectricalWeapons.h"
#include "../../renderer/Scene.h"
#include "object.h"
#include "weapon.h"
#include "player.h"
#include "game.h"
#include "findintersection.h"
#include "viseffect.h"
#include "psrand.h"
#include <stdexcept>

weapon Weapons[MAX_WEAPONS]{};
namespace {
int Traces = 0, ImpactCalls = 0;
bool Wall = true;
vector WallPoint{0,0,12};
}
int FindWeaponName(char*) { return 1; }
bool WeaponCalcGun(vector* point,vector* direction,object* source,int) {
    *point = source->pos;
    if (direction) *direction = source->orient.fvec;
    return true;
}
int fvi_FindIntersection(fvi_query* query,fvi_info* hit,bool) {
    ++Traces;
    if (Wall) { hit->hit_pnt = WallPoint; return HIT_WALL; }
    hit->hit_pnt = *query->p1;
    return HIT_NONE;
}
void CreateRandomSparksWithRandom(PSRand& random,int count,vector*,int,int,float) {
    ++ImpactCalls;
    for (int i = 0; i < count; ++i) random();
}
int runElectricalTests() {
    int checks = 0;
    const auto check = [&](bool valid) { ++checks; if (!valid) throw std::runtime_error("Game-owned electrical pulse contract"); };
    auto scene = d3scene::assets().lifetime();
    Highest_object_index = 2;
    for (int i = 0; i < 3; ++i) {
        auto& object = Objects[i]; object = {};
        object.type = i ? OBJ_WEAPON : OBJ_PLAYER;
        object.handle = HANDLE_COUNT_INCREMENT|i;
        object.orient.rvec.x = object.orient.uvec.y = object.orient.fvec.z = 1;
        object.id = i ? 1 : 0; object.lifeleft = 5;
        if (i) {
            object.parent_handle = Objects[0].handle;
            object.ctype.laser_info.track_handle = OBJECT_HANDLE_NONE;
            object.ctype.laser_info.src_gun_num = 0;
        }
    }
    Weapons[1].flags = WF_ELECTRICAL; Players[0].flags = 0;
    Traces = 0; ImpactCalls = 0; Wall = true; Gametime = 5;
    check(AdvanceGameElectricalWeapons(scene,90000,true) == 0 && Traces == 0 && Objects[1].lifeleft == 5);
    check(AdvanceGameElectricalWeapons(scene,90000,false) == 2 && Traces == 2 && ImpactCalls == 1);
    auto* pulse = FindGameElectricalPulse(scene,Objects[1].handle);
    check(pulse && pulse->source.z == 0 && pulse->target.z == 12 && pulse->omega && pulse->source_owner == uint32_t(Objects[0].handle));
    check(Objects[1].lifeleft == 0 && Objects[2].lifeleft == 0);
    Objects[1].lifeleft = 7;
    check(AdvanceGameElectricalWeapons(scene,90000,false) == 0 && Traces == 2 && ImpactCalls == 1 && Objects[1].lifeleft == 7);
    check(!FindGameElectricalPulse(scene+1,Objects[1].handle));
    check(!FindGameElectricalPulse(scene,Objects[1].handle+HANDLE_COUNT_INCREMENT));
    const int old = Objects[1].handle; Objects[1].handle += HANDLE_COUNT_INCREMENT;
    check(!FindGameElectricalPulse(scene,old));
    Wall = false; Gametime += Frametime;
    check(AdvanceGameElectricalWeapons(scene,90001,false) == 2 && ImpactCalls == 1);
    pulse = FindGameElectricalPulse(scene,Objects[1].handle);
    check(pulse && pulse->target.z == 50);
    Players[0].flags = PLAYER_FLAGS_DYING;
    check(AdvanceGameElectricalWeapons(scene,90002,false) == 0 && !FindGameElectricalPulse(scene,Objects[1].handle));
    Players[0].flags = 0;
    Objects[2].type = OBJ_PLAYER; Objects[2].id = 1; Players[1].flags = 0;
    Objects[2].pos = {0,0,20}; Objects[1].ctype.laser_info.track_handle = Objects[2].handle;
    check(AdvanceGameElectricalWeapons(scene,90003,false) == 1);
    check(FindGameElectricalPulse(scene,Objects[1].handle)->target.z == 19); // view-independent player contact
    Objects[2].pos.z = .1f;
    check(AdvanceGameElectricalWeapons(scene,90004,false) == 0);
    bool rejected = false;
    try { AdvanceGameElectricalWeapons(scene,90003,false); } catch (const std::logic_error&) { rejected = true; }
    check(rejected);
    ++scene; Objects[2].pos.z = 20;
    check(AdvanceGameElectricalWeapons(scene,0,false) == 1 && FindGameElectricalPulse(scene,Objects[1].handle));
    for (int i = 0; i < 3; ++i) Objects[i] = {};
    Weapons[1] = {}; Highest_object_index = -1; Gametime = 0;
    return checks;
}
