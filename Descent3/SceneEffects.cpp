// Descent 3MASTERED - GPL-3.0-or-later
#include "SceneExtraction.h"
#include "../renderer/SceneGeometry.h"
#include "../renderer/SceneAnimation.h"
#include "viseffect.h"
#include "fireball.h"
#include "vclip.h"
#include "gametexture.h"
#include "bitmap.h"
#include "descent.h"
#include "config.h"
#include "gameloop.h"
#include "game.h"
#include "object.h"
#include "args.h"
#include "weapon.h"
#include <cmath>
#include <algorithm>
namespace {
bool special(int id) {
    switch (id) {
    case LIGHTNING_BOLT_INDEX: case GRAY_LIGHTNING_BOLT_INDEX: case MASSDRIVER_EFFECT_INDEX:
    case MERCBOSS_MASSDRIVER_EFFECT_INDEX: case BILLBOARD_SMOKETRAIL_INDEX: case THICK_LIGHTNING_INDEX:
    case SINE_WAVE_INDEX: case BLAST_RING_INDEX: case FADING_LINE_INDEX: case SNOWFLAKE_INDEX:
    case RAINDROP_INDEX: case PUDDLEDROP_INDEX: case AXIS_BILLBOARD_INDEX: return true;
    default: return false;
    }
}
d3scene::Vec3 value(const vector& v) { return {v.x,v.y,v.z}; }
d3scene::Transform basis(const matrix& m) { return {{},value(m.rvec),value(m.uvec),value(m.fvec)}; }
float alphaByte(float alpha) { return static_cast<int>((std::max)(0.f,(std::min)(255.f,alpha*255.f)))/255.f; }
d3scene::PreparedMesh prepareRing(float radius,float phase,float alpha,int bitmap,int room) {
    // One immutable topology/material-coordinate layout, with frame-owned
    // procedural positions. No new asset revision for every expanding ring.
    auto geometry = d3scene::billboardRing(.5f,0,255,20);
    for (auto& polygon : geometry.polygons) {
        polygon.corners[0].u = 0; polygon.corners[0].v = 0;
        polygon.corners[1].u = 1; polygon.corners[1].v = 0;
        polygon.corners[2].u = 1; polygon.corners[2].v = 1;
        polygon.corners[3].u = 0; polygon.corners[3].v = 1;
    }
    d3scene::PreparedMesh ring;
    ring.asset = d3scene::assets().publishMesh(5,std::move(geometry));
    ring.room = room;
    for (int i = 0; i < 20; ++i) {
        const auto angle = static_cast<::angle>((i*(65536/20)+static_cast<int>(phase*65536))%65536);
        const float x = FixCos(angle)*radius, y = FixSin(angle)*radius;
        ring.vertices.push_back({x,y,0}); ring.vertices.push_back({x*.5f,y*.5f,0});
    }
    auto& surface = ring.surface;
    surface.bitmap = bitmap; surface.unlit = true; surface.two_sided = true;
    surface.texture_alpha = true; surface.texture_wrap = true;
    surface.blend = d3scene::BlendMode::Additive; surface.alpha = alpha;
    return ring;
}
bool prepareVisRing(d3scene::FrameData& frame,const vis_effect& effect) {
    if (!(effect.lifetime > 0) || !std::isfinite(effect.size) || effect.size <= 0 ||
        effect.custom_handle <= BAD_BITMAP_HANDLE || effect.custom_handle >= MAX_BITMAPS ||
        !GameBitmaps[effect.custom_handle].used) return false;
    float life = (effect.lifetime-effect.lifeleft)/effect.lifetime;
    if (effect.flags & VF_REVERSE) life = 1-life;
    life = (std::max)(0.f,(std::min)(1.f,life));
    auto ring = prepareRing(life*effect.size,0,1-life,effect.custom_handle,effect.roomnum);
    if (effect.flags & VF_PLANAR) {
        vector normal = effect.end_pos;
        if (vm_GetMagnitudeFast(&normal) < .5f) return false;
        matrix plane; vm_VectorToMatrix(&plane,&normal,nullptr,nullptr);
        ring.world = basis(plane);
    } else {
        ring.alignment = d3scene::MeshAlignment::FacingEye;
        // FacingEye's canonical forward points away from the eye; this authored
        // effect uses the opposite normal, including its texture handedness.
        ring.world.right = {-1,0,0}; ring.world.forward = {0,0,-1};
    }
    ring.world.position = value(effect.pos);
    ring.surface.u_offset = life; ring.surface.effect_depth_offset = -1;
    ring.surface.soft_depth_range = Render_soft_vis_effects && !(effect.flags & VF_NO_SOFT_PARTICLES) ? 2.f : 0.f;
    frame.meshes.push_back(std::move(ring));
    return true;
}
bool prepareObjectRing(d3scene::FrameData& frame,const d3scene::ObjectState& owner,const object& source,
    const d3scene::MeshAssetRef& quad) {
    if (!(source.lifetime > 0) || !(source.ctype.blast_info.max_size > 0)) return false;
    const int bitmap = source.ctype.blast_info.bm_handle;
    if (bitmap <= BAD_BITMAP_HANDLE || bitmap >= MAX_BITMAPS || !GameBitmaps[bitmap].used) return false;
    const float life = (std::max)(0.f,(std::min)(1.f,(source.lifetime-source.lifeleft)/source.lifetime));
    const bool gravity = source.id == GRAVITY_FIELD_INDEX;
    const float radius = (gravity ? 1-life : life)*source.ctype.blast_info.max_size;
    const float phase = life < .2f ? life*3 : .6f+(life-.2f)*.5f;
    auto ring = prepareRing(radius*2,phase,1-phase,bitmap,source.roomnum);
    // The old gravity draw forced ObjSetOrient(identity) during rendering.
    // This is a presentation choice, never a simulation orientation update.
    const auto& orientation = gravity ? Identity_matrix : source.orient;
    ring.world = basis(orientation);
    ring.world.up = value(orientation.fvec); ring.world.forward = value(-orientation.uvec);
    ring.world.position = value(source.pos); ring.has_owner = true; ring.owner = owner.handle;
    ring.surface.effect_depth_offset = -radius*.5f;
    ring.surface.soft_depth_range = Render_soft_vis_effects ? 2.f : 0.f;
    const float disk_scale = life < .1f ? life*10 : (std::max)(0.f,1-(life-.1f)*1.5f);
    const int disk_bitmap = Fireballs[SHRINKING_BLAST_INDEX].bm_handle;
    if (gravity && disk_scale > 0) {
        d3scene::PreparedMesh disk;
        disk.asset = d3scene::assets().publishMesh(6,d3scene::billboardRing(0,255,0,32));
        disk.has_owner = true; disk.owner = owner.handle; disk.room = source.roomnum;
        disk.alignment = d3scene::MeshAlignment::Camera; disk.world.position = value(source.pos);
        const float size = disk_scale*source.ctype.blast_info.max_size;
        for (int i = 0; i < 32; ++i) {
            disk.vertices.push_back({FixCos(i*(65536/32))*size,FixSin(i*(65536/32))*size,0});
            disk.vertices.push_back({0,0,0});
        }
        disk.surface = ring.surface; disk.surface.bitmap = -1; disk.surface.texture_alpha = false;
        disk.surface.alpha = .9f; disk.surface.base_color = {.3f,.5f,1};
        frame.meshes.push_back(std::move(disk));
    } else if (!gravity && disk_scale > 0 && disk_bitmap > BAD_BITMAP_HANDLE && disk_bitmap < MAX_BITMAPS && GameBitmaps[disk_bitmap].used) {
        const int width = bm_w(disk_bitmap,0), height = bm_h(disk_bitmap,0);
        if (width > 0 && height > 0) {
            d3scene::PreparedMesh disk;
            disk.asset = quad; disk.has_owner = true; disk.owner = owner.handle; disk.room = source.roomnum;
            disk.alignment = d3scene::MeshAlignment::Camera;
            matrix spin; vm_AnglesToMatrix(&spin,0,0,static_cast<angle>(d3scene::visualAngle(frame.simulation_time,life < .1f ? 20.f : 550.f,0)));
            vm_TransposeMatrix(&spin); disk.world = basis(spin); disk.world.position = value(source.pos);
            const float size = disk_scale*source.ctype.blast_info.max_size;
            disk.world.right = value(spin.rvec*size); disk.world.up = value(spin.uvec*(size*height/width));
            disk.surface = ring.surface; disk.surface.bitmap = disk_bitmap;
            disk.surface.alpha = alphaByte(disk_scale); disk.surface.texture_wrap = false;
            frame.meshes.push_back(std::move(disk));
        }
    }
    frame.meshes.push_back(std::move(ring));
    return true;
}
bool prepareAxisEffect(d3scene::FrameData& frame,const vis_effect& effect,const d3scene::MeshAssetRef& quad) {
    const bool trail = effect.id == BILLBOARD_SMOKETRAIL_INDEX;
    const bool beam = effect.id == THICK_LIGHTNING_INDEX;
    const float age = (std::max)(0.f,static_cast<float>(frame.simulation_time)-effect.creation_time);
    const float normalized = effect.lifetime > 0 ? (std::min)(.99999f,age/effect.lifetime) : 0;
    const float remaining = effect.lifetime > 0 ? (std::max)(0.f,effect.lifeleft/effect.lifetime) : 0;
    float width = effect.billboard_info.width;
    float alpha = effect.flags & VF_EXPAND ? remaining : 1.f;
    if (beam && !(effect.flags & VF_EXPAND)) alpha = .7f;
    if (trail) {
        width *= remaining;
        if (remaining > .8f) width *= 1-(remaining-.8f)/.2f;
        alpha = remaining*.7f;
    }
    if (!(width > 0) || !std::isfinite(width)) return false;
    d3scene::PreparedMesh rod;
    rod.asset = quad; rod.room = effect.roomnum; rod.alignment = d3scene::MeshAlignment::Axis;
    rod.world = d3scene::axisBillboardPlacement(value(effect.pos),value(effect.end_pos),width);
    auto& surface = rod.surface;
    surface.unlit = true; surface.two_sided = true;
    surface.texture_alpha = true; surface.texture_wrap = false;
    surface.blend = d3scene::BlendMode::Alpha;
    surface.soft_depth_range = Render_soft_vis_effects && !(effect.flags & VF_NO_SOFT_PARTICLES) ? 2.f : 0.f;
    int bitmap = effect.custom_handle;
    if (beam || trail || effect.billboard_info.texture) {
        const int texture = effect.custom_handle;
        if (texture < 0 || texture >= MAX_TEXTURES || !GameTextures[texture].used) return false;
        const auto& material = GameTextures[texture];
        if (beam) bitmap = GetTextureBitmapAtTime(texture,0,static_cast<float>(frame.simulation_time),true);
        else if (material.flags & TF_ANIMATED) {
            const int handle = material.bm_handle;
            if (handle < 0 || handle >= MAX_VCLIPS || !GameVClips[handle].used) return false;
            PageInVClip(handle);
            const auto& animation = GameVClips[handle];
            if (!animation.frames || animation.num_frames <= 0) return false;
            bitmap = animation.frames[d3scene::animationFrames(animation.num_frames,normalized,false).frame0];
        } else bitmap = GetTextureBitmapAtTime(texture,0,static_cast<float>(frame.simulation_time));
        if (material.flags & TF_SATURATE) surface.blend = d3scene::BlendMode::Additive;
        if (!trail && !beam) {
            alpha *= material.alpha;
            surface.u_offset = material.slide_u ? std::fmod(static_cast<float>(frame.simulation_time)/material.slide_u,1.f) : 0;
            surface.v_offset = material.slide_v ? std::fmod(static_cast<float>(frame.simulation_time)/material.slide_v,1.f) : 0;
            surface.texture_wrap = surface.u_offset != 0 || surface.v_offset != 0;
        }
    }
    if (bitmap <= BAD_BITMAP_HANDLE || bitmap >= MAX_BITMAPS || !GameBitmaps[bitmap].used) return false;
    if (trail || beam) {
        const auto color = GR_16_TO_COLOR(effect.lighting_color);
        surface.color_modulation = {GR_COLOR_RED(color)/255.f,GR_COLOR_GREEN(color)/255.f,GR_COLOR_BLUE(color)/255.f};
    }
    if (beam) {
        auto delta = effect.end_pos-effect.pos;
        const float length = vm_GetMagnitude(&delta);
        if (length < 1 || !std::isfinite(effect.velocity.x) || effect.velocity.x <= 0) return false;
        surface.texture_wrap = true; surface.blend = d3scene::BlendMode::Additive;
        surface.texture_scale.y = effect.velocity.z*(effect.billboard_info.texture ? length/width : 1.f);
        surface.v_offset = effect.velocity.y ? std::fmod(static_cast<float>(frame.simulation_time)/effect.velocity.y,1.f) : 0;
        // Legacy repeats an identical additive quad ceil(velocity.x) times.
        // Publish its total radiance once instead of replaying geometry.
        const float copies = std::ceil(effect.velocity.x);
        surface.color_modulation.x *= copies; surface.color_modulation.y *= copies; surface.color_modulation.z *= copies;
    }
    surface.bitmap = bitmap;
    surface.alpha = trail ? (std::min)(1.f,alpha) : alphaByte(alpha);
    frame.meshes.push_back(std::move(rod));
    return true;
}
}
// Object effects share owned geometry with model attachments. Publication is
// after object eligibility; per-view exclusion is applied by the scene query.
void PrepareGameObjectEffects(d3scene::FrameData& frame) {
    const auto quad = d3scene::assets().quadMesh();
    for (const auto& owner : frame.objects) {
        auto* source = ObjGet(owner.handle);
        if (!source || !owner.visible) continue;
        if (source->render_type == RT_FIREBALL && source->type == OBJ_FIREBALL &&
            (source->id == BLAST_RING_INDEX || source->id == GRAVITY_FIELD_INDEX)) {
            prepareObjectRing(frame,owner,*source,quad);
            continue;
        }
        if (source->render_type == RT_WEAPON && source->type == OBJ_WEAPON) {
            const auto& weapon = Weapons[source->id];
            if (!(weapon.flags & (WF_IMAGE_BITMAP | WF_IMAGE_VCLIP)) ||
                (weapon.flags & (WF_INVISIBLE | WF_ELECTRICAL)) || WeaponIsCloseScreenEffectObject(source)) continue;
            if (weapon.flags & WF_IMAGE_VCLIP) {
                const int clip = weapon.fire_image_handle;
                if (clip < 0 || clip >= MAX_VCLIPS || !GameVClips[clip].used) continue;
                PageInVClip(clip);
                if (!GameVClips[clip].frames || GameVClips[clip].num_frames <= 0 || !(GameVClips[clip].frame_time > 0)) continue;
            }
            const int bitmap = GetWeaponFireImageAtTime(source->id,0,static_cast<float>(frame.simulation_time));
            if (bitmap <= BAD_BITMAP_HANDLE || bitmap >= MAX_BITMAPS || !GameBitmaps[bitmap].used) continue;
            const int width = bm_w(bitmap,0), height = bm_h(bitmap,0);
            if (width <= 0 || height <= 0 || !(source->size > 0)) continue;
            const int rotation = weapon.flags & WF_NO_ROTATE ? static_cast<int>(source-Objects)*1000%65536 :
                d3scene::weaponSpriteAngle(frame.simulation_time,weapon.phys_info.rotvel.z);
            matrix spin; vm_AnglesToMatrix(&spin,0,0,static_cast<angle>(rotation));
            d3scene::PreparedMesh sprite;
            sprite.asset = quad; sprite.has_owner = true; sprite.owner = owner.handle; sprite.room = source->roomnum;
            if (weapon.flags & WF_PLANAR) {
                vector normal = source->mtype.phys_info.velocity;
                if (vm_GetMagnitude(&normal) < .0001f) continue;
                matrix plane; vm_VectorToMatrix(&plane,&normal,nullptr,nullptr);
                sprite.world = d3scene::compose(basis(plane),basis(spin));
            } else {
                vm_TransposeMatrix(&spin); sprite.world = basis(spin); sprite.alignment = d3scene::MeshAlignment::Camera;
            }
            sprite.world.position = value(source->pos);
            auto& r = sprite.world.right; auto& u = sprite.world.up;
            const float size = source->size, vertical = size*height/width;
            r = {r.x*size,r.y*size,r.z*size}; u = {u.x*vertical,u.y*vertical,u.z*vertical};
            auto& surface = sprite.surface;
            surface.bitmap = bitmap; surface.unlit = true; surface.two_sided = true;
            surface.texture_alpha = true; surface.texture_wrap = false;
            surface.alpha = alphaByte(weapon.alpha);
            surface.blend = weapon.flags & WF_SATURATE ? d3scene::BlendMode::Additive : d3scene::BlendMode::Alpha;
            frame.meshes.push_back(std::move(sprite));
            continue;
        }
        if (source->render_type != RT_FIREBALL || source->type != OBJ_FIREBALL) continue;
        if (source->id >= NUM_FIREBALLS || source->id == SMOLDERING_INDEX) continue;
        const auto& definition = Fireballs[source->id];
        const float age = (std::max)(0.f,static_cast<float>(frame.simulation_time)-source->creation_time);
        const float normalized = source->lifetime > 0 ? (std::min)(.99999f,age/source->lifetime) : .99999f;
        float size = source->size;
        int bitmap = -1;
        const auto clip = [&](int handle) {
            if (handle < 0 || handle >= MAX_VCLIPS || !GameVClips[handle].used) return;
            PageInVClip(handle);
            const auto& animation = GameVClips[handle];
            if (animation.frames && animation.num_frames > 0)
                bitmap = animation.frames[d3scene::animationFrames(animation.num_frames,normalized,false).frame0];
        };
        if (source->id == SMOKE_TRAIL_INDEX || source->id == CUSTOM_EXPLOSION_INDEX) {
            const int texture = source->ctype.blast_info.bm_handle;
            if (texture < 0 || texture >= MAX_TEXTURES || !GameTextures[texture].used) continue;
            const auto& material = GameTextures[texture];
            if (source->id == SMOKE_TRAIL_INDEX) {
                if (material.flags & TF_ANIMATED) clip(material.bm_handle);
                else bitmap = material.bm_handle;
            } else {
                bitmap = GetTextureBitmapAtTime(texture,0,static_cast<float>(frame.simulation_time));
                if (!(material.flags & TF_ANIMATED))
                    size *= normalized < .5f ? (std::min)(1.f,normalized/.2f) : 2*(1-normalized);
            }
        } else if (definition.type == FT_SPARK) { bitmap = definition.bm_handle; size *= 1-normalized; }
        else clip(definition.bm_handle);
        const bool smoke = definition.type == FT_SMOKE;
        if (smoke && normalized > .3f) size *= 1+(normalized-.3f)/.7f*2.3f;
        size = (std::min)(size,static_cast<float>(MAX_FIREBALL_SIZE));
        if (!(size > 0) || !std::isfinite(size) || bitmap <= BAD_BITMAP_HANDLE ||
            bitmap >= MAX_BITMAPS || !GameBitmaps[bitmap].used) continue;
        const int width = bm_w(bitmap,0), height = bm_h(bitmap,0);
        if (width <= 0 || height <= 0) continue;
        d3scene::PreparedMesh sprite;
        sprite.asset = quad; sprite.has_owner = true; sprite.owner = owner.handle;
        sprite.room = source->roomnum; sprite.alignment = d3scene::MeshAlignment::Camera;
        matrix spin;
        vm_AnglesToMatrix(&spin,0,0,static_cast<angle>(d3scene::visualAngle(frame.simulation_time,
            definition.type == FT_BILLOW ? 160.f : 30.f,static_cast<int>(source-Objects)*5000)));
        vm_TransposeMatrix(&spin); sprite.world = basis(spin); sprite.world.position = value(source->pos);
        sprite.world.right = value(spin.rvec*size); sprite.world.up = value(spin.uvec*(size*height/width));
        auto& surface = sprite.surface;
        surface.bitmap = bitmap; surface.unlit = true; surface.two_sided = true;
        surface.texture_alpha = true; surface.texture_wrap = false;
        surface.blend = smoke || source->id == CUSTOM_EXPLOSION_INDEX ? d3scene::BlendMode::Alpha : d3scene::BlendMode::Additive;
        surface.alpha = alphaByte((normalized > .5f ? 2*(1-normalized) : 1)*(smoke ? SMOKE_ALPHA : FIREBALL_ALPHA));
        surface.soft_depth_range = Render_soft_vis_effects ? 2.f : 0.f;
        surface.effect_depth_offset = Render_soft_vis_effects && smoke ? 0.f : -2.f;
        frame.meshes.push_back(std::move(sprite));
    }
}
void PrepareGameEffects(d3scene::FrameData& frame) {
    if (!VisEffects) return;
    const auto quad = d3scene::assets().quadMesh();
    unsigned prepared = 0, pending = 0;
    for (int slot = 0; slot <= Highest_vis_effect_index; ++slot) {
        const auto& effect = VisEffects[slot];
        if (effect.type == VIS_NONE || (effect.flags & VF_DEAD)) continue;
        if (effect.type == VIS_FIREBALL && effect.id == BLAST_RING_INDEX &&
            !(effect.flags & (VF_WINDSHIELD_EFFECT | VF_CLOSE_SCREEN_EFFECT))) {
            if (prepareVisRing(frame,effect)) ++prepared;
            continue;
        }
        if (effect.type == VIS_FIREBALL && (effect.id == AXIS_BILLBOARD_INDEX || effect.id == BILLBOARD_SMOKETRAIL_INDEX || effect.id == THICK_LIGHTNING_INDEX) &&
            !(effect.flags & (VF_WINDSHIELD_EFFECT | VF_CLOSE_SCREEN_EFFECT))) {
            if (prepareAxisEffect(frame,effect,quad)) ++prepared;
            continue;
        }
        if (effect.type != VIS_FIREBALL || special(effect.id) || effect.id >= NUM_FIREBALLS ||
            (effect.flags & (VF_WINDSHIELD_EFFECT | VF_CLOSE_SCREEN_EFFECT))) { ++pending; continue; }
        const auto& definition = Fireballs[effect.id];
        const bool attached = (effect.flags & VF_ATTACHED) != 0;
        const float age = (std::max)(0.f,static_cast<float>(frame.simulation_time)-effect.creation_time);
        float normalized = effect.lifetime > 0 ? age/effect.lifetime : 0;
        if (attached) normalized = age-std::floor(age);
        normalized = (std::min)(.99999f,normalized);
        float size = effect.size;
        if (Katmai && (effect.id == BIG_EXPLOSION_INDEX || effect.id == MED_EXPLOSION_INDEX ||
            effect.id == MED_EXPLOSION_INDEX2 || effect.id == MED_EXPLOSION_INDEX3)) size *= 1.8f;
        int rotation;
        if (definition.type == FT_BILLOW) rotation = d3scene::visualAngle(frame.simulation_time,160,slot*5000);
        else if (attached) rotation = 0;
        else if (effect.id == RUBBLE1_INDEX || effect.id == RUBBLE2_INDEX) rotation = d3scene::visualAngle(frame.simulation_time,860,slot*5000);
        else if (effect.id == SUN_CORONA_INDEX) {
            rotation = d3scene::visualAngle(frame.simulation_time,500,slot*5000);
            uint32_t noise = static_cast<uint32_t>(static_cast<int64_t>(std::floor(frame.simulation_time*60)))*1664525u+uint32_t(slot)*1013904223u;
            noise ^= noise >> 16; size *= 1+(noise%10)/100.f;
        } else rotation = (slot*5000)%65536;
        if (effect.flags & VF_EXPAND) size = effect.size*.5f*(1+normalized);
        int first_bitmap = -1, second_bitmap = -1;
        float second_weight = 0;
        const auto clip = [&](int handle) {
            if (handle < 0 || handle >= MAX_VCLIPS || !GameVClips[handle].used) return;
            PageInVClip(handle);
            const auto& animation = GameVClips[handle];
            if (!animation.frames || animation.num_frames <= 0) return;
            const auto selection = d3scene::animationFrames(animation.num_frames,normalized,attached);
            first_bitmap = animation.frames[selection.frame0];
            if (selection.has_frame1) { second_bitmap = animation.frames[selection.frame1]; second_weight = selection.frame1_weight; }
        };
        const bool custom = effect.id == SMOKE_TRAIL_INDEX || effect.id == CUSTOM_EXPLOSION_INDEX || effect.id == PARTICLE_INDEX;
        const texture* material = nullptr;
        if (custom) {
            if (effect.custom_handle < 0 || effect.custom_handle >= MAX_TEXTURES || !GameTextures[effect.custom_handle].used) continue;
            material = &GameTextures[effect.custom_handle];
            if (material->flags & TF_ANIMATED) clip(material->bm_handle);
            else first_bitmap = GetTextureBitmapAtTime(effect.custom_handle,0,static_cast<float>(frame.simulation_time),false);
        } else if (effect.id == SPRAY_INDEX) clip(effect.custom_handle);
        else if (definition.type == FT_SPARK) { first_bitmap = definition.bm_handle; size *= 1-normalized; }
        else if (effect.id == SUN_CORONA_INDEX || effect.id == MUZZLE_FLASH_INDEX || effect.id == RUBBLE1_INDEX || effect.id == RUBBLE2_INDEX)
            first_bitmap = definition.bm_handle;
        else clip(definition.bm_handle);
        if (definition.type == FT_SMOKE && normalized > .3f) {
            const float expansion = 1+(normalized-.3f)/.7f*2.3f;
            if (effect.flags & VF_REVERSE) size /= expansion; else size *= expansion;
        }
        size = (std::min)(size,static_cast<float>(MAX_FIREBALL_SIZE));
        if (effect.id != BIG_EXPLOSION_INDEX && effect.id != BLUE_EXPLOSION_INDEX) size = (std::min)(size,static_cast<float>(MAX_FIREBALL_SIZE/2));
        if (!(size > 0) || !std::isfinite(size)) continue;
        const float fade = normalized > .5f ? 2*(1-normalized) : 1;
        float alpha;
        if (custom) alpha = fade*material->alpha;
        else if (definition.type == FT_SMOKE) alpha = fade*SMOKE_ALPHA;
        else if (effect.id == RUBBLE1_INDEX || effect.id == RUBBLE2_INDEX) alpha = 1;
        else if (effect.id == MUZZLE_FLASH_INDEX) alpha = 128/255.f;
        else alpha = (attached ? 1 : fade)*FIREBALL_ALPHA;
        d3scene::PreparedMesh sprite;
        sprite.asset = quad; sprite.room = effect.roomnum;
        sprite.surface.unlit = true; sprite.surface.two_sided = true;
        sprite.surface.texture_alpha = true; sprite.surface.texture_wrap = false;
        sprite.surface.soft_depth_range = Render_soft_vis_effects && !(effect.flags & VF_NO_SOFT_PARTICLES) ? 2.f : 0.f;
        if (custom) sprite.surface.blend = material->flags & TF_SATURATE ? d3scene::BlendMode::Additive : d3scene::BlendMode::Alpha;
        else if (effect.id == BLACK_SMOKE_INDEX) sprite.surface.blend = d3scene::BlendMode::Multiply;
        else if ((definition.type == FT_SMOKE && effect.id != MED_SMOKE_INDEX) || effect.id == RUBBLE1_INDEX || effect.id == RUBBLE2_INDEX)
            sprite.surface.blend = d3scene::BlendMode::Alpha;
        else sprite.surface.blend = d3scene::BlendMode::Additive;
        if (effect.id == RUBBLE1_INDEX || effect.id == RUBBLE2_INDEX || effect.id == GRAY_SPARK_INDEX) {
            const auto color = GR_16_TO_COLOR(effect.lighting_color);
            sprite.surface.color_modulation = {GR_COLOR_RED(color)/255.f,GR_COLOR_GREEN(color)/255.f,GR_COLOR_BLUE(color)/255.f};
        }
        matrix spin; vm_AnglesToMatrix(&spin,0,0,static_cast<angle>(rotation));
        if (effect.flags & VF_PLANAR) {
            vector normal = effect.end_pos;
            if (vm_GetMagnitude(&normal) < .0001f) continue;
            matrix plane; vm_VectorToMatrix(&plane,&normal,nullptr,nullptr);
            sprite.world = d3scene::compose(basis(plane),basis(spin));
        } else {
            vm_TransposeMatrix(&spin); sprite.world = basis(spin); sprite.alignment = d3scene::MeshAlignment::Camera;
        }
        sprite.world.position = value(effect.pos);
        const auto publish = [&](int bitmap,float weight) {
            if (bitmap <= BAD_BITMAP_HANDLE || bitmap >= MAX_BITMAPS || !GameBitmaps[bitmap].used || weight <= .001f) return;
            const int width = bm_w(bitmap,0), height = bm_h(bitmap,0);
            if (width <= 0 || height <= 0) return;
            auto instance = sprite;
            const float vertical = size*height/width;
            instance.world.right = {sprite.world.right.x*size,sprite.world.right.y*size,sprite.world.right.z*size};
            instance.world.up = {sprite.world.up.x*vertical,sprite.world.up.y*vertical,sprite.world.up.z*vertical};
            instance.surface.bitmap = bitmap; instance.surface.alpha = alphaByte(alpha)*weight;
            frame.meshes.push_back(std::move(instance)); ++prepared;
        };
        const bool blending = second_bitmap > BAD_BITMAP_HANDLE && second_weight > .001f;
        publish(first_bitmap,blending ? 1-second_weight : 1);
        if (blending) publish(second_bitmap,second_weight);
    }
    if (SceneGeometryInspection() && FindArg("-scene-axis-probe") && Viewer_object) {
        // A local fixture, never inserted into VisEffects or the simulation.
        // Exercise the same material/placement compiler as real trail content.
        vis_effect probe{};
        probe.type = VIS_FIREBALL; probe.id = AXIS_BILLBOARD_INDEX;
        probe.roomnum = Viewer_object->roomnum;
        probe.lifetime = 1; probe.lifeleft = .65f;
        probe.creation_time = static_cast<float>(frame.simulation_time)-.35f;
        probe.custom_handle = -1;
        for (const auto& room : frame.rooms) for (const auto& surface : room.surfaces)
            if (probe.custom_handle < 0 && surface.texture >= 0 && surface.bitmap > BAD_BITMAP_HANDLE)
                probe.custom_handle = surface.texture;
        if (AutomatedCaptureFrameIndex() == 0)
            AutomatedCaptureLog("scene axis fixture texture=%d",probe.custom_handle);
        probe.billboard_info.texture = 1; probe.billboard_info.width = 1;
        const auto center = Viewer_object->pos+Viewer_object->orient.fvec*12;
        probe.pos = center-Viewer_object->orient.rvec*4+Viewer_object->orient.uvec*2;
        probe.end_pos = center+Viewer_object->orient.rvec*4+Viewer_object->orient.uvec*2;
        if (!prepareAxisEffect(frame,probe,quad)) Error("Axis fixture could not publish its source texture");
        probe.id = BILLBOARD_SMOKETRAIL_INDEX;
        probe.lighting_color = GR_RGB16(180,220,255);
        probe.pos -= Viewer_object->orient.uvec*4;
        probe.end_pos -= Viewer_object->orient.uvec*4;
        if (!prepareAxisEffect(frame,probe,quad)) Error("Trail fixture could not publish its source texture");
        probe.id = THICK_LIGHTNING_INDEX;
        probe.velocity = {2.2f,2.f,.5f};
        probe.pos += Viewer_object->orient.uvec*2;
        probe.end_pos += Viewer_object->orient.uvec*2;
        if (!prepareAxisEffect(frame,probe,quad)) Error("Beam fixture could not publish its source texture");
        prepared += 3;
    }
    if (SceneGeometryInspection() && FindArg("-scene-depth-policy-probe") && Viewer_object) {
        auto pair = d3scene::unitBillboardQuad();
        const auto far = pair;
        for (auto position : far.vertices) { position.z = 1; pair.vertices.push_back(position); }
        auto face = far.polygons[0];
        for (auto& corner : face.corners) corner.vertex += 4;
        pair.polygons.push_back(face);
        pair.vertex_colors = {{0,0,1},{0,0,1},{0,0,1},{0,0,1},{1,0,0},{1,0,0},{1,0,0},{1,0,0}};
        const auto pair_asset = d3scene::assets().publishMesh(7,std::move(pair));
        for (int column = 0; column < 4; ++column) {
            d3scene::PreparedMesh panel;
            panel.asset = quad; panel.room = Viewer_object->roomnum; panel.world = basis(Viewer_object->orient);
            panel.world.right = value(Viewer_object->orient.rvec*.65f);
            panel.world.up = value(Viewer_object->orient.uvec*.65f);
            const auto center = Viewer_object->pos+Viewer_object->orient.fvec*6+Viewer_object->orient.rvec*((column-1.5f)*1.7f);
            panel.world.position = value(center+Viewer_object->orient.fvec*2);
            panel.surface.unlit = true; panel.surface.two_sided = true; panel.surface.base_color = {0,0,0};
            frame.meshes.push_back(panel);
            if (column >= 2) {
                panel.world.position = value(center+Viewer_object->orient.fvec*.5f);
                panel.surface.base_color = {0,1,0}; frame.meshes.push_back(panel);
            }
            panel.asset = pair_asset; panel.world.position = value(center);
            panel.surface.base_color = {1,1,1}; panel.surface.alpha = .5f;
            panel.surface.blend = d3scene::BlendMode::Alpha;
            panel.surface.depth = column == 0 ? d3scene::DepthMode::ReadWrite :
                column == 2 ? d3scene::DepthMode::Disabled : d3scene::DepthMode::ReadOnly;
            frame.meshes.push_back(panel);
        }
        if (AutomatedCapturePending() || AutomatedCaptureFrameIndex() == 0)
            AutomatedCaptureLog("scene depth fixture columns=write,read,disabled,read_occluded expected=blue,purple,pink,cyan");
    }
    if (SceneGeometryInspection() && FindArg("-scene-ring-probe") && Viewer_object) {
        vis_effect probe{};
        probe.type = VIS_FIREBALL; probe.id = BLAST_RING_INDEX; probe.roomnum = Viewer_object->roomnum;
        probe.lifetime = 1; probe.lifeleft = .65f; probe.size = 7;
        probe.custom_handle = Fireballs[BLUE_BLAST_RING_INDEX].bm_handle;
        const auto center = Viewer_object->pos+Viewer_object->orient.fvec*12;
        probe.pos = center-Viewer_object->orient.rvec*3;
        if (!prepareVisRing(frame,probe)) Error("Facing ring fixture could not publish");
        probe.pos = center+Viewer_object->orient.rvec*3;
        probe.flags = VF_PLANAR | VF_REVERSE;
        probe.end_pos = -Viewer_object->orient.fvec+Viewer_object->orient.rvec*.6f;
        vm_NormalizeVector(&probe.end_pos);
        if (!prepareVisRing(frame,probe)) Error("Planar reverse ring fixture could not publish");
        object blast{};
        blast.pos = center-Viewer_object->orient.uvec*3;
        blast.orient = Viewer_object->orient;
        blast.orient.fvec = Viewer_object->orient.uvec;
        blast.orient.uvec = -Viewer_object->orient.fvec;
        blast.roomnum = probe.roomnum; blast.lifetime = 1; blast.lifeleft = .85f;
        blast.ctype.blast_info.max_size = 3; blast.ctype.blast_info.bm_handle = probe.custom_handle;
        const auto first_blast_mesh = frame.meshes.size();
        if (frame.objects.empty() || !prepareObjectRing(frame,frame.objects.front(),blast,quad))
            Error("Object blast ring fixture could not publish");
        // Fixture ownership must not inherit camera exclusion from the first
        // game object. The synthetic effect is never inserted into simulation.
        for (size_t i = first_blast_mesh; i < frame.meshes.size(); ++i) frame.meshes[i].has_owner = false;
        blast.id = GRAVITY_FIELD_INDEX; blast.pos = center+Viewer_object->orient.uvec*3;
        blast.lifeleft = .4f; blast.ctype.blast_info.max_size = 3;
        const auto first_gravity_mesh = frame.meshes.size();
        if (!prepareObjectRing(frame,frame.objects.front(),blast,quad)) Error("Gravity ring fixture could not publish");
        for (size_t i = first_gravity_mesh; i < frame.meshes.size(); ++i) frame.meshes[i].has_owner = false;
        prepared += 6;
        if (AutomatedCapturePending() || AutomatedCaptureFrameIndex() == 0)
            AutomatedCaptureLog("scene ring fixture facing=1 planar_reverse=1 object=1 gravity=1 prepared_positions=224");
    }
    if (AutomatedCapturePending() || AutomatedCaptureFrameIndex() == 0)
        AutomatedCaptureLog("scene effects sprites=%u pending=%u",prepared,pending);
}
