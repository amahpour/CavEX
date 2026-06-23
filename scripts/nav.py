#!/usr/bin/env python3
"""Pure A* navigation planner over a Minecraft-style surface heightmap.

This is the planner half of the navigation fix (round 8). It is *pure* -- no
game, no I/O, no engine -- so it unit-tests deterministically against synthetic
heightmaps (`python3 scripts/nav.py --selftest`) and the closed-loop executor in
agent_skills.goto() drives it against the live game.

Why this exists: the old `goto` was a greedy "face the target, hold FORWARD, jump
when stuck" walker. It collapses the Y axis, so it cannot reason about climbing a
terrace step or routing around a tree -- the navigation-over-distance blocker. A
real agent (mineflayer-pathfinder, Baritone) instead runs A* over a graph whose
EDGES encode the affordances of moving in a blocky world:

  * walk   (Dy == 0)              -> hold FORWARD toward the cell
  * step   (Dy == +1)             -> FORWARD + JUMP (one terrace up)
  * drop   (Dy in -max_drop..-1)  -> FORWARD off the edge, then land
  * dig    (Dy >= +2, optional)   -> MINE the blocking column, then walk (costly)
  * bridge (gap, optional)        -> PLACE a block, then walk (costly)

Costs are ported from Baritone's intent (not its exact ticks): walking is cheap,
placing/digging are penalised heavily so the planner is non-destructive-first and
conserves inventory -- the 2-phase behaviour falls out of the cost numbers with
no separate pass. The heuristic is octile XZ distance + vertical term, which is
admissible (never overestimates), so A* stays optimal.

Partial observability: the caller only knows a local window (the exported
heightmap). `surface` may return None for unknown cells; A* never expands into
them, so a plan is naturally bounded to what's visible. When the goal is outside
the known area, `plan` returns the path to the *reachable cell closest to the
goal* (Baritone's best-so-far), and the executor re-senses + re-plans as it walks
-- the receding-horizon loop.
"""

import argparse
import heapq
import math
import sys

# Edge costs (relative; ported from Baritone/mineflayer intent, not exact ticks).
WALK = 4.6            # one block on the flat
STEP_EXTRA = 1.4      # added for a +1 jump-up
DIAG = math.sqrt(2)   # diagonal multiplier on the walk cost
DIG_PENALTY = 8.0     # per block dug through (discourage, but allow when gated)
PLACE_PENALTY = 25.0  # per block placed to bridge (discourage; conserve blocks)
INF = 1e6             # finite (costs are summed) sentinel for "never"

# Fall cost by drop height (gentle; only meaningful relative to WALK).
_FALL = {1: 0.4, 2: 1.0, 3: 1.8, 4: 2.8}

# 4 cardinal + 4 diagonal neighbour offsets.
_CARD = [(1, 0), (-1, 0), (0, 1), (0, -1)]
_DIAG = [(1, 1), (1, -1), (-1, 1), (-1, -1)]


def _as_surface(surface):
    """Accept either a dict {(x,z): top_solid_y} or a callable (x,z)->y|None."""
    if callable(surface):
        return surface
    return lambda x, z: surface.get((x, z))


def _octile(dx, dz):
    """Octile distance in block units (diagonal moves allowed)."""
    dx, dz = abs(dx), abs(dz)
    return (dx + dz) + (DIAG - 2) * min(dx, dz)


def edge(surface, cx, cz, nx, nz, *, diagonal, max_step, max_drop,
         allow_dig, allow_bridge):
    """Cost + kind of moving from (cx,cz) to neighbour (nx,nz), or (None, None)
    if the move is not allowed. Pure function of the surface heights."""
    surf = surface
    yc, yn = surf(cx, cz), surf(nx, nz)
    if yc is None or yn is None:
        return None, None                      # unknown terrain -> never expand
    base = WALK * (DIAG if diagonal else 1.0)
    dy = yn - yc
    if diagonal:
        # Don't cut blocked corners: both orthogonal cells must be walkable-ish.
        ya, yb = surf(nx, cz), surf(cx, nz)
        if ya is None or yb is None:
            return None, None
        if (ya - yc) > max_step or (yb - yc) > max_step:
            return None, None
    if dy == 0:
        return base, "walk"
    if 1 <= dy <= max_step:
        return base + STEP_EXTRA * dy, "step"
    if -max_drop <= dy <= -1:
        return base + _FALL.get(-dy, INF), "drop"
    if dy >= max_step + 1:                      # a wall: dig through it (gated)
        if allow_dig and not diagonal:
            return base + DIG_PENALTY * (dy - max_step), "dig"
        return None, None
    # dy < -max_drop: too deep to drop safely; bridge across only if allowed.
    if allow_bridge and not diagonal:
        return base + PLACE_PENALTY, "bridge"
    return None, None


def plan(surface, start, goal, *, max_step=1, max_drop=3, allow_dig=False,
         allow_bridge=False, diagonals=True, max_expansions=4096,
         min_progress=0.0):
    """A* from `start` to `goal` (both (x,z)) over `surface`.

    Returns a list of waypoints [(x, z, y, kind), ...] from start (EXCLUSIVE) to
    the goal -- or, if the goal is unreachable within the known surface, to the
    reachable cell whose heuristic to the goal is smallest (best-so-far), so the
    caller can walk that far and re-plan. Returns [] if already at goal or no
    progress is possible.

    `surface` is a dict {(x,z): top_solid_y} or a callable (x,z)->y|None
    (None == unknown/unsensed; A* never steps onto it)."""
    surf = _as_surface(surface)
    sx, sz = start
    gx, gz = goal
    if (sx, sz) == (gx, gz):
        return []
    if surf(sx, sz) is None:
        return []

    def h(x, z):
        d = _octile(gx - x, gz - z) * WALK
        ys, yg = surf(x, z), surf(gx, gz)
        if ys is not None and yg is not None:
            d += abs(yg - ys) * STEP_EXTRA     # admissible vertical term
        return d

    start_h = h(sx, sz)
    openpq = [(start_h, 0.0, (sx, sz))]
    g = {(sx, sz): 0.0}
    came = {}                                  # node -> (prev, kind)
    best = (sx, sz)                            # best-so-far by heuristic
    best_h = start_h
    expansions = 0

    neighbours = _CARD + (_DIAG if diagonals else [])
    while openpq:
        f, gc, (cx, cz) = heapq.heappop(openpq)
        if gc > g.get((cx, cz), INF):
            continue                           # stale heap entry
        if (cx, cz) == (gx, gz):
            best = (cx, cz)
            break
        expansions += 1
        if expansions > max_expansions:
            break
        for i, (dx, dz) in enumerate(neighbours):
            nx, nz = cx + dx, cz + dz
            diagonal = i >= len(_CARD)
            cost, kind = edge(surf, cx, cz, nx, nz, diagonal=diagonal,
                              max_step=max_step, max_drop=max_drop,
                              allow_dig=allow_dig, allow_bridge=allow_bridge)
            if cost is None:
                continue
            ng = gc + cost
            if ng < g.get((nx, nz), INF):
                g[(nx, nz)] = ng
                came[(nx, nz)] = ((cx, cz), kind)
                nh = h(nx, nz)
                heapq.heappush(openpq, (ng + nh, ng, (nx, nz)))
                if nh < best_h:
                    best_h, best = nh, (nx, nz)

    target = (gx, gz) if (gx, gz) in came or (gx, gz) == (sx, sz) else best
    if target == (sx, sz):
        return []
    # Reconstruct start->target.
    rev = []
    node = target
    while node != (sx, sz):
        prev, kind = came[node]
        rev.append((node[0], node[1], surf(node[0], node[1]), kind))
        node = prev
    rev.reverse()
    # Best-so-far must make real progress, else report "stuck".
    if target != (gx, gz):
        progressed = start_h - best_h
        if progressed < min_progress or not rev:
            return []
    return rev


# --------------------------------------------------------------------------
# Deterministic self-test: synthetic heightmaps -> asserted plans. No game.
# --------------------------------------------------------------------------
def _grid(rows):
    """Build a surface dict from a list of int rows; (x,z)=(col,row)."""
    return {(x, z): rows[z][x] for z in range(len(rows)) for x in range(len(rows[0]))}


def _kinds(path):
    return [p[3] for p in path]


def _selftest():
    n = 0

    # 1. Flat straight line east: 3 walks, no detour.
    flat = _grid([[64] * 6 for _ in range(3)])
    p = plan(flat, (0, 1), (3, 1), diagonals=False)
    assert [(w[0], w[1]) for w in p] == [(1, 1), (2, 1), (3, 1)], p
    assert set(_kinds(p)) == {"walk"}, p
    n += 1

    # 2. A +1 terrace step in the middle must be CLIMBED (greedy goto can't).
    stair = _grid([
        [64, 64, 65, 65],
        [64, 64, 65, 65],
        [64, 64, 65, 65],
    ])
    p = plan(stair, (0, 1), (3, 1), diagonals=False)
    assert (1, 1, 64, "walk") in p and any(k == "step" for *_, k in p), p
    assert p[-1][:2] == (3, 1), p
    n += 1

    # 3. A 2-high wall with dig OFF -> route AROUND it; with dig ON -> shorter
    #    path may tunnel. Wall at x=2 for z=0,1 but open at z=2.
    wall = _grid([
        [64, 64, 66, 64],
        [64, 64, 66, 64],
        [64, 64, 64, 64],
    ])
    p_around = plan(wall, (0, 0), (3, 0), allow_dig=False)
    assert p_around and p_around[-1][:2] == (3, 0), p_around
    assert all(k != "dig" for *_, k in p_around), p_around   # never digs when off
    assert any((w[0], w[1]) == (2, 2) for w in p_around), p_around  # detoured south
    p_dig = plan(wall, (0, 0), (3, 0), allow_dig=True, diagonals=False)
    assert any(k == "dig" for *_, k in p_dig), p_dig         # allowed -> tunnels
    n += 1

    # 4. A drop of 2 is allowed (<= max_drop); a drop of 5 is NOT -> unreachable.
    ledge = _grid([
        [64, 64, 62, 62],
        [64, 64, 62, 62],
    ])
    p = plan(ledge, (0, 0), (3, 0), diagonals=False)
    assert any(k == "drop" for *_, k in p) and p[-1][:2] == (3, 0), p
    cliff = _grid([
        [64, 64, 59, 59],
        [64, 64, 59, 59],
    ])
    p = plan(cliff, (0, 0), (3, 0), max_drop=3, diagonals=False)
    assert all((w[0], w[1]) != (3, 0) for w in p), p          # 5-drop refused
    n += 1

    # 5. Unknown cells (None) are never traversed: a gap of unknown blocks east,
    #    open detour south -> path goes around the unknown, never onto it.
    surf = dict(_grid([[64] * 4 for _ in range(3)]))
    del surf[(2, 0)]
    del surf[(2, 1)]                                          # unknown column
    p = plan(surf, (0, 0), (3, 0))
    assert p and p[-1][:2] == (3, 0), p
    assert all((w[0], w[1]) not in {(2, 0), (2, 1)} for w in p), p
    n += 1

    # 6. Goal outside the known window -> best-so-far heads toward it.
    p = plan(flat, (0, 1), (50, 1), diagonals=False, min_progress=WALK)
    assert p, "expected a best-so-far segment toward an out-of-window goal"
    assert p[-1][0] > 0 and p[-1][1] == 1, p                  # progressed east
    n += 1

    # 7. Already at goal -> empty plan.
    assert plan(flat, (2, 1), (2, 1)) == []
    n += 1

    print("nav.py selftest: %d cases PASS" % n)
    return 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--selftest", action="store_true")
    args = ap.parse_args()
    if args.selftest:
        return _selftest()
    ap.print_help()
    return 0


if __name__ == "__main__":
    sys.exit(main())
