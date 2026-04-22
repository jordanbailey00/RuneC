# Viewer Validation

This file is the manual validation procedure for `rc-viewer`.

Use it to verify:
- the viewer builds from this repo
- the viewer starts from this repo with the expected data files
- world assets load and render
- movement, pathfinding, collision, camera, HUD, and animation behave
  as expected
- the viewer does not corrupt or bypass the simulation state it is
  allowed to present

This is not a planning doc.
This is not the source of truth for engine behavior.

---

## 1. What this doc does and does not validate

`rc-viewer` is currently a visual frontend and movement/debug shell for
the Varrock slice.

Today it validates:
- startup and asset loading
- terrain/object/model/animation rendering
- player movement presentation
- NPC rendering and basic wander presentation
- collision visualization
- camera and HUD behavior

Today it does **not** validate:
- click-to-attack or player combat input
- encounter/boss mechanics inside the viewer
- prayer, inventory, equipment, consumables, shops, dialogue, or other
  UI-driven gameplay loops
- byte-level determinism from the viewer path

Those behaviors are still covered primarily by headless tests and engine
work, not by the current viewer loop.

---

## 2. Important path rule

The viewer loads data by relative path.

That means:
- the binary may live anywhere
- but you must launch it with the project root as the working
  directory

Examples in this file assume the repo root is:
`/home/joe/projects/RuneC_copy`

---

## 3. Build procedure that works from `RuneC_copy`

Do not rely on the checked-in `build/` directory in this repo copy.
It may contain a stale CMake cache copied from the original `RuneC`
root.

Use a clean out-of-tree build instead:

```bash
cmake -S /home/joe/projects/RuneC_copy \
      -B /tmp/runec_copy_viewer_validation_build \
      -DCMAKE_BUILD_TYPE=Release

cmake --build /tmp/runec_copy_viewer_validation_build --target rc-viewer
```

If `rc-viewer` does not build:
- confirm `lib/raylib/` exists in the repo
- confirm the configure step did not print the
  `Raylib not found ... skipping rc-viewer build` warning

---

## 4. Launch procedure

Run the viewer from the repo root:

```bash
cd /home/joe/projects/RuneC_copy
/tmp/runec_copy_viewer_validation_build/rc-viewer
```

Expected startup log on stderr:
- `encounter_load: loaded 50 / 50 encounters from data/defs/encounters.bin`
- `npc_anim: created ... per-def anim states`
- `Collision check: region (50,53) has ... blocked tiles`
- `Viewer ready. Player at world (3213, 3428), local (141, 164)`

If startup fails:
- missing `encounter_load` line usually means the working directory is
  wrong or `data/defs/encounters.bin` is missing
- missing terrain/object rendering usually means `data/regions/varrock.*`
  assets are missing
- crash on startup is a viewer/runtime bug, not a validation failure to
  ignore

---

## 5. Controls

Current viewer controls:
- left click: move
- right drag: orbit camera
- mouse wheel: zoom
- `W/A/S/D`: step movement input
- `R`: toggle run/walk
- `L`: lock/unlock camera target
- `C`: collision overlay
- `G`: grid overlay
- `4` / `5`: camera presets
- `Space`: pause/unpause ticks

The HUD text at the bottom of the window should match these controls.

---

## 6. Manual checks

### 6.1 Startup and asset load

On launch, confirm:
- terrain renders
- objects render
- player renders
- nearby NPCs render
- no obvious all-black scene, missing mesh storm, or immediate crash

Fail conditions:
- empty world
- only fallback cubes everywhere
- repeated asset-load spam
- startup crash

### 6.2 Camera and presentation

Check:
- right-drag rotates smoothly
- mouse wheel zooms in/out without clipping the scene into unusability
- `L` toggles between following the player and free target
- `4` and `5` swap to useful preset views

Fail conditions:
- camera snaps wildly
- lock mode does not track the player
- zoom/orbit breaks the scene

### 6.3 Player movement and route presentation

Check:
- left-click on open ground produces a route and the player follows it
- route markers appear while a route is active
- `R` changes travel speed between walk and run
- `W/A/S/D` still injects simple movement when no route is active

Fail conditions:
- clicks do nothing on reachable tiles
- route markers appear but the player does not move
- run toggle changes HUD text but not movement behavior

### 6.4 Collision and blocked-space sanity

Press `C` to enable the collision overlay.

Check:
- blocked tiles show as red overlays
- wall edges show as yellow lines
- attempts to route through obviously blocked geometry stop or reroute

Use `G` as needed to compare collision overlays against tile layout.

Fail conditions:
- collision overlay is empty in clearly built-up space
- player routes through walls/buildings
- overlay and movement behavior disagree badly

### 6.5 NPC rendering and basic world animation

Check:
- NPCs render near their expected world positions
- some NPCs visibly wander over time
- idle/walk presentation updates without obvious mesh corruption
- fallback cubes appear only for missing-model cases, not for most of
  the population

Fail conditions:
- NPCs render on the wrong plane or far from ground
- wandering NPCs slide without position change
- animation updates explode/stretch meshes

### 6.6 Pause and tick sanity

Press `Space`.

Check:
- world tick advancement visibly stops while paused
- player route progress stops while paused
- unpausing resumes cleanly

Fail conditions:
- pause only freezes rendering but world state keeps advancing
- unpause causes large jumps or instability

### 6.7 Stability smoke check

Let the viewer run for a short session while moving around Varrock and
toggling overlays.

Check:
- no crash
- no runaway console spam
- no obvious asset corruption after several minutes of camera/movement
  use

---

## 7. Current non-goals for viewer validation

Do not use `rc-viewer` today to sign off on:
- player combat input
- boss encounter runtime behavior
- encounter phase transitions
- scripted boss mechanics
- inventory/equipment/prayer/dialogue/shop UI

Those are not yet driven end-to-end by the current viewer loop.

If a future viewer pass wires `rc_world_tick`, combat input, and debug
boss spawning into the frontend, this document should grow to cover
those behaviors. Until then, do not pretend the viewer validates them.

---

## 8. Optional headless companion checks

These are not viewer checks, but they are useful companion smoke tests
for a clean build:

```bash
cmake --build /tmp/runec_copy_viewer_validation_build \
      --target test_determinism test_pathfinding

/tmp/runec_copy_viewer_validation_build/test_determinism
/tmp/runec_copy_viewer_validation_build/test_pathfinding
```

Notes:
- I verified those two pass from the clean out-of-tree build.
- Some other test binaries still make fragile assumptions about build
  directory location and should not be treated as portable smoke checks
  until their path handling is cleaned up.

---

## 9. Pass criteria

Treat viewer validation as passing when all of the following are true:
- the viewer builds from a clean out-of-tree build directory
- it launches from the repo root and loads Varrock assets successfully
- movement, camera, overlays, and basic animation behave as described
- no crash or major visual corruption occurs during a short manual pass

If any of those fail, the viewer is not validated.
