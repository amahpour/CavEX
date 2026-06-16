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

// Demo-replay action source (PC dev rig only, env-gated).
//
// A deterministic, tick-stamped text script drives the real input path so PR
// proof can show genuine, reproducible gameplay across many frames instead of a
// single still. This is the file-replay implementation of the pluggable
// `input_virtual_source` interface (see input.h); a live/AI source plugs into
// the same interface (follow-up issue #67).
//
// Script format (whitespace-separated tokens, '#' starts a comment):
//
//   # walk forward, then look right while still walking
//   @0   FORWARD=1
//   @40  LOOK=+2.0,0.0
//   @80  FORWARD=0
//
// A line beginning with `@<tick>` opens a keyframe at that absolute 20 Hz tick.
// Remaining tokens set state that PERSISTS until a later keyframe changes it:
//   NAME=0|1        button held-state (FORWARD, BACKWARD, LEFT, RIGHT, JUMP,
//                   SNEAK, ACTION1, ACTION2, INVENTORY, ...)
//   LOOK=dx,dy      per-tick look delta in radians-ish units (the same units
//                   the camera applies from the joystick); held until changed,
//                   so set LOOK=0,0 to stop turning.
//
// The parser is a pure function (no I/O, no globals) so it can be unit-tested.

#ifndef DEMO_INPUT_H
#define DEMO_INPUT_H

#include <stdbool.h>
#include <stddef.h>

#include "input.h"

#define DEMO_MAX_KEYFRAMES 1024

// One resolved keyframe: the full input state in effect from `tick` onward
// (until the next keyframe). Keyframes are stored in ascending `tick` order.
struct demo_keyframe {
	int tick;
	bool buttons[IB_COUNT];
	float look_dx, look_dy;
};

struct demo_script {
	struct demo_keyframe frames[DEMO_MAX_KEYFRAMES];
	size_t count;
};

// Map a button name (case-insensitive, e.g. "FORWARD") to its IB_* index, or
// -1 if unknown. Pure helper, exposed for testing.
int demo_button_from_name(const char* name);

// Parse a whole script from text into `out`. Pure: no file or global access.
//
// Returns true on success. On a malformed line returns false and, when
// `err_line` is non-NULL, writes the 1-based line number of the offending line.
// Each keyframe inherits the previous keyframe's state, then applies its own
// tokens, so callers only need to specify what changes. Ticks must be
// non-negative and strictly increasing across keyframes.
bool demo_parse(const char* text, struct demo_script* out, int* err_line);

// Resolve the input state for an absolute tick: the most recent keyframe at or
// before `tick`. Before the first keyframe everything is neutral (no buttons,
// no look). Pure helper, exposed for testing.
void demo_state_at(const struct demo_script* s, int tick,
				   bool buttons_out[IB_COUNT], float* look_dx, float* look_dy);

#ifdef PLATFORM_PC
// Build a file-replay virtual source from CAVEX_DEMO (a script path). Returns a
// pointer suitable for input_set_virtual_source(), or NULL if the env var is
// unset or the file cannot be read/parsed (in which case gameplay is unchanged).
// The returned source is owned by the module (static storage); do not free it.
struct input_virtual_source* demo_input_create_from_env(void);
#endif

#endif
