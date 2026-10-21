// Descent 3MASTERED - GPL-3.0-or-later
#include "SceneExtraction.h"
#include "SceneMaterialBindings.h"
#include "gametexture.h"
#include "bitmap.h"
#include "fireball.h"
#include "descent.h"
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
#include "lightmap_info.h"
#include "room.h"
#include "terrain.h"
#include "player.h"
#include "ship.h"
#include "multi.h"
#include "renderer.h"
#include "../renderer/SceneLighting.h"
#include "../renderer/SceneGeometry.h"
#include "args.h"
#include "config.h"
#include "renderobject.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <unordered_map>

extern bool is_multi_demo;

namespace {
d3scene::Vec3 value(const vector& v) { return {v.x, v.y, v.z}; }
d3scene::Transform value(const ModelSubmodelTransform& t) {
    return {value(t.translation), value(t.orientation.rvec), value(t.orientation.uvec), value(t.orientation.fvec)};
}
using RoomTable = std::unordered_map<int,const d3scene::RoomState*>;
// A pose is a function of the model, its animation inputs and the presentation
// clock, not of object placement. Instances with identical inputs share the
// evaluation. Scratch retains capacity only; frames copy the resulting values.
const ModelTransforms& evaluateSharedPose(int number,const poly_model& model,const float* input,
    uint64_t sequence,float time) {
    struct Entry {
        bool has_input = false;
        std::array<float,MAX_SUBOBJECTS> input{};
        ModelTransforms transforms;
    };
    struct Bucket { uint64_t sequence = 0, source_revision = 0; size_t count = 0; std::vector<Entry> entries; };
    static thread_local std::array<Bucket,MAX_POLY_MODELS> cache;
    auto& bucket = cache[number];
    const auto revision = SceneModelSourceEpoch();
    if (bucket.sequence != sequence || bucket.source_revision != revision) {
        bucket.sequence = sequence; bucket.source_revision = revision; bucket.count = 0;
    }
    for (size_t i = 0; i < bucket.count; ++i) {
        const auto& entry = bucket.entries[i];
        if (entry.has_input == bool(input) && (!input || std::equal(input,input+model.n_models,entry.input.begin())))
            return entry.transforms;
    }
    if (bucket.entries.size() == bucket.count) bucket.entries.emplace_back();
    auto& entry = bucket.entries[bucket.count++];
    entry.has_input = input != nullptr;
    if (input) std::copy(input,input+model.n_models,entry.input.begin());
    entry.transforms = ComposeModelPose(model,EvaluateModelPose(model,input,time));
    return entry.transforms;
}
void prepareFacingPart(d3scene::FrameData& frame,const d3scene::ObjectState* owner,
    const poly_model& model,size_t part_index,const d3scene::Transform& world) {
    const auto& part = model.submodel[part_index];
    if (part.num_faces < 1 || part.faces[0].texnum < 0 || part.faces[0].texnum >= model.n_textures || !(part.rad > 0)) return;
    const int texture = model.textures[part.faces[0].texnum];
    if (texture < 0 || texture >= MAX_TEXTURES || !GameTextures[texture].used)
        throw std::invalid_argument("Facing model part references missing material");
    const int bitmap = GetTextureBitmapAtTime(texture,0,static_cast<float>(frame.simulation_time));
    const int width = bm_w(bitmap,0), height = bm_h(bitmap,0);
    if (width <= 0 || height <= 0) return;
    d3scene::PreparedMesh sprite;
    sprite.asset = d3scene::assets().quadMesh();
    if (owner) { sprite.owner = owner->handle; sprite.has_owner = true; sprite.room = owner->room; }
    sprite.alignment = d3scene::MeshAlignment::Camera;
    sprite.world.position = world.position;
    sprite.world.right = {part.rad,0,0}; sprite.world.up = {0,part.rad*height/width,0};
    sprite.surface.bitmap = bitmap; sprite.surface.texture_alpha = true; sprite.surface.texture_wrap = false;
    sprite.surface.unlit = true; sprite.surface.two_sided = true;
    sprite.surface.alpha = static_cast<unsigned>((std::max)(0.f,(std::min)(1.f,GameTextures[texture].alpha))*255)/255.f;
    sprite.surface.blend = GameTextures[texture].flags & TF_SATURATE ? d3scene::BlendMode::Additive : d3scene::BlendMode::Alpha;
    sprite.surface.soft_depth_range = SceneFacingUsesSoftIntersection(model.name,GameTextures[texture].name) ? 2.f : 0.f;
    frame.meshes.push_back(std::move(sprite));
}
void prepareGlowPart(d3scene::FrameData& frame,const d3scene::ObjectState* owner,const object* source,
    const poly_model& model,size_t part_index,const d3scene::Transform& world) {
    if (source && source->type == OBJ_WEAPON && !Detail_settings.Weapon_coronas_enabled) return;
    const auto* glow = model.submodel[part_index].glow_info;
    if (!glow || !(glow->glow_size > 0)) return;
    const int bitmap = Fireballs[GRADIENT_BALL_INDEX].bm_handle;
    if (bitmap <= BAD_BITMAP_HANDLE || bitmap >= MAX_BITMAPS || !GameBitmaps[bitmap].used) return;
    const int width = bm_w(bitmap,0), height = bm_h(bitmap,0);
    if (width <= 0 || height <= 0) return;
    d3scene::Vec3 color{glow->glow_r,glow->glow_g,glow->glow_b};
    std::vector<d3scene::GlowSlice> slices;
    const bool thruster = source && owner && source->type == OBJ_PLAYER;
    if (thruster) {
        const auto& player = Players[source->id];
        const float jitter = (int(d3scene::visualNoise(frame.simulation_time,owner->handle,0x74687275)%100)-50)/50.f;
        float length = player.thrust_mag+player.afterburner_mag*3+.2f*jitter;
        if (Katmai) length *= 1.5f;
        float radius = glow->glow_size*(player.thrust_mag+player.afterburner_mag*.5f);
        if (SceneGeometryInspection() && (FindArg("-scene-model-effects-probe") || FindArg("-scene-lod-probe"))) { radius = glow->glow_size; length = 1.5f; }
        slices = d3scene::thrusterSlices(radius,3*length);
        if ((is_multi_demo || (Game_mode & GM_MULTI)) && Num_teams > 1)
            color = ScenePlayerColor(PlayerGetTeam(source->id));
    } else slices.push_back({0,glow->glow_size});
    const auto quantize = [](float c) { return static_cast<unsigned>((std::max)(0.f,(std::min)(1.f,c))*255)/255.f; };
    color = {quantize(color.x),quantize(color.y),quantize(color.z)};
    const auto normal = d3scene::transformDirection(world,value(glow->normal));
    const bool soft = !thruster && SceneGlowUsesSoftIntersection(model.name);
    const auto quad = d3scene::assets().quadMesh();
    if (AutomatedCaptureFrameIndex() == 0)
        AutomatedCaptureLog("scene glow owner=%u model=%s part=%u slices=%u soft=%d",owner ? owner->handle : 0,model.name,
            static_cast<unsigned>(part_index),static_cast<unsigned>(slices.size()),soft ? 1 : 0);
    for (const auto& slice : slices) {
        d3scene::PreparedMesh sprite;
        sprite.asset = quad;
        if (owner) { sprite.owner = owner->handle; sprite.has_owner = true; sprite.room = owner->room; }
        sprite.alignment = d3scene::MeshAlignment::Camera;
        sprite.world.position = {world.position.x+normal.x*slice.distance,world.position.y+normal.y*slice.distance,
            world.position.z+normal.z*slice.distance};
        sprite.world.right = {slice.radius,0,0}; sprite.world.up = {0,slice.radius*height/width,0};
        sprite.surface.bitmap = bitmap; sprite.surface.texture_alpha = true; sprite.surface.texture_wrap = false;
        sprite.surface.unlit = true; sprite.surface.two_sided = true; sprite.surface.blend = d3scene::BlendMode::Additive;
        sprite.surface.alpha = thruster ? 76/255.f : 204/255.f; sprite.surface.color_modulation = color;
        sprite.surface.soft_depth_range = soft ? 2.f : 0.f;
        sprite.surface.effect_depth_offset = soft ? 0.f : -slice.radius;
        frame.meshes.push_back(std::move(sprite));
    }
}
struct ModelAppearance {
    float alpha = 1, deformation = 0;
    d3scene::Vec3 color{1,1,1};
    d3scene::Vec3 player_color{1,1,1};
    bool has_player_color = false;
    int custom_texture = -1;
};
ModelAppearance appearance(const object& source) {
    ModelAppearance result;
    if (source.type == OBJ_PLAYER && (is_multi_demo || (Game_mode & GM_MULTI))) {
        result.has_player_color = true;
        result.player_color = ScenePlayerColor(Num_teams > 1 ? PlayerGetTeam(source.id) : source.id);
        const int texture = Players[source.id].custom_texture_handle;
        if (Multi_logo_state && texture >= 0 && texture < MAX_TEXTURES && GameTextures[texture].used &&
            GameTextures[texture].bm_handle != BAD_BITMAP_HANDLE) result.custom_texture = texture;
    }
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

void PrepareGameModelInstances(d3scene::FrameData& data, const d3scene::FrameData* previous,bool retain_materials) {
    unsigned pose_reused = 0, pose_clock = 0, pose_changed = 0;
    const bool audit_geometry = FindArg("-scene-geometry-revision-audit") != 0;
    const bool lod_probe = SceneGeometryInspection() && FindArg("-scene-lod-probe");
    const bool deformation_probe = SceneGeometryInspection() && FindArg("-scene-deformation-probe");
    static std::array<uint64_t,MAX_POLY_MODELS> published_epochs{};
    const int player_color_texture = FindTextureName("MultiColor");
    std::unordered_map<int, d3scene::ModelAssetRef> frame_assets;
    RoomTable rooms;
    for (const auto& room : data.rooms) rooms.emplace(room.number,&room);
    std::unordered_map<uint64_t, const d3scene::ModelInstance*> prior;
    if (previous && previous->scene_lifetime == data.scene_lifetime && previous->simulation_time <= data.simulation_time)
        for (const auto& instance : previous->model_instances) prior.emplace(instance.identity, &instance);
    for (auto& owner : data.objects) {
        auto* source = ObjGet(owner.handle);
        if (!source) throw std::logic_error("Object changed during presentation preparation");
        owner.visible = source->type != OBJ_DUMMY;
        if (source->type == OBJ_OBSERVER && source->id >= 0 && source->id < MAX_PLAYERS) {
            const int followed = Players[source->id].piggy_objnum;
            if (followed >= 0 && followed <= Highest_object_index && Objects[followed].type != OBJ_NONE)
                owner.camera_hidden_objects.push_back(static_cast<uint32_t>(Objects[followed].handle));
        }
        if (source->flags & OF_ATTACHED) {
            const auto* parent = ObjGet(source->attach_ultimate_handle);
            owner.visible = owner.visible && parent && !(parent->render_type == RT_NONE && parent->type != OBJ_POWERUP &&
                (parent->type == OBJ_PLAYER || parent->movement_type != MT_NONE));
        }
        owner.observation_bounds = {value(source->min_xyz),value(source->max_xyz)};
        owner.observation_radius = source->size;
        owner.observation_eligible = owner.visible && source->render_type != RT_NONE &&
            (!OBJECT_OUTSIDE(source) || (!(source->flags & OF_DEAD) && source->type != OBJ_ROOM));
        owner.observation_front_to_back = RenderObjectCanUseSeparatedPasses(source);
        if (source->type == OBJ_WEAPON && source->id >= 0 && source->id < MAX_WEAPONS) {
            const auto flags = Weapons[source->id].flags;
            if (flags & WF_STREAMER) owner.observation_radius = Weapons[source->id].phys_info.velocity.z;
            owner.observation_unbounded = (flags & WF_ELECTRICAL) != 0;
        }
        if (!owner.visible) continue;
        if (source->render_type != RT_POLYOBJ && source->render_type != RT_WEAPON) continue;
        if (source->render_type == RT_WEAPON && (Weapons[source->id].flags &
            (WF_IMAGE_BITMAP | WF_IMAGE_VCLIP | WF_INVISIBLE | WF_ELECTRICAL))) continue;
        d3scene::ModelInstance instance;
        instance.identity = uint64_t(owner.handle)+1;
        instance.owner = owner.handle; instance.has_owner = true; instance.room = owner.room;
        instance.current = owner.current; instance.previous = owner.previous; instance.has_previous = owner.has_previous;
        int model_number = source->type == OBJ_MARKER ? Marker_polynum : source->rtype.pobj_info.model_num;
        if (source->type == OBJ_BUILDING && (source->flags & OF_USE_DESTROYED_POLYMODEL) &&
            Object_info[source->id].lo_render_handle >= 0) model_number = Object_info[source->id].lo_render_handle;
        std::array<int,3> requested{{model_number,model_number,model_number}};
        if ((Detail_settings.Object_complexity != OBJECT_COMPLEXITY_MAX || lod_probe) &&
            (source->lighting_render_type == LRT_STATIC || source->lighting_render_type == LRT_GOURAUD)) {
            int medium = -1, low = -1;
            if (source->type == OBJ_POWERUP || source->type == OBJ_ROBOT || source->type == OBJ_CLUTTER) {
                const auto& definition = Object_info[source->id];
                medium = definition.med_render_handle; low = definition.lo_render_handle;
                instance.lod_distances = {definition.med_lod_distance,definition.lo_lod_distance};
            } else if (source->type == OBJ_PLAYER && !(Players[source->id].flags & (PLAYER_FLAGS_DYING | PLAYER_FLAGS_DEAD))) {
                const auto& definition = Ships[Players[source->id].ship_index];
                medium = definition.med_render_handle; low = definition.lo_render_handle;
                instance.lod_distances = {definition.med_lod_distance,definition.lo_lod_distance};
            }
            const float scale = lod_probe ? 1.f : Detail_settings.Object_complexity == OBJECT_COMPLEXITY_LOW ? .6f :
                Detail_settings.Object_complexity == OBJECT_COMPLEXITY_HIGH ? 1.2f : 1.f;
            instance.lod_distances.x *= scale; instance.lod_distances.y *= scale;
            requested[1] = medium >= 0 ? medium : model_number;
            requested[2] = low >= 0 ? low : requested[1];
        }
        for (size_t tier = 0; tier < requested.size(); ++tier) {
            model_number = requested[tier];
            const auto existing = std::find_if(instance.models.begin(),instance.models.end(),
                [&](const d3scene::PreparedModel& model) { return model.asset->id().slot == uint32_t(model_number); });
            if (existing != instance.models.end()) {
                instance.lod_models[tier] = uint32_t(existing-instance.models.begin());
                continue;
            }
            const uint32_t variant_index = uint32_t(instance.models.size());
            instance.lod_models[tier] = variant_index;
            if (model_number < 0 || model_number >= MAX_POLY_MODELS || !Poly_models[model_number].used)
                throw std::invalid_argument("Object references missing model");
            auto* model = GetPolymodelPointer(model_number);
            if (!model->new_style) throw std::logic_error("Unprepared legacy model format in scene variant");
            auto found = frame_assets.find(model_number);
            if (found == frame_assets.end()) {
                PERF_MARKER_SCOPE("Scene.Models.Asset");
                auto asset = d3scene::assets().model(model_number);
                const auto epoch = SceneModelSourceEpoch();
                if (asset && published_epochs[model_number] == epoch && audit_geometry &&
                    !MatchesSceneModelAsset(asset->geometry(),*model))
                    throw std::logic_error("Model geometry changed without producer invalidation");
                if (!asset || (published_epochs[model_number] != epoch && !MatchesSceneModelAsset(asset->geometry(),*model))) {
                    size_t repaired = 0;
                    auto geometry = CompileSceneModelAsset(*model,&repaired);
                    if (repaired && !asset) AutomatedCaptureLog("scene model=%s repaired shading normals=%u",model->name,static_cast<unsigned>(repaired));
                    asset = d3scene::assets().publishModel(model_number,std::move(geometry));
                }
                published_epochs[model_number] = epoch;
                found = frame_assets.emplace(model_number, std::move(asset)).first;
            }
            d3scene::PreparedModel variant;
            variant.asset = found->second;
            variant.pose_source_revision = SceneModelSourceEpoch();
            const auto old = prior.find(instance.identity);
            const d3scene::PreparedModel* history = nullptr;
            if (old != prior.end()) for (const auto& previous_variant : old->second->models)
                if (previous_variant.asset->id() == variant.asset->id()) { history = &previous_variant; break; }
            const auto ambient = ambientColor(*source,rooms,data.terrain);
            auto effect = appearance(*source);
            // Explicit inspection fixture: exercises prepared-position uploads even
            // when a saved scene has no active cloak/deformation. Never mutates game state.
            if (deformation_probe) effect.deformation = .2f;
            const bool directional = source->lighting_render_type != LRT_STATIC &&
                (source->lighting_render_type == LRT_GOURAUD || NoLightmaps);
            // The saved per-object UV layout is not the shared model's topology.
            // Reject mismatched layouts before indexing (including destroyed-model swaps).
            PerfMarkersBegin("Scene.Models.Layout");
            const bool baked = !NoLightmaps && source->lm_object.used && source->lighting_render_type == LRT_LIGHTMAPS &&
                model_number == source->rtype.pobj_info.model_num && ObjectLightmapsMatchPolymodel(source);
            PerfMarkersEnd("Scene.Models.Layout");
            PerfMarkersBegin("Scene.Models.Pose");
            float normalized[MAX_SUBOBJECTS]{};
            const float* input = nullptr;
            if (source->rtype.pobj_info.anim_frame || model->frame_min != model->frame_max) {
                if (model_number == source->rtype.pobj_info.model_num) SetNormalizedTimeObj(source, normalized);
                else SetNormalizedTimeAnim(source->rtype.pobj_info.anim_frame, normalized, model);
                input = normalized;
            }
            const bool rest = !input && variant.asset->geometry().rest_pose.size() == variant.asset->geometry().parts.size();
            const bool same_inputs = history && (input ? history->pose_inputs &&
                history->pose_inputs->size() == size_t(model->n_models) &&
                std::equal(history->pose_inputs->begin(),history->pose_inputs->end(),input) : !history->pose_inputs);
            if (same_inputs) variant.pose_inputs = history->pose_inputs;
            else if (input) variant.pose_inputs = std::make_shared<const std::vector<float>>(input,input+model->n_models);
            const bool reuse_pose = same_inputs && !variant.asset->geometry().pose_uses_clock &&
                history->pose_source_revision == variant.pose_source_revision;
            pose_reused += reuse_pose; pose_clock += variant.asset->geometry().pose_uses_clock;
            pose_changed += !same_inputs;
            const ModelTransforms* transforms = nullptr;
            if (!rest && !reuse_pose) transforms = &evaluateSharedPose(model_number,*model,input,data.sequence,static_cast<float>(data.simulation_time));
            if (audit_geometry) {
                const auto evaluated = ComposeModelPose(*model,EvaluateModelPose(*model,input,static_cast<float>(data.simulation_time)));
                const auto same = [](d3scene::Vec3 a,d3scene::Vec3 b) { return a.x == b.x && a.y == b.y && a.z == b.z; };
                for (int i = 0; i < evaluated.count; ++i) {
                    const auto a = value(evaluated.submodels[i]);
                    const auto b = reuse_pose ? history->parts[i].current : rest ? variant.asset->geometry().rest_pose[i] : value(transforms->submodels[i]);
                    if (!same(a.position,b.position) || !same(a.right,b.right) || !same(a.up,b.up) || !same(a.forward,b.forward))
                        throw std::logic_error("Model pose changed without changed inputs or producer invalidation");
                }
            }
            PerfMarkersEnd("Scene.Models.Pose");
            const auto roles = ResolveSceneModelRoles(variant.asset->geometry(),source->rtype.pobj_info.subobj_flags,effect.custom_texture >= 0);
            PERF_MARKER_SCOPE("Scene.Models.Parts");
            for (size_t i = 0; i < variant.asset->geometry().parts.size(); ++i) {
                const auto& asset_part = variant.asset->geometry().parts[i];
                d3scene::PreparedModelPart part;
                part.current = reuse_pose ? history->parts[i].current : rest ? variant.asset->geometry().rest_pose[i] : value(transforms->submodels[i]);
                part.previous = history ? history->parts[i].current : part.current;
                part.has_previous = history && history->parts[i].draw && instance.has_previous;
                part.vertices = d3scene::deformMesh(asset_part.geometry,effect.deformation,data.simulation_time,owner.handle,static_cast<uint32_t>(i));
                part.previous_vertices = history ? history->parts[i].vertices : part.vertices;
                const auto role = roles[i];
                part.draw = role == SceneModelPartRole::Surface;
                const auto attachment_begin = data.meshes.size();
                if (role == SceneModelPartRole::Facing)
                    prepareFacingPart(data,&owner,*model,i,d3scene::compose(instance.current,part.current));
                if (role == SceneModelPartRole::Glow)
                    prepareGlowPart(data,&owner,source,*model,i,d3scene::compose(instance.current,part.current));
                for (size_t attachment = attachment_begin; attachment < data.meshes.size(); ++attachment) {
                    data.meshes[attachment].model_instance = instance.identity;
                    data.meshes[attachment].model_variant = static_cast<int32_t>(variant_index);
                }
                part.material_palette = !baked;
                part.baked_layout_revision = baked ? ObjectLightmapLayoutRevision() : 0;
                part.appearance_inputs = {{ambient.x,ambient.y,ambient.z,effect.alpha,effect.color.x,effect.color.y,effect.color.z,
                    float(directional),float(source->rtype.pobj_info.tmap_override),float(effect.custom_texture),float(effect.has_player_color),
                    effect.player_color.x,effect.player_color.y,effect.player_color.z,float(player_color_texture),float(part.draw)}};
                const auto* old_part = history ? &history->parts[i] : nullptr;
                bool reuse_materials = retain_materials && old_part && old_part->material_palette == part.material_palette &&
                    old_part->appearance_inputs == part.appearance_inputs && old_part->baked_layout_revision == part.baked_layout_revision &&
                    old_part->surfaces.publishedFor(variant.asset->id(),uint32_t(i),part.material_palette);
                if (reuse_materials && baked && part.draw && audit_geometry) {
                    for (size_t face = 0; reuse_materials && face < asset_part.geometry.polygons.size(); ++face) {
                        const auto& source_face = source->lm_object.lightmap_faces[i][face];
                        const auto& retained = old_part->surfaces[face];
                        reuse_materials = retained.lightmap == source_face.lmi_handle &&
                            retained.lightmap_bitmap == LightmapInfo[source_face.lmi_handle].lm_handle &&
                            retained.baked_uvs.size() == asset_part.geometry.polygons[face].corners.size();
                        for (size_t corner = 0; reuse_materials && corner < retained.baked_uvs.size(); ++corner)
                            reuse_materials = retained.baked_uvs[corner].x == source_face.u2[corner] && retained.baked_uvs[corner].y == source_face.v2[corner];
                    }
                    if (!reuse_materials) throw std::logic_error("Object lightmap layout changed without producer invalidation");
                }
                if (reuse_materials) {
                    part.surfaces = old_part->surfaces;
                    variant.parts.push_back(std::move(part));
                    continue;
                }
                const size_t surface_count = part.material_palette ? asset_part.surface_polygons.size() : asset_part.geometry.polygons.size();
                part.surfaces.reserve(surface_count);
                for (size_t material = 0; material < surface_count; ++material) {
                    const size_t j = part.material_palette ? asset_part.surface_polygons[material] : material;
                    d3scene::SurfaceBinding binding;
                    binding.occlusion_class = d3scene::OcclusionClass::Model;
                    // Normal authored polymodel transparency writes late raster
                    // depth for self-occlusion, but never enters opaque products.
                    binding.depth = d3scene::DepthMode::ReadWrite;
                    binding.ambient_color = ambient;
                    binding.directional_ambient = directional;
                    binding.alpha = effect.alpha;
                    binding.color_modulation = effect.color;
                    const int slot = asset_part.polygon_materials[j];
                    if (slot >= 0) binding.texture = variant.asset->geometry().textures[slot];
                    else binding.base_color = asset_part.polygon_colors[j];
                    if (source->rtype.pobj_info.tmap_override != -1) binding.texture = source->rtype.pobj_info.tmap_override;
                    if ((asset_part.authored_flags & SOF_CUSTOM) && effect.custom_texture >= 0 && binding.texture >= 0)
                        binding.texture = effect.custom_texture;
                    if (effect.has_player_color && binding.texture >= 0 && binding.texture == player_color_texture) {
                        // Authored team-color panels use flat ambient response,
                        // not the directional shading of the surrounding hull.
                        binding.directional_ambient = false;
                        binding.color_modulation = {effect.color.x*effect.player_color.x,
                            effect.color.y*effect.player_color.y,effect.color.z*effect.player_color.z};
                    }
                    if (baked && part.draw) {
                        const auto& face = source->lm_object.lightmap_faces[i][j];
                        binding.lightmap = face.lmi_handle;
                        for (size_t corner = 0; corner < asset_part.geometry.polygons[j].corners.size(); ++corner)
                            binding.baked_uvs.push_back({face.u2[corner],face.v2[corner]});
                    }
                    part.surfaces.push_back(binding);
                }
                variant.parts.push_back(std::move(part));
            }
            instance.models.push_back(std::move(variant));
        }
        if (!instance.models.empty()) data.model_instances.push_back(std::move(instance));
    }
    data.observation_inputs_prepared = true;
    if (AutomatedCapturePending()) AutomatedCaptureLog("scene pose cache reused=%u clock=%u changed=%u",pose_reused,pose_clock,pose_changed);
}

void PrepareGameStaticModel(d3scene::FrameData& data,int number,const d3scene::Transform& placement,
    uint64_t identity,uint64_t collection) {
    if (number < 0 || number >= MAX_POLY_MODELS || !Poly_models[number].used)
        throw std::invalid_argument("Static scene model is unavailable");
    const auto* source = GetPolymodelPointer(number);
    d3scene::ModelInstance instance;
    instance.identity = identity; instance.collection = collection; instance.current = instance.previous = placement;
    d3scene::PreparedModel model;
    model.asset = d3scene::assets().publishModel(number,CompileSceneModelAsset(*source));
    const auto pose = EvaluateModelPose(*source,nullptr,static_cast<float>(data.simulation_time));
    const auto transforms = ComposeModelPose(*source,pose);
    const auto roles = ResolveSceneModelRoles(model.asset->geometry(),0xffffffffu,false);
    for (size_t index = 0; index < model.asset->geometry().parts.size(); ++index) {
        const auto& geometry = model.asset->geometry().parts[index];
        d3scene::PreparedModelPart part;
        part.current = part.previous = value(transforms.submodels[index]);
        part.draw = roles[index] == SceneModelPartRole::Surface;
        const auto attachment_begin = data.meshes.size();
        const auto world = d3scene::compose(placement,part.current);
        if (roles[index] == SceneModelPartRole::Facing) prepareFacingPart(data,nullptr,*source,index,world);
        if (roles[index] == SceneModelPartRole::Glow) prepareGlowPart(data,nullptr,nullptr,*source,index,world);
        for (size_t attachment = attachment_begin; attachment < data.meshes.size(); ++attachment) {
            data.meshes[attachment].collection = collection;
            data.meshes[attachment].model_instance = identity;
            data.meshes[attachment].model_variant = 0;
        }
        for (size_t polygon = 0; polygon < geometry.geometry.polygons.size(); ++polygon) {
            d3scene::SurfaceBinding surface; surface.unlit = true;
            const int material = geometry.polygon_materials[polygon];
            if (material >= 0) surface.texture = model.asset->geometry().textures[material];
            else surface.base_color = geometry.polygon_colors[polygon];
            part.surfaces.push_back(std::move(surface));
        }
        model.parts.push_back(std::move(part));
    }
    instance.models.push_back(std::move(model));
    data.model_instances.push_back(std::move(instance));
}
