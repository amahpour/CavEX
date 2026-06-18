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

// Rideable, steerable boat entity (issue #34). Modelled on entity_item.c: the
// boat is a server-authoritative entity that buoys on water, falls under gravity
// on land, and applies steering input forwarded by the riding client through
// SRPC_BOAT_CONTROL. The render reuses the wooden-planks block renderer (no new
// art) scaled to the hull box.

#include <assert.h>
#include <math.h>

#include "../block/blocks_data.h"
#include "../network/client_interface.h"
#include "../network/server_local.h"
#include "entity.h"
// NB: do NOT include platform/gfx.h here. It pulls <GL/glew.h> (PC) which is
// absent in the headless CI test image, and this entity is compiled into the
// unit-test library. The render below only calls item->renderItem (a function
// pointer) and entity_shadow (declared in entity.h) -- no direct gfx_* calls --
// so no graphics header is needed.

// Pure steering math — see declaration in entity.h. Keeps no engine state so it
// can be unit-tested in isolation.
void entity_boat_steer(float* yaw, vec3 vel, int forward, int turn) {
	assert(yaw && vel);

	*yaw += (float)turn * BOAT_TURN_SPEED;

	vel[0] += (float)forward * BOAT_ACCEL * sinf(*yaw);
	vel[2] += (float)forward * BOAT_ACCEL * cosf(*yaw);

	vel[0] *= BOAT_DRAG;
	vel[2] *= BOAT_DRAG;
}

static bool entity_boat_client_tick(struct entity* e) {
	entity_default_client_tick(e);

	// The server only streams position (CRPC_ENTITY_MOVE), not yaw, so derive a
	// render heading from the hull's horizontal travel. While moving this tracks
	// the steered heading; when stationary the last heading is kept.
	float dx = e->pos[0] - e->pos_old[0];
	float dz = e->pos[2] - e->pos_old[2];
	if(dx * dx + dz * dz > 1.0e-4F)
		e->data.boat.yaw = atan2f(dx, dz);

	return false;
}

static bool entity_boat_server_tick(struct entity* e, struct server_local* s) {
	assert(e);
	(void)s; // boat physics is self-contained; no player state needed

	glm_vec3_copy(e->pos, e->pos_old);
	glm_vec2_copy(e->orient, e->orient_old);

	// steering: yaw + thrust + horizontal drag from the last control packet
	float yaw = e->data.boat.yaw;
	entity_boat_steer(&yaw, e->vel, e->data.boat.control_forward,
					  e->data.boat.control_turn);
	e->data.boat.yaw = yaw;
	e->orient[0] = yaw;

	// consume the control so the boat coasts to a stop if the rider dismounts or
	// stops sending input
	e->data.boat.control_forward = 0;
	e->data.boat.control_turn = 0;

	for(int k = 0; k < 3; k++)
		if(fabsf(e->vel[k]) < 0.005F)
			e->vel[k] = 0.0F;

	// buoyancy: sample water at the hull bottom and top. Fully submerged -> rise
	// toward the surface; straddling the surface -> damp and settle; in air ->
	// fall under gravity.
	struct block_data blk;
	bool feet_water
		= entity_get_block(e, floorf(e->pos[0]), floorf(e->pos[1] - 0.25F),
						   floorf(e->pos[2]), &blk)
		&& (blk.type == BLOCK_WATER_FLOW || blk.type == BLOCK_WATER_STILL);
	bool head_water
		= entity_get_block(e, floorf(e->pos[0]), floorf(e->pos[1] + 0.25F),
						   floorf(e->pos[2]), &blk)
		&& (blk.type == BLOCK_WATER_FLOW || blk.type == BLOCK_WATER_STILL);
	e->data.boat.in_water = feet_water || head_water;

	if(head_water)
		e->vel[1] += BOAT_BUOYANCY;
	else if(feet_water)
		e->vel[1] *= 0.5F;
	else
		e->vel[1] -= BOAT_GRAVITY;
	e->vel[1] *= 0.9F;

	struct AABB bbox;
	aabb_setsize_centered(&bbox, BOAT_WIDTH, BOAT_HEIGHT, BOAT_LENGTH);

	bool collision_xz = false;
	for(int k = 0; k < 3; k++)
		entity_try_move(e, e->pos, e->vel, &bbox, (size_t[]) {1, 0, 2}[k],
						&collision_xz, &e->on_ground);

	return false; // boats are never auto-destroyed
}

static void entity_boat_render(struct entity* e, mat4 view, float tick_delta) {
#ifndef CAVEX_TEST_BUILD
	vec3 pos_lerp;
	glm_vec3_lerp(e->pos_old, e->pos, tick_delta, pos_lerp);

	// Reuse the planks block renderer for a wooden hull box (no new art). The
	// unit cube it draws is rotated by the heading and scaled to the hull size.
	struct item_data plank
		= {.id = BLOCK_PLANKS, .durability = 0, .count = 1};
	struct item* it = item_get(&plank);

	if(it) {
		mat4 model;
		glm_translate_make(model, pos_lerp);
		glm_rotate_y(model, e->data.boat.yaw, model);
		glm_scale(model, (vec3) {BOAT_WIDTH, BOAT_HEIGHT, BOAT_LENGTH});
		glm_translate(model, (vec3) {-0.5F, -0.5F, -0.5F});

		mat4 mv;
		glm_mat4_mul(view, model, mv);
		it->renderItem(it, &plank, mv, true, R_ITEM_ENV_ENTITY);
	}

	struct AABB bbox;
	aabb_setsize_centered(&bbox, BOAT_WIDTH, 0.1F, BOAT_LENGTH);
	aabb_translate(&bbox, pos_lerp[0], pos_lerp[1] - BOAT_HEIGHT / 2.0F,
				   pos_lerp[2]);
	entity_shadow(e, &bbox, view);
#else
	(void)e;
	(void)view;
	(void)tick_delta;
#endif
}

void entity_boat(uint32_t id, struct entity* e, bool server, void* world) {
	assert(e && world);

	e->id = id;
	e->tick_server = entity_boat_server_tick;
	e->tick_client = entity_boat_client_tick;
	e->render = entity_boat_render;
	e->teleport = entity_default_teleport;
	e->type = ENTITY_BOAT;
	e->data.boat.yaw = 0.0F;
	e->data.boat.passenger_id = 0;
	e->data.boat.in_water = false;
	e->data.boat.control_forward = 0;
	e->data.boat.control_turn = 0;

	entity_default_init(e, server, world);
}
