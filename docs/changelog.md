# Changelog

## 2026-07-01 — Work Chunk 9 Viewer Startup Fix

- Change made: fixed the viewer startup path that manual validation found was
  too slow and too laggy to play.
- Exact surfaces changed:
  - Added `RUNEC_SCENE_MODE=auto|generated|fixed` startup selection back to
    `rc-viewer`, with generated startup preferred when a prewarmed slice is
    present and startup auto-export disabled by default.
  - Added a separate `RUNEC_STARTUP_SCENE_RADIUS_REGIONS` defaulting to `0`,
    so normal launch starts on a small generated Varrock-bank slice while
    broader scene streaming can still use `RUNEC_SCENE_RADIUS_REGIONS`.
  - Added `tools/cache_pipeline/extract_runtime_scene_slice.py` to derive
    small runtime scene slices from existing generated runtime region files.
  - Added `tools/cache_pipeline/subset_models_bundle.py` and generated
    `data/models/npcs_varrock_bank.models` for the default startup NPC set.
  - Rebuilt and reinstalled local `dist-data` packs; current local install has
    16 verified packs and includes `scene_3136_3392_r0*` plus
    `npcs_varrock_bank.*`.
  - Projectile models now load only when `RUNEC_LOAD_PROJECTILE_MODELS=1`, and
    higher scene planes are lazy unless `RUNEC_PRELOAD_SCENE_PLANES=1`.
- Exact validation:
  - `cmake --build build -j2 --target rc-viewer test_game_data_validation`
    passed.
  - `python3 -m py_compile tools/pack_runtime_data.py tools/cache_pipeline/subset_models_bundle.py tools/cache_pipeline/extract_runtime_scene_slice.py`
    passed.
  - `scripts/setup-data.sh --verify` passed for 16 installed packs.
  - Loose agent-run windowed measurement completed in 5.7 seconds:
    `timeout 30 env RC_VIEWER_QUIET=1 RUNEC_ASSET_BACKEND=loose RC_VIEWER_EXIT_FRAMES=120 RC_VIEWER_SCREENSHOT=/tmp/runec_loose_r0_slice.png ./build/rc-viewer`.
  - Pack agent-run windowed measurement completed in 11.9 seconds:
    `timeout 30 env RC_VIEWER_QUIET=1 RUNEC_ASSET_BACKEND=pack RC_VIEWER_EXIT_FRAMES=120 RC_VIEWER_SCREENSHOT=/tmp/runec_pack_r0_startup_npcs.png ./build/rc-viewer`.
- Upstream/downstream impact:
  - Work Chunk 9 remains open until the user manually validates the fixed
    viewer. Source-gap work should remain behind this revalidation.

## 2026-07-01 — Work Chunk 9 Agent Windowed Viewer Check

- Change made: validated real windowed viewer startup in both pack and loose
  asset modes as an agent-run check, while leaving user manual validation open.
- Exact validation:
  - `RUNEC_DATA_UNPACK=0 scripts/setup-data.sh --offline dist-data` installed
    the current local packs into `data/packs`.
  - `scripts/setup-data.sh --verify` passed for 15 installed packs.
  - `RUNEC_VIEWER_SMOKE=1 RUNEC_ASSET_BACKEND=pack ./build/rc-viewer` passed.
  - `RUNEC_VIEWER_SMOKE=1 RUNEC_ASSET_BACKEND=loose ./build/rc-viewer` passed.
  - `timeout 90 env RC_VIEWER_QUIET=1 RUNEC_ASSET_BACKEND=pack RC_VIEWER_EXIT_FRAMES=3 RC_VIEWER_SCREENSHOT=/tmp/runec_pack_window.png ./build/rc-viewer`
    passed through an actual windowed startup path and wrote a nonblank
    1280x720 screenshot.
  - `timeout 90 env RC_VIEWER_QUIET=1 RUNEC_ASSET_BACKEND=loose RC_VIEWER_EXIT_FRAMES=3 RC_VIEWER_SCREENSHOT=/tmp/runec_loose_window.png ./build/rc-viewer`
    passed through an actual windowed startup path and wrote a nonblank
    1280x720 screenshot.
- Upstream/downstream impact:
  - Work Chunk 9 has an agent-run windowed pass, but it is not closed until the
    user manually validates pack and loose viewer startup.
  - After user validation, the remaining data-cleanup gap work becomes
    source/release cleanup rather than viewer startup validation.

## 2026-06-29 — Step 8 Runtime Data Release Lock Guard

- Change made: prepared Work Chunk 8 without publishing official runtime data.
- Exact surfaces changed:
  - Updated `runtime-data.lock` to keep `status = "draft"` and
    `official_release = false`, while recording the current remaining source
    gaps as `area_flags`, `npc_spawns`, and `spells`.
  - Added `tools/validate_runtime_data_lock.py` to keep the lock aligned with
    `data-sources/source_gaps.json` and the generated release summary when it
    exists.
  - Wired the runtime-data lock validator into
    `tools/validate_pipeline_inputs.py` and the data pipeline
    `validate-source-authority` path.
  - Updated `data_pipeline.md` and `data_cleanup_gaps.md` to document that the
    official release setup remains prepared but not closeable until the release
    gate is publishable.
- Why it was changed: Step 8 cannot honestly fill official release URLs or
  checksums while the release gate is still
  `not_publishable_source_gaps`, but the lock should not drift from the current
  source-gap state.
- Upstream/downstream impact:
  - Default `scripts/setup-data.sh` still refuses remote download while the
    lock is draft/non-official.
  - Local/offline setup from `dist-data` remains the supported path until the
    source gaps are closed and official release artifacts exist.
  - Future attempts to mark the lock official while source gaps remain will
    fail validation.
- Validation:
  - `python3 -m py_compile tools/validate_runtime_data_lock.py
    tools/validate_pipeline_inputs.py tools/data_pipeline.py` passed.
  - `python3 tools/validate_runtime_data_lock.py` passed with
    `source_gaps=area_flags,npc_spawns,spells`.
  - `python3 tools/validate_pipeline_inputs.py` passed.
  - `python3 tools/data_pipeline.py --check` passed with
    `release_status=not_publishable_source_gaps`.
  - `RUNEC_DATA_DIR=/tmp/runec-step8-data RUNEC_DATA_UNPACK=0
    scripts/setup-data.sh --offline dist-data` passed.
  - `RUNEC_DATA_DIR=/tmp/runec-step8-data scripts/setup-data.sh --verify`
    passed.
  - `RUNEC_DATA_DIR=/tmp/runec-step8-refuse scripts/setup-data.sh` failed as
    intended with the draft/non-official lock refusal.

## 2026-06-29 — Step 7 Combat Visual Source Gap Closure

- Change made: completed the `combat_visuals` part of Work Chunk 7 while
  keeping the remaining true source gaps release-blocking.
- Exact surfaces changed:
  - Removed `combat_visuals` from `data-sources/source_gaps.json` and the
    generated source-gap writer in `tools/validate_source_authority.py`.
  - Updated `tools/validate_reports.py` so the report gate now expects the
    remaining source-gap datasets: `area_flags`, `npc_spawns`, and `spells`.
  - Tightened `tools/export_combat_visuals.py` so curated NPC rows must carry
    `curated:b237:` notes, duplicate checks include both stance and attack key,
    and the report records the closed source-gap status.
  - Updated `content/combat_visuals/README.md`,
    `tools/reports/combat_visuals.txt`, `tools/reports/area_flags_sources.txt`,
    `data-sources/sources.lock`, and `data_cleanup_gaps.md` for the new
    source-gap state.
- Why it was changed: combat visuals are now regenerated from tracked
  RuneC-owned content with provenance checks, so keeping them in the
  release-blocking source-gap list was stale. Area flags, NPC spawn
  direction/wander/provisional fixtures, and spell combat effects are still
  real source gaps in the current source tree.
- Upstream/downstream impact:
  - Generated release summaries now list `area_flags`, `npc_spawns`, and
    `spells` as the remaining source-gap datasets.
  - The release gate still correctly returns
    `release_status=not_publishable_source_gaps`.
  - `combat_visuals.tsv` remains a runtime-packaged TSV, but unresolved future
    rows must go back into `data-sources/source_gaps.json` instead of using
    blocked external checkout inputs.
- Validation:
  - `python3 -m py_compile tools/export_combat_visuals.py
    tools/validate_source_authority.py tools/validate_reports.py` passed.
  - `python3 tools/validate_sources.py` passed.
  - `python3 tools/validate_source_authority.py --write-gap-report` passed.
  - `python3 tools/export_combat_visuals.py` passed and produced 3,532 rows,
    20/20 reviewed NPC rows, 0 failures, and 0 warnings.
  - `python3 tools/data_pipeline.py --clean-generated all` passed, rebuilt 15
    packs containing 31,770 assets, and ended with
    `release_status=not_publishable_source_gaps`.
  - `python3 tools/data_pipeline.py --check` passed.
  - `cmake --build build -j2` passed.
  - `ctest --test-dir build --output-on-failure` passed 69/69.
  - `git diff --check` passed.

## 2026-06-29 — Content Regeneration Pipeline Promotion

- Change made: completed Work Chunk 6 for the current pass by promoting
  tracked content outputs from inventory-only to regenerated in the top-level
  data pipeline.
- Exact surfaces changed:
  - `tools/data_pipeline.py export-content` now runs the self-contained
    content exporters for encounters, activity spawns, activity states,
    activity mechanics, activity schemas, dialogue, combat visuals, and item
    effects.
  - The export-content stage record now lists exporter commands, observed
    outputs, and deferred content exports.
  - Cache/render assets and remaining non-content defs now write explicit
    `remaining_regeneration_gap` metadata in their stage records.
  - Activity content reports no longer write nondeterministic `elapsed_ms`
    lines, preventing source-report churn on clean regeneration.
  - Fixed `content/catalog.toml` so `activity_spawns` points at
    `data/defs/activity_spawns.bin`.
  - Updated `data-sources/sources.lock` for the corrected content-tree hash.
- Why it was changed: Work Chunk 6 requires true regeneration where approved
  inputs are already self-contained, while keeping the remaining cache/wiki
  source gaps explicit instead of silently packaging stale outputs.
- Upstream/downstream impact:
  - `python3 tools/data_pipeline.py all` now rebuilds tracked content-derived
    runtime defs before packing.
  - Quests, regular NPC special mechanics, item specials, cache/render assets,
    regions/spawns, and most non-content defs remain documented
    inventory-only/deferred surfaces until approved source inputs are wired.
- Validation:
  - `python3 -m py_compile tools/data_pipeline.py
    tools/export_activity_mechanics.py tools/export_activity_schemas.py
    tools/export_activity_spawns.py tools/export_activity_states.py` passed.
  - `python3 tools/data_pipeline.py export-content` passed.
  - `python3 tools/data_pipeline.py --clean-generated all` passed, rebuilt 15
    packs containing 31,770 assets, and ended with
    `release_status=not_publishable_source_gaps`.
  - `python3 tools/validate_sources.py` passed.
  - `python3 tools/data_pipeline.py --check` passed with
    `release_status=not_publishable_source_gaps`.
  - `cmake --build build -j2` passed and ran the direct-global guard.
  - `ctest --test-dir build --output-on-failure` passed 69/69.
  - `git diff --check` passed.

## 2026-06-29 — Older Dataset RcGameData Ownership

- Change made: completed Work Chunk 5 by moving the known older immutable
  runtime datasets into `RcGameData` ownership.
- Exact surfaces changed:
  - Added owned `RcGameData` containers and accessors for varbit/varp
    definitions, skill recipes/drops/gathering nodes, shops, quests, and
    dialogue transcripts.
  - Updated those runtime modules to load into explicit data containers,
    activate read views, reset active views on release, and retain legacy
    global mirrors only as compatibility shims.
  - Moved the migrated dataset load paths out of `rc-core/world.c` and into
    `rc_game_data_load()`.
  - Expanded `RcGameDataStats` and `tests/test_game_data_shared.c` so the
    shared-data test proves active accessors point into the owned
    `RcGameData` containers.
  - Tightened `tools/validate_direct_global_access.py` so production code is
    blocked from directly reading the migrated Step 5 compatibility globals.
  - Updated `data_cleanup_gaps.md` with the migrated ownership decisions and
    the remaining explicit carveouts for player action gates, combat profiles,
    monster mechanics, and slayer definition tables.
- Why it was changed: these tables are shared immutable runtime data and
  should be owned by the loaded game-data snapshot instead of depending on
  mutable process-global load order.
- Upstream/downstream impact:
  - Runtime callers should use existing accessors or `RcGameData` view
    accessors instead of touching compatibility globals.
  - Legacy direct loader APIs still populate compatibility globals for tests
    and older fixture paths.
  - Per-world mutable varp values and quest/slayer player progress remain
    world/player state.
  - Player action gates, combat profiles, monster mechanics, and slayer
    definition tables remain documented follow-up ownership carveouts.
- Validation:
  - `cmake --build build -j2` passed and ran the direct-global guard.
  - Focused Step 5 tests passed:
    `ctest --test-dir build --output-on-failure -R
    'test_game_data_shared|test_modular_loading|test_skills_runtime|test_quests_dialogue_runtime|test_varbits_runtime|test_shops_storage_runtime'`.
  - `python3 tools/validate_direct_global_access.py` passed with 73 protected
    migrated globals.
  - `python3 -m py_compile tools/validate_direct_global_access.py
    tools/data_pipeline.py` passed.
  - `python3 tools/data_pipeline.py --check` passed with
    `release_status=not_publishable_source_gaps`.
  - `ctest --test-dir build --output-on-failure` passed 69/69.
  - `git diff --check` passed.

## 2026-06-29 — Interaction Registry Global Fallback Removal

- Change made: completed Work Chunk 4 by removing the legacy mutable
  process-global interaction handler table.
- Exact surfaces changed:
  - Removed the legacy `rc_interaction_clear_handlers()`,
    `rc_interaction_register_handler()`, `rc_interaction_handler_count()`, and
    `rc_interaction_find_handler()` APIs.
  - Simplified interaction dispatch so it only consults the current
    `RcWorld` handler table.
  - Migrated interaction, ground-item, and combat handoff tests to
    `rc_interaction_register_world_handler()`.
  - Updated the multi-world handler isolation test so a world with cleared
    handlers fails with `RC_INTERACTION_FAIL_NO_HANDLER` instead of falling
    back to process-global state.
  - Updated `data_cleanup_gaps.md` to mark the encounter/content registry
    compatibility cleanup complete for the current pass.
- Why it was changed: mutable process-global handler registries break
  multi-world runtime isolation and no longer had production callers after the
  earlier world-owned registration migration.
- Upstream/downstream impact:
  - New custom interaction handlers must be registered on the target
    `RcWorld`.
  - Production default NPC/object/ground-item handlers continue to be
    registered lazily on each world before dispatch.
  - Encounter specs/scripts and combat content hooks remain world-owned;
    immutable primitive/code dispatch tables remain intentional static code.
- Validation:
  - `rg` found no remaining legacy interaction handler API or static handler
    table references in `rc-core` or `tests`.
  - `cmake --build build -j2` passed and ran the direct-global guard.
  - Focused interaction/content tests passed:
    `ctest --test-dir build --output-on-failure -R
    'test_interaction_engine_phase2|test_interaction_engine_phase3|test_interaction_engine_phase4|test_interaction_engine_phase5|test_interaction_engine_phase6|test_interaction_engine_phase7|test_interaction_engine_phase8|test_ground_items_phase1|test_ground_items_phase2|test_ground_items_phase5|test_combat_phase2_attack_handoff|test_npc_option_interactions|test_modular_loading'`.
  - `python3 tools/validate_direct_global_access.py` passed.
  - `python3 tools/data_pipeline.py --check` passed with
    `release_status=not_publishable_source_gaps`.
  - `ctest --test-dir build --output-on-failure` passed 69/69.
  - `git diff --check` passed.

## 2026-06-29 — RcGameData Mirror Compatibility Audit

- Change made: started Work Chunk 3 by removing a read-only test dependency on
  migrated transitional globals and documenting the remaining compatibility
  boundary.
- Exact surfaces changed:
  - Updated `tests/test_game_data_shared.c` so the shared-data lifecycle test no
    longer compares `RcGameData` owned tables against legacy global mirror
    addresses.
  - The test now proves the active runtime NPC view points at the shared
    `RcGameData` NPC definitions and keeps the rest of its checks on
    `RcGameData` accessors plus runtime view/accessor behavior.
  - Updated `data_cleanup_gaps.md` to record that production direct access is
    blocked, while remaining direct migrated-global hits are fixture/benchmark
    compatibility users.
- Why it was changed: the ownership test should exercise `RcGameData` and
  active views directly, not rely on the existence of compatibility mirrors
  that the migration intends to retire.
- Upstream/downstream impact:
  - No runtime behavior changes.
  - No public mirror was removed in this pass; remaining fixture tests still
    need helper APIs or data-builder migration before those mirrors can be
    hidden or deleted.
- Validation:
  - `cmake --build build -j2 --target test_game_data_shared` passed and ran
    the production direct-global guard.
  - `ctest --test-dir build --output-on-failure -R
    '^test_game_data_shared$'` passed.
  - `python3 tools/validate_direct_global_access.py` passed.
  - `python3 -m py_compile tools/validate_direct_global_access.py
    tools/data_pipeline.py` passed.
  - A protected-global scan of `tests/test_game_data_shared.c` returned no
    hits.
  - `python3 tools/data_pipeline.py --check` passed with
    `release_status=not_publishable_source_gaps`.
  - `cmake --build build -j2` passed.
  - `ctest --test-dir build --output-on-failure` passed 69/69.

## 2026-06-29 — Pipeline Inventory Record Accuracy

- Change made: fixed the cache-derived asset stage inventory so it records the
  real source lock file.
- Exact surfaces changed:
  - Updated `tools/data_pipeline.py` so
    `stage_export_cache_derived_assets()` records
    `data-sources/sources.lock` under `sources_lock`.
  - Updated `data_cleanup_gaps.md` to mark Work Chunk 2 complete for the
    current pass.
- Why it was changed: the stage declared `data-sources/sources.lock` as its
  input, but the generated inventory record pointed at nonexistent
  `data-sources/source_locks.toml`, making the stage record less trustworthy.
- Upstream/downstream impact:
  - Generated pipeline records now point at the actual source lock used by the
    validators.
  - This does not change runtime data contents or release status.
- Validation:
  - `python3 tools/data_pipeline.py export-cache-derived-assets` regenerated
    `generated/pipeline/stages/export-cache-derived-assets.json`.
  - The regenerated stage record contains `sources_lock.path =
    data-sources/sources.lock` with `exists = true` and no
    `source_locks.toml` reference.
  - `python3 -m py_compile tools/data_pipeline.py` passed.
  - `python3 tools/data_pipeline.py --check` passed with
    `release_status=not_publishable_source_gaps`.

## 2026-06-29 — Clean Clone Source Tracking Baseline

- Change made: prepared the data-cleanup source baseline for a future clean
  clone by staging the self-contained source inputs and leaving generated
  runtime outputs ignored.
- Exact surfaces changed:
  - Staged source-controlled cleanup inputs across code, tests, tools,
    schemas, content TOML, data-source metadata, runtime data lock files, and
    focused documentation.
  - Kept generated runtime installs and release outputs under `data/`,
    `dist-data/`, and `generated/` out of Git.
  - Updated `.gitignore` so content-local Markdown docs, such as
    `content/combat_visuals/README.md`, are treated as trackable source docs.
- Why it was changed: a fresh clone needs authored cleanup inputs in Git, but
  should not carry local generated packs, loose runtime data, build output, or
  Python bytecode.
- Upstream/downstream impact:
  - There are no remaining untracked non-ignored files after staging.
  - Official clean-clone runtime setup still depends on a later runtime data
    release or documented offline artifact source.
- Validation:
  - `git diff --cached --check` passed after mechanically trimming trailing
    whitespace in staged content TOML files.
  - `git ls-files --others --exclude-standard` returned no untracked
    source-controlled candidates.
  - Staged generated-artifact scan found no `dist-data/`, `generated/`,
    `build/`, `__pycache__/`, or `.pyc` paths; the only staged `data/` path is
    `data/README.md`.
  - `python3 tools/data_pipeline.py --check` passed with
    `release_status=not_publishable_source_gaps`.
  - `cmake --build build -j2` passed and ran the direct-global guard.
  - `scripts/setup-data.sh --verify` passed for the local installed runtime
    packs.
  - `ctest --test-dir build --output-on-failure` passed 69/69.

## 2026-06-28 — Phase 8 Pipeline Regeneration And Runtime Smoke

- Change made: completed the broader Phase 8 cleanup/regeneration/runtime
  verification pass for the current data-cleanup cycle.
- Exact surfaces changed:
  - Ran the pipeline cleaner/regeneration flow:
    `python3 tools/data_pipeline.py --clean-generated all`.
  - Fixed the pipeline report generation stage so it emits
    `generated/reports/area_flags_gaps.{json,txt}` through
    `tools/export_area_flags.py` before report validation.
  - Reran `python3 tools/data_pipeline.py all`; it rebuilt
    `data/manifest.json`, `dist-data/manifest.json`, 15 release packs, the
    unpack marker, pipeline records, and generated reports.
  - Installed the regenerated release packs into the default local pack runtime
    location with `RUNEC_DATA_UNPACK=0 scripts/setup-data.sh --offline
    dist-data`.
  - Added `RUNEC_VIEWER_SMOKE=1` to `rc-viewer` so headless environments can
    verify viewer startup/runtime data loading before GLFW window creation.
  - Documented the viewer smoke commands in `README.md` and updated
    `data_cleanup_gaps.md` with completed/remaining Phase 8 status.
- Why it was changed: Phase 8 requires a clean generated-output refresh,
  report-gated runtime packs, and verified pack/loose runtime startup after the
  structural data ownership changes.
- Upstream/downstream impact:
  - Local default pack mode now works after installing generated `dist-data`
    packs into `data/packs` with `scripts/setup-data.sh --offline dist-data`.
  - `tools/data_pipeline.py all` no longer fails from a cleaned report root due
    to missing area-flag gap reports.
  - Interactive viewer window validation still requires a display server or
    Xvfb; the new smoke mode covers startup/data/backend verification in
    headless shells.
- Validation:
  - `python3 tools/data_pipeline.py --clean-generated all` ran the cleaner and
    regenerated packs/reports; the first pass exposed the missing area-flag gap
    report.
  - `python3 tools/data_pipeline.py all` passed after the report-stage fix with
    `release_status=not_publishable_source_gaps`.
  - `RUNEC_DATA_UNPACK=0 scripts/setup-data.sh --offline dist-data` verified
    and installed 15 packs into `data/packs`.
  - `scripts/setup-data.sh --verify` passed for installed packs.
  - `RUNEC_VIEWER_SMOKE=1 RUNEC_ASSET_BACKEND=pack ./build/rc-viewer` passed
    with 3,532 combat visuals and 837 spawned NPCs.
  - `RUNEC_VIEWER_SMOKE=1 RUNEC_ASSET_BACKEND=loose ./build/rc-viewer` passed
    with 3,532 combat visuals and 837 spawned NPCs.
  - `python3 tools/data_pipeline.py --check` passed with
    `release_status=not_publishable_source_gaps`.
  - `ctest --test-dir build --output-on-failure` passed 69/69.
  - `data/curated` is absent; `content/encounters/scurrius.toml` is not
    ignored; `data/manifest.json`, `data/packs`, `dist-data/manifest.json`,
    and `dist-data/packs` are ignored generated runtime artifacts.
  - Blocked-source scan matched only validator policy files; the filtered
    active-code/runtime scan returned no matches.

## 2026-06-28 — Phase 8 Direct Global Access Enforcement

- Change made: added build/test/pipeline enforcement against direct production
  access to migrated `RcGameData` transitional globals.
- Exact surfaces changed:
  - Added `tools/validate_direct_global_access.py`, which scans production
    C/header files under `rc-core`, `rc-content`, and `rc-viewer`.
  - Protected the migrated `RcGameData` table globals for items, NPCs, drops,
    prayers, spells, normalization, objects, collision, area flags, traversal,
    and activity data.
  - Allowed only the owner modules and `rc-core/game_data.c` compatibility
    mirror/import boundary to touch those protected globals directly.
  - Wired the guard into CMake before `rc-core` builds and exposed it as the
    `validate_direct_global_access` CTest.
  - Added the guard to `tools/data_pipeline.py` validate-content/check flow.
  - Updated `data_cleanup_gaps.md` with active Phase 8 enforcement status and
    the remaining broader cleanup/regeneration work.
- Why it was changed: after Phase 7 moved table ownership behind
  `RcGameData`, Phase 8 needs regressions to fail automatically instead of
  relying on ad hoc scans.
- Upstream/downstream impact:
  - New production code must use accessors, active runtime views, `RcGameData`
    accessors, or world-owned state instead of direct migrated globals.
  - Test fixtures can still seed/inspect legacy globals because the guard
    intentionally scans production source only.
  - Non-migrated legacy datasets, such as shops/skills/quests/varbits/dialogue,
    are not blocked by this guard yet and should be handled by later ownership
    passes or explicit carveouts.
- Validation:
  - `cmake --build build -j2` passed; the CMake guard ran before `rc-core`.
  - `python3 tools/validate_direct_global_access.py` passed.
  - `python3 -m py_compile tools/validate_direct_global_access.py
    tools/data_pipeline.py` passed.
  - `ctest --test-dir build --output-on-failure` passed 69/69, including
    `validate_direct_global_access`.
  - `python3 tools/data_pipeline.py --check` passed and now runs
    `tools/validate_direct_global_access.py`.
  - `git diff --check` passed.
  - Blocked-source scan matched only validator policy files.

## 2026-06-28 — Pre-Phase 8 Registry Ownership Audit

- Change made: audited the remaining encounter/content registry state and
  migrated the clear mutable registry case into world-owned storage.
- Exact surfaces changed:
  - Added `RcWorld` interaction handler storage plus world-scoped registration,
    lookup, count, and clear APIs.
  - Moved production default NPC/object/ground-item interaction handler
    registration from the legacy process-global table to the current world.
  - Kept the legacy interaction handler table as a compatibility/test fallback,
    with same-specificity legacy registrations still overriding default world
    handlers to preserve older embedding behavior.
  - Added multi-world interaction handler isolation coverage.
  - Classified encounter spec/script registries as already per-world
    `RcEncounterState`, combat hooks as already `RcWorld` state, and immutable
    code-local dispatch tables as Phase 8 carveouts.
- Why it was changed: Phase 8 enforcement needs mutable runtime registries to
  be world-owned or explicitly carved out before compile-time global access
  checks are added.
- Upstream/downstream impact:
  - Production interaction dispatch now uses world handler tables for default
    runtime behavior, which supports multiple worlds in one process.
  - Legacy `rc_interaction_*_handler` APIs remain available during the
    compatibility window but should not be used by new production code.
  - `data_cleanup_gaps.md` now records the remaining enforcement carveouts:
    interaction compatibility table, `RcGameData` mirror/import code,
    immutable code-local dispatch tables, and dialogue transcript globals for
    a later ownership/carveout decision.
- Validation:
  - `cmake --build build -j2` passed.
  - Focused registry/interaction/content tests passed:
    `ctest --test-dir build --output-on-failure -R 'test_interaction_engine_phase2|test_interaction_engine_phase3|test_interaction_engine_phase4|test_interaction_engine_phase5|test_interaction_engine_phase6|test_interaction_engine_phase7|test_interaction_engine_phase8|test_ground_items_phase5|test_combat_phase2_attack_handoff|test_npc_option_interactions|test_modular_loading|test_game_data_shared'`.
  - `ctest --test-dir build --output-on-failure` passed 68/68.
  - `python3 tools/data_pipeline.py --check` passed with
    `release_status=not_publishable_source_gaps`.
  - Direct-global and registry scans passed with remaining matches limited to
    documented compatibility, per-world encounter fields, immutable code-local
    tables, and known non-registry dialogue globals.

## 2026-06-26 — Phase 7 Direct Global Consumer Cleanup

- Change made: converted remaining core/content/viewer compatibility consumers
  of migrated runtime tables to active view helpers/accessors.
- Exact surfaces changed:
  - Added `rc_npc_def_for_npc()` plus NPC definition name/all-def accessors for
    callers that need one NPC definition, a name lookup, or a full definition
    scan.
  - Replaced direct NPC definition global reads in combat, combat formulae,
    tick interactions, encounter runtime/primitives, regular NPC combat,
    Slayer matching, storage, world NPC lookup, dev validation, and viewer
    model/render/context-menu paths.
  - Replaced remaining direct object-placement/traversal reads in tick/viewer
    code with object/traversal active-view helpers.
  - Left `rc-core/game_data.c` compatibility mirror/import references in place;
    those are the mirror boundary rather than runtime consumers.
- Why it was changed: Phase 7's ownership model requires gameplay/content/viewer
  code to consume the active `RcGameData` views instead of indexing transitional
  process-global mirrors.
- Upstream/downstream impact:
  - Core/content/viewer source scans now show migrated-table globals only in
    owner modules and `RcGameData` compatibility mirroring.
  - Test fixtures still seed compatibility globals directly until Phase 8 adds
    compile-time guardrails with explicit fixture exceptions or helper APIs.
  - Phase 8 direct-global access enforcement can now target source consumers
    without first migrating gameplay/viewer call sites.
- Validation:
  - `cmake --build build -j2` passed.
  - Focused ownership/NPC/combat tests passed:
    `ctest --test-dir build --output-on-failure -R 'test_game_data_shared|test_modular_loading|test_objects_runtime|test_traversal_runtime|test_regular_npc_mechanics_combat|test_combat_phase3_movement_range_facing|test_combat_phase5_formula_core|test_combat_phase7_retaliation_ai|test_combat_phase8_view_state|test_combat_runtime_flow|test_encounter|test_encounter_prims|test_npc_option_interactions|test_shops_storage_runtime|test_slayer_bin'`.
  - `ctest --test-dir build --output-on-failure` passed 68/68.
  - `python3 tools/data_pipeline.py --check` passed with
    `release_status=not_publishable_source_gaps`.
  - Source scan passed for migrated direct consumers; remaining matches are
    limited to `rc-core/game_data.c` compatibility mirror/import code.

## 2026-06-26 — Phase 7 Larger/Indexed Table Ownership

- Change made: completed the remaining Phase 7 ownership cleanup for the
  larger/indexed runtime table families.
- Exact surfaces changed:
  - Added owned data containers, loaders, active-view helpers, and reset hooks
    for object data, collision tiles, area flags, traversal edges, and activity
    schemas/spawns/mechanics/states.
  - Extended `RcGameData` to own those containers and added public accessors for
    each migrated family.
  - Updated `rc_game_data_load()` to load those tables into `RcGameData`, mirror
    compatibility globals, and activate owned views for worlds using shared
    game data.
  - Preserved legacy/test compatibility by importing seeded object/activity
    globals into `RcGameData` when present, and by keeping same-data traversal
    mirroring non-destructive for existing legacy pointers.
  - Expanded `tests/test_game_data_shared.c` to assert owned backing storage
    and representative runtime lookup paths for objects, collision, area
    flags, traversal, and activity data.
- Why it was changed: Phase 7's scaling target is one immutable validated
  `RcGameData` load shared by many worlds, rather than large runtime tables
  being owned directly by process globals.
- Upstream/downstream impact:
  - Compatibility globals still exist as mirrors while direct global consumers
    are migrated.
  - Runtime lookup helpers now read from the active `RcGameData` views after
    shared data/world activation.
  - Phase 8 compile-time direct-global access enforcement should wait until
    remaining direct consumers are converted to accessors/view helpers.
- Validation:
  - `cmake --build build -j2` passed.
  - Focused runtime/data tests passed:
    `ctest --test-dir build --output-on-failure -R 'test_game_data_shared|test_modular_loading|test_objects_runtime|test_collision_tiles_runtime|test_area_flags_runtime|test_traversal_runtime|test_activity_(schemas|spawns|mechanics|states)_bin|test_active_area_runtime|test_interaction_engine_phase1|test_interaction_engine_phase7|test_interaction_engine_phase8|test_plane_contracts_runtime'`.
  - `ctest --test-dir build --output-on-failure` passed 68/68.
  - `python3 tools/data_pipeline.py --check` passed with
    `release_status=not_publishable_source_gaps`.
  - `python3 -m py_compile tools/data_pipeline.py tools/pack_runtime_data.py`
    passed.
  - Targeted C coverage from `/tmp/runec_phase7_remaining_cov` passed:
    touched-filter total 82%; `rc-core/traversal.c` 97%,
    `rc-core/activity_mechanics.c` 94%, `rc-core/activity_states.c` 93%,
    `rc-core/collision.c` 90%, `rc-core/activity_schemas.c` 89%,
    `rc-core/area_flags.c` 88%, `rc-core/objects.c` 86%,
    `rc-core/game_data.c` 73%, `rc-core/activity_spawns.c` 58%.
  - Backend workload benchmark passed:
    `timeout 120 bash tests/benchmarks/run_backend_workloads_benchmark.sh --mode spawn --ops 20`
    reported 279 active-area loads/sec.

## 2026-06-26 — Phase 7 Static Table Ownership

- Change made: completed the low-risk Phase 7 static table ownership slice by
  moving item definitions, NPC definitions, and drop tables into `RcGameData`
  storage.
- Exact surfaces changed:
  - Added caller-owned loaders and active-view helpers for item and NPC
    definitions.
  - Added `RcDropData` ownership for NPC drop tables plus RDT/GDT/MRDT rows,
    with compatibility global mirrors retained for legacy callers.
  - Added `rc_game_data_npc_defs()`, `rc_game_data_item_defs()`, and
    `rc_game_data_drop_data()` accessors.
  - Updated `RcGameData` loading/release to own NPC, item, drop, prayer,
    spell, and normalization rows, then activate those views for worlds built
    from shared data.
  - Preserved legacy test/content compatibility by mirroring owned data back to
    globals and falling back to changed globals when tests inject synthetic
    definitions.
  - Changed spawn-specific NPC wander overrides to live on `RcNpc` instances
    instead of mutating shared NPC definition rows.
  - Expanded `tests/test_game_data_shared.c` to assert owned storage for NPCs,
    items, drops, prayers, spells, and normalization.
- Why it was changed: Phase 7 requires immutable runtime data to be owned by a
  shared `RcGameData` object so many worlds can reuse one validated load
  without relying on process-global table storage.
- Upstream/downstream impact:
  - Runtime helpers for the migrated static tables now read from active
    `RcGameData` views after a shared data handle/world activates them.
  - Compatibility globals still exist as mirrors during migration.
  - Larger/indexed table families still need storage-owner migration before
    Phase 8 compile-time direct-global access enforcement.
- Validation:
  - `cmake --build build -j2` passed, including `rc-viewer`.
  - Focused former-failure tests passed:
    `ctest --test-dir build --output-on-failure -R 'test_combat_phase3_movement_range_facing|test_npc_option_interactions|test_combat_visuals_projectiles|test_encounter|test_encounter_prims|test_combat_phase10_resources_specials|test_shops_storage_runtime'`.
  - `ctest --test-dir build --output-on-failure -j2` passed 68/68.
  - `python3 tools/data_pipeline.py --check` passed with
    `release_status=not_publishable_source_gaps`.
  - `python3 -m py_compile tools/data_pipeline.py tools/pack_runtime_data.py`
    passed.
  - Targeted C coverage from `/tmp/runec_phase7_tables_cov` passed:
    `tests/test_game_data_shared.c` 100%, `rc-core/world.c` 92%,
    `rc-core/drops.c` 74%, `rc-core/game_data.c` 74%,
    `rc-core/normalization.c` 73%, `rc-core/spells.c` 70%,
    `rc-core/npc.c` 61%, touched-filter total 64%.
  - Backend workload benchmark passed:
    `timeout 120 bash tests/benchmarks/run_backend_workloads_benchmark.sh --mode spawn --ops 20`
    reported 307 active-area loads/sec.

## 2026-06-26 — rc-content Link Model and Normalization Ownership

- Change made: fixed the `rc-viewer` link failure and continued Phase 7 table
  ownership by moving normalization rows into `RcGameData`.
- Exact surfaces changed:
  - Added a CMake `rc-content-objects` object library and `rc-content-link`
    interface target so full-content executables link concrete content objects
    instead of relying on static archive extraction order.
  - Kept `rc-content` as a static library for existing library consumers.
  - Updated `rc-viewer` and C test executables to link through
    `rc-content-link`; `rc-viewer` now resolves
    `rc_content_encounter_script_stubs_register`.
  - Added `rc_load_normalization_into()` plus active normalization view/reset
    helpers.
  - Added `RcGameData` ownership for item normalization, NPC normalization, and
    source-normalization rows, with compatibility globals retained as mirrors.
  - Added `rc_game_data_item_normalization()`,
    `rc_game_data_npc_normalization()`, and
    `rc_game_data_source_normalization()` accessors.
  - Updated world creation from shared data to activate the shared
    normalization view for relevant subsystem masks.
  - Expanded `tests/test_game_data_shared.c` to assert data-owned
    normalization storage and legacy helper behavior.
- Why it was changed: the viewer needed a robust full-content link path, and
  Phase 7 requires immutable table storage to move behind `RcGameData` while
  legacy global consumers are migrated incrementally.
- Upstream/downstream impact:
  - Full-content executables no longer depend on archive member ordering for
    content registration symbols.
  - Normalization helpers now read from the active `RcGameData` storage when a
    shared data handle/world activates it.
  - Direct `g_rc_*_normalization*` access remains available as a compatibility
    mirror during migration.
  - Other larger table families still need ownership/accessor migration before
    Phase 8 compile-time global-access enforcement.
- Validation:
  - `cmake --build build --target test_game_data_shared -j2` passed.
  - Focused tests passed:
    `ctest --test-dir build --output-on-failure -R 'test_game_data_shared|test_normalization_runtime|test_modular_loading'`.
  - `cmake --build build -j2` passed, including `rc-viewer`.
  - `ctest --test-dir build --output-on-failure -j2` passed 68/68.
  - `python3 tools/data_pipeline.py --check` passed with
    `release_status=not_publishable_source_gaps`.
  - `python3 -m py_compile tools/data_pipeline.py tools/pack_runtime_data.py`
    passed.
  - Targeted C coverage from `/tmp/runec_phase7_norm_cov` passed:
    `tests/test_game_data_shared.c` 100%, `rc-core/normalization.c` 91%,
    `rc-core/game_data.c` 75%, `rc-core/world.c` 60%, touched-filter total
    76%.
  - Backend workload benchmark passed:
    `timeout 120 bash tests/benchmarks/run_backend_workloads_benchmark.sh --mode spawn --ops 20`
    reported 275 active-area loads/sec.

## 2026-06-26 — Runtime Data Path and Phase 7 Ownership Fixes

- Change made: resolved the verified runtime/pipeline findings that were safe
  to fix now and continued Phase 7 with a scoped table-ownership migration.
- Exact surfaces changed:
  - Fixed loose asset path resolution so default `data/...` config paths honor
    `RUNEC_DATA_ROOT` instead of being treated as repository-relative paths.
  - Kept `area_flags.bin` as required runtime data, but marked it as a
    provisional required logical path in generated manifests while the
    `area_flags` source gap keeps releases non-publishable.
  - Kept `runtime-data.lock` draft/non-official; release URLs/checksums remain
    empty until source gaps are resolved.
  - Left Phase 4 top-level export stages as inventory-only for now. True full
    regeneration remains deferred to lower-level exporter work with approved
    inputs.
  - Aligned pack size metadata by emitting `bytes` alongside `size` and making
    `tools/data_pipeline.py --check` validate either field.
  - Moved prayer and spell table ownership into `RcGameData` arrays, with
    compatibility globals retained as mirrors during migration.
  - Added `rc_game_data_prayer_defs()` and `rc_game_data_spell_defs()` plus
    loader variants that parse into caller-owned storage.
  - Regenerated local runtime packs/manifests so `data/manifest.json` and
    `dist-data/manifest.json` include pack `bytes` and provisional
    `area_flags` metadata.
- Why it was changed: these fixes close concrete runtime correctness gaps while
  preserving the source-gap release gate and continuing the planned migration
  from process-global data toward immutable shared `RcGameData`.
- Upstream/downstream impact:
  - Pack mode still works through logical pack paths.
  - Loose mode now works from outside the repo root when `RUNEC_DATA_ROOT`
    points at the runtime data directory.
  - Direct `g_rc_prayer_*` and `g_rc_spell_*` access still exists for
    compatibility, but new worlds activate the shared `RcGameData` prayer/spell
    storage.
  - Other table families remain transitional and still need ownership migration.
- Validation:
  - `cmake --build build --target test_game_data_shared -j2` passed.
  - `cmake --build build --target test_modular_loading -j2` passed.
  - Focused runtime tests passed:
    `ctest --test-dir build --output-on-failure -R 'test_game_data_shared|test_modular_loading|test_combat_phase5_formula_core|test_headless_action_runtime|test_prayer_spell_actions_runtime|test_combat_phase10_resources_specials|test_combat_phase11_validation_gate'`.
  - Loose root runtime check passed from `/tmp`:
    `RUNEC_DATA_ROOT=/home/joe/projects/RuneC_v4/data RUNEC_ASSET_BACKEND=loose build/test_game_data_shared`.
  - `python3 tools/data_pipeline.py pack-runtime-data` passed and regenerated
    15 packs containing 31,770 assets.
  - `python3 tools/data_pipeline.py --check` passed with
    `release_status=not_publishable_source_gaps`.
  - `ctest --test-dir build --output-on-failure -j2` passed 68/68.
  - Targeted C coverage from `/tmp/runec_phase7_cov` passed:
    `tests/test_game_data_shared.c` 100%, `rc-core/game_data.c` 74%,
    `rc-core/spells.c` 76%, `rc-core/world.c` 48%, `rc-core/prayer.c` 33%,
    `rc-core/assets.c` 12%, touched-filter total 49%.
  - Backend workload benchmark passed:
    `timeout 120 bash tests/benchmarks/run_backend_workloads_benchmark.sh --mode spawn --ops 20`
    reported 304 active-area loads/sec.
- Follow-up validation note:
  - The prior `rc-viewer` link failure for
    `rc_content_encounter_script_stubs_register` was resolved in the
    subsequent `rc-content` link-model entry above.

## 2026-06-26 — Data Cleanup Phase 7 Full Loader Boundary

- Change made: moved the remaining Phase 7 dataset loading boundary behind
  `RcGameData`.
- Exact surfaces changed:
  - Extended `RcGameDataStats` with object, collision, area flag, traversal,
    activity, and encounter counts.
  - Moved object defs/behaviors/placements/transports, collision tiles, area
    flags, traversal edges, activity schemas/spawns/mechanics/states, and
    encounter spec loading into `rc_game_data_load()`.
  - Split encounter loading so `rc_encounter_load_specs()` parses immutable
    encounter definitions once for `RcGameData`, while `RcWorld` copies those
    specs into its per-world encounter state after `rc_encounter_init()`.
  - Removed those larger/indexed table loader calls from
    `rc_world_create_with_data()`.
  - Expanded `tests/test_game_data_shared.c` to assert larger table counts and
    shared encounter registry reuse across multiple worlds.
- Why it was changed: Phase 7 requires shared game data to scale world
  creation by separating immutable runtime data loading from per-world mutable
  simulation state.
- Upstream/downstream impact:
  - `rc_world_create_config()` still preserves the legacy convenience path by
    loading `RcGameData` internally and then creating a world from it.
  - `rc_world_create_with_data()` no longer reparses object, region, traversal,
    activity, or encounter data for each world.
  - Compatibility globals remain in place for current core/content/viewer/test
    consumers. The remaining Phase 7 cleanup is to convert direct global table
    access to `RcGameData` accessors and then make accidental direct access fail
    at compile time.
- Validation:
  - `cmake --build build -j2` passed.
  - `ctest --test-dir build --output-on-failure -R test_game_data_shared`
    passed.
  - Focused runtime/data tests passed:
    `ctest --test-dir build --output-on-failure -R 'test_modular_loading|test_encounter_bin|test_objects_runtime|test_collision_tiles_runtime|test_area_flags_runtime|test_traversal_runtime|test_activity_(schemas|spawns|mechanics|states)_bin'`.
  - `ctest --test-dir build --output-on-failure -j2` passed 68/68.
  - `python3 tools/data_pipeline.py --check` passed.
  - Targeted C coverage from `/tmp/runec_phase7_cov` passed:
    `tests/test_game_data_shared.c` 100%, `rc-core/game_data.c` 77%,
    `rc-core/world.c` 46%, `rc-core/encounter.c` 10%, touched-filter total
    33%. `encounter.c` is low at file level because the file also contains
    broad runtime mechanic execution paths outside this loader-boundary change;
    the new `rc_encounter_load_specs()` success and failure entry points are
    exercised by `test_game_data_shared`.
  - Backend workload benchmark passed:
    `timeout 120 bash tests/benchmarks/run_backend_workloads_benchmark.sh --mode spawn --ops 20`
    reported 258 active-area loads/sec.

## 2026-06-26 — Data Cleanup Phase 7 Shared Game Data API

- Change made: started Phase 7 by introducing a shared `RcGameData` lifecycle
  for static runtime gameplay data.
- Exact surfaces changed:
  - Extended `rc-core/game_data.h` and `rc-core/game_data.c` with opaque
    `RcGameData`, load reports, retained/released ownership, subsystem masks,
    and static table load stats.
  - Added `rc_world_create_with_data()` and `rc_world_get_game_data()` so
    multiple worlds can share one loaded data handle.
  - Attached `RcGameData` to `RcWorld` and moved low-risk static table loading
    for NPC defs, item defs, normalization, drops, prayers, and spells out of
    `rc_world_create_config()`.
  - Added `tests/test_game_data_shared.c` for shared data reuse across multiple
    worlds and subsystem-mask rejection.
- Why it was changed: Phase 7 needs a data container boundary before the
  existing process-global tables can be migrated behind immutable owned data
  storage one table family at a time.
- Deferred work:
  - This is the first Phase 7 increment. The listed tables are still backed by
    compatibility globals after load; subsequent Phase 7 chunks should move
    storage ownership into `RcGameData` and leave wrappers as read-only accessors.
- Validation:
  - `cmake --build build -j2` passed.
  - `ctest --test-dir build --output-on-failure -R test_game_data_shared`
    passed.
  - `ctest --test-dir build --output-on-failure -j2` passed 68/68.
  - `python3 tools/data_pipeline.py --check` passed.
  - Targeted C coverage from `/tmp/runec_phase7_cov` passed:
    `tests/test_game_data_shared.c` 100%, `rc-core/game_data.c` 77%,
    `rc-core/world.c` 47%, touched-target total 67%.
  - Backend workload benchmark passed:
    `timeout 120 bash tests/benchmarks/run_backend_workloads_benchmark.sh --mode spawn --ops 20`
    reported 251 active-area loads/sec on the final run.

## 2026-06-26 — Data Cleanup Gap Register

- Change made: moved data-cleanup follow-up tracking into a dedicated
  `data_cleanup_gaps.md` document.
- Exact surfaces changed:
  - Added `data_cleanup_gaps.md` with release-blocking source gaps, accepted
    limitations to revisit, post-Phase 7/8 cleanup checks, and update rules.
  - Removed the data-cleanup gap tracking added to `docs/work.md` and
    `docs/work_highlevel.md`.
  - Added a narrow `.gitignore` exception so `data_cleanup_gaps.md` can be
    tracked.
- Why it was changed: data-cleanup follow-ups should not be mixed into the
  general work-plan docs. They now have one dedicated human review checklist.
- Validation: documentation/config-only change; no tests run.

## 2026-06-26 — Data Cleanup Phase 6 Runtime Install Validation

- Change made: added manifest-driven runtime data install validation and made
  setup lock-aware for the draft data-v1 release state.
- Exact surfaces changed:
  - Added `rc-core/game_data.h` and `rc-core/game_data.c` with
    `rc_game_data_validate_install(const char *data_root,
    RcGameDataValidationReport *out)`.
  - Wired `rc_world_create_config()` to validate `data/manifest.json` before
    loading subsystem data. `RUNEC_VALIDATE_DATA_INSTALL=0` remains a narrow
    dev escape hatch.
  - Extended `tools/pack_runtime_data.py` manifests with data version,
    source-lock hash, source-authority policy hash, content-catalog hash,
    schema versions, required/optional logical paths, and loose checksums for
    required runtime paths.
  - Changed `tools/pack_runtime_data.py --force` to remove stale generated
    `.pak` files that are not in the current manifest pack list.
  - Reworked `scripts/setup-data.sh` to read `runtime-data.lock`, install from
    `--offline dist-data`, verify installed packs with `--verify`, and refuse
    default remote downloads while the lock is draft/non-official.
  - Updated `runtime-data.lock` to Phase 6 metadata while keeping
    `official_release=false` and
    `release_status=not_publishable_source_gaps`.
  - Added `tests/test_game_data_validation.c` for default install validation,
    missing required paths, missing manifest pack files, blocked provenance
    rejection, and explicit dev override handling.
  - Updated `README.md` and `data_pipeline.md` so setup instructions match the
    draft lock and offline local-install flow.
- Why it was changed: Phase 6 requires runtime installs to be validated from a
  manifest contract instead of silently treating missing files as disabled
  data. It also keeps the clean-clone/runtime-pack direction without making the
  repo depend on raw source corpora or an unofficial release pointer.
- Upstream/downstream impact:
  - Every normal world creation now fails fast when the runtime data manifest
    is missing, malformed, contains blocked provenance, or lacks required
    logical paths in the selected loose/pack backend.
  - Pack-only and loose-file loading still share the same manifest contract.
  - `./scripts/setup-data.sh` is no longer allowed to assume a GitHub release
    while `runtime-data.lock` is draft; use
    `./scripts/setup-data.sh --offline dist-data` for local generated packs.
  - `runtime-data.lock` remains non-official until Phase 5 source gaps are
    closed: `area_flags`, `combat_visuals`, `npc_spawns`, and `spells`.
- Validation:
  - `python3 tools/data_pipeline.py all` passed and ended with
    `report validation passed: release_status=not_publishable_source_gaps`.
  - `python3 tools/data_pipeline.py --check` passed.
  - `cmake --build build -j2` passed.
  - `ctest --test-dir build --output-on-failure -j2` passed 67/67.
  - `RUNEC_DATA_DIR=/tmp/runec-phase6-data RUNEC_DATA_UNPACK=0 ./scripts/setup-data.sh --offline dist-data`
    passed.
  - `RUNEC_DATA_DIR=/tmp/runec-phase6-data ./scripts/setup-data.sh --verify`
    passed.
  - `RUNEC_DATA_DIR=/tmp/runec-phase6-refuse ./scripts/setup-data.sh`
    failed as intended with the draft/non-official lock refusal.
  - Python coverage over the touched pipeline/packer/report paths reported:
    `tools/pack_runtime_data.py` 47%,
    `tools/data_pipeline.py` 47%,
    `tools/validate_reports.py` 71%, total 55%.
  - Backend workload benchmark passed:
    `timeout 120 bash tests/benchmarks/run_backend_workloads_benchmark.sh --mode spawn --ops 20`
    reported 299 active-area loads/sec.
- Deferred work:
  - Publishable runtime-data release fields still stay empty until the report
    gate no longer returns `not_publishable_source_gaps`.
  - A future release-publish step should fill `runtime-data.lock` release URLs
    and manifest checksum only after the source-authority gaps are resolved.

## 2026-06-26 — Data Cleanup Phase 5 Report Release Gates

- Change made: added generated-report release gates and data-v1 release
  summaries for Phase 5.
- Exact surfaces changed:
  - Added `tools/validate_reports.py` to validate required reports, allowed
    statuses, activity `BLOCKS_PARITY` counts, blocked runtime provenance,
    explicit source-gap rows, aggregate/subreport count consistency, and
    accepted limitations.
  - Wired `tools/data_pipeline.py validate-reports` and `--check` to run the
    new validator.
  - Changed `tools/data_pipeline.py generate-reports` to refresh
    `tools/reports/database_completion.txt` before copying reports into
    `generated/reports/current/reports/`.
  - Updated `tools/report_database_completion.py` so area flags are an
    explicit `SOURCE_GAP`, optional music/audio packs are not required for the
    v1 gameplay data gate, and aggregate counts come from current subreports.
  - Added `tools/validate_reports.py` to the policy-file allowlists in
    `tools/validate_sources.py` and `tools/validate_source_authority.py`.
  - Updated `data_pipeline.md` with the Phase 5 report-gate outputs and
    release-status rule.
  - Regenerated `tools/reports/database_completion.txt`; current aggregate
    counts now match subreports, including 8,178 object behavior rows and
    45,740 traversal edges.
- Why it was changed: Phase 5 requires reports to become release gates, not
  loose text artifacts. The pipeline can now say whether data-v1 is structurally
  validated and whether it is publishable.
- Upstream/downstream impact:
  - `generated/reports/report-validation.json`,
    `generated/reports/data-v1-summary.json`, and
    `generated/reports/data-v1-summary.txt` are produced by the pipeline.
  - The current report gate passes structurally but returns
    `release_status=not_publishable_source_gaps`; `runtime-data.lock` must not
    point to an official release until the source-authority gaps are resolved.
  - `data_pipeline.py --check` now validates generated reports with
    `tools/validate_reports.py --check-only`.
- Validation:
  - `python3 tools/data_pipeline.py all` passed and ended with
    `report validation passed: release_status=not_publishable_source_gaps`.
  - `python3 tools/data_pipeline.py --check` passed.
  - `python3 tools/validate_reports.py --report-root generated/reports --data-root data --dist-root dist-data --version v1`
    passed.
  - `python3 -m py_compile tools/validate_reports.py tools/data_pipeline.py tools/report_database_completion.py tools/validate_sources.py tools/validate_source_authority.py`
    passed.
  - `cmake --build build -j2` passed.
  - `ctest --test-dir build --output-on-failure -j2` passed 66/66.
  - Python coverage:
    - `tools/validate_reports.py`: 78% branch coverage path coverage.
    - `tools/data_pipeline.py`: 79% after covered `--check` and `all` runs.
    - `tools/report_database_completion.py`: 88%.
  - Backend workload benchmark passed:
    `timeout 120 bash tests/benchmarks/run_backend_workloads_benchmark.sh --mode spawn --ops 20`
    reported 282 active-area loads/sec.
- Deferred work:
  - Resolve the source-gap datasets listed in the generated summary:
    `area_flags`, `combat_visuals`, `npc_spawns`, and `spells`.
  - Do not mark data-v1 publishable or fill release URLs/checksums in
    `runtime-data.lock` until the report gate no longer returns
    `not_publishable_source_gaps`.

## 2026-06-26 — Data Cleanup Phase 4 Pipeline Command

- Change made: added the Phase 4 top-level data pipeline command and draft
  runtime-data lock format.
- Exact surfaces changed:
  - Added `tools/data_pipeline.py` with declared stages for repository
    validation, source authority validation, content/schema validation,
    content/cache/defs/render asset inventory, runtime packing, runtime
    unpack verification, report generation, and report validation.
  - Added per-stage machine-readable records under `generated/pipeline/stages/`
    and an aggregate `generated/pipeline/build.json` as generated outputs.
  - Wired the pipeline to write `data/manifest.json`,
    `dist-data/manifest.json`, and `dist-data/packs/` from the current local
    runtime data tree.
  - Added `runtime-data.lock` as a draft non-release lock format for Phase 5
    release-data validation.
  - Updated `data_pipeline.md` and `data/README.md` so normal data builds use
    `python3 tools/data_pipeline.py all`, while individual exporters remain
    lower-level maintainer/debug tools until explicitly wired into declared
    stages.
  - Updated `tools/validate_pipeline_inputs.py` output so it points to the new
    Phase 4 pipeline records instead of describing them as future work.
  - Updated `data-sources/sources.lock` for the new `data/README.md`
    checksum and Phase 4 label.
- Why it was changed: Phase 4 requires one explicit command for local data
  validation, packing, unpack verification, generated reports, and build
  records instead of manually running individual exporter/packer commands.
- Upstream/downstream impact:
  - The top-level pipeline does not reintroduce RuneLite, RSMod,
    osrsreboxed, data_osrs, wiki-cache, or local raw-cache checkout
    dependencies.
  - Export stages currently inventory existing generated outputs; full
    regeneration from maintainer-only cache/wiki inputs remains lower-level
    work until those inputs are explicitly declared and present.
  - `runtime-data.lock` intentionally has empty release URLs/checksums because
    Phase 5 owns official release gates.
- Validation:
  - `python3 tools/data_pipeline.py all` passed. It rebuilt 15 runtime packs
    containing 31,770 assets, wrote `data/manifest.json` and
    `dist-data/manifest.json`, unpack-verified the packs, collected reports,
    and wrote `generated/pipeline/build.json`.
  - `python3 tools/data_pipeline.py --check` passed.
  - `python3 -m py_compile tools/data_pipeline.py tools/validate_pipeline_inputs.py`
    passed.
  - `cmake --build build -j2` passed.
  - `ctest --test-dir build --output-on-failure -j2` passed 66/66.
  - `python3 -m trace --count --file /tmp/runec_data_pipeline_check.trace tools/data_pipeline.py --check`
    passed. The stdlib trace tool warned while trying to write coverage files
    beside read-only system libraries; the local `tools/data_pipeline.cover`
    byproduct was removed.
  - Backend workload benchmark passed:
    `timeout 120 bash tests/benchmarks/run_backend_workloads_benchmark.sh --mode spawn --ops 20`
    reported 307 active-area loads/sec.
  - C coverage was not run because `gcovr` is not installed in this
    environment; `coverage.py` is also unavailable, so Python coverage used the
    stdlib `trace` module.
- Deferred work:
  - Phase 5 should tighten generated report validation into release-blocking
    completeness gates and fill official runtime-data lock release fields only
    after those gates pass.

## 2026-06-26 — Data Cleanup Phase 3 Runtime Schemas

- Change made: added tracked runtime schema documentation and validation for
  Phase 3.
- Exact surfaces changed:
  - Added `schema/README.md`.
  - Added 32 binary table schemas under `schema/defs/*.schema.toml` for the
    Phase 3 runtime tables with binary magic, current version, accepted
    versions, header shape, producer, consumer, source dataset, and row fields.
  - Added `schema/defs/combat_visuals.schema.toml` as a TSV column contract
    because `combat_visuals` is not a binary table.
  - Added `schema/packs/runec_pack_v1.md` for `RCPK0002` pack headers and the
    `RCPI0001` index format.
  - Added `tools/validate_schemas.py` and wired it into
    `tools/validate_pipeline_inputs.py`.
  - Promoted the `schemas` row in `data-sources/sources.lock` from planned to
    required tracked input with schema tree checksum and file count.
  - Updated `.gitignore` so schema Markdown docs are trackable.
  - Replaced duplicated format comments/docstrings in the NDEF loader and
    Phase 3 exporters with short schema references.
  - Added `schema` to the source-lock scan surface in
    `tools/validate_sources.py`.
- Why it was changed: Phase 3 requires runtime binary/table formats and pack
  formats to be explicit tracked contracts, not implicit knowledge split across
  exporters and loaders.
- Upstream/downstream impact:
  - Phase 4 pipeline work can validate required schema files before building
    manifests or packs.
  - Existing runtime binaries remain compatible; no binary header layout
    changes were needed because the listed binary tables already emitted
    explicit magic/version headers and loaders already rejected unsupported
    versions.
  - `combat_visuals` remains TSV by design and is documented as a column
    contract rather than forced into a fake binary schema.
- Validation:
  - `python3 tools/validate_schemas.py` passed with 32 binary schemas, 1 TSV
    schema, and 1 pack doc.
  - `python3 tools/validate_sources.py` passed. It reported optional missing
    maintainer inputs because `RUNEC_B237_CACHE`, `RUNEC_B237_DUMP`, and
    `RUNEC_B237_KEYS` are unset in this environment.
  - `python3 tools/validate_pipeline_inputs.py --strict-external-sources`
    passed.
  - `python3 tools/validate_no_external_repos.py --strict` passed.
  - `python3 tools/validate_content_layout.py --strict-no-legacy` passed with
    854 content files and 0 mirrored legacy files.
  - `python3 tools/validate_source_authority.py --write-gap-report` passed.
  - `python3 -m py_compile` passed for the edited Python tools.
  - `cmake --build build -j2` passed.
  - `ctest --test-dir build --output-on-failure -j2` passed 66/66.
  - Python trace coverage for `tools/validate_schemas.py` covered success and
    temporary negative-path cases with 142/142 executable lines hit.
  - C coverage was not run because `gcovr` is not installed in this
    environment; Python coverage used the stdlib `trace` module because
    `coverage.py` is also unavailable.
  - Backend workload benchmark passed:
    `timeout 120 bash tests/benchmarks/run_backend_workloads_benchmark.sh --mode spawn --ops 20`
    reported 286 active-area loads/sec.
- Deferred work:
  - Phase 4 should build the single data-pipeline command and per-stage
    manifests against the tracked schema/source contracts.

## 2026-06-25 — Data Cleanup Phase 2 Source Locking

- Change made: added Phase 2 source locking and removed remaining raw source
  corpora from the repo root.
- Exact surfaces changed:
  - Added `data-sources/README.md`, `data-sources/sources.lock`, and
    `data-sources/source_gaps.json`.
  - Added `tools/validate_sources.py` and wired it into
    `tools/validate_pipeline_inputs.py`.
  - Added `data/README.md` as the tracked migration note for the ignored local
    runtime-data install.
  - Changed `tools/source_paths.py` so b237 cache inputs come from
    `RUNEC_B237_CACHE` / `RUNEC_B237_KEYS` instead of a repo-local cache tree.
  - Changed cache-backed exporters and cache-pipeline tools to require explicit
    b237 cache/dump inputs via CLI args or `RUNEC_B237_CACHE` /
    `RUNEC_B237_DUMP`.
  - Removed runtime-facing fallback paths in `rc-viewer/viewer.c` for scene
    auto-export and world-map minimap assets.
  - Rewrote `tools/cache_pipeline/source/README.txt` as a deprecated-source
    migration note.
  - Deleted ignored local raw corpora: `tools/wiki_cache` and
    `tools/cache_pipeline/source/current_fightcaves_demo`; the empty
    `data/source` directory was removed.
  - Updated `.gitignore` so `data/README.md` and `data-sources/README.md` are
    tracked while generated runtime data remains ignored.
- Why it was changed: Phase 2 requires approved RuneC-owned inputs to be
  lockable and external/raw corpora to be explicit maintainer inputs outside
  the repo, not hidden defaults under `data/source` or
  `tools/cache_pipeline/source`.
- Upstream/downstream impact:
  - Normal build/test/runtime paths still use existing generated runtime data
    from `data/`.
  - Maintainer rebuild tools now require explicit cache/dump paths; running
    them without `--cache` or the relevant `RUNEC_*` environment variable fails
    with a clear message.
  - Legacy wiki/external-reference helpers remain research/deferred tools, but
    the local wiki cache and raw b237 cache mirror are no longer present under
    the repo root.
- Validation:
  - `python3 tools/validate_sources.py` passed. It reported optional missing
    maintainer inputs because `RUNEC_B237_CACHE`, `RUNEC_B237_DUMP`, and
    `RUNEC_B237_KEYS` are unset in this environment.
  - `python3 tools/validate_pipeline_inputs.py --strict-external-sources`
    passed.
  - `python3 tools/validate_no_external_repos.py --strict` passed.
  - `python3 tools/validate_content_layout.py --strict-no-legacy` passed with
    854 content files and 0 mirrored legacy files.
  - `python3 tools/validate_source_authority.py --write-gap-report` passed.
  - `python3 -m py_compile` passed for the edited Python tools.
  - CLI help checks passed for the edited cache-pipeline tools.
  - `cmake --build build -j2` passed.
  - `ctest --test-dir build --output-on-failure -j2` passed 66/66.
  - Python trace coverage for `tools/validate_sources.py` reported 100% line
    execution on the success path.
  - C coverage was not run because `gcovr` is not installed in this
    environment.
  - Backend workload benchmark passed:
    `timeout 120 bash tests/benchmarks/run_backend_workloads_benchmark.sh --mode spawn --ops 20`
    reported 290 active-area loads/sec.
- Deferred work:
  - Per-stage data pipeline manifests are still Phase 4 work.
  - Phase 3 should add tracked runtime binary schema documents under `schema/`.

## 2026-06-25 — Data Cleanup Phase 1C Strict Self-Containment Completion

- Change made: completed the strict Phase 1C external-source boundary so the
  self-containment guard now passes in strict mode.
- Exact surfaces changed:
  - Removed external checkout/dump roots from `tools/source_paths.py`.
  - Added `tools/legacy_external_source_paths.py` for old research/deferred
    helpers that still inspect `data_osrs`, `osrsreboxed`, or decoded dump
    layouts outside the official pipeline.
  - Retargeted legacy/audit/export helpers that still need deferred migration
    to import external roots from `legacy_external_source_paths.py`, not the
    normal source path module.
  - Updated `tools/validate_no_external_repos.py` so strict mode fails on
    active external-source references and on present local mirrors for
    `data/source/data_osrs`, `data/source/osrsreboxed-db`, and
    `tools/cache_pipeline/source/osrs-dumps`.
  - Removed hardcoded optional `osrs-dumps` defaults from
    `tools/cache_pipeline/export_item_render_models.py` and
    `tools/cache_pipeline/export_sprites_modern.py`; both now require explicit
    optional paths for those research aliases.
  - Changed tracked activity-spawn source notes from local wiki/cache/external
    checkout paths to OSRS Wiki/reference/source-gap labels.
  - Deleted the remaining ignored local mirrors:
    `data/source/data_osrs`, `data/source/osrsreboxed-db`, and
    `tools/cache_pipeline/source/osrs-dumps`.
- Why it was changed: Phase 1C requires official RuneC build/run/generation to
  stop depending on mirrored external source trees under the repo. Deferred
  replacement work remains possible as research, but it is no longer exposed as
  the normal pipeline path.
- Validation:
  - `python3 tools/validate_no_external_repos.py --strict` passed.
  - `python3 tools/validate_pipeline_inputs.py --strict-external-sources`
    passed.
  - `python3 tools/export_activity_spawns.py` passed and exported 117 activity
    spawn rows.
  - `python3 tools/validate_source_authority.py --write-gap-report` passed.
  - `python3 tools/validate_content_layout.py --strict-no-legacy` passed with
    854 content files and 0 mirrored legacy files.
  - `python3 -m py_compile` passed for the edited Python tools.
  - `cmake --build build -j2` passed.
  - `ctest --test-dir build --output-on-failure -j2` passed 66/66.
  - Backend workload benchmark passed:
    `timeout 120 bash tests/benchmarks/run_backend_workloads_benchmark.sh --mode spawn --ops 20`
    reported 303 active-area loads/sec.
- Coverage note: no C coverage command was run in this pass; `gcovr` is not
  installed in the current environment.

## 2026-06-25 — Data Cleanup Phase 1B/1C Source Authority And Self-Containment

- Change made: removed the active private-server/wrong-game source dependency
  surfaces and replaced the remaining RuneLite/RSMod hard checkout paths with
  RuneC-owned content inputs or disabled optional fallbacks.
- Exact surfaces changed:
  - Promoted `data/defs/combat_visuals.tsv` into
    `content/combat_visuals/visuals.tsv` and added
    `content/combat_visuals/README.md`.
  - Replaced `tools/export_combat_visuals.py` with a content-backed
    validator/copier that no longer reads RSMod, `RUNEC_RSMOD_ROOT`, or
    `tools/cache_pipeline/source/rsmod`.
  - Replaced `tools/export_item_effects.py` with a validator for
    `content/items/effects.toml`; the table now uses
    `runec_reviewed_itemstats` for imported stat-change rows and fixed
    migrated special paths to `content/specials`.
  - Removed the central `RUNELITE` path from `tools/source_paths.py` and
    removed optional RuneLite name/id fallback reads from
    `tools/export_npc_defs_full.py` and `tools/audit_npc_reconciliation.py`.
  - Changed `tools/cache_pipeline/export_item_render_models.py` so RSMod BAS
    data is no longer read from a default local checkout or environment root.
  - Added `tools/validate_no_external_repos.py` and
    `tools/validate_pipeline_inputs.py` for Phase 1C guard coverage.
  - Expanded `tools/validate_source_authority.py` to scan TOML and TSV content.
  - Removed blocked source-path provenance from `content/activity_spawns.toml`
    and retagged the affected Nex/Sol rows as source-gap provisional fixtures.
  - Deleted local blocked mirrors `data/source/void_rsps` and
    `data/source/near_reality`.
  - Updated `AGENT.md`, component READMEs, and `docs/work.md` so `data/` is an
    ignored local runtime install populated by setup/rebuild tooling, not a
    required nested RuneC-DB checkout.
  - Added `combat_visuals` to `content/catalog.toml` and corrected the
    `item_effects` catalog output/consumer description.
- Why it was changed: Phase 1B requires blocked private-server/wrong-game
  inputs to be removed before they are normalized by the new layout. Phase 1C
  additionally prevents full external repo checkouts such as RuneLite and RSMod
  from becoming build/export/runtime dependencies.
- Upstream/downstream impact:
  - Official combat visual and item-effect generation now uses RuneC-owned
    content tables instead of probing external checkouts.
  - `tools/validate_no_external_repos.py` fails on hard checkout assumptions
    and reports data_osrs, osrsreboxed, wiki-cache, and osrs-dumps reads as
    deferred migration work. `--strict` intentionally fails until those
    datasets are replaced one by one.
  - Remaining generated data under `data/` continues to be local runtime state;
    the deleted blocked mirrors are no longer available as accidental exporter
    inputs.
- Validation:
  - `python3 tools/export_combat_visuals.py` passed and wrote 3,532 rows from
    `content/combat_visuals/visuals.tsv`; it reported 0 failures and 7 existing
    duplicate-key warnings from the accepted TSV.
  - `python3 tools/export_item_effects.py` passed with 361 content rows and 997
    item-id references.
  - `python3 tools/validate_source_authority.py` passed with TOML/TSV scanning.
  - `python3 tools/validate_no_external_repos.py` passed with deferred
    external-source reads reported as non-fatal.
  - `python3 tools/validate_pipeline_inputs.py` passed; strict stage manifests
    remain Phase 2/4 work.
  - `python3 tools/validate_content_layout.py --strict-no-legacy` passed with
    854 content files and 0 legacy files.
  - `python3 -m py_compile` passed for the changed Phase 1B/1C tools.
  - `cmake --build build -j2` passed.
  - `ctest --test-dir build --output-on-failure -j2` passed 66/66.
  - Python trace coverage ran for `tools/export_item_effects.py`,
    `tools/export_combat_visuals.py`, and `tools/validate_pipeline_inputs.py`.
    The normal success paths executed; failure branches remain covered by
    explicit validation inputs rather than the happy-path run. A direct traced
    run of `tools/validate_no_external_repos.py` was stopped because tree-wide
    trace overhead was too slow, but the normal guard run passed.
  - Backend workload benchmark passed:
    `timeout 120 bash tests/benchmarks/run_backend_workloads_benchmark.sh --mode spawn --ops 20`
    reported 294 active-area loads/sec.
- Deferred verification at this checkpoint:
  - Full strict self-containment was not complete yet: data_osrs, osrsreboxed,
    tools/wiki_cache, and osrs-dumps reads were reported by
    `tools/validate_no_external_repos.py --strict`.
  - This was completed by the later 2026-06-25 Phase 1C strict
    self-containment entry above.

## 2026-06-25 — Data Cleanup Phase 1 Content Source Directory

- Change made: introduced tracked `content/` as the authored-content source of
  truth and moved the former `data/curated/` authored datasets into it.
- Exact surfaces changed:
  - Added `content/README.md` with authored-content ownership rules.
  - Added `content/catalog.toml` with dataset records for encounters,
    mechanics, quests, dialogue, item effects, item specials, activity spawns,
    activity state machines, regular NPC special mechanics, and mechanics
    owners.
  - Copied 850 authored TOML files from `data/curated/` into `content/`.
  - Added `tools/content_paths.py` so exporters prefer `content/` and
    temporarily fall back to `data/curated/` with a warning if the tracked path
    is missing.
  - Added `tools/validate_content_layout.py` to fail legacy-only or divergent
    files under `data/curated/`. Its `--strict-no-legacy` mode now enforces
    the completed cleanup step.
  - Updated `.gitignore` so `content/README.md` is not hidden by the global
    Markdown ignore rule.
  - Updated exporter/audit inputs and authoring outputs across
    `tools/export_encounters.py`, `tools/export_activity_*.py`,
    `tools/export_regular_npc_mechanics.py`, `tools/export_dialogue.py`,
    `tools/export_item_effects.py`, `tools/export_spawn_sources.py`,
    `tools/export_npc_defs_full.py`, `tools/audit_*`, `tools/extract_*`, and
    `tools/scrape_item_specials.py`.
  - Updated `rc-core/config.c` so the loose encounter TOML default is
    `content/encounters`; `data/defs/encounters.bin` remains the preferred
    runtime artifact.
  - Updated active docs/comments in `rc-core/encounter.h` and
    `rc-content/encounters/*`.
  - Extended `tools/validate_source_authority.py` to scan `content/`.
  - Updated `tests/test_prayer_spell_actions_runtime.c` to reflect the prior
    source-authority cleanup: combat spell max-hit/effect data is now a source
    gap, so Fire Blast has max-hit 0 and does not enqueue a magic hit.
- Why it was changed: authored content must be tracked and reviewable in the
  main repo, while ignored `data/` becomes generated/runtime install state.
  This also keeps the architecture roadmap moving toward immutable generated
  runtime data and a clean source/runtime boundary.
- Upstream/downstream impact:
  - Exporters and audits now read tracked authored TOML by default.
  - Authoring/extraction tools now emit new curated TOML into `content/`.
  - The local `data/curated/` tree was removed after exporter/test validation.
    Compatibility fallback code remains for older checkouts, but this checkout
    no longer carries authored content under ignored `data/`.
  - `tools/validate_content_layout.py --strict-no-legacy` is now the guard for
    keeping authored content out of `data/curated/`.
  - Regenerated runtime binaries remain under ignored `data/defs` and
    `data/spawns`.
- Validation:
  - `python3 -m py_compile` passed for the updated content path helper,
    layout guard, validators, exporters, audits, and extractors.
  - `python3 tools/validate_source_authority.py` passed with `content/`
    included in the scan.
  - `python3 tools/validate_content_layout.py` passed before cleanup: 852
    tracked content files and 850 mirrored legacy files.
  - After removing `data/curated/`, `python3 tools/validate_content_layout.py
    --strict-no-legacy` passed with 852 content files and 0 legacy files.
  - Focused exporters passed from `content/`: `export_encounters.py`,
    `export_activity_spawns.py`, `export_activity_states.py`,
    `export_activity_mechanics.py`, `export_activity_schemas.py`,
    `export_regular_npc_mechanics.py`, `export_dialogue.py`, and
    `export_spawn_sources.py`.
  - `python3 tools/audit_mechanics_coverage.py` passed.
  - `cmake --build build -j2` passed.
  - `ctest --test-dir build --output-on-failure -j2` passed 66/66 before and
    after removing `data/curated/`.
  - Coverage build in `build_cov` ran focused tests for spell/prayer actions,
    encounter bins, activity mechanics/schemas/spawns/states, quests/dialogue,
    regular NPC mechanics, and spawn sources. `gcov` summaries: `rc-core/config.c`
    50.00% lines, `rc-core/spells.c` 83.08% lines, and
    `tests/test_prayer_spell_actions_runtime.c` 100.00% lines.
  - Backend workload benchmark guard passed:
    `timeout 120 bash tests/benchmarks/run_backend_workloads_benchmark.sh --mode spawn --ops 20`
    reported 292 active-area loads/sec.
- Deferred verification:
  - `python3 tools/export_item_effects.py` is blocked in this checkout because
    `data/source/runelite` is absent.
  - `python3 tools/audit_npc_reconciliation.py` is blocked by a short read from
    the current local `data/defs/npc_defs.bin` artifact.

## 2026-06-25 — Data Cleanup Phase 1A Source Authority Guard

- Change made: added a source-authority guard and removed active
  private-server/wrong-game source defaults from the data-export path.
- Exact surfaces changed:
  - Added `tools/validate_source_authority.py` to scan active source,
    exporter, script, test, and README paths for blocked source-authority
    references. It can also generate `generated/reports/source_gaps.txt`
    and `generated/reports/source_gaps.json`.
  - Added `generated/` to `.gitignore`; the existing local
    `data_cleanup.md` unignore remains in place.
  - Disabled legacy `tools/export_npcs.py` as a failing compatibility entry
    point. Current NPC export paths are `tools/export_npc_defs_full.py`,
    `tools/export_npc_models_full.py`, and `tools/export_spawns.py`.
  - Removed blocked source path constants from `tools/source_paths.py`.
  - Removed blocked spell combat-effect input from `tools/export_spells.py`;
    spells now export wiki-derived metadata/rune costs and record combat
    max-hit/effect behavior as a source gap.
  - Replaced `tools/export_area_flags.py` with a gap-report generator that
    does not regenerate `data/defs/area_flags.bin` until approved OSRS-native
    area authority exists.
  - Replaced blocked combat-visual provenance tags in
    `tools/export_combat_visuals.py` with explicit `gap:` authority tags for
    the affected custom NPC projectile rows.
  - Updated tracked source reports for spells, area flags, area source notes,
    and database completion so they no longer present removed sources as
    accepted authority.
  - Updated `tools/report_database_completion.py` so an explicit
    `SOURCE_GAP` dataset is reported as open source-authority work rather than
    accepted closure.
  - Updated active README/comment guidance in `README.md`, `rc-content/*`,
    `tools/database_sources.py`, `tools/export_spawns.py`,
    `tools/patch_npc_defs_wiki.py`, and
    `tools/report_database_completion.py`.
- Why it was changed: `data_cleanup.md` now requires approved OSRS-native
  authority only: b237 cache/dumps, RuneLite, RSMod, OSRS Wiki where
  appropriate, or reviewed authored content. Missing facts should be explicit
  source gaps rather than silently preserved private-source or wrong-game
  dependencies.
- Upstream/downstream impact:
  - Future exporter runs can no longer regenerate area flags from the removed
    default source path.
  - Existing ignored runtime binaries are left untouched for current local
    validation; this pass changes exporter authority, not runtime loading.
  - Combat spell max-hit/effect hints are now absent unless later backed by
    approved sources.
  - Six custom combat-visual NPC rows remain usable but are marked as
    authority gaps until reauthored from approved evidence.
- Generated outputs:
  - `generated/reports/source_gaps.txt`
  - `generated/reports/source_gaps.json`
  - `generated/reports/area_flags_gaps.txt`
  - `generated/reports/area_flags_gaps.json`
- Validation:
  - `python3 -m py_compile tools/validate_source_authority.py
    tools/export_area_flags.py tools/export_spells.py tools/export_npcs.py
    tools/export_spawns.py tools/database_sources.py
    tools/patch_npc_defs_wiki.py tools/report_database_completion.py
    tools/export_combat_visuals.py tools/source_paths.py` passed.
  - `python3 tools/validate_source_authority.py --write-gap-report` passed
    and wrote the generated source-gap reports.
  - `python3 tools/export_area_flags.py` passed and wrote the generated area
    gap reports without touching `data/defs/area_flags.bin`.
  - `python3 tools/export_spells.py` passed and regenerated local ignored
    `data/defs/spells.bin` / `data/defs/teleports.bin` plus
    `tools/reports/spells.txt` with zero combat max-hit matches.
  - `python3 tools/export_npcs.py` exited with code 2 and the intended
    disabled-exporter message.
  - `python3 tools/report_database_completion.py` now gets past the area flag
    `SOURCE_GAP` status but cannot finish in this checkout because
    `data/defs/music.bin` is missing.
- Deferred verification: no C tests, coverage, or benchmarks were run because
  this pass changes exporter/source-authority policy and generated reports,
  not runtime simulation behavior. Runtime tests still rely on the existing
  ignored local `data/defs/area_flags.bin` until the area flag source gap is
  closed. Full database-completion regeneration remains blocked by the missing
  local music binary noted above.

## 2026-06-24 — Viewer Scene Cache Prewarm Phase 2 Source Sweep

- Change made: added explicit scene-cache prewarm tooling for generated
  viewer windows and scalable object transport destination sweeps.
- Exact surfaces changed:
  - Added `tools/cache_pipeline/prewarm_scene_cache.py`.
  - The tool can prewarm explicit centers with `--center x,y`.
  - The tool can read `data/defs/object_transports.bin` and prewarm generated
    destination scene windows with `--object-transport-destinations`.
  - Transport prewarm can be scoped to a source scene with
    `--transport-source-center x,y`, use a separate source radius through
    `--transport-source-radius-regions`, constrain bounds/plane filters,
    preview with `--dry-run`, and limit work with `--max-scenes`.
  - Transport destination prewarm can use radius-0 landing windows so the
    viewer has broad post-transport render coverage without requiring large
    radius-2 exports for every possible destination.
  - The tool dedupes destinations by generated scene-window key, skips current
    cache outputs, and writes `.scene.json` sidecar metadata after exports.
  - `rc-viewer/README.md` now documents generated scene prewarm examples.
- Local cache work performed: prewarmed the generated Varrock radius-2 scene
  for planes 1, 2, and 3 under `data/regions/scene_cache`. Also ran a
  source-window transport sweep for the Varrock radius-2 source scene:
  `--object-transport-destinations --transport-source-center 3184,3440 --transport-source-radius-regions 2 --radius-regions 0`.
  That sweep matched 1,793 object transport rows into 111 unique destination
  scene windows, exported 97 compact landing windows, skipped 8 current
  windows, and failed 6 windows because the local cache source lacked those
  map regions. These generated data files are ignored by the main repo.
- Why it was made: normal object transports can move core state to a new
  region/plane, but the viewer can only render the destination if a generated
  scene cache exists or runtime auto-export is explicitly enabled. The previous
  named-destination smoke set was useful for diagnosis but was not scalable;
  the correct workflow is to sweep object transports from the source scene.
- Runtime impact: none unless the new prewarm tool is run. Viewer launches
  still load existing cache files and avoid runtime exports by default. The
  viewer can fall back from a missing radius-2 destination to an existing
  radius-0 landing cache.
- Validation:
  - `python3 -m py_compile tools/cache_pipeline/prewarm_scene_cache.py`
    passed.
  - Explicit Varrock prewarm dry-run skipped current plane 0 cache.
  - Object transport destination dry-run from the Varrock radius-2 source
    scene matched 1,793 transport rows into 111 unique destination windows.
  - Full object transport destination dry-run matched 29,928 transport rows
    into 807 unique destination windows.
  - Generated Varrock all-plane prewarm completed in about 23 seconds.
  - Varrock source-window transport destination sweep completed in about
    412 seconds with `97` exports, `8` skips, and `6` missing-source failures.
  - `RUNEC_SCENE_PLANE=1 RC_VIEWER_EXIT_FRAMES=1 ./build/rc-viewer` now loaded
    generated `scene_3008_3264_r2.p1` assets directly.
  - Direct generated-start smokes at `3237,9858`, `3190,9833`, `3077,9771`,
    and `1859,5243` loaded generated terrain, objects, and active-area NPCs.

## 2026-06-24 — Viewer Generated Scene Startup Phase 1

- Change made: made generated scene startup a first-class `rc-viewer` mode
  while preserving fixed-slice compatibility for validation bundles.
- Exact surfaces changed:
  - `rc-viewer/viewer.c` now supports
    `RUNEC_SCENE_MODE=auto|generated|fixed`. Unset mode defaults to auto
    unless `RUNEC_TERRAIN` or `RUNEC_OBJECTS` is explicitly set, in which
    case fixed mode is selected for backward compatibility.
  - Auto mode uses a generated scene only when the matching cache window is
    already present/current; otherwise it falls back to the fixed scene without
    launching a blocking scene export.
  - `RUNEC_SCENE_AUTO_EXPORT` now defaults to off. Explicit generated mode
    still supports startup export, but only when `RUNEC_SCENE_AUTO_EXPORT=1`
    is set.
  - Generated scene selection now reuses existing smaller cached windows when
    the requested radius is missing/stale. This keeps existing radius-1
    validation caches usable after the default generated radius became 2.
  - Active generated scenes now remember the radius actually loaded, so
    follow-up plane loads/export messages use the correct cache key.
  - If a generated Varrock plane is missing but the fixed Varrock multi-plane
    slice exists, plane selection falls back to the fixed scene instead of
    leaving only actor models visible.
  - Generated startup now derives its scene window from
    `RUNEC_PLAYER_START_X/Y` and `RUNEC_SCENE_RADIUS_REGIONS`, defaulting the
    generated radius to 2 regions.
  - Runtime data checks no longer require fixed Varrock terrain/object files
    when generated scene mode is active.
  - Missing or stale generated scene files now report the exact terrain,
    object, and atlas paths plus the `export_scene_slice.py` command needed
    to prewarm/export the scene.
  - Viewer-only Varrock combat dummy validation now spawns only when the
    loaded scene contains the Varrock bank tile, avoiding off-window dummy
    state during non-Varrock generated startup.
  - `tools/frontend_validation/build_asset_bundle.py` now emits
    `RUNEC_SCENE_MODE=fixed` by default so existing validation bundles keep
    loading their staged fixed scene files.
  - `rc-viewer/README.md` documents generated/fixed scene startup and the
    generated cache controls.
- Why it was made: the viewer was still booting through fixed Varrock files
  before generated scene streaming could take over, which made broader world
  rendering depend on a slice-shaped startup path.
- Runtime impact: normal dev launches can start directly in a cache-backed
  generated scene around the player when cache files are present, and otherwise
  fall back to the fixed scene so viewer startup stays interactive. Fixed
  validation slices still work via explicit mode/env settings. `rc-core`
  remains headless and unchanged.
- Known gaps: Phase 2 still needs dedicated scene prewarm tooling and
  manifest-driven batch generation so normal launches do not rely on
  startup-time auto-export.
- Validation:
  - `python3 -m py_compile tools/frontend_validation/build_asset_bundle.py`
    passed.
  - `cmake --build build -j2` passed.
  - `ctest --test-dir build --output-on-failure -j2` passed 66/66.
  - Default viewer startup smoke with no scene env vars passed:
    `RC_VIEWER_EXIT_FRAMES=1 ./build/rc-viewer`; it selected generated cache
    when the local cache was present and did not export.
  - Empty generated-cache startup smoke passed:
    `RUNEC_SCENE_CACHE_DIR=/tmp/runec_empty_scene_cache RC_VIEWER_EXIT_FRAMES=1 ./build/rc-viewer`;
    it fell back to fixed scene startup without exporting.
  - Explicit generated missing-cache smoke exited fast with the prewarm
    command:
    `RUNEC_SCENE_MODE=generated RUNEC_SCENE_CACHE_DIR=/tmp/runec_empty_scene_cache RUNEC_SCENE_AUTO_EXPORT=0 RC_VIEWER_EXIT_FRAMES=1 ./build/rc-viewer`.
  - Validation transport smokes passed and loaded scene meshes by falling back
    to existing radius-1 caches where needed:
    `RUNEC_DEV_TRANSPORT_DEST=jad|graardor|kbd|vorkath RC_VIEWER_EXIT_FRAMES=1 ./build/rc-viewer`.
  - Plane fallback smoke passed:
    `RUNEC_SCENE_PLANE=1 RC_VIEWER_EXIT_FRAMES=1 ./build/rc-viewer`;
    it fell back to fixed Varrock plane assets instead of actor-only rendering.
  - Fixed viewer startup smoke passed:
    `RUNEC_SCENE_MODE=fixed RC_VIEWER_EXIT_FRAMES=1 ./build/rc-viewer`.
  - Generated viewer startup smoke passed against a pre-exported one-region
    cache window:
    `RUNEC_SCENE_MODE=generated RUNEC_PLAYER_START_X=2480 RUNEC_PLAYER_START_Y=3500 RUNEC_SCENE_RADIUS_REGIONS=0 RUNEC_SCENE_AUTO_EXPORT=0 RC_VIEWER_EXIT_FRAMES=1 ./build/rc-viewer`.
  - Missing/stale generated-scene failure messaging was checked with
    auto-export disabled; it printed the stale object path and exact
    `export_scene_slice.py` command.
  - Temporary coverage build in `/tmp/runec_viewer_scene_fallback_cov` ran
    default generated startup, selected-plane fallback, and Jad/Graardor/KBD/
    Vorkath validation transport smokes; `gcov` reported `rc-viewer/viewer.c`
    at 48.84% line coverage, 48.75% branch execution, and 30.19% branches
    taken.
  - Active-area benchmark guard passed:
    `timeout 240 bash tests/benchmarks/run_backend_workloads_benchmark.sh --mode spawn --ops 20`
    reported 296 active-area loads/sec. No full SPS before/after comparison
    was run because this phase changes viewer startup/data-loading behavior,
    not the backend tick loop.

## 2026-06-24 — rc-core / rc-viewer Presentation Boundary Cleanup

- Change made: moved combat presentation data and render metadata out of
  `rc-core` and into `rc-viewer`, while keeping core combat headless through
  logical attack events and backend hit-delay profiles.
- Exact surfaces changed:
  - `rc-core/combat_visuals.*` moved to `rc-viewer/combat_visuals.*`; core now
    has `rc-core/combat_profiles.*` for backend-only style/key/hit-delay rows.
  - `RcWorldConfig.combat_visuals_path` became `combat_profiles_path`; the
    viewer still loads full combat visuals through `RUNEC_COMBAT_VISUALS`.
  - `rc-core/types.h` no longer stores combat projectiles, combat visual
    events, item render model IDs, NPC render animation/model IDs, or combat
    attack animation timers/IDs. Core combat emits `RcCombatAttackEvent`
    records with actor IDs, styles, positions, action keys, and hit delay.
  - `rc-viewer/viewer.c` now owns combat projectile instances and viewer-side
    combat attack animation timers derived from core attack events.
  - Added `rc-viewer/item_render_defs.*` and `rc-viewer/npc_render_defs.*` so
    viewer-owned fallback render metadata is loaded outside core.
  - Added `rc-viewer/object_action_visuals.*`; core object behavior now keeps
    gameplay flags/transforms only and skips legacy action animation/audio
    columns instead of exposing them.
  - `rc-core/items.c` and `rc-core/npc.c` still read/skip render bytes from
    current generated binaries for compatibility but do not expose them.
  - `tests/test_items_bin.c`, `tests/test_npc_defs_bin.c`, and
    `tests/test_combat_visuals_projectiles.c` now validate the core/viewer
    split instead of expecting render metadata in core structs.
  - Added `rc-core/AGENT.md` with explicit headless/backend ownership rules.
- Why it was made: `rc-core` is strictly the backend simulation and must stay
  fast, headless, and independent of frontend/graphics/UI/rendering concerns.
- Runtime impact: core combat behavior and hit timing remain data-backed;
  projectile rendering, combat attack animation playback, object action
  animation playback, item render fallback metadata, and NPC render animation
  metadata are now viewer-owned. Existing generated binary formats remain
  readable.

## 2026-06-18 — Local Docs Layout And Agent Guide

- Change made: centralized local-only repo documentation under `docs/`, kept
  `docs/` ignored, and added root `AGENT.md` as the committed always-read
  coding-agent guide.
- Exact surfaces changed:
  - `.gitignore` now ignores `docs/` and arbitrary Markdown by default while
    explicitly allowing root `AGENT.md`, root `README.md`, and component
    READMEs to remain tracked.
  - `AGENT.md` now carries the repo execution rules, ownership boundaries,
    validation expectations, data-boundary notes, and commit/history discipline
    where agents can discover it from the repo root.
  - Component README/comment/docstring references now point at local
    `docs/work.md` and `docs/encounter_primitives.md` instead of stale
    root/data Markdown paths.
  - `tools/pack_runtime_data.py` now records relocated local docs in manifest
    metadata and explicit-exclude bookkeeping.
- Why it was made: the repo had scattered local Markdown and stale doc paths,
  while coding agents still need a root instruction file. This keeps long-form
  docs local/ignored without losing the root agent entry point.
- Runtime impact: none. No runtime data, packed assets, cache source inputs,
  rendering assets, or game behavior changed.

## 2026-05-20 — Encounter Participation And Tile-Target Visual Foundation

- Change made: broadened the Step 2.1 encounter validation foundation so
  selected encounter attacks can distinguish actor-targeted projectiles from
  tile-targeted/AoE projectiles, Jad validation placement can come from the
  activity spawn data path, and spawned encounter helpers are prepared for
  participation instead of appearing as wandering/inert visual leftovers. The
  viewer now also renders active encounter effects as their own scene pass,
  separate from projectiles, and projectile instances retain the exact
  cache/exporter timing profile fields used to calculate travel timing.
- Exact surfaces changed:
  - `rc-core/encounter.h` and `tools/export_encounters.py` now define and
    export `RC_ENC_ATTACK_TARGETS_TILE` for encounter attacks that target a
    world tile rather than an entity. Vorkath's Deadly Dragonfire is covered
    by the generated flag.
  - `rc-core/combat.c` now passes selected encounter attack flags into NPC
    projectile spawning. Actor-targeted attacks keep a player actor target,
    while tile-targeted attacks snapshot the player tile and emit a fixed
    `RC_COMBAT_ACTOR_NONE` projectile target. This matches the RuneLite API
    distinction between a projectile target actor and a nullable actor with a
    target point for AoE/tile attacks.
  - `rc-core/activity_spawns.c` / `rc-core/activity_spawns.h` now expose
    `rc_activity_spawn_wave_ref()` so tests and validation code can inspect
    the wave-owned spawn reference row before resolving the region.
  - `tools/export_activity_spawns.py` now preserves a single-activity NPC id
    on point, dynamic, wave-point, and wave-region reference rows when the
    activity has exactly one NPC id. Fight Cave wave references now carry the
    Jad NPC id instead of only the text entity name.
  - `rc-viewer/dev_validation.c` now resolves the Jad validation NPC through
    the Fight Cave wave 63 activity spawn region, defaulting to rotation 6 and
    allowing `RUNEC_DEV_JAD_ROTATION` to select a different generated wave
    region. The fallback static row remains only as dev-validation backup.
  - `rc-core/encounter_prims.c` now prepares NPCs spawned by encounter
    primitives with stable previous/spawn positions, disabled wandering, and
    optional delayed player participation for helper roles that block boss
    damage, heal on contact, or freeze the player. `spawn_npcs_once` also
    ignores duplicate live helper spawns with the same name.
  - `rc-core/encounter.h` / `rc-core/encounter.c` now keep original duration
    and elapsed age on each `RcEncounterEffect`, so viewer-side effect
    presentation can animate from core-owned state without inventing timing.
  - `rc-core/types.h` / `rc-core/combat.c` now preserve projectile length
    adjustment and step multiplier on each active `RcCombatProjectile`, making
    the profile used for launch/end timing inspectable during validation.
  - `rc-viewer/viewer.c` now draws active encounter effects as a generic
    tile/area graphics pass for acid pools, lava pools, room attacks, hidden
    objects, travelling effects, and form dives. This is intentionally driven
    by `world->encounter_effects` rather than boss-specific viewer branches.
  - `rc-viewer/viewer.c` now uses a lower-alpha projectile/effect shader for
    projectile model assets and applies spotanim brightness/shadow metadata as
    a render tint for launch, travel, and impact spotanim models.
  - `tests/test_combat_visuals_projectiles.c`,
    `tests/test_activity_spawns_runtime.c`,
    `tests/test_dev_validation_bank.c`, and
    `tests/test_encounter_prims.c` now cover tile-target projectile emission,
    retained projectile profile fields, Fight Cave wave reference metadata,
    activity-backed Jad dev placement, helper-spawn preparation, and encounter
    effect lifetime metadata.
- Reference audit:
  - Local RuneLite references were checked for projectile/rendering behavior.
    `Projectile` exposes both source/target points and source/target actors,
    and documents nullable target actors for AoE projectiles. RuneLite client
    projectile creation similarly accepts both a target position and nullable
    target actor. The Zalcano plugin also tracks falling-rock graphics objects
    separately from projectile movement, reinforcing that later exact parity
    should keep fixed-tile projectiles and independent graphics-object effects
    as separate presentation paths.
  - Local VoidPS/current Fight Caves references were checked for the same
    split: projectiles carry source/target offsets, heights, delay, flight
    time, curve, and offset, while independent area graphics carry tile,
    graphic id, height, delay, and rotation. This pass mirrors that separation
    at the RuneC state/rendering boundary, with exact per-effect graphic ids
    still to be sourced.
- Why it was made: boss validation could not reliably test encounter visuals
  while tile-targeted attacks looked like player-tracking projectiles, Jad was
  still relying on a static dev row instead of the activity wave data, and
  spawned helpers were not consistently prepared as encounter participants.
  These issues blocked visual validation before exact launch/impact offsets,
  alpha, lighting, and helper-object parity could be judged.
- Impact:
  - The new projectile target mode is generic and data-driven through
    encounter attack flags; it is not a viewer hardcode for Vorkath.
  - The Jad validation probe now exercises the same activity spawn binary used
    by runtime activity data, while preserving a fallback for missing data.
  - The viewer already renders `RC_COMBAT_ACTOR_NONE` projectile targets from
    their fixed `target_x/target_y`, so the core event shape now carries the
    information needed for tile-target visual checks.
  - Active hazard/object/room-attack effects are now visible in the viewer
    before exact model-backed OSRS graphic ids are attached, so encounter
    testing no longer depends on invisible core-only effect state.
  - Generated `data/defs/encounters.bin` and
    `data/defs/activity_spawns.bin` were refreshed locally. `data/` remains
    outside the main RuneC repository boundary.
- Known gaps:
  - Exact spotanim alpha, model lighting, launch/impact offset,
    model-orientation parity, and per-effect graphics-object ids still need
    viewer-side visual validation against b237/RuneLite/reference behavior.
  - Dynamic arena objects and the full set of spawned helper mechanics remain
    encounter presentation work; this pass makes active encounter effects
    visible but does not yet attach every effect to its exact OSRS model.
- Validation:
  - `python3 tools/export_encounters.py` wrote
    `data/defs/encounters.bin` with 50 encounters and zero warnings.
  - `python3 tools/export_activity_spawns.py` exported 117 activity spawn
    rows.
  - `python3 -m py_compile tools/export_activity_spawns.py tools/export_encounters.py`
    passed.
  - `cmake --build build --target test_combat_visuals_projectiles test_activity_spawns_runtime test_dev_validation_bank test_encounter_prims test_regular_npc_mechanics_combat`
    passed.
  - `ctest --test-dir build --output-on-failure -R "test_regular_npc_mechanics_combat|test_combat_visuals_projectiles|test_activity_spawns_runtime|test_dev_validation_bank|test_encounter_prims"`
    passed 5/5 matching tests.
  - `cmake --build build` passed.
  - `ctest --test-dir build --output-on-failure` passed 66/66 tests.
  - Follow-up focused validation after the encounter-effect render pass:
    `cmake --build build --target test_combat_visuals_projectiles test_encounter_prims`
    passed, `cmake --build build --target rc-viewer` passed, and
    `./build/test_combat_visuals_projectiles` plus
    `./build/test_encounter_prims` passed.
  - Follow-up full validation after the encounter-effect render pass:
    `cmake --build build` passed and
    `ctest --test-dir build --output-on-failure` passed 66/66 tests.
  - Follow-up coverage build `build_cov` passed
    `test_combat_visuals_projectiles` and `test_encounter_prims`. `gcov`
    reported `combat.c` at 70.71% line coverage, `encounter.c` at 77.89%,
    and `encounter_prims.c` at 39.67% for those focused tests. Changed core
    lines for projectile profile retention and encounter effect duration/age
    were executed.
  - Coverage build `build_cov` passed the same five focused tests. `gcov`
    reported `combat.c` at 75.26% line coverage,
    `activity_spawns.c` at 96.55%, `encounter_prims.c` at 39.67%,
    `encounter.c` at 77.99%, and `rc-viewer/dev_validation.c` at 82.25%.
    Changed lines were exercised: the fixed-tile projectile branch was taken,
    `rc_activity_spawn_wave_ref()` was called, helper preparation ran for both
    participating and non-participating helper paths, and the activity-backed
    Jad validation row was returned.
  - Current backend workload checks:
    `build/runec_backend_workloads_benchmark --mode npc-combat --envs 8 --ops 1000 --active 512`
    reported 14,228 units/sec;
    `build/runec_backend_workloads_benchmark --mode projectiles --envs 8 --ops 1000 --active 64`
    reported 2,284,440 units/sec; and
    `build/runec_backend_workloads_benchmark --mode spawn --ops 1000`
    reported 342 units/sec with 837,000 total spawned. No isolated before/after
    baseline was captured for this already-dirty 2.1 worktree, so these are
    current sanity numbers rather than a clean regression delta.

## 2026-05-19 — Encounter Visual Keys And Dev Grace Delay

- Change made: added a core-owned delayed NPC-vs-player activation path for
  dev validation encounters and replaced broad-only NPC combat visual
  selection with an attack-aware lookup that can use the exact selected
  encounter attack name from `encounters.bin`.
- Exact surfaces changed:
  - `rc-core/combat.c` / `rc-core/combat.h` now expose
    `rc_combat_start_npc_vs_player_delayed()`. Pending NPC activations face the
    player during the grace window, do not register the player as under attack,
    do not chase, and only enter normal NPC-vs-player combat after the delay.
  - `rc-core/types.h`, `rc-core/npc.c`, `rc-core/tick.c`, and
    `rc-core/encounter_prims.c` now clear delayed target state on spawn,
    respawn, death, explicit combat stop, and aggression-dropping encounter
    primitives.
  - `rc-viewer/dev_validation.c` now starts prepared boss-validation NPCs with
    an 8-tick grace delay by default, configurable with
    `RUNEC_DEV_BOSS_GRACE_TICKS`; `RUNEC_DEV_BOSS_ATTACKS=0` still disables the
    auto-start helper.
  - `rc-core/encounter.c` / `rc-core/encounter.h` now expose
    `rc_encounter_active_attack_name()` so combat can read the selected
    attack after `rc_encounter_select_npc_attack()`.
  - `rc-core/combat_visuals.c` / `rc-core/combat_visuals.h` now parse an
    optional `attack_key` column and provide
    `rc_combat_visual_for_npc_attack()`. Generic NPC visual lookup still
    prefers unkeyed rows and falls back safely.
  - `rc-core/combat.c` now selects NPC attack animation/projectile rows by
    the encounter attack key when an encounter is active.
  - `tools/export_combat_visuals.py` now emits an optional `attack_key`
    column and preserves keyed rows during deduplication.
  - Curated b237 boss/NPC rows now attach encounter attack names for current
    validation coverage: Graardor melee/shockwave, Strongstack melee,
    Steelwill magic, Grimspike ranged, KBD melee and breath variants,
    Vorkath melee/ranged/magic/dragonfire variants, and Jad melee/ranged/magic
    rows.
  - `tests/test_combat_visuals_projectiles.c` now verifies attack-keyed NPC
    visual lookup and generated KBD/Graardor/Vorkath keyed rows.
  - `tests/test_dev_validation_bank.c` now asserts boss-validation NPCs remain
    untargeted during the grace delay, emit no immediate projectile, and only
    produce incoming projectiles after the delay activates combat.
- Why it was made: dev boss transports started combat before the player could
  orient or move, and boss encounters can have multiple attacks with the same
  normalized combat style but different animation/projectile/impact visuals.
  KBD dragonfire variants and Vorkath standard/special dragonfire attacks
  cannot be represented correctly by only `npc_id + style`.
- Impact:
  - This is a generic runtime/exporter capability, not a viewer-side
    per-boss patch and not a reference-repo runtime dependency.
  - Existing non-encounter NPC visual rows keep working through the unkeyed
    fallback.
  - Generated `data/defs/combat_visuals.tsv` was refreshed locally from the
    b237/Joshua-F/RSMod-aligned exporter path. `data/` remains outside the
    main RuneC repo boundary.
  - This improves the foundation for boss combat presentation but does not
    complete every boss mechanic, every projectile alpha/detail issue, or
    every special/equipment side effect.
- Validation:
  - `python3 -m py_compile tools/export_combat_visuals.py` passed.
  - `python3 tools/export_combat_visuals.py` wrote 3532 source rows to
    `data/defs/combat_visuals.tsv`.
  - `cmake --build build --target test_combat_visuals_projectiles test_dev_validation_bank -- -j2`
    passed.
  - `./build/test_combat_visuals_projectiles` passed.
  - `./build/test_dev_validation_bank` passed.
  - `ctest --test-dir build -R "combat_visuals_projectiles|dev_validation_bank|encounter|combat_phase7_retaliation_ai|combat_phase3_movement_range_facing|regular_npc" --output-on-failure`
    passed 9/9 matching tests.
  - `ctest --test-dir build --output-on-failure` passed 66/66 tests.
  - Temporary coverage build under `/tmp/runec_cov` passed
    `test_combat_visuals_projectiles`, `test_dev_validation_bank`, and
    `test_encounter_prims`. `gcov` reported `combat.c` at 73.48% line
    coverage, `combat_visuals.c` at 95.17%, `encounter.c` at 77.73%,
    `npc.c` at 50.49%, `tick.c` at 12.34%, and `encounter_prims.c` at
    38.72%. The changed happy paths are covered; defensive cleanup branches
    for invalid delayed targets and NPC death/respawn remain low-frequency
    residual coverage gaps.
  - `./build/runec_backend_workloads_benchmark --mode projectiles --envs 64 --ops 2000 --active 32`
    reported 4,469,849 units/sec.
  - `./build/runec_backend_workloads_benchmark --mode npc-combat --envs 64 --ops 2000 --active 32`
    reported 240,394 units/sec.
  - `cmake --build build --target rc-viewer -- -j2` passed.
  - `timeout 8 env RC_VIEWER_EXIT_FRAMES=1 RC_VIEWER_QUIET=1 ./build/rc-viewer`
    loaded the viewer and exited after one frame.

## 2026-05-18 — Combat Visual Unsupported Markers And VoidPS Ingestion Removal

- Change made: kept the validation-bank unsupported weapon audit markers, but
  removed the attempted VoidPS combat/gfx ingestion path from
  `tools/export_combat_visuals.py`.
- Exact surfaces changed:
  - `tools/export_combat_visuals.py` no longer accepts `--voidps-root`, no
    longer reads VoidPS TOMLs, and no longer maps VoidPS NPC/sequence/spotanim
    aliases into generated combat visual rows.
  - Curated boss/NPC row notes now use `curated:b237:*` labels instead of
    misleading `voidps:*` labels.
  - Validation-bank weapons that still lack source-backed attack animation
    rows still emit `unsupported_item` audit rows instead of silently
    disappearing from `combat_visuals.tsv`.
  - The curated KBD dragonfire row remains keyed as `magic`, preserving the
    b237 dragonfire profile.
  - `tests/test_combat_visuals_projectiles.c` now asserts KBD/Jad curated
    rows and explicit unsupported weapon markers without depending on VoidPS.
- Why it was made: VoidPS is not guaranteed to use the same b237 cache, so it
  must not be a combat visual data source. Future generic boss/NPC visual
  loading needs to come from local b237-aligned data and reference audit, not
  direct ingestion of mismatched repo metadata.
- Impact:
  - Runtime loading is unchanged for unsupported markers because
    `unsupported_item` is not a recognized combat visual kind.
  - Generated `data/defs/combat_visuals.tsv` was refreshed locally and remains
    outside the main RuneC repo boundary because `data/` belongs to the DB repo.
- Validation:
  - `python3 -m py_compile tools/export_combat_visuals.py` passed.
  - `python3 tools/export_combat_visuals.py` wrote 3761 source rows to
    `data/defs/combat_visuals.tsv`.
  - `cmake --build build --target test_combat_visuals_projectiles test_dev_validation_bank rc-viewer -- -j2`
    passed.
  - `./build/test_combat_visuals_projectiles` passed.
  - `./build/test_dev_validation_bank` passed.
  - `ctest --test-dir build -R "combat|dev_validation|encounter|regular_npc" --output-on-failure`
    passed 20/20 matching tests.
  - `timeout 8 env RC_VIEWER_EXIT_FRAMES=1 RC_VIEWER_QUIET=1 ./build/rc-viewer`
    loaded the viewer and exited after one frame.
  - `tests/benchmarks/run_backend_workloads_benchmark.sh` completed the
    current backend workload suite. Key current results: path actions
    526 units/sec, object interactions 127 units/sec, mixed agent actions
    1146 units/sec, many-NPC combat ticks 12330 units/sec, projectile-heavy
    ticks 2450695 units/sec, and active-area loads 307 units/sec.
  - `tests/benchmarks/run_sps_benchmark.sh` reported 2738104 SPS for the
    current combat workload.
  - Coverage artifacts are unavailable in the current build
    (`build` has no `.gcno`/`.gcda` files), so line coverage could not be
    measured.

## 2026-05-18 — Dev Boss Incoming Projectile Validation Fixes

- Change made: corrected the dev-validation KBD fixture to use the real b237
  King Black Dragon NPC id `239`, updated the combat-visual exporter to emit
  KBD dragonfire as an `any`-style NPC visual row, and moved boss transport
  landing tiles closer to the target footprint for immediate incoming-visual
  validation.
- Exact surfaces changed:
  - `rc-viewer/dev_validation.c` now uses NPC `239` for the KBD transport and
    prepared encounter spawn.
  - `tools/export_combat_visuals.py` now emits the KBD dragonfire row from the
    `king_dragon` symbol and allows custom dragonfire styles to resolve the
    same visual.
  - `rc-viewer/viewer.c` now places dev-transport players at `npc_size + 2`
    tiles south of boss validation targets instead of `npc_size + 4`, avoiding
    avoidable pathing delay before bosses can attack.
  - `tests/test_dev_validation_bank.c` now loads content hooks plus encounter
    datasets and asserts incoming projectile creation for Steelwill, KBD,
    Vorkath, and Jad.
- Why it was made: manual combat validation needs boss buttons to prove
  incoming attack animations/projectiles. The prior KBD fixture used a
  Dagannoth Prime id from a stale visual row, and the regression test did not
  exercise actual incoming projectile emission.
- Impact:
  - This remains validation support; core combat APIs were not changed.
  - Generated `data/defs/combat_visuals.tsv` was refreshed locally, but
    generated data remains outside the main repo commit boundary.
- Validation:
  - `cmake --build build --target test_dev_validation_bank test_combat_visuals_projectiles rc-viewer -- -j2`
    passed.
  - `./build/test_dev_validation_bank` passed.
  - `./build/test_combat_visuals_projectiles` passed.
  - `ctest --test-dir build -R "dev_validation_bank|combat_visuals_projectiles|combat_phase10_resources_specials|combat_phase4_weapon_styles_cycle|combat_attack_animation_cooldown" --output-on-failure`
    passed 4/4 matching tests.
  - `timeout 8 env RC_VIEWER_EXIT_FRAMES=1 RC_VIEWER_QUIET=1 ./build/rc-viewer`
    loaded the viewer and exited after one frame.
  - Backend benchmark:
    `tests/benchmarks/run_backend_workloads_benchmark.sh --mode npc-combat --envs 16 --ops 2000 --warmup 100 --active 32`
    reported 239537 units/sec.
  - Backend benchmark:
    `tests/benchmarks/run_backend_workloads_benchmark.sh --mode projectiles --envs 16 --ops 5000 --warmup 100 --active 128`
    reported 2319950 units/sec.
  - Coverage artifacts are unavailable in the current build
    (`build` has no `.gcno`/`.gcda` files), so line coverage could not be
    measured.

## 2026-05-18 — Special Attack Runtime And Validation Coverage

- Change made: added first-pass OSRS special-attack content hooks for common
  validation-bank weapons and expanded generated combat-visual rows for their
  special animations, launch graphics, projectiles, and impact graphics.
- Exact surfaces changed:
  - `rc-content/combat/regular_npc_combat.c` now registers content hooks for
    player special energy cost and player special damage modification.
  - `rc-content/combat/regular_npc_combat.c` now covers representative
    validation specials: dragon dagger, dragon longsword, granite maul, dark
    bow, godswords, ancient godsword, toxic blowpipe, abyssal dagger, dragon
    claws, dragon warhammer, ballistas, dragon/armadyl/zaryte crossbows, and
    voidwaker.
  - `tools/export_combat_visuals.py` now emits broader b237-backed special
    rows, including ornate variants and explicit impact graphics where known.
  - `tests/test_combat_phase10_resources_specials.c` now verifies content-hook
    special energy spend, AGS damage boost, Dark Bow dragon-arrow minimum/cap,
    and Dark Bow extra ammo consumption.
- Why it was made: Combat Step 2.1 manual validation showed that the special
  attack button only exposed UI state for many weapons. We need a working
  runtime/content hook path before validating projectile and spotanim parity.
- Impact:
  - This is an OSRS content-layer implementation, not viewer gameplay logic.
  - Damage and cost foundations are now in place for common validation
    specials. Exact multi-hit timing, stat drains, freezes, delayed effects,
    bolt-proc guarantees, and full weapon-specific side effects remain later
    Step 3 parity work unless they are needed to unblock Step 2 visual tests.
  - Generated `data/defs/combat_visuals.tsv` was refreshed locally from the
    b237 cache/exporter path; generated DB data remains outside the main repo
    boundary.
- Validation:
  - `cmake --build build --target test_combat_phase10_resources_specials test_combat_visuals_projectiles test_dev_validation_bank rc-viewer -- -j2`
    passed.
  - `./build/test_combat_phase10_resources_specials` passed.
  - `./build/test_combat_visuals_projectiles` passed.
  - `./build/test_dev_validation_bank` passed.
  - `ctest --test-dir build -R "combat_phase10_resources_specials|combat_visuals_projectiles|dev_validation_bank|combat_phase4_weapon_styles_cycle" --output-on-failure`
    passed 4/4 tests.
  - `timeout 8 env RC_VIEWER_EXIT_FRAMES=1 RC_VIEWER_QUIET=1 ./build/rc-viewer`
    loaded the viewer and exited after one frame.
  - Backend benchmark:
    `tests/benchmarks/run_backend_workloads_benchmark.sh --mode npc-combat --envs 16 --ops 2000 --warmup 100 --active 32`
    reported 141600 units/sec.
  - Backend benchmark:
    `tests/benchmarks/run_backend_workloads_benchmark.sh --mode projectiles --envs 16 --ops 5000 --warmup 100 --active 128`
    reported 2117399 units/sec.
  - Coverage artifacts are unavailable in the current build
    (`build` has no `.gcno`/`.gcda` files), so line coverage could not be
    measured.

## 2026-05-18 — Combat Validation Encounter Prep

- Change made: expanded the temporary dev-validation encounter module so boss
  teleports can prepare representative combat targets instead of spawning only
  one inert focus NPC.
- Exact surfaces changed:
  - `rc-viewer/dev_validation.c` now defines encounter NPC tables for
    Graardor, KBD, Vorkath, and Jad. Graardor uses the VoidPS/reference
    bodyguard positions: Strongstack at `2866,5358,2`, Steelwill at
    `2872,5352,2`, and Grimspike at `2868,5362,2`.
  - `rc-viewer/dev_validation.c` now uses b237 KBD NPC id `2266`
    (`dagcave_magic_boss`), matching the existing combat-visual projectile row,
    instead of the stale `239` id.
  - `rc-viewer/viewer.c` now enables encounter/runtime mechanic datasets in
    the validation world and calls the dev encounter prep hook after a boss
    transport.
  - `tests/test_dev_validation_bank.c` now asserts the KBD id, Graardor
    encounter composition, multi-combat activation, stationary validation NPCs,
    and immediate NPC targeting for incoming-visual tests.
- Why it was made: Combat Step 2.1 validation needs incoming boss/NPC
  animations and projectiles. The previous boss buttons mostly moved the player
  near one spawned NPC; Graardor lacked bodyguards, KBD used an id without the
  b237 visual row, and the viewer world was not loading encounter datasets.
- Impact:
  - This remains isolated to `RUNEC_DEV_VALIDATION` viewer/test support.
  - Core combat APIs and headless combat presets were not changed.
  - Graardor/KBD/Vorkath/Jad buttons are now better validation fixtures for
    incoming projectile, spotanim, and attack-animation work.
- Validation:
  - Audited reference behavior against VoidPS Graardor bodyguard spawn code,
    2011Scape KBD combat script, RuneLite b237 gameval ids, and local curated
    encounter TOMLs.
  - `cmake --build build --target rc-viewer test_dev_validation_bank test_combat_visuals_projectiles -- -j2`
    passed.
  - `./build/test_dev_validation_bank` passed.
  - `./build/test_combat_visuals_projectiles` passed.
  - `ctest --test-dir build -R "dev_validation_bank|combat_visuals_projectiles|combat_phase4_weapon_styles_cycle" --output-on-failure`
    passed 3/3 tests.
  - `timeout 8 env RC_VIEWER_EXIT_FRAMES=1 RUNEC_DEV_TRANSPORT_DEST=graardor ./build/rc-viewer`
    loaded and exited successfully.
  - `timeout 8 env RC_VIEWER_EXIT_FRAMES=1 RC_VIEWER_QUIET=1 RUNEC_DEV_TRANSPORT_DEST=kbd ./build/rc-viewer`
    loaded and exited successfully.
  - Coverage artifacts are still unavailable in the current build
    (`build` has no `.gcno`/`.gcda` files), so line coverage could not be
    measured.
  - No backend SPS benchmark was run because this pass changes viewer-only
    validation setup and the viewer world configuration, not the headless
    runtime tick path.

## 2026-05-18 — Player Attack Animation One-Shot Playback

- Change made: changed `rc-viewer/viewer.c` player attack/action animation
  playback to treat attack/action sequences as one-shot animations. The viewer
  no longer wraps an attack sequence back to frame zero while the core
  attack-animation timer is still active.
- Change made: added viewer-side attack-timer edge tracking and suppression so
  falling back to the equipped stance after a completed attack does not allow
  the same active core attack timer to restart the first few attack frames.
- Change made: added a combat cooldown regression test in
  `tests/test_combat_phase4_weapon_styles_cycle.c`. The test attacks a target,
  verifies the first hit lands, ticks through the remaining weapon cooldown,
  and asserts no second player hit is queued until the cooldown expires.
- Why it was made: the player appeared to start a second attack animation
  immediately after the first animation finished, even while core attack
  cooldown still prevented a new attack. The core cooldown path was behaving
  correctly; the viewer was replaying the same sequence because animation frame
  advancement used modulo wrapping for attack animations, then briefly
  re-entered the attack while the same core timer was still active.
- Impact:
  - Core combat timing and hit queuing were not changed.
  - Continuous combat still attacks again when the weapon speed expires.
  - The visual attack sequence now plays once per core attack instead of
    looping during the cooldown window.
- Validation:
  - `cmake --build build --target rc-viewer test_combat_phase4_weapon_styles_cycle -- -j2`
    passed.
  - `./build/test_combat_phase4_weapon_styles_cycle` passed.
  - `ctest --test-dir build -R "combat_phase4_weapon_styles_cycle|combat_visuals_projectiles|dev_validation_bank" --output-on-failure`
    passed 3/3 tests.
  - `timeout 8 env RC_VIEWER_EXIT_FRAMES=1 ./build/rc-viewer` loaded and
    exited successfully after one frame.
  - Coverage review: the new core cooldown test executes the changed combat
    test assertion path; the viewer one-shot animation branch remains manually
    validated because this raylib render loop is not covered by the current C
    test harness.
  - Benchmark review: no backend SPS benchmark was rerun because core runtime
    behavior was unchanged; the fix is viewer animation playback plus a test.

## 2026-05-18 — Stance-Aware Weapon Attack And BAS Export

- Change made: added stance-indexed item combat visual lookup in `rc-core`.
  Player attacks now resolve item visuals by equipped weapon, combat style, and
  selected attack-style slot instead of flattening a weapon to one attack
  animation.
- Change made: updated `tools/export_combat_visuals.py` to emit per-stance
  weapon attack rows from RSMod/b237-enriched item configs. The generated
  `combat_visuals.tsv` now carries `stance_idx` and maps RSMod weapon-category
  stance types to RuneC combat styles.
- Change made: updated `tools/cache_pipeline/export_item_render_models.py` to
  read ready/walk/run BAS ids from the local RSMod cache-enricher TOML joined
  through the local Joshua-F `obj.sym` dump. This removes the old Armadyl-only
  godsword BAS fallback as the normal path.
- Change made: updated `tools/cache_pipeline/export_animations.py` so the
  player animation bundle includes BAS ids from `item_render.map` and
  attack/projectile animation ids from `combat_visuals.tsv`.
- Why it was made: validation-bank weapons, especially godswords, were sharing
  flattened or missing attack/stance data. RSMod separates combat stance,
  attack style, and attack type because weapons can have multiple aggressive
  stances with different animations; RuneC now preserves that distinction.
- Impact:
  - Armadyl, Bandos, Saradomin, Zamorak, and Ancient godswords now all export
    the same ready/walk/run BAS ids and the correct stance-specific attack
    sequences: slash stances 1/2, crush stance 3, and slash stance 4.
  - Validation-bank weapon audit currently reports 126 weapon-slot items, 104
    with explicit attack visual rows, and 22 without explicit attack rows. The
    missing set is now tracked in `work.md` as remaining Step 2.1 coverage
    rather than silently falling through.
  - Existing callers of `rc_combat_visual_for_item()` keep the previous
    fallback behavior; combat attack selection uses the new stance-aware lookup.
  - Generated local data was refreshed from the b237 cache:
    `data/defs/combat_visuals.tsv`, `data/models/items.models`,
    `data/models/item_render.map`, and `data/anims/player.anims`.
- Validation:
  - `python3 -m py_compile tools/export_combat_visuals.py tools/cache_pipeline/export_item_render_models.py tools/cache_pipeline/export_animations.py`
    passed.
  - `cc -fsyntax-only -Irc-core rc-core/combat_visuals.c rc-core/combat.c rc-core/combat_formula.c`
    passed.
  - `python3 tools/export_combat_visuals.py` wrote 3489 source rows with 34
    columns and 0 malformed rows.
  - `python3 tools/cache_pipeline/export_item_render_models.py --cache tools/cache_pipeline/source/current_fightcaves_demo/data/cache --item-ids combat-validation`
    wrote 1016 render models and 408 item render records.
  - `python3 tools/cache_pipeline/export_animations.py --modern-cache tools/cache_pipeline/source/current_fightcaves_demo/data/cache --output data/anims/player.anims --spotanims data/defs/spotanims.bin --item-render-map data/models/item_render.map --combat-visuals data/defs/combat_visuals.tsv`
    wrote `player.anims` with 48 item BAS sequence ids and 165 combat visual
    sequence ids included.
  - `cmake --build build --target rc-viewer test_combat_visuals_projectiles test_combat_phase4_weapon_styles_cycle test_dev_validation_bank -- -j2`
    passed.
  - `ctest --test-dir build -R "combat_visuals_projectiles|combat_phase4_weapon_styles_cycle|dev_validation_bank" --output-on-failure`
    passed 3/3 tests.
  - `timeout 8 env RC_VIEWER_EXIT_FRAMES=1 ./build/rc-viewer` loaded and
    exited successfully after one frame.
  - Coverage review: no coverage-enabled build artifacts or CMake coverage
    target are present in this checkout; targeted tests directly exercise the
    new stance-aware combat visual lookup and validation-bank load path.
  - Benchmarks:
    `tests/benchmarks/run_backend_workloads_benchmark.sh --mode projectiles --envs 8 --ops 20000 --active 64`
    passed at 2,383,294 units/s;
    `tests/benchmarks/run_backend_workloads_benchmark.sh --mode npc-combat --envs 8 --ops 20000 --active 64`
    passed at 115,954 units/s.

## 2026-05-18 — Exact Scene Hover And Left-Click Targeting

- Change made: removed the oversized viewer pick padding for banker NPCs,
  validation dummies, and storage objects. NPC/object hit tests now use tight
  footprint-sized boxes with only a small tolerance.
- Change made: added a single viewer scene-hover resolver for NPCs, placed
  objects, ground items, and walkable tiles. Middle-click menus, direct
  left-click actions, selected item/spell targeting, and the hover label now
  resolve through the same target path.
- Change made: added a top-left hover action label showing the default action
  that left-click will fire, matching the OSRS/RuneLite menu-entry model where
  the current top/default entry is observable before clicking.
- Why it was made: broad validation hitboxes caused nearby tile clicks to be
  swallowed by NPC/object overlap. The fix is exact scene targeting plus a
  visible action probe, not larger interaction radii.
- Impact notes:
  - `rc-viewer/viewer.c` owns this presentation/input translation change.
    `rc-core` interaction semantics were not changed.
  - The previous enlarged validation pick-volume entry is superseded by this
    tighter resolver.
- Validation:
  - `cmake --build build --target rc-viewer test_dev_validation_bank test_shops_storage_runtime -- -j2`
    passed.
  - `./build/test_dev_validation_bank` passed.
  - `./build/test_shops_storage_runtime` passed.
  - `ctest --test-dir build -R "dev_validation_bank|shops_storage" --output-on-failure`
    passed 2/2 tests.
  - `timeout 8 env RC_VIEWER_EXIT_FRAMES=1 ./build/rc-viewer` loaded and
    exited successfully after one frame.
  - Coverage was not measured because this is a raylib viewer input/rendering
    path and the current build has no coverage artifacts for it.
  - No SPS benchmark was run because the change is viewer-only and does not
    alter `rc-core` tick/pathfinding simulation cost.

## 2026-05-18 — Validation NPC Pick Volumes

- Change made: enlarged viewer-side NPC picking for validation combat dummies
  and banker NPCs without changing core NPC interaction semantics.
- Change made: enlarged viewer-side storage-object picking, so bank booths,
  stalls, and similar storage targets can be clicked from the visible bank
  wall/stall area instead of only a tiny tile box.
- Change made: made direct left-click object targeting use the same object
  picker as middle-click context menus, preventing a valid context target from
  no-oping on direct left-click.
- Change made: changed viewer left-click NPC dispatch to use the NPC's default
  useful option: storage actions first, attack second, then first available
  action. This prevents widened banker hitboxes from being interpreted as
  failed attack clicks.
- Change made: kept normal NPC picking tight so broad validation hitboxes do not
  leak into ordinary combat or world interaction targeting.
- Why it was made: the temporary Varrock-bank validation targets were too hard
  to click during combat loadout testing, and left-click targeting had drifted
  from context-menu targeting.
- Validation:
  - `cmake --build build --target rc-viewer test_dev_validation_bank -- -j2`
    passed.
  - `./build/test_dev_validation_bank` passed.
  - `ctest --test-dir build -R dev_validation_bank --output-on-failure`
    passed 1/1 test.

## 2026-05-18 — Stationary Validation Combat Dummy

- Change made: added an instance-local `disable_wander` flag for NPCs and made
  `rc_npc_tick()` skip wander movement when that flag is set.
- Change made: pinned the Varrock-bank validation combat dummy to a
  wall-adjacent safe tile and marked only that spawned dummy as non-wandering.
- Change made: extended `test_dev_validation_bank` to spawn the validation
  dummy, tick it repeatedly, and assert that it remains stationary.
- Why it was made: the combat dummy is temporary validation tooling and should
  behave like a fixed target, not like a normal wandering NPC.
- Validation:
  - `cmake --build build --target rc-viewer test_dev_validation_bank -- -j2`
    passed.
  - `./build/test_dev_validation_bank` passed.
  - `ctest --test-dir build -R "dev_validation_bank" --output-on-failure`
    passed 1/1 test.

## 2026-05-18 — Side Clan Tab Validation Controls

- Change made: moved the viewer scene-level selector out of the world overlay
  and into the side-panel Clan tab beside the combat-validation teleports.
- Change made: wired the side-panel Clan icon as a real clickable tab. The
  bottom chat `Clan` filter remains a chat filter only.
- Change made: added a UI scene-plane intent and synchronized current scene
  plane, player plane, and follow/override state from the viewer into UI state.
- Change made: removed the clickable overlay plane controls from the viewport;
  keyboard shortcuts remain: `PageUp`, `PageDown`, and `Home`.
- Why it was made: temporary validation controls should live in the side Clan
  tab with the other dev/test affordances instead of covering the game view.
- Validation:
  - `cmake --build build --target rc-viewer test_dev_validation_bank test_shops_storage_runtime -- -j2`
    passed.
  - `./build/test_dev_validation_bank` passed.
  - `./build/test_shops_storage_runtime` passed.
  - `ctest --test-dir build -R "dev_validation_bank|shops_storage" --output-on-failure`
    passed 2/2 tests.

## 2026-05-18 — Validation Bank New-Item Equip And Icon Fallback

- Change made: relaxed player-equipment validation for cache-backed equipment
  that has a valid slot and wearable male/female model links even when older
  metadata did not set the player-equippable flag. Requirement checks still run.
- Change made: updated `tools/export_items.py` so future item exports infer
  player-equippable and weapon flags from wearable model links for non-noted,
  non-placeholder equipment records.
- Change made: enabled model-derived UI item icon fallback by default unless
  `RUNEC_UI_MODEL_ITEM_ICONS=0`, so newer validation-bank items without
  pregenerated sprite PNGs no longer show as missing icons.
- Change made: expanded viewer-side bank NPC picking only for NPC definitions
  whose actions route to bank storage. Core reach/action validation is unchanged.
- Change made: extended `tests/test_dev_validation_bank.c` to equip representative
  newer b237 validation items: Oathplate pieces, Avernic treads, Confliction
  gauntlets, and Twinflame staff.
- Why it was made: several newer validation-bank items had cache wearable models
  but stale/incomplete exported equipment flags, which made them show missing
  icons and no-op when equipped. Bankers were also too precise to click while
  testing bank-driven combat loadouts.
- Validation:
  - `cmake --build build --target rc-viewer test_dev_validation_bank test_shops_storage_runtime test_combat_phase6_hit_pipeline -- -j2`
    passed.
  - `./build/test_dev_validation_bank`, `./build/test_shops_storage_runtime`,
    and `./build/test_combat_phase6_hit_pipeline` passed.
  - `ctest --test-dir build -R "dev_validation_bank|shops_storage|combat_phase6" --output-on-failure`
    passed 3/3 tests.
  - Coverage build in `build-coverage` with `--coverage` ran the targeted
    tests; `gcov` reported `test_dev_validation_bank.c` at `98.70%`,
    `dev_validation.c` at `54.33%`, `items.c` at `58.83%`, `storage.c` at
    `87.58%`, and `combat.c` at `43.84%`.
  - `tests/benchmarks/run_sps_benchmark.sh --mode combat --envs 64 --steps 20000 --warmup 1000`
    passed: `2,852,632` SPS, `350.55` ns/env-step.
  - `tests/benchmarks/run_sps_benchmark.sh --mode idle --envs 64 --steps 20000 --warmup 1000`
    passed: `76,053,928` SPS, `13.15` ns/env-step.

## 2026-05-18 — Isolated Combat Validation Bank And Item Render Coverage

- Change made: moved the hardcoded combat-validation bank seed, boss
  transport list, Varrock-bank start constants, validation withdraw policy, and
  combat dummy spawn into `rc-viewer/dev_validation.c`/`.h`. The viewer now
  calls that isolated layer; `rc-core` keeps only generic storage/combat state.
- Change made: validation bank seeding now guarantees at least two of every
  non-stackable item and at least 1000 of every stackable item. Ammo/rune and
  resource stacks still seed larger quantities where useful.
- Change made: validation bank withdraw now leaves one item in the bank slot so
  the tab order stays stable while repeatedly testing equipment. Generic
  `rc-core` bank withdrawal still supports emptying a bank slot.
- Change made: expanded the Special validation tab with the component items
  behind the requested full-set regression bundles so the bundles are testable
  without relying on another bank tab.
- Change made: extended `tools/cache_pipeline/export_item_render_models.py`
  with a `combat-validation` item-id mode. The exporter resolves item names
  from the isolated validation module, prefers unnoted/non-placeholder ids, and
  exports the corresponding b237 render map/models.
- Change made: regenerated `data/models/items.models` and
  `data/models/item_render.map` locally from the b237 cache for the validation
  bank set. The generated data lives in the separate `data/` repository, not
  the main RuneC repo.
- Change made: added `tests/test_dev_validation_bank.c` covering validation
  bank quantities, unnoted/non-placeholder storage, tab coverage, and
  leave-one-withdraw behavior.
- Why it was made: the combat validation helpers are temporary tooling and
  should stay easy to disable/remove. The bank also needs stable slot ordering
  while testing hundreds of gear/ammo/rune items, and the item render pipeline
  needs to export the validation set instead of relying on the old smaller
  simulation subset.
- Impact:
  - `RUNEC_DEV_VALIDATION=0` disables the isolated validation bank/transport
    helpers.
  - Rings/ammo and other non-body-overlay slots may still have no visible worn
    body model by design, but every validation item now has a render-map record
    generated from the local b237 cache.
  - Correct special attacks, exact spell/projectile assets, boss attacks, and
    weapon-specific side effects remain Combat Step 2/3 work; this change makes
    the test items available and renderable where cache wearable models exist.
- Validation:
  - `cmake --build build --target rc-viewer test_dev_validation_bank test_shops_storage_runtime test_combat_phase6_hit_pipeline -- -j2`
    passed.
  - `./build/test_dev_validation_bank`, `./build/test_shops_storage_runtime`,
    and `./build/test_combat_phase6_hit_pipeline` passed.
  - `ctest --test-dir build -R "dev_validation_bank|shops_storage|combat_phase6" --output-on-failure`
    passed 3/3 tests.
  - `python3 tools/cache_pipeline/export_item_render_models.py --cache tools/cache_pipeline/source/current_fightcaves_demo/data/cache --item-ids combat-validation`
    wrote 1016 render models and 408 item render records.
  - A sanity check over the generated render map reported 408 validation item
    ids, 408 render records, and 0 missing records.
  - Coverage build in `build-coverage` with `--coverage` ran the targeted
    tests; `gcov` reported `test_dev_validation_bank.c` at `100.00%`,
    `dev_validation.c` at `54.33%`, `storage.c` at `87.58%`, and `combat.c`
    at `43.84%`. The unhit validation helper paths are the headed boss
    transport/dummy scene paths, which remain manual viewer validation.
  - `tests/benchmarks/run_sps_benchmark.sh --mode combat --envs 64 --steps 20000 --warmup 1000`
    passed: `2,850,029` SPS, `350.87` ns/env-step.
  - `tests/benchmarks/run_sps_benchmark.sh --mode idle --envs 64 --steps 20000 --warmup 1000`
    passed: `77,666,821` SPS, `12.88` ns/env-step. An earlier parallel idle
    benchmark collided with the combat benchmark over the same temporary
    benchmark binary; rerunning idle sequentially passed.
  - Headed viewer smoke remains manual in this shell because there is no usable
    X display and `xvfb-run` is unavailable.

## 2026-05-18 — Combat Test Bank Tabs And Varrock Dummy

- Change made: extended `RcPlayer` bank state with a per-slot bank tab and
  added `rc_bank_add_item_tab()` so validation banks can keep duplicate
  item ids separated by test bucket while normal deposits still stack against
  the first existing bank stack.
- Change made: updated bank storage to convert noted item ids to their
  unnoted linked item before storing, keeping the validation bank unnoted.
- Change made: expanded the viewer bank from a flat list to five validation
  tabs: Ranged, Mage, Melee, PvP, and Special. The viewer filters and lazily
  builds item icons for only the active bank tab.
- Change made: replaced the overlay dev boss transport panel with Clan-chat
  tab buttons. The viewer now emits a dev-transport UI intent from the Clan
  chat tab instead of drawing buttons over the bank/game viewport.
- Change made: seeded the viewer validation bank by resolving item names
  against loaded `items.bin` at runtime, preferring unnoted, non-placeholder
  item definitions. The seed now includes the requested high-level ranged,
  mage, melee, PvP, and special-test item buckets plus the prior combat
  validation supplies.
- Change made: changed the default viewer spawn to Varrock west bank and
  added a temporary Varrock-bank combat dummy using cache NPC `2668`
  (`Combat dummy`). That specific spawned instance is marked as a max-hit
  target for validation; regular NPCs remain unaffected.
- Change made: added a generic per-instance `force_player_max_hit` NPC flag
  in `rc-core` and used it in the player combat hit roll path so the dummy
  always receives the current equipped setup's max hit.
- Change made: expanded storage and combat hit-pipeline tests for bank tab
  behavior and force-max-hit dummy behavior.
- Why it was made: Combat Step 2+ validation needs fast, repeatable access to
  high-level gear/ammo/runes and a nearby target without inventory clutter or
  running to a bank on every viewer launch. Moving dev transports into the Clan
  chat tab removes bank UI overlap while keeping the helper clearly
  validation-only.
- Impact:
  - `rc-core` still owns storage movement and combat damage semantics.
  - `rc-viewer` owns the temporary presentation/test affordances: bank tab UI,
    Clan-tab transport buttons, default start position, and local dummy spawn.
  - Some newly seeded items may still need item-render-map/model/exporter
    coverage before every equipped appearance is visually correct; this feeds
    the remaining Combat Step 2/3 asset and visual parity work.
- Validation:
  - `cmake --build build --target rc-viewer test_shops_storage_runtime test_combat_phase6_hit_pipeline -- -j2`
    passed.
  - `./build/test_shops_storage_runtime` passed.
  - `./build/test_combat_phase6_hit_pipeline` passed when run alone. An
    earlier parallel run raced with `ctest` over that test's existing
    `/tmp/runec_phase6_drops.bin` fixture; rerunning sequentially passed.
  - `ctest --test-dir build -R "shops_storage|combat_phase6|headless_action|combat_visuals_projectiles" --output-on-failure`
    passed 4/4 tests.
  - Coverage build in `build-coverage` with `--coverage` ran
    `test_shops_storage_runtime` and `test_combat_phase6_hit_pipeline`;
    `gcov` reported `storage.c` at `87.58%` line coverage and `combat.c` at
    `43.84%` line coverage for the targeted tests.
  - `tests/benchmarks/run_sps_benchmark.sh --mode combat --envs 64 --steps 20000 --warmup 1000`
    passed: `2,862,440` SPS, `349.35` ns/env-step.
  - `tests/benchmarks/run_sps_benchmark.sh --mode idle --envs 64 --steps 20000 --warmup 1000`
    passed: `80,371,608` SPS, `12.44` ns/env-step.
  - Headed viewer smoke remains manual in this shell because there is no usable
    X display and `xvfb-run` is unavailable.

## 2026-05-18 — Rudimentary Bank Runtime For Combat Validation

- Change made: extended `rc-core/storage.c`/`.h` and `rc-core/api.h` with
  NPC-backed storage opening, storage close, and a public bank seed helper.
- Change made: updated the default NPC interaction dispatch in
  `rc-core/tick.c` so a reached NPC option named `Bank` or `Collect` opens
  storage through core, matching the existing object-storage path.
- Change made: added a simple `rc-viewer` bank panel and UI intents for
  withdraw, deposit, and close. The viewer only translates input; bank state
  and item movement remain in `rc-core`.
- Change made: enabled `RC_SUB_STORAGE` in the viewer config and replaced the
  old validation inventory pile with a seeded core bank containing high-level
  combat weapons, armor, ammo, runes, food, and potions for combat visual
  testing.
- Change made: expanded storage tests with NPC banker opening, bank seeding,
  storage close, and withdraw/deposit coverage.
- Change made: fixed `tests/benchmarks/run_sps_benchmark.sh` to link zlib when
  compiling against `librc-core.a`; `assets.c` requires zlib symbols.
- Why it was made: Combat validation needs a quick local way to withdraw
  weapons, runes, ammo, and gear without hardcoding a new inventory for every
  visual test. The implementation keeps gameplay/storage rules in `rc-core`
  and limits `rc-viewer` to presentation and input-intent forwarding.
- Impact:
  - Bank booths/objects still open through the existing object behavior path.
  - Banker NPCs can now open bank state if their loaded cache action exposes a
    bank/collect option.
  - The bank UI is intentionally rudimentary and validation-focused; full OSRS
    bank tab/search/placeholder/collection-box parity remains later UI work.
  - The default viewer starts with combat test items in the bank instead of
    cluttering the inventory.
- Validation:
  - `cmake --build build --target rc-viewer test_shops_storage_runtime test_headless_action_runtime -- -j2`
    passed.
  - `./build/test_shops_storage_runtime` passed.
  - `./build/test_headless_action_runtime` passed.
  - `ctest --test-dir build -R "shops_storage|headless_action|npc_option" --output-on-failure`
    passed 3/3 tests.
  - Coverage build in `build-coverage` with `--coverage` ran
    `test_shops_storage_runtime`; `gcov` reported `storage.c` at `89.43%`
    line coverage. `tick.c` reported `13.95%` line coverage because the
    targeted storage test only exercises a small interaction-dispatch slice.
  - `tests/benchmarks/run_sps_benchmark.sh --mode combat --envs 64 --steps 20000 --warmup 1000`
    passed: `2,861,718` SPS, `349.44` ns/env-step.
  - `tests/benchmarks/run_sps_benchmark.sh --mode idle --envs 64 --steps 20000 --warmup 1000`
    passed: `84,133,400` SPS, `11.89` ns/env-step.
  - Headed viewer smoke testing could not be completed in this shell because
    there is no usable X display and `xvfb-run` is not installed. The viewer
    binary builds; manual validation should use `./build/rc-viewer`.

## 2026-05-18 — Combat Step 2 Double-Launch And Projectile Pitch

- Change made: added `double_launch_spotanim_id` to `RcCombatVisualDef` and
  extended `combat_visuals.tsv` loading/exporting with a final
  `double_launch_spotanim` column.
- Change made: updated `tools/export_combat_visuals.py` so ranged ammo rows
  preserve RSMod `projectile_launch_double` metadata from `objs.toml`.
- Change made: updated player projectile emission so grouped special/effect
  projectile events can use the ammo-specific double-launch spotanim. This
  fixes Dark Bow-style visual launch selection without hardcoding ammo ids in
  the viewer.
- Change made: updated `rc-viewer` projectile rendering so travelling
  projectile models apply pitch from the current projectile velocity, following
  the Client3/OSRS projectile motion model instead of yaw-only presentation.
- Change made: regenerated local ignored `data/defs/combat_visuals.tsv`; rune
  arrows now export normal launch `24` and double-launch `1109`, and Dark Bow
  special rows remain effect rows layered over the selected ammo visual.
- Change made: expanded `tests/test_combat_visuals_projectiles.c` to verify
  double-launch loading, generated rune-arrow double-launch metadata, and the
  Dark Bow-style grouped projectile event order.
- Why it was made: Combat Step 2 is about projectile and spotanim parity.
  RSMod shows Dark Bow and arrow ammo use `proj_launch_double`; preserving that
  metadata in the exporter/core keeps the behavior data-driven and prevents
  viewer-side guesses. Client3 shows projectile models update pitch from
  velocity, so headed rendering needs to observe the same profile shape.
- Impact:
  - Existing TSV rows remain compatible because the new column is appended and
    missing values default to `-1`.
  - Normal ranged attacks still use single launch spotanims.
  - Grouped special/effect projectile events can use the correct ammo-specific
    double launch when the selected ammo provides it.
  - Viewer projectile pitch is compile-validated, but final visual correctness
    still requires manual headed validation because the rendering path is not
    headless-testable.
- Validation:
  - `python3 tools/export_combat_visuals.py` regenerated local ignored
    `data/defs/combat_visuals.tsv`.
  - `cmake --build build --target rc-viewer test_combat_visuals_projectiles test_combat_phase10_resources_specials`
    passed.
  - `./build/test_combat_visuals_projectiles` passed.
  - `ctest --test-dir build -R "test_combat_visuals_projectiles|test_combat_phase10_resources_specials|test_headless_action_runtime|test_combat_phase11_validation_gate|test_prayer_spell_actions_runtime" --output-on-failure`
    passed 5/5 tests.
  - Coverage build in `build-coverage-combat-step2` with `--coverage` ran
    `test_combat_visuals_projectiles` and
    `test_combat_phase10_resources_specials`; `gcov` reported `combat.c` at
    `68.81%` line coverage and `70.98%` branch execution,
    `combat_visuals.c` at `94.74%` line coverage and `93.46%` branch
    execution, and `test_combat_visuals_projectiles.c` at `100.00%` line
    coverage.
  - `bash tests/benchmarks/run_backend_workloads_benchmark.sh --mode mixed --envs 4 --ops 200`
    passed: `mixed-agent-actions` measured `1,117` units/sec
    (`895,387.13` ns/unit) across 800 units.
  - `bash tests/benchmarks/run_backend_workloads_benchmark.sh --mode projectiles --envs 4 --ops 200 --active 64`
    passed: `projectile-heavy-ticks` measured `2,554,792` units/sec
    (`391.42` ns/unit) across 800 units.

## 2026-05-18 — Combat Step 2 Multi-Projectile Visual Foundation

- Change made: extended `RcCombatVisualDef` with multi-projectile metadata:
  projectile count, alternate projectile profile, auxiliary travel/impact
  spotanim ids, auxiliary model/animation ids, and last-shot-only impact
  control.
- Change made: extended `RcCombatProjectile` with sequence index/count so a
  single combat swing can expose grouped projectile visual events to headed
  rendering and headless tests.
- Change made: updated `rc-core/combat.c` so player and NPC projectile emission
  can spawn grouped projectile events. Special/effect visuals can now layer
  auxiliary projectiles over the normal ammo/spell projectile, and launch-only
  special spotanims can be emitted without inventing viewer-side gameplay
  behavior.
- Change made: updated `tools/export_combat_visuals.py` to emit the expanded
  TSV schema and first-pass Dark Bow double-arrow auxiliary projectile rows
  from RSMod/b237 references. Existing rows are padded during export and remain
  loader-compatible.
- Change made: expanded `tests/test_combat_visuals_projectiles.c` with a
  Dark Bow-style special regression: one special swing now emits two normal
  ammo projectile events plus two auxiliary smoke projectile events with
  separate first/second projectile profiles.
- Why it was made: Combat Step 2 requires the runtime event shape to support
  OSRS-style multi-projectile presentation before special attacks, boss
  attacks, and broader spell/ranged parity can be corrected. This keeps the
  behavior in `rc-core`/exported data instead of hardcoding projectile fixes in
  the viewer.
- Impact:
  - Existing single-projectile spells, ranged attacks, NPC attacks, and impact-
    only spell visuals remain compatible.
  - Dark Bow-style specials now have a data-backed path for layered projectiles
    and staggered timing.
  - Melee specials with only player-side spotanims can now surface a core-owned
    visual event for the viewer to render.
  - Full special attack gameplay, broader boss coverage, and visual
    alpha/lighting polish remain in Combat Steps 2 and 3.
- Validation:
  - `python3 tools/export_combat_visuals.py` regenerated local ignored
    `data/defs/combat_visuals.tsv` with expanded headers and Dark Bow rows.
  - `cmake --build build --target rc-viewer test_combat_visuals_projectiles test_combat_phase10_resources_specials`
    passed.
  - `./build/test_combat_visuals_projectiles` and
    `./build/test_combat_phase10_resources_specials` passed.
  - `ctest --test-dir build -R "test_combat_visuals_projectiles|test_combat_phase10_resources_specials|test_headless_action_runtime|test_combat_phase11_validation_gate|test_prayer_spell_actions_runtime" --output-on-failure`
    passed 5/5 tests.
  - Coverage build in `build-coverage-combat-step2` with `--coverage` ran
    `test_combat_visuals_projectiles` and
    `test_combat_phase10_resources_specials`; `gcov` reported
    `combat.c` at `68.69%` line coverage and `70.74%` branch execution,
    `combat_visuals.c` at `94.64%` line coverage and `93.40%` branch
    execution, and `test_combat_visuals_projectiles.c` at `100.00%` line
    coverage.
  - `bash tests/benchmarks/run_backend_workloads_benchmark.sh --mode projectiles --envs 4 --ops 200 --active 64`
    passed: `projectile-heavy-ticks` measured `1,592,290` units/sec
    (`628.03` ns/unit) across 800 units.
  - `bash tests/benchmarks/run_backend_workloads_benchmark.sh --mode mixed --envs 4 --ops 200`
    passed: `mixed-agent-actions` measured `1,113` units/sec
    (`898,221.84` ns/unit) across 800 units.

## 2026-05-18 — Stable Spell Visual ID Lookup

- Change made: added `rc_combat_visual_for_spell_id`, which looks up spell
  combat visuals by stable spell index before falling back to display-name
  matching.
- Change made: updated `rc_load_combat_visuals` so spell rows resolve their
  `key` column to a spell index when spell definitions are already loaded.
  Numeric spell keys are also accepted for future generated rows; existing
  human-readable name rows remain compatible.
- Change made: updated player magic projectile selection in `rc-core/combat.c`
  to pass the current manual/autocast spell index into combat visual lookup
  instead of relying only on `spell->name`.
- Change made: expanded `tests/test_combat_visuals_projectiles.c` with a
  regression check that renames a spell after loading combat visuals. The old
  name-only lookup fails for the renamed display name, but the id-keyed combat
  path still emits the correct projectile.
- Why it was made: this closes the final Combat Step 1.3 architecture item.
  Spell visual selection should survive display-name edits, capitalization
  drift, and future generated spell metadata changes before deeper combat
  polish builds on it.
- Impact:
  - Runtime spell projectile visuals are now keyed by core spell identity when
    possible.
  - Existing `combat_visuals.tsv` rows do not need regeneration; the loader
    resolves their names to stable indices when `spells.bin` is loaded first,
    which is the normal `RcWorldConfig` bring-up order.
  - Name fallback remains for compatibility with tests/tools that load combat
    visuals before spell definitions.
  - Combat Step 1.3 is complete for the current pass.
- Validation:
  - `cmake --build build --target rc-viewer test_combat_visuals_projectiles test_headless_action_runtime test_objects_runtime test_active_area_runtime`
    passed.
  - `./build/test_combat_visuals_projectiles`,
    `./build/test_headless_action_runtime`, `./build/test_objects_runtime`, and
    `./build/test_active_area_runtime` passed.
  - `ctest --test-dir build -R "test_headless_action_runtime|test_objects_runtime|test_active_area_runtime|test_combat_visuals_projectiles|test_spawn_slices_runtime|test_plane_contracts_runtime|test_prayer_spell_actions_runtime|test_combat_phase10_resources_specials" --output-on-failure`
    passed 8/8 tests.
  - Coverage build in `/tmp/runec_spell_visual_id_cov` with `--coverage` ran
    `test_combat_visuals_projectiles` and `test_headless_action_runtime`;
    `gcov` reported `combat_visuals.c` at `93.75%` line coverage and `92.31%`
    branch execution, and `combat.c` at `67.59%` line coverage and `69.82%`
    branch execution. `rc_combat_visual_for_spell_id` executed 10 calls, and
    `select_player_attack_visuals` executed the new id-keyed spell visual call
    4 times.
  - `bash tests/benchmarks/run_backend_workloads_benchmark.sh --mode mixed --ops 200 --envs 4`
    passed: `mixed-agent-actions` measured `942` units/sec
    (`1,061,446.95` ns/unit) across 800 units.

## 2026-05-18 — Placement-Key Object Interactions And Headless Action Tests

- Change made: added placement-key identity to `RcInteractionTarget` and added
  placement-key object APIs for object option, inventory-item-on-object, and
  spell-on-object actions. Existing tile/id object APIs now delegate to the
  placement-key path with key `0`.
- Change made: updated object interaction routing, reach validation, traversal
  lookup, dynamic object state lookup, door/gate mutation, and object action
  animation state creation to prefer exact placed-object keys when available.
  Mismatched object/tile/plane targets are rejected instead of falling back to
  broad id or nearby matching.
- Change made: added `rc_world_object_active_state_by_key` and updated
  `rc-viewer` object picking/action dispatch so presentation passes the exact
  picked placement key to core and observes the active state for that placement
  when rendering/picking dynamic objects.
- Change made: added `tests/test_headless_action_runtime.c`, which mirrors
  viewer-equivalent actions through core/content APIs: walk-to-tile, object
  option, NPC option, item-on-object, spell-on-object, spell-on-NPC, area
  activation, reset/load, and combat tick.
- Why it was made: Combat Step 1.3 needs headed play and future headless RL
  sims to share the same action semantics. Object interactions must resolve to
  one exact placed object, mutate one placement-local state entry, and leave
  `rc-viewer` as input translation/rendering only.
- Impact:
  - Dynamic object state is now keyed by placement identity when the cache
    placement row is known.
  - Viewer clicks on doors/gates/objects route through the same backend APIs
    that headless agents can call.
  - Legacy object APIs remain available for tests/tools that only know
    object-id/tile coordinates, but exact placement keys are preferred.
  - Spell visual id-keying was completed in the follow-up same-day Step 1.3
    entry.
- Validation:
  - `cmake --build build --target rc-viewer test_headless_action_runtime test_objects_runtime test_active_area_runtime`
    passed.
  - `./build/test_headless_action_runtime`, `./build/test_objects_runtime`, and
    `./build/test_active_area_runtime` passed.
  - `ctest --test-dir build -R "test_headless_action_runtime|test_objects_runtime|test_active_area_runtime|test_combat_visuals_projectiles|test_spawn_slices_runtime|test_plane_contracts_runtime" --output-on-failure`
    passed 6/6 tests.
  - Coverage build in `/tmp/runec_placement_actions_cov2` with `--coverage`
    ran `test_objects_runtime` and `test_headless_action_runtime`; `gcov`
    reported `tick.c` at `68.37%` line coverage and `74.29%` branch execution.
    The changed placement-key paths executed: `current_object_placement_key`
    ran 563 calls, `object_state_find_by_key` ran 135 calls,
    `rc_player_interact_object_placement` ran 43 calls,
    `rc_player_use_inventory_item_on_object_placement` ran 1 call,
    `rc_player_cast_spell_on_object_placement` ran 1 call, and
    `rc_world_object_active_state_by_key` ran 2 calls.
  - `bash tests/benchmarks/run_backend_workloads_benchmark.sh --mode mixed --ops 200 --envs 4`
    passed: `mixed-agent-actions` measured `896` units/sec
    (`1,115,564.37` ns/unit) across 800 units. A previous same-workload sample
    in this pass measured `931` units/sec, so this result is within small-run
    variance but should remain watched as interaction workload coverage grows.

## 2026-05-18 — Core-Owned Validation NPC Ensuring

- Change made: added `RcNpcEnsureResult`, `rc_world_find_npc_near`, and
  `rc_world_ensure_npc_near` so runtime/scenario code can find or materialize a
  cache-backed NPC target through `rc-core` instead of frontend policy.
- Change made: updated the temporary dev boss transport panel so `rc-viewer`
  no longer resolves NPC definitions or calls `rc_npc_spawn` directly. The
  viewer now delegates validation NPC ensuring to `rc-core` and only refreshes
  presentation-side NPC render state when core reports a new NPC.
- Change made: expanded `tests/test_active_area_runtime.c` to cover find,
  spawn, reuse, null-world, and missing-id behavior for the new core NPC ensure
  API.
- Why it was made: Combat Step 1.3 requires gameplay population ownership to
  live in the backend so headed viewer play and future headless RL sims use the
  same world-state rules.
- Impact:
  - Scene/NPC population policy is now backend-owned for both active-area
    reloads and temporary boss validation targets.
  - `rc-viewer` still observes NPC state for rendering, picking, minimap, and
    validation combat presentation, but it no longer owns NPC reload/spawn
    policy.
  - Follow-up same-day Step 1.3 entries completed placement-key interaction
    state, viewer-action parity tests, and stable spell visual id-keying.
- Validation:
  - `cmake --build build --target rc-viewer test_active_area_runtime test_combat_visuals_projectiles`
    passed.
  - `./build/test_active_area_runtime` passed.
  - `ctest --test-dir build -R "test_active_area_runtime|test_spawn_slices_runtime|test_combat_visuals_projectiles|test_objects_runtime|test_plane_contracts_runtime" --output-on-failure`
    passed 5/5 tests.
  - Coverage build in `/tmp/runec_npc_policy_cov3` with `--coverage` ran
    `test_active_area_runtime`; `gcov` reported `world.c` at `71.58%` line
    coverage. `rc_world_find_npc_near` executed 6 calls with 95% block coverage,
    and `rc_world_ensure_npc_near` executed 4 calls with 95% block coverage.
    The only uncovered new branch is the `rc_npc_spawn` capacity-failure return,
    which is a defensive failure path not practical to hit without filling the
    entire NPC array in this focused test.
  - `bash tests/benchmarks/run_backend_workloads_benchmark.sh --mode spawn --ops 20`
    passed: `active-area-loads` measured `251` loads/sec
    (`3,984,820.90` ns/load), last active area loaded `25` collision regions,
    matched `840` spawn rows, and spawned `837` NPCs.

## 2026-05-17 — Core-Owned Active Area Activation

- Change made: added `rc_world_activate_area`, `RcActiveAreaRequest`, and
  `RcActiveAreaStats` so `rc-core` owns gameplay area activation for collision
  windows and active NPC spawn slices.
- Change made: added `rc_collision_populate_map_rect`, which copies the active
  collision region window from the generated full-world collision dataset into
  `RcWorld.map`. This replaces the viewer-local `.cmap` collision loader path.
- Change made: stored the configured NPC spawn path on `RcWorld` and moved NPC
  slice clearing/reloading behind the core active-area API. Scene activation
  now clears stale NPCs/projectiles in core and reloads only the active slice.
- Change made: updated `rc-viewer` so scene selection/generation remains
  presentation-owned, but gameplay activation is delegated to `rc-core` before
  minimap/model refresh. The old viewer collision helper was removed.
- Change made: adjusted the temporary dev boss transport flow to activate the
  target area before picking the safe player tile, so destination collision is
  sourced from the correct area.
- Change made: added `tests/test_active_area_runtime.c` and changed the
  backend spawn benchmark to measure `active-area-loads` instead of direct
  spawn-slice mutation.
- Why it was made: Combat Step 1.3 requires the same backend game state to
  drive headed play and future headless RL sims. NPC slice selection and
  collision activation were previously viewer-owned gameplay policy.
- Impact:
  - Headless code can activate the same playable area without linking viewer
    code.
  - The viewer no longer directly clears/reloads `world->npcs` or loads
    collision into `RcWorld.map`.
  - Dynamic object state and richer scenario/instance bootstrap are still later
    Step 1.3 work; this pass establishes the core active-area boundary.
- Validation:
  - `cmake --build build --target rc-viewer test_active_area_runtime test_spawn_slices_runtime`
    passed.
  - `./build/test_active_area_runtime` passed.
  - `./build/test_spawn_slices_runtime` passed.
  - `ctest --test-dir build -R "test_active_area_runtime|test_spawn_slices_runtime|test_collision_tiles_runtime|test_objects_runtime|test_plane_contracts_runtime|test_combat_visuals_projectiles" --output-on-failure`
    passed 6/6 tests.
  - Coverage build in `/tmp/runec_active_area_cov2` with `--coverage` ran
    `test_active_area_runtime`; `gcov` reported `world.c` at `68.05%` line
    coverage and `collision.c` at `81.25%` line coverage. The new
    `rc_world_activate_area`, `rc_world_get_active_area`, and
    `rc_collision_populate_map_rect` functions executed.
  - `bash tests/benchmarks/run_backend_workloads_benchmark.sh --mode spawn --ops 20`
    passed: `active-area-loads` measured `297` loads/sec
    (`3,371,450.20` ns/load), last active area loaded `25` collision regions,
    matched `840` spawn rows, and spawned `837` NPCs.

## 2026-05-17 — Projectile Facing And Launch Orientation Fix

- Change made: corrected viewer combat projectile yaw so travelling magic and
  ranged projectile models face along the source-to-target vector instead of
  being flipped back toward the caster.
- Change made: launch spotanim rendering now inherits caster-to-target yaw plus
  the spotanim definition rotation, matching the actor-facing model used by
  the reference client more closely during spell windup.
- Why it was made: manual Combat Step 1.2 validation showed all projectile
  models facing the player/caster and magic launch effects sitting at an
  incorrect orientation during the cast windup.
- Outcome: Combat Step 1.2 is closed for the current pass. Remaining spell
  alpha/shading fidelity, boss animation breadth, movement/combat feel, and
  broader projectile/special parity stay in later combat steps.
- Reference notes:
  - Client3 computes projectile yaw from projectile velocity and applies actor
    yaw to actor spotanims.
  - RSMod projectile profiles provide start/end heights, delay, angle,
    progress, and timing; they do not require viewer-side hardcoded projectile
    direction fixes.
- Validation:
  - `cmake --build build --target rc-viewer test_combat_visuals_projectiles test_combat_phase10_resources_specials test_prayer_spell_actions_runtime`
    passed.
  - `./build/test_combat_visuals_projectiles` passed.
  - `./build/test_combat_phase10_resources_specials` passed.
  - `./build/test_prayer_spell_actions_runtime` passed.

## 2026-05-17 — Backend Workload Benchmarks For Architecture Audit

- Change made: added `tests/benchmarks/backend_workloads_benchmark.c`, a
  backend-only benchmark covering path-heavy agent actions, placed-object
  interactions with real collision/traversal data, rejected/edge actions, mixed
  agent loops, many-active-NPC idle/combat ticks, projectile-heavy ticks,
  current NPC spawn-slice area activation, cold full-game data load, and warm
  full-game reset.
- Change made: added
  `tests/benchmarks/run_backend_workloads_benchmark.sh`, which builds
  `rc-core`/`rc-content`, compiles the workload benchmark into
  `build/runec_backend_workloads_benchmark`, and supports
  `--mode all|path|object|spawn|load|reset|edge|mixed|npc-idle|npc-combat|projectiles`.
- Change made: updated `arch_audit.md` Step 5 with measured workload numbers
  and revised the remaining benchmark gaps.
- Why it was made: the original Step 5 audit only measured idle/simple-combat
  ticking and correctly flagged that path-heavy actions, object interactions,
  area activation, reset, and load costs were unmeasured. These workloads are
  the ones headless agents will stress when training against real game actions,
  so they need repeatable benchmarks in the repo.
- Impact:
  - We now have a reusable benchmark path for backend action and loading costs,
    separate from the small idle/combat SPS benchmark.
  - The benchmark shows base ticks are not the only performance story:
    path-heavy actions and real object interactions are currently much more
    expensive and should guide future optimization work.
  - Projectile bookkeeping is cheap at tested counts; many active NPC combat is
    meaningful but still much cheaper than real object-interaction routing.
  - Remaining benchmark gap is backend scenario reset after backend-owned
    area/scenario activation is implemented.
- Validation:
  - `cc -fsyntax-only -std=c11 -DRC_TEST_SOURCE_DIR=\"$RUNEC_ROOT\" -Irc-core -Irc-content tests/benchmarks/backend_workloads_benchmark.c`
    passed.
  - `bash -n tests/benchmarks/run_backend_workloads_benchmark.sh` passed.
  - `timeout 240 tests/benchmarks/run_backend_workloads_benchmark.sh --mode all`
    passed across the full benchmark suite.
  - `load-full-game-cold-process`: `1.062442` seconds.
  - `path-actions`: `531` actions/sec (`1,883,932.63` ns/action), `600`
    successful routes out of `800` attempts.
  - `object-interactions`: `128` interactions/sec (`7,808,282.55`
    ns/interaction).
  - `edge-actions`: `570` attempts/sec (`1,754,027.79` ns/action), `800`
    rejected or unrouted out of `800` attempts.
  - `mixed-agent-actions`: `1,155` actions/sec (`865,799.33` ns/action).
  - `many-npc-idle-ticks`: `119,158` env ticks/sec (`8,392.20` ns/env tick)
    with `512` active NPCs per env.
  - `many-npc-combat-ticks`: `14,741` env ticks/sec (`67,840.04` ns/env tick)
    with `512` active NPCs per env.
  - `projectile-heavy-ticks`: `2,555,690` env ticks/sec (`391.28` ns/env tick)
    with `64` active projectiles per env.
  - `spawn-slice-loads`: `808` slice loads/sec (`1,237,992.24` ns/load), with
    `24,110` spawn rows scanned, `840` matched, and `837` spawned in the last
    measured slice.
  - `reset-full-game-warm-data`: `9,221` resets/sec (`108,449.60` ns/reset).
  - Coverage review found no coverage artifacts in the current build
    (`find build -name '*.gcda' -o -name '*.gcno'` returned no files), so line
    coverage remains unavailable without a coverage-enabled build.

## 2026-05-15 — Rune Sources, Projectile Heights, And Boss Visual Validation

- Change made: added content-owned spell rune validation/consumption hooks to
  `RcCombatContentHooks`, with inventory-only core fallback preserved for
  worlds that do not register OSRS content.
- Change made: added first-pass OSRS rune-source handling in
  `rc-content/combat/regular_npc_combat.c`: worn unlimited elemental/nature
  sources, direct inventory runes, four-slot rune pouch state, combo rune
  validation, and combo rune consumption after full validation succeeds.
- Change made: added `RcPlayer.rune_pouch[4]` state and initialized it in
  world creation. UI/config hydration for the real rune pouch is still later
  work.
- Change made: split combat projectile launch and impact heights so magic and
  ranged presentation can mirror RSMod-style spotanim placement more closely.
  Magic launch spotanims use height `92`, magic impacts default to height
  `124`, ranged launch spotanims use height `96`, and mixed bow/ammo projectiles
  keep the cache-backed end height from the timing row.
- Change made: viewer projectile rendering now draws launch spotanims and
  removed the old magic-wide projectile scale override.
- Change made: the temporary dev boss validation path can place the focused
  boss into combat against the player, which lets boss/NPC attack animations
  and projectile rows be visually checked without adding boss gameplay rules to
  `rc-core`.
- Change made: expanded tests for staff/rune behavior, combo runes, rune pouch,
  projectile launch/impact heights, and manual/autocast state boundaries.
- Why it was made: Step 1.2 needed combat spell validation and projectile
  presentation to stop depending on inventory-only rune checks and viewer-side
  guesses. RSMod validates runes before consuming them, separates worn sources
  from inventory/pouch/combo handling, and uses explicit launch/impact heights
  for combat graphics; this pass moves RuneC toward that shape.
- Impact:
  - Standard spell casting can now use worn unlimited sources, rune pouch
    contents, and combo runes through the content hook.
  - Staff default combat remains melee/default unless a valid manual spell or
    autocast is active.
  - Magic/ranged projectile presentation has better source/impact placement and
    no longer scales every magic projectile uniformly.
  - Dev boss jumps are more useful for visual validation because the focused
    boss can immediately attack the player.
  - Remaining known gap: worn rune-source detection is currently item-name
    based in `rc-content` until exporter/database metadata exposes exact
    rune-source params.
- Validation:
  - `cc -fsyntax-only -std=c11 -Irc-core -Irc-content -Ilib/raylib/include rc-core/combat.c`
    passed.
  - `cc -fsyntax-only -std=c11 -Irc-core -Irc-content -Ilib/raylib/include rc-content/combat/regular_npc_combat.c`
    passed.
  - `cc -fsyntax-only -std=c11 -Irc-core -Irc-content -Ilib/raylib/include rc-viewer/viewer.c`
    passed.
  - `cmake --build build -j2` passed.
  - `ctest --test-dir build -R "test_combat_phase10_resources_specials|test_combat_visuals_projectiles|test_combat_phase4_weapon_styles_cycle|test_combat_phase11_validation_gate|test_interaction_engine_phase6|test_prayer_spell_actions_runtime" --output-on-failure`
    passed `6/6` tests.
  - `ctest --test-dir build --output-on-failure` passed `63/63` tests.
  - Coverage review found no coverage artifacts in the current build
    (`find build -name '*.gcda' -o -name '*.gcno'` returned no files), so line
    coverage remains unavailable without a coverage-enabled build.
  - `bash tests/benchmarks/run_sps_benchmark.sh --envs 8 --steps 1000 --warmup 100`
    passed on rerun at `2,361,778` SPS (`423.41` ns/env step). A longer, more
    stable run with `--steps 100000 --warmup 1000` passed at `2,698,324` SPS
    (`370.60` ns/env step).

## 2026-05-15 — Core Spell State Split And Staff Default Combat

- Change made: split core spell runtime state across selected spell,
  one-shot manual combat cast, autocast spell, defensive-autocast flag, and
  current spellbook.
- Change made: added `rc_player_set_spellbook` and
  `rc_player_set_autocast_spell` APIs. Spellbook changes clear selected,
  manual, and autocast state. Autocast selection validates that the spell is a
  loaded combat spell on the active spellbook and the player is wielding an
  autocast-capable weapon.
- Change made: `rc_player_select_spell` now represents UI spell targeting
  state only. It no longer sets or clears combat cast state.
- Change made: `spell-on-NPC` now creates a one-shot `manual_spell_cast`
  without changing the selected spell slot.
- Change made: combat style refresh no longer lets `selected_spell` force an
  ordinary staff into magic style. Staffs and polestaffs use their melee/default
  table unless a manual spell or valid autocast spell is active. Autocast is
  limited to staff-like/autocast-capable weapon categories.
- Change made: magic combat spell lookup now uses `manual_spell_cast` first,
  then `autocast_spell`; it no longer falls back to `selected_spell`.
- Change made: failed magic resource validation clears the active manual spell
  or autocast state and refreshes combat style, so a staff can fall back to its
  default behavior instead of freezing in invalid magic combat.
- Change made: viewer autocast UI intent now calls the new autocast API instead
  of selecting a spell.
- Change made: updated combat, projectile, prayer/spell action, and interaction
  tests so manual casts and autocast are modeled separately. Added a staff
  regression test proving selected spell does not override staff default attack,
  while valid autocast does.
- Why it was made: Step 1.2 needed OSRS-style separation between spellbook UI
  targeting, one-shot spell casts, and autocast. The previous collapsed state
  made staffs use magic just because a spell had been selected, which broke
  default staff combat and made spell validation noisy.
- Impact:
  - Ordinary staff attacks should now behave as melee/default unless the player
    manually casts a spell or has a valid autocast active.
  - Manual spell casts remain one-shot and clear after the attack.
  - Autocast remains active until cleared or invalidated by failed rune checks.
  - Rune-source completeness is still future Step 1.2 work: worn unlimited rune
    sources, rune pouch, combo runes, and staff priority are not implemented in
    this pass.
- Validation:
  - `cc -fsyntax-only -std=c11 -Ilib/raylib/include -I. rc-core/tick.c`
    passed.
  - `cc -fsyntax-only -std=c11 -Ilib/raylib/include -I. rc-core/combat.c`
    passed.
  - `cc -fsyntax-only -std=c11 -Ilib/raylib/include -I. rc-core/combat_formula.c`
    passed.
  - `cc -fsyntax-only -std=c11 -Ilib/raylib/include -I. rc-viewer/viewer.c`
    passed.
  - `cmake --build build -j2` passed.
  - `ctest --test-dir build -R "test_combat_phase4_weapon_styles_cycle|test_combat_phase10_resources_specials|test_combat_phase11_validation_gate|test_combat_visuals_projectiles|test_prayer_spell_actions_runtime|test_interaction_engine_phase6" --output-on-failure`
    passed `6/6` tests.
  - `ctest --test-dir build --output-on-failure` passed `63/63` tests.
  - Coverage review found no coverage artifacts in the current build
    (`find build -name '*.gcda' -o -name '*.gcno'` returned no files), so line
    coverage remains unavailable without a coverage-enabled build.
  - `bash tests/benchmarks/run_sps_benchmark.sh --envs 8 --steps 1000 --warmup 100`
    passed at `2,658,956` SPS (`376.09` ns/env step).

## 2026-05-15 — Combat Validation Spawn And Rune Loadout Cleanup

- Change made: updated the temporary viewer-only boss transport path in
  `rc-viewer/viewer.c` so validation jumps resolve a safe player tile near the
  target boss instead of applying a fixed `npc_size + 4` Y offset.
- Change made: the safe-tile resolver searches the local collision data for a
  walkable tile outside the focus NPC footprint, prefers the original south-side
  validation position, and slightly favors line of sight to the boss target.
- Change made: replaced the old viewer seed inventory partyhat/Torva clutter
  with a standard-combat-spell rune loadout while keeping useful combat visual
  test gear: fire/infernal capes, whip, godswords, magic shortbow/arrows, staff
  of fire, coins, and 3rd-age equipment.
- Why it was made: Step 1.2 combat visual validation needs boss jumps that do
  not spawn the player into walls, and spell validation needs runes available
  without filling the inventory with unrelated cosmetic/equipment clutter.
- Impact:
  - This is isolated to `rc-viewer`; it does not add boss transport or inventory
    rules to `rc-core` gameplay.
  - Graardor/Vorkath/KBD/Jad validation should be less noisy because player
    placement now respects collision where local collision data exists.
  - Standard spellbook combat testing now has air, water, earth, fire, mind,
    body, cosmic, chaos, nature, law, death, blood, soul, and wrath runes.
  - Rune source correctness is still a later core task: worn unlimited rune
    sources, rune pouch, combo runes, and staff priority are not solved here.
- Validation:
  - `cc -fsyntax-only -std=c11 -Ilib/raylib/include -I. rc-viewer/viewer.c`
    passed.
  - `cmake --build build -j2 --target rc-viewer` passed.
  - `ctest --test-dir build -R "test_pathfinding|test_items_bin|test_prayer_spell_actions_runtime|test_combat_visuals_projectiles" --output-on-failure`
    passed `4/4` tests.
  - `timeout 8 env RC_VIEWER_EXIT_FRAMES=2 RUNEC_DEV_TRANSPORT_DEST=vorkath ./build/rc-viewer`
    launched and exited successfully.
  - A redirected Graardor smoke attempt could not verify the transport path
    because GLFW failed to open display `:0` in that shell path before viewer
    initialization completed.
  - Coverage review found no coverage artifacts in the current build
    (`find build -name '*.gcda' -o -name '*.gcno'` returned no files), so line
    coverage for this viewer-only path remains unavailable.
  - No SPS benchmark was run because this pass changes one-shot viewer
    validation setup, not core tick/pathfinding runtime behavior.

## 2026-05-13 — Temporary Boss Transport Validation Controls

- Change made: added a temporary viewer/dev-only `DEV BOSS` transport panel to
  `rc-viewer/viewer.c` for combat visual validation.
- Change made: added direct validation destinations for Varrock, General
  Graardor, King Black Dragon, Vorkath, and Fight Caves/Jad. Coordinates are
  backed by local NPC spawn/reference data and b237 NPC ids from the local
  symbol dump.
- Change made: the transport control uses the existing scene reload,
  active-plane, NPC spawn-slice, and NPC model reload paths. It injects a
  focus boss NPC only when the normal slice does not contain that instance
  boss, keeping this as viewer validation state rather than `rc-core`
  gameplay.
- Change made: added `RUNEC_DEV_TRANSPORT_DEST=<key>` smoke-test support and
  `RUNEC_DEV_TRANSPORT_PANEL=0` to hide the temporary panel.
- Why it was made: combat visual validation needs fast travel to representative
  bosses and encounter spaces without adding gameplay rules to the viewer or
  spending manual time crossing the world.
- Impact:
  - Manual validation can now jump directly to `varrock`, `graardor`, `kbd`,
    `vorkath`, or `jad`.
  - `rc-core` combat rules were not expanded by this temporary transport UI.
  - The panel is intentionally temporary and should be removed once combat
    validation coverage is stable.
- Validation:
  - `cmake --build build` passed.
  - `./build/test_combat_visuals_projectiles` passed.
  - Viewer smoke passed for `RUNEC_DEV_TRANSPORT_DEST=varrock`,
    `graardor`, `kbd`, `vorkath`, and `jad`.
  - `ctest --test-dir build --output-on-failure` passed with `63/63` tests.
  - Coverage review: no coverage instrumentation is configured in this build;
    `rg "coverage|gcov|llvm-cov|profraw|--coverage" CMakeLists.txt tests rc-core rc-content rc-viewer -g '!*.md' -S`
    returned only a source comment.
  - `bash tests/benchmarks/run_sps_benchmark.sh --envs 8 --steps 1000 --warmup 100`
    passed at `2,727,767` SPS (`366.60` ns/env step).

## 2026-05-13 — Combat Visual Coverage Step 1

- Change made: extended `tools/export_combat_visuals.py` so generated combat
  visual rows now include curated b237 spell, NPC/boss, and special-attack
  coverage in addition to the existing RSMod item/ranged and elemental spell
  rows.
- Change made: added standard non-elemental spell rows for bind/snare/
  entangle, crumble undead, Iban Blast, Magic Dart, god spells, vulnerability,
  enfeeble, stun, confuse, weaken, curse, and Tele Block.
- Change made: added Ancient combat rows for smoke, shadow, blood, and ice
  rush/burst/blitz/barrage where current b237 travel or impact spotanims
  exist. Lunar utility spells intentionally do not generate fake combat rows.
- Change made: added Arceuus combat-effect rows for grasp, demonbane, and
  corruption spells as impact/cast effect rows.
- Change made: added first-pass NPC/boss projectile rows for General Graardor,
  Sergeant Steelwill, Sergeant Grimspike, King Black Dragon, Vorkath, and
  Fight Caves/Jad using b237 dump names and reference repo behavior notes.
- Change made: added first-pass `special` visual rows for dragon longsword,
  godswords, and toxic blowpipe. Multi-projectile specials such as dark bow
  remain explicit follow-up work because the current runtime event shape should
  not flatten them into one projectile.
- Change made: extended `rc-core/combat_visuals.*` with a `special` visual kind
  and `rc_combat_visual_for_special_item`.
- Change made: updated `rc-core/combat.c` so player special attacks can select
  special visual rows where the current schema can represent them, and so
  impact-only non-special spell visuals can produce renderable combat events.
- Change made: expanded `tests/test_combat_visuals_projectiles.c` to verify
  special-row lookup and generated b237 rows for Fire Blast, Iban Blast,
  Ice Barrage, Ghostly Grasp, Vorkath, Graardor, and dragon longsword special.
- Why it was made: combat presentation needed a broader data-backed foundation
  before adding boss transport validation and deeper runtime polish. The
  exporter now keeps these rows deterministic and source-resolved instead of
  hand-editing `combat_visuals.tsv`.
- Impact:
  - Generated `data/defs/combat_visuals.tsv` now has 1,218 source rows:
    1,144 item rows, 60 spell rows, 8 NPC rows, and 6 special rows.
  - The main RuneC repo owns exporter/runtime/test changes. The regenerated
    `data/` artifact remains in the local RuneC-DB boundary and was not added
    to the main repo.
  - Known remaining combat visual work after Step 1.1:
    multi-projectile/multi-hit specials, exact player-side special spotanim
    rendering, broader boss rows, spell/autocast state, staff defaults, and
    visual polish for launch/impact offsets.
- Validation:
  - `python3 -m py_compile tools/export_combat_visuals.py` passed.
  - `python3 tools/export_combat_visuals.py` passed and regenerated
    `data/defs/combat_visuals.tsv`.
  - `cmake --build build` passed.
  - `./build/test_combat_visuals_projectiles` passed.
  - `ctest --test-dir build --output-on-failure` passed with `63/63` tests.
  - Coverage review: no coverage instrumentation is configured in this build;
    `rg "coverage|gcov|llvm-cov|profraw|--coverage" CMakeLists.txt tests rc-core rc-content rc-viewer -g '!*.md' -S`
    returned only a source comment.
  - `bash tests/benchmarks/run_sps_benchmark.sh --envs 8 --steps 1000 --warmup 100`
    passed at `1,754,295` SPS (`570.03` ns/env step).

## 2026-05-12 — Local Archive Folder For Completed Docs

- Change made: created an ignored local `archive/` directory for completed
  archival Markdown notes.
- Change made: moved the completed local archive docs into `archive/`:
  `cache_pipeline_cleanup.md`, `interaction_engine.md`,
  `loot_interaction.md`, and `ui_cleanup.md`.
- Change made: updated `work.md` and `work_highlevel.md` so the active
  roadmap no longer links to those root-level completed docs. Completed
  implementation history remains in `changelog.md`.
- Why it was made: completed planning/reference docs should stay available
  locally without being part of the public GitHub-facing repo tree or active
  work roadmap.
- Impact:
  - The main repo now shows tracked deletions for the previously tracked root
    archive docs.
  - Local historical notes are still present under ignored `archive/`.
  - Active planning remains concentrated in `work.md` and
    `work_highlevel.md`.
- Validation:
  - Documentation/file-organization change only; no build, unit test,
    coverage, or benchmark validation was required.

## 2026-05-12 — Test Directory Consolidation

- Change made: moved the SPS benchmark helper from `testing/` into
  `tests/benchmarks/` so the repository has one source-controlled test
  directory.
- Change made: updated `tests/benchmarks/run_sps_benchmark.sh` to resolve the
  repo root from its new location and compile
  `tests/benchmarks/sps_benchmark.c`.
- Change made: updated the root `README.md` to describe `tests/` as the single
  home for runtime tests, regression tests, and benchmark helpers.
- Why it was made: having both `testing/` and `tests/` made the source layout
  look split even though `testing/` only contained the SPS benchmark. Keeping
  benchmarks under `tests/benchmarks/` makes the repo structure easier to scan.
- Impact:
  - The benchmark command is now
    `bash tests/benchmarks/run_sps_benchmark.sh`.
  - No runtime behavior changed.
- Validation:
  - `bash -n tests/benchmarks/run_sps_benchmark.sh` passed.
  - `cc -fsyntax-only -std=c11 -Irc-core -Irc-content tests/benchmarks/sps_benchmark.c`
    passed.
  - `git diff --check` passed.
  - `bash tests/benchmarks/run_sps_benchmark.sh --envs 8 --steps 1000 --warmup 100`
    passed at `2,508,036` SPS (`398.72` ns/env step).
  - `ctest --test-dir build --output-on-failure` passed with `63/63`
    tests passing.
  - Coverage review: no coverage instrumentation is configured in this build;
    `rg "coverage|gcov|llvm-cov|profraw|--coverage" CMakeLists.txt tests -S`
    returned no matches.

## 2026-05-12 — Cache Pipeline Cleanup Closeout And Doc Rename

- Change made: marked the cache pipeline cleanup as complete for the current
  pass and renamed the local historical cleanup doc from `code_cleanup.md` to
  `cache_pipeline_cleanup.md`.
- Change made: rewrote `cache_pipeline_cleanup.md` from an active plan into a
  completed technical history. The doc now records why the b237 pipeline was
  rebuilt, what source inputs it uses, how the shared `rc_cache` foundation
  works, what exporters and runtime consumers were moved to b237, what visual
  and animation paths were corrected, what dynamic-object and linked-below
  fixes landed, what obsolete paths were removed, and what work remains
  deferred in `work.md`.
- Change made: simplified `work.md` and `work_highlevel.md` so the active
  roadmap now points at combat fidelity as the next lane and treats cache
  pipeline, UI cleanup, Interaction Engine v1, and loot/ground-item work as
  closed references.
- Change made: refreshed the tracked README set. The root `README.md` now reads
  as the user-facing GitHub overview, while `rc-core/README.md`,
  `rc-viewer/README.md`, and `rc-content/**/README.md` document component
  ownership, runtime boundaries, and the `RuneC-DB` data split.
- Change made: kept `cache_pipeline_cleanup.md` local-only. The main repo still
  ignores root Markdown files except the approved README files; the cleanup
  history can remain in the checkout without being pushed to GitHub.
- Why it was made: the cleanup work is no longer an active implementation
  plan. Keeping the old plan shape would make the repo roadmap ambiguous and
  risk pulling future work back into an already-closed cleanup lane.
- Impact:
  - Current active planning is concentrated in `work.md`.
  - `cache_pipeline_cleanup.md` now acts as local technical history:
    implementation rationale, removed paths, validation shape, and deferred
    follow-up boundaries without becoming part of the public GitHub tree.
  - README files now reflect the current parent-repo versus `RuneC-DB`
    ownership boundary.
  - No runtime, exporter, generated-data, or build behavior changed.
- Validation:
  - Documentation-only change; no build, unit test, coverage, or benchmark
    validation was required.

## 2026-05-12 — Linked-Below Object Placement Interaction Fix

- Change made: fixed `tools/export_object_placements.py` so
  `data/defs/object_placements.bin` stores gameplay/collision planes after the
  b237 `LINK_BELOW` down-height rule instead of raw cache planes.
- Change made: regenerated the local object placement index and report. The
  full-world export now applies `265,865` linked-below plane adjustments and
  skips `114,971` raw plane-0 rows that lower below scene plane 0.
- Change made: added a regression in `tests/test_objects_runtime.c` for the
  Varrock Museum basement return stairs. Object `24427` now exists on scene
  plane 0 at `(1758,4959)`, not raw cache plane 1, and dispatches its
  `Walk-up` traversal back to `(3258,3452,0)`.
- Why it was made: the renderer was already drawing linked-below basement
  stairs on scene plane 0, and traversal rows were authored for scene plane 0,
  but the gameplay placement index still exposed the same loc on raw cache
  plane 1. The viewer/core therefore could not resolve an interaction target
  for some visible transport objects.
- Impact:
  - Linked-below transport objects become pickable/interactable on the same
    scene plane used by traversal and collision.
  - The fix is systemic for bridge/basement-style areas and avoids
    museum-specific object ids in runtime code.
- Validation:
  - `python3 tools/export_object_placements.py` passed and regenerated the
    local placement index/report.
  - `python3 -m py_compile tools/export_object_placements.py` passed.
  - `cmake --build build` passed.
  - Focused object runtime regression passed: `./build/test_objects_runtime`.
  - Museum-start viewer smoke passed:
    `timeout 20 env RC_VIEWER_QUIET=1 RC_VIEWER_EXIT_FRAMES=120 RUNEC_PLAYER_START_X=1759 RUNEC_PLAYER_START_Y=4958 ./build/rc-viewer`.
  - Full regression passed: `ctest --test-dir build --output-on-failure`
    reported `63/63` tests passing.
  - Coverage review: no coverage instrumentation is configured in this build;
    `rg "coverage|gcov|llvm-cov|profraw|--coverage" CMakeLists.txt testing -S`
    returned no matches.
  - `bash testing/run_sps_benchmark.sh` passed at `2,729,040` SPS
    (`366.43` ns/env step).

## 2026-05-12 — Linked-Below Terrain And Composite Loc Export Fix

- Change made: fixed `tools/cache_pipeline/export_terrain.py` so visible
  terrain mesh generation uses the same `LINK_BELOW` visual-plane rule already
  used by object placement export. When cache plane `N + 1` marks a tile with
  `LINK_BELOW`, that plane now supplies the visible terrain polygons for scene
  plane `N`.
- Change made: fixed `tools/cache_pipeline/export_objects.py` to use the OSRS
  client draw-level rule for rendering instead of collision bridge lowering.
  Plane-0 locs now remain visible on scene plane 0 even when cache plane 1 has
  `LINK_BELOW`, while upper linked-below locs still lower correctly.
- Change made: fixed loc model selection so all model ids matching a placed
  loc type are merged into one composite model before recolors, retextures,
  scale, rotation, and placement transforms are applied. The exporter no
  longer drops the second half of multi-part b237 locs such as stairs, ramps,
  shelves, stalls, and other composite objects.
- Why it was made: Varrock Museum basement transports to the underground
  mapsquare around `1759,4958,0`, but the cache stores its visible floor on
  plane 1 with `LINK_BELOW`. The terrain exporter was not sourcing those
  visible polygons, and the object exporter was using collision-style
  bridge-lowering for render filtering, which dropped the lower wall/ramp
  layer. The same area also exposed a broader b237 issue where multi-model loc
  definitions were exported as only their first model.
- Impact:
  - Generated scene slices now render linked-below basement/bridge-style floor
    geometry instead of only applying linked-below heightmap data.
  - Object export now preserves the client-visible linked-below object layer
    and composite loc geometry. This is systemic and not museum-specific.
  - The local generated museum scene cache
    `data/regions/scene_cache/scene_1664_4864_r1*` was regenerated. `data/`
    remains RuneC-DB generated output, not main-repo source.
- Validation:
  - `python3 -m py_compile tools/cache_pipeline/export_terrain.py tools/cache_pipeline/export_scene_slice.py tools/cache_pipeline/export_objects.py`
    passed.
  - Focused diagnostic confirmed Varrock Museum basement tiles such as
    `1759,4958` now source visible terrain from cache plane 1 overlay `56`
    through the `LINK_BELOW` rule.
  - Focused scene export passed:
    `timeout 240 python3 tools/cache_pipeline/export_scene_slice.py --center-x 1759 --center-y 4958 --radius-regions 1 --output-prefix /tmp/runec_museum_scene --planes 0`.
  - Focused object export for region `(27,77)` now keeps all `3,584` scene-0
    placements instead of dropping `LINK_BELOW`-adjacent plane-0 walls/ramps,
    and it merges multi-part loc models before mesh export.
  - The same export against the local scene cache passed and produced
    `19,200` terrain vertices plus `3,589` object placements for
    `scene_1664_4864_r1`, including the linked-below floor mesh and composite
    object geometry.
  - Museum-start viewer smoke passed:
    `timeout 20 env RC_VIEWER_QUIET=1 RC_VIEWER_EXIT_FRAMES=120 RUNEC_PLAYER_START_X=1759 RUNEC_PLAYER_START_Y=4958 ./build/rc-viewer`.
    The viewer regenerated and loaded
    `data/regions/scene_cache/scene_1664_4864_r1.terrain` with `19,200`
    vertices.
  - Full regression passed: `ctest --test-dir build --output-on-failure`
    reported `63/63` tests passing.
  - Coverage review: no coverage instrumentation is configured in this build;
    `rg "coverage|gcov|llvm-cov|profraw|--coverage" CMakeLists.txt testing -S`
    returned no matches.
  - `bash testing/run_sps_benchmark.sh` passed at `2,843,655` SPS
    (`351.66` ns/env step).

## 2026-05-12 — Dynamic Object Transport Fixes

- Change made: fixed active dynamic object transport lookup in
  `rc-core/tick.c`. Open/replaced placements now participate in traversal
  matching, so an opened manhole can resolve the `Climb-down` edge attached to
  its active object id.
- Change made: tightened dynamic object action detection. `Open`/`Unlock` and
  `Close`/`Lock` are now matched as exact action names, preventing
  `Climb-down` from being interpreted as a close action.
- Change made: object traversal interactions now route to authored traversal
  source tiles when those rows exist, instead of relying only on generic
  footprint adjacency. This keeps stairs, ladders, manholes, and similar
  transports aligned with their exported source tile.
- Change made: non-wall dynamic objects with replacement stages are now
  exported into dynamic object sidecars in
  `tools/cache_pipeline/export_objects.py`; non-wall replacements keep their
  original tile and rotation. The default Varrock region assets in
  `data/regions/varrock*` were regenerated from the b237 cache so the viewer
  uses the new sidecars.
- Change made: reduced viewer object-pick search radius from 16 tiles to 1
  tile and made streamed generated scenes default to a 1-region radius. This
  avoids broad nearby object picks while reducing partial visual loads after
  transports into adjacent-region interiors.
- Why it was made: manual validation found three remaining cleanup blockers:
  some upward stair interactions did not route, the open Varrock manhole did
  not visually change or apply `Climb-down`, and some transported areas loaded
  visibly incomplete terrain/object slices.
- Impact:
  - Dynamic object state remains placement-local and core-owned.
  - Viewer rendering observes active state instead of deciding behavior.
  - Initial Varrock generated assets now include non-wall dynamic replacement
    rows for objects such as manholes.
- Validation:
  - `python3 -m py_compile tools/cache_pipeline/export_objects.py tools/cache_pipeline/export_scene_slice.py`
    passed.
  - `git diff --check` passed.
  - `cmake --build build --target test_objects_runtime rc-viewer` passed.
  - Focused regression passed:
    `ctest --test-dir build --output-on-failure -R "objects_runtime|traversal_runtime|interaction_engine_phase8"`.
  - Full regression passed: `ctest --test-dir build --output-on-failure`
    reported `63/63` tests passing.
  - Regenerated the default Varrock viewer assets:
    `timeout 240 python3 tools/cache_pipeline/export_scene_slice.py --center-x 3213 --center-y 3428 --radius-regions 2 --output-prefix data/regions/varrock --planes 0,1,2,3`.
  - Viewer smoke passed:
    `timeout 15 env RC_VIEWER_QUIET=1 RC_VIEWER_EXIT_FRAMES=120 ./build/rc-viewer`
    reached `Viewer ready` and exited cleanly.
  - Coverage review: no coverage instrumentation is configured in this build;
    `rg "coverage|gcov|llvm-cov|profraw|--coverage" CMakeLists.txt testing -S`
    returned no matches and no coverage artifacts were present.
  - `bash testing/run_sps_benchmark.sh` passed at `2,848,694` SPS
    (`351.04` ns/env step).

## 2026-05-10 — Cache Pipeline Steps 13 And 14

- Change made: simplified generated scene orchestration. `tools/cache_pipeline/export_scene_slice.py`
  now calls explicit b237 terrain/object export APIs in-process instead of
  spawning `export_terrain.py` and `export_objects.py` subprocesses.
- Change made: added explicit modern export APIs in
  `tools/cache_pipeline/export_terrain.py` and
  `tools/cache_pipeline/export_objects.py`, with normal defaults pointing at
  the repo-local b237 OpenRS2 cache. Legacy 317 export paths remain available
  only through an explicit `--cache` argument.
- Change made: removed ignored b237 `--keys` plumbing from the active dense
  collision exporters and pointed their defaults at the repo-local cache.
- Change made: retired unused obsolete viewer export entry points:
  `tools/export_objects_bridge.py`, `tools/export_objects_all_planes.sh`,
  `tools/cache_pipeline/export_npcs.py`, and
  `tools/cache_pipeline/export_sprites.py`.
- Change made: `rc-viewer/viewer.c` now invalidates generated scene object
  slices when the terrain exporter changes, not only when object exporter code
  or object definition binaries change.
- Why it was made: Steps 13 and 14 close the cache cleanup pass by making the
  active b237 pipeline explicit, removing wrapper chains, and deleting unused
  legacy exporter entry points without removing shared compatibility helpers
  that active exporters still import.
- Impact:
  - Normal scene export no longer shells out through compatibility wrappers.
  - Generated scene slices still use the same b237 cache, terrain, object,
    atlas, and object-animation contracts.
  - Remaining compatibility code is retained only where active tools still
    depend on it.
- Validation:
  - `python3 -m py_compile tools/cache_pipeline/export_scene_slice.py tools/cache_pipeline/export_terrain.py tools/cache_pipeline/export_objects.py tools/cache_pipeline/export_collision_map_modern.py tools/export_collision.py`
    passed.
  - `python3 tools/cache_pipeline/validate_b237_cache.py --region 50,53 --region 48,53`
    passed.
  - `python3 tools/cache_pipeline/rc_cache/smoke.py --synthetic --cache tools/cache_pipeline/source/current_fightcaves_demo/data/cache --require-b237-configs`
    passed.
  - `timeout 60 python3 tools/cache_pipeline/export_scene_slice.py --center-x 3210 --center-y 3424 --radius-regions 0 --output-prefix /tmp/runec_step13_scene --planes 0`
    passed and generated terrain, objects, atlas, object animation rows, and
    animated object models.
  - Dense collision smoke passed:
    `python3 tools/export_collision.py --regions 50,53 --output /tmp/runec_step13_collision.cmap`.
  - Modern collision exporter smoke passed:
    `python3 tools/cache_pipeline/export_collision_map_modern.py --regions 50,53 --output /tmp/runec_step13_collision_modern.cmap`.
  - `cmake --build build --target rc-viewer test_objects_runtime test_traversal_runtime`
    passed.
  - Focused regression passed:
    `ctest --test-dir build --output-on-failure -R "objects_runtime|object_placements_bin|object_defs_bin|traversal_runtime|interaction_engine_phase|modular_loading|cache"`.
  - Full regression passed: `ctest --test-dir build --output-on-failure`
    reported `63/63` tests passing.
  - Viewer smoke passed:
    `timeout 10 env RC_VIEWER_QUIET=1 RC_VIEWER_EXIT_FRAMES=120 ./build/rc-viewer`
    reached `Viewer ready` and exited cleanly.
  - Coverage review: no coverage instrumentation is configured in this build;
    `rg "coverage|gcov|llvm-cov|profraw|--coverage" CMakeLists.txt testing -S`
    returned no matches and no coverage artifacts were present.
  - `bash testing/run_sps_benchmark.sh` passed at `2,923,791` SPS
    (`342.02` ns/env step).

## 2026-05-10 — Vertical Climb Return Fallback

- Change made: added a narrow runtime fallback in `rc-core/tick.c` for
  climb-up/climb-down objects that have cache actions and placed counterparts
  but no authored traversal row. Authored traversal edges still win first; the
  fallback first infers the return edge from an authored opposite-direction
  climb edge whose landing tile matches the clicked object's footprint, then
  falls back to exact same-anchor climb pairs on other planes.
- Change made: added regression coverage in `tests/test_objects_runtime.c` for
  missing-edge vertical pairs `11801`/`11802` at `(3261,3459)` and
  `60731`/`60732` at `(3217,3398)`, authored offset stair source rows, and a
  missing direct-source staircase return (`56230` at `(3204,3207)`).
- Why it was made: validation found stair/ladder objects where going down
  worked but going up did nothing because the local traversal dump omitted the
  up edge even though the b237 placement/action data had the matching climb
  pair.
- Impact: fixes the remaining "some stairs cannot go up" class without broad
  same-id/radius fallback, random tile transports, or viewer-owned behavior.
- Validation:
  - `cc -fsyntax-only -Irc-core rc-core/tick.c` passed.
  - `cmake --build build --target test_objects_runtime rc-viewer` passed.
  - `./build/test_objects_runtime` passed.
  - Focused regression passed:
    `ctest --test-dir build --output-on-failure -R "objects_runtime|object_placements_bin|object_defs_bin|traversal_runtime|interaction_engine_phase|modular_loading"`.
  - Full regression passed: `ctest --test-dir build --output-on-failure`
    reported `63/63` tests passing.
  - Viewer smoke passed:
    `timeout 10 env RC_VIEWER_QUIET=1 RC_VIEWER_EXIT_FRAMES=120 ./build/rc-viewer`
    reached `Viewer ready` and exited cleanly.
  - Coverage review: no coverage instrumentation is configured in this build;
    `rg "coverage|gcov|llvm-cov|profraw|--coverage" CMakeLists.txt testing -S`
    returned no matches and no coverage artifacts were present.
  - `bash testing/run_sps_benchmark.sh` passed at `2,879,779` SPS
    (`347.25` ns/env step).
  - `git diff --check` passed.

## 2026-05-10 — Multi-Option Staircases And Same-Id Gates

- Change made: fixed object traversal selection for duplicated
  source/option rows. `rc-core/tick.c` now scores matching traversal edges by
  the clicked option direction before distance, so multi-floor objects such as
  the Cooking Guild staircase resolve `Climb-up` to the higher plane and
  `Climb-down` to the lower plane instead of taking the first exported row.
- Change made: fixed same-id dynamic gate rendering. Door/gate behavior rows
  are now kept even when the source data has no replacement object id, dynamic
  wall placements are exported into sidecars, and paired placement lookup no
  longer rejects the mate when its behavior lacks `next_loc_stage`.
- Change made: viewer scene-cache reuse now checks object/exporter/definition
  mtimes before accepting an existing generated scene slice. Stale
  pre-sidecar scene files are regenerated automatically instead of hiding new
  dynamic gate rows behind old static meshes.
- Change made: added regression coverage for same-id paired gates and the
  Cooking Guild middle-floor staircase up/down options in
  `tests/test_objects_runtime.c`.
- Why it was made: Step 12 validation found that some gates did not visually
  open and objects with multiple traversal options could dispatch the wrong
  destination. The interaction engine needs exact option-specific behavior per
  placement before exporter orchestration cleanup.
- Validation:
  - `python3 -m py_compile tools/cache_pipeline/export_objects.py` passed.
  - `cc -fsyntax-only -Irc-core rc-core/tick.c` passed.
  - `cmake --build build` passed.
  - Focused regression passed:
    `ctest --test-dir build --output-on-failure -R "objects_runtime|object_placements_bin|object_defs_bin|interaction_engine_phase|traversal_runtime|modular_loading"`.
  - Full regression passed: `ctest --test-dir build --output-on-failure`
    reported `63/63` tests passing.
  - Viewer smoke passed:
    `timeout 10 env RC_VIEWER_QUIET=1 RC_VIEWER_EXIT_FRAMES=120 ./build/rc-viewer`
    reached `Viewer ready` and exited cleanly.
  - Coverage review: no coverage instrumentation is configured in this build;
    `rg "coverage|gcov|llvm-cov|profraw|--coverage" CMakeLists.txt testing -S`
    returned no matches and no coverage artifacts were present.
  - `bash testing/run_sps_benchmark.sh` passed at `2,847,255` SPS
    (`351.22` ns/env step).
  - `git diff --check` passed.

## 2026-05-10 — Stable Placement Keys And Object Action Effects

- Change made: promoted object placement export to `OPLC` v2 in
  `tools/export_object_placements.py`. Rows now include stable nonzero
  placement keys derived from object id, tile, map square, plane, shape,
  rotation, flags, and duplicate ordinal. `rc-core/objects.c` loads v1 and v2
  placement files, preserves v2 keys, and computes a nonzero fallback key for
  legacy v1 rows.
- Change made: active object state now carries `placement_key` plus an
  `animation_timer`. Dynamic door/gate/resource/object states retain the exact
  placement anchor needed for save/instance/script consumers instead of relying
  only on object id and tile.
- Change made: object-side action animation playback now lives in
  `rc-core/tick.c`. Non-door objects with cache animation ids can start a
  timed active-state animation, dynamic loc open/close actions can preserve
  object animation timing, and `rc-viewer/viewer.c` renders the active object
  animation id while the timer is live.
- Change made: representative agility-style shortcuts now route through the
  generic interaction path. The runtime rejects agility-like traversal when
  agility is unavailable, applies a player action lock and delayed traversal on
  success, and awards a small agility XP event through `RC_SUB_SKILLS`.
- Change made: refreshed generated object placement, object behavior, and
  gathering-node reports against the current contracts. `tests/test_objects_runtime.c`
  now covers placement keys, object animation timing, close action state,
  paired mutation, delayed traversal, and agility shortcut success/failure.
- Why it was made: Step 12 needed the remaining generic dynamic-object parity
  layer before moving to exporter orchestration. Runtime behavior now has exact
  placement identity, object/player effects, and representative shortcut
  semantics without viewer-owned gameplay hacks or broad object-id mutation.
- Upstream/downstream impact:
  - Dynamic object save/state, future instances, and object scripts can key off
    stable placement identity.
  - Viewer rendering observes core-owned active object animation state.
  - Sound/open-close audio remains deferred in `work.md`; Step 12 no longer
    blocks on audio metadata.
  - Further object scripts should add metadata only when a concrete behavior
    proves the current def/behavior/traversal/key contract is insufficient.
- Validation:
  - `cc -fsyntax-only -Irc-core rc-core/objects.c` passed.
  - `cc -fsyntax-only -Irc-core rc-core/tick.c` passed.
  - `python3 -m py_compile tools/export_object_placements.py tools/export_gathering_nodes.py tools/export_object_behaviors.py tools/cache_pipeline/export_objects.py`
    passed.
  - `python3 tools/export_object_placements.py`,
    `python3 tools/export_object_behaviors.py`, and
    `python3 tools/export_gathering_nodes.py` regenerated local outputs and
    reports successfully.
  - Focused regression passed:
    `ctest --test-dir build --output-on-failure -R "objects_runtime|object_placements_bin|object_defs_bin|interaction_engine_phase|traversal_runtime|modular_loading|skills_runtime"`.
  - Full regression passed: `ctest --test-dir build --output-on-failure`
    reported `63/63` tests passing.
  - Viewer smoke passed:
    `timeout 10 env RC_VIEWER_QUIET=1 RC_VIEWER_EXIT_FRAMES=120 ./build/rc-viewer`
    reached `Viewer ready` and exited cleanly.
  - Coverage review: no coverage instrumentation is configured in this build;
    `rg "coverage|gcov|llvm-cov|profraw|--coverage" CMakeLists.txt testing -S`
    and coverage artifact search returned no results. Changed object and
    traversal paths are covered by the focused tests above, but line coverage
    remains unavailable.
  - `bash testing/run_sps_benchmark.sh` passed at `2,793,887` SPS
    (`357.92` ns/env step).
  - `git diff --check` passed.

## 2026-05-10 — Placement-Local Pair Mutation And Action Timing

- Change made: object behavior export now marks source-derived dynamic pairs
  from the Joshua-F dump symbol map. `OBHV` behavior rows carry left/right pair
  flags for paired gates and double-door style placements, and the behavior
  report now summarizes paired dynamic loc coverage.
- Change made: `rc-core` dynamic object state now opens paired doors/gates by
  mutating only the clicked placement plus its exact adjacent mate. Runtime
  transforms and exported sidecar rows use the same pair-aware rotation and
  translation rules, including the special right-gate open offset.
- Change made: player action timing now has explicit action locks, temporary
  action animation ids, delayed traversal state, and viewer-side rendering for
  player action animations. Ladder/stair interactions with climb metadata delay
  transport briefly while showing the climb animation; shortcut-like traversal
  has the same generic delay foundation.
- Follow-up fix: delayed traversal now clears its transient action lock when
  the transport resolves, and immediate traversal clears any transient object
  interaction lock after applying. This prevents a stale one-tick lock from
  rejecting the next valid object interaction.
- Change made: `tests/test_objects_runtime.c` now covers source-derived pair
  flags, placement-local paired mutation, single-door and paired-gate active
  replacement close actions, delayed traversal state, action animation state,
  and post-transport interaction readiness.
- Why it was made: remaining Step 12 work needs the generic interaction system
  to support exact placement-local state changes and composable effects without
  viewer-owned behavior or broad object-id mutation.
- Validation:
  - `python3 -m py_compile tools/export_object_behaviors.py tools/cache_pipeline/export_objects.py`
    passed.
  - `cc -fsyntax-only -Irc-core rc-core/tick.c` passed.
  - `python3 tools/export_object_behaviors.py` regenerated
    `data/defs/object_behaviors.bin` and `tools/reports/object_behaviors.txt`
    with `262` paired dynamic loc rows.
  - `python3 tools/cache_pipeline/export_scene_slice.py --center-x 3210 --center-y 3424 --radius-regions 2 --output-prefix data/regions/varrock --planes 0,1,2,3`
    passed.
  - `cmake --build build` passed.
  - Focused Step 12 regression passed:
    `ctest --test-dir build --output-on-failure -R "objects_runtime|object_defs_bin|object_placements_bin|interaction_engine_phase|traversal_runtime|modular_loading"`.
  - Full regression passed: `ctest --test-dir build --output-on-failure`
    reported `63/63` tests passing.
  - Viewer smoke passed:
    `timeout 10 env RC_VIEWER_QUIET=1 RC_VIEWER_EXIT_FRAMES=120 ./build/rc-viewer`
    reached `Viewer ready` and exited cleanly.
  - `bash testing/run_sps_benchmark.sh` passed at `2,888,073` SPS
    (`346.25` ns/env step).
  - `git diff --check` passed.

## 2026-05-09 — Source-Backed Dynamic Object Metadata And Rendering

- Change made: promoted object definition export to `ODEF` v2. The b237 object
  decoder now preserves opcode `249` int params plus clip/model-clipped/hollow,
  support-items, ambient-sound, randomize-animation, and deferred-animation
  fields. Runtime loading remains backward compatible with v1 and exposes
  `rc_object_def_param_int`.
- Change made: promoted object behavior export to `OBHV` v2. The exporter now
  derives `next_loc_stage` from Joshua-F dump symbols plus RSMod-style generic
  door/gate pairs, reserves open/close sound fields, and exports default
  ladder/stair `climb_anim` metadata (`human_reachforladder`, sequence `828`).
- Change made: door open state in `rc-core/tick.c` now uses the exported
  source-backed replacement id and RSMod-style open coordinate/rotation
  translation instead of leaving the visual state as the base object id.
- Change made: object mesh export now emits dynamic door/gate placements as
  `.oanim` sidecar rows instead of baking them permanently into the static mesh.
  `rc-viewer` draws base or replacement sidecar rows according to
  `rc-core` active object state, so one changed placement can be hidden/drawn
  without rebuilding the scene or rendering broad sidecars.
- Change made: regenerated the Varrock scene slice locally so `./build/rc-viewer`
  uses the new dynamic object sidecar contract.
- Change made: refreshed object definition, behavior, and placement reports and
  updated tests for `ODEF`/`OBHV` v2 metadata, exact-placement test fixtures,
  current skilling traversal loading, and source-backed door replacement state.
- Why it was made: Step 12 requires generic dynamic-object behavior to be data
  backed and placement-local. Doors and gates need to change the exact clicked
  placement visually and in collision state without viewer-owned gameplay hacks
  or scene-wide mutation by object id.
- Upstream/downstream impact:
  - Runtime object scripts can now query cache object params and behavior
    metadata directly from local C-owned data.
  - Door visual replacement is now source-backed for exported pairs and observed
    by the viewer through core state.
  - Remaining Step 12 work is paired gate mutation, action animation timing,
    delay/input-lock effects, representative agility shortcut behavior, stable
    placement keys, and source-backed open/close sound ids.
- Validation:
  - `python3 -m py_compile tools/cache_pipeline/rc_cache/definitions.py tools/export_object_defs.py tools/export_object_behaviors.py tools/export_object_placements.py tools/cache_pipeline/export_objects.py`
    passed.
  - `python3 tools/export_object_defs.py`, `python3 tools/export_object_behaviors.py`,
    `python3 tools/export_object_placements.py`, and
    `python3 tools/cache_pipeline/export_scene_slice.py --center-x 3210 --center-y 3424 --radius-regions 2 --output-prefix data/regions/varrock --planes 0,1,2,3`
    passed.
  - `cmake --build build` passed.
  - Focused object/interaction regression passed:
    `ctest --test-dir build --output-on-failure -R "objects_runtime|object_defs_bin|interaction_engine_phase|modular_loading"`.
  - Full regression passed: `ctest --test-dir build --output-on-failure`
    reported `63/63` tests passing.
  - Viewer smoke passed:
    `timeout 10 env RC_VIEWER_QUIET=1 RC_VIEWER_EXIT_FRAMES=120 ./build/rc-viewer`
    reached `Viewer ready` and exited cleanly.
  - `bash testing/run_sps_benchmark.sh` passed at `2,903,459` SPS
    (`344.42` ns/env step).
  - `git diff --check` passed.

## 2026-05-09 — Placement-Local Object State And Reach Validation

- Follow-up fix: new spatial object/NPC interactions now clear any old route
  and immediately compute the approach route with `process_player_interaction`
  in validation-only mode. Reachable out-of-range objects/NPCs now start
  walking toward the correct reach tile immediately; unreachable targets cancel
  without moving or firing their action.
- Change made: added object/NPC regressions for immediate approach routing:
  reachable far object interaction queues a route, fully blocked object
  interaction fails without movement, and far NPC option interaction queues an
  approach route before the next tick.
- Follow-up fix: bounded viewer object picking for normal clicks to a
  tile-local placement scan around the cursor raycast tile instead of scanning
  every object placement in the loaded scene. The picker now uses cheap
  definition/behavior metadata while selecting a target and defers full
  traversal lookup until after an exact object is chosen.
- Follow-up fix: added a small thread-local region lookup cache in
  `rc-core/pathfinding.c` so click-to-move pathfinding does not linearly scan
  loaded collision regions for every collision flag read during BFS.
- Follow-up fix: stopped pending NPC/object/ground-item interactions from
  recomputing pathfinding every tick while the player already has an active
  route. The stricter object reach validator could otherwise turn a pending
  object click into repeated route searches at tick cadence, causing visible
  stalls in `rc-viewer`.
- Follow-up fix: guarded viewer active-object state lookup so the object picker
  skips the core state scan entirely while no dynamic object states exist.
- Change made: expanded `RcObjectState` in `rc-core/types.h` so dynamic object
  state is placement-local, with original and active id/type/rotation/tile/
  plane, animation id, respawn tick, revert tick, and dynamic/open/depleted
  flags. Added `rc_world_object_active_state` in `rc-core/api.h` and
  `rc-core/tick.c` so render/input consumers can observe core-owned active
  placement state without owning behavior.
- Change made: updated `rc-core/tick.c` object interaction routing to validate
  exact object reach before dispatch. Wall shapes now require a valid wall-side
  reach tile; rectangular locs now check collision wall flags and b237
  force-approach side masks instead of broad Chebyshev radius alone.
- Change made: decoded object opcode `69` as force-approach metadata in
  `tools/cache_pipeline/rc_cache/definitions.py`, exported it through the
  existing object definition binary padding byte in `tools/export_object_defs.py`,
  and loaded it into `RcObjectDef.force_approach` in `rc-core/objects.c`.
  Regenerated `data/defs/object_defs.bin` locally and refreshed
  `tools/reports/object_defs.txt`.
- Change made: made door state mutation placement-local in `rc-core/tick.c`.
  Opening a door now updates only that placement's active state, clears its wall
  collision, schedules a timed revert, and restores base state/collision when
  the revert tick fires.
- Change made: updated `rc-viewer/viewer.c` picking to read active object state
  from `rc-core` before labeling/context-targeting a placement. This keeps
  viewer input translation aligned with core state while leaving gameplay
  behavior in core.
- Change made: expanded `tests/test_objects_runtime.c` to cover force-approach
  export loading, active object state reads, placement-local door open state,
  collision clearing, and timed collision restore.
- Why it was made: Step 12 needs a generic dynamic-object foundation before
  adding more object-specific scripts. The previous behavior could decide reach
  from a broad rectangle and tracked only base/active ids, which was not enough
  for OSRS-style target reach, per-placement mutation, timed reverts, or viewer
  observation of active object state.
- Upstream/downstream impact:
  - Object interactions now route/dispatch through a stricter exact-placement
    reach contract.
  - Force-approach metadata is available to runtime consumers without changing
    the binary record size, so old generated data remains loadable with zero
    force flags and regenerated b237 data carries real flags.
  - Door/gate visual replacement, object/player action animations, gate-pair
    pairing, and full data/script-driven effect composition remain Step 12
    work before Step 13.
- Validation:
  - `python3 tools/export_object_defs.py` regenerated object definitions and
    report successfully.
  - `cmake --build build --target test_objects_runtime test_traversal_runtime rc-viewer`
    passed.
  - `./build/test_objects_runtime` and `./build/test_traversal_runtime` passed.
  - Full regression passed: `ctest --test-dir build --output-on-failure`
    reported `63/63` tests passing.
  - Viewer smoke passed:
    `timeout 10 env RC_VIEWER_QUIET=1 RC_VIEWER_EXIT_FRAMES=120 ./build/rc-viewer`
    reached `Viewer ready` and exited cleanly.
  - Coverage review: no coverage instrumentation is configured in this build;
    `rg "coverage|gcov|llvm-cov|profraw|--coverage" CMakeLists.txt testing -S`
    returned no matches and `find build -name '*.gcno' -o -name '*.gcda' -o -name '*.profraw' -o -name '*.profdata'`
    returned no artifacts. Changed paths are covered by the object runtime
    regression tests listed above, but line coverage remains unavailable.
  - `bash testing/run_sps_benchmark.sh` passed at `2,876,701` SPS
    (`347.62` ns/env step), within local noise of the previous Step 12
    benchmark entry at `2,882,559` SPS.
  - Follow-up performance validation after route-churn fix:
    `cmake --build build --target test_objects_runtime test_traversal_runtime rc-viewer`,
    `./build/test_objects_runtime`, `./build/test_traversal_runtime`,
    `ctest --test-dir build --output-on-failure`, and
    `timeout 10 env RC_VIEWER_QUIET=1 RC_VIEWER_EXIT_FRAMES=120 ./build/rc-viewer`
    all passed. `bash testing/run_sps_benchmark.sh` reported `2,884,758` SPS
    (`346.65` ns/env step).
  - Follow-up click-stall validation after tile-local picking and cached
    pathfinding region lookup:
    `cmake --build build --target test_objects_runtime test_traversal_runtime rc-viewer`,
    `./build/test_objects_runtime`, `./build/test_traversal_runtime`,
    `ctest --test-dir build --output-on-failure`, and
    `timeout 10 env RC_VIEWER_QUIET=1 RC_VIEWER_EXIT_FRAMES=120 ./build/rc-viewer`
    all passed. `git diff --check` passed. `bash testing/run_sps_benchmark.sh`
    reported `2,890,926` SPS (`345.91` ns/env step).
  - Follow-up approach-routing validation:
    `cmake --build build --target test_objects_runtime test_npc_option_interactions rc-viewer`,
    `./build/test_objects_runtime`, `./build/test_npc_option_interactions`,
    `./build/test_traversal_runtime`, `ctest --test-dir build --output-on-failure`,
    and `timeout 10 env RC_VIEWER_QUIET=1 RC_VIEWER_EXIT_FRAMES=120 ./build/rc-viewer`
    all passed. `git diff --check` passed. Coverage instrumentation remains
    unavailable (`rg "coverage|gcov|llvm-cov|profraw|--coverage" CMakeLists.txt testing -S`
    and coverage artifact search returned no results). `bash testing/run_sps_benchmark.sh`
    reported `2,883,375` SPS (`346.82` ns/env step).

## 2026-05-09 — Exact placed-object traversal matching

- Change made: tightened coordinate-backed object interactions in
  `rc-core/tick.c` so a clicked object id at `x/y/plane` must resolve to an
  actual exported `OPLC` placement when placement data is loaded. Clicking a
  transport source tile that is adjacent to an object, but is not itself the
  placed object anchor, no longer begins an object interaction.
- Change made: replaced the broad same-object-id anchor-drift fallback in
  `rc-core/tick.c` with placement-local source matching. Transport/traversal
  source rows may match only when their source tile is inside or directly
  cardinal-adjacent to the exact clicked placement's rotated cache footprint.
  When multiple source rows surround one placement, dispatch now prefers the
  row under the player's current tile and otherwise keeps deterministic first
  row selection.
- Change made: mirrored the same placement-local traversal constraint in
  `rc-viewer/viewer.c` so context-menu labels and first actions are sourced
  from the exact picked placement rather than a nearby object id row.
- Change made: hardened `rc-core/traversal.c` and `rc-core/objects.c` lookup
  loops by rechecking row kind/source id or object id inside indexed ranges.
  This prevents a malformed or duplicate-adjacent indexed range from applying
  another object's traversal row, as seen with the Wilderness lever sharing a
  source tile neighborhood with object `398`.
- Change made: updated `tests/test_objects_runtime.c` to use real exported
  placements for coordinate-backed object interactions and added negative
  coverage for source-only tiles near the Varrock Rat Pits manhole/ladder and
  other offset traversal rows.
- Why it was made: Step 12 manual validation exposed random/unintended
  transports from tiles that should have been normal movement or unrelated
  object clicks. The root issue was not one bad object row; the engine still
  allowed coordinate-backed interactions to resolve from broad same-id source
  drift instead of one exact scene placement.
- Upstream/downstream impact:
  - Viewer clicks and core object interactions now require an exact placed
    object target before transport/action dispatch.
  - Offset b237 traversal rows still work for placements such as the Varrock
    Rat Pits manhole, cellar ladders, far-area cave object, and Rat Pits exit
    staircase because their source rows are tied to the clicked placement's
    footprint/edge instead of loose object-id radius.
  - This is the false-positive transport/selection foundation for the broader
    generic dynamic-object interaction pass. Placement-local visual
    replacement, object/player action animations, force-approach routing, and
    data/script-driven effect composition remain Step 12 work before Step 13.
- Validation:
  - `cmake --build build --target test_objects_runtime test_traversal_runtime rc-viewer`
    passed.
  - `./build/test_objects_runtime` and `./build/test_traversal_runtime`
    passed.
  - Focused regression passed:
    `ctest --test-dir build -R "test_objects_runtime|test_traversal_runtime|test_interaction_engine|test_plane_contracts_runtime|test_collision_tiles_runtime" --output-on-failure`
    reported `12/12` tests passing.
  - Full regression passed: `ctest --test-dir build --output-on-failure`
    reported `63/63` tests passing.
  - Viewer smoke passed:
    `timeout 10 env RC_VIEWER_QUIET=1 RC_VIEWER_EXIT_FRAMES=120 ./build/rc-viewer`
    reached `Viewer ready` and exited cleanly.
  - Coverage artifacts remain unavailable in the current non-instrumented
    `build/` tree; `find build -name '*.gcno' -o -name '*.gcda'` returned no
    files.
  - `bash testing/run_sps_benchmark.sh` passed at `2,882,559` SPS
    (`346.91` ns/env step).

## 2026-05-09 — Transport regression cleanup and bank-row filtering

- Change made: retracted the uncommitted broad dynamic-object/agility-shortcut
  pass from the working tree and removed the generated sidecar artifacts it
  had left under `data/regions/` and `data/regions/scene_cache`. The viewer is
  back on the committed Step 12 static scene-streaming path with normal
  `OBJ2` object assets and no generated `DOBJ`/dynamic-object sidecar loads.
- Change made: regenerated the Varrock/F2P plane `0..3` object meshes from
  the stable object exporter so baked doors/gates/objects are present in the
  normal `.objects` files again instead of being removed into the retracted
  sidecar path.
- Change made: updated `tools/export_object_transports.py`,
  `tools/export_traversal_edges.py`, and `tools/export_object_behaviors.py` to
  filter non-transport bank/storage source rows before generating movement
  transports. Rows whose action is `Bank`, `Collect`, `Deposit`, or
  `Deposit-box` are now treated as storage interactions, not coordinate
  transport edges, even if the source corpus includes destination coordinates.
- Change made: regenerated `data/defs/object_transports.bin`,
  `data/defs/traversal_edges.bin`, `data/defs/object_behaviors.bin`, and their
  reports after the filter. The current generated counts are `29,928` object
  transport edges, `45,740` traversal edges, and `8,108` object behavior rows.
- Change made: added regression coverage that Varrock bank booth object
  `34810` at `(3185,3436,0)` does not resolve as either an object transport or
  a traversal edge, preventing bank clicks/tiles from routing to unrelated
  destinations such as Gnome Stronghold.
- Why it was made: manual validation showed severe viewer stutter and random
  tile/object transports after the too-broad object interaction pass. The
  generated sidecars widened runtime loading beyond the intended door/gate
  scope, and the existing exporter path was blindly promoting bank rows from
  the local OSRS transport source into movement transports.
- Upstream/downstream impact:
  - `./build/rc-viewer` should no longer load the retracted dynamic-object or
    agility-shortcut sidecars, and the accidental bank-booth/chest transport
    routes are removed from generated runtime data.
  - Banking/storage actions remain behavior-marked for later bank UI/runtime
    work; they are only excluded from movement transport exports.
  - Door/gate visual replacement and agility shortcuts remain deferred as
    narrow, separately validated interaction tasks. They should not be
    reintroduced as a broad sidecar/runtime pass.
- Validation:
  - `python3 -m py_compile tools/export_object_transports.py tools/export_traversal_edges.py tools/export_object_behaviors.py`
    passed.
  - `cmake --build build --target rc-viewer test_objects_runtime test_traversal_runtime`
    passed.
  - `./build/test_objects_runtime` and `./build/test_traversal_runtime`
    passed.
  - Focused regression passed:
    `ctest --test-dir build -R "test_objects_runtime|test_traversal_runtime|test_plane_contracts_runtime|test_collision_tiles_runtime" --output-on-failure`
    reported `4/4` tests passing.
  - Full regression passed: `ctest --test-dir build --output-on-failure`
    reported `63/63` tests passing.
  - Viewer smoke passed:
    `timeout 10 env RC_VIEWER_QUIET=1 RC_VIEWER_EXIT_FRAMES=120 ./build/rc-viewer`
    reached `Viewer ready` and exited cleanly.
  - Coverage artifacts remain unavailable in the current non-instrumented
    `build/` tree; `find build -name '*.gcno' -o -name '*.gcda'` returned no
    files.
  - `bash testing/run_sps_benchmark.sh` passed at `2,895,695` SPS
    (`345.34` ns/env step).

## 2026-05-08 — Viewer object/NPC clickbox picking correction

- Change made: updated `rc-viewer/viewer.c` object picking to remove the
  broad screen-center radius fallback that could select nearby rocks, gates,
  walls, or transports even when the mouse was over an adjacent tile/object.
  Viewer object actions now require the raycast tile to be inside the object's
  cache footprint or the camera ray to intersect that footprint's local
  bounding box.
- Change made: updated NPC picking in `rc-viewer/viewer.c` to remove the
  adjacent-tile and large screen-radius fallback. NPC interaction now uses a
  camera ray against the NPC's current rendered footprint, so left-click and
  middle-click do not attack/interact with nearby NPCs unless the mouse is
  actually over their click volume.
- Why it was made: manual validation showed middle-clicking small one- or
  two-tile targets such as the Barbarian Village Stronghold entrance could show
  options for neighboring rocks, and nearby guards/NPCs could be selected from
  outside their visible footprint. The local RuneLite audit confirmed the
  correct model is clickbox containment (`TileObject.getClickbox()` and
  `contains(mousePosition)`) rather than a large nearest-center radius.
- Upstream/downstream impact:
  - Viewer object/NPC context menus and left-click actions should be much less
    permissive; if no entity click volume is under the mouse, the viewer should
    fall through to normal tile movement.
  - This is a presentation/input picking correction only. No `rc-core`
    gameplay, traversal, combat, or object-state logic changed.
  - Object clickboxes are currently approximated by cache footprint bounding
    boxes. Exact model-triangle clickboxes remain a possible later precision
    pass if specific small decorations still over-select within their tile
    footprint.
- Validation:
  - `git diff --check` passed.
  - `cmake --build build --target rc-viewer` passed.
  - Full regression passed: `ctest --test-dir build --output-on-failure`
    reported `63/63` tests passing.
  - `timeout 5s ./build/rc-viewer` reached `Viewer ready`; exit `124` is the
    expected bounded-smoke timeout.
  - Coverage artifacts remain unavailable in the current non-instrumented
    `build/` tree; `find build -maxdepth 3 -name '*.gcno' -o -name '*.gcda'`
    returned no files. The changed picker paths are viewer input paths and
    require manual visual validation.
  - `bash testing/run_sps_benchmark.sh` passed at `2,911,316` SPS
    (`343.49` ns/env step).

## 2026-05-08 — Step 12 static transport scene-streaming pass

- Change made: updated `rc-core/tick.c` object traversal matching so
  source-backed transport rows may match a placed object within that object's
  cache definition footprint instead of requiring the source row to equal the
  placement anchor exactly. This keeps the match constrained by object id,
  option, and plane while covering b237 cases where the authored source tile is
  the clicked/interaction tile.
- Change made: mirrored that anchor-drift matching in `rc-viewer/viewer.c`
  for context-menu/right-object action detection, so the viewer and core agree
  on available static transport actions.
- Change made: changed generated viewer scene loading to export and load only
  the player's current scene plane on first transport into an unloaded static
  dungeon/cave slice. Generated slices now default to
  `data/regions/scene_cache`, and the plane selector generates missing planes
  lazily for that active slice.
- Change made: added initial-scene reload tracking to `rc-viewer/viewer.c`.
  If a transport returns the player into the original prebuilt Varrock/F2P
  rectangle, the viewer reloads the original terrain/object assets instead of
  generating a tiny surface slice around the return tile.
- Change made: extended `tests/test_objects_runtime.c` with Varrock Rat Pits
  manhole down/up coverage, Rat Pits entrance coverage, and the offset Rat
  Pits exit staircase regression. Existing tests still cover plane-changing
  stairs, same-plane dungeon moves, far-area static moves, lever transports,
  and ladder placement/source drift.
- Why it was made: manual validation showed Rat Pits-style object transports
  could be slow and unreliable. The local reference audit confirmed the
  correct split: RSMod/Void/2011Scape/RuneLite treat normal manholes, ladders,
  stairs, caves, and dungeons as absolute static world-coordinate moves, while
  true instances use separate template/chunk-remap systems. RuneC needed the
  same practical contract instead of treating source coordinates and placement
  anchors as always identical or generating every plane before first render.
- Upstream/downstream impact:
  - Static object transports whose source row names a clicked tile instead of
    the exact placement anchor should now trigger consistently.
  - First-load viewer stalls for unloaded static dungeon destinations should be
    reduced because the current plane is generated first; other planes are
    still available on demand.
  - Returning into the initial Varrock/F2P scene should restore the full
    original scene instead of loading an incorrect small surface window.
  - Instance-flagged transports, template chunks, dynamic chunk remapping, and
    activity-specific destination loading remain explicit later work.
  - Door/gate visual mesh replacement is still deferred; collision/object state
    mutation already exists, but the baked scene mesh is not dynamically
    swapped yet.
- Validation:
  - Reference audit checked local RSMod, VoidPS, 2011Scape, and RuneLite
    materials for static transports, map loading, and instance separation.
  - `git diff --check` passed.
  - `cmake --build build --target rc-viewer test_objects_runtime test_traversal_runtime test_plane_contracts_runtime`
    passed.
  - `./build/test_objects_runtime`, `./build/test_traversal_runtime`, and
    `./build/test_plane_contracts_runtime` passed.
  - Full regression passed: `ctest --test-dir build --output-on-failure`
    reported `63/63` tests passing.
  - `timeout 5s ./build/rc-viewer` reached `Viewer ready`; exit `124` is the
    expected bounded-smoke timeout.
  - Cold one-plane Rat Pits scene-slice export passed:
    `/usr/bin/time -f %e python3 tools/cache_pipeline/export_scene_slice.py --center-x 2901 --center-y 9867 --radius-regions 0 --output-prefix /tmp/runec_scene_bench_step12/scene_2880_9856_r0 --planes 0`
    completed in `2.88s`. The previous all-plane path measured about `9.28s`
    in the same exporter workflow.
  - Coverage artifacts remain unavailable in the current non-instrumented
    `build/` tree; `find build -maxdepth 3 -name '*.gcno' -o -name '*.gcda'`
    returned no files. Changed core transport matching is covered by
    `test_objects_runtime`; viewer scene-reload behavior still needs manual
    transport validation.
  - `bash testing/run_sps_benchmark.sh` passed at `2,915,057` SPS
    (`343.05` ns/env step).

## 2026-05-08 — Step 11 actor-tracked projectile impact correction

- Change made: extended `RcCombatProjectile` in `rc-core/types.h` with an
  impact retention window and updated `rc-core/combat.c` so projectile events
  remain alive after travel long enough for their target impact spotanim to
  render. Projectiles without impact graphics keep the previous cleanup
  behavior.
- Change made: updated `rc-viewer/viewer.c` projectile rendering to resolve
  actor targets from the live player/NPC render position every frame instead of
  always using the original target tile captured at launch. This mirrors the
  RSMod/RuneLite/2011Scape target-index contract: actor-targeted projectiles
  carry source/target actor ids and the client renders toward the actor, while
  the launch coordinate still supplies the initial path timing.
- Change made: added viewer-side impact drawing for projectile
  `impact_spotanim_id` using the same synthetic spotanim model, cache
  recolor/retexture, resize, and sequence playback path used for travel
  spotanims. Impact effects are drawn at the target actor's current position
  and stop after one sequence-duration window instead of looping indefinitely.
- Change made: increased magic projectile presentation scale in the viewer so
  elemental spell travel and impact effects such as Fire Blast read at the
  intended in-world size while ranged arrows retain their cache model scale.
- Why it was made: Fire Blast still appeared too small, had no impact effect,
  and both ranged/magic projectiles landed on the tile where the NPC stood at
  launch instead of following moving targets. The local reference audit
  confirmed RSMod `ProjAnim` stores `targetIndex`, RuneLite exposes projectile
  actor target state, and 2011Scape sends pawn target indexes for map
  projectiles.
- Upstream/downstream impact:
  - Magic spell validation should now show both the travel projectile and a
    target impact burst.
  - Moving NPCs should be hit at their current rendered position for player
    ranged and magic attacks.
  - Fire Blast/magic projectile alpha, density, and visual detail are still
    not OSRS-perfect. The current path is a working cache-backed substrate, but
    magic projectile presentation still needs a dedicated parity pass covering
    all spellbooks, all spell projectile/impact graphics, transparent/alpha
    handling, and exact per-spell sizing/animation behavior.
  - Ranged arrow presentation is good for the current bow/arrow path, but
    crossbow bolts, special bolts, thrown weapons, and other ranged projectile
    families still need generated row coverage and manual validation.
  - This does not yet implement launch spotanim drawing, projectile sound
    playback, or full special-attack projectile breadth; those remain Step 11
    parity/data-breadth work.
  - The runtime still uses local C state and generated cache assets only; no
    reference repository code is called at runtime.
- Validation:
  - `cmake --build build --target rc-viewer test_combat_visuals_projectiles`
    passed.
  - `./build/test_combat_visuals_projectiles` passed, including impact
    retention assertions and actor target id assertions.
  - `ctest --test-dir build -R test_combat_visuals_projectiles --output-on-failure`
    passed.
  - Full regression passed: `ctest --test-dir build --output-on-failure`
    reported `63/63` tests passing.
  - Viewer smoke reached `Viewer ready` under `timeout 5s ./build/rc-viewer`;
    exit `124` is the expected timeout after startup.
  - `git diff --check` passed.
  - Coverage artifacts remain unavailable in the current non-instrumented
    `build/` tree; `find build -name '*.gcda' -o -name '*.gcno' -o -name '*.profraw'`
    returned no files.
  - `bash testing/run_sps_benchmark.sh` passed at `2,946,836` SPS
    (`339.35` ns/env step).

## 2026-05-08 — Step 11 OSRS projectile profile correction

- Change made: extended `data/defs/combat_visuals.tsv` and
  `tools/export_combat_visuals.py` so generated combat visual rows now carry
  RSMod/OpenRS-style projectile profile fields: start height, end height,
  start cycle delay, slope/angle, length adjustment, horizontal start offset,
  and per-tile step multiplier.
- Change made: raised `RC_MAX_COMBAT_VISUAL_DEFS` in
  `rc-core/combat_visuals.h` from `1024` to `8192` and updated
  `rc-core/combat_visuals.c` to parse the expanded row shape while remaining
  compatible with the older shorter TSV format.
- Change made: updated `rc-core/combat.c` and `rc-core/types.h` so combat
  projectiles retain the client projectile profile, compute distance-based
  server/client lifetime from the authored RSMod formula, and use the weapon's
  projectile timing profile with the ammo's travel spotanim/model when RSMod
  splits those responsibilities across weapon and ammunition.
- Change made: replaced the placeholder `rc-viewer/viewer.c` projectile lerp
  with client-style projectile kinematics using start cycle, end cycle,
  vertical heights, horizontal start offset, slope, and cache model orientation.
  Cache-backed projectile models are drawn with their authored colors instead
  of ranged/magic tint overrides, and projectile mesh sequence playback starts
  on the projectile movement cycle.
- Change made: regenerated the ignored local data artifact
  `data/defs/combat_visuals.tsv` from local RSMod/Joshua-F b237 sources. The
  generated table now loads `1164` rows, including spell rows beyond the old
  `1024` cap such as `Fire Blast`, and rows now preserve the magic shortbow
  `arrow` projectile profile separately from rune arrow's travel spotanim.
- Why it was made: manual Step 11 validation still showed no magic projectile
  and malformed ranged projectile travel. The root cause was structural, not
  just rendering polish: the old visual loader silently truncated the generated
  table before elemental spell rows, and the runtime/viewer used generic
  projectile interpolation instead of the OSRS projectile profile fields used by
  RSMod, 2011Scape packets, and RuneLite's projectile API.
- Upstream/downstream impact:
  - Magic spells can now load their generated projectile visual rows instead
    of disappearing due to the visual-row cap.
  - Ranged projectiles now combine weapon timing and ammo travel visuals, which
    matches RSMod's ranged combat contract for bows and arrows.
  - Remaining Step 11/combat-fidelity work is data breadth and content-specific
    presentation: launch/impact spotanim rendering, non-elemental spells,
    special attacks, boss/NPC projectile rows, actor-specific projectile launch
    offsets, and broader spotanim/sequence coverage.
  - `data/defs/combat_visuals.tsv` remains a generated artifact under the
    separate ignored `data/` repository boundary.
- Validation:
  - Reference audit: RSMod `ProjAnimBuilds.kt`, RSMod `ProjAnim.kt`,
    RSMod `PvNCombat.kt`, 2011Scape `MapProjAnimMessage.kt`, and RuneLite
    `Projectile` / `WorldView.createProjectile` APIs were checked for the
    authoritative projectile field set and timing model.
  - `python3 -m py_compile tools/export_combat_visuals.py tools/cache_pipeline/export_projectile_models.py`
    passed.
  - `python3 tools/export_combat_visuals.py` passed and wrote `1164` source
    rows to `data/defs/combat_visuals.tsv`.
  - `cmake --build build --target rc-viewer test_combat_visuals_projectiles`
    passed.
  - `./build/test_combat_visuals_projectiles` passed, including the generated
    Fire Blast row and weapon/ammo projectile profile split.
  - `ctest --test-dir build --output-on-failure` passed `63/63`.
  - Coverage review: no `.gcda` files were present in this non-instrumented
    build tree, so line coverage was not available; changed projectile paths
    are covered by `test_combat_visuals_projectiles` and broad CTest
    regression.
  - `bash testing/run_sps_benchmark.sh` passed at `2,967,540` SPS
    (`336.98` ns/env step).
  - `timeout 5s ./build/rc-viewer` reached `Viewer ready`; timeout exit `124`
    is expected for the bounded smoke run.

## 2026-05-08 — Step 11 projectile and spellbook visual correction

- Change made: corrected the fixed spellbook fallback in `rc-viewer/ui.c` to
  use b237 `standard_spell_on_*` sprite frame ids instead of local UI slot
  numbers. The table now covers the full standard spellbook slot list used by
  the current viewer, including Fire Blast and the newer b237 standard-book
  entries, so spell clicks pass real spell names into the combat handoff
  instead of unresolved `"Spell N"` fallback names.
- Change made: regenerated projectile model data with spotanim-specific
  synthetic model ids in `tools/cache_pipeline/export_projectile_models.py`.
  Each synthetic row is keyed as `0xA2000000 + spotanim_id` and applies the
  spotanim recolor pairs before writing `data/models/projectiles.models`,
  allowing the viewer to render the cache-backed travel effect rather than a
  raw unrecolored model fallback.
- Change made: updated `rc-viewer/viewer.c` projectile drawing to prefer those
  synthetic spotanim model rows, apply spotanim `resize_xy` / `resize_z`
  scaling, remove the hard-coded oversized ranged/magic scale multipliers,
  correct projectile yaw for the cache model forward axis, and stop drawing a
  projectile once it reaches its target instead of leaving it hanging until the
  next core cleanup tick.
- Change made: hardened animated-object rendering so an animated placement
  whose model exists but has no compatible skin state still renders its base
  pose instead of disappearing. Actual looping loc opcode `24` animations still
  use the existing `OANM` plus `ANM2` path.
- Why it was made: manual Step 11 validation showed incorrect spellbook icons,
  no magic projectile for selected spells, an oversized/misrotated ranged
  projectile, and projectile models lingering after impact. RSMod's elemental
  spell and projectile authoring confirmed that travel projectiles are selected
  by spotanim/projectile definitions with authored delay/height/sequence data,
  not by generic raw model ids.
- Upstream/downstream impact:
  - Runtime combat still emits the same projectile events from core; this pass
    fixes viewer/cache interpretation and spellbook selection metadata.
  - `data/models/projectiles.models` is a regenerated local data artifact under
    the separate ignored `data/` boundary. It now contains `5,551` projectile
    model entries including spotanim synthetic variants.
  - RSMod, RuneLite, 2011Scape, and VoidPS remain references only; RuneC does
    not call into those repos at runtime.
  - Door/gate visual open-close remains a separate dynamic-loc replacement
    pass. RSMod and 2011Scape both model doors/gates by removing the current
    placed loc and spawning the next open/closed loc at translated coordinates
    and angle; this is not the same path as cache opcode `24` looping map-object
    animations.
- Verification:
  - `python3 -m py_compile tools/cache_pipeline/export_projectile_models.py tools/export_combat_visuals.py`
    passed.
  - `python3 tools/cache_pipeline/export_projectile_models.py --cache tools/cache_pipeline/source/current_fightcaves_demo/data/cache --spotanims data/defs/spotanims.bin --spotanim-ids all --output data/models/projectiles.models`
    passed and wrote `5,551` projectile models.
  - Synthetic spotanim rows for arrow and common magic travel effects were
    confirmed in `data/models/projectiles.models`, including spotanims `15`,
    `130`, `132`, and `133`.
  - `cmake --build build --target rc-viewer test_combat_visuals_projectiles test_objects_runtime`
    passed.
  - `./build/test_combat_visuals_projectiles` passed.
  - `./build/test_objects_runtime` passed.
  - Full regression passed: `ctest --test-dir build --output-on-failure`
    reported `63/63` tests passing.
  - Viewer smoke reached `Viewer ready` under `timeout 5s ./build/rc-viewer`;
    the process was intentionally stopped by `timeout` after startup.
  - `git diff --check` passed.
  - Coverage artifacts remain unavailable in the current non-instrumented
    build; `find . -path '*CMakeFiles*' -prune -o -name '*.gcda' -print`
    returned no files.
  - `bash testing/run_sps_benchmark.sh` passed and reported `2,499,176` SPS
    (`400.13` ns/env-step).

## 2026-05-08 — Step 11 combat visual data-backed follow-up

- Change made: added `tools/export_combat_visuals.py`, a local generator for
  `data/defs/combat_visuals.tsv`. It reads RSMod's authored item projectile,
  item attack-animation, projectile timing, and elemental spell definitions,
  then resolves b237 ids through the local Joshua-F dump symbols and spotanim
  dump, including travel spotanim model and sequence ids for projectile rows.
- Change made: added NPC combat visual lookup in `rc-core/combat_visuals.c` /
  `rc-core/combat_visuals.h`, matching the existing item and spell lookup
  behavior with exact style match first and `any` fallback.
- Change made: updated `rc-core/combat.c` so player attacks select visual rows
  before hit queueing, use visual-backed hit/client delays when present, and
  keep weapon animation selection separate from ammo/spell projectile rows.
  NPC attacks now look up visual rows by NPC definition id and style, apply
  visual-backed hit/client delays, use visual attack animations when present,
  and emit NPC-to-player projectile events from data.
- Change made: updated the interaction/combat handoff for manual spell casts.
  `rc_player_cast_spell_on_npc()` now routes spell-on-NPC clicks through the
  NPC attack content group, validates the selected spell, records a one-shot
  `manual_spell_cast`, refreshes player combat style to magic for that swing,
  and clears the one-shot cast after success, cancellation, or failed rune
  validation. This lets a clicked spell emit its magic projectile even when a
  bow or other non-magic weapon is currently equipped.
- Change made: corrected animated world-object playback timing in
  `rc-viewer/viewer.c`. Per-placement `OANM` object animations now advance in
  client-cycle units instead of raw RuneC server ticks, so active b237 map
  object animations are visible instead of appearing frozen or extremely slow.
- Change made: expanded `tests/test_combat_visuals_projectiles.c` with an NPC
  magic projectile fixture and assertions for source/target actor kinds,
  projectile ids, spotanims, animation ids, hit delay, client delay, and NPC
  attack animation selection. The test now also covers spell-on-NPC routing
  forcing a manual magic swing and emitting the Fire Blast travel projectile.
- Why it was made: Step 11 had the spotanim/projectile/animation substrate in
  place, but combat visual selection still depended on sparse fallback behavior.
  Combat needs a content/cache-backed path that can select projectiles,
  spotanims, attack animations, and timing by weapon, ammo, spell, and NPC
  attack before the later combat fidelity pass tunes exact presentation.
- Upstream/downstream impact:
  - `rc-core` now owns data-backed combat visual selection for player attacks
    and supports NPC attack visual rows when present. The current generator
    sources item and elemental spell rows first; broad NPC/boss row generation
    remains later data-breadth work. The viewer continues to render projectile
    and animation events from core state.
  - The generated `data/defs/combat_visuals.tsv` lives under the separate local
    `data/` repository boundary and is not committed in the main RuneC source
    repo.
  - Runtime does not call RSMod or any reference checkout. RSMod and the local
    b237 dump are used only as local export/source inputs for the generated
    data table.
  - Door/gate open-close visuals are still a separate object-state visual pass.
    Opening or closing a door currently mutates runtime/collision state, but
    the baked scene mesh is not yet replaced, hidden, or rotated per placed
    door. RSMod confirms this should be dynamic loc replacement: delete the
    current placed loc, add the next-stage loc at translated coordinates/angle,
    and revert after its configured duration. This is not the same cache path
    as loc opcode `24` looping object animations.
  - Remaining combat-fidelity work is data breadth and exact feel: special
    attacks, non-elemental spells, broader NPC/boss rows, projectile
    launch/target points, orientation, travel/impact/cleanup timing, hitsplats,
    block/cast/facing feedback, and any sequence gaps found in real content.
- Verification:
  - `python3 -m py_compile tools/export_combat_visuals.py` passed.
  - `python3 tools/export_combat_visuals.py --output /tmp/runec_combat_visuals.tsv`
    passed.
  - `python3 tools/export_combat_visuals.py` passed and generated
    `data/defs/combat_visuals.tsv` with `1,164` source rows plus the header.
  - `cmake --build build --target rc-viewer test_combat_visuals_projectiles test_objects_runtime`
    passed.
  - `./build/test_combat_visuals_projectiles` passed.
  - `./build/test_objects_runtime` passed.
  - `ctest --test-dir build -R 'test_combat_visuals_projectiles|test_objects_runtime' --output-on-failure`
    passed.
  - Full regression passed: `ctest --test-dir build --output-on-failure`
    reported `63/63` tests passing.
  - Viewer smoke passed with
    `timeout 15 env RC_VIEWER_QUIET=1 RUNEC_UI_DECODED=1 RUNEC_UI_RUNTIME_SELFTEST=1 RC_VIEWER_EXIT_FRAMES=2 ./build/rc-viewer`;
    it reached `Viewer ready` and reported `ui runtime selftest: PASS`.
  - `git diff --check` passed.
  - Coverage artifacts remain unavailable in the current non-instrumented
    `build/` tree; `find build -name '*.gcda' -o -name '*.gcno' -o -name '*.profraw'`
    returned no files.
  - `bash testing/run_sps_benchmark.sh` passed and reported `3,002,060` SPS
    (`333.10` ns/env-step).

## 2026-05-08 — Dynamic model backface culling correction

- Change made: audited RSMod's Fire cape handling and confirmed it has no
  Fire-cape-specific render path. RSMod only references `tzhaar_cape_fire` /
  `infernal_cape` as object types, places them in the worn Back slot, marks the
  player appearance for rebuild, and sends worn object ids plus secondary
  wearpos metadata to the client. The client/cache renderer owns the final cape
  model, texture, and animation appearance.
- Change made: scoped one-sided backface culling to item/equipment/player model
  draws in `rc-viewer/viewer.c`. The world pass still keeps terrain/objects
  double-sided for foliage and cutout scenery, but dynamic item/equipment models
  now stop rendering the reverse side of single-sided textured faces.
- Why it was made: Fire cape and Infernal cape validation still showed lava
  texture triangles on both sides of the cape. The generated b237 cache records
  were correct (`6570` uses male model `9638`, texture `40`; `21295` uses male
  model `33103`, texture `59`; neither has RSMod/client-script special casing).
  The viewer was disabling backface culling for the entire 3D scene, so cape
  textured faces could be visible from the wrong side even when the cache model
  itself only assigns lava to one surface direction.
- Upstream/downstream impact:
  - No gameplay, cache definition, or item rule behavior changed.
  - Environment rendering remains double-sided; this avoids regressing trees,
    foliage, fences, and other cutout scenery that currently depend on the
    world pass cull state.
  - Dynamic item/equipment rendering now better matches the client assumption
    that model faces are one-sided unless the model data explicitly supplies
    opposite faces.
- Verification:
  - `cmake --build build --target rc-viewer test_inventory_equipment_runtime test_combat_visuals_projectiles test_base_only`
    passed.
  - Focused regression passed:
    `ctest --test-dir build -R 'test_inventory_equipment_runtime|test_combat_visuals_projectiles|test_base_only' --output-on-failure`.
  - Viewer smoke reached `Viewer ready` under `timeout 5 ./build/rc-viewer`;
    the process was intentionally stopped by `timeout` after startup.
  - Full regression passed: `ctest --test-dir build --output-on-failure`
    reported `63/63` tests passing.
  - `git diff --check` passed.
  - Coverage artifacts remain unavailable in the current non-instrumented
    `build/` tree; `find build -name '*.gcda' -o -name '*.gcno' -o -name '*.profraw'`
    returned no files.
  - `bash testing/run_sps_benchmark.sh` passed and reported `2,938,320` SPS
    (`340.33` ns/env-step).

## 2026-05-07 — Cape equipment render and animated item atlas fix

- Change made: added Fire cape (`6570`) to the default item render export
  coverage in `tools/cache_pipeline/export_models.py`, then regenerated
  `data/models/items.models` and `data/models/item_render.map` from the local
  b237 cache. The generated render map now has back-slot male/female equipped
  model records for both Fire cape (`6570`) and Infernal cape (`21295`).
- Change made: extended `tools/cache_pipeline/export_item_render_models.py` to
  emit `data/models/items.tanim` beside the item model atlas. The sidecar
  includes b237 animated texture cells for texture `40` on the Fire cape and
  texture `59` on the Infernal cape.
- Change made: extended `rc-viewer/models.h` so all MDL3 model sets can load
  optional `.tanim` companions, preserve atlas base pixels, and scroll animated
  texture cells at runtime. `rc-viewer/viewer.c` now advances item, NPC, and
  projectile model atlases once per frame, giving wearable and other model-set
  textures the same animation support that object atlases already had.
- Change made: corrected atlas UV handling in
  `tools/cache_pipeline/export_models.py` to match RuneLite's model texture
  behavior more closely: S/U clamps, while T/V remains repeat-capable.
  `tools/cache_pipeline/export_textures.py` now supports wrapped vertical guard
  rows around atlas cells, `tools/cache_pipeline/export_item_render_models.py`
  exports item/equipment atlases with a one-texture-height vertical repeat
  margin, and the `.tanim` pad field records that guard size. `rc-viewer/models.h`
  and `rc-viewer/objects.h` now scroll animated cells through the unpadded
  center region while keeping the guard rows stitched to the same repeated
  texture. The prior clamp/wrap attempts could make Fire cape and Infernal cape
  texture coordinates collapse or sample across atlas boundaries, causing lava
  textures to smear into large triangular patches.
- Change made: added an optional `MUV1` block to MDL3 model exports. The block
  stores the original OSRS texture-triangle vertices and atlas mapping for each
  textured face. `rc-viewer/models.h` now loads that metadata and recomputes
  RuneLite-style texture UV projections after model animation/deformation, and
  `rc-viewer/viewer.c` restores rest UVs when models reset. Composed player
  equipment models preserve the same face-local texture metadata with base
  vertex offsets, so back-slot capes and future animated equipment do not reuse
  stale rest-pose UVs after the player mesh moves.
- Why it was made: cape validation exposed two first-use back-slot issues:
  Fire cape had valid b237 item/model metadata but no generated render-map
  record, animated cape textures were only supported on object atlases, and
  atlas-backed UV handling did not match RuneLite's texture-array semantics
  closely enough for cape texture coordinates near or slightly outside `0`/`1`.
  The remaining core issue was that textured capes are animated/deformed with the
  player model; baking UVs once in the rest pose tore the lava projection after
  animation, while RuneLite computes textured-face UVs from the current model
  vertices.
- Upstream/downstream impact:
  - Gameplay/equipment rules remain unchanged; this is viewer/exporter-only
    render coverage and atlas animation.
  - The model-set animation path is shared, so future animated wearable, NPC,
    projectile, and item model textures can use the same `.tanim` sidecar
    instead of object-specific code.
  - Item/equipment atlases are taller when repeat padding is requested. Existing
    object atlases keep their old layout unless an exporter opts into padding.
- Verification:
  - `python3 -m py_compile tools/cache_pipeline/export_textures.py tools/cache_pipeline/export_models.py tools/cache_pipeline/export_item_render_models.py`
    passed.
  - `cmake --build build --target rc-viewer test_inventory_equipment_runtime test_combat_visuals_projectiles test_base_only`
    passed.
  - Focused tests passed:
    `ctest --test-dir build -R 'test_inventory_equipment_runtime|test_combat_visuals_projectiles|test_base_only' --output-on-failure`.
  - Full regression passed: `ctest --test-dir build --output-on-failure`
    reported `63/63` tests passing.
  - Viewer smoke reached `Viewer ready` under `timeout 5 ./build/rc-viewer`;
    the process was intentionally stopped by `timeout` after startup and loaded
    `item_render: loaded 135 records`.
  - Generated-data validation confirmed `item_render.map` records for `6570`
    and `21295`, and `items.tanim` rows for textures `40` and `59` with
    `128` px vertical repeat padding (`128x384` atlas rows).
  - Generated-data validation confirmed `MUV1` blocks for the Fire cape and
    Infernal cape synthetic equipped model records in `data/models/items.models`.
  - `git diff --check` passed.
  - Coverage artifacts remain unavailable in the current non-instrumented
    `build/` tree; `find build -name '*.gcda' -o -name '*.gcno' -o -name '*.profraw'`
    returned no files.
  - `bash testing/run_sps_benchmark.sh` passed and reported `2,973,877` SPS
    (`336.26` ns/env-step).

## 2026-05-07 — Viewer inventory cape validation seed

- Change made: replaced the first two partyhat entries in the viewer showcase
  inventory with Fire cape (`6570`) and Infernal cape (`21295`) in
  `rc-viewer/viewer.c` and the UI fallback seed in `rc-viewer/ui.c`.
- Why it was made: the manual Step 11 visual pass needs obvious cape items in
  the starting inventory to help validate animated item/equipment rendering
  without changing the broader animation pipeline.
- Verification: not run; this is a two-item viewer seed change requested for
  immediate manual validation.

## 2026-05-07 — Step 11 animation parity slice

- Change made: added cache-backed spot animation metadata export in
  `tools/cache_pipeline/export_spotanims.py`. The new `SPOT` binary captures
  gfx id, model id, sequence id, resize, rotation, brightness, and shadow rows.
  `rc-viewer/spotanims.h` loads this metadata, and `rc-viewer/viewer.c` now
  resolves projectile travel models/animations from spotanim definitions when
  core combat visuals do not already provide explicit model ids.
- Change made: expanded projectile model and animation exports.
  `tools/cache_pipeline/export_projectile_models.py` can now export every model
  referenced by a `SPOT` binary, and `tools/cache_pipeline/export_animations.py`
  can include spotanim-referenced sequence ids. The local generated
  `data/models/projectiles.models` and `data/anims/player.anims` were
  regenerated from the b237 cache.
- Change made: added cache animated texture sidecars. `export_textures.py`
  writes `TANM` atlas-cell animation metadata, `export_objects.py` writes it
  beside object atlases, and `rc-viewer/objects.h` preserves atlas pixels and
  scrolls animated material cells at runtime.
- Change made: added animated environment object export/runtime support.
  `export_objects.py` now preserves loc opcode `24` animation ids, separates
  animated placed locs from the baked static `.objects` mesh, writes `OANM`
  placement sidecars, and writes matching `.object_anim.models` / atlas bundles
  with cache model variants. `rc-viewer/objects.h` loads `OANM` rows, and
  `rc-viewer/viewer.c` loads per-plane animated object model bundles and keeps
  per-placement `ANM2` scratch state before drawing animated locs.
- Generated local data updated: active Varrock/F2P plane assets were
  regenerated from the local OpenRS2 b237 cache. Plane 0 now has `936`
  animated object placements and `168` animated object model variants; planes
  1, 2, and 3 have `81`, `36`, and `1` animated placement rows respectively.
  `data/anims/player.anims` now contains `966` framebases, `1,977` sequences,
  and `42,651` sequence frames after including spotanim and object-location
  sequence coverage.
- Why it was made: Step 7 established the versioned `ANM2` animation binary,
  but the rest of the world still used mostly static cache assets. Step 11
  needed world assets to consume that contract through projectile/spotanim
  animation, animated materials, and placed object animations such as wheat,
  torches, fountains, lamps, and similar cache-driven locs.
- Upstream/downstream impact:
  - Runtime gameplay ownership remains unchanged. `rc-core` still owns combat
    events and traversal; `rc-viewer` only resolves and plays presentation
    assets.
  - Static `.objects` meshes no longer contain animated loc placements when
    companion `OANM`/`.object_anim.models` assets are emitted. The normal
    `./build/rc-viewer` path loads those companions automatically.
  - `data/defs/combat_visuals.tsv` is still absent, so exact combat launch/
    impact spotanim selection and timing remains in the later combat fidelity
    lane. The viewer now has the cache-backed spotanim/model/sequence substrate
    needed for that work.
  - b237 skeletal/Maya animation support remains conditional; the active
    viewer slice did not expose a runtime consumer requiring it.
- Verification:
  - `python3 -m py_compile tools/cache_pipeline/export_spotanims.py tools/cache_pipeline/export_projectile_models.py tools/cache_pipeline/export_animations.py tools/cache_pipeline/export_textures.py tools/cache_pipeline/export_objects.py`
    passed.
  - `python3 tools/cache_pipeline/validate_anims.py data/anims/player.anims --expect-version 2 --require-seq 663 --require-seq 1964 --require-seq 1965 --require-seq 1967 --require-seq 6470 --require-seq 481`
    passed.
  - Focused tests passed: `test_combat_visuals_projectiles`,
    `test_base_only`, `test_objects_runtime`, and `test_traversal_runtime`.
  - `ctest --test-dir build --output-on-failure` passed: `63/63` tests.
  - Viewer smoke passed with `timeout 90 env RC_VIEWER_QUIET=1 RUNEC_UI_DECODED=1 RUNEC_UI_RUNTIME_SELFTEST=1 RUNEC_SCENE_AUTO_EXPORT=0 RC_VIEWER_EXIT_FRAMES=2 ./build/rc-viewer`;
    it loaded `OANM`, `TANM`, `SPOT`, animated object model bundles, expanded
    `ANM2`, and the UI runtime selftest passed.
  - `git diff --check` passed.
  - Coverage artifacts are unavailable in the current non-instrumented
    `build/` tree; `find build -name '*.gcda' -o -name '*.gcno' -o -name '*.profraw'`
    returned no files. Changed exporter/runtime paths are covered by binary
    validators, loader smoke, focused runtime tests, and full CTest; visual
    fidelity still benefits from manual viewer validation.
  - `bash testing/run_sps_benchmark.sh` passed and reported `2,918,276` SPS
    (`342.67` ns/env-step).

## 2026-05-07 — Step 10 route and object interaction corrections

- Change made: fixed long tile-click routes in `rc-core/pathfinding.c` by
  compressing routes into direction-change waypoints instead of storing every
  step in the fixed `RC_MAX_ROUTE` buffer. The search window now covers a
  larger local scene, and failed route requests clear stale player routes.
- Change made: changed object option validation in `rc-core/tick.c` so cache
  object-definition actions are authoritative, matching the RSMod/VoidPS
  contract where a placed loc option is valid when the loc definition exposes
  that option. Behavior masks and traversal rows still provide concrete
  effects such as storage, skilling, doors, and transports.
- Change made: object traversal lookup now prefers exact
  object-id/option/source-tile matches but falls back to a same-plane one-tile
  source-anchor tolerance. This covers b237 placement anchors that differ from
  curated transport source tiles, such as Varrock ladders.
- Change made: opening/closing placed wall doors now mutates the loaded
  `RcWorldMap` wall flags for the placed wall shape and rotation, so opened
  doors unblock pathing through the doorway in the active collision map.
- Change made: viewer object menu/action availability now also accepts real
  object-definition actions and uses the same one-tile traversal-source
  tolerance for presentation labels.
- Change made: viewer left-click object actions and item/spell-on-object
  targeting use strict clicked-tile footprint picking again, while middle-click
  context menus keep the broader model-oriented picker for tall/narrow objects.
  This prevents broad object picking from stealing normal walk-here clicks.
- Change made: generated scene reloads now default to one mapsquare around the
  destination (`RUNEC_SCENE_RADIUS_REGIONS=0`) and run the export through a
  bounded timeout (`RUNEC_SCENE_EXPORT_TIMEOUT_SECONDS`, default `20`) so a
  far traversal cannot leave the viewer blocked indefinitely.
- Why it was made:
  - Manual validation still showed ladders, doors, stairs, caves, and similar
    objects doing nothing after selection.
  - Many clearly reachable Varrock tiles were not walkable from clicks because
    long routes could be truncated near the destination, leaving runtime
    movement to clamp toward a non-adjacent waypoint.
  - Door state previously changed only object state, not collision, so an
    opened door could remain physically blocked.
  - Manual validation after the first correction showed the broad left-click
    object picker still stole too many ground clicks, and an unintended
    dungeon-area traversal left the viewer waiting on a large synchronous
    generated-slice export.
- Upstream/downstream impact:
  - Core remains authoritative for object interactions and traversal; the
    viewer only translates clicks/context choices into core calls.
  - Definition-backed object actions can now dispatch even when generated
    object behavior metadata is incomplete; unknown actions complete as
    generic object interactions unless a behavior/traversal handler provides a
    stronger effect.
  - Door collision updates currently mutate loaded `RcWorldMap` regions. If a
    future runtime relies only on the global immutable collision table without
    loading the active region into `world->map`, dynamic door blocking will
    need a world-local collision overlay.
  - Normal left-click walk behavior is prioritized over loose object picking.
    Tall/narrow objects should be interacted with through middle-click context
    menus when their terrain footprint is hard to hit directly.
  - Deferred follow-up work is explicitly tracked in `work.md`: final minimap
    parity, movement/click-to-tile/pathfinding polish, static transport
    edge-case testing across representative dungeon/cave/stair/ladder/door/
    rift/portal content, and instance-specific transport/dynamic-chunk logic.
- Verification:
  - `cmake --build build --target rc-viewer test_base_only test_objects_runtime test_pathfinding test_traversal_runtime`
    passed.
  - Focused tests passed: `test_pathfinding`, `test_objects_runtime`,
    `test_base_only`, `test_traversal_runtime`.
  - `ctest --test-dir build --output-on-failure` passed: `63/63` tests.
  - `timeout 15 env RC_VIEWER_QUIET=1 RUNEC_UI_DECODED=1 RUNEC_UI_RUNTIME_SELFTEST=1 RUNEC_SCENE_AUTO_EXPORT=0 RC_VIEWER_EXIT_FRAMES=2 ./build/rc-viewer`
    passed.
  - Coverage artifacts are unavailable in the current non-instrumented
    `build/` tree; `find build -name '*.gcno' -o -name '*.profraw' -o -name '*.gcda'`
    returned no files. The changed core pathfinder, door collision, and
    traversal fallback paths are covered by focused runtime tests; manual
    click validation is still needed for viewer picking ergonomics.
  - `bash testing/run_sps_benchmark.sh` passed and reported `2,963,622` SPS
    (`337.42` ns/env-step).

## 2026-05-07 — Exact tile clicks and object traversal targeting

- Change made: fixed viewer ground-click selection by raycasting against the
  loaded terrain height surface instead of a single flat scene-height plane.
- Change made: restricted left-click object picking to the clicked object
  footprint, while keeping middle-click context menus on the broader
  screen-space picker for tall/narrow objects.
- Change made: direct `rc_player_walk_to()` and `rc_player_run_to()` now
  require an exact path to the selected tile instead of accepting pathfinder
  alternate destinations.
- Change made: placed-object interactions now account for placement rotation
  when building the object footprint used by core interaction routing.
- Why it was made:
  - Manual validation showed left-clicking a tile could route to a different
    tile or do nothing because broad object picking stole the click, flat-plane
    raycasts landed on the wrong tile over uneven terrain, and direct walk
    calls accepted alternate destinations.
  - Object travel actions for ladders, doors, cave entrances, and stairs need
    core to route to the placed loc footprint, including rotated dimensions,
    before dispatching traversal.
- Upstream/downstream impact:
  - Normal ground clicks now behave as tile movement, not opportunistic object
    interaction, unless the clicked terrain tile is actually inside that
    object's footprint.
  - Object actions still use the interaction engine and traversal data; the
    viewer only translates the selected object or tile into core calls.
- Verification:
  - `cmake --build build --target rc-viewer test_base_only test_objects_runtime test_pathfinding`
    passed.
  - Focused tests passed: `test_base_only`, `test_objects_runtime`,
    `test_pathfinding`.
  - `ctest --test-dir build --output-on-failure` passed: `63/63` tests.
  - `timeout 15 env RC_VIEWER_QUIET=1 RUNEC_UI_DECODED=1 RUNEC_UI_RUNTIME_SELFTEST=1 RUNEC_SCENE_AUTO_EXPORT=0 RC_VIEWER_EXIT_FRAMES=2 ./build/rc-viewer`
    passed.
  - Coverage artifacts are unavailable in the current non-instrumented
    `build/` tree; `find build -name '*.gcno' -o -name '*.profraw' -o -name '*.gcda'`
    returned no files. The exact blocked-tile walk behavior is covered in
    `test_base_only`; viewer raycast/object-pick behavior still requires
    manual validation.
  - `bash testing/run_sps_benchmark.sh` passed and reported `3,009,281` SPS
    (`332.31` ns/env-step).

## 2026-05-07 — Middle-click context menus and reachable object routing

- Change made: moved viewer and gameframe context menus from right-click to
  middle-click, leaving right-drag dedicated to camera movement.
- Change made: object and ground-item interactions now route to the best
  reachable tile around the target footprint instead of guessing one approach
  tile and failing when that tile is blocked.
- Why it was made: manual testing showed object actions for stairs, ladders,
  doors, cave entrances, and other placed locs were unreliable, and right-click
  was conflicting with camera controls.
- Verification:
  - `cmake --build build --target rc-viewer test_objects_runtime test_traversal_runtime`
    passed.
  - Focused tests passed: `test_objects_runtime`, `test_traversal_runtime`.
  - `ctest --test-dir build --output-on-failure` passed: `63/63` tests.
  - `timeout 15 env RC_VIEWER_QUIET=1 RUNEC_UI_DECODED=1 RUNEC_UI_RUNTIME_SELFTEST=1 RUNEC_SCENE_AUTO_EXPORT=0 RC_VIEWER_EXIT_FRAMES=2 ./build/rc-viewer`
    passed.
  - Coverage artifacts are unavailable in the current non-instrumented
    `build/` tree; `find build -name '*.gcno' -o -name '*.profraw' -o -name '*.gcda'`
    returned no files.
  - `bash testing/run_sps_benchmark.sh` passed and reported `3,000,045` SPS
    (`333.33` ns/env-step).

## 2026-05-07 — Step 10 traversal completion and generated scene reload

- Change made: finished the current Step 10 static-world traversal pass by
  making object transport actions valid from traversal rows even when generated
  object behavior masks are incomplete, improving viewer object picking, and
  adding generated b237 scene-slice reloads after coordinate-changing
  traversal.
- Why it was made:
  - Manual validation showed that right-click object actions for ladders,
    entrances, portals, and same-plane area transports were still unreliable.
  - The reference pass confirmed the correct contract: RSMod handles `OpLoc`
    as a placed loc id plus source tile plus player level, validates the
    current loc/op, routes to the loc, then dispatches the op; VoidPS stores
    object teleports by object id, tile, and option before applying the
    destination tile. RuneC already had equivalent `RcTraversalEdge` rows, but
    core still rejected transport objects when `object_behaviors.bin` had a
    zero action mask.
  - After core traversal moved the player to a far coordinate or dungeon
    coordinate, the viewer still had only the original Varrock/F2P visual
    rectangle loaded.
- Exact surfaces changed:
  - `rc-core/tick.c` now treats an exact `RcTraversalEdge` as valid object
    option authority for object interactions. Door/resource/storage/altar
    behavior still requires object behavior metadata, while transport movement
    can dispatch from traversal data directly.
  - `tests/test_traversal_runtime.c` and `tests/test_objects_runtime.c` now
    cover a lever transport whose behavior action mask is empty, plus the
    existing ladder, dungeon, and far same-plane traversal cases.
  - `rc-viewer/viewer.c` now searches loaded region placements on the selected
    scene plane using screen-space object center/height samples rather than
    requiring the mouse raycast terrain tile to fall inside the object
    footprint. This makes vertical and narrow objects such as ladders, levers,
    fences, portals, and entrances selectable.
  - `rc-viewer/viewer.c` now builds object context menus from behavior-backed
    actions and traversal-backed actions, then sends the exact source
    object id, `x/y/plane`, and option into core.
  - `tools/cache_pipeline/export_scene_slice.py` was added as a thin local
    b237 helper that generates plane-0 through plane-3 terrain/object/atlas
    assets around an absolute world tile.
  - `rc-viewer/viewer.c` now auto-generates or reuses cached scene slices
    under `/tmp/runec_scene_cache` when the player lands outside the currently
    loaded visual rectangle, reloads terrain/object/minimap data around the
    destination, and reloads NPC spawns plus filtered NPC models for that
    rectangle. `RUNEC_SCENE_AUTO_EXPORT=0`, `RUNEC_SCENE_RADIUS_REGIONS`, and
    `RUNEC_SCENE_CACHE_DIR` can adjust this viewer-only validation path.
- Upstream/downstream impact:
  - Core remains authoritative for traversal and collision; the viewer only
    translates input and refreshes presentation assets.
  - The active static-world path now supports same-plane dungeon moves,
    far-area transports, and plane-changing ladders/stairs through the normal
    object interaction path.
  - Instance-flagged transports and dynamic chunk remapping are still explicit
    later activity/instance work; they are not silently treated as static
    world moves.
- Verification:
  - `cmake --build build --target rc-viewer test_traversal_runtime test_objects_runtime`
    passed.
  - Focused tests passed: `test_traversal_runtime`, `test_objects_runtime`.
  - `python3 -m py_compile tools/cache_pipeline/export_scene_slice.py`
    passed.
  - `timeout 120 python3 tools/cache_pipeline/export_scene_slice.py --center-x 3154 --center-y 3924 --radius-regions 0 --output-prefix /tmp/runec_scene_smoke/scene_test --planes 0`
    passed and generated a one-region terrain/object/atlas smoke slice.
  - `timeout 180 env RC_VIEWER_QUIET=1 RUNEC_UI_DECODED=1 RUNEC_SCENE_RADIUS_REGIONS=0 RUNEC_SCENE_CACHE_DIR=/tmp/runec_scene_viewer_smoke RUNEC_PLAYER_START_X=3154 RUNEC_PLAYER_START_Y=3924 RC_VIEWER_EXIT_FRAMES=120 ./build/rc-viewer`
    passed and generated/reloaded a plane-0 through plane-3 scene slice around
    `3154,3924`, then reloaded `23` matching NPC spawns and `6` NPC models for
    that generated rectangle.
  - `ctest --test-dir build --output-on-failure` passed: `63/63` tests.
  - Coverage artifacts are unavailable in the current non-instrumented
    `build/` tree; `find build -name '*.gcno' -o -name '*.profraw' -o -name '*.gcda'`
    returned no files. The changed core traversal branches are covered by
    focused runtime tests; viewer picking/scene reload has the headless smoke
    checks above and still needs manual click validation.
  - `bash testing/run_sps_benchmark.sh` passed and reported `2,964,806` SPS
    (`337.29` ns/env-step).

## 2026-05-07 — Step 10 object traversal interaction slice

- Change made: started Step 10 by wiring viewer object picking into
  coordinate-backed `rc-core` object interactions and enabling traversal data
  in the interactive viewer world.
- Why it was made:
  - Ladders, stairs, dungeon entrances, portals, manholes, agility shortcuts,
    and similar objects must use explicit traversal rows keyed by object id,
    option, source `x/y/plane`, and destination `x/y/plane`.
  - The viewer had NPC and ground-item picking, but object clicks still fell
    through to walk-here behavior. That meant the existing core traversal
    contract could not be exercised from normal viewer input.
  - Skilling/object-focused worlds also carried traversal data paths without
    enabling `RC_SUB_TRAVERSAL`, which made object traversal unavailable in
    that preset.
- Exact surfaces changed:
  - `rc-viewer/objects.h` now uses a viewer-specific include guard so it no
    longer collides with `rc-core/objects.h`.
  - `rc-viewer/viewer.c` includes `rc-core/objects.h`, loads object
    definitions, object placements, object behaviors, object transports,
    global collision tiles, area flags, and traversal edges in the default
    interactive world config.
  - `rc-viewer/viewer.c` can pick placed objects on the selected scene plane
    using source object placements and definition footprints, including
    rotated footprints and active object-state ids where applicable.
  - Right-click now opens object context menus with cache/data-backed object
    actions plus Walk here, Examine, and Cancel. Chosen actions call
    `rc_player_interact_object_at()` with the picked object id and source
    `x/y/plane`.
  - Left-click now sends the first available object option to core before
    falling back to walk-here, and selected item/spell targets can be used on
    picked objects through the existing core interaction APIs.
  - Large traversal jumps and plane changes now reset viewer interpolation and
    scene-plane override so the camera follows the authoritative destination
    instead of tweening across the world. If the destination is outside the
    loaded visual window, the viewer now logs an explicit warning instead of
    silently presenting stale scene assets.
  - `rc_preset_skilling_only()` now enables `RC_SUB_TRAVERSAL`.
  - `tests/test_traversal_runtime.c` now covers both plane-changing and
    full-coordinate same-plane object traversal rows, including a dungeon-style
    `+6400` Y move and a far portal/coffin-style move.
  - `tests/test_objects_runtime.c` now verifies that object interactions apply
    those same full-destination traversal rows through the normal interaction
    tick path.
- Upstream/downstream impact:
  - Gameplay semantics remain in `rc-core`; `rc-viewer` only translates input
    into object interaction intents.
  - Collision/pathfinding can use the global `collision_tiles.bin` fallback
    after traversal. Visual terrain/object meshes are still bounded by the
    generated scene assets currently loaded in the viewer.
  - Remaining Step 10 work is active visual scene streaming for destinations
    outside the current generated Varrock/F2P rectangle, plus explicit handling
    for instance-flagged transports. The current change makes that gap visible
    and prevents stale interpolation, but it does not generate arbitrary
    destination scene meshes at runtime.
- Verification:
  - `cmake --build build --target rc-viewer test_traversal_runtime test_objects_runtime test_base_only`
    passed.
  - Focused tests passed: `test_traversal_runtime`, `test_objects_runtime`,
    `test_base_only`, `test_plane_contracts_runtime`,
    `test_modular_loading`, `test_interaction_engine_phase1`, and
    `test_interaction_engine_phase7`.
  - `ctest --test-dir build --output-on-failure` passed: `63/63` tests.
  - `timeout 15 env RC_VIEWER_QUIET=1 RUNEC_UI_DECODED=1 RUNEC_UI_RUNTIME_SELFTEST=1 RC_VIEWER_EXIT_FRAMES=2 ./build/rc-viewer`
    passed.
  - Coverage artifacts are unavailable in the current non-instrumented
    `build/` tree; `find build -name '*.gcno' -o -name '*.profraw' -o -name '*.gcda'`
    returned no files. Changed core traversal/config branches are covered by
    the focused tests above; the new viewer click path still needs manual
    validation in `./build/rc-viewer`.
  - `bash testing/run_sps_benchmark.sh` passed and reported `2,962,456` SPS
    (`337.56` ns/env-step).

## 2026-05-07 — Viewer scene-plane button controls and higher-plane assets

- Change made: added clickable scene-plane controls to the viewer and removed
  the plane-0 render fallback when inspecting another scene plane, then
  generated real Varrock/F2P plane-1 through plane-3 terrain/object assets for
  the default viewer data path.
- Why it was made:
  - The Step 9 scene-plane override was keyboard-accessible through
    `PageUp`, `PageDown`, and `Home`, but that was not usable enough for
    manual validation on every keyboard/layout.
  - Manual validation showed that higher scene planes still displayed plane-0
    terrain and object meshes when no plane-specific asset had been loaded.
    That made the plane controls misleading: selecting plane 1 should show
    only plane-1 actors/assets, not plane 1 actors over plane-0 scenery.
  - After strict plane isolation, higher planes correctly stopped showing
    plane-0 scenery, but they exposed the next gap: the default checkout only
    had plane-0 terrain/object files, so manual plane validation mostly showed
    plane-filtered NPCs over an empty scene.
- Exact surfaces changed:
  - `rc-viewer/viewer.c` now draws a compact plane control overlay in the
    upper-left of the viewport: an up arrow, current `P#` indicator, and down
    arrow.
  - Clicking the up/down arrows increments or decrements the scene plane using
    the same clamp behavior as the keyboard shortcuts.
  - Clicking the center `P#` button clears the override and returns scene-plane
    tracking to the player plane.
  - The existing keyboard shortcuts remain available and now share the same
    helper used by the buttons.
  - `viewer_terrain_for_plane()` and `viewer_objects_for_plane()` now return
    only assets loaded for the selected plane. They no longer fall back to
    plane-0 terrain/object meshes for missing higher-plane assets.
  - Minimap background selection now avoids the plane-0 world-map image when
    the selected scene plane is not plane 0, so higher-plane minimap rendering
    follows generated plane-filtered minimap data.
  - The camera target now samples the selected scene plane height while a
    scene-plane override is active, so higher-plane NPCs and building interiors
    are framed at their actual floor height instead of around the player
    plane's lower floor.
  - Generated default higher-plane Varrock/F2P assets from the local b237 cache:
    `data/regions/varrock.p1.terrain`, `.p1.objects`, `.p1.atlas`,
    `.p2.terrain`, `.p2.objects`, `.p2.atlas`, `.p3.terrain`, `.p3.objects`,
    and `.p3.atlas`.
  - Plane-1 export contains `32,484` terrain vertices, `10,828` terrain
    triangles, `9,663` object placements with geometry, `2,843,625` object
    vertices, and `947,875` object triangles.
  - Plane-2 export contains `8,958` terrain vertices, `2,986` terrain
    triangles, `5,426` object placements with geometry, `724,101` object
    vertices, and `241,367` object triangles.
  - Plane-3 export contains `3,480` terrain vertices, `1,160` terrain
    triangles, `2,004` object placements with geometry, `255,708` object
    vertices, and `85,236` object triangles.
- Upstream/downstream impact:
  - The code changes are viewer-only presentation/input-intent work. They do
    not change `rc-core` gameplay plane rules or runtime simulation semantics.
  - The generated data gives the normal `./build/rc-viewer` path real
    higher-plane scenery for the active Varrock/F2P validation rectangle.
    Planes can still be sparse where OSRS has little content on that plane, and
    areas outside the generated rectangle remain future streaming/export work.
- Verification:
  - `cmake --build build -j2 --target rc-viewer` passed.
  - `timeout 10 env RC_VIEWER_QUIET=1 RC_VIEWER_EXIT_FRAMES=2 RC_VIEWER_SCREENSHOT=/tmp/runec_plane_buttons.png ./build/rc-viewer`
    passed and produced a screenshot showing the controls.
  - `timeout 10 env RC_VIEWER_QUIET=1 RUNEC_SCENE_PLANE=1 RC_VIEWER_EXIT_FRAMES=2 RC_VIEWER_SCREENSHOT=/tmp/runec_plane1_strict.png ./build/rc-viewer`
    passed and produced a screenshot showing no plane-0 terrain/object fallback
    while viewing plane 1 without plane-1 terrain/object assets.
  - `timeout 10 env RC_VIEWER_QUIET=1 RUNEC_SCENE_PLANE=1 RUNEC_TERRAIN_P1=/tmp/runec_plane1.terrain RUNEC_OBJECTS_P1=/tmp/runec_plane1.objects RC_VIEWER_EXIT_FRAMES=2 RC_VIEWER_SCREENSHOT=/tmp/runec_plane1_asset_strict.png ./build/rc-viewer`
    passed and loaded only the supplied plane-1 terrain/object override for
    the selected scene plane.
  - `timeout 10 env RC_VIEWER_QUIET=1 RUNEC_SCENE_PLANE=1 RC_VIEWER_EXIT_FRAMES=2 RC_VIEWER_SCREENSHOT=/tmp/runec_plane1_real_assets.png ./build/rc-viewer`
    passed and auto-loaded the default plane-1 terrain/object assets.
  - Equivalent screenshot smokes for `RUNEC_SCENE_PLANE=2` and
    `RUNEC_SCENE_PLANE=3` passed and auto-loaded the default plane-2 and
    plane-3 assets.
  - `timeout 10 env RC_VIEWER_QUIET=1 RUNEC_SCENE_PLANE=1 RUNEC_PLAYER_START_X=3204 RUNEC_PLAYER_START_Y=3495 RC_VIEWER_EXIT_FRAMES=2 RC_VIEWER_SCREENSHOT=/tmp/runec_plane1_npc_probe.png ./build/rc-viewer`
    passed and showed plane-1 NPC models rendering in the Varrock castle
    upstairs area.
  - `ctest --test-dir build --output-on-failure` passed: `63/63` tests.
- Coverage and benchmark notes:
  - Coverage artifacts are still unavailable in this non-instrumented `build/`
    tree; `find build -name '*.gcda' -o -name '*.gcno'` returned no files.
  - `bash testing/run_sps_benchmark.sh` passed and reported `2,927,852` SPS
    (`341.55` ns/env-step). This is a viewer input/presentation change, not a
    simulation path change.

## 2026-05-07 — Plane-aware cache/runtime scene contract

- Change made: completed the first Step 9 pass for plane-aware cache and
  runtime/viewer contracts.
- Why it was made:
  - The cache pipeline had plane-aware data in terrain, object, collision,
    spawn, ground-item, projectile, and interaction systems, but the viewer
    still mostly treated the scene as one active plane.
  - Bridges, dungeons, ladders/stairs, higher-plane GE floor placements, and
    future instanced areas need an explicit contract between gameplay plane and
    rendered scene plane instead of scattered plane assumptions.
- Exact surfaces changed:
  - `rc-core/tick.c` now rejects cross-plane NPC, object, ground-item, spell,
    and inventory-use interactions before route or dispatch. Distance and LOS
    helpers also fail across planes, so queued interactions cannot keep trying
    to route toward unreachable plane-mismatched targets.
  - `rc-core/items.c` now refuses direct ground-item pickup when the ground
    item is on a different plane from the player.
  - `tests/test_plane_contracts_runtime.c` covers cross-plane NPC attack/
    interact rejection, object interaction rejection, item-on-ground-item
    rejection, and pickup rejection.
  - `rc-viewer/viewer.c` now owns a scene-plane selection contract separate
    from the player plane. It supports `RUNEC_SCENE_PLANE`, `PageUp` /
    `PageDown` plane override cycling, and `Home` to return to player-plane
    tracking.
  - `rc-viewer/viewer.c` can load plane-specific terrain/object assets through
    `RUNEC_TERRAIN_P0..P3`, `RUNEC_OBJECTS_P0..P3`, or sibling
    `*.pN.terrain` / `*.pN.objects` files. Terrain, object meshes, NPC
    presentation, ground items, projectiles, minimap tiles, overhead UI,
    route markers, collision debug, picking, and height sampling now use the
    selected scene plane where appropriate.
  - `tools/cache_pipeline/export_terrain.py` and
    `tools/cache_pipeline/export_objects.py` now accept `--scene-plane` so
    focused b237 plane exports can be generated without changing the default
    plane-0 asset path.
- Upstream/downstream impact:
  - `rc-core` remains gameplay-authoritative and rejects invalid cross-plane
    actions. `rc-viewer` remains presentation/input intent only; it can inspect
    or render another scene plane without mutating gameplay state.
  - Existing default assets and commands still load plane 0. Plane-specific
    files are optional, so this does not require regenerating the active
    `data/regions/varrock.*` files.
  - Step 10 can now build animated scene object, material, spot/projectile,
    and broader sequence playback on top of a clearer scene-plane contract.
- Verification:
  - `cmake --build build -j2` passed.
  - Focused tests passed:
    `test_plane_contracts_runtime`, `test_traversal_runtime`,
    `test_objects_runtime`, `test_collision_tiles_runtime`,
    `test_spawn_slices_runtime`, `test_npc_facing_runtime`, all
    `test_interaction_engine_phase1..8`, all `test_ground_items_phase1..5`,
    `test_npc_option_interactions`, and `test_prayer_spell_actions_runtime`.
  - `ctest --test-dir build --output-on-failure` passed: `63/63` tests.
  - `python3 -m py_compile tools/cache_pipeline/export_terrain.py tools/cache_pipeline/export_objects.py tools/export_objects_bridge.py`
    passed.
  - Plane-1 terrain smoke for region `48,53` produced `/tmp/runec_plane1.terrain`
    with `576` vertices, `192` triangles, and a `64x64` heightmap.
  - Plane-1 object smoke for region `48,53` produced `/tmp/runec_plane1.objects`
    and `/tmp/runec_plane1.atlas` with `351` objects with geometry,
    `61,110` vertices, and `20,370` triangles.
  - Viewer startup/UI runtime selftest passed with default assets and with
    `RUNEC_SCENE_PLANE=1`, `RUNEC_TERRAIN_P1=/tmp/runec_plane1.terrain`, and
    `RUNEC_OBJECTS_P1=/tmp/runec_plane1.objects`.
  - `git diff --check` passed.
- Coverage and benchmark notes:
  - Coverage artifacts are still unavailable in this non-instrumented `build/`
    tree; `find build -name '*.gcda' -o -name '*.gcno'` returned no files.
  - `bash testing/run_sps_benchmark.sh` passed and reported `2,959,185` SPS
    (`337.93` ns/env-step).

## 2026-05-07 — NPC movement render/core contract fix

- Change made: corrected NPC visual movement so `rc-viewer` no longer replays
  movement from mutable core `prev_x` / `prev_y` fields. The viewer now chains
  each visible segment from its own last authoritative server tile to the new
  `npc->x` / `npc->y`, then advances a local render waypoint queue.
- Why it was made:
  - Manual validation showed the same loop of failures: teleporting, walking in
    place, then teleporting again. The root cause was that render state was
    repeatedly tied back to core history fields that can be overwritten by more
    than one core subsystem during a tick.
  - The reference pattern from RSMod, Void, 2011Scape, and RuneLite is still
    the correct split: server/core owns discrete tile state and sync deltas,
    while the client/viewer owns local render position. Rendering must consume
    the latest authoritative tile once, not restart from stale history.
  - A second core issue was also fixed: `rc-core` combat leash logic was moving
    any passive NPC back toward spawn whenever it was away from its spawn tile,
    even when the NPC was only passively wandering. That could produce
    contradictory movement in the same world tick.
- Exact surfaces changed:
  - `rc-viewer/viewer.c` keeps `server_x` / `server_y` as the last consumed
    authoritative NPC tile, enqueues movement from that tile to the current
    core tile once per `world->tick`, and keeps drawing, minimap dots, picking,
    overhead UI, facing, and walk animation on the local render position.
  - `rc-viewer/viewer.c` removed the failed behavior that reset local render
    position to `npc->prev_x` / `npc->prev_y` when a new movement segment
    arrived.
  - `rc-core/combat.c` now only walks a targetless NPC back to spawn when the
    NPC is already in `leash_state` or has exceeded its leash range. Ordinary
    passive wander is left to `rc-core/npc.c`.
  - `tests/test_combat_phase7_retaliation_ai.c` now covers the passive-wander
    case: combat must not move an untargeted passive NPC unless leash return is
    explicitly active, and an active leash moves one tile from the current tile.
- Upstream/downstream impact:
  - `rc-core` remains the owner of gameplay movement, facing, combat chasing,
    path legality, and leash state. `rc-viewer` remains presentation-only and
    does not make gameplay decisions.
  - This removes the stale-history dependency that caused visible movement to
    restart from a tile different from the tile the NPC had just reached.
- Verification:
  - `cmake --build build -j2 --target rc-viewer test_npc_facing_runtime test_combat_phase7_retaliation_ai test_combat_phase3_movement_range_facing`
    passed.
  - `./build/test_npc_facing_runtime` passed.
  - `./build/test_combat_phase7_retaliation_ai` passed.
  - `./build/test_combat_phase3_movement_range_facing` passed.
  - `timeout 10 env RC_VIEWER_QUIET=1 RUNEC_UI_DECODED=1 RUNEC_UI_RUNTIME_SELFTEST=1 ./build/rc-viewer`
    passed.
  - `ctest --test-dir build --output-on-failure` passed: `62/62` tests.
  - `git diff --check` passed.
- Coverage and benchmark notes:
  - Coverage artifacts are still unavailable in this `build/` tree; `find`
    returned no `.gcda` or `.gcno` files.
  - `bash testing/run_sps_benchmark.sh` passed and reported `2,934,252` SPS
    (`340.80` ns/env-step).

## 2026-05-07 — NPC render queue movement fix

- Change made: replaced the `rc-viewer` NPC `tick_frac` movement rendering
  with a per-NPC local render waypoint queue.
- Why it was made:
  - Manual validation still showed NPCs teleporting. The prior `tick_frac`
    approach was still too tightly coupled to core tick history: when core
    advanced to the next tick, the viewer could lose or collapse the visible
    movement segment before the drawn actor had reached the destination.
  - Re-checking RSMod, Void, and 2011Scape confirmed the server side keeps
    authoritative tile movement as discrete walk/run deltas (`previousCoords`
    plus `pendingStepCount`, `steps.previous` plus `visuals.walkStep`, or
    `StepDirection`). RuneLite exposes the complementary client concept by
    separating server/world location from local/render location.
- Exact surfaces changed:
  - `rc-viewer/viewer.c` now keeps local NPC render position independent from
    core `prev_x` / `x` lifetime and enqueues new server tile deltas as
    visible waypoints.
  - The local renderer advances one OSRS-style tile step over client frames,
    uses Chebyshev movement so diagonal steps take the same duration as
    cardinal steps, clamps large frame deltas so hitch frames do not jump a
    full tile, and snaps only for plane changes or invalid large server moves.
  - Queue overflow collapses the tail to the latest server target instead of
    snapping the actor, preserving visible movement even if the renderer falls
    briefly behind the simulation.
  - NPC walk-animation selection now keys off the local render movement state
    only. Core `prev_x` / `prev_y` can remain different from `x` / `y` for the
    rest of a server tick after the visible step finishes, and using that core
    delta directly let NPCs keep walking in place while their drawn position was
    already stationary.
- Upstream/downstream impact:
  - `rc-core` remains the authoritative NPC movement owner. The change is
    presentation-only in `rc-viewer`, matching the reference server/client
    split instead of adding render behavior to core.
  - NPC world draw positions, minimap dots, picking, overhead health/hits, and
    movement animation now share this local render position.
- Verification:
  - `cmake --build build -j2 --target rc-viewer test_npc_facing_runtime`
    passed.
  - `./build/test_npc_facing_runtime` passed.
  - `./build/test_combat_phase3_movement_range_facing` passed.
  - `timeout 10 env RC_VIEWER_QUIET=1 RUNEC_UI_DECODED=1 RUNEC_UI_RUNTIME_SELFTEST=1 ./build/rc-viewer`
    passed.
  - `ctest --test-dir build --output-on-failure` passed: `62/62` tests.
  - `git diff --check` passed.
- Coverage and benchmark notes:
  - Coverage artifacts are still unavailable in this `build/` tree; `find`
    returned no `.gcda` or `.gcno` files.
  - `bash testing/run_sps_benchmark.sh` passed and reported `2,469,886` SPS
    (`404.88` ns/env-step).

## 2026-05-07 — NPC render motion tick-sync fix

- Change made: corrected the `rc-viewer` NPC render-position path so each NPC
  consumes the current core tick's `prev_x` / `prev_y` -> `x` / `y` movement
  segment with `tick_frac`, instead of accumulating a separate frame-time
  movement clock after detecting a changed server tile.
- Why it was made:
  - Manual validation after the first client-side render-motion pass showed
    NPCs playing walk animations and facing the right destination while their
    drawn position stayed on the original tile.
  - The focused reference audit still points to the same server/client split:
    RSMod, Void, and 2011Scape emit/consume a per-cycle movement segment, while
    RuneLite exposes separate server and client actor positions. The viewer
    needed to render the authoritative per-tick segment directly, not infer a
    second clock from frame deltas.
- Exact surfaces changed:
  - `rc-viewer/viewer.c` now starts an NPC render step from the core movement
    segment for the current `world->tick`, updates progress from `v.tick_frac`,
    and snaps only for invalid large deltas, hidden/dead NPCs, or plane/teleport
    changes.
  - NPC minimap dots, picking, overhead UI, facing, and animation selection keep
    using the same render state, but that state is now tied to the core tick
    instead of a presentation-only accumulator.
- Upstream/downstream impact:
  - `rc-core` remains the authoritative movement owner. This is a viewer-only
    correction and does not add frontend/rendering behavior to core.
  - If a future packet/update layer stores explicit walk/run deltas, this
    viewer path can consume those deltas the same way it now consumes
    `prev -> current`.
- Verification:
  - `cmake --build build -j2 --target rc-viewer test_npc_facing_runtime`
    passed.
  - `./build/test_npc_facing_runtime` passed.
  - `./build/test_combat_phase3_movement_range_facing` passed.
  - `timeout 10 env RC_VIEWER_QUIET=1 RUNEC_UI_DECODED=1 RUNEC_UI_RUNTIME_SELFTEST=1 ./build/rc-viewer`
    passed.
  - `ctest --test-dir build --output-on-failure` passed: `62/62` tests.
  - `git diff --check` passed.
- Coverage and benchmark notes:
  - Coverage artifacts are still unavailable in this `build/` tree; `find`
    returned no `.gcda` or `.gcno` files.
  - `bash testing/run_sps_benchmark.sh` passed and reported `2,928,658` SPS
    (`341.45` ns/env-step).

## 2026-05-07 — NPC client-side render motion

- Change made: moved NPC visual interpolation in `rc-viewer` off direct
  `prev_x` / `x` tile blending and onto a per-NPC client-side render position
  that follows the authoritative core tile state.
- Why it was made:
  - Manual validation still showed NPCs visually teleporting between tiles
    during movement.
  - A focused audit of the reference repos showed the same contract across the
    server/client split:
    - RSMod records `previousCoords` at the start of NPC processing, advances
      queued route steps, and emits walk/run deltas during post-tick sync.
    - Void keeps `steps.previous` plus per-cycle `visuals.walkStep` /
      `visuals.runStep` and resets only the transient sync fields after the
      update packet.
    - 2011Scape uses `MovementQueue` to advance one or two queued steps,
      stores `pawn.steps`, and clears the per-cycle movement segment after
      synchronization.
    - RuneLite's public `Actor` API distinguishes server-side
      `getWorldLocation()` from client-side `getLocalLocation()`, explicitly
      noting that server location can be ahead of rendered position.
  - Our viewer was still drawing NPCs directly from current core tile state,
    so a render hitch or consecutive tile update could skip the visible walk
    between tiles.
- Exact surfaces changed:
  - `rc-viewer/viewer.c` now stores per-NPC render state: last seen server
    tile, current render tile, movement endpoints, progress, and last movement
    delta.
  - NPC world draw positions, minimap dots, picking projection, overhead
    health/hits, and walk-animation selection now consume that render state.
  - Large deltas and plane changes still snap intentionally, matching
    teleport/region-change behavior instead of trying to animate invalid moves.
  - Added float terrain-height sampling for NPC render positions so movement
    over sloped terrain does not snap vertically by tile.
- Upstream/downstream impact:
  - `rc-core` remains the authoritative gameplay/server tile owner; this
    change is presentation-only in `rc-viewer`.
  - The current implementation smooths one authoritative step at a time. If we
    later add explicit NPC path queues or server update packets, this render
    state is the place to consume those deltas without changing core gameplay
    state.
- Verification:
  - `cmake --build build -j2 --target rc-viewer test_npc_facing_runtime`
    passed.
  - `./build/test_npc_facing_runtime` passed.
  - `./build/test_combat_phase3_movement_range_facing` passed.
  - `timeout 10 env RC_VIEWER_QUIET=1 RUNEC_UI_DECODED=1 RUNEC_UI_RUNTIME_SELFTEST=1 ./build/rc-viewer`
    passed.
  - `ctest --test-dir build --output-on-failure` passed: `62/62` tests.
  - `git diff --check` passed.
- Coverage and benchmark notes:
  - Coverage artifacts are still unavailable in this `build/` tree; `find`
    returned no `.gcda` or `.gcno` files.
  - `bash testing/run_sps_benchmark.sh` passed and reported `2,467,184` SPS
    (`405.32` ns/env-step).

## 2026-05-07 — NPC movement interpolation correction

- Change made: fixed NPC movement history so repeated wander steps interpolate
  from the NPC's current tile instead of visually snapping from an older tile,
  and changed the long-idle return path to walk back toward spawn one tile at a
  time instead of teleporting.
- Why it was made:
  - Manual validation showed NPCs visually starting later moves from their
    original spawn point instead of from the tile they had just reached.
  - `rc_npc_tick()` overwrote `prev_x` / `prev_y` before checking whether the
    NPC moved on the previous tick, so movement history was not reliable for
    wander timing and viewer interpolation.
  - The old 500-tick idle return path directly assigned `x/y` back to
    `spawn_x/spawn_y`, which could produce a visible snap when an NPC had
    wandered away.
- Exact surfaces changed:
  - `rc-core/npc.c` now computes `moved_last` before resetting `prev_x` /
    `prev_y`, preserving the previous tile that the viewer uses for smooth
    interpolation.
  - `rc-core/npc.c` now returns long-idle NPCs toward spawn through
    `rc_can_move()` and a one-tile step, while preserving the last-move facing
    direction.
  - `tests/test_npc_facing_runtime.c` now covers repeated wander movement from
    the current tile and long-idle return walking instead of snapping.
- Upstream/downstream impact:
  - NPC movement state remains authoritative in `rc-core`; `rc-viewer` keeps
    consuming `prev_x` / `prev_y` for interpolation.
  - This does not add authoritative static spawn facing data; it fixes runtime
    movement history once NPCs begin moving.
- Verification:
  - `cmake --build build -j2 --target test_npc_facing_runtime rc-viewer`
    passed.
  - `./build/test_npc_facing_runtime` passed.
  - `./build/test_combat_phase3_movement_range_facing` passed.
  - `timeout 10 env RC_VIEWER_QUIET=1 RUNEC_UI_DECODED=1 RUNEC_UI_RUNTIME_SELFTEST=1 ./build/rc-viewer`
    passed.
  - `ctest --test-dir build --output-on-failure` passed: `62/62` tests.
  - `git diff --check` passed.
- Coverage and benchmark notes:
  - Coverage artifacts are still unavailable in this `build/` tree; `find`
    returned no `.gcda` or `.gcno` files.
  - `bash testing/run_sps_benchmark.sh` passed and reported `2,469,100` SPS
    (`405.01` ns/env-step).

## 2026-05-07 — NPC facing state correction

- Change made: fixed NPC facing so idle NPCs no longer inherit a fake player
  target and non-interacting NPCs keep the direction of their last movement.
- Why it was made:
  - Manual validation after Step 8 showed NPCs facing the same direction while
    standing still.
  - `RcNpc` instances were zero-initialized, so `facing_entity` defaulted to
    `0`, which the viewer treated as target-facing state instead of "no
    facing target."
- Exact surfaces changed:
  - `rc-core/npc.c` now initializes spawned NPCs with `facing_entity`,
    `facing_x`, and `facing_y` set to `-1`, and records a direction tile when
    non-combat wander movement advances an NPC.
  - `rc-core/combat.c` now preserves target-facing when an NPC moves toward
    the player and records last-move direction when an NPC leashes/returns to
    a tile without an interaction target.
  - `rc-viewer/viewer.c` now gives target-facing priority for interacting NPCs
    and uses movement or persisted last-move direction for non-interacting NPCs.
  - Added `tests/test_npc_facing_runtime.c` to pin spawn sentinel state and
    last-move direction behavior.
- Upstream/downstream impact:
  - The gameplay-facing state remains in `rc-core`; the viewer only consumes
    the core facing fields to choose model rotation.
  - Initial static spawn direction is still limited by the current
    `NPCList_OSRS.json` source, which does not carry authoritative facing for
    every static spawn. The fix makes movement/interaction facing correct and
    removes the false default target-facing state.
- Verification:
  - `cmake -S . -B build` passed after adding the new test file.
  - `cmake --build build -j2 --target test_npc_facing_runtime test_combat_phase3_movement_range_facing rc-viewer`
    passed.
  - `./build/test_npc_facing_runtime` passed.
  - `./build/test_combat_phase3_movement_range_facing` passed.
  - `timeout 10 env RC_VIEWER_QUIET=1 RUNEC_UI_DECODED=1 RUNEC_UI_RUNTIME_SELFTEST=1 ./build/rc-viewer`
    passed.
  - `ctest --test-dir build --output-on-failure` passed: `62/62` tests.
  - `git diff --check` passed.
- Coverage and benchmark notes:
  - Coverage artifacts are still unavailable in this `build/` tree; `find`
    returned no `.gcda` or `.gcno` files.
  - `bash testing/run_sps_benchmark.sh` passed and reported `2,985,026` SPS
    (`335.01` ns/env-step).

## 2026-05-07 — Cache Step 8 spawn completeness and slice loading

- Change made: completed `code_cleanup.md` Step 8 for the current static NPC
  spawn pipeline and viewer slice loading path.
- Why it was made:
  - Static NPC spawns are server/content data, not client-cache map data, so
    they need a direct validation surface against `NPCList_OSRS.json` and the
    generated `NSPN` binaries.
  - The viewer still defaulted to a pre-sliced Varrock spawn file, which made
    spawn coverage harder to audit and did not exercise the full-world
    region/plane slice loader by default.
- Exact surfaces changed:
  - Added `tools/validate_spawn_completeness.py`, which compares
    `data/source/data_osrs/NPCList_OSRS.json` to
    `data/spawns/world.npc-spawns.bin` and
    `data/regions/varrock.npc-spawns.bin`, checks static spawn NPC ids against
    `data/defs/npc_defs.bin`, and reports plane/region distribution.
  - Added `tools/reports/spawn_completeness.txt` with the current validation
    result: `24,110` world rows, `840` Varrock rows, `20,796` static loadable
    rows, `3,314` instance-only rows, and `0` missing static NPC definitions.
  - `rc-core/npc.h` / `rc-core/npc.c` now expose
    `RcNpcSpawnLoadStats` and `rc_load_npc_spawns_rect_stats()` so loaders can
    report total rows, matched rows, filtered rows, instance skips,
    missing-def skips, capacity skips, and per-plane counts. The NSPN loader
    now rejects unsupported spawn binary versions instead of silently treating
    them as v2.
  - `rc-viewer/viewer.c` now defaults NPC spawn loading to
    `data/spawns/world.npc-spawns.bin`, filters it to the active viewer
    rectangle across planes `0..3`, and loads NPC models for all spawned planes
    in that rectangle. `RUNEC_NPC_SPAWNS_SLICE=0` keeps the old load-all
    behavior, and an explicit `RUNEC_NPC_SPAWNS` path is still honored.
  - `tests/test_spawn_slices_runtime.c` now validates the exact current b237
    Varrock slice counts: `840` matched rows, `837` spawned static NPCs,
    `3` instance-only skips, `0` missing-def skips, and spawned plane counts
    `[775, 38, 23, 1]`.
- Upstream/downstream impact:
  - Viewer spawn loading now exercises the same full-world `NSPN` file that
    runtime tests use, which makes missing NPCs by region/plane easier to
    diagnose.
  - Instance-only rows remain excluded from static world loading. Activity or
    instance entry code still owns spawning those NPCs when the relevant
    encounter/area is entered.
  - This does not yet solve full scene streaming or plane-aware rendering for
    terrain/objects/collision; that is the next cache-pipeline step.
- Verification:
  - `python3 -m py_compile tools/validate_spawn_completeness.py tools/export_spawns.py`
    passed.
  - `python3 tools/validate_spawn_completeness.py` passed and wrote
    `tools/reports/spawn_completeness.txt`.
  - `cmake --build build -j2 --target test_spawn_slices_runtime rc-viewer`
    passed.
  - `./build/test_spawn_slices_runtime` passed.
  - `ctest --test-dir build --output-on-failure` passed: `61/61` tests.
  - `timeout 10 env RC_VIEWER_QUIET=1 RUNEC_UI_DECODED=1 RUNEC_UI_RUNTIME_SELFTEST=1 ./build/rc-viewer`
    passed and reported `viewer npc slice: rows=24110 matched=840 spawned=837
    planes=[775,38,23,1]`.
  - `git diff --check` passed.
- Coverage and benchmark notes:
  - Coverage artifacts are still unavailable in this `build/` tree; `find`
    returned no `.gcda` or `.gcno` files.
  - `bash testing/run_sps_benchmark.sh` passed and reported `2,997,945` SPS
    (`333.56` ns/env-step).

## 2026-05-07 — Cache Step 7 animation contract

- Change made: completed `code_cleanup.md` Step 7 by pairing animation export
  and C loading around an explicit RuneC animation binary contract.
- Why it was made:
  - The previous `.anims` file shape was a legacy minimal header with no
    format version, no declared frame count, and ambiguous magic bytes.
  - `tools/export_npc_anims.py` invoked the shared exporter by mutating
    `NEEDED_ANIMATIONS` and `sys.argv`, which made NPC animation export harder
    to reason about and test as part of the cache foundation.
- Exact surfaces changed:
  - `tools/cache_pipeline/export_animations.py` now writes `ANM2` v2 files
    with a versioned header, framebase/sequence/sequence-frame counts, and
    flags documenting that the payload contains normal frame transforms while
    presentation metadata is omitted.
  - `rc-viewer/anims.h` now loads `ANM2`, preserves legacy local `.anims`
    compatibility, validates truncation and declared sequence-frame totals,
    and reports the loaded animation format version.
  - `tools/export_npc_anims.py` now calls the exporter through an explicit API
    instead of mutating exporter globals and `sys.argv`.
  - `tools/cache_pipeline/rc_cache/definitions.py` now preserves modern
    sequence metadata fields needed by the cache foundation, including
    AnimMaya id/range/masks, signed vertical offset, and cross-world sound
    flags.
  - Added `tools/cache_pipeline/validate_anims.py` for direct binary validation
    of generated `.anims` files and required sequence IDs.
  - Regenerated local `data/anims/player.anims` and `data/anims/npcs.anims`
    from the b237 OpenRS2 cache as `ANM2` data. These generated files remain in
    the separate local `data/` repository boundary.
- Upstream/downstream impact:
  - Runtime animation playback remains local C code and does not call
    RuneLite, RSMod, or any reference checkout.
  - Player and NPC animation bundles now load through the same explicit
    exporter/runtime contract. Skeletal/Maya presentation metadata is decoded
    for foundation awareness, but the active runtime payload still exports the
    normal frame transforms used by the current viewer path.
  - The next cache-pipeline work is spawn completeness validation,
    region/plane streaming, and plane-aware rendering/runtime contracts.
- Verification:
  - `python3 -m py_compile tools/cache_pipeline/export_animations.py tools/export_npc_anims.py tools/cache_pipeline/validate_anims.py tools/cache_pipeline/rc_cache/definitions.py`
    passed.
  - `python3 tools/cache_pipeline/export_animations.py --modern-cache tools/cache_pipeline/source/current_fightcaves_demo/data/cache --output data/anims/player.anims`
    passed and wrote `ANM2` with `16` framebases, `72` sequences, and `1111`
    sequence frames.
  - `python3 tools/export_npc_anims.py --cache tools/cache_pipeline/source/current_fightcaves_demo/data/cache --npc-defs data/defs/npc_defs.bin --output data/anims/npcs.anims --include-player`
    passed and wrote `ANM2` with `573` framebases, `2105` sequences, and
    `34850` sequence frames.
  - `python3 tools/cache_pipeline/validate_anims.py` passed for both generated
    files with `--expect-version 2` and required player/combat sequence IDs.
  - `python3 tools/cache_pipeline/validate_b237_cache.py --region 50,53 --region 48,53`
    passed after the sequence decoder metadata update.
  - `cmake --build build -j2 --target rc-viewer` passed.
  - `timeout 20 env RC_VIEWER_QUIET=1 RUNEC_UI_DECODED=1 RUNEC_UI_RUNTIME_SELFTEST=1 ./build/rc-viewer`
    passed and loaded both `ANM2` animation files through the real viewer path.
  - `ctest --test-dir build --output-on-failure` passed: `61/61` tests.
  - `git diff --check` passed.
- Coverage and benchmark notes:
  - Coverage artifacts are still unavailable in this `build/` tree; `find`
    returned no `.gcda` or `.gcno` files, so line coverage for the changed
    loader path could not be measured here.
  - `bash testing/run_sps_benchmark.sh` passed and reported `3,136,279` SPS
    (`318.85` ns/env-step). This change is primarily exporter/viewer
    data-loading work, so no combat simulation throughput change is expected.

## 2026-05-06 — Dynamic model baked-lighting isolation

- Change made: added an exporter-side unlit model-color path for dynamic
  player/NPC/equipment models and regenerated the active dynamic model assets.
- Why it was made:
  - Disabling the viewer dynamic shader did not remove the dark back-side
    shading on player/NPC/equipment models because that contrast was already
    baked into the exported vertex colors.
  - We need to isolate dynamic character appearance from scenery lighting while
    keeping terrain/object cache lighting intact.
- Exact surfaces changed:
  - `tools/cache_pipeline/export_models.py` now supports
    `model_lighting="unlit"` in `expand_model`/`write_models_binary`, plus a
    `--model-lighting` CLI switch for the legacy player/fallback exporter.
  - `tools/cache_pipeline/export_item_render_models.py` now writes dynamic
    item/body/equipment models with unlit material colors.
  - `tools/export_npc_models_full.py` now writes NPC models with unlit material
    colors.
  - `rc-viewer/viewer.c` has `RUNEC_DYNAMIC_MODEL_SHADER=0` as a separate
    diagnostic toggle for the remaining viewer-side dynamic shader.
  - Regenerated `data/models/items.models`, `data/models/item_render.map`,
    `data/models/player.models`, `data/models/npcs.models`, and
    `data/models/npcs.atlas` from the local b237 cache path.
- Verification:
  - `python3 -m py_compile tools/cache_pipeline/export_models.py tools/cache_pipeline/export_item_render_models.py tools/export_npc_models_full.py`
    passed.
  - `cmake --build build -j2 --target rc-viewer` passed.
  - Viewer selftest passed both normally and with
    `RUNEC_DYNAMIC_MODEL_SHADER=0`.
  - `ctest --test-dir build --output-on-failure` passed: `61/61` tests.
  - `git diff --check` passed.

## 2026-05-06 — Item equipment render and stack text polish

- Change made: tightened the item/equipment visual path and inventory stack
  count text rendering after manual validation found partyhat/shield render
  artifacts and choppy stack counts.
- Why it was made:
  - Wearable item models were being exported with baked face-priority geometry
    offsets. Face priorities are client draw-order metadata; baking them into
    small equipment models can separate coplanar faces and create visible
    triangle artifacts.
  - The decoded UI item-container path drew stack counts at size `10` with the
    small OSRS font, while the rest of the UI clamps tiny text to the readable
    native size.
- Exact surfaces changed:
  - `tools/cache_pipeline/export_item_render_models.py` now writes item,
    equipment, and default body render models without priority displacement,
    while still preserving priorities for animation metadata. Its default
    export set now includes the current runtime/combat `SIM_ITEM_IDS` plus
    coin-stack variants, so validation covers the items the viewer can actually
    equip or display.
  - `data/models/items.models`, `data/models/items.atlas`, and
    `data/models/item_render.map` were regenerated from the local b237 cache.
    The current item render set loads `348` MDL3 models and `134` render
    records.
  - `rc-viewer/ui_assets.c` / `.h` now expose one shared OSRS shadow-text
    helper, and both manual and decoded UI stack-count paths use it so stack
    text gets the same readable OSRS font treatment as the rest of the UI.
- Verification:
  - `python3 -m py_compile tools/cache_pipeline/export_item_render_models.py`
    passed.
  - `cmake --build build -j2 --target rc-viewer` passed.
  - `timeout 20 env RC_VIEWER_QUIET=1 RUNEC_UI_DECODED=1 RUNEC_UI_RUNTIME_SELFTEST=1 ./build/rc-viewer`
    passed and reported `ui runtime selftest: PASS`.
  - `ctest --test-dir build --output-on-failure` passed: `61/61` tests.
  - `git diff --check` passed.

## 2026-05-06 — Cache Step 6 generated-data closeout

- Change made: closed the current `code_cleanup.md` Step 6 cache/data
  validation blockers and moved the active cache-pipeline stop point to Step 7
  animation export/runtime loading cleanup.
- Why it was made:
  - UI/runtime validation exposed generated-data mismatches after the UI
    cleanup pass: item definitions did not cover the full b237 item config
    range, `Fire Blast` lacked the expected combat value, and several
    full-game loader datasets were absent from this checkout.
  - The full regression suite also exposed stale assumptions around NDEF v4
    readers, NPC display-name precedence, and an interaction routing test that
    depended on current b237 collision.
- Exact surfaces changed:
  - `tools/export_items.py` now preserves every b237 cache item row, including
    placeholder/null rows, so item IDs stay aligned with cache config ids.
  - `tools/export_normalization.py` and
    `tools/export_regular_npc_mechanics.py` now read NDEF v4 NPC records with
    action-option strings; regular NPC mechanics also preserves wiki-sourced
    poison/venom/disease IDs even when a b237 NPC variant has combat level 0.
  - `tools/export_npc_defs_full.py` strips cache color tags from NPC display
    names and prevents activity-spawn labels from overwriting canonical
    cache/wiki names.
  - `tools/export_spawn_sources.py` now skips NDEF v4 NPC option strings in
    its lightweight name reader.
  - `tests/test_interaction_engine_phase7.c` now uses a no-collision skilling
    config for generic interaction routing tests so it does not depend on the
    current generated map collision.
  - Local ignored data/source inputs were restored for spells, osrsreboxed
    docs, data_osrs transports/teleports, Near-Reality area geometry, and the
    curated runtime corpus; generated runtime data/reports were regenerated.
  - `code_cleanup.md` now marks Step 6 complete enough for the current viewer
    slice and records Step 7 as the next cache-pipeline action.
  - `work_highlevel.md` now names Step 7 as the next cache-pipeline step
    before returning to combat fidelity in `work.md`.
- Upstream/downstream impact:
  - Runtime and exporters remain repo-local; no RuneLite/RSMod/reference repo
    calls are used at runtime.
  - `data/defs/combat_visuals.tsv` is still absent. It is not blocking this
    Step 6 closeout, but combat fidelity still needs cache-backed or
    content-backed projectile/spot-animation selection.
- Verification:
  - `python3 -m py_compile` passed for the touched exporters and related
    scripts.
  - `python3 tools/cache_pipeline/validate_b237_cache.py --region 50,53 --region 48,53`
    passed.
  - `python3 tools/cache_pipeline/validate_ui_assets.py --assets-dir data/sprites/ui --require-transparent side_icon_inventory --require-transparent orb_frame_0 --require-transparent magicon_0 --require-transparent prayeron_0 --require-transparent fixed_map_mask --require-transparent fixed_compass_mask --require-transparent standard_spell_on_0 --require-transparent combaticons_0`
    passed: `335/335` required sprites and `8/8` transparent cutouts.
  - `timeout 20 env RC_VIEWER_QUIET=1 RUNEC_UI_DECODED=1 RUNEC_UI_RUNTIME_SELFTEST=1 ./build/rc-viewer`
    passed and reported `ui runtime selftest: PASS`.
  - `ctest --test-dir build --output-on-failure` passed: `61/61` tests.
  - `cmake --build build -j2` passed.
- Coverage and benchmark notes:
  - Line coverage remains unavailable because this `build/` tree has no gcov
    artifacts.
  - No SPS benchmark was rerun for this exporter/data closeout; the only C
    change is test isolation, not runtime behavior.

## 2026-05-06 — UI Phase 6 validation closeout

- Change made: completed `ui_cleanup.md` Phase 6 by adding repeatable viewer
  validation hooks and running the full UI asset/interface/screenshot/runtime
  smoke pass.
- Why it was made:
  - Phase 5 added listener-driven modal/overlay behavior, but those paths
    still needed an automated way to validate without manual clicking.
  - The UI cleanup pass needed a clear closeout before returning to the
    remaining cache pipeline work in `code_cleanup.md`.
  - Modal/overlay screenshots need deterministic startup hooks so visual
    validation can capture decoded surfaces on demand.
- Exact surfaces changed:
  - `rc-viewer/ui.h` and `rc-viewer/ui.c` now expose
    `runec_ui_runtime_selftest`, a local runtime smoke that validates decoded
    UI readiness, required groups, listener/trigger preservation, open
    top/chat/orb state, side-tab switching, inventory/equipment overrides,
    context state, selected item/spell targets, magic filter handlers,
    equipment stats modal, guide-price overlay, death-keep modal, follower
    call hook, transmit dispatch, and modal/overlay close behavior.
  - `rc-viewer/ui.c` now supports `RUNEC_UI_OPEN_MODAL=<group>` and
    `RUNEC_UI_OPEN_SIDE_OVERLAY=<group>` for deterministic screenshot capture
    of decoded modal/overlay groups.
  - `rc-viewer/viewer.c` now supports `RUNEC_UI_RUNTIME_SELFTEST=1` to run the
    UI runtime selftest through the real viewer initialization path, and
    `RC_VIEWER_QUIET=1` to suppress raylib trace noise during validation runs.
  - `ui_cleanup.md` now marks Phase 6 complete enough to close the UI cleanup
    pass and records the remaining UI follow-ups as future system integration
    work.
  - `work_highlevel.md` now moves the active stop point from UI cleanup back
    to cache pipeline cleanup in `code_cleanup.md`.
- Upstream/downstream impact:
  - Runtime remains local C code and does not call RuneLite, RSMod, or
    `runescape-rl-reference`.
  - The new validation hooks are debug/test paths only; they do not change
    normal UI behavior unless their environment variables are set.
  - The next planned work is to return to `code_cleanup.md`, then resume
    combat fidelity in `work.md`.
  - The guide-price side overlay opens and captures through the runtime path
    but is still visually sparse with current exported/runtime data; final
    price-checker parity remains follow-up work.
- Verification:
  - `python3 tools/cache_pipeline/validate_ui_assets.py --assets-dir data/sprites/ui --require-transparent side_icon_inventory --require-transparent orb_frame_0 --require-transparent magicon_0 --require-transparent prayeron_0 --require-transparent fixed_map_mask --require-transparent fixed_compass_mask --require-transparent standard_spell_on_0 --require-transparent combaticons_0`
    passed: `335/335` required UI sprites and `8/8` selected transparent
    cutout sprites.
  - `python3 tools/cache_pipeline/export_ui_interfaces.py --cache tools/cache_pipeline/source/current_fightcaves_demo/data/cache --dump-root tools/cache_pipeline/source/osrs-dumps --output /tmp/runec-phase6-interface_manifest.json --debug-output /tmp/runec-phase6-interface_debug.txt --binary-output /tmp/runec-phase6-interfaces.bin`
    passed: decoded `26251` components (`24155` IF3, `2096` IF1) and wrote a
    v2 `RCUIBIN2` temporary binary with `28` exported core groups.
  - `timeout 20 env RC_VIEWER_QUIET=1 RUNEC_UI_DECODED=1 RUNEC_UI_RUNTIME_SELFTEST=1 ./build/rc-viewer`
    passed and reported `ui runtime selftest: PASS`.
  - `cc -fsyntax-only -std=c11 -Ilib/raylib/include -I. rc-viewer/ui.c`
    passed.
  - `cc -fsyntax-only -std=c11 -Irc-core -Irc-content -Ilib/raylib/include -I. rc-viewer/viewer.c`
    passed.
  - `cc -fsyntax-only -std=c11 -Ilib/raylib/include -I. rc-viewer/ui_interfaces.c`
    passed.
  - `python3 -m py_compile tools/cache_pipeline/export_ui_interfaces.py tools/cache_pipeline/rc_cache/interfaces.py tools/cache_pipeline/validate_ui_assets.py`
    passed.
  - `cmake --build build -j2 --target rc-viewer` passed.
  - `ctest --test-dir build -R 'interaction_engine_phase8|combat_phase4|combat_phase8|combat_phase10|combat_phase11' --output-on-failure`
    passed all 5 targeted tests.
  - Fixed-viewport viewer screenshot smokes exited cleanly and wrote valid
    1280x720 PNGs for gameframe, inventory, equipment, combat, prayer,
    spellbook, decoded chatbox, minimap/orbs, equipment modal, price overlay,
    and death-keep modal under `/tmp/runec-ui-phase6-*.png`.
  - `git diff --check` passed.
- Known blockers and caveats:
  - `ctest --test-dir build -R 'inventory|prayer|spell' --output-on-failure`
    remains blocked by local generated-data mismatch: item-def count below the
    test's `> 30000` assertion and `Fire Blast` max-hit data missing the
    expected Void-derived value.
  - Line coverage remains unavailable because this `build/` tree has no gcov
    artifacts.
  - `bash testing/run_sps_benchmark.sh` passed and reported `3168466` SPS
    (`315.61` ns/env-step). No before/after comparison is available because
    Phase 6 builds on already-staged Phase 3-5 UI changes.

## 2026-05-06 — UI Phase 5 listener subset

- Change made: completed `ui_cleanup.md` Phase 5 by adding a narrow local
  listener/transmit handler layer for decoded b237 UI components without
  implementing a full CS2 VM.
- Why it was made:
  - Phase 4 routed core UI actions into local gameplay APIs, but decoded
    listener metadata was still not represented in the C runtime.
  - Dynamic UI surfaces such as magic filters, equipment stats, price checker,
    death-keep, and follower-call controls need component-id-driven behavior
    rather than screen-coordinate hardcoding.
  - Later UI work needs unhandled listeners and transmit triggers preserved so
    we can add targeted handlers without redoing the cache export format.
- Exact surfaces changed:
  - `tools/cache_pipeline/export_ui_interfaces.py` now writes interface binary
    version 2 with decoded listener payloads and var/inv/stat transmit trigger
    arrays; it also exports the additional core support groups `equipment`,
    `equipment_side`, `ge_pricechecker_side`, and `deathkeep`.
  - `tools/cache_pipeline/rc_cache/interfaces.py` now includes listener and
    trigger payloads in debug output.
  - `rc-viewer/ui_interfaces.h` and `rc-viewer/ui_interfaces.c` now load and
    store component listener masks, listener payload values, trigger masks, and
    trigger payload values while retaining version 1 binary compatibility.
  - Decoded hit-testing now treats listener-driven components as interactive
    when relevant listener masks are present, even if ordinary action strings
    are absent.
  - `rc-viewer/ui.h` and `rc-viewer/ui.c` now track magic filter state and add
    a table-driven local listener handler registry keyed by decoded group/file
    id plus listener kind.
  - Current local handlers cover magic spellbook filter visibility, equipment
    stats modal open, guide-price checker side overlay open, death-kept-items
    modal open, and follower-call chat/system feedback.
  - Opened decoded interfaces now dispatch `onLoad`, runtime inventory/
    equipment/stat refresh runs the transmit dispatch pass, and active decoded
    modals/side overlays capture clicks and close with Escape through opened
    interface mount rectangles.
  - `ui_cleanup.md` now marks Phase 5 complete enough to proceed into Phase 6
    full UI validation and records unresolved CS2/full-system follow-ups.
- Upstream/downstream impact:
  - Runtime remains local C code and does not call RuneLite, RSMod, or
    `runescape-rl-reference`.
  - Generated local `data/ui/interfaces.bin` is now version 2; it was
    regenerated locally from the b237 cache for validation, but generated
    `data/` artifacts remain ignored and should not be committed in this repo.
  - Phase 6 can now validate the full UI surface, including the new decoded
    modal/overlay hooks.
  - Full CS2 behavior, quick-prayer presets, autocast combat state, final chat
    routing, object/player selected-target dispatch, bank/shop/dialogue
    interfaces, and final OSRS minimap rendering remain explicit follow-up
    work.
- Verification:
  - `cc -fsyntax-only -std=c11 -Ilib/raylib/include -I. rc-viewer/ui_interfaces.c`
    passed.
  - `cc -fsyntax-only -std=c11 -Ilib/raylib/include -I. rc-viewer/ui.c`
    passed.
  - `python3 -m py_compile tools/cache_pipeline/export_ui_interfaces.py tools/cache_pipeline/rc_cache/interfaces.py`
    passed.
  - `python3 tools/cache_pipeline/export_ui_interfaces.py --cache tools/cache_pipeline/source/current_fightcaves_demo/data/cache --dump-root tools/cache_pipeline/source/osrs-dumps --skip-all`
    regenerated the local ignored UI binary/debug outputs and decoded `1868`
    IF3 components across `28` core groups.
  - `cmake --build build -j2 --target rc-viewer` passed.
  - `ctest --test-dir build -R 'interaction_engine_phase8|combat_phase4|combat_phase8|combat_phase10|combat_phase11' --output-on-failure`
    passed all 5 targeted tests.
  - Decoded viewer screenshot smokes exited cleanly and wrote valid 1280x720
    PNGs for Equipment, Spellbook, and Inventory:
    `/tmp/runec-ui-phase5-equipment.png`,
    `/tmp/runec-ui-phase5-spellbook.png`, and
    `/tmp/runec-ui-phase5-inventory.png`.
  - `git diff --check` passed.
- Coverage and benchmark notes:
  - Line coverage was reviewed but remains unavailable because this `build/`
    tree has no gcov artifacts.
  - `bash testing/run_sps_benchmark.sh` passed and reported `3163619` SPS
    (`316.09` ns/env-step) for the combat benchmark.

## 2026-05-06 — UI Phase 4 gameplay hook bridge

- Change made: completed `ui_cleanup.md` Phase 4 by routing basic UI gameplay
  actions from decoded/manual UI intents into local `rc-core` APIs instead of
  leaving the hook surface as viewer-only logs.
- Why it was made:
  - Phase 3 gave the viewer a decoded opened-interface/runtime state model,
    but several gameplay actions still stopped at UI intent emission.
  - The UI needs stable local hooks for inventory, equipment, combat, prayer,
    magic, minimap, and chat before listener/script work can safely build on
    it.
  - Item/spell selected-target behavior needs repo-local core entry points for
    inventory items and widgets, matching the existing NPC/object/ground-item
    interaction API shape.
- Exact surfaces changed:
  - `rc-core/api.h` and `rc-core/tick.c` now expose and implement
    `rc_player_use_inventory_item_on_inventory_item`,
    `rc_player_use_inventory_item_on_widget`,
    `rc_player_cast_spell_on_inventory_item`, and
    `rc_player_cast_spell_on_widget`.
  - `tests/test_interaction_engine_phase8.c` now validates item-on-item,
    spell-on-item, item-on-widget, and spell-on-widget through the interaction
    engine.
  - `rc-viewer/ui.h` and `rc-viewer/ui.c` now represent quick-prayer slot,
    quick-prayer toggle, and autocast as explicit intents, track active prayer
    state, route prayer context actions correctly, and draw active prayer
    sprites with `prayeron_*`.
  - `rc-viewer/viewer.c` now syncs active prayers from core, loads generated
    local prayer/player-action data when present, dispatches prayer toggles to
    `rc_player_set_prayer`, dispatches generic decoded component actions to
    `rc_player_widget_action`, and routes selected item/spell targets to
    inventory slots, decoded widgets, NPCs, and ground items.
  - Inventory/equipment examine actions now call the core interaction surface
    before logging the visible item name.
  - Quick-prayer setup/toggle, autocast, and chat send are now explicit logged
    hooks rather than implicit missing behavior; their full backend systems are
    still future work.
  - `ui_cleanup.md` now marks Phase 4 complete enough to proceed into Phase 5
    and records remaining listener/script, object/player target, minimap, and
    backend integration work.
- Upstream/downstream impact:
  - Runtime remains local C code and does not call RuneLite, RSMod, or
    `runescape-rl-reference`.
  - Phase 5 can now focus on decoded listener/transmit behavior instead of
    basic input-to-core action plumbing.
  - Full quick-prayer presets, autocast combat state, chat routing, object/
    player selected-target picking, and final OSRS minimap rendering are still
    explicit follow-up items.
- Verification:
  - `cc -fsyntax-only -std=c11 -Irc-core -Irc-content -Ilib/raylib/include -I. rc-core/tick.c`
    passed.
  - `cc -fsyntax-only -std=c11 -Ilib/raylib/include -I. rc-viewer/ui.c`
    passed.
  - `cc -fsyntax-only -std=c11 -Irc-core -Irc-content -Ilib/raylib/include -I. rc-viewer/viewer.c`
    passed.
  - `cc -fsyntax-only -std=c11 -Irc-core -Irc-content -I. tests/test_interaction_engine_phase8.c`
    passed.
  - `cmake --build build -j2 --target rc-viewer` passed.
  - `ctest --test-dir build -R 'interaction_engine_phase8|combat_phase4|combat_phase8|combat_phase10|combat_phase11' --output-on-failure`
    passed all 5 targeted tests.
  - Generated ignored local validation data with
    `python3 tools/export_prayers.py`,
    `python3 tools/export_player_actions.py`, and
    `python3 tools/export_spells.py`.
  - `ctest --test-dir build -R 'inventory|prayer|spell' --output-on-failure`
    remains blocked by generated-data/source mismatch: inventory expects an
    item-def count above `30000`, while the current local item export reports
    fewer records; prayer/action loading succeeds after generation, but the
    spell runtime test still fails the `Fire Blast` max-hit assertion because
    the spell exporter has no local Void max-hit source in this checkout.
  - Viewer screenshot smokes exited cleanly and wrote valid 1280x720 PNGs for
    Prayer, Spellbook, and Inventory:
    `/tmp/runec-ui-phase4-prayer.png`,
    `/tmp/runec-ui-phase4-spellbook.png`, and
    `/tmp/runec-ui-phase4-inventory.png`.
- Coverage and benchmark notes:
  - Line coverage was reviewed but remains unavailable because this `build/`
    tree has no gcov artifacts.
  - `bash testing/run_sps_benchmark.sh` passed and reported `3123333` SPS
    (`320.17` ns/env-step) for the combat benchmark. No before/after comparison
    is available because Phase 4 was already partly staged in the working tree
    when this validation pass began.

## 2026-05-06 — UI Phase 3 opened-interface runtime closeout

- Change made: finished `ui_cleanup.md` Phase 3 by widening decoded UI runtime
  state from side tabs into explicit top/subinterface/overlay/modal opens and
  moving decoded inventory/equipment item rendering onto runtime overrides.
- Why it was made:
  - Phase 3 was paused after the combat/text correction with the last major
    runtime gap still open: decoded UI could render side groups, but it did
    not yet have the local equivalent of an opened-interface table for
    top-level, mounted subinterfaces, overlays, and modals.
  - The b237 `inventory` interface exposes a single `items` container
    component rather than one child component per slot, so retiring the manual
    inventory grid required an item-container override path.
  - Worn equipment needed the same live item override shape as inventory so
    decoded components can receive runtime item ids, display icon ids,
    quantities, and selected state.
- Exact surfaces changed:
  - `rc-viewer/ui.h` now exposes `RUNEC_UI_TAB_NONE`,
    `RUNEC_UI_MOUNT_MODAL`, item-container override storage, target component
    ids/z-order on opened interfaces, and APIs for opening top/sub/overlay/
    modal interfaces, moving interfaces, setting component items, and setting
    component animations.
  - `rc-viewer/ui.c` now opens `toplevel_osrs_stretch` as the top-level
    interface, mounts chatbox/orbs/side content through decoded top-level
    component ids, resolves mount rectangles by component id, and renders
    overlay/modal groups from the open table.
  - `rc-viewer/ui.c` now seeds a decoded item-container override for
    `inventory:items` from live inventory state and seeds decoded worn-slot
    component item overrides from live equipment state.
  - Decoded inventory/equipment hit testing now resolves slots from decoded
    component rectangles plus the runtime item-container/worn-slot override
    data, while retaining the old manual path as a fallback.
  - `rc-viewer/ui_interfaces.h` and `rc-viewer/ui_interfaces.c` now support
    item overrides, animation ids, item-container override records, stack
    quantity formatting, local item-icon drawing, magenta missing-icon
    fallback, and component rectangle lookup by full component id.
  - `ui_cleanup.md` now marks Phase 3 complete enough to proceed to Phase 4
    and records the remaining asset/default-readiness backlog separately.
- Upstream/downstream impact:
  - Runtime still does not call RuneLite, RSMod, or reference repos; all new
    behavior is local C code using local generated cache data and local assets.
  - Phase 4 can now focus on gameplay hook formalization instead of needing
    another UI-layout/runtime bridge first.
  - Phase 5 still owns listener/script behavior for dynamic dialog/modal
    correctness; the modal/overlay mount APIs are ready for that work but no
    gameplay modal is opened by default yet.
- Verification:
  - `cc -fsyntax-only -std=c11 -Ilib/raylib/include -I. rc-viewer/ui_interfaces.c`
    passed.
  - `cc -fsyntax-only -std=c11 -Ilib/raylib/include -I. rc-viewer/ui.c`
    passed.
  - `cc -fsyntax-only -std=c11 -Ilib/raylib/include -I. rc-viewer/viewer.c`
    passed.
  - `cc -fsyntax-only -std=c11 -Ilib/raylib/include -I. rc-viewer/ui_assets.c`
    passed.
  - `cmake --build build -j2 --target rc-viewer` passed.
  - `python3 -m py_compile tools/cache_pipeline/export_ui_interfaces.py tools/cache_pipeline/rc_cache/interfaces.py tools/cache_pipeline/validate_ui_assets.py`
    passed.
  - `python3 tools/cache_pipeline/validate_ui_assets.py --assets-dir data/sprites/ui --require-transparent side_icon_inventory --require-transparent orb_frame_0 --require-transparent magicon_0 --require-transparent prayeron_0 --require-transparent fixed_map_mask --require-transparent fixed_compass_mask`
    passed.
  - Decoded viewer screenshot smokes exited cleanly and wrote valid 1280x720
    PNGs for Inventory, Equipment, Combat, Spellbook, Prayer, Skills,
    Settings, and decoded Chatbox.
  - `ctest --test-dir build -R 'combat_phase4|combat_phase8|combat_phase10|combat_phase11' --output-on-failure`
    passed all 4 targeted combat-adjacent checks.
  - `ctest --test-dir build -R 'inventory|prayer|spell' --output-on-failure`
    remains blocked by local data fixtures: `test_inventory_equipment_runtime`
    asserts `g_item_def_count > 30000`, while the local item-def fixture loads
    below that threshold, and `test_prayer_spell_actions_runtime` expects
    `data/defs/prayers.bin`, which is not present in this checkout.
- Coverage and benchmark notes:
  - Line coverage was reviewed but deferred because the current `build/` tree
    has no gcov instrumentation artifacts; the changed viewer/raylib paths
    were exercised through compile checks and fixed-viewport screenshot smokes.
  - No SPS benchmark was run because this change is confined to `rc-viewer`
    presentation/input translation and does not alter `rc-core` headless sim
    tick cost or training throughput.

## 2026-05-06 — UI Phase 3 combat tab and text correction

- Change made: corrected the combat tab to use b237 weapon-category combat
  styles and the real `combaticons*` sprite groups, and tightened small UI
  text rendering for chat, inventory stack counts, decoded text, and combat
  labels.
- Why it was made:
  - The combat tab was still partially hardcoded to whip labels and was using
    `sideicons_interface_*`, which are side-tab/UI icons rather than combat
    stance icons.
  - Decoded `combat_interface` components and the manual live overlay were
    both drawing combat text, which caused doubled/blurred labels.
  - The raw decoded combat interface shows listener-controlled autocast
    components by default because the local CS2/listener runtime is not
    implemented yet.
- Exact surfaces changed:
  - `rc-viewer/ui.c` now embeds the local b237
    `combat_interface_weapon_category` table from the Joshua-F dump for
    labels, stance modes, visible button indexes, and icon assets.
  - `rc-viewer/ui.h` now carries the current combat weapon name, weapon
    category, and resolved visible combat style buttons in UI state.
  - `rc-viewer/viewer.c` syncs the equipped weapon display name and core
    combat weapon category into the UI each frame.
  - Combat tab rendering and hit testing now use the resolved weapon profile,
    so unarmed, whip, bow, staff, spear, axe, etc. can present different
    button sets while still dispatching style indexes into core combat.
  - The combat side tab bypasses raw decoded component drawing for now and
    uses the live b237 table-driven panel as the sole combat tab surface,
    avoiding stray listener-controlled components such as autocast `Spell`.
  - `tools/cache_pipeline/export_sprites_modern.py` now exports
    `combaticons_0..19`, `combaticons2_0..19`, and `combaticons3_0..19`;
    `rc-viewer/ui_assets.c` registers them as UI assets.
  - UI fonts are loaded at closer-to-use sizes, text draw positions are
    rounded to pixel coordinates, and stack/combat labels use a slightly
    larger minimum size for legibility.
- Verification:
  - `python3 tools/cache_pipeline/export_sprites_modern.py --cache tools/cache_pipeline/source/current_fightcaves_demo/data/cache --output data/sprites/ui`
    exported `555` sprite groups with `0` failures.
  - `cc -fsyntax-only -std=c11 -Ilib/raylib/include -I. rc-viewer/ui.c`
    passed.
  - `cc -fsyntax-only -std=c11 -Ilib/raylib/include -I. rc-viewer/ui_assets.c`
    passed.
  - `cc -fsyntax-only -std=c11 -Ilib/raylib/include -I. rc-viewer/ui_interfaces.c`
    passed.
  - `cc -fsyntax-only -std=c11 -Ilib/raylib/include -I. rc-viewer/viewer.c`
    passed.
  - `python3 -m py_compile tools/cache_pipeline/export_sprites_modern.py`
    passed.
  - `cmake --build build -j2 --target rc-viewer` passed.
  - `timeout 8 env RC_VIEWER_EXIT_FRAMES=2 RUNEC_UI_DECODED=1 RUNEC_UI_START_TAB=Combat RC_VIEWER_SCREENSHOT=/tmp/runec-ui-combat-styles-2.png ./build/rc-viewer`
    exited cleanly and wrote a valid 1280x720 PNG.
  - `ctest --test-dir build -R 'combat_phase4|combat_phase8|combat_phase10|combat_phase11' --output-on-failure`
    passed all 4 targeted combat tests.
- Remaining Phase 3 stop point:
  - Do not proceed to Phase 4 hooks yet.
  - Resume Phase 3 with the full opened-interface model, decoded item-container
    overrides, item/model/head widget override fields, and fixed-viewport
    screenshot smoke captures for the core UI surfaces.

## 2026-05-06 — UI Phase 3 decoded component dispatch

- Change made: advanced `ui_cleanup.md` Phase 3 by making decoded side-tab
  components participate in runtime overrides, event-mask validation, context
  menu op preservation, and typed UI intent dispatch.
- Why it was made:
  - The decoded renderer had usable component hit testing, but interactions
    still fell back too often to manual tab grids and did not validate decoded
    component ops against opened-interface masks.
  - The runtime needs cache-backed component ids to become the primary UI
    surface before we wire gameplay hooks for prayers, spells, combat styles,
    equipment, chat, and later modal interfaces.
- Exact surfaces changed:
  - `rc-viewer/ui_interfaces.h` and `rc-viewer/ui_interfaces.c` now support
    per-component runtime overrides for text, hidden state, model id, color,
    and scroll, and apply those overrides during decoded rendering and hit
    testing.
  - `rc-viewer/ui.h` and `rc-viewer/ui.c` now track component override records,
    decoded event-mask records, and original context-menu widget op indexes.
  - Opening decoded side interfaces seeds component event masks from decoded
    click masks; decoded right-click menu entries and left-click actions are
    validated against those masks before dispatch.
  - Decoded side-tab left-clicks now route stable b237 component ids into typed
    UI intents for combat styles/toggles, worn equipment slots, prayer buttons,
    standard spellbook buttons, stats skills, and fallback decoded component
    actions.
  - Combat and stats decoded components receive narrow live text overrides for
    combat level/style/special text and total level.
  - `work.md`, `code_cleanup.md`, and `changelog.md` no longer reference a
    missing separate combat document; `work.md` remains the active combat
    planning source for this checkout.
  - `ui_cleanup.md` now records the current Phase 3 status and the remaining
    work: full opened-interface APIs, item-container overrides, model/head
    overrides, Phase 4 hooks, and final screenshot validation.
- Current caveat:
  - Inventory still uses the manual slot grid because the current b237
    `inventory` export exposes one `items` container component rather than
    stable child components per slot. Retiring that grid needs an
    item-container override path.
- Verification:
  - `cc -fsyntax-only -std=c11 -Ilib/raylib/include -I. rc-viewer/ui_interfaces.c`
    passed.
  - `cc -fsyntax-only -std=c11 -Ilib/raylib/include -I. rc-viewer/ui.c`
    passed.
  - `cc -fsyntax-only -std=c11 -Ilib/raylib/include -I. rc-viewer/viewer.c`
    passed.
  - `cmake --build build -j2 --target rc-viewer` passed.
  - `python3 -m py_compile tools/cache_pipeline/export_ui_interfaces.py tools/cache_pipeline/rc_cache/interfaces.py tools/cache_pipeline/validate_ui_assets.py`
    passed.
  - Viewer screenshot smokes exited cleanly and wrote valid PNGs for decoded
    Combat, Spellbook, Prayer, and Equipment side tabs.
  - `ctest --test-dir build -R 'inventory|prayer|spell' --output-on-failure`
    is blocked by current data fixtures, not this UI compile path:
    `test_inventory_equipment_runtime` asserts `g_item_def_count > 30000`,
    while the local `data/defs/items.bin` fixture loads below that threshold;
    `test_prayer_spell_actions_runtime` expects `data/defs/prayers.bin`, which
    is not present in this checkout's `data/defs/`.

## 2026-05-06 — UI Phase 2 closeout and Phase 3 input bridge

- Change made: finished the practical decoded-UI Phase 2 renderer/input
  closeout and started Phase 3 runtime dispatch.
- Why it was made:
  - The decoded renderer could draw side-tab groups, but it still needed
    decoded sprite borders, type-6 item widget fallback, opened side-interface
    state, and component hit-testing before it could become a useful runtime
    surface.
  - Phase 3 needs UI actions to be represented as typed events instead of
    screen-coordinate-only clicks so inventory, equipment, spell, and component
    actions can eventually route into core systems.
- Exact surfaces changed:
  - `rc-viewer/ui_interfaces.c` now draws decoded sprite border outlines,
    attempts local item PNG rendering for type-6 item model widgets, and exposes
    decoded component hit testing with component id, rect, click mask, actions,
    target verb, name, and text.
  - `rc-viewer/ui.h` and `rc-viewer/ui.c` now track selected item/spell target
    state, inventory drag state, context-menu source metadata, and a local open
    side-interface table.
  - Side-tab switching updates the open side-content group, and decoded side
    rendering resolves the active group from that table.
  - Left-click and right-click can now target decoded side-content components.
    Right-click builds context entries from decoded target verbs/actions/click
    masks; left-click emits a generic component-action intent when valid.
  - Inventory drag/reorder, item selection, spell selection, item-on-item,
    spell-on-item, item-on-component, spell-on-component, inventory action, and
    equipment action are represented as typed UI intents.
  - `rc-viewer/viewer.c` wires inventory drag, inventory drop/examine,
    equipment remove, and selected-spell selection to current local systems, and
    logs unresolved item/spell/component integration hooks.
  - `ui_cleanup.md` now records Phase 2 as complete enough to proceed into
    Phase 3, while keeping the remaining asset-generation backlog explicit:
    b237 item icon generation, cache bitmap fonts, real UI model widgets, and
    true scene minimap generation.
- Verification:
  - `cmake --build build -j2 --target rc-viewer` passed.
  - `timeout 8 env RC_VIEWER_EXIT_FRAMES=2 RUNEC_UI_DECODED=1 RUNEC_UI_START_TAB=3 RC_VIEWER_SCREENSHOT=/tmp/runec-ui-phase3-inventory.png ./build/rc-viewer`
    exited cleanly.
  - `timeout 8 env RC_VIEWER_EXIT_FRAMES=2 RUNEC_UI_DECODED=1 RUNEC_UI_START_TAB=Spellbook RC_VIEWER_SCREENSHOT=/tmp/runec-ui-phase3-spellbook.png ./build/rc-viewer`
    exited cleanly.
  - `timeout 8 env RC_VIEWER_EXIT_FRAMES=2 RUNEC_UI_DECODED=1 RUNEC_UI_START_TAB=Settings RC_VIEWER_SCREENSHOT=/tmp/runec-ui-phase3-settings.png ./build/rc-viewer`
    exited cleanly.

## 2026-05-05 — UI stack item and skills tab correction

- Change made: corrected quantity-dependent item icon selection and replaced
  hardcoded skills-tab demo values with live player skill state.
- Why it was made:
  - RuneLite selects item display definitions from `countObj/countCo` before
    drawing inventory sprites. Our UI cache was keyed only by base item id, so
    stacks such as coins and arrows could render with the wrong icon.
  - The stats tab was still drawing fixed placeholder levels, so it could not
    match OSRS/RuneLite/RSMod behavior once the core player state changed.
- Exact surfaces changed:
  - `tools/cache_pipeline/export_reference_item_icons.py` now reads local
    `osrsreboxed-db/data/items/items-stacked.json` and writes
    `data/sprites/items/item_stack_variants.tsv`.
  - `rc-viewer/viewer.c` loads that TSV and resolves each inventory/equipment
    stack to a display item id before loading or caching its icon. The 10M
    demo coin stack now loads `data/sprites/items/item_1004.png`; the 10K arrow
    stack loads its stacked arrow display icon.
  - `rc-viewer/ui.h` and `rc-viewer/ui.c` store `icon_item_id` per slot and
    cache icons by display id, avoiding base-item cache collisions.
  - `rc-viewer/viewer.c` maps the core `RcSkill` values into the OSRS stats
    display order and updates current/base/total level fields every frame.
    Sailing remains a placeholder level until the C core adds that skill.
  - `tools/cache_pipeline/export_sprites_modern.py` now exports the b237 stats
    total-bar sprites `189`, `190`, and `191` used by the decoded stats group.
- Verification:
  - `python3 tools/cache_pipeline/export_reference_item_icons.py --all` copied
    `30495` item icons and wrote `1027` stack icon variants.
  - `python3 tools/cache_pipeline/export_sprites_modern.py --cache tools/cache_pipeline/source/current_fightcaves_demo/data/cache --output data/sprites/ui`
    exported `495` sprite groups with `0` failures.
  - `python3 -m py_compile tools/cache_pipeline/export_reference_item_icons.py tools/cache_pipeline/export_sprites_modern.py`
    passed.
  - `cmake --build build -j2 --target rc-viewer` passed.
  - `timeout 8 env RC_VIEWER_EXIT_FRAMES=2 RUNEC_UI_DECODED=1 RUNEC_UI_START_TAB=3 RC_VIEWER_SCREENSHOT=/tmp/runec-ui-items-stack.png ./build/rc-viewer`
    exited cleanly and showed display-id item icons, including `item_1004.png`.
  - `timeout 8 env RC_VIEWER_EXIT_FRAMES=2 RUNEC_UI_DECODED=1 RUNEC_UI_START_TAB=1 RC_VIEWER_SCREENSHOT=/tmp/runec-ui-skills.png ./build/rc-viewer`
    exited cleanly and loaded the decoded stats total-bar sprites.

## 2026-05-05 — UI reference-backed icon/minimap correction

- Change made: corrected the UI regression path by following the local
  RuneLite/RSMod/reference material instead of continuing the guessed runtime
  fallbacks.
- Why it was made:
  - RuneLite's item icon path uses item 2D metadata and renders fixed inventory
    sprites; our temporary 3D model snapshots produced wrong inventory icons.
  - RuneLite/RSMod minimap handling is scene/gameframe driven; the downloaded
    OpenRS2 world-map PNG was not an in-game minimap source and looked wrong.
  - Cache interface text includes client markup and compact font metrics; the
    current TTF fallback needed cleanup until the cache bitmap font renderer is
    implemented.
- Exact surfaces changed:
  - Added `tools/cache_pipeline/export_reference_item_icons.py` to copy
    known-good local reference item PNGs from
    `osrsreboxed-db/docs/items-icons` into `data/sprites/items/` for immediate
    visual validation. It supports a focused default set and `--all` for the
    full local item icon corpus.
  - `rc-viewer/viewer.c` now prefers local `data/sprites/items/item_<id>.png`
    textures for inventory/equipment UI icons and uses coin visual buckets for
    item `995`.
  - The viewer refreshes the local icon cache after inventory/equipment sync
    each frame, so items that appear after startup can load their PNGs without
    restarting the viewer.
  - The old 3D model snapshot UI icon path is disabled by default and only runs
    when `RUNEC_UI_MODEL_ITEM_ICONS=1` is explicitly set.
  - `rc-viewer/viewer.c` no longer loads the downloaded b236 world-map PNG for
    the minimap by default. That path is debug-only behind
    `RUNEC_MINIMAP_WORLD_MAP=1`; normal rendering uses the local scene/terrain/
    collision minimap with live actor dots.
  - `rc-viewer/ui_interfaces.c` strips client markup such as `<col=...>` before
    drawing decoded text, uses local OSRS font selection, and draws with zero
    extra letter spacing.
  - `rc-viewer/ui.c` and `rc-viewer/ui_assets.c` point-sample UI font drawing
    for sharper small text. Runtime font loading now uses only local
    `data/fonts/` files, not reference-checkout fallback paths.
- Current caveat:
  - `data/sprites/items/` is a transitional local validation source. The final
    fix remains a repo-local ItemSpriteFactory-equivalent exporter from the
    b237 cache plus a cache bitmap font renderer and true scene minimap
    generator.
- Verification:
  - `python3 tools/cache_pipeline/export_reference_item_icons.py --all` copied
    `30495` item icons and coin visual buckets into `data/sprites/items/`
    (`30505` local PNGs total, `121M` under ignored `data/`).
  - `python3 -m py_compile tools/cache_pipeline/export_reference_item_icons.py`
    passed.
  - `cc -fsyntax-only -std=c11 -Ilib/raylib/include -I. rc-viewer/viewer.c`
    passed.
  - `cc -fsyntax-only -std=c11 -Ilib/raylib/include -I. rc-viewer/ui.c`
    passed.
  - `cc -fsyntax-only -std=c11 -Ilib/raylib/include -I. rc-viewer/ui_interfaces.c`
    passed.
  - `cc -fsyntax-only -std=c11 -Ilib/raylib/include -I. rc-viewer/ui_assets.c`
    passed.
  - `cmake --build build -j2 --target rc-viewer` passed.
  - `timeout 8 env RC_VIEWER_EXIT_FRAMES=2 RUNEC_UI_DECODED=1 RUNEC_UI_START_TAB=3 RC_VIEWER_SCREENSHOT=/tmp/runec-ui-regression-check.png ./build/rc-viewer`
    exited cleanly, loaded local item PNGs, did not load the world-map PNG, and
    wrote the validation screenshot.

## 2026-05-05 — UI decoded-mode regression fixes

- Change made: fixed the decoded UI validation regressions reported in
  spellbook, equipment tab, chatbox, and UI text rendering.
- Why it was made: decoded mode was mixing cache-static interface rendering
  with runtime-populated gameframe areas too aggressively. That caused some
  root-level equipment sprites to stretch across the whole side panel, showed
  only the old 25-spell manual overlay, and replaced the known-good chatbox
  chrome with an incomplete decoded chat runtime.
- Exact surfaces changed:
  - Fixed `rc-viewer/ui_interfaces.c` so only the true `file_id == 0`
    universe component uses the full mount rectangle. Other root-level
    components now lay out relative to the mount instead of stretching to fill
    it.
  - Extended `tools/cache_pipeline/export_sprites_modern.py` to export
    `standard_spell_on_0..79` and `standard_spell_off_0..79` from the b237
    Joshua-F `graphic.sym` aliases.
  - Increased `RUNEC_UI_ASSET_MAX` and registered
    `standard_spell_on_0..79` as required UI assets.
  - Updated the spellbook overlay to draw `80` standard spell slots using the
    b237 standard spell sprites instead of the previous 25-icon subset.
  - Put decoded chatbox rendering behind `RUNEC_UI_DECODED_CHAT=1`; decoded UI
    mode now keeps the current fixed chatbox/gameframe chrome by default until
    the full chat runtime is ready.
  - Added local OSRS font loading from `data/fonts/runescape.ttf` and copied
    RuneLite's OSRS font files into `data/fonts/` for local validation.
- Verification:
  - `python3 -m py_compile tools/cache_pipeline/export_sprites_modern.py`
    passed.
  - `python3 tools/cache_pipeline/export_sprites_modern.py --cache tools/cache_pipeline/source/current_fightcaves_demo/data/cache --output data/sprites/ui`
    exported `492` sprite groups with `0` failures.
  - `python3 tools/cache_pipeline/validate_ui_assets.py --assets-dir data/sprites/ui --require-transparent side_icon_inventory --require-transparent orb_frame_0 --require-transparent magicon_0 --require-transparent prayeron_0 --require-transparent fixed_map_mask --require-transparent fixed_compass_mask --require-transparent standard_spell_on_0`
    validated `275/275` required UI sprites and transparent pixels for `7/7`
    selected sprites.
  - `cc -fsyntax-only -std=c11 -Ilib/raylib/include -I. rc-viewer/ui_assets.c`
    passed.
  - `cc -fsyntax-only -std=c11 -Ilib/raylib/include -I. rc-viewer/ui.c`
    passed.
  - `cc -fsyntax-only -std=c11 -Ilib/raylib/include -I. rc-viewer/ui_interfaces.c`
    passed.
  - `cmake --build build -j2 --target rc-viewer` passed.
  - Captured decoded-mode screenshots:
    `/tmp/runec-ui-spellbook-fix.png` and
    `/tmp/runec-ui-equipment-fix.png`.

## 2026-05-05 — UI Phase 2 renderer hardening

- Change made: completed the remaining Phase 2 hardening pass for the opt-in
  decoded UI renderer.
- Why it was made: the first decoded renderer could draw simple side-tab
  groups, but it still needed clipping, scroll extents, top-level mount rects,
  dynamic tab overlays, and screenshot hooks before it was useful for serious
  visual validation.
- Exact surfaces changed:
  - Extended `tools/cache_pipeline/rc_cache/interfaces.py` and
    `tools/cache_pipeline/export_ui_interfaces.py` to preserve sprite flip
    flags and no-click-through state in debug and binary exports.
  - Extended `rc-viewer/ui_interfaces.c` and `rc-viewer/ui_interfaces.h` with
    clipped layer rendering, decoded scroll extents, sprite flip/shadow
    handling, placeholder type 6 model-widget markers, and a helper for looking
    up decoded component rectangles by group/component name.
  - Updated `rc-viewer/ui.c` so decoded top-level component rects can drive the
    chatbox, minimap/map frame, side menu, and side container mounts when
    `RUNEC_UI_DECODED=1` is enabled.
  - Rendered the decoded chatbox group behind live chat text in decoded mode.
  - Kept live runtime overlays for inventory, equipment, stats, combat,
    prayer, and magic on top of decoded cache layouts, because those tabs are
    populated by runtime state/scripts rather than cache-static components
    alone.
  - Added `RUNEC_UI_START_TAB` for starting decoded validation on a specific
    side tab by name or index.
  - Added `RC_VIEWER_SCREENSHOT=/path/file.png` for one-frame screenshot
    captures during smoke validation.
- Verification:
  - `python3 tools/cache_pipeline/export_ui_interfaces.py` passed and rewrote
    `data/ui/interfaces.bin`.
  - `python3 -m py_compile tools/cache_pipeline/export_ui_interfaces.py tools/cache_pipeline/rc_cache/interfaces.py tools/cache_pipeline/validate_ui_assets.py`
    passed.
  - `python3 tools/cache_pipeline/validate_ui_assets.py --assets-dir data/sprites/ui --require-transparent side_icon_inventory --require-transparent orb_frame_0 --require-transparent magicon_0 --require-transparent prayeron_0 --require-transparent fixed_map_mask --require-transparent fixed_compass_mask`
    validated `195/195` required UI sprites and transparent pixels for `6/6`
    selected cutout sprites.
  - `cc -fsyntax-only -std=c11 -Ilib/raylib/include -I. rc-viewer/ui_interfaces.c`
    passed.
  - `cc -fsyntax-only -std=c11 -Ilib/raylib/include -I. rc-viewer/ui.c`
    passed.
  - `cc -fsyntax-only -std=c11 -Ilib/raylib/include -I. rc-viewer/viewer.c`
    passed.
  - `cmake --build build -j2 --target rc-viewer` passed.
  - `timeout 8 env RC_VIEWER_EXIT_FRAMES=1 ./build/rc-viewer` exited cleanly
    in the default/manual UI path.
  - `timeout 8 env RC_VIEWER_EXIT_FRAMES=1 RUNEC_UI_DECODED=1 ./build/rc-viewer`
    exited cleanly in the decoded UI path.
  - `timeout 8 env RC_VIEWER_EXIT_FRAMES=1 RUNEC_UI_DECODED=1 RUNEC_UI_START_TAB=5 RC_VIEWER_SCREENSHOT=/tmp/runec-ui-prayer.png ./build/rc-viewer`
    exited cleanly and wrote a valid `1280x720` PNG.
- Known gaps:
  - Decoded UI is still opt-in.
  - Type 6 model widgets are placeholders. The current core export only has a
    cache-static type 6 widget in `questjournal`; inventory items, worn
    equipment, spell/prayer state, and stat values need runtime component
    overrides before decoded UI can own them directly.
  - Runtime component overrides and the explicit open-interface table remain
    the next step before decoded UI can replace the manual fallback.

## 2026-05-05 — UI Phase 2 decoded renderer foundation

- Change made: added the first opt-in decoded interface rendering path for
  cache-backed UI components.
- Why it was made: Phase 1 gave us decoded b237 interface data, but the viewer
  still needed a C-side loader and renderer before we can replace the manual
  side-tab/gameframe drawing with actual cache-backed component trees.
- Exact surfaces changed:
  - Extended `tools/cache_pipeline/export_ui_interfaces.py` to write
    `data/ui/interfaces.bin`, a compact C-readable binary for the core UI and
    gameframe interface groups.
  - Extended `tools/cache_pipeline/rc_cache/interfaces.py` debug/export fields
    with render data needed by the C runtime, including scroll dimensions, text
    layout flags, rectangle fill state, line state, and text color.
  - Added `rc-viewer/ui_interfaces.c` and `rc-viewer/ui_interfaces.h` with a
    repo-local interface binary loader, lazy sprite texture cache, layout
    evaluator, and renderer for layer, rectangle, text, sprite, tiled sprite,
    and line components.
  - Updated `rc-viewer/ui.c` and `rc-viewer/ui.h` so the viewer can opt into
    decoded side-tab rendering with `RUNEC_UI_DECODED=1`, while preserving the
    existing manual UI as the default fallback.
  - Kept inventory/equipment dynamic item overlays active on top of decoded
    tab backgrounds so current gameplay smoke testing remains usable.
- Local generated outputs:
  - `data/ui/interfaces.bin`
  - `data/ui/interface_manifest.json`
  - `data/ui/interface_debug.txt`
  - These are generated local artifacts under ignored `data/`, not source
    files.
- Verification:
  - `python3 tools/cache_pipeline/export_ui_interfaces.py` decoded `26251`
    interface components: `24155` IF3 and `2096` IF1, and wrote the JSON,
    debug text, and binary outputs.
  - `python3 -m py_compile tools/cache_pipeline/export_ui_interfaces.py tools/cache_pipeline/rc_cache/interfaces.py tools/cache_pipeline/validate_ui_assets.py`
    passed.
  - `python3 tools/cache_pipeline/validate_ui_assets.py --assets-dir data/sprites/ui --require-transparent side_icon_inventory --require-transparent orb_frame_0 --require-transparent magicon_0 --require-transparent prayeron_0 --require-transparent fixed_map_mask --require-transparent fixed_compass_mask`
    validated `195/195` required UI sprites and transparent pixels for `6/6`
    selected cutout sprites.
  - `cc -fsyntax-only -std=c11 -Ilib/raylib/include -I. rc-viewer/ui.c`
    passed.
  - `cc -fsyntax-only -std=c11 -Ilib/raylib/include -I. rc-viewer/ui_interfaces.c`
    passed.
  - `cmake --build build -j2` passed.
  - `timeout 8 env RC_VIEWER_EXIT_FRAMES=1 ./build/rc-viewer` exited cleanly
    in the default/manual UI path.
  - `timeout 8 env RC_VIEWER_EXIT_FRAMES=1 RUNEC_UI_DECODED=1 ./build/rc-viewer`
    exited cleanly in the decoded UI path.
- Known gaps:
  - Decoded UI is not the default yet.
  - This pass renders isolated decoded side-tab groups, not the full opened
    top-level gameframe/subinterface table.
  - Parent clipping, scroll areas, sprite flip/border/shadow edge behavior,
    model widgets, item-grid widgets, and screenshot parity validation remain
    Phase 2 follow-up work.

## 2026-05-05 — UI Phase 1 interface decode foundation

- Change made: completed the first repo-local UI interface decode foundation
  for b237 cache data.
- Why it was made: the viewer can now load cache-backed UI sprites, but the
  current runtime still draws most UI surfaces manually. Phase 1 creates the
  data foundation needed to replace those primitives with decoded OSRS
  interface components in later phases.
- Exact surfaces changed:
  - Added `tools/cache_pipeline/rc_cache/interfaces.py` with IF3 and IF1
    component decoding, b237 model-id handling, parent-id sentinel handling,
    listener payload preservation, and var/inv/stat trigger preservation.
  - Exported the interface decoder through `tools/cache_pipeline/rc_cache/__init__.py`.
  - Added `tools/cache_pipeline/export_ui_interfaces.py`, which resolves core
    interface ids from the Joshua-F dump symbols, decodes the b237 cache
    interface index, validates core component counts against the dump files,
    and writes local debug artifacts under `data/ui/`.
  - Extended `tools/cache_pipeline/validate_ui_assets.py` with optional
    transparent-pixel checks for selected UI sprites.
- Local generated outputs:
  - `data/ui/interface_manifest.json`
  - `data/ui/interface_debug.txt`
  - These are generated local artifacts under ignored `data/`, not source files.
- Verification:
  - `python3 -m py_compile tools/cache_pipeline/rc_cache/interfaces.py tools/cache_pipeline/export_ui_interfaces.py tools/cache_pipeline/validate_ui_assets.py`
    passed.
  - `python3 tools/cache_pipeline/export_ui_interfaces.py`
    decoded `26251` interface components: `24155` IF3 and `2096` IF1, with
    `0` errors and `0` warnings.
  - The export matched all `24` core UI/gameframe groups against Joshua-F dump
    component counts.
  - `python3 tools/cache_pipeline/validate_ui_assets.py --assets-dir data/sprites/ui --require-transparent side_icon_inventory --require-transparent orb_frame_0 --require-transparent magicon_0 --require-transparent prayeron_0 --require-transparent fixed_map_mask --require-transparent fixed_compass_mask`
    validated `195/195` required UI sprites and transparent pixels for `6/6`
    selected cutout sprites.
- Notes:
  - Three non-core model components have two trailing zero bytes after decode;
    these are recorded in the generated manifest for later investigation, but
    they do not affect the core gameframe/UI groups.
  - No viewer rendering behavior was changed in this phase.

## 2026-05-05 — Focused b237 UI asset runtime pass

- Change made: regenerated the viewer's cache-backed OSRS gameframe sprite
  assets from the repo-local OpenRS2 b237 cache and added a validator for the
  required UI PNG set.
- Why it was made: manual review showed the viewer was missing the visible UI
  layer: minimap chrome, tab icons, orb art, skill/prayer/spell icons, and
  related gameframe sprites. The code already had an OSRS-style UI shell, but
  `data/sprites/ui/` was absent in the checkout, so the renderer fell back to
  placeholder UI for many surfaces.
- Exact surfaces changed:
  - Regenerated `data/sprites/ui/` using
    `tools/cache_pipeline/export_sprites_modern.py` against
    `tools/cache_pipeline/source/current_fightcaves_demo/data/cache`.
  - Updated `rc-viewer/ui_assets.c` and `rc-viewer/ui_assets.h` so cache
    gameframe sprites are tracked as required while provisional item icon PNGs
    under `data/sprites/items/` remain optional fallbacks.
  - Added `tools/cache_pipeline/validate_ui_assets.py`, which reads the
    required `UI_ASSET(...)` entries from `rc-viewer/ui_assets.c` and verifies
    the corresponding PNG files exist and have valid PNG headers.
- Reference update:
  - Pulled latest `runelite`, `void_rsps`, `pufferlib_4`, and confirmed
    `rsmod`, `2011Scape-game`, `data_osrs`, and `osrsreboxed-db` were already
    current.
  - `model_dump/osrs-dumps` could not fast-forward because upstream force
    pushed `master`, so the checkout was moved to detached `origin/master` at
    `063a563d0` (`2026-04-29-rev237`) without rewriting the local branch.
- Verification:
  - `python3 tools/cache_pipeline/export_sprites_modern.py --cache tools/cache_pipeline/source/current_fightcaves_demo/data/cache --output data/sprites/ui`
    exported `282` sprite groups with `0` failures.
  - `python3 tools/cache_pipeline/validate_ui_assets.py --assets-dir data/sprites/ui`
    validated `195/195` required UI sprites.
  - `python3 -m py_compile tools/cache_pipeline/export_sprites_modern.py tools/cache_pipeline/validate_ui_assets.py`
    passed.
  - `cc -fsyntax-only -std=c11 -Ilib/raylib/include -I. rc-viewer/ui_assets.c`
    passed.
  - `cmake --build build -j2` passed.
  - `timeout 8 env RC_VIEWER_EXIT_FRAMES=1 ./build/rc-viewer` loaded the
    viewer, exercised the UI sprite loader, rendered one frame, and exited
    cleanly.

## 2026-05-03 — Combat projectile/manual validation stop point

- Change made: updated the active work docs to capture the current manual
  viewer validation failures and make the next work session a combat and
  interaction fidelity pass instead of moving directly to banking.
- Why it was made: manual testing showed that the current projectile and
  magic/ranged presentation is still not OSRS-faithful. The latest code
  has structural projectile plumbing, but the runtime behavior is not yet
  acceptable for general combat use.
- Validation findings to carry forward:
  - Ranged and magic projectiles are not facing the correct direction.
  - Projectile travel timing is wrong.
  - Projectile behavior is still too fixed/test-like. Weapon/ammo/spell
    or NPC attack data must choose visuals and timing.
  - Staff-equipped combat currently behaves like a spell attack path when
    OSRS default staff attacks should melee unless a spell, manual cast,
    or autocast state is selected.
  - Spellbook spell selection/autocast is not yet wired as core combat
    state. Selecting Air Wave, for example, should drive Air Wave rune
    requirements, animation, projectile, hit delay, and UI feedback.
  - The right-click interaction menu/toolbox presentation is not
    OSRS-faithful.
  - Right-click ground/camera panning currently conflicts with opening
    the interaction menu.
- Exact surfaces changed:
  - Updated `work.md` so the current stop point explicitly lists combat
    fidelity and interaction feel as the next priorities.
  - Updated `work_highlevel.md` so the high-level next sequence starts
    with combat projectile/spell/autocast/staff-default behavior and
    OSRS-style menu/camera feel before banking/storage.
- Upstream/downstream impacts:
  - Do not treat combat Phases 0 through 11 as visual/gameplay parity
    complete. They are structurally complete for the current runtime
    slice, but frontend fidelity and selected-spell behavior still need
    refinement.
  - Tomorrow's work should audit RSMod, RuneLite, VoidPS, 2011Scape, and
    OSRS cache data specifically for projectile spawn/travel math,
    spotanim/projectile selection, autocast/manual spell state, default
    staff attack semantics, and menu/camera input behavior.
  - Banking/storage should wait until this combat/interactions
    refinement pass is validated or explicitly paused.
- Verification:
  - Documentation-only stop-point update. No new code validation was run
    for this entry.

## 2026-05-03 — Projectile travel timing, visibility, and orientation fix

- Change made: corrected combat projectile lifecycle and viewer
  orientation/scale for visible ranged and magic projectile travel.
- Why it was made: manual viewer testing showed that ranged projectiles
  were not visible, magic projectiles started/traveled incorrectly,
  remained visible after landing, and faced the wrong direction. The
  prior path emitted projectile state, but aged projectiles on the same
  tick they were spawned and used too-short visual durations for
  player-visible flight.
- Exact surfaces changed:
  - Updated `data/defs/combat_visuals.tsv` so Rune arrow visual
    `client_delay` is `3` ticks and Fire Blast visual `client_delay` is
    `4` ticks while preserving the existing server hit delays. This
    keeps visible flight timing data-backed instead of hardcoded in the
    renderer.
  - Updated `rc-core/combat.c` so projectile age does not advance on
    the spawn tick and projectile events are removed promptly after
    their configured duration instead of lingering for extra ticks.
  - Updated `rc-viewer/viewer.c` so projectile model orientation uses
    the correct travel vector sign and model scale is chosen by combat
    style through a small viewer-only presentation helper.
  - Updated `tests/test_combat_visuals_projectiles.c` to assert the
    corrected ranged/magic visual durations and projectile lifecycle.
- Upstream/downstream impacts:
  - Core still owns projectile events and data-backed visual timing.
    Viewer changes are limited to presentation: model lookup, scale,
    and facing.
  - Different weapons/ammo/spells remain supported by adding or
    generating new visual records rather than branching in viewer code.
  - Spellbook spell selection remains a separate future UI/core
    integration task; this change only fixes projectile visuals for the
    currently selected spell path.
- Known gaps:
  - Launch and impact spotanims are still recorded but not yet rendered
    as separate start/end graphics.
  - Projectile mesh sequence animation is still not applied to the
    projectile model; the animation token remains available in
    `RcCombatProjectile`.
- Verification:
  - `python3 -m py_compile tools/cache_pipeline/export_projectile_models.py`
    passed.
  - `cmake --build build -j2 --target rc-viewer test_combat_visuals_projectiles`
    passed.
  - `ctest --test-dir build -R 'test_combat_visuals_projectiles|test_combat_phase8_view_state' --output-on-failure`
    passed 2/2.
  - `ctest --test-dir build --output-on-failure` passed 61/61.
  - `timeout 5 env RC_VIEWER_EXIT_FRAMES=1 ./build/rc-viewer` exited
    successfully.
  - `bash testing/run_sps_benchmark.sh` reported `3,048,920` SPS.
  - Coverage note: the current build does not emit `.gcno` coverage
    artifacts. Projectile event creation and lifecycle are covered by
    unit tests; final visual confirmation remains a manual viewer check.

## 2026-05-03 — Data-backed projectile visuals for ranged and magic combat

- Change made: replaced the temporary projectile visual mapping with
  OSRS symbol-backed spotanim/model IDs and added a dedicated viewer
  projectile model bundle.
- Why it was made: ranged and magic attack animations were playing, but
  no projectile was visible in the viewer because the combat visual
  records pointed at incorrect/temporary graphics and the viewer only
  looked for projectile meshes inside the item model bundle. OSRS/RSPS
  references show that these visuals are spotanim/graphic assets, not
  inventory item models.
- Reference findings:
  - Updated the local RuneLite reference repo and confirmed current
    `SpotanimID` constants: Rune arrow travel `15`, Rune arrow launch
    `24`, Fire Blast cast `129`, travel `130`, and impact `131`.
  - RSMod's ranged combat path uses weapon/ammo params for
    `proj_travel`, `proj_launch`, and `proj_type`, then spawns a
    projectile and uses its server/client delays.
  - RSMod's standard spell content maps Fire Blast through
    `fireblast_casting`, `fireblast_travel`, and `fireblast_impact`.
  - The local OSRS dump maps those spotanims to model IDs:
    Rune arrow travel `model_3136`, Rune arrow launch `model_3142`,
    Fire Blast cast `model_3083`, Fire Blast travel `model_3087`, and
    Fire Blast impact `model_3088`.
- Exact surfaces changed:
  - Updated `data/defs/combat_visuals.tsv` to use Rune arrow
    launch/travel `24/15` with model `3136`, and Fire Blast
    launch/travel/impact `129/130/131` with travel model `3087` and
    travel animation token `663`.
  - Added `tools/cache_pipeline/export_projectile_models.py`, a narrow
    dat2 cache-index-7 exporter that writes projectile meshes in the
    same MDL2 format consumed by the Raylib model loader.
  - Generated `data/models/projectiles.models` from the local
    `data/source/current_fightcaves_demo/data/cache` dat2 cache with
    models `3136`, `3142`, `3083`, `3087`, `3088`, `3080`, and `3135`.
  - Updated `rc-viewer/viewer.c` with a dedicated
    `RUNEC_PROJECTILE_MODELS` model set and projectile lookup order:
    projectile models first, then item models, then NPC models.
  - Updated `tests/test_combat_visuals_projectiles.c` so combat
    projectile tests assert the corrected OSRS-backed spotanim/model
    IDs.
- Upstream/downstream impacts:
  - Combat state remains the source of projectile events; viewer only
    renders model IDs emitted by core state and does not calculate
    combat behavior.
  - Future ranged weapons, ammunition, spells, and NPC attacks should
    extend data records and projectile model exports, not add
    viewer-specific branches.
  - `data/models/projectiles.models` belongs to the separate RuneC-DB
    data repo, not the main RuneC commit.
- Known gaps:
  - The viewer now renders the correct travel model IDs, but does not
    yet play projectile model sequence frames such as `blast_travel`
    over the projectile mesh. The animation token is preserved in core
    projectile state for that follow-up.
  - Launch and impact spotanims are recorded in core projectile state,
    but viewer rendering currently focuses on the travel projectile.
    Impact/launch graphics should be rendered from the same spotanim
    data path when visual parity is deepened.
- Verification:
  - `python3 -m py_compile tools/cache_pipeline/export_projectile_models.py`
    passed.
  - `python3 tools/cache_pipeline/export_projectile_models.py --cache data/source/current_fightcaves_demo/data/cache --models 3136,3142,3083,3087,3088,3080,3135 --output data/models/projectiles.models`
    exported seven projectile models.
  - `cmake --build build -j2 --target rc-viewer test_combat_visuals_projectiles`
    passed.
  - `ctest --test-dir build -R 'test_combat_visuals_projectiles|test_combat_phase8_view_state' --output-on-failure`
    passed 2/2.
  - `ctest --test-dir build --output-on-failure` passed 61/61.
  - `timeout 5 env RC_VIEWER_EXIT_FRAMES=1 ./build/rc-viewer` exited
    successfully and loaded the viewer/model data.
  - `bash testing/run_sps_benchmark.sh` reported
    `3,039,847` SPS for the combat benchmark.
  - Coverage note: the current build does not emit `.gcno` coverage
    artifacts, so gcov line coverage was unavailable. Projectile event
    generation is covered by unit tests; live visual projectile
    rendering remains a manual viewer validation item.

## 2026-05-03 — Viewer Equipment Model Regression Repair

- Change made: repaired the character/equipment model regression caused by
  replacing the entire viewer item model bundle during the ranged/magic
  asset fix.
- Why it was made: the full regeneration of `data/models/items.models`
  changed the known-good base player/equipment model bundle, which
  visually inflated the character model and made equipped meshes look
  incorrect. The bow/staff/rune additions only needed incremental item
  support, not a replacement of the established model corpus.
- Exact surfaces changed:
  - Restored the known-good `data/models/items.models` binary from the
    locally present Git LFS object for the data repo.
  - Rebuilt the new ranged/magic item export into `/tmp` only, then
    merged just the missing records/models into the restored bundle.
  - Extended `data/models/item_render.map` with records for Magic
    shortbow `861`, Rune arrow `892`, Staff of air `1381`, and runes
    `554`, `555`, `556`, `557`, `558`, `560`, and `562`.
  - Appended only the render models referenced by those new records,
    leaving existing body, Torva, Bandos, godsword, whip, partyhat, and
    3rd age model data intact.
- Upstream/downstream impacts:
  - This fixes the viewer regression without changing core combat,
    inventory, equipment validation, item definitions, or formulas.
  - Future viewer item additions should merge new item render records into
    the known-good model bundle or regenerate from the exact canonical
    pipeline/source, not overwrite the whole model set from an incidental
    cache.
- Verification:
  - `cmake --build build -j2 --target rc-viewer test_items_bin test_prayer_spell_actions_runtime test_combat_phase8_view_state` passed.
  - `ctest --test-dir build -R 'test_items_bin|test_prayer_spell_actions_runtime|test_combat_phase8_view_state' --output-on-failure` passed 3/3.
  - Visual confirmation still needs manual viewer validation from the
    desktop shell because this tool session cannot open an X11 display.

## 2026-05-03 — Viewer Ranged/Magic Asset and Attack Animation Fix

- Change made: completed the viewer-side asset and animation support for
  the seeded ranged and magic combat test kit.
- Why it was made: the previous seeded Magic shortbow, Rune arrows,
  Staff of air, and rune stacks existed in inventory state, but the
  viewer did not yet have their item icons, equipped render models, or
  player attack-animation routing. That made manual ranged/magic combat
  validation misleading because the UI showed blank slots and the player
  appeared to attack without weapon-specific motion.
- Exact surfaces changed:
  - Updated `rc-viewer/viewer.c` so player attack animation selection now
    consumes core attack animation timers and maps the current combat
    class/weapon to viewer animation tokens for bow attacks, standard
    spell casts, staff attacks, whip attacks, godsword attacks, and the
    unarmed fallback.
  - Updated `rc-viewer/ui_assets.c` so the UI asset registry can load
    icons for Magic shortbow `861`, Rune arrow `892`, Staff of air
    `1381`, and runes `554`, `555`, `556`, `557`, `558`, `560`, and
    `562`.
  - Updated `tools/cache_pipeline/export_item_render_models.py` so those
    item IDs are part of the default viewer item render export set.
  - Updated `tools/cache_pipeline/export_models.py` so the legacy/cache
    model export allowlist also includes the same ranged/magic test-kit
    items.
  - Updated `tools/export_npc_anims.py` so it correctly scans `NDEF v4`
    records by skipping combat metadata, model IDs, and option text after
    each NPC animation block.
  - Updated NPC viewer animation selection to use the NPC definition's
    attack animation when present and fall back to the standard humanoid
    punch animation only while `npc->attack_anim_timer` is active.
  - Generated `data/sprites/items/item_861.png`,
    `data/sprites/items/item_892.png`, `data/sprites/items/item_1381.png`,
    and rune icons from `data/source/osrsreboxed-db/docs/items-complete.json`.
  - Regenerated `data/models/items.models` and
    `data/models/item_render.map` from the local cache so the Magic
    shortbow and Staff of air have equipped male render models and the
    runes/arrows have ground-model render records.
  - Merged animation sequence `422` into `data/anims/npcs.anims` so
    common humanoid NPCs with missing definition-level attack animations,
    such as guards with `attack_anim = -1`, still animate while attacking.
- Upstream/downstream impacts:
  - Core combat, formulas, inventory rules, equipment validation, spell
    logic, ammo logic, and rune-consumption logic are unchanged.
  - The attack-animation mapping is intentionally a viewer bridge from
    core combat state to available cache animation IDs. Deeper parity can
    later move exact per-weapon/per-style animation metadata into data or
    content hooks without changing combat math.
  - The regenerated `data/` files live in the separate local DB/data
    tree and should remain excluded from normal RuneC source commits.
- Known gaps:
  - NPC-specific attack-animation parity is still only as good as the NPC
    definition/export data. The new fallback fixes common humanoid NPCs
    for manual combat testing, but non-humanoid NPC families should still
    receive exact attack/death sequence metadata when we deepen NPC combat
    visuals.
- Verification:
  - `cmake --build build -j2 --target rc-viewer test_items_bin test_prayer_spell_actions_runtime test_combat_phase8_view_state` passed.
  - `ctest --test-dir build -R 'test_items_bin|test_prayer_spell_actions_runtime|test_combat_phase8_view_state' --output-on-failure` passed 3/3.
  - A local animation-cache probe confirmed `data/anims/npcs.anims`
    contains sequence `422` after the merge.
  - Direct viewer startup smoke could not run in this tool session because
    GLFW could not open display `:0`, and `xvfb-run` was unavailable in
    PATH. Manual viewer validation should be run from the normal desktop
    shell.

## 2026-05-03 — Viewer Seeded Ranged and Magic Test Kit

- Change made: expanded the viewer's seeded player inventory with a
  ranged and magic test kit.
- Why it was made: manual combat validation needs quick access to
  melee, ranged, and magic gear without waiting for banking, spawning,
  or QA-item tooling.
- Exact surfaces changed:
  - Updated `rc-viewer/viewer.c`.
  - Added Magic shortbow `861` and Rune arrows `892` to the seeded
    inventory for ranged-style testing.
  - Added Staff of air `1381` and rune stacks for Air `556`, Water
    `555`, Earth `557`, Fire `554`, Mind `558`, Chaos `562`, and Death
    `560` to the seeded inventory for spell testing.
  - Set the viewer config to load `data/defs/spells.bin`.
  - Selected `Fire Blast` by default when spell definitions are loaded
    so equipping a staff can enter the magic combat path immediately.
- Upstream/downstream impacts:
  - This is viewer demo-state seeding only. It does not change item
    definitions, combat formulas, equipment validation, rune
    consumption, spell definitions, or core inventory rules.
  - The added items use existing item IDs, so the existing item icon and
    sprite lookup path renders them the same way as the rest of the
    inventory.
- Verification:
  - `cmake --build build -j2 --target rc-viewer test_items_bin test_prayer_spell_actions_runtime` passed.
  - `ctest --test-dir build -R 'test_items_bin|test_prayer_spell_actions_runtime' --output-on-failure` passed 2/2.
  - Viewer startup smoke passed with `timeout 20 env RC_VIEWER_EXIT_FRAMES=2 ./build/rc-viewer`.
  - Headless combat SPS smoke passed with `bash testing/run_sps_benchmark.sh --mode combat --envs 64 --steps 20000 --warmup 1000`, reporting 1,280,000 env steps in 0.439815s, or 2,910,313 SPS.
  - Coverage is deferred for this entry because the changed path is viewer demo seeding/startup state rather than core simulation logic; the runtime path was covered by the viewer startup smoke and the item/spell definitions were covered by targeted data tests.

## 2026-05-03 — Headless SPS Benchmark Tool

- Change made: added a reusable in-process headless SPS benchmark under
  `testing/`.
- Why it was made: process-level test timing was useful as smoke, but it
  did not answer the RL-training throughput question because every run
  paid process startup and fixture setup costs. The new benchmark keeps
  worlds alive in process, ticks them in a tight loop, and reports
  aggregate environment steps per second.
- Exact surfaces changed:
  - Added `testing/sps_benchmark.c`.
  - Added `testing/run_sps_benchmark.sh`.
- Runtime behavior:
  - The benchmark supports `--mode combat` and `--mode idle`.
  - `combat` mode creates vectorized headless worlds, spawns a durable
    adjacent NPC target in each world, starts player-vs-NPC combat, warms
    up, then times repeated `rc_world_tick` calls.
  - `idle` mode measures base world tick overhead without combat.
  - The script builds `rc-core` and `rc-content`, compiles the benchmark
    into the active build directory, and runs it with forwarded args.
- Upstream/downstream impacts:
  - This gives us a repeatable local SPS smoke for future performance
    comparisons before a real PufferLib-style training harness exists.
  - The result is still a synthetic C-level core benchmark. It does not
    include Python binding overhead, policy inference, vector env IPC,
    observation encoding, reward calculation, or viewer rendering.
- Verification:
  - `bash -n testing/run_sps_benchmark.sh` passed.
  - `bash testing/run_sps_benchmark.sh --mode combat --envs 64 --steps 20000 --warmup 1000` built the benchmark and reported 1,280,000 env steps in 0.429776s, or 2,978,295 SPS.
  - `bash testing/run_sps_benchmark.sh --mode idle --envs 64 --steps 20000 --warmup 1000` built the benchmark and reported 1,280,000 env steps in 0.013742s, or 93,143,944 SPS.
  - Coverage is not applicable to this entry because the change adds a benchmark harness only; it does not alter runtime simulation behavior.

## 2026-05-03 — Combat Phase 11 Validation Gate

- Change made: completed Combat Phase 11 as a validation/performance
  gate for the current combat rewrite slice.
- Why it was made: Phases 0 through 10 rebuilt combat state, attack
  handoff, movement/range/facing, weapon styles, formulas, hit
  pipeline, retaliation, viewer bridge, content hooks, and first-pass
  resource/special handling. Phase 11 exists to prove the integrated
  lane is safe enough to move on to banking/storage, skilling, and later
  combat parity deepening without hiding unverified behavior.
- Exact surfaces changed:
  - Added `tests/test_combat_phase11_validation_gate.c`.
  - The new Phase 11 test covers projectile-line-of-sight blocked
    ranged and magic attacks. It verifies that blocked LOS prevents hit
    queueing, prevents cooldown start, preserves ammo/rune quantities,
    mirrors blocked LOS in combat state, leaves the actor out of range,
    and routes the player toward another valid attack tile.
  - Updated the combat planning notes to mark Phase 11 complete, record the
    validation-gate result, and document that the legacy player/NPC
    combat tick internals remain as explicitly marked compatibility
    wrappers pending final deletion.
  - Updated `work.md` and `work_highlevel.md` to close combat Phases 0
    through 11 for the current runtime slice and set banking/storage
    runtime UI integration as the next major runtime section.
- Gaps and deferred work:
  - Phase 11 validates the current combat rewrite shape; it does not
    implement exact OSRS parity for every weapon special, projectile,
    ammo recovery rule, spell-family effect, NPC attack script, or
    encounter mechanic.
  - The old combat tick internals are still present behind the public
    compatibility wrappers. They are clearly marked but not deleted
    because current public callers and tests still route through
    `rc_combat_tick_player` and `rc_combat_tick_npc`.
  - The benchmark remains a process-level smoke/throughput check. A
    true in-process RL SPS benchmark should replace it once a stable
    combat benchmark harness exists.
- Upstream/downstream impacts:
  - The next runtime sections can consume combat as a stable current
    slice rather than continuing broad combat-engine restructuring.
  - Future combat work should be targeted parity/deepening: exact
    specials, projectiles, spell effects, ammo recovery, NPC scripts,
    encounter-specific mechanics, and deletion of the legacy internals
    after replacement callers exist.
  - Ranged/magic combat tests and sims must continue to provide valid
    resources because the Phase 10 resource gates are now validated as
    part of the combat rewrite baseline.
- Verification:
  - `cmake --build build -j2` passed before the validation gate patch.
  - `ctest --test-dir build -R 'test_combat_phase11_validation_gate' --output-on-failure` passed, proving the new LOS/resource gate in isolation.
  - Targeted combat regression passed 15/15 tests across combat Phases 1-11, interaction handoff, prayer/spell actions, runtime flow, and regular NPC mechanics.
  - Full regression passed 60/60 tests in the normal build.
  - Coverage build passed targeted combat regression 15/15.
  - `gcov` coverage review reported `rc-core/combat.c` at 92.48% lines, 92.76% branches executed, and 63.69% branches taken at least once.
  - `gcov` coverage review reported `tests/test_combat_phase11_validation_gate.c` at 100.00% lines and 100.00% branches executed.
  - `gcov` coverage review reported `rc-content/combat/regular_npc_combat.c` at 92.62% lines, 92.68% branches executed, and 68.50% branches taken at least once.
  - Benchmark smoke ran 1000 `test_combat_phase11_validation_gate` process executions in 16.36s, or about 61.12 runs/sec; this remains a process-level smoke, not a final in-process combat SPS benchmark.
  - Viewer startup smoke passed with `timeout 20 env RC_VIEWER_EXIT_FRAMES=2 ./build/rc-viewer`, including asset loading, world/model loading, and clean shutdown.

## 2026-05-03 — Combat Phase 10 Special Resources

- Change made: completed first-pass Combat Phase 10 by making player
  special attack toggles, ranged ammunition, magic runes, and special
  energy recovery meaningful in the active player-vs-NPC swing path.
- Why it was made: Phase 9 established the `rc-core`/`rc-content`
  combat hook boundary, but the player attack cycle still allowed ranged
  and magic attacks without resources and treated special attack as UI
  state only. Phase 10 moves those player-facing combat resources into
  core runtime state while leaving weapon-specific special effects to
  content hooks.
- Exact surfaces changed:
  - Added special-attack content hook fields to
    `RcCombatContentHooks` in `rc-core/types.h`:
    `player_special_energy_cost` and
    `modify_player_special_damage`.
  - Added `special_recover_counter` to `RcPlayer` so special energy can
    regenerate deterministically over player-status ticks.
  - Added special energy constants to `rc-core/combat.h`:
    max `10000`, recovery amount `1000`, and recovery cadence `50`
    ticks.
  - Updated `rc-core/combat.c` so a pending player special asks content
    for the equipped weapon's cost, spends energy only when the swing
    commits, clears the pending flag, and lets content modify rolled
    damage for that special swing.
  - Updated `rc-core/combat.c` so ranged attacks require a resource
    before swinging and consume one committed resource after the roll:
    stackable equipped weapon quantities are consumed for thrown-style
    first-pass behavior, otherwise the ammo slot is consumed.
  - Updated `rc-core/combat.c` so magic attacks require a loaded
    selected combat spell with positive max hit and enough inventory
    runes; the spell runes are consumed when the swing is queued.
  - Updated `rc-core/combat.c` so player special energy recovers in
    `rc_combat_tick_player_status`.
  - Added `tests/test_combat_phase10_resources_specials.c` covering
    missing ammo blocking ranged attacks, ammo consumption, missing
    runes blocking magic attacks, rune consumption, special hook cost
    and damage application, pending special clearing, and special energy
    recovery.
  - Updated `tests/test_prayer_spell_actions_runtime.c` and
    `tests/test_interaction_engine_phase6.c` so legacy magic attack
    coverage uses valid combat-spell/rune setup instead of relying on
    invalid placeholder spell IDs.
  - Updated `tests/test_combat_phase4_weapon_styles_cycle.c` so the
    ranged cooldown regression equips ammo before expecting a ranged
    swing to commit under the new resource gate.
  - Updated `AGENT_README.md` so the repo wiki records OSRS-specific
    weapon special, spell, ammo, and prayer content hooks as
    `rc-content` ownership.
  - Updated combat planning, `work.md`, and `work_highlevel.md` to mark
    Phase 10 complete and set Phase 11 validation/performance as the
    next combat stop point.
- Gaps and deferred work:
  - Full OSRS weapon-special parity is not implemented yet. The generic
    hook boundary is present, but exact AGS/DDS/claws/godswords/powered
    staves and other weapon-specific effects need content handlers and
    data-backed item special metadata.
  - Ammo handling is first-pass validation and consumption. Exact ammo
    compatibility, projectile recovery/drop behavior, Ava's devices,
    bolt effects, chinchompas, thrown recovery, and poison/venom ammo
    effects remain content/data work.
  - Rune handling is first-pass inventory consumption. Elemental staff
    rune substitution, rune pouch, tome effects, autocast persistence,
    powered-staff charges, manual spell interruption semantics, and
    spell-family effects remain future spell/content work.
  - Prayer offensive modifiers and queued-hit protection snapshots were
    already in the formula/hit paths and remain covered there, but
    overhead icon state and richer prayer effects are still viewer and
    content parity work.
  - Combat hooks are still one table on `RcWorld`; future special,
    spell, ammo, prayer, and encounter modules need to compose through
    the central `rc_content_combat_register` path rather than
    independently replacing the hook table.
- Upstream/downstream impacts:
  - Combat tests and sims that perform player magic attacks now need a
    valid selected combat spell and required runes in inventory.
  - Ranged combat tests and sims now need either equipped ammo or a
    stackable equipped ranged weapon quantity.
  - Viewer special attack UI now has core runtime consequences once a
    content special hook is registered; without a hook, the pending
    special flag clears and the attack proceeds normally.
  - Future `rc-content` weapon-special modules should use the new hook
    fields rather than adding item-specific branches to `rc-core`.
- Verification:
  - Main build passed with `cmake --build build -j2`.
  - Focused Phase 10 test passed: `ctest --test-dir build -R
    'test_combat_phase10_resources_specials' --output-on-failure`.
  - Targeted combat/magic regression passed 14/14 tests with `ctest
    --test-dir build -R
    'test_combat_phase10_resources_specials|test_prayer_spell_actions_runtime|test_interaction_engine_phase6|test_combat_phase8_view_state|test_combat_phase7_retaliation_ai|test_combat_phase6_hit_pipeline|test_combat_phase5_formula_core|test_combat_phase4_weapon_styles_cycle|test_combat_phase3_movement_range_facing|test_combat_phase2_attack_handoff|test_combat_phase1_state|test_regular_npc_mechanics_combat|test_combat_runtime_flow|test_combat$'
    --output-on-failure`.
  - Full regression passed 59/59 tests with `ctest --test-dir build
    --output-on-failure` in 5.48 seconds.
  - Coverage build passed in `build-coverage-combat-phase10` with
    `-DCMAKE_C_FLAGS='--coverage -O0 -g'`, and the same targeted
    combat/magic coverage set completed 14/14 passing.
  - Coverage review with `gcov -b -c` reported `rc-core/combat.c` at
    92.27% line coverage, 92.52% branches executed, and 63.09%
    branches taken at least once. `tests/test_combat_phase10_resources_specials.c`
    reported 100.00% line coverage, 100.00% branches executed, and
    53.12% branches taken at least once. `rc-content/combat/regular_npc_combat.c`
    remained at 92.62% line coverage, 92.68% branches executed, and
    68.50% branches taken at least once.
  - Benchmark smoke: 1,000 process-level invocations of
    `build/test_combat_phase10_resources_specials` completed in 20.29
    seconds, roughly 49.30 process runs/sec. This is a process-level
    resource/special regression benchmark, not an in-process combat SPS
    benchmark, and includes test-process startup/logging overhead.
  - Viewer startup smoke passed: `timeout 20 env RC_VIEWER_EXIT_FRAMES=2
    ./build/rc-viewer` initialized raylib/assets, loaded viewer
    resources, exited cleanly, and returned code 0.

## 2026-05-03 — Combat Phase 9 Content Hook Migration

- Change made: completed Combat Phase 9 by adding a core combat content
  hook boundary and moving the existing OSRS-specific regular-NPC and
  activity-profile combat behavior out of `rc-core/combat.c` into
  `rc-content/combat/regular_npc_combat.c`.
- Why it was made: the combat rewrite had reached the point where core
  owned generic state, movement, formulas, hit queues, HP/XP/death, and
  viewer state, but `rc-core/combat.c` still contained content-specific
  branches for regular NPC mechanic tags, activity profiles, Barrows-like
  effects, Revenants, Lizardman Shaman-style specials, Jad-style range
  selection, Araxxor-like enrage/status behavior, and equipment/name
  checks. Phase 9 creates the engine/content boundary needed before
  deeper special attacks, spells, ammo, prayers, and encounter scripts
  are added.
- Exact surfaces changed:
  - Added `RcCombatContentHooks` to `rc-core/types.h` and stored one hook
    table on `RcWorld`.
  - Added `rc_combat_register_content_hooks` to `rc-core/combat.h` and
    implemented hook registration/clearing in `rc-core/combat.c`.
  - Updated `rc-core/combat.c` so generic combat calls hooks for outgoing
    player damage gates, NPC attack damage modifiers, landed NPC hit
    effects, NPC style selection, NPC attack range, rolled NPC damage
    modification, post-swing effects, NPC attack speed modification, and
    protection-pierce damage after prayer protection.
  - Removed direct `activity_mechanics` and `monster_mechanics`
    dependencies from `rc-core/combat.c`.
  - Moved the prior regular-NPC/activity-profile behavior into
    `rc-content/combat/regular_npc_combat.c`, including restricted-damage
    gates, slayer finisher prevention, dragonfire/icy-breath mitigation,
    slayer equipment mitigation, poison/venom/disease/status effects,
    heal-on-hit effects, Barrows-style drains/effects, Revenant freeze/
    teleblock/heal patterns, Lizardman Shaman jump/area/minion behavior,
    Jad-style melee-or-ranged/magic selection/range, activity area
    pressure, Dharok-style missing-HP damage scaling, and enrage attack
    speed adjustments.
  - Added `rc_content_combat_register` to `rc-content/content.h` and
    registered it from the aggregate `rc_content_register_all`.
  - Updated `tests/test_regular_npc_mechanics_combat.c` so it first
    proves core fallback behavior is identity when no content hook is
    registered, then registers `rc_content_combat_register` and verifies
    the migrated OSRS behavior remains intact.
  - Updated `AGENT_README.md` so the architecture wiki explicitly lists
    OSRS-specific regular-NPC combat hooks as `rc-content` ownership.
  - Updated combat planning, `work.md`, and `work_highlevel.md` to mark Phase
    9 complete and set Phase 10 special attacks/spells/ammo/prayer depth
    as the next combat stop point.
- Gaps and deferred work:
  - Phase 9 moved existing behavior behind hooks; it did not add new
    special attacks, ammo/rune handling, spell effects, or deeper prayer
    behavior. That remains Phase 10.
  - The hook surface is intentionally small and direct. Future encounter
    modules may need additional hook points for projectile selection,
    pre-swing cancellation, post-death routing, richer NPC attack scripts,
    or target-specific damage caps.
  - Some OSRS content checks still use item-name matching because the
    curated item/equipment effect tags are not complete enough yet to
    replace every equipment-name rule.
  - The migrated regular-NPC/content behavior preserves the existing
    first-pass parity covered by tests. Exact final parity for Barrows,
    Revenants, Lizardman Shamans, Jad, Araxxor-like profiles, and other
    activity-profile behaviors remains future content deepening.
- Upstream/downstream impacts:
  - `rc-core` now remains content-agnostic for current combat content
    behavior while still providing generic timing, movement, formulas,
    hit queues, HP/XP/death, and viewer state.
  - `rc-content` is now the place to add OSRS-specific combat behavior
    such as special attacks, spells, ammo, prayer effects, and future
    encounter scripts.
  - Isolated sims can now choose whether to register regular-NPC combat
    content hooks. Without hooks, core combat falls back to generic
    identity/default behavior.
  - Future tests that expect OSRS regular-NPC special behavior must
    register `rc_content_combat_register` or the aggregate content
    registration path.
- Verification:
  - Confirmed `rc-core/combat.c` no longer contains activity/monster/
    profile-specific branch references with a targeted `rg` search.
  - Focused build passed for `test_regular_npc_mechanics_combat`.
  - Focused behavior test passed: `ctest --test-dir build -R
    'test_regular_npc_mechanics_combat' --output-on-failure`.
  - Targeted combat/content regression passed 12/12 tests with `ctest
    --test-dir build -R
    'test_regular_npc_mechanics_combat|test_combat_phase8_view_state|test_combat_phase7_retaliation_ai|test_combat_phase6_hit_pipeline|test_combat_phase5_formula_core|test_combat_phase4_weapon_styles_cycle|test_combat_phase3_movement_range_facing|test_combat_phase2_attack_handoff|test_combat_phase1_state|test_combat_runtime_flow|test_combat'
    --output-on-failure`.
  - Full build passed with `cmake --build build -j2`.
  - First full regression attempt was invalid because it started while
    the full build was still relinking `test_combat_runtime_flow`, causing
    one transient `BAD_COMMAND`/permission-denied result. After the build
    completed, the clean full regression passed 58/58 tests with `ctest
    --test-dir build --output-on-failure` in 5.45 seconds.
  - Coverage build passed in `build-coverage-combat-phase9` with
    `-DCMAKE_C_FLAGS='--coverage -O0 -g'`, and the targeted combat/
    content coverage set completed 12/12 passing.
  - Coverage review with `gcov -b -c` reported `rc-core/combat.c` at
    92.24% line coverage, 91.77% branches executed, and 62.70% branches
    taken at least once. `rc-content/combat/regular_npc_combat.c`
    reported 92.62% line coverage, 92.68% branches executed, and 68.50%
    branches taken at least once. `tests/test_regular_npc_mechanics_combat.c`
    reported 100.00% line coverage, 100.00% branches executed, and 50.93%
    branches taken at least once.
  - Benchmark smoke: 1,000 process-level invocations of
    `build/test_regular_npc_mechanics_combat` completed in 11.15 seconds,
    roughly 89.69 process runs/sec. This is a process-level content-hook
    regression benchmark, not an in-process combat SPS benchmark.
  - Viewer startup smoke passed: `timeout 20 env RC_VIEWER_EXIT_FRAMES=2
    ./build/rc-viewer` initialized raylib/assets, loaded viewer resources,
    exited cleanly, and returned code 0.

## 2026-05-03 — Combat Phase 8 UI And Viewer Combat Bridge

- Change made: completed Combat Phase 8 by adding a core combat-view
  snapshot API and wiring the viewer combat tab, target feedback,
  hitsplats, HP bars, special attack state, auto-retaliate state, and
  combat-facing presentation to core combat state instead of viewer-local
  gameplay assumptions.
- Why it was made: Phase 7 gave core combat durable attacker,
  retaliation, single/multi-combat, aggression, and target ownership
  state, but the viewer still needed a formal read-only bridge for combat
  state and write-only widget intents for combat actions. This keeps
  combat calculations and rules in `rc-core` while letting the viewer
  become a usable test surface for attack flow, target HP, hitsplats,
  facing, and combat widget interaction.
- Exact surfaces changed:
  - Added `RcCombatHitView`, `RcCombatViewState`, and
    `rc_combat_get_player_view` to `rc-core/combat.h`.
  - Added combat view snapshot construction in `rc-core/combat.c`,
    including selected style, weapon category, attack type/class/stance,
    XP mask, auto-retaliate, special energy and pending state, attack
    range/speed/cooldown, current target reference, target HP, target
    recent hits, player HP, player attack animation timer, and player
    recent hits.
  - Updated `rc-viewer/viewer.c` so UI state synchronization calls
    `rc_combat_get_player_view`, mirrors selected style/auto-retaliate/
    special state from core, and routes combat style, auto-retaliate, and
    special attack clicks back into `rc_combat_set_player_style`,
    `rc_combat_toggle_auto_retaliate`, and `rc_combat_toggle_special`.
  - Updated `rc-viewer/viewer.c` orientation helpers so idle player and
    NPC rendering can consume core combat-facing state, while movement
    orientation remains viewer presentation logic.
  - Updated `rc-viewer/viewer.c` target feedback so the active combat
    target shows a highlighted NPC border, HP state even at full HP,
    hitsplats from `combat.recent_hits`, player hitsplats from player
    recent-hit state, and a top-center target HP/name overlay from the
    combat view snapshot.
  - Updated `rc-viewer/ui.c` so the special attack orb/bar uses core
    special attack energy rather than unrelated run-energy state.
  - Added `tests/test_combat_phase8_view_state.c` covering combat-view
    snapshot state, style selection, auto-retaliate toggling, special
    state, target HP/recent hits, and player HP/recent hits after queued
    hits resolve.
  - Updated the test build registration for the new Phase 8 focused
    combat-view-state test.
  - Updated combat planning, `work.md`, and `work_highlevel.md` to mark Phase
    8 complete and set Phase 9 content-hook migration as the next combat
    stop point.
- Gaps and deferred work:
  - Combat style button labels/icons still come from the current
    viewer-side OSRS reference table. A fully dynamic core-exposed
    style-button table per weapon category remains later UI parity work.
  - Special attack toggle state now flows between UI and core, but actual
    weapon special effects, energy spend, and recovery remain Phase 10.
  - Hitsplats and target overlay are functional state rendering, not
    exact OSRS sprite/chrome/sound parity.
  - Projectiles, overhead icons, exact attack/block animation token
    mapping, and richer visual event presentation remain future combat
    visual/content work.
  - Viewer startup smoke covered initialization and short-frame exit, not
    manual visual QA for exact combat presentation.
- Upstream/downstream impacts:
  - Phase 9 can move content-specific combat decisions out of `rc-core`
    without needing to invent a separate viewer bridge; viewer feedback
    now reads generic combat state.
  - Phase 10 can attach real special attack effects, ammo/rune
    consumption, prayers, projectiles, and overhead icons to the existing
    UI/core intent and snapshot path.
  - Future viewer/UI parity work should replace static combat-tab button
    presentation with a core style-table view instead of adding
    calculation logic to the viewer.
  - Core combat state is now a public read surface for the viewer, so
    future combat changes should keep `RcCombatViewState` synchronized
    when adding new visible combat concepts.
- Verification:
  - Configure/build passed with `cmake -S . -B build && cmake --build
    build --target test_combat_phase8_view_state rc-viewer -j2`.
  - Focused Phase 8 test passed: `ctest --test-dir build -R
    'test_combat_phase8_view_state' --output-on-failure`.
  - Targeted combat regression passed 11/11 tests with `ctest --test-dir
    build -R
    'test_combat_phase8_view_state|test_combat_phase7_retaliation_ai|test_combat_phase6_hit_pipeline|test_combat_phase5_formula_core|test_combat_phase4_weapon_styles_cycle|test_combat_phase3_movement_range_facing|test_combat_phase2_attack_handoff|test_combat_phase1_state|test_combat_runtime_flow|test_combat'
    --output-on-failure`.
  - Full build passed with `cmake --build build -j2`.
  - Full regression passed 58/58 tests with `ctest --test-dir build
    --output-on-failure` in 5.45 seconds.
  - Coverage build passed in `build-coverage-combat-phase8` with
    `-DCMAKE_C_FLAGS='--coverage -O0 -g'`, and the targeted combat
    coverage set completed 11/11 passing.
  - Coverage review with `gcov -b -c` reported `rc-core/combat.c` at
    69.88% line coverage, 64.57% branches executed, and 40.90% branches
    taken at least once for the selected Phase 8 coverage objects.
    `tests/test_combat_phase8_view_state.c` reported 100.00% line
    coverage, 100.00% branches executed, and 51.56% branches taken at
    least once.
  - Benchmark smoke: 1,000 process-level invocations of
    `build/test_combat_phase8_view_state` completed in 6.28 seconds,
    roughly 159.24 process runs/sec. This is a process-level UI bridge
    state smoke benchmark, not an in-process combat SPS benchmark.
  - Viewer startup smoke passed: `timeout 20 env RC_VIEWER_EXIT_FRAMES=2
    ./build/rc-viewer` initialized raylib/assets, loaded viewer resources,
    exited cleanly, and returned code 0.

## 2026-05-03 — Combat Phase 7 Retaliation, Single/Multi Combat, And NPC AI

- Change made: completed Combat Phase 7 by adding explicit attacker
  tracking, under-attack timers, first-pass single/multi-combat gates,
  NPC retaliation from player-sourced damage, player auto-retaliate from
  positive incoming NPC damage, aggressive NPC target acquisition, and
  simple aggressive leash/return state.
- Why it was made: Phase 6 made delayed hit resolution, recent-hit state,
  XP, death, and loot handoff explicit, but combat still lacked durable
  attacker ownership and NPC target acquisition rules. Phase 7 gives the
  combat loop enough actor relationship state for NPCs to attack back,
  for single-combat to reject invalid pileups, and for aggressive NPCs to
  start combat without viewer or interaction special cases.
- Exact surfaces changed:
  - Expanded `RcCombatActorState` in `rc-core/types.h` with a primary
    attacker, bounded attacker list, attacker count, retaliates flag, and
    multi-combat mirror.
  - Added `multi_combat` to `RcWorld` as the first world-level combat
    allowance toggle. This is intentionally generic until area/region
    flags own multi-combat state.
  - Expanded `rc-core/combat.h` with public helpers for toggling/querying
    multi-combat, querying attacker/under-attack state, registering an
    attacker, and ticking actor threat expiry.
  - Updated `rc-core/combat.c` with actor-reference helpers, single-combat
    conflict checks, attacker registration, NPC retaliation capability
    inference from definition combat fields, and multi-combat propagation
    into combat state.
  - Updated player-vs-NPC combat start so player attacks register player
    threat on the NPC, respect single-combat locks when the player is
    already under attack by another live NPC, and only arm NPC retaliation
    when the target definition is combat-capable.
  - Updated NPC-vs-player combat start so NPC attacks register NPC threat
    on the player and reject invalid second attackers unless multi-combat
    is enabled.
  - Updated player hit resolution in `rc-core/combat.c` so incoming NPC
    hits register threat and positive damage can trigger player
    auto-retaliate when the player is not already attacking.
  - Updated NPC hit resolution in `rc-core/tick.c` so player-sourced
    damage registers player threat and starts NPC retaliation through the
    combat API instead of directly mutating `target_uid`.
  - Updated NPC ticking in `rc-core/npc.c` and player status ticking in
    `rc-core/combat.c` so actor threat timers expire through normal core
    tick paths.
  - Added aggressive NPC acquisition, data-derived aggressive leash range,
    and return-to-spawn stepping in `rc-core/combat.c`. Aggressive NPCs
    can acquire the player from definition `aggressive`/`aggro_range`
    fields; non-aggressive manually started combat preserves prior
    compatibility.
  - Added `tests/test_combat_phase7_retaliation_ai.c` covering NPC
    retaliation and threat tracking after player damage, single-combat
    rejection of a second NPC, multi-combat allowance of a second NPC,
    aggressive NPC acquisition, passive NPC non-acquisition, and
    aggressive leash clearing.
  - Updated combat planning, `work.md`, and `work_highlevel.md` to mark Phase
    7 complete and set Phase 8 UI/viewer combat bridge as the next combat
    stop point.
- Gaps and deferred work:
  - Multi-combat is currently a world-level toggle. Area/region-derived
    multi-combat flags are future map/runtime work.
  - Single-combat uses a simple actor under-attack timer and bounded
    attacker list. Exact OSRS PJ timing, last-hit ownership, logout
    behavior, and multi-area transitions remain future parity work.
  - NPC retaliation capability is inferred from `attack_speed` and
    `max_hit`; a dedicated DB/content `retaliates` flag does not exist
    yet.
  - Aggressive leash/return behavior applies to aggressive acquisition.
    Manually started non-aggressive combat intentionally preserves prior
    compatibility and does not yet enforce exact retreat rules.
  - NPC attack selection remains generic definition/profile logic. Rich
    per-NPC attack scripts, style rotations, special cases, and encounter
    combat hooks remain Phase 9/10 work.
  - Player death behavior remains outside Phase 7 scope.
- Upstream/downstream impacts:
  - Phase 8 can display auto-retaliate state, target/attacker context,
    target HP, and hitsplats from core state without calculating combat in
    the viewer.
  - Phase 9 content hooks can build on explicit attacker/target ownership
    rather than scattered `target_uid` mutation.
  - Future area flag work should replace the temporary world-level
    `multi_combat` toggle as the source of truth.
  - Existing direct combat tests that use synthetic stack NPCs without
    spawn coordinates remain compatible; leash checks ignore those
    synthetic no-spawn NPCs.
- Verification:
  - Configure/build: `cmake -S . -B build` completed successfully.
  - Targeted build passed for `test_combat_phase7_retaliation_ai`,
    `test_combat_phase6_hit_pipeline`, and
    `test_regular_npc_mechanics_combat`.
  - Focused Phase 7 test passed: `ctest --test-dir build -R
    'test_combat_phase7_retaliation_ai' --output-on-failure`.
  - Targeted combat regression passed: `ctest --test-dir build -R
    'test_combat_phase7_retaliation_ai|test_combat_phase6_hit_pipeline|test_combat_phase5_formula_core|test_combat_phase4_weapon_styles_cycle|test_combat_phase3_movement_range_facing|test_combat_phase2_attack_handoff|test_combat_phase1_state|test_combat_runtime_flow|test_combat'
    --output-on-failure` completed 10/10 passing.
  - Full build passed: `cmake --build build -j2`.
  - Full regression passed: `ctest --test-dir build --output-on-failure`
    completed 57/57 tests passing in 5.46 seconds.
  - Coverage build passed in `build-coverage-combat-phase7` with
    `-DCMAKE_C_FLAGS='--coverage -O0 -g'`, and the targeted combat
    coverage set completed 11/11 passing.
  - Coverage review with `gcov -b -c` reported `rc-core/combat.c` at
    91.99% line coverage, 91.43% branches executed, and 65.09% branches
    taken at least once. `rc-core/tick.c` reported 37.19% line coverage,
    39.45% branches executed, and 23.13% branches taken at least once.
    `rc-core/npc.c` reported 57.09% line coverage, 60.18% branches
    executed, and 34.96% branches taken at least once.
    `tests/test_combat_phase7_retaliation_ai.c` reported 100.00% line
    coverage, 100.00% branches executed, and 52.86% branches taken at
    least once.
  - Benchmark smoke: 1,000 process-level invocations of
    `build/test_combat_phase7_retaliation_ai` completed in 6.22 seconds,
    roughly 160.77 process runs/sec. This is a process-level combat-AI
    smoke benchmark, not an in-process combat SPS benchmark.

## 2026-05-03 — Combat Phase 6 Hit Pipeline, Visual Events, XP, And Death

- Change made: completed Combat Phase 6 by moving queued combat damage
  toward a metadata-rich delayed-hit pipeline with core-visible recent-hit
  state, HP mirrors, NPC death/respawn state, XP award, and loot handoff.
- Why it was made: Phase 5 made formula output reusable, but combat still
  resolved queued hits mostly as aggregate damage and did not expose enough
  per-hit metadata for hitsplats, HP bars, death handling, loot handoff,
  and later viewer feedback. Phase 6 makes the damage pipeline explicit
  enough for Phase 7 retaliation/NPC AI and Phase 8 viewer rendering.
- Exact surfaces changed:
  - Expanded `RcPendingHit` in `rc-core/types.h` with `max_hit`,
    `apply_tick`, `client_delay`, and `hit_type` metadata.
  - Added hit-type values for miss, normal hit, and max hit in
    `rc-core/types.h`, and expanded `RcCombatRecentHit` with matching
    `max_hit` and `hit_type` fields.
  - Added `hp_current` and `hp_max` mirrors to `RcCombatActorState` so core
    combat state can expose current target HP without forcing viewer code to
    inspect actor-specific internals directly.
  - Expanded `rc-core/combat_hit.h` and `rc-core/combat_hit.c` with
    `rc_combat_resolve_hit_damage`, `rc_queue_hit_meta`,
    `rc_combat_actor_record_hit`, and
    `rc_combat_actor_tick_recent_hits`.
  - Updated existing queue helpers so `rc_queue_hit` and
    `rc_queue_hit_flags` delegate to the metadata-aware queue path.
  - Updated player and NPC combat sync in `rc-core/combat.c` so combat state
    mirrors HP every combat tick.
  - Updated player and NPC attack queueing in `rc-core/combat.c` to store
    formula max-hit metadata on pending hits.
  - Updated player-side hit resolution in `rc-core/combat.c` so every
    resolved hit, including misses, records recent-hit metadata.
  - Updated `rc-core/npc.c` and `rc-core/combat.c` ticking so player and NPC
    recent-hit timers decay through core tick paths.
  - Reworked NPC pending-hit resolution in `rc-core/tick.c` from aggregate
    damage resolution into per-hit resolution. Each queued hit now applies HP
    damage, records hitsplat metadata, emits damage/death state, awards XP,
    enters death/respawn timing, and hands NPC death drops to the existing
    loot/ground-item runtime.
  - Added `tests/test_combat_phase6_hit_pipeline.c` covering direct queued
    player-to-NPC max hits, NPC death state, respawn timers, XP award,
    DB-backed loot handoff, NPC recent-hit metadata, player-side miss and
    damage hits, player recent-hit metadata, HP mirrors, and damage events.
  - Updated combat planning, `work.md`, and `work_highlevel.md` to mark Phase 6
    complete and set Phase 7 retaliation/single-multi/NPC AI as the next
    combat stop point.
- Gaps and deferred work:
  - Recent-hit state is core-visible metadata, not exact OSRS hitsplat
    sprite/type/color/sound parity.
  - Pending hits are still owned by the target actor queue rather than
    storing a full target actor reference inside every hit record.
  - Player and NPC HP mirrors preserve current internal units. Player HP is
    currently mirrored in tenths while NPC HP is mirrored in whole HP; viewer
    code should use actor-aware ratios until a future normalization pass.
  - Player death mechanics are not implemented yet. Player HP can be damaged,
    but player death/item-risk/respawn behavior remains future combat/death
    runtime work.
  - `RC_EVT_NPC_DAMAGED` still represents positive damage only. NPC misses
    are visible through recent-hit state, not through that damage event.
  - Attack/block animation tokens, projectile metadata, spot animations,
    exact client delays, exact hitsplat sprites, and sounds remain Phase 8
    and Phase 10 work.
  - Loot handoff is tested through the existing drop/ground-item runtime, but
    exact item-pile ordering, ownership parity, and rare/shared drop-table
    edge cases remain loot/content backlog items.
- Upstream/downstream impacts:
  - Phase 7 can build retaliation, single-combat ownership, multi-combat
    attacker tracking, and NPC attack loops on top of per-hit damage and
    recent-hit state instead of aggregate queued damage.
  - Phase 8 viewer work can render target HP and hitsplats from core state,
    but must still treat exact sprites/animations/projectiles as presentation
    consumers of future core tokens.
  - Loot remains centralized in the existing loot/ground-item runtime; combat
    does not duplicate item-pile spawning logic.
  - Existing callers of `rc_queue_hit` and `rc_queue_hit_flags` remain
    source-compatible through metadata-aware delegation.
- Verification:
  - Targeted build passed for `test_combat_phase6_hit_pipeline`.
  - Targeted Phase 6 test passed: `ctest --test-dir build -R
    'test_combat_phase6_hit_pipeline' --output-on-failure`.
  - Targeted combat regression passed: `ctest --test-dir build -R
    'test_combat_phase6_hit_pipeline|test_combat_phase5_formula_core|test_combat_phase4_weapon_styles_cycle|test_combat_phase3_movement_range_facing|test_combat_phase2_attack_handoff|test_combat_phase1_state|test_combat_runtime_flow|test_combat'
    --output-on-failure` completed 9/9 passing.
  - Coverage build passed in `build-coverage-combat-phase6` with
    `-DCMAKE_C_FLAGS='--coverage -O0 -g'`, and the targeted combat coverage
    test set completed 9/9 passing.
  - Coverage review with `gcov -b -c` reported `rc-core/combat_hit.c` at
    89.29% line coverage, 96.72% branches executed, and 72.13% branches
    taken at least once. `rc-core/combat.c` reported 64.02% line coverage,
    57.08% branches executed, and 35.45% branches taken at least once.
    `rc-core/tick.c` reported 37.20% line coverage, 39.66% branches
    executed, and 23.67% branches taken at least once.
    `tests/test_combat_phase6_hit_pipeline.c` reported 100.00% line
    coverage, 100.00% branches executed, and 55.21% branches taken at least
    once.
  - Full build passed: `cmake --build build -j2`.
  - Full regression passed: `ctest --test-dir build --output-on-failure`
    completed 56/56 tests passing in 5.47 seconds.
  - Benchmark smoke: 1,000 process-level invocations of
    `build/test_combat_phase6_hit_pipeline` completed in 6.36 seconds,
    roughly 157.23 process runs/sec. This is a process-level hit-pipeline
    smoke benchmark, not an in-process combat SPS benchmark.

## 2026-05-03 — Combat Phase 5 Formula Core V1

- Change made: completed Combat Phase 5 by extracting the active melee,
  ranged, magic, and NPC formula path into reusable formula-core helpers.
- Why it was made: Phase 4 made selected weapon style/category state stable,
  but the formula code still exposed only coarse `rc_calc_*` entry points and
  hid the underlying effective-level, roll, max-hit, and deterministic hit
  chance mechanics. Phase 5 makes those mechanics testable and reusable before
  the delayed-hit/death pipeline work in Phase 6.
- Exact surfaces changed:
  - Expanded `rc-core/combat_formula.h` with `RC_HIT_CHANCE_SCALE` and public
    helpers for scaled hit chance, melee/ranged/magic effective levels,
    defensive effective levels, player offensive/defensive rolls, player
    melee/ranged/magic max hits, NPC offensive rolls, and NPC defensive rolls.
  - Added `rc_hit_chance_scaled` in `rc-core/combat_formula.c` so tests and
    later combat code can use deterministic 0..10000 hit chance values without
    relying on floating-point comparisons.
  - Promoted effective-level logic from static internal helpers to reusable
    helpers covering Attack, Strength, Defence, Magic Defence, Ranged Attack,
    Ranged Strength, and Magic Attack.
  - Added player offensive/defensive roll helpers that consume selected combat
    style, equipment bonuses, boosted/drained levels, stance bonuses, and
    prayer modifiers.
  - Added player max-hit helpers for melee, ranged, and magic. Magic max hit
    now centralizes equipment magic-damage and prayer magic-damage modifiers.
  - Added NPC offensive and defensive roll helpers. NPC offensive rolls select
    the NPC attack, ranged, or magic stat by combat style.
  - Updated `rc_calc_melee`, `rc_calc_ranged`, `rc_calc_magic`, and
    `rc_calc_npc_attack_style` to delegate to the shared formula helpers
    instead of duplicating roll/max-hit math locally.
  - Added a first-pass OSRS-style player magic-defence effective level that
    blends Magic and Defence levels for magic defensive rolls.
  - Added `tests/test_combat_phase5_formula_core.c` covering deterministic
    hit chance, melee/ranged/magic effective levels, prayer modifiers,
    equipment bonuses, boosted stats, NPC offensive/defensive rolls, player
    defensive rolls, player max hits, and active `rc_calc_*` delegation.
  - Updated combat planning, `work.md`, and `work_highlevel.md` to mark Phase 5
    complete and set Phase 6 hit pipeline/visual events/XP/death as the next
    combat stop point.
- Gaps and deferred work:
  - NPC defensive rolls still use the current NPC Defence stat only because
    `RcNpcDef` does not currently expose per-style defensive bonuses. Exact
    NPC defensive-bonus parity remains future data/exporter work.
  - Content modifiers are not registered in Phase 5. Slayer, salve, void,
    crystal, demonbane, dragonbane, fang, special attacks, and boss-specific
    damage reductions remain Phase 9/10 hook/content work.
  - Magic spell-family effects, elemental weaknesses, tomes, charge, staff
    bonuses, rune requirements, ammo requirements, and resource consumption
    remain later spell/resource phases.
  - Prayer-specific magic defence bonuses are not wired yet because the
    current prayer API exposes magic attack/damage helpers but not a magic
    defence helper.
  - Formula rounding uses the current integer truncation path. Exact OSRS
    per-formula rounding parity still needs deeper audit when we tune combat
    against authoritative values.
- Upstream/downstream impacts:
  - Phase 6 can build delayed hits, hitsplats, HP bars, XP, death, and loot
    handoff on top of explicit roll/max-hit helpers instead of reaching into
    duplicated formula internals.
  - Phase 7 NPC combat AI can use `rc_npc_offensive_roll` and
    `rc_player_defensive_roll` for melee/ranged/magic NPC attacks.
  - Phase 8 viewer work gets cleaner core state to display but still must not
    calculate accuracy, max hit, damage, XP, death, or loot.
  - Future DB/exporter changes that add NPC defensive bonuses or richer spell
    metadata should integrate at these helper boundaries.
- Verification:
  - Configure/build: `cmake -S . -B build` completed successfully.
  - Targeted build passed for `test_combat_phase5_formula_core`.
  - Targeted combat regression passed: `ctest --test-dir build -R
    'test_combat_phase5_formula_core|test_combat_phase4_weapon_styles_cycle|test_combat_phase3_movement_range_facing|test_combat_phase2_attack_handoff|test_combat_phase1_state|test_combat_runtime_flow|test_combat'
    --output-on-failure` completed 8/8 passing.
  - Coverage build passed in `build-coverage-combat-phase5` with
    `-DCMAKE_C_FLAGS='--coverage -O0 -g'`, and the same targeted combat tests
    completed 8/8 passing under coverage.
  - Coverage review with `gcov -b -c` reported `rc-core/combat_formula.c` at
    76.32% line coverage, 84.50% branches executed, and 51.66% branches taken
    at least once. `tests/test_combat_phase5_formula_core.c` reported 100.00%
    line coverage, 100.00% branches executed, and 51.43% branches taken at
    least once. `rng.h` reported 100.00% line coverage in this run.
  - Full build passed: `cmake --build build -j2`.
  - Full regression passed: `ctest --test-dir build --output-on-failure`
    completed 55/55 tests passing in 5.60 seconds.
  - Benchmark smoke: 1,000 process-level invocations of
    `build/test_combat_phase5_formula_core` completed in 0.51 seconds,
    roughly 1,960.78 process runs/sec. This is formula-test process smoke, not
    an in-process combat SPS benchmark.

## 2026-05-03 — Combat Phase 4 Weapon Style Tables And Attack Cycle

- Change made: completed the Phase 4 exit criteria for weapon-type style
  selection and attack-cycle speed/range behavior.
- Why it was made: the active player style path still inferred combat style
  from equipment bonuses, which made weapon behavior fragile and blocked later
  formula work from relying on stable combat class, attack type, stance, XP
  routing, speed, and range state.
- Exact surfaces changed:
  - Added `RcCombatClass` and `RcCombatAttackType` enums in
    `rc-core/types.h` so player combat state can expose the derived combat
    class and attack type without viewer-side inference.
  - Reworked `rc-core/combat_formula.c` style refresh so loaded weapons use
    exported `weapon_type` as the active style-table selector.
  - Added first-pass weapon-type mappings for common melee, ranged, magic,
    staff, spear, whip, 2h/axe, slash-sword, stab-sword, and multistyle
    categories.
  - Kept bonus-based melee style selection only as a fallback for missing or
    unknown weapon metadata; it is no longer the active path for loaded weapon
    definitions.
  - Updated player style refresh to mirror selected style index, combat style,
    stance, XP mask, weapon category, attack type, combat class, and attack
    range into `RcCombatActorState`.
  - Updated `rc_player_attack_speed` so rapid style reduces attack speed by
    one tick while preserving item-defined attack speed and existing magic/
    ranged/melee defaults.
  - Updated `rc_player_attack_range` so longrange extends non-melee attack
    range by two tiles while preserving item-defined range and existing magic/
    ranged/melee defaults.
  - Added `tests/test_combat_phase4_weapon_styles_cycle.c` covering melee
    style metadata, controlled XP routing, ranged rapid/longrange modifiers,
    powered-staff magic selection, explicit avoidance of bonus-based guessing
    for loaded ranged weapons, and live attack cooldown behavior.
  - Updated combat planning, `work.md`, and `work_highlevel.md` to mark Phase 4
    complete and set Phase 5 formula core v1 as the next combat stop point.
- Gaps and deferred work:
  - Exact OSRS style-button parity is not complete for every exported weapon
    type. The new path is data-backed by weapon type, but niche/per-weapon
    style layouts such as salamanders, bulwarks, claws, thrown weapons,
    scythes, and other edge cases need deeper parity passes.
  - Attack animation token selection is still indirect through combat style,
    attack type, and the legacy attack animation timer. Explicit viewer-facing
    animation token IDs remain Phase 8 UI/viewer bridge work.
  - Special attack behavior remains Phase 10 work. This phase did not add
    weapon-specific special handlers, energy spending, or pre-special
    modifier hooks in `rc-core`.
  - `RcCombatActorState` still does not store attack speed directly; live
    attack-cycle cooldown remains observable through the existing player
    `attack_timer` and the `rc_player_attack_speed` helper until the legacy
    loop is removed in later combat phases.
- Upstream/downstream impacts:
  - Formula Phase 5 can now consume stable combat class, attack type, stance,
    XP mask, speed, and range state instead of re-deriving style from bonuses.
  - Viewer Phase 8 can eventually render selected combat style from core
    metadata, but viewer code still must not calculate combat rules.
  - Item exporter `weapon_type` values are now materially gameplay-visible in
    the combat path, so future DB/exporter changes to weapon type mappings can
    change player combat behavior.
  - Existing combat, interaction, and inventory/equipment tests remain
    compatible; no viewer or content-hook behavior was changed.
- Verification:
  - Configure/build: `cmake -S . -B build` completed successfully.
  - Targeted build passed for `test_combat_phase4_weapon_styles_cycle`.
  - Targeted combat regression passed: `ctest --test-dir build -R
    'test_combat_phase4_weapon_styles_cycle|test_combat_phase3_movement_range_facing|test_combat_phase2_attack_handoff|test_combat_phase1_state|test_combat_runtime_flow'
    --output-on-failure` completed 5/5 passing.
  - Coverage build passed in `build-coverage-combat-phase4` with
    `-DCMAKE_C_FLAGS='--coverage -O0 -g'`, and the same targeted combat tests
    completed 5/5 passing under coverage.
  - Coverage review with `gcov -b -c` reported `rc-core/combat_formula.c` at
    64.72% line coverage, 75.12% branches executed, and 43.32% branches taken
    at least once. `tests/test_combat_phase4_weapon_styles_cycle.c` reported
    100.00% line coverage, 100.00% branches executed, and 52.08% branches
    taken at least once. `rng.h` reported 100.00% line coverage in this run.
  - Full build passed: `cmake --build build -j2`.
  - Full regression passed: `ctest --test-dir build --output-on-failure`
    completed 54/54 tests passing in 6.62 seconds.
  - Benchmark smoke: 1,000 process-level invocations of
    `build/test_combat_phase4_weapon_styles_cycle` completed in 18.23 seconds,
    roughly 54.85 process runs/sec. This is still process-level smoke, not an
    in-process combat SPS baseline.

## 2026-05-03 — Combat Phase 3 Movement Range LOS And Facing

- Change made: completed Combat Phase 3 by moving combat range/facing
  observability and first-pass combat positioning into the combat path.
- Why it was made: Phase 2 handed NPC `Attack` interactions to the new
  combat API, but the combat state still did not expose whether an actor was
  in range, what attack range was being used, whether LOS was available, or
  whether approach/facing logic was operating against target footprints. The
  next combat phases need stable combat-owned movement/range/facing state
  before weapon style tables and attack-cycle work can be made authoritative.
- Exact surfaces changed:
  - Updated `RcCombatActorState` in `rc-core/types.h` with
    `attack_range`, `distance_to_target`, and `line_of_sight` fields.
  - Updated `rc-core/combat.c` player/NPC combat-state initialization and
    legacy synchronization to populate range, nearest-footprint distance,
    LOS, and `RC_COMBAT_STATE_IN_RANGE`.
  - Added footprint-aware helpers in `rc-core/combat.c` for detecting
    whether the player is inside a target footprint, checking player-vs-NPC
    attack range, checking NPC-vs-player attack range, choosing valid player
    attack tiles around NPC footprints, and stepping NPCs toward the player.
  - Updated player combat ticking so player attacks require a valid
    footprint-aware attack position. For melee, a player standing under a
    large NPC is no longer considered in range; the player routes toward a
    border attack tile first.
  - Updated player approach routing so it chooses a valid attack tile around
    the NPC footprint instead of routing only to a naive clamped target tile.
  - Updated NPC combat ticking so selected NPC style/range state is recorded,
    NPCs face the player while engaged, NPC combat movement updates
    `prev_x/prev_y`, and NPCs step toward the player when out of range or LOS.
  - Updated combat start so player-vs-NPC and NPC-vs-player starts
    immediately set facing state before the first attack tick.
  - Added `tests/test_combat_phase3_movement_range_facing.c` covering
    large-NPC in-range flags/facing, out-of-range player route creation,
    player-under-large-target step-out, and NPC chase/facing/range state.
  - Updated combat planning, `work.md`, and `work_highlevel.md` to mark Phase 3
    complete and set Phase 4 weapon style tables/attack cycle as the next
    combat stop point.
- Gaps and deferred work:
  - The old combat tick loop still owns the actual attack cycle and hit
    scheduling. Phase 3 improves movement/range/facing behavior and state,
    but Phase 4 and Phase 6 still own authoritative attack-cycle and hit
    pipeline replacement.
  - Exact large-NPC overlap parity is not complete. Player melee attacks now
    step out before attacking from under a large target, but NPCs spawned
    directly on the player still keep legacy immediate-attack behavior so
    existing regular-NPC mechanic tests can assert damage effects without
    becoming movement tests.
  - LOS uses the current `rc_has_los` helper. Exact OSRS projectile LOS,
    corner clipping, and large-NPC corner behavior remain future movement
    parity work.
  - NPC chase is still a direct single-step helper, not a full route planner
    with aggro/leash/retreat rules. Phase 7 owns richer NPC combat AI,
    single/multi restrictions, and aggression/leash state.
  - Player route selection picks a nearby valid attack tile around the target
    footprint but does not yet exhaustively search all reachable alternative
    tiles under every blocked-map configuration.
- Upstream/downstream impacts:
  - Viewer and future UI bridge work can now read combat state for active
    range, distance, LOS, target, and facing without recalculating gameplay
    rules in `rc-viewer`.
  - Phase 4 can use the populated `style`, `stance`, `attack_range`, and
    selected-style state when replacing ad hoc attack-cycle behavior with
    data-backed weapon style tables.
  - Existing interaction and regular-NPC mechanics remain compatible. A full
    regression caught the overlap behavior change and the final implementation
    preserves existing damage-mechanic assertions while keeping the new player
    under-target route-out contract.
- Verification:
  - Configure/build: `cmake -S . -B build` completed successfully.
  - Targeted build passed for `test_combat_phase3_movement_range_facing`,
    `test_combat_phase2_attack_handoff`, `test_combat_phase1_state`,
    `test_interaction_engine_phase6`, and `test_combat_runtime_flow`.
  - Targeted tests passed: `ctest --test-dir build -R
    'test_combat_phase3_movement_range_facing|test_combat_phase2_attack_handoff|test_combat_phase1_state|test_interaction_engine_phase6|test_combat_runtime_flow'
    --output-on-failure` completed 5/5 passing.
  - First full regression run found one adjacent regression in
    `test_regular_npc_mechanics_combat`: Araxxor was spawned directly on the
    player for damage-mechanic testing and no longer attacked after the first
    under-footprint range change. The implementation was adjusted so the new
    player-under-large-target step-out remains, while NPC-on-player legacy
    mechanics tests still attack immediately.
  - Regression check for the fix passed:
    `ctest --test-dir build -R
    'test_regular_npc_mechanics_combat|test_combat_phase3_movement_range_facing'
    --output-on-failure` completed 2/2 passing.
  - Coverage build passed in `build-coverage-combat-phase3` with
    `-DCMAKE_C_FLAGS='--coverage -O0 -g'`, and targeted coverage tests
    completed 6/6 passing after including `test_regular_npc_mechanics_combat`.
  - Coverage review with `gcov -b -c` reported `rc-core/combat.c` at 64.92%
    line coverage and 48.34% branches taken at least once, and
    `tests/test_combat_phase3_movement_range_facing.c` at 100.00% line
    coverage and 51.67% branches taken at least once. `rng.h` was 100.00%
    line covered in this run.
  - Benchmark smoke: 1,000 quiet process-level invocations of
    `build/test_combat_phase3_movement_range_facing` completed in 16.25
    seconds, roughly 61.54 process runs/sec. This is still process-level
    smoke, not a clean in-process combat SPS baseline.
  - Full build passed: `cmake --build build -j2`.
  - Full regression passed: `ctest --test-dir build --output-on-failure`
    completed 53/53 tests passing in 6.18 seconds.

## 2026-05-03 — Combat Phase 2 Interaction Attack Handoff

- Change made: completed Combat Phase 2 by routing default NPC `Attack`
  interactions through the new combat API instead of directly mutating
  legacy combat target fields.
- Why it was made: Phase 1 introduced explicit combat actor state, but
  the default NPC attack handler still wrote `player.attack_target`,
  `player.attack_target_def_id`, and `npc.target_uid` directly. Phase 2
  makes the interaction engine hand attack control to combat through
  `rc_combat_start_player_vs_npc`, matching the RSMod/VoidPS/2011Scape
  reference pattern where interaction validates and dispatches intent, then
  combat owns active engagement state.
- Exact surfaces changed:
  - Updated `api_default_npc_attack_handler` in `rc-core/tick.c` to call
    `rc_combat_start_player_vs_npc(world, 0, npc->uid)` after validating
    the NPC target and confirming the selected option is a data-backed
    `Attack` option.
  - Removed direct legacy combat target writes from the default attack
    handler. The compatibility mirror now comes from the combat API rather
    than the interaction handler.
  - Preserved interaction metadata in the default attack handler:
    `RC_INTERACT_NPC_ATTACK`, `interact_target`, and `interact_option` are
    still set so existing viewer/tests can observe the selected action while
    combat state becomes the target source of truth.
  - Added `api_stop_player_combat` in `rc-core/tick.c`, which stops the
    player combat actor and the current NPC target actor through
    `rc_combat_stop_actor`.
  - Updated manual walk/run actions to cancel combat through
    `api_stop_player_combat` instead of directly clearing only legacy player
    fields.
  - Updated accepted non-attack NPC options, object actions,
    inventory/equipment item actions, widget actions, item-on-target
    actions, and spell-on-target actions to cancel active combat through the
    new stop path.
  - Added `tests/test_combat_phase2_attack_handoff.c` covering default
    attack handoff into new combat state, content-handler priority over the
    generic default attack handler, and noncombat cancellation clearing both
    player and NPC combat state.
  - Updated combat planning, `work.md`, and `work_highlevel.md` to mark Phase 2
    complete and set Phase 3 movement/range/LOS/facing as the next combat
    stop point.
- Gaps and deferred work:
  - The old combat tick loop still owns the actual attack cycle, movement,
    range maintenance, facing, hit application, retaliation, and death
    behavior. Phase 2 only changes the interaction-to-combat handoff and
    cancellation boundary.
  - Direct low-level calls to `rc_interaction_begin` do not automatically
    stop combat because that API only receives `RcPlayer *` and does not
    have world/NPC context. Public world-level APIs now stop combat when
    accepted; direct low-level callers should be used carefully or routed
    through world-level helpers when combat cancellation matters.
  - The new stop helper clears the current player target and matching NPC
    target. It is not yet a full multi-attacker/single-combat cleanup
    system; Phase 7 owns attacker lists, single/multi rules, and richer
    retaliation ownership.
  - Spell-on-NPC currently cancels existing combat like other noncombat
    interaction intents. Future combat-spell/autocast behavior should be
    routed through the combat engine when Phase 10 adds spells/runes/prayer
    depth.
  - Phase 3 still needs to move approach, attack range, LOS, and facing into
    combat-owned per-tick state.
- Upstream/downstream impacts:
  - Interaction Engine v1 remains the entry point for NPC `Attack`, but the
    default attack handler no longer owns combat target mutation.
  - Content-specific attack handlers can still override the default handler
    by registering a more specific NPC attack dispatch key.
  - Viewer behavior should remain compatible because legacy interaction and
    combat fields are still mirrored, but future viewer code should prefer
    `RcCombatActorState` once Phase 8 exposes the combat bridge cleanly.
  - Noncombat public actions are now more aggressive about cancelling combat,
    which matches OSRS interaction semantics and prevents stale NPC target
    state from surviving accepted player actions.
- Verification:
  - `work_highlevel.md` was checked first and already showed Phase 2 as the
    active combat stop point before implementation began.
  - Configure/build: `cmake -S . -B build` completed successfully.
  - Targeted build passed for `test_combat_phase2_attack_handoff`,
    `test_combat_phase1_state`, `test_interaction_engine_phase5`,
    `test_interaction_engine_phase6`, `test_combat_runtime_flow`, and
    `test_npc_option_interactions`.
  - Targeted tests passed: `ctest --test-dir build -R
    'test_combat_phase2_attack_handoff|test_combat_phase1_state|test_interaction_engine_phase5|test_interaction_engine_phase6|test_combat_runtime_flow|test_npc_option_interactions'
    --output-on-failure` completed 6/6 passing.
  - Coverage build passed in `build-coverage-combat-phase2` with
    `-DCMAKE_C_FLAGS='--coverage -O0 -g'`, and the same targeted coverage
    tests completed 6/6 passing.
  - Coverage review with `gcov -b -c` reported `rc-core/tick.c` at 35.63%
    line coverage and 22.19% branches taken at least once,
    `rc-core/combat.c` at 51.26% line coverage and 24.09% branches taken at
    least once, and `tests/test_combat_phase2_attack_handoff.c` at 100.00%
    line coverage. The low aggregate percentages reflect broad files being
    reviewed against a narrow targeted Phase 2 test set, not an uncovered
    new test file.
  - Benchmark smoke: 10,000 process-level invocations of
    `build/test_combat_phase2_attack_handoff` completed in 156.57 seconds,
    roughly 63.87 process runs/sec. This is noisy because stderr was not
    redirected and each process logs NPC definition loading; it remains a
    process-level smoke benchmark, not a clean in-process combat SPS
    baseline.
  - Full build passed: `cmake --build build -j2`.
  - Full regression passed: `ctest --test-dir build --output-on-failure`
    completed 52/52 tests passing in 6.40 seconds.

## 2026-05-03 — Combat Phase 1 Actor State Model

- Change made: completed Combat Phase 1 by adding explicit combat
  actor/target state for players and NPCs while preserving compatibility
  with the legacy combat fields and old player/NPC combat tick loop.
- Why it was made: Phase 0 split reusable formula and hit helpers out of
  the old combat loop, but runtime target state was still scattered across
  `RcPlayer.attack_target`, `RcPlayer.attack_target_def_id`,
  `RcNpc.target_uid`, loose attack timers, selected style fields, pending
  hits, and viewer-facing legacy state. The combat rewrite needs a single
  actor state model before Phase 2 can hand interaction `Attack` actions to
  the new combat API.
- Exact surfaces changed:
  - Updated `rc-core/types.h` with `RcCombatActorKind`,
    `RcCombatActorRef`, `RcCombatTargetRef`,
    `RcCombatRecentHit`, `RcCombatActorState`, and combat-state flags for
    active, started, in-range, cooldown wait, cancelled, dead target, and
    invalid target states.
  - Embedded `RcCombatActorState combat` on both `RcPlayer` and `RcNpc`.
  - Updated `rc-core/combat.h` with Phase 1 APIs:
    `rc_combat_init_player_state`, `rc_combat_init_npc_state`,
    `rc_combat_start_player_vs_npc`, `rc_combat_start_npc_vs_player`,
    `rc_combat_stop_actor`, `rc_combat_tick_world`,
    `rc_combat_set_player_style`, `rc_combat_toggle_auto_retaliate`,
    `rc_combat_toggle_special`, and `rc_combat_actor_has_target`.
  - Updated `rc-core/world.c` so player default initialization initializes
    the new combat state after legacy combat/style defaults are set.
  - Updated `rc-core/npc.c` so NPC spawn and respawn initialize or clear
    the new combat state alongside legacy NPC target fields.
  - Updated `rc-core/combat.c` with Phase 1 target construction,
    legacy-to-new synchronization, bidirectional player/NPC combat start,
    actor stop, world sync tick, style/toggle APIs, and active-target
    queries.
  - Updated `rc-core/combat.c` so legacy public tick wrappers sync the new
    combat actor state after the old runtime loop runs. This keeps old
    behavior active while making the new state observable and testable.
  - Updated `rc-core/combat_formula.c` so existing attack-style selection
    refreshes the new combat style, stance, and XP-mask fields as part of
    the compatibility bridge.
  - Added `tests/test_combat_phase1_state.c` covering player/NPC combat
    state initialization, player-vs-NPC start, NPC-vs-player start,
    target reference metadata, actor stop/cancel flags, style/toggle APIs,
    and legacy tick synchronization.
  - Updated `tests/test_objects_runtime.c` to assert the named
    `RC_INTERACT_OBJECT` enum contract instead of a stale numeric literal,
    and to tick coordinate object interactions through the interaction
    processor before asserting door, skilling-node, and traversal effects.
  - Updated combat planning, `work.md`, and `work_highlevel.md` to mark Phase
    1 complete and set Phase 2 interaction attack handoff as the next
    combat stop point.
- Gaps and deferred work:
  - The old player/NPC combat loop still owns runtime attack behavior.
    Phase 1 intentionally adds state and compatibility sync only; Phase 2
    starts routing `Attack` interactions through the new API, and later
    phases replace movement, range, hit pipeline, retaliation, and visual
    events.
  - Legacy fields such as `attack_target`, `attack_target_def_id`,
    `target_uid`, `attack_timer`, `attack_count`, `pending_hits`, and
    selected style fields remain mirrored for compatibility. Tests that
    still assert those fields directly should migrate toward
    `RcCombatActorState` as the new contract becomes authoritative.
  - Recent-hit state exists structurally but is not yet the source of
    viewer hitsplat rendering. Phase 6 and Phase 8 own hit visual state and
    viewer bridge work.
  - `rc_combat_tick_world` is currently a synchronization bridge over the
    existing player/NPC tick wrappers, not the final world combat scheduler.
  - Content-specific branches remain in `rc-core/combat.c` from the old
    loop. They are tracked for Phase 9 content-hook migration, not solved
    in Phase 1.
  - The benchmark is still process-level smoke coverage, not a clean
    in-process combat SPS benchmark. A cleaner headless combat benchmark
    should be added once the new engine owns the active attack loop.
- Upstream/downstream impacts:
  - Phase 2 can now make the default NPC `Attack` interaction call
    `rc_combat_start_player_vs_npc` instead of writing legacy fields
    directly.
  - Interaction, loot, object, and viewer code can continue using existing
    APIs because legacy fields remain mirrored.
  - The object runtime test update reflects the already-established
    Interaction Engine v1 contract: coordinate object interactions are
    pending intents resolved by the tick processor, not always immediate
    mutations at API-call time.
  - No viewer combat rendering behavior was changed in this phase.
- Verification:
  - Full build passed with `cmake --build build -j2`.
  - Targeted build passed for `test_combat_phase1_state`, `test_combat`,
    `test_combat_e2e`, `test_combat_runtime_flow`,
    `test_interaction_engine_phase6`, and
    `test_regular_npc_mechanics_combat`.
  - Targeted combat/interaction tests passed: `ctest --test-dir build -R
    'test_combat_phase1_state|test_combat|test_combat_e2e|test_combat_runtime_flow|test_interaction_engine_phase6|test_regular_npc_mechanics_combat'
    --output-on-failure` completed 6/6 passing.
  - Coverage build passed in `build-coverage-combat-phase1` with
    `-DCMAKE_C_FLAGS='--coverage -O0 -g'`, and the same targeted coverage
    tests completed 6/6 passing.
  - Coverage review with `gcov -b -c` reported `rc-core/combat.c` at
    90.78% line coverage and 64.29% branches taken at least once,
    `rc-core/combat_formula.c` at 68.94% line coverage and 43.40%
    branches taken at least once, `rc-core/combat_hit.c` at 88.68% line
    coverage and 81.48% branches taken at least once, and
    `tests/test_combat_phase1_state.c` at 100.00% line coverage. Combined
    combat module line coverage across the reviewed combat files was
    86.40%.
  - Benchmark smoke: 10,000 process-level invocations of a Release-linked
    `test_combat_phase1_state` clone completed in 152.50 seconds, roughly
    65.57 process runs/sec. The run is noisy because each process reloads
    fixture data and emits NPC-definition loader logs.
  - Initial full regression before rebuilding every test executable exposed
    stale binaries that still failed on `npc_defs: unsupported version 4`.
    A full rebuild fixed that stale-binary issue.
  - Full regression after rebuild and object-runtime assertion alignment
    passed: `ctest --test-dir build --output-on-failure` completed 51/51
    tests passing in 6.55 seconds.

## 2026-05-03 — Combat Phase 0 Extraction And Legacy Quarantine

- Change made: completed Combat Phase 0 by separating reusable combat
  formula/roll helpers and delayed-hit helpers out of the prototype combat
  loop, while keeping the existing public combat API and runtime behavior
  compatible.
- Why it was made: the planned combat rewrite needs explicit seams before
  Phase 1 introduces actor combat state. The old implementation mixed pure
  math, pending-hit queue mechanics, content-specific damage/profile rules,
  movement/range/facing, and the player/NPC auto-attack loops in one file.
  This phase gives later work stable module boundaries without changing
  viewer or gameplay behavior yet.
- Exact surfaces changed:
  - Added `rc-core/combat_formula.h` and `rc-core/combat_formula.c` for
    reusable combat math, style selection, weapon-derived speed/range,
    player-vs-NPC formula helpers, NPC-vs-player formula helpers, NPC
    preferred-style selection, and attack rolling.
  - Added `rc-core/combat_hit.h` and `rc-core/combat_hit.c` for hit-delay
    lookup, pending-hit queueing, pending-hit flag queueing, protection-prayer
    mitigation, and generic pending-hit resolution.
  - Updated `rc-core/combat.c` to consume those extracted helpers and to keep
    regular-NPC/content-profile damage rules plus legacy loop orchestration in
    the old file for now.
  - Updated `rc-core/combat.c` so `rc_combat_tick_player` and
    `rc_combat_tick_npc` are compatibility wrappers around static legacy
    internals. Later phases can replace the internals while preserving callers.
  - Updated `rc-core/combat.h` to mark the current player/NPC combat tick
    entry points as Phase-0 compatibility wrappers.
  - Updated combat planning, `work.md`, and `work_highlevel.md` to mark Phase 0
    complete and set Phase 1 as the next combat stop point.
- Old field-coupled tests identified for migration: tests currently asserting
  direct legacy fields such as `RcPlayer.attack_target`,
  `RcPlayer.attack_target_def_id`, or `RcNpc.target_uid` include
  `tests/test_combat_e2e.c`, `tests/test_combat_runtime_flow.c`,
  `tests/test_interaction_engine_phase3.c`,
  `tests/test_interaction_engine_phase4.c`,
  `tests/test_interaction_engine_phase5.c`,
  `tests/test_interaction_engine_phase6.c`,
  `tests/test_npc_option_interactions.c`,
  `tests/test_prayer_spell_actions_runtime.c`, and
  `tests/test_regular_npc_mechanics_combat.c`.
- Gaps and deferred work:
  - The old combat loop still owns runtime behavior. This is intentional for
    Phase 0; Phase 1 introduces the new combat actor state, and Phase 2 moves
    interaction attack handoff to the new API.
  - Content-specific profile/tag branches remain in `rc-core/combat.c`. They
    are explicitly tracked for Phase 9 content-hook migration, not solved in
    this extraction pass.
  - Legacy direct fields such as `attack_target`, `attack_target_def_id`,
    `target_uid`, `attack_timer`, and `attack_count` are still the runtime
    source of truth until Phase 1 replaces them with mirrored compatibility
    state.
  - The benchmark is a process-level smoke benchmark using an existing combat
    test binary, not a clean in-process SPS baseline. A clean headless combat
    benchmark should be added once the new engine owns runtime state.
- Upstream/downstream impacts: CMake's existing `rc-core/*.c` glob picks up the
  new modules automatically, so no build-system change was needed. Existing
  tests, viewer callers, and interaction handlers can continue calling the
  public combat API. Phase 1 can now add `RcCombatActorState` beside the old
  fields with less risk of mixing formula/hit-pipeline mechanics into state
  migration work.
- Verification:
  - Configure/build: `cmake -S . -B build` completed successfully.
  - Targeted build passed for `test_combat`, `test_combat_e2e`,
    `test_combat_runtime_flow`, `test_interaction_engine_phase6`, and
    `test_regular_npc_mechanics_combat`.
  - Targeted tests passed: `ctest --test-dir build -R
    'test_combat|test_combat_e2e|test_combat_runtime_flow|test_interaction_engine_phase6|test_regular_npc_mechanics_combat'
    --output-on-failure` completed 5/5 passing.
  - Coverage build passed in `build-coverage-combat-phase0` with
    `-DCMAKE_C_FLAGS='--coverage -O0 -g'`, and the same targeted coverage
    tests completed 5/5 passing.
  - Coverage review with `gcov -b -c` reported `rc-core/combat.c` at 91.24%
    line coverage and 66.91% branches taken at least once,
    `rc-core/combat_formula.c` at 66.67% line coverage and 42.77% branches
    taken at least once, `rc-core/combat_hit.c` at 88.68% line coverage and
    81.48% branches taken at least once, and `tests/test_combat.c` at 100.00%
    line coverage.
  - Benchmark smoke: 500 process-level invocations of a Release-built
    `test_combat_e2e` clone completed in 9.43 seconds, roughly 53.02
    process runs/sec. This is not directly comparable to future in-process
    combat SPS because each invocation reloads/logs fixture data.
  - Broader regression check: `ctest --test-dir build --output-on-failure`
    completed 44/50 passing. The 6 failures were all NPC-definition/data
    loading dependents reporting `npc_defs: unsupported version 4`:
    `test_activity_spawns_runtime`, `test_modular_loading`,
    `test_npc_defs_bin`, `test_prayer_spell_actions_runtime`,
    `test_slayer_bin`, and `test_spawn_slices_runtime`. This Phase 0 code
    change did not modify NPC definition loading or generated data, so those
    failures are recorded as a current repo/data-version blocker outside the
    combat extraction itself.

## 2026-05-02 — Loot And Ground Items Phase 5 Item/Spell Ground Hooks

- Change made: completed Phase 5 by adding explicit item-on-ground-item and
  spell-on-ground-item fallback handling to the default ground-item interaction
  path. Registered content handlers with more specific dispatch keys still win,
  while unsupported item-on/spell-on ground-item interactions now fail with
  `RC_INTERACTION_FAIL_NO_HANDLER` without mutating source inventory or target
  ground item state.
- Why it was made: Phase 8 created source-aware interaction APIs, but loot
  Phase 5 needed the ground-item side of those APIs to be proven and made
  explicit. This gives future Telegrab, spell-on-item, and item-on-item content
  a stable hook without hardcoding those effects in `rc-core`.
- Exact surfaces changed:
  - Updated `api_default_ground_item_handler` in `rc-core/tick.c` so
    `RC_INTERACTION_USE_ON` validates source item metadata and returns a
    ground-item-specific no-handler failure when no content handler is
    registered.
  - Updated `api_default_ground_item_handler` in `rc-core/tick.c` so
    `RC_INTERACTION_SPELL_ON` validates source spell metadata and returns a
    ground-item-specific no-handler failure when no content handler is
    registered.
  - Updated `api_register_default_ground_item_handlers` in `rc-core/tick.c` to
    install default fallback handlers for OP1 `Take`, `USE_ON`, and `SPELL_ON`.
  - Added `tests/test_ground_items_phase5.c` covering item-on-ground dispatch,
    spell-on-ground dispatch, source item/source spell propagation, unsupported
    fallback behavior, invalid source fallback behavior, and no-mutation
    guarantees for source inventory and target ground items.
  - Updated `loot_interaction.md`, `work.md`, and `work_highlevel.md` to mark
    Phase 5 complete and set Phase 6 as the next loot stop point.
- Gaps and deferred work:
  - Telegrab is not implemented. It should be a future content handler using
    the Phase 5 spell-on-ground hook, not a hardcoded core branch.
  - Item-on-ground-item content effects are not implemented. This phase only
    provides dispatch and no-handler behavior.
  - Spell resource/rune consumption, projectile delay, XP, animation, and sound
    behavior are not implemented for spell-on-ground-item actions.
  - Pending interactions store source item id but not a durable source
    inventory-slot generation. If an item-on-ground interaction involves
    movement and the source inventory changes before arrival, future work
    should add source-slot/generation revalidation before content mutation.
- Upstream/downstream impacts: content code can now register exact handlers by
  target ground item id plus source item id or source spell id. Viewer code can
  keep translating item/spell-on-ground clicks into the existing API without
  owning gameplay rules. Existing OP1 `Take` behavior remains on the same
  default handler and existing registered custom handlers continue to override
  fallbacks by dispatch-key specificity.
- Verification:
  - Configure/build: `cmake -S . -B build` and targeted builds for
    `test_ground_items_phase1`, `test_ground_items_phase2`,
    `test_ground_items_phase3`, `test_ground_items_phase4`,
    `test_ground_items_phase5`, `test_interaction_engine_phase7`,
    `test_interaction_engine_phase8`, `test_inventory_equipment_runtime`,
    `test_drops_runtime`, and `test_combat_e2e` completed successfully.
  - Targeted tests passed: `ctest --test-dir build -R
    'test_ground_items_phase1|test_ground_items_phase2|test_ground_items_phase3|test_ground_items_phase4|test_ground_items_phase5|test_interaction_engine_phase7|test_interaction_engine_phase8|test_inventory_equipment_runtime|test_drops_runtime|test_combat_e2e'
    --output-on-failure` completed 10/10 passing.
  - Coverage tests passed in a fresh coverage directory:
    `ctest --test-dir build-coverage-phase5 -R
    'test_ground_items_phase1|test_ground_items_phase2|test_ground_items_phase3|test_ground_items_phase4|test_ground_items_phase5|test_interaction_engine_phase7|test_interaction_engine_phase8|test_inventory_equipment_runtime'
    --output-on-failure` completed 8/8 passing.
  - Coverage review with `gcov -b -c` on changed/adjacent core files reported
    `rc-core/tick.c` at 62.75% line coverage and 63.23% branch execution,
    `rc-core/interaction.c` at 81.93% line coverage and 91.96% branch
    execution, `rc-core/items.c` at 87.40% line coverage and 93.65% branch
    execution, and 73.00% combined line coverage for those inspected files.
  - Benchmark review: a 1,000,000-iteration ground-item hook benchmark with a
    persistent non-despawning fixture item reported 5,173,487.93 item-on
    fallback ops/sec, 4,044,639.90 item-on custom-handler ops/sec,
    5,249,936.48 spell-on fallback ops/sec, and 4,817,911.40 spell-on
    custom-handler ops/sec. No pre-Phase-5 baseline existed because these
    fallback paths were not implemented before this phase.

## 2026-05-02 — Loot And Ground Items Phase 4 Player Drop Cleanup

- Change made: completed the current player Drop cleanup pass. Successful
  inventory drops now emit `RC_EVT_ITEM_DROPPED` after the shared
  ground-item spawn path accepts the drop, inventory mutation succeeds, and
  player bonuses are recalculated. The drop path continues to use the existing
  ground-item registry rules for stackability, ownership, visibility,
  reveal/despawn timers, non-stackable splitting, and tile caps.
- Why it was made: Phase 1 through Phase 3 hardened ground items, pickup, and
  NPC death loot. Player-initiated drops still lacked an explicit event hook
  for downstream viewer/content behavior such as sounds, messages, overlays,
  or analytics. This phase makes successful player drops observable without
  moving gameplay rules into `rc-viewer` or creating another item registry.
- Exact surfaces changed:
  - Added `RC_EVT_ITEM_DROPPED` to `rc-core/events.h` and documented that it
    uses `RcPayloadItemEvent`.
  - Updated `rc-core/items.c` so `rc_player_drop_item` fires
    `RC_EVT_ITEM_DROPPED` with item id, clamped quantity, and the source
    inventory slot after a successful drop.
  - Added `tests/test_ground_items_phase4.c` covering drop-event emission,
    inventory removal, ground-item spawn state, tradeable private-to-public
    reveal followed by despawn, and untradeable private-permanent visibility
    until despawn.
  - Updated `AGENT.md` with an explicit instruction that changelog entries
    must call out known gaps, assumptions, deferred parity, compatibility
    concerns, and upstream/downstream risks.
  - Updated `loot_interaction.md`, `work.md`, and `work_highlevel.md` to mark
    Phase 4 complete and set the next loot stop point.
- Gaps and deferred work:
  - True charged-item or item-instance metadata preservation is not implemented
    because `RcInvSlot` currently stores only `item_id` and `quantity`. This
    requires a future item-instance metadata model before charges, imbues,
    degradation, custom names, or similar per-instance state can survive
    inventory/equipment/ground transitions.
  - Drop sound is hook-ready but not implemented as a sound event or viewer
    behavior. Downstream viewer/content code can subscribe to
    `RC_EVT_ITEM_DROPPED` later and translate it into presentation effects.
  - Multiplayer ownership, ironman restrictions, death piles, PvP loot, and
    exact OSRS public/private timing parity remain outside this phase.
- Upstream/downstream impacts: event subscribers now have a stable player-drop
  signal, so later viewer UI, sound, logging, analytics, or content scripts can
  react without intercepting inventory mutation. Existing callers of
  `rc_player_drop_item` keep the same mutation behavior. Adding the event enum
  increases `RC_EVT_MAX` but does not change existing event semantics.
- Verification:
  - Configure/build: `cmake -S . -B build` and targeted builds for
    `test_ground_items_phase1`, `test_ground_items_phase2`,
    `test_ground_items_phase3`, `test_ground_items_phase4`,
    `test_inventory_equipment_runtime`, `test_interaction_engine_phase7`,
    `test_interaction_engine_phase8`, `test_drops_runtime`, and
    `test_combat_e2e` completed successfully.
  - Targeted tests passed: `ctest --test-dir build -R
    'test_ground_items_phase1|test_ground_items_phase2|test_ground_items_phase3|test_ground_items_phase4|test_inventory_equipment_runtime|test_interaction_engine_phase7|test_interaction_engine_phase8|test_drops_runtime|test_combat_e2e'
    --output-on-failure` completed 9/9 passing.
  - Coverage tests passed: `ctest --test-dir build-coverage -R
    'test_ground_items_phase1|test_ground_items_phase2|test_ground_items_phase3|test_ground_items_phase4|test_inventory_equipment_runtime|test_interaction_engine_phase7|test_interaction_engine_phase8'
    --output-on-failure` completed 7/7 passing.
  - Coverage review with `gcov -b -c` on changed/adjacent core files reported
    `rc-core/items.c` at 87.40% line coverage and 93.65% branch execution,
    `rc-core/events.c` at 65.52% line coverage and 53.85% branch execution,
    `rc-core/tick.c` at 58.57% line coverage and 58.41% branch execution, and
    68.83% combined line coverage for those inspected files.
  - Benchmark review: a 1,000,000-iteration player coin-drop benchmark without
    a subscriber completed in 0.137271 seconds, or 7,284,844.59 drops/sec. The
    same benchmark with one `RC_EVT_ITEM_DROPPED` subscriber completed in
    0.139314 seconds, or 7,178,027.99 drops/sec, with 1,000,000 events
    observed.

## 2026-05-02 — Loot And Ground Items Phase 3 NPC Death Drops

- Change made: wired NPC death into DB-backed loot spawning while reusing the
  existing ground-item path. NPC death transitions now call a small loot helper
  when `RC_SUB_LOOT` is enabled and the death source is the local player.
  `rc_roll_npc_loot` rolls existing `drops.bin` table data into flat
  `RcLootDrop` results, and the results spawn at the NPC death tile through
  `rc_ground_item_spawn`.
- Why it was made: Phase 1 and Phase 2 made ground items durable enough for
  real loot by adding uid/version identity, ownership/visibility metadata,
  reveal/despawn behavior, stack-aware spawning, and explicit pickup result
  semantics. Phase 3 connects that hardened path to combat deaths so loot is
  generated from the database instead of being hardcoded or implemented as a
  second ground-item registry.
- Exact surfaces changed:
  - Added `RcLootDrop`, `RC_MAX_LOOT_DROPS`, and `rc_roll_npc_loot` to
    `rc-core/drops.h`.
  - Updated `rc-core/drops.c` with runtime rolling on top of existing
    `RcDropTable`/`RcDropEntry` data. The roller emits guaranteed drops,
    selects one weighted main drop using `rarity_inv`, applies `qmin..qmax`
    quantity ranges, and keeps tertiary rows on the same code path for future
    data.
  - Added `rc_ground_item_spawn` in `rc-core/items.h` and `rc-core/items.c` as
    the public core spawn wrapper over the existing internal ground-item spawn
    logic. It preserves centralized ownership, private visibility,
    reveal/despawn, stackability, non-stackable splitting, and tile-cap rules.
  - Updated `rc-core/tick.c` so the alive-to-dead NPC transition spawns loot
    before firing `RC_EVT_NPC_DIED`; this makes the loot visible immediately
    after the death tick while preserving the existing death event.
  - Added `tests/test_ground_items_phase3.c` covering guaranteed drop rolling,
    seeded variable-quantity rolling, NPC death spawning private ground loot,
    and no-table NPC deaths producing no loot.
  - Updated `loot_interaction.md`, `work.md`, and `work_highlevel.md` to mark
    Phase 3 complete and set the next stop point.
- Upstream/downstream impacts: combat now has a database-backed death-loot
  path without owning item rules. Viewer rendering continues to consume the
  same `RcGroundItem` state, so existing ground model rendering remains the
  presentation path. Future banking, skilling, consumables, Telegrab, and
  item-on-ground-item work can reuse the same ground-item identity and pickup
  semantics. Rows with unparseable rarity (`rarity_inv == 0`) are intentionally
  not auto-spawned yet, and exact shared RDT/GDT/MRDT routing remains deferred
  until the DB/content layer expresses those routes more precisely.
- Verification:
  - Configure/build: `cmake -S . -B build` and targeted builds for
    `test_ground_items_phase1`, `test_ground_items_phase2`,
    `test_ground_items_phase3`, `test_drops_runtime`,
    `test_inventory_equipment_runtime`, `test_interaction_engine_phase6`,
    `test_interaction_engine_phase7`, `test_interaction_engine_phase8`, and
    `test_combat_e2e` completed successfully.
  - Targeted tests passed: `ctest --test-dir build -R
    'test_ground_items_phase1|test_ground_items_phase2|test_ground_items_phase3|test_drops_runtime|test_inventory_equipment_runtime|test_interaction_engine_phase6|test_interaction_engine_phase7|test_interaction_engine_phase8|test_combat_e2e'
    --output-on-failure` completed 9/9 passing.
  - Coverage tests passed: `ctest --test-dir build-coverage -R
    'test_ground_items_phase1|test_ground_items_phase2|test_ground_items_phase3|test_drops_runtime|test_combat_e2e'
    --output-on-failure` completed 5/5 passing.
  - Coverage review with `gcov -b -c` on changed/adjacent core files reported
    `rc-core/drops.c` at 77.25% line coverage and 84.81% branch execution,
    `rc-core/items.c` at 66.02% line coverage and 68.53% branch execution,
    `rc-core/tick.c` at 26.20% line coverage and 25.00% branch execution, and
    44.47% combined line coverage for the inspected files. The new
    `rc_roll_npc_loot`, `rc_ground_item_spawn`, and `spawn_npc_loot` paths were
    executed. `drop_roll_succeeds` remains uncovered in this pass because the
    current `drops.bin` has no tertiary rows; tertiary support is hook-ready but
    data coverage is deferred.
  - Benchmark review: a 200,000-iteration NPC death tick benchmark with loot
    disabled completed in 0.025635 seconds, or 7,801,835.56 death ticks/sec.
    The same benchmark with `RC_SUB_LOOT` enabled and Obor DB-backed loot
    spawning completed in 0.303417 seconds, or 659,158.38 death ticks/sec, with
    an average of 13.146 spawned ground item instances per kill. This records
    the Phase 3 current-performance baseline for DB-backed death loot.

## 2026-05-02 — Loot And Ground Items Phase 2 Default Take Handler

- Change made: deepened the existing default ground-item `Take` path without
  changing the public viewer-facing pickup API. Ground item pickup now has an
  explicit `rc_player_take_ground_item` helper that validates expected uid and
  version, prechecks inventory capacity, preserves the ground item on full
  inventory, removes the ground item before committing the inventory mutation,
  rolls back if insertion unexpectedly fails, and fires `RC_EVT_ITEM_PICKED_UP`
  on success.
- Why it was made: Phase 1 added ground-item identity and ownership metadata,
  but the default handler still inferred success by checking whether the item
  disappeared after calling `rc_player_pickup_item`. Phase 2 makes pickup
  result semantics explicit so later loot, Telegrab, item-on-ground-item, and
  UI systems can distinguish success, full inventory, stale target, and missing
  target behavior.
- Exact surfaces changed:
  - Added ground-item take result constants and
    `rc_player_take_ground_item` declaration in `rc-core/items.h`.
  - Updated `rc-core/items.c` with inventory precheck-on-copy, uid/version
    take validation, all-or-nothing pickup mutation, rollback safety, and
    `RC_EVT_ITEM_PICKED_UP` emission.
  - Kept `rc_player_pickup_item` as the compatibility/public entrypoint; it
    still starts off-tile pending interactions and now delegates same-tile
    pickup to the explicit take helper.
  - Updated `api_default_ground_item_handler` in `rc-core/tick.c` to call the
    take helper and map stale, full, invalid, and success outcomes to explicit
    interaction results.
  - Added `tests/test_ground_items_phase2.c` covering pickup event emission,
    stale generation rejection, full inventory preserving ground-item state,
    and default handler full-inventory failure after off-tile routing.
  - Updated `loot_interaction.md`, `work.md`, and `work_highlevel.md` with the
    Phase 2 status and next Phase 3 stop point.
- Upstream/downstream impacts: NPC death loot spawning can now rely on a
  stronger default pickup path. Future Telegrab and item-on-ground-item content
  can use the same take helper after their own projectile/content checks.
  Existing viewer calls to `rc_player_pickup_item` remain valid. There is still
  no full message/chat event type; full inventory is currently surfaced as an
  interaction failure with the `"Inventory full"` reason.
- Verification:
  - Configure/build: `cmake -S . -B build` and targeted build for
    `test_ground_items_phase1`, `test_ground_items_phase2`,
    `test_inventory_equipment_runtime`, `test_interaction_engine_phase7`, and
    `test_interaction_engine_phase8` completed successfully.
  - Targeted tests passed: `ctest --test-dir build -R
    'test_ground_items_phase1|test_ground_items_phase2|test_inventory_equipment_runtime|test_interaction_engine_phase7|test_interaction_engine_phase8'
    --output-on-failure` completed 5/5 passing.
  - Coverage tests passed: `ctest --test-dir build-coverage -R
    'test_ground_items_phase1|test_ground_items_phase2|test_inventory_equipment_runtime|test_interaction_engine_phase7|test_interaction_engine_phase8'
    --output-on-failure` completed 5/5 passing.
  - Coverage review with `gcov -b -c` on changed/adjacent core files reported
    `rc-core/items.c` at 86.61% line coverage and 93.12% branch execution,
    `rc-core/tick.c` at 52.26% line coverage and 53.58% branch execution,
    `rc-core/interaction.c` at 94.78% line coverage and 97.32% branch
    execution, `rc-core/events.c` at 65.52% line coverage and 53.85% branch
    execution, and 68.82% combined line coverage for those files. `tick.c`
    remains lower overall because it contains many unrelated runtime systems
    outside this targeted ground-item coverage pass.
  - Benchmark review: rebuilt current-source drop/pickup benchmark completed
    250,000 same-tile coin drop/pickup cycles with zero failures in 0.017923
    seconds, or 13,948,939.08 drop/pickup cycles/sec. Phase 1's current
    reference benchmark was 13,166,262.23 cycles/sec, so Phase 2 did not
    introduce a measured regression in this workload.

## 2026-05-02 — Loot And Ground Items Phase 1 Path Hardening

- Change made: hardened the existing RuneC ground-item path instead of adding
  a second registry. `RcGroundItem` now carries uid/version identity,
  owner/original-owner metadata, visibility state, reveal timer, and the
  existing despawn timer. Player drop and pickup still use the same public APIs,
  but drop spawning is now stackability-aware and pickup interactions validate
  target identity/generation as well as item id, tile, plane, active state, and
  quantity.
- Why it was made: the repo already had working drop, off-tile pickup routing,
  despawn ticking, and viewer ground-model rendering. The next loot work needs
  stronger RSPS-style stale-target, ownership, reveal/despawn, merge, and split
  behavior without duplicating the existing system or breaking current viewer
  behavior.
- Exact surfaces changed:
  - Added ground-item visibility/owner constants and new `RcGroundItem` fields
    in `rc-core/types.h`, plus `RcWorld.next_ground_item_uid` for deterministic
    per-world ground item identity.
  - Updated `rc-core/items.c` so `rc_player_drop_item` spawns through a hardened
    helper path, merges only stackable same-owner/same-visibility piles, checks
    integer overflow, splits non-stackable multi-quantity slots into one-count
    ground items, applies a deterministic per-tile cap, marks tradeable drops
    private before reveal, and preserves existing inventory mutation semantics.
  - Updated `rc-core/items.c` so `rc_player_pickup_item` rejects foreign private
    items, snapshots uid/version into pending ground-item interactions, and
    bumps version when pickup removes an item.
  - Updated `rc-core/tick.c` so pending ground-item interactions validate
    visibility, uid, and version, and so loot ticking reveals private tradeable
    items to public visibility before later despawn.
  - Updated the Phase 8 ground-item target constructor in `rc-core/tick.c` so
    item-on-ground-item and spell-on-ground-item hooks also carry uid/version.
  - Added `tests/test_ground_items_phase1.c` covering stackable merge,
    reveal/despawn, non-stackable split, stale generation cancellation, and
    foreign private pickup rejection.
  - Updated `loot_interaction.md` to record that Phase 1 extends the existing
    path and should not be implemented as a duplicate registry.
- Upstream/downstream impacts: NPC death loot spawning can now reuse the
  existing ground-item APIs with stronger identity and stack behavior. Viewer
  rendering remains driven by the existing item render map and item ground model
  data. Ownership is still single-local-player oriented and does not implement
  full multiplayer, ironman, death-pile, PvP, or LootShare rules.
- Verification:
  - Configure/build: `cmake -S . -B build` and targeted build for
    `test_ground_items_phase1`, `test_inventory_equipment_runtime`,
    `test_interaction_engine_phase7`, and `test_interaction_engine_phase8`
    completed successfully.
  - Targeted tests passed: `ctest --test-dir build -R
    'test_ground_items_phase1|test_inventory_equipment_runtime|test_interaction_engine_phase7|test_interaction_engine_phase8'
    --output-on-failure` completed 4/4 passing.
  - Coverage tests passed: `ctest --test-dir build-coverage -R
    'test_ground_items_phase1|test_inventory_equipment_runtime|test_interaction_engine_phase7|test_interaction_engine_phase8'
    --output-on-failure` completed 4/4 passing.
  - Coverage review with `gcov -b -c` on changed/adjacent core files reported
    `rc-core/items.c` at 86.86% line coverage and 92.48% branch execution,
    `rc-core/tick.c` at 52.06% line coverage and 53.36% branch execution,
    `rc-core/interaction.c` at 94.38% line coverage and 97.32% branch
    execution, and 68.45% combined line coverage for those files. `tick.c`
    remains lower overall because it contains many unrelated runtime systems
    outside this targeted ground-item coverage pass.
  - Benchmark review: current hardened drop/pickup benchmark completed 250,000
    same-tile coin drop/pickup cycles with zero failures in 0.018988 seconds,
    or 13,166,262.23 drop/pickup cycles/sec. No pre-change baseline was
    captured in this work session; the benchmark is recorded as the Phase 1
    current-performance reference.

## 2026-05-02 — Interaction Engine Phase 8 Future Hook Points

- Change made: completed Interaction Engine v1 by adding explicit
  hook-point APIs and source-aware pending interaction creation for
  inventory items, equipment items, widget actions, item-on-target, and
  spell-on-target flows.
- Why it was made: Phases 1 through 7 established pending state,
  handler dispatch, NPC/object/ground-item routing, and combat handoff.
  Later systems still needed stable entrypoints so dialogue, shops,
  banks, skilling, consumables, magic, item-use content, and widget UI
  actions can plug into the same dispatcher instead of creating new
  click engines.
- Exact surfaces changed:
  - Added dialogue/shop/bank/skilling content-group constants and
    `RcInteractionSystemHandoff` values in `rc-core/interaction.h`.
  - Added `system_handoff` and `system_target_id` fields to
    `RcInteractionHandlerResult`, plus
    `rc_interaction_result_system_handoff`, so content handlers can
    request a later system handoff without implementing that system in
    the interaction engine.
  - Added `rc_interaction_begin_with_source` in
    `rc-core/interaction.h` and `rc-core/interaction.c` so pending
    interactions can carry `source_item_id`, `source_spell_id`,
    `source_widget_id`, and `source_component_id` into dispatch keys.
  - Added public APIs in `rc-core/api.h` and implementations in
    `rc-core/tick.c` for direct inventory-item actions,
    equipment-item actions, widget actions, item-on-NPC/object/ground
    item, and spell-on-NPC/object/ground-item.
  - Extended NPC and object validation in `rc-core/tick.c` so
    `USE_ON` and `SPELL_ON` world-target interactions validate source
    item/spell ids instead of requiring a normal target option slot.
  - Extended the interaction processor in `rc-core/tick.c` so
    inventory item, equipment item, and widget targets dispatch on tick
    through the same result/failure path as world targets.
  - Added `tests/test_interaction_engine_phase8.c` covering
    inventory/equipment/widget dispatch keys, item-on-NPC source item
    dispatch, spell-on-object source spell dispatch, system handoff
    result shape, and unsupported no-handler fallback.
  - Updated `interaction_engine.md`, `work.md`, and
    `work_highlevel.md` so Phase 8 is marked implemented and
    Interaction Engine v1 is closed.
- Upstream/downstream impacts: later content can now register handlers
  for source-aware `USE_ON`, `SPELL_ON`, `WIDGET_ACTION`, inventory item,
  and equipment item dispatch keys. Unsupported actions intentionally
  fail through `RC_INTERACTION_FAIL_NO_HANDLER`. These APIs do not
  implement full dialogue, shop, bank, skilling, magic/autocast,
  consumable, or item-effect behavior; they only provide the core
  routing and dispatch hooks those systems will use.
- Verification:
  - Configure/build: `cmake -S . -B build` and targeted build for
    `test_interaction_engine_phase1`, `test_interaction_engine_phase2`,
    `test_interaction_engine_phase3`, `test_interaction_engine_phase4`,
    `test_interaction_engine_phase5`, `test_interaction_engine_phase6`,
    `test_interaction_engine_phase7`, `test_interaction_engine_phase8`,
    `test_npc_option_interactions`, `test_combat_runtime_flow`,
    `test_combat_e2e`, `test_regular_npc_mechanics_combat`, and
    `test_inventory_equipment_runtime` completed successfully.
  - Targeted tests passed: `ctest --test-dir build -R
    'test_interaction_engine_phase1|test_interaction_engine_phase2|test_interaction_engine_phase3|test_interaction_engine_phase4|test_interaction_engine_phase5|test_interaction_engine_phase6|test_interaction_engine_phase7|test_interaction_engine_phase8|test_npc_option_interactions|test_combat_runtime_flow|test_combat_e2e|test_regular_npc_mechanics_combat|test_inventory_equipment_runtime'
    --output-on-failure` completed 13/13 passing.
  - Coverage tests passed: `ctest --test-dir build-coverage -R
    'test_interaction_engine_phase1|test_interaction_engine_phase2|test_interaction_engine_phase3|test_interaction_engine_phase4|test_interaction_engine_phase5|test_interaction_engine_phase6|test_interaction_engine_phase7|test_interaction_engine_phase8|test_inventory_equipment_runtime|test_combat_runtime_flow|test_combat_e2e'
    --output-on-failure` completed 11/11 passing.
  - Coverage review with `gcov -b -c` on changed/adjacent core files
    reported `rc-core/interaction.c` at 94.38% line coverage and 97.32%
    branch execution, `rc-core/tick.c` at 68.80% line coverage and
    71.56% branch execution, `rc-core/items.c` at 84.25% line coverage
    and 88.54% branch execution, `rc-core/combat.c` at 52.37% line
    coverage and 46.97% branch execution, `rc-core/events.c` at 65.52%
    line coverage and 53.85% branch execution, and 67.91% combined line
    coverage for those changed/covered files plus inline `rng.h`.
  - Benchmark review: Phase 8 hook-dispatch benchmark completed
    1,000,000 widget-action hook dispatches with zero failures in
    0.042456 seconds, or 23,553,920.59 hook dispatches/sec. There is no
    meaningful Phase 7 before/after baseline for these new hook APIs
    because Phase 7 did not expose inventory/equipment/widget,
    item-on-target, spell-on-target, or system-handoff entrypoints.

## 2026-05-02 — Interaction Engine Phase 7 Object And Ground-Item Processor

- Change made: extended the shared interaction processor to object and
  ground-item targets. Object interactions with world coordinates now
  create pending state and execute only after route/range/facing/handler
  dispatch. Ground-item pickup now creates a pending `Take` interaction
  when the player is not already on the item tile, then the default
  ground-item handler performs the existing inventory pickup once the
  player arrives.
- Why it was made: Phases 1 through 6 made NPC intent, route, facing,
  attack handoff, and combat run through the interaction architecture.
  Objects and ground items still had bespoke immediate behavior. Phase 7
  brings those world interactions under the same processor so doors,
  banks, skilling objects, traversal objects, and item piles can share
  target validation, route/approach, facing, stale-target handling, and
  handler dispatch.
- Exact surfaces changed:
  - Updated `rc-core/tick.c` with static-target distance, facing, and
    route helpers for object and ground-item footprints.
  - Added object validation for definition/action availability,
    depleted object state, active transform/version mismatch, and
    footprint refresh.
  - Added ground-item validation for instance index, active state,
    item-id/version match, tile/plane match, quantity, and footprint.
  - Extended `process_player_interaction` in `rc-core/tick.c` so NPC,
    object, and ground-item interactions all route/fail/face/dispatch
    through one path.
  - Split object behavior execution into `api_apply_object_interaction`
    and wired it through a default object handler. Direct object calls
    without coordinates keep the previous immediate fallback, while
    coordinate-backed object clicks defer behavior until the interaction
    processor dispatches.
  - Added default object handlers for OP1 through OP5 and a default
    ground-item OP1 `Take` handler.
  - Updated `rc-core/items.c` so `rc_player_pickup_item` starts a
    pending ground-item `Take` interaction when the player is not on the
    item tile, while preserving immediate pickup when already standing
    on the tile.
  - Added `tests/test_interaction_engine_phase7.c` covering object
    route/facing/custom handler dispatch, default object execution after
    arrival, ground-item route/facing/take pickup, stale ground-item
    cancellation, and direct object no-handler fallback.
  - Updated `interaction_engine.md`, `work.md`, and
    `work_highlevel.md` so Phase 7 is marked implemented and Phase 8 is
    the next interaction-engine step.
- Upstream/downstream impacts: coordinate-backed object interaction
  behavior is now tick-time rather than immediate, matching the NPC
  interaction model. Tests or callers that need the resulting object
  behavior must tick the world after issuing the intent. Existing direct
  ground-item pickup on the same tile remains immediate, but off-tile
  pickup now routes through pending interaction state. Full bank UI,
  skilling timers, loot ownership, pile ordering, object rotation/type
  reachability, and exact OSRS route/LOS parity remain later work.
- Verification:
  - Configure/build: `cmake -S . -B build` and targeted build for
    `test_interaction_engine_phase1`, `test_interaction_engine_phase2`,
    `test_interaction_engine_phase3`, `test_interaction_engine_phase4`,
    `test_interaction_engine_phase5`, `test_interaction_engine_phase6`,
    `test_interaction_engine_phase7`, `test_npc_option_interactions`,
    `test_combat_runtime_flow`, `test_combat_e2e`,
    `test_regular_npc_mechanics_combat`, and
    `test_inventory_equipment_runtime` completed successfully.
  - Targeted tests passed: `ctest --test-dir build -R
    'test_interaction_engine_phase1|test_interaction_engine_phase2|test_interaction_engine_phase3|test_interaction_engine_phase4|test_interaction_engine_phase5|test_interaction_engine_phase6|test_interaction_engine_phase7|test_npc_option_interactions|test_combat_runtime_flow|test_combat_e2e|test_regular_npc_mechanics_combat|test_inventory_equipment_runtime'
    --output-on-failure` completed 12/12 passing.
  - Coverage tests passed: `ctest --test-dir build-coverage -R
    'test_interaction_engine_phase1|test_interaction_engine_phase2|test_interaction_engine_phase3|test_interaction_engine_phase4|test_interaction_engine_phase5|test_interaction_engine_phase6|test_interaction_engine_phase7|test_inventory_equipment_runtime|test_combat_runtime_flow|test_combat_e2e'
    --output-on-failure` completed 10/10 passing.
  - Coverage review with `gcov -b -c` on changed/adjacent core files
    reported `rc-core/interaction.c` at 92.41% line coverage and 94.44%
    branch execution, `rc-core/tick.c` at 70.33% line coverage and
    74.37% branch execution, `rc-core/items.c` at 84.25% line coverage
    and 88.54% branch execution, `rc-core/combat.c` at 52.37% line
    coverage and 46.97% branch execution, `rc-core/events.c` at 65.52%
    line coverage and 53.85% branch execution, and 67.99% combined line
    coverage for those changed/covered files plus inline `rng.h`.
  - Benchmark review: adjacent coordinate-backed object interaction
    benchmark completed 100,000 iterations with zero failures. Phase 7
    measured 0.011185 seconds, or 8,940,693 object interactions/sec. The
    Phase 6 baseline at commit `79c5f51` measured 0.004271 seconds, or
    23,413,369 object interactions/sec. The slowdown is expected because
    coordinate-backed object interactions now create pending state and
    dispatch on tick instead of executing immediately. Off-tile
    ground-item routing has no semantic Phase 6 baseline because Phase 6
    could only pick up items when already standing on the item tile.

## 2026-05-02 — Interaction Engine Phase 6 Combat Vertical Slice

- Change made: connected NPC `Attack` handoff to a fuller core combat
  vertical slice. Combat now carries an NPC definition-id target
  snapshot alongside the target uid, refreshes player-facing state while
  attacking, sets NPC-facing state during retaliation, emits a
  player-attack event, and includes current/max HP in player/NPC damage
  event payloads so viewer hitsplats and HP bars can be driven from core
  events.
- Why it was made: Phases 4 and 5 made interaction own click intent,
  approach, facing, validation, and attack handoff. Phase 6 moves the
  repeated fight behavior behind that handoff so the viewer remains an
  input/presentation layer and core owns combat target validation,
  weapon timing, delayed hits, retaliation, death, and respawn
  stability.
- Exact surfaces changed:
  - Added `attack_target_def_id` to `RcPlayer` in `rc-core/types.h` and
    initialized/cleared it in `rc-core/world.c`, `rc-core/tick.c`, and
    core combat target validation.
  - Added `RC_EVT_PLAYER_ATTACK` and `RcPayloadPlayerAttack` in
    `rc-core/events.h`.
  - Extended `RcPayloadNpcDamaged` and `RcPayloadPlayerDamaged` with
    `current_hp` and `max_hp` fields for HP-bar-capable damage events.
  - Updated `rc-core/combat.c` so player combat validates the target uid
    plus definition snapshot, faces the NPC each combat tick, emits
    `RC_EVT_PLAYER_ATTACK` when a swing queues a hit, and keeps weapon
    speed/range/style sourced from current equipment/style runtime.
  - Updated NPC combat in `rc-core/combat.c` so retaliating NPCs face
    the player while engaging before their existing attack animation,
    queued hit, and `RC_EVT_NPC_ATTACK` emission.
  - Updated NPC damage resolution in `rc-core/tick.c` so death clears
    both player target uid and target definition snapshot, and damage
    events carry current/max NPC HP.
  - Added `tests/test_interaction_engine_phase6.c` covering
    interaction-path attack entry, target lock snapshot, player facing,
    weapon-speed attack cycle timing, player attack event emission,
    delayed magic hit queueing, NPC retaliation events, player damage
    events, NPC facing, NPC death target cleanup, and respawn stability.
  - Updated `interaction_engine.md`, `work.md`, and
    `work_highlevel.md` so Phase 6 is marked implemented and Phase 7 is
    the next interaction-engine step.
- Upstream/downstream impacts: damage event payloads are extended but
  remain source-compatible with existing positional/named initializers.
  Viewer systems can now consume `RC_EVT_PLAYER_ATTACK`,
  `RC_EVT_NPC_ATTACK`, `RC_EVT_NPC_DAMAGED`, and
  `RC_EVT_PLAYER_DAMAGED` as the core source for attack animations,
  hitsplats, and HP bars. Combat still does not implement exact OSRS
  attack animation IDs, projectile visuals, special attacks, ammo/rune
  consumption, full single/multi combat rules, loot-table execution, or
  boss-specific parity; those remain later combat/loot/encounter work.
- Verification:
  - Configure/build: `cmake -S . -B build` and targeted build for
    `test_interaction_engine_phase1`, `test_interaction_engine_phase2`,
    `test_interaction_engine_phase3`, `test_interaction_engine_phase4`,
    `test_interaction_engine_phase5`, `test_interaction_engine_phase6`,
    `test_npc_option_interactions`, `test_combat_runtime_flow`,
    `test_combat_e2e`, `test_regular_npc_mechanics_combat`, and
    `test_inventory_equipment_runtime` completed successfully.
  - Targeted tests passed: `ctest --test-dir build -R
    'test_interaction_engine_phase1|test_interaction_engine_phase2|test_interaction_engine_phase3|test_interaction_engine_phase4|test_interaction_engine_phase5|test_interaction_engine_phase6|test_npc_option_interactions|test_combat_runtime_flow|test_combat_e2e|test_regular_npc_mechanics_combat|test_inventory_equipment_runtime'
    --output-on-failure` completed 11/11 passing.
  - Coverage tests passed: `ctest --test-dir build-coverage -R
    'test_interaction_engine_phase1|test_interaction_engine_phase2|test_interaction_engine_phase3|test_interaction_engine_phase4|test_interaction_engine_phase5|test_interaction_engine_phase6|test_combat_runtime_flow|test_combat_e2e'
    --output-on-failure` completed 8/8 passing.
  - Coverage review with `gcov -b -c` on changed core files reported
    `rc-core/interaction.c` at 90.72% line coverage and 91.67% branch
    execution, `rc-core/tick.c` at 65.17% line coverage and 66.78%
    branch execution, `rc-core/combat.c` at 51.08% line coverage and
    45.60% branch execution, `rc-core/events.c` at 65.52% line coverage
    and 53.85% branch execution, and 61.39% combined line coverage for
    those changed/covered files plus inline `rng.h`.
  - Benchmark review: synthetic one-tick interaction-to-combat entry
    benchmark with a pinned NPC and data-backed `Attack` option
    completed 100,000 combat entries with zero failures. Phase 6
    measured 0.034273 seconds, or 2,917,781 combat entries/sec. The
    Phase 5 baseline at commit `2c38b57` measured 0.032369 seconds, or
    3,089,411 combat entries/sec. This is a small measured slowdown from
    target definition snapshot validation, combat-facing updates, and
    player-attack event emission.

## 2026-05-02 — Interaction Engine Phase 5 NPC Attack Handler Boundary

- Change made: split NPC `Attack` from the generic default NPC option
  handler. Attack pending interactions are now tagged with
  `RC_INTERACTION_CONTENT_GROUP_NPC_ATTACK`, dispatched through a
  dedicated registered attack handler, and rejected if the NPC
  definition does not expose a real data-backed `Attack` option.
- Why it was made: Phase 4 routed NPC interactions through tick-time
  validation and dispatch, but the default NPC handler still contained a
  mixed branch that treated any matching option or fabricated direct
  attack intent as combat handoff. Phase 5 makes attack a normal
  content-handler boundary so viewer/API input emits intent only,
  handler dispatch decides attack versus noncombat option behavior, and
  the combat loop remains responsible for repeated attacks after the
  handoff.
- Exact surfaces changed:
  - Added `RC_INTERACTION_CONTENT_GROUP_NPC_ATTACK` in
    `rc-core/interaction.h` for attack-specific dispatch keys.
  - Updated `rc-core/tick.c` so NPC interaction validation requires the
    selected option to exist in the NPC definition, stamps attack
    options with the attack content group, and rejects attack-group
    pending intents when the selected option is not data-backed Attack.
  - Split `api_default_npc_attack_handler` from
    `api_default_npc_option_handler`. The attack handler starts or
    refreshes combat state by setting `attack_target`, legacy
    interaction compatibility fields, NPC retaliation target, and combat
    style refresh before returning a combat handoff result. The generic
    handler now handles noncombat NPC options only.
  - Updated default NPC handler registration so each NPC option op gets
    both a specific attack-group handler and a fallback generic option
    handler. Exact/content-specific handlers still override defaults via
    normal dispatch-key specificity.
  - Updated `rc_player_attack_npc` to find the NPC's actual data-backed
    attack option slot instead of assuming OP2 or fabricating `Attack`
    when the definition lacks that option.
  - Added `tests/test_interaction_engine_phase5.c` covering attack
    content-group priority over a generic option handler, default combat
    handoff, repeated attack refresh, and failure for NPCs without an
    attack option.
  - Updated `tests/test_combat_runtime_flow.c` so its synthetic combat
    NPC exposes the required `Attack` option.
  - Updated `interaction_engine.md`, `work.md`, and
    `work_highlevel.md` so Phase 5 is marked implemented and Phase 6 is
    the next interaction-engine step.
- Upstream/downstream impacts: direct `rc_player_attack_npc` calls now
  require NPC definition menu data to include an `Attack` option. This
  is stricter and closer to OSRS menu semantics, but tests or synthetic
  content that previously relied on fabricated attacks must add the
  option to their test NPC definition. Viewer code remains unchanged:
  it can request an attack intent, but core validates whether that
  target is actually attackable. Phase 6 still owns deeper combat
  parity, animation/event timing, repeated attack behavior, hitsplats,
  retaliation depth, death, and respawn polish.
- Verification:
  - Configure/build: `cmake -S . -B build` and targeted build for
    `test_interaction_engine_phase1`, `test_interaction_engine_phase2`,
    `test_interaction_engine_phase3`, `test_interaction_engine_phase4`,
    `test_interaction_engine_phase5`, `test_npc_option_interactions`,
    `test_combat_runtime_flow`, and `test_inventory_equipment_runtime`
    completed successfully.
  - Targeted tests passed: `ctest --test-dir build -R
    'test_interaction_engine_phase1|test_interaction_engine_phase2|test_interaction_engine_phase3|test_interaction_engine_phase4|test_interaction_engine_phase5|test_npc_option_interactions|test_combat_runtime_flow|test_inventory_equipment_runtime'
    --output-on-failure` completed 8/8 passing.
  - Coverage tests passed: `ctest --test-dir build-coverage -R
    'test_interaction_engine_phase1|test_interaction_engine_phase2|test_interaction_engine_phase3|test_interaction_engine_phase4|test_interaction_engine_phase5'
    --output-on-failure` completed 5/5 passing.
  - Coverage review with `gcov -b -c` on changed core files reported
    `rc-core/interaction.c` at 90.72% line coverage and 91.67% branch
    execution, `rc-core/tick.c` at 60.56% line coverage and 64.44%
    branch execution, and 69.38% combined line coverage for those two
    files.
  - Benchmark review: synthetic one-tick NPC attack handoff benchmark
    with a pinned NPC and data-backed `Attack` option completed 100,000
    handoffs with zero failures. Phase 5 measured 0.042118 seconds, or
    2,374,277 handoffs/sec. The Phase 4 baseline at commit `e0aaafd`
    measured 0.023891 seconds, or 4,185,695 handoffs/sec. This is a
    measured slowdown from stricter attack-option lookup plus
    attack-specific content-group dispatch, but the absolute cost remains
    small relative to expected sim tick workloads.

## 2026-05-02 — Interaction Engine Phase 4 NPC Route And Validation Processor

- Change made: added the first shared NPC interaction processor inside
  `rc_world_tick`. Active NPC pending interactions now validate the live
  target, refresh target tile/footprint snapshots, route the player
  toward the target footprint, check range and LOS, set player facing,
  and dispatch the registered handler once the player is in range.
- Why it was made: after Phase 3, NPC clicks created pending state and
  dispatched through handlers immediately. The reference architecture
  requires durable interaction state to survive movement and only fire
  once the actor has reached a valid target. Phase 4 moves NPC
  interaction execution into tick processing so later content actions,
  combat, dialogue, banks, shops, skilling, and object interactions can
  share one route/approach/validation path.
- Exact surfaces changed:
  - Extended `RcPlayer` in `rc-core/types.h` with `facing_entity`,
    `facing_x`, and `facing_y` so the interaction processor can expose
    target-facing state to downstream systems/viewer presentation.
  - Updated `rc-core/world.c` defaults so player facing state starts
    inactive.
  - Updated `rc-core/tick.c` with NPC footprint distance helpers,
    live target validation, option revalidation, LOS check, route
    planning, facing update, interaction-result completion/cancel
    handling, and tick-time dispatch.
  - Changed `rc_player_interact_npc` and `rc_player_attack_npc` timing:
    they now create pending NPC interaction state only. Execution is
    performed by `rc_world_tick` once the player is validly in range.
  - Updated `tests/test_npc_option_interactions.c` and
    `tests/test_interaction_engine_phase3.c` for the new tick-time
    dispatch timing.
  - Added `tests/test_interaction_engine_phase4.c` covering route to a
    distant NPC, noncombat dispatch after arrival, player facing state,
    attack dispatch in range, and stale/missing target cancellation.
  - Updated `interaction_engine.md`, `work.md`, and
    `work_highlevel.md` so Phase 4 is marked implemented and Phase 5 is
    the next interaction-engine step.
- Upstream/downstream impacts: NPC options are no longer executed at
  click time. Any caller that needs the resulting legacy interaction or
  combat state must tick the world. This is intentional and matches the
  planned processor model. Viewer input remains a thin intent emitter,
  while core owns target validity, route/approach, facing, and dispatch
  timing. Object, ground-item, inventory-item, spell-on, widget, and
  full OP/AP custom approach behavior remain later phases.
- Verification:
  - Configure/build: `cmake -S . -B build`, `cmake -S . -B build-coverage
    -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_FLAGS=--coverage
    -DCMAKE_EXE_LINKER_FLAGS=--coverage`, targeted normal build for
    interaction/NPC/combat regression tests, and targeted coverage build
    for interaction tests.
  - Targeted tests passed: `ctest --test-dir build -R
    'test_interaction_engine_phase1|test_interaction_engine_phase2|test_interaction_engine_phase3|test_interaction_engine_phase4|test_npc_option_interactions|test_combat_runtime_flow|test_inventory_equipment_runtime'
    --output-on-failure` completed 7/7 passing.
  - Coverage tests passed: `ctest --test-dir build-coverage -R
    'test_interaction_engine_phase1|test_interaction_engine_phase2|test_interaction_engine_phase3|test_interaction_engine_phase4'
    --output-on-failure` completed 4/4 passing.
  - Coverage review with `gcov -b -c` on changed core files reported
    `rc-core/interaction.c` at 90.72% line coverage and 91.67% branch
    execution, `rc-core/tick.c` at 58.96% line coverage and 61.65%
    branch execution, and 68.64% combined line coverage for those two
    files.
  - Benchmark review: Phase 4 routed NPC interaction benchmark completed
    20,000 route/approach/dispatch interactions in 0.687473 seconds
    with zero failures, or 29,092 interactions/sec. A direct Phase 3
    route/approach baseline is not semantically comparable because
    Phase 3 had no tick-time processor and leaves the pending interaction
    active. Intent-only microbenchmark for 1,000,000 NPC click intents
    measured 23,811,686 intents/sec on Phase 4 versus 11,206,863
    intents/sec on Phase 3.

## 2026-05-02 — Interaction Engine Phase 3 NPC Intent Migration

- Change made: migrated NPC option execution onto the Interaction
  Engine pending-state plus handler-registry path. `rc_player_interact_npc`
  now validates the NPC/option, creates a typed pending interaction,
  lazily registers default NPC handlers, and dispatches through
  `rc_interaction_dispatch`. `rc_player_attack_npc` now uses the same
  pending interaction and dispatch path.
- Why it was made: NPC clicks were still executing through direct
  special-case branches even after Phases 1 and 2 introduced typed
  pending state and a handler registry. Phase 3 makes NPC intent follow
  the same architecture as the reference repos: click/menu intent
  becomes durable interaction state and then routes through a handler
  lookup before any gameplay behavior is applied.
- Exact surfaces changed:
  - Updated `rc-core/tick.c` with default NPC option handlers for
    generic noncombat options and `Attack`. These preserve existing
    legacy behavior by setting `interact_type`, `interact_target`,
    `interact_option`, `attack_target`, NPC retaliation target, and
    combat style refresh where applicable.
  - Updated `rc_player_interact_npc` to remove direct option-specific
    execution branches and dispatch pending NPC options through the
    handler registry.
  - Updated `rc_player_attack_npc` to create an `Attack` pending
    interaction and dispatch through the same default NPC handler path.
  - Added `tests/test_interaction_engine_phase3.c` covering exact
    handler override, default noncombat option behavior, default attack
    behavior, direct attack API behavior, missing-option no-op, and
    deterministic no-handler failure when dispatch is called without
    registered handlers.
  - Updated `interaction_engine.md`, `work.md`, and
    `work_highlevel.md` to mark Phase 3 as implemented and Phase 4 as
    the next interaction-engine step.
- Upstream/downstream impacts: NPC click intent now has the correct
  registry-backed seam for `rc-content` to override exact NPC behavior
  without viewer special cases. Existing viewer/API behavior is
  preserved for current noncombat and attack flows. Movement,
  reachability, live target revalidation, facing, and OP/AP timing are
  still not handled by the interaction processor; those remain Phase 4.
- Verification:
  - Configured normal build with `cmake -S . -B build`.
  - Built targeted normal-test binaries:
    `test_interaction_engine_phase1`,
    `test_interaction_engine_phase2`,
    `test_interaction_engine_phase3`, `test_npc_option_interactions`,
    `test_combat_runtime_flow`, and
    `test_inventory_equipment_runtime`.
  - Ran targeted normal regression tests with
    `ctest --test-dir build -R
    'test_interaction_engine_phase1|test_interaction_engine_phase2|test_interaction_engine_phase3|test_npc_option_interactions|test_combat_runtime_flow|test_inventory_equipment_runtime'
    --output-on-failure`; result: `6/6` passed.
  - Configured coverage build with `cmake -S . -B build-coverage
    -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_FLAGS=--coverage
    -DCMAKE_EXE_LINKER_FLAGS=--coverage`.
  - Built coverage-test binaries:
    `test_interaction_engine_phase1`,
    `test_interaction_engine_phase2`, and
    `test_interaction_engine_phase3`.
  - Ran coverage tests with `ctest --test-dir build-coverage -R
    'test_interaction_engine_phase1|test_interaction_engine_phase2|test_interaction_engine_phase3'
    --output-on-failure`; result: `3/3` passed.
  - Ran `gcov -b -c` over the coverage objects for
    `rc-core/interaction.c` and `rc-core/tick.c`. Results:
    `interaction.c` line coverage `90.72%`, branch execution
    `91.67%`; `tick.c` line coverage `32.01%`, branch execution
    `35.75%`.
  - `tick.c` coverage is low because the file contains broad
    pre-existing tick, movement, skilling, object, respawn, and combat
    paths outside Phase 3. The Phase 3-specific NPC API paths are
    covered by `test_interaction_engine_phase3` and the existing NPC
    option/combat regression tests.
  - Benchmark not run: Phase 3 adds dispatch to explicit NPC API calls,
    not to the per-tick processor. There is no material sim-throughput
    path to benchmark until Phase 4 introduces per-tick interaction
    route/approach/validation processing.

## 2026-05-02 — Interaction Engine Phase 2 Handler Registry

- Change made: added the structural handler-registration and
  dispatch-key layer for Interaction Engine v1. Core can now register,
  replace, look up, and explicitly dispatch handlers by target kind,
  interaction op, exact definition id, content group/tag, source item,
  source spell, widget id, and component id.
- Why it was made: Phase 1 created durable pending interaction state,
  but later phases need content-facing dispatch before NPC, object,
  ground-item, inventory, widget, skilling, bank, shop, dialogue, and
  combat actions can move out of viewer/direct API special cases.
- Exact surfaces changed:
  - Extended `RcPendingInteraction` in `rc-core/types.h` with source
    item/spell/widget/component ids so item-on, spell-on, and widget
    dispatch keys have stable source fields.
  - Extended `rc-core/interaction.h` with
    `RcInteractionDispatchKey`, `RcInteractionHandlerResult`,
    `RcInteractionHandlerCode`, `RcInteractionHandlerFn`, result
    constructors, registry APIs, lookup, and explicit dispatch.
  - Extended `rc-core/interaction.c` with a fixed-size handler registry
    (`RC_MAX_INTERACTION_HANDLERS`), wildcard dispatch keys,
    exact-definition/group/fallback specificity ordering, duplicate-key
    replacement, deterministic no-handler failure, and result
    propagation.
  - Added `tests/test_interaction_engine_phase2.c` covering exact-id,
    content-group, fallback lookup priority, duplicate replacement,
    combat-handoff result propagation, no-handler failure, invalid
    registration, and inactive-dispatch failure.
  - Updated `interaction_engine.md`, `work.md`, and
    `work_highlevel.md` so Phase 2 is marked structurally implemented
    and Phase 3 is the next interaction-engine step.
- Upstream/downstream impacts: `rc-content` can now register handlers
  against stable core keys, but no content handlers are registered yet.
  Existing gameplay behavior is unchanged because the tick processor
  does not call handler dispatch automatically until Phase 3/4. Viewer
  code still emits the same API calls; Phase 3 should migrate NPC
  click/menu intent to the registry-backed path.
- Verification:
  - Configured normal build with `cmake -S . -B build`.
  - Built targeted normal-test binaries:
    `test_interaction_engine_phase1`,
    `test_interaction_engine_phase2`, `test_npc_option_interactions`,
    `test_combat_runtime_flow`, and
    `test_inventory_equipment_runtime`.
  - Ran targeted normal regression tests with
    `ctest --test-dir build -R
    'test_interaction_engine_phase1|test_interaction_engine_phase2|test_npc_option_interactions|test_combat_runtime_flow|test_inventory_equipment_runtime'
    --output-on-failure`; result: `5/5` passed.
  - Configured coverage build with `cmake -S . -B build-coverage
    -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_FLAGS=--coverage
    -DCMAKE_EXE_LINKER_FLAGS=--coverage`.
  - Built coverage-test binaries:
    `test_interaction_engine_phase1` and
    `test_interaction_engine_phase2`.
  - Ran coverage tests with `ctest --test-dir build-coverage -R
    'test_interaction_engine_phase1|test_interaction_engine_phase2'
    --output-on-failure`; result: `2/2` passed.
  - Ran `gcov -b -c` over the coverage object for
    `rc-core/interaction.c`. Result: line coverage `89.45%`, branch
    execution `91.67%`.
  - Coverage gaps are mostly defensive branches for invalid/null input,
    source-item/source-spell/widget wildcard combinations not yet used
    by runtime callers, and registry-capacity failure.
  - Benchmark not run: Phase 2 adds explicit registration/lookup/
    dispatch APIs, but no per-tick interaction processor and no route
    loop. Benchmarking becomes material once Phase 4 runs interaction
    processing inside `rc_world_tick`.

## 2026-05-02 — Interaction Engine Phase 1 Core Model

- Change made: added the first structural Interaction Engine layer in
  `rc-core`: interaction target kinds, option/op codes, failure reasons,
  lifecycle flags, target snapshots, durable pending interaction state
  on the player, and public begin/cancel/clear/query APIs.
- Why it was made: the existing runtime used narrow legacy fields such
  as `interact_type`, `interact_target`, and direct combat/object API
  paths. Later phases need one durable, typed interaction state that can
  represent NPCs, objects, ground items, inventory items, equipment
  items, players, and widgets before route processing, OP/AP dispatch,
  content handlers, or combat handoff run.
- Exact surfaces changed:
  - Added `rc-core/interaction.h` and `rc-core/interaction.c`.
  - Extended `rc-core/types.h` with `RcInteractionKind`,
    `RcInteractionOp`, `RcInteractionFailure`,
    `RcInteractionTarget`, and `RcPendingInteraction`.
  - Added `RcPendingInteraction interaction` to `RcPlayer` while
    retaining the legacy interaction fields for compatibility during
    migration.
  - Updated `rc-core/api.h` to expose the interaction API through the
    public core include surface.
  - Updated `rc-core/world.c` player defaults so legacy interaction
    targets, storage targets, skilling target tiles, and pending
    interaction state start from deterministic inactive values.
  - Updated `rc-core/tick.c` so existing NPC entrypoints and
    coordinate-backed object entrypoints populate pending interaction
    state after current option/action validation; legacy ID-only object
    calls keep their existing behavior until later route/target
    migration phases can give them durable target coordinates.
    Walking/running cancels active pending interaction state.
  - Added `tests/test_interaction_engine_phase1.c` covering enum
    conversion, target structural validation, begin/replacement,
    cancel/clear lifecycle, existing NPC API pending-state
    population, and coordinate-backed object API pending-state
    population.
  - Updated `interaction_engine.md`, `work.md`, and
    `work_highlevel.md` to mark Phase 1 as structurally implemented
    and Phase 2 as the next interaction-engine step.
- Upstream/downstream impacts: `rc-viewer` can continue using the
  existing API surface for now. NPC calls and object calls that include
  tile coordinates now leave a typed pending interaction snapshot for
  later processors; legacy object-by-id calls intentionally remain
  behavior-compatible and may not create a structural pending object
  target until Phase 7 migrates object routing. `rc-content` still has
  no handler registry; Phase 2 must add dispatch keys and handler
  lookup before content-specific actions move out of legacy paths.
  Combat behavior is intentionally unchanged except that NPC attack
  entrypoints now also record the originating interaction.
- Verification:
  - Configured normal build with `cmake -S . -B build`.
  - Built targeted normal-test binaries:
    `test_interaction_engine_phase1`, `test_npc_option_interactions`,
    `test_combat_runtime_flow`, `test_inventory_equipment_runtime`,
    `test_shops_storage_runtime`, `test_skills_runtime`, and
    `test_traversal_runtime`.
  - Ran targeted normal regression tests with
    `ctest --test-dir build -R
    'test_interaction_engine_phase1|test_npc_option_interactions|test_combat_runtime_flow|test_inventory_equipment_runtime|test_shops_storage_runtime|test_skills_runtime|test_traversal_runtime'
    --output-on-failure`; result: `7/7` passed.
  - Configured coverage build with `cmake -S . -B build-coverage
    -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_FLAGS=--coverage
    -DCMAKE_EXE_LINKER_FLAGS=--coverage`.
  - Ran coverage regression tests with `ctest --test-dir
    build-coverage -R
    'test_interaction_engine_phase1|test_combat_runtime_flow|test_npc_option_interactions|test_shops_storage_runtime|test_skills_runtime|test_traversal_runtime'
    --output-on-failure`; result: `6/6` passed.
  - Ran `gcov -b -c` over the coverage objects for
    `rc-core/interaction.c` and `rc-core/tick.c`. Results:
    `interaction.c` line coverage `82.14%`, branch execution
    `82.69%`; `tick.c` line coverage `55.00%`, branch execution
    `51.71%`.
  - Coverage gaps are intentional for Phase 1: several structural
    target kinds (`GROUND_ITEM`, `EQUIPMENT_ITEM`, `PLAYER`) have model
    validation but no executing runtime path yet; broad `tick.c` misses
    are mostly pre-existing movement, skilling, object-state, and
    respawn paths outside this phase.
  - Benchmark not run: Phase 1 adds no interaction processor to
    `rc_world_tick` and no route/handler loop. The changed code runs on
    explicit API calls (`begin`, `cancel`, NPC/object selection,
    walking/running cancellation), so there is no material sim-throughput
    path to benchmark until Phase 4 introduces per-tick interaction
    processing.

## 2026-05-02 — Split Runtime Database Into RuneC-DB Repo

- Change made: established `data/` as a separate local Git repository
  backed by `https://github.com/jordanbailey00/RuneC-DB`, added DB-side
  Git LFS attributes for large binary artifacts, and updated the main
  RuneC ignore and agent boundary rules so `data/` contents are no
  longer committed with RuneC code.
- Why it was made: database artifacts, generated assets, curated DB
  inputs, and raw source corpora had grown into a large dataset that
  should be versioned independently from RuneC runtime/tool code. This
  keeps normal RuneC commits focused on code, tests, tools, docs, and
  architecture changes while allowing the full DB to evolve in its own
  repository.
- Exact surfaces changed:
  - Initialized `data/` as the local working tree for `RuneC-DB`.
  - Added `data/.gitattributes` with Git LFS rules for generated binary
    model, region, cache, and runtime-data files.
  - Added `data/README.md` documenting DB repo ownership.
  - Updated `.gitignore` so the main RuneC repo ignores `data/`.
  - Updated `AGENT.md` and `AGENT_README.md` with the new repo boundary.
- Upstream/downstream impacts: RuneC runtime loaders still read from
  the same local `data/` path, so code paths do not change. Developers
  must commit generated DB changes from inside `data/` and commit RuneC
  code changes from the parent repo. Clones that need the full DB must
  fetch `RuneC-DB` separately into `data/`.
- Verification:
  - `RuneC-DB` local repo was committed and pushed to GitHub on branch
    `main`; final DB commit is `f14715e`.
  - DB push uploaded `82/82` Git LFS objects, about `1.5 GB`.
  - GitHub accepted the push with a warning that
    `source/data_osrs/lofts.json` is `68.41 MB`, above GitHub's
    recommended `50 MB` file size but below the hard `100 MB` limit.
  - Main RuneC repo committed the index split on branch `testing_2` as
    `f2a7787`.
  - `git -C data status --short` was clean after push.
  - `git status --short` in the parent repo shows only the pre-existing
    runtime/code/test changes; no `data/` files remain tracked or
    pending in the parent repo.

## 2026-05-02 — Restored Opportunistic Future-Work Backlog

- Change made: restored planned lower-priority work that was
  over-collapsed during the roadmap cleanup into a dedicated
  opportunistic backlog at the bottom of `work.md`, and added a concise
  high-level pointer in `work_highlevel.md`.
- Why it was made: the previous cleanup correctly removed completed
  historical detail, but it also removed future work that should remain
  visible, including renderer scalability, render distance, world
  streaming, region/area work, run energy, quests, spawn overrides,
  noted item behavior, long-range pathfinding, and related data/runtime
  parity follow-ups. The viewer's current multilevel/multiplane
  rendering limitation also needed to be explicitly captured.
- Exact surfaces changed:
  - Added `work.md` section `Opportunistic Backlog: Do When We Get the
    Chance`.
  - Added future-work bullets to `work_highlevel.md` for world
    streaming, render distance, renderer scalability, multiplane
    rendering, plane-aware interactions, longer-range pathfinding, run
    energy, noted items, spawn overrides, and quests.
  - Updated `changelog.md`.
- Upstream/downstream impacts: no runtime code, generated data, tools,
  tests, or binaries changed. The active next sequence remains
  Interaction Engine v1; the restored backlog is intentionally
  lower-priority planned work.
- Verification: not run; this was a documentation-only correction.

## 2026-05-02 — Roadmap Cleanup and Interaction Engine Plan

- Change made: consolidated planning docs around the next active
  milestone, Interaction Engine v1. `work.md` now tracks active and
  future work only, while completed implementation history remains in
  dated changelog entries. `work_highlevel.md` is now a short status
  map rather than a historical ledger.
- Why it was made: `work.md` had accumulated completed encounter,
  database, UI, inventory/equipment, rendering, and combat details that
  made the next step hard to see. The project now needs a concise
  roadmap showing what is next, what follows, what is deferred, and
  what must be revisited.
- Exact surfaces changed:
  - Replaced `work.md` with an active roadmap centered on Interaction
    Engine v1, followed by loot, banking, consumables, skilling, combat
    parity, UI follow-up, encounter follow-up, and accepted source debt.
  - Replaced `work_highlevel.md` with a concise current-position and
    next-sequence summary.
  - Updated `AGENT.md` to clarify documentation read discipline:
    always read `AGENT.md`, do not read every Markdown file on every
    prompt, and read `AGENT_README.md` only when needed or prompted.
  - Updated `AGENT_README.md` to define it as the longer repo wiki /
    architecture reference, not the always-read agent instruction file.
- Upstream/downstream impacts: no runtime code, generated data, tools,
  tests, or binaries changed. Future agents should use `work.md` for
  current sequencing, `work_highlevel.md` for quick orientation,
  `changelog.md` for history, `AGENT.md` for always-on rules, and
  `AGENT_README.md` for deeper repo architecture context only when
  needed.
- Verification: not run; this was a documentation-only cleanup.

## Entry Template

Each changelog entry should be concise but technically complete:
- Change made: what was added, updated, removed, regenerated, or
  decided.
- Why it was made: the bug, parity gap, architecture rule, or source
  authority that required the change.
- Exact surfaces changed: files, generated data, reports, docs, tests,
  and any directly impacted runtime/export behavior.
- Upstream/downstream impacts: source inputs affected, generated
  outputs affected, runtime/API compatibility implications, and other
  systems that now depend on or must account for the change.
- Verification: commands/checks run, result, and any known unrelated
  failure observed during verification.

## 2026-05-02 — Data-Backed NPC Right-Click Option Menu

- Change made: implemented the first OSRS-style NPC interaction menu
  path. NPC definitions now carry the five cache action slots from the
  local `data_osrs` mirror, `rc_player_interact_npc` resolves selected
  option indexes against those slots, and the viewer opens the existing
  cache-sprite context menu on right-clicked NPCs. Selecting `Attack`
  routes to core attack targeting; selecting noncombat options records a
  generic NPC interaction target/option for later dialogue/shop/router
  consumers.
- Why it was made: the previous Step 3/4 pass made left-click attack
  and combat runtime available, but it skipped the OSRS interaction
  surface. Right-clicking NPCs still did nothing, which blocked normal
  player interaction testing. Local OSRS/RSPS references use the same
  shape: cache/definition option list, selected option index, then
  server/core dispatch by resolving that index back to an option string.
- Exact surfaces changed:
  - Updated `rc-core/npc.h` / `rc-core/npc.c` with NDEF v4 support,
    fixed five-slot NPC action storage, and helpers for resolving and
    identifying NPC options.
  - Updated `rc-core/tick.c` so `rc_player_interact_npc` validates the
    selected option against the NPC definition and only treats a true
    data-backed `Attack` slot as attack intent.
  - Updated `tools/export_npc_defs_full.py` to export NDEF v4 action
    slots from `data/source/data_osrs/npcids/*.json`.
  - Regenerated `data/defs/npc_defs.bin` and
    `tools/reports/npc_defs_full.txt`; the regenerated binary contains
    `15182` NPC definitions and reports as NDEF version `4`.
  - Updated `rc-viewer/ui.h` / `rc-viewer/ui.c` to expose the existing
    context menu as a public UI surface for world interactions.
  - Updated `rc-viewer/viewer.c` with tile/screen NPC picking,
    right-click NPC context menu construction, viewer-side option-index
    metadata, and context-action routing back into `rc-core`.
  - Added `tests/test_npc_option_interactions.c` covering
    Talk-to/noncombat interaction recording, Attack option routing, and
    invalid blank-slot rejection.
  - Updated `work.md`, `work_highlevel.md`, and `changelog.md`.
- Upstream/downstream impacts: `npc_defs.bin` is now NDEF v4. The core
  loader remains backward-compatible with v1-v3, but generated current
  data should be produced with the updated exporter. `rc-viewer`
  remains an input/render translator; it does not own NPC action rules.
  Later systems should consume `RC_INTERACT_NPC` plus
  `player.interact_option` to dispatch dialogue, shops, pickpocket,
  minigame, slayer, and other content-specific behavior.
- Verification:
  - `python3 tools/export_npc_defs_full.py` passed and regenerated
    NDEF v4 data/reports.
  - `python3 -c "import struct,pathlib; b=pathlib.Path('data/defs/npc_defs.bin').read_bytes(); print(struct.unpack_from('<III', b, 0))"`
    printed `(1313097030, 4, 15182)`.
  - `cmake --build /tmp/runec_copy_viewer_validation_build` passed.
  - `cmake --build /tmp/runec_copy_viewer_validation_build --target test`
    passed: `37/37` tests.
  - `timeout 20 env RC_VIEWER_EXIT_FRAMES=2 /tmp/runec_copy_viewer_validation_build/rc-viewer`
    passed from the repo root and loaded `npc_defs.bin` as NDEF v4.

## 2026-05-01 — Generic Player Combat Stats and NPC Attack Flow Runtime

- Change made: implemented the first generic runtime pass for playable
  Step 3 and Step 4. Player combat now tracks selected attack style,
  stance, XP routing, special-attack energy, weapon-derived speed/range,
  gear-derived offensive/defensive bonuses, prayer-modified effective
  levels, hit feedback, and combat XP on landed player hits. NPC
  interaction now supports generic attack targeting, path-to-target,
  LOS/range checks, NPC chase, retaliation, attack timers, pending-hit
  resolution, death timers, respawn timers, HP bars, hitsplats, and
  viewer click-to-attack.
- Why it was made: after inventory/equipment runtime and equipped
  rendering were usable, the next blocker was that the player could
  walk but could not attack guards/NPCs or exercise real combat state.
  The implementation stays data-driven: it consumes `items.bin`,
  `npc_defs.bin`, current skills, prayers, equipment bonuses, selected
  spell state, pathfinding collision, and generic NPC combat fields
  instead of hardcoding specific NPC behavior.
- Exact surfaces changed:
  - Updated `rc-core/types.h` with attack stance, combat XP masks,
    interaction kinds, style/special/hit-feedback player state, and
    NPC attack/hit-feedback timers.
  - Updated `rc-core/combat.h` / `rc-core/combat.c` with combat-style
    refresh, selected-style API, weapon speed/range helpers, stance
    effective-level bonuses, route-to-target, range/LOS checks, NPC
    chase, retaliation setup, player/NPC attack timers, NPC attack
    animation timers, and combat XP award routing.
  - Updated `rc-core/tick.c` with core-owned player route movement,
    walk/run/attack/interact-NPC APIs, pending NPC hit damage feedback,
    NPC death/respawn setup, and player-combat XP application.
  - Updated `rc-core/npc.c` with live NPC attack/hitsplat timer ticking
    and respawn cleanup.
  - Updated `rc-core/world.c`, `rc-core/items.c`, and `rc-core/api.h`
    for default combat state, automatic style refresh after equipment
    changes, and the public style-selection API.
  - Updated `rc-viewer/viewer.c` so the viewer enables combat/prayer,
    uses core `rc_world_tick`, routes through core walk/run APIs,
    syncs UI HP/prayer/run/combat/special state, maps combat-style UI
    clicks to core state, left-clicks NPCs to attack, plays NPC attack
    animation overrides when available, and draws generic NPC HP bars
    and hitsplats.
  - Added `tests/test_combat_runtime_flow.c` covering generic attack
    targeting, route-to-target, landed-hit XP, NPC death/respawn setup,
    and target clearing.
  - Updated `work.md`, `work_highlevel.md`, and `changelog.md`.
- Upstream/downstream impacts: combat state is now owned by `rc-core`;
  `rc-viewer` remains an input/render translation layer. This enables a
  guard/NPC combat vertical slice using database stats and equipment
  bonuses. Remaining parity work includes exact weapon-specific style
  button tables, ammo/rune consumption, special attack effects,
  generated NPC action-option tables beyond generic Attack/noncombat
  interaction recording, exact player attack animations, richer
  projectile rendering, loot table drops on death, and exact
  boss-specific behavior.
- Verification:
  - `cmake --build /tmp/runec_copy_viewer_validation_build` passed.
  - `cmake --build /tmp/runec_copy_viewer_validation_build --target test`
    passed: `36/36` tests.
  - `timeout 20 env RC_VIEWER_EXIT_FRAMES=2 /tmp/runec_copy_viewer_validation_build/rc-viewer`
    passed from the repo root.
  - Coverage analysis and RL-style performance benchmarks were not run
    for this pass; this is a documented verification gap from the
    project execution rule.

## 2026-05-01 — Client-Style Composed Player Appearance Path

- Change made: replaced the broken independent body/equipment draw path
  with a viewer-owned composed player appearance cache. Visible default
  identity-kit body parts and equipped item worn models are merged into
  one generated `ModelEntry`, one `AnimModelState`, and one draw call.
- Why it was made: RSPS repos do not render hands, armor, and weapons as
  separate overlay meshes. They encode appearance slots and render-emote
  metadata, and the client builds one composed animated player model.
  RuneC's separate draw calls caused hands and 2h weapons to drift behind
  the player, static weapon fallbacks, and visible armor seams.
- Exact surfaces changed:
  - Updated `rc-viewer/viewer.c` with a composed-player mesh cache,
    appearance signature rebuild, merged base vertices/skins/faces, and
    single-frame animation application for body, hands, armor, and
    weapon together.
  - Updated the player draw path to use composed appearance for
    weapon-only and armor/equipment cases, not only body-hiding armor.
  - Updated `work.md`, `work_highlevel.md`, and `changelog.md`.
- Upstream/downstream impacts: viewer-only renderer correction. Core
  inventory/equipment state and item rules are unchanged. This is the
  first real client-style composition step; cache-wide multi-model worn
  item composition and exact appearance override breadth remain future
  parity work.
- Verification:
  - `cmake --build /tmp/runec_copy_viewer_validation_build` passed.
  - `cmake --build /tmp/runec_copy_viewer_validation_build --target test`
    passed: `35/35` tests.
  - `timeout 20 env RC_VIEWER_EXIT_FRAMES=2 /tmp/runec_copy_viewer_validation_build/rc-viewer`
    passed from the repo root.

## 2026-05-01 — b237 Player BAS Animation Export

- Change made: added Jagex `main_file_cache.dat2` / `main_file_cache.idx*`
  disk-store support to the modern cache reader, regenerated
  `data/anims/player.anims` from the local OpenRS2 b237 cache, and
  switched the viewer's default player idle/walk/run sequence IDs to
  the real OSRS defaults (`808`, `819`, `824`).
- Why it was made: equipped-player rendering was looping on viewer
  pose heuristics because the animation bundle did not actually contain
  the default human sequences or the godsword render-emote/BAS
  sequences (`7043`, `7052`, `7053`). Without those sequences, held
  weapons had to be static or inherited an incorrect fallback pose.
- Exact surfaces changed:
  - Updated `tools/cache_pipeline/modern_cache_reader.py` to read both
    flat OpenRS2 directories and Jagex dat2/idx disk stores.
  - Regenerated `data/anims/player.anims` from
    `/tmp/openrs2_2528/cache`: `16` framebases, `72` sequences.
  - Updated `rc-viewer/viewer.c` to use default player sequence IDs
    `808`/`819`/`824` and to stop forcing static idle/equipment drawing
    when animation data is loaded.
  - Updated `work.md`, `work_highlevel.md`, and `changelog.md`.
- Upstream/downstream impacts: viewer/exporter-only correction. This
  makes the existing IREM v2 AGS BAS metadata usable at runtime and
  removes the static weapon workaround. Remaining exactness gap is the
  structural one: the viewer still draws player appearance parts as
  separate model entries instead of a single client-composed player
  mesh.
- Verification:
  - `python3 -m py_compile tools/cache_pipeline/modern_cache_reader.py tools/cache_pipeline/export_animations.py`
    passed.
  - `python3 tools/cache_pipeline/export_animations.py --modern-cache /tmp/openrs2_2528/cache --output data/anims/player.anims`
    passed and exported `16` framebases / `72` sequences.
  - `cmake --build /tmp/runec_copy_viewer_validation_build` passed.
  - `cmake --build /tmp/runec_copy_viewer_validation_build --target test`
    passed: `35/35` tests.
  - `timeout 20 env RC_VIEWER_EXIT_FRAMES=2 /tmp/runec_copy_viewer_validation_build/rc-viewer`
    passed from the repo root.

## 2026-05-01 — Equipped Movement Regression Correction

- Change made: narrowed the missing-BAS equipped-item fallback. Idle
  player/equipment rendering still resets to the captured rest pose,
  but weapon equipment no longer disables base player walk/run
  animation.
- Why it was made: the previous missing-BAS fallback prevented bad
  held-item placement but made the full player model static whenever a
  weapon was equipped and worsened visible composed-equipment seams.
- Exact surfaces changed:
  - Updated `rc-viewer/viewer.c` animation/update gating and generated
    equipment draw fallback selection.
  - Updated `work.md`, `work_highlevel.md`, and `changelog.md`.
- Upstream/downstream impacts: viewer-only correction. Held
  weapons/shields without loaded BAS still use an approximate safe
  fallback; exact movement/stance parity remains blocked on b237
  dat2-compatible default human/godsword BAS export/loading.
- Verification:
  - `cmake --build /tmp/runec_copy_viewer_validation_build` passed.
  - `cmake --build /tmp/runec_copy_viewer_validation_build --target test`
    passed: `35/35` tests.
  - `timeout 20 env RC_VIEWER_EXIT_FRAMES=2 /tmp/runec_copy_viewer_validation_build/rc-viewer`
    passed from the repo root.

## 2026-05-01 — Missing-BAS Movement Fallback Tightening

- Change made: tightened the viewer fallback for equipped held items
  whose required player BAS/render-emote sequences are not loaded.
  The base player mesh now restores to rest pose whenever such held
  equipment is worn, and composed equipment rendering prefers static
  rest-pose drawing while idle or when held gear cannot use a loaded
  BAS.
- Why it was made: the prior fallback fixed idle disappearance, but
  movement still applied the wrong walk/run sequence to the hands,
  weapon, shield, and composed armor, causing held gear to drift behind
  the player and armor idles to appear mid-stride.
- Exact surfaces changed:
  - Updated `rc-viewer/viewer.c` player-animation and composed
    equipment draw fallback behavior.
  - Updated `work.md`, `work_highlevel.md`, and `changelog.md`.
- Upstream/downstream impacts: viewer-only fallback. It intentionally
  avoids wrong animation parity for missing-BAS held gear; true OSRS
  walk/run/idle parity still requires exporting/loading the b237
  default human and weapon BAS sequences.
- Verification:
  - `cmake --build /tmp/runec_copy_viewer_validation_build` passed.
  - `cmake --build /tmp/runec_copy_viewer_validation_build --target test`
    passed: `35/35` tests.
  - `timeout 20 env RC_VIEWER_EXIT_FRAMES=2 /tmp/runec_copy_viewer_validation_build/rc-viewer`
    passed from the repo root.

## 2026-05-01 — Held Equipment Rest-Pose Fallback Correction

- Change made: corrected the held weapon/shield fallback introduced for
  missing BAS sequences. Model loading now retains each model's original
  scaled rest-pose vertex buffer, the viewer restores from that exact
  buffer for idle missing-BAS held-equipment fallback, and held
  weapons/shields animate again while the player is moving.
- Why it was made: the previous fallback stopped the weapon from
  appearing behind the player, but it froze held weapons during
  movement, could leave the character invisible or in a stale movement
  frame at idle, and did not reliably reset composed hands/body parts.
- Exact surfaces changed:
  - Updated `rc-viewer/models.h` to retain and free `rest_verts` for
    each loaded model.
  - Updated `rc-viewer/viewer.c` so rest-pose reset copies the captured
    scaled rest vertices, static fallback is limited to idle held
    weapons/shields whose BAS is not loaded, and moving held equipment
    returns to animated rendering.
  - Updated `work.md`, `work_highlevel.md`, and `changelog.md`.
- Upstream/downstream impacts: viewer-only change. Gameplay state,
  item rules, stats, and generated item render data are unchanged.
  True OSRS held-item animation parity still requires exporting/loading
  default human and godsword BAS sequences from the b237 cache.
- Verification:
  - `cmake --build /tmp/runec_copy_viewer_validation_build` passed.
  - `cmake --build /tmp/runec_copy_viewer_validation_build --target test`
    passed: `35/35` tests.
  - `timeout 20 env RC_VIEWER_EXIT_FRAMES=2 /tmp/runec_copy_viewer_validation_build/rc-viewer`
    passed from the repo root.

## 2026-05-01 — Equipped Rendering Data-Driven Pass

- Change made: implemented the first data-driven Step 2 equipped item
  rendering pass. The item render exporter now writes IREM v2 records
  with cache wearpos metadata, data-driven body-part hide masks,
  render flags, and RSMod-derived Armadyl godsword BAS ready/walk/run
  animation IDs. The viewer render-map loader accepts both IREM v1 and
  v2, and the viewer now uses the new metadata to choose composed
  body/equipment rendering only when an equipped item replaces base
  body parts.
- Why it was made: the previous Step 2 path relied on item-name
  heuristics for head/body visibility and loose equipped overlays.
  Reference repos show OSRS equipment visuals are driven by appearance
  composition metadata: wear slots, secondary slots, body-part
  replacement, recolors/retextures, and weapon BAS/render-emote fields.
- Exact surfaces changed:
  - Updated `tools/cache_pipeline/export_item_render_models.py` to
    parse cache wearpos opcodes 13/14/27, compute hide masks from
    wearpos instead of name tokens, flag two-handed weapons from
    right-hand/left-hand wearpos, and emit IREM v2 records.
  - Updated `rc-viewer/equipment_render.h` with the IREM v2 loader and
    new equip slot, wearpos, render flag, and BAS fields.
  - Updated `rc-viewer/viewer.c` so armor that hides body parts draws
    through the composed identity-kit/equipment path, no-hide items
    such as partyhats and weapons keep the original base player model,
    and weapon BAS animation IDs are selected when present in the
    loaded animation cache.
  - Regenerated `data/models/items.models` and
    `data/models/item_render.map`.
  - Updated `work.md`, `work_highlevel.md`, and `changelog.md`.
- Upstream/downstream impacts: gameplay/equip rules remain in
  `rc-core`; this change is viewer/exporter-only. Partyhats no longer
  hide head/hair/jaw, armor hides base parts from cache wearpos data,
  and AGS is marked as a two-handed weapon with RSMod-derived BAS IDs.
  Step 2 still needs visual QA and metadata broadening before full
  closure.
- Verification:
  - `python3 -m py_compile tools/cache_pipeline/export_item_render_models.py`
    passed.
  - `python3 tools/cache_pipeline/export_item_render_models.py --cache /tmp/openrs2_2528/cache --items data/defs/items.bin --output data/models/items.models --render-map data/models/item_render.map`
    passed and exported `68` render models plus `27` IREM v2 item
    render records.
  - `cmake --build /tmp/runec_copy_viewer_validation_build` passed.
  - `cmake --build /tmp/runec_copy_viewer_validation_build --target test`
    passed: `35/35` tests.
  - `timeout 20 env RC_VIEWER_EXIT_FRAMES=2 /tmp/runec_copy_viewer_validation_build/rc-viewer`
    passed from the repo root.

## 2026-05-01 — Held Weapon and Shield Placement Fallback

- Change made: fixed the visible held weapon/shield placement fallback
  when the viewer does not have the weapon's required BAS animation
  sequences loaded. Weapons and shields now avoid inheriting the bad
  fallback animated pose when their render record does not have a
  loaded BAS sequence, and the idle player body resets to its base rest
  pose while held equipment is in that fallback state.
- Why it was made: the current `data/anims/player.anims` bundle only
  contains six FC/crossbow-oriented sequences and lacks both default
  human `808/819/824` and AGS/godsword `7043/7052/7053` BAS sequences.
  Applying the crossbow idle animation to held equipment pushed the
  hands/weapon/shield behind the character.
- Exact surfaces changed:
  - Updated `rc-viewer/viewer.c` with held-equipment BAS availability
    checks, base-pose reset for idle held-equipment fallback, and
    non-animated draw fallback for weapons/shields when their BAS is
    not loaded.
  - Updated `tools/cache_pipeline/export_animations.py` so the needed
    animation set includes godsword BAS ready/walk/run/turn/block IDs
    for the future proper b237 animation export path.
  - Updated `work.md`, `work_highlevel.md`, and `changelog.md`.
- Upstream/downstream impacts: this remains viewer/exporter-only.
  Gameplay equipment state and stats in `rc-core` are unchanged. Full
  visual parity still needs a dat2-compatible b237 animation export path
  so `player.anims` can include default human and godsword BAS sequences
  instead of relying on the rest-pose fallback.
- Verification:
  - Inspected `data/anims/player.anims` through the RuneC animation
    loader: it has `6` sequences and does not contain `808`, `819`,
    `824`, `7043`, `7052`, or `7053`.
  - Inspected `data/anims/all.anims` through the RuneC animation
    loader: it has `36` sequences and also does not contain those BAS
    IDs.
  - `python3 -m py_compile tools/cache_pipeline/export_animations.py`
    passed.
  - `cmake --build /tmp/runec_copy_viewer_validation_build` passed.
  - `cmake --build /tmp/runec_copy_viewer_validation_build --target test`
    passed: `35/35` tests.
  - `timeout 20 env RC_VIEWER_EXIT_FRAMES=2 /tmp/runec_copy_viewer_validation_build/rc-viewer`
    passed from the repo root.
  - Attempted regeneration with
    `python3 tools/cache_pipeline/export_animations.py --modern-cache /tmp/openrs2_2528/cache --output data/anims/player.anims`;
    it failed before writing because that exporter path expects the
    `ModernCacheReader` manifest layout, while `/tmp/openrs2_2528/cache`
    is the Jagex dat2 cache layout used by the model exporter.

## 2026-05-01 — Equipped Rendering Reference Audit and Status Recalibration

- Change made: audited the local OSRS reference repositories for
  equipped item rendering and recalibrated planning docs so runtime
  Step 2 is marked active and unresolved rather than complete.
- Why it was made: the current viewer still has equipped rendering
  parity gaps around hand placement, two-handed weapon stance,
  partyhat/head/hair visibility, and name-heuristic body-part hiding.
  The next implementation pass needs to follow the real OSRS
  appearance composition model rather than continuing overlay fixes.
- Exact surfaces changed:
  - Updated `work.md` with the Step 2 audit conclusion, source
    authorities, known gaps, and next implementation surfaces.
  - Updated `work_highlevel.md` so the high-level map states Step 1 is
    complete, Step 2 is active, and Step 3+ runtime work is blocked
    until equipped rendering is explicitly closed.
  - Updated `changelog.md`.
- Upstream/downstream impacts: no runtime code or generated data
  changed. Future Step 2 implementation should extend
  `tools/cache_pipeline/export_item_render_models.py`,
  `rc-viewer/equipment_render.h`, and `rc-viewer/viewer.c` with
  data-driven appearance composition metadata while keeping gameplay
  rules in `rc-core`.
- Reference authorities audited: VoidPS/current_fightcaves appearance
  and body-part code, RuneLite item/kit cache definitions, RSMod
  appearance sync and wearpos code, 2011Scape appearance/equip update
  code, osrsreboxed model summary scripts, and the older pufferlib OSRS
  model exporter.
- Verification: not run; this was a documentation and reference-audit
  update only.

## 2026-05-01 — Restore Original Player Base Model Rendering

- Change made: restored the viewer default player body to the original
  exported `data/models/player.models` mesh and layered generated
  equipped item render models on top. Identity-kit body-part rendering
  now remains only as a fallback when `player.models` is unavailable.
- Why it was made: using generated identity-kit body pieces as the
  default player body changed the base character appearance and did not
  match the previous viewer model.
- Exact surfaces changed:
  - Updated `rc-viewer/viewer.c` to draw `player.models` first and draw
    generated equipment models from `item_render.map` as overlays.
  - Updated `changelog.md`.
- Upstream/downstream impacts: gameplay state is unchanged. Equipment
  visuals remain viewer-only; the remaining known issues are still
  hand/2h pose exactness and full OSRS appearance composition parity.
- Verification:
  - `cmake --build /tmp/runec_copy_viewer_validation_build` passed.
  - `cmake --build /tmp/runec_copy_viewer_validation_build --target test`
    passed: `35/35` tests.
  - `timeout 20 env RC_VIEWER_EXIT_FRAMES=2 /tmp/runec_copy_viewer_validation_build/rc-viewer`
    passed from the repo root.

## 2026-05-01 — Equipped Item Rendering Composition

- Change made: replaced the viewer's first-pass static equipped-item
  overlays with generated equipment composition metadata. The viewer
  now draws the player from default identity-kit body-part models plus
  equipped item render models, applies cache item recolors/retextures
  at export time, hides default body parts for equipped pieces, and
  animates composed body/equipment models with the current player
  frame.
- Why it was made: equipped state was core-owned, but the visual path
  was still drawing raw item model IDs over a full default player
  model. OSRS appearance is a composed model pipeline, not a loose
  overlay; Torva, AGS, Bandos, partyhats, and 3rd age pieces need
  cache-composed render models before the viewer can validate gear
  visually.
- Exact surfaces changed:
  - Updated `tools/cache_pipeline/export_item_render_models.py` to
    read cache item definitions and identity kits, apply item/kit
    recolors and retextures, generate synthetic default body-part,
    equipped-item, and ground-item models, and emit
    `data/models/item_render.map`.
  - Added `rc-viewer/equipment_render.h` for viewer-only item render
    map loading.
  - Updated `rc-viewer/viewer.c` to load `RUNEC_ITEM_RENDER_MAP`,
    allocate animation state for item/body render models, draw a
    composed player model from generated body/equipment render IDs,
    and use generated ground render IDs for dropped items.
  - Regenerated `data/models/items.models` and added generated
    `data/models/item_render.map`.
  - Updated `work.md`, `work_highlevel.md`, and `rc-viewer/README.md`
    to reflect the current equipped rendering state and remaining
    render-map breadth/exact appearance-type work.
- Upstream/downstream impacts: `rc-core` remains the owner of
  inventory/equipment rules and stats. `rc-viewer` now consumes
  viewer-only render metadata for appearance composition, which means
  future equipment parity work should extend the exporter/map rather
  than adding item-specific rendering rules to core. Current body-part
  hiding uses exported slot/model information plus conservative name
  heuristics for head coverage; the next deepening step is exact
  appearance-type metadata for all items.
- Verification:
  - `python3 -m py_compile tools/cache_pipeline/export_item_render_models.py`
    passed.
  - `python3 tools/cache_pipeline/export_item_render_models.py --cache /tmp/openrs2_2528/cache --items data/defs/items.bin --output data/models/items.models --render-map data/models/item_render.map`
    passed and exported `68` render models plus `27` item render
    records.
  - `cmake --build /tmp/runec_copy_viewer_validation_build` passed.
  - `cmake --build /tmp/runec_copy_viewer_validation_build --target test`
    passed: `35/35` tests.
  - `timeout 20 env RC_VIEWER_EXIT_FRAMES=2 /tmp/runec_copy_viewer_validation_build/rc-viewer`
    passed from the repo root.

## 2026-05-01 — Item Visual Runtime Follow-Up

- Change made: fixed the first round of inventory/equipment visual
  parity gaps after the runtime slice. Equipped items now draw first-
  pass item model overlays on the player, dropped items draw exported
  item ground models instead of yellow primitive cubes, coin stacks use
  OSRS-style stack icon/model thresholds and K/M quantity text colors,
  and occupied equipment-tab slots render actual item icons over the
  `wornitems` reference layout.
- Why it was made: runtime item state existed, but the viewer still
  presented several item surfaces as placeholders. The viewer should
  render core item state using cache/wiki assets while keeping gameplay
  rules in `rc-core`.
- Exact surfaces changed:
  - Added `tools/cache_pipeline/export_item_render_models.py`, which
    consumes `data/defs/items.bin`, reads selected item model groups
    from the local Jagex dat2 cache, and exports an MDL2 item model
    bundle for the Raylib viewer.
  - Added generated `data/models/items.models` for the current
    showcase/runtime item set: coin stack render variants, partyhats,
    abyssal whip, Armadyl godsword, Bandos chestplate/tassets, Torva
    pieces, and 3rd age melee pieces.
  - Updated `rc-viewer/viewer.c` to load `RUNEC_ITEM_MODELS`
    defaulting to `data/models/items.models`, draw equipped male model
    IDs from `items.bin` over the player, and draw dropped ground
    models using `ground_model_id`, including separate coin stack
    model IDs `995` through `1004` by quantity threshold.
  - Updated `rc-viewer/ui.c` so coins select the correct stack icon
    bucket, stack quantities render as exact/K/M text with OSRS color
    thresholds, and equipment slots render item icons rather than
    labels/custom boxes.
  - Updated `rc-viewer/ui_assets.c` to load `miscgraphics_2`,
    `miscgraphics_3`, and the coin stack icon variants.
  - Added OSRS Wiki coin stack PNG variants under `data/sprites/items/`
    and recorded them in `data/sprites/items/manifest.tsv`.
  - Updated `work.md`, `work_highlevel.md`, and `rc-viewer/README.md`
    to reflect the first-pass item visual runtime state and remaining
    exactness work.
- Upstream/downstream impacts: `items.bin` is now the viewer authority
  for equipped overlay model IDs and ground item model IDs. This is a
  first-pass visual overlay; exact OSRS body-part hiding, model
  recolors/retextures, exact stack countobj generalization beyond
  coins, and animation attachment still need a deeper
  equipment-rendering pass.
- Verification:
  - `python3 tools/cache_pipeline/export_item_render_models.py --cache /tmp/openrs2_2528/cache --items data/defs/items.bin --output data/models/items.models`
    passed and exported `36` item render models with `0` errors.
  - `cmake --build /tmp/runec_copy_viewer_validation_build` passed.
  - `cmake --build /tmp/runec_copy_viewer_validation_build --target test`
    passed: `35/35` tests.
  - `timeout 20 env RC_VIEWER_EXIT_FRAMES=2 /tmp/runec_copy_viewer_validation_build/rc-viewer`
    passed from the repo root.
  - Verification gap: no coverage report or RL-style performance
    benchmark target was run for this visual-only follow-up.

## 2026-05-01 — Inventory and Equipment Runtime Slice

- Change made: added the first real inventory/equipment runtime slice
  behind `rc-core`, with a 28-slot inventory container, equipment-slot
  equip/unequip validation, OSRS-style stack handling, item moving,
  item dropping, ground-item pickup, equipment bonus/weight refresh,
  and viewer click plumbing into the core APIs.
- Why it was made: the viewer UI had become clickable and OSRS-shaped,
  but the inventory/equipment panels still used fixture-only state.
  Core gameplay systems need to own item rules so the viewer remains a
  frontend that renders state and translates input intents.
- Exact surfaces changed:
  - Updated `AGENT.md`, `work.md`, and `work_highlevel.md` with the
    repo-boundary rule: `rc-core` owns simulation/gameplay state,
    `rc-viewer` owns rendering/input translation, and `rc-content`
    owns OSRS-specific content hooks.
  - Extended `rc-core/items.h` and `rc-core/items.c` with item
    requirement fields, non-stackable quantity expansion, stack merge
    rules, inventory move/remove helpers, equip/unequip/drop/pickup
    APIs, two-handed weapon shield clearing, and equipment stat
    recomputation from `items.bin`.
  - Updated `rc-core/tick.c` so ground items despawn through the core
    tick path instead of relying on stub item actions.
  - Updated `rc-core/api.h` with inventory move API exposure.
  - Updated `rc-viewer/viewer.c` and `rc-viewer/ui.c` so inventory and
    equipment clicks synchronize with `RcWorld.player` state, equippable
    item clicks call core equip, equipment-slot clicks call core
    unequip, shift-click inventory drops items, and `P`/same-tile scene
    clicks pick up ground items.
  - Updated `rc-viewer/README.md` to describe the viewer as
    core-synced for the first inventory/equipment runtime slice rather
    than placeholder-only state.
  - Updated `tests/test_items_bin.c` and
    `tests/test_shops_storage_runtime.c` for parsed equip requirements
    and OSRS-style non-stackable quantity semantics.
  - Added `tests/test_inventory_equipment_runtime.c`.
  - Updated `CMakeLists.txt` test discovery to use
    `CONFIGURE_DEPENDS` so new test files are picked up by the build.
- Upstream/downstream impacts: item definitions from `items.bin` now
  drive equip slots, stackability, requirements, weapon type, bonuses,
  and weight refresh for the runtime. The viewer remains a consumer of
  core state; it does not implement item rules. This unlocks the next
  systems: consumables, banking/storage UI, equipment model visibility,
  attack/click-NPC combat, and broader item-icon generation.
- Verification:
  - `cmake --build /tmp/runec_copy_viewer_validation_build` passed.
  - `cmake --build /tmp/runec_copy_viewer_validation_build --target test`
    passed: `35/35` tests passed, including
    `test_inventory_equipment_runtime`.
  - `timeout 20 env RC_VIEWER_EXIT_FRAMES=2 /tmp/runec_copy_viewer_validation_build/rc-viewer`
    passed from the repo root.
  - Verification gap: no coverage report or RL-style performance
    benchmark target was run for this slice; current validation is
    compile, assert-enabled CTest, and frame-capped viewer startup.

## 2026-05-01 — Real Item Icons for Inventory Showcase

- Change made: replaced the viewer inventory placeholder item drawings
  with real inventory icon PNGs for the requested showcase items:
  red/yellow/blue/green/purple/white partyhats, abyssal whip, Armadyl
  godsword, Bandos chestplate, Bandos tassets, Torva full helm,
  Torva platebody, Torva platelegs, 3rd age full helmet, 3rd age
  platebody, 3rd age platelegs, 3rd age kiteshield, and a 10m coin
  stack using the high-stack coin icon.
- Why it was made: the item database completion work produced item
  definitions/model links/equipment metadata, but it did not add a
  rendered item-icon asset path for the Raylib UI. The inventory tab
  needs actual OSRS item art now, while full inventory/equipment engine
  behavior remains a separate runtime system.
- Exact surfaces changed:
  - Added real PNG icon assets under `data/sprites/items/` plus
    `data/sprites/items/manifest.tsv` recording item IDs and source
    image URLs.
  - Updated `rc-viewer/ui_assets.c` to load item icon assets by stable
    names like `item_4151`.
  - Updated `rc-viewer/ui.c` so inventory item rendering first draws
    `item_<id>` icons and only falls back to lightweight shapes if an
    icon is missing.
  - Updated the viewer's default inventory fixture to the requested
    showcase items and right-click context titles to use item labels.
  - Updated `work.md` and `work_highlevel.md` to mark the remaining
    full item engine work: inventory container runtime, item stack
    rules, ground pickup/drop, equip/unequip validation, stat refresh,
    and full item-icon generation/lookup for every item ID.
- Upstream/downstream impacts: the UI now has real inventory art for
  the current showcase set without blocking on the full item/equipment
  engine. The general item-icon path for every item ID is still planned
  work, because full cache-accurate item icons require generated item
  model/icon rendering or a broader icon asset corpus, not only the
  item definition database.
- Verification:
  - Downloaded the requested OSRS Wiki inventory PNGs with `curl -L`
    into `data/sprites/items/`.
  - `cmake --build /tmp/runec_copy_viewer_validation_build --target rc-viewer` passed.
  - `timeout 20 env RC_VIEWER_EXIT_FRAMES=2 /tmp/runec_copy_viewer_validation_build/rc-viewer` passed from the repo root.

## 2026-05-01 — OSRS Tab Interior Reference Pass

- Change made: replaced the first set of ad-hoc side-tab interiors
  with OSRS reference-driven layouts for the combat options tab, skills
  tab, and inventory tab. Combat now uses `combat_interface` (`593`)
  geometry for stance buttons, auto-retaliate, and special attack.
  Skills now follows the local `stats` (`320`) component grid including
  the current Sailing cell. Inventory now uses the `inventory` (`149`)
  190x261 container with OSRS slot spacing instead of stretched fake
  slot boxes.
- Why it was made: visual QA showed the previous content inside tabs
  was still placeholder-like even though the outer gameframe was closer
  to OSRS. RSMod/Void-style references open cache interfaces into the
  side panel, so the viewer needs to mirror those interface component
  surfaces rather than inventing independent layouts.
- Exact surfaces changed:
  - Updated `rc-viewer/ui_reference.h` with interface IDs and component
    rectangles for `inventory` (`149`), `stats` (`320`), and
    `combat_interface` (`593`).
  - Updated `rc-viewer/ui.h` with hookable combat intent kinds and
    combat-side state for selected style, auto-retaliate, and special
    attack.
  - Updated `rc-viewer/ui.c` to draw/cache-click combat stances,
    auto-retaliate, special attack, OSRS inventory slots, and the
    expanded stats grid.
  - Updated `tools/cache_pipeline/export_sprites_modern.py` and
    `rc-viewer/ui_assets.c` to export/load `combatboxes_*`,
    `combatboxes_special_attack`, `combat_shield`, and
    `sideicons_interface_*` sprites.
  - Regenerated `data/sprites/ui/` from OpenRS2 cache id `2528`;
    export now covers `282` sprite groups with `0` failed groups.
- Upstream/downstream impacts: the tab click surfaces remain exposed
  through `RuneCUiIntent`, so later gameplay systems can hook style
  selection, auto-retaliate, special attack, skill inspection, and
  inventory slot actions without replacing the viewer UI. Inventory
  item art is still a lightweight viewer placeholder because true OSRS
  item icons are generated from object models/client code rather than
  simple static tab IF3 sprites.
- Verification:
  - `python3 tools/cache_pipeline/export_sprites_modern.py --cache /tmp/openrs2_2528/cache --output data/sprites/ui` passed with `282` exported sprite groups and `0` failed groups.
  - `cmake --build /tmp/runec_copy_viewer_validation_build --target rc-viewer` passed.
  - `timeout 20 env RC_VIEWER_EXIT_FRAMES=2 /tmp/runec_copy_viewer_validation_build/rc-viewer` passed from the repo root.

## 2026-05-01 — OSRS Side Tab Active-State Fix

- Change made: corrected side-tab selection rendering so only the
  currently active viewer tab draws the red `side_stone_highlights`
  backplate. Added a short tab press timer so clicked tabs depress for
  a brief frame window and then remain selected/red until another tab
  is selected.
- Why it was made: the previous reference-layout pass drew every
  `side_stone_highlights` piece as static strip chrome, which made the
  combat/edge tab appear permanently lit and prevented visible selected
  state changes. The RSPS/server references open the cache interfaces;
  selected tab state is a client-side active side-panel highlight over
  the base `osrs_stretch_side_topbottom` strip, not all highlights
  drawn at once.
- Exact surfaces changed:
  - Updated `rc-viewer/ui.h` with per-tab press timers.
  - Updated `rc-viewer/ui.c` tab input handling to set the active tab
    and press timer on click.
  - Updated `rc-viewer/ui.c` side-tab rendering to draw the base strip
    for all tabs, draw the red highlight only for `active_tab`, and
    offset the selected icon/backplate during the click animation.
- Upstream/downstream impacts: `RuneCUiIntent` tab IDs are unchanged.
  The active tab visual is now tied to UI state and ready for later
  runtime-backed side-panel content.
- Verification:
  - `cmake --build /tmp/runec_copy_viewer_validation_build --target rc-viewer` passed.
  - `timeout 20 env RC_VIEWER_EXIT_FRAMES=2 /tmp/runec_copy_viewer_validation_build/rc-viewer` passed from the repo root.

## 2026-05-01 — Reference-Derived OSRS Gameframe Layout

- Change made: replaced the remaining hand-positioned viewer gameframe
  layout with constants copied from the local OSRS interface dumps used
  by RSMod/Void-style servers. Added `rc-viewer/ui_reference.h` as the
  reference manifest for `toplevel_osrs_stretch` (`161`), `orbs`
  (`160`), `stats` (`320`), `wornitems` (`387`), `prayerbook` (`541`),
  and `magic_spellbook` (`218`). The viewer now anchors the minimap,
  orb cluster, side menu, top/bottom side-tab stones, side content
  panel, stats click rectangles, and worn-equipment button rectangles
  from those IF3 component coordinates instead of local visual guesses.
- Why it was made: visual QA still showed transparent/wrong tab
  buttons, incorrect orb/minimap placement, and squeezed tab content.
  The RSPS references do not custom-draw those widgets; they open cache
  interfaces. The correct local source of truth is therefore the
  dumped cache interface/component geometry plus the current OpenRS2
  sprite export.
- Exact surfaces changed:
  - Added `rc-viewer/ui_reference.h` with source-documented IF3 IDs,
    dimensions, tab-stone/icon rectangles, stats tab rectangles, and
    worn-equipment button rectangles.
  - Updated `rc-viewer/ui.c` to use `toplevel_osrs_stretch` anchors for
    minimap/orbs/side menu, draw all native side-tab stone sprites
    instead of only transparent click zones, use exact stats component
    rectangles for drawing/clicks, and use cache sprites for the
    equipment-side action buttons.
  - Updated `tools/cache_pipeline/export_sprites_modern.py` and
    `rc-viewer/ui_assets.c` to export/load `options_icons_16`,
    `options_icons_18`, `options_icons_28`, and `whistle`.
  - Regenerated `data/sprites/ui/` from OpenRS2 oldschool live cache id
    `2528`; export now covers `255` sprite groups with `0` failed
    groups.
  - Updated `viewer_validation.md`, `work.md`, `work_highlevel.md`, and
    this changelog.
- Upstream/downstream impacts: the viewer UI is now much closer to the
  same cache-interface model used by Void/RSMod. `RuneCUiIntent`
  surfaces remain stable for future runtime hooks. This is still not a
  full client interface renderer: exact clientscript execution, full
  dynamic tab population, item icon containers, camera-rotated minimap
  projection, mapscene icons, and full prayer/spellbook behavior remain
  future work.
- Verification:
  - `python3 tools/cache_pipeline/export_sprites_modern.py --cache /tmp/openrs2_2528/cache --output data/sprites/ui` passed with `255` exported sprite groups and `0` failed groups.
  - `cmake --build /tmp/runec_copy_viewer_validation_build --target rc-viewer` passed.
  - `timeout 20 env RC_VIEWER_EXIT_FRAMES=2 /tmp/runec_copy_viewer_validation_build/rc-viewer` passed from the repo root.

## 2026-05-01 — Current-Cache OSRS UI Sprite Authority

- Change made: replaced the remaining stale/wrong viewer UI widget
  sprites with current OSRS cache sprites. The UI sprite exporter now
  exports the native compass, fixed minimap cover/masks, minimap edge
  pieces, current side icons, modern orb button sprites, and corrected
  skill-tab aliases for Hunter, Construction, and Total. The viewer
  asset loader now loads those assets, and the UI renderer now composes
  native orb frames, compass/minimap cover art, side-panel chrome, and
  side-tab icons instead of drawing fake button chrome over mismatched
  sprites.
- Why it was made: visual QA showed the previous pass was still using
  valid PNGs from the wrong sprite authority. The old local Fight Caves
  cache did not line up with current RuneLite gameval sprite IDs, so
  names like `orb_icon_0` could resolve to unrelated art. RSMod and
  2011Scape are useful server/client references, but they do not vendor
  the current OSRS gameframe PNG art directly; the reliable authority is
  RuneLite's current sprite constants plus a current OpenRS2 OSRS cache.
- Exact surfaces changed:
  - Updated `tools/cache_pipeline/export_sprites_modern.py` with
    current-cache sprite IDs for compass/minimap widgets, side icons,
    orb widgets, and corrected `staticons2` skill aliases.
  - Clean-regenerated `data/sprites/ui/` from OpenRS2 oldschool live
    cache id `2528`, build `237`, timestamp
    `2026-04-29T10:45:05.699786Z`.
  - Updated `rc-viewer/ui_assets.c` to load the newly exported native
    assets and stop requesting removed prayer-glow placeholders.
  - Updated `rc-viewer/ui.c` to use native orb frames/fillers/icons,
    native compass and minimap cover art, current-cache side icons,
    corrected skill icon mapping, and a layout closer to the provided
    OSRS resizable-mode reference.
  - Updated `rc-viewer/README.md`, `viewer_validation.md`, `work.md`,
    `work_highlevel.md`, and this changelog.
- Upstream/downstream impacts: the viewer UI asset authority is now the
  current OpenRS2 OSRS cache, not the older local Fight Caves cache.
  Future UI sprite additions should be mapped from RuneLite gameval
  constants and exported from the current cache to avoid semantic ID
  drift. Hookable `RuneCUiIntent` surfaces remain unchanged.
- Verification:
  - `rm -rf data/sprites/ui && mkdir -p data/sprites/ui && python3 tools/cache_pipeline/export_sprites_modern.py --cache /tmp/openrs2_2528/cache --output data/sprites/ui` passed with `251` exported sprite groups and `0` failed groups.
  - `cmake --build /tmp/runec_copy_viewer_validation_build --target rc-viewer` passed.
  - `timeout 20 env RC_VIEWER_EXIT_FRAMES=2 /tmp/runec_copy_viewer_validation_build/rc-viewer` passed from the repo root; startup loaded the corrected UI sprite set including `compass`, `fixed_minimap_cover`, side icons, orb frames/fillers/icons, and modern orb buttons.

## 2026-05-01 — OSRS UI Tab Strip, Skills Panel, and Minimap Raster Tightening

- Change made: tightened the next visible OSRS UI parity pass. Bottom
  side-tab icons now sit on native seven-slot stone strip sprites
  instead of transparent click areas, the active tab uses the cache
  stone highlight overlay, the side panel draws the native content
  backing at its intended size, and the skills tab now uses an
  OSRS-style 3-column by 8-row stat grid with skill icons, per-skill
  current/base values, beveling, and a black total-level cell. The
  minimap now renders at the client-style 4 pixels per tile, quantizes
  scene colors into minimap-like terrain colors, overlays collision
  object/wall features into the raster, and updates minimap click
  routing to the same 4-pixel tile scale.
- Why it was made: visual QA showed three remaining first-order UI
  defects: side-tab buttons were clear instead of stone-backed,
  minimap contents were using an overly wide terrain-color
  approximation, and side-panel tab contents were squeezed and not
  shaped like the in-game skills panel. RuneLite references confirm the
  bottom-line viewport uses native side tab components and that the
  minimap is client/cache-driven rather than provided by RSMod or
  2011Scape as standalone art.
- Exact surfaces changed:
  - Updated `rc-viewer/ui.c` side-tab layout and drawing to compose
    `osrs_stretch_side_topbottom_*` plus side icons and active
    `side_stone_highlights_*`.
  - Updated `rc-viewer/ui.c` skills drawing to use the native side
    content dimensions and OSRS-style stat cells.
  - Updated `rc-viewer/viewer.c` minimap rastering and click conversion
    to 4 pixels per tile with terrain quantization and collision/object
    line overlays.
  - Updated `viewer_validation.md`, `work.md`, and this changelog.
- Upstream/downstream impacts: the minimap remains viewer-owned and
  hookable, but it now behaves closer to the client minimap projection
  scale expected by UI testing. Exact cache mapscene icon rendering,
  camera-rotated minimap projection, and full clientscript component
  layout are still future parity work rather than solved by this pass.
- Verification:
  - `cmake --build /tmp/runec_copy_viewer_validation_build --target rc-viewer` passed.
  - `timeout 20 env RC_VIEWER_EXIT_FRAMES=2 /tmp/runec_copy_viewer_validation_build/rc-viewer` passed from the repo root.

## 2026-05-01 — OSRS UI Sprite Correction and Dynamic Minimap

- Change made: corrected the follow-up UI pass after visual comparison
  showed stale/wrong button assets and a fake minimap. The generated UI
  sprite directory is now cleaned before export, the exporter no longer
  requests absent unused prayer-glow IDs, all 24 skill-tab icon sprites
  are exported and loaded, and the viewer builds a terrain/collision-
  backed minimap texture that updates around the player each frame.
- Why it was made: the previous cache-backed UI pass still displayed
  incorrect sprite families for several buttons/orbs and the minimap did
  not show a moving map surface. The viewer needs a usable OSRS-like UI
  shell for Section 4 runtime testing, and stale generated assets should
  not mask export mistakes.
- Exact surfaces changed:
  - Updated `tools/cache_pipeline/export_sprites_modern.py` to export
    `skill_icon_0` through `skill_icon_23` from `staticons` /
    `staticons2` and to omit unavailable unused `prayerglow` IDs.
  - Clean-regenerated `data/sprites/ui/` from the local OSRS cache;
    export now reports `232` sprite groups and `0` failed groups.
  - Updated `rc-viewer/ui_assets.c` to load the skill icon assets.
  - Updated `rc-viewer/ui.h` and `rc-viewer/ui.c` for minimap texture
    ownership/update APIs, skill icon drawing, cleaned text filtering,
    and non-stale minimap/orb presentation.
  - Updated `rc-viewer/viewer.c` to precompute minimap tile colors from
    terrain vertex colors plus collision flags, refresh the circular
    minimap texture around the player, preserve player/NPC/destination
    dots, and free the generated minimap buffer on shutdown.
  - Updated `rc-viewer/README.md`, `viewer_validation.md`, `work.md`,
    `work_highlevel.md`, and this changelog.
- Upstream/downstream impacts: the UI export path is now less tolerant
  of stale wrong assets because the generated folder should be cleaned
  before re-export. The minimap is now a viewer system rather than a
  placeholder; future systems can add exact icon layers, projection
  rotation, and richer map masks on top of the same update path. The
  local cache still does not contain the modern `tli_button01_*` or
  `border_map_compass` sprite IDs found in the symbol dump, so the
  renderer intentionally uses deterministic drawn bevels/fallbacks for
  those button backgrounds instead of displaying incorrect sprites.
- Verification:
  - `rm -rf data/sprites/ui && mkdir -p data/sprites/ui && python3 tools/cache_pipeline/export_sprites_modern.py --cache $RUNEC_REFERENCE_ROOT/current_fightcaves_demo/data/cache --output data/sprites/ui` passed with `232` exported sprite groups and `0` failed groups.
  - `cmake --build /tmp/runec_copy_viewer_validation_build --target rc-viewer` passed.
  - `timeout 20 env RC_VIEWER_EXIT_FRAMES=2 /tmp/runec_copy_viewer_validation_build/rc-viewer` passed from the repo root.

## 2026-05-01 — Modern UI Cleanup and Minimap Hooking

- Change made: corrected the first cache-backed UI pass toward the
  modern resizable OSRS layout. Moved side tabs to a bottom tab strip,
  anchored the side panel above that strip, cleaned up font filtering
  and text snapping, added more cache-exported UI sprites for modern
  side/minimap buttons, and replaced the fake rectangular minimap with
  a circular viewer minimap surface that renders player, NPC, and
  destination dots. Minimap clicks now route the player through the
  existing pathfinder instead of only emitting a placeholder intent.
- Why it was made: visual comparison against the provided reference
  screenshot showed the previous implementation used the wrong
  top-level layout (`toplevel_osrs_stretch`) and left the minimap as a
  nonfunctional placeholder. The viewer needs the minimap as a required
  system for interactive testing.
- Exact surfaces changed:
  - Updated `tools/cache_pipeline/export_sprites_modern.py` and
    regenerated additional sprites under `data/sprites/ui/`.
  - Updated `rc-viewer/ui_assets.c`.
  - Updated `rc-viewer/ui.h` and `rc-viewer/ui.c` for minimap dot state,
    modern side/tab layout, cleaned text rendering, and updated
    minimap/orb drawing.
  - Updated `rc-viewer/viewer.c` to populate minimap dots and translate
    minimap clicks into pathfinder routes.
- Upstream/downstream impacts: UI intent labels remain compatible, but
  minimap click handling is now consumed immediately by the viewer for
  movement. Future runtime work can use the minimap dot API for richer
  markers, route overlays, NPC/player filters, and exact camera-rotated
  projection. Some modern sprite IDs from the current interface dump
  were absent from the local cache, so those buttons still use
  deterministic fallbacks where necessary.
- Verification:
  - `python3 tools/cache_pipeline/export_sprites_modern.py --cache $RUNEC_REFERENCE_ROOT/current_fightcaves_demo/data/cache --output data/sprites/ui` passed, exporting `208` sprite groups; missing modern-only sprite IDs were reported but nonfatal.
  - `cmake --build /tmp/runec_copy_viewer_validation_build --target rc-viewer` passed.
  - `timeout 20 env RC_VIEWER_EXIT_FRAMES=2 /tmp/runec_copy_viewer_validation_build/rc-viewer` passed from the repo root.

## 2026-05-01 — Cache-Backed OSRS Viewer UI

- Change made: replaced the primitive hand-drawn viewer UI shell with a
  cache-sprite-backed OSRS gameframe pass. Exported required UI sprites
  into `data/sprites/ui/`, added a small Raylib UI asset loader, and
  wired the viewer UI to draw sprite-backed side panel chrome, chatbox,
  minimap frame, minimap orbs, side tabs, inventory/equipment surfaces,
  prayer icons, and spellbook icons. Kept the existing `RuneCUiIntent`
  click surface and labeled primary indices for tabs, inventory slots,
  equipment slots, prayer cells, spell cells, chat, minimap clicks, and
  run toggle.
- Why it was made: the previous UI shell was useful for hookability but
  did not resemble OSRS closely enough. The viewer needs a usable,
  recognizable OSRS client surface before Section 4 runtime systems are
  wired into inventory, equipment, item pickup/drop, shops, banks,
  dialogue, prayers, and spells.
- Exact surfaces changed:
  - Updated `tools/cache_pipeline/export_sprites_modern.py` to export
    the selected OSRS UI sprite set from the local Jagex
    `main_file_cache.dat2`/`idx*` cache layout.
  - Added generated sprite assets under `data/sprites/ui/`.
  - Added `rc-viewer/ui_assets.h` and `rc-viewer/ui_assets.c`.
  - Replaced `rc-viewer/ui.c` with the cache-backed renderer and
    explicit click mapping.
  - Updated `rc-viewer/ui.h` with UI asset state and
    `runec_ui_shutdown()`.
  - Updated `rc-viewer/viewer.c` to unload UI assets before closing the
    Raylib window.
  - Updated `rc-viewer/README.md`, `viewer_validation.md`, `work.md`,
    `work_highlevel.md`, and this changelog.
- Upstream/downstream impacts: the viewer now depends on
  `data/sprites/ui/*.png` for exact OSRS UI chrome and falls back to
  deterministic simple drawing only if an asset is missing. The UI
  remains presentation-only; gameplay rules still belong in `rc-core`
  and `rc-content`. The current inventory/equipment contents are still
  placeholder client state, not runtime-backed containers. Remaining UI
  parity gaps are exact clientscript-driven panel layout, true minimap
  masking/projection, runtime item icons/quantities from containers, and
  full behavior wiring for prayers, spells, shops, banks, dialogue, and
  combat input.
- Verification:
  - `python3 tools/cache_pipeline/export_sprites_modern.py --cache $RUNEC_REFERENCE_ROOT/current_fightcaves_demo/data/cache --output data/sprites/ui` passed, exporting `197` sprite groups. Two optional `prayerglow` sprite IDs were absent from this local cache and are not required by the current renderer.
  - `cmake -S $RUNEC_ROOT -B /tmp/runec_copy_viewer_validation_build -DCMAKE_BUILD_TYPE=Release && cmake --build /tmp/runec_copy_viewer_validation_build --target rc-viewer` passed.
  - `cmake --build /tmp/runec_copy_viewer_validation_build --target rc-viewer` passed after tightening one chat-line truncation warning.
  - `timeout 20 env RC_VIEWER_EXIT_FRAMES=2 /tmp/runec_copy_viewer_validation_build/rc-viewer` passed from the repo root; the viewer initialized Raylib, loaded the exported UI sprites, rendered two frames, unloaded textures, and exited cleanly.

## 2026-05-01 — OSRS-Style Viewer UI Shell

- Change made: added a viewer-only OSRS-style clickable UI shell with
  chatbox, minimap, run orb, side-panel tabs, inventory/equipment
  slots, prayer/spell/skills placeholder grids, context menus, and
  hook-ready UI intent/state structs.
- Why it was made: Section 4 playable-system work needs a usable
  client surface before inventory, equipment, shops, banks, dialogue,
  and other runtime systems can be tested comfortably through the
  viewer.
- Exact surfaces changed:
  - Added `rc-viewer/ui.h` and `rc-viewer/ui.c`.
  - Updated `rc-viewer/viewer.c` to initialize/draw UI, sync viewer
    status, process UI intents, and prevent UI clicks from also
    triggering world movement/camera actions.
  - Updated `CMakeLists.txt` so viewer source globbing reconfigures
    when new viewer `.c` files are added.
  - Updated `rc-viewer/README.md`, `viewer_validation.md`, `work.md`,
    and `work_highlevel.md`.
- Upstream/downstream impacts: gameplay rules remain outside
  `rc-viewer`; the UI emits lightweight intents that future
  inventory/equipment/prayer/spell/chat/shop/bank/dialogue runtime
  systems can consume. Current inventory/equipment entries are
  placeholder client state only and do not claim runtime-backed item
  behavior.
- Verification:
  - `cmake -S $RUNEC_ROOT -B /tmp/runec_copy_viewer_validation_build -DCMAKE_BUILD_TYPE=Release && cmake --build /tmp/runec_copy_viewer_validation_build --target rc-viewer` passed.
  - `timeout 20 env RC_VIEWER_EXIT_FRAMES=2 /tmp/runec_copy_viewer_validation_build/rc-viewer` passed from the repo root, reached `Viewer ready`, loaded `15182` NPC defs, spawned `235` Varrock NPCs, created `116` per-def NPC animation states, and exited cleanly.

## 2026-05-01 — Section 4 Tomorrow Pickup Recorded

- Change made: updated the human-facing planning docs to stop encounter
  work after database-complete v1 and record the next pickup as Section
  4 Required Playable v1 Systems, starting with inventory/equipment
  runtime and item pickup/drop.
- Why it was made: the user chose to stop for the day and return
  tomorrow with database completion closed and encounter expansion
  saved for later. The repo needed a clear handoff point so the next
  session does not reopen boss/deep encounter work by accident.
- Exact surfaces changed: updated `work.md`, `work_highlevel.md`, and
  this changelog.
- Upstream/downstream impacts: no runtime, exporter, generated data,
  or test behavior changed. Tomorrow's intended workstream is playable
  runtime/client systems on top of the closed database v1.
- Verification: not run; documentation-only handoff update.

## 2026-05-01 — Database Completion v1 Closure

- Change made: closed database completion v1 with an explicit generated
  closure report. Added `tools/report_database_completion.py` to verify
  required database-category reports and runtime binaries, then emit
  `tools/reports/database_completion.txt` with status
  `DATABASE_COMPLETE_V1_WITH_ACCEPTED_SOURCE_LIMITATIONS`.
- Why it was made: encounter expansion is being saved for later; the
  user wanted the database-completion work finished first. The current
  reports already show broad database coverage and no blocking
  unresolved activity-schema rows, but the repo needed one explicit
  milestone artifact distinguishing database completeness from exact
  runtime/encounter parity.
- Exact surfaces changed:
  - Added `tools/report_database_completion.py`.
  - Added/generated `tools/reports/database_completion.txt`.
  - Updated `database.md`, `work.md`, and `work_highlevel.md`.
  - Updated this changelog.
- Upstream/downstream impacts: future work should treat the database as
  complete for v1 coverage and treat remaining area/source authority,
  static-spawn direction/wander fidelity, uncommon object/per-skill
  behavior, activity fixture deepening, and encounter timing/geometry
  work as targeted parity/deepening, not database-completion blockers.
  The closure explicitly does not claim exact encounter logic parity,
  exact tick timing, UI behavior, or full runtime gameplay parity.
- Verification: `python3 -m py_compile tools/report_database_completion.py`
  passed; `python3 tools/report_database_completion.py` passed and
  generated the closure report.

## 2026-04-30 — Nex and Raid-Final First Runtime Slice

- Change made: added first-runtime-slice support for Nex and the three
  raid-final lanes: Chambers of Xeric/Great Olm, Theatre of Blood/
  Verzik, and Tombs of Amascut/Wardens. Runtime now registers thin
  rc-content scripts for Nex phases, Olm phases, Verzik phases, and
  Wardens phases. Generic reserved raid primitives now have shallow
  runtime behavior for multi-limb bosses, player position swaps,
  environmental walls, telegraphed lightning, continuous boss healing,
  one-shot provided weapons, destructible pillars, web tiles, colored
  nylocas, persistent dot pools, obelisk DPS checks, energized pylons,
  and periodic death-tile waves.
- Why it was made: the user chose to skip the wilderness/KBD and
  smaller standalone/modern boss lanes for now and move directly to Nex
  and raids. These encounters are complex, so local authored TOMLs were
  cross-checked against online OSRS Wiki strategy pages for Nex,
  Chambers of Xeric/Great Olm, Theatre of Blood/Verzik, and Tombs of
  Amascut/Wardens before implementing the shallow runtime layer.
- Exact surfaces changed:
  - Updated `tools/export_encounters.py`; regenerated
    `data/defs/encounters.bin`.
  - Updated `rc-core/encounter.h` and `rc-core/encounter_prims.c`.
  - Added `rc-content/encounters/nex.c` and
    `rc-content/encounters/raids.c`.
  - Updated `rc-content/content.c` and `rc-content/content.h`.
  - Updated `work.md`, `work_highlevel.md`, and this changelog.
- Upstream/downstream impacts: the ENCT runtime can now execute more of
  the existing authored Nex/raid primitive IDs instead of resolving them
  to no-op. Nex phase scripts reset encounter-local phase state,
  present phase transitions/minions, and apply a shallow Zaros heal.
  Olm, Verzik, and Wardens phase scripts provide phase-entry hooks while
  generic primitives provide raid-final presentation/pressure. This is
  not exact raid support: exact arena geometry, exact timing,
  multiplayer room sequencing, Olm hand reset timing, Verzik room
  progression, ToA invocation scaling, path boss room parity, and full
  raid-specific parity remain explicitly unvalidated and deferred until
  one of these raids becomes target playable content.
- Verification: `python3 tools/export_encounters.py` encoded `50`
  encounters with `0` skipped and `0` warnings; `cmake --build build`
  passed after CMake's content-source glob reconfigured for the new
  files; full `ctest --test-dir build --output-on-failure` passed
  `34/34`.
- Online references used:
  - OSRS Wiki Nex strategies:
    https://oldschool.runescape.wiki/w/Nex/Strategies
  - OSRS Wiki Chambers of Xeric strategies:
    https://oldschool.runescape.wiki/w/Chambers_of_Xeric/Strategies
  - OSRS Wiki Theatre of Blood strategies:
    https://oldschool.runescape.wiki/w/Theatre_of_Blood/Strategies
  - OSRS Wiki Tombs of Amascut strategies:
    https://oldschool.runescape.wiki/w/Tombs_of_Amascut/Strategies

## 2026-04-30 — Encounter Expansion Catch-Up: Hydra Through Hunllef

- Change made: added the missed changelog coverage for the encounter
  expansion batches landed after the ENCT v12 Zulrah/Vorkath checkpoint.
  The working tree now has first runtime slices for
  Hydra/Nightmare/Phantom Muspah/Duke Sucellus,
  Vardorvis/Leviathan/Whisperer, Abyssal Sire/Thermonuclear Smoke
  Devil/Skotizo/Sarachnis, and Hespori/Gauntlet-Hunllef. Grotesque
  Guardians and Barrows were audited during the last batch and remain
  activity-backed rather than ENCT-authored.
- Why it was made: these bosses were the next planned breadth-first
  encounter expansion lane. The curated TOMLs already described many
  mechanics, but runtime/export support was missing for primitive IDs,
  packed params, phase/object hooks, damage gates, incoming damage
  modifiers, and thin content scripts. This changelog entry also fixes
  the process gap that `changelog.md` was not updated alongside the
  intermediate repo changes.
- Exact surfaces changed:
  - Updated `tools/export_encounters.py`; regenerated
    `data/defs/encounters.bin`.
  - Updated `rc-core/encounter.h`, `rc-core/encounter.c`,
    `rc-core/encounter_prims.c`, and `rc-core/combat.c`.
  - Added/updated content modules and registration under `rc-content/`:
    `encounters/alchemical_hydra.c`, `encounters/the_nightmare.c`,
    `encounters/phantom_muspah.c`, `encounters/duke_sucellus.c`,
    `encounters/leviathan.c`, `encounters/whisperer.c`,
    `encounters/gauntlet.c`, `content.c`, and `content.h`.
  - Updated `CMakeLists.txt` so content sources are discovered through
    `file(GLOB_RECURSE ... CONFIGURE_DEPENDS rc-content/*.c)`.
  - Updated `work.md` and `work_highlevel.md`.
- Upstream/downstream impacts: encounter export still encodes the same
  50 authored encounter TOMLs, but more of their primitive IDs now have
  executable runtime behavior. New generic coverage includes Hydra
  damage gates/vents/fire walls, Nightmare totems/husks/portals,
  Muspah shield/spikes/clouds, Duke prep/wake handling, Vardorvis axes
  and debuffs, Leviathan pathfinder/buff-zone/enrage pressure,
  Whisperer sanity/tentacles/prayer demand presentation, Sire objective
  NPCs/pathing/tentacles, Thermy facemask incoming-damage scaling,
  Skotizo altar healing/object disabling, Sarachnis cub-gated healing,
  Hespori vine-gated damage, and Hunllef style-swap/tornado/prayer
  presentation. Grotesque Guardians and Barrows are intentionally
  recorded as activity/regular-NPC-mechanics-backed for now, not
  ENCT-backed. Exact arena geometry, exact timing, skilling-prep parity,
  Barrows crypt/tunnel parity, Grotesque phase parity, and full
  boss-specific parity remain explicitly unvalidated and deferred until
  selected as target playable content.
- Verification:
  - Hydra/Nightmare/Phantom Muspah/Duke Sucellus batch:
    `python3 tools/export_encounters.py` encoded `50` encounters with
    `0` skipped and `0` warnings; CMake configure/build passed; full
    `ctest --test-dir build --output-on-failure` passed `34/34`.
  - Vardorvis/Leviathan/Whisperer batch:
    `python3 tools/export_encounters.py` encoded `50` encounters with
    `0` skipped and `0` warnings; `cmake --build build` passed; full
    `ctest --test-dir build --output-on-failure` passed `34/34`.
  - Abyssal Sire/Thermonuclear Smoke Devil/Skotizo/Sarachnis batch:
    `python3 tools/export_encounters.py` encoded `50` encounters with
    `0` skipped and `0` warnings; `cmake --build build` passed; full
    `ctest --test-dir build --output-on-failure` passed `34/34`.
  - Hespori/Gauntlet-Hunllef plus Grotesque/Barrows audit batch:
    `python3 tools/export_encounters.py` encoded `50` encounters with
    `0` skipped and `0` warnings; `encounters.bin` is `23,803` bytes;
    `cmake --build build` passed after a local helper-order fix; full
    `ctest --test-dir build --output-on-failure` passed `34/34`.

## 2026-04-29 — Work Status Doc Consolidation

- Change made: removed duplicate human-facing status doc
  `work_concise.md` and kept `work_highlevel.md` as the single concise
  high-level view. Updated `work.md` to mark the current stopping
  point as the verified Zulrah/Vorkath ENCT v12 checkpoint and next
  pickup as Hydra/Nightmare/Muspah/DT2.
- Why it was made: `work_concise.md` and `work_highlevel.md` had
  effectively the same purpose. Keeping both created drift risk and
  made the tomorrow pickup path less clear.
- Exact surfaces changed: removed `work_concise.md`; updated
  `work.md`; updated this changelog entry.
- Upstream/downstream impacts: no runtime, exporter, generated data,
  or tests changed. Human-facing status now has one concise surface:
  `work_highlevel.md`. Detailed planning remains in `work.md`; detailed
  history remains in `changelog.md`.
- Verification: checked `git status --short --branch
  --untracked-files=all`; tracked tree remains clean on `testing_2`
  and ahead of `origin/testing_2` by `28` commits.

## 2026-04-29 — Generated Artifact and Stale Audit Cleanup

- Change made: removed generated coverage artifacts and stale audit
  scratch/report surfaces from the working tree.
- Why it was made: root `.gcov` files, Python bytecode caches, the old
  `build_cov/` tree, and the completed database-audit scratch/report
  were local working artifacts. Keeping them in the repo made status
  review noisier and risked confusing historical audit output with
  current source-of-truth docs.
- Exact surfaces changed: deleted ignored root `*.gcov` files, ignored
  `tools/__pycache__/`, ignored `tools/cache_pipeline/__pycache__/`,
  ignored `build_cov/`, ignored `temp_databaseaudit.md`, and tracked
  `tools/reports/database_audit.txt`.
- Upstream/downstream impacts: no runtime, exporter, binary, or test
  behavior changed. Current database status remains in `work.md`,
  `database.md`, reports under `tools/reports/`, and this changelog.
- Verification: scanned for `*.gcov`, `*.gcda`, `*.gcno`, `*.pyc`,
  `__pycache__`, `.pytest_cache`, temp, backup, and merge-artifact
  files after cleanup; no generated coverage/cache/temp artifacts
  remain. `git diff --check` passed.

## 2026-04-29 — ENCT v12 Zulrah and Vorkath First Runtime Slice

- Change made: upgraded `encounters.bin` to ENCT v12 and made the
  first Zulrah/Vorkath behavior slice executable through generic
  encounter data. Zulrah now has runtime form-dive effects, form-linked
  damage modifiers, halberd-only melee gating, damage cap rolls, and
  venom-on-hit. Vorkath now has alternating attack-count specials,
  Zombified Spawn freeze/damage-block state, acid-pool effects,
  acid-phase fireball markers, venomous dragonfire, and
  prayer-deactivation dragonfire.
- Why it was made: Zulrah/Vorkath were the next planned encounter
  batch after the GWD/DKS/Corp/Cerberus/Kraken exactness pass. Their
  authored TOMLs had mechanics that were still inert because primitive
  IDs `23-25`, venom/prayer-off attack effects, damage-cap modifiers,
  and spawn-blocking params were not encoded or consumed at runtime.
- Exact surfaces changed:
  - Updated `tools/export_encounters.py`; regenerated
    `data/defs/encounters.bin` and `tools/reports/encounters.txt`.
  - Updated `data/curated/encounters/zulrah.toml` and
    `data/curated/encounters/vorkath.toml`.
  - Updated `rc-core/encounter.h`, `rc-core/encounter.c`, and
    `rc-core/encounter_prims.c`.
  - Updated `tests/test_encounter_bin.c` and
    `tests/test_encounter_prims.c`.
  - Updated `work.md`, `work_highlevel.md`, `database.md`,
    `rc-core/README.md`, and
    `data/curated/encounters/_primitives.md`.
- Upstream/downstream impacts: ENCT v12 is now required for the new
  primitive params, extended spawn params, venom/prayer-off effects,
  and damage-cap rows. `rc_encounter_scale_player_damage()` now takes a
  mutable world pointer so damage-cap rolls can use the deterministic
  world RNG. Vorkath child attack-count mechanics are routed through
  the controller primitive to avoid double-firing. Existing ENCT v11
  data can still load, but must be regenerated to expose the new
  Zulrah/Vorkath fields.
- Verification: `python3 tools/export_encounters.py` encoded `50`
  encounters with `0` skipped and `0` warnings; `encounters.bin` is
  `23,770` bytes. `python3 -m py_compile tools/export_encounters.py`
  passed. `cc -std=c11 -fsyntax-only -Irc-core` passed for
  `rc-core/encounter.c` and `rc-core/encounter_prims.c`.
  `cmake --build build -j2` passed. Targeted encounter/combat CTest
  passed `5/5`. Full `ctest --test-dir build --output-on-failure`
  passed `34/34`. Coverage build full CTest passed `34/34`; `gcov`
  reported `encounter.c` `92.17%`, `encounter_prims.c` `95.55%`,
  `combat.c` `91.54%`, `tick.c` `86.18%`, and `92.11%` total for the
  checked files plus headers. Release benchmark comparison against
  commit `fbf00bf`: base tick `654,960,883` → `653,766,444` SPS,
  base load `3875.43` → `3904.09` loads/sec, combat load `2027.66` →
  `2007.31` loads/sec, skilling load `14.41` → `14.40` loads/sec,
  full load `153.87` → `153.44` loads/sec. The measured differences
  are within expected local benchmark noise.

## 2026-04-29 — ENCT v11 Encounter Presentation Effects and Corp Core Rolls

- Change made: upgraded `encounters.bin` to ENCT v11 and closed the
  requested GWD/DKS/Corp/Cerberus/Kraken follow-up. Runtime now records
  generic encounter presentation/effect state for travelling souls,
  lava pools, hidden whirlpool objects, and room-wide attacks. Corp
  dark-core spawn now uses the authored event roll: 1/8 after a 32+
  incoming hit or when Corp attacks below 1,000 HP; poisoned cores
  leech on the slower configured cadence.
- Why it was made: ENCT v10 made the mechanics executable, but four
  fidelity gaps remained: Cerberus soul/lava presentation, Kraken
  object-click reveal presentation, Corp dark-core exact random timing
  and poison weakening, and GWD room-wide side-effect state. The fix
  keeps `combat.c` generic by storing those effects on `RcWorld` and
  routing them through encounter/object APIs.
- Exact surfaces changed:
  - Updated `tools/export_encounters.py`; regenerated
    `data/defs/encounters.bin` and `tools/reports/encounters.txt`.
  - Updated `data/curated/encounters/corporeal_beast.toml`,
    `data/curated/encounters/cerberus.toml`, and
    `data/curated/encounters/kraken.toml`.
  - Updated `rc-core/types.h`, `rc-core/npc.c`,
    `rc-core/encounter.h`, `rc-core/encounter.c`,
    `rc-core/encounter_prims.c`, and `rc-core/tick.c`.
  - Updated `tests/test_encounter_bin.c` and
    `tests/test_encounter_prims.c`.
  - Updated `work.md`, `work_concise.md`, `work_highlevel.md`,
    `database.md`, `rc-core/README.md`, and
    `data/curated/encounters/_primitives.md`.
- Upstream/downstream impacts: ENCT v11 data is required for the new
  effect params and event-trigger encoding. Object interaction callers
  can reveal Kraken tentacles by clicking the authored whirlpool
  presentation effect before generic object-behavior fallback. Lava
  pool damage now ticks from persistent encounter effects instead of
  immediate one-shot primitive damage. Multiplayer room-wide fanout is
  still blocked on player-array runtime, but GWD attacks now emit a
  generic room-wide marker for that future consumer.
- Verification: `python3 -m py_compile tools/export_encounters.py`
  passed. `python3 tools/export_encounters.py` encoded `50`
  encounters with `0` skipped and `0` warnings; `encounters.bin` is
  `23,700` bytes. `cc -std=c11 -fsyntax-only -Irc-core` passed for
  `rc-core/encounter.c`, `rc-core/encounter_prims.c`,
  `rc-core/tick.c`, and `rc-core/npc.c`. `cmake --build build -j2`
  passed. Targeted encounter/combat CTest passed `5/5`. Full
  `ctest --test-dir build --output-on-failure` passed `34/34`.
  Coverage build full CTest passed `34/34`; `gcov` reported
  `encounter.c` `91.46%`, `encounter_prims.c` `95.05%`,
  `combat.c` `91.54%`, `tick.c` `86.18%`, `npc.c` `76.00%`, and
  `90.18%` total for checked files plus headers.
  `/tmp/runec_tick_bench` reported `base_tick_sps=271406326` for
  `10,000,000` ticks. `/tmp/runec_load_bench` reported
  `base_loads_per_sec=4265.34`, `combat_loads_per_sec=109.74`,
  `skilling_loads_per_sec=1.12`, and `full_loads_per_sec=1.08`.

## 2026-04-29 — ENCT v10 Hidden Minions, Leech Helpers, and Side Effects

- Change made: upgraded `encounters.bin` to ENCT v10 and deepened the
  current GWD/DKS/Corp/Cerberus/Kraken batch without adding
  boss-specific branches to core combat. Runtime now supports hidden
  NPC targetability/reveal state, Corp dark-core spawn/jump/leech/heal
  timing, Cerberus Ward of Arceuus and spectral spirit shield soul
  drain reductions, K'ril poison/prayer-drain attack effects, Kree
  knockback, and compact poison/prayer/knockback attack-effect
  encoding.
- Why it was made: ENCT v9 closed the first data path, but several
  authored mechanics were still only partially exact: dark core did not
  leech, hidden Kraken tentacles had no reveal semantics, Cerberus
  soul drains ignored Ward/Spectral reductions, and GWD side effects
  needed to execute through generic encounter effects rather than
  content-specific `combat.c` logic.
- Exact surfaces changed:
  - Updated `tools/export_encounters.py`; regenerated
    `data/defs/encounters.bin` and `tools/reports/encounters.txt`.
  - Updated `data/curated/encounters/corporeal_beast.toml`,
    `data/curated/encounters/cerberus.toml`, and
    `data/curated/encounters/kreearra.toml`.
  - Updated `rc-core/types.h`, `rc-core/npc.c`, `rc-core/combat.c`,
    `rc-core/encounter.h`, `rc-core/encounter.c`, and
    `rc-core/encounter_prims.c`.
  - Updated `tests/test_encounter_bin.c` and
    `tests/test_encounter_prims.c`.
  - Updated `rc-core/README.md`, `database.md`,
    `data/curated/encounters/_primitives.md`, `work.md`,
    `work_concise.md`, and `work_highlevel.md`.
- Upstream/downstream impacts: regenerated encounter data now requires
  ENCT v10 for the new param/effect fields. NPC targetability uses
  `player_untargetable` so zero-initialized test/local NPC structs
  remain targetable by default. Object/action UI can later call the
  generic hidden-NPC reveal API for Kraken whirlpools without changing
  combat. Ward of Arceuus has a core timer field, but the spell/action
  layer still owns setting that timer when casting is wired. Remaining
  known fidelity gaps are presentation/multiplayer/source-detail gaps:
  Cerberus travelling soul/lava objects, Kraken whirlpool click
  presentation, exact Corp dark-core random timing/poison weakness, and
  GWD multiplayer room-wide side effects.
- Verification: `python3 -m py_compile tools/export_encounters.py`
  passed. `python3 tools/export_encounters.py` encoded `50`
  encounters with `0` skipped and `0` warnings; `encounters.bin` is
  `23,685` bytes. `cc -std=c11 -fsyntax-only -Irc-core` passed for
  `rc-core/encounter.c`, `rc-core/encounter_prims.c`, and
  `rc-core/combat.c`. `cmake --build build -j2` passed. Targeted
  encounter/combat CTest passed `5/5`. Full
  `ctest --test-dir build --output-on-failure` passed `34/34`.
  Coverage build full CTest passed `34/34`; `gcov` reported
  `encounter.c` `91.32%`, `encounter_prims.c` `95.03%`,
  `combat.c` `91.54%`, `npc.c` `76.68%`, and `90.56%` total for the
  checked files plus headers. `/tmp/runec_tick_bench` reported
  `base_tick_sps=271576244` for `10,000,000` ticks.
  `/tmp/runec_load_bench` reported `base_loads_per_sec=4286.04`,
  `combat_loads_per_sec=107.20`, `skilling_loads_per_sec=1.12`, and
  `full_loads_per_sec=1.09`.

## 2026-04-29 — ENCT v9 Damage Modifiers, Attack Effects, and Kraken Targetability

- Change made: upgraded `encounters.bin` to ENCT v9 and added generic
  runtime support for encounter player-damage modifiers, compact
  attack on-hit effects, phase player-targetability, explicit empty
  phase attack masks, and group drop-source attribution params. The
  current GWD/DKS/Corp/Cerberus/Kraken exactness batch now covers DKS
  style locks, Corp/Kree weapon gates, Corp magic drain-heal, Corp
  split secondary hits, Kraken whirlpool-phase untargetability, and
  GWD/DKS `grant_drops_for` metadata.
- Why it was made: the first runtime slice still left several authored
  mechanics as inert TOML-only metadata. The fix keeps `combat.c`
  content-agnostic by routing player damage scaling, phase
  targetability, and attack effects through the encounter registry
  instead of adding boss branches to core combat.
- Exact surfaces changed:
  - Updated `tools/export_encounters.py`; regenerated
    `data/defs/encounters.bin` and `tools/reports/encounters.txt`.
  - Updated `data/curated/encounters/kraken.toml` with explicit
    `player_targetable` phase data.
  - Updated `rc-core/encounter.h`, `rc-core/encounter.c`,
    `rc-core/combat.h`, `rc-core/combat.c`, and `rc-core/types.h`.
  - Updated `tests/test_encounter_bin.c` and
    `tests/test_encounter_prims.c`.
  - Updated `rc-core/README.md`,
    `data/curated/encounters/_primitives.md`, local `database.md`,
    local `work.md`, local `work_concise.md`, and local
    `work_highlevel.md`.
- Upstream/downstream impacts: regenerated encounter data now requires
  ENCT v9 for phase targetability, explicit mask semantics, attack
  effects, and damage-mod rows. `combat.c` now asks the generic
  encounter layer whether an active phase can be player-targeted and
  scales player damage through compiled encounter rows. Pending hits
  gained a suppress-encounter-effects flag so secondary projectiles can
  resolve without recursively firing attack effects. Remaining
  fidelity gaps are state/presentation level: Corp dark-core
  adjacent-player leech/jump timing, Cerberus persistent soul/lava
  presentation and equipment-specific soul-drain reductions, Kraken
  whirlpool object click/reveal presentation, and GWD multiplayer
  knockback/room side effects.
- Verification: `python3 -m py_compile tools/export_encounters.py`
  passed. `python3 tools/export_encounters.py` encoded `50`
  encounters with `0` skipped and `0` warnings; `encounters.bin` is
  `23,685` bytes. `cc -std=c11 -fsyntax-only` passed for
  `rc-core/encounter.c` and `rc-core/combat.c`. `cmake --build build
  -j2` passed. Targeted encounter/combat CTest passed `5/5`. Full
  `ctest --test-dir build --output-on-failure` passed `34/34`.
  Coverage build full CTest passed `34/34`; `gcov` reported
  `encounter.c` `92.03%`, `combat.c` `91.50%`,
  `encounter_prims.c` `94.33%`, and `tick.c` `86.42%`.
  `/tmp/runec_tick_bench` reported `base_tick_sps=255803765` for
  `10,000,000` ticks. `/tmp/runec_load_bench` reported
  `base_loads_per_sec=4300.06`, `combat_loads_per_sec=105.32`,
  `skilling_loads_per_sec=1.10`, and `full_loads_per_sec=1.07`.

## 2026-04-29 — ENCT v5 and First GWD/DKS/Corp/Cerberus/Kraken Runtime Slice

- Change made: upgraded `encounters.bin` to ENCT v5, added
  owner-specific attack rows, encoded attack min-hit/flag metadata,
  moved initial phase-enter mechanics to first encounter tick to avoid
  same-event re-entry, and added generic runtime primitives for the
  first GWD/DKS/Corp/Cerberus/Kraken slice. Giant Mole exact burrow
  destinations were explicitly accepted as non-blocking source debt.
- Why it was made: the next encounter batch needed multi-NPC rooms and
  attack-counter mechanics to execute through data rather than
  hardcoded boss branches. Kraken exposed a real event-bus issue:
  spawning hidden minions inside `RC_EVT_NPC_SPAWNED` recursively fired
  the same event type, so first-phase mechanics now fire from the tick
  path instead of inside spawn dispatch.
- Exact surfaces changed:
  - Updated `tools/export_encounters.py`; regenerated
    `data/defs/encounters.bin` and `tools/reports/encounters.txt`.
  - Updated `data/curated/encounters/kril_tsutsaroth.toml`;
    `data/curated/encounters/giant_mole.toml` remains source-debt
    documented.
  - Updated `rc-core/encounter.h`, `rc-core/encounter.c`,
    `rc-core/encounter_prims.c`, and `rc-core/combat.c`.
  - Updated `tests/test_encounter_prims.c`.
  - Updated `rc-core/README.md`,
    `data/curated/encounters/_primitives.md`, local `database.md`,
    local `work.md`, local `work_concise.md`, and local
    `work_highlevel.md`.
- Upstream/downstream impacts: regenerated encounter data now requires
  ENCT v5 for owner NPC IDs, min-hit values, attack flags, and compact
  attack-counter params. GWD/DKS owner attacks and group kill/respawn
  are runtime-owned; Corp stomp/dark-core spawn, K'ril prayer-pierce
  cadence/drain, Cerberus triple/soul/lava/fire-line primitives, and
  Kraken tentacle gating now execute through generic encounter
  dispatch. Exact Corp leech/split/drain behavior, DKS damage
  modifiers, Cerberus persistent object state, and Kraken hidden-object
  reveal remain behavior-exactness gaps rather than binary/schema
  blockers.
- Verification: `python3 -m py_compile tools/export_encounters.py`
  passed. `python3 tools/export_encounters.py` encoded `50`
  encounters with `0` skipped and `0` warnings. `cmake --build build
  -j2` passed. Targeted CTest for encounter/combat passed `5/5`. Full
  `ctest --test-dir build --output-on-failure` passed `34/34`.
  Coverage build targeted CTests passed `7/7`; `gcov` reported
  `encounter.c` `91.65%`, `encounter_prims.c` `94.33%`,
  `combat.c` `84.48%`, and `tick.c` `28.81%`. `git diff --check`
  passed. `/tmp/runec_tick_bench` reported
  `base_tick_sps=251974015` for `10,000,000` ticks.
  `/tmp/runec_load_bench` reported `base_loads_per_sec=4246.86`,
  `combat_loads_per_sec=106.81`, `skilling_loads_per_sec=1.11`, and
  `full_loads_per_sec=1.08`.

## 2026-04-29 — Encounter Attack Tables, Protections, and Giant Mole Incoming-Hit Routing

- Change made: upgraded `encounters.bin` to ENCT v4, added
  data-driven phase attack-table/style-weight loading, added encounter
  protection rows consumed by player-hit protection resolution, and
  wired `RC_EVT_NPC_DAMAGED` so Giant Mole's incoming-hit burrow
  primitive can execute from the generic encounter runtime.
- Why it was made: the resumed wilderness/early-boss batch still had
  Obor/Bryophyta attack selection and Obor protection scaling outside
  executable data, and Giant Mole needed a payload-aware incoming-hit
  hook rather than a periodic/no-op primitive. The fix keeps
  `combat.c` generic: it consumes encounter-selected style/max-hit and
  damage-scaling rows without branching on boss names.
- Exact surfaces changed:
  - Updated `tools/export_encounters.py` and regenerated
    `data/defs/encounters.bin` plus `tools/reports/encounters.txt`.
  - Updated `data/curated/encounters/obor.toml` with adjacent and
    non-adjacent style weights, and corrected stale
    `data/curated/encounters/giant_mole.toml` comments.
  - Updated `rc-core/encounter.h`, `rc-core/encounter.c`,
    `rc-core/encounter_prims.c`, `rc-core/combat.c`,
    `rc-core/events.h`, and `rc-core/tick.c`.
  - Updated `tests/test_encounter_prims.c` and
    `tests/test_regular_npc_mechanics_combat.c`.
  - Updated `rc-core/README.md`, `data/curated/encounters/_primitives.md`,
    local `database.md`, local `work.md`, local `work_concise.md`,
    and local `work_highlevel.md`.
- Upstream/downstream impacts: encounter binaries are now ENCT v4;
  older ENCT v3 data still loads, but regenerated v4 data carries phase
  attack masks, adjacent/distant style weights, and protection rows.
  `combat.c` now asks the encounter runtime for optional attack style,
  max-hit, and prayer-scaling overrides before falling back to generic
  combat rules. Giant Mole burrow destination remains a local arena
  placeholder until exact lair floor anchors or live-capture data are
  sourced.
- Verification: `cmake --build build -j2` passed. Full
  `ctest --test-dir build --output-on-failure` passed `34/34`; the
  first full run exposed a regular-NPC test isolation/range regression,
  which was fixed before closure. Coverage build targeted CTests for
  encounter, combat, regular-NPC mechanics, area flags, activity
  schemas/spawns/states, and modular loading passed `10/10`; `gcov`
  reported `encounter.c` `77.91%`, `encounter_prims.c` `95.78%`,
  `combat.c` `84.39%`, `tick.c` `28.81%` overall with the new
  NPC-damage event lines executed, `area_flags.c` `89.95%`,
  `activity_schemas.c` `90.32%`, `activity_spawns.c` `96.50%`, and
  `activity_states.c` `97.32%`. `/tmp/runec_tick_bench` reported
  `base_tick_sps=268701836` for `10,000,000` ticks.
  `/tmp/runec_load_bench` reported `base_loads_per_sec=4271.76`,
  `combat_loads_per_sec=108.06`, `skilling_loads_per_sec=1.11`, and
  `full_loads_per_sec=1.07`.

## 2026-04-29 — Area Flag Wilderness-Level Consumer Correction

- Change made: corrected `area_flags.bin` wilderness-level rows to
  mirror the local b237 `proc,wilderness_level` clientscript, added
  `rc_wilderness_level_at()` as the runtime consumer, and updated
  loader regressions for the corrected area-row count.
- Why it was made: the provisional area-flag export only emitted odd
  wilderness-level strips, so level lookup could not return correct
  values for even-level tiles such as level 2. The runtime helper
  needed source-backed level values without hardcoding OSRS coordinate
  formulas inside `rc-core`.
- Exact surfaces changed:
  - Updated `tools/export_area_flags.py` and regenerated
    `data/defs/area_flags.bin` plus `tools/reports/area_flags.txt`.
  - Updated `rc-core/area_flags.h` and `rc-core/area_flags.c` with
    `rc_wilderness_level_at()`.
  - Updated `tests/test_area_flags_runtime.c` and
    `tests/test_modular_loading.c`.
  - Updated `rc-core/README.md`, local `database.md`, local
    `work.md`, local `work_concise.md`, and local
    `work_highlevel.md`.
- Upstream/downstream impacts: `area_flags.bin` grew from `531` rows /
  `43,676` bytes to `1,419` rows / `79,196` bytes because the
  clientscript-derived wilderness-level bands are now complete. All
  area rows remain `authoritative_osrs = false`; this is a provisional
  runtime consumer, not final geometry sign-off.
- Verification: `python3 tools/export_area_flags.py` regenerated the
  area catalog with `1,419` rows. `cmake --build build -j2` passed.
  Targeted `ctest --test-dir build -R test_area_flags_runtime
  --output-on-failure` passed `1/1`. Full `ctest --test-dir build
  --output-on-failure` passed `34/34` in `3.58s` after fixing stale
  row-count assertions exposed by the first full run. Coverage build
  targeted CTests for area/activity/modular paths passed `5/5`; `gcov`
  reported `area_flags.c` `89.95%`, `activity_schemas.c` `90.32%`,
  `activity_spawns.c` `96.50%`, and `activity_states.c` `97.32%`.
  `git diff --check` passed. `/tmp/runec_tick_bench` reported
  `base_tick_sps=270812055` for `10,000,000` ticks.
  `/tmp/runec_load_bench` reported `base_loads_per_sec=4187.15`,
  `combat_loads_per_sec=103.67`, `skilling_loads_per_sec=1.10`, and
  `full_loads_per_sec=1.07`.

## 2026-04-29 — Activity Schema Object IDs and Exact Spawn Consumers

- Change made: widened `activity_schemas.bin` to v2 so runtime schema
  rows expose object IDs, added activity-schema object lookup helpers,
  added exact wave-point NPC materialization and wave-region
  resolution for activity-local spawns, and made skilling activity
  object events reject unknown object-anchor keys when
  `activity_spawns.bin` is loaded. Typed activity runs now count unique
  boss deaths so duplicate `boss_dead` events cannot complete
  multi-boss or dual-boss activities.
- Why it was made: the schema/runtime layer already knew activity
  object counts and wave spawn rows, but runtime could not query exact
  object IDs, materialize one wave's NPCs directly, or resolve ordered
  Fight Cave-style wave/rotation region refs to their authored arena
  regions. Object-driven activity events also accepted arbitrary keys,
  which was too loose for parity once source-backed activity anchors
  exist. Multi-boss progress also counted repeated death events rather
  than unique boss IDs.
- Exact surfaces changed:
  - Updated `tools/export_activity_schemas.py` and regenerated
    `data/defs/activity_schemas.bin` plus
    `tools/reports/activity_schemas.txt`.
  - Updated `rc-core/activity_schemas.h` and
    `rc-core/activity_schemas.c` with schema v2 object-ID loading,
    `rc_activity_schema_has_object()`, and
    `rc_activity_schema_find_for_object()`.
  - Updated `rc-core/activity_spawns.h` and
    `rc-core/activity_spawns.c` with
    `rc_activity_spawn_materialize_wave_npcs()` and
    `rc_activity_spawn_wave_region()`.
  - Updated `rc-core/activity_states.c` so
    `rc_activity_run_object_event()` validates object-anchor keys when
    activity spawn data is available and `rc_activity_run_event()`
    tracks unique boss deaths.
  - Updated `rc-core/activity_states.h` with compact per-run dead-NPC
    tracking.
  - Updated `tests/test_activity_schemas_bin.c`,
    `tests/test_activity_spawns_runtime.c`, and
    `tests/test_activity_states_bin.c`.
  - Updated `rc-core/README.md`, local `database.md`,
    local `work.md`, local `work_concise.md`, and local
    `work_highlevel.md`.
- Upstream/downstream impacts: activity schema consumers can now route
  object-specific behavior by exact object ID instead of object-count
  summaries. Wave activities can spawn only the requested wave's
  source-backed NPC point rows and resolve wave-region references
  without inventing fake coordinates. Object-driven skilling activities
  keep lean behavior when spawn data is absent, but become stricter
  when the anchor index is loaded. Dual/multi-boss activity completion
  now depends on unique dead boss IDs instead of repeated events.
- Verification: `python3 tools/export_activity_schemas.py` regenerated
  66 schema rows with `0` `BLOCKS_PARITY` rows and increased
  `activity_schemas.bin` from `6769` to `6829` bytes. `cmake --build
  build -j2` passed. Targeted CTest for `test_activity_schemas_bin`,
  `test_activity_spawns_runtime`, and `test_activity_states_bin`
  passed `3/3`. Full `ctest --test-dir build --output-on-failure`
  passed `34/34` in `3.23s`. Coverage build targeted CTests passed
  `3/3`; `gcov` reported `activity_schemas.c` `90.32%`,
  `activity_spawns.c` `96.50%`, and `activity_states.c` `97.32%`.
  `git diff --check` passed. Temporary cold-load benchmark
  `/tmp/runec_load_bench` reported `base_loads_per_sec=4269.05`,
  `combat_loads_per_sec=105.12`, `skilling_loads_per_sec=1.19`, and
  `full_loads_per_sec=1.15`. Temporary headless tick benchmark
  `/tmp/runec_tick_bench` reported `base_tick_sps=230072707` for
  `10,000,000` ticks.

## 2026-04-29 — Runtime Owners for Remaining Database Lanes

- Change made: added runtime loaders/query APIs for NPC drop tables,
  shared RDT/GDT/MRDT tables, varbit/varp definitions plus per-world
  varp state, quest metadata, dialogue transcript trees, and sliced
  full-world NPC spawn loading. Wired the new loaders through
  `RcWorldConfig` under existing subsystem gates and extended the
  modular-loading regression to cover the newly owned lanes.
- Why it was made: compiled database parity was ahead of runtime
  readiness. The game/sim could compile these datasets but could not
  load or query them through `rc-core`, and full world spawns still
  required all-or-nothing loading rather than region/area selection.
- Exact surfaces changed:
  - Added `rc-core/drops.h`, `rc-core/drops.c`,
    `rc-core/varbits.h`, and `rc-core/varbits.c`.
  - Updated `rc-core/config.h` and `rc-core/config.c` with
    `varps_path` and `mrdt_path`, keeping base/combat presets lean and
    enabling varbit/varp defaults for full-game and skilling presets.
  - Updated `rc-core/world.c` to load drops/shared tables under
    `RC_SUB_LOOT`, quests under `RC_SUB_QUESTS`, dialogue under
    `RC_SUB_DIALOGUE`, skill drops under loot or skills, and
    varbit/varp state when configured.
  - Updated `rc-core/types.h` with fixed per-world varp storage.
  - Updated `rc-core/npc.h` and `rc-core/npc.c` with
    `rc_load_npc_spawns_rect()` and `rc_load_npc_spawns_near()`.
  - Expanded `rc-core/quests.h`, `rc-core/quests.c`,
    `rc-core/dialogue.h`, and `rc-core/dialogue.c` with loaders and
    compact query helpers.
  - Added `tests/test_drops_runtime.c`,
    `tests/test_varbits_runtime.c`,
    `tests/test_quests_dialogue_runtime.c`, and
    `tests/test_spawn_slices_runtime.c`; updated
    `tests/test_modular_loading.c`. `test_quests_dialogue_runtime`
    also covers malformed dialogue reloads preserving the last valid
    transcript index/string blob.
  - Updated `rc-core/README.md`, local `database.md`, local `work.md`,
    and local `work_concise.md`.
- Upstream/downstream impacts: full-game worlds now load all currently
  wired database owner lanes except explicit world NPC spawn slices.
  Skilling worlds now load varbit/varp definitions for object/state
  transforms; bare base and combat-only presets avoid that cost unless
  callers opt in. Drop tables are queryable by NPC ID, shared table
  entries are queryable by table kind, quest/dialogue corpora are
  runtime indexed, and world spawn loading can target a rectangle or
  center/radius slice without materializing every global NPC.
- Verification: `cmake -S . -B build` passed. `cmake --build build -j2`
  passed. Targeted CTest for `test_drops_runtime`,
  `test_varbits_runtime`, `test_quests_dialogue_runtime`,
  `test_spawn_slices_runtime`, and `test_modular_loading` passed `5/5`
  in `1.81s`. Full `ctest --test-dir build --output-on-failure`
  passed `34/34` in `3.31s`. Coverage build targeted CTests for
  `test_modular_loading`, `test_quests_dialogue_runtime`, and
  `test_varbits_runtime` passed `3/3`; `gcov` reported `dialogue.c`
  `84.32%` and `varbits.c` `89.38%`. Earlier owner-lane `gcov` checks
  reported `drops.c` `88.70%`, `quests.c` `81.25%`, `npc.c` `63.23%`,
  and `world.c` `93.84%`. `git diff --check` passed. Temporary
  cold-load benchmark `/tmp/runec_load_bench` reported
  `base_loads_per_sec=4347.02`, `combat_loads_per_sec=104.27`,
  `skilling_loads_per_sec=1.19`, and `full_loads_per_sec=1.15`.
  Temporary headless tick benchmark `/tmp/runec_tick_bench` reported
  `base_tick_sps=272688226` for `10,000,000` ticks.

## 2026-04-29 — Modular Dataset Loading Guardrail

- Change made: added a preset-level runtime regression proving that
  base, combat-only, skilling-only, and full-game worlds load only the
  datasets owned by their enabled subsystem bitmasks. Corrected the
  public config/docs contract so non-NULL default paths are treated as
  available loader inputs, not as forced loads for disabled subsystems.
- Why it was made: the repo needs modular game/sim bring-up without
  hidden "load everything" behavior. The old `RcWorldConfig` comment
  claimed strict path/bit matching that the presets never followed,
  and there was no single test preventing runtime-loader drift.
- Exact surfaces changed:
  - Updated `rc-core/config.h` loader-path contract comments.
  - Updated `rc-core/world.c` to explicitly document not-yet-owned
    runtime lanes: full world spawns, varbit state, drops, quests, and
    dialogue binary loading.
  - Added `tests/test_modular_loading.c`.
  - Updated `README.md`, `rc-core/README.md`, local `database.md`,
    local `work.md`, and local `work_concise.md`.
- Upstream/downstream impacts: enabled subsystem bits are now pinned as
  the runtime loading authority. Combat-only worlds load NPC/item/
  prayer/spell/mechanics/encounter support without loading skills,
  objects, regions, traversal, shops, or slayer. Skilling-only worlds
  load item/skill/object/region data without loading combat,
  encounter, traversal, shops, or slayer. Full-game currently loads all
  wired runtime owners, while compiled but unwired drop/quest/dialogue
  datasets remain explicit runtime-owner debt rather than hidden stubs.
- Verification: `cmake -S . -B build` passed. `cmake --build build -j2`
  passed. Targeted `ctest --test-dir build -R test_modular_loading
  --output-on-failure` passed `1/1` in `1.73s` after rerunning serially
  because an earlier parallel build/test invocation raced before the
  new executable finished linking. Full `ctest --test-dir build
  --output-on-failure` passed `30/30` in `3.13s`. Coverage build
  `ctest --test-dir build_cov -R test_modular_loading
  --output-on-failure` passed `1/1`; `gcov` reported `world.c`
  `92.68%` and `test_modular_loading.c` `100.00%`. `git diff --check`
  passed. Temporary cold-load benchmark `/tmp/runec_load_bench`
  reported `base_loads_per_sec=4200.25`, `combat_loads_per_sec=106.65`,
  `skilling_loads_per_sec=1.19`, and `full_loads_per_sec=1.18`.

## 2026-04-29 — Core Content-Hardcoding Cleanup

- Change made: removed the direct Tempoross/Wintertodt branches from
  typed activity runtime, replaced regular-NPC status/protection
  routing with exported mechanic tags, split poison/venom/disease into
  separate data families, made viewer region/assets/origin/player-start
  paths configurable by environment while keeping Varrock defaults, and
  moved cache-tool defaults from `/tmp` to local `data/source` paths.
- Why it was made: `rc-core` must stay component/content agnostic.
  Combat and activity runtime may consume generic tags, params,
  transitions, and IDs, but must not infer behavior from boss/NPC names
  or local machine paths. The viewer/exporter cleanup also keeps the
  repo self-contained instead of depending on another checkout.
- Exact surfaces changed:
  - Updated `.gitignore` to ignore coverage build artifacts.
  - Updated `data/curated/activity_state_machines.toml`.
  - Updated `data/curated/regular_npc_special_mechanics.toml`.
  - Regenerated `data/defs/activity_states.bin` and
    `data/defs/regular_npc_mechanics.bin`.
  - Updated `rc-core/activity_states.c`, `rc-core/combat.c`, and
    `rc-core/monster_mechanics.h`.
  - Updated `rc-viewer/viewer.c`, `rc-viewer/models.h`,
    `rc-viewer/objects.h`, `rc-viewer/terrain.h`, and
    `rc-viewer/README.md`.
  - Updated `tools/export_items.py`, `tools/xvalidate.py`, and
    `tools/export_regular_npc_mechanics.py`.
  - Updated `tests/test_activity_states_bin.c`,
    `tests/test_regular_npc_mechanics_bin.c`, and
    `tests/test_regular_npc_mechanics_combat.c`.
  - Updated `tools/reports/activity_states.txt` and
    `tools/reports/regular_npc_mechanics.txt`.
  - Updated local ignored status docs `work.md` and
    `work_concise.md`.
- Upstream/downstream impacts: `RcActivityRun` object events now use
  `object_resource_initial`, `object_resource_delta`, timer
  transitions, resource-zero transitions, and reward-role completion
  instead of per-activity name checks. Regular NPC status application
  now trusts poison/venom/disease tags; the exporter now classifies
  wiki poison/venom/disease strings separately so venom/disease are not
  over-applied to every poisonous NPC. Slayer protection routing now
  uses explicit protection-required tags instead of NPC display names.
  Viewer QA can now point at non-Varrock local slices through
  `RUNEC_*` environment variables without editing source.
- Verification: `python3 tools/export_activity_states.py` passed and
  exported `16` activity state-machine rows. `python3
  tools/export_regular_npc_mechanics.py` passed and exported `16`
  regular NPC mechanic families / `422` NPC links. `python3 -m
  py_compile tools/export_activity_states.py
  tools/export_regular_npc_mechanics.py tools/export_items.py
  tools/xvalidate.py` passed. `git diff --check` passed. `cmake
  --build build -j2` passed. Targeted CTest for
  `test_activity_states_bin`, `test_regular_npc_mechanics_bin`,
  `test_regular_npc_mechanics_combat`, and `test_base_only` passed
  `4/4`. Full `ctest --test-dir build --output-on-failure` passed
  `29/29`. Coverage build targeted CTests passed `4/4`; `gcov`
  reported `activity_states.c` `98.08%`, `combat.c` `73.64%`,
  `monster_mechanics.c` `87.14%`, and one-frame `rc-viewer` smoke
  brought `viewer.c` to `70.51%`. Viewer smoke command
  `timeout 8s env RC_VIEWER_EXIT_FRAMES=1 build_cov/rc-viewer` reached
  `Viewer ready`, loaded `15182` NPC defs, `235` Varrock spawns, and
  `116` visible-plane NPC models. Runtime benchmark reported
  `combat_rules_sps=6,322,580` and
  `activity_object_sps=472,371,733`.

## 2026-04-29 — Provisional Area Flags Runtime Dataset

- Change made: added `work_concise.md` for high-level human status,
  mirrored Near-Reality/Zenyte `MapLocations.java` into local ignored
  source inputs, exported provisional `area_flags.bin`, and added
  `rc-core` runtime loading/query support for area flags under
  `RC_SUB_REGIONS`.
- Why it was made: wilderness, multicombat, singles-plus, PvP safe
  zones, Deadman safe zones, and wilderness-level line checks were
  still schema/source blockers. No authoritative OSRS server geometry
  is available locally, so the project needed an explicit provisional
  dataset that is usable by runtime while preserving source authority
  boundaries.
- Exact surfaces changed:
  - Added `tools/export_area_flags.py`, `rc-core/area_flags.h`,
    `rc-core/area_flags.c`, and `tests/test_area_flags_runtime.c`.
  - Updated `tools/source_paths.py`, `rc-core/config.h`,
    `rc-core/config.c`, `rc-core/world.c`, and `rc-core/README.md`.
  - Generated `data/defs/area_flags.bin`: `531` polygon rows,
    `7,729` vertices, `43,676` bytes.
  - Added `tools/reports/area_flags.txt`.
  - Updated `tools/reports/area_flags_sources.txt`,
    `tools/reports/database_audit.txt`, local `database.md`, local
    `work.md`, and local `work_concise.md`.
  - Local ignored source mirror:
    `data/source/near_reality/MapLocations.java`.
- Upstream/downstream impacts: every exported row is marked
  `authoritative_osrs = false` and sourced from Near-Reality/Zenyte, so
  combat/content systems may consume it only as provisional data.
  `RcWorldConfig` now has `area_flags_path`, and full/skilling presets
  load `data/defs/area_flags.bin` when `RC_SUB_REGIONS` is enabled.
  Runtime rasterizes polygons into sparse mapsquare flag tiles at load
  time, so hot lookup is O(1). Final parity still requires
  authoritative OSRS server area data or live capture before promoting
  these rows.
- Verification: `python3 tools/export_area_flags.py` generated the
  binary/report. `cmake -S . -B build` was required after adding a new
  `rc-core/*.c` source; the pre-reconfigure build failed because
  `area_flags.c` was not yet in `librc-core.a`. After reconfigure,
  `cmake --build build -j2` passed. Targeted CTest for
  `test_area_flags_runtime` and `test_collision_tiles_runtime` passed
  `2/2`. Full `ctest --test-dir build --output-on-failure` passed
  `29/29`. Coverage build targeted tests passed `2/2`; `gcov`
  reported `area_flags.c` `89.95%` and `world.c` `52.03%`, with the
  new region-loader path exercised by `test_area_flags_runtime`.
  `python3 -m py_compile tools/export_area_flags.py tools/source_paths.py`
  passed. Area query benchmark improved from the initial polygon-query
  prototype at about `9,569,075` queries/sec to the rasterized runtime
  path at `305,092,209` queries/sec.

## 2026-04-29 — Object/Activity Consumer Runtime Pass

- Change made: added bounded world object state, retained object
  transform IDs in the object-definition loader, recorded door
  open/close state, added generic resource depletion/respawn with a
  next-respawn tick gate, expanded altar action handling, added
  activity object-anchor and region-containment lookup helpers, and
  wired simple object-driven Tempoross/Wintertodt resource events.
- Why it was made: object/activity data already existed, but runtime
  parity still had schema-only gaps. Doors, resources, special altar
  actions, and activity object anchors needed first consumers so the
  game/sim can use the database without per-call source parsing or
  inferred behavior.
- Exact surfaces changed:
  - Updated `rc-core/types.h`, `rc-core/objects.h`,
    `rc-core/objects.c`, `rc-core/api.h`, and `rc-core/tick.c`.
  - Updated `rc-core/activity_spawns.h`,
    `rc-core/activity_spawns.c`, `rc-core/activity_states.h`, and
    `rc-core/activity_states.c`.
  - Updated `tests/test_objects_runtime.c`,
    `tests/test_activity_spawns_runtime.c`, and
    `tests/test_activity_states_bin.c`.
  - Updated `rc-core/README.md`, local `work.md`, local
    `database.md`, `tools/export_object_defs.py`,
    `tools/export_object_behaviors.py`,
    `tools/export_object_placements.py`,
    `tools/export_gathering_nodes.py`, and related report text under
    `tools/reports/`.
  - No generated binary format was rewritten for this pass; the
    existing object-definition transform tail is now retained by the
    loader instead of discarded.
- Upstream/downstream impacts: `RcWorld` now owns up to
  `RC_MAX_OBJECT_STATES` transient object states and exposes
  `rc_world_object_active_id()`. Generic resource objects can deplete
  and respawn without scanning every object every tick. Doors now have
  recorded runtime state, but exact paired replacement geometry remains
  source-specific debt. Tempoross/Wintertodt activity state can consume
  object events. The verified Near-Reality/Zenyte area-geometry source
  should be promoted next into provisional `area_flags` rows with
  `authoritative_osrs = false`.
- Verification: initial `cmake --build build -j2` caught a missing
  `<stddef.h>` include in `tick.c`; after the include fix, the build
  passed. Targeted CTest for `test_objects_runtime`,
  `test_activity_spawns_runtime`, and `test_activity_states_bin` passed
  `3/3`. Full `ctest --test-dir build --output-on-failure` passed
  `28/28`. Coverage build targeted tests passed `3/3`; `gcov` reported
  `tick.c` `68.26%`, `objects.c` `87.95%`,
  `activity_spawns.c` `95.51%`, and `activity_states.c` `98.18%`.
  Focused object-state benchmark after the respawn gate reported
  `234256088` empty-world SPS and `237995733` SPS with `512` object
  states, compared with the pre-gate full-state result of about
  `2091780` SPS.

## 2026-04-29 — Provisional Nex/Sol Spawn Unblock

- Change made: verified the provisional source notes in
  `<local-notes>/osrs_parity_source_data_notes.md` against raw
  GitHub source files, then promoted Nex and Sol Heredit activity-local
  blockers into runtime-loadable provisional fixtures with
  `authoritative_osrs = false`.
- Why it was made: client cache, `data_osrs`, `osrsreboxed-db`, and
  RuneLite only proved IDs, object anchors, or non-NPC object-location
  rows. The project needed a safe provisional unblock so activity and
  encounter runtime work can continue before final server-authoritative
  OSRS sign-off is available.
- Exact surfaces changed:
  - Updated `data/curated/activity_spawns.toml`.
  - Regenerated `data/defs/activity_spawns.bin`: `117` rows,
    `0` unresolved required rows.
  - Regenerated `data/defs/spawn_sources.bin`: `46710` source rows,
    `0` unresolved required activity markers.
  - Regenerated `data/defs/activity_schemas.bin`: `66` schemas,
    `0` `BLOCKS_PARITY` rows.
  - Updated `tools/reports/activity_spawns.txt`,
    `tools/reports/spawn_sources.txt`,
    `tools/reports/activity_schemas.txt`,
    `tools/reports/area_flags_sources.txt`, and
    `tools/reports/database_audit.txt`.
  - Updated `tools/export_activity_spawns.py` report output to show
    source-status counts.
  - Updated `tests/test_activity_spawns_runtime.c`,
    `tests/test_activity_schemas_bin.c`, and
    `tests/test_spawn_sources_bin.c` to pin the new provisional rows.
  - Updated `tools/audit_npc_reconciliation.py` blocker text.
  - Updated local ignored docs `database.md` and `work.md`.
- Upstream/downstream impacts: Nex now has provisional activity spawn
  points for Nex/Fumus/Umbra/Cruor/Glacies. Sol Heredit now has a
  provisional wave-12 combat spawn and reduced-arena region rows.
  Activity schema/spawn reports no longer block on those rows, but the
  data remains non-authoritative and must be replaced or signed off
  from server-side configs or live runtime capture later. Area flags now
  have a verified provisional Near-Reality/Zenyte source target, but no
  `area_flags.bin` runtime dataset was generated.
- Verification: raw GitHub checks verified
  `clivester90/Vanguard-Public-Server` Nex positions,
  `jonathan-richer/DarkRealmMirror` Nex positions,
  `Bananastreet/Pyron-Server` Nex one-tile conflict, Okronos Sol
  spawn/bounds, and Near-Reality/Zenyte `MapLocations.java` area
  geometry. `python3 tools/export_activity_spawns.py`,
  `python3 tools/export_spawn_sources.py`, and
  `python3 tools/export_activity_schemas.py` passed. `cmake --build
  build -j2` passed. Targeted CTest for `test_activity_schemas_bin`,
  `test_activity_spawns_runtime`, and `test_spawn_sources_bin` passed
  `3/3`. Full `ctest --test-dir build --output-on-failure` passed
  `28/28`. A final targeted `test_activity_spawns_runtime` rerun passed
  after the report-only exporter update. `python3 -m py_compile` passed
  for the touched Python scripts. Coverage and SPS benchmarks were not run because no
  production runtime logic changed.

## 2026-04-29 — Nex/Sol Cache Authority Recheck

- Change made: rechecked the local b237 cache placement surface and
  cloned source databases for the carried Nex/Sol spawn blockers,
  recorded the source-authority result, and corrected the Sol throne
  object anchor to plane `1` while leaving gameplay marker/bounds rows
  on plane `0`.
- Why it was made: Nex/bodyguard spawn tiles and Sol wave-12
  spawn/reduced bounds were still blocked. The goal was to determine
  whether the client cache or cloned `data_osrs`/`osrsreboxed-db`
  mirrors could close those rows before moving on.
- Exact surfaces changed:
  - Updated `data/curated/activity_spawns.toml` source notes for Nex
    and Sol Heredit.
  - Regenerated `data/defs/activity_spawns.bin`.
  - Regenerated `data/defs/spawn_sources.bin`.
  - Updated `tools/reports/activity_spawns.txt`.
  - Updated `tools/reports/area_flags_sources.txt`.
  - Updated `tools/reports/database_audit.txt`.
  - Updated local ignored docs `database.md` and `work.md` with the
    source-authority boundary.
- Upstream/downstream impacts: b237 object placements are now trusted
  for static object anchors only. Overlapping numeric IDs in
  `data_osrs/locations` remain classified as object rows, not NPC
  spawns. RSMod confirms map NPC spawns and map areas are server-only
  groups, so the four Nex/Sol unresolved activity markers remain
  explicit blockers instead of being filled from inferred arena
  coordinates.
- Verification: `python3 tools/export_activity_spawns.py` exported
  `106` rows. `python3 tools/export_spawn_sources.py` exported
  `46699` source rows and retained `4` unresolved required markers.
  A direct `object_placements.bin` spot check confirmed Nex anchors
  and Sol pillars/throne, including throne `(1823, 3123, plane 1)`,
  while overlapping `11278`, `11284`, `11285`, `12821`, and `12827`
  hits are object placements. Targeted CTest for
  `test_activity_spawns_runtime` and `test_spawn_sources_bin` passed
  `2/2`. Full `ctest --test-dir build --output-on-failure` passed
  `28/28`. Coverage and SPS benchmarks were not run because no runtime
  code or logic changed.

## 2026-04-29 — Generic Altar and Gathering Object Consumers

- Change made: fixed object behavior export so `Pray-at` altar actions
  receive a valid action mask, regenerated `object_behaviors.bin`, and
  wired two generic object effects into `rc_player_interact_object_at()`:
  prayer altars restore current prayer points to base prayer level, and
  placed resource objects start gathering-node action state when the
  clicked object tile exists in `gathering_nodes.bin`.
- Why it was made: object capability data was present, but generic
  altar/resource interactions still stopped at "record object target".
  The `Pray-at` export bug also meant many altar rows could not be
  consumed through the action-mask gate.
- Exact surfaces changed:
  - Updated `tools/export_object_behaviors.py`.
  - Regenerated `data/defs/object_behaviors.bin`: `8112` behavior rows,
    `157` altar rows, `1509` resource rows.
  - Updated `tools/reports/object_behaviors.txt`.
  - Updated `rc-core/tick.c` object interaction dispatch.
  - Updated `tests/test_objects_runtime.c` for altar restore,
    gathering-node action start, and updated altar action-mask coverage.
  - Updated `rc-core/README.md`, `database.md`, `work.md`, and
    `tools/reports/database_audit.txt`.
  - Updated `tools/reports/area_flags_sources.txt` with the latest web
    source check for OSRS Wiki multicombat/Wilderness/Sol tile-marker
    surfaces.
- Upstream/downstream impacts: generic altars and placed resource nodes
  now have first runtime consumers. Resource depletion/respawn, success
  rolls, tool/level gates, special altar variants, and door open/close
  transforms still require source-backed per-skill/object rules before
  they can be exact. Nex/Sol exact spawn/bounds and static area flags
  remain source-authority blockers; no strategy markers or helper
  rectangles were promoted to parity data.
- Verification: `python3 tools/export_object_behaviors.py` regenerated
  the binary/report. `cmake --build build -j2` passed. Targeted CTest
  for `test_objects_runtime`, `test_skills_runtime`,
  `test_prayer_spell_actions_runtime`, and `test_traversal_runtime`
  passed `4/4`. Full `ctest --test-dir build --output-on-failure`
  passed `28/28`. Coverage build in `/tmp/runec_cov_object_effects`
  passed targeted and full CTest; `gcov` reported `tick.c` at `86.84%`,
  `skills.c` at `88.97%`, `prayer.c` at `91.55%`, and `objects.c` at
  `88.43%`. The new altar and gathering branches executed. Benchmark on
  this machine: generic object interaction `289104975 ops/sec`, altar
  restore `215992770 ops/sec`, resource action start `82044697 ops/sec`,
  object transport `35831158 ops/sec`.

## 2026-04-29 — Exact Object Transport Consumer and Blocker Recheck

- Change made: added `rc_player_interact_object_at()` as the
  coordinate-explicit object interaction API. It validates the existing
  player action gate and object behavior/action mask, preserves storage
  handling, records generic object interaction state, and applies a
  matching `traversal_edges.bin` object edge only when exact object
  x/y/plane is supplied and `RC_SUB_TRAVERSAL` is enabled. The legacy
  `rc_player_interact_object()` API now delegates to the same code with
  no coordinates, so it remains a generic non-teleporting compatibility
  path.
- Why it was made: object transport data, object behavior tags, and
  traversal edges were already exported/loadable, but exact runtime
  consumption was blocked because the old object-interaction API only
  carried object ID + option and could not select source-tile-specific
  ladders/stairs/doors without guessing.
- Exact surfaces changed:
  - Updated `rc-core/api.h` with `rc_player_interact_object_at()`.
  - Updated `rc-core/tick.c` object interaction dispatch to consume
    source-backed traversal edges.
  - Updated `tests/test_objects_runtime.c` with exact object transport
    movement, subsystem-gated disabled behavior, and legacy compatibility
    coverage.
  - Updated `rc-core/README.md`, `database.md`, `work.md`,
    `tools/reports/database_audit.txt`, and
    `tools/reports/area_flags_sources.txt`.
  - Rechecked source authority for unresolved Nex/Sol spawns and static
    area flags; no generated binary changed.
- Upstream/downstream impacts: action systems that know the clicked
  object tile can now perform exact object transport movement without
  reparsing source corpora. Door transforms, altar effects, resource
  depletion/respawn, and activity-specific object/arena consumers remain
  focused runtime work. Nex boss/bodyguard tiles, Sol Heredit wave-12
  spawn/reduced bounds, and authoritative static area flags remain
  blocked; no inferred coordinates or helper-rectangle area flags were
  added.
- Verification: `cmake --build build -j2` passed. Targeted CTest for
  `test_objects_runtime`, `test_traversal_runtime`, and
  `test_shops_storage_runtime` passed `3/3`. Full
  `ctest --test-dir build --output-on-failure` passed `28/28`.
  Coverage build in `/tmp/runec_cov_object_consumer` passed targeted and
  full CTest; `gcov` reported `tick.c` at `84.85%`,
  `traversal.c` at `95.88%`, and `objects.c` at `88.43%`. The new
  coordinate-explicit branch and traversal-apply branch executed.
  Benchmark on this machine: legacy generic object interaction
  `278938091 ops/sec`; exact object transport interaction
  `36480243 ops/sec`.

## 2026-04-28 — Activity-Local Spawn Runtime Ownership

- Change made: added `activity_spawns.bin` as a separate encounter
  runtime dataset for activity-local points, regions, dynamic spawn
  pools, wave spawn refs, object anchors, safe tiles, and explicit
  unresolved source blockers. `RC_SUB_ENCOUNTER` now loads it through
  `RcWorldConfig`, runtime can query by activity/key, and authored NPC
  point rows can be materialized when an activity starts.
- Why it was made: `activity_schemas.bin` was intentionally only a
  summary/index surface. Exact activity-local coordinates and anchors
  needed their own compact runtime owner so Inferno/Fight Cave/Zulrah/
  Tempoross/Wintertodt-style isolated sims do not depend on static
  world spawns or source TOML parsing.
- Exact surfaces changed:
  - Added `tools/export_activity_spawns.py`.
  - Generated `data/defs/activity_spawns.bin`: `106` rows across
    `7` activities.
  - Added `tools/reports/activity_spawns.txt`.
  - Added `rc-core/activity_spawns.{h,c}` with load, lookup,
    unresolved-check, kind-count, and NPC materialization APIs.
  - Updated `rc-core/config.{h,c}` and `rc-core/world.c` for
    `activity_spawns_path` bring-up under `RC_SUB_ENCOUNTER`.
  - Added `tests/test_activity_spawns_runtime.c` and updated
    `tests/test_base_only.c`.
  - Updated `rc-core/README.md`, `database.md`, `work.md`,
    `tools/reports/database_audit.txt`, and
    `tools/reports/area_flags_sources.txt`.
  - Regenerated report counts: `31` point rows, `6` region rows,
    `6` dynamic rows, `9` wave-point rows, `15` wave-region refs,
    `28` object anchors, `7` safe tiles, and `4` unresolved required
    Nex/Sol rows.
- Upstream/downstream impacts: activity-local/dynamic spawn runtime
  ownership is no longer a blocker. Encounter/activity startup code can
  consume a compact binary instead of reparsing curated TOML. Exact Nex
  boss/bodyguard tiles and Sol Heredit wave-12 spawn/reduced bounds
  remain blocked because local source and web/API checks expose IDs,
  anchors, and strategy metadata but not authoritative runtime tiles.
  Area flags remain blocked because current sources expose client
  varbits/helper rectangles, not authoritative static geometry.
- Verification: `python3 -m py_compile tools/export_activity_spawns.py`
  passed. `python3 tools/export_activity_spawns.py` regenerated the
  binary/report. Targeted build and CTest for
  `test_activity_spawns_runtime` and `test_base_only` passed after
  fixing a real exporter/runtime string-width mismatch found by the
  test. Full `ctest --test-dir build --output-on-failure` passed
  `28/28`. Coverage build in
  `/tmp/runec_cov_activity_spawns_20260428` passed
  `test_activity_spawns_runtime` and `test_base_only`; `gcov` reported
  `activity_spawns.c` at `96.27%` and `world.c` at `73.33%`.
  Uncovered changed lines are allocation failure and truncated-row
  defensive branches. Benchmark on this machine: loaded `106` rows in
  `0.000072483s`, lookup ran at `45319987 ops/sec`, and Inferno wave
  materialization ran at `1036495 runs/sec`.

## 2026-04-28 — Activity Schema and Normalization Runtime Indexes

- Change made: added two final database-completion runtime indexes.
  `activity_schemas.bin` now summarizes authored encounters, typed
  activity state machines, and activity-local arena/wave/object spawn
  schemas. `normalization.bin` now centralizes item form links, NPC
  display-name alias groups, and acquisition-source normalized-name
  hashes. Both load through `RcWorldConfig` and have focused runtime
  lookup APIs.
- Why it was made: the database audit still had two cross-cutting gaps:
  activity metadata for waves/rooms/rewards/requirements/completion/
  instances was spread across encounter/state/spawn TOMLs, and
  canonical item/NPC/source-name handling was still exporter-specific.
  These indexes make those surfaces loadable by isolated game/sim
  configurations without reparsing source corpora.
- Exact surfaces changed:
  - Added `tools/export_activity_schemas.py`; generated
    `data/defs/activity_schemas.bin` and
    `tools/reports/activity_schemas.txt`.
  - Added `rc-core/activity_schemas.{h,c}` and
    `tests/test_activity_schemas_bin.c`.
  - Added `tools/export_normalization.py`; generated
    `data/defs/normalization.bin` and
    `tools/reports/normalization.txt`.
  - Added `rc-core/normalization.{h,c}` and
    `tests/test_normalization_runtime.c`.
  - Updated `rc-core/config.{h,c}`, `rc-core/world.c`, and
    `tests/test_base_only.c` for `activity_schemas_path` and
    `normalization_path` bring-up.
  - Updated `rc-core/README.md`, `database.md`, `work.md`, and
    `tools/reports/database_audit.txt`.
  - `activity_schemas.bin`: `66` rows, `50` encounter-backed,
    `16` state-machine-backed, `7` spawn/arena-backed, `2`
    BLOCKS_PARITY rows (`nex`, `fortis_colosseum_sol_heredit`).
  - `normalization.bin`: `30197` item rows, `15182` NPC rows, and
    `5163` acquisition-source name rows.
- Upstream/downstream impacts: encounter/activity consumers can now
  query a compact activity schema index before dispatching exact
  behavior. Item, loot, shop, storage, and NPC/data joins can share
  canonical form/name hashes while preserving exact runtime IDs. Nex
  and Sol Heredit still need authoritative runtime spawn/bounds
  extraction. Exact per-activity attack/object consumers remain
  downstream behavior work where indexes/state flow are not enough.
- Verification: regenerated both binaries with
  `python3 tools/export_activity_schemas.py` and
  `python3 tools/export_normalization.py`. `cmake --build build`
  passed. Full CTest passed `27/27`. Coverage build in
  `/tmp/runec_cov_activity_norm` passed `test_activity_schemas_bin`,
  `test_normalization_runtime`, and `test_base_only`; `gcov` reported
  `activity_schemas.c` at `91.51%`, `normalization.c` at `92.17%`, and
  `world.c` at `72.65%`. Remaining uncovered changed paths are
  defensive malformed/truncated binary and allocation/open-failure
  branches. Benchmark on this machine: loaded `66` activity schemas and
  `50542` normalization rows in `0.002400s`; activity schema NPC lookup
  `874181641 ops/sec`; item normalization `672552266 ops/sec`; NPC
  normalization `588391072 ops/sec`.

## 2026-04-28 — Skills, Gathering, Recipes, and Production Runtime Ownership

- Change made: added a first-class object-backed gathering-node export
  and wired the skills subsystem to load recipes, skill-drop sources,
  and gathering nodes. `RC_SUB_SKILLS` now owns recipe lookup, skill
  drop source lookup, gathering node lookup, recipe requirement checks,
  and a minimal recipe completion helper that consumes materials,
  adds output, grants integer XP, and records skill action/timer state.
- Why it was made: the skills / gathering / recipes lane had source
  data (`recipes.bin`, `skill_drops.bin`, object behavior resource
  tags) but no runtime owner for production-chain checks, no direct
  gathering-node index, and no world bring-up loading for the compiled
  skilling datasets.
- Exact surfaces changed:
  - `tools/export_gathering_nodes.py` added GNOD v1 export derived from
    `data/defs/object_behaviors.bin` and
    `data/defs/object_placements.bin`.
  - `data/defs/gathering_nodes.bin` generated `34096` nodes:
    `24456` woodcutting, `3526` mining, `30` fishing, and `6084`
    farming nodes.
  - `tools/reports/gathering_nodes.txt` records source files, counts,
    top regions, and accepted simplifications.
  - `rc-core/skills.{h,c}` added recipe, skill-drop, and gathering-node
    structs/loaders/lookups plus recipe requirement/application helpers.
  - `rc-core/config.{h,c}` and `rc-core/world.c` added
    `gathering_nodes_path` and load `recipes.bin`, `skill_drops.bin`,
    and `gathering_nodes.bin` when `RC_SUB_SKILLS` is enabled.
  - `rc-core/api.h` exposes `rc_player_apply_recipe()`.
  - `tests/test_skills_runtime.c` covers loader failures, recipe
    lookup/application, skill-drop lookup, gathering-node lookup,
    subsystem gating, tool/level/material checks, XP, and action/timer
    state.
  - `tests/test_base_only.c`, `rc-core/README.md`, `database.md`,
    `work.md`, and `tools/reports/database_audit.txt` now reflect the
    skills runtime ownership boundary.
- Upstream/downstream impacts: object behavior and placement exports now
  feed a compact skilling node index for headless sims. Recipes can be
  used by production-chain consumers without reparsing wiki/cache data.
  Exact facility validation, animations, success rolls, depletion,
  respawn, hunter targets, stalls, activity-only nodes, and full
  per-skill state machines remain downstream skill/activity runtime
  work rather than database blockers.
- Verification: `python3 -m py_compile tools/export_gathering_nodes.py
  tools/export_recipes.py tools/export_skill_drops.py` passed.
  `python3 tools/export_gathering_nodes.py` regenerated
  `gathering_nodes.bin` and its report. `cmake --build build -j2`
  passed. Full CTest passed `25/25`. Coverage build in
  `/tmp/runec_cov_skills` passed `test_base_only`,
  `test_objects_runtime`, and `test_skills_runtime`; `gcov` reported
  `skills.c` at `88.97%`, `objects.c` at `88.43%`, and `world.c` at
  `60.91%`. Uncovered changed paths are allocation-failure,
  malformed/truncated binary, and defensive over-cap branches.
  Benchmark on this machine: loaded recipes, skill drops, and gathering
  nodes in `0.014143s`; recipe lookup `810255501 ops/sec`;
  gathering-node lookup `35779623 ops/sec`; recipe requirement check
  `13808595 ops/sec`.

## 2026-04-28 — Transportation and Traversal Runtime Ownership

- Change made: added a unified traversal-edge export and runtime owner.
  Object transports, item teleports, and spell teleports now compile
  into `traversal_edges.bin`; `RC_SUB_TRAVERSAL` loads the graph during
  world bring-up, exposes indexed source lookups, and provides a simple
  player relocation helper gated by the traversal subsystem.
- Why it was made: the transportation / traversal database lane still
  had broad source data but no unified runtime dataset. Tile
  pathfinding could move locally, but ladders, stairs, portals, item
  teleports, spell teleports, boats, shortcuts, and similar edges were
  not represented as loadable traversal data.
- Exact surfaces changed:
  - `tools/export_traversal_edges.py` added TRAV v1 export from
    `data/source/data_osrs/transports_osrs.json` and
    `data/source/data_osrs/teleports_osrs.json`.
  - `data/defs/traversal_edges.bin` generated `45771` edges:
    `29959` object, `13091` item, and `2721` spell edges.
  - `tools/reports/traversal_edges.txt` records counts, source files,
    top actions, and accepted simplifications.
  - `rc-core/traversal.{h,c}` added the loader, dense
    source-kind/source-id index, lookup helpers, target lookup helper,
    and player apply helper.
  - `rc-core/config.{h,c}`, `rc-core/world.c`, and `rc-core/api.h`
    added `RC_SUB_TRAVERSAL`, default path wiring, world bring-up, and
    public API exposure.
  - `tests/test_traversal_runtime.c` added loader, bad-header,
    truncated-row, indexed lookup, target lookup, subsystem-gated
    apply, and disabled-subsystem checks.
  - `tests/test_base_only.c` now checks traversal preset bit behavior.
  - `rc-core/README.md`, `database.md`, `work.md`, and
    `tools/reports/database_audit.txt` now document traversal runtime
    ownership and mark the lane complete with accepted simplifications.
- Upstream/downstream impacts: object-specific transport lookup remains
  available through `object_transports.bin`; the unified traversal graph
  is the broader object/item/spell destination index for movement,
  item, magic, and action consumers. Requirements, charges, rune costs,
  diaries, quest gates, UI destination selection, and instance-local
  dynamic traversal stay with their owning systems instead of being
  folded into the traversal index.
- Verification: `cmake --build build -j2` passed. Full CTest passed
  `24/24`. Coverage build in `/tmp/runec_cov_traversal` passed
  `test_base_only`, `test_objects_runtime`, and
  `test_traversal_runtime`; `gcov` reported `traversal.c` at `95.88%`,
  `objects.c` at `88.43%`, and `world.c` at `61.17%`. The remaining
  uncovered traversal path is allocation failure. Benchmark on this
  machine: loaded `45771` edges in `0.006856s`; indexed object lookup
  ran at `20800091 ops/sec`; `rc_player_apply_traversal()` ran at
  `578497614 ops/sec`.

## 2026-04-28 — Shops, Banks, and Storage Runtime Ownership

- Change made: wired `shops.bin` into `rc-core`, replaced the fixed
  32-shop stub surface with dynamic shop/stock loading, fixed shop
  metadata export so sparse duplicate wiki rows no longer erase
  owner/location/specialty fields, added storage behavior tagging for
  bank/deposit/storage objects, and added `RC_SUB_STORAGE` with
  object-backed storage open state plus basic bank deposit/withdraw
  helpers.
- Why it was made: the shops / banks / storage database lane was still
  a runtime blocker: the compiled shop dataset existed, but the engine
  did not load it, shop capacity was undersized, and `RcPlayer` had no
  bank state despite `RC_BANK_SIZE` existing.
- Exact surfaces changed:
  - `tools/export_shops.py` now merges duplicate metadata rows,
    strips wiki markup from runtime labels, regenerates
    `data/defs/shops.bin`, and writes `tools/reports/shops.txt`.
  - `tools/export_object_behaviors.py`, `rc-core/objects.h`,
    `data/defs/object_behaviors.bin`, and
    `tools/reports/object_behaviors.txt` now include storage behavior
    flags.
  - `rc-core/shops.{h,c}` now load and query shop rows and stock rows.
  - `rc-core/storage.{h,c}`, `rc-core/config.{h,c}`,
    `rc-core/types.h`, `rc-core/world.c`, `rc-core/tick.c`, and
    `rc-core/api.h` add `RC_SUB_STORAGE`, per-player bank slots,
    storage-open state, object-backed storage routing, and bank
    deposit/withdraw helpers.
  - `tests/test_shops_storage_runtime.c` covers the new loader and
    runtime paths; `tests/test_base_only.c` covers the new subsystem
    preset bit.
  - `rc-core/README.md`, `database.md`, `work.md`, and
    `tools/reports/database_audit.txt` now reflect that this lane has
    runtime ownership with accepted downstream simplifications.
- Upstream/downstream impacts: cached OSRS Wiki `infobox_shop` and
  `storeline` data now flow into runtime-accessible shop rows; object
  behavior export now marks storage access for bank/deposit/storage
  consumers. Full buy/sell economics, per-world shop stock mutation,
  bank tabs/placeholders, bank PIN, collection-box claims, and UI
  panels remain downstream transaction/UI work.
- Verification: `python3 -m py_compile tools/export_shops.py
  tools/export_object_behaviors.py` passed. Regenerated
  `shops.bin`, `object_behaviors.bin`, and their reports.
  `cmake --build build -j2` passed. Full CTest passed `23/23`.
  Coverage build in `/tmp/runec_cov_shops_storage` passed
  `test_base_only`, `test_objects_runtime`, and
  `test_shops_storage_runtime`; `gcov` reported `shops.c` `84.04%`,
  `storage.c` `94.74%`, `objects.c` `88.43%`, `world.c` `66.00%`,
  and `tick.c` `39.56%`. Uncovered changed paths are malformed /
  truncated binary, allocation-failure, and broader pre-existing tick
  branches not specific to this lane. Benchmark on this machine:
  runtime load `0.030863s` for shops/object defs/object behaviors/items,
  shop+storage lookup `64339559 ops/sec`, and bank withdraw+deposit
  `66879712 ops/sec`.

## 2026-04-28 — Prayer, Spellbook, Player-Action Data, and Area-Flag Source Pass

- Change made: added first-class prayer definitions, upgraded spell and
  teleport binaries to v2 with combat max-hit/effect hints, added a
  player-action gate dataset, and wired all three through `rc-core`
  config/world bring-up. Prayer toggles now use data-backed IDs,
  levels, drain, conflict groups, and melee/ranged/magic boosts;
  selected spell max-hit can feed player magic combat. The second
  area-flag source pass kept `area_flags.bin` blocked because local
  sources still expose only client state varbits and broad helper
  rectangles, not authoritative static geometry. Committed the tracked
  code/data/report checkpoint as `5a257b8`.
- Why it was made: the database completion lane needed prayer,
  spellbook, and player-action data to be loadable by isolated runtime
  presets instead of remaining metadata-only exports or hardcoded
  prayer bits. Area flags needed a source-authority decision before any
  generated geometry could be trusted.
- Exact surfaces changed:
  - `tools/export_prayers.py` added PRAY v1 export; generated
    `data/defs/prayers.bin` and `tools/reports/prayers.txt`.
  - `tools/export_spells.py` now emits SPEL/TELE v2 rows with `max_hit`
    and `effect_flags`; regenerated `data/defs/spells.bin`,
    `data/defs/teleports.bin`, and `tools/reports/spells.txt`.
  - `tools/export_player_actions.py` added PACT v1 export; generated
    `data/defs/player_actions.bin` and
    `tools/reports/player_actions.txt`.
  - `rc-core/prayer.{h,c}`, `spells.{h,c}`, `player_actions.{h,c}`,
    `config.{h,c}`, `world.c`, `tick.c`, `combat.c`, `api.h`, and
    `types.h` now load and consume the new runtime data.
  - `tests/test_prayer_spell_actions_runtime.c` added loader,
    action-gate, prayer toggle/drain/boost, selected-spell, and magic
    combat coverage.
  - `tools/reports/area_flags_sources.txt`,
    `tools/reports/database_audit.txt`, `database.md`, `work.md`, and
    `rc-core/README.md` updated to reflect the completed lane and the
    area-flag source blocker.
  - `work.md` now records the 2026-04-28 stop point and the next
    pickup: shops / banks / storage runtime ownership.
- Upstream/downstream impacts: RuneLite prayer/varbit source, cached
  OSRS Wiki spell rows, and local Void magic spell data now feed
  generated runtime binaries. Combat, prayer, and input dispatch can
  rely on loaded data in isolated sims; exact spell effects, rune
  consumption, prayer unlock state, and per-action effects remain owned
  by their focused runtime systems rather than a generic action layer.
- Verification: `python3 -m py_compile tools/export_prayers.py
  tools/export_spells.py tools/export_player_actions.py` passed.
  Regenerated all three reports/binaries. Full build passed and full
  CTest passed `22/22`. Targeted coverage build passed
  `test_base_only`, `test_combat`, `test_combat_e2e`,
  `test_objects_runtime`, and `test_prayer_spell_actions_runtime`;
  `gcov` reported `prayer.c` `91.55%`, `spells.c` `83.08%`,
  `player_actions.c` `86.67%`, `tick.c` `83.33%`, `world.c`
  `80.00%`, and `combat.c` `30.13%` because combat is a large file
  with many unrelated existing branches. Benchmarks on this machine:
  prayer drain `6157897 ops/s`, action/spell lookup `307683924 ops/s`.
  No before/after regression comparison exists because these runtime
  surfaces did not previously exist.

## 2026-04-27 — Full-World Collision Tile Catalog and Runtime Lookup

- Change made: added a source-backed sparse collision-tile catalog and
  shared `rc-core` collision runtime owner. `RC_SUB_REGIONS` now loads
  `collision_tiles.bin`, expands sparse rows into dense shared lookup
  tables, and `rc_get_flags()` falls back from explicitly loaded
  `RcWorldMap` regions to the shared full-world collision catalog.
- Why it was made: collision parity could not stay tied to Varrock-only
  `.cmap` files or viewer-owned region loading. Missing map regions
  previously degraded into walkable space; with the catalog loaded,
  missing mapsquares are treated as blocked instead of free traversal.
- Exact surfaces changed:
  - `tools/export_collision_tiles.py` added CTIL v1 export from the
    local b237 cache and `object_defs.bin`.
  - `data/defs/collision_tiles.bin` generated `4826105` non-zero
    collision rows across `2041` regions from `2870` cache map groups,
    with `0` parse/read errors.
  - `tools/reports/collision_tiles.txt` added collision row, plane,
    object-marking, and source counts.
  - `rc-core/collision.{h,c}` added global shared collision loading and
    indexed tile lookup.
  - `rc-core/pathfinding.c` now checks loaded `RcWorldMap` regions first,
    then the shared collision catalog, and blocks invalid/unknown tiles
    when collision data is loaded.
  - `rc-core/config.{h,c}` added `RC_SUB_REGIONS` and
    `collision_tiles_path`; full-game and skilling presets include it,
    combat-only remains lean.
  - `rc-core/world.c` loads collision data during configured world
    bring-up.
  - `tests/test_collision_tiles_runtime.c` added loader, bad-header,
    known-tile, pathfinding fallback, and world-config coverage.
  - `tests/test_base_only.c`, `rc-core/README.md`, `database.md`,
    `work.md`, `temp_databaseaudit.md`, and
    `tools/reports/database_audit.txt` were updated for collision
    runtime ownership and the remaining area-flag/render-terrain
    boundary.
  - `tools/reports/area_flags_sources.txt` added the area-flag source
    review and explicitly blocks `area_flags.bin` until authoritative
    shapes are extracted or curated with provenance.
- Upstream/downstream impacts: movement/pathfinding can now use
  whole-world cache-derived collision without depending on viewer region
  files. Area semantics are still separate downstream work because the
  checked local sources expose runtime varbits and rough helper bounds,
  not authoritative static shapes.
- Verification: `python3 -m py_compile tools/export_collision_tiles.py`
  passed. Full CTIL export regenerated the binary/report with zero
  parse/read errors. Full build passed and full CTest passed `21/21`.
  Python trace coverage ran the same exporter path in bounded mode with
  `--limit-regions 8` and showed no uncovered lines in
  `export_collision_tiles.py`; the full unbounded trace was killed after
  ~90s because instrumentation over all cache groups was impractical.
  C coverage build ran `test_base_only`, `test_collision_tiles_runtime`,
  and `test_pathfinding`; `gcov` reported `collision.c` at `82.35%`
  line coverage and `pathfinding.c` at `83.85%`. Uncovered new
  collision lines are malformed/truncated binary and allocation failure
  exits; uncovered pathfinding lines are pre-existing LOS and
  alternative-route branches. Benchmarks: CTIL export `13.45s` /
  `604672 KB` max RSS; runtime collision load `0.200352s`; collision /
  `rc_get_flags` / `rc_can_move` lookup loop `231606640 ops/sec`.

## 2026-04-27 — Object Behavior Runtime and Transport Edges

- Change made: added typed object behavior export/runtime ownership and
  source-backed object transport-edge export. `RC_SUB_OBJECTS` now loads
  object definitions, behavior rules, placements, and transports, and
  `rc_player_interact_object()` records only valid object/action inputs
  for later movement/action/skill/storage consumers.
- Why it was made: the object lane needed to move past raw definition
  and placement catalogs into gameplay-usable interaction metadata while
  keeping exact effects owned by focused systems. Generic `Use` actions
  were intentionally not classified as transports unless source-backed
  by the transport dataset or explicit movement verbs.
- Exact surfaces changed:
  - `tools/export_object_behaviors.py` added OBHV v1 export from
    `object_defs.bin` plus `data_osrs/transports_osrs.json`.
  - `data/defs/object_behaviors.bin` generated `8031` rows covering
    doors, ladders, stairs, banks, altars, resource nodes, and
    source-backed transports.
  - `tools/reports/object_behaviors.txt` added behavior and resource
    skill counts.
  - `tools/export_object_transports.py` added OTRP v1 export from
    `data_osrs/transports_osrs.json`.
  - `data/defs/object_transports.bin` generated `29959` transport
    edges across `2591` object IDs.
  - `tools/reports/object_transports.txt` added action/object-ID
    distributions.
  - `rc-core/objects.{h,c}` added object loaders and indexed lookups for
    definitions, behavior rows, placed objects, and transport edges.
  - `rc-core/config.{h,c}` added `RC_SUB_OBJECTS` and object dataset
    paths to full-game and skilling presets while keeping combat-only
    lean.
  - `rc-core/world.c` loads object datasets during configured world
    bring-up.
  - `rc-core/tick.c` validates object interaction inputs against loaded
    behavior action masks.
  - `tests/test_objects_runtime.c` added loader, lookup, bad-header,
    preset, transport, placement, and object-interaction coverage.
  - `tests/test_base_only.c`, `rc-core/README.md`, `database.md`,
    `work.md`, `temp_databaseaudit.md`, and
    `tools/reports/database_audit.txt` were updated for the new object
    runtime owner and remaining exact-consumer boundary.
- Upstream/downstream impacts: movement, skilling, bank/storage, altar,
  and action systems can now consume one source-backed object metadata
  surface instead of scraping viewer meshes. Exact effects remain
  downstream work: door transform mutation, transport movement,
  resource depletion/respawn, bank/storage state, and altar effects.
- Verification: `python3 -m py_compile` passed for both new exporters.
  Both exporters regenerated their binaries and reports. Full build
  passed, and full CTest passed `20/20`. Python trace coverage on both
  exporters showed no uncovered lines in the changed exporter files.
  C coverage build ran `test_objects_runtime`; `gcov` reported
  `objects.c` at `88.43%` line coverage, with only malformed/truncated
  binary error paths, overlong string skip-failure paths, and allocation
  failure paths uncovered. Benchmarks: behavior export `0.31s` /
  `87348 KB` max RSS; transport export `0.10s` / `50344 KB` max RSS;
  object runtime load `0.265507s`; indexed behavior+transport lookups
  `173738845 ops/sec`; placement tile queries `1256565 queries/sec`.

## 2026-04-27 — Object Definition and Placement Catalogs

- Change made: added first-class object-definition and full-world
  object-placement catalogs separate from viewer `.objects` mesh assets.
  The definition exporter decodes the b237 cache object-definition group,
  including 32-bit model opcodes and RuneLite-style entity-op action
  records, then cross-references cached wiki `object_id` and
  `infobox_scenery` IDs. The placement exporter writes compact gameplay
  placement rows from b237 map groups.
- Why it was made: object/interactable parity needed source-backed
  gameplay metadata for object IDs, names, dimensions, actions, model
  links, varbit/varp transforms, and placed world locations. The existing
  `data/regions/*.objects` files are baked render meshes and cannot be
  the source of gameplay interaction rules.
- Exact surfaces changed:
  - `tools/export_object_defs.py` added ODEF v1 export and
    `tools/reports/object_defs.txt` generation.
  - `data/defs/object_defs.bin` generated with `60568` cache object
    definitions, `16354` action-bearing defs, `4555` transform/morph
    defs, and `0` wiki object/scenery IDs missing cache definitions.
  - `tools/export_object_placements.py` added OPLC v1 export and
    `tools/reports/object_placements.txt` generation.
  - `data/defs/object_placements.bin` generated with `4883059`
    placements across `2448` mapsquares, `40515` unique placed object
    IDs, and `0` placement rows missing `object_defs.bin` definitions.
  - `tests/test_object_defs_bin.c` added direct binary coverage for
    catalog breadth plus known interactables: tree chop action, bank
    booth bank/collect actions, and fairy-tree conditional action text.
  - `tests/test_object_placements_bin.c` added direct binary coverage
    for full-world placement breadth, plane/type distribution, and known
    placed tree, bank booth, door, and ladder rows.
  - `database.md`, `work.md`, `temp_databaseaudit.md`, and
    `tools/reports/database_audit.txt` updated so object definitions /
    actions / transforms and full-world placement export are no longer
    listed as the active blocker.
- Upstream/downstream impacts: source-backed object actions, morph hooks,
  and full-world placement identity rows are now available to later
  gameplay consumers. Full parity still requires typed behavior rules for
  doors/ladders/stairs/banks/altars/resource nodes/transports and
  rc-core/runtime ownership when movement/action systems consume object
  interactions.
- Verification: `python3 -m py_compile tools/export_object_defs.py`
  and `python3 -m py_compile tools/export_object_placements.py` passed.
  `python3 tools/export_object_defs.py` regenerated ODEF with zero
  unknown opcode warnings; `python3 tools/export_object_placements.py`
  regenerated OPLC with zero loc read errors. `cmake -S . -B build`,
  `cmake --build build -j2`, and full CTest passed `19/19`. Python trace
  coverage on `tools/export_object_defs.py` executed the cache decode,
  entity-op decode, wiki cross-reference, binary write, and report write
  paths; legacy u16 model opcodes `1/5` and absent no-op opcode `91`
  remained uncovered because the current b237 cache did not contain
  those paths. Python trace coverage on `tools/export_object_placements.py`
  executed the map-group walk, placement parse/write, object-name join,
  and report write paths. Export benchmarks: ODEF `0.60s` / `71240 KB`
  max RSS; OPLC `3.81s` / `27008 KB` max RSS.

## 2026-04-27 — Source Mirror Restored for Object/Collision Export

- Change made: restored the local-only `data/source/` mirror needed by
  source-backed exporters and added minimal b237 flat-cache support to
  object/collision exporter tooling. Mirrored local reference corpora
  under `data/source/` and downloaded OpenRS2 oldschool build 237
  flat-file cache `2523` plus its keys file into
  `data/source/current_fightcaves_demo/data/`.
- Why it was made: object/interactable parity was blocked because
  `data/source/` was absent and the copied native `.dat2/.idx` cache did
  not match the exporter’s expected flat-file layout. Build 237 also
  stores map groups by mapsquare group ID with terrain/loc as files
  inside the group, not older named `mX_Y`/`lX_Y` groups.
- Exact surfaces changed:
  - `data/source/` restored locally; it remains gitignored.
  - `tools/cache_pipeline/export_collision_map_modern.py` now falls
    back to mapsquare group IDs when cache group-name hashes are absent.
  - `tools/cache_pipeline/export_objects.py` now reads b237 combined
    region groups via file `0` terrain and file `1` loc placements, and
    decodes object model opcodes `6/7` with 32-bit model IDs.
  - `tools/reports/spawn_sources.txt` regenerated so it now records
    `data/source present: True`.
- Upstream/downstream impacts: source-backed object, collision, varbit,
  varp, NPC, item, and model exporters can now resolve local source
  paths without reading other repo checkouts. Object/interactable parity
  is no longer blocked on source restoration; the remaining work is the
  actual gameplay object/interactable audit/export.
- Verification: source-path smoke found all required local inputs.
  `python3 -m py_compile` passed for the changed exporter modules and
  adjacent source-backed exporters. `python3 tools/export_varbits.py`
  loaded `18570` cache varbit defs, and `python3 tools/export_varps.py`
  loaded `5546` cache varps. `python3 tools/export_spawn_sources.py`
  regenerated the spawn-source report with `data/source present: True`.
  Object smoke for region `50,53` parsed `3973` placements and wrote
  `3865` geometry objects / `406557` triangles. Collision smoke for
  region `50,53` marked `4341` collision objects and wrote `2010`
  non-zero plane-0 tiles. Full build passed and CTest passed `17/17`.
  Python trace coverage confirmed the new mapsquare fallback, combined
  group read path, and b237 model opcodes executed. Exporter perf smoke
  for one Varrock region completed in `1.97s` with `288816 KB` max RSS.

## 2026-04-27 — Activity-State Consumers and Mechanics Surface Closure

- Change made: added the small `RcActivityRun` runtime consumer for typed
  activity state rows and promoted the remaining v1 mechanics-only rows
  into ASTA typed activity rows. Added the `single_boss` activity kind and
  typed rows for Araxxor, Doom of Mokhaiotl, Gemstone Crab, Penance
  Queen, Phosani's Nightmare, Revenant maledictus, Shellbane gryphon, and
  Vanguard. Mechanics coverage now reports `0` v1 mechanics-only rows
  lacking an executable encounter or typed activity surface.
- Why it was made: AMCH owner/profile bits and plain transition stepping
  were not enough to close the v1 mechanics database gate. The remaining
  rows needed a loadable runtime surface, but adding a large framework
  would violate the repo priority of simple, fast, data-driven code.
- Exact surfaces changed:
  - `rc-core/activity_states.{h,c}` added `RC_ACTIVITY_STATE_KIND_SINGLE_BOSS`
    plus `RcActivityRun` start/event stepping for wave progress,
    boss-death progress, HP-threshold transitions, skilling resource
    loops, and reward completion.
  - `data/curated/activity_state_machines.toml` now has 16 v1-required
    rows, 46 states, 31 transitions, and 51 params.
  - `tools/export_activity_states.py` now emits `single_boss`, allows all
    128 bits in the fixed two-word flag field, and reports the
    `RcActivityRun` state-flow boundary.
  - `data/defs/activity_states.bin` regenerated as ASTA v1 count `16`;
    `tools/reports/activity_states.txt` regenerated.
  - `tools/audit_mechanics_coverage.py` now recognizes richer encounter
    source keys, quest/nice-to-have deferrals, typed activity coverage,
    and no longer lists already-closed mechanics promotion as next work
    when uncovered count is `0`; `tools/reports/mechanics_coverage.txt`
    regenerated as `READY`.
  - `tests/test_activity_states_bin.c` now covers negative loads, all 16
    activity rows, `single_boss`, `multi_boss`, `wave_activity`,
    `skilling_boss`, threshold transitions, reward completion, and
    `RcWorldConfig` loading.
  - `work.md`, `database.md`, `rc-core/README.md`, and
    `tools/reports/database_audit.txt` updated to reflect current ASTA
    counts and the next source-backed database lane.
- Upstream/downstream impacts: `activity_states.bin` remains additive and
  separate from ENCT/AMCH. Encounter-enabled worlds now have a compact
  state-flow consumer available for current grouped/wave/object/boss
  activities, but exact attack, object, and arena behavior still belongs
  in focused runtime/content consumers where state-flow events are too
  broad. The next non-mechanics database lanes require the local
  `data/source/` mirror to be restored before authoritative regeneration
  or cross-reference can proceed.
- Verification: `python3 -m py_compile tools/export_activity_states.py
  tools/audit_mechanics_coverage.py` passed. Regenerated ASTA and
  mechanics coverage with `python3 tools/export_activity_states.py` and
  `python3 tools/audit_mechanics_coverage.py`. `cmake --build build -j2`
  passed; full CTest passed `17/17`. Coverage build
  `/tmp/runec_cov_activity_run` passed `17/17`; gcov showed
  `activity_states.c` at `100.00%` line coverage and `100.00%` branch
  execution. Benchmark `/tmp/runec_activity_run_bench`: NPC lookup
  `1.319B ops/sec`, timer event stepping `205.8M ops/sec`, and
  boss-death event stepping `41.6M ops/sec`; no throughput regression is
  indicated against the prior activity-state benchmark surface.

## 2026-04-27 — Typed Activity State Machines and Inferno ID Correction

- Change made: added ASTA v1 typed activity state-machine data and
  runtime loading for the first v1 grouped/wave/object activities:
  Barrows, TzHaar Fight Cave, Inferno, Grotesque Guardians, Moons of
  Peril, Royal Titans, Tempoross, and Wintertodt. Added generic runtime
  APIs for activity slug lookup, NPC lookup, parameter lookup, state-node
  lookup, and event/value-gated transition stepping. Corrected Inferno's
  encounter identity to use NPC `7706` for TzKal-Zuk; `7707` remains the
  Ancestral Glyph/shield.
- Why it was made: AMCH owner/profile bits are useful for broad
  mechanics lookup but are not enough for exact v1 activity flow,
  especially waves, grouped bosses, reward chests, object/scenery boss
  loops, and multi-state activities. The existing Inferno encounter TOML
  conflicted with local NPC definitions and activity-spawn data, which
  would have made typed activity state data point at a different boss ID
  than the encounter runtime.
- Exact surfaces changed:
  - `data/curated/activity_state_machines.toml` added 8 v1-required
    activity rows with 24 states, 14 transitions, and 33 integer params.
  - `tools/export_activity_states.py` added ASTA v1 export and report
    generation; `data/defs/activity_states.bin` generated with magic
    `ASTA`, version `1`, count `8`; `tools/reports/activity_states.txt`
    generated.
  - `rc-core/activity_states.{h,c}` added fixed-size runtime structs,
    loader, O(1) NPC lookup cache, generic node lookup, transition
    stepping, and parameter lookup.
  - `rc-core/config.{h,c}` and `rc-core/world.c` added
    `activity_states_path` and load it under `RC_SUB_ENCOUNTER`.
  - `tests/test_activity_states_bin.c` added negative binary fixtures,
    real-data load assertions, state transition assertions, NPC/param
    lookup assertions, and `RcWorldConfig` load coverage.
  - `data/curated/encounters/inferno.toml` changed TzKal-Zuk from
    `7707` to `7706`; `data/curated/activity_spawns.toml` updated the
    stale note; `data/defs/encounters.bin` regenerated with 50 encoded
    encounters and 0 warnings.
  - `tools/audit_mechanics_coverage.py`,
    `tools/reports/mechanics_coverage.txt`,
    `tools/reports/activity_mechanics.txt`,
    `tools/reports/database_audit.txt`, `database.md`, `work.md`, and
    `rc-core/README.md` updated to include ASTA and split mechanics-only
    coverage into `15` typed-state-covered rows and `50` rows still
    lacking standalone encounter or typed activity rows.
- Upstream/downstream impacts: encounter worlds now try to load
  `data/defs/activity_states.bin` when the encounter subsystem is
  enabled. The new binary is additive and separate from ENCT and AMCH,
  so existing encounter and mechanics loaders keep their current binary
  formats. Generic ASTA stepping provides executable activity flow, but
  exact per-activity tick behavior still requires typed consumers or
  content scripts where generic transitions are not sufficient.
- Verification: `python3 -m py_compile tools/export_activity_states.py
  tools/export_activity_mechanics.py tools/export_encounters.py
  tools/audit_mechanics_coverage.py` passed. Regenerated ASTA, AMCH, and
  ENCT via `python3 tools/export_activity_states.py`, `python3
  tools/export_activity_mechanics.py`, and `python3
  tools/export_encounters.py`; regenerated mechanics coverage via
  `python3 tools/audit_mechanics_coverage.py`. `cmake -S . -B build`
  was required once so CMake picked up the new source file, then
  `cmake --build build -j2` passed and full CTest passed `17/17`.
  Coverage build in `/tmp/runec_cov_activity_states` passed `17/17`;
  gcov showed `activity_states.c 100.00%`, `world.c 87.84%`, and
  `activity_mechanics.c 90.57%` line coverage. Benchmark
  `/tmp/runec_activity_state_bench`: activity-state NPC lookup
  `949.97M ops/sec`, transition stepping `103.22M ops/sec`, and
  parameter lookup `229.09M ops/sec`.

## 2026-04-27 — Activity Mechanics ID Closure and Bespoke Profile Expansion

- Change made: closed the AMCH activity-row NPC-ID gap by deriving
  composite activity NPC IDs from child mechanics rows and marking
  object/scenery-driven activities owner-only instead of missing. Added
  explicit activity profiles for Dawn, Dusk, Grotesque Guardians,
  Brutus, Demonic Brutus, Royal Titans, and Moons of Peril, then wired
  focused combat consumers for Dawn's melee/halberd damage gate,
  Dusk's reduced prayer pierce, and Brutus/Demonic Brutus special
  cadence, special damage, and protection-pierce handling.
- Why it was made: `activity_mechanics.bin` still reported `5` rows
  missing NPC IDs even though two were composite activity rows with
  resolvable child NPC IDs, two were legitimate object/scenery
  activities, and Royal Titans needed direct ID correction from the
  encounter/activity owner data. The grouped-owner rows also needed
  profile IDs and low-cost runtime behavior where generic owner bits
  were too broad but full encounter state machines were not yet
  required.
- Exact surfaces changed:
  - `data/curated/mechanics_owners.toml` added `npc_id_unions`,
    `owner_only_reasons`, Royal Titans owner metadata, new profile
    names, and protection-pierce tags for the Brutus/Dusk rows that
    need prayer-pierce semantics.
  - `tools/export_activity_mechanics.py` now resolves derived NPC-ID
    rows, marks owner-only rows with a dedicated flag, validates
    owner-only/union references, and reports owner-only plus derived-ID
    counts.
  - `data/defs/activity_mechanics.bin` regenerated; current report
    shows `96` rows, `64` encounter-backed rows, `32`
    owner-executable rows, `0` owner-mapped behavior-incomplete rows,
    `0` activity-indexed rows, `0` rows missing NPC IDs, `2`
    owner-only rows, `2` derived NPC-ID rows, and `223` section
    descriptors.
  - `rc-core/activity_mechanics.h` added the `OWNER_ONLY` row flag and
    profile constants for Dawn, Dusk, Grotesque Guardians, Brutus,
    Demonic Brutus, Royal Titans, and Moons of Peril.
  - `rc-core/combat.c` added simple profile dispatch for Dawn damage
    gating, Brutus/Demonic Brutus area cadence/damage, and
    Dusk/Brutus/Demonic Brutus protection-pierce resolution.
  - `tests/test_activity_mechanics_bin.c` now asserts `0` missing NPC
    rows, `2` owner-only rows, composite child-ID coverage for
    Grotesque Guardians, Moons of Peril, and Royal Titans, and
    owner-only flags for Tempoross/Wintertodt.
  - `tests/test_regular_npc_mechanics_combat.c` now covers Dawn
    halberd/ranged gating, Dusk reduced prayer pierce, Brutus
    protection-ignoring special damage, Demonic Brutus melee-through-
    prayer cap, and Brutus/Demonic special cadence area hits.
  - `tools/audit_mechanics_coverage.py`,
    `tools/reports/activity_mechanics.txt`,
    `tools/reports/mechanics_coverage.txt`,
    `tools/reports/database_audit.txt`, `work.md`, and `database.md`
    updated to remove stale missing-ID status and document owner-only /
    derived-ID behavior.
- Upstream/downstream impacts: AMCH binary shape remains v3-compatible;
  the new owner-only bit is an added flag in the existing row flag byte.
  Tempoross and Wintertodt stay represented in the activity index
  without pretending they have normal NPC combat rows. Composite
  owners now provide NPC-ID/profile lookup for grouped activity
  behavior. Full boss/activity state machines are still required where
  phase-local behavior, object interactions, room state, or multi-actor
  rotations cannot be represented by profile parameters.
- Verification: `python3 -m py_compile tools/export_activity_mechanics.py
  tools/audit_mechanics_coverage.py` passed; `python3
  tools/export_activity_mechanics.py` regenerated AMCH v3 with `0`
  missing NPC-ID rows; `python3 tools/audit_mechanics_coverage.py`
  regenerated mechanics coverage; `cmake --build build -j2` passed;
  full CTest passed `16/16`. Coverage build in
  `/tmp/runec_cov_mechanics_closure` passed `16/16`; gcov showed
  `combat.c 83.04%` and `activity_mechanics.c 90.57%` line coverage,
  with the new Dawn/Brutus/Demonic/Dusk branches exercised. Benchmark
  `/tmp/runec_activity_profile_bench`: comparable prior hot paths
  remained flat (`plain_tick 21.36M`, `activity_profile_tick 19.91M`,
  `plain_resolve 33.22M`, `activity_profile_resolve 19.75M`
  ops/sec); new measured paths were `new_activity_profile_tick
  17.35M`, `dawn_damage_gate 73.03M`, and `new_profile_resolve
  15.18M` ops/sec.

## 2026-04-27 — AMCH v3 Profiles and SLAY v3 Edge-Case Closure

- Change made: upgraded `activity_mechanics.bin` to AMCH v3 with
  per-row `profile_id`, added NPC-ID profile lookup alongside the
  existing behavior-bit cache, and made combat consume profile-backed
  owner behavior for Araxxor, Barrows, Moons of Peril, Revenant
  maledictus, Shellbane gryphon, and TzTok-Jad. Also completed the
  SLAY v3 slayer edge-case pass for exact progression gates, Konar
  location rolls, boss-task second rolls, combat-achievement boss
  amount caps, and regular/boss alternative kill-credit rules.
- Why it was made: AMCH v2 behavior bits were too broad for several
  bosses and activities; they applied generic heal/drain/enrage/area/
  prayer behavior where the current database already had enough source
  detail for more exact runtime parameters. SLAY v2 likewise modeled
  assignment metadata but did not capture location-specific assignment,
  boss second-roll behavior, exact progression state, or alternative
  kill families.
- Exact surfaces changed:
  - `data/curated/mechanics_owners.toml` now assigns behavior profiles
    for the current exact-parameter owner rows and corrects Revenant
    maledictus to freeze/area/heal behavior rather than teleblock.
  - `tools/export_activity_mechanics.py` now emits AMCH v3
    `profile_id` values and validates profile names from the owner map.
  - `data/defs/activity_mechanics.bin` regenerated as AMCH v3; current
    report shows `96` rows, `64` encounter-backed rows, `32`
    owner-executable rows, `0` owner-mapped behavior-incomplete rows,
    `0` activity-indexed rows, `5` rows missing NPC IDs, and `223`
    section descriptors.
  - `rc-core/activity_mechanics.{h,c}` added profile constants, v3
    load support with v1/v2 compatibility, and O(1)
    `rc_activity_mechanics_profile_for_npc` lookup.
  - `rc-core/combat.c` now uses profiles for Araxxor ranged/magic
    drain and enrage speed, Barrows brother effects, Blood Moon
    healing, Revenant maledictus freeze/heal/area cadence, Shellbane
    gryphon tortugan-shield protection, Jad range/style protection
    behavior, activity area cadence/damage, and generic-profile
    fallback behavior.
  - `tools/scrape_slayer.py` now emits SLAY v3 with progression flags,
    simple OR progression flags, task flags, Konar location tokens, and
    boss second-roll candidates.
  - `data/defs/slayer.bin` regenerated as SLAY v3; current size is
    `59355` bytes.
  - `rc-core/slayer.{h,c}` and `rc-core/types.h` added SLAY v3 load
    fields, parsed location/boss candidate arrays, exact eligibility,
    Konar location assignment, boss second-roll assignment, current
    boss/location state, combat-achievement amount caps, and
    alternative kill-family matching.
  - `tests/test_activity_mechanics_bin.c`,
    `tests/test_regular_npc_mechanics_combat.c`, and
    `tests/test_slayer_bin.c` cover AMCH v3 profiles, profile-backed
    combat behavior, SLAY v3 metadata, exact slayer assignment, boss
    rolls, location rolls, and alternative kill-credit paths.
  - `tools/audit_mechanics_coverage.py`,
    `tools/reports/activity_mechanics.txt`,
    `tools/reports/mechanics_coverage.txt`,
    `tools/reports/slayer.txt`, `tools/reports/database_audit.txt`,
    `work.md`, `database.md`, `temp_databaseaudit.md`, and
    `rc-core/README.md` updated to match the current AMCH v3 / SLAY v3
    state.
- Upstream/downstream impacts: AMCH consumers can still read v1/v2
  rows with zero profile IDs, but current generated data is v3.
  Combat behavior is now more exact for the profiled owner rows without
  moving OSRS-specific script bodies into `rc-core`; full state-machine
  boss behavior still belongs in encounter/activity data or
  `rc-content`. `RcSlayerTaskDef` and `RcPlayer` grew to carry parsed
  assignment metadata and active slayer boss/location state. Assignment
  work is heavier than the old broad-table path, but it is assignment
  time, not per-tick sim cost.
- Verification: `python3 -m py_compile tools/export_activity_mechanics.py
  tools/audit_mechanics_coverage.py tools/scrape_slayer.py` passed;
  `python3 tools/export_activity_mechanics.py` regenerated AMCH v3;
  `python3 tools/audit_mechanics_coverage.py` regenerated mechanics
  coverage; `cmake --build build -j2` passed; full CTest passed
  `16/16`. Coverage build in `/tmp/runec_cov_activity_profiles` passed
  `16/16`; gcov showed `activity_mechanics.c 90.57%`,
  `combat.c 82.51%`, and `slayer.c 93.19%` line coverage, with the
  new profile branches exercised. `python3 tools/scrape_slayer.py`
  regenerated `data/defs/slayer.bin` at `59355` bytes and full CTest
  remained `16/16` after regeneration. Benchmark
  `/tmp/runec_activity_profile_bench`: plain tick `21.45M ops/sec`,
  profiled tick `19.81M ops/sec`, plain resolve `33.51M ops/sec`,
  profiled resolve `19.76M ops/sec`.

## 2026-04-27 — Owner-Executable Activity Mechanics and SLAY v2

- Change made: converted the `32` owner-mapped boss/activity mechanics
  rows from behavior-incomplete owner records into AMCH v2
  owner-executable rows with behavior bitmasks, added an O(1) NPC-ID
  activity behavior index, consumed generic owner effects in combat,
  and upgraded slayer to SLAY v2 with amount ranges, extended ranges,
  level gates, unlock flags, block/prefer filters, and amount
  selection.
- Why it was made: the database completion lane needed the remaining
  owner-mapped mechanics rows to be executable at runtime instead of
  just owned, and slayer needed exact table metadata beyond simple
  weighted assignment and kill-credit decrement. The initial generic
  activity lookup benchmark also exposed a hot-path scan over the
  96-row mechanics table, which was too slow for the project’s
  high-throughput sim target.
- Exact surfaces changed:
  - `data/curated/mechanics_owners.toml` now includes behavior tags for
    all `32` owner-mapped rows
  - `tools/export_activity_mechanics.py` now emits AMCH v2
    `behavior_bits`, reports `32/32` owner-executable rows, and keeps
    owner-mapped behavior-incomplete rows at `0`
  - `data/defs/activity_mechanics.bin` regenerated; current report
    shows `96` rows, `64` encounter-backed rows, `32`
    owner-executable rows, `0` owner-mapped behavior-incomplete rows,
    `0` activity-indexed rows, `5` rows missing NPC IDs, and `223`
    section descriptors
  - `rc-core/activity_mechanics.{h,c}` added AMCH v2 load support,
    v1 compatibility, owner-executable status, behavior constants,
    owner/NPC behavior queries, and an NPC-ID behavior cache
  - `rc-core/combat.c` now consumes activity owner effects for venom,
    stat drain, prayer drain, healing, protection pierce, area
    pressure, enrage, dragonfire mitigation, teleblock, and freeze
  - `tools/scrape_slayer.py` now emits SLAY v2 from cached wiki
    wikitext, including task amount ranges, extended ranges,
    slayer/combat gates, unlock flags, alternatives, requirement text,
    and master requirements
  - `data/defs/slayer.bin` regenerated to SLAY v2; current size is
    `40009` bytes with `12` masters
  - `rc-core/slayer.{h,c}` now loads SLAY v2 while retaining v1
    compatibility, filters assignments by levels/unlocks, supports
    block/prefer lists, and selects task amounts from data
  - `rc-core/types.h` added player slayer block/prefer state
  - `tests/test_activity_mechanics_bin.c`,
    `tests/test_regular_npc_mechanics_combat.c`, and
    `tests/test_slayer_bin.c` updated for AMCH v2, activity combat
    effects, SLAY v2 metadata, eligibility, blocks, prefers, and amount
    selection
  - `tools/reports/activity_mechanics.txt`, `tools/reports/slayer.txt`,
    `tools/reports/mechanics_coverage.txt`,
    `tools/reports/database_audit.txt`, `tools/audit_mechanics_coverage.py`,
    `work.md`, `database.md`, `temp_databaseaudit.md`, and
    `rc-core/README.md` updated to remove stale
    behavior-incomplete/slayer-minimal status
- Upstream/downstream impacts: AMCH v1 artifacts still load with zero
  behavior bits, but current generated data is AMCH v2. Combat can now
  execute generic boss/activity owner effects before bespoke per-boss
  scripts are authored. SLAY v2 makes slayer assignment data usable for
  runtime filtering; exact quest/diary/combat-achievement state, Konar
  location rolls, boss-task second rolls, and alternative kill-family
  rules remain separate content/runtime edge cases rather than simple
  assignment-table fields.
- Verification: `python3 -m py_compile tools/export_activity_mechanics.py
  tools/scrape_slayer.py` passed; `python3
  tools/export_activity_mechanics.py` regenerated the activity index
  with `32/32` owner-executable rows; `python3 tools/scrape_slayer.py`
  regenerated SLAY v2 from cached wiki pages; `python3
  tools/audit_mechanics_coverage.py` regenerated mechanics coverage;
  `cmake -S . -B build` passed; `cmake --build build -j2` passed;
  full CTest passed `16/16`.
  Coverage build in `/tmp/runec_cov_activity_slayer` passed `16/16`;
  gcov showed `activity_mechanics.c 92.71%`, `slayer.c 92.51%`, and
  `combat.c 78.76%` line coverage. Benchmark:
  `/tmp/runec_activity_slayer_bench` measured activity behavior lookup
  at `666.80M` lookups/sec after adding the NPC-ID index, up from the
  initial `6.29M` scan-based lookups/sec; slayer assignment measured
  `1.56M` assignments/sec.

## 2026-04-27 — Mechanics Owner Map and Slayer Runtime Owner

- Change made: added explicit owner mappings for the remaining boss
  mechanics extracts, changed the activity-mechanics status model to
  distinguish owner-mapped behavior gaps from ownerless rows, and added
  a minimal slayer runtime owner for task loading, assignment, active
  task state, and NPC-death kill-credit decrement.
- Why it was made: the mechanics lane still reported `32`
  activity-indexed rows even though those rows had clear activity or
  boss owners. Regular NPC mechanics also still reported slayer task
  progression as having no runtime owner. Leaving both states vague
  made it hard to tell what was real missing behavior versus missing
  ownership.
- Exact surfaces changed:
  - `data/curated/mechanics_owners.toml` added `32` explicit owner
    mappings for Barrows, Moons of Peril, Grotesque Guardians, Fight
    Caves, quest bosses, raid/activity bosses, and standalone bosses
  - `tools/export_activity_mechanics.py` now reads the owner map,
    emits `STATUS_OWNER_MAPPED`, sets `FLAG_HAS_OWNER`, and reports
    owner-mapped behavior-incomplete rows separately from ownerless
    activity-indexed rows
  - `data/defs/activity_mechanics.bin` regenerated; current report
    shows `96` rows, `64` encounter-backed rows, `32`
    owner-mapped behavior-incomplete rows, `0` activity-indexed rows,
    `5` rows missing NPC IDs, and `223` section descriptors
  - `rc-core/activity_mechanics.h` added owner-mapped status/flag
    constants; existing AMCH v1 binary shape remains unchanged
  - `rc-core/slayer.h` and `rc-core/slayer.c` added the `SLAY` v1
    loader, master/task lookup, weighted assignment, active-task
    player state, and `RC_EVT_NPC_DIED` kill-credit handler
  - `rc-core/types.h` added player slayer task state; `rc-core/world.c`
    loads `slayer.bin` and subscribes the slayer event owner when
    `RC_SUB_SLAYER` is enabled
  - `tests/test_activity_mechanics_bin.c` now asserts `64` backed,
    `32` owner-mapped, and `0` ownerless indexed rows
  - `tests/test_slayer_bin.c` added slayer binary negative-path,
    lookup, assignment, world-load, and death-event decrement coverage
  - `tools/export_regular_npc_mechanics.py`,
    `tools/audit_mechanics_coverage.py`,
    `tools/reports/activity_mechanics.txt`,
    `tools/reports/regular_npc_mechanics.txt`,
    `tools/reports/mechanics_coverage.txt`,
    `tools/reports/database_audit.txt`, `work.md`, `database.md`,
    `temp_databaseaudit.md`, and `rc-core/README.md` updated so they
    no longer describe these rows as ownerless or slayer as ownerless
- Upstream/downstream impacts: mechanics data now has no ownerless
  activity-indexed rows; downstream work should author executable
  behavior under the mapped owners rather than creating duplicate
  encounter shells. The AMCH loader/API remains binary-shape
  compatible. `RC_SUB_SLAYER` worlds now load `slayer.bin`, subscribe
  to NPC-death events, and mutate player task state; exact task
  eligibility, unlock, amount, extension, block/prefer, and
  per-family edge rules are still deeper slayer-rule work.
- Verification: `python3 -m py_compile
  tools/export_activity_mechanics.py tools/audit_mechanics_coverage.py`
  passed; `python3 tools/export_activity_mechanics.py`,
  `python3 tools/export_regular_npc_mechanics.py`, and `python3
  tools/audit_mechanics_coverage.py` regenerated the data/report
  surfaces; `cmake -S . -B build` was rerun so CMake picked up
  `rc-core/slayer.c`; `cmake --build build -j2` passed; full CTest
  passed `16/16`. Coverage build in `/tmp/runec_cov_mechanics_owner`
  passed `16/16`; gcov showed `slayer.c 92.86%`,
  `activity_mechanics.c 94.12%`, and `world.c 87.32%` line coverage.
  Python trace coverage for `export_activity_mechanics.py` and
  `audit_mechanics_coverage.py` showed no missed-line markers.
  Benchmark: 5M matching slayer NPC-death events ran in `0.421531s`
  (`11.86M events/sec`). One benchmark compile attempt failed because
  `cc` was invoked with `-x c` still active for `build/librc-core.a`;
  rerunning with `-x none` before the archive fixed the command and
  passed.

## 2026-04-27 — Mechanics Conversion Pass, Regular NPC Consumers, and Encounter Hook Dispatch

- Change made: linked mechanics extracts to existing combined
  encounter/activity owners, added remaining regular-NPC consumer
  behavior supported by the current runtime, and wired generic
  dispatchers for previously dormant encounter hook classes.
- Why it was made: mechanics coverage still showed `65`
  mechanics-only rows, regular NPC tags were only partially consumed,
  and `after_attack`, `during_mechanic`, `attack_counter_special`, and
  named-event hooks were encoded but inert. This blocked the mechanics
  database lane from being usable by runtime systems.
- Exact surfaces changed:
  - `tools/export_activity_mechanics.py` now links mechanics extracts
    to encounter/activity owners by slug, NPC ID overlap, source-page
    overlap, and room/boss-name ownership
  - `data/defs/activity_mechanics.bin` regenerated from `12,240` to
    `12,705` bytes; current report shows `96` rows, `64`
    encounter/activity-backed rows, `32` activity-indexed rows, `5`
    rows missing NPC IDs, and `223` section descriptors
  - `tools/reports/activity_mechanics.txt` and
    `tools/reports/mechanics_coverage.txt` regenerated with the new
    `64` backed / `32` indexed split
  - `rc-core/types.h` added player status timers/damage fields,
    slayer unlock bits, and NPC attack counters
  - `rc-core/combat.h`, `rc-core/combat.c`, and `rc-core/tick.c`
    added poison/venom/disease ticking, status application on landed
    NPC hits, revenant heal/teleblock/freeze effects, lizardman shaman
    jump/area/minion pressure, slayer protection checks, and
    auto-finisher unlock support
  - `rc-core/events.h`, `rc-core/encounter.h`, and
    `rc-core/encounter.c` added `RC_EVT_NPC_ATTACK`, attack-count
    tracking, after-attack dispatch, attack-counter-special dispatch,
    during-mechanic windows, named-event dispatch, and
    `triggered_mechanics` observability
  - `tests/test_activity_mechanics_bin.c`,
    `tests/test_regular_npc_mechanics_combat.c`, and
    `tests/test_encounter.c` updated for the new activity counts,
    status/effect consumers, shaman/revenant/slayer paths, and hook
    dispatch behavior
  - `work.md`, `database.md`, `rc-core/README.md`,
    `tools/reports/database_audit.txt`, and `temp_databaseaudit.md`
    updated to reflect current mechanics status and remaining gaps
- Upstream/downstream impacts: 33 formerly activity-indexed rows are
  now recognized as owned by existing combined activities, reducing the
  true behavior-authoring gap to 32 rows. Combat now has real
  player-status and special-pressure side effects, so future
  consumable/slayer work must use these fields rather than inventing
  parallel state. Encounter TOMLs using dormant hook classes can now
  fire generic primitive plumbing, but many authored primitives remain
  unimplemented and per-boss scripts are still no-op stubs until their
  rc-content implementations land.
- Verification: `python3 -m py_compile
  tools/export_activity_mechanics.py tools/audit_mechanics_coverage.py`
  passed; `cc -std=c11 -fsyntax-only -Irc-core` passed for changed
  core C files; `python3 tools/export_activity_mechanics.py`
  regenerated the activity index in `53.602 ms`; `python3
  tools/audit_mechanics_coverage.py` regenerated the aggregate report;
  `cmake --build build -j2` passed; full CTest passed `15/15`.
  Coverage build in `/tmp/runec_cov_mechanics_consumers` passed
  `15/15`; gcov showed `combat.c 75.10%`, `encounter.c 91.95%`,
  and `tick.c 77.46%` line coverage. Python trace coverage for
  `export_activity_mechanics.py` and `audit_mechanics_coverage.py`
  showed no missed-line markers. Benchmarks: regular NPC combat
  consumer microbench measured `49.42M SPS` plain NPC, `265.22M SPS`
  lizardman shaman path, and `49.66M SPS` revenant path; encounter
  NPC-attack dispatcher bench measured `152.99M events/sec`.

## 2026-04-27 — Activity Mechanics Runtime Index

- Change made: added a compiled activity-mechanics database for boss
  mechanics extracts and wired a small `rc-core` loader/query API so
  every extracted boss mechanics TOML is represented in a loadable
  runtime index.
- Why it was made: mechanics extraction already produced `96` curated
  boss mechanics TOMLs, but `65` of them had no executable encounter
  TOML and no loadable activity surface. That made the database look
  broader than the runtime could inspect. The new index closes the
  loadability gap without fabricating executable boss behavior from
  prose.
- Exact surfaces changed:
  - `tools/export_activity_mechanics.py` added the `AMCH` v1 exporter
  - `data/defs/activity_mechanics.bin` added with `96` rows, `31`
    encounter-backed rows, `65` activity-indexed rows, `5` rows missing
    NPC IDs, and `223` section descriptors
  - `tools/reports/activity_mechanics.txt` added the export report
  - `rc-core/activity_mechanics.h` and `rc-core/activity_mechanics.c`
    added the fixed-cap loader, slug lookup, and NPC-ID membership API
  - `rc-core/config.h`, `rc-core/config.c`, and `rc-core/world.c` added
    `activity_mechanics_path`; encounter-enabled presets now load
    `data/defs/activity_mechanics.bin`
  - `tests/test_activity_mechanics_bin.c` added binary/header,
    negative-path, lookup, section-descriptor, NPC-membership, and
    world-config load coverage
  - `tools/audit_mechanics_coverage.py` and
    `tools/reports/mechanics_coverage.txt` now report the
    activity-indexed mechanics-only rows separately from executable
    encounter TOMLs
  - `database.md`, `work.md`, `rc-core/README.md`,
    `tools/reports/database_audit.txt`, and `temp_databaseaudit.md`
    updated so the database lane distinguishes loadable
    activity-mechanics coverage from executable behavior parity
- Upstream/downstream impacts: `data/curated/mechanics/*.toml` now has
  a compiled runtime index in addition to the raw curated/reference
  files. Encounter worlds pay one small initialization load for the
  `12,240` byte index. The `65` activity-indexed rows remain
  behavior-incomplete; downstream boss/activity work must convert only
  required rows into executable encounter/activity logic rather than
  treating this index as parity behavior.
- Verification: `python3 -m py_compile
  tools/export_activity_mechanics.py tools/audit_mechanics_coverage.py`
  passed; `cc -std=c11 -fsyntax-only -Irc-core
  rc-core/activity_mechanics.c` passed; `python3
  tools/export_activity_mechanics.py` exported `96` rows in
  `52.595 ms`; `python3 tools/audit_mechanics_coverage.py`
  regenerated the aggregate mechanics report; `cmake -S . -B build`
  and `cmake --build build -j2` passed; full CTest passed `15/15`.
  Coverage build in `/tmp/runec_cov_activity` passed `15/15`; gcov
  showed `activity_mechanics.c 94.12%`, `world.c 85.94%`, and
  `config.c 75.00%` line coverage. Benchmark: current loader
  benchmark measured `1000` loads in `0.029489s` (`33,910`
  loads/sec, `3.26M` rows/sec), and `10M` NPC-membership lookups at
  about `956M` lookups/sec. No before/after comparison exists because
  this index did not exist before this change.

## 2026-04-27 — Encounter Trigger/Script Runtime Binding and Regular NPC Consumers

- Change made: upgraded encounters to ENCT v3, encoded all authored
  encounter trigger/script bindings, added a world-local script
  registry, routed current authored `script = "..."` names through
  rc-content no-op stubs, and added first runtime consumers for regular
  NPC mechanic tags.
- Why it was made: the encounter exporter still deferred `phase_in`,
  `while_in_phase`, trigger unions, attack-counter, mechanic-scoped,
  and named-event bindings, while TOML phase scripts had no runtime
  route. Regular NPC mechanics were indexed but inert. This blocked the
  mechanics-coverage database lane from becoming executable runtime
  behavior.
- Exact surfaces changed:
  - `tools/export_encounters.py` now writes ENCT v3 with phase
    `script` names, mechanic `phase_mask`, and `trigger_ref`
  - `data/defs/encounters.bin` regenerated from `17162` to `18968`
    bytes
  - `tools/reports/encounters.txt` regenerated with `50` encoded
    encounters, `0` skipped, and `0` unresolved trigger warnings
  - `rc-core/encounter.h` and `rc-core/encounter.c` added
    `RcEncounterScriptFn`, script registry APIs, phase-script dispatch,
    phase-mask matching, dormant trigger types, and immediate
    first-eligible periodic scheduler behavior
  - `rc-content/content.h`, `rc-content/content.c`, and
    `rc-content/encounters/scripts.c` now register the 50 distinct
    authored script names to `rc_encounter_script_noop`
  - `rc-content/encounters/scurrius.c` and
    `rc-content/encounters/kalphite_queen.c` comments updated so they
    no longer describe a missing registry API
  - `rc-core/combat.h` and `rc-core/combat.c` added regular-NPC damage
    rules for weapon/ammo/spell gates, finisher-item gates, and
    dragonfire/icy-breath equipment mitigation
  - `tests/test_encounter.c`, `tests/test_encounter_bin.c`, and
    `tests/test_regular_npc_mechanics_combat.c` added coverage for
    script misses, phase-mask/fallback matching, dormant trigger
    encoding, script lookup, and combat tag consumers
  - `tools/audit_mechanics_coverage.py` and
    `tools/reports/mechanics_coverage.txt` updated so the mechanics
    audit reports `0` unresolved trigger bindings and `0` unrouted
    script names; at that checkpoint, 65 mechanics-only TOMLs remained
    active database work
  - `work.md`, `database.md`, `rc-core/README.md`,
    `rc-content/README.md`, and `rc-content/encounters/README.md`
    updated to reflect current runtime ownership and remaining work
  - `tools/reports/database_audit.txt` and `temp_databaseaudit.md`
    updated so the audit notes no longer carry stale deferred-trigger
    or missing-script-registry findings
- Upstream/downstream impacts: encounter TOML trigger/script data is no
  longer silently deferred at export time. `after_attack`,
  `during_mechanic`, `attack_counter_special`, and named-event hooks
  are encoded but remain dormant until their owning combat/mechanic
  dispatchers land. The rc-content script stubs prove routing only;
  real boss behavior still belongs in per-boss modules. Regular NPC
  mechanics now affect combat damage, but poison/venom/disease,
  revenants, lizardman shamans, and slayer task/unlock behavior remain
  future consumers.
- Verification: `python3 -m py_compile tools/export_encounters.py`
  passed; `python3 tools/export_encounters.py` regenerated ENCT v3 with
  `0` warnings; `cc -std=c11 -fsyntax-only` passed for changed
  `encounter.c` and `combat.c`; CMake was regenerated once so the new
  `rc-content/encounters/scripts.c` joined the build; `cmake --build
  build -j2` passed; full CTest passed `14/14`. Coverage build in
  `/tmp/runec_cov` passed `14/14`; gcov showed changed runtime files at
  `encounter.c 90.51%`, `combat.c 77.39%`,
  `rc-content/encounters/scripts.c 100%`, and `rc-content/content.c
  100%` line coverage. Benchmark: 10M-step headless dragon NPC
  combat-loop bench versus `HEAD` baseline measured `108.68M SPS`
  before and `92.18M SPS` after; the remaining cost is accepted for
  now because the path now performs real mitigation and stays above the
  tens-of-millions SPS target.

## 2026-04-27 — Mechanics Coverage and Regular NPC Mechanics Runtime Index

- Change made: added a regular combat NPC special-mechanics dataset,
  compiled it into a runtime binary, wired a lightweight `rc-core`
  loader/query API, and added a mechanics coverage report that separates
  boss prose extracts, executable boss encounters, and regular NPC
  mechanics.
- Why it was made: mechanics coverage was previously boss-heavy and
  regular combat NPC special behavior existed only as an ownership note.
  The database needs a concrete indexed surface for dragonfire users,
  wyverns, slayer equipment gates, weapon-restricted monsters,
  finisher-item monsters, poison/venom/disease users, revenants, and
  similar non-boss mechanics before combat/slayer behavior can consume
  them.
- Exact surfaces changed:
  - `data/curated/regular_npc_special_mechanics.toml` replaced the old
    nine-family note with schema v2 selectors, runtime tags, required
    systems, and source labels
  - `tools/export_regular_npc_mechanics.py` added the `RNME` exporter
  - `data/defs/regular_npc_mechanics.bin` added with `11` families and
    `422` family-to-NPC-ID links
  - `tools/reports/regular_npc_mechanics.txt` added the export report
  - `rc-core/monster_mechanics.h` and `rc-core/monster_mechanics.c`
    added the runtime family/tag loader, family lookup API, and
    ID-indexed tag lookup cache
  - `rc-core/config.h`, `rc-core/config.c`, and `rc-core/world.c` now
    expose/load `monster_mechanics_path` for combat/slayer/encounter
    users
  - `tests/test_regular_npc_mechanics_bin.c` added binary/runtime smoke
    coverage
  - `tools/audit_mechanics_coverage.py` and
    `tools/reports/mechanics_coverage.txt` added the mechanics lane
    coverage summary
  - `tools/audit_npc_reconciliation.py` and
    `tools/reports/npc_reconciliation.txt` updated so missing
    repo-local source corpora are reported as unavailable instead of
    false zero-count coverage
  - `database.md`, `work.md`, `temp_databaseaudit.md`, and
    `tools/reports/database_audit.txt` updated to reflect the new
    status
- Upstream/downstream impacts: regular NPC mechanics are now loadable
  and queryable by NPC ID, but they do not yet execute behavior.
  Combat/slayer runtime work must consume the tags for dragonfire,
  equipment gates, weapon gates, finisher items, poison/venom/disease,
  revenants, and shaman-style attacks. Boss mechanics remain blocking
  parity because encounter conversion still has deferred trigger
  bindings and unrouted authored script hooks.
- Verification: `python3 -m py_compile` passed for the new exporter,
  mechanics coverage audit, and NPC reconciliation audit; `python3
  tools/export_regular_npc_mechanics.py`
  regenerated the binary/report; `python3
  tools/audit_mechanics_coverage.py` regenerated the mechanics coverage
  report; `python3 tools/audit_npc_reconciliation.py` regenerated the
  NPC reconciliation report; CMake was regenerated once so the new
  `rc-core` source joined the static library; `cmake --build build -j2`
  passed; targeted CTest `test_regular_npc_mechanics_bin` passed; full
  CTest remained `12/13` with only the known pre-existing
  `test_encounter_prims` Scorpia HP assertion failure. Coverage:
  Python trace coverage for the new/changed scripts showed no `>>>>>>`
  missed-line markers; gcov on the focused C test showed
  `monster_mechanics.c` at `87.14%` line coverage and `world.c` at
  `85.25%`, with remaining uncovered `monster_mechanics.c` lines
  limited to short-read/bad-header error branches. Benchmark: a
  50M-call headless tag lookup microbench went from `9.42M`
  lookups/sec with the initial linear scan to `1.24B` lookups/sec after
  adding the NPC-ID tag cache.

## 2026-04-27 — Spawn Edge-Case Source Authority Recheck

- Change made: rechecked the remaining Nex and Sol Heredit spawn
  blockers against local reference corpora, cached wiki buckets, and
  RuneLite constants, then tightened the curated/audit docs to preserve
  only source-backed spawn facts.
- Why it was made: the spawn-source index isolated four required
  unresolved markers, but the edge cases needed explicit evidence so
  future work does not infer combat tiles from rendered arenas or
  mistake object/location rows for NPC spawns.
- Exact surfaces changed:
  - `data/curated/activity_spawns.toml` now records the exact negative
    source evidence for Nex/bodyguards and Sol Heredit
  - `tools/reports/database_audit.txt` now records the source-authority
    recheck result and keeps the four unresolved markers
  - `database.md` now warns that RuneLite/wiki rows prove IDs/stats for
    Nex/Sol, not authoritative combat spawn tiles
  - `temp_databaseaudit.md` now preserves the source checks and the
    resulting blocker status
  - `work.md` now moves the database-completion pickup past spawn
    source recheck while carrying the unresolved spawn blockers
- Upstream/downstream impacts: runtime behavior and binary shape are
  unchanged. `spawn_sources.bin` remains the current audit index, and
  Nex/Sol exact combat-spawn parity remains blocked on cache
  server-script or equivalent authoritative activity-config extraction.
- Verification: `data/curated/activity_spawns.toml` parses with
  `tomllib`; `tools/export_spawn_sources.py` compiles and regenerates
  the spawn index/report with the same `46699` rows and four required
  unresolved markers; trace coverage executed the exporter path with no
  `>>>>>>` missed-line markers in the generated coverage; timed export
  was `0.20s` / `40632 KB`; Debug build passed; targeted
  `test_spawn_sources_bin` passed. Full CTest remained `11/12` with
  only the known pre-existing `test_encounter_prims` Scorpia HP
  assertion failure.

## 2026-04-27 — Spawn Source Parity Index

- Change made: added a compiled spawn-source index that keeps static
  world spawns, wiki locline NPC coordinates, curated activity-local
  points/regions, wave points, dynamic spawn pools, object anchors,
  encounter-authored dynamic spawn declarations, and unresolved required
  markers in one auditable surface.
- Why it was made: `world.npc-spawns.bin` is a broad static-world
  spawn source, but it does not prove coverage for wiki-only arena
  coordinates, activity-local bosses, wave/activity spawns, or dynamic
  helper pools. Those classes need separate accounting before spawn
  parity can be signed off.
- Exact surfaces changed:
  - `tools/export_spawn_sources.py` added the source-index exporter
  - wiki locline resolution now joins `npc_id` and `infobox_monster`
    buckets, not monster-only names, so non-combat NPC locline rows are
    preserved where they resolve to NPC IDs
  - `data/defs/spawn_sources.bin` added with `46699` rows:
    `24110` static world rows, `22486` resolved wiki locline points,
    `31` activity points, `9` wave points, `6` dynamic points, `28`
    object anchors, `6` activity regions, `19` encounter-authored
    dynamic declarations, and `4` unresolved required markers
  - `tools/reports/spawn_sources.txt` added the spawn-source report,
    including wiki-only-by-ID counts and explicit Nex/Sol unresolved
    markers
  - `tests/test_spawn_sources_bin.c` added binary smoke coverage for
    the `SSPR` format and required row classes
  - `data/curated/activity_spawns.toml` updated with OSRS Wiki API
    source checks for Nex/Sol and Sol Colosseum strategy tile-marker
    bounds, while keeping exact runtime spawn/bounds fields unresolved
  - `work.md`, `database.md`, `temp_databaseaudit.md`, and
    `tools/reports/database_audit.txt` updated to show spawn parity is
    source-indexed but not fully closed
- Upstream/downstream impacts: spawn parity now has a source-index
  artifact, but runtime behavior is unchanged. `spawn_sources.bin`
  should be treated as an audit/parity index, not as the final activity
  runtime schema. `data/source/` was absent during this run, so the
  exporter consumed existing compiled NSPN binaries plus cached wiki and
  curated activity data. Exact Nex boss/bodyguard tiles and Sol Heredit
  wave-12 combat spawn/reduced bounds remain source blockers requiring
  cache-script or authoritative activity-config extraction.
- Verification: `python3 -m py_compile tools/export_spawn_sources.py`
  passed; `python3 tools/export_spawn_sources.py` regenerated the
  binary/report; Python trace coverage executed the exporter path;
  trace reported no `>>>>>>` missed-line markers for
  `export_spawn_sources.py`; `/usr/bin/time` measured export at `0.20s`
  / `40788 KB`; Debug build passed; targeted CTest
  `test_spawn_sources_bin` passed; full Debug CTest was `11/12` with
  only the pre-existing `test_encounter_prims` Scorpia HP assertion
  failing.

## 2026-04-27 — Drops and Non-Drop Acquisition Parity Closure

- Change made: closed the drops/non-drop source/export parity lane by
  sharing acquisition-name normalization, regenerating drop/acquisition
  binaries, adding a unified acquisition-source index, and correcting
  shared drop-table extraction.
- Why it was made: `drops.txt` treated every non-NPC `dropsline`
  source as an unresolved NPC, which hid the real state of the database:
  NPC drops, skill/object/container drops, shops, recipes, and shared
  drop tables existed as separate datasets but had no unified parity
  surface.
- Exact surfaces changed:
  - `tools/acquisition_common.py` added shared wiki-cache loading,
    item/NPC name normalization, source resolution, and non-NPC source
    classification
  - `tools/export_drops.py` now resolves sources before items, splits
    NPC-source rows from non-NPC acquisition rows, reports `0` true
    unresolved item names for resolved NPC sources, and classifies the
    two non-item NPC reward rows separately
  - `tools/export_skill_drops.py`, `tools/export_shops.py`, and
    `tools/export_recipes.py` now use the shared item/source resolver
  - `tools/scrape_rdt.py` now extracts section-specific RDT/GDT/MRDT
    data from cached wiki pages and labels `item`, `nothing`, and
    `table_ref` rows in `tools/reports/rdt_gdt.txt`
  - `tools/export_acquisition_index.py` added the cross-dataset
    acquisition inventory
  - `data/defs/drops.bin`, `skill_drops.bin`, `shops.bin`,
    `recipes.bin`, `rdt.bin`, `gdt.bin`, and `mrdt.bin` regenerated
  - `data/defs/acquisition_sources.bin` added with `5163` acquisition
    source rows across NPC drops, non-NPC drops, shops, recipes, and
    shared drop tables
  - `tools/reports/drops.txt`, `skill_drops.txt`, `rdt_gdt.txt`, and
    `acquisition_sources.txt` regenerated
  - `tests/test_acquisition_sources_bin.c` added binary smoke coverage
  - `work.md`, `database.md`, and `temp_databaseaudit.md` updated so
    drops/non-drop source/export parity is no longer the active
    database lane
- Upstream/downstream impacts: acquisition source coverage is now
  auditable across the split binaries without changing their runtime
  formats. Runtime loading/ownership is still deferred; consumers
  should keep using the detailed binaries (`drops.bin`,
  `skill_drops.bin`, `shops.bin`, `recipes.bin`, RDT/GDT/MRDT) until a
  dedicated runtime acquisition owner exists.
- Verification: relevant Python scripts compile; acquisition index
  trace coverage executed the changed `export_acquisition_index.py` and
  `acquisition_common.py` paths with no missed lines in those files;
  targeted Debug CTest `test_acquisition_sources_bin` passed; full
  assertion-enabled Debug CTest was `10/11` with only the pre-existing
  `test_encounter_prims` Scorpia HP assertion failing. Export timings:
  drops `0.33s` / `22080 KB`, skill drops `0.29s` / `18804 KB`,
  shared tables `0.11s` / `31896 KB`, shops `0.10s` / `19252 KB`,
  recipes `0.12s` / `19232 KB`, acquisition index `0.28s` /
  `17748 KB`.

## 2026-04-26 — Localized Cache/Render Pipeline Helpers

- Change made: removed RuneC references to the deleted external
  cache/render helper tree and vendored the cache pipeline helper modules
  into `tools/cache_pipeline/`.
- Why it was made: RuneC's rendering/data-export pipeline must execute
  from RuneC, not from another project checkout. Hardcoded external
  helper paths made the repo fragile and could silently reintroduce
  deleted reference trees.
- Exact surfaces changed:
  - `tools/cache_pipeline/*.py` now contains the local cache/model/object/
    collision/terrain/animation helper modules used by RuneC exporters
  - `tools/cache_item_defs.py`, `tools/export_collision.py`,
    `tools/export_npcs.py`, `tools/export_npc_anims.py`,
    `tools/export_npc_models_full.py`, `tools/export_objects_bridge.py`,
    `tools/export_objects_all_planes.sh`, `tools/export_varbits.py`,
    and `tools/export_varps.py` now import helpers from
    `tools/cache_pipeline/`
  - `tools/export_npc_models_full.py`, `tools/export_varbits.py`, and
    `tools/export_varps.py` continue to use `ModernCacheReader`, now from
    the local helper tree
  - `tools/source_paths.py` centralizes raw source-corpus roots under
    local `data/source/`; exporters do not support an environment
    override to another local repository checkout
  - `.gitignore` now ignores `data/source/` so large mirrored source
    corpora stay local unless intentionally handled separately
  - Database/export/audit scripts no longer hardcode the working-tree
    root; repo-local paths now derive from `Path(__file__).parents[1]`
  - `data/curated/activity_spawns.toml` now stores curated/source labels
    instead of absolute external helper paths for Zulrah/Inferno/Fight
    Cave/object-activity sources
  - `README.md`, `work.md`, `database.md`, `rc-content/README.md`,
    `rc-viewer/README.md`,
    `rc-content/encounters/kalphite_queen.c`,
    `tools/reports/encounters.txt`, and `tools/reports/drops.txt` were
    cleaned so they no longer contain stale absolute checkout paths
  - `database.md` now distinguishes local executable pipeline helpers
    from raw source-input corpora
- Upstream/downstream impacts: cache-backed item/NPC/model/object/
  collision/varbit/varp exporters no longer require another repo's
  helper scripts at runtime. Source corpora such as `runelite`,
  `data_osrs`, `osrsreboxed-db`, cache dumps, and model dumps are now
  expected under `data/source/` when full regeneration is needed.
  Current compiled runtime data and render assets already live under
  RuneC's `data/` tree; `data/source/` is local-only and may be absent
  until its tarball/mirror is restored.
- Verification: repository search finds no references to the deleted
  helper tree, no external cache-helper script paths, no hardcoded
  reference-checkout root, and no hardcoded RuneC checkout root in RuneC;
  changed Python scripts compile and import; local cache-pipeline helper
  scripts compile; `tools/export_objects_all_planes.sh` passes `bash -n`;
  `data/curated/activity_spawns.toml` parses with `tomllib`.

## 2026-04-26 — Item Parity Closure Pass

- Reason: close the known item-database completion debts without
  moving item effects into runtime before consumables/special attacks
  exist. OSRS Wiki is the source of truth for equipment bonuses.
- Added a current-cache item config reader for OpenRS2 OSRS cache
  `2523` item group `2/10`, then wired it into `tools/export_items.py`.
- Regenerated `data/defs/items.bin` with `30197` IDEF v2 rows:
  `1469` wiki-only supplemental rows are cache-backed, `505`
  stale/null wiki rows are ignored, `825` incomplete rows are resolved,
  and `16` null cache forms are ignored.
- Added triage reports for incomplete rows and wiki supplemental rows.
- Added `tools/export_item_effects.py` and generated
  `data/curated/items/effects.toml` from RuneLite itemstats plus curated
  special-attack TOMLs: `361` effect rows, `997` item ID references.
- Fixed RuneLite itemstats extraction to use canonical top-level item
  IDs instead of noted/placeholder nested constants.
- Resolved all `20` curated special-attack TOMLs that were missing
  item IDs and updated `tools/scrape_item_specials.py` so cached wiki
  page infobox IDs are used when re-running the scraper.
- Added the OSRS Wiki equipment-bonus override path. Resolved current
  item rows now use wiki bonus values as source of truth where a wiki
  bonus row maps to a current item definition.
- Improved bonus cross-validation by de-duplicating wiki bonus rows,
  adding normalized name resolution, and rerunning
  `tools/reports/xvalidate_bonuses.txt`; matched current items now show
  `0` bonus mismatches. The `114` nonzero wiki bonus rows that do not
  resolve to current cache/reboxed item definitions are unresolved
  source-coverage rows; no mismatched values are accepted for resolved
  current item rows.
- Item mesh ownership decision: `rc-core` stores item/form/model IDs;
  `rc-viewer` and tools own ground/worn mesh decode and rendering.
- Updated `tests/test_items_bin.c` to assert the high-ID supplemental
  item `33368` has current cache value/model IDs and that wiki bonus
  overrides affect representative cache-backed and supplemental
  equipment rows.
- Files/data changed:
  - `tools/cache_item_defs.py` added the current-cache item config
    decoder used by item export
  - `tools/wiki_item_bonuses.py` added shared wiki bonus resolution and
    override selection
  - `tools/export_items.py` now merges cache-backed supplemental rows,
    applies wiki bonus overrides, writes IDEF v2, and emits item reports
  - `tools/export_item_effects.py` added generated item-effect source
    rows from RuneLite itemstats plus curated specials
  - `tools/xvalidate.py` now validates the same wiki bonus override
    table the exporter applies
  - `tools/scrape_item_specials.py` now falls back to cached wiki
    infobox IDs when name-to-ID resolution is missing
  - `data/curated/specials/*.toml` updated for the 20 missing item-ID
    files
  - `data/curated/items/effects.toml`, `data/defs/items.bin`, and item
    reports under `tools/reports/` regenerated
  - `tests/test_items_bin.c` extended for current-cache, supplemental,
    and wiki-bonus coverage
  - `work.md`, `database.md`, `temp_databaseaudit.md` updated to mark
    item database completion closed for the current item-data scope
- Verification: script bytecode compile passed; generated effects TOML
  parses; targeted item test passed; full CTest remains `9/10` with the
  known `test_encounter_prims` Scorpia assertion failing; gcov item path
  coverage was `items.c 72.43%`, `world.c 75.44%`,
  `test_items_bin.c 100%`; Python trace executed the changed tool paths.
- Timings: item export `2.89s` / `153412 KB`; item effects export
  `0.04s` / `19012 KB`; xvalidate `2.19s` / `129396 KB`; item load test
  `<0.01s` / `15804 KB`.
- End-of-day status: item database completion is closed for the current
  definition/equipment/form/model/effects-source scope. Runtime
  item-effect loading was not added in this pass because the
  consumables/special-attack runtime owner does not exist yet.
- Final doc hygiene: reduced `work.md` to high-level current state and
  active work only; moved finished-work detail back to `changelog.md`
  and generated reports.

## 2026-04-26 — Item Database Runtime Bring-Up

- Reason: make the broad item corpus usable by the engine instead of
  leaving it as exporter-only data.
- Regenerated `data/defs/items.bin` as IDEF v2 with `29944` item rows.
- Preserved duplicate/form item IDs instead of dropping them, so
  noted/unnoted/placeholder/canonical variants are not lost at export
  time.
- Added linked base/noted/placeholder IDs and ground/male/female model
  ID links to the compiled item record.
- Implemented `rc_load_item_defs()`, raised `RC_MAX_ITEM_DEFS` to
  `65536`, and changed item lookup to ID-indexed runtime access so
  sparse high item IDs work.
- Wired item loading into `rc_world_create_config()` for inventory,
  equipment, consumable, loot, skills, and shops presets.
- Added `tests/test_items_bin.c` and `tools/reports/items_full.txt`.
- Files/data changed:
  - `rc-core/items.h` / `rc-core/items.c` gained IDEF v2 loading and
    ID-indexed lookup
  - `rc-core/types.h` raised item definition capacity
  - `rc-core/world.c` loads item definitions for item-using subsystem
    presets
  - `tools/export_items.py` emitted IDEF v2 records with form links and
    model IDs
  - `tests/test_items_bin.c` added the runtime load/smoke coverage
  - `data/defs/items.bin` regenerated
- Verification: targeted item test passed, full CTest is `9/10` with
  only the existing `test_encounter_prims` Scorpia assertion failing;
  coverage exercised the changed item/world load path; timed full item
  export was `0.67s` peak `69508 KB`, and item load smoke was `0.01s`
  peak `15804 KB`.
- Superseded by the Item Parity Closure Pass above for wiki-only
  supplemental rows, item-use/effect source rows, incomplete-row triage,
  item mesh ownership, and bonus mismatch/name-resolution debt.

## 2026-04-26 — NPC Mesh Seam Fix and Viewer Validation

- Reason: Varrock visual validation showed correct NPC identities but
  visible cracks between adjacent human-NPC body/clothing triangles.
- Fixed the remaining NPC mesh seam issue seen in Varrock visual
  validation. Root cause: face-priority metadata had been used as a
  tiny geometry offset, which physically separated adjacent NPC
  triangles and created visible cracks in capes, torsos, sleeves, and
  hair.
- Kept face priorities in `data/models/npcs.models` as metadata, but
  removed all priority-based vertex
  displacement from static NPC model export and animated mesh re-upload.
- Regenerated `data/models/npcs.models` from the b237 cache-aligned
  NPC visual source path. Current report: `12945` exported renderable
  NPC meshes, `0` oversized meshes, `0` partial model loads, and `5`
  empty/no-render meshes.
- Files/data changed:
  - `tools/export_npc_models_full.py` kept face priorities as metadata
    but stopped applying geometry displacement
  - `rc-viewer/anims.h` mirrors the exporter path without priority
    displacement during animated mesh re-upload
  - `data/models/npcs.models` regenerated
- Verified the viewer path with targeted NPC tests, full viewer smoke,
  coverage smoke, and timed export/viewer checks. Full CTest remains
  `8/9` because of the existing unrelated
  `test_encounter_prims` Scorpia HP assertion.

## 2026-04-25 — Cache-Aligned NPC Visual Source Fix

- Reason: Varrock smoke showed several human NPCs wearing wrong gear or
  body parts because the visual source did not match the cache used by
  the model exporter.
- Corrected the NPC visual source priority after Varrock smoke testing:
  b237 `model_dump` is now the authority for NPC model IDs, recolors,
  and resize fields because it matches the cache decoded by the model
  exporter.
- Kept `data_osrs` as fallback metadata for NPCs absent from
  `model_dump`, not as the primary render source.
- Regenerated `data/defs/npc_defs.bin` and `data/models/npcs.models`.
  Varrock visible-plane NDEF model IDs now have `0` mismatches against
  b237 `model_dump`.
- Preserved NPC face render priorities in `data/models/npcs.models` and
  restored them as explicit metadata. The
  temporary geometry-bias approach was removed on 2026-04-26 because it
  created visible seams.
- Verified `rc-viewer` builds and reaches `Viewer ready` with `15182`
  NPC defs, `235` Varrock spawns, and `116` visible-plane NPC models.
- Added `RC_VIEWER_EXIT_FRAMES` for clean automated viewer smoke runs
  and fixed Raylib shutdown order so smoke exits do not segfault.
- Re-ran NPC semantic/reconciliation reports; ID, spawn, drop-table,
  morph-target, varbit, and varp coverage stayed complete.
- Files/data changed:
  - `tools/export_npc_defs_full.py` changed NPC visual source priority
    to cache-aligned `model_dump`
  - `tools/export_npc_models_full.py` regenerated meshes against that
    source
  - `rc-viewer/viewer.c` gained the clean smoke-exit path
  - `data/defs/npc_defs.bin` and `data/models/npcs.models` regenerated
- Scope exclusion for this pass: runtime varbit/varp morph evaluation
  was not changed.

## 2026-04-24 — Varrock NPC Visual Smoke Fix

- Reason: first Varrock NPC visual smoke after broad NPC export exposed
  wrong human meshes, oversized NPCs, primitive fallbacks, and upper
  floor NPC placement issues.
- Fixed NPC visual model source priority: `data_osrs` model IDs now win
  over the secondary model dump, preventing invalid human body/equipment
  meshes from replacing correct spawn-source models. Superseded on
  2026-04-25 after b237 source alignment showed `model_dump` is the
  correct render authority for this cache.
- Regenerated `data/defs/npc_defs.bin` and `data/models/npcs.models`
  from the corrected NPC visual source mapping against OpenRS2 OSRS
  build-237 cache data.
- Updated `tools/export_npc_models_full.py` to use `data_osrs`
  recolor metadata when exporting NPC meshes from `data_osrs` model IDs.
- Updated `rc-viewer` NPC loading/rendering so Varrock visual smoke:
  loads only visible-plane NPC models, does not collapse upper-floor
  NPCs onto ground level, and no longer draws pink fallback primitives
  for morph-only/no-model NPC placeholders.
- Files/data changed:
  - `tools/export_npc_defs_full.py` and
    `tools/export_npc_models_full.py` updated for the then-current
    `data_osrs` visual-source priority
  - `rc-viewer/viewer.c` changed visible-plane and fallback rendering
    behavior
  - `data/defs/npc_defs.bin` and `data/models/npcs.models` regenerated
- Verified viewer smoke reaches `Viewer ready` with `15182` NPC defs,
  `235` Varrock spawns, and `116` visible-plane NPC models loaded.
- Scope exclusion for this pass: runtime varbit/varp morph evaluation
  was not changed.

## 2026-04-24 — NPC Database Closure Slice

- Reason: replace slice-sized NPC runtime/database coverage with a broad
  NPC definition corpus and close ID/stat/morph reconciliation for the
  current sources.
- Added `tools/export_npc_models_full.py` and regenerated
  `data/models/npcs.models` from the broad NDEF v3 model-link corpus.
- Fixed the b237 type-1 model decoder path so face-render-type stream
  presence uses bit 0 instead of `== 1`; regenerated
  `data/models/npcs.models` with all `12950` linked NPC meshes and zero
  MDL2 oversize/unsupported misses.
- Updated `rc-viewer/models.h` to use dynamic model storage and direct
  ID indexing instead of the old fixed `512` model cap.
- Fixed `tools/export_npc_defs_full.py` so valid zero-valued cache/wiki
  stats are preserved instead of treated as missing.
- Regenerated `data/defs/npc_defs.bin` with `15182` broad NPC
  definitions and zero non-excluded RuneLite ID misses.
- Added `tools/audit_npc_semantics.py`; semantic reconciliation now
  reports complete wiki monster ID presence, `0` wiki-backed field
  conflicts, `0` NPC morph varbit misses, and `0` NPC morph varp
  misses.
- Upgraded `tools/export_varbits.py` to emit cache-backed VBIT v2 rows
  with base varp + bit range metadata, added `tools/export_varps.py`,
  and regenerated `data/defs/varbits.bin` / `data/defs/varps.bin`
  from the 2026-04-23 OpenRS2 OSRS build-237 cache.
- Raised `RC_MAX_NPCS` to `30000` and extended
  `tests/test_npc_defs_bin.c` to load the full static spawn binary
  manually without forcing all modular presets to auto-load spawns.
- Rechecked Nex and Sol Heredit against OSRS Wiki pages/cache,
  `data_osrs`, RuneLite, and model-dump config surfaces; exact combat
  spawn tiles were not inferred because no authoritative
  server-script/activity-config source had been extracted.
- Files/data changed:
  - `tools/export_npc_defs_full.py`,
    `tools/export_npc_models_full.py`, `tools/audit_npc_semantics.py`,
    and `tools/audit_npc_reconciliation.py`
  - `tools/export_varbits.py` and `tools/export_varps.py`
  - `rc-core/npc.*`, `rc-core/types.h`, `rc-core/world.c`, and
    `tests/test_npc_defs_bin.c`
  - `rc-viewer/models.h`
  - `data/defs/npc_defs.bin`, `data/defs/varbits.bin`,
    `data/defs/varps.bin`, `data/models/npcs.models`, and related
    reports regenerated

## 2026-04-15 — Initial Build

### Project Setup
- Created flat C project structure: `rc-core/` (game backend), `rc-viewer/` (Raylib frontend), `rc-cache/` (empty, for future cache decoder), `tools/` (Python export scripts), `tests/`, `data/` (assets), `lib/` (third-party)
- CMake build system: `rc-core` compiles as static library, `rc-viewer` links against it + Raylib, tests link against `rc-core`
- Raylib 5.5 prebuilt copied from FC project (`runescape-rl/claude/demo-env/raylib/`) into `lib/raylib/`

### rc-core — Game Backend
Created 11 source files with headers:

**types.h** — All game structs and constants:
- `RcWorld`: top-level game state (player, NPCs, ground items, world map, tick counter, RNG state)
- `RcPlayer`: position, route, combat, prayer, skills, inventory, equipment, timers
- `RcNpc`: live NPC instance with position, HP, AI state, pending hits
- `RcTile`: per-tile collision flags + height + overlay/underlay
- `RcRegion`: 64x64x4 tile grid with region coordinates
- `RcWorldMap`: up to 32 loaded regions
- `RcRoute`: BFS pathfinding result (waypoints array)
- `RcPendingHit`: delayed damage with prayer snapshot
- Collision flag constants matching OSRS exactly (from RSMod `CollisionFlag.kt` / RuneLite `CollisionDataFlag.java`):
  - Wall directions: `COL_WALL_NW` through `COL_WALL_W` (0x1-0x80)
  - `COL_LOC` (0x100), `COL_GROUND_DECOR` (0x40000), `COL_BLOCK_WALK` (0x200000)
  - Composite block flags: `COL_BLOCK_N` through `COL_BLOCK_SW` — each combines the wall flag facing the entry direction + LOC + BLOCK_WALK + GROUND_DECOR

**api.h** — Public API: `rc_world_create`, `rc_world_destroy`, `rc_world_tick`, player input functions, state queries

**world.c** — World lifecycle. `rc_world_create` allocates with `calloc`, initializes player at Varrock square (3213, 3428) with level 1 stats and 10 HP

**tick.c** — 8-phase tick loop matching RSMod `GameCycle.kt` order: (1) player input, (2) route computation, (3) NPC processing, (4) player movement/combat/skilling, (5) pending hit resolution, (6) prayer drain, (7) stat regen, (8) death checks. Most phases are stubs.

**pathfinding.c** — BFS pathfinding on 128x128 search grid with directional collision.
- `rc_get_flags(map, x, y, plane)`: converts world coords to region+local, looks up collision flags. Returns 0 for unloaded regions (walkable by default).
- `rc_can_move(map, x, y, dx, dy, plane)`: checks if size-1 entity can step in direction. Matches RSMod `routeFindSize1()` — checks DESTINATION tile for composite block flags. Cardinals check one tile; diagonals check destination + both adjacent cardinal tiles.
- `rc_find_path()`: BFS from start to dest. 128x128 grid centered on start, 8 directions, traces path backwards from end to start. Supports alternative destinations when target is unreachable.
- `rc_has_los()`: Bresenham line check for projectile blocking.

**combat.c** — Hit chance formula (OSRS-accurate): `if att > def: 1-(def+2)/(2*(att+1))`, else `att/(2*(def+1))`. Pending hit queue with `rc_queue_hit`.

**prayer.c** — Counter-based drain matching OSRS exactly. Accumulates drain rate per tick, subtracts prayer point when counter exceeds resistance (60 + 2*prayer_bonus). 1-tick flicks are free. All prayer drain rates and combat bonus percentages.

**skills.c** — Precomputed XP table for levels 1-99. `rc_level_for_xp` binary search. `rc_combat_level` formula. `rc_add_xp` with auto level-up.

**items.c** — Inventory operations (add/remove/find/free_slot). Equipment bonus recalculation summing all 14 bonus types across 11 equipment slots.

**rng.h** — XORshift32 inline. `rc_rng_next` and `rc_rng_range`.

**npc.c, shops.c, dialogue.c, quests.c** — API defined, implementations stubbed.

### rc-viewer — Raylib Frontend

**terrain.h** — Loads TERR binary format (magic 0x54455252). Parses vertex count, region count, world origin, vertex positions (float[N*3]), vertex colors (uint8[N*4]), heightmap (float grid). Computes per-triangle normals. Creates Raylib Mesh/Model. `terrain_offset()` shifts vertices to local coordinates. `terrain_height_at/avg()` for ground-level queries. Ported directly from FC `fc_terrain_loader.h`.

**objects.h** — Loads OBJ2 binary format (magic 0x4F424A32) with optional texture atlas (ATLS, magic 0x41544C53). Parses vertices, colors, texture coordinates. Normal computation identical to terrain. Atlas loaded as Raylib texture and assigned to model's diffuse material. `objects_offset()` for coordinate shifting. Ported from FC `fc_objects_loader.h`.

**models.h** — Loads MDL2 binary format (magic 0x4D444C32). Per-model: model ID, expanded vertices (float), colors (uint8), base vertices (int16 for animation), vertex skins (uint8 group labels), face indices (uint16). Vertices scaled from OSRS units to tile units (÷128) with Z negated for Raylib's right-handed coords. Ported from FC `fc_npc_models.h`.

**anims.h** — Full OSRS vertex-group animation system. Direct copy of FC `fc_anim_loader.h`. Loads ANIM binary (magic 0x414E494D) with framebases and sequences. Transform types: origin (type 0, compute vertex group centroid), translate (type 1), rotate (type 2, Euler Z-X-Y with 2048-entry fixed-point sine table), scale (type 3, 128=1.0x). `anim_apply_frame()` resets to base pose then applies per-slot transforms. `anim_update_mesh()` re-expands base verts through face indices into rendering mesh. Supports interleaved two-track animation for walk+action blending.

**collision.h** — Loads .cmap binary (magic 0x434D4150). Reads mapsquare key, extracts region_x/region_y, fills RcRegion tiles with collision flags. Regions stored in world coordinates for direct use by `rc_get_flags`.

**viewer.c** — Main application:
- Spherical orbit camera: `position = target + dist * (cos(pitch)*sin(yaw), sin(pitch), cos(pitch)*cos(yaw))`. Right-drag orbits, scroll zooms, presets 4 (overview) and 5 (tactical). L key toggles player-follow lock.
- Click-to-move: screen→world raycast intersecting ground plane, converts to world tile coordinates, runs BFS pathfinding, stores route in player struct.
- Smooth interpolation: `tick_frac` (0.0 after tick, approaches 1.0 before next) interpolates player position between game ticks for 60fps rendering from ~1.67 TPS game speed.
- Player rendering: MDL2 model with animation (idle/walk/run switching based on movement state). Falls back to blue cube if model unavailable.
- Collision overlay (C key): renders red cubes for BLOCK_WALK/LOC tiles, yellow lines for directional wall flags.
- Player coordinates: world space for pathfinding, converted to local via `LOCAL_X/LOCAL_Y` macros for rendering (subtract WORLD_ORIGIN).

### Asset Pipeline

**Cache**: OSRS b237 from OpenRS2 archive #2509, extracted to `/tmp/osrs_cache_modern/cache/`. XTEA keys from b236 archive (b237 keys not submitted; b236 keys work for same regions).

**Export scripts** (now localized under `tools/cache_pipeline/`):
- `export_terrain.py` → `varrock.terrain` (TERR, 608k verts, 320x320 heightmap, 10MB)
- `export_objects.py` → `varrock.objects` (OBJ2, 18.7M verts, 430MB) + `varrock.atlas` (ATLS, 2048x1792, 15MB)
- Player model/anims copied from FC project: `player.models` (MDL2, 947 tris), `player.anims` (6 sequences)

**Region coverage**: 25 regions (5x5 grid, 48-52 × 51-55), 320x320 tiles. Bounds: River Lum/wilderness (NW) to Digsite/Al Kharid (SE) to Draynor Manor (SW).

### Collision System

**tools/export_collision.py** — Custom collision exporter for b237 cache. Key design decisions and fixes:

1. **Uses reference `parse_objects_modern()` and `parse_terrain()` from `export_collision_map_modern.py`** — these are proven correct for collision marking (wall directions, occupant blocking, terrain blocking). Previous attempts to reimplement this parser had multiple bugs.

2. **b237 object definition parser (`decode_obj_defs_b237`)** — the reference `decode_modern_obj_defs` doesn't handle b237's opcodes 6 and 7 (int32 model IDs instead of uint16). This caused 4,555 object definitions to fail parsing, making them invisible to collision. The b237 parser handles all opcodes including 6 (typed models with int32 IDs), 7 (untyped models with int32 IDs), 92/93 (transform variants), 100-102 (entityOps), and 249 (params).

3. **Cache reading via `reader.read_group(5, gid)` → file 1** — same code path as `export_objects.py`. The original `export_collision_map_modern.py` used `_read_raw()` + manual XTEA decryption which broke on b237's container format (zlib decompression error). `read_group()` handles decompression natively.

4. **No blanket plane merge** — earlier versions merged ALL plane 1 collision flags into plane 0, which incorrectly blocked building interiors and open areas with roofing collision. The reference code handles plane shifting correctly per-object via the `down_heights` set (from terrain's LINK_BELOW flag), so each object's collision is placed on the correct plane without any post-processing merge.

5. **`mark_wall` argument order** — the reference function signature is `(flags, direction, height, x, y, type, impenetrable)`. An earlier bug passed `(flags, height, x, y, type, direction, impenetrable)` — swapping direction and height — causing walls to be placed on wrong planes with wrong orientations.

**Collision flag values** — stored in .cmap using OSRS's exact bit values:
- Directional walls: 0x1 (NW) through 0x80 (W)
- LOC (solid object): 0x100
- GROUND_DECOR: 0x40000
- BLOCK_WALK (terrain/water/solid): 0x200000

**Movement checking** — `rc_can_move` in `pathfinding.c` matches RSMod's `routeFindSize1()`:
- Moving north: check dest tile for `COL_BLOCK_N = WALL_SOUTH | LOC | BLOCK_WALK | GROUND_DECOR`
- Moving east: check dest tile for `COL_BLOCK_E = WALL_WEST | LOC | BLOCK_WALK | GROUND_DECOR`
- Diagonals: check dest + both adjacent cardinal tiles
- This ensures walls block from both sides (a wall between two tiles places flags on both tiles) and the pathfinder correctly routes around all obstacles.

### Tests
- `test_combat.c` — hit chance formula, pending hit queue
- `test_pathfinding.c` — BFS around blocked tile
- `test_determinism.c` — same seed = same state hash
- `test_collision.c` — loads real varrock.cmap, verifies region lookup, flag values, and rc_can_move blocking

### Documentation
- `README.md` — full project plan, architecture, system designs, implementation phases
- `references.md` — RSMod vs RuneLite vs Void RSPS comparison
- `work.md` — done/todo tracking, known issues
- `changelog.md` — this file

---

## 2026-04-15 — Collision, Player Model, Object Rendering Fixes

### Collision System — Multiple Bug Fixes

**Bug 1: Collision flag value mismatch (types.h)**
- `COL_BLOCK_WALK` was `(1 << 10)` = `0x400`. The exported .cmap uses OSRS's actual value `0x200000`.
- Fixed: all collision constants in `types.h` now use exact OSRS values from RSMod `CollisionFlag.kt` / RuneLite `CollisionDataFlag.java`.
- Added composite block flags (`COL_BLOCK_N` through `COL_BLOCK_SW`) matching RSMod's movement checking pattern.

**Bug 2: Wrong movement checking logic (pathfinding.c)**
- `rc_can_move` was checking the CURRENT tile for wall flags. RSMod checks the DESTINATION tile for composite block flags.
- Rewrote `rc_can_move` to match RSMod `routeFindSize1()`: cardinals check dest tile for composite flag (e.g. moving north checks dest for `WALL_SOUTH | LOC | BLOCK_WALK | GROUND_DECOR`), diagonals check dest + both adjacent cardinal tiles.

**Bug 3: Broken object placement parser in export_collision.py**
- Custom `read_smart` / `read_extended_smart` implementation produced wrong object IDs, causing obj_defs lookup to fail for most objects.
- Fixed: replaced with reference `io.BytesIO` readers from `modern_cache_reader.py` (`read_smart`, `_read_extended_smart`) — same proven code path as `export_objects.py`.

**Bug 4: mark_wall argument order**
- Called `mark_wall(flags, height, x, y, type, rotation, imp)` but the reference signature is `(flags, direction, height, x, y, type, imp)`. Direction and height were swapped, causing walls to be placed on wrong planes with wrong orientations.
- Fixed: uses `parse_objects_modern()` from `export_collision_map_modern.py` directly which calls `mark_wall` correctly.

**Bug 5: b237 object definition opcodes 6/7 not handled**
- The reference `decode_modern_obj_defs` doesn't handle b237's opcodes 6 (typed models with int32 IDs) and 7 (untyped models with int32 IDs). 4,555 object definitions failed to parse, producing `None` for `obj_defs.get(obj_id)`, which `parse_objects_modern` skips with `if d is None: continue`.
- Added: `decode_obj_defs_b237()` in `export_collision.py` that handles all b237 opcodes including 6, 7, 92, 93, 100-102, 249.
- Result: 60,466 object definitions parsed (up from 55,911).

**Bug 6: Plane 1→0 merge done AFTER file write**
- The code that merges plane 1 collision flags into plane 0 ran after the .cmap binary was already written. The file contained un-merged data.
- Fixed: moved merge before the write.

**Bug 7: Blanket plane merge over-blocking**
- Merging ALL plane 1 flags to plane 0 dumped roofing collision (types 12-21) and upper floor objects onto the ground floor, blocking building interiors and open walkable areas.
- Fixed: removed blanket merge entirely. `parse_terrain()` and `parse_objects_modern()` already handle plane shifting correctly per-object via the `down_heights` set from terrain LINK_BELOW flags.

### Player Model

**Removed: Equipment-wearing player model**
- Previous model (id 99999, 2841 verts, 947 faces) was a composite from the FC project that included crossbow + black d'hide equipment.

**Added: Base (naked) player model**
- Extracted default male kit IDs from RSMod `Appearance.kt`: head=9, jaw=14, torso=109, arms=26, hands=33, legs=36, feet=42.
- Parsed IdentKit definitions from b237 cache (index 2, group 3). b237 uses opcode 5 for body model IDs (big-endian uint32) and opcode 70 for head model IDs (single big-endian uint32). The reference `KitLoader` uses opcode 2 with uint16 which doesn't work for b237.
- Kit 9 (HEAD): body model 28321 from opcode 5, head/chat model 28386 from opcode 70. Initially only opcode 5 was parsed, resulting in missing head — fixed by parsing opcode 70 to find the body model ID was actually in opcode 5 all along (28321), and the initial export missed it because op 70 with count=0 was hitting an early END byte.
- Combined 7 body part models: HEAD (28321), JAW (246), TORSO (28786), ARMS (26632), HANDS (176), LEGS (28285), FEET (181).
- Result: 1554 verts, 518 faces, 30KB (down from 2841/947/56KB with equipment).
- Colors flattened from `(r,g,b,a)` tuples to flat `[r,g,b,a,...]` byte array for MDL2 format.

### Player Animation Fix

**Bug: Wrong animation sequence IDs**
- Viewer used `ANIM_IDLE=808`, `ANIM_WALK=819`, `ANIM_RUN=824`. These IDs don't exist in `player.anims`.
- The exported sequences are: 829 (consuming), 836 (death), 4226 (walk), 4228 (run), 4230 (attack), 4591 (idle).
- Fixed: changed to `ANIM_IDLE=4591`, `ANIM_WALK=4226`, `ANIM_RUN=4228` matching FC viewer's `PLAYER_ANIM_IDLE/WALK/RUN`.

**Bug: Animation vertices not scaled to tile units**
- `anim_update_mesh()` writes raw OSRS int16 units (range ~-200 to +200) into the mesh vertex buffer without dividing by 128. The model loader initially scales by ÷128, but every frame the animation overwrites with unscaled values, making the player ~128x too large.
- Fixed: added per-vertex scale pass after `anim_update_mesh()`: `mv[i*3] /= 128.0f`, `mv[i*3+1] /= 128.0f`, `mv[i*3+2] /= -128.0f` (Z negated for Raylib coords).

### Player Facing Fix

**Bug: Player model didn't turn to face movement direction**
- `facing_angle` was computed as `atan2f(dx, dy)` which gives the world-space angle. But rendering uses negated Z (`pz = -world_y`), so the rotation was wrong.
- Fixed: changed to `atan2f(dx, -dy)` to account for the Z-flip.

### Lighting Shader — Added Then Removed

**Added:** Custom GLSL shader with ambient (0.55) + directional light applied to terrain, objects, and player model via `LoadShaderFromMemory`. Vertex shader passed normals, fragment shader computed `ambient + diff * 0.45`.

**Removed:** The export scripts already bake directional lighting into vertex colors during terrain/object export. The custom shader was just darkening everything without adding detail. Removed shader, removed `Shader lighting_shader` from ViewerState, removed shader assignment to model materials.

### Object Rendering — Plane 1 Support

**Problem:** `export_objects.py` filters `if height != 0: continue`, skipping all plane 1+ objects. This removes bridges (barbarian village bridge is on plane 1) and other ground-level elevated structures.

**Iteration 1 — Include all plane 1:** Changed filter to `if height > 1:`. Result: 72,083 objects. Problem: upper floor furniture, walls, and decorations from buildings rendered floating above ground.

**Iteration 2 — Plane 0 only (reverted):** Went back to `if height != 0:`. Result: 62,196 objects. Problem: bridge gone again.

**Iteration 3 — Plane 1 non-roof objects (current):** Created `tools/export_objects_bridge.py` that monkey-patches `parse_object_placements_modern()` to include plane 1 objects except roof types 12-21. Objects keep `height=1` so the exporter uses the plane 1 heightmap for vertical positioning (bridges render above water, not at ground level). Result: 69,554 objects — bridges render at correct elevation, no upper floor clutter.

**Why height=1 matters:** RSMod's `GameMapDecoder.kt` uses `tileHeights[visualLevel]` for object Y position. Plane 1 heightmap positions objects above the ground (above water for bridges). Setting height=0 would use plane 0 heightmap, placing the bridge at water level.

**Iteration 4 — RSMod LINK_BELOW visual level resolution (current):** Rewrote `export_objects_bridge.py` to implement RSMod `GameMapDecoder.kt` algorithm exactly. For each object at (x, y, level): check tile at (x, y, level+1) for LINK_BELOW flag; if set, resolved flags = tile above flags; if resolved flags have LINK_BELOW, visualLevel = level - 1; only include objects where visualLevel == 0. Objects keep original height for heightmap sampling.

**Critical terrain parser fix:** The terrain opcode parser was reading 1-byte opcodes, but RSMod `MapTileDecoder.kt` reads them as 2-byte unsigned shorts (big-endian). Overlays (opcode 2-49) also read a 2-byte ID. This caused the parser to misalign on every tile, producing garbage settings and finding zero LINK_BELOW tiles. Fixed to read 2-byte opcodes. Result: 1,128 LINK_BELOW tiles detected (was 0-2 with 1-byte parser).

**Result:** 61,387 objects — bridges render at correct elevation (plane 1 heightmap), upper floor objects excluded via visual level != 0, no roofing clutter.

### Collision Overlay

**Added:** C key toggles collision tile visualization. Red cubes for BLOCK_WALK/LOC tiles, yellow lines on tile edges for directional wall flags. Initially rendered only 20-tile radius around player (appeared as small cluster when zoomed out). Changed to render full 320x320 world for debugging.

### Git / Documentation

- Initialized git repo on `main` branch, created `testing` branch.
- Pushed to GitHub as public repo: `github.com/jordanbailey00/RuneC`
- `README.md` replaced with public-facing version (project overview, architecture, build instructions, credits).
- Original detailed README moved to `AGENT_README.md` (gitignored).
- `work.md`, `changelog.md`, `references.md` added to `.gitignore` (internal docs).
- `data/regions/varrock.*` (large exported assets 430MB+) added to `.gitignore`.
- `lib/raylib/lib/libraylib.a` tracked in git (2.7MB vendored dependency).

---

## Known Issues (active)

- **Texture UV mapping:** Textured faces (brick walls, trees, stumps) have incorrect wrapping. The export hardcodes UV coordinates (0,0),(1,0),(0,1) for all textured faces instead of computing proper UVs from the model's texture triangle projection (RuneLite `computeTextureUVCoordinates`). The texture atlas approach also can't handle GL_REPEAT tiling that OSRS uses. Needs proper texture triangle decoding and UV projection.
- **Missing objects:** Some objects still don't render (specific object IDs with missing/undecodable models).
- **Environment animations:** Static objects with animations (fountains, fires, flags) don't animate.

---

## 2026-04-16 — NPC system, animation, database planning

### NPC export pipeline — `tools/export_npcs.py` (new, ~500 lines)

Produces three binary artifacts consumed by `rc-core` + the viewer.

**Input sources and why each:**
- **b237 cache, index 2 group 9** — NPC definitions (name, size, combat level,
  HP, 6 stats, stand/walk/run/attack/death anim IDs, model IDs, recolor pairs,
  chathead models). Parsed with a full RuneLite `NpcLoader.decodeValues`
  opcode table: opcodes 1, 2, 12–18, 30–34, 40, 41, 60–62, 74–79, 93, 95–118,
  122–147, 249–253. Opcodes 61/62 are b237-specific int32 model IDs (legacy
  caches used u16 at opcodes 1/60) — both handled.
- **b237 cache, index 7** — per-body-part models referenced from an NPC def.
- **b237 cache, index 2 group 9 second pass** — builds an inverse map
  `display_name_lowercased → [npc_ids...]` across all 13,046 b237 NPCs.
  Used to resolve 2011Scape Kotlin constant names to b237 IDs.
- **2011Scape game repo** (fresh clone of github.com/2011Scape/game),
  read from the configured source checkout at
  `2011Scape-game/game/plugins/src/main/kotlin/gg/rsmod/plugins/content/areas/spawns/spawns_{regionId}.plugin.kts`.
  Each file is a hand-curated Kotlin DSL of `spawn_npc(npc = Npcs.X, x=N,
  z=N, height=N, walkRadius=N, direction=Direction.Y)` calls. Used as the
  spawn coordinate source because the OSRS **client** cache does *not*
  contain NPC spawn positions — RSMod's `MapNpcListEncoder.kt` explicit
  comment: *"Map npc spawns are a server-only group"*. RSMod itself only
  defines Lumbridge spawns in `content/areas/city/lumbridge/...`, so
  2011Scape is the only available source covering Varrock at authoritative
  per-tile precision.

**Kotlin-name → b237 ID resolution (`resolve_kotlin_to_b237`):**

The trailing numeric suffix on 2011Scape names like `BANKER_CLASSIC_MALE_PURPLE_44`
or `GRAND_EXCHANGE_CLERK_2240` is the **original-RuneScape** cache ID (2011Scape
emulates pre-2013 RuneScape, a different game from OSRS), which does *not* match
b237. Confirmed empirically: id 44 in 2011Scape's cache was "Banker classic male
purple"; id 44 in b237 is "Zombie". We therefore resolve by **base display
name**:

1. Strip trailing `_\d+` numeric suffix → `stem`.
2. Split by `_`, drop any tokens matching the `VARIANT_TOKENS` set
   (CLASSIC, LATEST, MODERN, MALE, FEMALE, PURPLE, GREY, BLACKSUIT,
   HANDSBEHIND, SPIKEYHAIR, STANDING, SITTING, WALKING, plus color names).
   Remaining tokens → lowercase space-joined display string.
3. Direct-ID hint: if the trailing 2011 ID happens to still point to an
   NPC with a word-overlapping name in b237, use it (rare but preserves
   correct variants where IDs carry through).
4. Exact match on the stripped display name in the b237 name map → use
   first matching ID.
5. Progressive prefix shortening: pop trailing tokens until a match lands.

Named characters (AUBURY, THESSALIA, LOWE, HORVIK, ZAFF, BARAEK, CURATOR_
HAIG_HALEN, etc.) have no variant tokens, so step 4 finds them directly.
Generic roles (banker, guard, man, barbarian) collapse to their base and
take whichever b237 variant is listed first (fine for MVP; we'll refine
per-variant matching in the database phase).

**Spawn parsing and filtering:**

Regex `spawn_npc\s*\(\s*npc\s*=\s*Npcs\.([A-Z0-9_]+)\s*,(?P<body>[^)]*)\)`
over all 589 `.plugin.kts` files finds spawns; per-spawn `ATTR_RE` extracts
`x/z/height/walkRadius/direction` from the body. Default bounds
`--bounds 3072,3264,3392,3520` (x_min,x_max,y_min,y_max) is a
Varrock-centered box. Out-of-bounds entries and unresolved names are
dropped with diagnostic output.

**Output artifacts (all binary):**

- `data/defs/npc_defs.bin` — **NDEF** magic `0x4E444546`. Layout:
  `magic u32 | version u32 | count u32 | per-entry: npc_id u32, size u8,
  combat_level i16, hitpoints u16, stats[6] u16, stand/walk/run/attack/
  death_anim i32×5, name_len u8, name[name_len]`. 79 entries × ~51 bytes =
  4044 bytes.
- `data/regions/varrock.npc-spawns.bin` — **NSPN** magic `0x4E53504E`.
  Layout: `magic | version | count | per-spawn: npc_id u32, x i32, y i32,
  plane u8, direction u8, wander_range u8`. 193 spawns × 15 bytes =
  2907 bytes.
- `data/models/npcs.models` — **MDL2** magic `0x4D444C32`. One entry per
  unique NPC ID. Each entry combines all body-part models listed in the
  NPC def into a single mesh: decodes each via cache index 7, applies the
  def's `recolors` (exact 15-bit HSL find→replace per face), passes through
  `expand_model` with `tex_colors` fallback (textured faces whose texture
  ID isn't in our atlas render as the texture's average HSL instead of
  pure black), concatenates vertices/colors/base-verts/skin-labels/
  face-indices into one flat record. 79 entries × ~45 KB average = 3.3 MB.

### NPC core loaders — `rc-core/npc.c` (was TODO stub; now full impl)

Implemented:

- `rc_load_npc_defs(path)` → NDEF reader. Fills the global `g_npc_defs`
  table. Sets baseline defaults per def: `wander_range=5`,
  `respawn_ticks=25`, `aggressive=false`, `aggro_range=0`. Linear scan
  `rc_npc_def_find(npc_id)` for lookup (79 entries, called once at spawn
  load time — no need for binary search).
- `rc_load_npc_spawns(world, path)` → NSPN reader. For each spawn: resolves
  def_idx via `rc_npc_def_find`, mutates
  `g_npc_defs[def_idx].wander_range = spawn.wander_range` if non-zero, then
  `rc_npc_spawn`. **Known issue (see below):** mutating def on spawn load
  is wrong for variable-per-spawn wander ranges — should be per-NPC.
- `rc_npc_spawn(world, def_idx, x, y, plane)` → allocates the next
  `world->npcs[]` slot, zeroes it, sets
  `def_id/uid/position/spawn_origin/prev_position/current_hp/target_uid=-1/active=true`.
  Returns the NPC array index.
- `rc_npc_tick(world, npc)` → OSRS wander AI, mirrors RSMod
  `NpcWanderModeProcessor`:
  1. If dead: decrement `death_timer` then `respawn_timer`; when both hit
     zero, respawn at `spawn_x/y` with full HP, clear target/pending hits.
  2. Decrement `attack_timer`.
  3. If no target and `wander_range > 0`: 1/8 RNG chance per tick to pick
     a random destination within `[spawn_x ± wander_range, spawn_y ± wander_range]`,
     step 1 tile toward it via `rc_can_move` (respects collision). Track
     `wander_timer` of idle ticks; after 500 idle ticks, respawn at
     `spawn_x/y`.

### NPC rendering — `rc-viewer/viewer.c`

Per-frame in `draw_scene`:
- `rc_get_npcs(world, &count)` returns the live array. Skip `!active` or
  `is_dead`.
- Interpolate between `(prev_x, prev_y)` and `(x, y)` using `tick_frac`
  (0..1 sub-tick factor) for smooth 60 FPS motion on top of 1.667 TPS
  ticks.
- Convert world → local render coords: `nx_r = (wx - WORLD_ORIGIN_X) +
  0.5*size`, `nz_r = -((wy - WORLD_ORIGIN_Y) + 0.5*size)` (Y negated for
  right-handed Raylib space; +0.5 centers entity on tile; size offset for
  multi-tile NPCs).
- Ground Y from terrain heightmap: `ny_r = ground_y(v, n->x, n->y)` (no
  plane support — all NPCs render at plane-0 height).
- Face angle: if NPC moved this tick, `atan2f(dx, -dy) * (180/π)` (same
  Z-flip convention as player model). Otherwise 0 = south.
- Model lookup: `ModelEntry *ne = model_find(npc_models, def->id)` — linear
  scan over 86 entries by cache ID.
- `DrawModelEx(ne->model, {nx_r, ny_r, nz_r}, {0,1,0}, face_angle,
  {1,1,1}, WHITE)` to render.
- Fallback: if no model found, `DrawCube` tinted red — makes missing
  NPCs visually obvious without crashing.

Bumped `MODEL_SET_MAX` in `rc-viewer/models.h` from 32 → 512 to fit the
NPC model count.

### NPC animation — `tools/export_npc_anims.py` + viewer wiring

**Exporter** (`tools/export_npc_anims.py`, ~60 lines) scans
`data/defs/npc_defs.bin` for every non-(-1) value of the 5 anim slots
across all NPC defs (79 defs × 5 = 395 slot values → 50 unique IDs after
de-dup and filtering -1). Injects those IDs into the reference
`export_animations.py`'s `NEEDED_ANIMATIONS` set (overwriting its
player-focused default), then invokes its `main()`. Result:
`data/anims/npcs.anims` (261 KB) containing **13 framebases, 50 sequences,
640 frames**. The b237 cache has 13,745 total sequences — we only export
referenced ones, keeps file small + load fast.

**Viewer integration:**

New `ViewerState` fields:
- `AnimCache *npc_anims` — separate from the player's `anims` (player
  uses anim IDs 4591/4226/4228 from `player.anims`; NPCs use 808/819/
  2064/... from `npcs.anims`). Loaded via `anim_cache_load(
  "data/anims/npcs.anims")`.
- `AnimModelState *npc_anim_state[RC_MAX_NPC_DEFS]` — **one per NPC def**.
  Each state holds the vertex-group lookup (built from the def's base-model
  `vertex_skins` labels) plus a scratch int16 vertex buffer for the animated
  base pose. Shared across NPC *instances* of the same type because each
  draw re-applies from `me->base_verts`, so cross-instance clobbering
  between consecutive draws is harmless.
- `npc_render[RC_MAX_NPCS].{cur_anim_id, frame_idx, frame_timer}` — per-
  instance anim progress. Lets two NPCs of the same type play independent
  frames (e.g., one walking while the other stands).

Startup sequence:
1. Load `npc_anims`.
2. Iterate `g_npc_defs[0..g_npc_def_count]`, look up each def's
   `ModelEntry` via `model_find(npc_models, def->id)`.
3. For each def with a loaded model and non-empty vertex_skins, call
   `anim_model_state_create(me->vertex_skins, me->base_vert_count)` →
   stored in `npc_anim_state[def_idx]`.
4. Log: `npc_anim: created 79 per-def anim states`.

Per-frame call path (new helper `update_npc_anim(v, npc_idx, me)`, invoked
from the NPC draw loop just before `DrawModelEx`):
1. Target anim selection: `n->is_dead && death_anim >= 0 → death_anim;
   moved_last_tick && walk_anim >= 0 → walk_anim; else stand_anim`. If
   target is -1 (NPC has no anim for that state), skip animation — draw
   the base pose.
2. Detect anim change: if `target != npc_render[i].cur_anim_id`, reset
   `frame_idx=0, frame_timer=0`.
3. Advance frame timer by `GetFrameTime() * 50.0f` (20 ms per client tick
   — OSRS convention). March through the sequence's per-frame `delay`
   values (variable length; e.g., anim 808 has 16 frames with delays
   ranging 2–6 ticks).
4. Resolve `AnimFrameBase *fb = anim_get_framebase(npc_anims,
   sf->frame.framebase_id)`.
5. `anim_apply_frame(state, me->base_verts, &sf->frame, fb)` — writes new
   int16 vertex positions into `state->verts` by applying the frame's
   transform list (each transform = slot_index + dx/dy/dz, interpreted as
   translate/rotate/scale based on `fb->types[slot]`) to the vertex groups
   defined by skin labels.
6. `anim_update_mesh(me->model.meshes[0].vertices, state,
   me->face_indices, me->face_count)` — expands indexed base verts into
   face-unrolled float verts in OSRS int16 units, applying Y-flip
   (OSRS Y is negative-up).
7. Scale: per-vertex `mv[i*3] /= 128.0f; mv[i*3+1] /= 128.0f; mv[i*3+2]
   /= -128.0f` (OSRS units → tile units; Z flipped for right-handed
   Raylib space).
8. `UpdateMeshBuffer(me->model.meshes[0], 0, mv, vc*3*sizeof(float), 0)`
   pushes to the GPU vertex buffer (glBufferSubData under the hood).

Then the caller's existing `DrawModelEx` sees the animated buffer state.

**Per-frame cost:** roughly 1 `UpdateMeshBuffer` per NPC (1–3 KB vertex
upload) + 500–2000 vertex transforms on CPU. At 193 NPCs × 60 FPS that's
approximately 6 ms/frame of animation work — tolerable. If it becomes a
bottleneck later, a natural optimization is skipping animation updates
for NPCs outside the camera view frustum.

**Cleanup:** `anim_cache_free(npc_anims)` + `anim_model_state_free` over
all `RC_MAX_NPC_DEFS` slots on exit.

### Database planning — `database.md` (new, ~350 lines)

Comprehensive plan written before the systems build-out so we know what
data is covered by existing repos vs what needs Wiki scraping.

**Audit methodology:** spawned four parallel Explore agents to crawl
`runelite/`, `rsmod/`, `void_rsps/`, `2011Scape-game/`, plus analyzed the
b237 cache via our existing export scripts. Each agent reported YES/
PARTIAL/NO for 20 data categories (NPC defs, spawns, drops, aggression,
dialogue, item defs, equipment bonuses, ground spawns, shop stock, quests,
skill mechanics, diaries, objects, areas, music, prayer, spellbook,
minigames, random events, combat formulas). Cross-checked findings with
OSRS Wiki MediaWiki Cargo table availability.

**Key findings — hard gaps (nothing in any cloned repo):**
1. **Item equipment bonuses** (stab/slash/crush att, mage-att, range-att,
   5× def bonuses, str, mage-dmg, range-str, prayer bonus) — literally
   zero repos store these.
2. **Comprehensive NPC drop tables** — Void has 121 partial TOML files,
   2011Scape has none, RuneLite has none.
3. **NPC aggression flags + aggro range** — scattered code in RSMod, no
   data file.
4. **OSRS content not in the pre-2013 RuneScape lineage** (NPCs like Haakon,
   Xuan, Herald of Varrock, Zeah/Kourend continent, Prifddinas, Vorkath,
   Nightmare) — Void and 2011Scape emulate pre-2013 RuneScape, a separate
   game from OSRS, so they simply don't contain this content.
5. **Varbit semantics** (what each of the ~15k varbits controls) — only
   IDs in RuneLite.
6. **GE prices** — no repo; use live API.
7. **Diary/achievement task details** — only requirement checks in
   RuneLite client plugins, no raw task list.

**External sources identified for integration:**
- `0xNeffarion/osrsreboxed-db` (GPL-3, 944 MB, pushed 2025-01-07) — item
  stats + NPC stats + monster data. Solves gaps #1, #3, #4, #7 in one
  repo. Per-item + summary JSON format.
- `mejrs/data_osrs` (46 MB, pushed 2025-11-23, very active) — varbit +
  varplayer semantics + cache cross-validation.
- `runelite/runelite` main repo — just the `gameval/*ID.java` constants
  (`NpcID.java`, `ItemID.java`, `ObjectID.java`, `VarbitID.java`) for
  human-readable ID names. BSD-2.
- **OSRS Wiki Cargo API** (no clone; queried at build time with disk
  cache) — `DropsLine`, `SpawnLines`, `QuestDetails`,
  `AchievementDiaryTask`, `VarbitDefinition`, `MonsterStats`, `ItemStats`
  tables. Authoritative for all OSRS content. CC-BY-NC-SA.
- `prices.runescape.wiki/api/v1/osrs/{latest,5m,1h,mapping}` — live GE
  prices, real-time, official wiki-maintained.

**Proposed storage layout:** flat binaries under `data/defs/` (npcs, items,
objects, prayers, spells, shops, drops, teleports, varbits, regions),
`data/quests/{id}.bin`, `data/diaries/{region}.bin`, `data/spawns/{region}.*.bin`,
`data/skills/{skill}.bin`. All binary at runtime — TOML/JSON only for
build-time pipeline.

**Proposed 5-phase build pipeline:** clone externals → extract cache →
merge osrsreboxed bonuses/aggression into NDEF/IDEF → fold in Void/
2011Scape TOML data for area-specific spawns/shops/ground-items →
scrape Wiki Cargo for remaining gaps (drops, OSRS spawns not covered by
2011Scape overlap, quests, diaries, varbits) → emit binaries with
cross-validation.

**`work.md` update:** inserted the database build as the new TODO #1,
renumbered combat/items/skills/interaction/quests/refactor/textures as
2–9. Rationale: every subsequent system needs the content database; doing
combat without drops or items without equipment bonuses would mean
hand-coding the data and re-doing it later.

### Full-world rendering exploration (completed, reverted)

Short-lived experiment to validate our streaming architecture could scale.
Exported the full OSRS surface (rx 16–65, ry 36–101 = 2091 regions):

- **New:** `tools/export_objects_per_region.py` — spawns N parallel
  subprocess workers, each calling `tools/export_objects_bridge.py` with
  `--regions "{rx},{ry}"` once per region, writing to
  `data/regions/objects/{rx}_{ry}.objects`. 8 workers × ~30 s/region ×
  2091 regions ÷ 8 workers = 30 min real time. Total 44 GB disk
  (~21 MB/region average; Varrock is 28 MB, wilderness edges <5 MB).
- Extended `tools/export_npcs.py` to support world-sized bounds
  (`--bounds 0,13500,0,13500`). Produced 12,673 NPC spawns across 1440
  unique NPC defs. `npcs.models` grew from 3.3 MB (86 Varrock models) to
  61 MB (1,440 world models).
- Ran the reference `export_terrain.py` over all 2091 regions in one call
  → 732 MB `world.terrain` (42,396,168 vertices).
- Ran `tools/export_collision.py` over all 2091 regions → 131 MB
  `world.cmap` (2,498,352 non-zero plane-0 tiles).
- **New streaming object pool** in `rc-viewer/objects.h`: `ObjectsPool`
  struct with `ObjectMesh* regions[256][256]` grid, `shared_atlas`
  Texture2D, `load_radius` Chebyshev-distance bound, `world_origin_x/y`
  for mesh offset. Functions `objects_pool_{create,load_region,
  unload_region,update,draw,free}`. Per tick: `objects_pool_update(p,
  center_rx, center_ry)` unloads anything where
  `abs(rx - center_rx) > r || abs(ry - center_ry) > r`, then loads every
  `(rx, ry)` within the (2r+1)² box whose file exists and isn't already
  loaded. Single shared atlas texture assigned to every loaded region's
  Model material (atlas pointer cleared before `UnloadModel` to prevent
  double-free on unload).
- Viewer constants bumped for full-world scale:
  - `WORLD_ORIGIN_X` 3072 → 1024, `WORLD_ORIGIN_Y` 3264 → 2304
  - `WORLD_W` 320 → 3200, `WORLD_H` 320 → 4224
  - `RC_MAX_NPCS` 256 → 16384, `RC_MAX_REGIONS` 32 → 2500
  - `RC_MAX_NPC_DEFS` 512 → 2048, `RC_MAX_ITEM_DEFS` 4096 → 32768
  - `RC_MAX_SHOPS` 32 → 256, `MODEL_SET_MAX` 512 → 4096
- Added per-NPC `int wander_range` field to `RcNpc` (types.h) so static
  NPCs could be tracked per-spawn rather than mutating the def.
- Added NPC marker debug visualization (N key) — tall colored vertical
  lines above every NPC color-coded by category (cyan banker, yellow GE
  clerk, red guard, green man, magenta other) visible through walls.
  Used to diagnose banker position mismatch at the GE.

**Results on first run:**
- 42M terrain vertices loaded, 2091 collision regions, 12,673 NPC spawns
  across 1440 defs.
- Objects streamed per tick — worked correctly, first tick froze for ~20 s
  loading 625 regions synchronously.
- Memory footprint at steady state ~10 GB RAM for the loaded object pool.

**Outcome — reverted via `git reset --hard testing-npc`:** all full-world
code + data discarded. Main returned to the Varrock-scope NPC commit
`6287a9e`. Rationale: the systems we're about to build (combat, skills,
shops, dialogue, quests) are per-entity logic independent of world scope;
Varrock already has every kind of NPC/object/skill interaction we need to
prototype. Full-world costs 44 GB disk, slow startup (20 s streaming lag),
and no proportional gain for system prototyping. The pre-export scripts
(`export_objects_per_region.py`, extended bounds in `export_npcs.py`) were
*not* committed to main and remain only in the testing-npc working
history — future us can reconstruct the pipeline from this changelog or
the `testing-npc` branch diff.

**Insight gained:** our per-placement vertex expansion (OBJ2 format stores
flattened mesh per region instead of referring to indexed model
definitions like OSRS does) is ~75× the disk footprint of OSRS's own cache
(600 MB total vs our 44 GB). Migrating to instanced placement rendering
(store model defs once, reference by ID + transform at each placement)
would close this gap and mirror OSRS's own architecture. Tracked against
TODO #9 (texture rendering overhaul) since both changes touch the same
object-rendering pipeline.

### Infrastructure / housekeeping

- Cloned `https://github.com/2011Scape/game.git` (shallow, default branch)
  into the source-checkout workspace. Added entry to
  `memory/reference_repos.md` explaining role + spawn DSL syntax.
- Created `testing-npc` branch from `main` via worktree at
  `/tmp/runec-testing` + committed the pre-marker NPC state as `6287a9e`.
  After the full-world revert, main was reset to this commit — `main` and
  `testing-npc` now point at the same commit.
- Unrelated historic `testing` branch (at the project's initial commit)
  left untouched.
- Worktree at `/tmp/runec-testing` kept around so the user can run that
  isolated copy via `cd /tmp/runec-testing && ./build/rc-viewer`.
  Symlinks from its `data/regions/varrock.{terrain,objects,atlas,cmap}`
  point to the main working dir's files (those gitignored assets are
  shared).

### Known issues introduced this cycle

- **Bankers / GE clerks / shopkeepers wander instead of staying static.**
  2011Scape omits `walkRadius=` for static NPCs (expecting 0); our exporter
  defaults to 5 when absent (`"wander_range": s["walk_radius"] if
  s["walk_radius"] > 0 else 5`), and the tick loop falls back to 5 when
  `def->wander_range == 0` (`int wander_range = def->wander_range > 0 ?
  def->wander_range : 5`). A per-NPC `wander_range` on `RcNpc` (rather
  than mutating the def) with `0 = static` semantics would fix this; the
  change was in the post-marker full-world branch and wasn't cherry-picked
  when we reverted to Varrock scope. Tracked as a focused fix for the next
  cycle.
- **Grand Exchange clerk positions don't match the OSRS cache map.**
  2011Scape (pre-2013 RuneScape emulator) places clerks at the four outer
  corners of the original-RS GE layout (SW, SE, NW, NE of the fenced
  area); OSRS's b237 map has the 8-booth circular arrangement in the
  center. The spawn data describes a different game's world. Fix requires
  Wiki Cargo `SpawnLines` for OSRS GE positions — queued for the database
  build.
- **10 named NPCs unresolved** (HAAKON_THE_CHAMPION, PROFESSOR_HENRY, SANI,
  MUSICIAN_8700, GYPSY_ARIS_9362, ERNIE, XUAN, URIST_LORIC,
  HERALD_OF_VARROCK, plus one). No match in b237 under those names
  (confirmed by full-name search across all 13,046 defs). Either they're
  OSRS-only NPCs not present in the cache build we're on, or 2011Scape's
  Kotlin name differs from the OSRS display name (2011Scape emulates
  pre-2013 RuneScape, a separate lineage). Wiki-sourced OSRS NPC data
  will close this gap.
- **Animation frame timer tied to real-time (`GetFrameTime()`) not game
  tick time.** Fine for visual loops (stand/walk); means death/attack
  anims won't sync with rc-core tick-scheduled events. Will need
  revisiting when combat lands.
- **No plane-aware NPC rendering.** `ground_y(world_x, world_y)` doesn't
  take plane, so any NPC with `plane > 0` (2 guards in Varrock castle
  upper floor) renders at plane-0 terrain height — visually submerged in
  the upper-floor geometry. Works fine for Varrock's bankers/clerks/most
  NPCs which are all plane 0; revisit alongside player plane-aware
  movement when we tackle stairs/ladders.

### Session wrap-up

- NPC animations verified visually: bankers stand-idle, men walk through
  their walk cycles, barbarians pace. All 79 NPC types animate with their
  cache-defined stand/walk sequences (13 framebases, 50 sequences, 640
  frames shared across the set).
- Committed to `main`: `rc-viewer/viewer.c` (anim integration +
  `update_npc_anim` helper), `tools/export_npc_anims.py` (new),
  `data/anims/npcs.anims` (new). Previous commit `6287a9e` (NPC loading,
  rendering, wander AI) stays as the preceding milestone.
- `work.md` TODO list reorganized around long-pole priorities. New
  ordering:
  - **#1 Build content database** (was already #1) — flagged explicitly
    as PRIORITY blocking downstream work.
  - **#2 Core / viewer isolation** (NEW) — audit target: `rc-core`
    compiles and runs headless without Raylib or any asset loader.
    Render/decode/mesh/UI code lives only in `rc-viewer` + `tools/`.
  - **#3 OSRS-style UI** (NEW) — chat, minimap, orbs, inventory,
    equipment paper doll, prayer/spellbook/skill tabs, right-click
    menus, NPC dialogue, shop, bank. Deferred until Phase 5 of #1
    emits the binaries the UI will consume.
  - **#4 NPC models + spawning** (was #2) — marked partially complete
    with remaining items (static-NPC `walk_radius==0` handling,
    attack/death anim hooks, plane-aware rendering).
  - **#5–11** — combat, items, skills, NPC interaction, quests,
    data-driven refactor, texture overhaul (renumbered from old 3–9).
  - **#12 Expand beyond Varrock to full OSRS surface world** (NEW,
    FINAL) — deferred until everything above ships. Scope notes
    include instanced placement rendering (the ~75× disk-footprint
    win we identified during the full-world exploration), plane-
    aware rendering, stream-ticking distant NPCs, per-instance
    wander_range (already designed in the reverted full-world branch).

---

## 2026-04-17 — Scope cuts: single-player, lightweight, no tracking systems

Decided RuneC is **single-player / no-multiplayer / lightweight**, focused
on **skilling / questing / combat & bossing**. Documented the full cut list
in a new `ignore.md` so future-us doesn't drift back into these. Every cut
has a paired rationale.

### Systems cut from requirements

**Multiplayer economy**
- Grand Exchange (offer slots, live price API, price history, buy limits).
- Player-to-player trade (request, 28-slot trade UI, confirm, wealth check).
  *Rationale:* no other players, no market, no shared economy. Item value
  is expressed via static shop prices + high/low alch from cache.

**Multiplayer social**
- Friends list, ignore list, private messaging.
- Clan chat system (tab, interface, ranks, clan hall).
- Chat tabs for public / private / clan / trade / GIM — collapsed to a
  single local channel for NPC dialogue + game/skill messages.
- Chat filter, chat commands (`::ge`, `::price`, etc.).
- Right-click "Report Player".
  *Rationale:* no other players to chat with, befriend, ignore, clan with,
  or report. One local system-message channel covers everything we need.

**Multiplayer infrastructure**
- Grouped Ironman (GIM tab, shared storage, group challenges).
- World select / world switcher.
- Hi-scores (requires shared snapshot).
- In-game polls.
- Bonds / membership currency. RuneC is always "members" for content
  purposes (or always f2p at build config); no runtime gate.
- Complex login / auth flow. Binary launches directly into save-slot
  picker; no account, no 2FA, no session tokens, no world queue.
  *Rationale:* all require shared server infrastructure or a subscription
  model we don't have.

**Tracking / achievement systems**
- Achievement diaries (12 regions × 4 tiers × ~576 tasks, `AchievementDiaryTask`
  Cargo scrape, diary journal UI).
- Combat achievements (~500 tasks across 7 tiers, `CombatAchievements`
  Cargo scrape, CA interface, tier rewards).
- Collection log (1400+ slots, per-slot drop tracking, clog-cape unlock,
  `CollectionLog` Cargo scrape).
  *Rationale:* all three are tracking overlays on top of gameplay that
  already exists. The meaningful rewards (Varrock armour mining bonus,
  fairy-ring access, gear recolors) become hand-coded always-on unlocks
  or quest-gated grants. No task-by-task verification.

**Random events**
- Drill Demon, Mime Theatre, Pillory, Frog Prince, Evil Bob, Kiss the Frog,
  Drunken Dwarf, Mysterious Old Man, Sandwich Lady, River Troll, Rick
  Turpentine.
- Per-skill-action random-event spawn rolls.
- Random-event teleport-to-island mechanics and reward outfits (camo,
  mime, zombie shirt, etc.).
  *Rationale:* mostly disabled in live OSRS since 2010. Adds interrupt
  complexity to every skill tick with near-zero gameplay benefit.

**PvP minigames** (PvP combat mechanics themselves are kept)
- Castle Wars, Clan Wars, Last Man Standing, Duel Arena, Bounty Hunter
  (target-finding + emblem trader loop), Emir's Arena, TzHaar Fight Pits
  PvP variant.
  *Rationale:* each requires other players. PvM wave minigames (Fight
  Caves, Inferno, Colosseum, Gauntlet, Nightmare Zone, Barrows, Barbarian
  Assault, Wintertodt, Tempoross, Volcanic Mine, Blast Furnace, Tithe Farm,
  Sepulchre, Motherlode, Guardians of the Rift, Mahogany Homes, etc.)
  remain in scope.

**Cosmetic / social systems**
- Emotes system (emotes panel, unlock chain, emote chat commands).
  Emote *animation IDs* from cache are still loaded — they're used by
  quests / cutscenes / death anims / NPC scripted sequences. There just
  isn't a player-facing emote picker.
- Character customization salons — barber (hairstyle), Thessalia's
  (clothing color), Makeover Mage (gender/skin). Initial character
  creation still sets these once; no mid-game salon NPCs with functional
  interaction.
- Pet insurance (Probita reclaim loop). Pets themselves remain as rare
  drops from bosses / skilling thresholds — they just don't have insurance
  or bank-pet storage.
  *Rationale:* aesthetic / social with no gameplay effect.

### Systems explicitly kept in scope

- **PvP combat mechanics**: wilderness PvP, skull system (20-min timer),
  3-item kept on PvP death, loot drop to killer. (PvP minigames are out,
  the underlying combat is in.)
- **Music** (per-region polygon mapping) — explicit keep.
- **All 23 skills**, **quests**, **prayer**, **magic**, **NPC AI**,
  **combat**, **inventory**, **bank** (simplified — no bank pin).
- **Pets** as rare drops (no insurance UI).
- **Bosses + raids** — single-player attempts work for most, group raids
  (ToB / CoX / TOA) can be soloed or scripted.
- **Clue scrolls + Treasure Trails** — kept pending final decision.

### Doc cleanup

- `things.md` §1.12 UI Panels: chat simplified to single local channel;
  Friends/Clan/GIM tabs dropped.
- `things.md` §1.16 Backend/Engine: save/load field list purged of
  friends/clan/GE/bond/trade; launch flow strips account/world-select.
- `things.md` §2.11 Multiplayer/network gaps: deleted entirely.
- `things.md` §2.12 QoL gaps: dropped barber, emote chain, chat commands,
  pet insurance, bonds. Renumbered to §2.11.
- `database_template.md` F.1 Minigames: PvP subset excluded from the
  intermediate TOML scrape list.
- `database_template.md` H.1 Emotes + H.2 Customization: removed entirely.
- `database.md` coverage matrix + data categories: GE prices, diary tasks,
  combat achievements, collection log, music track unlocks, random events
  rows all removed. Renumbered.
- `work.md` TODO #4 UI scope: Friends/Ignore removed; chat collapsed to
  one local channel.
- New `ignore.md` (14 sections) captures the full cut list with per-cut
  rationale + tangential consequences + revisit conditions.

### Internal doc gitignore

- `.gitignore` flipped to `*.md` + `!README.md`. Only `README.md` is
  tracked in git; `things.md`, `database.md`, `database_template.md`,
  `ignore.md`, `work.md`, `changelog.md`, `references.md` all live
  locally, never published.
- Rewrote in-progress commits `6287a9e` and `03153d9` via
  `git filter-branch` to remove `database.md` from commit history before
  the first push to `origin/main`. `03153d9` was database-only — pruned
  empty. `6287a9e` rewritten without database.md (→ `380c190`).
  `f460df5` rewritten unchanged (→ `acc8501`). Net result: 2 clean
  commits ahead of origin, no `.md` in either.

---

## 2026-04-18 — Phase 1 begins: OSRS Wiki Bucket client (Cargo is gone)

Built `tools/wiki_bucket.py` — generic OSRS Wiki query client. Key
discovery during bring-up: **the OSRS Wiki no longer supports Cargo**.
`action=cargoquery` now returns `badvalue`. Weird Gloop (the wiki
infrastructure operator) replaced Cargo + Semantic MediaWiki with a
custom "Bucket" extension.

**Bucket API:**
- Endpoint: `action=bucket&query=<DSL>`.
- DSL: `bucket('name').select('f1','f2').where('field','value').limit(n).offset(k).run()`.
- All names lowercase with underscores; fields are typed (TEXT, INTEGER,
  BOOLEAN, DOUBLE, PAGE; some are repeated).
- Reserved field `page_name` gives the source wiki page title — use as
  join key across buckets.
- Error shape for bucket errors is `{"error": "<string>"}`, not the
  standard MediaWiki `{"error": {"code", "info"}}` — handle both.
- Spec: `https://meta.weirdgloop.com/w/Bucket` (reachable intermittently
  from this box).

**Table mapping** (our Cargo plan → real bucket):
`DropsLine→dropsline`, `MonsterStats→infobox_monster` (50+ fields),
`ItemStats→infobox_bonuses`, `VarbitDefinition→varbit`,
`QuestDetails→quest`, `MusicTrack→music`, `SkillTraining→recipe`.
`TeleportLocation` has no direct bucket — split across `infobox_spell`
and `recipe`. NPC spawn positions: see follow-up entry below —
initially thought `locline` was scenery-only; turns out it has NPC
rows too, and `mejrs/data_osrs/NPCList_OSRS.json` is an even better
cache-ID-keyed source.

**Bonus buckets beyond plan:**
- `transcript` (full dialogue — huge win for quest NPC text)
- `storeline` (shop stock with quantities / restock)
- `infobox_item`, `infobox_scenery`, `infobox_spell`, `infobox_location`
- ID lookups: `npc_id`, `item_id`, `object_id`

**Etiquette implemented** (per MediaWiki `API:Etiquette`):
- Serial: one in-flight request (no parallelism).
- `maxlag=5` on every request; honor `Retry-After` on 503.
- UA `RuneC-data-builder/0.1 (jordanbaileypmp@gmail.com) python-requests/2.31`.
- Exponential backoff on `ratelimited`/429/503 (base 1s, cap 60s).
- 0.5s min interval between requests.
- Startup `userinfo&uiprop=ratelimits` probe (anon has no explicit
  read-rate cap; only edit/upload are capped).
- Disk cache under `tools/wiki_cache/{bucket}_{qhash}_{offset}.json`
  — re-runs are free.

**Smoke tests:** fetched `infobox_item` (5 rows) + `varbit` (2,897) +
`music` (1,187) + `quest` (225) + `infobox_bonuses` (~5.6k over 12
pages) successfully. Pagination and re-run-from-cache verified.

**Research notes added to `database.md`** summarizing osrsbox blog
findings + MediaWiki etiquette page + WeirdGloop reference — preserved
for future reference but treated as stale until reverified.

---

## 2026-04-18 (cont.) — NPC spawn gap actually already solved

After the Bucket scrape completed, revisited the two unresolved items:
NPC spawn positions and the "shallow" bucket scrapes. The NPC-spawn
gap (#2 in the coverage matrix) turns out to have been solved by Phase 0
assets we hadn't fully explored.

**`mejrs/data_osrs/NPCList_OSRS.json`** — a flat list of **24,110 NPC
spawn instances** already cloned locally. Each row:
`{id, name, x, y, p, size, combatLevel, walkingAnimation, standingAnimation,
actions, models, hasMinimapDot, category, ...}`. Cache-ID keyed so no
page-name resolution needed. This is literally an authoritative NPC
spawn dump for modern OSRS and obsoletes the 2011Scape fallback for any
NPC that exists in OSRS.

**`locline` bucket — correction.** I originally wrote that `locline`
was scenery-only based on the wiki's docs-page description ("crossbows,
gem rocks, spinning wheels, etc."). Wrong. The bucket contains NPC
rows too: Goblin has 20 entries with hundreds of coordinates, Man has
28, Guard 9, etc. Each row exposes `coordinates` (array of `"x:N,y:M"`
strings), `plane`, `mapid`, `members`, `leagueregion`. Keyed by
`page_name`, so it needs a name→cache-ID join to be useful. Role
reduced from "primary" to "cross-check against NPCList_OSRS."

**`osrsreboxed-db` confirmed no coordinates.** Its monster JSON covers
combat stats, aggression, slayer data, immunities — not spawns. That's
consistent with client cache not storing server-side spawn info.

**Updated NPC spawn pipeline plan:**
1. Primary load: `NPCList_OSRS.json` → per-region bucketing → `data/spawns/{rx}_{ry}.nspn`.
2. Cross-check: `locline` bucket rows → resolve `page_name` → cache ID
   → diff against NPCList. Coverage diff goes into a build-time report.
3. 2011Scape `.plugin.kts` drops to tertiary (only consulted for
   NPC-name-to-ID hints when other resolvers fail).

Docs updated: `work.md` Phase 1 follow-ups, `database.md` coverage
matrix + bucket catalog, memory `reference_osrs_wiki_bucket.md`.

**Follow-ups completed (same day, before Phase 2):**

1. **NPC spawn pipeline** (`tools/export_spawns.py`) — loads
   `data_osrs/NPCList_OSRS.json` (24,110 spawns, cache-ID keyed), emits
   `data/spawns/world.npc-spawns.bin` (362 KB) + `data/regions/varrock.npc-spawns.bin`
   (235 Varrock spawns, up from 193 via 2011Scape). Cross-checks against
   `locline` bucket; report at `tools/reports/spawn_coverage.txt`.
   620 names match both sources; "wiki-only" column is mostly
   scenery/user-sandbox pages, not missing NPCs.

2. **Shallow-bucket re-scrape** (`tools/scrape_shallow.py`) — ran 7
   buckets with full fields: `infobox_scenery` (8 fields, 13,175 rows),
   `infobox_shop` (6 fields, 503), `infobox_spell` (6 fields),
   `transcript` (3 fields, 922), `npc_id` (3 fields, 10,194),
   `item_id` (2 fields, 17,100), `object_id` (3 fields, 13,340). Cache
   keys differ from Phase 1 `page_name`-only scrapes, so old files
   remain but aren't consulted by downstream tools.

3. **Drops exporter** (`tools/export_drops.py`) — parses all 38,638
   `dropsline` rows. Rarity parser handles `"N/M"`, `"N/M,MMM"` (with
   commas), `"Always"`, `"Common"/"Uncommon"/"Rare"/"Very rare"`
   keywords, and `"Varies"` → None. Quantity parser handles `"N"`,
   `"N–M"` (em-dash), `"N-M"` (hyphen), `"Varies"` → None. Fragment
   stripping (`"Dark wizard#Low level"` → `"dark wizard"`) joins to
   `infobox_monster` and `infobox_item` via `page_name`/`name`. Output:
   `data/defs/drops.bin` (247 KB, DROP magic), 858 NPCs with drop
   tables, 20,184 drop entries total. Report at
   `tools/reports/drops.txt`. Remaining 12,434 unresolved rows are
   mostly non-NPC drop sources (trees, rocks, chests, containers,
   hunter catches) that need a separate skill-drops data shape; that's
   Phase 2 work.

**Totals end of Phase 1:**
- 147k+ rows of wiki data cached in 40+ MB on disk
- 24k NPC spawns emitted (world + Varrock binaries)
- 20k drop entries emitted across 858 NPCs
- Gaps closed: #2 NPC spawn positions, #3 NPC drop tables, #15 music,
  #24 quests, #27 varbits. Partial: #4 NPC aggression, #19 skill
  actions, #25 shops (raw bucket rows, not yet processed into binaries).

---

## 2026-04-19 — Phase 2 kickoff: binary processors

Restructured `work.md` phase numbering so binary emission becomes its
own phase: the old Phase 2 (wiki page scrape) is now Phase 3, old
Phase 3 (OSRS-only encounter reconstruction) is Phase 4, old Phase 4
(hand-curate stragglers) is Phase 5. The new Phase 2 covers
processors that turn Phase 1 Bucket caches into `data/defs/*.bin`.

**Phase 2 scope — one processor per binary:**
1. `export_varbits.py` → `varbits.bin` (2,897 varbit name↔index)
2. `export_music.py` → `music.bin` (1,187 tracks + region mapping)
3. `export_quests.py` → `quests.bin` (225 quest metadata records)
4. `export_shops.py` → `shops.bin` (503 shops + 6,253 stock lines)
5. `export_recipes.py` → `recipes.bin` (7,182 skill-action records)
6. `export_spells.py` → `spells.bin` + `teleports.bin` (201 spells +
   tablet recipes from `recipe`)
7. `export_skill_drops.py` → `skill_drops.bin` (~12k non-NPC drop
   rows from trees/rocks/chests/hunter targets)
8. `xvalidate.py` → `tools/reports/xvalidate.txt` (osrsreboxed-db vs
   `infobox_monster` / `infobox_bonuses` stat diff)

Each processor is tiny (~100 LOC), consumes the already-cached Bucket
JSON, and emits a binary with a 4-byte magic + version + count header.

---

## 2026-04-19 — Phase 2 complete

All 8 Phase 2 processors written and producing binaries. Schemas
documented in each processor's module docstring; item + NPC name
resolution reused across processors via the `infobox_item`/
`infobox_monster` bucket caches.

| Binary | Magic | Rows | Size | Source |
|---|---|---|---|---|
| `varbits.bin` | `VBIT` | 2,871 | 95 KB | `varbit` bucket |
| `music.bin` | `MUSC` | 858 | 34 KB | `music` bucket |
| `quests.bin` | `QEST` | 215 | 5.4 KB | `quest` bucket |
| `shops.bin` | `SHOP` | 597 | 132 KB | `infobox_shop` + `storeline` |
| `recipes.bin` | `RCIP` | 3,413 | 176 KB | `recipe` bucket |
| `spells.bin` | `SPEL` | 201 | 7.5 KB | `infobox_spell` |
| `teleports.bin` | `TELE` | 58 | 2.3 KB | subset of `infobox_spell` |
| `skill_drops.bin` | `SDRP` | 1,142 sources, 12,434 entries | 170 KB | residual `dropsline` |

Notes from the processor runs:

- **Storeline** needed a mid-phase re-scrape — Phase 1 captured only
  `page_name` for it. Now carries `sold_item`, `store_buy_price`,
  `store_sell_price`, `store_stock`, multipliers, `restock_time`.
- **Infobox_spell** also needed a re-scrape — the shallow-bucket pass
  tried to include a `type` field that doesn't exist (wiki doc-page
  hallucination). Correct fields: `page_name, image, spellbook,
  uses_material, is_members_only, json`. Rune costs parsed from the
  `json.cost` wikitext via `<sup>N</sup>[[File:X rune.png]]` regex.
- **Quests**: 212/215 have an official difficulty, 130/215 carry
  parseable skill reqs (regex over `data-skill="X" data-level="N"`
  template output). Step-by-step walkthroughs are intentionally
  deferred to Phase 3 (page scrape).
- **Recipes**: kept only rows with `source_template == "recipe"` and
  at least one skill req — drops 1,912 bare "skill info" events (e.g.
  quest XP) and 1,857 rows without skill data. Result: 3,413 real
  recipes with level/XP/inputs/output/facility/ticks.
- **Skill-drops**: the 12,434 dropsline rows whose "Dropped from"
  wasn't in the monster name map. Top sources include clue reward
  caskets (all 6 tiers), colosseum chests, minigame chests, and
  location-suffixed NPCs ("Skeleton (Tarn's Lair)", "Dagannoth
  (Waterbirth Island)", "Cyclops (God Wars Dungeon)") that
  infobox_monster keys by base name without the location. A Phase 3
  pass could merge location variants back into drops.bin.
- **Cross-validation**: 236 monster stat mismatches on 1,175 NPCs
  checked; 458 equipment-bonus mismatches on 3,898 items. Mismatches
  land in `tools/reports/xvalidate_monsters.txt` and
  `tools/reports/xvalidate_bonuses.txt` for Phase 5 polish.
  `max_hit` is the most common monster-stat diff — wiki often
  includes special/breath attacks that osrsreboxed-db lists as base
  melee only (Mithril dragon 28 vs 50, Green dragon 8 vs 50).

**Totals end of Phase 2:**
- Everything in `/data/defs/` except the cache-derived binaries
  (npc_defs.bin, items.bin, objects, terrain, etc.) is now produced
  end-to-end from the Bucket scrape + osrsreboxed cross-ref.
- Next phase (wiki page scrape) tackles unstructured content — quest
  walkthroughs, dialogue trees, boss mechanics, clue step solutions.

---

## 2026-04-20 — Phase 3 kickoff: per-page wiki scraping

Extracted the shared HTTP machinery from `wiki_bucket.py` into a new
`tools/wiki_client.py` (base class: pacing / maxlag / UA / backoff /
probe). `BucketClient` now subclasses it. New `tools/wiki_pages.py`
subclasses it too — page-level scraper for unstructured wiki content.

`mwparserfromhell` (v0.7.2) installed via `pip install
--break-system-packages`. Handles template parsing.

**`PageClient` capabilities:**
- `wikitext(title)` — action=parse&prop=wikitext, cached under
  `tools/wiki_cache/pages/{sanitized}.json`. Per-title file, not
  per-query-hash, since titles are globally unique on wiki.
- `templates(title)` — list of `mwparserfromhell` Template objects.
- `infobox(title, name)` — first `{{name}}` template's params as a
  dict of `{param: stripped_value}`.
- `all_infoboxes(title, name)` — every matching template (multi-phase
  bosses have multiple `Infobox Monster` entries).
- `category_members(category, namespace=0)` — MediaWiki category
  enumeration with continuation; returns title list.

**Smoke test:** extracted TzTok-Jad's Infobox Monster — all 40+ params
cleanly pulled. Unstructured fields like `max hit = "97 ([[Melee]]),
97 ([[Ranged]]), 95 ([[Magic]])"` carry richer info than the bucket's
flat `max_hit` field.

**Phase 3 first target: bosses.** `tools/scrape_bosses.py` enumerates
`Category:Bosses` (170 titles) and bulk-fetches every page's wikitext
to disk, then summarises template frequency across the whole set in
`tools/reports/bosses_templates.txt`. Output informs what's worth
extracting next (per-category TOML emitters come after).

**Phase 3 continuation: `/Strategies` subpages.** `tools/scrape_strategies.py`
follows `{{HasStrategy}}` templates + `{Boss}/Strategies` convention
to pull every strategy subpage. 58/160 cached (102 missing or redirect
to parent — many bosses keep mechanics on the main page rather than a
subpage). Reports:
- `tools/reports/strategies_sections.txt` — top sections by frequency
- `tools/reports/strategies_templates.txt` — top templates
- `tools/reports/strategies_missing.txt` — the 102 absent titles

**Key findings from boss scrape reports:**
- Boss main pages' structured data (Infobox Monster, DropsLine, LocLine,
  CombatAchievements) is already covered by Phase 1 Bucket scrapes.
- `/Strategies` subpages focus on **player-side** recommendations
  (Inventory: 84, Equipment: 58, Transportation: 39, Suggested skills:
  31, Requirements: 27). Only ~22 pages have a "Fight overview"
  section and 12 have explicit "Mechanics" sections — mostly prose,
  no standardized phase/rotation templates.
- Top extraction-worthy templates: `{{Recommended equipment}}` (174),
  `{{Inventory}}` (171), `{{Rune pouch}}` (101), `{{Cheap food}}`
  (303), `{{Cheap prayer}}` (81) — all player-loadout data.
- Boss mechanics prose is HIGH-EFFORT / LOW-STRUCTURE; not worth a
  sophisticated extractor today. Cached wikitext is preserved for
  future use.

**New typed exception:** `wiki_client.PageMissing` raised when
MediaWiki returns `missingtitle`. Scrapers catch this to skip pages
that don't exist (common for `/Strategies` subpages).

**Infrastructure refactor:** moved shared HTTP machinery from
`wiki_bucket.py` into new `tools/wiki_client.py` (base class).
`BucketClient` and `PageClient` both subclass, sharing one pacing
clock when used in the same process.

---

## 2026-04-20 — Incorrect scope cut (reverted same day)

I misread a user directive about boss/NPC page extraction as a
project-wide scope narrowing. Deleted 7 binaries + 7 scripts
(`music.bin`, `quests.bin`, `shops.bin`, `recipes.bin`, `spells.bin`,
`teleports.bin`, `skill_drops.bin` + their `export_*.py` +
`scrape_strategies.py`) along with related reports.

The actual intent was narrower: **when extracting from a boss or NPC
wiki page, only pull fight/spawn/drop-relevant data** — don't pull
recommended-equipment / inventory / transportation / suggested-skills
data from those pages. Other systems (music, shops, quests, skilling,
spells) remain fully in scope with their own scrape pipelines.

**Reversal actions taken the same session:**
- All 7 export scripts + `scrape_strategies.py` rewritten from source
  (they weren't in git, had to reconstruct from the binary schemas
  documented in the previous Phase 2 completion entry).
- All 7 binaries re-emitted; byte sizes match pre-delete exactly
  (same input data → deterministic output).
- `strategies_sections.txt`, `strategies_templates.txt`,
  `strategies_missing.txt`, `skill_drops.txt` regenerated.
- `ignore.md` §15 rewritten to reflect the correct, narrower rule
  (governs **per-boss page extraction only**, not project scope).
- `work.md`, `database.md` scope narrowing reverted.

**Lesson / rule going forward:**
- When a directive is ambiguous between "narrow interpretation
  (apply here)" and "broad interpretation (project-wide)",
  default to the narrow interpretation AND confirm before taking
  destructive action.
- Inventory every scrape or deletion candidate and wait for
  explicit approval on deletions. This pattern is now standing.
- Don't delete scripts just because their output binaries are cut —
  source code is cheap to keep, expensive to reconstruct.

---

## 2026-04-20 (cont.) — Phase 3 mechanics + item-specials extractors

**Boss mechanics extractor** (`tools/extract_mechanics.py`):
- Reads cached boss wikitexts under `tools/wiki_cache/pages/`
  (170 main pages + 57 /Strategies subpages).
- Whitelist-based section filter keeps only fight-relevant headers
  (contains one of: "mechanic", "attack", "ability", "fight",
  "phase", "form", "awakened", "weakness", "dragonfire", "prayer
  info", "overview", "special"). Explicit out-list catches edge
  cases like "Used in recommended equipment".
- Filtered to `Category:Bosses` members to avoid processing
  weapon pages cached during smoke tests.
- NPC-id resolution via `infobox_monster.page_name` → `id[]`.
- Output: `data/curated/mechanics/{slug}.toml` with `name`,
  `source_pages`, `npc_ids`, and `[sections]` map.
- Results: **96 boss TOMLs emitted**, 74 had no fight-relevant
  sections (mechanics embedded in lead prose without dedicated
  section headers), 5 multi-form bosses couldn't resolve a single
  NPC id (Grotesque Guardians, Moons of Peril, Royal Titans,
  Tempoross, Wintertodt — legit, need manual mapping).
- 257 sections kept, 2,134 cut as out-of-scope.
- Report: `tools/reports/mechanics_extract.txt`.

**Item special-attack scraper** (`tools/scrape_item_specials.py`):
- Enumerates `Category:Weapons with Special attacks` (122 pages).
- Fetches each weapon's wikitext (cached), extracts level-2
  sections titled "Special attack*".
- Item-ID resolution via `infobox_item` bucket (lowest ID wins
  when a name has multiple variants).
- Output: `data/curated/specials/{slug}.toml` with `name`,
  `item_ids`, and `[special]` map.
- Results: **120 TOMLs emitted**, 2 pages missing section (3rd age
  pickaxe, Infernal pickaxe — both are skilling-boost items, not
  combat specials), 20 unresolved item_ids (all Last Man
  Standing / Deadman Mode variants — PvP minigame-specific,
  out-of-scope per `ignore.md`).
- Sample: Dragon dagger's Puncture extracts with full mechanic
  prose (2 hits, +15%/+15% acc/dmg, 25% spec energy, 50 max per
  hit → 100 combined).
- Report: `tools/reports/item_specials.txt`.

**Remaining Phase 3 work** (in scope order):
1. Merge location-variant drops (~4-5k `skill_drops.bin` entries
   like "Skeleton (Tarn's Lair)") back into `drops.bin` under
   their base NPC.
2. Reconcile 236 xvalidate_monsters mismatches — wiki is
   authoritative on `max_hit` (includes breath/special).
3. Add dynamic/instanced spawn flagging to NSPN — `locline.mapid`
   distinguishes main-world (0) from instances (boss arenas).
4. Scrape Rare Drop Table + Gem Drop Table pages → resolve
   `{{RDT}}`/`{{GDT}}` references currently counted as opaque
   `rare_table_weight` in `drops.bin`.
5. Per-slayer-master task-weight pages — narrow scope, only
   NPC→master mapping for assignment logic.

**Intentionally deferred** (outside current scope):
- Dialogue transcripts (`Transcript:{NPC}` pages) — for quest impl.
- Quest walkthrough step progression — for quest state machine.

---

## 2026-04-20 (cont.) — Phase 3 remaining 5 items complete

**1. Location-variant drop merge.** Updated `export_drops.py` +
`export_skill_drops.py` with a parenthesized-suffix fallback resolver:
if `"Skeleton (Tarn's Lair)"` misses the exact name lookup, retry
against the base name (`"Skeleton"`). Canonical parenthesized NPCs
(e.g. `"Mummy (Ancient Pyramid)"`) hit on exact match first so they
keep their own drop tables. Results:
- `drops.bin`: 858 NPCs → **909 NPCs** (+51), 20,184 → **24,144
  entries** (+20%), unresolved items 1,246 → **29**.
- `skill_drops.bin`: 1,142 sources → **961 sources**, 12,434 →
  **9,202 entries**. Top sources now all legit non-NPC (reward
  caskets, minigame chests).

**2. xvalidate_monsters reconciliation.** New
`WikiMonsters` reader in `database_sources.py` indexes
`infobox_monster` bucket rows by cache NPC ID. `export_npcs.py`'s
`merge_osrsreboxed_fields()` accepts a `wiki` overlay: when wiki
has a `max_hit` value that differs from osrsreboxed, wiki wins
(wiki parses the `"97 (Melee), 97 (Ranged), 95 (Magic)"` strings
and takes max). Same for `poison_immune` / `venom_immune`. Since
running the full NPC export requires the raw cache (not extracted
locally), added `tools/patch_npc_defs_wiki.py` as an in-place
patcher that rewrites the NDEF v2 trailer for the existing
`npc_defs.bin`. Results for Varrock (79 NPCs): **4 max_hit patches**
applied. Patcher is re-runnable whenever wiki data updates.

**3. NSPN v2 with instance flag.** Bumped `NSPN_VERSION` to 2 and
appended a trailing `flags u8` per spawn record (bit0 =
`NSPN_FLAG_INSTANCE`). `export_spawns.py` populates the flag by
cross-referencing `locline.mapid`: if every locline entry for an
NPC has a non-zero mapid, the NPC is instance-only (boss arenas,
raid rooms). `rc-core/npc.c` updated to read the flag and skip
instance-only spawns during static world-spawn loading — runtime
code spawns those on instance entry. Results: **3,314 of 24,110
spawns (14%)** flagged as instance-only across world.

**4. Rare Drop Table + Gem Drop Table.** `tools/scrape_rdt.py`
fetches both wiki pages + "Mega-rare drop table" (redirect —
content lives inside RDT), parses `{{DropsLine}}` templates via
`mwparserfromhell`, resolves item names to cache IDs. Emits:
- `data/defs/rdt.bin` — **33 entries**, magic `'RDT_'` (408 B)
- `data/defs/gdt.bin` — **15 entries**, magic `'GDT_'` (192 B)
- `data/defs/mrdt.bin` — 0 entries placeholder (MRDT contents
  merged into RDT).
Same per-entry format as `drops.bin` (u32 item_id, u16 qmin/qmax,
u32 rarity_inv). The `rare_drop_table` counter in `drops.bin`
now points to real data.

**5. Per-slayer-master NPC assignments.** `tools/scrape_slayer.py`
enumerates 12 masters (Turael, Spria, Mazchna, Vannaka, Chaeldar,
Nieve, Steve, Konar quo Maten, Duradel, Krystilia, Achtryn, Aya).
Extracts the `==Tasks==` section from each master page; follows
`{{:Master/Slayer assignments}}` transclusions when the section
is just a subpage include (Turael, Mazchna, Nieve, Duradel).
Parses `|-` delimited rows for the first `[[Monster]]` link + the
`{{+=|weight|N|echo=2}}` weight template. Emits:
- `data/defs/slayer.bin` — magic `'SLAY'`, **429 task entries**
  across 12 masters (Turael 24, Spria 25, Mazchna 30, Vannaka 45,
  Chaeldar 40, Nieve 46, Steve 46, Konar 39, Duradel 43,
  Krystilia 37, Achtryn 30, Aya 24) — 6.4 KB.

**Final Phase 3 binary inventory:**
- `npc_defs.bin` (Varrock 79, wiki-overlaid max_hit)
- `items.bin` (13,020)
- `drops.bin` (909 NPCs, 24,144 entries)
- `skill_drops.bin` (961 non-NPC sources, 9,202 entries)
- `varbits.bin` (2,871)
- `music.bin` (858)
- `quests.bin` (215)
- `shops.bin` (597)
- `recipes.bin` (3,413)
- `spells.bin` (201) + `teleports.bin` (58)
- `rdt.bin` (33) + `gdt.bin` (15) + `mrdt.bin` (placeholder)
- `slayer.bin` (12 masters, 429 tasks)
- `world.npc-spawns.bin` (24,110, NSPN v2 with 3,314 instance flags)
- `varrock.npc-spawns.bin` (235)
- `data/curated/mechanics/` — 96 per-boss TOMLs
- `data/curated/specials/` — 120 per-weapon TOMLs

---

## 2026-04-20 (cont.) — Phase 6 plan added + Phase 4 kickoff

**Phase 6 (new)** — documented in `work.md`. Covers quest + dialogue
data pipeline that Phase 3's narrow scope deferred:
- `tools/extract_dialogue.py` → per-NPC dialogue state machines
  from `Transcript:{NPC}` wikitexts → `data/curated/dialogue/{npc_id}.toml`.
- `tools/extract_quest_steps.py` → per-quest walkthrough section
  extraction into `data/curated/quests/{slug}/steps.toml`.
- `tools/export_dialogue.py` → `data/defs/dialogue.bin`.
- Sequenced after Phase 4 (encounter reconstruction) so we only
  scrape dialogue/walkthroughs for content we actually wire up.

**Phase 4 kickoff.** Encounter TOML schema finalized; two pilots
authored:

- `database_template.md` §A5 rewritten with the full encounter
  schema — stats override, per-style attacks with forced-hit /
  prayer-drain / warning-ticks fields, ordered phases with HP
  thresholds (% or hard zero) and style weights, named mechanics
  bound to `rc-core/encounter.c` primitives.
- **Pilot 1 — Scurrius** (`data/curated/encounters/scurrius.toml`):
  3 phases (melee_focus → heal @ 80% → enraged @ 30%, with revert
  back to heal if healed above 30%), 3 attack styles, 3 named
  mechanics (Falling Bricks telegraphed AoE, Minions spawn, Food
  Heal object-based).
- **Pilot 2 — Kalphite Queen**
  (`data/curated/encounters/kalphite_queen.toml`): validates
  schema on hard HP=0 phase transition (vs %-based), 20-tick
  untargetable transition animation, partial-immunity overhead
  prayers, forced-hit ranged/magic, prayer-drain-on-damage,
  magic-chain-to-nearest-player (solo → no-op), stat-drain
  persistence across phases, 20-min enrage revert loop.

**Primitive registry (to be implemented in `rc-core/encounter.c`):**
the two pilots reference 10 distinct primitives. Implementation
blocked on TODO #5 (Combat) maturing — Phase 4 will land primitives
incrementally as each encounter gets wired to a functional combat
engine. Pilots serve as reference data for the eventual combat code.

---

## 2026-04-20 (cont.) — Phase 4 data complete (32 encounter TOMLs)

All MVP encounter data authored. Schema stabilized; primitive
registry spec at `data/curated/encounters/_primitives.md` is the
complete reference for rc-core encounter engine implementation
when that work begins.

**Encounters by batch:**
- Pilots (2): Scurrius, Kalphite Queen
- Batch 1 (5): Obor, Bryophyta, Scorpia, Giant Mole, Chaos Elemental
- Batch 2 (5): Corporeal Beast, Cerberus, Kraken, King Black Dragon,
  Dagannoth Kings
- Batch 3 (5): Zulrah, Vorkath, Alchemical Hydra, Nightmare,
  General Graardor
- Batch 4 (5): Commander Zilyana, K'ril Tsutsaroth, Kree'arra,
  Abyssal Sire, Phantom Muspah
- Batch 5 (4): Vardorvis, Leviathan, Whisperer, Duke Sucellus
  (with `[awakened_override]` overlays)
- Batch 6 (6): Gauntlet + Corrupted, Colosseum, Inferno,
  Chambers of Xeric, Theatre of Blood, Tombs of Amascut

**Schema final state:**
- ~60 generic mechanic primitives documented
- 12+ encounter-specific script primitives
- Complete attack-level field catalog (styles, on-hit effects,
  prayer interactions, damage modifiers, accuracy rolls, AoE
  shapes, combos)
- Complete phase-level field catalog (HP-threshold + hard-HP +
  event-based entry, style weights with adjacency variants,
  overhead-prayer partial immunity, shield mechanics, attack
  cycles)
- Encounter-level patterns: `[[bosses]]` for multi-boss fights,
  `[[rooms]]` for raids, `[[waves]]` for wave-PvM,
  `[awakened_override]` / `[corrupted_override]` for variants,
  `[entry_requirement]` for gated rooms
- `run_level_modifier_registry` primitive handles Colosseum
  modifier stack + ToA Invocation system uniformly

**Phase 4 remaining (all blocked on code work):**
- `rc-core/encounter.c` subsystem implementation (blocked on TODO #2
  rc-core refactor + TODO #5 combat engine)
- ~60 primitive C functions
- Per-encounter automated regression tests
- Manual viewer validation

Phase 4 data deliverable is complete. Phase 5 (per-item specials)
is already underway via `data/curated/specials/` (120 items, from
earlier Phase 3 work). Phase 6 (dialogue + quest walkthroughs) is
planned but not started.

---

## 2026-04-20 (cont.) — Phase 4 batch 7 (MVP complete) + Phase 6

**Batch 7 — 18 more encounter TOMLs:** wilderness bosses
(Callisto/Artio, Vet'ion/Calvar'ion, Venenatis/Spindel,
Chaos Fanatic / Crazy Archaeologist / Deranged Archaeologist),
Nex, Hueycoatl, Amoxliatl, Royal Titans, Yama, Thermonuclear Smoke
Devil, Sea Troll Queen, Sarachnis, Hespori, Skotizo, The Mimic,
Wintertodt, Tempoross, Zalcano. **Total MVP roster: 50 encounter
TOMLs.**

New primitives / fields added by batch 7:
- `surviving_boss_enrage` (Royal Titans)
- `heal_altars_player_must_disable` (Skotizo)
- `interactive_environment_object` (Hueycoatl stone pillar pin)
- `crafting_resource_loop` (Zalcano ore cycle)
- `interactive_resource_nodes` + `interactive_object_with_feed`
  (Tempoross)
- `periodic_water_rise` (Tempoross)
- `periodic_object_damage_event` (Wintertodt brazier explosions)
- `spawn_ally_npcs` (Wintertodt pyromancers — friendly)
- `periodic_tile_damage_all_players` (Wintertodt snow, Tempoross storm)
- `periodic_telegraphed_snowballs` (Wintertodt cold attacks)
- `encounter_type = "skilling_boss"` flag (Wintertodt, Tempoross,
  Zalcano)
- `one_shot_at_fight_start` (Hespori thorny vines)
- `[variant_override]` block for wilderness solo variants
  (Callisto → Artio, Vet'ion → Calvar'ion, Venenatis → Spindel)

**Phase 6 — dialogue + quest walkthroughs:**

`tools/scrape_transcripts.py` enumerated `Transcript:` page list
from the `transcript` bucket and fetched all 922 wikitexts into
`tools/wiki_cache/pages/` (13.6 min, 0 missing).

`tools/extract_dialogue.py` parses the nested bullet + template
structure (`{{topt}}`, `{{tselect}}`, `{{tcond}}`, `{{tbox}}`,
`{{tact}}`, `'''Speaker:''' text`) into dialogue state-machine
nodes. Each node carries id / parent / depth / kind / speaker /
text / children / is_terminal. Results:
- **380 dialogue TOMLs emitted** under `data/curated/dialogue/`
- **155,020 total dialogue nodes** (avg 408 nodes per transcript)
- 542 transcripts skipped — those are narrative/livestream
  transcripts (Postbag from the Hedge, event recaps) without
  conversational bullet structure. Legitimate skip.

`tools/export_dialogue.py` → `data/defs/dialogue.bin` (**10.3 MB**,
`DLGX` magic). Per-transcript header (slug + NPC list) then per-node
records with pointer-to-children indexing. Loadable by rc-core
dialogue subsystem for runtime state walks.

`tools/extract_quest_steps.py` — fetched each quest's main wiki
page, extracted `==Walkthrough==` section, split into level-3
sub-sections with referenced items/NPCs/locations extracted from
`[[links]]`. Emits `data/curated/quests/{slug}/steps.toml` per
quest. Results:
- **199 quest step TOMLs emitted** (of 215 quest titles)
- **1,081 total walkthrough steps** extracted (avg 5.4 per quest)
- 16 quests without a top-level `==Walkthrough==` section —
  most are multi-chapter epics (DSII, MM2, DT2-FE, Grim Tales,
  Observatory Quest, etc.) that structure their walkthrough with
  nested `===Chapter===` headers instead. Polishable later by
  extending the extractor to recognize alt-heading patterns.

**End-of-Phase-6 state:**
- 380 dialogue TOMLs + `dialogue.bin` (ready for NPC interaction TODO #8)
- 199 per-quest step TOMLs under `data/curated/quests/*/steps.toml`
  (ready as reference for quest state-machine authoring per TODO #9)
- `transcript` page cache preserved (922 files) — raw source for
  future re-extraction if schema improves

**All database phases now complete.** Next up per the critical-path
list: TODO #2 rc-core refactor → TODO #5 combat engine → Phase 4
engine + primitives → per-encounter regression + viewer validation.

---

## 2026-04-20 (cont.) — TODO #2 pass 1: rc-core modularity

First pass of the rc-core refactor per the principles in
`rc-core/README.md`. Non-disruptive — viewer + all existing tests
still build and pass unchanged.

**New headers / modules:**
- `rc-core/config.h` + `config.c` — `RcWorldConfig` with subsystem
  bitmask + four presets:
  - `rc_preset_full_game()` — all 12 subsystems on.
  - `rc_preset_combat_only()` — combat + prayer + equipment +
    inventory + consumables + encounter (Colosseum / Inferno RL).
  - `rc_preset_skilling_only()` — skills + inventory + equipment.
  - `rc_preset_base_only()` — zero subsystems (locomotion bench).
- `rc-core/events.h` + `events.c` — episodic event bus.
  13 event types (NPC death, drop, phase transition, dialogue,
  quest stage, prayer toggle, etc.), 8-handler slots per event,
  re-entry guard via dev-assert.
- `rc-core/handles.h` — `RcNpcId`, `RcItemSlot`, `RcGroundItemId`
  typedefs + sentinels. Forward-enables README §5 (handles, not
  pointers, across subsystem boundaries) for future migration.

**`types.h` changes:**
- `RcWorld` now has a named struct tag (`struct RcWorld`) so
  subsystem headers can forward-declare it without pulling in
  types.h — breaks circular include pressure.
- Inline additions to `RcWorld`: `uint32_t enabled` (subsystem
  bitmask) + `RcEventBus events`.
- All existing fields (player, npcs, map, ground_items, etc.)
  preserved at the same offsets so viewer code keeps working
  unchanged.

**`world.c` changes:**
- New entrypoint `rc_world_create_config(cfg)` — accepts
  `RcWorldConfig*`, applies the subsystem bitmask, initialises
  event bus.
- Legacy `rc_world_create(seed)` preserved as a thin wrapper that
  calls `rc_preset_full_game()` + sets the seed. No viewer /
  test breakage.

**`tick.c` changes:**
- Tick dispatcher now gates per-subsystem phases on the bitmask:
  combat tick only runs if `RC_SUB_COMBAT`, prayer drain only if
  `RC_SUB_PRAYER`, ground items only if `RC_SUB_LOOT`, etc.
- Base phases (NPC position, route planning, input, tick counter)
  always run — no conditional.
- Dispatch cost: cache-resident `on & flag` per subsystem per tick.
  Negligible even at 10M tps.

**README-compliance audit (grep-based, passes):**
- No `malloc`/`calloc`/`free`/`realloc` on the tick path (only in
  `world.c:rc_world_create_config` startup).
- No shared mutable globals without `_Thread_local` (pathfinding
  scratch arrays are `static _Thread_local`; everything else is
  stateless or on the `RcWorld` struct).
- No `printf`/`fprintf`/`puts` on the tick path (only in asset
  loaders during startup).

**New CMake target `test_base_only`:**
Proves modularity works end-to-end: creates a world with 0
subsystems enabled via `rc_preset_base_only()`, runs 100 ticks, and
asserts determinism across two seeded worlds. Also validates that
the preset bitmasks have the correct subsystems enabled /
disabled (`test_combat_sim` style preset-check folded in here).
**All 4 existing tests + the new one pass.**

**What's deferred to TODO #2 pass 2** (when subsystems actually
consume config):
- Per-subsystem binary loaders in `rc_world_create_config`
  (`if (cfg->subsystems & RC_SUB_LOOT) rc_loot_load(...)` etc.).
  Currently stubbed with TODO comment — safe because no subsystem
  depends on config-loaded data yet (combat is a stub, loot isn't
  wired, etc.).
- Hot/cold NPC split into parallel arrays (README §6) — not needed
  until we're profiling at 10M tps.
- Arena layout with inline subsystem state structs (README §4) —
  currently `RcWorld` has the player's combat/prayer/inventory
  fields inline, not grouped into sub-structs. Grouping is cosmetic
  until subsystem state genuinely differs from player state.
- Moving subsystem-specific types out of `types.h` (README §11).
  Currently `RcPendingHit`, `RcInvSlot`, `RcSkills`, etc. all
  live in `types.h`. Moving them requires careful audit of who
  includes what — deferred until the affected subsystems have
  real code.

**Unblocks:** TODO #5 combat engine + Phase 4 encounter engine can
land their own subsystem state / event subscriptions on top of
this foundation without retrofitting modularity later.

---

## 2026-04-20 (cont.) — TODO #5 pass 1: combat engine functional

`combat.c` unstubbed with real OSRS DPS math.

**Implemented:**
- Effective-level helpers (`eff_attack_melee`, `eff_strength_melee`,
  `eff_defence`, `eff_ranged_atk`, `eff_ranged_str`, `eff_magic_atk`)
  using `(base + stance) × (100 + prayer_bonus_pct) / 100 + 8`.
  Stance hardcoded to Accurate (+3 atk) pending TODO #3 UI work.
- Attack/defence roll: `eff_level × (bonus + 64)`. Equipment bonus
  indices centralised (`EQ_STAB_ATK`, `EQ_STR`, etc. — 14 slots
  matching the osrsreboxed-db layout).
- Player-vs-NPC calc (`rc_calc_melee`, `rc_calc_ranged`,
  `rc_calc_magic`) + NPC-vs-player calc (`rc_calc_npc_attack`).
- Pending-hit queue with prayer snapshot at queue time (per FC
  lesson: prayer must be active at queue tick to block, not
  impact tick).
- Protection prayer: 100% block for player defender, 50% reduction
  for NPC defender (boss overhead prayers).
- Auto-attack tick loop — `rc_combat_tick_player` /
  `rc_combat_tick_npc` wired into `tick.c` phase 3.5 + 4, gated on
  `RC_SUB_COMBAT`.
- Hit resolution in tick dispatcher: damage applied to
  `player.current_hp` / `npc.current_hp`; NPC marked `is_dead` at
  0 hp.

**Tests:**
- `test_combat.c` rewritten — unit tests for hit-chance, queue
  with delay, protection prayer full-block vs 50% reduction,
  wrong-prayer-no-block, queue cap.
- **New** `test_combat_e2e.c` — end-to-end: spawn 50-hp dummy NPC,
  attack, verify kill + determinism. Seed 42 → 107 ticks, seed 99
  → 13 ticks (proves RNG consumption).
- All 6 tests pass (test_base_only, test_collision, test_combat,
  test_combat_e2e, test_determinism, test_pathfinding).

**Deferred to pass 2** (land with caller):
- Per-weapon attack speed from items.bin
- Stance selection (Aggressive/Defensive/Controlled)
- Ranged/magic prayer boosts (Eagle Eye, Rigour, Mystic Might,
  Augury)
- NPC-side overhead prayers (KQ partial-immunity, Nightmare shield)
- Arrow/rune consumption
- Per-weapon attack range from items.bin
- Spec attack + spec energy (Phase 5 consumes
  `data/curated/specials/`)
- Status effects (poison/venom/freeze — bound to encounter
  primitives in Phase 4 engine)

**Unblocks:** Phase 4 encounter engine — primitive implementations
now have working combat math + pending-hit queue to wire onto.

---

## 2026-04-20 (cont.) — Phase 4 engine pass 1 landed

`rc-core/encounter.c` + `rc-core/encounter.h` — encounter subsystem
scaffolding with event-bus integration.

**New types:**
- `RcEncounterSpec` — per-encounter spec (slug, npc_ids list,
  attack pool, phase list, mechanic list). Capped at 16 attacks /
  8 phases / 16 mechanics per encounter; 64 registry slots.
- `RcEncounterPhase`, `RcEncounterAttack`, `RcEncounterMechanic`
  — sub-records matching the TOML schema in
  `data/curated/encounters/_primitives.md`.
- `RcActiveEncounter` — per-boss instance (spec index, boss
  handle, current phase, ticks-since-start). Up to 16 concurrent.
- `RcEncounterState` — subsystem state on `RcWorld` (inline per
  README §4): registry + active array + counters.
- `RcEncounterPrimFn` — function-pointer typedef for primitives.
  NULL until pass 2 lands the ~70 C functions.

**Event-bus wiring:**
- `rc_encounter_init` subscribes to `RC_EVT_NPC_SPAWNED` +
  `RC_EVT_NPC_DIED`.
- `rc_npc_spawn` fires `RC_EVT_NPC_SPAWNED` (payload:
  `{npc_id, def_id}`).
- `tick.c:resolve_npc_hits` fires `RC_EVT_NPC_DIED` once on the
  alive→dead transition.
- Matching NPCs create an active encounter; death marks it
  finished.

**Canonical event payloads** moved to `events.h`:
`RcPayloadNpcEvent`, `RcPayloadPlayerDamaged`, `RcPayloadItemEvent`.

**Tick dispatcher:**
- New phase 3.6: `rc_encounter_tick(world)` runs when
  `RC_SUB_ENCOUNTER` enabled. Per active encounter: HP-percent
  phase-transition check + mechanic period countdown. Primitives
  fire when non-NULL; no-op while registry is empty.

**Tests:**
- **New** `test_encounter.c` — 6 assertions:
  registry register/lookup; registered-NPC spawn starts encounter;
  unregistered-NPC spawn is silent; tick advances counters when
  subsystem enabled; disabling `RC_SUB_ENCOUNTER` freezes ticks;
  boss death finishes the encounter.
- All 6 tests pass (test_base_only, test_combat, test_combat_e2e,
  test_encounter, test_determinism, test_pathfinding).

**Deliberately skipped in pass 1** (data-grind work for pass 2):
- TOML → binary compiler (`tools/export_encounters.py`) +
  `data/defs/encounters.bin` loader. Specs currently built
  in-code by callers.
- ~70 primitive C functions per `_primitives.md`. Start with
  Scurrius + KQ.
- Multi-boss encounters (`[[bosses]]` array — DKS, Graardor, KBD
  pairs). Needs group-tracking.
- Raid multi-room progression (CoX, ToB, ToA). Needs room
  transitions.
- Wave progression (Colosseum, Inferno). Needs wave counter +
  per-wave spawn list.

**Unblocks:** all 50 encounter TOMLs have a runtime to host them.
Pass 2 is data-compilation + primitive implementation against the
spec — no more architecture work for simple encounters.

---

## 2026-04-20 (cont.) — Phase 4 tests + viewer validation

**TOML → binary compiler:** `tools/export_encounters.py` compiles
all 50 encounter TOMLs into `data/defs/encounters.bin` ('ENCT'
magic, 7,658 bytes). Captures slug, npc_ids, per-attack
{style, max_hit, warning_ticks}, per-phase
{id, enter_at_hp_pct, hard_hp_trigger}, per-mechanic
{name, primitive_id, period_ticks}. Two TOMLs had multi-line inline
tables (chambers_of_xeric, zalcano) — flattened to single-line for
TOML-spec compliance. 91 primitives mapped to u8 enum ids per
`_primitives.md`.

**C-side loader:** `rc_encounter_load(world, path)` in
`encounter.c`. Reads the binary, fills the registry, skips over
fields the schema doesn't yet consume. Wired into
`rc_world_create_config` — auto-loads when `RC_SUB_ENCOUNTER` is
enabled + `encounters_path` is set. Default preset points at
`data/defs/encounters.bin`.

**Subscription gating:** `rc_encounter_init` (event subscriptions)
now only runs when `RC_SUB_ENCOUNTER` is enabled, keeping
base-only worlds truly event-free per README §7.

**New regression test — `tests/test_encounter_bin.c`:**
Validates the full TOML → binary → registry → NPC-id lookup
pipeline. Key checks:
- All 50 TOMLs compile without skips
- Registry populates with 50 entries
- Every encounter has ≥1 NPC id
- 21 spot-check pairs (npc_id → expected slug) all match
- Aggregate counts: 132 NPC ids, 159 attacks, 92 phases,
  144 mechanics across the set
- Schema caps respected (no corruption in the compiler)

**All 7 tests pass:** test_base_only, test_combat, test_combat_e2e,
test_encounter, test_encounter_bin, test_determinism,
test_pathfinding.

**Viewer validation** — documented in `VIEWER_VALIDATION.md` at
project root. Covers:
- `rc-viewer` startup with encounter subsystem enabled
- Manual encounter-spawn procedure (Varrock doesn't natively
  spawn any registered bosses — need a temp code-edit to test
  end-to-end)
- What's NOT validatable yet: HP bars, AoE telegraphs, phase-
  transition visuals, boss-specific mechanic renders — all
  blocked on primitive implementations (pass 2) + UI work
  (TODO #3).

**Small correction:** `rc-viewer` was accidentally deleted via a
stray `rm -rf rc-viewer` from the wrong cwd; restored from git
(last commit `4c8e72f`). Re-applied the one drift from the
working-set state: moved `RcPlayer.facing_angle` reads to
`ViewerState.player_facing_angle` (viewer-side state, not a rc-core
concern per README §1).

**What Phase 4 engine looks like now:**
- 50 encounter TOMLs → `encounters.bin`
- Runtime registry with NPC-id → spec lookup
- Event-driven lifecycle (spawn → active, death → finished)
- Phase state machine with HP-percent transitions
- Mechanic scheduler (periods tick down; primitive dispatch
  is a NULL no-op until pass 2 fills in the function table)

**Closing TODOs for Phase 4:**
- Pass 2 = implement the 91 primitive C functions. Priority order
  per `_primitives.md`: start with Scurrius (3 primitives) +
  Kalphite Queen (3 primitives), extend from there.
- Pass 3 = multi-boss, raid rooms, wave progression (needs new
  spec shape in the binary).

## 2026-04-20 (cont.) — Phase 4 engine pass 2: primitive registry + 6 pilots

First 6 primitives now have real C implementations and fire through
the encounter dispatcher. Scurrius is the periodic-primitive pilot
(telegraphed_aoe_tile + spawn_npcs actually run per-tick); Kalphite
Queen is the event-driven pilot (drain_prayer_on_hit + chain_magic +
preserve_stat_drains are registered as callable, pending pass-3
event-bus hookup to fire them).

**Binary format extension.** `encounters.bin` now carries a 64-byte
opaque param block per mechanic. Each primitive defines a packed
struct in `rc-core/encounter.h` (layout mirrored in
`tools/export_encounters.py::pack_param_block`). Size grew from
7,658 B → 16,874 B for the same 50 encounters.

**Primitive registry.** `rc-core/encounter_prims.c` holds the 6 pilot
implementations + a lookup table indexed by primitive_id. At load
time, `rc_encounter_load` fills `RcEncounterMechanic.prim` via
`rc_encounter_prim_lookup`; unimplemented primitives resolve to
NULL and the tick loop skips them.

**Pilot primitives (Scurrius):**
- `telegraphed_aoe_tile` — Falling Bricks. Damages the player if
  standing on the boss's tile (primary) or within `extra_random_tiles`
  radius (secondary). Rolls damage uniform [damage_min, damage_max];
  solo-mode swaps in `solo_damage_max` when boss def_id = 7221.
  Uses `rc_queue_hit` with `warning_ticks` delay.
- `spawn_npcs` — Minions. Looks up the target NPC by name against
  `g_npc_defs[]` at call time, spawns `count` instances around the
  boss on a pre-baked ring offset. No-op if name doesn't resolve.
- `heal_at_object` — Food Heal. Raises boss HP by `heal_per_player`
  up to the def's max. Pass-2 stub: runtime firing still routed
  through the (non-existent yet) phase-enter trigger — primitive
  itself works when called directly, per the test.

**Pilot primitives (Kalphite Queen):**
- `drain_prayer_on_hit` — decrements `player.current_prayer_points`
  by `points`. Needs `RC_EVT_PLAYER_DAMAGED` hookup to fire
  automatically on the Barbed Spines attack (pass 3).
- `chain_magic_to_nearest_player` — single-player runtime = no-op.
  Registered so the multi-player swap is a function-table replace,
  no spec change.
- `preserve_stat_drains_across_transition` — stub flag for KQ's
  stat-persistence mechanic. Phase-exit trigger is pass 3.

**Regression test.** `tests/test_encounter_prims.c` loads all 50
encounters, confirms the 6 primitive function pointers are non-NULL
on their respective specs, then calls each primitive directly and
asserts its side effect: pending-hit queued, 6 NPCs spawned, prayer
decremented by 1, boss HP raised toward max. Bypasses the scheduler
so the test exercises the primitive contract, not the tick math.

**All 8 tests green:** test_base_only, test_combat, test_combat_e2e,
test_encounter, test_encounter_bin, test_encounter_prims,
test_determinism, test_pathfinding.

**What's next for Phase 4 pass 2+:**
- Wire `RC_EVT_PLAYER_DAMAGED` through combat.c so event-driven
  primitives (drain_prayer_on_hit + bound-to-attack lookups) fire
  during real fights.
- Wire phase-enter / phase-exit events so `heal_at_object` and
  `preserve_stat_drains` fire on transitions.
- Keep grinding primitives: next batch is the 3 Obor/Bryophyta
  mechanics + the wilderness bosses with overlapping primitives
  (Scorpia `spawn_npcs_once`, Chaos Elemental
  `teleport_player_nearby` + `unequip_player_items`).

## 2026-04-20 (cont.) — Phase 4 pass 2.1: event-bus wiring closes pilots

Took the 4 event-driven pilot primitives from "registered stubs"
to "actually fire in real fights" by plumbing `RC_EVT_PLAYER_DAMAGED`
end-to-end from the combat resolver through the encounter handler.

**What changed in combat.c:**
- New exported `rc_resolve_player_hits(world)` replaces the inline
  `resolve_player_hits` that used to live in tick.c. The new version
  iterates pending hits the same way `rc_resolve_pending` does
  (protection-prayer mitigation + compact) but additionally fires
  `RC_EVT_PLAYER_DAMAGED` per landing hit, carrying:
  - `source_npc_id` — NPC uid, or `0xFFFF` when `h->source_idx < 0`
    (player-side source, e.g. self-damage from a future effect).
  - `damage` — mitigated (post-protection) value, so subscribers see
    the real number that hits HP, not the pre-mitigation roll.
  - `style` — raw `RcCombatStyle`, for style-gated reactions.
- Added `#include "events.h"` to combat.c.
- tick.c's `resolve_player_hits` is now a comment pointing at
  combat.c; the tick dispatcher calls `rc_resolve_player_hits(world)`
  directly.
- Left `rc_resolve_pending` unchanged — it's still used by the NPC
  side, which doesn't need per-hit events yet.

**What changed in encounter.c:**
- `rc_encounter_init` now subscribes a third handler,
  `rc_encounter_on_player_damaged`, to `RC_EVT_PLAYER_DAMAGED`.
- New handler:
  1. Casts payload to `RcPayloadPlayerDamaged`.
  2. Ignores mitigated-to-zero hits (`damage == 0`) and non-NPC
     sources (`0xFFFF`) so flicked prayer doesn't drain.
  3. Looks up the active encounter by the source uid via
     `find_active_by_npc`.
  4. Iterates the spec's mechanics; for each one with
     `primitive_id == RC_PRIM_DRAIN_PRAYER_ON_HIT` and a non-NULL
     `prim`, invokes it with the mechanic's `param_block`.
- The KQ `bound_to = "Barbed Spines"` constraint isn't enforced yet
  — any KQ-sourced damage drains prayer in pass 2.1. Binding the
  drain to a specific attack name requires attack-level identity
  in the payload, which lands when the encounter engine starts
  driving boss attacks directly (pass 3+).

**What changed in encounter_prims.c:**
- `prim_telegraphed_aoe_tile` now queues its pending hit with
  `source_idx = boss->uid` instead of `-1`. This means a Scurrius
  Falling Bricks hit will fire `RC_EVT_PLAYER_DAMAGED` with the
  boss as source — no drain_prayer effect (Scurrius has no such
  mechanic), but the event correctly identifies the boss for any
  future subscribers.

**Test extension (`test_encounter_prims.c`):**
- New assertion block: spawns a stub Kalphite Queen def (cache id
  965), queues a pending hit with the KQ uid as source, calls
  `rc_resolve_player_hits(w)` directly, and asserts the player's
  prayer dropped by the expected 1 point. Exercises the full chain:
  combat resolver → event fire → encounter handler → primitive
  dispatch → prayer mutation.

**All 8 tests green:** test_base_only, test_combat, test_combat_e2e,
test_encounter, test_encounter_bin, test_encounter_prims,
test_determinism, test_pathfinding.

**Pilot story is now closed.** The 6 Scurrius + KQ primitives are
all (a) registered at load time via the primitive table, (b)
callable with correct param-block layout, and (c) either firing
periodically (Scurrius Falling Bricks / Minions) or event-driven
(KQ Barbed Spines drain). `heal_at_object` and
`preserve_stat_drains` remain callable-but-not-auto-triggered
pending phase-enter/exit event wiring — tracked under the "still
TODO for Phase 4 pass 2" bullets in `work.md`.

**File summary of this sub-pass:**
- `rc-core/combat.c` — new `rc_resolve_player_hits`; new events.h include.
- `rc-core/combat.h` — new prototype.
- `rc-core/encounter.c` — third event subscription + new handler.
- `rc-core/encounter.h` — new handler prototype.
- `rc-core/encounter_prims.c` — boss uid as source_idx.
- `rc-core/tick.c` — call the exported resolver instead of inline logic.
- `tests/test_encounter_prims.c` — event-chain regression assert.

## 2026-04-21 — Engine/content split: introduce `rc-content/`

Restructured the project into a two-layer architecture:
**`rc-core`** (generic engine, content-agnostic) + **`rc-content`**
(OSRS-specific scripts, depends on rc-core). Done before the
primitive grind so the 85+ remaining primitives don't all pile into
`rc-core/encounter_prims.c` and so isolated-sim build targets
(Colosseum-only, Scurrius-only) are a one-line CMake target rather
than a refactor.

**Motivation.** A comment in `prim_telegraphed_aoe_tile` — the
generic primitive — literally checked `g_npc_defs[boss->def_id].id ==
7221` to apply solo damage. That's the exact smell the split is
meant to prevent: engine code knowing about specific content
instances by name. If we kept going, 85 more primitives would
accumulate dozens of such checks. Fixing it now.

**Principles now documented in three READMEs:**
- `rc-core/README.md` — added §15 bullet "no OSRS-specific content
  in rc-core" + new §18 "Engine / content boundary" with the split
  table (generic primitive → rc-core; boss-specific script →
  rc-content; pure data → data/).
- `rc-content/README.md` — comprehensive design doc for the content
  layer: why it exists (isolated sims, clean boundaries, engine
  reuse), directory layout, per-module conventions, the split
  rule with a test ("if you removed this module, would the engine
  still compile and run?"), reference-repo usage rules (rsmod /
  void / 2011Scape only for OSRS-pre-2013 overlap bosses — not
  OSRS-only content), and the future isolated-sim build-target
  pattern.
- `rc-content/encounters/README.md` — per-boss file conventions:
  one `.c` per boss, named after TOML slug; `static` internals;
  single `rc_content_<slug>_register(struct RcWorld *)` public
  symbol; multi-file boss directories for raids + waves;
  reference-repo checklist for ported logic.
- `rc-content/regions/README.md` + `rc-content/quests/README.md`
  — scaffolding docs noting these directories are empty today +
  the expected pattern when regions / quests need code.

**New directory structure:**
```
rc-content/
├── README.md                  (design doc)
├── content.h                  (shared registration API)
├── content.c                  (aggregate rc_content_register_all)
├── encounters/
│   ├── README.md              (per-boss conventions)
│   ├── scurrius.c             (scaffolding — register fn, no scripts yet)
│   └── kalphite_queen.c       (scaffolding — register fn, no scripts yet)
├── regions/README.md          (scaffold-only)
└── quests/README.md           (scaffold-only)
```

**Public content API (`rc-content/content.h`):**
```c
void rc_content_register_all(struct RcWorld *world);
void rc_content_scurrius_register(struct RcWorld *world);
void rc_content_kalphite_queen_register(struct RcWorld *world);
```

Per-module register fns are called by callers (viewer, tests, sim
mains) after `rc_world_create_config`. rc-core never calls into
rc-content — strict one-way dependency.

**Engine cleanup:**
- `rc-core/encounter_prims.c::prim_telegraphed_aoe_tile` no longer
  checks for NPC id 7221 to apply solo-mode damage. Replaced with:
  "RuneC is single-player today — always apply solo_damage_max when
  the spec provides one; gate on a world-level flag when multiplayer
  lands." The generic primitive no longer knows about Scurrius.
- File header comment updated to explicitly declare the engine/content
  contract: "This file holds ONLY primitives that are reusable across
  multiple bosses. Boss-specific scripts belong in
  rc-content/encounters/<boss>.c."
- Verified by `grep -w "scurrius|kalphite|..." rc-core/` — the only
  remaining hits are comments describing canonical example usage
  ("telegraphed_aoe_tile: Scurrius Falling Bricks"), which is fine.

**CMake:**
- New `rc-content` static library with `file(GLOB_RECURSE ...)` over
  `rc-content/*.c`.
- `target_link_libraries(rc-content PUBLIC rc-core)` — one-way dep.
- Viewer + all tests now link both libraries.
- Tests + viewer call `rc_content_register_all(world)` after
  `rc_world_create_config` to establish the pattern (currently a
  no-op since the content modules are scaffolding — the call exists
  so future content lands without changing the call sites).

**Reference-repo usage codified:**
- Per the user's memory "OSRS vs 2011Scape" — codified in content
  README §5: rsmod / void / 2011Scape are valid references only for
  OSRS bosses with pre-2013 counterparts (KQ, GWD, DKS, Corp, Kraken,
  DK, Sire, Cerberus, Chaos Ele, Giant Mole, Scorpia, wilderness).
  OSRS-only content (Scurrius, Vorkath, Muspah, raids, DT2, Yama,
  Hueycoatl, Royal Titans, etc.) has no reference source — wiki
  reconstruction only.
- Comment-style convention for ported scripts: cite the specific
  source file + repo so the port's provenance is trackable.

**Future: isolated sim build targets.** Not built yet but the
design is ready. A Colosseum-only sim will look like:

```cmake
add_library(rc-content-colosseum STATIC
    rc-content/encounters/colosseum.c
)
target_link_libraries(rc-content-colosseum PUBLIC rc-core)
add_executable(rc-sim-colosseum sims/colosseum/main.c)
target_link_libraries(rc-sim-colosseum rc-content-colosseum)
```

The sim's main calls only the register fns it needs — not
`rc_content_register_all`. Unused boss modules never compile into
the binary. RL training workloads targeting one encounter won't pay
compile or runtime cost for the other 49.

**All 8 tests still green:** test_base_only, test_combat,
test_combat_e2e, test_encounter, test_encounter_bin,
test_encounter_prims, test_determinism, test_pathfinding.

**Files touched in this refactor:**
- `CMakeLists.txt` — new rc-content target, both linked into
  viewer + tests.
- `rc-core/README.md` — §15 updated + new §18 engine/content boundary.
- `rc-core/encounter_prims.c` — removed the Scurrius-specific check,
  updated header comment to declare the engine-only contract.
- `rc-content/` — entire directory new (9 files: content.h,
  content.c, 2 encounter modules, 4 READMEs).
- `rc-viewer/viewer.c` — include content.h, call
  `rc_content_register_all` after world create.
- `tests/test_encounter_prims.c` — same pattern as viewer.
- `work.md` — state snapshot updated; pick-up-here expanded to
  include the upcoming script-registry API work after
  phase-transition wiring.

**What this unblocks:**
- Writing `scurrius_heal_at_food_pile` won't clutter `rc-core` —
  it has a clear home in `rc-content/encounters/scurrius.c`.
- The 85-primitive grind for remaining bosses won't pile into one
  file — each boss's one-off scripts go to its own content module.
- Future isolated-sim targets need no refactor — the split is the
  prerequisite they were going to require anyway.

## 2026-04-21 (cont.) — Doc audit: staleness sweep across all .md files

Full pass over every `.md` file in the repo (15 files, 8,114 lines
total) to remove or flag stale content left over from earlier phases
+ the rc-content architecture split. Added explicit STATUS notices
to design docs that predate the current architecture so a reader
knows immediately what to trust and what's historical.

**Files updated:**

- `README.md` (git-tracked — only .md in git per `.gitignore`)
  — rewrote Architecture block to include `rc-content/` layer
    with explicit per-directory purposes.
  — replaced "Current State" bullets with accurate, dated status:
    world + rendering, engine subsystems, data pipeline, tests.
  — replaced "Upcoming" with actual next-steps (phase-transition
    wiring, script registry API, remaining primitives, UI, etc.).
  — added `osrsreboxed-db` + `OSRS Wiki` to Tools & References.
  — fixed build/run commands (binary is `./build/rc-viewer`, not
    `./rc-viewer/rc_viewer`).

- `AGENT_README.md` — added prominent STATUS block at top (30+
  lines) pointing readers at current docs (README, rc-core/README,
  rc-content/README, work.md, changelog) and listing the specific
  stale content (non-existent `rc-cache/` directory, old directory
  structure, outdated API sketches, superseded phase numbering,
  "upcoming" sections that describe done work). Body kept as
  reference material — the FC-lessons, reference-repo details,
  OSRS-formula citations, and tech-decision rationale are still
  valuable even when specific code layouts have moved on.

- `database.md` — STATUS block: Phase 0–6 COMPLETE, pointing at
  `data/defs/` contents + `changelog.md`. Body kept as the
  source-authority-ranking + scrape-DAG + ethics reference.

- `database_template.md` — STATUS block: pipeline operational,
  schemas here are authoritative for the curated-TOML layer,
  §A5 (encounter schema) is the spec the 50 TOMLs conform to.

- `things.md` — STATUS block: the Part-2 gap list this doc
  enumerated is mostly filled; current gap tracking lives in
  `work.md`. Part-1 taxonomy remains accurate.

- `references.md` — STATUS block: now 6 reference repos cloned,
  not 3. Hoisted the critical "OSRS ≠ 2011Scape" memory into the
  doc header as an explicit rule — don't use void/2011Scape as
  OSRS references for OSRS-only content. Pointed at
  `rc-content/README.md` §5 for the codified reference-repo usage
  rules for new content work.

- `VIEWER_VALIDATION.md` — rewrote to reflect pass-2.1 state.
  Was "7 headless tests, 92 primitives NULL-stubbed, Phase 4 pass
  1 validated" → now "8 headless tests, 6 primitives live + 85
  remaining, Phase 4 pass 2.1 validated." Added Check 4 (Scurrius
  pilot primitives firing during combat) + Check 5 (KQ prayer
  drain via event chain). Updated "Not validatable yet" list to
  distinguish between (a) primitives not yet authored,
  (b) phase-triggered primitives registered-but-not-auto-fired,
  (c) boss-specific scripts pending the script registry API.

- `data/curated/encounters/_primitives.md` — replaced the
  "deferred — see work.md TODO #2" stub reference with accurate
  status: 6 of 91 primitives implemented in
  `rc-core/encounter_prims.c`, generic-vs-boss-specific split
  codified per `rc-core/README.md` §18, remaining-85 priority
  order cited per `work.md` §1.1. Clarified the add-a-primitive
  checklist (enum in encounter.h + PRIMITIVE_IDS in exporter).

**Files NOT changed (already current):**
- `ignore.md` — scope-exclusion list; still accurate. No
  implemented-or-scraped items from the "out of scope" list.
- `work.md` — restructured earlier this session with the
  rc-content layer + refreshed "Pick up here" block.
- `rc-core/README.md` — §15 + new §18 already cover the
  engine/content boundary.
- `rc-content/*.md` (4 files) — just written alongside the
  architecture split.
- `changelog.md` — this is the source of truth for history;
  every substantive change goes here.

**Reader's map after this pass:** start with `README.md` for the
overview, then `work.md` for current state + pickup point, then
the two arch READMEs (`rc-core/` + `rc-content/`) for normative
rules. Other docs are reference material with explicit STATUS
notices flagging what's historical vs. current.

## 2026-04-22 — Bounded phase wiring, repo hygiene, and doc authority reset

### Encounter pass 2.2 — bounded slice only

Instead of implementing the whole authored trigger DSL, narrowed pass
2.2 to the part the current phase model can support cleanly:
single-phase `phase_enter:` / `phase_exit:` bindings on the existing
HP%-threshold phase system.

**Landed in the engine/data path:**
- `RC_EVT_PHASE_TRANSITION` completed as a real runtime path:
  payload added, fired from `tick_active`, consumed by the encounter
  subsystem.
- Encounter binary widened to carry bounded trigger metadata:
  `trigger_type` + `phase_idx`.
- Exporter now resolves simple `phase_enter:<phase>` /
  `phase_exit:<phase>` bindings and explicitly reports richer trigger
  forms as deferred instead of pretending they are live.
- Scurrius phase-enter heal now auto-fires through the bounded trigger
  path.
- Regression coverage added for both binary encoding and primitive
  auto-fire.

**Explicit non-goals of this slice:**
- no `phase_in`, `while_in_phase`, `after_attack`, unions, timed
  transitions, or `enter_after`
- no script registry API yet
- no boss-specific phase scripts yet

### Repo / build / test hygiene pass

Cleaned the repo so the tracked build/test path is trustworthy:
- switched validation to clean out-of-tree builds
- made tests cwd-independent
- kept assertions live in tracked test builds
- cleaned stale local clutter (`build/`, stray root junk)
- cleaned loader warning noise with checked-read helpers

**Verification run for the hygiene pass:**
- clean `RelWithDebInfo` build: passed
- clean `Release` build: passed
- `ctest`: `8/8` passed in both profiles
- manual `test_collision` diagnostic: clean

### Documentation / authority decisions

Made the doc model strict:
- planning belongs only in `work.md`
- READMEs are descriptive, not planning docs
- `AGENT_README.md` became the high-level repo truth for goals and
  architecture
- `database.md` became the single database catalog authority
- `database_template.md` was removed as redundant
- `ignore.md`, `things.md`, `references.md`, and
  `viewer_validation.md` were rewritten/cleaned to match their narrow
  intended roles
- component READMEs were cleaned to explain boundaries, files, and
  relationships instead of future plans

### GitHub markdown policy

Locked in the local-vs-GitHub doc split:
- only root/component `README.md` files stay tracked on GitHub
- all other `.md` files remain local planning/reference docs
- pushed that policy to branch `testing_2`

### Scope / v1 decisions recorded during the doc pass

- quests moved to `nice to have / time permitting`
- full item-database coverage remains a hard requirement even when an
  item's normal acquisition path is out of scope
- out-of-scope acquisition systems do **not** justify missing item
  defs, bonuses, or asset links

## 2026-04-23 — Full database audit completed; roadmap reprioritized

### NPC database closure slice

- Added `tools/export_npc_defs_full.py`, a broad NPC-definition exporter
  that merges `data_osrs`, the local `model_dump/osrs-dumps` NPC config
  dump, `osrsreboxed` monster stats, cached wiki monster overlays,
  RuneLite names, and curated activity-spawn edge IDs.
- Regenerated `data/defs/npc_defs.bin` as NDEF v3:
  - `15166` definitions
  - `5955` combat defs
  - `12950` definitions with model-ID links
  - `13525` definitions with stand animations
  - `13059` definitions with walk animations
- Enforced the v1 Sailing exclusion during export: `498` model-dump
  NPCs skipped, `392` RuneLite constants skipped as Sailing-linked,
  and `0` non-excluded RuneLite NPC constants missing.
- Added model-ID linkage to `RcNpcDef`, raised NPC definition capacity,
  added an O(1) NPC-ID index, and made `rc_world_create_config()` load
  configured NPC definitions once for NPC-using subsystem configs.
- Added `tests/test_npc_defs_bin.c` to assert the broad definition
  binary loads, key edge-case bosses exist, model links are present, and
  world creation can bring the NPC defs in.
- Added `tools/audit_npc_reconciliation.py` and
  `tools/reports/npc_reconciliation.txt`.
  Current reconciliation result: `0` non-excluded RuneLite NPC IDs
  missing, `0` drop-table IDs missing NPC defs, `0` world-spawn IDs
  missing NPC defs, and `0` non-excluded morph parent/target IDs
  missing NPC defs.
- Added `data/curated/regular_npc_special_mechanics.toml` as the
  ownership seed for non-boss combat mechanics; it currently tracks `9`
  families that need runtime support.
- Verification: exporter py_compile passed; assert-enabled Release
  `test_npc_defs_bin` passed; targeted coverage tests passed; full
  Release/coverage CTest remains blocked only by the pre-existing
  `test_encounter_prims` Scorpia HP assertion.
- Load benchmark: `100` invocations of `test_npc_defs_bin` completed in
  `0.71s`; that binary loads the full NPC database twice per run,
  roughly `282` full loads/sec or `4.3M` NPC definition records/sec.

### Wilderness bounded primitive slice resumed locally

Resumed the wilderness encounter batch with the bounded primitive model
instead of widening the engine with one-off hacks.

**New generic primitives / bounded exporter support:**
- `spawn_npcs_once`
- `periodic_heal_boss`
- `teleport_player_nearby`
- `unequip_player_items`
- primitive-specific `while_alive:<npc_name>` packing for
  `periodic_heal_boss`

**Bosses covered by this local slice:**
- Scorpia:
  - phase-enter guardian summon
  - guardian-based periodic boss heal
- Chaos Elemental:
  - confusion teleport
  - madness unequip effect

**Still deferred in the wilderness batch:**
- Obor / Bryophyta attack-table + protection parity
- Giant Mole burrow/incoming-hit path until the primitive/event model
  has a clean payload-aware incoming-hit route

**Verification run for this local encounter slice:**
- `python3 -m py_compile tools/export_encounters.py`
- exporter regeneration
- clean out-of-tree build
- `ctest`: `8/8` passed

### Full database completeness audit

Completed the full audit across all `14` planned tranches:
1. Items
2. NPCs / monsters / bosses
3. Drops
4. Spawns
5. Mechanics coverage
6. World objects / interactables
7. Tile / region / area / collision data
8. Varbit / varp / state-transform data
9. Prayer / spellbook / player-action data
10. Shops / banks / storage / non-drop acquisition
11. Transportation / traversal
12. Skills / gathering / recipes / production chains
13. Content activity schemas
14. Normalization / canonicalization / form-mapping

**Audit result:** all `14` tranches are currently `BLOCKS_PARITY`.

**Main cross-cutting blocker surfaced by the audit:**
- `rc_world_create_config()` still does not load most compiled runtime
  datasets, so source/export breadth and runtime readiness are still
  different things.

**Main blocker groups folded back into the roadmap:**
- runtime data bring-up + slice-sized caps
- canonicalization / source reconciliation
- world-surface data ownership
- missing runtime owners for key data families
- activity-schema widening
- per-family parity closure for items/NPCs/drops/spawns/mechanics

### Planning / execution policy updates

- `work.md` was restructured around the completed audit and now pauses
  broad feature expansion behind the first post-audit blocker pass
- `AGENT.md` added as the local execution-policy doc:
  - performance and simplicity first
  - concise, explicit comments only
  - avoid verbosity
  - code/logic changes require tests, coverage review, and RL-style
    performance benchmarking

### Scope / audit decisions recorded

- RuneC v1 still intentionally targets the pre-Sailing `23`-skill
  surface even though live OSRS now has `24` skills
- full database coverage remains the target for all in-scope data
  families, not only currently runnable gameplay loops
- regular combat NPC families with special behavior need explicit
  parity work; they are not considered "covered" just because generic
  combat stats exist

## 2026-04-24 — Database docs synced to current closure state

- Updated `database.md` so the database catalog now reflects the live
  full-NPC export path:
  `tools/export_npc_defs_full.py`,
  `tools/reports/npc_defs_full.txt`,
  `tools/reports/npc_reconciliation.txt`, and
  `data/curated/regular_npc_special_mechanics.toml`.
- Recorded the current NPC render gap explicitly in the catalog:
  `npc_defs.bin` is broad, but `data/models/npcs.models` is still only a
  slice-sized render dataset.
- Synced `work.md`, `temp_databaseaudit.md`, and
  `tools/reports/database_audit.txt` to the current active next-order
  inside the NPC lane:
  mesh export -> semantic reconciliation -> boss mechanics closure ->
  Nex/Sol activity-spawn extraction -> remaining runtime-owner work.
- Corrected the `rc-content/encounters/README.md` NPC-data reference to
  the current full-NPC exporter instead of the older slice exporter.

## 2026-05-04 — Combat visual/projectile and viewer pre-test refinement

- Added a core combat visual-definition loader in
  `rc-core/combat_visuals.{h,c}`.
  - The loader consumes `data/defs/combat_visuals.tsv`, a data-backed
    record file for attack animation IDs, launch/travel/impact
    spotanim tokens, projectile model/animation tokens, and hit/client
    delay hints.
  - This keeps weapon/spell visual selection out of viewer code and out
    of ad-hoc testing branches.
  - Initial records cover Magic shortbow, Rune arrow, and Fire Blast
    using local RSPS/reference data: RSMod-style ranged projectile
    resolution, VoidPS Fire Blast projectile naming, and OSRS symbol
    animation IDs.

- Added world-level combat projectile state in `rc-core/types.h` and
  public accessors in `rc-core/combat.h` / `rc-core/combat.c`.
  - Player ranged/magic swings now emit `RcCombatProjectile` records
    only after a real attack is queued and resource checks pass.
  - Projectile records store source/target actor metadata, source and
    target tiles, style, weapon/ammo/spell IDs, visual tokens, delay,
    duration, and age.
  - `rc_world_tick()` now advances and expires active projectile records.
  - Downstream impact: viewer and future clients can render projectiles
    from core state without owning combat rules. Future data work still
    needs complete cache-backed projectile model/spotanim export coverage
    for all weapons, ammo, spells, and NPC attacks.

- Updated player/NPC combat animation exposure.
  - `RcCombatActorState` and `RcCombatViewState` now carry
    `attack_animation_id` alongside the existing animation timer.
  - Player attack visuals prefer data-backed attack animation records,
    falling back to the older viewer animation mapping when no data
    record exists.
  - NPC attacks now populate the combat attack-animation ID from
    `npc_defs.bin` attack animation metadata.
  - Upstream impact: NPC definition animation coverage matters more
    visibly now; missing or unavailable animation sequences still
    degrade to rest-pose rendering.

- Tightened combat approach pathing in `rc-core/combat.c`.
  - Player combat approach now evaluates candidate attack tiles around
    the target footprint and only accepts exact successful paths.
  - The previous approach could accept pathfinder alternatives, which
    caused bad visual behavior such as routing toward arbitrary nearby
    tiles, buildings, or non-attack positions.
  - Added regression coverage in `test_combat_phase3_movement_range_facing`
    to assert the selected route endpoint is outside the NPC footprint
    and adjacent to the target footprint.
  - Downstream impact: if no exact attack tile is reachable, the player
    now stops instead of taking a misleading alternative route. Later
    interaction-pathing cleanup should apply the same exact-tile
    selection discipline to noncombat object/NPC interactions where
    needed.

- Updated `rc-viewer/viewer.c` to consume core visual state.
  - Player attack animation selection now checks
    `player.combat.attack_animation_id` before using legacy viewer-side
    fallbacks.
  - Viewer now draws active core projectile records in 3D. If a
    projectile model token is present in the loaded model bundle, the
    viewer uses that model; otherwise it draws a style-colored fallback
    projectile marker.
  - NPC animation playback now loads `data/anims/all.anims` as a
    fallback for sequences missing from `data/anims/npcs.anims`.
  - Downstream impact: projectile rendering is architecturally hooked up,
    but exact projectile geometry remains limited by available exported
    spotanim/projectile model bundles.

- Corrected the bow/staff equipped-rendering regression in
  `data/models/items.models`.
  - Only the synthetic equipped Magic shortbow and Staff of air models
    appended from the mismatched cache source were oversized.
  - Scaled the affected synthetic male/female equipped model records
    back to the same unit system as the rest of the known-good item
    bundle:
    `14680925`, `23069533`, `14681445`, and `23070053`.
  - Ground models and existing known-good equipment models were not
    regenerated or replaced.
  - Known gap: the item-render exporter still needs a generalized
    source-aware normalization/scaling path before future bulk
    regeneration from mixed cache sources.

- Added `tests/test_combat_visuals_projectiles.c`.
  - Verifies data-backed ranged projectile emission for Magic shortbow
    plus Rune arrows.
  - Verifies data-backed magic projectile emission for Fire Blast.
  - Verifies attack animation IDs are sourced from combat visual records
    and stored in core combat state.

- Updated `work.md` and `work_highlevel.md`.
  - Both docs now show this as the active pre-test combat/viewer
    refinement before banking/storage resumes.
  - The known revisit debt now distinguishes complete projectile model
    exports and full animation coverage from the first-pass projectile
    event architecture.

## 2026-05-05 — b237 cache visual fidelity hardening

- Change made: tightened the repo-local b237 visual asset pipeline around
  model render metadata, alpha cutouts, texture atlas behavior, and OSRS-style
  lighting.
- Why it was made: manual viewer validation still showed three core symptoms:
  alpha-masked foliage textures were smeared or striped, cutout planes could
  appear as large white triangles, and some props rendered too flat or dark.
  Those symptoms pointed at the shared model/texture expansion path rather than
  just one bad exported object.
- Exact surfaces changed:
  - `tools/cache_pipeline/export_models.py` now preserves per-face render
    types across b237 model decoders and merged submodels.
  - Model expansion now computes vertex/face normals, handles signed alpha
    sentinels, keeps flat versus Gouraud behavior, and applies client-style
    lighting using ambient/contrast and the OSRS light vector.
  - Textured faces now receive grayscale lighting multipliers instead of
    unlit white vertex colors; untextured faces receive lit HSL-derived colors.
  - `tools/cache_pipeline/export_objects.py` now parses location ambient and
    contrast opcodes and passes them into model expansion for object meshes.
  - The shared sprite/texture path keeps RGBA texture data so transparent
    pixels stay transparent in the atlas, and textured atlas misses now render
    magenta instead of silently falling back to bland colors.
  - The viewer textured object/model shaders discard alpha-cutout texels before
    drawing opaque cutout masks, and atlas sampling remains point-filtered to
    reduce texture bleed.
- Upstream/downstream impacts:
  - Active object, NPC, item/equipment, and projectile model exports continue
    to use the local OpenRS2 b237 cache under
    `tools/cache_pipeline/source/current_fightcaves_demo/data/cache`.
  - Remaining visual problems should now be diagnosable as a specific model,
    texture, object definition, terrain/floor decode issue, or runtime
    plane/scene-loading gap.
  - `data/models/player.models` is still generated through the older MDL2
    player exporter path; item/equipment, NPC, projectile, and object visual
    paths are the current textured MDL3/OBJ2 paths.
- Verification:
  - `python3 -m py_compile` passed for the changed cache/exporter modules.
  - Focused object, projectile, and item-render smoke exports completed against
    the local b237 cache.
  - Real viewer assets were regenerated for Varrock objects/atlas, item
    models/atlas, projectile models/atlas, NPC models/atlas, and player models.
  - Header checks confirmed `OBJ2`, `ATLS`, and `MDL3` outputs on regenerated
    assets.
  - `cmake --build build -j2` completed successfully.

## 2026-05-05 — Collision decode fix and viewer brightness lift

- Change made: corrected the b237 location/object definition opcode handling
  that feeds collision export, regenerated collision-derived runtime assets,
  and lifted the viewer object/model shader brightness.
- Why it was made: manual validation showed the player could path through
  walls/bridge-like objects and the latest lighting pass looked too dark with
  overly strong shadows.
- Exact surfaces changed:
  - `tools/cache_pipeline/rc_cache/definitions.py` now treats location opcode
    `19` as RuneLite's wall/door marker instead of `interact_type`.
  - Location opcode `27` now sets `interact_type = 1`, matching RuneLite's
    object decoder. Collision export uses `interact_type != 0`, so the prior
    opcode mixup could drop valid blockers.
  - Regenerated `data/regions/varrock.cmap`,
    `data/defs/collision_tiles.bin`, and `data/defs/object_defs.bin` from the
    local b237 cache.
  - `rc-viewer/viewer.c` now applies a small post-bake brightness lift in the
    alpha-cutout shader after transparent texel discard.
- Upstream/downstream impacts:
  - The Varrock slice now exports more collision-marked objects; region
    `50,53` increased from `3,693` to `4,333` collision objects and from
    `1,541` to `2,010` non-zero plane-0 collision tiles during export.
  - The full sparse collision catalog now contains `6,310,663` non-zero
    collision tiles across `2,331` regions.
  - Some bridge/plane behavior may still need the planned plane-aware
    scene/region pass, but ordinary wall/object pathing should no longer be
    weakened by the opcode decode bug.
- Verification:
  - `python3 -m py_compile` passed for the changed cache/exporter modules.
  - `python3 tools/export_collision.py ... --output data/regions/varrock.cmap`
    completed with `35,807` non-zero plane-0 tiles in the 25-region Varrock
    viewer slice.
  - `python3 tools/export_collision_tiles.py --output data/defs/collision_tiles.bin`
    completed with `0` parse/read errors.
  - `python3 tools/export_object_defs.py --cache ...` completed with `0`
    unknown opcode warnings.
  - `cmake --build build -j2`, `./build/test_collision_tiles_runtime`,
    `./build/test_object_defs_bin`, `./build/test_pathfinding`, and
    `./build/test_collision` passed.

## 2026-05-05 — Cache pipeline stop point for tomorrow

- Change made: documented the manual validation stop point for the b237
  cache/asset pipeline.
- Why it was made: the latest viewer check showed the general collision fix
  helped ordinary walls, but two visible Step 6 issues remain and should be
  resolved before starting Step 7.
- Findings to carry forward:
  - Ordinary wall/object collision is materially better after the b237
    location opcode fix.
  - Bridge traversal still behaves incorrectly: the player can walk
    through/under the bridge rather than using the correct bridge plane/scene
    semantics. Treat this as a bridge/link-below/plane handling issue for the
    next work session.
  - Player and NPC models still have stronger shadow contrast than the
    surrounding environment. The next visual pass should tone down dynamic
    model lighting/shadowing separately from terrain/object environment
    lighting.
- Roadmap state:
  - Steps 1 through 5 remain complete for the active b237 pipeline.
  - Step 6 is nearly complete but not closed until bridge traversal and
    dynamic model lighting balance are fixed.
  - After those two Step 6 issues are resolved, proceed to Step 7: rework
    animation export and C loading together.
- Verification:
  - Documentation-only stop-point update. No new code validation was run for
    this entry.

## 2026-05-05 — Bridge heightmap and dynamic model lighting pass

- Change made: implemented the two remaining Step 6 visual/runtime fixes from
  the manual validation stop point.
- Why it was made: the Barbarian Village bridge still looked like the player
  was walking through/under it, and player/NPC models had stronger shadow
  contrast than the surrounding environment.
- Exact surfaces changed:
  - `tools/cache_pipeline/export_terrain.py` now supports a
    `resolve_link_below` heightmap mode. The exported Varrock runtime
    heightmap resolves plane-0 actor grounding to plane 1 on `LINK_BELOW`
    bridge scene tiles while leaving the terrain mesh/object heightmap paths
    stable.
  - Regenerated `data/regions/varrock.terrain` from the local OpenRS2 b237
    cache. The bridge tiles around `3103..3107,3420..3421` now sample elevated
    bridge-plane heights for runtime grounding.
  - `rc-viewer/viewer.c` now creates separate alpha-cutout shaders for static
    scenery/projectiles and dynamic player/NPC/equipment models. Dynamic
    models use a softer shadow lift; static scenery keeps the existing
    environment tuning.
  - Viewer minimap/collision overlay/debug collision queries now use
    `player.plane` instead of hardcoded plane 0.
  - `tests/test_collision_tiles_runtime.c` now asserts the Barbarian Village
    bridge collision rails block north/south entry while allowing east/west
    bridge traversal.
- Upstream/downstream impacts:
  - This fixes the immediate bridge presentation issue without changing the
    `TERR` binary format or the core movement API.
  - Full plane-aware scene streaming remains later work for dungeons, ladders,
    stairs, instanced regions, and explicit current-plane visual loading.
  - Dynamic character lighting can now be tuned independently from static
    object/terrain lighting during manual viewer validation.
- Verification:
  - `python3 -m py_compile tools/cache_pipeline/export_terrain.py` passed.
  - `cc -fsyntax-only -std=c11 -Ilib/raylib/include -I. rc-viewer/viewer.c`
    passed.
  - Regenerated `data/regions/varrock.terrain` from the local b237 cache.
  - A focused binary heightmap check confirmed the bridge center samples
    elevated heights after regeneration.
  - `cmake --build build -j2` passed.
  - `./build/test_collision_tiles_runtime`, `./build/test_collision`, and
    `./build/test_pathfinding` passed.
  - `ctest --test-dir build -R "test_collision_tiles_runtime|test_pathfinding|test_collision" --output-on-failure`
    passed the two registered targeted tests; `test_collision` is not
    registered with CTest and was run directly.
  - `timeout 8 env RC_VIEWER_EXIT_FRAMES=1 ./build/rc-viewer` loaded the
    viewer and exited successfully after one frame.
  - Full `ctest --test-dir build --output-on-failure` remains blocked by
    pre-existing generated-data catalog failures unrelated to this change
    (missing/stale `data/defs` binaries such as activity schemas, drops,
    normalization, slayer, traversal, and related catalogs).
  - Coverage artifacts were unavailable in the current build (`build` contains
    no `.gcno` or `.gcda` files), so line coverage could not be measured.
  - No SPS benchmark was run because this pass does not change core tick or
    pathfinding runtime logic; it changes exporter-generated terrain grounding,
    viewer shader presentation, and a regression assertion.

## 2026-05-05 — UI text, item icons, and minimap runtime fixes

- Change made: addressed the first manual UI validation issues after Phase 1/2
  UI work.
- Why it was made:
  - Compact UI text was visibly losing strokes or rendering too thin.
  - Inventory slots were still falling back to placeholder shapes instead of
    item icons.
  - The minimap used synthetic terrain colors only, and NPC dots were updated
    from integer tile positions.
- Exact surfaces changed:
  - `rc-viewer/ui_assets.c` now loads both `data/fonts/runescape.ttf` and
    `data/fonts/runescape_small.ttf`, generates higher-resolution font atlases,
    and selects the small OSRS font for compact text sizes.
  - `rc-viewer/ui.c` now supports a runtime item-icon cache and draws those
    textures before falling back to legacy placeholder/item-name sprites.
  - `rc-viewer/viewer.c` renders the current inventory/equipment item icons
    into small textures from `data/models/items.models` and
    `data/models/item_render.map`.
  - `rc-viewer/viewer.c` now samples the local OpenRS2 map PNG for minimap
    colors, with baked map labels/grid pixels filtered back to the clean local
    minimap fallback.
  - NPC/player minimap dot offsets now interpolate between previous/current
    tile positions so dots move with runtime actors instead of snapping only at
    whole-tick integer positions.
- Current caveat:
  - Inventory icons are now cache/model backed and correct per item id for the
    loaded runtime inventory, but this is not yet the exact OSRS 2D item sprite
    renderer. The next UI asset pass should export and use item icon zoom,
    rotations, x/y offsets, quantity modes, and stack/noted variants.
- Verification:
  - `cc -fsyntax-only -std=c11 -Ilib/raylib/include -I. rc-viewer/ui.c` passed.
  - `cc -fsyntax-only -std=c11 -Ilib/raylib/include -I. rc-viewer/ui_assets.c` passed.
  - `cc -fsyntax-only -std=c11 -Ilib/raylib/include -I. rc-viewer/viewer.c` passed.
  - `cmake --build build -j2 --target rc-viewer` passed.
  - `timeout 8 env RC_VIEWER_EXIT_FRAMES=2 RUNEC_UI_DECODED=1 RUNEC_UI_START_TAB=3 RC_VIEWER_SCREENSHOT=/tmp/runec-ui-font32.png ./build/rc-viewer`
    completed and produced a screenshot with OSRS fonts loaded, runtime item
    icons visible, and the map-backed minimap active.
