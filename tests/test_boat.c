// Unit tests for the rideable boat (#34): the pure steering math
// (entity_boat_steer) and the entity registration done by entity_boat().

#include <string.h>

#include "block/blocks_data.h"
#include "entity/entity.h"
#include "harness.h"

// Pure steering math. entity_boat_steer is branchless, so a single test
// exercises every line (a second steering test would add no new coverage and
// fail the per-test coverage gate): turning, forward/reverse thrust along the
// heading, and horizontal drag.
TEST(boat_steer_math) {
	// turning right increases the heading, left decreases it, by BOAT_TURN_SPEED
	float yaw = 0.0F;
	vec3 vel = {0.0F, 0.0F, 0.0F};
	entity_boat_steer(&yaw, vel, 0, +1);
	ASSERT_NEAR(yaw, BOAT_TURN_SPEED, 1e-5);
	entity_boat_steer(&yaw, vel, 0, -1);
	entity_boat_steer(&yaw, vel, 0, -1);
	ASSERT_NEAR(yaw, -BOAT_TURN_SPEED, 1e-5);

	// at yaw 0 forward thrust pushes along +z only (x = sin 0, z = cos 0)
	float yawf = 0.0F;
	vec3 velf = {0.0F, 0.0F, 0.0F};
	entity_boat_steer(&yawf, velf, +1, 0);
	ASSERT(velf[2] > 0.0F);
	ASSERT_NEAR(velf[0], 0.0F, 1e-5);

	// reverse thrust pushes along -z
	float yawr = 0.0F;
	vec3 velr = {0.0F, 0.0F, 0.0F};
	entity_boat_steer(&yawr, velr, -1, 0);
	ASSERT(velr[2] < 0.0F);

	// a quarter turn redirects forward thrust along +x
	float yawq = glm_rad(90.0F);
	vec3 velq = {0.0F, 0.0F, 0.0F};
	entity_boat_steer(&yawq, velq, +1, 0);
	ASSERT(velq[0] > 0.0F);
	ASSERT_NEAR(velq[2], 0.0F, 1e-5);

	// with no input the velocity is dragged toward zero but keeps its direction
	float yawd = 0.0F;
	vec3 veld = {1.0F, 0.0F, 1.0F};
	entity_boat_steer(&yawd, veld, 0, 0);
	ASSERT(veld[0] < 1.0F && veld[0] > 0.0F);
	ASSERT(veld[2] < 1.0F && veld[2] > 0.0F);
	ASSERT_NEAR(veld[0], BOAT_DRAG, 1e-5);
}

// The constructor tags the entity ENTITY_BOAT, wires every callback, and zeroes
// the boat state.
TEST(boat_entity_registration) {
	struct entity e;
	int world_marker = 0;

	memset(&e, 0, sizeof(e));
	entity_boat(7, &e, true, &world_marker);

	ASSERT_EQ(e.id, 7U);
	ASSERT_EQ((int)e.type, (int)ENTITY_BOAT);
	ASSERT(e.tick_server != NULL);
	ASSERT(e.tick_client != NULL);
	ASSERT(e.render != NULL);
	ASSERT(e.teleport != NULL);
	ASSERT_EQ(e.data.boat.passenger_id, 0U);
	ASSERT_EQ(e.data.boat.control_forward, 0);
	ASSERT_EQ(e.data.boat.control_turn, 0);
}

const test_entry_t g_tests_boat[] = {
	{"boat_steer_math", test_boat_steer_math},
	{"boat_entity_registration", test_boat_entity_registration},
};

const size_t g_tests_boat_count
	= sizeof(g_tests_boat) / sizeof(g_tests_boat[0]);
