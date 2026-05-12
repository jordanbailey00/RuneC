#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "activity_spawns.h"
#include "api.h"
#include "config.h"
#include "npc.h"

#define ACTIVITY_SPAWNS_BIN \
    RC_TEST_SOURCE_DIR "/data/defs/activity_spawns.bin"
#define NPC_DEFS_BIN RC_TEST_SOURCE_DIR "/data/defs/npc_defs.bin"

static void write_u32(FILE *f, uint32_t value) {
    assert(fwrite(&value, sizeof(value), 1, f) == 1);
}

int main(void) {
    FILE *f = fopen("/tmp/runec_bad_activity_spawns.bin", "wb");
    assert(f != NULL);
    write_u32(f, 0);
    write_u32(f, 1);
    write_u32(f, 0);
    fclose(f);

    f = fopen("/tmp/runec_short_activity_spawns.bin", "wb");
    assert(f != NULL);
    fputc(0, f);
    fclose(f);

    assert(rc_load_activity_spawns(NULL) == -1);
    assert(rc_load_activity_spawns("/tmp/runec_bad_activity_spawns.bin") == -1);
    assert(rc_load_activity_spawns("/tmp/runec_short_activity_spawns.bin") == -1);
    assert(rc_load_activity_spawns("missing_activity_spawns.bin") == -1);

    RcWorldConfig cfg = rc_preset_combat_only();
    cfg.activity_spawns_path = ACTIVITY_SPAWNS_BIN;
    RcWorld *load_world = rc_world_create_config(&cfg);
    assert(load_world != NULL);
    rc_world_destroy(load_world);

    int loaded = g_rc_activity_spawn_count;
    assert(loaded > 100);
    assert(rc_load_activity_spawns(ACTIVITY_SPAWNS_BIN) == loaded);

    int zulrah_count = 0;
    const RcActivitySpawn *zulrah =
        rc_activity_spawns_for("zulrah", &zulrah_count);
    assert(zulrah != NULL);
    assert(zulrah_count == 16);
    assert(rc_activity_spawn_count_kind("zulrah",
                                        RC_ACTIVITY_SPAWN_POINT) == 4);
    assert(rc_activity_spawn_count_kind("zulrah",
                                        RC_ACTIVITY_SPAWN_DYNAMIC) == 5);
    assert(rc_activity_spawn_count_kind("zulrah",
                                        RC_ACTIVITY_SPAWN_SAFE_TILE) == 7);
    const RcActivitySpawn *north = rc_activity_spawn_find_key(
        "zulrah", RC_ACTIVITY_SPAWN_POINT, "north");
    assert(north != NULL);
    assert(north->x == 2266 && north->y == 3073 && north->local_y == 12);
    const RcActivitySpawn *shrine = rc_activity_spawn_find_key(
        "tempoross", RC_ACTIVITY_SPAWN_OBJECT_ANCHOR, "west_shrine");
    assert(shrine != NULL);
    assert(shrine->object_id == 41236 && shrine->x == 3035);
    assert(rc_activity_spawn_find_object_at("tempoross", 41236, 3035,
                                            2827, 0) == shrine);
    assert(rc_activity_spawn_find_key("tempoross",
                                      RC_ACTIVITY_SPAWN_OBJECT_ANCHOR,
                                      "missing") == NULL);

    assert(rc_activity_spawn_count_kind("tzhaar_fight_cave",
                                        RC_ACTIVITY_SPAWN_REGION) == 5);
    assert(rc_activity_spawn_count_kind("tzhaar_fight_cave",
                                        RC_ACTIVITY_SPAWN_WAVE_REGION_REF)
           == 15);
    const RcActivitySpawn *jad_region =
        rc_activity_spawn_wave_region("tzhaar_fight_cave", 63, 0);
    assert(jad_region != NULL);
    assert(strcmp(jad_region->key, "south_west") == 0);
    jad_region = rc_activity_spawn_wave_region("tzhaar_fight_cave", 63, 9);
    assert(jad_region != NULL);
    assert(strcmp(jad_region->key, "north_west") == 0);
    jad_region = rc_activity_spawn_wave_region("tzhaar_fight_cave", 63, 15);
    assert(jad_region != NULL);
    assert(strcmp(jad_region->key, "south_west") == 0);
    assert(rc_activity_spawn_wave_region("tzhaar_fight_cave", 62, 0) == NULL);
    assert(rc_activity_spawn_wave_region("tzhaar_fight_cave", 63, -1) == NULL);
    assert(rc_activity_spawn_count_kind("inferno",
                                        RC_ACTIVITY_SPAWN_WAVE_POINT) == 9);
    assert(rc_activity_spawn_count_kind("tempoross",
                                        RC_ACTIVITY_SPAWN_OBJECT_ANCHOR)
           == 8);
    assert(!rc_activity_spawn_has_unresolved("nex"));
    assert(!rc_activity_spawn_has_unresolved("fortis_colosseum_sol_heredit"));
    assert(!rc_activity_spawn_has_unresolved("inferno"));
    assert(rc_activity_spawn_count_kind("nex",
                                        RC_ACTIVITY_SPAWN_POINT) == 5);
    const RcActivitySpawn *nex = rc_activity_spawn_find_key(
        "nex", RC_ACTIVITY_SPAWN_POINT, "nex_provisional_spawn");
    assert(nex != NULL);
    assert(nex->npc_id == 11278 && nex->x == 2924 && nex->y == 5203);
    const RcActivitySpawn *fumus = rc_activity_spawn_find_key(
        "nex", RC_ACTIVITY_SPAWN_POINT, "fumus_provisional_spawn");
    assert(fumus != NULL);
    assert(fumus->npc_id == 11283 && fumus->x == 2913 && fumus->y == 5215);
    const RcActivitySpawn *sol = rc_activity_spawn_find_key(
        "fortis_colosseum_sol_heredit", RC_ACTIVITY_SPAWN_WAVE_POINT,
        "wave_12_sol_provisional_spawn");
    assert(sol != NULL);
    assert(sol->npc_id == 12821 && sol->x == 1824 && sol->y == 3109);
    assert(rc_activity_spawn_count_kind("fortis_colosseum_sol_heredit",
                                        RC_ACTIVITY_SPAWN_REGION) == 10);
    assert(rc_activity_spawn_region_contains(
        "fortis_colosseum_sol_heredit",
        "wave_12_reduced_arena_outer_bounds_provisional", 1824, 3109, 0));
    assert(!rc_activity_spawn_region_contains(
        "fortis_colosseum_sol_heredit",
        "wave_12_reduced_arena_outer_bounds_provisional", 1810, 3109, 0));
    assert(rc_activity_spawns_for("missing", NULL) == NULL);

    RcWorld *world = rc_world_create_config(&cfg);
    assert(world != NULL);
    assert(g_rc_activity_spawn_count == loaded);

    if (g_npc_def_count == 0) {
        assert(rc_load_npc_defs(NPC_DEFS_BIN) > 0);
    }
    int start_count = world->npc_count;
    int spawned = rc_activity_spawn_materialize_npcs(
        world, "inferno", RC_ACTIVITY_SPAWN_WAVE_POINT);
    assert(spawned == 6);
    assert(world->npc_count == start_count + spawned);

    spawned = rc_activity_spawn_materialize_npcs(
        world, "wintertodt", RC_ACTIVITY_SPAWN_POINT);
    assert(spawned >= 4);
    spawned = rc_activity_spawn_materialize_npcs(
        world, "zulrah", RC_ACTIVITY_SPAWN_POINT);
    assert(spawned == 0);
    spawned = rc_activity_spawn_materialize_wave_npcs(world, "inferno", 69);
    assert(spawned == 6);
    spawned = rc_activity_spawn_materialize_wave_npcs(
        world, "fortis_colosseum_sol_heredit", 12);
    assert(spawned == 1);
    assert(rc_activity_spawn_materialize_wave_npcs(world, "inferno", 68) == 0);
    assert(rc_activity_spawn_materialize_wave_npcs(world, "inferno", 0) == -1);
    assert(rc_activity_spawn_materialize_npcs(world, "missing", 0) == 0);
    assert(rc_activity_spawn_materialize_npcs(NULL, "inferno", 0) == -1);
    assert(rc_activity_spawn_materialize_wave_npcs(NULL, "inferno", 69) == -1);

    rc_world_destroy(world);
    printf("test_activity_spawns_runtime: activity spawn runtime loaded.\n");
    return 0;
}
