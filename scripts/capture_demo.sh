#!/usr/bin/env bash
# End-to-end gameplay capture: run a demo-replay script headless, stitch the
# dumped frames into a GIF, upload it to the `verification` GitHub prerelease,
# and print the markdown embed line for a PR body. Part of issue #66.
#
# Nothing binary is committed: the GIF lives only as a release asset.
#
# Usage:
#   scripts/capture_demo.sh [--demo SCRIPT] [--asset NAME] [--no-upload]
#                           [--mp4] [--world DIR] [--timeout SECS]
#
#   --demo SCRIPT   demo script to replay     (default: demos/walk-forward.txt)
#   --asset NAME    release asset filename    (default: issue-66-01-demo.gif)
#   --world DIR     world save to copy in     (default: a fresh scratch world
#                                              from gen_world.py -- the real
#                                              save is never touched)
#   --timeout SECS  hard run cap              (default: 45)
#   --mp4           also produce/upload an MP4 alongside the GIF
#   --no-upload     stitch only; skip the gh release upload
#
# Requires: a built PC binary (build_pc/cavex, Debug) and a stitcher
# (ffmpeg preferred; see make_demo_gif.sh). Upload needs the `gh` CLI.

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT_DIR"

DEMO="demos/walk-forward.txt"
ASSET="issue-66-01-demo.gif"
WORLD=""
TIMEOUT=45
WANT_MP4=0
UPLOAD=1
RELEASE_TAG="verification"

while [[ $# -gt 0 ]]; do
	case "$1" in
		--demo) DEMO="$2"; shift 2 ;;
		--asset) ASSET="$2"; shift 2 ;;
		--world) WORLD="$2"; shift 2 ;;
		--timeout) TIMEOUT="$2"; shift 2 ;;
		--mp4) WANT_MP4=1; shift ;;
		--no-upload) UPLOAD=0; shift ;;
		-h|--help) sed -n '2,30p' "$0"; exit 0 ;;
		*) echo "unknown arg: $1" >&2; exit 1 ;;
	esac
done

BIN="$ROOT_DIR/build_pc/cavex"
if [[ ! -x "$BIN" ]]; then
	echo "error: $BIN not found -- build the PC Debug target first:" >&2
	echo "  (cd build_pc && cmake .. -DCMAKE_BUILD_TYPE=Debug && make -j\$(nproc))" >&2
	exit 1
fi

if [[ ! -f "$DEMO" ]]; then
	echo "error: demo script not found: $DEMO" >&2
	exit 1
fi
DEMO_ABS="$(cd "$(dirname "$DEMO")" && pwd)/$(basename "$DEMO")"

# Build a fully ISOLATED run dir so the real Claude World save (and the normal
# build_pc/run/saves) is never read or mutated by the capture. The temp run dir
# has its own config whose paths.worlds points at a saves dir holding ONLY a
# scratch world, so AUTOPLAY (which enters the first world) can only pick ours.
RUN_DIR="$(mktemp -d)"
mkdir -p "$RUN_DIR/saves"
ln -sfn "$ROOT_DIR/assets" "$RUN_DIR/assets"
cat > "$RUN_DIR/config.json" <<JSON
{ "paths": { "texturepack": "$RUN_DIR/assets", "worlds": "$RUN_DIR/saves" },
$(sed -n '/"input"/,/}/p' "$ROOT_DIR/config_pc.json")
}
JSON

if [[ -n "$WORLD" ]]; then
	cp -r "$WORLD" "$RUN_DIR/saves/world"
else
	TMP_WORLD="$(mktemp -d)"
	echo "==> Generating a fresh scratch world (real saves untouched)"
	python3 "$ROOT_DIR/gen_world.py" "$TMP_WORLD" >/dev/null
	cp -r "$TMP_WORLD/world" "$RUN_DIR/saves/world"
	rm -rf "$TMP_WORLD"
fi

cleanup() { rm -rf "$RUN_DIR"; }
trap cleanup EXIT

# Stitch tuning (issue #66 follow-up: smooth/continuous clips).
#
#   GIF_START  Skip the world-load / settle frames at the head of the capture
#              (they are a held black/static view that otherwise plays as an
#              opening pause). The bundled demo does not start moving until tick
#              20, so 20 drops the load+settle cleanly. Tunable via env.
#   GIF_STEP   Keep every Nth frame. Default 1: we no longer thin by 2, because
#              the stitcher now DEDUPLICATES held frames itself (see GIF_DEDUP);
#              thinning real motion as well made the walk look choppy/fast. Bump
#              this only to shrink a very long clip.
#   GIF_DEDUP  1 (default) drops consecutive near-identical frames -> no static
#              pauses; 0 keeps every frame (legacy). Forwarded to make_demo_gif.
#
# Trailing idle is handled the same way: the demo ends on motion, and any held
# tail frames are collapsed by the dedup pass, so the clip ends on the last
# frame that actually changed.
GIF_START="${GIF_START:-20}"
GIF_STEP="${GIF_STEP:-1}"
GIF_SCALE="${GIF_SCALE:-360}"
GIF_FPS="${GIF_FPS:-15}"
GIF_DEDUP="${GIF_DEDUP:-1}"

# Run the demo, retrying on a too-short OR too-static capture. Two reasons a
# capture can be unusable, both handled by retrying:
#  1. CavEX has a known, pre-existing intermittent entity-dict assert during
#     world load (unrelated to this rig; reachable in normal play too). It
#     aborts before the script finishes -> short capture.
#  2. Frame-duplication under load: autoshot frames are keyed to the 20 Hz demo
#     tick, but when headless rendering (vblank_mode=0) runs slower than 20 fps
#     -- e.g. the host is busy -- main.c writes the SAME framebuffer for every
#     tick that elapsed in one slow frame. A heavily loaded run can thus be
#     mostly byte-identical frames; dedup would collapse it to a stutter. So we
#     also require enough UNIQUE (real-motion) frames after GIF_START, otherwise
#     the clip would not be smooth. MIN_MOTION is that floor.
ATTEMPTS="${CAPTURE_ATTEMPTS:-6}"
MIN_FRAMES="${CAPTURE_MIN_FRAMES:-30}"
MIN_MOTION="${CAPTURE_MIN_MOTION:-24}"

# Count frames after START whose mean per-pixel diff vs the previous KEPT frame
# exceeds the dedup threshold -- i.e. how many frames survive dedup as motion.
count_motion_frames() {
	python3 - "$RUN_DIR" "$GIF_START" "$GIF_STEP" "${DEDUP_THRESHOLD:-2.0}" <<'PY'
import sys, glob, os, re
try:
	from PIL import Image, ImageChops, ImageStat
except ImportError:
	print(-1); sys.exit(0)  # no Pillow -> skip the gate (fall back to frame count)
d, start, step, thr = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), float(sys.argv[4])
def idx(p):
	m = re.search(r"autoshot_(\d+)\.png$", os.path.basename(p)); return int(m.group(1)) if m else 0
paths = sorted(glob.glob(os.path.join(d, "autoshot_*.png")), key=idx)[start::max(1, step)]
kept = None; motion = 0
for p in paths:
	im = Image.open(p).convert("RGB")
	if kept is None:
		kept = im; motion = 1; continue
	md = sum(ImageStat.Stat(ImageChops.difference(kept, im)).mean) / 3
	if md > thr:
		motion += 1; kept = im
print(motion)
PY
}

N=0
MOTION=0
for attempt in $(seq 1 "$ATTEMPTS"); do
	rm -f "$RUN_DIR"/autoshot_*.png
	echo "==> Running demo '$DEMO' (attempt $attempt/$ATTEMPTS, timeout ${TIMEOUT}s)"
	rc=0
	(
		cd "$RUN_DIR"
		CAVEX_DEMO="$DEMO_ABS" CAVEX_AUTOPLAY=1 CAVEX_AUTOSHOT=2 vblank_mode=0 \
			timeout "$TIMEOUT" "$BIN"
	) || rc=$?
	N="$(find "$RUN_DIR" -maxdepth 1 -name 'autoshot_*.png' | wc -l)"
	if [[ "$rc" -ne 0 || "$N" -lt "$MIN_FRAMES" ]]; then
		echo "   attempt $attempt: rc=$rc, frames=$N (< $MIN_FRAMES or crashed) -- retrying" >&2
		continue
	fi
	MOTION="$(count_motion_frames)"
	# MOTION == -1 means Pillow is unavailable: we cannot measure, so accept on
	# the frame-count floor alone (ffmpeg/convert path will still dedup).
	if [[ "$MOTION" -eq -1 || "$MOTION" -ge "$MIN_MOTION" ]]; then
		echo "==> Captured $N frames ($MOTION real-motion frames after start $GIF_START)"
		break
	fi
	echo "   attempt $attempt: only $MOTION/$MIN_MOTION real-motion frames" \
		"(slow/frozen render -- frames duplicated) -- retrying" >&2
done

if [[ "$N" -lt "$MIN_FRAMES" ]]; then
	echo "error: never captured >= $MIN_FRAMES frames after $ATTEMPTS attempts" >&2
	exit 1
fi
if [[ "$MOTION" -ne -1 && "$MOTION" -lt "$MIN_MOTION" ]]; then
	echo "error: never captured >= $MIN_MOTION real-motion frames after $ATTEMPTS" \
		"attempts (host too busy? rendering slower than 20 fps duplicates frames)" >&2
	exit 1
fi

DEDUP_FLAG="--dedup"
[[ "$GIF_DEDUP" -eq 0 ]] && DEDUP_FLAG="--no-dedup"

echo "==> Stitching GIF -> $ASSET"
bash "$ROOT_DIR/scripts/make_demo_gif.sh" "$RUN_DIR" "$ROOT_DIR/$ASSET" \
	--start "$GIF_START" --step "$GIF_STEP" --scale "$GIF_SCALE" --fps "$GIF_FPS" \
	"$DEDUP_FLAG"

MP4_ASSET=""
if [[ "$WANT_MP4" -eq 1 ]]; then
	MP4_ASSET="${ASSET%.gif}.mp4"
	echo "==> Stitching MP4 -> $MP4_ASSET"
	bash "$ROOT_DIR/scripts/make_demo_gif.sh" "$RUN_DIR" "$ROOT_DIR/$MP4_ASSET" --mp4 \
		--start "$GIF_START" --step "$GIF_STEP" --scale "$GIF_SCALE" --fps "$GIF_FPS" \
		"$DEDUP_FLAG" || \
		echo "  (MP4 skipped: needs ffmpeg)" >&2
fi

# Clean the frames so they are never accidentally committed.
rm -f "$RUN_DIR"/autoshot_*.png

if [[ "$UPLOAD" -eq 1 ]]; then
	REPO="$(gh repo view --json nameWithOwner -q .nameWithOwner 2>/dev/null || echo amahpour/CavEX)"
	echo "==> Uploading to $REPO release '$RELEASE_TAG'"
	gh release upload "$RELEASE_TAG" "$ROOT_DIR/$ASSET" --clobber
	[[ -n "$MP4_ASSET" && -f "$ROOT_DIR/$MP4_ASSET" ]] && \
		gh release upload "$RELEASE_TAG" "$ROOT_DIR/$MP4_ASSET" --clobber

	BASE="https://github.com/$REPO/releases/download/$RELEASE_TAG"
	echo
	echo "Markdown embed for the PR body:"
	echo "![scripted demo]($BASE/$ASSET)"
	[[ -n "$MP4_ASSET" && -f "$ROOT_DIR/$MP4_ASSET" ]] && \
		echo "[MP4]($BASE/$MP4_ASSET)"
else
	echo
	echo "Stitched (not uploaded): $ROOT_DIR/$ASSET"
fi
