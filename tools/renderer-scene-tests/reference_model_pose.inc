// Frozen pre-migration animation oracle from polymodel.cpp.
// Copyright (C) 2024 Parallax Software; GPL-3.0-or-later.
// Test-only: retains original arithmetic and known edge defects.
static float ReferenceClock;
//Given a model pointer and an array of floats that go from 0..1, calculate the angles of each
// corresponding subobject
void ReferenceAngles (poly_model *po,float *normalized_angles)
{
	int i;

	ASSERT (!(po->flags & PMF_NOT_RESIDENT));

	if (po->num_key_angles>0 && normalized_angles)
	{
		// get time per keyframe state
		float state_time=1.0/(po->num_key_angles-1);
		float cur_state_time;
		int cur_state;
		float normal_state_time;
		
		for (i=0;i<po->n_models;i++)
		{
			// Don't rotate turrets or auto-rotators or weapon battery submodels
			if (!(po->submodel[i].flags & (SOF_ROTATE | SOF_TURRET )))
			{

				// Find out which keyframe we're at
		
				cur_state=normalized_angles[i]/state_time;
				matrix dest_matrix;
												
				// Find out how far into that keyframe we are
				cur_state_time=normalized_angles[i]-((float)cur_state*state_time);
				normal_state_time=cur_state_time/state_time;

				// Now do a parametric adjustment on the angles

				int cur_angle=0;

				// If we're already at the high point of the interpolation then just
				// stuff some values
				if (cur_state==po->num_key_angles-1)
				{
					vm_ExtractAnglesFromMatrix (&po->submodel[i].angs,&po->submodel[i].keyframe_matrix[po->num_key_angles-1]);
					continue;

				}

				dest_matrix=po->submodel[i].keyframe_matrix[cur_state]+
					((po->submodel[i].keyframe_matrix[cur_state+1]-po->submodel[i].keyframe_matrix[cur_state])*normal_state_time);

				vm_ExtractAnglesFromMatrix (&po->submodel[i].angs,&dest_matrix);
				//po->submodel[i].mod_matrix=dest_matrix;

			}
			else if (po->submodel[i].flags & SOF_ROTATE)
			{
				float flrot=ReferenceClock*po->submodel[i].rps;
				int introt=flrot;

				float fdiff=flrot-introt;
			
				ASSERT (fdiff>=0 && fdiff<=1);
	
				// fdiff now equals 0 to 1
				matrix temp_matrix;
				vector temp_vec;
				if (po->new_style)
					temp_vec=po->submodel[i].keyframe_axis[1];
				else
					temp_vec=po->submodel[i].norm;

				BuildModelAngleMatrix (&temp_matrix,fdiff*65535,&temp_vec);
				vm_ExtractAnglesFromMatrix (&po->submodel[i].angs,&temp_matrix);
			}
			else if (po->submodel[i].flags & SOF_TURRET)
			{
				matrix temp_matrix;
				if (po->new_style)
					BuildModelAngleMatrix (&temp_matrix,normalized_angles[i]*65535,&po->submodel[i].keyframe_axis[1]);
				else
					BuildModelAngleMatrix (&temp_matrix,normalized_angles[i]*65535,&po->submodel[i].norm);
					
				vm_ExtractAnglesFromMatrix (&po->submodel[i].angs,&temp_matrix);
			}
		}
	}
	else
	{
		for (i=0;i<po->n_models;i++)
		{
			if (po->submodel[i].flags & SOF_ROTATE)
			{
				float flrot=ReferenceClock*po->submodel[i].rps;
				int introt=flrot;
	
				float fdiff=flrot-introt;
				
				ASSERT (fdiff>=0 && fdiff<=1);

				// fdiff now equals 0 to 1
				matrix temp_matrix;
				vector temp_vec;
				if (po->new_style)
					temp_vec=po->submodel[i].keyframe_axis[1];
				else
					temp_vec=po->submodel[i].norm;

				BuildModelAngleMatrix (&temp_matrix,fdiff*65535,&temp_vec);
				vm_ExtractAnglesFromMatrix (&po->submodel[i].angs,&temp_matrix);
			}
			else if (po->submodel[i].flags & SOF_TURRET)
			{
				// We need to find where in this rotation is relative to gametime
				float flrot = ReferenceClock;
				int introt = flrot;
	
				float fdiff=flrot-introt;
					
				// fdiff now equals 0 to 1
				matrix temp_matrix;
				if (po->new_style)
					BuildModelAngleMatrix (&temp_matrix,fdiff*65535,&po->submodel[i].keyframe_axis[0]);
				else
					BuildModelAngleMatrix (&temp_matrix,fdiff*65535,&po->submodel[i].norm);
					
				vm_ExtractAnglesFromMatrix (&po->submodel[i].angs,&temp_matrix);
			}
			else
			{
				po->submodel[i].angs.h=0;
				po->submodel[i].angs.p=0;
				po->submodel[i].angs.h=0;
			}
		}
	}

}

//Given a model pointer and an array of floats that go from 0..1, calculate the interpolated 
//position of each corresponding subobject
void ReferencePositions (poly_model *po,float *normalized_pos)
{
	int i;

	ASSERT (!(po->flags & PMF_NOT_RESIDENT));
	
	if (normalized_pos && po->num_key_pos>0)
	{
		for (i=0;i<po->n_models;i++)
		{
			bsp_info *sm=&po->submodel[i];

			// get time per keyframe state
			float state_time=1.0/(po->num_key_pos-1);
			float cur_state_time;
			int cur_state;
			float normal_state_time;
		
			// Find out which keyframe we're at
			cur_state=normalized_pos[i]/state_time;
			
			// Find out how far into that keyframe we are
			cur_state_time=normalized_pos[i]-((float)cur_state*state_time);
			normal_state_time=cur_state_time/state_time;

			// Now do a parametric adjustment on the positions

			vector total_delta_pos={0,0,0};
			vector subpos;
			vector final_pos;

			// If we're already at the high point of the interpolation then just
			// stuff some values
			if (cur_state==sm->num_key_pos-1)
			{
				po->submodel[i].mod_pos=po->submodel[i].keyframe_pos[po->num_key_pos-1];
				continue;
			}
			
			if (cur_state==0)
				po->submodel[i].mod_pos=normal_state_time*po->submodel[i].keyframe_pos[1];
			else
			{
				vm_SubVectors (&subpos,&po->submodel[i].keyframe_pos[cur_state+1],&po->submodel[i].keyframe_pos[cur_state]);
				final_pos=po->submodel[i].keyframe_pos[cur_state]+(subpos*normal_state_time);
				po->submodel[i].mod_pos=final_pos;
			}
			
		}
	}
	else
	{
		for (i=0;i<po->n_models;i++)
		{
			po->submodel[i].mod_pos.x=0;
			po->submodel[i].mod_pos.y=0;
			po->submodel[i].mod_pos.z=0;
		}
	}
}

//Given a model pointer and an array of floats that go from 0..1, calculate the interpolated 
//position/angle of each corresponding subobject
void ReferenceTimed (poly_model *po,float *normalized_time,uint subobj_flags)
{
	int i;

	ASSERT (!(po->flags & PMF_NOT_RESIDENT));

	if (normalized_time)
	{
		// get time per keyframe state
		
		for (i=0;i<po->n_models;i++)
		{
			bsp_info *sm=&po->submodel[i];
			int int_tick,current_frame,total_ticks;
			float normal_state_time,current_tick;

			if (!(subobj_flags & (1<<i)))
				continue;
			
			if (sm->num_key_pos<=1)
			{
				vm_MakeZero(&sm->mod_pos);
				goto do_angles;
			}

			if (normalized_time[i]==1.0)
			{
				sm->mod_pos=sm->keyframe_pos[sm->num_key_pos-1];
				goto do_angles;
			}

			// Find out which keyframe we're at
			total_ticks=sm->pos_track_max-sm->pos_track_min;
			current_tick=(normalized_time[i]*total_ticks)+sm->pos_track_min;
			int_tick=current_tick;
			current_frame=sm->tick_pos_remap[int_tick-sm->pos_track_min];

			ASSERT (current_frame>=0 && current_frame<=sm->num_key_pos-1);

			if (current_frame==sm->num_key_pos-1)
			{
				sm->mod_pos=sm->keyframe_pos[sm->num_key_pos-1];
				goto do_angles;
			}
			else
			{
				int ticks_between_frames=sm->pos_start_time[current_frame+1]-sm->pos_start_time[current_frame];
				float this_tick=current_tick-sm->pos_start_time[current_frame];
				normal_state_time=(float)this_tick/(float)ticks_between_frames;

				vector subpos;
				vm_SubVectors (&subpos,&sm->keyframe_pos[current_frame+1],&sm->keyframe_pos[current_frame]);
				sm->mod_pos=sm->keyframe_pos[current_frame]+(subpos*normal_state_time);
			}


			// Now do angle stuff here
			do_angles:
			// Don't rotate turrets or auto-rotators
			if (!(sm->flags & (SOF_TURRET | SOF_ROTATE)))
			{
				if (sm->num_key_angles<=1)
				{
					sm->angs.p=0;
					sm->angs.h=0;
					sm->angs.b=0;
					continue;
				}

				if (normalized_time[i]==1.0)
				{
					vm_ExtractAnglesFromMatrix (&po->submodel[i].angs,&po->submodel[i].keyframe_matrix[sm->num_key_angles-1]);
					continue;
				}

				// Find out which keyframe we're at	
				total_ticks=sm->rot_track_max-sm->rot_track_min;
				current_tick=(normalized_time[i]*total_ticks)+sm->rot_track_min;
				int_tick=current_tick;
				current_frame=sm->tick_ang_remap[int_tick-sm->rot_track_min];

				ASSERT (current_frame>=0 && current_frame<=sm->num_key_angles-1);
										
				if (current_frame==sm->num_key_angles-1)
				{
					vm_ExtractAnglesFromMatrix (&po->submodel[i].angs,&po->submodel[i].keyframe_matrix[sm->num_key_angles-1]);
					continue;
				}
				else
				{
					int ticks_between_frames=sm->rot_start_time[current_frame+1]-sm->rot_start_time[current_frame];
					float this_tick=current_tick-sm->rot_start_time[current_frame];
					normal_state_time=(float)this_tick/(float)ticks_between_frames;

					// Now do a parametric adjustment on the angles

					matrix dest_matrix=sm->keyframe_matrix[current_frame]+
					((sm->keyframe_matrix[current_frame+1]-sm->keyframe_matrix[current_frame])*normal_state_time);

					vm_ExtractAnglesFromMatrix (&sm->angs,&dest_matrix);
				}

				
			}
			else
			{
				 // Adjust special subobjects
				if (po->submodel[i].flags & SOF_ROTATE)
				{
					float flrot=ReferenceClock*po->submodel[i].rps;
					int introt=flrot;
	
					float fdiff=flrot-introt;
				
					ASSERT (fdiff>=0 && fdiff<=1);
		
					// fdiff now equals 0 to 1
					matrix temp_matrix;
					vector temp_vec;
					if (po->new_style)
						temp_vec=po->submodel[i].keyframe_axis[1];
					else
						temp_vec=po->submodel[i].norm;

					BuildModelAngleMatrix (&temp_matrix,fdiff*65535,&temp_vec);
					vm_ExtractAnglesFromMatrix (&po->submodel[i].angs,&temp_matrix);
				}
				else if (po->submodel[i].flags & SOF_TURRET)
				{
					matrix temp_matrix;
					if (po->new_style)
						BuildModelAngleMatrix (&temp_matrix,normalized_time[i]*65535,&po->submodel[i].keyframe_axis[1]);
					else
						BuildModelAngleMatrix (&temp_matrix,normalized_time[i]*65535,&po->submodel[i].norm);
						
					vm_ExtractAnglesFromMatrix (&po->submodel[i].angs,&temp_matrix);
				}
			}

		}

	}
	else
	{
		for (i=0;i<po->n_models;i++)
		{
			po->submodel[i].mod_pos.x=0;
			po->submodel[i].mod_pos.y=0;
			po->submodel[i].mod_pos.z=0;

			po->submodel[i].angs.p=0;
			po->submodel[i].angs.h=0;
			po->submodel[i].angs.b=0;
		}
	}
}

// Sets the position and rotation of a polymodel.  Used for rendering and collision detection
void ReferencePose (poly_model *po,float *normalized_time,uint subobj_flags)
{
	ASSERT (!(po->flags & PMF_NOT_RESIDENT));


	if (po->flags & PMF_TIMED)
	{
		ReferenceTimed(po,normalized_time,subobj_flags);
		return;
	}
	else
	{
		ReferenceAngles(po,normalized_time);
		ReferencePositions (po,normalized_time);

	}
}
