// Descent 3MASTERED - GPL-3.0-or-later
#include "../../renderer/SceneDeformation.h"
#include "../../renderer/SceneHistory.h"
#include "../../Descent3/SceneExtraction.h"
#include "object.h"
#include "game.h"
#include <cmath>
#include <limits>
#include <stdexcept>

int runViewDeformationTests() {
    using namespace d3scene;
    int checks = 0;
    const auto check = [&](bool valid) { ++checks; if (!valid) throw std::runtime_error("View-owned world deformation contract"); };
    const float saved_time = Gametime; Gametime = 100;
    object viewer{}; viewer.handle = HANDLE_COUNT_INCREMENT|5;
    effect_info_s effect{}; effect.type_flags = EF_DEFORM; effect.deform_range = .3f; effect.deform_time = 1;
    viewer.effect_info = &effect;
    matrix orientation{}; orientation.rvec.x = orientation.uvec.y = orientation.fvec.z = 1;
    const auto make = [&](bool rear = false) { return MakeGameSceneView(ViewKind::Main,1,&viewer,{},-2,orientation,1,rear,{0,0,800,600},{0,0,800,600}); };
    auto view = make();
    check(std::abs(view.world_deformation.displacement.x-5.7f) < .00001f &&
        view.world_deformation.displacement.y == -view.world_deformation.displacement.x);
    check(make().world_deformation == view.world_deformation && make(true).world_deformation == view.world_deformation);
    const auto point = Vec3{1,2,3}; const auto deformed = deformViewPoint(point,view.world_deformation);
    check(std::abs(deformed.x-point.x) <= 5.7f && std::abs(deformed.y-point.y) <= 5.7f && std::abs((deformed.z-point.z)-(deformed.x-point.x)) < .00001f);
    const auto edge_a = deformViewPoint({63.999f,2,3},view.world_deformation);
    const auto edge_b = deformViewPoint({64.001f,2,3},view.world_deformation);
    check(std::abs((edge_a.x-63.999f)-(edge_b.x-64.001f)) < .001f);
    check(deformViewPoint(point,{}).x == point.x && deformViewPoint(point,view.world_deformation).x == deformed.x);
    check(hasDeformationMotion(view.world_deformation,{},{}));
    check(hasDeformationMotion({},view.world_deformation,{}));
    check(!hasDeformationMotion({},{},{}));
    check(!hasDeformationMotion(view.world_deformation,view.world_deformation,{0,0}));
    check(deformViewPoint({0,2,3},view.world_deformation).x == deformViewPoint({-0.f,2,3},view.world_deformation).x);
    Gametime += 1.f/60;
    check(make().world_deformation != view.world_deformation);
    effect.deform_time = .5f;
    check(std::abs(make().world_deformation.displacement.x-2.85f) < .00001f);
    effect.type_flags = 0; check(make().world_deformation == ViewDeformation{});
    effect.type_flags = EF_DEFORM;
    const auto foreground = cameraOverlayView(view,2,{0,0,800,600});
    check(foreground.world_deformation == ViewDeformation{} && view.world_deformation.displacement.x > 0);
    auto invalid = view; invalid.world_deformation.displacement.x = std::numeric_limits<float>::infinity();
    bool rejected = false; try { validateView(invalid); } catch (const std::invalid_argument&) { rejected = true; }
    check(rejected);
    {
        SceneAssets assets;
        FrameData malformed; malformed.sequence = 1; malformed.scene_lifetime = assets.lifetime();
        PreparedMesh mesh; mesh.asset = assets.quadMesh(); mesh.deformation_response.vertices = -1;
        malformed.meshes.push_back(mesh);
        bool invalid_response = false;
        try { Presentation::freeze(malformed); } catch (const std::invalid_argument& error) {
            invalid_response = std::strcmp(error.what(),"Invalid mesh deformation response") == 0;
        }
        check(invalid_response);
        malformed.meshes.clear(); ModelInstance invalid_model; invalid_model.identity = 1;
        invalid_model.deformation_response.translation = std::numeric_limits<float>::infinity();
        malformed.model_instances.push_back(invalid_model); invalid_response = false;
        try { Presentation::freeze(malformed); } catch (const std::invalid_argument& error) {
            invalid_response = std::strcmp(error.what(),"Invalid model deformation response") == 0;
        }
        check(invalid_response);
    }
    FrameData data; data.sequence = 1; data.scene_lifetime = 7;
    ModelInstance model; model.has_owner = true; model.owner = 0x10008; model.deformation_response = GameModelDeformationResponse();
    data.model_instances.push_back(model);
    PreparedMesh accent; accent.model_instance = 1; accent.compiled_model = 0; data.meshes.push_back(accent);
    VisibleSurface body; body.model = true;
    const auto body_binding = deformationForGeometry(data,body,RenderLayer::World);
    check(body_binding.response.vertices == 1.f/19 && body_binding.response.translation == 10.f/19 && body_binding.identity == model.owner);
    VisibleSurface attachment; attachment.mesh = true;
    const auto accent_binding = deformationForGeometry(data,attachment,RenderLayer::World);
    check(accent_binding.response.vertices == 0 && accent_binding.response.translation == body_binding.response.translation && accent_binding.identity == body_binding.identity);
    const auto body_offset = deformViewPoint(point,view.world_deformation,{0,body_binding.response.translation},body_binding.identity);
    const auto accent_offset = deformViewPoint(point,view.world_deformation,accent_binding.response,accent_binding.identity);
    check(body_offset.x == accent_offset.x && body_offset.y == accent_offset.y && body_offset.z == accent_offset.z);
    check(deformationForGeometry(data,body,RenderLayer::Background).response.vertices == 0);
    data.meshes[0].model_instance = 0;
    check(deformationForGeometry(data,attachment,RenderLayer::World).response.translation == 0);
    ViewHistory history; const auto first = history.prepare(data,view,prepareCamera(view)); history.commit(first);
    check(!first.previous_valid && first.previous_deformation == view.world_deformation);
    data.temporal_predecessor = 1; data.sequence = 2;
    auto next = make();
    const auto second = history.prepare(data,next,prepareCamera(next));
    check(second.previous_valid && second.previous_deformation == first.view.world_deformation &&
        second.previous_deformation != second.view.world_deformation);
    history.commit(second);
    check(history.prepare(data,next,prepareCamera(next)).previous_deformation == first.view.world_deformation);
    auto conflicting = next; ++conflicting.world_deformation.seed;
    rejected = false; try { history.prepare(data,conflicting,prepareCamera(conflicting)); } catch (const std::logic_error&) { rejected = true; }
    check(rejected);
    data.temporal_predecessor = 2; data.sequence = 3; data.paused = true;
    const auto exposure = prepareImageMotion(data,history.prepare(data,next,prepareCamera(next)),{});
    check(exposure.camera.previous_deformation == second.view.world_deformation);
    auto cut = next; cut.camera_cut = true;
    const auto reset = history.prepare(data,cut,prepareCamera(cut));
    check(!reset.previous_valid && reset.previous_deformation == cut.world_deformation);
    auto independent = next; independent.kind = ViewKind::SaveThumbnail; independent.history_key = 9;
    check(!history.prepare(data,independent,prepareCamera(independent)).previous_valid);
    data.scene_lifetime = 8;
    check(!history.prepare(data,next,prepareCamera(next)).previous_valid);
    Gametime = saved_time;
    return checks;
}
