// Worn gear, passives and special attacks on installed b241 item data.
#include "api.h"
#include "combat.h"
#include "combat_formula.h"
#include "config.h"
#include "items.h"
#include "npc.h"
#include "world_test_fixture.h"
#include "../rc-content/content.h"
#include "../rc-content/combat/ammunition.h"
#include "../rc-content/combat/gear.h"
#include "../rc-content/combat/magic.h"
#include "../rc-content/combat/ralos.h"
#include "../rc-content/combat/specials.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

enum { TARGET_NPC = 3014, DEMON_NPC = 2005, RONDACHE = 34719 };

static RcWorld *W;
static RcPlayer *P;
static RcNpc *N;

static void reset_gear(void) {
    for (int i = 0; i < RC_EQUIP_COUNT; i++) P->equipment[i] = (RcInvSlot){.item_id = -1};
    memset(P->content_gear, 0, sizeof(P->content_gear));
    P->attack_style_idx = 0;
    P->special_energy = 10000;
    P->combat.special_pending = false;
    P->attack_timer = 0;
    N->num_pending_hits = 0;
    N->stats[1] = N->stats[5] = 100;
    N->immobilized_until = 0;
    N->immobilize_immune_until = 0;
    N->burn_remaining = 0;
    N->toxin_severity = 0;
    N->toxin_venom = false;
    N->toxin_tick_counter = 0;
    N->current_hp = N->spawn_hp = 5000;
    W->combat_attack_event_count = 0;
    W->tick += 100;
    P->max_hp = 990;
    P->current_hp = 990;
}

static void wear(int slot, int item, int state) {
    P->equipment[slot] = (RcInvSlot){.item_id = item, .quantity = 1, .state_id = (uint32_t)state};
}

static void refresh(void) {
    rc_recalc_bonuses(P);
    rc_refresh_player_combat_style(P);
}

static void attack(bool special) {
    refresh();
    P->attack_timer = 0;
    P->combat.special_pending = special;
    N->x = P->x + 1;
    N->y = P->y;
    assert(rc_combat_start_player_vs_npc(W, 0, N->uid));
    rc_combat_tick_player(W);
}

static void test_tables(void) {
    assert(rc_content_gear_row(4151)->special == SPECIAL_ENERGY_DRAIN);
    assert(rc_content_gear_row(13652)->special == SPECIAL_SLICE_AND_DICE);
    assert(rc_content_gear_row(13652)->special_cost == 50);
    assert(rc_content_gear_row(1215)->special_cost == 25);
    assert(rc_content_gear_row(27859)->special_variant == 1);
    assert(rc_content_gear_row(33722)->family == GEAR_SUNSPEAR);
    assert(rc_content_gear_row(25865)->ammo_kind == AMMO_INTERNAL);
    assert(rc_content_gear_row(34684)->ammo_kind == AMMO_ASCENSION);
    assert(rc_content_gear_row(-1)->family == GEAR_NONE);
    assert(rc_content_gear_row(1000000)->special == SPECIAL_NONE);
}

static void test_new_weapon_styles(void) {
    reset_gear();
    wear(EQUIP_WEAPON, 34715, 100);  // Obligator
    P->attack_style_idx = 2;
    refresh();
    assert(P->combat_style == COMBAT_MELEE_CRUSH && rc_player_attack_speed(P) == 3);
    P->attack_style_idx = 0;
    refresh();
    assert(P->attack_stance == RC_ATTACK_STANCE_ACCURATE && rc_player_attack_speed(P) == 5);
    reset_gear();
    wear(EQUIP_WEAPON, 33718, 0);  // Hallowed flail
    P->attack_style_idx = 1;
    refresh();
    assert(P->combat_style == COMBAT_MELEE_SLASH && P->attack_stance == RC_ATTACK_STANCE_AGGRESSIVE);
    P->attack_style_idx = 2;
    refresh();
    assert(P->combat_style == COMBAT_NONE);
    reset_gear();
    wear(EQUIP_WEAPON, 34725, 100);  // Zorya's tome
    refresh();
    assert(P->combat_style == COMBAT_MAGIC && rc_player_attack_speed(P) == 3);
}

static void test_void_and_slayer_rolls(void) {
    reset_gear();
    wear(EQUIP_WEAPON, 4151, 0);
    refresh();
    RcCombatCalc plain = rc_calc_melee(P, N);
    RcGearAttack a;
    rc_content_gear_attack(&a, W, N, NULL, false);
    RcCombatCalc passive = plain;
    rc_content_gear_melee(&a, &passive);
    assert(passive.attack_roll == plain.attack_roll && passive.max_hit == plain.max_hit);

    wear(EQUIP_HEAD, 11665, 0);   // Void melee helm
    wear(EQUIP_BODY, 13072, 0);   // Elite void top
    wear(EQUIP_LEGS, 13073, 0);   // Elite void robe
    wear(EQUIP_GLOVES, 8842, 0);  // Void gloves
    refresh();
    plain = rc_calc_melee(P, N);
    rc_content_gear_attack(&a, W, N, NULL, false);
    passive = plain;
    rc_content_gear_melee(&a, &passive);
    int eff = rc_player_effective_attack_level(P) * 11 / 10;
    assert(passive.attack_roll == eff * (P->equipment_bonuses[EQ_SLASH_ATK] + 64));
    assert(passive.max_hit >= plain.max_hit);
}

static void test_twisted_bow_scaling(void) {
    reset_gear();
    wear(EQUIP_WEAPON, 20997, 0);
    wear(EQUIP_AMMO, 11212, 0);
    P->equipment[EQUIP_AMMO].quantity = 100;
    refresh();
    int low_magic = N->stats[5];
    RcCombatCalc base = rc_calc_ranged(P, N, true);
    RcGearAttack a;
    rc_content_gear_attack(&a, W, N, NULL, false);
    N->stats[5] = 250;
    RcCombatCalc high = base;
    rc_content_gear_ranged(&a, &high);
    assert(high.max_hit == base.max_hit * 215 / 100);  // 250 magic: 215%, not the 250% cap
    assert(high.attack_roll == base.attack_roll * 140 / 100);
    N->stats[5] = low_magic;
}

static void test_claws_land_four_hits(void) {
    reset_gear();
    N->force_player_max_hit = true;
    wear(EQUIP_WEAPON, 13652, 0);
    attack(true);
    assert(N->num_pending_hits == 4);
    assert(P->special_energy == 5000);
    const RcPendingHit *h = N->pending_hits;
    assert(h[0].damage > h[1].damage && h[1].damage >= h[2].damage);
    assert(h[3].damage == h[2].damage + 1);
    N->force_player_max_hit = false;
}

static void test_dragon_dagger_two_hits(void) {
    reset_gear();
    wear(EQUIP_WEAPON, 1215, 0);
    attack(true);
    assert(N->num_pending_hits == 2);
    assert(P->special_energy == 7500);
}

static void test_scythe_hits_by_size(void) {
    reset_gear();
    wear(EQUIP_WEAPON, 22325, 0);
    attack(false);
    assert(N->num_pending_hits == 1);  // the dummy is one tile
}

static void test_landing_effects(void) {
    reset_gear();
    N->force_player_max_hit = true;
    wear(EQUIP_WEAPON, 13576, 0);  // Dragon warhammer
    attack(true);
    RcPendingHit hit = N->pending_hits[0];
    rc_content_special_landed(W, N, &hit, hit.damage);
    assert(N->stats[1] == 70);

    reset_gear();
    wear(EQUIP_WEAPON, 11808, 0);  // Zamorak godsword
    attack(true);
    hit = N->pending_hits[0];
    assert(hit.flags & RC_HIT_FLAG_SPECIAL);
    rc_content_special_landed(W, N, &hit, hit.damage);
    assert(N->immobilized_until == W->tick + 32);

    reset_gear();
    P->current_hp = 100;
    wear(EQUIP_WEAPON, 12926, 16383);  // Toxic blowpipe
    attack(true);
    assert(N->num_pending_hits == 1);
    hit = N->pending_hits[0];
    rc_content_special_landed(W, N, &hit, 40);
    assert(P->current_hp == 300);
    N->force_player_max_hit = false;
}

static void test_instant_specials(void) {
    reset_gear();
    int before = P->skills.boosted_level[SKILL_DEFENCE];
    wear(EQUIP_WEAPON, 35, 0);  // Excalibur
    refresh();
    rc_combat_toggle_special(W);
    rc_world_tick(W);
    assert(!P->combat.special_pending);
    assert(P->special_energy == 0);
    assert(P->skills.boosted_level[SKILL_DEFENCE] == P->skills.base_level[SKILL_DEFENCE] + 8);
    P->skills.boosted_level[SKILL_DEFENCE] = before;

    reset_gear();
    P->special_energy = 500;
    wear(EQUIP_WEAPON, 35, 0);
    rc_combat_toggle_special(W);
    rc_world_tick(W);
    assert(P->special_energy == 500 && !P->combat.special_pending);

    reset_gear();
    wear(EQUIP_WEAPON, 4153, 0);  // Granite maul
    refresh();
    P->attack_target = N->uid;
    P->attack_timer = 5;
    int (*activate)(RcWorld *) = W->combat_hooks.activate_player_special;
    assert(activate(W) == 0 && P->attack_timer == 0);
    P->combat.special_pending = false;
    P->attack_target = -1;
}

static void test_power_of_death(void) {
    reset_gear();
    wear(EQUIP_WEAPON, 11791, 0);  // Staff of the dead
    refresh();
    rc_combat_toggle_special(W);
    rc_world_tick(W);
    RcPendingHit melee = {.attack_style = COMBAT_MELEE_SLASH, .source_idx = N->uid};
    assert(rc_content_gear_incoming(W, &melee, 40) == 20);
    RcPendingHit magic = {.attack_style = COMBAT_MAGIC, .source_idx = N->uid};
    assert(rc_content_gear_incoming(W, &magic, 40) == 40);
}

static void test_ammo_rules(void) {
    const char *failure = NULL;
    reset_gear();
    wear(EQUIP_WEAPON, 34681, 0);
    wear(EQUIP_AMMO, 34684, 0);
    assert(rc_content_ranged_resource_slot(P, &failure) == EQUIP_AMMO);
    wear(EQUIP_AMMO, 9144, 0);
    assert(rc_content_ranged_resource_slot(P, &failure) == RC_RANGED_RESOURCE_INVALID);
    reset_gear();
    wear(EQUIP_WEAPON, 859, 0);    // Magic longbow
    wear(EQUIP_AMMO, 33589, 0);    // Seeking amethyst arrow
    assert(rc_content_ranged_resource_slot(P, &failure) == EQUIP_AMMO);
    wear(EQUIP_AMMO, 33595, 0);    // Seeking dragon arrow needs a tier 60 bow
    assert(rc_content_ranged_resource_slot(P, &failure) == RC_RANGED_RESOURCE_INVALID);
    reset_gear();
    wear(EQUIP_WEAPON, 25865, 100);  // Bow of Faerdhinen fires from charges
    assert(rc_content_ranged_resource_slot(P, &failure) == RC_RANGED_RESOURCE_NONE);
    wear(EQUIP_WEAPON, 25865, 0);
    assert(rc_content_ranged_resource_slot(P, &failure) == RC_RANGED_RESOURCE_INVALID);
}

static void test_charges_empty_the_weapon(void) {
    reset_gear();
    wear(EQUIP_WEAPON, 25865, 1);
    refresh();
    assert(rc_content_magic_consume_charge(W, 25865));
    assert(P->equipment[EQUIP_WEAPON].item_id == 25862);
    assert(rc_content_magic_charge_capacity(12926) == 16383);
}

static void test_soul_stacks(void) {
    reset_gear();
    wear(EQUIP_WEAPON, 28338, 0);
    const char *failure = NULL;
    assert(rc_content_special_cost(W, P, N, 28338, &failure) == -1 && failure);
    P->content_gear[RC_GEAR_STATE_SOUL_STACKS] = 5;
    P->content_gear[RC_GEAR_STATE_SOUL_TICK] = (int)W->tick;
    assert(rc_content_special_cost(W, P, N, 28338, &failure) == 0);
}

static void test_arrivals(void) {
    reset_gear();
    int home = N->x;
    N->x = P->x + 4;
    wear(EQUIP_WEAPON, 19481, 0);  // Heavy ballista
    refresh();
    assert(rc_content_player_hit_delay(W, N, NULL, 0) == 3);
    N->x = P->x + 5;
    assert(rc_content_player_hit_delay(W, N, NULL, 0) == 4);
    wear(EQUIP_WEAPON, 861, 0);    // Magic shortbow
    refresh();
    assert(rc_content_player_hit_delay(W, N, NULL, 0) == 3);
    wear(EQUIP_WEAPON, 806, 0);    // Bronze dart
    refresh();
    assert(rc_content_player_hit_delay(W, N, NULL, 0) == 2);
    wear(EQUIP_WEAPON, 29000, 0);  // Eclipse atlatl uses the bow rule
    refresh();
    assert(rc_content_player_hit_delay(W, N, NULL, 0) == 3);
    wear(EQUIP_WEAPON, 28922, 10);  // Tonalztics of Ralos
    refresh();
    assert(rc_content_player_hit_delay(W, N, NULL, 0) == 3);
    N->x = P->x + 9;
    wear(EQUIP_WEAPON, 11785, 0);  // Armadyl crossbow
    refresh();
    assert(rc_content_player_hit_delay(W, N, NULL, 0) == 4);
    P->combat.special_pending = true;
    assert(rc_content_player_hit_delay(W, N, NULL, 0) == 3);
    wear(EQUIP_WEAPON, 27655, 100);  // Webweaver bow
    refresh();
    assert(rc_content_player_hit_delay(W, N, NULL, 0) == 2);
    assert(rc_content_player_hit_delay(W, N, NULL, 1) == 3);
    wear(EQUIP_WEAPON, 13652, 0);  // Dragon claws
    refresh();
    assert(rc_content_player_hit_delay(W, N, NULL, 1) == 1);
    assert(rc_content_player_hit_delay(W, N, NULL, 2) == 2);
    N->x = home;
}

static void test_toxins(void) {
    reset_gear();
    assert(rc_npc_apply_poison(W, N, 20));
    assert(N->toxin_severity == 20 && N->toxin_tick_counter == 30);
    N->toxin_tick_counter = 7;
    assert(rc_npc_apply_poison(W, N, 25));
    assert(N->toxin_severity == 25 && N->toxin_tick_counter == 7);
    N->toxin_tick_counter = 0;
    rc_npc_status_tick(W, N);
    assert(N->pending_hits[0].damage == 5 && N->toxin_severity == 24);
    assert(rc_npc_apply_venom(W, N));
    assert(N->toxin_venom && N->toxin_severity == 6 && N->toxin_tick_counter == 30);
    assert(!rc_npc_apply_poison(W, N, 30));
    for (int damage = 6; damage <= 22; damage += 2) {
        N->num_pending_hits = 0;
        N->toxin_tick_counter = 0;
        rc_npc_status_tick(W, N);
        assert(N->pending_hits[0].damage == (damage < 20 ? damage : 20));
    }
    assert(N->toxin_severity == 20);
}

static void test_venom_sources(void) {
    reset_gear();
    wear(EQUIP_HEAD, 12931, 0);    // Serpentine helm
    wear(EQUIP_WEAPON, 12899, 100);  // Trident of the swamp
    refresh();
    RcPendingHit hit = {.attack_style = COMBAT_MAGIC, .accurate = true, .weapon_id = 12899};
    rc_content_gear_landed(W, N, &hit, 5);
    assert(N->toxin_venom);
    reset_gear();
    wear(EQUIP_HEAD, 13197, 0);    // Tanzanite helm
    wear(EQUIP_WEAPON, 4151, 0);
    refresh();
    hit = (RcPendingHit){.attack_style = COMBAT_MELEE_SLASH, .accurate = true, .weapon_id = 4151};
    rc_content_gear_landed(W, N, &hit, 0);
    assert(!N->toxin_venom && "misses never envenom");
    int venomed = 0;
    for (int i = 0; i < 6000; i++) {
        N->toxin_venom = false;
        N->toxin_severity = 0;
        rc_content_gear_landed(W, N, &hit, 5);
        venomed += N->toxin_venom;
    }
    assert(venomed > 850 && venomed < 1150);
}

static void test_bolts(void) {
    reset_gear();
    wear(EQUIP_WEAPON, 9185, 0);   // Rune crossbow
    wear(EQUIP_AMMO, 9241, 5);     // Emerald bolts (e)
    refresh();
    assert(rc_content_gear_row(9241)->family == GEAR_EMERALD_BOLTS_E);
    RcPendingHit hit = {.attack_style = COMBAT_RANGED, .accurate = true, .weapon_id = 9185,
                        .flags = GEAR_HIT_POISON};
    rc_content_gear_landed(W, N, &hit, 0);
    assert(N->toxin_severity == 25);
    hit.flags = GEAR_HIT_BIND;
    rc_content_gear_landed(W, N, &hit, 0);
    assert(N->immobilized_until == W->tick + 8);
}

static void test_echo_boots(void) {
    reset_gear();
    wear(EQUIP_BOOTS, 28945, 2);
    RcPendingHit hit = {.attack_style = COMBAT_MELEE_SLASH, .damage = 5, .source_idx = N->uid};
    rc_content_gear_after_hit_taken(W, &hit, 5);
    assert(N->num_pending_hits == 1 && N->pending_hits[0].damage == 1);
    assert(P->equipment[EQUIP_BOOTS].state_id == 1);
    P->content_gear[RC_GEAR_STATE_ECHO_OFF] = 1;
    N->num_pending_hits = 0;
    rc_content_gear_after_hit_taken(W, &hit, 5);
    assert(N->num_pending_hits == 0 && "toggled off");
    P->content_gear[RC_GEAR_STATE_ECHO_OFF] = 0;
    int home = N->x;
    N->x = P->x + 2;
    rc_content_gear_after_hit_taken(W, &hit, 5);
    assert(N->num_pending_hits == 0 && "outside the 3x3");
    N->x = home;
    rc_content_gear_after_hit_taken(W, &hit, 0);
    assert(N->num_pending_hits == 0 && "only damage recoils");
    P->equipment[EQUIP_BOOTS].state_id = 0;
    rc_content_gear_after_hit_taken(W, &hit, 5);
    assert(N->num_pending_hits == 0 && P->equipment[EQUIP_BOOTS].item_id == 28945);
}

static void test_holy_water(void) {
    reset_gear();
    wear(EQUIP_WEAPON, 732, 5);
    refresh();
    assert(P->combat_style == COMBAT_RANGED && rc_player_attack_speed(P) == 4);
    RcGearAttack a;
    rc_content_gear_attack(&a, W, N, NULL, false);
    RcCombatCalc calc = rc_calc_ranged(P, N, false);
    rc_content_gear_ranged(&a, &calc);
    assert(calc.max_hit == 0 && "only demons take damage");
    int def = rc_npc_def_find(DEMON_NPC);
    assert(def >= 0);
    int index = rc_npc_spawn(W, def, P->x + 2, P->y, P->plane);
    assert(index >= 0);
    RcNpc *demon = &W->npcs[index];
    demon->disable_wander = true;
    rc_content_gear_attack(&a, W, demon, NULL, false);
    calc = rc_calc_ranged(P, demon, false);
    rc_content_gear_ranged(&a, &calc);
    int base = (109 * (P->equipment_bonuses[EQ_RANGED_STR] + 64) + 320) / 640;
    assert(calc.max_hit == base + base * 60 / 100);
    int defence = demon->stats[1];
    RcPendingHit hit = {.attack_style = COMBAT_RANGED, .accurate = true, .weapon_id = 732};
    rc_content_gear_landed(W, demon, &hit, 5);
    assert(demon->stats[1] == defence - defence * 5 / 100);
    int drained = demon->stats[1];
    rc_content_gear_landed(W, demon, &hit, 5);
    assert(demon->stats[1] == drained && "the drain does not stack");
    assert(rc_npc_remove(W, demon->uid));
}

static void test_rondache(void) {
    reset_gear();
    wear(EQUIP_SHIELD, RONDACHE, 0);
    RcPendingHit hit = {.attack_style = COMBAT_MELEE_SLASH, .damage = 9, .source_idx = N->uid};
    assert(rc_content_gear_incoming(W, &hit, 9) == 9 && "needs an attack after equipping");
    assert(rc_content_rondache_charges(W) == 0);
    P->content_gear[RC_GEAR_STATE_RONDACHE_ARMED] = 1;
    assert(rc_content_gear_incoming(W, &hit, 9) == 7);
    assert(rc_content_rondache_charges(W) == 2);
    assert(rc_content_gear_incoming(W, &hit, 9) == 7);
    assert(rc_content_rondache_charges(W) == 2 && "one gain per 2 ticks");
    W->tick += 2;
    assert(rc_content_gear_incoming(W, &hit, 2) == 1);
    assert(rc_content_rondache_charges(W) == 3);
    W->tick += 50;
    assert(rc_content_rondache_charges(W) == 2 && "decays in combat");
    for (int i = 0; i < 10; i++) {
        W->tick += 2;
        (void)rc_content_gear_incoming(W, &hit, 9);
    }
    int charges = rc_content_rondache_charges(W);
    assert(charges == 20);

    wear(EQUIP_WEAPON, 4151, 0);
    refresh();
    P->special_energy = 2500;
    P->attack_timer = 3;
    N->stats[1] = 0;
    P->attack_target = -1;
    assert(!rc_content_shield_slam(W) && "no target");
    P->attack_target = N->uid;
    assert(rc_content_shield_slam(W));
    assert(N->num_pending_hits == 1 && N->pending_hits[0].max_hit == 24);
    assert(N->pending_hits[0].damage == 0 || N->pending_hits[0].damage == 24);
    assert(P->attack_timer == 3 && P->special_energy == 2500);
    assert(rc_content_rondache_charges(W) == 0);
    assert(W->combat_attack_events[W->combat_attack_event_count - 1].weapon_item_id == RONDACHE);
    assert(!rc_content_shield_slam(W) && "needs 10 charges");
    (void)rc_content_gear_incoming(W, &hit, 9);
    assert(rc_content_rondache_charges(W) == 0 && "cooldown after the slam");
    P->attack_target = -1;
}

static void test_sunfire_runes(void) {
    reset_gear();
    int fire = rc_spell_find("Fire Surge");
    assert(fire >= 0);
    const RcSpellDef *spell = rc_spell_def_get(fire);
    assert(!rc_content_has_sunfire_runes(P, spell));
    P->inventory[0] = (RcInvSlot){.item_id = 28929, .quantity = 20};
    assert(rc_content_has_sunfire_runes(P, spell));
    RcGearAttack a;
    rc_content_gear_attack(&a, W, N, spell, false);
    RcCombatCalc calc = {.max_hit = 37};
    rc_content_gear_spell_floor(&a, &calc);
    assert(a.min_hit == 3);
    P->inventory[0] = (RcInvSlot){.item_id = -1};
}

static void test_lithic_sceptre(void) {
    reset_gear();
    wear(EQUIP_WEAPON, 33255, 100);
    refresh();
    RcCombatCalc calc;
    int speed = 0;
    const char *failure = NULL;
    P->skills.boosted_level[SKILL_MAGIC] = 50;
    assert(rc_content_prepare_magic(W, N, NULL, &calc, &speed, &failure));
    assert(calc.max_hit == 10);
    P->skills.boosted_level[SKILL_MAGIC] = 99;
    assert(rc_content_prepare_magic(W, N, NULL, &calc, &speed, &failure));
    assert(calc.max_hit == 23);
    assert(rc_content_special_cost(W, P, N, 33255, &failure) == -1);
    RcPendingHit hit = {.attack_style = COMBAT_MAGIC, .accurate = true, .weapon_id = 33255};
    for (int i = 0; i < 20; i++) rc_content_gear_landed(W, N, &hit, 0);
    assert(rc_content_shatter_stacks(W) == 14);
    W->tick += 50;
    assert(rc_content_shatter_stacks(W) == 13);
    assert(rc_content_special_cost(W, P, N, 33255, &failure) == 0);
    W->tick += 450;
    assert(rc_content_shatter_stacks(W) == 4);
}

// The best ammunition `weapon` can fire, or -1.
static int ammo_for(const RcGearRow *weapon) {
    int best = -1, tier = -1;
    for (int id = 0; id < RC_GEAR_ID_LIMIT; id++) {
        const RcGearRow *row = rc_content_gear_row(id);
        const RcItemDef *def = rc_item_def_get(id);
        if (!def || def->equip_slot != EQUIP_AMMO || row->ammo_kind != weapon->ammo_kind
                || row->ammo_tier > weapon->ammo_tier || row->ammo_tier <= tier)
            continue;
        best = id;
        tier = row->ammo_tier;
    }
    return best;
}

static int item_in_family(int family) {
    for (int id = 0; id < RC_GEAR_ID_LIMIT; id++)
        if (rc_content_gear_row(id)->family == family) return id;
    return -1;
}

static void wear_set(int helm, int body, int legs) {
    wear(EQUIP_HEAD, item_in_family(helm), 0);
    wear(EQUIP_BODY, item_in_family(body), 0);
    wear(EQUIP_LEGS, item_in_family(legs), 0);
}

static void prepare_weapon(int id) {
    const RcGearRow *row = rc_content_gear_row(id);
    const RcItemDef *def = rc_item_def_get(id);
    reset_gear();
    wear(EQUIP_WEAPON, id, (int)rc_content_magic_charge_capacity(id));
    if (def->stackable) P->equipment[EQUIP_WEAPON].quantity = 100;
    if (row->ammo_kind != AMMO_NONE && row->ammo_kind != AMMO_INTERNAL) {
        int ammo = id == 8880 ? 8882 : ammo_for(row);  // Dorgeshuun crossbow, bone bolts
        if (ammo >= 0) P->equipment[EQUIP_AMMO] = (RcInvSlot){.item_id = ammo, .quantity = 100};
    }
    if (row->family == GEAR_TONALZTICS) P->equipment[EQUIP_WEAPON].state_id = 100;
    switch (row->special) {
    case SPECIAL_BLOOD_INFUSION:
        wear_set(GEAR_BLOOD_MOON_HELM, GEAR_BLOOD_MOON_CHESTPLATE, GEAR_BLOOD_MOON_TASSETS);
        break;
    case SPECIAL_BREAK_SHACKLES:
        wear_set(GEAR_BLUE_MOON_HELM, GEAR_BLUE_MOON_CHESTPLATE, GEAR_BLUE_MOON_TASSETS);
        break;
    case SPECIAL_ECLIPSE:
        wear_set(GEAR_ECLIPSE_MOON_HELM, GEAR_ECLIPSE_MOON_CHESTPLATE, GEAR_ECLIPSE_MOON_TASSETS);
        break;
    case SPECIAL_BEHEAD:
        P->content_gear[RC_GEAR_STATE_SOUL_STACKS] = 5;
        P->content_gear[RC_GEAR_STATE_SOUL_TICK] = (int)W->tick;
        break;
    case SPECIAL_EXPLOSIVE_SHATTER:
        P->content_gear[RC_GEAR_STATE_SHATTER_STACKS] = 14;
        P->content_gear[RC_GEAR_STATE_SHATTER_TICK] = (int)W->tick;
        break;
    case SPECIAL_SCATTER_ASHES:
        P->current_spellbook = RC_SPELL_BOOK_ARCEUUS;
        break;
    default:
        break;
    }
}

static const char *launch_failure(void) {
    return P->combat.failure_reason ? P->combat.failure_reason : "no attack";
}

static RcNpc *spawn_beside(int npc_id) {
    int def = rc_npc_def_find(npc_id);
    assert(def >= 0);
    int index = rc_npc_spawn(W, def, P->x + 2, P->y, P->plane);
    assert(index >= 0);
    W->npcs[index].disable_wander = true;
    return &W->npcs[index];
}

static int ranged_defence_roll(RcNpc *npc, int weapon_id) {
    wear(EQUIP_WEAPON, weapon_id, 0);
    refresh();
    RcGearAttack a;
    rc_content_gear_attack(&a, W, npc, NULL, false);
    RcCombatCalc calc = rc_calc_ranged(P, npc, false);
    rc_content_gear_ranged(&a, &calc);
    return calc.defence_roll;
}

static void test_npc_weakness_and_ranged_defence(void) {
    reset_gear();
    RcNpc *spectre = spawn_beside(2);  // Aberrant spectre: air 50%, ranged 15/-15/15
    int defence = spectre->stats[1] + 9;
    assert(ranged_defence_roll(spectre, 861) == defence * (-15 + 64));   // bow
    assert(ranged_defence_roll(spectre, 9185) == defence * (15 + 64));   // crossbow
    assert(ranged_defence_roll(spectre, 806) == defence * (15 + 64));    // thrown
    reset_gear();
    wear(EQUIP_WEAPON, 1381, 0);
    refresh();
    RcGearAttack a;
    RcCombatCalc calc = {0};
    rc_content_gear_attack(&a, W, spectre, rc_spell_def_get(rc_spell_find("Wind Surge")), false);
    calc = rc_calc_magic(P, spectre, 21);
    int plain_roll = calc.attack_roll;
    rc_content_gear_magic(&a, &calc, 21);
    assert(calc.max_hit == 31 && calc.attack_roll == plain_roll + plain_roll / 2);
    rc_content_gear_attack(&a, W, spectre, rc_spell_def_get(rc_spell_find("Fire Surge")), false);
    calc = rc_calc_magic(P, spectre, 21);
    rc_content_gear_magic(&a, &calc, 21);
    assert(calc.max_hit == 21);
    assert(rc_npc_remove(W, spectre->uid));
}

static void test_magic_defence_drain_stops_at_zero(void) {
    reset_gear();
    int defence = N->stats[5] + 9;
    N->defence_bonus[COMBAT_MAGIC - 1] = 10;
    N->magic_defence_drain = 50;
    assert(rc_npc_defensive_roll(N, COMBAT_MAGIC) == defence * 64);
    N->defence_bonus[COMBAT_MAGIC - 1] = -10;
    assert(rc_npc_defensive_roll(N, COMBAT_MAGIC) == defence * 54);
    N->defence_bonus[COMBAT_MAGIC - 1] = 0;
    N->magic_defence_drain = 0;
}

static void test_zaryte_guarantee_needs_a_hit(void) {
    reset_gear();
    wear(EQUIP_WEAPON, 26374, 0);
    wear(EQUIP_AMMO, 9242, 100);  // Ruby bolts (e)
    P->equipment[EQUIP_AMMO].quantity = 100;
    refresh();
    RcGearAttack a;
    rc_content_gear_attack(&a, W, N, NULL, true);
    assert(a.uses_ammo_slot);
    RcCombatCalc calc = rc_calc_ranged(P, N, true);
    int procs = 0;
    for (int i = 0; i < 4000; i++) {
        RcPendingHit hit = {.damage = 0, .accurate = false};
        rc_content_gear_after_hits(&a, &calc, &hit, 1);
        procs += hit.damage > 0;
        P->current_hp = 990;
    }
    assert(procs > 150 && procs < 350);  // the usual 6%
    RcPendingHit landed = {.damage = 1, .accurate = true};
    rc_content_gear_after_hits(&a, &calc, &landed, 1);
    assert(landed.damage == 110);
}

static void test_blood_infusion(void) {
    reset_gear();
    prepare_weapon(item_in_family(GEAR_DUAL_MACUAHUITL));
    refresh();
    RcGearAttack a;
    rc_content_gear_attack(&a, W, N, NULL, true);
    for (int i = 0; i < 500; i++) {
        RcCombatCalc calc = {.attack_roll = 40000, .defence_roll = 100, .max_hit = 40};
        RcPendingHit hits[4] = {0};
        const char *failure = NULL;
        P->current_hp = 990;
        a.min_hit = 0;
        assert(rc_content_special_hits(&a, &calc, hits, 4, &failure) == 2);
        assert(!hits[0].accurate || (hits[0].damage >= 10 && hits[0].damage <= 25));
        assert(!hits[1].accurate || (hits[1].damage >= 10 && hits[1].damage <= 25));
    }
}

static void test_post_roll_rules_cover_every_hit(void) {
    reset_gear();
    RcNpc *corp = spawn_beside(319);
    wear(EQUIP_WEAPON, 13652, 0);  // Dragon claws
    refresh();
    RcGearAttack a;
    rc_content_gear_attack(&a, W, corp, NULL, true);
    RcCombatCalc calc = rc_calc_melee(P, corp);
    RcPendingHit hits[2] = {{.damage = 30, .accurate = true}, {.damage = 15, .accurate = true}};
    rc_content_gear_after_hits(&a, &calc, hits, 2);
    assert(hits[0].damage == 15 && hits[1].damage == 7);
    assert(rc_npc_remove(W, corp->uid));
}

static void test_tonalztics_uses_defence_bonus(void) {
    reset_gear();
    wear(EQUIP_WEAPON, 28919, 0);
    refresh();
    N->stats[1] = 100;
    RcCombatCalc base = {.attack_roll = 20000, .defence_roll = 109 * 164, .max_hit = 30};
    RcPendingHit hits[4] = {0};
    const char *failure = NULL;
    int landed = 0;
    for (int i = 0; i < 4000; i++) {
        assert(rc_content_ralos_hits(W, N, &base, false, hits, 4, &failure) == 1);
        landed += hits[0].accurate;
    }
    float chance = rc_hit_chance(base.attack_roll, base.defence_roll);
    assert(landed > (int)(chance * 3400) && landed < (int)(chance * 4600));
}

static void test_weaken_scales_with_base_levels(void) {
    reset_gear();
    RcNpc *demon = spawn_beside(DEMON_NPC);
    int def = demon->stats[1];
    demon->base_stats[1] = def;
    RcPendingHit hit = {.accurate = true, .weapon_id = 19675, .attack_style = COMBAT_MELEE_STAB};
    rc_content_special_landed(W, demon, &hit, 10);
    rc_content_special_landed(W, demon, &hit, 10);
    assert(demon->stats[1] == def - 2 * (def / 10 + 1));
    assert(rc_npc_remove(W, demon->uid));
}

// First NPC definition with all of `want` and none of `avoid`.
static RcNpc *spawn_with(uint32_t want, uint32_t avoid) {
    for (int i = 0; i < g_npc_def_count; i++) {
        RcNpc probe = {.def_id = i};
        uint32_t attrs = rc_content_npc_attributes(W, &probe);
        if ((attrs & want) != want || (attrs & avoid)) continue;
        int index = rc_npc_spawn(W, i, P->x + 2, P->y, P->plane);
        assert(index >= 0);
        W->npcs[index].disable_wander = true;
        W->npcs[index].current_hp = W->npcs[index].spawn_hp = 500;
        return &W->npcs[index];
    }
    assert(!"no NPC has those attributes");
    return NULL;
}

// Passive multipliers on one melee hit against the matching NPC.
static void test_melee_passives_against_targets(void) {
    static const struct { int family; uint32_t attr; int roll_num, roll_den, hit_num, hit_den; } cases[] = {
        {GEAR_ARCLIGHT, RC_NPC_ATTR_DEMON, 170, 100, 170, 100},
        {GEAR_EMBERLIGHT, RC_NPC_ATTR_DEMON, 170, 100, 170, 100},
        {GEAR_SILVERLIGHT, RC_NPC_ATTR_DEMON, 160, 100, 160, 100},
        {GEAR_BURNING_CLAWS, RC_NPC_ATTR_DEMON, 105, 100, 105, 100},
        {GEAR_DRAGON_HUNTER_LANCE, RC_NPC_ATTR_DRAGON, 6, 5, 6, 5},
        {GEAR_KERIS, RC_NPC_ATTR_KALPHITE, 1, 1, 133, 100},
        {GEAR_KERIS_BREACHING, RC_NPC_ATTR_KALPHITE, 133, 100, 133, 100},
        {GEAR_BARRONITE_MACE, RC_NPC_ATTR_GOLEM, 1, 1, 23, 20},
        {GEAR_GRANITE_HAMMER, RC_NPC_ATTR_GOLEM, 13, 10, 13, 10},
        {GEAR_LEAF_BLADED_BATTLEAXE, RC_NPC_ATTR_LEAFY, 1, 1, 47, 40},
    };
    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        reset_gear();
        RcNpc *npc = spawn_with(cases[i].attr, 0);
        wear(EQUIP_WEAPON, item_in_family(cases[i].family), 0);
        refresh();
        RcGearAttack a;
        rc_content_gear_attack(&a, W, npc, NULL, false);
        RcCombatCalc plain = rc_calc_melee(P, npc), passive = plain;
        rc_content_gear_melee(&a, &passive);
        if (passive.attack_roll != plain.attack_roll * cases[i].roll_num / cases[i].roll_den)
            fprintf(stderr, "case %u item %d npc %s: roll %d plain %d, hit %d plain %d\n", i,
                    P->equipment[EQUIP_WEAPON].item_id, rc_npc_def_for_npc(W, npc)->name,
                    passive.attack_roll, plain.attack_roll, passive.max_hit, plain.max_hit);
        assert(passive.attack_roll == plain.attack_roll * cases[i].roll_num / cases[i].roll_den);
        assert(passive.max_hit == plain.max_hit * cases[i].hit_num / cases[i].hit_den);
        assert(rc_npc_remove(W, npc->uid));
    }
    // Salve (e) replaces the Slayer helmet's bonus on undead.
    reset_gear();
    RcNpc *undead = spawn_with(RC_NPC_ATTR_UNDEAD, 0);
    wear(EQUIP_WEAPON, 4151, 0);
    wear(EQUIP_AMULET, item_in_family(GEAR_SALVE_AMULET_E), 0);
    refresh();
    RcGearAttack a;
    rc_content_gear_attack(&a, W, undead, NULL, false);
    RcCombatCalc plain = rc_calc_melee(P, undead), passive = plain;
    rc_content_gear_melee(&a, &passive);
    assert(passive.attack_roll == plain.attack_roll * 6 / 5);
    assert(rc_npc_remove(W, undead->uid));
}

static void test_vampyre_tiers(void) {
    RcCombatCalc calc = {.max_hit = 50};
    RcGearAttack a;
    reset_gear();
    RcNpc *two = spawn_with(RC_NPC_ATTR_VAMPYRE_2, 0);
    wear(EQUIP_WEAPON, 4151, 0);
    refresh();
    rc_content_gear_attack(&a, W, two, NULL, false);
    assert(rc_content_gear_blocked(&a));
    wear(EQUIP_RING, item_in_family(GEAR_EFARITAYS_AID), 0);
    refresh();
    rc_content_gear_attack(&a, W, two, NULL, false);
    assert(!rc_content_gear_blocked(&a));
    assert(rc_content_gear_after_roll(&a, &calc, 40, true) == 20);
    P->equipment[EQUIP_RING] = (RcInvSlot){.item_id = -1};
    wear(EQUIP_WEAPON, item_in_family(GEAR_SILVERLIGHT), 0);
    refresh();
    rc_content_gear_attack(&a, W, two, NULL, false);
    assert(!rc_content_gear_blocked(&a));
    assert(rc_content_gear_after_roll(&a, &calc, 40, true) == 10);
    wear(EQUIP_WEAPON, item_in_family(GEAR_BLISTERWOOD_FLAIL), 0);
    refresh();
    rc_content_gear_attack(&a, W, two, NULL, false);
    assert(rc_content_gear_after_roll(&a, &calc, 40, true) == 50);
    assert(rc_npc_remove(W, two->uid));
    reset_gear();
    RcNpc *three = spawn_with(RC_NPC_ATTR_VAMPYRE_3, 0);
    wear(EQUIP_WEAPON, item_in_family(GEAR_SILVERLIGHT), 0);
    refresh();
    rc_content_gear_attack(&a, W, three, NULL, false);
    assert(rc_content_gear_blocked(&a));
    wear(EQUIP_WEAPON, item_in_family(GEAR_IVANDIS_FLAIL), 0);
    refresh();
    rc_content_gear_attack(&a, W, three, NULL, false);
    assert(!rc_content_gear_blocked(&a));
    assert(rc_npc_remove(W, three->uid));
    reset_gear();
    RcNpc *one = spawn_with(RC_NPC_ATTR_VAMPYRE_1, 0);
    wear(EQUIP_WEAPON, item_in_family(GEAR_SILVERLIGHT), 0);
    refresh();
    rc_content_gear_attack(&a, W, one, NULL, false);
    assert(rc_content_gear_after_roll(&a, &calc, 40, true) == 44);
    assert(rc_npc_remove(W, one->uid));
}

// Every bolt effect fires at about its stated rate from a Rune crossbow.
static void test_bolt_procs(void) {
    static const struct { int family, percent; } bolts[] = {
        {GEAR_OPAL_BOLTS_E, 5}, {GEAR_JADE_BOLTS_E, 6}, {GEAR_PEARL_BOLTS_E, 6},
        {GEAR_RUBY_BOLTS_E, 6}, {GEAR_DIAMOND_BOLTS_E, 10}, {GEAR_DRAGONSTONE_BOLTS_E, 6},
        {GEAR_ONYX_BOLTS_E, 11}, {GEAR_EMERALD_BOLTS_E, 55},
    };
    for (unsigned i = 0; i < sizeof(bolts) / sizeof(bolts[0]); i++) {
        reset_gear();
        wear(EQUIP_WEAPON, 21902, 0);  // Dragon crossbow fires every bolt
        wear(EQUIP_AMMO, item_in_family(bolts[i].family), 100);
        P->equipment[EQUIP_AMMO].quantity = 100;
        P->current_hp = 500;
        refresh();
        RcGearAttack a;
        rc_content_gear_attack(&a, W, N, NULL, false);
        assert(a.uses_ammo_slot);
        RcCombatCalc calc = rc_calc_ranged(P, N, true);
        int procs = 0;
        for (int roll = 0; roll < 5000; roll++) {
            RcPendingHit hit = {.damage = 1, .accurate = true};
            rc_content_gear_after_hits(&a, &calc, &hit, 1);
            procs += P->attack_ammo_effect;
            P->current_hp = 500;
        }
        int want = 50 * bolts[i].percent;
        assert(procs > want - want / 5 && procs < want + want / 5);
    }
}

// Splash specials reach the other NPCs nearby in multi-combat.
static void test_multi_target_specials(void) {
    static const int specials[] = {SPECIAL_SHIELD_BASH, SPECIAL_POWERSTAB, SPECIAL_ANNIHILATE,
                                   SPECIAL_CHAINHIT, SPECIAL_SPEAR_WALL};
    reset_gear();
    RcNpc *other = spawn_beside(TARGET_NPC);
    other->x = N->x;
    other->y = N->y + 1;
    rc_combat_set_multi_combat(W, true);
    for (unsigned i = 0; i < sizeof(specials) / sizeof(specials[0]); i++) {
        int id = 0;
        while (rc_content_gear_row(id)->special != specials[i]) id++;
        prepare_weapon(id);
        other->num_pending_hits = 0;
        attack(true);
        P->combat.special_pending = false;
        P->attack_target = -1;
        if (W->combat_attack_event_count == 0)
            fprintf(stderr, "multi %d %s: %s\n", id, rc_item_def_get(id)->name, launch_failure());
        assert(W->combat_attack_event_count > 0);
        assert(other->num_pending_hits > 0 || specials[i] == SPECIAL_CHAINHIT);
    }
    rc_combat_set_multi_combat(W, false);
    assert(rc_npc_remove(W, other->uid));
}

static void test_claws_when_every_roll_misses(void) {
    reset_gear();
    wear(EQUIP_WEAPON, 13652, 0);
    refresh();
    RcGearAttack a;
    rc_content_gear_attack(&a, W, N, NULL, true);
    for (int i = 0; i < 300; i++) {
        RcCombatCalc calc = {.attack_roll = 1, .defence_roll = 1000000, .max_hit = 40};
        RcPendingHit hits[4] = {0};
        const char *failure = NULL;
        assert(rc_content_special_hits(&a, &calc, hits, 4, &failure) == 4);
        assert(hits[0].damage <= 1 && hits[2].damage == 0 && hits[0].damage == hits[1].damage);
    }
    wear(EQUIP_WEAPON, item_in_family(GEAR_BURNING_CLAWS), 0);
    refresh();
    rc_content_gear_attack(&a, W, N, NULL, true);
    for (int i = 0; i < 300; i++) {
        RcCombatCalc calc = {.attack_roll = 1, .defence_roll = 1000000, .max_hit = 40};
        RcPendingHit hits[4] = {0};
        const char *failure = NULL;
        assert(rc_content_special_hits(&a, &calc, hits, 4, &failure) == 3);
        assert(hits[0].damage <= 1 && hits[1].damage <= hits[0].damage && hits[2].damage == 0);
    }
}

static void test_every_landing_effect(void) {
    for (int id = 0; id < RC_GEAR_ID_LIMIT; id++) {
        const RcGearRow *row = rc_content_gear_row(id);
        if (row->special == SPECIAL_NONE) continue;
        reset_gear();
        for (int stat = 0; stat < 6; stat++) N->stats[stat] = 100;
        RcPendingHit hit = {.accurate = true, .weapon_id = (uint16_t)id,
                            .attack_style = COMBAT_MELEE_SLASH, .flags = RC_HIT_FLAG_SPECIAL};
        rc_content_special_landed(W, N, &hit, 24);
        for (int stat = 0; stat < 6; stat++) assert(N->stats[stat] >= 0);
    }
}

// Every special item through the real attack path: it pays once and hits.
static void test_every_special_launches(void) {
    int fired = 0, failed = 0;
    int (*activate)(RcWorld *) = W->combat_hooks.activate_player_special;
    for (int id = 0; id < RC_GEAR_ID_LIMIT; id++) {
        const RcGearRow *row = rc_content_gear_row(id);
        // Shields have their own triggers; the rest need a target or place this
        // fixture lacks (vampyre juveniles, underwater, Tombs of Amascut).
        if (row->special == SPECIAL_NONE || row->special == SPECIAL_SHIELD_SLAM
                || row->special == SPECIAL_DRAGONFIRE_BLAST || row->special == SPECIAL_RETAINER
                || row->special == SPECIAL_LIQUIFY || row->special == SPECIAL_WRATH_OF_AMASCUT)
            continue;
        prepare_weapon(id);
        refresh();
        bool instant = activate(W) != 0;
        if (!instant) attack(true);
        bool hit = instant || W->combat_attack_event_count > 0 || N->num_pending_hits > 0;
        if (!hit) {
            fprintf(stderr, "special %d %s: %s\n", id, rc_item_def_get(id)->name, launch_failure());
            failed++;
        }
        fired += hit;
        P->combat.special_pending = false;
        P->attack_target = -1;
        P->current_spellbook = RC_SPELL_BOOK_STANDARD;
    }
    fprintf(stderr, "specials fired %d, failed %d\n", fired, failed);
    assert(failed == 0 && fired > 250);
}

// Every weapon with a normal attack, using its first style.
static void test_every_weapon_attacks(void) {
    int fired = 0, failed = 0;
    for (int id = 0; id < RC_GEAR_ID_LIMIT; id++) {
        const RcItemDef *def = rc_item_def_get(id);
        if (!def || !def->loaded || def->equip_slot != EQUIP_WEAPON || !def->weapon_type) continue;
        prepare_weapon(id);
        attack(false);
        bool hit = W->combat_attack_event_count > 0 || N->num_pending_hits > 0;
        if (!hit) {
            fprintf(stderr, "weapon %d %s: %s\n", id, def->name, launch_failure());
            failed++;
        }
        fired += hit;
        P->attack_target = -1;
    }
    fprintf(stderr, "weapons fired %d, failed %d\n", fired, failed);
    assert(fired > 1000);
}

int main(void) {
    RcWorldConfig cfg = rc_preset_base_only();
    cfg.subsystems = RC_SUB_COMBAT | RC_SUB_INVENTORY | RC_SUB_EQUIPMENT;
    cfg.items_path = RC_TEST_SOURCE_DIR "/data/defs/items.bin";
    cfg.npc_defs_path = RC_TEST_SOURCE_DIR "/data/defs/npc_defs.bin";
    cfg.combat_profiles_path = RC_TEST_SOURCE_DIR "/data/defs/combat_visuals.tsv";
    cfg.spells_path = RC_TEST_SOURCE_DIR "/data/defs/spells.bin";
    W = rc_world_create_config(&cfg);
    assert(W);
    rc_content_combat_register(W);
    P = &W->player;
    for (int i = 0; i < SKILL_COUNT; i++) {
        P->skills.base_level[i] = 99;
        P->skills.boosted_level[i] = 99;
    }
    P->x = P->y = 3208;
    rc_test_open_mapsquare(W, P->x, P->y, 0);
    int def = rc_npc_def_find(TARGET_NPC);
    assert(def >= 0);
    int index = rc_npc_spawn(W, def, P->x + 1, P->y, P->plane);
    assert(index >= 0);
    N = &W->npcs[index];
    N->disable_wander = true;
    test_tables();
    test_new_weapon_styles();
    test_void_and_slayer_rolls();
    test_twisted_bow_scaling();
    test_claws_land_four_hits();
    test_dragon_dagger_two_hits();
    test_scythe_hits_by_size();
    test_landing_effects();
    test_instant_specials();
    test_power_of_death();
    test_ammo_rules();
    test_charges_empty_the_weapon();
    test_soul_stacks();
    test_arrivals();
    test_toxins();
    test_venom_sources();
    test_bolts();
    test_echo_boots();
    test_holy_water();
    test_rondache();
    test_lithic_sceptre();
    test_sunfire_runes();
    test_npc_weakness_and_ranged_defence();
    test_magic_defence_drain_stops_at_zero();
    test_zaryte_guarantee_needs_a_hit();
    test_blood_infusion();
    test_post_roll_rules_cover_every_hit();
    test_tonalztics_uses_defence_bonus();
    test_weaken_scales_with_base_levels();
    test_melee_passives_against_targets();
    test_vampyre_tiers();
    test_bolt_procs();
    test_multi_target_specials();
    test_claws_when_every_roll_misses();
    test_every_landing_effect();
    test_every_special_launches();
    test_every_weapon_attacks();
    rc_world_destroy(W);
    return 0;
}
