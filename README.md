# RuneC — OSRS Ported to C with Raylib

A faithful port of Old School RuneScape to C, rendered with Raylib, using the b237 cache for assets. Backend and frontend are cleanly separated so the game simulation can run standalone for high-performance simulations, bots, or RL training.

**Starting scope: Varrock** — the city, its NPCs, shops, quests, the surrounding area, and everything a player can do there.

---

## Table of Contents

1. [Goals & Priorities](#goals--priorities) — simplicity, correctness, performance, code conventions
2. [What We Already Have](#what-we-already-have) — reusable systems from FC, lessons learned
3. [Architecture Overview](#architecture-overview) — flat directory layout, backend/frontend split
4. [Varrock Scope](#varrock-scope) — NPCs, skills, coordinates, regions
5. [System-by-System Design](#system-by-system-design) — structs and logic for each game system
6. [Asset Pipeline](#asset-pipeline) — b237 cache to binary assets to C loaders
7. [Implementation Phases](#implementation-phases) — 7 phases from setup to polish
8. [Reference Map](#reference-map) — which repo to consult (details in [references.md](references.md))
9. [Technical Decisions](#technical-decisions) — why C, Raylib, b237, fixed arrays
10. [Directory Structure](#directory-structure) — flat, code-only directories
11. [Build & Run](#build--run)

---

## Goals & Priorities

1. **Simplicity** — Flat C structs, no heap allocation per tick, minimal abstraction. If a system can be a switch statement, it's a switch statement.
2. **Correctness** — Must play identically to OSRS. Same tick order, same combat formulas, same pathfinding behavior, same prayer drain math. RuneLite and RSMod are the source of truth.
3. **Performance** — The backend must run millions of ticks per second when headless. No rendering code in the game simulation. No string operations in the hot loop. Cache-friendly data layout.

**Non-goals (for now):** Multiplayer networking, sound, full world map, minigames outside Varrock.

### Code Conventions

- **Flat directories.** `rc-core/`, `rc-viewer/`, `rc-cache/`, `tools/` contain code only — no `src/`, `include/`, or other subdirectories. Headers and source files live side by side.
- **Non-code files live in `data/`.** Assets, definitions, collision maps, sprites — all in `data/` at the project root, organized by type.
- **Short, clear file names.** `combat.c` not `rc_combat_system_impl.c`. `types.h` not `rc_core_type_definitions.h`. The directory already provides namespace.
- **No verbose code.** No wrapper functions that just call another function. No abstraction layers "for later." No enterprise patterns. A 50-line file that does one thing is better than a 500-line file that does one thing with extensibility hooks.
- **No unnecessary prefixes inside a module.** Inside `rc-core/`, files don't need an `rc_` prefix since the directory is the namespace. Use short names: `tick.c`, `combat.c`, `npc.c`, `pathfinding.c`.
- **Comments only where the logic isn't obvious.** OSRS formulas get a comment citing the source. A for loop doesn't.
- **Single-player only.** No player arrays, no session management, no login flow. One player, one world, running locally. This eliminates massive complexity that RSPS codebases carry.

---

## What We Already Have

The `runescape-rl/claude` repo built a complete Fight Caves implementation that we can learn from and port forward. Here's what's proven and reusable:

### Reusable Systems (port directly)

| System | Source | What it does | Adaptation needed |
|--------|--------|-------------|-------------------|
| **Tick loop** | `fc-core/src/fc_tick.c` | 8-phase tick execution matching OSRS GameTick order | Generalize from FC-specific to world tick |
| **Combat formulas** | `fc-core/src/fc_combat.c` | OSRS accuracy roll (`att_roll > def_roll` branch), max hit calc | Add melee/magic formulas (FC only had ranged player) |
| **Pending hit queue** | `fc_types.h` | Delayed damage with prayer snapshot locking | Direct reuse — this is correct OSRS behavior |
| **Prayer drain** | `fc-core/src/fc_prayer.c` | Counter-based drain with resistance, 1-tick flick support | Add all 29 prayers (FC only had protect 3) |
| **BFS pathfinding** | `fc-core/src/fc_pathfinding.c` | BFS on walkable grid, LOS (Bresenham), melee reach checks | Replace uint8 grid with per-tile collision flags |
| **XORshift32 RNG** | `fc-core/src/fc_rng.c` | Deterministic, fast, no state beyond uint32 | Direct reuse |
| **Raylib viewer** | `demo-env/src/viewer.c` | Camera orbit, smooth tick_frac interpolation, hitsplats, debug overlays | Extend for world streaming, UI panels |
| **Terrain loader** | `demo-env/src/fc_terrain_loader.h` | TERR binary → Raylib mesh with heightmap | Extend for multi-region loading |
| **Object loader** | `demo-env/src/fc_objects_loader.h` | OBJ2 binary → placed world objects | Direct reuse per region |
| **NPC model loader** | `demo-env/src/fc_npc_models.h` | MDL2 binary → vertex-colored models with animation | Direct reuse |
| **Animation system** | `demo-env/src/fc_anim_loader.h` | Vertex-group animation, Euler rotation, frame blending | Direct reuse |
| **Debug overlays** | `demo-env/src/fc_debug_overlay.h` | 8 toggleable overlays (collision, LOS, path, range, etc.) | Extend for new systems |

### Systems to Generalize

| System | FC version | What changes |
|--------|-----------|--------------|
| **World map** | Fixed 64×64 arena | Arbitrary region grid, multi-region loading |
| **NPC AI** | 8 hardcoded fight cave types | Data-driven NPC definitions from cache |
| **Collision** | uint8 walkable grid | Per-tile directional collision flags (walls, doors) |
| **Player state** | Fixed loadout (rune crossbow, d'hide) | Full equipment, inventory, stat system |
| **Observations** | RL-specific float tensor | Not needed for game (only if re-adding RL later) |

### Key Lessons Learned (from FC development)

- **Static arrays in threaded code cause segfaults.** All scratch buffers must be stack-local or per-state.
- **Silent collision fallback is dangerous.** FC trained on open arena when collision file wasn't found. Always fail loudly.
- **Prayer snapshot timing matters.** Prayer locks at queue tick, not resolve tick. Getting this wrong breaks combat feel.
- **NPC sizes must come from cache.** Guessing sizes (1×1 for everything) breaks pathfinding and melee reach.
- **Terrain height formula is `h * -1/16`**, not `h * -1/128`. Getting this wrong makes terrain nearly flat.
- **Smooth interpolation is mandatory.** Game ticks at 1.67 TPS equivalent, but rendering at 60 FPS. Without `tick_frac` blending, everything teleports.

---

## Architecture Overview

```
rc-core/     Game backend. Pure C, no I/O, no rendering.
             tick.c, combat.c, npc.c, pathfinding.c, etc.

rc-viewer/   Raylib frontend. Reads rc-core state, renders it.
             viewer.c, camera.h, terrain.h, ui.h, etc.

rc-cache/    Cache decoder. Reads b237 cache, produces definitions.
             cache.c, npc_defs.c, item_defs.c, obj_defs.c

tools/       Python export scripts. Run offline, produce binary assets.

data/        Game assets. Terrain, models, sprites, definitions.

lib/         Third-party libraries (raylib).
```

### Backend / Frontend Contract

The backend (`rc-core`) exposes a simple C API:

```c
// Lifecycle
RcWorld* rc_world_create(uint32_t seed);
void     rc_world_destroy(RcWorld* world);

// Tick
void     rc_world_tick(RcWorld* world);

// Player input (queued, processed next tick)
void     rc_player_walk_to(RcWorld* world, int tile_x, int tile_y);
void     rc_player_run_to(RcWorld* world, int tile_x, int tile_y);
void     rc_player_attack_npc(RcWorld* world, int npc_index);
void     rc_player_set_prayer(RcWorld* world, int prayer_id);
void     rc_player_eat(RcWorld* world, int inv_slot);
void     rc_player_drink(RcWorld* world, int inv_slot);
void     rc_player_equip(RcWorld* world, int inv_slot);
void     rc_player_unequip(RcWorld* world, int equip_slot);
void     rc_player_use_item_on_npc(RcWorld* world, int inv_slot, int npc_index);
void     rc_player_use_item_on_object(RcWorld* world, int inv_slot, int obj_id);
void     rc_player_interact_object(RcWorld* world, int obj_id, int option);
void     rc_player_interact_npc(RcWorld* world, int npc_index, int option);
void     rc_player_drop_item(RcWorld* world, int inv_slot);
void     rc_player_pickup_item(RcWorld* world, int ground_item_id);

// State read (frontend reads these, never writes)
const RcPlayer*    rc_get_player(const RcWorld* world);
const RcNpc*       rc_get_npcs(const RcWorld* world, int* count);
const RcGroundItem* rc_get_ground_items(const RcWorld* world, int* count);
const RcObject*    rc_get_objects(const RcWorld* world, int region_id);
```

The frontend never modifies game state directly. It reads the world after each tick and renders it. This means:
- The backend can run headless at millions of TPS for sims
- Multiple frontends can observe the same backend (viewer, RL agent, test harness)
- Determinism is guaranteed — same inputs produce same outputs

---

## Varrock Scope

### Why Varrock First

Varrock is the best starting area because it exercises nearly every core system:
- **Movement**: Walking around a real city with walls, doors, stairs, multi-level buildings
- **Combat**: Guards, dark wizards south of Varrock, Varrock sewers monsters
- **NPCs**: Dozens of interactive NPCs with dialogue (shopkeepers, quest givers, bankers)
- **Shops**: Varrock sword shop, Zaff's staff shop, Aubury's rune shop, general store, Thessalia's clothes
- **Skills**: Mining (SE mine), Smithing (anvils west), Cooking (range in palace), Fishing (barbarian village nearby), Woodcutting (trees everywhere)
- **Items**: Full inventory management, equipment, ground items, shops
- **Quests**: Romeo & Juliet, Demon Slayer, Shield of Arrav (partial)
- **Objects**: Doors, gates, ladders, stairs, anvils, furnace, ranges, banks
- **UI**: All core interfaces (inventory, equipment, skills, prayer, minimap, chatbox)

### Varrock World Coordinates

Based on RuneLite and RSMod reference data:
- **Varrock center**: ~(3213, 3428) world coordinates
- **Varrock Palace**: ~(3210-3230, 3460-3490)
- **Grand Exchange**: ~(3160-3170, 3480-3495)
- **Varrock east bank**: ~(3250-3257, 3418-3423)
- **Varrock west bank**: ~(3180-3185, 3433-3441)
- **Aubury's rune shop**: ~(3253, 3401)
- **Dark wizards circle**: ~(3222, 3368)
- **Varrock sewers entrance**: ~(3237, 3459) (manhole)
- **SE Varrock mine**: ~(3280-3295, 3360-3375)
- **Barbarian Village** (west): ~(3075-3095, 3415-3435)
- **Lumbridge road** (south): extends to ~(3222, 3218)

### Regions to Load

Each region is 64×64 tiles. Varrock spans roughly:
- Region (50, 53) — SW Varrock, dark wizards
- Region (50, 54) — W Varrock, west bank
- Region (51, 53) — SE Varrock, mine
- Region (51, 54) — E Varrock, east bank, palace
- Region (49, 53) — Barbarian Village (if we extend west)
- Region (50, 55) / (51, 55) — Grand Exchange area

We'll load 4-6 regions initially (~256×384 tiles of playable space).

### Varrock NPCs (initial set)

| NPC | Location | Type | Purpose |
|-----|----------|------|---------|
| Guard (Lv 21) | Patrols city | Combat | Basic melee combat |
| Dark Wizard (Lv 7/20) | South of Varrock | Combat | Magic-using enemies |
| Shopkeeper | General store | Shop | Buy/sell items |
| Zaff | Staff shop | Shop | Sells staves |
| Horvik | Armour shop | Shop | Sells armour |
| Sword shop owner | Sword shop | Shop | Sells swords |
| Thessalia | Clothes shop | Shop | Sells clothes |
| Aubury | Rune shop | Shop | Sells runes |
| Banker | East/West bank | Bank | Banking interface |
| Juliet | Varrock west | Quest | Romeo & Juliet |
| Romeo | Varrock square | Quest | Romeo & Juliet |
| Rat (Lv 1) | Sewers | Combat | Weakest enemy |
| Giant Rat (Lv 6) | Sewers | Combat | Low-level combat |
| Moss Giant (Lv 42) | Sewers | Combat | Mid-level combat |

### Varrock Skills

| Skill | Where | How |
|-------|-------|-----|
| Attack/Strength/Defence | Guards, dark wizards, sewers | Melee combat |
| Magic | Dark wizards, training on NPCs | Casting spells |
| Ranged | Guards, dark wizards | Ranged combat |
| Prayer | Bury bones from kills | Bone burying |
| Hitpoints | All combat | Passive from combat |
| Mining | SE mine (tin, copper, iron, silver) | Click rock, wait, get ore |
| Smithing | Anvils in west Varrock | Use bar on anvil |
| Cooking | Range in Varrock palace kitchen | Use raw food on range |
| Woodcutting | Trees throughout Varrock | Chop trees |
| Firemaking | Logs on ground | Use tinderbox on logs |
| Fishing | Barbarian Village (if in scope) | Click fishing spot |

---

## System-by-System Design

### 1. World / Map System

**Reference:** RSMod `GameMapDecoder.kt`, RuneLite `Scene.java`, `CollisionData.java`

The FC version used a single 64×64 uint8 walkable grid. We need a real tile-based world.

```c
// A single tile in the world
typedef struct {
    uint32_t collision_flags;   // Directional blocking (N/S/E/W/NE/NW/SE/SW)
    int16_t  height;            // Ground height
    uint8_t  underlay_id;       // Ground texture
    uint8_t  overlay_id;        // Overlay texture
    uint8_t  overlay_shape;     // Shape/rotation
    uint8_t  settings;          // Flags (bridge, roof, etc.)
} RcTile;

// A region is 64x64 tiles, 4 planes
typedef struct {
    int region_x, region_y;     // Region coordinates
    RcTile tiles[4][64][64];    // [plane][x][y]
    // Static objects placed during map load
    RcStaticObject* objects;
    int object_count;
} RcRegion;

// Collision flags (from RSMod CollisionFlag.kt)
#define COL_WALL_N          (1 << 0)
#define COL_WALL_E          (1 << 1)
#define COL_WALL_S          (1 << 2)
#define COL_WALL_W          (1 << 3)
#define COL_WALL_NE         (1 << 4)
#define COL_WALL_SE         (1 << 5)
#define COL_WALL_SW         (1 << 6)
#define COL_WALL_NW         (1 << 7)
#define COL_LOC             (1 << 8)    // Object blocking
#define COL_GROUND_DECOR    (1 << 9)
#define COL_BLOCK_WALK      (1 << 10)   // Full tile block
#define COL_BLOCK_NPC       (1 << 11)
#define COL_BLOCK_PLAYER    (1 << 12)
#define COL_PROJ_BLOCKER_N  (1 << 13)   // Projectile LOS
// ... etc for all directions

// The loaded world (multiple regions)
typedef struct {
    RcRegion regions[16];       // Up to 16 loaded regions
    int region_count;
    int base_region_x;          // Origin for local coord calculation
    int base_region_y;
} RcWorldMap;
```

**Collision checking** changes from `walkable[x][y]` to flag-based directional checks:
```c
// Can entity at (x,y) step north?
bool rc_can_move_north(RcWorldMap* map, int x, int y, int plane) {
    uint32_t flags = rc_get_flags(map, x, y, plane);
    uint32_t north_flags = rc_get_flags(map, x, y + 1, plane);
    return !(flags & COL_WALL_N) && !(north_flags & COL_BLOCK_WALK);
}
```

### 2. Movement / Pathfinding

**Reference:** RSMod `RouteFinding.kt`, RuneLite `CollisionData.java`

Port the FC BFS pathfinder but upgrade it:

```c
// Pathfinding result
typedef struct {
    int waypoints_x[64];
    int waypoints_y[64];
    int length;
    bool success;       // Reached destination
    bool alternative;   // Reached closest reachable tile
} RcRoute;

// BFS pathfinding with directional collision
RcRoute rc_find_path(
    const RcWorldMap* map,
    int start_x, int start_y,
    int dest_x, int dest_y,
    int entity_size,        // 1 for players, varies for NPCs
    int plane,
    bool allow_alternative  // Stop at nearest reachable if blocked
);
```

**Movement processing per tick** (from RSMod):
1. Player submits route request (click destination)
2. Route computed via BFS
3. Each tick, consume 1 waypoint (walk) or 2 (run)
4. Check collision flags for each step
5. Update player coordinates
6. Trigger area-based effects (multi-combat zones, etc.)

**Entity sizes matter** — a 3×3 NPC like Tok-Xil must check collision for its entire footprint when moving. This is already partially handled in FC's `fc_footprint_walkable` but needs directional flag support.

### 3. Combat System

**Reference:** RSMod `PlayerMeleeAccuracy.kt`, `PlayerRangedAccuracy.kt`, `PlayerMagicAccuracy.kt`; RuneLite `Actor.java`

FC implemented ranged-only combat. We need all three styles plus the full formula chain.

```c
// Combat styles
typedef enum {
    COMBAT_MELEE_STAB,
    COMBAT_MELEE_SLASH,
    COMBAT_MELEE_CRUSH,
    COMBAT_RANGED,
    COMBAT_MAGIC,
} RcCombatStyle;

// Attack roll calculation (same structure for all styles)
// attack_roll = effective_level * (equipment_bonus + 64)
//
// effective_level = base_level
//   + prayer_bonus (percentage)
//   + style_bonus (+8 accurate, +11 controlled for atk, etc.)
//   + void_bonus (percentage, if wearing void)
//
// Same formula for defence roll on the defender's side.
//
// Hit chance:
//   if att > def: 1 - (def+2)/(2*(att+1))
//   else:         att / (2*(def+1))
//
// Max hit (melee): (effective_str * (str_bonus + 64) + 320) / 640
// Max hit (ranged): (effective_ranged * (ranged_str + 64)) / 640
//
// Damage: if hit succeeds, random [0, max_hit] inclusive

typedef struct {
    int attack_roll;
    int defence_roll;
    float hit_chance;
    int max_hit;
} RcCombatCalc;

RcCombatCalc rc_calc_melee(const RcPlayer* attacker, const RcNpc* defender);
RcCombatCalc rc_calc_ranged(const RcPlayer* attacker, const RcNpc* defender);
RcCombatCalc rc_calc_magic(const RcPlayer* attacker, const RcNpc* defender);
// NPC vs Player variants
RcCombatCalc rc_calc_npc_melee(const RcNpc* attacker, const RcPlayer* defender);
RcCombatCalc rc_calc_npc_ranged(const RcNpc* attacker, const RcPlayer* defender);
RcCombatCalc rc_calc_npc_magic(const RcNpc* attacker, const RcPlayer* defender);
```

**Pending hit queue** — reuse FC's design directly. Queue hits with delay, lock prayer at snapshot tick, resolve on countdown.

**Attack speed** — determined by weapon. Scimitar = 4 ticks, longsword = 5, 2h sword = 7, shortbow = 4, etc.

### 4. NPC System

**Reference:** RuneLite `NPCComposition.java`, `NpcDefinition.java`; RSMod `Npc.kt`, `NpcModeProcessor`

FC hardcoded 8 NPC types. We need data-driven NPCs loaded from cache definitions.

```c
// NPC definition (loaded from cache at startup)
typedef struct {
    int id;                     // Cache NPC ID
    char name[64];
    int combat_level;
    int size;                   // Tile footprint (1-5)
    int hitpoints;
    int stats[6];               // atk, def, str, hp, rng, mag
    int attack_speed;
    int attack_style;           // melee, ranged, magic
    int attack_range;
    int max_hit;
    int attack_anim;
    int death_anim;
    int walk_anim;
    int idle_anim;
    int model_ids[8];           // 3D model IDs
    int model_count;
    char options[5][32];        // Right-click menu options
    bool aggressive;
    int aggro_range;
    int wander_range;           // How far from spawn to roam
    int respawn_ticks;          // Ticks until respawn after death
} RcNpcDef;

// Live NPC instance in the world
typedef struct {
    int def_id;                 // Index into NPC definitions table
    int uid;                    // Unique instance ID
    int x, y, plane;
    int spawn_x, spawn_y;      // Respawn location
    int current_hp;
    int attack_timer;
    int death_timer;
    int respawn_timer;
    int target_uid;             // Who this NPC is fighting (-1 = none)
    RcPendingHit pending_hits[8];
    int num_pending_hits;
    int facing_entity;          // UID of entity to face
    int facing_x, facing_y;    // Or face a tile
    int animation;              // Current animation playing
    int anim_frame;
    bool is_dead;
    // Wander state
    int wander_timer;
    // Previous position for interpolation
    int prev_x, prev_y;
} RcNpc;
```

**NPC AI per tick** (from RSMod NpcModeProcessor):
1. If dead → decrement death timer, despawn when done, start respawn timer
2. If respawn timer done → respawn at spawn coords, reset HP
3. If has target → move toward target, attack if in range
4. If aggressive and player in range → acquire target
5. If no target → wander randomly within wander_range of spawn

### 5. Items / Inventory / Equipment

**Reference:** RuneLite `ItemComposition.java`, `ItemContainer.java`, `InventoryID`; RSMod item system

```c
// Item definition (loaded from cache)
typedef struct {
    int id;
    char name[64];
    int value;                  // GE/shop price
    bool stackable;
    bool members;
    bool tradeable;
    // Equipment bonuses (if equippable)
    bool equippable;
    int equip_slot;             // Head, body, legs, weapon, shield, etc.
    int attack_stab, attack_slash, attack_crush;
    int attack_magic, attack_ranged;
    int defence_stab, defence_slash, defence_crush;
    int defence_magic, defence_ranged;
    int strength_bonus;
    int ranged_strength;
    int magic_damage;
    int prayer_bonus;
    int attack_speed;           // If weapon
    int attack_range;           // If weapon
    // Model
    int inventory_model;
    int male_model, female_model;
    // Actions
    char inventory_options[5][32];  // Right-click options
    char ground_options[5][32];
} RcItemDef;

// Inventory slot
typedef struct {
    int item_id;    // -1 = empty
    int quantity;   // For stackable items
} RcInvSlot;

// Equipment slots
typedef enum {
    EQUIP_HEAD,
    EQUIP_CAPE,
    EQUIP_AMULET,
    EQUIP_WEAPON,
    EQUIP_BODY,
    EQUIP_SHIELD,
    EQUIP_LEGS,
    EQUIP_GLOVES,
    EQUIP_BOOTS,
    EQUIP_RING,
    EQUIP_AMMO,
    EQUIP_COUNT
} RcEquipSlot;

// Player containers
#define INVENTORY_SIZE 28
#define BANK_SIZE 800

typedef struct {
    RcInvSlot inventory[INVENTORY_SIZE];
    RcInvSlot equipment[EQUIP_COUNT];
    RcInvSlot bank[BANK_SIZE];
    int bank_used;  // How many bank slots in use
} RcContainers;
```

**Equipment bonuses** are summed from all equipped items and cached. Recalculated on equip/unequip.

### 6. Skills System

**Reference:** RuneLite `Skill.java`, `Experience.java`

```c
typedef enum {
    SKILL_ATTACK,
    SKILL_DEFENCE,
    SKILL_STRENGTH,
    SKILL_HITPOINTS,
    SKILL_RANGED,
    SKILL_PRAYER,
    SKILL_MAGIC,
    SKILL_COOKING,
    SKILL_WOODCUTTING,
    SKILL_FLETCHING,
    SKILL_FISHING,
    SKILL_FIREMAKING,
    SKILL_CRAFTING,
    SKILL_SMITHING,
    SKILL_MINING,
    SKILL_HERBLORE,
    SKILL_AGILITY,
    SKILL_THIEVING,
    SKILL_SLAYER,
    SKILL_FARMING,
    SKILL_RUNECRAFT,
    SKILL_HUNTER,
    SKILL_CONSTRUCTION,
    SKILL_COUNT
} RcSkill;

// XP table (levels 1-99)
// Level N requires: floor(sum(floor(x + 300 * 2^(x/7))) / 4) for x=1..N-1
// Precomputed as a static lookup table
extern const int RC_XP_TABLE[100]; // xp_table[level] = xp needed

// Skill state per player
typedef struct {
    int xp[SKILL_COUNT];           // Current experience
    int base_level[SKILL_COUNT];   // Level from XP (1-99)
    int boosted_level[SKILL_COUNT]; // Current level with boosts/drains
} RcSkills;

// Level from XP: binary search RC_XP_TABLE
int rc_level_for_xp(int xp);

// Combat level formula:
// base = (def + hp + floor(prayer/2)) / 4
// melee = (atk + str) * 0.325
// ranged = floor(ranged * 1.5) * 0.325
// magic = floor(magic * 1.5) * 0.325
// combat = base + max(melee, ranged, magic)
int rc_combat_level(const RcSkills* skills);
```

**Skilling actions** (mining, cooking, etc.) are tick-based:
1. Player clicks resource → start action
2. Each tick: check requirements, roll success, consume input, produce output, grant XP
3. Success rate depends on level vs requirement (e.g., mining iron at level 15 is slow, at 60 is fast)

### 7. Player State

```c
typedef struct {
    // Identity
    char name[16];

    // Position
    int x, y, plane;
    int prev_x, prev_y;        // For interpolation

    // Route
    int route_x[64], route_y[64];
    int route_len, route_idx;
    bool running;

    // Combat
    int current_hp;
    int attack_timer;
    int attack_target;          // NPC uid or -1
    RcCombatStyle combat_style;
    RcPendingHit pending_hits[8];
    int num_pending_hits;

    // Prayer
    uint32_t active_prayers;    // Bitfield of active prayers
    int prayer_drain_counter;
    int current_prayer_points;  // In tenths (430 = 43.0)

    // Timers
    int food_timer;             // 3-tick eating cooldown
    int potion_timer;           // 3-tick potion cooldown
    int combo_timer;            // Karambwan combo eat

    // Stats, items, skills
    RcSkills skills;
    RcContainers containers;
    int equipment_bonuses[14];  // Cached sum of equipped item bonuses

    // Interaction state
    int interact_type;          // None, NPC, Object, GroundItem
    int interact_target;        // Target UID
    int interact_option;        // Which right-click option

    // Skilling
    int skill_action;           // Current skilling action type
    int skill_timer;            // Ticks until next action attempt
    int skill_target_x, skill_target_y; // What tile/object

    // Animation
    int animation;
    int anim_frame;
    float facing_angle;

    // HP regen (1 hp per 100 ticks by default)
    int hp_regen_counter;

    // Special attack
    int special_energy;         // 0-1000 (100.0%)
    bool special_active;

    // Auto-retaliate
    bool auto_retaliate;

    // Run energy
    int run_energy;             // 0-10000 (100.00%)
    int weight;                 // Affects drain rate
} RcPlayer;
```

### 8. Shops / Dialogue / Banking

**Shops** — simple stock arrays:
```c
typedef struct {
    char name[64];
    RcInvSlot stock[40];        // Items and quantities
    int stock_count;
    bool general_store;         // Buys anything vs specific items
} RcShop;
```

**Dialogue** — state machine with text + options:
```c
typedef enum {
    DIALOGUE_NONE,
    DIALOGUE_NPC_CHAT,          // NPC says something
    DIALOGUE_PLAYER_CHAT,       // Player says something
    DIALOGUE_OPTIONS,           // Player picks from list
    DIALOGUE_ITEM_GIVEN,        // Item received
} RcDialogueType;

typedef struct {
    RcDialogueType type;
    int npc_id;                 // For NPC chat head
    char text[256];
    char options[5][64];        // Up to 5 choices
    int option_count;
    int next_state;             // State after continue/choice
} RcDialogueNode;

// Dialogues defined as arrays of nodes with branching
```

**Banking** — swap items between inventory and bank containers. Sort, search, noted/unnoted conversion.

### 9. Quest System

**Reference:** RSMod quest scripts, void_RSPS quest content

Quests are state machines tracked by a single integer per quest (quest varp).

```c
typedef enum {
    QUEST_NOT_STARTED = 0,
    // Quest-specific states (1, 2, 3, ... N)
    // Final state = completed
} RcQuestState;

typedef struct {
    int id;
    char name[64];
    int varp_id;                // Which variable tracks this quest
    int completed_state;        // Value when finished
    // Requirements
    int required_skills[SKILL_COUNT]; // Min levels (0 = no req)
    int required_quests[8];     // Quest IDs that must be complete
    int required_quest_count;
    // Rewards
    int xp_rewards[SKILL_COUNT]; // XP granted on completion
    int item_rewards[8];        // Item IDs given
    int item_reward_count;
    int quest_points;
} RcQuestDef;

// Varrock quests to implement:
// - Romeo & Juliet (simple, dialogue + fetch quest)
// - Demon Slayer (combat quest, Silverlight sword)
// - Shield of Arrav (partial — one gang path)
```

### 10. Game Tick Order

**Reference:** RSMod `GameCycle.kt` — the authoritative tick processing order.

```c
void rc_world_tick(RcWorld* world) {
    // Phase 1: Process queued player input
    rc_process_player_input(world);

    // Phase 2: Compute player route from input
    rc_process_player_route(world);

    // Phase 3: NPC processing
    for (int i = 0; i < world->npc_count; i++) {
        RcNpc* npc = &world->npcs[i];
        rc_npc_regen(npc);              // HP regeneration
        rc_npc_timer_tick(npc);         // AI timers
        rc_npc_ai(world, npc);          // Aggro, wander, hunt
        rc_npc_movement(world, npc);    // Walk toward target
        rc_npc_combat(world, npc);      // Attack if in range
    }

    // Phase 4: Player processing
    rc_player_movement(world);          // Consume route waypoints
    rc_player_combat(world);            // Attack target if in range
    rc_player_skilling(world);          // Process active skill action
    rc_player_timers(world);            // Decrement cooldowns

    // Phase 5: Resolve pending hits (both player and NPC)
    rc_resolve_player_hits(world);
    for (int i = 0; i < world->npc_count; i++) {
        rc_resolve_npc_hits(world, &world->npcs[i]);
    }

    // Phase 6: Prayer drain
    rc_prayer_drain_tick(world);

    // Phase 7: HP/stat regeneration
    rc_player_regen(world);
    rc_player_stat_restore(world);      // Boosted stats decay toward base

    // Phase 8: Death checks, respawn timers, ground item timers
    rc_check_deaths(world);
    rc_tick_respawns(world);
    rc_tick_ground_items(world);

    world->tick++;
}
```

### 11. UI System (Frontend)

The frontend needs these UI panels, rendered as 2D overlays in Raylib:

| Panel | Contents | Priority |
|-------|----------|----------|
| **Minimap** | Top-down tile view, player dot, NPC dots | Phase 1 |
| **Inventory** | 28-slot grid, item sprites, right-click menus | Phase 1 |
| **Equipment** | Paper-doll with equipped items | Phase 1 |
| **HP/Prayer orbs** | Current/max HP and prayer with orb graphics | Phase 1 |
| **Chatbox** | Game messages, NPC dialogue, options | Phase 1 |
| **Skills tab** | 23 skills with levels and XP | Phase 2 |
| **Prayer tab** | Prayer list with toggle buttons | Phase 2 |
| **Combat tab** | Attack style selection | Phase 2 |
| **Shop interface** | Grid of items with buy/sell | Phase 2 |
| **Bank interface** | Scrollable grid of banked items | Phase 2 |
| **Right-click menu** | Context menu on entities/items | Phase 1 |
| **Overhead text** | NPC names, HP bars, hitsplats | Phase 1 (from FC) |

**UI rendering approach:** All 2D, drawn after 3D scene. Use Raylib's 2D drawing functions (DrawRectangle, DrawTexture, DrawText). Sprite-based for icons (item sprites extracted from cache).

The FC viewer already has HP bars, hitsplats, and a side panel — we extend this.

---

## Asset Pipeline

### Source

The b237 OSRS cache is at:
```
/home/joe/Downloads/cache-oldschool-live-en-b237-2026-04-01-10-45-07-openrs2#2509.tar.gz
```

Export scripts from the existing project are at:
```
/home/joe/projects/runescape-rl-reference/valo_envs/ocean/osrs/scripts/
```

### Pipeline

```
b237 cache (.dat files)
    │
    ▼
Python export scripts (offline, run once per region)
    │
    ├── Region terrain  →  .terrain (TERR binary)
    ├── Region objects  →  .objects (OBJ2 binary)
    ├── Texture atlas   →  .atlas   (ATLS binary)
    ├── NPC models      →  .models  (MDL2 binary)
    ├── Player models   →  .models  (MDL2 binary)
    ├── Animations      →  .anims   (ANIM binary)
    ├── Collision map    →  .collision (per-tile flags)
    ├── NPC definitions  →  .npcdefs (binary table)
    ├── Item definitions →  .itemdefs (binary table)
    ├── Object defs      →  .objdefs (binary table)
    └── Sprites/icons    →  .sprites (RGBA atlas)
    │
    ▼
C loaders (fread at startup, zero parsing at runtime)
```

### Binary Formats (reuse from FC)

| Format | Magic | Purpose |
|--------|-------|---------|
| TERR | `0x54455252` | Terrain mesh: vertices, colors, heightmap |
| OBJ2 | `0x4F424A32` | Placed objects: instances, models, texture coords |
| ATLS | `0x41544C53` | Texture atlas: RGBA pixel data |
| MDL2 | `0x4D444C32` | 3D models: vertices, colors, base verts, vertex skins |
| ANIM | `0x414E494D` | Animations: framebases, vertex-group transforms |

### New Formats Needed

| Format | Magic | Purpose |
|--------|-------|---------|
| NDEF | `0x4E444546` | NPC definitions: stats, anims, options |
| IDEF | `0x49444546` | Item definitions: bonuses, models, options |
| ODEF | `0x4F444546` | Object definitions: options, animations |
| COLL | `0x434F4C4C` | Collision flags: per-tile uint32 directional flags |
| SPRT | `0x53505254` | Sprite atlas: item/UI icons as RGBA |

### Coordinate Transform

OSRS and Raylib use different coordinate systems:
- **OSRS game logic**: X = east, Y = north, integer tile coords
- **OSRS cache models**: units are 1/128th of a tile
- **Raylib**: X = east, Y = up, Z = south (right-handed, Y-up)
- **Transform**: `raylib_x = osrs_x`, `raylib_y = height`, `raylib_z = -osrs_y`
- **Scale**: Cache model coords ÷ 128 = tile units

---

## Implementation Phases

### Phase 0: Project Setup & Asset Export (foundation)

- [ ] Initialize CMake project with rc-core and rc-viewer targets
- [ ] Copy proven build system from FC (CMakeLists.txt, Raylib integration)
- [ ] Extract b237 cache to working directory
- [ ] Adapt Python export scripts for Varrock regions
- [ ] Export Varrock terrain, objects, textures, collision for 4-6 regions
- [ ] Export NPC definitions for Varrock NPCs
- [ ] Export item definitions for Varrock items (weapons, armour, food, ores)
- [ ] Export NPC/player models and animations
- [ ] Export item sprites for inventory UI
- [ ] Verify all assets load in a minimal test viewer

### Phase 1: Walk Around Varrock (world + movement + camera)

- [ ] Implement RcWorldMap with multi-region tile loading
- [ ] Implement directional collision flag system
- [ ] Port BFS pathfinder from FC, upgrade for directional collision
- [ ] Implement player movement (walk + run with energy drain)
- [ ] Implement click-to-move (raycast tile under cursor → BFS path)
- [ ] Port camera system from FC viewer (orbit, zoom, follow player)
- [ ] Render Varrock terrain (multi-region TERR loading)
- [ ] Render Varrock objects (multi-region OBJ2 loading)
- [ ] Render player model with walk/run/idle animations
- [ ] Implement smooth tick_frac interpolation
- [ ] Add minimap (top-down tile rendering)
- [ ] Add debug overlays (collision, path, tile coords)
- [ ] **Milestone: walk/run around all of Varrock with working collision**

### Phase 2: Combat (NPCs + fighting)

- [ ] Implement NPC definition loading from binary
- [ ] Implement NPC spawning at defined locations
- [ ] Implement NPC AI (aggro, wander, chase, attack)
- [ ] Implement NPC rendering (MDL2 models, animations)
- [ ] Implement melee combat formulas (attack roll, defence roll, max hit)
- [ ] Implement ranged combat formulas
- [ ] Implement magic combat formulas (basic spells)
- [ ] Port pending hit queue from FC
- [ ] Implement attack speed per weapon
- [ ] Implement combat XP rewards
- [ ] Implement NPC death + loot drops (ground items)
- [ ] Implement NPC respawning
- [ ] Implement player death (respawn at spawn point, lose items)
- [ ] Implement hitsplats (port from FC viewer)
- [ ] Implement HP bars over NPCs
- [ ] Implement auto-retaliate
- [ ] Add HP/prayer orbs to UI
- [ ] **Milestone: fight guards and dark wizards with correct combat**

### Phase 3: Items & Equipment

- [ ] Implement item definition loading from binary
- [ ] Implement 28-slot inventory
- [ ] Implement inventory UI panel (item sprites, right-click menu)
- [ ] Implement equipment system (11 slots)
- [ ] Implement equipment UI panel
- [ ] Implement equip/unequip with bonus recalculation
- [ ] Implement item pickup from ground
- [ ] Implement item dropping
- [ ] Implement ground item rendering (3D models on tiles)
- [ ] Implement ground item despawn timers
- [ ] Implement eating food (sharks, lobsters, etc.)
- [ ] Implement drinking potions (prayer, combat, etc.)
- [ ] Implement right-click context menus on world entities
- [ ] **Milestone: loot items from NPCs, equip gear, eat food mid-combat**

### Phase 4: Skills

- [ ] Implement XP table and level calculation
- [ ] Implement skills tab UI
- [ ] Implement Mining (click rock → roll → get ore → XP, rock depletes + respawns)
- [ ] Implement Smithing (use bar on anvil → select item → produce + XP)
- [ ] Implement Cooking (use raw food on range → cook/burn roll → XP)
- [ ] Implement Woodcutting (chop tree → logs → XP, tree depletes + respawns)
- [ ] Implement Firemaking (use tinderbox on logs → fire → XP)
- [ ] Implement Prayer (bury bones → XP, prayer tab with all protect prayers + boosts)
- [ ] Implement stat restore (boosted stats decay toward base every 60 ticks)
- [ ] **Milestone: train skills at Varrock mine, cook in palace, smith at anvils**

### Phase 5: NPCs & Interaction

- [ ] Implement NPC dialogue system (state machine with text + choices)
- [ ] Implement shops (buy/sell interface)
- [ ] Implement banking (deposit/withdraw, bank tab UI)
- [ ] Implement NPC right-click options ("Talk-to", "Trade", "Attack", "Pickpocket")
- [ ] Implement object interactions (open door, climb ladder, climb stairs)
- [ ] Implement doors (toggle collision flags on open/close)
- [ ] Implement stairs/ladders (plane transitions)
- [ ] Wire up Varrock shopkeepers (Zaff, Horvik, Thessalia, Aubury, etc.)
- [ ] Wire up Varrock bankers
- [ ] Implement chatbox for game messages and dialogue
- [ ] **Milestone: talk to NPCs, buy from shops, use the bank**

### Phase 6: Quests & Polish

- [ ] Implement quest state tracking (varps)
- [ ] Implement quest journal UI
- [ ] Implement Romeo & Juliet quest (dialogue + fetch)
- [ ] Implement Demon Slayer quest (combat + fetch, Silverlight weapon)
- [ ] Implement run energy system (drain while running, regen while walking/standing)
- [ ] Implement special attack energy
- [ ] Implement boosted stats from potions (super attack, super strength, etc.)
- [ ] Polish UI: proper OSRS-style layout, click areas, scrolling
- [ ] Polish rendering: NPC overhead names, level indicators
- [ ] Implement Varrock sewers (underground area, plane transition via manhole)
- [ ] **Milestone: complete quests, seamless Varrock experience**

---

## Reference Map

See [references.md](references.md) for a detailed comparison of RSMod, RuneLite, and Void RSPS — what each repo actually implements, what's usable for our single-player C port, and which to consult for each system.

Quick lookup:

| System | Go to | Why |
|--------|-------|-----|
| Tick order | RSMod `GameCycle.kt` | Most accurate tick phase ordering |
| Combat formulas | RSMod `combat-formulas/` + Void `Hit.kt` | Both have exact OSRS formulas |
| Pathfinding | RSMod `RouteFinding.kt` | Production-quality BFS with collision flags |
| Cache format | RuneLite `cache/definitions/` | Actual OSRS cache opcodes and decoders |
| Skills (mining, cooking, etc.) | Void RSPS `content/skill/` | Only repo with full skill implementations |
| Varrock content | Void RSPS `data/area/misthalin/varrock/` | Complete NPC spawns, shops, objects |
| Doors/stairs/objects | Void RSPS `content/entity/obj/` | Clean door toggle, ladder, stair logic |
| Collision flags | RuneLite `CollisionDataFlag.java` | Exact OSRS flag values |
| Prayer | FC `fc_prayer.c` | Proven counter-based drain, OSRS-accurate |
| Rendering/assets | FC `demo-env/` | Proven Raylib pipeline with b237 cache |

---

## Technical Decisions

### Why C (not C++, Rust, etc.)

- FC proved C works: 2M steps/sec with 4096 parallel environments
- Raylib is a C library — no binding layer needed
- Flat structs with no constructors/destructors = trivial memset reset for sims
- No hidden allocations, no vtable overhead, no template bloat
- The entire game state fits in a few KB — cache-friendly by default

### Why Raylib (not SDL, OpenGL, Vulkan)

- FC proved Raylib works for OSRS rendering
- Single-header simplicity, builds in seconds
- Built-in 3D (camera, mesh, model, shader) and 2D (texture, text, GUI) in one API
- No complex build dependencies (just a .a file)
- Cross-platform without build system gymnastics

### Why b237 Cache

- Modern OSRS cache with all current content
- Already have working export scripts from FC project
- Binary formats proven (TERR, OBJ2, MDL2, ANIM, ATLS)
- Contains Varrock in its current OSRS state

### Backend/Frontend Split

- Backend is a pure C library with no I/O, no rendering, no OS calls
- Frontend links against backend and Raylib
- This means:
  - Headless mode for sims/bots: just link rc-core, call `rc_world_tick()` in a loop
  - Deterministic replay: record inputs, replay identically
  - RL training: wrap rc-core in PufferLib (same pattern as FC)
  - Testing: step-by-step tick assertions without a window

### Fixed-Size Arrays vs Dynamic Allocation

Following FC's pattern: all arrays are fixed-size in the state struct.

```c
#define RC_MAX_NPCS         256     // Max live NPCs in loaded regions
#define RC_MAX_GROUND_ITEMS 512     // Max ground items
#define RC_MAX_REGIONS      16      // Max loaded regions
#define RC_MAX_PENDING_HITS 8       // Per entity
#define RC_MAX_ROUTE        64      // Max pathfinding route length
```

This means:
- No malloc/free in the tick loop
- Entire state is memcpy-able for snapshots
- Fixed memory footprint, predictable cache behavior
- Trade-off: can't exceed these limits (fine for single-area scope)

### Determinism

Same as FC: XORshift32 RNG seeded at world creation. Same seed + same input sequence = identical world state. Verified via state hashing (FNV-1a over explicit fields, padding-independent).

---

## Directory Structure

Code directories contain **code only** — no nesting, no subdirectories. Non-code files live in `data/`.

```
RuneC/
├── README.md                # This file
├── references.md            # Deep comparison of reference repos
├── CMakeLists.txt           # Top-level build
│
├── rc-core/                 # Game backend (code only, ZERO render deps)
│   ├── types.h              # All core structs (World, Player, Npc, etc.)
│   ├── api.h                # Public API (create, tick, input, query)
│   ├── world.c              # World lifecycle, region loading
│   ├── tick.c               # Main tick loop (8 phases)
│   ├── combat.h / combat.c  # Combat formulas (melee, ranged, magic)
│   ├── pathfinding.h / .c   # BFS, collision checks, LOS
│   ├── npc.h / npc.c        # NPC AI, spawning, respawning
│   ├── prayer.h / prayer.c  # Prayer drain, activation
│   ├── skills.h / skills.c  # XP table, skilling actions
│   ├── items.h / items.c    # Inventory, equipment, ground items
│   ├── shops.h / shops.c    # Buy/sell logic
│   ├── dialogue.h / .c      # Dialogue state machine
│   ├── quests.h / quests.c  # Quest tracking
│   └── rng.h / rng.c        # XORshift32
│
├── rc-viewer/               # Raylib frontend (code only)
│   ├── viewer.c             # Main loop, input, rendering
│   ├── camera.h             # Camera orbit, follow, presets
│   ├── terrain.h            # TERR → Raylib mesh
│   ├── objects.h            # OBJ2 → placed objects
│   ├── models.h             # MDL2 → entity models
│   ├── anims.h              # ANIM → vertex-group animation
│   ├── ui.h                 # 2D UI panels (inventory, equipment, etc.)
│   ├── minimap.h            # Minimap rendering
│   ├── hitsplats.h          # Floating damage numbers
│   ├── debug.h              # Debug overlays
│   └── input.h              # Mouse/keyboard → game actions
│
├── rc-cache/                # Cache decoder (code only)
│   ├── cache.h / cache.c    # Read b237 cache indices
│   ├── npc_defs.h / .c      # Parse NPC definitions from cache
│   ├── item_defs.h / .c     # Parse item definitions from cache
│   └── obj_defs.h / .c      # Parse object definitions from cache
│
├── tools/                   # Asset export scripts (code only)
│   ├── export_region.py     # Export terrain + objects for a region
│   ├── export_collision.py  # Export collision flags
│   ├── export_npcdefs.py    # Export NPC definitions
│   ├── export_itemdefs.py   # Export item definitions
│   ├── export_models.py     # Export 3D models
│   ├── export_anims.py      # Export animations
│   ├── export_sprites.py    # Export item/UI sprites
│   └── export_all.sh        # Run all exports for Varrock
│
├── tests/                   # Headless tests (code only)
│   ├── test_pathfinding.c
│   ├── test_combat.c
│   ├── test_determinism.c
│   └── test_tick_order.c
│
├── data/                    # ALL non-code files (assets, definitions)
│   ├── regions/             # Per-region terrain, objects, collision
│   ├── models/              # NPC, player, item models
│   ├── anims/               # Animation data
│   ├── sprites/             # Item icons, UI sprites
│   ├── textures/            # Texture atlases
│   └── defs/                # Binary definition tables (npc, item, obj)
│
└── lib/                     # Third-party libraries
    └── raylib/              # Raylib 5.5 prebuilt
```

---

## Build & Run

```bash
# Build
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)

# Run viewer
./rc-viewer/rc_viewer

# Run headless test
./tests/test_determinism
```

### Dependencies

- **CMake 3.20+**
- **C11 compiler** (gcc or clang)
- **Raylib 5.5** (prebuilt, included in lib/raylib/)
- **Python 3.10+** (for export scripts only, not needed at runtime)

---

## Open Questions

1. **Region streaming:** Do we load all Varrock regions at startup, or stream as the player moves? For 4-6 regions, loading all at startup is simpler and fine for memory.

2. **Multi-plane:** Varrock has buildings with upper floors (plane 1+). How much of the vertical world do we support initially? Start with plane 0, add stairs/ladders in Phase 5.

3. **Grand Exchange:** The GE is in Varrock but is a complex system (persistent market). Probably out of scope for initial release — treat it as a blocked-off area.

4. **Spell system:** Magic combat needs a spell book UI and rune consumption. Could defer to after Phase 2 and just support staff-based autocast initially.

5. **Varrock boundaries:** Where exactly do we draw the "everything accessible in Varrock" line? Suggestion: everything within the Varrock city walls + the immediate surrounding area (dark wizards, Barbarian Village, SE mine, Varrock sewers).
