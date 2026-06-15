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

TEST(inventory_accessors) {
	struct inventory inv = {0};
	struct item_data item = {0};

	test_items_init();
	ASSERT(inventory_create(&inv, NULL, NULL, INVENTORY_SIZE));

	// hotbar slot selection
	inventory_set_hotbar(&inv, 3);
	ASSERT_EQ(inventory_get_hotbar(&inv), 3U);

	inventory_set_slot(&inv, INVENTORY_SLOT_HOTBAR + 3,
					   (struct item_data) {.id = BLOCK_DIRT, .count = 5});
	ASSERT(inventory_get_hotbar_item(&inv, &item));
	ASSERT_EQ(item.id, BLOCK_DIRT);
	ASSERT_EQ(item.count, 5U);

	// clearing a slot
	inventory_clear_slot(&inv, INVENTORY_SLOT_HOTBAR + 3);
	ASSERT(!inventory_get_slot(&inv, INVENTORY_SLOT_HOTBAR + 3, NULL));

	// picked item set/get/clear
	ASSERT(!inventory_get_picked_item(&inv, NULL));
	inventory_set_picked_item(&inv, (struct item_data) {.id = BLOCK_LOG, .count = 2});
	ASSERT(inventory_get_picked_item(&inv, &item));
	ASSERT_EQ(item.id, BLOCK_LOG);
	inventory_clear_picked_item(&inv);
	ASSERT(!inventory_get_picked_item(&inv, NULL));

	// consume down to empty
	inventory_set_slot(&inv, 0, (struct item_data) {.id = BLOCK_DIRT, .count = 2});
	inventory_consume(&inv, 0);
	ASSERT(inventory_get_slot(&inv, 0, &item));
	ASSERT_EQ(item.count, 1U);
	inventory_consume(&inv, 0);
	ASSERT(!inventory_get_slot(&inv, 0, NULL));
	inventory_consume(&inv, 0); // no-op on empty slot

	inventory_destroy(&inv);
}

TEST(inventory_copy_independent) {
	struct inventory a = {0};
	struct inventory b = {0};
	struct item_data item = {0};

	test_items_init();
	ASSERT(inventory_create(&a, NULL, NULL, 9));
	ASSERT(inventory_create(&b, NULL, NULL, 9));

	inventory_set_slot(&a, 2, (struct item_data) {.id = BLOCK_DIRT, .count = 4});
	a.hotbar_slot = 1;
	inventory_copy(&b, &a);

	ASSERT(inventory_get_slot(&b, 2, &item));
	ASSERT_EQ(item.id, BLOCK_DIRT);
	ASSERT_EQ(b.hotbar_slot, 1);

	inventory_destroy(&a);
	inventory_destroy(&b);
}

TEST(inventory_right_click_split_and_place) {
	struct inventory inv = {0};
	struct item_data item = {0};

	test_items_init();
	ASSERT(inventory_create(&inv, NULL, NULL, 9));

	// right-click a stack of 8 -> picks up half (rounded up)
	inventory_set_slot(&inv, 0, (struct item_data) {.id = BLOCK_DIRT, .count = 8});
	ASSERT(inventory_action(&inv, 0, true, NULL));
	ASSERT(inventory_get_picked_item(&inv, &item));
	ASSERT_EQ(item.count, 4U);
	ASSERT(inventory_get_slot(&inv, 0, &item));
	ASSERT_EQ(item.count, 4U);

	// right-click an empty slot -> deposits one
	ASSERT(inventory_action(&inv, 1, true, NULL));
	ASSERT(inventory_get_slot(&inv, 1, &item));
	ASSERT_EQ(item.count, 1U);
	ASSERT(inventory_get_picked_item(&inv, &item));
	ASSERT_EQ(item.count, 3U);

	// right-click a matching slot -> stacks one more
	ASSERT(inventory_action(&inv, 1, true, NULL));
	ASSERT(inventory_get_slot(&inv, 1, &item));
	ASSERT_EQ(item.count, 2U);

	// right-click a differing slot while holding -> rejected
	inventory_set_slot(&inv, 2, (struct item_data) {.id = BLOCK_LOG, .count = 1});
	ASSERT(!inventory_action(&inv, 2, true, NULL));

	inventory_destroy(&inv);
}

TEST(inventory_left_click_stack_and_swap) {
	struct inventory inv = {0};
	struct item_data item = {0};

	test_items_init();
	ASSERT(inventory_create(&inv, NULL, NULL, 9));

	// pick up 60 dirt, drop onto 10 dirt -> stacks to 64, 6 remain held
	inventory_set_slot(&inv, 0, (struct item_data) {.id = BLOCK_DIRT, .count = 60});
	ASSERT(inventory_action(&inv, 0, false, NULL));
	inventory_set_slot(&inv, 1, (struct item_data) {.id = BLOCK_DIRT, .count = 10});
	ASSERT(inventory_action(&inv, 1, false, NULL));
	ASSERT(inventory_get_slot(&inv, 1, &item));
	ASSERT_EQ(item.count, 64U);
	ASSERT(inventory_get_picked_item(&inv, &item));
	ASSERT_EQ(item.count, 6U);

	// drop remainder onto a different item -> swap
	inventory_set_slot(&inv, 2, (struct item_data) {.id = BLOCK_LOG, .count = 1});
	ASSERT(inventory_action(&inv, 2, false, NULL));
	ASSERT(inventory_get_slot(&inv, 2, &item));
	ASSERT_EQ(item.id, BLOCK_DIRT);
	ASSERT_EQ(item.count, 6U);
	ASSERT(inventory_get_picked_item(&inv, &item));
	ASSERT_EQ(item.id, BLOCK_LOG);

	inventory_destroy(&inv);
}

static int logic_created;
static int logic_destroyed;
static int logic_pre;
static int logic_post;
static bool logic_allow;

static void lg_on_create(struct inventory* inv) {
	(void)inv;
	logic_created++;
}
static bool lg_on_destroy(struct inventory* inv) {
	(void)inv;
	logic_destroyed++;
	return true; // ask inventory_destroy to free(inv)
}
static bool lg_pre(struct inventory* inv, size_t slot, bool right,
				   set_inv_slot_t changes) {
	(void)inv;
	(void)slot;
	(void)right;
	(void)changes;
	logic_pre++;
	return logic_allow;
}
static void lg_post(struct inventory* inv, size_t slot, bool right, bool accepted,
					set_inv_slot_t changes) {
	(void)inv;
	(void)slot;
	(void)right;
	(void)accepted;
	(void)changes;
	logic_post++;
}

TEST(inventory_logic_hooks) {
	static struct inventory_logic logic = {
		.on_create = lg_on_create,
		.on_destroy = lg_on_destroy,
		.pre_action = lg_pre,
		.post_action = lg_post,
	};

	logic_created = logic_destroyed = logic_pre = logic_post = 0;
	logic_allow = false;

	// heap-allocated because on_destroy returns true (frees inv)
	struct inventory* inv = malloc(sizeof(struct inventory));
	ASSERT(inv != NULL);
	test_items_init();
	ASSERT(inventory_create(inv, &logic, NULL, 9));
	ASSERT_EQ(logic_created, 1);

	inventory_set_slot(inv, 0, (struct item_data) {.id = BLOCK_DIRT, .count = 4});

	// pre_action vetoes -> action rejected, nothing picked up
	ASSERT(!inventory_action(inv, 0, false, NULL));
	ASSERT_EQ(logic_pre, 1);
	ASSERT_EQ(logic_post, 1);
	ASSERT(!inventory_get_picked_item(inv, NULL));

	// allow it through this time
	logic_allow = true;
	ASSERT(inventory_action(inv, 0, false, NULL));
	ASSERT(inventory_get_picked_item(inv, NULL));

	inventory_destroy(inv);
	ASSERT_EQ(logic_destroyed, 1);
}

TEST(inventory_action_edge_cases) {
	struct inventory inv = {0};
	struct item_data item = {0};

	test_items_init();
	ASSERT(inventory_create(&inv, NULL, NULL, 9));

	// right-click a single item -> whole item is picked up (split of 1)
	inventory_set_slot(&inv, 0, (struct item_data) {.id = BLOCK_DIRT, .count = 1});
	ASSERT(inventory_action(&inv, 0, true, NULL));
	ASSERT(inventory_get_picked_item(&inv, &item));
	ASSERT_EQ(item.count, 1U);
	ASSERT(!inventory_get_slot(&inv, 0, NULL));

	// right-click into the last empty slot deposits the final unit and clears
	// the picked item entirely
	ASSERT(inventory_action(&inv, 1, true, NULL));
	ASSERT(!inventory_get_picked_item(&inv, NULL));
	ASSERT(inventory_get_slot(&inv, 1, &item));
	ASSERT_EQ(item.count, 1U);

	// right-click an empty slot with nothing held -> rejected (split of empty)
	ASSERT(!inventory_action(&inv, 5, true, NULL));

	// left-click stacking that does NOT overflow merges fully
	inventory_set_slot(&inv, 2, (struct item_data) {.id = BLOCK_DIRT, .count = 20});
	ASSERT(inventory_action(&inv, 2, false, NULL)); // pick 20
	inventory_set_slot(&inv, 3, (struct item_data) {.id = BLOCK_DIRT, .count = 10});
	ASSERT(inventory_action(&inv, 3, false, NULL)); // drop onto 10 -> 30
	ASSERT(inventory_get_slot(&inv, 3, &item));
	ASSERT_EQ(item.count, 30U);
	ASSERT(!inventory_get_picked_item(&inv, NULL));

	inventory_destroy(&inv);
}

TEST(inventory_collect_no_space) {
	struct inventory inv = {0};
	set_inv_slot_t changes;
	struct item_data stack = {.id = BLOCK_DIRT, .count = 5};
	struct item_data unknown = {.id = 250, .count = 1};

	test_items_init();
	ASSERT(inventory_create(&inv, NULL, NULL, 2));
	set_inv_slot_init(changes);

	// fill both slots with a different item so collect can't place
	inventory_set_slot(&inv, 0, (struct item_data) {.id = BLOCK_LOG, .count = 64});
	inventory_set_slot(&inv, 1, (struct item_data) {.id = BLOCK_LOG, .count = 64});
	uint8_t order[] = {0, 1};
	ASSERT(!inventory_collect(&inv, &stack, order, 2, changes));

	// an item with no item_get entry is rejected outright
	ASSERT(!inventory_collect(&inv, &unknown, order, 2, changes));

	set_inv_slot_clear(changes);
	inventory_destroy(&inv);
}

const test_entry_t g_tests_inventory[] = {
	{"inventory_pick_and_place", test_inventory_pick_and_place},
	{"inventory_collect_stacks", test_inventory_collect_stacks},
	{"inventory_accessors", test_inventory_accessors},
	{"inventory_copy_independent", test_inventory_copy_independent},
	{"inventory_right_click_split_and_place",
	 test_inventory_right_click_split_and_place},
	{"inventory_left_click_stack_and_swap",
	 test_inventory_left_click_stack_and_swap},
	{"inventory_logic_hooks", test_inventory_logic_hooks},
	{"inventory_action_edge_cases", test_inventory_action_edge_cases},
	{"inventory_collect_no_space", test_inventory_collect_no_space},
};

const size_t g_tests_inventory_count
	= sizeof(g_tests_inventory) / sizeof(g_tests_inventory[0]);
