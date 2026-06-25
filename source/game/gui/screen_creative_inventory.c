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

#include <limits.h>
#include <stdio.h>

#include "../../block/blocks.h"
#include "../../graphics/gfx_util.h"
#include "../../graphics/gui_util.h"
#include "../../item/inventory.h"
#include "../../item/window_container.h"
#include "../../network/server_interface.h"
#include "../../platform/gfx.h"
#include "../../platform/input.h"
#include "../game_state.h"
#include "creative_inventory_list.h"
#include "screen.h"

// Survival-style creative inventory: the same pick-up / move / swap / split
// cursor model as the survival inventory (screen_inventory.c), but the top of
// the screen is a paged grid of EVERY obtainable item (creative_inventory_list).
//
// Clicking a grid cell puts a full, non-depleting stack of that item onto the
// cursor (SRPC_CREATIVE_SET_PICKED -- the grid is a virtual, infinite source).
// Clicking one of the player's real hotbar/inventory slots goes through the
// exact same SRPC_WINDOW_CLICK path as survival, so moving items around behaves
// identically. This reuses WINDOWC_INVENTORY (the player inventory window that
// always exists); no separate window is opened.

#define GRID_COLS 9
#define GRID_ROWS 5
#define PAGE_SIZE (GRID_COLS * GRID_ROWS)

// Player slots shown at the bottom: 9 hotbar + 27 main inventory.
#define PLAYER_SLOTS (INVENTORY_SIZE_HOTBAR + INVENTORY_SIZE_MAIN)

// On-screen geometry (logical px drawn at 2x): 18px cell pitch, 16px icon.
#define PITCH (18 * 2)
#define ICON (16 * 2)
#define GAP_GRID_MAIN 16
#define GAP_MAIN_HOTBAR 8

// Current page, reset to 0 each open; selected player slot for no-pointer
// (controller) navigation.
static int page;
static size_t selected; // 0..PLAYER_SLOTS-1

static int page_count(void) {
	size_t total = creative_inventory_count();
	if(total == 0)
		return 1;
	return (int)((total + PAGE_SIZE - 1) / PAGE_SIZE);
}

// Shared layout: column origin + the y of each row band, centred in the window.
static void layout(int width, int height, int* ox, int* grid_oy, int* main_oy,
				   int* hotbar_oy) {
	int content_h = GRID_ROWS * PITCH + GAP_GRID_MAIN
		+ (INVENTORY_SIZE_MAIN / GRID_COLS) * PITCH + GAP_MAIN_HOTBAR + PITCH;
	int oy = (height - content_h) / 2;
	if(oy < 40)
		oy = 40; // keep clear of the title

	*ox = (width - GRID_COLS * PITCH) / 2;
	*grid_oy = oy;
	*main_oy = oy + GRID_ROWS * PITCH + GAP_GRID_MAIN;
	*hotbar_oy
		= *main_oy + (INVENTORY_SIZE_MAIN / GRID_COLS) * PITCH + GAP_MAIN_HOTBAR;
}

static void grid_cell_rect(int width, int height, int col, int row, int* x,
						   int* y) {
	int ox, grid_oy, main_oy, hotbar_oy;
	layout(width, height, &ox, &grid_oy, &main_oy, &hotbar_oy);
	*x = ox + col * PITCH;
	*y = grid_oy + row * PITCH;
}

// Screen rect + inventory slot id of player slot k (0..8 hotbar, 9..35 main).
static size_t player_slot_rect(int width, int height, size_t k, int* x,
							   int* y) {
	int ox, grid_oy, main_oy, hotbar_oy;
	layout(width, height, &ox, &grid_oy, &main_oy, &hotbar_oy);

	if(k < INVENTORY_SIZE_HOTBAR) {
		*x = ox + (int)k * PITCH;
		*y = hotbar_oy;
		return INVENTORY_SLOT_HOTBAR + k;
	}

	size_t m = k - INVENTORY_SIZE_HOTBAR;
	*x = ox + (int)(m % GRID_COLS) * PITCH;
	*y = main_oy + (int)(m / GRID_COLS) * PITCH;
	return INVENTORY_SLOT_MAIN + m;
}

static void send_window_click(size_t slot, bool right) {
	uint16_t action_id;
	if(windowc_new_action(gstate.windows[WINDOWC_INVENTORY], &action_id, right,
						  slot)) {
		svin_rpc_send(&(struct server_rpc) {
			.type = SRPC_WINDOW_CLICK,
			.payload.window_click.window = WINDOWC_INVENTORY,
			.payload.window_click.action_id = action_id,
			.payload.window_click.right_click = right,
			.payload.window_click.slot = slot,
		});
	}
}

static void send_set_picked(uint16_t item_id) {
	svin_rpc_send(&(struct server_rpc) {
		.type = SRPC_CREATIVE_SET_PICKED,
		.payload.creative_set_picked.item_id = item_id,
	});
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
	selected = inventory_get_hotbar(
		windowc_get_latest(gstate.windows[WINDOWC_INVENTORY]));
}

// Index of the grid cell under (px,py) on the current page, or -1.
static int grid_cell_at(int width, int height, float px, float py) {
	size_t total = creative_inventory_count();
	for(int row = 0; row < GRID_ROWS; row++) {
		for(int col = 0; col < GRID_COLS; col++) {
			size_t index
				= (size_t)page * PAGE_SIZE + (size_t)row * GRID_COLS + col;
			if(index >= total)
				continue;
			int cx, cy;
			grid_cell_rect(width, height, col, row, &cx, &cy);
			if(px >= cx && px < cx + ICON && py >= cy && py < cy + ICON)
				return (int)index;
		}
	}
	return -1;
}

// Index of the player slot (0..PLAYER_SLOTS-1) under (px,py), or -1.
static int player_slot_at(int width, int height, float px, float py) {
	for(size_t k = 0; k < PLAYER_SLOTS; k++) {
		int x, y;
		player_slot_rect(width, height, k, &x, &y);
		if(px >= x && px < x + ICON && py >= y && py < y + ICON)
			return (int)k;
	}
	return -1;
}

// Move `selected` to the nearest player slot in the given direction.
static void nav_player_slots(int width, int height, int dx, int dy) {
	int sx, sy;
	player_slot_rect(width, height, selected, &sx, &sy);

	size_t best = selected;
	int best_dist = INT_MAX;
	for(size_t k = 0; k < PLAYER_SLOTS; k++) {
		if(k == selected)
			continue;
		int x, y;
		player_slot_rect(width, height, k, &x, &y);
		int ddx = x - sx, ddy = y - sy;
		if((dx < 0 && ddx >= 0) || (dx > 0 && ddx <= 0) || (dy < 0 && ddy >= 0)
		   || (dy > 0 && ddy <= 0))
			continue;
		int dist = (dx != 0) ? ddx * ddx + ddy * ddy * 8
							  : ddx * ddx * 8 + ddy * ddy;
		if(dist < best_dist) {
			best_dist = dist;
			best = k;
		}
	}
	selected = best;
}

static void screen_creative_inventory_update(struct screen* s, float dt) {
	(void)dt;

	int width = gfx_width();
	int height = gfx_height();

	// Close: clear the (virtual) cursor first so no phantom stack is carried
	// over, then back out. The cursor item is infinite/virtual in creative, so
	// discarding it is correct (mirrors Minecraft creative).
	if(input_pressed(IB_INVENTORY) || input_pressed(IB_HOME)) {
		send_set_picked(0);
		screen_set(&screen_ingame);
		return;
	}

	// Page the item grid (wraps). Driven by the page key and, with no pointer,
	// the GUI left/right too.
	if(input_pressed(IB_CREATIVE_PAGE))
		page = (page + 1) % page_count();

	float px, py, angle;
	bool have_pointer = input_pointer(&px, &py, &angle);

	int hover_cell = have_pointer ? grid_cell_at(width, height, px, py) : -1;
	int hover_slot = have_pointer ? player_slot_at(width, height, px, py) : -1;

	if(have_pointer && hover_slot >= 0)
		selected = (size_t)hover_slot;

	if(input_pressed(IB_GUI_CLICK) || input_pressed(IB_GUI_CLICK_ALT)) {
		bool right = input_pressed(IB_GUI_CLICK_ALT);
		if(hover_cell >= 0) {
			// Grab a full stack of the hovered item onto the cursor.
			send_set_picked(creative_inventory_item_id((size_t)hover_cell));
		} else if(hover_slot >= 0) {
			int x, y;
			send_window_click(player_slot_rect(width, height, (size_t)hover_slot,
											   &x, &y),
							  right);
		} else if(!have_pointer) {
			int x, y;
			send_window_click(
				player_slot_rect(width, height, selected, &x, &y), right);
		}
	}

	// Keyboard / controller navigation over the player slots (no pointer). Page
	// keys double as grid paging above; here left/right/up/down move the slot
	// selection so a controller can still rearrange the inventory and place the
	// picked stack.
	if(!have_pointer) {
		if(input_pressed(IB_GUI_LEFT))
			nav_player_slots(width, height, -1, 0);
		if(input_pressed(IB_GUI_RIGHT))
			nav_player_slots(width, height, 1, 0);
		if(input_pressed(IB_GUI_UP))
			nav_player_slots(width, height, 0, -1);
		if(input_pressed(IB_GUI_DOWN))
			nav_player_slots(width, height, 0, 1);
	}
}

// Draw a grey cell backdrop so empty cells still read as a grid.
static void cell_backdrop(int x, int y) {
	gutil_texquad_col(x - 2, y - 2, 0, 0, 0, 0, ICON + 4, ICON + 4, 60, 60, 60,
					  255);
}

static void screen_creative_inventory_render2D(struct screen* s, int width,
											   int height) {
	gutil_bg();

	struct inventory* inv
		= windowc_get_latest(gstate.windows[WINDOWC_INVENTORY]);

	char title[48];
	snprintf(title, sizeof(title), "Creative Inventory  (page %d/%d)", page + 1,
			 page_count());
	gutil_text((width - gutil_font_width(title, 16)) / 2, 8, title, 16, true);

	// Backdrops: grid + player slots.
	gfx_texture(false);
	for(int row = 0; row < GRID_ROWS; row++) {
		for(int col = 0; col < GRID_COLS; col++) {
			int cx, cy;
			grid_cell_rect(width, height, col, row, &cx, &cy);
			cell_backdrop(cx, cy);
		}
	}
	for(size_t k = 0; k < PLAYER_SLOTS; k++) {
		int x, y;
		player_slot_rect(width, height, k, &x, &y);
		cell_backdrop(x, y);
	}
	gfx_texture(true);

	// Grid item icons for this page.
	size_t total = creative_inventory_count();
	for(int row = 0; row < GRID_ROWS; row++) {
		for(int col = 0; col < GRID_COLS; col++) {
			size_t index
				= (size_t)page * PAGE_SIZE + (size_t)row * GRID_COLS + col;
			if(index >= total)
				continue;
			uint16_t id = creative_inventory_item_id(index);
			if(id == 0)
				continue;
			int cx, cy;
			grid_cell_rect(width, height, col, row, &cx, &cy);
			// count 1 -> no number overlay, just the icon.
			gutil_draw_item(&(struct item_data) {.id = id, .count = 1}, cx, cy,
							1);
		}
	}

	// Player slot item icons.
	for(size_t k = 0; k < PLAYER_SLOTS; k++) {
		int x, y;
		size_t slot = player_slot_rect(width, height, k, &x, &y);
		struct item_data item;
		if(inventory_get_slot(inv, slot, &item))
			gutil_draw_item(&item, x, y, 1);
	}

	float px, py, angle;
	bool have_pointer = input_pointer(&px, &py, &angle);

	// Selection highlight for no-pointer navigation.
	if(!have_pointer) {
		int x, y;
		player_slot_rect(width, height, selected, &x, &y);
		gfx_bind_texture(&texture_gui2);
		gutil_texquad(x - 8, y - 8, 208, 0, 24, 24, 24 * 2, 24 * 2);
	}

	// Hover tooltip (grid or player slot).
	char* hover_name = NULL;
	float hx = 0, hy = 0;
	if(have_pointer) {
		int cell = grid_cell_at(width, height, px, py);
		int pslot = player_slot_at(width, height, px, py);
		if(cell >= 0) {
			uint16_t id = creative_inventory_item_id((size_t)cell);
			hover_name = (id < ITEMS_MAX && items[id]) ? items[id]->name
													   : "Unknown";
			hx = px;
			hy = py;
		} else if(pslot >= 0) {
			int x, y;
			size_t slot = player_slot_rect(width, height, (size_t)pslot, &x, &y);
			struct item_data item;
			if(inventory_get_slot(inv, slot, &item))
				hover_name = item_get(&item) ? item_get(&item)->name : "Unknown";
			hx = px;
			hy = py;
		}
	}

	if(hover_name) {
		gfx_blending(MODE_BLEND);
		gfx_texture(false);
		gutil_texquad_col(hx + 8, hy - 4, 0, 0, 0, 0,
						  gutil_font_width(hover_name, 16) + 7, 16 + 8, 0, 0, 0,
						  180);
		gfx_texture(true);
		gfx_blending(MODE_OFF);
		gutil_text(hx + 11, hy, hover_name, 16, true);
	}

	// Picked item rides the cursor (or the selected slot with no pointer).
	struct item_data picked;
	if(inventory_get_picked_item(inv, &picked)) {
		if(have_pointer) {
			gutil_draw_item(&picked, px - 8 * 2, py - 8 * 2, 0);
		} else {
			int x, y;
			player_slot_rect(width, height, selected, &x, &y);
			gutil_draw_item(&picked, x, y, 0);
		}
	}

	if(have_pointer) {
		gfx_bind_texture(&texture_pointer);
		gutil_texquad_rt_any(px, py, glm_rad(angle), 0, 0, 256, 256, 96, 96);
	}

	int icon_offset = 32;
	icon_offset += gutil_control_icon(icon_offset, IB_GUI_CLICK, "Grab / move");
	icon_offset
		+= gutil_control_icon(icon_offset, IB_GUI_CLICK_ALT, "Place one");
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
