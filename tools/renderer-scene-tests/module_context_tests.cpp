// Descent 3MASTERED - GPL-3.0-or-later
#include "../../Descent3/SceneModuleContext.h"
#include <stdexcept>

int runModuleContextTests() {
    int checks = 0;
    const auto check = [&](bool value,const char* message) { ++checks; if (!value) throw std::runtime_error(message); };
    const auto noFrame = [&] {
        bool rejected = false;
        try { ModuleScenePresentation(); } catch (const std::logic_error&) { rejected = true; }
        check(rejected,"Module request retained an implicit last frame outside its interval");
    };
    noFrame();
    bool rejected = false;
    try { ModuleSceneScope invalid(nullptr); } catch (const std::invalid_argument&) { rejected = true; }
    check(rejected,"Empty module frame accepted");
    d3scene::FrameData data; data.sequence = 1; data.scene_lifetime = d3scene::assets().lifetime();
    auto first = d3scene::Presentation::freeze(data);
    ++data.sequence;
    auto second = d3scene::Presentation::freeze(data);
    d3scene::PresentationRef held;
    {
        ModuleSceneScope scope(first);
        held = ModuleScenePresentation();
        check(held == first && ModuleScenePresentation() == held,"Additional module views did not reuse the published frame");
        rejected = false;
        try { ModuleSceneScope nested(second); } catch (const std::logic_error&) { rejected = true; }
        check(rejected && ModuleScenePresentation() == first,"Nested publication replaced the active module frame");
    }
    noFrame();
    {
        ModuleSceneScope scope(second);
        check(ModuleScenePresentation() == second && held->data().sequence == 1,
            "A new render interval changed an already held frame");
    }
    noFrame();
    try { ModuleSceneScope scope(first); throw std::runtime_error("fixture unwind"); }
    catch (const std::runtime_error&) {}
    noFrame();
    return checks;
}
