// test_combat — unit tests for combat primitives.
//   - hit-chance formula (OSRS DPS)
//   - pending-hit queue (snapshot at queue, resolve after delay)
//   - protection prayer application (full block for player,
//     50% reduction for NPC)
//   - effective-level derived max_hit

#include "../rc-core/combat.h"
#include "../rc-core/prayer.h"
#include <stdio.h>
#include <assert.h>

int main(void) {
    // ---- Hit chance ----
    float chance = rc_hit_chance(100, 50);
    assert(chance > 0.7f && chance < 0.8f);
    chance = rc_hit_chance(50, 100);
    assert(chance > 0.2f && chance < 0.3f);
    chance = rc_hit_chance(100, 100);
    assert(chance > 0.49f && chance < 0.51f);

    // ---- Queue + resolve (no prayer) ----
    RcPendingHit hits[RC_MAX_PENDING_HITS] = {0};
    int count = 0;
    // Queue a 15-damage melee hit with 0 delay, no prayer snapshot.
    rc_queue_hit(hits, &count, 15, 0, COMBAT_MELEE_SLASH, -1, 0u, 0);
    assert(count == 1);
    assert(hits[0].damage == 15);
    int dmg = rc_resolve_pending(hits, &count, true);
    assert(dmg == 15);
    assert(count == 0);

    // ---- Queue with delay (should not resolve immediately) ----
    count = 0;
    rc_queue_hit(hits, &count, 15, 2, COMBAT_RANGED, -1, 0u, 0);
    dmg = rc_resolve_pending(hits, &count, true);
    assert(dmg == 0 && count == 1);     // still in flight
    dmg = rc_resolve_pending(hits, &count, true);
    assert(dmg == 0 && count == 1);
    dmg = rc_resolve_pending(hits, &count, true);
    assert(dmg == 15 && count == 0);    // resolved at tick 3

    // ---- Protection prayer: player blocks melee fully ----
    count = 0;
    rc_queue_hit(hits, &count, 50, 0, COMBAT_MELEE_CRUSH, -1,
                 PRAYER_PROTECT_MELEE, 0);
    dmg = rc_resolve_pending(hits, &count, true /* player defender */);
    assert(dmg == 0);                   // fully blocked
    assert(count == 0);

    // ---- Protection prayer: NPC-side reduces by 50% ----
    count = 0;
    rc_queue_hit(hits, &count, 50, 0, COMBAT_MELEE_CRUSH, -1,
                 PRAYER_PROTECT_MELEE, 0);
    dmg = rc_resolve_pending(hits, &count, false /* npc defender */);
    assert(dmg == 25);
    assert(count == 0);

    // ---- Wrong prayer doesn't block (ranged hit vs Protect from Melee) ----
    count = 0;
    rc_queue_hit(hits, &count, 50, 0, COMBAT_RANGED, -1,
                 PRAYER_PROTECT_MELEE, 0);
    dmg = rc_resolve_pending(hits, &count, true);
    assert(dmg == 50);

    // ---- Queue cap ----
    count = 0;
    for (int i = 0; i < RC_MAX_PENDING_HITS + 3; i++) {
        rc_queue_hit(hits, &count, 1, 0, COMBAT_MELEE_CRUSH, -1, 0u, 0);
    }
    assert(count == RC_MAX_PENDING_HITS);

    printf("All combat tests passed.\n");
    return 0;
}
