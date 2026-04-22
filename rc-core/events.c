#include "events.h"
#include "types.h"
#include <assert.h>
#include <string.h>

void rc_events_init(RcEventBus *bus) {
    memset(bus, 0, sizeof(*bus));
}

int rc_event_subscribe(RcWorld *world, int evt, RcEventFn fn, void *ctx) {
    if (evt <= 0 || evt >= RC_EVT_MAX) return -1;
    RcEventSlot *slot = &world->events.slots[evt];
    if (slot->count >= RC_MAX_EVENT_HANDLERS) return -1;
    slot->handlers[slot->count].fn = fn;
    slot->handlers[slot->count].ctx = ctx;
    slot->count++;
    return 0;
}

int rc_event_unsubscribe(RcWorld *world, int evt, RcEventFn fn,
                         void *ctx) {
    if (evt <= 0 || evt >= RC_EVT_MAX) return -1;
    RcEventSlot *slot = &world->events.slots[evt];
    for (int i = 0; i < slot->count; i++) {
        if (slot->handlers[i].fn == fn && slot->handlers[i].ctx == ctx) {
            // Slide remaining handlers down
            for (int j = i; j + 1 < slot->count; j++) {
                slot->handlers[j] = slot->handlers[j + 1];
            }
            slot->count--;
            return 0;
        }
    }
    return -1;
}

void rc_event_fire(RcWorld *world, int evt, const void *payload) {
    if (evt <= 0 || evt >= RC_EVT_MAX) return;
    // Re-entry guard — catches handlers firing their own event
    // (stack overflow risk + unclear ordering semantics).
    assert(!world->events.dispatching[evt] &&
           "rc_event_fire: re-entry within same event type");
    world->events.dispatching[evt] = 1;
    RcEventSlot *slot = &world->events.slots[evt];
    for (int i = 0; i < slot->count; i++) {
        slot->handlers[i].fn(world, evt, payload, slot->handlers[i].ctx);
    }
    world->events.dispatching[evt] = 0;
}
