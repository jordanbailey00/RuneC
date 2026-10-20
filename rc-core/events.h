#ifndef RC_EVENTS_H
#define RC_EVENTS_H

#include <stdint.h>

// Episodic event bus. See rc-core/README.md §7 for the contract —
// events are for episodic cross-system concerns (NPC death, drop
// granted, phase transition, dialogue start). Per-tick things
// (NPC tick loop, combat damage resolve, pathfinding step) DO NOT
// go through events; those are direct function calls.
//
// Dispatch cost: ~1-5 ns per subscribed handler. Fine at
// hundreds-per-second; catastrophic at per-tick-per-entity. Do
// not abuse.

enum {
    RC_EVT_NPC_DIED = 1,
    RC_EVT_NPC_SPAWNED,
    RC_EVT_NPC_REMOVED,
    RC_EVT_PLAYER_DAMAGED,
    RC_EVT_ITEM_PICKED_UP,
    RC_EVT_DROP_GRANTED,
    RC_EVT_PHASE_TRANSITION,
    RC_EVT_NPC_ATTACK,
    RC_EVT_NPC_DAMAGED,
    RC_EVT_DIALOGUE_OPENED,
    RC_EVT_DIALOGUE_CHOICE,
    RC_EVT_QUEST_STAGE_CHANGED,
    RC_EVT_ITEM_EQUIPPED,
    RC_EVT_ITEM_UNEQUIPPED,
    RC_EVT_SPELL_CAST,
    RC_EVT_PRAYER_TOGGLED,
    RC_EVT_PLAYER_ATTACK,
    RC_EVT_ITEM_DROPPED,
    RC_EVT_PLAYER_DIED,
    RC_EVT_NPC_HIDDEN,
    RC_EVT_NPC_RESPAWNED,
    RC_EVT_MAX
};

// Fwd-decl; the struct tag must match the one in types.h.
struct RcWorld;

// Event handler callback. Payload is event-specific; handlers are
// responsible for casting to the right struct (documented per-event
// in the subsystem that emits). `ctx` is caller-provided at subscribe
// time, typically a pointer to subsystem state.
typedef void (*RcEventFn)(struct RcWorld *world, int evt,
                          const void *payload, void *ctx);

// Public API (world pointer type uses the struct tag, not the typedef,
// to keep this header free of a types.h dependency).
int  rc_event_subscribe(struct RcWorld *world, int evt,
                        RcEventFn fn, void *ctx);
int  rc_event_unsubscribe(struct RcWorld *world, int evt,
                          RcEventFn fn, void *ctx);
int  rc_event_fire(struct RcWorld *world, int evt, const void *payload);

// Max handlers per event. Tune if you subscribe more than 8 handlers
// to a single event in practice.
#define RC_MAX_EVENT_HANDLERS 8

typedef struct {
    struct {
        RcEventFn fn;
        void *ctx;
    } handlers[RC_MAX_EVENT_HANDLERS];
    uint8_t count;
} RcEventSlot;

typedef struct {
    RcEventSlot slots[RC_EVT_MAX];
    uint8_t dispatching[RC_EVT_MAX];   // re-entry guard (dev-assert)
} RcEventBus;

// Lifecycle (called by world_create / world_destroy internally).
void rc_events_init(RcEventBus *bus);

// Canonical event payloads — emitters populate these exactly, and
// handlers cast `payload` void* to the right struct per the event
// enum value.
typedef struct {
    uint32_t npc_id;           // NPC handle (uid)
    uint32_t def_id;           // registry-keyable identifier
    uint64_t spawn_key;        // stable static source identity; 0 = dynamic
} RcPayloadNpcEvent;           // NPC lifecycle events

typedef struct {
    uint32_t source_npc_id;    // attacker uid, or UINT32_MAX for player-self
    uint16_t damage;
    uint16_t current_hp;
    uint16_t max_hp;
    uint8_t style;             // RcCombatStyle
    uint8_t flags;
    int damage_tenths;         // actual HP removed; damage remains whole hitpoints
    uint16_t rolled;           // launch roll before protection/modifiers
    uint8_t enc_attack;        // encounter attack index + 1; zero = none
    uint32_t enc_generation;   // launching encounter run generation
} RcPayloadPlayerDamaged;      // RC_EVT_PLAYER_DAMAGED

typedef struct {
    uint32_t npc_id;           // defender uid
    uint32_t source_npc_id;    // UINT32_MAX for player / non-NPC source
    uint16_t damage;
    uint16_t current_hp;
    uint16_t max_hp;
    uint8_t style;             // RcCombatStyle
} RcPayloadNpcDamaged;         // RC_EVT_NPC_DAMAGED

typedef struct {
    uint32_t npc_id;           // boss uid
    uint8_t old_phase;         // previous phase index
    uint8_t new_phase;         // next phase index
} RcPayloadPhaseTransition;    // RC_EVT_PHASE_TRANSITION

typedef struct {
    uint32_t npc_id;           // attacker uid
    uint8_t style;             // RcCombatStyle
} RcPayloadNpcAttack;          // RC_EVT_NPC_ATTACK

typedef struct {
    uint32_t target_npc_id;    // defender uid
    uint8_t style;             // RcCombatStyle
} RcPayloadPlayerAttack;       // RC_EVT_PLAYER_ATTACK

typedef struct {
    uint32_t item_id;
    uint32_t quantity;
    int ground_uid;
    uint8_t slot;              // RcItemSlot
} RcPayloadItemEvent;          // RC_EVT_ITEM_PICKED_UP, RC_EVT_DROP_GRANTED,
                               // RC_EVT_ITEM_EQUIPPED, RC_EVT_ITEM_UNEQUIPPED,
                               // RC_EVT_ITEM_DROPPED

typedef struct {
    uint32_t transcript_id;
    uint32_t node_id;
    int32_t choice;
} RcPayloadDialogueEvent;

typedef struct {
    uint32_t quest_id;
    int32_t old_stage;
    int32_t new_stage;
} RcPayloadQuestStageChanged;

typedef struct {
    uint32_t spell_id;
    uint32_t target_id;
    uint8_t target_kind;
} RcPayloadSpellCast;

typedef struct {
    uint16_t prayer_id;
    uint8_t enabled;
} RcPayloadPrayerToggled;

typedef struct {
    uint64_t tick;
    int32_t x;
    int32_t y;
    int8_t plane;
} RcPayloadPlayerDeath;

#endif
