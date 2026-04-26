#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "api.h"
#include "config.h"
#include "npc.h"

static void path_join(char *out, size_t out_sz, const char *rel) {
#ifdef RC_TEST_SOURCE_DIR
    snprintf(out, out_sz, "%s/%s", RC_TEST_SOURCE_DIR, rel);
#else
    snprintf(out, out_sz, "%s", rel);
#endif
}

int main(void) {
    char path[512];
    path_join(path, sizeof(path), "data/defs/npc_defs.bin");

    g_npc_def_count = 0;
    int loaded = rc_load_npc_defs(path);
    assert(loaded > 10000);

    int jad = rc_npc_def_find(3127);
    int zulrah = rc_npc_def_find(2042);
    int zuk = rc_npc_def_find(7706);
    int glyph = rc_npc_def_find(7707);
    int tempoross = rc_npc_def_find(10572);
    int nex = rc_npc_def_find(11278);
    int sol = rc_npc_def_find(12821);
    assert(jad >= 0);
    assert(zulrah >= 0);
    assert(zuk >= 0);
    assert(glyph >= 0);
    assert(tempoross >= 0);
    assert(nex >= 0);
    assert(sol >= 0);
    assert(strcmp(g_npc_defs[jad].name, "TzTok-Jad") == 0);
    assert(strcmp(g_npc_defs[zuk].name, "TzKal-Zuk") == 0);
    assert(strcmp(g_npc_defs[glyph].name, "Ancestral Glyph") == 0);
    assert(g_npc_defs[nex].hitpoints >= 3400);
    assert(g_npc_defs[sol].hitpoints > 0);
    assert(g_npc_defs[jad].model_count > 0);
    assert(g_npc_defs[zuk].model_count > 0);
    assert(g_npc_defs[nex].model_count > 0);

    char spawns_path[512];
    path_join(spawns_path, sizeof(spawns_path), "data/spawns/world.npc-spawns.bin");
    RcWorldConfig spawn_cfg = rc_preset_base_only();
    RcWorld *spawn_world = rc_world_create_config(&spawn_cfg);
    assert(spawn_world != NULL);
    int spawned = rc_load_npc_spawns(spawn_world, spawns_path);
    assert(spawned > 20000);
    assert(spawn_world->npc_count == spawned);
    rc_world_destroy(spawn_world);

    g_npc_def_count = 0;
    RcWorldConfig cfg = rc_preset_base_only();
    cfg.subsystems = RC_SUB_COMBAT;
    cfg.npc_defs_path = path;
    RcWorld *world = rc_world_create_config(&cfg);
    assert(world != NULL);
    assert(g_npc_def_count > 10000);
    rc_world_destroy(world);

    printf("test_npc_defs_bin: loaded broad NPC definition binary.\n");
    return 0;
}
