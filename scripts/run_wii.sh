#!/usr/bin/env bash
# Build the Wii .dol, stage config/assets onto Dolphin's SD image, and launch
# headless Dolphin. The Qt GUI segfaults on this machine, so this always uses
# dolphin-emu-nogui.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
SD="${WII_SD:-$HOME/.local/share/dolphin-emu/Load/WiiSD.raw}"
LOG="${CAVEX_LOG:-$ROOT_DIR/dolphin-run.log}"
DOLPHIN_CFG="$HOME/.config/dolphin-emu"
# Bluetooth address of the real Wii Remote to connect straight to, skipping the
# inquiry-and-name-read dance that makes cold connects slow and flaky. Override
# with WII_REMOTE_MAC=... (empty disables).
REMOTE_MAC="${WII_REMOTE_MAC-CC:9E:00:5A:5A:52}"

# The toolchain is not on PATH in plain shells.
export DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"
export DEVKITPPC="${DEVKITPPC:-$DEVKITPRO/devkitPPC}"
export PATH="$DEVKITPPC/bin:$DEVKITPRO/tools/bin:$PATH"

echo "==> Build (Wii)"
cd "$ROOT_DIR"
make

# Dolphin only ever reads this exact path; it silently creates a blank card
# there and ignores a populated image anywhere else.
if [ ! -f "$SD" ]; then
	echo "error: SD image not found at $SD" >&2
	echo "       see .claude/skills/gen-beta-world (world staging)" >&2
	exit 1
fi

echo
echo "==> Stage SD image ($SD)"
mcopy -i "$SD" -o config.json ::/config.json
mcopy -i "$SD" -s -o assets ::/
echo "    config.json + assets staged"

# A running GNOME Bluetooth panel holds the adapter in continuous discovery,
# which starves Dolphin's own HCI inquiry and stops a real Wii Remote from ever
# connecting (it shows up as "Device or resource busy" in Dolphin's log).
if pgrep -f 'gnome-control-center bluetooth' >/dev/null 2>&1; then
	echo
	echo "WARNING: the GNOME Bluetooth settings panel is open."
	echo "         Close it or a real Wii Remote will not connect."
fi

# Don't pkill -f here: the pattern matches this script too, and comm is
# truncated at 15 chars so pgrep -x never matches dolphin-emu-nogui either.
if pgrep -f 'dolphin-emu-nogui' | grep -qv "^$$\$"; then
	echo
	echo "note: another dolphin-emu-nogui looks like it is already running."
fi

# Make sure Dolphin is actually writing the Wiimote log we rely on, and point
# it at a known remote so it can connect without a full inquiry sweep.
if [ -f "$DOLPHIN_CFG/Logger.ini" ]; then
	sed -i 's/^WriteToFile = False/WriteToFile = True/; s/^Wiimote = False/Wiimote = True/' \
		"$DOLPHIN_CFG/Logger.ini"
fi
if [ -n "$REMOTE_MAC" ] && [ -f "$DOLPHIN_CFG/Dolphin.ini" ]; then
	if grep -q '^WiimoteAutoConnectAddresses' "$DOLPHIN_CFG/Dolphin.ini"; then
		sed -i "s|^WiimoteAutoConnectAddresses.*|WiimoteAutoConnectAddresses = $REMOTE_MAC|" \
			"$DOLPHIN_CFG/Dolphin.ini"
	else
		sed -i "0,/^\[Core\]/s||[Core]\nWiimoteAutoConnectAddresses = $REMOTE_MAC|" \
			"$DOLPHIN_CFG/Dolphin.ini"
	fi
	echo "    auto-connect remote: $REMOTE_MAC"
fi

echo
echo "==> Launch (press 1+2 on a real Wii Remote to connect)"
echo "    log: $LOG   (overwritten each run; tail -f it from another shell)"
echo
# Truncate rather than append so the log always describes THIS run only.
: > "$LOG"
: > "$HOME/.local/share/dolphin-emu/Logs/dolphin.log" 2>/dev/null || true
# Keep Dolphin on stdout as well as the file, and preserve its exit status
# rather than tee's.
set -o pipefail
dolphin-emu-nogui -p x11 -v Vulkan -e ./CavEX.dol "$@" 2>&1 | tee "$LOG"
