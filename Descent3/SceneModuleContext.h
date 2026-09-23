// Descent 3MASTERED - GPL-3.0-or-later
#pragma once
#include "../renderer/Scene.h"

// The legacy module ABI cannot carry a presentation argument. Its translator
// borrows the game's published frame only for the enclosing render interval.
// It is never allowed to extract a second frame or retain an implicit last frame.
class ModuleSceneScope final {
public:
    explicit ModuleSceneScope(d3scene::PresentationRef);
    ~ModuleSceneScope();
    ModuleSceneScope(const ModuleSceneScope&) = delete;
    ModuleSceneScope& operator=(const ModuleSceneScope&) = delete;
};
d3scene::PresentationRef ModuleScenePresentation();
