# Agent Execution Rules

Read this file before starting any operation in this repo.

This is the only Markdown file that should be read automatically for
every repo task. Do not read all repo docs on every prompt.

These are mandatory rules for any task that changes code, logic,
runtime behavior, data-loading behavior, exported binary shape, or
performance-sensitive paths.

## Documentation read rules

1. Always read `AGENT.md`.
- `AGENT.md` is the short always-on instruction file.
- It contains execution rules, design principles, repo boundaries, and
  required update behavior.

2. Do not read all docs by default.
- Do not scan every `.md` file on each prompt.
- Read other docs only when the user asks, the task requires that
  context, or the file is directly affected by the requested change.

3. Keep long-form repo context in the relevant docs.
- Use `README.md`, component READMEs, and focused files under `docs/`
  when architecture, repo identity, scope, subsystem boundaries, or
  long-form repo context is needed.
- Do not treat every doc as always-read context.
- Keep the relevant doc updated when architecture, scope, or repo
  boundaries change.

4. Keep planning docs lean.
- `docs/work.md` tracks active and future work only.
- `docs/work_highlevel.md` is a concise human-facing status map.
- `docs/changelog.md` holds completed implementation history.
- `docs/git_history.md` holds commit, branch, push, and rollback-point
  notes with extra context that may not fit in commit messages.
- Move finished work out of `docs/work.md` and into
  `docs/changelog.md`.

## Core implementation priorities

1. Performance and simplicity come first.
- Prefer the simplest correct design.
- Prefer direct code over clever code.
- Prefer fewer moving parts, fewer abstractions, and fewer layers.
- Avoid unnecessary indirection, wrappers, helpers, or generalized
  systems when a simple local solution is clearer and faster.

2. Keep code simple.
- Write code that is easy to read and easy to trace.
- Do not introduce complexity unless it is required by correctness or
  measured performance.
- Avoid verbose implementations.

3. Keep comments simple.
- Comments must be concise, explicit, and useful.
- Explain non-obvious behavior, invariants, or performance-sensitive
  decisions.
- Do not add long comments, decorative comments, or comments that just
  restate the code.

4. Avoid verbosity always.
- Be concise in code structure, comments, changelogs, and task notes.
- Default to the shortest clear explanation, not the longest one.

5. Treat runtime/history logs as part of engineering quality.
- Every repo change (implementations, removals, reversions, and
  architecture decisions) must be recorded in `docs/changelog.md`.
- Entries should include: what changed, why it was changed, the exact
  files/modules changed, and upstream/downstream impact notes.
- For removals/reverts, include why the prior behavior was replaced or
  rolled back, and the migration or behavioral consequences.
- If required validation is deferred, document the reason and scope of
  unverified behavior in that entry.

## Repo organization rules

1. Keep gameplay logic out of `rc-viewer`.
- `rc-core` owns simulation state, item rules, combat math, inventory,
  equipment, drops, shops, banks, skilling, NPC behavior, and tick
  semantics.
- `rc-viewer` owns rendering, camera, input translation, UI visuals,
  animation playback, and presentation-only state.
- `rc-content` owns OSRS-specific scripts and content hooks on top of
  generic `rc-core` APIs.

2. Preserve modular subsystems.
- Runtime systems must consume `RcWorldConfig` subsystem flags and data
  binaries through the owning module.
- Do not add name-specific OSRS logic to `rc-core`; use generated IDs,
  tags, definitions, or `rc-content` scripts.
- Prefer small module-local APIs over new global cross-system state.

3. Keep the database repo boundary clean.
- Do not commit `data/` contents in the main RuneC repository.
- `data/` is an ignored local runtime-data install populated by
  `scripts/setup-data.sh` or maintainer rebuild tooling.
- Runtime code, exporter/tool code, curated source content, schemas, and
  source/gap manifests stay in RuneC; generated runtime data and packs stay
  ignored locally and are distributed as versioned release artifacts.

4. Build game-wide data-backed pipelines, not bespoke validation fixes.
- Validation slices such as Varrock, Graardor, KBD, Vorkath, or Jad are
  probes for the full-game pipeline. Do not solve them with one-off
  frontend/runtime hacks that only work for that encounter or region.
- Cache/export/runtime changes must be shaped to support all loaded
  b237 content: maps, terrain, locs/objects, NPCs, items, spotanims,
  projectiles, models, sequences, skeletal/Maya animations where needed,
  encounter metadata, and related assets.
- Prefer generated definitions, cache-backed metadata, data tags,
  curated data tables, and `rc-content` behavior registries over
  hardcoded per-id branches. If a temporary dev-validation exception is
  unavoidable, isolate it under dev-validation code and document the
  path to replace it with data-backed logic.
- `rc-viewer` must not decide gameplay behavior or asset availability.
  It should render the active area, actor state, dynamic object state,
  projectile/spotanim events, and UI state produced by `rc-core` and
  `rc-content`.
- When testing a small slice, still design code and data flow as if the
  whole game can be activated later without rewriting the pipeline.

5. Update logs.
- Every repo change must add a `changelog.md` entry.
- This includes implementation work, removals/retractions, reversions,
  and core design or architecture decisions.
- Changelog entries must be detailed enough to preserve decision context
  over time: what, why, where, and likely upstream/downstream effects.
- If a change leaves known gaps, assumptions, deferred parity work,
  compatibility concerns, or future upstream/downstream risks, record
  those explicitly in the same changelog entry.
- Pure markdown-only documentation updates are the only allowed exception
  to changelog requirements.
- Record commits, pushes, branch resets, and useful rollback points in
  `docs/git_history.md`.
- Keep `docs/work.md` and `docs/work_highlevel.md` aligned when scope,
  next steps, or stop points change.
- Keep the relevant component README or `docs/` file aligned when
  architecture, scope, or repo boundary rules change.

## Must-do requirements for code / logic changes

1. Run unit tests.
- Run targeted tests for the subsystem being changed.
- Run broader regression tests needed to prove the change did not break
  adjacent systems.
- If tests fail, fix them before considering the task complete.

2. Run coverage analysis.
- Check which changed code paths actually executed under test.
- Treat uncovered changed logic as a problem to resolve, not as
  "probably fine."
- Use coverage results to identify:
  - missing tests
  - dead code
  - code paths that exist but never fire
- For any uncovered path, do one of:
  - add tests
  - remove dead code
  - document exactly why coverage is deferred

3. Run performance benchmarks.
- Benchmark any logic/runtime change that could affect sim throughput,
  tick cost, memory movement, or data-loading overhead.
- Use headless, RL-style benchmark paths where possible.
- Prefer reporting SPS (steps per second) and use workloads that
  emulate PufferLib-style RL training runs.
- Compare before vs after and call out regressions or progressions.

4. Treat verification as part of implementation.
- A code change is not done when the code compiles.
- It is done only after tests, coverage review, and benchmark review
  are complete or a concrete blocker is documented.

## Required reporting after code / logic changes

When closing a task that changed code or logic, report:
- what tests were run
- what coverage check was run and what it showed
- what benchmark was run and the before/after result
- any remaining gaps or deferred verification

## If verification cannot be completed

If the environment blocks testing, coverage, or benchmarking:
- say exactly what could not be run
- say why it could not be run
- say what remains unverified
- do not imply the change is fully validated
