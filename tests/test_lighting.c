#include "block/blocks_data.h"
#include "harness.h"
#include "item/recipe.h"
#include "lighting.h"
#include "stubs/blocks_stub.h"

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

const test_entry_t g_tests_lighting[] = {
	{"lighting_heightmap_place_solid", test_lighting_heightmap_place_solid},
	{"lighting_heightmap_remove_solid", test_lighting_heightmap_remove_solid},
};

const size_t g_tests_lighting_count
	= sizeof(g_tests_lighting) / sizeof(g_tests_lighting[0]);
