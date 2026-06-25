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

#include "../network/server_local.h"
#include "blocks.h"

// A thin (1/16-block) rug that sits on top of a solid block, in the 16 wool
// colours (metadata 0..15, reusing the wool textures -- no new art). Not in
// Minecraft Beta 1.7.3, but the engine supports it cleanly and it is on the
// son's wishlist. Placed from creative as white (metadata 0), like wool.

static enum block_material getMaterial(struct block_info* this) {
	return MATERIAL_WOOL;
}

static size_t getBoundingBox(struct block_info* this, bool entity,
							 struct AABB* x) {
	if(x)
		aabb_setsize(x, 1.0F, 0.0625F, 1.0F);
	return 1;
}

static struct face_occlusion*
getSideMask(struct block_info* this, enum side side, struct block_info* it) {
	// Only the bottom face occludes: it sits flush on the block below, so
	// hiding that block's top face avoids z-fighting. The thin top/sides never
	// occlude neighbours.
	return (side == SIDE_BOTTOM) ? face_occlusion_full() : face_occlusion_empty();
}

static uint8_t getTextureIndex(struct block_info* this, enum side side) {
	switch(this->block->metadata & 0x0F) {
		case 1: return tex_atlas_lookup(TEXAT_WOOL_1);
		case 2: return tex_atlas_lookup(TEXAT_WOOL_2);
		case 3: return tex_atlas_lookup(TEXAT_WOOL_3);
		case 4: return tex_atlas_lookup(TEXAT_WOOL_4);
		case 5: return tex_atlas_lookup(TEXAT_WOOL_5);
		case 6: return tex_atlas_lookup(TEXAT_WOOL_6);
		case 7: return tex_atlas_lookup(TEXAT_WOOL_7);
		case 8: return tex_atlas_lookup(TEXAT_WOOL_8);
		case 9: return tex_atlas_lookup(TEXAT_WOOL_9);
		case 10: return tex_atlas_lookup(TEXAT_WOOL_10);
		case 11: return tex_atlas_lookup(TEXAT_WOOL_11);
		case 12: return tex_atlas_lookup(TEXAT_WOOL_12);
		case 13: return tex_atlas_lookup(TEXAT_WOOL_13);
		case 14: return tex_atlas_lookup(TEXAT_WOOL_14);
		case 15: return tex_atlas_lookup(TEXAT_WOOL_15);
		default: return tex_atlas_lookup(TEXAT_WOOL_0);
	}
}

static size_t getDroppedItem(struct block_info* this, struct item_data* it,
							 struct random_gen* g) {
	if(it) {
		it->id = this->block->type;
		it->durability = this->block->metadata;
		it->count = 1;
	}

	return 1;
}

static bool onItemPlace(struct server_local* s, struct item_data* it,
						struct block_info* where, struct block_info* on,
						enum side on_side) {
	// Needs a solid block directly beneath (no floating rugs).
	struct block_data blk;
	if(!server_world_get_block(&s->world, where->x, where->y - 1, where->z,
							   &blk))
		return false;

	if(!blocks[blk.type] || blocks[blk.type]->can_see_through)
		return false;

	return block_place_default(s, it, where, on, on_side);
}

struct block block_carpet = {
	.name = "Carpet",
	.getSideMask = getSideMask,
	.getBoundingBox = getBoundingBox,
	.getMaterial = getMaterial,
	.getTextureIndex = getTextureIndex,
	.getDroppedItem = getDroppedItem,
	.onRandomTick = NULL,
	.onRightClick = NULL,
	.transparent = false,
	.renderBlock = render_block_carpet,
	.renderBlockAlways = NULL,
	.luminance = 0,
	.double_sided = false,
	.can_see_through = true,
	.opacity = 0,
	.ignore_lighting = false,
	.flammable = true,
	.place_ignore = false,
	.digging.hardness = 150,
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
