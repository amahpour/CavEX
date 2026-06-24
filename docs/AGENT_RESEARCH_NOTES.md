# What other Minecraft-AI projects do that we don't — research notes

> Synthesis of a 4-agent GitHub/web survey (2026-06-21) comparing the CavEX AI
> player against the wider "LLM/AI plays Minecraft" landscape. Goal: find the
> highest-leverage ideas we're missing. Written during a shell outage; commit
> alongside the round-8 navigation work.

## TL;DR — the ranked gaps

1. **Real navigation = A\* over affordance-weighted edges** (THE blocker; round 8 in flight).
2. **Self-writing + self-verifying skill library** — and our harness can be the code-writer with **no in-game API**.
3. **A planner with goal decomposition** (GOAP / task-tree) instead of our template seam.
4. **A reactive "modes" reflex layer** beneath the planner (safety, unstuck).
5. **Cross-episode memory** (we start every run cold).
6. **Declarative goal types** + structured failure reasons.

## The landscape (who we compared against)

- **mindcraft** (mindcraft-bots/mindcraft) — JS, built on **mineflayer**; LLM emits
  `!command(args)` text, mineflayer executes to completion. Multi-agent, vision,
  `!newAction` code-synthesis. *Libraries don't transfer (Java/Node); algorithms do.*
- **Voyager** (MineDojo/Voyager) + lineage **JARVIS-1, Optimus-1, ODYSSEY, GITM,
  DEPS, Plan4MC, STEVE-1, MineDojo** — the academic skill-library/curriculum line.
  Voyager is the gold standard and, crucially, **uses a symbolic text state much
  like ours** (so it transfers well).
- **Navigation refs**: **mineflayer-pathfinder** (A* + Movements cost model),
  **Baritone** (segmented A* under partial observability — the closest analog to
  our 5×5-window limit).
- **Long tail**: Altera **Project Sid** (PIANO concurrent-cognition), **MineLand**
  (bounded senses as a design axiom), **Altoclef** (autonomous full-game via a
  recursive task-tree), **GPGOAP** (plain-C GOAP — portable), **JPS-3D** (C++ voxel
  pathfinding), a dozen **MCP-Minecraft servers** (query-vs-act tool split), and
  benchmarks **MineRL/MineDojo/Craftium/MineBench** for eval task specs.

## 1. Navigation — A* over affordance-weighted edges  (round 8, in progress)

Every serious bot solves movement with **A\* over a graph whose edges encode
affordances**, not a greedy heading-walk. Our old `goto` collapses the Y axis →
can't climb a terrace or route around a tree → the navigation-over-distance
blocker. The fix (being implemented in `scripts/nav.py` + a `goto` rewrite):

- **Node = stance `(x, z, y_feet)`** — hashing on Y is what makes step-up/drop
  distinct nodes (greedy can't).
- **Edges, costs ported from Baritone/mineflayer intent** (walk ≈ 4.6, +1 step
  +1.4, drop ≤ `maxDropDown`, **dig penalised ~8, place ~25** → non-destructive-
  first, conserve inventory falls out of the numbers):
  walk (Δy 0) · step-up (Δy +1 → JUMP+FORWARD) · drop (−1..−N) · dig-through
  (gated) · bridge/pillar (gated).
- **Heuristic = octile XZ + |Δy|** (admissible → optimal).
- **Partial observability** (our 5×5 window == Baritone's unloaded-chunk horizon):
  plan within what's visible → walk a few waypoints → re-sense → re-plan
  (receding horizon), best-so-far toward out-of-window goals.
- **Executors reuse existing skills**: `aim_at` for heading, `mine_block` /
  `place_world` / `pillar_up` for the destructive edges.
- **Stuck-recovery escalation**: replan → nudge/sidestep → (gated) dig → bail,
  vs the old single 8-tick give-up.
- **Recommended engine tweak** (dev-rig, gated behind `CAVEX_AGENT`): widen the
  exported heightmap window 5×5 → **15×15** so A* can commit a ~6-block segment
  per re-sense. Single highest-leverage change after the planner itself.
- **New eval tasks**: `reach_far_flat`, `reach_terraced` (greedy fails today →
  the proof metric), `reach_forested`, `reach_walled_dig`.

Refs: mineflayer-pathfinder `lib/{astar,movements,goals}.js`; Baritone
`ActionCosts.java`, `AbstractNodeCostSearch.java` (COEFFICIENTS best-so-far,
`MIN_DIST_PATH`, chunk-border cutoff); JPS-3D (KumarRobotics/jps3d, C++, portable).

## 2. Self-writing + self-verifying skill library — and we need NO in-game API

Voyager's defining loop: the LLM **writes a new skill as code → a critic verifies
it succeeded → only then store it, indexed by an embedding of its description →
retrieve top-k relevant skills on future tasks.** mindcraft has the write-and-run
half (`!newAction`: generate → ESLint + symbol-lint → SES sandbox → run → retry
×5) but **doesn't persist** the result.

**The unlock for us (both research agents converged on this):** the Claude Code
harness *is* Voyager's GPT-4, **offline**. It reads our `run.jsonl`, **writes a new
Python skill** against our closed-loop primitives, and the **existing eval battery
/ a scratch-world run is the critic** — pass the state-confirmed check ⇒ commit the
skill + a one-line description; fail ⇒ the run log + failed assertion is the
"critique" fed back for a rewrite. This closes three gaps at once (auto-skills,
curriculum, memory) **with no Anthropic API wired into the game** (honours the hard
rule), and — unlike mindcraft — we can actually *persist* the skill. Retrieval can
start as keyword match (we have ~12 skills); add local `paraphrase-MiniLM`
embeddings past ~30.

## 3. A planner with goal decomposition (GOAP / task-tree)

Our planner is template-only. The lineage turns an open-ended goal into **ordered,
self-healing sub-goals**:

- **GOAP** (Goal-Oriented Action Planning) = A* over *actions* with
  preconditions/effects. "make a boat" auto-expands → need planks → need wood →
  mine tree, and re-plans when a step fails. **GPGOAP is plain C → portable into
  CavEX** under our "algorithms transfer" rule. Strongest single planner upgrade.
- **Altoclef** proves the pattern (first bot to beat Minecraft autonomously via a
  recursive task/resource tree).
- **DEPS "Select"**: rank parallel sub-goals by estimated steps-to-completion (do
  the nearest-feasible first) — directly useful for decomposing a long `goto` into
  ordered, individually-confirmed waypoints.
- Beta 1.7.3's tech tree is tiny vs modern MC → a hand-encoded
  recipe/skill-dependency graph is hours, not a research project.

## 4. A reactive "modes" reflex layer beneath the planner

mindcraft runs reflex sub-policies in a 300 ms loop that **pre-empt the LLM**:
`self_preservation` (lava/water/fall/low-HP), `unstuck` (no progress 20 s → flee →
restart), `item_collecting`, `torch_placing`. We have no autonomous safety/
reactivity layer — everything is in the deliberate skill path. A small per-tick
reflex layer in the Python driver (fall/void/lava avoidance, stuck→sidestep,
re-aim on lagged aim) is cheap and decouples safety from the planner.

## 5. Cross-episode memory (we currently start cold)

Three memory types recur — **skill/plan** (Voyager, GITM), **episodic+reflection**
(Optimus-1 AMEP, JARVIS-1), **static world-knowledge** (Optimus-1 HDKG, ODYSSEY
Wiki). MemEngine offers a clean taxonomy (working/episodic/semantic/procedural).
Minimal adoptable version: a `memory/skill_episodes.jsonl` of
`{task, start_state_digest, plan, outcome, critique}` the harness greps by task +
coarse state digest before planning. Our fixed `--seed` makes episodic memory
*more* reliable here than in stochastic Minecraft. Dovetails with our MEMORY.md.

## 6. Smaller wins

- **Declarative goal types** (Baritone `GoalNear`/`GoalXZ`/`GoalGetToBlock`) — make
  nav requests composable objects the planner solves, instead of imperative
  `goto(x,z)`. Pairs with GOAP ("be near block B" = a precondition).
- **MCP-style pull perception + structured failure reasons** (haksnbot/yuniko tool
  surfaces split *query* vs *act* and return *why* an act was illegal). We push one
  fat state blob/tick and skills can no-op silently; targeted queries + explicit
  MINE/PLACE failure reasons would cut wasted ticks.
- **Blueprint diff-builder** (mindcraft `npc/build_goal.js`, MineBench
  `voxel.exec(block,box,line)`) — generalise `build_floor`/`build_walls` into one
  "blueprint → diff current vs desired → place/break to satisfy, report missing".
- **Borrow benchmark task specs** (MineDojo/Craftium machine-checkable goals;
  MineBench Glicko head-to-head) to grow our eval battery past 9/9.

## What does NOT transfer

mineflayer / -pathfinder / collectblock / pvp / prismarine-viewer (Java-protocol,
full client sim); native tool-calling (mindcraft doesn't even use it — the text
`!command` protocol is the portable idea); multi-agent (CavEX is single-agent);
fine-tuned models / Wiki KBs (we can't train, and our world is generated "Claude
World", not Mojang assets — modern-Wiki facts are partly wrong for Beta 1.7.3).

## Suggested order of work

Round 8 **navigation** (in progress) → then the **harness-as-Voyager skill loop**
(highest leverage, no API, persists) → **GOAP/declarative goals** planner →
**reflex modes** + **episodic memory**. Each is an eval-driven round in the
existing improvement loop.

### Key links
mindcraft https://github.com/mindcraft-bots/mindcraft · Voyager
https://github.com/MineDojo/Voyager · mineflayer-pathfinder
https://github.com/PrismarineJS/mineflayer-pathfinder · Baritone
https://github.com/cabaletta/baritone · GPGOAP https://github.com/stolk/GPGOAP ·
JPS-3D https://github.com/KumarRobotics/jps3d · Altoclef
https://github.com/gaucho-matrero/altoclef · Project Sid
https://github.com/altera-al/project-sid · MineLand
https://arxiv.org/html/2403.19267v1 · MemEngine https://arxiv.org/pdf/2505.02099 ·
yuniko MCP server https://github.com/yuniko-software/minecraft-mcp-server.
