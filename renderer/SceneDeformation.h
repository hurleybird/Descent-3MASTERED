// Descent 3MASTERED - GPL-3.0-or-later
#pragma once
#include "SceneView.h"
#include <cstring>
#include <cmath>
#include <algorithm>

namespace d3scene {
struct GeometryDeformationBinding { ViewDeformationResponse response; uint32_t identity = 0; };
inline bool hasDeformationMotion(ViewDeformation current,ViewDeformation previous,ViewDeformationResponse response) {
    const auto active = [](Vec3 p) { return p.x != 0 || p.y != 0 || p.z != 0; };
    return (response.vertices != 0 || response.translation != 0) && (active(current.displacement) || active(previous.displacement));
}
inline GeometryDeformationBinding deformationForGeometry(const FrameData& frame,const VisibleSurface& item,RenderLayer layer) {
    if (layer != RenderLayer::World) return {{0,0},0};
    GeometryDeformationBinding result;
    const auto model_binding = [&](const ModelInstance& model) {
        result.identity = model.has_owner ? model.owner : uint32_t(model.identity)^uint32_t(model.identity>>32);
        return model.deformation_response;
    };
    if (item.model) result.response = model_binding(frame.model_instances[item.model_instance_index]);
    else if (item.mesh) {
        const auto& mesh = frame.meshes[item.mesh_index]; result.response = mesh.deformation_response;
        result.identity = mesh.has_owner ? mesh.owner : 0;
        // Attached accents follow the body's rigid movement without receiving
        // an additional, independent wobble for each submodel or glow quad.
        if (mesh.model_instance) result.response.translation += model_binding(frame.model_instances[mesh.compiled_model]).translation;
    }
    return result;
}
// Reference for the common scene vertex shader. Equal world points share an
// offset across rooms, terrain, model seams and passes. No RNG or source writes.
inline uint32_t deformationHash(uint32_t h) {
    h ^= h>>16; h *= 0x7feb352du; h ^= h>>15; h *= 0x846ca68bu; return h^(h>>16);
}
inline float deformationNoise(uint32_t value) { return float(deformationHash(value)&65535u)/32767.5f-1; }
inline float deformationField(Vec3 point,uint32_t seed,float spacing) {
    const auto bits = [](float value) {
        if (value == 0) value = 0;
        uint32_t result; static_assert(sizeof(result) == sizeof(value),"Scene float width changed");
        std::memcpy(&result,&value,sizeof(result)); return result;
    };
    const Vec3 q{point.x/spacing,point.y/spacing,point.z/spacing};
    const Vec3 cell{std::floor(q.x),std::floor(q.y),std::floor(q.z)};
    const auto smooth = [](float x) { return x*x*(3-2*x); };
    const Vec3 f{smooth(q.x-cell.x),smooth(q.y-cell.y),smooth(q.z-cell.z)};
    const auto sample = [&](float x,float y,float z) {
        return deformationNoise(bits(cell.x+x)*0x9e3779b9u ^ bits(cell.y+y)*0x85ebca6bu ^ bits(cell.z+z)*0xc2b2ae35u ^ seed);
    };
    const auto mix = [](float a,float b,float t) { return a+(b-a)*t; };
    return mix(mix(mix(sample(0,0,0),sample(1,0,0),f.x),mix(sample(0,1,0),sample(1,1,0),f.x),f.y),
        mix(mix(sample(0,0,1),sample(1,0,1),f.x),mix(sample(0,1,1),sample(1,1,1),f.x),f.y),f.z);
}
inline Vec3 deformViewPoint(Vec3 point,const ViewDeformation& effect,ViewDeformationResponse response = {},uint32_t identity = 0) {
    const auto& axis = effect.displacement;
    if ((axis.x == 0 && axis.y == 0 && axis.z == 0) || (response.vertices == 0 && response.translation == 0)) return point;
    // Bound the field gradient as amplitude grows, so nearby triangles do not
    // fold through one another as independent per-vertex hash jitter would.
    const float spacing = std::max(64.f,std::sqrt(axis.x*axis.x+axis.y*axis.y+axis.z*axis.z)*8);
    const float value = response.vertices ? deformationField(point,effect.seed,spacing)*response.vertices : 0;
    const auto magnitude = [](float x) { return x < 0 ? -x : x; };
    const uint32_t rigid = effect.seed^identity;
    return {point.x+axis.x*value+magnitude(axis.x)*response.translation*deformationNoise(rigid^0x51ed270bu),
        point.y+axis.y*value+magnitude(axis.y)*response.translation*deformationNoise(rigid^0x68bc21ebu),
        point.z+axis.z*value+magnitude(axis.z)*response.translation*deformationNoise(rigid^0x02e5be93u)};
}
}
