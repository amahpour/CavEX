// Test fake for the server world's block get/set, backed by a small in-memory
// grid so the pure block logic (e.g. TNT chain detonation in block_tnt.c) can be
// exercised without the real chunk/region machinery. The `struct server_world*`
// argument is ignored; the grid is file-static and reset by
// test_server_world_reset() before each test.
//
// Coordinates are offset by SW_BIAS into a fixed positive window. Any access
// outside the window returns false from get (and is ignored by set), which the
// caller already treats as "no block here" -- so a test can also assert that the
// blast does not run off the edge of the world.

#include <string.h>

#include "block/blocks_data.h"
#include "world.h"

#include "server_world_stub.h"

#define SW_DIM 64
#define SW_BIAS 32  // maps coords -32..31 -> 0..63

static struct block_data sw_grid[SW_DIM][SW_DIM][SW_DIM];

static int sw_idx(w_coord_t v) {
	return (int)v + SW_BIAS;
}

static bool sw_in_range(w_coord_t x, w_coord_t y, w_coord_t z) {
	int ix = sw_idx(x), iy = sw_idx(y), iz = sw_idx(z);
	return ix >= 0 && ix < SW_DIM && iy >= 0 && iy < SW_DIM && iz >= 0
		&& iz < SW_DIM;
}

void test_server_world_reset(void) {
	memset(sw_grid, 0, sizeof(sw_grid));  // all BLOCK_AIR (type 0)
}

void test_server_world_set(w_coord_t x, w_coord_t y, w_coord_t z,
						   uint8_t type) {
	if(!sw_in_range(x, y, z))
		return;
	sw_grid[sw_idx(x)][sw_idx(y)][sw_idx(z)]
		= (struct block_data) {.type = type};
}

uint8_t test_server_world_get(w_coord_t x, w_coord_t y, w_coord_t z) {
	if(!sw_in_range(x, y, z))
		return BLOCK_AIR;
	return sw_grid[sw_idx(x)][sw_idx(y)][sw_idx(z)].type;
}

// --- the symbols block_tnt.c actually links against ---

bool server_world_get_block(struct server_world* w, w_coord_t x, w_coord_t y,
							w_coord_t z, struct block_data* blk) {
	(void)w;
	if(!sw_in_range(x, y, z))
		return false;
	if(blk)
		*blk = sw_grid[sw_idx(x)][sw_idx(y)][sw_idx(z)];
	return true;
}

bool server_world_set_block(struct server_world* w, w_coord_t x, w_coord_t y,
							w_coord_t z, struct block_data blk) {
	(void)w;
	if(!sw_in_range(x, y, z))
		return false;
	sw_grid[sw_idx(x)][sw_idx(y)][sw_idx(z)] = blk;
	return true;
}
