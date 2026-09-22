// Descent 3MASTERED - GPL-3.0-or-later
#include "SceneLighting.h"
#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <unordered_set>

namespace d3scene {
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
bool touches(const Light& light, const MeshGeometry& mesh, const MeshPolygon& polygon, const Transform& transform) {
    Vec3 low = transformPoint(transform,mesh.vertices[polygon.corners[0].vertex]), high = low;
    if (light.directional) {
        const auto normal = transformDirection(transform,polygon.normal);
        if (normal.x*(light.position.x-low.x)+normal.y*(light.position.y-low.y)+normal.z*(light.position.z-low.z) < 0) return false;
    }
    for (const auto& corner : polygon.corners) {
        const auto p = transformPoint(transform,mesh.vertices[corner.vertex]);
        low.x = std::min(low.x,p.x); low.y = std::min(low.y,p.y); low.z = std::min(low.z,p.z);
        high.x = std::max(high.x,p.x); high.y = std::max(high.y,p.y); high.z = std::max(high.z,p.z);
    }
    const float x = std::max(low.x-light.position.x,std::max(0.f,light.position.x-high.x));
    const float y = std::max(low.y-light.position.y,std::max(0.f,light.position.y-high.y));
    const float z = std::max(low.z-light.position.z,std::max(0.f,light.position.z-high.z));
    return x*x+y*y+z*z <= light.radius*light.radius;
}
}
void prepareLightInfluences(FrameData& data) {
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
                if (!touches(light,room.asset->geometry(),room.asset->geometry().polygons[portal.polygon],{})) continue;
                reachable[i].insert(portal.destination_room); queue.push_back(portal.destination_room);
            }
        }
    }
    auto assign = [&](int32_t room, const MeshGeometry& geometry, const Transform& transform, std::vector<SurfaceBinding>& surfaces) {
        for (size_t face = 0; face < surfaces.size(); ++face) {
            auto& indices = surfaces[face].light_indices;
            indices.clear();
            for (size_t i = 0; i < data.lights.size(); ++i) {
                if (room >= 0 && !reachable[i].count(room)) continue;
                if (touches(data.lights[i],geometry,geometry.polygons[face],transform)) indices.push_back(static_cast<uint32_t>(i));
            }
        }
    };
    for (auto& room : data.rooms) assign(room.number,room.asset->geometry(),{},room.surfaces);
    for (auto& object : data.objects) if (object.model) for (size_t part = 0; part < object.parts.size(); ++part)
        assign(object.room,object.model->geometry().parts[part].geometry,compose(object.current,object.parts[part].current),
            object.parts[part].surfaces);
}
}
