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

const test_entry_t g_tests_daytime[] = {
	{"daytime_celestial_angle_wrap", test_daytime_celestial_angle_wrap},
	{"daytime_star_brightness_range", test_daytime_star_brightness_range},
	{"daytime_sunset_in_horizon", test_daytime_sunset_in_horizon},
	{"daytime_brightness_overworld", test_daytime_brightness_overworld},
	{"daytime_brightness_nether", test_daytime_brightness_nether},
	{"daytime_sky_colors_overworld", test_daytime_sky_colors_overworld},
	{"daytime_sky_colors_nether", test_daytime_sky_colors_nether},
};

const size_t g_tests_daytime_count
	= sizeof(g_tests_daytime) / sizeof(g_tests_daytime[0]);
