// test_base_only — verify rc-core can run a world with every
// optional subsystem disabled. Exercises the tick dispatcher's
// bitmask gating (see rc-core/README.md §3 + tick.c).
//
// A zero-subsystem world should:
//   - create successfully via rc_preset_base_only()
//   - advance ticks without crashing
//   - produce identical state across two seeded worlds (determinism)
//   - NOT touch combat / prayer / loot / quest / encounter code paths

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "api.h"
#include "config.h"

int main(void) {
    RcWorldConfig cfg = rc_preset_base_only();
    cfg.seed = 42;

    RcWorld *w = rc_world_create_config(&cfg);
    assert(w != NULL);
    assert(w->enabled == 0);
    assert(w->tick == 0);
    assert(w->events.slots[RC_EVT_NPC_DIED].count == 0);

    // Run 100 ticks — must not crash, must advance tick counter.
    for (int i = 0; i < 100; i++) {
        rc_world_tick(w);
    }
    assert(w->tick == 100);

    // Player position preserved (nothing should move a stationary
    // player in base-only mode).
    assert(w->player.x == 3213);
    assert(w->player.y == 3428);

    // Second world, same seed, same ticks — must produce identical
    // state (determinism per README §13).
    RcWorld *w2 = rc_world_create_config(&cfg);
    assert(w2 != NULL);
    for (int i = 0; i < 100; i++) {
        rc_world_tick(w2);
    }
    assert(w->tick == w2->tick);
    assert(w->player.x == w2->player.x);
    assert(w->player.y == w2->player.y);
    assert(w->rng_state == w2->rng_state);

    rc_world_destroy(w);
    rc_world_destroy(w2);

    // Combat-only preset — should have combat/prayer/equipment/
    // inventory/consumables/encounter on, everything else off.
    RcWorldConfig cbt = rc_preset_combat_only();
    assert(cbt.subsystems & RC_SUB_COMBAT);
    assert(cbt.subsystems & RC_SUB_PRAYER);
    assert(cbt.subsystems & RC_SUB_ENCOUNTER);
    assert(!(cbt.subsystems & RC_SUB_LOOT));
    assert(!(cbt.subsystems & RC_SUB_QUESTS));
    assert(!(cbt.subsystems & RC_SUB_DIALOGUE));
    assert(!(cbt.subsystems & RC_SUB_SHOPS));
    assert(!(cbt.subsystems & RC_SUB_SKILLS));
    assert(!(cbt.subsystems & RC_SUB_SLAYER));

    // Full-game preset — everything on.
    RcWorldConfig full = rc_preset_full_game();
    assert((full.subsystems & RC_SUB_COMBAT) &&
           (full.subsystems & RC_SUB_LOOT) &&
           (full.subsystems & RC_SUB_QUESTS) &&
           (full.subsystems & RC_SUB_DIALOGUE) &&
           (full.subsystems & RC_SUB_SHOPS) &&
           (full.subsystems & RC_SUB_SKILLS) &&
           (full.subsystems & RC_SUB_SLAYER) &&
           (full.subsystems & RC_SUB_ENCOUNTER));

    printf("test_base_only: all base-mode + preset checks passed.\n");
    return 0;
}
