#ifndef RC_TYPES_H
#define RC_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include "coordinates.h"
#include "config.h"
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
#define RC_MAX_PLAYER_COMMANDS 32

typedef enum {
    RC_PLAYER_COMMAND_NONE = 0,
    RC_PLAYER_COMMAND_WALK_TO,
    RC_PLAYER_COMMAND_RUN_TO,
    RC_PLAYER_COMMAND_STEP,
    RC_PLAYER_COMMAND_SET_RUNNING,
    RC_PLAYER_COMMAND_ATTACK_NPC,
    RC_PLAYER_COMMAND_SET_ATTACK_STYLE,
    RC_PLAYER_COMMAND_SET_AUTO_RETALIATE,
    RC_PLAYER_COMMAND_TOGGLE_SPECIAL,
    RC_PLAYER_COMMAND_SET_PRAYER,
    RC_PLAYER_COMMAND_SET_SPELLBOOK,
    RC_PLAYER_COMMAND_SELECT_SPELL,
    RC_PLAYER_COMMAND_SET_AUTOCAST,
    RC_PLAYER_COMMAND_MOVE_INVENTORY,
    RC_PLAYER_COMMAND_EQUIP,
    RC_PLAYER_COMMAND_UNEQUIP,
    RC_PLAYER_COMMAND_INTERACT_NPC,
    RC_PLAYER_COMMAND_INTERACT_OBJECT,
    RC_PLAYER_COMMAND_INTERACT_INVENTORY,
    RC_PLAYER_COMMAND_INTERACT_EQUIPMENT,
    RC_PLAYER_COMMAND_WIDGET_ACTION,
    RC_PLAYER_COMMAND_USE_ITEM_ON_NPC,
    RC_PLAYER_COMMAND_USE_ITEM_ON_ITEM,
    RC_PLAYER_COMMAND_USE_ITEM_ON_OBJECT,
    RC_PLAYER_COMMAND_USE_ITEM_ON_GROUND,
    RC_PLAYER_COMMAND_USE_ITEM_ON_WIDGET,
    RC_PLAYER_COMMAND_CAST_ON_NPC,
    RC_PLAYER_COMMAND_CAST_ON_ITEM,
    RC_PLAYER_COMMAND_CAST_ON_OBJECT,
    RC_PLAYER_COMMAND_CAST_ON_GROUND,
    RC_PLAYER_COMMAND_CAST_ON_WIDGET,
    RC_PLAYER_COMMAND_CLOSE_STORAGE,
    RC_PLAYER_COMMAND_BANK_DEPOSIT,
    RC_PLAYER_COMMAND_BANK_DEPOSIT_ALL,
    RC_PLAYER_COMMAND_BANK_WITHDRAW,
    RC_PLAYER_COMMAND_APPLY_RECIPE,
    RC_PLAYER_COMMAND_DROP_ITEM,
    RC_PLAYER_COMMAND_PICKUP_ITEM,
    RC_PLAYER_COMMAND_EXAMINE_NPC,
    RC_PLAYER_COMMAND_EXAMINE_OBJECT,
    RC_PLAYER_COMMAND_EXAMINE_INVENTORY,
    RC_PLAYER_COMMAND_EXAMINE_EQUIPMENT,
    RC_PLAYER_COMMAND_EXAMINE_GROUND_ITEM,
    RC_PLAYER_COMMAND_SELECT_QUICK_PRAYER,
    RC_PLAYER_COMMAND_TOGGLE_QUICK_PRAYERS,
    RC_PLAYER_COMMAND_CONSUME,
} RcPlayerCommandKind;

typedef enum {
    RC_PRAYER_OK = 0,
    RC_PRAYER_QUEUED,
    RC_PRAYER_INVALID,
    RC_PRAYER_DISABLED,
    RC_PRAYER_DEAD,
    RC_PRAYER_EMPTY,
    RC_PRAYER_LEVEL,
    RC_PRAYER_DEFENCE,
    RC_PRAYER_LOCKED,
    RC_PRAYER_MEMBERS,
    RC_PRAYER_UNSUPPORTED,
    RC_PRAYER_MISSING_DATA,
    RC_PRAYER_NO_SELECTION,
    RC_PRAYER_REPLACED,
    RC_PRAYER_CONFLICT,
    RC_PRAYER_QUEUE_REJECTED,
    RC_PRAYER_DEPLETED,
    RC_PRAYER_FORCED_OFF,
} RcPrayerResult;

typedef struct {
    uint64_t sequence;
    RcPrayerResult code;
    int prayer_id;
} RcPrayerOutcome;

typedef enum {
    RC_ACTION_CATEGORY_SOFT = 0,
    RC_ACTION_CATEGORY_BACKGROUND,
    RC_ACTION_CATEGORY_NORMAL,
    RC_ACTION_CATEGORY_STRONG,
} RcActionCategory;

typedef enum {
    RC_COMMAND_RESULT_NONE = 0,
    RC_COMMAND_RESULT_QUEUED,
    RC_COMMAND_RESULT_EXECUTED,
    RC_COMMAND_RESULT_REJECTED_INVALID,
    RC_COMMAND_RESULT_REJECTED_FULL,
    RC_COMMAND_RESULT_REJECTED_DEAD,
    RC_COMMAND_RESULT_REJECTED_BUSY,
    RC_COMMAND_RESULT_REJECTED_CANCELLED,
} RcPlayerCommandResult;

typedef enum {
    RC_ACTION_OWNER_NONE = 0,
    RC_ACTION_OWNER_MOVEMENT,
    RC_ACTION_OWNER_INTERACTION,
    RC_ACTION_OWNER_COMBAT,
    RC_ACTION_OWNER_TRAVERSAL,
    RC_ACTION_OWNER_SKILL,
    RC_ACTION_OWNER_MODAL,
} RcPlayerActionOwner;

typedef enum {
    RC_ACTION_CANCEL_NONE = 0,
    RC_ACTION_CANCEL_REPLACED,
    RC_ACTION_CANCEL_DEATH,
    RC_ACTION_CANCEL_RELOCATED,
    RC_ACTION_CANCEL_FRONTEND,
} RcPlayerActionCancelReason;

typedef struct {
    uint8_t kind;
    uint8_t category;
    uint64_t sequence;
    int args[8];
    uint64_t key;
} RcPlayerCommand;

typedef struct {
    uint8_t count;
    uint64_t next_sequence;
    uint64_t last_sequence;
    RcPlayerCommandResult last_result;
    uint64_t rejected_count;
} RcPlayerCommandQueue;

typedef struct {
    bool active;
    RcPlayerActionOwner owner;
    RcActionCategory category;
    RcTick started_tick;
    RcTick ready_tick;
    RcPlayerActionCancelReason last_cancel_reason;
} RcPlayerActionState;

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
#define RC_INTERACTION_RESULT_MESSAGE_LEN 96

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
    uint32_t item_generation;
    int widget_id;
    int component_id;
    int ground_item_instance;
} RcInteractionTarget;

typedef struct {
    bool active;
    uint64_t generation;
    int source_actor_uid;
    RcInteractionOp op;
    char option_text[RC_INTERACTION_OPTION_TEXT_LEN];
    RcInteractionTarget target;
    int source_item_id;
    int source_inventory_slot;
    uint32_t source_item_generation;
    int source_spell_id;
    int source_widget_id;
    int source_component_id;
    int approach_range;
    RcTick ready_tick;
    uint32_t flags;
    RcInteractionFailure last_failure;
} RcPendingInteraction;

typedef struct {
    uint64_t sequence;
    uint64_t interaction_generation;
    int code;
    RcInteractionFailure failure;
    char message[RC_INTERACTION_RESULT_MESSAGE_LEN];
} RcInteractionOutcome;

typedef enum {
    RC_TRAVERSAL_POLICY_EXACT = 1,
    RC_TRAVERSAL_POLICY_RELATIVE,
    RC_TRAVERSAL_POLICY_RANDOM_AREA,
    RC_TRAVERSAL_POLICY_CHOICE,
    RC_TRAVERSAL_POLICY_CONTEXTUAL_RETURN,
    RC_TRAVERSAL_POLICY_INSTANCE_MAPPED,
} RcTraversalDestinationPolicy;

typedef enum {
    RC_TRAVERSAL_PRESENTATION_INSTANT = 0,
    RC_TRAVERSAL_PRESENTATION_CLIMB,
    RC_TRAVERSAL_PRESENTATION_TELEPORT,
    RC_TRAVERSAL_PRESENTATION_EXACT_MOVE,
} RcTraversalPresentation;

typedef enum {
    RC_TRAVERSAL_PHASE_NONE = 0,
    RC_TRAVERSAL_PHASE_APPROACH,
    RC_TRAVERSAL_PHASE_TAKEOFF,
    RC_TRAVERSAL_PHASE_TRANSIT,
    RC_TRAVERSAL_PHASE_RELOCATE,
    RC_TRAVERSAL_PHASE_LANDING,
} RcTraversalPhase;

typedef enum {
    RC_TRAVERSAL_RESULT_NONE = 0,
    RC_TRAVERSAL_RESULT_SUCCESS,
    RC_TRAVERSAL_RESULT_CANCELLED,
    RC_TRAVERSAL_RESULT_INVALID_SOURCE,
    RC_TRAVERSAL_RESULT_INVALID_DESTINATION,
    RC_TRAVERSAL_RESULT_DESTINATION_UNAVAILABLE,
    RC_TRAVERSAL_RESULT_UNSUPPORTED,
} RcTraversalResultCode;

typedef struct {
    bool active;
    uint64_t generation;
    uint64_t interaction_generation;
    int source_kind;
    int source_id;
    int source_option;
    uint32_t source_flags;
    uint64_t source_key;
    int source_x, source_y, source_plane;
    int approach_x, approach_y, approach_plane;
    RcTraversalDestinationPolicy policy;
    RcTraversalPresentation presentation;
    RcTraversalPhase phase;
    int destination_x, destination_y, destination_plane;
    int facing_x, facing_y;
    bool has_facing;
    RcTick ready_tick;
    int takeoff_ticks;
    int transit_ticks;
    int landing_ticks;
} RcTraversalState;

typedef struct {
    uint64_t sequence;
    uint64_t generation;
    uint64_t interaction_generation;
    RcTraversalPhase phase;
    RcTraversalPresentation presentation;
    int source_kind;
    int source_id;
    int source_option;
    uint64_t source_key;
    int source_x, source_y, source_plane;
    int approach_x, approach_y, approach_plane;
    int destination_x, destination_y, destination_plane;
    int facing_x, facing_y;
    bool has_facing;
    int takeoff_ticks, transit_ticks, landing_ticks;
} RcTraversalEvent;

typedef struct {
    uint64_t sequence;
    uint64_t generation;
    RcTraversalResultCode code;
    RcPlayerActionCancelReason cancel_reason;
    int source_kind;
    int source_id;
    int destination_x, destination_y, destination_plane;
    char message[RC_INTERACTION_RESULT_MESSAGE_LEN];
} RcTraversalOutcome;

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
#define COL_PROJ_WALL_NW    0x200
#define COL_PROJ_WALL_N     0x400
#define COL_PROJ_WALL_NE    0x800
#define COL_PROJ_WALL_E     0x1000
#define COL_PROJ_WALL_SE    0x2000
#define COL_PROJ_WALL_S     0x4000
#define COL_PROJ_WALL_SW    0x8000
#define COL_PROJ_WALL_W     0x10000
#define COL_PROJ_BLOCK_FULL 0x20000
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

#define COL_BLOCK_N_EW (COL_WALL_NW | COL_WALL_N | COL_WALL_NE | COL_WALL_E | COL_WALL_W | COL_LOC | COL_BLOCK_WALK | COL_GROUND_DECOR)
#define COL_BLOCK_S_EW (COL_WALL_E | COL_WALL_SE | COL_WALL_S | COL_WALL_SW | COL_WALL_W | COL_LOC | COL_BLOCK_WALK | COL_GROUND_DECOR)
#define COL_BLOCK_E_NS (COL_WALL_NW | COL_WALL_N | COL_WALL_S | COL_WALL_SW | COL_WALL_W | COL_LOC | COL_BLOCK_WALK | COL_GROUND_DECOR)
#define COL_BLOCK_W_NS (COL_WALL_N | COL_WALL_NE | COL_WALL_E | COL_WALL_SE | COL_WALL_S | COL_LOC | COL_BLOCK_WALK | COL_GROUND_DECOR)

#define COL_PROJ_SIGHT_N (COL_PROJ_WALL_N | COL_PROJ_BLOCK_FULL)
#define COL_PROJ_SIGHT_E (COL_PROJ_WALL_E | COL_PROJ_BLOCK_FULL)
#define COL_PROJ_SIGHT_S (COL_PROJ_WALL_S | COL_PROJ_BLOCK_FULL)
#define COL_PROJ_SIGHT_W (COL_PROJ_WALL_W | COL_PROJ_BLOCK_FULL)

#define RC_BLOCK_ACCESS_NORTH 0x1
#define RC_BLOCK_ACCESS_EAST  0x2
#define RC_BLOCK_ACCESS_SOUTH 0x4
#define RC_BLOCK_ACCESS_WEST  0x8

// Pending hit (delayed damage with prayer snapshot)
enum {
    RC_HIT_SUPPRESS_ENCOUNTER_EFFECTS = 1u << 0,
    RC_HIT_SOURCE_EXPIRED = 1u << 1,
    RC_HIT_BREATH_PROTECTED = 1u << 2,  // launch saw breath protection
};

enum {
    RC_HIT_TYPE_MISS = 0,
    RC_HIT_TYPE_NORMAL = 1,
    RC_HIT_TYPE_MAX = 2,
};

enum {
    RC_HIT_SOURCE_STATUS = -2,
    RC_HIT_SOURCE_PLAYER = -1,
};

// Field order keeps the struct at 56 bytes (no padding growth).
typedef struct {
    int active;
    int damage;
    int max_hit;
    int defence_drain;
    RcTick apply_tick;
    int attack_style;       // RcCombatStyle
    int source_idx;         // player, status, or NPC uid
    int prayer_snapshot;    // locked prayer at snapshot tick
    uint32_t enc_generation; // launching encounter run generation
    RcTick prayer_lock_tick;
    bool accurate;
    uint8_t hit_type;
    uint8_t flags;
    uint8_t enc_attack;     // encounter attack index + 1; zero = none
    uint16_t spell_key;     // spell index + 1; zero means no spellbook cast
    uint16_t weapon_id;     // launch-time weapon, zero for unarmed
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
    uint64_t sequence;
    uint8_t hit_type;
    uint8_t flags;
} RcCombatRecentHit;

typedef struct {
    bool active;
    RcCombatTargetRef target;
    const char *failure_reason;
    bool attack_prepared;
    uint16_t prepared_min_hit, prepared_max_hit;
    uint32_t prepared_attack_flags;
    uint8_t prepared_enc_attack;        // encounter attack index + 1
    uint32_t prepared_enc_generation;   // run generation at selection
    uint32_t flags;
    int attack_range;
    int distance_to_target;
    int line_of_sight;
    int hp_current;
    int hp_max;
    RcCombatActorRef primary_attacker;
    RcCombatActorRef attackers[RC_MAX_COMBAT_ATTACKERS];
    int attacker_count;
    uint8_t attacker_ticks[RC_MAX_COMBAT_ATTACKERS];
    int under_attack_timer;
    int last_hit_timer;
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
    RcCombatRecentHit recent_hits[4];
    int recent_hit_count;
    uint64_t next_hit_sequence;
} RcCombatActorState;

#define RC_MAX_COMBAT_ATTACK_EVENTS 64

typedef enum {
    RC_COMBAT_ACTION_NONE = 0,
    RC_COMBAT_ACTION_ITEM = 1,
    RC_COMBAT_ACTION_SPELL = 2,
    RC_COMBAT_ACTION_NPC = 3,
    RC_COMBAT_ACTION_SPECIAL = 4,
    RC_COMBAT_ACTION_ENCOUNTER = 5,   // action_key_name = encounter attack/part
} RcCombatActionKind;

typedef struct {
    bool active;
    uint8_t source_kind;
    uint8_t target_kind;
    uint8_t style;
    uint8_t action_kind;
    bool accurate;
    int source_uid;
    int hit_count;
    int target_uid;
    int source_definition_id;
    int target_definition_id;
    int source_x, source_y;
    int target_x, target_y;
    int plane;
    int action_key_id;
    char action_key_name[64];
    int hit_delay;
    uint8_t hit_delays[4];
    uint8_t hit_accurate[4];
    int weapon_item_id;
    int ammo_item_id;
    int spell_idx;
    int stance_idx;
    RcTick world_tick;
} RcCombatAttackEvent;

// Inventory slot
typedef struct {
    int item_id;    // -1 = empty
    int quantity;
    uint32_t state_id;   // 0 = ordinary/default item state
    uint32_t generation; // inventory: mutation; bank: stack identity lifetime
} RcInvSlot;

typedef enum {
    RC_ITEM_RESULT_INVALID = 0,
    RC_ITEM_RESULT_OK,
    RC_ITEM_RESULT_QUEUED,
    RC_ITEM_RESULT_DISABLED,
    RC_ITEM_RESULT_ACTION_DENIED,
    RC_ITEM_RESULT_EMPTY,
    RC_ITEM_RESULT_STALE,
    RC_ITEM_RESULT_NO_DEFINITION,
    RC_ITEM_RESULT_NOT_EQUIPPABLE,
    RC_ITEM_RESULT_REQUIREMENT,
    RC_ITEM_RESULT_CAPACITY,
    RC_ITEM_RESULT_STACK_LIMIT,
    RC_ITEM_RESULT_STATE_MISMATCH,
    RC_ITEM_RESULT_CONFLICT,
} RcItemResultCode;

typedef struct {
    RcItemResultCode code;
    int item_id;
    int slot;
    int required_skill;
    int required_level;
} RcItemActionResult;

typedef enum {
    RC_BANK_OK = 0, RC_BANK_PARTIAL, RC_BANK_INVALID, RC_BANK_DISABLED,
    RC_BANK_CLOSED, RC_BANK_STALE, RC_BANK_CAPACITY, RC_BANK_UNBANKABLE,
    RC_BANK_UNREVIEWED, RC_BANK_BAD_NOTE, RC_BANK_CONFLICT, RC_BANK_QUEUE_REJECTED,
} RcBankResultCode;

typedef struct {
    uint64_t sequence;
    RcBankResultCode code;
    int slot, item_id, requested;
    int64_t moved;
    bool withdraw, cannot_note;
} RcBankResult;

// Skill state
typedef struct {
    int xp[SKILL_COUNT];
    uint8_t xp_hundredths[SKILL_COUNT];
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
    int region_width, region_height;
} RcWorldMap;

typedef enum {
    RC_ROUTE_FAILED = 0,
    RC_ROUTE_BLOCKED,
    RC_ROUTE_ALREADY_ARRIVED,
    RC_ROUTE_EXACT,
    RC_ROUTE_ALTERNATIVE,
    RC_ROUTE_PARTIAL,
} RcRouteStatus;

typedef enum {
    RC_ROUTE_REACH_EXACT = 0,
    RC_ROUTE_REACH_RECTANGLE,
    RC_ROUTE_REACH_WALL,
} RcRouteReachKind;

typedef struct {
    int x, y;
    int width, height;
    int min_distance, max_distance;
    uint8_t kind;
    uint8_t shape;
    uint8_t rotation;
    uint8_t block_access;
    bool allow_inside;
    bool require_los;
    bool require_line_of_walk;
} RcRouteTarget;

typedef struct {
    int waypoints_x[RC_MAX_ROUTE];
    int waypoints_y[RC_MAX_ROUTE];
    int length;
    int reached_x, reached_y;
    int cost;
    int segment_cost;
    RcRouteStatus status;
} RcRoute;

typedef enum {
    RC_MOVEMENT_NONE = 0,
    RC_MOVEMENT_ROUTE_ADMITTED,
    RC_MOVEMENT_MOVED,
    RC_MOVEMENT_ARRIVED,
    RC_MOVEMENT_BLOCKED,
    RC_MOVEMENT_NO_ROUTE,
} RcMovementResult;

// Player
typedef struct {
    // Position
    int x, y, plane;
    int prev_x, prev_y;

    // Route
    int route_x[RC_MAX_ROUTE], route_y[RC_MAX_ROUTE];
    int route_len, route_idx;
    RcRouteTarget route_target;
    int route_entity_width, route_entity_height;
    bool route_allow_alternative;
    bool route_continue;
    RcRouteStatus route_status;
    RcMovementResult movement_result;
    int movement_step_x[2], movement_step_y[2];
    uint8_t movement_step_count;
    bool running;

    // Combat
    int current_hp, max_hp;
    bool is_dead;
    RcTick death_tick;
    int attack_timer;
    int attack_target;      // NPC uid or -1
    int attack_target_def_id;
    // The attack's last in-reach verdict, keyed by what decides it (see
    // rc_combat_player_approach); `tick` is its world tick plus one, 0: none.
    struct {
        RcTick tick;
        uint32_t npc_tile, tile;   // packed x | y << 14 | plane << 28
        int npc_slot, weapon, style_idx, autocast, manual_cast;
    } attack_reach;
    RcTick attack_unroutable_tick; // world tick + 1 of a failed attack route
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
    int last_hit;
    int last_hit_timer;
    // Facing, as OSRS face_pathingentity/face_square/face_angle: the client
    // turns toward facing_entity (an NPC uid, -1: none) every frame.
    // facing_x/y is a pending face square (south-west tile, -1: none) sent
    // once, as face_angle, when the player ends a tick without moving;
    // face_angle_seq counts the sends.
    int facing_entity;
    int facing_x, facing_y;
    uint8_t facing_width, facing_length;
    int16_t face_angle;
    uint32_t face_angle_seq;
    RcPendingHit pending_hits[RC_MAX_PENDING_HITS];
    int num_pending_hits;

    // Prayer
    uint32_t active_prayers;
    uint32_t quick_prayers;
    bool quick_prayers_active;
    int64_t prayer_drain_counter;
    int current_prayer_points; // tenths; content APIs name their units
    RcTick prayer_drain_start_tick;
    RcPrayerOutcome prayer_outcome;

    // Timers
    RcTick food_ready_tick, potion_ready_tick, combo_ready_tick;
    RcTick stamina_until;
    RcTick antifire_until, super_antifire_until;
    RcTick delayed_food_tick;
    int delayed_food_heal, delayed_food_energy;
    bool delayed_food_cure;
    uint32_t divine_mask;
    RcTick divine_until[SKILL_COUNT];
    int divine_boost[SKILL_COUNT];
    uint64_t consume_sequence, consume_outcome_sequence;
    int consume_kind, consume_result, consume_item_id;
    int ward_of_arceuus_timer;
    RcTraversalState traversal;
    uint64_t next_traversal_generation;
    uint64_t next_traversal_event_sequence;
    uint64_t next_traversal_outcome_sequence;
    RcTraversalEvent traversal_event;
    RcTraversalOutcome traversal_outcome;

    // Stats & items
    RcSkills skills;
    RcInvSlot inventory[RC_INVENTORY_SIZE];
    RcInvSlot bank[RC_BANK_SIZE];
    uint8_t bank_tab[RC_BANK_SIZE];
    RcInvSlot equipment[RC_EQUIP_COUNT];
    int equipment_bonuses[14];
    uint32_t inventory_revision;
    uint32_t equipment_revision;
    uint32_t bank_revision;
    uint32_t next_bank_generation;
    uint32_t next_item_generation;
    RcItemActionResult last_item_action;

    // Interaction
    int interact_type;
    int interact_target;
    int interact_option;
    RcPendingInteraction interaction;
    uint64_t next_interaction_generation;
    uint64_t next_interaction_outcome_sequence;
    RcInteractionOutcome interaction_outcome;
    int storage_kind;
    int storage_target;
    int storage_option;
    uint64_t storage_session;
    RcInteractionTarget storage_source;
    int storage_x, storage_y, storage_plane;
    RcBankResult bank_result;

    // Skilling
    int skill_action;
    RcTick skill_ready_tick;
    int skill_target_x, skill_target_y;

    // Regen
    int hp_regen_counter;
    int stat_restore_counter;
    int stat_boost_counter;
    int rapid_restore_counter;
    int preserve_timer;

    // Run energy
    int run_energy;         // 0-10000
    int weight;             // grams

    // Auto-retaliate
    bool auto_retaliate;

    // One mutually exclusive player toxin. Poison uses severity, venom hit damage.
    int toxin_severity;
    bool toxin_venom;
    RcTick toxin_tick, poison_immune_until, venom_immune_until;
    int disease_tick_counter;
    RcTick teleblock_start_tick;
    RcTick teleblock_expire_tick;
    RcTick freeze_start_tick;
    RcTick freeze_expire_tick;
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

struct RcWorld;
typedef RcItemActionResult (*RcItemEquipRequirementHook)(
    struct RcWorld *world, const RcPlayer *player, int item_id, void *ctx);

#define RC_INTERACTION_KEY_ANY -1
#define RC_INTERACTION_CONTENT_GROUP_NPC_ATTACK 1
#define RC_INTERACTION_CONTENT_GROUP_DIALOGUE 2
#define RC_INTERACTION_CONTENT_GROUP_SHOP 3
#define RC_INTERACTION_CONTENT_GROUP_BANK 4
#define RC_INTERACTION_CONTENT_GROUP_SKILLING 5
#define RC_MAX_INTERACTION_HANDLERS 256

typedef enum {
    RC_INTERACTION_HANDLER_NONE = 0,
    RC_INTERACTION_HANDLER_COMPLETE,
    RC_INTERACTION_HANDLER_CANCEL,
    RC_INTERACTION_HANDLER_CONTINUE_APPROACH,
    RC_INTERACTION_HANDLER_MESSAGE,
    RC_INTERACTION_HANDLER_FAILURE,
    RC_INTERACTION_HANDLER_HANDOFF,
} RcInteractionHandlerCode;

typedef struct {
    RcInteractionKind kind;
    RcInteractionOp op;
    int definition_id;
    int content_group;
    int source_item_id;
    int source_spell_id;
    int widget_id;
    int component_id;
} RcInteractionDispatchKey;

typedef struct {
    RcInteractionHandlerCode code;
    RcInteractionFailure failure;
    int approach_range;
    char message[RC_INTERACTION_RESULT_MESSAGE_LEN];
} RcInteractionHandlerResult;

typedef RcInteractionHandlerResult (*RcInteractionHandlerFn)(
    struct RcWorld *world, RcPlayer *player,
    const RcPendingInteraction *interaction, void *ctx);

typedef struct {
    RcInteractionDispatchKey key;
    RcInteractionHandlerFn fn;
    void *ctx;
} RcInteractionHandlerEntry;

typedef enum {
    RC_NPC_LIFE_REMOVED = 0,
    RC_NPC_LIFE_ALIVE,
    RC_NPC_LIFE_DYING,
    RC_NPC_LIFE_HIDDEN,
} RcNpcLifePhase;

typedef enum {
    RC_NPC_ROUTE_NONE = 0,
    RC_NPC_ROUTE_WANDER,
    RC_NPC_ROUTE_CHASE,
    RC_NPC_ROUTE_RETURN,
    RC_NPC_ROUTE_SCRIPTED,
} RcNpcRouteMode;

// NPC (live instance)
typedef struct {
    int def_id;             // index into definitions table
    int form_def;           // encounter form override: def index + 1; 0 = base
    // Encounter form defence bonuses by RcCombatStyle - 1 (stab, slash,
    // crush, ranged, magic); 0 without one.
    int16_t defence_bonus[5];
    RcTick despawn_tick;    // removed at this tick when set (temporary NPCs)
    int uid;
    uint64_t spawn_key;     // stable indexed-spawn identity; 0 = dynamic
    int x, y, plane;
    int spawn_x, spawn_y, spawn_plane;
    int spawn_hp;
    int current_hp;
    int stats[6];
    int attack_timer;
    int death_timer;
    int respawn_timer;
    int target_uid;         // -1 = no target
    RcCombatActorState combat;
    RcPendingHit pending_hits[RC_MAX_PENDING_HITS];
    int num_pending_hits;
    // Facing as the player's (facing_entity 0 is the player). NPCs pend no
    // face squares: steps set their direction, a spawn its face angle.
    int facing_entity;
    int16_t face_angle;
    uint32_t face_angle_seq;
    int last_hit;
    int last_hit_timer;
    bool is_dead;
    int wander_timer;
    int spawn_wander_range;
    int regen_timer;
    int hunt_timer;
    int attack_count;
    int prev_x, prev_y;
    int poison_damage;
    int poison_tick_counter;
    int route_x[RC_MAX_ROUTE], route_y[RC_MAX_ROUTE];
    RcTick immobilized_until;
    RcTick immobilize_immune_until;
    int route_len, route_idx;
    RcRouteTarget route_target;
    bool route_continue;
    RcRouteStatus route_status;
    RcMovementResult movement_result;
    uint8_t route_mode;
    uint8_t spawn_direction;
    bool respawns;
    bool disable_wander;
    bool force_player_max_hit;
    bool player_untargetable;
    bool owner_controlled;  // owner drives it; no wander/hunt/attacks
    bool player_loot_credit;
    bool loot_prepared;
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
    uint64_t spawn_key;     // stable indexed-spawn identity; 0 = dynamic
    int item_id;
    int quantity;
    uint32_t state_id;
    int spawn_quantity;
    int x, y, plane;
    int owner_uid;
    int original_owner_uid;
    int reveal_timer;
    int despawn_timer;
    int initial_reveal_ticks;
    int initial_lifetime_ticks;
    int respawn_ticks;
    int respawn_timer;
    int arrival_timer;
    RcTick timer_start_tick;
    uint8_t visibility;
    bool static_spawn;
    bool active;
} RcGroundItem;

#define RC_MAX_LOOT_DROPS 64
typedef struct {
    int item_id, quantity;
    uint32_t state_id;
} RcGroundGrant;

typedef enum {
    RC_LOOT_GENERIC,
    RC_LOOT_SUPPRESS,
    RC_LOOT_REPLACE,
    RC_LOOT_AUGMENT,
} RcLootPolicy;

typedef RcLootPolicy (*RcNpcLootHook)(struct RcWorld *world, const RcNpc *npc,
    RcGroundGrant *out, int capacity, int *count, void *ctx);

typedef struct {
    int npc_uid, npc_def_id;
    int x, y, plane;
    RcTick tick;
    int result, count;
    RcGroundGrant grants[RC_MAX_LOOT_DROPS];
} RcLootReceipt;

typedef struct RcObjectState {
    uint64_t placement_key;
    int base_obj_id;
    int active_obj_id;
    int x, y, plane;
    int active_x, active_y, active_plane;
    uint8_t base_type, base_rotation;
    uint8_t active_type, active_rotation;
    RcTick revert_tick;
    uint8_t layer;
    uint8_t flags;
} RcObjectState;

enum {
    RC_OBJECT_STATE_OPEN      = 1u << 0,
    RC_OBJECT_STATE_DEPLETED  = 1u << 1,
    RC_OBJECT_STATE_DYNAMIC   = 1u << 2,
    RC_OBJECT_STATE_SPAWNED   = 1u << 3,
    RC_OBJECT_STATE_HIDDEN    = 1u << 4,
};

typedef struct {
    bool active;
    int origin_x, origin_y;
    int width, height;
    int min_plane, max_plane;
    uint32_t options;
    uint32_t components;
    uint32_t generation;
} RcActiveArea;

typedef struct {
    uint64_t active_area_load_count;
    double active_area_load_ms;
    double active_area_load_total_ms;
    double backend_page_load_ms;
    double backend_page_load_total_ms;
    int backend_pages_loaded;
    int active_npcs;
    int active_ground_items;
    int dormant_npc_states;
    int dormant_ground_items;
    int saved_npc_states;
    int restored_npc_states;
    int saved_ground_items;
    int restored_ground_items;
    int expired_ground_items;
} RcWorldStreamingTelemetry;

struct RcWorld;
struct RcGameData;
struct RcSpellDef;
struct RcCombatCalc;
struct RcDormantNpcState;
struct RcDormantGroundItemState;

enum {
    RC_RANGED_RESOURCE_NONE = -1,
    RC_RANGED_RESOURCE_INVALID = -2,
};

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
    int (*extra_npc_hit_count)(const struct RcWorld *world, const RcNpc *npc);
    int (*modify_npc_attack_speed)(struct RcWorld *world, RcNpc *npc,
                                   int default_speed);
    int (*modify_incoming_damage_after_protection)(
        struct RcWorld *world, const RcPendingHit *hit, int damage);
    // Encounter breath attacks (RcEncounterAttack.breath): max hit for the
    // breath profile against the player's current protection. `resisted`
    // is the magic accuracy outcome; *shielded reports shield or Protect
    // from Magic coverage. Negative = unknown profile (attack rejected).
    int (*npc_breath_max_hit)(const struct RcWorld *world, const RcNpc *npc,
                              uint8_t breath, bool resisted, bool *shielded);
    int (*player_special_energy_cost)(const struct RcWorld *world,
                                      const RcPlayer *player,
                                      const RcNpc *target, int weapon_id);
    int (*player_ranged_resource_cost)(const RcPlayer *player, bool special);
    int (*player_ranged_resource_slot)(const RcPlayer *player,
                                      const char **failure_reason);
    int (*player_hit_delay)(const struct RcWorld *world, const RcNpc *target,
                            const struct RcSpellDef *spell, int hit_index);
    // Zero leaves ordinary single-hit rolling to core; negative rejects launch.
    int (*prepare_player_hits)(struct RcWorld *world, const RcNpc *target,
                               struct RcCombatCalc *calc, bool special,
                               RcPendingHit *hits, int capacity,
                               const char **failure_reason);
    int (*consume_weapon_charge)(struct RcWorld *world, int weapon_id);
    int (*consume_ranged_resource)(struct RcWorld *world, int slot, int count,
                                   int x, int y, int plane, int delay,
                                   const char **failure);
    int (*prepare_player_magic)(struct RcWorld *world, const RcNpc *target,
                                const struct RcSpellDef *spell,
                                struct RcCombatCalc *calc, int *speed,
                                const char **failure_reason);
    int (*can_autocast_spell)(const RcPlayer *player,
                              const struct RcSpellDef *spell);
    void (*on_player_hit_npc)(struct RcWorld *world, RcNpc *target,
                              const RcPendingHit *hit, int damage);
    int (*modify_player_special_damage)(struct RcWorld *world,
                                        const RcPlayer *player,
                                        const RcNpc *target, int weapon_id,
                                        RcCombatStyle style, int damage,
                                        int max_hit);
    void (*after_player_special_launch)(struct RcWorld *world,
                                        int weapon_id, int damage);
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
    RcNpc *npcs;
    int npc_count;
    int npc_capacity;
    int next_npc_uid;
    RcGroundItem ground_items[RC_MAX_GROUND_ITEMS];
    int ground_item_count;
    int next_ground_item_uid;
    RcNpcLootHook npc_loot_hook;
    void *npc_loot_ctx;
    RcLootReceipt last_loot;
    struct RcDormantNpcState *dormant_npcs;
    int dormant_npc_count;
    int dormant_npc_capacity;
    struct RcDormantGroundItemState *dormant_ground_items;
    int dormant_ground_item_count;
    int dormant_ground_item_capacity;
    RcObjectState object_states[RC_MAX_OBJECT_STATES];
    int object_state_count;
    RcEncounterEffect encounter_effects[RC_ENC_MAX_EFFECTS];
    int encounter_effect_count;
    RcTick next_object_change_tick;
    uint64_t next_dynamic_object_key;
    int32_t varps[RC_MAX_VARPS];
    RcWorldMap map;
    RcActiveArea active_area;
    RcWorldStreamingConfig streaming;
    RcWorldStreamingTelemetry streaming_telemetry;
    RcTick tick;
    bool in_tick;
    RcPlayerActionState player_action;
    RcPlayerCommandQueue player_commands;
    uint32_t rng_state;
    uint32_t initial_seed;
    bool multi_combat;
    RcCombatContentHooks combat_hooks;
    // Content installer re-run by rc_world_reset (see rc_world_install_content).
    int (*content_installer)(struct RcWorld *world);
    const struct RcConsumableDef *consumables;
    int consumable_count;
    int (*consumable_prayer_bonus)(const RcPlayer *player);
    const struct RcStoragePolicy *storage_policy;
    RcItemEquipRequirementHook item_equip_requirement_hook;
    void *item_equip_requirement_ctx;
    RcCombatAttackEvent combat_attack_events[RC_MAX_COMBAT_ATTACK_EVENTS];
    int combat_attack_event_count;
    RcInteractionHandlerEntry
        interaction_handlers[RC_MAX_INTERACTION_HANDLERS];
    int interaction_handler_count;

    // Subsystem bitmask — see config.h for RC_SUB_* flags. Checked
    // only by the base tick dispatcher; subsystem code assumes its
    // subsystem is enabled if it gets called.
    uint32_t enabled;
    bool members_world;

    // Event bus — subsystems subscribe at init, fire episodically.
    // See events.h / README §7.
    RcEventBus events;
    struct RcGameData *game_data;

    // Encounter subsystem state (inline arena layout per README §4).
    // Only exercised when RC_SUB_ENCOUNTER is enabled.
    RcEncounterState encounter;

    char npc_spawns_path[512];
    char ground_item_spawns_path[512];
    RcPlayerCommand player_command_storage[RC_MAX_PLAYER_COMMANDS];
} RcWorld;

#endif
