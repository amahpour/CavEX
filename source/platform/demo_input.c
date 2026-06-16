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

#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "demo_input.h"

// Button name -> IB_* index. Order is independent of the enum so renumbering
// the enum does not silently break scripts.
static const struct {
	const char* name;
	enum input_button button;
} demo_button_names[] = {
	{"FORWARD", IB_FORWARD},		 {"BACKWARD", IB_BACKWARD},
	{"LEFT", IB_LEFT},				 {"RIGHT", IB_RIGHT},
	{"ACTION1", IB_ACTION1},		 {"ACTION2", IB_ACTION2},
	{"MINE", IB_ACTION1},			 {"PLACE", IB_ACTION2},
	{"JUMP", IB_JUMP},				 {"SNEAK", IB_SNEAK},
	{"INVENTORY", IB_INVENTORY},	 {"HOME", IB_HOME},
	{"SCROLL_LEFT", IB_SCROLL_LEFT}, {"SCROLL_RIGHT", IB_SCROLL_RIGHT},
};

int demo_button_from_name(const char* name) {
	if(!name)
		return -1;

	for(size_t k = 0; k < sizeof(demo_button_names) / sizeof(demo_button_names[0]);
		k++) {
		if(strcasecmp(name, demo_button_names[k].name) == 0)
			return (int)demo_button_names[k].button;
	}

	return -1;
}

// Apply one whitespace-delimited token (e.g. "FORWARD=1" or "LOOK=+2.0,-1.0")
// to the keyframe being built. Returns false on a malformed token.
static bool demo_apply_token(char* token, struct demo_keyframe* kf) {
	char* eq = strchr(token, '=');
	if(!eq)
		return false;

	*eq = '\0';
	const char* key = token;
	const char* value = eq + 1;

	if(strcasecmp(key, "LOOK") == 0) {
		char* comma = strchr((char*)value, ',');
		if(!comma)
			return false;
		*comma = '\0';
		char* end;
		float dx = strtof(value, &end);
		// Require the whole component to be a number (reject "0.06x", "", etc.).
		if(end == value || *end != '\0')
			return false;
		const char* ystr = comma + 1;
		float dy = strtof(ystr, &end);
		if(end == ystr || *end != '\0')
			return false;
		kf->look_dx = dx;
		kf->look_dy = dy;
		return true;
	}

	int b = demo_button_from_name(key);
	if(b < 0)
		return false;

	if(strcmp(value, "0") == 0) {
		kf->buttons[b] = false;
		return true;
	}
	if(strcmp(value, "1") == 0) {
		kf->buttons[b] = true;
		return true;
	}

	return false;
}

bool demo_parse(const char* text, struct demo_script* out, int* err_line) {
	if(!text || !out)
		return false;

	out->count = 0;

	struct demo_keyframe carry; // running state inherited by each keyframe
	memset(&carry, 0, sizeof(carry));

	int line_no = 0;
	const char* p = text;

	while(*p) {
		line_no++;

		// Copy this line into a mutable buffer (strip the trailing newline).
		char line[512];
		size_t n = 0;
		while(*p && *p != '\n') {
			if(n < sizeof(line) - 1)
				line[n++] = *p;
			p++;
		}
		line[n] = '\0';
		if(*p == '\n')
			p++;

		// Strip comments.
		char* hash = strchr(line, '#');
		if(hash)
			*hash = '\0';

		// Tokenise.
		char* save;
		char* tok = strtok_r(line, " \t\r", &save);
		if(!tok)
			continue; // blank / comment-only line

		if(tok[0] != '@') {
			if(err_line)
				*err_line = line_no;
			return false;
		}

		char* end;
		long tick = strtol(tok + 1, &end, 10);
		if(end == tok + 1 || *end != '\0' || tick < 0 || tick > INT_MAX) {
			if(err_line)
				*err_line = line_no;
			return false;
		}

		// Ticks must strictly increase so resolution is unambiguous.
		if(out->count > 0 && (int)tick <= out->frames[out->count - 1].tick) {
			if(err_line)
				*err_line = line_no;
			return false;
		}

		if(out->count >= DEMO_MAX_KEYFRAMES) {
			if(err_line)
				*err_line = line_no;
			return false;
		}

		// New keyframe inherits the carried state, then applies its own tokens.
		carry.tick = (int)tick;
		while((tok = strtok_r(NULL, " \t\r", &save))) {
			if(!demo_apply_token(tok, &carry)) {
				if(err_line)
					*err_line = line_no;
				return false;
			}
		}

		out->frames[out->count++] = carry;
	}

	return true;
}

void demo_state_at(const struct demo_script* s, int tick,
				   bool buttons_out[IB_COUNT], float* look_dx, float* look_dy) {
	for(int b = 0; b < IB_COUNT; b++)
		buttons_out[b] = false;
	if(look_dx)
		*look_dx = 0.0F;
	if(look_dy)
		*look_dy = 0.0F;

	if(!s)
		return;

	const struct demo_keyframe* active = NULL;
	for(size_t k = 0; k < s->count; k++) {
		if(s->frames[k].tick <= tick)
			active = &s->frames[k];
		else
			break;
	}

	if(!active)
		return;

	for(int b = 0; b < IB_COUNT; b++)
		buttons_out[b] = active->buttons[b];
	if(look_dx)
		*look_dx = active->look_dx;
	if(look_dy)
		*look_dy = active->look_dy;
}

#ifdef PLATFORM_PC

// File-replay implementation of the virtual-input source. Per tick it resolves
// the script state and latches the look delta so it is applied exactly once per
// tick (input_joystick is polled per frame; without the latch a look keyframe
// would rotate the camera once for every frame in the tick).
struct demo_file_source {
	struct input_virtual_source base;
	struct demo_script script;
	int tick;
	bool buttons[IB_COUNT];
	float look_dx, look_dy;
	bool look_pending; // true between a tick step and the next look poll
};

static struct demo_file_source demo_file_source_storage;

static void demo_file_step_tick(struct input_virtual_source* self, int tick) {
	struct demo_file_source* s = (struct demo_file_source*)self;
	s->tick = tick;
	demo_state_at(&s->script, tick, s->buttons, &s->look_dx, &s->look_dy);
	s->look_pending = true;
}

static bool demo_file_get_button(struct input_virtual_source* self,
								 enum input_button b) {
	struct demo_file_source* s = (struct demo_file_source*)self;
	if(b < 0 || b >= IB_COUNT)
		return false;
	return s->buttons[b];
}

static void demo_file_get_look(struct input_virtual_source* self, float* dx,
							   float* dy) {
	struct demo_file_source* s = (struct demo_file_source*)self;
	if(s->look_pending) {
		*dx = s->look_dx;
		*dy = s->look_dy;
		s->look_pending = false; // applied once per tick
	} else {
		*dx = 0.0F;
		*dy = 0.0F;
	}
}

static bool demo_file_at_end(struct input_virtual_source* self) {
	struct demo_file_source* s = (struct demo_file_source*)self;
	if(s->script.count == 0)
		return true;
	// End one tick after the last keyframe so its state is held for a frame.
	return s->tick > s->script.frames[s->script.count - 1].tick;
}

struct input_virtual_source* demo_input_create_from_env(void) {
	const char* path = getenv("CAVEX_DEMO");
	if(!path || !path[0])
		return NULL;

	FILE* f = fopen(path, "rb");
	if(!f) {
		fprintf(stderr, "[demo] could not open CAVEX_DEMO '%s'\n", path);
		return NULL;
	}

	fseek(f, 0, SEEK_END);
	long size = ftell(f);
	if(size < 0 || size > 1 << 20) { // 1 MiB cap is plenty for a script
		fclose(f);
		fprintf(stderr, "[demo] CAVEX_DEMO '%s' too large or unreadable\n", path);
		return NULL;
	}
	fseek(f, 0, SEEK_SET);

	char* buf = malloc((size_t)size + 1);
	if(!buf) {
		fclose(f);
		return NULL;
	}
	size_t got = fread(buf, 1, (size_t)size, f);
	buf[got] = '\0';
	fclose(f);

	struct demo_file_source* s = &demo_file_source_storage;
	memset(s, 0, sizeof(*s));

	int err_line = 0;
	bool ok = demo_parse(buf, &s->script, &err_line);
	free(buf);

	if(!ok) {
		fprintf(stderr, "[demo] parse error in '%s' at line %d\n", path,
				err_line);
		return NULL;
	}

	if(s->script.count == 0) {
		fprintf(stderr, "[demo] '%s' has no keyframes -- nothing to replay\n",
				path);
		return NULL;
	}

	s->base.step_tick = demo_file_step_tick;
	s->base.get_button = demo_file_get_button;
	s->base.get_look = demo_file_get_look;
	s->base.at_end = demo_file_at_end;
	s->tick = -1;

	printf("[demo] replaying '%s' (%zu keyframes)\n", path, s->script.count);
	return &s->base;
}

#endif
