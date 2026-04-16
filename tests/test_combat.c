#include "../rc-core/combat.h"
#include <stdio.h>
#include <assert.h>

int main(void) {
    // Test hit chance formula
    // att > def case: 1 - (def+2)/(2*(att+1))
    float chance = rc_hit_chance(100, 50);
    assert(chance > 0.7f && chance < 0.8f);

    // att < def case: att/(2*(def+1))
    chance = rc_hit_chance(50, 100);
    assert(chance > 0.2f && chance < 0.3f);

    // Equal rolls
    chance = rc_hit_chance(100, 100);
    assert(chance > 0.49f && chance < 0.51f);

    // Test pending hit queue
    RcPendingHit hits[RC_MAX_PENDING_HITS] = {0};
    int count = 0;
    rc_queue_hit(hits, &count, 15, 2, COMBAT_MELEE_SLASH, -1);
    assert(count == 1);
    assert(hits[0].damage == 15);
    assert(hits[0].ticks_remaining == 2);

    printf("All combat tests passed.\n");
    return 0;
}
