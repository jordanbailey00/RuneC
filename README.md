# RuneC

RuneC is a C implementation of Old School RuneScape-style game systems. It is
built to support two modes from the same backend:

- a playable local client through a Raylib viewer
- fast, deterministic headless simulation for testing, evaluation, and RL-style
  workloads

The project is currently focused on correctness and runtime foundations: cache
derived world assets, object interaction, UI/runtime state, combat, traversal,
and modular content systems.

## Current Status

RuneC can currently run a playable OSRS-style viewer slice with:

- cache-derived terrain, objects, collision, models, textures, sprites, fonts,
  animations, NPC spawns, and object traversal data
- an OSRS-style gameframe shell with inventory, equipment, combat tab, prayer,
  spellbook, chatbox, minimap/orbs, and context menu surfaces
- player/NPC rendering, equipment rendering, object animation, projectile
  foundations, and dynamic object state such as doors, gates, ladders, stairs,
  portals, caves, manholes, and same-plane transports
- modular `rc-core` gameplay systems for movement, combat, prayer, inventory,
  equipment, item actions, loot, skills, quests, dialogue, shops, storage,
  traversal, objects, regions, slayer, and encounters

The current active work is combat fidelity: broader projectile/spotanim
coverage, spellbook/autocast state, staff default behavior, special attacks,
combat presentation, and OSRS-style interaction feel.

## Repository Layout

```text
rc-core/       Headless C game engine. Tick loop, pathfinding, combat,
               inventory, equipment, objects, traversal, data loading, and
               subsystem state. No rendering or OSRS-specific content.

rc-content/    OSRS-specific content hooks. Boss scripts, quest state machines,
               and region-specific behavior that sits on top of rc-core.

rc-viewer/     Raylib frontend. Rendering, camera, input translation, UI,
               animation playback, and presentation-only state.

tools/         Python exporters and cache/data tooling. These generate compact
               runtime assets from the local b237 cache, curated data, and
               reference sources.

tests/         C runtime tests, regression tests, and benchmark helpers.

data/          Local runtime data install. User clones populate this with
               scripts/setup-data.sh; data-factory work can also use a loose
               RuneC-DB checkout here.
```

## Data Setup

The main RuneC repository intentionally does not track generated data or cache
assets. For normal use, download the release data packs:

```bash
./scripts/setup-data.sh
```

This installs:

```text
data/
  manifest.json
  packs/
    *.pak
```

By default the script downloads from the `RuneC` GitHub Release named
`data-v1`. Override with `RUNEC_DATA_VERSION`, `RUNEC_DATA_BASE_URL`, or
`RUNEC_DATA_MANIFEST_URL`.

For data-factory work, keep loose generated data in `data/` and build packs
with:

```bash
./tools/pack_runtime_data.py --dry-run --version v1
./tools/pack_runtime_data.py --version v1 --output dist-data --force
```

Runtime asset loading supports both loose files and release packs. The default
backend is `auto`: loose `data/...` files are used when present, otherwise
`data/packs/*.pak` is used. Override with `RUNEC_ASSET_BACKEND=loose` or
`RUNEC_ASSET_BACKEND=pack`.

## Build

Requirements:

- CMake 3.20+
- C11 compiler
- Python 3.10+ for exporter/tooling work
- Raylib 5.5, provided under `lib/raylib/`

Build out of tree:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j"$(nproc)"
```

Run the viewer:

```bash
./build/rc-viewer
```

Run tests:

```bash
ctest --test-dir build --output-on-failure
```

Run the SPS benchmark:

```bash
bash tests/benchmarks/run_sps_benchmark.sh
```

## Engine API

`rc-core` exposes a small C API centered on world creation, queued player
intent, and deterministic ticks:

```c
RcWorldConfig cfg = rc_preset_combat_only();
RcWorld *world = rc_world_create_config(&cfg);

rc_player_walk_to(world, 3222, 3218);
rc_world_tick(world);

rc_world_destroy(world);
```

Useful presets:

```c
rc_preset_full_game();      // all gameplay systems
rc_preset_combat_only();    // movement + combat-focused simulation
rc_preset_skilling_only();  // movement + inventory/equipment + skills
rc_preset_base_only();      // minimal movement/tick baseline
```

## Architecture Boundaries

- `rc-core` owns gameplay state and rules.
- `rc-content` owns OSRS-specific content hooks and scripts.
- `rc-viewer` owns rendering, UI presentation, camera, animation playback, and
  input-intent translation.
- `tools/` owns export-time cache/data conversion and runtime pack creation.
- `data/` is local-only runtime/data-factory output.

Gameplay rules should not live in the viewer. Cache decoding should not happen
inside the runtime tick path. Generated data should not be committed to the
main RuneC repository.

## References

RuneC uses local reference checkouts for audit and parity research. The runtime
does not call those projects.

- [OpenRS2](https://archive.openrs2.org/) for OSRS cache archives
- [RuneLite](https://github.com/runelite/runelite) for cache/client behavior
  reference
- [RSMod](https://github.com/rsmod/rsmod) for OSRS server behavior reference
- [VoidPS](https://github.com/GregHib/void) and 2011Scape for older overlap
  behavior references
- [OSRS Wiki](https://oldschool.runescape.wiki/) for content facts and
  mechanics

## License

RuneC is for educational and research purposes. Old School RuneScape content,
cache data, names, and assets belong to Jagex Ltd.
