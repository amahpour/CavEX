#include <string.h>

#include "buffer.h"
#include "harness.h"

TEST(buffer_lazy_init_append) {
	struct buffer buf = {0};
	const unsigned char data[] = {1, 2, 3, 4};

	ASSERT_EQ(buffer_append(&buf, data, sizeof(data)), 0);
	ASSERT_EQ(buf.len, sizeof(data));
	ASSERT(buf.data != NULL);
	buffer_free(&buf);
}

TEST(buffer_reserve_growth) {
	struct buffer buf = {0};
	unsigned char chunk[512];
	const unsigned char tail[] = "world";

	memset(chunk, 0xAB, sizeof(chunk));
	ASSERT_EQ(buffer_reserve(&buf, 2048), 0);
	ASSERT(buf.cap >= 2048);
	ASSERT_EQ(buffer_append(&buf, chunk, sizeof(chunk)), 0);
	ASSERT_EQ(buffer_append(&buf, tail, sizeof(tail) - 1), 0);
	ASSERT(buf.len >= sizeof(chunk) + sizeof(tail) - 1);
	buffer_free(&buf);
}

const test_entry_t g_tests_buffer[] = {
	{"buffer_lazy_init_append", test_buffer_lazy_init_append},
	{"buffer_reserve_growth", test_buffer_reserve_growth},
};

const size_t g_tests_buffer_count
	= sizeof(g_tests_buffer) / sizeof(g_tests_buffer[0]);
