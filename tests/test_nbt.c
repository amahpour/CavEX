#include <errno.h>
#include <string.h>

#include "harness.h"
#include "nbt.h"
#include "test_path.h"

TEST(nbt_parse_level_dat) {
	char path[512];
	nbt_node* root;

	test_fixture_path(path, sizeof(path), "level.dat");
	root = nbt_parse_path(path);
	ASSERT(root != NULL);
	ASSERT_EQ(errno, 0);
	nbt_free(root);
}

TEST(nbt_find_spawn_coords) {
	char path[512];
	nbt_node *root, *spawn_x, *spawn_y, *spawn_z;

	test_fixture_path(path, sizeof(path), "level.dat");
	root = nbt_parse_path(path);
	ASSERT(root != NULL);

	spawn_x = nbt_find_by_path(root, ".Data.SpawnX");
	spawn_y = nbt_find_by_path(root, ".Data.SpawnY");
	spawn_z = nbt_find_by_path(root, ".Data.SpawnZ");
	ASSERT(spawn_x != NULL && spawn_y != NULL && spawn_z != NULL);
	ASSERT_EQ(spawn_x->type, TAG_INT);
	ASSERT_EQ(spawn_y->type, TAG_INT);
	ASSERT_EQ(spawn_z->type, TAG_INT);
	ASSERT_EQ(spawn_x->payload.tag_int, 24);
	ASSERT_EQ(spawn_y->payload.tag_int, 64);
	ASSERT_EQ(spawn_z->payload.tag_int, 24);

	nbt_free(root);
}

TEST(nbt_parse_missing_file) {
	nbt_node* root = nbt_parse_path("/tmp/cavex_missing_level_dat_test.dat");

	ASSERT(root == NULL);
	ASSERT_EQ(errno, NBT_EIO);
}

TEST(nbt_parse_truncated) {
	char path[512];
	nbt_node* root;

	test_fixture_path(path, sizeof(path), "nbt_truncated.dat");
	root = nbt_parse_path(path);
	ASSERT(root == NULL);
	ASSERT(errno != 0);
}

TEST(nbt_clone_roundtrip) {
	char path[512];
	nbt_node *root, *clone;

	test_fixture_path(path, sizeof(path), "level.dat");
	root = nbt_parse_path(path);
	ASSERT(root != NULL);

	clone = nbt_clone(root);
	ASSERT(clone != NULL);
	ASSERT(nbt_eq(root, clone));

	nbt_free(clone);
	nbt_free(root);
}

TEST(nbt_util_strings) {
	ASSERT(strcmp(nbt_type_to_string(TAG_INT), "TAG_INT") == 0);
	ASSERT(strcmp(nbt_error_to_string(NBT_OK), "No error.") == 0);
	ASSERT(strcmp(nbt_error_to_string(NBT_EIO), "IO Error. Nonexistant/corrupt file?")
		   == 0);
}

TEST(nbt_binary_roundtrip) {
	char path[512];
	nbt_node *root, *parsed;
	struct buffer blob;

	test_fixture_path(path, sizeof(path), "level.dat");
	root = nbt_parse_path(path);
	ASSERT(root != NULL);

	blob = nbt_dump_binary(root);
	ASSERT(blob.data != NULL);

	parsed = nbt_parse(blob.data, blob.len);
	ASSERT(parsed != NULL);
	ASSERT(nbt_eq(root, parsed));

	nbt_free(parsed);
	buffer_free(&blob);
	nbt_free(root);
}

TEST(nbt_find_by_name) {
	char path[512];
	nbt_node *root, *data;

	test_fixture_path(path, sizeof(path), "level.dat");
	root = nbt_parse_path(path);
	ASSERT(root != NULL);

	data = nbt_find_by_name(root, "Data");
	ASSERT(data != NULL);
	ASSERT_EQ(data->type, TAG_COMPOUND);

	nbt_free(root);
}

const test_entry_t g_tests_nbt[] = {
	{"nbt_parse_level_dat", test_nbt_parse_level_dat},
	{"nbt_find_spawn_coords", test_nbt_find_spawn_coords},
	{"nbt_parse_missing_file", test_nbt_parse_missing_file},
	{"nbt_parse_truncated", test_nbt_parse_truncated},
	{"nbt_clone_roundtrip", test_nbt_clone_roundtrip},
	{"nbt_util_strings", test_nbt_util_strings},
	{"nbt_binary_roundtrip", test_nbt_binary_roundtrip},
	{"nbt_find_by_name", test_nbt_find_by_name},
};

const size_t g_tests_nbt_count
	= sizeof(g_tests_nbt) / sizeof(g_tests_nbt[0]);
