#ifndef RC_ENCOUNTER_H
#define RC_ENCOUNTER_H

#include <stdint.h>
#include <stdbool.h>
#include "handles.h"

// Encounter subsystem — runtime dispatcher for boss scripts + mechanic
// primitives. Data comes from `data/curated/encounters/*.toml`
// (authored by hand; see `database.md` and a live encounter TOML such
// as `data/curated/encounters/scurrius.toml` for schema shape).
//
// Encounter specs are compiled into `data/defs/encounters.bin` and
// loaded into a registry at startup. rc-core owns generic dispatch;
// rc-content registers OSRS-specific script functions by name.

struct RcWorld;

// Primitive function signature. All primitives take the world +
// active encounter state; params are primitive-specific and cast
// from a void* per the registry.
typedef void (*RcEncounterPrimFn)(struct RcWorld *world, int enc_idx,
                                  const void *params);
typedef void (*RcEncounterScriptFn)(struct RcWorld *world, int enc_idx);

// Encounter phase — ordered list per encounter. `enter_at_hp_pct`
// of 0 means "hard HP=0 trigger"; 100 means "fight start".
typedef struct {
    char id[32];
    uint8_t enter_at_hp_pct;       // 100 → fight start; 0 → hard HP=0
    bool hard_hp_trigger;          // distinguishes hp=0 from uninit
    uint32_t allowed_attack_mask;  // bitmask of attack indices
    uint8_t adjacent_style_weights[8];
    uint8_t distant_style_weights[8];
    char script[48];               // rc-content script name, optional
    bool player_targetable;         // false = player cannot damage this phase
    bool allowed_attack_mask_explicit;
} RcEncounterPhase;

// Attack definition within an encounter (overrides the NDEF default
// when active). `style` maps to RcCombatStyle via the standard enum.
typedef struct {
    char name[48];
    uint32_t npc_id;               // 0 = applies to any NPC in spec
    uint8_t style;                 // RcCombatStyle
    uint16_t min_hit;
    uint16_t max_hit;
    uint16_t max_hit_solo;         // 0 = same as max_hit
    uint8_t warning_ticks;
    uint8_t accuracy_roll_idx;     // 0=stab 1=slash 2=crush 3=ranged 4=magic
    uint32_t flags;                // forced_hit, prayer_ignorable, ...
    uint8_t effect_id;             // RC_ENC_ATTACK_EFFECT_*
    uint8_t effect_min;
    uint8_t effect_max;
    uint8_t effect_pct;
    uint8_t effect_flags;
} RcEncounterAttack;

enum {
    RC_ENC_ATTACK_FORCED_HIT        = 1u << 0,
    RC_ENC_ATTACK_PRAYER_IGNORABLE  = 1u << 1,
    RC_ENC_ATTACK_REQ_MELEE_RANGE   = 1u << 2,
    RC_ENC_ATTACK_HITS_ALL_ROOM     = 1u << 3,
};

enum {
    RC_ENC_ATTACK_EFFECT_NONE = 0,
    RC_ENC_ATTACK_EFFECT_DRAIN_HEAL = 1,
    RC_ENC_ATTACK_EFFECT_SPLIT_PROJECTILES = 2,
    RC_ENC_ATTACK_EFFECT_DRAIN_PRAYER_PCT = 3,
    RC_ENC_ATTACK_EFFECT_POISON = 4,
    RC_ENC_ATTACK_EFFECT_KNOCKBACK = 5,
    RC_ENC_ATTACK_EFFECT_VENOM = 6,
    RC_ENC_ATTACK_EFFECT_DEACTIVATE_PRAYERS = 7,
};

enum {
    RC_ENC_ATTACK_EFFECT_DRAIN_MAGIC          = 1u << 0,
    RC_ENC_ATTACK_EFFECT_DRAIN_PRAYER         = 1u << 1,
    RC_ENC_ATTACK_EFFECT_POISON_PIERCES_PRAY  = 1u << 2,
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
    RC_ENC_TRIGGER_PERIODIC     = 0,
    RC_ENC_TRIGGER_PHASE_ENTER  = 1,
    RC_ENC_TRIGGER_PHASE_EXIT   = 2,
    RC_ENC_TRIGGER_PHASE_IN     = 3,     // active while phase is current
    RC_ENC_TRIGGER_AFTER_ATTACK = 4,     // hook exists; dispatcher later
    RC_ENC_TRIGGER_DURING_MECH  = 5,     // hook exists; dispatcher later
    RC_ENC_TRIGGER_ATTACK_COUNT = 6,     // hook exists; dispatcher later
    RC_ENC_TRIGGER_EVENT        = 7,     // named event hook
    RC_ENC_TRIGGER_NONE         = 255,
};

// Mechanic entry — schedules a primitive on a period or trigger.
typedef struct {
    char name[48];
    RcEncounterPrimFn prim;        // resolved from primitive_id at load
    uint8_t primitive_id;          // enum — see _primitives.md PRIMITIVE_IDS
    uint8_t trigger_type;          // RC_ENC_TRIGGER_*
    uint8_t phase_idx;             // index into phases[], or 0xFF
    uint32_t phase_mask;           // union trigger phase bitmask; 0 = phase_idx
    char trigger_ref[32];          // attack/mechanic/event name for hook triggers
    uint16_t period_ticks;
    uint16_t ticks_until_next;
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
    char name[32];                 // npc name — strcmp'd against g_npc_defs[].name
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

enum {
    RC_ENC_EFFECT_NONE = 0,
    RC_ENC_EFFECT_TRAVELLING_SOUL = 1,
    RC_ENC_EFFECT_LAVA_POOL = 2,
    RC_ENC_EFFECT_HIDDEN_OBJECT = 3,
    RC_ENC_EFFECT_ROOM_ATTACK = 4,
    RC_ENC_EFFECT_ACID_POOL = 5,
    RC_ENC_EFFECT_FORM_DIVE = 6,
};

typedef struct {
    bool active;
    uint8_t kind;
    uint8_t style;
    uint8_t damage_per_tick;
    uint16_t source_uid;
    int x, y, plane;
    int target_x, target_y;
    int ticks_remaining;          // <0 = persistent until interaction.
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

typedef struct __attribute__((packed)) {
    uint8_t every_n_attacks;
    uint8_t special_count;
    char special_names[2][24];
} RcPrimParamsAttackCounterAlternateSpecial;

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

// Encounter spec — built once at startup from TOML, lives in the
// registry keyed by `npc_ids[]`.
#define RC_ENC_MAX_ATTACKS        16
#define RC_ENC_MAX_PHASES         8
#define RC_ENC_MAX_MECHANICS      16
#define RC_ENC_MAX_NPC_IDS        8
#define RC_ENC_MAX_PROTECTIONS    16
#define RC_ENC_MAX_DAMAGE_MODS    16
#define RC_ENC_REGISTRY_CAP       64

typedef struct {
    char slug[48];
    uint32_t npc_ids[RC_ENC_MAX_NPC_IDS];
    uint8_t npc_id_count;

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

// Active encounter — instance data for a running encounter. One
// per spawned boss that matched a registry entry.
typedef struct {
    bool active;
    uint16_t spec_idx;             // index into registry
    RcNpcId boss_id;               // NPC handle
    uint32_t def_id;               // cache NPC ID for owner-specific data
    uint8_t current_phase;
    uint16_t attack_count;
    uint8_t attack_special_toggle;
    uint8_t last_attack_idx;
    uint8_t active_mechanic_idx;
    uint8_t invoking_mechanic_idx;
    uint16_t active_mechanic_ticks;
    uint16_t mechanic_progress;
    uint16_t shield_points;
    uint8_t script_flags;
    uint32_t ticks_since_start;
} RcActiveEncounter;

#define RC_ENC_MAX_ACTIVE 16
#define RC_ENC_SCRIPT_REGISTRY_CAP 128

typedef struct {
    char name[48];
    RcEncounterScriptFn fn;
} RcEncounterScriptEntry;

typedef struct {
    // Registry — populated once at init.
    RcEncounterSpec registry[RC_ENC_REGISTRY_CAP];
    uint8_t registry_count;

    // Active encounters — created when a registered NPC spawns.
    RcActiveEncounter active[RC_ENC_MAX_ACTIVE];

    // rc-content script registry. Names come from phase `script = "..."`
    // fields in the compiled encounter data.
    RcEncounterScriptEntry scripts[RC_ENC_SCRIPT_REGISTRY_CAP];
    uint8_t script_count;

    // Stats for observability + testing.
    uint32_t started_count;
    uint32_t finished_count;
    uint32_t scripts_called;
    uint32_t script_misses;
    uint32_t triggered_mechanics;
} RcEncounterState;

// Lifecycle
void rc_encounter_init(struct RcWorld *world);
void rc_encounter_tick(struct RcWorld *world);

// Event hooks — wired up at rc_encounter_init via rc_event_subscribe.
// Exposed here so tests can invoke them directly.
void rc_encounter_on_npc_spawned(struct RcWorld *world, int evt,
                                 const void *payload, void *ctx);
void rc_encounter_on_npc_died(struct RcWorld *world, int evt,
                              const void *payload, void *ctx);
void rc_encounter_on_player_damaged(struct RcWorld *world, int evt,
                                    const void *payload, void *ctx);
void rc_encounter_on_phase_transition(struct RcWorld *world, int evt,
                                      const void *payload, void *ctx);
void rc_encounter_on_npc_attack(struct RcWorld *world, int evt,
                                const void *payload, void *ctx);
void rc_encounter_on_npc_damaged(struct RcWorld *world, int evt,
                                 const void *payload, void *ctx);

// Registry access — tests use this to inject a minimal encounter
// spec without needing a full TOML pipeline.
int rc_encounter_register(struct RcWorld *world,
                          const RcEncounterSpec *spec);

int rc_encounter_register_script(struct RcWorld *world,
                                 const char *name,
                                 RcEncounterScriptFn fn);
RcEncounterScriptFn rc_encounter_script_lookup(const struct RcWorld *world,
                                               const char *name);
void rc_encounter_script_noop(struct RcWorld *world, int enc_idx);

// Load all encounters from `data/defs/encounters.bin` ('ENCT' magic).
// Populates the registry. Returns number loaded, or -1 on error.
int rc_encounter_load(struct RcWorld *world, const char *path);

// Primitive registry lookup (implemented in encounter_prims.c).
// Returns NULL for IDs that are explicitly unsupported (0 and out-of-range).
// IDs mapped by a valid enum value but not yet implemented are resolved
// to a no-op helper so mechanics can still be tracked and advanced.
RcEncounterPrimFn rc_encounter_prim_lookup(uint8_t primitive_id);
int rc_encounter_set_phase(struct RcWorld *world, int active_idx,
                           const char *phase_id);

// Lookup: returns the registry index for the first spec matching
// `npc_id`, or -1 if no match.
int rc_encounter_find_spec(const struct RcWorld *world,
                           uint32_t npc_id);
int rc_encounter_select_npc_attack(struct RcWorld *world, uint16_t npc_uid,
                                   int distance, uint8_t *style,
                                   uint16_t *min_hit, uint16_t *max_hit,
                                   uint32_t *flags);
int rc_encounter_player_protection_scale_pct(const struct RcWorld *world,
                                             int source_uid, int style,
                                             uint32_t prayer_snapshot);
int rc_encounter_scale_player_damage(struct RcWorld *world,
                                     uint16_t npc_uid, uint8_t style,
                                     int damage);
int rc_encounter_scale_incoming_damage(struct RcWorld *world,
                                       int source_uid, uint8_t style,
                                       int damage);
bool rc_encounter_player_can_target_npc(const struct RcWorld *world,
                                        uint16_t npc_uid);
int rc_encounter_reveal_hidden_npcs(struct RcWorld *world,
                                    const char *npc_name,
                                    int max_count);
int rc_encounter_add_effect(struct RcWorld *world, uint8_t kind,
                            int x, int y, int plane,
                            int target_x, int target_y,
                            int ticks, uint16_t source_uid,
                            uint8_t style, uint8_t damage_per_tick,
                            const char *name, const char *target_name);
void rc_encounter_tick_effects(struct RcWorld *world);
int rc_encounter_interact_object(struct RcWorld *world, int obj_id,
                                 const char *object_name,
                                 int x, int y, int plane, int opt);

#endif
