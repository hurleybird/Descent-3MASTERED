// Descent 3MASTERED - GPL-3.0-or-later
#include "../../renderer/Scene.h"
#include "../../renderer/SceneGeometry.h"
#include "../../renderer/SceneGeometryAllocator.h"
#include "../../Descent3/SceneExtraction.h"
#include "room.h"
#include "object.h"
#include "gametexture.h"
#include <cstdio>
#include <limits>
#include <stdexcept>
#include <thread>

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
void storageTests() {
    using namespace d3scene;
    {
        SceneGeometryAllocator ranges(16);
        auto a = ranges.allocate(5), b = ranges.allocate(7), c = ranges.allocate(4);
        check(a.page == b.page && b.page == c.page && a.offset == 0 && b.offset == 5 && c.offset == 12,
            "Geometry allocator failed packed page allocation");
        auto large = ranges.allocate(35);
        check(large.page == 1 && ranges.capacity(1) == 64,"Oversized mesh allocation failed");
        ranges.release(b); ranges.release(a); ranges.release(c);
        auto whole = ranges.allocate(16);
        check(whole.page == 0 && whole.offset == 0 && ranges.pageCount() == 2,"Retired geometry did not coalesce");
        ranges.release(whole);
        rejects([&] { ranges.release(whole); },"Double geometry retirement accepted");
        rejects([&] { ranges.release({0,15,2}); },"Out-of-bounds geometry retirement accepted");
        ranges.release(large);
        std::vector<SceneGeometryAllocator::Allocation> live;
        uint32_t random = 937;
        for (int i = 0; i < 2000; ++i) {
            random = random*1664525u+1013904223u;
            if (!live.empty() && (random&3) == 0) {
                const auto index = (random>>8)%live.size();
                ranges.release(live[index]); live.erase(live.begin()+index);
            } else {
                const auto allocation = ranges.allocate(1+(random>>16)%13);
                for (const auto old : live) check(old.page != allocation.page ||
                    old.offset+old.count <= allocation.offset || allocation.offset+allocation.count <= old.offset,
                    "Live geometry allocations overlap");
                live.push_back(allocation);
            }
        }
        for (const auto allocation : live) ranges.release(allocation);
        const auto pages = ranges.pageCount();
        for (uint32_t page = 0; page < pages; ++page) {
            const auto allocation = ranges.allocate(ranges.capacity(page));
            check(allocation.page == page && !allocation.offset,"Geometry page was lost to fragmentation");
        }
        ranges.clear(); check(ranges.pageCount() == 0 && ranges.allocate(16).page == 0,"Geometry reset retained old pages");
    }
    {
        SceneIdentityIndex<uint64_t> index;
        std::vector<std::pair<uint64_t,int>> inventory{{0,10},{UINT64_MAX,20},{17,30}};
        const auto key = [](const std::pair<uint64_t,int>& entry) { return entry.first; };
        check(index.assign(inventory,key) && !index.assign(inventory,key),"Identity index rebuilt unchanged ordering");
        inventory[1].second = 99;
        check(!index.assign(inventory,key),"Identity index depends on mutable payload");
        std::swap(inventory[0],inventory[2]);
        check(index.assign(inventory,key) && index.lookup().at(17) == 0 && index.lookup().at(0) == 2,
            "Identity index ignored reordered keys");
        inventory[2].first = 18;
        check(index.assign(inventory,key) && !index.lookup().count(0) && index.lookup().at(18) == 2,
            "Identity index retained replaced generation");
        inventory[2].first = 17;
        rejects([&] { index.assign(inventory,key); },"Identity index accepted duplicate keys");
        inventory.resize(2);
        check(index.assign(inventory,key) && index.lookup().size() == 2 && !index.lookup().count(18),
            "Interrupted identity compilation reused partial state");
        inventory.clear();
        check(index.assign(inventory,key) && !index.assign(inventory,key) && index.lookup().size() == 0,
            "Identity index failed empty inventory transition");
    }
    SceneAssets assets;
    auto source = acquireFrameData();
    source.sequence = 1; source.scene_lifetime = assets.lifetime();
    source.objects.reserve(128); source.objects.resize(1);
    source.objects[0].current.position.x = 17;
    auto held = Presentation::publish(std::move(source));
    auto reader = held;
    held.reset();
    auto while_held = acquireFrameData();
    check(while_held.objects.capacity() == 0 && reader->data().objects[0].current.position.x == 17,
        "Live snapshot storage was recycled");
    reader.reset();
    auto reused = acquireFrameData();
    check(reused.objects.capacity() >= 128 && reused.objects.empty() && !reused.sequence && !reused.scene_lifetime,
        "Released snapshot did not return empty capacity with fresh scalar state");
    reused.sequence = 2; reused.scene_lifetime = assets.lifetime();
    reused.objects.resize(1); reused.objects[0].handle = 42;
    auto foreign = Presentation::publish(std::move(reused));
    check(foreign->objectIndices().count(42) == 1 && foreign->objectIndices().count(0) == 0,
        "Recycled compilation retained the previous frame's object identities");
    std::thread release([frame = std::move(foreign)]() mutable { frame.reset(); });
    release.join();
    check(acquireFrameData().objects.capacity() == 0,"Foreign thread modified publisher's spare storage");
    PresentationRef outlives_publisher;
    std::thread publish([&] {
        auto input = acquireFrameData(); input.sequence = 3; input.scene_lifetime = assets.lifetime();
        outlives_publisher = Presentation::publish(std::move(input));
    });
    publish.join();
    check(outlives_publisher->data().sequence == 3,"Snapshot did not outlive publishing thread");
    outlives_publisher.reset();
    {
        auto input = acquireFrameData(); input.sequence = 4; input.scene_lifetime = assets.lifetime();
        input.objects.resize(1); input.objects[0].handle = 61;
        auto seed = Presentation::publish(std::move(input)); seed.reset();
        auto reused = acquireFrameData(); reused.sequence = 5; reused.scene_lifetime = assets.lifetime();
        reused.objects.resize(1); reused.objects[0].handle = 71;
        auto alias = reused;
        const auto first = Presentation::publish(std::move(reused));
        alias.objects[0].handle = 81;
        const auto second = Presentation::freeze(alias);
        check(first->objectIndices().count(71) && !first->objectIndices().count(61) && !first->objectIndices().count(81) &&
            second->objectIndices().count(81) && !second->objectIndices().count(71),
            "Copied builder scratch storage aliased published compilation");
    }
}
void assetTests() {
    {
        using namespace d3scene;
        SceneAssets assets;
        const auto a = assets.publishRoom(3,triangle()), b = assets.publishRoom(47,triangle());
        const auto publish = [&](std::initializer_list<int32_t> numbers) {
            auto data = acquireFrameData(); data.sequence = 1; data.scene_lifetime = assets.lifetime();
            for (const auto number : numbers) {
                RoomState room; room.number = number; room.asset = number == 3 ? a : b;
                room.surfaces.resize(1); data.rooms.push_back(std::move(room));
            }
            return Presentation::publish(std::move(data));
        };
        for (int round = 0; round < 5; ++round) {
            { const auto frame = publish({3,47});
              check(frame->roomIndices().at(3) == 0 && frame->roomIndices().at(47) == 1,"Room index failed stable layout"); }
            { const auto frame = publish({47,3});
              check(frame->roomIndices().at(47) == 0 && frame->roomIndices().at(3) == 1,"Room index failed reordered layout"); }
            { const auto frame = publish({47});
              check(frame->roomIndices().at(47) == 0 && !frame->roomIndices().count(3),"Room index retained removed room"); }
            rejects([&] { publish({3,3}); },"Retained room layout accepted duplicate identity");
            rejects([&] { publish({-1}); },"Retained room layout accepted negative identity");
            { const auto frame = publish({}); check(frame->roomIndices().size() == 0,"Empty room layout retained entries"); }
        }
        const auto held = publish({3,47}); const auto reordered = publish({47,3});
        check(held->roomIndices().at(3) == 0 && reordered->roomIndices().at(3) == 1,"Room index changed a held frame");
    }
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
        const auto repeated = Presentation::freeze(held->data());
        check(repeated->data().rooms[0].surfaces.data() == held->data().rooms[0].surfaces.data(),
            "Unchanged planar room recompiled its material table");
        auto removed = held->data(); removed.rooms[0].mirrors.clear();
        const auto ordinary = Presentation::freeze(removed);
        check(ordinary->data().rooms[0].surfaces[0].material.planar_slot == 0 &&
            held->data().rooms[0].surfaces[0].material.planar_slot != 0,"Removing mirrors left stale compiled planar policy");
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
        auto aperture = data;
        d3scene::Portal portal; portal.polygon = 0; portal.surface_controls_visibility = true;
        aperture.rooms[0].portals.push_back(portal);
        const auto solid = d3scene::Presentation::freeze(aperture);
        check(!solid->data().rooms[0].portals[0].traversable,"Solid portal material did not block traversal");
        auto glass = solid->data();
        glass.rooms[0].surfaces[0].blend = d3scene::BlendMode::Alpha;
        const auto transparent = d3scene::Presentation::freeze(glass);
        check(transparent->data().rooms[0].portals[0].traversable && !solid->data().rooms[0].portals[0].traversable,
            "Material change reused old portal policy or modified a held frame");
        auto combined = solid->data(); combined.rooms[0].surfaces = transparent->data().rooms[0].surfaces;
        const auto rebound = d3scene::Presentation::freeze(combined);
        check(rebound->data().rooms[0].portals[0].traversable,
            "A different sealed material table bypassed portal policy compilation");
        const auto stable = d3scene::Presentation::freeze(rebound->data());
        check(&stable->data().rooms[0].portals.read() == &rebound->data().rooms[0].portals.read(),
            "Unchanged portal material proof was rebuilt");
        auto invalid_portal = stable->data();
        invalid_portal.rooms[0].portals[0].polygon = 1;
        rejects([&] { d3scene::Presentation::freeze(invalid_portal); },"Edited sealed portal bypassed polygon validation");
        invalid_portal = stable->data(); invalid_portal.rooms[0].portals[0].polygon = -1;
        rejects([&] { d3scene::Presentation::freeze(invalid_portal); },"Edited sealed portal accepted negative polygon");
        combined.rooms[0].portals[0].surface_controls_visibility = false;
        combined.rooms[0].portals[0].traversable = true;
        combined.rooms[0].surfaces = solid->data().rooms[0].surfaces;
        check(d3scene::Presentation::freeze(combined)->data().rooms[0].portals[0].traversable,
            "Explicit open aperture was overridden by surface opacity");
    }
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
    source.mirror_face = -1;
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
    const auto retained = ExtractGamePresentation(2.7,0,false,false,after_thumbnail.get());
    check(retained->data().rooms[0].surfaces.data() == after_thumbnail->data().rooms[0].surfaces.data(),
        "Unchanged room appearance lost published storage");
    {
        portal connection{}; connection.portal_face = 0; connection.croom = -1; connection.cportal = -1; connection.combine_master = -1;
        source.num_portals = 1; source.portals = &connection;
        const auto first = ExtractGamePresentation(2.71,0,false,false,retained.get());
        const auto next = ExtractGamePresentation(2.72,0,false,false,first.get());
        check(&first->data().rooms[0].portals.read() == &next->data().rooms[0].portals.read(),
            "Extraction rebuilt unchanged portal topology");
        connection.croom = 0; connection.flags = 1;
        const auto changed = ExtractGamePresentation(2.73,0,false,false,next.get());
        check(changed->data().rooms[0].portals[0].destination_room == 0 && changed->data().rooms[0].portals[0].flags == 1 &&
            first->data().rooms[0].portals[0].destination_room == -1,"Portal reconnection was stale or changed a held frame");
        const auto original_flags = source.flags;
        source.flags |= RF_EXTERNAL; connection.croom = 1;
        Rooms[1] = source; Rooms[1].flags = RF_DOOR; Rooms[1].num_portals = 0; Rooms[1].portals = nullptr;
        const auto door = ExtractGamePresentation(2.74,0,false,false,changed.get());
        Rooms[1].flags = 0;
        const auto ordinary = ExtractGamePresentation(2.75,0,false,false,door.get());
        check(door->data().rooms[0].portals[0].two_sided && !ordinary->data().rooms[0].portals[0].two_sided,
            "Destination door classification reused stale aperture winding");
        Rooms[1] = {}; source.flags = original_flags;
        source.num_portals = 0; source.portals = nullptr;
    }
    surface.flags |= FF_SCORCHED; // effect bookkeeping is not a material input
    const auto bookkeeping = d3scene::Presentation::freeze(ExtractGameFrame(2.8,0,false,false,retained.get(),true));
    check(bookkeeping->data().rooms[0].surfaces.data() == retained->data().rooms[0].surfaces.data(),
        "Non-material face flags invalidated appearance");
    surface.tmap = 9;
    bool missed_appearance = false;
    try { ExtractGameFrame(2.9,0,false,false,retained.get(),true); }
    catch (const std::logic_error&) { missed_appearance = true; }
    check(missed_appearance,"Appearance audit missed an unannounced texture edit");
    InvalidateGameSceneAppearance();
    const auto repainted = ExtractGamePresentation(2.9,0,false,false,retained.get());
    check(repainted->data().rooms[0].surfaces[0].texture == 9 && retained->data().rooms[0].surfaces[0].texture != 9,
        "Appearance invalidation missed the edit or mutated a held frame");
    Objects[0].handle += HANDLE_COUNT_INCREMENT;
    vertices[0].x = 7; surface.tmap = 8;
    bool missed_invalidation = false;
    try { ExtractGameFrame(3,.016f,false,false,nullptr,true); }
    catch (const std::logic_error&) { missed_invalidation = true; }
    check(missed_invalidation,"Geometry audit missed an unannounced producer mutation");
    InvalidateGameSceneGeometry();
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
    surface.flags |= FF_NOT_SHELL;
    bool missed_shell = false;
    try { ExtractGameFrame(5.1,0,true,false,nullptr,true); }
    catch (const std::logic_error&) { missed_shell = true; }
    check(missed_shell,"Geometry audit ignored changed room-shell membership");
    InvalidateGameSceneGeometry();
    const auto detail = ExtractGamePresentation(5.1,0,true,false);
    check(!detail->data().rooms[0].asset->geometry().polygons[0].bounds_volume &&
        detail->data().rooms[0].surfaces.size() == 1 && detail->data().rooms[0].surfaces[0].draw_surface,
        "Non-shell detail either bounded fog or lost its render surface");
    check(e->data().rooms[0].asset->geometry().polygons[0].bounds_volume,
        "Shell edit changed an already published boundary");
    surface.flags &= ~FF_NOT_SHELL;
    source = {};
    const auto empty = ExtractGamePresentation(6, 0, true);
    check(empty->data().rooms.empty() && !d3scene::assets().room(0), "removed room retained in current scene");
    check(a->data().rooms[0].asset->geometry().vertices.size() == 3, "unload invalidated held presentation");
    Highest_object_index = -1;
    ExtractGamePresentation(7,0,false);
    Highest_object_index = 0;
    const auto restored_slot = ExtractGamePresentation(8,0,false);
    check(!restored_slot->data().objects[0].has_previous,"Absent high slot retained history after inventory shrink");
    Objects[0].type = OBJ_NONE;
    ExtractGamePresentation(9,0,false);
    Objects[0].type = OBJ_ROBOT;
    check(!ExtractGamePresentation(10,0,false)->data().objects[0].has_previous,
        "Absent object retained history across a committed empty slot");
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
        {
            d3scene::SceneLookup<uint64_t,size_t> index;
            check(index.find(0) == index.end(),"Empty scene lookup found a key");
            for (size_t i = 0; i < 4096; ++i)
                check(index.emplace(uint64_t(i)*UINT64_C(0x100000001),i).second,"Scene identity insertion failed");
            for (size_t i = 0; i < 4096; ++i) {
                const auto key = uint64_t(i)*UINT64_C(0x100000001);
                check(index.at(key) == i,"Packed identity lost generation bits");
                check(!index.emplace(key,9999).second && index.at(key) == i,"Duplicate identity overwrote its value");
            }
            check(index.size() == 4096 && !index.count(UINT64_MAX),"Scene lookup population or absence incorrect");
            auto copy = index;
            copy.emplace(UINT64_MAX,42);
            check(!index.count(UINT64_MAX) && copy.at(UINT64_MAX) == 42,"Copied identity table aliased its source");
            d3scene::SceneLookup<int32_t,size_t> signed_index;
            signed_index.reserve(3);
            signed_index.emplace(INT32_MIN,1); signed_index.emplace(-1,2); signed_index.emplace(INT32_MAX,3);
            check(signed_index.at(INT32_MIN) == 1 && signed_index.at(-1) == 2 && signed_index.at(INT32_MAX) == 3,
                "Signed identity sentinel collided with a valid key");
            bool absent = false;
            try { signed_index.at(0); } catch (const std::out_of_range&) { absent = true; }
            check(absent,"Missing identity did not reject lookup");
            const auto address = signed_index.find(-1);
            signed_index.clear();
            check(signed_index.size() == 0 && !signed_index.count(INT32_MIN) && !signed_index.count(INT32_MAX),
                "Cleared identity lookup retained old keys");
            signed_index.emplace(-1,7);
            check(signed_index.find(-1) == address && signed_index.at(-1) == 7,
                "Identity lookup clear discarded reusable storage");
        }
        storageTests(); assetTests(); frameTests(); extractionTests();
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
