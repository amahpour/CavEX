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

#ifndef DAYTIME_H
#define DAYTIME_H

#include <stdbool.h>
#include <stdint.h>

#include "cglm/cglm.h"

#define DAY_TICK_MS 50
#define DAY_LENGTH_TICKS 24000

float daytime_brightness(float time);
float daytime_celestial_angle(float time);
float daytime_star_brightness(float time);
void daytime_sky_colors(float time, vec3 top_plane, vec3 bottom_plane,
						vec3 atmosphere);
bool daytime_sunset_colors(float time, vec4 color, float* shift);

// Bed/sleep helpers (issue #117). world_time is the free-running tick counter.
bool daytime_is_night(uint64_t world_time);
uint64_t daytime_skip_to_dawn(uint64_t world_time);

#endif
