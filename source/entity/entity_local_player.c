/*
	Copyright (c) 2023 ByteBit/xtreme8000

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

#include "../block/blocks_data.h"
#include "../game/game_state.h"
#include "../network/server_interface.h"
#include "../platform/input.h"
#include "entity.h"

#ifndef CAVEX_TEST_BUILD
// Render-only dependencies; the unit-test build (no GL) compiles this file too,
// so anything that pulls platform/gfx.h stays behind the guard, exactly like
// entity_villager.c.
#include "../graphics/render_item.h"
#include "../graphics/render_model.h"
#endif

#define EYE_HEIGHT 1.62F

// Boat riding (issue #34): how far above the hull centre the rider's eyes sit,
// and how close the player's feet must be to a boat centre to board it.
#define BOAT_RIDE_HEIGHT 1.0F
#define BOAT_MOUNT_REACH 2.5F

// Upward velocity imparted by a bubble column each tick. Small and clamped so
// the player rises steadily without being launched through the ceiling.
#define BUBBLE_COLUMN_PUSH 0.25F

// JUMP_TAP_WINDOW (the double-tap window, in 20 Hz ticks) is defined in entity.h
// so the unit test and the implementation share the exact value.

// Vertical speed (blocks/tick) used while flying.
#define FLY_SPEED 0.4F

// Pure double-tap detector — see declaration in entity.h. Keeps no engine state
// so it can be unit-tested in isolation.
bool detect_double_tap(bool pressed, int* window) {
	if(!pressed) {
		if(*window > 0)
			(*window)--;
		return false;
	}

	// fresh press
	if(*window > 0) {
		*window = 0;
		return true; // second tap within the window -> toggle
	}

	*window = JUMP_TAP_WINDOW; // first tap -> open the window
	return false;
}

// Pure walk-cycle step — see declaration in entity.h. Keeps no engine state so
// it can be unit-tested in isolation (tests/test_player_anim.c).
void player_walk_anim(float dist, float* phase, float* amp) {
	if(dist < 0.0F)
		dist = 0.0F;

	*phase += dist * PLAYER_WALK_PHASE_RATE;
	if(*phase >= GLM_PIf * 2.0F)
		*phase -= floorf(*phase / (GLM_PIf * 2.0F)) * GLM_PIf * 2.0F;

	float target = dist / PLAYER_WALK_REF_SPEED;
	if(target > 1.0F)
		target = 1.0F;
	target *= PLAYER_WALK_MAX_SWING;

	// ease so limbs ramp in/out instead of snapping between rest and full swing
	*amp += (target - *amp) * 0.3F;
	if(*amp < 0.01F)
		*amp = 0.0F;
}

static void liquid_aabb(struct AABB* out, struct block_info* blk_info) {
	int block_height = (blk_info->block->metadata & 0x8) ?
		16 :
		(8 - blk_info->block->metadata) * 2 * 7 / 8;
	aabb_setsize(out, 1.0F, (float)block_height / 16.0F, 1.0F);
	aabb_translate(out, blk_info->x, blk_info->y, blk_info->z);
}

static bool test_in_lava(struct AABB* entity, struct block_info* blk_info) {
	if(blk_info->block->type != BLOCK_LAVA_FLOW
	   && blk_info->block->type != BLOCK_LAVA_STILL)
		return false;

	struct AABB bbox;
	liquid_aabb(&bbox, blk_info);
	return aabb_intersection(entity, &bbox);
}

static bool test_in_water(struct AABB* entity, struct block_info* blk_info) {
	if(blk_info->block->type != BLOCK_WATER_FLOW
	   && blk_info->block->type != BLOCK_WATER_STILL)
		return false;

	struct AABB bbox;
	liquid_aabb(&bbox, blk_info);
	return aabb_intersection(entity, &bbox);
}

static bool test_in_liquid(struct AABB* entity, struct block_info* blk_info) {
	return test_in_water(entity, blk_info) || test_in_lava(entity, blk_info);
}

// Find a boat entity within boarding range of a feet position, or 0 if none.
// Client-side: scans the shared entity dict (only a handful of entities exist).
static uint32_t boat_in_reach(vec3 feet) {
	dict_entity_it_t it;
	dict_entity_it(it, gstate.entities);

	while(!dict_entity_end_p(it)) {
		struct entity* e = &dict_entity_ref(it)->value;
		if((e->type == ENTITY_BOAT || e->type == ENTITY_MINECART)
		   && glm_vec3_distance2(feet, e->pos) < glm_pow2(BOAT_MOUNT_REACH))
			return e->id;
		dict_entity_next(it);
	}

	return 0;
}

static bool entity_tick(struct entity* e) {
	assert(e);

	// Which local input device drives this player (0 = P1, 1 = split-screen P2).
	// All input queries in this tick go through the _dev variants so two local
	// players move and look independently from their own key-sets.
	int device = e->data.local_player.device;

	// --- Boat riding (issue #34) -----------------------------------------
	// While mounted the player runs no physics of its own: it forwards steer
	// input to the server-authoritative boat and rides along, snapping the
	// camera onto the hull. Use/sneak dismounts. Kept as one bounded block so
	// the rest of the player tick below is unchanged.
	if(e->data.local_player.riding_boat_id) {
		struct entity* boat
			= dict_entity_get(gstate.entities,
							  e->data.local_player.riding_boat_id);

		if(boat
		   && (boat->type == ENTITY_BOAT || boat->type == ENTITY_MINECART)) {
			int forward = 0, turn = 0;
			bool dismount = false;

			if(e->data.local_player.capture_input) {
				if(input_held_dev(IB_FORWARD, device))
					forward++;
				if(input_held_dev(IB_BACKWARD, device))
					forward--;
				if(input_held_dev(IB_RIGHT, device))
					turn++;
				if(input_held_dev(IB_LEFT, device))
					turn--;
				// Dismount on sneak only. Using the place/use button (IB_ACTION2)
				// would also fire a block place on the Wii, where input_pressed()
				// is level-per-poll and not consumed between the player tick and
				// the in-game screen's place handler in the same frame.
				dismount = input_pressed_dev(IB_SNEAK, device);
			}

			svin_rpc_send(&(struct server_rpc) {
				.type = SRPC_BOAT_CONTROL,
				.payload.boat_control.entity_id = boat->id,
				.payload.boat_control.forward = forward,
				.payload.boat_control.turn = turn,
				.payload.boat_control.dismount = dismount,
			});

			// ride along: eyes above the hull, interpolated like the boat
			glm_vec3_copy(boat->pos_old, e->pos_old);
			glm_vec3_copy(boat->pos, e->pos);
			e->pos_old[1] += BOAT_RIDE_HEIGHT;
			e->pos[1] += BOAT_RIDE_HEIGHT;

			// keep the flight double-tap from arming on the dismount tap
			e->data.local_player.jump_tap_window = 0;

			// riding: feet are still — let the walk swing ease back to rest
			player_walk_anim(0.0F, &e->data.local_player.walk_phase,
							 &e->data.local_player.walk_amp);

			if(dismount)
				e->data.local_player.riding_boat_id = 0;

			return false;
		}

		// boat vanished -> resume normal movement this tick
		e->data.local_player.riding_boat_id = 0;
	}

	// Boarding: if standing in/at a boat and pressing use, mount it (and skip
	// the place action this tick). input_pressed() is only sampled when a boat
	// is actually in reach, so ordinary block placing is unaffected otherwise.
	if(e->data.local_player.capture_input) {
		uint32_t boat = boat_in_reach(
			(vec3) {e->pos[0], e->pos[1] - EYE_HEIGHT, e->pos[2]});
		if(boat && input_pressed_dev(IB_ACTION2, device)) {
			e->data.local_player.riding_boat_id = boat;
			return false; // ride begins next tick
		}
	}

	glm_vec3_copy(e->pos, e->pos_old);
	glm_vec2_copy(e->orient, e->orient_old);

	for(int k = 0; k < 3; k++)
		if(fabsf(e->vel[k]) < 0.005F)
			e->vel[k] = 0.0F;

	struct AABB bbox;
	aabb_setsize_centered(&bbox, 0.6F, 1.0F, 0.6F);
	aabb_translate(&bbox, e->pos[0], e->pos[1] + 1.8F / 2.0F - EYE_HEIGHT,
				   e->pos[2]);

	bool in_water = entity_intersection(e, &bbox, test_in_water);
	bool in_lava = entity_intersection(e, &bbox, test_in_lava);

	float slipperiness
		= (in_lava || in_water) ? 1.0F : (e->on_ground ? 0.6F : 1.0F);

	int forward = 0;
	int strafe = 0;
	bool jumping = false;
	bool sneaking = false;

	if(e->data.local_player.capture_input) {
		if(input_held_dev(IB_FORWARD, device))
			forward++;

		if(input_held_dev(IB_BACKWARD, device))
			forward--;

		if(input_held_dev(IB_RIGHT, device))
			strafe++;

		if(input_held_dev(IB_LEFT, device))
			strafe--;

		jumping = input_held_dev(IB_JUMP, device);
		sneaking = input_held_dev(IB_SNEAK, device);

		// Rising edge of jump, sampled at the 20 Hz tick. We must NOT call
		// input_pressed(IB_JUMP) here: input_held(IB_JUMP) above already calls
		// input_native_key_status(), which on PC consumes the press edge as a
		// side effect (sets input_key_held), so input_pressed() would read
		// false on this tick and flight could never toggle. Derive the edge
		// from the jump level we already sampled instead.
		bool jump_edge = jumping && !e->data.local_player.jump_held_prev;
		e->data.local_player.jump_held_prev = jumping;

		// double-tap jump toggles creative flight (local-only, not persisted)
		if(detect_double_tap(jump_edge, &e->data.local_player.jump_tap_window))
			e->data.local_player.flying = !e->data.local_player.flying;
	} else {
		// keep the window from going stale while input is not captured
		e->data.local_player.jump_tap_window = 0;
		e->data.local_player.jump_held_prev = false;
	}

	bool flying = e->data.local_player.flying;

	int dist = forward * forward + strafe * strafe;

	if(dist > 0) {
		float distf = fmaxf(sqrtf(dist), 1.0F);
		float dx = (forward * sinf(e->orient[0]) - strafe * cosf(e->orient[0]))
			/ distf;
		float dy = (strafe * sinf(e->orient[0]) + forward * cosf(e->orient[0]))
			/ distf;

		e->vel[0] += 0.1F * powf(0.6F / slipperiness, 3.0F) * dx;
		e->vel[2] += 0.1F * powf(0.6F / slipperiness, 3.0F) * dy;
	}

	if(e->data.local_player.jump_ticks > 0)
		e->data.local_player.jump_ticks--;

	if(flying) {
		// direct vertical control: jump rises, sneak descends, idle holds
		if(jumping)
			e->vel[1] = FLY_SPEED;
		else if(sneaking)
			e->vel[1] = -FLY_SPEED;
		else
			e->vel[1] = 0.0F;
		e->data.local_player.jump_ticks = 0;
	} else if(jumping) {
		if(in_water || in_lava) {
			e->vel[1] += 0.04F;
		} else if(e->on_ground && e->data.local_player.jump_ticks == 0) {
			e->vel[1] = 0.42F;
			e->data.local_player.jump_ticks = 10;
		}
	} else {
		e->data.local_player.jump_ticks = 0;
	}

	aabb_setsize_centered(&bbox, 0.6F, 1.8F, 0.6F);
	aabb_translate(&bbox, 0.0F, 1.8F / 2.0F - EYE_HEIGHT, 0.0F);

	// unstuck player
	struct AABB tmp1 = bbox, tmp2 = bbox;
	float unstuck_move = 0.01F;
	aabb_translate(&tmp1, e->pos[0], e->pos[1], e->pos[2]);
	aabb_translate(&tmp2, e->pos[0], e->pos[1] + unstuck_move, e->pos[2]);

	// is the player stuck in the floor due to inaccuracy?
	if(entity_aabb_intersection(e, &tmp1)
	   && !entity_aabb_intersection(e, &tmp2)) {
		e->pos[1] += unstuck_move;
	}

	vec3 new_pos, new_vel;
	glm_vec3_copy(e->pos, new_pos);
	glm_vec3_copy(e->vel, new_vel);

	bool collision_xz = false;

	for(int k = 0; k < 3; k++)
		entity_try_move(e, e->pos, e->vel, &bbox, (size_t[]) {1, 0, 2}[k],
						&collision_xz, &e->on_ground);

	if(e->on_ground) {
		bool collision = false;
		bool ground = e->on_ground;

		new_vel[1] = 0.6F;
		entity_try_move(e, new_pos, new_vel, &bbox, 1, &collision, &ground);

		new_vel[1] = 0.0F;
		entity_try_move(e, new_pos, new_vel, &bbox, 0, &collision, &ground);
		entity_try_move(e, new_pos, new_vel, &bbox, 2, &collision, &ground);

		new_vel[1] = -0.6F;
		entity_try_move(e, new_pos, new_vel, &bbox, 1, &collision, &ground);

		if(new_pos[1] > e->pos_old[1]
		   && glm_vec3_distance2(e->pos_old, e->pos)
			   < glm_vec3_distance2(e->pos_old, new_pos)) {
			collision_xz = collision;
			e->on_ground = ground;
			glm_vec3_copy(new_pos, e->pos);
			glm_vec3_copy(new_vel, e->vel);
		}
	}

	if(flying) {
		// no gravity / buoyancy while flying; vel[1] is driven by jump/sneak
		// above. keep horizontal drag for a controllable feel.
		e->vel[0] *= slipperiness * 0.91F;
		e->vel[2] *= slipperiness * 0.91F;
	} else if(in_lava) {
		e->vel[0] *= 0.5F;
		e->vel[2] *= 0.5F;
		e->vel[1] = e->vel[1] * 0.5F - 0.02F;
	} else if(in_water) {
		e->vel[0] *= 0.8F;
		e->vel[2] *= 0.8F;
		e->vel[1] = e->vel[1] * 0.8F - 0.02F;
	} else {
		e->vel[0] *= slipperiness * 0.91F;
		e->vel[2] *= slipperiness * 0.91F;
		e->vel[1] -= 0.08F;

		struct block_data blk;
		if(entity_get_block(e, floorf(e->pos[0]),
							floorf(e->pos[1] - EYE_HEIGHT), floorf(e->pos[2]),
							&blk)
		   && blk.type == BLOCK_LADDER) {
			if(collision_xz)
				e->vel[1] = 0.12F;

			e->vel[0] = fmaxf(fminf(e->vel[0], 0.15F), -0.15F);
			e->vel[1] = fmaxf(e->vel[1], -0.15F);
			e->vel[2] = fmaxf(fminf(e->vel[2], 0.15F), -0.15F);
		} else if(entity_get_block(e, floorf(e->pos[0]),
								   floorf(e->pos[1] - EYE_HEIGHT),
								   floorf(e->pos[2]), &blk)
				  && blk.type == BLOCK_BUBBLE_COLUMN) {
			// Bubble elevator: push the player upward against gravity. The
			// gravity applied above (e->vel[1] -= 0.08F) is overwritten with a
			// small, clamped upward velocity so the player rises while inside
			// the column and falls normally once they leave it. Up-only MVP.
			e->vel[1] = BUBBLE_COLUMN_PUSH;
		}

		e->vel[1] *= 0.98F;
	}

	if(!flying && collision_xz && (in_lava || in_water)) {
		struct AABB tmp;
		aabb_setsize_centered(&tmp, 0.6F, 1.8F, 0.6F);
		aabb_translate(&tmp, e->pos[0] + e->vel[0],
					   e->pos[1] + e->vel[1] + 1.8F / 2.0F - 1.62F + 0.6F,
					   e->pos[2] + e->vel[2]);

		if(!entity_intersection(e, &tmp, test_in_liquid))
			e->vel[1] = 0.3F;
	}

	// advance the third-person walk cycle by this tick's horizontal movement
	{
		float mdx = e->pos[0] - e->pos_old[0];
		float mdz = e->pos[2] - e->pos_old[2];
		player_walk_anim(sqrtf(mdx * mdx + mdz * mdz),
						 &e->data.local_player.walk_phase,
						 &e->data.local_player.walk_amp);
	}

	return false;
}

bool entity_local_player_block_collide(vec3 pos, struct block_info* blk_info) {
	assert(pos && blk_info);

	struct AABB bbox;
	aabb_setsize_centered(&bbox, 0.6F, 1.8F, 0.6F);
	aabb_translate(&bbox, pos[0], 1.8F / 2.0F - EYE_HEIGHT + pos[1], pos[2]);

	return entity_block_aabb_test(&bbox, blk_info);
}

#ifndef CAVEX_TEST_BUILD
// The classic player model is authored in 1/16-block units, 32 px tall with its
// origin 28 px above the feet (render_model_player boxes: legs -28..-16, body
// -16..-4, head -4..+4). Minecraft draws it at 0.9375 scale so the 32 px model
// reads as the 1.8-block player; entity pos is the EYE (EYE_HEIGHT above feet).
#define PLAYER_MODEL_SCALE (0.9375F / 16.0F)
#define PLAYER_MODEL_ORIGIN_Y (28.0F * PLAYER_MODEL_SCALE - EYE_HEIGHT)

// Third-person player model (issue #138). Drawn for every local-player entity
// EXCEPT the one whose eyes the active viewport looks through — in the split-
// screen render pass mp_swap_active_view() has made that player the canonical
// gstate.local_player, so comparing ids draws only the OTHER player in each
// half. Single-player: the only player entity is always the active one, so
// nothing is ever drawn and rendering is unchanged.
static void entity_player_render(struct entity* e, mat4 view,
								 float tick_delta) {
	if(gstate.local_player && e->id == gstate.local_player_id)
		return;

	vec3 pos_lerp;
	glm_vec3_lerp(e->pos_old, e->pos, tick_delta, pos_lerp);

	// brightness from the block the head is in (same sampling as dropped
	// items). Floored so the other player never disappears into pure black —
	// an unlit cave (or a not-yet-loaded chunk, which reads as light 0) keeps
	// a faint silhouette you can still find.
	struct block_data in_block;
	entity_get_block(e, floorf(pos_lerp[0]), floorf(pos_lerp[1]),
					 floorf(pos_lerp[2]), &in_block);
	uint8_t light = (in_block.torch_light << 4) | in_block.sky_light;

	mat4 model;
	glm_translate_make(model,
					   (vec3) {pos_lerp[0], pos_lerp[1] + PLAYER_MODEL_ORIGIN_Y,
							   pos_lerp[2]});
	glm_rotate_y(model, e->orient[0], model);
	glm_scale_uni(model, PLAYER_MODEL_SCALE);

	mat4 mv;
	glm_mat4_mul(view, model, mv);

	// limbs swing with the walk cycle; arms counter-swing against the legs
	float swing = sinf(e->data.local_player.walk_phase)
		* e->data.local_player.walk_amp;
	// orient[1] is the camera polar angle (0 = up, pi/2 = level, pi = down);
	// the model's head pitch is degrees down from level.
	float head_pitch = glm_deg(e->orient[1] - GLM_PIf / 2.0F);

	struct inventory* inv
		= windowc_get_latest(gstate.windows[WINDOWC_INVENTORY]);

	struct item_data held, helmet, chestplate, leggings, boots;
	bool has_held = inv && inventory_get_hotbar_item(inv, &held);
	bool has_helmet
		= inv && inventory_get_slot(inv, INVENTORY_SLOT_ARMOR + 0, &helmet);
	bool has_chest
		= inv && inventory_get_slot(inv, INVENTORY_SLOT_ARMOR + 1, &chestplate);
	bool has_legs
		= inv && inventory_get_slot(inv, INVENTORY_SLOT_ARMOR + 2, &leggings);
	bool has_boots
		= inv && inventory_get_slot(inv, INVENTORY_SLOT_ARMOR + 3, &boots);

	float brightness = gfx_lookup_light(light);
	if(brightness < 0.08F)
		brightness = 0.08F;

	gfx_lighting(false);
	render_item_update_light(light);
	render_model_player(mv, head_pitch, 0.0F, swing, -swing * 0.9F,
						has_held ? &held : NULL, has_helmet ? &helmet : NULL,
						has_chest ? &chestplate : NULL,
						has_legs ? &leggings : NULL, has_boots ? &boots : NULL,
						brightness);
	gfx_lighting(true);

	// standard entity blob shadow at the feet
	struct AABB bbox;
	aabb_setsize_centered(&bbox, 0.6F, 0.1F, 0.6F);
	aabb_translate(&bbox, pos_lerp[0], pos_lerp[1] - EYE_HEIGHT + 0.05F,
				   pos_lerp[2]);
	entity_shadow(e, &bbox, view);
}
#endif

void entity_local_player(uint32_t id, struct entity* e, struct world* w) {
	assert(e && w);

	e->id = id;
	e->tick_server = NULL;
	e->tick_client = entity_tick;
#ifndef CAVEX_TEST_BUILD
	// Visible to the OTHER split-screen player; the active viewport's own
	// entity is skipped inside the render (first person). See issue #138.
	e->render = entity_player_render;
#else
	e->render = NULL;
#endif
	e->teleport = entity_default_teleport;
	e->type = ENTITY_LOCAL_PLAYER;
	e->data.local_player.capture_input = false;
	e->data.local_player.device = 0; // player 1 by default; P2 set to 1 by caller

	entity_default_init(e, false, w);
	e->data.local_player.jump_ticks = 0;
	e->data.local_player.flying = false;
	e->data.local_player.creative = false; // updated by CRPC_GAMEMODE on load
	e->data.local_player.jump_tap_window = 0;
	e->data.local_player.jump_held_prev = false;
	e->data.local_player.walk_phase = 0.0F;
	e->data.local_player.walk_amp = 0.0F;
}
