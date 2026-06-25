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

#include <math.h>

#include "../../network/server_local.h"

// A bed is two blocks: the foot at the targeted cell and the head one cell away
// in the direction the player is facing. Both halves are BLOCK_BED; the low two
// metadata bits encode the facing direction (0:+Z 1:-X 2:-Z 3:+X, matching the
// renderer / Minecraft Beta), and bit 0x8 marks the head (pillow). The previous
// behaviour placed a single broken block via block_place_default; this gives a
// real, connected bed.
static bool onItemPlace(struct server_local* s, struct item_data* it,
						struct block_info* where, struct block_info* on,
						enum side on_side) {
	(void)it;
	(void)on;
	(void)on_side;

	// Player look direction (foot -> head), snapped to a cardinal axis. Uses the
	// same yaw convention as the boat: forward = (sin(-rx), cos(-rx)).
	float yaw = glm_rad(-(float)s->player.rx);
	float fx = sinf(yaw);
	float fz = cosf(yaw);

	int dx = 0, dz = 0, dir;
	if(fabsf(fx) > fabsf(fz)) {
		if(fx >= 0.0F) {
			dx = 1;
			dir = 3;
		} else {
			dx = -1;
			dir = 1;
		}
	} else {
		if(fz >= 0.0F) {
			dz = 1;
			dir = 0;
		} else {
			dz = -1;
			dir = 2;
		}
	}

	int hx = where->x + dx;
	int hy = where->y;
	int hz = where->z + dz;

	// The head cell must be free (the foot cell was already confirmed
	// replaceable by the block-place path before this runs).
	struct block_data b_head;
	if(!server_world_get_block(&s->world, hx, hy, hz, &b_head))
		return false;
	if(blocks[b_head.type] && !blocks[b_head.type]->place_ignore)
		return false;

	// Both halves need a solid floor so the bed doesn't float.
	struct block_data b_floor;
	if(!server_world_get_block(&s->world, where->x, where->y - 1, where->z,
							   &b_floor)
	   || !blocks[b_floor.type] || blocks[b_floor.type]->can_see_through)
		return false;
	if(!server_world_get_block(&s->world, hx, hy - 1, hz, &b_floor)
	   || !blocks[b_floor.type] || blocks[b_floor.type]->can_see_through)
		return false;

	server_world_set_block(&s->world, where->x, where->y, where->z,
						   (struct block_data) {
							   .type = BLOCK_BED,
							   .metadata = dir,
						   });
	server_world_set_block(&s->world, hx, hy, hz,
						   (struct block_data) {
							   .type = BLOCK_BED,
							   .metadata = dir | 0x8,
						   });
	return true;
}

struct item item_bed = {
	.name = "Bed",
	.has_damage = false,
	.max_stack = 1,
	.renderItem = render_item_flat,
	.onItemPlace = onItemPlace,
	.armor.is_armor = false,
	.tool.type = TOOL_TYPE_ANY,
	.render_data = {
		.item = {
			.texture_x = 13,
			.texture_y = 2,
		},
	},
};
