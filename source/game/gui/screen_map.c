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

#include <math.h>

#include "../../block/blocks_data.h"
#include "../../graphics/gui_util.h"
#include "../../network/server_interface.h"
#include "../../platform/gfx.h"
#include "../../platform/input.h"
#include "../game_state.h"
#include "map_projection.h"
#include "screen.h"

// Player.Pos.y stores the EYE position; the engine derives feet as
// Pos.y - EYE_HEIGHT (see source/entity/entity_local_player.c). Must match that
// constant so a tapped teleport lands feet-on-surface, never embedded.
#define EYE_HEIGHT 1.62F

// Map covers an odd-sized square grid of columns so the player sits in the dead
// centre cell. 49 columns (+-24 blocks) keeps the whole thing cheap (one surface
// sample per column), within the Wii MEM1 budget, and roughly matches the area
// kept loaded at view distance 3 so the grid fills with real terrain rather than
// unloaded "void" columns.
#define MAP_CELLS 49

// Column the map is centred on, snapshotted from the player when the screen
// opens (so panning the camera afterwards does not shift the map).
static int center_x, center_z;

static struct map_projection screen_map_projection(int width, int height) {
	// Fit the grid to the shorter screen axis, leaving a margin for the title
	// and the control hints.
	int usable = (width < height ? width : height) - 64;
	if(usable < MAP_CELLS)
		usable = MAP_CELLS;
	int cell_px = usable / MAP_CELLS;
	if(cell_px < 1)
		cell_px = 1;
	return map_projection_make(center_x, center_z, MAP_CELLS, cell_px, width,
							   height);
}

// Surface block id -> RGB. Distinct colours for the families the wishlist world
// actually produces (grass/water/stone/sand/snow); everything else falls back
// to a neutral grey so unknown blocks are still visible.
static void surface_color(uint8_t type, uint8_t* r, uint8_t* g, uint8_t* b) {
	switch(type) {
		case BLOCK_GRASS:
		case BLOCK_LEAVES:
		case BLOCK_TALL_GRASS:
			*r = 90;
			*g = 150;
			*b = 70;
			break;
		case BLOCK_WATER_FLOW:
		case BLOCK_WATER_STILL:
		case BLOCK_ICE:
			*r = 60;
			*g = 90;
			*b = 200;
			break;
		case BLOCK_LAVA_FLOW:
		case BLOCK_LAVA_STILL:
			*r = 220;
			*g = 90;
			*b = 30;
			break;
		case BLOCK_SAND:
		case BLOCK_SANDSTONE:
			*r = 220;
			*g = 210;
			*b = 150;
			break;
		case BLOCK_SNOW:
		case BLOCK_SNOW_BLOCK:
			*r = 240;
			*g = 240;
			*b = 245;
			break;
		case BLOCK_LOG:
			*r = 110;
			*g = 80;
			*b = 50;
			break;
		case BLOCK_DIRT:
		case BLOCK_GRAVEL:
			*r = 130;
			*g = 100;
			*b = 75;
			break;
		case BLOCK_AIR:
			// no surface here (void column) -> very dark so it reads as a hole
			*r = 20;
			*g = 20;
			*b = 25;
			break;
		case BLOCK_STONE:
		default:
			*r = 130;
			*g = 130;
			*b = 130;
			break;
	}
}

static void screen_map_reset(struct screen* s, int width, int height) {
	input_pointer_enable(true);

	// Stop feeding player movement while the map is open.
	if(gstate.local_player)
		gstate.local_player->data.local_player.capture_input = false;

	center_x = (int)floorf(gstate.camera.x);
	center_z = (int)floorf(gstate.camera.z);
}

// Teleport the player onto the surface of world column (wx, wz) and return to
// the game. world_get_height returns (topmost solid block y + 1), i.e. the feet
// cell directly above the surface; the eye position is that plus EYE_HEIGHT, so
// the feet rest on the surface top face -- not embedded, not floating (the #57
// spawn convention). Returns false (and does nothing) for an unloaded/void
// column so a tap on empty space outside the world can never drop the player
// into the void.
static bool teleport_to_column(int wx, int wz) {
	w_coord_t feet = world_get_height(&gstate.world, wx, wz);

	// 0 is world_get_height's "no loaded section" sentinel; such columns render
	// as void on the map. Refuse to travel there -- there is no surface to land
	// on, and y=EYE_HEIGHT would teleport the player to the bottom of the world.
	if(feet <= 0)
		return false;

	double tx = wx + 0.5;
	double tz = wz + 0.5;
	double ty = (double)feet + EYE_HEIGHT;

	// Update the server's authoritative position via the existing teleport RPC.
	svin_rpc_send(&(struct server_rpc) {
		.type = SRPC_PLAYER_POS,
		.payload.player_pos.x = tx,
		.payload.player_pos.y = ty,
		.payload.player_pos.z = tz,
		.payload.player_pos.rx = -glm_deg(gstate.camera.rx),
		.payload.player_pos.ry = glm_deg(gstate.camera.ry) - 90.0F,
	});

	// The server stores the position but only echoes it back on world load;
	// meanwhile the client re-reports gstate.camera every 50 ms. Move the local
	// player entity directly so the camera follows immediately and the next
	// periodic report carries the new column, not the stale one.
	if(gstate.local_player)
		gstate.local_player->teleport(gstate.local_player,
									  (vec3) {tx, ty, tz});

	screen_set(&screen_ingame);
	return true;
}

static void screen_map_update(struct screen* s, float dt) {
	// Close: the dedicated map key toggles it, the menu key also backs out.
	if(input_pressed(IB_MAP) || input_pressed(IB_HOME)) {
		screen_set(&screen_ingame);
		return;
	}

	float px, py, angle;
	bool have_pointer = input_pointer(&px, &py, &angle);

	if(have_pointer && input_pressed(IB_GUI_CLICK)) {
		struct map_projection proj
			= screen_map_projection(gfx_width(), gfx_height());

		int wx, wz;
		if(map_pixel_to_world(&proj, (int)px, (int)py, &wx, &wz))
			teleport_to_column(wx, wz);
	}
}

static void screen_map_render2D(struct screen* s, int width, int height) {
	gutil_bg();

	gutil_text((width - gutil_font_width("Map", 16)) / 2, 8, "Map", 16, true);

	struct map_projection proj = screen_map_projection(width, height);

	gfx_texture(false);

	// Border / backdrop behind the grid so void cells still sit on something.
	int span = proj.cells * proj.cell_px;
	gutil_texquad_col(proj.origin_x - 2, proj.origin_y - 2, 0, 0, 0, 0, span + 4,
					  span + 4, 0, 0, 0, 255);

	for(int j = 0; j < proj.cells; j++) {
		for(int i = 0; i < proj.cells; i++) {
			int wx, wz;
			map_cell_to_world(&proj, i, j, &wx, &wz);

			w_coord_t height_cell = world_get_height(&gstate.world, wx, wz);
			uint8_t r, g, b;
			if(height_cell <= 0) {
				surface_color(BLOCK_AIR, &r, &g, &b);
			} else {
				// topmost solid block sits one below the heightmap value
				struct block_data blk = world_get_block(&gstate.world, wx,
														height_cell - 1, wz);
				surface_color(blk.type, &r, &g, &b);
			}

			int cx, cy;
			map_cell_to_pixel(&proj, i, j, &cx, &cy);
			gutil_texquad_col(cx, cy, 0, 0, 0, 0, proj.cell_px, proj.cell_px, r,
							  g, b, 255);
		}
	}

	// Player marker: a red square on the centre cell.
	int half = proj.cells / 2;
	int mx, my;
	map_cell_to_pixel(&proj, half, half, &mx, &my);
	int marker = proj.cell_px < 3 ? 3 : proj.cell_px;
	gutil_texquad_col(mx - (marker - proj.cell_px) / 2,
					  my - (marker - proj.cell_px) / 2, 0, 0, 0, 0, marker,
					  marker, 230, 40, 40, 255);

	gfx_texture(true);

	int icon_offset = 32;
	icon_offset += gutil_control_icon(icon_offset, IB_GUI_CLICK, "Travel");
	icon_offset += gutil_control_icon(icon_offset, IB_MAP, "Close");
}

struct screen screen_map = {
	.reset = screen_map_reset,
	.update = screen_map_update,
	.render2D = screen_map_render2D,
	.render3D = NULL,
	.render_world = false,
};
