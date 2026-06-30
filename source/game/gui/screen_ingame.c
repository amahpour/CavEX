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

#include <stddef.h>

#include "../../block/blocks.h"
#include "../../graphics/gfx_util.h"
#include "../../graphics/gui_util.h"
#include "../../graphics/render_model.h"
#include "../../network/server_interface.h"
#include "../../particle.h"
#include "../../platform/gfx.h"
#include "../../platform/input.h"
#include "../game_state.h"

#include <malloc.h>

static void screen_ingame_reset(struct screen* s, int width, int height) {
	input_pointer_enable(false);

	if(gstate.local_player)
		gstate.local_player->data.local_player.capture_input = true;
	// Split-screen: player 2 captures input too (its own device).
	if(gstate.local_player2)
		gstate.local_player2->data.local_player.capture_input = true;
}

void screen_ingame_render3D(struct screen* s, mat4 view) {
	if(gstate.world_loaded && gstate.camera_hit.hit) {
		struct block_data blk
			= world_get_block(&gstate.world, gstate.camera_hit.x,
							  gstate.camera_hit.y, gstate.camera_hit.z);

		if(gstate.digging.active)
			render_block_cracks(&blk, view, gstate.camera_hit.x,
								gstate.camera_hit.y, gstate.camera_hit.z);

		gfx_blending(MODE_BLEND);
		gfx_alpha_test(false);

		gutil_block_selection(view,
							  &(struct block_info) {
								  .block = &blk,
								  .x = gstate.camera_hit.x,
								  .y = gstate.camera_hit.y,
								  .z = gstate.camera_hit.z,
							  });

		gfx_blending(MODE_OFF);
		gfx_alpha_test(true);
	}

	float place_lerp = 0.0F;
	size_t slot = inventory_get_hotbar(
		windowc_get_latest(gstate.windows[WINDOWC_INVENTORY]));

	float dig_lerp
		= time_diff_s(gstate.held_item_animation.punch.start, time_get())
		/ 0.4F;

	if(gstate.held_item_animation.punch.place)
		place_lerp = 1.0F - glm_clamp(dig_lerp * 4.0F, 0, 1);

	if(dig_lerp >= 1.0F)
		dig_lerp = 0.0F;

	float swing_lerp
		= time_diff_s(gstate.held_item_animation.switch_item.start, time_get())
		/ 0.3F;

	if(swing_lerp < 0.5F)
		slot = gstate.held_item_animation.switch_item.old_slot;

	if(swing_lerp >= 1.0F)
		swing_lerp = 0.0F;

	float sinHalfCircle = sinf(dig_lerp * GLM_PI);
	float sqrtLerpPI = sqrtf(dig_lerp) * GLM_PI;
	float sinHalfCircleWeird = sinf(glm_pow2(dig_lerp) * GLM_PI);

	struct block_data in_block
		= world_get_block(&gstate.world, floorf(gstate.camera.x),
						  floorf(gstate.camera.y), floorf(gstate.camera.z));
	uint8_t light = (in_block.torch_light << 4) | in_block.sky_light;

	gfx_depth_range(0.0F, 0.1F);

	mat4 model;
	struct item_data item;

	if(inventory_get_slot(windowc_get_latest(gstate.windows[WINDOWC_INVENTORY]),
						  slot + INVENTORY_SLOT_HOTBAR, &item)
	   && item_get(&item)) {
		glm_translate_make(model,
						   (vec3) {0.56F - sinf(sqrtLerpPI) * 0.4F,
								   -0.52F + sinf(sqrtLerpPI * 2.0F) * 0.2F
									   - 0.6F * place_lerp
									   - 0.4F * sinf(swing_lerp * GLM_PI),
								   -0.72F - sinHalfCircle * 0.2F});
		glm_rotate_y(model, glm_rad(45.0F), model);
		glm_rotate_y(model, glm_rad(-sinHalfCircleWeird * 20.0F), model);
		glm_rotate_z(model, glm_rad(-sinf(sqrtLerpPI) * 20.0F), model);
		glm_rotate_x(model, glm_rad(-sinf(sqrtLerpPI) * 80.0F), model);

		glm_scale_uni(model, 0.4F);
		glm_translate(model, (vec3) {-0.5F, -0.5F, -0.5F});
		render_item_update_light(light);
		items[item.id]->renderItem(item_get(&item), &item, model, false,
								   R_ITEM_ENV_FIRSTPERSON);
	} else {
		glm_translate_make(model,
						   (vec3) {0.64F - sinf(sqrtLerpPI) * 0.3F,
								   -0.6F + sinf(sqrtLerpPI * 2.0F) * 0.4F
									   - 0.4F * sinf(swing_lerp * GLM_PI),
								   -0.72F - sinHalfCircle * 0.4F});
		glm_rotate_y(model, glm_rad(45.0F), model);
		glm_rotate_y(model, glm_rad(sinf(sqrtLerpPI) * 70.0F), model);
		glm_rotate_z(model, glm_rad(-sinHalfCircleWeird * 20.0F), model);

		gfx_lighting(false);
		gfx_bind_texture(&texture_mob_char);

		glm_translate(model, (vec3) {-1.0F, 3.6F, 3.5F});
		glm_rotate_z(model, glm_rad(120.0F), model);
		glm_rotate_x(model, glm_rad(200.0F), model);
		glm_rotate_y(model, glm_rad(-135.0F), model);
		glm_translate(model, (vec3) {5.6F, 0.0F, 0.0F});

		glm_translate(model,
					  (vec3) {-5.0F / 16.0F, 2.0F / 16.0F, 0.0F / 16.0F});
		glm_scale_uni(model, 1.0F / 16.0F);
		glm_translate(model, (vec3) {-3.0F, -2.0F, -2.0F});

		// TODO: depth fix in inventory
		render_model_box(model, (vec3) {2, 12, 2}, (vec3) {2, 0, 2},
						 (vec3) {180.0F, 0, 0}, (ivec2) {44, 20},
						 (ivec3) {4, 4, 12}, 0.0F, false,
						 gfx_lookup_light(light));
	}

	gfx_depth_range(0.0F, 1.0F);
}

static void screen_ingame_update(struct screen* s, float dt) {
	// While riding a boat the player steers instead of interacting with the
	// world (issue #34): suppress block place/dig so a board/steer tap never
	// also places or mines a block. This is also what keeps the board tap from
	// double-firing a place on the Wii (input_pressed there is not consumed
	// between the player tick and this handler). Crosshair picking still runs.
	bool riding = gstate.local_player
		&& gstate.local_player->data.local_player.riding_boat_id;

	if(riding)
		gstate.digging.active = false;

	if(!riding && gstate.camera_hit.hit && input_pressed(IB_ACTION2)
	   && !gstate.digging.active) {
		svin_rpc_send(&(struct server_rpc) {
			.type = SRPC_BLOCK_PLACE,
			.payload.block_place.x = gstate.camera_hit.x,
			.payload.block_place.y = gstate.camera_hit.y,
			.payload.block_place.z = gstate.camera_hit.z,
			.payload.block_place.side = gstate.camera_hit.side,
		});

		if(inventory_get_hotbar_item(
			   windowc_get_latest(gstate.windows[WINDOWC_INVENTORY]), NULL)) {
			gstate.held_item_animation.punch.start = time_get();
			gstate.held_item_animation.punch.place = true;
		}
	}

	// Client mirror of the creative flag (drives instant-break feel below).
	bool creative = gstate.local_player
		&& gstate.local_player->data.local_player.creative;

	if(gstate.digging.active) {
		struct block_data blk
			= world_get_block(&gstate.world, gstate.digging.x, gstate.digging.y,
							  gstate.digging.z);
		struct item_data it;
		inventory_get_hotbar_item(
			windowc_get_latest(gstate.windows[WINDOWC_INVENTORY]), &it);
		int delay = blocks[blk.type] ?
			tool_dig_delay_ms(blocks[blk.type], item_get(&it), creative) :
			0;

		if(!gstate.camera_hit.hit || gstate.digging.x != gstate.camera_hit.x
		   || gstate.digging.y != gstate.camera_hit.y
		   || gstate.digging.z != gstate.camera_hit.z) {
			gstate.digging.start = time_get();
			gstate.digging.x = gstate.camera_hit.x;
			gstate.digging.y = gstate.camera_hit.y;
			gstate.digging.z = gstate.camera_hit.z;

			svin_rpc_send(&(struct server_rpc) {
				.type = SRPC_BLOCK_DIG,
				.payload.block_dig.x = gstate.digging.x,
				.payload.block_dig.y = gstate.digging.y,
				.payload.block_dig.z = gstate.digging.z,
				.payload.block_dig.side = gstate.camera_hit.side,
				.payload.block_dig.finished = false,
			});
		}

		// Creative instant break: finish the dig immediately for breakable
		// blocks. Re-read the block at the (possibly just-updated) aimed cell so
		// the breakability check matches the cell we actually break — `delay`
		// above may have been computed for the previous cell during a crosshair
		// sweep. tool_dig_delay_ms returns -1 for unbreakable blocks (e.g.
		// bedrock), which must NOT break even in creative; 0 means instant.
		if(creative && gstate.camera_hit.hit) {
			struct block_data aimed
				= world_get_block(&gstate.world, gstate.digging.x,
								  gstate.digging.y, gstate.digging.z);
			int aimed_delay = blocks[aimed.type] ?
				tool_dig_delay_ms(blocks[aimed.type], item_get(&it), true) :
				-1;

			if(aimed_delay == 0) {
				svin_rpc_send(&(struct server_rpc) {
					.type = SRPC_BLOCK_DIG,
					.payload.block_dig.x = gstate.digging.x,
					.payload.block_dig.y = gstate.digging.y,
					.payload.block_dig.z = gstate.digging.z,
					.payload.block_dig.side = gstate.camera_hit.side,
					.payload.block_dig.finished = true,
				});

				gstate.digging.cooldown = time_get();
				gstate.digging.active = false;
			}
		}

		if(delay > 0
		   && time_diff_ms(gstate.digging.start, time_get()) >= delay) {
			svin_rpc_send(&(struct server_rpc) {
				.type = SRPC_BLOCK_DIG,
				.payload.block_dig.x = gstate.digging.x,
				.payload.block_dig.y = gstate.digging.y,
				.payload.block_dig.z = gstate.digging.z,
				.payload.block_dig.side = gstate.camera_hit.side,
				.payload.block_dig.finished = true,
			});

			gstate.digging.cooldown = time_get();
			gstate.digging.active = false;
		}

		if(input_released(IB_ACTION1))
			gstate.digging.active = false;
	} else {
		if(!riding && gstate.camera_hit.hit && input_held(IB_ACTION1)
		   && time_diff_ms(gstate.digging.cooldown, time_get()) >= 250) {
			gstate.digging.active = true;
			gstate.digging.start = time_get();
			gstate.digging.x = gstate.camera_hit.x;
			gstate.digging.y = gstate.camera_hit.y;
			gstate.digging.z = gstate.camera_hit.z;

			svin_rpc_send(&(struct server_rpc) {
				.type = SRPC_BLOCK_DIG,
				.payload.block_dig.x = gstate.digging.x,
				.payload.block_dig.y = gstate.digging.y,
				.payload.block_dig.z = gstate.digging.z,
				.payload.block_dig.side = gstate.camera_hit.side,
				.payload.block_dig.finished = false,
			});
		}
	}

	if(input_held(IB_ACTION1)
	   && time_diff_s(gstate.held_item_animation.punch.start, time_get())
		   >= 0.2F) {
		gstate.held_item_animation.punch.start = time_get();
		gstate.held_item_animation.punch.place = false;

		if(gstate.camera_hit.hit) {
			struct block_data blk
				= world_get_block(&gstate.world, gstate.camera_hit.x,
								  gstate.camera_hit.y, gstate.camera_hit.z);

			struct block_data neighbours[6];

			for(int k = 0; k < SIDE_MAX; k++) {
				int ox, oy, oz;
				blocks_side_offset((enum side)k, &ox, &oy, &oz);

				neighbours[k] = world_get_block(
					&gstate.world, gstate.camera_hit.x + ox,
					gstate.camera_hit.y + oy, gstate.camera_hit.z + oz);
			}

			particle_generate_side(
				&(struct block_info) {.block = &blk,
									  .neighbours = neighbours,
									  .x = gstate.camera_hit.x,
									  .y = gstate.camera_hit.y,
									  .z = gstate.camera_hit.z},
				gstate.camera_hit.side);
		}
	}

	size_t slot = inventory_get_hotbar(
		windowc_get_latest(gstate.windows[WINDOWC_INVENTORY]));
	bool old_item_exists = inventory_get_hotbar_item(
		windowc_get_latest(gstate.windows[WINDOWC_INVENTORY]), NULL);

	if(input_pressed(IB_SCROLL_LEFT)) {
		size_t next_slot = (slot == 0) ? INVENTORY_SIZE_HOTBAR - 1 : slot - 1;
		inventory_set_hotbar(
			windowc_get_latest(gstate.windows[WINDOWC_INVENTORY]), next_slot);
		bool new_item_exists = inventory_get_hotbar_item(
			windowc_get_latest(gstate.windows[WINDOWC_INVENTORY]), NULL);

		if(time_diff_s(gstate.held_item_animation.switch_item.start, time_get())
			   >= 0.15F
		   && (old_item_exists || new_item_exists)) {
			gstate.held_item_animation.switch_item.start = time_get();
			gstate.held_item_animation.switch_item.old_slot = slot;
		}

		if(gstate.digging.active)
			gstate.digging.start = time_get();

		svin_rpc_send(&(struct server_rpc) {
			.type = SRPC_HOTBAR_SLOT,
			.payload.hotbar_slot.slot = next_slot,
		});
	}

	if(input_pressed(IB_SCROLL_RIGHT)) {
		size_t next_slot = (slot == INVENTORY_SIZE_HOTBAR - 1) ? 0 : slot + 1;
		inventory_set_hotbar(
			windowc_get_latest(gstate.windows[WINDOWC_INVENTORY]), next_slot);
		bool new_item_exists = inventory_get_hotbar_item(
			windowc_get_latest(gstate.windows[WINDOWC_INVENTORY]), NULL);

		if(time_diff_s(gstate.held_item_animation.switch_item.start, time_get())
			   >= 0.15F
		   && (old_item_exists || new_item_exists)) {
			gstate.held_item_animation.switch_item.start = time_get();
			gstate.held_item_animation.switch_item.old_slot = slot;
		}

		if(gstate.digging.active)
			gstate.digging.start = time_get();

		svin_rpc_send(&(struct server_rpc) {
			.type = SRPC_HOTBAR_SLOT,
			.payload.hotbar_slot.slot = next_slot,
		});
	}

	if(input_pressed(IB_HOME)) {
		screen_set(&screen_select_world);
		svin_rpc_send(&(struct server_rpc) {
			.type = SRPC_UNLOAD_WORLD,
		});
	}

	// Creative toggle: ask the (authoritative) server to flip its flag; it
	// echoes the new state back via CRPC_GAMEMODE which updates the HUD/feel.
	if(input_pressed(IB_TOGGLE_CREATIVE))
		svin_rpc_send(&(struct server_rpc) {
			.type = SRPC_SET_GAMEMODE,
			.payload.set_gamemode.toggle = true,
		});

	if(input_pressed(IB_INVENTORY))
		// Both modes open the SAME survival inventory screen so creative is "the
		// same as when you hit the inventory button"; in creative that screen
		// adds a paged all-items grab strip above the GUI (screen_inventory.c).
		screen_set(&screen_inventory);

	if(input_pressed(IB_MAP))
		screen_set(&screen_map);
}

static void screen_ingame_render2D(struct screen* s, int width, int height) {
	char str[64];
	sprintf(str, GAME_NAME " Alpha %i.%i.%i (impl. B1.7.3)", VERSION_MAJOR,
			VERSION_MINOR, VERSION_PATCH);
	gutil_text(4, 4 + 17 * 0, str, 16, true);

	// Creative-flight indicator: visible whenever the local player is flying so
	// the double-tap-Jump toggle gives immediate, unambiguous feedback even when
	// hovering in place on the ground.
	if(gstate.local_player && gstate.local_player->data.local_player.flying)
		gutil_text(4, 4 + 17 * 4,
				   "FLYING  (hold Jump = up, Sneak = down, double-tap Jump = off)",
				   16, true);

	// Creative-mode indicator: shown only while the (server-authoritative)
	// creative flag is mirrored on. Survival worlds never display it.
	if(gstate.local_player && gstate.local_player->data.local_player.creative)
		gutil_text(4, 4 + 17 * 6, "CREATIVE", 16, true);

#ifdef PLATFORM_WII
	{
		// temporary diagnostics: show raw WPAD expansion data on screen
		extern void input_debug_wpad(char* dst, size_t len);
		input_debug_wpad(str, sizeof(str));
		gutil_text(4, 4 + 17 * 1, str, 16, true);

		// temporary diagnostics: mesher throughput + lighting queue depth
		extern volatile int cm_dbg_sent, cm_dbg_recv;
		extern volatile int dbg_cs_count, dbg_dirty_seen;
		struct mallinfo mi = mallinfo();
		sprintf(str, "mesh sent=%i recv=%i lq=%i dirty=%i memfree=%iK",
				cm_dbg_sent, cm_dbg_recv,
				(int)stack_size(&gstate.world.lighting_updates),
				dbg_dirty_seen, (int)(mi.fordblks / 1024));
		gutil_text(4, 4 + 17 * 2, str, 16, true);

		// temporary diagnostics: dig state machine internals
		struct block_data dblk
			= world_get_block(&gstate.world, gstate.digging.x,
							  gstate.digging.y, gstate.digging.z);
		struct item_data dit;
		inventory_get_hotbar_item(
			windowc_get_latest(gstate.windows[WINDOWC_INVENTORY]), &dit);
		bool dcreative = gstate.local_player
			&& gstate.local_player->data.local_player.creative;
		int ddelay = blocks[dblk.type] ?
			tool_dig_delay_ms(blocks[dblk.type], item_get(&dit), dcreative) :
			0;
		sprintf(str, "dig a=%i e=%i d=%i cd=%i hit=%i A=%i",
				gstate.digging.active,
				gstate.digging.active ?
					(int)time_diff_ms(gstate.digging.start, time_get()) :
					-1,
				ddelay, (int)time_diff_ms(gstate.digging.cooldown, time_get()),
				gstate.camera_hit.hit, input_held(IB_ACTION1));
		gutil_text(4, 4 + 17 * 3, str, 16, true);
	}
#endif

#ifndef NDEBUG
	sprintf(str, "%0.1f fps, wait: gpu %0.1fms, vsync %0.1fms",
			gstate.stats.fps, gstate.stats.dt_gpu * 1000.0F,
			gstate.stats.dt_vsync * 1000.0F);
	gutil_text(4, 4 + 17 * 1, str, 16, true);

	sprintf(str, "%zu chunks", gstate.stats.chunks_rendered);
	gutil_text(4, 4 + 17 * 2, str, 16, true);

	sprintf(str, "(%0.1f, %0.1f, %0.1f) (%0.1f, %0.1f)", gstate.camera.x,
			gstate.camera.y, gstate.camera.z, glm_deg(gstate.camera.rx),
			glm_deg(gstate.camera.ry));
	gutil_text(4, 4 + 17 * 3, str, 16, true);

	if(gstate.camera_hit.hit) {
		struct block_data bd
			= world_get_block(&gstate.world, gstate.camera_hit.x,
							  gstate.camera_hit.y, gstate.camera_hit.z);
		struct block* b = blocks[bd.type];
		sprintf(str, "side: %s, (%i, %i, %i), %s, (%i:%i)",
				block_side_name(gstate.camera_hit.side), gstate.camera_hit.x,
				gstate.camera_hit.y, gstate.camera_hit.z, b ? b->name : NULL,
				bd.type, bd.metadata);
		gutil_text(4, 4 + 17 * 5, str, 16, true);
	}
#endif

	int icon_offset = 32;
	icon_offset += gutil_control_icon(icon_offset, IB_INVENTORY, "Inventory");
	icon_offset += gutil_control_icon(icon_offset, IB_JUMP, "Jump");

	if(gstate.camera_hit.hit) {
		struct item_data item;
		struct block_data bd
			= world_get_block(&gstate.world, gstate.camera_hit.x,
							  gstate.camera_hit.y, gstate.camera_hit.z);
		if(blocks[bd.type] && blocks[bd.type]->onRightClick) {
			icon_offset += gutil_control_icon(icon_offset, IB_ACTION2, "Use");
		} else if(inventory_get_hotbar_item(
					  windowc_get_latest(gstate.windows[WINDOWC_INVENTORY]),
					  &item)
				  && item_get(&item)) {
			icon_offset
				+= gutil_control_icon(icon_offset, IB_ACTION2,
									  item_is_block(&item) ? "Place" : "Use");
		}

		icon_offset += gutil_control_icon(icon_offset, IB_ACTION1, "Mine");
	} else {
		icon_offset += gutil_control_icon(icon_offset, IB_ACTION1, "Punch");
	}

	icon_offset += gutil_control_icon(icon_offset, IB_HOME, "Save & quit");

	// draw hotbar
	gfx_bind_texture(&texture_gui2);
	gutil_texquad((width - 182 * 2) / 2, height - 32 * 8 / 5 - 22 * 2, 0, 0,
				  182, 22, 182 * 2, 22 * 2);

	gfx_blending(MODE_INVERT);
	gutil_texquad((width - 16 * 2) / 2, (height - 16 * 2) / 2, 0, 229, 16, 16,
				  16 * 2, 16 * 2);

	gfx_blending(MODE_OFF);

	for(int k = 0; k < INVENTORY_SIZE_HOTBAR; k++) {
		struct item_data item;
		if(inventory_get_slot(
			   windowc_get_latest(gstate.windows[WINDOWC_INVENTORY]),
			   k + INVENTORY_SLOT_HOTBAR, &item))
			gutil_draw_item(&item, (width - 182 * 2) / 2 + 3 * 2 + 20 * 2 * k,
							height - 32 * 8 / 5 - 19 * 2, 0);
	}

	gfx_blending(MODE_BLEND);
	gfx_bind_texture(&texture_gui2);

	// draw hotbar selection
	gutil_texquad((width - 182 * 2) / 2 - 2
					  + 20 * 2
						  * inventory_get_hotbar(windowc_get_latest(
							  gstate.windows[WINDOWC_INVENTORY])),
				  height - 32 * 8 / 5 - 23 * 2, 208, 0, 24, 24, 24 * 2, 24 * 2);

	// The hearts are purely cosmetic (CavEX has no health system). In creative
	// mode the player is conceptually invincible, so hide them to match
	// Minecraft and reinforce the creative feel — in lockstep with the
	// "CREATIVE" indicator above. Survival worlds draw them exactly as before.
	bool creative = gstate.local_player
		&& gstate.local_player->data.local_player.creative;
	if(!creative) {
		for(int k = 0; k < 10; k++) {
			// draw hearts
			gutil_texquad((width - 182 * 2) / 2 + k * 8 * 2,
						  height - 32 * 8 / 5 - (22 + 10) * 2, 16, 229, 9, 9,
						  9 * 2, 9 * 2);
			gutil_texquad((width - 182 * 2) / 2 + k * 8 * 2,
						  height - 32 * 8 / 5 - (22 + 10) * 2, 52, 229, 9, 9,
						  9 * 2, 9 * 2);
		}
	}
}

struct screen screen_ingame = {
	.reset = screen_ingame_reset,
	.update = screen_ingame_update,
	.render2D = screen_ingame_render2D,
	.render3D = screen_ingame_render3D,
	.render_world = true,
};
