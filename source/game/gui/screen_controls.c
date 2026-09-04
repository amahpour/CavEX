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

// On-screen Controls help dialog. USB gamepad buttons have no printed labels and
// the HUD hint icons are blank on PC, so this lists the scheme AND shows a live
// "you just pressed: <action>" line per player — press any button/stick to see
// what it does (and to spot a look direction that needs swapping).

#include <stdio.h>

#include "../../graphics/gui_util.h"
#include "../../platform/gfx.h"
#include "../../platform/input.h"
#include "../game_state.h"
#include "screen.h"

// Actions scanned for the live "pressed now" readout, most-useful first.
static const struct {
	enum input_button b;
	const char* name;
} CONTROLS[] = {
	{IB_LOOK_UP, "Look up"},	  {IB_LOOK_DOWN, "Look down"},
	{IB_LOOK_LEFT, "Look left"},  {IB_LOOK_RIGHT, "Look right"},
	{IB_FORWARD, "Move forward"}, {IB_BACKWARD, "Move back"},
	{IB_LEFT, "Move left"},		  {IB_RIGHT, "Move right"},
	{IB_ACTION1, "Mine / dig"},	  {IB_ACTION2, "Place / use"},
	{IB_JUMP, "Jump"},			  {IB_SNEAK, "Sneak"},
	{IB_INVENTORY, "Inventory"},  {IB_SCROLL_LEFT, "Hotbar <"},
	{IB_SCROLL_RIGHT, "Hotbar >"},{IB_HOME, "Save & quit"},
};

// Gamepad legend (fixed scheme); keyboard help is a couple of summary lines.
static const char* PAD_LEGEND[] = {
	"\247fGAMEPAD  (each pad = one player)",
	"  D-Pad ............ Look  (up / down / left / right)",
	"  Face buttons ..... Move  (X fwd, B back, Y left, A right)",
	"  L shoulder ....... Mine / dig",
	"  R shoulder ....... Place / use",
	"  Select ........... Jump",
	"  Start ............ Inventory",
};
static const char* KEY_LEGEND[] = {
	"\247fKEYBOARD",
	"  P1: WASD move, mouse look, LMB mine, RMB place,",
	"      Space jump, E inventory, wheel hotbar",
	"  P2: IJKL move, arrows look, ',' mine, '.' place,",
	"      R-Shift jump, P inventory, U/O hotbar",
};

static void screen_controls_reset(struct screen* s, int width, int height) {
	// Pause both local players while the help is up (like the map screen).
	if(gstate.local_player)
		gstate.local_player->data.local_player.capture_input = false;
	if(gstate.local_player2)
		gstate.local_player2->data.local_player.capture_input = false;
}

static void screen_controls_update(struct screen* s, float dt) {
	if(input_pressed(IB_HELP) || input_pressed_dev(IB_HELP, 1)
	   || input_pressed(IB_HOME) || input_pressed_dev(IB_HOME, 1))
		screen_set(&screen_ingame);
}

// First currently-held action for a device, or NULL — the live readout.
static const char* pressed_now(int device) {
	for(size_t k = 0; k < sizeof(CONTROLS) / sizeof(CONTROLS[0]); k++)
		if(input_held_dev(CONTROLS[k].b, device))
			return CONTROLS[k].name;
	return NULL;
}

static void screen_controls_render2D(struct screen* s, int width, int height) {
	// dim the screen
	gfx_texture(false);
	gutil_texquad_col(0, 0, 0, 0, 0, 0, width, height, 0, 0, 0, 210);
	gfx_texture(true);

	gutil_text((width - gutil_font_width("Controls", 16)) / 2, 12, "Controls",
			   16, true);

	int x = 24;
	int y = 48;
	for(size_t k = 0; k < sizeof(PAD_LEGEND) / sizeof(PAD_LEGEND[0]); k++, y += 20)
		gutil_text(x, y, (char*)PAD_LEGEND[k], 12, true);

	y += 12;
	for(size_t k = 0; k < sizeof(KEY_LEGEND) / sizeof(KEY_LEGEND[0]); k++, y += 20)
		gutil_text(x, y, (char*)KEY_LEGEND[k], 12, true);

	// Live readout: press any control to confirm what it does.
	y += 20;
	gutil_text(x, y, "\247ePress a button to test it:", 12, true);
	y += 22;

	char line[64];
	const char* p1 = pressed_now(0);
	snprintf(line, sizeof(line), "  Player 1:  %s", p1 ? p1 : "-");
	gutil_text(x, y, line, 12, true);
	y += 20;

	if(gstate.num_local_players == 2) {
		const char* p2 = pressed_now(1);
		snprintf(line, sizeof(line), "  Player 2:  %s", p2 ? p2 : "-");
		gutil_text(x, y, line, 12, true);
		y += 20;
	}

	gutil_text(x, height - 28, "\2477Press F1 (or Save & quit) to close", 12,
			   true);
}

struct screen screen_controls = {
	.reset = screen_controls_reset,
	.update = screen_controls_update,
	.render2D = screen_controls_render2D,
	.render3D = NULL,
	.render_world = false,
	.render2D_fullscreen = false,
};
