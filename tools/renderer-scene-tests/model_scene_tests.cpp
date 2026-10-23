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
    check(sizeof(RoomSurfaces) == sizeof(std::shared_ptr<void>) && sizeof(ModelParts) == sizeof(std::shared_ptr<void>),
        "Retained binding handles duplicated ownership of table metadata");
    {
        SceneAssets fixtures;
        auto geometry = modelGeometry();
        auto& source = geometry.parts[0];
        source.geometry.polygons.resize(6,source.geometry.polygons[0]);
        source.polygon_materials.assign(6,-1);
        source.polygon_colors = {{1,0,0},{1,0,0},{0,1,0},{0,1,0},{1,0,0},{1,0,0}};
        const auto asset = fixtures.publishModel(1,geometry);
        const auto& groups = asset->geometry().parts[0].surface_groups;
        check(groups.size() == 2 && groups[0] == std::vector<uint32_t>({0,1,4,5}) &&
            groups[1] == std::vector<uint32_t>({2,3}),"Material groups lost authored primitive membership");
        FrameData input; input.sequence = 1; input.scene_lifetime = fixtures.lifetime();
        ModelInstance instance; instance.identity = 1; instance.current.position.z = 10;
        PreparedModel model; model.asset = asset;
        PreparedModelPart part; part.material_palette = true; part.surfaces.resize(2);
        model.parts.push_back(part); input.appendModel(instance,model); input.model_instances.push_back(instance);
        View request; request.history_key = 1; request.viewport = request.projection_anchor = {0,0,800,600};
        request.portal_visibility = false;
        const auto grouped = Presentation::freeze(input);
        const auto visible = queryView(*grouped,request);
        check(visible.surfaces.size() == 2,"Opaque model was not submitted as material groups");
        check(visible.geometry.models == std::vector<std::pair<uint32_t,uint32_t>>({{0,0}}) &&
            visible.geometry.parts == std::vector<uint32_t>({0}),"Model demand was duplicated per material group");
        size_t polygons = 0;
        for (const auto& item : visible.surfaces) {
            check(item.model && item.model_material_group,"Model group lost its primitive extent");
            polygons += groups[asset->geometry().parts[0].surface_for_polygon[item.polygon_index]].size();
        }
        check(polygons == 6,"Model batching lost primitives");
        input.model_variants[0].modulation.alpha = .5f;
        const auto transparent = queryView(*Presentation::freeze(input),request);
        check(transparent.surfaces.size() == 6,"Instance transparency lost per-face ordering");
        for (size_t i = 0; i < transparent.surfaces.size(); ++i) {
            const auto& item = transparent.surfaces[i];
            check(!item.model_material_group && item.blended && item.polygon_index == i,"Blended geometry lost authored tie order");
        }
        input.model_variants[0].modulation.alpha = 1;
        for (auto& surface : input.model_variants[0].parts[0].surfaces) surface.depth = DepthMode::Disabled;
        const auto painter = queryView(*Presentation::freeze(input),request);
        check(painter.surfaces.size() == 6,"Depth-independent geometry was reordered by material");
        for (auto& surface : input.model_variants[0].parts[0].surfaces) surface.depth = DepthMode::Automatic;
        input.model_variants[0].parts[0].vertices = source.geometry.vertices;
        input.model_variants[0].parts[0].vertices[0].x -= .1f;
        const auto deformed = queryView(*Presentation::freeze(input),request);
        check(deformed.surfaces.size() == 6,"Deformed geometry used an unproven winding batch");
        input.model_variants[0].parts[0].vertices.clear();
        check(queryView(*Presentation::freeze(input),request).surfaces.size() == 2 && queryView(*grouped,request).surfaces.size() == 2,
            "Batch eligibility transition changed a held frame or retained stale deformation");
        ObjectState camera_owner; camera_owner.handle = 42; input.objects.push_back(camera_owner);
        auto& shared_model = input.model_instances[0]; shared_model.alternate_collection = 99;
        shared_model.primary_view_style.camera_owner = 42; shared_model.primary_view_style.match = CameraMatch::Except;
        shared_model.primary_view_style.camera_kinds = 1u << uint32_t(ViewKind::Main);
        const auto shared = Presentation::freeze(input);
        request.attached_camera = true; request.camera_owner = 42;
        check(queryView(*shared,request).surfaces.empty(),"Camera-local model remained in its main world audience");
        request.kind = ViewKind::Auxiliary;
        check(queryView(*shared,request).surfaces.size() == 2,"Shared model disappeared from auxiliary world view");
        request.kind = ViewKind::Preview; request.collection = 99;
        check(queryView(*shared,request).surfaces.size() == 2 && shared->data().model_instances.size() == 1 &&
            shared->data().model_variants.size() == 1,"Alternate model audience duplicated pose or lost geometry");
        input.model_instances[0].alternate_collection = 100;
        check(queryView(*Presentation::freeze(input),request).surfaces.empty() && queryView(*shared,request).surfaces.size() == 2,
            "Model audience change reused stale membership or changed held geometry");
    }
    {
        uint32_t seed = 0x42b07u;
        const auto random = [&]() { seed = seed*1664525u+1013904223u; return (int(seed>>8)-8388608)/8388608.f; };
        for (int sample = 0; sample < 12000; ++sample) {
            const float scale = sample%3 == 0 ? 100000.f : sample%3 == 1 ? .00001f : 100.f;
            Transform placement;
            placement.position = {random()*scale,random()*scale,random()*scale};
            placement.right = {random()*3,random()*3,random()*3};
            placement.up = {random()*3,random()*3,random()*3};
            placement.forward = {random()*3,random()*3,random()*3};
            Bounds box;
            box.minimum = {random()*scale,random()*scale,random()*scale};
            box.maximum = {box.minimum.x+std::abs(random())*scale,
                box.minimum.y+std::abs(random())*scale,box.minimum.z+std::abs(random())*scale};
            Bounds reference; reference.minimum = reference.maximum = transformPoint(placement,box.minimum);
            for (unsigned corner = 1; corner < 8; ++corner) {
                const auto p = transformPoint(placement,{corner&1 ? box.maximum.x : box.minimum.x,
                    corner&2 ? box.maximum.y : box.minimum.y,corner&4 ? box.maximum.z : box.minimum.z});
                reference.minimum = {std::min(reference.minimum.x,p.x),std::min(reference.minimum.y,p.y),std::min(reference.minimum.z,p.z)};
                reference.maximum = {std::max(reference.maximum.x,p.x),std::max(reference.maximum.y,p.y),std::max(reference.maximum.z,p.z)};
            }
            const auto actual = transformBounds(placement,box);
            check(actual.minimum.x == reference.minimum.x && actual.minimum.y == reference.minimum.y &&
                actual.minimum.z == reference.minimum.z && actual.maximum.x == reference.maximum.x &&
                actual.maximum.y == reference.maximum.y && actual.maximum.z == reference.maximum.z,
                "Support-corner transform changed bounds rounding or missed reflected/scaled geometry");
        }
    }
    {
        ModelBindingInputs key;
        check(!(key == key),"Uninitialized producer binding inputs permitted reuse");
        key.valid = true; key.subobject_mask = UINT32_MAX; key.material_palette = true;
        check(key == key,"Unchanged complete model binding inputs failed reuse");
        for (size_t i = 0; i < key.appearance.size(); ++i) {
            auto changed = key; changed.appearance[i] = 1;
            check(!(key == changed),"Model binding reuse ignored an appearance input");
        }
        auto changed = key; changed.subobject_mask ^= 0x80000000u;
        check(!(key == changed),"Model binding reuse rounded a subobject mask");
        changed = key; changed.baked_layout_revision = 1;
        check(!(key == changed),"Model binding reuse ignored saved UV layout revision");
        changed = key; changed.material_palette = false;
        check(!(key == changed),"Model binding reuse confused baked and palette surfaces");
        changed = key; changed.valid = false;
        check(!(key == changed),"Model binding reuse accepted uninitialized history");
    }
    // Instance opacity must produce the same pipeline as baking it into each
    // surface, without changing the immutable binding table.
    for (auto blend : {BlendMode::Solid,BlendMode::Alpha,BlendMode::Additive,BlendMode::Multiply})
    for (auto depth : {DepthMode::Automatic,DepthMode::ReadWrite,DepthMode::ReadOnly,DepthMode::Disabled})
    for (float authored_alpha : {0.f,.4f,1.f}) for (float instance_alpha : {0.f,.25f,1.f}) {
        SurfaceBinding surface; surface.alpha = authored_alpha; surface.blend = blend; surface.depth = depth;
        surface.material = compileMaterialPolicy(surface,MeshPolygon{});
        auto baked = surface; baked.alpha *= instance_alpha;
        const auto expected = compileMaterialPolicy(baked,MeshPolygon{});
        const auto actual = modulatedMaterialPolicy(surface,instance_alpha);
        check(actual.blend == expected.blend && actual.depth == expected.depth && actual.texture_alpha == expected.texture_alpha,
            "Instance opacity changed authored compositing policy");
    }
    {
        SurfaceBinding surface; surface.ambient_color = {.2f,.4f,.8f}; surface.color_modulation = {.5f,1,.25f}; surface.alpha = .5f;
        const auto values = modulatedSurface(surface,{{.5f,.25f,1},{.2f,.4f,.8f},.4f});
        check(values.ambient.x == .1f && values.ambient.y == .1f && values.color.x == .1f && values.color.z == .2f && values.alpha == .2f,
            "Instance radiance modulation lost a material or instance factor");
    }
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
    u[0] = std::numeric_limits<float>::quiet_NaN();
    v[1] = std::numeric_limits<float>::infinity();
    face.texnum = 0; source_model.n_textures = 1; source_model.textures[0] = 0;
    size_t repaired_uvs = 0;
    const auto repaired_uv_model = CompileSceneModelAsset(source_model,nullptr,&repaired_uvs);
    check(repaired_uvs == 2 && repaired_uv_model.parts[0].geometry.polygons[0].corners[0].u == 0 &&
        repaired_uv_model.parts[0].geometry.polygons[0].corners[1].v == 0 &&
        repaired_uv_model.parts[0].geometry.polygons[0].corners[2].u == 1,
        "Invalid imported UVs were not repaired component-wise");
    check(std::isnan(u[0]) && std::isinf(v[1]),"UV repair mutated authored source");
    check(MatchesSceneModelAsset(repaired_uv_model,source_model),"Repaired UVs continually invalidate model assets");
    {
        SceneAssets repair_assets;
        check(bool(repair_assets.publishModel(0,repaired_uv_model)),"Repaired imported model failed scene validation");
        auto invalid_uv_model = repaired_uv_model;
        invalid_uv_model.parts[0].geometry.polygons[0].corners[0].u = std::numeric_limits<float>::quiet_NaN();
        rejects([&] { repair_assets.publishModel(1,invalid_uv_model); });
    }
    u[0] = 0; v[1] = 1; face.texnum = -1; source_model.n_textures = 0;
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
    {
        auto geometry = modelGeometry();
        auto animation = std::make_shared<ModelAnimation>();
        animation->n_models = 1; animation->num_key_pos = 2; animation->submodel.resize(1);
        animation->submodel[0].keyframe_pos = {{0,0,0},{4,2,-1}};
        geometry.animation = animation; geometry.rest_pose.push_back(Transform{});
        const auto rest_asset = assets.publishModel(97,geometry);
        for (bool current_keys : {false,true}) for (bool previous_keys : {false,true}) for (bool history : {false,true}) {
            FrameData input; input.sequence = 1; input.scene_lifetime = assets.lifetime(); input.simulation_time = 19;
            ModelInstance instance; instance.identity = 100;
            PreparedModel model; model.asset = rest_asset; model.deferred_pose = true;
            model.previous_pose_valid = history; model.previous_pose_time = 18;
            const float current_key = .75f, previous_key = .25f;
            if (current_keys) model.pose_inputs = input.appendPoseInputs(&current_key,1);
            if (previous_keys) model.previous_pose_inputs = input.appendPoseInputs(&previous_key,1);
            model.parts.resize(1); model.parts[0].surfaces.resize(1);
            input.appendModel(instance,std::move(model)); input.model_instances.push_back(instance);
            const auto frame = Presentation::freeze(input);
            auto retained = frame->data(); ++retained.sequence;
            const auto next = Presentation::freeze(retained);
            const auto& bounds = next->data().model_instances[0].render_bounds;
            const auto& local = rest_asset->geometry().parts[0].local_bounds;
            if (!current_keys) {
                check(bounds.minimum.x == local.minimum.x && bounds.maximum.x == local.maximum.x &&
                    bounds.minimum.z == local.minimum.z && bounds.maximum.z == local.maximum.z,
                    "Resting model still uses its full animation envelope");
                retained.model_variants[0].deformation = .2f;
                const auto deformed = Presentation::freeze(retained);
                check(deformed->data().model_instances[0].render_bounds.maximum.x > bounds.maximum.x,
                    "Deformation reused an invalid rest bound");
            } else check(bounds.maximum.x > local.maximum.x,"Keyed animation reused an invalid rest bound");
            const auto& pose = frame->resolvedModel(0,0).parts[0];
            const auto& source = input.modelsFor(0)[0];
            const auto expected = ComposeModelPose(*animation,EvaluateModelPose(*animation,
                input.poseInputs(source.pose_inputs).data(),19));
            const auto prior = history ? ComposeModelPose(*animation,EvaluateModelPose(*animation,
                input.poseInputs(source.previous_pose_inputs).data(),18)) : expected;
            check(pose.current.position.x == expected.submodels[0].translation.x &&
                pose.current.position.y == expected.submodels[0].translation.y &&
                pose.previous.position.x == prior.submodels[0].translation.x &&
                pose.previous.position.z == prior.submodels[0].translation.z,
                "Rest-pose reuse confused current/previous keyed and unkeyed animation");
        }
        assets.removeModel(97);
    }
    {
        auto geometry = modelGeometry();
        check(!assets.publishModel(98,geometry)->hasViewAttachments(),"Ordinary model requires attachment traversal");
        for (auto flags : {SOF_FACING,SOF_GLOW,SOF_THRUSTER}) {
            geometry.parts[0].authored_flags = flags;
            check(assets.publishModel(98,geometry)->hasViewAttachments(),"Model attachment metadata was lost");
        }
        geometry.parts[0].authored_flags = 0;
        check(!assets.publishModel(98,geometry)->hasViewAttachments(),"Removed attachment retained stale asset metadata");
        assets.removeModel(98);
    }
    {
        auto geometry = modelGeometry();
        geometry.parts.push_back(geometry.parts[0]);
        geometry.parts.push_back(geometry.parts[0]);
        geometry.parts[1].parent = 0;
        geometry.parts[1].authored_flags = SOF_FACING | SOF_CUSTOM;
        geometry.parts[2].parent = 1;
        geometry.parts[2].authored_flags = SOF_GLOW;
        for (const auto parent_flags : {uint32_t(0),uint32_t(SOF_SHELL),uint32_t(SOF_CUSTOM)}) {
            geometry.parts[0].authored_flags = parent_flags;
            const auto asset = assets.publishModel(98,geometry);
            check(asset->viewAttachments() == std::vector<uint32_t>({1,2}),"Attachment inventory lost authored order");
            for (uint32_t mask = 0; mask < 8; ++mask) for (bool custom : {false,true}) {
                const auto roles = ResolveSceneModelRoles(*asset,mask,custom);
                std::vector<uint32_t> expected,actual;
                for (uint32_t i = 0; i < roles.size(); ++i)
                    if (roles[i] == SceneModelPartRole::Facing || roles[i] == SceneModelPartRole::Glow) expected.push_back(i);
                for (const auto i : asset->viewAttachments()) {
                    const auto& part = asset->geometry().parts[i];
                    const auto role = part.hierarchy_hidden ? SceneModelPartRole::Hidden :
                        ResolveSceneModelPartRole(part.authored_flags,mask,i,custom);
                    if (role == SceneModelPartRole::Facing || role == SceneModelPartRole::Glow) actual.push_back(i);
                }
                check(actual == expected,"Sparse attachments changed mask/custom/hierarchy semantics");
            }
        }
        assets.removeModel(98);
    }
    {
        auto roles_geometry = modelGeometry();
        roles_geometry.parts.push_back(roles_geometry.parts[0]);
        roles_geometry.parts[1].parent = 0;
        for (uint32_t flags : {uint32_t(0),uint32_t(SOF_SHELL),uint32_t(SOF_FRONTFACE),uint32_t(SOF_CUSTOM),uint32_t(SOF_GLOW)}) {
            roles_geometry.parts[0].authored_flags = flags;
            const auto roles_asset = assets.publishModel(81,roles_geometry);
            for (uint32_t mask = 0; mask < 4; ++mask) for (bool custom : {false,true})
                check(ResolveSceneModelRoles(*roles_asset,mask,custom) == ResolveSceneModelRoles(roles_geometry,mask,custom),
                    "Compiled role hierarchy differs from authored traversal");
            roles_geometry = roles_asset->geometry(); // derived fields must be recomputed after edits
        }
    }
    auto animation_geometry = compiled;
    auto mutable_animation = std::make_shared<ModelAnimation>(*compiled.animation);
    animation_geometry.animation = mutable_animation;
    const auto animated_asset = assets.publishModel(7,animation_geometry);
    mutable_animation->submodel[0].rps = 123;
    check(animated_asset->geometry().animation->submodel[0].rps != 123,"Published animation retained a mutable builder alias");
    check(assets.publishModel(7,animation_geometry)->id() != animated_asset->id(),"Changed animation did not revise its model asset");
    animation_geometry.animation = std::make_shared<ModelAnimation>();
    rejects([&] { assets.publishModel(7,animation_geometry); });
    auto authored = modelGeometry();
    const auto shape = deformMesh(authored.parts[0].geometry,.2f,3.5,42,1);
    const auto same_shape = deformMesh(authored.parts[0].geometry,.2f,3.5,42,1);
    const auto other_shape = deformMesh(authored.parts[0].geometry,.2f,3.5,43,1);
    check(shape[0].x == same_shape[0].x && shape[1].y == same_shape[1].y,"Repeated deformation advanced visual state");
    check(shape[0].x != other_shape[0].x || shape[1].y != other_shape[1].y,"Deformation lost instance identity");
    check(std::abs(shape[0].x+1) <= .2f && authored.parts[0].geometry.vertices[0].x == -1,"Deformation mutated or escaped authored range");
    check(deformMesh(authored.parts[0].geometry,0,3.5,42,1).empty(),"Rigid instance duplicated authored positions");
    auto reused_shape = shape;
    const auto* deformation_storage = reused_shape.data();
    deformMeshInto(authored.parts[0].geometry,0,3.5,42,1,reused_shape);
    check(reused_shape.empty(),"Reused rigid deformation retained stale vertices");
    deformMeshInto(authored.parts[0].geometry,.2f,3.5,42,1,reused_shape);
    check(reused_shape.data() == deformation_storage && reused_shape.size() == shape.size(),
        "Deformation discarded reusable vertex storage");
    for (size_t i = 0; i < shape.size(); ++i)
        check(reused_shape[i].x == shape[i].x && reused_shape[i].y == shape[i].y && reused_shape[i].z == shape[i].z,
            "In-place deformation changed the visual recipe");
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
        check(palette.surface_groups.size() == 2 && palette.surface_groups[0] == std::vector<uint32_t>({0,1}) &&
            palette.surface_groups[1] == std::vector<uint32_t>({2}),"Compiled material groups crossed an appearance boundary");
    }
    check(assets.publishModel(3,authored) == asset, "Unchanged model lost identity");
    authored.parts[0].geometry.vertices[0].x = -2;
    auto revision = assets.publishModel(3,authored);
    check(revision->id() != asset->id(), "Model edit did not publish new revision");
    check(asset->geometry().parts[0].geometry.vertices[0].x == -1, "Model publication retained mutable geometry");
    FrameData data;
    data.sequence = 1; data.scene_lifetime = assets.lifetime();
    ModelInstance object;
    data.appendModel(object,PreparedModel{});
    object.identity = 42; data.variants(object)[0].asset = asset; object.current.position.z = 12;
    object.has_owner = true; object.owner = 42;
    ObjectState owner; owner.handle = 42; owner.radius = 1; owner.current = object.current;
    data.objects.push_back(owner); owner.handle = 43; data.objects.push_back(owner);
    PreparedModelPart part;
    part.current.position.z = -2;
    part.surfaces.resize(1); // intentionally flat-color, no bitmap
    part.surfaces[0].baked_uvs = {{0,0},{.5f,1},{1,0}};
    data.variants(object)[0].parts.push_back(part);
    data.model_instances.push_back(object);
    object.model_count = 0; data.appendModel(object,data.modelsFor(0)[0]);
    data.model_instances.push_back(object); data.model_instances[1].identity = 43; data.model_instances[1].owner = 43;
    data.model_instances[1].current.position.z = 20;
    data.modelsFor(1)[0].parts[0].surfaces[0].alpha = 0.5f;
    auto frame = Presentation::freeze(data);
    {
        check(frame->data().model_instances[0].compiled_owner==0 &&
            frame->data().model_instances[1].compiled_owner==1,"Model owner ordinals were not compiled");
        auto linked=frame->data();
        PreparedMesh attachment;
        attachment.asset=assets.publishMesh(999,asset->geometry().parts[0].geometry);
        attachment.has_owner=true; attachment.owner=42;
        attachment.model_instance=linked.model_instances[0].identity;
        attachment.model_variant=0; attachment.model_part=0;
        attachment.compiled_owner=attachment.compiled_model=999; // stale/untrusted builder hints
        linked.meshes.push_back(attachment);
        const auto held=Presentation::freeze(linked);
        check(held->data().meshes.back().compiled_owner==0 && held->data().meshes.back().compiled_model==0,
            "Publication trusted a builder's attachment ordinal");
        linked=held->data();
        std::swap(linked.objects[0],linked.objects[1]);
        linked.meshes.back().model_instance=linked.model_instances[1].identity;
        const auto reordered=Presentation::freeze(linked);
        check(reordered->data().model_instances[0].compiled_owner==1 &&
            reordered->data().model_instances[1].compiled_owner==0 &&
            reordered->data().meshes.back().compiled_owner==1 && reordered->data().meshes.back().compiled_model==1,
            "Reordered frame retained stale ownership/attachment ordinals");
        check(held->data().model_instances[0].compiled_owner==0 && held->data().meshes.back().compiled_model==0,
            "Recompiling ownership changed a held frame");
        auto generation_changed = held->data();
        generation_changed.objects[0].handle += 0x10000;
        rejects([&] { Presentation::freeze(generation_changed); });
        auto invalid_hint = held->data();
        invalid_hint.model_instances[0].compiled_owner = size_t(-2);
        invalid_hint.meshes.back().compiled_owner = 1;
        const auto repaired_hint = Presentation::freeze(invalid_hint);
        check(repaired_hint->data().model_instances[0].compiled_owner == 0 &&
            repaired_hint->data().meshes.back().compiled_owner == 0,"Invalid owner hints bypassed identity resolution");
        linked.model_instances[0].has_owner=false; linked.meshes.back().has_owner=false;
        linked.meshes.back().model_instance=0; linked.meshes.back().model_variant=linked.meshes.back().model_part=-1;
        const auto detached=Presentation::freeze(linked);
        check(detached->data().model_instances[0].compiled_owner==size_t(-1) &&
            detached->data().meshes.back().compiled_owner==size_t(-1) &&
            detached->data().meshes.back().compiled_model==size_t(-1),"Detached entities retained compiled dependencies");
    }
    {
        auto composed = frame->data();
        const Transform pose[] = {part.current};
        const auto range = composed.appendModelTransforms(pose,1);
        for (auto& model : composed.model_variants) { model.deferred_pose = true; model.composed_pose = range; }
        const auto explicit_frame = Presentation::freeze(composed);
        check(!asset->geometry().animation && explicit_frame->resolvedModel(0,0).parts[0].current.position.z == part.current.position.z &&
            explicit_frame->resolvedModel(1,0).parts[0].current.position.z == part.current.position.z,
            "Composed pose required animation tracks or lost a shared range");
        composed.model_transforms[0].position.z = 99;
        check(explicit_frame->resolvedModel(0,0).parts[0].current.position.z == part.current.position.z,
            "Composed pose retained mutable producer transforms");
    }
    check(frame->data().compiled_part_count == 2 && frame->data().modelsFor(0)[0].parts[0].compiled_index == 0 &&
        frame->data().modelsFor(1)[0].parts[0].compiled_index == 1,"Model parts lack unique packed addresses");
    {
        auto invalid = frame->data(); invalid.model_instances[1].model_begin = 0;
        rejects([&] { Presentation::freeze(invalid); });
        invalid = frame->data(); invalid.model_instances[1].model_count = UINT32_MAX;
        rejects([&] { Presentation::freeze(invalid); });
        invalid = frame->data(); invalid.model_variants.push_back(invalid.model_variants[0]);
        rejects([&] { Presentation::freeze(invalid); });
        invalid = frame->data();
        rejects([&] { invalid.appendModel(invalid.model_instances[0],invalid.model_variants[0]); });
        check(invalid.model_variants.size() == frame->data().model_variants.size(),"Invalid append modified frame storage");
        auto filtered = frame->data(); filtered.model_instances.erase(filtered.model_instances.begin());
        filtered.compactModels();
        const auto compact = Presentation::freeze(filtered);
        check(compact->data().model_variants.size() == 1 && compact->data().model_instances[0].model_begin == 0 &&
            compact->data().modelsFor(0)[0].parts[0].surfaces[0].alpha == .5f &&
            frame->data().modelsFor(0)[0].parts[0].surfaces[0].alpha == 1,
            "Compacted model ranges changed ownership or a held frame");
    }
    {
        auto retained = frame->data();
        const auto again = Presentation::freeze(retained);
        check(&again->data().modelsFor(0)[0].parts[0].surfaces.read() ==
            &frame->data().modelsFor(0)[0].parts[0].surfaces.read(),"Unchanged model table was copied");
        retained.modelsFor(0)[0].parts[0].material_palette = true;
        rejects([&] { Presentation::freeze(retained); }); // one-entry palette must not hide baked per-face UVs
        check(frame->objectIndices().at(42) == 0 && frame->modelIndices().at(43) == 1,"Compiled ownership indices are incorrect");
    }
    check(frame->data().model_instances[0].has_render_bounds && frame->data().model_instances[0].render_bounds.minimum.z == 10 &&
        frame->data().model_instances[0].render_bounds.maximum.x == 1,"Publication did not compile world-space posed bounds");
    data.modelsFor(0)[0].parts[0].surfaces[0].baked_uvs[0].x = 99;
    check(frame->data().modelsFor(0)[0].parts[0].surfaces[0].baked_uvs[0].x == 0,"Instance baked UVs not owned by frame");
    data.modelsFor(0)[0].parts[0].current.position.z = 55;
    check(frame->data().modelsFor(0)[0].parts[0].current.position.z == -2, "Held frame pose changed");
    View view;
    view.history_key = 1; view.viewport = view.projection_anchor = {0,0,1280,720};
    auto result = queryView(*frame,view);
    check(result.surfaces.size() == 2, "Prepared model surfaces missing");
    check(result.surfaces[0].model && result.world(result.surfaces[0]).position.z == 10, "Instance transform not composed");
    check(result.surfaces[1].blended && result.world(result.surfaces[1]).position.z == 18, "Instance material leaked between objects");
    view.excluded_objects = {42};
    auto excluded = queryView(*frame,view);
    check(excluded.surfaces.size() == 1 && excluded.surfaces[0].model_instance_index == 1, "Excluded viewer mesh was rendered");
    check(result.surfaces.size() == 2, "Auxiliary query changed prior surface list");
    view.excluded_objects.clear(); view.camera.position.x = 100;
    check(queryView(*frame,view).surfaces.empty(), "Offscreen posed geometry not rejected");
    // A pose can extend outside an obsolete source sphere. Both geometry and
    // the candidate object list must use the published rendering bounds.
    auto displaced = frame->data();
    displaced.model_instances.resize(1); displaced.model_variants.resize(1); displaced.model_instances[0].current.position.x = 100;
    displaced.objects.resize(1); displaced.objects[0].current.position.x = 100;
    displaced.modelsFor(0)[0].parts[0].current.position.x = -100;
    view.camera.position.x = 0;
    auto candidates = queryView(*Presentation::freeze(displaced),view);
    check(candidates.object_indices.size() == 1 && candidates.surfaces.size() == 1, "Source sphere incorrectly culled posed mesh");
    displaced.modelsFor(0)[0].parts[0].current.position.x = 0;
    displaced.modelsFor(0)[0].parts[0].vertices = {{-101,-1,0},{-100,1,0},{-99,-1,0}};
    auto frozen_shape = Presentation::freeze(displaced);
    check(frozen_shape->data().model_instances[0].render_bounds.minimum.x == -1 &&
        frozen_shape->data().model_instances[0].render_bounds.maximum.x == 1,"Deformation bounds used authored positions");
    check(queryView(*frozen_shape,view).surfaces.size() == 1,"Visibility did not consume prepared positions");
    displaced.modelsFor(0)[0].parts[0].vertices[0].x = 400;
    check(frozen_shape->data().modelsFor(0)[0].parts[0].vertices[0].x == -101,"Published deformation retained mutable input");
    auto hidden = frozen_shape->data(); hidden.modelsFor(0)[0].parts[0].draw = false;
    const auto hidden_frame = Presentation::freeze(hidden);
    check(!hidden_frame->data().model_instances[0].has_render_bounds && queryView(*hidden_frame,view).object_indices.empty(),
        "Hidden part retained stale rendering bounds");
    auto rotated = frozen_shape->data();
    rotated.model_instances[0].current = {};
    auto& rotated_part = rotated.modelsFor(0)[0].parts[0];
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
        posed.variants(instance)[0].parts[0].vertices[1].x += float(sample%5)*2;
        const auto transform = compose(instance.current,posed.variants(instance)[0].parts[0].current);
        auto geometry = modelGeometry().parts[0].geometry;
        for (size_t vertex = 0; vertex < geometry.vertices.size(); ++vertex)
            geometry.vertices[vertex] = transformPoint(transform,posed.variants(instance)[0].parts[0].vertices[vertex]);
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
    const auto& lit_part = lit_shape.modelsFor(0)[0].parts[0];
    const auto& lit_geometry = lit_shape.modelsFor(0)[0].asset->geometry().parts[0].geometry;
    std::vector<int32_t> lit_indices;
    appendSurfaceLightIndices(lit_shape,lit_part.light_candidates,MeshView(lit_geometry,lit_part.vertices),
        lit_geometry.polygons[0],compose(lit_shape.model_instances[0].current,lit_part.current),lit_indices);
    check(lit_indices.size() == 1,"Light query used undeformed geometry");
    {
        const auto held = Presentation::freeze(lit_shape);
        auto changed = held->data();
        changed.light_candidate_indices.clear();
        rejects([&] { Presentation::freeze(changed); });
        changed = held->data(); changed.light_candidate_indices[0] = UINT32_MAX;
        rejects([&] { Presentation::freeze(changed); });
        check(held->data().lightCandidates(held->data().modelsFor(0)[0].parts[0].light_candidates)[0] == 0,
            "Candidate table retained a mutable frame alias");
        changed = lit_shape;
        changed.modelsFor(0)[0].deferred_pose = true;
        ModelInstance sibling = changed.model_instances[0];
        sibling.identity += 100; sibling.model_count = 0;
        changed.appendModel(sibling,changed.modelsFor(0)[0]);
        changed.model_instances.push_back(sibling);
        prepareLightInfluences(changed);
        const auto a = changed.modelsFor(0)[0].light_candidates, b = changed.modelsFor(1)[0].light_candidates;
        check(a.offset == b.offset && a.count == 1 && b.count == 1 && changed.light_candidate_indices.size() == 1,
            "Equivalent deferred instances duplicated emitter lists");
        changed.model_instances[1].room = 9999;
        prepareLightInfluences(changed);
        check(changed.modelsFor(1)[0].light_candidates.empty(),"Unknown interior inherited exterior emitters");
    }
    auto invalid = modelGeometry(); invalid.parts[0].parent = 0;
    rejects([&] { assets.publishModel(4,invalid); });
    invalid = modelGeometry(); invalid.parts[0].polygon_materials[0] = 0;
    rejects([&] { assets.publishModel(4,invalid); });
    invalid = modelGeometry(); invalid.parts[0].geometry.vertex_normals.clear();
    rejects([&] { assets.publishModel(4,invalid); });
    auto malformed = frame->data(); malformed.modelsFor(0)[0].parts.clear();
    rejects([&] { Presentation::freeze(malformed); });
    malformed = frame->data(); malformed.modelsFor(0)[0].parts[0].vertices = {{0,0,0}};
    rejects([&] { Presentation::freeze(malformed); });
    malformed = frame->data(); malformed.modelsFor(0)[0].parts[0].surfaces[0].baked_uvs.pop_back();
    rejects([&] { Presentation::freeze(malformed); });
    malformed = frame->data(); malformed.modelsFor(0)[0].parts[0].surfaces[0].alpha = std::numeric_limits<float>::quiet_NaN();
    rejects([&] { Presentation::freeze(malformed); });
    malformed = frame->data(); malformed.modelsFor(0)[0].parts[0].current.position.x = std::numeric_limits<float>::quiet_NaN();
    rejects([&] { Presentation::freeze(malformed); });
    auto replacement_geometry = modelGeometry();
    replacement_geometry.parts[0].geometry.vertices.push_back({2,0,0});
    replacement_geometry.parts[0].geometry.vertex_normals.push_back({0,0,-1});
    replacement_geometry.parts[0].geometry.polygons[0].corners.push_back({3});
    malformed = frame->data(); malformed.modelsFor(0)[0].asset = assets.publishModel(82,replacement_geometry);
    rejects([&] { Presentation::freeze(malformed); }); // a sealed table is a proof only for its exact asset
    auto motion_data = frame->data();
    malformed = frame->data();
    malformed.modelsFor(0)[0].parts[0].surfaces[0].occlusion_class = static_cast<OcclusionClass>(255);
    rejects([&] { Presentation::freeze(malformed); });
    motion_data.modelsFor(0)[0].parts[0].surfaces[0].occlusion_class = OcclusionClass::Model;
    check(Presentation::freeze(motion_data)->data().modelsFor(0)[0].parts[0].surfaces[0].occlusion_class == OcclusionClass::Model,
        "Publication lost explicit material occlusion semantics");
    auto& moving = motion_data.model_instances[0];
    moving.has_previous = true; motion_data.variants(moving)[0].parts[0].has_previous = true;
    moving.previous.position = {2,3,4}; motion_data.variants(moving)[0].parts[0].previous.position = {5,6,7};
    auto moving_frame = Presentation::freeze(motion_data);
    VisibleSurface moving_surface; moving_surface.model = true; moving_surface.model_instance_index = 0; moving_surface.part_index = 0;
    const auto motion = surfaceMotion(*moving_frame,moving_surface,{},true);
    check(motion.valid && motion.identity != 0 && motion.previous.position.x == 7 && motion.previous.position.z == 11,
        "Motion did not compose previous object and hierarchical part transforms");
    check(!surfaceMotion(*moving_frame,moving_surface,{},false).valid,"Model motion ignored camera-history invalidation");
    motion_data.variants(moving)[0].parts[0].has_previous = false;
    check(!surfaceMotion(*Presentation::freeze(motion_data),moving_surface,{},true).valid,
        "New topology/part pose reused an unrelated predecessor");
    motion_data.variants(moving)[0].parts[0].has_previous = true; moving.has_previous = false;
    check(!surfaceMotion(*Presentation::freeze(motion_data),moving_surface,{},true).valid,
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
    lod_data.model_variants.resize(1);
    auto& lod_object = lod_data.model_instances[0];
    lod_data.appendModel(lod_object,lod_data.modelsFor(0)[0]);
    lod_data.appendModel(lod_object,lod_data.modelsFor(0)[0]);
    lod_object.lod_distances = {20,40}; lod_object.lod_models = {{0,1,2}};
    for (uint32_t variant = 0; variant < 3; ++variant) {
        lod_data.variants(lod_object)[variant].parts[0].surfaces[0].base_color = {float(variant),0,0};
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
        auto& pose = variant_motion.modelsFor(0)[variant].parts[0];
        pose.has_previous = true; pose.previous.position.x = float(variant+1);
    }
    const auto variant_motion_frame = Presentation::freeze(variant_motion);
    for (uint32_t variant = 0; variant < 3; ++variant) {
        VisibleSurface surface; surface.model = true; surface.model_variant = variant;
        const auto variant_surface_motion = surfaceMotion(*variant_motion_frame,surface,{},true);
        check(variant_surface_motion.valid && variant_surface_motion.previous.position.x == float(variant+1),"LOD motion borrowed another variant's pose");
    }
    auto bounded_lod = lod_frame->data();
    bounded_lod.modelsFor(0)[1].parts[0].current.position.x = 100;
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
        check(!surfaceMotion(*standalone_frame,standalone_surface,{},true).valid &&
            surfaceMotion(*standalone_frame,standalone_surface,{},true).identity == 0,"Standalone model borrowed game motion identity");
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
        standalone = frame->data(); standalone.model_instances[0].model_count = 0;
        rejects([&] { Presentation::freeze(standalone); });
        auto multi = frame->data(); multi.objects.resize(1);
        multi.model_instances[1].owner = multi.model_instances[0].owner;
        const auto multi_query = queryView(*Presentation::freeze(multi),view);
        check(multi_query.surfaces.size() == 2 && multi_query.object_indices.size() == 1,
            "Multiple model instances duplicated their game observation owner");
        check(queryView(*Presentation::freeze(multi),excluded_view).surfaces.empty(),
            "Owner exclusion failed to exclude all its model instances");
    }
    // View-driven evaluation must preserve exact poses and temporal geometry,
    // while its unevaluated bounds enclose every authored animation sample.
    {
        auto moving = modelGeometry();
        moving.parts.push_back(moving.parts[0]);
        moving.parts[1].parent = 0; moving.parts[1].offset = {3,0,0};
        auto tracks = std::make_shared<ModelAnimation>();
        tracks->n_models = 2; tracks->num_key_pos = 2;
        tracks->submodel.resize(2);
        tracks->submodel[0].flags = SOF_ROTATE;
        tracks->submodel[0].rps = .37f; tracks->submodel[0].norm = {0,0,1};
        tracks->submodel[1].parent = 0; tracks->submodel[1].offset = {3,0,0};
        for (auto& track : tracks->submodel) track.keyframe_pos = {{0,0,0},{4,-2,1}};
        moving.animation = tracks;
        const auto moving_asset = assets.publishModel(80,moving);
        FrameData deferred; deferred.sequence = 1; deferred.scene_lifetime = assets.lifetime();
        ModelInstance instance; instance.identity = 91; instance.current.position.z = 25;
        instance.has_owner = instance.has_previous = true; instance.owner = 91; instance.previous.position = {2,3,24};
        ObjectState pose_owner; pose_owner.handle = 91; deferred.objects.push_back(pose_owner);
        PreparedModel model; model.asset = moving_asset; model.deferred_pose = true;
        model.previous_pose_valid = true; model.deformation_owner = 91;
        model.deformation = .25f; model.previous_deformation = .1f;
        model.parts.resize(2);
        for (auto& p : model.parts) { p.surfaces.resize(1); p.has_previous = true; }
        deferred.appendModel(instance,model); deferred.model_instances.push_back(instance);
        View camera; camera.history_key = 1; camera.viewport = camera.projection_anchor = {0,0,1280,720};
        PresentationRef held;
        for (int sample = 0; sample <= 40; ++sample) {
            deferred.simulation_time = sample*.11;
            auto& input = deferred.modelsFor(0)[0];
            input.previous_pose_time = deferred.simulation_time-.07;
            const float current_keys[] = {sample/40.f,sample/40.f};
            const float previous_keys[] = {(40-sample)/40.f,(40-sample)/40.f};
            deferred.model_pose_inputs.clear();
            input.pose_inputs = deferred.appendPoseInputs(current_keys,2);
            input.previous_pose_inputs = deferred.appendPoseInputs(previous_keys,2);
            const auto published = Presentation::freeze(deferred);
            const auto coarse = published->data().model_instances[0].render_bounds;
            const auto& resolved = published->resolvedModel(0,0);
            auto composed_input = published->data();
            const Transform now[] = {resolved.parts[0].current,resolved.parts[1].current};
            const Transform before[] = {resolved.parts[0].previous,resolved.parts[1].previous};
            auto& composed_model = composed_input.modelsFor(0)[0];
            composed_model.pose_inputs = {}; composed_model.previous_pose_inputs = {};
            composed_model.composed_pose = composed_input.appendModelTransforms(now,2);
            composed_model.previous_composed_pose = composed_input.appendModelTransforms(before,2);
            const auto composed_frame = Presentation::freeze(composed_input);
            const auto& composed_result = composed_frame->resolvedModel(0,0);
            check(composed_frame->data().modelsFor(0)[0].parts.summary() == published->data().modelsFor(0)[0].parts.summary(),
                "Publishing a composed pose detached the immutable binding table");
            for (size_t p = 0; p < 2; ++p) {
                check(composed_result.parts[p].current.position.x == resolved.parts[p].current.position.x &&
                    composed_result.parts[p].previous.position.y == resolved.parts[p].previous.position.y &&
                    composed_result.parts[p].current.right.x == resolved.parts[p].current.right.x,
                    "Composed pose changed hierarchy transforms");
                const auto bounds = composed_frame->data().model_instances[0].render_bounds;
                for (const auto vertex : composed_result.parts[p].vertices) {
                    const auto world = transformPoint(compose(instance.current,composed_result.parts[p].current),vertex);
                    check(world.x >= bounds.minimum.x-1.e-4f && world.x <= bounds.maximum.x+1.e-4f &&
                        world.y >= bounds.minimum.y-1.e-4f && world.y <= bounds.maximum.y+1.e-4f &&
                        world.z >= bounds.minimum.z-1.e-4f && world.z <= bounds.maximum.z+1.e-4f,
                        "Composed pose bounds excluded deformed geometry");
                }
            }
            composed_model.composed_pose.count = 1;
            rejects([&] { Presentation::freeze(composed_input); });
            composed_model.composed_pose.count = 2;
            composed_input.model_transforms[0].position.x = std::numeric_limits<float>::infinity();
            rejects([&] { Presentation::freeze(composed_input); });
            check(&resolved == &published->resolvedModel(0,0),"Repeated view rebuilt a resolved model");
            check(published->data().modelsFor(0)[0].deferred_pose,"Resolution mutated published inputs");
            const auto expected = ComposeModelPose(*tracks,EvaluateModelPose(*tracks,deferred.poseInputs(input.pose_inputs).data(),float(deferred.simulation_time)));
            const auto prior = ComposeModelPose(*tracks,EvaluateModelPose(*tracks,deferred.poseInputs(input.previous_pose_inputs).data(),float(input.previous_pose_time)));
            VisibleSurface motion_surface; motion_surface.model = true; motion_surface.part_index = 1;
            const auto motion = surfaceMotion(*published,motion_surface,{},true);
            const auto expected_previous = compose(instance.previous,resolved.parts[1].previous);
            check(motion.valid && motion.previous.position.x == expected_previous.position.x &&
                motion.previous.position.y == expected_previous.position.y && motion.previous.right.y == expected_previous.right.y,
                "Deferred motion read canonical bindings instead of resolved previous animation");
            for (size_t p = 0; p < 2; ++p) {
                const auto& pose = resolved.parts[p];
                check(pose.current.position.x == expected.submodels[p].translation.x &&
                    pose.previous.position.y == prior.submodels[p].translation.y,"Deferred temporal pose changed");
                const auto previous_vertices = deformMesh(moving.parts[p].geometry,.1f,input.previous_pose_time,91,uint32_t(p));
                check(pose.previous_vertices[0].x == previous_vertices[0].x,"Deferred previous deformation changed");
                for (const auto vertex : pose.vertices) {
                    const auto world = transformPoint(compose(instance.current,pose.current),vertex);
                    check(world.x >= coarse.minimum.x && world.x <= coarse.maximum.x &&
                        world.y >= coarse.minimum.y && world.y <= coarse.maximum.y &&
                        world.z >= coarse.minimum.z && world.z <= coarse.maximum.z,"Conservative animation bound missed deformed vertex");
                }
            }
            const auto first = queryView(*published,camera);
            auto auxiliary = camera; auxiliary.camera.position.x = 100;
            queryView(*published,auxiliary);
            check(queryView(*published,camera).surfaces.size() == first.surfaces.size(),"Auxiliary query changed main model demand");
            if (!held) held = published;
        }
        auto next_inputs = held->data();
        {
            auto shared = held->data();
            auto second = shared.model_instances[0];
            second.identity = 92; second.model_count = 0; second.current.position.x += 10;
            auto variant = shared.modelsFor(0)[0];
            const float keys[] = {.75f,.75f};
            variant.pose_inputs = shared.appendPoseInputs(keys,2);
            variant.modulation.color = {.25f,.5f,.75f}; variant.modulation.alpha = .5f;
            shared.appendModel(second,std::move(variant)); shared.model_instances.push_back(second);
            const auto frame = Presentation::freeze(shared);
            check(frame->data().modelsFor(0)[0].parts.summary() == frame->data().modelsFor(1)[0].parts.summary(),
                "Identical published model bindings were copied per instance");
            const auto& first_pose = frame->resolvedModel(0,0);
            const auto& second_pose = frame->resolvedModel(1,0);
            check(first_pose.parts[0].current.position.x != second_pose.parts[0].current.position.x &&
                first_pose.modulation.alpha == 1 && second_pose.modulation.alpha == .5f &&
                second_pose.modulation.color.y == .5f,
                "Sharing immutable model bindings shared pose or instance modulation");
            auto edited = frame->data();
            edited.modelsFor(1)[0].parts[0].surfaces[0].alpha = .25f;
            const auto changed = Presentation::freeze(edited);
            check(changed->data().modelsFor(0)[0].parts.summary() == frame->data().modelsFor(0)[0].parts.summary() &&
                changed->data().modelsFor(1)[0].parts.summary() != frame->data().modelsFor(1)[0].parts.summary() &&
                frame->data().modelsFor(1)[0].parts[0].surfaces[0].alpha == 1,
                "Editing one shared model material changed another instance or held frame");
        }
        {
            auto reflective = held->data();
            ReflectionData reflection; reflection.sources.push_back({{0,1,0},{1,1,1}});
            const auto asset = assets.publishReflection(19,reflection);
            for (auto& part : reflective.modelsFor(0)[0].parts) {
                part.surfaces[0].reflection = asset; part.surfaces[0].specular_strength = 1;
            }
            const auto reflected = Presentation::freeze(reflective);
            const auto& dependencies = *reflected->data().modelsFor(0)[0].parts.summary()->reflections;
            check(dependencies == std::vector<uint64_t>{asset->id().revision},
                "Model reflection summary failed to unite part dependencies");
            for (auto& part : reflective.modelsFor(0)[0].parts) part.surfaces[0].specular_strength = 0;
            const auto unreflected = Presentation::freeze(reflective);
            check(unreflected->data().modelsFor(0)[0].parts.summary()->reflections->empty() && dependencies.size() == 1,
                "Reflection summary changed a held frame or retained disabled specular");
        }
        {
            auto ambient_inputs = held->data();
            VolumeGrid grid; grid.width = grid.height = grid.depth = 1; grid.colors = {255};
            ambient_inputs.model_ambient_inputs.resize(1);
            ambient_inputs.model_instances[0].ambient_input = 0;
            auto& ambient = ambient_inputs.model_ambient_inputs[0];
            ambient.source = ModelAmbientInput::Source::RoomVolume;
            ambient.volume = assets.publishRoom(95,modelGeometry().parts[0].geometry,grid);
            ambient.volume_scalar = .375f;
            const auto unresolved = Presentation::freeze(ambient_inputs);
            grid.colors[0] = 0;
            assets.publishRoom(95,modelGeometry().parts[0].geometry,grid);
            ambient.volume_scalar = 0;
            check(unresolved->data().rooms.empty() && unresolved->resolvedModel(0,0).modulation.ambient.x == .375f,
                "Deferred ambient borrowed mutable or removed room data");
            check(unresolved->data().modelsFor(0)[0].modulation.ambient.x == 1,
                "Ambient resolution mutated captured model inputs");
            ambient.minimum = -1;
            rejects([&] { Presentation::freeze(ambient_inputs); });
            ambient = {}; ambient.source = ModelAmbientInput::Source::Terrain;
            rejects([&] { Presentation::freeze(ambient_inputs); });
            ambient = {}; ambient_inputs.model_instances[0].ambient_input = 1;
            rejects([&] { Presentation::freeze(ambient_inputs); });
            FrameData constant;
            check(constant.appendAmbientInput({}) == UINT32_MAX && constant.model_ambient_inputs.empty(),
                "Constant lighting allocated an ambient field record");
        }
        next_inputs.simulation_time += .1;
        next_inputs.modelsFor(0)[0].previous_draw_mask = 1;
        const auto next_frame = Presentation::freeze(next_inputs);
        check(next_frame->data().modelsFor(0)[0].parts.data() ==
            held->data().modelsFor(0)[0].parts.data(),"Unchanged bindings were copied for a new pose");
        const auto* summary = held->data().modelsFor(0)[0].parts.summary();
        auto faded_inputs = held->data();
        faded_inputs.modelsFor(0)[0].modulation = {{.4f,.5f,.6f},{.7f,.8f,.9f},.25f};
        const auto faded_frame = Presentation::freeze(faded_inputs);
        check(faded_frame->data().modelsFor(0)[0].parts.summary() == summary && held->data().modelsFor(0)[0].modulation.alpha == 1,
            "Instance shading rebuilt materials or mutated a held frame");
        const auto faded_view = queryView(*faded_frame,camera);
        check(!faded_view.surfaces.empty(),"Modulation fixture is not visible");
        for (const auto& item : faded_view.surfaces)
            check(item.late && item.blended && visibleSurfacePolicy(*faded_frame,item).blend == BlendMode::Alpha,
                "Fading instance entered the opaque pass");
        faded_inputs.modelsFor(0)[0].modulation.alpha = 1;
        const auto restored_frame = Presentation::freeze(faded_inputs);
        for (const auto& item : queryView(*restored_frame,camera).surfaces)
            check(!item.late,"Ending fade retained stale translucent policy");
        faded_inputs.modelsFor(0)[0].modulation.alpha = 2;
        rejects([&] { Presentation::freeze(faded_inputs); });
        auto duplicated = held->data();
        auto duplicate = duplicated.model_instances[0]; duplicate.identity = 92; duplicate.model_count = 0;
        duplicated.appendModel(duplicate,duplicated.modelsFor(0)[0]); duplicated.model_instances.push_back(duplicate);
        const auto duplicated_frame = Presentation::freeze(duplicated);
        check(duplicated_frame->data().compiled_part_count == 4 && duplicated_frame->resolvedModel(1,0).parts[1].compiled_index == 3 &&
            held->resolvedModel(0,0).parts[1].compiled_index == 1,"Shared deferred bindings leaked another instance's part address");
        for (uint64_t i = 0; i < 64; ++i) {
            auto added = duplicated.model_instances[0]; added.identity = 1000+i; added.model_count = 0;
            duplicated.appendModel(added,duplicated.modelsFor(0)[0]); duplicated.model_instances.push_back(added);
        }
        const auto many_poses = Presentation::freeze(duplicated);
        const auto* first_pose = &many_poses->resolvedModel(0,0);
        const auto* first_part = &first_pose->parts[0];
        check(first_part->surfaces.data() == many_poses->data().modelsFor(0)[0].parts[0].surfaces.data(),
            "Pose preparation detached immutable material bindings");
        for (size_t i = 1; i < duplicated.model_instances.size(); ++i) {
            const auto& resolved = many_poses->resolvedModel(i,0);
            check(resolved.parts[0].compiled_index == i*2,"Packed pose cache lost instance addresses");
            check(&many_poses->resolvedModel(0,0) == first_pose,"Resolving another pose invalidated a held model reference");
            check(&many_poses->resolvedModel(0,0).parts[0] == first_part,"Resolving another pose invalidated a held part range");
        }
        check(summary && summary->surface_count == 2 && summary->has_geometry && summary->drawn_parts == 3 &&
            next_frame->data().modelsFor(0)[0].parts.summary() == summary,
            "Unchanged bindings rebuilt their compiled model summary");
        auto hidden = held->data();
        for (auto& part : hidden.modelsFor(0)[0].parts) part.draw = false;
        check(!hidden.modelsFor(0)[0].parts.summary(),"Part edit retained stale summary");
        const auto hidden_frame = Presentation::freeze(hidden);
        check(!hidden_frame->data().model_instances[0].has_render_bounds &&
            !hidden_frame->data().modelsFor(0)[0].parts.summary()->has_geometry &&
            !hidden_frame->data().modelsFor(0)[0].parts.summary()->drawn_parts && summary->has_geometry,
            "Changed draw membership retained old conservative bounds or mutated held summary");
        const auto& next_pose = next_frame->resolvedModel(0,0);
        check(next_pose.parts[0].has_previous && !next_pose.parts[1].has_previous,
            "Shared bindings leaked a prior frame's part visibility history");
        check(next_pose.parts[0].surface_base == next_frame->data().modelsFor(0)[0].surface_base &&
            next_pose.parts[1].surface_base == next_pose.parts[0].surface_base+1,"Deferred binding indices were not frame-local");
        next_inputs.modelsFor(0)[0].parts[0].surfaces[0].alpha = .25f;
        const auto changed = Presentation::freeze(next_inputs);
        check(changed->data().modelsFor(0)[0].parts[0].surfaces[0].alpha == .25f &&
            held->data().modelsFor(0)[0].parts[0].surfaces[0].alpha == 1,
            "Model binding edit mutated a held frame");
        PreparedMesh attachment; attachment.alignment = MeshAlignment::Camera;
        attachment.world.position = {2,3,4};
        const auto parent = compose(instance.current,next_pose.parts[1].current);
        const auto attached = resolveMeshTransform(attachment,prepareCamera(camera),&parent);
        const auto point = transformPoint(parent,attachment.world.position);
        check(attached.position.x == point.x && attached.position.y == point.y && attached.position.z == point.z &&
            attached.right.x == 1 && attached.up.y == 1,"Part attachment rotated its camera-facing sprite basis");
        const auto held_x = held->resolvedModel(0,0).parts[0].vertices[0].x;
        const auto pooledPose = [&] {
            auto input = acquireFrameData();
            input.sequence = deferred.sequence; input.scene_lifetime = deferred.scene_lifetime;
            input.simulation_time = deferred.simulation_time;
            input.objects = deferred.objects; input.model_instances = deferred.model_instances;
            input.model_variants = deferred.model_variants; input.model_pose_inputs = deferred.model_pose_inputs;
            input.model_ambient_inputs = deferred.model_ambient_inputs;
            return Presentation::publish(std::move(input));
        };
        auto pooled = pooledPose();
        const auto* pooled_vertices = pooled->resolvedModel(0,0).parts[0].vertices.data();
        const auto pooled_x = pooled_vertices[0].x;
        auto other_reader = pooled;
        pooled.reset();
        auto concurrent = pooledPose();
        check(concurrent->resolvedModel(0,0).parts[0].vertices.data() != pooled_vertices &&
            other_reader->resolvedModel(0,0).parts[0].vertices[0].x == pooled_x,
            "Pose arena was recycled while another reader held the snapshot");
        other_reader.reset();
        auto recycled = pooledPose();
        check(recycled->resolvedModel(0,0).parts[0].vertices.data() == pooled_vertices &&
            recycled->resolvedModel(0,0).parts[0].vertices[0].x == pooled_x,
            "Released pose arena lost reusable deformation storage or retained stale geometry");
        tracks->submodel.clear(); assets.removeModel(80);
        check(held->resolvedModel(0,0).parts[0].vertices[0].x == held_x,"Held pose depended on unloaded animation");
        auto invalid = deferred;
        const float key = .5f;
        invalid.modelsFor(0)[0].pose_inputs = invalid.appendPoseInputs(&key,1);
        rejects([&] { Presentation::freeze(invalid); });
        invalid = deferred; invalid.modelsFor(0)[0].pose_inputs = {UINT32_MAX,2};
        rejects([&] { Presentation::freeze(invalid); });
        invalid = deferred; invalid.model_pose_inputs[0] = std::numeric_limits<float>::quiet_NaN();
        rejects([&] { Presentation::freeze(invalid); });
        invalid = deferred; invalid.modelsFor(0)[0].deformation = std::numeric_limits<float>::infinity();
        rejects([&] { Presentation::freeze(invalid); });
    }
    {
        auto partitioned = frame->data();
        partitioned.model_instances[0].collection = UINT64_MAX;
        partitioned.model_instances[1].collection = 13;
        PreparedMesh mesh; mesh.asset = assets.quadMesh(); mesh.world.position.z = 12;
        mesh.collection = 13; partitioned.meshes.push_back(mesh);
        mesh.collection = UINT64_MAX; partitioned.meshes.push_back(mesh);
        mesh.collection = 13; partitioned.meshes.push_back(mesh);
        const auto packed = Presentation::freeze(partitioned);
        const auto first = packed->collection(13), second = packed->collection(UINT64_MAX);
        check(first.models.count == 1 && first.models.first[0] == 1 && first.meshes.count == 2 &&
            first.meshes.first[0] == 0 && first.meshes.first[1] == 2,"Collection partition lost original index ordering");
        check(second.models.count == 1 && second.models.first[0] == 0 && second.meshes.count == 1 &&
            second.meshes.first[0] == 1,"Collection partition lost a sparse/high-bit identity");
        check(packed->collection(0).models.count == 0 && packed->collection(7).meshes.count == 0,
            "Absent collection inherited world content");
        partitioned.meshes[0].collection = 0;
        check(packed->collection(13).meshes.count == 2,"Collection membership retained mutable builder state");
    }
    assets.removeModel(3);
    check(!assets.model(3) && frame->data().modelsFor(0)[0].asset == asset, "Model unload invalidated held frame");
    assets.reset();
    check(assets.publishModel(3,modelGeometry())->id().lifetime != asset->id().lifetime, "Model slot reuse kept old lifetime");
    malformed = frame->data(); malformed.scene_lifetime = assets.lifetime();
    rejects([&] { Presentation::freeze(malformed); });
    return checks;
}
