// Unit tests for the rail-following minecart (#111): the PURE rail math
// (metadata -> track ends/direction, velocity projection onto the track, and
// the along-track speed cap) and the entity registration done by
// entity_minecart(). Mirrors tests/test_boat.c. The rail-follow server tick
// itself needs a live world, so only the world-free pieces are covered here.

#include <math.h>
#include <string.h>

#include "block/blocks_data.h"
#include "entity/entity.h"
#include "harness.h"

// metadata -> track ends, and the horizontal direction derived from them. One
// test walks every shape row (straights, slopes, curves) so each line of the
// matrix lookup + the normalise is exercised; a second direction test would add
// no new coverage and fail the per-test gate.
TEST(minecart_rail_ends_and_direction) {
	ivec3 e0, e1;

	// Out-of-range shapes are rejected and leave the outputs untouched.
	e0[0] = e0[1] = e0[2] = 77;
	ASSERT(!minecart_rail_ends(-1, e0, e1));
	ASSERT(!minecart_rail_ends(10, e0, e1));
	ASSERT_EQ(e0[0], 77);
	vec3 d = {77.0F, 77.0F, 77.0F};
	ASSERT(!minecart_rail_direction(10, d));
	ASSERT_NEAR(d[0], 77.0F, 1e-5);

	// Shape 0 is north-south: ends straddle z, direction is +/-z (unit, no x).
	ASSERT(minecart_rail_ends(0, e0, e1));
	ASSERT_EQ(e0[2], -1);
	ASSERT_EQ(e1[2], 1);
	ASSERT_EQ(e0[0], 0);
	ASSERT_EQ(e1[0], 0);
	ASSERT(minecart_rail_direction(0, d));
	ASSERT_NEAR(d[0], 0.0F, 1e-5);
	ASSERT_NEAR(d[1], 0.0F, 1e-5);
	ASSERT_NEAR(fabsf(d[2]), 1.0F, 1e-5);

	// Shape 1 is east-west: direction is +/-x, no z.
	ASSERT(minecart_rail_direction(1, d));
	ASSERT_NEAR(fabsf(d[0]), 1.0F, 1e-5);
	ASSERT_NEAR(d[2], 0.0F, 1e-5);

	// Every shape returns a horizontal unit vector (length 1, y == 0). This walks
	// all ten matrix rows incl. the diagonal curves (length sqrt(2) before norm).
	for(int shape = 0; shape <= 9; shape++) {
		ASSERT(minecart_rail_ends(shape, e0, e1));
		ASSERT(minecart_rail_direction(shape, d));
		float len = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
		ASSERT_NEAR(len, 1.0F, 1e-5);
		ASSERT_NEAR(d[1], 0.0F, 1e-5);
	}

	// A curve (shape 6, south-east) is a true diagonal: both x and z components
	// are non-zero and equal in magnitude (45 degrees).
	ASSERT(minecart_rail_direction(6, d));
	ASSERT(fabsf(d[0]) > 0.1F && fabsf(d[2]) > 0.1F);
	ASSERT_NEAR(fabsf(d[0]), fabsf(d[2]), 1e-5);
}

// Travel direction through a shape: straights keep the cart's sense; curves
// route the cart onto the outgoing arm (the bend), which is what keeps a cart
// circulating a closed loop instead of driving off the diagonal at a corner.
TEST(minecart_rail_travel_dir_math) {
	vec3 d;

	// Out-of-range shape is rejected, output untouched.
	d[0] = 5.0F;
	vec3 anyv = {1.0F, 0.0F, 0.0F};
	ASSERT(!minecart_rail_travel_dir(11, anyv, d));
	ASSERT_NEAR(d[0], 5.0F, 1e-5);

	// Straight N-S (shape 0): a cart moving +z travels +z; moving -z travels -z.
	vec3 vz_pos = {0.0F, 0.0F, 0.3F};
	ASSERT(minecart_rail_travel_dir(0, vz_pos, d));
	ASSERT_NEAR(d[0], 0.0F, 1e-5);
	ASSERT(d[2] > 0.9F);
	vec3 vz_neg = {0.0F, 0.0F, -0.3F};
	ASSERT(minecart_rail_travel_dir(0, vz_neg, d));
	ASSERT(d[2] < -0.9F);

	// Straight E-W (shape 1): a cart moving +x travels +x.
	vec3 vx = {0.4F, 0.0F, 0.0F};
	ASSERT(minecart_rail_travel_dir(1, vx, d));
	ASSERT(d[0] > 0.9F);
	ASSERT_NEAR(d[2], 0.0F, 1e-5);

	// Curve NE (shape 7): connects +z (south) and -x (west). A cart arriving from
	// the west moving +x must exit SOUTH (+z), rounding the bend -- NOT continue
	// +x off the track. The result is a horizontal unit vector pointing +z.
	vec3 v_east = {0.3F, 0.0F, 0.0F};
	ASSERT(minecart_rail_travel_dir(7, v_east, d));
	ASSERT_NEAR(d[0], 0.0F, 1e-5);
	ASSERT_NEAR(d[1], 0.0F, 1e-5);
	ASSERT_NEAR(d[2], 1.0F, 1e-5);

	// Curve SE (shape 8): connects -z and -x. A cart arriving from the north
	// moving +z (down the east edge) exits WEST (-x) along the bottom edge.
	vec3 v_south = {0.0F, 0.0F, 0.3F};
	ASSERT(minecart_rail_travel_dir(8, v_south, d));
	ASSERT_NEAR(d[0], -1.0F, 1e-5);
	ASSERT_NEAR(d[2], 0.0F, 1e-5);

	// Every shape yields a horizontal unit travel direction for an arbitrary
	// velocity (exercises both the straight branch and the curve branch fully).
	vec3 vdiag = {0.2F, 0.0F, 0.1F};
	for(int shape = 0; shape <= 9; shape++) {
		ASSERT(minecart_rail_travel_dir(shape, vdiag, d));
		float len = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
		ASSERT_NEAR(len, 1.0F, 1e-5);
		ASSERT_NEAR(d[1], 0.0F, 1e-5);
	}
}

// Velocity projection: an arbitrary horizontal velocity is collapsed onto the
// track unit direction (across-track component dropped, along-track sign kept),
// and the vertical component is untouched.
TEST(minecart_project_velocity_math) {
	// Track along +z: an x+z velocity keeps only its z part; x is zeroed.
	vec3 dirZ = {0.0F, 0.0F, 1.0F};
	vec3 v = {0.3F, 0.9F, 0.2F};
	minecart_project_velocity(v, dirZ);
	ASSERT_NEAR(v[0], 0.0F, 1e-5);
	ASSERT_NEAR(v[2], 0.2F, 1e-5);
	ASSERT_NEAR(v[1], 0.9F, 1e-5); // vertical untouched

	// Track along +x: symmetric -- keep x, drop z.
	vec3 dirX = {1.0F, 0.0F, 0.0F};
	vec3 v2 = {0.4F, 0.0F, 0.5F};
	minecart_project_velocity(v2, dirX);
	ASSERT_NEAR(v2[0], 0.4F, 1e-5);
	ASSERT_NEAR(v2[2], 0.0F, 1e-5);

	// Moving against the track direction keeps the (negative) along-track sign,
	// so the cart does not get flipped around by the projection.
	vec3 v3 = {0.0F, 0.0F, -0.6F};
	minecart_project_velocity(v3, dirZ);
	ASSERT(v3[2] < 0.0F);
	ASSERT_NEAR(v3[2], -0.6F, 1e-5);

	// A purely across-track velocity projects to zero (cart cannot move sideways
	// off the rail).
	vec3 v4 = {0.7F, 0.0F, 0.0F};
	minecart_project_velocity(v4, dirZ);
	ASSERT_NEAR(v4[0], 0.0F, 1e-5);
	ASSERT_NEAR(v4[2], 0.0F, 1e-5);

	// On a 45-degree curve direction, an axis-aligned velocity projects onto the
	// diagonal with both components equal.
	vec3 dirDiag = {0.70710678F, 0.0F, 0.70710678F};
	vec3 v5 = {1.0F, 0.0F, 0.0F};
	minecart_project_velocity(v5, dirDiag);
	ASSERT_NEAR(v5[0], v5[2], 1e-5);
	ASSERT(v5[0] > 0.0F);
}

// Along-track speed cap: horizontal speed is clamped to the cap (x/z scaled
// together so the heading is preserved); below the cap it is a no-op and the
// vertical component is never touched. Branches: clamp / no-clamp.
TEST(minecart_speed_cap_math) {
	// Below the cap -> untouched.
	vec3 slow = {0.1F, 0.5F, 0.1F};
	minecart_speed_cap(slow, MINECART_MAX_SPEED);
	ASSERT_NEAR(slow[0], 0.1F, 1e-5);
	ASSERT_NEAR(slow[2], 0.1F, 1e-5);
	ASSERT_NEAR(slow[1], 0.5F, 1e-5);

	// Over the cap on a diagonal -> clamped to exactly the cap, components stay
	// equal (heading preserved), vertical untouched.
	vec3 fast = {10.0F, 0.5F, 10.0F};
	minecart_speed_cap(fast, MINECART_MAX_SPEED);
	float sp = sqrtf(fast[0] * fast[0] + fast[2] * fast[2]);
	ASSERT_NEAR(sp, MINECART_MAX_SPEED, 1e-4);
	ASSERT_NEAR(fast[0], fast[2], 1e-4);
	ASSERT_NEAR(fast[1], 0.5F, 1e-5);

	// Exactly at the cap is treated as not-over (no scaling, no NaN).
	vec3 at = {MINECART_MAX_SPEED, 0.0F, 0.0F};
	minecart_speed_cap(at, MINECART_MAX_SPEED);
	ASSERT_NEAR(at[0], MINECART_MAX_SPEED, 1e-5);
}

// The constructor tags the entity ENTITY_MINECART, wires every callback, and
// zeroes the shared boat state (yaw/passenger/controls/powered).
TEST(minecart_entity_registration) {
	struct entity e;
	int world_marker = 0;

	memset(&e, 0, sizeof(e));
	entity_minecart(9, &e, true, &world_marker);

	ASSERT_EQ(e.id, 9U);
	ASSERT_EQ((int)e.type, (int)ENTITY_MINECART);
	ASSERT(e.tick_server != NULL);
	ASSERT(e.tick_client != NULL);
	ASSERT(e.render != NULL);
	ASSERT(e.teleport != NULL);
	ASSERT_EQ(e.data.boat.passenger_id, 0U);
	ASSERT_EQ(e.data.boat.control_forward, 0);
	ASSERT_EQ(e.data.boat.control_turn, 0);
	ASSERT(!e.data.boat.powered);
}

const test_entry_t g_tests_minecart[] = {
	{"minecart_rail_ends_and_direction", test_minecart_rail_ends_and_direction},
	{"minecart_rail_travel_dir_math", test_minecart_rail_travel_dir_math},
	{"minecart_project_velocity_math", test_minecart_project_velocity_math},
	{"minecart_speed_cap_math", test_minecart_speed_cap_math},
	{"minecart_entity_registration", test_minecart_entity_registration},
};

const size_t g_tests_minecart_count
	= sizeof(g_tests_minecart) / sizeof(g_tests_minecart[0]);
