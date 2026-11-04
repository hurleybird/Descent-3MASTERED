/*
* Descent 3
* Copyright (C) 2024 Parallax Software
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "gauges.h"
#include "gamefont.h"
#include "hud.h"
#include "game.h"
#include "config.h"
#include "renderer.h"
#include "render.h"
#include "player.h"
#include "polymodel.h"
#include "gametexture.h"
#include "bitmap.h"
#include "vclip.h"
#include "Inventory.h"
#include "cockpit.h"
#include "player.h"
#include "ship.h"
#include "3d.h"
#include "weapon.h"
#include "stringtable.h"
#include "SceneExtraction.h"
#include "SceneCanvas.h"
#include "../renderer/SceneView.h"
#include "../model/ModelPose.h"
#include "../renderer/SceneGeometry.h"
#include "gameloop.h"
#include <array>
#include <cmath>
#include <stdexcept>

#include <algorithm>
#include <vector>

//////////////////////////////////////////////////////////////////////////////

struct tGauge
{
	poly_model* cockpit;				// cockpit model of gauge
	bsp_info* monitor;					// monitor in cockpit.
	int first_vert;
	bool functional;					// is this gauge working?
	ushort mask;						// mask of stat for gauge
};

//	which monitors correspond to what faces on the cockpit model.
#define PRIMARY_MONITOR		SOF_MONITOR1
#define SHIP_MONITOR		SOF_MONITOR2
#define SECONDARY_MONITOR	SOF_MONITOR3
#define ENERGY1_MONITOR		SOF_MONITOR4
#define ENERGY2_MONITOR		SOF_MONITOR5
#define SHIELD_MONITOR		SOF_MONITOR6
#define AFTERBURN_MONITOR	SOF_MONITOR7

//Hack vars for turning off the monitors
bool Disable_primary_monitor = 0, Disable_secondary_monitor = 0;

//////////////////////////////////////////////////////////////////////////////

//	Available gauges to be rendered.
static tStatMask Gauge_mask;

//	sets for next frame the functional state of each gauge (on/off).
static tGauge Gauge_list[NUM_GAUGES];

// Returns the cockpit HUD image for the selected weapon slot.
inline int get_weapon_hud_image(int player, int type);

// Gauges publish frame-owned quads from the same immutable cockpit transforms.
inline int GAUGE_INDEX(tStatMask mask);
static constexpr float GAUGE_INVSHIPRING_DELTA = .06f;
// No g3 projection, renderer-state capture, or shared model pose is involved.
void PrepareGameCockpitGauges(d3scene::FrameData& frame,const ModelTransforms& transforms,
    const d3scene::Transform& placement,bool moving)
{
    using Quad = std::array<d3scene::Vec3,4>;
    const auto asset = d3scene::assets().quadMesh();
    int order = 0;
    int32_t text_order = 0;
    d3scene::View gauge_view;
    gauge_view.kind = d3scene::ViewKind::Preview; gauge_view.history_key = SceneCockpitGauges;
    gauge_view.viewport = gauge_view.projection_anchor = {0,0,Game_window_w,Game_window_h};
    gauge_view.zoom = SceneCockpitZoom;
    const auto camera = d3scene::prepareCamera(gauge_view);
    SceneTextStyle text_style; text_style.font = HUD_FONT; text_style.color = {0,180.f/255,0};
    const float window_scale = float(Game_window_w)/Max_window_w;
    // Authored HUD offsets use the 4:3 reference width, while monitor geometry
    // uses the full cockpit viewport. Previously this survived only as stale
    // Hud_aspect_x after RenderHUDFrame restored Game_window_w.
    const float hud_x = float(Game_window_h*4/3)/DEFAULT_HUD_WIDTH, hud_y = float(Game_window_h)/DEFAULT_HUD_HEIGHT;
    text_style.scale = (window_scale <= .6f ? .6f : window_scale <= .8f ? .8f : 1.f)*ConfigNormalizeHudTextScale(Hud_text_scale);
    const auto text = [&](int x,int y,const char* value) {
        auto style = text_style;
        PrepareSceneText(frame,SceneCockpitText,text_order,x,y,value,style);
        style.additive = true;
        PrepareSceneText(frame,SceneCockpitText,text_order,x,y,value,style);
    };
    const auto mix = [](d3scene::Vec3 a,d3scene::Vec3 b,float t) {
        return d3scene::Vec3{a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,a.z+(b.z-a.z)*t};
    };
    const auto append = [&](const Quad& points,int bitmap,float alpha,bool additive,
        bool texture_alpha,float u0,float v0,float u1,float v1) {
        d3scene::PreparedMesh mesh;
        mesh.asset = asset; mesh.collection = SceneCockpitGauges;
        mesh.draw_purpose = d3scene::DrawPurpose::Gauge;
        mesh.vertices.assign(points.begin(),points.end());
        auto& surface = mesh.surface;
        surface.composition_order = order++;
        surface.bitmap = bitmap < 0 ? BAD_BITMAP_HANDLE : bitmap;
        surface.unlit = true; surface.two_sided = true; surface.depth = d3scene::DepthMode::Disabled;
        surface.medium_transport = false;
        surface.blend = additive ? d3scene::BlendMode::Additive : d3scene::BlendMode::Alpha;
        surface.alpha = alpha; surface.texture_alpha = texture_alpha;
        surface.texture_wrap = false;
        surface.u_offset = u0; surface.v_offset = v0; surface.texture_scale = {u1-u0,v1-v0};
        frame.meshes.push_back(std::move(mesh));
    };
    const auto image = [&](const Quad& points,int bitmap,float alpha = 1.f,bool additive = false) {
        append(points,bitmap,alpha,additive,true,0,0,1,1);
    };
    const tStatMask masks[] = {STAT_PRIMARYLOAD,STAT_SECONDARYLOAD,STAT_SHIELDS,STAT_SHIP,STAT_ENERGY,STAT_AFTERBURN};
    for (const auto mask : masks) {
      if (!(Gauge_mask & mask)) continue;
      const int first = GAUGE_INDEX(mask);
      if (first < 0) throw std::logic_error("Prepared cockpit gauge has no registered monitor");
      for (int side = 0; side < (mask == STAT_ENERGY ? 2 : 1); ++side) {
        const auto& gauge = Gauge_list[first+side];
        if (!gauge.monitor || !gauge.functional) continue;
        const int part = static_cast<int>(gauge.monitor-gauge.cockpit->submodel);
        if (part < 0 || part >= transforms.count || gauge.monitor->num_faces != 1 || gauge.monitor->faces[0].nverts != 4)
            throw std::logic_error("Prepared cockpit gauge has invalid quad topology");
        Quad points;
        for (int i = 0; i < 4; ++i) {
            const int vertex = gauge.monitor->faces[0].vertnums[(gauge.first_vert+i)%4];
            const auto root = TransformModelPoint(transforms.submodels[part],gauge.monitor->verts[vertex]);
            points[i] = d3scene::transformPoint(placement,{root.x,root.y,root.z});
        }
        if (mask == STAT_PRIMARYLOAD || mask == STAT_SECONDARYLOAD) {
            const bool primary = mask == STAT_PRIMARYLOAD;
            if (!moving) {
                double left=Game_window_w,top=Game_window_h,right=0,bottom=0;
                bool projected=true;
                for (const auto& point : points) {
                    const auto clip=d3scene::project(camera,point);
                    if (clip.w <= 0) { projected=false; break; }
                    const double x=(clip.x/clip.w+1)*.5*Game_window_w;
                    const double y=(1-clip.y/clip.w)*.5*Game_window_h;
                    left=(std::min)(left,x); right=(std::max)(right,x);
                    top=(std::min)(top,y); bottom=(std::max)(bottom,y);
                }
                if (projected) {
                    const int x=int(std::floor(left)),y=int(std::floor(top));
                    frame.view_regions.push_back({primary ? SceneCockpitPrimaryView : SceneCockpitSecondaryView,
                        {Game_window_x+x,Game_window_y+y,int(std::ceil(right))+2-x,int(std::ceil(bottom))+2-y}});
                }
            }
            if ((primary && Disable_primary_monitor) || (!primary && Disable_secondary_monitor)) continue;
            if (Cockpit_alt_mode) {
                d3scene::Vec3 center;
                for (const auto point : points) { center.x += point.x*.25f; center.y += point.y*.25f; center.z += point.z*.25f; }
                auto expanded = points;
                for (auto& point : expanded) point = mix(center,point,1.1f);
                append(expanded,BAD_BITMAP_HANDLE,.2f*.7f,false,false,0,0,1,1);
                auto& backing = frame.meshes.back();
                backing.collection = SceneCockpitMonitorBacking;
                backing.draw_purpose = d3scene::DrawPurpose::Gauge;
                backing.surface.bitmap = -1; backing.surface.base_color = {0,0,0};
                backing.surface.raster_stripe = {3.f*ConfigNormalizeSupersamplingFactor(Render_preferred_state.supersampling_factor),
                    static_cast<float>(frame.simulation_time)*14,.5f};
            }
            append(points,get_weapon_hud_image(Player_num,primary ? PW_PRIMARY : PW_SECONDARY),127.f/255,false,false,0,0,1,1);
            if (Cockpit_alt_mode) frame.meshes.back().collection = SceneCockpitMonitorBacking;
            if (!moving) {
                const auto clip = d3scene::project(camera,points[0]);
                if (clip.w > 0) {
                    const int x = int((clip.x/clip.w+1)*.5*Game_window_w);
                    int y = int((1-clip.y/clip.w)*.5*Game_window_h);
                    const int weapon_type = primary ? PW_PRIMARY : PW_SECONDARY;
                    const int index = Players[Player_num].weapon[weapon_type].index;
                    text(int(x+(primary ? 30 : 5)*hud_x),int(y+10*hud_y),TXT(Static_weapon_ckpt_names[index][0]));
                    const auto* second_line = TXT(Static_weapon_ckpt_names[index][1]);
                    if (*second_line) {
                        text(int(x+(primary ? 35 : 5)*hud_x),int(y+20*hud_y),second_line);
                        y = int(y+10*hud_y);
                    }
                    const auto& ship = Ships[Players[Player_num].ship_index];
                    if (!primary || ship.static_wb[index].ammo_usage) {
                        const int ammo = Players[Player_num].weapon_ammo[index]; char amount[32];
                        if (primary && (ship.fire_flags[index]&SFF_TENTHS)) snprintf(amount,sizeof(amount),"%d.%d",ammo/10,ammo%10);
                        else snprintf(amount,sizeof(amount),"%d",ammo);
                        text(int(x+(primary ? 40 : 15)*hud_x),int(y+20*hud_y),amount);
                    }
                }
            }
        } else if (mask == STAT_SHIELDS) {
            const float amount = (std::min)(1.f,Objects[Players[Player_num].objnum].shields/INITIAL_SHIELDS);
            int index = static_cast<int>(std::ceil((1.f-amount-.1f)*NUM_SHIELD_GAUGE_FRAMES));
            if (index < NUM_SHIELD_GAUGE_FRAMES) image(points,HUD_resources.shield_bmp[(std::max)(0,index)]);
        } else if (mask == STAT_ENERGY) {
            const float amount = (std::min)(1.f,Players[Player_num].energy/INITIAL_ENERGY);
            auto used = points, left = points;
            used[3] = mix(points[3],points[0],amount); used[2] = mix(points[2],points[1],amount);
            left[0] = mix(points[0],points[3],1-amount); left[1] = mix(points[1],points[2],1-amount);
            append(used,HUD_resources.energy_bmp,64.f/255,false,true,side ? 1.f : 0.f,0,side ? 0.f : 1.f,1-amount);
            append(left,HUD_resources.energy_bmp,1,false,true,side ? 1.f : 0.f,1-amount,side ? 0.f : 1.f,1);
        } else if (mask == STAT_AFTERBURN) {
            const float amount = Players[Player_num].afterburn_time_left/AFTERBURN_TIME;
            auto used = points, left = points;
            used[3] = mix(points[3],points[2],amount); used[0] = mix(points[0],points[1],amount);
            left[1] = mix(points[1],points[0],1-amount); left[2] = mix(points[2],points[3],1-amount);
            append(used,HUD_resources.afterburn_bmp,64.f/255,false,true,amount,0,1,1);
            append(left,HUD_resources.afterburn_bmp,1,false,true,0,0,amount,1);
        } else if (mask == STAT_SHIP) {
            unsigned char alpha = 255;
            const auto* effect = Objects[Players[Player_num].objnum].effect_info;
            if (effect && (effect->type_flags & EF_CLOAKED)) {
                const float time = effect->cloak_time;
                alpha = time < HUD_CLOAKEND_TIME ? static_cast<unsigned char>(128-127*FixCos(65536.f*(time-int(time)))) : 0;
            }
            image(points,HUD_resources.ship_bmp,alpha/255.f);
            image(points,HUD_resources.ship_bmp,alpha/255.f,true);
            if (Players[Player_num].flags & PLAYER_FLAGS_INVULNERABLE) {
                const float phase = static_cast<float>(frame.simulation_time-std::floor(frame.simulation_time));
                const float aspect = float(Game_window_w)/Game_window_h, amount = phase*GAUGE_INVSHIPRING_DELTA;
                const float x = aspect >= 1 ? amount/aspect : amount, y = aspect >= 1 ? amount : amount*aspect;
                auto pulse = points;
                pulse[0].x -= x; pulse[0].y += y; pulse[1].x += x; pulse[1].y += y;
                pulse[2].x += x; pulse[2].y -= y; pulse[3].x -= x; pulse[3].y -= y;
                const float fade = static_cast<unsigned char>(255*(1-phase/2))/255.f;
                image(pulse,HUD_resources.invpulse_bmp,fade); image(pulse,HUD_resources.invpulse_bmp,fade,true);
            }
        }
      }
    }
    if (AutomatedCapturePending() || AutomatedCaptureFrameIndex() == 0) {
        size_t glyphs = 0;
        for (const auto& mesh : frame.meshes) if (mesh.collection == SceneCockpitText) glyphs += mesh.vertices.size()/4;
        AutomatedCaptureLog("scene gauge graphics prepared quads=%d glyphs=%u text_batches=%d backing=%d",
            order,static_cast<unsigned>(glyphs),text_order,Cockpit_alt_mode ? 1 : 0);
    }
}


// correctly orders monitor vertices based off of UVs
int GetFirstVert(bsp_info* sm);


//	tells what gauge index is the gauge stat item.
inline int GAUGE_INDEX(tStatMask mask)
{
	int i;

	for (i = 0; i < NUM_GAUGES; i++)
		if (Gauge_list[i].mask == mask)
			return i;

	return -1;
}


//////////////////////////////////////////////////////////////////////////////
//	Initialization routines

//	initializes cockpit gauges
void InitGauges(ushort gauge_mask)
{
	Gauge_mask = gauge_mask;
	int gauge = 0;

	for (int i = 0; i < NUM_GAUGES; i++)
	{
		ushort mask = Gauge_mask & (1 << i);

		if (mask == STAT_SHIELDS)
			Gauge_list[gauge].monitor = CockpitGetMonitorSubmodel(SHIELD_MONITOR);
		if (mask == STAT_PRIMARYLOAD)
			Gauge_list[gauge].monitor = CockpitGetMonitorSubmodel(PRIMARY_MONITOR);
		else if (mask == STAT_SECONDARYLOAD)
			Gauge_list[gauge].monitor = CockpitGetMonitorSubmodel(SECONDARY_MONITOR);
		else if (mask == STAT_SHIP)
			Gauge_list[gauge].monitor = CockpitGetMonitorSubmodel(SHIP_MONITOR);
		else if (mask == STAT_ENERGY)
		{
			// initialize two gauges. one for left, and one for right energy monitor.
			Gauge_list[gauge].monitor = CockpitGetMonitorSubmodel(ENERGY1_MONITOR);
			Gauge_list[gauge].functional = false;
			Gauge_list[gauge].cockpit = CockpitGetPolyModel();
			Gauge_list[gauge].mask = mask;
			Gauge_list[gauge].first_vert = GetFirstVert(Gauge_list[gauge].monitor);
			gauge++;
			Gauge_list[gauge].monitor = CockpitGetMonitorSubmodel(ENERGY2_MONITOR);
		}
		else if (mask == STAT_AFTERBURN)
			Gauge_list[gauge].monitor = CockpitGetMonitorSubmodel(AFTERBURN_MONITOR);

		if (mask)
		{
			Gauge_list[gauge].functional = false;
			Gauge_list[gauge].cockpit = CockpitGetPolyModel();
			Gauge_list[gauge].mask = mask;
			Gauge_list[gauge].first_vert = GetFirstVert(Gauge_list[gauge].monitor);

			gauge++;
		}
	}
}

//	deinitializes cockpit gauges
void CloseGauges()
{
	for (int i = 0; i < NUM_GAUGES; i++)
	{
		Gauge_list[i].monitor = NULL;
	}
	Gauge_mask = 0;
}




//	sets whether the gauges are functional
void FlagGaugesFunctional(tStatMask mask)
{
	ushort i = 0x8000, j = NUM_GAUGES;

	while (i)
	{
		if (mask & i)
			Gauge_list[j - 1].functional = true;
		i = i >> 1;
		j--;
	}
}


//	sets whether the gauges are functional
void FlagGaugesNonfunctional(tStatMask mask)
{
	ushort i = 0x8000, j = NUM_GAUGES;

	while (i)
	{
		if (mask & i)
			Gauge_list[j - 1].functional = false;
		i = i >> 1;
		j--;
	}
}


inline int get_weapon_hud_image(int player, int type)
{
	player_weapon* pw = &Players[player].weapon[type];
	weapon* wpn = GetWeaponFromIndex(player, pw->index);

	return (wpn ? wpn->hud_image_handle : BAD_BITMAP_HANDLE);
}



// Returns the vertex index that corresponds to the upper-left vertex
int GetFirstVert(bsp_info* sm)
{
	ASSERT(sm->num_faces == 1 && sm->faces[0].nverts == 4);
	int i;
	polyface* face = &sm->faces[0];
	int first_vert = -1;

	for (i = 0; i < 4 && first_vert == -1; i++)
	{
		if (sm->alpha[face->vertnums[i]] < .5)
			first_vert = i;
	}

	if (first_vert < 0)
		first_vert = 0;		// If no alpha info, just set to first vertex

	return first_vert;
}
