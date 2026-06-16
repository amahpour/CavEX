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

# Run the demo, retrying on a too-short capture. CavEX has a known, pre-existing
# intermittent entity-dict assert during world load (unrelated to this rig --
# it is in the entity iteration path and is also reachable in normal play;
# tracked separately). It aborts the process before the script finishes, so we
# just retry until we get a full-length capture. MIN_FRAMES is a sanity floor.
ATTEMPTS="${CAPTURE_ATTEMPTS:-6}"
MIN_FRAMES="${CAPTURE_MIN_FRAMES:-30}"
N=0
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
	if [[ "$rc" -eq 0 && "$N" -ge "$MIN_FRAMES" ]]; then
		break
	fi
	echo "   attempt $attempt: rc=$rc, frames=$N (< $MIN_FRAMES or crashed) -- retrying" >&2
done

if [[ "$N" -lt "$MIN_FRAMES" ]]; then
	echo "error: never captured >= $MIN_FRAMES frames after $ATTEMPTS attempts" >&2
	exit 1
fi
echo "==> Captured $N frames"

# Skip the first frames (world still meshing -> black) and thin for a compact
# clip. Tunable via env: GIF_START / GIF_STEP / GIF_SCALE / GIF_FPS.
GIF_START="${GIF_START:-16}"
GIF_STEP="${GIF_STEP:-2}"
GIF_SCALE="${GIF_SCALE:-360}"
GIF_FPS="${GIF_FPS:-15}"

echo "==> Stitching GIF -> $ASSET"
bash "$ROOT_DIR/scripts/make_demo_gif.sh" "$RUN_DIR" "$ROOT_DIR/$ASSET" \
	--start "$GIF_START" --step "$GIF_STEP" --scale "$GIF_SCALE" --fps "$GIF_FPS"

MP4_ASSET=""
if [[ "$WANT_MP4" -eq 1 ]]; then
	MP4_ASSET="${ASSET%.gif}.mp4"
	echo "==> Stitching MP4 -> $MP4_ASSET"
	bash "$ROOT_DIR/scripts/make_demo_gif.sh" "$RUN_DIR" "$ROOT_DIR/$MP4_ASSET" --mp4 \
		--start "$GIF_START" --step "$GIF_STEP" --scale "$GIF_SCALE" --fps "$GIF_FPS" || \
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
