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

#include "blocks.h"

enum side blocks_side_opposite(enum side s) {
	switch(s) {
		default:
		case SIDE_TOP: return SIDE_BOTTOM;
		case SIDE_BOTTOM: return SIDE_TOP;
		case SIDE_LEFT: return SIDE_RIGHT;
		case SIDE_RIGHT: return SIDE_LEFT;
		case SIDE_FRONT: return SIDE_BACK;
		case SIDE_BACK: return SIDE_FRONT;
	}
}

const char* block_side_name(enum side s) {
	switch(s) {
		case SIDE_TOP: return "top";
		case SIDE_BOTTOM: return "bottom";
		case SIDE_LEFT: return "left";
		case SIDE_RIGHT: return "right";
		case SIDE_FRONT: return "front";
		case SIDE_BACK: return "back";
		default: return "invalid";
	}
}

void blocks_side_offset(enum side s, int* x, int* y, int* z) {
	assert(x && y && z);

	switch(s) {
		default:
		case SIDE_TOP:
			*x = 0;
			*y = 1;
			*z = 0;
			break;
		case SIDE_BOTTOM:
			*x = 0;
			*y = -1;
			*z = 0;
			break;
		case SIDE_LEFT:
			*x = -1;
			*y = 0;
			*z = 0;
			break;
		case SIDE_RIGHT:
			*x = 1;
			*y = 0;
			*z = 0;
			break;
		case SIDE_BACK:
			*x = 0;
			*y = 0;
			*z = 1;
			break;
		case SIDE_FRONT:
			*x = 0;
			*y = 0;
			*z = -1;
			break;
	}
}

size_t block_drop_default(struct block_info* this, struct item_data* it,
						  struct random_gen* g) {
	(void)g;

	if(it) {
		it->id = this->block->type;
		it->durability = 0;
		it->count = 1;
	}

	return 1;
}
