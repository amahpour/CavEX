#include <string.h>

#include "block/blocks_data.h"
#include "harness.h"
#include "item/items.h"
#include "item/recipe.h"
#include "network/client_interface.h"
#include "network/server_local.h"

// Defined in stubs/render_stub.c: captures the client RPC firework_use() sends
// so we can verify it requested a particle burst.
extern struct client_rpc test_last_client_rpc;
extern int test_client_rpc_count;

// item_firework (issue #31) lives in source/item/items/item_firework.c, which
// is linked into the test harness. The real items[] registry (items.c) is
// graphics-bound and is replaced by a stub here, so register the firework into
// the stub registry the same way items_init() does, then exercise it.
TEST(firework_item_and_use) {
	extern struct item item_firework;

	items[ITEM_FIREWORK] = &item_firework;

	// registered with a real reused tile and a use action, max_stack 64
	struct item_data fw = {.id = ITEM_FIREWORK, .count = 1};
	struct item* it = item_get(&fw);
	ASSERT_NE(it, NULL);
	ASSERT_EQ(it, &item_firework);
	ASSERT(strcmp(it->name, "Firework Rocket") == 0);
	ASSERT_EQ(it->max_stack, 64);
	ASSERT_NE(it->onItemUse, NULL);
	ASSERT_EQ(it->onItemPlace, NULL); // use-only, never places a block

	// using the firework returns true (consume one) and asks the client to
	// spawn a multi-particle burst at the player
	struct server_local s;
	memset(&s, 0, sizeof(s));
	s.player.x = 10.0;
	s.player.y = 64.0;
	s.player.z = -7.0;

	test_client_rpc_count = 0;
	memset(&test_last_client_rpc, 0, sizeof(test_last_client_rpc));
	ASSERT(it->onItemUse(&s, &fw));
	ASSERT_EQ(test_client_rpc_count, 1);
	ASSERT_EQ(test_last_client_rpc.type, CRPC_PARTICLE_BURST);
	ASSERT(test_last_client_rpc.payload.particle_burst.count > 1);
	// burst originates at the player (slightly above the feet)
	ASSERT_NEAR(test_last_client_rpc.payload.particle_burst.pos[1], 65.0, 0.01);

	// craftable in normal play: paper above gunpowder. (Kept in this same
	// test because the standalone recipe assertion adds no uniquely-covered
	// lines over recipe_init_full_table and the per-test coverage gate would
	// drop it.)
	struct item_data slots[9] = {0};
	bool slot_empty[9];
	struct item_data result = {0};

	recipe_init();

	memset(slot_empty, true, sizeof(slot_empty));
	slots[0] = (struct item_data) {.id = ITEM_PAPER};
	slots[3] = (struct item_data) {.id = ITEM_GUNPOWDER};
	slot_empty[0] = slot_empty[3] = false;
	ASSERT(recipe_match(recipes_crafting, slots, slot_empty, &result));
	ASSERT_EQ(result.id, ITEM_FIREWORK);
	ASSERT_EQ(result.count, 1U);

	// reversed (gunpowder above paper) is a different shape and must not match
	memset(slot_empty, true, sizeof(slot_empty));
	memset(slots, 0, sizeof(slots));
	slots[0] = (struct item_data) {.id = ITEM_GUNPOWDER};
	slots[3] = (struct item_data) {.id = ITEM_PAPER};
	slot_empty[0] = slot_empty[3] = false;
	ASSERT(!recipe_match(recipes_crafting, slots, slot_empty, &result));

	array_recipe_clear(recipes_crafting);

	items[ITEM_FIREWORK] = NULL;
}

const test_entry_t g_tests_firework[] = {
	{"firework_item_and_use", test_firework_item_and_use},
};

const size_t g_tests_firework_count
	= sizeof(g_tests_firework) / sizeof(g_tests_firework[0]);
