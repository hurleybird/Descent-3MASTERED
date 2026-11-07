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

#include <algorithm>
#include <vector>
#include "viseffect.h"
#include "fireball.h"
#include "terrain.h"
#include "game.h"
#include "room.h"
#include "vclip.h"
#include "bitmap.h"
#include "gametexture.h"
#include "object.h"
#include <memory.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <float.h>
#include "PHYSICS.H"
#include "weapon.h"
#include "lighting.h"
#include "dedicated_server.h"
#include "player.h"
#include "config.h"
#include "gameloop.h"
#include "weather.h"
#include "polymodel.h"
#include "renderer.h"
#include "../renderer/HardwareInternal.h"
#include "../renderer/SceneAnimation.h"
#include "psrand.h"
#include "mem.h"

vis_effect* VisEffects = NULL;
static short* Vis_free_list = NULL;
static std::vector<uint64_t> Vis_effect_generations;
static uint64_t Next_vis_effect_generation = 1;
uint64_t VisEffectGeneration(int slot) {
    return slot >= 0 && static_cast<size_t>(slot) < Vis_effect_generations.size() ? Vis_effect_generations[slot] : 0;
}
ushort* VisDeadList = NULL;

ushort max_vis_effects = 0;

int NumVisDead = 0;
int Highest_vis_effect_index = 0;
int Num_vis_effects = 0;

static double VisEffect_last_move_duration = 0.0;

static int Enhanced_snow_atlas_handle = BAD_BITMAP_HANDLE;
static int Enhanced_snow_fallback_handle = BAD_BITMAP_HANDLE;
static std::vector<int> Close_screen_weapon_object_handles;


static float EnhancedSnowGaussian(float x, float y, float cx, float cy,
	float radius_x, float radius_y, float angle_radians)
{
	const float c = cosf(angle_radians);
	const float s = sinf(angle_radians);
	const float dx = x - cx;
	const float dy = y - cy;
	const float along = (dx * c + dy * s) / radius_x;
	const float across = (-dx * s + dy * c) / radius_y;
	return expf(-(along * along + across * across) * 2.2f);
}

static float EnhancedSnowCoverage(int variant, float x, float y)
{
	float coverage;
	if (variant == 0)
	{
		coverage = EnhancedSnowGaussian(x, y, 0.0f, 0.0f, 0.66f, 0.19f, 0.08f);
		coverage = std::max(coverage, 0.62f *
			EnhancedSnowGaussian(x, y, -0.23f, 0.18f, 0.30f, 0.13f, -0.35f));
	}
	else if (variant == 1)
	{
		coverage = EnhancedSnowGaussian(x, y, -0.15f, -0.02f, 0.42f, 0.25f, 0.24f);
		coverage = std::max(coverage, 0.82f *
			EnhancedSnowGaussian(x, y, 0.27f, 0.13f, 0.29f, 0.20f, -0.26f));
		coverage = std::max(coverage, 0.48f *
			EnhancedSnowGaussian(x, y, 0.03f, -0.28f, 0.22f, 0.12f, 0.08f));
	}
	else if (variant == 2)
	{
		coverage = EnhancedSnowGaussian(x, y, 0.0f, 0.0f, 0.74f, 0.115f, 0.34f);
		coverage = std::max(coverage, 0.38f *
			EnhancedSnowGaussian(x, y, -0.08f, -0.11f, 0.40f, 0.17f, 0.05f));
	}
	else
	{
		coverage = EnhancedSnowGaussian(x, y, 0.0f, 0.0f, 0.53f, 0.17f, -0.18f);
		coverage = std::max(coverage, 0.55f *
			EnhancedSnowGaussian(x, y, 0.18f, -0.16f, 0.24f, 0.10f, 0.30f));
	}

	return coverage > 1.0f ? 1.0f : coverage;
}

static ushort EnhancedSnowPixel(int variant, int x, int y)
{
	float coverage = 0.0f;
	for (int sy = 0; sy < 2; ++sy)
	{
		for (int sx = 0; sx < 2; ++sx)
		{
			const float px = ((x + (sx + 0.5f) * 0.5f) / 32.0f) * 2.0f - 1.0f;
			const float py = ((y + (sy + 0.5f) * 0.5f) / 32.0f) * 2.0f - 1.0f;
			coverage += EnhancedSnowCoverage(variant, px, py);
		}
	}
	coverage *= 0.25f;
	if (coverage <= 0.025f)
		return NEW_TRANSPARENT_COLOR;

	int value = (int)(30.0f + 225.0f * powf(coverage, 0.65f));
	if (value > 255)
		value = 255;
	return GR_RGB16(value - 12, value - 5, value) | OPAQUE_FLAG;
}

static bool VisEffectCreateEnhancedSnowTextures()
{
	if (Enhanced_snow_atlas_handle > BAD_BITMAP_HANDLE &&
		GameBitmaps[Enhanced_snow_atlas_handle].used &&
		Enhanced_snow_fallback_handle > BAD_BITMAP_HANDLE &&
		GameBitmaps[Enhanced_snow_fallback_handle].used)
	{
		return true;
	}

	Enhanced_snow_atlas_handle = bm_AllocBitmap(128, 32, 0);
	Enhanced_snow_fallback_handle = bm_AllocBitmap(32, 32, 0);
	if (Enhanced_snow_atlas_handle <= BAD_BITMAP_HANDLE ||
		Enhanced_snow_fallback_handle <= BAD_BITMAP_HANDLE)
	{
		if (Enhanced_snow_atlas_handle > BAD_BITMAP_HANDLE)
			bm_FreeBitmap(Enhanced_snow_atlas_handle);
		if (Enhanced_snow_fallback_handle > BAD_BITMAP_HANDLE)
			bm_FreeBitmap(Enhanced_snow_fallback_handle);
		Enhanced_snow_atlas_handle = BAD_BITMAP_HANDLE;
		Enhanced_snow_fallback_handle = BAD_BITMAP_HANDLE;
		return false;
	}

	ushort* atlas = bm_data(Enhanced_snow_atlas_handle, 0);
	ushort* fallback = bm_data(Enhanced_snow_fallback_handle, 0);
	for (int y = 0; y < 32; ++y)
	{
		for (int x = 0; x < 32; ++x)
		{
			fallback[y * 32 + x] = EnhancedSnowPixel(0, x, y);
			for (int variant = 0; variant < 4; ++variant)
				atlas[y * 128 + variant * 32 + x] = EnhancedSnowPixel(variant, x, y);
		}
	}
	GameBitmaps[Enhanced_snow_atlas_handle].flags |= BF_CHANGED;
	bm_MarkChanged(Enhanced_snow_atlas_handle);
	GameBitmaps[Enhanced_snow_fallback_handle].flags |= BF_CHANGED;
	bm_MarkChanged(Enhanced_snow_fallback_handle);
	return true;
}

int PrepareEnhancedSnowAtlas() {
	return VisEffectCreateEnhancedSnowTextures() ? Enhanced_snow_atlas_handle : BAD_BITMAP_HANDLE;
}


bool VisEffectViewerIsFirstPersonLocalPlayerView()
{
	if (Player_object == NULL || Viewer_object == NULL)
		return false;
	if (Viewer_object != Player_object)
		return false;
	if (Players[Player_num].flags & PLAYER_FLAGS_REARVIEW)
		return false;

	return true;
}


void ShutdownVisEffects()
{
	if (Enhanced_snow_atlas_handle > BAD_BITMAP_HANDLE)
		bm_FreeBitmap(Enhanced_snow_atlas_handle);
	if (Enhanced_snow_fallback_handle > BAD_BITMAP_HANDLE)
		bm_FreeBitmap(Enhanced_snow_fallback_handle);
	Enhanced_snow_atlas_handle = BAD_BITMAP_HANDLE;
	Enhanced_snow_fallback_handle = BAD_BITMAP_HANDLE;
	if (VisEffects)
		mem_free(VisEffects);
	if (VisDeadList)
		mem_free(VisDeadList);
	if (Vis_free_list)
		mem_free(Vis_free_list);
}

// Goes through our array and clears the slots out
void InitVisEffects()
{
	static ushort old_max_vis = 0;
	max_vis_effects = MAX_VIS_EFFECTS;		//always peg to max on PC
	Close_screen_weapon_object_handles.clear();

	if (old_max_vis == max_vis_effects)
		return;

	if (VisEffects != NULL)
	{
		VisEffects = (vis_effect*)mem_realloc(VisEffects, sizeof(vis_effect) * max_vis_effects);
		VisDeadList = (ushort*)mem_realloc(VisDeadList, sizeof(ushort) * max_vis_effects);
		Vis_free_list = (short*)mem_realloc(Vis_free_list, sizeof(short) * max_vis_effects);
	}
	else if (VisEffects == NULL)
	{
		VisEffects = (vis_effect*)mem_malloc(sizeof(vis_effect) * max_vis_effects);
		VisDeadList = (ushort*)mem_malloc(sizeof(ushort) * max_vis_effects);
		Vis_free_list = (short*)mem_malloc(sizeof(short) * max_vis_effects);
}
	for (int i = 0; i < max_vis_effects; i++)
	{
		VisEffects[i].type = VIS_NONE;
		VisEffects[i].roomnum = -1;
		VisEffects[i].prev = -1;
		VisEffects[i].next = -1;
		Vis_free_list[i] = i;
	}
	old_max_vis = max_vis_effects;
	Num_vis_effects = 0;
	Highest_vis_effect_index = 0;

	atexit(ShutdownVisEffects);
}


// Returns the next free viseffect
int VisEffectAllocate()
{
	if (Num_vis_effects == max_vis_effects)
	{
		mprintf((0, "Couldn't allocate vis effect!\n"));
		return -1;
	}

	int n = Vis_free_list[Num_vis_effects++];
	if (Vis_effect_generations.size() <= static_cast<size_t>(n)) Vis_effect_generations.resize(n+1);
	Vis_effect_generations[n] = Next_vis_effect_generation++;
	ASSERT(VisEffects[n].type == VIS_NONE);	// Get Jason

	if (n > Highest_vis_effect_index)
	{
		Highest_vis_effect_index = n;
	}

	return n;
}

// Frees up a viseffect for use
int VisEffectFree(int visnum)
{
	ASSERT(visnum >= 0 && visnum <= max_vis_effects);

	Vis_free_list[--Num_vis_effects] = visnum;
	VisEffects[visnum].type = VIS_NONE;

	if (visnum == Highest_vis_effect_index)
	{
		while (VisEffects[Highest_vis_effect_index].type != VIS_NONE && Highest_vis_effect_index > 0)
			Highest_vis_effect_index--;
	}

	return 1;
}

int VisEffectInitType(vis_effect* vis)
{
	int ret = 1;

	vis->size = Fireballs[vis->id].size;
	vis->flags |= VF_USES_LIFELEFT;
	vis->lifeleft = Fireballs[vis->id].total_life;
	vis->lifetime = vis->lifeleft;

	if (Fireballs[vis->id].type == FT_EXPLOSION && vis->id != CUSTOM_EXPLOSION_INDEX && vis->id != NAPALM_BALL_INDEX)
		vis->lighting_color = OPAQUE_FLAG | GR_RGB16(255, 180, 20);

	return ret;
}


//initialize a new viseffect.  adds to the list for the given room
//returns the vis number
int VisEffectCreate(ubyte type, ubyte id, int roomnum, vector* pos)
{
	int visnum;
	vis_effect* vis;

	if ((Game_mode & GM_MULTI) && Dedicated_server)
		return -1;	// Dedicated servers don't need to create vis effects

	if (ROOMNUM_OUTSIDE(roomnum))
	{
		if (CELLNUM(roomnum) > TERRAIN_WIDTH * TERRAIN_DEPTH)
			return -1;
		if (CELLNUM(roomnum) < 0)
			return -1;

		if (Gametime - Last_terrain_render_time > 5.0)
			return -1;
	}
	else
	{
		ASSERT(roomnum >= 0 && roomnum <= Highest_room_index && Rooms[roomnum].used);

		// Don't create viseffects in rooms we haven't seen in a while
		if (Gametime - Rooms[roomnum].last_render_time > 5.0)
			return -1;
	}

	// Find next free object
	visnum = VisEffectAllocate();

	if (visnum == -1)		//no free objects
		return -1;

	ASSERT(VisEffects[visnum].type == VIS_NONE);		//make sure unused 

	vis = &VisEffects[visnum];

	ASSERT(vis->roomnum == -1);

	memset(vis, 0, sizeof(vis_effect));

	//Fill in fields
	vis->type = VIS_FIREBALL;
	vis->id = id;
	vis->pos = *pos;
	vis->flags = 0;
	vis->phys_flags = 0;
	vis->roomnum = roomnum;
	vis->movement_type = MT_NONE;
	vis->attach_info.end_vertnum = -1;
	vis->next = -1;
	vis->prev = -1;
	vis->lighting_color = 0;

	ASSERT(roomnum != -1);

	vis->creation_time = Gametime;

	//Initialize all the type-specific info
	VisEffectInitType(vis);

	VisEffectLink(visnum, roomnum);

	return visnum;
}

bool VisEffectIsCloseScreenSourceObject(object* obj)
{
	if (obj == NULL)
		return false;
	if (!VisEffectViewerIsFirstPersonLocalPlayerView())
		return false;

	return obj == Player_object;
}

bool VisEffectIsLocalPlayerAttachedSourceObject(object* obj)
{
	if (obj == NULL || Player_object == NULL)
		return false;
	if (!VisEffectViewerIsFirstPersonLocalPlayerView())
		return false;

	if (obj == Player_object)
		return true;

	return (obj->flags & OF_ATTACHED) != 0 && ObjGet(obj->attach_ultimate_handle) == Player_object;
}

bool VisEffectIsLocalPlayerViewSourceObject(object* obj)
{
	if (obj == NULL || Player_object == NULL)
		return false;

	return VisEffectIsLocalPlayerAttachedSourceObject(obj);
}

bool VisEffectIsNearLocalPlayerView(object* obj, float padding)
{
	if (obj == NULL || Player_object == NULL || Viewer_object == NULL)
		return false;
	if (!VisEffectViewerIsFirstPersonLocalPlayerView())
		return false;

	float player_view_distance = vm_VectorDistanceQuick(&Player_object->pos, &Viewer_object->pos);
	float player_envelope = player_view_distance + Player_object->size + obj->size + padding;
	if (vm_VectorDistanceQuick(&obj->pos, &Player_object->pos) <= player_envelope)
		return true;

	float viewer_envelope = obj->size + padding;
	return vm_VectorDistanceQuick(&obj->pos, &Viewer_object->pos) <= viewer_envelope;
}


void VisEffectMarkCloseScreenWeaponObject(object* obj)
{
	if (obj == NULL || obj->handle == OBJECT_HANDLE_NONE)
		return;
	if (!VisEffectViewerIsFirstPersonLocalPlayerView())
		return;

	if (std::find(Close_screen_weapon_object_handles.begin(), Close_screen_weapon_object_handles.end(), obj->handle) != Close_screen_weapon_object_handles.end())
		return;

	if (Close_screen_weapon_object_handles.size() >= MAX_OBJECTS)
		Close_screen_weapon_object_handles.erase(Close_screen_weapon_object_handles.begin());

	Close_screen_weapon_object_handles.push_back(obj->handle);
}

bool VisEffectWasMarkedCloseScreenWeaponObject(const object* obj)
{
	if (obj == NULL || obj->type != OBJ_WEAPON)
		return false;

	return std::find(Close_screen_weapon_object_handles.begin(), Close_screen_weapon_object_handles.end(), obj->handle) != Close_screen_weapon_object_handles.end();
}
bool VisEffectIsCloseScreenWeaponObject(object* obj)
{
	return VisEffectViewerIsFirstPersonLocalPlayerView() && VisEffectWasMarkedCloseScreenWeaponObject(obj);
}

// Creates vis effects but has the caller set their parameters
//initialize a new viseffect.  adds to the list for the given room
//returns the vis number
int VisEffectCreateControlled(ubyte type, object* parent, ubyte id, int roomnum, vector* pos, float lifetime, vector* velocity, int phys_flags, float size, float drag, float mass, bool isreal)
{
	int visnum;
	vis_effect* vis;
	static int napalm_id = -1;

	if (napalm_id == -1)
		napalm_id = FindWeaponName("Napalm");

	if (isreal)
	{
		ASSERT(parent != NULL);	// You have a spew that has no parent object?

		if (ROOMNUM_OUTSIDE(roomnum))
		{
			if (CELLNUM(roomnum) > TERRAIN_WIDTH * TERRAIN_DEPTH)
				return -1;
			if (CELLNUM(roomnum) < 0)
				return -1;

			if (Gametime - Last_terrain_render_time > 5.0)
				return -1;
		}
		else
		{
			ASSERT(roomnum >= 0 && roomnum <= Highest_room_index && Rooms[roomnum].used);

			// Don't create real objects in rooms we haven't seen in a while
			if (Gametime - Rooms[roomnum].last_render_time > 5.0)
				return -1;
		}

		int objnum = CreateAndFireWeapon(pos, velocity, parent, napalm_id);
		if (objnum >= 0)
		{
			object* obj = &Objects[objnum];

			obj->lifeleft = lifetime;
			obj->lifetime = lifetime;
			obj->movement_type = MT_PHYSICS;

			if (mass > 0 && drag > 0)
			{
				obj->mtype.phys_info.mass = mass;
				obj->mtype.phys_info.drag = drag;
			}

			if (size > 0)
				obj->size = size;

			obj->mtype.phys_info.velocity = *velocity;

			if (phys_flags)
				obj->mtype.phys_info.flags = phys_flags;

			obj->ctype.laser_info.casts_light = false;
			if (VisEffectIsLocalPlayerAttachedSourceObject(parent))
				VisEffectMarkCloseScreenWeaponObject(obj);

		}

		return objnum;
	}

	visnum = VisEffectCreate(type, id, roomnum, pos);

	if (visnum < 0)
		return -1;		// No vis effects free

	vis = &VisEffects[visnum];

	vis->lifeleft = lifetime;
	vis->lifetime = lifetime;
	vis->movement_type = MT_PHYSICS;
	vis->mass = mass;
	vis->drag = drag;

	if (size > 0)
		vis->size = size;

	vis->velocity = *velocity;

	//float mag=vm_GetMagnitudeFast (&vis->velocity);
	//mprintf ((0,"CREATION:Velocity mag is %f\n",mag));

	if (phys_flags)
		vis->phys_flags = phys_flags;

	if (ROOMNUM_OUTSIDE(roomnum))
		vis->phys_flags |= PF_NO_COLLIDE;

	vis->flags |= VF_NO_Z_ADJUST;
	if (VisEffectIsCloseScreenSourceObject(parent) || VisEffectIsLocalPlayerAttachedSourceObject(parent))
		vis->flags |= VF_CLOSE_SCREEN_EFFECT;
	vis->lighting_color &= ~(0x8000);


	return visnum;
}


//link the viseffect  into the list for its room
// Does nothing for effects over terrain
void VisEffectLink(int visnum, int roomnum)
{
	vis_effect* vis = &VisEffects[visnum];

	ASSERT(visnum != -1);

	if (!ROOMNUM_OUTSIDE(vis->roomnum))
		ASSERT(roomnum >= 0 && roomnum <= Highest_room_index);

	vis->roomnum = roomnum;

	if (ROOMNUM_OUTSIDE(vis->roomnum))
		return;

	vis->next = Rooms[roomnum].vis_effects;
	Rooms[roomnum].vis_effects = visnum;
	ASSERT(vis->next != visnum);

	vis->prev = -1;

	if (vis->next != -1)
		VisEffects[vis->next].prev = visnum;
}

// Unlinks a viseffect from a room
// Does nothing for terrain
void VisEffectUnlink(int visnum)
{
	vis_effect* vis = &VisEffects[visnum];

	ASSERT(visnum != -1);

	if (ROOMNUM_OUTSIDE(vis->roomnum))
		return;
	else
	{

		room* rp = &Rooms[vis->roomnum];

		if (vis->prev == -1)
			rp->vis_effects = vis->next;
		else
			VisEffects[vis->prev].next = vis->next;

		if (vis->next != -1)
			VisEffects[vis->next].prev = vis->prev;

		vis->roomnum = -1;
	}
}

//when an effect has moved into a new room, this function unlinks it
//from its old room and links it into the new room
void VisEffectRelink(int visnum, int newroomnum)
{
	ASSERT(visnum >= 0 && visnum < max_vis_effects);

	VisEffectUnlink(visnum);
	VisEffectLink(visnum, newroomnum);
}



// Kills all the effects that are dead
void VisEffectDeleteDead()
{
	int i;
	for (i = 0; i < NumVisDead; i++)
	{
		VisEffectDelete(VisDeadList[i]);
	}

	NumVisDead = 0;
}


//remove viseffect from the world
void VisEffectDelete(int visnum)
{
	vis_effect* vis = &VisEffects[visnum];

	ASSERT(visnum != -1);
	ASSERT(vis->type != VIS_NONE);

	VisEffectUnlink(visnum);

	vis->type = VIS_NONE;		//unused!
	vis->roomnum = -1;				// zero it!

	VisEffectFree(visnum);
}

// Frees all the objects that are currently in use
void FreeAllVisEffects()
{
	Close_screen_weapon_object_handles.clear();

	for (int i = 0; i < max_vis_effects; i++)
		if (VisEffects[i].type != VIS_NONE)
			VisEffectDelete(i);
}

// Creates a some sparks that go in random directions
void CreateRandomLineSparks(int num_sparks, vector* pos, int roomnum, ushort color, float force_scalar)
{
	// Make more sparks if Katmai
	if (Katmai)
		num_sparks *= 2;

	// Create some sparks
	for (int i = 0; i < num_sparks; i++)
	{
		int visnum = VisEffectCreate(VIS_FIREBALL, FADING_LINE_INDEX, roomnum, pos);
		if (visnum >= 0)
		{
			vis_effect* vis = &VisEffects[visnum];

			vis->movement_type = MT_PHYSICS;
			vis->mass = 500;
			vis->drag = .001f;
			vis->phys_flags |= PF_GRAVITY | PF_NO_COLLIDE;

			vis->velocity.x = (ps_rand() % 100) - 50;
			vis->velocity.y = (ps_rand() % 100);
			vis->velocity.z = (ps_rand() % 100) - 50;

			vm_NormalizeVectorFast(&vis->velocity);

			vis->velocity *= 20 + (ps_rand() % 10);
			vis->velocity *= force_scalar;
			vis->size = .7 + ((ps_rand() % 10) * .04);
			vis->flags |= VF_USES_LIFELEFT;
			float lifetime = 1 + ((ps_rand() % 10) * .15);
			vis->lifeleft = lifetime;
			vis->lifetime = lifetime;

			if (color == 0)
				vis->lighting_color = GR_RGB16(200 + (ps_rand() % 50), 150 + (ps_rand() % 50), ps_rand() % 50);
			else
				vis->lighting_color = color;
		}
	}
}


// Creates a some sparks that go in random directions
template<class Random>
static void CreateRandomSparksFrom(Random& random,int num_sparks,vector* pos,int roomnum,int which_index,float force_scalar)
{
	// Make more sparks if Katmai
	if (Katmai)
		num_sparks *= 2;


	// Create some sparks
	for (int i = 0; i < num_sparks; i++)
	{
		int sparknum;
		int index;

		if (random() % 2)
			index = HOT_SPARK_INDEX;
		else
			index = COOL_SPARK_INDEX;

		if (which_index != -1)
			index = which_index;

		sparknum = VisEffectCreate(VIS_FIREBALL, index, roomnum, pos);

		if (sparknum >= 0)
		{
			vis_effect* vis = &VisEffects[sparknum];

			vis->movement_type = MT_PHYSICS;
			vis->mass = 100;
			vis->drag = .1f;

			vis->phys_flags |= PF_GRAVITY | PF_NO_COLLIDE;

			vis->velocity.x = (random() % 100) - 50;
			vis->velocity.y = (random() % 100);
			vis->velocity.z = (random() % 100) - 50;

			vm_NormalizeVectorFast(&vis->velocity);
			vis->velocity *= 10 + (random() % 10);
			vis->velocity *= force_scalar;
			vis->size = .2 + ((random() % 10) * .01);
			vis->flags |= VF_USES_LIFELEFT;
			float lifetime = 1 + ((random() % 10) * .15);
			vis->lifeleft = lifetime;
			vis->lifetime = lifetime;
		}
	}
}

void CreateRandomSparks(int num_sparks,vector* pos,int roomnum,int which_index,float force_scalar)
{
	auto random = [] { return ps_rand(); };
	CreateRandomSparksFrom(random,num_sparks,pos,roomnum,which_index,force_scalar);
}

void CreateRandomSparksWithRandom(PSRand& random,int num_sparks,vector* pos,int roomnum,int which_index,float force_scalar)
{
	CreateRandomSparksFrom(random,num_sparks,pos,roomnum,which_index,force_scalar);
}

// Creates a some particles that go in random directions
void CreateRandomParticles(int num_sparks, vector* pos, int roomnum, int bm_handle,
	float size, float life, bool close_screen_effect, float age)
{
	// Create some sparks
	float tenth_life = life / 10.0;
	float tenth_size = size / 10.0;

	for (int i = 0; i < num_sparks; i++)
	{
		int sparknum;

		sparknum = VisEffectCreate(VIS_FIREBALL, PARTICLE_INDEX, roomnum, pos);

		if (sparknum >= 0)
		{
			vis_effect* vis = &VisEffects[sparknum];


			vis->movement_type = MT_PHYSICS;
			vis->mass = 100;
			vis->drag = .1f;

			vis->phys_flags |= PF_GRAVITY | PF_NO_COLLIDE;

			vis->velocity.x = (ps_rand() % 100) - 50;
			vis->velocity.y = (ps_rand() % 100);
			vis->velocity.z = (ps_rand() % 100) - 50;

			vm_NormalizeVectorFast(&vis->velocity);
			vis->velocity *= 10 + (ps_rand() % 10);
			vis->pos += vis->velocity * age;
			vis->size = size + (((ps_rand() % 11) - 5) * tenth_size);
			vis->flags |= VF_USES_LIFELEFT;
			float lifetime = life + (((ps_rand() % 11) - 5) * tenth_life);
			vis->lifeleft = lifetime - age;
			vis->lifetime = lifetime;
			vis->creation_time -= age;
			vis->custom_handle = bm_handle;
			if (close_screen_effect)
				vis->flags |= VF_CLOSE_SCREEN_EFFECT;
		}
	}
}





void VisEffectSetDeadFlag(vis_effect* vis)
{
	if (vis->flags & VF_DEAD)
		return;
	if (vis->type == VIS_NONE)
		return;

	vis->flags |= VF_DEAD;

	VisDeadList[NumVisDead++] = vis - VisEffects;
}

// Moves 
void VisEffectMoveOne(vis_effect* vis)
{
	if (vis->flags & VF_USES_LIFELEFT)
		vis->lifeleft -= Frametime;		//...inevitable countdown towards death

	// Chris, do your stuff here
	if (vis->movement_type == MT_PHYSICS)
		do_vis_physics_sim(vis);

	if (vis->flags & VF_USES_LIFELEFT)
	{
		if (vis->lifeleft < 0)
			VisEffectSetDeadFlag(vis);
	}

	if (vis->id == SNOWFLAKE_INDEX)
	{
		const bool enhanced = (vis->flags & VF_ENHANCED_SNOW) != 0;
		if (enhanced != Render_enhanced_weather)
		{
			VisEffectSetDeadFlag(vis);
			return;
		}

		Weather.snowflakes_to_create++;
		if (enhanced)
		{
			if (!(vis->custom_handle & 4))
			{
				const float time_live = Gametime - vis->creation_time;
				const float phase = vis->mass + time_live * vis->drag;
				const float flutter_speed = sinf(phase) * (2.0f + vis->size * 3.5f);
				vector frame_velocity = vis->velocity;
				frame_velocity.x += vis->end_pos.x * flutter_speed;
				frame_velocity.z += vis->end_pos.z * flutter_speed;
				frame_velocity.y += cosf(phase * 0.71f) * 0.8f;
				vis->pos += frame_velocity * Frametime;

				if (vis->pos.y - vis->end_pos.y < 24.0f)
				{
					vector ground_normal;
					const float ground_y = GetTerrainGroundPoint(&vis->pos, &ground_normal);
					vis->end_pos.y = ground_y;
					if (vis->pos.y <= ground_y + 0.12f)
					{
						vis->pos.y = ground_y + 0.04f;
						vis->velocity = ground_normal;
						vis->custom_handle |= 4;
						if (vis->lifeleft > 0.32f)
							vis->lifeleft = 0.32f;
					}
				}
			}

			if (Viewer_object)
			{
				const vector delta = vis->pos - Viewer_object->pos;
				const float horizontal_distance_squared =
					delta.x * delta.x + delta.z * delta.z;
				vector view_forward = Viewer_object->orient.fvec;
				view_forward.y = 0.0f;
				vm_NormalizeVector(&view_forward);
				const float forward_distance =
					delta.x * view_forward.x + delta.z * view_forward.z;
				if (horizontal_distance_squared > 390.0f * 390.0f ||
					delta.y < -150.0f || delta.y > 195.0f || forward_distance < -90.0f)
				{
					VisEffectSetDeadFlag(vis);
				}
			}
		}
		else
		{
			vis->pos += vis->velocity * Frametime;
			if (vis->pos.y < 1)
				VisEffectSetDeadFlag(vis);
		}
	}

	// Do attached viseffect stuff here
	if (vis->flags & VF_ATTACHED)
	{
		int objnum = vis->attach_info.obj_handle & HANDLE_OBJNUM_MASK;
		uint sig = vis->attach_info.obj_handle & HANDLE_COUNT_MASK;
		object* obj = &Objects[objnum];

		if ((obj->flags & OF_DEAD) || (obj->handle & HANDLE_COUNT_MASK) != sig)
		{
			// The object we're attached to doesn't exist anymore
			VisEffectSetDeadFlag(vis);

		}
		else if (obj->type == OBJ_PLAYER && (Players[obj->id].flags & (PLAYER_FLAGS_DYING | PLAYER_FLAGS_DEAD)))
		{
			// The object we're attached to doesn't exist anymore
			VisEffectSetDeadFlag(vis);
		}
		else
		{
			if (vis->id == THICK_LIGHTNING_INDEX)
			{
				if (vis->flags & VF_PLANAR)
				{
					// Do object to object attachment
					int dest_objnum = vis->attach_info.dest_objhandle & HANDLE_OBJNUM_MASK;
					uint dest_sig = vis->attach_info.dest_objhandle & HANDLE_COUNT_MASK;
					object* dest_obj = &Objects[dest_objnum];

					if ((dest_obj->flags & OF_DEAD) || (dest_obj->handle & HANDLE_COUNT_MASK) != dest_sig)
					{
						// The object we're attached to doesn't exist anymore
						VisEffectSetDeadFlag(vis);
						return;
					}

					if (dest_obj->type == OBJ_PLAYER && (Players[dest_obj->id].flags & (PLAYER_FLAGS_DYING | PLAYER_FLAGS_DEAD)))
					{
						VisEffectSetDeadFlag(vis);
						return;
					}

					vis->pos = obj->pos;
					vis->end_pos = dest_obj->pos;

					// If were are shooting to the exact center of the viewer object then move
					// the positions a little bit or it will look wrong:
					if (obj == Viewer_object)
					{
						vis->pos -= (obj->orient.uvec * .1f);
					}

					if (dest_obj == Viewer_object)
					{
						vis->end_pos -= (dest_obj->orient.uvec * .1f);
					}

				}
				else
				{
					if (obj->rtype.pobj_info.model_num != vis->attach_info.modelnum)
					{
						VisEffectSetDeadFlag(vis);
					}
					else
					{
						WeaponCalcGun(&vis->pos, NULL, obj, vis->attach_info.vertnum);
						WeaponCalcGun(&vis->end_pos, NULL, obj, vis->attach_info.end_vertnum);
					}
				}
			}
			else
			{
				float normalized_time[MAX_SUBOBJECTS];

				if (obj->lowest_attached_vis == -1)
				{
					obj->lowest_attached_vis = vis - VisEffects;
					poly_model* pm = &Poly_models[obj->rtype.pobj_info.model_num];

					int i;

					SetNormalizedTimeObj(obj, normalized_time);


					for (i = vis - VisEffects; i <= Highest_vis_effect_index; i++)
					{
						vis_effect* this_vis = &VisEffects[i];
						if (this_vis->type != VIS_NONE && (this_vis->flags & VF_ATTACHED) && ((this_vis->attach_info.obj_handle & HANDLE_OBJNUM_MASK) == objnum) && this_vis->id != THICK_LIGHTNING_INDEX)
						{
							bsp_info* sm = &pm->submodel[this_vis->attach_info.subnum];
							GetPolyModelPointInWorld(&this_vis->pos, pm, &obj->pos, &obj->orient, this_vis->attach_info.subnum, normalized_time, &sm->verts[this_vis->attach_info.vertnum], NULL);

							if (this_vis->attach_info.end_vertnum != -1)
								GetPolyModelPointInWorld(&this_vis->end_pos, pm, &obj->pos, &obj->orient, this_vis->attach_info.subnum, normalized_time, &sm->verts[this_vis->attach_info.end_vertnum], NULL);
						}
					}
				}
			}

			// Relink if need be
			if (obj->roomnum != vis->roomnum)
				VisEffectRelink(vis - VisEffects, obj->roomnum);

		}
	}


}

// Moves our visuals
void VisEffectMoveAll()
{
	int i;
	const double move_start = Perf_markers_enabled ? PerfMarkersNow() : 0.0;

	for (i = 0; i <= Highest_vis_effect_index; i++)
	{
		if (VisEffects[i].type != VIS_NONE)
		{
			VisEffectMoveOne(&VisEffects[i]);
		}
	}

	if (Perf_markers_enabled)
		VisEffect_last_move_duration = PerfMarkersNow() - move_start;

}

void VisEffectRecordPerfMarkers()
{
	if (!Perf_markers_enabled)
		return;

	const double marker_time = PerfMarkersNow();
	PerfMarkersRecordDuration("VisEffect.MoveAll", marker_time, VisEffect_last_move_duration);

}

/*
// Attaches viseffects that move with an object
void AttachRandomVisEffectsToObject (int num,int handle,object *obj)
{
	int i;
	vector zero_pos={0,0,0};
	poly_model *pm = &Poly_models[obj->rtype.pobj_info.model_num];



	for (i=0;i<num;i++)
	{

		int visnum=VisEffectCreate(VIS_FIREBALL,NAPALM_BALL_INDEX,obj->roomnum,&zero_pos);
		if (visnum>=0)
		{
			vis_effect *vis=&VisEffects[visnum];
			vis->size=1.0;
			vis->lifetime=15.0;
			vis->lifeleft=15.0;
			vis->flags |=VF_ATTACHED;
			vis->attach_info.obj_handle=obj->handle;

			int subnum=ps_rand()%pm->n_models;
			bsp_info *sm=&pm->submodel[subnum];

			vis->attach_info.subnum=subnum;
			vis->attach_info.vertnum=ps_rand()%sm->nverts;

		}
	}
}*/

// Attaches viseffects that move with an object
void AttachRandomNapalmEffectsToObject(object* obj)
{
	if (obj->flags & OF_DEAD)
		return;
	float effect_ages[8] = {};
	const int effect_events = Get60HzVisualEventAges(OBJNUM(obj), obj->handle,
		VIS60_ATTACHED_NAPALM, effect_ages, 8);
	if (effect_events == 0)
		return;

	vector velocity_norm = obj->mtype.phys_info.velocity;
	vm_NormalizeVector(&velocity_norm);
	vector pos = obj->pos - (velocity_norm * (obj->size / 2));

	// Napalm effects here are attached to the burning object. Only napalm attached
	// to the local player belongs to the close-screen late pass.
	const bool close_screen_napalm = VisEffectIsLocalPlayerAttachedSourceObject(obj);
	float size_scalar = obj->size / 7.0;

	size_scalar = std::max(1.0f, size_scalar);
	size_scalar = std::min(4.0f, size_scalar);

	for (int event = 0; event < effect_events; ++event)
	{
		if ((obj->movement_type == MT_PHYSICS && OBJECT_OUTSIDE(obj) &&
			(ps_rand() % 3) == 0) || (ps_rand() % 3) == 0)
		{
			int smoke_visnum = CreateFireball(&pos, BLACK_SMOKE_INDEX, obj->roomnum, VISUAL_FIREBALL);
			if (smoke_visnum >= 0)
			{
				VisEffects[smoke_visnum].creation_time -= effect_ages[event];
				VisEffects[smoke_visnum].lifeleft -= effect_ages[event];
				if (close_screen_napalm)
					VisEffects[smoke_visnum].flags |= VF_CLOSE_SCREEN_EFFECT;
			}
		}

		// Create an explosion that follows every now and then.
		if ((ps_rand() % 3) != 0)
			continue;
		if (!(obj->flags & OF_POLYGON_OBJECT))
			return;

		int num = 1;

		num += (obj->size / 15);

		for (int i = 0; i < num; i++)
		{
			vector dest;
			poly_model* pm = &Poly_models[obj->rtype.pobj_info.model_num];

			if (pm->n_models == 0)
				return;

			int subnum = ps_rand() % pm->n_models;

			if (IsNonRenderableSubmodel(pm, subnum))
				continue;

			bsp_info* sm = &pm->submodel[subnum];

			if (sm->nverts == 0)
				return;

			int vertnum = ps_rand() % sm->nverts;

			GetPolyModelPointInWorld(&dest, &Poly_models[obj->rtype.pobj_info.model_num], &obj->pos, &obj->orient, subnum, &sm->verts[vertnum]);
			int visnum = VisEffectCreate(VIS_FIREBALL, GetRandomSmallExplosion(), obj->roomnum, &dest);
			if (visnum == -1)
				return;

			if (close_screen_napalm)
				VisEffects[visnum].flags |= VF_CLOSE_SCREEN_EFFECT;

			VisEffects[visnum].creation_time -= effect_ages[event];
			VisEffects[visnum].lifeleft -= effect_ages[event];
			VisEffects[visnum].size += ((ps_rand() % 20) / 20.0) * 1.0;

			VisEffects[visnum].size *= size_scalar;

			if ((ps_rand() % 2))
			{
				if (obj->movement_type == MT_PHYSICS)
				{
					VisEffects[visnum].movement_type = MT_PHYSICS;
					VisEffects[visnum].velocity = obj->mtype.phys_info.velocity;
					VisEffects[visnum].mass = obj->mtype.phys_info.mass;
					VisEffects[visnum].drag = obj->mtype.phys_info.drag;
					VisEffects[visnum].pos += VisEffects[visnum].velocity * effect_ages[event];
				}
			}
		}
	}
}
