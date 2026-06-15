#include <stdlib.h>
#include <string.h>

#include "harness.h"
#include "network/level_archive.h"
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

const test_entry_t g_tests_level[] = {
	{"level_archive_read_write_time", test_level_archive_read_write_time},
	{"level_archive_read_level_name", test_level_archive_read_level_name},
};

const size_t g_tests_level_count
	= sizeof(g_tests_level) / sizeof(g_tests_level[0]);
