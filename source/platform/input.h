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

#ifndef INPUT_H
#define INPUT_H

#include <stdbool.h>

enum input_button {
	IB_FORWARD,
	IB_BACKWARD,
	IB_LEFT,
	IB_RIGHT,
	IB_ACTION1,
	IB_ACTION2,
	IB_JUMP,
	IB_SNEAK,
	IB_INVENTORY,
	IB_HOME,
	IB_SCROLL_LEFT,
	IB_SCROLL_RIGHT,
	IB_GUI_UP,
	IB_GUI_DOWN,
	IB_GUI_LEFT,
	IB_GUI_RIGHT,
	IB_GUI_CLICK,
	IB_GUI_CLICK_ALT,
	IB_SCREENSHOT,
	IB_MAP,
	IB_TOGGLE_CREATIVE,
	// Advance the creative inventory to the next page (wraps). Lets the
	// all-blocks grid be paged from the keyboard so the demo rig can show every
	// page; ignored outside the creative inventory screen.
	IB_CREATIVE_PAGE,
	IB_COUNT,
};

enum input_category {
	INPUT_CAT_WIIMOTE,
	INPUT_CAT_NUNCHUK,
	INPUT_CAT_CLASSIC_CONTROLLER,
	INPUT_CAT_NONE,
};

void input_init(void);
void input_poll(void);

bool input_symbol(enum input_button b, int* symbol, int* symbol_help,
				  enum input_category* category);
bool input_pressed(enum input_button b);
bool input_released(enum input_button b);
bool input_held(enum input_button b);
bool input_joystick(float dt, float* x, float* y);
void input_pointer_enable(bool enable);
// Re-establish the gameplay cursor lock after a window event (resize/maximize/
// focus-gain) drops the pointer grab. No-op on Wii and while in menus.
void input_pointer_reassert(void);
bool input_pointer(float* x, float* y, float* angle);

#ifdef PLATFORM_PC
// fed by the GLFW scroll callback; mouse wheel drives hotbar switching
void input_native_scroll(double yoffset);

// Virtual-input (demo-replay) hook — PC dev rig only, see demo_input.h.
//
// When a virtual source is installed the IB-level queries (input_held /
// input_pressed / input_released) and the look delta (input_joystick) return
// the scripted state instead of real hardware input. NULL (the default) means
// normal hardware input — gameplay is byte-identical to a build without the rig.
//
// step_tick() is called once per 20 Hz game tick to advance the source;
// at_end() then reports when the script has finished (the rig quits the game).
struct input_virtual_source {
	void (*step_tick)(struct input_virtual_source* self, int tick);
	bool (*get_button)(struct input_virtual_source* self, enum input_button b);
	void (*get_look)(struct input_virtual_source* self, float* dx, float* dy);
	bool (*at_end)(struct input_virtual_source* self);
};

// Install (or, with NULL, remove) the active virtual source.
void input_set_virtual_source(struct input_virtual_source* src);
// The currently installed source, or NULL for hardware input.
struct input_virtual_source* input_get_virtual_source(void);
// Advance the active source by one tick (no-op when none is installed).
void input_virtual_step_tick(int tick);
// True when a virtual source is installed and its script has finished.
bool input_virtual_at_end(void);
#endif

#endif
