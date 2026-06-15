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

extern const test_entry_t g_tests_face_occlusion[];
extern const size_t g_tests_face_occlusion_count;

extern const test_entry_t g_tests_buffer[];
extern const size_t g_tests_buffer_count;

const char* g_current_test = "";

typedef struct {
	const test_entry_t* entries;
	size_t count;
} test_group_t;

static test_group_t groups[5];
static const size_t group_count = 5;

static void init_groups(void) {
	groups[0] = (test_group_t){g_tests_stack, g_tests_stack_count};
	groups[1] = (test_group_t){g_tests_util, g_tests_util_count};
	groups[2] = (test_group_t){g_tests_aabb, g_tests_aabb_count};
	groups[3] = (test_group_t){g_tests_face_occlusion, g_tests_face_occlusion_count};
	groups[4] = (test_group_t){g_tests_buffer, g_tests_buffer_count};
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
