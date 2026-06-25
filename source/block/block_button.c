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

// Stone button. Shares the lever's minimal "redstone" (block_redstone_activate)
// and the same metadata layout: orientation in the low 3 bits, on/off in 0x08.
// A real Minecraft button is momentary (auto-releases after a short delay), but
// CavEX has no block-tick scheduler, so this button TOGGLES like a lever -- one
// click powers the neighbours, the next releases them. Renders as a small stub
// via the torch renderer (no dedicated button art).

static enum block_material getMaterial(struct block_info* this) {
	return MATERIAL_STONE;
}

static size_t getBoundingBox(struct block_info* this, bool entity,
							 struct AABB* x) {
	if(x) {
		aabb_setsize(x, 0.25F, 0.375F, 0.25F);
		switch(this->block->metadata & 0x07) {
			case 1: aabb_translate(x, -0.4063F, 0.125F, 0); break;
			case 2: aabb_translate(x, 0.4063F, 0.125F, 0); break;
			case 3: aabb_translate(x, 0, 0.125F, -0.4063F); break;
			case 4: aabb_translate(x, 0, 0.125F, 0.4063F); break;
			default: break;
		}
	}

	return entity ? 0 : 1;
}

static struct face_occlusion*
getSideMask(struct block_info* this, enum side side, struct block_info* it) {
	return face_occlusion_empty();
}

static uint8_t getTextureIndex(struct block_info* this, enum side side) {
	return tex_atlas_lookup(TEXAT_STONE);
}

static bool onItemPlace(struct server_local* s, struct item_data* it,
						struct block_info* where, struct block_info* on,
						enum side on_side) {
	// Buttons attach to a vertical wall face (not floor/ceiling), onto a solid
	// block -- same orientation encoding as the torch/lever.
	if(on_side == SIDE_BOTTOM || on_side == SIDE_TOP || !blocks[on->block->type]
	   || blocks[on->block->type]->can_see_through)
		return false;

	int metadata;
	switch(on_side) {
		case SIDE_LEFT: metadata = 2; break;
		case SIDE_RIGHT: metadata = 1; break;
		case SIDE_FRONT: metadata = 4; break;
		case SIDE_BACK: metadata = 3; break;
		default: return false;
	}

	server_world_set_block(&s->world, where->x, where->y, where->z,
						   (struct block_data) {
							   .type = it->id,
							   .metadata = metadata,
							   .sky_light = 0,
							   .torch_light = 0,
						   });
	return true;
}

static void onRightClick(struct server_local* s, struct item_data* it,
						 struct block_info* where, struct block_info* on,
						 enum side on_side) {
	on->block->metadata ^= 0x08;
	server_world_set_block(&s->world, on->x, on->y, on->z, *on->block);
	block_redstone_activate(s, on->x, on->y, on->z,
							on->block->metadata & 0x08);
}

struct block block_stone_button = {
	.name = "Button",
	.getSideMask = getSideMask,
	.getBoundingBox = getBoundingBox,
	.getMaterial = getMaterial,
	.getTextureIndex = getTextureIndex,
	.getDroppedItem = block_drop_default,
	.onRandomTick = NULL,
	.onRightClick = onRightClick,
	.transparent = false,
	.renderBlock = render_block_torch,
	.renderBlockAlways = NULL,
	.luminance = 0,
	.double_sided = false,
	.can_see_through = true,
	.opacity = 0,
	.ignore_lighting = false,
	.flammable = false,
	.place_ignore = false,
	.digging.hardness = 250,
	.digging.tool = TOOL_TYPE_PICKAXE,
	.digging.min = TOOL_TIER_WOOD,
	.digging.best = TOOL_TIER_WOOD,
	.block_item = {
		.has_damage = false,
		.max_stack = 64,
		.renderItem = render_item_flat,
		.onItemPlace = onItemPlace,
		.armor.is_armor = false,
		.tool.type = TOOL_TYPE_ANY,
	},
};
