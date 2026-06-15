#include "block/blocks.h"
#include "block/blocks_data.h"

static struct block solid_block = {
	.can_see_through = false,
	.opacity = 15,
	.luminance = 0,
};

static struct block transparent_block = {
	.can_see_through = true,
	.opacity = 0,
	.luminance = 0,
};

static struct block torch_block = {
	.can_see_through = true,
	.opacity = 0,
	.luminance = 14,
};

struct block* blocks[256];

void test_blocks_init(void) {
	for(int k = 0; k < 256; k++)
		blocks[k] = NULL;

	blocks[BLOCK_STONE] = &solid_block;
	blocks[BLOCK_GLASS] = &transparent_block;
	blocks[BLOCK_TORCH] = &torch_block;
	blocks[BLOCK_DIRT] = &solid_block;
}
