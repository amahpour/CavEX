// Unit tests for the pure third-person walk-cycle step (issue #138).
// player_walk_anim(dist, &phase, &amp) is the seam the entity tick advances and
// the render samples: phase accumulates with distance travelled (wrapped to
// 2*pi) and the amplitude eases toward the speed-scaled swing target, capped at
// PLAYER_WALK_MAX_SWING and settling back to exactly 0 at rest.

#include "entity/entity.h"
#include "harness.h"

// Standing still advances nothing and keeps the limbs at rest.
TEST(player_anim_rest_is_zero) {
	float phase = 0.0F, amp = 0.0F;

	for(int k = 0; k < 20; k++)
		player_walk_anim(0.0F, &phase, &amp);

	ASSERT_NEAR(phase, 0.0F, 1e-6);
	ASSERT_NEAR(amp, 0.0F, 1e-6);
}

// Walking ramps the amplitude toward (never past) the cap; stopping eases it
// back to exactly zero with the limbs frozen in place (phase must not drift).
TEST(player_anim_walk_ramps_up_and_settles) {
	float phase = 0.0F, amp = 0.0F;

	// full walking speed for 2 s of ticks
	for(int k = 0; k < 40; k++)
		player_walk_anim(PLAYER_WALK_REF_SPEED, &phase, &amp);

	ASSERT(amp > PLAYER_WALK_MAX_SWING * 0.9F);
	ASSERT(amp <= PLAYER_WALK_MAX_SWING + 1e-4F);

	// phase advanced 40 * dist * rate radians, wrapped into [0, 2*pi)
	ASSERT(phase >= 0.0F);
	ASSERT(phase < 2.0F * 3.14159265F);

	float phase_at_stop = phase;
	for(int k = 0; k < 60; k++)
		player_walk_anim(0.0F, &phase, &amp);

	ASSERT_NEAR(amp, 0.0F, 1e-6);
	ASSERT_NEAR(phase, phase_at_stop, 1e-6);
}

// Overspeed (sprint-like bursts) still clamps the swing target at the cap.
TEST(player_anim_overspeed_clamps) {
	float phase = 0.0F, amp = 0.0F;

	for(int k = 0; k < 60; k++)
		player_walk_anim(PLAYER_WALK_REF_SPEED * 5.0F, &phase, &amp);

	ASSERT(amp <= PLAYER_WALK_MAX_SWING + 1e-4F);
}

// Negative distances (defensive: lerp jitter) are treated as no movement.
TEST(player_anim_negative_dist_is_rest) {
	float phase = 1.0F, amp = 5.0F;

	player_walk_anim(-1.0F, &phase, &amp);

	ASSERT_NEAR(phase, 1.0F, 1e-6);
	ASSERT(amp < 5.0F); // eases down like rest
}

const test_entry_t g_tests_player_anim[] = {
	{"player_anim_rest_is_zero", test_player_anim_rest_is_zero},
	{"player_anim_walk_ramps_up_and_settles",
	 test_player_anim_walk_ramps_up_and_settles},
	{"player_anim_overspeed_clamps", test_player_anim_overspeed_clamps},
	{"player_anim_negative_dist_is_rest", test_player_anim_negative_dist_is_rest},
};

const size_t g_tests_player_anim_count
	= sizeof(g_tests_player_anim) / sizeof(g_tests_player_anim[0]);
