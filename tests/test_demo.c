// Unit tests for the pure demo-script parser behind the gameplay capture rig
// (#66). The parser turns a tick-stamped text script into resolved keyframes;
// demo_state_at() then reports the input state in effect at any tick. No I/O,
// no globals -- fully deterministic.
//
// NOTE: the repo enforces a per-test coverage gate (each registered test must
// add >=1 line not covered by any other test), so these are grouped by the
// distinct code paths they exercise rather than split one-assertion-per-test.

#include "harness.h"
#include "platform/demo_input.h"

// Name mapping (case-insensitive, aliases, unknowns) + the empty/neutral path.
TEST(demo_names_and_empty) {
	ASSERT_EQ(demo_button_from_name("FORWARD"), IB_FORWARD);
	ASSERT_EQ(demo_button_from_name("forward"), IB_FORWARD); // case-insensitive
	ASSERT_EQ(demo_button_from_name("JUMP"), IB_JUMP);
	ASSERT_EQ(demo_button_from_name("MINE"), IB_ACTION1);	// alias
	ASSERT_EQ(demo_button_from_name("PLACE"), IB_ACTION2);	// alias
	// Creative-toggle token + alias (issue #21): lets a demo script drive the
	// IB_TOGGLE_CREATIVE keybind so creative mode can be demonstrated by the rig.
	ASSERT_EQ(demo_button_from_name("TOGGLE_CREATIVE"), IB_TOGGLE_CREATIVE);
	ASSERT_EQ(demo_button_from_name("CREATIVE"), IB_TOGGLE_CREATIVE); // alias
	ASSERT_EQ(demo_button_from_name("NOPE"), -1);
	ASSERT_EQ(demo_button_from_name(NULL), -1);

	// A comment-only script is valid and has no keyframes; every tick resolves
	// to neutral (this drives the s->count == 0 path in demo_state_at and the
	// NULL-script guard).
	struct demo_script s;
	ASSERT(demo_parse("# only a comment\n", &s, NULL));
	ASSERT_EQ(s.count, 0u);

	bool btn[IB_COUNT];
	float dx = 9, dy = 9;
	demo_state_at(&s, 999, btn, &dx, &dy);
	ASSERT_EQ(btn[IB_FORWARD], false);
	ASSERT_NEAR(dx, 0.0, 1e-6);
	ASSERT_NEAR(dy, 0.0, 1e-6);
	demo_state_at(NULL, 0, btn, NULL, NULL); // NULL-script guard, NULL look ptrs
	ASSERT_EQ(btn[IB_JUMP], false);
}

// A multi-keyframe script parses; keyframes inherit prior state; look deltas are
// captured and held; comments/blank lines and multi-button keyframes work.
TEST(demo_parse_basic_and_resolve) {
	const char* script = "\n"
						 "   # leading comment\n"
						 "@0  FORWARD=1 JUMP=1   # walk + jump\n"
						 "@10 JUMP=0 LOOK=+2.0,-1.0\n"
						 "@20 FORWARD=0\n";
	struct demo_script s;
	int err = -1;
	ASSERT(demo_parse(script, &s, &err));
	ASSERT_EQ(s.count, 3u);

	bool btn[IB_COUNT];
	float dx, dy;

	// Before any keyframe: everything neutral.
	demo_state_at(&s, -1, btn, &dx, &dy);
	ASSERT_EQ(btn[IB_FORWARD], false);
	ASSERT_NEAR(dx, 0.0, 1e-6);

	// Tick 0..9: forward + jump held (two buttons on one keyframe), no look.
	demo_state_at(&s, 5, btn, &dx, &dy);
	ASSERT_EQ(btn[IB_FORWARD], true);
	ASSERT_EQ(btn[IB_JUMP], true);
	ASSERT_NEAR(dx, 0.0, 1e-6);

	// Tick 10..19: forward STILL held (inherited), jump released, look applied.
	demo_state_at(&s, 15, btn, &dx, &dy);
	ASSERT_EQ(btn[IB_FORWARD], true);
	ASSERT_EQ(btn[IB_JUMP], false);
	ASSERT_NEAR(dx, 2.0, 1e-6);
	ASSERT_NEAR(dy, -1.0, 1e-6);

	// Tick 20+: forward released, look delta inherited (held until changed).
	demo_state_at(&s, 100, btn, &dx, &dy);
	ASSERT_EQ(btn[IB_FORWARD], false);
	ASSERT_NEAR(dx, 2.0, 1e-6);
}

// Malformed input is rejected with the offending 1-based line number.
TEST(demo_parse_errors) {
	struct demo_script s;
	int err;

	// A line not starting with '@'.
	err = 0;
	ASSERT(!demo_parse("@0 FORWARD=1\nGARBAGE\n", &s, &err));
	ASSERT_EQ(err, 2);

	// Unknown button name.
	err = 0;
	ASSERT(!demo_parse("@0 WIGGLE=1\n", &s, &err));
	ASSERT_EQ(err, 1);

	// Bad button value (not 0/1).
	err = 0;
	ASSERT(!demo_parse("@0 FORWARD=2\n", &s, &err));
	ASSERT_EQ(err, 1);

	// Malformed LOOK (missing comma).
	err = 0;
	ASSERT(!demo_parse("@0 LOOK=1.0\n", &s, &err));
	ASSERT_EQ(err, 1);

	// Malformed LOOK (non-numeric component).
	err = 0;
	ASSERT(!demo_parse("@0 LOOK=x,1.0\n", &s, &err));
	ASSERT_EQ(err, 1);

	// LOOK with trailing garbage after a valid number prefix is rejected (the
	// component must be wholly numeric, not just start with a number).
	err = 0;
	ASSERT(!demo_parse("@0 LOOK=0.06x,0.0\n", &s, &err));
	ASSERT_EQ(err, 1);
	err = 0;
	ASSERT(!demo_parse("@0 LOOK=1.0,2.0junk\n", &s, &err));
	ASSERT_EQ(err, 1);

	// Tick overflow (> INT_MAX) is rejected, not silently truncated.
	err = 0;
	ASSERT(!demo_parse("@99999999999 FORWARD=1\n", &s, &err));
	ASSERT_EQ(err, 1);

	// Non-increasing ticks.
	err = 0;
	ASSERT(!demo_parse("@5 FORWARD=1\n@5 FORWARD=0\n", &s, &err));
	ASSERT_EQ(err, 2);

	// Negative tick.
	err = 0;
	ASSERT(!demo_parse("@-1 FORWARD=1\n", &s, &err));
	ASSERT_EQ(err, 1);

	// Non-numeric tick.
	err = 0;
	ASSERT(!demo_parse("@x FORWARD=1\n", &s, &err));
	ASSERT_EQ(err, 1);

	// Token without '='.
	err = 0;
	ASSERT(!demo_parse("@0 FORWARD\n", &s, &err));
	ASSERT_EQ(err, 1);

	// NULL guards.
	ASSERT(!demo_parse(NULL, &s, NULL));
	ASSERT(!demo_parse("@0 FORWARD=1\n", NULL, NULL));
}

const test_entry_t g_tests_demo[] = {
	{"demo_names_and_empty", test_demo_names_and_empty},
	{"demo_parse_basic_and_resolve", test_demo_parse_basic_and_resolve},
	{"demo_parse_errors", test_demo_parse_errors},
};

const size_t g_tests_demo_count
	= sizeof(g_tests_demo) / sizeof(g_tests_demo[0]);
