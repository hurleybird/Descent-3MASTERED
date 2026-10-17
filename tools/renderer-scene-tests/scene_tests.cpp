// Descent 3MASTERED - GPL-3.0-or-later
#include "../../renderer/Scene.h"
#include "../../renderer/SceneGeometry.h"
#include "../../Descent3/SceneExtraction.h"
#include "room.h"
#include "object.h"
#include "gametexture.h"
#include <cstdio>
#include <limits>
#include <stdexcept>

// Minimal simulation fixture, not a renderer mock. Linking the real extractor
// against only these globals also catches accidental dependencies on draw/RNG
// functions. No GL context, game runtime, or save files are required.
room Rooms[MAX_ROOMS + MAX_PALETTE_ROOMS]{};
object Objects[MAX_OBJECTS]{};
texture GameTextures[MAX_TEXTURES]{};
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
    // Plane membership is owned by the frozen scene, not recovered from mutable rooms.
    {
        using namespace d3scene;
        auto mesh = unitQuadBatch(3);
        mesh.polygons[1].normal = {0,0,7}; // opposite, non-unit authored normal
        for (size_t i = 8; i < 12; ++i) mesh.vertices[i].z = 5;
        const auto groups = compilePlanarSurfaces(mesh,{2,1,0});
        const auto ordered = compilePlanarSurfaces(mesh,{0,1,2});
        check(groups.size() == 2 && groups[0].polygons == std::vector<uint32_t>({0,1}) &&
            groups[1].polygons == std::vector<uint32_t>({2}), "Planar grouping confused parallel and coplanar faces");
        check(groups[0].polygons == ordered[0].polygons && groups[1].plane.constant == ordered[1].plane.constant &&
            groups[0].plane.normal.z == ordered[0].plane.normal.z, "Planar grouping depends on publication order");
        rejects([&] { compilePlanarSurfaces(mesh,{0,0}); }, "Repeated mirror face accepted");
        rejects([&] { compilePlanarSurfaces(mesh,{3}); }, "Missing mirror face accepted");
        auto bad = mesh; bad.polygons[0].normal = {};
        rejects([&] { compilePlanarSurfaces(bad,{0}); }, "Zero mirror normal accepted");
        bad = mesh; bad.vertices[0].z = 1;
        rejects([&] { compilePlanarSurfaces(bad,{0}); }, "Nonplanar mirror accepted");
        SceneAssets owner;
        FrameData data; data.sequence = 1; data.scene_lifetime = owner.lifetime();
        RoomState room; room.number = 0; room.asset = owner.publishRoom(0,mesh);
        room.surfaces.resize(3); room.mirrors = groups; data.rooms.push_back(room);
        const auto held = Presentation::freeze(data);
        data.rooms[0].mirrors[1].polygons.push_back(0);
        rejects([&] { Presentation::freeze(data); }, "Repeated mirror membership survived freeze");
        data.rooms[0].mirrors = groups;
        for (auto& vertex : mesh.vertices) vertex.z += 2;
        data.rooms[0].asset = owner.publishRoom(0,mesh);
        rejects([&] { Presentation::freeze(data); }, "Stale mirror plane survived geometry replacement");
        data.rooms[0].mirrors = compilePlanarSurfaces(mesh,{0,1,2});
        const auto updated = Presentation::freeze(data);
        check(held->data().rooms[0].asset->geometry().vertices[0].z == 0 &&
            held->data().rooms[0].mirrors[0].plane.constant == 0 &&
            updated->data().rooms[0].mirrors[0].plane.constant == 2,
            "New mirror publication mutated held frame");
    }
    d3scene::SceneAssets assets;
    const auto a = assets.publishRoom(3, triangle());
    const auto quad = assets.quadMesh();
    const auto batch = assets.quadMesh(128);
    check(quad == assets.quadMesh() && batch == assets.quadMesh(128), "canonical primitive was republished");
    check(quad->geometry().vertices.size() == 4 && batch->geometry().vertices.size() == 512,
        "canonical primitive topology is wrong");
    rejects([&] { assets.quadMesh(0); }, "empty canonical batch accepted");
    rejects([&] { assets.quadMesh(129); }, "oversize canonical batch accepted");
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
    check(assets.quadMesh()->id().lifetime != quad->id().lifetime && quad->geometry().vertices.size() == 4,
        "canonical primitive reset lost lifetime isolation");
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
    data.rooms[0].surfaces[0].texture_scale = {-2,3};
    const auto tiled_material = d3scene::Presentation::freeze(data);
    {
        auto dependencies = data;
        auto& surface = dependencies.rooms[0].surfaces[0];
        surface.bitmap = surface.specular_bitmap = surface.lightmap_bitmap = 9;
        const auto first = d3scene::Presentation::freeze(dependencies);
        const auto& images = first->data().rooms[0].surfaces.images();
        check(images.size() == 2 && images[0].handle == 9 && !images[0].baked && images[1].baked,
            "Surface publication did not deduplicate image roles");
        surface.bitmap = surface.specular_bitmap = 17;
        const auto second = d3scene::Presentation::freeze(dependencies);
        check(images[0].handle == 9 && second->data().rooms[0].surfaces.images().back().handle == 17,
            "Publication scratch storage mutated a held dependency table");
    }
    {
        auto retained = tiled_material->data();
        const auto again = d3scene::Presentation::freeze(retained);
        check(&again->data().rooms[0].surfaces.read() == &tiled_material->data().rooms[0].surfaces.read(),
            "Unchanged publication copied retained surface storage");
        retained.rooms[0].surfaces[0].alpha = .5f;
        const auto edited = d3scene::Presentation::freeze(retained);
        check(edited->data().rooms[0].surfaces[0].alpha == .5f && tiled_material->data().rooms[0].surfaces[0].alpha == 1,
            "Editing retained appearance mutated a held frame");
        auto fresh = data;
        auto& escaped = fresh.rooms[0].surfaces[0];
        const auto held = d3scene::Presentation::freeze(fresh);
        escaped.alpha = .25f;
        check(held->data().rooms[0].surfaces[0].alpha == 1,"Builder element reference could mutate a published table");
        auto exclusive = data;
        const auto* storage = exclusive.rooms[0].surfaces.read().data();
        const auto transferred = d3scene::Presentation::publish(std::move(exclusive));
        check(transferred->data().rooms[0].surfaces.read().data() == storage,
            "Exclusive publication copied owned appearance storage");
        check(transferred->roomIndices().at(0) == 0,"Publication did not retain validated room indices");
    }
    check(tiled_material->data().compiled_surface_count == 1 &&
        tiled_material->data().rooms[0].surfaces[0].compiled_index == 0,"Publication did not assign dense surface indices");
    data.compiled_surface_count = 900;
    data.rooms[0].surfaces[0].compiled_index = 700;
    const auto reindexed = d3scene::Presentation::freeze(data);
    check(reindexed->data().compiled_surface_count == 1 && reindexed->data().rooms[0].surface_base == 0 &&
        tiled_material->data().rooms[0].surfaces[0].compiled_index == 0,"Publication trusted source indices or mutated a held frame");
    check(tiled_material->data().rooms[0].surfaces[0].texture_scale.x == -2 &&
        tiled_material->data().rooms[0].surfaces[0].texture_scale.y == 3,"Material lost reflected/tiled UV scale");
    data.rooms[0].surfaces[0].texture_scale.x = std::numeric_limits<float>::infinity();
    rejects([&] { d3scene::Presentation::freeze(data); },"Nonfinite material UV transform accepted");
    data.rooms[0].surfaces[0].texture_scale = {1,1};
    data.rooms[0].surfaces[0].effect_depth_offset = -1;
    rejects([&] { d3scene::Presentation::freeze(data); },"Opaque surface accepted effect-only depth offset");
    data.rooms[0].surfaces[0].unlit = true;
    data.rooms[0].surfaces[0].blend = d3scene::BlendMode::Additive;
    const auto biased_effect = d3scene::Presentation::freeze(data);
    check(!biased_effect->data().rooms[0].surfaces[0].material.writesSolidDepth(),"Biased effect entered opaque depth product");
    data.rooms[0].surfaces[0].layer = d3scene::RenderLayer::Background;
    rejects([&] { d3scene::Presentation::freeze(data); },"Background accepted world effect depth offset");
    data.rooms[0].surfaces[0].layer = d3scene::RenderLayer::World;
    data.rooms[0].surfaces[0].effect_depth_offset = std::numeric_limits<float>::quiet_NaN();
    rejects([&] { d3scene::Presentation::freeze(data); },"Nonfinite effect depth offset accepted");
    data.rooms[0].surfaces[0].effect_depth_offset = 0;
    data.rooms[0].surfaces[0].unlit = false;
    data.rooms[0].surfaces[0].blend = d3scene::BlendMode::Solid;
    d3scene::ReflectionData reflection;
    reflection.sources.push_back({{1,2,3},{.5f,.25f,1}});
    reflection.corner_normals = {{0,0,1},{0,0,1},{0,0,1}};
    const auto original_reflection = assets.publishReflection(7,reflection);
    check(assets.publishReflection(7,reflection) == original_reflection,"Unchanged reflection allocated a revision");
    const auto sparse_reflection = assets.publishReflection(UINT32_MAX,reflection);
    check(sparse_reflection->id().slot == UINT32_MAX && assets.publishReflection(UINT32_MAX,reflection) == sparse_reflection,
        "Sparse environment identity lost publication caching");
    check(assets.publishReflection(7,reflection) == original_reflection,"Sparse reflection replaced another asset identity");
    reflection.sources[0].position.x = 4;
    const auto changed_reflection = assets.publishReflection(7,reflection);
    check(changed_reflection->id() != original_reflection->id(),"Changed reflection reused identity");
    check(original_reflection->data().sources[0].position.x == 1,"Reflection publication mutated retained asset");
    reflection.sources[0].color.x = -1;
    rejects([&] { assets.publishReflection(7,reflection); },"Negative reflection color accepted");
    reflection = changed_reflection->data();
    reflection.corner_normals[0].x = std::numeric_limits<float>::quiet_NaN();
    rejects([&] { assets.publishReflection(7,reflection); },"Nonfinite reflection normal accepted");
    data.rooms[0].surfaces[0].reflection = original_reflection;
    const auto reflected = d3scene::Presentation::freeze(data);
    reflection = changed_reflection->data(); reflection.corner_normals.pop_back();
    data.rooms[0].surfaces[0].reflection = assets.publishReflection(7,reflection);
    rejects([&] { d3scene::Presentation::freeze(data); },"Incomplete reflection normals accepted");
    check(reflected->data().rooms[0].surfaces[0].reflection == original_reflection,"Frame lost immutable reflection data");
    reflection = original_reflection->data(); reflection.field_corners.resize(3);
    reflection.field_corners[0].lobes[0] = {{5,6,7},{.1f,.2f,.3f}};
    const auto field = assets.publishReflection(8,reflection);
    check(assets.publishReflection(8,reflection) == field,"Unchanged field allocated new revision");
    data.rooms[0].surfaces[0].reflection = field;
    const auto field_frame = d3scene::Presentation::freeze(data);
    reflection.field_corners[0].lobes[0].position.x = 99;
    check(assets.publishReflection(8,reflection)->id() != field->id(),"Changed field reused revision");
    check(field_frame->data().rooms[0].surfaces[0].reflection->data().field_corners[0].lobes[0].position.x == 5,
        "Reflection field aliases mutable bake inputs");
    reflection.field_corners.pop_back();
    data.rooms[0].surfaces[0].reflection = assets.publishReflection(8,reflection);
    rejects([&] { d3scene::Presentation::freeze(data); },"Incomplete field corners accepted");
    reflection.field_corners[0].lobes[0].color.y = std::numeric_limits<float>::quiet_NaN();
    rejects([&] { assets.publishReflection(8,reflection); },"Nonfinite field color accepted");
    d3scene::SceneAssets other_assets;
    data.rooms[0].surfaces[0].reflection = other_assets.publishReflection(7,{});
    rejects([&] { d3scene::Presentation::freeze(data); },"Cross-level reflection accepted");
    data.rooms[0].surfaces[0].reflection.reset();
    auto& material = data.rooms[0].surfaces[0];
    material.specular_exponent = 0;
    rejects([&] { d3scene::Presentation::freeze(data); },"Zero specular exponent accepted");
    material.specular_exponent = 6;
    material.specular_lightmap_mix = 2;
    rejects([&] { d3scene::Presentation::freeze(data); },"Invalid specular lightmap mix accepted");
    material.specular_lightmap_mix = 0;
    material.specular_strength = std::numeric_limits<float>::quiet_NaN();
    rejects([&] { d3scene::Presentation::freeze(data); },"Nonfinite specular response accepted");
    material.specular_strength = 0;
    d3scene::ObjectState object;
    object.handle = 42; data.objects.push_back(object);
    data.player_control_delta = {1,2,3};
    data.afterburner_factor = .75f;
    const auto frozen = d3scene::Presentation::freeze(data);
    data.player_control_delta = {};
    data.afterburner_factor = 0;
    check(frozen->data().player_control_delta.y == 2 && frozen->data().afterburner_factor == .75f,
        "Presentation motion inputs alias simulation state");
    data.player_control_delta.z = std::numeric_limits<float>::quiet_NaN();
    rejects([&] { d3scene::Presentation::freeze(data); }, "Nonfinite controlled translation accepted");
    data.player_control_delta = {};
    for (float invalid : {-1.0f, 1.01f, std::numeric_limits<float>::infinity()}) {
        data.afterburner_factor = invalid;
        rejects([&] { d3scene::Presentation::freeze(data); }, "Invalid afterburner factor accepted");
    }
    data.afterburner_factor = 0;
    data.lighting_quality = d3scene::LightingQuality::Vertex;
    const auto low_quality = d3scene::Presentation::freeze(data);
    check(low_quality->data().lighting_quality == d3scene::LightingQuality::Vertex &&
        frozen->data().lighting_quality == d3scene::LightingQuality::Pixel,"Lighting quality is not frame-owned");
    data.lighting_quality = static_cast<d3scene::LightingQuality>(255);
    rejects([&] { d3scene::Presentation::freeze(data); }, "Invalid lighting quality accepted");
    data.lighting_quality = d3scene::LightingQuality::Pixel;
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
    check(!view.dynamic_lighting && view.exclude_camera_rig && view.camera_owner == 2048, "auxiliary view lost explicit policy");
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
int runTerrainTests();
int runFogTests();
int runFeedbackTests();
int runCanvasTests();
int runModuleContextTests();
int runAutomapTests();
int main() {
    try {
        assetTests(); frameTests(); extractionTests();
        Checks += runSceneViewTests();
        Checks += runModelPoseTests();
        Checks += runModelSceneTests();
        Checks += runLightTests();
        Checks += runSceneImageTests();
        Checks += runProceduralTests();
        Checks += runTerrainTests();
        Checks += runFogTests();
        Checks += runFeedbackTests();
        Checks += runCanvasTests();
        Checks += runModuleContextTests();
        Checks += runAutomapTests();
        std::printf("PASS: %d renderer scene checks\n", Checks);
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL after %d checks: %s\n", Checks, error.what());
        return 1;
    }
}
