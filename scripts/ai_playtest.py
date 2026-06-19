#!/usr/bin/env python3
"""Autonomous AI gameplay-assessment harness for CavEX (issue #83).

Plays a scenario through CavEX's *live* input path (issue #67) on an isolated
scratch world, logs every perceive->act tick, and emits a structured
gameplay-quality assessment -- no human, no API keys, deterministic.

Pipeline:
  1. make an ISOLATED run dir + scratch world (reuses agent_play_demo.make_run_dir;
     the real Claude World save is never touched), seeded so it is reproducible.
  2. launch the PC binary GATED (CAVEX_AGENT=1 + CAVEX_AGENT_GATED=1) so driver
     latency can never drop or mistime an input -- the game pauses each tick until
     our action line arrives (pause-think-act).
  3. drive it with a pluggable Policy. Each driven tick appends
     ``{tick, state, action}`` (+ an optional policy note) to ``run.jsonl``; every
     action is a REAL action line through the live source (no engine shortcut).
  4. score the log with the pure scripts/gameplay_assess.py scorer and write
     ``assessment.json`` (machine) + ``assessment.md`` (human/AI verdict).
  5. optional: with --autoshot, stitch the dumped frames into a GIF
     (scripts/make_demo_gif.sh) and, with --upload, push it to the `verification`
     release (same pattern as scripts/capture_demo.sh).

Policy seam (the path an LLM plugs into): ``Policy.decide(state, frame_path) ->
action line | macro``. Two ship here:
  * ``heuristic``     -- agent_play_demo.HeuristicPolicy, the deterministic
                         walk-toward-higher-ground driver (default; CI/regression).
  * ``claude-bridge`` -- a STUB seam (not gating, no keys) showing where an LLM
                         reads the state (+ optionally the latest frame), returns an
                         action AND an optional per-decision feel/feedback note.

Macros (a policy may return one instead of a single action line; the harness
expands it to per-tick lines, re-consulting the policy only at decision points):
  * ``HOLD <action-line> <N>``  -> the action line repeated N ticks.
  * ``<action-line> xN``        -> the action line repeated N ticks.
A bare button name expands to ``NAME=1`` and ``LOOK dx,dy`` to ``LOOK=dx,dy`` so the
shorthand in the issue (``HOLD FORWARD 15`` / ``LOOK 0.05,0 x6``) works; every
expanded tick is still a real action line driven through the live source.

Usage:
  scripts/ai_playtest.py --policy heuristic --max-ticks 200 --run-dir /tmp/pt \
      --autoshot 4 [--seed 42] [--goal move:5] [--upload]
"""

import argparse
import json
import math
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "scripts"))

import agent_play_demo as apd  # make_run_dir, HeuristicPolicy
import gameplay_assess as ga    # assess, render_markdown

# Button names that may appear bare in a macro body (expanded to NAME=1).
_BUTTON_NAMES = frozenset({
    "FORWARD", "BACKWARD", "LEFT", "RIGHT", "JUMP", "SNEAK", "MINE", "PLACE",
    "ACTION1", "ACTION2", "INVENTORY", "HOME", "SCROLL_LEFT", "SCROLL_RIGHT",
    "MAP", "TOGGLE_CREATIVE", "CREATIVE", "CREATIVE_PAGE", "PAGE",
})


def log(msg, quiet=False):
    if not quiet:
        print(msg, file=sys.stderr, flush=True)


def normalize_action_line(body):
    """Turn a (possibly shorthand) macro body into a valid per-tick action line.

    ``LOOK dx,dy`` -> ``LOOK=dx,dy``; a bare button name -> ``NAME=1``; tokens that
    already carry ``=`` pass through untouched. Anything unrecognised is left as-is
    (the live source treats a malformed token as a neutral no-op and warns).
    """
    body = re.sub(
        r"\bLOOK\s+(-?\d+(?:\.\d+)?,-?\d+(?:\.\d+)?)", r"LOOK=\1", body)
    out = []
    for tok in body.split():
        if "=" in tok:
            out.append(tok)
        elif tok.upper() in _BUTTON_NAMES:
            out.append(tok.upper() + "=1")
        else:
            out.append(tok)
    return " ".join(out)


def expand_action(value):
    """Expand a policy return value into a list of per-tick action lines.

    Plain line -> one tick. ``HOLD <body> <N>`` or ``<body> xN`` -> the body
    repeated N times. Always returns at least one element (a neutral "" tick).
    """
    value = (value or "").strip()
    if not value:
        return [""]
    toks = value.split()
    if toks[0].upper() == "HOLD" and len(toks) >= 3 and toks[-1].isdigit():
        n = max(0, int(toks[-1]))
        body = normalize_action_line(" ".join(toks[1:-1]))
        return [body] * n or [""]
    if len(toks) >= 2 and re.fullmatch(r"[xX]\d+", toks[-1]):
        n = max(0, int(toks[-1][1:]))
        body = normalize_action_line(" ".join(toks[:-1]))
        return [body] * n or [""]
    return [normalize_action_line(value)]


class ClaudeBridgePolicy:
    """STUB seam for an LLM (e.g. Claude) to drive richer goals and judge feel.

    NOT wired to any API and NOT on the CI gate. It exists to make the Policy
    contract an LLM driver implements explicit and exercisable end-to-end with no
    key: ``decide(state, frame_path) -> action line | macro`` plus an optional
    per-decision judgment in ``last_note`` -- the hook by which an LLM's qualitative
    read of how a mechanic *felt* this tick lands in the run log (and, later, feeds
    the deferred feel/feedback rubric dimensions).

    As a stub it defers to the deterministic heuristic so ``--policy claude-bridge``
    plays a full episode offline. A real driver replaces ``decide`` with a model
    call that reads ``state`` (and may ``Read`` the frame at ``frame_path``) and
    returns an action (or macro) and a feel note.
    """

    name = "claude_bridge"
    wants_frame = True  # a real LLM driver may inspect the latest frame

    def __init__(self, goal=None):
        self.goal = goal
        self._heuristic = apd.HeuristicPolicy()
        self.last_note = None

    def decide(self, state, frame_path=None):
        # --- LLM seam --------------------------------------------------------
        # A real driver calls the model here with `state` (+ frame_path + goal) and
        # returns an action line / macro, setting self.last_note to a one-line
        # mechanic-feel judgment. The stub defers to the heuristic (so the harness
        # runs with no key) and records no note (None) -- the place a judgment goes.
        action = self._heuristic.decide(state, frame_path)
        self.last_note = None
        return action


POLICIES = {
    "heuristic": lambda goal: apd.HeuristicPolicy(),
    "claude-bridge": lambda goal: ClaudeBridgePolicy(goal),
    "claude_bridge": lambda goal: ClaudeBridgePolicy(goal),
}


def latest_frame(run_dir):
    """Newest autoshot_*.png in run_dir, or None (for frame-aware policies)."""
    best = None
    best_idx = -1
    try:
        names = os.listdir(run_dir)
    except OSError:
        return None
    for name in names:
        m = re.match(r"autoshot_(\d+)\.png$", name)
        if m and int(m.group(1)) > best_idx:
            best_idx = int(m.group(1))
            best = os.path.join(run_dir, name)
    return best


def drive(proc, policy, run_dir, max_ticks, quiet):
    """Gated perceive->act loop. Returns (entries, driven, moved)."""
    entries = []
    queue = []            # pending per-tick lines from a macro expansion
    driven = 0
    moved = 0.0
    last_pos = None
    for raw in proc.stdout:
        raw = raw.strip()
        if not raw.startswith("{"):
            if raw and not quiet:
                log("   [game] %s" % raw, quiet)
            continue
        try:
            state = json.loads(raw)
        except json.JSONDecodeError:
            continue

        note = None
        if not queue:
            frame = latest_frame(run_dir) if getattr(
                policy, "wants_frame", False) else None
            value = policy.decide(state, frame)
            note = getattr(policy, "last_note", None)
            queue = expand_action(value)
        action = queue.pop(0)

        try:
            proc.stdin.write(action + "\n")
            proc.stdin.flush()
        except BrokenPipeError:
            break

        entry = {"tick": state.get("tick"), "state": state, "action": action}
        if note is not None:
            entry["note"] = note
        entries.append(entry)

        if state.get("player"):
            driven += 1
            pos = state.get("pos")
            if pos and last_pos:
                moved += math.dist(pos, last_pos)
            last_pos = pos
            if driven % 20 == 0:
                log("   tick %d pos=%s action='%s'" % (
                    state.get("tick"), pos, action), quiet)

        if driven >= max_ticks:
            log("==> Drove %d ticks; closing stdin to end the run" % driven, quiet)
            break
    return entries, driven, moved


def maybe_make_gif(run_dir, asset_path, quiet):
    """Best-effort: stitch the dumped frames into a GIF. Returns path or None."""
    frames = [n for n in os.listdir(run_dir) if re.match(r"autoshot_\d+\.png$", n)]
    if not frames:
        log("==> No autoshot frames to stitch (autoshot not enabled?)", quiet)
        return None
    try:
        subprocess.run(
            ["bash", os.path.join(ROOT, "scripts", "make_demo_gif.sh"),
             run_dir, asset_path, "--fps", "12", "--scale", "360"],
            check=True, stdout=subprocess.DEVNULL,
            stderr=(subprocess.DEVNULL if quiet else None))
        return asset_path if os.path.isfile(asset_path) else None
    except (OSError, subprocess.CalledProcessError) as exc:
        log("==> GIF stitch skipped: %s" % exc, quiet)
        return None


def maybe_upload(asset_path, quiet):
    """Best-effort: upload the GIF to the `verification` release (needs gh)."""
    try:
        subprocess.run(
            ["gh", "release", "upload", "verification", asset_path, "--clobber"],
            check=True, stdout=subprocess.DEVNULL,
            stderr=(subprocess.DEVNULL if quiet else None))
        repo = "amahpour/CavEX"
        url = ("https://github.com/%s/releases/download/verification/%s"
               % (repo, os.path.basename(asset_path)))
        log("==> Uploaded GIF: %s" % url, quiet)
        return url
    except (OSError, subprocess.CalledProcessError) as exc:
        log("==> Upload skipped: %s" % exc, quiet)
        return None


def main():
    ap = argparse.ArgumentParser(description="CavEX autonomous playtest harness")
    ap.add_argument("--policy", default="heuristic",
                    choices=sorted(POLICIES), help="driving policy")
    ap.add_argument("--goal", default=None,
                    help="optional stop-condition / goal (e.g. 'move:5')")
    ap.add_argument("--max-ticks", type=int, default=200,
                    help="max in-world ticks to drive")
    ap.add_argument("--bin", default=os.path.join(ROOT, "build_pc", "cavex"))
    ap.add_argument("--world", default=None, help="world save dir to copy in")
    ap.add_argument("--seed", type=int, default=42,
                    help="world seed (CAVEX_SEED) for a reproducible scratch world")
    ap.add_argument("--autoshot", type=int, default=0,
                    help="dump a framebuffer every N frames (for the GIF)")
    ap.add_argument("--run-dir", default=None,
                    help="where to write run.jsonl/assessment.* (default: temp)")
    ap.add_argument("--upload", action="store_true",
                    help="upload the GIF to the `verification` release")
    ap.add_argument("--quiet", action="store_true")
    args = ap.parse_args()

    if not os.path.isfile(args.bin) or not os.access(args.bin, os.X_OK):
        log("error: PC binary not found/executable: %s" % args.bin, False)
        log("  build it: (cd build_pc && cmake .. -DCMAKE_BUILD_TYPE=Debug"
            " && make -j$(nproc))", False)
        return 2

    if args.run_dir is None:
        import tempfile
        args.run_dir = tempfile.mkdtemp(prefix="cavex_playtest_")
    os.makedirs(args.run_dir, exist_ok=True)

    # Isolated scratch world (reuses the demo rig's isolation; seeded for repro).
    apd.make_run_dir(args.world, args.run_dir, seed=args.seed)

    policy = POLICIES[args.policy](args.goal)

    env = dict(os.environ)
    env["CAVEX_AGENT"] = "1"
    env["CAVEX_AGENT_GATED"] = "1"     # pause-think-act: never drop/mistime input
    env["CAVEX_AUTOPLAY"] = "1"        # auto-enter the scratch world
    env["vblank_mode"] = "0"          # headless: don't block on the compositor
    if args.autoshot > 0:
        env["CAVEX_AUTOSHOT"] = str(args.autoshot)

    log("==> Playtest: policy=%s seed=%d up to %d ticks (gated)" % (
        args.policy, args.seed, args.max_ticks), args.quiet)

    proc = subprocess.Popen(
        [args.bin], cwd=args.run_dir, env=env,
        stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, bufsize=1)

    clean_exit = False
    try:
        entries, driven, moved = drive(
            proc, policy, args.run_dir, args.max_ticks, args.quiet)
    finally:
        try:
            proc.stdin.close()
        except Exception:
            pass
        try:
            rc = proc.wait(timeout=15)
            clean_exit = rc == 0
        except subprocess.TimeoutExpired:
            proc.terminate()
            rc = None

    log("==> Drove %d ticks, moved %.2f blocks (exit rc=%s)" % (
        driven, moved, rc), args.quiet)

    # Persist the run log (one record per driven tick).
    run_path = os.path.join(args.run_dir, "run.jsonl")
    with open(run_path, "w") as f:
        for e in entries:
            f.write(json.dumps(e) + "\n")

    # Score it (pure) and write both report forms.
    assessment = ga.assess(entries, goal=args.goal, meta={"clean_exit": clean_exit})
    with open(os.path.join(args.run_dir, "assessment.json"), "w") as f:
        json.dump(assessment, f, indent=2, sort_keys=True)
    with open(os.path.join(args.run_dir, "assessment.md"), "w") as f:
        f.write(ga.render_markdown(assessment))

    log("==> %s" % assessment["narrative"], args.quiet)
    log("==> Wrote run.jsonl + assessment.json + assessment.md in %s"
        % args.run_dir, args.quiet)

    # Optional visual artifact.
    if args.autoshot > 0:
        asset = os.path.join(args.run_dir, "playtest-%s.gif" % args.policy)
        gif = maybe_make_gif(args.run_dir, asset, args.quiet)
        if gif and args.upload:
            maybe_upload(gif, args.quiet)

    # Exit non-zero on a broken verdict OR a run that never drove the player, so
    # this is usable as a coarse gate too.
    return 0 if (driven > 0 and assessment["verdict"] != "broken") else 1


if __name__ == "__main__":
    sys.exit(main())
