#!/usr/bin/env python3
"""Pure-Pillow GIF stitcher: fallback for make_demo_gif.sh when neither ffmpeg
nor ImageMagick is installed (issue #66 capture rig).

Reads autoshot_*.png frames from FRAMES_DIR and writes a palette-optimized,
looping GIF to OUTPUT, scaled to SCALE px wide at FPS frames/sec. These are
passed via environment variables by make_demo_gif.sh.

SMOOTHNESS (issue #66 follow-up): with DEDUP=1 (the default) this drops
consecutive near-identical frames BEFORE timing them to a constant fps, the
Pillow equivalent of ffmpeg's `mpdecimate`. The autoshot frames are keyed to
the 20 Hz demo tick, so any tick where the rendered view does not change (world
still loading, or the player standing still) is byte-identical to its
predecessor; re-timing those to even fps without dropping them would replay a
static still as a visible PAUSE. Dropping the dupes first then evening the frame
durations is what yields continuous motion. DEDUP=0 keeps every frame (legacy).
"""

import glob
import os
import re
import sys


# A frame is a duplicate of the previous KEPT frame when the mean absolute
# per-channel pixel difference is at or below this threshold (0..255). Exact
# dupes score 0; tiny dithering/encoding jitter stays well under a few units;
# any real camera/world motion scores far higher. Tunable via DEDUP_THRESHOLD.
DEFAULT_DEDUP_THRESHOLD = 2.0


def _mean_abs_diff(a, b):
    """Mean absolute per-channel pixel difference between two RGB images."""
    from PIL import ImageChops, ImageStat

    diff = ImageChops.difference(a, b)
    # Stat.mean is per-band; average the bands for a single scalar.
    means = ImageStat.Stat(diff).mean
    return sum(means) / len(means) if means else 0.0


def main() -> int:
    try:
        from PIL import Image
    except ImportError:
        sys.stderr.write("Pillow (python3-pil) is required for this fallback\n")
        return 1

    frames_dir = os.environ.get("FRAMES_DIR", "build_pc/run")
    output = os.environ.get("OUTPUT", "demo.gif")
    fps = float(os.environ.get("FPS", "20"))
    scale = int(os.environ.get("SCALE", "480"))
    start = int(os.environ.get("START", "0"))  # skip N leading frames
    step = max(1, int(os.environ.get("STEP", "1")))  # keep every STEPth frame
    dedup = os.environ.get("DEDUP", "1") != "0"
    threshold = float(os.environ.get("DEDUP_THRESHOLD", DEFAULT_DEDUP_THRESHOLD))

    paths = glob.glob(os.path.join(frames_dir, "autoshot_*.png"))
    # Sort by the numeric frame index, not lexically.
    def idx(p):
        m = re.search(r"autoshot_(\d+)\.png$", os.path.basename(p))
        return int(m.group(1)) if m else 0

    paths.sort(key=idx)
    total = len(paths)
    if total == 0:
        sys.stderr.write("no autoshot_*.png frames in %s\n" % frames_dir)
        return 1
    # --start/--step are a 0-based INDEX into the sorted list (same as the shell
    # script and the ffmpeg path). An empty selection is an error, not an empty
    # GIF -- e.g. --start beyond the frame count (autoshot files are
    # tick-numbered, so a tick was likely mistaken for a count).
    paths = paths[start::step]
    if not paths:
        sys.stderr.write(
            "no frames selected: --start %d exceeds the %d frames in %s "
            "(--start is a 0-based index, not a tick number)\n"
            % (start, total, frames_dir)
        )
        return 1

    # Load + scale to RGB first; dedup on the scaled RGB so the comparison sees
    # exactly what is encoded (and is cheaper than comparing full-res frames).
    scaled = []
    for p in paths:
        im = Image.open(p).convert("RGB")
        if scale and im.width != scale:
            h = max(1, round(im.height * scale / im.width))
            im = im.resize((scale, h), Image.LANCZOS)
        scaled.append(im)

    dropped = 0
    if dedup and len(scaled) > 1:
        kept = [scaled[0]]
        for im in scaled[1:]:
            if _mean_abs_diff(kept[-1], im) <= threshold:
                dropped += 1
                continue
            kept.append(im)
        scaled = kept

    # Quantize to a stable adaptive palette for a smaller, cleaner GIF.
    frames = [im.convert("P", palette=Image.ADAPTIVE, colors=256) for im in scaled]

    duration_ms = max(1, round(1000.0 / fps))
    # optimize=False: Pillow's optimizer can merge visually-identical consecutive
    # frames; we have already done the dedup ourselves above (when enabled) and
    # want the kept frames timed evenly, so leave the optimizer off.
    frames[0].save(
        output,
        save_all=True,
        append_images=frames[1:],
        duration=duration_ms,
        loop=0,
        optimize=False,
        disposal=1,
    )
    sys.stderr.write(
        "Pillow: wrote %d frames -> %s (%.0f fps, dedup=%s dropped %d)\n"
        % (len(frames), output, fps, "on" if dedup else "off", dropped)
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
