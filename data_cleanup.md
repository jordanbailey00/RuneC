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

Current state:
- Full Varrock startup remains the baseline.
- Automated loose/pack smoke checks now verify scene assets for the current
  first-release validation set:
  - dev boss destinations: Graardor, KBD, Vorkath, Jad;
  - object/traversal smoke destinations: Edgeville dungeon, Varrock Rat Pits,
    Varrock sewer, Wilderness lever/coffin, Observatory ladder, Yanille
    railing.
- Manual validation has not passed yet.

Required work:
1. User manually validates `./build/rc-viewer` for:
   - startup Varrock;
   - boss validation teleports;
   - the object/traversal smoke destinations listed above;
   - local NPCs, terrain, objects, object animations, and projectile visuals.
2. Fix any manual-validation issues found.
3. If a failed route is outside the listed validation set, explicitly decide
   whether it is first-release scope before adding scene coverage for it.

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
