// Descent 3MASTERED - GPL-3.0-or-later
#include "../../renderer/Scene.h"
#include "../../Descent3/SceneExtraction.h"
#include "room.h"
#include "object.h"
#include <cstdio>
#include <limits>
#include <stdexcept>

// Minimal simulation fixture, not a renderer mock. Linking the real extractor
// against only these globals also catches accidental dependencies on draw/RNG
// functions. No GL context, game runtime, or save files are required.
room Rooms[MAX_ROOMS + MAX_PALETTE_ROOMS]{};
object Objects[MAX_OBJECTS]{};
int Highest_object_index = -1;

namespace {
int Checks = 0;
void check(bool condition, const char* message) {
    ++Checks;
    if (!condition) throw std::runtime_error(message);
}
template<class F> void rejects(F operation, const char* message) {
    bool rejected = false;
    try { operation(); } catch (const std::logic_error&) { rejected = true; }
    check(rejected, message);
}
d3scene::MeshGeometry triangle() {
    d3scene::MeshGeometry geometry;
    geometry.vertices = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    d3scene::MeshPolygon polygon;
    polygon.normal = {0, 0, 1};
    polygon.corners = {{0, 0, 0, 0, 0, 255}, {1, 1, 0, 1, 0, 255}, {2, 0, 1, 0, 1, 255}};
    geometry.polygons.push_back(polygon);
    return geometry;
}
void assetTests() {
    d3scene::SceneAssets assets;
    const auto a = assets.publishRoom(3, triangle());
    const auto unchanged = assets.publishRoom(3, triangle());
    check(a == unchanged, "unchanged geometry allocated a new revision");
    auto changed = triangle(); changed.vertices[0].x = 9;
    const auto b = assets.publishRoom(3, changed);
    check(a->id() != b->id(), "changed geometry reused asset identity");
    check(a->geometry().vertices[0].x == 0, "publishing mutated existing revision");
    changed.vertices[0].x = 14;
    check(b->geometry().vertices[0].x == 9, "asset aliases caller geometry");
    assets.removeRoom(3);
    check(!assets.room(3), "removed asset is still current");
    const auto c = assets.publishRoom(3, triangle());
    check(c->id() != a->id(), "recycled room reused revision identity");
    assets.reset();
    const auto d = assets.publishRoom(3, triangle());
    check(d->id().lifetime != c->id().lifetime, "level reset reused scene lifetime");
    check(a->geometry().vertices.size() == 3, "level reset invalidated frozen asset");
    auto bad = triangle(); bad.polygons[0].corners[0].vertex = 99;
    rejects([&] { assets.publishRoom(3, bad); }, "invalid index accepted");
    check(assets.room(3) == d, "failed publication changed current asset");
    bad = triangle(); bad.vertices[0].x = std::numeric_limits<float>::quiet_NaN();
    rejects([&] { assets.publishRoom(3, bad); }, "NaN geometry accepted");
    bad = triangle(); bad.polygons[0].corners.resize(2);
    rejects([&] { assets.publishRoom(3, bad); }, "degenerate polygon accepted");
}
void frameTests() {
    d3scene::SceneAssets assets;
    d3scene::FrameData data;
    data.sequence = 1; data.scene_lifetime = assets.lifetime();
    d3scene::RoomState room;
    room.number = 0; room.asset = assets.publishRoom(0, triangle()); room.surfaces.resize(1);
    data.rooms.push_back(room);
    d3scene::ObjectState object;
    object.handle = 42; data.objects.push_back(object);
    const auto frozen = d3scene::Presentation::freeze(data);
    data.rooms[0].surfaces[0].texture = 99;
    data.objects[0].current.position.x = 22;
    check(frozen->data().rooms[0].surfaces[0].texture == -1, "frame aliases surface bindings");
    check(frozen->data().objects[0].current.position.x == 0, "frame aliases transforms");
    rejects([&] { frozen->requireExecutable(); }, "incomplete domains silently executable");
    data.rooms.push_back(room);
    rejects([&] { d3scene::Presentation::freeze(data); }, "duplicate rooms accepted");
    data.rooms.pop_back(); data.objects.push_back(object);
    rejects([&] { d3scene::Presentation::freeze(data); }, "duplicate object lifetimes accepted");
    data.objects.pop_back(); data.rooms[0].surfaces.clear();
    rejects([&] { d3scene::Presentation::freeze(data); }, "missing surface bindings accepted");
    data.rooms[0].surfaces.resize(1); assets.reset(); data.scene_lifetime = assets.lifetime();
    rejects([&] { d3scene::Presentation::freeze(data); }, "cross-level asset reference accepted");
    d3scene::View view;
    rejects([&] { d3scene::validateView(view); }, "anonymous zero-sized view accepted");
    view.history_key = 1; view.viewport = {0, 0, 1280, 720}; view.projection_anchor = view.viewport;
    d3scene::validateView(view);
    view.zoom = 0;
    rejects([&] { d3scene::validateView(view); }, "zero zoom accepted");
}
void extractionTests() {
    ResetGamePresentationScene();
    vector vertices[3] = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    short indices[3] = {0, 1, 2};
    roomUVL uv[3] = {{0, 0, 0, 0, 255}, {1, 0, 1, 0, 255}, {0, 1, 0, 1, 255}};
    face surface{};
    surface.num_verts = 3; surface.face_verts = indices; surface.face_uvls = uv;
    surface.normal = {0, 0, 1}; surface.portal_num = -1;
    auto& source = Rooms[0];
    source.used = 1; source.num_verts = 3; source.num_faces = 1;
    source.verts = vertices; source.faces = &surface;
    source.flags = RF_MIRROR_VISIBLE;
    surface.flags = FF_VISIBLE | FF_NOT_FACING | FF_TOUCHED | FF_SPEC_INVISIBLE;
    Highest_object_index = 0;
    Objects[0].type = OBJ_ROBOT; Objects[0].handle = 2048; Objects[0].size = 1;
    Objects[0].orient.rvec = {1, 0, 0}; Objects[0].orient.uvec = {0, 1, 0}; Objects[0].orient.fvec = {0, 0, 1};
    const auto a = ExtractGamePresentation(1, 0.016f, false);
    check(a->data().rooms.size() == 1 && a->data().objects.size() == 1, "scene extraction omitted source content");
    check(!a->data().objects[0].has_previous, "new object has fabricated history");
    check(a->data().rooms[0].authored_flags == 0 && a->data().rooms[0].surfaces[0].authored_flags == 0,
        "camera traversal marks crossed scene boundary");
    check(source.flags == RF_MIRROR_VISIBLE && surface.flags == (FF_VISIBLE | FF_NOT_FACING | FF_TOUCHED | FF_SPEC_INVISIBLE),
        "extraction mutated source visibility");
    Objects[0].pos.x = 5;
    const auto b = ExtractGamePresentation(2, 0.016f, false);
    check(b->data().rooms[0].asset == a->data().rooms[0].asset, "unchanged extraction copied geometry");
    check(b->data().objects[0].has_previous && b->data().objects[0].previous.position.x == 0 &&
        b->data().objects[0].current.position.x == 5, "presentation history not advanced correctly");
    const auto view = MakeGameSceneView(d3scene::ViewKind::Auxiliary, 2, &Objects[0], Objects[0].pos,
        0, Objects[0].orient, 1, true, {0, 0, 320, 180}, {0, 0, 320, 180});
    check(view.camera.forward.z == -1 && Objects[0].orient.fvec.z == 1, "rear view changed simulation pose");
    check(!view.dynamic_lighting && view.excluded_object == 2048, "auxiliary view lost explicit policy");
    check(b->data().sequence == a->data().sequence + 1, "view request advanced presentation history");
    Objects[0].pos.x = 77;
    const auto thumbnail = ExtractGamePresentation(2.5, 0, false, false);
    check(thumbnail->data().objects[0].current.position.x == 77, "thumbnail did not capture its requested state");
    Objects[0].pos.x = 8;
    const auto after_thumbnail = ExtractGamePresentation(2.6, 0, false);
    check(after_thumbnail->data().objects[0].previous.position.x == 5, "thumbnail advanced gameplay presentation history");
    Objects[0].handle += HANDLE_COUNT_INCREMENT;
    vertices[0].x = 7; surface.tmap = 8;
    const auto c = ExtractGamePresentation(3, 0.016f, false);
    check(!c->data().objects[0].has_previous, "recycled object slot inherited prior occupant history");
    check(c->data().rooms[0].asset->id() != b->data().rooms[0].asset->id(), "geometry change missed");
    check(a->data().rooms[0].asset->geometry().vertices[0].x == 0, "live geometry changed old frame");
    surface.tmap = 9;
    const auto d = ExtractGamePresentation(4, 0.016f, false);
    check(d->data().rooms[0].asset == c->data().rooms[0].asset, "material-only update copied geometry");
    check(c->data().rooms[0].surfaces[0].texture == 8 && d->data().rooms[0].surfaces[0].texture == 9,
        "material change not frame-local");
    const auto rewind = ExtractGamePresentation(0, 0, true);
    check(!rewind->data().objects[0].has_previous, "clock rewind retained temporal history");
    ResetGamePresentationScene();
    const auto e = ExtractGamePresentation(5, 0, true);
    check(e->data().scene_lifetime != a->data().scene_lifetime && !e->data().objects[0].has_previous,
        "level reset retained object history");
    source = {};
    const auto empty = ExtractGamePresentation(6, 0, true);
    check(empty->data().rooms.empty() && !d3scene::assets().room(0), "removed room retained in current scene");
    check(a->data().rooms[0].asset->geometry().vertices.size() == 3, "unload invalidated held presentation");
    ResetGamePresentationScene();
}
}
int runSceneViewTests();
int runModelPoseTests();
int runModelSceneTests();
int runLightTests();
int runSceneImageTests();
int runProceduralTests();
int main() {
    try {
        assetTests(); frameTests(); extractionTests();
        Checks += runSceneViewTests();
        Checks += runModelPoseTests();
        Checks += runModelSceneTests();
        Checks += runLightTests();
        Checks += runSceneImageTests();
        Checks += runProceduralTests();
        std::printf("PASS: %d renderer scene checks\n", Checks);
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL after %d checks: %s\n", Checks, error.what());
        return 1;
    }
}
