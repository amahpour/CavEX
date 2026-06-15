#include "block/blocks.h"
#include "block/blocks_data.h"
#include "harness.h"
#include "stubs/blocks_stub.h"

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

TEST(blocks_all_sides) {
	int x, y, z;

	// opposite for every side
	ASSERT_EQ(blocks_side_opposite(SIDE_TOP), SIDE_BOTTOM);
	ASSERT_EQ(blocks_side_opposite(SIDE_BOTTOM), SIDE_TOP);
	ASSERT_EQ(blocks_side_opposite(SIDE_LEFT), SIDE_RIGHT);
	ASSERT_EQ(blocks_side_opposite(SIDE_RIGHT), SIDE_LEFT);
	ASSERT_EQ(blocks_side_opposite(SIDE_FRONT), SIDE_BACK);
	ASSERT_EQ(blocks_side_opposite(SIDE_BACK), SIDE_FRONT);

	// name for every side
	ASSERT(strcmp(block_side_name(SIDE_TOP), "top") == 0);
	ASSERT(strcmp(block_side_name(SIDE_BOTTOM), "bottom") == 0);
	ASSERT(strcmp(block_side_name(SIDE_LEFT), "left") == 0);
	ASSERT(strcmp(block_side_name(SIDE_RIGHT), "right") == 0);
	ASSERT(strcmp(block_side_name(SIDE_FRONT), "front") == 0);
	ASSERT(strcmp(block_side_name(SIDE_BACK), "back") == 0);

	// offset for every side
	blocks_side_offset(SIDE_TOP, &x, &y, &z);
	ASSERT(x == 0 && y == 1 && z == 0);
	blocks_side_offset(SIDE_BOTTOM, &x, &y, &z);
	ASSERT(x == 0 && y == -1 && z == 0);
	blocks_side_offset(SIDE_LEFT, &x, &y, &z);
	ASSERT(x == -1 && y == 0 && z == 0);
	blocks_side_offset(SIDE_RIGHT, &x, &y, &z);
	ASSERT(x == 1 && y == 0 && z == 0);
	blocks_side_offset(SIDE_BACK, &x, &y, &z);
	ASSERT(x == 0 && y == 0 && z == 1);
	blocks_side_offset(SIDE_FRONT, &x, &y, &z);
	ASSERT(x == 0 && y == 0 && z == -1);
}

TEST(block_bubble_column_registered) {
	test_blocks_init();

	struct block* b = blocks[BLOCK_BUBBLE_COLUMN];

	// registered and named (issue #32 acceptance: blocks[98] != NULL, name set)
	ASSERT(b != NULL);
	ASSERT(b->name != NULL);
	ASSERT(strcmp(b->name, "Bubble Column") == 0);

	// see-through so it renders translucent and never occludes
	ASSERT(b->can_see_through);

	struct block_data blk = {.type = BLOCK_BUBBLE_COLUMN};
	struct block_info info = {.block = &blk};

	// passable: no collision box against entities (the property that lets the
	// player stand inside the column and be pushed upward). Calling the real
	// getBoundingBox callback also gives the coverage gate a unique line.
	struct AABB box;
	ASSERT_EQ(b->getBoundingBox(&info, true, &box), 0U);
	// still solid for ray/mesh queries (non-entity)
	ASSERT_EQ(b->getBoundingBox(&info, false, &box), 1U);

	// drops nothing
	ASSERT_EQ(b->getDroppedItem(&info, NULL, NULL), 0U);
}

TEST(candle_block_registered) {
	// The candle (issue #30) is a registration-only, light-emitting block.
	// blocks_init() is GX/graphics-bound and is not linked into the test
	// harness, so register the real candle struct into the stub registry the
	// same way blocks_init() does, then exercise its accessors.
	blocks[BLOCK_CANDLE] = &block_candle;

	ASSERT_NE(blocks[BLOCK_CANDLE], NULL);
	ASSERT(strcmp(blocks[BLOCK_CANDLE]->name, "Candle") == 0);
	ASSERT(blocks[BLOCK_CANDLE]->luminance > 0);

	// Drives the real candle code paths so the test adds covered lines.
	struct block_data blk = {.type = BLOCK_CANDLE};
	struct block_info info = {.block = &blk};
	ASSERT_EQ(blocks[BLOCK_CANDLE]->getMaterial(&info), MATERIAL_GLASS);
	(void)blocks[BLOCK_CANDLE]->getTextureIndex(&info, SIDE_TOP);

	// Candle drops itself (block_drop_default), like other simple blocks.
	struct item_data drop = {0};
	ASSERT_EQ(blocks[BLOCK_CANDLE]->getDroppedItem(&info, &drop, NULL), 1U);
	ASSERT_EQ(drop.id, BLOCK_CANDLE);
	ASSERT_EQ(drop.count, 1U);

	blocks[BLOCK_CANDLE] = NULL;
}

const test_entry_t g_tests_blocks[] = {
	{"blocks_side_helpers", test_blocks_side_helpers},
	{"block_drop_default_item", test_block_drop_default_item},
	{"blocks_all_sides", test_blocks_all_sides},
	{"block_bubble_column_registered", test_block_bubble_column_registered},
	{"candle_block_registered", test_candle_block_registered},
};

const size_t g_tests_blocks_count
	= sizeof(g_tests_blocks) / sizeof(g_tests_blocks[0]);
