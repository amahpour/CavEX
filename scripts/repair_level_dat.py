#!/usr/bin/env python3
"""Rebuild a lost/truncated level.dat for a world whose region file survived.

CavEX truncates level.dat when it opens it for writing, so a crash mid-save
leaves a 0-byte level.dat next to a perfectly good r.0.0.mcr: the world vanishes
from the world list even though every block built in it is still on disk.

This reads the ACTUAL surface height out of the region file and writes a fresh
level.dat spawning the player on top of it. Spawn height matters: CavEX stores
Player.Pos.y as the EYE position and derives feet as Pos.y - EYE_HEIGHT, so a
guessed spawn buries the player in the ground, wedged in place.

    python3 scripts/repair_level_dat.py <world-dir> [--name NAME] [--dry-run]
"""
import argparse
import gzip
import struct
import sys
import time
import zlib
from pathlib import Path

EYE_HEIGHT = 1.62      # must match source/entity/entity_local_player.c
TAG_END, TAG_BYTE, TAG_SHORT, TAG_INT, TAG_LONG = 0, 1, 2, 3, 4
TAG_FLOAT, TAG_DOUBLE, TAG_STRING, TAG_LIST, TAG_COMPOUND = 5, 6, 8, 9, 10
CHUNK_H = 128          # Beta 1.7.3 world height

# ---------------- Blocks extraction ----------------
# A chunk's NBT carries Entities/TileEntities lists whose element types a
# minimal reader has no reason to understand, and Blocks is the only field
# needed here -- so find that byte array directly rather than walking the tree.
TAG_BYTE_ARRAY = 7


def extract_blocks(raw: bytes):
    """The Level.Blocks byte array, or None."""
    key = bytes([TAG_BYTE_ARRAY]) + struct.pack(">H", 6) + b"Blocks"
    i = raw.find(key)
    if i < 0:
        return None
    i += len(key)
    (n,) = struct.unpack(">i", raw[i:i + 4])
    i += 4
    if n <= 0 or i + n > len(raw):
        return None
    return raw[i:i + n]


# ---------------- McRegion ----------------
def read_chunk(region: Path, cx: int, cz: int):
    """Decompressed chunk NBT, or None if that slot is empty."""
    with region.open("rb") as f:
        f.seek(4 * ((cx & 31) + (cz & 31) * 32))
        loc = f.read(4)
        if len(loc) != 4:
            return None
        off = int.from_bytes(loc[:3], "big")
        if off == 0:
            return None
        f.seek(off * 4096)
        head = f.read(5)
        if len(head) != 5:
            return None
        length = int.from_bytes(head[:4], "big")
        comp = head[4]
        data = f.read(length - 1)
    if comp == 1:
        return gzip.decompress(data)
    if comp == 2:
        return zlib.decompress(data)
    raise ValueError(f"unknown chunk compression {comp}")


def surface_y(blocks: bytes, lx: int, lz: int):
    """Highest solid block y in a column, or None if the column is all air.

    Beta chunk Blocks are YZX-ordered: index = y + z*128 + x*2048."""
    base = lz * CHUNK_H + lx * 2048
    for y in range(CHUNK_H - 1, -1, -1):
        if blocks[base + y] != 0:
            return y
    return None


def find_spawn(region: Path):
    """A standable column: scan chunks outward from the origin, take the middle
    of the first populated one and the highest solid block under open sky."""
    for radius in range(0, 16):
        for cx in range(-radius, radius + 1):
            for cz in range(-radius, radius + 1):
                if max(abs(cx), abs(cz)) != radius:
                    continue
                try:
                    raw = read_chunk(region, cx, cz)
                except Exception:
                    continue
                if not raw:
                    continue
                blocks = extract_blocks(raw)
                if not blocks:
                    continue
                # A 128-tall (Wii/legacy) chunk is 16*16*128 = 32768 bytes. A
                # 256-tall PC chunk is double that and is NOT loadable on Wii.
                if len(blocks) != 16 * 16 * CHUNK_H:
                    continue
                for lx, lz in ((8, 8), (4, 4), (12, 12), (0, 0)):
                    y = surface_y(blocks, lx, lz)
                    if y is not None and 1 <= y < CHUNK_H - 3:
                        return cx * 16 + lx, y, cz * 16 + lz
    return None


# ---------------- NBT writing (mirrors gen_world.py's writers) ----------------
def _name(n):
    b = n.encode()
    return struct.pack(">h", len(b)) + b


def t_byte(n, v):
    return bytes([TAG_BYTE]) + _name(n) + struct.pack(">b", v)


def t_short(n, v):
    return bytes([TAG_SHORT]) + _name(n) + struct.pack(">h", v)


def t_int(n, v):
    return bytes([TAG_INT]) + _name(n) + struct.pack(">i", v)


def t_long(n, v):
    return bytes([TAG_LONG]) + _name(n) + struct.pack(">q", v)


def t_string(n, v):
    b = v.encode()
    return bytes([TAG_STRING]) + _name(n) + struct.pack(">h", len(b)) + b


def t_list(n, et, items):
    return (bytes([TAG_LIST]) + _name(n) + bytes([et])
            + struct.pack(">i", len(items)) + b"".join(items))


def t_compound(n, items):
    return bytes([TAG_COMPOUND]) + _name(n) + b"".join(items) + bytes([TAG_END])


def p_double(v):
    return struct.pack(">d", v)


def p_float(v):
    return struct.pack(">f", v)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("world", type=Path, help="world dir containing region/")
    ap.add_argument("--name", default=None, help="LevelName (default: dir name)")
    ap.add_argument("--dry-run", action="store_true")
    a = ap.parse_args()

    region_dir = a.world / "region"
    regions = sorted(region_dir.glob("r.*.mcr")) if region_dir.is_dir() else []
    if not regions:
        sys.exit(f"no region files under {region_dir}")
    region = regions[0]

    spot = find_spawn(region)
    if not spot:
        sys.exit(f"no standable column found in {region}")
    sx, surface, sz = spot
    feet = surface + 1
    print(f"region:  {region} ({region.stat().st_size} bytes)")
    print(f"spawn:   x={sx} z={sz}, surface block y={surface}, feet y={feet}")

    level = a.world / "level.dat"
    old = level.stat().st_size if level.exists() else 0
    print(f"level.dat currently {old} bytes")
    if a.dry_run:
        print("(dry run, nothing written)")
        return

    player = t_compound("Player", [
        t_short("Health", 20),
        t_int("Dimension", 0),
        t_byte("gameMode", 0),
        # Pos.y is the EYE position; feet are derived as Pos.y - EYE_HEIGHT.
        t_list("Pos", TAG_DOUBLE, [p_double(sx + 0.5),
                                   p_double(feet + 0.38 + EYE_HEIGHT),
                                   p_double(sz + 0.5)]),
        t_list("Rotation", TAG_FLOAT, [p_float(0.0), p_float(15.0)]),
        t_list("Motion", TAG_DOUBLE, [p_double(0.0)] * 3),
        t_list("Inventory", TAG_COMPOUND, []),
    ])
    data = t_compound("Data", [
        t_string("LevelName", a.name or a.world.name),
        t_long("Time", 0),
        t_long("LastPlayed", int(time.time() * 1000)),
        t_long("SizeOnDisk", sum(r.stat().st_size for r in regions)),
        t_long("RandomSeed", 42),
        t_int("SpawnX", sx), t_int("SpawnY", feet), t_int("SpawnZ", sz),
        t_int("version", 19132),
        player,
    ])
    if level.exists() and old > 0:
        level.rename(level.with_suffix(".dat.bak"))
    level.write_bytes(gzip.compress(t_compound("", [data])))
    print(f"wrote {level} ({level.stat().st_size} bytes)")


if __name__ == "__main__":
    main()
