#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "block/blocks_data.h"
#include "harness.h"
#include "lighting.h"
#include "stubs/blocks_stub.h"
#include "util.h"
#include "world.h"

#define HEIGHTMAP_SIZE (CHUNK_SIZE * CHUNK_SIZE)

typedef struct {
	uint8_t types[CHUNK_SIZE][CHUNK_SIZE][16];
} fake_column;

static bool fake_get_block(void* user, c_coord_t x, w_coord_t y, c_coord_t z,
						   struct block_data* blk) {
	fake_column* column = user;

	if(x >= CHUNK_SIZE || z >= CHUNK_SIZE || y >= 16)
		return false;

	blk->type = column->types[x][z][y];
	blk->metadata = 0;
	return true;
}

TEST(lighting_heightmap_place_solid) {
	uint8_t heightmap[HEIGHTMAP_SIZE] = {0};
	fake_column column = {0};

	test_blocks_init();
	column.types[4][4][5] = BLOCK_STONE;
	lighting_heightmap_update(heightmap, 4, 5, 4, BLOCK_STONE, fake_get_block,
							 &column);
	ASSERT_EQ(heightmap[4 + 4 * CHUNK_SIZE], 6);
}

TEST(lighting_heightmap_remove_solid) {
	uint8_t heightmap[HEIGHTMAP_SIZE] = {0};
	fake_column column = {0};

	test_blocks_init();
	heightmap[4 + 4 * CHUNK_SIZE] = 8;
	column.types[4][4][6] = BLOCK_STONE;

	lighting_heightmap_update(heightmap, 4, 7, 4, BLOCK_GLASS, fake_get_block,
							 &column);
	ASSERT_EQ(heightmap[4 + 4 * CHUNK_SIZE], 7);
}

// Regression test for issue #26: at WORLD_HEIGHT 256 a solid block can sit at
// the ceiling y=255. The per-column heightmap is a single uint8_t, so the
// natural "first sky cell" value y+1 == 256 wraps to 0 and would flood the
// whole column with skylight. lighting_heightmap_update must clamp to 255.
TEST(lighting_heightmap_place_at_ceiling) {
	uint8_t heightmap[HEIGHTMAP_SIZE] = {0};
	fake_column column = {0};

	test_blocks_init();
	// Place a solid block at the very top of a 256-tall world.
	lighting_heightmap_update(heightmap, 4, 255, 4, BLOCK_STONE, fake_get_block,
							 &column);
	// Must NOT be 0 (the 256->0 wrap that lit the entire column); clamped to 255.
	ASSERT_EQ(heightmap[4 + 4 * CHUNK_SIZE], 255);
}

/* Tiny 2x2x2 world — strict bounds so BFS cannot escape and OOM. */
#define LIGHT_GRID 2

typedef struct {
	uint8_t types[LIGHT_GRID][LIGHT_GRID][LIGHT_GRID];
	uint8_t lights[LIGHT_GRID][LIGHT_GRID][LIGHT_GRID];
} light_world;

static bool light_world_in_bounds(w_coord_t x, w_coord_t y, w_coord_t z) {
	return x >= 0 && y >= 0 && z >= 0 && x < LIGHT_GRID && y < LIGHT_GRID
		   && z < LIGHT_GRID;
}

static bool light_world_get(void* user, w_coord_t x, w_coord_t y, w_coord_t z,
							struct block_data* blk, uint8_t* height) {
	light_world* world = user;

	if(!light_world_in_bounds(x, y, z))
		return false;

	if(blk) {
		blk->type = world->types[x][y][z];
		blk->metadata = 0;
		blk->sky_light = world->lights[x][y][z] & 0xF;
		blk->torch_light = world->lights[x][y][z] >> 4;
	}

	if(height)
		*height = 0;

	return true;
}

static void light_world_set(void* user, w_coord_t x, w_coord_t y, w_coord_t z,
							uint8_t light) {
	light_world* world = user;

	if(!light_world_in_bounds(x, y, z))
		return;

	world->lights[x][y][z] = light;
}

TEST(lighting_torch_propagates) {
	light_world world = {0};

	test_blocks_init();
	world.types[0][0][0] = BLOCK_TORCH;

	lighting_update_at_block((struct world_modification_entry) {0, 0, 0}, true,
							 light_world_get, light_world_set, &world);

	ASSERT((world.lights[0][0][0] >> 4) >= 14);
	ASSERT((world.lights[1][0][0] >> 4) >= 13);
}

const test_entry_t g_tests_lighting[] = {
	{"lighting_heightmap_place_solid", test_lighting_heightmap_place_solid},
	{"lighting_heightmap_remove_solid", test_lighting_heightmap_remove_solid},
	{"lighting_heightmap_place_at_ceiling",
	 test_lighting_heightmap_place_at_ceiling},
	{"lighting_torch_propagates", test_lighting_torch_propagates},
};

const size_t g_tests_lighting_count
	= sizeof(g_tests_lighting) / sizeof(g_tests_lighting[0]);
