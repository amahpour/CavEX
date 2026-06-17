#include "block/blocks.h"
#include "harness.h"
#include "item/tool.h"

TEST(tool_tier_divider_values) {
	ASSERT_EQ(tool_tier_divider(TOOL_TIER_WOOD), 2);
	ASSERT_EQ(tool_tier_divider(TOOL_TIER_STONE), 4);
	ASSERT_EQ(tool_tier_divider(TOOL_TIER_IRON), 6);
	ASSERT_EQ(tool_tier_divider(TOOL_TIER_DIAMOND), 8);
	ASSERT_EQ(tool_tier_divider(TOOL_TIER_GOLD), 12);
	ASSERT_EQ(tool_tier_divider(TOOL_TIER_ANY), 1);
}

TEST(tool_dig_delay_cases) {
	struct block stone = {
		.digging = {
			.hardness = 1500,
			.tool = TOOL_TYPE_PICKAXE,
			.min = TOOL_TIER_WOOD,
			.best = TOOL_TIER_DIAMOND,
		},
	};
	struct item iron_pick = {
		.tool = {.type = TOOL_TYPE_PICKAXE, .tier = TOOL_TIER_IRON},
	};

	ASSERT_EQ(tool_dig_delay_ms(&stone, &iron_pick, false), 250);
	ASSERT_EQ(tool_dig_delay_ms(&stone, NULL, false), 5000);

	struct block instant = {.digging = {.hardness = 0}};
	ASSERT_EQ(tool_dig_delay_ms(&instant, NULL, false), -1);

	struct item sword = {.tool = {.type = TOOL_TYPE_SWORD, .tier = TOOL_TIER_IRON}};
	ASSERT_EQ(tool_dig_delay_ms(&stone, &sword, false), 1000);
}

TEST(tool_dig_delay_creative) {
	// Creative mode breaks any breakable block instantly, regardless of the
	// held tool (issue #21).
	struct block stone = {
		.digging = {
			.hardness = 1500,
			.tool = TOOL_TYPE_PICKAXE,
			.min = TOOL_TIER_WOOD,
			.best = TOOL_TIER_DIAMOND,
		},
	};
	struct item iron_pick = {
		.tool = {.type = TOOL_TYPE_PICKAXE, .tier = TOOL_TIER_IRON},
	};

	ASSERT_EQ(tool_dig_delay_ms(&stone, &iron_pick, true), 0);
	ASSERT_EQ(tool_dig_delay_ms(&stone, NULL, true), 0);

	// Unbreakable blocks (hardness <= 0, e.g. bedrock) stay unbreakable even in
	// creative: the -1 guard runs before the creative shortcut.
	struct block unbreakable = {.digging = {.hardness = 0}};
	ASSERT_EQ(tool_dig_delay_ms(&unbreakable, NULL, true), -1);
}

const test_entry_t g_tests_tool[] = {
	{"tool_tier_divider_values", test_tool_tier_divider_values},
	{"tool_dig_delay_cases", test_tool_dig_delay_cases},
	{"tool_dig_delay_creative", test_tool_dig_delay_creative},
};

const size_t g_tests_tool_count = sizeof(g_tests_tool) / sizeof(g_tests_tool[0]);
