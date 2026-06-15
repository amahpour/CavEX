---
name: overnight-run
description: Ship a queue of `agent-ready` CavEX issues overnight as independent CI-green PRs you review one-at-a-time in the morning. The model orchestrates — spawns a fresh-context subagent per issue running `/work-on-an-issue`, verifies each PR is CI-green, never merges. Use to knock out a batch of surgical, well-scoped issues autonomously.
argument-hint: "<issue-number> [<issue-number> ...] [<N>+<M> ...]"
---

# Overnight run

## Purpose

Turn a queue of bullet-proof issues into independent PRs while you sleep, each
**CI-green** and reviewable in isolation in the morning. The model is an
**orchestrator**: it does ZERO implementation itself — it spawns one
fresh-context subagent per queue item (each runs the full `/work-on-an-issue`
gate), verifies the result, and moves on.

Why subagents: each runs in its own context, so quality doesn't degrade across
issues. The orchestrator conversation holds only its own log + each subagent's
summary. (1M-context Opus can also run **inline** — do `/work-on-an-issue`
directly, one issue after another — when subagent spawning is unavailable.)

Repo: `amahpour/CavEX` · base **`master`**.

---

## Arguments

Space-separated issue numbers. Use `+` to bundle issues into ONE PR — only when
they touch the **same** file/surface; cap a bundle at **2**.

```
/overnight-run 9 11 27 33+34
```

Queue sizing (CavEX CI is fast, ~2 min, so CI wait isn't the bottleneck — issue
size is): **5–8 items** is the comfortable range for a single overnight window.

---

## Step 0 — Pre-run planning (WITH the user, before they sleep)

The only phase where user input is allowed. For each issue:

1. **Validate** via `mcp__github__issue_read`: it's `agent-ready`, not blocked by
   an open issue outside the queue, and genuinely bullet-proof (file map +
   verify recipe present). Flag any that fail — propose deferring or fixing with
   `write-bulletproof-issue` first. A vague issue WILL waste its slot.
2. **File-collision matrix.** Pull each issue's **`Where (surface)`** section and
   build file → issues. Any file hit by 2+ issues is a collision: bundle them
   (same surface), or order them adjacent with the wider one later, or defer one.
3. **Order** so the **widest-footprint issue runs LAST** — it rebases on top of
   clean `master` in the morning after the smaller PRs merge.
4. **Budget** each item (implementation + `make test` + `/code-review` +
   autoshot + CI wait). Sum to the agreed window; if it overflows, **trim items**,
   don't shrink budgets (a blown budget wastes a slot on a draft PR).
5. **Present** the ordered queue + budgets + total, call out unavoidable
   collisions, and wait for approval before kicking off.

**Prereqs:** `agent-ready` label exists; for the smooth CI wait, `gh auth login`
on the box (else subagents fall back to MCP status polling — still gated, just
chattier).

---

## Step 1 — Generate the orchestrator prompt

Paste the filled template below as the first message of a **fresh** conversation
(keeps orchestrator context minimal). Keep the rules verbatim.

````
You are an overnight orchestrator for CavEX GitHub issues (repo amahpour/CavEX,
base master). DO NOT read issues, read source, or implement anything yourself.
Your ONLY job: spawn {{N}} sequential subagents (subagent_type: general-purpose),
one per queue item, wait for each, verify + log it, then start the next.

## Rules every subagent prompt MUST carry

1. Invoke /work-on-an-issue with the issue number(s). It runs the full gate:
   readiness → branch → implement → PC Debug build (+ .dol if PLATFORM_WII) →
   make test → autoshot/Dolphin visual proof if user-visible → /code-review →
   commit → push → PR (Closes #N) → **CI green** → issue comment. Ends at an
   open, CI-green PR. NEVER merges.

2. CI green is the ship gate, not local make test. Wait for the PR's "Unit tests"
   check with `gh pr checks <PR> --watch --fail-fast` (run_in_background: true so
   the wait is detached — no foreground sleep, no Monitor/SendMessage). If gh is
   unauthed, poll mcp__github__pull_request_read across turns. Red CI → fix on
   the same branch, re-push, re-wait. Do not report SHIPPED on red or pending CI.

3. Surgical scope — the PR closes its issue(s) and nothing else. No drive-by
   refactors. Tight diffs rebase cleanly at morning merge.

4. NO user input. On an ambiguous issue, make the most conservative choice,
   proceed, and record the assumption in the issue comment for morning audit.

5. Budget: see per-item below. If exceeded, stop, push partial work as a **draft**
   PR, and return a failure summary — never delete the branch.

6. Pause-free: the only wait is the backgrounded `gh pr checks --watch` (or MCP
   poll). Keep the subagent context alive end-to-end; return ONE final summary.

7. Any background or recovery spawn that touches git MUST use
   isolation: "worktree" — a shared working dir will `git checkout`/`stash` the
   orchestrator's tree and make untracked autoshots/files vanish.

8. The return summary MUST include: issue #, PR URL, branch, **CI conclusion**
   (success/failure — the receipt), and any assumption made. Prose like
   "verified, shipped" without a green CI conclusion is treated as NOT shipped.

## Failure handling
Can't finish (CI red it can't fix, /code-review blocker, budget exceeded, bad
repo state) → return a structured failure (issue #, which step, branch/PR URL,
1–2 line root cause, state left = draft PR). Log it, do NOT retry, move on.
{{COLLISION_NOTE}}

## Between items (housekeeping)
After a subagent returns:
1. Log its summary verbatim.
2. **Verify CI is actually green** — don't trust the self-report. `gh pr checks
   <PR>` (one-shot) or `mcp__github__pull_request_read` must show the Unit tests
   check concluded success. If it's red/empty, the item is NOT shipped — log it
   failed and move on (or spawn a worktree-isolated recovery agent).
3. `git checkout master && git pull --prune` in the repo root; confirm clean.
4. Spawn the next subagent.
Do NOT run make test or read source yourself between items.

## Queue — strict order ({{LAST}} last = widest footprint)
{{QUEUE_ITEMS}}

## Morning summary
Two tables in one final message:

### Shipped (CI-green, ready to merge)
| Issue | PR | Branch | CI | Time |
|---|---|---|---|---|

### Failed (needs attention)
| Issue | Died on | Branch/PR | Root cause |
|---|---|---|---|

Include the empty Failed table even if all shipped. Then restate any collision
files for the morning rebaser.

## Start now
Begin with item 1. Do not ask for confirmation — I'm asleep.
````

### Queue item shape

```
N. **#<num>** (solo | bundled #<a>+#<b> | LAST)
   - <one-line scope: what files, what change — lifted from the issue's Where + sketch>
   - <budget> min.
```

---

## Step 2 — Kickoff

Paste the generated prompt as the first message of a fresh conversation titled
`overnight-run`. Do NOT run it in the planning conversation — a fresh context
keeps the orchestrator lean.

---

## Anti-patterns

| Anti-pattern | Why |
|---|---|
| Orchestrator implementing anything itself | Defeats context isolation; one giant context degrades across items. Spawn even for trivial items. |
| Queuing a vague / not-`agent-ready` issue | The subagent burns its budget on discovery, then guesses. Make it bullet-proof first. |
| Trusting a subagent's "shipped" without checking CI | A self-report is not a green check. Verify the Unit tests check concluded success (housekeeping step 2). |
| Bundling unrelated issues | Breaks one-PR-per-issue morning review. Bundle only same-surface, cap 2. |
| Shrinking budgets to cram more items | Blown budgets force draft PRs that waste slots. Trim items instead. |
| Background/recovery spawn without `isolation: "worktree"` | It stashes/checks-out the shared tree; untracked autoshots disappear. |
| Letting subagents retry on failure | Retries burn the window silently. Fail fast, push draft, move on. |
| Merging overnight | You review each PR in the morning. End at open + CI-green. |

---

## Integration

- **`work-on-an-issue`** — each item runs this end-to-end; the per-issue gate.
- **`write-bulletproof-issue`** — the queue is only as good as the issues; vet in Step 0.
- **`/code-review`** — the review gate inside each subagent's run.
