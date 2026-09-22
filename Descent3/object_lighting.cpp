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

#include "object_lighting.h"
#include "object.h"
#include "lighting.h"
#include "objinfo.h"
#include "weapon.h"
#include "descent.h"
#include "game.h"
#include "polymodel.h"
#include "renderobject.h"
#include <stdlib.h>
#include "lightmap_info.h"
#include "fireball.h"
#include "player.h"
#include "mem.h"
#include "config.h"
#include "findintersection.h"
#include "psrand.h"
#include "../model/retained_polymodel.h"

// returns true if this light point is visible
bool IsLightPointVisible(vector* pos, float size, object* obj)
{
	if (obj != NULL)
	{
		if ((obj->flags & OF_RENDERED) || IsPointVisible(pos, size, NULL))
			return true;
	}
	else
	{
		if (IsPointVisible(pos, size, NULL))
			return true;
	}

	return false;
}

// Fills in the color of the passed in players thrusters.  For lighting
void FindPlayerThrusterColor(int slot, float* r, float* g, float* b)
{
	ASSERT(slot >= 0 && slot < MAX_PLAYERS);
	object* obj = &Objects[Players[slot].objnum];
	int model_num = obj->rtype.pobj_info.model_num;

	*r = 0;
	*g = 0;
	*b = 0;

	if (obj->render_type != RT_POLYOBJ)
		return;

	poly_model* pm = &Poly_models[model_num];

	for (int i = 0; i < pm->n_models; i++)
	{
		if (pm->submodel[i].flags & SOF_GLOW)
		{
			*r = pm->submodel[i].glow_info->glow_r;
			*g = pm->submodel[i].glow_info->glow_g;
			*b = pm->submodel[i].glow_info->glow_b;

			return;
		}
	}

	Int3(); // No thruster glow?
}

ubyte EasterEgg = 0;
#define EASTER_EGG_TIMER (3600*4)

void ReflectRay(vector*, vector*, vector*);

// Frees all the memory associated with this objects lightmap
void ClearObjectLightmaps(object* obj)
{
	if (obj->lm_object.used == 1)
	{
		RetainedPolymodelInvalidateLightmapObject(&obj->lm_object);
		obj->lm_object.used = 0;
		mprintf((1, "CLEAR %d %s", obj->handle, obj->name));

		// The model assigned to an object can change while its level-authored
		// lightmap allocation still describes the previous model. Free from the
		// allocation's stored layout rather than indexing through the current
		// polymodel, which may have different submodel and face counts.
		float* uvblock = NULL;
		const int num_models = obj->lm_object.num_models < MAX_SUBOBJECTS ?
			obj->lm_object.num_models : MAX_SUBOBJECTS;
		for (int model_num = 0; model_num < num_models; model_num++)
		{
			if (obj->lm_object.num_faces[model_num] > 0 &&
				obj->lm_object.lightmap_faces[model_num])
			{
				uvblock = obj->lm_object.lightmap_faces[model_num][0].u2;
				break;
			}
		}
		mprintf((1, " %p\n", static_cast<void*>(uvblock)));
		if (uvblock)
			mem_free(uvblock);

		for (int model_num = 0; model_num < num_models; model_num++)
		{
			lightmap_object_face* faces = obj->lm_object.lightmap_faces[model_num];
			const int num_faces = obj->lm_object.num_faces[model_num] > 0 ?
				obj->lm_object.num_faces[model_num] : 0;
			for (int face_num = 0; faces && face_num < num_faces; face_num++)
			{
				lightmap_object_face* lof = &faces[face_num];
				if (lof->lmi_handle >= 0 &&
					lof->lmi_handle < MAX_LIGHTMAP_INFOS)
				{
					FreeLightmapInfo(lof->lmi_handle);
					lof->lmi_handle = BAD_LMI_INDEX;
				}
				lof->num_verts = 0;
			}

			if (faces)
				mem_free(faces);
			obj->lm_object.lightmap_faces[model_num] = NULL;
			obj->lm_object.num_faces[model_num] = 0;
		}
		for (int model_num = num_models; model_num < MAX_SUBOBJECTS; model_num++)
		{
			obj->lm_object.lightmap_faces[model_num] = NULL;
			obj->lm_object.num_faces[model_num] = 0;
		}
		obj->lm_object.num_models = 0;
	}
}

bool ObjectLightmapsMatchPolymodel(const object* obj)
{
	if (!obj || obj->lm_object.used == 0)
		return true;
	if (obj->render_type != RT_POLYOBJ)
		return false;

	const int model_num = obj->rtype.pobj_info.model_num;
	if (model_num < 0 || model_num >= MAX_POLY_MODELS)
		return false;
	poly_model* pm = &Poly_models[model_num];
	if (!pm->used || (pm->flags & PMF_NOT_RESIDENT) ||
		pm->n_models < 0 || pm->n_models > MAX_SUBOBJECTS ||
		obj->lm_object.num_models != pm->n_models)
	{
		return false;
	}

	for (int submodel_num = 0; submodel_num < pm->n_models; submodel_num++)
	{
		const bsp_info* sm = &pm->submodel[submodel_num];
		const int expected_faces =
			IsNonRenderableSubmodel(pm, submodel_num) ? 0 : sm->num_faces;
		if (obj->lm_object.num_faces[submodel_num] != expected_faces)
			return false;

		const lightmap_object_face* faces =
			obj->lm_object.lightmap_faces[submodel_num];
		if ((expected_faces > 0) != (faces != NULL))
			return false;
		for (int face_num = 0; face_num < expected_faces; face_num++)
		{
			if (faces[face_num].num_verts != sm->faces[face_num].nverts ||
				!faces[face_num].u2 || !faces[face_num].v2 ||
				faces[face_num].lmi_handle == BAD_LMI_INDEX ||
				faces[face_num].lmi_handle >= MAX_LIGHTMAP_INFOS)
			{
				return false;
			}
		}
	}
	return true;
}

// Frees all the memory associated with lightmap objects
void ClearAllObjectLightmaps(int terrain)
{
	int i;

	for (i = 0; i <= Highest_object_index; i++)
	{
		object* obj = &Objects[i];

		if ((OBJECT_OUTSIDE(obj) != 0) != (terrain != 0))
			continue;

		if (obj->lm_object.used == 1)
		{
			ClearObjectLightmaps(obj);

		}
	}
}


// Sets up the memory to be used by an object for lightmaps
void SetupObjectLightmapMemory(object* obj)
{
	int Mnum, Fnum;
	lightmap_object_face* lof;

	ASSERT(obj->lm_object.used == 0);
	RetainedPolymodelInvalidateLightmapObject(&obj->lm_object);
	obj->lm_object.used = 1;

	poly_model* pm = &Poly_models[obj->rtype.pobj_info.model_num];
	ASSERT(pm->new_style);

	obj->lm_object.num_models = pm->n_models;

	ASSERT(pm->n_models < MAX_SUBOBJECTS);
	int uv2size = 0;

	mprintf((1, "SETUP %d %s", obj->handle, obj->name));
	for (Mnum = 0; Mnum < pm->n_models; Mnum++)
	{
		if (IsNonRenderableSubmodel(pm, Mnum))
		{
			obj->lm_object.num_faces[Mnum] = 0;
			obj->lm_object.lightmap_faces[Mnum] = NULL;
			continue;
		}

		obj->lm_object.num_faces[Mnum] = pm->submodel[Mnum].num_faces;
		obj->lm_object.lightmap_faces[Mnum] = (lightmap_object_face*)mem_malloc(obj->lm_object.num_faces[Mnum] * sizeof(lightmap_object_face));
		ASSERT(obj->lm_object.lightmap_faces[Mnum]);

		for (Fnum = 0; Fnum < pm->submodel[Mnum].num_faces; Fnum++)
		{
			lof = &obj->lm_object.lightmap_faces[Mnum][Fnum];

			lof->num_verts = pm->submodel[Mnum].faces[Fnum].nverts;
			lof->lmi_handle = BAD_LMI_INDEX;

			uv2size += lof->num_verts;
		}
	}
	if (uv2size) {
		float* uvblock = (float*)mem_malloc(2 * uv2size * sizeof(float));
		mprintf((1, " %X %d\n", uvblock, uv2size));

		for (Mnum = 0; Mnum < pm->n_models; Mnum++)
		{
			if (!IsNonRenderableSubmodel(pm, Mnum)) {
				for (Fnum = 0; Fnum < pm->submodel[Mnum].num_faces; Fnum++)
				{
					obj->lm_object.lightmap_faces[Mnum][Fnum].u2 = uvblock;
					uvblock += pm->submodel[Mnum].faces[Fnum].nverts;
					obj->lm_object.lightmap_faces[Mnum][Fnum].v2 = uvblock;
					uvblock += pm->submodel[Mnum].faces[Fnum].nverts;
				}
			}
		}
	}
}


//SHOULDN'T THE FOLLOWING TWO FUNCTIONS REALLY BE IN OBJECT.CPP?
#include "hlsoundlib.h"
#include "soundload.h"	
#include "hud.h"
#include "stringtable.h"
//	makes the an object cloaked
void MakeObjectInvisible(object* obj, float time, float fade_time, bool no_hud_message)
{
	if (obj->effect_info)
	{
		obj->effect_info->cloak_time = time;
		obj->effect_info->type_flags |= EF_FADING_OUT;
		obj->effect_info->fade_time = fade_time;
		obj->effect_info->fade_max_time = fade_time;
	}

	if (obj->type == OBJ_PLAYER && !no_hud_message)
	{
		static int cloak_sound_on_id = -2;
		if (cloak_sound_on_id == -2)
			cloak_sound_on_id = FindSoundName("Cloak on");

		if (cloak_sound_on_id != -1)
			Sound_system.Play3dSound(cloak_sound_on_id, SND_PRIORITY_HIGHEST, obj, MAX_GAME_VOLUME / 2);

		if (obj->id == Player_num) {
			AddHUDMessage(TXT_CLOAKON);
		}

		if (obj->effect_info)
			obj->effect_info->type_flags |= EF_CLOAK_WITH_MSG;
	}
}

//	makes an object visbile
void MakeObjectVisible(object* obj)
{
	if (obj->effect_info)
	{
		//	Play sound maybe.
		obj->effect_info->type_flags &= ~EF_CLOAKED;
		obj->effect_info->type_flags |= EF_FADING_IN;
		obj->effect_info->fade_time = obj->effect_info->fade_max_time;
		obj->effect_info->cloak_time = 0;
	}

	if (obj->type == OBJ_PLAYER && obj->effect_info && obj->effect_info->type_flags & EF_CLOAK_WITH_MSG)
	{
		static int cloak_sound_off_id = -2;
		if (cloak_sound_off_id == -2)
			cloak_sound_off_id = FindSoundName("Cloak off");

		if (cloak_sound_off_id != -1)
			Sound_system.Play3dSound(cloak_sound_off_id, SND_PRIORITY_HIGHEST, obj, MAX_GAME_VOLUME / 2);

		if (obj->id == Player_num) {
			AddHUDMessage(TXT_MSG_CLOAKOFF);
		}
	}
}

//Sets an object to have local lighting info
void ObjSetLocalLighting(object* objp)
{
	//If this object doesn't have its own lighting info, create it
	if (objp->lighting_info == NULL) {

		//make sure this is a generic object
		ASSERT((objp->type == OBJ_POWERUP) || (objp->type == OBJ_ROBOT) || (objp->type == OBJ_CLUTTER) || (objp->type == OBJ_BUILDING));

		//allocate a light info for this object
		objp->lighting_info = (light_info*)mem_malloc(sizeof(*objp->lighting_info));

		//copy the lighting info from the type for this object
		*objp->lighting_info = Object_info[objp->id].lighting_info;
	}
}

//Returns a pointer to the lighting info for the specified object
light_info* ObjGetLightInfo(object* objp)
{
	if (objp->lighting_info)
		return objp->lighting_info;

	if ((objp->type == OBJ_POWERUP) || (objp->type == OBJ_ROBOT) || (objp->type == OBJ_CLUTTER) || (objp->type == OBJ_BUILDING))
		return &Object_info[objp->id].lighting_info;

	if (objp->type == OBJ_WEAPON)
		return &Weapons[objp->id].lighting_info;

	Int3();

	return NULL;
}

