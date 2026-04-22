# RuneC

![RuneC - Varrock](data/header.png)

A faithful port of Old School RuneScape to C, rendered with
[Raylib](https://www.raylib.com/). RuneC is intended to be both a
playable OSRS game and a high-performance baseline for headless RL
simulation.

Currently rendering the Varrock region with terrain, buildings,
objects, NPCs with animations, and tile-based collision from the OSRS
cache. Combat engine is live with OSRS DPS formulas; encounter
subsystem dispatches 50 boss specs via a data-driven TOML pipeline.

Planning and task tracking live in `work.md`; this README stays focused
on repository scope and layout.
`AGENT_README.md` is the highest-level repo truth for architecture,
scope, system categories, and required data categories.

Design goal: the same backend should run the full playable game or a
stripped-down sim with only the required systems and content enabled.
An Inferno RL build, for example, should be able to keep movement,
combat, equipment, prayer, and Inferno encounter logic while excluding
quests, shops, music, and unrelated bosses.

## Architecture

```
rc-core/      Generic game engine (pure C, no render deps).
              Tick loop, pathfinding, combat, prayer, skills, items,
              encounter subsystem, event bus. Content-agnostic — no
              OSRS-specific code lives here.

rc-content/   OSRS-specific content modules (depends on rc-core).
              Per-boss scripts (encounters/), per-quest state machines
              (quests/), region-specific behavior (regions/). Isolated
              sim targets link only the modules they need.

rc-viewer/    Raylib frontend (depends on both).
              3D rendering, camera, input, asset loading, animation.

tools/        Python scripts for scraping + exporting game data
              from the OSRS cache, OSRS Wiki, and reference repos.

data/         Compiled binary assets + curated TOMLs.
              regions/ (per-region terrain/objects/collision),
              defs/ (NPCs, items, drops, encounters, etc.),
              curated/ (hand-authored TOMLs source-of-truth).
```

The backend exposes a simple C API — `rc_world_create_config`,
`rc_world_tick`, player input functions — and the frontend reads game
state each frame to render it. The engine/content split means the same
`rc-core` can drive the full game, an Inferno-only simulator, a
headless RL training loop, or repeatable automated correctness checks
without dragging in unrelated content.

See `rc-core/README.md`, `rc-content/README.md`, and
`rc-viewer/README.md` for the component boundaries and integration
rules.

## Current State

**World + Rendering:**
- 25-region Varrock world (320x320 tiles) with terrain, buildings, trees, objects
- 79 Varrock NPC types rendering with stand/walk animations
- Player model with idle/walk/run animations
- Click-to-move with BFS pathfinding (respects directional collision flags)
- Orbit camera with zoom, follow mode, presets

**Game Engine:**
- Subsystem-based architecture with runtime bitmask toggles (combat,
  prayer, equipment, inventory, loot, skills, quests, dialogue, shops,
  slayer, encounter)
- Combat engine with OSRS DPS formulas (melee/ranged/magic accuracy +
  max hit), protection prayers, pending-hit queue with prayer snapshot
- Encounter subsystem: 50 bosses in a TOML→binary→registry pipeline;
  event-driven lifecycle (spawn → active → finished); phase-HP
  transitions; periodic + event-driven mechanic dispatch; 6 pilot
  primitives live (Scurrius + Kalphite Queen)
- NPC wander AI, respawn, deterministic XORshift32 RNG

**Data Pipeline:**
- 55 MB wiki cache, 17 buckets, 147k+ rows scraped
- Compiled binaries under `data/defs/`: 79 NPC types + 13,020 items +
  2,871 varbits + 858 music tracks + 215 quests + 597 shops + 3,413
  recipes + 201 spells + 858 NPC drop tables + 50 encounters + 155k
  dialogue nodes + slayer tasks
- Spawn data: 24,110 world NPC spawns + 235 Varrock spawns

**Tests:** 8/8 green — `test_base_only`, `test_combat`, `test_combat_e2e`, `test_encounter`, `test_encounter_bin`, `test_encounter_prims`, `test_determinism`, `test_pathfinding`.

## Build

Use a clean out-of-tree build. `RelWithDebInfo` is the default
development profile; use `Release` when benchmarking.

```bash
cmake -S /home/joe/projects/RuneC_copy -B /tmp/runec_build \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build /tmp/runec_build -j"$(nproc)"

# Run viewer
/tmp/runec_build/rc-viewer

# Run the tracked test suite
ctest --test-dir /tmp/runec_build --output-on-failure
```

`test_collision` is a manual diagnostic binary, not part of the tracked
CTest suite.

Requires CMake 3.20+, a C11 compiler, Raylib 5.5 (prebuilt in
`lib/raylib/`), Python 3.10+ (for tools only, not runtime).

## Tools & References

**Built with:**
- [Raylib 5.5](https://www.raylib.com/) — rendering, input, windowing
- C11 / CMake — build system
- Python 3 — data-pipeline scripts

**OSRS cache & references:**
- [OpenRS2](https://archive.openrs2.org/) — OSRS cache archives (b237)
- [RuneLite](https://github.com/runelite/runelite) — cache format, collision flags, coordinate system, item/NPC/object definitions
- [RSMod](https://github.com/rsmod/rsmod) — tick processing order, BFS pathfinding, combat accuracy formulas, collision system (OSRS-accurate)
- [Void RSPS](https://github.com/GregHib/void) — skill implementations, Varrock content, object interactions (pre-2013 RS — overlap source only)
- [osrsreboxed-db](https://github.com/0xNeffarion/osrsreboxed-db) — item equipment bonuses, NPC combat stats, aggression
- [OSRS Wiki](https://oldschool.runescape.wiki/) — authoritative for all OSRS content (drop tables, mechanics, quests)
- [runescape-rl](https://github.com/jbaileydev/runescape-rl) — Fight Caves C implementation (direct ancestor of this project)

## License

This project is for educational and research purposes. OSRS content and cache data belong to Jagex Ltd.
