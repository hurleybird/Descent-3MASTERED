// Descent 3MASTERED - GPL-3.0-or-later
#include "../../renderer/SceneProcedural.h"
#include "../../renderer/SceneAnimation.h"
#include <stdexcept>
#include <climits>
int runProceduralTests() {
    using namespace d3scene;
    int checks = 0;
    const auto check = [&](bool value) { ++checks; if (!value) throw std::runtime_error("Procedural clock/random contract"); };
    check(visualAngle(1,160,5000) == 14600);
    check(weaponSpriteAngle(10,0) == 0);
    check(weaponSpriteAngle(1.25,65536) == 16384);
    check(weaponSpriteAngle(1.25,32768) == 16384);
    check(weaponSpriteAngle(-.25,65536) == 49152);
    check(visualAngle(-1,160) == 55936);
    check(visualAngle(65536,1) == 0);
    const auto first = animationFrames(10,0,false), halfway = animationFrames(10,.25f,false);
    check(first.frame0 == 0 && !first.has_frame1);
    check(halfway.frame0 == 2 && halfway.frame1 == 3 && halfway.frame1_weight == .5f && halfway.has_frame1);
    check(!animationFrames(10,.99f,false).has_frame1);
    check(animationFrames(10,.99f,true).frame1 == 0 && animationFrames(10,.99f,true).has_frame1);
    check(animationFrames(10,-1,true).frame0 == 0 && animationFrames(10,2,false).frame0 == 9);
    check(!animationFrames(1,.3f,true).has_frame1 && !animationFrames(0,.3f,true).has_frame1);
    check(proceduralSteps(-1, 700, true) == 1);
    check(proceduralSteps(-1, 0, false) == 1);
    check(proceduralSteps(10, 10, true) == 0);
    check(proceduralSteps(10, 10, false) == 0);
    check(proceduralSteps(10, 18, true) == 8);
    check(proceduralSteps(10, 19, true) == 1);
    check(proceduralSteps(10, 609, false) == 0);
    check(proceduralSteps(10, 610, false) == 1);
    check(proceduralSteps(700, 10, false) == 1);
    check(proceduralSteps(0, INT_MAX, true) == 1);
    ProceduralRandom reference(1);
    check(reference() == 41); check(reference() == 18467); check(reference() == 6334);
    ProceduralRandom a(42, 100), repeat(42, 100), other(43, 100), next(42, 101);
    bool other_differs = false, next_differs = false;
    for (int i = 0; i < 1000; ++i) {
        const int value = a();
        other_differs |= value != other(); next_differs |= value != next();
        check(value == repeat() && value >= 0 && value <= 32767);
    }
    check(other_differs && next_differs);
    return checks;
}
