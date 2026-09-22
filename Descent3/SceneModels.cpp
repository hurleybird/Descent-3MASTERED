// Descent 3MASTERED - GPL-3.0-or-later
#include "SceneExtraction.h"
#include "object.h"
#include "objinfo.h"
#include "polymodel.h"
#include "weapon.h"
#include "marker.h"
#include "../model/ModelPose.h"
#include "../model/SceneModelAsset.h"
#include "../renderer/SceneTerrain.h"
#include "gameloop.h"
#include "game.h"
#include "object_lighting.h"
#include "room.h"
#include "terrain.h"
#include "player.h"
#include "multi.h"
#include "renderer.h"
#include "../renderer/SceneLighting.h"
#include "../renderer/SceneGeometry.h"
#include "args.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <unordered_map>

namespace {
d3scene::Vec3 value(const vector& v) { return {v.x, v.y, v.z}; }
d3scene::Transform value(const ModelSubmodelTransform& t) {
    return {value(t.translation), value(t.orientation.rvec), value(t.orientation.uvec), value(t.orientation.fvec)};
}
using RoomTable = std::unordered_map<int,const d3scene::RoomState*>;
struct ModelAppearance {
    float alpha = 1, deformation = 0;
    d3scene::Vec3 color{1,1,1};
};
ModelAppearance appearance(const object& source) {
    ModelAppearance result;
    if (source.effect_info) {
        const auto& effect = *source.effect_info;
        const float fade = effect.fade_max_time > 0 ? effect.fade_time/effect.fade_max_time : 0;
        if (effect.type_flags & EF_FADING_OUT) result.alpha = .08f+.92f*fade;
        if (effect.type_flags & EF_FADING_IN) result.alpha = .08f+.92f*(1-fade);
        if (effect.type_flags & EF_CLOAKED) { result.alpha = .13f; result.deformation = .1f; }
        if (effect.type_flags & EF_DEFORM) {
            result.deformation = effect.deform_range*effect.deform_time;
            const float amount = std::max(0.f,std::min(1.f,effect.deform_time));
            result.color = {1,1-amount*.5f,1-amount*.5f};
        }
    }
    if (source.type == OBJ_POWERUP && (source.flags & OF_USES_LIFELEFT) && source.lifeleft < 5) {
        result.alpha = source.lifeleft/5;
        result.deformation = .2f*(1-result.alpha);
    }
    result.alpha = std::max(0.f,std::min(1.f,result.alpha));
    return result;
}
d3scene::Vec3 volumeColor(const RoomTable& rooms, int number, const vector& position) {
    const auto found = rooms.find(number);
    if (found == rooms.end() || found->second->asset->volume().colors.empty()) return {1,1,1};
    const auto& room = *found->second;
    auto color = d3scene::sampleVolume(room.asset->volume(),value(position));
    return {color.x*room.light_scalar,color.y*room.light_scalar,color.z*room.light_scalar};
}
d3scene::Vec3 ambientColor(const object& source, const RoomTable& rooms, const d3scene::TerrainState& terrain) {
    d3scene::Vec3 color{1,1,1};
    const bool outside = OBJECT_OUTSIDE(&source);
    const bool volume = source.effect_info && (source.effect_info->type_flags & EF_VOLUME_LIT);
    if (outside) {
        if (source.type == OBJ_POWERUP) return color;
        const float scalar = d3scene::sampleTerrainExposure(terrain.asset->grid(),
            {source.pos.x,source.pos.y,source.pos.z},CELLNUM(source.roomnum));
        color = {scalar,scalar,scalar};
    } else {
        if (source.lighting_render_type == LRT_STATIC ||
            !(source.lighting_render_type == LRT_GOURAUD || NoLightmaps) || !volume) return color;
        const auto found = rooms.find(source.roomnum);
        if (found == rooms.end() || (found->second->authored_flags & RF_EXTERNAL)) return color;
        color = volumeColor(rooms,source.roomnum,source.pos);
        const auto& effect = *source.effect_info;
        if (effect.type_flags & EF_VOLUME_CHANGING) {
            const auto old = volumeColor(rooms,effect.volume_old_room,effect.volume_old_pos);
            const float t = effect.volume_change_time;
            color = {old.x*t+color.x*(1-t),old.y*t+color.y*(1-t),old.z*t+color.z*(1-t)};
        }
    }
    if (volume) {
        if (source.type == OBJ_ROBOT) {
            const float minimum = outside ? .25f : .1f;
            color = {std::max(minimum,color.x),std::max(minimum,color.y),std::max(minimum,color.z)};
        }
        if (source.type == OBJ_PLAYER) {
            if ((Players[source.id].flags & PLAYER_FLAGS_HEADLIGHT) ||
                ((Game_mode & GM_MULTI) && (Netgame.flags & NF_BRIGHT_PLAYERS))) return {1,1,1};
            if (!outside && (Game_mode & GM_MULTI)) {
                vector velocity = source.mtype.phys_info.velocity;
                const float speed = 1+std::min(vm_GetMagnitudeFast(&velocity)/20.f,1.f);
                color = {std::min(1.f,color.x*speed),std::min(1.f,color.y*speed),std::min(1.f,color.z*speed)};
            }
        }
    }
    return color;
}
}

void PrepareGameModelInstances(d3scene::FrameData& data, const d3scene::FrameData* previous) {
    std::unordered_map<int, d3scene::ModelAssetRef> frame_assets;
    RoomTable rooms;
    for (const auto& room : data.rooms) rooms.emplace(room.number,&room);
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
        const auto ambient = ambientColor(*source,rooms,data.terrain);
        auto effect = appearance(*source);
        // Explicit inspection fixture: exercises prepared-position uploads even
        // when a saved scene has no active cloak/deformation. Never mutates game state.
        if (SceneGeometryInspection() && FindArg("-scene-deformation-probe")) effect.deformation = .2f;
        const bool directional = source->lighting_render_type != LRT_STATIC &&
            (source->lighting_render_type == LRT_GOURAUD || NoLightmaps);
        // The saved per-object UV layout is not the shared model's topology.
        // Reject mismatched layouts before indexing (including destroyed-model swaps).
        const bool baked = !NoLightmaps && source->lm_object.used && source->lighting_render_type == LRT_LIGHTMAPS &&
            model_number == source->rtype.pobj_info.model_num && ObjectLightmapsMatchPolymodel(source);
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
            part.has_previous = history && history->parts[i].draw && instance.has_previous;
            part.vertices = d3scene::deformMesh(asset_part.geometry,effect.deformation,data.simulation_time,instance.handle,static_cast<uint32_t>(i));
            part.previous_vertices = history ? history->parts[i].vertices : part.vertices;
            // Special billboard/glow geometry needs explicit effect preparation.
            part.draw = !(asset_part.authored_flags & (SOF_SHELL | SOF_FRONTFACE | SOF_FACING | SOF_GLOW | SOF_THRUSTER));
            part.draw = part.draw && (source->rtype.pobj_info.subobj_flags & (1u << i));
            for (size_t j = 0; j < asset_part.geometry.polygons.size(); ++j) {
                d3scene::SurfaceBinding binding;
                binding.ambient_color = ambient;
                binding.directional_ambient = directional;
                binding.alpha = effect.alpha;
                binding.color_modulation = effect.color;
                const int slot = asset_part.polygon_materials[j];
                if (slot >= 0) binding.texture = instance.model->geometry().textures[slot];
                else binding.base_color = asset_part.polygon_colors[j];
                if (source->rtype.pobj_info.tmap_override != -1) binding.texture = source->rtype.pobj_info.tmap_override;
                if (baked && part.draw) {
                    const auto& face = source->lm_object.lightmap_faces[i][j];
                    binding.lightmap = face.lmi_handle;
                    for (size_t corner = 0; corner < asset_part.geometry.polygons[j].corners.size(); ++corner)
                        binding.baked_uvs.push_back({face.u2[corner],face.v2[corner]});
                }
                part.surfaces.push_back(binding);
            }
            instance.parts.push_back(std::move(part));
        }
    }
}
