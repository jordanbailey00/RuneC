# RuneC

RuneC is a C implementation of Old School RuneScape-style game systems. It is
built to support two modes from the same backend:

- a playable local client through a Raylib viewer
- fast, deterministic headless simulation for testing, evaluation, and RL-style
  workloads

The project is currently focused on correctness and runtime foundations: cache
derived world assets, object interaction, UI/runtime state, combat, traversal,
and modular content systems.

## Dependencies

The current clone-and-run path is tested on Linux.

On Ubuntu/Debian:

```bash
sudo apt update
sudo apt install git build-essential cmake python3 curl zlib1g-dev libgl1-mesa-dev
```

You also need Bash, `sha256sum`, and a working desktop/OpenGL environment to
run the Raylib viewer. Raylib itself is vendored in `lib/raylib/`, so normal
builds do not require a separate Raylib install.

## Quick Start

```bash
git clone https://github.com/jordanbailey00/RuneC.git
cd RuneC
./scripts/setup-data.sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build --parallel
./build/rc-viewer
```

`./scripts/setup-data.sh` downloads the published runtime data packs, verifies
them, and expands them into local loose files under `data/` so the viewer starts
from the fast runtime path.

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

## Repository Layout

```text
rc-core/       Headless C game engine. Tick loop, pathfinding, combat,
               inventory, equipment, objects, traversal, data loading, and
               subsystem state. No rendering or OSRS-specific content.

rc-content/    OSRS-specific content hooks. Boss scripts, quest state machines,
               and region-specific behavior that sits on top of rc-core.

rc-viewer/     Raylib frontend. Rendering, camera, input translation, UI,
               animation playback, and presentation-only state.

tools/         Python data tooling for packing, unpacking, validating, and
               exporting runtime assets.

tests/         C runtime tests, regression tests, and benchmark helpers.

data/          Local runtime data install populated by scripts/setup-data.sh.
               This directory is ignored by Git.
```

## Data Setup

RuneC does not track generated runtime data in Git. Run this once after cloning:

```bash
./scripts/setup-data.sh
```

This installs:

```text
data/
  manifest.json
  packs/
    *.pak
  defs/
  models/
  regions/
  sprites/
  fonts/
  ui/
```

By default the script downloads from the `RuneC` GitHub Release named
`data-v1`. Override with `RUNEC_DATA_VERSION`, `RUNEC_DATA_BASE_URL`, or
`RUNEC_DATA_MANIFEST_URL`. It also expands the packs into loose local runtime
files so the viewer uses the fast file-loading path. Set `RUNEC_DATA_UNPACK=0`
to keep only the manifest and packs, or `RUNEC_DATA_UNPACK_FORCE=1` to rewrite
already extracted files.

Runtime asset loading supports both loose files and release packs. The default
backend is `auto`: loose `data/...` files are used when present, otherwise
`data/packs/*.pak` is used. Override with `RUNEC_ASSET_BACKEND=loose` or
`RUNEC_ASSET_BACKEND=pack`.

## Build

Build out of tree:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build --parallel
```

Run the viewer:

```bash
./build/rc-viewer
```

Run tests:

```bash
ctest --test-dir build --output-on-failure
```

Data maintainers can rebuild release packs from loose `data/` with:

```bash
./tools/pack_runtime_data.py --dry-run --version v1
./tools/pack_runtime_data.py --version v1 --output dist-data --force
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
RuneC repository.

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
