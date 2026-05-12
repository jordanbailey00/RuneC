#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "activity_schemas.h"
#include "api.h"
#include "config.h"

#define RC_TEST_ACTIVITY_SCHEMAS_BIN \
    RC_TEST_SOURCE_DIR "/data/defs/activity_schemas.bin"

static void write_u32(FILE *f, uint32_t value) {
    assert(fwrite(&value, sizeof(value), 1, f) == 1);
}

static int count_status(uint8_t status) {
    int count = 0;
    for (int i = 0; i < g_rc_activity_schema_count; i++) {
        if (g_rc_activity_schemas[i].status == status) count++;
    }
    return count;
}

int main(void) {
    FILE *f = fopen("/tmp/runec_bad_activity_schema_header.bin", "wb");
    assert(f != NULL);
    write_u32(f, 0);
    write_u32(f, 1);
    write_u32(f, 0);
    fclose(f);

    f = fopen("/tmp/runec_short_activity_schema_header.bin", "wb");
    assert(f != NULL);
    fputc(0, f);
    fclose(f);

    assert(rc_load_activity_schemas(NULL) == -1);
    assert(rc_load_activity_schemas(
               "/tmp/runec_bad_activity_schema_header.bin") == -1);
    assert(rc_load_activity_schemas(
               "/tmp/runec_short_activity_schema_header.bin") == -1);
    assert(rc_load_activity_schemas("missing_activity_schemas.bin") == -1);

    int loaded = rc_load_activity_schemas(RC_TEST_ACTIVITY_SCHEMAS_BIN);
    assert(loaded > 50);
    assert(g_rc_activity_schema_count == loaded);
    assert(count_status(RC_ACTIVITY_SCHEMA_BLOCKS_PARITY) == 0);

    int scurrius = rc_activity_schema_find_slug("scurrius");
    int fight_caves = rc_activity_schema_find_slug("tzhaar_fight_cave");
    int inferno = rc_activity_schema_find_slug("inferno");
    int tempoross = rc_activity_schema_find_slug("tempoross");
    int nex = rc_activity_schema_find_slug("nex");
    int sol = rc_activity_schema_find_slug("fortis_colosseum_sol_heredit");
    assert(scurrius >= 0 && fight_caves >= 0 && inferno >= 0);
    assert(tempoross >= 0 && nex >= 0 && sol >= 0);
    assert(rc_activity_schema_find_slug("missing") == -1);
    assert(rc_activity_schema_find_slug(NULL) == -1);

    const RcActivitySchema *sc = &g_rc_activity_schemas[scurrius];
    assert(sc->status == RC_ACTIVITY_SCHEMA_READY);
    assert((sc->flags & RC_ACTIVITY_SCHEMA_ENCOUNTER) != 0);
    assert((sc->flags & RC_ACTIVITY_SCHEMA_MECHANICS) != 0);
    assert(sc->phase_count == 3);
    assert(sc->mechanic_count >= 3);
    assert(rc_activity_schema_has_npc(scurrius, 7221));

    const RcActivitySchema *fc = &g_rc_activity_schemas[fight_caves];
    assert((fc->flags & RC_ACTIVITY_SCHEMA_WAVES) != 0);
    assert((fc->flags & RC_ACTIVITY_SCHEMA_STATE_MACHINE) != 0);
    assert((fc->flags & RC_ACTIVITY_SCHEMA_SPAWN_DATA) != 0);
    assert(fc->state_count == 3);
    assert(fc->wave_spawn_count >= 1);
    assert(fc->min_x <= 2378 && fc->max_x >= 2422);
    assert(rc_activity_schema_find_for_npc(3127) == fight_caves);

    const RcActivitySchema *inf = &g_rc_activity_schemas[inferno];
    assert((inf->flags & RC_ACTIVITY_SCHEMA_INSTANCE) != 0);
    assert(inf->wave_spawn_count >= 4);
    assert(rc_activity_schema_has_npc(inferno, 7706));

    const RcActivitySchema *tp = &g_rc_activity_schemas[tempoross];
    assert((tp->flags & RC_ACTIVITY_SCHEMA_OBJECTS) != 0);
    assert(tp->object_anchor_count >= 8);
    assert(tp->object_count >= 4);
    assert(tp->object_id_count >= 4);
    assert(rc_activity_schema_has_object(tempoross, 41236));
    assert(rc_activity_schema_has_object(tempoross, 41239));
    assert(rc_activity_schema_find_for_object(41236) == tempoross);
    assert(!rc_activity_schema_has_object(tempoross, 999999));
    assert(!rc_activity_schema_has_object(-1, 41236));

    assert(!rc_activity_schema_blocks_parity(nex));
    assert(!rc_activity_schema_blocks_parity(sol));
    assert(g_rc_activity_schemas[nex].status ==
           RC_ACTIVITY_SCHEMA_READY_SIMPLIFIED);
    assert(g_rc_activity_schemas[sol].status ==
           RC_ACTIVITY_SCHEMA_READY_SIMPLIFIED);
    assert(g_rc_activity_schemas[nex].class_id ==
           RC_ACTIVITY_SCHEMA_CLASS_ARENA_LOCAL);
    assert(g_rc_activity_schemas[nex].spawn_point_count == 5);
    assert(g_rc_activity_schemas[sol].wave_spawn_count == 1);
    assert(g_rc_activity_schemas[sol].spawn_region_count >= 10);
    assert(g_rc_activity_schemas[nex].unresolved_count == 0);
    assert(g_rc_activity_schemas[sol].unresolved_count == 0);
    assert(!rc_activity_schema_blocks_parity(scurrius));
    assert(rc_activity_schema_blocks_parity(-1));
    assert(!rc_activity_schema_has_npc(-1, 3127));
    assert(rc_activity_schema_find_for_npc(999999) == -1);
    assert(rc_activity_schema_find_for_object(999999) == -1);

    RcWorldConfig cfg = rc_preset_combat_only();
    cfg.activity_schemas_path = RC_TEST_ACTIVITY_SCHEMAS_BIN;
    RcWorld *world = rc_world_create_config(&cfg);
    assert(world != NULL);
    assert(g_rc_activity_schema_count == loaded);
    rc_world_destroy(world);

    printf("test_activity_schemas_bin: activity schema index loaded.\n");
    return 0;
}
