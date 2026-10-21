// Descent 3MASTERED - GPL-3.0-or-later
#include "../../renderer/SceneLighting.h"
#include "../../renderer/SceneGeometry.h"
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
    VolumeGrid grid; grid.width = grid.height = grid.depth = 2;
    grid.origin = {10,20,30}; grid.colors = {0,224,28,252,3,227,31,255};
    auto center = sampleVolume(grid,{15,25,35});
    check(center.x == .5f && center.y == .5f && center.z == .5f,"RGB332 trilinear volume sample changed");
    auto edge = sampleVolume(grid,{999,999,-999});
    check(edge.x == 1 && edge.y == 1 && edge.z == 0,"Volume coordinates did not clamp to grid");
    grid.width = grid.height = grid.depth = 1; grid.colors = {255};
    check(sampleVolume(grid,{-999,999,42}).z == 1,"Singleton volume grid failed");
    check(sampleVolume(VolumeGrid{},{}).x == 1,"Missing authored volume must be white");
    check(roomLightScalar(.02f,1,0,0,false,false,false) == .5f,"Minimum pulse half period changed");
    check(roomLightScalar(0,0,0,1,true,false,false) == 0,"Room strobe phase lost identity");
    check(roomLightScalar(0,1,0,1,true,true,true) == 1,"Editor light policy ignored");
    for (int tick = 0; tick < 100; ++tick) {
        const float time = tick*.031f;
        const float half = .73f, clock = time+.17f;
        const float phase = (clock-static_cast<int>(clock/(half*2))*half*2)/half;
        const float expected = phase > 1 ? 1-(phase-1) : phase;
        check(std::fabs(roomLightScalar(time,73,17,0,false,false,false)-expected)<.00001f,"Room pulse differs from source rule");
    }
    Light point;
    {
        uint32_t seed = 0xc71f25u; bool exercised_roundoff = false;
        for (unsigned sample = 0; sample < 200000; ++sample) {
            seed = seed*1664525u+1013904223u;
            const float time = float(seed&0xffffffu)*.0625f;
            const uint8_t pulse = uint8_t(sample%255+1), offset = uint8_t(sample/255);
            const float half = std::max(pulse*.01f,.04f), clock = time+offset*.01f;
            const float phase = (clock-static_cast<int>(clock/(half*2))*(half*2))/half;
            const float old = phase > 1 ? 2-phase : phase;
            const float actual = roomLightScalar(time,pulse,offset,0,false,false,false);
            check(std::isfinite(actual) && actual >= 0 && actual <= 1,"Pulse wrap produced invalid radiance");
            if (old >= 0 && old <= 1) check(actual == old,"Pulse repair changed an in-range authored sample");
            else exercised_roundoff = true;
        }
        check(exercised_roundoff,"Pulse regression test did not exercise boundary roundoff");
    }
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
    auto volume_asset = assets.publishRoom(8,mesh,grid);
    {
        ModelAmbientInput input;
        const Vec3 authored{.2f,.4f,.6f};
        check(resolveModelAmbient(input,{},authored).z == authored.z,"Constant model ambient changed");
        input.source = ModelAmbientInput::Source::RoomVolume;
        input.volume = volume_asset; input.volume_scalar = .5f;
        check(resolveModelAmbient(input,{},authored).x == .5f,"Deferred ambient lost captured room pulse");
        input.blend_previous = true; input.previous_weight = .25f;
        input.previous_volume = volume_asset; input.previous_scalar = .25f;
        check(resolveModelAmbient(input,{},authored).x == .4375f,"Deferred volume transition changed operation order");
        input.minimum = .6f; input.clamp_after_multiplier = true; input.multiplier = 2;
        check(resolveModelAmbient(input,{},authored).x == 1,"Deferred ambient lost minimum/speed clamp");
        input = {}; input.source = ModelAmbientInput::Source::RoomVolume; input.volume_scalar = 0;
        check(resolveModelAmbient(input,{},authored).x == 1,"Missing volume applied room pulse to fallback white");
    }
    check(assets.publishRoom(8,mesh,grid) == volume_asset,"Unchanged light volume lost revision");
    grid.colors[0] = 0;
    check(assets.publishRoom(8,mesh,grid)->id() != volume_asset->id() && volume_asset->volume().colors[0] == 255,
        "Light volume revisions are not immutable");
    grid.width = 2;
    rejects([&] { assets.publishRoom(8,mesh,grid); });
    FrameData data; data.sequence = 1; data.scene_lifetime = assets.lifetime(); data.lights = {point};
    for (int i = 0; i < 3; ++i) {
        RoomState room; room.number = i; room.asset = assets.publishRoom(i,mesh); room.surfaces.resize(1);
        data.rooms.push_back(room);
    }
    data.rooms[0].portals.push_back({0,1,0}); data.rooms[1].portals.push_back({0,0,0});
    PreparedMesh effect; effect.asset = assets.publishMesh(1,mesh);
    data.meshes.assign(3,effect); data.meshes[2].surface.unlit = true;
    prepareLightInfluences(data);
    check(data.meshes[0].light_influences_prepared && data.meshes[0].surface.light_indices.empty() &&
        data.meshes[0].light_candidates.offset == data.meshes[1].light_candidates.offset &&
        data.lightCandidates(data.meshes[0].light_candidates).size() == 1 &&
        data.lightCandidates(data.meshes[0].light_candidates)[0] == 0 && data.meshes[2].light_candidates.empty(),
        "Effects duplicated their canonical light set or lit an unlit material");
    check(data.lightCandidates(data.rooms[0].light_candidates).size() == 1 &&
        data.lightCandidates(data.rooms[0].light_candidates)[0] == 0,"Source room lost emitter reference");
    check(data.lightCandidates(data.rooms[1].light_candidates).size() == 1 &&
        data.lightCandidates(data.rooms[1].light_candidates)[0] == 0,"Connected room lost emitter reference");
    check(data.rooms[2].light_candidates.empty(),"Light leaked into disconnected room");
    auto frame = Presentation::freeze(data);
    std::vector<int32_t> submitted{17};
    appendSurfaceLightIndices(frame->data(),frame->data().rooms[0].light_candidates,
        MeshView(mesh),mesh.polygons[0],{},submitted);
    check(submitted == std::vector<int32_t>({17,0}),"View refinement changed emitter order or overwrote prior ranges");
    Transform distant; distant.position = {1000,0,0};
    submitted.clear();
    appendSurfaceLightIndices(frame->data(),frame->data().rooms[0].light_candidates,
        MeshView(mesh),mesh.polygons[0],distant,submitted);
    check(submitted.empty(),"View refinement ignored posed polygon position");
    {
        FrameData candidates;
        for (uint32_t i = 0; i < 32; ++i) {
            Light light = point;
            light.position = {float(int(i%7)-3)*7,float(int(i%5)-2)*9,float(int(i%11)-5)*3};
            light.radius = 1.f+float(i%13)*3; light.directional = (i%3 == 0);
            candidates.lights.push_back(light); candidates.light_candidate_indices.push_back(i);
        }
        const auto& polygon = mesh.polygons[0];
        for (int trial = 0; trial < 512; ++trial) {
            Transform placement;
            const float angle = float(trial)*.037f;
            placement.right = {std::cos(angle),0,std::sin(angle)};
            placement.forward = {-std::sin(angle),0,std::cos(angle)};
            placement.position = {float(trial%17)-8,float(trial%13)-6,float(trial%19)-9};
            std::vector<int32_t> expected{123}, actual{123};
            for (uint32_t i = 0; i < candidates.lights.size(); ++i) {
                const auto& light = candidates.lights[i];
                auto low = transformPoint(placement,mesh.vertices[polygon.corners[0].vertex]), high = low;
                const auto normal = transformDirection(placement,polygon.normal);
                if (light.directional && normal.x*(light.position.x-low.x)+normal.y*(light.position.y-low.y)+
                    normal.z*(light.position.z-low.z) < 0) continue;
                for (const auto& corner : polygon.corners) {
                    const auto p = transformPoint(placement,mesh.vertices[corner.vertex]);
                    low = {std::min(low.x,p.x),std::min(low.y,p.y),std::min(low.z,p.z)};
                    high = {std::max(high.x,p.x),std::max(high.y,p.y),std::max(high.z,p.z)};
                }
                const float x = std::max(low.x-light.position.x,std::max(0.f,light.position.x-high.x));
                const float y = std::max(low.y-light.position.y,std::max(0.f,light.position.y-high.y));
                const float z = std::max(low.z-light.position.z,std::max(0.f,light.position.z-high.z));
                if (x*x+y*y+z*z <= light.radius*light.radius) expected.push_back(int32_t(i));
            }
            appendSurfaceLightIndices(candidates,{0,32},MeshView(mesh),polygon,placement,actual);
            check(actual == expected,"Shared surface bounds changed light coverage or candidate order");
        }
    }
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
    check(data.rooms[0].light_candidates.empty(),"Influence compiler ignored source radius");
    submitted.clear();
    appendSurfaceLightIndices(frame->data(),frame->data().rooms[0].light_candidates,
        MeshView(mesh),mesh.polygons[0],{},submitted);
    check(submitted == std::vector<int32_t>{0},"Later frame preparation changed held-frame light refinement");
    {
        FrameData keys; keys.sequence = 1; keys.scene_lifetime = assets.lifetime();
        for (uint64_t i = 100; i > 0; --i) {
            auto light = point; light.id.owner = i; keys.lights.push_back(light);
        }
        const auto held = Presentation::freeze(keys);
        keys.lights.push_back(keys.lights[37]);
        rejects([&] { Presentation::freeze(keys); });
        keys.lights.clear(); keys.lights.push_back(point);
        check(Presentation::freeze(keys)->data().lights.size() == 1 && held->data().lights.size() == 100,
            "Publication uniqueness scratch retained a failed frame or mutated a held frame");
        keys.camera_perturbations.resize(2);
        rejects([&] { Presentation::freeze(keys); });
        keys.camera_perturbations.back().owner = 1;
        check(Presentation::freeze(keys)->data().camera_perturbations.size() == 2,"Distinct camera owners rejected");
        keys.foregrounds.resize(2);
        View overlay; overlay.kind = ViewKind::Preview; overlay.history_key = 1;
        overlay.viewport = overlay.projection_anchor = {0,0,64,64};
        for (auto& foreground : keys.foregrounds) foreground.layer.scene_views.push_back(overlay);
        keys.foregrounds[0].identity = keys.foregrounds[1].identity = 1;
        keys.foregrounds[1].order = 1;
        rejects([&] { Presentation::freeze(keys); });
        keys.foregrounds[1].identity = 2; keys.foregrounds[1].order = 0;
        rejects([&] { Presentation::freeze(keys); });
        keys.foregrounds[1].phase = ForegroundPhase::FinalOverlay;
        check(Presentation::freeze(keys)->data().foregrounds.size() == 2,"Independent foreground phases collapsed");
    }
    return checks;
}
