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

	ASSERT_EQ(tool_dig_delay_ms(&stone, &iron_pick), 250);
	ASSERT_EQ(tool_dig_delay_ms(&stone, NULL), 5000);

	struct block instant = {.digging = {.hardness = 0}};
	ASSERT_EQ(tool_dig_delay_ms(&instant, NULL), -1);

	struct item sword = {.tool = {.type = TOOL_TYPE_SWORD, .tier = TOOL_TIER_IRON}};
	ASSERT_EQ(tool_dig_delay_ms(&stone, &sword), 1000);
}

const test_entry_t g_tests_tool[] = {
	{"tool_tier_divider_values", test_tool_tier_divider_values},
	{"tool_dig_delay_cases", test_tool_dig_delay_cases},
};

const size_t g_tests_tool_count = sizeof(g_tests_tool) / sizeof(g_tests_tool[0]);
