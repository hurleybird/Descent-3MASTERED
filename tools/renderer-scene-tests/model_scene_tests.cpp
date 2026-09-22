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
    check(repaired == 2 && compiled.parts[0].geometry.vertex_normals[1].z == -1, "Invalid authored normals not repaired at asset boundary");
    check(std::isnan(normals[1].y), "Asset compiler rewrote source shading data");
    vertices[0].x = 99;
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
    check(assets.publishModel(3,authored) == asset, "Unchanged model lost identity");
    authored.parts[0].geometry.vertices[0].x = -2;
    auto revision = assets.publishModel(3,authored);
    check(revision->id() != asset->id(), "Model edit did not publish new revision");
    check(asset->geometry().parts[0].geometry.vertices[0].x == -1, "Model publication retained mutable geometry");
    FrameData data;
    data.sequence = 1; data.scene_lifetime = assets.lifetime();
    ObjectState object;
    object.handle = 42; object.model = asset; object.current.position.z = 12;
    object.radius = 1;
    PreparedModelPart part;
    part.current.position.z = -2;
    part.surfaces.resize(1); // intentionally flat-color, no bitmap
    part.surfaces[0].baked_uvs = {{0,0},{.5f,1},{1,0}};
    object.parts.push_back(part);
    data.objects.push_back(object);
    data.objects.push_back(object); data.objects[1].handle = 43;
    data.objects[1].current.position.z = 20;
    data.objects[1].parts[0].surfaces[0].alpha = 0.5f;
    auto frame = Presentation::freeze(data);
    data.objects[0].parts[0].surfaces[0].baked_uvs[0].x = 99;
    check(frame->data().objects[0].parts[0].surfaces[0].baked_uvs[0].x == 0,"Instance baked UVs not owned by frame");
    data.objects[0].parts[0].current.position.z = 55;
    check(frame->data().objects[0].parts[0].current.position.z == -2, "Held frame pose changed");
    View view;
    view.history_key = 1; view.viewport = view.projection_anchor = {0,0,1280,720};
    auto result = queryView(*frame,view);
    check(result.surfaces.size() == 2, "Prepared model surfaces missing");
    check(result.surfaces[0].model && result.surfaces[0].world.position.z == 10, "Instance transform not composed");
    check(result.surfaces[1].blended && result.surfaces[1].world.position.z == 18, "Instance material leaked between objects");
    view.exclude_object = true; view.excluded_object = 42;
    auto excluded = queryView(*frame,view);
    check(excluded.surfaces.size() == 1 && excluded.surfaces[0].object_index == 1, "Excluded viewer mesh was rendered");
    check(result.surfaces.size() == 2, "Auxiliary query changed prior surface list");
    view.exclude_object = false; view.camera.position.x = 100;
    check(queryView(*frame,view).surfaces.empty(), "Offscreen posed geometry not rejected");
    // A pose can extend outside an obsolete source sphere: don't reject it on
    // that sphere until animated bounds have been compiled authoritatively.
    auto displaced = frame->data();
    displaced.objects.resize(1); displaced.objects[0].current.position.x = 100;
    displaced.objects[0].parts[0].current.position.x = -100;
    view.camera.position.x = 0;
    auto candidates = queryView(*Presentation::freeze(displaced),view);
    check(candidates.object_indices.empty() && candidates.surfaces.size() == 1, "Source sphere incorrectly culled posed mesh");
    displaced.objects[0].parts[0].current.position.x = 0;
    displaced.objects[0].parts[0].vertices = {{-101,-1,0},{-100,1,0},{-99,-1,0}};
    auto frozen_shape = Presentation::freeze(displaced);
    check(queryView(*frozen_shape,view).surfaces.size() == 1,"Visibility did not consume prepared positions");
    displaced.objects[0].parts[0].vertices[0].x = 400;
    check(frozen_shape->data().objects[0].parts[0].vertices[0].x == -101,"Published deformation retained mutable input");
    auto lit_shape = frozen_shape->data();
    Light light; light.id.owner = 1; light.radius = 15; light.position = {0,0,10}; light.color = {1,1,1};
    lit_shape.lights.push_back(light); lit_shape.objects[0].room = -1;
    prepareLightInfluences(lit_shape);
    check(lit_shape.objects[0].parts[0].surfaces[0].light_indices.size() == 1,"Light query used undeformed geometry");
    auto invalid = modelGeometry(); invalid.parts[0].parent = 0;
    rejects([&] { assets.publishModel(4,invalid); });
    invalid = modelGeometry(); invalid.parts[0].polygon_materials[0] = 0;
    rejects([&] { assets.publishModel(4,invalid); });
    invalid = modelGeometry(); invalid.parts[0].geometry.vertex_normals.clear();
    rejects([&] { assets.publishModel(4,invalid); });
    auto malformed = frame->data(); malformed.objects[0].parts.clear();
    rejects([&] { Presentation::freeze(malformed); });
    malformed = frame->data(); malformed.objects[0].parts[0].vertices = {{0,0,0}};
    rejects([&] { Presentation::freeze(malformed); });
    malformed = frame->data(); malformed.objects[0].parts[0].surfaces[0].baked_uvs.pop_back();
    rejects([&] { Presentation::freeze(malformed); });
    malformed = frame->data(); malformed.objects[0].parts[0].surfaces[0].alpha = std::numeric_limits<float>::quiet_NaN();
    rejects([&] { Presentation::freeze(malformed); });
    auto motion_data = frame->data();
    malformed = frame->data();
    malformed.objects[0].parts[0].surfaces[0].occlusion_class = static_cast<OcclusionClass>(255);
    rejects([&] { Presentation::freeze(malformed); });
    motion_data.objects[0].parts[0].surfaces[0].occlusion_class = OcclusionClass::Model;
    check(Presentation::freeze(motion_data)->data().objects[0].parts[0].surfaces[0].occlusion_class == OcclusionClass::Model,
        "Publication lost explicit material occlusion semantics");
    auto& moving = motion_data.objects[0];
    moving.has_previous = true; moving.parts[0].has_previous = true;
    moving.previous.position = {2,3,4}; moving.parts[0].previous.position = {5,6,7};
    auto moving_frame = Presentation::freeze(motion_data);
    VisibleSurface moving_surface; moving_surface.model = true; moving_surface.object_index = 0; moving_surface.part_index = 0;
    const auto motion = surfaceMotion(*moving_frame,moving_surface,true);
    check(motion.valid && motion.identity != 0 && motion.previous.position.x == 7 && motion.previous.position.z == 11,
        "Motion did not compose previous object and hierarchical part transforms");
    check(!surfaceMotion(*moving_frame,moving_surface,false).valid,"Model motion ignored camera-history invalidation");
    moving.parts[0].has_previous = false;
    check(!surfaceMotion(*Presentation::freeze(motion_data),moving_surface,true).valid,
        "New topology/part pose reused an unrelated predecessor");
    moving.parts[0].has_previous = true; moving.has_previous = false;
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
    assets.removeModel(3);
    check(!assets.model(3) && frame->data().objects[0].model == asset, "Model unload invalidated held frame");
    assets.reset();
    check(assets.publishModel(3,modelGeometry())->id().lifetime != asset->id().lifetime, "Model slot reuse kept old lifetime");
    malformed = frame->data(); malformed.scene_lifetime = assets.lifetime();
    rejects([&] { Presentation::freeze(malformed); });
    return checks;
}
