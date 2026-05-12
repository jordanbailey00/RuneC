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
#include "../rc-content/content.h"

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

static int find_attack(const RcEncounterSpec *s, const char *name) {
    for (int i = 0; i < s->attack_count; i++) {
        if (strcmp(s->attacks[i].name, name) == 0) return i;
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
    rc_content_register_all(w);

    // All 50 TOMLs should load.
    assert(w->encounter.registry_count == 50);
    assert(w->encounter.script_count == 50);
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
        assert(s->damage_mod_count <= RC_ENC_MAX_DAMAGE_MODS);
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

    // Trigger binding spot-checks: v3 encodes phase-enter/exit,
    // phase-in unions, and dormant hook trigger classes.
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

    const RcEncounterSpec *kraken =
        &w->encounter.registry[rc_encounter_find_spec(w, 494)];
    int whirlpools = find_phase(kraken, "whirlpools");
    int surfaced = find_phase(kraken, "surfaced");
    int surface = find_mech(kraken, "Surface After Tentacles");
    int whirlpool_gating = find_mech(kraken, "Whirlpool Gating");
    assert(whirlpools >= 0 && surfaced >= 0 && surface >= 0);
    assert(whirlpool_gating >= 0);
    assert(!kraken->phases[whirlpools].player_targetable);
    assert(kraken->phases[surfaced].player_targetable);
    const RcPrimParamsSpawnHiddenMinions *whirlpool_params =
        (const RcPrimParamsSpawnHiddenMinions *)
        kraken->mechanics[whirlpool_gating].param_block;
    assert(strcmp(whirlpool_params->object_name, "Whirlpool") == 0);
    assert(kraken->mechanics[surface].trigger_type == RC_ENC_TRIGGER_PHASE_IN);
    assert((kraken->mechanics[surface].phase_mask & (1u << whirlpools)) != 0);

    const RcEncounterSpec *vorkath =
        &w->encounter.registry[rc_encounter_find_spec(w, 8061)];
    int special = find_mech(vorkath, "Special Attack Counter");
    int spawn = find_mech(vorkath, "Zombified Spawn");
    int acid = find_mech(vorkath, "Acid Pool");
    int fireball = find_mech(vorkath, "Fireball During Acid");
    assert(special >= 0 && spawn >= 0 && acid >= 0 && fireball >= 0);
    assert(vorkath->mechanics[special].primitive_id
           == RC_PRIM_ATTACK_COUNTER_ALTERNATE_SPECIAL);
    assert(vorkath->mechanics[special].trigger_type
           == RC_ENC_TRIGGER_ATTACK_COUNT);
    const RcPrimParamsAttackCounterAlternateSpecial *special_params =
        (const RcPrimParamsAttackCounterAlternateSpecial *)
        vorkath->mechanics[special].param_block;
    assert(special_params->every_n_attacks == 7);
    assert(special_params->special_count == 2);
    assert(strcmp(special_params->special_names[0], "Zombified Spawn") == 0);
    assert(strcmp(special_params->special_names[1], "Acid Pool") == 0);
    assert(vorkath->mechanics[spawn].trigger_type
           == RC_ENC_TRIGGER_ATTACK_COUNT);
    const RcPrimParamsSpawnNpcs *spawn_params =
        (const RcPrimParamsSpawnNpcs *)vorkath->mechanics[spawn].param_block;
    assert(strcmp(spawn_params->name, "Zombified Spawn") == 0);
    assert(spawn_params->blocks_boss_damage_until_dead == 1);
    assert(spawn_params->freeze_player_ticks == 24);
    assert(vorkath->mechanics[acid].trigger_type
           == RC_ENC_TRIGGER_ATTACK_COUNT);
    assert(vorkath->mechanics[acid].primitive_id
           == RC_PRIM_COVERED_ARENA_ENVIRONMENT);
    const RcPrimParamsCoveredArenaEnvironment *acid_params =
        (const RcPrimParamsCoveredArenaEnvironment *)
        vorkath->mechanics[acid].param_block;
    assert(acid_params->duration_ticks == 24);
    assert(acid_params->dot_per_tick == 10);
    assert(acid_params->pool_count == 10);
    assert(acid_params->damage_reduction_pct == 50);
    assert(vorkath->mechanics[fireball].trigger_type
           == RC_ENC_TRIGGER_DURING_MECH);
    assert(strcmp(vorkath->mechanics[fireball].trigger_ref,
                  "Acid Pool") == 0);
    int venom = find_attack(vorkath, "Venomous Dragonfire");
    int corrupt = find_attack(vorkath, "Corrupting Dragonfire");
    assert(venom >= 0 && corrupt >= 0);
    assert(vorkath->attacks[venom].effect_id == RC_ENC_ATTACK_EFFECT_VENOM);
    assert(vorkath->attacks[corrupt].effect_id
           == RC_ENC_ATTACK_EFFECT_DEACTIVATE_PRAYERS);

    const RcEncounterSpec *zulrah =
        &w->encounter.registry[rc_encounter_find_spec(w, 2042)];
    int z_default = find_phase(zulrah, "default");
    int z_dive = find_mech(zulrah, "Dive Between Forms");
    assert(z_default >= 0);
    assert(strcmp(zulrah->phases[z_default].script,
                  "zulrah_drive_rotation") == 0);
    assert(rc_encounter_script_lookup(w, "zulrah_drive_rotation") != NULL);
    assert(z_dive >= 0);
    assert(zulrah->mechanics[z_dive].primitive_id
           == RC_PRIM_FORM_TRANSITION_DIVE);
    assert(zulrah->mechanics[z_dive].period_ticks == 30);
    const RcPrimParamsFormTransitionDive *dive_params =
        (const RcPrimParamsFormTransitionDive *)
        zulrah->mechanics[z_dive].param_block;
    assert(dive_params->dive_ticks == 3);
    assert(dive_params->resurface_ticks == 2);
    assert(dive_params->untargetable_during_dive == 1);
    int has_cap = 0;
    for (int i = 0; i < zulrah->damage_mod_count; i++) {
        if (zulrah->damage_mods[i].condition == RC_ENC_DMG_CAP_RANGE) {
            has_cap = 1;
            assert(zulrah->damage_mods[i].style == 45);
            assert(zulrah->damage_mods[i].damage_pct == 50);
        }
    }
    assert(has_cap);

    const RcEncounterSpec *dks =
        &w->encounter.registry[rc_encounter_find_spec(w, 2883)];
    const RcEncounterSpec *corp =
        &w->encounter.registry[rc_encounter_find_spec(w, 319)];
    const RcEncounterSpec *kree =
        &w->encounter.registry[rc_encounter_find_spec(w, 3162)];
    const RcEncounterSpec *kril =
        &w->encounter.registry[rc_encounter_find_spec(w, 3129)];
    const RcEncounterSpec *cerb =
        &w->encounter.registry[rc_encounter_find_spec(w, 5862)];
    assert(dks->damage_mod_count == 3);
    assert(corp->damage_mod_count == 1);
    assert(kree->damage_mod_count == 4);
    int corp_drain = find_attack(corp, "Magic Drain-Heal");
    assert(corp_drain >= 0);
    assert(corp->attacks[corp_drain].effect_id
           == RC_ENC_ATTACK_EFFECT_DRAIN_HEAL);
    assert(corp->attacks[corp_drain].effect_min == 1);
    assert(corp->attacks[corp_drain].effect_max == 2);
    assert(corp->attacks[corp_drain].effect_pct == 50);
    int corp_split = find_attack(corp, "Magic AoE Split");
    assert(corp_split >= 0);
    assert(corp->attacks[corp_split].effect_id
           == RC_ENC_ATTACK_EFFECT_SPLIT_PROJECTILES);
    assert(corp->attacks[corp_split].effect_min == 6);
    assert(corp->attacks[corp_split].effect_max == 30);
    int corp_core = find_mech(corp, "Dark Energy Core");
    assert(corp_core >= 0);
    assert(corp->mechanics[corp_core].trigger_type == RC_ENC_TRIGGER_EVENT);
    const RcPrimParamsSpawnLeechNpc *core_params =
        (const RcPrimParamsSpawnLeechNpc *)
        corp->mechanics[corp_core].param_block;
    assert(core_params->large_hit_threshold == 32);
    assert(core_params->boss_hp_below == 1000);
    assert(core_params->spawn_chance_denominator == 8);
    assert(core_params->poisoned_leech_period_ticks == 10);
    int kril_poison = find_attack(kril, "Melee Scimitars");
    int kril_drain = find_attack(kril, "Enraged Melee (Prayer-Pierce)");
    assert(kril_poison >= 0 && kril_drain >= 0);
    assert(kril->attacks[kril_poison].effect_id
           == RC_ENC_ATTACK_EFFECT_POISON);
    assert(kril->attacks[kril_poison].effect_min == 16);
    assert((kril->attacks[kril_poison].effect_flags
            & RC_ENC_ATTACK_EFFECT_POISON_PIERCES_PRAY) != 0);
    assert(kril->attacks[kril_drain].effect_id
           == RC_ENC_ATTACK_EFFECT_DRAIN_PRAYER_PCT);
    assert(kril->attacks[kril_drain].effect_pct == 50);
    int kree_wind = find_attack(kree, "Wind Gust");
    assert(kree_wind >= 0);
    assert(kree->attacks[kree_wind].effect_id
           == RC_ENC_ATTACK_EFFECT_KNOCKBACK);
    assert(kree->attacks[kree_wind].effect_min == 1);
    int cerb_souls = find_mech(cerb, "Summoned Souls");
    assert(cerb_souls >= 0);
    const RcPrimParamsSpawnSoulAttackers *souls_params =
        (const RcPrimParamsSpawnSoulAttackers *)
        cerb->mechanics[cerb_souls].param_block;
    assert(souls_params->prayer_drain_with_ward == 20);
    assert(souls_params->prayer_drain_with_spectral == 15);
    assert(souls_params->prayer_drain_with_both == 10);
    assert(souls_params->spectral_item_id == 12821);
    int cerb_lava = find_mech(cerb, "Lava Pools");
    assert(cerb_lava >= 0);
    const RcPrimParamsDotTilePlacement *lava_params =
        (const RcPrimParamsDotTilePlacement *)
        cerb->mechanics[cerb_lava].param_block;
    assert(lava_params->pools_random == 2);
    assert(lava_params->duration_ticks == 10);
    assert(lava_params->dragonfire_protection_applies == 1);

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
