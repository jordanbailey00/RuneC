#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "activity_mechanics.h"
#include "api.h"
#include "config.h"

#define RC_TEST_ACTIVITY_MECH_BIN \
    RC_TEST_SOURCE_DIR "/data/defs/activity_mechanics.bin"

static void reset_activity_mechanics(void) {
    g_rc_activity_mechanic_count = 0;
    rc_activity_mechanics_rebuild_index();
}

static int count_status(uint8_t status) {
    int count = 0;
    for (int i = 0; i < g_rc_activity_mechanic_count; i++) {
        if (g_rc_activity_mechanics[i].status == status) count++;
    }
    return count;
}

static int count_flag(uint8_t flag) {
    int count = 0;
    for (int i = 0; i < g_rc_activity_mechanic_count; i++) {
        if ((g_rc_activity_mechanics[i].flags & flag) != 0) count++;
    }
    return count;
}

static void write_u32(FILE *f, uint32_t value) {
    assert(fwrite(&value, sizeof(value), 1, f) == 1);
}

static void write_u16(FILE *f, uint16_t value) {
    assert(fwrite(&value, sizeof(value), 1, f) == 1);
}

static void write_pstr(FILE *f, const char *value) {
    uint8_t len = (uint8_t)strlen(value);
    assert(fwrite(&len, sizeof(len), 1, f) == 1);
    assert(fwrite(value, sizeof(char), len, f) == len);
}

static void write_bad_bins(void) {
    FILE *f = fopen("/tmp/runec_bad_activity_header.bin", "wb");
    assert(f != NULL);
    write_u32(f, 0);
    write_u32(f, 1);
    write_u32(f, 0);
    fclose(f);

    f = fopen("/tmp/runec_short_activity_header.bin", "wb");
    assert(f != NULL);
    fputc(0, f);
    fclose(f);

    f = fopen("/tmp/runec_short_activity_row.bin", "wb");
    assert(f != NULL);
    write_u32(f, 0x48434D41u);
    write_u32(f, 2);
    write_u32(f, 1);
    fclose(f);

    f = fopen("/tmp/runec_v1_activity.bin", "wb");
    assert(f != NULL);
    write_u32(f, 0x48434D41u);
    write_u32(f, 1);
    write_u32(f, 1);
    fputc(RC_ACTIVITY_MECH_STATUS_OWNER_MAPPED, f);
    fputc(RC_ACTIVITY_MECH_HAS_OWNER, f);
    write_u16(f, 1);
    write_u16(f, 0);
    write_pstr(f, "legacy");
    write_pstr(f, "Legacy");
    write_pstr(f, "Legacy");
    write_pstr(f, "legacy_owner");
    write_u32(f, 12345);
    fclose(f);
}

int main(void) {
    write_bad_bins();
    assert(rc_load_activity_mechanics("/tmp/runec_bad_activity_header.bin")
           == -1);
    assert(rc_load_activity_mechanics("/tmp/runec_short_activity_header.bin")
           == -1);
    assert(rc_load_activity_mechanics("/tmp/runec_short_activity_row.bin")
           == -1);
    assert(rc_load_activity_mechanics("/tmp/runec_v1_activity.bin") == 1);
    assert(g_rc_activity_mechanics[0].behavior_bits == 0);
    assert(rc_activity_mechanics_has_npc(0, 12345));
    assert(rc_activity_mechanics_behavior_for_npc(12345) == 0);

    reset_activity_mechanics();
    int loaded = rc_load_activity_mechanics(RC_TEST_ACTIVITY_MECH_BIN);
    assert(loaded == 96);
    assert(g_rc_activity_mechanic_count == 96);
    assert(count_status(RC_ACTIVITY_MECH_STATUS_ENCOUNTER_BACKED) == 64);
    assert(count_status(RC_ACTIVITY_MECH_STATUS_OWNER_EXECUTABLE) == 32);
    assert(count_status(RC_ACTIVITY_MECH_STATUS_OWNER_MAPPED) == 0);
    assert(count_status(RC_ACTIVITY_MECH_STATUS_ACTIVITY_INDEXED) == 0);
    assert(count_flag(RC_ACTIVITY_MECH_MISSING_NPC_IDS) == 0);
    assert(count_flag(RC_ACTIVITY_MECH_OWNER_ONLY) == 2);
    assert(rc_load_activity_mechanics(NULL) == -1);
    assert(rc_load_activity_mechanics("missing_activity_mechanics.bin") == -1);

    int scurrius = rc_activity_mechanics_find_slug("Scurrius");
    int araxxor = rc_activity_mechanics_find_slug("Araxxor");
    int jad = rc_activity_mechanics_find_slug("TzTok-Jad");
    int verzik = rc_activity_mechanics_find_slug("Verzik_Vitur");
    int gg = rc_activity_mechanics_find_slug("Grotesque_Guardians");
    int moons = rc_activity_mechanics_find_slug("Moons_of_Peril");
    int royal = rc_activity_mechanics_find_slug("Royal_Titans");
    int tempoross = rc_activity_mechanics_find_slug("Tempoross");
    int wintertodt = rc_activity_mechanics_find_slug("Wintertodt");
    assert(scurrius >= 0 && araxxor >= 0 && jad >= 0 && verzik >= 0);
    assert(gg >= 0 && moons >= 0 && royal >= 0);
    assert(tempoross >= 0 && wintertodt >= 0);
    assert(rc_activity_mechanics_find_slug("missing_slug") == -1);
    assert(rc_activity_mechanics_find_slug(NULL) == -1);
    assert(!rc_activity_mechanics_has_npc(-1, 7221));

    const RcActivityMechanic *sc = &g_rc_activity_mechanics[scurrius];
    assert(sc->status == RC_ACTIVITY_MECH_STATUS_ENCOUNTER_BACKED);
    assert((sc->flags & RC_ACTIVITY_MECH_HAS_ENCOUNTER) != 0);
    assert(strcmp(sc->encounter_slug, "scurrius") == 0);
    assert(rc_activity_mechanics_has_npc(scurrius, 7221));
    assert(!rc_activity_mechanics_has_npc(scurrius, 999999));
    assert(sc->section_count >= 1);
    assert(sc->sections[0].text_hash != 0);
    assert(sc->sections[0].text_len > 0);

    const RcActivityMechanic *ar = &g_rc_activity_mechanics[araxxor];
    assert(ar->status == RC_ACTIVITY_MECH_STATUS_OWNER_EXECUTABLE);
    assert((ar->flags & RC_ACTIVITY_MECH_HAS_OWNER) != 0);
    assert((ar->flags & RC_ACTIVITY_MECH_BEHAVIOR_INCOMPLETE) == 0);
    assert(strcmp(ar->encounter_slug, "araxxor") == 0);
    assert(rc_activity_mechanics_has_npc(araxxor, 13668));
    assert((ar->behavior_bits & RC_ACTIVITY_BEHAVIOR_VENOM) != 0);
    assert((ar->behavior_bits & RC_ACTIVITY_BEHAVIOR_ENRAGE) != 0);
    assert(ar->profile_id == RC_ACTIVITY_PROFILE_ARAXXOR);
    assert((rc_activity_mechanics_behavior_for_npc(13668) &
            RC_ACTIVITY_BEHAVIOR_VENOM) != 0);
    assert(rc_activity_mechanics_profile_for_npc(13668) ==
           RC_ACTIVITY_PROFILE_ARAXXOR);

    const RcActivityMechanic *gg_row = &g_rc_activity_mechanics[gg];
    assert(gg_row->status == RC_ACTIVITY_MECH_STATUS_OWNER_EXECUTABLE);
    assert(strcmp(gg_row->encounter_slug, "grotesque_guardians") == 0);
    assert((gg_row->behavior_bits & RC_ACTIVITY_BEHAVIOR_AREA_PRESSURE) != 0);
    assert((gg_row->flags & RC_ACTIVITY_MECH_MISSING_NPC_IDS) == 0);
    assert(rc_activity_mechanics_has_npc(gg, 7852));
    assert(rc_activity_mechanics_has_npc(gg, 7887));
    assert(gg_row->profile_id == RC_ACTIVITY_PROFILE_GROTESQUE_GUARDIANS);
    assert((rc_activity_mechanics_behavior_for_owner("grotesque_guardians") &
            RC_ACTIVITY_BEHAVIOR_FORM_ROTATION) != 0);
    assert(rc_activity_mechanics_behavior_for_owner("missing_owner") == 0);

    const RcActivityMechanic *moons_row = &g_rc_activity_mechanics[moons];
    assert(rc_activity_mechanics_has_npc(moons, 13011));
    assert(rc_activity_mechanics_has_npc(moons, 13012));
    assert(rc_activity_mechanics_has_npc(moons, 13013));
    assert(moons_row->profile_id == RC_ACTIVITY_PROFILE_MOONS_OF_PERIL);

    const RcActivityMechanic *royal_row = &g_rc_activity_mechanics[royal];
    assert(rc_activity_mechanics_has_npc(royal, 14147));
    assert(rc_activity_mechanics_has_npc(royal, 14148));
    assert(royal_row->profile_id == RC_ACTIVITY_PROFILE_ROYAL_TITANS);

    assert((g_rc_activity_mechanics[tempoross].flags &
            RC_ACTIVITY_MECH_OWNER_ONLY) != 0);
    assert((g_rc_activity_mechanics[wintertodt].flags &
            RC_ACTIVITY_MECH_OWNER_ONLY) != 0);

    const RcActivityMechanic *jad_row = &g_rc_activity_mechanics[jad];
    assert(jad_row->profile_id == RC_ACTIVITY_PROFILE_TZTOK_JAD);
    const RcActivityMechanic *rev_row =
        &g_rc_activity_mechanics[
            rc_activity_mechanics_find_slug("Revenant_maledictus")];
    assert(rev_row->profile_id == RC_ACTIVITY_PROFILE_REVENANT_MALEDICTUS);
    assert((rev_row->behavior_bits & RC_ACTIVITY_BEHAVIOR_TELEBLOCK) == 0);

    reset_activity_mechanics();
    RcWorldConfig cfg = rc_preset_combat_only();
    cfg.activity_mechanics_path = RC_TEST_ACTIVITY_MECH_BIN;
    RcWorld *world = rc_world_create_config(&cfg);
    assert(world != NULL);
    assert(g_rc_activity_mechanic_count == 96);
    rc_world_destroy(world);

    printf("test_activity_mechanics_bin: activity mechanics index loaded.\n");
    return 0;
}
