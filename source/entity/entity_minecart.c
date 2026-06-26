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

// Rideable minecart that follows rails (issue #111). Replaces the boat-style
// free-rolling tick with a port of the Minecraft Beta 1.7.3 EntityMinecart
// on-rail algorithm: the cart snaps to the rail centre line and only moves along
// the track (straights, curves and ascending/descending slopes); powered rails
// accelerate it while powered and brake it while unpowered; a rider's forward
// intent adds a small push along the track. Off any rail the cart just settles
// in place under gravity + strong horizontal friction ("only works on rails").
// Registration, the client tick (derives heading from motion), the render and
// boarding are unchanged from the boat-style cart. The render reuses the
// iron-block renderer (no new art) scaled to the cart box.

#include <assert.h>
#include <math.h>

#include "../block/blocks_data.h"
#include "../network/client_interface.h"
#include "../network/server_local.h"
#include "entity.h"
// NB: do NOT include platform/gfx.h here -- it pulls <GL/glew.h> (PC), absent in
// the headless CI test image, and this entity is compiled into the unit-test
// library. The render only calls item->renderItem (a function pointer) and
// entity_shadow (declared in entity.h) -- no direct gfx_* calls.

// Beta offset matrix: for each rail shape (metadata masked to 0..9) the two
// track-end offsets relative to the rail block, in {-1,0,1}. The cart runs
// between end0 and end1. Slopes (2..5) carry a -1 on the high/low end's y; the y
// rows are verified against render_block_rail (the drawn slope direction). Plain
// rails use all ten shapes; powered/detector rails mask to 0..5 (no curves).
static const int RAIL_ENDS[10][2][3] = {
	{{0, 0, -1}, {0, 0, 1}},   // 0  north-south (along z)
	{{-1, 0, 0}, {1, 0, 0}},   // 1  east-west   (along x)
	{{-1, -1, 0}, {1, 0, 0}},  // 2  ascend east
	{{-1, 0, 0}, {1, -1, 0}},  // 3  ascend west
	{{0, 0, -1}, {0, -1, 1}},  // 4  ascend south
	{{0, -1, -1}, {0, 0, 1}},  // 5  ascend north
	{{0, 0, 1}, {1, 0, 0}},	   // 6  curve south-east
	{{0, 0, 1}, {-1, 0, 0}},   // 7  curve south-west
	{{0, 0, -1}, {-1, 0, 0}},  // 8  curve north-west
	{{0, 0, -1}, {1, 0, 0}},   // 9  curve north-east
};

// --- pure rail math (declared in entity.h; no engine/world state) ---

bool minecart_rail_ends(int shape, ivec3 end0, ivec3 end1) {
	assert(end0 && end1);
	if(shape < 0 || shape > 9)
		return false;
	for(int k = 0; k < 3; k++) {
		end0[k] = RAIL_ENDS[shape][0][k];
		end1[k] = RAIL_ENDS[shape][1][k];
	}
	return true;
}

bool minecart_rail_direction(int shape, vec3 dir_out) {
	assert(dir_out);
	ivec3 e0, e1;
	if(!minecart_rail_ends(shape, e0, e1))
		return false;

	// Horizontal direction the track runs: (end1 - end0) with y dropped, then
	// normalised. Every shape has a non-zero horizontal extent, so the length is
	// always positive (no divide-by-zero guard needed, but assert it in debug).
	float dx = (float)(e1[0] - e0[0]);
	float dz = (float)(e1[2] - e0[2]);
	float len = sqrtf(dx * dx + dz * dz);
	assert(len > 0.0F);
	dir_out[0] = dx / len;
	dir_out[1] = 0.0F;
	dir_out[2] = dz / len;
	return true;
}

bool minecart_rail_travel_dir(int shape, const vec3 vel_in, vec3 dir_out) {
	assert(vel_in && dir_out);
	ivec3 e0, e1;
	if(!minecart_rail_ends(shape, e0, e1))
		return false;

	if(shape < 6) {
		// Straight or slope: travel along the track axis, sense chosen to match
		// the cart's current motion (default + when at rest).
		minecart_rail_direction(shape, dir_out); // shape in range -> succeeds
		float along = vel_in[0] * dir_out[0] + vel_in[2] * dir_out[2];
		if(along < 0.0F) {
			dir_out[0] = -dir_out[0];
			dir_out[2] = -dir_out[2];
		}
		return true;
	}

	// Curve: the two ends are perpendicular arms. Build each arm's horizontal
	// unit direction (from the block centre toward that end) and exit via the arm
	// the cart's velocity points most toward (largest dot). A cart entering along
	// one arm leaves along the other, rounding the bend (never driving off the
	// diagonal chord). At rest both dots are 0 and arm1 (e1) wins by the >= tie.
	float a0x = (float)e0[0], a0z = (float)e0[2];
	float a1x = (float)e1[0], a1z = (float)e1[2];
	float l0 = sqrtf(a0x * a0x + a0z * a0z);
	float l1 = sqrtf(a1x * a1x + a1z * a1z);
	assert(l0 > 0.0F && l1 > 0.0F);
	a0x /= l0;
	a0z /= l0;
	a1x /= l1;
	a1z /= l1;

	float d0 = vel_in[0] * a0x + vel_in[2] * a0z;
	float d1 = vel_in[0] * a1x + vel_in[2] * a1z;
	if(d0 > d1) {
		dir_out[0] = a0x;
		dir_out[1] = 0.0F;
		dir_out[2] = a0z;
	} else {
		dir_out[0] = a1x;
		dir_out[1] = 0.0F;
		dir_out[2] = a1z;
	}
	return true;
}

void minecart_project_velocity(vec3 vel, const vec3 dir) {
	assert(vel && dir);
	// Scalar projection of the horizontal velocity onto the (unit) track
	// direction, then re-expand along that direction: kills any across-track
	// component so the cart stays on the rail, keeps the along-track sign.
	float proj = vel[0] * dir[0] + vel[2] * dir[2];
	vel[0] = proj * dir[0];
	vel[2] = proj * dir[2];
}

void minecart_speed_cap(vec3 vel, float cap) {
	assert(vel);
	float speed = sqrtf(vel[0] * vel[0] + vel[2] * vel[2]);
	if(speed > cap) {
		float scale = cap / speed; // speed > cap >= 0 implies speed > 0
		vel[0] *= scale;
		vel[2] *= scale;
	}
}

// True for the three rail block types; writes the block + its masked shape.
static bool minecart_is_rail(uint8_t type) {
	return type == BLOCK_RAIL || type == BLOCK_POWERED_RAIL
		|| type == BLOCK_DETECTOR_RAIL;
}

// Look for a rail at the cart's block, or one block below (the foot of a slope,
// where the cart sits a step lower than the rail it is climbing). On success
// writes the rail world-y, the masked shape (0..9 plain, 0..5 powered/detector)
// and whether it is a powered rail that is currently powered. Plain rails may be
// curved (shape 6..9); powered/detector mask to 0..5 so a stray high nibble can
// never select a curve on them.
static bool minecart_find_rail(struct entity* e, w_coord_t bx, w_coord_t by,
							   w_coord_t bz, w_coord_t* rail_y, int* shape,
							   bool* powered_accel, bool* powered_brake) {
	struct block_data blk;
	for(int dy = 0; dy >= -1; dy--) {
		if(!entity_get_block(e, bx, by + dy, bz, &blk))
			continue;
		if(!minecart_is_rail(blk.type))
			continue;

		bool is_powered_rail = blk.type == BLOCK_POWERED_RAIL;
		int meta = blk.metadata;
		int s = (blk.type == BLOCK_RAIL) ? (meta & 0xF) : (meta & 0x7);
		if(s > 9 || (blk.type != BLOCK_RAIL && s > 5))
			s = 0; // defensive: never index past the matrix / pick a bad shape

		*rail_y = by + dy;
		*shape = s;
		*powered_accel = is_powered_rail && (meta & 0x8);
		*powered_brake = is_powered_rail && !(meta & 0x8);
		return true;
	}
	return false;
}

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

	int forward = e->data.boat.control_forward;
	// consume the rider control so the cart coasts if input stops
	e->data.boat.control_forward = 0;
	e->data.boat.control_turn = 0;

	w_coord_t bx = (w_coord_t)floorf(e->pos[0]);
	w_coord_t by = (w_coord_t)floorf(e->pos[1]);
	w_coord_t bz = (w_coord_t)floorf(e->pos[2]);

	w_coord_t rail_y;
	int shape;
	bool powered_accel, powered_brake;
	bool on_rail = minecart_find_rail(e, bx, by, bz, &rail_y, &shape,
									  &powered_accel, &powered_brake);

	if(!on_rail) {
		// Off any rail: the cart "only works on rails" -- shed horizontal speed
		// fast and fall under gravity so it just settles in place where it left
		// the track (it does NOT free-roll across the ground like the boat).
		e->vel[0] *= 0.5F;
		e->vel[2] *= 0.5F;
		e->vel[1] -= MINECART_GRAVITY;
		e->vel[1] *= 0.95F;

		for(int k = 0; k < 3; k++)
			if(fabsf(e->vel[k]) < 0.005F)
				e->vel[k] = 0.0F;

		struct AABB bbox;
		aabb_setsize_centered(&bbox, MINECART_WIDTH, MINECART_HEIGHT,
							  MINECART_LENGTH);
		bool collision_xz = false;
		for(int k = 0; k < 3; k++)
			entity_try_move(e, e->pos, e->vel, &bbox, (size_t[]) {1, 0, 2}[k],
							&collision_xz, &e->on_ground);
		return false;
	}

	// On a rail: pick the direction the cart travels through this shape. For a
	// straight/slope this is the track axis in the cart's current sense; for a
	// curve the cart exits via the arm it is heading toward (rounds the bend).
	vec3 dir;
	minecart_rail_travel_dir(shape, e->vel, dir); // shape in range -> succeeds
	bool is_curve = shape >= 6;

	if(is_curve) {
		// Curve: redirect the cart's horizontal SPEED onto the outgoing arm. A
		// 90-degree bend is perpendicular to the incoming velocity, so projecting
		// onto it would zero the speed and stall the cart at the corner -- instead
		// carry the speed through the turn by re-aiming it along the exit arm.
		float speed = sqrtf(e->vel[0] * e->vel[0] + e->vel[2] * e->vel[2]);
		e->vel[0] = speed * dir[0];
		e->vel[2] = speed * dir[2];
	} else {
		// Straight/slope: kill the across-track velocity component (project onto
		// the axis) so the cart only ever moves along the rail.
		minecart_project_velocity(e->vel, dir);
	}

	float cx = (float)bx + 0.5F;
	float cz = (float)bz + 0.5F;
	const float SNAP = 0.4F;
	if(is_curve) {
		// Curve: ease the cart toward the block centre on both axes so it tracks
		// smoothly around the corner onto the outgoing arm.
		e->pos[0] += (cx - e->pos[0]) * SNAP;
		e->pos[2] += (cz - e->pos[2]) * SNAP;
	} else {
		// Straight/slope: pin the off-axis coordinate to the centre line.
		if(dir[0] == 0.0F)
			e->pos[0] += (cx - e->pos[0]) * SNAP; // N-S track: hold x centred
		if(dir[2] == 0.0F)
			e->pos[2] += (cz - e->pos[2]) * SNAP; // E-W track: hold z centred
	}

	// Powered rail: accelerate along the current direction while powered; brake
	// hard (and stop when slow) while a powered rail is unpowered.
	if(powered_accel) {
		e->vel[0] += MINECART_ACCEL * dir[0];
		e->vel[2] += MINECART_ACCEL * dir[2];
	} else if(powered_brake) {
		e->vel[0] *= MINECART_BRAKE;
		e->vel[2] *= MINECART_BRAKE;
		float sp = sqrtf(e->vel[0] * e->vel[0] + e->vel[2] * e->vel[2]);
		if(sp < MINECART_STOP) {
			e->vel[0] = 0.0F;
			e->vel[2] = 0.0F;
		}
	}

	// Rider push: forward/back adds a small impulse along/against the track. From
	// rest (no along-track motion yet) this also establishes the travel sense.
	if(forward != 0) {
		e->vel[0] += (float)forward * MINECART_PUSH * dir[0];
		e->vel[2] += (float)forward * MINECART_PUSH * dir[2];
	}

	// Gentle rolling friction so an unpushed cart on a flat plain rail coasts to
	// a stop instead of gliding forever (matches the "settles at the track end"
	// acceptance criterion). Powered rails overcome it while powered.
	e->vel[0] *= 0.98F;
	e->vel[2] *= 0.98F;

	// Cap the along-track speed so the cart can never move far enough in one tick
	// to skip past unloaded chunks (no teleport / chunk-skip).
	minecart_speed_cap(e->vel, MINECART_MAX_SPEED);

	for(int k = 0; k < 3; k++)
		if(fabsf(e->vel[k]) < 0.001F)
			e->vel[k] = 0.0F;

	// Vertical: derive the target cart height from the rail. On a slope (shape
	// 2..5) the cart rides half a block higher per block travelled along the
	// climb; on the flat it sits just above the rail. Ease y toward the target
	// (a simple, stable approximation of the Beta slope handling).
	float target_y = (float)rail_y + MINECART_HEIGHT / 2.0F + 0.05F;
	if(shape >= 2 && shape <= 5) {
		// Slope: the high end is +1 block; bias the rest height up so the cart
		// visibly climbs/descends rather than clipping into the ascending block.
		target_y += 0.5F;
	}
	e->pos[1] += (target_y - e->pos[1]) * 0.5F;

	// Drive the cart along the track. Per-axis move with the cart AABB so it
	// still resolves against any solid block bordering the rail.
	struct AABB bbox;
	aabb_setsize_centered(&bbox, MINECART_WIDTH, MINECART_HEIGHT,
						  MINECART_LENGTH);
	bool collision_xz = false;
	for(int k = 0; k < 3; k++)
		entity_try_move(e, e->pos, e->vel, &bbox, (size_t[]) {1, 0, 2}[k],
						&collision_xz, &e->on_ground);

	// Face the render along the track motion so the cart points where it goes.
	if(fabsf(e->vel[0]) > 1.0e-4F || fabsf(e->vel[2]) > 1.0e-4F)
		e->data.boat.yaw = atan2f(e->vel[0], e->vel[2]);
	e->orient[0] = e->data.boat.yaw;

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
	e->data.boat.powered = false;

	entity_default_init(e, server, world);
}
