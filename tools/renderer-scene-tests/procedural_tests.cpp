// Descent 3MASTERED - GPL-3.0-or-later
#include "../../renderer/SceneProcedural.h"
#include <stdexcept>
#include <climits>
int runProceduralTests() {
    using namespace d3scene;
    int checks = 0;
    const auto check = [&](bool value) { ++checks; if (!value) throw std::runtime_error("Procedural clock/random contract"); };
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
