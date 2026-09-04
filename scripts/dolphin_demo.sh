#!/usr/bin/env bash
# Run a scripted CavEX session inside headless Dolphin and collect frames.
#
# This is the reliable Wii/Dolphin test path: unlike the native PC build it runs
# under the real 24 MB MEM1 constraint and GX, so MEM1-only bugs (mesh OOM,
# fragmentation, "grass floats when you dig") actually reproduce. Input is a
# deterministic demo script staged on the SD card (env vars do not cross into
# the emulated Wii); eyes are Dolphin's own frame dump. The in-game debug
# overlay (memfree=/oom=/mesh) renders straight into those frames.
#
#   scripts/dolphin_demo.sh <demo-script> [seconds] [out-dir]
#
# Demo script tokens (one keyframe per line, @<tick> optional prefix):
#   FORWARD=1  BACKWARD=1  LEFT=1  RIGHT=1  JUMP=1  SNEAK=1
#   MINE=1  PLACE=1  INVENTORY=1  LOOK=<dx>,<dy>   (see source/platform/demo_input.c)
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SCRIPT="${1:?usage: dolphin_demo.sh <demo-script> [seconds] [out-dir]}"
SECS="${2:-60}"
OUT="${3:-$ROOT/build_pc/run/dolphin_demo}"
SD="${WII_SD:-$HOME/.local/share/dolphin-emu/Load/WiiSD.raw}"
DUMP="$HOME/.local/share/dolphin-emu/Dump/Frames"
CFG="$HOME/.config/dolphin-emu"

[ -f "$SCRIPT" ] || { echo "no demo script: $SCRIPT" >&2; exit 1; }
[ -f "$SD" ]     || { echo "no SD image: $SD" >&2; exit 1; }
command -v mcopy >/dev/null || { echo "need mtools (mcopy)" >&2; exit 1; }

if ps -eo comm --no-headers | grep -q '^dolphin'; then
	echo "a dolphin process is already running -- close it first" >&2; exit 1
fi

echo "==> Stage demo script onto SD as demo.txt"
mcopy -i "$SD" -o "$SCRIPT" ::/demo.txt
mcopy -i "$SD" -o "$ROOT/config.json" ::/config.json

echo "==> Enable Dolphin image frame dump"
mkdir -p "$CFG"
# DumpFramesAsImages writes individual PNGs; DumpFrames turns capture on.
python3 - "$CFG/GFX.ini" <<'PY'
import sys, pathlib
p = pathlib.Path(sys.argv[1]); t = p.read_text() if p.exists() else "[Settings]\n"
if "[Settings]" not in t: t = "[Settings]\n" + t
if "DumpFramesAsImages" in t:
    import re; t = re.sub(r"DumpFramesAsImages = \w+", "DumpFramesAsImages = True", t)
else:
    t = t.replace("[Settings]", "[Settings]\nDumpFramesAsImages = True", 1)
p.write_text(t)
PY
sed -i 's/^DumpFrames = .*/DumpFrames = True/' "$CFG/Dolphin.ini" 2>/dev/null || true
grep -q '^\[Movie\]' "$CFG/Dolphin.ini" || printf '\n[Movie]\nDumpFrames = True\n' >> "$CFG/Dolphin.ini"
grep -q '^DumpFrames = True' "$CFG/Dolphin.ini" || sed -i '/^\[Movie\]/a DumpFrames = True' "$CFG/Dolphin.ini"

# Automated runs do not want the real Wii Remote: its continuous scan floods
# the log and can outlive the run. Turn it off for the duration, restore after.
WNI="$CFG/WiimoteNew.ini"; DNI="$CFG/Dolphin.ini"
saved_scan="$(grep -m1 '^WiimoteContinuousScanning' "$DNI" 2>/dev/null || true)"
sed -i 's/^WiimoteContinuousScanning = True/WiimoteContinuousScanning = False/' "$DNI" 2>/dev/null || true
cleanup() {
	for pid in $(ps -eo pid,comm --no-headers | awk '$2 ~ /^dolphin/ {print $1}'); do
		kill "$pid" 2>/dev/null || true
	done
	[ -n "$saved_scan" ] && sed -i "s/^WiimoteContinuousScanning = False/$saved_scan/" "$DNI" 2>/dev/null || true
}
trap cleanup EXIT

rm -rf "$DUMP"; mkdir -p "$DUMP" "$OUT"
: > "$ROOT/dolphin-run.log"

echo "==> Run headless Dolphin for ${SECS}s (autoplay + scripted input)"
timeout "$SECS" dolphin-emu-nogui -p x11 -v Vulkan -e "$ROOT/CavEX.dol" \
	>"$ROOT/dolphin-run.log" 2>&1 || true

echo "==> Collect frames"
# Dolphin writes framedump_N.png (or a subdir). Copy a sampled set out.
found="$(find "$DUMP" -name '*.png' | sort)"
n="$(printf '%s\n' "$found" | grep -c . || true)"
if [ "$n" -eq 0 ]; then
	echo "no frames dumped -- check $ROOT/dolphin-run.log (did autoplay fire?)"
	grep -iE 'AUTOPLAY|demo|replay|error' "$ROOT/dolphin-run.log" | tail -5 || true
	exit 2
fi
# keep ~24 evenly-spaced NON-EMPTY frames (0-byte = capture cut off mid-write)
mapfile -t good < <(find "$DUMP" -name 'framedump_*.png' -size +0 \
	-printf '%f\n' | sed 's/framedump_//;s/.png//' | sort -n)
gn=${#good[@]}
[ "$gn" -eq 0 ] && { echo "frames dumped but all empty"; exit 2; }
step=$(( gn/24 > 0 ? gn/24 : 1 )); i=0
for idx in "${good[@]}"; do
	[ $(( i % step )) -eq 0 ] && cp "$DUMP/framedump_$idx.png" \
		"$OUT/$(printf 'f%04d_%s.png' "$i" "$idx")"
	i=$((i+1))
done
echo "==> $n frames dumped; sampled $(ls "$OUT"/f*.png | wc -l) into $OUT"
echo "    last few frames are the end-of-script state."
ls "$OUT"/f*.png | tail -3
