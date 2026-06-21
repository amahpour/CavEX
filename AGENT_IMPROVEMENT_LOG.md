# CavEX AI Player — improvement log

> The honest record of the "train the AI to play like a human" effort
> (autonomous session started 2026-06-19). Read this first.

## What "training" means here (read this)

There is **no neural-network/weight training** in this project, and there is no
way to do gradient-based RL on CavEX from here — CavEX is a C game, and "Claude
plays it" means an LLM (or a deterministic policy) reads an exported game state
each tick and writes an action line through the **live input path**
(`CAVEX_AGENT` / `CAVEX_AGENT_GATED`, issues #67/#83). The model's weights are
fixed.

So the leverage — the thing that actually makes the agent better at "build me a
boat / build X like a human" — is **everything around the model**:

1. **Perception** — how rich and legible the exported state is (pos, orient, the
   aimed block, heightmap, inventory, `riding`).
2. **Skills** — a closed-loop library that turns single-tick button/LOOK lines
   into *confirmed* human actions (aim, mine, place, navigate, build).
3. **Planner** — turning an English goal into an ordered skill plan.
4. **The eval-driven improvement loop** — run an objective task battery, find what
   fails or looks non-human, fix the skills/perception/planner, re-run. Adversarial
   **subagents** act as independent critics ("refute that this works / find why it
   failed"). This loop is the legitimate analog of "training": a reward signal
   (task pass + human-likeness critique) drives iterative improvement of the stack,
   round after round.

This file logs that loop. Each round: scorecard → diagnosis (often by an
adversarial subagent) → fix → re-measure.

## Architecture

```
 English goal ──► Planner ──► [skill, skill, ...] ──► Executor
                  (template | LLM seam)                  │
                                                         ▼
 exported state ◄──────── CavEX (live, gated) ◄──── action lines
   (perception)                                    (one per tick)
        │                                                ▲
        └──────────► Skills (closed-loop) ───────────────┘
                     aim/mine/place/goto/build  (confirm via state)

 Eval battery (objective, isolated worlds) ──► scorecard
        │                                          │
        └──► adversarial subagent review ◄─────────┘
                     │
                     ▼  proposed fix → apply → re-measure
```

Files: `scripts/agent_skills.py` (skills), `scripts/agent_eval.py` (task
battery), `scripts/agent_planner.py` (NL goal → plan → verified execution).
Foundation proof: `agent_skills.py --selftest` mines + places one real block.

## Capability frontier (as characterized)

| Skill | Status |
|---|---|
| Navigate to a column (`goto`) | **solid** |
| Turn / aim at a block (`turn_to`/`aim_at`) | **solid** (calibrated LOOK gain; spherical pitch) |
| Mine a block (survival, wall-clock dig) | **solid** |
| Place one block | **solid** |
| Horizontal multi-block build (floor 2×2) | **solid** (4/4) |
| Vertical stacking (pillar) | **hard** (~1/3) — support top nears eye level → shallow look-down → side-face place. Needs pillar-jump. |
| Dig straight down | **hard** — straight-down aim needs verifying under the correct pitch convention. |
| Board a boat | **blocked on perception** — no nearby-entity export, so the agent can't locate a boat. Candidate: add boats/mobs to the state export. |

## Rounds

### Baseline (seed 42, 9 tasks) — mean score 0.926, 8/9 pass

| task | pass | score | note |
|---|---|---|---|
| walk_to | ✅ | 1.00 | final dist 0.75 |
| face_east | ✅ | 1.00 | yaw err 0.001 |
| aim_block | ✅ | 1.00 | crosshair on target |
| mine_one | ✅ | 1.00 | survival hand-dig |
| place_one | ✅ | 1.00 | |
| **stack_3** | ❌ | **0.33** | only 1/3 of a vertical pillar places |
| floor_2x2 | ✅ | 1.00 | horizontal build |
| wall_ring_3x3 | ✅ | 1.00 | 8/8 — a real hollow structure |
| dig_down_2 | ✅ | 1.00 | straight-down aim+mine works |

Key reads: structures (wall ring) build reliably; straight-down aiming works
(dig_down passes), so **pillar-jump is feasible**. The lone gap is building
*upward*. Round 1 targets `stack_3`.

### Round 1 — vertical stacking (`stack_3`) + a calibration-robustness bug

**Adversarial subagent review.** A subagent was given the live-interface facts,
the symptom (`stack_3` 1/3), and the code, and told to independently diagnose +
propose a fix (pure code analysis, no game). Its diagnosis went deeper than the
first hypothesis and was code-verified:

> The skill `_aim_is` confirms only the target **cell** (x,y,z) and is **blind to
> which face** the ray hits. `place_world`'s body-overlap back-off shoves the
> player ~1 block north after the base course, so from there the reach-ray enters
> each support's **vertical side face** — the server places at hit-cell +
> `blocks_side_offset(NORTH)` = (0,0,−1), i.e. the block lands *beside* the
> support at its own level, not on top. Hence only the first (top-face) block
> lands → 1/3. Engine refs: `server_local.c:269-314`, `blocks_util.c:48-84`,
> `screen_ingame.c:169-182`. The "shallow look" hypothesis was right in mechanism
> but mislocated in cause.

**Fix: pillar-jump.** Building *upward* by standing-and-placing is unreliable
(side-face hits). `pillar_up(n)` instead stands ON the column, aims **straight
down** at the block under the feet (always a TOP-face hit — proven by `dig_down`),
JUMPs once, and PLACEs into the gap below during the airborne window (feet clear
+0.55; jump arc gives a ~7-tick window), confirming each course by the feet rising
exactly one block. One JUMP edge per course + on-ground cooldown so two jumps
never trip the ~10-tick double-tap creative-flight toggle.

**Bonus bug (found by observing a planner run): calibration could silently fail.**
A single LOOK probe occasionally reads ~0 (frame-boundary export lag), which left
the bogus placeholder `gain_pitch=250.0` → pitch control ~40× too weak → all
aim/build silently breaks on that run. `calibrate()` now takes the **median of
several probes per axis** and falls back to the **known −2.0 constant** (never a
placeholder) when a reading is implausible.

**Result:** `stack_3` **1/3 → 3/3** (3 courses, feet +3, score 1.00) — vertical
building fixed. Also confirmed the world-reuse speedup: the task ran in **4.7s**
vs **67s** before (terrain is generated once per seed and copied in). Full-battery
re-confirm follows once the in-flight planner run frees the game.

**Full-battery re-confirm:** `stack_3` **3/3** in the battery; battery **8/9**,
mean **0.926 → 0.944**. World-reuse cut battery wall time from ~11 min to ~80 s
(per-task 64–67 s → 2–14 s).

**New weakness surfaced (the loop working):** `dig_down_2` went 2/2 → **1/2**, and
the dig code was untouched — so it's **flaky**, not a regression. Confirmed over 3
repeats: **PASS / FAIL / PASS** (2/3). Round 2 targets it.

### Round 2 — `dig_down` reliability (flaky 2/3)

Hypothesis: digging straight down, the player breaks the block, **falls**, and the
next `mine_block` starts before the player has settled — while falling, the aimed
cell below keeps changing, which **resets dig progress** (CavEX resets the dig
whenever `camera_hit` moves), so the second dig intermittently times out. Fix
direction: after each break, wait for `on_ground` + a stable aim before the next
dig, and retry once. (Other backlog: export `aim.side` to gate top-face placement;
build-efficiency for multi-block houses; boats.)

**Result:** `dig_down` hardened — wait for `on_ground` before each dig + retry
once. **6/6** over repeats (was 2/3). Verified adversarially by repetition rather
than a single run, since the bug was intermittent.

### Round 3 — build efficiency (multi-block builds were slow)

**Adversarial subagent review** (pure code analysis) found the cost: a "build a
3x3 house" took ~33,000 ticks via a **C→A pathology** — `goto` spins the full
`max_ticks=300` on an enclosed wall stance, then `aim_at`'s **7×5=35-iter** refine
grid burns up to ~770 more ticks failing to clear an occluded support, *per hard
cell*. Easy cells never hit either (they exit at the first-try aim).

**Fix (zero-risk minimal patch — only shortens failure paths):**
- `aim_at` refine: 7×5 grid → 3×3 cross, inner `turn_to` 20→8 ticks, skip the
  redundant (0,0), and a 60-tick budget early-out.
- `goto`: `max_ticks` 300→80, and bail in ~8 ticks when genuinely wedged (no
  positional progress) instead of spinning — a progressing open walk never bails.

**Result:** battery still **9/9, mean 1.000** (no regression); `floor_2x2`
14.2→**9.8 s**, `wall_ring_3x3` 25.5→**17.3 s** (~30% on open builds; the enclosed
house regime, which hit the full pathology, gains far more).

**House validation (and a bug it surfaced):** end-to-end `build a 3x3 house` via
the planner exposed a real bug — `build_walls` recomputed its base `y0` from
`top_solid_y(0,0)` *after* the floor was placed, so the contaminated post-floor
position gave the wrong wall height → 0/7 walls. Fixed by threading an explicit
`y0=base+1` from the planner. Now: **floor 9/9, walls 5/7** (14/16 blocks — a real,
mostly-complete house). The last 2 cells are elevated + enclosed wall stances —
the build frontier that FIX 2/4 (skip-redundant-goto + outside stance) or
pillar-jump-style wall courses would close. Tracked in the backlog.

### Round 4 — boats ("create a boat", the original ask) ✅

The headline goal. CavEX boats are an **item** (`ITEM_BOAT` 333; craftable from 5
planks) that spawns a rideable boat **entity** when placed; boarding sets the
`riding` flag. Crafting needs a grid UI that isn't in the action vocab, so the
pragmatic path: give the world a boat item (`gen_world.py` slot 6) and have the
agent **place it and board the boat it just placed** — it knows the cell, so the
missing nearby-entity export is irrelevant. `make_boat()`: select boat → aim at
the ground → PLACE (confirmed by the item being **consumed** → the entity spawned)
→ walk to the cell → use (confirmed by `riding`).

**Result:** works end-to-end headless — boards on the first use (`riding: True`),
reliable across repeats (**6/6** without frame capture). Battery now **10/10, mean
1.000** (the boat-item addition didn't regress the other 9). Wired as the
`make_boat` eval task + a planner template, so `agent_planner.py --goal "create a
boat"` does it.

> Caveat worth remembering: heavy `--autoshot` PNG dumps perturb the *render-time*
> aim raycast and can fail an otherwise-passing captured run — so the headless
> battery (no autoshot) is the verification, not a screenshot/GIF. This is the
> same hazard as [[overnight-pr-proof-of-play-images]] (proof tooling must not
> change the path it's proving).

### Round 5 — watchable live play + proof GIFs

Made the agent **watchable in real time** and produced GIF proof:
- `scripts/agent_live.py` + `GameSession(visible=True)` open a real window and
  drive the agent ONE skill at a time over a command pipe — the brain is the LLM
  in the harness (or `llm_complete`→Anthropic API), **not** a regex planner; the
  skills are the toolbox. The world idles a neutral tick between commands so the
  window animates instead of freezing.
- **Bug found by watching it live:** `goto`'s recovery-jumps could double-tap into
  creative flight on rough terrain and send the walker airborne — the flat-ground
  `walk_to` eval never saw it. Fixed with a 13-tick jump cooldown (> the ~10-tick
  double-tap window). Confirmed: a 4-leg walk now reaches 4/4, `flying=False`.
- **autoshot** (dev-rig) dumped a PNG *every tick* in agent mode — heavy per-tick
  I/O. Now dumps every Nth tick (`main.c`). Builds + navigation capture cleanly;
  the boat's *boarding* is still too autoshot-sensitive to GIF (the framebuffer
  read perturbs the render-time aim), so the headless **10/10** battery stays the
  verification for it. (Same hazard family as [[overnight-pr-proof-of-play-images]].)
- Proof on **PR #100**: a walled-hut build (with a step-back reveal) and a 4-leg
  navigation walk, both captured through the live input path.

### Backlog (future rounds)

- **Export `aim.side`** (engine has `camera_hit.side`) → gate placement on a
  confirmed TOP-face hit; retro-hardens horizontal builds + finishes enclosed
  wall cells (the house's last 2).
- **Taller walls / a proper roofed house** — `build_walls` height>1 needs
  pillar-jump-style courses (vertical `place_world` hits the side face).
- **Wire `llm_complete`** to the Anthropic API so fully open-ended goals plan
  themselves (the seam exists; falls back to the template planner offline).

