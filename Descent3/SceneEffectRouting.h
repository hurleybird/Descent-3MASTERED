// Descent 3MASTERED - GPL-3.0-or-later
#pragma once
#include "fireball_external.h"
#include "viseffect_external.h"

enum class EffectAudience { World, WorldAndMainCamera, CameraLocal };
// Family policy precedes flag policy. Authored lines/trails remain physical
// world content even if their emitter carried a close-screen hint.
inline EffectAudience CompileEffectAudience(int type,int id,int flags) {
    if (type == VIS_FIREBALL) {
        switch (id) {
        case FADING_LINE_INDEX: case LIGHTNING_BOLT_INDEX: case GRAY_LIGHTNING_BOLT_INDEX:
        case THICK_LIGHTNING_INDEX: case SINE_WAVE_INDEX: case BILLBOARD_SMOKETRAIL_INDEX:
        case MASSDRIVER_EFFECT_INDEX: case MERCBOSS_MASSDRIVER_EFFECT_INDEX:
            return EffectAudience::World;
        }
        if ((flags & VF_WINDSHIELD_EFFECT) && (id == RAINDROP_INDEX || id == SNOWFLAKE_INDEX))
            return EffectAudience::CameraLocal;
    }
    return flags & VF_CLOSE_SCREEN_EFFECT ? EffectAudience::WorldAndMainCamera : EffectAudience::World;
}
