#include "../rc-core/api.h"
#include <stdio.h>
#include <assert.h>

// Simple state hash for determinism checks
static uint32_t hash_world(const RcWorld *world) {
    uint32_t hash = 2166136261u; // FNV offset basis
    const RcPlayer *p = &world->player;

    // Hash player position and HP
    hash ^= (uint32_t)p->x; hash *= 16777619u;
    hash ^= (uint32_t)p->y; hash *= 16777619u;
    hash ^= (uint32_t)p->current_hp; hash *= 16777619u;
    hash ^= (uint32_t)world->tick; hash *= 16777619u;
    hash ^= world->rng_state; hash *= 16777619u;

    return hash;
}

int main(void) {
    // Two worlds with same seed should produce identical state
    RcWorld *w1 = rc_world_create(42);
    RcWorld *w2 = rc_world_create(42);

    for (int i = 0; i < 100; i++) {
        rc_world_tick(w1);
        rc_world_tick(w2);
    }

    uint32_t h1 = hash_world(w1);
    uint32_t h2 = hash_world(w2);
    assert(h1 == h2);

    // Different seed should produce different state
    RcWorld *w3 = rc_world_create(99);
    for (int i = 0; i < 100; i++) {
        rc_world_tick(w3);
    }
    uint32_t h3 = hash_world(w3);
    assert(h1 != h3);

    rc_world_destroy(w1);
    rc_world_destroy(w2);
    rc_world_destroy(w3);

    printf("All determinism tests passed.\n");
    return 0;
}
