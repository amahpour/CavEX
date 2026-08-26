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

#include <assert.h>

#include "../cglm/cglm.h"
#include "gfx.h"
#include "input.h"

#ifdef PLATFORM_PC

#include <GLFW/glfw3.h>

extern GLFWwindow* window;

static bool input_pointer_enabled;
static double input_old_pointer_x, input_old_pointer_y;
static bool input_key_held[1024];

// Virtual key codes for the mouse wheel (config_pc.json binds scroll to these).
// GLFW scroll events are momentary, so the callback accumulates notches here
// and input_native_key_status reports each as a one-shot "pressed" edge.
#define KEY_WHEEL_UP 2000
#define KEY_WHEEL_DOWN 2001
static int input_wheel_up, input_wheel_down;

// USB gamepad support (SNES-style pads etc.). config_pc.json binds actions to
// synthetic codes read from GLFW joysticks; the joystick index is the input
// DEVICE, so player 1 uses joystick 0 and player 2 uses joystick 1 with no
// other plumbing. A pad has one d-pad (2 axes) + buttons, so movement uses the
// axes and the face buttons drive the look (see input_joystick_dev).
//   3000 + n           -> joystick button n
//   3100 + 2*axis + d  -> joystick axis `axis`, d=0 negative / d=1 positive half
#define PAD_BUTTON_BASE 3000
#define PAD_AXIS_BASE 3100
#define PAD_AXIS_DEADZONE 0.5F
// Edge state per (device, code) so polled pad input yields pressed/released like
// the keyboard. Indexed by (code - PAD_BUTTON_BASE), covering buttons 0..99 and
// axis half-codes 100..127.
static bool pad_held[INPUT_MAX_DEVICES][128];

// Report a synthetic pad code for `device`'s joystick, with keyboard-style edges.
static void input_pad_status(int device, int key, bool* pressed, bool* released,
							 bool* held) {
	*pressed = *released = *held = false;
	if(device < 0 || device >= INPUT_MAX_DEVICES)
		return;

	int jid = GLFW_JOYSTICK_1 + device;
	if(!glfwJoystickPresent(jid))
		return;

	bool state = false;
	if(key >= PAD_AXIS_BASE) {
		int m = key - PAD_AXIS_BASE;
		int axis = m >> 1, dir = m & 1;
		int n = 0;
		const float* ax = glfwGetJoystickAxes(jid, &n);
		if(ax && axis < n)
			state = dir ? (ax[axis] > PAD_AXIS_DEADZONE)
						: (ax[axis] < -PAD_AXIS_DEADZONE);
	} else {
		int b = key - PAD_BUTTON_BASE;
		int n = 0;
		const unsigned char* btns = glfwGetJoystickButtons(jid, &n);
		if(btns && b >= 0 && b < n)
			state = btns[b] == GLFW_PRESS;
	}

	int idx = key - PAD_BUTTON_BASE;
	if(idx < 0 || idx >= 128) {
		*held = state;
		return;
	}
	bool was = pad_held[device][idx];
	*pressed = state && !was;
	*released = !state && was;
	*held = state && was;
	pad_held[device][idx] = state;
}

// Non-consuming: is this pad code physically active right now? Reads raw GLFW
// state WITHOUT touching pad_held, so it can be polled for the L+R hotbar chord
// without disturbing the pressed/released edges the normal action path relies on.
static bool input_pad_down(int device, int code) {
	if(device < 0 || device >= INPUT_MAX_DEVICES)
		return false;
	int jid = GLFW_JOYSTICK_1 + device;
	if(!glfwJoystickPresent(jid))
		return false;

	if(code >= PAD_AXIS_BASE) {
		int m = code - PAD_AXIS_BASE, axis = m >> 1, dir = m & 1, n = 0;
		const float* ax = glfwGetJoystickAxes(jid, &n);
		return ax && axis < n
			&& (dir ? ax[axis] > PAD_AXIS_DEADZONE
					: ax[axis] < -PAD_AXIS_DEADZONE);
	}

	int b = code - PAD_BUTTON_BASE, n = 0;
	const unsigned char* btns = glfwGetJoystickButtons(jid, &n);
	return btns && b >= 0 && b < n && btns[b] == GLFW_PRESS;
}

void input_native_scroll(double yoffset) {
	if(yoffset > 0)
		input_wheel_up++;
	else if(yoffset < 0)
		input_wheel_down++;
}

void input_init() {
	for(int k = 0; k < 1024; k++)
		input_key_held[k] = false;

	for(int d = 0; d < INPUT_MAX_DEVICES; d++)
		for(int k = 0; k < 128; k++)
			pad_held[d][k] = false;

	input_pointer_enabled = false;
	input_old_pointer_x = 0;
	input_old_pointer_y = 0;
	input_wheel_up = input_wheel_down = 0;
}

void input_poll() { }

void input_native_key_status(int key, bool* pressed, bool* released,
							 bool* held) {
	if(key == KEY_WHEEL_UP || key == KEY_WHEEL_DOWN) {
		int* w = (key == KEY_WHEEL_UP) ? &input_wheel_up : &input_wheel_down;
		*pressed = *w > 0;	// consumed once per accumulated notch
		*released = false;
		*held = false;
		if(*w > 0)
			(*w)--;
		return;
	}

	if(key >= 1024) {
		*pressed = false;
		*released = false;
		*held = false;
		return;
	}

	int state = key < 1000 ? glfwGetKey(window, key) :
							 glfwGetMouseButton(window, key - 1000);

	*pressed = (state == GLFW_PRESS) && !input_key_held[key];
	*released = (state == GLFW_RELEASE) && input_key_held[key];
	*held = !(*released) && input_key_held[key];

	if(state == GLFW_PRESS)
		input_key_held[key] = true;

	if(state == GLFW_RELEASE)
		input_key_held[key] = false;
}

bool input_native_key_symbol(int key, int* symbol, int* symbol_help,
							 enum input_category* category, int* priority) {
	*category = INPUT_CAT_NONE;
	*symbol = 7;
	*symbol_help = 7;
	*priority = 1;
	return true;
}

bool input_native_key_any(int* key) {
	return false;
}

void input_pointer_enable(bool enable) {
	glfwSetInputMode(window, GLFW_CURSOR,
					 enable ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_DISABLED);

	if(!input_pointer_enabled && enable)
		glfwSetCursorPos(window, gfx_width() / 2, gfx_height() / 2);

	if(input_pointer_enabled && !enable)
		glfwGetCursorPos(window, &input_old_pointer_x, &input_old_pointer_y);

	input_pointer_enabled = enable;
}

void input_pointer_reassert(void) {
	// Only re-grab during gameplay (cursor disabled); leave the menu cursor
	// visible. On X11/XWayland a window resize/maximize/focus-change drops the
	// pointer grab and the cursor escapes. A plain re-set to GLFW_CURSOR_DISABLED
	// is a no-op when GLFW still thinks the cursor is disabled, so toggle through
	// GLFW_CURSOR_NORMAL to force the underlying grab back.
	if(!input_pointer_enabled) {
		glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
		glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
	}
}

bool input_pointer(float* x, float* y, float* angle) {
	double x2, y2;
	glfwGetCursorPos(window, &x2, &y2);
	*x = x2;
	*y = y2;
	*angle = 0.0F;
	return input_pointer_enabled && x2 >= 0 && y2 >= 0 && x2 < gfx_width()
		&& y2 < gfx_height();
}

void input_native_joystick(float dt, float* dx, float* dy) {
	if(!input_pointer_enabled) {
		double x2, y2;
		glfwGetCursorPos(window, &x2, &y2);
		*dx = (x2 - input_old_pointer_x) * 0.001F;
		*dy = -(y2 - input_old_pointer_y) * 0.001F;
		input_old_pointer_x = x2;
		input_old_pointer_y = y2;
	} else {
		*dx = 0.0F;
		*dy = 0.0F;
	}
}

// PC: keyboard/mouse devices are told apart by their config key set (player2_*),
// so the native layer ignores the device index for those. Gamepad codes DO use
// the device — it selects the joystick — so player 1 reads joystick 0 and
// player 2 joystick 1. The _dev shape also lets the shared query code route the
// WPAD channel on Wii.
static void input_native_key_status_dev(int key, int device, bool* pressed,
										bool* released, bool* held) {
	if(key >= PAD_BUTTON_BASE) {
		input_pad_status(device, key, pressed, released, held);
		return;
	}
	(void)device;
	input_native_key_status(key, pressed, released, held);
}

#endif

#ifdef PLATFORM_WII

#include <wiiuse/wpad.h>

// Per-CHANNEL Wiimote state (issue #140): channel 0 drives local player 1,
// channel 1 drives split-screen player 2. Both read the SAME config bindings;
// the device index selects the WPAD channel instead of a key-name set.
#define WPAD_LOCAL_CHANNELS 2

static struct {
	float dx, dy;
	float magnitude;
	bool available;
} joystick_input[WPAD_LOCAL_CHANNELS][3];

static bool js_emulated_btns_prev[WPAD_LOCAL_CHANNELS][3][4];
static bool js_emulated_btns_held[WPAD_LOCAL_CHANNELS][3][4];

void input_init() {
	WPAD_Init();
	for(int ch = 0; ch < WPAD_LOCAL_CHANNELS; ch++) {
		WPAD_SetDataFormat(ch, WPAD_FMT_BTNS_ACC_IR);
		WPAD_SetVRes(ch, gfx_window_width(), gfx_window_height());
	}

	for(int ch = 0; ch < WPAD_LOCAL_CHANNELS; ch++)
		for(int k = 0; k < 4; k++)
			for(int j = 0; j < 3; j++)
				js_emulated_btns_prev[ch][j][k]
					= js_emulated_btns_held[ch][j][k] = false;
}

void input_poll() {
	WPAD_ScanPads();

	for(int ch = 0; ch < WPAD_LOCAL_CHANNELS; ch++) {
		expansion_t e;
		WPAD_Expansion(ch, &e);

		if(e.type == WPAD_EXP_NUNCHUK) {
			joystick_input[ch][0].dx = sin(glm_rad(e.nunchuk.js.ang));
			joystick_input[ch][0].dy = cos(glm_rad(e.nunchuk.js.ang));
			joystick_input[ch][0].magnitude = e.nunchuk.js.mag;
			joystick_input[ch][0].available = true;
		} else {
			joystick_input[ch][0].available = false;
		}

		if(e.type == WPAD_EXP_CLASSIC) {
			joystick_input[ch][1].dx = sin(glm_rad(e.classic.ljs.ang));
			joystick_input[ch][1].dy = cos(glm_rad(e.classic.ljs.ang));
			joystick_input[ch][1].magnitude = e.classic.ljs.mag;
			joystick_input[ch][1].available = true;

			joystick_input[ch][2].dx = sin(glm_rad(e.classic.rjs.ang));
			joystick_input[ch][2].dy = cos(glm_rad(e.classic.rjs.ang));
			joystick_input[ch][2].magnitude = e.classic.rjs.mag;
			joystick_input[ch][2].available = true;
		} else {
			joystick_input[ch][1].available = joystick_input[ch][2].available
				= false;
		}

		for(int j = 0; j < 3; j++) {
			for(int k = 0; k < 4; k++) {
				js_emulated_btns_prev[ch][j][k]
					= js_emulated_btns_held[ch][j][k];
				js_emulated_btns_held[ch][j][k] = false;
			}

			if(joystick_input[ch][j].available) {
				float x = joystick_input[ch][j].dx
					* joystick_input[ch][j].magnitude;
				float y = joystick_input[ch][j].dy
					* joystick_input[ch][j].magnitude;

				if(x > 0.2F) {
					js_emulated_btns_held[ch][j][3] = true;
				} else if(x < -0.2F) {
					js_emulated_btns_held[ch][j][2] = true;
				}

				if(y > 0.2F) {
					js_emulated_btns_held[ch][j][0] = true;
				} else if(y < -0.2F) {
					js_emulated_btns_held[ch][j][1] = true;
				}
			}
		}
	}
}

static uint32_t input_wpad_translate(int key, int chan) {
	switch(key) {
		case 0: return WPAD_BUTTON_UP;
		case 1: return WPAD_BUTTON_DOWN;
		case 2: return WPAD_BUTTON_LEFT;
		case 3: return WPAD_BUTTON_RIGHT;
		case 4: return WPAD_BUTTON_A;
		case 5: return WPAD_BUTTON_B;
		case 6: return WPAD_BUTTON_1;
		case 7: return WPAD_BUTTON_2;
		case 8: return WPAD_BUTTON_PLUS;
		case 9: return WPAD_BUTTON_MINUS;
		case 10: return WPAD_BUTTON_HOME;
		default: break;
	}

	expansion_t e;
	WPAD_Expansion(chan, &e);

	if(e.type == WPAD_EXP_NUNCHUK) {
		switch(key) {
			case 100: return WPAD_NUNCHUK_BUTTON_Z;
			case 101: return WPAD_NUNCHUK_BUTTON_C;
			default: break;
		}
	} else if(e.type == WPAD_EXP_CLASSIC) {
		switch(key) {
			case 200: return WPAD_CLASSIC_BUTTON_UP;
			case 201: return WPAD_CLASSIC_BUTTON_DOWN;
			case 202: return WPAD_CLASSIC_BUTTON_LEFT;
			case 203: return WPAD_CLASSIC_BUTTON_RIGHT;
			case 204: return WPAD_CLASSIC_BUTTON_A;
			case 205: return WPAD_CLASSIC_BUTTON_B;
			case 206: return WPAD_CLASSIC_BUTTON_X;
			case 207: return WPAD_CLASSIC_BUTTON_Y;
			case 208: return WPAD_CLASSIC_BUTTON_ZL;
			case 209: return WPAD_CLASSIC_BUTTON_ZR;
			case 210: return WPAD_CLASSIC_BUTTON_FULL_L;
			case 211: return WPAD_CLASSIC_BUTTON_FULL_R;
			case 212: return WPAD_CLASSIC_BUTTON_PLUS;
			case 213: return WPAD_CLASSIC_BUTTON_MINUS;
			case 214: return WPAD_CLASSIC_BUTTON_HOME;
			default: break;
		}
	} else if(e.type == WPAD_EXP_GUITARHERO3) {
		switch(key) {
			case 300: return WPAD_GUITAR_HERO_3_BUTTON_YELLOW;
			case 301: return WPAD_GUITAR_HERO_3_BUTTON_GREEN;
			case 302: return WPAD_GUITAR_HERO_3_BUTTON_BLUE;
			case 303: return WPAD_GUITAR_HERO_3_BUTTON_RED;
			case 304: return WPAD_GUITAR_HERO_3_BUTTON_ORANGE;
			case 305: return WPAD_GUITAR_HERO_3_BUTTON_PLUS;
			case 306: return WPAD_GUITAR_HERO_3_BUTTON_MINUS;
			case 307: return WPAD_GUITAR_HERO_3_BUTTON_STRUM_UP;
			case 308: return WPAD_GUITAR_HERO_3_BUTTON_STRUM_DOWN;
			default: break;
		}
	}

	return 0;
}

// Channel-aware button state (issue #140): device N reads WPAD channel N with
// the SAME config bindings — that is the whole Wii device-routing model.
static void input_native_key_status_dev(int key, int chan, bool* pressed,
										bool* released, bool* held) {
	if(chan < 0 || chan >= WPAD_LOCAL_CHANNELS)
		chan = 0;

	if(key >= 900 && key < 924) {
		int js = (key - 900) / 10;
		int offset = (key - 900) % 10;
		if(offset < 4) {
			*held = js_emulated_btns_held[chan][js][offset]
				&& js_emulated_btns_prev[chan][js][offset];
			*pressed = js_emulated_btns_held[chan][js][offset]
				&& !js_emulated_btns_prev[chan][js][offset];
			*released = !js_emulated_btns_held[chan][js][offset]
				&& js_emulated_btns_prev[chan][js][offset];
			return;
		}
	}

	uint32_t mask = input_wpad_translate(key, chan);
	*pressed = WPAD_ButtonsDown(chan) & mask;
	*released = WPAD_ButtonsUp(chan) & mask;
	*held = !(*pressed) && !(*released) && (WPAD_ButtonsHeld(chan) & mask);
}

void input_native_key_status(int key, bool* pressed, bool* released,
							 bool* held) {
	input_native_key_status_dev(key, 0, pressed, released, held);
}

// Re-issue the IR resolution after gfx_setup has picked the real screen
// aspect: input_init runs first and latches the 802-wide default, which on a
// 4:3 console (640 gfx units) would skew every IR position ~25% right.
void input_ir_vres_update(void) {
	for(int chan = 0; chan < WPAD_LOCAL_CHANNELS; chan++)
		WPAD_SetVRes(chan, gfx_window_width(), gfx_window_height());
}

bool input_native_key_symbol(int key, int* symbol, int* symbol_help,
							 enum input_category* category, int* priority) {
	if(key >= 900 && key < 904) {
		*symbol = *symbol_help = 17;
		*category = INPUT_CAT_NUNCHUK;
		*priority = 1;
		return true;
	}

	if(key >= 910 && key < 914) {
		*symbol = *symbol_help = 18;
		*category = INPUT_CAT_CLASSIC_CONTROLLER;
		*priority = 1;
		return true;
	}

	if(key >= 920 && key < 924) {
		*symbol = *symbol_help = 19;
		*category = INPUT_CAT_CLASSIC_CONTROLLER;
		*priority = 1;
		return true;
	}

	if(key < 0 || key > 308)
		return false;

	int symbols[] = {
		[0] = 25,	[1] = 26,	[2] = 27,	[3] = 28,	[4] = 0,	[5] = 1,
		[6] = 2,	[7] = 3,	[8] = 5,	[9] = 6,	[10] = 4,	[100] = 8,
		[101] = 9,	[200] = 25, [201] = 26, [202] = 27, [203] = 28, [204] = 10,
		[205] = 11, [206] = 12, [207] = 13, [208] = 14, [209] = 15, [210] = 22,
		[211] = 23, [212] = 5,	[213] = 6,	[214] = 4,	[300] = 7,	[301] = 7,
		[302] = 7,	[303] = 7,	[304] = 7,	[305] = 5,	[306] = 6,	[307] = 7,
		[308] = 7,
	};

	*category = INPUT_CAT_NONE;

	if(key >= 0 && key <= 10)
		*category = INPUT_CAT_WIIMOTE;

	if(key >= 100 && key <= 101)
		*category = INPUT_CAT_NUNCHUK;

	if(key >= 200 && key <= 214)
		*category = INPUT_CAT_CLASSIC_CONTROLLER;

	*symbol = symbols[key];
	*symbol_help = symbols[key];

	if(*symbol_help >= 25 && *symbol_help <= 28)
		*symbol_help = 24;

	expansion_t e;
	WPAD_Expansion(WPAD_CHAN_0, &e);

	if((*category == INPUT_CAT_NUNCHUK && e.type == WPAD_EXP_NUNCHUK)
	   || (*category == INPUT_CAT_CLASSIC_CONTROLLER
		   && e.type == WPAD_EXP_CLASSIC)) {
		*priority = 2;
	} else {
		*priority = 1;
	}

	return true;
}

bool input_native_key_any(int* key) {
	return false;
}

// temporary diagnostics: raw WPAD expansion state for the on-screen debug line
#include <stdio.h>
void input_debug_wpad(char* dst, size_t len) {
	expansion_t e;
	WPAD_Expansion(WPAD_CHAN_0, &e);
	u32 held = WPAD_ButtonsHeld(WPAD_CHAN_0);
	int t = e.type;
	int mag = -1, ang = -1, px = -1, py = -1;
	if(t == WPAD_EXP_NUNCHUK) {
		mag = (int)(e.nunchuk.js.mag * 100.0F);
		ang = (int)e.nunchuk.js.ang;
		px = e.nunchuk.js.pos.x;
		py = e.nunchuk.js.pos.y;
	} else if(t == WPAD_EXP_CLASSIC) {
		mag = (int)(e.classic.rjs.mag * 100.0F);
		ang = (int)e.classic.rjs.ang;
		px = e.classic.rjs.pos.x;
		py = e.classic.rjs.pos.y;
	}
	snprintf(dst, len, "ext=%d mag=%d ang=%d pos=%d,%d held=%08x", t, mag, ang,
			 px, py, (unsigned)held);
}

void input_pointer_enable(bool enable) { }

void input_pointer_reassert(void) { }

bool input_pointer(float* x, float* y, float* angle) {
	struct ir_t ir;
	WPAD_IR(WPAD_CHAN_0, &ir);
	*x = ir.x;
	*y = ir.y;
	*angle = ir.angle;
	return ir.valid;
}

// Per-channel camera input (issue #140): the channel's own extension stick if
// one is live, else that channel's IR edge-pan fallback.
static void input_native_joystick_dev(float dt, float* dx, float* dy,
									  int chan) {
	if(chan < 0 || chan >= WPAD_LOCAL_CHANNELS)
		chan = 0;

	if(joystick_input[chan][0].available
	   && joystick_input[chan][0].magnitude > 0.1F) {
		*dx = joystick_input[chan][0].dx * joystick_input[chan][0].magnitude
			* dt;
		*dy = joystick_input[chan][0].dy * joystick_input[chan][0].magnitude
			* dt;
	} else if(joystick_input[chan][2].available
			  && joystick_input[chan][2].magnitude > 0.1F) {
		*dx = joystick_input[chan][2].dx * joystick_input[chan][2].magnitude
			* dt;
		*dy = joystick_input[chan][2].dy * joystick_input[chan][2].magnitude
			* dt;
	} else {
		// Fallback when no extension stick is available (e.g. Dolphin's
		// emulated Wiimote, whose Nunchuk/Classic never complete the WPAD
		// handshake): steer the camera with the IR pointer instead. Pushing
		// the pointer outside the middle of the screen pans the view toward
		// it, scaled by how far it is from center; the inner zone is neutral
		// so the view doesn't drift while aiming.
		struct ir_t ir;
		WPAD_IR(chan, &ir);
		*dx = 0.0F;
		*dy = 0.0F;
		// Hold the view still while mining: CavEX resets dig progress whenever
		// the targeted cell changes, so even slight edge-pan drift would keep
		// restarting the dig and blocks would never break.
		if(ir.valid && !input_held_dev(IB_ACTION1, chan)) {
			float nx = ir.x / (ir.vres[0] ? ir.vres[0] : 640) * 2.0F - 1.0F;
			float ny = ir.y / (ir.vres[1] ? ir.vres[1] : 480) * 2.0F - 1.0F;
			float dead = 0.15F;
			float speed = 2.0F;
			float ax = fabsf(nx), ay = fabsf(ny);
			if(ax > dead)
				*dx = (nx > 0 ? 1 : -1) * (ax - dead) / (1.0F - dead) * speed * dt;
			if(ay > dead)
				*dy = -(ny > 0 ? 1 : -1) * (ay - dead) / (1.0F - dead) * speed * dt;
		}
	}
}

void input_native_joystick(float dt, float* dx, float* dy) {
	input_native_joystick_dev(dt, dx, dy, 0);
}

#endif

#include "../game/game_state.h"

#ifdef PLATFORM_PC
// Virtual-input (demo-replay) sources, one per local device. NULL = normal
// hardware input for that device, so when no demo is loaded every query below
// falls through to the real input path and behaviour is identical to a build
// without the rig. Device 0 is the primary (single-player demo); device 1 drives
// local split-screen player 2 in the dual-player demo. See input.h/demo_input.h.
static struct input_virtual_source* input_virtual_src[INPUT_MAX_DEVICES];

// Button state latched at the last tick boundary so that input_pressed /
// input_released report exactly one edge per tick transition, independent of
// how many frames (and thus query calls) fall within a tick. Indexed by device.
static bool input_virtual_cur[INPUT_MAX_DEVICES][IB_COUNT];
static bool input_virtual_prev[INPUT_MAX_DEVICES][IB_COUNT];

// The 20 Hz accumulator (main.c) can step the virtual source SEVERAL times per
// rendered frame, but the consumers run once per frame: input_pressed() in the
// screen update, input_joystick()/get_look() in the camera. Deriving the edge
// live from cur/prev (rolled every tick) therefore loses any press whose tick is
// not the last in its frame, and reading the source's per-tick look once per
// frame loses every earlier tick's delta. So we LATCH edges and ACCUMULATE look
// across all ticks stepped within a frame; the consumers drain the latch once.
static bool input_virtual_pending_press[INPUT_MAX_DEVICES][IB_COUNT];
static bool input_virtual_pending_release[INPUT_MAX_DEVICES][IB_COUNT];
static float input_virtual_look_dx[INPUT_MAX_DEVICES];
static float input_virtual_look_dy[INPUT_MAX_DEVICES];

void input_set_virtual_source_dev(int device, struct input_virtual_source* src) {
	if(device < 0 || device >= INPUT_MAX_DEVICES)
		return;

	input_virtual_src[device] = src;
	for(int b = 0; b < IB_COUNT; b++) {
		input_virtual_cur[device][b] = input_virtual_prev[device][b] = false;
		input_virtual_pending_press[device][b] = false;
		input_virtual_pending_release[device][b] = false;
	}
	input_virtual_look_dx[device] = input_virtual_look_dy[device] = 0.0F;
}

void input_set_virtual_source(struct input_virtual_source* src) {
	input_set_virtual_source_dev(0, src);
}

struct input_virtual_source* input_get_virtual_source_dev(int device) {
	if(device < 0 || device >= INPUT_MAX_DEVICES)
		return NULL;
	return input_virtual_src[device];
}

struct input_virtual_source* input_get_virtual_source(void) {
	return input_get_virtual_source_dev(0);
}

void input_virtual_step_tick(int tick) {
	for(int d = 0; d < INPUT_MAX_DEVICES; d++) {
		struct input_virtual_source* src = input_virtual_src[d];
		if(!src)
			continue;

		if(src->step_tick)
			src->step_tick(src, tick);

		// Snapshot the per-tick button levels and roll the previous set forward,
		// then LATCH any edge into a pending flag that survives until a consumer
		// drains it -- so a press/release on a non-final tick of a multi-tick
		// frame is not lost.
		for(int b = 0; b < IB_COUNT; b++) {
			input_virtual_prev[d][b] = input_virtual_cur[d][b];
			input_virtual_cur[d][b] = src->get_button
				? src->get_button(src, (enum input_button)b)
				: false;
			if(input_virtual_cur[d][b] && !input_virtual_prev[d][b])
				input_virtual_pending_press[d][b] = true;
			if(!input_virtual_cur[d][b] && input_virtual_prev[d][b])
				input_virtual_pending_release[d][b] = true;
		}

		// Drain this tick's look delta and ACCUMULATE it; input_joystick_dev()
		// applies (and clears) the sum once per frame. Summing means every stepped
		// tick's rotation reaches the camera even when several ticks share a frame.
		if(src->get_look) {
			float dx = 0.0F, dy = 0.0F;
			src->get_look(src, &dx, &dy);
			input_virtual_look_dx[d] += dx;
			input_virtual_look_dy[d] += dy;
		}
	}
}

bool input_virtual_at_end(void) {
	struct input_virtual_source* src = input_virtual_src[0];
	return src && src->at_end && src->at_end(src);
}

bool input_virtual_any_at_end(void) {
	for(int d = 0; d < INPUT_MAX_DEVICES; d++) {
		struct input_virtual_source* src = input_virtual_src[d];
		if(src && src->at_end && src->at_end(src))
			return true;
	}
	return false;
}
#endif

bool input_symbol(enum input_button b, int* symbol, int* symbol_help,
				  enum input_category* category) {
	const char* key = input_config_key(b, 0);

	if(!key)
		return false;

	size_t length = 8;
	int mapping[length];

	if(!config_read_int_array(&gstate.config_user, key, mapping, &length))
		return false;

	int priority = 0;
	bool has_any = false;

	for(size_t k = 0; k < length; k++) {
		int symbol_tmp, symbol_help_tmp, priority_tmp;
		enum input_category category_tmp;
		if(input_native_key_symbol(mapping[k], &symbol_tmp, &symbol_help_tmp,
								   &category_tmp, &priority_tmp)
		   && priority_tmp > priority) {
			priority = priority_tmp;
			*symbol = symbol_tmp;
			*symbol_help = symbol_help_tmp;
			*category = category_tmp;
			has_any = true;
		}
	}

	return has_any;
}

#ifdef PLATFORM_PC
// True if any PAD binding (code >= 3000) of `button` for `device` is physically
// down. Ignores keyboard/mouse codes, so the chord below is pad-only.
static bool input_pad_binding_down(enum input_button button, int device) {
	const char* key = input_config_key(button, device);
	if(!key)
		return false;

	size_t length = 8;
	int mapping[length];
	if(!config_read_int_array(&gstate.config_user, key, mapping, &length))
		return false;

	for(size_t k = 0; k < length; k++)
		if(mapping[k] >= 3000 && input_pad_down(device, mapping[k]))
			return true;
	return false;
}

bool input_gamepad_present(int device) {
	return device >= 0 && device < INPUT_MAX_DEVICES
		&& glfwJoystickPresent(GLFW_JOYSTICK_1 + device);
}

bool input_gamepad_shoulders(int device) {
	// Both shoulder buttons (the pad bindings for mine + place) held at once =
	// the L+R hotbar chord. Pad-only + non-consuming (see input_pad_down), so a
	// mouse LMB+RMB or the keyboard never triggers it.
	return input_pad_binding_down(IB_ACTION1, device)
		&& input_pad_binding_down(IB_ACTION2, device);
}
#else
bool input_gamepad_present(int device) {
	(void)device;
	return false;
}
bool input_gamepad_shoulders(int device) {
	(void)device;
	return false;
}
#endif

bool input_pressed_dev(enum input_button b, int device) {
#ifdef PLATFORM_PC
	if(device >= 0 && device < INPUT_MAX_DEVICES && input_virtual_src[device]) {
		// Drain the latched rising edge (set by input_virtual_step_tick for any
		// tick in this frame). Returning it once and clearing keeps a per-frame
		// consumer from firing the action repeatedly within the tick, and the
		// latch keeps an edge from a non-final tick of the frame alive until read.
		bool edge = input_virtual_pending_press[device][b];
		input_virtual_pending_press[device][b] = false;
		return edge;
	}
#endif

	const char* key = input_config_key(b, device);

	if(!key)
		return false;

	size_t length = 8;
	int mapping[length];

	if(!config_read_int_array(&gstate.config_user, key, mapping, &length))
		return false;

	bool any_pressed = false;
	bool any_held = false;
	bool any_released = false;

	for(size_t k = 0; k < length; k++) {
		bool pressed, released, held;
		input_native_key_status_dev(mapping[k], device, &pressed, &released,
									&held);
		if(pressed)
			any_pressed = true;
		if(released)
			any_released = true;
		if(held)
			any_held = true;
	}

	return any_pressed && !any_held && !any_released;
}

bool input_pressed(enum input_button b) {
	return input_pressed_dev(b, 0);
}

bool input_released_dev(enum input_button b, int device) {
#ifdef PLATFORM_PC
	if(device >= 0 && device < INPUT_MAX_DEVICES && input_virtual_src[device]) {
		// Drain the latched falling edge (see input_pressed_dev()).
		bool edge = input_virtual_pending_release[device][b];
		input_virtual_pending_release[device][b] = false;
		return edge;
	}
#endif

	const char* key = input_config_key(b, device);

	if(!key)
		return false;

	size_t length = 8;
	int mapping[length];

	if(!config_read_int_array(&gstate.config_user, key, mapping, &length))
		return false;

	bool any_pressed = false;
	bool any_held = false;
	bool any_released = false;

	for(size_t k = 0; k < length; k++) {
		bool pressed, released, held;
		input_native_key_status_dev(mapping[k], device, &pressed, &released,
									&held);
		if(pressed)
			any_pressed = true;
		if(released)
			any_released = true;
		if(held)
			any_held = true;
	}

	return !any_pressed && !any_held && any_released;
}

bool input_released(enum input_button b) {
	return input_released_dev(b, 0);
}

bool input_held_dev(enum input_button b, int device) {
#ifdef PLATFORM_PC
	if(device >= 0 && device < INPUT_MAX_DEVICES && input_virtual_src[device])
		return input_virtual_cur[device][b];
#endif

	const char* key = input_config_key(b, device);

	if(!key)
		return false;

	size_t length = 8;
	int mapping[length];

	if(!config_read_int_array(&gstate.config_user, key, mapping, &length))
		return false;

	bool any_pressed = false;
	bool any_held = false;

	for(size_t k = 0; k < length; k++) {
		bool pressed, released, held;
		input_native_key_status_dev(mapping[k], device, &pressed, &released,
									&held);
		if(pressed)
			any_pressed = true;
		if(held)
			any_held = true;
	}

	return any_pressed || any_held;
}

bool input_held(enum input_button b) {
	return input_held_dev(b, 0);
}

bool input_joystick_dev(float dt, float* x, float* y, int device) {
#ifdef PLATFORM_PC
	if(device >= 0 && device < INPUT_MAX_DEVICES && input_virtual_src[device]) {
		// Apply the look delta accumulated across every tick stepped since the
		// last camera poll, then clear it (drained once per frame).
		*x = input_virtual_look_dx[device];
		*y = input_virtual_look_dy[device];
		input_virtual_look_dx[device] = input_virtual_look_dy[device] = 0.0F;
		return true;
	}
#endif

	if(device == 0) {
		// Player 1 looks with the mouse (PC) or the joystick/IR (Wii).
		input_native_joystick(dt, x, y);
#ifdef PLATFORM_PC
		// PC: also fold in a gamepad's face-button look so player 1 can play on a
		// pad (added to the mouse delta, so mouse and pad both work). These keys
		// are empty for a keyboard+mouse player, so nothing changes there. Same
		// sign convention as player 2's look keys below.
		const float ls = 0.018F;
		if(input_held_dev(IB_LOOK_LEFT, 0))
			*x -= ls;
		if(input_held_dev(IB_LOOK_RIGHT, 0))
			*x += ls;
		if(input_held_dev(IB_LOOK_UP, 0))
			*y += ls;
		if(input_held_dev(IB_LOOK_DOWN, 0))
			*y -= ls;
#endif
		return true;
	}

#ifdef PLATFORM_WII
	// Wii: each further local player steers its camera with its own Wiimote
	// channel — extension stick, or its IR edge-pan fallback (issue #140).
	// Devices past the polled channels have no input source at all: say so
	// instead of silently clamping onto another player's Wiimote.
	if(device < WPAD_LOCAL_CHANNELS) {
		input_native_joystick_dev(dt, x, y, device);
		return true;
	}

	return false;
#else
	// PC devices >= 1 have no pointer of their own (one mouse, shared keyboard),
	// so the camera is steered with the discrete IB_LOOK_* keys. The magnitude is
	// a per-frame constant tuned to roughly match a comfortable mouse turn rate;
	// camera_attach() scales it by 2 like the mouse delta.
	const float look_speed = 0.018F;
	float lx = 0.0F, ly = 0.0F;

	// Sign convention must match player 1's mouse look (input_native_joystick):
	// dx>0 turns the view right, dy>0 turns it up (camera_physics applies
	// rx -= dx*2, ry -= dy*2). The horizontal pair used to be flipped, so
	// player 2's Left arrow looked right and vice-versa (issue #140 follow-up).
	if(input_held_dev(IB_LOOK_LEFT, device))
		lx -= look_speed;
	if(input_held_dev(IB_LOOK_RIGHT, device))
		lx += look_speed;
	if(input_held_dev(IB_LOOK_UP, device))
		ly += look_speed;
	if(input_held_dev(IB_LOOK_DOWN, device))
		ly -= look_speed;

	*x = lx;
	*y = ly;
	return true;
#endif
}

bool input_joystick(float dt, float* x, float* y) {
	return input_joystick_dev(dt, x, y, 0);
}
