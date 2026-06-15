#include <assert.h>

#include "block/blocks.h"
#include "block/blocks_data.h"

static struct block solid_block = {
	.can_see_through = false,
	.opacity = 15,
};

static struct block transparent_block = {
	.can_see_through = true,
	.opacity = 0,
};

static struct block semi_opaque_block = {
	.can_see_through = true,
	.opacity = 15,
};

struct block* blocks[256];

void test_blocks_init(void) {
	for(int k = 0; k < 256; k++)
		blocks[k] = NULL;

	blocks[BLOCK_STONE] = &solid_block;
	blocks[BLOCK_GLASS] = &transparent_block;
	blocks[BLOCK_ICE] = &semi_opaque_block;
}

void blocks_side_offset(enum side s, int* x, int* y, int* z) {
	assert(x && y && z);

	switch(s) {
		default:
		case SIDE_TOP:
			*x = 0;
			*y = 1;
			*z = 0;
			break;
		case SIDE_BOTTOM:
			*x = 0;
			*y = -1;
			*z = 0;
			break;
		case SIDE_LEFT:
			*x = -1;
			*y = 0;
			*z = 0;
			break;
		case SIDE_RIGHT:
			*x = 1;
			*y = 0;
			*z = 0;
			break;
		case SIDE_BACK:
			*x = 0;
			*y = 0;
			*z = -1;
			break;
		case SIDE_FRONT:
			*x = 0;
			*y = 0;
			*z = 1;
			break;
	}
}
