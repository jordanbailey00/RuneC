#include "magic.h"
#include "items.h"
#include "interaction.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

enum { AIR = 556, WATER = 555, EARTH = 557, FIRE = 554, NATURE = 561,
       SUNFIRE = 28929 };

static const struct { int item, first, second; } combos[] = {
    {4695, AIR, WATER}, {4696, AIR, EARTH}, {4698, WATER, EARTH},
    {4697, AIR, FIRE}, {4694, WATER, FIRE}, {4699, EARTH, FIRE},
};

static unsigned element(int rune) {
    return rune == AIR ? 1 : rune == WATER ? 2 : rune == EARTH ? 4 : rune == FIRE ? 8 : 0;
}

static unsigned staff_elements(const RcInvSlot *slot) {
    const RcItemDef *def = rc_item_def_get(slot->item_id);
    if (!def || slot->quantity <= 0) return 0;
    char name[sizeof(def->name)];
    for (unsigned i = 0; i < sizeof(name); i++) name[i] = (char)tolower((unsigned char)def->name[i]);
    if (strstr(name, "kodai wand")) return 2;
    if (strstr(name, "devil's element")) return 15;
    if (!strstr(name, "staff") && !strstr(name, "wand")) return 0;
    if (strstr(name, "mist")) return 3;
    if (strstr(name, "dust")) return 5;
    if (strstr(name, "mud")) return 6;
    if (strstr(name, "smoke")) return 9;
    if (strstr(name, "steam")) return 10;
    if (strstr(name, "lava")) return 12;
    if (strstr(name, "air")) return 1;
    if (strstr(name, "water")) return 2;
    if (strstr(name, "earth")) return 4;
    if (strstr(name, "fire")) return 8;
    return 0;
}

static int pouch_slots(const RcInvSlot *inventory) {
    int slots = 0;
    for (int i = 0; i < RC_INVENTORY_SIZE; i++) {
        if (inventory[i].quantity <= 0) continue;
        const RcItemDef *def = rc_item_def_get(inventory[i].item_id);
        if (!def) continue;
        if (!strcmp(def->name, "Divine rune pouch") || !strcmp(def->name, "Divine rune pouch (l)"))
            return 4;
        if (!strcmp(def->name, "Rune pouch") || !strcmp(def->name, "Rune pouch (l)")) slots = 3;
    }
    return slots;
}

typedef struct {
    RcInvSlot inventory[RC_INVENTORY_SIZE], equipment[RC_EQUIP_COUNT], pouch[4];
    int pouch_slots;
} RunePayment;

static int take(RunePayment *payment, int item, int amount) {
    int left = amount;
    for (int i = 0; i < RC_INVENTORY_SIZE && left; i++) {
        RcInvSlot *slot = &payment->inventory[i];
        if (slot->item_id == item && slot->state_id == 0)
            left -= rc_inv_remove_quantity(payment->inventory, i, left);
    }
    for (int i = 0; i < payment->pouch_slots && left; i++) {
        RcInvSlot *slot = &payment->pouch[i];
        if (slot->item_id != item || slot->state_id || slot->quantity <= 0 || slot->quantity > 16000)
            continue;
        int count = slot->quantity < left ? slot->quantity : left;
        left -= count;
        slot->quantity -= count;
        if (!slot->quantity) *slot = (RcInvSlot){.item_id = -1};
    }
    return amount - left;
}

static int requirement(const RcSpellDef *spell, int item) {
    for (int i = 0; i < spell->rune_count; i++)
        if (spell->runes[i].item_id == (uint32_t)item) return i;
    return -1;
}

// Validation and payment reserve the same concrete stock, including partial stacks.
static int plan_payment(const RcPlayer *player, const RcSpellDef *spell, RunePayment *payment) {
    if (!player || !spell || spell->rune_count > RC_SPELL_MAX_RUNES) return 0;
    memcpy(payment->inventory, player->inventory, sizeof(payment->inventory));
    memcpy(payment->equipment, player->equipment, sizeof(payment->equipment));
    memcpy(payment->pouch, player->rune_pouch, sizeof(payment->pouch));
    payment->pouch_slots = pouch_slots(payment->inventory);
    unsigned unlimited = staff_elements(&payment->equipment[EQUIP_WEAPON]);
    RcInvSlot *shield = &payment->equipment[EQUIP_SHIELD];
    unsigned tome = shield->quantity > 0 && shield->state_id > 0
        ? shield->item_id == 20714 ? 8 : shield->item_id == 25574 ? 2
        : shield->item_id == 30064 ? 4 : 0 : 0;
    unlimited |= tome;
    int remaining[RC_SPELL_MAX_RUNES] = {0};
    for (int i = 0; i < spell->rune_count; i++) {
        if (!spell->runes[i].qty || spell->runes[i].item_id > INT32_MAX) return 0;
        // Definitions must not repeat identities; this also protects direct hook callers.
        if (requirement(spell, spell->runes[i].item_id) != i) return 0;
        remaining[i] = (unlimited & element(spell->runes[i].item_id)) ? 0 : spell->runes[i].qty;
    }
    for (unsigned i = 0; i < sizeof(combos) / sizeof(combos[0]); i++) {
        int a = requirement(spell, combos[i].first), b = requirement(spell, combos[i].second);
        if (a < 0 || b < 0 || !remaining[a] || !remaining[b]) continue;
        int count = take(payment, combos[i].item, remaining[a] < remaining[b] ? remaining[a] : remaining[b]);
        remaining[a] -= count;
        remaining[b] -= count;
    }
    for (int i = 0; i < spell->rune_count; i++) {
        int item = spell->runes[i].item_id;
        remaining[i] -= take(payment, item, remaining[i]);
        if (item == FIRE) remaining[i] -= take(payment, SUNFIRE, remaining[i]);
        RcInvSlot *weapon = &payment->equipment[EQUIP_WEAPON];
        if (item == NATURE && weapon->item_id == 22370 && weapon->quantity > 0 && weapon->state_id <= 1000) {
            int used = remaining[i] < (int)weapon->state_id ? remaining[i] : (int)weapon->state_id;
            remaining[i] -= used;
            weapon->state_id -= used;
            if (!weapon->state_id) weapon->item_id = 22368;
        }
    }
    for (unsigned i = 0; i < sizeof(combos) / sizeof(combos[0]); i++) {
        int a = requirement(spell, combos[i].first), b = requirement(spell, combos[i].second);
        int first = a >= 0 ? remaining[a] : 0, second = b >= 0 ? remaining[b] : 0;
        int count = take(payment, combos[i].item, first > second ? first : second);
        if (a >= 0) remaining[a] -= count < first ? count : first;
        if (b >= 0) remaining[b] -= count < second ? count : second;
    }
    for (int i = 0; i < spell->rune_count; i++) if (remaining[i]) return 0;
    if ((tome == 8 && !strncmp(spell->name, "Fire ", 5)) ||
        (tome == 2 && !strncmp(spell->name, "Water ", 6)) ||
        (tome == 4 && !strncmp(spell->name, "Earth ", 6))) {
        if (--shield->state_id == 0) shield->item_id = tome == 8 ? 20716 : tome == 2 ? 25576 : 30066;
    }
    if (!strcmp(spell->name, "Iban Blast")) {
        RcInvSlot *weapon = &payment->equipment[EQUIP_WEAPON];
        uint32_t capacity = rc_content_magic_charge_capacity(weapon->item_id);
        if (!capacity || !weapon->state_id || weapon->state_id > capacity) return 0;
        weapon->state_id--;
    }
    return 1;
}

int rc_content_has_spell_runes(const RcWorld *world, const RcPlayer *player, const RcSpellDef *spell) {
    (void)world;
    RunePayment payment;
    return plan_payment(player, spell, &payment);
}

int rc_content_consume_spell_runes(RcWorld *world, RcPlayer *player, const RcSpellDef *spell) {
    if (!world || player != &world->player) return 0;
    RunePayment payment;
    RcItemTransaction tx;
    if (rc_item_tx_begin(&tx, world).code != RC_ITEM_RESULT_OK || !plan_payment(player, spell, &payment))
        return 0;
    memcpy(tx.inventory, payment.inventory, sizeof(tx.inventory));
    memcpy(tx.equipment, payment.equipment, sizeof(tx.equipment));
    memcpy(tx.rune_pouch, payment.pouch, sizeof(tx.rune_pouch));
    return rc_item_tx_commit(&tx).code == RC_ITEM_RESULT_OK;
}

static RcInteractionHandlerResult pouch_action(RcWorld *world, RcPlayer *player,
    const RcPendingInteraction *it, void *ctx) {
    (void)ctx;
    RcItemTransaction tx;
    if (rc_item_tx_begin(&tx, world).code != RC_ITEM_RESULT_OK)
        return rc_interaction_result_message("Rune pouch transaction unavailable.");
    int capacity = pouch_slots(tx.inventory);
    if (!capacity) return rc_interaction_result_message("You need to carry a rune pouch.");
    if (it->op == RC_INTERACTION_OP4) {
        for (int i = 0; i < 4; i++) {
            RcInvSlot *slot = &tx.rune_pouch[i];
            if (slot->quantity <= 0) continue;
            if (rc_item_tx_add(&tx, slot->item_id, slot->quantity, 0).code != RC_ITEM_RESULT_OK)
                return rc_interaction_result_message("Not enough inventory space to empty the rune pouch.");
            *slot = (RcInvSlot){.item_id = -1};
        }
    } else if (it->op == RC_INTERACTION_USE_ON) {
        int source = it->source_inventory_slot;
        if (source < 0 || source >= RC_INVENTORY_SIZE)
            return rc_interaction_result_message("The source item is unavailable.");
        RcInvSlot item = tx.inventory[source];
        const RcItemDef *def = rc_item_def_get(item.item_id);
        size_t len = def ? strlen(def->name) : 0;
        if (!def || !def->stackable || def->noted || item.state_id || len < 5
                || strcmp(def->name + len - 5, " rune"))
            return rc_interaction_result_message("Only unnoted runes can go in the rune pouch.");
        int slot = -1;
        for (int i = 0; i < capacity; i++)
            if (tx.rune_pouch[i].item_id == item.item_id) { slot = i; break; }
        if (slot < 0) for (int i = 0; i < capacity; i++)
            if (tx.rune_pouch[i].quantity == 0) { slot = i; break; }
        if (slot < 0) return rc_interaction_result_message("The rune pouch has no free rune slot.");
        int amount = 16000 - tx.rune_pouch[slot].quantity;
        if (amount > item.quantity) amount = item.quantity;
        if (amount <= 0) return rc_interaction_result_message("That pouch rune stack is full.");
        if (rc_item_tx_remove_slot(&tx, source, amount, item.generation).code != RC_ITEM_RESULT_OK)
            return rc_interaction_result_message("Runes changed; pouch transfer cancelled.");
        tx.rune_pouch[slot].item_id = item.item_id;
        tx.rune_pouch[slot].quantity += amount;
    } else {
        char message[RC_INTERACTION_RESULT_MESSAGE_LEN];
        int total = 0;
        for (int i = 0; i < capacity; i++) total += player->rune_pouch[i].quantity;
        snprintf(message, sizeof(message), "Rune pouch: %d runes, %d available slots. Use runes on it or choose Empty.", total, capacity);
        return rc_interaction_result_message(message);
    }
    if (rc_item_tx_commit(&tx).code != RC_ITEM_RESULT_OK)
        return rc_interaction_result_message("Rune pouch transaction failed; no items changed.");
    return rc_interaction_result_complete();
}

int rc_content_runes_register(RcWorld *world) {
    const int ids[] = {12791, 24416, 27281, 27509};
    const int ops[] = {RC_INTERACTION_OP1, RC_INTERACTION_OP4, RC_INTERACTION_USE_ON};
    for (unsigned i = 0; i < sizeof(ids) / sizeof(ids[0]); i++) {
        for (unsigned j = 0; j < sizeof(ops) / sizeof(ops[0]); j++) {
            RcInteractionDispatchKey key = rc_interaction_dispatch_key_any();
            key.kind = RC_INTERACTION_INVENTORY_ITEM;
            key.definition_id = ids[i];
            key.op = ops[j];
            if (!rc_interaction_register_world_handler(world, &key, pouch_action, NULL)) {
                fprintf(stderr, "runes: could not register pouch handler\n");
                return 0;
            }
        }
    }
    return 1;
}
