/*
	Copyright (c) 2023 ByteBit/xtreme8000

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
#include "../../graphics/render_model.h"
#include "../../item/items.h"
#include "../../network/server_interface.h"
#include "../../platform/gfx.h"
#include "../../platform/input.h"
#include "../../platform/time.h"
#include "../game_state.h"
#include "creative_inventory_list.h"
#include "screen.h"

#define GUI_WIDTH 176
#define GUI_HEIGHT 167

// Creative mode reuses THIS survival inventory screen verbatim (player model,
// crafting, armor, your slots, identical pickup/move/swap/split) and adds a
// paged "all items" grab strip above it: click a cell to put a full,
// non-depleting stack of that item on the cursor, then drop it into any slot.
// Everything below is gated on creative_active(); the survival path is
// untouched. The strip + GUI are centred together as one unit. The strip's row
// count (cinv_rows) FILLS the window height above the GUI, so a bigger/maximised
// window shows more items and fewer pages; it is clamped so the Wii's 480px
// screen keeps the minimum and very tall windows stay sane.
#define CINV_COLS 9
#define CINV_PITCH (18 * 2) // px between cell origins
#define CINV_ICON (16 * 2)	// drawn icon size
#define CINV_GAP 10			// px between the strip and the GUI
#define CINV_TOP 28			// px reserved above the strip for the title
#define CINV_MIN_ROWS 3		// keep >=3 even on the Wii's 480px screen
#define CINV_MAX_ROWS 8		// cap the grid on very tall windows

struct inv_slot {
	int x, y;
	size_t slot;
};

static bool pointer_has_item;
static bool pointer_available;
static float pointer_x, pointer_y, pointer_angle;
static struct inv_slot slots[INVENTORY_SIZE];
static size_t slots_index;

static size_t selected_slot;

// Creative grab-strip state (unused in survival). cinv_sel is the focused strip
// cell (0..cinv_page_size()-1) when keyboard focus is ON the strip, or -1 when focus is
// on the inventory slots (the survival behaviour).
static int cinv_page;
static int cinv_sel;

static bool creative_active(void) {
	return gstate.local_player
		&& gstate.local_player->data.local_player.creative;
}

// Number of strip rows that fit above the GUI in the current window, clamped.
static int cinv_rows(void) {
	int avail = gfx_height() - CINV_TOP - CINV_GAP - GUI_HEIGHT * 2;
	int r = avail / CINV_PITCH;
	if(r < CINV_MIN_ROWS)
		r = CINV_MIN_ROWS;
	if(r > CINV_MAX_ROWS)
		r = CINV_MAX_ROWS;
	return r;
}

static int cinv_page_size(void) {
	return cinv_rows() * CINV_COLS;
}

static int cinv_page_count(void) {
	size_t total = creative_inventory_count();
	int ps = cinv_page_size();
	return total ? (int)(((int)total + ps - 1) / ps) : 1;
}

// Top-left origin of the survival GUI. In creative the whole {strip + GUI} unit
// is centred, so the GUI is pushed down by the strip height; in survival it is
// the original centred position (byte-identical).
static void inv_origin(int width, int height, int* off_x, int* off_y) {
	*off_x = (width - GUI_WIDTH * 2) / 2;
	if(creative_active()) {
		int strip = cinv_rows() * CINV_PITCH;
		int total = strip + CINV_GAP + GUI_HEIGHT * 2;
		int top = (height - total) / 2;
		if(top < 4)
			top = 4;
		*off_y = top + strip + CINV_GAP;
	} else {
		*off_y = (height - GUI_HEIGHT * 2) / 2;
	}
}

// Top-left origin of the grab strip (above the GUI). Creative only.
static void cinv_origin(int width, int height, int* gx, int* gy) {
	int strip = cinv_rows() * CINV_PITCH;
	int total = strip + CINV_GAP + GUI_HEIGHT * 2;
	int top = (height - total) / 2;
	if(top < 4)
		top = 4;
	*gx = (width - CINV_COLS * CINV_PITCH) / 2;
	*gy = top;
}

// Grab-strip cell index under (px,py) on the current page, or -1.
static int cinv_cell_at(int width, int height, float px, float py) {
	int gx, gy;
	cinv_origin(width, height, &gx, &gy);
	size_t total = creative_inventory_count();
	for(int r = 0; r < cinv_rows(); r++) {
		for(int c = 0; c < CINV_COLS; c++) {
			size_t idx = (size_t)cinv_page * cinv_page_size() + (size_t)r * CINV_COLS
				+ c;
			if(idx >= total)
				continue;
			int cx = gx + c * CINV_PITCH;
			int cy = gy + r * CINV_PITCH;
			if(px >= cx && px < cx + CINV_ICON && py >= cy && py < cy + CINV_ICON)
				return (int)idx;
		}
	}
	return -1;
}

static void cinv_send_set_picked(uint16_t id) {
	svin_rpc_send(&(struct server_rpc) {
		.type = SRPC_CREATIVE_SET_PICKED,
		.payload.creative_set_picked.item_id = id,
	});
}

static void screen_inventory_reset(struct screen* s, int width, int height) {
	input_pointer_enable(true);

	if(gstate.local_player)
		gstate.local_player->data.local_player.capture_input = false;

	cinv_page = 0;
	// In creative, open with keyboard focus already ON the grab strip so WASD /
	// the d-pad walk the items right away (press DOWN to drop into your own
	// slots). Survival keeps focus on the slots.
	cinv_sel = creative_active() ? 0 : -1;

	s->render3D = screen_ingame.render3D;

	pointer_available = false;
	pointer_has_item = false;

	slots_index = 0;

	for(int k = 0; k < INVENTORY_SIZE_MAIN; k++) {
		slots[slots_index++] = (struct inv_slot) {
			.x = (8 + (k % INVENTORY_SIZE_HOTBAR) * 18) * 2,
			.y = (84 + (k / INVENTORY_SIZE_HOTBAR) * 18) * 2,
			.slot = k + INVENTORY_SLOT_MAIN,
		};
	}

	for(int k = 0; k < INVENTORY_SIZE_HOTBAR; k++) {
		if(k
		   == (int)inventory_get_hotbar(
			   windowc_get_latest(gstate.windows[WINDOWC_INVENTORY])))
			selected_slot = slots_index;

		slots[slots_index++] = (struct inv_slot) {
			.x = (8 + k * 18) * 2,
			.y = (84 + 3 * 18 + 4) * 2,
			.slot = k + INVENTORY_SLOT_HOTBAR,
		};
	}

	for(int k = 0; k < INVENTORY_SIZE_ARMOR; k++) {
		slots[slots_index++] = (struct inv_slot) {
			.x = 8 * 2,
			.y = (8 + k * 18) * 2,
			.slot = k + INVENTORY_SLOT_ARMOR,
		};
	}

	for(int k = 0; k < INVENTORY_SIZE_CRAFTING; k++) {
		slots[slots_index++] = (struct inv_slot) {
			.x = (88 + (k % 2) * 18) * 2,
			.y = (26 + (k / 2) * 18) * 2,
			.slot = k + INVENTORY_SLOT_CRAFTING,
		};
	}

	slots[slots_index++] = (struct inv_slot) {
		.x = 144 * 2,
		.y = 36 * 2,
		.slot = INVENTORY_SLOT_OUTPUT,
	};
}

static void screen_inventory_update(struct screen* s, float dt) {
	if(input_pressed(IB_INVENTORY)) {
		svin_rpc_send(&(struct server_rpc) {
			.type = SRPC_WINDOW_CLOSE,
			.payload.window_close.window = WINDOWC_INVENTORY,
		});

		screen_set(&screen_ingame);
	}

	// Creative grab strip: page it, and turn a click on a strip cell into a
	// non-depleting stack on the cursor. The click edge is only consumed when
	// the pointer is over a cell, so survival slot clicks below are unaffected.
	if(creative_active()) {
		// A window resize can change the row count (and thus the page count);
		// keep the page and strip selection in range.
		if(cinv_page >= cinv_page_count())
			cinv_page = 0;
		if(cinv_sel >= cinv_page_size())
			cinv_sel = cinv_page_size() - 1;

		if(input_pressed(IB_CREATIVE_PAGE) || input_pressed(IB_SCROLL_RIGHT))
			cinv_page = (cinv_page + 1) % cinv_page_count();
		if(input_pressed(IB_SCROLL_LEFT))
			cinv_page
				= (cinv_page + cinv_page_count() - 1) % cinv_page_count();

		float gpx, gpy, gpa;
		if(input_pointer(&gpx, &gpy, &gpa)) {
			int cell = cinv_cell_at(gfx_width(), gfx_height(), gpx, gpy);
			if(cell >= 0
			   && (input_pressed(IB_GUI_CLICK)
				   || input_pressed(IB_GUI_CLICK_ALT))) {
				cinv_send_set_picked(
					creative_inventory_item_id((size_t)cell));
				return; // consumed -- do not also click a slot
			}
		}
	}

	if(cinv_sel < 0 && input_pressed(IB_GUI_CLICK)) {
		uint16_t action_id;
		if(windowc_new_action(gstate.windows[WINDOWC_INVENTORY], &action_id,
							  false, slots[selected_slot].slot)) {
			svin_rpc_send(&(struct server_rpc) {
				.type = SRPC_WINDOW_CLICK,
				.payload.window_click.window = WINDOWC_INVENTORY,
				.payload.window_click.action_id = action_id,
				.payload.window_click.right_click = false,
				.payload.window_click.slot = slots[selected_slot].slot,
			});
		}
	} else if(cinv_sel < 0 && input_pressed(IB_GUI_CLICK_ALT)) {
		uint16_t action_id;
		if(windowc_new_action(gstate.windows[WINDOWC_INVENTORY], &action_id,
							  true, slots[selected_slot].slot)) {
			svin_rpc_send(&(struct server_rpc) {
				.type = SRPC_WINDOW_CLICK,
				.payload.window_click.window = WINDOWC_INVENTORY,
				.payload.window_click.action_id = action_id,
				.payload.window_click.right_click = true,
				.payload.window_click.slot = slots[selected_slot].slot,
			});
		}
	}

	pointer_available = input_pointer(&pointer_x, &pointer_y, &pointer_angle);

	size_t slot_nearest[4]
		= {selected_slot, selected_slot, selected_slot, selected_slot};
	int slot_dist[4] = {INT_MAX, INT_MAX, INT_MAX, INT_MAX};
	int pointer_slot = -1;

	int off_x, off_y;
	inv_origin(gfx_width(), gfx_height(), &off_x, &off_y);

	for(size_t k = 0; k < slots_index; k++) {
		int dx = slots[k].x - slots[selected_slot].x;
		int dy = slots[k].y - slots[selected_slot].y;

		if(pointer_x >= off_x + slots[k].x
		   && pointer_x < off_x + slots[k].x + 16 * 2
		   && pointer_y >= off_y + slots[k].y
		   && pointer_y < off_y + slots[k].y + 16 * 2)
			pointer_slot = k;

		int distx = dx * dx + dy * dy * 8;
		int disty = dx * dx * 8 + dy * dy;

		if(dx < 0 && distx < slot_dist[0]) {
			slot_nearest[0] = k;
			slot_dist[0] = distx;
		}

		if(dx > 0 && distx < slot_dist[1]) {
			slot_nearest[1] = k;
			slot_dist[1] = distx;
		}

		if(dy < 0 && disty < slot_dist[2]) {
			slot_nearest[2] = k;
			slot_dist[2] = disty;
		}

		if(dy > 0 && disty < slot_dist[3]) {
			slot_nearest[3] = k;
			slot_dist[3] = disty;
		}
	}

	if(pointer_available && pointer_slot >= 0) {
		selected_slot = pointer_slot;
		pointer_has_item = true;
		cinv_sel = -1; // mouse over a slot -> inventory focus
	} else if(creative_active() && cinv_sel >= 0) {
		// Keyboard/controller focus is on the grab strip: arrows/WASD walk the
		// item grid, page at the left/right edges, DOWN off the bottom row
		// returns to the inventory slots, and the click/confirm button grabs the
		// focused item onto the cursor.
		int col = cinv_sel % CINV_COLS;
		int row = cinv_sel / CINV_COLS;
		int pc = cinv_page_count();

		if(input_pressed(IB_GUI_LEFT)) {
			if(col > 0) {
				col--;
			} else {
				cinv_page = (cinv_page + pc - 1) % pc;
				col = CINV_COLS - 1;
			}
		}
		if(input_pressed(IB_GUI_RIGHT)) {
			if(col < CINV_COLS - 1) {
				col++;
			} else {
				cinv_page = (cinv_page + 1) % pc;
				col = 0;
			}
		}
		if(input_pressed(IB_GUI_UP) && row > 0)
			row--;
		if(input_pressed(IB_GUI_DOWN)) {
			if(row < cinv_rows() - 1)
				row++;
			else
				cinv_sel = -1; // drop back down to the inventory slots
		}

		if(cinv_sel >= 0) {
			cinv_sel = row * CINV_COLS + col;
			if(input_pressed(IB_GUI_CLICK) || input_pressed(IB_GUI_CLICK_ALT)) {
				size_t idx
					= (size_t)cinv_page * cinv_page_size() + (size_t)cinv_sel;
				if(idx < creative_inventory_count())
					cinv_send_set_picked(creative_inventory_item_id(idx));
			}
		}
	} else {
		if(input_pressed(IB_GUI_LEFT)) {
			selected_slot = slot_nearest[0];
			pointer_has_item = false;
		}

		if(input_pressed(IB_GUI_RIGHT)) {
			selected_slot = slot_nearest[1];
			pointer_has_item = false;
		}

		if(input_pressed(IB_GUI_UP)) {
			// Off the top of the inventory in creative -> step into the strip.
			if(creative_active() && slot_nearest[2] == selected_slot) {
				cinv_sel = (cinv_rows() - 1) * CINV_COLS;
			} else {
				selected_slot = slot_nearest[2];
				pointer_has_item = false;
			}
		}

		if(input_pressed(IB_GUI_DOWN)) {
			selected_slot = slot_nearest[3];
			pointer_has_item = false;
		}
	}

	// Over the grab strip (or empty space) with a pointer, keep the held item on
	// the cursor rather than snapping it to the selected slot.
	if(creative_active() && pointer_available && pointer_slot < 0)
		pointer_has_item = true;
}

// Draw the creative grab strip (page label + cells + item icons + hovered
// name) above the survival GUI. Creative only.
static void cinv_render2d(int width, int height) {
	int gx, gy;
	cinv_origin(width, height, &gx, &gy);
	size_t total = creative_inventory_count();

	char title[40];
	snprintf(title, sizeof(title), "\2478All items  (%d/%d)", cinv_page + 1,
			 cinv_page_count());
	gutil_text(gx, gy - 18, title, 16, false);

	gfx_texture(false);
	for(int r = 0; r < cinv_rows(); r++)
		for(int c = 0; c < CINV_COLS; c++)
			gutil_texquad_col(gx + c * CINV_PITCH - 2, gy + r * CINV_PITCH - 2, 0,
							  0, 0, 0, CINV_ICON + 4, CINV_ICON + 4, 60, 60, 60,
							  255);
	gfx_texture(true);

	for(int r = 0; r < cinv_rows(); r++) {
		for(int c = 0; c < CINV_COLS; c++) {
			size_t idx
				= (size_t)cinv_page * cinv_page_size() + (size_t)r * CINV_COLS + c;
			if(idx >= total)
				continue;
			uint16_t id = creative_inventory_item_id(idx);
			if(id == 0)
				continue;
			gutil_draw_item(&(struct item_data) {.id = id, .count = 1},
							gx + c * CINV_PITCH, gy + r * CINV_PITCH, 1);
		}
	}

	// keyboard/controller-focused cell highlight
	if(cinv_sel >= 0) {
		int hr = cinv_sel / CINV_COLS;
		int hc = cinv_sel % CINV_COLS;
		gfx_bind_texture(&texture_gui2);
		gutil_texquad(gx + hc * CINV_PITCH - 8, gy + hr * CINV_PITCH - 8, 208, 0,
					  24, 24, 24 * 2, 24 * 2);
	}

	// hovered item name
	float px, py, ang;
	if(input_pointer(&px, &py, &ang)) {
		int cell = cinv_cell_at(width, height, px, py);
		if(cell >= 0) {
			uint16_t id = creative_inventory_item_id((size_t)cell);
			char* name
				= (id < ITEMS_MAX && items[id]) ? items[id]->name : "Unknown";
			gfx_blending(MODE_BLEND);
			gfx_texture(false);
			gutil_texquad_col(px + 8, py - 4, 0, 0, 0, 0,
							  gutil_font_width(name, 16) + 7, 16 + 8, 0, 0, 0,
							  180);
			gfx_texture(true);
			gfx_blending(MODE_OFF);
			gutil_text(px + 11, py, name, 16, true);
		}
	}
}

static void screen_inventory_render2D(struct screen* s, int width, int height) {
	struct inventory* inv
		= windowc_get_latest(gstate.windows[WINDOWC_INVENTORY]);

	// darken background
	gfx_texture(false);
	gutil_texquad_col(0, 0, 0, 0, 0, 0, width, height, 0, 0, 0, 180);
	gfx_texture(true);

	int off_x, off_y;
	inv_origin(width, height, &off_x, &off_y);

	if(creative_active())
		cinv_render2d(width, height);

	// draw inventory
	gfx_bind_texture(&texture_gui_inventory);
	gutil_texquad(off_x, off_y, 0, 0, GUI_WIDTH, GUI_HEIGHT, GUI_WIDTH * 2,
				  GUI_HEIGHT * 2);
	gutil_text(off_x + 86 * 2, off_y + 16 * 2, "\2478Crafting", 16, false);

	struct inv_slot* selection = slots + selected_slot;

	float angle_x
		= atan2f((pointer_has_item ? pointer_x : off_x + selection->x + 8 * 2)
					 - (off_x + 52 * 2),
				 192.0F);
	float angle_y
		= atan2f((pointer_has_item ? pointer_y : off_y + selection->y + 8 * 2)
					 - (off_y + 19 * 2),
				 192.0F);

	mat4 view;
	glm_mat4_identity(view);
	glm_translate(view, (vec3) {off_x + 52 * 2, off_y + 39 * 2, 0.0F});
	glm_scale(view, (vec3) {3.75F, -3.75F, 1.0F});
	glm_rotate_x(view, angle_y * 0.66F * 0.5F, view);
	glm_rotate_y(view, angle_x * 0.5F, view);
	glm_translate(view, (vec3) {0.0F, 10.0F, 0.0F});

	gfx_write_buffers(true, true, true);
	struct item_data held_item, helmet, chestplate, leggings, boots;
	render_model_player(
		view, glm_deg(angle_y * 0.66F * 0.5F), glm_deg(angle_x * 0.5F), 0.0F,
		0.0F, inventory_get_hotbar_item(inv, &held_item) ? &held_item : NULL,
		inventory_get_slot(inv, INVENTORY_SLOT_ARMOR + 0, &helmet) ? &helmet :
																	 NULL,
		inventory_get_slot(inv, INVENTORY_SLOT_ARMOR + 1, &chestplate) ?
			&chestplate :
			NULL,
		inventory_get_slot(inv, INVENTORY_SLOT_ARMOR + 2, &leggings) ?
			&leggings :
			NULL,
		inventory_get_slot(inv, INVENTORY_SLOT_ARMOR + 3, &boots) ? &boots :
																	NULL);
	gfx_write_buffers(true, false, false);
	gfx_matrix_modelview(GLM_MAT4_IDENTITY);

	// draw items
	for(size_t k = 0; k < slots_index; k++) {
		struct item_data item;
		if((selected_slot != k || !inventory_get_picked_item(inv, NULL)
			|| (pointer_available && pointer_has_item))
		   && inventory_get_slot(inv, slots[k].slot, &item))
			gutil_draw_item(&item, off_x + slots[k].x, off_y + slots[k].y, 1);
	}

	// The inventory selection box is hidden while keyboard focus is on the grab
	// strip (the strip draws its own highlight).
	if(cinv_sel < 0) {
		gfx_bind_texture(&texture_gui2);
		gutil_texquad(off_x + selection->x - 8, off_y + selection->y - 8, 208, 0,
					  24, 24, 24 * 2, 24 * 2);
	}

	int icon_offset = 32;
	icon_offset += gutil_control_icon(icon_offset, IB_GUI_UP, "Move");
	if(inventory_get_picked_item(inv, NULL)) {
		icon_offset
			+= gutil_control_icon(icon_offset, IB_GUI_CLICK, "Swap item");
		icon_offset
			+= gutil_control_icon(icon_offset, IB_GUI_CLICK_ALT, "Place one");
	} else if(inventory_get_slot(inv, selection->slot, NULL)) {
		icon_offset
			+= gutil_control_icon(icon_offset, IB_GUI_CLICK, "Pickup item");
		icon_offset
			+= gutil_control_icon(icon_offset, IB_GUI_CLICK_ALT, "Split stack");
	}

	icon_offset += gutil_control_icon(icon_offset, IB_INVENTORY, "Leave");

	if(creative_active())
		icon_offset
			+= gutil_control_icon(icon_offset, IB_CREATIVE_PAGE, "Page items");

	struct item_data item;
	if(inventory_get_picked_item(inv, &item)) {
		if(pointer_available && pointer_has_item) {
			gutil_draw_item(&item, pointer_x - 8 * 2, pointer_y - 8 * 2, 0);
		} else if(cinv_sel >= 0) {
			int gx2, gy2;
			cinv_origin(width, height, &gx2, &gy2);
			gutil_draw_item(&item, gx2 + (cinv_sel % CINV_COLS) * CINV_PITCH,
							gy2 + (cinv_sel / CINV_COLS) * CINV_PITCH, 0);
		} else {
			gutil_draw_item(&item, off_x + selection->x, off_y + selection->y,
							0);
		}
	} else if(inventory_get_slot(inv, selection->slot, &item)) {
		char* tmp = item_get(&item) ? item_get(&item)->name : "Unknown";
		gfx_blending(MODE_BLEND);
		gfx_texture(false);
		gutil_texquad_col(off_x + selection->x - 4 + 16
							  - gutil_font_width(tmp, 16) / 2,
						  off_y + selection->y - 4 + 46, 0, 0, 0, 0,
						  gutil_font_width(tmp, 16) + 7, 16 + 8, 0, 0, 0, 180);
		gfx_texture(true);
		gfx_blending(MODE_OFF);

		gutil_text(off_x + selection->x + 16 - gutil_font_width(tmp, 16) / 2,
				   off_y + selection->y + 46, tmp, 16, false);
	}

	if(pointer_available) {
		gfx_bind_texture(&texture_pointer);
		gutil_texquad_rt_any(pointer_x, pointer_y, glm_rad(pointer_angle), 0, 0,
							 256, 256, 96, 96);
	}
}

struct screen screen_inventory = {
	.reset = screen_inventory_reset,
	.update = screen_inventory_update,
	.render2D = screen_inventory_render2D,
	.render3D = NULL,
	.render_world = true,
};
