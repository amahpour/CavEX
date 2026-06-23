#!/usr/bin/env python3
"""Natural-language goal -> skill plan -> executed, verified play (the real
"ask Claude to build X" seam, issue #67/#83).

This makes the ``claude-bridge`` policy real: a goal in English ("build a 4x4
house", "walk to 30 80", "dig down 3") is turned into an ordered list of
closed-loop skill calls (scripts/agent_skills.py), executed on an isolated
scratch world through the *live* input path, and verified step by step -- the
agent watches the state and reports what actually happened, like a human.

Two planners share one interface ``plan(goal, session) -> [step, ...]``:

  * TemplatePlanner -- deterministic, offline, NO API key. Pattern-matches the
    common goals against the live world anchor (the player's spawn column). This
    is what runs in CI and offline.
  * LLMPlanner      -- the genuine LLM seam. Given the goal, the live state, and a
    machine-readable description of the skill vocabulary, an LLM emits the plan as
    JSON. Wire ``llm_complete`` to an Anthropic API call (or, in an agent session,
    to Claude itself) and richer/open-ended goals plan themselves. Falls back to
    the template planner when no model is wired, so it always runs.

    python3 scripts/agent_planner.py --goal "build a 3x3 house" --autoshot 4
    python3 scripts/agent_planner.py --goal "walk to 40 90" --run-dir /tmp/p
"""

import argparse
import json
import math
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "scripts"))
import agent_skills as sk


# --- skill vocabulary the executor understands ----------------------------
# Each entry: name -> (callable(session, args) -> {ok, detail}, arg-doc). The
# arg-doc is what a planner (template or LLM) is told it may emit.
def _build_result(res):
    placed, total = res
    return {"ok": placed == total, "detail": "%d/%d blocks" % (placed, total),
            "placed": placed, "total": total}


SKILLS = {
    "goto": (
        lambda s, a: {"ok": s.goto(a["x"], a["z"], tol=a.get("tol", 0.9)),
                      "detail": "to (%s,%s)" % (a["x"], a["z"])},
        "goto {x,z}: walk to world column (x,z)"),
    "mine_block": (
        lambda s, a: {"ok": s.mine_block(a["x"], a["y"], a["z"]),
                      "detail": "(%s,%s,%s)" % (a["x"], a["y"], a["z"])},
        "mine_block {x,y,z}: dig out the block at (x,y,z)"),
    "place_world": (
        lambda s, a: {"ok": s.place_world(a["x"], a["y"], a["z"],
                                          item=a.get("item", 3)),
                      "detail": "(%s,%s,%s)" % (a["x"], a["y"], a["z"])},
        "place_world {x,y,z,item?}: place a block at (x,y,z)"),
    "build_floor": (
        lambda s, a: _build_result(s.build_floor(
            a["x0"], a["z0"], a["w"], a["l"], a["y"], item=a.get("item", 3))),
        "build_floor {x0,z0,w,l,y,item?}: fill a w*l platform at level y"),
    "build_walls": (
        lambda s, a: _build_result(s.build_walls(
            a["x0"], a["z0"], a["w"], a["l"], a["height"],
            item=a.get("item", 3), door=a.get("door", True), y0=a.get("y0"))),
        "build_walls {x0,z0,w,l,height,y0?,item?,door?}: perimeter walls, opt doorway"),
    "dig_down": (
        lambda s, a: {"ok": s.dig_down(a["n"]) == a["n"],
                      "detail": "%s deep" % a["n"]},
        "dig_down {n}: mine straight down n blocks"),
    "make_boat": (
        lambda s, a: {"ok": s.make_boat(), "detail": "place + board a boat"},
        "make_boat {}: place a boat on the ground and ride it"),
}


def skills_doc():
    return "\n".join("  - " + doc for _, (_, doc) in SKILLS.items())


# --- planners --------------------------------------------------------------
class TemplatePlanner:
    """Deterministic offline planner: regex the goal, anchor to the live spawn."""

    name = "template"

    def _dims(self, goal, default=(3, 3)):
        m = re.search(r"(\d+)\s*[x×by\s]+\s*(\d+)", goal)
        if m:
            return int(m.group(1)), int(m.group(2))
        m = re.search(r"\b(\d+)\b", goal)
        if m:
            n = int(m.group(1))
            return n, n
        return default

    def plan(self, goal, session):
        g = goal.lower().strip()
        cx, cz = session.feet_column()
        base = session.top_solid_y(0, 0) + 1
        steps = []

        m = re.search(r"(?:walk|go|move|travel)\s+to\s+(-?\d+)[ ,]+(-?\d+)", g)
        if m:
            return [{"skill": "goto", "args": {"x": int(m.group(1)),
                                               "z": int(m.group(2))},
                     "note": "walk to the requested spot"}]

        if "boat" in g:
            return [{"skill": "make_boat", "args": {},
                     "note": "place a boat on the ground and board it"}]

        if "dig" in g and "down" in g:
            n, _ = self._dims(g, (2, 2))
            return [{"skill": "dig_down", "args": {"n": n},
                     "note": "dig a shaft straight down"}]

        if re.search(r"\bmine\b|\bdig\b", g) and "down" not in g:
            n, _ = self._dims(g, (1, 1))
            steps = []
            for k in range(min(n, 4)):
                steps.append({"skill": "mine_block",
                              "args": {"x": cx + 1 + k, "y": base - 1, "z": cz},
                              "note": "mine surface block %d" % (k + 1)})
            return steps

        # build something with walls
        if re.search(r"house|hut|shack|shelter|building|cabin|room|wall", g):
            w, l = self._dims(g, (3, 3))
            x0, z0 = cx + 1, cz + 1
            steps.append({"skill": "build_floor",
                          "args": {"x0": x0, "z0": z0, "w": w, "l": l, "y": base},
                          "note": "lay the %dx%d floor" % (w, l)})
            steps.append({"skill": "build_walls",
                          # walls sit ON the floor -> explicit y0=base+1 (don't let
                          # build_walls recompute it from the post-floor position)
                          "args": {"x0": x0, "z0": z0, "w": w, "l": l,
                                   "height": 1, "door": True, "y0": base + 1},
                          "note": "raise the perimeter walls with a doorway"})
            return steps

        if re.search(r"floor|platform|slab|patio|deck", g):
            w, l = self._dims(g, (3, 3))
            return [{"skill": "build_floor",
                     "args": {"x0": cx + 1, "z0": cz + 1, "w": w, "l": l,
                              "y": base},
                     "note": "build a %dx%d platform" % (w, l)}]

        # default: a small hut so an unrecognised "build" still does something
        return [{"skill": "build_floor",
                 "args": {"x0": cx + 1, "z0": cz + 1, "w": 3, "l": 3, "y": base},
                 "note": "fallback: a 3x3 floor"},
                {"skill": "build_walls",
                 "args": {"x0": cx + 1, "z0": cz + 1, "w": 3, "l": 3,
                          "height": 1, "door": True, "y0": base + 1},
                 "note": "fallback: 3x3 walls"}]


PLAN_PROMPT = """You are driving a Minecraft-like agent. Turn the GOAL into a \
JSON plan: a list of steps, each {"skill": NAME, "args": {...}, "note": "..."}.

The player spawns at column (x={cx}, z={cz}); the ground surface air cell is \
y={base} (the topmost solid block is y={solid}). +x is east, +z is south, +y up.

Available skills:
{skills}

Return ONLY a JSON array of steps. GOAL: {goal}
"""


class LLMPlanner:
    """The real LLM seam. ``llm_complete(prompt) -> text`` is the only thing a
    deployment must provide (an Anthropic API call, or Claude itself in an agent
    session). With no model wired it defers to the template planner, so the
    pipeline always runs end-to-end offline."""

    name = "llm"

    def __init__(self, llm_complete=None):
        self.llm_complete = llm_complete
        self._fallback = TemplatePlanner()

    def plan(self, goal, session):
        if self.llm_complete is None:
            return self._fallback.plan(goal, session)
        cx, cz = session.feet_column()
        solid = session.top_solid_y(0, 0)
        prompt = PLAN_PROMPT.format(cx=cx, cz=cz, base=solid + 1, solid=solid,
                                    skills=skills_doc(), goal=goal)
        text = self.llm_complete(prompt)
        try:
            blob = re.search(r"\[.*\]", text, re.S).group(0)
            steps = json.loads(blob)
            assert isinstance(steps, list) and steps
            return steps
        except Exception:
            return self._fallback.plan(goal, session)


# --- executor --------------------------------------------------------------
def execute_goal(goal, run_dir=None, seed=42, autoshot=0, planner=None,
                 quiet=True):
    """Plan ``goal`` against a fresh world and execute it, verifying each step."""
    planner = planner or TemplatePlanner()
    if run_dir is None:
        run_dir = "/tmp/cavex_goal"
    s = sk.GameSession(run_dir, seed=seed, autoshot=autoshot, quiet=quiet)
    s.start()
    steps = planner.plan(goal, s)
    report = {"goal": goal, "planner": planner.name, "seed": seed,
              "spawn": s.pos(), "steps": []}
    for i, step in enumerate(steps):
        name = step.get("skill")
        args = step.get("args", {})
        entry = {"i": i, "skill": name, "args": args, "note": step.get("note")}
        fn = SKILLS.get(name)
        if fn is None:
            entry.update({"ok": False, "detail": "unknown skill"})
        else:
            try:
                entry.update(fn[0](s, args))
            except Exception as exc:
                entry.update({"ok": False,
                              "detail": "EXC %s: %s" % (type(exc).__name__, exc)})
        report["steps"].append(entry)
        if not quiet:
            print("  step %d %-12s %s -> %s" % (
                i, name, entry.get("detail", ""),
                "ok" if entry.get("ok") else "FAIL"), file=sys.stderr, flush=True)
    s.close()
    done = [e for e in report["steps"] if e.get("ok")]
    report["done"] = len(done)
    report["total"] = len(report["steps"])
    report["success"] = report["total"] > 0 and report["done"] == report["total"]
    return report


def main():
    ap = argparse.ArgumentParser(description="CavEX NL-goal planner + executor")
    ap.add_argument("--goal", required=True, help='e.g. "build a 3x3 house"')
    ap.add_argument("--seed", type=int, default=42)
    ap.add_argument("--run-dir", default="/tmp/cavex_goal")
    ap.add_argument("--autoshot", type=int, default=0)
    ap.add_argument("--json", help="write the execution report here")
    ap.add_argument("--quiet", action="store_true")
    args = ap.parse_args()

    print('==> goal: "%s"' % args.goal, file=sys.stderr)
    report = execute_goal(args.goal, run_dir=args.run_dir, seed=args.seed,
                          autoshot=args.autoshot, quiet=args.quiet)
    print("==> %d/%d steps ok (%s)" % (
        report["done"], report["total"],
        "SUCCESS" if report["success"] else "partial"), file=sys.stderr)
    if args.json:
        with open(args.json, "w") as f:
            json.dump(report, f, indent=2)
    else:
        print(json.dumps(report, indent=2))
    return 0 if report["success"] else 1


if __name__ == "__main__":
    sys.exit(main())
