// Special attacks, keyed by cache enum 906 through gear_rows.inc. Accuracy and
// damage factors follow osrs-dps-calc; b241-only weapons follow the OSRS Wiki.
#include "specials.h"

#include "combat_formula.h"
#include "combat_hit.h"
#include "interaction.h"
#include "items.h"
#include "npc.h"
#include "prayer.h"
#include "rng.h"
#include "skills.h"
#include <stdio.h>
#include <string.h>

static int factor(int value, int num, int den) {
    return (int)((int64_t)value * num / den);
}

static void boost(RcCombatCalc *c, int acc_num, int acc_den, int dmg_num, int dmg_den) {
    c->attack_roll = factor(c->attack_roll, acc_num, acc_den);
    c->max_hit = factor(c->max_hit, dmg_num, dmg_den);
}

static void against(const RcGearAttack *a, RcCombatCalc *c, RcCombatStyle style) {
    c->defence_roll = rc_npc_defensive_roll(a->npc, style);
}

static void refresh(RcCombatCalc *c) {
    c->hit_chance = rc_hit_chance(c->attack_roll, c->defence_roll);
}

static void set_hit(RcPendingHit *hit, const RcCombatCalc *c, int damage, bool accurate) {
    *hit = (RcPendingHit){.damage = damage, .max_hit = c->max_hit, .accurate = accurate};
}

static int roll_hits(RcGearAttack *a, RcCombatCalc *c, RcPendingHit *hits, int count) {
    refresh(c);
    for (int i = 0; i < count; i++) {
        RcCombatRoll roll = rc_content_gear_roll(a, c);
        set_hit(&hits[i], c, roll.damage, roll.accurate);
    }
    return count;
}

// Chainhit's ricochets: 10% energy each, ending at the first miss.
static void chain_hits(RcGearAttack *a, const RcCombatCalc *c) {
    RcWorld *w = a->world;
    if (!rc_combat_is_multi_combat(w)) return;
    int x = a->npc->x, y = a->npc->y, targets = 4;
    for (int i = 0; i < w->npc_count && targets > 0 && a->p->special_energy - 1000 >= 1000; i++) {
        RcNpc *npc = &w->npcs[i];
        if (!npc->active || npc->is_dead || npc->current_hp <= 0 || npc == a->npc
                || npc->plane != a->npc->plane || npc->player_untargetable) continue;
        int dx = npc->x - x, dy = npc->y - y;
        if (dx < -3 || dx > 3 || dy < -3 || dy > 3) continue;
        const RcNpcDef *def = rc_npc_def_for_npc(w, npc);
        if (!def || def->attack_speed <= 0) continue;
        RcCombatCalc other = *c;
        other.defence_roll = rc_npc_defensive_roll(npc, a->style);
        other.hit_chance = rc_hit_chance(other.attack_roll, other.defence_roll);
        RcCombatRoll roll = rc_roll_attack(&other, &w->rng_state, true);
        a->p->special_energy -= 1000;
        if (rc_queue_hit_meta(npc->pending_hits, &npc->num_pending_hits, roll.damage, 2,
                              a->style, RC_HIT_SOURCE_PLAYER, 0, w->tick, 0, other.max_hit)) {
            RcPendingHit *hit = &npc->pending_hits[npc->num_pending_hits - 1];
            hit->accurate = roll.accurate;
            hit->weapon_id = a->weapon_id > 0 ? (uint16_t)a->weapon_id : 0;
            hit->flags = RC_HIT_FLAG_SPECIAL;
        }
        if (!roll.accurate) return;
        targets--;
    }
}

static int roll_between(RcGearAttack *a, int low, int high) {
    return high > low ? low + rc_content_roll(&a->world->rng_state, high - low + 1) : low;
}

// One hit that lands whatever the rolls say, between low and high.
static int sure_hit(RcGearAttack *a, RcCombatCalc *c, RcPendingHit *hits, int low, int high) {
    refresh(c);
    c->max_hit = high;
    set_hit(&hits[0], c, a->npc->force_player_max_hit ? high : roll_between(a, low, high), true);
    return 1;
}

static int ranged_sure_max(const RcGearAttack *a) {
    // Magic bows and the Seercull ignore everything but the ammo's strength.
    const RcItemDef *ammo = rc_item_def_get(a->p->equipment[EQUIP_AMMO].item_id);
    int strength = ammo ? ammo->ranged_strength : 0;
    int level = a->p->skills.boosted_level[SKILL_RANGED] + 10;
    return (int)(((int64_t)level * (strength + 64) + 320) / 640);
}

// Some bosses keep a Defence floor no drain gets past.
static void drain(RcWorld *world, RcNpc *npc, int stat, int amount) {
    const RcNpcCombatRow *row = stat == 1 ? rc_content_npc_row(world, npc) : NULL;
    int floor = row ? row->defence_floor : 0;
    int left = npc->stats[stat] - amount;
    if (left < floor) left = floor < npc->stats[stat] ? floor : npc->stats[stat];
    npc->stats[stat] = left > 0 ? left : 0;
}

static void drain_percent(RcWorld *world, RcNpc *npc, int stat, int percent) {
    drain(world, npc, stat, npc->stats[stat] * percent / 100);
}

// Level drains that carry over: each stat empties before the next one starts,
// and a stat left above zero ends the drain.
static void drain_in_order(RcWorld *world, RcNpc *npc, const int *stats, int count, int amount) {
    for (int i = 0; i < count && amount > 0; i++) {
        int start = npc->stats[stats[i]];
        drain(world, npc, stats[i], amount);
        if (npc->stats[stats[i]] > 0) return;
        amount -= start;
    }
}

static int base_stat(RcWorld *world, const RcNpc *npc, int stat) {
    if (npc->base_stats[stat] > 0) return npc->base_stats[stat];
    const RcNpcDef *def = rc_npc_def_for_npc(world, npc);
    return def ? def->stats[stat] : npc->stats[stat];
}

static void heal(RcWorld *world, int hitpoints, int cap_percent) {
    RcPlayer *p = &world->player;
    int cap = p->max_hp * cap_percent / 100;
    if (p->current_hp >= cap) return;
    p->current_hp += hitpoints * 10;
    if (p->current_hp > cap) p->current_hp = cap;
}

static void burn(RcWorld *world, RcNpc *npc, int damage) {
    uint32_t attrs = rc_content_npc_attributes(world, npc);
    if (attrs & (RC_NPC_ATTR_BURN_IMMUNE | RC_NPC_ATTR_STRONG_BURN_IMMUNE)) return;
    if (npc->burn_remaining <= 0) npc->burn_next_tick = world->tick + 4;
    npc->burn_remaining = (int16_t)(npc->burn_remaining + damage);
    if (npc->burn_remaining > 50) npc->burn_remaining = 50;
}

static void say(RcWorld *world, const char *text) {
    rc_interaction_publish_message(&world->player, text);
}

static bool wearing_all(const RcPlayer *p, int a, int b, int c) {
    return rc_content_wearing(p, a) && rc_content_wearing(p, b) && rc_content_wearing(p, c);
}

// Hits one nearby NPC per roll in multi-combat. `from` centres the search.
static void splash(RcGearAttack *a, const RcCombatCalc *c, int x, int y, int radius,
                   int max_targets, int delay) {
    RcWorld *w = a->world;
    if (!rc_combat_is_multi_combat(w)) return;
    for (int i = 0; i < w->npc_count && max_targets > 0; i++) {
        RcNpc *npc = &w->npcs[i];
        if (!npc->active || npc->is_dead || npc->current_hp <= 0 || npc == a->npc
                || npc->plane != a->npc->plane || npc->player_untargetable) continue;
        int dx = npc->x - x, dy = npc->y - y;
        if (dx < -radius || dx > radius || dy < -radius || dy > radius) continue;
        const RcNpcDef *def = rc_npc_def_for_npc(w, npc);
        if (!def || def->attack_speed <= 0) continue;
        RcCombatCalc other = *c;
        other.defence_roll = rc_npc_defensive_roll(npc, a->style);
        refresh(&other);
        RcCombatRoll roll = rc_roll_attack(&other, &w->rng_state, true);
        rc_queue_hit_meta(npc->pending_hits, &npc->num_pending_hits, roll.damage, delay,
                          a->style, RC_HIT_SOURCE_PLAYER, 0, w->tick, 0, other.max_hit);
        if (npc->num_pending_hits > 0) {
            RcPendingHit *hit = &npc->pending_hits[npc->num_pending_hits - 1];
            hit->accurate = roll.accurate;
            hit->weapon_id = a->weapon_id > 0 ? (uint16_t)a->weapon_id : 0;
            hit->flags = RC_HIT_FLAG_SPECIAL;
        }
        max_targets--;
    }
}

int rc_content_special_cost(const RcWorld *world, const RcPlayer *player, const RcNpc *target,
                            int weapon_id, const char **failure) {
    const RcGearRow *row = rc_content_gear_row(weapon_id);
    if (row->special == SPECIAL_NONE) return -1;
    switch (row->special) {
    case SPECIAL_SHOVE: {
        const RcNpcDef *def = rc_npc_def_for_npc(world, target);
        if (!def || def->size <= 1) break;
        *failure = "That creature is too large to be shoved.";
        return -1;
    }
    case SPECIAL_DRAGONFIRE_BLAST:
        *failure = "This shield's attack is used from its operate option.";
        return -1;
    case SPECIAL_BEHEAD:
        if (rc_content_soul_stacks(world) > 0) return 0;
        *failure = "You need Soul Stacks to use Behead.";
        return -1;
    case SPECIAL_EXPLOSIVE_SHATTER:
        if (rc_content_shatter_stacks(world) >= 5) return 0;
        *failure = "You need 5 Shatter stacks for this.";
        return -1;
    case SPECIAL_BLOOD_INFUSION:
        if (wearing_all(player, GEAR_BLOOD_MOON_HELM, GEAR_BLOOD_MOON_CHESTPLATE, GEAR_BLOOD_MOON_TASSETS)) break;
        *failure = "You need the full blood moon armour set for this.";
        return -1;
    case SPECIAL_BREAK_SHACKLES:
        if (wearing_all(player, GEAR_BLUE_MOON_HELM, GEAR_BLUE_MOON_CHESTPLATE, GEAR_BLUE_MOON_TASSETS)) break;
        *failure = "You need the full blue moon armour set for this.";
        return -1;
    case SPECIAL_ECLIPSE:
        if (wearing_all(player, GEAR_ECLIPSE_MOON_HELM, GEAR_ECLIPSE_MOON_CHESTPLATE, GEAR_ECLIPSE_MOON_TASSETS)) break;
        *failure = "You need the full eclipse moon armour set for this.";
        return -1;
    case SPECIAL_UNLEASH:
        return player->special_energy >= 500 ? player->special_energy - player->special_energy % 500 : -1;
    case SPECIAL_DESCENT_OF_DARKNESS:
        if (player->equipment[EQUIP_AMMO].quantity >= 2) break;
        *failure = "You need two arrows for this special attack.";
        return -1;
    default:
        break;
    }
    return row->special_cost * 100;
}

static int godsword(RcGearAttack *a, RcCombatCalc *c, RcPendingHit *hits, int num, int den) {
    boost(c, 2, 1, 11, 10);
    c->max_hit = factor(c->max_hit, num, den);
    against(a, c, COMBAT_MELEE_SLASH);
    return roll_hits(a, c, hits, 1);
}

static int claws_four(RcGearAttack *a, RcCombatCalc *c, RcPendingHit *hits) {
    against(a, c, COMBAT_MELEE_SLASH);
    refresh(c);
    int max = c->max_hit;
    int first = -1;
    for (int i = 0; i < 4 && first < 0; i++) {
        uint32_t roll = rc_rng_next(&a->world->rng_state) & 0xFFFF;
        if (a->npc->force_player_max_hit || roll < (uint32_t)(c->hit_chance * 65536.0f)) first = i;
    }
    if (first < 0) {
        // All four rolls missed: two thirds of the time it still deals 1, 1.
        int chip = rc_content_roll(&a->world->rng_state, 3) != 0;
        for (int i = 0; i < 4; i++) set_hit(&hits[i], c, i < 2 ? chip : 0, false);
        return 4;
    }
    int low = factor(max, 4 - first, 4);
    int high = max + low - 1;
    int damage = a->npc->force_player_max_hit ? high : roll_between(a, low, high);
    int split[4] = {0};
    if (first == 0) { split[0] = damage / 2; split[1] = damage / 4; split[2] = damage / 8; split[3] = damage / 8 + 1; }
    else if (first == 1) { split[1] = damage / 2; split[2] = damage / 4; split[3] = damage / 4 + 1; }
    else if (first == 2) { split[2] = damage / 2; split[3] = damage / 2 + 1; }
    else split[3] = damage + 1;
    for (int i = 0; i < 4; i++) set_hit(&hits[i], c, split[i], i >= first);
    return 4;
}

static int claws_three(RcGearAttack *a, RcCombatCalc *c, RcPendingHit *hits) {
    refresh(c);
    int max = c->max_hit, first = -1;
    for (int i = 0; i < 3 && first < 0; i++) {
        uint32_t roll = rc_rng_next(&a->world->rng_state) & 0xFFFF;
        if (a->npc->force_player_max_hit || roll < (uint32_t)(c->hit_chance * 65536.0f)) first = i;
    }
    if (first < 0) {
        // All three missed: 1 in 5 deals nothing, the rest one or two chips of 1.
        int outcome = rc_content_roll(&a->world->rng_state, 5);
        int chips = outcome == 0 ? 0 : outcome <= 2 ? 1 : 2;
        for (int i = 0; i < 3; i++) set_hit(&hits[i], c, i < chips, false);
        return 3;
    }
    int low = factor(max, 3 - first, 4);
    int high = max + low;
    int damage = a->npc->force_player_max_hit ? high : roll_between(a, low, high);
    int split[3];
    if (first == 0) { split[0] = damage / 2; split[1] = split[2] = damage / 4; }
    else if (first == 1) { split[0] = split[1] = damage / 2 - 1; split[2] = 2; }
    else { split[0] = damage - 2; split[1] = split[2] = 1; }
    for (int i = 0; i < 3; i++) {
        set_hit(&hits[i], c, split[i], true);
        // Each splat burns on 15, 30 or 45% by the roll that landed first.
        if (rc_content_roll(&a->world->rng_state, 100) < 15 * (first + 1))
            hits[i].flags |= GEAR_HIT_BURN;
    }
    return 3;
}

static int kisten(RcGearAttack *a, RcCombatCalc *c, RcPendingHit *hits) {
    against(a, c, COMBAT_MELEE_CRUSH);
    refresh(c);
    int landed = 0;
    for (int i = 0; i < 4; i++)
        if (a->npc->force_player_max_hit
                || (rc_rng_next(&a->world->rng_state) & 0xFFFF) < (uint32_t)(c->hit_chance * 65536.0f))
            landed++;
    int max = c->max_hit;
    if (!landed) { set_hit(&hits[0], c, 0, false); return 1; }
    int low = factor(max, 70 + (landed - 1) * 20, 100);
    int high = factor(max, 110 + (landed - 1) * 20, 100);
    c->max_hit = high;
    set_hit(&hits[0], c, a->npc->force_player_max_hit ? high : roll_between(a, low, high), true);
    return 1;
}

int rc_content_special_hits(RcGearAttack *a, RcCombatCalc *c, RcPendingHit *hits,
                            int capacity, const char **failure) {
    if (capacity < 4) { *failure = "Insufficient hit slots for this special."; return -1; }
    RcPlayer *p = a->p;
    const RcNpc *npc = a->npc;
    RcWorld *w = a->world;
    const RcGearRow *row = a->weapon;
    bool bh = row->special_variant != 0;
    uint32_t attrs = a->attrs;
    switch (row->special) {
    case SPECIAL_PUNCTURE:
        boost(c, 23, 20, 23, 20); against(a, c, COMBAT_MELEE_SLASH);
        return roll_hits(a, c, hits, 2);
    case SPECIAL_ABYSSAL_PUNCTURE: {
        boost(c, 5, 4, bh ? 19 : 17, 20); against(a, c, COMBAT_MELEE_SLASH); refresh(c);
        RcCombatRoll one = rc_content_gear_roll(a, c);
        set_hit(&hits[0], c, one.damage, one.accurate);
        set_hit(&hits[1], c, one.accurate ? rc_content_roll(&w->rng_state, c->max_hit + 1) : 0, one.accurate);
        return 2;
    }
    case SPECIAL_CLEAVE:
        if (bh) boost(c, 5, 4, 5, 4); else boost(c, 1, 1, 5, 4);
        against(a, c, COMBAT_MELEE_SLASH);
        return roll_hits(a, c, hits, 1);
    case SPECIAL_SHATTER:
        boost(c, 5, 4, 3, 2); against(a, c, COMBAT_MELEE_CRUSH);
        if (bh) c->defence_roll = factor(c->defence_roll, 60, 100);
        return roll_hits(a, c, hits, 1);
    case SPECIAL_SEVER:
        boost(c, 5, 4, 1, 1); against(a, c, COMBAT_MELEE_SLASH);
        return roll_hits(a, c, hits, 1);
    case SPECIAL_WILD_STAB:
        boost(c, 5, 4, 5, 4); against(a, c, COMBAT_MELEE_STAB);
        return roll_hits(a, c, hits, 1);
    case SPECIAL_SHOVE:
        set_hit(&hits[0], c, 0, true);
        return 1;
    case SPECIAL_SMASH:
        if (row->special_cost == 35) {  // Statius: 25-125% of the max hit
            refresh(c);
            RcCombatRoll roll = rc_content_gear_roll(a, c);
            int base = c->max_hit;
            c->max_hit = factor(base, 125, 100);
            set_hit(&hits[0], c, !roll.accurate ? 0 : a->npc->force_player_max_hit ? c->max_hit
                : roll_between(a, base / 4, c->max_hit), roll.accurate);
            return 1;
        }
        boost(c, 1, 1, 3, 2);
        return roll_hits(a, c, hits, 1);
    case SPECIAL_SLICE_AND_DICE: return claws_four(a, c, hits);
    case SPECIAL_BURNING_BARRAGE: return claws_three(a, c, hits);
    case SPECIAL_THE_JUDGEMENT: return godsword(a, c, hits, 5, 4);
    case SPECIAL_WARSTRIKE: return godsword(a, c, hits, 11, 10);
    case SPECIAL_HEALING_BLADE:
    case SPECIAL_ICE_CLEAVE:
    case SPECIAL_BLOOD_SACRIFICE: return godsword(a, c, hits, 1, 1);
    case SPECIAL_SUNDER:
        boost(c, 2, 1, bh ? 5 : 11, bh ? 4 : 10);
        return roll_hits(a, c, hits, 1);
    case SPECIAL_EVISCERATE:
        boost(c, 3, 2, 1, 1);
        return roll_hits(a, c, hits, 1);
    case SPECIAL_FEINT: {
        against(a, c, COMBAT_MELEE_STAB);
        c->defence_roll = factor(c->defence_roll, 1, 4);
        refresh(c);
        RcCombatRoll roll = rc_content_gear_roll(a, c);
        int base = c->max_hit;
        c->max_hit = factor(base, 120, 100);
        set_hit(&hits[0], c, !roll.accurate ? 0 : a->npc->force_player_max_hit ? c->max_hit
            : roll_between(a, base / 5, c->max_hit), roll.accurate);
        return 1;
    }
    case SPECIAL_PULVERIZE:
        boost(c, 5, 4, 1, 1);
        return roll_hits(a, c, hits, 1);
    case SPECIAL_PENANCE: {
        int missing = p->skills.base_level[SKILL_PRAYER] - p->current_prayer_points / RC_PRAYER_POINT_SCALE;
        if (missing < 0) missing = 0;
        c->max_hit = (int)((int64_t)c->max_hit * (200 + missing) / 200);
        return roll_hits(a, c, hits, 1);
    }
    case SPECIAL_ENERGY_DRAIN:
        boost(c, 5, 4, 1, 1); against(a, c, COMBAT_MELEE_SLASH);
        return roll_hits(a, c, hits, 1);
    case SPECIAL_BINDING_TENTACLE:
        return roll_hits(a, c, hits, 1);
    case SPECIAL_FAVOUR_OF_THE_WAR_GOD:
        against(a, c, COMBAT_MELEE_CRUSH);
        return roll_hits(a, c, hits, 1);
    case SPECIAL_BEAR_DOWN:
        boost(c, 2, 1, 1, 1); against(a, c, COMBAT_MELEE_SLASH);
        return roll_hits(a, c, hits, 1);
    case SPECIAL_BRUTAL_SWING: return kisten(a, c, hits);
    case SPECIAL_FLAMES_OF_RALOS:
        boost(c, 3, 2, 3, 2); against(a, c, COMBAT_MELEE_SLASH);
        return roll_hits(a, c, hits, 1);
    case SPECIAL_HAMMER_BLOW: {
        boost(c, 3, 2, 1, 1);
        roll_hits(a, c, hits, 1);
        hits[0].damage += 5;
        return 1;
    }
    case SPECIAL_QUICK_SMASH:
    case SPECIAL_FEROCITY:
        return roll_hits(a, c, hits, 1);
    case SPECIAL_WEAKEN:
        against(a, c, COMBAT_MELEE_STAB);
        return roll_hits(a, c, hits, 1);
    case SPECIAL_RETAINER:
        if (!(attrs & (RC_NPC_ATTR_VAMPYRE_1 | RC_NPC_ATTR_VAMPYRE_2))) {
            *failure = "Only a vampyre juvenile or juvinate can be trapped.";
            return -1;
        }
        if (npc->current_hp * 2 >= npc->spawn_hp) {
            *failure = "The vampyre must be below half health to trap.";
            return -1;
        }
        set_hit(&hits[0], c, 0, true);
        return 1;
    case SPECIAL_SHIELD_BASH: {
        boost(c, 6, 5, 1, 1); against(a, c, COMBAT_MELEE_CRUSH);
        // Only multi-combat doubles the hit on the main target.
        int count = roll_hits(a, c, hits, rc_combat_is_multi_combat(w) ? 2 : 1);
        if (count == 2) hits[1].flags |= GEAR_HIT_REPEAT;
        splash(a, c, p->x, p->y, 5, 9, 1);
        return count;
    }
    case SPECIAL_POWERSTAB: {
        int count = roll_hits(a, c, hits, 1);
        splash(a, c, p->x, p->y, 1, 13, 1);
        return count;
    }
    case SPECIAL_SWEEP: {
        c->max_hit = factor(c->max_hit, 11, 10);
        against(a, c, COMBAT_MELEE_SLASH);
        int count = roll_hits(a, c, hits, 1);
        if (a->size > 1) {
            RcCombatCalc second = *c;
            second.attack_roll = factor(c->attack_roll, 3, 4);
            refresh(&second);
            RcCombatRoll roll = rc_content_gear_roll(a, &second);
            set_hit(&hits[1], &second, roll.damage, roll.accurate);
            return 2;
        }
        splash(a, c, npc->x, npc->y, 1, 9, 1);
        return count;
    }
    case SPECIAL_UNLEASH: {
        int steps = p->special_energy / 500;
        c->attack_roll = factor(c->attack_roll, 100 + 5 * steps, 100);
        c->max_hit = factor(c->max_hit, 200 + 5 * steps, 200);
        against(a, c, COMBAT_MELEE_STAB);
        return roll_hits(a, c, hits, 1);
    }
    case SPECIAL_IMPALE:
        boost(c, 1, 1, 11, 10);
        return roll_hits(a, c, hits, 1);
    case SPECIAL_BACKSTAB:
    case SPECIAL_SNIPE: {
        // Never misses unless the player was the last to damage the target.
        if (npc->player_loot_credit) return roll_hits(a, c, hits, 1);
        refresh(c);
        c->hit_chance = 1.0f;
        RcCombatRoll roll = rc_content_gear_roll(a, c);
        set_hit(&hits[0], c, roll.damage, roll.accurate);
        return 1;
    }
    case SPECIAL_LIQUIFY:
        if (!(attrs & RC_NPC_ATTR_UNDERWATER)) {
            *failure = "This special can only be used underwater.";
            return -1;
        }
        boost(c, 2, 1, 1, 1);
        return roll_hits(a, c, hits, 1);
    case SPECIAL_CRYSTALLINE_SEVERANCE:
        return roll_hits(a, c, hits, 1);
    case SPECIAL_WRATH_OF_AMASCUT:
        if (!(attrs & RC_NPC_ATTR_TOA)) {
            *failure = "This special can only be used within the Tombs of Amascut.";
            return -1;
        }
        boost(c, 2, 1, 5, 4);
        return roll_hits(a, c, hits, 1);
    case SPECIAL_SEEKING_LUNGE: {
        refresh(c);
        int damage = factor(c->max_hit, 7, 10);
        if (npc->current_hp <= damage) {
            // The finisher's fixed roll: 70% of the attack roll, surely above defence.
            c->attack_roll = factor(c->attack_roll, 7, 10);
            c->hit_chance = c->attack_roll > c->defence_roll ? 1.0f
                : (float)c->attack_roll / (float)(c->defence_roll + 1);
        }
        RcCombatRoll roll = rc_content_gear_roll(a, c);
        set_hit(&hits[0], c, roll.accurate ? damage : 0, roll.accurate);
        return 1;
    }
    case SPECIAL_BEHEAD: {
        int stacks = rc_content_soul_stacks(w);
        c->attack_roll = factor(c->attack_roll, 100 + 12 * stacks, 100);
        int base = c->max_hit;
        c->max_hit = factor(base, 100 + 6 * stacks, 100);
        a->min_hit = factor(base, 6 * stacks, 100);
        p->content_gear[RC_GEAR_STATE_SOUL_STACKS] = 0;
        return roll_hits(a, c, hits, 1);
    }
    case SPECIAL_BLOOD_INFUSION: {
        // A quarter of the max hit raises both ends; the second hit no longer
        // depends on the first landing.
        a->min_hit = c->max_hit / 4;
        c->max_hit += a->min_hit;
        int cost = p->current_hp / 40;
        if (cost >= 1) p->current_hp -= cost * 10;
        refresh(c);
        return rc_content_gear_split_hits(a, c, hits, false);
    }
    case SPECIAL_BREAK_SHACKLES: {
        int bound = npc->immobilized_until > w->tick ? (int)(npc->immobilized_until - w->tick) : 0;
        c->attack_roll = factor(c->attack_roll, 1000 + 15 * bound, 1000);
        int damage = 15 * bound;
        if (damage > 1125) damage = 1125;
        c->max_hit = factor(c->max_hit, 1000 + damage, 1000);
        ((RcNpc *)npc)->immobilized_until = w->tick;
        return roll_hits(a, c, hits, 1);
    }
    case SPECIAL_ECLIPSE: {
        // Magic-based. The burn it consumes raises the max and half raises the min.
        int burned = npc->burn_remaining < 50 ? npc->burn_remaining : 50;
        c->attack_roll = rc_player_effective_magic_attack_level(p)
            * (p->equipment_bonuses[EQ_MAGIC_ATK] + 64);
        boost(c, 3, 2, 1, 1);
        against(a, c, COMBAT_MAGIC);
        c->max_hit += burned;
        a->min_hit = burned / 2;
        ((RcNpc *)npc)->burn_remaining = 0;
        return roll_hits(a, c, hits, 1);
    }
    case SPECIAL_BRAND:
        boost(c, 5, 4, 5, 4);
        return roll_hits(a, c, hits, 4);
    case SPECIAL_POWER_OF_THE_GODS:
        boost(c, 2, 1, 11, 10);
        c->max_hit = factor(c->max_hit, 5, 4);
        return roll_hits(a, c, hits, 1);
    case SPECIAL_LIGHTNING_STRIKE: {
        int count = roll_hits(a, c, hits, 2);
        if (hits[0].accurate || hits[1].accurate) {
            RcCombatRoll bolt = rc_content_gear_roll(a, c);
            set_hit(&hits[2], c, bolt.damage, true);
            count = 3;
        }
        return count;
    }
    case SPECIAL_SARADOMINS_LIGHTNING:
        if (row->special_cost == 100) {  // Saradomin sword
            c->max_hit = factor(c->max_hit, 11, 10);
            against(a, c, COMBAT_MELEE_SLASH);
            roll_hits(a, c, hits, 1);
            int magic = hits[0].accurate ? 1 + rc_content_roll(&w->rng_state, 16) : 0;
            set_hit(&hits[1], c, magic, hits[0].accurate);
            return 2;
        }
        c->max_hit = factor(c->max_hit, 5, 4);
        against(a, c, COMBAT_MAGIC);
        return roll_hits(a, c, hits, 1);
    case SPECIAL_DISRUPT: {
        int base = c->max_hit;
        return sure_hit(a, c, hits, base / 2, base + base / 2);
    }
    case SPECIAL_SPEAR_WALL: {
        int count = roll_hits(a, c, hits, 1);
        if (!bh) hits[0].damage /= 2;
        if (bh && hits[0].accurate) {
            set_hit(&hits[1], c, hits[0].damage * (50 + rc_content_roll(&w->rng_state, 26)) / 100, true);
            count = 2;
        }
        splash(a, c, p->x, p->y, 1, 15, 1);
        p->content_gear[RC_GEAR_STATE_IMMUNE_UNTIL] = (int)(w->tick + 8);
        return count;
    }
    case SPECIAL_SOL_SLAM: {
        int prayer = p->equipment_bonuses[EQ_PRAYER];
        c->max_hit = factor(c->max_hit, 100 + prayer, 100);
        int count = roll_hits(a, c, hits, 1);
        splash(a, c, p->x, p->y, 3, 15, 1);
        return count;
    }
    case SPECIAL_CELEBRATE:
        return roll_hits(a, c, hits, 1);
    case SPECIAL_ARMADYL_EYE:
    case SPECIAL_EVOKE:
        boost(c, 2, 1, 1, 1);
        return roll_hits(a, c, hits, 1);
    case SPECIAL_ANNIHILATE: {
        RcCombatCalc splash_calc = *c;
        splash_calc.max_hit = factor(c->max_hit, 4, 5);
        c->max_hit = factor(c->max_hit, 6, 5);
        int count = roll_hits(a, c, hits, 1);
        splash(a, &splash_calc, npc->x, npc->y, 1, 8, 2);
        return count;
    }
    case SPECIAL_CONCENTRATED_SHOT:
        boost(c, 5, 4, 5, 4);
        return roll_hits(a, c, hits, 1);
    case SPECIAL_DESCENT_OF_DARKNESS: {
        int ammo = p->equipment[EQUIP_AMMO].item_id;
        bool dragon = ammo == 11212 || (ammo >= 11227 && ammo <= 11229);
        int count = p->equipment[EQUIP_AMMO].quantity > 1 ? 2 : 1;
        int minimum = dragon ? 8 : bh ? 7 : 5;
        c->max_hit = factor(c->max_hit, dragon ? 15 : 13, 10);
        refresh(c);
        for (int i = 0; i < count; i++) {
            RcCombatRoll roll = rc_content_gear_roll(a, c);
            int damage = roll.damage < minimum ? minimum : roll.damage > 48 ? 48 : roll.damage;
            set_hit(&hits[i], c, damage, roll.accurate);
        }
        return count;
    }
    case SPECIAL_SNAPSHOT:
        c->attack_roll = factor(c->attack_roll, 10, 7);
        c->max_hit = ranged_sure_max(a);
        return roll_hits(a, c, hits, 2);
    case SPECIAL_POWERSHOT:
    case SPECIAL_SOULSHOT: {
        int max = ranged_sure_max(a);
        return sure_hit(a, c, hits, 1, max);
    }
    case SPECIAL_MOMENTUM_THROW:
        boost(c, 5, 4, 1, 1);
        return roll_hits(a, c, hits, 1);
    case SPECIAL_CHAINHIT: {
        // Visible level and the axe's own strength only, no prayers.
        const RcItemDef *axe = rc_item_def_get(a->weapon_id);
        c->max_hit = (int)(((int64_t)(p->skills.boosted_level[SKILL_RANGED] + 10)
                            * ((axe ? axe->ranged_strength : 0) + 64) + 320) / 640);
        int count = roll_hits(a, c, hits, 1);
        if (hits[0].accurate) chain_hits(a, c);
        return count;
    }
    case SPECIAL_DUALITY:
        return roll_hits(a, c, hits, 2);
    case SPECIAL_HAMSTRING: {
        if (bh) boost(c, 3, 2, 1, 1);
        refresh(c);
        RcCombatRoll roll = rc_content_gear_roll(a, c);
        int base = c->max_hit;
        c->max_hit = factor(base, bh ? 150 : 120, 100);
        set_hit(&hits[0], c, !roll.accurate ? 0 : a->npc->force_player_max_hit ? c->max_hit
            : roll_between(a, factor(base, bh ? 50 : 20, 100), c->max_hit), roll.accurate);
        return 1;
    }
    case SPECIAL_PHANTOM_STRIKE:
        if (bh) boost(c, 3, 2, 1, 1);
        return roll_hits(a, c, hits, 1);
    case SPECIAL_TOXIC_SIPHON:
        boost(c, 2, 1, 3, 2);
        return roll_hits(a, c, hits, 1);
    case SPECIAL_RAPID_BURST:
        boost(c, 4, 5, 11, 10);
        return roll_hits(a, c, hits, 2);
    case SPECIAL_SWARM: {
        c->attack_roll *= 2;
        c->max_hit = c->max_hit - factor(c->max_hit, 6, 10);
        return roll_hits(a, c, hits, 4);
    }
    case SPECIAL_SCORCHING_SHACKLES:
        return roll_hits(a, c, hits, 1);
    case SPECIAL_DIVISION:
        return 0;  // rc_content_ralos_hits drains on the charged throw
    case SPECIAL_PULSATE:
        return sure_hit(a, c, hits, 75, 150);
    case SPECIAL_CONDEMN:
        // The (a) form fires its built-in spell whatever the autocast.
        if (a->spell || a->style != COMBAT_MAGIC) {
            a->spell = NULL;
            int base = p->skills.boosted_level[SKILL_MAGIC] / 3 - 6;
            against(a, c, COMBAT_MAGIC);
            rc_content_gear_magic(a, c, base > 1 ? base : 1);
        }
        boost(c, 3, 2, 3, 2);
        return roll_hits(a, c, hits, 1);
    case SPECIAL_INVOCATE:
    case SPECIAL_IMMOLATE: {
        int level = p->skills.boosted_level[SKILL_MAGIC];
        int top = row->special == SPECIAL_IMMOLATE ? 58 : 44;
        int base = (99 + top * level) / 99;
        if (base > top) base = top;
        against(a, c, COMBAT_MAGIC);
        rc_content_gear_magic(a, c, base);
        if (row->special == SPECIAL_IMMOLATE) c->attack_roll = factor(c->attack_roll, 3, 2);
        return roll_hits(a, c, hits, 1);
    }
    case SPECIAL_SOUL_REND:
        boost(c, 2, 1, 1, 1);
        return roll_hits(a, c, hits, 1);
    case SPECIAL_ZORYAS_FLOW:
        boost(c, 25, 10, 16, 10);
        return roll_hits(a, c, hits, 1);
    case SPECIAL_EXPLOSIVE_SHATTER: {
        // Item text: 130% of the max hit as magic damage and up to 65% as
        // typeless damage, on the target and 11 more within 3 tiles.
        p->content_gear[RC_GEAR_STATE_SHATTER_STACKS] = rc_content_shatter_stacks(w) - 5;
        p->content_gear[RC_GEAR_STATE_SHATTER_TICK] = (int)w->tick;
        int typeless = c->max_hit * 65 / 100;
        c->max_hit = c->max_hit * 13 / 10;
        roll_hits(a, c, hits, 1);
        splash(a, c, npc->x, npc->y, 3, 11, 1);
        hits[1] = (RcPendingHit){.damage = (int)rc_content_roll(&w->rng_state, typeless + 1),
            .max_hit = typeless, .accurate = true};
        return 2;
    }
    case SPECIAL_SCATTER_ASHES: {
        if (p->current_spellbook != RC_SPELL_BOOK_ARCEUUS) {
            *failure = "You must be on the Arceuus spellbook to use this.";
            return -1;
        }
        return roll_hits(a, c, hits, 1);
    }
    default:
        return roll_hits(a, c, hits, 1);
    }
}

void rc_content_special_landed(RcWorld *world, RcNpc *npc, const RcPendingHit *hit, int damage) {
    const RcGearRow *row = rc_content_gear_row(hit->weapon_id);
    RcPlayer *p = &world->player;
    uint32_t attrs = rc_content_npc_attributes(world, npc);
    bool bh = row->special_variant != 0;
    bool demon = (attrs & RC_NPC_ATTR_DEMON) != 0;
    switch (row->special) {
    case SPECIAL_SMASH:
        if (damage > 0) drain_percent(world, npc, 1, row->special_cost == 35 && bh ? 75 : 30);
        break;
    case SPECIAL_PULVERIZE:
        if (damage > 0) drain_percent(world, npc, 1, 35);
        break;
    case SPECIAL_WARSTRIKE: {
        static const int order[] = {1, 2, 0, 5, 4};
        drain_in_order(world, npc, order, 5, damage);
        break;
    }
    case SPECIAL_HEALING_BLADE:
        if (hit->accurate && damage > 0) {
            int hp = (damage + 1) / 2, prayer = (damage + 3) / 4;
            heal(world, hp < 10 ? 10 : hp, 100);
            rc_prayer_restore_points(p, prayer < 5 ? 5 : prayer);
        }
        break;
    case SPECIAL_ICE_CLEAVE:
        if (damage > 0) rc_content_gear_bind(world, npc, 32);
        break;
    case SPECIAL_BLOOD_SACRIFICE:
        if (hit->accurate && !(hit->flags & RC_HIT_FLAG_FOLLOW_UP)) {
            rc_queue_hit_meta(npc->pending_hits, &npc->num_pending_hits, 25, 8, COMBAT_NONE,
                              RC_HIT_SOURCE_PLAYER, 0, world->tick,
                              RC_HIT_FLAG_SPECIAL | RC_HIT_FLAG_FOLLOW_UP, 25);
            if (npc->num_pending_hits > 0) {
                RcPendingHit *mark = &npc->pending_hits[npc->num_pending_hits - 1];
                mark->weapon_id = hit->weapon_id;
                mark->accurate = true;
            }
        } else if (hit->flags & RC_HIT_FLAG_FOLLOW_UP) {
            // Heals what the sacrifice dealt, up to 15% of the target's max Hitpoints.
            int cap = npc->spawn_hp * 15 / 100;
            if (cap > 25) cap = 25;
            heal(world, damage < cap ? damage : cap, 100);
        }
        break;
    case SPECIAL_SUNDER:
        if (damage > 0) {
            static const int order[] = {1, 0, 4, 5};
            drain_in_order(world, npc, order, 4, damage / 10);
        }
        break;
    case SPECIAL_SHOVE:
        rc_content_gear_bind(world, npc, 5);
        break;
    case SPECIAL_BINDING_TENTACLE:
        rc_content_gear_bind(world, npc, 8);
        if (rc_content_roll(&world->rng_state, 2) == 0) rc_npc_apply_poison(world, npc, 20);
        break;
    case SPECIAL_FAVOUR_OF_THE_WAR_GOD:
        // Can pass the player's Prayer level, by up to the damage dealt.
        if (damage > 0) {
            int cap = (p->skills.base_level[SKILL_PRAYER] + damage) * RC_PRAYER_POINT_SCALE;
            p->current_prayer_points += damage * RC_PRAYER_POINT_SCALE;
            if (p->current_prayer_points > cap) p->current_prayer_points = cap;
        }
        break;
    case SPECIAL_BEAR_DOWN:
        if (damage > 0) burn(world, npc, 20);
        break;
    case SPECIAL_FLAMES_OF_RALOS:
        if (damage > 0) burn(world, npc, 10);
        break;
    case SPECIAL_BURNING_BARRAGE:
        if (hit->flags & GEAR_HIT_BURN) burn(world, npc, 10);
        break;
    case SPECIAL_WEAKEN: {
        if (!hit->accurate) break;
        // A share of the base level plus one, stacking additively.
        int percent = row->family == GEAR_EMBERLIGHT && demon ? 15 : demon ? 10 : 5;
        static const int stats[] = {0, 2, 1};
        for (int i = 0; i < 3; i++)
            drain(world, npc, stats[i], base_stat(world, npc, stats[i]) * percent / 100 + 1);
        break;
    }
    case SPECIAL_RETAINER:
        rc_content_gear_bind(world, npc, 50);
        break;
    case SPECIAL_BACKSTAB:
    case SPECIAL_SNIPE:
        if (damage > 0 && npc->stats[1] >= base_stat(world, npc, 1)) drain(world, npc, 1, damage);
        break;
    case SPECIAL_LIQUIFY:
        if (damage > 0) {
            int up = damage / 4;
            p->skills.boosted_level[SKILL_STRENGTH] += up;
            p->skills.boosted_level[SKILL_ATTACK] += up;
            p->skills.boosted_level[SKILL_DEFENCE] += up;
        }
        break;
    case SPECIAL_TOXIC_SIPHON:
        if (damage > 0) heal(world, damage / 2, 100);
        break;
    case SPECIAL_SWARM:
        if (hit->accurate && rc_content_roll(&world->rng_state, 4) == 0) rc_npc_apply_poison(world, npc, 20);
        break;
    case SPECIAL_SCORCHING_SHACKLES:
        if (hit->accurate && demon) { rc_content_gear_bind(world, npc, 20); burn(world, npc, 5); }
        else if (hit->accurate) burn(world, npc, 1);
        break;
    case SPECIAL_CONDEMN:
        if (hit->accurate) {
            for (int stat = 1; stat <= 5; stat += 4) {
                int floor = npc->base_stats[stat] * 85 / 100;
                if (npc->stats[stat] > floor) npc->stats[stat] = floor;
            }
        }
        break;
    case SPECIAL_INVOCATE:
        if (damage > 0) {
            int up = damage / 2;
            p->current_prayer_points += up * RC_PRAYER_POINT_SCALE;
            if (p->current_prayer_points > 120 * RC_PRAYER_POINT_SCALE)
                p->current_prayer_points = 120 * RC_PRAYER_POINT_SCALE;
        }
        break;
    case SPECIAL_SOUL_REND:
        if (damage > 0) npc->magic_defence_drain = (int16_t)(npc->magic_defence_drain + damage);
        break;
    case SPECIAL_SOULSHOT:
        if (damage > 0 && npc->stats[5] >= base_stat(world, npc, 5)) drain(world, npc, 5, damage);
        break;
    case SPECIAL_SHIELD_BASH: {
        if (!hit->accurate || (hit->flags & GEAR_HIT_REPEAT)) break;
        // Highest offensive stat: Attack and Strength first when they tie.
        int best = npc->stats[0];
        if (npc->stats[2] > best) best = npc->stats[2];
        if (npc->stats[4] > best) best = npc->stats[4];
        if (npc->stats[5] > best) best = npc->stats[5];
        bool melee = npc->stats[0] == best || npc->stats[2] == best;
        for (int i = 0; i < 2 && melee; i++)
            if (npc->stats[i * 2] == best) drain_percent(world, npc, i * 2, 5);
        if (!melee) drain_percent(world, npc, npc->stats[4] == best ? 4 : 5, 5);
        break;
    }
    case SPECIAL_WRATH_OF_AMASCUT:
        if (hit->accurate) {
            p->content_gear[RC_GEAR_STATE_WRATH_TARGET] = npc->uid + 1;
            p->content_gear[RC_GEAR_STATE_WRATH_UNTIL] = (int)(world->tick + 10);
        }
        break;
    case SPECIAL_HAMSTRING:
        if (hit->accurate) p->content_gear[RC_GEAR_STATE_ATTACK_SPEED_CUT] = 1;
        break;
    case SPECIAL_ZORYAS_FLOW:
        if (hit->accurate) {
            int left = p->content_gear[RC_GEAR_STATE_FLOW_ATTACKS];
            p->content_gear[RC_GEAR_STATE_FLOW_ATTACKS] = left > 0 ? 5 : 3;
            p->content_gear[RC_GEAR_STATE_FLOW_UNTIL] = (int)(world->tick + 50);
        }
        break;
    case SPECIAL_POWER_OF_THE_GODS:
        if (hit->accurate && damage > 0) {
            heal(world, damage / 2, 100);
            rc_prayer_restore_points(p, damage / 4);
            drain(world, npc, 1, damage);
            rc_content_gear_bind(world, npc, 33);
        }
        break;
    default:
        break;
    }
}

static bool skill_boost(RcPlayer *p, int skill, int amount) {
    int cap = p->skills.base_level[skill] + amount;
    if (p->skills.boosted_level[skill] < cap) p->skills.boosted_level[skill] = cap;
    return true;
}

int rc_content_special_activate(RcWorld *world) {
    RcPlayer *p = &world->player;
    const RcGearRow *row = rc_content_gear_row(p->equipment[EQUIP_WEAPON].item_id);
    if (row->special == SPECIAL_NONE) return 0;
    int cost = row->special_cost * 100;
    switch (row->special) {
    case SPECIAL_QUICK_SMASH:
    case SPECIAL_MOMENTUM_THROW:
    case SPECIAL_FEROCITY:
        if (p->attack_target >= 0 && p->special_energy >= cost) p->attack_timer = 0;
        return 0;
    case SPECIAL_RAMPAGE:
    case SPECIAL_SANCTUARY:
    case SPECIAL_LUMBER_UP:
    case SPECIAL_ROCK_KNOCKER:
    case SPECIAL_FISHSTABBER:
    case SPECIAL_POWER_OF_DEATH:
    case SPECIAL_TUMEKENS_LIGHT:
    case SPECIAL_ENTICE:
    case SPECIAL_VIRULENCE:
        break;
    default:
        return 0;
    }
    if (p->special_energy < cost) {
        say(world, "You do not have enough special attack energy.");
        return 1;
    }
    int *b = p->skills.boosted_level;
    switch (row->special) {
    case SPECIAL_RAMPAGE: {
        int drained = 0;
        static const int stats[] = {SKILL_ATTACK, SKILL_DEFENCE, SKILL_RANGED, SKILL_MAGIC};
        for (int i = 0; i < 4; i++) {
            int amount = b[stats[i]] / 10;
            b[stats[i]] -= amount;
            drained += amount;
        }
        b[SKILL_STRENGTH] += 10 + drained / 4;
        int cap = p->skills.base_level[SKILL_STRENGTH] * 112 / 100 + 10;
        if (b[SKILL_STRENGTH] > cap) b[SKILL_STRENGTH] = cap;
        break;
    }
    case SPECIAL_SANCTUARY: skill_boost(p, SKILL_DEFENCE, 8); break;
    case SPECIAL_LUMBER_UP: skill_boost(p, SKILL_WOODCUTTING, 3); break;
    case SPECIAL_ROCK_KNOCKER: skill_boost(p, SKILL_MINING, 3); break;
    case SPECIAL_FISHSTABBER: skill_boost(p, SKILL_FISHING, 3); break;
    case SPECIAL_POWER_OF_DEATH:
        p->content_gear[RC_GEAR_STATE_MELEE_WARD_UNTIL] = (int)(world->tick + 100);
        say(world, "Spirits of deceased evildoers offer you their protection.");
        break;
    case SPECIAL_TUMEKENS_LIGHT:
        say(world, "This special attack can only be used within the Tombs of Amascut.");
        return 1;
    case SPECIAL_ENTICE:
        say(world, "There is nobody nearby to entice.");
        return 1;
    case SPECIAL_VIRULENCE:
        if (p->toxin_severity <= 0) {
            say(world, "You can only use this special while poisoned or envenomed.");
            return 1;
        }
        p->content_gear[RC_GEAR_STATE_NEXT_DAMAGE_BONUS] = p->toxin_venom ? p->toxin_severity : (p->toxin_severity + 4) / 5;
        p->toxin_severity = 0;
        p->toxin_venom = false;
        break;
    default:
        break;
    }
    p->special_energy -= cost;
    rc_combat_record_player_action(world, p->equipment[EQUIP_WEAPON].item_id);
    return 1;
}

int rc_content_special_attack_speed(RcWorld *world, int weapon_id, int speed) {
    const RcGearRow *row = rc_content_gear_row(weapon_id);
    bool bh = row->special_variant != 0;
    switch (row->special) {
    case SPECIAL_SHOVE:
    case SPECIAL_IMPALE:
    case SPECIAL_CHAINHIT: return speed + 1;
    case SPECIAL_WRATH_OF_AMASCUT: return 8;
    case SPECIAL_SOUL_REND: return 5;
    case SPECIAL_CONCENTRATED_SHOT: return speed + 4;
    case SPECIAL_CLEAVE: return bh ? speed - 1 : speed;
    case SPECIAL_SUNDER: return bh ? speed - 2 : speed;
    case SPECIAL_SPEAR_WALL: return bh ? speed - 1 : speed;
    case SPECIAL_ZORYAS_FLOW: return speed - 1;
    default: return speed;
    }
}

// The Rondache's instant attack leaves the attack cycle and special energy alone.
int rc_content_shield_slam(RcWorld *world) {
    RcPlayer *p = &world->player;
    int charges = rc_content_rondache_charges(world);
    if (charges < 0) return 0;
    if (charges < 10) {
        say(world, "The rondache needs at least 10 charges.");
        return 0;
    }
    RcNpc *npc = p->attack_target >= 0 ? rc_npc_resolve(world, p->attack_target) : NULL;
    const RcNpcDef *def = npc ? rc_npc_def_for_npc(world, npc) : NULL;
    if (!def || npc->is_dead
            || rc_content_edge_distance(p, npc->x, npc->y, def->size) > 1) {
        say(world, "You need to be next to your target to use Shield Slam.");
        return 0;
    }
    RcCombatCalc calc = rc_calc_melee(p, npc);
    calc.attack_roll = rc_player_effective_attack_level(p)
        * (p->equipment_bonuses[EQ_CRUSH_ATK] + 6 * charges + 64);
    calc.defence_roll = rc_npc_defensive_roll(npc, COMBAT_MELEE_CRUSH);
    refresh(&calc);
    calc.max_hit = charges + charges * charges / 100;
    RcCombatRoll roll = rc_roll_attack(&calc, &world->rng_state, true);
    int damage = roll.accurate ? calc.max_hit : 0;
    damage = rc_combat_apply_regular_npc_player_damage_rules(world, npc, damage);
    damage = rc_encounter_scale_player_damage(world, npc->uid, COMBAT_MELEE_CRUSH, damage);
    if (!rc_queue_hit_meta(npc->pending_hits, &npc->num_pending_hits, damage, 1,
                           COMBAT_MELEE_CRUSH, RC_HIT_SOURCE_PLAYER, 0, world->tick, 0,
                           calc.max_hit))
        return 0;
    RcPendingHit *hit = &npc->pending_hits[npc->num_pending_hits - 1];
    hit->accurate = roll.accurate;
    hit->weapon_id = (uint16_t)p->equipment[EQUIP_SHIELD].item_id;
    // Spent charges start a cooldown of about 8 seconds on gaining more.
    p->content_gear[RC_GEAR_STATE_RONDACHE_CHARGES] = 0;
    p->content_gear[RC_GEAR_STATE_RONDACHE_NEXT_GAIN] = (int)(world->tick + 13);
    rc_combat_record_player_action(world, p->equipment[EQUIP_SHIELD].item_id);
    return 1;
}

// Specials with their own arrival, zero to keep the weapon's. Values include
// the tick NPCs add by processing before players.
int rc_content_special_hit_delay(int weapon_id, int distance, int hit_index) {
    switch (rc_content_gear_row(weapon_id)->special) {
    case SPECIAL_ARMADYL_EYE: case SPECIAL_EVOKE:
    case SPECIAL_IMMOLATE: case SPECIAL_INVOCATE:
        return 3;
    case SPECIAL_SWARM:
        return hit_index ? 3 : 2;
    case SPECIAL_SLICE_AND_DICE:
        return hit_index >= 2 ? 2 : 1;
    case SPECIAL_EXPLOSIVE_SHATTER:
        return 1;
    case SPECIAL_TOXIC_SIPHON:
        return distance == 4 || distance == 5 ? 3 : 0;
    default:
        return 0;
    }
}
