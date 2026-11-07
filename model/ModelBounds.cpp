// Descent 3MASTERED - GPL-3.0-or-later
// Source-model bounds used by simulation and asset loading.
#include "polymodel.h"
#include "pserror.h"
#include "findintersection.h"

float	ComputeDefaultSizeFunc(int handle, float *size_ptr, vector *offset_ptr, bool f_use_all_frames)
{
	poly_model *pm;
	matrix m;
	float normalized_time[MAX_SUBOBJECTS];
	int i, j, n;
	float cur_dist;
	float size = 0.0;
	int start_frame = 0;
	int end_frame = 0;

	vector geometric_center = Zero_vector;

	// Chris: Come see me when you are ready to deal with the paging problem - JL
	pm = GetPolymodelPointer(handle);

	ASSERT(start_frame <= end_frame);
	ASSERT(end_frame <= pm->frame_max);

	if(f_use_all_frames)
	{
		end_frame = pm->frame_max;
	}

	if(offset_ptr)
	{ 
		//[ISB] initialize these, because some models have a first submodel with 0 verts. 
		vector min_xyz = {};
		vector max_xyz = {};

		for(n = start_frame; n <= end_frame; n++)
		{
			// Because size changes with animation, we need the worst case point -- so, check every keyframe
			// NOTE:  This code does not currently account for all $turret and $rotate positions
			
			SetNormalizedTimeAnim(n, normalized_time, pm);

			SetModelAnglesAndPos (pm,normalized_time);

			for (i = 0;i < pm->n_models; i++)
			{
				bsp_info *sm=&pm->submodel[i];
					
				// For every vertex
				for(j = 0; j < sm->nverts; j++)
				{
					vector pnt;
					int mn;
				
					// Get the point and its current sub-object
					pnt    = sm->verts[j];
					mn     = i;

					// Instance up the tree
					while (mn != -1)
					{
						vector tpnt;

						vm_AnglesToMatrix(&m, pm->submodel[mn].angs.p,pm->submodel[mn].angs.h, pm->submodel[mn].angs.b);
						vm_TransposeMatrix(&m);

						tpnt    = pnt * m;

						pnt = tpnt + pm->submodel[mn].offset + pm->submodel[mn].mod_pos;
							
						mn = pm->submodel[mn].parent;
					}

	//				Maybe use for other code -- Accounts for world coordinates
	//				m = obj->orient;
	//				vm_TransposeMatrix(&m);
	//
	//				pnt = pnt * m;
	//
	//				*gun_point += obj->pos;

					// Find the min_xyz and max_xyz
					if(n == start_frame && i == 0 && j == 0)
					{
						min_xyz = max_xyz = pnt;
					}
					else
					{
						if(pnt.x < min_xyz.x) min_xyz.x = pnt.x;
						else if(pnt.x > max_xyz.x) max_xyz.x = pnt.x;

						if(pnt.y < min_xyz.y) min_xyz.y = pnt.y;
						else if(pnt.y > max_xyz.y) max_xyz.y = pnt.y;

						if(pnt.z < min_xyz.z) min_xyz.z = pnt.z;
						else if(pnt.z > max_xyz.z) max_xyz.z = pnt.z;
					}
				}
			}
		}

		geometric_center = (max_xyz + min_xyz)/2.0;
		*offset_ptr = geometric_center;
	}

	for(n = start_frame; n <= end_frame; n++)
	{
		// Because size changes with animation, we need the worst case point -- so, check every keyframe
		// NOTE:  This code does not currently account for all $turret and $rotate positions
		
		SetNormalizedTimeAnim(n, normalized_time, pm);

		SetModelAnglesAndPos (pm,normalized_time);

		for (i = 0;i < pm->n_models; i++)
		{
			bsp_info *sm=&pm->submodel[i];
				
			// For every vertex
			for(j = 0; j < sm->nverts; j++)
			{
				vector pnt;
				int mn;
			
				// Get the point and its current sub-object
				pnt    = sm->verts[j];
				mn     = i;

				// Instance up the tree
				while (mn != -1)
				{
					vector tpnt;

					vm_AnglesToMatrix(&m, pm->submodel[mn].angs.p,pm->submodel[mn].angs.h, pm->submodel[mn].angs.b);
					vm_TransposeMatrix(&m);

					tpnt    = pnt * m;

					pnt = tpnt + pm->submodel[mn].offset + pm->submodel[mn].mod_pos;
						
					mn = pm->submodel[mn].parent;
				}

//				Maybe use for other code -- Accounts for world coordinates
//				m = obj->orient;
//				vm_TransposeMatrix(&m);
//
//				pnt = pnt * m;
//
//				*gun_point += obj->pos;

				cur_dist = vm_VectorDistance(&geometric_center, &pnt);
				if(cur_dist > size) size = cur_dist;
			}
		}
	}

	// This is a arbitary value.  It allows for some turret and rotations to be caught
	size = size + 0.01f;
	
	if(size_ptr)	//DAJ
		*size_ptr = size;

	return size;
}

float	ComputeDefaultSize(int type, int handle, float *size_ptr)
{
	float size = ComputeDefaultSizeFunc(handle, size_ptr, NULL, true); 
	
	if(type != OBJ_WEAPON && type != OBJ_DEBRIS && type != OBJ_POWERUP)
	{
		ComputeDefaultSizeFunc(handle, &Poly_models[handle].wall_size, &Poly_models[handle].wall_size_offset, false);
		ComputeDefaultSizeFunc(handle, &Poly_models[handle].anim_size, &Poly_models[handle].anim_size_offset, true);

		if (type == OBJ_PLAYER)
		{
			Poly_models[handle].anim_size *= PLAYER_SIZE_SCALAR;
			Poly_models[handle].anim_size_offset = Zero_vector;
		}
	}
	else
	{
		if(type == OBJ_POWERUP)
		{
			size *= 2.0f;
			*size_ptr *= 2.0f;
		}

		Poly_models[handle].wall_size = size;
		Poly_models[handle].wall_size_offset = Zero_vector;

		Poly_models[handle].anim_size = size;
		Poly_models[handle].anim_size_offset = Zero_vector;
	}
	
	Poly_models[handle].flags |= PMF_SIZE_COMPUTED;
	
	return size;
}
