// Descent 3MASTERED - GPL-3.0-or-later
#pragma once
#include <cstdint>

namespace d3scene {
// Procedural images advance on presentation time, never wall time or draw count.
// A long absence advances one step rather than replaying unbounded hidden work.
inline int proceduralSteps(int previous, int current, bool enabled) {
    if (previous == current) return 0;
    const int64_t elapsed = int64_t(current) - previous;
    if (previous >= 0 && elapsed > 0 && !enabled && elapsed < 600) return 0;
    return previous < 0 || elapsed < 1 || elapsed > 8 ? 1 : int(elapsed);
}

// Local LCG retains the procedural kernels' 15-bit random range. Each image/tick
// has its own stream: extra views or another texture cannot consume its entropy.
class ProceduralRandom {
    uint32_t state_;
public:
    explicit ProceduralRandom(uint32_t seed) : state_(seed) {}
    ProceduralRandom(uint32_t image, int tick) {
        uint32_t seed = image * 0x9e3779b9u ^ uint32_t(tick) ^ 0x70726f63u;
        seed ^= seed >> 16; seed *= 0x7feb352du;
        seed ^= seed >> 15; seed *= 0x846ca68bu;
        state_ = seed ^ (seed >> 16);
    }
    int operator()() {
        state_ = state_ * 214013u + 2531011u;
        return int((state_ >> 16) & 0x7fffu);
    }
};
}
