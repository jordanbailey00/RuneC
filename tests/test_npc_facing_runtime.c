#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "api.h"
#include "config.h"
#include "npc.h"

static RcWorld *facing_world(void) {
    RcWorldConfig cfg = rc_preset_base_only();
    cfg.seed = 17;
    RcWorld *world = rc_world_create_config(&cfg);
    assert(world != NULL);
    return world;
}

static void seed_facing_npc_def(void) {
    memset(g_npc_defs, 0, sizeof(g_npc_defs));
    g_npc_def_count = 1;
    g_npc_defs[0].id = 910000;
    g_npc_defs[0].size = 1;
    g_npc_defs[0].hitpoints = 10;
    g_npc_defs[0].wander_range = 1;
    strcpy(g_npc_defs[0].name, "Facing test");
}

static void test_spawn_has_no_fake_target_facing(void) {
    seed_facing_npc_def();
    RcWorld *world = facing_world();
    int idx = rc_npc_spawn(world, 0, 3200, 3200, 0);
    assert(idx >= 0);
    RcNpc *npc = &world->npcs[idx];
    assert(npc->facing_entity == -1);
    assert(npc->facing_x == -1);
    assert(npc->facing_y == -1);
    rc_world_destroy(world);
}

static void test_wander_keeps_last_move_direction(void) {
    seed_facing_npc_def();
    RcWorld *world = facing_world();
    int idx = rc_npc_spawn(world, 0, 3200, 3200, 0);
    assert(idx >= 0);
    RcNpc *npc = &world->npcs[idx];

    rc_npc_tick(world, npc);

    assert(npc->x == 3201);
    assert(npc->y == 3200);
    assert(npc->facing_entity == -1);
    assert(npc->facing_x == 3202);
    assert(npc->facing_y == 3200);
    rc_world_destroy(world);
}

static void test_repeated_wander_moves_from_current_tile(void) {
    seed_facing_npc_def();
    RcWorld *world = facing_world();
    int idx = rc_npc_spawn(world, 0, 3200, 3200, 0);
    assert(idx >= 0);
    RcNpc *npc = &world->npcs[idx];

    rc_npc_tick(world, npc);
    assert(npc->x == 3201);
    assert(npc->y == 3200);

    for (int i = 0; i < 7; i++)
        rc_npc_tick(world, npc);

    assert(npc->prev_x == 3201);
    assert(npc->prev_y == 3200);
    assert(npc->x == 3200);
    assert(npc->y == 3201);
    rc_world_destroy(world);
}

static void test_idle_return_walks_instead_of_snapping(void) {
    seed_facing_npc_def();
    RcWorld *world = facing_world();
    int idx = rc_npc_spawn(world, 0, 3200, 3200, 0);
    assert(idx >= 0);
    RcNpc *npc = &world->npcs[idx];
    npc->x = 3203;
    npc->y = 3200;
    npc->prev_x = 3203;
    npc->prev_y = 3200;
    npc->wander_timer = 499;

    rc_npc_tick(world, npc);

    assert(npc->prev_x == 3203);
    assert(npc->prev_y == 3200);
    assert(npc->x == 3202);
    assert(npc->y == 3200);
    assert(npc->facing_entity == -1);
    assert(npc->facing_x == 3201);
    assert(npc->facing_y == 3200);
    rc_world_destroy(world);
}

int main(void) {
    test_spawn_has_no_fake_target_facing();
    test_wander_keeps_last_move_direction();
    test_repeated_wander_moves_from_current_tile();
    test_idle_return_walks_instead_of_snapping();
    printf("test_npc_facing_runtime: NPC facing state follows runtime movement.\n");
    return 0;
}
