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

#include <assert.h>

#include "block/blocks.h"
#include "lighting.h"

void lighting_heightmap_update(uint8_t* heightmap, c_coord_t x, w_coord_t y,
							   c_coord_t z, uint8_t type,
							   bool (*get_block)(void* user, c_coord_t x,
												 w_coord_t y, c_coord_t z,
												 struct block_data* blk),
							   void* user) {
	assert(heightmap && get_block);

	uint8_t* height = heightmap + x + z * CHUNK_SIZE;

	if(blocks[type]
	   && (!blocks[type]->can_see_through || blocks[type]->opacity > 0)) {
		if(y >= *height) {
			// The heightmap is one uint8_t per column (the Beta on-disk
			// HeightMap format), so it can only hold 0..255. When WORLD_HEIGHT
			// is 256 (PC, issue #26) the topmost cell is y=255 and the natural
			// "first sky cell" value y+1 would be 256, which wraps to 0 and
			// would flood the whole column with skylight. Clamp to 255: at
			// worst the single ceiling cell is treated as sky-lit, instead of
			// the entire column. (Unreachable at WORLD_HEIGHT 128.)
			if(y >= 255)
				*height = 255;
			else
				*height = (uint8_t)(y + 1);
		}
	} else if(y < *height) {
		while(*height > 0) {
			struct block_data blk;

			if(get_block(user, x, *height - 1, z, &blk) && blocks[blk.type]
			   && (!blocks[blk.type]->can_see_through
				   || blocks[blk.type]->opacity > 0))
				break;

			(*height)--;
		}
	}
}

struct lighting_update_entry {
	w_coord_t x, y, z;
};

static inline int8_t MAX_I8(int8_t a, int8_t b) {
	return a > b ? a : b;
}

void lighting_update_at_block(
	struct world_modification_entry source, bool ignore_sky_light,
	bool (*get_block)(void* user, w_coord_t x, w_coord_t y, w_coord_t z,
					  struct block_data* blk, uint8_t* height),
	void (*set_light)(void* user, w_coord_t x, w_coord_t y, w_coord_t z,
					  uint8_t light),
	void* user) {
	assert(get_block && set_light);

	struct stack queue;
	stack_create(&queue, 128, sizeof(struct lighting_update_entry));
	stack_push(&queue,
			   &(struct lighting_update_entry) {
				   .x = source.x,
				   .y = source.y,
				   .z = source.z,
			   });

	bool force_source = true;

	while(!stack_empty(&queue)) {
		struct lighting_update_entry current;
		stack_pop(&queue, &current);

		uint8_t column_height;
		struct block_data old;
		bool old_exists = get_block(user, current.x, current.y, current.z, &old,
									&column_height);
		assert(old_exists);

		uint8_t old_light = (old.torch_light << 4) | old.sky_light;
		uint8_t new_light_sky = 0, new_light_torch = 0;

		if(!ignore_sky_light && current.y >= column_height)
			new_light_sky = 0xF;

		if(blocks[old.type])
			new_light_torch = blocks[old.type]->luminance;

		if(!blocks[old.type] || blocks[old.type]->can_see_through) {
			for(enum side s = 0; s < SIDE_MAX; s++) {
				int x, y, z;
				blocks_side_offset(s, &x, &y, &z);

				struct block_data other;
				bool other_exists
					= get_block(user, current.x + x, current.y + y,
								current.z + z, &other, NULL);

				if(other_exists) {
					int8_t opacity = blocks[old.type] ?
						MAX_I8(blocks[old.type]->opacity, 1) :
						1;

					new_light_sky
						= MAX_I8(MAX_I8((int8_t)other.sky_light - opacity, 0),
								 new_light_sky);
					new_light_torch
						= MAX_I8(MAX_I8((int8_t)other.torch_light - opacity, 0),
								 new_light_torch);
				}
			}
		}

		uint8_t new_light = (new_light_torch << 4) | new_light_sky;

		bool is_source = source.x == current.x && source.y == current.y
						 && source.z == current.z;

		if(old_light != new_light || (is_source && force_source)) {
			set_light(user, current.x, current.y, current.z, new_light);

			if(is_source)
				force_source = false;

			for(enum side s = 0; s < SIDE_MAX; s++) {
				int x, y, z;
				blocks_side_offset(s, &x, &y, &z);

				if(get_block(user, current.x + x, current.y + y, current.z + z,
							 NULL, NULL))
					stack_push(&queue,
							   &(struct lighting_update_entry) {
								   .x = current.x + x,
								   .y = current.y + y,
								   .z = current.z + z,
							   });
			}
		}
	}

	stack_destroy(&queue);
}
