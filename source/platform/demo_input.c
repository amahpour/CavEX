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
	{"MAP", IB_MAP},
	{"TOGGLE_CREATIVE", IB_TOGGLE_CREATIVE}, {"CREATIVE", IB_TOGGLE_CREATIVE},
	{"CREATIVE_PAGE", IB_CREATIVE_PAGE},	 {"PAGE", IB_CREATIVE_PAGE},
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
// to a button/look state. Returns false on a malformed token. Shared by the
// demo-script keyframe parser and the live-agent action parser so both honour
// exactly the same token grammar. `token` is mutated in place (the '=' and ','
// are split with NULs).
static bool demo_apply_token(char* token, bool buttons[IB_COUNT],
							 float* look_dx, float* look_dy) {
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
		*look_dx = dx;
		*look_dy = dy;
		return true;
	}

	int b = demo_button_from_name(key);
	if(b < 0)
		return false;

	if(strcmp(value, "0") == 0) {
		buttons[b] = false;
		return true;
	}
	if(strcmp(value, "1") == 0) {
		buttons[b] = true;
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
			if(!demo_apply_token(tok, carry.buttons, &carry.look_dx,
								 &carry.look_dy)) {
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

bool agent_parse_action(const char* line, struct agent_action* out) {
	if(!out)
		return false;

	memset(out, 0, sizeof(*out)); // omitted fields read neutral

	if(!line)
		return true; // treated like a blank line: neutral action

	// Copy into a mutable buffer so the token splitter can write NULs; strip a
	// trailing newline and any '#' comment, mirroring the script grammar.
	char buf[512];
	size_t n = 0;
	for(const char* p = line; *p && *p != '\n' && n < sizeof(buf) - 1; p++)
		buf[n++] = *p;
	buf[n] = '\0';

	char* hash = strchr(buf, '#');
	if(hash)
		*hash = '\0';

	char* save;
	for(char* tok = strtok_r(buf, " \t\r", &save); tok;
		tok = strtok_r(NULL, " \t\r", &save)) {
		if(!demo_apply_token(tok, out->buttons, &out->look_dx, &out->look_dy))
			return false;
	}

	return true;
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

// -----------------------------------------------------------------------------
// Live action source: read one action line from stdin per tick. In GATED mode
// step_tick() blocks until a full line arrives (pause-think-act); otherwise it
// drains whatever bytes are ready without blocking and is neutral when no
// complete line is available this tick (real-time).
//
// Input is read from the raw stdin fd with read(), NOT stdio (fgets), and
// assembled into a persistent line buffer. This avoids two classic traps:
//   - select()/poll() on the fd cannot see bytes already sitting in a stdio
//     FILE buffer, so mixing select() with fgets() can miss queued input;
//   - select()-ready means ">=1 byte", not "a full line", so fgets() can block
//     mid-line even in real-time mode. Buffering raw bytes ourselves and only
//     completing an action on a '\n' fixes both.
//
// This glue is part of the engine input path (it touches input_virtual_source),
// which the pure unit-test harness does not link, so it is excluded from the
// test build (CAVEX_TEST_BUILD). The pure action parser above IS tested.
#ifndef CAVEX_TEST_BUILD

#include <sys/select.h>
#include <unistd.h>

#define AGENT_LINE_MAX 512

struct agent_live_source {
	struct input_virtual_source base;
	struct agent_action action; // input for the current tick
	bool gated;					// block per tick until a line arrives
	bool ended;					// stdin reached EOF -> finish the run
	bool look_pending;			// apply the look delta once per tick
	char line[AGENT_LINE_MAX];	// partial line accumulated across reads
	size_t line_len;			// bytes currently in `line`
};

static struct agent_live_source agent_live_source_storage;

// True if stdin has at least one byte readable without blocking.
static bool agent_stdin_ready(void) {
	fd_set rfds;
	FD_ZERO(&rfds);
	FD_SET(STDIN_FILENO, &rfds);
	struct timeval tv = {0, 0};
	int r = select(STDIN_FILENO + 1, &rfds, NULL, NULL, &tv);
	return r > 0 && FD_ISSET(STDIN_FILENO, &rfds);
}

// Pull one complete '\n'-terminated line into out[outsz] (newline stripped,
// NUL-terminated). Returns 1 on a full line, 0 if none is ready yet (real-time
// only), -1 on EOF. In gated mode it blocks (read() on a blocking fd) until a
// line or EOF; in real-time mode it only consumes bytes that are ready.
static int agent_read_line(struct agent_live_source* s, char* out,
						   size_t outsz) {
	for(;;) {
		// Is a complete line already buffered?
		for(size_t i = 0; i < s->line_len; i++) {
			if(s->line[i] == '\n') {
				size_t n = i < outsz - 1 ? i : outsz - 1;
				memcpy(out, s->line, n);
				out[n] = '\0';
				// Shift the remainder (next line's bytes) down.
				size_t rest = s->line_len - (i + 1);
				memmove(s->line, s->line + i + 1, rest);
				s->line_len = rest;
				return 1;
			}
		}

		// No full line yet. In real-time mode, only read if bytes are waiting.
		if(!s->gated && !agent_stdin_ready())
			return 0;

		// Make room; if the buffer is full without a newline the line is over
		// length -- drop it (keep the latest bytes) rather than block forever.
		if(s->line_len >= sizeof(s->line)) {
			fprintf(stderr, "[agent] over-long action line dropped\n");
			s->line_len = 0;
		}

		ssize_t got = read(STDIN_FILENO, s->line + s->line_len,
						   sizeof(s->line) - s->line_len);
		if(got == 0)
			return -1; // EOF
		if(got < 0)
			return s->gated ? -1 : 0; // treat a read error as no-input/EOF
		s->line_len += (size_t)got;
	}
}

static void agent_live_step_tick(struct input_virtual_source* self, int tick) {
	(void)tick;
	struct agent_live_source* s = (struct agent_live_source*)self;

	// Default: no input this tick (real-time mode with nothing pending).
	memset(&s->action, 0, sizeof(s->action));
	s->look_pending = true;

	if(s->ended)
		return;

	char line[AGENT_LINE_MAX];
	int r = agent_read_line(s, line, sizeof(line));
	if(r < 0) {
		// EOF (the driver closed its pipe): end the run after this tick.
		s->ended = true;
		return;
	}
	if(r == 0)
		return; // real-time: keep the neutral action, don't block

	// A malformed line is non-fatal here (live input is noisy): warn and treat
	// the tick as neutral rather than aborting the session.
	if(!agent_parse_action(line, &s->action)) {
		fprintf(stderr, "[agent] ignoring malformed action line: %s\n", line);
		memset(&s->action, 0, sizeof(s->action));
	}
}

static bool agent_live_get_button(struct input_virtual_source* self,
								  enum input_button b) {
	struct agent_live_source* s = (struct agent_live_source*)self;
	if(b < 0 || b >= IB_COUNT)
		return false;
	return s->action.buttons[b];
}

static void agent_live_get_look(struct input_virtual_source* self, float* dx,
								float* dy) {
	struct agent_live_source* s = (struct agent_live_source*)self;
	if(s->look_pending) {
		*dx = s->action.look_dx;
		*dy = s->action.look_dy;
		s->look_pending = false; // applied once per tick
	} else {
		*dx = 0.0F;
		*dy = 0.0F;
	}
}

static bool agent_live_at_end(struct input_virtual_source* self) {
	struct agent_live_source* s = (struct agent_live_source*)self;
	return s->ended;
}

struct input_virtual_source* agent_input_create_from_env(void) {
	const char* on = getenv("CAVEX_AGENT");
	if(!on || on[0] != '1')
		return NULL;

	struct agent_live_source* s = &agent_live_source_storage;
	memset(s, 0, sizeof(*s));

	const char* gated = getenv("CAVEX_AGENT_GATED");
	s->gated = gated && gated[0] == '1';

	s->base.step_tick = agent_live_step_tick;
	s->base.get_button = agent_live_get_button;
	s->base.get_look = agent_live_get_look;
	s->base.at_end = agent_live_at_end;

	printf("[agent] live action source active (%s)\n",
		   s->gated ? "gated/pause-think-act" : "real-time");
	fflush(stdout);
	return &s->base;
}

bool agent_input_active(void) {
	return input_get_virtual_source() == &agent_live_source_storage.base;
}

#endif // !CAVEX_TEST_BUILD

#endif // PLATFORM_PC
