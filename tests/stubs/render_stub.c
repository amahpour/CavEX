/*
	Inert stubs for the address-taken render/atlas/placement symbols that real
	block definition files (e.g. block_candle.c, block_bubble_column.c) reference
	in their struct block initializers. The unit test harness links the real
	block .c files for coverage but does NOT pull in the GX-bound graphics units,
	so these symbols are provided here as no-ops. They are never called by the
	registry tests; only their addresses are taken at static-init time.
*/

#include "block/blocks.h"
#include "graphics/texture_atlas.h"
#include "graphics/render_block.h"
#include "graphics/render_item.h"

size_t render_block_full(struct displaylist* d, struct block_info* this,
						 enum side side, struct block_info* it,
						 uint8_t* vertex_light, bool count_only) {
	(void)d;
	(void)this;
	(void)side;
	(void)it;
	(void)vertex_light;
	(void)count_only;
	return 0;
}

void render_item_block(struct item* item, struct item_data* stack, mat4 view,
					   bool fullbright, enum render_item_env env) {
	(void)item;
	(void)stack;
	(void)view;
	(void)fullbright;
	(void)env;
}

bool block_place_default(struct server_local* s, struct item_data* it,
						 struct block_info* where, struct block_info* on,
						 enum side on_side) {
	(void)s;
	(void)it;
	(void)where;
	(void)on;
	(void)on_side;
	return false;
}

uint8_t tex_atlas_lookup(enum tex_atlas_entry name) {
	(void)name;
	return 0;
}
