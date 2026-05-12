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

data/          Separate local RuneC-DB checkout. Generated assets, generated
               data, curated DB inputs, and large source corpora live there,
               not in this repository.
```

## Data Setup

The main RuneC repository intentionally does not track generated data or cache
assets. Use the separate database repository for `data/`:

```bash
git clone https://github.com/jordanbailey00/RuneC-DB.git data
```

The local checkout used by the viewer expects `data/` to exist at the project
root. `data/` may be a nested Git repo; the parent RuneC repo ignores it.

Runtime code and exporter code stay in this repository. Generated binaries,
sprites, model files, region files, curated DB inputs, and local raw source
corpora stay in `RuneC-DB`.

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
- `tools/` owns export-time cache/data conversion.
- `data/` is produced/owned by RuneC-DB.

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
