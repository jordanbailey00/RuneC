#include "storage.h"
#include "config.h"
#include "items.h"
#include "objects.h"
#include "player_actions.h"

#include <string.h>

static int has_word(const char *s, const char *word) {
    return s && strstr(s, word) != NULL;
}

int rc_storage_kind_for_object(int obj_id, int option) {
    if (option < 0 || option >= RC_OBJECT_ACTIONS) return RC_STORAGE_NONE;
    const RcObjectBehavior *behavior = rc_object_behavior_get(obj_id);
    const RcObjectDef *def = rc_object_def_get(obj_id);
    if (!behavior || !def || !(behavior->action_mask & (1u << option))) {
        return RC_STORAGE_NONE;
    }
    const char *action = def->actions[option];
    if (has_word(action, "Deposit") || has_word(action, "deposit")) {
        return RC_STORAGE_DEPOSIT_BOX;
    }
    if (has_word(action, "Collect") || has_word(action, "collect")) {
        return RC_STORAGE_COLLECTION;
    }
    if (behavior->flags & RC_OBJ_BEHAVIOR_BANK) return RC_STORAGE_BANK;
    if (behavior->flags & RC_OBJ_BEHAVIOR_STORAGE) return RC_STORAGE_CONTAINER;
    return RC_STORAGE_NONE;
}

int rc_player_open_storage_object(RcWorld *world, int obj_id, int option) {
    if (!world || !(world->enabled & RC_SUB_STORAGE)) return RC_STORAGE_NONE;
    if (!rc_player_action_allowed(world->enabled,
                                  RC_PLAYER_ACTION_INTERACT_OBJECT)) {
        return RC_STORAGE_NONE;
    }
    int kind = rc_storage_kind_for_object(obj_id, option);
    if (!kind) return RC_STORAGE_NONE;
    world->player.storage_kind = kind;
    world->player.storage_target = obj_id;
    world->player.storage_option = option;
    world->player.interact_type = 3;
    world->player.interact_target = obj_id;
    world->player.interact_option = option;
    return kind;
}

static int bank_add(RcInvSlot *bank, int item_id, int quantity) {
    for (int i = 0; i < RC_BANK_SIZE; i++) {
        if (bank[i].item_id == item_id) {
            bank[i].quantity += quantity;
            return i;
        }
    }
    for (int i = 0; i < RC_BANK_SIZE; i++) {
        if (bank[i].item_id == -1) {
            bank[i].item_id = item_id;
            bank[i].quantity = quantity;
            return i;
        }
    }
    return -1;
}

int rc_bank_deposit_slot(RcWorld *world, int inv_slot, int quantity) {
    if (!world || !(world->enabled & RC_SUB_STORAGE)) return -1;
    if (world->player.storage_kind == RC_STORAGE_NONE
            || world->player.storage_kind == RC_STORAGE_COLLECTION) {
        return -1;
    }
    if (inv_slot < 0 || inv_slot >= RC_INVENTORY_SIZE) return -1;
    RcInvSlot *slot = &world->player.inventory[inv_slot];
    if (slot->item_id < 0 || slot->quantity <= 0) return -1;
    int move = quantity <= 0 || quantity > slot->quantity
             ? slot->quantity : quantity;
    if (bank_add(world->player.bank, slot->item_id, move) < 0) return -1;
    slot->quantity -= move;
    if (slot->quantity == 0) slot->item_id = -1;
    return move;
}

int rc_bank_withdraw_slot(RcWorld *world, int bank_slot, int quantity) {
    if (!world || !(world->enabled & RC_SUB_STORAGE)) return -1;
    if (world->player.storage_kind != RC_STORAGE_BANK
            && world->player.storage_kind != RC_STORAGE_CONTAINER) {
        return -1;
    }
    if (bank_slot < 0 || bank_slot >= RC_BANK_SIZE) return -1;
    RcInvSlot *slot = &world->player.bank[bank_slot];
    if (slot->item_id < 0 || slot->quantity <= 0) return -1;
    int move = quantity <= 0 || quantity > slot->quantity
             ? slot->quantity : quantity;
    const RcItemDef *def = rc_item_def_get(slot->item_id);
    int moved = 0;
    if (def && def->stackable) {
        moved = rc_inv_add(world->player.inventory, slot->item_id, move) >= 0
              ? move : 0;
    } else {
        while (moved < move &&
               rc_inv_add(world->player.inventory, slot->item_id, 1) >= 0) {
            moved++;
        }
    }
    if (!moved) return -1;
    slot->quantity -= moved;
    if (slot->quantity == 0) slot->item_id = -1;
    return moved;
}
