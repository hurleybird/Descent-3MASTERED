// Descent 3MASTERED - GPL-3.0-or-later
#include "../../Descent3/SceneFeedback.h"
#include "../../renderer/SceneTerrain.h"
#include "../../renderer/SceneGeometry.h"
#include <stdexcept>
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
    check(!candidate.exterior_pending && !candidate.mirrors_pending && !candidate.report.terrain);
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
