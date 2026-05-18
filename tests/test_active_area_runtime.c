#include <assert.h>
#include <stdio.h>

#include "api.h"
#include "collision.h"
#include "config.h"
#include "npc.h"
#include "pathfinding.h"

#define NPC_PATH RC_TEST_SOURCE_DIR "/data/defs/npc_defs.bin"
#define SPAWN_PATH RC_TEST_SOURCE_DIR "/data/spawns/world.npc-spawns.bin"
#define CTIL_PATH RC_TEST_SOURCE_DIR "/data/defs/collision_tiles.bin"

int main(void) {
    g_npc_def_count = 0;
    g_rc_collision_region_count = 0;

    RcWorldConfig cfg = rc_preset_base_only();
    cfg.subsystems = RC_SUB_COMBAT | RC_SUB_REGIONS;
    cfg.npc_defs_path = NPC_PATH;
    cfg.spawns_path = SPAWN_PATH;
    cfg.collision_tiles_path = CTIL_PATH;
    RcWorld *world = rc_world_create_config(&cfg);
    assert(world != NULL);
    assert(g_npc_def_count > 10000);
    assert(rc_collision_is_loaded());

    RcActiveAreaRequest bad = {0};
    assert(rc_world_activate_area(world, &bad, NULL) == -1);

    RcActiveAreaRequest req = {
        .origin_x = 3072,
        .origin_y = 3264,
        .width = 320,
        .height = 320,
        .min_plane = 0,
        .max_plane = RC_MAX_PLANES - 1,
        .flags = RC_ACTIVE_AREA_LOAD_COLLISION
               | RC_ACTIVE_AREA_LOAD_NPCS
               | RC_ACTIVE_AREA_CLEAR_NPCS,
        .npc_spawns_path = SPAWN_PATH,
    };
    RcActiveAreaStats stats;
    assert(rc_world_activate_area(world, &req, &stats) == 1);
    assert(stats.collision_regions > 0);
    assert(stats.collision_regions <= RC_MAX_REGIONS);
    assert(stats.npc_stats.total_rows == 24110);
    assert(stats.npc_stats.matched_filter == 840);
    assert(stats.spawned_npcs == 837);
    assert(world->npc_count == 837);
    assert(world->active_area.active);
    const RcActiveArea *active = rc_world_get_active_area(world);
    assert(active == &world->active_area);
    assert(world->active_area.origin_x == req.origin_x);
    assert(world->active_area.origin_y == req.origin_y);
    assert(world->active_area.width == req.width);
    assert(world->active_area.height == req.height);
    assert(world->active_area.min_plane == 0);
    assert(world->active_area.max_plane == RC_MAX_PLANES - 1);
    assert(rc_get_flags(&world->map, 3213, 3428, 0)
           == rc_collision_flags_at(3213, 3428, 0, NULL));

    uint32_t first_gen = world->active_area.generation;
    int graardor_def = rc_npc_def_find(2215);
    if (graardor_def >= 0) {
        assert(rc_npc_spawn(world, graardor_def, 2872, 5358, 2) >= 0);
        assert(world->npc_count == 838);
    }
    assert(rc_world_activate_area(world, &req, &stats) == 1);
    assert(world->active_area.generation == first_gen + 1);
    assert(world->npc_count == 837);

    RcActiveAreaRequest collision_only = req;
    collision_only.origin_x = 3200;
    collision_only.origin_y = 3392;
    collision_only.width = 64;
    collision_only.height = 64;
    collision_only.flags = RC_ACTIVE_AREA_LOAD_COLLISION;
    assert(rc_world_activate_area(world, &collision_only, &stats) == 1);
    assert(stats.spawned_npcs == 0);
    assert(world->npc_count == 837);
    assert(world->active_area.origin_x == 3200);
    assert(world->active_area.width == 64);

    RcActiveAreaRequest default_flags = req;
    default_flags.flags = 0;
    default_flags.npc_spawns_path = NULL;
    assert(rc_world_activate_area(world, &default_flags, &stats) == 1);
    assert(stats.spawned_npcs == 837);

    rc_world_destroy(world);
    printf("test_active_area_runtime: core-owned active area loaded.\n");
    return 0;
}
