// Descent 3MASTERED - GPL-3.0-or-later
#include "SceneModelAsset.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace {
uint64_t SourceEpoch = 1;
d3scene::Vec3 value(const vector& v) { return {v.x,v.y,v.z}; }
float finiteUV(float value) { return std::isfinite(value) ? value : 0.f; }
}
uint64_t SceneModelSourceEpoch() { return SourceEpoch; }
void InvalidateSceneModelSources() {
    if (++SourceEpoch == 0) throw std::overflow_error("Model source epoch exhausted");
}
SceneModelPartRole ResolveSceneModelPartRole(uint32_t flags,uint32_t mask,size_t part,bool has_custom_texture) {
    if (part >= 32 || !(mask&(uint32_t(1)<<part)) || ((flags&SOF_CUSTOM) && !has_custom_texture))
        return SceneModelPartRole::Hidden;
    // Door authoring metadata is intentionally not model draw geometry.
    if (flags&(SOF_SHELL|SOF_FRONTFACE)) return SceneModelPartRole::Hidden;
    if (flags&(SOF_GLOW|SOF_THRUSTER)) return SceneModelPartRole::Glow;
    if (flags&SOF_FACING) return SceneModelPartRole::Facing;
    return SceneModelPartRole::Surface;
}
SceneModelRoles ResolveSceneModelRoles(const d3scene::ModelGeometry& model,uint32_t mask,bool has_custom_texture) {
    if (model.parts.size() > MAX_SUBOBJECTS) throw std::invalid_argument("Too many model role parts");
    SceneModelRoles result{};
    for (size_t i = 0; i < model.parts.size(); ++i) {
        auto role = ResolveSceneModelPartRole(model.parts[i].authored_flags,mask,i,has_custom_texture);
        int current = static_cast<int>(i);
        for (size_t depth = 0; current >= 0; ++depth) {
            if (size_t(current) >= model.parts.size() || depth >= model.parts.size())
                throw std::invalid_argument("Invalid model role hierarchy");
            const auto& ancestor = model.parts[current];
            // Legacy traversal returns before visiting children for metadata
            // and empty parts. A masked/custom-hidden parent, by contrast,
            // still traverses its children; do not propagate that draw mask.
            if (ancestor.geometry.polygons.empty() || (ancestor.authored_flags&(SOF_SHELL|SOF_FRONTFACE)))
                role = SceneModelPartRole::Hidden;
            current = ancestor.parent;
        }
        result[i] = role;
    }
    return result;
}
SceneModelRoles ResolveSceneModelRoles(const d3scene::ModelAsset& asset,uint32_t mask,bool has_custom_texture) {
    const auto& parts = asset.geometry().parts;
    if (parts.size() > MAX_SUBOBJECTS) throw std::invalid_argument("Too many model role parts");
    SceneModelRoles result{};
    for (size_t i = 0; i < parts.size(); ++i)
        result[i] = parts[i].hierarchy_hidden ? SceneModelPartRole::Hidden :
            ResolveSceneModelPartRole(parts[i].authored_flags,mask,i,has_custom_texture);
    return result;
}
d3scene::ModelGeometry CompileSceneModelAsset(const poly_model& model, size_t* repaired_normals,size_t* repaired_uv_components) {
    if (repaired_normals) *repaired_normals = 0;
    if (repaired_uv_components) *repaired_uv_components = 0;
    if (model.n_models < 0 || model.n_models > MAX_SUBOBJECTS || model.n_textures < 0 ||
        model.n_textures > MAX_MODEL_TEXTURES || (model.n_models && !model.submodel))
        throw std::invalid_argument("Invalid authored model counts");
    d3scene::ModelGeometry geometry;
    geometry.animation = std::make_shared<const ModelAnimation>(CompileModelAnimation(model));
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
                // Shipped OOF files can contain NaN texture coordinates (for
                // example MUDSickleFull). Preserve topology and every finite
                // component; undefined coordinates have a deterministic origin.
                // The immutable scene boundary still rejects non-finite data.
                if (repaired_uv_components) *repaired_uv_components +=
                    size_t(!std::isfinite(face.u[k]))+size_t(!std::isfinite(face.v[k]));
                polygon.corners.push_back({static_cast<uint32_t>(vertex), finiteUV(face.u[k]), finiteUV(face.v[k]), 0, 0,
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
    const bool static_rest = (model.flags & PMF_TIMED) || std::none_of(geometry.parts.begin(),geometry.parts.end(),
        [](const d3scene::ModelPart& part) { return (part.authored_flags & (SOF_ROTATE|SOF_TURRET)) != 0; });
    geometry.pose_uses_clock = std::any_of(geometry.parts.begin(),geometry.parts.end(),[&](const d3scene::ModelPart& part) {
        return (part.authored_flags & SOF_ROTATE) || ((part.authored_flags & SOF_TURRET) &&
            !(model.flags & PMF_TIMED) && model.num_key_angles <= 0);
    });
    if (static_rest) {
        ModelPose rest; rest.count = model.n_models;
        const auto transforms = ComposeModelPose(model,rest);
        for (int i = 0; i < transforms.count; ++i) {
            const auto& t = transforms.submodels[i];
            geometry.rest_pose.push_back({value(t.translation),value(t.orientation.rvec),value(t.orientation.uvec),value(t.orientation.fvec)});
        }
    }
    return geometry;
}
bool MatchesSceneModelAsset(const d3scene::ModelGeometry& geometry,const poly_model& model) {
    if (model.n_models < 0 || model.n_textures < 0 || geometry.parts.size() != size_t(model.n_models) ||
        geometry.textures.size() != size_t(model.n_textures) || (model.n_models && !model.submodel)) return false;
    if (!geometry.animation || !(*geometry.animation == CompileModelAnimation(model))) return false;
    const bool static_rest = (model.flags & PMF_TIMED) || std::none_of(geometry.parts.begin(),geometry.parts.end(),
        [](const d3scene::ModelPart& part) { return (part.authored_flags & (SOF_ROTATE|SOF_TURRET)) != 0; });
    if (static_rest != (geometry.rest_pose.size() == geometry.parts.size())) return false;
    const bool clock = std::any_of(geometry.parts.begin(),geometry.parts.end(),[&](const d3scene::ModelPart& part) {
        return (part.authored_flags & SOF_ROTATE) || ((part.authored_flags & SOF_TURRET) &&
            !(model.flags & PMF_TIMED) && model.num_key_angles <= 0);
    });
    if (clock != geometry.pose_uses_clock) return false;
    const auto same = [](d3scene::Vec3 a,const vector& b) { return a.x == b.x && a.y == b.y && a.z == b.z; };
    for (int i = 0; i < model.n_textures; ++i) if (geometry.textures[i] != model.textures[i]) return false;
    for (int i = 0; i < model.n_models; ++i) {
        const auto& source = model.submodel[i]; const auto& part = geometry.parts[i]; const auto& mesh = part.geometry;
        if (source.nverts < 0 || source.num_faces < 0 || mesh.vertices.size() != size_t(source.nverts) ||
            mesh.polygons.size() != size_t(source.num_faces) || part.parent != source.parent ||
            part.authored_flags != source.flags || !same(part.offset,source.offset) ||
            (source.nverts && !source.verts) || (source.num_faces && !source.faces)) return false;
        for (int v = 0; v < source.nverts; ++v) {
            if (!same(mesh.vertices[v],source.verts[v])) return false;
            auto normal = source.vertnorms ? value(source.vertnorms[v]) : d3scene::Vec3{};
            const float length2 = normal.x*normal.x+normal.y*normal.y+normal.z*normal.z;
            if (!std::isfinite(length2) || length2 <= 1.e-12f) {
                normal = {};
                for (int f = 0; f < source.num_faces; ++f) {
                    const auto& face = source.faces[f];
                    if (!face.vertnums) return false;
                    for (int c = 0; c < face.nverts; ++c) if (face.vertnums[c] == v) {
                        normal.x += face.normal.x; normal.y += face.normal.y; normal.z += face.normal.z; break;
                    }
                }
                const float length = std::sqrt(normal.x*normal.x+normal.y*normal.y+normal.z*normal.z);
                if (length > 1.e-6f && std::isfinite(length)) { normal.x /= length; normal.y /= length; normal.z /= length; }
                else normal = {};
            }
            const auto stored = mesh.vertex_normals[v];
            if (normal.x != stored.x || normal.y != stored.y || normal.z != stored.z) return false;
        }
        for (int f = 0; f < source.num_faces; ++f) {
            const auto& face = source.faces[f]; const auto& polygon = mesh.polygons[f];
            if (face.nverts < 3 || !face.vertnums || !face.u || !face.v || polygon.corners.size() != size_t(face.nverts) ||
                !same(polygon.normal,face.normal) || part.polygon_materials[f] != face.texnum) return false;
            const auto color = part.polygon_colors[f];
            const d3scene::Vec3 expected = face.texnum < 0 ? d3scene::Vec3{GR_COLOR_RED(face.color)/255.f,
                GR_COLOR_GREEN(face.color)/255.f,GR_COLOR_BLUE(face.color)/255.f} : d3scene::Vec3{1,1,1};
            if (color.x != expected.x || color.y != expected.y || color.z != expected.z) return false;
            for (int c = 0; c < face.nverts; ++c) {
                const int vertex = face.vertnums[c]; const auto& corner = polygon.corners[c];
                if (vertex < 0 || vertex >= source.nverts) return false;
                const float alpha = source.alpha ? source.alpha[vertex] : 1.f;
                if (!std::isfinite(alpha) || corner.vertex != uint32_t(vertex) || corner.u != finiteUV(face.u[c]) || corner.v != finiteUV(face.v[c]) ||
                    corner.alpha != uint8_t(std::max(0.f,std::min(alpha,1.f))*255.f)) return false;
            }
        }
    }
    return true;
}
