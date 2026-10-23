// Descent 3MASTERED - GPL-3.0-or-later
#include "../../Descent3/GameRoomCoronas.h"
#include <limits>

int runRoomCoronaTests() {
    int checks = 0;
    const auto check = [&](bool v) { ++checks; if (!v) throw std::runtime_error("Room corona exposure contract"); };
    GameRoomCorona source; source.geometry = {7,1,3}; source.appearance = 2; source.room = 3; source.polygon = 0;
    source.size = 2; source.normal = {0,0,1}; source.color = {1,.5f,.25f};
    check(GameCoronaOpacity(source,{0,0,9},1,1) == 0);
    check(GameCoronaOpacity(source,{0,0,10},1,1) == 0);
    check(GameCoronaOpacity(source,{0,0,40},1,1) == .4f);
    check(GameCoronaOpacity(source,{0,0,40},.5f,.5f) == .2f);
    check(GameCoronaOpacity(source,{0,0,-100},1,1) == 0);
    check(GameCoronaOpacity(source,{0,0,40},1,0) == 0);
    GameRoomCoronaBatch candidates; candidates.count = 1; candidates.values[0] = source;
    GameRoomCoronaClock clock;
    check(clock.advance(7,0,.125f,candidates) && clock.count() == 1 && clock[0].fade == 0);
    check(!clock.advance(7,0,.125f,candidates) && clock[0].fade == 0);
    clock.advance(7,1,0,candidates); check(clock[0].fade == .5f);
    clock.advance(7,2,.125f,candidates); check(clock[0].fade == .5f);
    clock.advance(7,3,.1f,candidates); check(clock[0].fade == 1);
    GameRoomCoronaBatch empty;
    clock.advance(7,4,.125f,empty); check(clock[0].fade == 1);
    clock.advance(7,5,.125f,empty); check(clock[0].fade == .5f);
    clock.advance(7,6,.125f,empty); check(clock.count() == 0);
    clock.advance(7,7,.125f,candidates); clock.advance(7,8,.125f,candidates); check(clock[0].fade == .5f);
    ++candidates.values[0].geometry.revision;
    clock.advance(7,9,.125f,candidates); check(clock.count() == 1 && clock[0].fade == 0);
    ++candidates.values[0].appearance;
    clock.advance(7,10,.125f,candidates); check(clock.count() == 1 && clock[0].fade == 0);
    ++candidates.values[0].materials;
    clock.advance(7,11,.125f,candidates); check(clock.count() == 1 && clock[0].fade == 0);
    candidates.values[0].geometry.lifetime = 8;
    clock.advance(8,0,.125f,candidates); check(clock.count() == 1 && clock[0].fade == 0);
    bool rejected = false;
    try { clock.advance(8,1,-1,candidates); } catch (const std::invalid_argument&) { rejected = true; } check(rejected);
    clock.advance(8,1,0,candidates); check(clock[0].fade == .5f);
    rejected = false;
    try { clock.advance(8,0,0,candidates); } catch (const std::logic_error&) { rejected = true; } check(rejected);
    rejected = false; candidates.values[0].size = std::numeric_limits<float>::quiet_NaN();
    try { clock.advance(8,2,0,candidates); } catch (const std::invalid_argument&) { rejected = true; } check(rejected);
    candidates.values[0] = source; candidates.count = GameRoomCoronaLimit+1; rejected = false;
    try { clock.advance(8,2,0,candidates); } catch (const std::invalid_argument&) { rejected = true; } check(rejected);
    return checks;
}
