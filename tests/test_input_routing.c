// Unit tests for the pure input-routing map behind local split-screen 2-player
// (#23). input_config_key(button, device) is the single seam that turns a virtual
// button + device index into a config key; device 0 must reproduce the original
// single-player bindings exactly (so single-player is unchanged) and device 1
// must map to the "player2_*" set. Buttons a device does not use return NULL.

#include <string.h>

#include "harness.h"
#include "platform/input.h"

// Helper: true when key() returns exactly the expected string (and not NULL).
static int key_is(enum input_button b, int device, const char* expect) {
	const char* k = input_config_key(b, device);
	return k && strcmp(k, expect) == 0;
}

// Device 0 reproduces the historical single-player binding names verbatim.
TEST(input_routing_device0_matches_player1) {
	ASSERT(key_is(IB_FORWARD, 0, "input.player_forward"));
	ASSERT(key_is(IB_BACKWARD, 0, "input.player_backward"));
	ASSERT(key_is(IB_LEFT, 0, "input.player_left"));
	ASSERT(key_is(IB_RIGHT, 0, "input.player_right"));
	ASSERT(key_is(IB_JUMP, 0, "input.player_jump"));
	ASSERT(key_is(IB_SNEAK, 0, "input.player_sneak"));
	ASSERT(key_is(IB_ACTION1, 0, "input.item_action_left"));
	ASSERT(key_is(IB_ACTION2, 0, "input.item_action_right"));
	ASSERT(key_is(IB_INVENTORY, 0, "input.inventory"));
	ASSERT(key_is(IB_SCREENSHOT, 0, "input.screenshot"));
	ASSERT(key_is(IB_TOGGLE_CREATIVE, 0, "input.toggle_creative"));
}

// Device 1 maps the gameplay buttons to the player-2 binding set.
TEST(input_routing_device1_is_player2) {
	ASSERT(key_is(IB_FORWARD, 1, "input.player2_forward"));
	ASSERT(key_is(IB_BACKWARD, 1, "input.player2_backward"));
	ASSERT(key_is(IB_LEFT, 1, "input.player2_left"));
	ASSERT(key_is(IB_RIGHT, 1, "input.player2_right"));
	ASSERT(key_is(IB_JUMP, 1, "input.player2_jump"));
	ASSERT(key_is(IB_SNEAK, 1, "input.player2_sneak"));
	ASSERT(key_is(IB_ACTION1, 1, "input.player2_action_left"));
	ASSERT(key_is(IB_ACTION2, 1, "input.player2_action_right"));
}

// Look-keys: player 1 looks with the mouse, but the discrete look-keys now
// resolve (device 0) so a gamepad's face buttons can ADD to that delta; they are
// simply left empty in config for a keyboard+mouse player. Player 2
// (keyboard/pad) gets its own discrete look-key binding on device 1.
TEST(input_routing_look_keys) {
	ASSERT(key_is(IB_LOOK_UP, 0, "input.player_look_up"));
	ASSERT(key_is(IB_LOOK_DOWN, 0, "input.player_look_down"));
	ASSERT(key_is(IB_LOOK_LEFT, 0, "input.player_look_left"));
	ASSERT(key_is(IB_LOOK_RIGHT, 0, "input.player_look_right"));

	ASSERT(key_is(IB_LOOK_UP, 1, "input.player2_look_up"));
	ASSERT(key_is(IB_LOOK_DOWN, 1, "input.player2_look_down"));
	ASSERT(key_is(IB_LOOK_LEFT, 1, "input.player2_look_left"));
	ASSERT(key_is(IB_LOOK_RIGHT, 1, "input.player2_look_right"));
}

// Player 2 owns its inventory/GUI keys (issue #139) so it can drive its own
// screens; world-level buttons (save&quit, map, creative, screenshot) stay
// player-1 only so a shared keyboard cannot fire them from P2's cluster.
TEST(input_routing_player2_menu_split) {
	ASSERT(key_is(IB_INVENTORY, 1, "input.player2_inventory"));
	ASSERT(key_is(IB_SCROLL_LEFT, 1, "input.player2_scroll_left"));
	ASSERT(key_is(IB_SCROLL_RIGHT, 1, "input.player2_scroll_right"));
	ASSERT(key_is(IB_GUI_UP, 1, "input.player2_gui_up"));
	ASSERT(key_is(IB_GUI_DOWN, 1, "input.player2_gui_down"));
	ASSERT(key_is(IB_GUI_LEFT, 1, "input.player2_gui_left"));
	ASSERT(key_is(IB_GUI_RIGHT, 1, "input.player2_gui_right"));
	ASSERT(key_is(IB_GUI_CLICK, 1, "input.player2_gui_click"));
	ASSERT(key_is(IB_GUI_CLICK_ALT, 1, "input.player2_gui_click_alt"));

	ASSERT(input_config_key(IB_HOME, 1) == NULL);
	ASSERT(input_config_key(IB_SCREENSHOT, 1) == NULL);
	ASSERT(input_config_key(IB_MAP, 1) == NULL);
	ASSERT(input_config_key(IB_TOGGLE_CREATIVE, 1) == NULL);
}

const test_entry_t g_tests_input_routing[] = {
	{"input_routing_device0_matches_player1",
	 test_input_routing_device0_matches_player1},
	{"input_routing_device1_is_player2", test_input_routing_device1_is_player2},
	{"input_routing_look_keys", test_input_routing_look_keys},
	{"input_routing_player2_menu_split",
	 test_input_routing_player2_menu_split},
};

const size_t g_tests_input_routing_count
	= sizeof(g_tests_input_routing) / sizeof(g_tests_input_routing[0]);
