/*
	Copyright (c) 2026 ByteBit/xtreme8000

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

// Rideable, steerable minecart -- a boat that rolls on land. It reuses the boat
// steering math (entity_boat_steer) and the shared data.boat control/heading
// state, but falls under gravity with no buoyancy (boat-style scope: free
// driving, not strict rail-following). The render reuses the iron-block renderer
// (no new art) scaled to the cart box.

#include <assert.h>
#include <math.h>

#include "../block/blocks_data.h"
#include "../network/client_interface.h"
#include "../network/server_local.h"
#include "entity.h"
// NB: do NOT include platform/gfx.h here -- it pulls <GL/glew.h> (PC), absent in
// the headless CI test image, and this entity may be compiled into tooling. The
// render only calls item->renderItem (a function pointer) and entity_shadow.

static bool entity_minecart_client_tick(struct entity* e) {
	entity_default_client_tick(e);

	// Derive a render heading from horizontal travel (the server streams only
	// position), keeping the last heading while stationary -- same as the boat.
	float dx = e->pos[0] - e->pos_old[0];
	float dz = e->pos[2] - e->pos_old[2];
	if(dx * dx + dz * dz > 1.0e-4F)
		e->data.boat.yaw = atan2f(dx, dz);

	return false;
}

static bool entity_minecart_server_tick(struct entity* e,
										struct server_local* s) {
	assert(e);
	(void)s;

	glm_vec3_copy(e->pos, e->pos_old);
	glm_vec2_copy(e->orient, e->orient_old);

	// steering: yaw + thrust + horizontal drag from the last control packet
	float yaw = e->data.boat.yaw;
	entity_boat_steer(&yaw, e->vel, e->data.boat.control_forward,
					  e->data.boat.control_turn);
	e->data.boat.yaw = yaw;
	e->orient[0] = yaw;

	// consume the control so the cart coasts to a stop if input stops
	e->data.boat.control_forward = 0;
	e->data.boat.control_turn = 0;

	// A riderless cart damps harder so it settles instead of wandering off.
	if(e->data.boat.passenger_id == 0) {
		e->vel[0] *= MINECART_DRAG;
		e->vel[2] *= MINECART_DRAG;
	}

	for(int k = 0; k < 3; k++)
		if(fabsf(e->vel[k]) < 0.005F)
			e->vel[k] = 0.0F;

	// gravity (no buoyancy): the cart rolls on the ground.
	e->vel[1] -= MINECART_GRAVITY;
	e->vel[1] *= 0.95F;

	struct AABB bbox;
	aabb_setsize_centered(&bbox, MINECART_WIDTH, MINECART_HEIGHT,
						  MINECART_LENGTH);

	bool collision_xz = false;
	for(int k = 0; k < 3; k++)
		entity_try_move(e, e->pos, e->vel, &bbox, (size_t[]) {1, 0, 2}[k],
						&collision_xz, &e->on_ground);

	return false; // minecarts are never auto-destroyed
}

static void entity_minecart_render(struct entity* e, mat4 view,
								   float tick_delta) {
#ifndef CAVEX_TEST_BUILD
	vec3 pos_lerp;
	glm_vec3_lerp(e->pos_old, e->pos, tick_delta, pos_lerp);

	// Reuse the iron-block renderer for a metal cart box (no new art).
	struct item_data metal = {.id = BLOCK_IRON_CAST, .durability = 0, .count = 1};
	struct item* it = item_get(&metal);

	if(it) {
		mat4 model;
		glm_translate_make(model, pos_lerp);
		glm_rotate_y(model, e->data.boat.yaw, model);
		glm_scale(model,
				  (vec3) {MINECART_WIDTH, MINECART_HEIGHT, MINECART_LENGTH});
		glm_translate(model, (vec3) {-0.5F, -0.5F, -0.5F});

		mat4 mv;
		glm_mat4_mul(view, model, mv);
		it->renderItem(it, &metal, mv, true, R_ITEM_ENV_ENTITY);
	}

	struct AABB bbox;
	aabb_setsize_centered(&bbox, MINECART_WIDTH, 0.1F, MINECART_LENGTH);
	aabb_translate(&bbox, pos_lerp[0], pos_lerp[1] - MINECART_HEIGHT / 2.0F,
				   pos_lerp[2]);
	entity_shadow(e, &bbox, view);
#else
	(void)e;
	(void)view;
	(void)tick_delta;
#endif
}

void entity_minecart(uint32_t id, struct entity* e, bool server, void* world) {
	assert(e && world);

	e->id = id;
	e->tick_server = entity_minecart_server_tick;
	e->tick_client = entity_minecart_client_tick;
	e->render = entity_minecart_render;
	e->teleport = entity_default_teleport;
	e->type = ENTITY_MINECART;
	e->data.boat.yaw = 0.0F;
	e->data.boat.passenger_id = 0;
	e->data.boat.in_water = false;
	e->data.boat.control_forward = 0;
	e->data.boat.control_turn = 0;

	entity_default_init(e, server, world);
}
