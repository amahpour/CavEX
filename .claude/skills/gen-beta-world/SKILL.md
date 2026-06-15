---
name: gen-beta-world
description: Generate a valid Minecraft Beta 1.7.3 (McRegion) world for CavEX with pure-stdlib Python (gen_world.py) and stage it for the Wii (SD image) or PC build. Use when the user needs a new/clean world, wants different terrain/spawn/inventory, or a save "won't show up in the world list" (usually an NBT formatting bug). CavEX has NO world generation of its own — without a staged save the world list is empty.
argument-hint: "[output-dir]"
---

# Generate a Beta 1.7.3 world

## Purpose

CavEX only *loads* Beta-era saves. `~/code/CavEX/gen_world.py` builds one from
scratch (no Mojang assets): `level.dat` (gzipped NBT with player pos/rotation/
health/inventory) + `region/r.0.0.mcr` (1024 flat-grass chunk columns, tree +
stone-pillar landmarks at spawn, stocked hotbar).

## Steps

1. **Generate + self-check** (validates structure and spawn-column blocks):

   ```bash
   cd ~/code/CavEX && python3 gen_world.py /tmp/cavexworld
   # -> /tmp/cavexworld/world/{level.dat, region/r.0.0.mcr}
   ```

   Knobs are constants in the script: `GROUND`, `SPAWN`, `column_blocks()`
   for landmarks, the `inv` list for starting hotbar (id/Count/Slot),
   `LevelName`.

2. **Stage for the Wii build** (Dolphin SD image; kill Dolphin first):

   ```bash
   pkill -9 -x dolphin-emu-nog; sleep 1
   SD="$HOME/.local/share/dolphin-emu/Load/WiiSD.raw"
   mcopy -i "$SD" -s -o /tmp/cavexworld/world ::/saves/
   mdir  -i "$SD" ::/saves/world    # expect level.dat + region/
   ```

3. **Stage for the PC build**:

   ```bash
   cp -r /tmp/cavexworld/world ~/code/CavEX/build_pc/run/saves/
   ```

4. **Verify NBT against CavEX's own parser before blaming the game** — cNBT
   compiles natively (host has no zlib headers; borrow the portable PPC one
   and link the runtime .so directly):

   ```bash
   gcc -o /tmp/nbtcheck /tmp/nbt_harness.c ~/code/CavEX/source/cNBT/{nbt_parsing.c,nbt_loading.c,nbt_treeops.c,nbt_util.c,buffer.c} \
     -I ~/code/CavEX/source/cNBT -I /opt/devkitpro/portlibs/ppc/include \
     /lib/x86_64-linux-gnu/libz.so.1
   ```

   (Write a tiny main that calls `nbt_parse_path` + `nbt_find_by_path`;
   parse failure returns NULL with errno −1.)

## Format rules that actually bit us

- **List-element compounds are BARE payloads** — no tag id, no name header,
  just children + `0x00` terminator. Writing full named compounds inside a
  TAG_List is the classic mistake; cNBT rejects the whole file (world
  silently missing from the Select World list).
- Beta block array order is XZY: `index = y + z*128 + x*128*16`; nibble
  arrays (Data/SkyLight/BlockLight) pack the even index in the LOW nibble.
- Set SkyLight 15 above the surface and a sane HeightMap, or the world loads
  pitch-black.
- McRegion chunk payload: 4-byte big-endian length, 1 compression byte
  (2 = zlib), then zlib data; offsets table entry = `(sector << 8) | count`,
  first data sector is 2.
- `level.dat` fields CavEX actually reads: `.Data.LevelName/Time/SizeOnDisk/
  LastPlayed/RandomSeed` and `.Data.Player.{Health,Pos,Rotation,Motion,
  Dimension,Inventory}`.
