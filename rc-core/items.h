#ifndef RC_ITEMS_H
#define RC_ITEMS_H

#include "types.h"

#define RC_ITEM_MAX_REQUIREMENTS 4

// Item definition (loaded from cache/data)
typedef struct {
    int id;
    char name[64];
    int value;
    int highalch;
    int lowalch;
    int buy_limit;
    int weight_cg;
    int linked_id_item;
    int linked_id_noted;
    int linked_id_placeholder;
    int kind;
    bool stackable;
    bool tradeable;
    bool members;
    bool quest_item;
    bool noted;
    bool noteable;
    bool placeholder;
    bool loaded;
    bool equippable;
    bool equipable_by_player;
    bool equipable_weapon;
    int equip_slot;
    int req_count;
    int req_skill[RC_ITEM_MAX_REQUIREMENTS];
    int req_level[RC_ITEM_MAX_REQUIREMENTS];
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
    int weapon_type;
    int stance_bits;
} RcItemDef;

extern RcItemDef g_item_defs[RC_MAX_ITEM_DEFS];
extern int g_item_def_count;

int rc_load_item_defs(const char *path);
int rc_load_item_defs_into(const char *path, RcItemDef *defs, int max_defs,
                           int *out_count);
void rc_item_use_defs(const RcItemDef *defs, int count);
void rc_item_reset_defs_if_active(const RcItemDef *defs);
const RcItemDef *rc_item_def_get(int item_id);

// Inventory operations
int  rc_inv_add(RcInvSlot *inv, int item_id, int quantity);
int  rc_inv_remove_quantity(RcInvSlot *inv, int slot, int quantity);
void rc_inv_remove(RcInvSlot *inv, int slot);
int  rc_inv_find(const RcInvSlot *inv, int item_id);
int  rc_inv_free_slot(const RcInvSlot *inv);

enum {
    RC_GROUND_TAKE_INVALID = 0,
    RC_GROUND_TAKE_OK = 1,
    RC_GROUND_TAKE_FULL = -1,
    RC_GROUND_TAKE_STALE = -2,
};

int rc_player_take_ground_item(RcWorld *world, int ground_item_idx,
                               int expected_uid, int expected_version);
int rc_ground_item_spawn(RcWorld *world, int item_id, int quantity,
                         int x, int y, int plane, int owner_uid);

// Equipment bonus recalculation
void rc_recalc_bonuses(RcPlayer *player);

#endif
