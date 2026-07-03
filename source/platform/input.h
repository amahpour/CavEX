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
	// Discrete look buttons. Player 1 looks with the mouse, so these stay unbound
	// for device 0; the local split-screen player 2 (keyboard-only, no second
	// mouse) steers the camera with them. input_joystick_dev() synthesises a look
	// delta from them for devices >= 1.
	IB_LOOK_UP,
	IB_LOOK_DOWN,
	IB_LOOK_LEFT,
	IB_LOOK_RIGHT,
	IB_COUNT,
};

// Number of local input devices (split-screen players). Device 0 is the original
// single-player path; device 1 is local player 2.
#define INPUT_MAX_DEVICES 2

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

// Pure mapping (button, device) -> config JSON key, or NULL when the button is
// unbound for that device. device 0 == the original single-player bindings;
// device 1 == "player2_*". Implemented in input_config.c with no platform deps so
// it is unit-testable (see tests/test_input_routing.c).
const char* input_config_key(enum input_button b, int device);

// Device-indexed input queries. The non-_dev variants above are exact aliases for
// device 0, so every existing single-player caller is unchanged. Local split-
// screen routes player 2's entity/camera through device 1.
bool input_pressed_dev(enum input_button b, int device);
bool input_released_dev(enum input_button b, int device);
bool input_held_dev(enum input_button b, int device);
bool input_joystick_dev(float dt, float* x, float* y, int device);
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

// Install (or, with NULL, remove) the active virtual source for device 0.
void input_set_virtual_source(struct input_virtual_source* src);
// The currently installed source for device 0, or NULL for hardware input.
struct input_virtual_source* input_get_virtual_source(void);
// Advance every installed source by one tick (no-op for devices with none).
void input_virtual_step_tick(int tick);
// True when device 0's source is installed and its script has finished. Device 0
// is the primary; the dual-player demo makes both scripts the same length so the
// run ends when the primary does.
bool input_virtual_at_end(void);

// Per-device virtual source, for the local split-screen demo (one scripted source
// per player). device 0's _dev calls are equivalent to the non-_dev ones above.
void input_set_virtual_source_dev(int device, struct input_virtual_source* src);
struct input_virtual_source* input_get_virtual_source_dev(int device);
// True when ANY installed source has finished -- the dual demo stops as soon as
// either player's script is exhausted.
bool input_virtual_any_at_end(void);
#endif

#endif
