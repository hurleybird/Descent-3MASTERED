// Descent 3MASTERED - GPL-3.0-or-later
#include "../../renderer/SceneTerrain.h"
#include "../../renderer/SceneView.h"
#include "../../renderer/SceneLighting.h"
#include <cmath>
#include <stdexcept>
int runTerrainTests() {
    using namespace d3scene;
    int checks = 0;
    const auto check = [&](bool value) { ++checks; if (!value) throw std::runtime_error("Terrain geometry contract"); };
    const auto rejects = [&](const TerrainGrid& grid) {
        SceneAssets assets;
        bool failed = false;
        try { assets.publishTerrain(grid); } catch (const std::invalid_argument&) { failed = true; }
        check(failed);
    };
    TerrainGrid grid; grid.width = grid.depth = 256;
    grid.heights.resize(256*256,0); grid.cells.resize(255*255); grid.materials.push_back({7,2});
    SceneAssets assets;
    const auto first = assets.publishTerrain(grid);
    check(first->patches().size() == 256);
    size_t triangles = 0;
    for (const auto& patch : first->patches()) for (const auto& batch : patch.batches) triangles += batch.geometry.polygons.size();
    check(triangles == 255*255*2);
    check(assets.publishTerrain(grid) == first);
    bool mapping_matches = true;
    const uint32_t corner_ids[6] = {0,3,1,3,2,1};
    for (uint32_t rotation = 0; rotation < 4; ++rotation) {
        for (uint32_t index = 0; index < grid.cells.size(); ++index) {
            const uint32_t x = index%255, z = index/255;
            grid.cells[index].rotation = uint8_t(0x30|rotation);
            const auto vertices = terrainCellTriangles(grid,index);
            for (uint32_t i = 0; i < 6; ++i) {
                const uint32_t corner = corner_ids[i];
                const uint32_t dx = corner == 1 || corner == 2 ? 1 : 0;
                const uint32_t dz = corner < 2 ? 1 : 0;
                float u = float(x&7)*.125f, v = float(7-(z&7))*.125f;
                if (dx) u += .125f;
                if (!dz) v += .125f;
                float ru = u, rv = v;
                if (rotation == 1) { ru = 1-v; rv = u; }
                if (rotation == 2) { ru = 1-u; rv = 1-v; }
                if (rotation == 3) { ru = v; rv = 1-u; }
                const auto& vertex = vertices[i];
                mapping_matches &= vertex.position.x == (x+dx)*16 && vertex.position.z == (z+dz)*16 &&
                    vertex.normal.y == 1 && vertex.uv.x == ru*3 && vertex.uv.y == rv*3 &&
                    vertex.baked_uv.x == float((x&127)+dx)/128 && vertex.baked_uv.y == float(128-(z&127)-dz)/128;
            }
        }
    }
    check(mapping_matches);
    grid.heights[256] = 12;
    const auto changed = assets.publishTerrain(grid);
    check(first != changed && first->grid().heights[256] == 0 && changed->grid().heights[256] == 12);
    const auto triangle = terrainCellTriangles(grid,0);
    check(triangle[0].position.y == 12 && triangle[1].position.y == 0 && triangle[2].position.y == 0);
    check(triangle[0].normal.y > 0 && triangle[0].normal.y < 1);
    grid.cells[0].visible = false;
    check(assets.publishTerrain(grid) != changed);
    auto bad = grid; bad.heights.pop_back(); rejects(bad);
    bad = grid; bad.cells.pop_back(); rejects(bad);
    bad = grid; bad.cells[0].material = 9; rejects(bad);
    bad = grid; bad.spacing = 0; rejects(bad);
    FrameData frame; frame.sequence = 1; frame.scene_lifetime = assets.lifetime();
    frame.terrain.asset = changed; frame.terrain.materials.resize(1);
    prepareTerrainPatches(frame.terrain);
    const auto frozen = Presentation::freeze(frame);
    check(&frozen->data().terrain.surface(0,0) == &frozen->data().terrain.surface(changed->patches().size()-1,0));
    uint32_t batch_count = 0;
    for (size_t patch = 0; patch < changed->patches().size(); ++patch) {
        check(frozen->data().terrain.patches[patch].surface_base == batch_count);
        batch_count += uint32_t(changed->patches()[patch].batches.size());
    }
    check(frozen->data().compiled_surface_count == batch_count);
    frame.terrain.materials[0].alpha = .5f;
    const auto tinted = Presentation::freeze(frame);
    check(tinted->data().terrain.surface(0,0).material.blended() &&
        frozen->data().terrain.surface(0,0).alpha == 1);
    frame.terrain.materials[0].alpha = 1;
    View view; view.history_key = 1; view.viewport = view.projection_anchor = {0,0,800,600};
    view.camera.position = {128,100,16}; view.camera.forward = {0,-.6f,.8f}; view.camera.up = {0,.8f,.6f};
    const auto visible = queryView(*frozen,view);
    check(!visible.surfaces.empty() && visible.surfaces.size() < changed->patches().size());
    for (const auto& item : visible.surfaces) check(item.terrain);
    {
        TerrainOcclusionData blocked; blocked.width = blocked.depth = 16;
        blocked.visibility.resize(256*32,0);
        frame.terrain.occlusion = assets.publishTerrainOcclusion(blocked);
        // Gameplay's authored table must not erase rendered geometry.
        check(queryView(*Presentation::freeze(frame),view).surfaces.size() == visible.surfaces.size());
        check(!queryView(*frozen,view).surfaces.empty()); // retained pre-occlusion frame is unchanged
        frame.terrain.occlusion.reset();
    }
    MeshGeometry wall; wall.vertices = {{0,0,0},{1,0,0},{0,1,0}};
    MeshPolygon face; face.normal = {0,0,1}; face.corners = {{0},{1},{2}}; wall.polygons.push_back(face);
    RoomState room; room.number = 0; room.asset = assets.publishRoom(0,wall); room.surfaces.resize(1);
    frame.rooms.push_back(room); view.room = 0;
    check(queryView(*Presentation::freeze(frame),view).surfaces.empty());
    frame.rooms[0].exterior = true;
    check(!queryView(*Presentation::freeze(frame),view).surfaces.empty());
    Light light; light.room = 0; light.position = {8,8,8}; light.radius = 32;
    frame.lights.push_back(light); prepareLightInfluences(frame);
    check(frame.lightCandidates(frame.terrain.patches[0].light_candidates).size() == 1 && frame.terrain.patches.back().light_candidates.empty());
    const auto illuminated = Presentation::freeze(frame);
    frame.rooms[0].exterior = false; prepareLightInfluences(frame);
    check(frame.terrain.patches[0].light_candidates.empty());
    check(illuminated->data().lightCandidates(illuminated->data().terrain.patches[0].light_candidates).size() == 1);
    {
        auto invalid = frame;
        invalid.terrain.patches[0].light_candidates = {UINT32_MAX,1};
        bool rejected = false;
        try { Presentation::freeze(invalid); } catch (const std::invalid_argument&) { rejected = true; }
        check(rejected);
    }
    assets.reset();
    check(!assets.terrain() && frozen->data().terrain.asset->grid().heights[256] == 12);
    check(assets.publishTerrain(grid)->id().lifetime != changed->id().lifetime);
    TerrainGrid clipped; clipped.width = clipped.depth = 2; clipped.heights.resize(4,0);
    clipped.cells.resize(1); clipped.materials.push_back({7,2});
    clipped.clip_planes.push_back({{1,0,0},-8}); clipped.cells[0].clip_plane = 1;
    auto clipped_vertices = terrainCellTriangles(clipped,0);
    check(clipped_vertices.size() == 9);
    bool kept_half = true;
    for (const auto& vertex : clipped_vertices) kept_half &= vertex.position.x >= 7.9949f &&
        std::abs(vertex.uv.x-vertex.position.x/128.f) < .000001f;
    check(kept_half);
    clipped.clip_planes[0].constant = -20;
    check(terrainCellTriangles(clipped,0).empty() && assets.publishTerrain(clipped)->patches().empty());
    clipped.cells[0].clip_plane = 2; rejects(clipped);
    const uint8_t exposure[] = {1,2,4,8};
    const TerrainExposureView field{exposure,2,2,16,43};
    check(sampleTerrainExposure(field,{0,0,0},0) == 1);
    check(sampleTerrainExposure(field,{0,21.5f,0},0) == .5f);
    check(sampleTerrainExposure(field,{8,0,8},0) == .25f);
    check(sampleTerrainExposure(field,{8,344,8},0) == 1);
    check(sampleTerrainExposure(field,{-1,0,0},0) == .5f);
    check(sampleTerrainExposure(field,{0,-43,0},0) == 1); // defined lower bound, no negative bit shifts
    check(sampleTerrainExposure(field,{0,0,0},100) == 1);
    grid.sky_visibility.resize(grid.heights.size());
    for (size_t i = 0; i < grid.sky_visibility.size(); ++i) grid.sky_visibility[i] = uint8_t((i*37)^i);
    for (int probe = 0; probe < 1000; ++probe) {
        const Vec3 p{float((probe*113)%4096)+.25f,float((probe*37)%400)+.5f,float((probe*181)%4096)+.75f};
        const int x = int(p.x/16), z = int(p.z/16), y = int(p.y/43);
        const float fx = p.x/16-x, fz = p.z/16-z, fy = p.y/43-y;
        float expected = 0;
        for (int dz = 0; dz < 2; ++dz) for (int dx = 0; dx < 2; ++dx) for (int dy = 0; dy < 2; ++dy) {
            const int index = z*256+x+dz*256+dx, bit = y+dy;
            const float value = index >= 256*256 || bit >= 8 ? 1.f : (grid.sky_visibility[index] & (1u<<bit) ? 1.f : 0.f);
            expected += value*(dx ? fx : 1-fx)*(dz ? fz : 1-fz)*(dy ? fy : 1-fy);
        }
        check(std::abs(sampleTerrainExposure(grid,p,z*256+x)-expected) < .000001f);
    }
    {
        SceneAssets catalog;
        TerrainOcclusionData table; table.width = table.depth = 2;
        table.sector_size = 10; table.height_ceiling = 100; table.visibility = {2,0,0,0};
        const auto original = catalog.publishTerrainOcclusion(table);
        check(catalog.publishTerrainOcclusion(table) == original);
        check(terrainMaySeeSphere(*original,{1,0,1},{11,0,1},0));
        check(!terrainMaySeeSphere(*original,{11,0,1},{1,0,1},0)); // directed, not symmetrized
        check(!terrainMaySeeSphere(*original,{1,0,1},{1,0,11},0));
        check(terrainMaySeeSphere(*original,{1,0,1},{1,100,11},0));
        check(terrainMaySeeSphere(*original,{1,0,1},{1,99,11},1));
        check(terrainMaySeeSphere(*original,{-.1f,0,1},{1,0,11},0)); // no negative sector alias
        check(terrainMaySeeSphere(*original,{1,0,1},{20,0,11},0));
        table.visibility[0] = 0;
        const auto replacement = catalog.publishTerrainOcclusion(table);
        check(replacement != original && terrainMaySeeSphere(*original,{1,0,1},{11,0,1},0));
        check(!terrainMaySeeSphere(*replacement,{1,0,1},{11,0,1},0));
        bool invalid = false;
        table.visibility.pop_back();
        try { catalog.publishTerrainOcclusion(table); } catch (const std::invalid_argument&) { invalid = true; }
        check(invalid);
        FrameData held; held.sequence = 1; held.scene_lifetime = catalog.lifetime(); held.terrain.occlusion = original;
        const auto owned = Presentation::freeze(held);
        catalog.reset();
        check(!catalog.terrainOcclusion() && owned->data().terrain.occlusion == original);
        held.scene_lifetime = catalog.lifetime(); invalid = false;
        try { Presentation::freeze(held); } catch (const std::invalid_argument&) { invalid = true; }
        check(invalid);
        table.width = 9; table.depth = 1; table.visibility.assign(18,0); table.visibility[1] = 1;
        const auto byte_boundary = catalog.publishTerrainOcclusion(table);
        check(terrainMaySeeSphere(*byte_boundary,{1,0,1},{81,0,1},0));
        check(!terrainMaySeeSphere(*byte_boundary,{1,0,1},{71,0,1},0));
    }
    return checks;
}
