---
name: write-bulletproof-issue
description: Author a GitHub issue an autonomous (overnight) agent can implement end-to-end with no human in the loop — CavEX context, a file map, testable acceptance criteria, an exact build/test/autoshot verify recipe, and the `agent-ready` gate. Use when filing or upgrading an issue you intend an agent to pick up, or when making an `overnight-run` queue bullet-proof.
---

# Write a bullet-proof issue

## Why

An overnight agent has no one to ask. Every ambiguity becomes a guess, and one
wrong guess burns the whole item slot. A bullet-proof issue front-loads the
answers so the agent spends its budget *implementing*, not *discovering*. Bar to
clear: a competent contributor who has never opened this repo could ship it from
the issue text alone.

This is the foundation the rig stands on — `work-on-an-issue` and `overnight-run`
are only as good as the issues feeding them.

---

## The `agent-ready` gate

An issue is overnight-eligible only when ALL of these hold. If any fails, it's a
human-hours issue, not an autonomous one — leave it `triage needed`.

- [ ] Has all 9 sections below — no TBDs, no "figure out X"
- [ ] Scope is ONE surgical change (one fix/feature, not a project or an epic)
- [ ] The surface is enumerated — the agent shouldn't have to hunt for the files
- [ ] Acceptance criteria are concrete and checkable
- [ ] A verify recipe exists: exact build + `make test` + (if visible) an autoshot scenario
- [ ] Not blocked by an open issue outside the run
- [ ] Carries `priority/*` + `platform/*`; NOT `triage needed`
- [ ] Labeled `agent-ready`

`agent-ready` is the CavEX analog of "is it the agent's turn?" — the one signal
`overnight-run` filters on. One-time setup: create the label (ask me, or
`gh label create agent-ready -c 0E8A16 -d "Vetted for autonomous/overnight work"`
once `gh` is authed).

---

## Template

Paste this skeleton and fill every section. Keep it tight — bullets over prose.

```markdown
## Context
<1–3 sentences: the problem or gap, and why it matters. Cite a source if there
is one (a playtest note, a HANDOFF item #, an upstream behavior).>

## Where (surface)
<The files/functions/systems involved — real paths, e.g. `source/game/gui/
screen_ingame.c`. State the platform: PC, Wii, or both.>

## Implementation sketch
<2–6 bullets: the specific change, key functions, the mechanism. Concrete enough
that nothing is left to "decide" mid-run. If you'd have to read code to write
this, read it now — that discovery is what makes an issue NOT bullet-proof.>

## Acceptance criteria
- [ ] <testable outcome — a behavior, not a vibe>
- [ ] <testable outcome>

## How to verify
- Build: <PC Debug always; Wii `.dol` too if any PLATFORM_WII / GX / WPAD code>
- Test: `make test`  <!-- always; this is exactly what CI runs -->
- Visual (only if user-visible): <autoshot scenario + the frame that proves it>
- Wii-affecting: <Dolphin frame-dump check via run-wii-dolphin>

## Constraints / don't-break
<Wii MEM1 budget; never reintroduce -march=native; keep chunk_mesher static
buffers; MAX_VIEW_DISTANCE 3; PC/Wii saves are separate. Surgical scope — this
issue only, no "while I'm here" refactors.>

## Out of scope
<What NOT to touch, so the PR stays reviewable in one sitting.>

## Size & budget
<S / M / L, plus a rough minute budget for the overnight planner.>

## Dependencies
<Depends on #N / Blocks #M, or "none".>
```

---

## The two sections that make or break autonomy

**`Implementation sketch`** kills the discovery phase. "Fix the hotbar bug" forces
the agent to reverse-engineer the bug first (expensive, error-prone). "In
`hud_draw_hotbar()` the selection rect uses `slot` instead of `slot * SLOT_W` —
multiply" leaves nothing to guess. If you can't write the sketch without reading
code, that reading IS the work — do it before filing, not at 3am unattended.

**`How to verify`** is what the agent uses to *prove* it's done, and what you
eyeball in the morning. Make it executable:

- `make test` is the always-on check; the PR's **CI run is the ship gate** — the
  same `make test` on a clean ubuntu box (`.github/workflows/tests.yml`). Local
  green is the fast pre-check; **CI green is what ships it** (the clean room
  catches uncommitted files / path assumptions local builds miss). Pure-logic
  issues (NBT, lighting, mesher, util) need no screenshot, but still gate on CI.
- For anything **user-visible**, name the autoshot scenario and the expected
  frame, e.g. *"`vblank_mode=0 CAVEX_AUTOPLAY=1 CAVEX_AUTOSHOT=120 timeout 45
  ../cavex`; an `autoshot_*.png` shows the selection box centered on the held
  slot."* (See `dev-native-pc` for the rig.)
- For **Wii-platform** issues, the truth is the Dolphin frame dump (`run-wii-dolphin`).

### Attaching visual proof (GitHub reality, public repo)

`make test` output is text — paste it. Autoshot frames are PNGs, and embedding a
PNG in a PR is the one fiddly bit, so the issue's verify recipe should assume
this path:

> **NEVER commit screenshots to the repo.** Binary PNGs in `verification/` (or
> anywhere in the tree) bloat the repo and live in git history **forever**; purging
> them later needs a `git filter-repo` rewrite + force-push of `master` that churns
> every commit SHA. We had to do exactly that cleanup on **2026-06-15** — do not
> recreate the mess. Host frames **off-tree** and embed the URL.
>
> **There is no token-authenticated API to attach an image to a GitHub comment/PR**
> (the web drag-drop hits a cookie-gated `github.com/upload/...` endpoint that
> rejects PAT/OAuth/App tokens). So:
>
> 1. **Release assets — REQUIRED default.** Upload frames to a single reused
>    prerelease and embed the asset URL (token-based, headless-safe, **nothing in
>    the source tree or git history**):
>    ```bash
>    # one-time: gh release create verification --prerelease \
>    #   --title "Verification frames (PR proof-of-play)" --notes "Off-tree image host."
>    gh release upload verification issue-<N>-01-<what>.png --clobber
>    ```
>    `![](https://github.com/amahpour/CavEX/releases/download/verification/issue-<N>-01-<what>.png)`
>    Asset names must be **unique within the release** — always prefix with `issue-<N>-`.
> 2. **Native user-attachments CDN** (the real drag-drop URL): only via
>    browser-cookie helpers (`gh-attach`, `gh-image`). One-time browser login;
>    overkill for a public hobby fork.
>
> Known gap, not a local quirk: cli/cli#12960, cli/cli#13256,
> anthropics/claude-code#26831. **Default to release assets; never commit frames.**

---

## Labels & metadata (set on every agent-ready issue)

| Field | Values | Note |
|---|---|---|
| Priority | `priority/P1`–`P4` | Required; drives `overnight-run` ordering |
| Platform | `platform/pc`, `platform/wii` | Which build is "truth" for verify |
| Type | `bug`, `enhancement`, `tech-debt`, `fork-merge` | |
| Gate | `agent-ready` | The overnight filter. Remove `triage needed` first |

---

## Filled example (illustrative)

```markdown
**Title:** Center the hotbar selection box on the held slot

## Context
The selection box highlights the slot to the right of the one actually held —
off-by-one in the HUD draw. Cosmetic but constantly visible. (PC + Wii share the
HUD path, so one fix covers both.)

## Where (surface)
`source/game/gui/screen_ingame.c` — the hotbar HUD draw. PC + Wii (shared).

## Implementation sketch
- The selection-rect X uses the raw `slot` index instead of `slot * SLOT_W`.
- Multiply by the per-slot width used by the slot icons directly above.
- No state/logic change — draw-position only.

## Acceptance criteria
- [ ] Selection box sits exactly over the held slot (slots 0 and 8 included)
- [ ] No change to which item is actually selected / used

## How to verify
- Build: PC Debug (`build_pc`, `cmake .. -DCMAKE_BUILD_TYPE=Debug && make -j`)
- Test: `make test`
- Visual: `vblank_mode=0 CAVEX_AUTOPLAY=1 CAVEX_AUTOSHOT=120 timeout 45 ../cavex`;
  an `autoshot_*.png` shows the box centered on slot 0. Upload that frame to the
  `verification` prerelease (`gh release upload verification issue-<N>-01.png
  --clobber`) and embed its asset URL in the PR — never commit it to the repo.

## Constraints / don't-break
Draw-only; don't touch input or selection state. Surgical — this issue only.

## Out of scope
Hotbar scroll behavior (#9), item rendering.

## Size & budget
S — 30 min.

## Dependencies
None.
```

---

## File / update it (GitHub MCP)

`gh` is not authenticated in this env — use the GitHub MCP, repo `amahpour/CavEX`:

1. `mcp__github__issue_write` (`method: "create"`) with the filled body.
2. `mcp__github__issue_write` (`method: "update"`) to set `labels`
   (full set — the API replaces, so include all existing + new).
3. To upgrade an existing issue: `mcp__github__issue_read` (`get`) it first, then
   rewrite the body into the template and add `agent-ready`.

---

## Anti-patterns

| Anti-pattern | Why it breaks an overnight run |
|---|---|
| "Investigate and fix X" | Discovery isn't bullet-proof — the agent burns budget reverse-engineering. Do the investigation when filing. |
| No file map | The agent hunts for the surface; collisions can't be planned (`overnight-run` needs the file list). |
| Untestable AC ("make it feel better") | Nothing to gate on; nothing to verify in the morning. |
| Missing verify recipe | The agent can't prove it's done and you can't check it. `make test` at minimum. |
| Bundling several features in one issue | Defeats the one-PR-per-issue morning review. Split them. |
| Leaving `triage needed` | That label means un-prioritized — the inverse of `agent-ready`. Triage first. |
| Depending on an issue not in the run | The agent hits a wall mid-run. Resolve the dep or defer. |

---

## Integration

- **`work-on-an-issue`** — consumes a bullet-proof issue and runs the gate.
- **`overnight-run`** — queues `agent-ready` issues; needs the file map + budget.
- **`dev-native-pc`** — the autoshot/autoplay rig the verify recipe calls.
- **`run-wii-dolphin`** — the frame-dump verify path for Wii-platform issues.
