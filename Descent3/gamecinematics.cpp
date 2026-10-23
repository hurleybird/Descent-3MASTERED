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

#include "pstypes.h"
#include "pserror.h"
#include "grtext.h"
#include "renderer.h"
#include "gamecinematics.h"
#include "vecmat.h"
#include "mem.h"
#include "gamepath.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "game.h"
#include "AIGoal.h"
#include "aipath.h"
#include "objinfo.h"
#include "player.h"
#include "ddio.h"
#include "gamefont.h"
#include "gameloop.h"
#include "AIMain.h"
#include "controls.h"
#include "d3music.h"
#include "gamesequence.h"
#include "demofile.h"
#include "osiris_dll.h"
#include "multi.h"
#include "weapon.h"
#include "SceneCanvas.h"
#include "SceneExtraction.h"
#include "gamefont.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

#ifdef _DEBUG
bool Cinematics_enabled = true;
extern int DoAI;
#ifdef EDITOR
extern bool Game_being_played_from_quick_play;
#endif
#endif
extern unsigned short Demo_obj_map[MAX_OBJECTS];

struct
{
	// some important, pre-computed times
	float text_display_start, text_display_stop;
	float track_start, track_stop;
	float player_start, player_stop;
	float camera_start, camera_stop;
	float exit_start, exit_end;
	float end_time;

	void (*callback)(int type);

	int pathnum;
	int room;
	vector position;

	int target_objhandle;
	int camera_objhandle;

	int longest_line;

	uint flags;

	tHUDMode old_hudmode;

	int num_lines;
	int end_transition_type;	//what kind of ending transition should the cinematic have
	int start_transition_type;	//what kind of starting transition should the cinematic have
	char* text;
	bool target_triggered;	//whether the notify event has been sent to the object yet

	bool doing_end_transition;	//we're processing an end transition still
	float end_transition_start_time;	//Gametime that the end transition started
	float end_transition_end_time;	//Gametime that the end transition should end

	float start_transition_start_time;
	float start_transition_end_time;

	//some transition data
	float early_trans_end_start_time;

	bool controls_suspended;
} GameCinema;
static uint64_t CinematicGeneration = 0;

#define CDI_NOT_CANNED		0
#define CDI_CANNED			1

struct tCinematicDemoInfo
{
	ubyte cinematic_type;	//canned/not canned	
	int camera_objhandle;	//object handle for camera to use

	char* text_string;		//not canned text string

	union
	{
		tCannedCinematicInfo* canned_data;	//data to start a canned cinematic
		tGameCinematic* cinematic_data;		//data to start a regular cinematic
	};

};

void Cinematic_WriteDemoFileData(tCinematicDemoInfo* info);
bool Cinematic_StartCine(tGameCinematic* info, char* text_string, int camera_objhandle);

bool Cinematic_inuse;
tGameCinematic Cinematic_fake_info;
bool Cinematic_fake_queued = false;

int gc_wacky_bmp_handle = BAD_BITMAP_HANDLE;

#define END_TRANSITION_TIME		1.0f

float Cine_GetPathTravelSpeed(int pathnum, float time);

//	Returns the hud mode before cinematics
tHUDMode Cinematic_GetOldHudMode(void)
{
	return GameCinema.old_hudmode;
}

//	Cinematic_Close
//
//	Closes the in-game cinematics
void Cinematic_Close(void)
{
	if (Cinematic_inuse) {
		Cinematic_Stop();
	}

	if (gc_wacky_bmp_handle > BAD_BITMAP_HANDLE)
	{
		bm_FreeBitmap(gc_wacky_bmp_handle);
		gc_wacky_bmp_handle = BAD_BITMAP_HANDLE;
	}

	GameCinema.doing_end_transition = false;
}

//	Cinematic_Init
//
//	Initializes the in-game cinematics
void Cinematic_Init(void)
{
	static bool called_once = false;
	Cinematic_inuse = false;

	memset(&GameCinema, 0, sizeof(GameCinema));

	GameCinema.doing_end_transition = false;
	gc_wacky_bmp_handle = bm_AllocLoadFileBitmap("ShieldBitmap.ogf", 0);

	if (!called_once)
	{
		atexit(Cinematic_Close);
		called_once = true;
	}
}

void Cinematic_LevelInit(void)
{
	Cinematic_inuse = false;
	Cinematic_fake_queued = false;
	GameCinema.doing_end_transition = false;
}

inline void verify_percentranage(PercentageRange* range)
{
	if (range->min < 0.0f) range->min = 0.0f;
	if (range->min > 1.0f)	range->min = 1.0f;
	if (range->max < 0.0f)	range->max = 0.0f;
	if (range->max > 1.0f)	range->max = 1.0f;

	if (range->max < range->min) {
		float temp = range->max;
		range->max = range->min;
		range->min = temp;
	}
}

// Cinematic_PerformFake
//
//	Performs a fake cinematic...just calls certain functions that may need to be
//	called for certain things to happen.  For instance, some bosses are triggered when
//	their boss intro cam is started, but in multiplayer there is no cinematics, so calling 
//	this would do what's needed.
void Cinematic_PerformFake(void)
{
	Cinematic_fake_queued = false;
	mprintf((0, "Cinematics: Faking Cinematic\n"));
	object* target = ObjGet(Cinematic_fake_info.target_objhandle);
	if (!target)
	{
		mprintf((0, "Cinematics: Invalid target...can't fake\n"));
		return;
	}

	//tell the target it's the focus of a cinematic
	AINotify(target, AIN_MOVIE_START, NULL);
	AINotify(target, AIN_MOVIE_END, NULL);

	if (target->type == OBJ_PLAYER)
	{
		tOSIRISEventInfo ei;

		Osiris_CallLevelEvent(EVT_PLAYER_MOVIE_START, &ei);
		Osiris_CallLevelEvent(EVT_PLAYER_MOVIE_END, &ei);
	}
}

void Cinematic_SetForFakeCinematic(tGameCinematic* info)
{
	ASSERT(Cinematic_fake_queued == false);	//ack trying to play 2 cinematics in one frame!?

	memcpy(&Cinematic_fake_info, info, sizeof(tGameCinematic));
	Cinematic_fake_queued = true;
}

inline int Cinematics_CreateCamera(void)
{
	int objnum = ObjCreate(OBJ_CAMERA, 0, Player_object->roomnum, &Player_object->pos, NULL);
	if (objnum == -1)
		return OBJECT_HANDLE_NONE;

	if (Demo_flags == DF_RECORDING)
	{
		DemoWriteObjCreate(OBJ_CAMERA, 0, Player_object->roomnum, &Player_object->pos, NULL, OBJECT_HANDLE_NONE, &Objects[objnum]);
	}

	return Objects[objnum].handle;
}

//	Cinematic_Start
//
//	Starts an in-game cinematic sequence.  text_string is the text to be displayed
//	use pipes (|) to seperate lines. (calls demo routines to record)
bool Cinematic_Start(tGameCinematic* info, char* text_string)
{
	if (Demo_flags == DF_PLAYBACK)
		return true;	//demo system calls Cinematic_StartCine itself

	ASSERT(info != NULL);
	ASSERT(text_string != NULL);
	if (!info || !text_string)
		return false;

	if (Cinematic_inuse)
	{
		Cinematic_SetForFakeCinematic(info);
		return false;
	}

	if (Game_mode & GM_MULTI)
	{
		Cinematic_SetForFakeCinematic(info);
		return false;	//no cinematics in multiplayer
	}

	if ((info->flags & GCF_CAMERAPLACEMENT) == GCF_USEPATH)
	{
		info->orient = NULL;	//ensure that this is NULL
	}

	int camera_handle = Cinematics_CreateCamera();

	if (Demo_flags == DF_RECORDING)
	{
		tCinematicDemoInfo demo_data;
		demo_data.cinematic_type = CDI_NOT_CANNED;
		demo_data.text_string = text_string;
		demo_data.cinematic_data = info;
		demo_data.camera_objhandle = camera_handle;
		Cinematic_WriteDemoFileData(&demo_data);
	}

	return Cinematic_StartCine(info, text_string, camera_handle);
}

inline void Cinematic_DeleteCamera(int objhandle)
{
	object* obj = ObjGet(objhandle);
	if (obj)
	{
		SetObjectDeadFlag(obj);
	}
}


//	Cinematic_StartCine
//
//	Starts an in-game cinematic sequence.  text_string is the text to be displayed
//	use pipes (|) to seperate lines. (does not call demo routines)
bool Cinematic_StartCine(tGameCinematic* info, char* text_string, int camera_objhandle)
{
	ASSERT(info != NULL);
	ASSERT(text_string != NULL);
	if (!info || !text_string)
	{
		Cinematic_DeleteCamera(camera_objhandle);
		return false;
	}

	if (Cinematic_inuse)
	{
		Cinematic_SetForFakeCinematic(info);
		Cinematic_DeleteCamera(camera_objhandle);
		return false;
	}

	if (Game_mode & GM_MULTI)
	{
		Cinematic_SetForFakeCinematic(info);
		Cinematic_DeleteCamera(camera_objhandle);
		return false;	//no cinematics in multiplayer
	}

#ifdef _DEBUG
	if (!Cinematics_enabled)
	{
		Cinematic_SetForFakeCinematic(info);
		Cinematic_DeleteCamera(camera_objhandle);
		AddBlinkingHUDMessage("Cinematic: Not Displayed, they are disabled (DEL-SHIFT-C)");
		return true;
	}

	if (!DoAI)
	{
		Cinematic_SetForFakeCinematic(info);
		Cinematic_DeleteCamera(camera_objhandle);
		AddBlinkingHUDMessage("Cinematic: Not Displayed, AI is not turned on (DEL-A)");
		return true;
	}
#endif


	//take the given values and setup the struct
	memset(&GameCinema, 0, sizeof(GameCinema));

	//verify passed in values
	verify_percentranage(&info->text_display);
	verify_percentranage(&info->track_target);
	verify_percentranage(&info->player_disabled);
	verify_percentranage(&info->in_camera_view);
	verify_percentranage(&info->quick_exit);

	GameCinema.flags = info->flags;
	GameCinema.target_objhandle = info->target_objhandle;
	GameCinema.callback = info->callback;
	GameCinema.old_hudmode = GetHUDMode();

	int objnum;
	object* c = ObjGet(camera_objhandle);
	if (c)
	{
		objnum = OBJNUM(c);
	}
	else
	{
		mprintf((0, "Unable to create camera\n"));
		return false;
	}


	if ((info->flags & GCF_CAMERAPLACEMENT) == GCF_USEPOINT) {
		GameCinema.room = info->room;
		GameCinema.position = info->position;
		//objnum = ObjCreate(OBJ_CAMERA,0,info->room,&info->position,info->orient);
		ObjSetPos(c, &info->position, info->room, info->orient, false);

	}
	else
	{
		GameCinema.pathnum = info->pathid;
		if (GameCinema.pathnum == -1) {
			mprintf((0, "Invalid Path given\n"));
			return false;
		}
		//Abort if not a good path
		if (((info->flags & GCF_CAMERAPLACEMENT) == GCF_USEPOINT))
		{
			if (GamePaths[GameCinema.pathnum].num_nodes < 2)
			{
				mprintf((0, "Not a good path passed to Cinematic_Start\n"));
				return false;
			}
		}

		//objnum = ObjCreate(OBJ_CAMERA,0,GamePaths[GameCinema.pathnum].pathnodes[0].roomnum,&GamePaths[GameCinema.pathnum].pathnodes[0].pos,NULL);
		ObjSetPos(c, &GamePaths[GameCinema.pathnum].pathnodes[0].pos, GamePaths[GameCinema.pathnum].pathnodes[0].roomnum, NULL, false);
	}

	/*
	if(objnum==-1){
		//couldn't create camera
		mprintf((0,"Unable to create camera\n"));
		return false;
	}
	*/

	GameCinema.camera_objhandle = Objects[objnum].handle;
	GameCinema.target_triggered = false;
	object* camera = &Objects[objnum];

	// parse the text line and extract the lines
	GameCinema.num_lines = 0;
	GameCinema.text = mem_strdup(text_string);
	char* ptr = GameCinema.text;
	if (!ptr)
		Error("Out of Memory\n");

	while (*ptr)
	{
		if (*ptr == '|')
		{
			*ptr = '\0';
			GameCinema.num_lines++;
		}
		ptr++;
	}
	GameCinema.num_lines++;

	float start_time = Gametime;
	GameCinema.end_time = start_time + info->max_time_play;
	GameCinema.text_display_start = start_time + (info->max_time_play * info->text_display.min);
	GameCinema.text_display_stop = start_time + (info->max_time_play * info->text_display.max);
	GameCinema.track_start = start_time + (info->max_time_play * info->track_target.min);
	GameCinema.track_stop = start_time + (info->max_time_play * info->track_target.max);
	GameCinema.player_start = start_time + (info->max_time_play * info->player_disabled.min);
	GameCinema.player_stop = start_time + (info->max_time_play * info->player_disabled.max);
	GameCinema.camera_start = start_time + (info->max_time_play * info->in_camera_view.min);
	GameCinema.camera_stop = start_time + (info->max_time_play * info->in_camera_view.max);
	GameCinema.exit_start = start_time + (info->max_time_play * info->quick_exit.min);
	GameCinema.exit_end = start_time + (info->max_time_play * info->quick_exit.max);
	GameCinema.start_transition_type = info->start_transition;
	GameCinema.end_transition_type = info->end_transition;
	GameCinema.doing_end_transition = false;	//stop all pending end transitions
	GameCinema.controls_suspended = false;

	if (GameCinema.end_transition_type == GCTT_WACKY || GameCinema.end_transition_type == GCTT_FADEINOUT)
	{
		GameCinema.early_trans_end_start_time = start_time + info->max_time_play - (END_TRANSITION_TIME / 2.0f);
	}

	if (GameCinema.end_transition_type == GCTT_FADEWHITE)
	{
		GameCinema.early_trans_end_start_time = start_time;
	}

	if (GameCinema.start_transition_type == GCTT_NONE)
	{
		GameCinema.start_transition_start_time = start_time;
		GameCinema.start_transition_end_time = start_time;
	}
	else
	{
		GameCinema.start_transition_start_time = GameCinema.start_transition_end_time = -1.0f;
	}


	// The structs are setup, time to setup the camera and objects

	mprintf((0, "Cinematic_Start\n"));

	Player_object->flags &= ~OF_DESTROYABLE;
	Players[Player_num].flags &= ~PLAYER_FLAGS_AFTERBURN_ON;
	camera->flags &= ~OF_DESTROYABLE;

	//setup camera physics
	camera->movement_type = MT_PHYSICS;
	camera->mtype.phys_info.flags &= ~PF_USES_THRUST;
	//camera->mtype.phys_info.flags |= PF_LEVELING;
	camera->mtype.phys_info.flags &= ~PF_LEVELING;
	camera->mtype.phys_info.drag = 0.1f;
	camera->mtype.phys_info.mass = 2.0f;
	camera->mtype.phys_info.rotdrag = 0.1f;

	vm_MakeZero(&camera->mtype.phys_info.thrust);
	vm_MakeZero(&camera->mtype.phys_info.rotthrust);
	vm_MakeZero(&camera->mtype.phys_info.rotvel);
	vm_MakeZero(&camera->mtype.phys_info.velocity);

	//setup camera AI
	SetObjectControlType(camera, CT_AI);
	memset(camera->ai_info, 0, sizeof(ai_frame));

	camera->ai_info->ai_class = AIC_STATIC;
	camera->ai_info->ai_type = AIT_AIS;

	GoalInitTypeGoals(camera, AIT_AIS);

	float velocity_to_use = 75.0f;

	if ((info->flags & GCF_CAMERAPLACEMENT) == GCF_USEPATH)
	{
		//determine velocity based on distance to travel on path
		velocity_to_use = Cine_GetPathTravelSpeed(GameCinema.pathnum, info->max_time_play);
	}

	camera->ai_info->flags = AIF_PERSISTANT | AIF_DISABLE_FIRING | AIF_DISABLE_MELEE | AIF_FORCE_AWARENESS | AIF_DETERMINE_TARGET;
	camera->ai_info->max_velocity = velocity_to_use;  // Slightly faster than the real ship
	camera->ai_info->max_delta_velocity = 40.0f;
	camera->ai_info->max_turn_rate = 14000;
	camera->ai_info->awareness = AWARE_MOSTLY;
	camera->ai_info->movement_type = MC_FLYING;
	camera->ai_info->next_movement = AI_INVALID_INDEX;
	camera->ai_info->anim_sound_handle = 0;
	camera->ai_info->status_reg = 0;
	camera->ai_info->last_played_sound_index = -1;
	camera->ai_info->weapon_speed = 0.0f;
	camera->ai_info->animation_type = AS_ALERT;
	camera->ai_info->next_melee_time = Gametime;
	camera->ai_info->next_flinch_time = Gametime;
	camera->ai_info->target_handle = OBJECT_HANDLE_NONE;
	camera->ai_info->next_check_see_target_time = Gametime;
	camera->ai_info->last_see_target_time = Gametime;
	camera->ai_info->last_render_time = -1.0f;
	camera->ai_info->next_target_update_time = Gametime;
	camera->ai_info->notify_flags |= AI_NOTIFIES_ALWAYS_ON;
	camera->ai_info->last_see_target_pos = Zero_vector;
	camera->ai_info->dodge_vel_percent = 1.0f;
	camera->ai_info->attack_vel_percent = 1.0f;
	camera->ai_info->fight_same = 0.0f;
	camera->ai_info->agression = 0.0f;
	camera->ai_info->avoid_friends_distance = 0.0f;
	camera->ai_info->biased_flight_importance = 0.0f;
	camera->ai_info->circle_distance = 0.0f;
	camera->ai_info->dodge_percent = 0.0f;
	camera->ai_info->fight_team = 0.0;

	AIPathInitPath(&camera->ai_info->path);

	//Move camera to start position
	vector c_pos;
	int c_room;
	matrix c_orient;

	if ((info->flags & GCF_CAMERAPLACEMENT) == GCF_USEPOINT)
	{
		c_room = GameCinema.room;
		c_pos = GameCinema.position;
		if (info->orient)
			c_orient = *info->orient;
		else
			vm_MakeIdentity(&c_orient);
	}
	else
	{
		//Set up path info
		path_information pi;

		pi.path_id = GameCinema.pathnum;
		pi.start_node = 0;
		pi.end_node = GamePaths[GameCinema.pathnum].num_nodes - 1;

		if (GamePaths[GameCinema.pathnum].num_nodes == 1) {
			vm_VectorToMatrix(&c_orient, &GamePaths[GameCinema.pathnum].pathnodes[0].fvec,
				&GamePaths[GameCinema.pathnum].pathnodes[0].uvec, NULL);
			pi.next_node = 0;
		}
		else
		{
			vector fvec = GamePaths[GameCinema.pathnum].pathnodes[1].pos - GamePaths[GameCinema.pathnum].pathnodes[0].pos;
			vm_VectorToMatrix(&c_orient, &fvec, NULL, NULL);
			pi.next_node = 1;
		}

		c_pos = GamePaths[GameCinema.pathnum].pathnodes[0].pos;
		c_room = GamePaths[GameCinema.pathnum].pathnodes[0].roomnum;

		//Make the player follow the path
		GoalAddGoal(camera, AIG_FOLLOW_PATH, (void*)&pi, 0, 10.0f, GF_ORIENT_TARGET | GF_FORCE_AWARENESS);
	}

	ObjSetPos(camera, &c_pos, c_room, &c_orient, true);

	//Switch to correct hud mode
	switch (GameCinema.flags & GCF_SCREENFORMAT) {
	case GCF_LETTERBOX:
		SetHUDMode(HUD_LETTERBOX);
		break;
	case GCF_FULLSCREEN:
		SetHUDMode(HUD_OBSERVER);
		break;
	}

	GameCinema.longest_line = 0;

	//determine longest line and add text to game messages
	int old_font = grtext_GetFont();
	grtext_SetFont(BIG_BRIEFING_FONT);
	int pos = 0, len;
	char* str_ptr;

	for (int i = 0; i < GameCinema.num_lines; i++)
	{
		len = strlen(&GameCinema.text[pos]);

		int l = grtext_GetTextLineWidth(&GameCinema.text[pos]);
		if (GameCinema.longest_line < l)
			GameCinema.longest_line = l;

		//add text to game messages
		str_ptr = &GameCinema.text[pos];
		while (*str_ptr && (*str_ptr == ' ' || *str_ptr == '\t')) str_ptr++;
		if (*str_ptr)
			AddGameMessage(str_ptr);

		pos += len + 1;
	}
	GameCinema.longest_line += 10;
	grtext_SetFont(old_font);

	Cinematic_inuse = true;
	++CinematicGeneration;
	HUDPauseMessages();
	D3MusicStartCinematic();

	if (GameCinema.camera_start <= Gametime)
	{
		Viewer_object = camera;

		object* target;
		target = ObjGet(GameCinema.target_objhandle);

		if (GameCinema.track_start <= Gametime && target)
		{
			//face target immediatly
			vector u_axis;
			angle goal_angle;

			vector turn_to = target->pos - camera->pos;
			vm_NormalizeVector(&turn_to);

			if (turn_to == Zero_vector)
			{
				mprintf((0, "Cine: No turn_to or less than 1 degree off goal\n"));
				goto continue_start;
			}

			goal_angle = vm_DeltaAngVecNorm(&camera->orient.fvec, &turn_to, &u_axis);
			if (goal_angle == 0)
			{
				mprintf((0, "Cine: Goal angle is zero\n"));
				goto continue_start;
			}

			camera->orient.fvec = turn_to;

			camera->orient.uvec.x = camera->orient.uvec.z = 0.0f;
			camera->orient.uvec.y = 1.0f;

			vm_Orthogonalize(&camera->orient);
			ObjSetOrient(camera, &camera->orient);

		}
		else if ((GameCinema.flags & GCF_CAMERAPLACEMENT) == GCF_USEPATH)
		{
			//adjust to path node orientation (if we are on a path)
			int cur_path = camera->ai_info->path.cur_path;
			int cur_node = camera->ai_info->path.cur_node;
			int end_node = camera->ai_info->path.path_end_node[cur_path];
			int next_node = min(cur_node + 1, end_node);
			int path_num = camera->ai_info->path.path_id[cur_path];

			if (cur_node != next_node) {
				vector fvec = GamePaths[path_num].pathnodes[next_node].pos - GamePaths[path_num].pathnodes[cur_node].pos;
				vm_VectorToMatrix(&camera->orient, &fvec, NULL, NULL);
				vm_Orthogonalize(&camera->orient);
				ObjSetOrient(camera, &camera->orient);
			}
		}

	continue_start:;

	}

	if (GameCinema.callback)
		(*GameCinema.callback)(GCCT_START);

	return true;
}

inline bool Cinematic_IsPlayerDead(void)
{
	if (!Player_object || (Player_object->type == OBJ_GHOST) || (Player_object->type == OBJ_PLAYER && Players[Player_object->id].flags & (PLAYER_FLAGS_DYING | PLAYER_FLAGS_DEAD))) {
		return true;
	}
	return false;
}

//	Cinematic_Stop
//
//	Stops and clears up a in-game cinematic.
void Cinematic_Stop(void)
{
	if (!Cinematic_inuse)
		return;
	++CinematicGeneration;

	if (GameCinema.text)
	{
		mem_free(GameCinema.text);
		GameCinema.text = NULL;
	}

	//tell the target object the cinematic is over
	object* target = ObjGet(GameCinema.target_objhandle);

	if (GameCinema.callback)
		(*GameCinema.callback)(GCCT_STOP);

	//reset things back to normal, anything that might be messed up
	bool dont_restore_viewer = false;
	if (GameCinema.flags & GCF_DONTRESTOREVIEWER)
	{
		mprintf((0, "********NOT RESTORING VIEW TO PLAYER*************\n"));
		dont_restore_viewer = true;
	}

	if (!dont_restore_viewer)
	{
		Viewer_object = Player_object;
	}
	Players[Player_object->id].controller_bitflags = 0xFFFFFFFF;
	Player_object->flags |= OF_DESTROYABLE;
	ResumeControls();

	//go back to full screen
	if (!Cinematic_IsPlayerDead() && GetScreenMode() == SM_GAME)	//player death cam has started
		SetHUDMode(Cinematic_GetOldHudMode());

	//delete the camera
	if (!dont_restore_viewer)
	{
		object* cam = ObjGet(GameCinema.camera_objhandle);
		if (cam)
		{
			SetObjectDeadFlag(cam);
		}
	}

	Cinematic_inuse = false;
	HUDUnpauseMessages();
	D3MusicStopCinematic();

	//Start fade out transition
	if (GameCinema.end_transition_type == GCTT_FADE)
	{
		GameCinema.doing_end_transition = true;
		GameCinema.end_transition_start_time = Gametime;
		GameCinema.end_transition_end_time = Gametime + END_TRANSITION_TIME;
	}
	else if (GameCinema.end_transition_type == GCTT_WACKY)
	{
		//only start the wacky transition if it already hasn't (early break)
		if (!GameCinema.doing_end_transition)
		{
			GameCinema.doing_end_transition = true;
			GameCinema.end_transition_start_time = Gametime - (END_TRANSITION_TIME / 2);
			GameCinema.end_transition_end_time = Gametime + (END_TRANSITION_TIME / 2);
		}
	}
	else if (GameCinema.end_transition_type == GCTT_FADEINOUT)
	{
		//only start if it already hasn't
		if (!GameCinema.doing_end_transition)
		{
			GameCinema.doing_end_transition = true;
			GameCinema.end_transition_start_time = Gametime - (END_TRANSITION_TIME / 2);
			GameCinema.end_transition_end_time = Gametime + (END_TRANSITION_TIME / 2);
		}
	}
	else if (GameCinema.end_transition_type == GCTT_FADEWHITE)
	{
		if (!GameCinema.doing_end_transition)
		{
			GameCinema.doing_end_transition = true;
			GameCinema.end_transition_start_time = GameCinema.early_trans_end_start_time;
			GameCinema.end_transition_end_time = GameCinema.end_time;
		}
	}

	if (target)
	{
		AINotify(target, AIN_MOVIE_END, NULL);

		if (target->type == OBJ_PLAYER)
		{
			tOSIRISEventInfo ei;
			Osiris_CallLevelEvent(EVT_PLAYER_MOVIE_END, &ei);
		}
	}
}

bool Cinematic_IsKeyPressed(void)
{
	for (int i = 0; i < DDIO_MAX_KEYS; i++)
	{
		if (ddio_InternalKeyState(i))
			return true;
	}

	return false;
}


//	Cinematic_Frame
//
//	Processes a frame for the Cinematics
void Cinematic_Frame(void)
{
	if (Cinematic_fake_queued)
		Cinematic_PerformFake();

	if (!Cinematic_inuse)
		return;

	//check to see if the player died somehow
	if ((!(GameCinema.flags & GCF_IGNOREPLAYERDEAD)) && Cinematic_IsPlayerDead())
	{
		Cinematic_Stop();
		return;
	}

	//check for end of cinematic
	if (Gametime > GameCinema.end_time)
	{
		Cinematic_Stop();
		return;
	}

	//check for keyboard quick exit
	if (Gametime >= GameCinema.exit_start && Gametime <= GameCinema.exit_end)
	{
		if (Cinematic_IsKeyPressed())
		{
			//a key has been pressed, quick exit!
			ddio_KeyFlush();

			if (GameCinema.flags & GCF_FORCETARGETTOEND)
			{
				//move object to end of path
				object* target = ObjGet(GameCinema.target_objhandle);
				if (target && target->ai_info && target->ai_info->path.num_paths > 0)
				{
					bool shouldmove = true;

#if defined(_DEBUG) && defined(EDITOR)
					extern bool Game_being_played_from_quick_play;

					if (Game_being_played_from_quick_play && target->type == OBJ_PLAYER)
					{
						shouldmove = false;
					}
#endif

					//its a target on a path
					matrix orient;
					vector pos;
					int room;

					int currpath = target->ai_info->path.path_id[target->ai_info->path.cur_path];
					int endnode = GamePaths[currpath].num_nodes - 1;

					vm_VectorToMatrix(&orient, &GamePaths[currpath].pathnodes[endnode].fvec, &GamePaths[currpath].pathnodes[endnode].uvec, NULL);

					pos = GamePaths[currpath].pathnodes[endnode].pos;
					room = GamePaths[currpath].pathnodes[endnode].roomnum;

					if (shouldmove)
					{
						ObjSetPos(target, &pos, room, &orient, true);
					}
				}
			}
			Cinematic_Stop();
			return;
		}
	}

	object* camera = ObjGet(GameCinema.camera_objhandle);
	object* target = ObjGet(GameCinema.target_objhandle);

	if (!target && GameCinema.flags & GCF_STOPIFTAGETDEAD)
	{
		Cinematic_Stop();
		return;
	}

	//tell the target it's the focus of a cinematic
	if (target && !GameCinema.target_triggered)
	{
		GameCinema.target_triggered = true;
		AINotify(target, AIN_MOVIE_START, NULL);

		if (target->type == OBJ_PLAYER)
		{
			tOSIRISEventInfo ei;
			Osiris_CallLevelEvent(EVT_PLAYER_MOVIE_START, &ei);
		}
	}

	//check to see if we should be looking through the camera
	if (Gametime >= GameCinema.camera_start && Gametime <= GameCinema.camera_stop && camera != NULL)
	{
		//camera should be the viewer
		Viewer_object = camera;
	}
	else
	{
		Viewer_object = Player_object;
	}

	//check to see if we should be disabling player controls
	if (Gametime >= GameCinema.player_start && Gametime <= GameCinema.player_stop)
	{
		//disable controls
		Players[Player_object->id].controller_bitflags = 0;

		//make sure play controls are suspended
		if (!GameCinema.controls_suspended)
		{
			SuspendControls();
			GameCinema.controls_suspended = true;
		}
	}
	else
	{
		//make sure controls are enabled
		Players[Player_object->id].controller_bitflags = 0xFFFFFFFF;

		//make sure play controls are enabled
		if (GameCinema.controls_suspended)
		{
			ResumeControls();
			GameCinema.controls_suspended = false;
		}
	}

	//adjust view to target
	if (Gametime >= GameCinema.track_start && Gametime <= GameCinema.track_stop && target)
	{
		//track target
		vector u_axis;
		angle goal_angle;

		vector turn_to = target->pos - camera->pos;
		vm_NormalizeVector(&turn_to);

		if (turn_to == Zero_vector) {
			mprintf((0, "Cine: No turn_to or less than 1 degree off goal\n"));
			goto continue_frame;
		}

		goal_angle = vm_DeltaAngVecNorm(&camera->orient.fvec, &turn_to, &u_axis);
		if (goal_angle == 0) {
			mprintf((0, "Cine: Goal angle is zero\n"));
			goto continue_frame;
		}

		camera->orient.fvec = turn_to;

		camera->orient.uvec.x = camera->orient.uvec.z = 0.0f;
		camera->orient.uvec.y = 1.0f;

		vm_Orthogonalize(&camera->orient);
		ObjSetOrient(camera, &camera->orient);


	}
	else if ((GameCinema.flags & GCF_CAMERAPLACEMENT) == GCF_USEPATH)
	{
		//adjust to path node orientation (if we are on a path)
		int cur_path = camera->ai_info->path.cur_path;
		int cur_node = camera->ai_info->path.cur_node;
		int end_node = camera->ai_info->path.path_end_node[cur_path];
		int next_node = min(cur_node + 1, end_node);
		int path_num = camera->ai_info->path.path_id[cur_path];

		if (cur_node != next_node)
		{
			vector fvec = GamePaths[path_num].pathnodes[next_node].pos - GamePaths[path_num].pathnodes[cur_node].pos;
			vm_VectorToMatrix(&camera->orient, &fvec, NULL, NULL);
			vm_Orthogonalize(&camera->orient);
			ObjSetOrient(camera, &camera->orient);
		}
	}
	else
	{
		//don't adjust at all
	}

continue_frame:

	if (GameCinema.end_transition_type == GCTT_WACKY || GameCinema.end_transition_type == GCTT_FADEINOUT)
	{
		if (Gametime > GameCinema.early_trans_end_start_time && !GameCinema.doing_end_transition)
		{
			//start the transition early
			GameCinema.doing_end_transition = true;
			GameCinema.end_transition_start_time = Gametime;
			GameCinema.end_transition_end_time = Gametime + END_TRANSITION_TIME;
		}
	}

	if (GameCinema.end_transition_type == GCTT_FADEWHITE)
	{
		if (Gametime > GameCinema.early_trans_end_start_time && !GameCinema.doing_end_transition)
		{
			//start the transition early
			GameCinema.doing_end_transition = true;
			GameCinema.end_transition_start_time = GameCinema.early_trans_end_start_time;
			GameCinema.end_transition_end_time = GameCinema.end_time;
		}
	}
}

namespace {
struct CinematicFrameReceipt {
    uint64_t lifetime = 0, frame = 0, generation = 0;
    bool valid = false, committed = false;
    void (*callback)(int) = nullptr;
} CinemaFrame;
float unit(float value) { return (std::max)(0.f,(std::min)(1.f,value)); }
}

// Game-owned visual scheduling; no canvas, text or GL execution can enter here.
void AdvanceCinematicPresentation(uint64_t lifetime,uint64_t frame)
{
    if (CinemaFrame.valid && CinemaFrame.lifetime == lifetime) {
        if (frame == CinemaFrame.frame) return;
        if (frame < CinemaFrame.frame) throw std::logic_error("Cinematic frame reordered");
    }
    CinemaFrame = {lifetime,frame,CinematicGeneration,true,false,Cinematic_inuse ? GameCinema.callback : nullptr};
    if (Cinematic_inuse) {
        if (GetHUDMode() == HUD_LETTERBOX) Clear_screen = 4;
        if (GameCinema.start_transition_type != GCTT_NONE && GameCinema.start_transition_start_time == -1) {
            GameCinema.start_transition_start_time = Gametime;
            GameCinema.start_transition_end_time = Gametime+2;
        }
        if ((GameCinema.flags & GCF_TEXT_MASK) == GCF_TEXT_WIPEIN &&
            ((GameCinema.flags & GCF_SCREENFORMAT) != GCF_LETTERBOX ||
             (GameCinema.flags & GCF_LAYOUT_MASK) == GCF_LAYOUT_MIDDLE ||
             (GameCinema.flags & GCF_LAYOUT_MASK) == GCF_LAYOUT_LOW)) {
            GameCinema.flags = (GameCinema.flags & ~GCF_TEXT_MASK) | GCF_TEXT_FADEINOUT;
        }
    }
    // A stopped cinematic may retain a transition. It still advances here.
    if (GameCinema.doing_end_transition &&
        (Gametime > GameCinema.end_transition_end_time || Gametime < GameCinema.end_transition_start_time ||
         GameCinema.end_transition_type == GCTT_NONE)) GameCinema.doing_end_transition = false;
}

void CommitCinematicFrame(uint64_t lifetime,uint64_t frame)
{
    if (!CinemaFrame.valid || CinemaFrame.lifetime != lifetime || CinemaFrame.frame != frame)
        throw std::logic_error("Cinematic callback without its game frame");
    if (CinemaFrame.committed) return;
    CinemaFrame.committed = true;
    // This callback can teleport objects. It deliberately runs AFTER the
    // current world is frozen/executed, affecting only the next publication.
    if (Cinematic_inuse && CinemaFrame.generation == CinematicGeneration && CinemaFrame.callback)
        (*CinemaFrame.callback)(GCCT_FRAME);
}

void PrepareCinematicCanvas(d3scene::FrameData& frame)
{
    if (!Cinematic_inuse && !GameCinema.doing_end_transition) return;
    constexpr uint64_t collection = 0x43494e454d410001ull;
    const d3scene::Rect viewport{0,0,Max_window_w,Max_window_h};
    const auto begin = frame.meshes.size();
    int32_t order = 0;
    const auto quad = [&](d3scene::Rect rect,float alpha,d3scene::Vec3 color,int bitmap = -1) {
        SceneQuadStyle style; style.alpha = unit(alpha); style.color = color; style.bitmap = bitmap;
        if (style.alpha > 0) PrepareSceneQuad(frame,collection,order,rect,style);
    };
    const auto transition = [&](bool start) {
        if (start && (!Cinematic_inuse || GameCinema.start_transition_type == GCTT_NONE)) return;
        if (!start && !GameCinema.doing_end_transition) return;
        const float first = start ? GameCinema.start_transition_start_time : GameCinema.end_transition_start_time;
        const float last = start ? GameCinema.start_transition_end_time : GameCinema.end_transition_end_time;
        if (!(last > first) || Gametime < first || Gametime > last) return;
        const float progress = unit((Gametime-first)/(last-first));
        const int type = start ? GameCinema.start_transition_type : GameCinema.end_transition_type;
        if (start) { quad(viewport,1-progress,type == GCTT_FADEWHITE ? d3scene::Vec3{1,1,1} : d3scene::Vec3{}); return; }
        switch (type) {
        case GCTT_FADE: quad(viewport,1-progress,{}); break;
        case GCTT_FADEINOUT: quad(viewport,1-std::abs(progress*2-1),{}); break;
        case GCTT_FADEWHITE:
            if (progress < .8f) quad(viewport,progress/.8f,{1,1,1});
            else { const float grey = unit(1-(progress-.8f)/.2f); quad(viewport,1,{grey,grey,grey}); }
            break;
        case GCTT_WACKY: {
            const int bitmap = gc_wacky_bmp_handle;
            if (bitmap <= BAD_BITMAP_HANDLE || !GameBitmaps[bitmap].used) break;
            const float width = float(bm_w(bitmap,0)), height = float(bm_h(bitmap,0));
            if (!(width > 0 && height > 0)) break;
            int w, h;
            if (Max_window_w/width < Max_window_h/height) { w = Max_window_w; h = int(Max_window_h*width/height); }
            else { h = Max_window_h; w = int(Max_window_w*height/width); }
            const float scale = 1-std::abs(progress*2-1);
            w = int(w*scale); h = int(h*scale);
            quad({(Max_window_w-w)/2,(Max_window_h-h)/2,w,h},1,{1,1,1},bitmap);
            break;
        }
        }
    };
    const auto text = [&] {
        if (!Cinematic_inuse || !GameCinema.text || Gametime < GameCinema.text_display_start ||
            Gametime >= GameCinema.text_display_stop) return;
        const float duration = GameCinema.text_display_stop-GameCinema.text_display_start;
        if (!(duration > 0)) return;
        const float progress = unit((Gametime-GameCinema.text_display_start)/duration);
        const int effect = GameCinema.flags & GCF_TEXT_MASK;
        SceneTextStyle style; style.font = BIG_BRIEFING_FONT; style.color = {1,1,1};
        if (effect == GCF_TEXT_WIPEIN || effect == GCF_TEXT_FADEINOUT)
            style.alpha = progress <= .33f ? unit(progress/.33f) :
                effect == GCF_TEXT_FADEINOUT && progress > .66f ? unit(1-(progress-.66f)/.33f) : 1;
        style.alpha = int(style.alpha*255)/255.f;
        const int line_height = grfont_GetHeight(style.font)+2;
        const int height = GameCinema.num_lines*line_height;
        int y = 2;
        switch (GameCinema.flags & GCF_LAYOUT_MASK) {
        case GCF_LAYOUT_LOW: y = int(Max_window_h*.6667f)-height; break;
        case GCF_LAYOUT_BOTTOM: y = Max_window_h-height; break;
        case GCF_LAYOUT_MIDDLE: y = (Max_window_h-height)/2; break;
        }
        const char* line = GameCinema.text;
        for (int i = 0; i < GameCinema.num_lines; ++i) {
            const int width = MeasureSceneText(line,style).width;
            PrepareSceneText(frame,collection,order,(Max_window_w-width)/2,y,line,style);
            y += line_height; line += std::strlen(line)+1;
        }
        if (effect == GCF_TEXT_WIPEIN) {
            const float p = unit(progress/.18f);
            const int width = int((1-p)*GameCinema.longest_line);
            const int right = (Max_window_w+GameCinema.longest_line)/2;
            const int top = (GameCinema.flags & GCF_LAYOUT_MASK) == GCF_LAYOUT_TOP ? 0 : Game_window_y+Game_window_h;
            const int bottom = top == 0 ? Game_window_y : Max_window_h;
            const auto before = frame.meshes.size();
            quad({right-width,top,width,bottom-top},1,{});
            if (frame.meshes.size() != before) frame.meshes.back().asset = d3scene::assets().gradientQuadMesh();
        }
    };
    transition(true);
    const bool transition_first = GameCinema.flags & GCF_DOTRANSBEFORETEXT;
    if (transition_first) transition(false);
    text();
    if (!transition_first) transition(false);
    if (frame.meshes.size() == begin) return;
    d3scene::PreparedForeground layer; layer.identity = collection;
    layer.phase = d3scene::ForegroundPhase::PostHUD;
    layer.layer.display_views.push_back(MakeSceneCanvasView(collection,viewport,d3scene::ViewTarget::Display,SceneGeometryInspection()));
    frame.foregrounds.push_back(std::move(layer));
    if (AutomatedCapturePending()) AutomatedCaptureLog("scene cinematic canvas active=%d tail=%d meshes=%u",
        Cinematic_inuse,GameCinema.doing_end_transition,unsigned(frame.meshes.size()-begin));
}


float Cine_GetPathTravelSpeed(int pathnum, float time)
{
	float velocity_to_use = 75.0f;

	//determine velocity based on distance to travel on path
	int num_nodes = GamePaths[pathnum].num_nodes;
	float distance;
	distance = 0;

	for (int n = 0; n < num_nodes - 1; n++)
	{
		distance += vm_VectorDistance(&GamePaths[pathnum].pathnodes[n].pos, &GamePaths[pathnum].pathnodes[n + 1].pos);
	}

	if (time > 0)
		velocity_to_use = distance / time;

	return velocity_to_use;
}

/*
***********************************************************************
	Intro Movie Canned Cinematic
***********************************************************************
*/


struct
{
	int player_path;
	float cinematic_time;
	bool should_thrust;
}CannedCinematicIntro;

void CannedCinematicIntroCallback(int type)
{
	switch (type)
	{
	case GCCT_START:
	{
		//start the player on the path
		mprintf((0, "START PLAYER ON PATH\n"));

		//determine how fast they need to travel
		int pathnum = CannedCinematicIntro.player_path;
		float velocity = Cine_GetPathTravelSpeed(pathnum, CannedCinematicIntro.cinematic_time);

		SuspendControls();

		if (Players[Player_num].objnum > -1)
		{
			object* playerobj = &Objects[Players[Player_num].objnum];
			ClearPlayerFiring(playerobj, PW_PRIMARY);
			ClearPlayerFiring(playerobj, PW_SECONDARY);
		}

		PlayerSetControlToAI(Player_num, velocity);

		//move the target (player) to the first node of the path
		//this makes it multiplayer friendly since this will only be called
		//in a single player game
		vector pos;
		matrix orient;
		int roomnum;
		int next_node;

		if (pathnum == -1)
		{
			mprintf((0, "ILLEGAL PATH FOR PLAYER\n"));
			break;
		}

		if (GamePaths[pathnum].num_nodes > 1)
		{
			next_node = 1;
			vector fvec = GamePaths[pathnum].pathnodes[1].pos - GamePaths[pathnum].pathnodes[0].pos;
			vm_VectorToMatrix(&orient, &fvec, NULL, NULL);

			//see if it is a hacked 2 node path (i.e. its such a small distance he should move
			if (GamePaths[pathnum].num_nodes == 2)
			{
				if (fabs(vm_VectorDistance(&GamePaths[pathnum].pathnodes[1].pos, &GamePaths[pathnum].pathnodes[0].pos)) > 30.0f)
				{
					CannedCinematicIntro.should_thrust = true;
					mprintf((0, "Player should thrust\n"));
				}
				else
				{
					CannedCinematicIntro.should_thrust = false;
					mprintf((0, "Player should NOT thrust\n"));
				}
			}
			else
			{
				CannedCinematicIntro.should_thrust = true;
				mprintf((0, "Player should thrust\n"));
			}
		}
		else
		{
			next_node = 0;
			CannedCinematicIntro.should_thrust = false;
			mprintf((0, "Player should NOT thrust\n"));
			vm_VectorToMatrix(&orient, &GamePaths[pathnum].pathnodes[0].fvec, &GamePaths[pathnum].pathnodes[0].uvec, NULL);
		}
		pos = GamePaths[pathnum].pathnodes[0].pos;
		roomnum = GamePaths[pathnum].pathnodes[0].roomnum;

		ObjSetPos(Player_object, &pos, roomnum, &orient, true);

		if (CannedCinematicIntro.should_thrust)
		{
			//Set up path info
			path_information pi;
			pi.path_id = pathnum;
			pi.start_node = 0;
			pi.next_node = next_node;
			pi.end_node = GamePaths[pathnum].num_nodes - 1;

			//Make the player follow the path			
			GoalAddGoal(Player_object, AIG_FOLLOW_PATH, (void*)&pi, 0, 1.0f, GF_ORIENT_PATH_NODE | GF_FORCE_AWARENESS);
		}
	}break;
	case GCCT_STOP:
	{
		//stop the player on the path
		mprintf((0, "STOP PLAYER ON PATH\n"));
		ResetPlayerControlType(Player_num);
		ResumeControls();

		vm_MakeZero(&Player_object->mtype.phys_info.velocity);

		Players[Player_num].flags &= ~PLAYER_FLAGS_THRUSTED;

	}break;
	case GCCT_FRAME:
	{
		if (CannedCinematicIntro.should_thrust)
			Players[Player_num].flags |= PLAYER_FLAGS_THRUSTED;
		else
			Players[Player_num].flags &= ~PLAYER_FLAGS_THRUSTED;
	}break;
	}
}


void CannedCinematic_Intro(int PathID, char* Text, int PlayerPath, float Seconds, int camera_handle)
{
	CannedCinematicIntro.player_path = PlayerPath;
	CannedCinematicIntro.cinematic_time = Seconds;

	tGameCinematic info;
	info.flags = 0;
	info.flags = (GCF_LETTERBOX | GCF_USEPATH | GCF_TEXT_FADEINOUT | GCF_LAYOUT_BOTTOM | GCF_FORCETARGETTOEND);
	info.target_objhandle = Player_object->handle;
	info.max_time_play = Seconds;
	info.callback = CannedCinematicIntroCallback;
	info.text_display.min = 0.40f;
	info.text_display.max = 0.80f;
	info.track_target.min = 0.0f;
	info.track_target.max = 1.0f;
	info.player_disabled.min = 0.0f;
	info.player_disabled.max = 1.0f;
	info.in_camera_view.min = 0.0f;
	info.in_camera_view.max = 1.0f;
	info.quick_exit.min = 0.25f;
	info.quick_exit.max = 1.0f;

	info.end_transition = GCTT_FADEINOUT;
	info.start_transition = GCTT_FADE;
	info.pathid = PathID;

	Cinematic_StartCine(&info, Text, camera_handle);
}


struct
{
	int player_path;
	float cinematic_time;
	bool should_thrust;
}CannedCinematicEndLevel;
bool EndLevel();

void CannedCinematicEndLevelCallback(int type)
{
	switch (type)
	{
	case GCCT_START:
	{
		//if the player is dead, bring them back from the dead
		if ((Players[Player_num].flags & PLAYER_FLAGS_DYING) || (Players[Player_num].flags & PLAYER_FLAGS_DEAD))
		{
			//the player is currently dead
			mprintf((0, "Bringing player back from the dead\n"));
			Players[Player_num].flags &= ~(PLAYER_FLAGS_DYING | PLAYER_FLAGS_DEAD);
			Player_object->shields = 1.0f;
		}

		//Make player invulnerable
		MakePlayerInvulnerable(Player_num, CannedCinematicEndLevel.cinematic_time, false);

		//start the player on the path
		mprintf((0, "START PLAYER ON PATH\n"));

		//determine how fast they need to travel
		int pathnum = CannedCinematicEndLevel.player_path;

		if (pathnum == -1)
		{
			mprintf((0, "ILLEGAL PATH FOR PLAYER\n"));
			break;
		}

		float velocity = Cine_GetPathTravelSpeed(pathnum, CannedCinematicEndLevel.cinematic_time);

		SuspendControls();
		if (Players[Player_num].objnum > -1)
		{
			object* playerobj = &Objects[Players[Player_num].objnum];
			ClearPlayerFiring(playerobj, PW_PRIMARY);
			ClearPlayerFiring(playerobj, PW_SECONDARY);
		}

		PlayerSetControlToAI(Player_num, velocity);

		//move the target (player) to the first node of the path
		//this makes it multiplayer friendly since this will only be called
		//in a single player game
		vector pos;
		matrix orient;
		int roomnum;
		int next_node;

		if (GamePaths[pathnum].num_nodes > 1)
		{
			next_node = 1;
			vector fvec = GamePaths[pathnum].pathnodes[1].pos - GamePaths[pathnum].pathnodes[0].pos;
			vm_VectorToMatrix(&orient, &fvec, NULL, NULL);

			if (GamePaths[pathnum].num_nodes == 2)
			{
				if (fabs(vm_VectorDistance(&GamePaths[pathnum].pathnodes[1].pos, &GamePaths[pathnum].pathnodes[0].pos)) > 30.0f)
				{
					CannedCinematicEndLevel.should_thrust = true;
				}
				else
				{
					CannedCinematicEndLevel.should_thrust = false;
				}
			}
			else
			{
				CannedCinematicEndLevel.should_thrust = true;
			}
		}
		else
		{
			next_node = 0;
			vm_VectorToMatrix(&orient, &GamePaths[pathnum].pathnodes[0].fvec, &GamePaths[pathnum].pathnodes[0].uvec, NULL);
			CannedCinematicEndLevel.should_thrust = false;
		}
		pos = GamePaths[pathnum].pathnodes[0].pos;
		roomnum = GamePaths[pathnum].pathnodes[0].roomnum;

		ObjSetPos(Player_object, &pos, roomnum, &orient, true);

		if (CannedCinematicEndLevel.should_thrust)
		{

			//Set up path info
			path_information pi;
			pi.path_id = pathnum;
			pi.start_node = 0;
			pi.next_node = next_node;
			pi.end_node = GamePaths[pathnum].num_nodes - 1;

			//Make the player follow the path			
			GoalAddGoal(Player_object, AIG_FOLLOW_PATH, (void*)&pi, 0, 1.0f, GF_ORIENT_PATH_NODE | GF_FORCE_AWARENESS);
		}
	}break;
	case GCCT_STOP:
	{
		//stop the player on the path
		mprintf((0, "STOP PLAYER ON PATH\n"));
		ResetPlayerControlType(Player_num);
		ResumeControls();

		vm_MakeZero(&Player_object->mtype.phys_info.velocity);

		SetGameState(GAMESTATE_LVLEND);
	}break;
	case GCCT_FRAME:
	{
		if (CannedCinematicEndLevel.should_thrust)
			Players[Player_num].flags |= PLAYER_FLAGS_THRUSTED;
		else
			Players[Player_num].flags &= ~PLAYER_FLAGS_THRUSTED;
	}break;
	}
}

void CannedCinematic_EndLevelPath(int PathID, char* Text, int PlayerPath, float Seconds, int camera_handle)
{
	CannedCinematicEndLevel.player_path = PlayerPath;
	CannedCinematicEndLevel.cinematic_time = Seconds;

	tGameCinematic info;
	info.flags = 0;
	info.flags = (GCF_LETTERBOX | GCF_USEPATH | GCF_TEXT_WIPEIN | GCF_LAYOUT_BOTTOM | GCF_FORCETARGETTOEND | GCF_DONTRESTOREVIEWER);
	info.target_objhandle = Player_object->handle;
	info.max_time_play = Seconds;
	info.callback = CannedCinematicEndLevelCallback;
	info.text_display.min = 0.40f;
	info.text_display.max = 0.80f;
	info.track_target.min = 0.0f;
	info.track_target.max = 1.0f;
	info.player_disabled.min = 0.0f;
	info.player_disabled.max = 1.0f;
	info.in_camera_view.min = 0.0f;
	info.in_camera_view.max = 1.0f;
	info.quick_exit.min = 1.0f;
	info.quick_exit.max = 1.0f;

	info.end_transition = GCTT_FADEINOUT;
	info.start_transition = GCTT_FADE;
	info.pathid = PathID;

	Cinematic_StartCine(&info, Text, camera_handle);
}

void CannedCinematic_EndLevelPoint(vector* pos, int room, char* Text, int PlayerPath, float Seconds, int camera_handle)
{
	CannedCinematicEndLevel.player_path = PlayerPath;
	CannedCinematicEndLevel.cinematic_time = Seconds;

	tGameCinematic info;
	info.flags = 0;
	info.flags = (GCF_LETTERBOX | GCF_USEPOINT | GCF_TEXT_WIPEIN | GCF_LAYOUT_BOTTOM | GCF_FORCETARGETTOEND | GCF_DONTRESTOREVIEWER);
	info.target_objhandle = Player_object->handle;
	info.max_time_play = Seconds;
	info.callback = CannedCinematicEndLevelCallback;
	info.text_display.min = 0.40f;
	info.text_display.max = 0.80f;
	info.track_target.min = 0.0f;
	info.track_target.max = 1.0f;
	info.player_disabled.min = 0.0f;
	info.player_disabled.max = 1.0f;
	info.in_camera_view.min = 0.0f;
	info.in_camera_view.max = 1.0f;
	info.quick_exit.min = 1.0f;
	info.quick_exit.max = 1.0f;
	info.end_transition = GCTT_FADEINOUT;
	info.start_transition = GCTT_FADE;

	info.position = *pos;
	info.room = room;
	info.orient = NULL;

	Cinematic_StartCine(&info, Text, camera_handle);
}

struct
{
	object* player;
	int room;
	vector pos;
	matrix orient;
}CannedCinematicMovePlayerFade;

void CannedCinematicMovePlayerFadeCallback(int type)
{
	switch (type)
	{
	case GCCT_FRAME:
		object* player = CannedCinematicMovePlayerFade.player;
		ObjSetPos(player, &CannedCinematicMovePlayerFade.pos, CannedCinematicMovePlayerFade.room, &CannedCinematicMovePlayerFade.orient, true);
		break;
	}
}

void CannedCinematic_MovePlayerFade(object* player, int room, vector* pos, matrix* orient, int camera_handle)
{
	CannedCinematicMovePlayerFade.player = player;
	CannedCinematicMovePlayerFade.room = room;
	CannedCinematicMovePlayerFade.pos = *pos;
	CannedCinematicMovePlayerFade.orient = *orient;

	tGameCinematic info;
	info.flags = 0;
	info.flags = (GCF_FULLSCREEN | GCF_USEPOINT | GCF_TEXT_NOEFFECT | GCF_LAYOUT_BOTTOM);
	info.target_objhandle = OBJECT_HANDLE_NONE;
	info.max_time_play = 1.0f;
	info.callback = CannedCinematicMovePlayerFadeCallback;
	info.text_display.min = 1.0f;
	info.text_display.max = 1.0f;
	info.track_target.min = 1.0f;
	info.track_target.max = 1.0f;
	info.player_disabled.min = 1.0f;
	info.player_disabled.max = 1.0f;
	info.in_camera_view.min = 0.0f;
	info.in_camera_view.max = 1.0f;
	info.quick_exit.min = 1.0f;
	info.quick_exit.max = 1.0f;
	info.end_transition = GCTT_FADEINOUT;
	info.start_transition = GCTT_NONE;

	info.position = player->pos;
	info.room = player->roomnum;
	info.orient = &player->orient;

	Cinematic_StartCine(&info, "", camera_handle);
}

struct
{
	float start_time;
	float max_time;
}CannedCinematicLevelEndFadeWhite;

void CannedCinematicLevelEndFadeWhiteCallback(int type)
{
	switch (type)
	{
	case GCCT_START:
		break;
	case GCCT_FRAME:
		break;
	case GCCT_STOP:
		SetGameState(GAMESTATE_LVLEND);
		break;
	}
}

void CannedCinematic_LevelEndFadeWhite(int camera_handle, float time, char* text_to_display)
{
	CannedCinematicLevelEndFadeWhite.start_time = Gametime;
	CannedCinematicLevelEndFadeWhite.max_time = time;

	tGameCinematic info;
	info.flags = (GCF_LETTERBOX | GCF_USEPOINT | GCF_TEXT_NOEFFECT | GCF_LAYOUT_MIDDLE | GCF_DONTRESTOREVIEWER | GCF_IGNOREPLAYERDEAD | GCF_DOTRANSBEFORETEXT);
	info.target_objhandle = OBJECT_HANDLE_NONE;
	info.max_time_play = time;
	info.callback = CannedCinematicLevelEndFadeWhiteCallback;
	info.text_display.min = 0.0f;
	info.text_display.max = 1.0f;
	info.track_target.min = 1.0f;
	info.track_target.max = 1.0f;
	info.player_disabled.min = 1.0f;
	info.player_disabled.max = 1.0f;
	info.in_camera_view.min = 1.0f;
	info.in_camera_view.max = 1.0f;
	info.quick_exit.min = 1.0f;
	info.quick_exit.max = 1.0f;
	info.end_transition = GCTT_FADEWHITE;
	info.start_transition = GCTT_NONE;

	info.position = Player_object->pos;
	info.room = Player_object->roomnum;
	info.orient = &Player_object->orient;

	Cinematic_StartCine(&info, (text_to_display) ? text_to_display : (char*)"", camera_handle);
}

//==============================================================================
// Canned Cinematic interface

void Cinematic_DoFakeCannedCinematics(tCannedCinematicInfo* info)
{
	switch (info->type)
	{
	case CANNED_LEVEL_INTRO:
		break;
	case CANNED_LEVEL_END_PATH:
	case CANNED_LEVEL_END_POINT:
	case CANNED_LEVEL_END_FADE_WHITE:
		if (Game_mode & GM_MULTI)
		{
			MultiEndLevel();
		}
		else
		{
			SetGameState(GAMESTATE_LVLEND);
		}
		break;
	case CANNED_MOVE_PLAYER_FADE:
		break;
	}
}

void Cinematic_StartCannedScript(tCannedCinematicInfo* info)
{
	if (Demo_flags == DF_PLAYBACK)
		return;	//demo system calls Cinematic_StartCanned itself

	Cinematic_StartCanned(info);
}

void Cinematic_StartCanned(tCannedCinematicInfo* info, int camera_handle)
{
	if (Cinematic_inuse)
	{
		tGameCinematic gc;
		memset(&gc, 0, sizeof(tGameCinematic));
		switch (info->type)
		{
		case CANNED_LEVEL_INTRO:
		case CANNED_LEVEL_END_PATH:
		case CANNED_LEVEL_END_POINT:
		case CANNED_MOVE_PLAYER_FADE:
		case CANNED_LEVEL_END_FADE_WHITE:
			gc.target_objhandle = Player_object->handle;
			break;
		default:
		{
			gc.target_objhandle = OBJECT_HANDLE_NONE;
		}break;
		}

		Cinematic_SetForFakeCinematic(&gc);
		Cinematic_DoFakeCannedCinematics(info);
		if (camera_handle != -1)
			Cinematic_DeleteCamera(camera_handle);
		return;
	}

	if (Game_mode & GM_MULTI)
	{
		tGameCinematic gc;
		memset(&gc, 0, sizeof(tGameCinematic));
		switch (info->type)
		{
		case CANNED_LEVEL_INTRO:
		case CANNED_LEVEL_END_PATH:
		case CANNED_LEVEL_END_POINT:
		case CANNED_MOVE_PLAYER_FADE:
		case CANNED_LEVEL_END_FADE_WHITE:
			gc.target_objhandle = Player_object->handle;
			break;
		default:
		{
			gc.target_objhandle = OBJECT_HANDLE_NONE;
		}break;
		}

		Cinematic_SetForFakeCinematic(&gc);
		Cinematic_DoFakeCannedCinematics(info);
		if (camera_handle != -1)
			Cinematic_DeleteCamera(camera_handle);
		return;	//no cinematics in multiplayer
	}

	if (camera_handle == -1)
	{
		//when playing back a demo, this should have a value
		ASSERT(Demo_flags != DF_PLAYBACK);
		camera_handle = Cinematics_CreateCamera();
	}

	if (Demo_flags == DF_RECORDING)
	{
		tCinematicDemoInfo demo_data;
		demo_data.cinematic_type = CDI_CANNED;
		demo_data.text_string = NULL;
		demo_data.canned_data = info;
		demo_data.camera_objhandle = camera_handle;
		Cinematic_WriteDemoFileData(&demo_data);
	}

	switch (info->type)
	{
	case CANNED_LEVEL_INTRO:
		CannedCinematic_Intro(info->camera_pathid, info->text_to_display, info->target_pathid, info->time, camera_handle);
		break;
	case CANNED_LEVEL_END_PATH:
		CannedCinematic_EndLevelPath(info->camera_pathid, info->text_to_display, info->target_pathid, info->time, camera_handle);
		break;
	case CANNED_LEVEL_END_POINT:
	{
		object* cam = ObjGet(info->object_to_use_for_point);
		if (cam)
		{
			CannedCinematic_EndLevelPoint(&cam->pos, cam->roomnum, info->text_to_display, info->target_pathid, info->time, camera_handle);
		}
		else
		{
			mprintf((0, "Passed in object handle for canned cinematic (level end) for camera position does not exist\n"));
			Cinematic_DeleteCamera(camera_handle);
			SetGameState(GAMESTATE_LVLEND);
		}
	}break;
	case CANNED_MOVE_PLAYER_FADE:
	{	object* player = ObjGet(info->target_objhandle);
	if (!player || player->type != OBJ_PLAYER)
	{
		mprintf((0, "Invalid player passed to CANNED_MOVE_PLAYER_FADE\n"));
		Cinematic_DeleteCamera(camera_handle);
		return;
	}

	CannedCinematic_MovePlayerFade(player, info->room, &info->pos, &info->orient, camera_handle);

	}break;
	case CANNED_LEVEL_END_FADE_WHITE:
	{
		CannedCinematic_LevelEndFadeWhite(camera_handle, info->time, info->text_to_display);
	}break;
	default:
	{
		Cinematic_DeleteCamera(camera_handle);
	}break;
	}
}


//==================================================
// Demo file support
//==================================================
void mf_WriteInt(ubyte* buffer, int* pointer, int data);
void mf_WriteShort(ubyte* buffer, int* pointer, short data);
void mf_WriteByte(ubyte* buffer, int* pointer, ubyte data);
void mf_WriteFloat(ubyte* buffer, int* pointer, float data);
void mf_WriteBytes(ubyte* buffer, int* pointer, ubyte* data, int len);
void mf_WriteString(ubyte* buffer, int* pointer, char* string);
int mf_ReadInt(ubyte* buffer, int* pointer);
short mf_ReadShort(ubyte* buffer, int* pointer);
ubyte mf_ReadByte(ubyte* buffer, int* pointer);
float mf_ReadFloat(ubyte* buffer, int* pointer);
void mf_ReadBytes(ubyte* buffer, int* pointer, ubyte* data, int len);
void mf_ReadString(ubyte* buffer, int* pointer, char* string);

void Cinematic_DoDemoFileData(ubyte* buffer)
{
	int count = 0;
	char text_string[512];
	tCannedCinematicInfo canned_data;
	tGameCinematic cinematic_data;
	int old_camera_objnum, camera_objnum, camera_handle;

	//see what kind of cinematic we are loading
	switch (mf_ReadByte(buffer, &count))
	{
	case CDI_NOT_CANNED:
	{
		matrix fake_orient;
		vm_MakeIdentity(&fake_orient);

		cinematic_data.orient = &fake_orient;
		cinematic_data.callback = NULL;
		cinematic_data.flags = mf_ReadInt(buffer, &count);
		cinematic_data.target_objhandle = mf_ReadInt(buffer, &count);
		cinematic_data.end_transition = mf_ReadInt(buffer, &count);
		cinematic_data.start_transition = mf_ReadInt(buffer, &count);
		cinematic_data.pathid = mf_ReadInt(buffer, &count);
		cinematic_data.position.x = mf_ReadFloat(buffer, &count);
		cinematic_data.position.y = mf_ReadFloat(buffer, &count);
		cinematic_data.position.z = mf_ReadFloat(buffer, &count);
		cinematic_data.room = mf_ReadInt(buffer, &count);
		if (mf_ReadByte(buffer, &count))
		{
			fake_orient.fvec.x = mf_ReadFloat(buffer, &count);
			fake_orient.fvec.y = mf_ReadFloat(buffer, &count);
			fake_orient.fvec.z = mf_ReadFloat(buffer, &count);
			fake_orient.uvec.x = mf_ReadFloat(buffer, &count);
			fake_orient.uvec.y = mf_ReadFloat(buffer, &count);
			fake_orient.uvec.z = mf_ReadFloat(buffer, &count);
			fake_orient.rvec.x = mf_ReadFloat(buffer, &count);
			fake_orient.rvec.y = mf_ReadFloat(buffer, &count);
			fake_orient.rvec.z = mf_ReadFloat(buffer, &count);
		}

		cinematic_data.max_time_play = mf_ReadFloat(buffer, &count);
		cinematic_data.text_display.max = mf_ReadFloat(buffer, &count);
		cinematic_data.text_display.min = mf_ReadFloat(buffer, &count);
		cinematic_data.track_target.max = mf_ReadFloat(buffer, &count);
		cinematic_data.track_target.min = mf_ReadFloat(buffer, &count);
		cinematic_data.player_disabled.max = mf_ReadFloat(buffer, &count);
		cinematic_data.player_disabled.min = mf_ReadFloat(buffer, &count);
		cinematic_data.in_camera_view.max = mf_ReadFloat(buffer, &count);
		cinematic_data.in_camera_view.min = mf_ReadFloat(buffer, &count);
		cinematic_data.quick_exit.max = mf_ReadFloat(buffer, &count);
		cinematic_data.quick_exit.min = mf_ReadFloat(buffer, &count);
		mf_ReadString(buffer, &count, text_string);

		old_camera_objnum = mf_ReadInt(buffer, &count);
		ASSERT(old_camera_objnum >= 0 && old_camera_objnum < MAX_OBJECTS);
		camera_objnum = Demo_obj_map[old_camera_objnum];
		ASSERT(camera_objnum >= 0 && camera_objnum < MAX_OBJECTS && Objects[camera_objnum].type == OBJ_CAMERA);
		camera_handle = Objects[camera_objnum].handle;

		Cinematic_StartCine(&cinematic_data, text_string, camera_handle);

	}break;
	case CDI_CANNED:
	{
		canned_data.type = mf_ReadInt(buffer, &count);
		canned_data.camera_pathid = mf_ReadInt(buffer, &count);
		canned_data.target_pathid = mf_ReadInt(buffer, &count);
		canned_data.target_objhandle = mf_ReadInt(buffer, &count);
		canned_data.time = mf_ReadFloat(buffer, &count);
		canned_data.object_to_use_for_point = mf_ReadInt(buffer, &count);
		canned_data.room = mf_ReadInt(buffer, &count);
		canned_data.pos.x = mf_ReadFloat(buffer, &count);
		canned_data.pos.y = mf_ReadFloat(buffer, &count);
		canned_data.pos.z = mf_ReadFloat(buffer, &count);
		canned_data.orient.fvec.x = mf_ReadFloat(buffer, &count);
		canned_data.orient.fvec.y = mf_ReadFloat(buffer, &count);
		canned_data.orient.fvec.z = mf_ReadFloat(buffer, &count);
		canned_data.orient.uvec.x = mf_ReadFloat(buffer, &count);
		canned_data.orient.uvec.y = mf_ReadFloat(buffer, &count);
		canned_data.orient.uvec.z = mf_ReadFloat(buffer, &count);
		canned_data.orient.rvec.x = mf_ReadFloat(buffer, &count);
		canned_data.orient.rvec.y = mf_ReadFloat(buffer, &count);
		canned_data.orient.rvec.z = mf_ReadFloat(buffer, &count);

		mf_ReadString(buffer, &count, text_string);
		canned_data.text_to_display = text_string;

		old_camera_objnum = mf_ReadInt(buffer, &count);
		ASSERT(old_camera_objnum >= 0 && old_camera_objnum < MAX_OBJECTS);
		camera_objnum = Demo_obj_map[old_camera_objnum];
		ASSERT(camera_objnum >= 0 && camera_objnum < MAX_OBJECTS && Objects[camera_objnum].type == OBJ_CAMERA);
		camera_handle = Objects[camera_objnum].handle;

		Cinematic_StartCanned(&canned_data, camera_handle);

	}break;
	}

}

void Cinematic_WriteDemoFileData(tCinematicDemoInfo* info)
{
	ubyte buffer[1500];
	int count = 0;

	// what kind of cinematic are we saving
	mf_WriteByte(buffer, &count, info->cinematic_type);

	switch (info->cinematic_type)
	{
	case CDI_NOT_CANNED:
	{

		mf_WriteInt(buffer, &count, info->cinematic_data->flags);
		mf_WriteInt(buffer, &count, info->cinematic_data->target_objhandle);
		mf_WriteInt(buffer, &count, info->cinematic_data->end_transition);
		mf_WriteInt(buffer, &count, info->cinematic_data->start_transition);
		mf_WriteInt(buffer, &count, info->cinematic_data->pathid);
		mf_WriteFloat(buffer, &count, info->cinematic_data->position.x);
		mf_WriteFloat(buffer, &count, info->cinematic_data->position.y);
		mf_WriteFloat(buffer, &count, info->cinematic_data->position.z);
		mf_WriteInt(buffer, &count, info->cinematic_data->room);

		if (info->cinematic_data->orient)
		{
			mf_WriteByte(buffer, &count, 1);
			mf_WriteFloat(buffer, &count, info->cinematic_data->orient->fvec.x);
			mf_WriteFloat(buffer, &count, info->cinematic_data->orient->fvec.y);
			mf_WriteFloat(buffer, &count, info->cinematic_data->orient->fvec.z);
			mf_WriteFloat(buffer, &count, info->cinematic_data->orient->uvec.x);
			mf_WriteFloat(buffer, &count, info->cinematic_data->orient->uvec.y);
			mf_WriteFloat(buffer, &count, info->cinematic_data->orient->uvec.z);
			mf_WriteFloat(buffer, &count, info->cinematic_data->orient->rvec.x);
			mf_WriteFloat(buffer, &count, info->cinematic_data->orient->rvec.y);
			mf_WriteFloat(buffer, &count, info->cinematic_data->orient->rvec.z);

		}
		else
		{
			mf_WriteByte(buffer, &count, 0);
		}

		mf_WriteFloat(buffer, &count, info->cinematic_data->max_time_play);
		mf_WriteFloat(buffer, &count, info->cinematic_data->text_display.max);
		mf_WriteFloat(buffer, &count, info->cinematic_data->text_display.min);
		mf_WriteFloat(buffer, &count, info->cinematic_data->track_target.max);
		mf_WriteFloat(buffer, &count, info->cinematic_data->track_target.min);
		mf_WriteFloat(buffer, &count, info->cinematic_data->player_disabled.max);
		mf_WriteFloat(buffer, &count, info->cinematic_data->player_disabled.min);
		mf_WriteFloat(buffer, &count, info->cinematic_data->in_camera_view.max);
		mf_WriteFloat(buffer, &count, info->cinematic_data->in_camera_view.min);
		mf_WriteFloat(buffer, &count, info->cinematic_data->quick_exit.max);
		mf_WriteFloat(buffer, &count, info->cinematic_data->quick_exit.min);

		if (info->text_string)
			mf_WriteString(buffer, &count, info->text_string);
		else
			mf_WriteByte(buffer, &count, 0);

		object* obj = ObjGet(info->camera_objhandle);
		ASSERT(obj && obj->type == OBJ_CAMERA);
		mf_WriteInt(buffer, &count, OBJNUM(obj));

	}break;
	case CDI_CANNED:
	{

		mf_WriteInt(buffer, &count, info->canned_data->type);
		mf_WriteInt(buffer, &count, info->canned_data->camera_pathid);
		mf_WriteInt(buffer, &count, info->canned_data->target_pathid);
		mf_WriteInt(buffer, &count, info->canned_data->target_objhandle);
		mf_WriteFloat(buffer, &count, info->canned_data->time);
		mf_WriteInt(buffer, &count, info->canned_data->object_to_use_for_point);
		mf_WriteInt(buffer, &count, info->canned_data->room);
		mf_WriteFloat(buffer, &count, info->canned_data->pos.x);
		mf_WriteFloat(buffer, &count, info->canned_data->pos.y);
		mf_WriteFloat(buffer, &count, info->canned_data->pos.z);
		mf_WriteFloat(buffer, &count, info->canned_data->orient.fvec.x);
		mf_WriteFloat(buffer, &count, info->canned_data->orient.fvec.y);
		mf_WriteFloat(buffer, &count, info->canned_data->orient.fvec.z);
		mf_WriteFloat(buffer, &count, info->canned_data->orient.uvec.x);
		mf_WriteFloat(buffer, &count, info->canned_data->orient.uvec.y);
		mf_WriteFloat(buffer, &count, info->canned_data->orient.uvec.z);
		mf_WriteFloat(buffer, &count, info->canned_data->orient.rvec.x);
		mf_WriteFloat(buffer, &count, info->canned_data->orient.rvec.y);
		mf_WriteFloat(buffer, &count, info->canned_data->orient.rvec.z);

		if (info->canned_data->text_to_display)
			mf_WriteString(buffer, &count, info->canned_data->text_to_display);
		else
			mf_WriteByte(buffer, &count, 0);

		object* obj = ObjGet(info->camera_objhandle);
		ASSERT(obj && obj->type == OBJ_CAMERA);
		mf_WriteInt(buffer, &count, OBJNUM(obj));

	}break;
	}

	DemoWriteCinematics(buffer, count);
}


void mf_WriteInt(ubyte* buffer, int* pointer, int data)
{
	memcpy(&buffer[(*pointer)], &data, sizeof(int));
	(*pointer) += sizeof(int);
}

void mf_WriteShort(ubyte* buffer, int* pointer, short data)
{
	memcpy(&buffer[(*pointer)], &data, sizeof(short));
	(*pointer) += sizeof(short);
}

void mf_WriteByte(ubyte* buffer, int* pointer, ubyte data)
{
	memcpy(&buffer[(*pointer)], &data, sizeof(ubyte));
	(*pointer) += sizeof(ubyte);
}

void mf_WriteFloat(ubyte* buffer, int* pointer, float data)
{
	memcpy(&buffer[(*pointer)], &data, sizeof(float));
	(*pointer) += sizeof(float);
}

void mf_WriteBytes(ubyte* buffer, int* pointer, ubyte* data, int len)
{
	memcpy(&buffer[(*pointer)], data, len);
	(*pointer) += len;
}

void mf_WriteString(ubyte* buffer, int* pointer, char* string)
{
	while (*string)
	{
		mf_WriteByte(buffer, pointer, (*string));
		string++;
	}
	mf_WriteByte(buffer, pointer, (*string));
}

int mf_ReadInt(ubyte* buffer, int* pointer)
{
	int value;
	memcpy(&value, &buffer[(*pointer)], sizeof(value));
	(*pointer) += sizeof(value);
	return value;
}

short mf_ReadShort(ubyte* buffer, int* pointer)
{
	short value;
	memcpy(&value, &buffer[(*pointer)], sizeof(value));
	(*pointer) += sizeof(value);
	return value;
}

ubyte mf_ReadByte(ubyte* buffer, int* pointer)
{
	ubyte value;
	memcpy(&value, &buffer[(*pointer)], sizeof(value));
	(*pointer) += sizeof(value);
	return value;
}

float mf_ReadFloat(ubyte* buffer, int* pointer)
{
	float value;
	memcpy(&value, &buffer[(*pointer)], sizeof(value));
	(*pointer) += sizeof(value);
	return value;
}

void mf_ReadBytes(ubyte* buffer, int* pointer, ubyte* data, int len)
{
	memcpy(data, &buffer[(*pointer)], len);
	(*pointer) += len;
}

void mf_ReadString(ubyte* buffer, int* pointer, char* string)
{
	ubyte data;
	data = mf_ReadByte(buffer, pointer);
	while (data)
	{
		*string = data;
		string++;
		data = mf_ReadByte(buffer, pointer);
	}
	*string = '\0';
}
