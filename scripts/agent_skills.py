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
import nav                      # pure A* path planner (round 8 navigation fix)
import mem_guard                # OOM safeguard: gate launch + abort before OOM

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
                 quiet=True, visible=False, mem_floor_mb=None,
                 mem_launch_floor_mb=None):
        self.run_dir = run_dir
        self.seed = seed
        self.autoshot = autoshot
        self.world = world
        self.binary = binary or os.path.join(ROOT, "build_pc", "cavex")
        self.quiet = quiet
        self.visible = visible    # show a real window (don't force the headless path)
        self.proc = None
        self.state = None
        self.tick_count = 0
        # OOM safeguard (the user's machine has repeatedly OOM-crashed under heavy
        # runs). Refuse to launch below mem_launch_floor_mb; a watchdog aborts the
        # game if MemAvailable later falls below mem_floor_mb. Override via env so
        # any entrypoint (live driver, playtest) inherits one knob.
        self.mem_floor_mb = int(mem_floor_mb if mem_floor_mb is not None
                                else os.environ.get("CAVEX_MEM_FLOOR_MB",
                                                    mem_guard.DEFAULT_CRITICAL_FLOOR_MB))
        self.mem_launch_floor_mb = int(
            mem_launch_floor_mb if mem_launch_floor_mb is not None
            else os.environ.get("CAVEX_MEM_LAUNCH_FLOOR_MB",
                                mem_guard.DEFAULT_LAUNCH_FLOOR_MB))
        self._mem_guard = None
        self._mem_tripped = False
        # Session-global last-JUMP tick. The double-tap-to-fly toggle (~10 ticks)
        # doesn't reset between goto() calls, so the cooldown can't either: an
        # intensive skill (level_pad does dozens of gotos + digs) would otherwise
        # land two jumps astride a call boundary and flip on creative flight,
        # which corrupts every ground-relative read. Tracked here, not per-call.
        self._last_jump_tick = -100
        # Calibrated at start(): signed radians of orient change per 1.0 LOOK unit.
        self.gain_yaw = -250.0    # placeholder; replaced by calibrate()
        self.gain_pitch = 250.0   # placeholder; replaced by calibrate()

    # -- lifecycle ---------------------------------------------------------
    def log(self, msg):
        if not self.quiet:
            print("[skills] " + msg, file=sys.stderr, flush=True)

    def start(self):
        # OOM safeguard: refuse to even generate the world / spawn the game when
        # there isn't enough headroom -- a heavy run on a near-full box is exactly
        # what has OOM-crashed this machine.
        ok, msg = mem_guard.safe_to_launch(self.mem_launch_floor_mb)
        self.log(msg)
        if not ok:
            raise SkillError(msg)
        os.makedirs(self.run_dir, exist_ok=True)
        apd.make_run_dir(self.world, self.run_dir, seed=self.seed)
        env = dict(os.environ)
        env.update(CAVEX_AGENT="1", CAVEX_AGENT_GATED="1", CAVEX_AUTOPLAY="1")
        # vblank_mode=0 is REQUIRED here (GLX init/SwapBuffers hangs without it on
        # this Mesa/Xwayland setup) -- it only disables vsync, it does NOT hide the
        # window. So we always set it; a real window still appears whenever there's
        # a DISPLAY. `visible` just documents that this session is meant to be
        # watched (and, with a DISPLAY set by the caller, it is).
        env["vblank_mode"] = "0"
        if self.autoshot > 0:
            env["CAVEX_AUTOSHOT"] = str(self.autoshot)
        self.proc = subprocess.Popen(
            [self.binary], cwd=self.run_dir, env=env, stdin=subprocess.PIPE,
            stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True, bufsize=1)
        # Arm the watchdog: if MemAvailable later falls below the floor, kill the
        # game so the box never reaches the kernel OOM-killer. step() then aborts.
        self._mem_guard = mem_guard.MemGuard(
            pid=self.proc.pid, floor_mb=self.mem_floor_mb, interval=1.0,
            on_low=self._on_mem_low, log=self.log)
        self._mem_guard.start()
        self.state = self._read_state()
        if self.state is None:
            raise SkillError("game produced no state line on startup")
        self.wait_for_world()
        self.calibrate()
        return self

    def _on_mem_low(self, avail_mb):
        """Watchdog callback: memory hit the floor -> abort the game now."""
        self._mem_tripped = True
        self.log("mem_guard: aborting game pid %s at %d MiB available"
                 % (getattr(self.proc, "pid", "?"), avail_mb))
        try:
            if self.proc and self.proc.poll() is None:
                self.proc.terminate()
        except Exception:
            pass

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
        if self._mem_tripped:
            raise SkillError("aborted: memory guard tripped (low RAM) -- game killed "
                             "to protect the machine from OOM")
        self._send(action)
        nxt = self._read_state()
        if nxt is None:
            if self._mem_tripped:
                raise SkillError("aborted: memory guard tripped (low RAM) -- game "
                                 "killed to protect the machine from OOM")
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
        if self._mem_guard is not None:
            self._mem_guard.stop()
            self._mem_guard = None
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

    def local_ground_y(self):
        """Stance-relative SITE ground level: the (median top-solid y, spread)
        over the local heightmap window.

        Read this *at the build site* (walk there first) so build height tracks
        the ground you're about to build ON -- never a transient stance left on a
        hill by a previous command. That stance-vs-site mismatch is the root of
        the "avatar cranes up at the sky, 0/N placed" failure: build height was
        taken from top_solid_y(0,0) wherever the player happened to stop, so on
        sloped terrain every target Y floated above (sky-aim) or buried below the
        actual columns. The median is robust to a stray tree/pit in the window;
        the spread tells the caller how flat the site is."""
        hm = self.heightmap()
        if hm is None:
            p = self.pos()
            return int(round(p[1] - EYE_HEIGHT)), 0
        vals = sorted(hm)
        median_first_air = vals[len(vals) // 2]
        spread = vals[-1] - vals[0]
        return median_first_air - 1, spread   # (top-solid y, flatness spread)

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
        # Sky-aim guard. Floors/walls always place onto a support at, or just
        # below, the feet. A support well ABOVE the feet means the build height
        # came from a bad (hill) stance -- the avatar would crane up at the sky
        # and the place would fail anyway. Bail fast and loud, the cheap signal
        # that a build height needs re-surveying, instead of burning the budget
        # aiming at nothing. (pillar_up builds upward via its own straight-down
        # jump-place path, not place_block, so this never blocks legit height.)
        _, py, _ = self.pos()
        feet_y = py - EYE_HEIGHT
        if (by - 1) - feet_y > 1.5:
            self.log("place_block: support y=%d is %.1f above feet -- sky-aim "
                     "guard, refusing (re-survey build height)"
                     % (by - 1, (by - 1) - feet_y))
            return False
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
    def _unfly(self, tries=3):
        """Recover from accidental creative flight by double-tapping JUMP (the
        same gesture that toggled it on), confirmed via the ``flying`` flag, then
        wait out the fall. Ground-relative reads (pos, heightmap) are meaningless
        while airborne, so navigation calls this before trusting them."""
        for _ in range(tries):
            if not self.state.get("flying"):
                return True
            self.step("JUMP=1")
            self.step("")                 # release, so the next is a fresh edge
            self.step("JUMP=1")           # second tap within the double-tap window
            self.step("", settle=2)
            for _ in range(20):           # let the drop back to ground finish
                if self.state.get("on_ground"):
                    break
                self.step("")
        return not self.state.get("flying")

    def _within(self, tx, tz, tol):
        px, _, pz = self.pos()
        return math.hypot((tx + 0.5) - px, (tz + 0.5) - pz) <= tol

    def _surface_window(self):
        """The local heightmap as a world-column -> top-solid-y dict for nav.plan
        (None where unsensed). nav never steps onto unknown cells."""
        hm = self.heightmap()
        if hm is None:
            return None
        fx, fz = self.feet_column()
        surf = {}
        for dz in range(-HALF, HALF + 1):
            for dx in range(-HALF, HALF + 1):
                surf[(fx + dx, fz + dz)] = hm[(dz + HALF) * WINDOW + (dx + HALF)] - 1
        return surf

    def _step_toward(self, tx, tz, jump=False, level=True):
        """One tick of heading-controlled walking toward column-centre (tx,tz)."""
        px, _, pz = self.pos()
        ey = _wrap(math.atan2((tx + 0.5) - px, (tz + 0.5) - pz) - self.yaw())
        ldx = _clamp(ey / self.gain_yaw, -0.20, 0.20)
        ldy = _clamp((math.pi / 2 - self.pitch()) / self.gain_pitch,
                     -0.10, 0.10) if level else 0.0
        act = "FORWARD=1 LOOK=%.4f,%.4f" % (ldx, ldy)
        if jump and (self.tick_count - self._last_jump_tick) >= 13:
            act += " JUMP=1"            # global cooldown: never double-tap into flight
            self._last_jump_tick = self.tick_count
        self.step(act)

    def _walk_waypoint(self, wx, wz, kind, max_ticks=16):
        """Drive to an ADJACENT planned waypoint, confirming arrival by feet
        column. 'step' jumps the +1; 'drop' waits out the fall. Returns True once
        the feet land in (wx,wz)."""
        for _ in range(max_ticks):
            if self.state.get("flying"):
                self._unfly()
            if self.feet_column() == (wx, wz):
                return True
            self._step_toward(wx, wz, jump=(kind == "step"))
            if kind == "drop":
                self._await_ground(cap=6)   # ground-relative reads are junk mid-fall
        return self.feet_column() == (wx, wz)

    def goto(self, tx, tz, tol=0.7, max_ticks=200, allow_dig=False, level=True):
        """Navigate to world column (tx,tz) by A* over the live heightmap window.

        Receding horizon (Baritone-style): plan over what's visible, walk the
        first few waypoints, re-sense, re-plan. Unlike the old greedy walker this
        CLIMBS +1 terrace steps, DROPS up to 3, and ROUTES AROUND obstacles --
        the navigation-over-distance blocker. Each planned edge is executed by an
        existing confirmed primitive (walk/step/drop here; dig/bridge are Stage 2
        and gated by ``allow_dig``). Falls back to a direct heading-walk only when
        no heightmap is exported. Stuck-recovery escalates (replan -> nudge ->
        bail) instead of the old single 8-tick give-up."""
        start_tick = self.tick_count
        stuck = 0
        while self.tick_count - start_tick < max_ticks:
            if self.state.get("flying"):
                self._unfly()
            if self._within(tx, tz, tol):
                self.step("")
                return True
            surf = self._surface_window()
            if surf is None:                       # no perception -> greedy fallback
                self._step_toward(tx, tz, jump=(stuck >= 2), level=level)
                stuck = stuck + 1 if stuck < 3 else 3
                continue
            fx, fz = self.feet_column()
            path = nav.plan(surf, (fx, fz), (tx, tz), allow_dig=allow_dig,
                            min_progress=nav.WALK)
            if not path:
                stuck += 1
                if stuck >= 3:                     # genuinely wedged -> bail
                    self.step("")
                    return self._within(tx, tz, tol)
                self._recover_nudge()
                continue
            stuck = 0
            for (wx, wz, _wy, kind) in path[:4]:   # walk a few, then re-sense
                if kind in ("dig", "bridge"):
                    break                          # Stage 2; re-plan without it
                if not self._walk_waypoint(wx, wz, kind):
                    break                          # lost the waypoint -> re-plan
                if self._within(tx, tz, tol):
                    self.step("")
                    return True
                if self.tick_count - start_tick >= max_ticks:
                    break
        return self._within(tx, tz, tol)

    def _recover_nudge(self):
        """Dislodge a wedge: one cooldown-respecting jump + a brief strafe, so the
        next re-plan starts from a slightly different cell."""
        if (self.tick_count - self._last_jump_tick) >= 13:
            self.step("JUMP=1")
            self._last_jump_tick = self.tick_count
        self.step("LEFT=1")
        self.step("LEFT=1")

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

    def _footprint_columns(self, x0, z0, w, l):
        """Boustrophedon (snake) order over a w x l footprint, so consecutive
        columns are always neighbours -- the walker never crosses the whole pad
        between two columns, and on terraced ground the step between neighbours
        stays small (one terrace), which goto can climb."""
        cols = []
        for j in range(l):
            row = [(x0 + i, z0 + j) for i in range(w)]
            if j % 2:                       # serpentine: reverse every other row
                row.reverse()
            cols.extend(row)
        return cols

    def _tile_centers(self, x0, z0, w, l):
        """Stance points covering the footprint with OVERLAP: spaced every 3 so
        each column sits within +/-1 of some stance. +/-1 keeps every mined
        column close (reliable aim/reach) and the overlap means a single off-by-
        one landing can't drop a column from coverage -- the v1/v2 failure where
        one stance per 5-span lost its edge columns whenever goto drifted."""
        def axis(a0, n):
            cs = list(range(a0 + 1, a0 + n, 3))     # a0+1, a0+4, ... each covers +/-1
            if not cs:
                return [a0]
            if cs[-1] < a0 + n - 2:                 # ensure the far edge is covered
                cs.append(a0 + n - 2)
            return cs
        return [(cx, cz) for cz in axis(z0, l) for cx in axis(x0, w)]

    def level_pad(self, x0, z0, w, l, target=None, cap_item=None, max_cut=8):
        """Terraform a w x l footprint to ONE flat plane.

        This closes the Round-6 cap: on terraced/sloped ground a single-level
        floor leaves gaps, because a down-step column has no support block to
        place onto. Rather than fight that, flatten the ground first.

        Mining-only, therefore reliable: the target plane defaults to the LOWEST
        surveyed column, so every other column is dug DOWN to it (digging is
        proven; filling UP would hit the side-face stacking problem pillar_up
        exists to avoid). To avoid the precision trap of standing on every
        column (v1: only ~half landed exactly), we instead stand at a few tile
        centres, read the 5x5 heightmap, and MINE each in-window column's excess
        from that stance -- which also clears any trees rooted in the footprint
        for free. Caps the flat plane with an optional ``cap_item`` foundation.

        Returns (leveled_columns, total_columns, target_y)."""
        centers = self._tile_centers(x0, z0, w, l)
        in_fp = lambda x, z: x0 <= x < x0 + w and z0 <= z < z0 + l
        # -- survey: read each tile's heightmap window into a column->top map --
        heights = {}
        for (cx, cz) in centers:
            self.goto(cx, cz, tol=0.7)
            fx, fz = self.feet_column()
            hm = self.heightmap()
            if not hm:
                continue
            for r in range(WINDOW):
                for c in range(WINDOW):
                    wx, wz = fx + (c - HALF), fz + (r - HALF)
                    if in_fp(wx, wz):
                        heights[(wx, wz)] = hm[r * WINDOW + c] - 1
        if not heights:
            return 0, w * l, None
        tgt = target if target is not None else min(heights.values())
        self.log("level_pad: surveyed %d/%d cols, target y=%d (range %d..%d)"
                 % (len(heights), w * l, tgt,
                    min(heights.values()), max(heights.values())))
        # -- dig: from each tile centre, mine every in-window column down to tgt
        #    (top block first; the centre column itself goes straight down) --
        leveled = set()
        for (cx, cz) in centers:
            self.goto(cx, cz, tol=0.7)
            fx, fz = self.feet_column()
            window = [(wx, wz) for (wx, wz) in heights
                      if abs(wx - fx) <= 1 and abs(wz - fz) <= 1]
            for (wx, wz) in window:
                if (wx, wz) == (fx, fz):
                    continue                 # the stance column: handled below
                cur = self.top_solid_y(wx - fx, wz - fz)
                cut = 0
                while cur is not None and cur > tgt and cut < max_cut:
                    if not self.mine_block(wx, cur, wz):
                        break
                    cur = self.top_solid_y(wx - fx, wz - fz)
                    cut += 1
                if cur is not None and cur <= tgt:
                    leveled.add((wx, wz))
            if in_fp(fx, fz) and (self.top_solid_y(0, 0) or tgt) > tgt:
                self.dig_down(self.top_solid_y(0, 0) - tgt)
                if self.top_solid_y(0, 0) <= tgt:
                    leveled.add((fx, fz))
        if cap_item is not None:
            self.build_floor(x0, z0, w, l, tgt + 1, item=cap_item)
        return len(leveled), w * l, tgt

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

    def make_boat(self, bx=None, bz=None, item=333):
        """Craft-free "create a boat": select a boat item, place it on the ground
        ahead (placing the item spawns the rideable boat entity and consumes the
        item), then board it. Confirms placement by the item being consumed and
        boarding by the exported ``riding`` flag -- and because the agent placed
        the boat it knows the cell, so no nearby-entity export is needed.
        Returns True once riding."""
        if not self.select(item):
            return False
        cx, cz = self.feet_column()
        if bx is None:
            bx, bz = cx + 1, cz                 # default: one block ahead (east)
        sy = self.top_solid_y(bx - cx, bz - cz)
        if sy is None:
            return False
        if not self.aim_at(bx, sy, bz):
            return False
        before = self.hotbar().get("count", 0)
        self.step("PLACE=1")
        self.step("", settle=3)                 # let the boat entity spawn
        hb = self.hotbar()
        if hb.get("item") == item and hb.get("count", 0) >= before:
            return False                        # item not consumed -> not placed
        # Board: stand by the boat's cell and use until aboard.
        self.goto(bx, bz, tol=0.9)
        for _ in range(10):
            self.step("PLACE=1")
            self.step("", settle=2)
            if self.riding():
                return True
        return self.riding()


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
