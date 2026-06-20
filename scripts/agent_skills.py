#!/usr/bin/env python3
"""Closed-loop skill primitives for CavEX's live agent input path.

This is the "muscle memory" layer for an AI player. It sits on top of the gated
perceive->act protocol (CAVEX_AGENT + CAVEX_AGENT_GATED, issues #67/#83) and turns
raw single-tick action lines (FORWARD=1, LOOK=dx,dy, MINE=1, PLACE=1, ...) into
reliable, *confirmed* human-style actions: turn to a heading, aim at a specific
block, mine it, place a block, walk to a column, dig down, pillar up, build a
platform / wall, board a boat.

Design rules (every one learned the hard way -- see the memory note
`rig-edge-actions-unreliable-headless`):

  * Everything is CLOSED-LOOP. A primitive acts, reads the exported state back,
    and only reports success once the world actually shows the change -- exactly
    how a human watches the screen. Nothing here is an engine shortcut; all output
    is real action lines through the live source.
  * Exported pos[1] is the EYE Y (eye == pos); feet are EYE_HEIGHT below.
  * forward = (sin yaw, cos yaw) in (x, z); +pitch looks down, ~0 is the horizon.
  * Exported orient/aim trail ~1 frame, so we step in small increments and CONFIRM
    against the next state before nudging again (no blind ramps -> no overshoot).
  * Survival dig completion is WALL-CLOCK timed (tool_dig_delay_ms vs time_get),
    and gated mode blocks the game on our stdin, so a held MINE elapses ~0 real
    time per tick. mine_aimed() therefore sleeps real time per tick.
  * Straight down (pitch >= ~1.5) yields aim=null; we aim at a moderate pitch.
  * The LOOK->angle gain is unknown/uncalibrated, so we MEASURE it at startup.

Standalone proof:
    python3 scripts/agent_skills.py --selftest      # mine + place one real block
"""

import argparse
import json
import math
import os
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "scripts"))
import agent_play_demo as apd  # make_run_dir (isolated scratch world)

EYE_HEIGHT = 1.62
WINDOW = 5            # heightmap window side (STATE_EXPORT_HEIGHT_WINDOW)
HALF = WINDOW // 2


def _wrap(a):
    """Wrap an angle to [-pi, pi]."""
    while a > math.pi:
        a -= 2 * math.pi
    while a < -math.pi:
        a += 2 * math.pi
    return a


def _clamp(v, lo, hi):
    return lo if v < lo else hi if v > hi else v


class SkillError(RuntimeError):
    pass


class GameSession:
    """A live, gated CavEX process you drive one confirmed tick at a time.

    The contract: the game emits one JSON state line per tick and then BLOCKS on
    stdin for exactly one action line. ``self.state`` always holds the state the
    game is currently waiting on; ``step(action)`` answers it and returns the next
    state (the world *after* the action). Decide on ``self.state``, then step().
    """

    def __init__(self, run_dir, seed=42, autoshot=0, world=None, binary=None,
                 quiet=True):
        self.run_dir = run_dir
        self.seed = seed
        self.autoshot = autoshot
        self.world = world
        self.binary = binary or os.path.join(ROOT, "build_pc", "cavex")
        self.quiet = quiet
        self.proc = None
        self.state = None
        self.tick_count = 0
        # Calibrated at start(): signed radians of orient change per 1.0 LOOK unit.
        self.gain_yaw = -250.0    # placeholder; replaced by calibrate()
        self.gain_pitch = 250.0   # placeholder; replaced by calibrate()

    # -- lifecycle ---------------------------------------------------------
    def log(self, msg):
        if not self.quiet:
            print("[skills] " + msg, file=sys.stderr, flush=True)

    def start(self):
        os.makedirs(self.run_dir, exist_ok=True)
        apd.make_run_dir(self.world, self.run_dir, seed=self.seed)
        env = dict(os.environ)
        env.update(CAVEX_AGENT="1", CAVEX_AGENT_GATED="1", CAVEX_AUTOPLAY="1",
                   vblank_mode="0")
        if self.autoshot > 0:
            env["CAVEX_AUTOSHOT"] = str(self.autoshot)
        self.proc = subprocess.Popen(
            [self.binary], cwd=self.run_dir, env=env, stdin=subprocess.PIPE,
            stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True, bufsize=1)
        self.state = self._read_state()
        if self.state is None:
            raise SkillError("game produced no state line on startup")
        self.wait_for_world()
        self.calibrate()
        return self

    def _read_state(self):
        """Read stdout until the next JSON state line; skip engine chatter."""
        for raw in self.proc.stdout:
            raw = raw.strip()
            if raw.startswith("{"):
                try:
                    return json.loads(raw)
                except json.JSONDecodeError:
                    continue
            elif raw:
                self.log("game: " + raw)
        return None  # EOF

    def _send(self, action):
        try:
            self.proc.stdin.write((action or "") + "\n")
            self.proc.stdin.flush()
        except (BrokenPipeError, ValueError):
            raise SkillError("game stdin closed (process died?)")

    def step(self, action="", settle=0):
        """Answer the pending state with one action; return the next state.

        ``settle`` extra neutral ticks let the ~1-frame-late ``aim`` catch up to a
        just-finished turn before a caller inspects it.
        """
        self._send(action)
        nxt = self._read_state()
        if nxt is None:
            raise SkillError("game ended mid-run (EOF)")
        self.state = nxt
        self.tick_count += 1
        for _ in range(settle):
            self._send("")
            self.state = self._read_state()
            if self.state is None:
                raise SkillError("game ended during settle")
            self.tick_count += 1
        return self.state

    def wait_for_world(self, cap=600):
        n = 0
        while not (self.state and self.state.get("player")):
            self.step("")
            n += 1
            if n > cap:
                raise SkillError("player never entered the world")
        return self.state

    def close(self):
        if not self.proc:
            return
        try:
            self.proc.stdin.close()
        except Exception:
            pass
        try:
            self.proc.wait(timeout=10)
        except subprocess.TimeoutExpired:
            self.proc.terminate()

    # -- perception accessors ---------------------------------------------
    def pos(self):
        return self.state.get("pos")

    def yaw(self):
        return self.state.get("orient", [0.0, 0.0])[0]

    def pitch(self):
        return self.state.get("orient", [0.0, 0.0])[1]

    def aim(self):
        return self.state.get("aim")

    def hotbar(self):
        return self.state.get("hotbar", {})

    def riding(self):
        return bool(self.state.get("riding"))

    def heightmap(self):
        hm = self.state.get("heightmap")
        return hm if hm and len(hm) == WINDOW * WINDOW else None

    def height_at(self, dx, dz):
        """Heightmap value at column offset (dx east, dz south): the y of the
        FIRST AIR cell above the surface (== top solid y + 1)."""
        hm = self.heightmap()
        if hm is None:
            return None
        c, r = dx + HALF, dz + HALF
        if 0 <= c < WINDOW and 0 <= r < WINDOW:
            return hm[r * WINDOW + c]
        return None

    def top_solid_y(self, dx, dz):
        """y of the topmost SOLID block in column offset (dx,dz), or None.

        The exported heightmap is the first-air y, so the surface block is one
        below it (confirmed against the raycast: aiming at height_at lands on
        height_at-1)."""
        h = self.height_at(dx, dz)
        return None if h is None else h - 1

    def feet_column(self):
        """Integer (x, z) block column the player stands in."""
        p = self.pos()
        return int(math.floor(p[0])), int(math.floor(p[2]))

    # -- calibration -------------------------------------------------------
    DEFAULT_GAIN = -2.0   # measured look sensitivity (rad of orient per LOOK unit)

    def calibrate(self):
        """Measure signed radians-of-orient per unit LOOK so control is robust to
        the look-sensitivity config constant. The constant is fixed (~-2.0 here),
        but a SINGLE measurement occasionally reads ~0 when it lands on a frame
        boundary (export lag) -- which used to leave a bogus placeholder gain and
        silently break all pitch control. So we take the median of several probes
        per axis and fall back to the known constant when a reading is implausible
        (never to a placeholder)."""
        def measure(axis):
            vals = []
            for _ in range(3):
                before = self.yaw() if axis == 0 else self.pitch()
                self.step("LOOK=0.10,0.0" if axis == 0 else "LOOK=0.0,0.10")
                after = self.yaw() if axis == 0 else self.pitch()
                d = _wrap(after - before) if axis == 0 else (after - before)
                vals.append(d / 0.10)
                self.step("LOOK=-0.10,0.0" if axis == 0 else "LOOK=0.0,-0.10")
            vals.sort()
            return vals[len(vals) // 2]                  # median is spike-robust

        gy, gp = measure(0), measure(1)
        self.gain_yaw = gy if 0.5 <= abs(gy) <= 50 else self.DEFAULT_GAIN
        self.gain_pitch = gp if 0.5 <= abs(gp) <= 50 else self.DEFAULT_GAIN
        self.log("calibrated gain_yaw=%.2f gain_pitch=%.2f (raw %.2f/%.2f)"
                 % (self.gain_yaw, self.gain_pitch, gy, gp))
        return self.gain_yaw, self.gain_pitch

    # -- orientation control ----------------------------------------------
    def look_angles_to(self, bx, by, bz):
        """Desired (yaw, pitch) to look at the centre of block (bx,by,bz).

        Matches the engine's exact raycast (main.c camera_ray_pick):
            dir = (sin(rx)*sin(ry), cos(ry), cos(rx)*sin(ry))
        so pitch ``ry`` is the polar angle from +Y: 0=straight up, pi/2=horizon,
        pi=straight down. eye == pos (exported pos[1] is the eye Y)."""
        px, py, pz = self.pos()
        dx = (bx + 0.5) - px
        dy = (by + 0.5) - py
        dz = (bz + 0.5) - pz
        yaw = math.atan2(dx, dz)                  # heading rx (forward = sin,cos)
        pitch = math.atan2(math.hypot(dx, dz), dy)  # ry from +Y; >pi/2 looks down
        return yaw, pitch

    def turn_to(self, yaw_des=None, pitch_des=None, tol=0.025, max_ticks=80,
                hold=""):
        """Closed-loop turn to a heading, confirming against exported orient.

        Steps LOOK in small, gain-scaled increments and re-reads each tick, so the
        ~1-frame export lag can never accumulate into an overshoot. ``hold`` lets a
        caller keep e.g. FORWARD held while turning."""
        for _ in range(max_ticks):
            ey = _wrap(yaw_des - self.yaw()) if yaw_des is not None else 0.0
            ep = (pitch_des - self.pitch()) if pitch_des is not None else 0.0
            if abs(ey) <= tol and abs(ep) <= tol:
                return True
            # radians error -> LOOK units, clamped per tick for a smooth, lag-safe pan
            ldx = _clamp(ey / self.gain_yaw, -0.18, 0.18) if yaw_des is not None else 0.0
            ldy = _clamp(ep / self.gain_pitch, -0.18, 0.18) if pitch_des is not None else 0.0
            self.step((hold + " " if hold else "") + "LOOK=%.4f,%.4f" % (ldx, ldy))
        ey = _wrap(yaw_des - self.yaw()) if yaw_des is not None else 0.0
        ep = (pitch_des - self.pitch()) if pitch_des is not None else 0.0
        return abs(ey) <= tol and abs(ep) <= tol

    def aim_at(self, bx, by, bz, max_ticks=90):
        """Orient until the exported ``aim`` raycast lands on block (bx,by,bz).

        Geometry gets us close; a short confirm-and-refine search corrects any
        small model error and the export lag. Returns True once aim == target."""
        yaw_des, pitch_des = self.look_angles_to(bx, by, bz)
        self.turn_to(yaw_des, pitch_des, tol=0.02, max_ticks=max_ticks)
        # Let the render-time aim catch up, then verify.
        self.step("", settle=1)
        if self._aim_is(bx, by, bz):
            return True
        # Refine: a small pitch/yaw search to slide the crosshair onto the block.
        # Bounded -- a 3x3 cross, short turns, and a hard tick budget. Calibrated
        # geometry lands most aims first-try (above), so this only runs on occluded
        # cells, where a tight search lands the same hits as a wide one but without
        # the ~770-tick doomed-search tail that made builds crawl.
        start = self.tick_count
        for dp in (0.06, -0.06, 0.0):
            for dyaw in (0.0, 0.05, -0.05):
                if dp == 0.0 and dyaw == 0.0:
                    continue                 # turn_to already converged here
                self.turn_to(yaw_des + dyaw, pitch_des + dp, tol=0.02, max_ticks=8)
                self.step("", settle=1)
                if self._aim_is(bx, by, bz):
                    return True
                if self.tick_count - start > 60:
                    return False             # budget spent -> give up (no false place)
        return self._aim_is(bx, by, bz)

    def _aim_is(self, bx, by, bz):
        a = self.aim()
        return bool(a) and a["x"] == bx and a["y"] == by and a["z"] == bz

    # -- inventory ---------------------------------------------------------
    def select(self, item_id, max_ticks=12):
        """Scroll the hotbar until the held item id matches (one slot per pulse)."""
        for _ in range(max_ticks):
            if self.hotbar().get("item") == item_id:
                return True
            self.step("SCROLL_RIGHT=1")
            self.step("")  # release so the next pulse is a fresh edge
        return self.hotbar().get("item") == item_id

    # -- mining ------------------------------------------------------------
    def mine_aimed(self, timeout_s=6.0, tick_sleep=0.05):
        """Hold MINE on the currently-aimed block until it breaks.

        Keeps orientation FROZEN (no LOOK / move) so dig progress is not reset, and
        sleeps real time each tick because survival dig completion is wall-clock
        timed and gated mode would otherwise elapse ~0 seconds. Returns True when
        the aimed block changes/disappears."""
        a0 = self.aim()
        if not a0:
            return False
        target = (a0["x"], a0["y"], a0["z"])
        start = time.time()
        while time.time() - start < timeout_s:
            self.step("MINE=1")
            time.sleep(tick_sleep)
            a = self.aim()
            if a is None or (a["x"], a["y"], a["z"]) != target or \
               a.get("block") != a0.get("block"):
                self.step("")  # release
                return True
        self.step("")
        return False

    def mine_block(self, bx, by, bz, timeout_s=6.0):
        """Aim at (bx,by,bz) and mine it; confirm via the surface heightmap drop."""
        if not self.aim_at(bx, by, bz):
            return False
        # Remember this column's surface height to confirm the break.
        cx, cz = self.feet_column()
        before = self.height_at(bx - cx, bz - cz)
        if not self.mine_aimed(timeout_s=timeout_s):
            return False
        self.step("", settle=1)
        cx, cz = self.feet_column()
        after = self.height_at(bx - cx, bz - cz)
        if before is not None and after is not None and after < before:
            return True
        # Fallback confirm: nothing solid remains where we aimed.
        return not self._aim_is(bx, by, bz)

    # -- placing -----------------------------------------------------------
    def place_block(self, bx, by, bz, item=None):
        """Place a block at (bx,by,bz) by aiming at the solid support below it.

        Confirms by the target column's surface height rising to >= by."""
        if item is not None and not self.select(item):
            return False
        support = (bx, by - 1, bz)            # build on top of the block beneath
        if not self.aim_at(*support):
            return False
        cx, cz = self.feet_column()
        before = self.height_at(bx - cx, bz - cz)
        # Pulse PLACE as a clean rising edge.
        self.step("PLACE=1")
        self.step("", settle=1)
        cx, cz = self.feet_column()
        after = self.height_at(bx - cx, bz - cz)
        if before is not None and after is not None and after >= by and after > before:
            return True
        # Fallback confirm: we now aim at a solid block where the target is.
        return self._aim_is(bx, by, bz)

    # -- navigation --------------------------------------------------------
    def goto(self, tx, tz, tol=0.7, max_ticks=80, level=True):
        """Walk to the centre of world column (tx,tz). Closed-loop steer toward
        the heading with FORWARD held; auto-jumps when a step blocks progress, and
        gives up FAST when genuinely wedged (an enclosed stance) instead of
        spinning to the cap -- the per-cell build-speed killer.

        Keeps the view near the horizon (``level``) for a natural human gait."""
        last = None
        no_progress = 0
        for _ in range(max_ticks):
            px, _, pz = self.pos()
            dx, dz = (tx + 0.5) - px, (tz + 0.5) - pz
            if math.hypot(dx, dz) <= tol:
                self.step("")             # arrive: stop walking
                return True
            ey = _wrap(math.atan2(dx, dz) - self.yaw())
            ldx = _clamp(ey / self.gain_yaw, -0.20, 0.20)
            ldy = 0.0
            if level:
                ldy = _clamp((math.pi / 2 - self.pitch()) / self.gain_pitch,
                             -0.10, 0.10)
            act = "FORWARD=1 LOOK=%.4f,%.4f" % (ldx, ldy)
            if last is not None and math.dist((px, pz), last) < 0.03:
                no_progress += 1
            else:
                no_progress = 0           # real progress: a long open walk never bails
            last = (px, pz)
            if no_progress >= 8:          # wedged -> stop fast (was a 300-tick spin)
                self.step("")
                return False
            if no_progress and no_progress % 2 == 0:
                act += " JUMP=1"          # hop the obstacle/step
            self.step(act)
        px, _, pz = self.pos()
        return math.hypot((tx + 0.5) - px, (tz + 0.5) - pz) <= tol

    def reachable(self, bx, by, bz, maxd=4.0):
        px, py, pz = self.pos()
        return math.dist((px, py, pz), (bx + 0.5, by + 0.5, bz + 0.5)) <= maxd

    def place_world(self, bx, by, bz, item=None):
        """Place a block anywhere by first walking to a clear stance just SOUTH of
        it and looking DOWN at the support's top face.

        This is the human-like, reliable placement path: standing adjacent forces
        a steep approach (camera_hit.side == UP -> block lands on top, not the
        side) and removes the occlusion that breaks same-spot multi-block builds.
        """
        sx, sz = bx, bz - 1
        px, _, pz = self.pos()
        if math.hypot((sx + 0.5) - px, (sz + 0.5) - pz) > 0.7:
            self.goto(sx, sz, tol=0.6)
        # A floor block sits at the player's own feet level; if the body (0.6 wide)
        # overlaps the target column the engine refuses the place ("can't place
        # inside yourself"). Back off north until the south edge clears.
        for _ in range(6):
            px, py, pz = self.pos()
            feet_y = round(py - EYE_HEIGHT)
            if by != feet_y or (bz - pz) >= 1.05:
                break
            self.turn_to(yaw_des=0.0, pitch_des=self.pitch(), tol=0.12,
                         max_ticks=8)        # face +z (toward target)
            self.step("BACKWARD=1")          # ... so BACKWARD steps away (north)
        return self.place_block(bx, by, bz, item=item)

    # -- composite builds --------------------------------------------------
    def build_blocks(self, targets, item=3):
        """Place a list of world blocks (ordered bottom-up by the caller).

        Repositions (goto) when a target is out of arm's reach, re-selects the
        item after moving, and confirms each placement. Returns (placed, total)."""
        total = len(targets)
        placed = 0
        if not self.select(item):
            return placed, total
        # Build bottom-up (y), and far-to-near in z so the south stance cell
        # (bz-1) is always clear ground, never a block we just placed.
        order = sorted(targets, key=lambda b: (b[1], -b[2], b[0]))
        for (bx, by, bz) in order:
            if self.place_world(bx, by, bz, item=item):
                placed += 1
        return placed, total

    def dig_down(self, n=1, timeout_s=8.0):
        """Mine straight down ``n`` blocks (the block under the player's feet).

        After each break the player FALLS one block; the next dig must not start
        mid-fall, or the aimed cell keeps changing and CavEX resets dig progress
        (the flaky 2/3 failure mode). So we wait to land fully before each dig and
        retry a dig once."""
        broke = 0
        for _ in range(n):
            self._await_ground()              # fully landed before the next dig
            cx, cz = self.feet_column()
            sy = self.top_solid_y(0, 0)
            if sy is None:
                break
            ok = self.mine_block(cx, sy, cz, timeout_s=timeout_s)
            if not ok:
                self._await_ground()          # settle and retry once
                ok = self.mine_block(cx, sy, cz, timeout_s=timeout_s)
            if not ok:
                break
            broke += 1
            self.step("", settle=2)
        return broke

    def build_floor(self, x0, z0, w, l, y, item=3):
        """Fill a w x l platform of blocks at level ``y``, corner at (x0,z0)."""
        targets = [(x0 + i, y, z0 + j) for i in range(w) for j in range(l)]
        return self.build_blocks(targets, item=item)

    def build_walls(self, x0, z0, w, l, height, item=3, y0=None, door=True):
        """Build the perimeter walls of a w x l footprint, ``height`` blocks tall.

        Corner at (x0,z0); optionally leave a 1-wide doorway in the south wall."""
        if y0 is None:
            y0 = self.top_solid_y(0, 0) + 1
        door_cell = (x0 + w // 2, z0) if door else None
        targets = []
        for h in range(height):
            for i in range(w):
                for j in range(l):
                    if i in (0, w - 1) or j in (0, l - 1):
                        if h == 0 and door_cell == (x0 + i, z0 + j):
                            continue                 # leave the doorway open
                        targets.append((x0 + i, y0 + h, z0 + j))
        return self.build_blocks(targets, item=item)

    def pillar_up(self, n, item=3, retries=3):
        """Build an n-high pillar UNDER the player by jump-and-place -- the human
        "pillaring" technique, and the reliable way to build UPWARD.

        Standing-and-placing fails for vertical stacks: the reach-ray hits the
        support's vertical SIDE face (not its top), so the block lands beside it.
        Here we instead stand ON the column and aim STRAIGHT DOWN at the block
        below -- which always presents its TOP face (proven reliable by dig_down)
        -- JUMP once, and PLACE into the gap below during the airborne window. Each
        course is confirmed by the player's feet rising exactly one block.

        One JUMP edge per course, with on-ground cooldown between attempts, so two
        jumps never fall inside CavEX's ~10-tick double-tap creative-flight window.
        Returns the number of courses confirmed risen."""
        if not self.select(item):
            return 0
        risen = 0
        for _ in range(n):
            if self.state.get("flying"):
                break                       # never want flight mode here
            cx, cz = self.feet_column()
            base_feet = round(self.pos()[1] - EYE_HEIGHT)   # current feet level y
            course_ok = False
            for _ in range(retries):
                self._await_ground()
                if self._jump_place_course(cx, cz, base_feet, item):
                    course_ok = True
                    break
            if not course_ok:
                break
            risen += 1
        return risen

    def _await_ground(self, cap=20):
        for _ in range(cap):
            if self.state.get("on_ground"):
                return True
            self.step("")
        return bool(self.state.get("on_ground"))

    def _jump_place_course(self, cx, cz, base_feet, item):
        """One jump-and-place course; True iff the feet rose one block."""
        if not self.aim_at(cx, base_feet - 1, cz):   # straight down -> top face
            return False
        self.select(item)
        self.step(""); self.step("")          # settle; widen the jump cooldown
        self.step("JUMP=1")                   # exactly one rising edge
        placed = False
        for _ in range(14):
            if self.state.get("flying"):
                return False                  # accidental flight -> bail out
            feet = self.pos()[1] - EYE_HEIGHT
            # The new block lands at y=base_feet (occupies [base_feet, base_feet+1]);
            # the player AABB bottom == feet, and the engine refuses a place that
            # touches the body, so the feet must be STRICTLY above the block top.
            # The jump apex (~+1.34) clears base_feet+1.05 for a ~5-tick window.
            if (not placed and not self.state.get("on_ground")
                    and feet >= base_feet + 1.05):
                self.step("PLACE=1")
                placed = True
            else:
                self.step("")                 # no JUMP (one edge), no LOOK (frozen)
            if placed and self.state.get("on_ground"):
                break
        self.step("", settle=2)
        return round(self.pos()[1] - EYE_HEIGHT) == base_feet + 1


# ---------------------------------------------------------------------------
# Self-test: prove mine + place mutate the world, end-to-end, headless.
# ---------------------------------------------------------------------------
def selftest(run_dir, seed=42, quiet=False):
    s = GameSession(run_dir, seed=seed, quiet=quiet)
    s.start()
    cx, cz = s.feet_column()
    result = {"calibrated": [round(s.gain_yaw, 1), round(s.gain_pitch, 1)],
              "mine": "NO", "place": "NO"}
    # Mine the topmost solid block one step east of the player.
    sy_e = s.top_solid_y(1, 0)
    if sy_e is not None:
        if s.mine_block(cx + 1, sy_e, cz):
            result["mine"] = "YES (broke block at %d,%d,%d)" % (cx + 1, sy_e, cz)
    # Place a block on top of the surface one step west (target the air cell).
    sy_w = s.top_solid_y(-1, 0)
    if sy_w is not None:
        if s.place_block(cx - 1, sy_w + 1, cz, item=3):  # dirt on top
            result["place"] = "YES (placed at %d,%d,%d)" % (cx - 1, sy_w + 1, cz)
    s.close()
    return result


def composite_selftest(run_dir, seed=42, quiet=False):
    """Prove the composite skills. Each check runs in its OWN isolated session so
    results are independent and deterministic (no cross-task world coupling)."""
    r = {}

    # Navigate: walk 5 blocks south and confirm arrival.
    s = GameSession(run_dir + "_goto", seed=seed, quiet=quiet).start()
    cx, cz = s.feet_column()
    arrived = s.goto(cx, cz + 5, tol=0.9)
    px, _, pz = s.pos()
    r["goto"] = "arrived" if arrived else ("dist=%.1f" % math.hypot(
        cx + 0.5 - px, cz + 5.5 - pz))
    s.close()

    # Build a 2x2 floor north-east of the player.
    s = GameSession(run_dir + "_floor", seed=seed, quiet=quiet).start()
    cx, cz = s.feet_column()
    base = s.top_solid_y(0, 0) + 1
    floor = [(cx + 1, base, cz + 1), (cx + 2, base, cz + 1),
             (cx + 1, base, cz + 2), (cx + 2, base, cz + 2)]
    placed, total = s.build_blocks(floor, item=3)
    r["floor"] = "%d/%d" % (placed, total)
    s.close()

    # Build a 3-high pillar under the player (pillar-jump).
    s = GameSession(run_dir + "_pillar", seed=seed, quiet=quiet).start()
    p = s.pillar_up(3, item=3)
    r["pillar"] = "%d/3" % p
    s.close()
    return r


def main():
    ap = argparse.ArgumentParser(description="CavEX closed-loop skills")
    ap.add_argument("--selftest", action="store_true",
                    help="prove mine + place one block")
    ap.add_argument("--composite", action="store_true",
                    help="prove navigate + build a floor + stack")
    ap.add_argument("--run-dir", default="/tmp/cavex_skills_selftest")
    ap.add_argument("--seed", type=int, default=42)
    ap.add_argument("--quiet", action="store_true")
    args = ap.parse_args()
    if args.selftest:
        r = selftest(args.run_dir, seed=args.seed, quiet=args.quiet)
        print("SELFTEST:", json.dumps(r, indent=2))
        return 0 if (r["mine"].startswith("YES") and r["place"].startswith("YES")) else 1
    if args.composite:
        r = composite_selftest(args.run_dir, seed=args.seed, quiet=args.quiet)
        print("COMPOSITE:", json.dumps(r, indent=2))
        # Gate on the SOLID skills (navigate + horizontal build). Vertical
        # stacking (pillar) is a known-hard skill reported for information -- it
        # needs pillar-jumping and is a target for the improvement loop.
        ok = r["floor"] == "4/4" and r["goto"] == "arrived"
        return 0 if ok else 1
    ap.print_help()
    return 2


if __name__ == "__main__":
    sys.exit(main())
