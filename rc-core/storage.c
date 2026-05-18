#include "storage.h"
#include "config.h"
#include "items.h"
#include "npc.h"
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

int rc_storage_kind_for_npc(const RcNpcDef *def, int option) {
    if (!def || option < 0 || option >= RC_NPC_OPTION_COUNT)
        return RC_STORAGE_NONE;
    const char *action = rc_npc_def_option(def, option);
    if (!action || !action[0])
        return RC_STORAGE_NONE;
    if (has_word(action, "Bank") || has_word(action, "bank"))
        return RC_STORAGE_BANK;
    if (has_word(action, "Collect") || has_word(action, "collect"))
        return RC_STORAGE_COLLECTION;
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
    world->player.interact_type = RC_INTERACT_OBJECT;
    world->player.interact_target = obj_id;
    world->player.interact_option = option;
    return kind;
}

int rc_player_open_storage_npc(RcWorld *world, int npc_uid, int option) {
    if (!world || !(world->enabled & RC_SUB_STORAGE)) return RC_STORAGE_NONE;
    if (!rc_player_action_allowed(world->enabled,
                                  RC_PLAYER_ACTION_INTERACT_NPC)) {
        return RC_STORAGE_NONE;
    }
    RcNpc *npc = NULL;
    for (int i = 0; i < world->npc_count; i++) {
        if (world->npcs[i].active && world->npcs[i].uid == npc_uid) {
            npc = &world->npcs[i];
            break;
        }
    }
    if (!npc || npc->is_dead || npc->def_id < 0 ||
            npc->def_id >= g_npc_def_count) {
        return RC_STORAGE_NONE;
    }
    int kind = rc_storage_kind_for_npc(&g_npc_defs[npc->def_id], option);
    if (!kind) return RC_STORAGE_NONE;
    world->player.storage_kind = kind;
    world->player.storage_target = npc_uid;
    world->player.storage_option = option;
    world->player.interact_type = RC_INTERACT_NPC;
    world->player.interact_target = npc_uid;
    world->player.interact_option = option;
    return kind;
}

int rc_player_close_storage(RcWorld *world) {
    if (!world || !(world->enabled & RC_SUB_STORAGE)) return 0;
    world->player.storage_kind = RC_STORAGE_NONE;
    world->player.storage_target = -1;
    world->player.storage_option = -1;
    if (world->player.interact_type == RC_INTERACT_OBJECT ||
            world->player.interact_type == RC_INTERACT_NPC) {
        world->player.interact_type = RC_INTERACT_NONE;
        world->player.interact_target = -1;
        world->player.interact_option = -1;
    }
    return 1;
}

static int unnoted_item_id(int item_id) {
    const RcItemDef *def = rc_item_def_get(item_id);
    if (def && def->noted && def->linked_id_item >= 0) {
        const RcItemDef *linked = rc_item_def_get(def->linked_id_item);
        if (linked && !linked->noted && !linked->placeholder)
            return linked->id;
    }
    return item_id;
}

static int bank_add_global(RcInvSlot *bank, uint8_t *tabs,
                           int item_id, int quantity, int new_tab) {
    item_id = unnoted_item_id(item_id);
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
            if (tabs) tabs[i] = (uint8_t)(new_tab < 0 ? 0 : new_tab);
            return i;
        }
    }
    return -1;
}

static int bank_add_tabbed(RcInvSlot *bank, uint8_t *tabs,
                           int item_id, int quantity, int tab) {
    item_id = unnoted_item_id(item_id);
    if (tab < 0) tab = 0;
    for (int i = 0; i < RC_BANK_SIZE; i++) {
        if (bank[i].item_id == item_id && tabs && tabs[i] == (uint8_t)tab) {
            bank[i].quantity += quantity;
            return i;
        }
    }
    for (int i = 0; i < RC_BANK_SIZE; i++) {
        if (bank[i].item_id == -1) {
            bank[i].item_id = item_id;
            bank[i].quantity = quantity;
            if (tabs) tabs[i] = (uint8_t)tab;
            return i;
        }
    }
    return -1;
}

int rc_bank_add_item(RcWorld *world, int item_id, int quantity) {
    if (!world || !(world->enabled & RC_SUB_STORAGE) || item_id < 0 ||
            quantity <= 0) {
        return -1;
    }
    return bank_add_global(world->player.bank, world->player.bank_tab,
                           item_id, quantity, 0);
}

int rc_bank_add_item_tab(RcWorld *world, int item_id, int quantity, int tab) {
    if (!world || !(world->enabled & RC_SUB_STORAGE) || item_id < 0 ||
            quantity <= 0) {
        return -1;
    }
    return bank_add_tabbed(world->player.bank, world->player.bank_tab,
                           item_id, quantity, tab);
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
    if (bank_add_global(world->player.bank, world->player.bank_tab,
                        slot->item_id, move, 0) < 0) return -1;
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
    if (slot->quantity == 0) {
        slot->item_id = -1;
        world->player.bank_tab[bank_slot] = 0;
    }
    return moved;
}
