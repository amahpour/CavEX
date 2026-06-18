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

#include "creative_inventory_list.h"

#include "../../block/blocks.h"

// A block is offered in the creative inventory when it is registered AND it can
// be drawn as an item (so no cell renders as nothing/magenta). Keeping this in
// one predicate means count() and block_id() can never disagree.
static bool creative_inventory_includes(uint8_t id) {
	return id > 0 && blocks[id] && blocks[id]->block_item.renderItem;
}

size_t creative_inventory_count(void) {
	size_t n = 0;
	for(int id = 1; id < 256; id++) {
		if(creative_inventory_includes((uint8_t)id))
			n++;
	}
	return n;
}

uint8_t creative_inventory_block_id(size_t index) {
	size_t seen = 0;
	for(int id = 1; id < 256; id++) {
		if(creative_inventory_includes((uint8_t)id)) {
			if(seen == index)
				return (uint8_t)id;
			seen++;
		}
	}
	return 0;
}
