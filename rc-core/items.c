#include "items.h"
#include <string.h>

RcItemDef g_item_defs[RC_MAX_ITEM_DEFS];
int g_item_def_count = 0;

int rc_load_item_defs(const char *path) {
    // TODO: load from binary file
    (void)path;
    return 0;
}

int rc_inv_add(RcInvSlot *inv, int item_id, int quantity) {
    // Try to stack with existing
    for (int i = 0; i < RC_INVENTORY_SIZE; i++) {
        if (inv[i].item_id == item_id && item_id >= 0 &&
            item_id < g_item_def_count && g_item_defs[item_id].stackable) {
            inv[i].quantity += quantity;
            return i;
        }
    }
    // Find empty slot
    for (int i = 0; i < RC_INVENTORY_SIZE; i++) {
        if (inv[i].item_id == -1) {
            inv[i].item_id = item_id;
            inv[i].quantity = quantity;
            return i;
        }
    }
    return -1; // full
}

void rc_inv_remove(RcInvSlot *inv, int slot) {
    if (slot < 0 || slot >= RC_INVENTORY_SIZE) return;
    inv[slot].item_id = -1;
    inv[slot].quantity = 0;
}

int rc_inv_find(const RcInvSlot *inv, int item_id) {
    for (int i = 0; i < RC_INVENTORY_SIZE; i++) {
        if (inv[i].item_id == item_id) return i;
    }
    return -1;
}

int rc_inv_free_slot(const RcInvSlot *inv) {
    for (int i = 0; i < RC_INVENTORY_SIZE; i++) {
        if (inv[i].item_id == -1) return i;
    }
    return -1;
}

void rc_recalc_bonuses(RcPlayer *player) {
    memset(player->equipment_bonuses, 0, sizeof(player->equipment_bonuses));
    for (int i = 0; i < RC_EQUIP_COUNT; i++) {
        int id = player->equipment[i].item_id;
        if (id < 0 || id >= g_item_def_count) continue;
        RcItemDef *def = &g_item_defs[id];
        player->equipment_bonuses[0]  += def->attack_stab;
        player->equipment_bonuses[1]  += def->attack_slash;
        player->equipment_bonuses[2]  += def->attack_crush;
        player->equipment_bonuses[3]  += def->attack_magic;
        player->equipment_bonuses[4]  += def->attack_ranged;
        player->equipment_bonuses[5]  += def->defence_stab;
        player->equipment_bonuses[6]  += def->defence_slash;
        player->equipment_bonuses[7]  += def->defence_crush;
        player->equipment_bonuses[8]  += def->defence_magic;
        player->equipment_bonuses[9]  += def->defence_ranged;
        player->equipment_bonuses[10] += def->strength_bonus;
        player->equipment_bonuses[11] += def->ranged_strength;
        player->equipment_bonuses[12] += def->magic_damage;
        player->equipment_bonuses[13] += def->prayer_bonus;
    }
}
