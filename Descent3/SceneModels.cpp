// Descent 3MASTERED - GPL-3.0-or-later
#include "SceneExtraction.h"
#include "object.h"
#include "objinfo.h"
#include "polymodel.h"
#include "weapon.h"
#include "marker.h"
#include "../model/ModelPose.h"
#include "../model/SceneModelAsset.h"
#include "gameloop.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <unordered_map>

namespace {
d3scene::Vec3 value(const vector& v) { return {v.x, v.y, v.z}; }
d3scene::Transform value(const ModelSubmodelTransform& t) {
    return {value(t.translation), value(t.orientation.rvec), value(t.orientation.uvec), value(t.orientation.fvec)};
}
}

void PrepareGameModelInstances(d3scene::FrameData& data, const d3scene::FrameData* previous) {
    std::unordered_map<int, d3scene::ModelAssetRef> frame_assets;
    std::unordered_map<uint32_t, const d3scene::ObjectState*> prior;
    if (previous && previous->scene_lifetime == data.scene_lifetime && previous->simulation_time <= data.simulation_time)
        for (const auto& object : previous->objects) prior.emplace(object.handle, &object);
    for (auto& instance : data.objects) {
        auto* source = ObjGet(instance.handle);
        if (!source) throw std::logic_error("Object changed during presentation preparation");
        if (source->render_type != RT_POLYOBJ && source->render_type != RT_WEAPON) continue;
        if (source->render_type == RT_WEAPON && (Weapons[source->id].flags & (WF_IMAGE_BITMAP | WF_IMAGE_VCLIP))) continue;
        int model_number = source->type == OBJ_MARKER ? Marker_polynum : source->rtype.pobj_info.model_num;
        if (source->type == OBJ_BUILDING && (source->flags & OF_USE_DESTROYED_POLYMODEL) &&
            Object_info[source->id].lo_render_handle >= 0) model_number = Object_info[source->id].lo_render_handle;
        if (model_number < 0 || model_number >= MAX_POLY_MODELS || !Poly_models[model_number].used)
            throw std::invalid_argument("Object references missing model");
        auto* model = GetPolymodelPointer(model_number);
        if (!model->new_style) continue; // old-format executor remains a pending domain, never a legacy draw fallback
        auto found = frame_assets.find(model_number);
        if (found == frame_assets.end()) {
            // Compare once per referenced model, not once per instance or view.
            // The source loader has no revision counter; publication owns a copy.
            size_t repaired = 0;
            auto geometry = CompileSceneModelAsset(*model,&repaired);
            if (repaired && !d3scene::assets().model(model_number))
                AutomatedCaptureLog("scene model=%s repaired shading normals=%u",model->name,static_cast<unsigned>(repaired));
            auto asset = d3scene::assets().publishModel(model_number, std::move(geometry));
            found = frame_assets.emplace(model_number, std::move(asset)).first;
        }
        instance.model = found->second;
        float normalized[MAX_SUBOBJECTS]{};
        const float* input = nullptr;
        if (source->rtype.pobj_info.anim_frame || model->frame_min != model->frame_max) {
            if (model_number == source->rtype.pobj_info.model_num) SetNormalizedTimeObj(source, normalized);
            else SetNormalizedTimeAnim(source->rtype.pobj_info.anim_frame, normalized, model);
            input = normalized;
        }
        const auto pose = EvaluateModelPose(*model, input, static_cast<float>(data.simulation_time));
        const auto transforms = ComposeModelPose(*model, pose);
        const auto old = prior.find(instance.handle);
        const d3scene::ObjectState* history = old != prior.end() && old->second->model &&
            old->second->model->id() == instance.model->id() ? old->second : nullptr;
        for (size_t i = 0; i < instance.model->geometry().parts.size(); ++i) {
            const auto& asset_part = instance.model->geometry().parts[i];
            d3scene::PreparedModelPart part;
            part.current = value(transforms.submodels[i]);
            part.previous = history ? history->parts[i].current : part.current;
            // Special billboard/glow geometry needs explicit effect preparation.
            part.draw = !(asset_part.authored_flags & (SOF_SHELL | SOF_FRONTFACE | SOF_FACING | SOF_GLOW | SOF_THRUSTER));
            part.draw = part.draw && (source->rtype.pobj_info.subobj_flags & (1u << i));
            for (size_t j = 0; j < asset_part.geometry.polygons.size(); ++j) {
                d3scene::SurfaceBinding binding;
                const int slot = asset_part.polygon_materials[j];
                if (slot >= 0) binding.texture = instance.model->geometry().textures[slot];
                else binding.base_color = asset_part.polygon_colors[j];
                if (source->rtype.pobj_info.tmap_override != -1) binding.texture = source->rtype.pobj_info.tmap_override;
                part.surfaces.push_back(binding);
            }
            instance.parts.push_back(std::move(part));
        }
    }
}
