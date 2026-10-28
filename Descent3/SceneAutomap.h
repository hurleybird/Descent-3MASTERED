// Descent 3MASTERED - GPL-3.0-or-later
#pragma once
#include "../renderer/Scene.h"
#include <array>

enum class AutomapFaceAppearance { Hidden, Baked, Black, Unexplored };
struct AutomapFaceRevision {
    AutomapFaceAppearance appearance = AutomapFaceAppearance::Black;
    int lightmap = -1;
    uint64_t content = 0;
};
inline bool operator==(const AutomapFaceRevision& a,const AutomapFaceRevision& b) {
    return a.appearance == b.appearance && a.lightmap == b.lightmap && a.content == b.content;
}
struct AutomapGeometryKey {
    d3scene::AssetId geometry;
    uint64_t appearance = 0;
    std::vector<AutomapFaceRevision> faces;
};
inline bool operator==(const AutomapGeometryKey& a,const AutomapGeometryKey& b) {
    return a.geometry == b.geometry && a.appearance == b.appearance && a.faces == b.faces;
}
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
