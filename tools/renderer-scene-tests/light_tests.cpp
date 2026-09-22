// Descent 3MASTERED - GPL-3.0-or-later
#include "../../renderer/SceneLighting.h"
#include <stdexcept>
#include <cmath>
namespace {
int checks;
void check(bool result,const char* message) { ++checks; if (!result) throw std::runtime_error(message); }
template<class F> void rejects(F fn) {
    bool rejected = false;
    try { fn(); } catch (const std::invalid_argument&) { rejected = true; }
    check(rejected,"Invalid scene light accepted");
}
}
int runLightTests() {
    using namespace d3scene;
    checks = 0;
    Light point;
    point.id.owner = 123; point.radius = 20; point.position = {0,0,0}; point.color = {1,0,-0.5f}; point.room = 0;
    check(lightAttenuation(point,{0,0,0}) == 1,"Point light origin attenuation changed");
    check(lightAttenuation(point,{0,0,10}) == .5f,"Point light linear attenuation changed");
    check(lightAttenuation(point,{0,0,20}) == 0,"Light extends beyond radius");
    auto cone = point; cone.directional = true; cone.cone = .75f;
    check(lightAttenuation(cone,{0,0,10}) == .5f,"Headlight forward attenuation changed");
    check(lightAttenuation(cone,{0,0,-10}) == 0,"Spotlight lights behind emitter");
    cone.falloff = 2;
    check(lightAttenuation(cone,{0,0,10}) == .25f,"Light exponent ignored");
    for (int tick = 0; tick < 100; ++tick) {
        const double time = tick*.17;
        uint32_t expected = static_cast<uint32_t>(std::floor(time*60))*0x9e3779b9u;
        expected ^= 42*0x85ebca6bu; expected ^= 3*0xc2b2ae35u;
        expected ^= expected >> 16; expected *= 0x7feb352du; expected ^= expected >> 15; expected *= 0x846ca68bu;
        expected ^= expected >> 16;
        check(visualNoise(time,42,3) == expected,"Explicit visual clock changed flicker sequence");
    }
    SceneAssets assets;
    MeshGeometry mesh;
    mesh.vertices = {{-1,-1,5},{0,1,5},{1,-1,5}};
    mesh.polygons.push_back({{0,0,-1},{{0},{1},{2}}});
    FrameData data; data.sequence = 1; data.scene_lifetime = assets.lifetime(); data.lights = {point};
    for (int i = 0; i < 3; ++i) {
        RoomState room; room.number = i; room.asset = assets.publishRoom(i,mesh); room.surfaces.resize(1);
        data.rooms.push_back(room);
    }
    data.rooms[0].portals.push_back({0,1,0}); data.rooms[1].portals.push_back({0,0,0});
    prepareLightInfluences(data);
    check(data.rooms[0].surfaces[0].light_indices == std::vector<uint32_t>{0},"Source room lost emitter reference");
    check(data.rooms[1].surfaces[0].light_indices == std::vector<uint32_t>{0},"Connected room lost emitter reference");
    check(data.rooms[2].surfaces[0].light_indices.empty(),"Light leaked into disconnected room");
    auto frame = Presentation::freeze(data);
    data.lights[0].color.x = 99;
    check(frame->data().lights[0].color.x == 1,"Published emitter was mutated");
    data = frame->data();
    data.lights.push_back(point);
    rejects([&] { Presentation::freeze(data); });
    data.lights.back().id.channel = 1;
    check(Presentation::freeze(data)->data().lights.size() == 2,"Distinct emitter channels collapsed by value");
    data.rooms[0].surfaces[0].light_indices = {2};
    rejects([&] { Presentation::freeze(data); });
    data = frame->data(); data.lights[0].radius = -1;
    rejects([&] { Presentation::freeze(data); });
    data = frame->data(); data.lights[0].directional = true; data.lights[0].direction.z = 3;
    rejects([&] { Presentation::freeze(data); });
    data = frame->data(); data.lights[0].position.x = 1000;
    prepareLightInfluences(data);
    check(data.rooms[0].surfaces[0].light_indices.empty(),"Influence compiler ignored source radius");
    return checks;
}
