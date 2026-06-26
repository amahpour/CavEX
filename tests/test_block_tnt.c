// Unit tests for TNT chain reactions (issue #115).
//
// block_tnt.c's onRightClick detonates TNT instantly: it clears a radius-2
// sphere to air and, crucially, *chains* into any other TNT in that sphere,
// recursing outward so a connected cluster all goes off from one ignition. The
// recursion is bounded two ways -- a depth cap and clear-before-recurse (each
// TNT is set to air before it is recursed into) -- so it can never hang or clear
// unbounded terrain. These tests drive the real callback against a fake world
// (server_world_stub) and assert the chain, the bounds, and the no-ops.
//
// The fake world ignores the server_world* pointer, so a zeroed server_local on
// the stack is enough to exercise the logic without any chunk/region machinery.
//
// NOTE: block_tnt.c has only a few distinct branches, so the suite is two tests
// (the repo's per-test coverage gate rejects a test that adds no new line).
// Several distinct scenarios -- straight line, 3D cube cluster, far disconnected
// TNT, depth-cap termination -- are folded into those two so every behaviour is
// still asserted.

#include "block/blocks.h"
#include "block/blocks_data.h"
#include "harness.h"
#include "item/items.h"
#include "network/server_local.h"
#include "stubs/server_world_stub.h"

// Detonate by firing the real onRightClick with flint & steel on the block at
// (x,y,z). A zeroed server_local is fine: the fake world is global.
static void ignite(w_coord_t x, w_coord_t y, w_coord_t z) {
	static struct server_local s;  // large; static avoids a big stack frame
	memset(&s, 0, sizeof(s));
	struct item_data flint = {.id = ITEM_FLINT_STEEL, .count = 1};
	struct block_data blk = {.type = BLOCK_TNT};
	struct block_info on = {.block = &blk, .x = x, .y = y, .z = z};
	struct block_info where = {0};
	block_tnt.onRightClick(&s, &flint, &where, &on, SIDE_TOP);
}

#define IS_AIR(x, y, z) (test_server_world_get((x), (y), (z)) == BLOCK_AIR)
#define IS_TNT(x, y, z) (test_server_world_get((x), (y), (z)) == BLOCK_TNT)

// The chain detonates connected clusters (a line AND a 3D cube), clears ordinary
// blocks caught in a sphere, and -- because it is bounded -- leaves disconnected
// TNT well outside the blast untouched.
TEST(tnt_chain_clears_connected_clusters) {
	// (a) A line of 5 TNT along x, packed one apart (each neighbour is inside
	// the next one's radius-2 sphere, so the chain propagates the length of it).
	test_server_world_reset();
	for(w_coord_t x = 0; x < 5; x++)
		test_server_world_set(x, 0, 0, BLOCK_TNT);
	test_server_world_set(2, 1, 0, BLOCK_STONE);	// ordinary block in a sphere
	// A lone TNT well outside any sphere (dist 12 on x): must survive, proving
	// the blast does not run away to disconnected TNT.
	test_server_world_set(12, 0, 0, BLOCK_TNT);

	ignite(0, 0, 0);

	for(w_coord_t x = 0; x < 5; x++)
		ASSERT(IS_AIR(x, 0, 0));   // whole connected line gone
	ASSERT(IS_AIR(2, 1, 0));	   // adjacent stone cleared by a sphere
	ASSERT(IS_TNT(12, 0, 0));	   // disconnected TNT untouched -> bounded

	// (b) A solid 2x2x2 cube of TNT (every cell adjacent) fully detonates --
	// exercises recursion branching in all axes, not just a line.
	test_server_world_reset();
	for(w_coord_t x = 0; x < 2; x++)
		for(w_coord_t y = 0; y < 2; y++)
			for(w_coord_t z = 0; z < 2; z++)
				test_server_world_set(x, y, z, BLOCK_TNT);

	ignite(0, 0, 0);

	for(w_coord_t x = 0; x < 2; x++)
		for(w_coord_t y = 0; y < 2; y++)
			for(w_coord_t z = 0; z < 2; z++)
				ASSERT(IS_AIR(x, y, z));
}

// Bounds & no-ops: bedrock survives a blast, a non-flint (and null) item does
// nothing, and a long packed line terminates without hanging while the depth cap
// limits the reach (the far end of the line survives -> not a whole-grid wipe).
TEST(tnt_bounds_bedrock_survives_noops_and_depth_cap) {
	test_server_world_reset();

	// Single TNT to ignite, with bedrock right next to it.
	test_server_world_set(0, 0, 0, BLOCK_TNT);
	test_server_world_set(1, 0, 0, BLOCK_BEDROCK);

	// A non-flint item, and a null item, must NOT detonate anything.
	static struct server_local s;
	memset(&s, 0, sizeof(s));
	struct block_data blk = {.type = BLOCK_TNT};
	struct block_info on = {.block = &blk, .x = 0, .y = 0, .z = 0};
	struct block_info where = {0};
	struct item_data pick = {.id = ITEM_IRON_PICKAXE, .count = 1};
	block_tnt.onRightClick(&s, &pick, &where, &on, SIDE_TOP);
	ASSERT(IS_TNT(0, 0, 0));
	block_tnt.onRightClick(&s, NULL, &where, &on, SIDE_TOP);
	ASSERT(IS_TNT(0, 0, 0));

	ignite(0, 0, 0);
	ASSERT(IS_AIR(0, 0, 0));								   // detonated TNT cleared
	ASSERT_EQ(test_server_world_get(1, 0, 0), BLOCK_BEDROCK);  // bedrock survives

	// Termination & depth cap: a long packed line must not hang (clear-before-
	// recurse visits each TNT once), but a TNT beyond the depth-capped reach
	// survives -- the chain is bounded, not a whole-grid wipe.
	test_server_world_reset();
	for(w_coord_t x = 0; x < 40; x++)
		test_server_world_set(x, 0, 0, BLOCK_TNT);

	ignite(0, 0, 0);  // must return (no infinite recursion / hang)

	ASSERT(IS_AIR(0, 0, 0));   // near end consumed
	ASSERT(IS_AIR(1, 0, 0));
	bool some_tnt_remains = false;
	for(w_coord_t x = 0; x < 40; x++) {
		if(IS_TNT(x, 0, 0)) {
			some_tnt_remains = true;
			break;
		}
	}
	ASSERT(some_tnt_remains);
}

const test_entry_t g_tests_block_tnt[] = {
	{"tnt_chain_clears_connected_clusters",
	 test_tnt_chain_clears_connected_clusters},
	{"tnt_bounds_bedrock_survives_noops_and_depth_cap",
	 test_tnt_bounds_bedrock_survives_noops_and_depth_cap},
};

const size_t g_tests_block_tnt_count
	= sizeof(g_tests_block_tnt) / sizeof(g_tests_block_tnt[0]);
