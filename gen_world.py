#!/usr/bin/env python3
"""Generate a minimal Minecraft Beta 1.7.3 (McRegion) world for CavEX.

Pure stdlib. Produces:
  out/world/level.dat          gzipped NBT, fields CavEX's level_archive.c reads
  out/world/region/r.0.0.mcr   1024 flat-grass chunks, zlib-compressed NBT

Beta block array order: index = y + z*WORLD_HEIGHT + x*WORLD_HEIGHT*16  (XZY)
Nibble arrays (Data/SkyLight/BlockLight): even index -> low nibble.

WORLD_HEIGHT must match the engine's source/world.h WORLD_HEIGHT for the build
this world is staged for: the region's Blocks/Data/Light NBT arrays are sized
16*16*WORLD_HEIGHT (and /2 for the nibble arrays). The PC build is 256-tall
(issue #26); the Wii build stays 128-tall. Worlds are therefore NOT portable
between the two heights. Override with CAVEX_WORLD_HEIGHT for a Wii (128) gen.
"""
import gzip, os, struct, sys, time, zlib
from functools import lru_cache
from pathlib import Path

# ---------------- NBT writer ----------------
def tag_hdr(tid, name):
    b = name.encode()
    return struct.pack(">bh", tid, len(b)) + b

def t_byte(name, v):   return tag_hdr(1, name) + struct.pack(">b", v)
def t_short(name, v):  return tag_hdr(2, name) + struct.pack(">h", v)
def t_int(name, v):    return tag_hdr(3, name) + struct.pack(">i", v)
def t_long(name, v):   return tag_hdr(4, name) + struct.pack(">q", v)
def t_bytearr(name, b):return tag_hdr(7, name) + struct.pack(">i", len(b)) + bytes(b)
def t_string(name, s):
    e = s.encode()
    return tag_hdr(8, name) + struct.pack(">h", len(e)) + e
def t_list(name, tid, payloads):
    return tag_hdr(9, name) + struct.pack(">bi", tid, len(payloads)) + b"".join(payloads)
def t_compound(name, children):
    return tag_hdr(10, name) + b"".join(children) + b"\x00"
def p_compound(children):
    # list-element compounds are BARE payloads: no tag id, no name
    return b"".join(children) + b"\x00"
def p_double(v): return struct.pack(">d", v)
def p_float(v):  return struct.pack(">f", v)

# ---------------- terrain ----------------
AIR, STONE, GRASS, DIRT, BEDROCK, LOG, LEAVES, SNOW = 0, 1, 2, 3, 7, 17, 18, 80
# Beta 1.7.3 ids below are all registered + non-NULL in
# source/block/blocks_data.h + source/block/blocks.c (verified).
WATER, LAVA = 9, 11                      # still source variants
SAND = 12
GRAVEL = 13
GOLD_ORE, IRON_ORE, COAL_ORE = 14, 15, 16
LAPIS_ORE = 21
DIAMOND_ORE = 56
REDSTONE_ORE = 73
SEED = int(os.environ.get("CAVEX_SEED", "42"))   # CAVEX_SEED overrides; terrain + strata + RandomSeed all derive from it
LEVEL_NAME = os.environ.get("CAVEX_LEVEL_NAME", "Claude World")  # CAVEX_LEVEL_NAME overrides the world-list name
# Column height in blocks. MUST equal the target build's source/world.h
# WORLD_HEIGHT (PC=256, Wii=128). Set CAVEX_WORLD_HEIGHT=128 to gen for the Wii.
WORLD_HEIGHT = int(os.environ.get("CAVEX_WORLD_HEIGHT", "256"))
COL_BLOCKS = 16 * 16 * WORLD_HEIGHT      # bytes per chunk Blocks array (XZY, full height)
COL_NIBBLES = COL_BLOCKS // 2            # bytes per Data/SkyLight/BlockLight (one nibble per cell)
BASE_Y = 62          # baseline surface height (sea-level-ish)
SNOW_LINE = 90       # ground at/above this y gets a snow surface
CELL = 24            # value-noise lattice spacing in blocks

# ---- caves (issue #24): connected 3D-noise tunnels carved into the stone body ----
CAVE_CELL_XZ = 14    # horizontal lattice spacing of the 3D cave noise (blocks)
CAVE_CELL_Y = 9      # vertical lattice spacing (squashed -> wider, flatter tunnels)
CAVE_SHELL = 0.085   # half-width of the iso-surface band carved to AIR; a thin band
                     # around the noise mid-value yields *connected*, winding tunnels
                     # (a plain low threshold would give isolated blobs)
CAVE_FLOOR = 5       # lowest y a cave may carve (keeps bedrock y0..3 + a margin solid)
CAVE_ROOF_MARGIN = 3 # caves stop this many blocks below the surface (solid roof, no
                     # surface breach -> spawn/terrain integrity stays guaranteed)

# ---- lakes/ponds (issue #24): water bodies in the flat basin floors ----
# surface_y() clamps any column whose relief noise dipped to/below BASE_Y up to
# BASE_Y, so the y==BASE_Y columns are exactly the natural valley basins. We carve
# a shallow bowl into them and fill it with still water up to WATER_LEVEL.
WATER_LEVEL = BASE_Y         # still-water surface height for lakes/ponds (== 62)
LAKE_MAX_DEPTH = 3           # deepest a basin bowl is dug below WATER_LEVEL
LAKE_SPAWN_CLEAR = 6         # keep this Chebyshev radius around spawn lake-free, so a
                            # seed whose spawn column happens to be a basin still gets
                            # dry, solid footing (mirrors TREE_CLEAR for the spawn rule)

def h(wx, wz, y, salt):
    """Deterministic pure-stdlib hash -> 32-bit unsigned, seeded from SEED.

    No randomness/wall-clock: same (wx, wz, y, salt) always yields the same
    value, so the region is byte-identical across runs. Mixes the coordinates
    with fixed odd primes (xorshift-style) and masks to 32 bits.
    """
    n = (SEED & 0xFFFFFFFF)
    n = (n * 1597334677 + (wx & 0xFFFFFFFF) * 3812015801) & 0xFFFFFFFF
    n = (n * 1597334677 + (wz & 0xFFFFFFFF) * 2654435761) & 0xFFFFFFFF
    n = (n * 1597334677 + (y & 0xFFFFFFFF) * 40503) & 0xFFFFFFFF
    n = (n * 1597334677 + (salt & 0xFFFFFFFF) * 2246822519) & 0xFFFFFFFF
    n ^= (n >> 15)
    n = (n * 2246822519) & 0xFFFFFFFF
    n ^= (n >> 13)
    n = (n * 3266489917) & 0xFFFFFFFF
    n ^= (n >> 16)
    return n

def chance(wx, wz, y, salt, prob):
    """True with probability ~prob (0..1), deterministically."""
    return h(wx, wz, y, salt) < int(prob * 0x100000000)

def strata_block(wx, wz, y):
    """Block id for a sub-surface cell at world column (wx, wz), height y.

    Layout (y rises from 0):
      y == 0            -> bedrock (solid floor)
      1 <= y <= 3       -> rough bedrock/stone mix (vanilla-style floor)
      otherwise         -> stone body, with scattered ores + rare lava/water
    Returns STONE by default; callers handle dirt/grass at the top.
    """
    if y == 0:
        return BEDROCK
    if y <= 3:
        # higher chance of bedrock lower down: y1 ~75%, y2 ~50%, y3 ~25%
        return BEDROCK if chance(wx, wz, y, 1, (4 - y) * 0.25) else STONE

    # Rare fluid pockets (single static source blocks, no flow sim).
    if y < 10 and chance(wx, wz, y, 2, 0.004):
        return LAVA
    if 12 <= y <= 48 and chance(wx, wz, y, 3, 0.0015):
        return WATER

    # Ore veins: depth-banded probabilities, small clusters via per-cell hash.
    # Common ores in the upper stone body.
    if chance(wx, wz, y, 4, 0.012):
        return COAL_ORE
    if y < 64 and chance(wx, wz, y, 5, 0.010):
        return IRON_ORE
    if 4 <= y <= 40 and chance(wx, wz, y, 6, 0.006):
        return GRAVEL
    # Rarer ores get deeper.
    if y < 32 and chance(wx, wz, y, 7, 0.004):
        return GOLD_ORE
    if y < 16 and chance(wx, wz, y, 8, 0.005):
        return REDSTONE_ORE
    if y < 30 and chance(wx, wz, y, 9, 0.003):
        return LAPIS_ORE
    if y < 16 and chance(wx, wz, y, 10, 0.002):
        return DIAMOND_ORE
    return STONE

# ---------------- heightmap (deterministic value-noise) ----------------
def _hash01(ix, iz):
    """Lattice-point hash -> float in [0,1). Pure-integer, seeded, deterministic."""
    h = (ix * 374761393 + iz * 668265263 + SEED * 2147483647) & 0xFFFFFFFF
    h = (h ^ (h >> 13)) * 1274126177 & 0xFFFFFFFF
    h ^= h >> 16
    return (h & 0xFFFFFF) / float(0x1000000)

def _smooth(t):
    # smoothstep so cell boundaries don't crease
    return t * t * (3.0 - 2.0 * t)

def _value_noise(wx, wz, cell):
    """Bilinearly-interpolated, smoothstepped value noise in [0,1)."""
    gx, gz = wx / float(cell), wz / float(cell)
    ix, iz = int(gx) - (1 if gx < 0 else 0), int(gz) - (1 if gz < 0 else 0)
    fx, fz = _smooth(gx - ix), _smooth(gz - iz)
    n00, n10 = _hash01(ix, iz), _hash01(ix + 1, iz)
    n01, n11 = _hash01(ix, iz + 1), _hash01(ix + 1, iz + 1)
    nx0 = n00 + (n10 - n00) * fx
    nx1 = n01 + (n11 - n01) * fx
    return nx0 + (nx1 - nx0) * fz

@lru_cache(maxsize=None)
def _hash01_3(ix, iy, iz, salt):
    """3D lattice-point hash -> float in [0,1). Pure-integer, seeded, deterministic.
    `salt` lets independent 3D noise fields share the lattice hashing helper.
    Cached: each lattice point is shared by CAVE_CELL_XZ^2 * CAVE_CELL_Y cells, so
    memoizing here collapses tens of millions of cell evals onto a few thousand
    distinct lattice points -> generation stays close to the pre-cave time."""
    h = (ix * 374761393 + iy * 1610612741 + iz * 668265263
         + salt * 2246822519 + SEED * 2147483647) & 0xFFFFFFFF
    h = (h ^ (h >> 13)) * 1274126177 & 0xFFFFFFFF
    h ^= h >> 16
    return (h & 0xFFFFFF) / float(0x1000000)

def _value_noise3(wx, wy, wz, cell_xz, cell_y, salt=0):
    """Trilinearly-interpolated, smoothstepped 3D value noise in [0,1).
    Vertical lattice spacing is independent so caves can be squashed (wider than
    they are tall). Pure-integer lattice hash -> fully deterministic from SEED."""
    gx, gy, gz = wx / float(cell_xz), wy / float(cell_y), wz / float(cell_xz)
    ix = int(gx) - (1 if gx < 0 else 0)
    iy = int(gy) - (1 if gy < 0 else 0)
    iz = int(gz) - (1 if gz < 0 else 0)
    fx, fy, fz = _smooth(gx - ix), _smooth(gy - iy), _smooth(gz - iz)
    hh = _hash01_3
    # 8 corner lattice values, interpolate along x, then y, then z
    n000, n100 = hh(ix, iy, iz, salt),     hh(ix + 1, iy, iz, salt)
    n010, n110 = hh(ix, iy + 1, iz, salt), hh(ix + 1, iy + 1, iz, salt)
    n001, n101 = hh(ix, iy, iz + 1, salt), hh(ix + 1, iy, iz + 1, salt)
    n011, n111 = hh(ix, iy + 1, iz + 1, salt), hh(ix + 1, iy + 1, iz + 1, salt)
    c00 = n000 + (n100 - n000) * fx
    c10 = n010 + (n110 - n010) * fx
    c01 = n001 + (n101 - n001) * fx
    c11 = n011 + (n111 - n011) * fx
    c0 = c00 + (c10 - c00) * fy
    c1 = c01 + (c11 - c01) * fy
    return c0 + (c1 - c0) * fz

def is_cave(wx, wz, y, surf):
    """True if (wx, y, wz) should be carved to AIR (a cave cell).

    Caves are the thin iso-surface band |noise - 0.5| < CAVE_SHELL of a squashed
    3D value-noise field -> long, branching, *connected* tunnels (Beta-ish). The
    band is only evaluated in CAVE_FLOOR .. (surf - CAVE_ROOF_MARGIN), so bedrock
    (y0..3) and a solid surface roof are always preserved (no surface breach)."""
    if y < CAVE_FLOOR or y > surf - CAVE_ROOF_MARGIN:
        return False
    n = _value_noise3(wx, y, wz, CAVE_CELL_XZ, CAVE_CELL_Y, salt=101)
    return abs(n - 0.5) < CAVE_SHELL

@lru_cache(maxsize=None)
def lake_depth(wx, wz):
    """Bowl depth (blocks dug below WATER_LEVEL) for a basin column, else 0.

    Only the flat valley basins (surface clamped to BASE_Y) become lakes. Depth
    is a deterministic 1..LAKE_MAX_DEPTH from the relief noise so the bowl has a
    natural, varied floor. The spawn clearing is force-excluded so that even a
    seed whose spawn column is itself a basin keeps dry, solid footing — this is
    what keeps the spawn-clear / eye-height rule intact for every seed."""
    if max(abs(wx - SPAWN_X), abs(wz - SPAWN_Z)) <= LAKE_SPAWN_CLEAR:
        return 0
    if surface_y(wx, wz) > WATER_LEVEL:
        return 0
    # deeper toward basin interiors: drive depth from the (low) relief noise value
    t = 1.0 - _value_noise(wx, wz, CELL)        # 0 at basin rim, larger inside
    d = 1 + int(t * LAKE_MAX_DEPTH)
    return max(1, min(LAKE_MAX_DEPTH, d))

@lru_cache(maxsize=None)
def surface_y(wx, wz):
    """Deterministic surface height: rolling hills + sparser, taller peaks.
    Bounded to BASE_Y..~100 and hard-capped at 122 (terrain-shape choice; well
    under WORLD_HEIGHT). The taller PC world (#26) keeps the same natural relief
    — extra headroom above is for player builds, not bigger natural mountains."""
    hills = _value_noise(wx, wz, CELL)            # broad relief
    peaks = _value_noise(wx, wz, CELL * 3)        # rarer big features
    h = BASE_Y + hills * 22.0                     # ~62..84
    if peaks > 0.72:                              # occasional high ground
        h += (peaks - 0.72) * (38.0 / 0.28)       # adds up to ~+38 -> ~100s
    top = int(h)
    if top < BASE_Y:
        top = BASE_Y
    if top > 122:                                 # hard safety cap (< 124 < 128)
        top = 122
    return top

def surface_block(top):
    """Surface block for a column of the given height: snow on high ground, else grass."""
    return SNOW if top >= SNOW_LINE else GRASS

TREE_PROB = 0.010          # ~1% of (non-snow) land columns sprout a tree
TREE_CLEAR = 4             # keep this Chebyshev radius around spawn tree-free

@lru_cache(maxsize=None)
def is_tree(tx, tz):
    """True if a tree trunk originates at this column. Deterministic + seeded, so
    every seed grows a DIFFERENT forest layout. No trees in the spawn clearing or
    on the bare snowy peaks."""
    if max(abs(tx - SPAWN_X), abs(tz - SPAWN_Z)) <= TREE_CLEAR:
        return False
    if surface_y(tx, tz) >= SNOW_LINE:
        return False
    if lake_depth(tx, tz):          # no trees standing in lakes/ponds
        return False
    return chance(tx, tz, 0, 20, TREE_PROB)

def column_blocks(wx, wz, top):
    """Scattered oak-like trees -> each seed's forest is unique. A column can carry
    trunk and/or canopy from any tree whose 5x5 footprint covers it; blocks rebase
    onto each tree origin's OWN surface height (not this column's `top`)."""
    extra = []
    for dx in range(-2, 3):
        for dz in range(-2, 3):
            tx, tz = wx + dx, wz + dz
            if not is_tree(tx, tz):
                continue
            tsurf = surface_y(tx, tz)
            trunk_h = 4 + (h(tx, tz, 0, 21) % 2)        # 4 or 5 blocks tall
            T = tsurf + trunk_h                          # trunk top
            cheb = max(abs(dx), abs(dz))
            if dx == 0 and dz == 0:
                extra += [(tsurf + k, LOG) for k in range(1, trunk_h + 1)]
                extra += [(T + 1, LEAVES), (T + 2, LEAVES)]            # top cap
            else:
                if cheb <= 2 and not (abs(dx) == 2 and abs(dz) == 2):
                    extra += [(T - 1, LEAVES), (T, LEAVES)]            # wide skirt
                if cheb <= 1:
                    extra.append((T + 1, LEAVES))                      # upper ring
    return extra

# ---------------- spawn (computed from the heightmap, so the player stands on ground) ----------------
# CavEX stores Player.Pos.y as the EYE position; the engine derives the feet as
# Pos.y - EYE_HEIGHT (source/entity/entity_local_player.c). Pos.y must therefore
# clear the surface block top by EYE_HEIGHT, or the player spawns embedded in the
# ground and is wedged in place (has to dig out). Minecraft's feet convention
# (SPAWN_SURFACE + 1.5) does exactly that and was the stuck-at-spawn bug.
EYE_HEIGHT = 1.62                                     # must match entity_local_player.c
SPAWN_X, SPAWN_Z = 24, 24
SPAWN_SURFACE = surface_y(SPAWN_X, SPAWN_Z)          # solid surface block y at spawn column
SPAWN_FEET = SPAWN_SURFACE + 1                        # top face of the surface block (feet rest here)
SPAWN_Y = SPAWN_FEET                                  # integer respawn cell for SpawnX/Y/Z
SPAWN = (SPAWN_X + 0.5, SPAWN_FEET + 0.38 + EYE_HEIGHT, SPAWN_Z + 0.5)  # eye pos; feet ~0.38 above ground

def build_chunk(cx, cz):
    blocks = bytearray(COL_BLOCKS)
    data   = bytearray(COL_NIBBLES)    # all 0
    skyl   = bytearray(COL_NIBBLES)
    blockl = bytearray(COL_NIBBLES)    # all 0
    hmap   = bytearray(256)

    for x in range(16):
        for z in range(16):
            wx, wz = cx * 16 + x, cz * 16 + z
            base = (x * 16 + z) * WORLD_HEIGHT   # x*H*16 + z*H
            surf = surface_y(wx, wz)        # per-column surface height (relief)
            depth = lake_depth(wx, wz)      # >0 only for flat basin (lake) columns
            # Layered sub-surface: bedrock floor, stone body, ores + fluid pockets
            # (deterministic from SEED via strata_block()), up to the surface.
            for y in range(0, surf - 4):        blocks[base + y] = strata_block(wx, wz, y)
            for y in range(surf - 4, surf):     blocks[base + y] = DIRT
            blocks[base + surf] = surface_block(surf)   # snow on high ground, else grass

            # Carve caves: thin 3D-noise iso-surface -> connected tunnels. Stops
            # short of the surface (solid roof) and the bedrock floor; in lake
            # columns it also stays a block below the basin bowl so water can't
            # drain into the cave system.
            cave_top = surf - CAVE_ROOF_MARGIN
            if depth:
                cave_top = min(cave_top, WATER_LEVEL - depth - 1)
            for y in range(CAVE_FLOOR, cave_top + 1):
                if is_cave(wx, wz, y, surf):
                    blocks[base + y] = AIR

            # Lakes/ponds: dig a shallow bowl into the basin floor and fill it
            # with still water up to WATER_LEVEL; sandy bowl floor underneath.
            water_top = -1
            if depth:
                floor_y = WATER_LEVEL - depth
                blocks[base + floor_y] = SAND
                for y in range(floor_y + 1, WATER_LEVEL + 1):
                    blocks[base + y] = WATER
                water_top = WATER_LEVEL

            top = surf
            for (y, bid) in column_blocks(wx, wz, surf):
                if 0 <= y < WORLD_HEIGHT:
                    if bid == LEAVES and blocks[base + y] != AIR:
                        continue            # don't bury terrain under floating leaves
                    blocks[base + y] = bid
                    top = max(top, y)
            hmap[z * 16 + x] = top + 1
            # Skylight: for land, the air above the surface block is lit. For a
            # lake the topmost water block is open to the sky, so light it too;
            # the water below it stays dark (SkyLight 0), reading as deeper water.
            sky_from = water_top if water_top >= 0 else top + 1
            for y in range(sky_from, WORLD_HEIGHT):
                idx = base + y
                if idx & 1: skyl[idx >> 1] |= 0xF0
                else:       skyl[idx >> 1] |= 0x0F

    level = t_compound("Level", [
        t_bytearr("Blocks", blocks),
        t_bytearr("Data", data),
        t_bytearr("SkyLight", skyl),
        t_bytearr("BlockLight", blockl),
        t_bytearr("HeightMap", hmap),
        t_list("Entities", 10, []),
        t_list("TileEntities", 10, []),
        t_long("LastUpdate", 0),
        t_int("xPos", cx),
        t_int("zPos", cz),
        t_byte("TerrainPopulated", 1),
    ])
    return t_compound("", [level])

def write_region(path):
    offsets = bytearray(4096)
    stamps  = bytearray(4096)
    body = bytearray()
    sector = 2
    for cz in range(32):
        for cx in range(32):
            raw = zlib.compress(build_chunk(cx, cz), 6)
            payload = struct.pack(">i", len(raw) + 1) + b"\x02" + raw
            sectors = (len(payload) + 4095) // 4096
            payload += b"\x00" * (sectors * 4096 - len(payload))
            i = (cx + cz * 32) * 4
            offsets[i:i+4] = struct.pack(">i", (sector << 8) | sectors)
            body += payload
            sector += sectors
    path.write_bytes(bytes(offsets) + bytes(stamps) + bytes(body))
    return path.stat().st_size

def write_level_dat(path, disk_size):
    inv = [
        p_compound([t_short("id", 3),  t_byte("Count", 64), t_short("Damage", 0), t_byte("Slot", 0)]),
        p_compound([t_short("id", 5),  t_byte("Count", 64), t_short("Damage", 0), t_byte("Slot", 1)]),
        p_compound([t_short("id", 50), t_byte("Count", 64), t_short("Damage", 0), t_byte("Slot", 2)]),
        p_compound([t_short("id", 1),  t_byte("Count", 64), t_short("Damage", 0), t_byte("Slot", 3)]),
        p_compound([t_short("id", 97), t_byte("Count", 64), t_short("Damage", 0), t_byte("Slot", 4)]),  # Candle (#55) — glowing block
        p_compound([t_short("id", 98), t_byte("Count", 64), t_short("Damage", 0), t_byte("Slot", 5)]),  # Bubble column (#56) — ride upward
    ]
    player = t_compound("Player", [
        t_short("Health", 20),
        t_int("Dimension", 0),
        t_list("Pos", 6, [p_double(v) for v in SPAWN]),
        t_list("Rotation", 5, [p_float(float(h(SPAWN_X, SPAWN_Z, 0, 99) % 360)), p_float(15.0)]),
        t_list("Motion", 6, [p_double(0.0)] * 3),
        t_list("Inventory", 10, inv),
    ])
    data = t_compound("Data", [
        t_string("LevelName", LEVEL_NAME),
        t_long("Time", 0),
        t_long("LastPlayed", int(time.time() * 1000)),
        t_long("SizeOnDisk", disk_size),
        t_long("RandomSeed", SEED),
        t_int("SpawnX", SPAWN_X), t_int("SpawnY", SPAWN_Y), t_int("SpawnZ", SPAWN_Z),
        t_int("version", 19132),
        player,
    ])
    path.write_bytes(gzip.compress(t_compound("", [data])))

def _largest_connected(cols):
    """Largest 4-connected component size in a set of (x, z) cells (self-check)."""
    from collections import deque
    cols = set(cols)
    seen = set()
    best = 0
    for start in cols:
        if start in seen:
            continue
        q = deque([start]); seen.add(start); size = 0
        while q:
            x, z = q.popleft(); size += 1
            for nx, nz in ((x+1, z), (x-1, z), (x, z+1), (x, z-1)):
                n = (nx, nz)
                if n in cols and n not in seen:
                    seen.add(n); q.append(n)
        if size > best:
            best = size
    return best

def _largest_cave_volume(read_chunk):
    """Largest 6-connected AIR volume below the surface roof across sample chunks.

    Proves caves are *connected* tunnels (not scattered single-cell pockets)
    without the cost of flooding the whole region. Sampling several spread-out
    chunks (rather than one) makes the connectivity gate robust to any single
    chunk happening to hold only short stubs for a given seed. The per-chunk
    flood is chunk-local, so it UNDER-counts tunnels that exit the chunk — i.e.
    it can only under-report, never over-report, connectivity (safe direction)."""
    from collections import deque
    best = 0
    for scx, scz in ((2, 2), (10, 6), (20, 24), (28, 14)):
        ch = read_chunk(scx, scz)
        bi = ch.find(b"Blocks") + len("Blocks") + 4
        blocks = ch[bi:bi + 32768]
        air = set()
        for x in range(16):
            for z in range(16):
                col = (x * 16 + z) * 128
                top_y = -1
                for y in range(127, -1, -1):
                    if blocks[col + y] != AIR:
                        top_y = y
                        break
                for y in range(CAVE_FLOOR, top_y):
                    if blocks[col + y] == AIR:
                        air.add((x, y, z))
        seen = set()
        for start in air:
            if start in seen:
                continue
            q = deque([start]); seen.add(start); size = 0
            while q:
                x, y, z = q.popleft(); size += 1
                for nx, ny, nz in ((x+1,y,z),(x-1,y,z),(x,y+1,z),(x,y-1,z),(x,y,z+1),(x,y,z-1)):
                    n = (nx, ny, nz)
                    if n in air and n not in seen:
                        seen.add(n); q.append(n)
            if size > best:
                best = size
    return best

def main():
    out = Path(sys.argv[1] if len(sys.argv) > 1 else "out") / "world"
    (out / "region").mkdir(parents=True, exist_ok=True)
    size = write_region(out / "region" / "r.0.0.mcr")
    write_level_dat(out / "level.dat", size)
    print(f"region: {size} bytes, level.dat: {(out/'level.dat').stat().st_size} bytes")

    # -------- self-check: re-parse what we wrote --------
    lvl = gzip.decompress((out / "level.dat").read_bytes())
    # LevelName + inventory round-trip (the configured name, default 'Claude World')
    assert b"LevelName" in lvl and LEVEL_NAME.encode() in lvl and b"Inventory" in lvl
    reg = (out / "region" / "r.0.0.mcr").read_bytes()

    def read_chunk(cx, cz):
        off = struct.unpack(">i", reg[((cx & 31) + (cz & 31) * 32) * 4:][:4])[0]
        sec = off >> 8
        ln = struct.unpack(">i", reg[sec * 4096:sec * 4096 + 4])[0]
        return zlib.decompress(reg[sec * 4096 + 5: sec * 4096 + 4 + ln])

    def column_top(chunk, x_in, z_in):
        """topmost non-air block y in a column of a parsed chunk."""
        bi = chunk.find(b"Blocks") + len("Blocks") + 4
        col = bi + (x_in * 16 + z_in) * WORLD_HEIGHT
        for y in range(WORLD_HEIGHT - 1, -1, -1):
            if chunk[col + y] != AIR:
                return y, chunk[col + y]
        return 0, AIR

    # spawn chunk parses with the expected NBT skeleton
    spawn_chunk = read_chunk(SPAWN_X // 16, SPAWN_Z // 16)
    spawn_xin, spawn_zin = SPAWN_X % 16, SPAWN_Z % 16
    assert b"Blocks" in spawn_chunk and b"SkyLight" in spawn_chunk and b"xPos" in spawn_chunk

    # Scan the whole region once: collect surface heights, count snow caps, track
    # the absolute top non-air block (landmark stacks included), and tally the new
    # caves (underground air) + lake water bodies.
    heights = set()      # distinct grass/snow surface heights (relief)
    snow_cols = 0        # columns whose surface block is snow
    region_max = 0       # highest non-air block anywhere (must stay < WORLD_HEIGHT)
    cave_air = 0         # AIR cells trapped below the surface roof (caves)
    water_cells = 0      # WATER cells anywhere (lakes/ponds)
    lake_cols = set()    # (wx, wz) columns whose top is still water (lake surface)
    for cz in range(32):
        for cx in range(32):
            ch = read_chunk(cx, cz)
            bi = ch.find(b"Blocks") + len("Blocks") + 4
            for x_in in range(16):
                for z_in in range(16):
                    col = bi + (x_in * 16 + z_in) * WORLD_HEIGHT
                    # walk down from the top: first non-air is the surface (land
                    # block, or water for a lake); everything AIR strictly below
                    # the highest solid/water block is cave space.
                    top_y = -1
                    for y in range(WORLD_HEIGHT - 1, -1, -1):
                        b = ch[col + y]
                        if b == AIR:
                            # count only air that is unambiguously subsurface: below
                            # the column's top AND at/under BASE_Y, so foliage gaps
                            # between tree canopy and ground are never miscounted.
                            if top_y >= 0 and CAVE_FLOOR <= y <= BASE_Y:
                                cave_air += 1   # air under the roof -> a cave cell
                            continue
                        if top_y < 0:
                            top_y = y
                            if y > region_max:
                                region_max = y
                            if b == WATER:
                                lake_cols.add((cx * 16 + x_in, cz * 16 + z_in))
                            elif b in (GRASS, SNOW):
                                heights.add(y)
                                if b == SNOW:
                                    snow_cols += 1
                        # count only lake-band water (the bowl is dug at most
                        # LAKE_MAX_DEPTH below WATER_LEVEL); this excludes the deep
                        # strata fluid pockets so the tally reflects lakes alone.
                        if b == WATER and y >= WATER_LEVEL - LAKE_MAX_DEPTH:
                            water_cells += 1

    # (1) terrain is non-flat: a real spread of surface heights
    assert len(heights) >= 5, f"terrain too flat: only {len(heights)} distinct heights"
    assert max(heights) - min(heights) >= 8, "terrain relief too shallow"
    # (2) at least one snow-capped column at/above the snow line
    assert snow_cols >= 1, "no snow-capped columns found"
    assert max(heights) >= SNOW_LINE, f"no ground reaches the snow line (max {max(heights)})"
    # (3) the height cap holds for the whole region (incl. landmark stacks)
    assert region_max < WORLD_HEIGHT, \
        f"a column exceeds the {WORLD_HEIGHT} cap (max y {region_max})"

    # (6) caves: a meaningful amount of connected underground air was carved.
    assert cave_air >= 10000, f"too little cave air carved ({cave_air} cells)"
    largest_cave = _largest_cave_volume(read_chunk)
    assert largest_cave >= 200, \
        f"caves not connected (largest single cave volume {largest_cave} cells)"

    # (7) lakes/ponds: at least one multi-block surface water body. Flood-fill the
    # lake-surface columns; require one horizontally-connected pool of >= 8 cells.
    assert water_cells >= 1, "no water cells found in region"
    biggest_lake = _largest_connected(lake_cols)
    assert biggest_lake >= 8, \
        f"no multi-block water body (largest connected lake surface {biggest_lake} cells)"

    # (4) spawn is safe: solid surface, 2 air blocks of head clearance, and the
    # player's FEET (Pos.y - EYE_HEIGHT) land just above the surface block top.
    sy, sb = column_top(spawn_chunk, spawn_xin, spawn_zin)
    assert sb in (GRASS, SNOW, STONE, DIRT), f"spawn surface not solid ground (block {sb})"
    assert sy == SPAWN_SURFACE, f"spawn surface y {sy} != computed {SPAWN_SURFACE}"
    bi = spawn_chunk.find(b"Blocks") + len("Blocks") + 4
    col = (spawn_xin * 16 + spawn_zin) * WORLD_HEIGHT
    assert spawn_chunk[bi + col + sy + 1] == AIR and spawn_chunk[bi + col + sy + 2] == AIR, \
        "spawn column lacks 2 blocks of head clearance"
    assert SPAWN_Y == sy + 1, f"SpawnY {SPAWN_Y} not just above surface {sy}"
    feet = SPAWN[1] - EYE_HEIGHT
    assert sy + 1 <= feet < sy + 2, \
        f"player would spawn embedded/floating: feet {feet:.2f}, surface top {sy + 1}"

    # (5) strata present: bedrock floor + ores + fluid pockets (from strata_block)
    sbi = spawn_chunk.find(b"Blocks") + len("Blocks") + 4
    spawn_blocks = spawn_chunk[sbi:sbi + COL_BLOCKS]
    assert spawn_blocks[(spawn_xin * 16 + spawn_zin) * WORLD_HEIGHT + 0] == BEDROCK, "bedrock missing at y=0"
    ore_ids = {COAL_ORE, IRON_ORE, GOLD_ORE, REDSTONE_ORE, LAPIS_ORE, DIAMOND_ORE}
    ores_found = ore_ids & set(spawn_blocks)
    lava_low = any(spawn_blocks[c * WORLD_HEIGHT + y] == LAVA
                   for c in range(256) for y in range(10))
    water_mid = any(spawn_blocks[c * WORLD_HEIGHT + y] == WATER
                    for c in range(256) for y in range(12, 49))
    assert len(ores_found) >= 1, "no ore ids found in spawn chunk"
    assert lava_low, "no LAVA block at y<10 in spawn chunk"
    assert water_mid, "no WATER block at mid-depth in spawn chunk"

    print(f"self-check OK: relief {min(heights)}..{max(heights)} "
          f"({len(heights)} heights), {snow_cols} snow column(s) >= y{SNOW_LINE}, "
          f"region max y {region_max} < {WORLD_HEIGHT}, spawn solid at y{sy} (SpawnY {SPAWN_Y}), "
          f"strata ores {sorted(ores_found)} + lava/water, "
          f"caves {cave_air} air cells (largest vol {largest_cave}), "
          f"lakes {water_cells} water cells (largest pool {biggest_lake}), "
          f"'Claude World' preserved")

if __name__ == "__main__":
    main()
