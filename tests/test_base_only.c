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

    memset(&w->map, 0, sizeof(w->map));
    w->map.region_count = 1;
    w->map.regions[0].region_x = 0;
    w->map.regions[0].region_y = 0;
    w->map.regions[0].loaded = 1;
    w->player.x = 3;
    w->player.y = 5;
    w->player.plane = 0;
    w->map.regions[0].tiles[0][5][5].collision_flags = COL_BLOCK_WALK;
    rc_player_walk_to(w, 5, 5);
    assert(w->player.route_len == 0);
    w->map.regions[0].tiles[0][5][5].collision_flags = 0;
    rc_player_walk_to(w, 7, 5);
    assert(w->player.route_len > 0);
    assert(w->player.route_x[w->player.route_len - 1] == 7);
    assert(w->player.route_y[w->player.route_len - 1] == 5);
    w->map.regions[0].tiles[0][8][5].collision_flags = COL_BLOCK_WALK;
    rc_player_walk_to(w, 8, 5);
    assert(w->player.route_len == 0);
    w->player.x = 3213;
    w->player.y = 3428;
    w->player.route_len = 0;
    w->player.route_idx = 0;

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
    assert(cbt.activity_schemas_path != NULL);
    assert(cbt.activity_spawns_path != NULL);
    assert(cbt.normalization_path != NULL);
    assert(!(cbt.subsystems & RC_SUB_LOOT));
    assert(!(cbt.subsystems & RC_SUB_QUESTS));
    assert(!(cbt.subsystems & RC_SUB_DIALOGUE));
    assert(!(cbt.subsystems & RC_SUB_SHOPS));
    assert(!(cbt.subsystems & RC_SUB_SKILLS));
    assert(!(cbt.subsystems & RC_SUB_SLAYER));
    assert(!(cbt.subsystems & RC_SUB_OBJECTS));
    assert(!(cbt.subsystems & RC_SUB_REGIONS));
    assert(!(cbt.subsystems & RC_SUB_STORAGE));
    assert(!(cbt.subsystems & RC_SUB_TRAVERSAL));

    RcWorldConfig skill = rc_preset_skilling_only();
    assert(skill.subsystems & RC_SUB_OBJECTS);
    assert(skill.subsystems & RC_SUB_REGIONS);
    assert(skill.subsystems & RC_SUB_TRAVERSAL);
    assert(skill.subsystems & RC_SUB_SKILLS);
    assert(skill.recipes_path != NULL);
    assert(skill.skill_drops_path != NULL);
    assert(skill.gathering_nodes_path != NULL);
    assert(skill.traversal_edges_path != NULL);

    // Full-game preset — everything on.
    RcWorldConfig full = rc_preset_full_game();
    assert((full.subsystems & RC_SUB_COMBAT) &&
           (full.subsystems & RC_SUB_LOOT) &&
           (full.subsystems & RC_SUB_QUESTS) &&
           (full.subsystems & RC_SUB_DIALOGUE) &&
           (full.subsystems & RC_SUB_SHOPS) &&
           (full.subsystems & RC_SUB_SKILLS) &&
           (full.subsystems & RC_SUB_SLAYER) &&
           (full.subsystems & RC_SUB_ENCOUNTER) &&
           (full.subsystems & RC_SUB_OBJECTS) &&
           (full.subsystems & RC_SUB_REGIONS) &&
           (full.subsystems & RC_SUB_STORAGE) &&
           (full.subsystems & RC_SUB_TRAVERSAL));

    printf("test_base_only: all base-mode + preset checks passed.\n");
    return 0;
}
