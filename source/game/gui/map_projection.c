/*
	Copyright (c) 2026 ByteBit/xtreme8000

	This file is part of CavEX.

	CavEX is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	CavEX is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with CavEX.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "map_projection.h"

struct map_projection map_projection_make(int center_x, int center_z, int cells,
										   int cell_px, int screen_w,
										   int screen_h) {
	struct map_projection p;
	p.center_x = center_x;
	p.center_z = center_z;
	p.cells = cells;
	p.cell_px = cell_px;

	int span = cells * cell_px;
	p.origin_x = (screen_w - span) / 2;
	p.origin_y = (screen_h - span) / 2;
	return p;
}

void map_cell_to_pixel(const struct map_projection* p, int i, int j, int* px,
					   int* py) {
	if(px)
		*px = p->origin_x + i * p->cell_px;
	if(py)
		*py = p->origin_y + j * p->cell_px;
}

void map_cell_to_world(const struct map_projection* p, int i, int j,
					   int* world_x, int* world_z) {
	int half = p->cells / 2;
	if(world_x)
		*world_x = p->center_x - half + i;
	if(world_z)
		*world_z = p->center_z - half + j;
}

bool map_pixel_to_world(const struct map_projection* p, int px, int py,
						int* world_x, int* world_z) {
	// Reject anything outside the grid first; this also keeps the division below
	// away from the negative-pixel case where C truncation rounds toward zero.
	int span = p->cells * p->cell_px;
	if(px < p->origin_x || py < p->origin_y || px >= p->origin_x + span
	   || py >= p->origin_y + span)
		return false;

	int i = (px - p->origin_x) / p->cell_px;
	int j = (py - p->origin_y) / p->cell_px;

	map_cell_to_world(p, i, j, world_x, world_z);
	return true;
}
