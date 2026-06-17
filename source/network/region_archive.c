/*
	Copyright (c) 2022 ByteBit/xtreme8000

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

#include <assert.h>
#include <m-lib/m-string.h>

#include "../cNBT/nbt.h"
#include "../util.h"

#include "region_archive.h"
#include "server_world.h"

#define CHUNK_EXISTS(offset, sectors) ((offset) >= 2 && (sectors) >= 1)

// Beta 1.7.3 / McRegion height of every save written before issue #26 bumped
// the PC engine to a taller world. Chunk block/light/data NBT arrays are laid
// out XZY with a per-column stride equal to the world height, so a save written
// at this height has Blocks = CHUNK_SIZE*CHUNK_SIZE*LEGACY_WORLD_HEIGHT bytes
// (and the nibble arrays at half that). See region_archive_migrate_legacy().
#define LEGACY_WORLD_HEIGHT 128

static uint32_t conv_u32_native(uint8_t* data) {
	return (data[0] << 24) | (data[1] << 16) | (data[2] << 8) | data[3];
}

static void conv_native_u32(uint32_t in, uint8_t* data) {
	data[0] = in >> 24;
	data[1] = (in >> 16) & 0xFF;
	data[2] = (in >> 8) & 0xFF;
	data[3] = in & 0xFF;
}

static size_t fread_u32(uint32_t* out, FILE* f) {
	size_t res = fread(out, sizeof(uint32_t), 1, f);
	*out = conv_u32_native((uint8_t*)out);
	return res;
}

static size_t fwrite_u32(uint32_t in, FILE* f) {
	uint8_t tmp[sizeof(uint32_t)];
	conv_native_u32(in, tmp);

	return fwrite(tmp, sizeof(uint32_t), 1, f);
}

static int sort_region_chunks(const void* a, const void* b) {
	uint32_t offset_a = (*(const uint32_t*)a) >> 8;
	uint32_t offset_b = (*(const uint32_t*)b) >> 8;
	return offset_a - offset_b;
}

static bool rebuild_occupied_list(struct region_archive* ra) {
	assert(ra);

	ra->occupied_index = 0;
	ra->occupied_sorted[ra->occupied_index++] = (0 << 8) | 2;

	for(size_t k = 0; k < REGION_SIZE * REGION_SIZE; k++) {
		uint32_t offset = ra->offsets[k] >> 8;
		uint32_t sectors = ra->offsets[k] & 0xFF;

		if(CHUNK_EXISTS(offset, sectors))
			ra->occupied_sorted[ra->occupied_index++] = ra->offsets[k];
	}

	qsort(ra->occupied_sorted, ra->occupied_index, sizeof(uint32_t),
		  sort_region_chunks);

	uint32_t prev = 1;
	for(size_t k = 1; k < ra->occupied_index; k++) {
		uint32_t offset = ra->occupied_sorted[k] >> 8;

		if(offset <= prev)
			return false;

		prev = offset;
	}

	return true;
}

bool region_archive_create_new(struct region_archive* ra, string_t world_name,
							   w_coord_t x, w_coord_t z,
							   enum world_dim dimension) {
	assert(ra && world_name);

	if(dimension == WORLD_DIM_OVERWORLD) {
		string_init_printf(ra->file_name, "%s/region/r.%i.%i.mcr",
						   string_get_cstr(world_name), x, z);
	} else {
		string_init_printf(ra->file_name, "%s/DIM-1/region/r.%i.%i.mcr",
						   string_get_cstr(world_name), x, z);
	}

	FILE* f = fopen(string_get_cstr(ra->file_name), "w");

	if(!f) {
		string_clear(ra->file_name);
		return false;
	}

	for(size_t k = 0; k < REGION_SIZE * REGION_SIZE * 2; k++)
		fwrite((uint32_t[]) {0}, sizeof(uint32_t), 1, f);

	fclose(f);
	string_clear(ra->file_name);

	return region_archive_create(ra, world_name, x, z, dimension);
}

bool region_archive_create(struct region_archive* ra, string_t world_name,
						   w_coord_t x, w_coord_t z, enum world_dim dimension) {
	assert(ra && world_name);

	ra->offsets = malloc(sizeof(uint32_t) * REGION_SIZE * REGION_SIZE);

	if(!ra->offsets)
		return false;

	ra->occupied_sorted
		= malloc(sizeof(uint32_t) * (REGION_SIZE * REGION_SIZE + 1));

	if(!ra->occupied_sorted) {
		free(ra->offsets);
		return false;
	}

	if(dimension == WORLD_DIM_OVERWORLD) {
		string_init_printf(ra->file_name, "%s/region/r.%i.%i.mcr",
						   string_get_cstr(world_name), x, z);
	} else {
		string_init_printf(ra->file_name, "%s/DIM-1/region/r.%i.%i.mcr",
						   string_get_cstr(world_name), x, z);
	}

	ra->x = x;
	ra->z = z;

	FILE* f = fopen(string_get_cstr(ra->file_name), "rb");

	if(!f) {
		free(ra->offsets);
		string_clear(ra->file_name);
		return false;
	}

	if(!fread(ra->offsets, sizeof(uint32_t) * REGION_SIZE * REGION_SIZE, 1,
			  f)) {
		free(ra->offsets);
		fclose(f);
		string_clear(ra->file_name);
		return false;
	}

	// convert from big endian to native endian
	for(size_t k = 0; k < REGION_SIZE * REGION_SIZE; k++)
		ra->offsets[k] = conv_u32_native((uint8_t*)(ra->offsets + k));

	fclose(f);

	ilist_regions_init_field(ra);

	if(!rebuild_occupied_list(ra)) {
		free(ra->offsets);
		free(ra->occupied_sorted);
		string_clear(ra->file_name);
		return false;
	}

	return true;
}

void region_archive_destroy(struct region_archive* ra) {
	assert(ra && ra->offsets && ra->occupied_sorted);

	free(ra->offsets);
	free(ra->occupied_sorted);
	string_clear(ra->file_name);
}

bool region_archive_contains(struct region_archive* ra, w_coord_t x,
							 w_coord_t z, bool* chunk_exists) {
	assert(ra && chunk_exists);

	if(CHUNK_REGION_COORD(x) != ra->x || CHUNK_REGION_COORD(z) != ra->z) {
		*chunk_exists = false;
		return false;
	}

	int rx = x & (REGION_SIZE - 1);
	int rz = z & (REGION_SIZE - 1);

	uint32_t offset = ra->offsets[rx + rz * REGION_SIZE] >> 8;
	uint32_t sectors = ra->offsets[rx + rz * REGION_SIZE] & 0xFF;

	*chunk_exists = CHUNK_EXISTS(offset, sectors);
	return true;
}

// Load-time migration for saves written before issue #26 raised WORLD_HEIGHT.
//
// A LEGACY_WORLD_HEIGHT-tall chunk stores its Blocks/Data/Light arrays XZY with
// a per-column stride of LEGACY_WORLD_HEIGHT; the running engine expects a
// stride of WORLD_HEIGHT. This rebuilds each of the CHUNK_SIZE*CHUNK_SIZE
// columns into freshly allocated WORLD_HEIGHT-tall arrays:
//   - the bottom LEGACY_WORLD_HEIGHT cells are copied verbatim,
//   - the new cells above are AIR (0) for blocks/metadata/block-light,
//   - the new cells above are full sky (0xF) for sky-light so the freshly
//     exposed upper world is not pitch black,
//   - HeightMap is one byte per column and is height-independent, so it is
//     copied unchanged.
// On success the borrowed legacy arrays are freed and *sc points at the new
// WORLD_HEIGHT-tall arrays. The chunk is flagged modified so it is rewritten in
// the current (taller) layout on the next save, completing the upgrade on disk.
//
// Returns false (leaving *sc untouched, caller still owns the legacy arrays) on
// allocation failure.
static bool region_archive_migrate_legacy(struct server_chunk* sc) {
	assert(sc && sc->ids && sc->metadata && sc->lighting_sky
		   && sc->lighting_torch && sc->heightmap);

	const size_t columns = CHUNK_SIZE * CHUNK_SIZE;
	const size_t new_blocks = columns * WORLD_HEIGHT;
	const size_t new_nibbles = new_blocks / 2;

	uint8_t* ids = calloc(1, new_blocks);
	uint8_t* metadata = calloc(1, new_nibbles);
	uint8_t* lighting_sky = calloc(1, new_nibbles);
	uint8_t* lighting_torch = calloc(1, new_nibbles);

	if(!ids || !metadata || !lighting_sky || !lighting_torch) {
		free(ids);
		free(metadata);
		free(lighting_sky);
		free(lighting_torch);
		return false;
	}

	for(size_t c = 0; c < columns; c++) {
		size_t old_base = c * LEGACY_WORLD_HEIGHT;
		size_t new_base = c * WORLD_HEIGHT;

		for(size_t y = 0; y < LEGACY_WORLD_HEIGHT; y++) {
			ids[new_base + y] = sc->ids[old_base + y];
			nibble_write(metadata, new_base + y,
						 nibble_read(sc->metadata, old_base + y));
			nibble_write(lighting_sky, new_base + y,
						 nibble_read(sc->lighting_sky, old_base + y));
			nibble_write(lighting_torch, new_base + y,
						 nibble_read(sc->lighting_torch, old_base + y));
		}

		// calloc already zeroed blocks/metadata/torch-light above the legacy
		// ceiling; the newly exposed cells need full sky light so the upper
		// world renders lit rather than black.
		for(size_t y = LEGACY_WORLD_HEIGHT; y < WORLD_HEIGHT; y++)
			nibble_write(lighting_sky, new_base + y, 0xF);
	}

	free(sc->ids);
	free(sc->metadata);
	free(sc->lighting_sky);
	free(sc->lighting_torch);

	sc->ids = ids;
	sc->metadata = metadata;
	sc->lighting_sky = lighting_sky;
	sc->lighting_torch = lighting_torch;
	// heightmap is one byte per column, height-independent; keep as-is.
	sc->modified = true;

	return true;
}

bool region_archive_get_blocks(struct region_archive* ra, w_coord_t x,
							   w_coord_t z, struct server_chunk* sc) {
	assert(ra && sc);
	bool chunk_exists;
	assert(region_archive_contains(ra, x, z, &chunk_exists) && chunk_exists);

	int rx = x & (REGION_SIZE - 1);
	int rz = z & (REGION_SIZE - 1);

	uint32_t offset = ra->offsets[rx + rz * REGION_SIZE] >> 8;
	uint32_t sectors = ra->offsets[rx + rz * REGION_SIZE] & 0xFF;

	// TODO: little endian

	FILE* f = fopen(string_get_cstr(ra->file_name), "rb");

	if(!f)
		return false;

	if(fseek(f, offset * REGION_SECTOR_SIZE, SEEK_SET) != 0) {
		fclose(f);
		return false;
	}

	uint32_t length;
	if(!fread_u32(&length, f)
	   || length + sizeof(uint32_t) > sectors * REGION_SECTOR_SIZE) {
		fclose(f);
		return false;
	}

	uint8_t type;
	if(!fread(&type, sizeof(uint8_t), 1, f) || type > 3) {
		fclose(f);
		return false;
	}

	void* nbt_compressed = malloc(length - 1);

	if(!nbt_compressed) {
		fclose(f);
		return false;
	}

	if(!fread(nbt_compressed, length - 1, 1, f)) {
		free(nbt_compressed);
		fclose(f);
		return false;
	}

	nbt_node* chunk = nbt_parse_compressed(nbt_compressed, length - 1);

	free(nbt_compressed);
	fclose(f);

	if(!chunk)
		return false;

	nbt_node* n_x = nbt_find_by_path(chunk, ".Level.xPos");
	nbt_node* n_z = nbt_find_by_path(chunk, ".Level.zPos");

	if(!n_x || !n_z || n_x->type != TAG_INT || n_z->type != TAG_INT
	   || n_x->payload.tag_int != x || n_z->payload.tag_int != z) {
		nbt_free(chunk);
		return false;
	}

	nbt_node* n_blocks = nbt_find_by_path(chunk, ".Level.Blocks");
	nbt_node* n_metadata = nbt_find_by_path(chunk, ".Level.Data");
	nbt_node* n_skyl = nbt_find_by_path(chunk, ".Level.SkyLight");
	nbt_node* n_torchl = nbt_find_by_path(chunk, ".Level.BlockLight");
	nbt_node* n_height = nbt_find_by_path(chunk, ".Level.HeightMap");

	if(!n_blocks || !n_metadata || !n_skyl || !n_torchl || !n_height
	   || n_blocks->type != TAG_BYTE_ARRAY || n_metadata->type != TAG_BYTE_ARRAY
	   || n_skyl->type != TAG_BYTE_ARRAY || n_torchl->type != TAG_BYTE_ARRAY
	   || n_height->type != TAG_BYTE_ARRAY) {
		nbt_free(chunk);
		return false;
	}

	// Accept the current WORLD_HEIGHT layout and, when the engine is taller
	// than the original world (issue #26 bumped PC to 256), the LEGACY_WORLD_HEIGHT
	// layout too. Both Blocks and the three nibble arrays must agree on one of
	// those two heights; HeightMap is always one byte per column.
	int32_t blocks_len = n_blocks->payload.tag_byte_array.length;
	int height = (blocks_len == CHUNK_SIZE * CHUNK_SIZE * WORLD_HEIGHT) ?
		WORLD_HEIGHT :
		(blocks_len == CHUNK_SIZE * CHUNK_SIZE * LEGACY_WORLD_HEIGHT) ?
		LEGACY_WORLD_HEIGHT :
		0;

	if(height == 0
	   || n_metadata->payload.tag_byte_array.length
		   != CHUNK_SIZE * CHUNK_SIZE * height / 2
	   || n_skyl->payload.tag_byte_array.length
		   != CHUNK_SIZE * CHUNK_SIZE * height / 2
	   || n_torchl->payload.tag_byte_array.length
		   != CHUNK_SIZE * CHUNK_SIZE * height / 2
	   || n_height->payload.tag_byte_array.length != CHUNK_SIZE * CHUNK_SIZE) {
		nbt_free(chunk);
		return false;
	}

	// borrow memory regions from nbt tree

	n_blocks->type = TAG_INVALID;
	n_metadata->type = TAG_INVALID;
	n_skyl->type = TAG_INVALID;
	n_torchl->type = TAG_INVALID;
	n_height->type = TAG_INVALID;

	sc->ids = n_blocks->payload.tag_byte_array.data;
	sc->metadata = n_metadata->payload.tag_byte_array.data;
	sc->lighting_sky = n_skyl->payload.tag_byte_array.data;
	sc->lighting_torch = n_torchl->payload.tag_byte_array.data;
	sc->heightmap = n_height->payload.tag_byte_array.data;

	nbt_free(chunk);

	// A legacy-height chunk uses a shorter per-column stride than the running
	// engine; rebuild it into the current WORLD_HEIGHT layout in memory. (When
	// WORLD_HEIGHT == LEGACY_WORLD_HEIGHT, e.g. the Wii build, this branch is
	// never taken and the arrays are already native.)
	if(height != WORLD_HEIGHT) {
		if(!region_archive_migrate_legacy(sc)) {
			// migration left the borrowed legacy arrays in place; free them so
			// the failed load leaks nothing.
			free(sc->ids);
			free(sc->metadata);
			free(sc->lighting_sky);
			free(sc->lighting_torch);
			free(sc->heightmap);
			return false;
		}
	}

	return true;
}

static bool file_overwrite_index(FILE* f, size_t index, uint32_t data) {
	assert(f);

	if(fseek(f, index * sizeof(uint32_t), SEEK_SET)) {
		fclose(f);
		return false;
	}

	if(fwrite_u32(data, f) != 1) {
		fclose(f);
		return false;
	}

	return true;
}

static bool file_overwrite_chunk(FILE* f, size_t offset, void* data,
								 size_t length, bool pad) {
	assert(f && data && length > 0);

	if(fseek(f, offset, SEEK_SET)) {
		fclose(f);
		return false;
	}

	if(fwrite_u32(length + 1, f) != 1) {
		fclose(f);
		return false;
	}

	if(fwrite((uint8_t[]) {2}, sizeof(uint8_t), 1, f) != 1) {
		fclose(f);
		return false;
	}

	if(fwrite(data, length, 1, f) != 1) {
		fclose(f);
		return false;
	}

	size_t sectors = (length + REGION_SECTOR_SIZE - 1) / REGION_SECTOR_SIZE
		* REGION_SECTOR_SIZE;

	if(pad && sectors > length + 5) {
		for(size_t k = 0; k < sectors - length - 5; k++) {
			if(fwrite((uint8_t[]) {0}, sizeof(uint8_t), 1, f) != 1) {
				fclose(f);
				return false;
			}
		}
	}

	return true;
}

bool region_archive_set_blocks(struct region_archive* ra, w_coord_t x,
							   w_coord_t z, struct server_chunk* sc) {
	assert(ra && sc);
	assert(CHUNK_REGION_COORD(x) == ra->x && CHUNK_REGION_COORD(z) == ra->z);

	FILE* f = fopen(string_get_cstr(ra->file_name), "rb+");

	// early exit
	if(!f)
		return false;

	struct nbt_list root_list_sentinel = (struct nbt_list) {
		.data = NULL,
	};
	struct nbt_list level_list_sentinel = (struct nbt_list) {
		.data = NULL,
	};
	struct nbt_list empty_list_sentinel = (struct nbt_list) {
		.data = &(nbt_node) {.type = TAG_COMPOUND},
	};

	INIT_LIST_HEAD(&root_list_sentinel.entry);
	INIT_LIST_HEAD(&level_list_sentinel.entry);
	INIT_LIST_HEAD(&empty_list_sentinel.entry);

	nbt_node root = (nbt_node) {
		.type = TAG_COMPOUND,
		.name = "",
		.payload.tag_compound = &root_list_sentinel,
	};

	nbt_node level = (nbt_node) {
		.type = TAG_COMPOUND,
		.name = "Level",
		.payload.tag_compound = &level_list_sentinel,
	};

	struct nbt_list root_list = (struct nbt_list) {
		.data = &level,
	};

	list_add_head(&root_list.entry, &root_list_sentinel.entry);

	nbt_node level_list_nodes[] = {
		{
			.type = TAG_BYTE_ARRAY,
			.name = "Blocks",
			.payload.tag_byte_array.data = sc->ids,
			.payload.tag_byte_array.length
			= CHUNK_SIZE * CHUNK_SIZE * WORLD_HEIGHT,
		},
		{
			.type = TAG_BYTE_ARRAY,
			.name = "Data",
			.payload.tag_byte_array.data = sc->metadata,
			.payload.tag_byte_array.length
			= CHUNK_SIZE * CHUNK_SIZE * WORLD_HEIGHT / 2,
		},
		{
			.type = TAG_BYTE_ARRAY,
			.name = "SkyLight",
			.payload.tag_byte_array.data = sc->lighting_sky,
			.payload.tag_byte_array.length
			= CHUNK_SIZE * CHUNK_SIZE * WORLD_HEIGHT / 2,
		},
		{
			.type = TAG_BYTE_ARRAY,
			.name = "BlockLight",
			.payload.tag_byte_array.data = sc->lighting_torch,
			.payload.tag_byte_array.length
			= CHUNK_SIZE * CHUNK_SIZE * WORLD_HEIGHT / 2,
		},
		{
			.type = TAG_BYTE_ARRAY,
			.name = "HeightMap",
			.payload.tag_byte_array.data = sc->heightmap,
			.payload.tag_byte_array.length = CHUNK_SIZE * CHUNK_SIZE,
		},
		{
			.type = TAG_LIST,
			.name = "Entities",
			.payload.tag_list = &empty_list_sentinel,
		},
		{
			.type = TAG_LIST,
			.name = "TileEntities",
			.payload.tag_list = &empty_list_sentinel,
		},
		{.type = TAG_LONG, .name = "LastUpdate", .payload.tag_long = 0},
		{.type = TAG_INT, .name = "xPos", .payload.tag_int = x},
		{.type = TAG_INT, .name = "zPos", .payload.tag_int = z},
		{.type = TAG_BYTE, .name = "TerrainPopulated", .payload.tag_byte = 1},
	};

	struct nbt_list
		level_list[sizeof(level_list_nodes) / sizeof(*level_list_nodes)];

	for(size_t k = 0; k < sizeof(level_list) / sizeof(*level_list); k++) {
		level_list[k].data = level_list_nodes + k;
		list_add_tail(&level_list[k].entry, &level_list_sentinel.entry);
	}

	struct buffer res = nbt_dump_compressed(&root, STRAT_INFLATE);

	if(!res.data)
		return false;

	uint32_t new_data_sectors = (res.len + sizeof(uint32_t) + sizeof(uint8_t)
								 + REGION_SECTOR_SIZE - 1)
		/ REGION_SECTOR_SIZE;

	int rx = x & (REGION_SIZE - 1);
	int rz = z & (REGION_SIZE - 1);

	uint32_t offset = ra->offsets[rx + rz * REGION_SIZE] >> 8;
	uint32_t sectors = ra->offsets[rx + rz * REGION_SIZE] & 0xFF;

	bool success = true;

	if(CHUNK_EXISTS(offset, sectors) && new_data_sectors <= sectors) {
		// chunk already exists in file and existing space fits new data
		uint32_t data = (offset << 8) | new_data_sectors;
		ra->offsets[rx + rz * REGION_SIZE] = data;

		if(success && sectors != new_data_sectors
		   && !file_overwrite_index(f, rx + rz * REGION_SIZE, data))
			success = false;

		if(success
		   && !file_overwrite_chunk(f, offset * REGION_SECTOR_SIZE, res.data,
									res.len, false))
			success = false;

	} else {
		/* append new data at end or insert it in between existing chunks where
		 * there is enough space left */
		uint32_t new_offset = 0;
		bool pad = false;

		for(size_t k = 0; k < ra->occupied_index; k++) {
			uint32_t off1 = ra->occupied_sorted[k] >> 8;
			uint32_t sec1 = ra->occupied_sorted[k] & 0xFF;

			// append at end
			if(k + 1 >= ra->occupied_index) {
				new_offset = off1 + sec1;
				pad = true; // mc requires files to be multiples of 4KiB
				break;
			}

			uint32_t off2 = ra->occupied_sorted[k + 1] >> 8;

			// insert in between?
			if(off2 - (off1 + sec1) >= new_data_sectors) {
				new_offset = off1 + sec1;
				pad = false;
				break;
			}
		}

		// sanity check, don't overwrite lookup tables at start of file
		if(new_offset > 0) {
			uint32_t data = (new_offset << 8) | new_data_sectors;
			ra->offsets[rx + rz * REGION_SIZE] = data;

			if(success && !file_overwrite_index(f, rx + rz * REGION_SIZE, data))
				success = false;

			if(success
			   && !file_overwrite_chunk(f, new_offset * REGION_SECTOR_SIZE,
										res.data, res.len, pad))
				success = false;
		} else {
			success = false;
		}
	}

	// this could be done without resorting the entire list
	if(success)
		success = rebuild_occupied_list(ra);

	fclose(f);
	buffer_free(&res);
	return success;
}
