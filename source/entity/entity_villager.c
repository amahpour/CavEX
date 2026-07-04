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

// Passive villager mob (Epic #28, sub-epic PR #1): the first real mob in the
// codebase (boat/minecart are vehicles, item is a drop). There is no mob/AI
// subsystem and none is being built here -- a villager is just a new entity type
// whose server tick does idle + slow bounded wander + gravity + per-axis AABB
// ground collision itself, exactly like the minecart's off-rail branch. No AI,
// pathfinding, targeting, trading, health/damage, or reacting to the player.
// This file ships NO client render body (a GL-guarded compile-only stub, filled
// in by PR #2) and NO spawner (PR #3) -- nothing appears in-game yet.

#include <assert.h>
#include <math.h>

#include "../block/blocks_data.h"
#include "../network/client_interface.h"
#include "../network/server_local.h"
#include "entity.h"
// NB: do NOT include platform/gfx.h here -- it pulls <GL/glew.h> (PC), absent in
// the headless CI test image, and this entity is compiled into the unit-test
// library. The render only calls entity_shadow (declared in entity.h) once the
// render body lands -- no direct gfx_* calls.

// Pure wander math -- see declaration in entity.h. No engine state, unit-testable.
void entity_villager_wander(float* yaw, vec3 vel, int turn, int forward,
							float speed) {
	assert(yaw && vel);
	*yaw += (float)turn * 0.15F; // small heading nudge per decision
	vel[0] = (float)forward * speed * sinf(*yaw);
	vel[2] = (float)forward * speed * cosf(*yaw);
	// vel[1] intentionally untouched (gravity owns vertical)
}

static bool entity_villager_client_tick(struct entity* e) {
	entity_default_client_tick(e);
	float dx = e->pos[0] - e->pos_old[0];
	float dz = e->pos[2] - e->pos_old[2];
	if(dx * dx + dz * dz > 1.0e-4F)
		e->data.villager.yaw = atan2f(dx, dz);
	return false;
}

static bool entity_villager_server_tick(struct entity* e,
										struct server_local* s) {
	assert(e && s);
	glm_vec3_copy(e->pos, e->pos_old);
	glm_vec2_copy(e->orient, e->orient_old);

	// (1) Wander decision timer: on expiry, either pause (forward 0) or pick a
	//     fresh heading and step forward a little; bounded to home.
	if(--e->data.villager.idle_timer <= 0) {
		int forward = (rand_gen_flt(&s->rand_src) < 0.5F) ? 0 : 1;
		int turn = rand_gen_range(&s->rand_src, -1, 2); // -1, 0, or +1

		float dx = e->pos[0] - e->data.villager.home[0];
		float dz = e->pos[2] - e->data.villager.home[2];
		if(dx * dx + dz * dz
		   > VILLAGER_WANDER_RANGE * VILLAGER_WANDER_RANGE) {
			// Beyond range: head back toward home instead of outward.
			e->data.villager.yaw = atan2f(-dx, -dz);
			turn = 0;
			forward = 1;
		}

		float yaw = e->data.villager.yaw;
		entity_villager_wander(&yaw, e->vel, turn, forward,
							   VILLAGER_WANDER_SPEED);
		e->data.villager.yaw = yaw;

		e->data.villager.idle_timer = rand_gen_range(
			&s->rand_src, VILLAGER_IDLE_MIN, VILLAGER_IDLE_MAX + 1);
	}

	// (2) Gravity + horizontal ground friction (so it doesn't slide forever).
	e->vel[1] -= VILLAGER_GRAVITY;
	e->vel[1] *= 0.98F;
	e->vel[0] *= (e->on_ground ? 0.6F : 1.0F) * 0.98F;
	e->vel[2] *= (e->on_ground ? 0.6F : 1.0F) * 0.98F;

	for(int k = 0; k < 3; k++)
		if(fabsf(e->vel[k]) < 0.005F)
			e->vel[k] = 0.0F;

	// (3) Per-axis sweep with the villager AABB (y, then x, then z).
	struct AABB bbox;
	aabb_setsize_centered(&bbox, VILLAGER_WIDTH, VILLAGER_HEIGHT,
						  VILLAGER_LENGTH);
	bool collision_xz = false;
	for(int k = 0; k < 3; k++)
		entity_try_move(e, e->pos, e->vel, &bbox, (size_t[]) {1, 0, 2}[k],
						&collision_xz, &e->on_ground);

	// (4) Face travel heading.
	if(fabsf(e->vel[0]) > 1.0e-4F || fabsf(e->vel[2]) > 1.0e-4F)
		e->data.villager.yaw = atan2f(e->vel[0], e->vel[2]);
	e->orient[0] = e->data.villager.yaw;

	return false; // a passive villager NEVER auto-destroys
}

static void entity_villager_render(struct entity* e, mat4 view,
								   float tick_delta) {
#ifndef CAVEX_TEST_BUILD
	/* Render body is added in the villager rendering issue (#2 of the
	   sub-epic). Left as a no-op stub here so the file links into the Wii and
	   PC builds. */
	(void)e;
	(void)view;
	(void)tick_delta;
#else
	(void)e;
	(void)view;
	(void)tick_delta;
#endif
}

void entity_villager(uint32_t id, struct entity* e, bool server, void* world) {
	assert(e && world);
	e->id = id;
	e->tick_server = entity_villager_server_tick;
	e->tick_client = entity_villager_client_tick;
	e->render = entity_villager_render;
	e->teleport = entity_default_teleport;
	e->type = ENTITY_VILLAGER;
	e->data.villager.yaw = 0.0F;
	e->data.villager.idle_timer = VILLAGER_IDLE_MIN;
	e->data.villager.wander_ticks = 0;
	glm_vec3_zero(e->data.villager.home);
	entity_default_init(e, server, world);
}
