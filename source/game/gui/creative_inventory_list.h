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

#ifndef CREATIVE_INVENTORY_LIST_H
#define CREATIVE_INVENTORY_LIST_H

#include <stddef.h>
#include <stdint.h>

// Pure enumeration of the blocks shown in the creative pick-any inventory.
//
// The creative inventory is a paginated grid of every PLACEABLE registered
// block. A block id is included iff blocks[id] is non-NULL (it is a real,
// registered block) AND that block exposes an item renderer
// (blocks[id]->block_item.renderItem != NULL) so the cell draws a real texture
// rather than nothing. Ids are scanned in ascending order 1..255 (id 0 is air
// and is never offered).
//
// These functions are deliberately free of any GUI / GL / engine state so the
// enumeration logic can be unit-tested directly against the block registry
// (the GUI screen merely lays the resulting list out on a grid).

// Number of blocks offered by the creative inventory, given the current
// `blocks[]` registry.
size_t creative_inventory_count(void);

// The block id at logical position `index` in the creative list (0-based, in
// ascending block-id order), or 0 if `index` is out of range. The returned id
// is always a valid index into `blocks[]` with a non-NULL entry.
uint8_t creative_inventory_block_id(size_t index);

#endif
