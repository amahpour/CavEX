#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "harness.h"
#include "network/region_archive.h"
#include "network/server_world.h"
#include "world.h"

#define BLOCKS_LEN (CHUNK_SIZE * CHUNK_SIZE * WORLD_HEIGHT)
#define HALF_LEN (CHUNK_SIZE * CHUNK_SIZE * WORLD_HEIGHT / 2)
#define HEIGHT_LEN (CHUNK_SIZE * CHUNK_SIZE)

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

static void make_temp_world(char* dir, size_t length) {
	char tmpl[] = "/tmp/cavex_region_XXXXXX";
	char* base = mkdtemp(tmpl);
	ASSERT(base != NULL);
	snprintf(dir, length, "%s", base);

	char region_dir[512];
	snprintf(region_dir, sizeof(region_dir), "%s/region", base);
	ASSERT(mkdir(region_dir, 0777) == 0);
}

static struct server_chunk alloc_chunk(void) {
	struct server_chunk sc = {0};
	sc.ids = calloc(1, BLOCKS_LEN);
	sc.metadata = calloc(1, HALF_LEN);
	sc.lighting_sky = calloc(1, HALF_LEN);
	sc.lighting_torch = calloc(1, HALF_LEN);
	sc.heightmap = calloc(1, HEIGHT_LEN);
	ASSERT(sc.ids && sc.metadata && sc.lighting_sky && sc.lighting_torch
		   && sc.heightmap);
	return sc;
}

static void free_chunk(struct server_chunk* sc) {
	free(sc->ids);
	free(sc->metadata);
	free(sc->lighting_sky);
	free(sc->lighting_torch);
	free(sc->heightmap);
}

TEST(region_archive_create_missing) {
	struct region_archive ra = {0};
	string_t world;
	string_init_printf(world, "/tmp/cavex_region_does_not_exist_%i", (int)getpid());

	// no file present -> create fails
	ASSERT(!region_archive_create(&ra, world, 0, 0, WORLD_DIM_OVERWORLD));

	string_clear(world);
}

TEST(region_archive_roundtrip_blocks) {
	char dir[512];
	struct region_archive ra = {0};
	string_t world;

	make_temp_world(dir, sizeof(dir));
	string_init_printf(world, "%s", dir);

	ASSERT(region_archive_create_new(&ra, world, 0, 0, WORLD_DIM_OVERWORLD));

	// initially the chunk is empty on disk
	bool exists = true;
	ASSERT(region_archive_contains(&ra, 0, 0, &exists));
	ASSERT(!exists);

	struct server_chunk out = alloc_chunk();
	out.ids[0] = 1;
	out.ids[100] = 7;
	out.ids[BLOCKS_LEN - 1] = 42;
	out.metadata[5] = 0xAB;
	out.heightmap[3] = 64;

	ASSERT(region_archive_set_blocks(&ra, 0, 0, &out));

	// now the chunk exists
	ASSERT(region_archive_contains(&ra, 0, 0, &exists));
	ASSERT(exists);

	struct server_chunk in = {0};
	ASSERT(region_archive_get_blocks(&ra, 0, 0, &in));
	ASSERT_EQ(in.ids[0], 1);
	ASSERT_EQ(in.ids[100], 7);
	ASSERT_EQ(in.ids[BLOCKS_LEN - 1], 42);
	ASSERT_EQ(in.metadata[5], 0xAB);
	ASSERT_EQ(in.heightmap[3], 64);

	// get_blocks borrows the arrays out of the NBT tree; free them here
	free_chunk(&in);

	// overwrite the same chunk again (exercises the in-place update path)
	out.ids[0] = 9;
	ASSERT(region_archive_set_blocks(&ra, 0, 0, &out));

	struct server_chunk in2 = {0};
	ASSERT(region_archive_get_blocks(&ra, 0, 0, &in2));
	ASSERT_EQ(in2.ids[0], 9);
	free_chunk(&in2);

	// write two more distinct chunks in the same region; this drives the
	// "append at the end of the file" placement loop with a growing occupancy
	// list
	for(w_coord_t cx = 1; cx <= 2; cx++) {
		out.ids[0] = (uint8_t)(50 + cx);
		ASSERT(region_archive_set_blocks(&ra, cx, 0, &out));

		struct server_chunk extra = {0};
		ASSERT(region_archive_get_blocks(&ra, cx, 0, &extra));
		ASSERT_EQ(extra.ids[0], (uint8_t)(50 + cx));
		free_chunk(&extra);
	}

	free_chunk(&out);
	region_archive_destroy(&ra);

	// reopen the freshly written file via region_archive_create
	struct region_archive ra2 = {0};
	ASSERT(region_archive_create(&ra2, world, 0, 0, WORLD_DIM_OVERWORLD));
	ASSERT(region_archive_contains(&ra2, 0, 0, &exists));
	ASSERT(exists);
	region_archive_destroy(&ra2);

	string_clear(world);
}

TEST(region_archive_nether_roundtrip) {
	char dir[512];
	struct region_archive ra = {0};
	string_t world;

	// the nether dimension stores regions under DIM-1/region/
	char tmpl[] = "/tmp/cavex_region_nether_XXXXXX";
	char* base = mkdtemp(tmpl);
	ASSERT(base != NULL);
	snprintf(dir, sizeof(dir), "%s/DIM-1", base);
	ASSERT(mkdir(dir, 0777) == 0);
	snprintf(dir, sizeof(dir), "%s/DIM-1/region", base);
	ASSERT(mkdir(dir, 0777) == 0);

	string_init_printf(world, "%s", base);
	ASSERT(region_archive_create_new(&ra, world, 0, 0, WORLD_DIM_NETHER));

	struct server_chunk out = alloc_chunk();
	out.ids[7] = 88;
	ASSERT(region_archive_set_blocks(&ra, 0, 0, &out));

	struct server_chunk in = {0};
	ASSERT(region_archive_get_blocks(&ra, 0, 0, &in));
	ASSERT_EQ(in.ids[7], 88);

	free_chunk(&in);
	free_chunk(&out);
	region_archive_destroy(&ra);
	string_clear(world);
}

const test_entry_t g_tests_region[] = {
	{"region_archive_contains_chunk", test_region_archive_contains_chunk},
	{"region_archive_create_missing", test_region_archive_create_missing},
	{"region_archive_roundtrip_blocks", test_region_archive_roundtrip_blocks},
	{"region_archive_nether_roundtrip", test_region_archive_nether_roundtrip},
};

const size_t g_tests_region_count
	= sizeof(g_tests_region) / sizeof(g_tests_region[0]);
