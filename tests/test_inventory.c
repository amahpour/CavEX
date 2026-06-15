#include <stdlib.h>
#include <string.h>

#include "block/blocks_data.h"
#include "harness.h"
#include "item/inventory.h"
#include "network/inventory_logic.h"
#include "stubs/items_stub.h"

TEST(inventory_pick_and_place) {
	struct inventory inv = {0};

	test_items_init();
	ASSERT(inventory_create(&inv, NULL, NULL, 9));

	inventory_set_slot(&inv, 2,
					   (struct item_data) {.id = BLOCK_DIRT, .count = 8});
	ASSERT(inventory_action(&inv, 2, false, NULL));

	struct item_data picked = {0};
	ASSERT(inventory_get_picked_item(&inv, &picked));
	ASSERT_EQ(picked.id, BLOCK_DIRT);
	ASSERT_EQ(picked.count, 8U);

	ASSERT(inventory_action(&inv, 4, false, NULL));
	ASSERT(inventory_get_slot(&inv, 4, &picked));
	ASSERT_EQ(picked.count, 8U);
	ASSERT(!inventory_get_picked_item(&inv, NULL));

	inventory_consume(&inv, 4);
	ASSERT(inventory_get_slot(&inv, 4, &picked));
	ASSERT_EQ(picked.count, 7U);

	inventory_destroy(&inv);
}

TEST(inventory_collect_stacks) {
	struct inventory inv = {0};
	set_inv_slot_t changes;
	struct item_data stack = {.id = BLOCK_DIRT, .count = 70};

	test_items_init();
	ASSERT(inventory_create(&inv, NULL, NULL, 9));
	inventory_set_slot(&inv, 0, (struct item_data) {.id = BLOCK_DIRT, .count = 50});

	set_inv_slot_init(changes);
	uint8_t order[] = {0, 1, 2};
	ASSERT(inventory_collect(&inv, &stack, order, 3, changes));
	ASSERT_EQ(stack.count, 0U);
	ASSERT(inventory_get_slot(&inv, 0, &stack));
	ASSERT_EQ(stack.count, 64U);
	ASSERT(inventory_get_slot(&inv, 1, &stack));
	ASSERT_EQ(stack.count, 56U);

	set_inv_slot_clear(changes);
	inventory_destroy(&inv);
}

const test_entry_t g_tests_inventory[] = {
	{"inventory_pick_and_place", test_inventory_pick_and_place},
	{"inventory_collect_stacks", test_inventory_collect_stacks},
};

const size_t g_tests_inventory_count
	= sizeof(g_tests_inventory) / sizeof(g_tests_inventory[0]);
