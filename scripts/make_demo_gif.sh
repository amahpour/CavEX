#!/usr/bin/env bash
# Stitch the autoshot_*.png frames dumped by a demo-replay run into an animated
# GIF (default) or MP4. Part of the gameplay capture rig (issue #66).
#
# ffmpeg is the preferred stitcher; if it is missing we fall back to ImageMagick
# `convert`, then to a pure-Pillow Python helper, so the rig works on hosts
# without ffmpeg. ffmpeg/convert/Pillow are dev-host tools only -- NOT a CI or
# game-build dependency.
#
# Usage:
#   scripts/make_demo_gif.sh [FRAMES_DIR] [OUTPUT] [--mp4] [--fps N] [--scale W]
#
#   FRAMES_DIR  directory of autoshot_*.png frames   (default: build_pc/run)
#   OUTPUT      output file                          (default: demo.gif / .mp4)
#   --mp4       write an MP4 instead of a GIF
#   --fps N     output frame rate                    (default: 20)
#   --scale W   scale to width W, preserving aspect  (default: 480)
#   --start N   skip the first N frames (loading)    (default: 0)
#   --step N    keep every Nth frame (thinning)      (default: 1)
#
# Examples:
#   scripts/make_demo_gif.sh
#   scripts/make_demo_gif.sh build_pc/run out.gif --fps 15 --scale 360
#   scripts/make_demo_gif.sh build_pc/run out.gif --start 16 --step 2
#   scripts/make_demo_gif.sh build_pc/run out.mp4 --mp4

set -euo pipefail

FRAMES_DIR="build_pc/run"
OUTPUT=""
WANT_MP4=0
FPS=20
SCALE=480
START=0
STEP=1

POSITIONAL=()
while [[ $# -gt 0 ]]; do
	case "$1" in
		--mp4) WANT_MP4=1; shift ;;
		--fps) FPS="$2"; shift 2 ;;
		--scale) SCALE="$2"; shift 2 ;;
		--start) START="$2"; shift 2 ;;
		--step) STEP="$2"; shift 2 ;;
		-h|--help) sed -n '2,32p' "$0"; exit 0 ;;
		*) POSITIONAL+=("$1"); shift ;;
	esac
done

[[ ${#POSITIONAL[@]} -ge 1 ]] && FRAMES_DIR="${POSITIONAL[0]}"
[[ ${#POSITIONAL[@]} -ge 2 ]] && OUTPUT="${POSITIONAL[1]}"

if [[ -z "$OUTPUT" ]]; then
	if [[ "$WANT_MP4" -eq 1 ]]; then OUTPUT="demo.mp4"; else OUTPUT="demo.gif"; fi
fi

if [[ ! -d "$FRAMES_DIR" ]]; then
	echo "error: frames dir not found: $FRAMES_DIR" >&2
	exit 1
fi

shopt -s nullglob
FRAMES=("$FRAMES_DIR"/autoshot_*.png)
shopt -u nullglob
if [[ ${#FRAMES[@]} -eq 0 ]]; then
	echo "error: no autoshot_*.png frames in $FRAMES_DIR" >&2
	echo "  run the rig first, e.g.:" >&2
	echo "  CAVEX_DEMO=demos/walk-forward.txt CAVEX_AUTOPLAY=1 CAVEX_AUTOSHOT=2 vblank_mode=0 ../cavex" >&2
	exit 1
fi
echo "Stitching ${#FRAMES[@]} frames from $FRAMES_DIR -> $OUTPUT (${FPS}fps, ${SCALE}px wide, start ${START}, step ${STEP})"

# ffmpeg frame selector: drop the first START frames, then keep every STEPth.
SELECT="select='gte(n\,${START})*not(mod(n-${START}\,${STEP}))',setpts=N/(${FPS}*TB)"

# ---- preferred: ffmpeg ------------------------------------------------------
if command -v ffmpeg >/dev/null 2>&1; then
	if [[ "$WANT_MP4" -eq 1 ]]; then
		ffmpeg -y -framerate "$FPS" -pattern_type glob \
			-i "$FRAMES_DIR/autoshot_*.png" \
			-vf "${SELECT},scale=${SCALE}:-2:flags=lanczos" \
			-r "$FPS" -c:v libx264 -pix_fmt yuv420p -movflags +faststart "$OUTPUT"
	else
		# Two-pass palette for clean GIF colours.
		PAL="$(mktemp --suffix=.png)"
		trap 'rm -f "$PAL"' EXIT
		ffmpeg -y -framerate "$FPS" -pattern_type glob \
			-i "$FRAMES_DIR/autoshot_*.png" \
			-vf "${SELECT},scale=${SCALE}:-1:flags=lanczos,palettegen=stats_mode=diff" \
			"$PAL"
		ffmpeg -y -framerate "$FPS" -pattern_type glob \
			-i "$FRAMES_DIR/autoshot_*.png" -i "$PAL" \
			-lavfi "${SELECT},scale=${SCALE}:-1:flags=lanczos [x]; [x][1:v] paletteuse=dither=bayer" \
			-r "$FPS" "$OUTPUT"
	fi
	echo "Wrote $OUTPUT"
	exit 0
fi

# ---- fallback: ImageMagick convert (GIF only) -------------------------------
if [[ "$WANT_MP4" -eq 0 ]] && command -v convert >/dev/null 2>&1; then
	echo "ffmpeg not found; using ImageMagick convert (GIF)" >&2
	# convert delay is in 1/100 s.
	DELAY=$(awk "BEGIN { printf \"%d\", 100 / $FPS }")
	# Apply start/step by selecting the frame list explicitly.
	mapfile -t ALL < <(ls "$FRAMES_DIR"/autoshot_*.png | sort)
	SEL=()
	for ((i=START; i<${#ALL[@]}; i+=STEP)); do SEL+=("${ALL[$i]}"); done
	# -coalesce before -layers Optimize: Optimize assumes coalesced input, so
	# feeding it resized full frames without coalescing first can ghost frames.
	convert -delay "$DELAY" -loop 0 "${SEL[@]}" \
		-coalesce -resize "${SCALE}x" -layers Optimize "$OUTPUT"
	echo "Wrote $OUTPUT"
	exit 0
fi

# ---- fallback: pure Pillow (GIF only) ---------------------------------------
if [[ "$WANT_MP4" -eq 0 ]] && python3 -c "import PIL" >/dev/null 2>&1; then
	echo "ffmpeg/convert not found; using Pillow (GIF)" >&2
	FPS="$FPS" SCALE="$SCALE" START="$START" STEP="$STEP" \
		OUTPUT="$OUTPUT" FRAMES_DIR="$FRAMES_DIR" \
		python3 "$(dirname "$0")/make_demo_gif_pillow.py"
	echo "Wrote $OUTPUT"
	exit 0
fi

echo "error: no stitcher available." >&2
echo "  install one of: ffmpeg (preferred), imagemagick, or python3-pil" >&2
[[ "$WANT_MP4" -eq 1 ]] && echo "  note: MP4 output requires ffmpeg." >&2
exit 1
