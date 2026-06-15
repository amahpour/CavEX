#include <string.h>

#include "block/blocks_data.h"
#include "harness.h"
#include "item/recipe.h"

TEST(recipe_match_shapes) {
	array_recipe_t recipes;
	struct item_data slots[9] = {0};
	bool slot_empty[9];
	struct item_data result = {0};
	uint8_t log_shape[] = {1};
	uint8_t stick_shape[] = {1, 1};

	memset(slot_empty, true, sizeof(slot_empty));
	array_recipe_init(recipes);
	recipe_add(recipes,
			   (struct item_data) {.id = BLOCK_PLANKS, .count = 4},
			   1, 1, log_shape, (struct item_data) {.id = BLOCK_LOG}, false);
	recipe_add(recipes,
			   (struct item_data) {.id = ITEM_STICK, .count = 4},
			   1, 2, stick_shape, (struct item_data) {.id = BLOCK_PLANKS},
			   false);

	slots[0] = (struct item_data) {.id = BLOCK_LOG};
	slot_empty[0] = false;
	ASSERT(recipe_match(recipes, slots, slot_empty, &result));
	ASSERT_EQ(result.id, BLOCK_PLANKS);
	ASSERT_EQ(result.count, 4U);

	memset(slots, 0, sizeof(slots));
	memset(slot_empty, true, sizeof(slot_empty));
	slots[0] = (struct item_data) {.id = BLOCK_PLANKS};
	slots[3] = (struct item_data) {.id = BLOCK_PLANKS};
	slot_empty[0] = slot_empty[3] = false;
	ASSERT(recipe_match(recipes, slots, slot_empty, &result));
	ASSERT_EQ(result.id, ITEM_STICK);
	ASSERT_EQ(result.count, 4U);

	array_recipe_clear(recipes);
}

TEST(recipe_match_no_match) {
	array_recipe_t recipes;
	struct item_data slots[9] = {0};
	bool slot_empty[9];
	struct item_data result = {0};
	uint8_t shape[] = {1};

	memset(slot_empty, true, sizeof(slot_empty));
	array_recipe_init(recipes);
	recipe_add(recipes,
			   (struct item_data) {.id = BLOCK_PLANKS, .count = 4},
			   1, 1, shape, (struct item_data) {.id = BLOCK_LOG}, false);

	slots[0] = (struct item_data) {.id = BLOCK_DIRT};
	slot_empty[0] = false;

	ASSERT(!recipe_match(recipes, slots, slot_empty, &result));

	array_recipe_clear(recipes);
}

const test_entry_t g_tests_recipe[] = {
	{"recipe_match_shapes", test_recipe_match_shapes},
	{"recipe_match_no_match", test_recipe_match_no_match},
};

const size_t g_tests_recipe_count
	= sizeof(g_tests_recipe) / sizeof(g_tests_recipe[0]);
