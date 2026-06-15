// Minimal stubs so entity_local_player.c links into the test library for the
// detect_double_tap() unit test. entity_tick() (which references these) is never
// invoked by the tests — only the pure detect_double_tap() helper is exercised —
// so the bodies just need to satisfy the linker.

#include "entity/entity.h"
#include "platform/input.h"

void entity_default_init(struct entity* e, bool server, void* world) {
	(void)e;
	(void)server;
	(void)world;
}

void entity_default_teleport(struct entity* e, vec3 pos) {
	(void)e;
	(void)pos;
}

bool entity_get_block(struct entity* e, w_coord_t x, w_coord_t y, w_coord_t z,
					  struct block_data* blk) {
	(void)e;
	(void)x;
	(void)y;
	(void)z;
	(void)blk;
	return false;
}

bool entity_intersection(struct entity* e, struct AABB* a,
						 bool (*test)(struct AABB* entity,
									  struct block_info* blk_info)) {
	(void)e;
	(void)a;
	(void)test;
	return false;
}

bool entity_block_aabb_test(struct AABB* entity, struct block_info* blk_info) {
	(void)entity;
	(void)blk_info;
	return false;
}

bool entity_aabb_intersection(struct entity* e, struct AABB* a) {
	(void)e;
	(void)a;
	return false;
}

void entity_try_move(struct entity* e, vec3 pos, vec3 vel, struct AABB* bbox,
					 size_t coord, bool* collision_xz, bool* on_ground) {
	(void)e;
	(void)pos;
	(void)vel;
	(void)bbox;
	(void)coord;
	(void)collision_xz;
	(void)on_ground;
}

bool input_held(enum input_button b) {
	(void)b;
	return false;
}

bool input_pressed(enum input_button b) {
	(void)b;
	return false;
}
