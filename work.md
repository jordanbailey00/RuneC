# Work Log

## Done

### Project setup
- CMake build: rc-core (static lib), rc-viewer (raylib binary), tests
- Flat directory structure: code-only dirs, data/ for assets, lib/ for raylib
- Raylib 5.5 prebuilt from FC project in lib/raylib/

### rc-core (game backend)
- `types.h` — RcWorld, RcPlayer, RcNpc, RcTile, RcRegion, RcWorldMap, RcRoute, RcPendingHit, collision flags
- `api.h` — create/destroy world, tick, player input, state queries
- `world.c` — world lifecycle, player init at Varrock square (3213, 3428)
- `tick.c` — 8-phase tick loop (stubs for most phases, movement works)
- `pathfinding.c` — BFS 128x128 grid, directional collision, Bresenham LOS. Unloaded regions = walkable.
- `combat.c` — hit chance formula (OSRS-accurate), pending hit queue
- `prayer.c` — counter-based drain (OSRS-accurate), all drain rates, bonuses
- `skills.c` — XP table 1-99, level_for_xp, combat level formula
- `items.c` — inventory add/remove/find, equipment bonus recalc
- `npc.c` — NPC def table, spawn, death/respawn timer
- `shops.c`, `dialogue.c`, `quests.c` — API defined, stubs

### rc-viewer (raylib frontend)
- `terrain.h` — TERR loader (vertices, colors, heightmap, normals). From FC.
- `objects.h` — OBJ2 loader (vertices, colors, texcoords) + ATLS texture atlas. From FC.
- `models.h` — MDL2 loader (vertices, colors, base verts, skins, face indices). From FC.
- `anims.h` — ANIM loader (framebases, sequences, vertex-group transforms: origin/translate/rotate/scale, interleaved two-track). Direct copy from FC fc_anim_loader.h.
- `viewer.c`:
  - Spherical orbit camera (right-drag, scroll zoom, presets, lock/follow)
  - Click-to-move (BFS pathfinding via screen→world raycast)
  - WASD continuous movement
  - Smooth tick_frac interpolation between game ticks
  - Terrain + objects rendering (backface culling disabled)
  - Player model rendering with idle/walk/run animation switching
  - Animation frame advancement (per-frame, independent of game ticks)
  - Route visualization, grid overlay, HUD

### Assets exported (data/)
- `regions/varrock.terrain` — 25 regions (5x5, 48-52 x 51-55), 608k verts, 320x320 heightmap, 10MB
- `regions/varrock.objects` — 62k placements, 18.7M verts, OBJ2 format, 430MB
- `regions/varrock.atlas` — 2048x1792 texture atlas, 15MB
- `models/player.models` — MDL2, 947 tris, 704 base verts, 56KB
- `anims/player.anims` — 6 sequences (idle/walk/run/etc), 25KB
- `anims/all.anims` — 35 sequences (all FC anims), 170KB

### World bounds
Regions (48,51)-(52,55), 320x320 tiles, world origin (3072, 3264)
- NW: River Lum at wilderness
- NE: Lumberyard at wilderness
- SE: Digsite/Al Kharid fence
- SW: Draynor Manor

### Tests
- test_combat, test_pathfinding, test_determinism — all pass

---

### Collision
- `tools/export_collision.py` — collision exporter using reference `parse_objects_modern()` and `parse_terrain()` from the proven `export_collision_map_modern.py` module. Custom `decode_obj_defs_b237()` handles b237's int32 model ID opcodes (6, 7) that the reference parser doesn't support.
- `collision.h` — loads .cmap binary into RcWorldMap regions.
- `varrock.cmap` — 25 regions, ~35k non-zero tiles on plane 0, 1.6MB
- Collision flags use exact OSRS values (0x200000 for BLOCK_WALK, 0x1-0x80 for walls, 0x100 for LOC)
- Movement checking in `rc_can_move` matches RSMod `routeFindSize1()` — checks destination tile for composite block flags
- Player uses world coordinates for pathfinding, LOCAL_X/LOCAL_Y macros for rendering
- RC_MAX_REGIONS = 32

---

## TODO (in order)

### 1. NPC models + spawning
- Export Varrock NPC models from b237 (guards, dark wizards, rats, shopkeepers)
- Get NPC spawn coords from void_rsps data/area/misthalin/varrock/varrock.npc-spawns.toml
- Render NPCs at their spawn positions with idle animations
- Basic wander AI (random walk within range of spawn point)

### 2. Combat
- Melee/ranged/magic accuracy and max hit (RSMod formulas in combat.c stubs)
- NPC aggro (dark wizards attack on sight)
- Pending hit resolution with prayer snapshot
- Hitsplats (floating damage numbers projected from 3D to 2D)
- HP bars above entities
- Death + respawn (NPC: timer then respawn at spawn; player: respawn at Varrock square)
- Combat XP rewards
- Auto-retaliate
- Attack speed per weapon

### 3. Items & equipment
- Item definitions from cache (export script needed)
- Inventory UI (28-slot grid with item sprites)
- Equipment UI (paper doll, 11 slots)
- Equip/unequip with bonus recalculation
- Ground item drops from NPC kills
- Item pickup, dropping
- Eating food, drinking potions
- Right-click context menus

### 4. Skills
- Skills tab UI (23 skills, levels, XP)
- Mining (SE Varrock mine: tin, copper, iron, silver)
- Smithing (anvils west Varrock)
- Cooking (palace kitchen range)
- Woodcutting (trees throughout)
- Firemaking (tinderbox + logs)
- Prayer (bury bones from kills)
- Fishing (Barbarian Village if in bounds)
- Resource node depletion + respawn timers

### 5. NPC interaction
- Dialogue state machine (NPC chat, player choices)
- Shops (buy/sell UI, stock, restocking)
- Banking (deposit/withdraw, bank tab)
- Object interactions (doors open/close with collision toggle, stairs/ladders plane change)
- Wire up Varrock NPCs: Zaff, Horvik, Thessalia, Aubury, shopkeepers, bankers

### 6. Quests & polish
- Quest state tracking (integer per quest)
- Romeo & Juliet, Demon Slayer
- Run energy drain/regen
- Special attack energy
- Stat boost/drain from potions
- UI polish (OSRS-style layout)
- Varrock sewers (underground plane)

---

## Known Issues / Bugs
- **Missing objects**: some objects (e.g. Barbarian Village bridge) don't render. Likely export_objects.py skips certain object types or definitions with missing model data.
- **Textures**: many objects have no texture or incorrect textures. The texture atlas maps some faces but not all. Vertex-colored objects without texcoords render as flat colors. Some terrain colors may not match OSRS.
- **Player animations not playing**: anim sequence IDs (808, 819, 824) from FC may not match the b237 player model export. Need to verify sequence-to-framebase mapping and confirm animation frames are being applied to mesh.
- **Environment animations**: objects with animations (fountains, flags, fires) are static. Need per-object animation sequences exported and applied per frame.
- **Missing objects**: some objects (e.g. Barbarian Village bridge) don't render at all. Likely export_objects.py skips certain object types or definitions with missing model data.
- **Textures**: many objects have no texture or incorrect textures. Vertex-colored faces without texcoords render as flat colors. Some terrain colors may not match OSRS.
- **430MB objects file**: loads fine on RTX 5070 Ti. May need per-region splitting for lower-end hardware.
- **BFS search grid**: 128x128 tiles centered on start — long-distance clicks beyond 64 tiles won't pathfind.
- **Collision coverage**: plane 1→0 merge catches most building walls. Some edge cases (diagonal walls, ground decorations with blocking) may still be missed. mark_wall handles types 0-3,9; mark_occupant handles 10-11.
