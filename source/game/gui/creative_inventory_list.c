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
#include "../../item/items.h"

// "Placed form" block ids that are offered through a dedicated item instead, so
// the grid never shows a duplicate that places a broken single block. The Bed
// and Door items (ids >= 256) place the real multi-block structure; their raw
// blocks (this list) are hidden from the grid.
static bool creative_inventory_excluded(uint16_t id) {
	return id == BLOCK_BED || id == BLOCK_WOODEN_DOOR || id == BLOCK_IRON_DOOR;
}

// An item is offered in the creative inventory when it is registered AND it can
// be drawn as an item (so no cell renders as nothing/magenta) AND it is not a
// hidden placed-form. Keeping this in one predicate means count() and item_id()
// can never disagree.
static bool creative_inventory_includes(uint16_t id) {
	return id > 0 && id < ITEMS_MAX && items[id] && items[id]->renderItem
		&& !creative_inventory_excluded(id);
}

size_t creative_inventory_count(void) {
	size_t n = 0;
	for(int id = 1; id < ITEMS_MAX; id++) {
		if(creative_inventory_includes((uint16_t)id))
			n++;
	}
	return n;
}

uint16_t creative_inventory_item_id(size_t index) {
	size_t seen = 0;
	for(int id = 1; id < ITEMS_MAX; id++) {
		if(creative_inventory_includes((uint16_t)id)) {
			if(seen == index)
				return (uint16_t)id;
			seen++;
		}
	}
	return 0;
}
