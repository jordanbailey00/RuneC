# Changelog

## 2026-04-15 — Initial Build

### Project Setup
- Created flat C project structure: `rc-core/` (game backend), `rc-viewer/` (Raylib frontend), `rc-cache/` (empty, for future cache decoder), `tools/` (Python export scripts), `tests/`, `data/` (assets), `lib/` (third-party)
- CMake build system: `rc-core` compiles as static library, `rc-viewer` links against it + Raylib, tests link against `rc-core`
- Raylib 5.5 prebuilt copied from FC project (`runescape-rl/claude/demo-env/raylib/`) into `lib/raylib/`

### rc-core — Game Backend
Created 11 source files with headers:

**types.h** — All game structs and constants:
- `RcWorld`: top-level game state (player, NPCs, ground items, world map, tick counter, RNG state)
- `RcPlayer`: position, route, combat, prayer, skills, inventory, equipment, timers
- `RcNpc`: live NPC instance with position, HP, AI state, pending hits
- `RcTile`: per-tile collision flags + height + overlay/underlay
- `RcRegion`: 64x64x4 tile grid with region coordinates
- `RcWorldMap`: up to 32 loaded regions
- `RcRoute`: BFS pathfinding result (waypoints array)
- `RcPendingHit`: delayed damage with prayer snapshot
- Collision flag constants matching OSRS exactly (from RSMod `CollisionFlag.kt` / RuneLite `CollisionDataFlag.java`):
  - Wall directions: `COL_WALL_NW` through `COL_WALL_W` (0x1-0x80)
  - `COL_LOC` (0x100), `COL_GROUND_DECOR` (0x40000), `COL_BLOCK_WALK` (0x200000)
  - Composite block flags: `COL_BLOCK_N` through `COL_BLOCK_SW` — each combines the wall flag facing the entry direction + LOC + BLOCK_WALK + GROUND_DECOR

**api.h** — Public API: `rc_world_create`, `rc_world_destroy`, `rc_world_tick`, player input functions, state queries

**world.c** — World lifecycle. `rc_world_create` allocates with `calloc`, initializes player at Varrock square (3213, 3428) with level 1 stats and 10 HP

**tick.c** — 8-phase tick loop matching RSMod `GameCycle.kt` order: (1) player input, (2) route computation, (3) NPC processing, (4) player movement/combat/skilling, (5) pending hit resolution, (6) prayer drain, (7) stat regen, (8) death checks. Most phases are stubs.

**pathfinding.c** — BFS pathfinding on 128x128 search grid with directional collision.
- `rc_get_flags(map, x, y, plane)`: converts world coords to region+local, looks up collision flags. Returns 0 for unloaded regions (walkable by default).
- `rc_can_move(map, x, y, dx, dy, plane)`: checks if size-1 entity can step in direction. Matches RSMod `routeFindSize1()` — checks DESTINATION tile for composite block flags. Cardinals check one tile; diagonals check destination + both adjacent cardinal tiles.
- `rc_find_path()`: BFS from start to dest. 128x128 grid centered on start, 8 directions, traces path backwards from end to start. Supports alternative destinations when target is unreachable.
- `rc_has_los()`: Bresenham line check for projectile blocking.

**combat.c** — Hit chance formula (OSRS-accurate): `if att > def: 1-(def+2)/(2*(att+1))`, else `att/(2*(def+1))`. Pending hit queue with `rc_queue_hit`.

**prayer.c** — Counter-based drain matching OSRS exactly. Accumulates drain rate per tick, subtracts prayer point when counter exceeds resistance (60 + 2*prayer_bonus). 1-tick flicks are free. All prayer drain rates and combat bonus percentages.

**skills.c** — Precomputed XP table for levels 1-99. `rc_level_for_xp` binary search. `rc_combat_level` formula. `rc_add_xp` with auto level-up.

**items.c** — Inventory operations (add/remove/find/free_slot). Equipment bonus recalculation summing all 14 bonus types across 11 equipment slots.

**rng.h** — XORshift32 inline. `rc_rng_next` and `rc_rng_range`.

**npc.c, shops.c, dialogue.c, quests.c** — API defined, implementations stubbed.

### rc-viewer — Raylib Frontend

**terrain.h** — Loads TERR binary format (magic 0x54455252). Parses vertex count, region count, world origin, vertex positions (float[N*3]), vertex colors (uint8[N*4]), heightmap (float grid). Computes per-triangle normals. Creates Raylib Mesh/Model. `terrain_offset()` shifts vertices to local coordinates. `terrain_height_at/avg()` for ground-level queries. Ported directly from FC `fc_terrain_loader.h`.

**objects.h** — Loads OBJ2 binary format (magic 0x4F424A32) with optional texture atlas (ATLS, magic 0x41544C53). Parses vertices, colors, texture coordinates. Normal computation identical to terrain. Atlas loaded as Raylib texture and assigned to model's diffuse material. `objects_offset()` for coordinate shifting. Ported from FC `fc_objects_loader.h`.

**models.h** — Loads MDL2 binary format (magic 0x4D444C32). Per-model: model ID, expanded vertices (float), colors (uint8), base vertices (int16 for animation), vertex skins (uint8 group labels), face indices (uint16). Vertices scaled from OSRS units to tile units (÷128) with Z negated for Raylib's right-handed coords. Ported from FC `fc_npc_models.h`.

**anims.h** — Full OSRS vertex-group animation system. Direct copy of FC `fc_anim_loader.h`. Loads ANIM binary (magic 0x414E494D) with framebases and sequences. Transform types: origin (type 0, compute vertex group centroid), translate (type 1), rotate (type 2, Euler Z-X-Y with 2048-entry fixed-point sine table), scale (type 3, 128=1.0x). `anim_apply_frame()` resets to base pose then applies per-slot transforms. `anim_update_mesh()` re-expands base verts through face indices into rendering mesh. Supports interleaved two-track animation for walk+action blending.

**collision.h** — Loads .cmap binary (magic 0x434D4150). Reads mapsquare key, extracts region_x/region_y, fills RcRegion tiles with collision flags. Regions stored in world coordinates for direct use by `rc_get_flags`.

**viewer.c** — Main application:
- Spherical orbit camera: `position = target + dist * (cos(pitch)*sin(yaw), sin(pitch), cos(pitch)*cos(yaw))`. Right-drag orbits, scroll zooms, presets 4 (overview) and 5 (tactical). L key toggles player-follow lock.
- Click-to-move: screen→world raycast intersecting ground plane, converts to world tile coordinates, runs BFS pathfinding, stores route in player struct.
- Smooth interpolation: `tick_frac` (0.0 after tick, approaches 1.0 before next) interpolates player position between game ticks for 60fps rendering from ~1.67 TPS game speed.
- Player rendering: MDL2 model with animation (idle/walk/run switching based on movement state). Falls back to blue cube if model unavailable.
- Collision overlay (C key): renders red cubes for BLOCK_WALK/LOC tiles, yellow lines for directional wall flags.
- Player coordinates: world space for pathfinding, converted to local via `LOCAL_X/LOCAL_Y` macros for rendering (subtract WORLD_ORIGIN).

### Asset Pipeline

**Cache**: OSRS b237 from OpenRS2 archive #2509, extracted to `/tmp/osrs_cache_modern/cache/`. XTEA keys from b236 archive (b237 keys not submitted; b236 keys work for same regions).

**Export scripts** (from `runescape-rl-reference/valo_envs/ocean/osrs/scripts/`):
- `export_terrain.py` → `varrock.terrain` (TERR, 608k verts, 320x320 heightmap, 10MB)
- `export_objects.py` → `varrock.objects` (OBJ2, 18.7M verts, 430MB) + `varrock.atlas` (ATLS, 2048x1792, 15MB)
- Player model/anims copied from FC project: `player.models` (MDL2, 947 tris), `player.anims` (6 sequences)

**Region coverage**: 25 regions (5x5 grid, 48-52 × 51-55), 320x320 tiles. Bounds: River Lum/wilderness (NW) to Digsite/Al Kharid (SE) to Draynor Manor (SW).

### Collision System

**tools/export_collision.py** — Custom collision exporter for b237 cache. Key design decisions and fixes:

1. **Uses reference `parse_objects_modern()` and `parse_terrain()` from `export_collision_map_modern.py`** — these are proven correct for collision marking (wall directions, occupant blocking, terrain blocking). Previous attempts to reimplement this parser had multiple bugs.

2. **b237 object definition parser (`decode_obj_defs_b237`)** — the reference `decode_modern_obj_defs` doesn't handle b237's opcodes 6 and 7 (int32 model IDs instead of uint16). This caused 4,555 object definitions to fail parsing, making them invisible to collision. The b237 parser handles all opcodes including 6 (typed models with int32 IDs), 7 (untyped models with int32 IDs), 92/93 (transform variants), 100-102 (entityOps), and 249 (params).

3. **Cache reading via `reader.read_group(5, gid)` → file 1** — same code path as `export_objects.py`. The original `export_collision_map_modern.py` used `_read_raw()` + manual XTEA decryption which broke on b237's container format (zlib decompression error). `read_group()` handles decompression natively.

4. **No blanket plane merge** — earlier versions merged ALL plane 1 collision flags into plane 0, which incorrectly blocked building interiors and open areas with roofing collision. The reference code handles plane shifting correctly per-object via the `down_heights` set (from terrain's LINK_BELOW flag), so each object's collision is placed on the correct plane without any post-processing merge.

5. **`mark_wall` argument order** — the reference function signature is `(flags, direction, height, x, y, type, impenetrable)`. An earlier bug passed `(flags, height, x, y, type, direction, impenetrable)` — swapping direction and height — causing walls to be placed on wrong planes with wrong orientations.

**Collision flag values** — stored in .cmap using OSRS's exact bit values:
- Directional walls: 0x1 (NW) through 0x80 (W)
- LOC (solid object): 0x100
- GROUND_DECOR: 0x40000
- BLOCK_WALK (terrain/water/solid): 0x200000

**Movement checking** — `rc_can_move` in `pathfinding.c` matches RSMod's `routeFindSize1()`:
- Moving north: check dest tile for `COL_BLOCK_N = WALL_SOUTH | LOC | BLOCK_WALK | GROUND_DECOR`
- Moving east: check dest tile for `COL_BLOCK_E = WALL_WEST | LOC | BLOCK_WALK | GROUND_DECOR`
- Diagonals: check dest + both adjacent cardinal tiles
- This ensures walls block from both sides (a wall between two tiles places flags on both tiles) and the pathfinder correctly routes around all obstacles.

### Tests
- `test_combat.c` — hit chance formula, pending hit queue
- `test_pathfinding.c` — BFS around blocked tile
- `test_determinism.c` — same seed = same state hash
- `test_collision.c` — loads real varrock.cmap, verifies region lookup, flag values, and rc_can_move blocking

### Documentation
- `README.md` — full project plan, architecture, system designs, implementation phases
- `references.md` — RSMod vs RuneLite vs Void RSPS comparison
- `work.md` — done/todo tracking, known issues
- `changelog.md` — this file
