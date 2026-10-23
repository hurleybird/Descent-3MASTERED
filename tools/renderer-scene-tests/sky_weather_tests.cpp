// Descent 3MASTERED - GPL-3.0-or-later
#include "../../Descent3/GameSkyWeather.h"
#include "weather.h"
#include <cmath>
#include <limits>
#include <stdexcept>

int runSkyWeatherTests() {
    int checks = 0;
    const auto check = [&](bool valid) { ++checks; if (!valid) throw std::runtime_error("Game-owned sky weather contract"); };
    GameSkyWeatherController controller;
    weather state{}; state.flags = WEATHER_FLAGS_LIGHTNING; state.lightning_sequence = 1;
    matrix orientation{}; orientation.rvec.x = orientation.uvec.y = orientation.fvec.z = 1;
    const vector position{0,200,0};
    check(!controller.find(7));
    check(controller.advance(7,10,false,state,500,position,orientation));
    check(state.lightning_sequence == 2 && controller.find(7)->phase == 1 && !controller.find(7)->count);
    check(!controller.advance(7,10,false,state,500,position,orientation) && state.lightning_sequence == 2);
    check(!controller.advance(7,11,true,state,500,position,orientation) && controller.find(7)->phase == 1);
    check(controller.advance(7,11,false,state,500,position,orientation));
    const auto bolt = *controller.find(7);
    check(state.lightning_sequence == 0 && bolt.phase == 2 && bolt.count > 0 && !bolt.truncated && bolt.camera_owner == 500);
    for (unsigned i = 0; i < bolt.count; ++i)
        check(std::isfinite(bolt.segments[i].to.x) && bolt.segments[i].to.y < bolt.segments[i].from.y);
    check(!controller.advance(7,11,false,state,500,position,orientation) && controller.find(7)->count == bolt.count);
    GameSkyWeatherController repeat; weather same{}; same.flags = WEATHER_FLAGS_LIGHTNING; same.lightning_sequence = 2;
    repeat.advance(7,11,false,same,500,position,orientation);
    const auto* repeated = repeat.find(7);
    check(repeated->count == bolt.count && repeated->origin_type == bolt.origin_type);
    for (unsigned i = 0; i < bolt.count; ++i)
        check(repeated->segments[i].to.x == bolt.segments[i].to.x && repeated->segments[i].to.y == bolt.segments[i].to.y);
    check(controller.advance(7,12,false,state,500,position,orientation) && controller.find(7)->phase == 0);
    bool rejected = false;
    try { controller.advance(7,11,false,state,500,position,orientation); } catch (const std::logic_error&) { rejected = true; }
    check(rejected);
    check(!controller.advance(8,0,true,state,500,position,orientation) && !controller.find(7) && controller.find(8)->phase == 0);
    state.lightning_sequence = 2; state.flags = 0;
    check(controller.advance(8,1,false,state,500,position,orientation) && !state.lightning_sequence && !controller.find(8)->phase);
    state.flags = WEATHER_FLAGS_LIGHTNING; state.lightning_sequence = 2;
    const vector high{0,1.e7f,0}; controller.advance(8,2,false,state,500,high,orientation);
    check(controller.find(8)->count == 512 && controller.find(8)->truncated);
    state.lightning_sequence = 2;
    const vector invalid{0,std::numeric_limits<float>::infinity(),0};
    controller.advance(8,3,false,state,500,invalid,orientation);
    check(!controller.find(8)->phase && !controller.find(8)->count);
    state.lightning_sequence = 2;
    controller.advance(8,4,false,state,0,position,orientation);
    check(!controller.find(8)->phase && !state.lightning_sequence); // no physical camera cannot leave a pending strike
    return checks;
}
