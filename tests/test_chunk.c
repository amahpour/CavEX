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

// Regression test for issue #26 (taller PC worlds, WORLD_HEIGHT 128 -> 256).
//
// Part A — CHUNK_TO_ID Y packing: the id used to pack chunk-Y in only 4 bits
// (mask 0xF), so any two columns whose chunk-Y differed by a multiple of 16
// collided to the same id — silently truncating worlds taller than 16 chunks
// (128 blocks). The mask is now 6 bits (0x3F). Assert every chunk-Y in [0, 63]
// round-trips and stays distinct, and that x/z still disambiguate.
//
// Part B — the WORLD_HEIGHT boundary itself: chunk_lookup_block()'s out-of-
// loaded-world fallback keys "is this above the sky?" off `y < WORLD_HEIGHT`.
// Probing it just below and at/above the cap exercises that height-dependent
// branch (and gives the test real source coverage), proving y up to 255 is
// "inside" the world on PC while y >= 256 reads as open sky.
TEST(chunk_to_id_y_no_truncation) {
	// --- Part A: CHUNK_TO_ID across the full 6-bit Y range ---
	// A 256-tall PC world spans chunk-Y 0..15; check well past that (0..63)
	// so the widened field is exercised to its limit.
	for(int y1 = 0; y1 <= 63; y1++) {
		// Y survives in the low 6 bits.
		ASSERT_EQ((int)(CHUNK_TO_ID(3, y1, 7) & 0x3F), y1);

		// The historical bug: y and y+16 collided under the 4-bit mask.
		if(y1 + 16 <= 63)
			ASSERT_NE(CHUNK_TO_ID(3, y1, 7), CHUNK_TO_ID(3, y1 + 16, 7));

		// Every distinct Y in range maps to a distinct id for a fixed column.
		for(int y2 = y1 + 1; y2 <= 63; y2++)
			ASSERT_NE(CHUNK_TO_ID(3, y1, 7), CHUNK_TO_ID(3, y2, 7));
	}

	// X and Z still disambiguate without aliasing into the Y field.
	ASSERT_NE(CHUNK_TO_ID(0, 5, 0), CHUNK_TO_ID(1, 5, 0));
	ASSERT_NE(CHUNK_TO_ID(0, 5, 0), CHUNK_TO_ID(0, 5, 1));
	ASSERT_NE(CHUNK_TO_ID(1, 5, 0), CHUNK_TO_ID(0, 5, 1));

	// Negative coordinates (valid w_coord_t) stay distinct from positives.
	ASSERT_NE(CHUNK_TO_ID(-1, 5, 7), CHUNK_TO_ID(1, 5, 7));
	ASSERT_NE(CHUNK_TO_ID(3, 5, -1), CHUNK_TO_ID(3, 5, 1));

	// --- Part B: WORLD_HEIGHT cap in chunk_lookup_block's fallback ---
	// world_find_chunk() is stubbed to return NULL here, so every lookup that
	// leaves the home chunk hits the "open world" fallback whose values flip at
	// y == WORLD_HEIGHT.
	struct world world = {0};
	struct chunk* chunk = malloc(sizeof(struct chunk));
	ASSERT(chunk != NULL);
	chunk_init(chunk, &world, 0, 0, 0);
	chunk_ref(chunk);

	// Below the cap reads as solid-ish "inside the world" (type 1, no skylight).
	struct block_data below = chunk_lookup_block(chunk, 0, WORLD_HEIGHT - 1, 0);
	ASSERT_EQ(below.type, 1);
	ASSERT_EQ(below.sky_light, 0U);

	// At and above the cap reads as open sky (type 0, full skylight). On PC the
	// new cap is 256, so y=255 is inside (checked above) but y=256 is sky.
	struct block_data above = chunk_lookup_block(chunk, 0, WORLD_HEIGHT, 0);
	ASSERT_EQ(above.type, 0);
	ASSERT_EQ(above.sky_light, 15U);

	chunk_unref(chunk);
}

const test_entry_t g_tests_chunk[] = {
	{"chunk_block_roundtrip", test_chunk_block_roundtrip},
	{"chunk_to_id_y_no_truncation", test_chunk_to_id_y_no_truncation},
};

const size_t g_tests_chunk_count
	= sizeof(g_tests_chunk) / sizeof(g_tests_chunk[0]);
