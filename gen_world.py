#!/usr/bin/env python3
"""Generate a minimal Minecraft Beta 1.7.3 (McRegion) world for CavEX.

Pure stdlib. Produces:
  out/world/level.dat          gzipped NBT, fields CavEX's level_archive.c reads
  out/world/region/r.0.0.mcr   1024 flat-grass chunks, zlib-compressed NBT

Beta block array order: index = y + z*128 + x*128*16  (XZY)
Nibble arrays (Data/SkyLight/BlockLight): even index -> low nibble.
"""
import gzip, struct, sys, time, zlib
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
AIR, STONE, GRASS, DIRT, BEDROCK, LOG, LEAVES = 0, 1, 2, 3, 7, 17, 18
# Beta 1.7.3 ids below are all registered + non-NULL in
# source/block/blocks_data.h + source/block/blocks.c (verified).
WATER, LAVA = 9, 11                      # still source variants
GRAVEL = 13
GOLD_ORE, IRON_ORE, COAL_ORE = 14, 15, 16
LAPIS_ORE = 21
DIAMOND_ORE = 56
REDSTONE_ORE = 73
GROUND = 63          # y of grass surface
SPAWN = (24.5, 66.0, 24.5)
SEED = 42            # mirrors level.dat RandomSeed; all strata derive from it

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
      y < GROUND - 4    -> stone body, with scattered ores + rare lava/water
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

def column_blocks(wx, wz):
    """world-absolute column -> list of (y, block_id) above the flat layers"""
    extra = []
    # stone pillar landmark at (28, 24)
    if (wx, wz) == (28, 24):
        extra += [(y, STONE) for y in range(64, 68)]
    # "tree": log trunk at (20,20), leaf blob around it
    if (wx, wz) == (20, 20):
        extra += [(y, LOG) for y in range(64, 68)]
    if 18 <= wx <= 22 and 18 <= wz <= 22 and (wx, wz) != (20, 20):
        extra += [(y, LEAVES) for y in range(67, 70)]
    if 19 <= wx <= 21 and 19 <= wz <= 21:
        extra += [(70, LEAVES)]
    return extra

def build_chunk(cx, cz):
    blocks = bytearray(32768)
    data   = bytearray(16384)          # all 0
    skyl   = bytearray(16384)
    blockl = bytearray(16384)          # all 0
    hmap   = bytearray(256)

    for x in range(16):
        for z in range(16):
            wx, wz = cx * 16 + x, cz * 16 + z
            base = (x * 16 + z) * 128       # x*128*16 + z*128
            # Layered sub-surface: bedrock floor, stone body, ores + fluid
            # pockets (all deterministic from SEED via strata_block()).
            for y in range(0, GROUND - 4):  blocks[base + y] = strata_block(wx, wz, y)
            for y in range(GROUND - 4, GROUND): blocks[base + y] = DIRT
            blocks[base + GROUND] = GRASS
            top = GROUND
            for (y, bid) in column_blocks(wx, wz):
                blocks[base + y] = bid
                top = max(top, y)
            hmap[z * 16 + x] = top + 1
            # full skylight above (and at) the surface
            for y in range(top + 1, 128):
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
    ]
    player = t_compound("Player", [
        t_short("Health", 20),
        t_int("Dimension", 0),
        t_list("Pos", 6, [p_double(v) for v in SPAWN]),
        t_list("Rotation", 5, [p_float(135.0), p_float(15.0)]),
        t_list("Motion", 6, [p_double(0.0)] * 3),
        t_list("Inventory", 10, inv),
    ])
    data = t_compound("Data", [
        t_string("LevelName", "Claude World"),
        t_long("Time", 0),
        t_long("LastPlayed", int(time.time() * 1000)),
        t_long("SizeOnDisk", disk_size),
        t_long("RandomSeed", 42),
        t_int("SpawnX", 24), t_int("SpawnY", 64), t_int("SpawnZ", 24),
        t_int("version", 19132),
        player,
    ])
    path.write_bytes(gzip.compress(t_compound("", [data])))

def main():
    out = Path(sys.argv[1] if len(sys.argv) > 1 else "out") / "world"
    (out / "region").mkdir(parents=True, exist_ok=True)
    size = write_region(out / "region" / "r.0.0.mcr")
    write_level_dat(out / "level.dat", size)
    print(f"region: {size} bytes, level.dat: {(out/'level.dat').stat().st_size} bytes")

    # -------- self-check: re-parse what we wrote --------
    lvl = gzip.decompress((out / "level.dat").read_bytes())
    assert b"LevelName" in lvl and b"Claude World" in lvl and b"Inventory" in lvl
    reg = (out / "region" / "r.0.0.mcr").read_bytes()
    off = struct.unpack(">i", reg[(24 // 16 + (24 // 16) * 32) * 4:][:4])[0]  # spawn chunk (1,1)
    sec, cnt = off >> 8, off & 0xFF
    ln = struct.unpack(">i", reg[sec * 4096:sec * 4096 + 4])[0]
    chunk = zlib.decompress(reg[sec * 4096 + 5: sec * 4096 + 4 + ln])
    assert b"Blocks" in chunk and b"SkyLight" in chunk and b"xPos" in chunk
    # spawn column: surface unchanged + bedrock floor
    bi = chunk.find(b"Blocks") + len("Blocks") + 4
    spawn_blocks = chunk[bi:bi + 32768]
    x_in, z_in = 24 % 16, 24 % 16
    col = (x_in * 16 + z_in) * 128
    assert spawn_blocks[col + GROUND] == GRASS, "grass missing at y=GROUND"
    assert spawn_blocks[col + 0] == BEDROCK, "bedrock missing at y=0"
    # strata across the whole spawn chunk: ores + fluid pockets are present
    ore_ids = {COAL_ORE, IRON_ORE, GOLD_ORE, REDSTONE_ORE, LAPIS_ORE, DIAMOND_ORE}
    ores_found = ore_ids & set(spawn_blocks)
    lava_low = any(spawn_blocks[c * 128 + y] == LAVA
                   for c in range(256) for y in range(10))
    water_mid = any(spawn_blocks[c * 128 + y] == WATER
                    for c in range(256) for y in range(12, 49))
    assert len(ores_found) >= 1, "no ore ids found in spawn chunk"
    assert lava_low, "no LAVA block at y<10 in spawn chunk"
    assert water_mid, "no WATER block at mid-depth in spawn chunk"
    print(f"self-check OK: level.dat parses 'Claude World'; spawn chunk has "
          f"grass at y={GROUND}, bedrock at y=0, ores {sorted(ores_found)}, "
          f"lava (y<10) and water (mid-depth)")

if __name__ == "__main__":
    main()
