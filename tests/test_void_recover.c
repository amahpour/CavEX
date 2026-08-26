// Unit tests for the pure void-fall recovery step (2026-08-26). A player only
// reaches a negative eye Y by falling out of the world (spawn over not-yet-loaded
// chunks, or an islands gap); player_void_recover_step() records the last solid
// spot and snaps them back instead of letting them fall forever.

#include "entity/entity.h"
#include "harness.h"

// Standing on solid ground above y=0 records the spot and does not recover.
TEST(void_recover_records_safe_ground) {
	vec3 pos = {10.0F, 65.0F, 20.0F}, vel = {0, 0, 0}, last = {0, 0, 0};
	bool has = false;

	ASSERT(!player_void_recover_step(pos, vel, true, last, &has));
	ASSERT(has);
	ASSERT_NEAR(last[0], 10.0F, 1e-5);
	ASSERT_NEAR(last[1], 65.0F, 1e-5);
	ASSERT_NEAR(last[2], 20.0F, 1e-5);
}

// Falling out of the world with a known safe spot snaps back there and kills the
// velocity (so the endless void fall stops).
TEST(void_recover_snaps_back_to_last_safe) {
	vec3 last = {10.0F, 65.0F, 20.0F};
	vec3 pos = {37.0F, -3613.0F, 51.0F}, vel = {0.1F, -3.0F, 0.2F};
	bool has = true;

	ASSERT(player_void_recover_step(pos, vel, false, last, &has));
	ASSERT_NEAR(pos[0], 10.0F, 1e-4);
	ASSERT_NEAR(pos[1], 65.0F, 1e-4);
	ASSERT_NEAR(pos[2], 20.0F, 1e-4);
	ASSERT_NEAR(vel[0], 0.0F, 1e-6);
	ASSERT_NEAR(vel[1], 0.0F, 1e-6);
	ASSERT_NEAR(vel[2], 0.0F, 1e-6);
}

// Falling before ever touching ground (spawned straight into a gap) lifts back
// up in place instead of falling forever.
TEST(void_recover_lifts_when_no_safe_yet) {
	vec3 last = {0, 0, 0};
	vec3 pos = {5.0F, -50.0F, 6.0F}, vel = {0.0F, -2.0F, 0.0F};
	bool has = false;

	ASSERT(player_void_recover_step(pos, vel, false, last, &has));
	ASSERT(pos[1] > 0.0F);			 // lifted back into the world
	ASSERT_NEAR(pos[0], 5.0F, 1e-6); // x/z kept
	ASSERT_NEAR(pos[2], 6.0F, 1e-6);
}

// A normal mid-air position (jumping/short fall, still above the void floor)
// neither records a safe spot nor recovers.
TEST(void_recover_ignores_normal_fall) {
	vec3 last = {1.0F, 2.0F, 3.0F};
	vec3 pos = {5.0F, 40.0F, 6.0F}, vel = {0.0F, -0.5F, 0.0F};
	bool has = false;

	ASSERT(!player_void_recover_step(pos, vel, false, last, &has));
	ASSERT(!has);
	ASSERT_NEAR(pos[1], 40.0F, 1e-6); // untouched
}

const test_entry_t g_tests_void_recover[] = {
	{"void_recover_records_safe_ground", test_void_recover_records_safe_ground},
	{"void_recover_snaps_back_to_last_safe",
	 test_void_recover_snaps_back_to_last_safe},
	{"void_recover_lifts_when_no_safe_yet",
	 test_void_recover_lifts_when_no_safe_yet},
	{"void_recover_ignores_normal_fall",
	 test_void_recover_ignores_normal_fall},
};

const size_t g_tests_void_recover_count
	= sizeof(g_tests_void_recover) / sizeof(g_tests_void_recover[0]);
