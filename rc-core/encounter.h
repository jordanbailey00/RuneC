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
// loaded into a registry at startup. The current frontier is simple
// trigger plumbing for mechanics bound to named phase-enter / phase-exit
// transitions reached by the existing HP%-based phase model. Richer
// phase semantics (hard-hp zero transitions, timed transitions,
// `enter_after`, script dispatch) remain deferred.

struct RcWorld;

// Primitive function signature. All primitives take the world +
// active encounter state; params are primitive-specific and cast
// from a void* per the registry.
typedef void (*RcEncounterPrimFn)(struct RcWorld *world, int enc_idx,
                                  const void *params);

// Encounter phase — ordered list per encounter. `enter_at_hp_pct`
// of 0 means "hard HP=0 trigger"; 100 means "fight start".
typedef struct {
    char id[32];
    uint8_t enter_at_hp_pct;       // 100 → fight start; 0 → hard HP=0
    bool hard_hp_trigger;          // distinguishes hp=0 from uninit
    uint32_t allowed_attack_mask;  // bitmask of attack indices
} RcEncounterPhase;

// Attack definition within an encounter (overrides the NDEF default
// when active). `style` maps to RcCombatStyle via the standard enum.
typedef struct {
    char name[48];
    uint8_t style;                 // RcCombatStyle
    uint16_t max_hit;
    uint16_t max_hit_solo;         // 0 = same as max_hit
    uint8_t warning_ticks;
    uint8_t accuracy_roll_idx;     // 0=stab 1=slash 2=crush 3=ranged 4=magic
    uint32_t flags;                // forced_hit, prayer_ignorable, ...
} RcEncounterAttack;

enum {
    RC_ENC_TRIGGER_PERIODIC     = 0,
    RC_ENC_TRIGGER_PHASE_ENTER  = 1,
    RC_ENC_TRIGGER_PHASE_EXIT   = 2,
    RC_ENC_TRIGGER_NONE         = 255,   // unsupported / deferred trigger
};

// Mechanic entry — schedules a primitive on a period or trigger.
typedef struct {
    char name[48];
    RcEncounterPrimFn prim;        // resolved from primitive_id at load
    uint8_t primitive_id;          // enum — see _primitives.md PRIMITIVE_IDS
    uint8_t trigger_type;          // RC_ENC_TRIGGER_*
    uint8_t phase_idx;             // index into phases[], or 0xFF
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
    // ... pass-2+ primitives added incrementally as implemented.
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

// Encounter spec — built once at startup from TOML, lives in the
// registry keyed by `npc_ids[]`.
#define RC_ENC_MAX_ATTACKS        16
#define RC_ENC_MAX_PHASES         8
#define RC_ENC_MAX_MECHANICS      16
#define RC_ENC_MAX_NPC_IDS        8
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
} RcEncounterSpec;

// Active encounter — instance data for a running encounter. One
// per spawned boss that matched a registry entry.
typedef struct {
    bool active;
    uint16_t spec_idx;             // index into registry
    RcNpcId boss_id;               // NPC handle
    uint8_t current_phase;
    uint32_t ticks_since_start;
} RcActiveEncounter;

#define RC_ENC_MAX_ACTIVE 16

typedef struct {
    // Registry — populated once at init.
    RcEncounterSpec registry[RC_ENC_REGISTRY_CAP];
    uint8_t registry_count;

    // Active encounters — created when a registered NPC spawns.
    RcActiveEncounter active[RC_ENC_MAX_ACTIVE];

    // Stats for observability + testing.
    uint32_t started_count;
    uint32_t finished_count;
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

// Registry access — tests use this to inject a minimal encounter
// spec without needing a full TOML pipeline.
int rc_encounter_register(struct RcWorld *world,
                          const RcEncounterSpec *spec);

// Load all encounters from `data/defs/encounters.bin` ('ENCT' magic).
// Populates the registry. Returns number loaded, or -1 on error.
int rc_encounter_load(struct RcWorld *world, const char *path);

// Primitive registry lookup (implemented in encounter_prims.c).
// Returns NULL for primitive_ids the engine hasn't implemented yet.
RcEncounterPrimFn rc_encounter_prim_lookup(uint8_t primitive_id);

// Lookup: returns the registry index for the first spec matching
// `npc_id`, or -1 if no match.
int rc_encounter_find_spec(const struct RcWorld *world,
                           uint32_t npc_id);

#endif
