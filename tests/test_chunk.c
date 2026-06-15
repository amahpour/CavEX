#include <stdlib.h>

#include "chunk.h"
#include "harness.h"
#include "world.h"

TEST(chunk_block_roundtrip) {
	struct world world = {0};
	struct chunk* chunk = malloc(sizeof(struct chunk));

	ASSERT(chunk != NULL);
	ASSERT_EQ(WCOORD_CHUNK_OFFSET(15), 0);
	ASSERT_EQ(W2C_COORD(-1), 15);
	ASSERT_NE(CHUNK_TO_ID(0, 0, 0), CHUNK_TO_ID(1, 0, 0));

	chunk_init(chunk, &world, 0, 0, 0);
	chunk_ref(chunk);

	chunk_set_block(chunk, 4, 5, 6,
					(struct block_data) {
						.type = 17,
						.metadata = 3,
						.sky_light = 12,
						.torch_light = 5,
					});

	struct block_data out = chunk_get_block(chunk, 4, 5, 6);
	ASSERT_EQ(out.type, 17);
	ASSERT_EQ(out.metadata, 3U);
	ASSERT_EQ(out.sky_light, 12U);
	ASSERT_EQ(out.torch_light, 5U);

	chunk_set_light(chunk, 4, 5, 6, 0xA5);
	out = chunk_get_block(chunk, 4, 5, 6);
	ASSERT_EQ(out.sky_light, 5U);
	ASSERT_EQ(out.torch_light, 10U);

	chunk_unref(chunk);
}

const test_entry_t g_tests_chunk[] = {
	{"chunk_block_roundtrip", test_chunk_block_roundtrip},
};

const size_t g_tests_chunk_count
	= sizeof(g_tests_chunk) / sizeof(g_tests_chunk[0]);
