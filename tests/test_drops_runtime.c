#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "api.h"
#include "config.h"
#include "drops.h"

#define DROPS_PATH RC_TEST_SOURCE_DIR "/data/defs/drops.bin"
#define RDT_PATH RC_TEST_SOURCE_DIR "/data/defs/rdt.bin"
#define GDT_PATH RC_TEST_SOURCE_DIR "/data/defs/gdt.bin"
#define MRDT_PATH RC_TEST_SOURCE_DIR "/data/defs/mrdt.bin"
#define BAD_PATH "/tmp/runec_bad_drops.bin"

static void write_bad_header(void) {
    uint32_t bad[3] = {0, 1, 0};
    FILE *f = fopen(BAD_PATH, "wb");
    assert(f != NULL);
    assert(fwrite(bad, sizeof(bad), 1, f) == 1);
    fclose(f);
}

int main(void) {
    write_bad_header();
    assert(rc_load_drops(NULL) == -1);
    assert(rc_load_drops(BAD_PATH) == -1);
    assert(rc_load_rdt(BAD_PATH) == -1);
    assert(rc_load_drops(DROPS_PATH) == 1052);
    assert(rc_load_rdt(RDT_PATH) == 19);
    assert(rc_load_gdt(GDT_PATH) == 11);
    assert(rc_load_mrdt(MRDT_PATH) == 4);
    assert(g_rc_drop_entry_count > 24000);

    const RcDropTable *obor = rc_drop_table_for_npc(7416);
    assert(obor != NULL);
    assert(obor->count[RC_DROP_ALWAYS] == 2);
    assert(obor->count[RC_DROP_MAIN] == 109);
    assert(obor->rare_table_weight == 15);

    int n = 0;
    const RcDropEntry *always = rc_drop_entries_for(obor, RC_DROP_ALWAYS, &n);
    assert(always != NULL && n == 2);
    assert(always[0].rarity_inv == 1);
    const RcDropEntry *main = rc_drop_entries_for(obor, RC_DROP_MAIN, &n);
    assert(main != NULL && n == 109);
    assert(main[0].rarity_inv > 0);
    assert(rc_drop_entries_for(NULL, RC_DROP_MAIN, &n) == NULL && n == 0);
    assert(rc_drop_table_for_npc(-1) == NULL);

    const RcDropEntry *rdt = rc_shared_drop_entries(RC_SHARED_DROP_RDT, &n);
    assert(rdt != NULL && n == 19);
    assert(rc_shared_drop_entries(RC_SHARED_DROP_GDT, &n) != NULL && n == 11);
    assert(rc_shared_drop_entries(RC_SHARED_DROP_MRDT, &n) != NULL && n == 4);

    g_rc_drop_table_count = 0;
    g_rc_rdt_entry_count = 0;
    g_rc_gdt_entry_count = 0;
    g_rc_mrdt_entry_count = 0;
    RcWorldConfig cfg = rc_preset_base_only();
    cfg.subsystems = RC_SUB_LOOT;
    cfg.drops_path = DROPS_PATH;
    cfg.rdt_path = RDT_PATH;
    cfg.gdt_path = GDT_PATH;
    cfg.mrdt_path = MRDT_PATH;
    RcWorld *world = rc_world_create_config(&cfg);
    assert(world != NULL);
    assert(g_rc_drop_table_count == 1052);
    assert(g_rc_rdt_entry_count == 19);
    assert(g_rc_gdt_entry_count == 11);
    assert(g_rc_mrdt_entry_count == 4);
    rc_world_destroy(world);

    printf("test_drops_runtime: drops and shared tables loaded.\n");
    return 0;
}
