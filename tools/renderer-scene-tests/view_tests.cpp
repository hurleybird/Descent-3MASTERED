// Descent 3MASTERED - GPL-3.0-or-later
#include "../../renderer/SceneView.h"
#include "../../renderer/ScenePortals.h"
#include <cmath>
#include <stdexcept>

namespace {
int Checks = 0;
void check(bool result, const char* message) {
    ++Checks;
    if (!result) throw std::runtime_error(message);
}
bool near(double a, double b) { return std::abs(a-b)<.00001; }
d3scene::View view() {
    d3scene::View result;
    result.history_key=1;
    result.viewport={0,0,800,600};
    result.projection_anchor=result.viewport;
    return result;
}
d3scene::MeshGeometry triangle(float x, float z, bool facing=true) {
    d3scene::MeshGeometry geometry;
    geometry.vertices={{x-1,-1,z},{x+1,-1,z},{x,1,z}};
    d3scene::MeshPolygon polygon;
    polygon.normal={0,0,facing?-1.f:1.f};
    polygon.corners={{0},{1},{2}};
    geometry.polygons.push_back(polygon);
    return geometry;
}
void appendRoom(d3scene::SceneAssets& assets,d3scene::FrameData& data,d3scene::MeshGeometry geometry) {
    d3scene::RoomState room;
    room.number=static_cast<int32_t>(data.rooms.size());
    room.asset=assets.publishRoom(room.number,std::move(geometry));
    room.surfaces.resize(1);
    room.surfaces[0].bitmap=1;
    data.rooms.push_back(std::move(room));
}
}

int runSceneViewTests() {
    auto v=view();
    auto camera=d3scene::prepareCamera(v);
    check(near(camera.projection.x_scale,1)&&near(camera.projection.y_scale,4.0/3),"Legacy 4:3 FOV changed");
    auto p=d3scene::project(camera,{0,0,1});
    check(near(p.x,0)&&near(p.y,0)&&near(p.z,-1)&&near(p.w,1),"Near plane does not map to depth zero");
    p=d3scene::project(camera,{0,0,2});
    check(near(.5*(p.z/p.w+1),.5),"Legacy 1-1/z depth changed");
    check(d3scene::sphereIntersects(camera,{0,0,100000},0),"Unexpected far clipping plane");
    check(!d3scene::sphereIntersects(camera,{0,0,.5f},0),"Point before near plane survived");
    check(d3scene::sphereIntersects(camera,{0,0,.5f},.5f),"Sphere touching near plane was culled");
    check(!d3scene::sphereIntersects(camera,{0,0,-10},1),"Sphere behind camera survived");
    check(d3scene::sphereIntersects(camera,{10,0,10},0),"Right clip-plane boundary was culled");
    check(!d3scene::sphereIntersects(camera,{12,0,10},0),"Right outside point survived");
    check(!d3scene::sphereIntersects(camera,{-12,0,10},0),"Left outside point survived");
    check(!d3scene::sphereIntersects(camera,{0,9,10},0),"Top outside point survived");
    check(!d3scene::sphereIntersects(camera,{0,-9,10},0),"Bottom outside point survived");
    v.viewport={0,0,600,800}; v.projection_anchor=v.viewport;
    camera=d3scene::prepareCamera(v);
    check(near(camera.projection.x_scale,4.0/3)&&near(camera.projection.y_scale,1),"Portrait FOV changed");
    v.viewport={0,0,1600,900}; v.projection_anchor={160,90,1280,720};
    camera=d3scene::prepareCamera(v);
    check(near(camera.projection.x_offset,0)&&near(camera.projection.y_offset,0),"Centered overscan shifted optical axis");
    check(near(camera.projection.x_scale,.6),"Overscan scale changed");
    v=view(); v.projection_anchor={200,100,400,300};
    camera=d3scene::prepareCamera(v);
    p=d3scene::project(camera,{0,0,10});
    check(near((p.x/p.w+1)*400,400)&&near((1-p.y/p.w)*300,250),"Anchored optical center is incorrect");
    v=view(); v.camera.position={10000,20000,30000};
    camera=d3scene::prepareCamera(v);
    p=d3scene::project(camera,{10000,20000,30010});
    check(near(p.x,0)&&near(p.y,0)&&near(p.w,10),"Translated camera projection changed");
    check(d3scene::sphereIntersects(camera,{10000,20000,30010},0),"Translated camera frustum changed");
    v=view(); v.camera.forward={0,0,-1}; v.camera.right={-1,0,0};
    camera=d3scene::prepareCamera(v);
    check(d3scene::sphereIntersects(camera,{0,0,-10},0)&&!d3scene::sphereIntersects(camera,{0,0,10},0),"Rear view still uses main-view planes");

    // Compare CPU half-spaces against the GPU's homogeneous clipping contract
    // over a deterministic corpus. This uses no game RNG and tests rotated,
    // translated and asymmetric views, not only the identity camera.
    bool clip_agrees=true;
    uint32_t seed=12345;
    for(int camera_index=0; camera_index<4; ++camera_index) {
        v=view();
        v.camera.position={13,-27,91};
        if(camera_index&1) {
            v.camera.right={.8f,0,-.6f};
            v.camera.forward={.6f,0,.8f};
        }
        if(camera_index&2) v.projection_anchor={-80,30,1000,550};
        camera=d3scene::prepareCamera(v);
        for(int sample=0; sample<1000; ++sample) {
            auto random=[&]() { seed=1664525u*seed+1013904223u; return (int32_t(seed>>8)-8388608)/32768.f; };
            const d3scene::Vec3 world={random(),random(),random()};
            p=d3scene::project(camera,world);
            const double distances[5]={p.w+p.x,p.w-p.x,p.w+p.y,p.w-p.y,p.w+p.z};
            bool inside=true, boundary=false;
            for(double d:distances) { inside=inside&&d>=0; boundary=boundary||std::abs(d)<.01; }
            if(!boundary && inside!=d3scene::sphereIntersects(camera,world,0)) clip_agrees=false;
        }
    }
    check(clip_agrees,"CPU visibility disagrees with GPU homogeneous clipping");

    d3scene::SceneAssets assets;
    d3scene::FrameData data;
    data.sequence=1; data.scene_lifetime=assets.lifetime();
    appendRoom(assets,data,triangle(0,10)); // visible
    appendRoom(assets,data,triangle(100,10)); // outside one plane
    appendRoom(assets,data,triangle(0,-10,false)); // facing camera, but behind it
    appendRoom(assets,data,triangle(0,.5f)); // before near plane
    appendRoom(assets,data,triangle(0,10,false)); // wrong facing
    auto giant=triangle(0,10);
    giant.vertices={{-100,-100,10},{100,-100,10},{0,100,10}};
    appendRoom(assets,data,giant); // every vertex outside, polygon covers viewport
    auto crosses=triangle(0,2);
    crosses.vertices[0].z=.5f;
    appendRoom(assets,data,crosses); // intersects near plane
    auto tangent=triangle(0,1);
    appendRoom(assets,data,tangent);
    appendRoom(assets,data,triangle(0,20));
    data.rooms.back().surfaces[0].alpha=.5;
    appendRoom(assets,data,triangle(0,15));
    data.rooms.back().surfaces[0].alpha=.5;
    d3scene::ObjectState object;
    object.handle=31; object.current.position={0,0,10}; object.radius=1;
    data.objects.push_back(object);
    object.handle=32; object.current.position={100,0,10}; data.objects.push_back(object);
    object.handle=33; object.current.position={0,0,-10}; data.objects.push_back(object);
    const auto frame=d3scene::Presentation::freeze(data);
    const auto a=d3scene::queryView(*frame,view());
    check(a.frame_sequence==1&&a.scene_lifetime==data.scene_lifetime&&a.history_key==1,"Query lost scene identity");
    check(a.surfaces.size()==6,"Conservative polygon query rejected crossing/tangent/covering geometry");
    check(a.stats.surfaces_backfacing==1&&a.stats.surfaces_outside==3,"Query rejection counters incorrect");
    check(a.surfaces[0].room_index==6&&a.surfaces[1].room_index==7,"Opaque near-to-far ordering changed");
    check(a.surfaces[4].room_index==8&&a.surfaces[5].room_index==9,"Transparent far-to-near ordering changed");
    check(a.object_indices.size()==1&&a.object_indices[0]==0,"Object sphere query incorrect");
    v=view(); v.exclude_object=true; v.excluded_object=31;
    check(d3scene::queryView(*frame,v).object_indices.empty(),"Explicit viewer exclusion ignored");
    v=view(); v.history_key=2; v.camera.forward={0,0,-1}; v.camera.right={-1,0,0};
    const auto rear=d3scene::queryView(*frame,v);
    check(rear.object_indices.size()==1&&rear.object_indices[0]==2,"Auxiliary query reused main object results");
    const auto repeated=d3scene::queryView(*frame,view());
    bool identical=a.surfaces.size()==repeated.surfaces.size();
    for(size_t i=0; identical&&i<a.surfaces.size(); ++i)
        identical=a.surfaces[i].room_index==repeated.surfaces[i].room_index && a.surfaces[i].depth==repeated.surfaces[i].depth;
    check(identical,"Auxiliary view mutated main visibility");
    check(frame->data().rooms.size()==10&&frame->data().objects.size()==3,"Visibility discarded off-camera scene content");
    d3scene::FrameData portals; portals.sequence = 2; portals.scene_lifetime = assets.lifetime();
    for (int i = 0; i < 4; ++i) appendRoom(assets,portals,triangle(0,2.f+i*2));
    portals.rooms[0].portals.push_back({0,1});
    portals.rooms[1].portals.push_back({0,2});
    portals.rooms[2].portals.push_back({0,1}); // cycle must converge without a depth cutoff
    auto portal_view = view(); portal_view.room = 0;
    auto portal_frame = d3scene::Presentation::freeze(portals);
    auto windows = d3scene::queryPortalWindows(*portal_frame,portal_view,d3scene::prepareCamera(portal_view));
    check(windows[0].visible && windows[1].visible && windows[2].visible && !windows[3].visible,"Portal reachability or cycle handling failed");
    check(d3scene::queryView(*portal_frame,portal_view).room_indices.size() == 3,"View query ignored portal traversal");
    portals.rooms[1].blocks_portals = true;
    auto door_frame = d3scene::Presentation::freeze(portals);
    check(d3scene::queryView(*door_frame,portal_view).room_indices.size() == 2,"Closed opaque door did not terminate traversal");
    portal_view.room = 1;
    check(d3scene::queryView(*door_frame,portal_view).room_indices.size() == 2,"Viewer inside closed door could not look out");
    portal_view.room = 0; portals.rooms[0].portals[0].traversable = false;
    check(d3scene::queryView(*d3scene::Presentation::freeze(portals),portal_view).room_indices.size() == 1,"Opaque portal was traversed");
    portals.rooms[0].portals[0].traversable = true; portals.rooms[1].blocks_portals = false;
    portals.rooms[0].asset = assets.publishRoom(0,triangle(0,.5f));
    check(d3scene::queryView(*d3scene::Presentation::freeze(portals),portal_view).room_indices.size() == 3,"Aperture before near plane hid destination rooms");
    portals.rooms[0].asset = assets.publishRoom(0,triangle(0,-2));
    check(d3scene::queryView(*d3scene::Presentation::freeze(portals),portal_view).room_indices.size() == 1,"Portal behind the camera was traversed");
    portals.rooms[0].asset = assets.publishRoom(0,triangle(100,2));
    check(d3scene::queryView(*d3scene::Presentation::freeze(portals),portal_view).room_indices.size() == 1,"Offscreen aperture was traversed");
    portal_view.portal_visibility = false;
    check(d3scene::queryView(*d3scene::Presentation::freeze(portals),portal_view).room_indices.size() == 4,"Conservative reference query lost off-camera scene availability");
    // Two openings into the same destination must widen its window, independent
    // of traversal order; a first-visited mark would incorrectly lose one.
    auto openings = triangle(-3,5);
    auto right_opening = triangle(3,5);
    openings.vertices.insert(openings.vertices.end(),right_opening.vertices.begin(),right_opening.vertices.end());
    auto polygon = right_opening.polygons[0]; for (auto& corner : polygon.corners) corner.vertex += 3;
    openings.polygons.push_back(polygon);
    portals.rooms[0].asset = assets.publishRoom(0,openings); portals.rooms[0].surfaces.resize(2);
    portals.rooms[0].portals = {{0,1},{1,1}};
    portal_view.portal_visibility = true;
    windows = d3scene::queryPortalWindows(*d3scene::Presentation::freeze(portals),portal_view,d3scene::prepareCamera(portal_view));
    check(windows[1].visible && windows[1].left < -.79 && windows[1].right > .79,"Multiple portal windows were not unioned");
    check(portal_frame->data().rooms[0].asset->geometry().vertices[0].z == 2,"Portal query changed held scene assets");
    return Checks;
}
