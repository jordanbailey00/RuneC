#ifndef RC_ENCOUNTER_H
#define RC_ENCOUNTER_H

#include <stdint.h>
#include <stdbool.h>
#include "handles.h"

// Encounter subsystem: generic dispatch for boss forms, attacks, phases
// and mechanic primitives. Specs come from `content/encounters/*.toml`
// through `tools/export_encounters.py` into `data/defs/encounters.bin`
// (ENCT v14). Only encounters that declare a support level are exported;
// the loader rejects malformed, over-capacity or unsupported content.
// Specs are immutable and shared through RcGameData; all fight state
// lives in a world-owned run. rc-content registers scripts by name.

struct RcWorld;

// Primitive function signature. All primitives take the world +
// active encounter state; params are primitive-specific and cast
// from a void* per the registry.
typedef void (*RcEncounterPrimFn)(struct RcWorld *world, int enc_idx,
                                  const void *params);
typedef void (*RcEncounterScriptFn)(struct RcWorld *world, int enc_idx);

enum {
    RC_ENC_SUPPORT_NONE = 0,
    RC_ENC_SUPPORT_FULL = 1,          // every attack, form, phase, mechanic
    RC_ENC_SUPPORT_ATTACKS_ONLY = 2,  // verified attack table only
};

// Encounter phase — ordered list per encounter. Phase 0 starts the
// fight; later phases enter when boss HP drops below `enter_at_hp_pct`
// or when a mechanic/script selects them.
typedef struct {
    char id[32];
    uint8_t enter_at_hp_pct;       // 100 = selected only explicitly
    uint32_t allowed_attack_mask;  // bitmask of attack indices
    char script[48];               // rc-content script name, optional
    bool player_targetable;        // false = player cannot damage this phase
} RcEncounterPhase;

// Attack definition. A selected attack replaces the NDEF default for
// one launch; weights choose among attacks allowed by the phase.
typedef struct {
    char name[48];
    uint32_t npc_id;               // owning form/actor cache id; 0 = any
    uint8_t style;                 // RcCombatStyle (melee subtype included)
    uint16_t min_hit;
    uint16_t max_hit;
    uint8_t weight_adjacent;       // selection weight when adjacent
    uint8_t weight_distant;        // selection weight otherwise
    uint8_t range;                 // reach in tiles; 0 = NPC default
    uint8_t hit_delay;             // ticks to impact; 0 = combat profile
    uint8_t breath;                // content breath profile; 0 = none
    uint8_t adjacent_pct;          // tile attacks: damage % beside impact
    uint32_t flags;                // RC_ENC_ATTACK_*
    uint8_t effect_id;             // RC_ENC_ATTACK_EFFECT_*
    uint8_t effect_when;           // RC_ENC_EFFECT_WHEN_*
    uint8_t effect_a;              // effect amount (damage, points, tiles)
    uint8_t effect_b;              // effect duration or count
    uint16_t effect_chance;        // per mille when unprotected
    uint16_t effect_chance_protected; // per mille behind shield/prayer
} RcEncounterAttack;

enum {
    RC_ENC_ATTACK_ALWAYS_ACCURATE    = 1u << 0,  // roll damage, never miss
    RC_ENC_ATTACK_IGNORES_PROTECTION = 1u << 1,  // no prayer/breath protection
    RC_ENC_ATTACK_TILE_TARGET        = 1u << 2,  // strikes the launch tile
    RC_ENC_ATTACK_NO_DAMAGE          = 1u << 3,  // effect-only attack
    RC_ENC_ATTACK_NEEDS_FREE_SLOT    = 1u << 4,  // selectable only with space
    RC_ENC_ATTACK_DIAGONAL_REACH     = 1u << 5,  // adjacency includes diagonals
};

enum {
    RC_ENC_ATTACK_EFFECT_NONE = 0,
    RC_ENC_ATTACK_EFFECT_POISON = 1,           // a = starting damage
    RC_ENC_ATTACK_EFFECT_VENOM = 2,            // a = starting damage
    RC_ENC_ATTACK_EFFECT_DEACTIVATE_PRAYERS = 3,
    RC_ENC_ATTACK_EFFECT_DRAIN_PRAYER = 4,     // a = prayer points
    RC_ENC_ATTACK_EFFECT_DRAIN_ALL_STATS = 5,  // a = levels
    RC_ENC_ATTACK_EFFECT_FREEZE = 6,           // b = ticks
    RC_ENC_ATTACK_EFFECT_TELEPORT_AWAY = 7,    // a = min tiles, b = max tiles
    RC_ENC_ATTACK_EFFECT_UNEQUIP = 8,          // b = item count, weapon first
    RC_ENC_ATTACK_EFFECT_COUNT = 9,
};

enum {
    RC_ENC_EFFECT_WHEN_DAMAGED = 0,   // landed with damage > 0
    RC_ENC_EFFECT_WHEN_ALWAYS = 1,    // every impact, even fully blocked
    RC_ENC_EFFECT_WHEN_ROLLED = 2,    // rolled damage > 0, even if blocked
    RC_ENC_EFFECT_WHEN_COUNT = 3,
};

typedef struct {
    uint8_t attack_idx;
    uint8_t style;
    uint32_t prayer_flag;
    uint8_t damage_pct;
} RcEncounterProtection;

enum {
    RC_ENC_DMG_STYLE_NOT = 1,
    RC_ENC_DMG_NOT_CORPBANE_STAB = 2,
    RC_ENC_DMG_MELEE_NOT_HALBERD_SALAMANDER = 3,
    RC_ENC_DMG_CAP_RANGE = 4,
    RC_ENC_DMG_STYLE_IS = 5,
    RC_ENC_DMG_MELEE_NOT_HALBERD = 6,
    RC_ENC_DMG_PLAYER_NOT_FACEMASK = 7,
};

typedef struct {
    uint32_t npc_id;               // 0 = applies to any NPC in spec
    uint8_t condition;             // RC_ENC_DMG_*
    uint8_t style;                 // RcCombatStyle for style predicates
    uint8_t damage_pct;            // post-hit scaling percent
    uint8_t flags;
} RcEncounterDamageModifier;

enum {
    RC_ENC_TRIGGER_PERIODIC     = 0,     // every period_ticks while engaged
    RC_ENC_TRIGGER_PHASE_ENTER  = 1,
    RC_ENC_TRIGGER_PHASE_EXIT   = 2,
    RC_ENC_TRIGGER_PHASE_IN     = 3,     // periodic while phase is current
    RC_ENC_TRIGGER_AFTER_ATTACK = 4,     // not dispatched; loader rejects
    RC_ENC_TRIGGER_DURING_MECH  = 5,     // not dispatched; loader rejects
    RC_ENC_TRIGGER_ATTACK_COUNT = 6,     // run by a special rotation
    RC_ENC_TRIGGER_EVENT        = 7,     // not dispatched; loader rejects
    RC_ENC_TRIGGER_BOSS_HIT     = 8,     // each hit the boss takes, even 0
    RC_ENC_TRIGGER_NONE         = 255,
};

// Mechanic entry — schedules a primitive on a period or trigger. Specs
// are immutable; each run owns its own due ticks.
typedef struct {
    char name[48];
    RcEncounterPrimFn prim;        // resolved from primitive_id at load
    uint8_t primitive_id;          // enum — see docs/encounter_primitives.md PRIMITIVE_IDS
    uint8_t trigger_type;          // RC_ENC_TRIGGER_*
    uint8_t phase_idx;             // index into phases[], or 0xFF
    uint32_t phase_mask;           // union trigger phase bitmask; 0 = phase_idx
    char trigger_ref[32];          // attack/mechanic/event name for hook triggers
    uint16_t period_ticks;         // periodic triggers: ticks between runs
    uint8_t param_block[64];       // opaque per-primitive params
} RcEncounterMechanic;

// Primitive IDs — must match PRIMITIVE_IDS in tools/export_encounters.py.
enum {
    RC_PRIM_NONE                           = 0,
    RC_PRIM_TELEGRAPHED_AOE_TILE           = 1,
    RC_PRIM_SPAWN_NPCS                     = 2,
    RC_PRIM_SPAWN_NPCS_ONCE                = 3,
    RC_PRIM_HEAL_AT_OBJECT                 = 4,
    RC_PRIM_PERIODIC_HEAL_BOSS             = 5,
    RC_PRIM_DRAIN_PRAYER_ON_HIT            = 6,
    RC_PRIM_CHAIN_MAGIC_TO_NEAREST         = 7,
    RC_PRIM_PRESERVE_STAT_DRAINS           = 8,
    RC_PRIM_TELEPORT_ON_INCOMING_ATTACK    = 9,
    RC_PRIM_TELEPORT_PLAYER_NEARBY         = 10,
    RC_PRIM_UNEQUIP_PLAYER_ITEMS           = 11,
    RC_PRIM_POSITIONAL_AOE                 = 12,
    RC_PRIM_SPAWN_LEECH_NPC                = 13,
    RC_PRIM_REGEN_WHEN_NO_PLAYER           = 14,
    RC_PRIM_ATTACK_COUNTER_SPECIAL         = 15,
    RC_PRIM_SPAWN_SOUL_ATTACKERS           = 16,
    RC_PRIM_DOT_TILE_PLACEMENT             = 17,
    RC_PRIM_STATIC_DOT_LINE                = 18,
    RC_PRIM_SPAWN_HIDDEN_MINIONS           = 19,
    RC_PRIM_PHASE_ADVANCE_ON_CONDITION     = 20,
    RC_PRIM_GROUP_KILL_REQUIRED            = 21,
    RC_PRIM_PERIODIC_RESPAWN_IF_DEAD       = 22,
    RC_PRIM_FORM_TRANSITION_DIVE           = 23,
    RC_PRIM_ATTACK_COUNTER_ALTERNATE_SPECIAL = 24,
    RC_PRIM_COVERED_ARENA_ENVIRONMENT      = 25,
    RC_PRIM_DAMAGE_REDUCTION_UNTIL_TRIGGER = 26,
    RC_PRIM_ANIMATION_WARNING_STYLE_SWAP   = 27,
    RC_PRIM_CONVERGING_AOE                 = 28,
    RC_PRIM_STUN_THEN_FIRE_WALLS           = 29,
    RC_PRIM_MOVING_DOT_LINE                = 30,
    RC_PRIM_OBJECT_INTERACTION_TICKED      = 31,
    RC_PRIM_TOTEM_CHARGE_PROGRESSION       = 32,
    RC_PRIM_TELEGRAPHED_PORTAL_AOE         = 33,
    RC_PRIM_SPAWN_PAIRED_HUSKS             = 34,
    RC_PRIM_QUADRANT_SAFE_ZONE_DOT         = 35,
    RC_PRIM_SHUFFLE_PLAYER_PRAYERS         = 36,
    RC_PRIM_INFECTIOUS_DOT_WITH_CURE       = 37,
    RC_PRIM_SPAWN_CONVERGENT_MINIONS       = 38,
    RC_PRIM_AOE_TILE_DEBUFF                = 39,
    RC_PRIM_LINE_DASH                      = 40,
    RC_PRIM_SPAWN_OBJECTIVE_NPCS           = 41,
    RC_PRIM_NPC_PATHED_MOVEMENT            = 42,
    RC_PRIM_SPAWN_WALL_TENTACLES           = 43,
    RC_PRIM_TELEPORTING_TILE_AOE           = 44,
    RC_PRIM_HOMING_PROJECTILES_WITH_WALLS  = 45,
    RC_PRIM_SMITE_DRAIN_SHIELD             = 46,
    RC_PRIM_PERIODIC_SPIKE_CLUSTER         = 47,
    RC_PRIM_MOVING_ROTATIONAL_HAZARDS      = 48,
    RC_PRIM_HEAL_BOSS_ON_PLAYER_ATTACK_MISS = 49,
    RC_PRIM_TILE_DEBUFF_ON_STAND           = 50,
    RC_PRIM_SPAWN_BUFF_ZONE_NPC            = 51,
    RC_PRIM_ONE_SHOT_ARENA_EFFECT          = 52,
    RC_PRIM_PLAYER_SANITY_TRACKER          = 53,
    RC_PRIM_SPAWN_TENTACLE_PROJECTILES     = 54,
    RC_PRIM_AOE_PRAYER_SWAP_DEMAND         = 55,
    RC_PRIM_AUDIO_VISUAL_DISRUPTION        = 56,
    RC_PRIM_OBJECT_ITEM_INTERACTION        = 57,
    RC_PRIM_PASSIVE_HEAL_DURING_PHASE      = 58,
    RC_PRIM_ATTACK_COUNTER_STYLE_SWAP      = 59,
    RC_PRIM_SPAWN_TRACKING_TORNADOES       = 60,
    RC_PRIM_FORCED_PRAYER_SWITCH_ON_STYLE_SWAP = 61,
    RC_PRIM_MULTI_LIMB_BOSS                = 68,
    RC_PRIM_PLAYER_POSITION_SWAP           = 69,
    RC_PRIM_ENVIRONMENTAL_WALL_SPAWN       = 70,
    RC_PRIM_TILE_TELEGRAPH_LIGHTNING       = 71,
    RC_PRIM_CONTINUOUS_HEAL_UNLESS_INTERRUPTED = 72,
    RC_PRIM_ONE_SHOT_WEAPON_PROVIDED       = 73,
    RC_PRIM_DESTRUCTIBLE_PILLARS           = 66,
    RC_PRIM_SPAWN_WEB_TILES                = 74,
    RC_PRIM_SPAWN_COLORED_NYLOCAS          = 75,
    RC_PRIM_PERSISTENT_DOT_TILE_POOL       = 76,
    RC_PRIM_OBELISK_DPS_CHECK              = 77,
    RC_PRIM_SPAWN_ENERGIZED_PYLONS         = 78,
    RC_PRIM_PERIODIC_DEATH_TILE_WAVE       = 79,
    RC_PRIM_SURVIVING_BOSS_ENRAGE          = 80,
    RC_PRIM_HEAL_ALTARS_PLAYER_MUST_DISABLE = 81,
    RC_PRIM_ACID_FIELD                     = 82,
    RC_PRIM_SPAWN_OWNED_HELPERS            = 83,
    RC_PRIM_HATCH_HELPERS_ON_HIT           = 84,
    RC_PRIM_MAX                            = 92,
};

// Per-primitive param structs. Each fits in RcEncounterMechanic.param_block[64]
// starting at offset 0. Packed to match export_encounters.py struct layout.

typedef struct __attribute__((packed)) {
    uint16_t damage_min;
    uint16_t damage_max;
    uint16_t solo_damage_max;
    uint8_t warning_ticks;
    uint8_t extra_random_tiles;
    uint8_t target_current_tile;
} RcPrimParamsTelegraphedAoe;

typedef struct __attribute__((packed)) {
    char name[32];                 // NPC name matched through RcNpcDef views
    uint8_t count;
    uint8_t persist_after_death;
    uint8_t heal_boss_on_contact;
    uint8_t blocks_boss_damage_until_dead;
    uint8_t freeze_player_ticks;
} RcPrimParamsSpawnNpcs;

typedef struct __attribute__((packed)) {
    char alive_npc_name[32];       // heal only while any matching NPC is alive
    uint8_t heal_per_tick;
} RcPrimParamsPeriodicHealBoss;

typedef struct __attribute__((packed)) {
    uint8_t heal_per_player;
    uint8_t heal_ticks_cap;
    uint8_t tick_period;
} RcPrimParamsHealAtObject;

typedef struct __attribute__((packed)) {
    uint8_t points;
    uint8_t spectral_shield_mitigation;
} RcPrimParamsDrainPrayerOnHit;

typedef struct __attribute__((packed)) {
    uint8_t max_bounces;
} RcPrimParamsChainMagic;

typedef struct __attribute__((packed)) {
    uint8_t hp_min_pct;
    uint8_t hp_max_pct;
    uint8_t chance_pct;
    uint8_t exclude_splashed_magic;
    uint8_t exclude_poison_dot;
    uint8_t extinguishes_light;
    uint8_t drops_aggression;
} RcPrimParamsTeleportOnIncomingAttack;

typedef struct __attribute__((packed)) {
    uint8_t marker;                // just confirms spec existence
} RcPrimParamsPreserveStatDrains;

typedef struct __attribute__((packed)) {
    uint8_t min_distance;
    uint8_t max_distance;
    uint8_t constrain_to_arena;
} RcPrimParamsTeleportPlayerNearby;

typedef struct __attribute__((packed)) {
    uint8_t count;
    uint8_t weapon_priority;
    uint16_t slot_mask;            // bit i => RcEquipSlot i may be unequipped
} RcPrimParamsUnequipPlayerItems;

typedef struct __attribute__((packed)) {
    uint16_t damage_min;
    uint16_t damage_max;
    uint8_t prayer_ignorable;
} RcPrimParamsPositionalAoe;

typedef struct __attribute__((packed)) {
    char name[32];
    uint8_t leech_per_tick;
    uint8_t heals_boss;
    uint8_t poison_weak;
    uint16_t spawn_cooldown_ticks;
    uint16_t large_hit_threshold;
    uint16_t boss_hp_below;
    uint8_t spawn_chance_denominator;
    uint8_t poisoned_leech_period_ticks;
} RcPrimParamsSpawnLeechNpc;

typedef struct __attribute__((packed)) {
    uint16_t every_n_attacks;
    uint8_t first_attack_trigger;
    uint8_t min_hit;
    uint8_t max_hit;
    uint8_t style;
    uint8_t sequence_count;
    uint8_t sequence_styles[4];
    uint8_t sequence_delays[4];
    uint8_t condition_prayer_style;
    uint8_t prayer_ignorable;
    uint8_t drain_prayer_pct;
    uint16_t max_hp;
    uint8_t skip_chance_pct;
    uint8_t priority;
} RcPrimParamsAttackCounterSpecial;

typedef struct __attribute__((packed)) {
    uint16_t every_n_attacks;
    uint16_t min_hp;
    uint16_t max_hp;
    uint8_t style_count;
    uint8_t styles[3];
    uint8_t damage_if_unblocked;
    uint8_t prayer_drain_if_blocked;
    uint8_t skip_chance_pct;
    uint8_t priority;
    uint8_t prayer_drain_with_ward;
    uint8_t prayer_drain_with_spectral;
    uint8_t prayer_drain_with_both;
    uint16_t spectral_item_id;
} RcPrimParamsSpawnSoulAttackers;

typedef struct __attribute__((packed)) {
    uint16_t every_n_attacks;
    uint16_t max_hp;
    uint8_t pool_count;
    uint8_t pools_on_player;
    uint8_t dot_per_tick;
    uint8_t skip_chance_pct;
    uint8_t priority;
    uint8_t pools_random;
    uint8_t duration_ticks;
    uint8_t dragonfire_protection_applies;
} RcPrimParamsDotTilePlacement;

typedef struct __attribute__((packed)) {
    uint8_t damage_per_cross;
} RcPrimParamsStaticDotLine;

typedef struct __attribute__((packed)) {
    char name[32];
    char object_name[24];
    uint8_t count;
    uint8_t max_hit;
} RcPrimParamsSpawnHiddenMinions;

#define RC_ENC_MAX_EFFECTS 64
#define RC_ENC_RUN_EFFECT_QUOTA 32
// Placement searches: acid pools within this radius of the player, owned
// helpers at most this far from the player.
#define RC_ENC_POOL_RADIUS_MAX 16
#define RC_ENC_SPAWN_DISTANCE_MAX 32

enum {
    RC_ENC_EFFECT_NONE = 0,
    RC_ENC_EFFECT_TRAVELLING_SOUL = 1,
    RC_ENC_EFFECT_LAVA_POOL = 2,
    RC_ENC_EFFECT_HIDDEN_OBJECT = 3,
    RC_ENC_EFFECT_ROOM_ATTACK = 4,
    RC_ENC_EFFECT_ACID_POOL = 5,      // standing on it damages; leech heals
    RC_ENC_EFFECT_FORM_DIVE = 6,
    RC_ENC_EFFECT_TILE_STRIKE = 7,    // warned tile; checked at impact
    RC_ENC_EFFECT_HELPER_DROP = 8,    // helper in flight; spawns at impact
};

// World-visible hazard owned by one encounter run. Presentation reads
// these; damage and lifetime are decided here only.
typedef struct {
    bool active;
    uint8_t kind;
    uint8_t style;
    uint8_t damage_per_tick;      // pools: max damage per tick standing on it
    uint8_t adjacent_pct;         // strikes: damage % beside the impact tile
    uint8_t leech_pct;            // pools: % of damage healed to the owner
    uint8_t run_slot;             // owning run slot
    uint32_t run_generation;      // owning run generation
    RcNpcId source_uid;           // owning actor, full width
    int x, y, plane;
    int target_x, target_y;
    int ticks_remaining;          // <0 = persistent until interaction.
    RcTick impact_tick;           // strikes, pools and drops: landing tick
    uint16_t min_damage, max_damage; // strikes: damage roll on impact
    bool landed;                  // pools: live (damaging) since impact
    uint8_t helper;               // drops: spec helper index
    uint8_t loc_type;             // pools: placement type of loc_id
    int loc_id;                   // pools: loc shown while live, or -1
    uint64_t object_key;          // pools: spawned loc state key, 0 = none
    char name[32];
    char target_name[32];
} RcEncounterEffect;

typedef struct __attribute__((packed)) {
    char npc_name[32];
    char target_phase[24];
} RcPrimParamsPhaseAdvanceOnCondition;

typedef struct __attribute__((packed)) {
    uint8_t count;
    uint32_t npc_ids[8];
    uint8_t drop_count;
    uint16_t drop_npc_ids[8];
} RcPrimParamsGroupKillRequired;

typedef struct __attribute__((packed)) {
    uint16_t cooldown_ticks;
    uint8_t count;
    uint32_t npc_ids[8];
} RcPrimParamsPeriodicRespawnIfDead;

typedef struct __attribute__((packed)) {
    uint8_t dive_ticks;
    uint8_t resurface_ticks;
    uint8_t untargetable_during_dive;
    uint8_t in_flight_attacks_still_resolve;
} RcPrimParamsFormTransitionDive;

// A special replaces every Nth attack and alternates between listed
// mechanics (exporter resolves names to indices).
typedef struct __attribute__((packed)) {
    uint8_t every_n_attacks;
    uint8_t special_count;          // 1..2
    uint8_t special_mechanics[2];   // mechanic indices, alternated
    uint8_t random_first;           // first special chosen with world RNG
} RcPrimParamsAttackCounterAlternateSpecial;

// Acid field: pools around the player (one on the player's tile) and a
// run of tile strikes, one per tick at the player's tile, checked on
// impact. The boss idles and takes scaled damage for the window.
typedef struct __attribute__((packed)) {
    uint8_t pool_count;
    uint8_t pool_radius;            // pools within this many tiles, on
                                    // tiles the player can walk to
    uint8_t pool_damage_max;        // per tick while standing on a pool
    uint8_t pool_leech_pct;         // % of pool damage healed to the boss
    uint8_t pool_ticks;             // pool lifetime
    uint8_t strike_count;           // strikes, one per tick
    uint8_t strike_delay;           // ticks from launch to impact
    uint16_t strike_min, strike_max;
    uint8_t damage_taken_pct;       // boss damage scale during the window
    uint8_t strike_style;           // RcCombatStyle for the strike hit
    uint8_t pool_object;            // spec object index, or RC_ENC_NO_OBJECT
    uint8_t pool_delay;             // ticks from the spit to the pools landing
} RcPrimParamsAcidField;

// Owned helpers placed a fixed distance from the player, on tiles whose
// direct walk reaches the player. While any lives, the run can hold the
// boss immune/idle and the player frozen.
typedef struct __attribute__((packed)) {
    uint8_t helper;                 // spec helper index
    uint8_t count;                  // 1..RC_ENC_MAX_RUN_HELPERS
    uint8_t spawn_distance;         // tiles from the player
    uint8_t explode_max;            // seek and explode, max at full HP; 0 = no
    uint8_t boss_immune;
    uint8_t freeze_player;
    uint8_t boss_idle;
    char instakill_spell[24];       // spell name that kills a helper outright
    uint8_t spawn_delay;            // ticks from launch to the helper landing
} RcPrimParamsSpawnOwnedHelpers;

// On a boss_hit trigger: a 1/chance_denominator roll hatches one helper
// beside the nearest map loc `near_loc` within `near_distance` tiles of the
// boss. Hatched helpers are ordinary aggressive NPCs that outlive the run
// and despawn after `despawn_ticks`.
typedef struct __attribute__((packed)) {
    uint8_t helper;                 // spec helper index
    uint8_t chance_denominator;     // 1..255
    uint32_t near_loc;              // B237 loc id (map placement)
    uint8_t near_distance;          // tiles between the footprints
    uint16_t despawn_ticks;         // lifetime of a hatched helper
} RcPrimParamsHatchHelpersOnHit;

typedef struct __attribute__((packed)) {
    uint16_t duration_ticks;
    uint8_t dot_per_tick;
    uint8_t safe_path_navigation;
    uint8_t pool_count;
    uint8_t damage_reduction_pct;
} RcPrimParamsCoveredArenaEnvironment;

typedef struct __attribute__((packed)) {
    uint8_t damage_reduction_pct;
    uint8_t damage_increase_pct;
    char clear_npc_name[32];
} RcPrimParamsDamageGate;

typedef struct __attribute__((packed)) {
    uint16_t every_n_attacks;
    uint8_t warning_ticks;
} RcPrimParamsStyleSwap;

typedef struct __attribute__((packed)) {
    uint8_t bolt_count;
    uint8_t damage_per_bolt;
    uint8_t warning_ticks;
} RcPrimParamsConvergingAoe;

typedef struct __attribute__((packed)) {
    uint8_t stun_ticks;
    uint8_t fire_wall_damage;
    uint8_t duration_ticks;
} RcPrimParamsStunFireWalls;

typedef struct __attribute__((packed)) {
    uint8_t trail_length;
    uint8_t dot_per_tick;
    uint8_t duration_ticks;
    uint8_t slows_player;
} RcPrimParamsMovingDotLine;

typedef struct __attribute__((packed)) {
    char object_names[3][18];
    uint8_t activation_ticks;
} RcPrimParamsObjectInteractionTicked;

typedef struct __attribute__((packed)) {
    uint8_t totem_count;
    uint8_t charge_per_attack;
    uint8_t total_charge_per_totem;
    uint16_t advance_damage;
} RcPrimParamsTotemCharge;

typedef struct __attribute__((packed)) {
    uint8_t portal_count_extra;
    uint8_t warning_ticks;
    uint8_t damage_max;
    uint8_t prayer_ignorable;
} RcPrimParamsTelegraphedPortalAoe;

typedef struct __attribute__((packed)) {
    uint8_t pair_count;
    uint8_t blue_style;
    uint8_t green_style;
    uint8_t immobilizes_target;
} RcPrimParamsSpawnPairedHusks;

typedef struct __attribute__((packed)) {
    uint8_t safe_quadrants;
    uint8_t unsafe_dot_per_tick;
    uint8_t heals_boss_multiplier_x10;
    uint8_t duration_ticks;
} RcPrimParamsQuadrantSafeZoneDot;

typedef struct __attribute__((packed)) {
    uint8_t duration_attacks;
} RcPrimParamsShufflePlayerPrayers;

typedef struct __attribute__((packed)) {
    uint8_t incubation_ticks;
    uint8_t burst_damage;
    uint8_t burst_damage_if_cured;
    uint8_t parasite_hp_uncured;
    uint8_t parasite_hp_cured;
} RcPrimParamsInfectiousDot;

typedef struct __attribute__((packed)) {
    char npc_name[32];
    uint8_t count_per_player;
    uint8_t absorb_damage_per_walker;
    uint8_t power_blast_on_absorption;
    uint8_t power_blast_min;
} RcPrimParamsSpawnConvergentMinions;

typedef struct __attribute__((packed)) {
    uint8_t aoe_size;
    uint8_t debuff_ticks;
    uint8_t attack_speed_penalty;
    uint8_t run_disabled;
} RcPrimParamsAoeTileDebuff;

typedef struct __attribute__((packed)) {
    uint8_t damage_max;
} RcPrimParamsLineDash;

typedef struct __attribute__((packed)) {
    char npc_name[32];
    char advance_to[24];
    uint8_t count;
    uint8_t fallback_ticks;
} RcPrimParamsSpawnObjectiveNpcs;

typedef struct __attribute__((packed)) {
    char advance_to[24];
    uint16_t ticks_to_complete;
} RcPrimParamsNpcPathedMovement;

typedef struct __attribute__((packed)) {
    char npc_name[32];
    char advance_to[24];
    uint8_t tentacle_count;
    uint8_t fallback_ticks;
} RcPrimParamsSpawnWallTentacles;

typedef struct __attribute__((packed)) {
    uint8_t cloud_count;
    uint8_t damage_max;
    uint8_t safe_tile_exists;
    uint8_t boss_untargetable_during;
} RcPrimParamsTeleportingTileAoe;

typedef struct __attribute__((packed)) {
    uint8_t spike_count_min;
    uint8_t spike_count_max;
    uint8_t harden_ticks;
    uint8_t damage_on_hit;
} RcPrimParamsHomingProjectiles;

typedef struct __attribute__((packed)) {
    uint16_t shield_hp;
    uint8_t hits_to_drain;
    uint8_t sapphire_bonus_drain;
} RcPrimParamsSmiteDrainShield;

typedef struct __attribute__((packed)) {
    uint16_t every_n_attacks;
    uint8_t cluster_tiles;
    uint8_t one_on_player;
    uint8_t damage_on_hit;
} RcPrimParamsPeriodicSpikeCluster;

typedef struct __attribute__((packed)) {
    uint8_t hazard_count;
    uint8_t orbit_radius;
    uint8_t rotation_ticks;
    uint8_t damage_on_hit;
} RcPrimParamsMovingRotationalHazards;

typedef struct __attribute__((packed)) {
    uint8_t warning_ticks;
    uint8_t heal_per_attack;
    uint8_t cancel_player_attack;
} RcPrimParamsHealOnAttack;

typedef struct __attribute__((packed)) {
    uint8_t tile_count;
    uint8_t damage_on_step;
    int8_t debuff_amount;
} RcPrimParamsTileDebuffOnStand;

typedef struct __attribute__((packed)) {
    char npc_name[32];
    uint8_t buff_aoe_size;
    uint8_t minimum_damage_pct;
    uint8_t outside_damage_pct;
} RcPrimParamsSpawnBuffZoneNpc;

typedef struct __attribute__((packed)) {
    uint8_t tile_count;
    uint8_t damage_max;
} RcPrimParamsOneShotArenaEffect;

typedef struct __attribute__((packed)) {
    uint8_t max_sanity;
    uint8_t drain_per_tentacle_hit;
    uint8_t drain_per_wrong_prayer_tick;
    uint8_t regen_per_correct_prayer_tick;
    uint8_t insane_threshold;
    uint8_t restored_on_boss_death;
} RcPrimParamsPlayerSanityTracker;

typedef struct __attribute__((packed)) {
    uint8_t tentacle_count;
    uint8_t warning_ticks;
    uint8_t damage_on_hit;
    uint8_t sanity_drain_on_hit;
} RcPrimParamsSpawnTentacles;

typedef struct __attribute__((packed)) {
    uint8_t warning_ticks;
    uint8_t wrong_prayer_damage;
    uint8_t wrong_prayer_sanity_drain;
} RcPrimParamsPrayerDemand;

typedef struct __attribute__((packed)) {
    uint8_t duration_ticks;
    uint8_t disables_audio_cues;
    uint8_t hides_overhead_warning;
} RcPrimParamsAudioVisualDisruption;

typedef struct __attribute__((packed)) {
    char advance_to[24];
    uint8_t wrong_herb_damage;
    uint8_t wrong_herb_delay_ticks;
    uint8_t correct_herbs_to_wake;
} RcPrimParamsObjectItemInteraction;

typedef struct __attribute__((packed)) {
    uint8_t heal_per_tick;
    uint8_t cancelled_by_correct_herb;
} RcPrimParamsPassiveHealDuringPhase;

typedef struct __attribute__((packed)) {
    uint16_t every_n_attacks;
    uint8_t style_count;
    uint8_t styles[4];
} RcPrimParamsAttackCounterStyleSwap;

typedef struct __attribute__((packed)) {
    uint8_t tornado_count;
    uint8_t damage_on_contact;
    uint8_t duration_ticks;
} RcPrimParamsSpawnTrackingTornadoes;

typedef struct __attribute__((packed)) {
    uint8_t marker;
} RcPrimParamsForcedPrayerSwitch;

typedef struct __attribute__((packed)) {
    uint8_t limb_count;
    char limb_names[3][16];
    uint16_t limb_hp[3];
} RcPrimParamsMultiLimbBoss;

typedef struct __attribute__((packed)) {
    uint8_t min_distance;
} RcPrimParamsPlayerPositionSwap;

typedef struct __attribute__((packed)) {
    uint8_t wall_length;
    uint8_t damage_on_cross;
} RcPrimParamsEnvironmentalWall;

typedef struct __attribute__((packed)) {
    uint8_t telegraph_ticks;
    uint8_t damage_on_tile;
    uint8_t tiles_per_volley;
} RcPrimParamsTileLightning;

typedef struct __attribute__((packed)) {
    uint8_t heal_per_tick;
    char interrupt_object[32];
    uint8_t interrupt_cooldown_ticks;
} RcPrimParamsContinuousHeal;

typedef struct __attribute__((packed)) {
    char item[32];
    uint8_t effective_max_hit;
    char removed_at_phase[24];
} RcPrimParamsOneShotWeapon;

typedef struct __attribute__((packed)) {
    uint8_t pillar_count;
    uint16_t pillar_hp;
    uint8_t damaged_per_electrify;
} RcPrimParamsDestructiblePillars;

typedef struct __attribute__((packed)) {
    uint8_t web_tile_count;
    uint8_t immobilizes_player_ticks;
} RcPrimParamsSpawnWebTiles;

typedef struct __attribute__((packed)) {
    uint8_t color_count;
    uint8_t heals_boss;
    uint8_t heal_per_tick;
} RcPrimParamsSpawnColoredNylocas;

typedef struct __attribute__((packed)) {
    uint8_t duration_ticks;
    uint8_t tile_count_per_spawn;
    uint8_t dot_per_tick;
} RcPrimParamsPersistentDotTilePool;

typedef struct __attribute__((packed)) {
    uint16_t obelisk_hp;
    uint16_t time_limit_ticks;
} RcPrimParamsObeliskDpsCheck;

typedef struct __attribute__((packed)) {
    uint8_t pylon_count;
    uint8_t buff_range_tiles;
    uint8_t buff_damage_pct;
} RcPrimParamsSpawnPylons;

typedef struct __attribute__((packed)) {
    uint8_t wave_tile_count;
    uint16_t damage_on_tile;
} RcPrimParamsDeathTileWave;

typedef struct __attribute__((packed)) {
    uint8_t altar_count;
    uint8_t heal_per_altar_per_tick;
    uint8_t disable_on_click;
    uint8_t respawn_ticks;
} RcPrimParamsHealAltars;

// Encounter spec — immutable after load and shared by every world.
#define RC_ENC_MAX_ATTACKS        16
#define RC_ENC_MAX_PHASES         8
#define RC_ENC_MAX_MECHANICS      16
#define RC_ENC_MAX_FORMS          4
#define RC_ENC_MAX_HELPERS        4
#define RC_ENC_MAX_OBJECTS        4
#define RC_ENC_MAX_GUARDS         4
#define RC_ENC_NO_OBJECT          0xFFu
#define RC_ENC_MAX_PROTECTIONS    16
#define RC_ENC_MAX_DAMAGE_MODS    16
#define RC_ENC_REGISTRY_CAP       64
#define RC_ENC_MAX_RUN_HELPERS    8

enum {
    RC_ENC_LETHAL_DIE = 0,          // ordinary death, loot and respawn
    RC_ENC_LETHAL_TRANSFORM = 1,    // nonterminal: enter next_form instead
};

enum {
    RC_ENC_FORM_HP_FULL = 0,
    RC_ENC_FORM_HP_KEEP = 1,
};

enum {
    RC_ENC_DISENGAGE_KEEP = 0,      // form/HP persist when the fight ends
    RC_ENC_DISENGAGE_RESET = 1,     // return to form 0 at full HP
};

// One boss form. Form 0 is the base definition the NPC spawns as.
typedef struct {
    uint32_t npc_id;                // B237 definition cache id
    char name[64];                  // expected definition name
    bool binds_on_spawn;            // spawning this definition starts a run
    bool dormant;                   // no attacks or mechanics until woken
    uint8_t on_lethal;              // RC_ENC_LETHAL_*
    uint8_t next_form;              // transform or wake target
    uint8_t next_hp;                // RC_ENC_FORM_HP_* entering next_form
    uint16_t transition_ticks;      // idle, untargetable before next_form
    uint16_t revert_after_ticks;    // 0 = never
    uint8_t revert_form;            // form entered when the timer expires
    uint8_t revert_hp;              // RC_ENC_FORM_HP_* entering revert_form
    // Defence bonuses while this form is current, by RcCombatStyle - 1
    // (stab, slash, crush, ranged, magic); all 0 = none.
    int16_t defence_bonus[5];
    // Never walks, as OSRS's moverestrict nomove: its routes are dropped,
    // so it attacks only what its attack ranges reach.
    bool anchored;
} RcEncounterForm;

// Helper definition owned by the run that spawns it. Helpers never bind
// their own run and never respawn.
typedef struct {
    uint32_t npc_id;
    char name[64];                  // expected definition name
} RcEncounterHelper;

// World-spawned NPC that belongs to the boss's room: a guard whose spawn
// lies in the spec's arena never respawns on its own; it returns when the
// boss next binds (respawns).
typedef struct {
    uint32_t npc_id;
    char name[64];                  // expected definition name
} RcEncounterGuard;

// Loc a mechanic spawns into the world (B237 object id, checked against
// its expected definition name like NPC roles).
typedef struct {
    uint32_t loc_id;
    char name[64];                  // expected definition name
    uint8_t loc_type;               // placement type, 0..22
} RcEncounterObject;

typedef struct {
    char slug[48];
    char name[64];
    uint8_t support;                // RC_ENC_SUPPORT_*
    uint8_t disengage;              // RC_ENC_DISENGAGE_*
    uint16_t respawn_ticks;         // 0 = definition policy
    // Room the boss and its guards fight in: they only engage a player
    // inside it (inclusive bounds) and drop one who leaves.
    bool has_arena;
    uint16_t arena_min_x, arena_min_y, arena_max_x, arena_max_y;
    uint8_t arena_plane;

    RcEncounterForm forms[RC_ENC_MAX_FORMS];
    uint8_t form_count;

    RcEncounterHelper helpers[RC_ENC_MAX_HELPERS];
    uint8_t helper_count;

    RcEncounterObject objects[RC_ENC_MAX_OBJECTS];
    uint8_t object_count;

    RcEncounterGuard guards[RC_ENC_MAX_GUARDS];
    uint8_t guard_count;

    RcEncounterAttack attacks[RC_ENC_MAX_ATTACKS];
    uint8_t attack_count;

    RcEncounterPhase phases[RC_ENC_MAX_PHASES];
    uint8_t phase_count;

    RcEncounterMechanic mechanics[RC_ENC_MAX_MECHANICS];
    uint8_t mechanic_count;

    RcEncounterProtection protections[RC_ENC_MAX_PROTECTIONS];
    uint8_t protection_count;

    RcEncounterDamageModifier damage_mods[RC_ENC_MAX_DAMAGE_MODS];
    uint8_t damage_mod_count;
} RcEncounterSpec;

enum {
    RC_ENC_RUN_BOSS_IMMUNE    = 1u << 0,   // boss takes no damage
    RC_ENC_RUN_PLAYER_FROZEN  = 1u << 1,   // player held while flag is set
    RC_ENC_RUN_BOSS_IDLE      = 1u << 2,   // boss does not attack
};

// One fight. A run starts when a binding form spawns and owns every
// timer, counter, helper and hazard of that fight. `generation` is
// unique per world so stale references to a reused slot are detected.
typedef struct {
    bool active;
    uint32_t generation;
    uint16_t spec_idx;
    RcNpcId boss_id;               // NPC handle
    int boss_slot;                 // cached npcs[] index, revalidated on use
    uint32_t def_id;               // current form definition cache id
    uint8_t form;                  // current form index
    uint8_t current_phase;
    bool engaged;
    RcTick bound_tick;
    RcTick engaged_tick;
    RcTick form_entered_tick;
    RcTick transition_until;       // >0: enter transition_form at this tick
    uint8_t transition_form;
    uint8_t transition_hp;
    uint16_t attack_count;
    uint8_t attack_special_toggle;
    uint8_t last_attack_idx;
    uint8_t active_mechanic_idx;
    uint8_t invoking_mechanic_idx;
    uint16_t active_mechanic_ticks;
    uint16_t mechanic_progress;
    uint16_t shield_points;
    uint8_t script_flags;
    uint8_t run_flags;             // RC_ENC_RUN_*
    uint8_t damage_taken_pct;      // boss damage scale while a window runs
    uint8_t effect_count;          // live effects owned by this run
    uint8_t calm_ticks;            // consecutive ticks with no fight
    uint32_t ticks_since_start;
    RcTick next_due[RC_ENC_MAX_MECHANICS];
    RcNpcId helpers[RC_ENC_MAX_RUN_HELPERS];
    uint8_t helper_count;
    uint8_t boss_hits;             // hits taken since the last encounter tick
} RcActiveEncounter;

#define RC_ENC_MAX_ACTIVE 16
#define RC_ENC_SCRIPT_REGISTRY_CAP 128

typedef struct {
    char name[48];
    RcEncounterScriptFn fn;
} RcEncounterScriptEntry;

typedef struct {
    // Immutable shared specs (game data, or a caller-owned test set).
    const RcEncounterSpec *specs;
    uint8_t spec_count;
    uint32_t next_generation;
    uint64_t refused_specs;         // specs refused at bind, diagnosed once
    uint64_t arena_specs;           // specs with an arena (engagement rule)
    // Definitions of the loaded specs' anchored forms (never walk).
    uint32_t anchored_ids[RC_ENC_REGISTRY_CAP * RC_ENC_MAX_FORMS];
    uint16_t anchored_count;

    RcActiveEncounter active[RC_ENC_MAX_ACTIVE];
    uint16_t active_mask;           // bit i set while active[i] is in use

    // Helpers of ended or disengaged runs. The encounter tick removes them,
    // never the NPC lifecycle event that ended the run.
    RcNpcId helper_removals[RC_ENC_MAX_ACTIVE * RC_ENC_MAX_RUN_HELPERS];
    uint8_t helper_removal_count;

    // rc-content script registry. Names come from phase `script = "..."`
    // fields in the compiled encounter data.
    RcEncounterScriptEntry scripts[RC_ENC_SCRIPT_REGISTRY_CAP];
    uint8_t script_count;

    // Stats for observability + testing.
    uint32_t started_count;
    uint32_t finished_count;
    uint32_t scripts_called;
    uint32_t refused_count;
    uint32_t triggered_mechanics;
} RcEncounterState;

// True when definition `def_id` is an anchored form of a loaded spec: the
// NPC never walks. Inline: NPC movement asks for every step.
static inline bool rc_encounter_def_anchored(const RcEncounterState *s,
                                             uint32_t def_id) {
    for (int i = 0; i < s->anchored_count; i++)
        if (s->anchored_ids[i] == def_id) return true;
    return false;
}

// Lifecycle
int rc_encounter_init(struct RcWorld *world);
void rc_encounter_tick(struct RcWorld *world);

// Point a world at caller-owned immutable specs (tests and tools). The
// array must outlive the world or the next call; runs are cleared.
int rc_encounter_use_specs(struct RcWorld *world, const RcEncounterSpec *specs,
                           int count);
const RcEncounterSpec *rc_encounter_spec(const struct RcWorld *world,
                                         int spec_idx);

// Event hooks — wired up at rc_encounter_init via rc_event_subscribe.
// Exposed here so tests can invoke them directly.
void rc_encounter_on_npc_spawned(struct RcWorld *world, int evt,
                                 const void *payload, void *ctx);
void rc_encounter_on_npc_removed(struct RcWorld *world, int evt,
                                 const void *payload, void *ctx);
void rc_encounter_on_npc_died(struct RcWorld *world, int evt,
                              const void *payload, void *ctx);
void rc_encounter_on_player_damaged(struct RcWorld *world, int evt,
                                    const void *payload, void *ctx);
void rc_encounter_on_npc_attack(struct RcWorld *world, int evt,
                                const void *payload, void *ctx);
void rc_encounter_on_npc_damaged(struct RcWorld *world, int evt,
                                 const void *payload, void *ctx);
void rc_encounter_on_player_died(struct RcWorld *world, int evt,
                                 const void *payload, void *ctx);

int rc_encounter_register_script(struct RcWorld *world,
                                 const char *name,
                                 RcEncounterScriptFn fn);
RcEncounterScriptFn rc_encounter_script_lookup(const struct RcWorld *world,
                                               const char *name);

// Load all-or-fail from `data/defs/encounters.bin` (ENCT v14). Returns
// the spec count, or -1 on any malformed, over-capacity or unsupported
// content (nothing is partially published).
int rc_encounter_load_specs(const char *path, RcEncounterSpec *out, int max);
// Check role identities against loaded definitions. `npc_name` and
// `object_name` return the definition name for a cache id, or NULL when
// missing; a NULL `object_name` skips object roles (no object data).
// Returns 1 when every role resolves to its expected name.
typedef const char *(*RcEncounterDefNameFn)(const void *ctx, uint32_t id);
int rc_encounter_validate_roles(const RcEncounterSpec *specs, int count,
                                RcEncounterDefNameFn npc_name,
                                RcEncounterDefNameFn object_name,
                                const void *ctx, char *message,
                                int message_capacity);

// Primitive lookup (encounter_prims.c). Returns NULL for unknown or
// unsupported primitives; the loader rejects specs that reference them.
RcEncounterPrimFn rc_encounter_prim_lookup(uint8_t primitive_id);
// Presentation parts a supported primitive records for its mechanic as
// combat attack events (RC_COMBAT_ACTION_ENCOUNTER): the key is the
// mechanic name for part "" and "<mechanic>:<part>" otherwise.
// NULL-terminated; NULL when the primitive records nothing itself.
const char *const *rc_encounter_prim_parts(uint8_t primitive_id);
// Parts the mechanic's helpers record from themselves (source = the helper,
// e.g. spawn_owned_helpers' "explode"), NULL-terminated; NULL when none.
const char *const *rc_encounter_prim_helper_parts(uint8_t primitive_id);
// Presentation keys for form changes, recorded by the form they belong to:
// the leaving form when its transition starts (hit_delay = the transition's
// ticks, 0 when immediate) and the form that takes over. Attack and mechanic
// names never contain ':' and no mechanic is named "form", so these keys
// never collide with attacks or mechanic parts.
#define RC_ENC_FORM_LEAVE_KEY "form:leave"
#define RC_ENC_FORM_ENTER_KEY "form:enter"
int rc_encounter_set_phase(struct RcWorld *world, int active_idx,
                           const char *phase_id);

// Run slot for an NPC handle, or -1.
int rc_encounter_run_for_npc(const struct RcWorld *world, RcNpcId npc_uid);
// Attack selection results below zero.
enum {
    RC_ENC_SELECT_NONE = -1,        // no encounter: NDEF default attack
    RC_ENC_SELECT_SPECIAL = -2,     // a special used this attack turn
    RC_ENC_SELECT_HOLD = -3,        // boss idles; no attack this tick
    RC_ENC_SELECT_APPROACH = -4,    // only melee-range attacks; close in
};
// Chooses the encounter attack for a launch. Returns the attack index
// (with *attack and *generation set) or an RC_ENC_SELECT_* value.
// `distance` is the player's distance from the boss footprint and
// `side_adjacent` whether ordinary melee reaches the player; attacks with
// RC_ENC_ATTACK_DIAGONAL_REACH also count diagonal contact. A special
// runs its mechanic here and counts as this attack turn.
int rc_encounter_select_npc_attack(struct RcWorld *world, RcNpcId npc_uid,
                                   int distance, bool side_adjacent,
                                   const RcEncounterAttack **attack,
                                   uint32_t *generation);
// Attack `attack_idx` of the run bound to `npc_uid`; NULL when absent.
const RcEncounterAttack *rc_encounter_attack(const struct RcWorld *world,
                                             RcNpcId npc_uid, int attack_idx,
                                             uint32_t *generation);
// Attack-specific protection percent for a landed hit (`attack` is the
// hit's encounter attack index + 1, `generation` its run generation), or
// -1 for default protection.
int rc_encounter_player_protection_scale_pct(const struct RcWorld *world,
                                             int source_uid, int attack,
                                             uint32_t generation,
                                             uint32_t prayer_snapshot);
void rc_encounter_player_attack_committed(struct RcWorld *world, RcNpcId npc_uid);
// Launch-time scaling of player damage against a run's boss: immunity,
// window damage scale and spec damage modifiers.
int rc_encounter_scale_player_damage(struct RcWorld *world,
                                     RcNpcId npc_uid, uint8_t style,
                                     int damage);
// True when `spell_key` (spell index + 1) kills this owned helper outright.
bool rc_encounter_player_instakill(const struct RcWorld *world,
                                   RcNpcId npc_uid, int spell_key);
bool rc_encounter_player_can_target_npc(const struct RcWorld *world,
                                        RcNpcId npc_uid);
// False when an NPC of definition `def_id` spawned at the given tile is a
// boss form or guard of a spec whose arena holds that spawn, and the player
// is outside the arena: it neither aggroes nor keeps fighting.
bool rc_encounter_npc_may_engage(const struct RcWorld *world, uint32_t def_id,
                                 int spawn_x, int spawn_y, int spawn_plane);
// True while the NPC is a boss in a dormant form (asleep until woken, its
// wake transition included): it neither fights nor faces anyone.
bool rc_encounter_npc_dormant(const struct RcWorld *world, RcNpcId npc_uid);
// Called before an NPC dies. Returns 1 when the encounter takes the
// lethal hit as a nonterminal form change (no death, loot or event).
int rc_encounter_intercept_lethal(struct RcWorld *world, RcNpcId npc_uid);
// Content-owned wake actions (e.g. an NPC option) start a dormant fight.
int rc_encounter_wake(struct RcWorld *world, RcNpcId npc_uid);
// True when both NPCs belong to one run (its boss or owned helpers), so
// single-combat locks treat them as one opponent.
bool rc_encounter_same_fight(const struct RcWorld *world, RcNpcId a, RcNpcId b);
// Ends any run for the NPC and binds a fresh one from its current
// definition. Callers that restore an NPC's life outside the respawn path
// (tools, dev transports) use this. Returns 1 when a run is bound.
int rc_encounter_restart(struct RcWorld *world, RcNpcId npc_uid);
// Primitive support. Helpers spawned here belong to run `enc_idx`:
// they never respawn, never bind a run and are removed with it.
int rc_encounter_spawn_helper(struct RcWorld *world, int enc_idx,
                              uint8_t helper, int x, int y, int plane,
                              RcNpcId *out_uid);
// Per-tick work of the open mechanic window (encounter_prims.c).
void rc_encounter_prim_window_tick(struct RcWorld *world, int enc_idx,
                                   const RcEncounterMechanic *mech);
// Opens the run's mechanic window for `mechanic_idx`. `ticks` 0 keeps it
// open until the window's helpers are gone.
void rc_encounter_begin_window(struct RcWorld *world, int enc_idx,
                               uint8_t mechanic_idx, uint16_t ticks,
                               uint8_t run_flags, uint8_t damage_taken_pct);
// Pool on a tile that lands `delay` ticks from now. Once landed it lives
// `ticks` ticks; standing on it deals 1..damage_max per tick and heals the
// owner `leech_pct` of that. `loc_id` >= 0 is shown as a loc of
// `loc_type` while the pool lives. Returns the effect slot or -1.
int rc_encounter_add_pool(struct RcWorld *world, int enc_idx, int x, int y,
                          int plane, int delay, int ticks, uint8_t damage_max,
                          uint8_t leech_pct, int loc_id, uint8_t loc_type);
// Helper `helper` landing on a tile `delay` ticks from now; 0 spawns it at
// once. Returns 1 when the helper spawned or is in flight.
int rc_encounter_add_helper_drop(struct RcWorld *world, int enc_idx,
                                 uint8_t helper, int x, int y, int plane,
                                 int delay);
// Warned strike on a tile, resolved against the player's position at
// impact. Returns the effect slot or -1.
int rc_encounter_add_strike(struct RcWorld *world, int enc_idx,
                            int x, int y, int plane, int delay,
                            uint16_t min_damage, uint16_t max_damage,
                            uint8_t adjacent_pct, uint8_t style);
// Adds a hazard owned by run `enc_idx`; fails when the world pool or
// the run quota is full.
int rc_encounter_add_effect(struct RcWorld *world, int enc_idx, uint8_t kind,
                            int x, int y, int plane,
                            int target_x, int target_y,
                            int ticks, uint8_t style, uint8_t damage_per_tick,
                            const char *name, const char *target_name);
void rc_encounter_tick_effects(struct RcWorld *world);

#endif
