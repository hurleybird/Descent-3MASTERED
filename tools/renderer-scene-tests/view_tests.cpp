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
#include <thread>
#include <algorithm>

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
        using namespace d3scene;
        auto camera = view(); camera.attached_camera = true; camera.camera_owner = 42;
        MeshViewStyle style; style.camera_owner = 42; style.match = CameraMatch::Only; style.forward_only = true;
        check(meshViewOpacity(style,camera) == 1,"Attached forward mesh selection failed");
        camera.camera_variant = 1;
        check(meshViewOpacity(style,camera) == 0,"Rear camera inherited forward-only content");
        style.match = CameraMatch::Except;
        check(meshViewOpacity(style,camera) == 1,"Rear camera lost world variant");
        camera.camera_variant = 0; camera.kind = ViewKind::Mirror;
        check(meshViewOpacity(style,camera) == 1,"Mirror inherited camera-local variant");
        style.match = CameraMatch::Any; style.directional_opacity = {1,0,0};
        check(meshViewOpacity(style,camera) == 0,"World directional opacity ignored the camera");
        camera.kind = ViewKind::Main;
        check(meshViewOpacity(style,camera) == 1,"Source camera was attenuated by world-facing fade");
        camera.camera_owner = 43; camera.camera.forward = {.6f,0,.8f};
        check(near(meshViewOpacity(style,camera),.6),"View-facing opacity wrong");
        check(style.directional_opacity.x == 1,"View compilation mutated captured mesh style");
        SceneAssets assets; FrameData data; data.sequence = 1; data.scene_lifetime = assets.lifetime();
        ObjectState source; source.handle = 42; data.objects.push_back(source);
        PreparedMesh mesh; mesh.asset = assets.createMesh(triangle(0,4));
        mesh.surface.unlit = mesh.surface.two_sided = true;
        mesh.surface.blend = BlendMode::Additive; mesh.surface.depth = DepthMode::ReadOnly;
        mesh.view_style.camera_owner = 42; mesh.view_style.match = CameraMatch::Only; mesh.view_style.forward_only = true;
        data.meshes.push_back(mesh);
        const auto frozen = Presentation::freeze(data);
        camera = view(); camera.attached_camera = true; camera.camera_owner = 42; camera.portal_visibility = false;
        auto selected = queryView(*frozen,camera);
        check(selected.surfaces.size() == 1 && selected.surfaces[0].view_alpha == 255,"Captured camera selection was not compiled");
        const auto resolved_camera = resolveView(*frozen,camera);
        check(resolved_camera.attached_camera && resolved_camera.camera_effects_resolved &&
            queryView(*frozen,resolved_camera).surfaces.size() == 1,"Resolution erased camera attachment identity");
        camera.camera_variant = 1;
        check(queryView(*frozen,camera).surfaces.empty(),"Captured forward geometry leaked into rear view");
        camera.camera_variant = 0; camera.kind = ViewKind::Mirror;
        check(queryView(*frozen,camera).surfaces.empty(),"Captured camera-local geometry leaked into mirror");
        data.sequence = 2; data.meshes[0].view_style.match = CameraMatch::Any;
        data.meshes[0].view_style.directional_opacity = {.6f,0,.8f};
        const auto world_frame = Presentation::freeze(data);
        selected = queryView(*world_frame,camera);
        check(selected.surfaces.size() == 1 && selected.surfaces[0].view_alpha == 204 &&
            visibleSurfacePolicy(*world_frame,selected.surfaces[0]).blend == BlendMode::Additive,
            "Captured view opacity or material policy was not compiled");
        check(world_frame->data().meshes[0].surface.alpha == 1,"View opacity mutated frozen material");
        const auto rejects_style = [&] {
            bool rejected = false;
            try { Presentation::freeze(data); } catch (const std::invalid_argument&) { rejected = true; }
            check(rejected,"Invalid view-dependent mesh style accepted");
        };
        data.meshes[0].view_style.camera_owner = 43; rejects_style();
        data.meshes[0].view_style.camera_owner = 42; data.meshes[0].view_style.directional_opacity = {2,0,0}; rejects_style();
    }
    {
        using namespace d3scene;
        auto main_source = view();
        main_source.viewport = {-20,-15,840,630}; main_source.projection_anchor = {20,15,800,600};
        main_source.attached_camera = true; main_source.camera_owner = 42;
        main_source.camera.position = {2,3,4}; main_source.exclude_camera_rig = true;
        main_source.feedback = Automap | ObjectVisibility;
        const auto overlay = cameraOverlayView(main_source,99,{0,0,800,600});
        check(overlay.kind == ViewKind::Preview && overlay.target == ViewTarget::Display && overlay.collection == 99 &&
            overlay.feedback == NoFeedback && !overlay.portal_visibility && !overlay.dynamic_lighting && overlay.clear_depth,
            "Camera foreground inherited world admission or feedback");
        check(overlay.attached_camera && overlay.camera_owner == 42 && !overlay.exclude_camera_rig &&
            overlay.projection_anchor.x == 0 && overlay.projection_anchor.y == 0,"Camera foreground lost captured source/crop");
        const Vec3 point{5,4,20};
        const auto a = project(prepareCamera(main_source),point), b = project(prepareCamera(overlay),point);
        check(near(main_source.viewport.x+(a.x/a.w+1)*main_source.viewport.width/2,
                   overlay.viewport.x+(b.x/b.w+1)*overlay.viewport.width/2) &&
              near(main_source.viewport.y+(1-a.y/a.w)*main_source.viewport.height/2,
                   overlay.viewport.y+(1-b.y/b.w)*overlay.viewport.height/2),"Camera foreground shifted optical sample positions");
        SceneAssets overlay_assets; FrameData overlay_data;
        overlay_data.sequence = 1; overlay_data.scene_lifetime = overlay_assets.lifetime();
        ObjectState camera_owner; camera_owner.handle = 42; overlay_data.objects.push_back(camera_owner);
        Transform rotation; rotation.right = {0,0,-1}; rotation.forward = {1,0,0};
        overlay_data.camera_perturbations.push_back({42,rotation});
        const auto overlay_frame = Presentation::freeze(overlay_data);
        const auto resolved_source = resolveView(*overlay_frame,main_source), resolved_overlay = resolveView(*overlay_frame,overlay);
        check(near(resolved_source.camera.forward.x,resolved_overlay.camera.forward.x) &&
            near(resolveView(*overlay_frame,resolved_overlay).camera.forward.x,resolved_overlay.camera.forward.x),
            "Camera foreground missed or double-applied source perturbation");
        const auto rejects_overlay = [&](View source,uint64_t identity) {
            bool rejected = false;
            try { cameraOverlayView(source,identity,{0,0,800,600}); } catch (const std::invalid_argument&) { rejected = true; }
            check(rejected,"Unsupported camera foreground source accepted");
        };
        rejects_overlay(main_source,main_source.history_key);
        main_source.kind = ViewKind::Mirror; rejects_overlay(main_source,100);
        main_source.kind = ViewKind::Auxiliary; rejects_overlay(main_source,100);
        ForegroundLayer retained_layer;
        retained_layer.display_views.push_back(overlay);
        auto copied_layer = retained_layer;
        copied_layer.display_views[0].camera.position.x = 99;
        check(retained_layer.display_views[0].camera.position.x == 2,"Foreground storage shared live view values");
        size_t retained_capacity = 0;
        {
            ForegroundLayer scratch_layer; scratch_layer.display_views.reserve(31);
            retained_capacity = scratch_layer.display_views.capacity();
        }
        ForegroundLayer reused_layer;
        check(reused_layer.display_views.empty() && reused_layer.display_views.capacity() == retained_capacity,
            "Released foreground view-list storage was not recycled");
        auto foreign_layer = retained_layer;
        std::thread foreign_release([layer = std::move(foreign_layer)] {});
        foreign_release.join();
        check(retained_layer.display_views[0].camera.position.x == 2,"Foreign foreground release corrupted held source");
    }
    {
        using namespace d3scene;
        SceneAssets assets; FrameData data; data.sequence = 1; data.scene_lifetime = assets.lifetime();
        appendRoom(assets,data,triangle(-2,3));
        appendRoom(assets,data,triangle(2,3));
        const auto frame = Presentation::freeze(data);
        auto request = view(); request.kind = ViewKind::Auxiliary;
        check(queryView(*frame,request).surfaces.size() == 2,"Coverage fixture lost a full-view surface");
        const auto full_projection = viewMatrices(prepareCamera(request)).projection;
        request.coverage = {0,0,400,600};
        for (bool portals : {false,true}) {
            request.portal_visibility = portals;
            const auto selected = queryView(*frame,request);
            check(selected.surfaces.size() == 1 && selected.surfaces[0].room_index == 0,
                "View aperture did not reject the unconsumed half of the image");
            check(viewMatrices(selected.camera).projection == full_projection,
                "View aperture changed pixel projection");
        }
        const auto scaled = rasterViewport(request.coverage,1200,2);
        check(scaled.x == 0 && scaled.y == 0 && scaled.width == 800 && scaled.height == 1200,
            "View aperture lost raster scale");
        request.kind = ViewKind::Main;
        bool rejected = false;
        try { validateView(request); } catch (const std::invalid_argument&) { rejected = true; }
        check(rejected,"Image coverage silently narrowed the main gameplay view");
    }
    {
        using namespace d3scene;
        static_assert(sizeof(VisibleSurface) <= 48,"Per-polygon visibility record grew placement data");
        ViewVisibility first;
        first.placements.push_back({{7,8,9}});
        VisibleSurface a; a.placement = 0; a.polygon_index = 2;
        VisibleSurface b = a; b.polygon_index = 3;
        first.surfaces = {a,b,{}};
        check(&first.world(first.surfaces[0]) == &first.world(first.surfaces[1]),"Part polygons did not share placement");
        check(first.world(first.surfaces[2]).position.x == 0,"World-space surface has nonidentity placement");
        auto copy = first;
        copy.placements[0].position.x = 19;
        check(first.world(first.surfaces[0]).position.x == 7 && copy.world(copy.surfaces[1]).position.x == 19,
            "Copied visibility aliases mutable placements");
        auto moved = std::move(copy);
        check(moved.world(moved.surfaces[0]).position.x == 19,"Moving view lost placement indices");
        VisibleSurface invalid; invalid.placement = 2;
        bool rejected = false;
        try { first.world(invalid); } catch (const std::out_of_range&) { rejected = true; }
        check(rejected,"Missing view placement was silently accepted");
    }
    {
        using namespace d3scene;
        const VisibleSurface* storage = nullptr;
        {
            ViewVisibility first;
            first.surfaces.resize(8192); first.surfaces[0].polygon_index = 31;
            storage = first.surfaces.data();
            auto copy = first;
            check(copy.surfaces.data() != storage && copy.surfaces[0].polygon_index == 31,"Copied visibility aliases mutable storage");
            auto moved = std::move(first);
            check(moved.surfaces.data() == storage,"Moved visibility copied its records");
        }
        // A held result is never recycled even while later queries come/go.
        ViewVisibility held; held.surfaces.resize(32); held.surfaces[0].polygon_index = 71;
        const auto held_storage = held.surfaces.data();
        { ViewVisibility next; next.surfaces.resize(8192);
          check(next.surfaces.data() != held_storage,"Live view storage was recycled"); }
        check(held.surfaces[0].polygon_index == 71,"Subsequent query changed a held view");
        {
            ViewVisibility warm; warm.surfaces.reserve(16384); storage = warm.surfaces.data();
            warm.geometry.rooms.push_back(42); warm.geometry.meshes.push_back(19);
        }
        { ViewVisibility reused;
          check(reused.surfaces.empty() && reused.surfaces.data() == storage,"Released visibility capacity was not reused"); }
        { ViewVisibility reused;
          check(reused.geometry.rooms.empty() && reused.geometry.meshes.empty(),"Recycled view retained geometry demand"); }
        std::thread release([owned = std::move(held)]() mutable { owned.surfaces.clear(); });
        release.join();
    }
    {
        using namespace d3scene;
        SceneAssets assets; FrameData data; data.sequence = 1; data.scene_lifetime = assets.lifetime();
        for (int i = 0; i < 17; ++i) {
            appendRoom(assets,data,triangle(0,4));
            data.rooms.back().surfaces[0].composition_order = (i*7)%9-4;
        }
        const auto frame = Presentation::freeze(data);
        uint32_t random = 314159;
        for (const auto ordering : {ViewOrdering::Depth,ViewOrdering::Compositing}) {
            ViewVisibility result; result.ordering = ordering;
            for (uint32_t i = 0; i < 4096; ++i) {
                random = random*1664525u+1013904223u;
                VisibleSurface item; item.room_index = i%17; item.model_instance_index = i;
                item.depth = float(int(random%31)-15);
                item.layer = (random&3) == 0 ? RenderLayer::Background : RenderLayer::World;
                item.late = (random&4) != 0;
                result.surfaces.push_back(item);
            }
            auto expected = result.surfaces;
            std::stable_sort(expected.begin(),expected.end(),[&](const VisibleSurface& a,const VisibleSurface& b) {
                if (ordering == ViewOrdering::Compositing)
                    return visibleSurfaceBinding(*frame,a).composition_order < visibleSurfaceBinding(*frame,b).composition_order;
                if (a.layer != b.layer) return a.layer == RenderLayer::Background;
                if (a.layer == RenderLayer::Background) return false;
                if (a.late != b.late) return !a.late;
                return a.late ? a.depth > b.depth : a.depth < b.depth;
            });
            orderViewSurfaces(*frame,result);
            bool same = result.surfaces.size() == expected.size();
            for (size_t i = 0; same && i < expected.size(); ++i)
                same = result.surfaces[i].model_instance_index == expected[i].model_instance_index;
            check(same,"Compact-key ordering differs from stable full-record ordering");
            result.surfaces.clear(); orderViewSurfaces(*frame,result);
            result.surfaces.resize(1); orderViewSurfaces(*frame,result);
            check(result.surfaces.size() == 1,"Empty/single-element ordering failed");
        }
    }
    {
        using namespace d3scene;
        const auto reference = [](const Camera& camera,const Bounds& bounds,PortalWindow window) {
            std::array<Vec3,8> corners;
            for (unsigned i = 0; i < 8; ++i) corners[i] = {
                i&1 ? bounds.maximum.x : bounds.minimum.x,i&2 ? bounds.maximum.y : bounds.minimum.y,
                i&4 ? bounds.maximum.z : bounds.minimum.z};
            for (const auto& plane : camera.planes) {
                bool outside = true;
                for (const auto p : corners) {
                    const double distance = double(plane.normal.x)*p.x+double(plane.normal.y)*p.y+double(plane.normal.z)*p.z+plane.constant;
                    const double epsilon = 2e-6*(1+std::abs(double(plane.normal.x)*p.x)+
                        std::abs(double(plane.normal.y)*p.y)+std::abs(double(plane.normal.z)*p.z)+std::abs(plane.constant));
                    if (distance >= -epsilon) { outside = false; break; }
                }
                if (outside) return true;
            }
            bool left = true,right = true,bottom = true,top = true;
            for (const auto point : corners) {
                const auto p = project(camera,point);
                const double epsilon = 2e-6*(1+std::abs(p.x)+std::abs(p.y)+std::abs(p.w));
                left = left && p.x < window.left*p.w-epsilon; right = right && p.x > window.right*p.w+epsilon;
                bottom = bottom && p.y < window.bottom*p.w-epsilon; top = top && p.y > window.top*p.w+epsilon;
            }
            return left || right || bottom || top;
        };
        uint32_t random = 173;
        const auto sample = [&] { random = random*1664525u+1013904223u; return float(random>>8)/16777216.f; };
        for (int i = 0; i < 12000; ++i) {
            auto request = view();
            request.kind = ViewKind::Auxiliary; request.portal_visibility = false;
            const float scale = i%3 == 0 ? 1.e6f : 100.f;
            request.camera.position = {scale*(sample()-.5f),scale*(sample()-.5f),scale*(sample()-.5f)};
            const float angle = sample()*6.28f;
            request.camera.right = {std::cos(angle),0,-std::sin(angle)};
            request.camera.forward = {std::sin(angle),0,std::cos(angle)};
            if (i%2) { request.projection = ViewProjection::Orthographic; request.orthographic_size = {200,150}; request.orthographic_depth = {-200,200}; }
            if (i%10 == 0) { request.has_clip_plane = true; request.clip_plane = {{.6f,0,.8f},sample()*scale}; }
            const auto camera = prepareCamera(request);
            Bounds bounds;
            bounds.minimum = {scale*(sample()-.5f),scale*(sample()-.5f),scale*(sample()-.5f)};
            bounds.maximum = {bounds.minimum.x+scale*sample(),bounds.minimum.y+scale*sample(),bounds.minimum.z+scale*sample()};
            const PortalWindow window{-sample(),-sample(),sample(),sample(),true};
            check(boundsOutside(camera,bounds,window) == reference(camera,bounds,window),
                "Support-corner bounds test differs from eight-corner reference");
        }
    }
    {
        using namespace d3scene;
        SceneAssets fixtures;
        auto geometry = unitBillboardQuad();
        const auto asset = fixtures.publishMesh(90,geometry);
        const auto scalarVisible = [&](const PreparedMesh& mesh,const Camera& camera) {
            const MeshView source(mesh.asset->geometry(),mesh.vertices);
            for (const auto& polygon : source.topology.polygons) {
                bool rejected = false;
                for (const auto& plane : camera.planes) {
                    bool outside = true;
                    for (const auto& corner : polygon.corners) {
                        const auto p = transformPoint(mesh.world,source.positions[corner.vertex]);
                        const double distance = double(plane.normal.x)*p.x+double(plane.normal.y)*p.y+double(plane.normal.z)*p.z+plane.constant;
                        const double epsilon = 2e-6*(1+std::abs(double(plane.normal.x)*p.x)+
                            std::abs(double(plane.normal.y)*p.y)+std::abs(double(plane.normal.z)*p.z)+std::abs(plane.constant));
                        if (distance >= -epsilon) { outside = false; break; }
                    }
                    rejected = rejected || outside;
                }
                bool left = true,right = true,bottom = true,top = true;
                for (const auto& corner : polygon.corners) {
                    const auto p = project(camera,transformPoint(mesh.world,source.positions[corner.vertex]));
                    const double epsilon = 2e-6*(1+std::abs(p.x)+std::abs(p.y)+std::abs(p.w));
                    left = left && p.x < -p.w-epsilon; right = right && p.x > p.w+epsilon;
                    bottom = bottom && p.y < -p.w-epsilon; top = top && p.y > p.w+epsilon;
                }
                if (!rejected && !left && !right && !bottom && !top) return true;
            }
            return false;
        };
        for (int sample = 0; sample < 300; ++sample) {
            FrameData fixture; fixture.sequence = sample+1; fixture.scene_lifetime = fixtures.lifetime();
            auto request = view(); request.kind = ViewKind::Auxiliary; request.portal_visibility = false;
            request.camera.position = {float(sample%3),float(sample%5),0};
            if (sample%2) { request.projection = ViewProjection::Orthographic; request.orthographic_size = {20,15}; }
            if (sample%6 == 0) { request.has_clip_plane = true; request.clip_plane = {{1,0,0},-2}; }
            for (int instance = 0; instance < 2; ++instance) {
                PreparedMesh mesh; mesh.asset = asset;
                mesh.world.position = {float(sample%17-8)*3+instance*7.f,float(sample%11-5)*2,float(sample%23-5)};
                mesh.world.right = {float(sample%7+1),0,float(instance)};
                mesh.world.up = {0,float(sample%4+1),0};
                if (sample%5 == 0) { mesh.vertices = geometry.vertices; mesh.vertices[0].z += 4; }
                fixture.meshes.push_back(mesh);
            }
            const auto camera = prepareCamera(request);
            const auto frozen = Presentation::freeze(fixture);
            const auto visible = queryView(*frozen,request);
            for (size_t i = 0; i < fixture.meshes.size(); ++i) {
                bool found = false;
                for (const auto& surface : visible.surfaces) found = found || (surface.mesh && surface.mesh_index == i);
                check(found == scalarVisible(fixture.meshes[i],camera),"Cached vertex clipping differs from scalar plane tests");
            }
        }
    }
    {
        using namespace d3scene;
        SceneAssets assets; FrameData data; data.sequence = 1; data.scene_lifetime = assets.lifetime();
        appendRoom(assets,data,triangle(0,3));
        data.rooms[0].mirrors = compilePlanarSurfaces(data.rooms[0].asset->geometry(),{0});
        data.rooms[0].surfaces[0].alpha = .3f;
        Portal mirror_portal; mirror_portal.polygon = 0; mirror_portal.surface_controls_visibility = true;
        data.rooms[0].portals.push_back(mirror_portal);
        const auto frame = Presentation::freeze(data);
        const auto& material = frame->data().rooms[0].surfaces[0].material;
        check(material.planar_slot && material.writesSolidDepth() && !material.allowsBehind() && material.alpha_cutoff == 0,
            "Layered mirror film was compiled as a translucent hole");
        check(!frame->data().rooms[0].portals[0].traversable,
            "Portal policy was compiled before the final mirror film material");
        auto parent = view(); parent.room = 0;
        const auto plan = planPlanarViews(*frame,parent);
        check(frame->data().has_planar_surfaces,"Publication missed authored reflection producers");
        auto no_mirrors = data;
        no_mirrors.rooms[0].mirrors.clear(); no_mirrors.has_planar_surfaces = true;
        const auto ordinary = Presentation::freeze(no_mirrors);
        check(!ordinary->data().has_planar_surfaces && planPlanarViews(*ordinary,parent).images.empty(),
            "Publication retained stale reflection-producer membership");
        check(&plan.consumer.frame() == frame.get() && plan.consumer.visibility().surfaces.size() == queryView(*frame,parent).surfaces.size(),
            "Planar schedule did not retain the consumer visibility");
        {
            auto owner = Presentation::freeze(data);
            std::weak_ptr<const Presentation> lifetime = owner;
            auto request = parent;
            const auto prepared = prepareView(*owner,request);
            const auto* identity = owner.get();
            owner.reset(); request.camera.position.x = 1000;
            check(!lifetime.expired() && &prepared.frame() == identity && prepared.view().camera.position.x == parent.camera.position.x,
                "Prepared view lost frame ownership or retained mutable camera input");
            check(prepared.visibility().surfaces.size() == queryView(prepared.frame(),prepared.view()).surfaces.size(),
                "Prepared visibility differs from its immutable camera");
            const auto replacement = Presentation::freeze(data);
            check(&prepared.frame() != replacement.get(),"Prepared view substituted another publication with the same sequence");
        }
        check(planPlanarViews(*frame,parent,4).images[0].samples == 4,
            "Planar producer lost explicit antialiasing quality");
        check(plan.images.size() == 1 && plan.consumer.view().image_inputs.size() == 1,
            "Visible mirror group did not schedule one image");
        const auto& child = plan.images[0];
        check(child.view.coverage.width > 0 && child.view.coverage.width < child.view.viewport.width &&
            child.view.coverage.height > 0 && child.view.coverage.height < child.view.viewport.height,
            "Small mirror did not constrain producer raster coverage");
        check(child.view.reflections == ReflectionPolicy::SurfaceFilm && child.view.feedback == NoFeedback &&
            child.view.excluded_surfaces.size() == 1 && queryView(*frame,child.view).surfaces.empty(),
            "Reflected producer retained its own film or recursive policy");
        check(viewImageSource(plan.consumer.view(),material.planar_slot) == child.image &&
            orderViewImages(plan.images) == std::vector<size_t>({0}),"Mirror consumer has no scheduled producer");
        check(planPlanarViews(*frame,child.view).images.empty(),"Terminal reflection scheduled recursion");
        auto moved = parent; moved.camera.position.x = .25f;
        check(planPlanarViews(*frame,moved).images[0].image == child.image,"Mirror image identity changed with camera motion");
        auto near_film = parent; near_film.camera.position.z = 2;
        const auto near_plan = planPlanarViews(*frame,near_film);
        check(near_plan.images.size() == 1 && near_plan.images[0].view.coverage.width == 0,
            "Mirror touching the near plane lost conservative full-image coverage");
        for (Vec3 point : {Vec3{0,0,3},Vec3{.5f,.2f,3}}) {
            const auto original = project(prepareCamera(parent),point);
            const auto reflected = project(prepareCamera(child.view),point);
            check(near(original.x/original.w,reflected.x/reflected.w) && near(original.y/original.w,reflected.y/reflected.w),
                "Mirror surface and source image projections disagree");
        }
        auto hidden = parent; hidden.excluded_surfaces = {{data.rooms[0].asset->id(),0}};
        check(planPlanarViews(*frame,hidden).images.empty(),"Hidden mirror allocated a producer");
        auto conflict = parent; conflict.image_inputs = {{material.planar_slot,1}};
        bool rejected = false;
        try { planPlanarViews(*frame,conflict); } catch (const std::invalid_argument&) { rejected = true; }
        check(rejected,"Planar schedule accepted a conflicting consumer slot");
        check(parent.image_inputs.empty() && data.rooms[0].surfaces[0].material.planar_slot == 0,
            "Planar compilation mutated authoring inputs");
    }
    {
        using namespace d3scene;
        SceneAssets assets; FrameData data; data.sequence = 1; data.scene_lifetime = assets.lifetime();
        appendRoom(assets,data,triangle(-1,3)); appendRoom(assets,data,triangle(1,3));
        const auto frame = Presentation::freeze(data);
        auto parent = view(); parent.portal_visibility = false;
        const RoomSurfaceId first{data.rooms[0].asset->id(),0}, second{data.rooms[1].asset->id(),0};
        auto selected = parent; selected.excluded_surfaces = {first,first};
        const auto resolved = resolveView(*frame,selected);
        check(resolved.excluded_surfaces.size() == 1,"Repeated surface exclusions were not canonicalized");
        const auto visible = queryView(*frame,resolved);
        check(visible.surfaces.size() == 1 && visible.surfaces[0].room_index == 1,
            "Surface exclusion affected the wrong room");
        check(queryView(*frame,parent).surfaces.size() == 2 && frame->data().rooms[0].surfaces[0].draw_surface,
            "View exclusion mutated shared geometry");
        auto reordered = selected; reordered.excluded_surfaces = {second,first,second};
        auto ordered = selected; ordered.excluded_surfaces = {first,second};
        check(resolveView(*frame,reordered).excluded_surfaces == resolveView(*frame,ordered).excluded_surfaces,
            "Equivalent selections depend on caller order");
        ViewHistory history; history.commit(history.prepare(data,resolved,prepareCamera(resolved)));
        bool rejected = false;
        try { history.prepare(data,parent,prepareCamera(parent)); } catch (const std::logic_error&) { rejected = true; }
        check(rejected,"One history key accepted conflicting surface selections");
        ++data.sequence;
        check(!history.prepare(data,parent,prepareCamera(parent)).previous_valid,"Selection change retained image history");
        for (int invalid = 0; invalid < 4; ++invalid) {
            auto bad = selected;
            if (invalid == 0) ++bad.excluded_surfaces[0].room.revision;
            if (invalid == 1) ++bad.excluded_surfaces[0].room.lifetime;
            if (invalid == 2) bad.excluded_surfaces[0].polygon = 1;
            if (invalid == 3) bad.feedback = Automap;
            rejected = false;
            try { queryView(*frame,bad); } catch (const std::invalid_argument&) { rejected = true; }
            check(rejected,"Stale or unqualified surface selection accepted");
        }
        data.rooms[0].asset = assets.publishRoom(0,triangle(-1,4));
        rejected = false;
        try { queryView(*Presentation::freeze(data),selected); } catch (const std::invalid_argument&) { rejected = true; }
        check(rejected && queryView(*frame,selected).surfaces.size() == 1,
            "Replacement geometry aliased a held surface selection");
    }
    {
        using namespace d3scene;
        ViewImageRequest image; image.image = 17; image.view = view();
        image.view.kind = ViewKind::Preview; image.samples = 4;
        validateViewImage(image);
        image.view.coverage = {20,30,100,200}; validateViewImage(image);
        for (int invalid = 0; invalid < 11; ++invalid) {
            auto bad = image;
            if (invalid == 0) bad.image = 0;
            if (invalid == 1) bad.view.kind = ViewKind::Main;
            if (invalid == 2) bad.view.target = ViewTarget::Display;
            if (invalid == 3) bad.view.feedback = Automap;
            if (invalid == 4) bad.view.viewport.x = 1;
            if (invalid == 5) bad.scale = 3;
            if (invalid == 6) bad.samples = 1;
            if (invalid == 7) { bad.scale = 4; bad.view.viewport.width = (std::numeric_limits<int32_t>::max)(); }
            if (invalid == 8) bad.view.coverage.x = -1;
            if (invalid == 9) bad.view.coverage.width = bad.view.viewport.width;
            if (invalid == 10) bad.view.coverage.height = 0;
            bool rejected = false;
            try { validateViewImage(bad); } catch (const std::invalid_argument&) { rejected = true; }
            check(rejected,"Invalid independent view image request accepted");
        }
        SceneAssets assets; FrameData data; data.sequence = 1; data.scene_lifetime = assets.lifetime();
        appendRoom(assets,data,triangle(0,3));
        auto& surface = data.rooms[0].surfaces[0]; surface.bitmap = -1; surface.view_image_slot = 1;
        surface.texture_alpha = true;
        check(queryView(*Presentation::freeze(data),view()).surfaces.size() == 1,
            "Room view-image surface was discarded by a bitmap sentinel");
        check(Presentation::freeze(data)->data().rooms[0].surfaces[0].material.texture_alpha == TextureAlpha::Continuous,
            "View image lost continuous alpha semantics");
        surface.bitmap = 1;
        bool rejected = false;
        try { Presentation::freeze(data); } catch (const std::invalid_argument&) { rejected = true; }
        check(rejected,"Surface accepted ambiguous asset and view image sources");
        auto consumer = view(); consumer.image_inputs.push_back({1,image.image});
        check(viewImageSource(consumer,1) == image.image,"View image input did not resolve");
        auto other = consumer; other.image_inputs[0].image = 18;
        check(viewImageSource(other,1) == 18 && viewImageSource(consumer,1) == 17,"View image sources leaked between cameras");
        rejected = false;
        try { viewImageSource(consumer,2); } catch (const std::invalid_argument&) { rejected = true; }
        check(rejected,"Missing view image input fell back to a global image");
        consumer.image_inputs.push_back({1,18}); rejected = false;
        try { validateView(consumer); } catch (const std::invalid_argument&) { rejected = true; }
        check(rejected,"Duplicate view image slots accepted");
        auto dependent = image; dependent.image = 19;
        dependent.view.image_inputs = {{1,17},{2,17}};
        auto independent = image; independent.image = 20;
        check(orderViewImages({dependent,image,independent}) == std::vector<size_t>({1,0,2}),
            "View image graph lost dependency order or duplicated a producer");
        check(orderViewImages({}).empty(),"Empty view image graph produced work");
        {
            FrameData image_data; image_data.sequence = 8; image_data.scene_lifetime = assets.lifetime();
            appendRoom(assets,image_data,triangle(0,3));
            auto image_frame = Presentation::freeze(image_data);
            std::vector<ViewImageRequest> requests{dependent,image,independent};
            for (auto& request : requests) { request.view.coverage = {}; request.view.history_key = request.image; }
            const auto compiled = prepareViewImages(*image_frame,requests);
            check(compiled.images().size() == 3 && compiled.images()[0].image() == 17 &&
                compiled.images()[1].image() == 19 && compiled.images()[2].image() == 20,
                "Prepared image producers lost dependency order");
            check(compiled.images()[0].samples() == 4 && compiled.images()[0].scale() == 1 &&
                compiled.images()[0].producer().visibility().surfaces.size() == 1,
                "Image preparation did not own its actual visibility/sample contract");
            requests[1].view.camera.position.x = 100;
            image_data.sequence = 9;
            const auto later_frame = Presentation::freeze(image_data);
            const auto later = prepareViewImages(*later_frame,requests);
            image_frame.reset();
            check(compiled.images()[0].producer().frame().data().sequence == 8 &&
                compiled.images()[0].producer().view().camera.position.x == 0 &&
                later.images()[0].producer().view().camera.position.x == 100,
                "Prepared producer borrowed later camera/frame state");
            const auto copied = compiled;
            check(copied.images()[0].producer().visibility().surfaces.size() == 1,
                "Copied image plan lost independently owned query results");
            auto external = requests[0];
            external.view.image_inputs = {{1,17,copied.images()[0].producer().frame().shared_from_this()}};
            check(orderViewImages({external}) == std::vector<size_t>{0},
                "Explicit external image incorrectly required a second local world producer");
            const auto external_plan = prepareViewImages(*later_frame,{external});
            check(viewImageInput(external_plan.images()[0].producer().view(),1).producer->data().sequence == 8,
                "External image consumer lost its immutable producer owner");
            bool alias = false;
            try { orderViewImages({external,requests[1]}); } catch (const std::invalid_argument&) { alias = true; }
            check(alias,"External image was silently overwritten by a local producer");
            auto invalid_external = external;
            invalid_external.view.image_inputs[0].producer = later_frame;
            bool same_publication = false;
            try { prepareViewImages(*later_frame,{invalid_external}); } catch (const std::invalid_argument&) { same_publication = true; }
            check(same_publication,"Same-publication image bypassed the local dependency graph");
            SceneAssets unrelated_assets;
            FrameData unrelated_data; unrelated_data.sequence = 1; unrelated_data.scene_lifetime = unrelated_assets.lifetime();
            invalid_external.view.image_inputs[0].producer = Presentation::freeze(unrelated_data);
            bool stale_lifetime = false;
            try { prepareViewImages(*later_frame,{invalid_external}); } catch (const std::invalid_argument&) { stale_lifetime = true; }
            check(stale_lifetime,"External image consumer accepted a different scene lifetime");
            auto invalid_overlay = image_data;
            PreparedForeground external_layer; external_layer.identity = 1;
            external_layer.layer.display_views.push_back(invalid_external.view);
            external_layer.layer.display_views.back().target = ViewTarget::Display;
            invalid_overlay.foregrounds.push_back(external_layer);
            bool rejected_publication = false;
            try { Presentation::freeze(invalid_overlay); } catch (const std::invalid_argument&) { rejected_publication = true; }
            check(rejected_publication,"Overlay publication retained invalid external image provenance");
            auto invalid_requests = requests; invalid_requests[0].view.image_inputs[0].image = 999;
            bool invalid_plan = false;
            try { prepareViewImages(*later_frame,invalid_requests); } catch (const std::invalid_argument&) { invalid_plan = true; }
            check(invalid_plan,"Prepared image plan accepted an absent dependency");
            check(prepareViewImages(*later_frame,{}).images().empty(),"Empty prepared image plan retained old producers");
        }
        for (int invalid = 0; invalid < 4; ++invalid) {
            auto a = image, b = dependent;
            if (invalid == 0) b.image = a.image;
            if (invalid == 1) b.view.image_inputs = {{1,99}};
            if (invalid == 2) a.view.image_inputs = {{1,19}};
            if (invalid == 3) a.view.image_inputs = {{1,17}};
            rejected = false;
            try { orderViewImages({a,b}); } catch (const std::invalid_argument&) { rejected = true; }
            check(rejected,"Invalid view image dependency graph accepted");
        }
    }
    {
        using namespace d3scene;
        SceneAssets assets; FrameData data; data.sequence = 1; data.scene_lifetime = assets.lifetime();
        appendRoom(assets,data,triangle(0,3,false));
        appendRoom(assets,data,triangle(0,7,false));
        const auto frame = Presentation::freeze(data);
        auto parent = view(); parent.room = 0; parent.portal_visibility = false;
        parent.camera_velocity = {3,4,5};
        parent.excluded_objects = {42};
        auto reflected = reflectedView(*frame,parent,{{0,0,2},-10},2,0);
        check(reflected.camera_velocity.x == 3 && reflected.camera_velocity.y == 4 && reflected.camera_velocity.z == -5 &&
            parent.camera_velocity.z == 5,"Mirror lost reflected physical camera motion or mutated its source");
        check(near(reflected.camera.position.z,10) && near(reflected.camera.forward.z,-1),"Mirror camera reflection failed");
        check(reversesWinding(reflected.camera) && !reversesWinding(parent.camera),"Reflection lost orientation parity");
        check(reflected.fog_path_origin == FogPathOrigin::ClipPlane && parent.fog_path_origin == FogPathOrigin::Camera,
            "Reflection did not declare its physical optical interval");
        check(reflected.kind == ViewKind::Mirror && reflected.feedback == NoFeedback && reflected.excluded_objects == parent.excluded_objects,
            "Reflected view lost explicit policy");
        const auto camera = prepareCamera(reflected);
        check(sphereIntersects(camera,{0,0,3},0) && !sphereIntersects(camera,{0,0,7},0),"Mirror half-space admitted behind-plane geometry");
        check(sphereIntersects(camera,{0,0,5.5f},1),"Mirror plane rejected a crossing sphere");
        const auto visible = queryView(*frame,reflected);
        check(visible.surfaces.size() == 1 && visible.surfaces[0].room_index == 0,"Geometry query ignored mirror clipping");
        const auto opposite = reflectedView(*frame,parent,{{0,0,-1},5},3,0);
        check(opposite.clip_plane.normal.z == reflected.clip_plane.normal.z && opposite.clip_plane.constant == reflected.clip_plane.constant,
            "Mirror plane orientation changed retained physical half-space");
        const auto clip = project(camera,{1,0,3});
        const auto point = unproject(camera,float(clip.x/clip.w),float(clip.y/clip.w),float((clip.z/clip.w+1)*.5));
        check(near(point.x,1) && near(point.z,3),"Reflected camera projection did not round trip");
        check(parent.camera.position.z == 0 && frame->data().rooms[0].asset->geometry().vertices[0].z == 3,
            "Mirror view mutated its parent or scene");
        const auto oblique = reflectedView(*frame,parent,{{1,0,1},-10},4,0);
        check(near(oblique.camera.position.x,10) && near(oblique.camera.position.z,10) &&
            near(oblique.camera.forward.x,-1) && near(oblique.camera.forward.z,0),"Oblique reflection failed");
        auto clipped = parent; clipped.has_clip_plane = true; clipped.clip_plane = {{1,0,0},0};
        auto crossing = data; crossing.rooms.resize(1);
        crossing.rooms[0].asset = assets.publishRoom(0,triangle(0,3));
        check(queryView(*Presentation::freeze(crossing),clipped).surfaces.size() == 1,
            "Clip query rejected a polygon crossing the plane");
        ViewHistory history; const auto first = history.prepare(data,reflected,camera); history.commit(first);
        auto moved_clip = reflected; moved_clip.clip_plane.constant += 1;
        bool rejected = false;
        try { history.prepare(data,moved_clip,prepareCamera(moved_clip)); } catch (const std::logic_error&) { rejected = true; }
        check(rejected,"History accepted conflicting clip volumes for one view");
        auto changed_optics = reflected; changed_optics.fog_path_origin = FogPathOrigin::Camera;
        rejected = false;
        try { history.prepare(data,changed_optics,prepareCamera(changed_optics)); } catch (const std::logic_error&) { rejected = true; }
        check(rejected,"History accepted conflicting fog paths for one view");
        ++data.sequence;
        check(!history.prepare(data,changed_optics,prepareCamera(changed_optics)).previous_valid,
            "Optical interval change reused temporal history");
        check(!history.prepare(data,moved_clip,prepareCamera(moved_clip)).previous_valid,"Clip change reused temporal history");
        for (int invalid = 0; invalid < 4; ++invalid) {
            rejected = false;
            try {
                if (invalid == 0) reflectedView(*frame,parent,{},2,0);
                if (invalid == 1) reflectedView(*frame,parent,{{0,0,1},-5},1,0);
                if (invalid == 2) reflectedView(*frame,parent,{{0,0,1},-5},2,99);
                if (invalid == 3) reflectedView(*frame,reflected,{{0,0,1},-5},3,0);
            } catch (const std::invalid_argument&) { rejected = true; }
            check(rejected,"Invalid reflected view request accepted");
        }
        auto bad = reflected; bad.clip_plane.normal = {};
        rejected = false;
        try { validateView(bad); } catch (const std::invalid_argument&) { rejected = true; }
        check(rejected,"Degenerate world clip plane accepted");
    }
    {
        d3scene::SceneAssets batch_assets;
        d3scene::FrameData data; data.sequence = 1; data.scene_lifetime = batch_assets.lifetime();
        d3scene::PreparedMesh batch;
        batch.asset = batch_assets.publishMesh(258,d3scene::unitQuadBatch(2));
        for (int i = 0; i < 8; ++i) batch.texture_uvs.push_back({float(i),float(i+10)});
        data.meshes.push_back(batch);
        const auto frame = d3scene::Presentation::freeze(data);
        std::vector<d3scene::Vec2> coordinates;
        d3scene::appendPreparedTextureCoordinates(frame->data().meshes[0],coordinates);
        const int indices[12] = {0,1,2,0,2,3,4,5,6,4,6,7};
        check(coordinates.size() == 12,"Prepared texture coordinates lost quad batch triangles");
        d3scene::appendPreparedTextureCoordinates(frame->data().meshes[0],coordinates);
        check(coordinates.size() == 24 && coordinates[12].x == coordinates[0].x,
            "UV upload append overwrote an earlier mesh range");
        for (int i = 0; i < 12; ++i) check(coordinates[i].x == indices[i] && coordinates[i].y == indices[i]+10,
            "Prepared base UV ordering diverged from triangulated positions");
        data.meshes[0].texture_uvs[0].x = 99;
        check(frame->data().meshes[0].texture_uvs[0].x == 0,"Published UVs borrowed caller storage");
        data.meshes[0].texture_uvs.pop_back(); bool rejected = false;
        try { d3scene::Presentation::freeze(data); } catch (const std::invalid_argument&) { rejected = true; }
        check(rejected,"Incomplete base UV attributes passed publication");
        data.meshes[0].texture_uvs = batch.texture_uvs;
        data.meshes[0].texture_uvs[0].x = std::numeric_limits<float>::infinity(); rejected = false;
        try { d3scene::Presentation::freeze(data); } catch (const std::invalid_argument&) { rejected = true; }
        check(rejected,"Nonfinite base UV attributes passed publication");
        for (const uint32_t count : {0u,129u}) {
            rejected = false;
            try { d3scene::unitQuadBatch(count); } catch (const std::invalid_argument&) { rejected = true; }
            check(rejected,"Unbounded canonical quad batch was accepted");
        }
    }
    {
        auto canvas = view(); canvas.kind = d3scene::ViewKind::Preview;
        canvas.projection = d3scene::ViewProjection::Orthographic;
        canvas.orthographic_size = {800,600}; canvas.orthographic_depth = {-2,2};
        canvas.camera.position = {400,300,0}; canvas.camera.up = {0,-1,0};
        const auto camera = d3scene::prepareCamera(canvas);
        const auto first = d3scene::project(camera,{0,0,-2});
        const auto last = d3scene::project(camera,{800,600,2});
        check(near(first.x,-1) && near(first.y,1) && near(first.z,-1) && first.w == 1 &&
            near(last.x,1) && near(last.y,-1) && near(last.z,1) && last.w == 1,
            "Orthographic canvas lost corners, depth endpoints or constant W");
        const auto near_point = d3scene::project(camera,{123,456,-1});
        const auto far_point = d3scene::project(camera,{123,456,1});
        check(near(near_point.x,far_point.x) && near(near_point.y,far_point.y),"Orthographic size changed with depth");
        const auto restored = d3scene::unproject(camera,float(near_point.x),float(near_point.y),float((near_point.z+1)*.5));
        check(std::abs(restored.x-123) < .0001 && std::abs(restored.y-456) < .0001 && near(restored.z,-1),
            "Orthographic depth unprojection did not recover the source point");
        check(d3scene::sphereIntersects(camera,{400,300,0},0) &&
            !d3scene::sphereIntersects(camera,{-10,300,0},1) &&
            !d3scene::sphereIntersects(camera,{400,610,0},1) &&
            !d3scene::sphereIntersects(camera,{400,300,-3},.5f),"Orthographic conservative planes are not parallel");
        const auto matrix = d3scene::viewMatrices(camera).view_projection;
        const double point[4] = {123,456,-1,1}; double actual[4]{};
        for (int row = 0; row < 4; ++row) for (int column = 0; column < 4; ++column)
            actual[row] += matrix[column*4+row]*point[column];
        check(near(actual[0],near_point.x) && near(actual[1],near_point.y) && near(actual[2],near_point.z) && actual[3] == 1,
            "Orthographic image and raster camera projections disagree");
        for (int invalid = 0; invalid < 3; ++invalid) {
            auto request = canvas;
            if (invalid == 0) request.kind = d3scene::ViewKind::Main;
            if (invalid == 1) request.orthographic_depth = {2,2};
            if (invalid == 2) request.orthographic_size.x = 0;
            bool rejected = false;
            try { d3scene::prepareCamera(request); } catch (const std::invalid_argument&) { rejected = true; }
            check(rejected,"Invalid orthographic volume or main-world ownership was accepted");
        }
    }
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
    {
        d3scene::ForegroundLayer layer;
        const auto rejected = [&] {
            try { d3scene::validateForeground(layer); } catch (const std::invalid_argument&) { return true; }
            return false;
        };
        check(rejected(),"Empty foreground accepted");
        auto request = view(); request.kind = d3scene::ViewKind::Preview;
        layer.scene_views.push_back(request);
        check(!rejected(),"Independent foreground rejected");
        layer.scene_views[0].kind = d3scene::ViewKind::Main;
        check(rejected(),"Foreground accepted world ownership");
        layer.scene_views[0] = request;
        layer.scene_views[0].feedback = d3scene::Automap;
        check(rejected(),"Foreground accepted game observations");
        layer.scene_views[0] = request;
        layer.scene_views[0].target = d3scene::ViewTarget::Display;
        check(rejected(),"Scene-resolution list accepted display view");
        layer.scene_views[0] = request;
        layer.display_views.push_back(request);
        check(rejected(),"Display-resolution list accepted scene view");
        layer.display_views[0].target = d3scene::ViewTarget::Display;
        check(!rejected(),"Ordered mixed-resolution layer rejected");
        layer.scene_views.clear();
        check(!rejected(),"Native-resolution-only layer rejected");
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
    auto demanded_rooms = std::vector<uint32_t>{};
    for (const auto& item : a.surfaces) demanded_rooms.push_back(item.room_index);
    std::sort(demanded_rooms.begin(),demanded_rooms.end());
    demanded_rooms.erase(std::unique(demanded_rooms.begin(),demanded_rooms.end()),demanded_rooms.end());
    check(a.geometry.rooms == demanded_rooms,"Room demand lost source order or included invisible geometry");
    check(a.stats.surfaces_backfacing==1&&a.stats.surfaces_outside==3,"Query rejection counters incorrect");
    check(a.surfaces[0].room_index==6&&a.surfaces[1].room_index==7,"Opaque near-to-far ordering changed");
    check(a.surfaces[4].room_index==8&&a.surfaces[5].room_index==9,"Transparent far-to-near ordering changed");
    check(a.object_indices.size()==1&&a.object_indices[0]==0,"Object sphere query incorrect");
    v=view(); v.excluded_objects={31};
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
    {
        // Independent materialized-clipping reference for streaming aperture
        // bounds, including eye-plane crossings and entirely hidden openings.
        using namespace d3scene;
        SceneAssets fixture_assets;
        FrameData data; data.sequence=1; data.scene_lifetime=fixture_assets.lifetime();
        appendRoom(fixture_assets,data,triangle(0,2));
        appendRoom(fixture_assets,data,triangle(0,4));
        Portal portal; portal.polygon=0; portal.destination_room=1; portal.two_sided=true;
        data.rooms[0].portals.push_back(portal);
        const auto frame=Presentation::freeze(data);
        const auto& mesh=frame->data().rooms[0].asset->geometry();
        for (int sample=0;sample<512;++sample) {
            auto request=view(); request.room=0;
            const float angle=float(sample)*.071f;
            request.camera.position={float(sample%17-8)*.2f,float(sample%11-5)*.2f,float(sample%23)*.2f};
            request.camera.right={std::cos(angle),0,-std::sin(angle)};
            request.camera.forward={std::sin(angle),0,std::cos(angle)};
            const auto camera=prepareCamera(request);
            std::vector<ClipPosition> input,clipped;
            for (auto p : mesh.vertices) input.push_back(project(camera,p));
            auto previous=input.back();
            constexpr double eye_clip=1.e-5;
            for (auto current : input) {
                if ((previous.w>=eye_clip)!=(current.w>=eye_clip)) {
                    const double t=(eye_clip-previous.w)/(current.w-previous.w);
                    clipped.push_back({previous.x+(current.x-previous.x)*t,previous.y+(current.y-previous.y)*t,0,eye_clip});
                }
                if (current.w>=eye_clip) clipped.push_back(current);
                previous=current;
            }
            const double infinity=std::numeric_limits<double>::infinity();
            PortalWindow expected{infinity,infinity,-infinity,-infinity,!clipped.empty()};
            for (auto p : clipped) {
                expected.left=std::min(expected.left,p.x/p.w); expected.right=std::max(expected.right,p.x/p.w);
                expected.bottom=std::min(expected.bottom,p.y/p.w); expected.top=std::max(expected.top,p.y/p.w);
            }
            const auto eye=request.camera.position;
            if (clipped.empty() && std::abs(2.0-eye.z)<=2.e-6*(1+std::abs(eye.x)+std::abs(eye.y)+std::abs(eye.z)))
                expected={-1,-1,1,1,true};
            expected.left=std::max(-1.0,expected.left); expected.right=std::min(1.0,expected.right);
            expected.bottom=std::max(-1.0,expected.bottom); expected.top=std::min(1.0,expected.top);
            expected.visible=expected.visible && expected.left<=expected.right && expected.bottom<=expected.top;
            const auto result=queryPortalVisibility(*frame,request,camera);
            const auto actual=result.rooms[1];
            check(actual.visible==expected.visible && (!expected.visible ||
                (actual.left==expected.left && actual.right==expected.right &&
                 actual.bottom==expected.bottom && actual.top==expected.top)),
                "Streaming portal clipping differs from materialized reference");
        }
    }
    d3scene::FrameData portals; portals.sequence = 2; portals.scene_lifetime = assets.lifetime();
    for (int i = 0; i < 4; ++i) appendRoom(assets,portals,triangle(0,2.f+i*2));
    portals.rooms[0].portals.push_back({0,1});
    portals.rooms[1].portals.push_back({0,2});
    portals.rooms[2].portals.push_back({0,1}); // cycle must converge without a depth cutoff
    auto portal_view = view(); portal_view.room = 0;
    auto portal_frame = d3scene::Presentation::freeze(portals);
    {
        auto retained = portal_frame->data();
        const auto next = d3scene::Presentation::freeze(retained);
        check(&next->data().rooms[0].portals.read() == &portal_frame->data().rooms[0].portals.read(),
            "Unchanged portal topology was copied");
        retained.rooms[0].portals[0].traversable = false;
        const auto changed = d3scene::Presentation::freeze(retained);
        check(!changed->data().rooms[0].portals[0].traversable && portal_frame->data().rooms[0].portals[0].traversable,
            "Traversal edit mutated held portal topology");
        auto builder = portals;
        auto& borrowed = builder.rooms[0].portals[0];
        const auto published = d3scene::Presentation::freeze(builder);
        borrowed.destination_room = 3;
        check(published->data().rooms[0].portals[0].destination_room == 1,
            "Unsealed portal builder retained a mutable alias after freeze");
    }
    check(!d3scene::queryPortalVisibility(*portal_frame,portal_view,d3scene::prepareCamera(portal_view)).exterior.visible,
        "Enclosed portal graph admitted exterior observations");
    auto windows = d3scene::queryPortalVisibility(*portal_frame,portal_view,d3scene::prepareCamera(portal_view)).rooms;
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
    windows = d3scene::queryPortalVisibility(*d3scene::Presentation::freeze(portals),portal_view,d3scene::prepareCamera(portal_view)).rooms;
    check(windows[1].visible && windows[1].left < -.79 && windows[1].right > .79,"Multiple portal windows were not unioned");
    check(portal_frame->data().rooms[0].asset->geometry().vertices[0].z == 2,"Portal query changed held scene assets");
    auto exterior_data = portals;
    exterior_data.rooms[0].exterior = true;
    auto exterior_view = view(); exterior_view.exterior_origin = true;
    auto exterior_frame = d3scene::Presentation::freeze(exterior_data);
    const auto exterior_aperture = d3scene::queryPortalVisibility(*exterior_frame,exterior_view,d3scene::prepareCamera(exterior_view)).exterior;
    check(exterior_aperture.visible && exterior_aperture.left == -1 && exterior_aperture.right == 1 &&
        exterior_aperture.bottom == -1 && exterior_aperture.top == 1,"Exterior camera lost full terrain aperture");
    auto exterior_windows = d3scene::queryPortalVisibility(*exterior_frame,exterior_view,d3scene::prepareCamera(exterior_view)).rooms;
    check(exterior_windows[0].visible && exterior_windows[1].visible && exterior_windows[2].visible && !exterior_windows[3].visible,
        "Exterior origin exposed disconnected interiors");
    exterior_data.rooms[1].blocks_portals = true;
    exterior_windows = d3scene::queryPortalVisibility(*d3scene::Presentation::freeze(exterior_data),exterior_view,d3scene::prepareCamera(exterior_view)).rooms;
    check(exterior_windows[1].visible && !exterior_windows[2].visible,"Exterior seed bypassed a closed door");
    auto reversed_apertures = exterior_data.rooms[0].asset->geometry();
    for (auto& face : reversed_apertures.polygons) face.normal = {0,0,1};
    exterior_data.rooms[0].asset = assets.publishRoom(0,reversed_apertures);
    exterior_windows = d3scene::queryPortalVisibility(*d3scene::Presentation::freeze(exterior_data),exterior_view,d3scene::prepareCamera(exterior_view)).rooms;
    check(!exterior_windows[1].visible,"One-sided backfacing aperture was traversed");
    for (auto& portal : exterior_data.rooms[0].portals) portal.two_sided = true;
    exterior_windows = d3scene::queryPortalVisibility(*d3scene::Presentation::freeze(exterior_data),exterior_view,d3scene::prepareCamera(exterior_view)).rooms;
    check(exterior_windows[1].visible && !exterior_windows[2].visible,"Two-sided door aperture lost visibility or bypassed door closure");
    exterior_data.rooms[0].exterior = false;
    exterior_windows = d3scene::queryPortalVisibility(*d3scene::Presentation::freeze(exterior_data),exterior_view,d3scene::prepareCamera(exterior_view)).rooms;
    check(!exterior_windows[0].visible && !exterior_windows[3].visible,"No exterior shells fell back to all interiors");
    exterior_data.rooms[3].exterior = true;
    exterior_data.rooms[3].asset = assets.publishRoom(3,triangle(100,5));
    exterior_windows = d3scene::queryPortalVisibility(*d3scene::Presentation::freeze(exterior_data),exterior_view,d3scene::prepareCamera(exterior_view)).rooms;
    check(!exterior_windows[3].visible,"Offscreen exterior shell seeded traversal");
    exterior_data.rooms[3].asset = assets.publishRoom(3,triangle(0,.5f));
    exterior_windows = d3scene::queryPortalVisibility(*d3scene::Presentation::freeze(exterior_data),exterior_view,d3scene::prepareCamera(exterior_view)).rooms;
    check(exterior_windows[3].visible,"Near-plane exterior shell lost aperture eligibility");
    exterior_data.rooms[3].asset = assets.publishRoom(3,triangle(0,-5));
    exterior_windows = d3scene::queryPortalVisibility(*d3scene::Presentation::freeze(exterior_data),exterior_view,d3scene::prepareCamera(exterior_view)).rooms;
    check(!exterior_windows[3].visible,"Exterior shell behind camera seeded traversal");
    auto enclosing = triangle(0,5); enclosing.vertices[0] = {-100,-100,-5}; enclosing.vertices[1] = {100,100,5};
    exterior_data.rooms[3].asset = assets.publishRoom(3,enclosing);
    exterior_windows = d3scene::queryPortalVisibility(*d3scene::Presentation::freeze(exterior_data),exterior_view,d3scene::prepareCamera(exterior_view)).rooms;
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
    const auto prepared_shake = d3scene::prepareView(*shake_frame,attached);
    check(prepared_shake.view().attached_camera && prepared_shake.view().camera_effects_resolved && prepared_shake.view().camera.forward.x == 1 &&
        prepared_shake.visibility().camera.transform.forward.x == 1,
        "Executable preparation applied camera shake twice or queried another camera");
    attached.camera_owner = 43;
    check(d3scene::resolveView(*shake_frame,attached).camera.forward.z == 1,"Shake leaked into another object's camera");
    attached.camera_owner = 42; attached.attached_camera = false;
    check(d3scene::resolveView(*shake_frame,attached).camera.forward.z == 1,"Detached preview inherited player shake");
    d3scene::FrameData environment; environment.sequence = 4; environment.scene_lifetime = assets.lifetime();
    auto colored = triangle(0,10); colored.vertex_colors = {{1,0,0},{0,1,0},{0,0,1}};
    const auto original_mesh = assets.publishMesh(0,colored);
    {
        d3scene::SceneAssets fragments;
        const auto piece = fragments.createMesh(colored);
        const auto other = fragments.createMesh(colored);
        check(piece->id() != other->id() && piece->geometry() == other->geometry(),
            "Independent fragment allocations aliased a mutable topology identity");
        const auto old_id = piece->id();
        fragments.reset();
        const auto recycled = fragments.createMesh(colored);
        check(recycled->id().lifetime != old_id.lifetime && piece->geometry() == colored,
            "Retiring a scene invalidated held fragment geometry or reused its identity");
    }
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
    check(sky_result.surfaces.size() == 1 && sky_result.world(sky_result.surfaces[0]).position.x == 100 &&
        sky_result.world(sky_result.surfaces[0]).position.y == 1 && sky_result.world(sky_result.surfaces[0]).position.z == 50,
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
        std::vector<d3scene::PositionPair> pairs;
        d3scene::appendPreparedPositionPairs(prepared.asset->geometry(),prepared.vertices,prepared.vertices,pairs);
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
    {
        const auto stroke_camera = d3scene::prepareCamera(view());
        const auto placement = d3scene::axisBillboardPlacement({0,2,10},{0,-2,10},0);
        const auto stroke_side = d3scene::rasterStrokeSide(stroke_camera,placement,{0,0,800,600});
        check(near(stroke_side.x,1) && near(stroke_side.y,0),"Raster stroke lost screen-perpendicular width");
        auto crossing = d3scene::axisBillboardPlacement({-2,0,-1},{2,0,10},0);
        const auto clipped_side = d3scene::rasterStrokeSide(stroke_camera,crossing,{0,0,800,600});
        check(near(clipped_side.x,0) && near(clipped_side.y,1),"Near-crossing stroke divided behind the eye");
        const auto degenerate = d3scene::axisBillboardPlacement({0,0,2},{0,0,20},0);
        const auto end_on = d3scene::rasterStrokeSide(stroke_camera,degenerate,{0,0,800,600});
        check(end_on.x == 0 && end_on.y == 0,"End-on raster stroke produced arbitrary geometry");
        const auto hidden = d3scene::axisBillboardPlacement({1,0,-1},{0,0,-2},0);
        check(d3scene::rasterStrokeSide(stroke_camera,hidden,{0,0,800,600}).x == 0,"Behind-eye stroke did not collapse");
        auto oblique = d3scene::axisBillboardPlacement({-2,2,10},{2,-2,20},0);
        const auto square = d3scene::rasterStrokeSide(stroke_camera,oblique,{0,0,800,800});
        const auto wide = d3scene::rasterStrokeSide(stroke_camera,oblique,{0,0,1600,800});
        check(near(square.x*square.x+square.y*square.y,1) && near(wide.x*wide.x+wide.y*wide.y,1) &&
            !near(square.x,wide.x),"Stroke width/orientation ignored logical viewport aspect");
        auto reverse = view(); reverse.camera.forward = {0,0,-1}; reverse.camera.right = {-1,0,0};
        check(d3scene::rasterStrokeSide(d3scene::prepareCamera(reverse),placement,{0,0,800,600}).x == 0 &&
            near(d3scene::rasterStrokeSide(stroke_camera,placement,{0,0,800,600}).x,1),"Stroke facing leaked across views");
        d3scene::FrameData strokes; strokes.sequence = 1; strokes.scene_lifetime = assets.lifetime();
        d3scene::PreparedMesh stroke; stroke.asset = assets.fadingStrokeMesh();
        check(stroke.asset == assets.fadingStrokeMesh(),"Stroke topology allocated per instance");
        const auto solid = assets.strokeMesh();
        check(solid == assets.strokeMesh() && !solid->hasVertexAlpha() &&
            solid->geometry().raster_offsets.size() == 4,"Solid stroke lost shared constant-opacity topology");
        const auto disk = assets.radialDiskMesh();
        check(disk == assets.radialDiskMesh() && disk->geometry().polygons.size() == 16 &&
            disk->geometry().polygons[0].corners[0].alpha == 0 &&
            disk->geometry().polygons[0].corners[2].alpha == 255,"Matcen disk radial coverage changed");
        check(stroke.asset->geometry().polygons[0].corners[0].alpha == 255 &&
            stroke.asset->geometry().polygons[0].corners[2].alpha == 0,"Stroke endpoint opacity lost");
        stroke.world = placement; stroke.raster_stroke_width = 1; stroke.surface.unlit = true;
        stroke.surface.blend = d3scene::BlendMode::Additive; strokes.meshes.push_back(stroke);
        const auto held = d3scene::Presentation::freeze(strokes);
        check(d3scene::queryView(*held,view()).surfaces.size() == 1,"World raster stroke was dropped before expansion");
        strokes.meshes[0].raster_stroke_width = -1;
        bool rejected_stroke = false;
        try { d3scene::Presentation::freeze(strokes); } catch (const std::invalid_argument&) { rejected_stroke = true; }
        check(rejected_stroke && held->data().meshes[0].raster_stroke_width == 1,"Stroke width was invalid or not owned");
        strokes.meshes[0].raster_stroke_width = 1; strokes.meshes[0].surface.depth = d3scene::DepthMode::ReadWrite;
        rejected_stroke = false;
        try { d3scene::Presentation::freeze(strokes); } catch (const std::invalid_argument&) { rejected_stroke = true; }
        check(rejected_stroke,"Raster stroke entered unsupported depth/history path");
    }
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
    auto excluded_view = view(); excluded_view.excluded_objects = {91};
    check(d3scene::queryView(*owned_marker,excluded_view).surfaces.empty(),"Camera did not exclude its owner's effect geometry");
    excluded_view.excluded_objects = {92};
    check(d3scene::queryView(*owned_marker,excluded_view).surfaces.size() == 1,"Camera excluded another object's effect");
    {
        auto rig_data = marker_data;
        d3scene::ObjectState camera_owner; camera_owner.handle = 92; camera_owner.camera_hidden_objects = {91};
        rig_data.objects.push_back(camera_owner);
        const auto rig_frame = d3scene::Presentation::freeze(rig_data);
        auto rig_view = view(); rig_view.camera_owner = 92; rig_view.exclude_camera_rig = true;
        rig_view.excluded_objects = {91};
        const auto resolved = d3scene::resolveView(*rig_frame,rig_view);
        check(resolved.excluded_objects == std::vector<uint32_t>({91,92}),"Camera rig exclusions were not canonicalized");
        check(d3scene::resolveView(*rig_frame,resolved).excluded_objects == resolved.excluded_objects,
            "Resolving a camera rig twice changed exclusions");
        check(d3scene::queryView(*rig_frame,rig_view).surfaces.empty(),"Camera rig left its followed object's effects visible");
        check(d3scene::queryView(*rig_frame,view()).surfaces.size() == 1,"Camera rig exclusions leaked into another view");
        rig_data.objects.back().camera_hidden_objects = {93};
        bool invalid_rig = false;
        try { d3scene::Presentation::freeze(rig_data); } catch (const std::invalid_argument&) { invalid_rig = true; }
        check(invalid_rig,"Camera rig accepted a stale followed-object allocation");
        rig_view.camera_owner = 93; invalid_rig = false;
        try { d3scene::resolveView(*rig_frame,rig_view); } catch (const std::invalid_argument&) { invalid_rig = true; }
        check(invalid_rig,"Camera rig accepted a stale camera allocation");
    }
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
        auto volume = soft;
        volume.raster_domain = d3scene::RasterDomain::SoftVolume;
        volume.surface.unlit = true;
        volume.surface.blend = d3scene::BlendMode::Alpha;
        auto volume_data = scheduled; volume_data.meshes = {volume};
        auto volume_frame = d3scene::Presentation::freeze(volume_data);
        auto volume_view = d3scene::queryView(*volume_frame,view());
        check(d3scene::buildViewPasses(*volume_frame,volume_view).back().kind == d3scene::ViewPassKind::LateWorld,
            "Soft volume changed the default raster schedule");
        check(d3scene::buildViewPasses(*volume_frame,volume_view,{false,false,true}).back().kind == d3scene::ViewPassKind::SoftVolumes,
            "Explicit soft volume was not scheduled into its layer");
        volume_data.meshes[0].surface.blend = d3scene::BlendMode::Multiply;
        volume_frame = d3scene::Presentation::freeze(volume_data);
        check(d3scene::buildViewPasses(*volume_frame,d3scene::queryView(*volume_frame,view()),{false,false,true}).back().kind == d3scene::ViewPassKind::LateWorld,
            "Multiplicative material entered scalar-coverage particle layer");
        auto glass = volume; glass.raster_domain = d3scene::RasterDomain::Surface;
        auto depth_writer = glass; depth_writer.surface.depth = d3scene::DepthMode::ReadWrite;
        auto additive = volume; additive.surface.blend = d3scene::BlendMode::Additive;
        volume_data.meshes = {volume,volume,additive,glass,volume,depth_writer,volume};
        volume_frame = d3scene::Presentation::freeze(volume_data);
        const auto grouped = d3scene::buildViewPasses(*volume_frame,d3scene::queryView(*volume_frame,view()),{false,false,true});
        check(grouped.size() == 6 && grouped[1].kind == d3scene::ViewPassKind::SoftVolumes &&
            grouped[1].begin == 0 && grouped[1].end == 2 &&
            grouped[2].kind == d3scene::ViewPassKind::SoftVolumes && grouped[2].begin == 2 && grouped[2].end == 3 &&
            grouped[3].kind == d3scene::ViewPassKind::LateWorld && grouped[3].begin == 3 && grouped[3].end == 4 &&
            grouped[4].kind == d3scene::ViewPassKind::SoftVolumes && grouped[4].begin == 4 && grouped[4].end == 5 &&
            grouped[5].kind == d3scene::ViewPassKind::LateWorld && grouped[5].begin == 5 && grouped[5].end == 7,
            "Particle grouping crossed blend saturation, transparency or depth-writing barriers");
    }
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
    check(d3scene::buildViewPasses(*empty_frame,d3scene::queryView(*empty_frame,view())).empty(),
        "Empty view without products scheduled raster passes");
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
    const auto prepass = std::find_if(radiance_passes.begin(),radiance_passes.end(),[](const auto& pass) {
        return pass.kind == d3scene::ViewPassKind::DepthSurface;
    });
    check(prepass != radiance_passes.end() && std::distance(prepass,radiance_passes.end()) >= 4 &&
        (prepass+1)->kind == d3scene::ViewPassKind::ExportOpaqueScene &&
        (prepass+2)->kind == d3scene::ViewPassKind::EvaluateWorldOcclusion &&
        (prepass+3)->kind == d3scene::ViewPassKind::OpaqueWorld &&
        radiance_passes.back().kind == d3scene::ViewPassKind::LateWorld,
        "Occlusion must consume surface depth before lighting, not revisit completed translucent color");
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
    tick.sequence = 2; tick.temporal_predecessor = 1;
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
    auto pause_tick = tick; pause_tick.temporal_predecessor = tick.sequence; pause_tick.sequence++; pause_tick.paused = true;
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
    auto interrupted_tick = pause_tick; interrupted_tick.temporal_predecessor = pause_tick.sequence; interrupted_tick.sequence++;
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
        prior_tick.temporal_predecessor = prior_tick.sequence; ++prior_tick.sequence;
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
    tick.sequence = 3; tick.temporal_predecessor = 2;
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
    // Independent publication holes preserve the explicit world predecessor.
    tick.sequence = 40;
    check(d3scene::retainViewImageStorage(2,tick) && d3scene::retainViewImageStorage(40,tick) &&
        d3scene::retainViewImageStorage(41,tick) &&
        !d3scene::retainViewImageStorage(1,tick) && !d3scene::retainViewImageStorage(39,tick),
        "View-image storage aging inferred world advancement from publication-ID gaps");
    auto export_tick = tick; export_tick.temporal_predecessor = 0;
    check(d3scene::retainViewImageStorage(2,export_tick),
        "Independent export retired the world's preceding view-image storage");
    const auto after_thumbnail = histories.prepare(tick,main_view,d3scene::prepareCamera(main_view));
    check(after_thumbnail.previous_valid && after_thumbnail.previous.view_projection == second.current.view_projection,
        "Independent UI/thumbnail publication reset world history");
    auto paused_hole = pause_tick; paused_hole.sequence = 41;
    auto paused_hole_camera = pause_camera; paused_hole_camera.sequence = 41;
    check(d3scene::prepareImageMotion(paused_hole,paused_hole_camera,exposure).delta_time == .02f,
        "Independent UI/thumbnail publication discarded a paused exposure");
    tick.sequence = 4; tick.temporal_predecessor = 3;
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
    {
        d3scene::PreparedMesh particle;
        particle.alignment = d3scene::MeshAlignment::Velocity;
        particle.world.position = {0,0,10}; particle.world.up = {0,2,0};
        particle.billboard_motion.velocity = {0,-10,0}; particle.billboard_motion.speed_stretch = .1f;
        particle.billboard_motion.maximum_stretch = 1; particle.billboard_motion.proximity_range = 20;
        auto request = view();
        const auto stretched = d3scene::resolveMeshTransform(particle,d3scene::prepareCamera(request));
        check(std::abs(stretched.up.y-2.5f) < .00001f && std::abs(stretched.right.x-1) < .00001f,
            "Motion billboard lost its per-view speed/proximity stretch");
        request.camera_velocity = {0,-10,0};
        const auto matched = d3scene::resolveMeshTransform(particle,d3scene::prepareCamera(request));
        check(std::abs(matched.up.y-2) < .00001f && std::abs(matched.right.x-1) < .00001f,
            "Motion billboard inherited a different view's camera velocity");
        check(particle.world.up.y == 2 && request.camera_velocity.y == -10,
            "Motion billboard resolution mutated its publication/request");
        request.camera_velocity.x = std::numeric_limits<float>::infinity();
        bool rejected = false;
        try { d3scene::prepareCamera(request); } catch (const std::invalid_argument&) { rejected = true; }
        check(rejected,"Nonfinite physical camera motion entered view preparation");
        d3scene::SceneAssets motion_assets;
        particle.asset = motion_assets.quadMesh(); particle.billboard_motion.speed_stretch = -1;
        d3scene::FrameData invalid; invalid.sequence = 1; invalid.scene_lifetime = motion_assets.lifetime(); invalid.meshes.push_back(particle);
        rejected = false;
        try { d3scene::Presentation::freeze(invalid); } catch (const std::invalid_argument&) { rejected = true; }
        check(rejected,"Negative motion-billboard stretch entered an immutable publication");
    }
    return Checks;
}
