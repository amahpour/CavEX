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
#include <malloc.h>
#include <math.h>
#include <stddef.h>
#include <string.h>

#include "block/blocks.h"
#include "chunk.h"
#include "game/game_state.h"
#include "platform/gfx.h"

bool chunk_check_built(struct chunk* c) {
	assert(c);

	if(c->rebuild_displist && chunk_mesher_send(c)) {
		c->rebuild_displist = false;
		return true;
	}

	return false;
}

void chunk_pre_render(struct chunk* c, mat4 view, bool has_fog) {
	assert(c && view);

	glm_translate_to(view, (vec3) {c->x, c->y, c->z}, c->model_view);
	c->has_fog = has_fog;
}

static void check_matrix_set(struct chunk* c, bool* needs_matrix) {
	assert(c && needs_matrix);

	if(*needs_matrix) {
		gfx_matrix_modelview(c->model_view);
		gfx_fog(c->has_fog);
		gfx_fog_pos(c->x - gstate.camera.x, c->z - gstate.camera.z,
					gstate.config.fog_distance);
		*needs_matrix = false;
	}
}

void chunk_render(struct chunk* c, bool pass, float x, float y, float z) {
	assert(c);

	bool needs_matrix = true;
	int offset = pass ? 6 : 0;

	if(y < c->y + CHUNK_SIZE && c->has_displist[SIDE_BOTTOM + offset]) {
		check_matrix_set(c, &needs_matrix);
		displaylist_render(c->mesh + SIDE_BOTTOM + offset);
	}

	if(y > c->y && c->has_displist[SIDE_TOP + offset]) {
		check_matrix_set(c, &needs_matrix);
		displaylist_render(c->mesh + SIDE_TOP + offset);
	}

	if(x < c->x + CHUNK_SIZE && c->has_displist[SIDE_LEFT + offset]) {
		check_matrix_set(c, &needs_matrix);
		displaylist_render(c->mesh + SIDE_LEFT + offset);
	}

	if(x > c->x && c->has_displist[SIDE_RIGHT + offset]) {
		check_matrix_set(c, &needs_matrix);
		displaylist_render(c->mesh + SIDE_RIGHT + offset);
	}

	if(z < c->z + CHUNK_SIZE && c->has_displist[SIDE_FRONT + offset]) {
		check_matrix_set(c, &needs_matrix);
		displaylist_render(c->mesh + SIDE_FRONT + offset);
	}

	if(z > c->z && c->has_displist[SIDE_BACK + offset]) {
		check_matrix_set(c, &needs_matrix);
		displaylist_render(c->mesh + SIDE_BACK + offset);
	}

	if(!pass && c->has_displist[12]) {
		check_matrix_set(c, &needs_matrix);
		gfx_cull_func(MODE_NONE);
		displaylist_render(c->mesh + 12);
		gfx_cull_func(MODE_BACK);
	}
}
