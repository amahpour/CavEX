#include <string.h>

#include "config.h"
#include "harness.h"
#include "test_path.h"

TEST(config_create_valid) {
	struct config cfg = {0};
	char path[512];

	test_fixture_path(path, sizeof(path), "config_valid.json");
	ASSERT(config_create(&cfg, path));
	config_destroy(&cfg);
}

TEST(config_create_missing_file) {
	struct config cfg = {0};

	ASSERT(!config_create(&cfg, "/tmp/cavex_missing_config_test.json"));
}

TEST(config_create_non_object) {
	struct config cfg = {0};
	char path[512];

	test_fixture_path(path, sizeof(path), "config_array_root.json");
	ASSERT(!config_create(&cfg, path));
}

TEST(config_read_string) {
	struct config cfg = {0};
	char path[512];
	const char* value;

	test_fixture_path(path, sizeof(path), "config_valid.json");
	ASSERT(config_create(&cfg, path));

	value = config_read_string(&cfg, "name", "fallback");
	ASSERT(value != NULL);
	ASSERT(strcmp(value, "Claude World") == 0);

	value = config_read_string(&cfg, "missing.key", "fallback");
	ASSERT(value != NULL);
	ASSERT(strcmp(value, "fallback") == 0);

	config_destroy(&cfg);
}

TEST(config_read_int_array) {
	struct config cfg = {0};
	char path[512];
	int values[2] = {0};
	size_t length = 2;

	test_fixture_path(path, sizeof(path), "config_valid.json");
	ASSERT(config_create(&cfg, path));
	ASSERT(config_read_int_array(&cfg, "nums", values, &length));
	ASSERT_EQ(length, 2U);
	ASSERT_EQ(values[0], 10);
	ASSERT_EQ(values[1], 20);
	config_destroy(&cfg);
}

TEST(config_read_int_array_missing) {
	struct config cfg = {0};
	char path[512];
	int values[4] = {0};
	size_t length = 4;

	test_fixture_path(path, sizeof(path), "config_valid.json");
	ASSERT(config_create(&cfg, path));
	ASSERT(!config_read_int_array(&cfg, "missing", values, &length));
	config_destroy(&cfg);
}

const test_entry_t g_tests_config[] = {
	{"config_create_valid", test_config_create_valid},
	{"config_create_missing_file", test_config_create_missing_file},
	{"config_create_non_object", test_config_create_non_object},
	{"config_read_string", test_config_read_string},
	{"config_read_int_array", test_config_read_int_array},
	{"config_read_int_array_missing", test_config_read_int_array_missing},
};

const size_t g_tests_config_count
	= sizeof(g_tests_config) / sizeof(g_tests_config[0]);
