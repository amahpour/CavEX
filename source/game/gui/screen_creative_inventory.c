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

#include <stdio.h>

#include "../../block/blocks.h"
#include "../../graphics/gfx_util.h"
#include "../../graphics/gui_util.h"
#include "../../network/server_interface.h"
#include "../../platform/gfx.h"
#include "../../platform/input.h"
#include "../game_state.h"
#include "creative_inventory_list.h"
#include "screen.h"

// Creative pick-any inventory: a full-screen, paginated grid of every placeable
// registered block (see creative_inventory_list.c for the enumeration). Clicking
// a cell with the GUI pointer asks the server for a full stack of that block in
// the selected hotbar slot; the stack never depletes because creative placing
// does not consume (issue #64). This screen is only ever opened from
// screen_ingame while the local player is creative -- the survival inventory
// path is untouched.

// 9 columns x 5 rows, matching the survival inventory's column count and cell
// pitch (18px logical, drawn at 2x). 45 cells/page comfortably pages the
// ~100-block set in a few pages while staying within the Wii MEM1 budget (no
// per-page allocation -- everything is drawn straight from the registry).
#define GRID_COLS 9
#define GRID_ROWS 5
#define PAGE_SIZE (GRID_COLS * GRID_ROWS)
#define CELL 18 // logical pixels between cell origins (drawn at *2)

// Current page, retained across frames; reset to 0 each time the screen opens.
static int page;

static int page_count(void) {
	size_t total = creative_inventory_count();
	if(total == 0)
		return 1;
	return (int)((total + PAGE_SIZE - 1) / PAGE_SIZE);
}

// Top-left logical origin of the grid, centred in the window.
static void grid_origin(int width, int height, int* ox, int* oy) {
	*ox = (width - GRID_COLS * CELL * 2) / 2;
	*oy = (height - GRID_ROWS * CELL * 2) / 2;
}

static void screen_creative_inventory_reset(struct screen* s, int width,
											int height) {
	(void)width;
	(void)height;
	input_pointer_enable(true);

	// Stop feeding player movement while the picker is open.
	if(gstate.local_player)
		gstate.local_player->data.local_player.capture_input = false;

	page = 0;
}

// Pixel rect (top-left + size) of grid cell `col,row` for the current window.
static void cell_rect(int width, int height, int col, int row, int* x, int* y) {
	int ox, oy;
	grid_origin(width, height, &ox, &oy);
	*x = ox + col * CELL * 2;
	*y = oy + row * CELL * 2;
}

static void screen_creative_inventory_update(struct screen* s, float dt) {
	(void)dt;

	// Close: the inventory key toggles it, the menu key also backs out.
	if(input_pressed(IB_INVENTORY) || input_pressed(IB_HOME)) {
		screen_set(&screen_ingame);
		return;
	}

	// Keyboard page navigation (wraps) so the all-blocks grid can be paged with
	// no pointer -- the capture rig drives this to show every page.
	if(input_pressed(IB_CREATIVE_PAGE))
		page = (page + 1) % page_count();

	// On-screen prev/next also map onto the GUI left/right when there is no
	// pointer, so a controller without a pointer can still page.
	if(input_pressed(IB_GUI_RIGHT))
		page = (page + 1) % page_count();
	if(input_pressed(IB_GUI_LEFT))
		page = (page + page_count() - 1) % page_count();

	float px, py, angle;
	bool have_pointer = input_pointer(&px, &py, &angle);

	if(have_pointer && input_pressed(IB_GUI_CLICK)) {
		int width = gfx_width();
		int height = gfx_height();

		for(int row = 0; row < GRID_ROWS; row++) {
			for(int col = 0; col < GRID_COLS; col++) {
				size_t index
					= (size_t)page * PAGE_SIZE + (size_t)row * GRID_COLS + col;
				if(index >= creative_inventory_count())
					continue;

				int cx, cy;
				cell_rect(width, height, col, row, &cx, &cy);

				if(px >= cx && px < cx + 16 * 2 && py >= cy
				   && py < cy + 16 * 2) {
					uint8_t id = creative_inventory_block_id(index);
					if(id > 0)
						svin_rpc_send(&(struct server_rpc) {
							.type = SRPC_CREATIVE_PICK_BLOCK,
							.payload.creative_pick_block.block_id = id,
						});
				}
			}
		}
	}
}

static void screen_creative_inventory_render2D(struct screen* s, int width,
											   int height) {
	gutil_bg();

	char title[48];
	snprintf(title, sizeof(title), "Creative Inventory  (page %d/%d)", page + 1,
			 page_count());
	gutil_text((width - gutil_font_width(title, 16)) / 2, 8, title, 16, true);

	int ox, oy;
	grid_origin(width, height, &ox, &oy);

	// Cell backdrops so empty/short final pages still read as a grid.
	gfx_texture(false);
	for(int row = 0; row < GRID_ROWS; row++) {
		for(int col = 0; col < GRID_COLS; col++) {
			int cx, cy;
			cell_rect(width, height, col, row, &cx, &cy);
			gutil_texquad_col(cx - 2, cy - 2, 0, 0, 0, 0, 16 * 2 + 4, 16 * 2 + 4,
							  60, 60, 60, 255);
		}
	}
	gfx_texture(true);

	// Block icons for this page.
	size_t total = creative_inventory_count();
	for(int row = 0; row < GRID_ROWS; row++) {
		for(int col = 0; col < GRID_COLS; col++) {
			size_t index
				= (size_t)page * PAGE_SIZE + (size_t)row * GRID_COLS + col;
			if(index >= total)
				continue;

			uint8_t id = creative_inventory_block_id(index);
			if(id == 0)
				continue;

			int cx, cy;
			cell_rect(width, height, col, row, &cx, &cy);
			// count 1 -> no number overlay, just the block icon.
			gutil_draw_item(&(struct item_data) {.id = id, .count = 1}, cx, cy,
							1);
		}
	}

	// Pointer cursor + hovered block name.
	float px, py, angle;
	if(input_pointer(&px, &py, &angle)) {
		for(int row = 0; row < GRID_ROWS; row++) {
			for(int col = 0; col < GRID_COLS; col++) {
				size_t index
					= (size_t)page * PAGE_SIZE + (size_t)row * GRID_COLS + col;
				if(index >= total)
					continue;

				int cx, cy;
				cell_rect(width, height, col, row, &cx, &cy);
				if(px >= cx && px < cx + 16 * 2 && py >= cy
				   && py < cy + 16 * 2) {
					uint8_t id = creative_inventory_block_id(index);
					char* name = blocks[id] ? blocks[id]->name : "Unknown";

					gfx_blending(MODE_BLEND);
					gfx_texture(false);
					gutil_texquad_col(px + 8, py - 4, 0, 0, 0, 0,
									  gutil_font_width(name, 16) + 7, 16 + 8, 0,
									  0, 0, 180);
					gfx_texture(true);
					gfx_blending(MODE_OFF);
					gutil_text(px + 11, py, name, 16, true);
				}
			}
		}

		gfx_bind_texture(&texture_pointer);
		gutil_texquad_rt_any(px, py, glm_rad(angle), 0, 0, 256, 256, 96, 96);
	}

	int icon_offset = 32;
	icon_offset += gutil_control_icon(icon_offset, IB_GUI_CLICK, "Grab block");
	icon_offset += gutil_control_icon(icon_offset, IB_CREATIVE_PAGE, "Next page");
	icon_offset += gutil_control_icon(icon_offset, IB_INVENTORY, "Close");
}

struct screen screen_creative_inventory = {
	.reset = screen_creative_inventory_reset,
	.update = screen_creative_inventory_update,
	.render2D = screen_creative_inventory_render2D,
	.render3D = NULL,
	.render_world = false,
};
