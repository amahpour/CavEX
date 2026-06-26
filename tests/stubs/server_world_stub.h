#ifndef SERVER_WORLD_STUB_H
#define SERVER_WORLD_STUB_H

#include <stdint.h>

#include "world.h"  // w_coord_t

// Reset the fake world to all-air. Call at the start of each test.
void test_server_world_reset(void);

// Seed / read a single cell's block type in the fake world window.
void test_server_world_set(w_coord_t x, w_coord_t y, w_coord_t z, uint8_t type);
uint8_t test_server_world_get(w_coord_t x, w_coord_t y, w_coord_t z);

#endif
