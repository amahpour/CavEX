#include "block/blocks_data.h"
#include "harness.h"
#include "item/inventory.h"
#include "item/window_container.h"
#include "stubs/items_stub.h"

TEST(window_container_slot_change) {
	struct window_container wc = {0};
	struct item_data item = {0};

	test_items_init();
	ASSERT(windowc_create(&wc, WINDOW_TYPE_INVENTORY, 9));
	windowc_slot_change(&wc, 1,
						(struct item_data) {.id = BLOCK_DIRT, .count = 3});

	ASSERT(inventory_get_slot(windowc_get_latest(&wc), 1, &item));
	ASSERT_EQ(item.id, BLOCK_DIRT);
	ASSERT_EQ(item.count, 3U);

	windowc_destroy(&wc);
}

TEST(window_container_picked_slot_change) {
	struct window_container wc = {0};
	struct item_data item = {0};

	test_items_init();
	ASSERT(windowc_create(&wc, WINDOW_TYPE_INVENTORY, 9));

	windowc_slot_change(&wc, SPECIAL_SLOT_PICKED_ITEM,
						(struct item_data) {.id = BLOCK_LOG, .count = 1});
	ASSERT(inventory_get_picked_item(windowc_get_latest(&wc), &item));
	ASSERT_EQ(item.id, BLOCK_LOG);

	windowc_destroy(&wc);
}

TEST(window_container_action_history) {
	struct window_container wc = {0};
	uint16_t id1 = 0, id2 = 0, id3 = 0;

	test_items_init();
	ASSERT(windowc_create(&wc, WINDOW_TYPE_INVENTORY, 9));

	// queue three speculative actions on top of the initial revision
	ASSERT(windowc_new_action(&wc, &id1, false, 0));
	ASSERT(windowc_new_action(&wc, &id2, false, 1));
	ASSERT(windowc_new_action(&wc, &id3, false, 2));
	ASSERT_NE(id1, id2);
	ASSERT_NE(id2, id3);

	// a base-slot change must re-propagate through every queued revision
	struct item_data item = {0};
	windowc_slot_change(&wc, 4,
						(struct item_data) {.id = BLOCK_DIRT, .count = 2});
	ASSERT(inventory_get_slot(windowc_get_latest(&wc), 4, &item));
	ASSERT_EQ(item.id, BLOCK_DIRT);

	// server accepts id1: everything strictly before id1 is dropped
	windowc_action_apply_result(&wc, id1, true);
	ASSERT(windowc_get_latest(&wc) != NULL);

	// server rejects id3: id3 and everything after it is dropped
	windowc_action_apply_result(&wc, id3, false);
	ASSERT(windowc_get_latest(&wc) != NULL);

	windowc_destroy(&wc);
}

const test_entry_t g_tests_window[] = {
	{"window_container_slot_change", test_window_container_slot_change},
	{"window_container_picked_slot_change",
	 test_window_container_picked_slot_change},
	{"window_container_action_history", test_window_container_action_history},
};

const size_t g_tests_window_count
	= sizeof(g_tests_window) / sizeof(g_tests_window[0]);
