#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "api.h"
#include "config.h"
#include "events.h"
#include "npc.h"
#include "slayer.h"
#include "types.h"

#define RC_TEST_SLAYER_BIN RC_TEST_SOURCE_DIR "/data/defs/slayer.bin"
#define RC_TEST_NPC_DEFS_BIN RC_TEST_SOURCE_DIR "/data/defs/npc_defs.bin"

static void write_u32(FILE *f, uint32_t value) {
    assert(fwrite(&value, sizeof(value), 1, f) == 1);
}

static void write_bad_bins(void) {
    FILE *f = fopen("/tmp/runec_bad_slayer_header.bin", "wb");
    assert(f != NULL);
    write_u32(f, 0);
    write_u32(f, 1);
    write_u32(f, 0);
    fclose(f);

    f = fopen("/tmp/runec_short_slayer_header.bin", "wb");
    assert(f != NULL);
    fputc(0, f);
    fclose(f);
}

static void append_stub_npc(int id, const char *name) {
    int idx = g_npc_def_count++;
    assert(idx < RC_MAX_NPC_DEFS);
    memset(&g_npc_defs[idx], 0, sizeof(g_npc_defs[idx]));
    g_npc_defs[idx].id = id;
    strcpy(g_npc_defs[idx].name, name);
}

int main(void) {
    write_bad_bins();
    assert(rc_load_slayer(NULL) == -1);
    assert(rc_load_slayer("/tmp/runec_bad_slayer_header.bin") == -1);
    assert(rc_load_slayer("/tmp/runec_short_slayer_header.bin") == -1);

    g_rc_slayer_master_count = 0;
    assert(rc_load_slayer(RC_TEST_SLAYER_BIN) == 12);
    int duradel = rc_slayer_find_master("Duradel");
    assert(duradel >= 0);
    int gargoyles = rc_slayer_find_task(duradel, "Gargoyles");
    assert(gargoyles >= 0);
    const RcSlayerTaskDef *gargoyle_task =
        &g_rc_slayer_masters[duradel].tasks[gargoyles];
    assert(gargoyle_task->amount_min == 130);
    assert(gargoyle_task->amount_max == 200);
    assert(gargoyle_task->extended_min == 200);
    assert(gargoyle_task->extended_max == 250);
    assert(gargoyle_task->req_slayer == 75);
    assert(gargoyle_task->req_combat == 80);
    assert((gargoyle_task->progression_flags &
            RC_SLAYER_PROG_PRIEST_IN_PERIL) != 0);
    assert(rc_slayer_task_amount_for_roll(gargoyle_task, 0, 0) == 130);
    assert(rc_slayer_task_amount_for_roll(gargoyle_task, 0, 1) == 200);
    int boss = rc_slayer_find_task(duradel, "Boss");
    assert(boss >= 0);
    const RcSlayerTaskDef *boss_task =
        &g_rc_slayer_masters[duradel].tasks[boss];
    assert((boss_task->task_flags & RC_SLAYER_TASK_BOSS_SECOND_ROLL) != 0);
    assert(boss_task->boss_candidates[0] != '\0');
    int krystilia = rc_slayer_find_master("Krystilia");
    assert(krystilia >= 0);
    int wild_boss = rc_slayer_find_task(krystilia, "Wilderness boss");
    assert(wild_boss >= 0);
    assert((g_rc_slayer_masters[krystilia].tasks[wild_boss].task_flags &
            RC_SLAYER_TASK_BOSS_SECOND_ROLL) != 0);
    assert(rc_slayer_find_task(duradel, "missing task") == -1);
    assert(rc_slayer_find_master("missing master") == -1);

    g_rc_slayer_master_count = 0;
    RcWorldConfig cfg = rc_preset_base_only();
    cfg.subsystems = RC_SUB_SLAYER;
    cfg.npc_defs_path = RC_TEST_NPC_DEFS_BIN;
    cfg.slayer_path = RC_TEST_SLAYER_BIN;
    RcWorld *w = rc_world_create_config(&cfg);
    assert(w != NULL);
    assert(g_rc_slayer_master_count == 12);
    for (int i = 0; i < SKILL_COUNT; i++) {
        w->player.skills.base_level[i] = 99;
        w->player.skills.boosted_level[i] = 99;
    }
    w->player.slayer_unlocks = 0;
    assert(rc_slayer_task_eligible(w, duradel, gargoyles) == 0);
    w->player.slayer_progression_flags = RC_SLAYER_PROG_PRIEST_IN_PERIL;
    assert(rc_slayer_start_task(w, "Duradel", "Gargoyles", 2) == 0);
    assert(w->player.slayer_task_remaining == 2);
    assert(rc_slayer_current_task_name(w) != NULL);

    int lizardmen = rc_slayer_find_task(duradel, "Lizardmen");
    assert(lizardmen >= 0);
    assert(rc_slayer_task_eligible(w, duradel, lizardmen) == 0);
    w->player.slayer_unlocks |= RC_SLAYER_UNLOCK_REPTILE_GOT_RIPPED;
    assert(rc_slayer_task_eligible(w, duradel, lizardmen) == 1);

    int konar = rc_slayer_find_master("Konar quo Maten");
    assert(konar >= 0);
    int kurask = rc_slayer_find_task(konar, "Kurask");
    assert(kurask >= 0);
    const RcSlayerTaskDef *kurask_task =
        &g_rc_slayer_masters[konar].tasks[kurask];
    assert((kurask_task->task_flags & RC_SLAYER_TASK_HAS_LOCATIONS) != 0);
    char picked[80];
    w->player.slayer_progression_flags = 0;
    assert(rc_slayer_task_location_for_roll(w, kurask_task, 0,
                                            picked, sizeof(picked)) == 0);
    assert(strcmp(picked, "Iorwerth Dungeon") != 0);
    w->player.slayer_progression_flags = RC_SLAYER_PROG_SONG_OF_THE_ELVES;
    int saw_iorwerth = 0;
    for (uint32_t r = 0; r < 8; r++) {
        assert(rc_slayer_task_location_for_roll(w, kurask_task, r,
                                                picked, sizeof(picked)) == 0);
        if (strcmp(picked, "Iorwerth Dungeon") == 0) saw_iorwerth = 1;
    }
    assert(saw_iorwerth);

    w->player.slayer_progression_flags = UINT64_MAX;
    w->player.slayer_unlocks |= RC_SLAYER_UNLOCK_LIKE_A_BOSS;
    char boss_name[80];
    assert(rc_slayer_boss_task_for_roll(w, duradel, boss_task, 0,
                                        boss_name, sizeof(boss_name)) == 0);
    assert(boss_name[0] != '\0');
    assert(rc_slayer_boss_task_for_roll(w, duradel, boss_task, 1,
                                        boss_name, sizeof(boss_name)) == 0);
    assert(boss_name[0] != '\0');
    assert(rc_slayer_boss_task_for_roll(
               w, krystilia, &g_rc_slayer_masters[krystilia].tasks[wild_boss],
               0, boss_name, sizeof(boss_name)) == 0);
    assert(strcmp(boss_name, "King Black Dragon") != 0);
    w->player.slayer_combat_achievement_tier = 6;
    assert(rc_slayer_task_amount_for_world(w, boss_task, 62, 0) == 65);

    int assigned_boss = 0;
    for (uint32_t r = 0; r < 10000 && !assigned_boss; r++) {
        if (rc_slayer_assign_task(w, "Duradel", r, 0) == 0 &&
                rc_slayer_current_boss_name(w) != NULL) {
            assigned_boss = 1;
        }
    }
    assert(assigned_boss);
    assert(rc_slayer_current_boss_name(w)[0] != '\0');

    int assigned_location = 0;
    for (uint32_t r = 0; r < 10000 && !assigned_location; r++) {
        if (rc_slayer_assign_task(w, "Konar quo Maten", r, 0) == 0 &&
                rc_slayer_current_location(w) != NULL) {
            assigned_location = 1;
        }
    }
    assert(assigned_location);
    assert(rc_slayer_current_location(w)[0] != '\0');
    assert(rc_slayer_start_task(w, "Duradel", "Gargoyles", 2) == 0);

    RcPayloadNpcEvent miss = {.npc_id = 1, .def_id = 260}; /* green dragon */
    rc_event_fire(w, RC_EVT_NPC_DIED, &miss);
    assert(w->player.slayer_task_remaining == 2);

    RcPayloadNpcEvent hit = {.npc_id = 2, .def_id = 412}; /* gargoyle */
    rc_event_fire(w, RC_EVT_NPC_DIED, &hit);
    assert(w->player.slayer_task_remaining == 1);
    rc_event_fire(w, RC_EVT_NPC_DIED, &hit);
    assert(w->player.slayer_task_remaining == 0);

    append_stub_npc(65000, "Dawn");
    assert(rc_slayer_start_task(w, "Duradel", "Gargoyles", 1) == 0);
    RcPayloadNpcEvent alt = {.npc_id = 3, .def_id = 65000};
    rc_event_fire(w, RC_EVT_NPC_DIED, &alt);
    assert(w->player.slayer_task_remaining == 0);

    append_stub_npc(65001, "Calvar'ion");
    w->player.slayer_master_idx = duradel;
    w->player.slayer_task_idx = boss;
    w->player.slayer_task_remaining = 1;
    strcpy(w->player.slayer_boss_name, "Vet'ion");
    RcPayloadNpcEvent boss_alt = {.npc_id = 4, .def_id = 65001};
    rc_event_fire(w, RC_EVT_NPC_DIED, &boss_alt);
    assert(w->player.slayer_task_remaining == 0);

    append_stub_npc(65002, "Ahrim the Blighted");
    append_stub_npc(65003, "Dagannoth Rex");
    append_stub_npc(65004, "Spindel");
    append_stub_npc(65005, "Artio");
    append_stub_npc(65006, "Deranged archaeologist");
    const char *groups[] = {
        "Barrows brothers", "Dagannoth Kings", "Venenatis",
        "Callisto", "Crazy archaeologist",
    };
    int ids[] = {65002, 65003, 65004, 65005, 65006};
    for (int i = 0; i < 5; i++) {
        w->player.slayer_master_idx = duradel;
        w->player.slayer_task_idx = boss;
        w->player.slayer_task_remaining = 1;
        strcpy(w->player.slayer_boss_name, groups[i]);
        RcPayloadNpcEvent evt = {
            .npc_id = (uint16_t)(5 + i),
            .def_id = (uint32_t)ids[i],
        };
        rc_event_fire(w, RC_EVT_NPC_DIED, &evt);
        assert(w->player.slayer_task_remaining == 0);
    }

    assert(rc_slayer_block_task(w, "Aberrant spectres") == 0);
    assert(rc_slayer_prefer_task(w, "Gargoyles") == 0);
    assert(rc_slayer_assign_task(w, "Duradel", 0, 55) == 0);
    assert(w->player.slayer_task_remaining == 55);
    assert(strcmp(rc_slayer_current_task_name(w), "Slayer task/Aberrant spectres") != 0);
    assert(rc_slayer_assign_task(w, "Duradel", 0x00100001u, 0) == 0);
    assert(w->player.slayer_task_remaining > 0);
    assert(rc_slayer_assign_task(w, "missing", 0, 55) == -1);
    rc_world_destroy(w);

    printf("test_slayer_bin: slayer task runtime loaded.\n");
    return 0;
}
