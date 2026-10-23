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

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <limits.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <vector>
#include "gameloop.h"
#include "SceneExtraction.h"
#include "SceneCanvas.h"
#include "SceneAutomap.h"
#include "TelCom.h"
#include "SceneModuleContext.h"
#include "SceneFeedback.h"
#include "GameVisualEffects.h"
#include "GameElectricalWeapons.h"
#include "GameSkyWeather.h"
#include "../renderer/SceneLighting.h"
#include "../renderer/SceneAnimation.h"
#include "game.h"
#include "render.h"
#include "descent.h"
#include "slew.h"
#include "mono.h"
#include "doorway.h"
#include "weapon.h"
#include "hlsoundlib.h"
#include "multi.h"
#include "player.h"
#include "damage.h"
#include "ship.h"
#include "objinit.h"
#include "objinfo.h"
#include "gameevent.h"
#include "gametexture.h"
#include "AIMain.h"
#include "ddio.h"
#include "hud.h"
#include "terrain.h"
#include "BOA.h"
#include "lighting.h"
#include "findintersection.h"
#include "bsp.h"
#include "gauges.h"
#include "SmallViews.h"
#include "newui.h"
#include "Inventory.h"
#include "PHYSICS.H"
#include "Controller.h"
#include "controls.h"
#include "gamesequence.h"
#include "cockpit.h"
#include "help.h"
#include "aipath.h"
#include "game2dll.h"
#include "Mission.h"
#include "object_lighting.h"
#include "fireball.h"
#include "weather.h"
#include "stringtable.h"
#include "streamaudio.h"
#include "soundload.h"
#include "sounds.h"
#include "ambient.h"
#include "config.h"
#include "matcen.h"
#include "dedicated_server.h"
#include "D3ForceFeedback.h"
#include "levelgoal.h"
#include "demofile.h"
#include "pilot.h"
#include "rtperformance.h"
#include "d3music.h"
#include "osiris_dll.h"
#include "aiambient.h"
#include "marker.h"
#include "gamecinematics.h"
#include "postrender.h"
#include "debuggraph.h"
#include "gamesave.h"
#include "psrand.h"
#include "spew.h"
#include "grtext.h"
#include "gamefont.h"
#include "renderobject.h"
#include "vibeinterface.h"
#include "gamespy.h"
#include "viseffect.h"
#include "args.h"
#include "scorch.h"

#ifdef EDITOR
#include "editor\d3edit.h"
#endif

extern bool Display_renderer_stats;
void DoDamageEffect(int,void*);
void DoEDrainEffect(int,void*);

//Current zoom factor (this is the tan of 29.25, which is half our FOV of 58.5)
float Render_FOV = Render_FOV_desired;
float Render_zoom = D3_DEFAULT_ZOOM;
float Render_afterburner_visual_factor = 0.0f;

//How long (in seconds) the last frame took
float Frametime = .1f;

//How long (in seconds) since the game started
float Gametime;

int Timedemo_frame = -1;

//How many frames have been renered.
//NOTE: this is a count of 3d frames, not game frames
int FrameCount = 0;

double Get60HzVisualFrame()
{
	return (double)Gametime * 60.0;
}

int Get60HzVisualTick()
{
	return (int)floor(Get60HzVisualFrame());
}

int Get60HzVisualAngle(float units_per_frame, int offset)
{
	return d3scene::visualAngle(Gametime,units_per_frame,offset);
}

uint32_t Get60HzVisualNoise(uint32_t key, uint32_t salt)
{
	return d3scene::visualNoise(Gametime,key,salt);
}

struct visual_60hz_event_clock
{
	int object_handle;
	int last_tick;
	float last_call_time;
	bool initialized;
};

static visual_60hz_event_clock Visual_60hz_event_clocks[MAX_OBJECTS]
	[VIS60_CLOCK_CHANNEL_COUNT] = {};

int Get60HzVisualEventAges(int object_index, int object_handle,
	visual_60hz_clock_channel channel, float* ages, int max_events)
{
	if (!ages || max_events <= 0 || object_index < 0 ||
		object_index >= MAX_OBJECTS || channel < 0 ||
		channel >= VIS60_CLOCK_CHANNEL_COUNT)
		return 0;

	visual_60hz_event_clock& clock =
		Visual_60hz_event_clocks[object_index][channel];
	const int tick = Get60HzVisualTick();
	const float call_gap = Gametime - clock.last_call_time;
	const float continuity_limit = (std::max)(0.05f, Frametime * 2.5f);
	if (!clock.initialized || clock.object_handle != object_handle || tick < clock.last_tick ||
		call_gap < 0.0f || call_gap > continuity_limit)
	{
		clock.initialized = true;
		clock.object_handle = object_handle;
		clock.last_tick = tick - 1;
	}
	clock.last_call_time = Gametime;

	int first_tick = clock.last_tick + 1;
	if (first_tick > tick)
		return 0;
	if (tick - first_tick + 1 > max_events)
		first_tick = tick - max_events + 1;

	int count = 0;
	for (int event_tick = first_tick; event_tick <= tick; ++event_tick)
	{
		const float age = Gametime - (float)event_tick / 60.0f;
		ages[count++] = age > 0.0f ? age : 0.0f;
	}
	clock.last_tick = tick;
	return count;
}

static bool Screenshot_requested = false;
static bool Clean_screenshot_requested = false;
static bool Clean_screenshot_active = false;
extern bool Skip_render_game_frame;
extern bool Menu_interface_mode;

void RequestCleanGameplayScreenshot()
{
	Clean_screenshot_requested = true;
}

bool IsCleanGameplayScreenshotActive()
{
	return Clean_screenshot_active;
}

static void CaptureCleanGameplayScreenshot()
{
	if (!Clean_screenshot_active)
		return;

	DoScreenshot();
	Clean_screenshot_active = false;
}

// Deterministic renderer-comparison capture. The frame index is relative to
// the first fully rendered gameplay frame, not the save's persisted global
// FrameCount. A fixed simulation delta keeps comparison runs on the same game
// state even when their real frame times differ.
struct automated_capture_state
{
	bool initialized;
	bool enabled;
	bool gameplay_frame_active;
	bool capture_pending;
	bool exit_pending;
	int target_frame;
	int series_from_frame;
	int gameplay_frame;
	float fixed_delta;
	bool realtime;
	bool force_forward;
	bool force_tricord;
	bool force_up;
	bool force_heading;
	int forward_until_frame;
	int up_from_frame;
	int heading_until_frame;
	int heading_from_frame;
	bool force_primary_fire;
	int force_primary_until_frame;
	bool place_scorch;
	bool scorch_placed;
	int force_primary_slot;
	bool force_primary_alt;
	bool grant_primary_weapon;
	bool primary_slot_applied;
	bool reload_performed;
	int reload_frame;
	char reload_path[PSPATHNAME_LEN];
	char output_path[PSPATHNAME_LEN];
};

static automated_capture_state Automated_capture = {};

static const char* GetEngineEnvironmentValue(const char* name, const char* legacy_name)
{
	const char* value = getenv(name);
	if ((!value || !value[0]) && legacy_name)
		value = getenv(legacy_name);
	return value;
}

void AutomatedCaptureLog(const char* format, ...)
{
	const char* path = GetEngineEnvironmentValue(ENGINE_ENV_CAPTURE_LOG,
		ENGINE_LEGACY_ENV_CAPTURE_LOG);
	if (!path || !path[0] || !format)
		return;

	FILE* file = fopen(path, "ab");
	if (!file)
		return;

	va_list arguments;
	va_start(arguments, format);
	vfprintf(file, format, arguments);
	va_end(arguments);
	fputc('\n', file);
	fclose(file);
}

static bool AutomatedFramePerfEnabled()
{
	const char* path = GetEngineEnvironmentValue(ENGINE_ENV_FRAME_PERF_LOG,
		ENGINE_LEGACY_ENV_FRAME_PERF_LOG);
	return path && path[0];
}

static void InitRendererFramePacing()
{
	static bool initialized = false;
	if (initialized)
		return;
	initialized = true;

	// Keep one submitted frame outstanding while the CPU prepares the next one.
	// Queue retirement happens immediately before submission, preserving CPU/GPU
	// overlap without allowing driver backpressure to perturb the next simulation.
	// Keep zero available as the driver-managed comparison path.
	int queue_depth = 1;
	const int queue_arg = FindArg("-framequeue");
	if (queue_arg)
	{
		const char* value = GetArg(queue_arg + 1);
		char* end = nullptr;
		const long parsed = value ? strtol(value, &end, 10) : -1;
		if (value && end != value && *end == '\0' && parsed >= 0 && parsed <= 3)
			queue_depth = (int)parsed;
		else
			mprintf((0, "Ignoring invalid -framequeue value; expected 0 through 3.\n"));
	}

	const char* telemetry_env = GetEngineEnvironmentValue(ENGINE_ENV_FRAME_PACING_TELEMETRY,
		ENGINE_LEGACY_ENV_FRAME_PACING_TELEMETRY);
	const bool telemetry_requested = AutomatedFramePerfEnabled() ||
		(telemetry_env && telemetry_env[0] && strcmp(telemetry_env, "0") != 0);
	rend_ConfigureFramePacing(queue_depth, telemetry_requested);
	AutomatedCaptureLog("frame pacing queue=%d gpu_timing=%d telemetry=%d", queue_depth,
		telemetry_requested ? 1 : 0,
		telemetry_requested ? 1 : 0);
	mprintf((0, "Frame pacing: queue depth %d, GPU timing %s.\n",
		queue_depth, telemetry_requested ? "enabled" : "disabled"));
}

static void AutomatedFramePerfLog(int gameplay_frame, double frame_start_interval_ms,
	double pacing_wait_ms, double simulation_ms, double render_ms, double present_ms,
	double tail_ms, double cap_wait_ms, double total_ms, float frame_time_ms,
	double work_ms, double effective_interval_ms,
	double previous_defer_ms, const renderer_frame_pacing_info& pacing_info,
	const tRendererStats& renderer_stats, long long render_thread_cycles)
{
	const char* path = GetEngineEnvironmentValue(ENGINE_ENV_FRAME_PERF_LOG,
		ENGINE_LEGACY_ENV_FRAME_PERF_LOG);
	if (!path || !path[0])
		return;
	static bool wrote_header = false;
	FILE* file = fopen(path, "ab");
	if (!file)
		return;
	if (!wrote_header)
	{
		fputs("gameplay_frame\tframe_start_interval_ms\tpacing_wait_ms\tsimulation_ms\trender_ms\tpresent_ms\ttail_ms\tcap_wait_ms\ttotal_ms\tframetime_ms\twork_ms\teffective_interval_ms\tdefer_ms\tpresent_interval_ms\tswap_call_ms\tqueue_depth\tqueued_frames\tgpu_frame_ms\tgpu_frame_serial\tpolygons\tvertices\ttexture_uploads\trender_thread_cycles\n", file);
		wrote_header = true;
	}
	fprintf(file, "%d\t%.3f\t%.3f\t%.3f\t%.3f\t%.3f\t%.3f\t%.3f\t%.3f\t%.3f\t%.3f\t%.3f\t%.3f\t%.3f\t%.3f\t%d\t%d\t%.3f\t%llu\t%d\t%d\t%d\t%lld\n",
		gameplay_frame, frame_start_interval_ms, pacing_wait_ms, simulation_ms,
		render_ms, present_ms, tail_ms, cap_wait_ms, total_ms, frame_time_ms,
		work_ms, effective_interval_ms, previous_defer_ms,
		pacing_info.latest_present_interval_ms, pacing_info.latest_swap_call_ms,
		pacing_info.configured_queue_depth, pacing_info.queued_frames,
		pacing_info.latest_gpu_frame_ms,
		(unsigned long long)pacing_info.latest_gpu_frame_serial,
		renderer_stats.poly_count, renderer_stats.vert_count,
		renderer_stats.texture_uploads, render_thread_cycles);
	fclose(file);
}

bool AutomatedCaptureSuppressesInput()
{
	return FindArg("-capture-frame") || FindArg("-screenshot-frame");
}

bool AutomatedCaptureForcesForwardInput()
{
	return Automated_capture.gameplay_frame_active &&
		Automated_capture.force_forward && Automated_capture.gameplay_frame <= Automated_capture.forward_until_frame;
}

bool AutomatedCaptureForcesTricordInput()
{
	return Automated_capture.gameplay_frame_active &&
		Automated_capture.force_tricord;
}

bool AutomatedCaptureForcesUpInput()
{
	return Automated_capture.gameplay_frame_active && Automated_capture.force_up &&
		Automated_capture.gameplay_frame >= Automated_capture.up_from_frame;
}

bool AutomatedCaptureForcesHeadingInput()
{
	return Automated_capture.gameplay_frame_active &&
		Automated_capture.force_heading && Automated_capture.gameplay_frame >= Automated_capture.heading_from_frame &&
		Automated_capture.gameplay_frame <= Automated_capture.heading_until_frame;
}

bool AutomatedCaptureForcesPrimaryFireInput()
{
	return Automated_capture.gameplay_frame_active &&
		Automated_capture.force_primary_fire &&
		Automated_capture.gameplay_frame <= Automated_capture.force_primary_until_frame;
}

static int FindAutomatedCaptureArg(const char* primary, const char* alias)
{
	int argument = FindArg((char*)primary);
	return argument ? argument : FindArg((char*)alias);
}

static void PlaceAutomatedCaptureScorch()
{
	if (!Automated_capture.place_scorch || Automated_capture.scorch_placed ||
		!Player_object || Player_num < 0)
		return;

	Automated_capture.scorch_placed = true;
	vector scorch_direction = Player_object->orient.fvec +
		Player_object->orient.rvec * 0.35f -
		Player_object->orient.uvec * 0.20f;
	vm_NormalizeVectorFast(&scorch_direction);
	vector ray_end = Player_object->pos + scorch_direction * 500.0f;
	fvi_query query = {};
	query.p0 = &Player_object->pos;
	query.p1 = &ray_end;
	query.startroom = Player_object->roomnum;
	query.thisobjnum = Players[Player_num].objnum;
	query.flags = FQ_IGNORE_MOVING_OBJECTS | FQ_IGNORE_WEAPONS |
		FQ_IGNORE_RENDER_THROUGH_PORTALS;
	fvi_info hit = {};
	if (fvi_FindIntersection(&query, &hit) != HIT_WALL || hit.num_hits <= 0)
	{
		AutomatedCaptureLog("capture scorch placement missed room geometry");
		return;
	}

	int scorch_texture = -1;
	float scorch_size = 1.0f;
	const int battery_index = Players[Player_num].weapon[PW_PRIMARY].index;
	if (battery_index >= 0 && battery_index < MAX_PLAYER_WEAPONS)
	{
		const otype_wb_info& battery = Ships[Players[Player_num].ship_index].static_wb[battery_index];
		for (int gun = 0; gun < MAX_WB_GUNPOINTS; gun++)
		{
			const int weapon_index = battery.gp_weapon_index[gun];
			if (weapon_index >= 0 && weapon_index < MAX_WEAPONS &&
				Weapons[weapon_index].used && Weapons[weapon_index].scorch_handle >= 0)
			{
				scorch_texture = Weapons[weapon_index].scorch_handle;
				scorch_size = Weapons[weapon_index].scorch_size < 0.5f ?
					0.5f : Weapons[weapon_index].scorch_size;
				break;
			}
		}
	}
	if (scorch_texture < 0)
	{
		for (int weapon_index = 0; weapon_index < MAX_WEAPONS; weapon_index++)
		{
			if (Weapons[weapon_index].used && Weapons[weapon_index].scorch_handle >= 0)
			{
				scorch_texture = Weapons[weapon_index].scorch_handle;
				scorch_size = Weapons[weapon_index].scorch_size < 0.5f ?
					0.5f : Weapons[weapon_index].scorch_size;
				break;
			}
		}
	}
	if (scorch_texture < 0)
	{
		AutomatedCaptureLog("capture scorch placement found no loaded scorch texture");
		return;
	}

	room& hit_room = Rooms[hit.hit_face_room[0]];
	face& hit_face = hit_room.faces[hit.hit_face[0]];
	vector scorch_position = hit.hit_face_pnt[0];
	float nearest_edge = 15.0f;
	vector* edge_start = &hit_room.verts[
		hit_face.face_verts[hit_face.num_verts - 1]];
	for (int vertex = 0; vertex < hit_face.num_verts; vertex++)
	{
		vector* edge_end = &hit_room.verts[hit_face.face_verts[vertex]];
		vector edge_direction;
		vm_GetNormalizedDir(&edge_direction, edge_end, edge_start);
		const vector from_edge = scorch_position - *edge_start;
		const vector closest = *edge_start + edge_direction *
			(from_edge * edge_direction);
		const float distance = vm_VectorDistance(&closest, &scorch_position);
		if (distance < nearest_edge)
			nearest_edge = distance;
		edge_start = edge_end;
	}
	float placed_size = scorch_size < 2.0f ? 2.0f : scorch_size;
	if (placed_size > 4.0f)
		placed_size = 4.0f;
	if (placed_size >= nearest_edge)
		placed_size = nearest_edge * 0.45f;
	AddScorch(hit.hit_face_room[0], hit.hit_face[0], &scorch_position,
		scorch_texture, placed_size);
	AutomatedCaptureLog("capture scorch placed room=%d face=%d texture=%d size=%.3f",
		hit.hit_face_room[0], hit.hit_face[0], scorch_texture,
		placed_size);
}

static void InitAutomatedCapture()
{
	if (Automated_capture.initialized)
		return;
	Automated_capture.initialized = true;
	if (FindArg("-perf-markers"))
		PerfMarkersSetEnabled(true);
	Automated_capture.fixed_delta = 1.0f / 60.0f;
	Automated_capture.realtime = FindArg("-capture-realtime") != 0;
	Automated_capture.force_forward = FindArg("-capture-forward") != 0;
	Automated_capture.force_tricord = FindArg("-capture-tricord") != 0;
	Automated_capture.force_up = FindArg("-capture-up") != 0;
	Automated_capture.force_heading = FindArg("-capture-heading") != 0;
	const auto capture_frame_option = [](const char* name, int fallback) {
		const int arg = FindArg(const_cast<char*>(name));
		if (!arg) return fallback;
		const char* value = GetArg(arg + 1);
		char* end = nullptr;
		const long frame = value ? strtol(value, &end, 10) : -1;
		if (!value || end == value || *end != '\0' || frame < 0 || frame > 1000000)
			Error("Invalid capture frame option %s", name);
		return int(frame);
	};
	Automated_capture.forward_until_frame = capture_frame_option("-capture-forward-until", INT_MAX);
	Automated_capture.up_from_frame = capture_frame_option("-capture-up-from", 0);
	Automated_capture.heading_until_frame = capture_frame_option("-capture-heading-until", INT_MAX);
	Automated_capture.heading_from_frame = capture_frame_option("-capture-heading-from", 0);
	Automated_capture.series_from_frame = capture_frame_option("-capture-series-from", INT_MAX);
	Automated_capture.force_primary_fire =
		FindArg("-capture-fire-primary") != 0;
	Automated_capture.place_scorch = FindArg("-capture-scorch") != 0;
	Automated_capture.force_primary_until_frame = INT_MAX;
	const int primary_until_arg = FindArg("-capture-fire-until");
	if (primary_until_arg)
	{
		const char* value = GetArg(primary_until_arg + 1);
		char* end = nullptr;
		const long frame = value ? strtol(value, &end, 10) : -1;
		if (value && end != value && *end == '\0' && frame >= 0 &&
			frame <= 1000000)
		{
			Automated_capture.force_primary_until_frame = (int)frame;
		}
	}
	Automated_capture.force_primary_slot = -1;
	Automated_capture.reload_frame = -1;
	Automated_capture.force_primary_alt =
		FindArg("-capture-primary-alt") != 0;
	Automated_capture.grant_primary_weapon =
		FindArg("-capture-grant-primary") != 0;
	const int primary_slot_arg = FindArg("-capture-primary-slot");
	if (primary_slot_arg)
	{
		const char* value = GetArg(primary_slot_arg + 1);
		char* end = nullptr;
		const long slot = value ? strtol(value, &end, 10) : -1;
		if (value && end != value && *end == '\0' && slot >= 0 &&
			slot < 5)
		{
			Automated_capture.force_primary_slot = (int)slot;
		}
	}

	const int frame_arg = FindAutomatedCaptureArg("-capture-frame",
		"-screenshot-frame");
	const char* frame_value = frame_arg ? GetArg(frame_arg + 1) : nullptr;
	char* frame_end = nullptr;
	const long target_frame = frame_value ? strtol(frame_value, &frame_end, 10) : -1;
	if (!frame_value || frame_end == frame_value || *frame_end != '\0' ||
		target_frame < 0 || target_frame > 1000000)
	{
		if (frame_arg)
			mprintf((0, "Automated capture disabled: invalid gameplay frame.\n"));
		return;
	}

	const int reload_arg = FindArg("-capture-reloadgame");
	const char* reload_path = reload_arg ? GetArg(reload_arg + 1) : nullptr;
	if (reload_path && reload_path[0])
	{
		if (strlen(reload_path) >= sizeof(Automated_capture.reload_path))
		{
			mprintf((0, "Ignoring -capture-reloadgame: path is too long.\n"));
		}
		else
		{
			strcpy(Automated_capture.reload_path, reload_path);
			Automated_capture.reload_frame = (int)target_frame / 2;
			const int reload_frame_arg = FindArg("-capture-reload-frame");
			if (reload_frame_arg)
			{
				const char* value = GetArg(reload_frame_arg + 1);
				char* end = nullptr;
				const long frame = value ? strtol(value, &end, 10) : -1;
				if (value && end != value && *end == '\0' && frame >= 0 && frame < target_frame)
					Automated_capture.reload_frame = (int)frame;
				else
					mprintf((0, "Ignoring invalid -capture-reload-frame value.\n"));
			}
		}
	}

	const int delta_arg = FindAutomatedCaptureArg("-capture-dt",
		"-screenshot-dt");
	const char* delta_value = delta_arg ? GetArg(delta_arg + 1) : nullptr;
	if (delta_value)
	{
		char* delta_end = nullptr;
		const double fixed_delta = strtod(delta_value, &delta_end);
		if (delta_end != delta_value && *delta_end == '\0' &&
			fixed_delta > 0.0 && fixed_delta <= 1.0)
			Automated_capture.fixed_delta = (float)fixed_delta;
	}

	const int output_arg = FindAutomatedCaptureArg("-capture-output",
		"-screenshot-output");
	const char* output_value = output_arg ? GetArg(output_arg + 1) : nullptr;
	if (output_value && output_value[0])
	{
		if (strlen(output_value) >= sizeof(Automated_capture.output_path))
		{
			mprintf((0, "Automated capture disabled: output path is too long.\n"));
			return;
		}
		strcpy(Automated_capture.output_path, output_value);
	}
	else
	{
		char filename[64];
		snprintf(filename, sizeof(filename), "CaptureFrame%.6ld.png",
			target_frame);
		ddio_MakePath(Automated_capture.output_path, User_directory,
			filename, NULL);
	}

	Automated_capture.enabled = true;
	Automated_capture.target_frame = (int)target_frame;
	AutomatedCaptureLog("armed frame=%d output=%s dt=%.9f realtime=%d forward=%d tricord=%d heading=%d primary=%d",
		Automated_capture.target_frame, Automated_capture.output_path,
		Automated_capture.fixed_delta, Automated_capture.realtime ? 1 : 0,
		Automated_capture.force_forward ? 1 : 0,
		Automated_capture.force_tricord ? 1 : 0,
		Automated_capture.force_heading ? 1 : 0,
		Automated_capture.force_primary_fire ? 1 : 0);
	mprintf((0, "Automated capture armed: gameplay frame %d -> %s (dt %.6f, realtime %d, forward %d, tricord %d, heading %d, primary %d).\n",
		Automated_capture.target_frame, Automated_capture.output_path,
		Automated_capture.fixed_delta, Automated_capture.realtime ? 1 : 0,
		Automated_capture.force_forward ? 1 : 0,
		Automated_capture.force_tricord ? 1 : 0,
		Automated_capture.force_heading ? 1 : 0,
		Automated_capture.force_primary_fire ? 1 : 0));
}

static void BeginAutomatedCaptureFrame()
{
	InitAutomatedCapture();
	Automated_capture.gameplay_frame_active = false;
	Automated_capture.capture_pending = false;
	if (FindArg("-capture-host-mission") &&
		Game_interface_mode == GAME_DLL_INTERFACE)
	{
		// A one-player automated host does not need the interactive DMFC
		// "wait for players" screen.
		Game_interface_mode = GAME_INTERFACE;
	}

	uint32_t gates = 0;
	if (!Automated_capture.enabled) gates |= 1u << 0;
	if (Automated_capture.exit_pending) gates |= 1u << 1;
	if (Game_state != GAMESTATE_LVLPLAYING) gates |= 1u << 2;
	if (Game_interface_mode != GAME_INTERFACE) gates |= 1u << 3;
	if (Menu_interface_mode) gates |= 1u << 4;
	if (Skip_render_game_frame) gates |= 1u << 5;
	if (Dedicated_server) gates |= 1u << 6;

	static uint32_t last_gates = UINT32_MAX;
	if (gates != last_gates)
	{
		AutomatedCaptureLog("gates=0x%02x game_state=%d interface=%d menu=%d skip=%d",
			gates, Game_state, Game_interface_mode,
			Menu_interface_mode ? 1 : 0, Skip_render_game_frame ? 1 : 0);
		last_gates = gates;
	}
	if (gates != 0)
		return;

	Automated_capture.gameplay_frame_active = true;
	if (Automated_capture.gameplay_frame == 0 && FindArg("-capture-no-fog"))
	{
		Detail_settings.Fog_enabled = false;
		AutomatedCaptureLog("capture fog disabled");
	}
	if (Automated_capture.gameplay_frame == 0 && FindArg("-capture-headlight") &&
		Player_num >= 0 && Player_num < MAX_PLAYERS)
	{
		Players[Player_num].flags |= PLAYER_FLAGS_HEADLIGHT;
		AutomatedCaptureLog("capture headlight enabled");
	}
	if (Automated_capture.gameplay_frame == 0 && FindArg("-capture-rear-smallview") && Player_object)
		CreateSmallView(SVW_LEFT,Player_object->handle,SVF_REARVIEW,0.0f,D3_DEFAULT_ZOOM,-1,nullptr);
	if (Automated_capture.gameplay_frame == 0 && FindArg("-capture-main-rear") && Player_object)
		Players[Player_num].flags |= PLAYER_FLAGS_REARVIEW;
	PlaceAutomatedCaptureScorch();
	if (Automated_capture.gameplay_frame == 10 && FindArg("-capture-fire-flare") &&
		Player_object && Player_num >= 0 && Player_num < MAX_PLAYERS)
	{
		FireFlareFromPlayer(Player_object);
		AutomatedCaptureLog("capture flare fired");
	}
	if (!Automated_capture.primary_slot_applied &&
		Automated_capture.force_primary_slot >= 0)
	{
		if (Automated_capture.grant_primary_weapon && Player_num >= 0)
		{
			Players[Player_num].weapon_flags |=
				HAS_FLAG(Automated_capture.force_primary_slot);
			Players[Player_num].energy = INITIAL_ENERGY;
		}
		SelectWeapon(Automated_capture.force_primary_slot);
		if (Automated_capture.force_primary_alt)
			SelectWeapon(Automated_capture.force_primary_slot);
		Automated_capture.primary_slot_applied = true;
	}
	Automated_capture.capture_pending =
		Automated_capture.gameplay_frame == Automated_capture.target_frame ||
		(Automated_capture.gameplay_frame >= Automated_capture.series_from_frame &&
		 Automated_capture.gameplay_frame < Automated_capture.target_frame);
	if (!Automated_capture.realtime)
		Frametime = Automated_capture.fixed_delta;
}

bool AutomatedCapturePending() { return Automated_capture.capture_pending; }
int AutomatedCaptureFrameIndex() { return Automated_capture.gameplay_frame_active ? Automated_capture.gameplay_frame : -1; }

void CaptureAutomatedFrameIfRequested()
{
	if (!Automated_capture.capture_pending)
		return;
	static bool capturing_ui = false;
	if (!capturing_ui && SceneGeometryInspection() && FindArg("-scene-automap-telcom-probe")) {
		// Complete the gameplay frame before entering the real modal presenter.
		// TelCom captures its own fully composed frame before its swap.
		rend_Flip();
		capturing_ui = true;
		TelComShow(TS_MAP,true,false);
		capturing_ui = false;
		if (Automated_capture.capture_pending) Error("Automap TelCom fixture did not capture its presentation");
		return;
	}

	Automated_capture.capture_pending = false;
	const bool final_capture = Automated_capture.gameplay_frame == Automated_capture.target_frame;
	char series_path[PSPATHNAME_LEN];
	const char* output_path = Automated_capture.output_path;
	if (!final_capture) {
		const int length = snprintf(series_path,sizeof(series_path),"%s.frame%06d.png",
			output_path,Automated_capture.gameplay_frame);
		if (length < 0 || size_t(length) >= sizeof(series_path)) Error("Capture series path is too long");
		output_path = series_path;
	}
	StopTime();
	const int saved = rend_SaveScreenshotPNG(output_path);
	StartTime();
	mprintf((0, "Automated capture %s: gameplay frame %d -> %s.\n",
		saved ? "saved" : "failed", Automated_capture.gameplay_frame,
		output_path));
	AutomatedCaptureLog("capture %s frame=%d output=%s",
		saved ? "saved" : "failed", Automated_capture.gameplay_frame,
		output_path);
	if (Player_object && Player_object->movement_type == MT_PHYSICS)
	{
		const physics_info& physics = Player_object->mtype.phys_info;
		AutomatedCaptureLog(
			"player rotation rotvel=%.9f,%.9f,%.9f rotthrust=%.9f,%.9f,%.9f mass=%.9f rotdrag=%.9f full_rotthrust=%.9f turn_scalar=%.9f",
			physics.rotvel.x, physics.rotvel.y, physics.rotvel.z,
			physics.rotthrust.x, physics.rotthrust.y, physics.rotthrust.z,
			physics.mass, physics.rotdrag, physics.full_rotthrust,
			Players[Player_num].turn_scalar);
	}
	const int save_arg = FindArg("-capture-save-output");
	const char* save_path = save_arg ? GetArg(save_arg + 1) : nullptr;
	if (final_capture && save_path && save_path[0])
	{
		const bool save_ok = SaveGameState(save_path, "Automated capture");
		AutomatedCaptureLog("save %s output=%s", save_ok ? "saved" : "failed",
			save_path);
	}
	Automated_capture.exit_pending = final_capture;
}

static void EndAutomatedCaptureFrame()
{
	if (Automated_capture.gameplay_frame_active &&
		!Automated_capture.exit_pending)
	{
		++Automated_capture.gameplay_frame;
		if (!Automated_capture.reload_performed &&
			Automated_capture.reload_path[0] &&
			Automated_capture.gameplay_frame >= Automated_capture.reload_frame)
		{
			Automated_capture.reload_performed = true;
			if (SetLoadGamePath(Automated_capture.reload_path))
			{
				AutomatedCaptureLog("scheduled reload frame=%d path=%s",
					Automated_capture.gameplay_frame, Automated_capture.reload_path);
				SetGameState(GAMESTATE_LOADGAME);
			}
			else
			{
				AutomatedCaptureLog("scheduled reload rejected path=%s",
					Automated_capture.reload_path);
			}
		}
	}
	if (Automated_capture.exit_pending)
	{
		Automated_capture.exit_pending = false;
		Automated_capture.enabled = false;
		SetFunctionMode(QUIT_MODE);
	}
}

bool HUD_disabled = 0;
#ifdef _DEBUG
int DoAI = 1;
bool System_locked = false;
int Stream_type = 0;
#else
int Stream_type = 0;
#define DoAI 1
#endif

bool Tracking_FVI = false;

//Set this to force window clear
int Clear_screen = 0;

//	determines if game is paused.
bool Game_paused = false;

// Used for limiting the framerate
double Min_allowed_frametime = 0;
static int Frame_limit_fps = 0;
static bool Frame_limit_command_line_override = false;

void SetFrameLimitFps(int fps)
{
	Frame_limit_fps = fps > 0 ? fps : 0;
	Min_allowed_frametime = Frame_limit_fps > 0 ? 1.0 / (double)Frame_limit_fps : 0.0;
}

int GetFrameLimitFps()
{
	return Frame_limit_fps;
}

void SetFrameLimitCommandLineOverride(bool enabled)
{
	Frame_limit_command_line_override = enabled;
}

bool FrameLimitHasCommandLineOverride()
{
	return Frame_limit_command_line_override;
}

// determines if we're rendering the main view
bool Rendering_main_view = false;
bool Skip_render_game_frame = false;
bool Menu_interface_mode = false;

bool RendererShouldCountMsaaTransitionFrame()
{
	return Game_interface_mode == GAME_INTERFACE && !Menu_interface_mode;
}

const int PERF_MARKER_NAME_LEN = 96;

struct PerfMarkerRecord
{
	char name[PERF_MARKER_NAME_LEN];
	double start_time;
	double duration;
	int depth;
};

struct PerfMarkerStackEntry
{
	const char* name;
	double start_time;
	int depth;
};

bool Perf_markers_enabled = false;
static bool Perf_markers_gpu_only = false;
static FILE* Perf_marker_file = nullptr;
static bool Perf_marker_frame_active = false;
static int Perf_marker_frame_number = 0;
static int Perf_marker_forced_frames = 0;
static double Perf_marker_capture_start = 0.0;
static double Perf_marker_frame_start = 0.0;
static bool Perf_marker_current_frame_forced = false;
static bool Perf_marker_current_frame_requested = false;
static char Perf_marker_force_reason[PERF_MARKER_NAME_LEN] = {};
static std::vector<PerfMarkerRecord> Perf_marker_records;
static std::vector<PerfMarkerStackEntry> Perf_marker_stack;

static bool PerfMarkersShouldCaptureFrame()
{
	const bool gameplay_frame = !Dedicated_server &&
		!Skip_render_game_frame &&
		!Game_paused &&
		!Menu_interface_mode &&
		Game_interface_mode == GAME_INTERFACE;
	if (gameplay_frame)
		return true;

	return !Dedicated_server &&
		!Skip_render_game_frame &&
		Perf_marker_forced_frames > 0;
}

static void PerfMarkersCopyName(char* dest, const char* source)
{
	if (!source)
		source = "unnamed";
	strncpy(dest, source, PERF_MARKER_NAME_LEN - 1);
	dest[PERF_MARKER_NAME_LEN - 1] = '\0';
}

static void PerfMarkersCloseFile()
{
	if (Perf_marker_file)
	{
		fflush(Perf_marker_file);
		fclose(Perf_marker_file);
		Perf_marker_file = nullptr;
	}
}

void PerfMarkersSetGpuOnly(bool gpu_only)
{
	Perf_markers_gpu_only = gpu_only;
}

void PerfMarkersSetEnabled(bool enabled)
{
	if (enabled == Perf_markers_enabled)
		return;

	if (!enabled)
	{
		if (Perf_marker_frame_active)
			PerfMarkersEndFrame();
		Perf_markers_enabled = false;
		PerfMarkersCloseFile();
		return;
	}

	char marker_path[_MAX_PATH];
	ddio_MakePath(marker_path, Base_directory, "perfmarkers.txt", nullptr);
	Perf_marker_file = fopen(marker_path, "w");
	if (!Perf_marker_file)
	{
		mprintf((0, "Unable to open perf marker log: %s\n", marker_path));
		Perf_markers_enabled = false;
		return;
	}

	Perf_marker_records.clear();
	Perf_marker_stack.clear();
	Perf_marker_records.reserve(512);
	Perf_marker_stack.reserve(32);
	Perf_marker_frame_active = false;
	Perf_marker_frame_number = 0;
	Perf_marker_forced_frames = 0;
	Perf_marker_capture_start = timer_GetTime64();
	Perf_marker_frame_start = 0.0;
	Perf_marker_current_frame_forced = false;
	Perf_marker_current_frame_requested = false;
	Perf_marker_force_reason[0] = '\0';
	Perf_markers_enabled = true;

	fprintf(Perf_marker_file, "# " ENGINE_NAME " perf markers\n");
	fprintf(Perf_marker_file, "# frame\tstart_ms\tduration_ms\tdepth\tmarker\n");
}

void PerfMarkersCaptureNextFrames(int frame_count, const char* reason)
{
	if (!Perf_markers_enabled || frame_count <= 0)
		return;

	if (Perf_marker_forced_frames < frame_count)
		Perf_marker_forced_frames = frame_count;
	PerfMarkersCopyName(Perf_marker_force_reason, reason ? reason : "forced");
}

void PerfMarkersBeginFrame()
{
	if (!Perf_markers_enabled || !Perf_marker_file)
		return;
	if (!PerfMarkersShouldCaptureFrame())
		return;

	Perf_marker_frame_active = true;
	Perf_marker_frame_start = timer_GetTime64();
	Perf_marker_current_frame_requested = Perf_marker_forced_frames > 0;
	Perf_marker_current_frame_forced = Perf_marker_current_frame_requested &&
		(Game_interface_mode != GAME_INTERFACE || Menu_interface_mode || Game_paused);
	Perf_marker_records.clear();
	Perf_marker_stack.clear();
	PolymodelPerfReset();
	RenderObjectPerfReset();
	Perf_marker_frame_number++;
}

void PerfMarkersEndFrame()
{
	if (!Perf_markers_enabled || !Perf_marker_file || !Perf_marker_frame_active)
		return;

	const double frame_end = timer_GetTime64();
	if (!PerfMarkersShouldCaptureFrame())
	{
		Perf_marker_records.clear();
		Perf_marker_stack.clear();
		Perf_marker_frame_active = false;
		Perf_marker_current_frame_forced = false;
		Perf_marker_current_frame_requested = false;
		return;
	}

	PolymodelPerfFlush();
	RenderObjectPerfFlush();
	PerfMarkersRecordDuration("Frame.Total", Perf_marker_frame_start, frame_end - Perf_marker_frame_start);
	if (!Perf_marker_stack.empty())
	{
		char open_marker_count[64];
		snprintf(open_marker_count, sizeof(open_marker_count), "Frame.Count.OpenMarkers=%d", (int)Perf_marker_stack.size());
		PerfMarkersRecordDuration(open_marker_count, frame_end, 0.0);
	}
	char marker_record_count[64];
	snprintf(marker_record_count, sizeof(marker_record_count), "Frame.Count.MarkerRecords=%d", (int)Perf_marker_records.size());
	PerfMarkersRecordDuration(marker_record_count, frame_end, 0.0);

	fprintf(Perf_marker_file,
		"FRAME\t%d\t%.3f\t%.3f\t%.3f\tgametime=%.3f\tframetime=%.3f\tcapture=%s\treason=%s\tgame_if=%d\tmenu_if=%d\n",
		Perf_marker_frame_number,
		(Perf_marker_frame_start - Perf_marker_capture_start) * 1000.0,
		(frame_end - Perf_marker_frame_start) * 1000.0,
		0.0,
		Gametime,
		Frametime,
		Perf_marker_current_frame_forced ? "forced" : "gameplay",
		Perf_marker_current_frame_requested ? Perf_marker_force_reason : "",
		Game_interface_mode,
		Menu_interface_mode ? 1 : 0);

	for (const PerfMarkerRecord& record : Perf_marker_records)
	{
		fprintf(Perf_marker_file, "%d\t%.3f\t%.3f\t%d\t%s\n",
			Perf_marker_frame_number,
			(record.start_time - Perf_marker_frame_start) * 1000.0,
			record.duration * 1000.0,
			record.depth,
			record.name);
	}

	if ((Perf_marker_frame_number % 120) == 0)
		fflush(Perf_marker_file);

	if (Perf_marker_current_frame_requested && Perf_marker_forced_frames > 0)
		Perf_marker_forced_frames--;
	Perf_marker_frame_active = false;
	Perf_marker_current_frame_forced = false;
	Perf_marker_current_frame_requested = false;
}

void PerfMarkersBegin(const char* marker_name)
{
	if (!Perf_markers_enabled || !Perf_marker_file || !Perf_marker_frame_active)
		return;
	if (Perf_markers_gpu_only)
		return;

	PerfMarkerStackEntry entry = {};
	entry.name = marker_name;
	entry.start_time = timer_GetTime64();
	entry.depth = (int)Perf_marker_stack.size();
	Perf_marker_stack.push_back(entry);
}

void PerfMarkersEnd(const char* marker_name)
{
	(void)marker_name;
	if (!Perf_markers_enabled || !Perf_marker_file || !Perf_marker_frame_active || Perf_marker_stack.empty())
		return;

	const double end_time = timer_GetTime64();
	const PerfMarkerStackEntry entry = Perf_marker_stack.back();
	Perf_marker_stack.pop_back();

	PerfMarkerRecord record = {};
	PerfMarkersCopyName(record.name, entry.name);
	record.start_time = entry.start_time;
	record.duration = end_time - entry.start_time;
	record.depth = entry.depth;
	Perf_marker_records.push_back(record);
}

double PerfMarkersNow()
{
	return timer_GetTime64();
}

void PerfMarkersRecordDuration(const char* marker_name, double start_time, double duration)
{
	if (!Perf_markers_enabled || !Perf_marker_file || !Perf_marker_frame_active)
		return;
	if (Perf_markers_gpu_only &&
		(!marker_name || strncmp(marker_name, "GPU.", 4) != 0))
	{
		return;
	}

	PerfMarkerRecord record = {};
	PerfMarkersCopyName(record.name, marker_name);
	record.start_time = start_time;
	record.duration = duration;
	record.depth = (int)Perf_marker_stack.size();
	Perf_marker_records.push_back(record);
}

//#ifdef GAMEGAUGE
int frames_one_second = 0;
int min_one_second = 0x7fffffff;
int max_one_second = 0;
float gamegauge_start_time;
int gamegauge_total_frame_seconds = 0;
int gamegauge_total_frames = 0;
float gamegauge_total_time = 0.0;

short gamegauge_fpslog[GAMEGAUGE_MAX_LOG];

bool Game_gauge_do_time_test = false;
char Game_gauge_usefile[_MAX_PATH] = "gg.dem";
//#endif

double last_timer = 0;

// contains information for the music system.
tMusicSeqInfo Game_music_info;

//Defines for changing the window size
#define WINDOW_W_DELTA  ((Max_window_w / 16)&~1)
#define WINDOW_H_DELTA  ((Max_window_h / 16)&~1)

#define WINDOW_MIN_W    ((Max_window_w * 10) / 22)
#define WINDOW_MIN_H    ((Max_window_h * 10) / 22)

//	Functions
void ApplyShadowsToRooms();
void StartGameMenu();
void EndGameMenu();
void ProcessGuidebotKeys(int key);

//Make the 3D window larger
void GrowWindow()
{
	if (GetHUDMode() == HUD_LETTERBOX)
		return;

	Game_window_w += WINDOW_W_DELTA;
	Game_window_h += WINDOW_H_DELTA;

	if (Game_window_h > Max_window_h)
		Game_window_h = Max_window_h;

	if (Game_window_w > Max_window_w)
		Game_window_w = Max_window_w;

	Game_window_x = (Max_window_w - Game_window_w) / 2;
	Game_window_y = (Max_window_h - Game_window_h) / 2;

	Current_pilot.set_hud_data(NULL, NULL, NULL, &Game_window_w, &Game_window_h);

	Clear_screen = 4;

	ResizeCockpit();
}

//Make the 3D window smaller
void ShrinkWindow()
{
	if (GetHUDMode() == HUD_LETTERBOX)
		return;

	if (Game_window_w > WINDOW_MIN_W) {

		Game_window_w -= WINDOW_W_DELTA;
		Game_window_h -= WINDOW_H_DELTA;

		if (Game_window_w < WINDOW_MIN_W)
			Game_window_w = WINDOW_MIN_W;

		if (Game_window_h < WINDOW_MIN_H)
			Game_window_h = WINDOW_MIN_H;

		Game_window_x = (Max_window_w - Game_window_w) / 2;
		Game_window_y = (Max_window_h - Game_window_h) / 2;

		Clear_screen = 4;

		ResizeCockpit();

		Current_pilot.set_hud_data(NULL, NULL, NULL, &Game_window_w, &Game_window_h);
	}
}

//Data and code for camera views

#define CV_NONE		0		//View not active
#define CV_REARVIEW	1		//Rear view
#define CV_MARKER1	2		//Marker view
#define CV_MARKER2	3		//Marker view
#define CV_MARKER3	4		//Marker view
#define CV_MARKER4	5		//Marker view
#define CV_MARKER5	6		//Marker view
#define CV_MARKER6	7		//Marker view
#define CV_MARKER7	8		//Last marker view
#define CV_MARKER8	9		//Last marker view
#define CV_GUIDEBOT	10		//Guide-Bot
//#define CV_COOP		3		//View from a coop player

#define NUM_CAMERA_VIEWS	3

int Camera_view_mode[NUM_CAMERA_VIEWS];

//Selects the next mode for this view
void SelectNextCameraView(int window)
{
	int current = Camera_view_mode[window];

	//If the window has gone away because there's no object, then the current state is none
	if (GetSmallViewer(window) == OBJECT_HANDLE_NONE)
		current = CV_NONE;

	switch (current) {
	case CV_NONE:
		Camera_view_mode[window] = CV_REARVIEW;
		CreateSmallView(window, Player_object->handle, SVF_REARVIEW, 0.0, D3_DEFAULT_ZOOM, -1, TXT_VIEW_REAR);
		if (window == 0) Current_pilot.lrearview_enabled = true;		// set to true.
		else if (window == 2) Current_pilot.rrearview_enabled = true;
		break;

	case CV_REARVIEW: {
		object* objp = ObjGet(Buddy_handle[Player_num]);
		if (window == 0) Current_pilot.lrearview_enabled = false;		// leaving rearview
		else if (window == 2) Current_pilot.rrearview_enabled = false;

		if (objp && objp->type == OBJ_ROBOT) {		//Not hidden
			Camera_view_mode[window] = CV_GUIDEBOT;
			CreateSmallView(window, objp->handle, 0, 0.0, D3_DEFAULT_ZOOM, -1, TXT_VIEW_GUIDEBOT);
			break;
		}
		//If no GB, then fall into next case
	}
	case CV_GUIDEBOT:
	case CV_MARKER1:
	case CV_MARKER2:
	case CV_MARKER3:
	case CV_MARKER4:
	case CV_MARKER5:
	case CV_MARKER6:
	case CV_MARKER7:
	case CV_MARKER8: {
		if (window == 0) Current_pilot.lrearview_enabled = false;		// leaving rearview
		else if (window == 2) Current_pilot.rrearview_enabled = false;

		if (Players[Player_num].num_markers > 0) {
			int num = (current == CV_GUIDEBOT) ? 0 : (current - CV_MARKER1) + 1;
			int max = (Game_mode & GM_MULTI) ? 2 : 8;		//Why no symbolic constants?  Ask Jason.
			int id = Player_num * 2 + num;

			if (num < max) {
				int i;

				// Search for the marker
				for (i = 0; i <= Highest_object_index; i++) {
					if ((Objects[i].type == OBJ_MARKER) && (Objects[i].id == id))
						break;
				}

				if (i <= Highest_object_index) {
					char buf[25];
					sprintf(buf, TXT_VIEW_MARKER, num + 1);

					CreateSmallView(window, Objects[i].handle, 0, 0.0, D3_DEFAULT_ZOOM, -1, buf);
					Camera_view_mode[window] = CV_MARKER1 + num;

					break;
				}
			}
		}
		//If couldn't find marker or have cycled though all, close window
		Camera_view_mode[window] = CV_NONE;
		CloseSmallView(window);
		break;
	}
	default:
		Int3();	//all cases should be handled
	}
}

//Turn off all camera views
//If total reset is true, set all views to none, otherwise kill object view but keep rear views.
void InitCameraViews(bool total_reset)
{
	for (int i = 0; i < NUM_CAMERA_VIEWS; i++) {
		if (total_reset || (Camera_view_mode[i] != CV_REARVIEW)) {
			Camera_view_mode[i] = CV_NONE;
		}
		else {	//is rear view, so keep it
			Camera_view_mode[i] = CV_REARVIEW;
			CreateSmallView(i, Player_object->handle, SVF_REARVIEW, 0.0, D3_DEFAULT_ZOOM, -1, TXT_VIEW_REAR);
		}
	}
}


void RestoreCameraRearviews()
{
	if (Camera_view_mode[0] != CV_REARVIEW) {
		if (Current_pilot.lrearview_enabled) {
			Camera_view_mode[0] = CV_REARVIEW;
			CreateSmallView(0, Player_object->handle, SVF_REARVIEW, 0.0, D3_DEFAULT_ZOOM, -1, TXT_VIEW_REAR);
		}
	}
	if (Camera_view_mode[2] != CV_REARVIEW) {
		if (Current_pilot.rrearview_enabled) {
			Camera_view_mode[2] = CV_REARVIEW;
			CreateSmallView(2, Player_object->handle, SVF_REARVIEW, 0.0, D3_DEFAULT_ZOOM, -1, TXT_VIEW_REAR);
		}
	}
}

void ProcessButtons()
{
	//If dead, any key not handled above will eand the death sequence
	// this shouldn't be called in ReadPlayerControls since the player is DEAD!
	if (Players[Player_num].flags & PLAYER_FLAGS_DEAD) {
		int x, y;
		PollControls();
		if (Controller->get_joy_raw_values(&x, &y) || Controller->get_mouse_raw_values(&x, &y)) {
			// death.
			mprintf((0, "here?"));

			if (Total_time_dead < DEATH_RESPAWN_TIME)
				return;

			mprintf((0, "Respawning joystick death.  Death time=%f\n", Total_time_dead));

			if (Game_mode & GM_MULTI)
				MultiSendEndPlayerDeath();		//couldn't this be called from withing EndPlayerDeath()?
			else
				EndPlayerDeath(Player_num);
			// Clear keyboard
			ResetPersistentHUDMessage();
			ddio_KeyFlush();
			if (Controller) {
				Controller->flush();
			}
		}
		return;
	}
}


#define GBC_FIND_ACTIVE_GOAL_0		3  // Find next primary
#define GBC_FIND_SPEW				19  // Find Spew
#define GBC_FIND_POWERUP			20  // Find Powerup
#define GBC_FIND_ENERGY_CENTER		21  // Find energy 
#define GBC_ESCORT_SHIP				35  // Escort Ship
#define GBC_EXTINGUISH				36  // Use Extinguisher
#define GBC_GO_WINGNUT				37  // Use Wingnut Powerup
#define GBC_RTYPE					39  // Use Gaurdian Powerup
#define GBC_ANTI_VIRUS				40  // Use Anti-virus Powerup
#define GBC_RETURN_TO_SHIP			43  // Return to ship
extern int Buddy_handle[MAX_PLAYERS];
extern void MultiSendGuidebotMenuSelection(gb_com*);
// Handles guidebot shortcut keys
void ProcessGuidebotKeys(int key)
{
	if (!(key & KEY_SHIFTED))
		return;
	key &= ~KEY_SHIFTED;

	//check to make sure our Guidebot is out of the ship
	object* buddy = ObjGet(Buddy_handle[Player_num]);
	if (!buddy || buddy->type != OBJ_ROBOT)
	{
		//not out of the ship
		mprintf((0, "Guidebot: mrmph mmrump..mrmph...LET ME OUT OF YOUR SHIP!\n"));
		return;
	}
	int command_id = -1;

	switch (key)
	{
	case KEY_1:
		command_id = GBC_FIND_ACTIVE_GOAL_0;
		break;
	case KEY_2:
		command_id = GBC_FIND_SPEW;
		break;
	case KEY_3:
		command_id = GBC_FIND_POWERUP;
		break;
	case KEY_4:
		command_id = GBC_FIND_ENERGY_CENTER;
		break;
	case KEY_5:
		command_id = GBC_ESCORT_SHIP;
		break;
	case KEY_6:
		command_id = GBC_EXTINGUISH;
		break;
	case KEY_7:
		command_id = GBC_GO_WINGNUT;
		break;
	case KEY_8:
		command_id = GBC_RTYPE;
		break;
	case KEY_9:
		command_id = GBC_ANTI_VIRUS;
		break;
	case KEY_0:
		command_id = GBC_RETURN_TO_SHIP;
		break;
	default:
		//nope
		return;
		break;
	}

	// Process the command
	gb_com command;
	command.action = COM_DO_ACTION;
	command.index = command_id;
	command.ptr = NULL;

	if ((!(Game_mode & GM_MULTI)) || (Netgame.local_role != LR_CLIENT))
	{
		AINotify(buddy, AIN_USER_DEFINED, (void*)&command);
	}
	else
	{
		MultiSendGuidebotMenuSelection(&command);
	}
}

//Deals with a normal (non-debug) key.  Once the key is handled, this function returns

void ProcessNormalKey(int key)
{
	if (IsAltEnterFullscreenEnabled() && IsAltEnterFullscreenKey(key))
	{
		ToggleFullscreenMode();
		return;
	}
	if (IsAltF4QuitKey(key) && ShouldConfirmAltF4QuitInGame())
	{
		RequestAltF4QuitConfirmation();
		return;
	}
	if (IsAltPrintScreenKey(key))
	{
		mprintf((0, "Doing clean gameplay screenshot!\n"));
		if (IsCtrlPrintScreenKey(key))
			RequestDoubleResolutionScreenshot();
		RequestCleanGameplayScreenshot();
		return;
	}
	if (IsCtrlPrintScreenKey(key))
	{
		mprintf((0, "Doing double-resolution screenshot!\n"));
		RequestDoubleResolutionScreenshot();
		Screenshot_requested = true;
		return;
	}

	//First do keys that work normally even when dead
	switch (key) {

		//ingnore the modifier keys
	case KEY_LALT:
	case KEY_RALT:
	case KEY_LSHIFT:
	case KEY_RSHIFT:
	case KEY_LCTRL:
	case KEY_RCTRL:
		return;

	case KEY_ESC:
		Game_interface_mode = GAME_EXIT_CONFIRM;
		return;

	case KEY_PAUSE:
		Game_interface_mode = GAME_PAUSE_INTERFACE;
		return;

	case KEY_F1:
		Game_interface_mode = GAME_HELP_INTERFACE;
		return;

	case KEY_F2:
		Game_interface_mode = GAME_OPTIONS_INTERFACE;
		return;

	case KEY_F8:
		if (Game_mode & GM_MULTI)
		{
			StartHUDInputMessage();
			return;
		}
		break;

	case KEY_SHIFTED + KEY_F8:
		ToggleGameMessageConsole();
		Clear_screen = 4;
		break;

	case KEY_SHIFTED + KEY_F9:
		ToggleHUDMessageConsole();
		Clear_screen = 4;				// clears screen.
		break;

		//#ifndef DEMO		
	case KEY_F5:		Game_interface_mode = GAME_TOGGLE_DEMO;			return;
	case KEY_ALTED + KEY_F5:		DemoAbort(true);						return;
		//#endif
				//case KEY_SHIFTED+KEY_LAPOSTRO:	Game_interface_mode = GAME_TELCOM_CARGO;return;
				//case KEY_ALTED+KEY_LAPOSTRO: Game_interface_mode = GAME_TELCOM_BRIEFINGS; return;
	case KEY_SHIFTED + KEY_TAB: Game_interface_mode = GAME_TELCOM_BRIEFINGS; return;
	case KEY_ALTED + KEY_F3:
		if (!Cinematic_inuse)
		{
			Game_interface_mode = GAME_LOAD_INTERFACE;
			return;
		}break;

	case KEY_CTRLED + KEY_F8:
		if (Game_mode & GM_MULTI)
		{
			StartTeamHUDInputMessage();
			return;
		}
		break;
	case KEY_CTRLED + KEY_1:
	case KEY_CTRLED + KEY_2:
	case KEY_CTRLED + KEY_3:
	case KEY_CTRLED + KEY_4:
	case KEY_CTRLED + KEY_5:
	case KEY_CTRLED + KEY_6:
	case KEY_CTRLED + KEY_7:
	case KEY_CTRLED + KEY_8:
	{
		int index = key & ~KEY_CTRLED;
		index = index - KEY_1;

		if (Game_mode & GM_MULTI)
		{
			char str[80];

			mprintf((0, "Printing message %d!\n", index));

			int save_do = Doing_input_message;
			int save_len = HudInputMessageLen;
			strcpy(str, HudInputMessage);

			//strcpy (HudInputMessage,Current_pilot.taunts[index]);
			//decode the text macro by sending it off to the DLL
			//for processesing
			DLLInfo.input_string = Current_pilot.taunts[index];
			DLLInfo.special_data = (ubyte*)HudInputMessage;
			DLLInfo.iParam = MAX_HUD_INPUT_LEN;
			CallGameDLL(EVT_CLIENT_DECODETEXTMACRO, &DLLInfo);

			Doing_input_message = 1;
			SendOffHUDInputMessage();
			strcpy(HudInputMessage, str);
			HudInputMessageLen = save_len;
			Doing_input_message = save_do;
		}

		break;
	}

	case KEY_PRINT_SCREEN:
	case KEY_SHIFTED + KEY_PRINT_SCREEN:
		mprintf((0, "Doing screenshot!\n"));
		Screenshot_requested = true;
		return;

	case KEY_SHIFTED + KEY_MINUS:
	case KEY_MINUS:					ShrinkWindow(); return;

	case KEY_SHIFTED + KEY_EQUAL:
	case KEY_EQUAL:					GrowWindow(); return;
	case KEY_SPACEBAR:
		if (Players[Player_num].flags & PLAYER_FLAGS_DEAD)
		{

			if (Total_time_dead < DEATH_RESPAWN_TIME)
				return;

			if (Game_mode & GM_MULTI)
				MultiSendEndPlayerDeath();		//couldn't this be called from withing EndPlayerDeath()?
			else
				EndPlayerDeath(Player_num);

			// Clear keyboard
			ResetPersistentHUDMessage();
			ddio_KeyFlush();
			if (Controller)
				Controller->flush();

			return;
		}

	}
	if (Demo_flags == DF_PLAYBACK)
	{
		switch (key)
		{
		case KEY_C:
		{
			// goto next player
			int viewer_objnum = Viewer_object - Objects;
			int found = 0;

			for (int i = viewer_objnum + 1; i <= Highest_object_index && !found; i++, i %= (Highest_object_index + 1))
			{
				if (Objects[i].type == OBJ_CAMERA)
				{
					found = 1;
					Viewer_object = &Objects[i];
				}
			}
		}
		break;
		case KEY_P:
		{
			// goto next player
			int viewer_objnum = Viewer_object - Objects;
			int found = 0;

			for (int i = viewer_objnum + 1; i <= Highest_object_index && !found; i++, i %= (Highest_object_index + 1))
			{
				if (Objects[i].type == OBJ_PLAYER)
				{
					found = 1;
					Viewer_object = &Objects[i];
				}
			}
		}
		break;
		case KEY_R:
		{
			// goto next robot
			int viewer_objnum = Viewer_object - Objects;
			int found = 0;

			for (int i = viewer_objnum + 1; i <= Highest_object_index && !found; i++, i %= (Highest_object_index + 1))
			{
				if (Objects[i].type == OBJ_ROBOT)
				{
					found = 1;
					Viewer_object = &Objects[i];
				}
			}
		}
		break;
		case KEY_SPACEBAR:
			//Reset view
			Viewer_object = Player_object;
			break;
		case KEY_DOWN:
			Game_paused = true;
			Demo_paused = true;
			break;
		case KEY_RIGHT:
			Demo_do_one_frame = true;
			break;
		case KEY_CTRLED + KEY_LEFT:
		{
			char sztmp[_MAX_PATH * 2];
			strcpy(sztmp, Demo_fname);
			DemoAbort();
			Game_interface_mode = GAME_DEMO_LOOP;
			Demo_restart = true;
			strcpy(Demo_fname, sztmp);
		}
		break;
		case KEY_UP:
			Game_paused = false;
			Demo_paused = false;
			break;
		default:
			break;
		}

	}

	//If dead, any key not handled above will eand the death sequence
	if (Players[Player_num].flags & PLAYER_FLAGS_DEAD)
	{
		return;
	}

	//If dying, only the keys handled above do anything, so bail
	if (Players[Player_num].flags & PLAYER_FLAGS_DYING)
		return;

	//These keys only work when not dying or dead
	ProcessGuidebotKeys(key);

	switch (key)
	{
	case KEY_SHIFTED + KEY_F1:		SelectNextCameraView(0);		return;
	case KEY_SHIFTED + KEY_F2:		SelectNextCameraView(2);		return;
		//case KEY_SHIFTED+KEY_F3:		SelectNextCameraView(2);		return;

	case KEY_1:			SelectWeapon(0);	return;
	case KEY_2:			SelectWeapon(1);	return;
	case KEY_3:			SelectWeapon(2);	return;
	case KEY_4:			SelectWeapon(3);	return;
	case KEY_5:			SelectWeapon(4);	return;
	case KEY_6:			SelectWeapon(5);	return;
	case KEY_7:			SelectWeapon(6);	return;
	case KEY_8:			SelectWeapon(7);	return;
	case KEY_9:			SelectWeapon(8);	return;
	case KEY_0:			SelectWeapon(9);	return;

	case KEY_F3:		if (GetHUDMode() != HUD_OBSERVER) ToggleHUDMode();	return;
	case KEY_F12:
		Marker_message = 1;
		StartHUDInputMessage();
		break;
	case KEY_F4:
	{
		if ((!(Game_mode & GM_MULTI)) || (Netgame.flags & NF_ALLOWGUIDEBOT))
		{
			Game_interface_mode = GAME_BUDDY_INTERFACE;
		}
		return;
	}break;
	case KEY_F9:
		//	quick save key!
		if (!Cinematic_inuse)
		{
			StartGameMenu();
			QuickSaveGame();
			EndGameMenu();
		}
		break;
	case KEY_ALTED + KEY_F2:
		if (!Cinematic_inuse)
		{
			Game_interface_mode = GAME_SAVE_INTERFACE;
			return;
		}break;
	}
}
bool Force_one_texture = false;

#ifdef _DEBUG
static int test_kill_objs = 0;

void InitTestSystems()
{
	test_kill_objs = 0;
}

// Tells others that we are cheating
void SendCheaterText()
{
	//Notify other players that you're using debug/test keys.  We may want to change this to 
	//only send the message for some keys.
	if (Game_mode & GM_MULTI)
	{
		char str[255];
		sprintf(str, TXT_CHEATER, Players[Player_num].callsign);

		if (Netgame.local_role == LR_SERVER)
			MultiSendMessageFromServer(GR_RGB(255, 0, 0), str);
		else
			MultiSendMessageToServer(0, str);
	}
}

#include "osiris_dll.h"
#include "audiotaunts.h"

void InitObjectScripts(object* obj, bool call_evt_created = true);

#ifdef _DEBUG
extern bool Cinematics_enabled;
#endif

//Deal with keys that only work for testing & debugging (eg., slew)
void ProcessTestKeys(int key)
{
	static int ffret = -1;
	int i;

	//Debug break
	if (key == KEY_SHIFTED + KEY_BACKSP)
		Int3();

	//Handle slew keys
	if (Player_object->control_type == CT_SLEW)
	{
		switch (key)	//Special for slew keys.  Add all new debugging keys to the second switch statement.
		{
		case KEY_PAD5:		SlewStop(Player_object); break;
		case KEY_PAD0:		SlewResetOrient(Player_object); break;
		}
	}

	if (key == (KEY_SHIFTED + KEY_LBRACKET))
		MultiSendRequestPlayTaunt(0);

	if (key == (KEY_SHIFTED + KEY_RBRACKET))
		MultiSendRequestPlayTaunt(1);

	//All other debug/test keys require the DEBUGGED modifier, so bail if it's not set
	if (!(key & KEY_DEBUGGED))
		return;

	switch (key & ~KEY_DEBUGGED) //for simplicity, do swich on keycode without the flag
	{

		/*
		**************************************************
						  Keys A-Z
		**************************************************
		*/
		// [ISB] Del+Shift+Z will cause a crash
	case KEY_Z + KEY_SHIFTED:
	{
		static int warncount = 0;
		if (warncount == 0)
		{
			AddHUDMessage("Press Del+Shift+Z again to test crash");
			warncount++;
		}
		else
		{
#pragma warning (push)
#pragma warning (disable : 6011)
			int(*kill)() = (int(*)())nullptr;
			kill();
#pragma warning (pop)
		}
	}
	break;
	case KEY_A:

		DoAI = !DoAI;
		AddHUDMessage("AI turned %s.", DoAI ? "on" : "off");
		break;

	case KEY_B:
		Game_show_sphere = (Game_show_sphere + 1) % 4;

		if (Game_show_sphere == 0)
			AddHUDMessage("No visible spheres.");
		else if (Game_show_sphere == 1)
			AddHUDMessage("Wall collision spheres(only encompass first anim frame).");
		else if (Game_show_sphere == 2)
			AddHUDMessage("Spheres for FVI(encompass all anims).");
		else if (Game_show_sphere == 3)
			AddHUDMessage("Object size spheres.");

		break;

	case KEY_C:
		SendCheaterText();
		if (Player_object->effect_info->type_flags & EF_CLOAKED)
		{
			MakeObjectVisible(Player_object);
			AddHUDMessage("You escape the darkness!");
		}
		else
		{
			MakeObjectInvisible(Player_object, 10000);
			AddHUDMessage("You enter the darkness!");
		}
		break;

	case (KEY_C + KEY_SHIFTED):
	{
		Cinematics_enabled = !Cinematics_enabled;

		AddHUDMessage("Game Cinematics: %s", Cinematics_enabled ? "enabled" : "disabled");
	}break;

	case KEY_D:
	{

		int GetRankIndex(int pnum, char* rankbuf);
		void ChangeRankIndex(int old_index, int pnum);

		int oldrank = GetRankIndex(1, NULL);
		Players[1].rank += 100;
		ChangeRankIndex(oldrank, 1);

		break;
	}

	case KEY_SHIFTED + KEY_D:
	{
		int done = 0;
		for (i = 0; i <= Highest_room_index && !done; i++)
		{
			for (int t = 0; t < Rooms[i].num_faces && !done; t++)
			{
				if (GameTextures[Rooms[i].faces[t].tmap].flags & TF_LIGHT)
				{
					DestroyLight(i, t);
					done = 1;
				}
			}
		}
		break;
	}

	case KEY_E:
		// ends level prematurely 
			//this is only for single player
		if (Game_mode & GM_MULTI) {
			AddHUDMessage("Press F6 to end a multiplayer level");
		}
		else {
			SetGameState(GAMESTATE_LVLEND);
		}
		break;

	case KEY_E + KEY_ALTED:
		//restart level
		if ((!(Game_mode & GM_MULTI)) || Netgame.local_role == LR_SERVER)
		{
			SetGameState(GAMESTATE_LVLFAILED);
		}break;

	case KEY_F:
		Hud_stat_mask ^= STAT_FPS;
		break;

		/*case KEY_G:
			{
				char dmb_msg[100];
				mprintf((0,"Current Gametime is %f\n",Gametime));
				sprintf(dmb_msg,"Current Gametime is %f\n",Gametime);
				AddHUDMessage(dmb_msg);
			}
			break;*/

	case KEY_G:
	{
		if (Player_object->type == OBJ_OBSERVER)
		{
			if (Game_mode & GM_MULTI)
				MultiSendRequestToObserve(OBSERVER_MODE_ROAM, 0, 0);
			else
				PlayerStopObserving(Player_num);

		}
		else
		{
			if (Game_mode & GM_MULTI)
				MultiSendRequestToObserve(OBSERVER_MODE_ROAM, 1, 0);
			else
				PlayerSwitchToObserver(Player_num, OBSERVER_MODE_ROAM);
		}
	}
	break;

	case KEY_H:
		HUD_disabled = !HUD_disabled;
		if (!HUD_disabled)
			AddHUDMessage(TXT_HUDENABLED);
		break;

	case KEY_I:

		SendCheaterText();
		if (Players[Player_num].flags & PLAYER_FLAGS_INVULNERABLE)
		{
			MakePlayerVulnerable(Player_num);
			AddHUDMessage("Your mother wont protect you now!");
		}
		else
		{
			MakePlayerInvulnerable(Player_num, 10000);
			AddHUDMessage("You're a wimp!");
		}
		break;

	case KEY_J:
	{
		object* obj = &Objects[Player_num];
		for (i = 0; i < 21; i++) {
			if (obj->dynamic_wb[i].flags & DWBF_QUAD) {
				obj->dynamic_wb[i].flags &= ~DWBF_QUAD;
				AddHUDMessage("Quad Flag Turned Off");
			}
			else {
				obj->dynamic_wb[i].flags |= DWBF_QUAD;
				AddHUDMessage("Quad Flag Turned On");
			}
		}

	}
	break;

	case KEY_K:
	{
		if (Game_mode & GM_MULTI)
			break;

		for (i = 0; i < MAX_OBJECTS; i++)
		{
			if (test_kill_objs == 0 && Objects[i].type == OBJ_ROBOT) {
				DoAI = 0;
				ObjDelete(i);
				AddHUDMessage("All robots dead!");
			}
			else if (test_kill_objs == 1 && Objects[i].type == OBJ_CLUTTER) {
				ObjDelete(i);
				AddHUDMessage("All clutter gone!");
			}
			else if (test_kill_objs == 2 && Objects[i].type == OBJ_POWERUP) {
				ObjDelete(i);
				AddHUDMessage("All powerups gone!");
			}
		}
		test_kill_objs++;

		test_kill_objs = test_kill_objs % 3;

		break;
	}

	case KEY_L:
		Terrain_LOD_engine_off = !Terrain_LOD_engine_off;
		AddHUDMessage("Terrain LOD engine turned %s", Terrain_LOD_engine_off ? "OFF" : "ON");
		break;

	case KEY_M:
		Render_portals = 1 - Render_portals;
		break;

	case KEY_N + KEY_SHIFTED:
		if (Current_level && Current_level->filename) {
			vector player_pos = Player_object->pos;
			matrix player_orient = Player_object->orient;
			int player_roomnum = Player_object->roomnum;

			//Load the level
			LoadAndStartCurrentLevel();

			//Restore player position
			ObjSetPos(Player_object, &player_pos, player_roomnum, &player_orient, true);
		}
		break;

	case KEY_O:
		Outline_mode ^= OM_ON;
		AddHUDMessage("Outline mode turned %s", OUTLINE_ON(0) ? "ON" : "OFF");
		break;

	case KEY_O + KEY_SHIFTED:
		Outline_mode = OM_ON + OM_ALL;
		AddHUDMessage("All outline options turned on.");
		break;

	case KEY_Q:
		if (StateLimited)
		{
			StateLimited = 0;
			AddHUDMessage("State limited turned off.");
		}
		else
		{
			StateLimited = 1;
			AddHUDMessage("State limited turned on.");
		}
		break;


	case KEY_R:
		if (Weather.flags & WEATHER_FLAGS_SNOW)
		{
			Weather.flags &= ~WEATHER_FLAGS_SNOW;
			AddHUDMessage("Snow turned off.");
		}
		else
		{
			Weather.flags |= WEATHER_FLAGS_SNOW;
			AddHUDMessage("Snow turned on.");
		}
		break;

	case KEY_S:
		if (Sound_system.IsActive()) {
			AddHUDMessage("Sound system off.");
			Sound_system.KillSoundLib(false);
		}
		else {
			AddHUDMessage("Sound system on.");
			Sound_system.InitSoundLib(Descent, Sound_mixer, Sound_quality, false);
		}
		break;

	case KEY_S + KEY_SHIFTED:
	{
		//extern spewinfo SpewEffects[MAX_SPEW_EFFECTS];
		int count;

		AddHUDMessage("Spewers Killed");

		for (count = 0; count < MAX_SPEW_EFFECTS; count++)
		{
			SpewEffects[count].inuse = 0;
		}

	}break;

	case KEY_T:
		if (Timedemo_frame == -1)
			Timedemo_frame = 0;
		break;

	case KEY_V:
		if (Debug_print_block)
		{
			AddHUDMessage("No block debug info");
			Debug_print_block = false;
		}
		else
		{
			AddHUDMessage("Block debug info on");
			Debug_print_block = true;
		}
		break;

	case KEY_W:
		if (Weather.flags & WEATHER_FLAGS_LIGHTNING)
		{
			Weather.flags &= ~WEATHER_FLAGS_LIGHTNING;
			AddHUDMessage("Lighting turned off.");

		}
		else
		{
			Weather.flags |= WEATHER_FLAGS_LIGHTNING;
			Weather.lightning_sequence = 0;
			Weather.lightning_rand_value = 70;
			Weather.lightning_interval_time = .01f;
			AddHUDMessage("Lighting turned on.");
		}
		break;

	case KEY_X:
	{
		SendCheaterText();
		Players[Player_num].energy = 200.0f;
		Players[Player_num].weapon_flags = 0xFFFFFFFF;
		int i;

		for (i = 0; i < MAX_PLAYER_WEAPONS; i++)
			Players[Player_num].weapon_ammo[i] = Ships[Players[Player_num].ship_index].max_ammo[i];

		Players[Player_num].afterburn_time_left = AFTERBURN_TIME;

		AddHUDMessage("All weapons!");


		//add all the countermeasures
		if (Game_mode & GM_MULTI)
			break;
		int c_id, b_id, s_id, g_id, p_id;
		c_id = FindWeaponName("Chaff");
		b_id = FindWeaponName("Betty");
		s_id = FindWeaponName("SeekerMine");
		g_id = FindWeaponName("Gunboy");
		p_id = FindWeaponName("ProxMine");

		int c_pid, b_pid, s_pid, g_pid, p_pid;
		c_pid = FindObjectIDName("chaff");
		b_pid = FindObjectIDName("Betty4pack");
		s_pid = FindObjectIDName("Seeker3pack");
		g_pid = FindObjectIDName("gunboypowerup");
		p_pid = FindObjectIDName("ProxMinepowerup");

		if (c_pid == -1)
			c_pid = FindObjectIDName("Shield");
		if (b_pid == -1)
			b_pid = FindObjectIDName("Energy");
		if (s_pid == -1)
			s_pid = FindObjectIDName("Shield");
		if (g_pid == -1)
			g_pid = FindObjectIDName("Energy");
		if (p_pid == -1)
			p_pid = FindObjectIDName("Shield");

		object* objp = &Objects[Players[Player_num].objnum];
		for (i = 0; i < 30; i++) {
			if (c_id != -1)
				Players[Player_num].counter_measures.Add(OBJ_WEAPON, c_id, objp, OBJ_POWERUP, c_pid);
			if (b_id != -1)
				Players[Player_num].counter_measures.Add(OBJ_WEAPON, b_id, objp, OBJ_POWERUP, b_pid);
			if (s_id != -1)
				Players[Player_num].counter_measures.Add(OBJ_WEAPON, s_id, objp, OBJ_POWERUP, s_pid);
			if (g_id != -1)
				Players[Player_num].counter_measures.Add(OBJ_WEAPON, g_id, objp, OBJ_POWERUP, g_pid);
			if (p_id != -1)
				Players[Player_num].counter_measures.Add(OBJ_WEAPON, p_id, objp, OBJ_POWERUP, p_pid);
		}

		break;
	}

	case KEY_Y:
		Physics_player_verbose = 1 - Physics_player_verbose;
		if (Physics_player_verbose)
			AddHUDMessage("Verbose player physics info\n");
		else
			AddHUDMessage("Player physics info off\n");
		break;

	case KEY_Z:
	{
		//Inventory test
		int id, objnum;

		id = FindObjectIDName("Betty4Pack");
		objnum = ObjCreate(OBJ_POWERUP, id, Player_object->roomnum, &Player_object->pos, &Player_object->orient);

		if (objnum != -1)
		{
			if (Game_mode & GM_MULTI)
				MultiSendObject(&Objects[objnum], false);

			InitObjectScripts(&Objects[objnum], true);
			Players[Player_num].inventory.AddObject(Objects[objnum].handle, true);
		}
	}break;

	case KEY_Z + KEY_ALTED:
	{
		//look for a betty4pack and remove the sucker
		int id;
		id = FindObjectIDName("Betty4Pack");
		for (i = 0; i < MAX_OBJECTS; i++) {
			if (Objects[i].type == OBJ_DUMMY && Objects[i].dummy_type == OBJ_POWERUP && Objects[i].id == id) {
				//here's a betty
				mprintf((0, "Killing a betty\n"));
				SetObjectDeadFlag(&Objects[i], true);
			}
		}

	}break;
	/*
	**************************************************
					  Keypad Keys
	**************************************************
	*/

	case KEY_PAD0:
	{
		Game_update_attach = 1 - Game_update_attach;

		if (Game_update_attach)
		{
			mprintf((0, "Update attach on\n"));
		}
		else
		{
			mprintf((0, "Update attach off\n"));
		}
	}
	break;

	case KEY_PAD1:
	{
		Game_do_flying_sim = 1 - Game_do_flying_sim;
		if (Game_do_flying_sim)
		{
			mprintf((0, "Game_do_flying_sim on\n"));
		}
		else
		{
			mprintf((0, "Game_do_flying_sim off\n"));
		}
	}
	break;

	case KEY_PAD2:
	{
		Game_do_walking_sim = 1 - Game_do_walking_sim;
		if (Game_do_walking_sim)
		{
			mprintf((0, "Game_do_walking_sim on\n"));
		}
		else
		{
			mprintf((0, "Game_do_walking_sim off\n"));
		}
	}
	break;

	case KEY_PAD3:
	{
		Game_do_vis_sim = 1 - Game_do_vis_sim;
		if (Game_do_vis_sim)
		{
			mprintf((0, "Game_do_vis_sim on\n"));
		}
		else
		{
			mprintf((0, "Game_do_vis_sim off\n"));
		}
	}
	break;

	case KEY_PAD4:
	{
		Game_do_ai_movement = 1 - Game_do_ai_movement;
		if (Game_do_ai_movement)
		{
			mprintf((0, "Game_do_ai_movement on\n"));
		}
		else
		{
			mprintf((0, "Game_do_ai_movement off\n"));
		}
	}
	break;

	case KEY_PAD5:
	{
		Game_do_ai_vis = 1 - Game_do_ai_vis;
		if (Game_do_ai_vis)
		{
			mprintf((0, "Game_do_ai_vis on\n"));
		}
		else
		{
			mprintf((0, "Game_do_ai_vis off\n"));
		}
	}
	break;

	case KEY_PAD6:
	{
		if (AI_debug_robot_do)
		{
			AI_debug_robot_do = false;
			mprintf((0, "AI Debug Info Off\n"));
		}
		else
		{
			AI_debug_robot_do = true;
			mprintf((0, "AI Debug Info On\n"));
		}
	}
	break;

	case KEY_PAD7:
	{
		bool killed_something = false;
		int killed_count = 0;
		int i, a;
		for (i = 0; i < MAX_OBJECTS; i++)
		{
			if (Objects[i].type == OBJ_ROBOT || (Objects[i].type == OBJ_BUILDING && Objects[i].ai_info))
			{
				// Now, kill all robots of this type!
				AddHUDMessage("Killing all %s robots!", Object_info[Objects[i].id].name);
				for (a = 0; a < MAX_OBJECTS; a++)
				{
					if (Objects[a].id == Objects[i].id)
					{
						SetObjectDeadFlag(&Objects[a]);
						killed_something = true;
						killed_count++;
					}

				}
				/*
				SetObjectDeadFlag(&Objects[i]);
				AddHUDMessage("Killed object %d!",i);

				if(killed_count>4)
					break;
				*/
			}
			if (killed_something)
				break;
		}
		if (!killed_something)
			AddHUDMessage("Nothing to kill!");
		else
			mprintf((0, "Killed %d objects of type %s!\n", killed_count, Object_info[Objects[i].id].name));
	}
	break;
	/*
	**************************************************
					  Number Keys
	**************************************************
	*/

	case KEY_1:
	{
		// goto prev viewer
		int viewer_objnum = Viewer_object - Objects;
		int found = 0;

		for (i = viewer_objnum - 1; i >= 0 && !found; i--, i < 0 ? i = Highest_object_index : i = i)
		{
			if (Objects[i].type == OBJ_POWERUP || Objects[i].type == OBJ_PLAYER || Objects[i].type == OBJ_ROBOT ||
				Objects[i].type == OBJ_CLUTTER || Objects[i].type == OBJ_BUILDING || Objects[i].type == OBJ_MARKER)
			{
				found = 1;
				Viewer_object = &Objects[i];
			}
		}

		AddHUDMessage("Viewer set to previous object.");

	}
	break;

	case KEY_2:
	{
		// reset viewer
		AddHUDMessage("Viewer set to player object.");
		Viewer_object = Player_object;
	}
	break;

	case KEY_3:
	{
		// goto next viewer
		int viewer_objnum = Viewer_object - Objects;
		int found = 0;

		for (i = viewer_objnum + 1; i <= Highest_object_index && !found; i++, i %= (Highest_object_index + 1))
		{
			if (Objects[i].type == OBJ_POWERUP || Objects[i].type == OBJ_PLAYER || Objects[i].type == OBJ_ROBOT ||
				Objects[i].type == OBJ_CLUTTER || Objects[i].type == OBJ_BUILDING || Objects[i].type == OBJ_MARKER)
			{
				found = 1;
				Viewer_object = &Objects[i];
			}
		}

		AddHUDMessage("Viewer set to next object.");
	}
	break;

	case KEY_4:
		if (Tracking_FVI)
			Tracking_FVI = false;
		else
			Tracking_FVI = true;

		AddHUDMessage("Tracking FVI now set to %s.", Tracking_FVI ? "on" : "off");

		break;


	case KEY_6:
		Render_FOV -= 1;
		AddHUDMessage("FOV is now %f", Render_FOV);
		break;

	case KEY_7:
		Render_FOV += 1;
		AddHUDMessage("FOV is now %f", Render_FOV);
		break;

	case KEY_8:
		Render_FOV = Render_FOV_desired;
		AddHUDMessage("FOV has been reset to %f", Render_FOV);
		break;

	case KEY_9:
		Force_one_texture = !Force_one_texture;
		break;

		/*
		**************************************************
						  Function Keys
		**************************************************
		*/

	case KEY_F5:
		D3MusicToggle();
		break;

	case KEY_F6:
	{
		vector test_pos;
		fvi_info hit_info;
		fvi_query fq;
		int fate;
		int this_key = key & ~KEY_DEBUGGED;

		test_pos = Player_object->pos + Player_object->orient.fvec * 1000000.0f;

		fq.p0 = &Player_object->pos;
		fq.startroom = Player_object->roomnum;
		fq.p1 = &test_pos;
		fq.rad = 0.0;
		fq.thisobjnum = OBJNUM(Player_object);
		fq.ignore_obj_list = NULL;
		if (this_key == KEY_F6)
			fq.flags = FQ_CHECK_OBJS | FQ_IGNORE_RENDER_THROUGH_PORTALS;
		else
			fq.flags = 0;

		fate = fvi_FindIntersection(&fq, &hit_info);

		switch (fate)
		{
		case HIT_NONE:
			mprintf((0, "Hit nothing\n"));
			break;
		case HIT_WALL:
			mprintf((0, "Hit wall\n"));
			break;
		case HIT_OBJECT:
			mprintf((0, "Hit Object %d\n", hit_info.hit_object));
			break;
		case HIT_TERRAIN:
			mprintf((0, "Hit Terrain %d\n", CELLNUM(hit_info.hit_room)));
			break;
		case HIT_OUT_OF_TERRAIN_BOUNDS:
			mprintf((0, "Hit nothing: Leaving Terrain\n"));
			break;
		case HIT_SPHERE_2_POLY_OBJECT:
			mprintf((0, "Hit Poly_object\n"));
			break;
		default:
			mprintf((0, "Hit %d not printed, add to list\n", fate));
			break;
		}

		if (fate)
		{
			int objnum;

			objnum = ObjCreate(OBJ_DEBUG_LINE, 1, Player_object->roomnum, &Player_object->pos, NULL);
			if (objnum >= 0) { //DAJ -1FIX
				Objects[objnum].rtype.line_info.end_pos = hit_info.hit_pnt;
				Objects[objnum].size = vm_VectorDistance(&Player_object->pos, &hit_info.hit_pnt);
				ObjSetAABB(&Objects[objnum]);
			}
		}

		Sound_system.CheckAndForceSoundDataAlloc(SOUND_REFUELING);
		Sound_system.PlayStream(0, SoundFiles[Sounds[SOUND_REFUELING].sample_index].sample_8bit, SoundFiles[Sounds[SOUND_REFUELING].sample_index].sample_length, SIF_STREAMING_8_M, 1.0, NULL);
	}
	break;

	case KEY_F7:
		// Toggle Music Debug Info!!
		Music_debug_verbose = !Music_debug_verbose;
		break;

	case KEY_F8:
		D3MusicSetRegion((D3MusicGetRegion() > 0) ? 0 : 1);
		break;

	case KEY_F9:
	{
		vector vec = Player_object->pos + (Player_object->orient.fvec * 20);
		if (BSPRayOccluded(&Player_object->pos, &vec, MineBSP.root))
			mprintf((0, "Occluded!\n"));
		else
			mprintf((0, "NOT occluded!\n"));
	}break;

	case KEY_F10:
		int ObjInitTypeSpecific(object * objp, bool reinitializing);
		FreeCockpit();
		CloseShipHUD();
		Players[0].ship_index = GetNextShip(Players[0].ship_index);
		AddHUDMessage("Player ship set to %s\n", Ships[Players[0].ship_index].name);
		ObjInitTypeSpecific(Player_object, 0);		//reset physics, model, etc.
		InitShipHUD(Players[0].ship_index);
		InitCockpit(Players[0].ship_index);
		if (GetHUDMode() == HUD_COCKPIT)
			SetHUDMode(HUD_COCKPIT);
		else if (GetHUDMode() == HUD_FULLSCREEN)
			SetHUDMode(HUD_FULLSCREEN);
		break;

	case KEY_F11:
	{
		char test[MAX_MATCEN_NAME_LEN];
		bool f_name_changed;

		strcpy(test, "test");

		int m_id = CreateMatcen(test, &f_name_changed);

		if (m_id >= 0)
		{
			int id = FindObjectIDName("Tubbs");
			int p = 100;
			float t = 5.0f;
			int m = 5;

			mprintf((0, "Matcen alive!\n"));

			vector centerPt = Player_object->pos + (Player_object->orient.fvec * 2.0f);
			Matcen[m_id]->SetAttachType(MT_ROOM);
			Matcen[m_id]->SetAttach(Player_object->roomnum);
			Matcen[m_id]->SetCreateRoom(Player_object->roomnum);
			Matcen[m_id]->SetCreatePnt(&centerPt);
			Matcen[m_id]->SetControlType(MPC_WHILE_PLAYER_NEAR);

			Matcen[m_id]->SetMaxProd(12);
			Matcen[m_id]->SetNumProdTypes(3);
			Matcen[m_id]->SetMaxAliveChildren(1);
			Matcen[m_id]->SetProdInfo(0, &id, &p, &t, &m);
			id = FindObjectIDName("Sixgun");
			Matcen[m_id]->SetProdInfo(1, &id, &p, &t, &m);
			id = FindObjectIDName("bladescaled");
			Matcen[m_id]->SetProdInfo(2, &id, &p, &t, &m);

			Matcen[m_id]->SetStatus(MSTAT_ACTIVE | MSTAT_RANDOM_PROD_ORDER, true);
		}
		else
		{
			mprintf((0, "Nope!\n"));
		}
	}
	break;

	/*
	**************************************************
					  Other Keys
	**************************************************
	*/
	case KEY_TAB:
		Game_interface_mode = GAME_DEBUGGRAPH_INTERFACE;
		break;

	case KEY_COMMA:
		rtp_StartLog();
		break;

	case KEY_PERIOD:
		rtp_StopLog();
		break;

	case KEY_LAPOSTRO:
		Show_osiris_debug = !Show_osiris_debug;
		AddHUDMessage("%s osiris debug messages", (Show_osiris_debug) ? "Enabling" : "Disabling");
		break;

	case KEY_BACKSLASH:
		//attempt to restore video
		if (DebugBreak_callback_stop)
			(*DebugBreak_callback_stop)();
		if (DebugBreak_callback_resume)
			(*DebugBreak_callback_resume)();
		break;

	case KEY_SPACEBAR:
		if (Player_object->control_type == CT_FLYING)
		{
			SetObjectControlType(Player_object, CT_SLEW);
			Player_object->movement_type = MT_NONE;
			SlewStop(Player_object);
			AddHUDMessage("Slew on");
		}
		else
		{
			SlewStop(Player_object);
			SetObjectControlType(Player_object, CT_FLYING);
			Player_object->movement_type = MT_PHYSICS;
			AddHUDMessage("Physics on");
		}
		break;

	case KEY_BACKSP:
	{
		if (!ROOMNUM_OUTSIDE(Player_object->roomnum))
		{
			int i;

			Game_show_portal_vis_pnts = 1 - Game_show_portal_vis_pnts;

			mprintf((0, "Vis info for room %d\n", Player_object->roomnum));

			for (i = 0; i <= Highest_room_index; i++)
			{
				if (BOA_IsVisible(i, Player_object->roomnum))
				{
					mprintf((0, "%d can see you in %d\n", i, Player_object->roomnum));
				}
			}

			for (i = Highest_room_index + 1; i <= Highest_room_index + 8; i++)
			{
				if (BOA_IsVisible(i, Player_object->roomnum))
				{
					mprintf((0, "Terrain %d can see you in %d\n", i - Highest_room_index - 1, Player_object->roomnum));
				}
			}
		}
	}
	break;
	}
}
#endif

//Sends this key to the multiplayer game DLL
//Return:	true means to handle the key normally
//				false means to do no further processing on this key
bool SendKeyToGameDLL(int key)
{
	DLLInfo.input_key = key;

	DLLInfo.iRet = 0;
	CallGameDLL(EVT_CLIENT_KEYPRESS, &DLLInfo);

	if (DLLInfo.iRet != 0)
	{
		ddio_KeyFlush();		//keep the controls stuff from getting any keys
		return 0;				//Don't process this key
	}

	//Handle key normally
	return 1;
}

void DemoCheats(int key);
//Get and handle all pending keys
void ProcessKeys()
{
	int key;
#ifdef GAMEGAUGE
	if (1)
#else
	if (Game_gauge_do_time_test)
#endif
	{
		return;
	}

	if (Dedicated_server)
		return;	// No key processing for dedicated server!

	//	if we are in some menu then don't process game keys!
	if (Game_interface_mode != GAME_INTERFACE || Menu_interface_mode)
		return;

	//Process all pending keys
	while ((key = ddio_KeyInKey()) != 0)
	{
		//If inputting a multiplayer message, send keys there
		if (Doing_input_message)
		{
			DoHUDInputMessageKey(key);
			continue;
		}

		//If multiplayer, send the key to the game DLL
		if (Game_mode & GM_MULTI)
		{
			if (!SendKeyToGameDLL(key))		//returns true if should keep processing this key
				continue;							//the DLL says not process this key
		}

		//Handle normal key
		ProcessNormalKey(key);

		//Handle debugging & test keys
#ifdef _DEBUG
		ProcessTestKeys(key);
#endif

		//#ifdef DEMO
		DemoCheats(key);
		//#endif
	}
}

//Render the world into a game window
//Parameters:	viewer - if not null, this object disabled from rendering.  Not used otherwise.
//					viewer_eye - where we're rendering from
//					viewer_roomnum - the roomnum viewer_eye is in
//					viewer_orient - the oriention for this view
//					zoom - the zoom for this view
//					rear_view - if true, we're looking out the rear of this object

#ifdef _DEBUG
extern void DrawRoomVisPnts(object* obj);
#endif

// GameRenderWorld's stateful world execution has been removed. World views
// below consume the presentation extracted once by GameRenderFrame.

static void GetMainViewOverscanFrame(int* x1, int* y1, int* x2, int* y2, int* anchor_x, int* anchor_y)
{
	*x1 = Game_window_x;
	*y1 = Game_window_y;
	*x2 = Game_window_x + Game_window_w;
	*y2 = Game_window_y + Game_window_h;
	*anchor_x = 0;
	*anchor_y = 0;

	if (Cinematic_inuse)
		return;

	if (!Render_preferred_state.ao_enabled)
		return;

	int overscan_percent = Render_preferred_state.ao_overscan_percent;
	if (overscan_percent < 100)
		overscan_percent = 100;
	if (overscan_percent > 150)
		overscan_percent = 150;
	if (overscan_percent <= 100 || Game_window_w <= 0 || Game_window_h <= 0)
		return;

	const int overscan_w = (Game_window_w * overscan_percent + 99) / 100;
	const int overscan_h = (Game_window_h * overscan_percent + 99) / 100;
	const int extra_w = overscan_w - Game_window_w;
	const int extra_h = overscan_h - Game_window_h;
	const int left = (extra_w + 1) / 2;
	const int top = (extra_h + 1) / 2;

	*x1 = Game_window_x - left;
	*y1 = Game_window_y - top;
	*x2 = Game_window_x + Game_window_w + (extra_w - left);
	*y2 = Game_window_y + Game_window_h + (extra_h - top);
	*anchor_x = left;
	*anchor_y = top;
}

static GameMainView CaptureGameMainView()
{
	extern bool Guided_missile_smallview;
	int x1,y1,x2,y2,anchor_x,anchor_y;
	GetMainViewOverscanFrame(&x1,&y1,&x2,&y2,&anchor_x,&anchor_y);
	const bool guided = !Cinematic_inuse && Players[Player_num].guided_obj && !Guided_missile_smallview;
	const auto* viewer = guided ? Players[Player_num].guided_obj : Viewer_object;
	const bool rear = !guided && viewer == Player_object && (Players[Player_num].flags & PLAYER_FLAGS_REARVIEW);
	GameMainView result;
	result.world = MakeGameSceneView(d3scene::ViewKind::Main,1,viewer,viewer->pos,viewer->roomnum,
		viewer->orient,Render_zoom,rear,{x1,y1,x2-x1,y2-y1},{anchor_x,anchor_y,Game_window_w,Game_window_h});
	result.display_viewport = {Game_window_x,Game_window_y,Game_window_w,Game_window_h};
	result.player_first_person = viewer == Player_object && !rear;
	return result;
}

//Render into the big window
void GameDrawMainView(const d3scene::Presentation& presentation, SceneFeedbackTransaction& feedback,const GameMainView& main_view)
{
	PERF_MARKER_SCOPE("GameDrawMainView");

	DebugBlockPrint("SR");

	//Start rendering
	const auto& viewport = main_view.world.viewport;
	{
		PERF_MARKER_SCOPE("StartFrame.MainView");
		StartFrame(viewport.x,viewport.y,viewport.x+viewport.width,viewport.y+viewport.height,true);
	}

	//Draw the world
	Rendering_main_view = true;
	{
		PERF_MARKER_SCOPE("GameRenderWorld.MainView");
		RenderGameSceneView(presentation,main_view.world,&feedback);
	}
	rend_PerfGpuSceneMark(RENDERER_GPU_SCENE_AFTER_MAIN_WORLD);
	Rendering_main_view = false;

	// Room changes
	{
		PERF_MARKER_SCOPE("DoRoomChangeFrame");
		DoRoomChangeFrame();
	}
	rend_PerfGpuSceneMark(RENDERER_GPU_SCENE_AFTER_ROOM_CHANGE);

	// Matcen effects already belong to the published world. No draw-time update.
	rend_PerfGpuSceneMark(RENDERER_GPU_SCENE_AFTER_MATCENS);

	// Render only immutable screen-blend data. Queue callbacks already advanced
	// once in the game phase, before publication; this stage cannot replay them.
	{
		PERF_MARKER_SCOPE("Scene.FrameOverlays");
		RenderGameForegrounds(presentation,d3scene::ForegroundPhase::AfterMainWorld);
	}
	rend_PerfGpuSceneMark(RENDERER_GPU_SCENE_AFTER_RENDER_EVENTS);

	rend_PerfGpuSceneMark(RENDERER_GPU_SCENE_AFTER_CAPTURE_BLOOM);

	//We're done with this window
	{
		PERF_MARKER_SCOPE("EndFrame.MainView");
		EndFrame();
	}

	DebugBlockPrint("DR");
}

//Added by Samir
constexpr float HUD_RENDER_ZOOM = SceneCockpitZoom;

//Do Cockpit/Hud
void GameDrawHud()
{
	PERF_MARKER_SCOPE("GameDrawHud");
	//render HUD
	//[ISB] Moved frame start and end to RenderHUDFrame so it can make decisions based on the current HUD mode. 
	{
		PERF_MARKER_SCOPE("RenderHUDFrame");
		RenderHUDFrame(HUD_RENDER_ZOOM);
	}
	rend_PerfGpuSceneMark(RENDERER_GPU_SCENE_AFTER_PRIMARY_HUD);

	//render auxillary consoles
	{
		PERF_MARKER_SCOPE("StartFrame.AuxHUD");
		StartFrame(0, 0, Max_window_w, Max_window_h, false);
	}
	{
		PERF_MARKER_SCOPE("g3_StartFrame.AuxHUD");
		g3_StartFrame(&Viewer_object->pos, &Viewer_object->orient, HUD_RENDER_ZOOM);
	}

	{
		PERF_MARKER_SCOPE("RenderAuxHUDFrame");
		RenderAuxHUDFrame();
	}

	//End frame
	{
		PERF_MARKER_SCOPE("g3_EndFrame.AuxHUD");
		g3_EndFrame();
	}
	{
		PERF_MARKER_SCOPE("EndFrame.AuxHUD");
		EndFrame();
	}
	rend_PerfGpuSceneMark(RENDERER_GPU_SCENE_AFTER_AUX_HUD);
}

static void GameDrawPostPresentFrame(bool force_backbuffer_frame)
{
	const bool draw_message_consoles = !HUD_disabled &&
		!IsCleanGameplayScreenshotActive() && AreHUDMessageConsolesOpen();
	// Cockpit and close-screen effects are owned foregrounds now. This adapter
	// remains only for the not-yet-migrated console UI; never replay old camera
	// snapshots or late-effect queues into the completed scene.
	if (!force_backbuffer_frame && !draw_message_consoles)
		return;

	PERF_MARKER_SCOPE("GameDrawPostPresentFrame");
	rend_StartPostPresentFrame(0, 0, Max_window_w, Max_window_h, RF_CLEAR_ZBUFFER);
	if (draw_message_consoles)
	{
		RenderHUDMessageConsoles();
	}
	rend_EndFrame();
}

//Draw a frame of the game
void GameRenderFrame(void)
{
	bool no_render = false;
	AI_NumRendered = 0;
	AI_NumHostileAlert = 0;
	ResetPostProcessHUDText();

	if (Dedicated_server)
		return;

	PerfMarkersBeginFrame();
	if (SceneGeometryInspection() && FindArg("-scene-hud-small-probe")) {
		Game_window_w = Max_window_w*3/5; Game_window_h = Max_window_h*3/5;
		Game_window_x = (Max_window_w-Game_window_w)/2; Game_window_y = (Max_window_h-Game_window_h)/2;
	}
	if (SceneGeometryInspection() && FindArg("-scene-bloom-resource-probe")) {
		const int index = AutomatedCaptureFrameIndex();
		if (index == 15 || index == 35 || index == 55) {
			// Keep dimensions/sample count unchanged: bloom alone must create
			// and retire its coverage image through the ordinary settings API.
			auto state = Render_preferred_state;
			if (index != 55) state.bloom_enabled = index == 35;
			if (!rend_SetPreferredState(&state)) Error("Bloom resource transition failed");
			AutomatedCaptureLog("scene bloom transition frame=%d enabled=%d",index,state.bloom_enabled ? 1 : 0);
		}
	}
	if (SceneGeometryInspection() && FindArg("-scene-resource-probe")) {
		const int index = AutomatedCaptureFrameIndex();
		if (index >= 10 && index <= 50 && index % 10 == 0) {
			// Capture-only resource transitions. Do not change or save the user's
			// preferred settings; exercise the normal in-place renderer API.
			auto state = Render_preferred_state;
			if (index == 10 || index == 40) { state.supersampling_factor = 1; state.msaa_samples = 0; }
			if (index == 20) { state.supersampling_factor = 2; state.msaa_samples = 2; }
			if (index == 30) { state.supersampling_factor = 4; state.msaa_samples = 4; }
			state.antialised = state.msaa_samples > 0;
			if (!rend_SetPreferredState(&state)) Error("Resource transition fixture could not apply renderer state");
			AutomatedCaptureLog("scene resource transition frame=%d ssaa=%u msaa=%u",index,
				unsigned(state.supersampling_factor),unsigned(state.msaa_samples));
		}
	}

#ifndef RELEASE
	Mine_depth = 0;
#endif

	{
		PERF_MARKER_SCOPE("PreUpdateAllLightGlows");
		PreUpdateAllLightGlows();
	}

	// Don't render if receiving data
	if ((Game_mode & GM_MULTI) && NetPlayers[Player_num].sequence != NETSEQ_PLAYING)
	{
		no_render = true;
		ShowProgressScreen(TXT_RECEIVINGDATA, NULL, 0);
	}

	rend_SetZBufferState(1);
	rend_SetZBufferWriteMask(1);

	// clear screen if needed
	if (Clear_screen)
	{
		PERF_MARKER_SCOPE("ClearScreen");
		StartFrame(0, 0, Max_window_w, Max_window_h, false);
		rend_ClearScreen(GR_BLACK);
		EndFrame();
		Clear_screen--;
	}

	//Render the mine
	if (!no_render)
	{
		if (Game_paused && !Demo_do_one_frame)
		{
			PERF_MARKER_SCOPE("ObjDoLightingFrameAll");
			ObjDoLightingFrameAll();
		}

		// render preliminary hud view (for dirty rectangles)
		if (Small_hud_flag)
		{
			PERF_MARKER_SCOPE("RenderPreHUDFrame");
			// small hud flag is set in RenderHUDFrame in GameDrawHud.
			StartFrame(0, 0, Max_window_w, Max_window_h, false);
			RenderPreHUDFrame();
			EndFrame();
		}


		// Publish once. Every world view below sees this same owned scene, not
		// a second traversal of mutable simulation state.
		if (AutomatedCaptureFrameIndex() == 0 && FindArg("-scene-close-model-probe") && Player_object) {
			int prototype = -1;
			for (int i = 0; i < MAX_WEAPONS; ++i) if (Weapons[i].used && !(Weapons[i].flags &
				(WF_IMAGE_BITMAP|WF_IMAGE_VCLIP|WF_ELECTRICAL|WF_INVISIBLE)) && Weapons[i].fire_image_handle >= 0) { prototype = i; break; }
			if (prototype < 0) Error("Camera audience fixture has no authored model weapon");
			for (int marked = 0; marked < 2; ++marked) {
				auto position = Player_object->pos+Player_object->orient.fvec*10+Player_object->orient.rvec*((marked-.5f)*4);
				const int index = ObjCreate(OBJ_WEAPON,prototype,Player_object->roomnum,&position,&Player_object->orient,Player_object->handle);
				if (index < 0) Error("Camera audience model initialization failed");
				auto& source = Objects[index]; source.control_type = CT_NONE; source.movement_type = MT_NONE;
				source.size = 1; source.lifeleft = source.lifetime = 3;
				if (marked) VisEffectMarkCloseScreenWeaponObject(&source);
				AutomatedCaptureLog("scene real model audience fixture owner=%u marked=%d definition=%d",source.handle,marked,prototype);
			}
		}
		if (AutomatedCaptureFrameIndex() == 0 && FindArg("-scene-object-emissions-probe") && Player_object) {
			int powerup = -1,robot = GENOBJ_GUIDEBOT;
			for (int i = 0; i <= Highest_object_index; ++i) {
				if (Objects[i].type == OBJ_POWERUP && powerup < 0) powerup = Objects[i].id;
				if (Objects[i].type == OBJ_ROBOT) robot = Objects[i].id;
			}
			if (powerup < 0) Error("Object emission fixture needs an authored powerup");
			Render_powerup_sparkles = true;
			for (int variant = 0; variant < 3; ++variant) {
				auto position = Player_object->pos+Player_object->orient.fvec*16+
					Player_object->orient.rvec*((variant-1.f)*5);
				const int index = ObjCreate(variant ? OBJ_ROBOT : OBJ_POWERUP,variant ? robot : powerup,
					Player_object->roomnum,&position,&Player_object->orient);
				if (index < 0) Error("Object emission fixture initialization failed");
				auto& source = Objects[index]; source.control_type = CT_NONE; source.movement_type = MT_NONE;
				source.shields = variant == 1 ? .01f : 100000;
				if (variant == 2) {
					if (!source.effect_info) Error("Authored robot has no effect state for virus fixture");
					source.effect_info->type_flags |= EF_VIRUS_INFECTED;
				}
				AutomatedCaptureLog("scene real object-init emitter fixture variant=%d owner=%u definition=%d",variant,source.handle,source.id);
			}
		}
		if (AutomatedCaptureFrameIndex() == 1 && FindArg("-scene-mass-snow-probe") && Player_object) {
			// Legacy flakes are deliberately retired by the simulation while the
			// enhanced-weather option is active. Exercise their actual setting.
			Render_enhanced_weather = false;
			const int weapon = FindWeaponName("Mass Driver");
			if (weapon < 0 || FireWeaponFromObject(Player_object,weapon,-1,true,false) < 0)
				Error("Real mass-driver firing fixture failed");
			vis_effect* trail = nullptr;
			for (int i = 0; i <= Highest_vis_effect_index; ++i) if (VisEffects[i].type == VIS_FIREBALL && VisEffects[i].id == MASSDRIVER_EFFECT_INDEX)
				trail = &VisEffects[i];
			if (!trail) Error("Real mass-driver firing did not emit its trail");
			const auto center = Player_object->pos+Player_object->orient.fvec*16;
			trail->pos = center-Player_object->orient.rvec*6+Player_object->orient.uvec*2;
			trail->end_pos = trail->pos+Player_object->orient.rvec*12;
			trail->flags &= ~VF_LINK_TO_VIEWER;
			auto position = center-Player_object->orient.rvec*6-Player_object->orient.uvec*2;
			const int index = VisEffectCreate(VIS_FIREBALL,MERCBOSS_MASSDRIVER_EFFECT_INDEX,Player_object->roomnum,&position);
			if (index < 0) Error("Boss mass-driver fixture allocation failed");
			auto& boss = VisEffects[index]; boss.end_pos = position+Player_object->orient.rvec*12;
			boss.custom_handle = trail->custom_handle; boss.billboard_info.width = 1;
			boss.lifeleft = boss.lifetime = 1.5f; boss.flags |= VF_USES_LIFELEFT;
			boss.lighting_color = GR_RGB16(240,10,10);
			for (int i = 0; i < 3; ++i) {
				position = center+Player_object->orient.uvec*(i-1.f);
				const int snow = VisEffectCreate(VIS_FIREBALL,SNOWFLAKE_INDEX,Player_object->roomnum,&position);
				if (snow < 0) Error("Legacy snow fixture allocation failed");
				auto& flake = VisEffects[snow]; flake.lifetime = 3; flake.lifeleft = 2.7f; flake.creation_time = Gametime-.3f;
				flake.size = .08f*(i+1); flake.flags |= VF_USES_LIFELEFT; flake.lighting_color = GR_RGB16(255,255,255);
			}
			AutomatedCaptureLog("scene mass/snow fixture real_fired_mass=1 boss_record=1 legacy_flakes=3");
		}
		if (AutomatedCaptureFrameIndex() == 0 && FindArg("-scene-weapon-contributions-probe") && Player_object) {
			int sprite = -1,model = -1;
			for (int i = 0; i < MAX_WEAPONS; ++i) if (Weapons[i].used && !(Weapons[i].flags & (WF_ELECTRICAL | WF_INVISIBLE))) {
				if (Weapons[i].flags & (WF_IMAGE_BITMAP | WF_IMAGE_VCLIP)) { if (sprite < 0) sprite = i; }
				else if (model < 0 && Weapons[i].fire_image_handle >= 0) model = i;
			}
			if (sprite < 0 || model < 0) Error("Weapon contribution fixture has no authored sprite/model prototype");
			for (int variant = 0; variant < 4; ++variant) {
				const int definition = AllocWeapon();
				if (definition < 0) Error("Weapon contribution fixture has no definition slot");
				Weapons[definition] = Weapons[variant < 2 ? sprite : model];
				strcpy(Weapons[definition].name,"Integration contributions");
				Weapons[definition].flags |= WF_STREAMER | WF_RING;
				if (variant%2) Weapons[definition].flags |= WF_INVISIBLE;
				Weapons[definition].lighting_info.red_light1 = .4f;
				Weapons[definition].lighting_info.green_light1 = .8f;
				Weapons[definition].lighting_info.blue_light1 = 1;
				auto position = Player_object->pos+Player_object->orient.fvec*12+
					Player_object->orient.rvec*((variant-1.5f)*3);
				const int index = ObjCreate(OBJ_WEAPON,definition,Player_object->roomnum,&position,&Player_object->orient,Player_object->handle);
				if (index < 0) Error("Weapon contribution fixture object initialization failed");
				auto& object = Objects[index]; object.control_type = CT_NONE; object.movement_type = MT_NONE;
				object.size = 1; object.lifetime = object.lifeleft = 3;
				AutomatedCaptureLog("scene real weapon-init fixture variant=%d owner=%u template=%d definition=%d",variant,object.handle,variant < 2 ? sprite : model,definition);
			}
		}
		if (AutomatedCaptureFrameIndex() == 0 && FindArg("-scene-fragment-probe") && Viewer_object) {
			if (ROOMNUM_OUTSIDE(Viewer_object->roomnum)) Error("Fragment fixture requires an indoor save");
			auto& room = Rooms[Viewer_object->roomnum];
			if (!room.num_portals) Error("Fragment fixture requires a portal for the real glass-break producer");
			auto& portal = room.portals[0];
			portal.flags |= PF_RENDER_FACES;
			const int previous_highest = Highest_object_index;
			BreakGlassFace(&room,portal.portal_face);
			CreateSplintersFromBody(Player_object,10,3);
			unsigned shards = 0,splinters = 0;
			for (int i = 0; i <= Highest_object_index; ++i) {
				auto& piece = Objects[i];
				if (piece.type != OBJ_SHARD && piece.type != OBJ_SPLINTER) continue;
				// Stage/scalewise separate the real producer's results for inspection;
				// retain their UV/material/shape rather than fabricating GPU triangles.
				const bool shard = piece.type == OBJ_SHARD;
				const unsigned ordinal = shard ? shards++ : splinters++;
				if (shard) for (auto& point : piece.rtype.shard_info.points) point *= .08f;
				else for (auto& point : piece.ctype.splinter_info.verts) point *= .4f;
				auto position = Viewer_object->pos+Viewer_object->orient.fvec*12+
					Viewer_object->orient.rvec*((shard ? -5.5f : 1.5f)+(ordinal%3)*1.5f)+
					Viewer_object->orient.uvec*(3-int(ordinal/3)*1.1f);
				ObjSetPos(&piece,&position,Viewer_object->roomnum,nullptr,false);
				piece.movement_type = MT_NONE; piece.lifeleft = piece.lifetime = 3;
			}
			if (!shards || !splinters) Error("Real fragment producers did not both emit geometry");
			AutomatedCaptureLog("scene fragment fixture real_shards=%u real_splinters=%u previous_highest=%d",shards,splinters,previous_highest);
			if (FindArg("-scene-smolder-probe")) {
				auto position = Viewer_object->pos+Viewer_object->orient.fvec*25;
				const int index = CreateSmolderingObject(&position,SMOLDERING_INDEX,3,6,Viewer_object->roomnum);
				if (index < 0) Error("Smolder API fixture failed");
				ObjSetPos(&Objects[index],&position,Viewer_object->roomnum,nullptr,false);
				AutomatedCaptureLog("scene real smolder API fixture object=%d",index);
			}
		}
		if (FindArg("-scene-module-live-probe") && (AutomatedCaptureFrameIndex() == 0 ||
			(AutomatedCaptureFrameIndex() == 10 && FindArg("-scene-module-reload-probe")))) {
			if (Game_mode & GM_MULTI) Error("The module UI fixture must not start a network game");
			const bool entropy_probe = FindArg("-scene-module-entropy-probe") != 0;
			char module[16]; strcpy(module,entropy_probe ? "entropy" : FindArg("-scene-module-coop-probe") ? "coop" : "anarchy");
			if (!LoadGameDLL(module,entropy_probe ? 2 : 1)) Error("Could not load the shipped module for canvas qualification");
			// Real module records/callbacks, but no multiplayer transport. A remote
			// player avoids the local server's blocking wait-for-players screen.
			const int peer = Player_num == 1 ? 2 : 1;
			Players[peer].ship_index = Players[Player_num].ship_index;
			strcpy(Players[peer].callsign,"Canvas peer");
			NetPlayers[peer].flags = NPF_CONNECTED;
			NetPlayers[peer].sequence = NETSEQ_PLAYING;
			NetPlayers[peer].pilot_pic_id = 65535;
			auto position = Player_object->pos+Player_object->orient.fvec*10;
			const int object_index = AutomatedCaptureFrameIndex() == 0 ?
				ObjCreate(OBJ_PLAYER,peer,Player_object->roomnum,&position,&Player_object->orient) : Players[peer].objnum;
			if (object_index < 0) Error("Could not create the module UI fixture player");
			Players[peer].objnum = object_index;
			Objects[object_index].control_type = CT_NONE;
			Objects[object_index].movement_type = MT_NONE;
			dllinfo entry{}; entry.me_handle = Objects[object_index].handle; entry.it_handle = OBJECT_HANDLE_NONE;
			CallGameDLL(EVT_CLIENT_GAMEPLAYERENTERSGAME,&entry);
			dllinfo interval{}; interval.me_handle = interval.it_handle = OBJECT_HANDLE_NONE;
			CallGameDLL(EVT_GAME_INTERVAL,&interval);
			if (entropy_probe) {
				if (ROOMNUM_OUTSIDE(Player_object->roomnum) || FindTextureName("RedLab") < 0)
					Error("Entropy paint qualification needs an indoor room and authored RedLab texture");
				Rooms[Player_object->roomnum].flags |= RF_SPECIAL1;
				const auto before = GameSceneAppearanceRevision();
				CallGameDLL(EVT_CLIENT_GAMELEVELSTART,&interval);
				if (GameSceneAppearanceRevision() <= before) Error("Native Entropy level start did not notify room painting");
				AutomatedCaptureLog("scene native Entropy painted room=%d before=%llu after=%llu",Player_object->roomnum,
					static_cast<unsigned long long>(before),static_cast<unsigned long long>(GameSceneAppearanceRevision()));
			}
			if (FindArg("-scene-module-stats-probe")) {
				interval.input_key = KEY_F7;
				CallGameDLL(EVT_CLIENT_KEYPRESS,&interval);
			}
			if (FindArg("-scene-module-menu-probe")) {
				interval.input_key = KEY_F6;
				CallGameDLL(EVT_CLIENT_KEYPRESS,&interval);
			}
			AutomatedCaptureLog("scene loaded module UI fixture name=%s identity=%llu network_mode_unchanged=1",module,(unsigned long long)GameModuleCanvasIdentity());
		}
		if (AutomatedCaptureFrameIndex() == 10 && FindArg("-scene-module-entropy-probe")) {
			dllinfo level{}; level.me_handle = level.it_handle = OBJECT_HANDLE_NONE;
			const auto before = GameSceneAppearanceRevision();
			CallGameDLL(EVT_CLIENT_GAMELEVELSTART,&level);
			if (GameSceneAppearanceRevision() != before) Error("Unchanged Entropy repaint invalidated room appearance");
			AutomatedCaptureLog("scene native Entropy unchanged repaint appearance=%llu",static_cast<unsigned long long>(before));
		}
		if (AutomatedCaptureFrameIndex() == 20 && FindArg("-scene-overlay-events-probe")) {
			// Exercise the real queue and damage/energy/fusion producers in a
			// private fixed-input capture, not a fabricated GPU-only quad.
			Players[Player_num].damage_magnitude = MAX_DAMAGE_MAG*.5f;
			Players[Player_num].edrain_magnitude = MAX_EDRAIN_MAG*.5f;
			if (FindEventID(DAMAGE_EFFECT) < 0)
				CreateNewEvent(RENDER_EVENT,DAMAGE_EFFECT,0,nullptr,0,DoDamageEffect);
			if (FindEventID(EDRAIN_EFFECT) < 0)
				CreateNewEvent(RENDER_EVENT,EDRAIN_EFFECT,0,nullptr,0,DoEDrainEffect);
			float rgba[4] = {1,1,1,.1f};
			CreateNewEvent(RENDER_EVENT,FUSION_EFFECT,0,rgba,sizeof(rgba),DrawAlphaEvent);
		}
		if (SceneGeometryInspection() && FindArg("-scene-integration-effects-probe") && AutomatedCaptureFrameIndex() == 20 && Viewer_object) {
			const auto center = Viewer_object->pos+Viewer_object->orient.fvec*12;
			for (int i = 0; i < 3; ++i) {
				auto from = center-Viewer_object->orient.rvec*4+Viewer_object->orient.uvec*float(2-i*2);
				const int slot = VisEffectCreate(VIS_FIREBALL,i == 0 ? LIGHTNING_BOLT_INDEX : i == 1 ? GRAY_LIGHTNING_BOLT_INDEX : SINE_WAVE_INDEX,Viewer_object->roomnum,&from);
				if (slot < 0) Error("Integration line effect allocation failed");
				auto& effect = VisEffects[slot]; effect.end_pos = from+Viewer_object->orient.rvec*8;
				effect.lifeleft = effect.lifetime = 8; effect.flags = VF_USES_LIFELEFT | VF_EXPAND;
				effect.velocity = {2,1,0}; effect.lighting_color = GR_RGB16(220,240,255);
			}
			if (ROOMNUM_OUTSIDE(Viewer_object->roomnum)) Error("Integration Matcen fixture requires an indoor save");
			int type = -1;
			for (int i = 0; i <= Highest_object_index; ++i) if (Objects[i].type == OBJ_ROBOT) { type = Objects[i].id; break; }
			bool changed = false; char name[] = "IntegrationVisuals";
			const int index = CreateMatcen(name,&changed);
			if (index < 0 || type < 0) Error("Integration Matcen fixture has no producer/type");
			auto& producer = *Matcen[index];
			producer.SetAttachType(MT_ROOM); producer.SetAttach(Viewer_object->roomnum);
			producer.SetNumSpawnPnts(1); producer.SetSpawnPnt(0,0);
			producer.SetStatus(MSTAT_MANUAL_UPDATE_CREATE_PNT | MSTAT_NOT_HURT_PLAYER | MSTAT_ACTIVE,true);
			auto position = center-Viewer_object->orient.uvec*4;
			producer.SetCreatePnt(&position); producer.SetCreateRoom(Viewer_object->roomnum);
			producer.SetMaxProd(-1); producer.SetNumProdTypes(1);
			int priority = 100, maximum = -1; float delay = 0;
			producer.SetProdInfo(0,&type,&priority,&delay,&maximum);
			producer.SetPreProdTime(2); producer.Reset();
			producer.SetStatus(MSTAT_ACTIVE,true);
			producer.DoThinkFrame(); producer.AdvanceVisuals();
			MatcenVisualDisk disk;
			if (!producer.GetVisualDisk(disk)) Error("Integration Matcen failed to enter preproduction: attach=%d status=%d",producer.GetAttachType(),producer.GetStatus());
			AutomatedCaptureLog("scene integration Matcen created index=%d source_type=%d",index,type);
		}
		const auto& screen_overlays = AdvanceFrameOverlayEvents(d3scene::assets().lifetime(),uint64_t(FrameCount));
		if (!screen_overlays.empty() && AutomatedCapturePending())
			AutomatedCaptureLog("scene frame overlays logical_frame=%d count=%u queued=%d",FrameCount,
				(unsigned)screen_overlays.size(),Num_events);
		if (SceneGeometryInspection() && FindArg("-scene-integration-cinematic-probe") && AutomatedCaptureFrameIndex() == 20 && Viewer_object) {
			tGameCinematic info{};
			info.flags = GCF_USEPOINT | GCF_LAYOUT_MIDDLE | GCF_TEXT_FADEINOUT;
			info.target_objhandle = Player_object->handle; info.position = Viewer_object->pos;
			info.orient = &Viewer_object->orient; info.room = Viewer_object->roomnum;
			info.max_time_play = 1.5f; info.end_transition = GCTT_FADE;
			const int option = FindArg("-scene-integration-cinematic-probe");
			const char* transition = GetArg(option+1);
			if (transition && transition[0] >= '0' && transition[0] <= '4' && !transition[1])
				info.end_transition = transition[0]-'0';
			if (info.end_transition == GCTT_WACKY)
				info.flags = GCF_USEPOINT | GCF_LAYOUT_TOP | GCF_TEXT_WIPEIN;
			if (info.end_transition == GCTT_FADEINOUT) info.flags |= GCF_DOTRANSBEFORETEXT;
			info.text_display = info.player_disabled = info.in_camera_view = {0,1};
			info.track_target = info.quick_exit = {1,1};
			info.callback = [](int type) {
				static int last_frame = -1, calls = 0;
				if (type == GCCT_START) { last_frame = -1; calls = 0; }
				if (type == GCCT_FRAME) {
					if (last_frame == FrameCount) Error("Cinematic callback repeated within logical frame");
					last_frame = FrameCount; ++calls;
				}
				if (type != GCCT_FRAME || AutomatedCapturePending())
					AutomatedCaptureLog("scene cinematic callback type=%d logical_frame=%d calls=%d",type,FrameCount,calls);
			};
			char text[] = "Renderer integration|Cinematic presentation";
			if (!Cinematic_Start(&info,text)) Error("Integration cinematic did not start");
		}
		AdvanceCinematicPresentation(d3scene::assets().lifetime(),uint64_t(FrameCount));
		const auto main_view = CaptureGameMainView();
		const auto hud_layout = CaptureGameHUDLayout();
		if (FindArg("-scene-history-export-probe") && AutomatedCaptureFrameIndex() == 20) {
			const int save_arg = FindArg("-capture-save-output");
			const char* path = save_arg ? GetArg(save_arg+1) : nullptr;
			if (!path || !*path || !SaveGameState(path,"Renderer interleaved publication check"))
				Error("Interleaved save/export qualification failed");
			AutomatedCaptureLog("scene interleaved save exported before world frame=%d",FrameCount);
		}
		const auto presentation = SceneGeometryInspection() && FindArg("-scene-automap-probe") ?
			PrepareAutomapInspection() : PrepareGamePresentation(true,&main_view,&screen_overlays,&hud_layout);
		const ModuleSceneScope module_scene(presentation);
		SceneFeedbackTransaction feedback(*presentation);

		// Draw the big 3d view
		{
			PERF_MARKER_SCOPE("GameDrawMainView.Total");
			GameDrawMainView(*presentation,feedback,main_view);
		}
		rend_PerfGpuSceneMark(RENDERER_GPU_SCENE_AFTER_MAIN_VIEW);

		//Do the small views.  These should be before GameDrawHUD() for the small windows
		{
			PERF_MARKER_SCOPE("DrawSmallViews");
			DrawSmallViews(*presentation,feedback);
		}
		// One observation boundary after world views, before foreground canvases.
		// Observation commit is game-owned. Draw replay cannot update AI, map,
		// weather or effect clocks; those results use source simulation volumes.
		const auto& observations = feedback.seal();
		CommitGameVisibility(*presentation,observations,uint64_t(FrameCount));
		if (AutomatedCapturePending() || AutomatedCaptureFrameIndex() == 0)
			AutomatedCaptureLog("scene observations committed logical_frame=%d sequence=%llu predecessor=%llu automap=%u effects=%u objects=%u ai=%d terrain=%d main_present=%d main_exterior=%d main_owner=%u",
				FrameCount,(unsigned long long)observations.frame_sequence,(unsigned long long)presentation->data().temporal_predecessor,
				(unsigned)observations.automap_rooms.size(),(unsigned)observations.effect_rooms.size(),
				(unsigned)observations.objects.size(),AI_NumRendered,observations.terrain,
				observations.main_view_present,observations.main_exterior_visible,observations.main_camera_owner);
		rend_PerfGpuSceneMark(RENDERER_GPU_SCENE_AFTER_SMALL_VIEWS);
		static std::vector<d3scene::ViewImageRequest> module_images;
		module_images.clear();
		const auto overlay_presentation = FindArg("-scene-automap-probe") ? d3scene::PresentationRef{} :
			PrepareGameOverlayPresentation(*presentation,hud_layout,true,module_images);
		if (FindArg("-scene-module-live-probe") && FindArg("-scene-module-unload-probe") && AutomatedCaptureFrameIndex() == 9) {
			const auto identity = GameModuleCanvasIdentity();
			FreeGameDLL();
			if (GameModuleCanvasIdentity()) Error("Module canvas owner survived unload");
			// Execute this already-owned publication after callback code and its
			// source bitmaps have gone away. The next frame must have no callbacks.
			AutomatedCaptureLog("scene module fixture unloaded after overlay publication owner=%llu replay_sequence=%llu",
				(unsigned long long)identity,(unsigned long long)overlay_presentation->data().sequence);
		}
		for (auto& image : module_images) {
			auto plan = d3scene::planPlanarViews(*presentation,image.view,Render_preferred_state.msaa_samples);
			image.view = plan.consumer.view();
			plan.images.push_back(image);
			d3scene::renderViewImages(*presentation,plan.images);
		}
		if (SceneGeometryInspection() && FindArg("-scene-layer-probe"))
		{
			// Exercise the canvas composition boundary without invoking any
			// unmigrated cockpit model/gauge renderer or changing authored geometry.
			if (!rend_BeginCockpitFrame()) Error("Layer fixture could not begin composition");
			StartFrame(0,0,Max_window_w,Max_window_h,false);
			rend_SetZBufferState(0); rend_SetZBufferWriteMask(0);
			rend_SetLighting(LS_NONE); rend_SetTextureType(TT_FLAT);
			rend_SetOverlayType(OT_NONE); rend_SetAlphaType(AT_CONSTANT);
			rend_SetAlphaValue(128); rend_SetFlatColor(GR_RGB(0,180,220));
			g3Point corners[4] = {}; g3Point* points[4] = {&corners[0],&corners[1],&corners[2],&corners[3]};
			const float x[4] = {.6f,.9f,.9f,.6f}, y[4] = {.65f,.65f,.9f,.9f};
			for (int i = 0; i < 4; ++i) {
				corners[i].p3_sx = x[i]*Max_window_w; corners[i].p3_sy = y[i]*Max_window_h;
				corners[i].p3_flags = PF_PROJECTED;
			}
			rend_DrawPolygon2D(0,points,4);
			EndFrame(); rend_EndCockpitFrame();
			rend_SetZBufferState(1); rend_SetZBufferWriteMask(1);
			if (AutomatedCapturePending() || AutomatedCaptureFrameIndex() == 0)
				AutomatedCaptureLog("scene layer fixture composed");
		}

		if (SceneGeometryInspection()) RenderGameForegrounds(*presentation,d3scene::ForegroundPhase::WorldOverlay,overlay_presentation.get());

		// Do Cockpit/Hud
		// Cinematics own their immutable final canvas below.
		// Do not run either the primary or auxiliary gameplay HUD while one is active.
		if (!HUD_disabled && !Cinematic_inuse && !SceneGeometryInspection())
		{
			PERF_MARKER_SCOPE("GameDrawHud.Total");
			GameDrawHud();
		}
		rend_PerfGpuSceneMark(RENDERER_GPU_SCENE_AFTER_HUD);

		// Render Ingame Cinematics
		{
			PERF_MARKER_SCOPE("Scene.CinematicPresentation");
			RenderGameForegrounds(*presentation,d3scene::ForegroundPhase::PostHUD,overlay_presentation.get());
			CommitCinematicFrame(d3scene::assets().lifetime(),uint64_t(FrameCount));
			if (FindArg("-scene-integration-cinematic-probe"))
				CommitCinematicFrame(d3scene::assets().lifetime(),uint64_t(FrameCount)); // deliberate cadence replay
		}
		rend_PerfGpuSceneMark(RENDERER_GPU_SCENE_AFTER_CINEMATIC);

		// Process the debug visual graph
		if (!IsCleanGameplayScreenshotActive())
		{
			PERF_MARKER_SCOPE("DebugGraph_Render");
			DebugGraph_Render();
		}

		if (Display_renderer_stats && !IsCleanGameplayScreenshotActive())
		{
			PERF_MARKER_SCOPE("DisplayRendererStats");
			//display some rendering stats
			tRendererStats stats;
			rend_GetStatistics(&stats);
			grtext_SetFont(HUD_FONT);

			char buffer[128];
			int x, y, height;

			x = 15;
			y = 240;
			StartFrame(0, 0, Game_window_w, Game_window_h);
			rend_StartFrame(0, 0, Game_window_w, Game_window_h, 0);
			height = grfont_GetHeight(HUD_FONT) + 1;
			sprintf(buffer, "Polys=%d", stats.poly_count);
			RenderHUDText(GR_RGB(255, 40, 40), 255, 1, x, y, buffer); y += height;
			sprintf(buffer, "Verts=%d", stats.vert_count);
			RenderHUDText(GR_RGB(255, 40, 40), 255, 1, x, y, buffer); y += height;
			sprintf(buffer, "Uploads=%d", stats.texture_uploads);
			RenderHUDText(GR_RGB(255, 40, 40), 255, 1, x, y, buffer); y += height;
			grtext_Flush();
			EndFrame();
		}
		if (SceneGeometryInspection())
		{
			RenderGameForegrounds(*presentation,d3scene::ForegroundPhase::FinalOverlay,overlay_presentation.get());
		}
		rend_FlushTextLayer();
		rend_PerfGpuSceneMark(RENDERER_GPU_SCENE_AFTER_DEBUG);
	}

	//Do UI Frame
	if (Game_interface_mode == GAME_INTERFACE && !Menu_interface_mode)
	{
		PERF_MARKER_SCOPE("DoUIFrameWithoutInput");
		DoUIFrameWithoutInput();
		//rend_Flip();
	}
	rend_PerfGpuSceneMark(RENDERER_GPU_SCENE_AFTER_UI);

	//Done with the dynamic lights
	{
		PERF_MARKER_SCOPE("ClearDynamicLightmaps");
		ClearDynamicLightmaps();
	}

	//Increment frame count
	FrameCount++;
	//#ifdef GAMEGAUGE
	frames_one_second++;
	gamegauge_total_frames++;
	//#endif
		// Update our glows
	{
		PERF_MARKER_SCOPE("PostUpdateAllLightGlows");
		PostUpdateAllLightGlows();
	}

}

void GameProcessMusic()
{
	// the last game state in the sequencer.
	extern tGameState Last_game_state;

	if (Last_game_state == GAMESTATE_LVLSTART)
		Game_music_info.started_level = true;
	else
		Game_music_info.started_level = false;

	// player_damaged set in damage.cpp DecreasePlayerShields.

	if ((Players[Player_num].flags & PLAYER_FLAGS_DYING) || (Players[Player_num].flags & PLAYER_FLAGS_DEAD))
		Game_music_info.player_dead = true;
	else
		Game_music_info.player_dead = false;

	Game_music_info.n_hostiles = AI_NumHostileAlert;
	Game_music_info.frametime = Frametime;

	D3MusicDoFrame(&Game_music_info);
}

//variables for Frametime system

//timer_GetMSTime()
//float last_timer=0.0;
int timer_paused = 0;

static void PerfMarkersRecordFramePacingState(const char* phase)
{
	if (!Perf_markers_enabled)
		return;

	const double now = timer_GetTime64();
	char marker[160];
	snprintf(marker, sizeof(marker),
		"State.FramePacing.%s min=%.3fms frametime=%.3fms since_last=%.3fms paused=%d timer_paused=%d game_if=%d menu_if=%d",
		phase,
		Min_allowed_frametime * 1000.0,
		Frametime * 1000.0f,
		(now - last_timer) * 1000.0,
		Game_paused ? 1 : 0,
		timer_paused,
		Game_interface_mode,
		Menu_interface_mode ? 1 : 0);
	PerfMarkersRecordDuration(marker, PerfMarkersNow(), 0.0);
}

//Stop the Frametime clock
void StopTime()
{
	if (!timer_paused)
	{
		last_timer = timer_GetTime64() - last_timer;
		if (last_timer < 0)
		{
			//Int3();
			last_timer = 0;
		}
	}

	timer_paused++;
}

//Restart the Frametime clock
void StartTime()
{
	if (timer_paused == 0)
		return;

	timer_paused--;

	ASSERT(timer_paused >= 0);

	if (!timer_paused)
		last_timer = timer_GetTime64() - last_timer;
}

float Min_frametime = 500;
float Max_frametime = 0;
float Avg_frametime = 0;
unsigned int Frames_counted = 0;

//Compute how long last frame took
void CalcFrameTime(double current_timer)
{
	if (timer_paused)
		return;

	if (current_timer >= last_timer)
		Frametime = current_timer - last_timer;
	else
		Frametime = 0.0f;

	last_timer = current_timer;

	if (Min_frametime > Frametime)
	{
		mprintf((0, "This was the fastest frame yet!\n"));
		Min_frametime = Frametime - Demo_frame_ofs;
	}
	else if (Max_frametime < Frametime)
	{
		mprintf((0, "This was the slowest frame yet!\n"));
		Max_frametime = Frametime - Demo_frame_ofs;
	}
	Frames_counted++;
	Avg_frametime += (Frametime - Demo_frame_ofs);
#ifdef GAMEGAUGE
	if (1)
#else
	if (Game_gauge_do_time_test)
#endif
	{
		if (gamegauge_total_frames == 0)
			gamegauge_start_time = timer_GetTime();
		if ((timer_GetTime() - gamegauge_start_time) > 1.0)
		{
			//Skip the first frame, it's always more than one second
			if (gamegauge_total_frames > 1)
			{
				if (frames_one_second < min_one_second)
					min_one_second = frames_one_second;
				if (frames_one_second > max_one_second)
					max_one_second = frames_one_second;
				if (gamegauge_total_frame_seconds < GAMEGAUGE_MAX_LOG)
					gamegauge_fpslog[gamegauge_total_frame_seconds] = frames_one_second;
			}
			gamegauge_total_frame_seconds++;
			frames_one_second = 0;
			gamegauge_start_time = timer_GetTime();
		}
		gamegauge_total_time += Frametime;
	}
}


//	called before first call to StopTime, StartTime or CalcFrameTime
void InitFrameTime(void)
{
	if (timer_paused)
	{
		mprintf((1, "Timer paused in InitFrameTime()\n"));
	}
	last_timer = timer_GetTime64();
	timer_paused = 0;
}

//Pauses game
void PauseGame()
{
	mprintf((0, "Game paused.\n"));
	Game_paused = true;

	D3MusicPause();
	Sound_system.PauseSounds();
	StopTime();
}


//	resumes game operation.
void ResumeGame()
{
	StartTime();
	Game_paused = false;
	Sound_system.ResumeSounds();
	D3MusicResume();
	mprintf((0, "Game resumed.\n"));
}

//Data for terrain sound
terrain_sound_band Terrain_sound_bands[NUM_TERRAIN_SOUND_BANDS];
int Terrain_sound_handles[NUM_TERRAIN_SOUND_BANDS];
float Terrain_sound_fade;	//used for transitions between inside & outside

#define FADE_TIME	1.0

//Update the sound on the terrain
void UpdateTerrainSound()
{
	//Update fade in/out (for inside/outside transitions)
	if (OBJECT_OUTSIDE(Player_object))
	{
		if (Terrain_sound_fade < 1.0)
		{
			//fading in
			Terrain_sound_fade += Frametime / FADE_TIME;
			if (Terrain_sound_fade > 1.0)
				Terrain_sound_fade = 1.0;
		}
	}
	else
	{
		if (Terrain_sound_fade > 0.0f)
		{
			// Match the outdoor fade-in while transitioning back indoors.
			// StartTerrainSound still creates the loop at zero volume, so an
			// indoor restore cannot leak ambience before this path runs.
			Terrain_sound_fade -= Frametime / FADE_TIME;
			if (Terrain_sound_fade < 0.0f)
				Terrain_sound_fade = 0.0f;
		}
		else
			return;		//already faded out
	}

	//Get player altitude
	int alt = Player_object->pos.y / TERRAIN_HEIGHT_INCREMENT;
	if (alt > 255)
	{
		alt = 255;
	}

	//Update each sound
	for (int b = 0; b < NUM_TERRAIN_SOUND_BANDS; b++)
	{
		terrain_sound_band* tb = &Terrain_sound_bands[b];
		if (tb->sound_index != -1)
		{
			float volume;

			if ((alt < tb->low_alt) || (alt > tb->high_alt))
				volume = 0.0;
			else
				volume = tb->low_volume + ((tb->high_volume - tb->low_volume) * (alt - tb->low_alt) / (tb->high_alt - tb->low_alt));

			Sound_system.Update2dSound(Terrain_sound_handles[b], volume * Terrain_sound_fade, 0.0);
		}
	}
}

//Clear out all the terrain sound bands
void ClearTerrainSound()
{
	for (int b = 0; b < NUM_TERRAIN_SOUND_BANDS; b++) {
		Terrain_sound_bands[b].sound_index = -1;
		Terrain_sound_bands[b].low_alt = 0;
		Terrain_sound_bands[b].high_alt = 0;
		Terrain_sound_bands[b].low_volume = 0.0;
		Terrain_sound_bands[b].high_volume = 0.0;
	}
}

//Starts the sound on the terrain
void StartTerrainSound()
{
	// Restored games apply their final player position after level sounds are
	// created.  Always start terrain loops silent so that an indoor restore
	// cannot leak a buffer at the default Play2dSound volume.
	Terrain_sound_fade = 0.0f;

	//Start the sounds
	for (int b = 0; b < NUM_TERRAIN_SOUND_BANDS; b++)
	{
		terrain_sound_band* tb = &Terrain_sound_bands[b];
		if (tb->sound_index != -1)
			Terrain_sound_handles[b] = Sound_system.Play2dSound(tb->sound_index, 0.0f);
	}

	//Set the volumes
	UpdateTerrainSound();
}

//The main loop for D3.  It renders, gets input, etc. for one frame
void GameFrame(void)
{
	static bool options_capture_started = false;
	if (!options_capture_started && FindArg("-capture-options-output"))
	{
		options_capture_started = true;
		extern void OptionsMenu();
		OptionsMenu();
	}

	if (Menu_interface_mode || Game_interface_mode == GAME_OPTIONS_INTERFACE)
	{
		int menu_frame_limit = GetFrameLimitFps();
		if (menu_frame_limit < 60)
			menu_frame_limit = 60;
		ui_SetFrameRateLimit(menu_frame_limit);
	}

#ifdef USE_RTP
	INT64 curr_time;
#endif
	BeginAutomatedCaptureFrame();
	InitRendererFramePacing();
	const bool automated_perf_active = Automated_capture.gameplay_frame_active &&
		AutomatedFramePerfEnabled();
	const double automated_perf_frame_begin = automated_perf_active ? timer_GetTime64() : 0.0;
	static double automated_perf_previous_frame_begin = 0.0;
	static double automated_perf_previous_defer_ms = 0.0;
	const double automated_perf_frame_start_interval_ms = automated_perf_active &&
		automated_perf_previous_frame_begin > 0.0 ?
		(automated_perf_frame_begin - automated_perf_previous_frame_begin) * 1000.0 : 0.0;
	if (automated_perf_active)
		automated_perf_previous_frame_begin = automated_perf_frame_begin;
	double automated_perf_pacing_wait_ms = 0.0;
	const double automated_perf_simulation_begin = automated_perf_frame_begin;
	double automated_perf_render_begin = automated_perf_simulation_begin;
	double automated_perf_render_end = automated_perf_simulation_begin;
	unsigned long long automated_perf_cycles_begin = 0;
	long long automated_perf_render_cycles = -1;
	bool automated_perf_cycles_valid = false;
	double automated_perf_present_begin = automated_perf_simulation_begin;
	double automated_perf_present_end = automated_perf_simulation_begin;
	double automated_perf_pre_cap_end = automated_perf_simulation_begin;
	double automated_perf_cap_begin = automated_perf_simulation_begin;
	double automated_perf_cap_end = automated_perf_simulation_begin;
	double frame_pacing_work_ms = 0.0;
	double frame_pacing_effective_interval_ms = Min_allowed_frametime * 1000.0;
	renderer_frame_pacing_info frame_pacing_info = {};

	bool is_game_idle = !Descent->active();

	if (Tracking_FVI)
	{
		mprintf((0, "Beginning frame!\n"));
	}

	// Begin Gameloop stuff
	Physics_normal_counter = 0;
	Physics_normal_looping_counter = 0;
	Physics_walking_counter = 0;
	Physics_walking_looping_counter = 0;
	Physics_vis_counter = 0;

	FVI_counter = 0;
	FVI_room_counter = 0;

#ifdef _DEBUG
	// Dump networking stats to virtual window
	tNetworkStatus netstat;
	static tNetworkStatus old_netstat;
	static bool netstat_init = false;
	static float netstat_time = 0;

	nw_GetNetworkStats(&netstat);
	mprintf_at((5, 0, 0, "TCP/IP Network Stats:"));
	mprintf_at((5, 1, 0, "TCP: tx: %d/%d rtx: %d/%d rx: %d/%d", netstat.tcp_total_packets_sent, netstat.tcp_total_bytes_sent, netstat.tcp_total_packets_resent, netstat.tcp_total_bytes_resent, netstat.tcp_total_packets_rec, netstat.tcp_total_bytes_rec));
	mprintf_at((5, 2, 0, "UDP: tx: %d/%d rx: %d/%d", netstat.udp_total_packets_sent, netstat.udp_total_bytes_sent, netstat.udp_total_packets_rec, netstat.udp_total_bytes_rec));

	mprintf_at((6, 0, 0, "IPX/SPX Network Stats:"));
	mprintf_at((6, 1, 0, "SPX: tx: %d/%d rtx: %d/%d rx: %d/%d", netstat.spx_total_packets_sent, netstat.spx_total_bytes_sent, netstat.spx_total_packets_resent, netstat.spx_total_bytes_resent, netstat.spx_total_packets_rec, netstat.spx_total_bytes_rec));
	mprintf_at((6, 2, 0, "IPX: tx: %d/%d rx: %d/%d", netstat.ipx_total_packets_sent, netstat.ipx_total_bytes_sent, netstat.ipx_total_packets_rec, netstat.ipx_total_bytes_rec));

	if (!netstat_init)
	{
		netstat_time = timer_GetTime();

		old_netstat = netstat;
		netstat_init = true;
	}
	else
	{
		float newt = timer_GetTime();
		if (netstat_time + 0.5f <= newt)
		{
			//time to update
			float tcp_rx, tcp_rtx, tcp_tx;
			float udp_rx, udp_tx;
			float ipx_rx, ipx_tx;
			float spx_rx, spx_tx, spx_rtx;
			float time_diff = newt - netstat_time;

			tcp_rx = ((float)(netstat.tcp_total_bytes_rec - old_netstat.tcp_total_bytes_rec)) / time_diff;
			tcp_tx = ((float)(netstat.tcp_total_bytes_sent - old_netstat.tcp_total_bytes_sent)) / time_diff;
			tcp_rtx = ((float)(netstat.tcp_total_bytes_resent - old_netstat.tcp_total_bytes_resent)) / time_diff;

			udp_rx = ((float)(netstat.udp_total_bytes_rec - old_netstat.udp_total_bytes_rec)) / time_diff;
			udp_tx = ((float)(netstat.udp_total_bytes_sent - old_netstat.udp_total_bytes_sent)) / time_diff;

			spx_rx = ((float)(netstat.spx_total_bytes_rec - old_netstat.spx_total_bytes_rec)) / time_diff;
			spx_tx = ((float)(netstat.spx_total_bytes_sent - old_netstat.spx_total_bytes_sent)) / time_diff;
			spx_rtx = ((float)(netstat.spx_total_bytes_resent - old_netstat.spx_total_bytes_resent)) / time_diff;

			ipx_rx = ((float)(netstat.ipx_total_bytes_rec - old_netstat.ipx_total_bytes_rec)) / time_diff;
			ipx_tx = ((float)(netstat.ipx_total_bytes_sent - old_netstat.ipx_total_bytes_sent)) / time_diff;

			mprintf_at((5, 3, 0, "TCP/s: TX: % 5.1f RTX: % 5.1f RX: % 5.1f", tcp_tx, tcp_rtx, tcp_rx));
			mprintf_at((5, 4, 0, "UDP/s: TX: % 5.1f RX: % 5.1f", udp_tx, udp_rx));
			mprintf_at((6, 3, 0, "SPX/s: TX: % 5.1f RTX: % 5.1f RX: % 5.1f", spx_tx, spx_rtx, spx_rx));
			mprintf_at((6, 4, 0, "IPX/s: TX: % 5.1f RX: % 5.1f", ipx_tx, ipx_rx));

			old_netstat = netstat;
			netstat_time = newt;
		}
	}

#endif
	// Do our first quaterframe of IntelliVIBE
	VIBE_DoQuaterFrame(true);

	// clear out music struct
	memset(&Game_music_info, 0, sizeof(Game_music_info));

	// if in user interface, clear screen every frame for small game windows.
	if (Game_interface_mode != GAME_INTERFACE || Menu_interface_mode)
		Clear_screen = 4;

	// Setup sound for new frame
	Sound_system.BeginSoundFrame();

#ifdef USE_RTP
	RTP_GETCLOCK(curr_time);
#endif

	const bool advance_simulation = !Game_paused || Demo_do_one_frame;
	if (advance_simulation)
	{
		//if we are recording a demo, all moved objects will be written
		if (!Skip_render_game_frame)
			DemoWriteChangedObjects();
		// Demo Frame (do this first so all subsequent demo items are marked for this frame)
		if (!Skip_render_game_frame)
			DemoStartNewFrame();
		if (Demo_flags == DF_PLAYBACK)
			DemoFrame();

		if (!is_game_idle)
		{
			//Get and process keys
			RTP_tSTARTTIME(processkeys_time, curr_time);
			ProcessKeys();
			ProcessButtons();
			RTP_tENDTIME(processkeys_time, curr_time);
		}

		//Global AI Frame Stuff  -- must be before ObjMoveAll
		RTP_tSTARTTIME(aiframeall_time, curr_time);
		if (DoAI)
			AIFrameAll();

		a_life.DoFrame();
		RTP_tENDTIME(aiframeall_time, curr_time);

		//ApplyShadowsToRooms ();

		//Move objects for this frame
		RTP_tSTARTTIME(objframe_time, curr_time);
		ObjDoFrameAll();
		const auto visual_emissions = AdvanceGameVisualEffects(d3scene::assets().lifetime(),uint64_t(FrameCount),false);
		if (FindArg("-scene-emitter-replay-probe") &&
			AdvanceGameVisualEffects(d3scene::assets().lifetime(),uint64_t(FrameCount),false).advanced)
			Error("Game-owned emitter advanced twice in one logical frame");
		if (FindArg("-scene-object-emissions-probe") && AutomatedCaptureFrameIndex() >= 0) {
			static GameVisualEmissionStats totals;
			if (AutomatedCaptureFrameIndex() == 0) totals = {};
			totals.sparkles += visual_emissions.sparkles; totals.damage += visual_emissions.damage; totals.virus += visual_emissions.virus;
			if (AutomatedCapturePending()) AutomatedCaptureLog("game visual fixture totals sparkles=%u damage=%u virus=%u",
				totals.sparkles,totals.damage,totals.virus);
		}
		if (AutomatedCapturePending() && (visual_emissions.sparkles || visual_emissions.damage || visual_emissions.virus))
			AutomatedCaptureLog("game visual emissions frame=%d objects=%u sparkles=%u damage=%u virus=%u",FrameCount,
				visual_emissions.objects,visual_emissions.sparkles,visual_emissions.damage,visual_emissions.virus);
		RTP_tENDTIME(objframe_time, curr_time);

		RTP_tSTARTTIME(matcenframe_time, curr_time);
		DoMatcensFrame();
		RTP_tENDTIME(matcenframe_time, curr_time);

		// Checks for goal completion
		RTP_tSTARTTIME(levelgoal_time, curr_time);
		Level_goals.DoFrame();
		RTP_tENDTIME(levelgoal_time, curr_time);

		//Do doorways
		RTP_tSTARTTIME(doorframe_time, curr_time);
		DoorwayDoFrame();
		RTP_tENDTIME(doorframe_time, curr_time);

		//	Do player frame
		RTP_tSTARTTIME(playerframe_time, curr_time);
		DoPlayerFrame();
		RTP_tENDTIME(playerframe_time, curr_time);

		// Do our second quaterframe of IntelliVIBE
		VIBE_DoQuaterFrame(false);

		// Weather frame
		RTP_tSTARTTIME(weatherframe_time, curr_time);
		if (FindArg("-scene-sky-lightning-probe") && AutomatedCaptureFrameIndex() == 20) {
			SetLightningState(1,0,0x7fff); // real weather trigger on its next evaluation
		}
		DoWeatherForFrame();
		if (FindArg("-scene-sky-lightning-probe") && AutomatedCaptureFrameIndex() == 20)
			Weather.lightning_interval_time = 10000;
		AdvanceWeatherSkyVisuals(d3scene::assets().lifetime(),uint64_t(FrameCount),false);
		if (FindArg("-scene-emitter-replay-probe")) {
			const auto before = GetWeatherSkyVisuals(d3scene::assets().lifetime());
			const unsigned phase = before ? before->phase : 0, count = before ? before->count : 0;
			AdvanceWeatherSkyVisuals(d3scene::assets().lifetime(),uint64_t(FrameCount),false);
			const auto after = GetWeatherSkyVisuals(d3scene::assets().lifetime());
			if (!after || after->phase != phase || after->count != count) Error("Sky weather replay advanced");
		}
		RTP_tENDTIME(weatherframe_time, curr_time);

		// Ambient sounds
		RTP_tSTARTTIME(ambsound_frame_time, curr_time);
		DoAmbientSounds();
		RTP_tENDTIME(ambsound_frame_time, curr_time);

		//Terrain sound
		UpdateTerrainSound();


		//Call the interval script for the level script
		//@$-D3XExecScript(Current_level->d3xthread, Current_mission.cur_level, REF_LEVELTYPE, EVT_INTERVAL, 0, 0);
		tOSIRISEventInfo ei;
		ei.evt_interval.frame_time = Frametime;
		ei.evt_interval.game_time = Gametime;
		Osiris_CallLevelEvent(EVT_INTERVAL, &ei);
		Osiris_ProcessTimers();

		// Process any in-game cinematics
		Cinematic_Frame();
		AdvancePlayerCameraShake(Frametime);

		// Do our third quaterframe of IntelliVIBE
		VIBE_DoQuaterFrame(false);

	}
	else
	{
		// Do our second quaterframe of IntelliVIBE
		VIBE_DoQuaterFrame(false);
		Sleep(3);
		// Do our third quaterframe of IntelliVIBE
		VIBE_DoQuaterFrame(false);
	}

	// do music always.
	RTP_tSTARTTIME(musicframe_time, curr_time);
	GameProcessMusic();
	RTP_tENDTIME(musicframe_time, curr_time);

	if ((Demo_paused) && (Demo_flags == DF_PLAYBACK) && !is_game_idle)
	{
		Demo_do_one_frame = false;
		ProcessKeys();
	}

	// Always do multiplayer frames before render frame
	// 
	// Do multiplayer stuff
	RTP_tSTARTTIME(multiframe_time, curr_time);
	MultiDoFrame();
	RTP_tENDTIME(multiframe_time, curr_time);
	if (advance_simulation) {
		if (AutomatedCaptureFrameIndex() >= 0 && FindArg("-scene-electrical-probe") && Player_object) {
			const int omega = FindWeaponName("Omega");
			if (omega < 0 || FireWeaponFromObject(Player_object,omega,0,true,false) < 0)
				Error("Native electrical firing fixture failed");
			if (FindArg("-scene-electrical-world-probe")) {
				static int emitter = OBJECT_HANDLE_NONE;
				if (AutomatedCaptureFrameIndex() == 0) {
					auto position = Player_object->pos+Player_object->orient.fvec*15+Player_object->orient.rvec*5;
					const int index = ObjCreate(OBJ_ROBOT,GENOBJ_GUIDEBOT,Player_object->roomnum,&position,&Player_object->orient);
					if (index < 0) Error("Native electrical remote source fixture failed");
					auto& source = Objects[index]; source.control_type = CT_NONE; source.movement_type = MT_NONE;
					emitter = source.handle;
				}
				auto* source = ObjGet(emitter);
				if (!source || FireWeaponFromObject(source,omega,0,true,false) < 0)
					Error("Native remote electrical firing failed");
			}
		}
		const auto pulses = AdvanceGameElectricalWeapons(d3scene::assets().lifetime(),uint64_t(FrameCount),false);
		if (pulses && AutomatedCapturePending()) AutomatedCaptureLog("game resolved electrical pulses=%u",pulses);
		if (FindArg("-scene-emitter-replay-probe") &&
			AdvanceGameElectricalWeapons(d3scene::assets().lifetime(),uint64_t(FrameCount),false))
			Error("Electrical pulse advanced twice in one logical frame");
	}

	//Do Gamespy stuff
	gspy_DoFrame();

	// Do our fourth quaterframe of IntelliVIBE
	VIBE_DoQuaterFrame(false);

	// Make the current simulation state recoverable before graphics-driver work.
	// This is a stdio flush, not a physical disk sync, so it avoids stalling on
	// storage while ensuring a process or driver failure does not strand it in
	// the C runtime's write buffer.
	DemoFlushRecording();

#ifdef USE_RTP
	RTP_GETCLOCK(curr_time);	//update the current time, since something has happened since ENDFTIME
#endif

	RTP_tSTARTTIME(renderframe_time, curr_time);
	if (automated_perf_active && FindArg("-capture-thread-cycles"))
		automated_perf_cycles_valid = timer_GetThreadCycles(automated_perf_cycles_begin);
	if (automated_perf_active)
		automated_perf_render_begin = timer_GetTime64();
	if (Clean_screenshot_requested && !Skip_render_game_frame && !Dedicated_server)
	{
		Clean_screenshot_requested = false;
		Clean_screenshot_active = true;
	}
	if (!Skip_render_game_frame)
		//Render the frame
		GameRenderFrame();

	if (!Skip_render_game_frame && !Dedicated_server && (Game_interface_mode != GAME_INTERFACE || Menu_interface_mode))
	{
		if (rend_BeginPostPresentFrame())
		{
			GameDrawPostPresentFrame(true);
		}
	}
	if (Clean_screenshot_active &&
		(Game_interface_mode != GAME_INTERFACE || Menu_interface_mode))
	{
		CaptureCleanGameplayScreenshot();
	}
	if (automated_perf_active)
		automated_perf_render_end = timer_GetTime64();
	if (automated_perf_cycles_valid) {
		unsigned long long end = 0;
		if (timer_GetThreadCycles(end) && end >= automated_perf_cycles_begin)
			automated_perf_render_cycles = static_cast<long long>(end-automated_perf_cycles_begin);
	}

	RTP_tENDTIME(renderframe_time, curr_time);

	if (!Game_paused)
	{
		// Do pending events
		RTP_tSTARTTIME(normalevent_time, curr_time);
		ProcessNormalEvents();
		RTP_tENDTIME(normalevent_time, curr_time);

		bool post_present_frame_ready = false;
		bool fallback_present = false;

		// Fully compose the frame before waiting for its presentation deadline. The
		// final backbuffer blit and SwapBuffers remain pending until after the wait,
		// so variations in simulation/render time do not move the visible present.
		if (!Skip_render_game_frame && !Dedicated_server)
		{
			if (Game_interface_mode == GAME_INTERFACE && !Menu_interface_mode)
			{
				PERF_MARKER_SCOPE("Renderer.PreparePresent");
				if (rend_BeginPostPresentFrame())
				{
					GameDrawPostPresentFrame(false);
					CaptureAutomatedFrameIfRequested();
					CaptureCleanGameplayScreenshot();
					if (Screenshot_requested)
					{
						DoScreenshot();
						Screenshot_requested = false;
					}
					post_present_frame_ready = true;
				}
				else
				{
					CaptureAutomatedFrameIfRequested();
					CaptureCleanGameplayScreenshot();
					if (Screenshot_requested)
					{
						DoScreenshot();
						Screenshot_requested = false;
					}
					fallback_present = true;
				}
			}
		}
		PerfMarkersRecordFramePacingState("BeforeFramecap");
		const double frame_work_end = timer_GetTime64();
		frame_pacing_work_ms = (std::max)(0.0, frame_work_end - last_timer) * 1000.0;
		if (automated_perf_active)
			automated_perf_pre_cap_end = frame_work_end;
		rend_GetFramePacingInfo(&frame_pacing_info);
		if (automated_perf_active)
			automated_perf_cap_begin = timer_GetTime64();

		//float start_delay = timer_GetTime();
		//Slow down the game if the user asked us to
		const double effective_interval = Min_allowed_frametime;
		frame_pacing_effective_interval_ms = effective_interval * 1000.0;

		// Make room in the bounded GPU queue before submitting this frame. Waiting
		// after SwapBuffers delayed the next simulation step instead, which made the
		// present intervals alternate short/long and fed that oscillation back into
		// the variable simulation delta.
		automated_perf_pacing_wait_ms = rend_WaitForFramePacing();
		double current_timer = timer_GetTime64();
		double target_time = last_timer + effective_interval;
		if (current_timer > target_time)
		{
			target_time = current_timer;
		}

		const bool renderer_schedules_present = post_present_frame_ready &&
			rend_SchedulePresent(effective_interval);
		if (!renderer_schedules_present && current_timer < target_time)
		{
			const double wait_time = target_time - current_timer;
			if (wait_time > 0.0)
			{
				unsigned int sleep_time = (unsigned int)(wait_time * 1000.0);
				if (Dedicated_server)
				{
					Sleep(sleep_time);
				}
				else if (sleep_time > 2)
				{
					Sleep(sleep_time - 2);
				}
			}
			while (timer_GetTime64() < target_time) {}
		}
		if (post_present_frame_ready || fallback_present)
		{
			if (automated_perf_active)
				automated_perf_present_begin = timer_GetTime64();
			{
				PERF_MARKER_SCOPE("Renderer.Flip");
				PerfMarkersRecordFramePacingState("BeforeFlip");
				if (post_present_frame_ready)
					rend_EndPostPresentFrame();
				else
					rend_Flip();
			}
			if (automated_perf_active)
				automated_perf_present_end = timer_GetTime64();
		}
		if (automated_perf_active)
			automated_perf_cap_end = timer_GetTime64();

		PerfMarkersEndFrame();
		// Use the real submission time. Using the ideal deadline hid overshoot from
		// both simulation and the FPS display and encouraged a short corrective
		// interval after a late frame.
		CalcFrameTime(timer_GetTime64());
		rend_GetFramePacingInfo(&frame_pacing_info);
		if (post_present_frame_ready &&
			frame_pacing_info.latest_present_interval_ms > 0.0 &&
			frame_pacing_info.latest_present_interval_ms < 1000.0)
		{
			Frametime = (float)(
				frame_pacing_info.latest_present_interval_ms / 1000.0);
		}
		if (Automated_capture.gameplay_frame_active && !Automated_capture.realtime)
			Frametime = Automated_capture.fixed_delta;

		//Update Gametime
		Gametime += Frametime;

		static int graph_id = -2;
		if (graph_id == -2)
		{
			graph_id = DebugGraph_Add((float)0.0f, (float)260.0f, "Framerate");
		}
		if (graph_id >= 0 && Frametime > 0)
		{
			DebugGraph_Update(graph_id, 1.0f / Frametime);
		}
	}
	else
	{
		// Paused frames can still present UI or forced captures. Keep those
		// submissions bounded even though they do not advance game time.
		rend_WaitForFramePacing();
		PerfMarkersEndFrame();
	}



	// End Gameloop Loop stuff
	Sound_system.EndSoundFrame();
	DoDestroyedLightsForFrame();

	// Clear lod stuff
	ClearLODOffs();

	static int fvi_graph_id = -2;
	if (fvi_graph_id == -2)
		fvi_graph_id = DebugGraph_Add(0, 1000, "FVI Calls");
	if (fvi_graph_id >= 0)
		DebugGraph_Update(fvi_graph_id, FVI_counter);

#ifdef USE_RTP
	RTP_RECORDVALUE(frame_time, Frametime);
	rtp_RecordFrame();

	/*
		if (frame_info.ai_time>.1)
			mprintf ((0,"NOTE: AI frame took longer than .1 seconds! %f\n",frame_info.ai_time));
		if (frame_info.render_time>.1)
			mprintf ((0,"NOTE: Render frame took longer than .1 seconds! %f\n",frame_info.render_time));
		if (frame_info.multi_time>.1)
			mprintf ((0,"NOTE: Multi frame took longer than .1 seconds! %f\n",frame_info.multi_time));
		if (frame_info.obj_time>.1)
			mprintf ((0,"NOTE: Object frame took longer than .1 seconds! %f\n",frame_info.obj_time));
	*/
#endif

	mprintf_at((1, 0, 39, "Pn %05d, L %05d", Physics_normal_counter, Physics_normal_looping_counter));
	mprintf_at((1, 1, 39, "Pw %05d, L %05d", Physics_walking_counter, Physics_walking_looping_counter));
	mprintf_at((1, 2, 39, "Pv %05d", Physics_vis_counter));
	mprintf_at((1, 3, 39, "Fc %05d, R %05d", FVI_counter, FVI_room_counter));

#ifdef D3_FAST
	if (FrameCount > 20)
		SetFunctionMode(MENU_MODE);
	else {
		int key;
		int rand_num = ps_rand() % 4;

		if (rand_num == 0)
			key = KEY_LCTRL;
		else if (rand_num == 1)
			key = KEY_SPACEBAR;
		else if (rand_num == 2)
			key = KEY_A;
		else
			key = KEY_Z;

		ddio_UpdateKeyState(key, timer_GetTime(), 1);
		ddio_UpdateKeyState(key, timer_GetTime() + .5f, 0);
	}
#endif

	if (Tracking_FVI)
		mprintf((0, "Ending frame!\n"));

	if (automated_perf_active)
	{
		tRendererStats renderer_stats = {};
		rend_GetStatistics(&renderer_stats);
		AutomatedFramePerfLog(Automated_capture.gameplay_frame,
			automated_perf_frame_start_interval_ms,
			automated_perf_pacing_wait_ms,
			(automated_perf_render_begin - automated_perf_simulation_begin) * 1000.0,
			(automated_perf_render_end - automated_perf_render_begin) * 1000.0,
			(automated_perf_present_end - automated_perf_present_begin) * 1000.0,
			(automated_perf_pre_cap_end - automated_perf_present_end) * 1000.0,
			(automated_perf_cap_end - automated_perf_cap_begin) * 1000.0,
			(automated_perf_cap_end - automated_perf_frame_begin) * 1000.0,
			Frametime * 1000.0f, frame_pacing_work_ms,
			frame_pacing_effective_interval_ms, automated_perf_previous_defer_ms,
			frame_pacing_info, renderer_stats, automated_perf_render_cycles);
	}

	EndAutomatedCaptureFrame();

	if (!is_game_idle)
	{
		const double defer_start = automated_perf_active ? timer_GetTime64() : 0.0;
		Descent->defer();
		if (automated_perf_active)
			automated_perf_previous_defer_ms =
				(timer_GetTime64() - defer_start) * 1000.0;
	}
	else if (automated_perf_active)
	{
		automated_perf_previous_defer_ms = 0.0;
	}
}


// game menu functions
void StartGameMenu()
{
	SetUICallback(GameFrame);
	SuspendControls();
	Menu_interface_mode = true;

	// reset bail flag.
	Multi_bail_ui_menu = false;

	if (!(Game_mode & GM_MULTI))
		PauseGame();

	ui_ShowCursor();
}


void EndGameMenu()
{
	ui_HideCursor();
	ui_SetFrameRateLimit(60);

	if (!(Game_mode & GM_MULTI))
		ResumeGame();

	Menu_interface_mode = false;
	if (ConsumeAltF4QuitConfirmationRequest())
		Game_interface_mode = GAME_QUIT_CONFIRM;

	ResumeControls();
	SetUICallback(NULL);
	Clear_screen = 4;
}
