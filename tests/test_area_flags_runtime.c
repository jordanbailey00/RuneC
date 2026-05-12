#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "api.h"
#include "area_flags.h"
#include "config.h"

#define AFLG_PATH RC_TEST_SOURCE_DIR "/data/defs/area_flags.bin"
#define BAD_PATH "/tmp/runec_bad_area_flags.bin"
#define AFLG_MAGIC 0x474C4641u

static void write_bad_header(void) {
    uint32_t bad[4] = {0, 1, 0, 0};
    FILE *f = fopen(BAD_PATH, "wb");
    assert(f != NULL);
    assert(fwrite(bad, sizeof(bad), 1, f) == 1);
    fclose(f);
}

static void write_truncated_row(void) {
    uint32_t header[4] = {AFLG_MAGIC, 1, 1, 3};
    FILE *f = fopen(BAD_PATH, "wb");
    assert(f != NULL);
    assert(fwrite(header, sizeof(header), 1, f) == 1);
    fclose(f);
}

int main(void) {
    write_bad_header();
    assert(rc_load_area_flags(NULL) == -1);
    assert(rc_load_area_flags("/missing/area_flags.bin") == -1);
    assert(rc_load_area_flags(BAD_PATH) == -1);
    write_truncated_row();
    assert(rc_load_area_flags(BAD_PATH) == -1);

    assert(rc_load_area_flags(AFLG_PATH) == 1419);
    assert(rc_area_flags_is_loaded());
    assert(rc_area_flags_all_provisional());

    uint32_t falador = rc_area_flags_at(2950, 3400, 0);
    assert(falador & RC_AREA_MULTICOMBAT);

    uint32_t fally_not_multi = rc_area_flags_at(2964, 3332, 0);
    assert((fally_not_multi & RC_AREA_MULTICOMBAT) == 0);
    assert(rc_area_flag_rows_at(2964, 3332, 0) >= 2);

    uint32_t wilderness = rc_area_flags_at(3000, 3526, 0);
    assert(wilderness & RC_AREA_WILDERNESS);
    assert(wilderness & RC_AREA_WILDERNESS_LEVEL_LINE);
    assert(rc_wilderness_level_at(3000, 3526, 0) == 1);
    assert(rc_wilderness_level_at(3000, 3530, 0) == 2);
    assert(rc_wilderness_level_at(3000, 3530, 1) == 2);
    assert(rc_wilderness_level_at(2964, 3332, 0) == 0);

    uint32_t revs = rc_area_flags_at(3200, 10120, 0);
    assert(revs & RC_AREA_WILDERNESS);
    assert(revs & RC_AREA_SINGLES_PLUS);

    uint32_t ferox = rc_area_flags_at(3130, 3625, 0);
    assert(ferox & RC_AREA_PVP_SAFE);

    assert(rc_area_flags_at(-1, 0, 0) == 0);
    assert(rc_area_flags_at(3130, 3625, 4) == 0);
    assert(rc_wilderness_level_at(-1, 0, 0) == 0);
    assert(rc_wilderness_level_at(3130, 3625, 4) == 0);

    RcWorldConfig cfg = rc_preset_base_only();
    cfg.subsystems = RC_SUB_REGIONS;
    cfg.collision_tiles_path = NULL;
    cfg.area_flags_path = AFLG_PATH;
    RcWorld *world = rc_world_create_config(&cfg);
    assert(world != NULL);
    assert(world->enabled & RC_SUB_REGIONS);
    assert(g_rc_area_flag_count == 1419);
    rc_world_destroy(world);

    return 0;
}
