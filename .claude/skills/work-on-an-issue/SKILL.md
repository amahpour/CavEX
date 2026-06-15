---
name: work-on-an-issue
description: Take one `agent-ready` CavEX GitHub issue from branch to a CI-green PR with a gate at build, `make test`, `/code-review`, and (for visible changes) an autoshot/Dolphin visual check. Use when implementing a single issue end-to-end — interactively or as one item of an `overnight-run`. Ends at an open, CI-green PR for human review; never merges.
argument-hint: "<issue-number>"
---

# Work on an issue

## Purpose

The single per-issue contract: a bullet-proof issue (`write-bulletproof-issue`)
goes in, a CI-green PR comes out, gated so "done" is unambiguous — code committed,
build + `make test` + `/code-review` green locally, PR opened with `Closes #N`,
**CI green**, visual proof posted for anything user-visible. It stops at an open
PR; the human merges after morning review.

**Gate-driven.** No commit until build + `make test` + `/code-review` pass. Not
"shipped" until CI is green on the PR.

Repo: `amahpour/CavEX` · default branch **`master`** · `origin` = fork.

---

## Step 0 — Readiness gate

Fetch the issue and confirm it's actually `agent-ready`:

```
mcp__github__issue_read (method: get)         # body + labels
mcp__github__issue_read (method: get_comments) # comments OVERRIDE the body
```

- Has `agent-ready`, a `priority/*`, and is NOT `triage needed`.
- Read the comments — they frequently amend the description.
- The body has the 9 bullet-proof sections (esp. a file map + verify recipe).

If it's not bullet-proof: **interactive** → stop and ask the user to fill the
gaps. **Overnight (no user)** → make the most conservative interpretation,
proceed, and record the assumption verbatim in the issue comment (Step 9) so it
can be audited in the morning. Never silently guess on something load-bearing.

---

## Step 1 — Branch

```bash
cd ~/code/CavEX
git fetch origin && git checkout master && git pull --ff-only
git checkout -b issue-<N>-<short-slug>
```

---

## Step 2 — Implement (surgical)

Only the files the issue names. No "while I'm here" refactors — tight diffs
review one-at-a-time and rebase cleanly. Build PC as **Debug** so asserts stay
live (NDEBUG turns upstream's malloc/file-read asserts into NULL-deref crashes).
Honor the don't-break list: never `-march=native`; keep `chunk_mesher.c` static
buffers; `MAX_VIEW_DISTANCE 3`; PC/Wii saves are separate.

---

## Step 3 — Build gate

```bash
cd ~/code/CavEX/build_pc
cmake .. -DCMAKE_BUILD_TYPE=Debug      # only after CMakeLists changes
make -j"$(nproc)"
```

If the issue touches any `PLATFORM_WII` / GX / WPAD code, also cross-build the
`.dol` (devkitPro env + `make` from root) — PC-only green is not enough for a
platform change.

---

## Step 4 — Test gate (local pre-check)

```bash
make test          # from repo root: configures build_test/, ctest, coverage gate
```

Must be green before you push. This is the *same command* CI runs — but local
green is the pre-check, **CI green (Step 7) is the ship gate.** Don't push a
known-red branch to "let CI sort it out."

---

## Step 5 — Visual verify (only if user-visible)

Skip for pure-logic issues (NBT, lighting, mesher, util) — `make test` + CI
cover them. For anything that renders:

```bash
cd ~/code/CavEX/build_pc/run
vblank_mode=0 CAVEX_AUTOPLAY=1 CAVEX_AUTOSHOT=120 timeout 45 ../cavex
```

Read the `autoshot_*.png` and confirm each acceptance criterion. Keep the frames
that prove the fix (you'll embed them in the PR — see Step 6). For **Wii-platform**
issues, the truth is the Dolphin frame dump (`run-wii-dolphin`), not the PC autoshot.

> **Embedding the proof — NEVER commit images to the repo** (they bloat git
> history forever; we had to `filter-repo`-purge them on 2026-06-15). GitHub has
> no token API to attach images to a PR, so host frames as **release assets** and
> embed the asset URL — nothing lands in the source tree:
> `gh release upload verification issue-<N>-01.png --clobber` →
> `![](https://github.com/amahpour/CavEX/releases/download/verification/issue-<N>-01.png)`
> See `write-bulletproof-issue` for the one-time release setup.

---

## Step 6 — Review gate, commit, push, PR

1. **`/code-review`** on the diff. Fix every finding before committing — tests
   are necessary but not sufficient (this catches the alignment/race/closure
   class that `make test` won't).

2. **Commit** — stage specific files (never `git add .`), conventional style:

   ```bash
   git add source/path/file.c
   git commit -m "$(cat <<'EOF'
   fix(scope): imperative summary (#<N>)

   - what changed, briefly

   Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>
   EOF
   )"
   git push -u origin issue-<N>-<slug>
   ```

3. **Open the PR** with `mcp__github__create_pull_request` (base `master`, head
   the branch). Body — no stakeholder ceremony, just:

   ```markdown
   ## What & why
   <1–3 sentences: the problem and the fix. Link the issue context.>

   Closes #<N>

   ## How to verify
   - `make test` → <result>
   - <visible: the autoshot assertion + the embedded raw-URL frame>

   ## Notes
   <any conservative assumption made under Step 0, or "none">
   ```

---

## Step 7 — CI gate (required)

The PR must be **CI-green** before it counts as shipped. Wait for the
`Unit tests` workflow to conclude:

```bash
# Preferred — re-wakes the agent when checks finish; pass exit 0 / fail non-zero.
# run_in_background: true (detached, so the blocked wait respects the no-foreground-sleep rule)
gh pr checks <PR> --watch --fail-fast
```

Needs `gh auth login` / `GH_TOKEN` on the box. **Fallback if `gh` is unauthed:**
poll `mcp__github__pull_request_read` for the check-run status across a few turns
(CavEX CI is ~2 min). Either way:

- **Green** → proceed to Step 8.
- **Red** → fetch the failing log (`gh run view <run> --log-failed` or the run
  page), fix on the same branch, re-run Steps 3–4, push, re-wait. Don't
  amend/force-push unless asked.

---

## Step 8 — Hand off (don't merge)

1. Comment on the issue with: what was done, files touched, how to verify, the PR
   URL, and any assumption from Step 0. (`mcp__github__add_issue_comment`)
2. Leave the PR **open and unmerged** — the human merges after review. Merge
   auto-closes the issue via `Closes #N`; GitHub handles the rest (no separate
   close-out step beyond `git branch -d` after merge).

Final summary line: `#<N> → PR #<P> (<branch>) · CI green · <Nm>` plus any
assumption flagged.

---

## Anti-patterns

| Anti-pattern | Why |
|---|---|
| Commit before build + `make test` + `/code-review` all green | "Tests pass" has shipped alignment/race/closure bugs. The review is part of the gate. |
| Calling SHIPPED before CI is green | Local `make test` ≠ clean-room. CI is the gate — an uncommitted file or path assumption fails only there. |
| `git add .` | Stages secrets, build dirs, stray autoshots. Stage named files. |
| Release/NDEBUG PC build | Compiles out the asserts that guard malloc/file-read — failures become NULL-deref crashes. Always `-DCMAKE_BUILD_TYPE=Debug`. |
| Skipping the autoshot on a visible change | `make test` doesn't see the framebuffer. The frame is the only proof a render fix actually rendered. |
| "PC builds, ship it" for a Wii change | PC and Wii diverge exactly where it matters (MEM1, GX, WPAD). Cross-build the `.dol`. |
| Merging the PR | The human reviews each PR in the morning. Stop at open + CI-green. |
| Guessing on an ambiguous issue with no flag | Make the conservative choice AND record it in the issue comment so it's auditable. |

---

## Integration

- **`write-bulletproof-issue`** — the input contract; if the issue isn't ready, this skill can't run clean.
- **`overnight-run`** — runs this skill once per queue item, then verifies CI green before logging it shipped.
- **`dev-native-pc`** — the Debug build + autoshot rig (Steps 3, 5).
- **`run-wii-dolphin`** — the `.dol` build + frame-dump verify for Wii issues.
- **`/code-review`** — Step 6 gate.
