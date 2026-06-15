#include <stdlib.h>

#include "harness.h"
#include "network/region_archive.h"

TEST(region_archive_contains_chunk) {
	struct region_archive ra = {
		.x = 0,
		.z = 0,
		.offsets = calloc(REGION_SIZE * REGION_SIZE, sizeof(uint32_t)),
	};
	bool exists = false;

	ASSERT(ra.offsets != NULL);
	ra.offsets[0] = (2U << 8) | 1U;

	ASSERT(region_archive_contains(&ra, 0, 0, &exists));
	ASSERT(exists);
	ASSERT(region_archive_contains(&ra, 31, 31, &exists));
	ASSERT(!exists);
	ASSERT(!region_archive_contains(&ra, 32, 0, &exists));

	free(ra.offsets);
}

const test_entry_t g_tests_region[] = {
	{"region_archive_contains_chunk", test_region_archive_contains_chunk},
};

const size_t g_tests_region_count
	= sizeof(g_tests_region) / sizeof(g_tests_region[0]);
