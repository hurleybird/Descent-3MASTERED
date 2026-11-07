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

#ifndef POLYMODEL_H
#define POLYMODEL_H

#include "manage.h"
#include "pstypes.h"
#include "vecmat.h"
#include "3d.h"
#include "robotfirestruct.h"
#include "polymodel_external.h"
#include "object_external_struct.h"

#define PM_COMPATIBLE_VERSION	1807
#define PM_OBJFILE_VERSION		2300

#define WB_INDEX_SHIFT	16			// bits to shift over to get the weapon battery index (after masking out flags)
#define SOF_WB_MASKS	0x01F0000	// Room for 32 weapon batteries (currently we only use 21 slots)

#define SOF_MONITOR_MASK	0x0ff0	// mask for monitors

extern int Num_poly_models;
extern poly_model Poly_models[];

extern vector Interp_pos_instance_vec;

inline float POLY_WIDTH(int model_num)
{
	return Poly_models[model_num].maxs.x - Poly_models[model_num].mins.x;
}

inline float POLY_HEIGHT(int model_num)
{
	return Poly_models[model_num].maxs.y - Poly_models[model_num].mins.y;
}

inline float POLY_DEPTH(int model_num)
{
	return Poly_models[model_num].maxs.z - Poly_models[model_num].mins.z;
}

// given a filename, reads in a POF and returns an index into the Poly_models array
// returns -1 if something is wrong
int LoadPolyModel (char *filename,int pageable);

// gets the filename from a path, plus appends our .pof extension
void ChangePolyModelName (char *src,char *dest);

// Searches thru all polymodels for a specific name, returns -1 if not found
// or index of polymodel with name
int FindPolyModelName (char *name);


// Inits our models array 
int InitModels ();

// Frees polymodel located in index of Poly_models array
void FreePolyModel (int index);

// Given an actual keyframe number, returns the normalized (0 to 1) position of that
// keyframe.  Handle is an index into the Poly_models array
float GetNormalizedKeyframe (int handle,float num);

// Goes through all poly models and gets all missing textures
void RemapPolyModels ();

// For macintosh, we must swap the interpreted model code
void SwapPolymodelData (ubyte *data);

// Sets a positional instance
void StartPolyModelPosInstance (vector *posvec);

// Pops a positional instance
void DonePolyModelPosInstance ();




void SetNormalizedTimeObj(object *obj, float *normalized_time);
void SetNormalizedTimeAnim(float norm_anim_frame, float *normalized_time, 	poly_model *pm);

void WBClearInfo(poly_model *pm);

// Computes the size of a polymodel.
float	ComputeDefaultSize(int type, int handle, float *size_ptr);

// Returns the total number of faces in a model
int CountFacesInPolymodel (poly_model *pm);

//	returns point within polymodel/submodel in world coordinates.
void GetPolyModelPointInWorld (vector *dest,poly_model *pm, vector *wpos, matrix *orient,int subnum, vector *pos,vector *norm=NULL);
void GetPolyModelPointInWorld (vector *dest,poly_model *pm, vector *wpos, matrix *orient,int subnum, float *normalized_time, vector *pos, vector *norm=NULL);

// Returns 1 if this submodel shouldn't be rendered
int IsNonRenderableSubmodel (poly_model *pm,int submodelnum);

// Pages in a polymodel if its not already in memory
void PageInPolymodel (int polynum, int type = -1, float *size_ptr = NULL);

// Gets a pointer to a polymodel.  Pages it in if neccessary
poly_model *GetPolymodelPointer (int polynum);

// Frees all the polymodel data, but doesn't free the actual polymodel itself
void FreePolymodelData (int i);

// Sets the position and rotation of a polymodel.  Used for rendering and collision detection
void SetModelAnglesAndPos (poly_model *po,float *normalized_time,uint subobj_flags=0xFFFFFFFF);

#endif
