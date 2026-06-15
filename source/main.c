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

		while(tick_delta >= 1.0F) {
			last_tick = time_add_ms(last_tick, 50);
			tick_delta -= 1.0F;
			particle_update();
			entities_client_tick(gstate.entities);
		}

		if(gstate.local_player)
			camera_attach(&gstate.camera, gstate.local_player, tick_delta,
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
			entities_client_render(gstate.entities, &gstate.camera, tick_delta);
			gfx_fog(true);

			world_render(&gstate.world, &gstate.camera, true);

			if(gstate.world.dimension == WORLD_DIM_OVERWORLD)
				gutil_clouds(gstate.camera.view, daytime);
		}

		gfx_mode_gui();

		if(in_water) {
			gfx_bind_texture(&texture_water);
			gutil_texquad_col(0, 0, -gstate.camera.rx / GLM_PI * 256,
							  gstate.camera.ry / GLM_PI * 256, 512,
							  512 * (float)gfx_height() / (float)gfx_width(),
							  gfx_width(), gfx_height(), 0xFF, 0xFF, 0xFF,
							  0x80);
		}

		if(gstate.current_screen->render2D)
			gstate.current_screen->render2D(gstate.current_screen, gfx_width(),
											gfx_height());

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
			if(autoshot_every > 0 && ++autoshot_frame % autoshot_every == 0) {
				size_t width, height;
				gfx_copy_framebuffer(NULL, &width, &height);
				void* image = malloc(width * height * 4);
				if(image) {
					gfx_copy_framebuffer(image, &width, &height);
					char name[64];
					snprintf(name, sizeof(name), "autoshot_%06d.png",
							 autoshot_frame);
					lodepng_encode32_file(name, image, width, height);
					free(image);
				}
			}
		}

		input_poll();
		gfx_finish(true);
	}

	return 0;
}
