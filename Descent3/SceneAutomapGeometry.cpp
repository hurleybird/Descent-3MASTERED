// Descent 3MASTERED - GPL-3.0-or-later
#include "SceneAutomap.h"
#include <stdexcept>

std::array<d3scene::MeshGeometry,9> CompileAutomapGeometry(
    const d3scene::MeshGeometry& source,const std::vector<AutomapFaceSource>& faces) {
    if (faces.size() != source.polygons.size()) throw std::invalid_argument("Incomplete automap face policy");
    std::array<d3scene::MeshGeometry,9> result;
    for (size_t index = 0; index < faces.size(); ++index) {
        const auto& face = faces[index];
        if (face.detail > 2) throw std::invalid_argument("Unknown automap detail tier");
        if (face.appearance == AutomapFaceAppearance::Hidden) continue;
        unsigned appearance = 0;
        switch (face.appearance) {
        case AutomapFaceAppearance::Baked: break;
        case AutomapFaceAppearance::Black: appearance = 1; break;
        case AutomapFaceAppearance::Unexplored: appearance = 2; break;
        default: throw std::invalid_argument("Unknown automap appearance");
        }
        const auto& polygon = source.polygons[index];
        if (face.appearance == AutomapFaceAppearance::Baked && face.colors.size() != polygon.corners.size())
            throw std::invalid_argument("Incomplete automap corner colors");
        auto& geometry = result[appearance*3+face.detail];
        d3scene::MeshPolygon copy; copy.normal = polygon.normal;
        for (size_t corner = 0; corner < polygon.corners.size(); ++corner) {
            if (polygon.corners[corner].vertex >= source.vertices.size())
                throw std::invalid_argument("Automap corner references an absent vertex");
            copy.corners.push_back({static_cast<uint32_t>(geometry.vertices.size())});
            geometry.vertices.push_back(source.vertices[polygon.corners[corner].vertex]);
            geometry.vertex_colors.push_back(face.appearance == AutomapFaceAppearance::Baked ? face.colors[corner] :
                face.appearance == AutomapFaceAppearance::Black ? d3scene::Vec3{} : d3scene::Vec3{1,1,1});
        }
        geometry.polygons.push_back(std::move(copy));
    }
    return result;
}
