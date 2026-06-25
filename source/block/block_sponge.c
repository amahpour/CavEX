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

#include "../network/server_local.h"
#include "blocks.h"

// Sponge absorption radius (in blocks) around the placed sponge. Capped at 2
// (a 5x5x5 = 125-cell scan) to keep the resulting remesh within the Wii MEM1
// budget (see issue #104).
#define SPONGE_ABSORB_RADIUS 2

static enum block_material getMaterial(struct block_info* this) {
	return MATERIAL_ORGANIC;
}

static size_t getBoundingBox(struct block_info* this, bool entity,
							 struct AABB* x) {
	if(x)
		aabb_setsize(x, 1.0F, 1.0F, 1.0F);
	return 1;
}

static struct face_occlusion*
getSideMask(struct block_info* this, enum side side, struct block_info* it) {
	return face_occlusion_full();
}

static uint8_t getTextureIndex(struct block_info* this, enum side side) {
	return tex_atlas_lookup(TEXAT_SPONGE);
}

static bool onItemPlace(struct server_local* s, struct item_data* it,
						struct block_info* where, struct block_info* on,
						enum side on_side) {
	// Place the sponge itself first; bail (without absorbing) if that fails.
	if(!block_place_default(s, it, where, on, on_side))
		return false;

	// Dry up nearby water: scan a cube around the sponge and clear only water
	// cells (never lava or solids). Server-authoritative via the `s` world.
	for(int dx = -SPONGE_ABSORB_RADIUS; dx <= SPONGE_ABSORB_RADIUS; dx++) {
		for(int dy = -SPONGE_ABSORB_RADIUS; dy <= SPONGE_ABSORB_RADIUS; dy++) {
			for(int dz = -SPONGE_ABSORB_RADIUS; dz <= SPONGE_ABSORB_RADIUS;
				dz++) {
				w_coord_t x = where->x + dx;
				w_coord_t y = where->y + dy;
				w_coord_t z = where->z + dz;

				struct block_data blk;
				if(!server_world_get_block(&s->world, x, y, z, &blk))
					continue;

				if(blk.type == BLOCK_WATER_FLOW
				   || blk.type == BLOCK_WATER_STILL)
					server_world_set_block(&s->world, x, y, z,
										   (struct block_data) {
											   .type = BLOCK_AIR,
											   .metadata = 0,
										   });
			}
		}
	}

	return true;
}

struct block block_sponge = {
	.name = "Sponge",
	.getSideMask = getSideMask,
	.getBoundingBox = getBoundingBox,
	.getMaterial = getMaterial,
	.getTextureIndex = getTextureIndex,
	.getDroppedItem = block_drop_default,
	.onRandomTick = NULL,
	.onRightClick = NULL,
	.transparent = false,
	.renderBlock = render_block_full,
	.renderBlockAlways = NULL,
	.luminance = 0,
	.double_sided = false,
	.can_see_through = false,
	.ignore_lighting = false,
	.flammable = false,
	.place_ignore = false,
	.digging.hardness = 900,
	.digging.tool = TOOL_TYPE_ANY,
	.digging.min = TOOL_TIER_ANY,
	.digging.best = TOOL_TIER_ANY,
	.block_item = {
		.has_damage = false,
		.max_stack = 64,
		.renderItem = render_item_block,
		.onItemPlace = onItemPlace,
		.render_data.block.has_default = false,
		.armor.is_armor = false,
		.tool.type = TOOL_TYPE_ANY,
	},
};
