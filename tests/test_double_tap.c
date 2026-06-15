// Unit tests for the pure double-tap detector behind creative flight (#22).
// The detector is called once per game tick with the just-pressed edge of the
// jump button; it returns true on the second press inside the ~5-tick window
// (a toggle should fire) and false otherwise.

#include "entity/entity.h"
#include "harness.h"

// Two presses inside the window toggle: the second press returns true.
// Also exercises the toggle (return-true) branch of the detector.
TEST(double_tap_second_press_toggles) {
	int window = 0;
	ASSERT_EQ(detect_double_tap(true, &window), false); // first tap opens window
	ASSERT(window > 0);                                 // window now open
	ASSERT_EQ(detect_double_tap(true, &window), true);  // second tap -> toggle
	ASSERT_EQ(window, 0);                               // window consumed
}

// Presses spread beyond the window do NOT toggle: the window decays to zero
// across idle ticks, so the next press is just a fresh first tap.
TEST(double_tap_expires_no_toggle) {
	int window = 0;
	ASSERT_EQ(detect_double_tap(true, &window), false); // first tap, window open
	for(int k = 0; k < 5; k++)
		ASSERT_EQ(detect_double_tap(false, &window), false); // idle ticks decay it
	ASSERT_EQ(window, 0);                               // window has expired
	ASSERT_EQ(detect_double_tap(true, &window), false); // fresh tap, no toggle
	ASSERT(window > 0);                                 // and a new window opens
}

const test_entry_t g_tests_double_tap[] = {
	{"double_tap_second_press_toggles", test_double_tap_second_press_toggles},
	{"double_tap_expires_no_toggle", test_double_tap_expires_no_toggle},
};

const size_t g_tests_double_tap_count
	= sizeof(g_tests_double_tap) / sizeof(g_tests_double_tap[0]);
