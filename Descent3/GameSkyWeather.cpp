// Descent 3MASTERED - GPL-3.0-or-later
#include "GameSkyWeather.h"
#include "weather.h"
#include "psrand.h"
#include <cmath>
#include <stdexcept>

bool GameSkyWeatherController::advance(uint64_t scene,uint64_t frame,bool paused,weather& state,
    uint32_t owner,const vector& position,const matrix& orientation) {
    if (advanced_ && current_.scene == scene) {
        if (paused || frame == current_.frame) return false;
        if (frame < current_.frame) throw std::logic_error("Sky weather frame reordered");
    }
    // Retire foreign-scene data even when the new scene is paused.
    current_.scene = scene; current_.frame = frame;
    current_.phase = current_.count = 0; current_.camera_owner = 0;
    current_.truncated = false; current_.origin = {};
    current_.origin_size = 0; current_.origin_type = -1;
    advanced_ = true;
    if (paused) return false;
    if (!(state.flags & WEATHER_FLAGS_LIGHTNING)) { state.lightning_sequence = 0; return true; }
    const unsigned phase = state.lightning_sequence;
    state.lightning_sequence = phase == 1 ? 2 : 0;
    if (phase != 1 && phase != 2) return true;
    current_.phase = phase; current_.camera_owner = owner;
    if (phase == 1) return true;
    if (!owner || !std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z)) {
        current_.phase = 0; return true;
    }
    // Local event seed, not a persistent draw RNG or the gameplay RNG. Repeated
    // publication, rear views and export cannot change this strike's topology.
    PSRand random(owner*0x9e3779b9u ^ uint32_t(frame)*0x85ebca6bu ^ uint32_t(frame>>32));
    auto camera_orientation = orientation;
    angvec angles; vm_ExtractAnglesFromMatrix(&angles,&camera_orientation);
    const int heading = angles.h+int(((random()%1000)-500)*30);
    matrix plane; vm_AnglesToMatrix(&plane,0,static_cast<angle>(heading),0);
    const float limit = -position.y*2+random()%400;
    vector start = position+plane.fvec*4000;
    start.y += 800+random()%100;
    current_.origin = start;
    if (random()%3) {
        current_.origin_type = random()%2;
        current_.origin_size = float(300+random()%200);
    }
    struct Branch { vector start; unsigned splits; };
    std::array<Branch,4> stack{};
    unsigned pending = 1; stack[0] = {start,0};
    while (pending && current_.count < current_.segments.size()) {
        const auto branch = stack[--pending];
        auto end = branch.start+plane.rvec*(((random()%200)-100)*.7f)-plane.uvec*(30.f+random()%100);
        current_.segments[current_.count++] = {branch.start,end};
        if (branch.start.y < limit) continue;
        if (random()%8 == 0 && branch.splits < 2) {
            stack[pending++] = {end,branch.splits+1};
            stack[pending++] = {end,branch.splits+1};
        } else stack[pending++] = {end,branch.splits};
    }
    current_.truncated = pending != 0;
    return true;
}
