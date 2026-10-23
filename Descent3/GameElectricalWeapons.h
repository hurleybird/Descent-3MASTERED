// Descent 3MASTERED - GPL-3.0-or-later
#pragma once
#include "vecmat.h"
#include <cstdint>

// Game-resolved pulse. Rendering receives positions, never a raycast callback
// or authority to expire a weapon/create impact particles.
struct GameElectricalPulse {
    uint64_t scene = 0;
    uint32_t owner = 0, source_owner = 0;
    vector source{}, target{};
    bool valid = false, omega = false;
};
unsigned AdvanceGameElectricalWeapons(uint64_t scene,uint64_t frame,bool paused);
const GameElectricalPulse* FindGameElectricalPulse(uint64_t scene,uint32_t owner);
