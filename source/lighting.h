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

#ifndef LIGHTING_H
#define LIGHTING_H

#include "world.h"

struct world_modification_entry {
	w_coord_t x, y, z;
	struct block_data blk;
};

void lighting_heightmap_update(uint8_t* heightmap, c_coord_t x, w_coord_t y,
							   c_coord_t z, uint8_t type,
							   bool (*get_block)(void* user, c_coord_t x,
												 w_coord_t y, c_coord_t z,
												 struct block_data* blk),
							   void* user);

// The flood-fill scratch queue. One per OWNER, created once and reused for
// every update: the server thread and the client thread each keep their own,
// so nothing is shared across threads. It used to be malloc'd, grown and freed
// on every single block change, which fragmented the Wii's 24 MB MEM1 until a
// realloc failed (see stack_push). Sized so a normal cascade never grows it.
void lighting_queue_create(struct stack* queue);
void lighting_queue_destroy(struct stack* queue);

void lighting_update_at_block(
	struct world_modification_entry source, bool ignore_sky_light,
	bool (*get_block)(void* user, w_coord_t x, w_coord_t y, w_coord_t z,
					  struct block_data* blk, uint8_t* height),
	void (*set_light)(void* user, w_coord_t x, w_coord_t y, w_coord_t z,
					  uint8_t light),
	void* user, struct stack* queue);

#endif
