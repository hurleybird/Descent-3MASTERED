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

#include "terrain.h"
#include "object.h"

// Given a position, returns the terrain segment that that position is in/over
// returns -1 if not over terrain
int GetTerrainCellFromPos(vector* pos)
{
	int x = pos->x / TERRAIN_SIZE;
	int z = pos->z / TERRAIN_SIZE;

	if (x < 0 || x >= TERRAIN_WIDTH || z < 0 || z >= TERRAIN_DEPTH)
		return -1;

	return (z * TERRAIN_WIDTH + x);
}

int GetTerrainRoomFromPos(vector* pos)
{
	return MAKE_ROOMNUM(GetTerrainCellFromPos(pos));
}

// Computes the center of the segment in x,z and also sets y touching the ground
void ComputeTerrainSegmentCenter(vector* pos, int segnum)
{
	int segx = segnum % TERRAIN_WIDTH;
	int segz = segnum / TERRAIN_WIDTH;

	pos->x = (segx * TERRAIN_SIZE) + (TERRAIN_SIZE / 2);
	pos->z = (segz * TERRAIN_SIZE) + (TERRAIN_SIZE / 2);

	pos->y = GetTerrainGroundPoint(pos);
}

// Given an position, returns the terrain Y coord at that location
float GetTerrainGroundPoint(vector* pos, vector* normal)
{
	float y;
	vector pnt, norm;
	int t;
	int x, z;

	x = pos->x / TERRAIN_SIZE;
	z = pos->z / TERRAIN_SIZE;

	if (x < 0 || x >= TERRAIN_WIDTH || z < 0 || z >= TERRAIN_DEPTH)
		return (0);

	t = z * TERRAIN_WIDTH + x;

	pnt = *pos;
	pnt.x -= (x * TERRAIN_SIZE);
	pnt.z -= (z * TERRAIN_SIZE);

	if (pnt.x > pnt.z)
		norm = TerrainNormals[MAX_TERRAIN_LOD - 1][t].normal2;
	else
		norm = TerrainNormals[MAX_TERRAIN_LOD - 1][t].normal1;

	// Do plane equation on x,z and then interpolate y
	y = ((pnt.x * norm.x) + (pnt.z * norm.z)) / norm.y;
	y = -y;
	y += Terrain_seg[t].y;

	if (normal != NULL)
		*normal = norm;

	return (y);
}
