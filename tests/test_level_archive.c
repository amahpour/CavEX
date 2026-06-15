#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "harness.h"
#include "item/inventory.h"
#include "network/level_archive.h"
#include "stubs/items_stub.h"
#include "test_path.h"

TEST(level_archive_read_write_time) {
	char dir[512];
	struct level_archive la = {0};
	string_t world_dir;
	int64_t time = 0;

	test_fixture_path(dir, sizeof(dir), "level.dat");
	dir[strlen(dir) - strlen("/level.dat")] = '\0';

	string_init_printf(world_dir, "%s", dir);
	ASSERT(level_archive_create(&la, world_dir));

	ASSERT(level_archive_read(&la, LEVEL_TIME, &time, 0));
	ASSERT(level_archive_write(&la, LEVEL_TIME, &(int64_t) {12345}));
	ASSERT(level_archive_read(&la, LEVEL_TIME, &time, 0));
	ASSERT_EQ(time, 12345);

	level_archive_destroy(&la);
	string_clear(world_dir);
}

TEST(level_archive_read_level_name) {
	char dir[512];
	struct level_archive la = {0};
	string_t world_dir;
	char name[64] = {0};

	test_fixture_path(dir, sizeof(dir), "level.dat");
	dir[strlen(dir) - strlen("/level.dat")] = '\0';

	string_init_printf(world_dir, "%s", dir);
	ASSERT(level_archive_create(&la, world_dir));
	ASSERT(level_archive_read(&la, LEVEL_NAME, name, sizeof(name)));
	ASSERT(name[0] != '\0');

	level_archive_destroy(&la);
	string_clear(world_dir);
}

TEST(level_archive_read_missing_tag) {
	char dir[512];
	struct level_archive la = {0};
	string_t world_dir;
	int32_t dummy = 0;

	test_fixture_path(dir, sizeof(dir), "level.dat");
	dir[strlen(dir) - strlen("/level.dat")] = '\0';

	string_init_printf(world_dir, "%s", dir);
	ASSERT(level_archive_create(&la, world_dir));

	// a tag that does not exist in the file
	struct level_archive_tag missing = {".Data.NotThere", TAG_INT};
	ASSERT(!level_archive_read(&la, missing, &dummy, 0));
	// writing a missing tag also fails (node lookup returns nothing)
	ASSERT(!level_archive_write(&la, missing, &dummy));
	// writing an unsupported (string) type returns false
	ASSERT(!level_archive_write(&la, LEVEL_NAME, "x"));

	level_archive_destroy(&la);
	string_clear(world_dir);
}

// Copies the committed level.dat fixture into a fresh temp world directory so
// that destroy-time writes never touch the checked-in fixture.
static void copy_fixture_world(string_t out_dir) {
	char src[512];
	test_fixture_path(src, sizeof(src), "level.dat");

	char tmpl[] = "/tmp/cavex_level_XXXXXX";
	char* base = mkdtemp(tmpl);
	ASSERT(base != NULL);

	char dst[600];
	snprintf(dst, sizeof(dst), "%s/level.dat", base);

	FILE* in = fopen(src, "rb");
	ASSERT(in != NULL);
	fseek(in, 0, SEEK_END);
	long len = ftell(in);
	fseek(in, 0, SEEK_SET);
	void* buf = malloc(len);
	ASSERT(buf != NULL);
	ASSERT(fread(buf, len, 1, in) == 1);
	fclose(in);

	FILE* out = fopen(dst, "wb");
	ASSERT(out != NULL);
	ASSERT(fwrite(buf, len, 1, out) == 1);
	fclose(out);
	free(buf);

	string_init_printf(out_dir, "%s", base);
}

TEST(level_archive_player_roundtrip) {
	struct level_archive la = {0};
	string_t world_dir;

	copy_fixture_world(world_dir);
	ASSERT(level_archive_create(&la, world_dir));

	vec3 pos = {1.5, 64.0, -3.25};
	vec2 rot = {90.0F, -10.0F};
	vec3 vel = {0.1, -0.2, 0.3};
	ASSERT(level_archive_write_player(&la, pos, rot, vel, WORLD_DIM_OVERWORLD));

	vec3 pos_out = {0};
	vec2 rot_out = {0};
	vec3 vel_out = {0};
	enum world_dim dim = WORLD_DIM_NETHER;
	ASSERT(level_archive_read_player(&la, pos_out, rot_out, vel_out, &dim));
	ASSERT_NEAR(pos_out[0], 1.5, 0.0001);
	ASSERT_NEAR(pos_out[1], 64.0, 0.0001);
	ASSERT_NEAR(pos_out[2], -3.25, 0.0001);
	ASSERT_NEAR(rot_out[0], 90.0, 0.001);
	ASSERT_NEAR(vel_out[2], 0.3, 0.0001);
	ASSERT_EQ(dim, WORLD_DIM_OVERWORLD);

	// partial reads/writes with NULL arguments are allowed
	ASSERT(level_archive_write_player(&la, NULL, NULL, NULL, WORLD_DIM_NETHER));
	ASSERT(level_archive_read_player(&la, NULL, NULL, NULL, &dim));
	ASSERT_EQ(dim, WORLD_DIM_NETHER);

	level_archive_destroy(&la);
	string_clear(world_dir);
}

TEST(level_archive_inventory_roundtrip) {
	struct level_archive la = {0};
	struct inventory inv = {0};
	string_t world_dir;

	test_items_init();
	copy_fixture_world(world_dir);
	ASSERT(level_archive_create(&la, world_dir));
	ASSERT(inventory_create(&inv, NULL, NULL, INVENTORY_SIZE));

	// read whatever the fixture ships with
	ASSERT(level_archive_read_inventory(&la, &inv));

	// place known items across hotbar, main and armor slots and round-trip
	// them through the archive (each slot class uses a different translation)
	inventory_set_slot(&inv, INVENTORY_SLOT_HOTBAR,
					   (struct item_data) {.id = BLOCK_DIRT, .count = 7});
	inventory_set_slot(&inv, INVENTORY_SLOT_MAIN,
					   (struct item_data) {.id = BLOCK_LOG, .count = 12});
	inventory_set_slot(&inv, INVENTORY_SLOT_ARMOR,
					   (struct item_data) {.id = BLOCK_DIRT, .count = 1});
	ASSERT(level_archive_write_inventory(&la, &inv));

	struct inventory loaded = {0};
	ASSERT(inventory_create(&loaded, NULL, NULL, INVENTORY_SIZE));
	ASSERT(level_archive_read_inventory(&la, &loaded));

	struct item_data item = {0};
	ASSERT(inventory_get_slot(&loaded, INVENTORY_SLOT_HOTBAR, &item));
	ASSERT_EQ(item.id, BLOCK_DIRT);
	ASSERT_EQ(item.count, 7U);
	ASSERT(inventory_get_slot(&loaded, INVENTORY_SLOT_MAIN, &item));
	ASSERT_EQ(item.id, BLOCK_LOG);
	ASSERT_EQ(item.count, 12U);
	ASSERT(inventory_get_slot(&loaded, INVENTORY_SLOT_ARMOR, &item));
	ASSERT_EQ(item.id, BLOCK_DIRT);

	inventory_destroy(&inv);
	inventory_destroy(&loaded);
	level_archive_destroy(&la);
	string_clear(world_dir);
}

const test_entry_t g_tests_level[] = {
	{"level_archive_read_write_time", test_level_archive_read_write_time},
	{"level_archive_read_level_name", test_level_archive_read_level_name},
	{"level_archive_read_missing_tag", test_level_archive_read_missing_tag},
	{"level_archive_player_roundtrip", test_level_archive_player_roundtrip},
	{"level_archive_inventory_roundtrip",
	 test_level_archive_inventory_roundtrip},
};

const size_t g_tests_level_count
	= sizeof(g_tests_level) / sizeof(g_tests_level[0]);
