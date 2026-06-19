# CavEX (patched clone) — Agent Start

## What this is

**[amahpour/CavEX](https://github.com/amahpour/CavEX)** — authoritative repo for
this project. Descended from [xtreme8000/CavEX](https://github.com/xtreme8000/CavEX)
(Minecraft Beta 1.7.3 recreation for Wii, base `8f70987`), now carrying **local
patches** that make it build, run, and stay stable in Dolphin on this machine — plus a native PC dev build and an autonomous test rig. The
playable world ("Claude World") is generated, not from Mojang assets.

Skills in `.claude/skills/` cover the three core workflows: `run-wii-dolphin`,
`dev-native-pc`, `gen-beta-world`. Read those before reinventing commands.
**`HANDOFF.md` has the current status + open-items roadmap — start there.**

Status: fully playable both ways; ~25 min continuous play, zero crashes on the
current build. All local work is committed to the `amahpour/CavEX` fork (`master`, via merged PRs #1 and #2).

## The two builds — when to use which

| | Wii build (`make` → `CavEX.dol`) | PC build (`build_pc/`) |
|---|---|---|
| Use for | platform truth: 24 MB MEM1 pressure, GX, WPAD input | game logic: world/NBT/lighting/gameplay |
| Debugging | Dolphin frame dump + on-screen debug overlay | native gdb, live asserts, ASan (`build_asan/`) |
| Iteration | rebuild + restage SD + relaunch | `make` + run a binary |

Bug-hunt heuristic proven here: if ASan runs clean but the normal build
crashes, suspect **alignment**, not memory corruption (ASan's allocator
over-aligns — it masked the `-march=native`/malloc-16 crash).

## Local patches (committed to fork `master`) — keep intact

Stability/correctness:
- `source/chunk_mesher.c` — mesher request buffers are **static** (the
  per-remesh 17.5 KB malloc failed under heap pressure → remeshes silently
  stopped forever: "broken block never disappears").
- `source/platform/wii/displaylist.c` + `displaylist.h` — `oom` flag; unchecked
  malloc/realloc used to write GX vertices **through NULL** (millions of
  Dolphin "Invalid write to 0x0" lines, garbage-polygon meshes).
- `source/network/server_local.h` — `MAX_VIEW_DISTANCE 5 → 3` (+ matching fog
  in `main.c`) for MEM1 headroom.
- `source/main.c` — `chdir("sd:/")` after `fatInitDefault()` (cwd isn't SD
  root when Dolphin boots a .dol directly).
- `CMakeLists.txt` — `-O3 -march=native` → `-O2` (**`-march=native` crashes
  the PC build**: AVX 32-byte aligned stores into 16-byte-aligned malloc'd
  `struct chunk`); plus `target_include_directories(... include)` for m-lib.
- `source/platform/pc/gfx.c` — force GLFW X11 backend (Wayland GLFW +
  GLX-only GLEW = segfault in first GL extension call).

Input workarounds (Dolphin's emulated Wiimote **never delivers extension
data** to libogc — Nunchuk and Classic both enumerate but stream zeros; the
Ⓒ/Ⓩ HUD glyphs render regardless and prove nothing):
- `config.json` — mine/place also bound to core buttons A (id 4) / "2" (id 7).
- `source/platform/input.c` — camera fallback: IR-pointer edge-pan when no
  extension stick (dead-zone 0.15, speed 2.0); look **freezes while LMB held**
  because CavEX resets dig progress whenever the aimed cell changes.
- Mouse-wheel hotbar switching (PC build): `source/platform/pc/gfx.c` wires the
  GLFW scroll callback → `input_native_scroll` (`input.c`/`input.h`) which
  latches notches as one-shot "pressed" edges on virtual keys 2000/2001;
  `config_pc.json` binds `scroll_left/right` to them. (Wii build already scrolls
  via `+`/`-` → wheel; folding wheel into the Wii path is HANDOFF item #7.)

PC-build running gotcha: prepend **`vblank_mode=0`** for any unattended/headless
run — Mesa DRI3 `SwapBuffers` blocks on `xcb_wait_for_special_event` forever
when the window isn't being composited. (Interactive play with a visible window
is fine without it.)

## Unit tests (native PC)

End-to-end from repo root (no devkitPro, no GLFW):

```bash
make test
```

This configures `build_test/`, builds `cavex_tests`, runs `ctest`, enforces the
**per-test coverage gate** (each test must add ≥1 newly executed line via gcov),
then prints a per-file coverage summary.

CI runs the same `make test` on **pull requests only**
(`.github/workflows/tests.yml`).

Coverage policy: `scripts/check_test_coverage.sh` runs every registered test in
isolation; tests that add zero unique covered lines fail the gate and must be
removed or rewritten. See GitHub issue #40 for tier 2+ expansion plans.

## Autonomous playtest / gameplay assessment (native PC)

Plays CavEX through the **live** input path (#67) on an isolated scratch world,
logs every perceive→act tick, and scores a gameplay-quality verdict — no human,
no API keys, deterministic (issue #83). Python + docs only; not in `make test`
(it needs the headless PC binary, like the capture rig).

```bash
# end-to-end heuristic episode -> run.jsonl + assessment.json + assessment.md
python3 scripts/ai_playtest.py --policy heuristic --max-ticks 200 --run-dir /tmp/pt --autoshot 4
# the pure scorer's deterministic proof (synthetic logs -> asserted scores; no game)
python3 scripts/gameplay_assess.py --selftest
```

- `scripts/gameplay_assess.py` — **pure** scorer (no I/O/game): a run log →
  objective rubric (control responsiveness, locomotion, stability, friction) +
  evidence refs + a `solid`/`rough`/`broken` verdict; `mechanic_completion` /
  `feedback_legibility` / `feel` are reported **DEFERRED** (need an LLM and/or
  state-export additions), never silently omitted. `--selftest` is the gate.
- `scripts/ai_playtest.py` — orchestrator: gated run (`CAVEX_AGENT=1` +
  `CAVEX_AGENT_GATED=1`, so driver latency never drops/mistimes input), pluggable
  **Policy** seam, writes the log + both report forms, optional `--autoshot`→GIF
  and `--upload`. Reuses `agent_play_demo.make_run_dir` for isolation; `--seed`
  (default 42 → `CAVEX_SEED`) makes the scratch world reproducible.
- Policies: `heuristic` (the `agent_play_demo.py` walker, moved behind
  `HeuristicPolicy`; deterministic regression driver, behaviour unchanged) and a
  `claude-bridge` **stub** — the seam where an LLM reads state (+ optionally the
  latest frame) and returns an action **and** a per-decision feel note. Wired,
  not gating; defers to the heuristic so it runs offline with no key.
- Determinism: fixed `--seed` + heuristic ⇒ identical `assessment.json` across
  runs (every emitted state line is an in-world tick from 0; no menu ticks leak).

Dev rig (TEMPORARY — REMOVE before any "release"/clean commit; HANDOFF item #3):
- Debug overlay lines in `screen_ingame.c` (+ helpers in `input.c`, counters
  in `chunk_mesher.c`/`world.c`/`chunk.c`): WPAD state, mesh sent/recv,
  lighting queue, dig state machine, `mallinfo` free (NB: mallinfo can't see
  ungrown sbrk — don't trust it as absolute headroom).
- `CAVEX_AUTOSHOT=N` env — game dumps its own framebuffer every N frames
  (`autoshot_*.png` in cwd). `CAVEX_AUTOPLAY=1` — auto-enters the first world
  (`screen_select_world.c`). Together these give an agent eyes and hands with no
  screenshot tool or input injection.
- **TRAP** (origin-snap hunt, currently armed): RPC ring buffers in
  `client_interface.c` + `server_local.c`, detector + dump writer in `main.c`.
  On a >8-block teleport or origin-snap during play it appends a forensic dump
  (breadcrumbs + last 32 client/server RPCs) to `build_pc/run/trap_dump.txt`.
  Never fired in clean play. See HANDOFF item #2.
- `local_player_id` re-resolve (`game_state.h`, `main.c`, `client_interface.c`)
  — added chasing the dict-dangling theory that was then DISPROVEN. Harmless but
  NOT a real fix; remove with the rest.

Vendored / required, now committed in PR #1 (upstream ships these dirs empty):
libs in `source/{cNBT,lodepng,parson,cglm}/` + `include/m-lib/`;
`assets/*.shader` (PC build loads shaders from the **texturepack dir**, not
cwd); `gen_world.py`. Build dirs, `*.dol`/`*.elf`, and the local `HANDOFF.md`
roadmap are gitignored.

## Don'ts (each cost real debugging time)

- Don't trust `assert` as a guard here — both Makefile (`-DNDEBUG`) and
  Release CMake compile it out; upstream asserts malloc/file-read successes.
  PC dev builds must be `-DCMAKE_BUILD_TYPE=Debug`.
- Don't put the SD image anywhere but `~/.local/share/dolphin-emu/Load/WiiSD.raw`
  (`Wii/` is the NAND; Dolphin silently auto-creates a blank card at the real
  path and your populated one is ignored).
- Don't bind Dolphin input to device `Keyboard Mouse` — the real XInput2
  device is `Virtual core pointer`. Key names are keysyms (`w`, `space`,
  `Escape`); extension bindings go inside `[Wiimote1]` as `Nunchuk/...`;
  `RelativeMouse` never fires under XWayland (absolute `Cursor` works);
  MotionPlus off = `Extension/Attach MotionPlus = False`.
- Don't use `pkill -f`/`pgrep -x dolphin-emu-nogui` (see wii-example-game
  CLAUDE.md: 15-char comm truncation + self-match).
- Don't commit screenshots / proof-of-play PNGs to the repo — host them as GitHub
  **release assets** (the `verification` prerelease) and embed the asset URL in the
  PR (`gh release upload verification issue-<N>-01.png --clobber`). Committed images
  bloat git history forever; we had to `git filter-repo`-purge a `verification/` dir
  and force-push `master` on 2026-06-15. See `write-bulletproof-issue`.

## Git remote

**Only `origin`** — `git@github.com:amahpour/CavEX.git`. Do not add
`xtreme8000/CavEX` as a remote; this fork is the canonical project.

## Optional upstream features (INTENT ONLY — do not act without explicit ask)

A Cursor session (2026-06-11, in agentsview) surveyed other CavEX forks; the two
that matter: **markkampstra/CavEX** (~40 ahead — entities/mobs, death screen,
dev console `tp/time/spawn/kill`) and **yyy257/fCavEX** (~238 ahead —
health/food/doors/signs/chests; incompatible save extensions). If ever wanted:
add `mark`/`fcavex` remotes temporarily, merge/cherry-pick, then remove. Expected
conflicts: `screen_ingame.c` (our debug HUD vs mark's hearts/death screen);
our `chunk_mesher.c` fix merges cleanly into both. No fork has creative mode.
