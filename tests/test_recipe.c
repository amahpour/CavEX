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

/* Builds the entire built-in crafting table (recipe_init + add_tools +
 * add_armor) and matches a handful of real recipes against it. */
TEST(recipe_init_full_table) {
	struct item_data slots[9] = {0};
	bool slot_empty[9];
	struct item_data result = {0};

	recipe_init();
	ASSERT(array_recipe_size(recipes_crafting) > 80);

	// planks from a single log (1x1 anywhere)
	memset(slot_empty, true, sizeof(slot_empty));
	slots[4] = (struct item_data) {.id = BLOCK_LOG};
	slot_empty[4] = false;
	ASSERT(recipe_match(recipes_crafting, slots, slot_empty, &result));
	ASSERT_EQ(result.id, BLOCK_PLANKS);
	ASSERT_EQ(result.count, 4U);

	// workbench from a 2x2 of planks
	memset(slots, 0, sizeof(slots));
	memset(slot_empty, true, sizeof(slot_empty));
	slots[0] = slots[1] = slots[3] = slots[4]
		= (struct item_data) {.id = BLOCK_PLANKS};
	slot_empty[0] = slot_empty[1] = slot_empty[3] = slot_empty[4] = false;
	ASSERT(recipe_match(recipes_crafting, slots, slot_empty, &result));
	ASSERT_EQ(result.id, BLOCK_WORKBENCH);

	// sticks from two stacked planks (mirror-symmetric vertical recipe)
	memset(slots, 0, sizeof(slots));
	memset(slot_empty, true, sizeof(slot_empty));
	slots[1] = slots[4] = (struct item_data) {.id = BLOCK_PLANKS};
	slot_empty[1] = slot_empty[4] = false;
	ASSERT(recipe_match(recipes_crafting, slots, slot_empty, &result));
	ASSERT_EQ(result.id, ITEM_STICK);

	// lapis block from a 3x3 of dye with durability 4: this is the one recipe
	// that matches on durability, exercising that comparison branch
	memset(slots, 0, sizeof(slots));
	memset(slot_empty, false, sizeof(slot_empty));
	for(size_t k = 0; k < 9; k++)
		slots[k] = (struct item_data) {.id = ITEM_DYE, .durability = 4};
	ASSERT(recipe_match(recipes_crafting, slots, slot_empty, &result));
	ASSERT_EQ(result.id, BLOCK_LAPIS_CAST);

	// same shape but the wrong durability no longer matches that recipe
	for(size_t k = 0; k < 9; k++)
		slots[k] = (struct item_data) {.id = ITEM_DYE, .durability = 0};
	ASSERT(!recipe_match(recipes_crafting, slots, slot_empty, &result));

	// a filled grid that matches nothing
	memset(slot_empty, false, sizeof(slot_empty));
	for(size_t k = 0; k < 9; k++)
		slots[k] = (struct item_data) {.id = BLOCK_DIRT};
	ASSERT(!recipe_match(recipes_crafting, slots, slot_empty, &result));

	// issue #29 decorative blocks: each must be craftable (reachable in normal
	// play). Each is a 2x2 of an existing block placed in the grid's top-left.
	struct {
		enum block_type ingredient;
		enum block_type result;
		uint8_t count;
	} decorative[] = {
		{BLOCK_STONE, BLOCK_SMOOTH_STONE, 4},
		{BLOCK_SANDSTONE, BLOCK_SMOOTH_SANDSTONE, 4},
		{BLOCK_LOG, BLOCK_OAK_WOOD, 3},
	};
	for(size_t c = 0; c < sizeof(decorative) / sizeof(decorative[0]); c++) {
		memset(slots, 0, sizeof(slots));
		memset(slot_empty, true, sizeof(slot_empty));
		slots[0] = slots[1] = slots[3] = slots[4]
			= (struct item_data) {.id = decorative[c].ingredient};
		slot_empty[0] = slot_empty[1] = slot_empty[3] = slot_empty[4] = false;

		ASSERT(recipe_match(recipes_crafting, slots, slot_empty, &result));
		ASSERT_EQ(result.id, (uint8_t)decorative[c].result);
		ASSERT_EQ(result.count, (uint8_t)decorative[c].count);
	}

	array_recipe_clear(recipes_crafting);
}

const test_entry_t g_tests_recipe[] = {
	{"recipe_match_shapes", test_recipe_match_shapes},
	{"recipe_match_no_match", test_recipe_match_no_match},
	{"recipe_init_full_table", test_recipe_init_full_table},
};

const size_t g_tests_recipe_count
	= sizeof(g_tests_recipe) / sizeof(g_tests_recipe[0]);
