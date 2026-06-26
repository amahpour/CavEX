/*
	Copyright (c) 2022 ByteBit/xtreme8000

	This file is part of CavEX.

	CavEX is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	CavEX is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with CavEX.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <assert.h>

#include "daytime.h"
#include "game/game_state.h"
#include "util.h"

// Sleepable night window, in ticks within the 0..DAY_LENGTH_TICKS day cycle.
// Matches Minecraft Beta 1.7.3: a bed may be used from dusk (~12541) until just
// before dawn (~23458); dawn is tick 0. Outside this window it is "day" and a
// bed does nothing.
#define NIGHT_START_TICKS 12541
#define NIGHT_END_TICKS 23458

// True when `world_time` (a free-running tick counter) currently falls in the
// sleepable night window. world_time is reduced modulo the day length first, so
// it works on any day. Pure -- no game state -- so it is unit-tested directly.
bool daytime_is_night(uint64_t world_time) {
	uint64_t tod = world_time % DAY_LENGTH_TICKS;
	return tod >= NIGHT_START_TICKS && tod <= NIGHT_END_TICKS;
}

// The next dawn at or after `world_time`: the next whole-day boundary (a
// multiple of DAY_LENGTH_TICKS), i.e. time-of-day 0. Advancing the server clock
// to this value skips the remainder of the night while keeping the counter
// monotonic (so saved LEVEL_TIME never goes backwards). If world_time is already
// exactly a day boundary this returns the FOLLOWING dawn (a full day later);
// callers only invoke it at night, so that case never arises in practice.
uint64_t daytime_skip_to_dawn(uint64_t world_time) {
	return (world_time / DAY_LENGTH_TICKS + 1) * (uint64_t)DAY_LENGTH_TICKS;
}

float daytime_brightness(float time) {
	return (gstate.world.dimension == WORLD_DIM_OVERWORLD) ?
		glm_clamp(cosf(daytime_celestial_angle(time) * 2.0F * GLM_PIf) * 2.0F
					  + 0.5F,
				  0.0F, 1.0F) :
		0.0F;
}

float daytime_celestial_angle(float time) {
	float X = time - 0.25F;

	if(X < 0)
		X += 1;

	return X + ((1.0F - (cosf(X * GLM_PIf) + 1.0F) / 2.0F) - X) / 3.0F;
}

float daytime_star_brightness(float time) {
	float x = glm_clamp(
		0.25F - cosf(daytime_celestial_angle(time) * 2.0F * GLM_PIf) * 2.0F,
		0.0F, 1.0F);
	return x * x * 0.5F;
}

bool daytime_sunset_colors(float time, vec4 color, float* shift) {
	assert(color && shift);

	float sun_horizon_dist
		= cosf(daytime_celestial_angle(time) * GLM_PIf * 2.0F) * 1.25F + 0.5F;

	float s = 0.01F + 0.99F * sinf(sun_horizon_dist * GLM_PIf);
	*shift = s * s;

	color[0] = sun_horizon_dist * 0.3F + 0.7F;
	color[1] = sun_horizon_dist * sun_horizon_dist * 0.7F + 0.2F;
	color[2] = 0.2F;
	color[3] = *shift;

	glm_vec4_scale(color, 255.0F, color);

	return sun_horizon_dist >= 0.0F && sun_horizon_dist <= 1.0F;
}

void daytime_sky_colors(float time, vec3 top_plane, vec3 bottom_plane,
						vec3 atmosphere) {
	assert(top_plane && bottom_plane && atmosphere);

	if(gstate.world.dimension == WORLD_DIM_OVERWORLD) {
		float brightness_mul = daytime_brightness(time);

		/* vec3 world_sky_color = {
			0.6222222F - (0.7F / 3.0F) * 0.05F,
			0.5F + (0.7F / 3.0F) * 0.1F,
			1.0F,
		};

		hsv2rgb(world_sky_color + 0, world_sky_color + 1, world_sky_color +
		2); */

		vec3 world_sky_color = {
			0.5333333F,
			0.7333333F,
			1.0F,
		};

		glm_vec3_scale(world_sky_color, brightness_mul, world_sky_color);

		vec3 fog_color = {
			0.7529412F * (brightness_mul * 0.94F + 0.06F),
			0.8470588F * (brightness_mul * 0.94F + 0.06F),
			1.0F * (brightness_mul * 0.91F + 0.09F),
		};

		vec3 atmosphere_color;
		// 1 − (1 / (4 − 0)) ^ 0.25
		glm_vec3_lerp(fog_color, world_sky_color, 0.2929F, atmosphere_color);

		vec3 bottom_plane_color = {0.04F, 0.04F, 0.1F};
		glm_vec3_muladd(world_sky_color, (vec3) {0.2F, 0.2F, 0.6F},
						bottom_plane_color);

		glm_vec3_scale(atmosphere_color, 255.0F, atmosphere);
		glm_vec3_scale(world_sky_color, 255.0F, top_plane);
		glm_vec3_scale(bottom_plane_color, 255.0F, bottom_plane);
	} else {
		vec3 const_color = {0.2F, 0.03F, 0.03F};
		glm_vec3_scale(const_color, 255.0F, atmosphere);
		glm_vec3_scale(const_color, 255.0F, top_plane);
		glm_vec3_scale(const_color, 255.0F, bottom_plane);
	}
}
