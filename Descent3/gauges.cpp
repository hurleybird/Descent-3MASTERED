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
	bool just_init;						// gauge just initialized?
	ushort mask;						// mask of stat for gauge
	int state;							// gauge dependent state data.
	union {								// data for gauge.
		int i;
		float f;
	}
	data;
	g3Point pts[4];						// points of monitor in view coordinates. (may be cached from last frame)
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

//	sets for next frame which gauges have been modified.
static tStatMask Gauge_mask_modified;

//	sets for next frame the functional state of each gauge (on/off).
static tGauge Gauge_list[NUM_GAUGES];

//	position of cockpit 
static vector* Render_gauge_pos;

// matrix of cockpit
static matrix* Render_gauge_matrix;

//	normalized times of cockpit
static float* Render_normalized_times;

//	if gauges are moving in cockpit.
static bool Render_gauge_moving;					// gauge is moving, but temporarily off.
static bool Render_gauge_reset;					//	set this if gauges will be moving, but are still active


//	loads all shield bitmaps into memory
void FreeShieldFrames();

//	loads all ship bitmaps into memory
void FreeShipFrames();

//	projects monitor coordinates to screen coordinates
void RotateMonitorPosition(tGauge* gauge);

//renders the current inventory item name at the x,y position
void InventoryRenderGauge(int x, int y);

//	returns the cockpit HUD image for the selected weapon slot
inline int get_weapon_hud_image(int player, int type);

//	renders the primary monitor gauge
void RenderPrimaryMonitor(tGauge* gauge, bool modified);

//	renders the secondary monitor gauge
void RenderSecondaryMonitor(tGauge* gauge, bool modified);

//	renders the shield gauge monitor
void RenderShieldMonitor(tGauge* gauge, bool modified);

//	renders the ship gauge monitor
void RenderShipMonitor(tGauge* gauge, bool modified);

//	renders the ship gauge monitor
void RenderEnergyMonitor(tGauge* gauge, int orient, bool modified);

//	renders the ship gauge monitor
void RenderAfterburnMonitor(tGauge* gauge, bool modified);

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
        mesh.vertices.assign(points.begin(),points.end());
        auto& surface = mesh.surface;
        surface.composition_order = order++;
        surface.bitmap = bitmap < 0 ? BAD_BITMAP_HANDLE : bitmap;
        surface.unlit = true; surface.two_sided = true; surface.depth = d3scene::DepthMode::Disabled;
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
            if ((primary && Disable_primary_monitor) || (!primary && Disable_secondary_monitor)) continue;
            if (Cockpit_alt_mode) {
                d3scene::Vec3 center;
                for (const auto point : points) { center.x += point.x*.25f; center.y += point.y*.25f; center.z += point.z*.25f; }
                auto expanded = points;
                for (auto& point : expanded) point = mix(center,point,1.1f);
                append(expanded,BAD_BITMAP_HANDLE,.2f*.7f,false,false,0,0,1,1);
                auto& backing = frame.meshes.back();
                backing.collection = SceneCockpitMonitorBacking;
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

//	renders a monitor style quad 
void DrawGaugeMonitor(g3Point* pts, int bm, float brightness, float* alphas);
void DrawGaugeMonitorSurface(g3Point* pts);

//	renders a square texture onto the screen.
void DrawGaugeQuad(g3Point* pts, int bm, float u0, float v0, float u1, float v1, ubyte alpha, bool saturate);

//	renders a square texture onto the screen.
void DrawGaugeQuad(g3Point* pts, int bm, ubyte alpha = 255, bool saturate = false);

//	renders a flat poly onto the screen with given color
void DrawGaugeQuadFlat(g3Point* pts, float r, float g, float b, ubyte alpha);
static void DrawGaugeQuadFlatImmediate(g3Point* pts, float r, float g, float b, ubyte alpha);

//	renders a flat poly onto the screen with 4 colors (for each vertex)
void DrawGaugeQuadFlat(g3Point* pts, float* r, float* g, float* b, ubyte alpha);

// correctly orders monitor vertices based off of UVs
int GetFirstVert(bsp_info* sm);

struct GaugeQuadBatchKey
{
	int bitmap_handle;
	texture_type texture_type_value;
	light_state lighting;
	color_model color_model_value;
	sbyte alpha_type;
	ubyte alpha_value;

	bool Equals(const GaugeQuadBatchKey& other) const
	{
		return bitmap_handle == other.bitmap_handle &&
			texture_type_value == other.texture_type_value &&
			lighting == other.lighting &&
			color_model_value == other.color_model_value &&
			alpha_type == other.alpha_type &&
			alpha_value == other.alpha_value;
	}
};

struct GaugeQuadBatchItem
{
	g3Point points[4];
	g3Point* pointlist[4];

	void RefreshPointList()
	{
		for (int i = 0; i < 4; i++)
			pointlist[i] = &points[i];
	}
};

struct GaugeQuadBatch
{
	GaugeQuadBatchKey key;
	std::vector<GaugeQuadBatchItem> items;
};

static std::vector<GaugeQuadBatch> Gauge_quad_batches;

static void FlushGaugeQuadBatches()
{
	if (Gauge_quad_batches.empty())
		return;

	rend_SetZBufferState(0);
	rend_SetOverlayType(OT_NONE);

	for (size_t batch_index = 0; batch_index < Gauge_quad_batches.size(); batch_index++)
	{
		GaugeQuadBatch& batch = Gauge_quad_batches[batch_index];
		if (batch.items.empty())
			continue;

		rend_SetTextureType(batch.key.texture_type_value);
		rend_SetLighting(batch.key.lighting);
		rend_SetColorModel(batch.key.color_model_value);
		rend_SetAlphaValue(batch.key.alpha_value);
		rend_SetAlphaType(batch.key.alpha_type);

		std::vector<renderer_poly_batch_item> renderer_items(batch.items.size());
		for (size_t item_index = 0; item_index < batch.items.size(); item_index++)
		{
			batch.items[item_index].RefreshPointList();
			renderer_items[item_index].pointlist = batch.items[item_index].pointlist;
			renderer_items[item_index].nv = 4;
		}

		rend_DrawPolygon3DBatch(batch.key.bitmap_handle, renderer_items.data(), (int)renderer_items.size(), MAP_TYPE_BITMAP);
	}

	Gauge_quad_batches.clear();
}

static bool QueueGaugeQuad(const GaugeQuadBatchKey& key, g3Point* pts)
{
	g3Codes face_cc;
	face_cc.cc_or = 0;
	face_cc.cc_and = 0xff;
	for (int i = 0; i < 4; i++)
	{
		face_cc.cc_or |= pts[i].p3_codes;
		face_cc.cc_and &= pts[i].p3_codes;
	}

	if (face_cc.cc_or)
		return false;

	GaugeQuadBatchItem item;
	for (int i = 0; i < 4; i++)
	{
		item.points[i] = pts[i];
		item.points[i].p3_flags &= ~PF_TEMP_POINT;
		if (!(item.points[i].p3_flags & PF_PROJECTED))
			g3_ProjectPoint(&item.points[i]);
	}

	for (size_t i = 0; i < Gauge_quad_batches.size(); i++)
	{
		if (Gauge_quad_batches[i].key.Equals(key))
		{
			Gauge_quad_batches[i].items.push_back(item);
			return true;
		}
	}

	GaugeQuadBatch batch;
	batch.key = key;
	batch.items.push_back(item);
	Gauge_quad_batches.push_back(batch);
	return true;
}

static void ExpandGaugeQuad(g3Point* expanded_pts, g3Point* pts, float scale)
{
	vector center = Zero_vector;
	vector prerot_center = Zero_vector;

	for (int i = 0; i < 4; i++)
	{
		center += pts[i].p3_vec;
		prerot_center += pts[i].p3_vecPreRot;
	}

	center /= 4.0f;
	prerot_center /= 4.0f;

	for (int i = 0; i < 4; i++)
	{
		expanded_pts[i] = pts[i];
		expanded_pts[i].p3_vec = center + (pts[i].p3_vec - center) * scale;
		expanded_pts[i].p3_vecPreRot = prerot_center + (pts[i].p3_vecPreRot - prerot_center) * scale;
		expanded_pts[i].p3_flags &= ~PF_PROJECTED;
	}
}

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
	Gauge_mask_modified = Gauge_mask;
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
			Gauge_list[gauge].state = 0;
			Gauge_list[gauge].cockpit = CockpitGetPolyModel();
			Gauge_list[gauge].just_init = true;
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
			Gauge_list[gauge].state = 0;
			Gauge_list[gauge].cockpit = CockpitGetPolyModel();
			Gauge_list[gauge].just_init = true;
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
		ushort mask = Gauge_mask & (1 << i);
		Gauge_list[i].monitor = NULL;
	}
	Gauge_mask = 0;
	Gauge_mask_modified = 0;
}


static void RenderGaugeMonitorSurface(tStatMask mask, int weapon_type)
{
	if (!(Gauge_mask & mask))
		return;

	ASSERT(GAUGE_INDEX(mask) != -1);
	tGauge* gauge = &Gauge_list[GAUGE_INDEX(mask)];
	if (!gauge->monitor || !gauge->functional)
		return;
	if (weapon_type == PW_PRIMARY && Disable_primary_monitor)
		return;
	if (weapon_type == PW_SECONDARY && Disable_secondary_monitor)
		return;

	float alphas[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
	int hud_image = get_weapon_hud_image(Player_num, weapon_type);

	RotateMonitorPosition(gauge);
	DrawGaugeMonitorSurface(gauge->pts);
	DrawGaugeMonitor(gauge->pts, hud_image, 0.5f, alphas);
}

void RenderGaugeMonitorSurfaces(vector* cockpit_pos, matrix* cockpit_mat, float* normalized_time, bool moving, bool reset)
{
	renderer_3d_draw_call_scope gauge_draw_scope(RENDERER_DRAW_CALL_3D_GAUGE);

	Render_gauge_pos = cockpit_pos;
	Render_gauge_matrix = cockpit_mat;
	Render_normalized_times = normalized_time;
	Render_gauge_moving = moving;
	Render_gauge_reset = reset;

	RenderGaugeMonitorSurface(STAT_PRIMARYLOAD, PW_PRIMARY);
	RenderGaugeMonitorSurface(STAT_SECONDARYLOAD, PW_SECONDARY);
	FlushGaugeQuadBatches();
}

//	renders gauges
void RenderGauges(vector* cockpit_pos, matrix* cockpit_mat, float* normalized_time, bool moving, bool reset)
{
	renderer_3d_draw_call_scope gauge_draw_scope(RENDERER_DRAW_CALL_3D_GAUGE);
	tGauge* gauge;
	float font_aspect_x;
	float font_aspect_y;

	Render_gauge_pos = cockpit_pos;
	Render_gauge_matrix = cockpit_mat;
	Render_normalized_times = normalized_time;
	Render_gauge_moving = moving;
	Render_gauge_reset = reset;

	grtext_Reset();
	grtext_SetFont(HUD_FONT);
	//	for lores screens, we use different fonts, so DONT SCALE.
	font_aspect_x = (float)Game_window_w / Max_window_w;
	font_aspect_y = (float)Game_window_h / Max_window_h;

	if (font_aspect_x <= 0.60)
		grtext_SetFontScale(0.60f * ConfigNormalizeHudTextScale(Hud_text_scale));
	else if (font_aspect_x <= 0.80)
		grtext_SetFontScale(0.80f * ConfigNormalizeHudTextScale(Hud_text_scale));
	else
		grtext_SetFontScale(ConfigNormalizeHudTextScale(Hud_text_scale));

	if (Gauge_mask & STAT_PRIMARYLOAD)
	{
		ASSERT(GAUGE_INDEX(STAT_PRIMARYLOAD) != -1);
		gauge = &Gauge_list[GAUGE_INDEX(STAT_PRIMARYLOAD)];
		if (gauge->monitor)
		{
			RotateMonitorPosition(gauge);
			RenderPrimaryMonitor(gauge, (Gauge_mask_modified & STAT_PRIMARYLOAD) ? true : false);
		}
		gauge->just_init = false;
	}
	if (Gauge_mask & STAT_SECONDARYLOAD)
	{
		ASSERT(GAUGE_INDEX(STAT_SECONDARYLOAD) != -1);
		gauge = &Gauge_list[GAUGE_INDEX(STAT_SECONDARYLOAD)];
		if (gauge->monitor)
		{
			RotateMonitorPosition(gauge);
			RenderSecondaryMonitor(gauge, (Gauge_mask_modified & STAT_SECONDARYLOAD) ? true : false);
		}
		gauge->just_init = false;
	}
	if (Gauge_mask & STAT_SHIELDS)
	{
		ASSERT(GAUGE_INDEX(STAT_SHIELDS) != -1);
		gauge = &Gauge_list[GAUGE_INDEX(STAT_SHIELDS)];
		if (gauge->monitor)
		{
			RotateMonitorPosition(gauge);
			RenderShieldMonitor(gauge, (Gauge_mask_modified & STAT_SHIELDS) ? true : false);
		}
		gauge->just_init = false;
	}
	if (Gauge_mask & STAT_SHIP)
	{
		ASSERT(GAUGE_INDEX(STAT_SHIP) != -1);
		gauge = &Gauge_list[GAUGE_INDEX(STAT_SHIP)];
		if (gauge->monitor)
		{
			RotateMonitorPosition(gauge);
			RenderShipMonitor(gauge, (Gauge_mask_modified & STAT_SHIP) ? true : false);
		}
		gauge->just_init = false;
	}
	if (Gauge_mask & STAT_ENERGY)
	{
		ASSERT(GAUGE_INDEX(STAT_ENERGY) != -1);
		gauge = &Gauge_list[GAUGE_INDEX(STAT_ENERGY)];
		if (gauge->monitor)
		{
			RotateMonitorPosition(gauge);
			RenderEnergyMonitor(gauge, 0, (Gauge_mask_modified & STAT_ENERGY) ? true : false);
		}
		gauge->just_init = false;
		gauge = &Gauge_list[GAUGE_INDEX(STAT_ENERGY) + 1];
		if (gauge->monitor)
		{
			RotateMonitorPosition(gauge);
			RenderEnergyMonitor(gauge, 1, (Gauge_mask_modified & STAT_ENERGY) ? true : false);
		}
		gauge->just_init = false;
	}
	if (Gauge_mask & STAT_AFTERBURN)
	{
		ASSERT(GAUGE_INDEX(STAT_AFTERBURN) != -1);
		gauge = &Gauge_list[GAUGE_INDEX(STAT_AFTERBURN)];
		if (gauge->monitor)
		{
			RotateMonitorPosition(gauge);
			RenderAfterburnMonitor(gauge, (Gauge_mask_modified & STAT_ENERGY) ? true : false);
		}
		gauge->just_init = false;
	}

	//	render all text
	FlushGaugeQuadBatches();
	grtext_Flush();

	Gauge_mask_modified = 0;
}


//	flags certain gauges to be modified next frame.
void FlagGaugesModified(tStatMask mask_modified)
{
	Gauge_mask_modified |= mask_modified;
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


//	renders the primary monitor gauge
//		this will normally render the primary weapon currently selected
//		can render another view though.
//	renders the primary monitor gauge

inline int get_weapon_hud_image(int player, int type)
{
	player_weapon* pw = &Players[player].weapon[type];
	weapon* wpn = GetWeaponFromIndex(player, pw->index);

	return (wpn ? wpn->hud_image_handle : BAD_BITMAP_HANDLE);
}

void RenderPrimaryMonitor(tGauge* gauge, bool modified)
{
	if (Disable_primary_monitor)
		return;

	if (gauge->functional)
	{
		if (!Cockpit_alt_mode)
		{
			float alphas[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
			int hud_image = get_weapon_hud_image(Player_num, PW_PRIMARY);
			DrawGaugeMonitor(gauge->pts, hud_image, 0.5f, alphas);
		}

		if (!Render_gauge_moving)
		{
			int x1, y;
			int index = Players[Player_num].weapon[PW_PRIMARY].index;
			ship* ship = &Ships[Players[Player_num].ship_index];
			otype_wb_info* wb = &ship->static_wb[index];

			gauge->pts[0].p3_flags &= (~PF_PROJECTED);
			g3_ProjectPoint(&gauge->pts[0]);
			x1 = gauge->pts[0].p3_sx;
			y = gauge->pts[0].p3_sy;

			grtext_SetFlags(0);
			grtext_SetFancyColor(GR_RGB(0, 180, 0), GR_RGB(0, 180, 0), GR_RGB(0, 180, 0), GR_RGB(0, 180, 0));
			grtext_Printf(x1 + HUD_X(30), y + HUD_Y(10), "%s", TXT(Static_weapon_ckpt_names[index][0]));
			grtext_SetFlags(GRTEXTFLAG_SATURATE);
			grtext_Printf(x1 + HUD_X(30), y + HUD_Y(10), "%s", TXT(Static_weapon_ckpt_names[index][0]));

			if (strlen(TXT(Static_weapon_ckpt_names[Players[Player_num].weapon[PW_PRIMARY].index][1])))
			{
				grtext_SetFlags(0);
				grtext_Printf(x1 + HUD_X(35), y + HUD_Y(20), "%s", TXT(Static_weapon_ckpt_names[index][1]));
				grtext_SetFlags(GRTEXTFLAG_SATURATE);
				grtext_Printf(x1 + HUD_X(35), y + HUD_Y(20), "%s", TXT(Static_weapon_ckpt_names[index][1]));
				y = y + HUD_Y(10);
			}

			if (wb && wb->ammo_usage)
			{
				int ammo = Players[Player_num].weapon_ammo[index];
				grtext_SetFlags(0);
				if (ship->fire_flags[index] & SFF_TENTHS) {
					grtext_Printf(x1 + HUD_X(40), y + HUD_Y(20), "%d.%d", ammo / 10, ammo % 10);
					grtext_SetFlags(GRTEXTFLAG_SATURATE);
					grtext_Printf(x1 + HUD_X(40), y + HUD_Y(20), "%d.%d", ammo / 10, ammo % 10);
				}
				else {
					grtext_Printf(x1 + HUD_X(40), y + HUD_Y(20), "%d", ammo);
					grtext_SetFlags(GRTEXTFLAG_SATURATE);
					grtext_Printf(x1 + HUD_X(40), y + HUD_Y(20), "%d", ammo);
				}
			}
		}
	}
}


//	renders the secondary monitor gauge
void RenderSecondaryMonitor(tGauge* gauge, bool modified)
{
	if (Disable_secondary_monitor)
		return;

	if (gauge->functional)
	{
		if (!Cockpit_alt_mode)
		{
			float alphas[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
			int hud_image = get_weapon_hud_image(Player_num, PW_SECONDARY);
			DrawGaugeMonitor(gauge->pts, hud_image, 0.5f, alphas);
		}

		if (!Render_gauge_moving)
		{
			int x1, y;

			gauge->pts[0].p3_flags &= (~PF_PROJECTED);
			g3_ProjectPoint(&gauge->pts[0]);
			x1 = gauge->pts[0].p3_sx;
			y = gauge->pts[0].p3_sy;

			grtext_SetFancyColor(GR_RGB(0, 180, 0), GR_RGB(0, 180, 0), GR_RGB(0, 180, 0), GR_RGB(0, 180, 0));
			grtext_SetFlags(0);
			grtext_Printf(x1 + HUD_X(5), y + HUD_Y(10), "%s", TXT(Static_weapon_ckpt_names[Players[Player_num].weapon[PW_SECONDARY].index][0]));
			grtext_SetFlags(GRTEXTFLAG_SATURATE);
			grtext_Printf(x1 + HUD_X(5), y + HUD_Y(10), "%s", TXT(Static_weapon_ckpt_names[Players[Player_num].weapon[PW_SECONDARY].index][0]));
			if (strlen(TXT(Static_weapon_ckpt_names[Players[Player_num].weapon[PW_SECONDARY].index][1])))
			{
				grtext_SetFlags(0);
				grtext_Printf(x1 + HUD_X(5), y + HUD_Y(20), "%s", TXT(Static_weapon_ckpt_names[Players[Player_num].weapon[PW_SECONDARY].index][1]));
				grtext_SetFlags(GRTEXTFLAG_SATURATE);
				grtext_Printf(x1 + HUD_X(5), y + HUD_Y(20), "%s", TXT(Static_weapon_ckpt_names[Players[Player_num].weapon[PW_SECONDARY].index][1]));
				y = y + HUD_Y(10);
			}
			grtext_SetFlags(0);
			grtext_Printf(x1 + HUD_X(15), y + HUD_Y(20), "%d", Players[Player_num].weapon_ammo[Players[Player_num].weapon[PW_SECONDARY].index]);
			grtext_SetFlags(GRTEXTFLAG_SATURATE);
			grtext_Printf(x1 + HUD_X(15), y + HUD_Y(20), "%d", Players[Player_num].weapon_ammo[Players[Player_num].weapon[PW_SECONDARY].index]);
		}
	}
}


//	renders the shield gauge monitor
void RenderShieldMonitor(tGauge* gauge, bool modified)
{
	float alpha_mod = (Objects[Players[Player_num].objnum].shields) / (float)INITIAL_SHIELDS;

	if (alpha_mod > 1.0f)
		alpha_mod = 1.0f;

	int img = (int)ceil((1.0f - alpha_mod - 0.1f) * NUM_SHIELD_GAUGE_FRAMES);
	if (img < NUM_SHIELD_GAUGE_FRAMES)
	{
		if (img < 0)
			img = 0;
		img = HUD_resources.shield_bmp[img];

		if (gauge->functional)
			DrawGaugeQuad(gauge->pts, img, 255);
	}
}


//	renders the ship gauge monitor
void RenderEnergyMonitor(tGauge* gauge, int orient, bool modified)
{
	if (gauge->functional)
	{
		float img_h = bm_h(HUD_resources.energy_bmp, 0);
		float normalized_energy = (float)Players[Player_num].energy / (float)INITIAL_ENERGY;
		g3Point used_pts[4], left_pts[4];
		float img_energy_h;
		float hgt_scalar;
		int i;

		if (normalized_energy > 1.0f)
			normalized_energy = 1.0f;

		img_energy_h = (normalized_energy * img_h);

		for (i = 0; i < 4; i++)
		{
			used_pts[i] = gauge->pts[i];
			left_pts[i] = gauge->pts[i];
		}

		//	draw energy gauge, showing how much energy below 100% you have. draw energy spent faded.
		hgt_scalar = img_energy_h / img_h;
		used_pts[3].p3_vec = used_pts[3].p3_vec - (used_pts[3].p3_vec - used_pts[0].p3_vec) * hgt_scalar;
		used_pts[2].p3_vec = used_pts[2].p3_vec - (used_pts[2].p3_vec - used_pts[1].p3_vec) * hgt_scalar;
		used_pts[3].p3_vecPreRot = used_pts[3].p3_vecPreRot - (used_pts[3].p3_vecPreRot - used_pts[0].p3_vecPreRot) * hgt_scalar;
		used_pts[2].p3_vecPreRot = used_pts[2].p3_vecPreRot - (used_pts[2].p3_vecPreRot - used_pts[1].p3_vecPreRot) * hgt_scalar;

		if (!orient)
			DrawGaugeQuad(used_pts, HUD_resources.energy_bmp, 0, 0, 1, 1.0f - normalized_energy, 64, 0);
		else
			DrawGaugeQuad(used_pts, HUD_resources.energy_bmp, 1, 0, 0, 1.0f - normalized_energy, 64, 0);

		left_pts[0].p3_vec = left_pts[0].p3_vec + (left_pts[3].p3_vec - left_pts[0].p3_vec) * (1.0f - hgt_scalar);
		left_pts[1].p3_vec = left_pts[1].p3_vec + (left_pts[2].p3_vec - left_pts[1].p3_vec) * (1.0f - hgt_scalar);
		left_pts[0].p3_vecPreRot = left_pts[0].p3_vecPreRot + (left_pts[3].p3_vecPreRot - left_pts[0].p3_vecPreRot) * (1.0f - hgt_scalar);
		left_pts[1].p3_vecPreRot = left_pts[1].p3_vecPreRot + (left_pts[2].p3_vecPreRot - left_pts[1].p3_vecPreRot) * (1.0f - hgt_scalar);

		if (!orient)
			DrawGaugeQuad(left_pts, HUD_resources.energy_bmp, 0, 1.0f - normalized_energy, 1, 1, 255, 0);
		else
			DrawGaugeQuad(left_pts, HUD_resources.energy_bmp, 1, 1.0f - normalized_energy, 0, 1, 255, 0);
	}
}



//	renders the ship gauge monitor
void RenderAfterburnMonitor(tGauge* gauge, bool modified)
{
	g3Point used_pts[4], left_pts[4];

	if (gauge->functional)
	{
		float img_w = bm_w(HUD_resources.afterburn_bmp, 0);
		float val = (Players[Player_num].afterburn_time_left / AFTERBURN_TIME);
		float img_burn_w = (val * img_w);
		float w_scalar;
		int i;

		for (i = 0; i < 4; i++)
		{
			used_pts[i] = gauge->pts[i];
			left_pts[i] = gauge->pts[i];
		}

		//	draw energy gauge, 
		//	showing how much energy below 100% you have. draw energy spent faded.
		w_scalar = img_burn_w / img_w;

		used_pts[3].p3_vec = used_pts[3].p3_vec + (used_pts[2].p3_vec - used_pts[3].p3_vec) * w_scalar;
		used_pts[0].p3_vec = used_pts[0].p3_vec + (used_pts[1].p3_vec - used_pts[0].p3_vec) * w_scalar;
		used_pts[3].p3_vecPreRot = used_pts[3].p3_vecPreRot + (used_pts[2].p3_vecPreRot - used_pts[3].p3_vecPreRot) * w_scalar;
		used_pts[0].p3_vecPreRot = used_pts[0].p3_vecPreRot + (used_pts[1].p3_vecPreRot - used_pts[0].p3_vecPreRot) * w_scalar;
		DrawGaugeQuad(used_pts, HUD_resources.afterburn_bmp, val, 0, 1, 1, 64, 0);

		left_pts[1].p3_vec = left_pts[1].p3_vec - (left_pts[1].p3_vec - left_pts[0].p3_vec) * (1.0f - w_scalar);
		left_pts[2].p3_vec = left_pts[2].p3_vec - (left_pts[2].p3_vec - left_pts[3].p3_vec) * (1.0f - w_scalar);
		left_pts[1].p3_vecPreRot = left_pts[1].p3_vecPreRot - (left_pts[1].p3_vecPreRot - left_pts[0].p3_vecPreRot) * (1.0f - w_scalar);
		left_pts[2].p3_vecPreRot = left_pts[2].p3_vecPreRot - (left_pts[2].p3_vecPreRot - left_pts[3].p3_vecPreRot) * (1.0f - w_scalar);
		DrawGaugeQuad(left_pts, HUD_resources.afterburn_bmp, 0, 0, val, 1, 255, 0);
	}
}



//	renders the ship gauge monitor
void RenderShipMonitor(tGauge* gauge, bool modified)
{
	if (!gauge->functional)
		return;

	ubyte alpha = 255;
	if (Objects[Players[Player_num].objnum].effect_info->type_flags & EF_CLOAKED)
	{
		float time_frame = Objects[Players[Player_num].objnum].effect_info->cloak_time;
		if (time_frame < HUD_CLOAKEND_TIME)
		{
			alpha = 128 - 127 * FixCos(65536.0f * (time_frame - (int)time_frame));
		}
		else
		{
			alpha = 0;
		}
	}

	DrawGaugeQuad(gauge->pts, HUD_resources.ship_bmp, alpha);
	DrawGaugeQuad(gauge->pts, HUD_resources.ship_bmp, alpha, true);

	if (Players[Player_num].flags & PLAYER_FLAGS_INVULNERABLE)
	{
		float aspect = (float)Game_window_w / Game_window_h;

		float inv_time_frame = (Gametime - (int)Gametime);
		float inv_alpha = (ubyte)(255 * (1.0f - (inv_time_frame / 2.0f)));
		g3Point invpts[4];
		int i;

		// do invulnerability animation.
		for (i = 0; i < 4; i++)
		{
			invpts[i] = gauge->pts[i];
		}

		float amount = (inv_time_frame * GAUGE_INVSHIPRING_DELTA);
		float xamount, yamount;
		if (aspect >= 1.0f)
		{
			xamount = amount / aspect;
			yamount = amount;

		}
		else
		{
			xamount = amount;
			yamount = amount * aspect;
		}
		invpts[0].p3_x -= xamount;
		invpts[0].p3_y += yamount;
		invpts[1].p3_x += xamount;
		invpts[1].p3_y += yamount;
		invpts[2].p3_x += xamount;
		invpts[2].p3_y -= yamount;
		invpts[3].p3_x -= xamount;
		invpts[3].p3_y -= yamount;

		DrawGaugeQuad(invpts, HUD_resources.invpulse_bmp, inv_alpha);
		DrawGaugeQuad(invpts, HUD_resources.invpulse_bmp, inv_alpha, true);
	}
}


//	projects monitor coordinates to screen coordinates
void RotateMonitorPosition(tGauge* gauge)
{
	ASSERT(gauge->monitor);
	ASSERT(gauge->monitor->num_faces == 1);
	int subnum = gauge->monitor - gauge->cockpit->submodel;

	if (gauge->monitor->faces[0].nverts != 4)
		Int3();

	for (int j = 0; j < gauge->monitor->faces[0].nverts; j++)
	{
		//	saved the view coordinates of the monitor if the gauge is moving and/or was just initialized.
		//	if gauge is not moving, then just use these saved points and make sure to mark them as NOT PROJECTED.
		vector vpt;
		vector vert;
		vpt = gauge->monitor->verts[gauge->monitor->faces[0].vertnums[(gauge->first_vert + j) % 4]];
		// JEFFNOTE: Since we are using hardware transforms, we want to update the points all the time
		//if (Render_gauge_moving || gauge->just_init || Render_gauge_reset)
		{
			GetPolyModelPointInWorld(&vert, gauge->cockpit, Render_gauge_pos, Render_gauge_matrix, subnum, Render_normalized_times, &vpt);
			g3_RotatePoint(&gauge->pts[j], &vert);
		}
	}
}


//////////////////////////////////////////////////////////////////////////////
//	Graphic primatives for hud


//	renders a monitor style quad 
void DrawGaugeMonitor(g3Point* pts, int bm, float brightness, float* alphas)
{
	g3Point* pntlist[4];
	g3Point pnts[4];

	if (bm == -1)
		bm = BAD_BITMAP_HANDLE;

	for (int i = 0; i < 4; i++)
	{
		pnts[i] = pts[i];
		pnts[i].p3_flags = PF_UV | PF_RGBA | (pts[i].p3_flags & PF_ORIGPOINT);
		pntlist[i] = &pnts[i];
	}

	pnts[0].p3_u = 0;
	pnts[0].p3_v = 0;
	pnts[0].p3_a = alphas[0];

	pnts[1].p3_u = 1;
	pnts[1].p3_v = 0;
	pnts[1].p3_a = alphas[1];

	pnts[2].p3_u = 1;
	pnts[2].p3_v = 1;
	pnts[2].p3_a = alphas[2];

	pnts[3].p3_u = 0;
	pnts[3].p3_v = 1;
	pnts[3].p3_a = alphas[3];

	rend_SetZBufferState(0);
	rend_SetTextureType(TT_LINEAR);
	rend_SetLighting(LS_NONE);
	rend_SetAlphaValue(brightness * 255);

	rend_SetAlphaType(AT_CONSTANT_VERTEX);

	GaugeQuadBatchKey key = {};
	key.bitmap_handle = bm;
	key.texture_type_value = TT_LINEAR;
	key.lighting = LS_NONE;
	key.color_model_value = CM_MONO;
	key.alpha_type = AT_CONSTANT_VERTEX;
	key.alpha_value = (ubyte)(brightness * 255);
	if (QueueGaugeQuad(key, pnts))
		return;

	FlushGaugeQuadBatches();
	g3_DrawPoly(4, pntlist, bm);
}

void DrawGaugeMonitorSurface(g3Point* pts)
{
	g3Point expanded_pts[4];
	ExpandGaugeQuad(expanded_pts, pts, 1.10f);

	renderer_cockpit_backing_effect effect = {};
	effect.enabled = 1;
	effect.alpha = 0.20f;
	effect.darkness = 0.0f;
	effect.scanlines_enabled = 1;
	effect.scanline_strength = 0.70f;
	effect.scanline_spacing = 3.0f * (float)ConfigNormalizeSupersamplingFactor(Render_preferred_state.supersampling_factor);
	effect.scanline_thickness = 0.50f;
	effect.scanline_phase = Gametime * 14.0f;

	FlushGaugeQuadBatches();
	rend_SetCockpitBackingEffect(&effect);
	DrawGaugeQuadFlatImmediate(expanded_pts, 0.0f, 0.0f, 0.0f,
		(ubyte)(effect.alpha * effect.darkness * 255.0f + 0.5f));
	rend_SetCockpitBackingEffect(NULL);
}


//	renders a square texture onto the screen.
void DrawGaugeQuad(g3Point* pts, int bm, ubyte alpha, bool saturate)
{
	DrawGaugeQuad(pts, bm, 0, 0, 1, 1, alpha, saturate);
}


//	renders a square texture onto the screen.
void DrawGaugeQuad(g3Point* pts, int bm, float u0, float v0, float u1, float v1, ubyte alpha, bool saturate)
{
	g3Point* pntlist[4];
	g3Point pnts[4];

	if (bm == -1)
		bm = BAD_BITMAP_HANDLE;

	for (int i = 0; i < 4; i++)
	{
		pnts[i] = pts[i];
		pnts[i].p3_flags = PF_UV | (pts[i].p3_flags & PF_ORIGPOINT);
		pntlist[i] = &pnts[i];
	}

	pnts[0].p3_u = u0;
	pnts[0].p3_v = v0;

	pnts[1].p3_u = u1;
	pnts[1].p3_v = v0;

	pnts[2].p3_u = u1;
	pnts[2].p3_v = v1;

	pnts[3].p3_u = u0;
	pnts[3].p3_v = v1;


	rend_SetZBufferState(0);
	rend_SetTextureType(TT_LINEAR);
	rend_SetLighting(LS_NONE);
	rend_SetAlphaValue(alpha);

	if (!saturate)
		rend_SetAlphaType(AT_CONSTANT_TEXTURE);
	else
		rend_SetAlphaType(AT_SATURATE_TEXTURE);

	GaugeQuadBatchKey key = {};
	key.bitmap_handle = bm;
	key.texture_type_value = TT_LINEAR;
	key.lighting = LS_NONE;
	key.color_model_value = CM_MONO;
	key.alpha_type = saturate ? AT_SATURATE_TEXTURE : AT_CONSTANT_TEXTURE;
	key.alpha_value = alpha;
	if (QueueGaugeQuad(key, pnts))
		return;

	FlushGaugeQuadBatches();
	g3_DrawPoly(4, pntlist, bm);
}


//	renders a flat poly onto the screen with given color
void DrawGaugeQuadFlat(g3Point* pts, float r, float g, float b, ubyte alpha)
{
	float ar[4] = { r,r,r,r }, ag[4] = { g,g,g,g }, ab[4] = { b,b,b,b };

	DrawGaugeQuadFlat(pts, ar, ag, ab, alpha);
}


//	renders a flat poly onto the screen with 4 colors (for each vertex)
void DrawGaugeQuadFlat(g3Point* pts, float* r, float* g, float* b, ubyte alpha)
{
	g3Point* pntlist[4];
	g3Point pnts[4];

	for (int i = 0; i < 4; i++)
	{
		pnts[i] = pts[i];
		pnts[i].p3_flags = PF_RGBA | (pts[i].p3_flags & PF_ORIGPOINT);
		pntlist[i] = &pnts[i];
	}

	pnts[0].p3_r = r[0];
	pnts[0].p3_g = g[0];
	pnts[0].p3_b = b[0];

	pnts[1].p3_r = r[1];
	pnts[1].p3_g = g[1];
	pnts[1].p3_b = b[1];

	pnts[2].p3_r = r[2];
	pnts[2].p3_g = g[2];
	pnts[2].p3_b = b[2];

	pnts[3].p3_r = r[3];
	pnts[3].p3_g = g[3];
	pnts[3].p3_b = b[3];


	rend_SetZBufferState(0);
	rend_SetLighting(LS_GOURAUD);
	rend_SetTextureType(TT_FLAT);
	rend_SetColorModel(CM_RGB);
	rend_SetAlphaType(AT_CONSTANT);
	rend_SetAlphaValue(alpha);

	GaugeQuadBatchKey key = {};
	key.bitmap_handle = 0;
	key.texture_type_value = TT_FLAT;
	key.lighting = LS_GOURAUD;
	key.color_model_value = CM_RGB;
	key.alpha_type = AT_CONSTANT;
	key.alpha_value = alpha;
	if (QueueGaugeQuad(key, pnts))
		return;

	FlushGaugeQuadBatches();
	g3_DrawPoly(4, pntlist, 0);
}

static void DrawGaugeQuadFlatImmediate(g3Point* pts, float r, float g, float b, ubyte alpha)
{
	float ar[4] = { r,r,r,r }, ag[4] = { g,g,g,g }, ab[4] = { b,b,b,b };
	g3Point* pntlist[4];
	g3Point pnts[4];

	for (int i = 0; i < 4; i++)
	{
		pnts[i] = pts[i];
		pnts[i].p3_flags = PF_RGBA | (pts[i].p3_flags & PF_ORIGPOINT);
		pntlist[i] = &pnts[i];
		pnts[i].p3_r = ar[i];
		pnts[i].p3_g = ag[i];
		pnts[i].p3_b = ab[i];
	}

	rend_SetZBufferState(0);
	rend_SetLighting(LS_GOURAUD);
	rend_SetTextureType(TT_FLAT);
	rend_SetColorModel(CM_RGB);
	rend_SetAlphaType(AT_CONSTANT);
	rend_SetAlphaValue(alpha);

	g3_DrawPoly(4, pntlist, 0);
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

//Returns the coordinates of the specified cockpit monitor
//Parameter:	window - 0 means primary monitor, 1 means secondary
//					x0,y0,x1,y1 - these are filled in with the coordinates of the montiro
//Returns: true if got coords, false if the monitor was animating
bool GetCockpitWindowCoords(int window, int* left, int* top, int* right, int* bot)
{
	if (Render_gauge_moving)
		return 0;

	tGauge* gauge;

	gauge = &Gauge_list[GAUGE_INDEX(window ? STAT_SECONDARYLOAD : STAT_PRIMARYLOAD)];

	if (gauge->monitor) 
	{
#define HUD_RENDER_ZOOM	0.56f

		StartFrame(false);
		g3_StartFrame(&Viewer_object->pos, &Viewer_object->orient, HUD_RENDER_ZOOM);

		for (int i = 0; i < 2; i++)
		{
			gauge->pts[i * 2].p3_flags &= (~PF_PROJECTED);
			g3_ProjectPoint(&gauge->pts[i * 2]);
		}

		*left = gauge->pts[0].p3_sx;
		*top = gauge->pts[0].p3_sy;
		*right = gauge->pts[2].p3_sx + 2;
		*bot = gauge->pts[2].p3_sy + 2;

		g3_EndFrame();
		EndFrame();

		return 1;
	}

	return 0;
}
