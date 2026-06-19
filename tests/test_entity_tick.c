#include <stdint.h>
#include <string.h>

#include "entity/entity.h"
#include "harness.h"

// Regression coverage for issue #69: the server entity-tick loop used to call
// dict_entity_erase() while a dict_entity_it iterator over the same dict was
// still live. CavEX's dict is an open-addressing hash map that resizes and
// relocates its buckets on erase once the live count crosses a threshold, which
// invalidates the in-flight iterator and trips M*LIB's `index != NULL` contract
// (or reads freed memory) on the next access. entity_tick_all() fixes this by
// collecting the keys to remove during the walk and erasing them only after the
// iterator is released. This test drives that helper at a size that forces the
// dict to grow and shrink repeatedly — the old erase-while-iterating pattern
// crashes here under a Debug (asserts-on) build; collect-then-erase stays clean.
//
// Everything lives in one TEST because each scenario exercises the same
// entity_tick_all() lines; splitting them would make the per-test coverage gate
// flag the later cases as adding zero new lines.

// Removes 3 of every 4 entities. At N=4096 this drops the live count below the
// dict's shrink threshold (lower bound 0.2 * 8192 ~= 1638) mid-pass, forcing a
// resize-down + index realloc — exactly the event that invalidated the live
// iterator in the old erase-while-iterating loop (issue #69). Keeping only
// multiples of 4.
static bool remove_three_of_four(uint32_t key, struct entity* e, void* ctx) {
	(void)e;
	(void)ctx;
	return (key % 4) != 0;
}

static bool remove_none(uint32_t key, struct entity* e, void* ctx) {
	(void)key;
	(void)e;
	(void)ctx;
	return false;
}

static bool remove_all(uint32_t key, struct entity* e, void* ctx) {
	(void)key;
	(void)e;
	(void)ctx;
	return true;
}

struct count_ctx {
	int calls;
};

static bool count_cb(uint32_t key, struct entity* e, void* ctx) {
	(void)key;
	(void)e;
	((struct count_ctx*)ctx)->calls++;
	return false;
}

TEST(entity_tick_all_stress) {
	enum { N = 4096, ITERS = 64 };

	dict_entity_t dict;
	dict_entity_init(dict);

	struct entity ent;
	memset(&ent, 0, sizeof(ent));

	for(uint32_t k = 1; k <= N; k++)
		dict_entity_set_at(dict, k, ent);
	ASSERT_EQ(dict_entity_size(dict), (size_t)N);

	// Spawn many entities (forcing several dict grows), remove 3/4 through the
	// tick helper (forcing a shrink/realloc that would invalidate a live
	// iterator), re-add them, and repeat. Each pass asserts the removal count,
	// the surviving size, and that the dict is still consistently walkable.
	// With the old erase-while-iterating loop the mid-pass resize-down drops
	// or skips entities (or crashes); collect-then-erase removes exactly 3/4.
	for(int i = 0; i < ITERS; i++) {
		size_t removed = entity_tick_all(dict, remove_three_of_four, NULL);
		ASSERT_EQ(removed, (size_t)(N - N / 4));
		ASSERT_EQ(dict_entity_size(dict), (size_t)(N / 4));

		// Walking the dict after the erase pass would trip the M*LIB contract
		// if collect-then-erase had left the table inconsistent. Only multiples
		// of 4 survive, so the largest key is N and the next free id is N + 1.
		ASSERT_EQ(entity_gen_id(dict), (uint32_t)(N + 1));

		// Re-add the removed keys to force the table to grow again next round.
		for(uint32_t k = 1; k <= N; k++)
			if((k % 4) != 0)
				dict_entity_set_at(dict, k, ent);
		ASSERT_EQ(dict_entity_size(dict), (size_t)N);
	}

	// Boundary: removing nothing leaves the dict intact and exercises the
	// "no keys collected" path (the erase loop body never runs).
	ASSERT_EQ(entity_tick_all(dict, remove_none, NULL), 0U);
	ASSERT_EQ(dict_entity_size(dict), (size_t)N);

	// The callback context pointer is forwarded unchanged to every entity.
	struct count_ctx ctx = {0};
	ASSERT_EQ(entity_tick_all(dict, count_cb, &ctx), 0U);
	ASSERT_EQ(ctx.calls, N);

	// Walking an empty dict is a no-op.
	dict_entity_t empty;
	dict_entity_init(empty);
	ASSERT_EQ(entity_tick_all(empty, remove_all, NULL), 0U);
	dict_entity_clear(empty);

	// Removing everything clears the dict and leaves it valid afterwards.
	ASSERT_EQ(entity_tick_all(dict, remove_all, NULL), (size_t)N);
	ASSERT_EQ(dict_entity_size(dict), 0U);
	ASSERT_EQ(entity_gen_id(dict), 1U);

	dict_entity_clear(dict);
}

const test_entry_t g_tests_entity_tick[] = {
	{"entity_tick_all_stress", test_entity_tick_all_stress},
};

const size_t g_tests_entity_tick_count
	= sizeof(g_tests_entity_tick) / sizeof(g_tests_entity_tick[0]);
