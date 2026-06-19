#!/usr/bin/env python3
"""Reference live-driver for CavEX's agent action source (issue #67).

This is the pure-stdlib proof that the live bridge works end-to-end with NO
LLM: it spawns the PC game with CAVEX_AGENT=1, reads one game-state JSON line
per tick from the game's stdout, and writes one action line per tick to the
game's stdin. The heuristic is deliberately simple -- "walk toward higher
ground" -- so it visibly, deterministically controls the player. An LLM/Claude
driver can later replace the decide() function (a separate, non-repo concern).

The game is run in GATED (pause-think-act) mode by default so no decision is
dropped if this script is slow; pass --realtime to let the game free-run.

Protocol (see source/game/state_export.h and source/platform/demo_input.h):
  game -> stdout : {"tick":N,"screen":"ingame","player":true,"pos":[...],
                    "orient":[yaw,pitch],...,"heightmap":[...25 ints...]}
  stdin -> game  : FORWARD=1 LOOK=+0.05,0.0 JUMP=1   (one line per tick)

State lines start once the player is in the world (the game prints non-JSON
startup chatter first, which this driver skips). Each state line is answered by
exactly one action line, so the game acts on the world the driver just saw.

Usage:
  scripts/agent_play_demo.py [--bin build_pc/cavex] [--world DIR]
                             [--ticks N] [--realtime] [--autoshot N]
                             [--run-dir DIR] [--quiet]

It mirrors capture_demo.sh's isolation: a fresh scratch world in a private run
dir (the real Claude World save is never touched). With --autoshot the game
dumps autoshot_*.png frames into the run dir, which scripts/make_demo_gif.sh
can then stitch into the PR GIF.
"""

import argparse
import json
import math
import os
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Heightmap window side (must match STATE_EXPORT_HEIGHT_WINDOW in the C header).
WINDOW = 5

# Ticks to wait between hops. CavEX toggles creative flight on a double-tap of
# JUMP within JUMP_TAP_WINDOW (10) ticks (source/entity/entity.h), so two jumps
# inside that window would flip flight on/off. We space hops strictly wider than
# that window so the heuristic walker stays grounded and never trips the toggle.
JUMP_COOLDOWN_TICKS = 12


def log(msg, quiet=False):
    if not quiet:
        print(msg, file=sys.stderr, flush=True)


def make_run_dir(world_src, run_dir=None, seed=None):
    """Build an isolated run dir (own config + a scratch world) like the rig.

    Everything (config paths, assets symlink, world) is created directly in the
    final dir so the absolute paths baked into config.json stay valid.

    `seed`, when set, is exported as CAVEX_SEED to the gen_world.py subprocess so
    a generated scratch world is reproducible (gen_world derives all terrain from
    CAVEX_SEED, default 42). Passing an explicit `world_src` ignores `seed`. This
    keeps the default behaviour (seed=None) byte-for-byte unchanged.
    """
    if run_dir is None:
        run_dir = tempfile.mkdtemp(prefix="cavex_agent_")
    else:
        os.makedirs(run_dir, exist_ok=True)
    saves = os.path.join(run_dir, "saves")
    os.makedirs(saves, exist_ok=True)
    # Symlink assets so the texturepack/shaders resolve.
    assets_link = os.path.join(run_dir, "assets")
    if not os.path.lexists(assets_link):
        os.symlink(os.path.join(ROOT, "assets"), assets_link)

    # Minimal config pointing worlds at our private saves dir, reusing the
    # input block from config_pc.json (kept verbatim).
    with open(os.path.join(ROOT, "config_pc.json")) as f:
        base = json.load(f)
    cfg = {
        "paths": {
            "texturepack": os.path.join(run_dir, "assets"),
            "worlds": saves,
        }
    }
    if "input" in base:
        cfg["input"] = base["input"]
    with open(os.path.join(run_dir, "config.json"), "w") as f:
        json.dump(cfg, f)

    # Provide the world the autoplay will enter (replace any stale copy).
    world_dst = os.path.join(saves, "world")
    shutil.rmtree(world_dst, ignore_errors=True)
    if world_src:
        shutil.copytree(world_src, world_dst)
    else:
        tmp = tempfile.mkdtemp(prefix="cavex_world_")
        gen_env = dict(os.environ)
        if seed is not None:
            gen_env["CAVEX_SEED"] = str(seed)
        subprocess.run(
            [sys.executable, os.path.join(ROOT, "gen_world.py"), tmp],
            check=True, stdout=subprocess.DEVNULL, env=gen_env,
        )
        shutil.copytree(os.path.join(tmp, "world"), world_dst)
        shutil.rmtree(tmp, ignore_errors=True)

    return run_dir


def decide(state, jump_cooldown=0):
    """Heuristic policy: walk toward the highest nearby ground.

    Pure function of the state dict (+ how many ticks we must still wait before
    the next hop) -> an action line (str). Reads the local heightmap window,
    finds the highest cell, and steers the yaw toward it while walking forward;
    hops a single step up when the ground ahead rises AND the jump cooldown has
    elapsed (jump_cooldown == 0). The cooldown keeps successive hops far enough
    apart that two of them never land inside CavEX's double-tap flight window, so
    the walker stays grounded. No LLM, fully deterministic.
    """
    if not state.get("player"):
        return ""  # on a menu / loading: do nothing, just wait

    hm = state.get("heightmap")
    yaw = state.get("orient", [0.0, 0.0])[0]

    # Default: just walk forward.
    if not hm or len(hm) != WINDOW * WINDOW:
        return "FORWARD=1"

    half = WINDOW // 2
    centre = hm[half * WINDOW + half]

    # Find the highest cell (prefer ones further from centre so we actually
    # travel); ties broken by distance so the target is unambiguous.
    best = None
    best_key = None
    for r in range(WINDOW):
        for c in range(WINDOW):
            dz = r - half  # +z = south
            dx = c - half  # +x = east
            if dx == 0 and dz == 0:
                continue
            h = hm[r * WINDOW + c]
            dist = math.hypot(dx, dz)
            key = (h, dist)  # highest first, then farthest
            if best_key is None or key > best_key:
                best_key = key
                best = (dx, dz, h)

    dx, dz, best_h = best

    # Forward in CavEX is (sin yaw, cos yaw) in (x, z). Desired yaw to face the
    # target cell; nudge LOOK to rotate toward it (LOOK dx DECREASES yaw).
    target_yaw = math.atan2(dx, dz)
    err = target_yaw - yaw
    # Wrap to [-pi, pi].
    while err > math.pi:
        err -= 2 * math.pi
    while err < -math.pi:
        err += 2 * math.pi

    # Proportional turn, clamped so the view pans smoothly (not a snap).
    look_dx = max(-0.15, min(0.15, -err * 0.25))

    tokens = ["FORWARD=1", "LOOK=%.3f,0.0" % look_dx]
    # If the ground ahead rises, hop -- but only once the cooldown has elapsed,
    # so two hops never fall inside the double-tap window that toggles flight.
    if best_h > centre and jump_cooldown <= 0:
        tokens.append("JUMP=1")
    return " ".join(tokens)


class HeuristicPolicy:
    """The reference 'walk toward higher ground' policy as a reusable object.

    This is `decide()` plus the jump-cooldown bookkeeping that the reference
    driver's main() loop keeps, packaged behind the pluggable Policy seam used by
    scripts/ai_playtest.py: `decide(state, frame_path=None) -> action line`. The
    behaviour is IDENTICAL to running this module standalone -- the cooldown is
    decremented once per consulted tick, the same action string is produced, and
    a hop re-arms the cooldown -- so it stays the deterministic regression driver
    while also being importable by the assessment harness.

    `frame_path` (the latest captured frame) is accepted for seam compatibility
    with richer policies but ignored: the heuristic decides on the state alone.
    """

    name = "heuristic"
    wants_frame = False  # decides on state only; never reads a frame

    def __init__(self):
        self.jump_cooldown = 0
        self.last_note = None  # heuristic emits no per-decision judgment

    def decide(self, state, frame_path=None):
        # Mirror main()'s order exactly: age the cooldown, decide, then re-arm on
        # a hop. Identical sequencing keeps the walker's output bit-for-bit equal
        # to the standalone driver.
        if self.jump_cooldown > 0:
            self.jump_cooldown -= 1
        action = decide(state, self.jump_cooldown)
        if "JUMP=1" in action.split():
            self.jump_cooldown = JUMP_COOLDOWN_TICKS
        return action


def main():
    ap = argparse.ArgumentParser(description="CavEX live-agent reference driver")
    ap.add_argument("--bin", default=os.path.join(ROOT, "build_pc", "cavex"))
    ap.add_argument("--world", default=None, help="world save dir to copy in")
    ap.add_argument("--ticks", type=int, default=160, help="max ticks to drive")
    ap.add_argument("--realtime", action="store_true",
                    help="let the game free-run (default: gated/pause-think-act)")
    ap.add_argument("--autoshot", type=int, default=0,
                    help="dump a framebuffer every N frames (for the GIF)")
    ap.add_argument("--run-dir", default=None,
                    help="keep the run dir here (default: a temp dir, removed)")
    ap.add_argument("--quiet", action="store_true")
    args = ap.parse_args()

    if not os.path.isfile(args.bin) or not os.access(args.bin, os.X_OK):
        log("error: PC binary not found/executable: %s" % args.bin, False)
        log("  build it first: (cd build_pc && cmake .. -DCMAKE_BUILD_TYPE=Debug"
            " && make -j$(nproc))", False)
        return 2

    keep_run = args.run_dir is not None
    run_dir = make_run_dir(args.world, args.run_dir)

    env = dict(os.environ)
    env["CAVEX_AGENT"] = "1"
    env["CAVEX_AUTOPLAY"] = "1"        # auto-enter the scratch world
    env["vblank_mode"] = "0"          # headless: don't block on the compositor
    if not args.realtime:
        env["CAVEX_AGENT_GATED"] = "1"  # pause-think-act
    if args.autoshot > 0:
        env["CAVEX_AUTOSHOT"] = str(args.autoshot)

    log("==> Launching %s (%s, up to %d ticks)" % (
        args.bin, "real-time" if args.realtime else "gated", args.ticks),
        args.quiet)

    proc = subprocess.Popen(
        [args.bin], cwd=run_dir, env=env,
        stdin=subprocess.PIPE, stdout=subprocess.PIPE,
        text=True, bufsize=1,
    )

    driven = 0
    last_pos = None
    moved = 0.0
    jump_cooldown = 0
    try:
        for raw in proc.stdout:
            raw = raw.strip()
            if not raw.startswith("{"):
                # Non-JSON chatter from the engine (load messages etc.).
                if not args.quiet and raw:
                    log("   [game] %s" % raw, args.quiet)
                continue
            try:
                state = json.loads(raw)
            except json.JSONDecodeError:
                continue

            if jump_cooldown > 0:
                jump_cooldown -= 1
            action = decide(state, jump_cooldown)
            if "JUMP=1" in action.split():
                jump_cooldown = JUMP_COOLDOWN_TICKS
            try:
                proc.stdin.write(action + "\n")
                proc.stdin.flush()
            except BrokenPipeError:
                break

            if state.get("player"):
                driven += 1
                pos = state.get("pos")
                if pos and last_pos:
                    moved += math.dist(pos, last_pos)
                last_pos = pos
                if driven % 20 == 0:
                    log("   tick %d pos=%s action='%s'" % (
                        state.get("tick"), pos, action), args.quiet)

            if driven >= args.ticks:
                log("==> Drove %d ticks; closing stdin to end the run" % driven,
                    args.quiet)
                break
    finally:
        try:
            proc.stdin.close()
        except Exception:
            pass
        try:
            proc.wait(timeout=10)
        except subprocess.TimeoutExpired:
            proc.terminate()

    log("==> Player moved a total of %.2f blocks over %d driven ticks" % (
        moved, driven), args.quiet)

    if not keep_run:
        shutil.rmtree(run_dir, ignore_errors=True)
    else:
        log("==> Run dir kept at %s" % run_dir, args.quiet)

    # Success if we actually drove the player and it moved.
    return 0 if (driven > 0 and moved > 0.5) else 1


if __name__ == "__main__":
    sys.exit(main())
