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

#ifdef NEWEDITOR //include first to get rid of ugly warning message about macro redfinitions
#include "..\neweditor\globals.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <algorithm>
#include "object.h"
#include "viseffect.h"
#include "render.h"
#include "renderobject.h"
#include "room.h"
#include "postrender.h"
#include "config.h"
#include "weapon.h"
#include "terrain.h"
#include "renderer.h"
#include "gameloop.h"

postrender_struct Postrender_list[MAX_POSTRENDERS];
int Num_postrenders = 0;

static vector Viewer_eye;
static matrix Viewer_orient;
static int Viewer_roomnum;




void SetupPostrenderRoom(room* rp)
{
	// Setup faces if this is a fogged room
	if (rp->flags & RF_FOG)
		SetupRoomFog(rp, &Viewer_eye, &Viewer_orient, Viewer_roomnum);
}


// Rotates a face, and then renders it
void DrawPostrenderFace(int roomnum, int facenum, bool change_z)
{
	PERF_MARKER_SCOPE("PostRenderFace");
	int i;

	// Always draw as non state limited
	bool save_state = StateLimited;
	StateLimited = false;

	ASSERT(roomnum >= 0 && roomnum < (MAX_ROOMS + MAX_PALETTE_ROOMS));
	ASSERT(Rooms[roomnum].used);

	room* rp = &Rooms[roomnum];

	ASSERT(facenum >= 0 && facenum < rp->num_faces);

	face* fp = &rp->faces[facenum];

	// Rotate points
	rp->wpb_index = 0;
	{
		PERF_MARKER_SCOPE("PostRenderFace.RotateProject");
		for (i = 0; i < fp->num_verts; i++)
		{
			g3_RotatePoint(&World_point_buffer[fp->face_verts[i]], &rp->verts[fp->face_verts[i]]);
			g3_ProjectPoint(&World_point_buffer[fp->face_verts[i]]);
		}
	}

	{
		PERF_MARKER_SCOPE("PostRenderFace.SetupRoom");
		SetupPostrenderRoom(rp);
	}
	const bool material_fog = BeginRoomMaterialFog(rp, &Viewer_eye,
		Viewer_roomnum, Room_light_val);
	const bool fog_portal_boundary = material_fog && fp->portal_num >= 0 &&
		(rp->portals[fp->portal_num].flags & PF_RENDER_FACES);
	if (fog_portal_boundary)
		EndRoomMaterialFog();
	const texture& face_texture = GameTextures[fp->tmap];
	const bool translucent_water = (face_texture.flags & TF_WATER) != 0 &&
		face_texture.alpha < 1.0f;

	// Render!
	if (change_z)
		rend_SetZBufferWriteMask(0);
	if (translucent_water)
		rend_SetAOSuppression(1.0f);
	{
		PERF_MARKER_SCOPE("PostRenderFace.RenderFace");
		RenderFace(rp, facenum);
	}
	if (translucent_water)
		rend_SetAOSuppression(0.0f);

	// Render any effects for this face
	if (Num_specular_faces_to_render > 0)
	{
		PERF_MARKER_SCOPE("PostRenderFace.Specular");
		RenderSpecularFacesFlat(rp);
		Num_specular_faces_to_render = 0;
		Num_real_specular_faces_to_render = 0;
	}

	if (Num_fog_faces_to_render > 0)
	{
		PERF_MARKER_SCOPE("PostRenderFace.Fog");
		if (fog_portal_boundary)
			BeginRoomMaterialFog(rp, &Viewer_eye, Viewer_roomnum, Room_light_val);
		RenderFogFaces(rp);
		if (fog_portal_boundary)
			EndRoomMaterialFog();
		Num_fog_faces_to_render = 0;
	}
	if (material_fog && !fog_portal_boundary)
		EndRoomMaterialFog();

	// Restore statelimited setting
	StateLimited = save_state;

	if (change_z)
		rend_SetZBufferWriteMask(1);
}

// Renders all the objects/viseffects/walls we have in our postrender list
