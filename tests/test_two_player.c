// Unit tests for the per-player inventory plumbing (issue #139):
//  - pickup_nearest_player(): the pure nearest-in-range pick that decides which
//    local player collects a dropped item.
//  - level_archive_create_player_file(): the player2.dat sidecar — a missing
//    file yields a usable empty tree, and an inventory written through the
//    ordinary level_archive helpers round-trips through the file on disk.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "harness.h"
#include "item/inventory.h"
#include "network/inventory_logic.h"
#include "network/level_archive.h"
#include "stubs/items_stub.h"

// Single-player collapse: only player 0 exists — in range collects, out of
// range does not, and player 1 can never win.
TEST(two_player_pickup_single_player) {
	ASSERT_EQ(pickup_nearest_player(true, 0, 64, 0, false, 0, 0, 0, /*item*/ 1,
									64, 0, 2.0),
			  0);
	ASSERT_EQ(pickup_nearest_player(true, 0, 64, 0, false, 0, 0, 0, /*item*/ 9,
									64, 0, 2.0),
			  -1);
	// no players at all
	ASSERT_EQ(pickup_nearest_player(false, 0, 0, 0, false, 0, 0, 0, 0, 0, 0,
									2.0),
			  -1);
}

// Two players: the nearer one wins; a distant P1 loses to an in-range P2; ties
// go to player 0 (stable, matches the pre-#139 behaviour).
TEST(two_player_pickup_nearest_wins) {
	// item right next to player 1 (index 1), player 0 far away
	ASSERT_EQ(pickup_nearest_player(true, 100, 64, 100, true, 0, 64, 0,
									/*item*/ 0.5, 64, 0, 2.0),
			  1);
	// both in range, player 0 closer
	ASSERT_EQ(pickup_nearest_player(true, 1, 64, 0, true, 1.8, 64, 0,
									/*item*/ 0, 64, 0, 2.0),
			  0);
	// both in range, player 1 closer
	ASSERT_EQ(pickup_nearest_player(true, 1.8, 64, 0, true, 1, 64, 0,
									/*item*/ 0, 64, 0, 2.0),
			  1);
	// equidistant -> player 0
	ASSERT_EQ(pickup_nearest_player(true, 1, 64, 0, true, -1, 64, 0,
									/*item*/ 0, 64, 0, 2.0),
			  0);
	// neither in range
	ASSERT_EQ(pickup_nearest_player(true, 10, 64, 0, true, -10, 64, 0,
									/*item*/ 0, 64, 0, 2.0),
			  -1);
}

// player2.dat sidecar: a missing file still opens (fresh empty tree), reads as
// an empty inventory, and a written inventory round-trips through disk.
TEST(two_player_sidecar_roundtrip) {
	char tmpl[] = "/tmp/cavex_p2_XXXXXX";
	char* base = mkdtemp(tmpl);
	ASSERT(base != NULL);

	string_t world_dir;
	string_init_printf(world_dir, "%s", base);

	// 1) missing file -> usable archive, empty inventory
	struct level_archive la = {0};
	ASSERT(level_archive_create_player_file(&la, world_dir, "player2.dat"));

	struct inventory inv;
	ASSERT(inventory_create(&inv, NULL, NULL, INVENTORY_SIZE));
	inv.items[INVENTORY_SLOT_HOTBAR].id = 999; // canary: must be cleared
	ASSERT(level_archive_read_inventory(&la, &inv));
	ASSERT_EQ(inv.items[INVENTORY_SLOT_HOTBAR].id, 0);

	// 2) write a couple of stacks and persist
	inv.items[INVENTORY_SLOT_HOTBAR + 2]
		= (struct item_data) {.id = 4, .durability = 0, .count = 32};
	inv.items[INVENTORY_SLOT_MAIN + 5]
		= (struct item_data) {.id = 17, .durability = 3, .count = 7};
	ASSERT(level_archive_write_inventory(&la, &inv));
	level_archive_destroy(&la); // flushes to <dir>/player2.dat

	// 3) reopen from disk into a fresh inventory: stacks are back
	struct level_archive la2 = {0};
	ASSERT(level_archive_create_player_file(&la2, world_dir, "player2.dat"));

	struct inventory inv2;
	ASSERT(inventory_create(&inv2, NULL, NULL, INVENTORY_SIZE));
	ASSERT(level_archive_read_inventory(&la2, &inv2));
	ASSERT_EQ(inv2.items[INVENTORY_SLOT_HOTBAR + 2].id, 4);
	ASSERT_EQ(inv2.items[INVENTORY_SLOT_HOTBAR + 2].count, 32);
	ASSERT_EQ(inv2.items[INVENTORY_SLOT_MAIN + 5].id, 17);
	ASSERT_EQ(inv2.items[INVENTORY_SLOT_MAIN + 5].durability, 3);
	ASSERT_EQ(inv2.items[INVENTORY_SLOT_MAIN + 6].id, 0);

	level_archive_destroy(&la2);
	inventory_destroy(&inv);
	inventory_destroy(&inv2);
	string_clear(world_dir);

	// tidy the temp dir (best effort)
	char path[600];
	snprintf(path, sizeof(path), "%s/player2.dat", base);
	remove(path);
	rmdir(base);
}

const test_entry_t g_tests_two_player[] = {
	{"two_player_pickup_single_player", test_two_player_pickup_single_player},
	{"two_player_pickup_nearest_wins", test_two_player_pickup_nearest_wins},
	{"two_player_sidecar_roundtrip", test_two_player_sidecar_roundtrip},
};

const size_t g_tests_two_player_count
	= sizeof(g_tests_two_player) / sizeof(g_tests_two_player[0]);
