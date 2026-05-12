#include <assert.h>
#include <stdio.h>

#include "api.h"
#include "config.h"
#include "npc.h"

#define NPC_PATH RC_TEST_SOURCE_DIR "/data/defs/npc_defs.bin"
#define SPAWN_PATH RC_TEST_SOURCE_DIR "/data/spawns/world.npc-spawns.bin"

int main(void) {
    g_npc_def_count = 0;
    assert(rc_load_npc_defs(NPC_PATH) > 10000);

    RcWorldConfig cfg = rc_preset_base_only();
    RcWorld *world = rc_world_create_config(&cfg);
    assert(world != NULL);
    assert(rc_load_npc_spawns_rect(NULL, SPAWN_PATH, 0, 0, 1, 1, 0, 0) == -1);
    assert(rc_load_npc_spawns_rect(world, SPAWN_PATH, 10, 0, 1, 1, 0, 0) == -1);
    RcNpcSpawnLoadStats stats = {0};
    assert(rc_load_npc_spawns_rect_stats(NULL, SPAWN_PATH,
                                         0, 0, 1, 1, 0, 0, &stats) == -1);
    assert(rc_load_npc_spawns_rect_stats(world, SPAWN_PATH,
                                         0, 0, 1, 1, 0, 0, NULL) == -1);
    int spawned = rc_load_npc_spawns_rect(world, SPAWN_PATH,
                                          3160, 3370, 3260, 3465, 0, 0);
    assert(spawned > 100);
    assert(spawned < 1000);
    assert(world->npc_count == spawned);
    rc_world_destroy(world);

    world = rc_world_create_config(&cfg);
    assert(world != NULL);
    spawned = rc_load_npc_spawns_near(world, SPAWN_PATH, 3213, 3428, 64, 0);
    assert(spawned > 100);
    assert(spawned < 1000);
    assert(world->npc_count == spawned);
    assert(rc_load_npc_spawns_near(world, SPAWN_PATH, 3213, 3428, -1, 0) == -1);
    rc_world_destroy(world);

    world = rc_world_create_config(&cfg);
    assert(world != NULL);
    spawned = rc_load_npc_spawns_rect_stats(world, SPAWN_PATH,
                                            3072, 3264, 3391, 3583, 0, 3,
                                            &stats);
    assert(spawned == 837);
    assert(stats.total_rows == 24110);
    assert(stats.skipped_filter == 23270);
    assert(stats.matched_filter == 840);
    assert(stats.skipped_instance == 3);
    assert(stats.skipped_missing_def == 0);
    assert(stats.spawned == spawned);
    assert(stats.spawned_plane_counts[0] == 775);
    assert(stats.spawned_plane_counts[1] == 38);
    assert(stats.spawned_plane_counts[2] == 23);
    assert(stats.spawned_plane_counts[3] == 1);
    assert(stats.matched_plane_counts[0] == 778);
    rc_world_destroy(world);

    printf("test_spawn_slices_runtime: full-world spawn slicing loaded.\n");
    return 0;
}
