#include "../rc-core/combat.h"
#include "../rc-core/combat_formula.h"
#include "../rc-core/npc.h"
#include "../rc-core/prayer.h"

#include <assert.h>
#include <string.h>

static void reset_player(RcPlayer *p) {
    memset(p, 0, sizeof(*p));
    for (int i = 0; i < SKILL_COUNT; i++) {
        p->skills.base_level[i] = 1;
        p->skills.boosted_level[i] = 1;
    }
}

static int add_formula_npc(void) {
    int def_idx = g_npc_def_count++;
    assert(def_idx < RC_MAX_NPC_DEFS);
    memset(&g_npc_defs[def_idx], 0, sizeof(g_npc_defs[def_idx]));
    g_npc_defs[def_idx].id = 950500;
    strcpy(g_npc_defs[def_idx].name, "Phase 5 Formula Target");
    g_npc_defs[def_idx].stats[0] = 55;
    g_npc_defs[def_idx].stats[1] = 40;
    g_npc_defs[def_idx].stats[4] = 65;
    g_npc_defs[def_idx].stats[5] = 70;
    g_npc_defs[def_idx].max_hit = 12;
    g_npc_defs[def_idx].attack_types = 0x08;
    return def_idx;
}

static void fake_prayer(int id, int attack, int strength, int defence,
                        int ranged_attack, int ranged_strength,
                        int magic_attack, int magic_damage) {
    memset(&g_rc_prayer_defs[id], 0, sizeof(g_rc_prayer_defs[id]));
    g_rc_prayer_defs[id].id = id;
    g_rc_prayer_defs[id].loaded = 1;
    g_rc_prayer_defs[id].attack = attack;
    g_rc_prayer_defs[id].strength = strength;
    g_rc_prayer_defs[id].defence = defence;
    g_rc_prayer_defs[id].ranged_attack = ranged_attack;
    g_rc_prayer_defs[id].ranged_strength = ranged_strength;
    g_rc_prayer_defs[id].magic_attack = magic_attack;
    g_rc_prayer_defs[id].magic_damage = magic_damage;
}

static void test_scaled_hit_chance_is_deterministic(void) {
    assert(rc_hit_chance_scaled(100, 50) == 7426);
    assert(rc_hit_chance_scaled(50, 100) == 2475);
    assert(rc_hit_chance_scaled(100, 100) == 4950);
    assert(rc_hit_chance_scaled(0, 100) == 0);
}

static void test_melee_formula_uses_boosts_prayer_equipment_and_npc_defence(void) {
    RcPlayer p;
    reset_player(&p);
    fake_prayer(RC_PRAYER_PIETY, 20, 23, 25, 0, 0, 0, 0);
    p.skills.boosted_level[SKILL_ATTACK] = 70;
    p.skills.boosted_level[SKILL_STRENGTH] = 80;
    p.combat_style = COMBAT_MELEE_SLASH;
    p.attack_stance = RC_ATTACK_STANCE_AGGRESSIVE;
    p.active_prayers = PRAYER_PIETY;
    p.equipment_bonuses[EQ_SLASH_ATK] = 100;
    p.equipment_bonuses[EQ_STR] = 50;
    int npc_def = add_formula_npc();

    assert(rc_player_effective_attack_level(&p) == 92);
    assert(rc_player_effective_strength_level(&p) == 110);
    assert(rc_player_offensive_roll(&p, COMBAT_MELEE_SLASH) == 15088);
    assert(rc_npc_defensive_roll(npc_def, COMBAT_MELEE_SLASH) == 3136);
    assert(rc_player_max_hit_melee(&p) == 20);

    RcCombatCalc calc = rc_calc_melee(&p, npc_def);
    assert(calc.attack_roll == 15088);
    assert(calc.defence_roll == 3136);
    assert(calc.max_hit == 20);
}

static void test_ranged_formula_uses_ranged_prayers_and_strength_bonus(void) {
    RcPlayer p;
    reset_player(&p);
    fake_prayer(RC_PRAYER_RIGOUR, 0, 0, 0, 20, 23, 0, 0);
    p.skills.boosted_level[SKILL_RANGED] = 75;
    p.combat_style = COMBAT_RANGED;
    p.attack_stance = RC_ATTACK_STANCE_RAPID;
    p.active_prayers = PRAYER_RIGOUR;
    p.equipment_bonuses[EQ_RANGED_ATK] = 90;
    p.equipment_bonuses[EQ_RANGED_STR] = 40;
    int npc_def = add_formula_npc();

    assert(rc_player_effective_ranged_attack_level(&p) == 98);
    assert(rc_player_effective_ranged_strength_level(&p) == 100);
    assert(rc_player_offensive_roll(&p, COMBAT_RANGED) == 15092);
    assert(rc_player_max_hit_ranged(&p) == 17);

    RcCombatCalc calc = rc_calc_ranged(&p, npc_def);
    assert(calc.attack_roll == 15092);
    assert(calc.defence_roll == 3136);
    assert(calc.max_hit == 17);
}

static void test_magic_formula_uses_magic_prayer_damage_and_defence_blend(void) {
    RcPlayer p;
    reset_player(&p);
    fake_prayer(RC_PRAYER_AUGURY, 0, 0, 0, 0, 0, 25, 4);
    p.skills.boosted_level[SKILL_MAGIC] = 80;
    p.skills.boosted_level[SKILL_DEFENCE] = 50;
    p.combat_style = COMBAT_MAGIC;
    p.attack_stance = RC_ATTACK_STANCE_CAST;
    p.active_prayers = PRAYER_AUGURY;
    p.equipment_bonuses[EQ_MAGIC_ATK] = 70;
    p.equipment_bonuses[EQ_MAGIC_DMG] = 10;
    p.equipment_bonuses[EQ_MAGIC_DEF] = 30;
    int npc_def = add_formula_npc();

    assert(rc_player_effective_magic_attack_level(&p) == 111);
    assert(rc_player_effective_magic_defence_level(&p) == 79);
    assert(rc_player_offensive_roll(&p, COMBAT_MAGIC) == 14874);
    assert(rc_player_defensive_roll(&p, COMBAT_MAGIC) == 7426);
    assert(rc_player_max_hit_magic(&p, 20) == 22);

    RcCombatCalc calc = rc_calc_magic(&p, npc_def, 20);
    assert(calc.attack_roll == 14874);
    assert(calc.defence_roll == 3136);
    assert(calc.max_hit == 22);
}

static void test_npc_formula_uses_style_stat_and_player_defence_roll(void) {
    RcPlayer p;
    reset_player(&p);
    p.skills.boosted_level[SKILL_DEFENCE] = 50;
    p.skills.boosted_level[SKILL_MAGIC] = 90;
    p.attack_stance = RC_ATTACK_STANCE_DEFENSIVE;
    p.equipment_bonuses[EQ_MAGIC_DEF] = 30;
    int npc_def = add_formula_npc();

    assert(rc_npc_offensive_roll(npc_def, COMBAT_MAGIC) == 5056);
    assert(rc_player_effective_magic_defence_level(&p) == 89);
    assert(rc_player_defensive_roll(&p, COMBAT_MAGIC) == 8366);

    RcCombatCalc calc = rc_calc_npc_attack_style(npc_def, &p, COMBAT_MAGIC);
    assert(calc.attack_roll == 5056);
    assert(calc.defence_roll == 8366);
    assert(calc.max_hit == 12);
}

int main(void) {
    test_scaled_hit_chance_is_deterministic();
    test_melee_formula_uses_boosts_prayer_equipment_and_npc_defence();
    test_ranged_formula_uses_ranged_prayers_and_strength_bonus();
    test_magic_formula_uses_magic_prayer_damage_and_defence_blend();
    test_npc_formula_uses_style_stat_and_player_defence_roll();
    return 0;
}
