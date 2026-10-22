#include "api.h"
#include "config.h"
#include "pathfinding.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static RcWorld *make_world(void) {
    RcWorldConfig config = rc_preset_base_only();
    config.seed = 8102;
    RcWorld *world = rc_world_create_config(&config);
    assert(world);

    memset(&world->map, 0, sizeof(world->map));
    world->map.base_region_x = 0;
    world->map.base_region_y = 0;
    world->map.region_width = 1;
    world->map.region_height = 1;
    world->map.region_count = 1;
    world->map.regions[0].loaded = 1;
    world->map.regions[0].region_x = 0;
    world->map.regions[0].region_y = 0;
    world->active_area.active = true;
    world->active_area.origin_x = 0;
    world->active_area.origin_y = 0;
    world->active_area.width = RC_MAPSQUARE_SIZE;
    world->active_area.height = RC_MAPSQUARE_SIZE;
    world->active_area.min_plane = 0;
    world->active_area.max_plane = RC_MAX_PLANES - 1;
    world->player.x = 10;
    world->player.y = 10;
    world->player.prev_x = 10;
    world->player.prev_y = 10;
    world->player.plane = 0;
    world->player.route_entity_width = 1;
    world->player.route_entity_height = 1;
    return world;
}

static uint32_t *flags_at(RcWorld *world, int x, int y) {
    for (int i = 0; i < world->map.region_count; i++) {
        RcRegion *region = &world->map.regions[i];
        if (region->region_x == x / RC_REGION_SIZE
                && region->region_y == y / RC_REGION_SIZE) {
            return &region->tiles[0][x % RC_REGION_SIZE]
                                    [y % RC_REGION_SIZE].collision_flags;
        }
    }
    assert(0 && "test attempted to mutate an absent region");
    return NULL;
}

// A walk click replaces the route like the OSRS game click: a blocked tile
// routes to the nearest reachable tile, and a click with no route stops the
// player (rsmod MoveGameClickHandler, moveNear; PlayerMovementProcessor
// consumeRoute).
static void test_walk_click_replaces_route(void) {
    RcWorld *world = make_world();
    assert(rc_player_walk_to(world, 15, 10));
    rc_world_tick(world);
    assert(world->player.x == 11 && world->player.y == 10);
    assert(world->player.route_target.x == 15);
    assert(!world->player.running);

    // The nearest reachable tile to the blocked one is a single step north.
    *flags_at(world, 11, 12) = COL_BLOCK_WALK;
    assert(rc_player_run_to(world, 11, 12));
    rc_world_tick(world);
    assert(world->player.x == 11 && world->player.y == 11);
    assert(world->player.route_target.x == 11);
    assert(world->player.route_target.y == 12);
    assert(world->player.route_idx >= world->player.route_len);
    assert(world->player.running);
    assert(rc_player_last_command_result(world, NULL)
           == RC_COMMAND_RESULT_EXECUTED);

    // Nothing within the alternative search of the target is reachable:
    // the click still replaces the walk, so the player stops.
    assert(rc_player_walk_to(world, 20, 11));
    rc_world_tick(world);
    assert(world->player.x == 12 && world->player.y == 11);
    for (int x = 30; x <= 50; x++)
        for (int y = 30; y <= 50; y++)
            *flags_at(world, x, y) = COL_BLOCK_WALK;
    assert(rc_player_walk_to(world, 40, 40));
    rc_world_tick(world);
    assert(world->player.x == 12 && world->player.y == 11);
    assert(world->player.route_idx >= world->player.route_len);
    assert(!world->player.running);
    assert(rc_player_last_command_result(world, NULL)
           == RC_COMMAND_RESULT_EXECUTED);

    rc_world_destroy(world);
}

static void test_core_directional_step(void) {
    RcWorld *world = make_world();
    assert(rc_player_step(world, 1, 1));
    rc_world_tick(world);
    assert(world->player.x == 11 && world->player.y == 11);
    assert(world->player.movement_step_count == 1);
    assert(world->player.movement_step_x[0] == 11);
    assert(world->player.movement_step_y[0] == 11);

    *flags_at(world, 12, 11) = COL_BLOCK_WALK;
    assert(rc_player_step(world, 1, 0));
    rc_world_tick(world);
    assert(world->player.x == 11 && world->player.y == 11);
    assert(world->player.movement_step_count == 0);
    assert(rc_player_last_command_result(world, NULL)
           == RC_COMMAND_RESULT_REJECTED_INVALID);

    rc_world_destroy(world);
}

static void test_run_substeps_drain_recovery_and_zero_fallback(void) {
    RcWorld *world = make_world();
    world->player.run_energy = 10000;
    world->player.weight = 64000;
    assert(rc_player_run_to(world, 14, 10));
    rc_world_tick(world);
    assert(world->player.running);
    assert(world->player.x == 12 && world->player.y == 10);
    assert(world->player.movement_step_count == 2);
    assert(world->player.movement_step_x[0] == 11);
    assert(world->player.movement_step_x[1] == 12);
    assert(world->player.run_energy == 9874);

    world->player.x = 20;
    world->player.y = 20;
    world->player.prev_x = 20;
    world->player.prev_y = 20;
    rc_player_route_clear(&world->player, RC_MOVEMENT_NONE);
    world->player.run_energy = 5000;
    world->player.weight = 0;
    assert(rc_player_run_to(world, 21, 20));
    rc_world_tick(world);
    assert(world->player.x == 21 && world->player.y == 20);
    assert(world->player.movement_step_count == 1);
    assert(world->player.run_energy == 5015);

    world->player.x = 30;
    world->player.y = 30;
    world->player.prev_x = 30;
    world->player.prev_y = 30;
    rc_player_route_clear(&world->player, RC_MOVEMENT_NONE);
    world->player.run_energy = 0;
    assert(rc_player_run_to(world, 33, 30));
    rc_world_tick(world);
    assert(!world->player.running);
    assert(world->player.x == 31 && world->player.y == 30);
    assert(world->player.movement_step_count == 1);
    assert(world->player.run_energy == 15);

    rc_world_destroy(world);
}

static void test_partial_route_continues_to_original_target(void) {
    RcWorld *world = make_world();
    world->map.region_height = 2;
    world->map.region_count = 2;
    world->map.regions[1].loaded = 1;
    world->map.regions[1].region_x = 0;
    world->map.regions[1].region_y = 1;
    for (int region_index = 0; region_index < 2; region_index++) {
        RcRegion *region = &world->map.regions[region_index];
        for (int x = 0; x < RC_REGION_SIZE; x++) {
            for (int y = 0; y < RC_REGION_SIZE; y++) {
                region->tiles[0][x][y].collision_flags = COL_BLOCK_WALK;
            }
        }
    }

    const int first_y = 10;
    const int rows = 36;
    for (int row = 0; row < rows; row++) {
        int y = first_y + row * 2;
        for (int x = 10; x <= 40; x++) *flags_at(world, x, y) = 0;
        if (row + 1 < rows)
            *flags_at(world, row % 2 == 0 ? 40 : 10, y + 1) = 0;
    }

    world->player.x = 10;
    world->player.y = first_y;
    world->player.prev_x = world->player.x;
    world->player.prev_y = world->player.y;
    const int target_x = (rows - 1) % 2 == 0 ? 40 : 10;
    const int target_y = first_y + (rows - 1) * 2;
    RcRouteTarget target = rc_route_target_point(target_x, target_y);
    RcRoute partial = rc_find_route(
        &world->map, world->player.x, world->player.y,
        1, 1, world->player.plane, &target, false);
    assert(partial.status == RC_ROUTE_PARTIAL);
    assert(rc_player_route_admit(
        &world->player, &partial, &target, 1, 1, false));

    world->player.x = partial.reached_x;
    world->player.y = partial.reached_y;
    world->player.prev_x = world->player.x;
    world->player.prev_y = world->player.y;
    world->player.route_idx = world->player.route_len;
    world->active_area.origin_x = 0;
    world->active_area.origin_y =
        (world->player.y / RC_MAPSQUARE_SIZE) * RC_MAPSQUARE_SIZE;
    world->active_area.width = RC_MAPSQUARE_SIZE;
    world->active_area.height = RC_MAPSQUARE_SIZE;

    for (int tick = 0; tick < 2000
            && (world->player.x != target_x
                || world->player.y != target_y); tick++) {
        rc_world_tick(world);
    }
    assert(world->player.x == target_x);
    assert(world->player.y == target_y);
    assert(!world->player.route_continue);
    assert(world->player.route_status == RC_ROUTE_EXACT);
    assert(world->player.movement_result == RC_MOVEMENT_ARRIVED);

    rc_world_destroy(world);
}

int main(void) {
    test_walk_click_replaces_route();
    test_core_directional_step();
    test_run_substeps_drain_recovery_and_zero_fallback();
    test_partial_route_continues_to_original_target();
    puts("test_movement_routing_runtime: movement contracts passed.");
    return 0;
}
