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

#include "../../network/server_local.h"

// The Cake item (id 354) and the Cake block (id 92) are separate: the item is
// what you hold/craft, the block is what sits in the world (and is eaten via the
// block's onRightClick from #105). The item used to have no onItemPlace, so a
// cake could not be placed from the hotbar/inventory -- only the raw block-item
// form placed. This adds placement of the cake BLOCK from the item.
//
// Cakes need support: the cell below must be a solid (non-see-through) block, so
// a cake never floats (mirrors block_snow.c's onItemPlace support check). The
// target cell was already confirmed replaceable by the block-place path before
// this runs. We place block id 92 (not the item's id 354) by handing
// block_place_default a remapped item_data, which also runs the entity-collision
// check so a cake cannot be placed inside the player.
#define BLOCK_ID_CAKE 92

static bool onItemPlace(struct server_local* s, struct item_data* it,
						struct block_info* where, struct block_info* on,
						enum side on_side) {
	(void)it;

	// The block below must be solid so the cake has support.
	struct block_data below;
	if(!server_world_get_block(&s->world, where->x, where->y - 1, where->z,
							   &below))
		return false;
	if(!blocks[below.type] || blocks[below.type]->can_see_through)
		return false;

	// Place the cake BLOCK (id 92, metadata 0 = whole cake), not the item id.
	struct item_data cake_block = {
		.id = BLOCK_ID_CAKE,
		.durability = 0,
		.count = 1,
	};
	return block_place_default(s, &cake_block, where, on, on_side);
}

struct item item_cake = {
	.name = "Cake",
	.has_damage = false,
	.max_stack = 1,
	.renderItem = render_item_flat,
	.onItemPlace = onItemPlace,
	.armor.is_armor = false,
	.tool.type = TOOL_TYPE_ANY,
	.render_data = {
		.item = {
			.texture_x = 13,
			.texture_y = 1,
		},
	},
};
