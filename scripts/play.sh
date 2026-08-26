#!/usr/bin/env bash
# Launch the native PC build for couch co-op and keep the desktop awake.
#
# GNOME's idle monitor only watches the keyboard and mouse, so a gamepad-only
# session trips the idle timer and the screen blanks + locks (idle-delay is 60 s
# on this box). Wrapping the game in an idle inhibitor holds that off for the
# DURATION OF PLAY ONLY — nothing is changed permanently; normal locking resumes
# the moment the game exits.
#
# Usage:  scripts/play.sh [extra cavex args]
#   CAVEX_2P defaults to 1 (two players); set CAVEX_2P=0 for single-player.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
RUN="$ROOT/build_pc/run"
BIN="$ROOT/build_pc/cavex"

if [ ! -x "$BIN" ]; then
	echo "PC build not found at $BIN — build it first:" >&2
	echo "  (cd '$ROOT/build_pc' && cmake .. -DCMAKE_BUILD_TYPE=Debug && make -j\$(nproc))" >&2
	exit 1
fi

export CAVEX_2P="${CAVEX_2P:-1}" # two-player couch co-op by default
cd "$RUN"                        # cavex reads config.json + saves/ from here

if command -v gnome-session-inhibit >/dev/null 2>&1; then
	# GNOME-native: inhibits the idle screensaver/lock (and suspend) while cavex runs.
	exec gnome-session-inhibit --inhibit idle:suspend \
		--reason "CavEX gamepad session" "$BIN" "$@"
elif command -v systemd-inhibit >/dev/null 2>&1; then
	exec systemd-inhibit --what=idle:sleep \
		--why="CavEX gamepad session" "$BIN" "$@"
else
	echo "(no idle inhibitor found — the screen may lock while playing)" >&2
	exec "$BIN" "$@"
fi
