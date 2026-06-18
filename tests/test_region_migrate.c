/*
	Tests for the load-time 128 -> 256 chunk migration in region_archive.c.

	Background: issue #26 raised the PC engine's WORLD_HEIGHT from 128 to 256.
	The chunk read path then required Blocks to be CHUNK_SIZE*CHUNK_SIZE*256 =
	65536 bytes, so every pre-#26 save (32768-byte, 128-tall Blocks) was
	rejected, the chunk failed to load, and the player was reset to (0,0,0).

	region_archive_get_blocks() now accepts a legacy 128-tall chunk and migrates
	it in memory to the 256-tall XZY layout the engine expects. These tests build
	a real McRegion file containing one synthetic 128-tall chunk with known
	blocks/light at known coordinates (including the top legacy cell y=127), read
	it back through the production path, and prove the migration is correct.
*/

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "cNBT/nbt.h"
#include "harness.h"
#include "network/region_archive.h"
#include "network/server_world.h"
#include "util.h"
#include "world.h"

#define LEGACY_HEIGHT 128
#define COLUMNS (CHUNK_SIZE * CHUNK_SIZE)

#define LEGACY_BLOCKS (COLUMNS * LEGACY_HEIGHT)
#define LEGACY_NIBBLES (LEGACY_BLOCKS / 2)

#define NEW_BLOCKS (COLUMNS * WORLD_HEIGHT)
#define NEW_NIBBLES (NEW_BLOCKS / 2)

#define HEIGHT_LEN (CHUNK_SIZE * CHUNK_SIZE)

#define REGION_SECTOR 4096

// get_blocks() borrows the arrays out of the NBT tree (and the migration path
// allocates fresh ones); either way the five buffers are plain malloc'd and the
// caller owns them. server_world.c isn't in the test link, so free here.
static void free_chunk(struct server_chunk* sc) {
	free(sc->ids);
	free(sc->metadata);
	free(sc->lighting_sky);
	free(sc->lighting_torch);
	free(sc->heightmap);
}

// XZY column-major indices; per-column stride is the world height.
static size_t idx_legacy(int x, int y, int z) {
	return (size_t)y + ((size_t)z + (size_t)x * CHUNK_SIZE) * LEGACY_HEIGHT;
}

static size_t idx_new(int x, int y, int z) {
	return (size_t)y + ((size_t)z + (size_t)x * CHUNK_SIZE) * WORLD_HEIGHT;
}

static void store_u32_be(uint8_t* p, uint32_t v) {
	p[0] = (v >> 24) & 0xFF;
	p[1] = (v >> 16) & 0xFF;
	p[2] = (v >> 8) & 0xFF;
	p[3] = v & 0xFF;
}

// Build a compressed Beta chunk NBT for a 128-tall world. Mirrors the on-disk
// shape produced by region_archive_set_blocks(), but with legacy-height arrays
// so we can exercise the migration. Caller frees the returned buffer's data.
static struct buffer build_legacy_chunk_nbt(int x, int z, const uint8_t* blocks,
											 const uint8_t* data,
											 const uint8_t* skyl,
											 const uint8_t* blockl,
											 const uint8_t* hmap) {
	struct nbt_list root_list_sentinel = (struct nbt_list) {.data = NULL};
	struct nbt_list level_list_sentinel = (struct nbt_list) {.data = NULL};
	struct nbt_list empty_list_sentinel = (struct nbt_list) {
		.data = &(nbt_node) {.type = TAG_COMPOUND},
	};

	INIT_LIST_HEAD(&root_list_sentinel.entry);
	INIT_LIST_HEAD(&level_list_sentinel.entry);
	INIT_LIST_HEAD(&empty_list_sentinel.entry);

	nbt_node level = (nbt_node) {
		.type = TAG_COMPOUND,
		.name = "Level",
		.payload.tag_compound = &level_list_sentinel,
	};

	nbt_node root = (nbt_node) {
		.type = TAG_COMPOUND,
		.name = "",
		.payload.tag_compound = &root_list_sentinel,
	};

	struct nbt_list root_list = (struct nbt_list) {.data = &level};
	list_add_head(&root_list.entry, &root_list_sentinel.entry);

	nbt_node level_list_nodes[] = {
		{
			.type = TAG_BYTE_ARRAY,
			.name = "Blocks",
			.payload.tag_byte_array.data = (unsigned char*)blocks,
			.payload.tag_byte_array.length = LEGACY_BLOCKS,
		},
		{
			.type = TAG_BYTE_ARRAY,
			.name = "Data",
			.payload.tag_byte_array.data = (unsigned char*)data,
			.payload.tag_byte_array.length = LEGACY_NIBBLES,
		},
		{
			.type = TAG_BYTE_ARRAY,
			.name = "SkyLight",
			.payload.tag_byte_array.data = (unsigned char*)skyl,
			.payload.tag_byte_array.length = LEGACY_NIBBLES,
		},
		{
			.type = TAG_BYTE_ARRAY,
			.name = "BlockLight",
			.payload.tag_byte_array.data = (unsigned char*)blockl,
			.payload.tag_byte_array.length = LEGACY_NIBBLES,
		},
		{
			.type = TAG_BYTE_ARRAY,
			.name = "HeightMap",
			.payload.tag_byte_array.data = (unsigned char*)hmap,
			.payload.tag_byte_array.length = HEIGHT_LEN,
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

	return nbt_dump_compressed(&root, STRAT_INFLATE);
}

// Write a valid single-chunk McRegion file for chunk (x=0,z=0). Layout matches
// region_archive: a 2-sector (8 KiB) header of big-endian (offset<<8)|sectors
// entries, then the chunk record (4-byte BE length, 1 type byte, zlib payload)
// at sector 2.
static void write_region_file(const char* path, const struct buffer* nbt) {
	FILE* f = fopen(path, "wb");
	ASSERT(f != NULL);

	uint8_t header[REGION_SECTOR * 2];
	memset(header, 0, sizeof(header));

	// chunk (0,0) lives at index 0; one sector is plenty for our payload.
	uint32_t length = (uint32_t)nbt->len + 1; // +1 for the type byte
	uint32_t sectors
		= (uint32_t)((length + sizeof(uint32_t) + REGION_SECTOR - 1)
					 / REGION_SECTOR);
	ASSERT(sectors >= 1);
	store_u32_be(header, (2U << 8) | sectors); // offset 2 sectors, N sectors

	ASSERT(fwrite(header, sizeof(header), 1, f) == 1);

	uint8_t rec_header[5];
	store_u32_be(rec_header, length);
	rec_header[4] = 2; // 2 = zlib (STRAT_INFLATE)
	ASSERT(fwrite(rec_header, sizeof(rec_header), 1, f) == 1);
	ASSERT(fwrite(nbt->data, nbt->len, 1, f) == 1);

	fclose(f);
}

static void make_temp_world(char* dir, size_t length) {
	char tmpl[] = "/tmp/cavex_migrate_XXXXXX";
	char* base = mkdtemp(tmpl);
	ASSERT(base != NULL);
	snprintf(dir, length, "%s", base);

	char region_dir[600];
	snprintf(region_dir, sizeof(region_dir), "%s/region", base);
	ASSERT(mkdir(region_dir, 0777) == 0);
}

// Full end-to-end: synthesize a 128-tall chunk on disk, read it through the
// production path (which migrates), and verify the 256-tall result.
TEST(region_archive_migrates_128_to_256) {
	// This test only makes sense on the taller engine; on a 128-tall build
	// there is nothing to migrate (and the sizes coincide).
	ASSERT(WORLD_HEIGHT > LEGACY_HEIGHT);

	uint8_t* blocks = calloc(1, LEGACY_BLOCKS);
	uint8_t* data = calloc(1, LEGACY_NIBBLES);
	uint8_t* skyl = calloc(1, LEGACY_NIBBLES);
	uint8_t* blockl = calloc(1, LEGACY_NIBBLES);
	uint8_t* hmap = calloc(1, HEIGHT_LEN);
	ASSERT(blocks && data && skyl && blockl && hmap);

	// Known blocks at known coordinates, including the top legacy cell y=127.
	blocks[idx_legacy(0, 0, 0)] = 7;	 // bedrock at the floor
	blocks[idx_legacy(15, 64, 15)] = 1;	 // stone, far corner column, mid height
	blocks[idx_legacy(5, 127, 3)] = 41;	 // gold block at the legacy ceiling
	blocks[idx_legacy(8, 50, 9)] = 17;	 // wood somewhere in the middle

	// Known per-cell light + metadata (nibble packed).
	nibble_write(data, idx_legacy(5, 127, 3), 0xA);
	nibble_write(data, idx_legacy(0, 1, 0), 0x3);
	nibble_write(blockl, idx_legacy(5, 127, 3), 0x7);
	nibble_write(skyl, idx_legacy(0, 10, 0), 0x4);
	nibble_write(skyl, idx_legacy(5, 127, 3), 0x2);

	// Per-column heightmap (one byte per column, height independent).
	hmap[2 + 4 * CHUNK_SIZE] = 100;
	hmap[15 + 15 * CHUNK_SIZE] = 65;

	struct buffer nbt
		= build_legacy_chunk_nbt(0, 0, blocks, data, skyl, blockl, hmap);
	ASSERT(nbt.data != NULL);

	char dir[512];
	make_temp_world(dir, sizeof(dir));

	char region_path[700];
	snprintf(region_path, sizeof(region_path), "%s/region/r.0.0.mcr", dir);
	write_region_file(region_path, &nbt);
	buffer_free(&nbt);

	string_t world;
	string_init_printf(world, "%s", dir);

	struct region_archive ra = {0};
	ASSERT(region_archive_create(&ra, world, 0, 0, WORLD_DIM_OVERWORLD));

	bool exists = false;
	ASSERT(region_archive_contains(&ra, 0, 0, &exists));
	ASSERT(exists);

	struct server_chunk sc = {0};
	ASSERT(region_archive_get_blocks(&ra, 0, 0, &sc));

	// (a) blocks landed at the correct NEW 256-stride indices
	ASSERT_EQ(sc.ids[idx_new(0, 0, 0)], 7);
	ASSERT_EQ(sc.ids[idx_new(15, 64, 15)], 1);
	ASSERT_EQ(sc.ids[idx_new(5, 127, 3)], 41);
	ASSERT_EQ(sc.ids[idx_new(8, 50, 9)], 17);

	// metadata + block light preserved at the migrated indices
	ASSERT_EQ(nibble_read(sc.metadata, idx_new(5, 127, 3)), 0xA);
	ASSERT_EQ(nibble_read(sc.metadata, idx_new(0, 1, 0)), 0x3);
	ASSERT_EQ(nibble_read(sc.lighting_torch, idx_new(5, 127, 3)), 0x7);
	ASSERT_EQ(nibble_read(sc.lighting_sky, idx_new(0, 10, 0)), 0x4);
	ASSERT_EQ(nibble_read(sc.lighting_sky, idx_new(5, 127, 3)), 0x2);

	// a cell that should still be air in the bottom half (and would have been
	// clobbered by an off-by-one stride bug)
	ASSERT_EQ(sc.ids[idx_new(0, 1, 0)], 0);

	// (b) the new upper half (y >= 128) is air, with zero metadata/block-light,
	// and (c) full sky light, across several columns including the boundary.
	int probe_cols[][2] = {{0, 0}, {5, 3}, {15, 15}, {8, 9}};
	for(size_t p = 0; p < sizeof(probe_cols) / sizeof(*probe_cols); p++) {
		int x = probe_cols[p][0];
		int z = probe_cols[p][1];
		for(int y = LEGACY_HEIGHT; y < WORLD_HEIGHT; y++) {
			size_t i = idx_new(x, y, z);
			ASSERT_EQ(sc.ids[i], 0);
			ASSERT_EQ(nibble_read(sc.metadata, i), 0x0);
			ASSERT_EQ(nibble_read(sc.lighting_torch, i), 0x0);
			ASSERT_EQ(nibble_read(sc.lighting_sky, i), 0xF);
		}
		// the cell just below the seam is the migrated legacy ceiling, not air'd
		// over: sky light there is whatever we wrote (default 0), never forced.
		ASSERT_EQ(nibble_read(sc.lighting_sky, idx_new(x, LEGACY_HEIGHT - 1, z)),
				  (x == 5 && z == 3) ? 0x2 : 0x0);
	}

	// (d) HeightMap is one byte per column and copied unchanged.
	ASSERT_EQ(sc.heightmap[2 + 4 * CHUNK_SIZE], 100);
	ASSERT_EQ(sc.heightmap[15 + 15 * CHUNK_SIZE], 65);

	// the migrated chunk is flagged modified so the next save rewrites it in the
	// current 256-tall layout (self-upgrade on disk).
	ASSERT(sc.modified);

	// Re-save: the write path emits WORLD_HEIGHT-sized arrays, so reading it
	// back must now succeed with NO migration and the data must survive the
	// round trip at full height (proves the on-disk upgrade).
	ASSERT(region_archive_set_blocks(&ra, 0, 0, &sc));
	free_chunk(&sc);

	struct server_chunk sc2 = {0};
	ASSERT(region_archive_get_blocks(&ra, 0, 0, &sc2));
	ASSERT_EQ(sc2.ids[idx_new(5, 127, 3)], 41);
	ASSERT_EQ(sc2.ids[idx_new(0, 0, 0)], 7);
	ASSERT_EQ(nibble_read(sc2.lighting_sky, idx_new(0, WORLD_HEIGHT - 1, 0)),
			  0xF);
	free_chunk(&sc2);

	region_archive_destroy(&ra);
	string_clear(world);

	free(blocks);
	free(data);
	free(skyl);
	free(blockl);
	free(hmap);
}

const test_entry_t g_tests_region_migrate[] = {
	{"region_archive_migrates_128_to_256",
	 test_region_archive_migrates_128_to_256},
};

const size_t g_tests_region_migrate_count
	= sizeof(g_tests_region_migrate) / sizeof(g_tests_region_migrate[0]);
