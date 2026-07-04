// Unit tests for the passive villager (#28 sub-epic PR #1): the pure wander
// math (entity_villager_wander) and the entity registration done by
// entity_villager(). Mirrors tests/test_boat.c. The server tick needs a live
// world/blocks and the render is #ifdef'd out of the test build, so only the
// world-free pieces are covered here.

#include <math.h>
#include <string.h>

#include "block/blocks_data.h"
#include "entity/entity.h"
#include "harness.h"

// Pure wander math. entity_villager_wander is branchless, so a single test
// exercises every line (a second math test would add no new coverage and FAIL
// the per-test coverage gate): heading nudge, forward/stand thrust along the
// heading, and the untouched vertical component.
TEST(villager_wander_math) {
	// At yaw 0, walking forward pushes along +z only (x = sin 0, z = cos 0).
	float yaw = 0.0F;
	vec3 vel = {0.0F, 7.0F, 0.0F}; // seed vertical non-zero to prove it is kept
	entity_villager_wander(&yaw, vel, 0, 1, VILLAGER_WANDER_SPEED);
	ASSERT(vel[2] > 0.0F);
	ASSERT_NEAR(vel[0], 0.0F, 1e-5);
	ASSERT_NEAR(vel[1], 7.0F, 1e-5); // vertical never written

	// Standing (forward 0) zeroes both horizontal components.
	float yaw0 = 0.0F;
	vec3 vel0 = {5.0F, 0.0F, 5.0F};
	entity_villager_wander(&yaw0, vel0, 0, 0, VILLAGER_WANDER_SPEED);
	ASSERT_NEAR(vel0[0], 0.0F, 1e-5);
	ASSERT_NEAR(vel0[2], 0.0F, 1e-5);

	// turn +1 advances the heading; turn -1 retreats it.
	float yawt = 0.0F;
	vec3 velt = {0.0F, 0.0F, 0.0F};
	entity_villager_wander(&yawt, velt, +1, 0, VILLAGER_WANDER_SPEED);
	ASSERT(yawt > 0.0F);
	entity_villager_wander(&yawt, velt, -1, 0, VILLAGER_WANDER_SPEED);
	entity_villager_wander(&yawt, velt, -1, 0, VILLAGER_WANDER_SPEED);
	ASSERT(yawt < 0.0F);

	// A quarter turn redirects the walk thrust onto +x.
	float yawq = glm_rad(90.0F);
	vec3 velq = {0.0F, 0.0F, 0.0F};
	entity_villager_wander(&yawq, velq, 0, 1, VILLAGER_WANDER_SPEED);
	ASSERT(velq[0] > 0.0F);
	ASSERT_NEAR(velq[2], 0.0F, 1e-5);
}

// The constructor tags the entity ENTITY_VILLAGER, wires every callback, and
// initialises the villager state.
TEST(villager_entity_registration) {
	struct entity e;
	int world_marker = 0;

	memset(&e, 0, sizeof(e));
	entity_villager(11, &e, true, &world_marker);

	ASSERT_EQ(e.id, 11U);
	ASSERT_EQ((int)e.type, (int)ENTITY_VILLAGER);
	ASSERT(e.tick_server != NULL);
	ASSERT(e.tick_client != NULL);
	ASSERT(e.render != NULL);
	ASSERT(e.teleport != NULL);
	ASSERT_EQ(e.data.villager.idle_timer, VILLAGER_IDLE_MIN);
	ASSERT_EQ(e.data.villager.wander_ticks, 0);
	ASSERT_NEAR(e.data.villager.yaw, 0.0F, 1e-6);
}

const test_entry_t g_tests_villager[] = {
	{"villager_wander_math", test_villager_wander_math},
	{"villager_entity_registration", test_villager_entity_registration},
};

const size_t g_tests_villager_count
	= sizeof(g_tests_villager) / sizeof(g_tests_villager[0]);
