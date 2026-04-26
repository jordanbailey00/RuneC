// test_encounter_bin — validate the TOML → binary → registry pipeline
// for all 50 encounter TOMLs.
//
// Verifies:
//   1. encounters.bin parses cleanly (magic + version).
//   2. All 50 encounters land in the registry.
//   3. Every encounter has >= 1 npc_id registered.
//   4. Known boss NPC IDs resolve to their expected slug (e.g. 7221
//      → "scurrius", 965 → "kalphite_queen").
//   5. Spec counts for attacks/phases/mechanics are within the
//      schema caps (no corruption from the compiler).

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../rc-core/api.h"
#include "../rc-core/config.h"
#include "../rc-core/encounter.h"

#define RC_TEST_ENCOUNTERS_BIN RC_TEST_SOURCE_DIR "/data/defs/encounters.bin"

// Spot-check lookups: npc_id → expected slug prefix.
static const struct { uint32_t id; const char *slug; } expected[] = {
    { 7221,  "scurrius" },
    { 965,   "kalphite_queen" },
    { 7416,  "obor" },
    { 8195,  "bryophyta" },
    { 6615,  "scorpia" },
    { 5779,  "giant_mole" },
    { 2054,  "chaos_elemental" },
    { 319,   "corporeal_beast" },
    { 5862,  "cerberus" },
    { 494,   "kraken" },
    { 239,   "king_black_dragon" },
    { 2042,  "zulrah" },
    { 8061,  "vorkath" },
    { 2205,  "commander_zilyana" },
    { 3129,  "kril_tsutsaroth" },
    { 3162,  "kreearra" },
    { 2215,  "general_graardor" },
    { 12224, "vardorvis" },
    { 12215, "leviathan" },
    { 11278, "nex" },
    { 14176, "yama" },
};

static int find_phase(const RcEncounterSpec *s, const char *id) {
    for (int i = 0; i < s->phase_count; i++) {
        if (strcmp(s->phases[i].id, id) == 0) return i;
    }
    return -1;
}

static int find_mech(const RcEncounterSpec *s, const char *name) {
    for (int i = 0; i < s->mechanic_count; i++) {
        if (strcmp(s->mechanics[i].name, name) == 0) return i;
    }
    return -1;
}

int main(void) {
    RcWorldConfig cfg = rc_preset_combat_only();
    cfg.seed = 1;
    cfg.encounters_path = RC_TEST_ENCOUNTERS_BIN;
    RcWorld *w = rc_world_create_config(&cfg);
    assert(w != NULL);
    assert((w->enabled & RC_SUB_ENCOUNTER) != 0);

    // All 50 TOMLs should load.
    assert(w->encounter.registry_count == 50);
    printf("  registry loaded: %d encounters\n", w->encounter.registry_count);

    // Sanity: every encounter has at least one NPC id registered.
    int zero_nids = 0;
    for (int i = 0; i < w->encounter.registry_count; i++) {
        const RcEncounterSpec *s = &w->encounter.registry[i];
        if (s->npc_id_count == 0) zero_nids++;
        // Schema caps enforced by the compiler + loader.
        assert(s->attack_count <= RC_ENC_MAX_ATTACKS);
        assert(s->phase_count <= RC_ENC_MAX_PHASES);
        assert(s->mechanic_count <= RC_ENC_MAX_MECHANICS);
        assert(s->npc_id_count <= RC_ENC_MAX_NPC_IDS);
    }
    assert(zero_nids == 0 && "every encounter must have >= 1 npc_id");

    // Spot-check: known NPC IDs resolve to their expected slug.
    int matched = 0, missed = 0;
    for (size_t i = 0; i < sizeof(expected) / sizeof(expected[0]); i++) {
        int idx = rc_encounter_find_spec(w, expected[i].id);
        if (idx < 0) {
            fprintf(stderr, "  MISS: npc_id %u → no registered encounter\n",
                    expected[i].id);
            missed++;
            continue;
        }
        const RcEncounterSpec *s = &w->encounter.registry[idx];
        if (strcmp(s->slug, expected[i].slug) != 0) {
            fprintf(stderr, "  WRONG: npc_id %u → slug '%s' "
                    "(expected '%s')\n",
                    expected[i].id, s->slug, expected[i].slug);
            missed++;
            continue;
        }
        matched++;
    }
    printf("  spot-check: %d/%lu expected npc_id → slug matches\n",
           matched, sizeof(expected) / sizeof(expected[0]));
    assert(missed == 0);

    // Trigger binding spot-checks: the bounded pass-2.2 exporter
    // encodes simple phase-enter / phase-exit bindings, even though
    // richer trigger forms remain deferred.
    const RcEncounterSpec *scurrius =
        &w->encounter.registry[rc_encounter_find_spec(w, 7221)];
    int scur_heal_phase = find_phase(scurrius, "heal");
    int scur_food_heal = find_mech(scurrius, "Food Heal");
    assert(scur_heal_phase >= 0 && scur_food_heal >= 0);
    assert(scurrius->mechanics[scur_food_heal].trigger_type
           == RC_ENC_TRIGGER_PHASE_ENTER);
    assert(scurrius->mechanics[scur_food_heal].phase_idx == scur_heal_phase);

    const RcEncounterSpec *kq =
        &w->encounter.registry[rc_encounter_find_spec(w, 965)];
    int grounded_phase = find_phase(kq, "grounded");
    int preserve = find_mech(kq, "Stat-Drain Carries to Phase 2");
    assert(grounded_phase >= 0 && preserve >= 0);
    assert(kq->mechanics[preserve].trigger_type
           == RC_ENC_TRIGGER_PHASE_EXIT);
    assert(kq->mechanics[preserve].phase_idx == grounded_phase);

    const RcEncounterSpec *scorpia =
        &w->encounter.registry[rc_encounter_find_spec(w, 6615)];
    int guarded_phase = find_phase(scorpia, "guarded");
    int summon = find_mech(scorpia, "Summon Guardians");
    int guardian_heal = find_mech(scorpia, "Guardian Heal");
    assert(guarded_phase >= 0 && summon >= 0 && guardian_heal >= 0);
    assert(scorpia->mechanics[summon].primitive_id == RC_PRIM_SPAWN_NPCS_ONCE);
    assert(scorpia->mechanics[summon].trigger_type
           == RC_ENC_TRIGGER_PHASE_ENTER);
    assert(scorpia->mechanics[summon].phase_idx == guarded_phase);
    assert(scorpia->mechanics[guardian_heal].primitive_id
           == RC_PRIM_PERIODIC_HEAL_BOSS);
    assert(scorpia->mechanics[guardian_heal].trigger_type
           == RC_ENC_TRIGGER_PERIODIC);
    assert(scorpia->mechanics[guardian_heal].period_ticks == 1);
    const RcPrimParamsPeriodicHealBoss *heal_params =
        (const RcPrimParamsPeriodicHealBoss *)
        scorpia->mechanics[guardian_heal].param_block;
    assert(strcmp(heal_params->alive_npc_name, "Scorpia's guardian") == 0);
    assert(heal_params->heal_per_tick == 4);

    const RcEncounterSpec *chaos =
        &w->encounter.registry[rc_encounter_find_spec(w, 2054)];
    int confusion = find_mech(chaos, "Confusion");
    int madness = find_mech(chaos, "Madness");
    assert(confusion >= 0 && madness >= 0);
    assert(chaos->mechanics[confusion].primitive_id
           == RC_PRIM_TELEPORT_PLAYER_NEARBY);
    assert(chaos->mechanics[madness].primitive_id
           == RC_PRIM_UNEQUIP_PLAYER_ITEMS);
    const RcPrimParamsUnequipPlayerItems *madness_params =
        (const RcPrimParamsUnequipPlayerItems *)
        chaos->mechanics[madness].param_block;
    assert(madness_params->count == 4);
    assert(madness_params->weapon_priority == 1);
    assert((madness_params->slot_mask & (1u << EQUIP_WEAPON)) != 0u);

    // Aggregate stats across the registry — sanity that attacks /
    // phases / mechanics actually got compiled in (not just empty
    // shells).
    int total_attacks = 0, total_phases = 0, total_mechanics = 0;
    int total_nids = 0;
    for (int i = 0; i < w->encounter.registry_count; i++) {
        const RcEncounterSpec *s = &w->encounter.registry[i];
        total_attacks += s->attack_count;
        total_phases += s->phase_count;
        total_mechanics += s->mechanic_count;
        total_nids += s->npc_id_count;
    }
    printf("  totals: %d npc_ids, %d attacks, %d phases, %d mechanics\n",
           total_nids, total_attacks, total_phases, total_mechanics);
    assert(total_attacks > 50);    // at least 1 attack per encounter avg
    assert(total_phases >= 50);    // at least 1 phase per encounter
    assert(total_mechanics > 30);  // many encounters have mechanics

    rc_world_destroy(w);
    printf("test_encounter_bin: all 50 encounters loaded + validated.\n");
    return 0;
}
