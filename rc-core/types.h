#ifndef RC_TYPES_H
#define RC_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include "handles.h"
#include "events.h"
#include "encounter.h"

// Limits
#define RC_MAX_NPCS         30000
#define RC_MAX_GROUND_ITEMS 512
#define RC_MAX_REGIONS      32
#define RC_MAX_PENDING_HITS 8
#define RC_MAX_ROUTE        64
#define RC_MAX_NPC_DEFS     20000
#define RC_MAX_NPC_ID       20000
#define RC_NPC_MAX_MODELS   12
#define RC_MAX_OBJECT_STATES 512
#define RC_MAX_ITEM_DEFS    65536
#define RC_MAX_VARBITS      32768
#define RC_MAX_VARPS        8192
#define RC_INVENTORY_SIZE   28
#define RC_BANK_SIZE        800
#define RC_EQUIP_COUNT      11
#define RC_MAX_COMBAT_ATTACKERS 8
#define RC_SLAYER_BLOCK_SLOTS 8
#define RC_SLAYER_PREFER_SLOTS 8

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

typedef enum {
    RC_COMBAT_CLASS_NONE = 0,
    RC_COMBAT_CLASS_MELEE,
    RC_COMBAT_CLASS_RANGED,
    RC_COMBAT_CLASS_MAGIC,
} RcCombatClass;

typedef enum {
    RC_ATTACK_TYPE_NONE = 0,
    RC_ATTACK_TYPE_STAB,
    RC_ATTACK_TYPE_SLASH,
    RC_ATTACK_TYPE_CRUSH,
    RC_ATTACK_TYPE_RANGED,
    RC_ATTACK_TYPE_MAGIC,
} RcCombatAttackType;

typedef enum {
    RC_ATTACK_STANCE_ACCURATE = 0,
    RC_ATTACK_STANCE_AGGRESSIVE,
    RC_ATTACK_STANCE_DEFENSIVE,
    RC_ATTACK_STANCE_CONTROLLED,
    RC_ATTACK_STANCE_RAPID,
    RC_ATTACK_STANCE_LONGRANGE,
    RC_ATTACK_STANCE_CAST,
    RC_ATTACK_STANCE_DEFENSIVE_CAST,
} RcAttackStance;

enum {
    RC_COMBAT_XP_ATTACK   = 1u << 0,  // SKILL_ATTACK
    RC_COMBAT_XP_DEFENCE  = 1u << 1,  // SKILL_DEFENCE
    RC_COMBAT_XP_STRENGTH = 1u << 2,  // SKILL_STRENGTH
    RC_COMBAT_XP_RANGED   = 1u << 4,  // SKILL_RANGED
    RC_COMBAT_XP_MAGIC    = 1u << 6,  // SKILL_MAGIC
};

typedef enum {
    RC_INTERACT_NONE = 0,
    RC_INTERACT_NPC = 1,
    RC_INTERACT_NPC_ATTACK = 2,
    RC_INTERACT_OBJECT = 3,
} RcInteractionType;

#define RC_INTERACTION_OPTION_TEXT_LEN 32

typedef enum {
    RC_INTERACTION_NONE = 0,
    RC_INTERACTION_NPC,
    RC_INTERACTION_OBJECT,
    RC_INTERACTION_GROUND_ITEM,
    RC_INTERACTION_INVENTORY_ITEM,
    RC_INTERACTION_EQUIPMENT_ITEM,
    RC_INTERACTION_PLAYER,
    RC_INTERACTION_WIDGET,
} RcInteractionKind;

typedef enum {
    RC_INTERACTION_OP_NONE = 0,
    RC_INTERACTION_OP1,
    RC_INTERACTION_OP2,
    RC_INTERACTION_OP3,
    RC_INTERACTION_OP4,
    RC_INTERACTION_OP5,
    RC_INTERACTION_EXAMINE,
    RC_INTERACTION_USE_ON,
    RC_INTERACTION_SPELL_ON,
    RC_INTERACTION_WIDGET_ACTION,
} RcInteractionOp;

typedef enum {
    RC_INTERACTION_FAIL_NONE = 0,
    RC_INTERACTION_FAIL_TARGET_MISSING,
    RC_INTERACTION_FAIL_TARGET_VERSION_CHANGED,
    RC_INTERACTION_FAIL_TARGET_DEAD,
    RC_INTERACTION_FAIL_OPTION_UNAVAILABLE,
    RC_INTERACTION_FAIL_NO_HANDLER,
    RC_INTERACTION_FAIL_CANNOT_REACH,
    RC_INTERACTION_FAIL_LOS_BLOCKED,
    RC_INTERACTION_FAIL_ACTOR_BUSY,
    RC_INTERACTION_FAIL_ACTOR_DEAD,
    RC_INTERACTION_FAIL_INVALID_SOURCE,
    RC_INTERACTION_FAIL_INVALID_TARGET,
    RC_INTERACTION_FAIL_CANCELLED,
} RcInteractionFailure;

enum {
    RC_INTERACTION_STARTED    = 1u << 0,
    RC_INTERACTION_MOVED      = 1u << 1,
    RC_INTERACTION_ARRIVED    = 1u << 2,
    RC_INTERACTION_INTERACTED = 1u << 3,
    RC_INTERACTION_COMPLETED  = 1u << 4,
    RC_INTERACTION_CANCELLED  = 1u << 5,
    RC_INTERACTION_AP_CALLED  = 1u << 6,
};

typedef struct {
    RcInteractionKind kind;
    int entity_uid;
    int entity_generation;
    int definition_id;
    uint64_t placement_key;
    int content_group;
    int tile_x, tile_y, plane;
    int footprint_width, footprint_height;
    int inventory_slot;
    int equipment_slot;
    int widget_id;
    int component_id;
    int ground_item_instance;
} RcInteractionTarget;

typedef struct {
    bool active;
    int source_actor_uid;
    RcInteractionOp op;
    char option_text[RC_INTERACTION_OPTION_TEXT_LEN];
    RcInteractionTarget target;
    int source_item_id;
    int source_spell_id;
    int source_widget_id;
    int source_component_id;
    int approach_range;
    int requested_move_mode;
    uint32_t dispatch_key;
    uint32_t flags;
    int ap_range;
    RcInteractionFailure last_failure;
} RcPendingInteraction;

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
enum {
    RC_HIT_SUPPRESS_ENCOUNTER_EFFECTS = 1u << 0,
};

enum {
    RC_HIT_TYPE_MISS = 0,
    RC_HIT_TYPE_NORMAL = 1,
    RC_HIT_TYPE_MAX = 2,
};

typedef struct {
    int active;
    int damage;
    int max_hit;
    int ticks_remaining;
    int apply_tick;
    int client_delay;
    int attack_style;       // RcCombatStyle
    int source_idx;         // -1 for player attacks on NPCs
    int prayer_snapshot;    // locked prayer at snapshot tick
    int prayer_lock_tick;
    uint8_t hit_type;
    uint8_t flags;
} RcPendingHit;

typedef enum {
    RC_COMBAT_ACTOR_NONE = 0,
    RC_COMBAT_ACTOR_PLAYER,
    RC_COMBAT_ACTOR_NPC,
} RcCombatActorKind;

typedef struct {
    RcCombatActorKind kind;
    int uid;
} RcCombatActorRef;

enum {
    RC_COMBAT_STATE_ACTIVE               = 1u << 0,
    RC_COMBAT_STATE_STARTED              = 1u << 1,
    RC_COMBAT_STATE_IN_RANGE             = 1u << 2,
    RC_COMBAT_STATE_WAITING_FOR_COOLDOWN = 1u << 3,
    RC_COMBAT_STATE_CANCELLED            = 1u << 4,
    RC_COMBAT_STATE_DEAD_TARGET          = 1u << 5,
    RC_COMBAT_STATE_INVALID_TARGET       = 1u << 6,
};

typedef struct {
    RcCombatActorKind kind;
    int uid;
    int definition_id;
    int generation;
    int tile_x, tile_y, plane;
    int footprint_width, footprint_height;
} RcCombatTargetRef;

typedef struct {
    int damage;
    int max_hit;
    int style;
    int source_uid;
    int timer;
    uint8_t hit_type;
    uint8_t flags;
} RcCombatRecentHit;

typedef struct {
    bool active;
    RcCombatTargetRef target;
    uint32_t flags;
    int attack_cooldown;
    int action_delay;
    int attack_range;
    int distance_to_target;
    int line_of_sight;
    int hp_current;
    int hp_max;
    RcCombatActorRef primary_attacker;
    RcCombatActorRef attackers[RC_MAX_COMBAT_ATTACKERS];
    int attacker_count;
    int under_attack_timer;
    int last_hit_timer;
    int attack_animation_timer;
    int attack_animation_id;
    int block_animation_timer;
    int selected_style_idx;
    int weapon_category;
    int attack_type;
    int combat_class;
    RcCombatStyle style;
    RcAttackStance stance;
    int xp_mask;
    int special_energy;
    bool special_pending;
    bool auto_retaliate;
    RcCombatStyle selected_npc_style;
    RcCombatStyle last_npc_style;
    int attack_count;
    int aggro_state;
    int leash_state;
    bool retaliates;
    bool in_multi_combat;
    RcPendingHit pending_hits[RC_MAX_PENDING_HITS];
    int num_pending_hits;
    RcCombatRecentHit recent_hits[4];
    int recent_hit_count;
} RcCombatActorState;

#define RC_MAX_COMBAT_PROJECTILES 64
#define RC_MAX_COMBAT_VISUAL_EVENTS 64

typedef enum {
    RC_COMBAT_VISUAL_ACTION_NONE = 0,
    RC_COMBAT_VISUAL_ACTION_ITEM = 1,
    RC_COMBAT_VISUAL_ACTION_SPELL = 2,
    RC_COMBAT_VISUAL_ACTION_NPC = 3,
    RC_COMBAT_VISUAL_ACTION_SPECIAL = 4,
} RcCombatVisualActionKind;

typedef struct {
    bool active;
    uint8_t source_kind;
    uint8_t target_kind;
    uint8_t style;
    uint8_t reserved;
    int source_uid;
    int target_uid;
    int source_x, source_y;
    int target_x, target_y;
    int plane;
    int weapon_item_id;
    int ammo_item_id;
    int spell_idx;
    int attack_anim_id;
    int launch_spotanim_id;
    int travel_spotanim_id;
    int impact_spotanim_id;
    int projectile_model_id;
    int projectile_anim_id;
    int start_tick;
    int duration_ticks;
    int impact_duration_ticks;
    int age_ticks;
    int hit_delay;
    int client_delay;
    int launch_spotanim_height;
    int impact_spotanim_height;
    int projectile_start_height;
    int projectile_end_height;
    int projectile_start_time;
    int projectile_end_time;
    int projectile_angle;
    int projectile_progress;
    int sequence_index;
    int sequence_count;
} RcCombatProjectile;

typedef struct {
    bool active;
    uint8_t source_kind;
    uint8_t target_kind;
    uint8_t style;
    uint8_t action_kind;
    int source_uid;
    int target_uid;
    int source_definition_id;
    int target_definition_id;
    int source_x, source_y;
    int target_x, target_y;
    int plane;
    int action_key_id;
    char action_key_name[64];
    int profile_kind;
    int profile_key_id;
    char profile_key_name[64];
    int selected_attack_anim_id;
    int hit_delay;
    int client_delay;
    int weapon_item_id;
    int ammo_item_id;
    int spell_idx;
    int stance_idx;
    int world_tick;
} RcCombatVisualEvent;

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
    int attack_target_def_id;
    RcCombatActorState combat;
    RcCombatStyle combat_style;
    int attack_style_idx;
    RcAttackStance attack_stance;
    int combat_xp_mask;
    int special_energy;     // 0-10000
    int special_recover_counter;
    int current_spellbook;
    int selected_spell;
    int manual_spell_cast;
    int autocast_spell;
    bool defensive_autocast;
    RcInvSlot rune_pouch[4];
    int attack_anim_timer;
    int last_hit;
    int last_hit_timer;
    int facing_entity;
    int facing_x, facing_y;
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
    int ward_of_arceuus_timer;
    int action_lock_timer;
    int action_anim_id;
    int action_anim_timer;
    int pending_traversal_active;
    int pending_traversal_tick;
    int pending_traversal_x, pending_traversal_y, pending_traversal_plane;

    // Stats & items
    RcSkills skills;
    RcInvSlot inventory[RC_INVENTORY_SIZE];
    RcInvSlot bank[RC_BANK_SIZE];
    uint8_t bank_tab[RC_BANK_SIZE];
    RcInvSlot equipment[RC_EQUIP_COUNT];
    int equipment_bonuses[14];

    // Interaction
    int interact_type;
    int interact_target;
    int interact_option;
    RcPendingInteraction interaction;
    int storage_kind;
    int storage_target;
    int storage_option;

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

    // Combat statuses. Timers are ticks; poison/venom damage is HP.
    int poison_damage;
    int poison_tick_counter;
    int venom_damage;
    int venom_tick_counter;
    int disease_tick_counter;
    int teleblock_timer;
    int freeze_timer;
    uint32_t slayer_unlocks;
    uint64_t slayer_progression_flags;
    uint32_t slayer_blocked_keys[RC_SLAYER_BLOCK_SLOTS];
    uint32_t slayer_preferred_keys[RC_SLAYER_PREFER_SLOTS];
    char slayer_boss_name[64];
    char slayer_location[64];
    int slayer_master_idx;
    int slayer_task_idx;
    int slayer_task_remaining;
    uint8_t slayer_block_count;
    uint8_t slayer_prefer_count;
    uint8_t slayer_combat_achievement_tier;
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
    RcCombatActorState combat;
    RcPendingHit pending_hits[RC_MAX_PENDING_HITS];
    int num_pending_hits;
    int facing_entity;
    int facing_x, facing_y;
    int attack_anim_timer;
    int last_hit;
    int last_hit_timer;
    bool is_dead;
    int wander_timer;
    int attack_count;
    int prev_x, prev_y;
    int poison_damage;
    int poison_tick_counter;
    bool disable_wander;
    bool force_player_max_hit;
    bool player_untargetable;
    bool active;
} RcNpc;

// Ground item
enum {
    RC_GROUND_VIS_PUBLIC = 0,
    RC_GROUND_VIS_PRIVATE = 1,
    RC_GROUND_VIS_PRIVATE_PERMANENT = 2,
};

enum {
    RC_GROUND_OWNER_NONE = -1,
    RC_GROUND_OWNER_LOCAL_PLAYER = 0,
};

typedef struct {
    int uid;
    int version;
    int item_id;
    int quantity;
    int x, y, plane;
    int owner_uid;
    int original_owner_uid;
    int reveal_timer;
    int despawn_timer;
    uint8_t visibility;
    bool active;
} RcGroundItem;

typedef struct {
    uint64_t placement_key;
    int base_obj_id;
    int active_obj_id;
    int x, y, plane;
    int active_x, active_y, active_plane;
    uint8_t base_type, base_rotation;
    uint8_t active_type, active_rotation;
    int respawn_tick;
    int revert_tick;
    int animation_id;
    int animation_timer;
    uint8_t flags;
} RcObjectState;

enum {
    RC_OBJECT_STATE_OPEN      = 1u << 0,
    RC_OBJECT_STATE_DEPLETED  = 1u << 1,
    RC_OBJECT_STATE_DYNAMIC   = 1u << 2,
};

typedef struct {
    bool active;
    int origin_x, origin_y;
    int width, height;
    int min_plane, max_plane;
    uint32_t generation;
} RcActiveArea;

struct RcWorld;
struct RcSpellDef;

typedef struct {
    int (*apply_player_damage)(const struct RcWorld *world,
                               const RcNpc *target, int damage);
    int (*apply_npc_attack_damage)(struct RcWorld *world, RcNpc *attacker,
                                   int damage);
    void (*on_npc_hit_player)(struct RcWorld *world,
                              const RcPendingHit *hit, int damage);
    RcCombatStyle (*select_npc_style)(struct RcWorld *world, RcNpc *npc,
                                      const RcPlayer *player,
                                      RcCombatStyle default_style);
    int (*npc_attack_range)(const struct RcWorld *world, const RcNpc *npc,
                            RcCombatStyle style, int default_range);
    int (*modify_npc_roll_damage)(struct RcWorld *world, RcNpc *npc,
                                  RcCombatStyle style, int damage);
    void (*after_npc_swing)(struct RcWorld *world, RcNpc *npc,
                            RcCombatStyle style);
    int (*modify_npc_attack_speed)(struct RcWorld *world, RcNpc *npc,
                                   int default_speed);
    int (*modify_incoming_damage_after_protection)(
        struct RcWorld *world, const RcPendingHit *hit, int damage);
    int (*player_special_energy_cost)(const struct RcWorld *world,
                                      const RcPlayer *player,
                                      const RcNpc *target, int weapon_id);
    int (*modify_player_special_damage)(struct RcWorld *world,
                                        const RcPlayer *player,
                                        const RcNpc *target, int weapon_id,
                                        RcCombatStyle style, int damage,
                                        int max_hit);
    int (*player_has_spell_runes)(const struct RcWorld *world,
                                  const RcPlayer *player,
                                  const struct RcSpellDef *spell);
    int (*player_consume_spell_runes)(struct RcWorld *world,
                                      RcPlayer *player,
                                      const struct RcSpellDef *spell);
} RcCombatContentHooks;

// World (top-level game state). Named struct tag so other subsystem
// headers can forward-declare it without pulling in this file.
typedef struct RcWorld {
    // Base (always present, always valid)
    RcPlayer player;
    RcNpc npcs[RC_MAX_NPCS];
    int npc_count;
    RcGroundItem ground_items[RC_MAX_GROUND_ITEMS];
    int ground_item_count;
    int next_ground_item_uid;
    RcObjectState object_states[RC_MAX_OBJECT_STATES];
    int object_state_count;
    RcEncounterEffect encounter_effects[RC_ENC_MAX_EFFECTS];
    int encounter_effect_count;
    int next_object_respawn_tick;
    int32_t varps[RC_MAX_VARPS];
    RcWorldMap map;
    RcActiveArea active_area;
    int tick;
    uint32_t rng_state;
    bool multi_combat;
    RcCombatContentHooks combat_hooks;
    RcCombatProjectile combat_projectiles[RC_MAX_COMBAT_PROJECTILES];
    int combat_projectile_count;
    RcCombatVisualEvent combat_visual_events[RC_MAX_COMBAT_VISUAL_EVENTS];
    int combat_visual_event_count;

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

    char npc_spawns_path[512];
} RcWorld;

#endif
