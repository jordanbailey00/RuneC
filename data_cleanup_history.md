# RuneC Data Cleanup History

This is the historical record of completed data-cleanup work. It is not the
active plan. Remaining work is tracked only in `data_cleanup.md`.

## Source And Layout Cleanup

- Created tracked RuneC-owned content inputs under `content/`, including
  catalogs and domain content for encounters, mechanics, dialogue, activity
  spawns/state machines, item effects, and combat visuals.
- Moved the cleanup direction away from ignored `data/curated` style source
  ownership and toward tracked source inputs plus generated runtime outputs.
- Established the working split between:
  - tracked source/content inputs;
  - generated runtime data under `data/`;
  - release artifacts under `dist-data/`;
  - local installed packs under `data/packs`.
- Added and maintained source-policy docs for blocked private-server and
  wrong-game authorities.
- Added validation tooling for source authority and direct external-repo
  residue, including checks that active runtime/tooling does not silently depend
  on private-server or wrong-game data.

## Pipeline And Packaging Work

- Added a single pipeline driver in `tools/data_pipeline.py` with stages for
  content export, validation, pack generation, unpacking, and report checks.
- Replaced the remaining top-level `inventory_existing_outputs` stage behavior
  with explicit rebuild-coverage records:
  - cache/render stages now record required `RUNEC_B237_CACHE` /
    `RUNEC_B237_DUMP` inputs, planned exporter commands, observed outputs, and
    required rebuild gaps;
  - `export-defs` records required non-content definition rebuild inputs,
    commands, outputs, and blockers for legacy research-source migrations or
    formal source gaps;
  - `pack-runtime-data` refuses to pack release-looking artifacts while required
    rebuild gaps remain;
  - `tools/data_pipeline.py --check` fails on unresolved required rebuild gaps.
- Compared the two user-provided b237 cache archives and chose the flat OpenRS2
  tarball because the current cache pipeline is built around the OpenRS2
  `cache/<archive>/<group>.dat` layout; the `.dat2/.idx` zip was not imported.
- Extracted the chosen cache to the ignored local maintainer-input path
  `data/source/b237-openrs2-2528/cache` and verified the install is about
  `535M`.
- Updated source policy so `RUNEC_B237_CACHE` may point at that ignored local
  cache install while decoded b237 dumps, external repos, and wiki caches remain
  non-repo maintainer/research inputs.
- Validated the local b237 cache against
  `/home/joe/projects/referencee/model_dump/osrs-dumps` with
  `tools/cache_pipeline/validate_b237_cache.py`.
- Ran `tools/data_pipeline.py all` with explicit `RUNEC_B237_CACHE` and
  `RUNEC_B237_DUMP` inputs. The b237-backed rebuilds regenerated:
  - item render models;
  - projectile models;
  - animations;
  - UI sprites;
  - UI interfaces;
  - Varrock terrain, objects, object-animation companions, and collision map;
  - items;
  - varbits and varps;
  - object definitions, object placements, and collision tiles;
  - prayers and player actions.
- Confirmed runtime packing was intentionally blocked by the rebuild-gap guard
  while item sprites, legacy-source migrations, and Step 2 source gaps remained
  unresolved.
- Fixed pipeline inventory records so the cache-derived asset stage records the
  real `data-sources/sources.lock` input instead of the stale
  `data-sources/source_locks.toml` path.
- Wired `tools/data_pipeline.py export-content` to regenerate the runtime
  outputs whose approved inputs are tracked under `content/`:
  - encounters;
  - activity spawns;
  - activity states;
  - activity mechanics;
  - activity schemas;
  - dialogue;
  - combat visuals;
  - item-effect validation reports.
- Added pipeline stage records under `generated/pipeline/stages/` that record
  exporter commands, observed outputs, and deferred content exports.
- Removed wall-clock `elapsed_ms` churn from content reports so repeated exports
  do not dirty reports solely because runtime changed.
- Fixed `content/catalog.toml` to point activity-spawn generation at
  `data/defs/activity_spawns.bin`.
- Built local runtime-data packs with `tools/pack_runtime_data.py` and installed
  them with `scripts/setup-data.sh --offline dist-data`.
- Verified local smoke loading through both pack and loose asset backends.

## Release Gate And Runtime-Data Lock Work

- Added release-gate tracking for source gaps and provisional required runtime
  data.
- Kept `runtime-data.lock` in non-official mode while source gaps remained.
- Added validation preventing official runtime-data release metadata from being
  filled while release-blocking source gaps remain.
- Recorded the then-current release-blocking source-gap datasets in the
  lock/gap flow:
  - `area_flags`;
  - `npc_spawns`;
  - `spells`.
- Kept local/offline runtime-data setup working from generated `dist-data`
  while official clean-clone release artifacts remain unpublished.

## Source Gap Work Completed

- Removed `combat_visuals` from the release-blocking source-gap list.
- Regenerated combat visuals from tracked RuneC-owned
  `content/combat_visuals/visuals.tsv`.
- Blocked known private/wrong-game combat-visual provenance markers in the
  exporter/report path.
- Validated curated combat visual NPC rows as reviewed `curated:b237:` rows.
- Confirmed combat visuals load in current runtime smoke validation.

## Full Rebuild And Source-Gap Closure

- Added `tools/cache_pipeline/export_item_sprites_b237.py` and wired
  `tools/data_pipeline.py` to render `data/sprites/items/` from the b237 cache
  item config and inventory models.
- Rebuilt item sprites through the pipeline:
  - rendered 33,295 item sprite PNGs;
  - wrote 1,398 stack icon variant rows;
  - kept transparent outputs for null/template/internal item rows whose b237
    definitions have no inventory model.
- Added tracked RuneC-owned first-release runtime snapshots under
  `content/runtime_snapshots/`, with a manifest containing file sizes and
  SHA-256 checksums.
- Added `tools/export_runtime_snapshots.py` to verify the snapshot manifest,
  install selected logical files into `data/`, and regenerate source-clean
  reports for migrated datasets.
- Migrated the remaining legacy-source definition/gameplay outputs to tracked
  runtime snapshot inputs:
  - NPC definitions;
  - static NPC spawns, including the accepted first-release
    direction/wander-radius simplification;
  - drops, RDT/GDT/MRDT, acquisition sources, and normalization;
  - recipes, skill drops, and shops;
  - quests and slayer;
  - regular NPC mechanics;
  - object behaviors, transports, traversal edges, and gathering nodes.
- Closed the remaining formal source gaps by moving them to reviewed
  first-release runtime snapshot authority:
  - `area_flags`;
  - `npc_spawns`;
  - `spells`.
- Updated `data-sources/source_gaps.json`, source-authority validation, report
  validation, and `runtime-data.lock` so formal source gaps are empty and the
  release status is `publishable`.
- Updated `data-sources/sources.lock` for the new tracked content tree and empty
  source-gap report checksum.
- Ran the full clean generated pipeline with explicit b237 inputs:
  - `RUNEC_B237_CACHE=data/source/b237-openrs2-2528/cache`
  - `RUNEC_B237_DUMP=/home/joe/projects/referencee/model_dump/osrs-dumps`
  - `python3 tools/data_pipeline.py --clean-generated all`
- The clean pipeline rebuilt data, generated reports, packed runtime data, and
  produced 16 runtime-data packs containing 34,624 assets.
- Ran `python3 tools/data_pipeline.py --check`; validators, rebuild coverage,
  loose manifest checks, runtime pack checks, and report validation passed with
  `release_status=publishable`.

## `RcGameData` Ownership Work

- Introduced `RcGameData` ownership for migrated immutable runtime datasets.
- Migrated dialogue, shops, skills, quests, and varbit/varp definitions into
  owned `RcGameData` containers with active runtime views/accessors.
- Moved load/activation for those migrated datasets into `rc_game_data_load()`
  instead of direct legacy loading from `rc-core/world.c`.
- Kept live mutable state, such as player quest progress and live varp values,
  in world/player state rather than shared immutable data.
- Added `tools/validate_direct_global_access.py` coverage so production callers
  use accessors or `RcGameData` views for migrated tables.
- Updated `tests/test_game_data_shared.c` to verify `RcGameData` views and
  active runtime accessors instead of comparing transitional global mirror
  addresses.
- Left compatibility globals only where legacy loaders, fixtures, or benchmarks
  still need them.

## Interaction And Content Registry Cleanup

- Removed the legacy process-global interaction handler table and
  `rc_interaction_*_handler` compatibility APIs.
- Moved production interaction handler registration and dispatch to per-world
  `RcWorld` storage.
- Updated interaction, ground-item, and combat handoff tests to register custom
  handlers with `rc_interaction_register_world_handler()` on the test world.
- Added multi-world coverage proving that clearing handlers from one world does
  not fall back to mutable process-global interaction state.
- Kept immutable primitive/code dispatch tables as intentional static code
  tables.

## Viewer And Runtime Loading Work

- Confirmed the viewer does not enable `RC_SUB_DIALOGUE`; dialogue data is
  packaged as general runtime data but is not loaded by viewer startup unless a
  caller explicitly requests the dialogue subsystem.
- Added `rc_asset_size()` so callers can query loose or packed asset sizes
  without fully opening/inflating an asset.
- Removed the default object vertex cap by setting the default object vertex
  limit to full-scene loading.
- Added optional `RUNEC_OBJECT_VERTEX_LIMIT` behavior only when explicitly set.
- Added object mesh support for shared atlases so multiple object chunks can
  share one loaded texture.
- Added full-scene Varrock object chunk indexing/loading:
  - default chunk directory: `data/regions/varrock_chunks`;
  - default prefix: `varrock`;
  - default chunk size: 64 tiles;
  - default draw radius: 180 tiles;
  - fallback to monolithic object mesh when chunks are unavailable.
- Added `tools/cache_pipeline/extract_runtime_scene_slice.py` support for
  centroid triangle selection so chunk grids assign each triangle to one chunk
  instead of duplicating boundary triangles.
- Regenerated the 25 full Varrock object chunks.
- Added `tools/cache_pipeline/subset_models_bundle.py` to create smaller model
  bundles by copying selected MDL2/MDL3 entries.
- Derived a full-Varrock NPC model subset from active Varrock spawns plus the
  dev combat dummy, producing `data/models/npcs_varrock.models`.
- Updated default full-Varrock startup to use the full-Varrock NPC model subset
  when no scene or NPC overrides are set.
- Rejected the reduced generated Varrock-bank startup slice after manual
  validation showed it hid too much world content.
- Restored the default startup path to the full fixed Varrock scene.
- Rebuilt and installed local packs after chunk and NPC-subset generation.

## Viewer Validation Findings

- User manual validation found the original monolithic startup path took about
  45 seconds and was too laggy after load.
- User manual validation rejected the generated bank-only startup path because
  it rendered a smaller grid and hid required world content.
- Current full Varrock startup is the retained baseline.
- User manual validation then exposed the next blocker: non-Varrock
  destinations reached through object transports, dungeon transitions, or boss
  validation teleports do not yet have complete default visual scene coverage.
- Boss validation can spawn/focus boss NPCs without terrain, objects,
  destination NPC population, or complete projectile visuals because that path
  prepares the boss independently of visual scene coverage.
- Projectile model loading is currently disabled by default as a startup
  performance workaround and needs lazy/scoped loading before combat visual
  validation can close.

## Step 2 Viewer Validation Readiness Implementation

- Added `tools/cache_pipeline/export_validation_scenes.py` as the tracked
  b237 exporter for the first-release viewer validation scene set.
- The exporter cleans `data/regions/scene_cache` before writing the approved
  scene set so stale local scene-cache files are not packed.
- Wired validation scene export into the cache-derived `regions` rebuild stage
  and added `data/regions/scene_cache` to `--clean-generated`.
- Generated and packed scene assets for:
  - Graardor, KBD, Vorkath, and Jad boss validation teleports;
  - Edgeville dungeon, Varrock Rat Pits, Varrock sewer, Wilderness
    lever/coffin, Observatory ladder, and Yanille railing object/traversal
    smoke destinations.
- Updated viewer scene reloads to load the required destination plane first,
  so nonzero-plane destinations such as Graardor plane 2 do not depend on
  plane 0 visual assets.
- Added `RUNEC_VIEWER_SMOKE_SCENES=1` to verify required validation scene
  assets exist in both loose and pack backends.
- Narrowed generated scene freshness checks to the actual scene exporter
  dependencies; runtime definition binaries are not inputs to the b237 scene
  mesh exporter.
- Added lazy projectile model loading for active combat projectile/spotanim
  model IDs, preserving the optional `RUNEC_LOAD_PROJECTILE_MODELS=1` eager
  full-pack path without making it the default.
- Added a local development pack fallback so `RUNEC_ASSET_BACKEND=pack` can
  read freshly built `dist-data/packs` when official/setup-installed
  `data/packs` is absent.
- Rebuilt the full data pipeline with:
  - `RUNEC_B237_CACHE=data/source/b237-openrs2-2528/cache`
  - `RUNEC_B237_DUMP=/home/joe/projects/referencee/model_dump/osrs-dumps`
  - `python3 tools/data_pipeline.py --clean-generated all`
- The rebuilt manifest contains 34,631 runtime assets, including 70 validation
  scene-cache assets.
- Ran `python3 tools/data_pipeline.py --check`; it passed.
- Ran `RUNEC_VIEWER_SMOKE=1 RUNEC_VIEWER_SMOKE_SCENES=1
  RUNEC_ASSET_BACKEND=loose ./build/rc-viewer`; it passed and checked 10
  validation scenes.
- Ran `RUNEC_VIEWER_SMOKE=1 RUNEC_VIEWER_SMOKE_SCENES=1
  RUNEC_ASSET_BACKEND=pack ./build/rc-viewer`; it passed and checked 10
  validation scenes.
- Manual viewer validation is still pending and is required before Step 2 can
  close.

## Validation Already Run

- `cmake --build build -j2 --target rc-viewer test_game_data_validation`
  passed after the full-Varrock startup work.
- `RUNEC_VIEWER_SMOKE=1 RUNEC_ASSET_BACKEND=pack ./build/rc-viewer` passed.
- `RUNEC_VIEWER_SMOKE=1 RUNEC_ASSET_BACKEND=loose ./build/rc-viewer` passed.
- `ctest --test-dir build --output-on-failure` passed 69/69 tests.

## Deferred Ownership Work Identified

These items were identified but intentionally left out of the first
data-cleanup finish line unless the release bar changes:

- Remove remaining `RcGameData` compatibility globals after replacement fixture
  helpers exist.
- Move player action gates, combat profiles, monster mechanics, and slayer
  immutable definitions fully into `RcGameData` or document permanent carveouts.
- Expand viewer visual coverage beyond the agreed validation destinations.
- Perfect every boss mechanic beyond the current validation scope.
