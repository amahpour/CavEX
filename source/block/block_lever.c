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

// Lever + the minimal "redstone" shared with the button. CavEX has no power
// graph (no wire/dust propagation), so this is DIRECT LOCAL ACTIVATION: flipping
// the switch drives the up-to-6 blocks touching it -- (iron) doors open/close,
// powered/detector rails toggle their powered bit. The switch keeps its
// orientation in the low 3 metadata bits (torch convention: 1..4 wall, 5 floor)
// and its on/off state in bit 0x08; the render reuses the torch renderer, which
// masks those orientation bits.

void block_redstone_activate(struct server_local* s, w_coord_t x, w_coord_t y,
							 w_coord_t z, bool powered) {
	static const int off[6][3]
		= {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};

	for(int i = 0; i < 6; i++) {
		w_coord_t nx = x + off[i][0];
		w_coord_t ny = y + off[i][1];
		w_coord_t nz = z + off[i][2];

		struct block_data b;
		if(!server_world_get_block(&s->world, nx, ny, nz, &b))
			continue;

		if(b.type == BLOCK_WOODEN_DOOR || b.type == BLOCK_IRON_DOOR) {
			block_door_set_open(s, nx, ny, nz, powered);
		} else if(b.type == BLOCK_POWERED_RAIL || b.type == BLOCK_DETECTOR_RAIL) {
			if(powered)
				b.metadata |= 0x08;
			else
				b.metadata &= ~0x08;
			server_world_set_block(&s->world, nx, ny, nz, b);
		}
	}
}

static enum block_material getMaterial(struct block_info* this) {
	return MATERIAL_STONE;
}

static size_t getBoundingBox(struct block_info* this, bool entity,
							 struct AABB* x) {
	if(x) {
		aabb_setsize(x, 0.3125F, 0.625F, 0.3125F);
		switch(this->block->metadata & 0x07) {
			case 1: aabb_translate(x, -0.34375F, 0.1875F, 0); break;
			case 2: aabb_translate(x, 0.34375F, 0.1875F, 0); break;
			case 3: aabb_translate(x, 0, 0.1875F, -0.34375F); break;
			case 4: aabb_translate(x, 0, 0.1875F, 0.34375F); break;
			default: aabb_setsize(x, 0.25F, 0.375F, 0.25F); break;
		}
	}

	return entity ? 0 : 1;
}

static struct face_occlusion*
getSideMask(struct block_info* this, enum side side, struct block_info* it) {
	return face_occlusion_empty();
}

static uint8_t getTextureIndex(struct block_info* this, enum side side) {
	return tex_atlas_lookup(TEXAT_COBBLESTONE);
}

static bool onItemPlace(struct server_local* s, struct item_data* it,
						struct block_info* where, struct block_info* on,
						enum side on_side) {
	// Attach to the face that was clicked (not the underside), onto a solid
	// block -- same rule and orientation encoding as a torch.
	if(on_side == SIDE_BOTTOM || !blocks[on->block->type]
	   || blocks[on->block->type]->can_see_through)
		return false;

	int metadata;
	switch(on_side) {
		case SIDE_LEFT: metadata = 2; break;
		case SIDE_RIGHT: metadata = 1; break;
		case SIDE_FRONT: metadata = 4; break;
		case SIDE_BACK: metadata = 3; break;
		default: metadata = 5; break; // SIDE_TOP -> stands on the floor
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
	// Toggle the on/off bit, persist it, then drive the neighbours.
	on->block->metadata ^= 0x08;
	server_world_set_block(&s->world, on->x, on->y, on->z, *on->block);
	block_redstone_activate(s, on->x, on->y, on->z,
							on->block->metadata & 0x08);
}

struct block block_lever = {
	.name = "Lever",
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
	.digging.tool = TOOL_TYPE_ANY,
	.digging.min = TOOL_TIER_ANY,
	.digging.best = TOOL_TIER_ANY,
	.block_item = {
		.has_damage = false,
		.max_stack = 64,
		.renderItem = render_item_flat,
		.onItemPlace = onItemPlace,
		.armor.is_armor = false,
		.tool.type = TOOL_TYPE_ANY,
	},
};
