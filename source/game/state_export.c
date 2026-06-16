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

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>

#include "state_export.h"

// Hand-rolled JSON so this stays free of any heap/library dependency and can be
// linked into the pure unit-test harness. We accumulate with snprintf into a
// moving cursor and track the would-be length so truncation is detectable
// exactly like snprintf (return >= bufsz). `off` is the running would-be
// length; we only ever write while off < bufsz.
static void put(char* buf, size_t bufsz, int* off, const char* fmt, ...)
	__attribute__((format(printf, 4, 5)));

static void put(char* buf, size_t bufsz, int* off, const char* fmt, ...) {
	va_list ap;
	va_start(ap, fmt);

	// Single clamp: how much of the buffer is already used (never past bufsz).
	// With no real buffer we still let vsnprintf compute the would-be length,
	// but must pass (NULL, 0) -- the only NULL form vsnprintf accepts -- so a
	// NULL/oversized-size combination can never deref through NULL.
	size_t used = (*off >= 0 && (size_t)*off < bufsz) ? (size_t)*off : bufsz;
	char* dst = buf ? buf + used : NULL;
	size_t cap = dst ? bufsz - used : 0;
	int n = vsnprintf(dst, cap, fmt, ap);
	va_end(ap);

	if(n > 0)
		*off += n;
}

int state_export_write(char* buf, size_t bufsz,
					   const struct game_export_state* st) {
	if(buf && bufsz > 0)
		buf[0] = '\0';
	if(!st)
		return 0;

	int off = 0;

	put(buf, bufsz, &off, "{\"tick\":%d", st->tick);

	const char* screen = st->screen ? st->screen : "unknown";
	put(buf, bufsz, &off, ",\"screen\":\"%s\"", screen);

	put(buf, bufsz, &off, ",\"player\":%s", st->has_player ? "true" : "false");

	if(st->has_player) {
		put(buf, bufsz, &off, ",\"pos\":[%.2f,%.2f,%.2f]", st->px, st->py,
			st->pz);
		put(buf, bufsz, &off, ",\"orient\":[%.3f,%.3f]", st->yaw, st->pitch);
		put(buf, bufsz, &off, ",\"on_ground\":%s",
			st->on_ground ? "true" : "false");
		put(buf, bufsz, &off, ",\"flying\":%s", st->flying ? "true" : "false");
		put(buf, bufsz, &off, ",\"creative\":%s",
			st->creative ? "true" : "false");

		put(buf, bufsz, &off, ",\"hotbar\":{\"slot\":%d,\"item\":%d,\"count\":%d}",
			st->hotbar_slot, st->hotbar_item, st->hotbar_count);

		if(st->has_aim) {
			put(buf, bufsz, &off,
				",\"aim\":{\"x\":%d,\"y\":%d,\"z\":%d,\"block\":%d}", st->aim_x,
				st->aim_y, st->aim_z, st->aim_block);
		} else {
			put(buf, bufsz, &off, ",\"aim\":null");
		}

		if(st->has_heightmap) {
			put(buf, bufsz, &off, ",\"heightmap\":[");
			int n = STATE_EXPORT_HEIGHT_WINDOW * STATE_EXPORT_HEIGHT_WINDOW;
			for(int k = 0; k < n; k++)
				put(buf, bufsz, &off, "%s%d", k ? "," : "", st->heightmap[k]);
			put(buf, bufsz, &off, "]");
		}
	}

	put(buf, bufsz, &off, "}");
	return off;
}

// The live collector reads the engine (gstate/world/inventory/screens). The
// unit-test harness links this file only for the PURE serializer above and does
// NOT provide those engine symbols, so the collector is excluded from the test
// build (CAVEX_TEST_BUILD). It is otherwise compiled for the PC game.
#if defined(PLATFORM_PC) && !defined(CAVEX_TEST_BUILD)

#include <math.h>
#include <string.h>

#include "../block/blocks_data.h"
#include "../entity/entity.h"
#include "../item/inventory.h"
#include "../item/window_container.h"
#include "../world.h"
#include "game_state.h"
#include "gui/screen.h"

// Map the active screen pointer to a short, stable name for the JSON.
static const char* state_export_screen_name(void) {
	const struct screen* s = gstate.current_screen;
	if(s == &screen_ingame)
		return "ingame";
	if(s == &screen_load_world)
		return "load_world";
	if(s == &screen_select_world)
		return "select_world";
	if(s == &screen_inventory)
		return "inventory";
	if(s == &screen_crafting)
		return "crafting";
	if(s == &screen_furnace)
		return "furnace";
	return "other";
}

void state_export_emit(int tick) {
	struct game_export_state st;
	memset(&st, 0, sizeof(st));

	st.tick = tick;
	st.screen = state_export_screen_name();
	st.hotbar_slot = -1;
	st.hotbar_item = -1;
	st.hotbar_count = 0;

	struct entity* p = gstate.local_player;
	st.has_player = gstate.world_loaded && p != NULL;

	if(st.has_player) {
		st.px = p->pos[0];
		st.py = p->pos[1];
		st.pz = p->pos[2];
		st.yaw = p->orient[0];
		st.pitch = p->orient[1];
		st.on_ground = p->on_ground;
		st.flying = p->data.local_player.flying;
		// Creative mode is a separate (later) feature; until it lands flight is
		// the only "creative-ish" state, so report it conservatively as off.
		st.creative = false;

		struct inventory* inv
			= windowc_get_latest(gstate.windows[WINDOWC_INVENTORY]);
		if(inv) {
			st.hotbar_slot = (int)inventory_get_hotbar(inv);
			struct item_data it;
			if(inventory_get_hotbar_item(inv, &it)) {
				st.hotbar_item = (int)it.id;
				st.hotbar_count = (int)it.count;
			}
		}

		if(gstate.camera_hit.hit) {
			st.has_aim = true;
			st.aim_x = gstate.camera_hit.x;
			st.aim_y = gstate.camera_hit.y;
			st.aim_z = gstate.camera_hit.z;
			struct block_data b = world_get_block(&gstate.world,
												  gstate.camera_hit.x,
												  gstate.camera_hit.y,
												  gstate.camera_hit.z);
			st.aim_block = b.type;
		}

		// Local heightmap window centred on the player's column, so the driver
		// can steer toward higher ground without per-block queries of its own.
		int half = STATE_EXPORT_HEIGHT_WINDOW / 2;
		int cx = (int)floorf(p->pos[0]);
		int cz = (int)floorf(p->pos[2]);
		int idx = 0;
		for(int dz = -half; dz <= half; dz++) {
			for(int dx = -half; dx <= half; dx++) {
				st.heightmap[idx++]
					= world_get_height(&gstate.world, cx + dx, cz + dz);
			}
		}
		st.has_heightmap = true;
	}

	char line[1024];
	state_export_write(line, sizeof(line), &st);
	printf("%s\n", line);
	fflush(stdout);
}

#endif
