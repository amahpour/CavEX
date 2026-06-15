#include <string.h>

#include "block/blocks.h"
#include "block/blocks_data.h"
#include "item/items.h"

struct item test_item_stack64 = {
	.max_stack = 64,
};

struct item* items[ITEMS_MAX];

void test_items_init(void) {
	memset(items, 0, sizeof(items));
	items[BLOCK_DIRT] = &test_item_stack64;
	items[BLOCK_PLANKS] = &test_item_stack64;
	items[BLOCK_LOG] = &test_item_stack64;
	items[ITEM_STICK] = &test_item_stack64;
}

struct item* item_get(struct item_data* item) {
	return item->id < ITEMS_MAX ? items[item->id] : NULL;
}

bool item_is_block(struct item_data* item) {
	return item_get(item) && item->id < 256 && blocks[item->id];
}
