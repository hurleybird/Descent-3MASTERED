// Descent 3MASTERED - GPL-3.0-or-later
#pragma once
#include "weapon.h"

enum class WeaponBodyVisual { None, Model, Sprite, Electrical };
struct WeaponVisualPlan {
    WeaponBodyVisual body = WeaponBodyVisual::None;
    bool streamer = false,ring = false;
};
// Visibility of the body does not own admission of independent contributions.
// Share one source contract between model/sprite producers so a body-specific
// early return cannot discard the other work.
inline WeaponVisualPlan CompileWeaponVisuals(int flags) {
    WeaponVisualPlan result;
    result.streamer = (flags & WF_STREAMER) != 0;
    result.ring = (flags & WF_RING) != 0;
    if (!(flags & WF_INVISIBLE)) result.body = flags & WF_ELECTRICAL ? WeaponBodyVisual::Electrical :
        flags & (WF_IMAGE_BITMAP | WF_IMAGE_VCLIP) ? WeaponBodyVisual::Sprite : WeaponBodyVisual::Model;
    return result;
}
