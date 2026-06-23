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

### Round 6 — the "staring at the sky" bug (found by watching live) ✅

The user, watching a live build, called it before the data did: *"you keep going
around in circles and staring at the sky."* That weirdness was the reward signal.
Reading the run results frame-by-frame confirmed it: a `build_walls` aimed every
block at `y:70` while the player's feet were at `~67` — the crosshair literally
pointed into the sky → **0/35**; a `build_floor` after the avatar climbed a hill
to feet `~71` aimed at `y:70` buried in the slope.

**Root cause (one cause, both symptoms):** build height (`base` Y) was taken from
`top_solid_y(0,0)` — the topmost block in the player's **current column**,
wherever the previous command left them. On the flat eval worlds stance == site,
so rounds 1–5 never saw it. Live, on terraced forest terrain, the player wandered
onto a hill and every target Y floated above the real columns (sky-aim) or buried
below them. The planner's old "thread explicit y0" fix never reached the **live**
dispatch, which still recomputed `base` from the contaminated stance.

**Fix:** `GameSession.local_ground_y()` → median top-solid Y (+ flatness spread)
over the local heightmap, read **at the build site** (walk there first), so build
height tracks the ground you're building on, never a transient stance. The live
dispatch now does `goto(site center) → local_ground_y() → base` for floor/walls
(surveying walls *after* the floor self-corrects: it reads the floor as the new
ground). Plus a **sky-aim guard** in `place_block` that refuses (fast, logged)
when the support is >1.5 above the feet — the cheap signal to re-survey instead of
craning at the sky. `pillar_up` builds upward via its own path, so it's unaffected.

**Result (proven live on terraced terrain):** **0 sky-aim guard fires**, no more
craning at the sky; `build_walls` **17/19** (vs the old house's 5/7), all at the
correct surveyed height; built a 6×6 plank cabin (stone floor + door + two
glowstone interior lights) end-to-end through the fixed pipeline. Lesson banked as
the `cavex-build-site-survey` memory.

**New frontier surfaced (the loop continuing):** terraced terrain still leaves
*floor* gaps — a down-step column has no support block, so a single-level flat
slab can't complete there (correctly *skipped*, not sky-aimed). The next
iterations: a **terraform/`level_pad`** skill (mine highs / fill lows to one
plane before laying the floor) for clean big footprints, and a reliable
**build-upward** primitive (stand-on-structure or `aim.side`-gated top-face) so
walls go >1 high and roofs become possible.

### Round 7 — `level_pad` terraform + a creative-flight bug caught on camera

Started the terraform skill to unblock big builds on terraced ground. The loop
ran hot here — three iterations, and the decisive bug was found by *looking at a
frame*, exactly the Round-6 lesson again:

- **v1** (walk onto every column, `dig_down` the excess): **8/16**. Survey only
  reached 11/16 — standing *exactly* on each column is imprecise (goto drifts).
- **v2** (stand at tile centres, read the 5×5 heightmap, mine excess from the
  stance — also clears trees for free): **9/25**. Denser **overlapping** stances
  (every column within ±1 of a stance) lifted survey coverage but leveling was
  still ~10/16.
- **Root cause (a frame showed `FLYING` on the debug overlay):** level_pad fires
  dozens of back-to-back `goto`/`dig` calls, and the Round-5 jump cooldown was
  **per-goto-call** — two recovery-jumps astride a call boundary slipped inside
  CavEX's ~10-tick double-tap window and toggled **creative flight**. Airborne,
  every ground-relative read (feet column, heightmap) is meaningless, so survey
  and leveling silently produced garbage.
- **Fix:** a **session-global** last-jump tick (cooldown holds across all calls,
  not just within one goto) + `_unfly()` recovery (double-tap to toggle flight
  back off, confirmed via the `flying` flag) + `goto` refuses to navigate while
  airborne. This hardens *all* navigation, not just terraform.
- **Result:** survey **10–13/25 → 25/25**, leveled **9/25 → 19/25** (76% on a
  2-block-spread site), no flight; produces a visibly flat stone pad carved into
  the terraced hillside.

**Honest status:** the terraform works and is reliable enough to *flatten*, but
76% isn't flat enough to *chain* into a clean walled shell — the ~24% of columns
still a step high leave the capped foundation at spread 2, so `build_walls`'
sky-aim guard correctly refuses most of it (1/15). Closing that last quarter
(the +2 columns / corner reach, then re-survey the cap before walling) is the
next iteration. The flight fix and the survey-at-site fix both shipped.

### Backlog (future rounds)

- **`level_pad` completeness** — push leveling 76% → ~100% (mine the +2 columns
  reliably from a corner-adjacent stance; re-survey the cap before walling) so
  flatten→foundation→walls chains cleanly into a big house. Optionally a
  dedicated tree-clear pass before leveling.
- **Export `aim.side`** (engine has `camera_hit.side`) → gate placement on a
  confirmed TOP-face hit; retro-hardens horizontal builds + finishes enclosed
  wall cells (the house's last 2).
- **Taller walls / a proper roofed house** — `build_walls` height>1 needs
  pillar-jump-style courses (vertical `place_world` hits the side face).
- **Wire `llm_complete`** to the Anthropic API so fully open-ended goals plan
  themselves (the seam exists; falls back to the template planner offline).

