# RuneC Data Cleanup Remaining Plan

This is the single active tracker for unfinished data-cleanup work. Completed
implementation history lives in `data_cleanup_history.md`.

Do not add completed work back to this file. Remove an item only after the
matching report, test, runtime check, clean-clone check, or release gate proves
it is closed.

## Done Means

Remaining data cleanup is done when all of these are true:

- `runtime-data.lock` points to official runtime-data artifacts with URLs and
  checksums, and a clean clone can install them.
- The viewer is ready for manual validation across the agreed destinations,
  including non-Varrock scenes and projectile visuals.
- `python3 tools/data_pipeline.py --check` and the CTest suite pass from a clean
  checkout after documented data setup.

## Ordered Work

### 1. Official Clean-Clone Runtime-Data Release

Current state:
- The full rebuild pipeline and `tools/data_pipeline.py --check` pass with
  `release_status=publishable`.
- Local/offline setup can use generated `dist-data`.
- `runtime-data.lock` is still non-official; a clean clone cannot fetch official
  runtime-data artifacts from lockfile URLs yet.

Required work:
1. Build official runtime-data packs from the approved pipeline output.
2. Publish official artifacts and record:
   - artifact URLs;
   - pack checksums;
   - manifest checksum;
   - data version;
   - source/build metadata.
3. Update `runtime-data.lock` from draft/non-official to official.
4. Verify a clean clone can run documented setup without local-only artifacts.

Close when:
- `scripts/setup-data.sh` works from official artifact URLs in a clean clone.
- The lockfile validator accepts the official release metadata.
- Pack, loose, and manifest checksums agree.

### 2. Viewer Validation Readiness

Current blocker:
- Full Varrock startup is the baseline, but non-Varrock validation destinations
  do not have complete default visual scene coverage.
- Boss validation can render/focus a boss NPC without terrain, objects,
  destination NPC population, or projectile visuals.
- Projectile model loading is currently disabled by default to avoid eager
  startup cost.

Required work:
1. Define the required manual-validation destination set:
   - startup Varrock;
   - validation bosses;
   - dungeons reached by object transports;
   - object-transport destinations selected for the first release.
2. For each destination, export/package/load:
   - terrain;
   - object meshes;
   - atlases and texture animations;
   - object animation companions;
   - collision/active-area coverage;
   - destination NPC population.
3. Make active-area NPC/collision loading robust when visual scene loading is
   missing or delayed.
4. Restore projectile visuals through lazy loading or scoped validation
   projectile bundles instead of eagerly loading the full projectile model pack.
5. Rebuild packs and run:
   - `RUNEC_VIEWER_SMOKE=1 RUNEC_ASSET_BACKEND=pack ./build/rc-viewer`
   - `RUNEC_VIEWER_SMOKE=1 RUNEC_ASSET_BACKEND=loose ./build/rc-viewer`
6. Ask for user manual validation only after the destination coverage and
   projectile path are fixed.

Close when:
- User manually validates pack/loose startup and the agreed destination set.
- Bosses, dungeons, transports, local NPCs, terrain, objects, and projectile
  visuals render for the agreed validation scope.

### 3. Final Closure Verification

Required work:
1. Correct stale planning/docs so they match the final repo state.
2. Re-run source/provenance validators and ensure blocked private/wrong-game
   provenance is absent from active required runtime data.
3. Confirm any accepted RuneLite/RSMod/wiki evidence is documented as evidence,
   not as a local checkout dependency.
4. Split or organize the staged work so source imports, runtime code, tooling,
   generated data policy, and docs are reviewable.
5. From a clean checkout after documented data setup, run:
   - `python3 tools/data_pipeline.py --check`
   - `ctest --test-dir build --output-on-failure`
6. Confirm generated artifacts, source-controlled inputs, release artifacts, and
   ignored local installs are classified correctly.

Close when:
- The clean-checkout validation passes.
- The release gate is publishable.
- Manual viewer validation has passed for the agreed scope.

## Deferred Technical Debt

These are important but should not block the first data-cleanup release unless
we explicitly change the release bar:

- Removing every remaining `RcGameData` compatibility global.
- Moving all legacy immutable tables into `RcGameData`.
- Perfecting every boss mechanic.
- Full-world visual coverage beyond the agreed validation destinations.
- Dialogue behavior validation beyond ensuring dialogue data is not loaded by
  viewer startup unless explicitly requested.
