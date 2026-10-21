// Descent 3MASTERED - GPL-3.0-or-later
#include "../../renderer/SceneFog.h"
#include "../../renderer/SceneView.h"
#include <cmath>
#include <stdexcept>
#include <limits>
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
    {
        using namespace d3scene;
        View request; request.history_key = 1; request.viewport = request.projection_anchor = {0,0,800,600};
        request.has_clip_plane = true; request.clip_plane = {{0,0,1},-4};
        auto camera = prepareCamera(request);
        check(fogMayIntersectView({{-.01f,-.01f,.01f},{.01f,.01f,.02f}},camera,true),
            "Optical candidates incorrectly used geometry near/mirror clipping");
        check(!fogMayIntersectView({{100,100,1},{101,101,2}},camera,true),"Off-axis fog remained a candidate");
        check(!fogMayIntersectView({{-1,-1,-4},{1,1,-2}},camera,true),"Fog wholly behind the eye remained a candidate");
        check(fogMayIntersectView({{-100,-100,-100},{100,100,100}},camera,true),"Enclosing fog was rejected");
        check(fogMayIntersectView({{100,100,1},{101,101,2}},camera,false),"Orthographic optical rays were perspective-culled");
        request.viewport = {200,100,400,300}; request.camera.position = {100,-20,30};
        camera = prepareCamera(request);
        uint32_t random = 887;
        const auto sample = [&] { random = random*1664525u+1013904223u; return float(random>>8)/16777216.f; };
        for (int i = 0; i < 2000; ++i) {
            const auto end = unproject(camera,sample()*2-1,sample()*2-1,.999f);
            const float t = sample(); const auto eye = camera.transform.position;
            const Vec3 p{eye.x+t*(end.x-eye.x),eye.y+t*(end.y-eye.y),eye.z+t*(end.z-eye.z)};
            check(fogMayIntersectView({{p.x-.01f,p.y-.01f,p.z-.01f},{p.x+.01f,p.y+.01f,p.z+.01f}},camera,true),
                "Fog candidate rejection lost a camera-to-surface ray segment");
        }
    }
    d3scene::SceneAssets assets;
    d3scene::FrameData frame; frame.rooms.push_back(box(assets,0,2));
    frame.fog = assets.publishFog(frame.rooms);
    check(frame.fog->volumes().size() == 1 && frame.fog->volumes()[0].boundary.size() == 12,"Fog box boundary is incomplete");
    const auto volume = frame.fog->volumes()[0];
    const auto original_asset = frame.fog;
    {
        using namespace d3scene;
        FogVolume many = volume;
        for (int x = -4; x <= 4; ++x) for (int z = -4; z <= 4; ++z) {
            if (!x && !z) continue;
            for (auto triangle : volume.boundary) {
                for (auto* point : {&triangle.a,&triangle.b,&triangle.c}) { point->x += x*8.f; point->z += z*8.f; }
                many.boundary.push_back(triangle);
            }
        }
        buildFogAcceleration(many);
        auto flat = many;
        flat.nodes.resize(1); flat.nodes[0].bounds = many.bounds;
        flat.nodes[0].first = 0; flat.nodes[0].count = uint32_t(flat.boundary.size()); flat.nodes[0].escape = 1;
        std::vector<unsigned> coverage(many.boundary.size());
        for (size_t i = 0; i < many.nodes.size(); ++i) {
            const auto& node = many.nodes[i];
            check(node.escape > i && node.escape <= many.nodes.size(),"Fog tree escape is not forward/bounded");
            check(size_t(node.first)+node.count <= many.boundary.size(),"Fog tree leaf exceeds geometry");
            for (uint32_t t = node.first; t < node.first+node.count; ++t) ++coverage[t];
        }
        for (auto count : coverage) check(count == 1,"Fog tree lost or duplicated a boundary triangle");
        View request; request.history_key = 1; request.viewport = request.projection_anchor = {0,0,800,600};
        std::vector<FogViewNode> nodes;
        auto camera = prepareCamera(request);
        prepareFogViewNodes(many,camera,false,nodes);
        bool identical = nodes.size() == many.nodes.size();
        for (size_t i = 0; identical && i < nodes.size(); ++i)
            identical = nodes[i].source == i && nodes[i].escape == many.nodes[i].escape;
        check(identical,"Unrestricted fog traversal changed tree ordering");
        uint32_t random = 12345;
        const auto sample = [&] { random = random*1664525u+1013904223u; return float(random>>8)/16777216.f; };
        for (const float eye_z : {0.f,3.f}) {
            request.camera.position.z = eye_z; camera = prepareCamera(request);
            prepareFogViewNodes(many,camera,true,nodes);
            check(!nodes.empty() && nodes.size() < many.nodes.size(),"Optical cone did not prune off-view fog branches");
            auto narrowed = many; narrowed.nodes.clear();
            for (size_t i = 0; i < nodes.size(); ++i) {
                check(nodes[i].escape > i && nodes[i].escape <= nodes.size(),"Pruned fog escape is not forward/bounded");
                auto node = many.nodes[nodes[i].source]; node.escape = nodes[i].escape; narrowed.nodes.push_back(node);
            }
            const bool inside = fogContains(many,request.camera.position);
            for (int i = 0; i < 2000; ++i) {
                const auto end = unproject(camera,sample()*2-1,sample()*2-1,.5f+sample()*.499f);
                const float start = sample()*5;
                check(fogSegmentLength(many,request.camera.position,end,inside,start) ==
                    fogSegmentLength(flat,request.camera.position,end,inside,start),
                    "Fog acceleration changed brute-force integration");
                check(fogSegmentLength(many,request.camera.position,end,inside,start) ==
                    fogSegmentLength(narrowed,request.camera.position,end,inside,start),
                    "Pruned fog traversal changed exact optical integration");
            }
        }
        FogVolume empty; prepareFogViewNodes(empty,camera,true,nodes);
        check(nodes.empty(),"Empty fog retained old view nodes");
    }
    check(assets.publishFog(frame.rooms) == original_asset,"Unchanged fog rebuilt its spatial asset");
    {
        auto invalid = frame.rooms;
        invalid[0].fog_depth = std::numeric_limits<float>::quiet_NaN();
        bool rejected = false;
        try { assets.publishFog(invalid); } catch (const std::invalid_argument&) { rejected = true; }
        check(rejected,"Retained fog skipped source validation");
        auto changed = frame.rooms;
        auto geometry = changed[0].asset->geometry(); geometry.vertices[0].x -= .1f;
        changed[0].asset = assets.publishRoom(0,std::move(geometry));
        const auto replacement = assets.publishFog(changed);
        check(replacement != original_asset && assets.publishFog(changed) == replacement,
            "Fog compilation ignored geometry revision or failed reuse");
        check(assets.publishFog(frame.rooms)->volumes()[0].boundary[0].a.x == -1,
            "Held fog geometry was replaced by a newer source slot");
    }
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
    check(assets.publishFog(frame.rooms) == frame.fog,"Connected fog did not reuse its compiled portal source");
    {
        auto invalid = frame.rooms; invalid[0].surfaces.clear();
        bool rejected = false;
        try { assets.publishFog(invalid); } catch (const std::invalid_argument&) { rejected = true; }
        check(rejected,"Retained fog indexed a missing portal surface");
    }
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
