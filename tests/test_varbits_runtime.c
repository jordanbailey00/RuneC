#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "api.h"
#include "config.h"
#include "varbits.h"

#define VBIT_PATH RC_TEST_SOURCE_DIR "/data/defs/varbits.bin"
#define VARP_PATH RC_TEST_SOURCE_DIR "/data/defs/varps.bin"
#define BAD_PATH "/tmp/runec_bad_varbits.bin"

static void write_bad_header(void) {
    uint32_t bad[3] = {0, 2, 0};
    FILE *f = fopen(BAD_PATH, "wb");
    assert(f != NULL);
    assert(fwrite(bad, sizeof(bad), 1, f) == 1);
    fclose(f);
}

int main(void) {
    write_bad_header();
    assert(rc_load_varbits(NULL) == -1);
    assert(rc_load_varbits(BAD_PATH) == -1);
    assert(rc_load_varps(BAD_PATH) == -1);
    assert(rc_load_varbits(VBIT_PATH) == 18639);
    assert(rc_load_varps(VARP_PATH) == 5546);

    int holy_grail = rc_varbit_find("HOLY_GRAIL_PROGRESS");
    assert(holy_grail == 5);
    const RcVarbitDef *def = rc_varbit_def_get(holy_grail);
    assert(def != NULL);
    assert(def->base_varp == 318);
    assert(def->lsb == 5 && def->msb == 5);
    assert(rc_varp_find("VARP_318") == 318);
    assert(rc_varp_def_get(318) != NULL);

    RcWorldConfig cfg = rc_preset_base_only();
    cfg.varbits_path = VBIT_PATH;
    cfg.varps_path = VARP_PATH;
    RcWorld *world = rc_world_create_config(&cfg);
    assert(world != NULL);
    assert(g_rc_varbit_count == 18639);
    assert(g_rc_varp_count == 5546);
    assert(rc_varbit_get(world, 7) == 0);
    assert(rc_varbit_set(world, 7, 5) == 0);
    assert(rc_varbit_get(world, 7) == 5);
    assert(((uint32_t)world->varps[336] & 0x0eu) == 10u);
    assert(rc_varbit_set(world, -1, 1) == -1);
    rc_world_destroy(world);

    printf("test_varbits_runtime: varbit/varp definitions and state loaded.\n");
    return 0;
}
