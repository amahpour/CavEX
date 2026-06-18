// Unit tests for the pure creative-inventory block enumeration (issue #72).
// creative_inventory_count()/_block_id() walk the global blocks[] registry and
// report the placeable blocks the creative pick-any grid offers: every non-NULL
// entry in id order 1..255 that also has an item renderer (so no cell would
// render as nothing). No GUI / GL state, so it is tested directly against a
// hand-built registry.
//
// The repo enforces a per-test coverage gate (each registered test must add >=1
// line not covered by any other test), so these group the distinct code paths.

#include "block/blocks.h"
#include "game/gui/creative_inventory_list.h"
#include "harness.h"

// These real block definitions are linked into cavex_testlib and each carries a
// non-NULL block_item.renderItem, so they are valid creative entries.
extern struct block block_candle;
extern struct block block_oak_wood;
extern struct block block_smooth_stone;

// A registered block with NO item renderer: must be excluded from the list even
// though blocks[id] is non-NULL (otherwise its cell would draw nothing).
static struct block block_no_renderer = {
	.name = "No Renderer",
};

// Install a known registry: id 0 is air (never offered), a couple of NULL gaps,
// three renderable blocks and one renderer-less block.
static void setup_registry(void) {
	for(int k = 0; k < 256; k++)
		blocks[k] = NULL;

	blocks[5] = &block_candle;		   // included
	blocks[17] = &block_oak_wood;	   // included
	blocks[42] = &block_no_renderer;   // excluded (no renderItem)
	blocks[200] = &block_smooth_stone; // included
}

// count(): only renderable, non-NULL, id>=1 entries are counted.
TEST(creative_count_skips_null_and_unrenderable) {
	setup_registry();
	ASSERT_EQ(creative_inventory_count(), 3u);

	// id 0 (air) is never offered even if something is wrongly parked there.
	blocks[0] = &block_candle;
	ASSERT_EQ(creative_inventory_count(), 3u);
	blocks[0] = NULL;

	// An empty registry yields an empty list.
	for(int k = 0; k < 256; k++)
		blocks[k] = NULL;
	ASSERT_EQ(creative_inventory_count(), 0u);
}

// block_id(): returns ids in ascending order, skipping NULL/unrenderable gaps,
// and returns 0 for out-of-range indices.
TEST(creative_block_id_order_and_bounds) {
	setup_registry();

	// Ascending id order, gaps (NULLs + the renderer-less block 42) skipped.
	ASSERT_EQ(creative_inventory_block_id(0), 5);
	ASSERT_EQ(creative_inventory_block_id(1), 17);
	ASSERT_EQ(creative_inventory_block_id(2), 200);

	// Out of range -> 0 (the grid treats this as an empty cell).
	ASSERT_EQ(creative_inventory_block_id(3), 0);
	ASSERT_EQ(creative_inventory_block_id(99), 0);

	// Every reported id is a valid, non-NULL, renderable registry entry, and
	// indices below the count are dense (never 0).
	for(size_t i = 0; i < creative_inventory_count(); i++) {
		uint8_t id = creative_inventory_block_id(i);
		ASSERT(id > 0);
		ASSERT(blocks[id] != NULL);
		ASSERT(blocks[id]->block_item.renderItem != NULL);
	}
}

const test_entry_t g_tests_creative_inventory[] = {
	{"creative_count_skips_null_and_unrenderable",
	 test_creative_count_skips_null_and_unrenderable},
	{"creative_block_id_order_and_bounds",
	 test_creative_block_id_order_and_bounds},
};

const size_t g_tests_creative_inventory_count
	= sizeof(g_tests_creative_inventory) / sizeof(g_tests_creative_inventory[0]);
