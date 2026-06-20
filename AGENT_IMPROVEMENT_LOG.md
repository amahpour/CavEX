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

**Follow-up (next round):** export `aim.side` (the engine already has
`camera_hit.side`) and gate placement on a confirmed TOP-face hit — retro-hardens
horizontal builds too.

