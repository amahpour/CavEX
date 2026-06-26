#include "daytime.h"
#include "game/game_state.h"
#include "harness.h"
#include "world.h"

static void set_dimension(enum world_dim dim) {
	gstate.world.dimension = dim;
}

TEST(daytime_celestial_angle_wrap) {
	float wrapped = daytime_celestial_angle(0.1F);

	ASSERT(wrapped >= 0.0F && wrapped <= 1.0F);
	ASSERT_NEAR(daytime_celestial_angle(1.1F), wrapped, 0.001F);
}

TEST(daytime_star_brightness_range) {
	float noon = daytime_star_brightness(0.25F);
	float night = daytime_star_brightness(0.75F);

	ASSERT(noon >= 0.0F && noon <= 1.0F);
	ASSERT(night >= 0.0F && night <= 1.0F);
	ASSERT(night >= noon);
}

TEST(daytime_sunset_in_horizon) {
	vec4 color = {0};
	float shift = 0.0F;

	ASSERT(daytime_sunset_colors(0.0F, color, &shift));
	ASSERT(shift > 0.0F);
	ASSERT(color[3] > 0.0F);

	color[0] = color[1] = color[2] = color[3] = 0.0F;
	shift = 0.0F;
	ASSERT(!daytime_sunset_colors(0.75F, color, &shift));
}

TEST(daytime_brightness_overworld) {
	set_dimension(WORLD_DIM_OVERWORLD);
	ASSERT(daytime_brightness(0.25F) > 0.0F);
}

TEST(daytime_brightness_nether) {
	set_dimension(WORLD_DIM_NETHER);
	ASSERT_EQ(daytime_brightness(0.25F), 0.0F);
}

TEST(daytime_sky_colors_overworld) {
	vec3 top = {0};
	vec3 bottom = {0};
	vec3 atmosphere = {0};

	set_dimension(WORLD_DIM_OVERWORLD);
	daytime_sky_colors(0.25F, top, bottom, atmosphere);
	ASSERT(top[0] > 0.0F);
	ASSERT(bottom[0] > 0.0F);
	ASSERT(atmosphere[0] > 0.0F);
}

TEST(daytime_sky_colors_nether) {
	vec3 top = {0};
	vec3 bottom = {0};
	vec3 atmosphere = {0};

	set_dimension(WORLD_DIM_NETHER);
	daytime_sky_colors(0.25F, top, bottom, atmosphere);
	ASSERT_NEAR(top[0], 51.0F, 0.1F);
	ASSERT_NEAR(bottom[1], 7.65F, 0.1F);
	ASSERT_NEAR(atmosphere[2], 7.65F, 0.1F);
}

// Bed/sleep clock helpers (issue #117): the night window and the skip-to-dawn
// advance that block_bed's onRightClick uses.
TEST(daytime_is_night_window) {
	// Daytime ticks are NOT night: dawn (0), morning, noon (6000), dusk start.
	ASSERT(!daytime_is_night(0));
	ASSERT(!daytime_is_night(6000));
	ASSERT(!daytime_is_night(12000));	   // still dusk, before the window
	ASSERT(!daytime_is_night(23459));	   // just after the window -> day again
	ASSERT(!daytime_is_night(24000));	   // next dawn

	// Night ticks ARE night: window edges (12541, 23458) and midnight (18000).
	ASSERT(daytime_is_night(12541));
	ASSERT(daytime_is_night(18000));
	ASSERT(daytime_is_night(23458));

	// Works on any day: the same time-of-day on day 3 reads the same.
	ASSERT(daytime_is_night(18000 + 3 * DAY_LENGTH_TICKS));
	ASSERT(!daytime_is_night(6000 + 3 * DAY_LENGTH_TICKS));
}

TEST(daytime_skip_to_dawn_advances_to_next_day_boundary) {
	// From any tick within day 0, dawn is the next multiple of the day length.
	ASSERT_EQ(daytime_skip_to_dawn(18000), (uint64_t)DAY_LENGTH_TICKS);
	ASSERT_EQ(daytime_skip_to_dawn(12541), (uint64_t)DAY_LENGTH_TICKS);
	// On a later day, it lands on THAT day's following dawn (monotonic; the
	// saved clock never goes backwards).
	ASSERT_EQ(daytime_skip_to_dawn(18000 + 2 * DAY_LENGTH_TICKS),
			  (uint64_t)3 * DAY_LENGTH_TICKS);
	// Sanity: the result is always a whole-day boundary (time-of-day 0) and
	// strictly ahead of the input.
	uint64_t t = 20000 + 5 * DAY_LENGTH_TICKS;
	uint64_t dawn = daytime_skip_to_dawn(t);
	ASSERT(dawn > t);
	ASSERT_EQ(dawn % DAY_LENGTH_TICKS, 0u);
	ASSERT(!daytime_is_night(dawn));  // and dawn is daytime
}

const test_entry_t g_tests_daytime[] = {
	{"daytime_celestial_angle_wrap", test_daytime_celestial_angle_wrap},
	{"daytime_star_brightness_range", test_daytime_star_brightness_range},
	{"daytime_sunset_in_horizon", test_daytime_sunset_in_horizon},
	{"daytime_brightness_overworld", test_daytime_brightness_overworld},
	{"daytime_brightness_nether", test_daytime_brightness_nether},
	{"daytime_sky_colors_overworld", test_daytime_sky_colors_overworld},
	{"daytime_sky_colors_nether", test_daytime_sky_colors_nether},
	{"daytime_is_night_window", test_daytime_is_night_window},
	{"daytime_skip_to_dawn_advances_to_next_day_boundary",
	 test_daytime_skip_to_dawn_advances_to_next_day_boundary},
};

const size_t g_tests_daytime_count
	= sizeof(g_tests_daytime) / sizeof(g_tests_daytime[0]);
