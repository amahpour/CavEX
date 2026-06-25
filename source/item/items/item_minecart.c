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

#include "../../graphics/render_item.h"
#include "../../network/server_local.h"

// onItemPlace: spawn a rideable minecart resting on the targeted cell floor,
// facing away from the player. Server-authoritative; mirrored to the client via
// CRPC_SPAWN_MINECART. Returns true so one minecart is consumed (survival).
// Boat-style scope: the cart can be placed anywhere (it rolls on the ground);
// it does not snap to rails.
static bool minecart_place(struct server_local* s, struct item_data* it,
						   struct block_info* where, struct block_info* on,
						   enum side on_side) {
	(void)it;
	(void)on;
	(void)on_side;

	float yaw = glm_rad(-(float)s->player.rx);

	// Rest the box ON the cell floor, not embedded in it (same reasoning as the
	// boat: the origin must sit at least MINECART_HEIGHT/2 above the floor or the
	// box clips the block below and steering impulses get cancelled).
	server_local_spawn_minecart((vec3) {where->x + 0.5F,
										where->y + MINECART_HEIGHT / 2.0F + 0.05F,
										where->z + 0.5F},
								yaw, s);

	return true;
}

struct item item_minecart = {
	.name = "Minecart",
	.has_damage = false,
	.max_stack = 1,
	.renderItem = render_item_flat,
	.onItemPlace = minecart_place,
	.armor.is_armor = false,
	.tool.type = TOOL_TYPE_ANY,
	.render_data = {
		.item = {
			// Vanilla Beta items.png minecart tile. Verify in playtest; swap to
			// another tile if the atlas differs.
			.texture_x = 7,
			.texture_y = 8,
		},
	},
};
