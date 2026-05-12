#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "activity_states.h"
#include "activity_spawns.h"
#include "api.h"
#include "config.h"

#define RC_TEST_ACTIVITY_STATES_BIN \
    RC_TEST_SOURCE_DIR "/data/defs/activity_states.bin"
#define RC_TEST_ACTIVITY_SPAWNS_BIN \
    RC_TEST_SOURCE_DIR "/data/defs/activity_spawns.bin"

static void reset_activity_states(void) {
    g_rc_activity_state_count = 0;
    rc_activity_states_rebuild_index();
}

static int count_kind(uint8_t kind) {
    int count = 0;
    for (int i = 0; i < g_rc_activity_state_count; i++) {
        if (g_rc_activity_states[i].kind == kind) count++;
    }
    return count;
}

static void write_u32(FILE *f, uint32_t value) {
    assert(fwrite(&value, sizeof(value), 1, f) == 1);
}

static void write_u16(FILE *f, uint16_t value) {
    assert(fwrite(&value, sizeof(value), 1, f) == 1);
}

static void write_u64(FILE *f, uint64_t value) {
    assert(fwrite(&value, sizeof(value), 1, f) == 1);
}

static void write_pstr(FILE *f, const char *value) {
    uint8_t len = (uint8_t)strlen(value);
    assert(fwrite(&len, sizeof(len), 1, f) == 1);
    assert(fwrite(value, sizeof(char), len, f) == len);
}

static void write_row_header(FILE *f, uint16_t npc_count,
                             uint16_t state_count,
                             uint16_t transition_count,
                             uint16_t param_count) {
    fputc(RC_ACTIVITY_STATE_KIND_WAVE, f);
    fputc(RC_ACTIVITY_STATE_STATUS_TYPED, f);
    write_u16(f, npc_count);
    write_u16(f, state_count);
    write_u16(f, transition_count);
    write_u16(f, param_count);
    write_u64(f, 0);
    write_u64(f, 0);
    write_pstr(f, "bad");
    write_pstr(f, "Bad");
    write_pstr(f, "");
    write_pstr(f, "");
}

static void write_bad_bins(void) {
    FILE *f = fopen("/tmp/runec_bad_activity_states_header.bin", "wb");
    assert(f != NULL);
    write_u32(f, 0);
    write_u32(f, 1);
    write_u32(f, 0);
    fclose(f);

    f = fopen("/tmp/runec_short_activity_states_header.bin", "wb");
    assert(f != NULL);
    fputc(0, f);
    fclose(f);

    f = fopen("/tmp/runec_short_activity_states_row.bin", "wb");
    assert(f != NULL);
    write_u32(f, 0x41545341u);
    write_u32(f, 1);
    write_u32(f, 1);
    fclose(f);

    f = fopen("/tmp/runec_short_activity_states_npc.bin", "wb");
    assert(f != NULL);
    write_u32(f, 0x41545341u);
    write_u32(f, 1);
    write_u32(f, 1);
    write_row_header(f, 1, 0, 0, 0);
    fclose(f);

    f = fopen("/tmp/runec_short_activity_states_state.bin", "wb");
    assert(f != NULL);
    write_u32(f, 0x41545341u);
    write_u32(f, 1);
    write_u32(f, 1);
    write_row_header(f, 0, 1, 0, 0);
    fclose(f);

    f = fopen("/tmp/runec_short_activity_states_transition.bin", "wb");
    assert(f != NULL);
    write_u32(f, 0x41545341u);
    write_u32(f, 1);
    write_u32(f, 1);
    write_row_header(f, 0, 0, 1, 0);
    fclose(f);

    f = fopen("/tmp/runec_short_activity_states_param.bin", "wb");
    assert(f != NULL);
    write_u32(f, 0x41545341u);
    write_u32(f, 1);
    write_u32(f, 1);
    write_row_header(f, 0, 0, 0, 1);
    fclose(f);
}

int main(void) {
    write_bad_bins();
    assert(rc_load_activity_states(NULL) == -1);
    assert(rc_load_activity_states("/tmp/runec_bad_activity_states_header.bin")
           == -1);
    assert(rc_load_activity_states("/tmp/runec_short_activity_states_header.bin")
           == -1);
    assert(rc_load_activity_states("/tmp/runec_short_activity_states_row.bin")
           == -1);
    assert(rc_load_activity_states("/tmp/runec_short_activity_states_npc.bin")
           == -1);
    assert(rc_load_activity_states("/tmp/runec_short_activity_states_state.bin")
           == -1);
    assert(rc_load_activity_states(
               "/tmp/runec_short_activity_states_transition.bin") == -1);
    assert(rc_load_activity_states("/tmp/runec_short_activity_states_param.bin")
           == -1);
    assert(rc_load_activity_states("missing_activity_states.bin") == -1);

    reset_activity_states();
    assert(rc_activity_state_find_for_npc(3127) == -1);
    int loaded = rc_load_activity_states(RC_TEST_ACTIVITY_STATES_BIN);
    assert(loaded == 16);
    assert(g_rc_activity_state_count == 16);
    assert(count_kind(RC_ACTIVITY_STATE_KIND_WAVE) == 3);
    assert(count_kind(RC_ACTIVITY_STATE_KIND_MULTI_BOSS) == 2);
    assert(count_kind(RC_ACTIVITY_STATE_KIND_DUAL_BOSS) == 2);
    assert(count_kind(RC_ACTIVITY_STATE_KIND_SKILLING_BOSS) == 2);
    assert(count_kind(RC_ACTIVITY_STATE_KIND_CRYPT) == 1);
    assert(count_kind(RC_ACTIVITY_STATE_KIND_SINGLE_BOSS) == 6);

    int barrows = rc_activity_state_find_slug("barrows");
    int fight_caves = rc_activity_state_find_slug("tzhaar_fight_cave");
    int inferno = rc_activity_state_find_slug("inferno");
    int grotesque = rc_activity_state_find_slug("grotesque_guardians");
    int royal = rc_activity_state_find_slug("royal_titans");
    int tempoross = rc_activity_state_find_slug("tempoross");
    int wintertodt = rc_activity_state_find_slug("wintertodt");
    int araxxor = rc_activity_state_find_slug("araxxor");
    int doom = rc_activity_state_find_slug("doom_of_mokhaiotl");
    int gemstone = rc_activity_state_find_slug("gemstone_crab");
    int penance = rc_activity_state_find_slug("penance_queen");
    int phosani = rc_activity_state_find_slug("phosanis_nightmare");
    int revenant = rc_activity_state_find_slug("revenant_maledictus");
    int shellbane = rc_activity_state_find_slug("shellbane_gryphon");
    int vanguard = rc_activity_state_find_slug("vanguard");
    assert(barrows >= 0 && fight_caves >= 0 && inferno >= 0);
    assert(grotesque >= 0 && royal >= 0 && tempoross >= 0);
    assert(wintertodt >= 0 && araxxor >= 0 && doom >= 0);
    assert(gemstone >= 0 && penance >= 0 && phosani >= 0);
    assert(revenant >= 0 && shellbane >= 0 && vanguard >= 0);
    assert(rc_activity_state_find_slug("missing") == -1);
    assert(rc_activity_state_find_slug(NULL) == -1);
    assert(!rc_activity_state_has_npc(-1, 1672));

    const RcActivityStateMachine *ba = &g_rc_activity_states[barrows];
    assert(ba->kind == RC_ACTIVITY_STATE_KIND_CRYPT);
    assert(rc_activity_state_has_npc(barrows, 1672));
    assert(!rc_activity_state_has_npc(barrows, 7706));
    assert(rc_activity_state_param(ba, "required_brother_kills", -1) == 6);
    int crypts = rc_activity_state_find_node(ba, "crypts");
    int tunnels = rc_activity_state_find_node(ba, "tunnels");
    int chest = rc_activity_state_find_node(ba, "chest");
    assert(crypts >= 0 && tunnels >= 0 && chest >= 0);
    assert(rc_activity_state_step(
               ba, crypts, RC_ACTIVITY_STATE_EVT_TUNNEL_BROTHER_SELECTED, 0)
           == tunnels);
    assert(rc_activity_state_step(
               ba, tunnels, RC_ACTIVITY_STATE_EVT_REQUIRED_BOSSES_DEAD, 5)
           == tunnels);
    assert(rc_activity_state_step(
               ba, tunnels, RC_ACTIVITY_STATE_EVT_REQUIRED_BOSSES_DEAD, 6)
           == chest);
    assert(rc_activity_state_step(ba, -1, RC_ACTIVITY_STATE_EVT_TIMER, 0)
           == -1);
    assert(rc_activity_state_find_node(NULL, "crypts") == -1);
    assert(rc_activity_state_find_node(ba, NULL) == -1);
    assert(rc_activity_state_find_node(ba, "missing") == -1);
    RcActivityRun run;
    assert(rc_activity_run_start(&run, "barrows") == crypts);
    assert(rc_activity_run_event(&run,
                                 RC_ACTIVITY_STATE_EVT_TUNNEL_BROTHER_SELECTED,
                                 0) == tunnels);
    assert(run.complete == 0);
    assert(rc_activity_run_event(&run,
                                 RC_ACTIVITY_STATE_EVT_REQUIRED_BOSSES_DEAD,
                                 6) == chest);
    assert(run.complete == 1);

    const RcActivityStateMachine *fc = &g_rc_activity_states[fight_caves];
    assert(rc_activity_state_has_npc(fight_caves, 3127));
    assert(rc_activity_state_param(fc, "wave_count", 0) == 63);
    int waves = rc_activity_state_find_node(fc, "waves");
    int jad = rc_activity_state_find_node(fc, "jad");
    assert(waves >= 0 && jad >= 0);
    assert(rc_activity_state_step(fc, waves,
                                  RC_ACTIVITY_STATE_EVT_WAVE_REACHED, 62)
           == waves);
    assert(rc_activity_state_step(fc, waves,
                                  RC_ACTIVITY_STATE_EVT_WAVE_REACHED, 63)
           == jad);
    assert(rc_activity_run_start(&run, "tzhaar_fight_cave") == waves);
    assert(rc_activity_run_event(&run,
                                 RC_ACTIVITY_STATE_EVT_WAVE_REACHED,
                                 63) == jad);
    assert(run.wave == 63);
    assert(run.complete == 0);
    assert(rc_activity_run_event(&run, RC_ACTIVITY_STATE_EVT_BOSS_DEAD,
                                 3127) >= 0);
    assert(run.complete == 1);

    const RcActivityStateMachine *inf = &g_rc_activity_states[inferno];
    assert(rc_activity_state_has_npc(inferno, 7706));
    assert(rc_activity_state_has_npc(inferno, 7707));
    assert(rc_activity_state_param(inf, "zuk_npc_id", 0) == 7706);
    assert(rc_activity_state_param(inf, "ancestral_glyph_npc_id", 0) == 7707);
    assert(rc_activity_run_start(&run, "inferno") >= 0);
    assert(rc_activity_run_event(&run,
                                 RC_ACTIVITY_STATE_EVT_WAVE_REACHED,
                                 68) == rc_activity_state_find_node(inf,
                                                                     "triple_jad"));
    assert(rc_activity_run_event(&run,
                                 RC_ACTIVITY_STATE_EVT_WAVE_REACHED,
                                 69) == rc_activity_state_find_node(inf,
                                                                     "zuk"));
    assert(rc_activity_run_event(&run, RC_ACTIVITY_STATE_EVT_BOSS_DEAD,
                                 7706) >= 0);
    assert(run.complete == 1);

    const RcActivityStateMachine *gg = &g_rc_activity_states[grotesque];
    assert(rc_activity_run_start(&run, "grotesque_guardians") >= 0);
    assert(rc_activity_run_event(&run, RC_ACTIVITY_STATE_EVT_HP_THRESHOLD,
                                 50) == rc_activity_state_find_node(gg,
                                                                     "dusk_open"));
    assert(rc_activity_run_event(&run, RC_ACTIVITY_STATE_EVT_HP_THRESHOLD,
                                 50) == rc_activity_state_find_node(gg,
                                                                     "final_duo"));
    assert(rc_activity_run_event(&run, RC_ACTIVITY_STATE_EVT_BOSS_DEAD,
                                 7852) >= 0);
    assert(run.progress == 1);
    assert(rc_activity_run_event(&run, RC_ACTIVITY_STATE_EVT_BOSS_DEAD,
                                 7852) >= 0);
    assert(run.progress == 1);
    assert(run.complete == 0);
    assert(rc_activity_run_event(&run, RC_ACTIVITY_STATE_EVT_BOSS_DEAD,
                                 7853) >= 0);
    assert(run.complete == 1);

    const RcActivityStateMachine *rt = &g_rc_activity_states[royal];
    assert(rc_activity_state_has_npc(royal, 14147));
    assert(rc_activity_state_has_npc(royal, 14148));
    assert(rc_activity_state_param(rt, "phase_2_hp_pct", 0) == 35);
    assert(rc_activity_state_param(rt, "missing_param", -7) == -7);
    assert(rc_activity_state_find_for_npc(14147) == royal);
    assert(rc_activity_state_find_for_npc(999999) == -1);
    assert(rc_activity_run_start(&run, "royal_titans") >= 0);
    assert(rc_activity_run_event(&run, RC_ACTIVITY_STATE_EVT_HP_THRESHOLD,
                                 35) == rc_activity_state_find_node(rt,
                                                                     "phase_2"));
    assert(rc_activity_run_event(&run, RC_ACTIVITY_STATE_EVT_BOSS_DEAD,
                                 14147) >= 0);
    assert(rc_activity_run_event(&run, RC_ACTIVITY_STATE_EVT_BOSS_DEAD,
                                 14147) >= 0);
    assert(run.progress == 1);
    assert(run.complete == 0);
    assert(rc_activity_run_event(&run, RC_ACTIVITY_STATE_EVT_BOSS_DEAD,
                                 14148) >= 0);
    assert(run.complete == 1);

    int moons = rc_activity_state_find_slug("moons_of_peril");
    assert(rc_activity_run_start_idx(&run, moons) >= 0);
    assert(rc_activity_run_event(&run, RC_ACTIVITY_STATE_EVT_BOSS_DEAD,
                                 13011) >= 0);
    assert(rc_activity_run_event(&run, RC_ACTIVITY_STATE_EVT_BOSS_DEAD,
                                 13011) >= 0);
    assert(rc_activity_run_event(&run, RC_ACTIVITY_STATE_EVT_BOSS_DEAD,
                                 13012) >= 0);
    assert(run.progress == 2);
    assert(run.complete == 0);
    assert(rc_activity_run_event(&run, RC_ACTIVITY_STATE_EVT_BOSS_DEAD,
                                 13013) >= 0);
    assert(run.complete == 1);

    const RcActivityStateMachine *tp = &g_rc_activity_states[tempoross];
    assert(tp->npc_count == 0);
    assert(tp->kind == RC_ACTIVITY_STATE_KIND_SKILLING_BOSS);
    assert(rc_activity_state_param(tp, "max_cycles", 0) == 3);
    int energy = rc_activity_state_find_node(tp, "energy");
    int essence = rc_activity_state_find_node(tp, "essence");
    int tp_reward = rc_activity_state_find_node(tp, "reward");
    assert(energy >= 0 && essence >= 0 && tp_reward >= 0);
    assert(rc_activity_state_step(tp, energy,
                                  RC_ACTIVITY_STATE_EVT_RESOURCE_ZERO, 0)
           == essence);
    assert(rc_activity_run_start(&run, "tempoross") == energy);
    assert(rc_activity_run_object_event(&run, "west_cannon_north", 2600)
           == essence);
    assert(run.resource == 0);
    assert(run.complete == 0);
    assert(rc_activity_run_object_event(&run, "west_shrine", 0) == energy);
    assert(rc_activity_run_event(&run,
                                 RC_ACTIVITY_STATE_EVT_RESOURCE_ZERO, 0)
           == essence);
    assert(run.complete == 0);
    assert(rc_activity_run_event(&run,
                                 RC_ACTIVITY_STATE_EVT_RESOURCE_ZERO, 0)
           == tp_reward);
    assert(run.complete == 1);
    assert(rc_activity_state_param(&g_rc_activity_states[wintertodt],
                                   "max_braziers_lit", 0) == 3);
    assert(rc_activity_run_start(&run, "wintertodt") >= 0);
    assert(rc_activity_run_object_event(&run, "brazier_south_west", 10)
           >= 0);
    assert(run.complete == 1);
    assert(rc_activity_run_start(&run, "wintertodt") >= 0);
    assert(rc_activity_run_event(&run,
                                 RC_ACTIVITY_STATE_EVT_RESOURCE_ZERO, 0)
           == rc_activity_state_find_node(&g_rc_activity_states[wintertodt],
                                          "reward"));
    assert(run.complete == 1);
    assert(rc_load_activity_spawns(RC_TEST_ACTIVITY_SPAWNS_BIN) > 0);
    assert(rc_activity_run_start(&run, "tempoross") == energy);
    assert(rc_activity_run_object_event(&run, "missing_anchor", 10) == -1);
    assert(rc_activity_run_object_event(&run, "west_cannon_north", 2600)
           == essence);

    const RcActivityStateMachine *ar = &g_rc_activity_states[araxxor];
    assert(rc_activity_state_has_npc(araxxor, 13668));
    assert(rc_activity_state_param(ar, "enrage_hp", 0) == 255);
    assert(rc_activity_run_start(&run, "araxxor")
           == rc_activity_state_find_node(ar, "combat"));
    assert(rc_activity_run_event(&run, RC_ACTIVITY_STATE_EVT_HP_THRESHOLD,
                                 300) == rc_activity_state_find_node(ar,
                                                                      "combat"));
    assert(rc_activity_run_event(&run, RC_ACTIVITY_STATE_EVT_HP_THRESHOLD,
                                 255) == rc_activity_state_find_node(ar,
                                                                      "enrage"));
    assert(rc_activity_run_event(&run, RC_ACTIVITY_STATE_EVT_BOSS_DEAD,
                                 13668) == rc_activity_state_find_node(ar,
                                                                       "reward"));
    assert(run.complete == 1);

    const RcActivityStateMachine *dm = &g_rc_activity_states[doom];
    assert(rc_activity_state_has_npc(doom, 14707));
    assert(rc_activity_state_param(dm, "shield_phase_hp_pct", 0) == 75);
    assert(rc_activity_run_start(&run, "doom_of_mokhaiotl")
           == rc_activity_state_find_node(dm, "standard"));
    assert(rc_activity_run_event(&run, RC_ACTIVITY_STATE_EVT_HP_THRESHOLD,
                                 75) == rc_activity_state_find_node(dm,
                                                                     "shield"));
    assert(rc_activity_run_event(&run, RC_ACTIVITY_STATE_EVT_TIMER, 1)
           == rc_activity_state_find_node(dm, "burrow"));
    assert(rc_activity_run_event(&run, RC_ACTIVITY_STATE_EVT_BOSS_DEAD,
                                 14707) == rc_activity_state_find_node(dm,
                                                                       "reward"));
    assert(run.complete == 1);

    const RcActivityStateMachine *gc = &g_rc_activity_states[gemstone];
    assert(rc_activity_state_param(gc, "ruby_bolt_effective_hp", 0) == 300);
    assert(rc_activity_run_start(&run, "gemstone_crab") >= 0);
    assert(rc_activity_run_event(&run, RC_ACTIVITY_STATE_EVT_BOSS_DEAD,
                                 14779) == rc_activity_state_find_node(gc,
                                                                       "reward"));
    assert(run.complete == 1);

    const RcActivityStateMachine *pq = &g_rc_activity_states[penance];
    assert(rc_activity_run_start(&run, "penance_queen") >= 0);
    assert(rc_activity_run_event(&run, RC_ACTIVITY_STATE_EVT_WAVE_REACHED,
                                 10) == rc_activity_state_find_node(pq,
                                                                     "queen"));
    assert(rc_activity_run_event(&run, RC_ACTIVITY_STATE_EVT_BOSS_DEAD,
                                 5775) == rc_activity_state_find_node(pq,
                                                                      "reward"));
    assert(run.complete == 1);

    const RcActivityStateMachine *pn = &g_rc_activity_states[phosani];
    assert(rc_activity_state_param(pn, "shield_phase_count", 0) == 3);
    assert(rc_activity_run_start(&run, "phosanis_nightmare") >= 0);
    assert(rc_activity_run_event(&run,
                                 RC_ACTIVITY_STATE_EVT_REQUIRED_BOSSES_DEAD,
                                 3) == rc_activity_state_find_node(pn,
                                                                    "desperation"));
    assert(rc_activity_run_event(&run, RC_ACTIVITY_STATE_EVT_BOSS_DEAD,
                                 9416) == rc_activity_state_find_node(pn,
                                                                      "reward"));
    assert(run.complete == 1);

    const RcActivityStateMachine *rm = &g_rc_activity_states[revenant];
    assert(rc_activity_state_param(rm, "air_surge_aoe", 0) == 5);
    assert(rc_activity_run_start(&run, "revenant_maledictus") >= 0);
    assert(rc_activity_run_event(&run, RC_ACTIVITY_STATE_EVT_BOSS_DEAD,
                                 11246) == rc_activity_state_find_node(rm,
                                                                       "reward"));
    assert(run.complete == 1);

    const RcActivityStateMachine *sg = &g_rc_activity_states[shellbane];
    assert(rc_activity_state_param(sg, "max_whirlwinds", 0) == 5);
    assert(rc_activity_run_start(&run, "shellbane_gryphon") >= 0);
    assert(rc_activity_run_event(&run, RC_ACTIVITY_STATE_EVT_BOSS_DEAD,
                                 14860) == rc_activity_state_find_node(sg,
                                                                       "reward"));
    assert(run.complete == 1);

    const RcActivityStateMachine *vg = &g_rc_activity_states[vanguard];
    assert(rc_activity_state_param(vg, "boss_count", 0) == 3);
    assert(rc_activity_run_start(&run, "vanguard")
           == rc_activity_state_find_node(vg, "buried"));
    assert(rc_activity_run_event(&run, RC_ACTIVITY_STATE_EVT_TIMER, 1)
           == rc_activity_state_find_node(vg, "combat"));
    assert(rc_activity_run_event(&run,
                                 RC_ACTIVITY_STATE_EVT_REQUIRED_BOSSES_DEAD,
                                 3) == rc_activity_state_find_node(vg,
                                                                    "reward"));
    assert(run.complete == 1);
    assert(rc_activity_run_start(&run, "vanguard") >= 0);
    assert(rc_activity_run_event(&run, RC_ACTIVITY_STATE_EVT_BOSS_DEAD,
                                 7528) >= 0);
    assert(rc_activity_run_event(&run, RC_ACTIVITY_STATE_EVT_BOSS_DEAD,
                                 7528) >= 0);
    assert(rc_activity_run_event(&run, RC_ACTIVITY_STATE_EVT_BOSS_DEAD,
                                 7527) >= 0);
    assert(run.progress == 2);
    assert(run.complete == 0);
    assert(rc_activity_run_event(&run, RC_ACTIVITY_STATE_EVT_BOSS_DEAD,
                                 7526) >= 0);
    assert(run.complete == 1);

    assert(rc_activity_run_start(&run, "missing") == -1);
    assert(rc_activity_run_start_idx(&run, -1) == -1);
    assert(rc_activity_run_event(NULL, RC_ACTIVITY_STATE_EVT_TIMER, 0) == -1);

    reset_activity_states();
    RcWorldConfig cfg = rc_preset_combat_only();
    cfg.activity_states_path = RC_TEST_ACTIVITY_STATES_BIN;
    RcWorld *world = rc_world_create_config(&cfg);
    assert(world != NULL);
    assert(g_rc_activity_state_count == 16);
    assert(rc_activity_state_find_for_npc(3127) >= 0);
    rc_world_destroy(world);

    printf("test_activity_states_bin: activity states loaded + stepped.\n");
    return 0;
}
