#!/usr/bin/env python3
"""Objective task battery + scorer for the CavEX AI player (issue #83 follow-on).

Each task runs in its OWN isolated scratch world, drives the closed-loop skills in
scripts/agent_skills.py through the *live* input path, and returns a
machine-checkable pass/fail + score + evidence -- no human, no LLM, deterministic
per seed. This is the eval the improvement loop optimises against, and a
regression gate for the skills layer.

    python3 scripts/agent_eval.py --all [--seed 42] [--json report.json]
    python3 scripts/agent_eval.py --task floor_2x2 --autoshot 4
    python3 scripts/agent_eval.py --list

A task is a function ``fn(session) -> {pass, score, detail, evidence}`` registered
with @task. The harness owns world isolation, timing, autoshot frames, and
crash-to-fail conversion so a single broken skill can never wedge the battery.
"""

import argparse
import json
import math
import os
import shutil
import subprocess
import sys
import tempfile
import time
import traceback

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "scripts"))
import agent_skills as sk

TASKS = {}
_WORLD_CACHE = {}


def pregen_world(seed):
    """Generate ONE scratch world for a seed and reuse it across tasks. World-gen
    dominates per-task wall time, so the battery (and the improvement loop that
    re-runs it many times) generates terrain once and copies it in."""
    if seed in _WORLD_CACHE and os.path.isdir(_WORLD_CACHE[seed]):
        return _WORLD_CACHE[seed]
    tmp = tempfile.mkdtemp(prefix="cavex_world_seed%d_" % seed)
    env = dict(os.environ, CAVEX_SEED=str(seed))
    subprocess.run([sys.executable, os.path.join(ROOT, "gen_world.py"), tmp],
                   check=True, stdout=subprocess.DEVNULL, env=env)
    _WORLD_CACHE[seed] = os.path.join(tmp, "world")
    return _WORLD_CACHE[seed]


def task(name, desc):
    def deco(fn):
        TASKS[name] = {"fn": fn, "desc": desc}
        return fn
    return deco


def _ring(x0, z0, y, w, l):
    """Perimeter cells of a w x l footprint at corner (x0,z0), height y."""
    return [(x0 + i, y, z0 + j) for i in range(w) for j in range(l)
            if i in (0, w - 1) or j in (0, l - 1)]


# --- the battery (ordered easy -> hard) -----------------------------------
@task("walk_to", "navigate 8 blocks east and stop on the target column")
def t_walk_to(s):
    cx, cz = s.feet_column()
    ok = s.goto(cx + 8, cz, tol=0.9)
    px, _, pz = s.pos()
    d = math.hypot(cx + 8.5 - px, cz + 0.5 - pz)
    return {"pass": ok, "score": 1.0 if ok else max(0.0, 1 - d / 8.0),
            "detail": "final dist=%.2f" % d, "evidence": {"end_pos": s.pos()}}


@task("face_east", "turn to face +x (east) within 0.05 rad")
def t_face_east(s):
    target = math.pi / 2          # forward = (sin yaw, cos yaw); east = +x => yaw=pi/2
    s.turn_to(yaw_des=target, pitch_des=math.pi / 2, tol=0.02)
    err = abs(sk._wrap(target - s.yaw()))
    return {"pass": err < 0.05, "score": max(0.0, 1 - err / 0.5),
            "detail": "yaw err=%.3f" % err, "evidence": {"yaw": s.yaw()}}


@task("aim_block", "aim the crosshair at a chosen surface block")
def t_aim_block(s):
    cx, cz = s.feet_column()
    sy = s.top_solid_y(1, 0)
    ok = s.aim_at(cx + 1, sy, cz)
    return {"pass": ok, "score": 1.0 if ok else 0.0,
            "detail": "aim=%s" % s.aim(), "evidence": {"aim": s.aim()}}


@task("mine_one", "mine one surface block (survival, wall-clock dig)")
def t_mine_one(s):
    cx, cz = s.feet_column()
    sy = s.top_solid_y(1, 0)
    ok = s.mine_block(cx + 1, sy, cz)
    return {"pass": ok, "score": 1.0 if ok else 0.0,
            "detail": "broke (%d,%d,%d)=%s" % (cx + 1, sy, cz, ok), "evidence": {}}


@task("place_one", "place one block on the surface")
def t_place_one(s):
    cx, cz = s.feet_column()
    sy = s.top_solid_y(-1, 0)
    ok = s.place_world(cx - 1, sy + 1, cz, item=3)
    return {"pass": ok, "score": 1.0 if ok else 0.0,
            "detail": "placed (%d,%d,%d)=%s" % (cx - 1, sy + 1, cz, ok),
            "evidence": {}}


@task("stack_3", "build a 3-high pillar under the player (pillar-jump)")
def t_stack_3(s):
    feet0 = round(s.pos()[1] - sk.EYE_HEIGHT)     # starting feet level
    n = s.pillar_up(3, item=3)
    risen = round(s.pos()[1] - sk.EYE_HEIGHT) - feet0   # state-derived, not counted
    return {"pass": n == 3 and risen == 3, "score": n / 3.0,
            "detail": "%d/3 courses, feet +%d" % (n, risen),
            "evidence": {"courses": n, "feet_rise": risen}}


@task("floor_2x2", "build a 2x2 floor platform")
def t_floor_2x2(s):
    cx, cz = s.feet_column()
    base = s.top_solid_y(0, 0) + 1
    targets = [(cx + 1, base, cz + 1), (cx + 2, base, cz + 1),
               (cx + 1, base, cz + 2), (cx + 2, base, cz + 2)]
    placed, total = s.build_blocks(targets, item=3)
    return {"pass": placed == total, "score": placed / total,
            "detail": "%d/%d placed" % (placed, total),
            "evidence": {"placed": placed}}


@task("wall_ring_3x3", "build a hollow 3x3 wall ring (8 blocks)")
def t_wall_ring(s):
    cx, cz = s.feet_column()
    base = s.top_solid_y(0, 0) + 1
    targets = _ring(cx + 1, cz + 1, base, 3, 3)
    placed, total = s.build_blocks(targets, item=3)
    return {"pass": placed == total, "score": placed / total,
            "detail": "%d/%d wall blocks" % (placed, total),
            "evidence": {"placed": placed, "total": total}}


@task("dig_down_2", "dig straight down 2 blocks (known-hard: straight-down aim)")
def t_dig_down_2(s):
    n = s.dig_down(2)
    return {"pass": n == 2, "score": n / 2.0, "detail": "dug %d/2 down" % n,
            "evidence": {"depth": n}}


# --- runner ----------------------------------------------------------------
def run_task(name, seed=42, run_dir=None, autoshot=0, quiet=True, world=None):
    meta = TASKS[name]
    if run_dir is None:
        run_dir = "/tmp/cavex_eval_%s" % name
    t0 = time.time()
    result = {"task": name, "desc": meta["desc"]}
    s = None
    try:
        s = sk.GameSession(run_dir, seed=seed, autoshot=autoshot, quiet=quiet,
                           world=world)
        s.start()
        out = meta["fn"](s)
        result.update(out)
        result["ticks"] = s.tick_count
    except Exception as exc:                       # any skill error -> task fail
        result.update({"pass": False, "score": 0.0,
                       "detail": "EXC %s: %s" % (type(exc).__name__, exc),
                       "evidence": {"trace": traceback.format_exc()[-600:]}})
    finally:
        if s:
            s.close()
    result["secs"] = round(time.time() - t0, 1)
    result["run_dir"] = run_dir
    return result


def run_battery(names, seed=42, autoshot=0, quiet=True, log=True, reuse_world=True):
    results = []
    world = pregen_world(seed) if reuse_world else None
    for name in names:
        r = run_task(name, seed=seed, autoshot=autoshot,
                     run_dir="/tmp/cavex_eval_%s_%d" % (name, seed), quiet=quiet,
                     world=world)
        results.append(r)
        if log:
            mark = "PASS" if r.get("pass") else "FAIL"
            print("  [%s] %-14s score=%.2f %4ss  %s" % (
                mark, name, r.get("score", 0.0), r.get("secs", 0), r.get("detail")),
                file=sys.stderr, flush=True)
    npass = sum(1 for r in results if r.get("pass"))
    score = sum(r.get("score", 0.0) for r in results) / max(1, len(results))
    return {"seed": seed, "n": len(results), "pass": npass,
            "score": round(score, 3), "results": results}


def main():
    ap = argparse.ArgumentParser(description="CavEX AI task battery")
    ap.add_argument("--task", help="run a single task by name")
    ap.add_argument("--all", action="store_true", help="run the whole battery")
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--seed", type=int, default=42)
    ap.add_argument("--autoshot", type=int, default=0)
    ap.add_argument("--json", help="write the report JSON here")
    ap.add_argument("--quiet", action="store_true")
    args = ap.parse_args()

    if args.list:
        for n, m in TASKS.items():
            print("%-14s %s" % (n, m["desc"]))
        return 0

    names = list(TASKS) if args.all else ([args.task] if args.task else [])
    if not names:
        ap.print_help()
        return 2

    print("==> battery: %d task(s), seed=%d" % (len(names), args.seed),
          file=sys.stderr)
    report = run_battery(names, seed=args.seed, autoshot=args.autoshot,
                         quiet=args.quiet)
    print("==> %d/%d passed, mean score %.3f" % (
        report["pass"], report["n"], report["score"]), file=sys.stderr)
    if args.json:
        with open(args.json, "w") as f:
            json.dump(report, f, indent=2)
        print("==> wrote %s" % args.json, file=sys.stderr)
    else:
        print(json.dumps(report, indent=2))
    return 0 if report["pass"] == report["n"] else 1


if __name__ == "__main__":
    sys.exit(main())
