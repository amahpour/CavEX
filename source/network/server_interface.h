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

#ifndef SERVER_INTERFACE_H
#define SERVER_INTERFACE_H

#include "../world.h"
#include <m-lib/m-string.h>
#include <stdbool.h>

enum server_rpc_type {
	SRPC_PLAYER_POS,
	SRPC_LOAD_WORLD,
	SRPC_UNLOAD_WORLD,
	SRPC_HOTBAR_SLOT,
	SRPC_BLOCK_PLACE,
	SRPC_BLOCK_DIG,
	SRPC_WINDOW_CLICK,
	SRPC_WINDOW_CLOSE,
	SRPC_SET_GAMEMODE,
	SRPC_CREATIVE_PICK_BLOCK,
};

struct server_rpc {
	enum server_rpc_type type;
	union {
		struct {
			double x, y, z;
			float rx, ry;
		} player_pos;
		struct {
			string_t name;
		} load_world;
		struct {
			w_coord_t x, y, z;
			enum side side;
		} block_place;
		struct {
			bool finished;
			w_coord_t x, y, z;
			enum side side;
		} block_dig;
		struct {
			size_t slot;
		} hotbar_slot;
		struct {
			uint8_t window;
			uint8_t slot;
			bool right_click;
			uint16_t action_id;
		} window_click;
		struct {
			uint8_t window;
		} window_close;
		struct {
			// true  -> toggle the server's creative flag (echoed back via
			//          CRPC_GAMEMODE)
			// false -> just request the current state be (re)sent
			bool toggle;
		} set_gamemode;
		struct {
			// Block id (1..255) the creative inventory wants a full stack of in
			// the currently selected hotbar slot. Honoured only while the
			// server's creative flag is set.
			uint16_t block_id;
		} creative_pick_block;
	} payload;
};

void svin_init(void);
void svin_process_messages(void (*process)(struct server_rpc*, void*),
						   void* user, bool block);
void svin_rpc_send(struct server_rpc* call);

#endif
