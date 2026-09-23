// Descent 3MASTERED - GPL-3.0-or-later
#include "../../Descent3/SceneFeedback.h"
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
    auto candidate = InspectSceneObservations(*observed_frame,view);
    check(candidate.report.rooms == std::vector<int32_t>{7}); // disconnected room not explored
    check(candidate.report.objects == std::vector<uint32_t>({200,100})); // includes before near plane
    check(!candidate.exterior_pending && !candidate.mirrors_pending && !candidate.report.terrain);
    view.exclude_object = true; view.excluded_object = 200;
    check(InspectSceneObservations(*observed_frame,view).report.objects == std::vector<uint32_t>{100});
    view.exclude_object = false;
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
    portal_data.rooms[1].observation_unclipped = true;
    portal_frame = Presentation::freeze(portal_data);
    check(InspectSceneObservations(*portal_frame,view).report.objects.size() == 2);
    portal_data.rooms[1].observation_unclipped = false;
    portal_data.objects[0].observation_bounds.minimum.z = -.1f;
    portal_frame = Presentation::freeze(portal_data);
    check(InspectSceneObservations(*portal_frame,view).report.objects.size() == 2); // eye-crossing bounds
    portal_data.rooms[1].mirror_polygon = 0;
    // A real mirror needs geometry; reuse the owned aperture for this coverage fixture.
    portal_data.rooms[1].asset = assets.publishRoom(9,aperture);
    portal_data.rooms[1].surfaces.resize(1);
    portal_frame = Presentation::freeze(portal_data);
    check(InspectSceneObservations(*portal_frame,view).mirrors_pending);
    return checks;
}
