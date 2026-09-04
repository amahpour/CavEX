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

// Pure (button, device) -> config-key mapping for the input layer. Split into its
// own translation unit, free of any platform headers (GLFW / libogc), so it can be
// linked into the unit-test build and exercised without hardware. See input.c for
// the runtime that consumes these keys.
//
// device 0 is the original single-player binding set ("input.player_*"); device 1
// is the local split-screen second player ("input.player2_*"). Buttons a device
// does not use return NULL, and the caller treats NULL as "unbound" (always
// false). Player 2 has its own inventory/GUI keys (issue #139) so it can drive
// its own screens; world-level buttons (save&quit, map, creative toggle,
// screenshot) stay player-1 only so a shared keyboard cannot collide.

#include <stddef.h>

#include "input.h"

const char* input_config_key(enum input_button b, int device) {
#ifdef PLATFORM_WII
	// Wii tells players apart by WPAD channel, not by key-name set: both
	// Wiimotes share the device-0 bindings (issue #140). Collapsing the device
	// index HERE — the one routing authority — keeps any caller correct; the
	// player2_* key sets below are a PC concept (one shared keyboard).
	device = 0;
#endif
	if(device <= 0) {
		switch(b) {
			case IB_ACTION1: return "input.item_action_left";
			case IB_ACTION2: return "input.item_action_right";
			case IB_FORWARD: return "input.player_forward";
			case IB_BACKWARD: return "input.player_backward";
			case IB_LEFT: return "input.player_left";
			case IB_RIGHT: return "input.player_right";
			case IB_JUMP: return "input.player_jump";
			case IB_SNEAK: return "input.player_sneak";
			case IB_INVENTORY: return "input.inventory";
			case IB_HOME: return "input.open_menu";
			case IB_SCROLL_LEFT: return "input.scroll_left";
			case IB_SCROLL_RIGHT: return "input.scroll_right";
			case IB_GUI_UP: return "input.gui_up";
			case IB_GUI_DOWN: return "input.gui_down";
			case IB_GUI_LEFT: return "input.gui_left";
			case IB_GUI_RIGHT: return "input.gui_right";
			case IB_GUI_CLICK: return "input.gui_click";
			case IB_GUI_CLICK_ALT: return "input.gui_click_alt";
			case IB_SCREENSHOT: return "input.screenshot";
			case IB_MAP: return "input.map";
			case IB_TOGGLE_CREATIVE: return "input.toggle_creative";
			case IB_CREATIVE_PAGE: return "input.creative_page";
			case IB_HELP: return "input.help";
			// Player 1 looks with the mouse (input_native_joystick); these
			// discrete look-keys ADD to that delta so player 1 can also look with
			// a gamepad's face buttons (config_pc.json leaves them empty for a
			// pure keyboard+mouse player, so nothing changes there).
			case IB_LOOK_UP: return "input.player_look_up";
			case IB_LOOK_DOWN: return "input.player_look_down";
			case IB_LOOK_LEFT: return "input.player_look_left";
			case IB_LOOK_RIGHT: return "input.player_look_right";
			default: return NULL;
		}
	}

	// device 1: local split-screen player 2 (keyboard-only, no mouse -> look-keys).
	switch(b) {
		case IB_FORWARD: return "input.player2_forward";
		case IB_BACKWARD: return "input.player2_backward";
		case IB_LEFT: return "input.player2_left";
		case IB_RIGHT: return "input.player2_right";
		case IB_JUMP: return "input.player2_jump";
		case IB_SNEAK: return "input.player2_sneak";
		case IB_ACTION1: return "input.player2_action_left";
		case IB_ACTION2: return "input.player2_action_right";
		case IB_LOOK_UP: return "input.player2_look_up";
		case IB_LOOK_DOWN: return "input.player2_look_down";
		case IB_LOOK_LEFT: return "input.player2_look_left";
		case IB_LOOK_RIGHT: return "input.player2_look_right";
		case IB_SCROLL_LEFT: return "input.player2_scroll_left";
		case IB_SCROLL_RIGHT: return "input.player2_scroll_right";
		case IB_HELP: return "input.player2_help";
		// Player 2 drives its own inventory / window screens (issue #139).
		case IB_INVENTORY: return "input.player2_inventory";
		case IB_GUI_UP: return "input.player2_gui_up";
		case IB_GUI_DOWN: return "input.player2_gui_down";
		case IB_GUI_LEFT: return "input.player2_gui_left";
		case IB_GUI_RIGHT: return "input.player2_gui_right";
		case IB_GUI_CLICK: return "input.player2_gui_click";
		case IB_GUI_CLICK_ALT: return "input.player2_gui_click_alt";
		default: return NULL;
	}
}
