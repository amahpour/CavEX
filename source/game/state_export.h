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

// Game-state export for the live AI/heuristic action source (PC dev rig only,
// env-gated). Once the player is in the world and accepting input, the engine
// writes one small JSON line to stdout per game tick -- paired one-to-one with
// the action it then reads for that tick -- so an external driver can decide
// cheaply on text instead of pixels and feed actions back through the live
// input source (see demo_input.h, issue #67). No line is emitted while the menu
// or load screen is up (the player isn't driveable yet); a driver should skip
// the engine's non-JSON startup chatter until the first state line arrives.
//
// The serializer is split into a PURE part (state_export_write) that takes a
// plain snapshot struct and renders JSON into a caller buffer -- no globals, no
// I/O, no engine -- so it is unit-testable; and a thin PC-only collector
// (state_export_emit) that fills the snapshot from the live game state and
// prints it. Nothing here runs unless the agent env is set.

#ifndef STATE_EXPORT_H
#define STATE_EXPORT_H

#include <stdbool.h>
#include <stddef.h>

// Side length of the square local heightmap window centred on the player. Must
// be odd so the player sits in the middle cell. 5x5 = 25 ints is plenty for a
// cheap "walk toward higher ground" decision and keeps the JSON line small.
#define STATE_EXPORT_HEIGHT_WINDOW 5

// A flat, engine-free snapshot of everything the exporter serializes. The
// collector fills this from gstate; the pure writer only ever reads it.
struct game_export_state {
	int tick; // monotonic 20 Hz agent clock

	bool has_player; // false on menus / before the world loads
	double px, py, pz; // player position (world coords)
	double yaw, pitch; // orientation, radians (yaw = rx, pitch = ry)
	bool on_ground;
	bool flying;
	bool creative;

	const char* screen; // current screen name, e.g. "ingame" / "select_world"

	int hotbar_slot;			 // selected hotbar index, or -1 if unknown
	int hotbar_item;			 // item id in the selected slot, or -1 if empty
	int hotbar_count;			 // stack size in the selected slot, 0 if empty

	// Aimed block. The collector fills this from the engine's last raycast,
	// which is computed during rendering -- so it trails the player's current
	// orientation by one frame. Fine for the heightmap-driven reference policy;
	// a driver that mines/places off `aim` should expect ~1 frame of latency.
	bool has_aim;				 // true when the player is aiming at a block
	int aim_x, aim_y, aim_z;	 // aimed block coords (valid iff has_aim)
	int aim_block;				 // aimed block type (valid iff has_aim)

	bool has_heightmap;			 // true when the height window was filled
	// Surface height per cell of the window, row-major, north-west origin; the
	// centre cell ([n/2][n/2]) is the player's column.
	int heightmap[STATE_EXPORT_HEIGHT_WINDOW * STATE_EXPORT_HEIGHT_WINDOW];
};

// Render `st` as a single-line JSON object into `buf` (NUL-terminated).
//
// Pure: reads only `st`, writes only `buf`. Returns the number of characters
// that WOULD be written (excluding the NUL), like snprintf -- so a caller can
// detect truncation (return value >= bufsz). Always NUL-terminates when
// bufsz > 0. Floating-point fields are emitted with fixed precision so the line
// is stable and compact.
int state_export_write(char* buf, size_t bufsz,
					   const struct game_export_state* st);

#ifdef PLATFORM_PC
// Gather the live game state for absolute agent tick `tick` and print one JSON
// line to stdout (flushed). PC dev rig only; the caller must already have
// decided the agent is active. No-op-safe to call without the world loaded
// (emits has_player=false). Implemented in main-side glue so it can read gstate.
void state_export_emit(int tick);
#endif

#endif
