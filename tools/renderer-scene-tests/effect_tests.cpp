// Descent 3MASTERED - GPL-3.0-or-later
#include "../../Descent3/SceneWeaponVisuals.h"
#include <stdexcept>
#include <initializer_list>

int runEffectTests() {
    int checks = 0;
    for (int body : {0,WF_IMAGE_BITMAP,WF_IMAGE_VCLIP,WF_ELECTRICAL,WF_ELECTRICAL|WF_IMAGE_BITMAP})
        for (int independent : {0,WF_RING,WF_STREAMER,WF_RING|WF_STREAMER})
            for (int hidden : {0,WF_INVISIBLE}) {
                const auto plan = CompileWeaponVisuals(body|independent|hidden);
                const auto expected = hidden ? WeaponBodyVisual::None : body & WF_ELECTRICAL ? WeaponBodyVisual::Electrical :
                    body & (WF_IMAGE_BITMAP|WF_IMAGE_VCLIP) ? WeaponBodyVisual::Sprite : WeaponBodyVisual::Model;
                ++checks;
                if (plan.body != expected || plan.ring != bool(independent&WF_RING) || plan.streamer != bool(independent&WF_STREAMER))
                    throw std::runtime_error("Weapon body visibility suppressed or changed independent contributions");
            }
    return checks;
}
