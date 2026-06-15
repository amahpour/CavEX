#include <string.h>

#include "block/blocks_data.h"
#include "entity/entity.h"
#include "harness.h"

TEST(entity_gen_id_empty) {
	dict_entity_t dict;
	dict_entity_init(dict);
	ASSERT_EQ(entity_gen_id(dict), 1U);
	dict_entity_clear(dict);
}

TEST(entity_gen_id_gap) {
	dict_entity_t dict;
	struct entity ent = {0};

	dict_entity_init(dict);
	dict_entity_set_at(dict, 5, ent);
	dict_entity_set_at(dict, 10, ent);
	ASSERT_EQ(entity_gen_id(dict), 11U);
	dict_entity_clear(dict);
}

const test_entry_t g_tests_entity[] = {
	{"entity_gen_id_empty", test_entity_gen_id_empty},
	{"entity_gen_id_gap", test_entity_gen_id_gap},
};

const size_t g_tests_entity_count
	= sizeof(g_tests_entity) / sizeof(g_tests_entity[0]);
