#!/usr/bin/env python3
"""Convert a 256-tall (PC) CavEX world into a 128-tall (Wii) one.

world.h: the Wii build is WORLD_HEIGHT 128, the PC build 256, and a chunk's
Blocks/Data/SkyLight/BlockLight arrays are sized by it -- so a PC save loads on
the Wii as void. This slices every column to its bottom 128 blocks, the exact
mirror of region_archive_migrate_legacy() (which pads 128 -> 256 on load).

Everything above y=127 is LOST. Run --survey first: it counts those blocks per
world and this refuses to convert a world that has any unless --force.

    python3 scripts/downconvert_world.py --survey <world-dir>...
    python3 scripts/downconvert_world.py <src-world-dir> <dst-world-dir> [--force]

Chunk NBT is edited surgically (the byte arrays are spliced in place, every
other tag is byte-identical), so no full NBT parser is needed and nothing else
in the chunk can be mangled. Entities are left as-is; one above y=127 gets the
engine's void/ground recovery.
"""
import argparse
import gzip
import shutil
import struct
import sys
import zlib
from pathlib import Path

OLD_H, NEW_H = 256, 128
COLS = 16 * 16
TAG_BYTE_ARRAY, TAG_INT, TAG_LIST, TAG_DOUBLE = 7, 3, 9, 6


# ---------------- McRegion ----------------
def iter_chunks(region: Path):
    """Yield (slot, timestamp, compression, payload) for every stored chunk."""
    data = region.read_bytes()
    for slot in range(1024):
        loc = data[slot * 4:slot * 4 + 4]
        off = int.from_bytes(loc[:3], "big")
        if off == 0:
            continue
        ts = int.from_bytes(data[4096 + slot * 4:4096 + slot * 4 + 4], "big")
        base = off * 4096
        length = int.from_bytes(data[base:base + 4], "big")
        comp = data[base + 4]
        yield slot, ts, comp, data[base + 5:base + 4 + length]


def write_region(path: Path, chunks):
    """chunks: list of (slot, timestamp, compression, payload)."""
    locs = bytearray(4096)
    times = bytearray(4096)
    body = bytearray()
    sector = 2
    for slot, ts, comp, payload in chunks:
        blob = (len(payload) + 1).to_bytes(4, "big") + bytes([comp]) + payload
        pad = (-len(blob)) % 4096
        blob += b"\0" * pad
        n = len(blob) // 4096
        locs[slot * 4:slot * 4 + 4] = sector.to_bytes(3, "big") + bytes([n])
        times[slot * 4:slot * 4 + 4] = ts.to_bytes(4, "big")
        body += blob
        sector += n
    path.write_bytes(bytes(locs) + bytes(times) + bytes(body))


def decompress(comp, payload):
    return gzip.decompress(payload) if comp == 1 else zlib.decompress(payload)


def compress(comp, raw):
    return gzip.compress(raw) if comp == 1 else zlib.compress(raw)


# ---------------- surgical NBT ----------------
def find_array(raw: bytes, name: str):
    """(start_of_payload, length) of TAG_Byte_Array `name`, or None."""
    key = bytes([TAG_BYTE_ARRAY]) + struct.pack(">H", len(name)) + name.encode()
    i = raw.find(key)
    if i < 0:
        return None
    i += len(key)
    (n,) = struct.unpack(">i", raw[i:i + 4])
    return i, n


def splice_array(raw: bytes, name: str, new_payload: bytes) -> bytes:
    i, n = find_array(raw, name)
    return raw[:i] + struct.pack(">i", len(new_payload)) + new_payload + raw[i + 4 + n:]


def slice_bytes(arr: bytes) -> bytes:
    """Keep the bottom NEW_H of each column's OLD_H bytes (XZY layout)."""
    out = bytearray(COLS * NEW_H)
    for c in range(COLS):
        out[c * NEW_H:(c + 1) * NEW_H] = arr[c * OLD_H:c * OLD_H + NEW_H]
    return bytes(out)


def slice_nibbles(arr: bytes) -> bytes:
    """Same for a half-byte-per-block array. Column runs are 128/64 bytes, so
    every column boundary is byte-aligned and this is a byte copy too."""
    out = bytearray(COLS * NEW_H // 2)
    src_run, dst_run = OLD_H // 2, NEW_H // 2
    for c in range(COLS):
        out[c * dst_run:(c + 1) * dst_run] = arr[c * src_run:c * src_run + dst_run]
    return bytes(out)


def heightmap_from_blocks(blocks: bytes) -> bytes:
    """Highest non-air y + 1 per column (0 for an empty column), the Beta
    convention region_archive/lighting_heightmap_update maintain."""
    hm = bytearray(COLS)
    for c in range(COLS):
        col = blocks[c * NEW_H:(c + 1) * NEW_H]
        h = 0
        for y in range(NEW_H - 1, -1, -1):
            if col[y] != 0:
                h = y + 1
                break
        hm[c] = h
    return bytes(hm)


def blocks_above(blocks: bytes) -> int:
    n = 0
    for c in range(COLS):
        n += sum(1 for b in blocks[c * OLD_H + NEW_H:(c + 1) * OLD_H] if b)
    return n


def convert_chunk(raw: bytes):
    """Return (new_raw, blocks_lost) or raise if the chunk is not 256-tall."""
    pos = find_array(raw, "Blocks")
    if not pos:
        raise ValueError("no Blocks array")
    i, n = pos
    if n == COLS * NEW_H:
        raise ValueError("already 128-tall")
    if n != COLS * OLD_H:
        raise ValueError(f"unexpected Blocks length {n}")
    blocks = raw[i + 4:i + 4 + n]
    lost = blocks_above(blocks)
    new_blocks = slice_bytes(blocks)

    out = splice_array(raw, "Blocks", new_blocks)
    for name in ("Data", "SkyLight", "BlockLight"):
        j, m = find_array(out, name)
        if m != COLS * OLD_H // 2:
            raise ValueError(f"unexpected {name} length {m}")
        out = splice_array(out, name, slice_nibbles(out[j + 4:j + 4 + m]))
    if find_array(out, "HeightMap"):
        out = splice_array(out, "HeightMap", heightmap_from_blocks(new_blocks))
    return out, lost


# ---------------- level.dat ----------------
def patch_level_dat(src: Path, dst: Path):
    """Clamp SpawnY and Player.Pos.y below the new ceiling, surgically, so the
    inventory and everything else survive byte-for-byte."""
    raw = bytearray(gzip.decompress(src.read_bytes()))
    key = bytes([TAG_INT]) + struct.pack(">H", 6) + b"SpawnY"
    i = raw.find(key)
    if i >= 0:
        i += len(key)
        (y,) = struct.unpack(">i", raw[i:i + 4])
        if y >= NEW_H:
            raw[i:i + 4] = struct.pack(">i", NEW_H - 2)
    key = bytes([TAG_LIST]) + struct.pack(">H", 3) + b"Pos" + bytes([TAG_DOUBLE]) + struct.pack(">i", 3)
    i = raw.find(key)
    if i >= 0:
        i += len(key) + 8  # skip x
        (y,) = struct.unpack(">d", raw[i:i + 8])
        if y >= NEW_H - 2:
            raw[i:i + 8] = struct.pack(">d", NEW_H - 4.0)
    dst.write_bytes(gzip.compress(bytes(raw)))


# ---------------- driver ----------------
def survey(world: Path):
    total_lost = chunks = 0
    already = False
    for region in sorted((world / "region").glob("r.*.mcr")):
        for slot, ts, comp, payload in iter_chunks(region):
            raw = decompress(comp, payload)
            pos = find_array(raw, "Blocks")
            if not pos:
                continue
            i, n = pos
            chunks += 1
            if n == COLS * OLD_H:
                total_lost += blocks_above(raw[i + 4:i + 4 + n])
            elif n == COLS * NEW_H:
                already = True
    return chunks, (None if already else total_lost)


def convert(src: Path, dst: Path, force: bool):
    chunks, lost = survey(src)
    if lost is None:
        sys.exit(f"{src.name}: already 128-tall, nothing to do")
    print(f"{src.name}: {chunks} chunks, {lost} blocks above y=127 would be lost")
    if lost and not force:
        sys.exit("refusing: there are blocks above y=127 (use --force to slice them off)")
    if dst.exists():
        sys.exit(f"refusing to overwrite {dst}")
    (dst / "region").mkdir(parents=True)
    for region in sorted((src / "region").glob("r.*.mcr")):
        out = []
        for slot, ts, comp, payload in iter_chunks(region):
            raw = decompress(comp, payload)
            new_raw, _ = convert_chunk(raw)
            out.append((slot, ts, comp, compress(comp, new_raw)))
        write_region(dst / "region" / region.name, out)
    patch_level_dat(src / "level.dat", dst / "level.dat")
    for extra in src.glob("player*.dat"):
        shutil.copy(extra, dst / extra.name)
    print(f"wrote {dst} ({len(out)} chunks in {region.name})")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("paths", nargs="+", type=Path)
    ap.add_argument("--survey", action="store_true")
    ap.add_argument("--force", action="store_true")
    a = ap.parse_args()
    if a.survey:
        for w in a.paths:
            chunks, lost = survey(w)
            tag = "already 128-tall" if lost is None else f"{lost:6d} blocks above y=127"
            print(f"{w.name:16s} {chunks:4d} chunks  {tag}")
        return
    if len(a.paths) != 2:
        sys.exit("convert needs <src> <dst>")
    convert(a.paths[0], a.paths[1], a.force)


if __name__ == "__main__":
    main()
