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
#include "../../network/client_interface.h"
#include "../../network/server_local.h"

// Number of sparks emitted by a single firework burst.
#define FIREWORK_PARTICLE_COUNT 24
// Terrain-atlas tile reused for the spark. Glowstone (atlas x=9, y=6) is a
// bright yellow tile; no new art is added (issue #31 MVP: texture-only, no
// per-particle RGB).
#define FIREWORK_PARTICLE_TEX (6 * 14 + 9)

// onItemUse: emit an upward, outward burst of sparks at the player. MVP — no
// projectile entity; the burst happens at the player's head. The particle
// system is owned by the client thread, so this asks the client to spawn the
// burst via an RPC rather than touching the particle array from the server
// thread. Returns true so the caller consumes one firework from the stack.
static bool firework_use(struct server_local* s, struct item_data* it) {
	(void)it;

	clin_rpc_send(&(struct client_rpc) {
		.type = CRPC_PARTICLE_BURST,
		.payload.particle_burst.pos = {s->player.x, s->player.y + 1.0F,
									   s->player.z},
		.payload.particle_burst.count = FIREWORK_PARTICLE_COUNT,
		.payload.particle_burst.tex = FIREWORK_PARTICLE_TEX,
	});

	return true;
}

struct item item_firework = {
	.name = "Firework Rocket",
	.has_damage = false,
	.max_stack = 64,
	.renderItem = render_item_flat,
	.onItemUse = firework_use,
	.armor.is_armor = false,
	.tool.type = TOOL_TYPE_ANY,
	.render_data = {
		.item = {
			// reuse the gunpowder tile (atlas x=8, y=2); no new art
			.texture_x = 8,
			.texture_y = 2,
		},
	},
};
