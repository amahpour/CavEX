#!/usr/bin/env python3
"""Pure-Pillow GIF stitcher: fallback for make_demo_gif.sh when neither ffmpeg
nor ImageMagick is installed (issue #66 capture rig).

Reads autoshot_*.png frames from FRAMES_DIR and writes a palette-optimized,
looping GIF to OUTPUT, scaled to SCALE px wide at FPS frames/sec. All four are
passed via environment variables by make_demo_gif.sh.
"""

import glob
import os
import re
import sys


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

    paths = glob.glob(os.path.join(frames_dir, "autoshot_*.png"))
    # Sort by the numeric frame index, not lexically.
    def idx(p):
        m = re.search(r"autoshot_(\d+)\.png$", os.path.basename(p))
        return int(m.group(1)) if m else 0

    paths.sort(key=idx)
    paths = paths[start::step]
    if not paths:
        sys.stderr.write("no autoshot_*.png frames in %s\n" % frames_dir)
        return 1

    frames = []
    for p in paths:
        im = Image.open(p).convert("RGB")
        if scale and im.width != scale:
            h = max(1, round(im.height * scale / im.width))
            im = im.resize((scale, h), Image.LANCZOS)
        # Quantize to a stable adaptive palette for a smaller, cleaner GIF.
        frames.append(im.convert("P", palette=Image.ADAPTIVE, colors=256))

    duration_ms = max(1, round(1000.0 / fps))
    # optimize=False: Pillow's optimizer merges visually-identical consecutive
    # frames (collapsing slow/flat motion into far fewer, longer frames), which
    # would shorten the clip. Keep every frame so the playback length matches
    # the script; the adaptive-palette quantize above already keeps size down.
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
        "Pillow: wrote %d frames -> %s (%.0f fps)\n" % (len(frames), output, fps)
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
