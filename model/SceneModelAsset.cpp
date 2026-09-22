// Descent 3MASTERED - GPL-3.0-or-later
#include "SceneModelAsset.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace {
d3scene::Vec3 value(const vector& v) { return {v.x,v.y,v.z}; }
}
d3scene::ModelGeometry CompileSceneModelAsset(const poly_model& model, size_t* repaired_normals) {
    if (repaired_normals) *repaired_normals = 0;
    if (model.n_models < 0 || model.n_models > MAX_SUBOBJECTS || model.n_textures < 0 ||
        model.n_textures > MAX_MODEL_TEXTURES || (model.n_models && !model.submodel))
        throw std::invalid_argument("Invalid authored model counts");
    d3scene::ModelGeometry geometry;
    geometry.textures.assign(model.textures, model.textures + model.n_textures);
    for (int i = 0; i < model.n_models; ++i) {
        const auto& source = model.submodel[i];
        if (source.nverts < 0 || source.num_faces < 0 || (source.nverts && !source.verts) ||
            (source.num_faces && !source.faces)) throw std::invalid_argument("Incomplete authored model geometry");
        d3scene::ModelPart part;
        part.parent = source.parent; part.offset = value(source.offset); part.authored_flags = source.flags;
        for (int j = 0; j < source.nverts; ++j) {
            part.geometry.vertices.push_back(value(source.verts[j]));
            part.geometry.vertex_normals.push_back(source.vertnorms ? value(source.vertnorms[j]) : d3scene::Vec3{});
        }
        for (int j = 0; j < source.num_faces; ++j) {
            const auto& face = source.faces[j];
            if (face.nverts < 3 || !face.vertnums || !face.u || !face.v)
                throw std::invalid_argument("Invalid authored model polygon");
            d3scene::MeshPolygon polygon;
            polygon.normal = value(face.normal);
            for (int k = 0; k < face.nverts; ++k) {
                const int vertex = face.vertnums[k];
                if (vertex < 0 || vertex >= source.nverts) throw std::invalid_argument("Invalid authored model vertex");
                const float alpha = source.alpha ? source.alpha[vertex] : 1.f;
                if (!std::isfinite(alpha)) throw std::invalid_argument("Invalid authored model alpha");
                polygon.corners.push_back({static_cast<uint32_t>(vertex), face.u[k], face.v[k], 0, 0,
                    static_cast<uint8_t>(std::max(0.f, std::min(alpha, 1.f)) * 255.f)});
            }
            part.geometry.polygons.push_back(std::move(polygon));
            part.polygon_materials.push_back(face.texnum);
            part.polygon_colors.push_back(face.texnum < 0 ? d3scene::Vec3{GR_COLOR_RED(face.color)/255.f,
                GR_COLOR_GREEN(face.color)/255.f, GR_COLOR_BLUE(face.color)/255.f} : d3scene::Vec3{1,1,1});
        }
        // Some shipped assets contain non-finite vertex normals. Repair only
        // invalid/missing shading data from incident polygon normals; unused
        // vertices have a defined zero normal. Never send NaNs into lighting.
        for (size_t vertex = 0; vertex < part.geometry.vertex_normals.size(); ++vertex) {
            auto& normal = part.geometry.vertex_normals[vertex];
            const float length2 = normal.x*normal.x + normal.y*normal.y + normal.z*normal.z;
            if (std::isfinite(length2) && length2 > 1.e-12f) continue;
            normal = {};
            for (const auto& polygon : part.geometry.polygons) for (const auto& corner : polygon.corners)
                if (corner.vertex == vertex) {
                    normal.x += polygon.normal.x; normal.y += polygon.normal.y; normal.z += polygon.normal.z;
                    break;
                }
            const float length = std::sqrt(normal.x*normal.x + normal.y*normal.y + normal.z*normal.z);
            if (length > 1.e-6f && std::isfinite(length)) {
                normal.x /= length; normal.y /= length; normal.z /= length;
            } else normal = {};
            if (repaired_normals) ++*repaired_normals;
        }
        geometry.parts.push_back(std::move(part));
    }
    return geometry;
}
