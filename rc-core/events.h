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
    RC_EVT_PLAYER_DAMAGED,
    RC_EVT_ITEM_PICKED_UP,
    RC_EVT_DROP_GRANTED,
    RC_EVT_PHASE_TRANSITION,
    RC_EVT_DIALOGUE_OPENED,
    RC_EVT_DIALOGUE_CHOICE,
    RC_EVT_QUEST_STAGE_CHANGED,
    RC_EVT_ITEM_EQUIPPED,
    RC_EVT_ITEM_UNEQUIPPED,
    RC_EVT_SPELL_CAST,
    RC_EVT_PRAYER_TOGGLED,
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
void rc_event_fire(struct RcWorld *world, int evt, const void *payload);

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
    uint16_t npc_id;           // NPC handle (uid)
    uint32_t def_id;           // registry-keyable identifier
} RcPayloadNpcEvent;           // RC_EVT_NPC_SPAWNED, RC_EVT_NPC_DIED

typedef struct {
    uint16_t source_npc_id;    // attacker uid, or 0xFFFF for player-self
    uint16_t damage;
    uint8_t style;             // RcCombatStyle
} RcPayloadPlayerDamaged;      // RC_EVT_PLAYER_DAMAGED

typedef struct {
    uint16_t npc_id;           // boss uid
    uint8_t old_phase;         // previous phase index
    uint8_t new_phase;         // next phase index
} RcPayloadPhaseTransition;    // RC_EVT_PHASE_TRANSITION

typedef struct {
    uint32_t item_id;
    uint16_t quantity;
    uint8_t slot;              // RcItemSlot
} RcPayloadItemEvent;          // RC_EVT_ITEM_PICKED_UP, RC_EVT_DROP_GRANTED,
                               // RC_EVT_ITEM_EQUIPPED, RC_EVT_ITEM_UNEQUIPPED

#endif
