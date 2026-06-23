#!/usr/bin/env python3
"""Live, WATCHABLE CavEX driver — open a real window and let a brain play it in
real time, one skill at a time, while you watch.

The point of this file (vs. the headless eval rig): the brain is NOT a regex
planner. The skills in agent_skills.py are a TOOLBOX; the brain reads the live
state and decides which skill to call next. In the Claude Code harness that brain
is Claude itself (it steers this process across shell calls). A standalone,
away-from-keyboard brain would instead wire agent_planner.LLMPlanner.llm_complete
to the Anthropic API and call dispatch() from there.

It opens a VISIBLE window (GameSession(visible=True)) and keeps the world live by
idling a neutral tick whenever no command is pending, so the window animates
instead of freezing between decisions. Steer it across separate shell calls with
two files under <dir> (default /tmp/cavex_live):

  * append one command per line to  <dir>/commands  as "<seq> <verb> <args...>"
  * read the matching result from   <dir>/results   as  "RESULT <seq> <ok> <state>"

Verbs (each = one closed-loop, state-confirmed skill):
  goto X Z | mine X Y Z | place X Y Z [item] | make_boat | dig_down N |
  pillar_up N | build_floor X0 Z0 W L [item] | build_walls X0 Z0 W L H [item] |
  level_pad X0 Z0 W L [cap_item] | build_pad X0 Z0 W L [foundation] [wall] |
  look | quit
(level_pad flattens terraced/sloped ground to one plane so big floors complete;
 build_pad = level_pad + foundation + a walled shell, in one command.)

Usage:  python3 scripts/agent_live.py [dir] [--seed N]
"""

import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import agent_skills as sk


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    seed, autoshot = 42, 0
    for a in sys.argv[1:]:
        if a.startswith("--seed") and "=" in a:
            seed = int(a.split("=")[1])
        if a.startswith("--autoshot") and "=" in a:
            autoshot = int(a.split("=")[1])
    d = args[0] if args else "/tmp/cavex_live"
    os.makedirs(d, exist_ok=True)
    cmds_path = os.path.join(d, "commands")
    res_path = os.path.join(d, "results")
    open(cmds_path, "a").close()

    print("[live] opening a visible window (watch your screen)...",
          file=sys.stderr, flush=True)
    s = sk.GameSession(os.path.join(d, "run"), seed=seed, visible=True,
                       autoshot=autoshot, quiet=False).start()
    print("[live] in-world. brain: append commands to %s" % cmds_path,
          file=sys.stderr, flush=True)

    def compact():
        st = s.state
        return {"tick": st.get("tick"), "pos": st.get("pos"),
                "on_ground": st.get("on_ground"), "riding": st.get("riding"),
                "hotbar": st.get("hotbar"), "aim": st.get("aim"),
                "feet_col": list(s.feet_column())}

    def dispatch(verb, a):
        if verb == "goto":
            return s.goto(int(a[0]), int(a[1]))
        if verb == "mine":
            return s.mine_block(int(a[0]), int(a[1]), int(a[2]))
        if verb == "place":
            return s.place_world(int(a[0]), int(a[1]), int(a[2]),
                                 item=int(a[3]) if len(a) > 3 else 3)
        if verb == "make_boat":
            return s.make_boat()
        if verb == "dig_down":
            return s.dig_down(int(a[0])) == int(a[0])
        if verb == "pillar_up":
            return s.pillar_up(int(a[0])) == int(a[0])
        if verb == "build_floor":
            x0, z0, w, l = int(a[0]), int(a[1]), int(a[2]), int(a[3])
            s.goto(x0 + w // 2, z0 + l // 2)   # stand ON the site, not a stale stance
            gy, spread = s.local_ground_y()    # survey HERE: base tracks the site
            base = gy + 1
            p, t = s.build_floor(x0, z0, w, l, base,
                                 item=int(a[4]) if len(a) > 4 else 3)
            return "%d/%d@y%d(spread%d)" % (p, t, base, spread)
        if verb == "build_walls":
            x0, z0, w, l = int(a[0]), int(a[1]), int(a[2]), int(a[3])
            s.goto(x0 + w // 2, z0 + l // 2)   # walk to the site first
            gy, spread = s.local_ground_y()    # now reads the floor we just laid
            base = gy + 1
            p, t = s.build_walls(x0, z0, w, l, int(a[4]),
                                 item=int(a[5]) if len(a) > 5 else 3,
                                 y0=base, door=True)
            return "%d/%d@y%d(spread%d)" % (p, t, base, spread)
        if verb == "level_pad":          # terraform a footprint flat (mine highs)
            x0, z0, w, l = int(a[0]), int(a[1]), int(a[2]), int(a[3])
            cap = int(a[4]) if len(a) > 4 else None
            lev, tot, tgt = s.level_pad(x0, z0, w, l, cap_item=cap)
            return "%d/%d@y%s" % (lev, tot, tgt)
        if verb == "build_pad":          # flatten -> foundation -> walls, in one go
            x0, z0, w, l = int(a[0]), int(a[1]), int(a[2]), int(a[3])
            foundation = int(a[4]) if len(a) > 4 else 1
            wall = int(a[5]) if len(a) > 5 else 5
            lev, tot, tgt = s.level_pad(x0, z0, w, l, cap_item=foundation)
            p, t = s.build_walls(x0, z0, w, l, 1, item=wall,
                                 y0=tgt + 2, door=True)
            return "lvl %d/%d walls %d/%d @y%s" % (lev, tot, p, t, tgt)
        if verb == "face":               # turn to look at a block (for a reveal)
            return s.aim_at(int(a[0]), int(a[1]), int(a[2]))
        if verb == "act":                # raw action line, repeated N ticks
            n = int(a[-1]) if a and a[-1].isdigit() else 1
            body = a[:-1] if (a and a[-1].isdigit()) else a
            for _ in range(n):
                s.step(" ".join(body))
            return True
        if verb == "look":
            return True
        raise ValueError("unknown verb %r" % verb)

    processed = 0
    res = open(res_path, "a", buffering=1)
    try:
        while True:
            lines = open(cmds_path).read().splitlines()
            if len(lines) > processed:
                line = lines[processed].strip()
                processed += 1
                if not line:
                    continue
                parts = line.split()
                seq, verb, rest = parts[0], (parts[1] if len(parts) > 1 else ""), parts[2:]
                if verb == "quit":
                    res.write("RESULT %s done bye\n" % seq)
                    break
                print("[live] >> %s %s" % (verb, " ".join(rest)),
                      file=sys.stderr, flush=True)
                try:
                    ok = dispatch(verb, rest)
                except Exception as exc:
                    ok = "ERR:%s" % exc
                res.write("RESULT %s %s %s\n" % (seq, ok, json.dumps(compact())))
                print("[live] << %s -> %s" % (verb, ok), file=sys.stderr, flush=True)
            else:
                s.step("")          # idle: keep the window live between commands
    finally:
        s.close()


if __name__ == "__main__":
    main()
