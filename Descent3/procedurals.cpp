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
#include <math.h>
#include <memory.h>
#include <algorithm>

#include "procedurals.h"
#include "bitmap.h"
#include "gametexture.h"
#include "vclip.h"
#include "game.h"
#include "texture.h"
#include "mem.h"
#include "ddio.h"
#include "config.h"
#include "gameloop.h"
#include "../renderer/SceneProcedural.h"
#include "../renderer/SceneWaterImage.h"

#define BRIGHT_COLOR	254
#define PROC_SIZE	128
#define TABSIZE          256
#define TABMASK          (TABSIZE-1)
#define PERM(x)          perm[(x)&TABMASK]
#define INDEX(ix,iy)  PERM((ix)+PERM((iy)))
struct ProceduralEvaluation {
    int tick;
    d3scene::ProceduralRandom random;
    ubyte* destination = nullptr;
    ProceduralEvaluation(int image, int visual_tick) : tick(visual_tick), random(image, visual_tick) {}
};
#define LERP(t,x0,x1) ((x0)+(t)*((x1)-(x0)))
#define SMOOTHSTEP(x)  ((x))

unsigned char perm[TABSIZE];
static float Noise_table[TABSIZE * 3];
dynamic_proc_element DynamicProcElements[MAX_PROC_ELEMENTS];
ushort DefaultProcPalette[256];
int Num_proc_elements = 0;
static short Proc_free_list[MAX_PROC_ELEMENTS];
ushort ProcFadeTable[32768];
#define NUM_WATER_SHADES	64
ushort WaterProcTableHi[NUM_WATER_SHADES][256];
ubyte WaterProcTableLo[NUM_WATER_SHADES][256];
char* ProcNames[] = { "None","Line Lightning","Sphere lightning","Straight","Rising Embers","Random Embers","Spinners","Roamers","Fountain","Cone","Fall Right","Fall Left","END" };
char* WaterProcNames[] = { "None","Height blob","Sine Blob","Random Raindrops","Random Blobdrops","END" };

// Given an array of r,g,b values, generates a 16bit palette table for those colors
void GeneratePaletteForProcedural(ubyte* r, ubyte* g, ubyte* b, ushort* pal)
{
	float fr, fg, fb;
	for (int i = 0; i < 256; i++)
	{
		int ir = r[i];
		int ig = g[i];
		int ib = b[i];
		fr = (float)ir / 255.0;
		fg = (float)ig / 255.0;
		fb = (float)ib / 255.0;
		ir = (fr * 31.0);
		ig = (fg * 31.0);
		ib = (fb * 31.0);
		pal[i] = OPAQUE_FLAG | (ir << 10) | (ig << 5) | (ib);
	}
}

// Inits the noise table for procedurals
void InitNoise()
{
	d3scene::ProceduralRandom noise(1);
	float* table = Noise_table;
	float z, r, theta;

	for (int i = 0; i < TABSIZE; i++)
	{
		perm[i] = noise() % TABMASK;
		z = 1.0f - 2.0f * (noise() / 32767.0f);

		/* r is radius of x,y circle */
		r = sqrtf(1 - z * z);
		/* theta is angle in (x,y) */
		theta = 2.0f * 3.14f * (noise() / 32767.0f);
		*table++ = r * cosf(theta);
		*table++ = r * sinf(theta);
		*table++ = z;
	}
}

// Gets a lattice point for our noise
float GradLattice(int ix, int iy, float fx, float fy)
{
	float* g = &Noise_table[INDEX(ix, iy) * 2];
	return g[0] * fx + g[1] * fy;
}

float GradNoise(float x, float y)
{
	int ix, iy;
	float fx0, fx1, fy0, fy1;
	float wx, wy;
	float vx0, vx1, vy0, vy1;
	ix = (int)floor(x);
	fx0 = x - ix;
	fx1 = fx0 - 1;
	wx = SMOOTHSTEP(fx0);
	iy = (int)floor(y);
	fy0 = y - iy;
	fy1 = fy0 - 1;
	wy = SMOOTHSTEP(fy0);
	vx0 = GradLattice(ix, iy, fx0, fy0);
	vx1 = GradLattice(ix + 1, iy, fx1, fy0);
	vy0 = LERP(wx, vx0, vx1);
	vx0 = GradLattice(ix, iy + 1, fx0, fy1);
	vx1 = GradLattice(ix + 1, iy + 1, fx1, fy1);
	vy1 = LERP(wx, vx0, vx1);

	return LERP(wy, vy0, vy1);
}

extern ubyte EasterEgg;
int Easter_egg_handle = -1;
// Goes through our array and clears the slots out
void InitProcedurals()
{
	int t;
	int i;

	// Load easter egg bitmap
	Easter_egg_handle = bm_AllocLoadFileBitmap("FreakyEye.ogf", 0);
	if (Easter_egg_handle == -1)
	{
		mprintf((0, "Failed to load easter egg!\n"));
	}
	for (i = 0; i < MAX_PROC_ELEMENTS; i++)
	{
		DynamicProcElements[i].type = PROC_NONE;
		Proc_free_list[i] = i;
	}

	Num_proc_elements = 0;

	// Init our fade table
	for (i = 0; i < 32768; i++)
	{
		int r = (i >> 10) & 0x1f;
		int g = (i >> 5) & 0x1f;
		int b = (i) & 0x1f;
		r = __max(0, r - 1);
		g = __max(0, g - 1);
		b = __max(0, b - 1);
		ProcFadeTable[i] = OPAQUE_FLAG | (r << 10) | (g << 5) | b;
	}

	// Init our palette
	for (i = 0; i < 128; i++)
	{
		float fr = (float)i / 127.0;
		int ib = fr * 31.0;
		int ig = fr * 16.0;
		DefaultProcPalette[i] = OPAQUE_FLAG | (ig << 5) | (ib);
	}

	// Do lower part of table
	for (i = 0; i < (NUM_WATER_SHADES); i++)
	{
		float norm = ((float)i / (float)(NUM_WATER_SHADES - 1));
		float lo_norm = std::min((norm / .5) * 1.0, 1.0);
		float hi_norm = std::max(((norm - .5) / .5) * 1.0, 0.);

		for (int rcount = 0; rcount < 32; rcount++)
		{
			for (int gcount = 0; gcount < 4; gcount++)
			{
				int r = rcount;

				int index = (rcount * 4) + gcount;

				float fr = r;

				r = std::min(fr * lo_norm + (31.0 * hi_norm), 31.);

				WaterProcTableHi[i][index] = OPAQUE_FLAG | (r << 10);
			}
		}
		for (int bcount = 0; bcount < 32; bcount++)
		{
			for (int gcount = 0; gcount < 8; gcount++)
			{
				int b = bcount;
				int index = gcount * 32 + bcount;

				float fb = b;
				b = std::min(fb * lo_norm + (31.0 * hi_norm), 31.);

				WaterProcTableLo[i][index] = b;
			}
		}

		int gcount;
		for (gcount = 0; gcount < 8; gcount++)
		{
			float fg = gcount;
			int g = std::min((fg * lo_norm) + (7.0 * hi_norm), 7.);
			for (t = 0; t < 32; t++)
			{
				int index = gcount * 32 + t;
				WaterProcTableLo[i][gcount * 32 + t] |= (g << 5);
			}
		}
		for (gcount = 0; gcount < 4; gcount++)
		{
			float fg = gcount * 8;
			int g = std::min((fg * lo_norm) + (24.0 * hi_norm), 24.);

			for (t = 0; t < 32; t++)
			{
				int index = t * 4 + gcount;

				WaterProcTableHi[i][t * 4 + gcount] |= (g << 5);

			}
		}
	}

	/*for (i=0;i<NUM_WATER_SHADES;i++)
	{
		int r=(WaterProcTableHi[i][127]>>10) & 0x1f;
		int g=((WaterProcTableHi[i][127]>>5) & 0x1f);
		g+=((WaterProcTableLo[i][255]>>5) & 0x1f);
		int b=(WaterProcTableLo[i][255]) & 0x1f;
		mprintf ((0,"index=%d r=%d g=%d b=%d\n",i,r,g,b));
	}*/

	// Init our default palette
	for (i = 0; i < 128; i++)
	{
		float norm = (float)i / 127.0;
		int ir = (norm * 31.0);
		int ig = 16 + (norm * 15.0);
		DefaultProcPalette[i + 128] = OPAQUE_FLAG | (ir << 10) | (ig << 5) | (0x1f);
	}

	InitNoise();
}

// Returns the next free procelement
int ProcElementAllocate()
{
	if (Num_proc_elements == MAX_PROC_ELEMENTS)
	{
		//mprintf ((0,"Couldn't allocate proc element!\n"));
		return -1;
	}

	int n = Proc_free_list[Num_proc_elements++];
	ASSERT(DynamicProcElements[n].type == PROC_NONE);	// Get Jason

	DynamicProcElements[n].next = -1;
	DynamicProcElements[n].prev = -1;

	return n;
}

// Frees up a procelement for use
int ProcElementFree(int index)
{
	ASSERT(index >= 0 && index <= MAX_PROC_ELEMENTS);
	Proc_free_list[--Num_proc_elements] = index;
	DynamicProcElements[index].type = PROC_NONE;
	return 1;
}

#define SWAP(a,b) do { int _swap_var_=(a); (a)=(b); (b)=_swap_var_; } while (0)
// Draws a line into the passed in bitmap
void DrawProceduralLine(ProceduralEvaluation& context, int x1, int y1, int x2, int y2, ubyte color)
{
	int DY, DX;
	int i, x, y, yinc = 1, xinc = 1, error_term;

	ubyte* surface_ptr = context.destination;

	int xmask = PROC_SIZE - 1;
	int ymask = PROC_SIZE - 1;

	// Check to see if our x coords are reversed
	if (x1 > x2)
	{
		SWAP(x1, x2);
		SWAP(y1, y2);
	}

	DX = x2 - x1; DY = y2 - y1;
	if (DX < 0)
	{
		xinc = -1;
		DX = -DX;
	}
	if (DY < 0)
	{
		yinc = -1;
		DY = -DY;
	}

	if (DX >= DY)   // X is greater than y
	{
		error_term = 0;
		x = x1 & xmask; y = y1 & ymask;
		surface_ptr += y * PROC_SIZE;
		for (i = 0; i < DX; i++)
		{
			*(surface_ptr + x) = color;
			x += xinc;
			x &= xmask;
			error_term += DY;
			if (error_term >= DX)
			{
				y += yinc;
				y &= ymask;
				surface_ptr = context.destination + (y * PROC_SIZE);
				error_term -= DX;
			}
		}
	}
	else
	{
		error_term = 0;
		x = x1 & xmask; y = y1 & ymask;
		surface_ptr += y * PROC_SIZE;

		for (i = 0; i < DY; i++)
		{
			*(surface_ptr + x) = color;
			y += yinc;
			y &= ymask;
			error_term += DX;
			surface_ptr = context.destination + (y * PROC_SIZE);
			if (error_term >= DY)
			{
				x += xinc;
				x &= xmask;
				error_term -= DY;
			}
		}
	}
}

// Fades an entire bitmap one step closer to black
void FadeProcTexture(ProceduralEvaluation& context, int tex_handle)
{
    d3scene::fadeProcedural(context.destination,PROC_SIZE*PROC_SIZE,GameTextures[tex_handle].procedural->heat);
}

// Heats an entire bitmap 
void HeatProcTexture(ProceduralEvaluation& context, int tex_handle)
{
	int total = PROC_SIZE * PROC_SIZE;
	ubyte* src_data = context.destination;
	ubyte val;
	ubyte heat = GameTextures[tex_handle].procedural->heat;
	val = 255 - heat;

	for (int i = 0; i < total; i++, src_data++)
	{
		int pix = *src_data;
		if (pix < heat && pix >= val)
			(*src_data)++;
	}
}

// Fades and entire bitmap one step closer to black
void BlendProcTexture(int tex_handle)
{
    const auto* procedural = GameTextures[tex_handle].procedural;
    d3scene::blendProcedural(static_cast<const uint8_t*>(procedural->proc1),
        static_cast<uint8_t*>(procedural->proc2),PROC_SIZE,PROC_SIZE);
}

// Draws lightning into a bitmap
void AddProcLightning(ProceduralEvaluation& context, int x1, int y1, int x2, int y2, ubyte color, static_proc_element* proc)
{
	float dx = x2 - x1;
	float dy = y2 - y1;

	float mag = sqrt((dx * dx) + (dy * dy));
	if (mag < 1)
		return;
	int num_segments = mag / 8;
	dx /= mag;
	dy /= mag;
	float cur_x = x1;
	float cur_y = y1;
	float rot_x = dy;
	float rot_y = dx;
	float from_x = cur_x;
	float from_y = cur_y;

	for (int i = 0; i < num_segments; i++, cur_x += (dx * 8), cur_y += (dy * 8))
	{
		float to_x = cur_x + (dx * 8);
		float to_y = cur_y + (dy * 8);
		if (i != num_segments - 1)
		{
			to_x += (rot_x) * ((1 + proc->speed) * (((context.random() % 200) - 100.0) / 18.0));
			to_y += (rot_y) * ((1 + proc->speed) * (((context.random() % 200) - 100.0) / 18.0));
		}

		DrawProceduralLine(context, from_x, from_y, to_x, to_y, color);

		from_x = to_x;
		from_y = to_y;
	}
}

//link the procedural into the list for its texture
void ProcElementLink(int index, int texnum)
{
	dynamic_proc_element* proc = &DynamicProcElements[index];
	proc->next = GameTextures[texnum].procedural->dynamic_proc_elements;
	GameTextures[texnum].procedural->dynamic_proc_elements = index;
	ASSERT(proc->next != index);

	proc->prev = -1;
	if (proc->next != -1)
		DynamicProcElements[proc->next].prev = index;
}

// Unlinks a proc_element from a texture
void ProcElementUnlink(int index, int texnum)
{
	dynamic_proc_element* proc = &DynamicProcElements[index];

	ASSERT(index != -1);
	if (proc->prev == -1)
		GameTextures[texnum].procedural->dynamic_proc_elements = proc->next;
	else
		DynamicProcElements[proc->prev].next = proc->next;
	if (proc->next != -1)
		DynamicProcElements[proc->next].prev = proc->prev;
	ProcElementFree(index);
}

// Clears all the procedurals elements attached to this texture
void ClearAllProceduralsFromTexture(int texnum)
{
	int proc_num = GameTextures[texnum].procedural->dynamic_proc_elements;

	while (proc_num != -1)
	{
		int temp = DynamicProcElements[proc_num].next;
		ProcElementUnlink(proc_num, texnum);
		proc_num = temp;
	}
	GameTextures[texnum].procedural->num_static_elements = 0;
}

// Adds one point to a bitmap
void AddProcPoint(ProceduralEvaluation& context, int x, int y, ubyte color)
{
	x &= (PROC_SIZE - 1);
	y &= (PROC_SIZE - 1);

	int index = y * PROC_SIZE + x;
	context.destination[index] = color;
}

// Adds one point to a bitmap
inline void PlaceProcPoint(ProceduralEvaluation& context, int x, int y, ubyte color)
{
	x &= (PROC_SIZE - 1);
	y &= (PROC_SIZE - 1);
	context.destination[y * PROC_SIZE + x] = color;
}

// Adds a sphere lightning bolt
void AddProcSphereLightning(ProceduralEvaluation& context, int handle, static_proc_element* proc)
{
	int procnum = proc - GameTextures[handle].procedural->static_proc_elements;

	if (Detail_settings.Procedurals_enabled)
	{
		if (proc->frequency != 0 && (context.tick % proc->frequency) != 0)
			return;
	}
	float norm = ((float)proc->size / 255.0);
	int len = norm * (float)PROC_SIZE;
	len /= 2;
	int dir = context.random() * 2;
	int dest_x = proc->x1 + (FixCos(dir) * (float)len);
	int dest_y = proc->y1 + (FixSin(dir) * (float)len);
	AddProcLightning(context, proc->x1, proc->y1, dest_x, dest_y, BRIGHT_COLOR, proc);
}

void AddProcRisingEmber(ProceduralEvaluation& context, int handle, static_proc_element* proc)
{
	if (proc->frequency == 0 || (context.tick % proc->frequency) == 0)
	{
		// Add a new fizzle
		int num = context.random() % 7;
		for (int i = 0; i < num; i++)
		{
			int index = ProcElementAllocate();
			if (index > -1)
			{
				ProcElementLink(index, handle);
				DynamicProcElements[index].type = proc->type;

				DynamicProcElements[index].x1 = IntToFix((int)proc->x1);
				DynamicProcElements[index].y1 = IntToFix((int)proc->y1);
				DynamicProcElements[index].color = BRIGHT_COLOR;
				DynamicProcElements[index].speed = proc->speed;
				DynamicProcElements[index].frames_left = (context.random() % 10) + 15;
			}
		}
	}
}

void DoDynamicRisingEmber(ProceduralEvaluation& context, int handle, dynamic_proc_element* proc)
{
	PlaceProcPoint(context, FixToInt(proc->x1), FixToInt(proc->y1), proc->color);
	proc->frames_left--;
	if (proc->frames_left < 1)
		ProcElementUnlink(proc - DynamicProcElements, handle);
	else
	{
		proc->color--;
		if (proc->color == 0)
			ProcElementUnlink(proc - DynamicProcElements, handle);
		else
		{
			int speed_adjust = 1 + ((proc->speed / 255.0) * 2.0);
			proc->dx = FloatToFix(((context.random() % 3) - 1) * speed_adjust);
			proc->dy = FloatToFix(((context.random() % 3) - 2) * speed_adjust);
			proc->x1 += proc->dx;
			proc->y1 += proc->dy;
		}
	}
}

constexpr fix NarrowToFix(int value)
{
	return value * 65536;
}

void AddProcSpinners(ProceduralEvaluation& context, int handle, static_proc_element* proc)
{
	if (proc->frequency == 0 || (context.tick % proc->frequency) == 0)
	{

		int proc_num = (proc - GameTextures[handle].procedural->static_proc_elements) * 60;
		int speed_adjust = 1 + ((proc->speed / 255.0) * 5.0);
		int cur_angle = (((context.tick + proc_num) * speed_adjust) % 64) * 1024;

		int index = ProcElementAllocate();
		if (index > -1)
		{
			ProcElementLink(index, handle);
			DynamicProcElements[index].type = proc->type;

			DynamicProcElements[index].dx = FloatToFix(FixCos(cur_angle));
			DynamicProcElements[index].dy = FloatToFix(FixSin(cur_angle));
			DynamicProcElements[index].x1 = NarrowToFix(proc->x1 + (DynamicProcElements[index].dx * NarrowToFix(proc->size)));
			DynamicProcElements[index].y1 = NarrowToFix(proc->y1 + (DynamicProcElements[index].dy * NarrowToFix(proc->size)));
			DynamicProcElements[index].color = BRIGHT_COLOR;
			DynamicProcElements[index].speed = proc->speed;
			DynamicProcElements[index].frames_left = (context.random() % 10) + 15;
		}
	}
}

void DoDynamicSpinners(ProceduralEvaluation& context, int handle, dynamic_proc_element* proc)
{
	PlaceProcPoint(context, FixToInt(proc->x1), FixToInt(proc->y1), proc->color);
	proc->frames_left--;
	if (proc->frames_left < 1)
		ProcElementUnlink(proc - DynamicProcElements, handle);
	else
	{
		proc->color--;
		if (proc->color == 0)
			ProcElementUnlink(proc - DynamicProcElements, handle);
		else
		{
			proc->x1 += proc->dx;
			proc->y1 += proc->dy;
		}
	}
}

void AddProcRandomEmber(ProceduralEvaluation& context, int handle, static_proc_element* proc)
{
	if (proc->frequency == 0 || (context.tick % proc->frequency) == 0)
	{
		// Add a new fizzle
		int num = (context.random() % 4) + 1;
		for (int i = 0; i < num; i++)
		{
			int index = ProcElementAllocate();
			if (index > -1)
			{
				ProcElementLink(index, handle);
				DynamicProcElements[index].type = proc->type;

				DynamicProcElements[index].x1 = IntToFix(proc->x1);
				DynamicProcElements[index].y1 = IntToFix(proc->y1);
				DynamicProcElements[index].color = BRIGHT_COLOR;
				DynamicProcElements[index].speed = proc->speed;
				DynamicProcElements[index].frames_left = (context.random() % 10) + 15;
			}
		}
	}
}

void DoDynamicRandomEmber(ProceduralEvaluation& context, int handle, dynamic_proc_element* proc)
{
	PlaceProcPoint(context, FixToInt(proc->x1), FixToInt(proc->y1), proc->color);
	proc->frames_left--;
	if (proc->frames_left < 1)
		ProcElementUnlink(proc - DynamicProcElements, handle);
	else
	{
		proc->color--;
		if (proc->color == 0)
			ProcElementUnlink(proc - DynamicProcElements, handle);
		else
		{
			int speed_adjust = 1 + ((proc->speed / 255.0) * 2.0);
			proc->dx = FloatToFix(((context.random() % 3) - 1) * speed_adjust);
			proc->dy = FloatToFix(((context.random() % 3) - 1) * speed_adjust);
			proc->x1 += proc->dx;
			proc->y1 += proc->dy;
		}
	}
}

void AddProcRoamers(ProceduralEvaluation& context, int handle, static_proc_element* proc)
{
	proc->x1 += (context.random() % 5) - 2;
	proc->y1 += (context.random() % 5) - 2;
	if (proc->frequency == 0 || (context.tick % proc->frequency) == 0)
	{
		// Add a new fizzle
		int num = (context.random() % 4) + 1;
		for (int i = 0; i < num; i++)
		{
			int index = ProcElementAllocate();
			if (index > -1)
			{
				ProcElementLink(index, handle);
				DynamicProcElements[index].type = proc->type;

				DynamicProcElements[index].x1 = IntToFix(proc->x1);
				DynamicProcElements[index].y1 = IntToFix(proc->y1);
				DynamicProcElements[index].color = BRIGHT_COLOR;
				DynamicProcElements[index].speed = proc->speed;
				DynamicProcElements[index].frames_left = (context.random() % 10) + 15;
			}
		}
	}
}

void DoDynamicRoamers(ProceduralEvaluation& context, int handle, dynamic_proc_element* proc)
{
	PlaceProcPoint(context, FixToInt(proc->x1), FixToInt(proc->y1), proc->color);
	proc->frames_left--;
	if (proc->frames_left < 1)
		ProcElementUnlink(proc - DynamicProcElements, handle);
	else
	{
		proc->color--;
		if (proc->color == 0)
			ProcElementUnlink(proc - DynamicProcElements, handle);
		else
		{
			int speed_adjust = 1 + ((proc->speed / 255.0) * 2.0);
			proc->dx = FloatToFix(((context.random() % 3) - 1) * speed_adjust);
			proc->dy = FloatToFix(((context.random() % 3) - 1) * speed_adjust);
			proc->x1 += proc->dx;
			proc->y1 += proc->dy;
		}
	}
}

void AddProcFountain(ProceduralEvaluation& context, int handle, static_proc_element* proc)
{
	if (proc->frequency == 0 || (context.tick % proc->frequency) == 0)
	{
		// Add a new fizzle
		int num = (context.random() % 4) + 1;
		for (int i = 0; i < num; i++)
		{
			int index = ProcElementAllocate();
			if (index > -1)
			{
				ProcElementLink(index, handle);
				DynamicProcElements[index].type = proc->type;

				DynamicProcElements[index].x1 = IntToFix(proc->x1);
				DynamicProcElements[index].y1 = IntToFix(proc->y1);
				DynamicProcElements[index].color = BRIGHT_COLOR;
				DynamicProcElements[index].speed = proc->speed;
				DynamicProcElements[index].dx = FloatToFix(((context.random() % 100) - 50) / 200.0);
				if ((context.random() % 10) == 0)
				{
					DynamicProcElements[index].dy = FloatToFix(-((context.random() % 100) / 300.0));
					DynamicProcElements[index].frames_left = (context.random() % 6) + 3;
				}
				else
				{
					DynamicProcElements[index].dy = FloatToFix((context.random() % 100) / 50.0);
					DynamicProcElements[index].frames_left = (context.random() % 10) + 15;
				}
			}
		}
	}
}

void DoDynamicFountain(ProceduralEvaluation& context, int handle, dynamic_proc_element* proc)
{
	PlaceProcPoint(context, FixToInt(proc->x1), FixToInt(proc->y1), proc->color);
	proc->frames_left--;
	if (proc->frames_left < 1)
		ProcElementUnlink(proc - DynamicProcElements, handle);
	else
	{
		proc->color--;
		if (proc->color == 0)
			ProcElementUnlink(proc - DynamicProcElements, handle);
		else
		{
			proc->x1 += proc->dx;
			proc->y1 += proc->dy;
		}
	}
}

void AddProcCone(ProceduralEvaluation& context, int handle, static_proc_element* proc)
{
	if (proc->frequency == 0 || (context.tick % proc->frequency) == 0)
	{
		// Add a new fizzle
		int num = (context.random() % 4) + 1;
		for (int i = 0; i < num; i++)
		{
			int index = ProcElementAllocate();
			if (index > -1)
			{
				ProcElementLink(index, handle);
				DynamicProcElements[index].type = proc->type;

				DynamicProcElements[index].x1 = IntToFix(proc->x1);
				DynamicProcElements[index].y1 = IntToFix(proc->y1);

				DynamicProcElements[index].color = BRIGHT_COLOR;
				DynamicProcElements[index].speed = proc->speed;
				DynamicProcElements[index].dx = FloatToFix(((context.random() % 100) - 50) / 80.0);
				if ((context.random() % 10) == 0)
				{
					DynamicProcElements[index].dy = FloatToFix(-((context.random() % 100) / 300.0));
					DynamicProcElements[index].frames_left = (context.random() % 6) + 3;
				}
				else
				{
					DynamicProcElements[index].dy = IntToFix(1);
					DynamicProcElements[index].frames_left = (context.random() % 10) + 15;
				}
			}
		}
	}
}

void DoDynamicCone(ProceduralEvaluation& context, int handle, dynamic_proc_element* proc)
{
	PlaceProcPoint(context, FixToInt(proc->x1), FixToInt(proc->y1), proc->color);
	proc->frames_left--;
	if (proc->frames_left < 1)
		ProcElementUnlink(proc - DynamicProcElements, handle);
	else
	{
		proc->color--;
		if (proc->color == 0)
			ProcElementUnlink(proc - DynamicProcElements, handle);
		else
		{
			proc->x1 += proc->dx;
			proc->y1 += proc->dy;
		}
	}
}

void AddProcFallRight(ProceduralEvaluation& context, int handle, static_proc_element* proc)
{
	if (proc->frequency == 0 || (context.tick % proc->frequency) == 0)
	{
		// Add a new fizzle
		int num = (context.random() % 2) + 1;
		for (int i = 0; i < num; i++)
		{
			int index = ProcElementAllocate();
			if (index > -1)
			{
				ProcElementLink(index, handle);
				DynamicProcElements[index].type = proc->type;

				DynamicProcElements[index].x1 = IntToFix(proc->x1 + ((context.random() % 5) - 2));
				DynamicProcElements[index].y1 = IntToFix(proc->y1 + ((context.random() % 5) - 2));
				DynamicProcElements[index].color = BRIGHT_COLOR;
				DynamicProcElements[index].dx = IntToFix(1);
				DynamicProcElements[index].dy = FloatToFix((-(context.random() % 100)) / 300.0);
				DynamicProcElements[index].frames_left = (context.random() % 15) + 25;
			}
		}
	}
}

void DoDynamicFallRight(ProceduralEvaluation& context, int handle, dynamic_proc_element* proc)
{
	PlaceProcPoint(context, FixToInt(proc->x1), FixToInt(proc->y1), proc->color);
	proc->frames_left--;
	if (proc->frames_left < 1)
		ProcElementUnlink(proc - DynamicProcElements, handle);
	else
	{
		proc->color--;
		if (proc->color == 0)
			ProcElementUnlink(proc - DynamicProcElements, handle);
		else
		{
			if (FixToInt(proc->dx) > 0)
				proc->dx -= FloatToFix(((context.random() % 100) / 2000.0));
			if (FixToInt(proc->dy) < 2)
				proc->dy += FloatToFix(((context.random() % 100) / 1000.0));
			proc->x1 += proc->dx;
			proc->y1 += proc->dy;
		}
	}
}

void AddProcFallLeft(ProceduralEvaluation& context, int handle, static_proc_element* proc)
{
	if (proc->frequency == 0 || (context.tick % proc->frequency) == 0)
	{
		// Add a new fizzle
		int num = (context.random() % 2) + 1;
		for (int i = 0; i < num; i++)
		{
			int index = ProcElementAllocate();
			if (index > -1)
			{
				ProcElementLink(index, handle);
				DynamicProcElements[index].type = proc->type;

				DynamicProcElements[index].x1 = IntToFix(proc->x1 + ((context.random() % 5) - 2));
				DynamicProcElements[index].y1 = IntToFix(proc->y1 + ((context.random() % 5) - 2));
				DynamicProcElements[index].color = BRIGHT_COLOR;
				DynamicProcElements[index].dx = IntToFix(-1);
				DynamicProcElements[index].dy = FloatToFix((-(context.random() % 100)) / 300.0);
				DynamicProcElements[index].frames_left = (context.random() % 15) + 25;
			}
		}
	}
}

void DoDynamicFallLeft(ProceduralEvaluation& context, int handle, dynamic_proc_element* proc)
{
	PlaceProcPoint(context, FixToInt(proc->x1), FixToInt(proc->y1), proc->color);
	proc->frames_left--;
	if (proc->frames_left < 1)
		ProcElementUnlink(proc - DynamicProcElements, handle);
	else
	{
		proc->color--;
		if (proc->color == 0)
			ProcElementUnlink(proc - DynamicProcElements, handle);
		else
		{
			if (FixToInt(proc->dx) < 0)
				proc->dx += FloatToFix(((context.random() % 100) / 2000.0));
			if (FixToInt(proc->dy) < 2)
				proc->dy += FloatToFix(((context.random() % 100) / 1000.0));
			proc->x1 += proc->dx;
			proc->y1 += proc->dy;
		}
	}
}

void CalcWater2(int handle, int density)
{
	int newh;
	int count = PROC_SIZE + 1;
	proc_struct* procedural = GameTextures[handle].procedural;

	short* newptr = (short*)procedural->proc2;
	short* oldptr = (short*)procedural->proc1;

	int x, y;

	// Do main block
	for (y = 1; y < (PROC_SIZE - 1); y++, count += 2)
	{
		for (x = 1; x < PROC_SIZE - 1; x++, count++)
		{
			newh = ((oldptr[count + PROC_SIZE]
				+ oldptr[count - PROC_SIZE]
				+ oldptr[count + 1]
				+ oldptr[count - 1]
				+ oldptr[count - PROC_SIZE - 1]
				+ oldptr[count - PROC_SIZE + 1]
				+ oldptr[count + PROC_SIZE - 1]
				+ oldptr[count + PROC_SIZE + 1]
				) >> 2)
				- newptr[count];
			newptr[count] = newh - (newh >> density);
		}
	}
	int up, down, left, right;
	count = 0;
	for (y = 0; y < PROC_SIZE; y++)
	{
		if (y == 0)
			up = -((PROC_SIZE - 1) * PROC_SIZE);
		else
			up = PROC_SIZE;
		if (y == PROC_SIZE - 1)
			down = -((PROC_SIZE - 1) * PROC_SIZE);
		else
			down = PROC_SIZE;
		for (x = 0; x < PROC_SIZE; x++, count++)
		{
			if ((y != 0 && y != PROC_SIZE - 1) && x != 0 && x != PROC_SIZE - 1)
			{
				if (x == 1)
				{
					x = PROC_SIZE - 2;
					count += (PROC_SIZE - 3);
				}
				continue;
			}
			if (x == 0)
				left = -(PROC_SIZE - 1);
			else
				left = 1;

			if (x == PROC_SIZE - 1)
				right = -(PROC_SIZE - 1);
			else
				right = 1;
			newh = oldptr[count + down];
			newh += oldptr[count - up];
			newh += oldptr[count + right];
			newh += oldptr[count - left];
			newh += oldptr[count - up - left];
			newh += oldptr[count - up + right];
			newh += oldptr[count + down - left];
			newh += oldptr[count + down + right];
			newh >>= 2;
			newh -= newptr[count];
			newptr[count] = newh - (newh >> density);
		}
	}
}

void CalcWater(int handle, int density)
{
    const auto* procedural = GameTextures[handle].procedural;
    d3scene::stepProceduralWater(static_cast<const int16_t*>(procedural->proc1),
        static_cast<int16_t*>(procedural->proc2),PROC_SIZE,PROC_SIZE,unsigned(density));
}

void PublishWaterImage(int handle)
{
    const auto* procedural = GameTextures[handle].procedural;
    // These authored lookup tables are initialized once. A retained recipe owns
    // a frozen copy, never a pointer to mutable global appearance data.
    static const auto shading = [] {
        auto value = std::make_shared<d3scene::WaterShading>();
        for (size_t i = 0; i < 64; ++i) for (size_t c = 0; c < 256; ++c) {
            value->high[i][c] = WaterProcTableHi[i][c];
            value->low[i][c] = WaterProcTableLo[i][c];
        }
        return std::shared_ptr<const d3scene::WaterShading>(std::move(value));
    }();
    bm_SetDeferredPixels(procedural->procedural_bitmap,
        std::make_shared<d3scene::WaterImage>(PROC_SIZE,PROC_SIZE,
            static_cast<const int16_t*>(procedural->proc1),
            bm_SnapshotPixels(GameTextures[handle].bm_handle),int(procedural->light)-1,shading));
}

void AddProcHeightBlob(ProceduralEvaluation& context, static_proc_element* proc, int handle)
{
	int rquad;
	int cx, cy, cyq;
	int left, top, right, bottom;
	short* dest_data = (short*)GameTextures[handle].procedural->proc1;
	int x = proc->x1;
	int y = proc->y1;
	int procnum = proc - GameTextures[handle].procedural->static_proc_elements;
	if (proc->frequency && ((context.tick + procnum) % proc->frequency))
		return;
	int radius = proc->size;
	int height = proc->speed;
	rquad = radius * radius;

	left = -radius; right = radius;
	top = -radius; bottom = radius;
	// Perform edge clipping...
	if (x - radius < 1) left -= (x - radius - 1);
	if (y - radius < 1) top -= (y - radius - 1);
	if (x + radius > PROC_SIZE - 1) right -= (x + radius - PROC_SIZE + 1);
	if (y + radius > PROC_SIZE - 1) bottom -= (y + radius - PROC_SIZE + 1);
	for (cy = top; cy < bottom; cy++)
	{
		cyq = cy * cy;
		for (cx = left; cx < right; cx++)
		{
			if (cx * cx + cyq < rquad)
				dest_data[PROC_SIZE * (cy + y) + (cx + x)] += height;
		}
	}
}

void AddProcRaindrops(ProceduralEvaluation& context, static_proc_element* proc, int handle)
{
	int procnum = proc - GameTextures[handle].procedural->static_proc_elements;
	if (proc->frequency && ((context.tick + procnum) % proc->frequency))
		return;
	ubyte proc_frequency = proc->frequency;
	ubyte proc_size = proc->size;
	ubyte proc_speed = proc->speed;
	ubyte x1 = proc->x1;
	ubyte y1 = proc->y1;
	proc->frequency = 0;
	proc->size = (context.random() % 3) + 1;
	proc->speed = std::max(0, proc_speed + ((context.random() % 10) - 5));
	proc->x1 = x1 + (context.random() % (proc_size * 2)) - (proc_size);
	proc->y1 = y1 + (context.random() % (proc_size * 2)) - (proc_size);
	AddProcHeightBlob(context, proc, handle);
	proc->x1 = x1;
	proc->y1 = y1;
	proc->size = proc_size;
	proc->speed = proc_speed;
	proc->frequency = proc_frequency;
}

void AddProcBlobdrops(ProceduralEvaluation& context, static_proc_element* proc, int handle)
{
	int procnum = proc - GameTextures[handle].procedural->static_proc_elements;
	if (proc->frequency && ((context.tick + procnum) % proc->frequency))
		return;
	ubyte proc_frequency = proc->frequency;
	ubyte proc_size = proc->size;
	ubyte proc_speed = proc->speed;
	ubyte x1 = proc->x1;
	ubyte y1 = proc->y1;
	proc->frequency = 0;
	proc->size = (context.random() % 6) + 4;
	proc->speed = std::max(0, proc_speed + ((context.random() % 50) - 25));
	proc->x1 = x1 + (context.random() % (proc_size * 2)) - (proc_size);
	proc->y1 = y1 + (context.random() % (proc_size * 2)) - (proc_size);
	AddProcHeightBlob(context, proc, handle);
	proc->x1 = x1;
	proc->y1 = y1;
	proc->size = proc_size;
	proc->speed = proc_speed;
	proc->frequency = proc_frequency;
}

void AddProcSineBlob(ProceduralEvaluation& context, static_proc_element* proc, int handle)
{
	int procnum = proc - GameTextures[handle].procedural->static_proc_elements;
	if (proc->frequency && ((context.tick + procnum) % proc->frequency))
		return;
	int x = proc->x1;
	int y = proc->y1;
	int cx, cy;
	int left, top, right, bottom;
	int square, dist;
	int radius = proc->size;
	int radsquare = radius * radius;
	float length = (1024.0 / (float)radius) * (1024.0 / (float)radius);
	short* dest_data = (short*)GameTextures[handle].procedural->proc1;
	int height = proc->speed;
	radsquare = (radius * radius);
	left = -radius; right = radius;
	top = -radius; bottom = radius;
	// Perform edge clipping...
	if (x - radius < 1) left -= (x - radius - 1);
	if (y - radius < 1) top -= (y - radius - 1);
	if (x + radius > PROC_SIZE - 1) right -= (x + radius - PROC_SIZE + 1);
	if (y + radius > PROC_SIZE - 1) bottom -= (y + radius - PROC_SIZE + 1);
	for (cy = top; cy < bottom; cy++)
	{
		for (cx = left; cx < right; cx++)
		{
			square = cy * cy + cx * cx;
			if (square < radsquare)
			{
				dist = sqrt(square * length);
				int addval = (FixCos(dist % 65536) * (float)height);
				addval /= 8;
				dest_data[PROC_SIZE * (cy + y) + cx + x] += addval;
			}
		}
	}
}

void AllocateMemoryForWaterProcedural(int handle)
{
	proc_struct* procedural = GameTextures[handle].procedural;
	int w = PROC_SIZE;
	int h = PROC_SIZE;
	if (procedural->proc1)
		mem_free(procedural->proc1);
	if (procedural->proc2)
		mem_free(procedural->proc2);
	GameTextures[handle].procedural->proc1 = (ushort*)mem_malloc(w * h * 2);
	ASSERT(GameTextures[handle].procedural->proc1);
	memset(GameTextures[handle].procedural->proc1, 0, w * h * 2);

	GameTextures[handle].procedural->proc2 = (ushort*)mem_malloc(w * h * 2);
	ASSERT(GameTextures[handle].procedural->proc2);
	memset(GameTextures[handle].procedural->proc2, 0, w * h * 2);
	procedural->memory_type = PROC_MEMORY_TYPE_WATER;
}

// Does a water effect for this texture
void EvaluateWaterProcedural(ProceduralEvaluation& context, int handle)
{
    PERF_MARKER_SCOPE("Scene.Procedural.Water");
	proc_struct* procedural = GameTextures[handle].procedural;
	if (procedural->memory_type != PROC_MEMORY_TYPE_WATER)
		AllocateMemoryForWaterProcedural(handle);

	int dest_bitmap = procedural->procedural_bitmap;
	for (int i = 0; i < procedural->num_static_elements; i++)
	{
		static_proc_element* proc = &procedural->static_proc_elements[i];
		switch (proc->type)
		{
		case PROC_WATER_HEIGHT_BLOB:
			AddProcHeightBlob(context, proc, handle);
			break;
		case PROC_WATER_SINE_BLOB:
			AddProcSineBlob(context, proc, handle);
			break;
		case PROC_WATER_RAINDROPS:
			AddProcRaindrops(context, proc, handle);
			break;
		case PROC_WATER_BLOBDROPS:
			AddProcBlobdrops(context, proc, handle);
			break;
		default:
			break;
		}
	}

	if (EasterEgg)
	{
		if (Easter_egg_handle != -1)
		{
			ushort* src_data = bm_data(Easter_egg_handle, 0);
			short* dest_data = (short*)GameTextures[handle].procedural->proc1;
			int sw = bm_w(Easter_egg_handle, 0);
			int sh = bm_w(Easter_egg_handle, 0);
			int x1 = (PROC_SIZE / 2) - (sw / 2);
			int y1 = (PROC_SIZE / 2) - (sh / 2);
			// Make sure size is valid
			if (sw <= PROC_SIZE && sh <= PROC_SIZE)
			{
				int i, t;
				for (i = 0; i < sh; i++)
				{
					for (t = 0; t < sw; t++)
					{
						if (src_data[i * sw + t] & OPAQUE_FLAG)
							dest_data[((y1 + i) * PROC_SIZE) + t + x1] += 200;
					}
				}

			}
		}
		EasterEgg = 0;
	}

	// Calculate the water on the current texture
	PublishWaterImage(handle);
	int thickness = procedural->thickness;
	if (procedural->osc_time > 0)
	{
		int start = procedural->osc_value;
		int end = procedural->thickness;
		if (start > end)
		{
			int t = start;
			start = end;
			end = t;
		}
		int diff = end - start;
		if (diff > 0)
		{
			float gametime = (context.tick / 60.0f);
			float frametime = procedural->osc_time / (diff);
			float cur_frametime = gametime / frametime;
			int int_frame = cur_frametime;

			int_frame %= ((diff) * 2);
			if (int_frame >= diff)
				int_frame = (diff - 1) - (int_frame % diff);
			else
				int_frame %= diff;

			thickness = start + int_frame;
		}
	}

	CalcWater(handle, thickness);
	// Swap for next time
	short* temp = (short*)procedural->proc1;
	procedural->proc1 = procedural->proc2;
	procedural->proc2 = temp;
}

void AllocateMemoryForFireProcedural(int handle)
{
	proc_struct* procedural = GameTextures[handle].procedural;
	int w = PROC_SIZE;
	int h = PROC_SIZE;
	if (procedural->proc1)
		mem_free(procedural->proc1);
	if (procedural->proc2)
		mem_free(procedural->proc2);
	GameTextures[handle].procedural->proc1 = (ubyte*)mem_malloc(w * h * 1);
	ASSERT(GameTextures[handle].procedural->proc1);
	memset(GameTextures[handle].procedural->proc1, 0, w * h * 1);

	GameTextures[handle].procedural->proc2 = (ubyte*)mem_malloc(w * h * 1);
	ASSERT(GameTextures[handle].procedural->proc2);
	memset(GameTextures[handle].procedural->proc2, 0, w * h * 1);
	procedural->memory_type = PROC_MEMORY_TYPE_FIRE;
}

void EvaluateFireProcedural(ProceduralEvaluation& context, int handle)
{
    PERF_MARKER_SCOPE("Scene.Procedural.Fire");
	proc_struct* procedural = GameTextures[handle].procedural;

	int dest_bitmap = procedural->procedural_bitmap;
	if (procedural->memory_type != PROC_MEMORY_TYPE_FIRE)
		AllocateMemoryForFireProcedural(handle);
	
	context.destination = (ubyte*)procedural->proc1;
	// fade and the current texture
	FadeProcTexture(context, handle);
	//HeatProcTexture(context, handle);
	// Do the static procedurals first
	
	for (int i = 0; i < procedural->num_static_elements; i++)
	{
		static_proc_element* proc = &procedural->static_proc_elements[i];
		switch (proc->type)
		{
		case PROC_LINE_LIGHTNING:
			AddProcLightning(context, proc->x1, proc->y1, proc->x2, proc->y2, BRIGHT_COLOR, proc);
			break;
		case PROC_SPHERE_LIGHTNING:
			AddProcSphereLightning(context, handle, proc);
			break;
		case PROC_RISING_EMBER:
			AddProcRisingEmber(context, handle, proc);
			break;
		case PROC_RANDOM_EMBERS:
			AddProcRandomEmber(context, handle, proc);
			break;
		case PROC_SPINNERS:
			AddProcSpinners(context, handle, proc);
			break;
		case PROC_ROAMERS:
			AddProcRoamers(context, handle, proc);
			break;
		case PROC_FOUNTAIN:
			AddProcFountain(context, handle, proc);
			break;
		case PROC_CONE:
			AddProcCone(context, handle, proc);
			break;
		case PROC_FALL_RIGHT:
			AddProcFallRight(context, handle, proc);
			break;
		case PROC_FALL_LEFT:
			AddProcFallLeft(context, handle, proc);
			break;
		default:
			break;
		}

	}
	int proc_num = procedural->dynamic_proc_elements;
	while (proc_num != -1)
	{
		dynamic_proc_element* proc = &DynamicProcElements[proc_num];

		switch (proc->type)
		{
		case PROC_RISING_EMBER:
			DoDynamicRisingEmber(context, handle, proc);
			break;
		case PROC_RANDOM_EMBERS:
			DoDynamicRandomEmber(context, handle, proc);
			break;
		case PROC_SPINNERS:
			DoDynamicSpinners(context, handle, proc);
			break;
		case PROC_ROAMERS:
			DoDynamicRoamers(context, handle, proc);
			break;
		case PROC_FOUNTAIN:
			DoDynamicFountain(context, handle, proc);
			break;
		case PROC_CONE:
			DoDynamicCone(context, handle, proc);
			break;
		case PROC_FALL_RIGHT:
			DoDynamicFallRight(context, handle, proc);
			break;
		case PROC_FALL_LEFT:
			DoDynamicFallLeft(context, handle, proc);
			break;
		default:
			break;
		}
		proc_num = DynamicProcElements[proc_num].next;
	}

	// blend the current texture
	BlendProcTexture(handle);
    // Publish the compact simulation result. Pixel expansion is independent of
    // simulation and is shared by every consumer of this exact image value.
    bm_SetDeferredPixels(dest_bitmap,std::make_shared<IndexedPixels16>(
        static_cast<const uint8_t*>(procedural->proc2),PROC_SIZE*PROC_SIZE,procedural->palette));

	// Swap for next time
	ubyte* temp = (ubyte*)procedural->proc1;
	procedural->proc1 = procedural->proc2;
	procedural->proc2 = temp;
}

// Does a procedural for this texture
void EvaluateProcedural(int handle, int visual_tick)
{
	ProceduralEvaluation context(handle, visual_tick);
	proc_struct* procedural = GameTextures[handle].procedural;

	int dest_bitmap = procedural->procedural_bitmap;
	if (bm_w(dest_bitmap, 0) != PROC_SIZE)
	{
		mprintf((0, "Couldn't evaluate procedural because its not %d x %d!\n", PROC_SIZE, PROC_SIZE));
		return;
	}

	if (GameTextures[handle].flags & TF_WATER_PROCEDURAL)
		EvaluateWaterProcedural(context, handle);
	else
		EvaluateFireProcedural(context, handle);
}
