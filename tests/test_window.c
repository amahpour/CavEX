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

const test_entry_t g_tests_window[] = {
	{"window_container_slot_change", test_window_container_slot_change},
};

const size_t g_tests_window_count
	= sizeof(g_tests_window) / sizeof(g_tests_window[0]);
