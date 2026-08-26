/*
	Copyright (c) 2022 ByteBit/xtreme8000

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

#ifndef SCREEN_H
#define SCREEN_H

#include <stdbool.h>

#include "../../cglm/cglm.h"

struct screen {
	void (*reset)(struct screen* s, int width, int height);
	void (*update)(struct screen* s, float dt);
	void (*render2D)(struct screen* s, int width, int height);
	void (*render3D)(struct screen* s, mat4 view);
	bool render_world;
	// Split-screen: draw render2D once over the FULL screen after the per-view
	// loop instead of once per half. For modal GUIs (inventory/crafting/
	// furnace) whose update() hit-tests under the full viewport — and whose
	// 334-unit-tall layout cannot fit a 240-unit Wii half.
	bool render2D_fullscreen;
};

extern struct screen screen_ingame;
extern struct screen screen_load_world;
extern struct screen screen_select_world;
extern struct screen screen_map;
extern struct screen screen_inventory;
extern struct screen screen_crafting;
extern struct screen screen_furnace;
extern struct screen screen_controls;

void screen_set(struct screen* s);

void screen_crafting_set_windowc(uint8_t container);
void screen_furnace_set_windowc(uint8_t container);
// Which local player opened the window / owns the screen (issue #139): its
// GUI reads that player's input device and its clicks act on that player.
void screen_crafting_set_owner(uint8_t player);
void screen_furnace_set_owner(uint8_t player);
void screen_inventory_set_owner(uint8_t player);

// Split-screen per-owner inventory overlay (issue #140 follow-up): screen_ingame
// drives these so each local player's inventory is drawn into its own half and
// the other player keeps playing. `owner` is the input device (0 or 1).
void screen_inventory_open_owner(uint8_t owner);
bool screen_inventory_update_owner(uint8_t owner, float dt);
void screen_inventory_render_owner(uint8_t owner, int width, int height);

#endif
