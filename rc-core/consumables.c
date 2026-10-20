#include "consumables.h"
#include "api.h"
#include "combat.h"
#include "interaction.h"
#include "items.h"
#include "player_command.h"
#include "prayer.h"
#include "storage.h"
#include "traversal.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

static int minimum(int a, int b) { return a < b ? a : b; }
static int maximum(int a, int b) { return a > b ? a : b; }

int rc_consume_accepted(RcConsumeResult result) {
    return result == RC_CONSUME_OK || result == RC_CONSUME_QUEUED;
}

const char *rc_consume_result_message(int result) {
    switch (result) {
    case RC_CONSUME_OK: return "Consumed.";
    case RC_CONSUME_QUEUED: return "";
    case RC_CONSUME_DISABLED: return "Consumables are disabled in this world.";
    case RC_CONSUME_UNSUPPORTED: return "This item's consumption rules are not implemented; no item was used.";
    case RC_CONSUME_BUSY: return "You cannot eat or drink during this action.";
    case RC_CONSUME_STALE: return "That inventory item changed before it could be consumed.";
    case RC_CONSUME_COOLDOWN: return "You must wait before consuming that again.";
    case RC_CONSUME_LOW_HP: return "You need more than 10 Hitpoints to drink a divine potion.";
    case RC_CONSUME_ITEM_FAILURE: return "The consumption item transaction failed; nothing was consumed.";
    case RC_CONSUME_BAD_DATA: return "The consumable or player state is invalid; nothing was consumed.";
    case RC_CONSUME_MEMBERS: return "You need a members world to consume that item.";
    default: return "That item cannot be consumed with this action.";
    }
}

static RcConsumeResult result(RcWorld *w, int id, RcConsumeResult code) {
    if (w) {
        w->player.consume_result = code;
        w->player.consume_item_id = id;
        if (code != RC_CONSUME_QUEUED) w->player.consume_outcome_sequence++;
    }
    return code;
}

int rc_consumable_action(int id, int option) {
    const RcItemDef *item = rc_item_def_get(id);
    if (!item || item->noted || item->placeholder || option < 0
            || option >= RC_ITEM_ACTION_COUNT) return 0;
    const char *action = item->inventory_actions[option];
    return !strcmp(action, "Eat") ? 1 : !strcmp(action, "Drink") ? 2 : 0;
}

const RcConsumableDef *rc_consumable_get(const RcWorld *w, int id) {
    for (int i = 0; w && i < w->consumable_count; i++)
        if (w->consumables[i].item_id == id) return &w->consumables[i];
    return NULL;
}

int rc_consumables_register(RcWorld *w, const RcConsumableDef *defs, int count) {
    if (!w || !defs || count <= 0) return 0;
    for (int i = 0; i < count; i++) {
        const RcConsumableDef *d = &defs[i];
        const RcItemDef *item = rc_item_def_get(d->item_id);
        const RcItemDef *replacement = rc_item_def_get(d->replacement_id);
        int valid = item && item->loaded && !item->noted && !item->placeholder
            && d->kind <= RC_CONSUME_DRINK && d->eat_delay > 0
            && d->eat_delay <= 3 && d->attack_delay <= 3 && d->cure <= 2
            && d->energy <= 100 && d->heal_percent <= 100
            && d->prayer_percent <= 100 && d->delayed_energy <= 100
            && d->angler <= 1 && d->overheal <= 1
            && d->delayed_cure <= 1 && d->replacement_id >= -1
            && d->replacement_id != d->item_id
            && (d->delayed_heal || (!d->delayed_energy && !d->delayed_cure))
            && (d->kind != RC_CONSUME_DRINK || !d->attack_delay)
            && !(d->antifire_ticks && d->super_antifire_ticks);
        bool action = false;
        for (int op = 0; op < RC_ITEM_ACTION_COUNT; op++)
            action |= rc_consumable_action(d->item_id, op)
                == (d->kind == RC_CONSUME_DRINK ? 2 : 1);
        valid &= action;
        if (d->replacement_id >= 0)
            valid &= replacement && replacement->loaded && !replacement->noted
                && !replacement->placeholder && !item->stackable;
        for (int s = 0; s < SKILL_COUNT; s++)
            valid &= d->stats[s].op <= RC_STAT_DRAIN && d->stats[s].percent <= 100
                && ((s != SKILL_HITPOINTS && s != SKILL_PRAYER) || !d->stats[s].op);
        for (int j = 0; j < i; j++) valid &= defs[j].item_id != d->item_id;
        if (!valid) {
            fprintf(stderr, "consumables: invalid/duplicate definition for item %d\n", d->item_id);
            return 0;
        }
    }
    w->consumables = defs;
    w->consumable_count = count;
    return 1;
}

static void heal(RcPlayer *p, int amount, int extra) {
    int64_t cap = (int64_t)p->max_hp + (int64_t)extra * 10;
    int64_t hp = (int64_t)p->current_hp + (int64_t)amount * 10;
    if (hp > cap) hp = cap;
    if (hp > INT_MAX) hp = INT_MAX;
    if (hp > p->current_hp) p->current_hp = (int)hp;
    p->skills.boosted_level[SKILL_HITPOINTS] = (int)(((int64_t)p->current_hp + 9) / 10);
}

static void cure(RcPlayer *p, int kind) {
    if (!kind) return;
    if (p->toxin_venom && kind != 2) {
        p->toxin_severity *= 5;
        p->toxin_venom = false;
    } else {
        p->toxin_severity = 0;
        p->toxin_venom = false;
    }
}

RcConsumeResult rc_player_consume_expected(RcWorld *w, int slot,
                                          uint32_t generation, int drink) {
    if (!w) return RC_CONSUME_INVALID;
    RcPlayer *p = &w->player;
    if ((w->enabled & (RC_SUB_INVENTORY | RC_SUB_CONSUMABLES))
            != (RC_SUB_INVENTORY | RC_SUB_CONSUMABLES))
        return result(w, -1, RC_CONSUME_DISABLED);
    if (slot < 0 || slot >= RC_INVENTORY_SIZE)
        return result(w, -1, RC_CONSUME_INVALID);
    const RcInvSlot *item = &p->inventory[slot];
    int id = item->item_id;
    if (id < 0 || item->quantity <= 0 || item->generation != generation)
        return result(w, id, RC_CONSUME_STALE);
    const RcConsumableDef *d = rc_consumable_get(w, id);
    if (!d) return result(w, id, RC_CONSUME_UNSUPPORTED);
    if (rc_item_def_get(id)->members && !w->members_world)
        return result(w, id, RC_CONSUME_MEMBERS);
    if ((d->kind == RC_CONSUME_DRINK) != (drink != 0) || item->state_id)
        return result(w, id, RC_CONSUME_INVALID);
    if (p->is_dead || p->current_hp <= 0 || p->storage_kind != RC_STORAGE_NONE
            || (w->player_action.active
                && w->player_action.category == RC_ACTION_CATEGORY_STRONG
                && w->player_action.ready_tick > w->tick)
            || (p->traversal.active && p->traversal.phase != RC_TRAVERSAL_PHASE_APPROACH))
        return result(w, id, RC_CONSUME_BUSY);
    if (rc_player_command_should_queue(w)) {
        int args[8] = {slot, (int)generation, drink};
        if (!rc_player_command_submit(w, RC_PLAYER_COMMAND_CONSUME,
                                      RC_ACTION_CATEGORY_NORMAL, args, 0))
            return result(w, id, RC_CONSUME_BUSY);
        return result(w, id, RC_CONSUME_QUEUED);
    }
    RcTick ready = d->kind == RC_CONSUME_FOOD ? p->food_ready_tick
        : d->kind == RC_CONSUME_COMBO ? p->combo_ready_tick : p->potion_ready_tick;
    if (w->tick < ready) return result(w, id, RC_CONSUME_COOLDOWN);
    if (d->divine_ticks && p->current_hp <= 100)
        return result(w, id, RC_CONSUME_LOW_HP);
    for (int s = 0; s < SKILL_COUNT; s++) {
        if (p->skills.base_level[s] < 0 || p->skills.base_level[s] > 255
                || (d->stats[s].op && (p->skills.boosted_level[s] < 0
                    || p->skills.boosted_level[s] > 255)))
            return result(w, id, RC_CONSUME_BAD_DATA);
    }
    RcItemTransaction tx;
    if (rc_item_tx_begin(&tx, w).code != RC_ITEM_RESULT_OK
            || rc_item_tx_remove_slot(&tx, slot, 1, generation).code != RC_ITEM_RESULT_OK)
        return result(w, id, RC_CONSUME_ITEM_FAILURE);
    if (d->replacement_id >= 0)
        tx.inventory[slot] = (RcInvSlot){.item_id = d->replacement_id, .quantity = 1};
    if (rc_item_tx_commit(&tx).code != RC_ITEM_RESULT_OK)
        return result(w, id, RC_CONSUME_ITEM_FAILURE);

    // Nothing after the item commit can fail. All resource effects are bounded.
    int base_hp = p->skills.base_level[SKILL_HITPOINTS];
    int hp = d->heal + base_hp * d->heal_percent / 100;
    if (d->angler)
        hp = base_hp / 10 + (base_hp >= 93 ? 13 : base_hp >= 75 ? 8
            : base_hp >= 50 ? 6 : base_hp >= 25 ? 4 : 2);
    heal(p, hp, d->overheal ? hp : 0);
    if (d->divine_ticks) {
        RcPendingHit hit = {.source_idx = RC_HIT_SOURCE_PLAYER,
            .attack_style = COMBAT_NONE, .flags = RC_HIT_SUPPRESS_ENCOUNTER_EFFECTS};
        rc_combat_apply_player_hit(w, &hit, 10);
    }
    for (int s = 0; s < SKILL_COUNT; s++) {
        RcConsumeStat effect = d->stats[s];
        int base = p->skills.base_level[s], old = p->skills.boosted_level[s];
        int amount = effect.flat + (effect.op == RC_STAT_DRAIN ? old : base) * effect.percent / 100;
        if (effect.op == RC_STAT_BOOST) {
            p->skills.boosted_level[s] = maximum(old, minimum(old + amount, base + amount));
            if (d->divine_ticks) {
                p->divine_mask |= 1u << s;
                p->divine_until[s] = w->tick + d->divine_ticks;
                p->divine_boost[s] = base + amount;
            }
        } else if (effect.op == RC_STAT_RESTORE && old < base)
            p->skills.boosted_level[s] = minimum(base, old + amount);
        else if (effect.op == RC_STAT_DRAIN)
            p->skills.boosted_level[s] = maximum(1, old - amount);
    }
    if (d->prayer_flat || d->prayer_percent) {
        int bonus = w->consumable_prayer_bonus ? w->consumable_prayer_bonus(p) : 0;
        rc_prayer_restore_points(p, d->prayer_flat
            + p->skills.base_level[SKILL_PRAYER] * (d->prayer_percent + bonus) / 100);
    }
    p->run_energy = minimum(10000, p->run_energy + d->energy * 100);
    if (d->stamina_ticks) p->stamina_until = w->tick + d->stamina_ticks;
    if (d->antifire_ticks) p->antifire_until = w->tick + d->antifire_ticks;
    if (d->super_antifire_ticks)
        p->super_antifire_until = w->tick + d->super_antifire_ticks;
    bool downgraded = p->toxin_venom && d->cure != 2;
    cure(p, d->cure);
    if (d->poison_immunity && !downgraded)
        p->poison_immune_until = w->tick + d->poison_immunity;
    if (d->venom_immunity && !downgraded)
        p->venom_immune_until = w->tick + d->venom_immunity;
    if (d->delayed_heal) {
        p->delayed_food_tick = w->tick + 7;
        p->delayed_food_heal = d->delayed_heal;
        p->delayed_food_energy = d->delayed_energy;
        p->delayed_food_cure = d->delayed_cure;
    }
    p->food_ready_tick = w->tick + d->eat_delay;
    if (d->kind != RC_CONSUME_FOOD) p->potion_ready_tick = w->tick + d->eat_delay;
    if (d->kind == RC_CONSUME_COMBO) p->combo_ready_tick = w->tick + d->eat_delay;
    // Combat decrements its timer later in this tick. A value of 1 is already ready.
    if (p->attack_timer > 1 && d->attack_delay)
        p->attack_timer = p->attack_timer > INT_MAX - d->attack_delay
            ? INT_MAX : p->attack_timer + d->attack_delay;
    p->skill_action = 0;
    p->skill_ready_tick = 0;
    rc_interaction_cancel(p, RC_INTERACTION_FAIL_CANCELLED);
    rc_traversal_cancel(w, RC_ACTION_CANCEL_REPLACED);
    p->consume_kind = d->kind;
    p->consume_sequence++;
    return result(w, id, RC_CONSUME_OK);
}

int rc_player_eat(RcWorld *w, int slot) {
    uint32_t gen = w && slot >= 0 && slot < RC_INVENTORY_SIZE
        ? w->player.inventory[slot].generation : 0;
    return rc_consume_accepted(rc_player_consume_expected(w, slot, gen, 0));
}

int rc_player_drink(RcWorld *w, int slot) {
    uint32_t gen = w && slot >= 0 && slot < RC_INVENTORY_SIZE
        ? w->player.inventory[slot].generation : 0;
    return rc_consume_accepted(rc_player_consume_expected(w, slot, gen, 1));
}

void rc_consumables_tick(RcWorld *w) {
    RcPlayer *p = &w->player;
    if (p->is_dead || p->current_hp <= 0) return;
    if (p->delayed_food_heal && w->tick >= p->delayed_food_tick) {
        heal(p, p->delayed_food_heal, 0);
        p->run_energy = minimum(10000, p->run_energy + p->delayed_food_energy * 100);
        if (p->delayed_food_cure) cure(p, 1);
        p->delayed_food_heal = p->delayed_food_energy = 0;
        p->delayed_food_cure = false;
    }
    if (p->divine_mask) {
        for (int s = 0; s < SKILL_COUNT; s++) {
            if (!(p->divine_mask & (1u << s)) || w->tick < p->divine_until[s]) continue;
            p->divine_mask &= ~(1u << s);
            if (p->skills.boosted_level[s] > p->skills.base_level[s])
                p->skills.boosted_level[s] = p->skills.base_level[s];
        }
    }
}

void rc_consumables_reset(RcPlayer *p) {
    p->food_ready_tick = p->potion_ready_tick = p->combo_ready_tick = 0;
    p->stamina_until = p->delayed_food_tick = 0;
    p->antifire_until = p->super_antifire_until = 0;
    p->delayed_food_heal = p->delayed_food_energy = 0;
    p->delayed_food_cure = false;
    p->divine_mask = 0;
    memset(p->divine_until, 0, sizeof(p->divine_until));
    memset(p->divine_boost, 0, sizeof(p->divine_boost));
    p->toxin_severity = 0;
    p->toxin_venom = false;
    p->toxin_tick = p->poison_immune_until = p->venom_immune_until = 0;
}

void rc_player_apply_toxin(RcWorld *w, int damage, bool venom) {
    if (!w || damage <= 0 || damage > 1000 || w->player.is_dead) return;
    RcPlayer *p = &w->player;
    if (w->tick < (venom ? p->venom_immune_until : p->poison_immune_until)) return;
    if (!venom && p->toxin_venom) return;
    int severity = venom ? minimum(20, maximum(6, damage)) : damage * 5;
    bool fresh = !p->toxin_severity || (venom && !p->toxin_venom);
    if (fresh || severity > p->toxin_severity) p->toxin_severity = severity;
    p->toxin_venom = venom;
    if (fresh) p->toxin_tick = w->tick + 30;
}

void rc_player_toxin_tick(RcWorld *w) {
    RcPlayer *p = &w->player;
    if (!p->toxin_severity || p->is_dead || p->current_hp <= 0
            || w->tick < p->toxin_tick || p->storage_kind != RC_STORAGE_NONE) return;
    int damage = p->toxin_venom ? p->toxin_severity : (p->toxin_severity + 4) / 5;
    RcPendingHit hit = {.source_idx = RC_HIT_SOURCE_STATUS,
        .attack_style = COMBAT_NONE, .max_hit = damage,
        .flags = RC_HIT_SUPPRESS_ENCOUNTER_EFFECTS};
    rc_combat_apply_player_hit(w, &hit, damage);
    p->toxin_severity = p->toxin_venom ? minimum(20, damage + 2) : p->toxin_severity - 1;
    p->toxin_tick = w->tick + 30;
}
