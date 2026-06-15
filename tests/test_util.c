#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "harness.h"
#include "test_path.h"
#include "util.h"

TEST(hash_u32_zero) {
	ASSERT_EQ(hash_u32(0), 0U);
	ASSERT_NE(hash_u32(1), hash_u32(2));
}

TEST(nibble_even_odd) {
	uint8_t bytes[2] = {0};

	nibble_write(bytes, 0, 0xA);
	nibble_write(bytes, 1, 0xB);
	ASSERT_EQ(nibble_read(bytes, 0), 0xA);
	ASSERT_EQ(nibble_read(bytes, 1), 0xB);

	nibble_write(bytes, 0, 0x3);
	nibble_write(bytes, 1, 0x4);
	ASSERT_EQ(bytes[0], 0x43);
}

TEST(rand_gen_sequence) {
	struct random_gen gen = {.seed = 12345U};

	uint32_t first = rand_gen(&gen);
	uint32_t second = rand_gen(&gen);

	gen.seed = 12345U;
	ASSERT_EQ(rand_gen(&gen), first);
	ASSERT_EQ(rand_gen(&gen), second);
}

TEST(rand_gen_range) {
	struct random_gen gen = {.seed = 99U};

	for(int k = 0; k < 32; k++) {
		int value = rand_gen_range(&gen, 2, 5);
		ASSERT(value >= 2);
		ASSERT(value < 5);
	}
}

TEST(rand_gen_flt) {
	struct random_gen gen = {.seed = 7U};

	for(int k = 0; k < 32; k++) {
		float value = rand_gen_flt(&gen);
		ASSERT(value >= 0.0F);
		ASSERT(value <= 1.0F);
	}
}

TEST(hsv2rgb_all_hues) {
	for(int hue = 0; hue < 6; hue++) {
		float h = (float)hue / 6.0F;
		float s = 1.0F;
		float v = 1.0F;

		hsv2rgb(&h, &s, &v);
		ASSERT(h >= 0.0F && h <= 1.0F);
		ASSERT(s >= 0.0F && s <= 1.0F);
		ASSERT(v >= 0.0F && v <= 1.0F);
	}
}

TEST(file_read_fixture) {
	char path[512];
	void* data;

	test_fixture_path(path, sizeof(path), "config_valid.json");
	data = file_read(path);
	ASSERT(data != NULL);
	free(data);
	ASSERT(file_read("/tmp/cavex_missing_file_read_test.bin") == NULL);
}

const test_entry_t g_tests_util[] = {
	{"hash_u32_zero", test_hash_u32_zero},
	{"nibble_even_odd", test_nibble_even_odd},
	{"rand_gen_sequence", test_rand_gen_sequence},
	{"rand_gen_range", test_rand_gen_range},
	{"rand_gen_flt", test_rand_gen_flt},
	{"hsv2rgb_all_hues", test_hsv2rgb_all_hues},
	{"file_read_fixture", test_file_read_fixture},
};

const size_t g_tests_util_count
	= sizeof(g_tests_util) / sizeof(g_tests_util[0]);
