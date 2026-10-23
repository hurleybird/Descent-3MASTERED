// Descent 3MASTERED - GPL-3.0-or-later
#include "../../renderer/SceneRasterTrail.h"
#include "../../renderer/SceneMeshExposure.h"
#include "../../renderer/SceneHistory.h"
#include <limits>

int runRasterTrailTests() {
    using namespace d3scene;
    int checks = 0;
    const auto check = [&](bool v) { ++checks; if (!v) throw std::runtime_error("Retained raster trail contract"); };
    const auto near = [](double a,double b) { return std::abs(a-b) < 1.e-5; };
    RasterTrail style{10,.75f,.6f,4}; style.reference_length = 100;
    Projection optics; const Vec2 pixel{2.f/800,2.f/600};
    check(!rasterTrailVertex({0,0,100},{9,0,100},style,{-.5f,.5f},optics,pixel).extended);
    const auto head = rasterTrailVertex({0,0,100},{16,0,100},style,{-.5f,.5f},optics,pixel);
    const auto tail = rasterTrailVertex({0,0,100},{16,0,100},style,{.5f,.5f},optics,pixel);
    check(head.extended && tail.extended && near(head.alpha,.744) && tail.alpha == 0);
    check(near(head.clip.x/head.clip.w,0) && near(tail.clip.x/tail.clip.w,.12));
    check(rasterTrailVertex({0,0,1000},{16,0,1000},style,{.5f,.5f},optics,pixel).extended);
    const auto lower = rasterTrailVertex({0,0,100},{16,0,100},style,{-.5f,-.5f},optics,pixel);
    check(near((head.clip.y/head.clip.w-lower.clip.y/lower.clip.w)/pixel.y,1));
    const auto capped = rasterTrailVertex({0,0,100},{1000,0,100},style,{.5f,0},optics,pixel);
    check(near(capped.clip.x/capped.clip.w,.75));
    const auto crossing = rasterTrailVertex({20,0,.5f},{0,0,20},style,{-.5f,.5f},optics,pixel);
    check(crossing.extended && crossing.clip.w >= optics.near_distance && std::isfinite(crossing.clip.x));
    check(!rasterTrailVertex({10,0,-10},{20,0,-5},style,{.5f,.5f},optics,pixel).extended);
    MeshExposureHistory exposures; Transform rotation; const AssetId asset{7,1,4};
    check(!exposures.sample(asset,1,0,rotation,true).valid);
    auto next = rotation; next.right = {0,0,1}; next.forward = {-1,0,0};
    const auto second = exposures.sample(asset,3,1,next,true);
    check(second.valid && second.previous.right.x == 1);
    check(exposures.sample(asset,3,1,next,true).previous.right.x == 1);
    exposures.sample(asset,4,0,rotation,false); // independent export cannot age the world exposure
    check(exposures.sample(asset,6,3,rotation,true).previous.right.z == 1);
    check(!exposures.sample({7,2,4},7,6,rotation,true).valid);
    check(!exposures.sample({8,1,4},8,7,rotation,true).valid);
    bool rejected = false;
    try { exposures.sample({8,1,4},8,7,next,true); } catch (const std::logic_error&) { rejected = true; } check(rejected);
    rejected = false;
    try { exposures.sample({8,1,4},7,6,next,true); } catch (const std::logic_error&) { rejected = true; } check(rejected);
    PreparedMesh mesh; mesh.world.position = {0,0,0}; mesh.previous_world = rotation; mesh.camera_translation = {1,1,1};
    mesh.raster_trail = style; mesh.raster_trail.camera_kinds = 1u<<uint32_t(ViewKind::Main); mesh.has_previous_world = true;
    View audience;
    check(rasterTrailActive(mesh,audience,true,false));
    check(!rasterTrailActive(mesh,audience,true,true) && !rasterTrailActive(mesh,audience,false,false));
    audience.kind = ViewKind::Auxiliary; check(!rasterTrailActive(mesh,audience,true,false));
    audience.kind = ViewKind::Mirror; check(!rasterTrailActive(mesh,audience,true,false));
    mesh.raster_trail.camera_kinds = UINT32_MAX; check(rasterTrailActive(mesh,audience,true,false));
    Camera prior; prior.transform.position = {20,30,40};
    const auto placement = resolveMeshTransform(mesh,prior,nullptr,&mesh.previous_world);
    check(placement.position.x == 20 && placement.position.y == 30 && placement.position.z == 40);
    FrameData frame; frame.scene_lifetime = 7; frame.sequence = 1;
    View view; view.history_key = 1; view.viewport = view.projection_anchor = {0,0,800,600}; view.camera.position = {20,30,40};
    ViewHistory history; history.commit(history.prepare(frame,view,prepareCamera(view)));
    frame.sequence = 3; frame.temporal_predecessor = 1; view.camera.position = {40,50,60};
    auto sample = history.prepare(frame,view,prepareCamera(view));
    check(sample.previous_valid && sample.previous_camera.position.x == 20); history.commit(sample);
    check(history.prepare(frame,view,prepareCamera(view)).previous_camera.position.x == 20);
    frame.sequence = 4; frame.temporal_predecessor = 3; view.camera_cut = true;
    sample = history.prepare(frame,view,prepareCamera(view)); check(!sample.previous_valid && sample.previous_camera.position.x == 40);
    SceneAssets assets;
    MeshGeometry geometry; geometry.vertices.assign(4,{0,0,100});
    geometry.raster_offsets = {{-.5f,.5f},{.5f,.5f},{.5f,-.5f},{-.5f,-.5f}};
    MeshPolygon polygon; polygon.normal = {0,0,-1}; polygon.corners = {{0},{1},{2},{3}};
    geometry.polygons.push_back(polygon);
    mesh.asset = assets.publishMesh(0,std::move(geometry));
    mesh.surface.layer = RenderLayer::Background; mesh.surface.unlit = mesh.surface.two_sided = true;
    mesh.surface.medium_transport = false; mesh.surface.blend = BlendMode::Alpha;
    FrameData source; source.sequence = 1; source.scene_lifetime = assets.lifetime(); source.meshes.push_back(mesh);
    check(Presentation::freeze(source)->data().meshes[0].has_previous_world);
    source.meshes[0].raster_trail.length = std::numeric_limits<float>::quiet_NaN(); rejected = false;
    try { Presentation::freeze(source); } catch (const std::invalid_argument&) { rejected = true; } check(rejected);
    source.meshes[0].raster_trail = style; source.meshes[0].surface.medium_transport = true; rejected = false;
    try { Presentation::freeze(source); } catch (const std::invalid_argument&) { rejected = true; } check(rejected);
    source.meshes[0].surface.medium_transport = false; source.meshes[0].previous_world.position.x = std::numeric_limits<float>::infinity(); rejected = false;
    try { Presentation::freeze(source); } catch (const std::invalid_argument&) { rejected = true; } check(rejected);
    return checks;
}
