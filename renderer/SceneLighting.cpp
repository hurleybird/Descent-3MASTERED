// Descent 3MASTERED - GPL-3.0-or-later
#include "SceneLighting.h"
#include "SceneGeometry.h"
#include "SceneTerrain.h"
#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <unordered_set>

namespace d3scene {
float roomLightScalar(float time, uint8_t pulse, uint8_t offset, uint32_t room, bool strobe, bool flicker, bool editor) {
    if (editor) return 1;
    float scalar = 1;
    if (pulse) {
        const float half_period = std::max(pulse*.01f,.04f);
        const float clock = time+offset*.01f;
        const float phase = (clock-static_cast<int>(clock/(half_period*2))*(half_period*2))/half_period;
        scalar = phase > 1 ? 2-phase : phase;
    }
    if (strobe && ((static_cast<int>(time*10)+room)%2)) scalar = 0;
    if (flicker && (visualNoise(time,room,3)&1u)) scalar = 0;
    return scalar;
}
Vec3 sampleVolume(const VolumeGrid& grid,Vec3 position) {
    if (grid.colors.empty()) return {1,1,1};
    return sampleVolume(VolumeGridView{grid.origin,grid.width,grid.height,grid.depth,grid.spacing,grid.colors.data()},position);
}
Vec3 sampleVolume(VolumeGridView grid,Vec3 position) {
    if (!grid.colors || !grid.width || !grid.height || !grid.depth || grid.spacing <= 0) return {1,1,1};
    const auto coordinate = [&](float p,float origin,uint32_t size) {
        return std::max(0.f,std::min((p-origin)/grid.spacing,static_cast<float>(size-1)));
    };
    const float x = coordinate(position.x,grid.origin.x,grid.width);
    const float y = coordinate(position.y,grid.origin.y,grid.height);
    const float z = coordinate(position.z,grid.origin.z,grid.depth);
    const uint32_t ix = static_cast<uint32_t>(x), iy = static_cast<uint32_t>(y), iz = static_cast<uint32_t>(z);
    const auto sample = [&](uint32_t dx,uint32_t dy,uint32_t dz) {
        const auto color = grid.colors[(std::min(iz+dz,grid.depth-1)*grid.height+std::min(iy+dy,grid.height-1))*grid.width+
            std::min(ix+dx,grid.width-1)];
        return Vec3{(color>>5)/7.f,((color>>2)&7)/7.f,(color&3)/3.f};
    };
    const auto mix = [](Vec3 a,Vec3 b,float t) { return Vec3{a.x*(1-t)+b.x*t,a.y*(1-t)+b.y*t,a.z*(1-t)+b.z*t}; };
    return mix(mix(mix(sample(0,0,0),sample(0,1,0),y-iy),mix(sample(1,0,0),sample(1,1,0),y-iy),x-ix),
        mix(mix(sample(0,0,1),sample(0,1,1),y-iy),mix(sample(1,0,1),sample(1,1,1),y-iy),x-ix),z-iz);
}
uint32_t visualNoise(double time, uint32_t key, uint32_t salt) {
    const double wrapped = std::fmod(std::floor(time*60.0),4294967296.0);
    uint32_t value = static_cast<uint32_t>(wrapped < 0 ? wrapped+4294967296.0 : wrapped) * 0x9e3779b9u;
    value ^= key*0x85ebca6bu; value ^= salt*0xc2b2ae35u;
    value ^= value >> 16; value *= 0x7feb352du; value ^= value >> 15; value *= 0x846ca68bu;
    return value ^ (value >> 16);
}
float lightAttenuation(const Light& light, Vec3 world) {
    const Vec3 delta{world.x-light.position.x,world.y-light.position.y,world.z-light.position.z};
    const float distance = std::sqrt(delta.x*delta.x+delta.y*delta.y+delta.z*delta.z);
    if (light.radius <= 0 || distance >= light.radius) return 0;
    float attenuation = std::pow(1-distance/light.radius,light.falloff);
    if (light.directional) {
        const float dot = distance > 1.e-6f ? (delta.x*light.direction.x+delta.y*light.direction.y+
            delta.z*light.direction.z)/distance : 1.f;
        attenuation *= std::max(0.f,(dot-light.cone)/std::max(1.e-6f,1-light.cone));
    }
    return attenuation;
}
namespace {
bool touchesBounds(const Light& light,const Bounds& b) {
    const float x = std::max(b.minimum.x-light.position.x,std::max(0.f,light.position.x-b.maximum.x));
    const float y = std::max(b.minimum.y-light.position.y,std::max(0.f,light.position.y-b.maximum.y));
    const float z = std::max(b.minimum.z-light.position.z,std::max(0.f,light.position.z-b.maximum.z));
    return x*x+y*y+z*z <= light.radius*light.radius;
}
bool touches(const Light& light, MeshView mesh, const MeshPolygon& polygon, const Transform& transform) {
    Vec3 low = transformPoint(transform,mesh.positions[polygon.corners[0].vertex]), high = low;
    if (light.directional) {
        const auto normal = transformDirection(transform,polygon.normal);
        if (normal.x*(light.position.x-low.x)+normal.y*(light.position.y-low.y)+normal.z*(light.position.z-low.z) < 0) return false;
    }
    for (const auto& corner : polygon.corners) {
        const auto p = transformPoint(transform,mesh.positions[corner.vertex]);
        low.x = std::min(low.x,p.x); low.y = std::min(low.y,p.y); low.z = std::min(low.z,p.z);
        high.x = std::max(high.x,p.x); high.y = std::max(high.y,p.y); high.z = std::max(high.z,p.z);
    }
    const float x = std::max(low.x-light.position.x,std::max(0.f,light.position.x-high.x));
    const float y = std::max(low.y-light.position.y,std::max(0.f,light.position.y-high.y));
    const float z = std::max(low.z-light.position.z,std::max(0.f,light.position.z-high.z));
    return x*x+y*y+z*z <= light.radius*light.radius;
}
}
void appendSurfaceLightIndices(const FrameData& data,const std::vector<uint32_t>& candidates,
    MeshView mesh,const MeshPolygon& polygon,const Transform& transform,std::vector<int32_t>& output) {
    for (const auto index : candidates)
        if (touches(data.lights[index],mesh,polygon,transform)) output.push_back(static_cast<int32_t>(index));
}
void prepareLightInfluences(FrameData& data) {
    // Camera-anchored meshes cannot be spatially culled during view-independent
    // preparation. Keep the canonical light table; the shader evaluates range.
    for (auto& mesh : data.meshes) {
        mesh.surface.light_indices.clear();
        if (!mesh.surface.unlit) for (size_t i = 0; i < data.lights.size(); ++i)
            mesh.surface.light_indices.push_back(static_cast<uint32_t>(i));
    }
    std::unordered_map<int32_t,const RoomState*> rooms;
    for (const auto& room : data.rooms) rooms.emplace(room.number,&room);
    std::vector<std::unordered_set<int32_t>> reachable(data.lights.size());
    for (size_t i = 0; i < data.lights.size(); ++i) {
        const auto& light = data.lights[i];
        std::vector<int32_t> queue;
        if (rooms.count(light.room)) { queue.push_back(light.room); reachable[i].insert(light.room); }
        else { // Exterior policy remains conservative until terrain portals migrate.
            for (const auto& room : data.rooms) reachable[i].insert(room.number);
        }
        for (size_t next = 0; next < queue.size(); ++next) {
            const auto& room = *rooms.at(queue[next]);
            for (const auto& portal : room.portals) {
                if (!rooms.count(portal.destination_room) || reachable[i].count(portal.destination_room)) continue;
                if (!touches(light,MeshView(room.asset->geometry()),room.asset->geometry().polygons[portal.polygon],{})) continue;
                reachable[i].insert(portal.destination_room); queue.push_back(portal.destination_room);
            }
        }
    }
    for (auto& room : data.rooms) {
        room.light_influences_prepared = true;
        auto& candidates = room.light_candidates;
        candidates.clear();
        for (size_t i = 0; i < data.lights.size(); ++i) if (reachable[i].count(room.number) &&
            touchesBounds(data.lights[i],room.asset->bounds())) candidates.push_back(uint32_t(i));
    }
    for (auto& object : data.model_instances) for (auto& model : object.models) for (size_t part = 0; part < model.parts.size(); ++part) {
        auto& pose = model.parts[part];
        pose.light_influences_prepared = true;
        auto& candidates = pose.light_candidates;
        candidates.clear();
        if (!pose.draw) continue;
        const auto transform = compose(object.current,pose.current);
        const auto bounds = transformBounds(transform,model.asset->geometry().parts[part].local_bounds);
        for (size_t i = 0; i < data.lights.size(); ++i)
            if ((object.room < 0 || reachable[i].count(object.room)) &&
                (!pose.vertices.empty() || touchesBounds(data.lights[i],bounds))) candidates.push_back(uint32_t(i));
    }
    if (data.terrain.asset) for (size_t patch = 0; patch < data.terrain.patches.size(); ++patch) {
        const auto& bounds = data.terrain.asset->patches()[patch].bounds;
        std::vector<uint32_t> indices;
        for (size_t light = 0; light < data.lights.size(); ++light) {
            const auto& source = data.lights[light];
            bool exterior = !rooms.count(source.room);
            for (auto room : reachable[light]) exterior = exterior || rooms.at(room)->exterior;
            if (!exterior) continue;
            const auto axis = [](float p,float lo,float hi) { return p-std::max(lo,std::min(p,hi)); };
            const float x = axis(source.position.x,bounds.minimum.x,bounds.maximum.x);
            const float y = axis(source.position.y,bounds.minimum.y,bounds.maximum.y);
            const float z = axis(source.position.z,bounds.minimum.z,bounds.maximum.z);
            if (x*x+y*y+z*z <= source.radius*source.radius) indices.push_back(static_cast<uint32_t>(light));
        }
        for (auto& surface : data.terrain.patches[patch]) surface.light_indices = indices;
    }
}
}
