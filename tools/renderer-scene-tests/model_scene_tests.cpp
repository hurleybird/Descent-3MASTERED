// Descent 3MASTERED - GPL-3.0-or-later
#include "../../renderer/Scene.h"
#include "../../renderer/SceneView.h"
#include "../../renderer/SceneGeometry.h"
#include "../../renderer/SceneLighting.h"
#include "../../renderer/SceneMotion.h"
#include "../../model/SceneModelAsset.h"
#include <limits>
#include <cmath>
#include <stdexcept>

namespace {
int checks;
void check(bool condition, const char* message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}
template<class F> void rejects(F f) {
    bool rejected = false;
    try { f(); } catch (const std::invalid_argument&) { rejected = true; }
    check(rejected, "Invalid model scene accepted");
}
d3scene::ModelGeometry modelGeometry() {
    d3scene::ModelGeometry geometry;
    d3scene::ModelPart part;
    part.geometry.vertices = {{-1,-1,0},{0,1,0},{1,-1,0}};
    part.geometry.polygons.push_back({{0,0,-1},{{0,0,0},{1,0,1},{2,1,0}}});
    part.geometry.vertex_normals = {{0,0,-1},{0,0,-1},{0,0,-1}};
    part.polygon_materials = {-1}; part.polygon_colors = {{1,0,0}};
    geometry.parts.push_back(part);
    return geometry;
}
}
int runModelSceneTests() {
    using namespace d3scene;
    checks = 0;
    check(ResolveSceneModelPartRole(0,1,0,false) == SceneModelPartRole::Surface,"Ordinary model part lost surface role");
    check(ResolveSceneModelPartRole(SOF_FACING,1,0,false) == SceneModelPartRole::Facing,"Facing part became ordinary geometry");
    check(ResolveSceneModelPartRole(SOF_FACING|SOF_GLOW,1,0,false) == SceneModelPartRole::Glow,"Glow role lost legacy precedence");
    check(ResolveSceneModelPartRole(SOF_THRUSTER,1,0,false) == SceneModelPartRole::Glow,"Thruster bypassed glow preparation");
    check(ResolveSceneModelPartRole(SOF_CUSTOM,1,0,false) == SceneModelPartRole::Hidden &&
        ResolveSceneModelPartRole(SOF_CUSTOM,1,0,true) == SceneModelPartRole::Surface,"Custom panel eligibility ignored published texture");
    check(ResolveSceneModelPartRole(SOF_CUSTOM|SOF_FACING,1,0,true) == SceneModelPartRole::Facing,"Custom facing part lost its role");
    check(ResolveSceneModelPartRole(SOF_SHELL,1,0,true) == SceneModelPartRole::Hidden &&
        ResolveSceneModelPartRole(SOF_FRONTFACE,1,0,true) == SceneModelPartRole::Hidden,"Door authoring metadata became model draw geometry");
    check(ResolveSceneModelPartRole(SOF_GLOW,1,1,true) == SceneModelPartRole::Hidden &&
        ResolveSceneModelPartRole(0,UINT32_MAX,32,true) == SceneModelPartRole::Hidden,"Part visibility mask wrapped or was ignored");
    auto hierarchy = modelGeometry(); hierarchy.parts.push_back(hierarchy.parts[0]); hierarchy.parts[1].parent = 0;
    hierarchy.parts[0].authored_flags = SOF_SHELL;
    check(ResolveSceneModelRoles(hierarchy,3,false)[1] == SceneModelPartRole::Hidden,"Door metadata exposed descendant geometry");
    hierarchy.parts[0].authored_flags = SOF_CUSTOM;
    check(ResolveSceneModelRoles(hierarchy,2,false)[1] == SceneModelPartRole::Surface,"Masked/custom parent hid an independent child");
    hierarchy.parts[0].geometry.polygons.clear();
    check(ResolveSceneModelRoles(hierarchy,3,true)[1] == SceneModelPartRole::Hidden,"Empty parent bypassed legacy traversal exclusion");
    hierarchy.parts[0].parent = 1;
    rejects([&] { ResolveSceneModelRoles(hierarchy,3,true); });
    vector vertices[] = {{-1,-1,0},{0,1,0},{1,-1,0}};
    vector normals[] = {{0,0,-1},{0,std::numeric_limits<float>::quiet_NaN(),0},{0,0,0}};
    short indices[] = {0,1,2};
    float u[] = {0,0,1}, v[] = {0,1,0}, alpha[] = {1,0.5f,1};
    polyface face{}; face.nverts = 3; face.vertnums = indices; face.u = u; face.v = v;
    face.normal = {0,0,-1}; face.texnum = -1; face.color = GR_RGB(128,64,32);
    bsp_info source_part{};
    source_part.parent = -1; source_part.nverts = 3; source_part.verts = vertices;
    source_part.vertnorms = normals; source_part.alpha = alpha; source_part.num_faces = 1; source_part.faces = &face;
    poly_model source_model{}; source_model.n_models = 1; source_model.submodel = &source_part;
    size_t repaired = 0;
    const auto compiled = CompileSceneModelAsset(source_model,&repaired);
    check(compiled.rest_pose.size() == 1 && compiled.rest_pose[0].right.x == 1,
        "Static model rest transform was not compiled with its asset");
    source_part.flags |= SOF_ROTATE;
    check(CompileSceneModelAsset(source_model).rest_pose.empty(),"Autonomous rotation was frozen into a rest pose");
    source_model.flags |= PMF_TIMED;
    check(CompileSceneModelAsset(source_model).rest_pose.size() == 1,"Timed no-input rest semantics changed");
    source_model.flags &= ~PMF_TIMED; source_part.flags &= ~SOF_ROTATE;
    check(repaired == 2 && compiled.parts[0].geometry.vertex_normals[1].z == -1, "Invalid authored normals not repaired at asset boundary");
    check(std::isnan(normals[1].y), "Asset compiler rewrote source shading data");
    check(MatchesSceneModelAsset(compiled,source_model), "Resident model did not recognize repaired source normals");
    u[1] = .25f;
    check(!MatchesSceneModelAsset(compiled,source_model), "Resident model missed changed UV");
    u[1] = 0; alpha[1] = 1;
    check(!MatchesSceneModelAsset(compiled,source_model), "Resident model missed changed vertex alpha");
    alpha[1] = .5f; face.color = GR_RGB(1,2,3);
    check(!MatchesSceneModelAsset(compiled,source_model), "Resident model missed changed flat color");
    face.color = GR_RGB(128,64,32); normals[0].x = .1f;
    check(!MatchesSceneModelAsset(compiled,source_model), "Resident model missed changed normal");
    normals[0].x = 0;
    check(MatchesSceneModelAsset(compiled,source_model), "Restored model no longer matches");
    vertices[0].x = 99;
    check(!MatchesSceneModelAsset(compiled,source_model), "Resident model missed changed position");
    check(compiled.parts[0].geometry.vertices[0].x == -1, "Compiled geometry retained source pointers");
    check(compiled.parts[0].geometry.polygons[0].corners[1].alpha == 127, "Authored model alpha lost");
    check(compiled.parts[0].polygon_colors[0].x == 128/255.f, "Flat model color lost");
    SceneAssets assets;
    auto authored = modelGeometry();
    const auto shape = deformMesh(authored.parts[0].geometry,.2f,3.5,42,1);
    const auto same_shape = deformMesh(authored.parts[0].geometry,.2f,3.5,42,1);
    const auto other_shape = deformMesh(authored.parts[0].geometry,.2f,3.5,43,1);
    check(shape[0].x == same_shape[0].x && shape[1].y == same_shape[1].y,"Repeated deformation advanced visual state");
    check(shape[0].x != other_shape[0].x || shape[1].y != other_shape[1].y,"Deformation lost instance identity");
    check(std::abs(shape[0].x+1) <= .2f && authored.parts[0].geometry.vertices[0].x == -1,"Deformation mutated or escaped authored range");
    check(deformMesh(authored.parts[0].geometry,0,3.5,42,1).empty(),"Rigid instance duplicated authored positions");
    std::vector<size_t> corners;
    forEachTriangleCorner(4,[&](size_t corner) { corners.push_back(corner); });
    check(corners == std::vector<size_t>{0,1,2,0,2,3},"UV and position stream topology differs");
    auto asset = assets.publishModel(3,authored);
    {
        auto palette_geometry = authored;
        auto& p = palette_geometry.parts[0];
        p.geometry.polygons.push_back(p.geometry.polygons[0]);
        p.geometry.polygons.push_back(p.geometry.polygons[0]);
        p.geometry.polygons[2].corners[0].alpha = 127;
        p.polygon_materials = {-1,-1,-1}; p.polygon_colors = {{1,0,0},{1,0,0},{1,0,0}};
        const auto palette_asset = assets.publishModel(4,palette_geometry);
        const auto& palette = palette_asset->geometry().parts[0];
        check(palette.surface_polygons.size() == 2 && palette.surface_for_polygon[0] == palette.surface_for_polygon[1] &&
            palette.surface_for_polygon[0] != palette.surface_for_polygon[2],"Material palette merged distinct alpha policies or duplicated identical materials");
    }
    check(assets.publishModel(3,authored) == asset, "Unchanged model lost identity");
    authored.parts[0].geometry.vertices[0].x = -2;
    auto revision = assets.publishModel(3,authored);
    check(revision->id() != asset->id(), "Model edit did not publish new revision");
    check(asset->geometry().parts[0].geometry.vertices[0].x == -1, "Model publication retained mutable geometry");
    FrameData data;
    data.sequence = 1; data.scene_lifetime = assets.lifetime();
    ModelInstance object;
    object.models.emplace_back();
    object.identity = 42; object.models[0].asset = asset; object.current.position.z = 12;
    object.has_owner = true; object.owner = 42;
    ObjectState owner; owner.handle = 42; owner.radius = 1; owner.current = object.current;
    data.objects.push_back(owner); owner.handle = 43; data.objects.push_back(owner);
    PreparedModelPart part;
    part.current.position.z = -2;
    part.surfaces.resize(1); // intentionally flat-color, no bitmap
    part.surfaces[0].baked_uvs = {{0,0},{.5f,1},{1,0}};
    object.models[0].parts.push_back(part);
    data.model_instances.push_back(object);
    data.model_instances.push_back(object); data.model_instances[1].identity = 43; data.model_instances[1].owner = 43;
    data.model_instances[1].current.position.z = 20;
    data.model_instances[1].models[0].parts[0].surfaces[0].alpha = 0.5f;
    auto frame = Presentation::freeze(data);
    {
        auto retained = frame->data();
        const auto again = Presentation::freeze(retained);
        check(&again->data().model_instances[0].models[0].parts[0].surfaces.read() ==
            &frame->data().model_instances[0].models[0].parts[0].surfaces.read(),"Unchanged model table was copied");
        retained.model_instances[0].models[0].parts[0].material_palette = true;
        rejects([&] { Presentation::freeze(retained); }); // one-entry palette must not hide baked per-face UVs
        check(frame->objectIndices().at(42) == 0 && frame->modelIndices().at(43) == 1,"Compiled ownership indices are incorrect");
    }
    check(frame->data().model_instances[0].has_render_bounds && frame->data().model_instances[0].render_bounds.minimum.z == 10 &&
        frame->data().model_instances[0].render_bounds.maximum.x == 1,"Publication did not compile world-space posed bounds");
    data.model_instances[0].models[0].parts[0].surfaces[0].baked_uvs[0].x = 99;
    check(frame->data().model_instances[0].models[0].parts[0].surfaces[0].baked_uvs[0].x == 0,"Instance baked UVs not owned by frame");
    data.model_instances[0].models[0].parts[0].current.position.z = 55;
    check(frame->data().model_instances[0].models[0].parts[0].current.position.z == -2, "Held frame pose changed");
    View view;
    view.history_key = 1; view.viewport = view.projection_anchor = {0,0,1280,720};
    auto result = queryView(*frame,view);
    check(result.surfaces.size() == 2, "Prepared model surfaces missing");
    check(result.surfaces[0].model && result.surfaces[0].world.position.z == 10, "Instance transform not composed");
    check(result.surfaces[1].blended && result.surfaces[1].world.position.z == 18, "Instance material leaked between objects");
    view.excluded_objects = {42};
    auto excluded = queryView(*frame,view);
    check(excluded.surfaces.size() == 1 && excluded.surfaces[0].model_instance_index == 1, "Excluded viewer mesh was rendered");
    check(result.surfaces.size() == 2, "Auxiliary query changed prior surface list");
    view.excluded_objects.clear(); view.camera.position.x = 100;
    check(queryView(*frame,view).surfaces.empty(), "Offscreen posed geometry not rejected");
    // A pose can extend outside an obsolete source sphere. Both geometry and
    // the candidate object list must use the published rendering bounds.
    auto displaced = frame->data();
    displaced.model_instances.resize(1); displaced.model_instances[0].current.position.x = 100;
    displaced.objects.resize(1); displaced.objects[0].current.position.x = 100;
    displaced.model_instances[0].models[0].parts[0].current.position.x = -100;
    view.camera.position.x = 0;
    auto candidates = queryView(*Presentation::freeze(displaced),view);
    check(candidates.object_indices.size() == 1 && candidates.surfaces.size() == 1, "Source sphere incorrectly culled posed mesh");
    displaced.model_instances[0].models[0].parts[0].current.position.x = 0;
    displaced.model_instances[0].models[0].parts[0].vertices = {{-101,-1,0},{-100,1,0},{-99,-1,0}};
    auto frozen_shape = Presentation::freeze(displaced);
    check(frozen_shape->data().model_instances[0].render_bounds.minimum.x == -1 &&
        frozen_shape->data().model_instances[0].render_bounds.maximum.x == 1,"Deformation bounds used authored positions");
    check(queryView(*frozen_shape,view).surfaces.size() == 1,"Visibility did not consume prepared positions");
    displaced.model_instances[0].models[0].parts[0].vertices[0].x = 400;
    check(frozen_shape->data().model_instances[0].models[0].parts[0].vertices[0].x == -101,"Published deformation retained mutable input");
    auto hidden = frozen_shape->data(); hidden.model_instances[0].models[0].parts[0].draw = false;
    const auto hidden_frame = Presentation::freeze(hidden);
    check(!hidden_frame->data().model_instances[0].has_render_bounds && queryView(*hidden_frame,view).object_indices.empty(),
        "Hidden part retained stale rendering bounds");
    auto rotated = frozen_shape->data();
    rotated.model_instances[0].current = {};
    auto& rotated_part = rotated.model_instances[0].models[0].parts[0];
    rotated_part.vertices = {{-1,-1,0},{0,1,0},{1,-1,0}};
    rotated_part.current.position = {0,0,10};
    rotated_part.current.right = {0,2,0}; rotated_part.current.up = {-3,0,0};
    const auto rotated_frame = Presentation::freeze(rotated);
    const auto bounds = rotated_frame->data().model_instances[0].render_bounds;
    check(bounds.minimum.x == -3 && bounds.maximum.x == 3 && bounds.minimum.y == -2 && bounds.maximum.y == 2,
        "Rotated/scaled part bounds were not transformed");
    check(queryView(*rotated_frame,view).surfaces.size() == 1,"Valid transformed bounds hid visible geometry");
    // Reference the same posed triangle as baked room geometry, which has no
    // model broad phase. Include near-plane crossings, offscreen placement and
    // deformed vertices outside the simulation sphere.
    for (int sample = 0; sample < 160; ++sample) {
        auto posed = rotated;
        auto& instance = posed.model_instances[0];
        instance.current.position = {float(sample%13-6)*4,float(sample%7-3)*3,float(sample%19)-9};
        instance.models[0].parts[0].vertices[1].x += float(sample%5)*2;
        const auto transform = compose(instance.current,instance.models[0].parts[0].current);
        auto geometry = modelGeometry().parts[0].geometry;
        for (size_t vertex = 0; vertex < geometry.vertices.size(); ++vertex)
            geometry.vertices[vertex] = transformPoint(transform,instance.models[0].parts[0].vertices[vertex]);
        geometry.polygons[0].normal = transformDirection(transform,geometry.polygons[0].normal);
        FrameData reference; reference.sequence = 1; reference.scene_lifetime = assets.lifetime();
        RoomState room; room.number = 0; room.asset = assets.publishRoom(0,geometry);
        room.surfaces.resize(1); room.surfaces[0].bitmap = 1; reference.rooms.push_back(room);
        const auto expected = queryView(*Presentation::freeze(reference),view);
        const auto actual = queryView(*Presentation::freeze(posed),view);
        check(actual.surfaces.size() == expected.surfaces.size(),"Posed bounds disagreed with unbounded triangle query");
    }
    auto lit_shape = frozen_shape->data();
    Light light; light.id.owner = 1; light.radius = 15; light.position = {0,0,10}; light.color = {1,1,1};
    lit_shape.lights.push_back(light); lit_shape.model_instances[0].room = -1;
    prepareLightInfluences(lit_shape);
    check(lit_shape.model_instances[0].models[0].parts[0].polygon_lights[0].size() == 1,"Light query used undeformed geometry");
    auto invalid = modelGeometry(); invalid.parts[0].parent = 0;
    rejects([&] { assets.publishModel(4,invalid); });
    invalid = modelGeometry(); invalid.parts[0].polygon_materials[0] = 0;
    rejects([&] { assets.publishModel(4,invalid); });
    invalid = modelGeometry(); invalid.parts[0].geometry.vertex_normals.clear();
    rejects([&] { assets.publishModel(4,invalid); });
    auto malformed = frame->data(); malformed.model_instances[0].models[0].parts.clear();
    rejects([&] { Presentation::freeze(malformed); });
    malformed = frame->data(); malformed.model_instances[0].models[0].parts[0].vertices = {{0,0,0}};
    rejects([&] { Presentation::freeze(malformed); });
    malformed = frame->data(); malformed.model_instances[0].models[0].parts[0].surfaces[0].baked_uvs.pop_back();
    rejects([&] { Presentation::freeze(malformed); });
    malformed = frame->data(); malformed.model_instances[0].models[0].parts[0].surfaces[0].alpha = std::numeric_limits<float>::quiet_NaN();
    rejects([&] { Presentation::freeze(malformed); });
    auto motion_data = frame->data();
    malformed = frame->data();
    malformed.model_instances[0].models[0].parts[0].surfaces[0].occlusion_class = static_cast<OcclusionClass>(255);
    rejects([&] { Presentation::freeze(malformed); });
    motion_data.model_instances[0].models[0].parts[0].surfaces[0].occlusion_class = OcclusionClass::Model;
    check(Presentation::freeze(motion_data)->data().model_instances[0].models[0].parts[0].surfaces[0].occlusion_class == OcclusionClass::Model,
        "Publication lost explicit material occlusion semantics");
    auto& moving = motion_data.model_instances[0];
    moving.has_previous = true; moving.models[0].parts[0].has_previous = true;
    moving.previous.position = {2,3,4}; moving.models[0].parts[0].previous.position = {5,6,7};
    auto moving_frame = Presentation::freeze(motion_data);
    VisibleSurface moving_surface; moving_surface.model = true; moving_surface.model_instance_index = 0; moving_surface.part_index = 0;
    const auto motion = surfaceMotion(*moving_frame,moving_surface,true);
    check(motion.valid && motion.identity != 0 && motion.previous.position.x == 7 && motion.previous.position.z == 11,
        "Motion did not compose previous object and hierarchical part transforms");
    check(!surfaceMotion(*moving_frame,moving_surface,false).valid,"Model motion ignored camera-history invalidation");
    moving.models[0].parts[0].has_previous = false;
    check(!surfaceMotion(*Presentation::freeze(motion_data),moving_surface,true).valid,
        "New topology/part pose reused an unrelated predecessor");
    moving.models[0].parts[0].has_previous = true; moving.has_previous = false;
    check(!surfaceMotion(*Presentation::freeze(motion_data),moving_surface,true).valid,
        "A newly allocated object claimed valid model motion");
    PreparedModelPart paired;
    paired.previous_vertices = authored.parts[0].geometry.vertices;
    paired.previous_vertices[0].x -= 2;
    const auto pairs = modelPositionPairs(authored.parts[0].geometry,paired);
    check(pairs.size() == 3 && pairs[0].current.x-pairs[0].previous.x == 2,
        "Ending deformation discarded the previous prepared positions");
    paired.vertices = paired.previous_vertices; paired.previous_vertices.clear();
    const auto beginning = modelPositionPairs(authored.parts[0].geometry,paired);
    check(beginning[0].previous.x-beginning[0].current.x == 2,"Starting deformation lost the authored predecessor");
    const auto projected_motion = projectMotion({2,4,0,10},{1,2,0,10},true);
    check(projected_motion.valid && std::abs(projected_motion.velocity.x-.05f) < 1.e-6f &&
        std::abs(projected_motion.velocity.y-.1f) < 1.e-6f,"Clip displacement has wrong direction or UV units");
    check(!projectMotion({2,4,0,10},{1,2,0,-1},true).valid,"Behind-camera geometry fabricated a temporal correspondence");
    check(!projectMotion({2,4,0,10},{1,2,0,10},false).valid,"Missing pose history fabricated a zero-motion correspondence");
    // Every camera selects from one immutable set, including variant-owned
    // glow/facing attachments. No query can swap the object's shared model.
    auto lod_data = frame->data(); lod_data.model_instances.resize(1); lod_data.objects.resize(1);
    auto& lod_object = lod_data.model_instances[0];
    lod_object.models.resize(3,lod_object.models[0]);
    lod_object.lod_distances = {20,40}; lod_object.lod_models = {{0,1,2}};
    for (uint32_t variant = 0; variant < 3; ++variant) {
        lod_object.models[variant].parts[0].surfaces[0].base_color = {float(variant),0,0};
        PreparedMesh attachment;
        attachment.asset = assets.publishMesh(9,unitBillboardQuad());
        attachment.world.position = {0,0,10};
        attachment.surface.bitmap = 1; attachment.surface.unlit = true; attachment.surface.two_sided = true;
        attachment.has_owner = true; attachment.owner = lod_object.owner;
        attachment.model_instance = lod_object.identity; attachment.model_variant = variant;
        lod_data.meshes.push_back(attachment);
    }
    const auto lod_frame = Presentation::freeze(lod_data);
    for (uint32_t variant = 0; variant < 3; ++variant) {
        auto camera_view = view; camera_view.camera.position.z = -float(variant)*20;
        const auto selected = queryView(*lod_frame,camera_view);
        size_t models = 0, attachments = 0;
        for (const auto& surface : selected.surfaces) {
            if (surface.model) {
                ++models;
                check(surface.model_variant == variant && visibleSurfaceBinding(*lod_frame,surface).base_color.x == float(variant),
                    "Camera selected another view's model or material variant");
            }
            if (surface.mesh) {
                ++attachments;
                check(lod_frame->data().meshes[surface.mesh_index].model_variant == int(variant),
                    "LOD query rendered an attachment from another variant");
            }
        }
        check(models == 1 && attachments == 1,"LOD query lost the selected model or duplicated variant attachments");
    }
    check(queryView(*lod_frame,view).surfaces[0].model_variant == 0,"Secondary view mutated main-view LOD selection");
    auto variant_motion = lod_frame->data();
    variant_motion.model_instances[0].has_previous = true;
    for (uint32_t variant = 0; variant < 3; ++variant) {
        auto& pose = variant_motion.model_instances[0].models[variant].parts[0];
        pose.has_previous = true; pose.previous.position.x = float(variant+1);
    }
    const auto variant_motion_frame = Presentation::freeze(variant_motion);
    for (uint32_t variant = 0; variant < 3; ++variant) {
        VisibleSurface surface; surface.model = true; surface.model_variant = variant;
        const auto variant_surface_motion = surfaceMotion(*variant_motion_frame,surface,true);
        check(variant_surface_motion.valid && variant_surface_motion.previous.position.x == float(variant+1),"LOD motion borrowed another variant's pose");
    }
    auto bounded_lod = lod_frame->data();
    bounded_lod.model_instances[0].models[1].parts[0].current.position.x = 100;
    auto medium_view = view; medium_view.camera.position.z = -20;
    const auto bounded_query = queryView(*Presentation::freeze(bounded_lod),medium_view);
    bool wrong_bounds = false;
    for (const auto& surface : bounded_query.surfaces) wrong_bounds = wrong_bounds || surface.model;
    check(!wrong_bounds,"Selected LOD borrowed the base variant's visibility bounds");
    auto boundary_view = view; boundary_view.camera.position.z = -8;
    check(selectModelVariant(lod_object,prepareCamera(boundary_view)) == 1,"Medium LOD threshold is not inclusive");
    boundary_view.camera.position.z = -28;
    check(selectModelVariant(lod_object,prepareCamera(boundary_view)) == 2,"Low LOD threshold is not inclusive");
    lod_object.lod_models[2] = 1;
    check(selectModelVariant(lod_object,prepareCamera(boundary_view)) == 1,"Missing low model did not select the prepared fallback");
    lod_object.lod_models[2] = 3;
    rejects([&] { Presentation::freeze(lod_data); });
    lod_object.lod_models[2] = 2;
    lod_data.meshes[0].model_variant = 3;
    rejects([&] { Presentation::freeze(lod_data); });
    lod_data.meshes[0].model_variant = 0; lod_data.meshes[0].model_instance = 0;
    rejects([&] { Presentation::freeze(lod_data); });
    lod_data.meshes[0].has_owner = true;
    lod_object.lod_distances.x = std::numeric_limits<float>::infinity();
    rejects([&] { Presentation::freeze(lod_data); });
    {
        auto standalone = frame->data();
        standalone.objects.clear();
        for (auto& instance : standalone.model_instances) instance.has_owner = false;
        const auto standalone_frame = Presentation::freeze(standalone);
        const auto standalone_query = queryView(*standalone_frame,view);
        check(standalone_query.surfaces.size() == 2 && standalone_query.object_indices.empty(),
            "Standalone models required or invented gameplay objects");
        auto excluded_view = view; excluded_view.excluded_objects = {42};
        check(queryView(*standalone_frame,excluded_view).surfaces.size() == 2,"Object exclusion hid an unrelated standalone model");
        VisibleSurface standalone_surface; standalone_surface.model = true;
        check(!surfaceMotion(*standalone_frame,standalone_surface,true).valid &&
            surfaceMotion(*standalone_frame,standalone_surface,true).identity == 0,"Standalone model borrowed game motion identity");
        PreparedMesh attachment; attachment.asset = assets.publishMesh(19,unitBillboardQuad());
        attachment.world.position.z = 10; attachment.model_instance = standalone.model_instances[0].identity;
        attachment.model_variant = 0; standalone.meshes.push_back(attachment);
        check(queryView(*Presentation::freeze(standalone),view).surfaces.size() == 3,"Standalone model attachment needed a game owner");
        auto collections = standalone;
        collections.model_instances[0].collection = 17;
        collections.meshes[0].collection = 17;
        RoomState world_room; world_room.number = 22;
        world_room.asset = assets.publishRoom(22,asset->geometry().parts[0].geometry);
        world_room.surfaces.resize(world_room.asset->geometry().polygons.size());
        collections.rooms.push_back(world_room);
        const auto collection_frame = Presentation::freeze(collections);
        auto preview = view; preview.kind = ViewKind::Preview; preview.collection = 17;
        const auto preview_query = queryView(*collection_frame,preview);
        check(preview_query.surfaces.size() == 2 && preview_query.room_indices.empty() &&
            preview_query.object_indices.empty() && !preview_query.exterior_visible,"Collection leaked world content or observations");
        check(queryView(*collection_frame,view).surfaces.size() == 1,"World query admitted another collection's models or attachments");
        auto invalid_view = preview; invalid_view.kind = ViewKind::Main;
        rejects([&] { queryView(*collection_frame,invalid_view); });
        invalid_view = preview; invalid_view.feedback = Automap;
        rejects([&] { queryView(*collection_frame,invalid_view); });
        collections.meshes[0].collection = 0;
        rejects([&] { Presentation::freeze(collections); });
        standalone.model_instances[0].visible = false;
        check(queryView(*Presentation::freeze(standalone),view).surfaces.size() == 1,"Hidden standalone model left an attachment visible");
        standalone.model_instances[0].identity = standalone.model_instances[1].identity;
        rejects([&] { Presentation::freeze(standalone); });
        standalone = frame->data(); standalone.model_instances[0].identity = 0;
        rejects([&] { Presentation::freeze(standalone); });
        standalone = frame->data(); standalone.model_instances[0].owner = 999;
        rejects([&] { Presentation::freeze(standalone); });
        standalone = frame->data(); standalone.model_instances[0].models.clear();
        rejects([&] { Presentation::freeze(standalone); });
        auto multi = frame->data(); multi.objects.resize(1);
        multi.model_instances[1].owner = multi.model_instances[0].owner;
        const auto multi_query = queryView(*Presentation::freeze(multi),view);
        check(multi_query.surfaces.size() == 2 && multi_query.object_indices.size() == 1,
            "Multiple model instances duplicated their game observation owner");
        check(queryView(*Presentation::freeze(multi),excluded_view).surfaces.empty(),
            "Owner exclusion failed to exclude all its model instances");
    }
    assets.removeModel(3);
    check(!assets.model(3) && frame->data().model_instances[0].models[0].asset == asset, "Model unload invalidated held frame");
    assets.reset();
    check(assets.publishModel(3,modelGeometry())->id().lifetime != asset->id().lifetime, "Model slot reuse kept old lifetime");
    malformed = frame->data(); malformed.scene_lifetime = assets.lifetime();
    rejects([&] { Presentation::freeze(malformed); });
    return checks;
}
