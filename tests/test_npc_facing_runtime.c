#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "api.h"
#include "config.h"
#include "npc.h"

static RcWorld *facing_world(void) {
    RcWorldConfig cfg = rc_preset_base_only();
    cfg.seed = 17;
    RcWorld *world = rc_world_create_config(&cfg);
    assert(world != NULL);
    int region_x = 3200 / RC_MAPSQUARE_SIZE;
    int region_y = 3200 / RC_MAPSQUARE_SIZE;
    world->map.base_region_x = region_x;
    world->map.base_region_y = region_y;
    world->map.region_width = 1;
    world->map.region_height = 1;
    world->map.region_count = 1;
    world->map.regions[0].region_x = region_x;
    world->map.regions[0].region_y = region_y;
    world->map.regions[0].loaded = 1;
    world->active_area.active = true;
    world->active_area.origin_x = region_x * RC_MAPSQUARE_SIZE;
    world->active_area.origin_y = region_y * RC_MAPSQUARE_SIZE;
    world->active_area.width = RC_MAPSQUARE_SIZE;
    world->active_area.height = RC_MAPSQUARE_SIZE;
    world->active_area.min_plane = world->player.plane;
    world->active_area.max_plane = world->player.plane;
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

static void test_spawn_uses_explicit_direction(void) {
    seed_facing_npc_def();
    RcWorld *world = facing_world();
    int idx = rc_npc_spawn(world, 0, 3200, 3200, 0);
    assert(idx >= 0);
    RcNpc *npc = &world->npcs[idx];
    // The spawn direction (south by default) goes out once as a face angle.
    assert(npc->facing_entity == -1);
    assert(npc->face_angle == 0);
    assert(npc->face_angle_seq == 1);
    rc_world_destroy(world);
}

static void test_wander_keeps_last_move_direction(void) {
    seed_facing_npc_def();
    RcWorld *world = facing_world();
    int idx = rc_npc_spawn(world, 0, 3200, 3200, 0);
    assert(idx >= 0);
    RcNpc *npc = &world->npcs[idx];

    rc_npc_tick(world, npc);
    rc_npc_movement_tick(world, npc);

    assert(npc->x == 3201);
    assert(npc->y == 3200);
    // Steps send no face angle: the client faces along the step.
    assert(npc->facing_entity == -1);
    assert(npc->face_angle_seq == 1);
    rc_world_destroy(world);
}

static void test_repeated_wander_moves_from_current_tile(void) {
    seed_facing_npc_def();
    RcWorld *world = facing_world();
    int idx = rc_npc_spawn(world, 0, 3200, 3200, 0);
    assert(idx >= 0);
    RcNpc *npc = &world->npcs[idx];

    int moved = 0;
    for (int i = 0; i < 8; i++) {
        int before_x = npc->x;
        int before_y = npc->y;
        rc_npc_tick(world, npc);
        rc_npc_movement_tick(world, npc);
        int dx = npc->x - before_x;
        int dy = npc->y - before_y;
        if (dx || dy) {
            moved++;
            assert(npc->facing_entity == -1);
            assert(npc->face_angle_seq == 1);
        }
    }
    assert(moved > 0);
    assert(abs(npc->x - npc->spawn_x) <= npc->spawn_wander_range);
    assert(abs(npc->y - npc->spawn_y) <= npc->spawn_wander_range);
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
    rc_npc_movement_tick(world, npc);

    assert(npc->prev_x == 3203);
    assert(npc->prev_y == 3200);
    assert(npc->x == 3202);
    assert(npc->y == 3200);
    assert(npc->facing_entity == -1);
    assert(npc->face_angle_seq == 1);
    rc_world_destroy(world);
}

int main(void) {
    test_spawn_uses_explicit_direction();
    test_wander_keeps_last_move_direction();
    test_repeated_wander_moves_from_current_tile();
    test_idle_return_walks_instead_of_snapping();
    printf("test_npc_facing_runtime: spawns send their face angle; steps leave facing to the client.\n");
    return 0;
}
