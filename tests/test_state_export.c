// Unit tests for the live AI/heuristic action source (#67):
//   - state_export_write(): the PURE game-state JSON serializer
//   - agent_parse_action(): the PURE per-tick action-line parser
// Both are free of I/O and globals, so they are fully deterministic here.
//
// NOTE: the repo enforces a per-test coverage gate (each registered test must
// add >=1 line not covered by any other test), so tests are grouped by the
// distinct code paths they exercise.

#include <string.h>

#include "game/state_export.h"
#include "harness.h"
#include "platform/demo_input.h"

// A small helper: does `hay` contain `needle`?
static bool contains(const char* hay, const char* needle) {
	return strstr(hay, needle) != NULL;
}

// Full player snapshot -> all fields present; aim + heightmap rendered.
TEST(state_export_full) {
	struct game_export_state st;
	memset(&st, 0, sizeof(st));
	st.tick = 42;
	st.screen = "ingame";
	st.has_player = true;
	st.px = 1.5;
	st.py = 65.0;
	st.pz = -2.25;
	st.yaw = 0.5;
	st.pitch = -0.25;
	st.on_ground = true;
	st.flying = false;
	st.creative = false;
	st.hotbar_slot = 3;
	st.hotbar_item = 17;
	st.hotbar_count = 2;
	st.has_aim = true;
	st.aim_x = 10;
	st.aim_y = 64;
	st.aim_z = -3;
	st.aim_block = 1;
	st.has_heightmap = true;
	for(int k = 0; k < STATE_EXPORT_HEIGHT_WINDOW * STATE_EXPORT_HEIGHT_WINDOW;
		k++)
		st.heightmap[k] = 60 + k;

	char buf[1024];
	int n = state_export_write(buf, sizeof(buf), &st);

	ASSERT(n > 0);
	ASSERT_EQ((size_t)n, strlen(buf)); // not truncated: return == written length
	ASSERT_EQ(buf[0], '{');
	ASSERT_EQ(buf[strlen(buf) - 1], '}');

	ASSERT(contains(buf, "\"tick\":42"));
	ASSERT(contains(buf, "\"screen\":\"ingame\""));
	ASSERT(contains(buf, "\"player\":true"));
	ASSERT(contains(buf, "\"pos\":[1.50,65.00,-2.25]"));
	ASSERT(contains(buf, "\"orient\":[0.500,-0.250]"));
	ASSERT(contains(buf, "\"on_ground\":true"));
	ASSERT(contains(buf, "\"flying\":false"));
	ASSERT(contains(buf, "\"creative\":false"));
	ASSERT(contains(buf, "\"hotbar\":{\"slot\":3,\"item\":17,\"count\":2}"));
	ASSERT(contains(buf, "\"aim\":{\"x\":10,\"y\":64,\"z\":-3,\"block\":1}"));
	// first heightmap cell = 60, last = 60 + (25-1) = 84
	ASSERT(contains(buf, "\"heightmap\":[60,"));
	ASSERT(contains(buf, ",84]"));
}

// No player (menu/loading): only the lightweight header fields appear; no pos,
// no aim, no heightmap. Also covers the NULL-screen default and the aim:null /
// missing-heightmap branches.
TEST(state_export_no_player_and_defaults) {
	struct game_export_state st;
	memset(&st, 0, sizeof(st));
	st.tick = 7;
	st.screen = NULL; // -> "unknown"
	st.has_player = false;

	char buf[256];
	int n = state_export_write(buf, sizeof(buf), &st);
	ASSERT(n > 0);
	ASSERT(contains(buf, "\"tick\":7"));
	ASSERT(contains(buf, "\"screen\":\"unknown\""));
	ASSERT(contains(buf, "\"player\":false"));
	ASSERT(!contains(buf, "\"pos\"")); // gated on has_player
	ASSERT(!contains(buf, "\"aim\""));
	ASSERT(!contains(buf, "\"heightmap\""));

	// has_player but no aim / no heightmap -> aim:null, heightmap omitted.
	memset(&st, 0, sizeof(st));
	st.screen = "ingame";
	st.has_player = true;
	st.has_aim = false;
	st.has_heightmap = false;
	st.hotbar_slot = -1;
	st.hotbar_item = -1;
	n = state_export_write(buf, sizeof(buf), &st);
	ASSERT(n > 0);
	ASSERT(contains(buf, "\"aim\":null"));
	ASSERT(!contains(buf, "\"heightmap\""));
	ASSERT(contains(buf, "\"hotbar\":{\"slot\":-1,\"item\":-1,\"count\":0}"));
}

// Truncation + NULL guards behave like snprintf: always NUL-terminated, and the
// return value is the would-be length so a caller can detect overflow.
TEST(state_export_truncation_and_null) {
	struct game_export_state st;
	memset(&st, 0, sizeof(st));
	st.tick = 123456;
	st.screen = "ingame";
	st.has_player = true;
	st.has_heightmap = true;

	char small[16];
	int n = state_export_write(small, sizeof(small), &st);
	ASSERT(n >= (int)sizeof(small)); // would-be length exceeds the buffer
	ASSERT_EQ(small[sizeof(small) - 1], '\0'); // still NUL-terminated

	// bufsz 0: snprintf semantics -- write nothing but still report the
	// would-be length (so a caller can size a buffer). The sentinel byte we
	// pre-seed must be left untouched.
	small[0] = '!';
	int n0 = state_export_write(small, 0, &st);
	ASSERT(n0 > 0);			   // would-be length, not zero
	ASSERT_EQ(small[0], '!');  // nothing written into the buffer

	// NULL state yields 0 and clears the buffer; NULL buf is tolerated.
	ASSERT_EQ(state_export_write(small, sizeof(small), NULL), 0);
	ASSERT_EQ(small[0], '\0'); // cleared by the NULL-state path

	// NULL buffer is tolerated for sizing with EITHER a zero or a nonzero size
	// (the documented "measure the line" use): it must not deref through NULL
	// and reports the same would-be length both ways.
	ASSERT_EQ(state_export_write(NULL, 0, &st),
			  state_export_write(NULL, 99, &st));
	ASSERT(state_export_write(NULL, 99, &st) > 0);
}

// ---- action parser ---------------------------------------------------------

// A typical multi-token line: buttons + look, aliases, omitted = neutral.
TEST(agent_parse_basic) {
	struct agent_action a;

	ASSERT(agent_parse_action("FORWARD=1 LOOK=+0.05,-0.10 MINE=1", &a));
	ASSERT_EQ(a.buttons[IB_FORWARD], true);
	ASSERT_EQ(a.buttons[IB_ACTION1], true); // MINE alias
	ASSERT_EQ(a.buttons[IB_BACKWARD], false); // omitted -> neutral
	ASSERT_NEAR(a.look_dx, 0.05, 1e-6);
	ASSERT_NEAR(a.look_dy, -0.10, 1e-6);

	// PLACE alias + explicit 0 clears.
	ASSERT(agent_parse_action("PLACE=1 FORWARD=0", &a));
	ASSERT_EQ(a.buttons[IB_ACTION2], true);
	ASSERT_EQ(a.buttons[IB_FORWARD], false);

	// Each call is self-contained (no inheritance): a fresh line resets all.
	ASSERT(agent_parse_action("JUMP=1", &a));
	ASSERT_EQ(a.buttons[IB_JUMP], true);
	ASSERT_EQ(a.buttons[IB_ACTION2], false); // not carried from the prior line
	ASSERT_NEAR(a.look_dx, 0.0, 1e-6);		 // look also reset
}

// Blank / comment-only / NULL lines are valid and produce the neutral action.
TEST(agent_parse_blank_and_null) {
	struct agent_action a;

	ASSERT(agent_parse_action("", &a));
	ASSERT_EQ(a.buttons[IB_FORWARD], false);
	ASSERT_NEAR(a.look_dx, 0.0, 1e-6);

	ASSERT(agent_parse_action("   \t \n", &a)); // whitespace + newline only
	ASSERT_EQ(a.buttons[IB_JUMP], false);

	ASSERT(agent_parse_action("# just a comment", &a));
	ASSERT_EQ(a.buttons[IB_FORWARD], false);

	// A trailing comment after real tokens is stripped.
	ASSERT(agent_parse_action("FORWARD=1 # go", &a));
	ASSERT_EQ(a.buttons[IB_FORWARD], true);

	ASSERT(agent_parse_action(NULL, &a)); // NULL line -> neutral, still ok
	ASSERT_EQ(a.buttons[IB_FORWARD], false);

	ASSERT(!agent_parse_action("FORWARD=1", NULL)); // NULL out -> false
}

// Malformed tokens are rejected (returns false).
TEST(agent_parse_errors) {
	struct agent_action a;

	ASSERT(!agent_parse_action("WIGGLE=1", &a));	  // unknown button
	ASSERT(!agent_parse_action("FORWARD=2", &a));	  // bad value
	ASSERT(!agent_parse_action("FORWARD", &a));		  // no '='
	ASSERT(!agent_parse_action("LOOK=1.0", &a));	  // missing comma
	ASSERT(!agent_parse_action("LOOK=x,0.0", &a));	  // non-numeric
	ASSERT(!agent_parse_action("LOOK=0.1z,0.0", &a)); // trailing garbage
}

const test_entry_t g_tests_state_export[] = {
	{"state_export_full", test_state_export_full},
	{"state_export_no_player_and_defaults",
	 test_state_export_no_player_and_defaults},
	{"state_export_truncation_and_null", test_state_export_truncation_and_null},
	{"agent_parse_basic", test_agent_parse_basic},
	{"agent_parse_blank_and_null", test_agent_parse_blank_and_null},
	{"agent_parse_errors", test_agent_parse_errors},
};

const size_t g_tests_state_export_count
	= sizeof(g_tests_state_export) / sizeof(g_tests_state_export[0]);
