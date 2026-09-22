// Descent 3MASTERED - GPL-3.0-or-later
#pragma once
#include <cmath>
#include <cstdint>
#include <stdexcept>
namespace d3scene {
inline int visualAngle(double time,float units_per_tick,int offset = 0) {
    if (!std::isfinite(time) || !std::isfinite(units_per_tick)) throw std::invalid_argument("Invalid visual phase");
    double phase = std::fmod(time*60.0*units_per_tick+offset,65536.0);
    if (!std::isfinite(phase)) throw std::invalid_argument("Visual phase overflow");
    if (phase < 0) phase += 65536.0;
    return static_cast<int>(phase);
}
// Preserve the authored legacy remainder convention, including its unusual
// period scaling, while defining stationary sprites instead of dividing by 0.
inline int weaponSpriteAngle(double time,float rotation_velocity) {
    if (!std::isfinite(time) || !std::isfinite(rotation_velocity)) throw std::invalid_argument("Invalid weapon phase");
    const double period = rotation_velocity/65536.0;
    if (period == 0) return 0;
    double phase = std::fmod(std::fmod(time,period)*65536.0,65536.0);
    if (phase < 0) phase += 65536.0;
    return static_cast<int>(phase);
}
struct AnimationFrames {
    int frame0 = 0, frame1 = 0;
    float frame1_weight = 0;
    bool has_frame1 = false;
};
inline AnimationFrames animationFrames(int count,float normalized_time,bool loop) {
    AnimationFrames result;
    if (!std::isfinite(normalized_time)) throw std::invalid_argument("Invalid animation phase");
    if (count <= 0) return result;
    float phase = count*normalized_time;
    if (phase < 0) phase = 0;
    if (phase >= count) phase = count-.00001f;
    result.frame0 = static_cast<int>(phase);
    if (result.frame0 >= count) result.frame0 = count-1;
    if (count <= 1) return result;
    result.frame1_weight = phase-result.frame0;
    if (result.frame1_weight <= .001f) return result;
    result.frame1 = result.frame0+1;
    if (result.frame1 >= count) {
        if (!loop) return result;
        result.frame1 = 0;
    }
    result.has_frame1 = true;
    return result;
}
}
