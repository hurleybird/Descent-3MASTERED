// Descent 3MASTERED - GPL-3.0-or-later
#pragma once
#include "vecmat.h"
#include <array>
#include <cstdint>

struct weather;
struct GameSkyWeatherFrame {
    struct Segment { vector from{}, to{}; };
    uint64_t scene = 0, frame = 0;
    uint32_t camera_owner = 0;
    unsigned phase = 0, count = 0;
    bool truncated = false;
    vector origin{};
    float origin_size = 0;
    int origin_type = -1;
    std::array<Segment,512> segments{};
};
// One logical two-frame flash/bolt, shared by all cameras. This deliberately
// replaces the old draw-count clock (which could consume both phases in one
// multi-view frame or leave an indoor strike pending indefinitely).
class GameSkyWeatherController {
    GameSkyWeatherFrame current_;
    bool advanced_ = false;
public:
    bool advance(uint64_t scene,uint64_t frame,bool paused,weather& state,
        uint32_t owner,const vector& position,const matrix& orientation);
    const GameSkyWeatherFrame* find(uint64_t scene) const {
        return advanced_ && current_.scene == scene ? &current_ : nullptr;
    }
};
