![RuneC Varrock overview](assets/readme/varrock-overview.png)

# RuneC

RuneC is a local C implementation of Old School RuneScape-style gameplay. It
runs as a playable desktop viewer, and the same engine runs headless for fast,
deterministic simulation. No server is needed.

RuneC is in active development. Many systems and bosses work, but combat,
content and item behavior are not at full OSRS parity yet.

## Versions

| Branch | OSRS cache | Status |
| --- | --- | --- |
| `v4.0` | b237 (OpenRS2 #2528) | Stable; all data comes from one release |
| `v5.0` (this branch) | b241 (OpenRS2 #2746) | In progress; adds Chambers of Xeric |

The same branches are published on
[RuneC](https://github.com/jordanbailey00/RuneC) and
[RuneC-Testing](https://github.com/jordanbailey00/RuneC-Testing).

On v5.0 the map data (terrain, objects, collision, minimaps) is b241. Items,
NPCs and animations are still b237. There is no b241 data release yet, so
setup installs the b237 release and then exports the b241 maps from the cache
on your machine.

## Requirements

Tested on Linux. On Ubuntu or Debian:

```bash
sudo apt install git build-essential cmake python3 curl zlib1g-dev libgl1-mesa-dev
```

You also need a desktop with OpenGL. Raylib is included in `lib/raylib/`.
The data download is about 3.7 GiB and unpacks to about 29 GiB.

## Quick Start

Clone, install the base data and build:

```bash
git clone --branch v5.0 https://github.com/jordanbailey00/RuneC.git
cd RuneC
RUNEC_DATA_MANIFEST_URL=https://github.com/jordanbailey00/RuneC-Testing/releases/download/data-v2026.10.22-b237/manifest.json ./scripts/setup-data.sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build --parallel
```

Then download cache #2746 from [OpenRS2](https://archive.openrs2.org/), unzip
it, and export the b241 maps over the base install. Point `B241` at the folder
that holds `main_file_cache.dat2`. The region export takes about 20 minutes.

```bash
export B241=/path/to/b241/cache
python3 tools/export_object_defs.py --cache "$B241"
python3 tools/export_object_placements.py --cache "$B241"
python3 tools/export_collision_tiles.py --cache "$B241"
RUNEC_B237_CACHE="$B241" python3 tools/data_pipeline.py export-regions --full
python3 tools/cache_pipeline/export_spawn_objects.py --cache "$B241"
python3 tools/data_pipeline.py export-content
python3 tools/cache_pipeline/export_animations.py --modern-cache "$B241" \
  --object-anims data/regions data/models/spawn_objects.oanim \
  --output data/anims/object.anims
python3 tools/cache_pipeline/export_animations.py --modern-cache "$B241" \
  --spotanims data/defs/spotanims.bin \
  --object-anims data/regions data/models/spawn_objects.oanim \
  --item-render-map data/models/item_render.map \
  --combat-visuals data/defs/combat_visuals.tsv \
  --npc-defs data/defs/npc_defs.bin \
  --npc-attack-anims data/defs/npc_attack_anims.tsv \
  --output data/anims/all.anims
```

Run the game:

```bash
./build/rc-viewer
```

The setup script checks every file against `runtime-data.lock` and installs
into `data/`, which Git ignores. To upgrade, move the old `data/` folder aside
first; packs from different releases can't be mixed.

## Playing

- Left-click to walk or to use the action shown in the top-left corner.
- Right-click for the options menu; hold the right button and drag to move
  the camera.
- The interface follows the OSRS fixed layout: inventory, equipment, prayer,
  spellbook, combat styles, chat, minimap and orbs.

![RuneC validation tools](assets/readme/validation-tools.png)

The Clan Chat tab holds a testing panel:

- `Follow` and `0`–`3` choose which floor the camera shows.
- `Varrock`, `Graardor`, `KBD`, `KQ`, `Chaos Ele`, `Vorkath` and `Jad`
  teleport you to that spot. Vorkath starts asleep; Poke it to begin.
- `CoX` takes you to the Chambers of Xeric steps. The raid is solo, with one
  fixed layout: Tightrope, Jewelled crabs, Lizardman shamans, Muttadiles,
  Tekton, then the Great Olm. There is no loot or Challenge Mode yet.
- `ToB`, `ToA` and `Archive` only take you into those raids' maps; their
  bosses and puzzles aren't implemented.

The Varrock bank is stocked with test gear in Ranged, Mage, Melee, PvP and
Special tabs, and a combat dummy stands nearby.

![RuneC validation bank](assets/readme/validation-bank.png)

To turn the testing tools off, or start somewhere else:

```bash
RUNEC_DEV_VALIDATION=0 ./build/rc-viewer
RUNEC_DEV_BANK_DUMMY=0 ./build/rc-viewer
RUNEC_DEV_TRANSPORT_DEST=graardor ./build/rc-viewer
```

## Tests and Benchmarks

```bash
ctest --test-dir build --output-on-failure
bash tests/benchmarks/run_sps_benchmark.sh
```

The benchmark measures headless simulation steps per second.

## Headless Use

The engine in `rc-core` has a small C API:

```c
RcWorldConfig cfg = rc_preset_combat_only();
RcWorld *world = rc_world_create_config(&cfg);
rc_player_walk_to(world, 3222, 3218);
rc_world_tick(world);
rc_world_destroy(world);
```

Other presets: `rc_preset_full_game()`, `rc_preset_skilling_only()` and
`rc_preset_base_only()`.

## Repository Layout

```text
rc-core/     Game engine: ticks, movement, combat, items, objects. No rendering.
rc-content/  OSRS-specific content: bosses, raids, quests, area scripts.
rc-viewer/   Raylib frontend: rendering, camera, input and UI.
tools/       Python tools that export cache data and build data packs.
tests/       Tests and benchmarks.
data/        Your local data install (not in Git).
```

Game rules live in `rc-core` and `rc-content`; the viewer only draws and
sends input.

## References

- [OpenRS2](https://archive.openrs2.org/) for OSRS caches
- [RuneLite](https://github.com/runelite/runelite) for client behavior
- [RSMod](https://github.com/rsmod/rsmod) for server behavior
- [OSRS Wiki](https://oldschool.runescape.wiki/) for game facts

## License

RuneC is for educational and research purposes. Old School RuneScape content,
cache data, names and assets belong to Jagex Ltd.
