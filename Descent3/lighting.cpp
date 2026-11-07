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

#include "3d.h"
#include "texture.h"
#include "gametexture.h"
#include "lighting.h"
#include "../renderer/SceneLighting.h"
#include "lightmap.h"
#include "descent.h"
#include "game.h"
#include "room.h"
#include <string.h>
#include <stdlib.h>
#include "findintersection.h"
#include "lightmap_info.h"
#include "polymodel.h"
#include "special_face.h"
#include "mem.h"
#include "config.h"
#include "dedicated_server.h"
#include "objinfo.h"
#include "Macros.h"
#include "renderer.h"
#include "terrain.h"
#include "BOA.h"

// Source-light data only. Dynamic illumination belongs to SceneLights.
#define MAX_DYNAMIC_FACES 2000
#define MAX_DESTROYED_LIGHTS_PER_FRAME 20
static ubyte Lmi_spoken_for[MAX_LIGHTMAP_INFOS / 8];
static float Ubyte_to_float[256];
static int Num_destroyed_lights_this_frame = 0;
static int Destroyed_light_rooms_this_frame[MAX_DESTROYED_LIGHTS_PER_FRAME];
static int Destroyed_light_faces_this_frame[MAX_DESTROYED_LIGHTS_PER_FRAME];

void InitLightingSources()
{
	memset(Lmi_spoken_for, 0, sizeof(Lmi_spoken_for));
	for (int i = 0; i < 256; ++i)
		Ubyte_to_float[i] = (float)i / 255.0;
}

#define DESTROYABLE_IGNORE_LIMIT .2f

void GetRoomDynamicScalar(vector* pos, room* rp, float* r, float* g, float* b,bool editor_preview)
{
    const d3scene::VolumeGridView grid{{rp->min_xyz.x,rp->min_xyz.y,rp->min_xyz.z},
        static_cast<uint32_t>(rp->volume_width),static_cast<uint32_t>(rp->volume_height),
        static_cast<uint32_t>(rp->volume_depth),static_cast<float>(VOLUME_SPACING),rp->volume_lights};
    auto color = d3scene::sampleVolume(grid,{pos->x,pos->y,pos->z});
    const float scalar = rp->volume_lights ? d3scene::roomLightScalar(Gametime,rp->pulse_time,rp->pulse_offset,
        static_cast<uint32_t>(rp-Rooms),(rp->flags & RF_STROBE) != 0,(rp->flags & RF_FLICKER) != 0,editor_preview) : 1;
    *r = color.x*scalar; *g = color.y*scalar; *b = color.z*scalar;
}


ubyte Float_to_ubyte(float fnum)
{
	int i;

	if (fnum < 0 || fnum>1.0)
	{
		mprintf((0, "ERROR: Trying to get value in Float_to_ubyte that is %f!\n", fnum));
		return 0;
	}

	for (i = 0; i < 256; i++)
	{
		if (fnum < Ubyte_to_float[i])
			return i;
	}

	return 255;
}


void BlendLightingEdges(lightmap_info* lmi_ptr)
{
	int lmi_handle = lmi_ptr - LightmapInfo;
	int h = lmi_h(lmi_handle);
	int w = lmi_w(lmi_handle);
	int lm_handle = lmi_ptr->lm_handle;
	ushort* dest_data = (ushort*)lm_data(lm_handle);
	ushort* src_data = dest_data;
	int dest_x = lmi_ptr->x1 - 1;
	int dest_y = lmi_ptr->y1 - 1;
	int i;

	ASSERT(dest_data);
	if (!dest_data)
		return;

	ASSERT(dest_x >= 0 && dest_y >= 0);
	ASSERT(dest_x + w < 127);
	ASSERT(dest_y + h < 127);

	for (i = 0; i < h; i++)
	{
		// Left edge
		dest_data[((dest_y + 1 + i) * 128) + dest_x] = src_data[((dest_y + 1 + i) * 128) + dest_x + 1];
		// Right edge
		dest_data[((dest_y + 1 + i) * 128) + dest_x + w + 1] = src_data[((dest_y + 1 + i) * 128) + dest_x + w];
	}


	for (i = 0; i < w; i++)
	{
		// Top edge
		dest_data[(dest_y * 128) + (dest_x + i + 1)] = src_data[((dest_y + 1) * 128) + (dest_x + i + 1)];
		// Bottom edge
		dest_data[((dest_y + 1 + h) * 128) + (dest_x + i + 1)] = src_data[((dest_y + h) * 128) + (dest_x + i + 1)];
	}


	// Now copy the corners
	// Upper left
	dest_data[(dest_y * 128) + (dest_x)] = src_data[((dest_y + 1) * 128) + (dest_x + 1)];
	// Upper right
	dest_data[(dest_y * 128) + (dest_x + w + 1)] = src_data[((dest_y + 1) * 128) + (dest_x + w)];
	// Lower left
	dest_data[((dest_y + h + 1) * 128) + (dest_x)] = src_data[((dest_y + h) * 128) + (dest_x + 1)];
	// Lower right
	dest_data[((dest_y + h + 1) * 128) + (dest_x + w + 1)] = src_data[((dest_y + h) * 128) + (dest_x + w)];

}


void SetRoomPulse(room* rp, ubyte pulse_time, ubyte pulse_offset)
{
	ASSERT(rp->used);

	rp->pulse_time = pulse_time;
	rp->pulse_offset = pulse_offset;

}


int GetVolumeSizeOfRoom(room* rp, int* w, int* h, int* d)
{
	int width = ((rp->max_xyz.x - rp->min_xyz.x) / VOLUME_SPACING) + 1;
	int height = ((rp->max_xyz.y - rp->min_xyz.y) / VOLUME_SPACING) + 1;
	int depth = ((rp->max_xyz.z - rp->min_xyz.z) / VOLUME_SPACING) + 1;

	if (w)
		*w = width;
	if (h)
		*h = height;
	if (d)
		*d = depth;


	return (width * height * depth);
}


void DestroyLight(int roomnum, int facenum)
{
	vector vecs[MAX_VERTS_PER_FACE], center;
	room* destroy_rp = &Rooms[roomnum];
	face* destroy_fp = &destroy_rp->faces[facenum];
	float r, g, b;
	fvi_face_room_list facelist[MAX_DYNAMIC_FACES];
	int num_faces, i;
	ushort lmilist[MAX_DYNAMIC_FACES];
	int num_spoken_for = 0;

	if (Dedicated_server)
		return;

	float mul = ((float)destroy_fp->light_multiple) / 4.0;

	r = GameTextures[destroy_fp->tmap].r * mul;
	g = GameTextures[destroy_fp->tmap].g * mul;
	b = GameTextures[destroy_fp->tmap].b * mul;

	// Get highest component
	float rmax = max(r, g);
	rmax = max(rmax, b);

	// Get the normalized color that this face emits
	float red_scale = r / rmax;
	float green_scale = g / rmax;
	float blue_scale = b / rmax;

	//red_scale/=30;
	//green_scale/=30;
	//blue_scale/=30;

	mprintf((0, "r=%f g=%f b=%f\n", red_scale, green_scale, blue_scale));

	// Get center and area of light face
	for (i = 0; i < destroy_fp->num_verts; i++)
		vecs[i] = destroy_rp->verts[destroy_fp->face_verts[i]];

	float area = vm_GetCentroid(&center, vecs, destroy_fp->num_verts);

	// Get the sphere of influence of this light
	float power = rmax * area;
	float temp_ignore = DESTROYABLE_IGNORE_LIMIT / power;
	float express = 1.0 / temp_ignore;
	express /= 3.14f;
	express -= area;
	float sphere_dist = sqrt(express);

	//	ApplyLightingToObjects (pos,roomnum,light_dist,red_scale,green_scale,blue_scale,light_direction,dot_range);

	num_faces = fvi_QuickDistFaceList(roomnum, &center, sphere_dist, facelist, MAX_DYNAMIC_FACES);

	for (i = 0; i < num_faces; i++)
	{
		room* rp = &Rooms[facelist[i].room_index];
		face* fp = &rp->faces[facelist[i].face_index];

		ASSERT(Rooms[facelist[i].room_index].used);
		ASSERT(facelist[i].face_index < Rooms[facelist[i].room_index].num_faces);

		// Make sure there already is a lightmap for this face
		if (!(fp->flags & FF_LIGHTMAP))
			continue;

		if (GameTextures[fp->tmap].flags & TF_LIGHT)
			continue;

		if (Lmi_spoken_for[fp->lmi_handle / 8] & (1 << (fp->lmi_handle % 8)))
			continue;

		int xres = lmi_w(fp->lmi_handle);
		int yres = lmi_h(fp->lmi_handle);

		lightmap_info* lmi_ptr = &LightmapInfo[fp->lmi_handle];

		int lmw = lm_w(lmi_ptr->lm_handle);


		vector subvec = center - lmi_ptr->upper_left;
		float dist_from_plane = vm_DotProduct(&subvec, &lmi_ptr->normal);

		// Check for backfaces
		if (dist_from_plane < 0)
			continue;

		dist_from_plane = fabs(dist_from_plane);

		if (dist_from_plane > sphere_dist)
			continue;

		// Compute face matrix
		matrix facematrix;
		vector fvec = -lmi_ptr->normal;
		vm_VectorToMatrix(&facematrix, &fvec, NULL, NULL);

		// Get upper left vector of face
		vector base_vector = lmi_ptr->upper_left;

		// Get the area of effect for this light
		float area_of_effect = (1.0 - (dist_from_plane / sphere_dist)) * sphere_dist;
		vector touch_vector = center - (lmi_ptr->normal * dist_from_plane);

		subvec = touch_vector - base_vector;
		float rdist = vm_DotProduct(&subvec, &facematrix.rvec);
		float udist = -vm_DotProduct(&subvec, &facematrix.uvec);
		int x_add = 1 + ((area_of_effect + (lmi_ptr->xspacing / 2)) / lmi_ptr->xspacing);
		int y_add = 1 + ((area_of_effect + (lmi_ptr->yspacing / 2)) / lmi_ptr->yspacing);

		// Get the rectangle of the lightmap that we want to light
		int start_x = (rdist / lmi_ptr->xspacing) - x_add;
		int start_y = (udist / lmi_ptr->yspacing) - y_add;
		int end_x = (rdist / lmi_ptr->xspacing) + x_add + .5;
		int end_y = (udist / lmi_ptr->yspacing) + y_add + .5;

		if (end_x < 0 || end_y < 0 || start_x >= (xres) || start_y >= (yres))
			continue;

		if (start_x < 0)
			start_x = 0;
		if (start_y < 0)
			start_y = 0;
		if (end_x > xres)
			end_x = xres;
		if (end_y > yres)
			end_y = yres;

		end_x++;
		end_y++;

		if (end_x > xres)
			end_x = xres;
		if (end_y > yres)
			end_y = yres;

		int width = end_x - start_x;
		int height = end_y - start_y;

		if (width == 0)
			width++;
		if (height == 0)
			height++;

		ASSERT(width > 0);
		ASSERT(height > 0);

		// Now copy our source data to our dest data so we have a base to work with
		ushort* dest_data;

		dest_data = (ushort*)lm_data(LightmapInfo[fp->lmi_handle].lm_handle);
		int lm_handle = LightmapInfo[fp->lmi_handle].lm_handle;

		if (!(GameLightmaps[lm_handle].flags & LF_LIMITS))
		{
			GameLightmaps[lm_handle].cx1 = start_x + lmi_ptr->x1;
			GameLightmaps[lm_handle].cx2 = start_x + width + lmi_ptr->x1;
			GameLightmaps[lm_handle].cy1 = start_y + lmi_ptr->y1;
			GameLightmaps[lm_handle].cy2 = start_y + height + lmi_ptr->y1;
		}
		else
		{
			if (start_x + lmi_ptr->x1 < GameLightmaps[lm_handle].cx1)
				GameLightmaps[lm_handle].cx1 = start_x + lmi_ptr->x1;
			if (start_x + lmi_ptr->x1 + width > GameLightmaps[lm_handle].cx2)
				GameLightmaps[lm_handle].cx2 = start_x + width + lmi_ptr->x1;

			if (start_y + lmi_ptr->y1 < GameLightmaps[lm_handle].cy1)
				GameLightmaps[lm_handle].cy1 = start_y + lmi_ptr->y1;
			if (lmi_ptr->y1 + start_y + height > GameLightmaps[lm_handle].cy2)
				GameLightmaps[lm_handle].cy2 = start_y + height + lmi_ptr->y1;
		}


		GameLightmaps[lm_handle].flags |= (LF_LIMITS | LF_CHANGED);
		lm_MarkChanged(lm_handle);

		lmilist[num_spoken_for] = fp->lmi_handle;
		Lmi_spoken_for[fp->lmi_handle / 8] |= (1 << (fp->lmi_handle % 8));
		num_spoken_for++;

		vector element_vec;

		base_vector -= (start_y * (facematrix.uvec * lmi_ptr->yspacing));
		base_vector += (start_x * (facematrix.rvec * lmi_ptr->xspacing));

		base_vector -= ((facematrix.uvec / 2) * lmi_ptr->yspacing);
		base_vector += ((facematrix.rvec / 2) * lmi_ptr->xspacing);

		// Go through and change each element of this lightmap
		// SLOW!

		int texel_num = ((start_y + lmi_ptr->y1) * lmw) + start_x + lmi_ptr->x1;
		for (int y = 0; y < height; y++, base_vector -= (facematrix.uvec * lmi_ptr->yspacing), texel_num += lmw)
		{
			element_vec = base_vector;

			for (int x = 0; x < width; x++, element_vec += (facematrix.rvec * lmi_ptr->xspacing))
			{
				int lightmap_texel_num = texel_num + x;

				ushort lightmap_texel = dest_data[lightmap_texel_num];

				if (!(lightmap_texel & OPAQUE_FLAG))
					continue;

				float dist = vm_VectorDistanceQuick(&element_vec, &center);
				float scalar = 1.0 - (dist / sphere_dist);

				if (scalar <= 0)
					continue;

				int r = (lightmap_texel >> 10) & 0x1f;
				int g = (lightmap_texel >> 5) & 0x1f;
				int b = lightmap_texel & 0x1f;

				if (r > 0)
					r = max(0, r - (scalar * red_scale * 31));

				if (g > 0)
					g = max(0, g - (scalar * green_scale * 31));

				if (b > 0)
					b = max(0, b - (scalar * blue_scale * 31));

				lightmap_texel = OPAQUE_FLAG | (r << 10) | (g << 5) | b;

				dest_data[lightmap_texel_num] = lightmap_texel;
			}
		}

		BlendLightingEdges(lmi_ptr);
	}

	for (i = 0; i < num_spoken_for; i++)
	{
		int index = lmilist[i];
		Lmi_spoken_for[index / 8] = 0;
	}
}


void AddToDestroyableLightList(int roomnum, int facenum)
{
	if (Num_destroyed_lights_this_frame >= MAX_DESTROYED_LIGHTS_PER_FRAME)
	{
		mprintf((0, "Ran out of destroyable light slots!\n"));
		return;
	}

	mprintf((0, "Destroying light.  Room=%d face=%d\n", roomnum, facenum));

	Destroyed_light_rooms_this_frame[Num_destroyed_lights_this_frame] = roomnum;
	Destroyed_light_faces_this_frame[Num_destroyed_lights_this_frame++] = facenum;

}


void DoDestroyedLightsForFrame()
{
	int i;
	for (i = 0; i < Num_destroyed_lights_this_frame; i++)
	{
		int roomnum = Destroyed_light_rooms_this_frame[i];
		int facenum = Destroyed_light_faces_this_frame[i];
		DestroyLight(roomnum, facenum);
	}

	Num_destroyed_lights_this_frame = 0;
}

