# CavEX AI Player — "ask Claude to build X"

An AI that plays CavEX **like a human**: it reads the game's exported state each
tick and drives the **live input path** (the same buttons/look a player uses), and
turns an English goal into actions it executes and verifies. No engine shortcuts.

> **On "training" (read this).** There is no neural-network/weight training here,
> and there's no way to do gradient RL on CavEX from this repo — it's a C game.
> "Claude plays it" = an LLM (or a deterministic policy) reads state and writes
> action lines; the model's weights are fixed. What actually makes the agent
> *better* is the stuff around the model: perception, a reliable skill library, a
> planner, and an **eval-driven improvement loop** with adversarial subagent
> critics. That loop is the honest analog of "training for hours" — see
> [`AGENT_IMPROVEMENT_LOG.md`](AGENT_IMPROVEMENT_LOG.md).

## Quick start

```bash
# build the PC binary once (Debug)
(cd build_pc && cmake .. -DCMAKE_BUILD_TYPE=Debug && make -j$(nproc))

# ask the agent to do something — it plans, acts, and verifies, on an ISOLATED
# scratch world (your real saves are never touched):
python3 scripts/agent_planner.py --goal "build a 3x3 house"   --autoshot 6
python3 scripts/agent_planner.py --goal "build a 5x4 floor"
python3 scripts/agent_planner.py --goal "walk to 40 90"
python3 scripts/agent_planner.py --goal "dig down 3"

# prove the muscle layer works end to end (mines + places a real block):
python3 scripts/agent_skills.py --selftest

# score the agent against the objective task battery:
python3 scripts/agent_eval.py --all --json report.json
```

## The pieces

| File | Role |
|---|---|
| `scripts/agent_skills.py` | **Skills** — the closed-loop "muscle memory". Turns single-tick button/LOOK lines into *confirmed* human actions: `aim_at`, `mine_block`, `place_block`, `goto`, `build_floor`, `build_walls`, `pillar_up`, `dig_down`. Every action reads state back to confirm it worked. |
| `scripts/agent_eval.py` | **Eval** — an objective task battery (`walk_to`, `mine_one`, `place_one`, `stack_3`, `floor_2x2`, `wall_ring_3x3`, `dig_down_2`, …), each in its own isolated world, machine-scored. |
| `scripts/agent_planner.py` | **Planner** — English goal → ordered skill plan → executed + verified. |
| `scripts/ai_playtest.py` | the original harness this builds on (#83): gated perceive→act + the gameplay scorer. |

How it fits together:

```
 English goal ─► Planner ─► [skill, skill, …] ─► Executor ─┐
                (template | LLM)                            │ action lines
 exported state ◄──────────── CavEX (live, gated) ◄─────────┘ (one per tick)
        └────────────► Skills (closed-loop, confirm via state)
```

## How "ask Claude to build X" works

Today there are **two planners** behind one interface:

- **TemplatePlanner** (default, offline, no API key): pattern-matches the common
  goals — `walk to X Z`, `dig down N`, `mine N`, `build NxM floor`,
  `build NxM house` — against the live spawn, and emits a concrete plan. This is
  what runs in CI and offline.
- **LLMPlanner** (the real Claude seam): given the goal, the live state, and a
  machine-readable description of the skills, an LLM emits the plan as JSON. To
  turn it on, wire one function:

  ```python
  # scripts/agent_planner.py
  import anthropic
  def llm_complete(prompt):
      msg = anthropic.Anthropic().messages.create(
          model="claude-opus-4-8", max_tokens=2000,
          messages=[{"role": "user", "content": prompt}])
      return msg.content[0].text
  planner = LLMPlanner(llm_complete)          # falls back to TemplatePlanner with no key
  ```

  With that, open-ended goals ("build a watchtower with a 2-wide door") plan
  themselves into skill calls. The executor still verifies every step against the
  game state, so a bad plan fails loudly instead of silently.

## What it can do (and can't, yet)

| Capability | Status |
|---|---|
| Navigate to a point | ✅ solid |
| Aim at / mine / place a specific block | ✅ solid |
| Build a floor / platform | ✅ solid |
| Build walls / a hollow structure (1 high) | ✅ solid (`wall_ring_3x3` 8/8) |
| Build **upward** (pillar) | ✅ `pillar_up` (jump-and-place) — fixed in round 1 |
| Dig straight down | ✅ solid |
| Build a house (floor + 1-high walls + door) | ⚠️ mostly — floor 9/9, walls 5/7 (the 2 elevated+enclosed wall cells are the frontier); ~3× faster after round 3 |
| Multi-course (tall) walls | ⛔ `build_walls` places courses with `place_world` (side-face fails above the first); needs pillar-jump-style courses |
| **Board / make a boat** | ⛔ needs a boat *item* in inventory + a place/board skill (the world has planks; crafting UI isn't in the action vocab). The agent can board a boat **it places** (it knows the cell) without any engine change. Planned. |
| Open-ended goals | ✅ once you wire `llm_complete` (above) |

## The improvement loop (the "training")

```bash
python3 scripts/agent_eval.py --all --json report.json   # measure
# inspect failing tasks, have a subagent diagnose, fix the skill, re-measure, repeat
```

Each round: run the battery → an **adversarial subagent** independently diagnoses
the weakest task (and tries to refute "it works") → apply the fix → re-measure →
commit. Round 1 (subagent-diagnosed) fixed vertical building (`stack_3` 1/3 → 3/3)
and a calibration-robustness bug. Full history: `AGENT_IMPROVEMENT_LOG.md`.

## Notes

- Runs **headless** (`vblank_mode=0`); every run uses an isolated scratch world via
  `agent_play_demo.make_run_dir` — the real "Claude World" save is never touched.
- The substrate this relies on (gated live input + state export + the #96 edge/LOOK
  fix) is described in `CLAUDE.md` and issues #67/#83/#96.
