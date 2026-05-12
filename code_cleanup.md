# Cache Pipeline Cleanup Plan

Last updated: 2026-05-12

## Goal

Build a repo-local b237 cache and asset pipeline for RuneC.

The pipeline must decode the OpenRS2 b237 cache we actually use, generate
deterministic RuneC assets, and keep runtime behavior owned by this repo.
Reference projects are audit material only; RuneC must not depend on them at
runtime or call them during normal export/rendering.

Why this work matters:

- remove old cache assumptions and brittle compatibility paths
- make terrain, objects, collision, models, textures, sprites, UI, animations,
  spawns, and transports come from one coherent b237 pipeline
- keep renderer/runtime data correct enough to support combat, traversal,
  dungeons, instances, and later league-specific systems
- keep the implementation practical: reuse existing RuneC exporters where they
  are correct, replace the parts that are duplicated, slow, or wrong

## Active Inputs

- Cache: `tools/cache_pipeline/source/current_fightcaves_demo/data/cache`
- Cache series: OpenRS2 oldschool live b237, `2026-04-29`, cache `#2528`
- Keys: matching b237 keys file, empty list because this cache is not XTEA
  encrypted
- Decoded oracle: `tools/cache_pipeline/source/osrs-dumps`
- Generated/runtime assets: `data/` in the separate local RuneC-DB repo
- Reference checkouts: `/home/joe/projects/runescape-rl-reference`

## Completed

- Step 1: inventoried active exporter contracts, generated formats, consumers,
  and smoke commands.
- Step 2: created repo-local `rc_cache` foundation for cache store access,
  containers, groups, configs, sprites, textures, models, maps, and definitions.
- Step 3: validated the b237 cache against the Joshua-F dump and fixed unnamed
  b237 map/loc archive resolution.
- Step 4: moved terrain, object placement, object mesh, dense collision, sparse
  collision, and region export onto the shared b237 map/loc path.
- Step 5: promoted object, item, NPC, varbit, varp, sequence, frame, framemap,
  and spot animation decoders into exporter-grade shared decoders.
- Step 6: moved the active visual asset path onto shared model/sprite/texture
  decoding, including alpha cutouts, textured faces, atlas use, item/equipment
  models, NPC models, projectile models, UI sprites, and cache-backed fonts.
- Step 7: replaced the legacy animation export contract with `ANM2` sequence /
  frame loading for player, NPC, object, spotanim, and equipment consumers.
- Step 8: added spawn completeness validation and viewer loading of world NPC
  spawns by active region rectangle and plane.
- Step 9: made plane-aware rendering/runtime state explicit for terrain,
  objects, NPCs, ground items, projectiles, collision, interaction rejection,
  manual plane validation, and `LINK_BELOW` visual-plane terrain.
- Step 10: added static object traversal and active scene streaming for
  ladders, stairs, caves, dungeons, portals, manholes, and same-plane large
  coordinate moves.
- Step 11: added the active animation parity slice: animated materials,
  animated world objects, equipment animation, projectile/spotanim metadata,
  actor-targeted projectile travel, first impact presentation, and generated
  combat visual selection.
- Step 12: completed the generic dynamic-object interaction pass. Target
  resolution is exact-placement based, reach is validated before action,
  dispatch is keyed by placement plus option, dynamic object state is
  placement-local, active replacements can route/transport, and the viewer
  observes `rc-core` active state.
- Step 13: simplified scene exporter orchestration. `export_scene_slice.py`
  now calls explicit b237 terrain/object export APIs in-process instead of
  shelling out through wrapper scripts.
- Step 14: retired safe obsolete paths. Removed unused 317-era NPC/sprite
  viewer exporters and the object-export bridge wrappers. Shared compatibility
  helpers remain only where active exporters still import them.

## Current Status

Cache pipeline cleanup is closed for the current pass after the final
dynamic-object transport, visual replacement, linked-below terrain/object
render-level, and composite loc model export fixes.

Known non-blocking follow-up is tracked in `work.md`: minimap parity,
movement/pathfinding polish, transport edge-case testing, instance-specific
transport/chunk remapping, broader rendering/asset corrections, performance
work, audio, and combat fidelity.

Deferred outside this cleanup:

- minimap parity and correct minimap source/rendering
- movement, click-to-tile, and pathfinding polish
- transport edge-case testing across more dungeons/caves/portals
- instance-specific transport and dynamic chunk remapping
- backend/frontend performance work, including only loading/rendering active
  NPC animations and assets near the player
- remaining combat fidelity work tracked in `work.md`

## Validation

Use targeted checks while editing, then full checks before closing a step:

- `python3 tools/cache_pipeline/validate_b237_cache.py --region 50,53 --region 48,53`
- `python3 tools/cache_pipeline/rc_cache/smoke.py --synthetic --cache tools/cache_pipeline/source/current_fightcaves_demo/data/cache --require-b237-configs`
- `timeout 60 python3 tools/cache_pipeline/export_scene_slice.py --center-x 3210 --center-y 3424 --radius-regions 0 --output-prefix /tmp/runec_step13_scene --planes 0`
- `cmake --build build --target rc-viewer test_objects_runtime test_traversal_runtime`
- `ctest --test-dir build --output-on-failure`
- `timeout 10 env RC_VIEWER_QUIET=1 RC_VIEWER_EXIT_FRAMES=120 ./build/rc-viewer`
- `bash testing/run_sps_benchmark.sh`
