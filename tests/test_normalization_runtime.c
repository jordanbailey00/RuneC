#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "api.h"
#include "config.h"
#include "normalization.h"

#define NORM_PATH RC_TEST_SOURCE_DIR "/data/defs/normalization.bin"

static void write_u32(FILE *f, uint32_t value) {
    assert(fwrite(&value, sizeof(value), 1, f) == 1);
}

int main(void) {
    FILE *f = fopen("/tmp/runec_bad_normalization.bin", "wb");
    assert(f != NULL);
    write_u32(f, 0);
    write_u32(f, 1);
    write_u32(f, 0);
    write_u32(f, 0);
    write_u32(f, 0);
    fclose(f);

    f = fopen("/tmp/runec_short_normalization.bin", "wb");
    assert(f != NULL);
    fputc(0, f);
    fclose(f);

    assert(rc_load_normalization(NULL) == -1);
    assert(rc_load_normalization("/tmp/runec_bad_normalization.bin") == -1);
    assert(rc_load_normalization("/tmp/runec_short_normalization.bin") == -1);
    assert(rc_load_normalization("missing_normalization.bin") == -1);

    int loaded = rc_load_normalization(NORM_PATH);
    assert(loaded > 30000);
    assert(g_rc_item_normalization_count > 30000);
    assert(g_rc_npc_normalization_count > 10000);
    assert(g_rc_source_normalization_count > 5000);

    assert(rc_normalize_item_id(4151) == 4151);
    assert(rc_normalize_item_id(4152) == 4151);
    assert(rc_normalize_item_id(14032) == 4151);
    assert(rc_item_noted_id(4151) == 4152);
    assert(rc_item_placeholder_id(4151) == 14032);
    assert(rc_item_noted_id(-1) == -1);
    assert(rc_normalize_item_id(-1) == -1);

    assert(rc_normalize_npc_id(7221) == 7221);
    assert(rc_normalize_npc_id(7222) == 7221);
    assert(rc_normalize_npc_id(-1) == -1);

    uint32_t whip_key = rc_normalization_hash_key("Abyssal whip");
    assert(whip_key == g_rc_item_normalization[4151].key_hash);
    assert(rc_normalization_hash_key("Abyssal whip (or)") == whip_key);

    uint32_t shop_key = rc_normalization_hash_key("Diango");
    assert(rc_normalization_find_source(3, shop_key) >= 0);
    assert(rc_normalization_find_source(3, 0) == -1);

    RcWorldConfig cfg = rc_preset_combat_only();
    cfg.normalization_path = NORM_PATH;
    RcWorld *world = rc_world_create_config(&cfg);
    assert(world != NULL);
    assert(g_rc_item_normalization_count > 30000);
    rc_world_destroy(world);

    printf("test_normalization_runtime: normalization data loaded.\n");
    return 0;
}
