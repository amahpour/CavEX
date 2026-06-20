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

