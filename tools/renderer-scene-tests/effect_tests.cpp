// Descent 3MASTERED - GPL-3.0-or-later
#include "../../Descent3/SceneWeaponVisuals.h"
#include "../../Descent3/SceneEffectRouting.h"
#include <stdexcept>
#include <initializer_list>

int runEffectTests() {
    int checks = 0;
    for (int id : {FADING_LINE_INDEX,LIGHTNING_BOLT_INDEX,GRAY_LIGHTNING_BOLT_INDEX,THICK_LIGHTNING_INDEX,
        SINE_WAVE_INDEX,BILLBOARD_SMOKETRAIL_INDEX,MASSDRIVER_EFFECT_INDEX,MERCBOSS_MASSDRIVER_EFFECT_INDEX})
        for (int flags : {0,VF_CLOSE_SCREEN_EFFECT,VF_WINDSHIELD_EFFECT,VF_CLOSE_SCREEN_EFFECT|VF_WINDSHIELD_EFFECT}) {
            ++checks;
            if (CompileEffectAudience(VIS_FIREBALL,id,flags) != EffectAudience::World)
                throw std::runtime_error("Line/trail was routed by a blanket close-screen flag");
        }
    for (int id : {BLAST_RING_INDEX,AXIS_BILLBOARD_INDEX,HOT_SPARK_INDEX,BLACK_SMOKE_INDEX}) {
        ++checks;
        if (CompileEffectAudience(VIS_FIREBALL,id,0) != EffectAudience::World ||
            CompileEffectAudience(VIS_FIREBALL,id,VF_CLOSE_SCREEN_EFFECT) != EffectAudience::WorldAndMainCamera)
            throw std::runtime_error("Ordinary effect audience lost source flag policy");
    }
    for (int id : {RAINDROP_INDEX,SNOWFLAKE_INDEX}) {
        ++checks;
        if (CompileEffectAudience(VIS_FIREBALL,id,VF_WINDSHIELD_EFFECT|VF_CLOSE_SCREEN_EFFECT) != EffectAudience::CameraLocal)
            throw std::runtime_error("Camera-local weather leaked into world membership");
    }
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
