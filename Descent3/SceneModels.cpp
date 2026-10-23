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
#include "SceneWeaponVisuals.h"
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

extern bool is_multi_demo;

namespace {
d3scene::Vec3 value(const vector& v) { return {v.x, v.y, v.z}; }
d3scene::Transform value(const ModelSubmodelTransform& t) {
    return {value(t.translation), value(t.orientation.rvec), value(t.orientation.uvec), value(t.orientation.fvec)};
}
// The game producer's room namespace is bounded. No hash table or heap
// allocation is needed to translate it to this preparation's owned rooms.
using RoomTable = std::array<const d3scene::RoomState*,MAX_ROOMS>;
const d3scene::RoomState* roomAt(const RoomTable& rooms,int number) {
    return number >= 0 && number < MAX_ROOMS ? rooms[number] : nullptr;
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
d3scene::ModelAmbientInput ambientInput(const object& source,const RoomTable& rooms,const d3scene::TerrainState& terrain) {
    d3scene::ModelAmbientInput input;
    using Source = d3scene::ModelAmbientInput::Source;
    const bool outside = OBJECT_OUTSIDE(&source);
    const bool volume = source.effect_info && (source.effect_info->type_flags & EF_VOLUME_LIT);
    if (outside) {
        if (source.type == OBJ_POWERUP) return input;
        input.source = Source::Terrain; input.terrain_cell = CELLNUM(source.roomnum);
        input.terrain = terrain.asset;
    } else {
        if (source.lighting_render_type == LRT_STATIC ||
            !(source.lighting_render_type == LRT_GOURAUD || NoLightmaps) || !volume) return input;
        const auto* found = roomAt(rooms,source.roomnum);
        if (!found || (found->authored_flags & RF_EXTERNAL)) return input;
        input.source = Source::RoomVolume;
        input.volume = found->asset; input.volume_scalar = found->light_scalar;
        const auto& effect = *source.effect_info;
        if (effect.type_flags & EF_VOLUME_CHANGING) {
            input.blend_previous = true;
            const auto* old = roomAt(rooms,effect.volume_old_room);
            if (old) { input.previous_volume = old->asset; input.previous_scalar = old->light_scalar; }
            input.previous_position = value(effect.volume_old_pos); input.previous_weight = effect.volume_change_time;
        }
    }
    if (volume) {
        if (source.type == OBJ_ROBOT) input.minimum = outside ? .25f : .1f;
        if (source.type == OBJ_PLAYER) {
            if ((Players[source.id].flags & PLAYER_FLAGS_HEADLIGHT) ||
                ((Game_mode & GM_MULTI) && (Netgame.flags & NF_BRIGHT_PLAYERS))) return {};
            if (!outside && (Game_mode & GM_MULTI)) {
                vector velocity = source.mtype.phys_info.velocity;
                input.multiplier = 1+std::min(vm_GetMagnitudeFast(&velocity)/20.f,1.f);
                input.clamp_after_multiplier = true;
            }
        }
    }
    return input;
}
}

d3scene::ModelAssetRef PrepareGameModelAsset(int number,bool audit_geometry) {
    if (number < 0 || number >= MAX_POLY_MODELS || !Poly_models[number].used)
        throw std::invalid_argument("Scene model is unavailable");
    const auto* model = GetPolymodelPointer(number);
    if (!model->new_style) throw std::logic_error("Unprepared legacy model format in scene variant");
    static std::array<uint64_t,MAX_POLY_MODELS> published_epochs{};
    auto asset = d3scene::assets().model(number);
    const auto epoch = SceneModelSourceEpoch();
    if (asset && published_epochs[number] == epoch && audit_geometry &&
        !MatchesSceneModelAsset(asset->geometry(),*model))
        throw std::logic_error("Model geometry changed without producer invalidation");
    if (!asset || (published_epochs[number] != epoch && !MatchesSceneModelAsset(asset->geometry(),*model))) {
        size_t repaired = 0, repaired_uvs = 0;
        auto geometry = CompileSceneModelAsset(*model,&repaired,&repaired_uvs);
        if (repaired && !asset) AutomatedCaptureLog("scene model=%s repaired shading normals=%u",model->name,static_cast<unsigned>(repaired));
        if (repaired_uvs) AutomatedCaptureLog("scene model=%s repaired UV components=%u",model->name,static_cast<unsigned>(repaired_uvs));
        try {
            asset = d3scene::assets().publishModel(number,std::move(geometry));
        } catch (const std::exception& error) {
            AutomatedCaptureLog("scene model publication failed model=%s reason=%s",model->name,error.what());
            throw;
        }
    }
    published_epochs[number] = epoch;
    return asset;
}

void PrepareGameModelInstances(d3scene::FrameData& data, const d3scene::Presentation* previous_frame,bool retain_materials) {
    const auto* previous = previous_frame ? &previous_frame->data() : nullptr;
    if (previous && (previous->scene_lifetime != data.scene_lifetime || previous->simulation_time > data.simulation_time))
        previous_frame = nullptr;
    data.model_instances.reserve(data.objects.size()+3);
    data.model_variants.reserve(previous_frame ? previous->model_variants.size() : data.objects.size());
    if (previous_frame) data.model_pose_inputs.reserve(previous->model_pose_inputs.size());
    if (previous_frame) data.model_ambient_inputs.reserve(previous->model_ambient_inputs.size());
    const bool audit_geometry = FindArg("-scene-geometry-revision-audit") != 0;
    const bool lod_probe = SceneGeometryInspection() && FindArg("-scene-lod-probe");
    const bool deformation_probe = SceneGeometryInspection() && FindArg("-scene-deformation-probe");
    const int player_color_texture = FindTextureName("MultiColor");
    // Reuse only lookup capacity. Every transaction discards all old entries
    // before resolving this frame's model revisions; no mutable source is kept.
    static thread_local std::array<d3scene::ModelAssetRef,MAX_POLY_MODELS> frame_assets;
    static thread_local std::vector<int> frame_asset_slots;
    for (const auto slot : frame_asset_slots) frame_assets[slot].reset();
    frame_asset_slots.clear();
    // A published palette belongs to an asset/appearance, not an object pose.
    // Borrow one canonical table per asset during this transaction. Distinct
    // appearances keep their own history; per-object baked UVs never share.
    std::array<const d3scene::PreparedModel*,MAX_POLY_MODELS> material_sources{};
    RoomTable rooms{};
    for (const auto& room : data.rooms) {
        if (room.number < 0 || room.number >= MAX_ROOMS) throw std::invalid_argument("Game room outside producer registry");
        rooms[room.number] = &room;
    }
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
        if (source->render_type == RT_WEAPON && CompileWeaponVisuals(Weapons[source->id].flags).body != WeaponBodyVisual::Model) continue;
        d3scene::ModelInstance instance;
        instance.identity = GameObjectModelIdentity(owner.handle);
        instance.deformation_response = GameModelDeformationResponse();
        instance.owner = owner.handle; instance.has_owner = true; instance.room = owner.room;
        instance.compiled_owner = size_t(&owner-data.objects.data());
        instance.current = owner.current; instance.previous = owner.previous; instance.has_previous = owner.has_previous;
        instance.ambient_input = data.appendAmbientInput(ambientInput(*source,rooms,data.terrain));
        auto effect = appearance(*source);
        if (deformation_probe) effect.deformation = .2f;
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
        // History identifies an instance, not one of its LOD variants. Resolve
        // it once. Stable inventory order supplies a checked ordinal; insertion,
        // removal and allocation reuse fall back to the full identity index.
        const d3scene::ModelInstance* previous_instance = nullptr;
        if (previous_frame) {
            const size_t ordinal = data.model_instances.size();
            if (ordinal < previous->model_instances.size() && previous->model_instances[ordinal].identity == instance.identity)
                previous_instance = &previous->model_instances[ordinal];
            else if (const auto* found = previous_frame->modelIndices().find(instance.identity))
                previous_instance = &previous->model_instances[found->second];
        }
        for (size_t tier = 0; tier < requested.size(); ++tier) {
            model_number = requested[tier];
            if (instance.model_count) {
            const auto variants = data.variants(instance);
            const auto existing = std::find_if(variants.begin(),variants.end(),
                [&](const d3scene::PreparedModel& model) { return model.asset->id().slot == uint32_t(model_number); });
            if (existing != variants.end()) {
                instance.lod_models[tier] = uint32_t(existing-variants.begin());
                continue;
            }
            }
            const uint32_t variant_index = instance.model_count;
            instance.lod_models[tier] = variant_index;
            if (model_number < 0 || model_number >= MAX_POLY_MODELS || !Poly_models[model_number].used)
                throw std::invalid_argument("Object references missing model");
            auto* model = GetPolymodelPointer(model_number);
            if (!model->new_style) throw std::logic_error("Unprepared legacy model format in scene variant");
            auto& asset = frame_assets[model_number];
            if (!asset) {
                PERF_MARKER_SCOPE("Scene.Models.Asset");
                frame_asset_slots.push_back(model_number);
                asset = PrepareGameModelAsset(model_number,audit_geometry);
            }
            d3scene::PreparedModel variant;
            variant.asset = asset;
            variant.pose_source_revision = SceneModelSourceEpoch();
            const d3scene::PreparedModel* history = nullptr;
            if (previous_instance) for (const auto& previous_variant : previous->variants(*previous_instance))
                if (previous_variant.asset->id() == variant.asset->id()) { history = &previous_variant; break; }
            variant.modulation = {{1,1,1},effect.color,effect.alpha};
            const bool directional = source->lighting_render_type != LRT_STATIC &&
                (source->lighting_render_type == LRT_GOURAUD || NoLightmaps);
            // The saved per-object UV layout is not the shared model's topology.
            // Reject mismatched layouts before indexing (including destroyed-model swaps).
            PerfMarkersBegin("Scene.Models.Layout");
            const auto layout_revision = ObjectLightmapLayoutRevision();
            const bool baked_candidate = !NoLightmaps && source->lm_object.used && source->lighting_render_type == LRT_LIGHTMAPS &&
                source->render_type == RT_POLYOBJ && model_number == source->rtype.pobj_info.model_num;
            // A retained successful binding is also a compatibility proof for
            // this object generation, immutable model revision and allocation
            // epoch. Setup/clear invalidate the epoch; model replacement cannot
            // find this history. Never reuse a negative or unvalidated result.
            const bool layout_proven = history && history->binding_inputs.valid &&
                history->binding_inputs.baked_layout_revision == layout_revision &&
                history->pose_source_revision == variant.pose_source_revision;
            const bool checked_layout = baked_candidate && (!layout_proven || audit_geometry) ?
                ObjectLightmapsMatchPolymodel(source) : layout_proven;
            if (baked_candidate && layout_proven && audit_geometry && !checked_layout)
                throw std::logic_error("Object lightmap compatibility changed without producer invalidation");
            const bool baked = baked_candidate && checked_layout;
            PerfMarkersEnd("Scene.Models.Layout");
            PerfMarkersBegin("Scene.Models.Pose");
            float normalized[MAX_SUBOBJECTS]{};
            const float* input = nullptr;
            if (source->rtype.pobj_info.anim_frame || model->frame_min != model->frame_max) {
                if (model_number == source->rtype.pobj_info.model_num) SetNormalizedTimeObj(source, normalized);
                else SetNormalizedTimeAnim(source->rtype.pobj_info.anim_frame, normalized, model);
                input = normalized;
            }
            if (input) variant.pose_inputs = data.appendPoseInputs(input,model->n_models);
            variant.deferred_pose = true;
            variant.previous_pose_valid = history && instance.has_previous;
            variant.previous_pose_inputs = variant.pose_inputs;
            if (history) {
                const auto prior = previous->poseInputs(history->pose_inputs);
                const bool same_inputs = prior.size() == variant.pose_inputs.count &&
                    (prior.empty() || std::equal(prior.begin(),prior.end(),input));
                if (!same_inputs) variant.previous_pose_inputs = data.appendPoseInputs(prior.data(),prior.size());
            }
            variant.previous_pose_time = history ? previous->simulation_time : data.simulation_time;
            variant.deformation = effect.deformation;
            variant.previous_deformation = history ? history->deformation : effect.deformation;
            variant.deformation_owner = owner.handle;
            PerfMarkersEnd("Scene.Models.Pose");
            // RT_WEAPON never consumed the polymodel texture override. Older
            // weapon objects (including saves) leave it zero, which is the
            // random diagnostic texture, not an authored material override.
            const int texture_override = source->render_type == RT_POLYOBJ ? source->rtype.pobj_info.tmap_override : -1;
            variant.binding_inputs.appearance = {{float(directional),float(texture_override),
                float(effect.custom_texture),float(effect.has_player_color),effect.player_color.x,
                effect.player_color.y,effect.player_color.z,float(player_color_texture)}};
            variant.binding_inputs.baked_layout_revision = baked ? layout_revision : 0;
            variant.binding_inputs.subobject_mask = source->rtype.pobj_info.subobj_flags;
            variant.binding_inputs.material_palette = !baked;
            variant.binding_inputs.valid = true;
            const auto matches_material = [&](const d3scene::PreparedModel* source) {
                return source && source->binding_inputs == variant.binding_inputs &&
                    source->parts.publishedFor(variant.asset->id());
            };
            const d3scene::PreparedModel* material = nullptr;
            if (retain_materials) {
                const auto* shared = baked ? nullptr : material_sources[model_number];
                if (matches_material(shared)) material = shared;
                else if (matches_material(history)) material = history;
                if (!baked && material && !material_sources[model_number]) material_sources[model_number] = material;
            }
            const bool reuse_materials = material != nullptr;
            if (reuse_materials) variant.parts = material->parts;
            else variant.parts.resize(variant.asset->geometry().parts.size());
            variant.previous_draw_mask = 0;
            PERF_MARKER_SCOPE("Scene.Models.Parts");
            if (history && instance.has_previous) {
                if (const auto* summary = history->parts.summary()) variant.previous_draw_mask = summary->drawn_parts;
                else for (size_t i = 0; i < history->parts.size(); ++i)
                    if (history->parts[i].draw) variant.previous_draw_mask |= uint32_t(1)<<i;
            }
            const auto append_attachment = [&](size_t i,SceneModelPartRole role) {
                const auto begin = data.meshes.size();
                if (role == SceneModelPartRole::Facing) prepareFacingPart(data,&owner,*model,i,{});
                if (role == SceneModelPartRole::Glow) prepareGlowPart(data,&owner,source,*model,i,{});
                for (size_t attachment = begin; attachment < data.meshes.size(); ++attachment) {
                    auto& mesh = data.meshes[attachment];
                    mesh.compiled_owner = instance.compiled_owner;
                    mesh.model_instance = instance.identity;
                    mesh.model_variant = static_cast<int32_t>(variant_index);
                    mesh.model_part = static_cast<int32_t>(i);
                }
            };
            // The asset owns attachment inventory. Reusing a material table
            // must not restart the full authored hierarchy walk per instance.
            if (reuse_materials && !(baked && audit_geometry)) {
                for (const auto i : variant.asset->viewAttachments()) {
                    const auto& part = variant.asset->geometry().parts[i];
                    if (!part.hierarchy_hidden) append_attachment(i,ResolveSceneModelPartRole(part.authored_flags,
                        source->rtype.pobj_info.subobj_flags,i,effect.custom_texture >= 0));
                }
                data.appendModel(instance,std::move(variant));
                continue;
            }
            const auto roles = ResolveSceneModelRoles(*variant.asset,source->rtype.pobj_info.subobj_flags,effect.custom_texture >= 0);
            for (size_t i = 0; i < variant.asset->geometry().parts.size(); ++i) {
                const auto& asset_part = variant.asset->geometry().parts[i];
                const bool has_previous = (variant.previous_draw_mask & (uint32_t(1)<<i)) != 0;
                const auto role = roles[i];
                const bool draw = role == SceneModelPartRole::Surface;
                append_attachment(i,role);
                const auto* old_part = history ? &history->parts[i] : nullptr;
                if (reuse_materials && baked && draw && audit_geometry) {
                    bool matches = true;
                    for (size_t face = 0; matches && face < asset_part.geometry.polygons.size(); ++face) {
                        const auto& source_face = source->lm_object.lightmap_faces[i][face];
                        const auto& retained = old_part->surfaces[face];
                        matches = retained.lightmap == source_face.lmi_handle &&
                            retained.lightmap_bitmap == LightmapInfo[source_face.lmi_handle].lm_handle &&
                            retained.baked_uvs.size() == asset_part.geometry.polygons[face].corners.size();
                        for (size_t corner = 0; matches && corner < retained.baked_uvs.size(); ++corner)
                            matches = retained.baked_uvs[corner].x == source_face.u2[corner] && retained.baked_uvs[corner].y == source_face.v2[corner];
                    }
                    if (!matches) throw std::logic_error("Object lightmap layout changed without producer invalidation");
                }
                if (reuse_materials) {
                    continue;
                }
                d3scene::PreparedModelPart part;
                part.has_previous = has_previous;
                part.draw = draw;
                part.material_palette = !baked;
                const size_t surface_count = part.material_palette ? asset_part.surface_polygons.size() : asset_part.geometry.polygons.size();
                part.surfaces.reserve(surface_count);
                for (size_t material = 0; material < surface_count; ++material) {
                    const size_t j = part.material_palette ? asset_part.surface_polygons[material] : material;
                    d3scene::SurfaceBinding binding;
                    binding.occlusion_class = d3scene::OcclusionClass::Model;
                    // Normal authored polymodel transparency writes late raster
                    // depth for self-occlusion, but never enters opaque products.
                    binding.depth = d3scene::DepthMode::ReadWrite;
                    binding.directional_ambient = directional;
                    const int slot = asset_part.polygon_materials[j];
                    if (slot >= 0) binding.texture = variant.asset->geometry().textures[slot];
                    else binding.base_color = asset_part.polygon_colors[j];
                    if (texture_override != -1) binding.texture = texture_override;
                    if (source->type == OBJ_WEAPON && FindArg("-capture-fire-flare"))
                        AutomatedCaptureLog("weapon material model=%s slot=%d authored=%d override=%d resolved=%d",model->name,slot,
                            slot >= 0 ? variant.asset->geometry().textures[slot] : -1,texture_override,binding.texture);
                    if ((asset_part.authored_flags & SOF_CUSTOM) && effect.custom_texture >= 0 && binding.texture >= 0)
                        binding.texture = effect.custom_texture;
                    if (effect.has_player_color && binding.texture >= 0 && binding.texture == player_color_texture) {
                        // Authored team-color panels use flat ambient response,
                        // not the directional shading of the surrounding hull.
                        binding.directional_ambient = false;
                        binding.color_modulation = effect.player_color;
                    }
                    if (baked && part.draw) {
                        const auto& face = source->lm_object.lightmap_faces[i][j];
                        binding.lightmap = face.lmi_handle;
                        for (size_t corner = 0; corner < asset_part.geometry.polygons[j].corners.size(); ++corner)
                            binding.baked_uvs.push_back({face.u2[corner],face.v2[corner]});
                    }
                    part.surfaces.push_back(binding);
                }
                variant.parts[i] = std::move(part);
            }
            data.appendModel(instance,std::move(variant));
        }
        if (instance.model_count) data.model_instances.push_back(std::move(instance));
    }
    data.observation_inputs_prepared = true;
}

void AuditGameModelPoses(const d3scene::Presentation& frame) {
    const auto same = [](d3scene::Vec3 a,d3scene::Vec3 b) { return a.x == b.x && a.y == b.y && a.z == b.z; };
    for (size_t i = 0; i < frame.data().model_instances.size(); ++i) {
        const auto& instance = frame.data().model_instances[i];
        for (size_t v = 0; v < instance.model_count; ++v) {
            const auto& source = frame.data().variants(instance)[v];
            if (!source.deferred_pose) continue;
            if (!source.composed_pose.empty()) {
                const auto transforms = frame.data().modelTransforms(source.composed_pose);
                const auto previous = frame.data().modelTransforms(source.previous_composed_pose);
                const auto& resolved = frame.resolvedModel(i,v);
                const auto check = [&](const d3scene::Transform& a,const d3scene::Transform& b) {
                    if (!same(a.position,b.position) || !same(a.right,b.right) || !same(a.up,b.up) || !same(a.forward,b.forward))
                        throw std::logic_error("Resolved pose differs from composed producer inputs");
                };
                for (size_t part = 0; part < transforms.size(); ++part) {
                    check(transforms[part],resolved.parts[part].current);
                    check(source.previous_pose_valid ? previous[part] : transforms[part],resolved.parts[part].previous);
                }
                continue;
            }
            const auto* authored = GetPolymodelPointer(int(source.asset->id().slot));
            const auto expected = ComposeModelPose(*authored,EvaluateModelPose(*authored,
                frame.data().poseInputs(source.pose_inputs).data(),float(frame.data().simulation_time)));
            const auto& resolved = frame.resolvedModel(i,v);
            for (int part = 0; part < expected.count; ++part) {
                const auto a = value(expected.submodels[part]); const auto& b = resolved.parts[part].current;
                if (!same(a.position,b.position) || !same(a.right,b.right) || !same(a.up,b.up) || !same(a.forward,b.forward))
                    throw std::logic_error("Deferred pose differs from source evaluation");
            }
        }
    }
}

void PrepareGameStaticModel(d3scene::FrameData& data,int number,const d3scene::Transform& placement,
    uint64_t identity,uint64_t collection) {
    if (number < 0 || number >= MAX_POLY_MODELS || !Poly_models[number].used)
        throw std::invalid_argument("Static scene model is unavailable");
    const auto* source = GetPolymodelPointer(number);
    d3scene::ModelInstance instance;
    instance.identity = identity; instance.collection = collection; instance.current = instance.previous = placement;
    instance.deformation_response = GameModelDeformationResponse();
    d3scene::PreparedModel model;
    model.asset = PrepareGameModelAsset(number,FindArg("-scene-geometry-revision-audit") != 0);
    const auto& animation = *model.asset->geometry().animation;
    const auto pose = EvaluateModelPose(animation,nullptr,static_cast<float>(data.simulation_time));
    const auto transforms = ComposeModelPose(animation,pose);
    const auto roles = ResolveSceneModelRoles(*model.asset,0xffffffffu,false);
    for (size_t index = 0; index < model.asset->geometry().parts.size(); ++index) {
        const auto& geometry = model.asset->geometry().parts[index];
        d3scene::PreparedModelPart part;
        part.current = part.previous = value(transforms.submodels[index]);
        part.draw = roles[index] == SceneModelPartRole::Surface;
        part.material_palette = true;
        const auto attachment_begin = data.meshes.size();
        const auto world = d3scene::compose(placement,part.current);
        if (roles[index] == SceneModelPartRole::Facing) prepareFacingPart(data,nullptr,*source,index,world);
        if (roles[index] == SceneModelPartRole::Glow) prepareGlowPart(data,nullptr,nullptr,*source,index,world);
        for (size_t attachment = attachment_begin; attachment < data.meshes.size(); ++attachment) {
            data.meshes[attachment].collection = collection;
            data.meshes[attachment].model_instance = identity;
            data.meshes[attachment].model_variant = 0;
        }
        for (const auto polygon : geometry.surface_polygons) {
            d3scene::SurfaceBinding surface; surface.unlit = true;
            const int material = geometry.polygon_materials[polygon];
            if (material >= 0) surface.texture = model.asset->geometry().textures[material];
            else surface.base_color = geometry.polygon_colors[polygon];
            part.surfaces.push_back(std::move(surface));
        }
        model.parts.push_back(std::move(part));
    }
    data.appendModel(instance,std::move(model));
    data.model_instances.push_back(std::move(instance));
}
