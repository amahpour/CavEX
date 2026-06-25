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

// Pure steering math: advance the heading by the turn input, add forward thrust
// along the new heading into the x/z velocity, then apply horizontal drag.
// forward/turn are each -1/0/+1. No engine state, so it is unit-testable.
void entity_boat_steer(float* yaw, vec3 vel, int forward, int turn);

// Rideable minecart (boat-style; issue chosen scope). Same constructor shape as
// the boat and reuses the boat steering + data.boat state, but rolls on land
// under gravity (no buoyancy) instead of floating. Tagged ENTITY_MINECART.
void entity_minecart(uint32_t id, struct entity* e, bool server, void* world);

// Minecart box dimensions (blocks) and physics tuning, shared by the server
// tick and the render so the collision box and drawn box agree.
#define MINECART_WIDTH 0.98F
#define MINECART_HEIGHT 0.7F
#define MINECART_LENGTH 0.98F
#define MINECART_GRAVITY 0.04F // downward pull per tick while airborne
#define MINECART_DRAG 0.95F	   // idle horizontal damping (carts coast further)

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