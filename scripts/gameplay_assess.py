#!/usr/bin/env python3
"""Pure, deterministic gameplay-quality scorer for CavEX playtest runs (issue #83).

Input is a *run log*: a list of per-tick records ``{"tick", "state", "action"}``
exactly as scripts/ai_playtest.py appends to ``run.jsonl`` -- ``state`` is one
game-state JSON object (see source/game/state_export.h) and ``action`` is the
real action line the harness fed back that tick (see source/platform/demo_input.h).

This module is PURE: ``assess()`` and ``render_markdown()`` read only their
arguments and touch no files, no game, no clock, no randomness. The same run log
therefore always yields the same assessment -- which is what makes a fixed world
seed + the deterministic heuristic policy reproducible end to end. The orchestrator
(ai_playtest.py) does all the I/O; this file is unit-testable in isolation and is
exercised by ``--selftest`` (synthetic logs -> asserted scores), the deterministic
proof that needs no game.

Objective rubric (scored here, from the log alone):
  1. control_responsiveness -- did commanded inputs produce the expected state
     delta within K ticks (move->pos, look->orient, jump->leaves ground / rises)?
  2. locomotion_progress    -- total distance, net displacement, worst stuck streak.
  3. stability              -- clean exit + no state anomaly (NaN / >8-block
     teleport / origin-snap / clip below the world).
  4. friction              -- longest stall, decision count, ticks-to-goal.

Deferred (NOT scored here -- they need an LLM and/or state-export additions, and
are reported explicitly so coverage is honest, never silently omitted):
  mechanic_completion, feedback_legibility, feel.

Each scored dimension yields a 0-5 score, a pass/fail, evidence refs (indices into
the run log == lines of run.jsonl), and raw detail. The run rolls up to an overall
VERDICT: ``solid`` / ``rough`` / ``broken`` plus a short narrative.

Usage:
  scripts/gameplay_assess.py --selftest        # synthetic logs -> asserted scores
  scripts/gameplay_assess.py --run run.jsonl    # score an existing run, print JSON
  scripts/gameplay_assess.py --run run.jsonl --md   # ... print the markdown verdict
"""

import argparse
import json
import math
import sys

SCHEMA = "cavex.gameplay_assess/1"

# --- tuning (kept as named constants so the rubric is auditable) --------------
K_LOOKAHEAD = 4          # ticks a commanded input has to show its expected delta
MOVE_EPS = 0.03          # horizontal blocks counted as "moved" within a tick
LOOK_EPS = 0.005         # radians of yaw/pitch counted as "turned"
JUMP_RISE_EPS = 0.05     # vertical blocks counted as "left the ground" via rise
TELEPORT_BLOCKS = 8.0    # 3D jump between consecutive ticks flagged as a teleport
ORIGIN_NEAR = 0.5        # within this of (0,0,0) counts as "at the origin"
ORIGIN_FROM = 4.0        # ...having been at least this far away the tick before
CLIP_Y = -1.0            # player Y below this is "clipped below the world"
STALL_FAIL = 30          # a stall this long (ticks of ~0 progress) fails friction

MOVE_BUTTONS = frozenset({"FORWARD", "BACKWARD", "LEFT", "RIGHT"})


def parse_action(line):
    """Parse one action line into ``{'buttons': set[str], 'look': (dx, dy)}``.

    Mirrors the engine's token grammar (NAME=1 / NAME=0 / LOOK=dx,dy); unknown or
    malformed tokens are ignored, exactly as the live source treats them. A
    released button (NAME=0) is simply absent from the set.
    """
    buttons = set()
    look = (0.0, 0.0)
    for tok in (line or "").split():
        if "=" not in tok:
            continue
        key, _, val = tok.partition("=")
        key = key.upper()
        if key == "LOOK":
            parts = val.split(",")
            if len(parts) == 2:
                try:
                    look = (float(parts[0]), float(parts[1]))
                except ValueError:
                    pass
        elif val == "1":
            buttons.add(key)
    return {"buttons": buttons, "look": look}


def _horiz(a, b):
    """Horizontal (x/z) distance between two [x, y, z] positions."""
    return math.hypot(a[0] - b[0], a[2] - b[2])


def _finite(seq):
    return all(isinstance(v, (int, float)) and math.isfinite(v) for v in seq)


def _dimension(score, passed, evidence, detail):
    return {
        "score": int(score),
        "pass": bool(passed),
        "evidence": list(evidence[:10]),  # cap so a flood of refs stays readable
        "detail": detail,
        "deferred": False,
    }


def _score_responsiveness(idxs, states, acts):
    """Fraction of commanded inputs that produced their expected delta in K ticks."""
    expected = 0
    satisfied = 0
    fails = []
    n = len(idxs)
    for i in range(n):
        st = states[i]
        act = acts[i]
        pos0 = st.get("pos")
        orient0 = st.get("orient")
        ground0 = st.get("on_ground")
        fut = states[i + 1: i + 1 + K_LOOKAHEAD]

        if act["buttons"] & MOVE_BUTTONS:
            expected += 1
            ok = pos0 and any(
                f.get("pos") and _horiz(f["pos"], pos0) > MOVE_EPS for f in fut)
            satisfied += 1 if ok else 0
            if not ok:
                fails.append(idxs[i])

        if act["look"] != (0.0, 0.0):
            expected += 1
            ok = orient0 and any(
                f.get("orient") and (
                    abs(f["orient"][0] - orient0[0]) > LOOK_EPS
                    or abs(f["orient"][1] - orient0[1]) > LOOK_EPS)
                for f in fut)
            satisfied += 1 if ok else 0
            if not ok:
                fails.append(idxs[i])

        if "JUMP" in act["buttons"]:
            expected += 1
            ok = any(
                (ground0 and f.get("on_ground") is False)
                or (pos0 and f.get("pos")
                    and f["pos"][1] - pos0[1] > JUMP_RISE_EPS)
                for f in fut)
            satisfied += 1 if ok else 0
            if not ok:
                fails.append(idxs[i])

    frac = (satisfied / expected) if expected else None
    if frac is None:
        return frac, _dimension(
            5, True, [],
            {"fraction": None, "expected": 0, "satisfied": 0,
             "note": "no commanded inputs in the run -- nothing to score"})
    score = round(frac * 5)
    return frac, _dimension(
        score, frac >= 0.6, fails,
        {"fraction": round(frac, 4), "expected": expected, "satisfied": satisfied,
         "lookahead_ticks": K_LOOKAHEAD})


def _score_locomotion(states, acts):
    positions = [s.get("pos") for s in states if s.get("pos")]
    total = 0.0
    for i in range(1, len(positions)):
        total += _horiz(positions[i], positions[i - 1])
    net = _horiz(positions[0], positions[-1]) if len(positions) >= 2 else 0.0

    # Worst run of consecutive ticks where movement was commanded but did not move.
    max_stuck = cur = 0
    for i in range(1, len(states)):
        if acts[i - 1]["buttons"] & MOVE_BUTTONS:
            p0, p1 = states[i - 1].get("pos"), states[i].get("pos")
            if p0 and p1 and _horiz(p1, p0) < MOVE_EPS:
                cur += 1
                max_stuck = max(max_stuck, cur)
            else:
                cur = 0
        else:
            cur = 0

    score = 0
    for thr, pts in ((1.0, 1), (4.0, 2), (8.0, 3), (14.0, 4), (20.0, 5)):
        if total >= thr:
            score = pts
    if max_stuck >= 40:
        score = min(score, 1)
    elif max_stuck >= 20:
        score = min(score, 2)
    passed = total >= 2.0 and net >= 1.0 and max_stuck < 40
    return _dimension(
        score, passed, [],
        {"total_distance": round(total, 3), "net_displacement": round(net, 3),
         "max_stuck_streak": max_stuck})


def _score_stability(idxs, states, meta):
    anomalies = []
    prev = None
    for i, st in enumerate(states):
        ref = idxs[i]
        pos = st.get("pos")
        orient = st.get("orient") or []
        if pos and not (_finite(pos) and _finite(orient)):
            anomalies.append({"kind": "nan", "ref": ref})
        if pos and _finite(pos):
            if prev is not None:
                if math.dist(pos, prev) > TELEPORT_BLOCKS:
                    anomalies.append({"kind": "teleport", "ref": ref})
                if (math.dist(pos, [0.0, 0.0, 0.0]) < ORIGIN_NEAR
                        and math.dist(prev, [0.0, 0.0, 0.0]) > ORIGIN_FROM):
                    anomalies.append({"kind": "origin_snap", "ref": ref})
            if pos[1] < CLIP_Y:
                anomalies.append({"kind": "clip_below_world", "ref": ref})
            prev = pos

    clean_exit = True if meta is None else bool(meta.get("clean_exit", True))
    if not clean_exit:
        anomalies.append({"kind": "dirty_exit", "ref": None})

    kinds = sorted({a["kind"] for a in anomalies})
    score = max(0, 5 - 2 * len(kinds))
    passed = not anomalies and clean_exit
    return _dimension(
        score, passed, [a["ref"] for a in anomalies if a["ref"] is not None],
        {"clean_exit": clean_exit, "anomaly_kinds": kinds,
         "anomalies": anomalies[:10]})


def _resolve_goal(goal, states):
    """Objectively measurable goals only. Returns (measurable, reached, ticks).

    Supported form: ``move:<D>`` / ``move>=<D>`` -- reached the first tick whose net
    horizontal displacement from the start is >= D blocks. Any other goal string is
    a qualitative/LLM goal the objective scorer cannot judge, so it is reported as
    not-objectively-measurable (deferred) rather than failed.
    """
    if not goal:
        return False, None, None
    g = goal.strip().lower().replace(">=", ":").replace("=", ":")
    if g.startswith("move:"):
        try:
            need = float(g.split(":", 1)[1])
        except ValueError:
            return False, None, None
        positions = [s.get("pos") for s in states if s.get("pos")]
        if not positions:
            return True, False, None
        start = positions[0]
        for i, p in enumerate(positions):
            if _horiz(p, start) >= need:
                return True, True, i
        return True, False, None
    return False, None, None  # not objectively measurable


def _score_friction(states, acts, goal):
    decisions = sum(1 for a in acts if a["buttons"] or a["look"] != (0.0, 0.0))
    positions = [s.get("pos") for s in states if s.get("pos")]
    longest_stall = cur = 0
    for i in range(1, len(positions)):
        if _horiz(positions[i], positions[i - 1]) < MOVE_EPS:
            cur += 1
            longest_stall = max(longest_stall, cur)
        else:
            cur = 0

    measurable, reached, ticks_to_goal = _resolve_goal(goal, states)

    score = 5
    for thr, pts in ((5, 4), (10, 3), (20, 2), (STALL_FAIL, 1)):
        if longest_stall >= thr:
            score = pts
    passed = longest_stall < STALL_FAIL
    if measurable and not reached:
        passed = False
        score = min(score, 2)

    detail = {"decisions": decisions, "longest_stall": longest_stall,
              "ticks_to_goal": ticks_to_goal}
    if goal:
        detail["goal"] = goal
        detail["goal_objectively_measurable"] = measurable
        if not measurable:
            detail["note"] = ("goal not objectively measurable -- deferred to "
                              "LLM judging")
    return _dimension(score, passed, [], detail)


def assess(entries, goal=None, meta=None):
    """Score a run log into the gameplay-quality assessment dict (PURE).

    ``entries``: list of ``{"tick", "state", "action"}`` records. ``goal``: optional
    stop-condition (see _resolve_goal). ``meta``: optional run metadata, notably
    ``clean_exit`` (the orchestrator knows it; the log alone does not).
    """
    entries = entries or []
    idxs = [k for k, e in enumerate(entries)
            if isinstance(e.get("state"), dict) and e["state"].get("player")]
    states = [entries[k]["state"] for k in idxs]
    acts = [parse_action(entries[k].get("action") or "") for k in idxs]

    deferred = {
        "mechanic_completion": "needs per-action ack / inventory deltas "
                               "(state-export follow-up)",
        "feedback_legibility": "needs frame inspection + LLM judging",
        "feel": "needs LLM-in-the-loop judging",
    }

    if not states:
        return {
            "schema": SCHEMA,
            "verdict": "broken",
            "narrative": "No in-world player ticks were recorded -- the run never "
                         "reached gameplay.",
            "tick_count": 0,
            "goal": goal,
            "dimensions": {},
            "deferred": deferred,
        }

    frac, responsiveness = _score_responsiveness(idxs, states, acts)
    locomotion = _score_locomotion(states, acts)
    stability = _score_stability(idxs, states, meta)
    friction = _score_friction(states, acts, goal)

    dims = {
        "control_responsiveness": responsiveness,
        "locomotion_progress": locomotion,
        "stability": stability,
        "friction": friction,
    }

    total_dist = locomotion["detail"]["total_distance"]
    broken = (not stability["pass"]) or total_dist < 0.5 \
        or (frac is not None and frac < 0.3)
    solid = all(d["pass"] for d in dims.values()) \
        and (frac is None or frac >= 0.7) and locomotion["score"] >= 3

    if broken:
        verdict = "broken"
    elif solid:
        verdict = "solid"
    else:
        verdict = "rough"

    narrative = _narrate(verdict, dims, len(states), total_dist)

    return {
        "schema": SCHEMA,
        "verdict": verdict,
        "narrative": narrative,
        "tick_count": len(states),
        "goal": goal,
        "dimensions": dims,
        "deferred": deferred,
    }


def _narrate(verdict, dims, ticks, total_dist):
    r = dims["control_responsiveness"]
    s = dims["stability"]
    f = dims["friction"]
    bits = ["%d in-world ticks; player travelled %.1f blocks." % (ticks, total_dist)]
    rf = r["detail"].get("fraction")
    if rf is None:
        bits.append("No commanded inputs to gauge responsiveness.")
    else:
        bits.append("Inputs produced the expected response %.0f%% of the time."
                    % (rf * 100))
    if not s["pass"]:
        kinds = s["detail"]["anomaly_kinds"]
        bits.append("Stability anomalies: %s." % (", ".join(kinds) or "present"))
    else:
        bits.append("No stability anomalies.")
    if f["detail"]["longest_stall"] >= 10:
        bits.append("Longest stall %d ticks." % f["detail"]["longest_stall"])
    head = {"solid": "SOLID -- gameplay handled cleanly.",
            "rough": "ROUGH -- playable but with friction.",
            "broken": "BROKEN -- a core gameplay expectation failed."}[verdict]
    return head + " " + " ".join(bits)


def render_markdown(a):
    """Render the assessment dict as a human/AI-readable markdown report (PURE)."""
    lines = ["# CavEX gameplay assessment",
             "",
             "**Verdict: %s**" % a["verdict"].upper(),
             "",
             a["narrative"],
             "",
             "- Schema: `%s`" % a["schema"],
             "- In-world ticks: %d" % a["tick_count"]]
    if a.get("goal"):
        lines.append("- Goal: `%s`" % a["goal"])
    lines += ["", "## Objective rubric", "",
              "| Dimension | Score | Pass | Detail |",
              "|---|---|---|---|"]
    labels = {
        "control_responsiveness": "Control responsiveness",
        "locomotion_progress": "Locomotion / progress",
        "stability": "Stability",
        "friction": "Friction",
    }
    for key, label in labels.items():
        d = a["dimensions"].get(key)
        if not d:
            continue
        detail = ", ".join("%s=%s" % (k, v) for k, v in d["detail"].items())
        lines.append("| %s | %d/5 | %s | %s |" % (
            label, d["score"], "✅" if d["pass"] else "❌", detail))
        if d["evidence"]:
            lines.append("| | | | evidence (run.jsonl idx): %s |" % (
                ", ".join(str(e) for e in d["evidence"])))
    lines += ["", "## Deferred (not scored here)", ""]
    for k, why in a["deferred"].items():
        lines.append("- **%s** — %s" % (k, why))
    lines.append("")
    return "\n".join(lines)


def load_run(path):
    """Read a run.jsonl file into a list of entries (the only I/O in this module)."""
    entries = []
    with open(path) as f:
        for line in f:
            line = line.strip()
            if line:
                entries.append(json.loads(line))
    return entries


# -----------------------------------------------------------------------------
# Self-test: synthetic run logs with known properties -> asserted scores. This is
# the deterministic proof of the scorer and needs no game. Run via --selftest.
# -----------------------------------------------------------------------------
def _entry(tick, pos, orient, action, on_ground=True):
    return {
        "tick": tick,
        "state": {
            "tick": tick, "screen": "ingame", "player": True,
            "pos": pos, "orient": orient, "on_ground": on_ground,
        },
        "action": action,
    }


def _log_solid_walk():
    log = []
    for i in range(50):
        x = round(i * 0.3, 3)
        yaw = round(i * 0.01, 4)
        og = True
        action = "FORWARD=1 LOOK=0.010,0.0"
        if i == 10:                      # one hop, lands as a jump response
            action += " JUMP=1"
        if i == 11:                      # ...left the ground the next tick
            og = False
        log.append(_entry(i, [x, 64.0, 0.0], [yaw, 0.0], action, on_ground=og))
    return log


def _log_teleport():
    log = []
    for i in range(10):
        log.append(_entry(i, [round(i * 0.3, 3), 64.0, 0.0], [0.0, 0.0],
                          "FORWARD=1"))
    log.append(_entry(10, [40.0, 64.0, 0.0], [0.0, 0.0], "FORWARD=1"))   # teleport
    log.append(_entry(11, [0.0, 0.0, 0.0], [0.0, 0.0], "FORWARD=1"))     # origin snap
    return log


def _log_stuck():
    return [_entry(i, [5.0, 64.0, 5.0], [0.0, 0.0], "FORWARD=1") for i in range(20)]


def _selftest():
    # --- solid walk -----------------------------------------------------------
    a = assess(_log_solid_walk())
    assert a["verdict"] == "solid", a["verdict"]
    r = a["dimensions"]["control_responsiveness"]
    assert r["score"] == 5 and r["pass"], r
    loco = a["dimensions"]["locomotion_progress"]
    assert loco["pass"] and loco["score"] >= 3, loco
    assert a["dimensions"]["stability"]["score"] == 5
    assert a["dimensions"]["stability"]["pass"]
    assert a["dimensions"]["friction"]["pass"]
    # deferred dims are reported, not omitted
    assert set(a["deferred"]) == {"mechanic_completion", "feedback_legibility",
                                  "feel"}
    # purity / determinism: same log -> byte-identical assessment
    assert json.dumps(assess(_log_solid_walk()), sort_keys=True) \
        == json.dumps(assess(_log_solid_walk()), sort_keys=True)
    # markdown renders without error
    assert "Verdict: SOLID" in render_markdown(a)

    # --- teleport / origin-snap -> broken via stability -----------------------
    b = assess(_log_teleport())
    sb = b["dimensions"]["stability"]
    assert not sb["pass"], sb
    assert "teleport" in sb["detail"]["anomaly_kinds"]
    assert "origin_snap" in sb["detail"]["anomaly_kinds"]
    assert b["verdict"] == "broken", b["verdict"]

    # --- stuck: input commanded but no movement -> broken ---------------------
    c = assess(_log_stuck())
    rc = c["dimensions"]["control_responsiveness"]
    assert rc["score"] == 0 and not rc["pass"], rc
    assert not c["dimensions"]["locomotion_progress"]["pass"]
    assert c["verdict"] == "broken", c["verdict"]

    # --- meta.clean_exit=False fails stability even on a clean log -------------
    d = assess(_log_solid_walk(), meta={"clean_exit": False})
    assert not d["dimensions"]["stability"]["pass"]
    assert "dirty_exit" in d["dimensions"]["stability"]["detail"]["anomaly_kinds"]

    # --- objectively-measurable goal -----------------------------------------
    e = assess(_log_solid_walk(), goal="move:5")
    assert e["dimensions"]["friction"]["detail"]["ticks_to_goal"] is not None
    # unmeasurable goal is deferred, not failed
    g = assess(_log_solid_walk(), goal="mine a block")
    assert g["dimensions"]["friction"]["detail"][
        "goal_objectively_measurable"] is False

    # --- empty run -> broken, no crash ---------------------------------------
    z = assess([])
    assert z["verdict"] == "broken" and z["tick_count"] == 0

    print("gameplay_assess selftest: PASS")
    return 0


def main():
    ap = argparse.ArgumentParser(description="CavEX gameplay-quality scorer")
    ap.add_argument("--selftest", action="store_true",
                    help="run synthetic logs through the scorer and assert scores")
    ap.add_argument("--run", help="score an existing run.jsonl and print JSON")
    ap.add_argument("--goal", default=None, help="optional stop-condition goal")
    ap.add_argument("--md", action="store_true",
                    help="with --run, print the markdown report instead of JSON")
    args = ap.parse_args()

    if args.selftest:
        return _selftest()
    if args.run:
        a = assess(load_run(args.run), goal=args.goal)
        print(render_markdown(a) if args.md else json.dumps(a, indent=2))
        return 0
    ap.print_help()
    return 2


if __name__ == "__main__":
    sys.exit(main())
