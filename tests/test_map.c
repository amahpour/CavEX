// Unit tests for the pure world<->screen projection behind the clickable map
// screen (#35). The projection has no engine/GL state, so it can be exercised
// directly the way detect_double_tap is for creative flight.
//
// Each test targets a distinct code path of map_projection.c so every one
// contributes newly-covered lines to the per-test coverage gate.

#include "game/gui/map_projection.h"
#include "harness.h"

// A 5x5 grid of 10px cells centred at world column (100, 200) inside a 200x100
// window: the grid spans 50px, so it is centred horizontally at x=75 and
// vertically at y=25.
static struct map_projection make_fixture(void) {
	return map_projection_make(100, 200, 5, 10, 200, 100);
}

// map_projection_make: the grid is centred in the window and its geometry is
// reported back exactly.
TEST(map_projection_centred) {
	struct map_projection p = make_fixture();
	ASSERT_EQ(p.cells, 5);
	ASSERT_EQ(p.cell_px, 10);
	ASSERT_EQ(p.center_x, 100);
	ASSERT_EQ(p.center_z, 200);
	ASSERT_EQ(p.origin_x, 75); // (200 - 50) / 2
	ASSERT_EQ(p.origin_y, 25); // (100 - 50) / 2
}

// map_cell_to_world + map_cell_to_pixel: the centre cell maps to the player's
// column at the centre of the grid, and the axes are oriented so +i is east
// (+world X) and +j is south (+world Z).
TEST(map_cell_mapping) {
	struct map_projection p = make_fixture();
	int wx, wz, px, py;

	// centre cell (half = 5/2 = 2)
	map_cell_to_world(&p, 2, 2, &wx, &wz);
	ASSERT_EQ(wx, 100);
	ASSERT_EQ(wz, 200);

	map_cell_to_pixel(&p, 2, 2, &px, &py);
	ASSERT_EQ(px, 95); // 75 + 2*10
	ASSERT_EQ(py, 45); // 25 + 2*10

	// top-left and bottom-right corners pin the axis orientation
	map_cell_to_world(&p, 0, 0, &wx, &wz);
	ASSERT_EQ(wx, 98);  // 100 - 2 (west)
	ASSERT_EQ(wz, 198); // 200 - 2 (north)

	map_cell_to_world(&p, 4, 4, &wx, &wz);
	ASSERT_EQ(wx, 102); // 100 + 2 (east)
	ASSERT_EQ(wz, 202); // 200 + 2 (south)

	map_cell_to_pixel(&p, 0, 0, &px, &py);
	ASSERT_EQ(px, 75); // grid origin
	ASSERT_EQ(py, 25);
}

// map_pixel_to_world success path: pixels inside a cell map back to that cell's
// world column, including the cell boundaries.
TEST(map_pixel_to_world_inside) {
	struct map_projection p = make_fixture();
	int wx, wz;

	// top-left pixel of the centre cell
	ASSERT(map_pixel_to_world(&p, 95, 45, &wx, &wz));
	ASSERT_EQ(wx, 100);
	ASSERT_EQ(wz, 200);

	// last pixel still inside the centre cell (origin + cell_px - 1)
	ASSERT(map_pixel_to_world(&p, 104, 54, &wx, &wz));
	ASSERT_EQ(wx, 100);
	ASSERT_EQ(wz, 200);

	// first pixel of the next cell to the east/south
	ASSERT(map_pixel_to_world(&p, 105, 55, &wx, &wz));
	ASSERT_EQ(wx, 101);
	ASSERT_EQ(wz, 201);

	// far corners of the grid
	ASSERT(map_pixel_to_world(&p, 75, 25, &wx, &wz)); // grid top-left
	ASSERT_EQ(wx, 98);
	ASSERT_EQ(wz, 198);
}

// map_pixel_to_world rejection path: pixels outside the grid return false (so
// background clicks do nothing) and must not modify the outputs.
TEST(map_pixel_to_world_outside) {
	struct map_projection p = make_fixture();
	int wx = -7, wz = -7;

	ASSERT(!map_pixel_to_world(&p, 0, 0, &wx, &wz));    // top-left of window
	ASSERT(!map_pixel_to_world(&p, 74, 45, &wx, &wz));  // just left of grid
	ASSERT(!map_pixel_to_world(&p, 125, 45, &wx, &wz)); // just right of grid
	ASSERT(!map_pixel_to_world(&p, 95, 24, &wx, &wz));  // just above grid
	ASSERT(!map_pixel_to_world(&p, 95, 75, &wx, &wz));  // just below grid

	ASSERT_EQ(wx, -7); // untouched on rejection
	ASSERT_EQ(wz, -7);
}

const test_entry_t g_tests_map[] = {
	{"map_projection_centred", test_map_projection_centred},
	{"map_cell_mapping", test_map_cell_mapping},
	{"map_pixel_to_world_inside", test_map_pixel_to_world_inside},
	{"map_pixel_to_world_outside", test_map_pixel_to_world_outside},
};

const size_t g_tests_map_count = sizeof(g_tests_map) / sizeof(g_tests_map[0]);
