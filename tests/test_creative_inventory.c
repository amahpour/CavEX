// Unit tests for the pure creative-inventory item enumeration.
// creative_inventory_count()/_item_id() walk the global items[] registry and
// report the entries the creative grid offers: every non-NULL items[id] that
// has an item renderer AND is not a hidden "placed-form" (the raw bed/door
// blocks, offered as dedicated items instead). This spans BOTH block items (ids
// 1..255) and dedicated items (ids >= 256), so the survival-style grid offers
// the whole set. No GUI / GL state, so it is tested directly against a
// hand-built registry.
//
// Ordering: a curated FEATURED set (carpet/lever/button/doors/bed/minecart/boat)
// is surfaced FIRST in build-priority order; the remaining includable ids follow
// in ascending id order with the featured ids skipped, so each id appears once
// and the total count is unchanged.
//
// The repo enforces a per-test coverage gate (each registered test must add >=1
// line not covered by any other test), so these group the distinct code paths.

#include "block/blocks_data.h"
#include "game/gui/creative_inventory_list.h"
#include "harness.h"
#include "item/items.h"

// A non-NULL renderer is all the enumeration needs; it is never called here.
static void dummy_render(struct item* a, struct item_data* b, mat4 c, bool d,
						 enum render_item_env e) {
	(void)a;
	(void)b;
	(void)c;
	(void)d;
	(void)e;
}

static struct item item_renderable
	= {.name = "Renderable", .max_stack = 64, .renderItem = dummy_render};
static struct item item_no_renderer = {.name = "No Renderer"};

// Install a known registry: id 0 is empty (never offered), a couple of NULL
// gaps, two renderable items, one renderer-less item, one hidden placed-form
// (the raw bed block id), and one dedicated item id >= 256 to prove the
// enumeration is not limited to blocks.
static void setup_registry(void) {
	for(int k = 0; k < ITEMS_MAX; k++)
		items[k] = NULL;

	items[5] = &item_renderable;		  // included
	items[17] = &item_renderable;		  // included
	items[42] = &item_no_renderer;		  // excluded (no renderItem)
	items[BLOCK_BED] = &item_renderable;  // excluded (hidden placed-form, id 26)
	items[300] = &item_renderable;		  // included (dedicated item, id >= 256)
}

// count(): only renderable, non-NULL, non-excluded, id>=1 entries are counted.
TEST(creative_count_skips_null_unrenderable_and_excluded) {
	setup_registry();
	ASSERT_EQ(creative_inventory_count(), 3u);

	// id 0 is never offered even if something is wrongly parked there.
	items[0] = &item_renderable;
	ASSERT_EQ(creative_inventory_count(), 3u);
	items[0] = NULL;

	// An empty registry yields an empty list.
	for(int k = 0; k < ITEMS_MAX; k++)
		items[k] = NULL;
	ASSERT_EQ(creative_inventory_count(), 0u);
}

// item_id(): returns ids in ascending order, skipping NULL/unrenderable/hidden
// gaps, spanning ids >= 256, and returns 0 for out-of-range indices.
TEST(creative_item_id_order_bounds_and_excludes) {
	setup_registry();

	// Ascending id order; gaps (NULLs, the renderer-less 42, the hidden bed 26)
	// are skipped, and the dedicated item at 300 is included.
	ASSERT_EQ(creative_inventory_item_id(0), 5);
	ASSERT_EQ(creative_inventory_item_id(1), 17);
	ASSERT_EQ(creative_inventory_item_id(2), 300);

	// Out of range -> 0 (the grid treats this as an empty cell).
	ASSERT_EQ(creative_inventory_item_id(3), 0);
	ASSERT_EQ(creative_inventory_item_id(99), 0);

	// Every reported id is a valid, renderable, non-excluded registry entry, and
	// the hidden placed-form is never returned even though it is renderable.
	for(size_t i = 0; i < creative_inventory_count(); i++) {
		uint16_t id = creative_inventory_item_id(i);
		ASSERT(id > 0);
		ASSERT(id != BLOCK_BED);
		ASSERT(items[id] != NULL);
		ASSERT(items[id]->renderItem != NULL);
	}
}

// Install a registry exercising the featured reorder: several curated featured
// ids (carpet 171, lever 69, button 77, bed 355 -- a subset of FEATURED, given
// out of registration order), plus two plain ids (5, 17) that must fall to the
// ascending tail behind the featured block.
static void setup_featured_registry(void) {
	for(int k = 0; k < ITEMS_MAX; k++)
		items[k] = NULL;

	items[5] = &item_renderable;	// plain tail
	items[17] = &item_renderable;	// plain tail
	items[69] = &item_renderable;	// featured (lever)
	items[77] = &item_renderable;	// featured (button)
	items[171] = &item_renderable;	// featured (carpet)
	items[355] = &item_renderable;	// featured (bed item)
}

// item_id(): the includable featured ids come first in FEATURED order (NOT
// ascending id order), then the remaining ids ascending; count is unchanged.
TEST(creative_item_id_features_placeables_first) {
	setup_featured_registry();

	// Total is still every includable id (4 featured + 2 plain), unchanged by
	// the reorder.
	ASSERT_EQ(creative_inventory_count(), 6u);

	// Page 1: featured ids in FEATURED order {171,69,77,...,355,...}. Only the
	// registered subset appears, but their relative order matches FEATURED:
	// carpet(171), lever(69), button(77), bed(355).
	ASSERT_EQ(creative_inventory_item_id(0), 171);
	ASSERT_EQ(creative_inventory_item_id(1), 69);
	ASSERT_EQ(creative_inventory_item_id(2), 77);
	ASSERT_EQ(creative_inventory_item_id(3), 355);

	// Tail: the non-featured ids, ascending, after the featured block.
	ASSERT_EQ(creative_inventory_item_id(4), 5);
	ASSERT_EQ(creative_inventory_item_id(5), 17);

	// Out of range past the combined list -> 0.
	ASSERT_EQ(creative_inventory_item_id(6), 0);

	// Every id is emitted exactly once across the whole list: a featured id is
	// never re-emitted by the ascending tail (no duplicates), and a featured id
	// absent from the registry (minecart 328, boat 333, doors 324/330) never
	// surfaces -- the featured loop skips non-includable ids, count stays right.
	int hits[ITEMS_MAX] = {0};
	for(size_t i = 0; i < creative_inventory_count(); i++) {
		uint16_t id = creative_inventory_item_id(i);
		ASSERT(id > 0 && id < ITEMS_MAX);
		hits[id]++;
		ASSERT(hits[id] == 1);
	}
	ASSERT_EQ(hits[328], 0);
	ASSERT_EQ(hits[333], 0);
	ASSERT_EQ(hits[324], 0);
	ASSERT_EQ(hits[330], 0);
}

const test_entry_t g_tests_creative_inventory[] = {
	{"creative_count_skips_null_unrenderable_and_excluded",
	 test_creative_count_skips_null_unrenderable_and_excluded},
	{"creative_item_id_order_bounds_and_excludes",
	 test_creative_item_id_order_bounds_and_excludes},
	{"creative_item_id_features_placeables_first",
	 test_creative_item_id_features_placeables_first},
};

const size_t g_tests_creative_inventory_count
	= sizeof(g_tests_creative_inventory) / sizeof(g_tests_creative_inventory[0]);
