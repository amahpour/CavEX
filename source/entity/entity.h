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

#ifndef ENTITY_H
#define ENTITY_H

#include <m-lib/m-dict.h>
#include <stdbool.h>

#include "../cglm/cglm.h"
#include "../item/items.h"

enum entity_type {
	ENTITY_LOCAL_PLAYER,
	ENTITY_ITEM,
	ENTITY_BOAT,
	ENTITY_MINECART,
};

struct server_local;

struct entity {
	uint32_t id;
	bool on_server;
	void* world;
	int delay_destroy;

	vec3 pos;
	vec3 pos_old;
	vec3 vel;
	vec2 orient;
	vec2 orient_old;
	bool on_ground;

	vec3 network_pos;

	bool (*tick_client)(struct entity*);
	bool (*tick_server)(struct entity*, struct server_local*);
	void (*render)(struct entity*, mat4, float);
	void (*teleport)(struct entity*, vec3);

	enum entity_type type;
	union entity_data {
		struct entity_local_player {
			int jump_ticks;
			bool capture_input;
			bool flying;
			// Client mirror of the server's creative flag (HUD + dig-timer
			// gate only; the server stays authoritative). Set by CRPC_GAMEMODE.
			bool creative;
			int jump_tap_window;
			bool jump_held_prev;
			// Entity id of the vehicle currently being ridden (0 = on foot).
			// Set client-side when the player boards a boat OR minecart; while
			// non-zero the player tick steers that vehicle and rides along
			// instead of walking. (Named for the boat, which came first; it now
			// holds either vehicle's id.)
			uint32_t riding_boat_id;
		} local_player;
		struct entity_item {
			struct item_data item;
			int age;
		} item;
		struct entity_boat {
			float yaw;			   // heading in radians (server-authoritative)
			uint32_t passenger_id; // non-zero while a player is aboard
			bool in_water;		   // set by the server tick (buoyancy state)
			int control_forward;   // last steer intent -1/0/+1 (server only)
			int control_turn;	   // last turn  intent -1/0/+1 (server only)
			bool powered; // motor engaged this tick: cruise forward on its own
						  // (issue #33). Server-only; set from the rider's held
						  // motor item, cleared on dismount. Runtime state only.
		} boat;
	} data;
};

DICT_DEF2(dict_entity, uint32_t, M_BASIC_OPLIST, struct entity, M_POD_OPLIST)

#include "../world.h"

void entity_local_player(uint32_t id, struct entity* e, struct world* w);
bool entity_local_player_block_collide(vec3 pos, struct block_info* blk_info);

// Pure double-tap detector for the flight toggle. `pressed` is the just-pressed
// edge of the jump button; `window` holds the ticks remaining in the open
// double-tap window (decremented by the caller each tick). Returns true on the
// second press inside the window (i.e. a toggle should fire); the window is
// reset in either case. Pure (no engine state) so it is unit-testable.
// Length of the double-tap window in 20 Hz ticks (50 ms/tick): both jump taps
// must fall within this many ticks to toggle flight. 10 ticks ~= 0.5 s.
#define JUMP_TAP_WINDOW 10
bool detect_double_tap(bool pressed, int* window);

void entity_item(uint32_t id, struct entity* e, bool server, void* world,
				 struct item_data it);

// Rideable boat (issue #34). Same constructor shape as entity_item: wires the
// tick/render/teleport callbacks and tags the entity ENTITY_BOAT.
void entity_boat(uint32_t id, struct entity* e, bool server, void* world);

// Boat hull dimensions (blocks) and physics tuning. Shared between the server
// tick and the render so the collision box and drawn box agree.
#define BOAT_WIDTH 1.0F
#define BOAT_HEIGHT 0.5F
#define BOAT_LENGTH 2.0F
#define BOAT_TURN_SPEED 0.06F // yaw change (rad) per tick of turn input
#define BOAT_ACCEL 0.04F	  // forward thrust per tick along the heading
#define BOAT_DRAG 0.9F		  // fraction of horizontal speed kept per tick
#define BOAT_BUOYANCY 0.04F	  // upward push per tick while submerged
#define BOAT_GRAVITY 0.04F	  // downward pull per tick while airborne

// Motor (issue #33): when the motor is engaged the boat self-propels forward.
// MOTOR_THRUST is added along the heading each tick (independent of rider input,
// so it cruises hands-off); MOTOR_MAX_SPEED caps the resulting horizontal speed
// so a powered boat can never outrun chunk loading and skip terrain. The cap is
// chosen below the per-tick distance that would cross a chunk edge in one step.
#define MOTOR_THRUST 0.06F	  // forward accel per tick while powered
#define MOTOR_MAX_SPEED 0.35F // hard cap on horizontal speed (blocks/tick)

// Pure steering math: advance the heading by the turn input, add forward thrust
// along the new heading into the x/z velocity, then apply horizontal drag.
// forward/turn are each -1/0/+1. No engine state, so it is unit-testable.
void entity_boat_steer(float* yaw, vec3 vel, int forward, int turn);

// Rideable minecart that follows rails (issue #111). Same constructor shape as
// the boat and reuses the data.boat state (yaw, passenger_id, control_forward),
// but its server tick is rail-following Minecraft Beta 1.7.3 physics: on a rail
// it snaps to the track centre line and moves only along the track (straights,
// curves and slopes); powered rails accelerate/brake it; off-rail it just
// settles under gravity + strong friction. Tagged ENTITY_MINECART.
void entity_minecart(uint32_t id, struct entity* e, bool server, void* world);

// Minecart box dimensions (blocks) and physics tuning, shared by the server
// tick and the render so the collision box and drawn box agree.
#define MINECART_WIDTH 0.98F
#define MINECART_HEIGHT 0.7F
#define MINECART_LENGTH 0.98F
#define MINECART_GRAVITY 0.04F // downward pull per tick while airborne

// Rail-follow tuning (issue #111). MAX_SPEED is the hard horizontal cap chosen
// below the per-tick distance that would cross a chunk edge in one step, so a
// cart can never outrun chunk loading (no chunk-skip / teleport). ACCEL is the
// push a powered rail adds along the current direction each tick; a powered rail
// that is *unpowered* brakes by BRAKE (a fraction of speed kept per tick) and
// stops outright once slower than STOP. PUSH is the per-tick impulse a rider's
// forward/back intent adds along/against the track.
#define MINECART_MAX_SPEED 0.35F // hard cap on along-track speed (blocks/tick)
#define MINECART_ACCEL 0.04F	 // powered-rail push along the track per tick
#define MINECART_BRAKE 0.5F		 // speed fraction kept/tick on an unpowered rail
#define MINECART_STOP 0.02F		 // below this along-track speed the cart halts
#define MINECART_PUSH 0.02F		 // rider forward/back impulse along the track

// Pure rail math (issue #111) — no engine/world state, so unit-testable.
//
// minecart_rail_direction: given a rail metadata value (already masked to its
// shape bits, 0..9), write the two track-end offsets (relative to the rail
// block, in {-1,0,1}) into `end0`/`end1`. The Beta offset matrix is the source
// of truth for which way each shape connects; the cart moves between the ends.
// Returns false for an out-of-range shape (then the offsets are left untouched).
bool minecart_rail_ends(int shape, ivec3 end0, ivec3 end1);

// Horizontal unit direction of a rail shape, derived from its end offsets
// (end1 - end0, y dropped, normalised). Writes {dx,0,dz} into `dir_out` and
// returns true; returns false (dir_out untouched) for an out-of-range shape.
// For a straight/slope this is the track axis; for a curve it is the (diagonal)
// chord between the two ends -- use minecart_rail_travel_dir for the actual
// direction a moving cart takes through a shape.
bool minecart_rail_direction(int shape, vec3 dir_out);

// Direction a cart with horizontal velocity `vel_in` travels through a rail of
// the given shape, written as a horizontal unit vector into `dir_out`.
//   - Straights/slopes (0..5): the track axis, with its sense chosen to match
//     the cart's current motion (so it keeps going the way it travels); if the
//     cart is at rest the axis is returned in its default (+) sense.
//   - Curves (6..9): the two ends are perpendicular arms; the cart exits via the
//     arm its velocity points most toward (dot-product), i.e. it rounds the bend
//     onto the outgoing arm instead of driving straight off the diagonal. At
//     rest it defaults to end1's arm.
// Returns true on success; false (dir_out untouched) for an out-of-range shape.
bool minecart_rail_travel_dir(int shape, const vec3 vel_in, vec3 dir_out);

// Project the horizontal velocity onto the track unit direction `dir` (so the
// cart only moves along the rail, never across it) and write the result back
// into `vel` x/z. `dir` must be horizontal+normalised (minecart_rail_direction).
// The vertical component vel[1] is left untouched. Sign of the projection is
// preserved so the cart keeps its travel direction along the track.
void minecart_project_velocity(vec3 vel, const vec3 dir);

// Clamp the horizontal speed of `vel` to `cap` (scale x/z together so the
// heading is preserved); vertical is untouched. No-op when already at/under cap.
void minecart_speed_cap(vec3 vel, float cap);

// Pure motor math: while `powered`, add MOTOR_THRUST along the heading into the
// x/z velocity, then clamp the horizontal speed to MOTOR_MAX_SPEED so a cruising
// boat can never chunk-skip. When not powered it is a no-op (the boat coasts on
// the steering drag from entity_boat_steer). No engine state, so unit-testable.
void entity_boat_throttle(float yaw, vec3 vel, bool powered);

uint32_t entity_gen_id(dict_entity_t dict);

// Per-entity callback for entity_tick_all(). Invoked once for every entity in
// the dict during a safe walk. Return true to mark the entity for removal; it
// is erased only after the walk has finished. The callback may freely read and
// mutate `e` and emit RPCs, but must NOT insert into or erase from `dict`.
typedef bool (*entity_tick_fn)(uint32_t key, struct entity* e, void* ctx);

// Tick every entity through `cb`, then erase the ones it marked for removal.
// Two-pass (collect-then-erase): no entity is erased while the dict iterator is
// still live. CavEX's dict is an open-addressing hash map that may resize and
// relocate its buckets on erase, which would invalidate a live iterator and
// trip M*LIB's `index != NULL` contract on the next access (issue #69).
// Returns the number of entities removed.
size_t entity_tick_all(dict_entity_t dict, entity_tick_fn cb, void* ctx);

void entities_client_tick(dict_entity_t dict);
void entities_client_render(dict_entity_t dict, struct camera* c,
							float tick_delta);

void entity_default_init(struct entity* e, bool server, void* world);
void entity_default_teleport(struct entity* e, vec3 pos);
bool entity_default_client_tick(struct entity* e);

void entity_shadow(struct entity* e, struct AABB* a, mat4 view);

bool entity_get_block(struct entity* e, w_coord_t x, w_coord_t y, w_coord_t z,
					  struct block_data* blk);
bool entity_intersection_threshold(struct entity* e, struct AABB* aabb,
								   vec3 old_pos, vec3 new_pos,
								   float* threshold);
bool entity_intersection(struct entity* e, struct AABB* a,
						 bool (*test)(struct AABB* entity,
									  struct block_info* blk_info));
bool entity_block_aabb_test(struct AABB* entity, struct block_info* blk_info);
bool entity_aabb_intersection(struct entity* e, struct AABB* a);
void entity_try_move(struct entity* e, vec3 pos, vec3 vel, struct AABB* bbox,
					 size_t coord, bool* collision_xz, bool* on_ground);

#endif