#!/usr/bin/env bash
# Stitch the autoshot_*.png frames dumped by a demo-replay run into an animated
# GIF (default) or MP4. Part of the gameplay capture rig (issue #66).
#
# ffmpeg is the preferred stitcher; if it is missing we fall back to ImageMagick
# `convert`, then to a pure-Pillow Python helper, so the rig works on hosts
# without ffmpeg. ffmpeg/convert/Pillow are dev-host tools only -- NOT a CI or
# game-build dependency.
#
# SMOOTHNESS (issue #66 follow-up): every stitch path DEDUPLICATES consecutive
# near-identical frames before re-timing to a constant fps. The autoshot frames
# are keyed to the 20 Hz demo tick, so any tick where the view does not change
# (world still loading, or the player standing still) yields a frame identical
# to its predecessor. Re-timing those to even fps without dropping them replays
# a static still as a visible PAUSE. Dropping the dupes first, then re-timing,
# gives continuous motion. ffmpeg uses `mpdecimate`; the Pillow fallback does
# the same with a per-pixel mean-difference threshold. (ImageMagick `convert`
# has no robust per-frame dedup primitive, so that fallback documents that
# ffmpeg / Pillow are the smooth paths -- see below.)
#
# Usage:
#   scripts/make_demo_gif.sh [FRAMES_DIR] [OUTPUT] [--mp4] [--fps N] [--scale W]
#
#   FRAMES_DIR  directory of autoshot_*.png frames   (default: build_pc/run)
#   OUTPUT      output file                          (default: demo.gif / .mp4)
#   --mp4       write an MP4 instead of a GIF
#   --fps N     output frame rate                    (default: 20)
#   --scale W   scale to width W, preserving aspect  (default: 480)
#   --start N   skip the first N frames by INDEX     (default: 0)
#   --step N    keep every Nth frame (thinning)      (default: 1)
#
#   --start/--step are a 0-based INDEX into the sorted frame list (identical in
#   the ffmpeg and Pillow/convert paths), NOT a frame/tick number. autoshot
#   files are tick-numbered (autoshot_000540.png), so to skip a load phase pass
#   a COUNT of leading frames, not a tick: --start 8 drops the first 8 frames.
#   --start beyond the number of frames is an error (no empty output is written).
#   --dedup     drop consecutive near-identical frames (default: ON)
#   --no-dedup  keep every frame (legacy behaviour; may show static pauses)
#
# Examples:
#   scripts/make_demo_gif.sh
#   scripts/make_demo_gif.sh build_pc/run out.gif --fps 15 --scale 360
#   scripts/make_demo_gif.sh build_pc/run out.gif --start 16 --step 2
#   scripts/make_demo_gif.sh build_pc/run out.gif --no-dedup
#   scripts/make_demo_gif.sh build_pc/run out.mp4 --mp4

set -euo pipefail

FRAMES_DIR="build_pc/run"
OUTPUT=""
WANT_MP4=0
FPS=20
SCALE=480
START=0
STEP=1
DEDUP=1

POSITIONAL=()
while [[ $# -gt 0 ]]; do
	case "$1" in
		--mp4) WANT_MP4=1; shift ;;
		--fps) FPS="$2"; shift 2 ;;
		--scale) SCALE="$2"; shift 2 ;;
		--start) START="$2"; shift 2 ;;
		--step) STEP="$2"; shift 2 ;;
		--dedup) DEDUP=1; shift ;;
		--no-dedup) DEDUP=0; shift ;;
		-h|--help) sed -n '2,45p' "$0"; exit 0 ;;
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

# --start/--step select a 0-based INDEX range (start, start+step, ...) of the
# sorted frame list -- the SAME selection every stitcher branch applies. Compute
# how many frames survive that selection up front so we can FAIL LOUDLY here,
# once, instead of silently writing an empty GIF (e.g. --start 540 against a run
# that only dumped ~50 frames -- the autoshot files are tick-numbered, so a tick
# was mistaken for a count). This guards ffmpeg and the convert/Pillow fallbacks
# alike. mpdecimate dedup runs later and only ever removes more, so an empty set
# here means an empty output regardless of path.
NSEL=0
if [[ "$START" -lt "${#FRAMES[@]}" ]]; then
	NSEL=$(( (${#FRAMES[@]} - START + STEP - 1) / STEP ))
fi
if [[ "$NSEL" -le 0 ]]; then
	echo "error: no frames selected: --start $START exceeds the ${#FRAMES[@]} frames in $FRAMES_DIR" >&2
	echo "  --start/--step are a 0-based INDEX into the sorted frame list, not a tick number." >&2
	echo "  autoshot files are tick-numbered (autoshot_000540.png); pass a COUNT of leading frames to skip." >&2
	exit 1
fi
echo "Stitching ${#FRAMES[@]} frames ($NSEL selected) from $FRAMES_DIR -> $OUTPUT (${FPS}fps, ${SCALE}px wide, start ${START}, step ${STEP}, dedup ${DEDUP})"

# ffmpeg filter prefix, applied (in order) before scale in every ffmpeg pass:
#   1. select  -- drop the first START frames, then keep every STEPth
#   2. mpdecimate (when DEDUP) -- drop consecutive near-identical frames so a
#      held view (load phase / standing still) is not replayed as a static
#      pause. hi/lo/frac are tuned to catch true dupes (incl. the constant load
#      frames) while preserving any frame with real motion. Tunable via env
#      MPDECIMATE (default below).
#   3. setpts  -- re-time whatever survives to a constant fps -> continuous
#      motion. MUST come AFTER mpdecimate: dropping dupes first then evening the
#      timestamps is what removes the pause; setpts alone only evens time.
# Backslash-escaped commas inside select() are required by ffmpeg's parser.
MPDECIMATE="${MPDECIMATE:-mpdecimate=hi=64*12:lo=64*5:frac=0.33}"
SELECT="select='gte(n\,${START})*not(mod(n-${START}\,${STEP}))'"
if [[ "$DEDUP" -eq 1 ]]; then
	SELECT="${SELECT},${MPDECIMATE}"
fi
SELECT="${SELECT},setpts=N/(${FPS}*TB)"

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
	# NOTE: convert has no robust per-frame dedup primitive (-layers Optimize
	# only delta-compresses, it does not DROP held frames), so this path cannot
	# remove static pauses. For a smooth/continuous clip install ffmpeg
	# (preferred) or python3-pil (Pillow) -- both dedup. See header.
	[[ "$DEDUP" -eq 1 ]] && \
		echo "  (note: convert cannot dedup frames; clip may show pauses --" \
			 "install ffmpeg or python3-pil for the smooth path)" >&2
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
	FPS="$FPS" SCALE="$SCALE" START="$START" STEP="$STEP" DEDUP="$DEDUP" \
		OUTPUT="$OUTPUT" FRAMES_DIR="$FRAMES_DIR" \
		python3 "$(dirname "$0")/make_demo_gif_pillow.py"
	echo "Wrote $OUTPUT"
	exit 0
fi

echo "error: no stitcher available." >&2
echo "  install one of: ffmpeg (preferred), imagemagick, or python3-pil" >&2
[[ "$WANT_MP4" -eq 1 ]] && echo "  note: MP4 output requires ffmpeg." >&2
exit 1
