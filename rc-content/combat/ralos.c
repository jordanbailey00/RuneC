#include "ralos.h"

#include "interaction.h"
#include "items.h"
#include <stdio.h>
#include <stdlib.h>

enum { RALOS_EMPTY = 28919, RALOS_CHARGED = 28922, SUNFIRE = 28924,
       MAX_CHARGES = 20000 };

int rc_content_ralos_hits(RcWorld *world, const RcNpc *target,
                          const RcCombatCalc *base, bool special,
                          RcPendingHit *hits, int capacity, const char **failure) {
    const RcInvSlot *weapon = &world->player.equipment[EQUIP_WEAPON];
    if (weapon->item_id != RALOS_EMPTY && weapon->item_id != RALOS_CHARGED)
        return 0;
    int count = weapon->item_id == RALOS_CHARGED ? 2 : 1;
    if (capacity < count) {
        *failure = "Insufficient hit slots for the Tonalztics attack.";
        return -1;
    }
    if ((count == 2 && (weapon->state_id == 0 || weapon->state_id > MAX_CHARGES))
            || (count == 1 && weapon->state_id != 0)) {
        *failure = "Charge the Tonalztics with Sunfire splinters before using the charged form.";
        return -1;
    }
    RcCombatCalc calc = *base;
    calc.max_hit = calc.max_hit * 3 / 4;
    int defence = target->stats[1];
    for (int i = 0; i < count; i++) {
        // B237-era Division: 10% of Magic; the 2026 buff is a later rule.
        calc.defence_roll = (defence + 9) * 64;
        calc.hit_chance = rc_hit_chance(calc.attack_roll, calc.defence_roll);
        RcCombatRoll roll = rc_roll_attack(&calc, &world->rng_state, true);
        if (target->force_player_max_hit) roll = (RcCombatRoll){true, calc.max_hit};
        int drain = special && roll.accurate ? target->stats[5] / 10 : 0;
        if (drain > defence) drain = defence;
        hits[i] = (RcPendingHit){.damage = roll.damage, .max_hit = calc.max_hit,
            .accurate = roll.accurate, .defence_drain = drain};
        defence -= drain;
    }
    return count;
}

int rc_content_ralos_consume_charge(RcWorld *world, int weapon_id) {
    if (weapon_id != RALOS_CHARGED) return 1;
    RcItemTransaction tx;
    if (rc_item_tx_begin(&tx, world).code != RC_ITEM_RESULT_OK) return 0;
    RcInvSlot *weapon = &tx.equipment[EQUIP_WEAPON];
    if (weapon->item_id != weapon_id || weapon->state_id == 0
            || weapon->state_id > MAX_CHARGES) return 0;
    if (--weapon->state_id == 0) weapon->item_id = RALOS_EMPTY;
    return rc_item_tx_commit(&tx).code == RC_ITEM_RESULT_OK;
}

static RcInteractionHandlerResult ralos_option(RcWorld *world, RcPlayer *p,
                                                const RcPendingInteraction *it,
                                                void *ctx) {
    (void)p;
    (void)ctx;
    RcItemTransaction tx;
    if (rc_item_tx_begin(&tx, world).code != RC_ITEM_RESULT_OK)
        return rc_interaction_result_message("Item transaction unavailable.");
    int slot = it->target.inventory_slot;
    int source = it->op == RC_INTERACTION_USE_ON ? it->source_inventory_slot : -1;
    if (it->target.definition_id == SUNFIRE) {
        slot = source;
        source = it->target.inventory_slot;
    }
    if (slot < 0 || slot >= RC_INVENTORY_SIZE)
        return rc_interaction_result_message("Weapon slot is no longer available.");
    RcInvSlot *weapon = &tx.inventory[slot];
    if ((weapon->item_id != RALOS_EMPTY && weapon->item_id != RALOS_CHARGED)
            || weapon->state_id > MAX_CHARGES
            || (weapon->item_id == RALOS_EMPTY && weapon->state_id != 0))
        return rc_interaction_result_message("Invalid Tonalztics item state.");
    if (it->op == RC_INTERACTION_OP3) {
        char message[RC_INTERACTION_RESULT_MESSAGE_LEN];
        snprintf(message, sizeof(message), "Tonalztics has %u charges.", weapon->state_id);
        return rc_interaction_result_message(message);
    }
    if (it->op == RC_INTERACTION_OP5) {
        if (weapon->state_id && rc_item_tx_add(&tx, SUNFIRE,
                (int)weapon->state_id, 0).code != RC_ITEM_RESULT_OK)
            return rc_interaction_result_message("Make room for the returned Sunfire splinters.");
        weapon->item_id = RALOS_EMPTY;
        weapon->state_id = 0;
    } else {
        if (source < 0) source = rc_inv_find(tx.inventory, SUNFIRE);
        if (source < 0 || source >= RC_INVENTORY_SIZE
                || tx.inventory[source].item_id != SUNFIRE)
            return rc_interaction_result_message("You need Sunfire splinters to charge this weapon.");
        int amount = MAX_CHARGES - (int)weapon->state_id;
        if (amount > tx.inventory[source].quantity) amount = tx.inventory[source].quantity;
        if (amount <= 0) return rc_interaction_result_message("The weapon is fully charged.");
        if (rc_item_tx_remove_slot(&tx, source, amount, UINT32_MAX).code != RC_ITEM_RESULT_OK)
            return rc_interaction_result_message("The Sunfire splinters changed; charge cancelled.");
        weapon->item_id = RALOS_CHARGED;
        weapon->state_id += (uint32_t)amount;
    }
    if (rc_item_tx_commit(&tx).code != RC_ITEM_RESULT_OK)
        return rc_interaction_result_message("Item transaction failed; no charges changed.");
    return rc_interaction_result_complete();
}

int rc_content_ralos_register(RcWorld *world) {
    const int ids[] = {RALOS_EMPTY, RALOS_CHARGED};
    for (unsigned i = 0; i < sizeof(ids) / sizeof(ids[0]); i++) {
        int first_op = ids[i] == RALOS_EMPTY ? RC_INTERACTION_USE_ON : RC_INTERACTION_OP3;
        for (int op = first_op; op <= RC_INTERACTION_USE_ON; op++) {
            RcInteractionDispatchKey key = rc_interaction_dispatch_key_any();
            key.kind = RC_INTERACTION_INVENTORY_ITEM;
            key.op = op;
            key.definition_id = ids[i];
            if (op == RC_INTERACTION_USE_ON) key.source_item_id = SUNFIRE;
            if (!rc_interaction_register_world_handler(world, &key, ralos_option, NULL)) {
                fprintf(stderr, "ralos: could not register inventory handler\n");
                return 0;
            }
            if (op == RC_INTERACTION_USE_ON) {
                key.definition_id = SUNFIRE;
                key.source_item_id = ids[i];
                if (!rc_interaction_register_world_handler(world, &key, ralos_option, NULL)) {
                    fprintf(stderr, "ralos: could not register charging handler\n");
                    return 0;
                }
            }
        }
    }
    return 1;
}
