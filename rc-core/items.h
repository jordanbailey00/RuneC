#ifndef RC_ITEMS_H
#define RC_ITEMS_H

#include "types.h"

// Item definition (loaded from cache/data)
typedef struct {
    int id;
    char name[64];
    int value;
    bool stackable;
    bool equippable;
    int equip_slot;
    int attack_stab, attack_slash, attack_crush;
    int attack_magic, attack_ranged;
    int defence_stab, defence_slash, defence_crush;
    int defence_magic, defence_ranged;
    int strength_bonus;
    int ranged_strength;
    int magic_damage;
    int prayer_bonus;
    int attack_speed;
    int attack_range;
} RcItemDef;

extern RcItemDef g_item_defs[RC_MAX_ITEM_DEFS];
extern int g_item_def_count;

int rc_load_item_defs(const char *path);

// Inventory operations
int  rc_inv_add(RcInvSlot *inv, int item_id, int quantity);
void rc_inv_remove(RcInvSlot *inv, int slot);
int  rc_inv_find(const RcInvSlot *inv, int item_id);
int  rc_inv_free_slot(const RcInvSlot *inv);

// Equipment bonus recalculation
void rc_recalc_bonuses(RcPlayer *player);

#endif
