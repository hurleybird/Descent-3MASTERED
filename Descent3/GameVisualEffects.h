// Descent 3MASTERED - GPL-3.0-or-later
#pragma once
#include <cstdint>

struct GameVisualEmissionStats {
    bool advanced = false;
    unsigned objects = 0, sparkles = 0, damage = 0, virus = 0;
};
extern bool Render_powerup_sparkles;
// Runs once in the game update, after object movement. Presentation extraction
// and every GPU view are consumers only. No global gameplay RNG is consumed.
GameVisualEmissionStats AdvanceGameVisualEffects(uint64_t scene,uint64_t frame,bool paused);
