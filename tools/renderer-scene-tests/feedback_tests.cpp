// Descent 3MASTERED - GPL-3.0-or-later
#include "../../Descent3/SceneFeedback.h"
#include "../../renderer/SceneTerrain.h"
#include "../../renderer/SceneGeometry.h"
#include <stdexcept>
#include <thread>
int runFeedbackTests() {
    using namespace d3scene;
    int checks = 0;
    const auto check = [&](bool value) { ++checks; if (!value) throw std::runtime_error("Game observation transaction"); };
    const auto rejects = [&](const auto& action) {
        bool rejected = false;
        try { action(); } catch (const std::logic_error&) { rejected = true; }
        check(rejected);
    };
    SceneAssets assets;
    FrameData data; data.sequence = 31; data.scene_lifetime = assets.lifetime();
    RoomState room; room.number = 7; room.asset = assets.publishRoom(7,{});
    data.rooms.push_back(room);
    room.number = 9; room.asset = assets.publishRoom(9,{}); data.rooms.push_back(room);
    ObjectState object; object.handle = 100; data.objects.push_back(object);
    object.handle = 200; data.objects.push_back(object);
    const auto frame = Presentation::freeze(data);
    SceneFeedbackTransaction transaction(*frame);
    VisibilityReport a;
    a.frame_sequence = data.sequence; a.scene_lifetime = data.scene_lifetime; a.view_history_key = 1;
    a.feedback = Automap | ObjectVisibility; a.rooms = {7}; a.objects = {200,100,200};
    transaction.add(a); transaction.add(a);
    auto b = a; b.view_history_key = 2; b.feedback = EffectEligibility | ObjectVisibility;
    b.rooms = {9}; b.objects = {100}; b.terrain = true;
    transaction.add(b);
    auto invalid = a; invalid.frame_sequence++;
    rejects([&] { transaction.add(invalid); });
    invalid = a; invalid.scene_lifetime++;
    rejects([&] { transaction.add(invalid); });
    invalid = a; invalid.objects = {100};
    rejects([&] { transaction.add(invalid); }); // conflicting replay
    invalid = a; invalid.view_history_key = 3; invalid.rooms = {9,77};
    rejects([&] { transaction.add(invalid); }); // must not partially mark 9 on the map
    invalid = a; invalid.view_history_key = 3; invalid.objects = {201};
    rejects([&] { transaction.add(invalid); }); // stale allocation identity
    invalid = a; invalid.view_history_key = 3; invalid.feedback = NoFeedback;
    rejects([&] { transaction.add(invalid); });
    invalid.rooms.clear(); invalid.objects.clear(); transaction.add(invalid);
    const auto& result = transaction.seal();
    check(result.frame_sequence == 31 && result.scene_lifetime == data.scene_lifetime);
    check(result.automap_rooms == std::vector<int32_t>{7});
    check(result.effect_rooms == std::vector<int32_t>{9});
    check(result.objects == std::vector<uint32_t>({200,100}));
    check(result.terrain);
    rejects([&] { transaction.add(a); });
    rejects([&] { transaction.seal(); });
    SceneFeedbackTransaction next(*frame);
    check(next.seal().objects.empty()); // no cross-frame carryover
    {
        VisibilityReport held_report; held_report.frame_sequence = 1; held_report.rooms = {7}; held_report.objects = {200};
        auto independent = held_report; independent.rooms[0] = 9;
        check(held_report.rooms[0] == 7);
        VisibilityReport receiver; receiver.rooms.reserve(71);
        const auto spare_capacity = receiver.rooms.capacity();
        {
            VisibilityReport incoming; incoming.rooms = {9}; incoming.frame_sequence = 99;
            receiver = std::move(incoming);
        }
        VisibilityReport recycled;
        check(recycled.rooms.empty() && recycled.rooms.capacity() == spare_capacity);
        check(receiver.rooms == std::vector<int32_t>{9} && receiver.frame_sequence == 99);
        std::thread foreign_report([report = held_report] {}); foreign_report.join();
        check(held_report.objects == std::vector<uint32_t>{200});
        auto owned_frame = Presentation::freeze(data);
        auto owned = std::make_unique<SceneFeedbackTransaction>(*owned_frame); owned_frame.reset();
        owned->add(a);
        check(owned->seal().objects == std::vector<uint32_t>({200,100}));
        std::thread foreign_transaction([value = std::move(owned)] {}); foreign_transaction.join();
        auto replacement_data = data; replacement_data.sequence++;
        replacement_data.objects[0].handle = 201;
        const auto replacement_frame = Presentation::freeze(replacement_data);
        { SceneFeedbackTransaction retired(*frame); retired.add(a); check(retired.seal().objects.size() == 2); }
        SceneFeedbackTransaction replacement(*replacement_frame);
        auto replacement_report = a; replacement_report.frame_sequence++;
        replacement_report.objects = {201}; replacement.add(replacement_report);
        check(replacement.seal().objects == std::vector<uint32_t>{201});
    }
    View view; view.history_key = 1; view.room = 7;
    view.viewport = view.projection_anchor = {0,0,800,600};
    check(InspectSceneObservations(*frame,view).report.objects.empty());
    view.feedback = Automap | ObjectVisibility | EffectEligibility;
    rejects([&] { InspectSceneObservations(*frame,view); });
    data.observation_inputs_prepared = true;
    for (auto& item : data.objects) {
        item.room = 7; item.observation_eligible = true;
        item.observation_front_to_back = true;
        item.current.position = {0,0,item.handle == 100 ? 5.f : .1f};
        item.observation_bounds = {item.current.position,item.current.position};
    }
    auto observed_frame = Presentation::freeze(data);
    {
        auto region_data = data;
        region_data.view_regions.push_back({42,{12,34,56,78}});
        const auto region_frame = Presentation::freeze(region_data);
        region_data.view_regions[0].rectangle.width = 0;
        check(region_frame->data().view_regions[0].rectangle.width == 56);
        rejects([&] { Presentation::freeze(region_data); });
        region_data.view_regions = {{42,{0,0,10,10}},{42,{0,0,20,20}}};
        rejects([&] { Presentation::freeze(region_data); });
    }
    auto effect_view = view;
    effect_view.feedback = NoFeedback;
    auto effects = CollectSceneEffectEligibility(prepareView(*observed_frame,effect_view));
    check(effects.feedback == EffectEligibility);
    check(effects.rooms == std::vector<int32_t>{7});
    check(effects.objects.empty() && !effects.terrain);
    check(effects.frame_sequence == data.sequence && effects.scene_lifetime == data.scene_lifetime);
    effect_view.kind = ViewKind::Auxiliary;
    check(CollectSceneEffectEligibility(prepareView(*observed_frame,effect_view)).rooms == effects.rooms);
    effect_view.kind = ViewKind::Preview;
    check(CollectSceneEffectEligibility(prepareView(*observed_frame,effect_view)).feedback == NoFeedback);
    effect_view.kind = ViewKind::Main; effect_view.portal_visibility = false;
    check(CollectSceneEffectEligibility(prepareView(*observed_frame,effect_view)).feedback == NoFeedback);
    auto candidate = InspectSceneObservations(*observed_frame,view);
    check(candidate.report.rooms == std::vector<int32_t>{7}); // disconnected room not explored
    check(candidate.report.objects == std::vector<uint32_t>({200,100})); // includes before near plane
    const auto prepared_observation = prepareView(*observed_frame,view);
    const auto shared_observation = InspectSceneObservations(prepared_observation,view.feedback);
    check(shared_observation.report.rooms == candidate.report.rooms &&
        shared_observation.report.objects == candidate.report.objects);
    check(InspectSceneObservations(prepared_observation,NoFeedback).report.objects.empty());
    rejects([&] { InspectSceneObservations(prepared_observation,128); });
    {
        SceneFeedbackTransaction live(*observed_frame);
        check(live.observe(prepared_observation));
        check(live.observe(prepared_observation)); // exact replay is observation-only
        auto shifted_main = view; shifted_main.camera.position.x += .01f;
        rejects([&] { live.observe(prepareView(*observed_frame,shifted_main)); });
        auto cropped_main = view; cropped_main.viewport.x += 1;
        rejects([&] { live.observe(prepareView(*observed_frame,cropped_main)); });
        auto auxiliary = view; auxiliary.kind = ViewKind::Auxiliary; auxiliary.history_key = 2;
        auxiliary.exterior_origin = true; auxiliary.room = -2;
        check(live.observe(prepareView(*observed_frame,auxiliary)));
        auto preview = view; preview.kind = ViewKind::Preview; preview.history_key = 3;
        check(!live.observe(prepareView(*observed_frame,preview)));
        auto independent = view; independent.kind = ViewKind::SaveThumbnail; independent.history_key = 4;
        check(!live.observe(prepareView(*observed_frame,independent)));
        const auto& accepted = live.seal();
        check(accepted.main_view_present && !accepted.main_exterior_visible && accepted.terrain);
        check(accepted.main_viewport.x == view.viewport.x && accepted.main_viewport.width == view.viewport.width &&
            accepted.main_camera_room == view.room &&
            accepted.main_camera.projection.x_scale == prepared_observation.visibility().camera.projection.x_scale &&
            accepted.main_camera.transform.position.x == prepared_observation.visibility().camera.transform.position.x &&
            accepted.main_excluded_objects == prepared_observation.view().excluded_objects);
        check(accepted.automap_rooms == std::vector<int32_t>{7});
        check(accepted.objects == std::vector<uint32_t>({200,100}));
    }
    {
        auto zoom_data = data;
        auto& edge_object = zoom_data.objects[0];
        edge_object.current.position = {0,-48.8f,72.3f}; edge_object.observation_radius = 7;
        edge_object.observation_bounds = {edge_object.current.position,edge_object.current.position};
        const auto zoom_frame = Presentation::freeze(zoom_data);
        auto narrow = view; narrow.kind = ViewKind::Auxiliary;
        narrow.viewport = narrow.projection_anchor = {0,0,142,142}; narrow.zoom = .726f;
        auto wide = narrow; wide.kind = ViewKind::Main; wide.zoom = 1;
        const auto narrow_view = prepareView(*zoom_frame,narrow);
        const auto wide_view = prepareView(*zoom_frame,wide);
        check(InspectSceneObservations(narrow_view,view.feedback).report.objects == std::vector<uint32_t>{200});
        check(InspectSceneObservations(wide_view,view.feedback).report.objects == std::vector<uint32_t>({200,100}));
        check(InspectSceneObservations(narrow_view,view.feedback).report.objects == std::vector<uint32_t>{200});
    }
    check(!candidate.exterior_pending && !candidate.mirrors_pending && !candidate.report.terrain);
    {
        auto mirror_data = data;
        MeshGeometry shell; shell.vertices = {{-1,-1,0},{1,1,6}};
        mirror_data.rooms[0].asset = assets.publishRoom(7,shell);
        const auto mirror_frame = Presentation::freeze(mirror_data);
        auto mirror_view = view;
        mirror_view.kind = ViewKind::Mirror; mirror_view.feedback = NoFeedback;
        mirror_view.has_clip_plane = true; mirror_view.clip_plane = {{0,0,1},-3};
        mirror_view.fog_path_origin = FogPathOrigin::ClipPlane;
        mirror_view.reflections = ReflectionPolicy::SurfaceFilm;
        const auto reflected = prepareView(*mirror_frame,mirror_view);
        const auto reflected_report = InspectSceneObservations(reflected,ObjectVisibility | EffectEligibility);
        check(reflected_report.report.objects == std::vector<uint32_t>{100});
        check(reflected_report.report.rooms == std::vector<int32_t>{7});
        rejects([&] { InspectSceneObservations(reflected,Automap); });
        SceneFeedbackTransaction reflected_game(*mirror_frame);
        check(reflected_game.observe(reflected));
        const auto& reflected_game_result = reflected_game.seal();
        check(reflected_game_result.automap_rooms.empty() && !reflected_game_result.main_view_present);
        check(reflected_game_result.effect_rooms == std::vector<int32_t>{7} &&
            reflected_game_result.objects == std::vector<uint32_t>{100});
        mirror_view.coverage = {600,0,200,600};
        check(InspectSceneObservations(prepareView(*mirror_frame,mirror_view),ObjectVisibility).report.objects.empty());
        auto equal_depth = data;
        equal_depth.objects[0].current = equal_depth.objects[1].current;
        equal_depth.objects[0].observation_bounds = equal_depth.objects[1].observation_bounds;
        std::swap(equal_depth.objects[0],equal_depth.objects[1]);
        const auto equal_frame = Presentation::freeze(equal_depth);
        check(InspectSceneObservations(prepareView(*equal_frame,view),ObjectVisibility).report.objects == std::vector<uint32_t>({200,100}));
    }
    view.excluded_objects = {200};
    check(InspectSceneObservations(*observed_frame,view).report.objects == std::vector<uint32_t>{100});
    view.excluded_objects.clear();
    data.objects[0].current.position.x = 100;
    data.objects[0].observation_bounds = {data.objects[0].current.position,data.objects[0].current.position};
    observed_frame = Presentation::freeze(data);
    check(InspectSceneObservations(*observed_frame,view).report.objects == std::vector<uint32_t>{200});
    data.objects[0].observation_unbounded = true;
    observed_frame = Presentation::freeze(data);
    check(InspectSceneObservations(*observed_frame,view).report.objects.size() == 2);
    data.objects[0].observation_bounds.minimum.x = 101;
    rejects([&] { Presentation::freeze(data); });
    view.room = 99;
    rejects([&] { InspectSceneObservations(*observed_frame,view); });
    view.room = 7; view.kind = ViewKind::Mirror;
    rejects([&] { InspectSceneObservations(*observed_frame,view); });
    view.kind = ViewKind::Main;
    auto portal_data = observed_frame->data();
    MeshGeometry aperture;
    aperture.vertices = {{-.2f,-.2f,2},{.2f,-.2f,2},{.2f,.2f,2},{-.2f,.2f,2}};
    MeshPolygon polygon; polygon.normal = {0,0,-1}; polygon.corners = {{0},{1},{2},{3}};
    aperture.polygons.push_back(polygon);
    portal_data.rooms[0].asset = assets.publishRoom(7,aperture);
    portal_data.rooms[0].surfaces.resize(1);
    portal_data.rooms[0].portals.push_back({0,9});
    auto& through_portal = portal_data.objects[0];
    through_portal.room = 9; through_portal.observation_unbounded = false;
    through_portal.current.position = {3,0,5};
    through_portal.observation_bounds = {through_portal.current.position,through_portal.current.position};
    auto portal_frame = Presentation::freeze(portal_data);
    candidate = InspectSceneObservations(*portal_frame,view);
    check(candidate.report.rooms == std::vector<int32_t>({7,9}));
    check(candidate.report.objects == std::vector<uint32_t>{200}); // sphere visible, portal AABB rejected
    const auto narrow_prepared = prepareView(*portal_frame,view);
    const auto narrow_report = InspectSceneObservations(narrow_prepared,view.feedback);
    check(narrow_report.report.objects == candidate.report.objects && narrow_report.report.rooms == candidate.report.rooms);
    auto other_view = view; other_view.kind = ViewKind::Auxiliary; other_view.history_key = 2;
    other_view.camera.position.x = 100;
    const auto other_prepared = prepareView(*portal_frame,other_view);
    const auto other_report = InspectSceneObservations(other_prepared,other_view.feedback);
    check(other_report.report.objects == InspectSceneObservations(*portal_frame,other_view).report.objects);
    check(InspectSceneObservations(narrow_prepared,view.feedback).report.objects == narrow_report.report.objects);
    check(other_prepared.visibility().spatial.rooms.size() == portal_frame->data().rooms.size());
    portal_data.rooms[1].observation_unclipped = true;
    portal_frame = Presentation::freeze(portal_data);
    check(InspectSceneObservations(*portal_frame,view).report.objects.size() == 2);
    portal_data.rooms[1].observation_unclipped = false;
    portal_data.objects[0].observation_bounds.minimum.z = -.1f;
    portal_frame = Presentation::freeze(portal_data);
    check(InspectSceneObservations(*portal_frame,view).report.objects.size() == 2); // eye-crossing bounds
    // A real mirror needs geometry; reuse the owned aperture for this coverage fixture.
    portal_data.rooms[1].asset = assets.publishRoom(9,aperture);
    portal_data.rooms[1].surfaces.resize(1);
    portal_data.rooms[1].mirrors = compilePlanarSurfaces(aperture,{0});
    portal_frame = Presentation::freeze(portal_data);
    check(InspectSceneObservations(*portal_frame,view).mirrors_pending);
    auto rig_data = observed_frame->data();
    rig_data.objects[0].camera_hidden_objects = {200};
    const auto rig_frame = Presentation::freeze(rig_data);
    view.camera_owner = 100; view.exclude_camera_rig = true; view.excluded_objects = {200};
    check(InspectSceneObservations(*rig_frame,view).report.objects.empty());
    view.exclude_camera_rig = false; view.excluded_objects.clear();
    check(InspectSceneObservations(*rig_frame,view).report.objects.size() == 2);
    FrameData exterior; exterior.sequence = 40; exterior.scene_lifetime = assets.lifetime();
    exterior.observation_inputs_prepared = true; exterior.terrain.observation_distance = 30;
    ObjectState outside; outside.handle = 300; outside.room = -2; outside.observation_eligible = true;
    outside.current.position = {1,0,11}; outside.observation_bounds = {outside.current.position,outside.current.position};
    exterior.objects.push_back(outside);
    View outdoor; outdoor.history_key = 4; outdoor.viewport = outdoor.projection_anchor = {0,0,800,600};
    outdoor.camera.position = {1,0,1}; outdoor.exterior_origin = true; outdoor.feedback = ObjectVisibility | EffectEligibility;
    TerrainOcclusionData table; table.width = table.depth = 2; table.sector_size = 10;
    table.visibility = {0,0,0,0}; exterior.terrain.occlusion = assets.publishTerrainOcclusion(table);
    auto outside_frame = Presentation::freeze(exterior);
    auto outside_report = InspectSceneObservations(*outside_frame,outdoor);
    check(outside_report.report.objects.empty() && outside_report.report.terrain && outside_report.exterior_pending);
    table.visibility[0] = 4; exterior.terrain.occlusion = assets.publishTerrainOcclusion(table);
    outside_frame = Presentation::freeze(exterior);
    check(InspectSceneObservations(*outside_frame,outdoor).report.objects == std::vector<uint32_t>{300});
    exterior.terrain.observation_distance = 5; outside_frame = Presentation::freeze(exterior);
    check(InspectSceneObservations(*outside_frame,outdoor).report.objects.empty());
    exterior.objects[0].observation_unbounded = true; outside_frame = Presentation::freeze(exterior);
    check(InspectSceneObservations(*outside_frame,outdoor).report.objects.size() == 1);
    table.visibility[0] = 0; exterior.terrain.occlusion = assets.publishTerrainOcclusion(table);
    outside_frame = Presentation::freeze(exterior);
    check(InspectSceneObservations(*outside_frame,outdoor).report.objects.empty()); // electrical admission still honors occlusion
    return checks;
}
