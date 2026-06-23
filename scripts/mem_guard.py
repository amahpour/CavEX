#!/usr/bin/env python3
"""Memory guard for autonomous CavEX runs.

Running the PC build (visible or headless) for demos/playtests has repeatedly
eaten all of the dev box's RAM and OOM-crashed the whole machine -- the game +
Mesa GL + the agent harness + background poll-loops push the box into swap and
the kernel OOM-killer (or the session) dies. This module is the safeguard the
user asked for: never start a heavy run without checking headroom, and abort the
game *before* OOM instead of after.

Pure stdlib (parses /proc/meminfo) -- no psutil dependency, Linux-only (which is
where the game runs).

Two pieces:

  safe_to_launch(floor_mb)  -> (ok, msg). Call before spawning the game; refuse
                               when MemAvailable is below the floor.
  MemGuard(pid, floor_mb)   -> a daemon thread that samples MemAvailable every
                               `interval` s and, when it falls below `floor_mb`
                               (or swap is nearly exhausted), runs `on_low`
                               (default: terminate `pid`) so we abort cleanly.

CLI (use as a standalone monitor while a run is live):
    python3 scripts/mem_guard.py --status
    python3 scripts/mem_guard.py --watch --floor 1500 --pid <game_pid>
"""

import argparse
import os
import signal
import sys
import threading
import time

MEMINFO = "/proc/meminfo"

# Defaults tuned for the 30 GiB dev box that kept OOM-crashing. Hitting these
# means something ate the box -- abort before the kernel OOM-killer fires.
DEFAULT_LAUNCH_FLOOR_MB = 2500   # need this much free to even start the game
DEFAULT_CRITICAL_FLOOR_MB = 1200  # abort the live game if we drop below this
DEFAULT_SWAPFREE_FLOOR_MB = 256   # swap nearly exhausted -> OOM imminent


def read_meminfo():
    """Return /proc/meminfo as a dict of {key: kB int}. Empty on non-Linux."""
    out = {}
    try:
        with open(MEMINFO) as f:
            for line in f:
                parts = line.split(":")
                if len(parts) != 2:
                    continue
                k = parts[0].strip()
                v = parts[1].strip().split()
                if v and v[0].isdigit():
                    out[k] = int(v[0])  # kB
    except OSError:
        pass
    return out


def available_mb():
    """MemAvailable in MiB (-1 if unreadable, so checks fail safe-open)."""
    mi = read_meminfo()
    if "MemAvailable" not in mi:
        return -1
    return mi["MemAvailable"] // 1024


def swap_free_mb():
    mi = read_meminfo()
    if "SwapFree" not in mi:
        return -1
    return mi["SwapFree"] // 1024


def snapshot():
    """Human-readable one-liner of the current memory situation."""
    mi = read_meminfo()
    if not mi:
        return "meminfo unavailable (non-Linux?)"
    avail = mi.get("MemAvailable", 0) // 1024
    total = mi.get("MemTotal", 0) // 1024
    sfree = mi.get("SwapFree", 0) // 1024
    stot = mi.get("SwapTotal", 0) // 1024
    sused = stot - sfree
    return ("MemAvailable %d MiB / %d MiB  |  Swap used %d / %d MiB"
            % (avail, total, sused, stot))


def safe_to_launch(floor_mb=DEFAULT_LAUNCH_FLOOR_MB):
    """(ok, message). Refuse to start a heavy run with too little headroom."""
    avail = available_mb()
    if avail < 0:
        # Can't read meminfo -> not Linux / no /proc. Don't block, but say so.
        return True, "mem_guard: /proc/meminfo unavailable; skipping launch gate"
    if avail < floor_mb:
        return False, ("mem_guard: REFUSING to launch -- only %d MiB available "
                       "(need >= %d). %s" % (avail, floor_mb, snapshot()))
    return True, "mem_guard: ok to launch (%s)" % snapshot()


def _terminate(pid, log):
    """SIGTERM then SIGKILL a pid; never raise."""
    for sig, wait in ((signal.SIGTERM, 3.0), (signal.SIGKILL, 0.0)):
        try:
            os.kill(pid, sig)
            log("mem_guard: sent %s to pid %d" % (sig.name, pid))
        except ProcessLookupError:
            return  # already gone
        except OSError as e:
            log("mem_guard: kill(%d) failed: %s" % (pid, e))
            return
        if wait:
            time.sleep(wait)
            try:
                os.kill(pid, 0)   # still alive?
            except OSError:
                return            # exited on SIGTERM


class MemGuard(threading.Thread):
    """Background watchdog: abort the game before the box OOM-crashes.

    Samples MemAvailable (and SwapFree) every `interval` seconds. On the first
    breach of `floor_mb` (or `swapfree_floor_mb`) it sets `tripped`, calls
    `on_low(avail)` (default: terminate `pid`), and stops. A single warning is
    logged when headroom first dips below `warn_mb`.
    """

    def __init__(self, pid=None, floor_mb=DEFAULT_CRITICAL_FLOOR_MB,
                 warn_mb=None, swapfree_floor_mb=DEFAULT_SWAPFREE_FLOOR_MB,
                 interval=2.0, on_low=None, log=None):
        super().__init__(daemon=True)
        self.pid = pid
        self.floor_mb = floor_mb
        self.warn_mb = warn_mb if warn_mb is not None else floor_mb * 2
        self.swapfree_floor_mb = swapfree_floor_mb
        self.interval = interval
        self.on_low = on_low
        self._log = log or (lambda m: print(m, file=sys.stderr, flush=True))
        self._stop = threading.Event()
        self.tripped = False
        self.low_value = None
        self._warned = False

    def log(self, m):
        self._log(m)

    def run(self):
        while not self._stop.is_set():
            avail = available_mb()
            sfree = swap_free_mb()
            if avail < 0:
                return  # can't monitor -> don't pretend to
            breach = avail < self.floor_mb or (0 <= sfree < self.swapfree_floor_mb)
            if breach:
                self.tripped = True
                self.low_value = avail
                self.log("mem_guard: CRITICAL %s -> aborting run" % snapshot())
                try:
                    if self.on_low is not None:
                        self.on_low(avail)
                    elif self.pid is not None:
                        _terminate(self.pid, self.log)
                except Exception as e:               # never let the guard crash
                    self.log("mem_guard: on_low handler error: %s" % e)
                return
            if not self._warned and avail < self.warn_mb:
                self._warned = True
                self.log("mem_guard: WARNING low headroom -- %s" % snapshot())
            self._stop.wait(self.interval)

    def stop(self):
        self._stop.set()


def _cli(argv=None):
    ap = argparse.ArgumentParser(description="CavEX memory guard")
    ap.add_argument("--status", action="store_true", help="print one snapshot")
    ap.add_argument("--watch", action="store_true", help="loop until floor breached")
    ap.add_argument("--floor", type=int, default=DEFAULT_CRITICAL_FLOOR_MB)
    ap.add_argument("--pid", type=int, default=None,
                    help="kill this pid when the floor is breached")
    ap.add_argument("--interval", type=float, default=2.0)
    args = ap.parse_args(argv)

    if args.status or not args.watch:
        print(snapshot())
        if not args.watch:
            return 0
    g = MemGuard(pid=args.pid, floor_mb=args.floor, interval=args.interval)
    g.start()
    try:
        while g.is_alive():
            time.sleep(0.5)
    except KeyboardInterrupt:
        g.stop()
    if g.tripped:
        print("mem_guard: TRIPPED at %d MiB available" % (g.low_value or -1))
        return 3
    return 0


if __name__ == "__main__":
    sys.exit(_cli())
