// Descent 3MASTERED - GPL-3.0-or-later
#include "../../renderer/SceneFog.h"
#include <cmath>
#include <stdexcept>
namespace {
int Checks = 0;
void check(bool value,const char* message) { ++Checks; if (!value) throw std::runtime_error(message); }
bool near(float a,float b) { return std::abs(a-b) < .0001f; }
d3scene::RoomState box(d3scene::SceneAssets& assets,int id,float z) {
    d3scene::MeshGeometry mesh;
    mesh.vertices = {{-1,-1,z},{1,-1,z},{1,1,z},{-1,1,z},{-1,-1,z+2},{1,-1,z+2},{1,1,z+2},{-1,1,z+2}};
    mesh.polygons = {{{0,0,1},{{0},{1},{2},{3}}},{{0,0,-1},{{4},{7},{6},{5}}},
        {{1,0,0},{{0},{3},{7},{4}}},{{-1,0,0},{{1},{5},{6},{2}}},
        {{0,1,0},{{0},{4},{5},{1}}},{{0,-1,0},{{3},{2},{6},{7}}}};
    d3scene::RoomState room; room.number = id; room.asset = assets.publishRoom(id,mesh);
    room.surfaces.resize(6); room.fog_enabled = true; room.fog_depth = 10; room.fog_color = {.2f,.4f,.8f};
    return room;
}
}
int runFogTests() {
    d3scene::SceneAssets assets;
    d3scene::FrameData frame; frame.rooms.push_back(box(assets,0,2));
    frame.fog = assets.publishFog(frame.rooms);
    check(frame.fog->volumes().size() == 1 && frame.fog->volumes()[0].boundary.size() == 12,"Fog box boundary is incomplete");
    const auto volume = frame.fog->volumes()[0];
    const auto original_asset = frame.fog;
    check(assets.publishFog(frame.rooms) == original_asset,"Unchanged fog rebuilt its spatial asset");
    frame.rooms[0].fog_color.x = .7f;
    const auto recolored = assets.publishFog(frame.rooms);
    check(recolored->id() != original_asset->id() && original_asset->volumes()[0].color.x == .2f,
        "Fog material revision mutated held scene data");
    frame.rooms[0].fog_color.x = .2f;
    check(d3scene::fogContains(volume,{0,0,3}),"Fog containment missed inside point");
    check(!d3scene::fogContains(volume,{0,0,0}),"Fog containment included outside point");
    check(near(d3scene::fogSegmentLength(volume,{0,0,0},{0,0,10},false),2),"Diagonal crossing counted fog triangles twice");
    check(near(d3scene::fogSegmentLength(volume,{0,0,0},{0,0,3},false),1),"Entry fog path is incorrect");
    check(near(d3scene::fogSegmentLength(volume,{0,0,3},{0,0,10},true),1),"Exit fog path is incorrect");
    check(near(d3scene::fogSegmentLength(volume,{0,0,3},{0,0,3.5f},true),.5f),"Interior fog path is incorrect");
    check(near(d3scene::fogSegmentLength(volume,{2,0,0},{2,0,10},false),0),"Ray beside fog volume was fogged");
    auto separated = volume;
    for (auto triangle : volume.boundary) {
        triangle.a.z += 4; triangle.b.z += 4; triangle.c.z += 4;
        separated.boundary.push_back(triangle);
    }
    d3scene::buildFogAcceleration(separated);
    check(!d3scene::fogContains(separated,{0,0,5}) && d3scene::fogContains(separated,{0,0,7}),
        "Non-convex fog containment filled a clear interval");
    check(near(d3scene::fogSegmentLength(separated,{0,0,0},{0,0,10},false),4),"Separated fog intervals lost an exit or entry");
    check(near(d3scene::fogSegmentLength(separated,{0,0,3},{0,0,7},true),2),"Clear space between fog intervals was integrated");
    // The excluded prefix still determines winding at the optical start. Test
    // starts before, inside, between and after disconnected fog intervals.
    for (int i = 0; i <= 100; ++i) {
        const float start = i*.1f;
        const float expected = (std::max)(0.f,4.f-(std::max)(2.f,start)) +
            (std::max)(0.f,8.f-(std::max)(6.f,start));
        check(near(d3scene::fogSegmentLength(separated,{0,0,0},{0,0,10},false,start),expected),
            "Trimmed fog interval lost pre-start containment");
        check(near(d3scene::fogSegmentLength(separated,{0,0,10},{0,0,0},false,start),expected),
            "Trimmed fog interval depends on ray orientation");
    }
    check(near(d3scene::fogSegmentLength(separated,{0,0,3},{0,0,9},true,4),1),
        "Trimmed fog ray from inside retained an excluded interval");
    check(near(d3scene::fogSegmentLength(volume,{0,0,3},{0,0,3.5f},true,1),0),
        "Fog start beyond endpoint produced a negative interval");
    d3scene::View clipped; clipped.history_key = 1;
    clipped.viewport = clipped.projection_anchor = {0,0,800,600};
    clipped.has_clip_plane = true; clipped.clip_plane = {{0,0,1},-3};
    check(near(d3scene::fogRayStart(clipped,{0,0,10}),0),"Geometry clipping implicitly changed optical policy");
    clipped.fog_path_origin = d3scene::FogPathOrigin::ClipPlane;
    check(near(d3scene::fogRayStart(clipped,{0,0,10}),3),"Fog ray did not start on physical plane");
    check(near(d3scene::fogRayStart(clipped,{4,0,3}),5),"Oblique fog start used axial distance");
    check(near(d3scene::fogRayStart(clipped,{0,0,2}),2),"Behind-plane fragment integrated fog");
    check(near(d3scene::fogRayStart(clipped,{4,0,0}),4),"Parallel excluded ray integrated fog");
    clipped.camera.position.z = 4;
    check(near(d3scene::fogRayStart(clipped,{0,0,10}),0),"Retained camera ray lost its prefix");
    clipped.has_clip_plane = false;
    bool missing_plane = false;
    try { d3scene::validateView(clipped); } catch (const std::invalid_argument&) { missing_plane = true; }
    check(missing_plane,"Optical plane policy accepted missing plane");
    auto trigger_geometry = frame.rooms[0].asset->geometry();
    auto trigger = trigger_geometry.polygons[0]; trigger.bounds_volume = false;
    trigger_geometry.polygons.push_back(trigger);
    frame.rooms[0].asset = assets.publishRoom(0,trigger_geometry); frame.rooms[0].surfaces.resize(7);
    frame.fog = assets.publishFog(frame.rooms);
    check(frame.fog->volumes()[0].boundary.size() == 12,"Floating trigger became a spatial fog boundary");
    // Off-diagonal rays and reversed direction must agree with slab intersection.
    for (int i = -49; i <= 49; ++i) {
        const float x = i/50.f, y = i/99.f;
        check(near(d3scene::fogSegmentLength(volume,{x,y,0},{x,y,9},false),2),"Fog ray disagrees with analytic slab");
        check(near(d3scene::fogSegmentLength(volume,{x,y,9},{x,y,0},false),2),"Fog path depends on viewing direction");
    }
    frame.rooms.push_back(box(assets,1,4));
    frame.rooms[0].portals.push_back({1,1,0}); frame.rooms[0].surfaces[1].draw_surface = false;
    frame.rooms[1].portals.push_back({0,0,0}); frame.rooms[1].surfaces[0].draw_surface = false;
    frame.fog = assets.publishFog(frame.rooms);
    check(frame.fog->volumes().size() == 1 && frame.fog->volumes()[0].rooms.size() == 2 && frame.fog->volumes()[0].boundary.size() == 20,
        "Matching connected fog was split or retained internal portal faces");
    check(near(d3scene::fogSegmentLength(frame.fog->volumes()[0],{0,0,0},{0,0,10},false),4),"Connected room seam changed optical distance");
    check(near(d3scene::fogSegmentLength(frame.fog->volumes()[0],{0,0,4},{0,0,10},true),2),"Camera on internal portal lost fog");
    frame.rooms[1].fog_color.x = .3f; frame.fog = assets.publishFog(frame.rooms);
    check(frame.fog->volumes().size() == 2,"Different authored fog colors were merged");
    frame.rooms[1].fog_color.x = .2f; frame.rooms[1].surfaces[0].draw_surface = true; frame.fog = assets.publishFog(frame.rooms);
    check(frame.fog->volumes().size() == 2,"Rendered portal sheet failed to separate fog components");
    frame.rooms[0].fog_enabled = frame.rooms[1].fog_enabled = false; frame.fog = assets.publishFog(frame.rooms);
    check(frame.fog->volumes().empty(),"Disabling fog retained stale volumes");
    assets.reset();
    check(original_asset->volumes()[0].boundary.size() == 12,"Level unload destroyed held fog geometry");
    d3scene::FrameData stale; stale.sequence = 1; stale.scene_lifetime = assets.lifetime(); stale.fog = original_asset;
    bool rejected = false;
    try { d3scene::Presentation::freeze(stale); } catch (const std::invalid_argument&) { rejected = true; }
    check(rejected,"Fog asset from another level lifetime was accepted");
    return Checks;
}
