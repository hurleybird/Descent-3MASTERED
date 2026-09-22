// Descent 3MASTERED - GPL-3.0-or-later
#include "../../renderer/SceneView.h"
#include "../../renderer/ScenePortals.h"
#include "../../renderer/SceneGeometry.h"
#include "../../renderer/SceneCompositing.h"
#include "../../renderer/SceneHistory.h"
#include "../../renderer/SceneSamples.h"
#include "../../renderer/SceneMotion.h"
#include <limits>
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
    {
        d3scene::SceneAssets overlay_assets;
        d3scene::FrameData overlay_data;
        overlay_data.sequence = 1; overlay_data.scene_lifetime = overlay_assets.lifetime();
        for (int i = 0; i < 3; ++i) {
            d3scene::PreparedMesh mesh;
            mesh.asset = overlay_assets.publishMesh(i,triangle(0,2.f+i)); mesh.collection = 7;
            mesh.surface.depth = d3scene::DepthMode::Disabled;
            mesh.surface.blend = i == 1 ? d3scene::BlendMode::Solid : d3scene::BlendMode::Alpha;
            mesh.surface.composition_order = i == 1 ? -1 : 0;
            overlay_data.meshes.push_back(mesh);
        }
        auto overlay_view = view(); overlay_view.collection = 7; overlay_view.kind = d3scene::ViewKind::Preview;
        overlay_view.ordering = d3scene::ViewOrdering::Compositing;
        const auto overlay_frame = d3scene::Presentation::freeze(overlay_data);
        const auto visible = d3scene::queryView(*overlay_frame,overlay_view);
        check(visible.surfaces.size() == 3 && visible.surfaces[0].mesh_index == 1 &&
            visible.surfaces[1].mesh_index == 0 && visible.surfaces[2].mesh_index == 2,
            "Compositing lost authored order/ties or grouped by alpha/depth");
        const auto schedule = d3scene::buildViewPasses(*overlay_frame,visible);
        check(schedule.size() == 1 && schedule[0].kind == d3scene::ViewPassKind::CompositeSurfaces &&
            schedule[0].begin == 0 && schedule[0].end == 3,"Compositing split its ordered coverage into world passes");
        bool rejected = false;
        try { d3scene::buildViewPasses(*overlay_frame,visible,{true,false}); }
        catch (const std::invalid_argument&) { rejected = true; }
        check(rejected,"Compositing claimed an opaque world image product");
        overlay_data.meshes[0].surface.depth = d3scene::DepthMode::ReadOnly;
        const auto invalid_frame = d3scene::Presentation::freeze(overlay_data);
        rejected = false;
        try { d3scene::buildViewPasses(*invalid_frame,d3scene::queryView(*invalid_frame,overlay_view)); }
        catch (const std::invalid_argument&) { rejected = true; }
        check(rejected,"Depth-dependent geometry entered a depth-independent compositing view");
        d3scene::ViewHistory history;
        history.commit(history.prepare(overlay_data,overlay_view,d3scene::prepareCamera(overlay_view)));
        ++overlay_data.sequence; overlay_view.ordering = d3scene::ViewOrdering::Depth;
        check(!history.prepare(overlay_data,overlay_view,d3scene::prepareCamera(overlay_view)).previous_valid,
            "View history crossed a compositing-order change");
        overlay_data.meshes[0].surface.raster_stripe = {6,14,.5f};
        check(d3scene::Presentation::freeze(overlay_data)->data().meshes[0].surface.raster_stripe.x == 6,
            "Raster coverage was not preserved in the immutable material");
        for (const auto stripe : {d3scene::Vec3{.5f,0,.5f},d3scene::Vec3{3,0,2},
            d3scene::Vec3{3,std::numeric_limits<float>::infinity(),.5f}}) {
            overlay_data.meshes[0].surface.raster_stripe = stripe; rejected = false;
            try { d3scene::Presentation::freeze(overlay_data); } catch (const std::invalid_argument&) { rejected = true; }
            check(rejected,"Invalid raster stripe reached publication");
        }
    }
    {
        const auto normal = d3scene::rasterViewport({20,30,800,600},1440,2);
        check(normal.x == 40 && normal.y == 180 && normal.width == 1600 && normal.height == 1200,
            "Scene viewport lost supersampling or top-left conversion");
        const auto overscan = d3scene::rasterViewport({-32,-18,1344,756},1512,2,32,18);
        check(overscan.x == 0 && overscan.y == 0 && overscan.width == 2688 && overscan.height == 1512,
            "Scene viewport lost allocation overscan origin");
        const auto display = d3scene::rasterViewport({640,360,640,360},720,1);
        check(display.x == 640 && display.y == 0 && display.width == 640 && display.height == 360,
            "Display viewport inherited scene supersampling");
        for (int mode = 0; mode < 3; ++mode) {
            bool rejected = false;
            try { d3scene::rasterViewport({0,0,mode == 2 ? INT32_MAX : 800,600},720,mode == 0 ? 0 : 2,mode == 1 ? INT32_MAX : 0); }
            catch (const std::invalid_argument&) { rejected = true; }
            check(rejected,"Raster viewport accepted invalid scale or overflowing coordinates");
        }
        auto request = view(); request.target = d3scene::ViewTarget::Display;
        bool rejected = false;
        try { d3scene::validateView(request); } catch (const std::invalid_argument&) { rejected = true; }
        check(rejected,"Main world view accepted display-space output");
        request.kind = d3scene::ViewKind::Preview;
        d3scene::validateView(request);
        request.feedback = d3scene::Automap;
        rejected = false;
        try { d3scene::validateView(request); } catch (const std::invalid_argument&) { rejected = true; }
        check(rejected,"Display preview accepted gameplay feedback");
    }
    check(d3scene::imageReductionSteps(1).empty(),"Native resolution introduced a reduction pass");
    const auto reduction2 = d3scene::imageReductionSteps(2), reduction4 = d3scene::imageReductionSteps(4);
    check(reduction2.size() == 1 && reduction2[0].source_scale == 2 && reduction2[0].destination_scale == 1,
        "Two-times layer reduction changed kernel scale");
    check(reduction4.size() == 2 && reduction4[0].source_scale == 4 && reduction4[0].destination_scale == 2 &&
        reduction4[1].source_scale == 2 && reduction4[1].destination_scale == 1,"Four-times layer skipped an intermediate reduction");
    bool invalid_reduction = false;
    try { d3scene::imageReductionSteps(3); } catch (const std::invalid_argument&) { invalid_reduction = true; }
    check(invalid_reduction,"Unsupported reduction scale silently changed filter");
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
    auto exterior_data = portals;
    exterior_data.rooms[0].exterior = true;
    auto exterior_view = view(); exterior_view.exterior_origin = true;
    auto exterior_frame = d3scene::Presentation::freeze(exterior_data);
    auto exterior_windows = d3scene::queryPortalWindows(*exterior_frame,exterior_view,d3scene::prepareCamera(exterior_view));
    check(exterior_windows[0].visible && exterior_windows[1].visible && exterior_windows[2].visible && !exterior_windows[3].visible,
        "Exterior origin exposed disconnected interiors");
    exterior_data.rooms[1].blocks_portals = true;
    exterior_windows = d3scene::queryPortalWindows(*d3scene::Presentation::freeze(exterior_data),exterior_view,d3scene::prepareCamera(exterior_view));
    check(exterior_windows[1].visible && !exterior_windows[2].visible,"Exterior seed bypassed a closed door");
    auto reversed_apertures = exterior_data.rooms[0].asset->geometry();
    for (auto& face : reversed_apertures.polygons) face.normal = {0,0,1};
    exterior_data.rooms[0].asset = assets.publishRoom(0,reversed_apertures);
    exterior_windows = d3scene::queryPortalWindows(*d3scene::Presentation::freeze(exterior_data),exterior_view,d3scene::prepareCamera(exterior_view));
    check(!exterior_windows[1].visible,"One-sided backfacing aperture was traversed");
    for (auto& portal : exterior_data.rooms[0].portals) portal.two_sided = true;
    exterior_windows = d3scene::queryPortalWindows(*d3scene::Presentation::freeze(exterior_data),exterior_view,d3scene::prepareCamera(exterior_view));
    check(exterior_windows[1].visible && !exterior_windows[2].visible,"Two-sided door aperture lost visibility or bypassed door closure");
    exterior_data.rooms[0].exterior = false;
    exterior_windows = d3scene::queryPortalWindows(*d3scene::Presentation::freeze(exterior_data),exterior_view,d3scene::prepareCamera(exterior_view));
    check(!exterior_windows[0].visible && !exterior_windows[3].visible,"No exterior shells fell back to all interiors");
    exterior_data.rooms[3].exterior = true;
    exterior_data.rooms[3].asset = assets.publishRoom(3,triangle(100,5));
    exterior_windows = d3scene::queryPortalWindows(*d3scene::Presentation::freeze(exterior_data),exterior_view,d3scene::prepareCamera(exterior_view));
    check(!exterior_windows[3].visible,"Offscreen exterior shell seeded traversal");
    exterior_data.rooms[3].asset = assets.publishRoom(3,triangle(0,.5f));
    exterior_windows = d3scene::queryPortalWindows(*d3scene::Presentation::freeze(exterior_data),exterior_view,d3scene::prepareCamera(exterior_view));
    check(exterior_windows[3].visible,"Near-plane exterior shell lost aperture eligibility");
    exterior_data.rooms[3].asset = assets.publishRoom(3,triangle(0,-5));
    exterior_windows = d3scene::queryPortalWindows(*d3scene::Presentation::freeze(exterior_data),exterior_view,d3scene::prepareCamera(exterior_view));
    check(!exterior_windows[3].visible,"Exterior shell behind camera seeded traversal");
    auto enclosing = triangle(0,5); enclosing.vertices[0] = {-100,-100,-5}; enclosing.vertices[1] = {100,100,5};
    exterior_data.rooms[3].asset = assets.publishRoom(3,enclosing);
    exterior_windows = d3scene::queryPortalWindows(*d3scene::Presentation::freeze(exterior_data),exterior_view,d3scene::prepareCamera(exterior_view));
    check(exterior_windows[3].visible,"Eye-straddling exterior shell was culled by projected corners");
    d3scene::FrameData shaken; shaken.sequence = 3; shaken.scene_lifetime = assets.lifetime();
    d3scene::Transform rotation; rotation.right = {0,0,-1}; rotation.forward = {1,0,0};
    shaken.camera_perturbations.push_back({42,rotation});
    const auto shake_frame = d3scene::Presentation::freeze(shaken);
    auto attached = view(); attached.camera.position = {1,2,3}; attached.attached_camera = true; attached.camera_owner = 42;
    auto resolved = d3scene::resolveView(*shake_frame,attached);
    check(resolved.camera.forward.x == 1 && resolved.camera.position.x == 1 && resolved.camera.position.z == 3,
        "Camera perturbation moved eye or failed to rotate view");
    check(d3scene::resolveView(*shake_frame,resolved).camera.forward.x == 1,"Resolved camera applied shake twice");
    check(attached.camera.forward.z == 1,"Camera preparation mutated input");
    check(d3scene::queryView(*shake_frame,attached).camera.transform.forward.x == 1,"Visibility ignored prepared camera");
    attached.camera_owner = 43;
    check(d3scene::resolveView(*shake_frame,attached).camera.forward.z == 1,"Shake leaked into another object's camera");
    attached.camera_owner = 42; attached.attached_camera = false;
    check(d3scene::resolveView(*shake_frame,attached).camera.forward.z == 1,"Detached preview inherited player shake");
    d3scene::FrameData environment; environment.sequence = 4; environment.scene_lifetime = assets.lifetime();
    auto colored = triangle(0,10); colored.vertex_colors = {{1,0,0},{0,1,0},{0,0,1}};
    const auto original_mesh = assets.publishMesh(0,colored);
    check(assets.publishMesh(0,colored) == original_mesh,"Unchanged generic mesh was republished");
    colored.vertex_colors[0].x = .5f;
    const auto changed_mesh = assets.publishMesh(0,colored);
    check(changed_mesh->id() != original_mesh->id() && original_mesh->geometry().vertex_colors[0].x == 1,
        "Mesh color revision mutated a held asset");
    d3scene::PreparedMesh background; background.asset = changed_mesh;
    background.surface.layer = d3scene::RenderLayer::Background;
    background.surface.unlit = true; background.surface.two_sided = true;
    background.camera_translation = {1,.5f,1};
    environment.meshes.push_back(background);
    auto foreground = background; foreground.surface.layer = d3scene::RenderLayer::World;
    foreground.camera_translation = {}; environment.meshes.insert(environment.meshes.begin(),foreground);
    const auto environment_frame = d3scene::Presentation::freeze(environment);
    const auto& background_policy = environment_frame->data().meshes[1].surface.material;
    check(!background_policy.testsDepth() && !background_policy.writesSolidDepth(),"Background contaminated world depth policy");
    check(environment_frame->data().meshes[0].surface.material.writesSolidDepth(),"World mesh lost solid depth policy");
    auto sky_view = view();
    auto sky_result = d3scene::queryView(*environment_frame,sky_view);
    check(sky_result.exterior_visible && sky_result.surfaces.size() == 2 && sky_result.surfaces[0].mesh_index == 1,
        "Background did not precede world geometry");
    sky_view.camera.position = {100,2,50};
    sky_result = d3scene::queryView(*environment_frame,sky_view);
    check(sky_result.surfaces.size() == 1 && sky_result.surfaces[0].world.position.x == 100 &&
        sky_result.surfaces[0].world.position.y == 1 && sky_result.surfaces[0].world.position.z == 50,
        "View-relative mesh anchor did not preserve authored parallax");
    check(environment_frame->data().meshes[1].world.position.x == 0,"Auxiliary camera mutated prepared mesh transform");
    appendRoom(assets,environment,triangle(0,3)); sky_view = view(); sky_view.room = 0;
    const auto enclosed = d3scene::queryView(*d3scene::Presentation::freeze(environment),sky_view);
    check(!enclosed.exterior_visible,"Enclosed room requested sky clear");
    bool background_leaked = false;
    for (const auto& surface : enclosed.surfaces) background_leaked |= surface.layer == d3scene::RenderLayer::Background;
    check(!background_leaked,"Background geometry leaked into enclosed room");
    auto decals = environment;
    decals.meshes.resize(1); decals.meshes[0].room = 0; decals.meshes[0].receiver_polygon = 0;
    auto decal_query = d3scene::queryView(*d3scene::Presentation::freeze(decals),sky_view);
    check(decal_query.surfaces.size() == 2,"Visible receiver lost its decal");
    decals.rooms[0].surfaces[0].draw_surface = false;
    check(d3scene::queryView(*d3scene::Presentation::freeze(decals),sky_view).surfaces.empty(),"Hidden receiver left a floating decal");
    decals.meshes[0].receiver_polygon = 1;
    bool missing_receiver = false;
    try { d3scene::Presentation::freeze(decals); } catch (const std::invalid_argument&) { missing_receiver = true; }
    check(missing_receiver,"Missing decal receiver was accepted");
    decals.meshes[0].receiver_polygon = -1; decals.meshes[0].size_fade_range = {20,10};
    bool bad_fade = false;
    try { d3scene::Presentation::freeze(decals); } catch (const std::invalid_argument&) { bad_fade = true; }
    check(bad_fade,"Reversed mesh size fade was accepted");
    auto enclosed_exterior = environment;
    enclosed_exterior.meshes[0].room = -2;
    d3scene::ObjectState exterior_object; exterior_object.handle = 91;
    exterior_object.room = -2; exterior_object.current.position = {0,0,3}; exterior_object.radius = 1;
    enclosed_exterior.objects.push_back(exterior_object);
    const auto enclosed_frame = d3scene::Presentation::freeze(enclosed_exterior);
    const auto excluded_exterior = d3scene::queryView(*enclosed_frame,sky_view);
    check(excluded_exterior.object_indices.empty(),"Enclosed camera admitted an exterior object");
    bool exterior_mesh_leaked = false;
    for (const auto& surface : excluded_exterior.surfaces) exterior_mesh_leaked |= surface.mesh;
    check(!exterior_mesh_leaked,"Enclosed camera admitted an exterior world mesh");
    auto reference_view = sky_view; reference_view.portal_visibility = false;
    check(d3scene::queryView(*enclosed_frame,reference_view).object_indices.size() == 1,
        "Spatial filtering destroyed reference query scene availability");
    colored.vertex_colors.pop_back(); bool rejected_colors = false;
    try { assets.publishMesh(1,colored); } catch (const std::invalid_argument&) { rejected_colors = true; }
    check(rejected_colors,"Incomplete authored mesh color stream was accepted");
    d3scene::PreparedMesh billboard;
    billboard.asset = assets.publishMesh(2,d3scene::unitBillboardQuad());
    {
        d3scene::FrameData transient; transient.scene_lifetime = assets.lifetime();
        transient.sequence = 1;
        auto mesh = billboard;
        mesh.world.position = {0,0,5}; mesh.surface.two_sided = true;
        transient.meshes.push_back(mesh);
        check(d3scene::queryView(*d3scene::Presentation::freeze(transient),view()).surfaces.size() == 1,
            "Authored transient mesh was not visible");
        transient.meshes[0].vertices = mesh.asset->geometry().vertices;
        for (auto& vertex : transient.meshes[0].vertices) vertex.x += 100;
        const auto moved = d3scene::Presentation::freeze(transient);
        check(d3scene::queryView(*moved,view()).surfaces.empty(),"Mesh culling ignored prepared positions");
        for (auto& vertex : transient.meshes[0].vertices) vertex.x -= 100;
        const auto restored = d3scene::Presentation::freeze(transient);
        check(d3scene::queryView(*restored,view()).surfaces.size() == 1,"Prepared mesh positions did not return to view");
        check(moved->data().meshes[0].vertices[0].x > 90,"Published procedural positions aliased mutable input");
        const auto& prepared = restored->data().meshes[0];
        const auto pairs = d3scene::preparedPositionPairs(prepared.asset->geometry(),prepared.vertices,prepared.vertices);
        check(pairs.size() == 6 && pairs[0].current.x == -1 && pairs[0].previous.x == -1,
            "Transient mesh GPU position stream lost topology or invented history");
        transient.meshes[0].vertices.pop_back(); bool incomplete = false;
        try { d3scene::Presentation::freeze(transient); } catch (const std::invalid_argument&) { incomplete = true; }
        check(incomplete,"Incomplete procedural positions were accepted");
        transient.meshes[0].vertices = prepared.vertices;
        transient.meshes[0].vertices[0].x = std::numeric_limits<float>::infinity(); bool nonfinite = false;
        try { d3scene::Presentation::freeze(transient); } catch (const std::invalid_argument&) { nonfinite = true; }
        check(nonfinite,"Nonfinite procedural positions were accepted");
    }
    billboard.alignment = d3scene::MeshAlignment::Camera;
    billboard.world.position = {10,2,3};
    billboard.world.right = {0,-2,0}; billboard.world.up = {3,0,0}; // rotated, non-square sprite
    auto side = view(); side.camera.right = {0,0,-1}; side.camera.forward = {1,0,0};
    auto fading = billboard; fading.size_fade_range = {5,15};
    const auto faded = d3scene::resolveMeshTransform(fading,d3scene::prepareCamera(side));
    check(near(faded.right.y,-1) && near(faded.up.z,-1.5),"Mesh distance fade did not use view depth");
    const auto unfaded = d3scene::resolveMeshTransform(fading,d3scene::prepareCamera(view()));
    check(near(unfaded.right.y,-2) && near(unfaded.up.x,3),"One view's distance fade leaked into another");
    fading.world.position.x = 20;
    const auto vanished = d3scene::resolveMeshTransform(fading,d3scene::prepareCamera(side));
    check(near(vanished.right.y,0) && near(vanished.up.z,0),"Mesh did not vanish at fade end");
    const auto oriented = d3scene::resolveMeshTransform(billboard,d3scene::prepareCamera(side));
    check(oriented.position.x == 10 && oriented.position.y == 2 && oriented.position.z == 3,"Billboarding moved world-space center");
    check(oriented.right.y == -2 && oriented.up.z == -3 && oriented.forward.x == 1,"Billboard lost authored spin or aspect ratio");
    check(billboard.world.up.x == 3 && billboard.world.forward.z == 1,"Billboarding rewrote prepared source transform");
    billboard.alignment = d3scene::MeshAlignment::World;
    check(d3scene::resolveMeshTransform(billboard,d3scene::prepareCamera(side)).up.x == 3,"Planar geometry inherited camera rotation");
    auto anchored = billboard;
    anchored.world = {}; anchored.world.position = {100,0,0};
    anchored.world.right = {2,0,0}; anchored.world.up = {0,3,0};
    anchored.alignment = d3scene::MeshAlignment::FacingEye; anchored.eye_distance = 50;
    const auto anchored_world = d3scene::resolveMeshTransform(anchored,d3scene::prepareCamera(view()));
    check(near(anchored_world.position.x,50) && near(anchored_world.right.z,-2) && near(anchored_world.up.y,3),
        "View-centered anchor lost radial placement or world-upright facing");
    auto translated = view(); translated.camera.position = {100,0,-100};
    const auto translated_anchor = d3scene::resolveMeshTransform(anchored,d3scene::prepareCamera(translated));
    check(near(translated_anchor.position.x,100) && near(translated_anchor.position.z,-50) && near(translated_anchor.right.x,2),
        "Second view reused another view's anchor transform");
    check(anchored.world.position.x == 100 && anchored.world.position.z == 0,"View placement mutated published sky");
    anchored.world.position = {0,100,0};
    const auto pole = d3scene::resolveMeshTransform(anchored,d3scene::prepareCamera(view()));
    check(near(pole.position.y,50) && near(pole.up.z,-3),"Vertical view anchor produced invalid orientation");
    anchored.alignment = d3scene::MeshAlignment::CameraUpright;
    for (float pitch : {-.9f,0.f,.7f}) for (float heading : {-1.f,0.f,2.f}) for (float bank : {-2.f,0.f,1.f}) {
        const float sp = std::sin(pitch), cp = std::cos(pitch), sh = std::sin(heading), ch = std::cos(heading);
        const float sb = std::sin(bank), cb = std::cos(bank);
        auto rolled = view();
        rolled.camera.right = {cb*ch+sp*sb*sh,sb*cp,sp*sb*ch-cb*sh};
        rolled.camera.up = {sp*cb*sh-sb*ch,cb*cp,sb*sh+sp*cb*ch};
        rolled.camera.forward = {sh*cp,-sp,ch*cp};
        const auto upright = d3scene::resolveMeshTransform(anchored,d3scene::prepareCamera(rolled));
        // Independent legacy composition: view-to-world times inverse bank.
        const auto& r = rolled.camera.right; const auto& u = rolled.camera.up;
        check(near(upright.right.x,2*(cb*r.x-sb*u.x)) && near(upright.right.y,2*(cb*r.y-sb*u.y)) &&
            near(upright.right.z,2*(cb*r.z-sb*u.z)),"Upright overlay differs from bank-cancelled view basis");
        check(near(upright.up.x,3*(sb*r.x+cb*u.x)) && near(upright.up.y,3*(sb*r.y+cb*u.y)) &&
            near(upright.up.z,3*(sb*r.z+cb*u.z)),"Upright overlay lost aspect ratio or pitch");
    }
    auto vertical_view = view(); vertical_view.camera.right = {0,0,1};
    vertical_view.camera.up = {1,0,0}; vertical_view.camera.forward = {0,1,0};
    const auto upright_pole = d3scene::resolveMeshTransform(anchored,d3scene::prepareCamera(vertical_view));
    check(near(upright_pole.right.z,2) && near(upright_pole.up.x,3),"Upright camera pole lost the view meridian");
    const auto ring = d3scene::billboardRing(.3f,102,0);
    const auto thrust = d3scene::thrusterSlices(1,3);
    check(thrust.size() == 9 && near(thrust.back().radius,1) && near(thrust.back().distance,0) &&
        near(thrust.front().radius,1.f/9) && near(thrust.front().distance,8.f/3),"Thruster profile changed authored density or order");
    check(d3scene::thrusterSlices(1,.2f).empty() && d3scene::thrusterSlices(.04f,3).empty(),
        "Degenerate thruster profile generated geometry");
    check(d3scene::thrusterSlices(100,100).size() == 100,"Thruster appearance cap changed");
    auto rod = billboard; rod.alignment = d3scene::MeshAlignment::Axis;
    rod.world = d3scene::axisBillboardPlacement({0,2,10},{0,-2,10},.5f);
    const auto rod_front = d3scene::resolveMeshTransform(rod,d3scene::prepareCamera(view()));
    check(near(rod_front.right.x,-.5f) && near(rod_front.up.y,2) && near(rod_front.position.z,10),
        "Axis billboard changed endpoints, radius or authored UV handedness");
    auto rod_side_view = view(); rod_side_view.camera.position = {10,0,10};
    const auto rod_side = d3scene::resolveMeshTransform(rod,d3scene::prepareCamera(rod_side_view));
    check(near(rod_side.right.z,-.5f) && near(rod_side.right.x,0) && near(rod_side.up.y,2),
        "Axis billboard reused another view's facing");
    check(near(rod.world.right.x,.5f) && near(rod.world.up.y,2),"Axis billboard mutated published placement");
    rod_side_view.camera.position = {0,4,10};
    const auto rod_end = d3scene::resolveMeshTransform(rod,d3scene::prepareCamera(rod_side_view));
    check(near(rod_end.right.x,0) && near(rod_end.right.y,0) && near(rod_end.right.z,0),"End-on rod produced nonfinite geometry");
    rod.world = d3scene::axisBillboardPlacement({0,0,10},{0,0,10},1);
    check(near(d3scene::resolveMeshTransform(rod,d3scene::prepareCamera(view())).right.x,0),"Zero-length rod did not collapse");
    check(ring.vertices.size() == 64 && ring.polygons.size() == 32 && ring.polygons.back().corners[1].vertex == 0,
        "Billboard ring has missing closure or incorrect topology");
    check(ring.polygons[0].corners[0].alpha == 0 && ring.polygons[0].corners[2].alpha == 102,
        "Billboard ring lost radial opacity");
    auto marker_geometry = triangle(100,10);
    marker_geometry.raster_offsets = {{-.5f,.5f},{.5f,.5f},{0,-.5f}};
    const auto marker_asset = assets.publishMesh(5,marker_geometry);
    marker_geometry.raster_offsets[0].x = -1;
    check(assets.publishMesh(5,marker_geometry)->id() != marker_asset->id(),"Raster offset edit reused stale mesh revision");
    marker_geometry.raster_offsets.pop_back(); bool rejected_offsets = false;
    try { assets.publishMesh(5,marker_geometry); } catch (const std::invalid_argument&) { rejected_offsets = true; }
    check(rejected_offsets,"Incomplete raster marker stream accepted");
    d3scene::FrameData marker_data; marker_data.sequence = 1; marker_data.scene_lifetime = assets.lifetime();
    d3scene::PreparedMesh marker; marker.asset = marker_asset; marker.surface.layer = d3scene::RenderLayer::Background;
    marker_data.meshes.push_back(marker);
    check(d3scene::queryView(*d3scene::Presentation::freeze(marker_data),view()).surfaces.size() == 1,
        "Unexpanded marker centers were CPU-clipped instead of submitting the GPU-clipped batch");
    marker_data.meshes[0].surface.layer = d3scene::RenderLayer::World;
    bool rejected_marker_layer = false;
    try { d3scene::Presentation::freeze(marker_data); } catch (const std::invalid_argument&) { rejected_marker_layer = true; }
    check(rejected_marker_layer,"Raster marker entered unsupported world depth/history path");
    marker_data.meshes[0].surface.layer = d3scene::RenderLayer::Background;
    marker_data.meshes[0].has_owner = true; marker_data.meshes[0].owner = 91;
    bool rejected_owner = false;
    try { d3scene::Presentation::freeze(marker_data); } catch (const std::invalid_argument&) { rejected_owner = true; }
    check(rejected_owner,"Mesh referencing an absent object lifetime was accepted");
    d3scene::ObjectState marker_owner; marker_owner.handle = 91;
    marker_data.objects.push_back(marker_owner);
    const auto owned_marker = d3scene::Presentation::freeze(marker_data);
    check(d3scene::queryView(*owned_marker,view()).surfaces.size() == 1,"Eligible owned effect disappeared");
    auto excluded_view = view(); excluded_view.exclude_object = true; excluded_view.excluded_object = 91;
    check(d3scene::queryView(*owned_marker,excluded_view).surfaces.empty(),"Camera did not exclude its owner's effect geometry");
    excluded_view.excluded_object = 92;
    check(d3scene::queryView(*owned_marker,excluded_view).surfaces.size() == 1,"Camera excluded another object's effect");
    marker_data.objects[0].visible = false;
    check(d3scene::queryView(*d3scene::Presentation::freeze(marker_data),view()).surfaces.empty(),"Ineligible owner left visible effect geometry");
    check(d3scene::queryView(*owned_marker,view()).surfaces.size() == 1,"Visibility edit mutated retained presentation");
    d3scene::FrameData scheduled; scheduled.sequence = 5; scheduled.scene_lifetime = assets.lifetime();
    auto solid = background; solid.surface.layer = d3scene::RenderLayer::World;
    auto soft = solid; soft.surface.soft_depth_range = 2;
    scheduled.meshes = {soft,solid,background};
    const auto scheduled_frame = d3scene::Presentation::freeze(scheduled);
    const auto scheduled_visibility = d3scene::queryView(*scheduled_frame,view());
    const auto passes = d3scene::buildViewPasses(*scheduled_frame,scheduled_visibility);
    check(passes.size() == 4 && passes[0].kind == d3scene::ViewPassKind::Background &&
        passes[1].kind == d3scene::ViewPassKind::OpaqueWorld && passes[2].kind == d3scene::ViewPassKind::SnapshotOpaqueDepth &&
        passes[3].kind == d3scene::ViewPassKind::LateWorld,"Opaque depth dependency was scheduled after its consumer");
    check(passes[0].begin == 0 && passes[0].end == 1 && passes[1].begin == 1 && passes[1].end == 2 &&
        passes[2].begin == passes[2].end && passes[3].begin == 2 && passes[3].end == 3,"View pass ranges overlap or omit surfaces");
    scheduled.meshes[0].surface.soft_depth_range = 0; scheduled.meshes[0].surface.blend = d3scene::BlendMode::Alpha;
    const auto no_depth_frame = d3scene::Presentation::freeze(scheduled);
    check(d3scene::buildViewPasses(*no_depth_frame,d3scene::queryView(*no_depth_frame,view())).size() == 3,
        "Unconsumed opaque depth was copied");
    const auto exported = d3scene::buildViewPasses(*scheduled_frame,scheduled_visibility,{true});
    check(exported.size() == 5 && exported[3].kind == d3scene::ViewPassKind::ExportOpaqueScene &&
        exported[4].kind == d3scene::ViewPassKind::LateWorld,"Opaque image product includes translucent consumers");
    const auto empty_frame = d3scene::Presentation::freeze(shaken);
    {
        auto read_only = solid; read_only.surface.depth = d3scene::DepthMode::ReadOnly;
        auto no_depth_overlay = solid; no_depth_overlay.surface.depth = d3scene::DepthMode::Disabled;
        auto depth_scene = scheduled; depth_scene.meshes = {no_depth_overlay,solid,read_only};
        const auto depth_frame = d3scene::Presentation::freeze(depth_scene);
        const auto depth_view = d3scene::queryView(*depth_frame,view());
        const auto depth_passes = d3scene::buildViewPasses(*depth_frame,depth_view,{true});
        check(depth_passes.size() == 3 && depth_passes[0].end == 1 &&
            depth_passes[1].kind == d3scene::ViewPassKind::ExportOpaqueScene && depth_passes[2].begin == 1,
            "No-depth or read-only geometry was included in opaque products");
        check(!depth_view.surfaces[1].blended && depth_view.surfaces[1].late &&
            !depth_view.surfaces[2].blended && depth_view.surfaces[2].late,"Late execution still depends on alpha blending");
        depth_scene.meshes[0].surface.depth = static_cast<d3scene::DepthMode>(255); bool invalid_depth = false;
        try { d3scene::Presentation::freeze(depth_scene); } catch (const std::invalid_argument&) { invalid_depth = true; }
        check(invalid_depth,"Unknown depth policy accepted");
        depth_scene.meshes[0] = background; depth_scene.meshes[0].surface.depth = d3scene::DepthMode::ReadWrite;
        invalid_depth = false;
        try { d3scene::Presentation::freeze(depth_scene); } catch (const std::invalid_argument&) { invalid_depth = true; }
        check(invalid_depth,"Background depth writes accepted");
    }
    const auto empty_passes = d3scene::buildViewPasses(*empty_frame,d3scene::queryView(*empty_frame,view()),{true});
    check(empty_passes.size() == 1 && empty_passes[0].kind == d3scene::ViewPassKind::ExportOpaqueScene,
        "An empty view reused a previous view's opaque image product");
    const auto matrix_camera = d3scene::prepareCamera(side);
    const auto matrices = d3scene::viewMatrices(matrix_camera);
    const d3scene::Vec3 point{12,3,-2};
    const auto clip = d3scene::project(matrix_camera,point);
    const std::array<float,4> input{{point.x,point.y,point.z,1}};
    const double expected[4] = {clip.x,clip.y,clip.z,clip.w};
    for (int row = 0; row < 4; ++row) {
        float actual = 0;
        for (int column = 0; column < 4; ++column) actual += matrices.view_projection[column*4+row]*input[column];
        check(std::abs(actual-expected[row]) < .0001,"Post-consumer projection differs from scene visibility/depth");
    }
    check(matrices.inverse_view[0] == side.camera.right.x && matrices.inverse_view[8] == side.camera.forward.x &&
        matrices.inverse_view[12] == side.camera.position.x,"Post-consumer inverse view uses another camera");
    const auto radiance_passes = d3scene::buildViewPasses(*scheduled_frame,scheduled_visibility,{true,true});
    bool rejected_missing_depth = false;
    try { d3scene::buildViewPasses(*scheduled_frame,scheduled_visibility,{false,true}); }
    catch (const std::invalid_argument&) { rejected_missing_depth = true; }
    check(rejected_missing_depth,"Occlusion pass accepted a missing opaque scene dependency");
    check(radiance_passes.back().kind == d3scene::ViewPassKind::CompositeWorldOcclusion &&
        radiance_passes[radiance_passes.size()-2].kind == d3scene::ViewPassKind::LateWorld,
        "Eligible radiance was exported before translucency attenuated it");
    const d3scene::RadianceSample opaque{{.6f,.7f,.8f},{.3f,.2f,.1f}}; // the remainder is fog/emissive
    for (const auto blend : {d3scene::BlendMode::Alpha,d3scene::BlendMode::Additive,d3scene::BlendMode::Multiply}) {
        const d3scene::RadianceSample source{{.2f,.5f,.9f},{}};
        const auto composited = d3scene::composeRadiance(opaque,source,blend,.6f);
        const auto late = d3scene::applyOcclusion(composited,.35f);
        const d3scene::RadianceSample early_ao{d3scene::applyOcclusion(opaque,.35f),{}};
        const auto early = d3scene::composeRadiance(early_ao,source,blend,.6f);
        check(near(late.x,early.color.x) && near(late.y,early.color.y) && near(late.z,early.color.z),
            "Deferred AO changed fog/particle radiance or violated a material blending law");
    }
    const d3scene::RadianceSample fog_only{{.2f,.7f,.3f},{}};
    const d3scene::RadianceSample bright{{2,3,4},{1,2,3}};
    const auto bright_overlay = d3scene::composeRadiance(bright,{{.5f,.5f,.5f},{}},d3scene::BlendMode::Additive,1);
    const auto bright_occluded = d3scene::applyOcclusion(bright_overlay,.5f);
    check(near(bright_occluded.x,2) && near(bright_occluded.y,2.5f) && near(bright_occluded.z,3),
        "Overbright compositing clipped radiance before eligible-light subtraction");
    check(near(d3scene::applyOcclusion(fog_only,0).y,.7f),"AO darkened pure fog radiance");
    auto asymmetric = matrix_camera;
    asymmetric.transform.position = {2,3,4};
    asymmetric.projection.x_offset = .13f; asymmetric.projection.y_offset = -.27f;
    asymmetric.projection.near_distance = 3.5f;
    for (const float depth : {3.5f,10.f,100.f,1000.f}) {
        const auto world = d3scene::transformPoint(asymmetric.transform,{2,-1,depth});
        const auto projected = d3scene::project(asymmetric,world);
        const float sample = float((projected.z/projected.w+1)*.5);
        const auto reconstructed = d3scene::unproject(asymmetric,float(projected.x/projected.w),float(projected.y/projected.w),sample);
        check(std::abs(reconstructed.x-world.x) < .02f && std::abs(reconstructed.y-world.y) < .02f &&
            std::abs(reconstructed.z-world.z) < .02f,"Depth reconstruction lost near plane, asymmetric projection or resolved camera");
    }
    bool rejected_background = false;
    try { d3scene::unproject(asymmetric,0,0,1); } catch (const std::invalid_argument&) { rejected_background = true; }
    check(rejected_background,"Infinite background depth became a fabricated world position");
    d3scene::ViewHistory histories;
    d3scene::FrameData tick; tick.scene_lifetime = 7; tick.sequence = 1;
    auto main_view = view(); main_view.camera_owner = 42;
    auto first = histories.prepare(tick,main_view,d3scene::prepareCamera(main_view));
    check(!first.previous_valid,"First view fabricated temporal history");
    tick.sequence = 2;
    check(!histories.prepare(tick,main_view,d3scene::prepareCamera(main_view)).previous_valid,
        "Preparing an unexecuted view advanced history");
    histories.commit(first);
    main_view.camera.position.x = 3;
    const auto second = histories.prepare(tick,main_view,d3scene::prepareCamera(main_view));
    check(second.previous_valid && second.previous.view_projection == first.current.view_projection,
        "Moving view did not retain its own predecessor");
    histories.commit(second);
    tick.delta_time = .02f; tick.afterburner_factor = .5f; tick.player_control_delta = {1,0,0};
    const auto exposure = d3scene::prepareImageMotion(tick,second,{});
    check(exposure.valid && !exposure.paused && exposure.camera.previous_valid && exposure.delta_time == .02f,
        "Motion exposure lost its explicit main-view inputs");
    auto pause_tick = tick; pause_tick.sequence++; pause_tick.paused = true;
    pause_tick.delta_time = 0; pause_tick.afterburner_factor = 0; pause_tick.player_control_delta = {};
    const auto pause_camera = histories.prepare(pause_tick,main_view,d3scene::prepareCamera(main_view));
    const auto paused_exposure = d3scene::prepareImageMotion(pause_tick,pause_camera,exposure);
    check(paused_exposure.paused && paused_exposure.delta_time == .02f && paused_exposure.afterburner_factor == .5f &&
        paused_exposure.player_control_delta.x == 1 &&
        paused_exposure.camera.previous.view_projection == first.current.view_projection,
        "Pause lost the completed exposure or mixed it with new timing inputs");
    for (int change = 0; change < 8; ++change) {
        auto changed = pause_camera;
        if (change == 0) changed.view.camera_owner++;
        if (change == 1) changed.view.camera_variant++;
        if (change == 2) changed.view.camera_cut = true;
        if (change == 3) changed.view.viewport.width++;
        if (change == 4) changed.current.projection[0] += 1;
        if (change == 5) changed.current.view_projection[12] += 1;
        if (change == 6) changed.view.history_key++;
        if (change == 7) changed.view.projection_anchor.width++;
        check(d3scene::prepareImageMotion(pause_tick,changed,exposure).delta_time == 0,
            "Changed paused view borrowed an incompatible exposure");
    }
    bool rejected_motion_camera = false;
    auto wrong_camera = second; wrong_camera.view.kind = d3scene::ViewKind::Auxiliary;
    try { d3scene::prepareImageMotion(tick,wrong_camera,exposure); }
    catch (const std::logic_error&) { rejected_motion_camera = true; }
    check(rejected_motion_camera,"Auxiliary camera became the world motion product");
    rejected_motion_camera = false;
    try { d3scene::prepareImageMotion(pause_tick,second,exposure); }
    catch (const std::logic_error&) { rejected_motion_camera = true; }
    check(rejected_motion_camera,"Motion exposure combined different presentation sequences");
    auto interrupted_tick = pause_tick; interrupted_tick.sequence++;
    auto interrupted_camera = pause_camera; interrupted_camera.sequence++;
    check(d3scene::prepareImageMotion(interrupted_tick,interrupted_camera,exposure).delta_time == 0,
        "Pause inherited an exposure from a skipped presentation");
    interrupted_tick.scene_lifetime++; interrupted_camera.lifetime++;
    check(d3scene::prepareImageMotion(interrupted_tick,interrupted_camera,exposure).delta_time == 0,
        "Reload inherited a prior level's exposure");
    const auto replay = histories.prepare(tick,main_view,d3scene::prepareCamera(main_view));
    check(replay.previous_valid && replay.previous.view_projection == first.current.view_projection,
        "Pass replay replaced previous camera with the current one");
    auto auxiliary = main_view; auxiliary.kind = d3scene::ViewKind::Auxiliary;
    {
        d3scene::ViewHistory collection_history;
        auto preview = main_view; preview.kind = d3scene::ViewKind::Preview; preview.collection = 9;
        auto prior_tick = tick;
        const auto prior_collection = collection_history.prepare(prior_tick,preview,d3scene::prepareCamera(preview));
        collection_history.commit(prior_collection);
        ++prior_tick.sequence;
        check(collection_history.prepare(prior_tick,preview,d3scene::prepareCamera(preview)).previous_valid,
            "Stable collection lost its completed view history");
        ++preview.collection;
        check(!collection_history.prepare(prior_tick,preview,d3scene::prepareCamera(preview)).previous_valid,
            "View history crossed content collections");
        --preview.collection; preview.target = d3scene::ViewTarget::Display;
        check(!collection_history.prepare(prior_tick,preview,d3scene::prepareCamera(preview)).previous_valid,
            "View history crossed raster targets");
    }
    check(!histories.prepare(tick,auxiliary,d3scene::prepareCamera(auxiliary)).previous_valid,
        "Auxiliary view borrowed main-view history through a numeric key collision");
    histories.commit(histories.prepare(tick,auxiliary,d3scene::prepareCamera(auxiliary)));
    bool conflicting_replay = false;
    auto moved_replay = main_view; moved_replay.camera.position.y += 1;
    try { histories.prepare(tick,moved_replay,d3scene::prepareCamera(moved_replay)); }
    catch (const std::logic_error&) { conflicting_replay = true; }
    check(conflicting_replay,"One history key accepted two different cameras in one presentation");
    tick.sequence = 3;
    for (int change = 0; change < 6; ++change) {
        auto changed = main_view;
        if (change == 0) changed.camera_owner++;
        if (change == 1) changed.camera_variant++;
        if (change == 2) changed.camera_cut = true;
        if (change == 3) changed.viewport.width++;
        if (change == 4) changed.projection_anchor.x++;
        if (change == 5) changed.zoom *= 2;
        check(!histories.prepare(tick,changed,d3scene::prepareCamera(changed)).previous_valid,
            "Discontinuous camera identity or projection reused temporal history");
    }
    check(histories.prepare(tick,main_view,d3scene::prepareCamera(main_view)).previous_valid,
        "Uncommitted alternative views damaged main-view history");
    tick.sequence = 4;
    check(!histories.prepare(tick,main_view,d3scene::prepareCamera(main_view)).previous_valid,
        "Skipped presentation reused stale motion as a one-frame predecessor");
    tick.sequence = 3; tick.scene_lifetime++;
    check(!histories.prepare(tick,main_view,d3scene::prepareCamera(main_view)).previous_valid,
        "Level transition retained the previous scene's history");
    bool older_commit = false;
    try { histories.commit(first); } catch (const std::logic_error&) { older_commit = true; }
    check(older_commit,"An older presentation overwrote newer view history");
    histories.clear(); tick.scene_lifetime--;
    check(!histories.prepare(tick,main_view,d3scene::prepareCamera(main_view)).previous_valid,
        "Renderer reset retained temporal camera state");
    check(d3scene::nearestDepthSample({.8f,.2f,.2f,.8f}) == 1,"Depth tie did not select a deterministic surface record");
    check(d3scene::nearestDepthSample({1,1,1,1}) == 0,"Background resolve has no stable sample");
    check(d3scene::nearestDepthSample({.7f,.6f,.5f,.4f,.3f,.2f,.1f,0}) == 7,"Resolve silently capped the sample count");
    return Checks;
}
