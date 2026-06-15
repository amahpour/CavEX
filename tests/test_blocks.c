#include "block/blocks.h"
#include "block/blocks_data.h"
#include "harness.h"

TEST(blocks_side_helpers) {
	int x, y, z;

	ASSERT_EQ(blocks_side_opposite(SIDE_TOP), SIDE_BOTTOM);
	ASSERT_EQ(blocks_side_opposite(SIDE_LEFT), SIDE_RIGHT);
	ASSERT_EQ(blocks_side_opposite(SIDE_FRONT), SIDE_BACK);
	ASSERT(strcmp(block_side_name(SIDE_TOP), "top") == 0);
	ASSERT(strcmp(block_side_name((enum side)99), "invalid") == 0);

	blocks_side_offset(SIDE_RIGHT, &x, &y, &z);
	ASSERT_EQ(x, 1);
	ASSERT_EQ(y, 0);
	ASSERT_EQ(z, 0);
}

TEST(block_drop_default_item) {
	struct block_data blk = {.type = BLOCK_DIRT};
	struct block_info info = {.block = &blk};
	struct item_data drop = {0};

	ASSERT_EQ(block_drop_default(&info, &drop, NULL), 1U);
	ASSERT_EQ(drop.id, BLOCK_DIRT);
	ASSERT_EQ(drop.count, 1U);
}

const test_entry_t g_tests_blocks[] = {
	{"blocks_side_helpers", test_blocks_side_helpers},
	{"block_drop_default_item", test_block_drop_default_item},
};

const size_t g_tests_blocks_count
	= sizeof(g_tests_blocks) / sizeof(g_tests_blocks[0]);
