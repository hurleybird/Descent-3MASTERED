// Descent 3MASTERED - GPL-3.0-or-later
#include "SceneExtraction.h"
#include "../renderer/SceneLighting.h"
#include "object.h"
#include "object_lighting.h"
#include "objinfo.h"
#include "weapon.h"
#include "fireball.h"
#include "player.h"
#include "game.h"
#include "config.h"
#include "renderer.h"
#include "lighting.h"
#include "viseffect.h"
#include <cmath>
#include <algorithm>

namespace {
d3scene::Vec3 value(const vector& v) { return {v.x,v.y,v.z}; }
float envelope(float lifetime, float remaining) {
    if (!(lifetime > 0)) return 0;
    const float age = (lifetime-remaining)/lifetime;
    return std::max(0.f,1.f-std::abs(2.f*age-1.f));
}
void emit(d3scene::FrameData& frame, d3scene::LightId id, int room, const vector& position,
    float radius, float r, float g, float b, const vector* direction = nullptr, float cone = 0, bool headlight = false) {
    if (!(radius > 0)) return;
    d3scene::Light light;
    light.id = id; light.room = room; light.position = value(position);
    light.radius = radius; light.color = {r,g,b};
    light.headlight = headlight; light.directional = direction != nullptr;
    light.specular_strength = headlight ? Render_per_pixel_headlight_specular_strength : Render_per_pixel_dynamic_specular_strength;
    if (direction) {
        const float length = std::sqrt(direction->x*direction->x+direction->y*direction->y+direction->z*direction->z);
        if (!(length > 0)) return;
        light.direction = {direction->x/length,direction->y/length,direction->z/length};
        light.cone = std::max(-1.f,std::min(cone,0.999999f));
    }
    if (headlight) {
        light.radius *= .67f; light.falloff = 1.5f;
        light.color = {r*1.5f,g*1.5f,b*1.5f};
    }
    frame.lights.push_back(light);
}
}

void PrepareGameSceneLights(d3scene::FrameData& frame) {
    frame.lights.clear();
    const float time = static_cast<float>(frame.simulation_time);
    const int flare = FindWeaponName("Yellow flare"), greenflare = FindWeaponName("GreenFlare");
    for (int i = 0; i <= Highest_object_index; ++i) {
        auto& obj = Objects[i];
        if (obj.type == OBJ_NONE || (obj.flags & OF_DEAD) || obj.render_type == RT_NONE) continue;
        const float sign = obj.effect_info && (obj.effect_info->type_flags & EF_NEGATIVE_LIGHT) ? -1.f : 1.f;
        const auto identity = [&](uint32_t channel) {
            return d3scene::LightId{d3scene::LightSource::Object,static_cast<uint32_t>(obj.handle),channel};
        };
        if (obj.type == OBJ_FIREBALL) {
            if (Detail_settings.Dynamic_lighting && (obj.flags & OF_USES_LIFELEFT) && Fireballs[obj.id].type == FT_EXPLOSION) {
                const float amount = envelope(obj.lifetime,obj.lifeleft);
                if (amount > .05f) emit(frame,identity(0),obj.roomnum,obj.pos,40*amount,sign,.5f*sign,0);
            }
            continue;
        }
        if (obj.type == OBJ_PLAYER) {
            const auto& player = Players[obj.id];
            if (player.flags & PLAYER_FLAGS_HEADLIGHT)
                emit(frame,identity(0),obj.roomnum,obj.pos,PLAYER_HEADLIGHT_DISTANCE,sign,sign,sign,
                    &obj.orient.fvec,PLAYER_HEADLIGHT_DOT,true);
            if (Detail_settings.Dynamic_lighting) {
                if (player.light_dist > .5f)
                    emit(frame,identity(1),obj.roomnum,obj.pos,player.light_dist,sign*player.r,sign*player.g,sign*player.b);
                for (int ball = 0; ball < player.num_balls; ++ball) {
                    vector position;
                    PlayerGetBallPosition(&position,obj.id,ball);
                    emit(frame,identity(2+ball),obj.roomnum,position,25,sign*player.ball_r[ball],sign*player.ball_g[ball],sign*player.ball_b[ball]);
                }
            }
            continue;
        }
        const light_info* info = nullptr;
        if (obj.type == OBJ_WEAPON) {
            if ((!Detail_settings.Dynamic_lighting && obj.id != flare && obj.id != greenflare) || !obj.ctype.laser_info.casts_light) continue;
            info = &Weapons[obj.id].lighting_info;
        } else if (obj.type == OBJ_POWERUP || obj.type == OBJ_ROBOT || obj.type == OBJ_CLUTTER || obj.type == OBJ_BUILDING) {
            if (!Detail_settings.Dynamic_lighting && (obj.type == OBJ_ROBOT || obj.type == OBJ_POWERUP)) continue;
            info = ObjGetLightInfo(&obj);
        }
        if (!info || !(info->light_distance > 0)) continue;
        float radius = info->light_distance * (Katmai ? 1.5f : 1.f);
        float scalar = 1, r = info->red_light1, g = info->green_light1, b = info->blue_light1;
        if (info->flags & OLF_FLICKER_SLIGHTLY) {
            const int distance = info->flicker_distance;
            if (distance <= 0) continue;
            radius += static_cast<int>(d3scene::visualNoise(frame.simulation_time,obj.handle,1) % distance) - distance/2;
        }
        if ((info->flags & (OLF_PULSE | OLF_PULSE_TO_SECOND)) && info->time_interval > 0) {
            const float phase = std::fmod(time,2*info->time_interval)/info->time_interval;
            const float triangle = phase > 1 ? 2-phase : phase;
            if (info->flags & OLF_PULSE) scalar = info->flicker_distance + triangle*(1-info->flicker_distance);
            if (info->flags & OLF_PULSE_TO_SECOND) {
                r = (1-triangle)*info->red_light1 + triangle*info->red_light2;
                g = (1-triangle)*info->green_light1 + triangle*info->green_light2;
                b = (1-triangle)*info->blue_light1 + triangle*info->blue_light2;
            }
        }
        if ((info->flags & OLF_FLICKERING) && (d3scene::visualNoise(frame.simulation_time,obj.handle,2) & 1u)) continue;
        if (info->flags & OLF_TIMEBITS) {
            if (!(info->time_interval > 0)) continue;
            const int bit = static_cast<int>(std::fmod(time,info->time_interval)*8/info->time_interval);
            if (!(info->timebits & (1 << std::max(0,std::min(bit,7))))) continue;
        }
        emit(frame,identity(0),obj.roomnum,obj.pos,radius*std::max(0.f,scalar),r*sign,g*sign,b*sign,
            info->flags & OLF_DIRECTIONAL ? &obj.orient.fvec : nullptr,info->directional_dot);
    }
    if (Detail_settings.Dynamic_lighting && VisEffects) for (int i = 0; i <= Highest_vis_effect_index; ++i) {
        const auto& effect = VisEffects[i];
        if (effect.type == VIS_NONE || (effect.flags & VF_DEAD) || !(effect.flags & VF_USES_LIFELEFT) ||
            !effect.lighting_color || !(effect.lighting_color & OPAQUE_FLAG)) continue;
        const float amount = envelope(effect.lifetime,effect.lifeleft);
        if (amount <= .05f) continue;
        const auto color = GR_16_TO_COLOR(effect.lighting_color);
        emit(frame,{d3scene::LightSource::Effect,VisEffectGeneration(i),0},effect.roomnum,effect.pos,effect.size*amount*3,
            GR_COLOR_RED(color)/255.f,GR_COLOR_GREEN(color)/255.f,GR_COLOR_BLUE(color)/255.f);
    }
    // Camera-mounted headlight retains its visual shake without changing the
    // ship's physical pose, other lights, or geometry seen by another view.
    for (auto& light : frame.lights) if (light.headlight)
        for (const auto& effect : frame.camera_perturbations)
            if (light.id.source == d3scene::LightSource::Object && light.id.owner == effect.owner)
                light.direction = d3scene::transformDirection(effect.rotation,light.direction);
    d3scene::prepareLightInfluences(frame);
}
