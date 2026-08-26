---
name: run-wii-dolphin
description: Build the CavEX Wii .dol, stage assets/config/world onto Dolphin's SD image, launch headless Dolphin, and visually verify via frame dump. Use when the user asks to run/test CavEX "on the Wii", "in Dolphin", "emulated", or after changing any PLATFORM_WII code. The Dolphin Qt GUI segfaults on this machine — never use `dolphin-emu`, only `dolphin-emu-nogui`.
---

# Run CavEX in Dolphin (Wii build)

## Purpose

Full loop for the emulated-Wii target: cross-compile → SD staging → launch →
agent-visible verification. This is the "platform truth" target (24 MB MEM1,
GX, WPAD); for game-logic iteration prefer the `dev-native-pc` skill.

## Steps

1. **Build** (toolchain env is not in plain shells):

   ```bash
   cd ~/code/CavEX
   export DEVKITPRO=/opt/devkitpro DEVKITPPC=/opt/devkitpro/devkitPPC
   export PATH=$DEVKITPPC/bin:$DEVKITPRO/tools/bin:$PATH
   make    # -> CavEX.dol (~1.1 MB)
   ```

2. **Stage the SD image** — only needed when assets/config/world changed.
   The image is `~/.local/share/dolphin-emu/Load/WiiSD.raw` (NOT `Wii/sd.raw`
   — that's the NAND; Dolphin auto-creates a blank card at the Load/ path and
   silently ignores anything elsewhere). Kill Dolphin first; use mtools (no
   sudo, no mount):

   ```bash
   pkill -9 -x dolphin-emu-nog; sleep 1
   SD="$HOME/.local/share/dolphin-emu/Load/WiiSD.raw"
   mcopy -i "$SD" -o config.json ::/config.json
   mcopy -i "$SD" -s -o assets ::/
   mcopy -i "$SD" -s -o /tmp/cavexworld/world ::/saves/   # world (see gen-beta-world)
   mdir  -i "$SD" ::/saves/world                          # verify
   ```

   `[Core] WiiSDCard = True` must be in `~/.config/dolphin-emu/Dolphin.ini`
   (already set on this machine).

3. **Launch** (background it so the session isn't blocked):

   ```bash
   cd ~/code/CavEX
   # Log to DISK, never /tmp: /tmp is tmpfs (RAM). A faulting .dol makes Dolphin
   # spew millions of "Invalid write" lines; an uncapped log on tmpfs grew to
   # 5.3 GB and systemd-oomd killed the whole Claude session (2026-08-21).
   LOG=~/.local/share/dolphin-emu/cavex_run.log; : > "$LOG"
   XDG_RUNTIME_DIR=/run/user/1000 DISPLAY=:0 \
   nohup dolphin-emu-nogui -p x11 -v Vulkan -e ./CavEX.dol >"$LOG" 2>&1 &

   # Crash watchdog (MANDATORY for unattended runs): kill Dolphin on the first
   # emulated fault so a game bug can't flood the log + frame dumps and OOM the
   # box. Clean CavEX play never logs "Invalid write", so this never misfires.
   ( until { pgrep -x dolphin-emu-nog >/dev/null; } do :; done              # wait for start
     while pgrep -x dolphin-emu-nog >/dev/null; do
       if grep -qm1 'Invalid write to 0x' "$LOG"; then
         echo "WATCHDOG: emulated fault -> killing Dolphin" >>"$LOG"
         pkill -9 -x dolphin-emu-nog; break
       fi
       sleep 2
     done ) &
   ```

   The invalid-write signature means the .dol dereferenced NULL/garbage (on real
   hardware it would hard-crash). Map the faulting PC with
   `powerpc-eabi-addr2line -f -e CavEX.elf 0x<PC>` (LTO may only name the
   enclosing function; read the disasm around the `st*` for the real store).

   Keyboard/mouse → emulated Wiimote mapping lives in
   `~/.config/dolphin-emu/WiimoteNew.ini`. Controls: WASD move, mouse =
   IR pointer (menus) / edge-pan look (in-game; freezes while LMB held),
   LMB mine, RMB place, Space jump, E inventory, wheel hotbar, Esc save&quit.

   **Two-player (issue #140):** `[Wiimote2] Source = 1` is configured on this
   machine (backup: `WiimoteNew.ini.bak-2p`). Player 2 keys: IJKL move,
   arrows = IR edge-pan look, `,` mine, `.` place, Right-Shift jump,
   `p` inventory, `u`/`o` hotbar. Pick "2 Players" on the world-select
   screen (P1's d-pad left/right = A/D keys) — Wii splits top/bottom.
   For scripted P2 input use a Pipe device on `[Wiimote2]` instead.

4. **See the screen yourself** (no desktop screenshot tool works here):
   enable Dolphin frame dump, then Read the PNGs:

   ```bash
   # before launching:
   grep -q '^\[Movie\]' ~/.config/dolphin-emu/Dolphin.ini || \
     printf '[Movie]\nDumpFrames = True\n' >> ~/.config/dolphin-emu/Dolphin.ini
   printf '[Settings]\nDumpFramesAsImages = True\n' > ~/.config/dolphin-emu/GFX.ini
   # frames appear in ~/.local/share/dolphin-emu/Dump/Frames/framedump_N.png
   ```

   The `[Movie]` key is the master switch — `GFX.ini` alone dumps nothing.
   **Revert both and clear Dump/Frames when done** (~8 GB/hour). Use
   `find ... -delete`, not `rm` (glob overflows past ~10k files).

5. **Scripted input (autonomous play)** — Dolphin Pipe device: `mkfifo
   ~/.local/share/dolphin-emu/Pipes/<name>`, set `Device = Pipe/0/<name>` in a
   temporary WiimoteNew.ini (back up the user's first!), write `PRESS A`,
   `RELEASE A`, `SET MAIN_STICK 0.5 0.5` lines into the fifo. Restore the
   keyboard profile afterward.

## Gotchas

- Process names: `pkill -9 -x dolphin-emu-nog` (15-char comm truncation;
  `-f` patterns self-match your own shell via `~/.config/dolphin-emu` paths).
- Dolphin's emulated Wiimote **extensions never deliver data** to libogc
  (Nunchuk and Classic both stream zeros) — that's why config.json maps
  actions to core buttons and the camera uses the IR fallback. Don't burn
  time remapping extension sticks.
- A `timeout`'d run that exits 124 = ran fine until killed; Dolphin catching
  SIGTERM can hang — follow with `pkill -9 -x`.
