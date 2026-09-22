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
}
void PrepareGameEffects(d3scene::FrameData& frame) {
    if (!VisEffects) return;
    const auto quad = d3scene::assets().publishMesh(2,d3scene::unitBillboardQuad());
    unsigned prepared = 0, pending = 0;
    for (int slot = 0; slot <= Highest_vis_effect_index; ++slot) {
        const auto& effect = VisEffects[slot];
        if (effect.type == VIS_NONE || (effect.flags & VF_DEAD)) continue;
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
    if (AutomatedCapturePending() || AutomatedCaptureFrameIndex() == 0)
        AutomatedCaptureLog("scene effects sprites=%u pending=%u",prepared,pending);
}
