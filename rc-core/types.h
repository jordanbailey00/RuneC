#ifndef RC_TYPES_H
#define RC_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include "handles.h"
#include "events.h"
#include "encounter.h"

// Limits
#define RC_MAX_NPCS         256
#define RC_MAX_GROUND_ITEMS 512
#define RC_MAX_REGIONS      32
#define RC_MAX_PENDING_HITS 8
#define RC_MAX_ROUTE        64
#define RC_MAX_NPC_DEFS     512
#define RC_MAX_ITEM_DEFS    4096
#define RC_MAX_SHOPS        32
#define RC_INVENTORY_SIZE   28
#define RC_BANK_SIZE        800
#define RC_EQUIP_COUNT      11

// Tile / region constants
#define RC_REGION_SIZE      64
#define RC_MAX_PLANES       4

// Combat styles
typedef enum {
    COMBAT_NONE,
    COMBAT_MELEE_STAB,
    COMBAT_MELEE_SLASH,
    COMBAT_MELEE_CRUSH,
    COMBAT_RANGED,
    COMBAT_MAGIC,
} RcCombatStyle;

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
} RcEquipSlot;

// Skills
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

// Collision flags — EXACT values from RSMod CollisionFlag.kt / RuneLite CollisionDataFlag.java.
// These MUST match the values in the exported .cmap files.
#define COL_WALL_NW         0x1
#define COL_WALL_N          0x2
#define COL_WALL_NE         0x4
#define COL_WALL_E          0x8
#define COL_WALL_SE         0x10
#define COL_WALL_S          0x20
#define COL_WALL_SW         0x40
#define COL_WALL_W          0x80
#define COL_LOC             0x100
#define COL_GROUND_DECOR    0x40000
#define COL_BLOCK_WALK      0x200000

// Composite flags for movement checks (from RSMod).
// "BLOCK_NORTH" = what flags on a tile block ENTRY from the south (i.e., moving north into it).
#define COL_BLOCK_N   (COL_WALL_S  | COL_LOC | COL_BLOCK_WALK | COL_GROUND_DECOR)
#define COL_BLOCK_E   (COL_WALL_W  | COL_LOC | COL_BLOCK_WALK | COL_GROUND_DECOR)
#define COL_BLOCK_S   (COL_WALL_N  | COL_LOC | COL_BLOCK_WALK | COL_GROUND_DECOR)
#define COL_BLOCK_W   (COL_WALL_E  | COL_LOC | COL_BLOCK_WALK | COL_GROUND_DECOR)
#define COL_BLOCK_NE  (COL_WALL_S | COL_WALL_SW | COL_WALL_W | COL_LOC | COL_BLOCK_WALK | COL_GROUND_DECOR)
#define COL_BLOCK_NW  (COL_WALL_E | COL_WALL_SE | COL_WALL_S | COL_LOC | COL_BLOCK_WALK | COL_GROUND_DECOR)
#define COL_BLOCK_SE  (COL_WALL_NW | COL_WALL_N | COL_WALL_W | COL_LOC | COL_BLOCK_WALK | COL_GROUND_DECOR)
#define COL_BLOCK_SW  (COL_WALL_N | COL_WALL_NE | COL_WALL_E | COL_LOC | COL_BLOCK_WALK | COL_GROUND_DECOR)

#define COL_PROJ_BLOCK_FULL 0x20000

// Pending hit (delayed damage with prayer snapshot)
typedef struct {
    int active;
    int damage;
    int ticks_remaining;
    int attack_style;       // RcCombatStyle
    int source_idx;         // -1 for player attacks on NPCs
    int prayer_snapshot;    // locked prayer at snapshot tick
    int prayer_lock_tick;
} RcPendingHit;

// Inventory slot
typedef struct {
    int item_id;    // -1 = empty
    int quantity;
} RcInvSlot;

// Skill state
typedef struct {
    int xp[SKILL_COUNT];
    int base_level[SKILL_COUNT];
    int boosted_level[SKILL_COUNT];
} RcSkills;

// Tile — core keeps only collision data. Visual fields (height, underlay,
// overlay, shape, settings) belong to the viewer's terrain mesh, not the
// game-logic grid. See rc-viewer/terrain.h for visual terrain state.
typedef struct {
    uint32_t collision_flags;
} RcTile;

// Region (64x64 tiles, 4 planes)
typedef struct {
    int region_x, region_y;
    RcTile tiles[RC_MAX_PLANES][RC_REGION_SIZE][RC_REGION_SIZE];
    int loaded;
} RcRegion;

// World map (loaded regions)
typedef struct {
    RcRegion regions[RC_MAX_REGIONS];
    int region_count;
    int base_region_x, base_region_y;
} RcWorldMap;

// Pathfinding result
typedef struct {
    int waypoints_x[RC_MAX_ROUTE];
    int waypoints_y[RC_MAX_ROUTE];
    int length;
    bool success;
    bool alternative;
} RcRoute;

// Player
typedef struct {
    // Position
    int x, y, plane;
    int prev_x, prev_y;

    // Route
    int route_x[RC_MAX_ROUTE], route_y[RC_MAX_ROUTE];
    int route_len, route_idx;
    bool running;

    // Combat
    int current_hp, max_hp;
    int attack_timer;
    int attack_target;      // NPC uid or -1
    RcCombatStyle combat_style;
    RcPendingHit pending_hits[RC_MAX_PENDING_HITS];
    int num_pending_hits;

    // Prayer
    uint32_t active_prayers;
    int prayer_drain_counter;
    int current_prayer_points;

    // Timers
    int food_timer;
    int potion_timer;
    int combo_timer;

    // Stats & items
    RcSkills skills;
    RcInvSlot inventory[RC_INVENTORY_SIZE];
    RcInvSlot equipment[RC_EQUIP_COUNT];
    int equipment_bonuses[14];

    // Interaction
    int interact_type;
    int interact_target;
    int interact_option;

    // Skilling
    int skill_action;
    int skill_timer;
    int skill_target_x, skill_target_y;

    // Regen
    int hp_regen_counter;

    // Run energy
    int run_energy;         // 0-10000
    int weight;

    // Auto-retaliate
    bool auto_retaliate;
} RcPlayer;

// NPC (live instance)
typedef struct {
    int def_id;             // index into definitions table
    int uid;
    int x, y, plane;
    int spawn_x, spawn_y;
    int current_hp;
    int attack_timer;
    int death_timer;
    int respawn_timer;
    int target_uid;         // -1 = no target
    RcPendingHit pending_hits[RC_MAX_PENDING_HITS];
    int num_pending_hits;
    int facing_entity;
    int facing_x, facing_y;
    bool is_dead;
    int wander_timer;
    int prev_x, prev_y;
    bool active;
} RcNpc;

// Ground item
typedef struct {
    int item_id;
    int quantity;
    int x, y, plane;
    int despawn_timer;
    bool active;
} RcGroundItem;

// World (top-level game state). Named struct tag so other subsystem
// headers can forward-declare it without pulling in this file.
typedef struct RcWorld {
    // Base (always present, always valid)
    RcPlayer player;
    RcNpc npcs[RC_MAX_NPCS];
    int npc_count;
    RcGroundItem ground_items[RC_MAX_GROUND_ITEMS];
    int ground_item_count;
    RcWorldMap map;
    int tick;
    uint32_t rng_state;

    // Subsystem bitmask — see config.h for RC_SUB_* flags. Checked
    // only by the base tick dispatcher; subsystem code assumes its
    // subsystem is enabled if it gets called.
    uint32_t enabled;

    // Event bus — subsystems subscribe at init, fire episodically.
    // See events.h / README §7.
    RcEventBus events;

    // Encounter subsystem state (inline arena layout per README §4).
    // Only exercised when RC_SUB_ENCOUNTER is enabled.
    RcEncounterState encounter;
} RcWorld;

#endif
