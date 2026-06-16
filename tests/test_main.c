#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "harness.h"

extern const test_entry_t g_tests_stack[];
extern const size_t g_tests_stack_count;

extern const test_entry_t g_tests_util[];
extern const size_t g_tests_util_count;

extern const test_entry_t g_tests_aabb[];
extern const size_t g_tests_aabb_count;

extern const test_entry_t g_tests_blocks[];
extern const size_t g_tests_blocks_count;

extern const test_entry_t g_tests_face_occlusion[];
extern const size_t g_tests_face_occlusion_count;

extern const test_entry_t g_tests_buffer[];
extern const size_t g_tests_buffer_count;

extern const test_entry_t g_tests_daytime[];
extern const size_t g_tests_daytime_count;

extern const test_entry_t g_tests_config[];
extern const size_t g_tests_config_count;

extern const test_entry_t g_tests_json[];
extern const size_t g_tests_json_count;

extern const test_entry_t g_tests_nbt[];
extern const size_t g_tests_nbt_count;

extern const test_entry_t g_tests_lighting[];
extern const size_t g_tests_lighting_count;

extern const test_entry_t g_tests_recipe[];
extern const size_t g_tests_recipe_count;

extern const test_entry_t g_tests_tool[];
extern const size_t g_tests_tool_count;

extern const test_entry_t g_tests_entity[];
extern const size_t g_tests_entity_count;

extern const test_entry_t g_tests_double_tap[];
extern const size_t g_tests_double_tap_count;

extern const test_entry_t g_tests_map[];
extern const size_t g_tests_map_count;

extern const test_entry_t g_tests_chunk[];
extern const size_t g_tests_chunk_count;

extern const test_entry_t g_tests_inventory[];
extern const size_t g_tests_inventory_count;

extern const test_entry_t g_tests_window[];
extern const size_t g_tests_window_count;

extern const test_entry_t g_tests_level[];
extern const size_t g_tests_level_count;

extern const test_entry_t g_tests_region[];
extern const size_t g_tests_region_count;

extern const test_entry_t g_tests_demo[];
extern const size_t g_tests_demo_count;

extern const test_entry_t g_tests_state_export[];
extern const size_t g_tests_state_export_count;

extern const test_entry_t g_tests_firework[];
extern const size_t g_tests_firework_count;

const char* g_current_test = "";

typedef struct {
	const test_entry_t* entries;
	size_t count;
} test_group_t;

static test_group_t groups[24];
static const size_t group_count = 24;

static void init_groups(void) {
	groups[0] = (test_group_t){g_tests_stack, g_tests_stack_count};
	groups[1] = (test_group_t){g_tests_util, g_tests_util_count};
	groups[2] = (test_group_t){g_tests_aabb, g_tests_aabb_count};
	groups[3] = (test_group_t){g_tests_blocks, g_tests_blocks_count};
	groups[4] = (test_group_t){g_tests_face_occlusion, g_tests_face_occlusion_count};
	groups[5] = (test_group_t){g_tests_buffer, g_tests_buffer_count};
	groups[6] = (test_group_t){g_tests_daytime, g_tests_daytime_count};
	groups[7] = (test_group_t){g_tests_config, g_tests_config_count};
	groups[8] = (test_group_t){g_tests_nbt, g_tests_nbt_count};
	groups[9] = (test_group_t){g_tests_lighting, g_tests_lighting_count};
	groups[10] = (test_group_t){g_tests_recipe, g_tests_recipe_count};
	groups[11] = (test_group_t){g_tests_tool, g_tests_tool_count};
	groups[12] = (test_group_t){g_tests_entity, g_tests_entity_count};
	groups[13] = (test_group_t){g_tests_chunk, g_tests_chunk_count};
	groups[14] = (test_group_t){g_tests_inventory, g_tests_inventory_count};
	groups[15] = (test_group_t){g_tests_window, g_tests_window_count};
	groups[16] = (test_group_t){g_tests_level, g_tests_level_count};
	groups[17] = (test_group_t){g_tests_region, g_tests_region_count};
	groups[18] = (test_group_t){g_tests_json, g_tests_json_count};
	groups[19]
		= (test_group_t){g_tests_double_tap, g_tests_double_tap_count};
	groups[20] = (test_group_t){g_tests_demo, g_tests_demo_count};
	groups[21]
		= (test_group_t){g_tests_state_export, g_tests_state_export_count};
	groups[22] = (test_group_t){g_tests_firework, g_tests_firework_count};
	groups[23] = (test_group_t){g_tests_map, g_tests_map_count};
}

static void list_tests(const test_entry_t* entries, size_t count) {
	for(size_t k = 0; k < count; k++)
		printf("%s\n", entries[k].name);
}

static int run_one(const char* name, const test_entry_t* entries, size_t count) {
	for(size_t k = 0; k < count; k++) {
		if(strcmp(entries[k].name, name) == 0) {
			g_current_test = entries[k].name;
			entries[k].fn();
			return 0;
		}
	}

	return -1;
}

static int run_all(const test_entry_t* entries, size_t count) {
	for(size_t k = 0; k < count; k++) {
		g_current_test = entries[k].name;
		entries[k].fn();
	}

	return 0;
}

static void usage(const char* argv0) {
	fprintf(stderr, "Usage: %s [--list | --run <name>]\n", argv0);
}

int main(int argc, char** argv) {
	init_groups();

	if(argc == 2 && strcmp(argv[1], "--list") == 0) {
		for(size_t g = 0; g < group_count; g++)
			list_tests(groups[g].entries, groups[g].count);
		return 0;
	}

	if(argc == 3 && strcmp(argv[1], "--run") == 0) {
		for(size_t g = 0; g < group_count; g++) {
			if(run_one(argv[2], groups[g].entries, groups[g].count) == 0)
				return 0;
		}

		fprintf(stderr, "unknown test: %s\n", argv[2]);
		return 1;
	}

	if(argc != 1) {
		usage(argv[0]);
		return 1;
	}

	for(size_t g = 0; g < group_count; g++)
		run_all(groups[g].entries, groups[g].count);

	return 0;
}
