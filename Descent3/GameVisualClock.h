// Descent 3MASTERED - GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

// Simulation-owned, bounded visual cadence. A new allocation, scene, rewind,
// or visibility gap starts now rather than releasing a hidden backlog.
struct GameVisualEvent { int tick = 0; float age = 0; };
class GameVisualClock {
    uint64_t scene_ = 0;
    uint32_t owner_ = 0;
    int bucket_ = 0;
    float time_ = 0;
    bool initialized_ = false;
public:
    void reset() { initialized_ = false; }
    int consume(uint64_t scene,uint32_t owner,float time,float delta,int period,
        GameVisualEvent* events,int capacity) {
        if (!events || capacity <= 0 || period <= 0 || !std::isfinite(time) || !std::isfinite(delta)) return 0;
        const int bucket = int(std::floor(double(time)*60.0/period));
        const float gap = time-time_;
        if (!initialized_ || scene_ != scene || owner_ != owner || bucket < bucket_ ||
            gap < 0 || gap > (std::max)(.05f,delta*2.5f)) {
            initialized_ = true; scene_ = scene; owner_ = owner; bucket_ = bucket-1;
        }
        time_ = time;
        const int first = (std::max)(bucket_+1,bucket-capacity+1);
        int count = 0;
        for (int b = first; b <= bucket; ++b) {
            events[count].tick = b*period;
            events[count++].age = (std::max)(0.f,time-float(b*period)/60.f);
        }
        bucket_ = bucket;
        return count;
    }
};
