#!/usr/bin/env python3
"""Bind a USB gamepad to CavEX by pressing what you want for each action.

CavEX reads USB pads through synthetic input codes (see source/platform/input.c):
  3000 + n           -> joystick button n
  3100 + 2*axis + d  -> joystick axis `axis`, d=0 negative half / d=1 positive
The joystick INDEX is the player (js0 -> player 1, js1 -> player 2), so one pad's
layout applies to both players.

This asks you, for each CavEX action, to press the control YOU want for it -- a
d-pad direction OR a face/shoulder button, whatever you like. It records the real
button number / axis (no assumptions about the pad's layout or which thumb does
what), then writes the bindings into config_pc.json (keyboard bindings kept) and
prints the matching Dolphin targets.

Usage:
  python3 scripts/pad_ident.py                 # read /dev/input/js0
  python3 scripts/pad_ident.py --js 1          # read the other pad
  python3 scripts/pad_ident.py --config build_pc/run/config.json   # write live run config
  python3 scripts/pad_ident.py --dry-run       # show, don't write
"""
import argparse
import json
import os
import struct
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
JS_EVENT = struct.Struct("IhBB")  # u32 time, s16 value, u8 type, u8 number
JS_BUTTON, JS_AXIS, JS_INIT = 0x01, 0x02, 0x80
AXIS_ON = 16000   # |value| (of 32767) to count a d-pad/axis push as "pressed"

# (label, Dolphin target, [config keys that receive this control]).
# Every player-1 key has its player-2 twin; the joystick index picks the player.
ACTIONS = [
    ("MOVE forward",   "D-Pad/Up",    ["player_forward", "player2_forward",
                                       "gui_up", "player2_gui_up"]),
    ("MOVE back",      "D-Pad/Down",  ["player_backward", "player2_backward",
                                       "gui_down", "player2_gui_down"]),
    ("MOVE left",      "D-Pad/Left",  ["player_left", "player2_left",
                                       "gui_left", "player2_gui_left"]),
    ("MOVE right",     "D-Pad/Right", ["player_right", "player2_right",
                                       "gui_right", "player2_gui_right"]),
    ("LOOK up",        "IR/Up",       ["player_look_up", "player2_look_up"]),
    ("LOOK down",      "IR/Down",     ["player_look_down", "player2_look_down"]),
    ("LOOK left",      "IR/Left",     ["player_look_left", "player2_look_left"]),
    ("LOOK right",     "IR/Right",    ["player_look_right", "player2_look_right"]),
    ("MINE / dig",     "Buttons/A",   ["item_action_left", "player2_action_left",
                                       "gui_click", "player2_gui_click"]),
    ("PLACE / use",    "Buttons/2",   ["item_action_right", "player2_action_right",
                                       "gui_click_alt", "player2_gui_click_alt"]),
    ("JUMP",           "Buttons/B",   ["player_jump", "player2_jump"]),
    ("INVENTORY",      "Buttons/1",   ["inventory", "player2_inventory"]),
]


def drain(fd, secs=0.35):
    t0 = time.time()
    while time.time() - t0 < secs:
        try:
            os.read(fd, JS_EVENT.size)
        except BlockingIOError:
            time.sleep(0.01)


def read_control(fd):
    """Block until a button press or a firm axis push. Returns (code, human)."""
    drain(fd)
    while True:
        try:
            data = os.read(fd, JS_EVENT.size)
        except BlockingIOError:
            time.sleep(0.01)
            continue
        if len(data) < JS_EVENT.size:
            time.sleep(0.01)
            continue
        _, value, typ, number = JS_EVENT.unpack(data)
        if typ & JS_INIT:
            continue
        if (typ & JS_BUTTON) and value == 1:
            return 3000 + number, "button %d" % number
        if (typ & JS_AXIS) and abs(value) >= AXIS_ON:
            d = 1 if value > 0 else 0
            return 3100 + 2 * number + d, "axis %d%s" % (number, "+" if d else "-")


def set_pad_code(arr, code):
    return [c for c in arr if c < 3000] + [code]


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--js", type=int, default=0)
    ap.add_argument("--config", default=os.path.join(ROOT, "config_pc.json"))
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    dev = "/dev/input/js%d" % args.js
    try:
        fd = os.open(dev, os.O_RDONLY | os.O_NONBLOCK)
    except OSError as e:
        print("error: cannot open %s (%s). Is the pad plugged in?" % (dev, e),
              file=sys.stderr)
        return 2

    print("=== CavEX gamepad setup (%s) ===" % dev)
    print("For each action, press the button OR d-pad direction you want for it.\n"
          "Press Ctrl-C to abort.\n")

    codes, dolphin = {}, []
    try:
        for label, dtarget, keys in ACTIONS:
            print("  %-14s -> press it ... " % label, end="", flush=True)
            code, human = read_control(fd)
            print(human)
            for k in keys:
                codes[k] = code
            dolphin.append((label, dtarget, human))
            time.sleep(0.3)  # let it release
    except KeyboardInterrupt:
        print("\naborted; no changes written.")
        return 1
    finally:
        os.close(fd)

    with open(args.config) as f:
        cfg = json.load(f)
    inp = cfg.setdefault("input", {})
    for key, code in codes.items():
        inp[key] = set_pad_code(inp.get(key, []), code)

    print("\n=== native PC build (config.json) ===")
    if args.dry_run:
        print(json.dumps({k: inp[k] for k in codes}, indent=2))
    else:
        with open(args.config, "w") as f:
            json.dump(cfg, f, indent="\t")
            f.write("\n")
        print("wrote pad bindings to %s" % args.config)
        run_cfg = os.path.join(ROOT, "build_pc", "run", "config.json")
        if os.path.abspath(args.config) != os.path.abspath(run_cfg) \
           and os.path.exists(run_cfg):
            print("note: also copy to your run dir:  cp %s %s"
                  % (args.config, run_cfg))

    print("\n=== Dolphin (Wii route) — WiimoteNew.ini targets ===")
    for label, dtarget, human in dolphin:
        print("  %-14s -> %-11s  (pad %s)" % (label, dtarget, human))
    return 0


if __name__ == "__main__":
    sys.exit(main())
