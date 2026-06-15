#include <stdbool.h>

#include "block/blocks_data.h"
#include "chunk.h"
#include "chunk_mesher.h"
#include "platform/displaylist.h"
#include "world.h"

struct chunk* world_find_chunk(struct world* w, w_coord_t x, w_coord_t y,
							   w_coord_t z) {
	(void)w;
	(void)x;
	(void)y;
	(void)z;
	return NULL;
}

void displaylist_destroy(struct displaylist* dl) {
	(void)dl;
}

bool chunk_mesher_send(struct chunk* c) {
	(void)c;
	return false;
}
