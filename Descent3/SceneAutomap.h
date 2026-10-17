// Descent 3MASTERED - GPL-3.0-or-later
#pragma once
#include "../renderer/Scene.h"
#include <array>

enum class AutomapFaceAppearance { Hidden, Baked, Black, Unexplored };
struct AutomapFaceSource {
    AutomapFaceAppearance appearance = AutomapFaceAppearance::Black;
    uint8_t detail = 0; // 0 always, 1 through depth 600, 2 through depth 300
    std::vector<d3scene::Vec3> colors;
};
// Nine immutable mesh groups: appearance (baked/black/pulse) x detail tier.
// Topology/vertex color discontinuities are compiled before any camera query.
std::array<d3scene::MeshGeometry,9> CompileAutomapGeometry(
    const d3scene::MeshGeometry&,const std::vector<AutomapFaceSource>&);
d3scene::PresentationRef PrepareGameAutomap(uint64_t sequence,double display_time,bool terrain,int ship_model,int marker);
d3scene::PresentationRef PrepareAutomapInspection();
