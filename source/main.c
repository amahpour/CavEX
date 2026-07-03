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
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef PLATFORM_WII
#include <fat.h>
#include <unistd.h>
#endif

#include "chunk_mesher.h"
#include "daytime.h"
#include "game/game_state.h"
#include "game/gui/screen.h"
#include "graphics/gfx_util.h"
#include "graphics/gui_util.h"
#include "item/recipe.h"
#include "network/client_interface.h"
#include "network/server_interface.h"
#include "network/server_local.h"
#include "particle.h"
#include "platform/gfx.h"
#include "platform/input.h"
#include "world.h"

#ifdef PLATFORM_PC
#include "game/state_export.h"
#include "platform/demo_input.h"
#endif

#include "cNBT/nbt.h"
#include "cglm/cglm.h"
#include "lodepng/lodepng.h"

#ifndef NDEBUG
// TRAP (origin-snap hunt): RPC rings defined in client_interface.c /
// server_local.c
struct trap_crpc_entry {
	int type;
	float a, b, c;
	unsigned seq;
};
extern struct trap_crpc_entry trap_crpc_ring[32];
extern unsigned trap_crpc_seq;
struct trap_srpc_entry {
	int type;
	float a, b, c;
	unsigned seq;
};
extern struct trap_srpc_entry trap_srpc_ring[32];
extern unsigned trap_srpc_seq;
#endif

// Local split-screen (issue #23): exchange player 1's and player 2's view state
// in the canonical gstate fields. Calling it makes the *2 player "active" so that
// every renderer/HUD that reads gstate.camera / camera_hit / digging /
// held_item_animation / local_player draws that player with no per-callsite
// changes; calling it again restores player 1. A swap (not a copy) means player
// 2's camera/aim updated during its pass are preserved back into the *2 fields.
// Declared in game_state.h so the in-game screen can run player 2's interaction.
void mp_swap_active_view(void) {
	struct camera tc = gstate.camera;
	gstate.camera = gstate.camera2;
	gstate.camera2 = tc;

	struct camera_ray_result th = gstate.camera_hit;
	gstate.camera_hit = gstate.camera_hit2;
	gstate.camera_hit2 = th;

	struct digging td = gstate.digging;
	gstate.digging = gstate.digging2;
	gstate.digging2 = td;

	struct held_anim ta = gstate.held_item_animation;
	gstate.held_item_animation = gstate.held_item_animation2;
	gstate.held_item_animation2 = ta;

	struct entity* tp = gstate.local_player;
	gstate.local_player = gstate.local_player2;
	gstate.local_player2 = tp;

	uint32_t ti = gstate.local_player_id;
	gstate.local_player_id = gstate.local_player2_id;
	gstate.local_player2_id = ti;
}

int main(void) {
	gstate.quit = false;
	gstate.camera = (struct camera) {
		.x = 0, .y = 0, .z = 0, .rx = 0, .ry = 0, .controller = {0, 0, 0}};
	gstate.config.fov = 70.0F;
	gstate.config.render_distance = 96.0F;
	gstate.config.fog_distance = 3 * 16.0F;
	gstate.world_loaded = false;
	gstate.held_item_animation.punch.start = time_get();
	gstate.held_item_animation.switch_item.start = time_get();
	gstate.digging.cooldown = time_get();
	gstate.digging.active = false;
	gstate.held_item_animation2 = gstate.held_item_animation;
	gstate.digging2 = gstate.digging;

	// Local split-screen co-op (issue #23): single-player by default. CAVEX_2P
	// (PC) enables a second local player -- vertical split, second camera, input
	// device 1 (the player2_* bindings).
	gstate.num_local_players = 1;
#ifdef PLATFORM_PC
	if(getenv("CAVEX_2P"))
		gstate.num_local_players = 2;
#endif

	rand_gen_seed(&gstate.rand_src);

#ifdef PLATFORM_WII
	fatInitDefault();
	// When booted directly in Dolphin (not via the Homebrew Channel) the working
	// directory isn't set to the SD root, so relative asset paths ("assets/...",
	// "saves/...") fail to open. Pin the cwd to the SD card so they resolve.
	chdir("sd:/");
#endif

	config_create(&gstate.config_user, "config.json");

	input_init();

#ifdef PLATFORM_PC
	// Virtual-input dev rig (dev only, env-gated). When no env var is set this
	// is a no-op and input/gameplay is byte-identical to a normal build.
	//   CAVEX_DEMO=<script>  -> deterministic file-replay source (#66)
	//   CAVEX_AGENT=1        -> live action source reading stdin (#67); a driver
	//                           reads the per-tick state line and writes actions.
	// The live agent source takes precedence if both are set (it is the
	// interactive path). Only one virtual source can be installed.
	{
		struct input_virtual_source* src = agent_input_create_from_env();
		if(!src)
			src = demo_input_create_from_env();
		if(src)
			input_set_virtual_source(src);

		// Split-screen (issue #23): player 2 can be scripted independently via
		// CAVEX_DEMO2 on input device 1, so the rig can demo full two-player
		// gameplay (both players acting) in one deterministic run.
		if(gstate.num_local_players >= 2) {
			struct input_virtual_source* src2
				= demo_input_create_from_env_dev(1);
			if(src2)
				input_set_virtual_source_dev(1, src2);
		}
	}
#endif

	blocks_init();
	items_init();
	recipe_init();
	gfx_setup();
	gutil_init();

	screen_set(&screen_select_world);

	world_create(&gstate.world);

	for(size_t k = 0; k < 256; k++)
		gstate.windows[k] = NULL;

	clin_init();
	svin_init();
	chunk_mesher_init();
	particle_init();

	dict_entity_init(gstate.entities);
	gstate.local_player = NULL;

	struct server_local server;
	server_local_create(&server);

	ptime_t last_frame = time_get();
	ptime_t last_tick = last_frame;

	while(!gstate.quit) {
		ptime_t this_frame = time_get();
		gstate.stats.dt = time_diff_s(last_frame, this_frame);
		gstate.stats.fps = 1.0F / gstate.stats.dt;
		last_frame = this_frame;

		float daytime
			= (float)((gstate.world_time
					   + time_diff_ms(gstate.world_time_start, this_frame)
						   / DAY_TICK_MS)
					  % DAY_LENGTH_TICKS)
			/ (float)DAY_LENGTH_TICKS;

#ifdef PLATFORM_PC
		// Demo-replay capture should be reproducible, so stop wall-clock values
		// from leaking into the rendered frame: hold the time-of-day fixed (sky
		// + lighting). The FPS/timing readout the debug overlay prints is zeroed
		// just before the 2D pass (gfx_flip_buffers rewrites dt_gpu/dt_vsync
		// later in the loop). No effect outside demo mode.
		bool demo_capture = input_get_virtual_source() != NULL;
		if(demo_capture)
			daytime = 0.5F; // midday, stable lighting
#endif

		clin_update();

		// Defensive: re-resolve the cached local-player pointer from its stable
		// id (the entity dict could mutate during clin_update()).
		if(gstate.local_player)
			gstate.local_player
				= dict_entity_get(gstate.entities, gstate.local_player_id);

#ifndef NDEBUG
		{
			// TEMP instrumentation (env-gated): heartbeat + scripted trunk dig
			static int tf = 0;
			tf++;
			if(getenv("CAVEX_TRACE") && tf % 20 == 0) {
				if(gstate.local_player)
					printf("[t%5d] loaded=%d pos=(%.1f,%.1f,%.1f)\n", tf,
						   gstate.world_loaded, gstate.local_player->pos[0],
						   gstate.local_player->pos[1],
						   gstate.local_player->pos[2]);
				else
					printf("[t%5d] loaded=%d (no local_player)\n", tf,
						   gstate.world_loaded);
			}
			// TRAP: catch the player snapping to the world origin (or any
			// teleport-sized jump) during normal play and dump the recent
			// RPC history + position breadcrumbs to trap_dump.txt.
			static vec3 crumbs[128];
			static int crumb_n = 0, armed = 0, dumps = 0;
			if(gstate.world_loaded && gstate.local_player) {
				float* p = gstate.local_player->pos;

				if(armed > 60) {
					float* prev = crumbs[(crumb_n + 127) % 128];
					float dx = p[0] - prev[0], dy = p[1] - prev[1],
						  dz = p[2] - prev[2];
					float jump2 = dx * dx + dy * dy + dz * dz;
					bool at_origin = fabsf(p[0]) < 1.5F && fabsf(p[2]) < 1.5F
						&& p[1] < 2.0F;
					if((jump2 > 64.0F || at_origin) && dumps < 3) {
						dumps++;
						FILE* f = fopen("trap_dump.txt", "a");
						if(f) {
							fprintf(f, "==== TRAP #%d: %s (frame %d) ====\n",
									dumps, at_origin ? "AT ORIGIN" : "POS JUMP",
									tf);
							fprintf(f, "pos now (%.2f,%.2f,%.2f)  prev (%.2f,%.2f,%.2f)  jump %.1f\n",
									p[0], p[1], p[2], prev[0], prev[1], prev[2],
									sqrtf(jump2));
							fprintf(f, "local_player_id=%u screen=%s\n",
									gstate.local_player_id,
									gstate.current_screen == &screen_ingame ?
										"ingame" : "other");
							fprintf(f, "-- breadcrumbs (oldest->newest, every frame) --\n");
							for(int k = 0; k < 128; k++) {
								float* c = crumbs[(crumb_n + k) % 128];
								if(k % 8 == 0 || k > 118)
									fprintf(f, "  t-%03d (%.2f,%.2f,%.2f)\n",
											128 - k, c[0], c[1], c[2]);
							}
							fprintf(f, "-- last client RPCs (type a b c) --\n");
							for(unsigned k = 0; k < 32 && k < trap_crpc_seq; k++) {
								unsigned i = (trap_crpc_seq - 1 - k) % 32;
								fprintf(f, "  seq%u type=%d (%.1f,%.1f,%.1f)\n",
										trap_crpc_ring[i].seq,
										trap_crpc_ring[i].type,
										trap_crpc_ring[i].a, trap_crpc_ring[i].b,
										trap_crpc_ring[i].c);
							}
							fprintf(f, "-- last server RPCs (type a b c) --\n");
							for(unsigned k = 0; k < 32 && k < trap_srpc_seq; k++) {
								unsigned i = (trap_srpc_seq - 1 - k) % 32;
								fprintf(f, "  seq%u type=%d (%.1f,%.1f,%.1f)\n",
										trap_srpc_ring[i].seq,
										trap_srpc_ring[i].type,
										trap_srpc_ring[i].a, trap_srpc_ring[i].b,
										trap_srpc_ring[i].c);
							}
							fprintf(f, "\n");
							fclose(f);
						}
						printf("!!! TRAP fired (#%d) — see trap_dump.txt\n",
							   dumps);
					}
				}

				glm_vec3_copy(p, crumbs[crumb_n % 128]);
				crumb_n++;
				armed++;
			} else {
				// world not loaded: disarm so menu/load transitions don't trip
				armed = 0;
			}
		}
#endif

		float tick_delta = time_diff_s(last_tick, time_get()) / 0.05F;

#ifdef PLATFORM_PC
		static int demo_tick = 0;	  // monotonic 20 Hz demo clock (PC rig)
		int demo_ticks_this_frame = 0; // ticks the demo stepped this frame

		// The demo only drives input once the local player is actually accepting
		// it (capture_input) -- i.e. on screen_ingame, not the load screen. Both
		// the entity tick and camera look are gated on capture_input, so stepping
		// the demo any earlier would silently drop those ticks (a script's `@0`
		// keyframe would be lost). Gating here keeps the very first scripted tick
		// landing on the first tick gameplay reads, which also makes the start
		// point identical across runs.
		bool demo_ready = input_get_virtual_source() && gstate.local_player
			&& gstate.local_player->data.local_player.capture_input;
#endif

		// The demo is stepped INSIDE the normal 20 Hz accumulator so simulation
		// runs at the real tick rate (one tick per frame would couple sim speed
		// to frame rate and over-tick the entity system). Each elapsed tick that
		// the demo drives is counted so capture can emit one image per tick.
		while(tick_delta >= 1.0F) {
			last_tick = time_add_ms(last_tick, 50);
			tick_delta -= 1.0F;
#ifdef PLATFORM_PC
			bool demo_done = false;
			if(demo_ready) {
				// Live agent (#67): publish the state for this tick BEFORE the
				// source reads its action, so the driver decides on the world it
				// is about to act in. In gated mode the step below then blocks
				// until the driver's action line arrives (pause-think-act).
				if(agent_input_active())
					state_export_emit(demo_tick);
				input_virtual_step_tick(demo_tick++);
				demo_ticks_this_frame++;
				// Stop at exactly the last scripted tick so the total tick count
				// (and thus the captured frame count) is independent of how many
				// ticks happen to elapse in the final frame -- this is what keeps
				// "same script twice -> same frame count" true.
				demo_done = input_virtual_at_end();
			}
#endif
			particle_update();
			entities_client_tick(gstate.entities);
#ifdef PLATFORM_PC
			// End after fully simulating this tick; the run holds the final state
			// for one rendered frame, then the rig stitches the dumped frames.
			if(demo_done) {
				gstate.quit = true;
				break;
			}
#endif
		}

		if(gstate.local_player)
			camera_attach(&gstate.camera, gstate.local_player, tick_delta,
						  gstate.stats.dt);
		// Split-screen: advance player 2's camera from its own entity + device.
		if(gstate.local_player2)
			camera_attach(&gstate.camera2, gstate.local_player2, tick_delta,
						  gstate.stats.dt);

		bool render_world
			= gstate.current_screen->render_world && gstate.world_loaded;
		bool in_water = false;

		if(render_world) {
			struct block_data blk = world_get_block(
				&gstate.world, floorf(gstate.camera.x),
				floorf(gstate.camera.y + 0.1F), floorf(gstate.camera.z));
			in_water
				= blk.type == BLOCK_WATER_FLOW || blk.type == BLOCK_WATER_STILL;
		}

		camera_update(&gstate.camera, in_water);

		if(render_world) {
			world_pre_render(&gstate.world, &gstate.camera, gstate.camera.view);

			struct camera* c = &gstate.camera;
			camera_ray_pick(&gstate.world, c->x, c->y, c->z,
							c->x + sinf(c->rx) * sinf(c->ry) * 4.5F,
							c->y + cosf(c->ry) * 4.5F,
							c->z + cosf(c->rx) * sinf(c->ry) * 4.5F,
							&gstate.camera_hit);
		} else {
			world_pre_render_clear(&gstate.world);
			gstate.camera_hit.hit = false;
		}

		world_update_lighting(&gstate.world);
		world_build_chunks(&gstate.world, CHUNK_MESHER_QLENGTH);

		if(gstate.current_screen->update)
			gstate.current_screen->update(gstate.current_screen,
										  gstate.stats.dt);

		gfx_flip_buffers(&gstate.stats.dt_gpu, &gstate.stats.dt_vsync);

		// must not modify displaylists while still rendering!
		chunk_mesher_receive();
		world_render_completed(&gstate.world, render_world);

		vec3 top_plane_color, bottom_plane_color, atmosphere_color;
		daytime_sky_colors(daytime, top_plane_color, bottom_plane_color,
						   atmosphere_color);

		if(render_world) {
			gfx_clear_buffers(atmosphere_color[0], atmosphere_color[1],
							  atmosphere_color[2]);
		} else {
			gfx_clear_buffers(128, 128, 128);
		}

		gfx_fog_color(atmosphere_color[0], atmosphere_color[1],
					  atmosphere_color[2]);

		// === per-player view passes ===
		// Single-player: one full-screen pass, identical to before. Split-screen:
		// two left/right passes. Player 1 was attached + culled above; for player 2
		// the active-view state is swapped into the canonical gstate fields so every
		// renderer below (world, entities, HUD, selection box) draws that player
		// unchanged, and it is swapped back after the loop.
		bool split = render_world && gstate.num_local_players == 2
			&& gstate.local_player2;
		int view_count = split ? 2 : 1;

		for(int view = 0; view < view_count; view++) {
			if(view == 1) {
				mp_swap_active_view(); // activate player 2

				bool p2_water = false;
				if(render_world) {
					struct block_data b = world_get_block(
						&gstate.world, floorf(gstate.camera.x),
						floorf(gstate.camera.y + 0.1F), floorf(gstate.camera.z));
					p2_water = b.type == BLOCK_WATER_FLOW
						|| b.type == BLOCK_WATER_STILL;
				}
				camera_update(&gstate.camera, p2_water);
				world_pre_render(&gstate.world, &gstate.camera,
								 gstate.camera.view);
				struct camera* c = &gstate.camera;
				camera_ray_pick(&gstate.world, c->x, c->y, c->z,
								c->x + sinf(c->rx) * sinf(c->ry) * 4.5F,
								c->y + cosf(c->ry) * 4.5F,
								c->z + cosf(c->rx) * sinf(c->ry) * 4.5F,
								&gstate.camera_hit);
			}

			if(split) {
				// vertical split: player 1 left half, player 2 right half
				int half = gfx_window_width() / 2;
				int vx = (view == 0) ? 0 : half;
				int vw = (view == 0) ? half : gfx_window_width() - half;
				gfx_viewport(vx, 0, vw, gfx_window_height());
				gfx_scissor(true, vx, 0, vw, gfx_window_height());
			}

			// per-view underwater overlay flag (same camera this pass renders)
			bool view_in_water = false;
			if(render_world) {
				struct block_data wb = world_get_block(
					&gstate.world, floorf(gstate.camera.x),
					floorf(gstate.camera.y + 0.1F), floorf(gstate.camera.z));
				view_in_water = wb.type == BLOCK_WATER_FLOW
					|| wb.type == BLOCK_WATER_STILL;
			}

			gfx_mode_world();
			gfx_matrix_projection(gstate.camera.projection, true);

			if(render_world) {
				gfx_update_light(daytime_brightness(daytime),
								 world_dimension_light(&gstate.world));

				if(gstate.world.dimension == WORLD_DIM_OVERWORLD)
					gutil_sky_box(gstate.camera.view_origin, daytime,
								  top_plane_color, bottom_plane_color);

				gstate.stats.chunks_rendered
					= world_render(&gstate.world, &gstate.camera, false);
			} else {
				gstate.stats.chunks_rendered = 0;
			}

			if(gstate.current_screen->render3D) {
				gfx_fog(false);
				gstate.current_screen->render3D(gstate.current_screen,
												gstate.camera.view);
			}

			if(render_world) {
				gfx_fog(false);
				particle_render(
					gstate.camera.view,
					(vec3) {gstate.camera.x, gstate.camera.y, gstate.camera.z},
					tick_delta);
				entities_client_render(gstate.entities, &gstate.camera,
									   tick_delta);
				gfx_fog(true);

				world_render(&gstate.world, &gstate.camera, true);

				if(gstate.world.dimension == WORLD_DIM_OVERWORLD)
					gutil_clouds(gstate.camera.view, daytime);
			}

			gfx_mode_gui();

			if(view_in_water) {
				gfx_bind_texture(&texture_water);
				gutil_texquad_col(0, 0, -gstate.camera.rx / GLM_PI * 256,
								  gstate.camera.ry / GLM_PI * 256, 512,
								  512 * (float)gfx_height() / (float)gfx_width(),
								  gfx_width(), gfx_height(), 0xFF, 0xFF, 0xFF,
								  0x80);
			}

#ifdef PLATFORM_PC
			// Zero the wall-clock timing readout so the captured debug overlay is
			// byte-identical across demo runs (see demo_capture note above).
			if(demo_capture) {
				gstate.stats.fps = 0.0F;
				gstate.stats.dt_gpu = 0.0F;
				gstate.stats.dt_vsync = 0.0F;
			}
#endif

			if(gstate.current_screen->render2D)
				gstate.current_screen->render2D(gstate.current_screen,
												gfx_width(), gfx_height());
		}

		if(split) {
			// restore player 1 (player 2's updated camera/aim go back to the *2
			// fields) and reset the viewport for the full-screen overlays below.
			mp_swap_active_view();
			gfx_viewport_full();
			gfx_scissor(false, 0, 0, 0, 0);
		}

		if(input_pressed(IB_SCREENSHOT)) {
			size_t width, height;
			gfx_copy_framebuffer(NULL, &width, &height);

			void* image = malloc(width * height * 4);

			if(image) {
				gfx_copy_framebuffer(image, &width, &height);

				char name[64];
				snprintf(name, sizeof(name), "%ld.png", (long)time(NULL));

				lodepng_encode32_file(name, image, width, height);
				free(image);
			}
		}

		{
			// CAVEX_AUTOSHOT=N: dump the framebuffer every N frames so an
			// agent (or CI) can see what the game renders without a desktop
			// screenshot tool. -2 = unread, -1 = disabled.
			static int autoshot_every = -2;
			static int autoshot_frame = 0;
			if(autoshot_every == -2) {
				const char* e = getenv("CAVEX_AUTOSHOT");
				autoshot_every = (e && atoi(e) > 0) ? atoi(e) : -1;
			}

			// Frames to dump this iteration. Normally 0 or 1; in demo mode one
			// per tick stepped this frame so the frame set is keyed to the 20 Hz
			// demo tick (FPS-independent, identical count across runs -- this is
			// what makes "same script twice -> same frames" hold for the frame
			// set). If several ticks elapsed in one slow frame, the same rendered
			// framebuffer is written for each so tick numbering stays contiguous.
			int first_tick = 0, last_tick_excl = 0;
			bool frame_capture = false; // non-demo: one numbered shot

#ifdef PLATFORM_PC
			bool demo_active = input_get_virtual_source() != NULL;
			if(autoshot_every > 0 && demo_active && demo_ticks_this_frame > 0) {
				// Dump every Nth demo tick, NOT every tick. A per-tick framebuffer
				// read + PNG encode is heavy enough to perturb the gated tick
				// cadence -- which corrupts aim-dependent skills (place/mine/board
				// mis-aim). Dumping every Nth tick keeps the I/O light (so capture
				// no longer changes behaviour) and gives a controllable GIF rate.
				for(int t = demo_tick - demo_ticks_this_frame; t < demo_tick; t++) {
					if(t % autoshot_every == 0) {
						first_tick = t;
						last_tick_excl = t + 1;   // one shot at this Nth tick
						break;
					}
				}
			}
#else
			bool demo_active = false;
#endif

			if(!demo_active && autoshot_every > 0
			   && ++autoshot_frame % autoshot_every == 0)
				frame_capture = true;

			if(first_tick < last_tick_excl || frame_capture) {
				size_t width, height;
				gfx_copy_framebuffer(NULL, &width, &height);
				void* image = malloc(width * height * 4);
				if(image) {
					gfx_copy_framebuffer(image, &width, &height);
					char name[64];
					if(frame_capture) {
						snprintf(name, sizeof(name), "autoshot_%06d.png",
								 autoshot_frame);
						lodepng_encode32_file(name, image, width, height);
					}
					for(int t = first_tick; t < last_tick_excl; t++) {
						snprintf(name, sizeof(name), "autoshot_%06d.png", t);
						lodepng_encode32_file(name, image, width, height);
					}
					free(image);
				}
			}
		}

		input_poll();
		gfx_finish(true);
	}

	return 0;
}
