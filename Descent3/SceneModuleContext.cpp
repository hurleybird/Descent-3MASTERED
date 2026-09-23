// Descent 3MASTERED - GPL-3.0-or-later
#include "SceneModuleContext.h"
#include <stdexcept>

namespace {
// Scope ownership is per calling thread, not renderer global state. Concurrent
// simulation extraction remains unsupported; this does not imply otherwise.
thread_local d3scene::PresentationRef active;
}
ModuleSceneScope::ModuleSceneScope(d3scene::PresentationRef frame) {
    if (!frame) throw std::invalid_argument("Module scene scope requires a published frame");
    if (active) throw std::logic_error("Nested module scene publication");
    active = std::move(frame);
}
ModuleSceneScope::~ModuleSceneScope() { active.reset(); }
d3scene::PresentationRef ModuleScenePresentation() {
    if (!active) throw std::logic_error("Module scene request outside a published render interval");
    return active;
}
