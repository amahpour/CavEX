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

#ifndef MAP_PROJECTION_H
#define MAP_PROJECTION_H

#include <stdbool.h>

// Pure world<->screen projection for the clickable map screen (issue #35).
//
// The map renders a square grid of `cells` x `cells` world columns centred on
// the column (center_x, center_z), each column drawn as a `cell_px` pixel
// square. The grid's top-left corner sits at screen pixel (origin_x, origin_y).
//
// Screen convention (matches the in-world axes, north up):
//   - increasing screen X  -> increasing world X (east, to the right)
//   - increasing screen Y  -> increasing world Z (south, downward)
//
// Keeping the math here, free of any engine/GL state, makes it unit-testable in
// isolation (mirrors detect_double_tap for creative flight).
struct map_projection {
	int center_x, center_z; // world column the map is centred on
	int cells;              // grid is cells x cells columns (odd centres cleanly)
	int cell_px;            // on-screen size of one column, in pixels
	int origin_x, origin_y; // top-left pixel of the grid
};

// Build a projection that centres a `cells` x `cells` grid of `cell_px` squares
// in a `screen_w` x `screen_h` window around column (center_x, center_z).
struct map_projection map_projection_make(int center_x, int center_z, int cells,
										   int cell_px, int screen_w,
										   int screen_h);

// Top-left pixel of grid cell (i, j), 0 <= i,j < cells. i indexes world X, j
// indexes world Z. Defined for any i, j (no bounds check).
void map_cell_to_pixel(const struct map_projection* p, int i, int j, int* px,
					   int* py);

// World column for grid cell (i, j).
void map_cell_to_world(const struct map_projection* p, int i, int j,
					   int* world_x, int* world_z);

// Inverse of the screen mapping: which world column does pixel (px, py) fall in?
// Returns false (and leaves outputs untouched) when the pixel is outside the
// grid, so the caller can ignore clicks on the surrounding background.
bool map_pixel_to_world(const struct map_projection* p, int px, int py,
						int* world_x, int* world_z);

#endif
