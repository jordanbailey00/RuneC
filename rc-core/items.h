#ifndef RC_ITEMS_H
#define RC_ITEMS_H

#include "types.h"

#define RC_ITEM_MAX_REQUIREMENTS SKILL_COUNT
#define RC_ITEM_ACTION_COUNT 5
#define RC_ITEM_ACTION_LEN 24
#define RC_ITEM_EXAMINE_LEN 128
#define RC_WEAPON_TYPE_LIMIT 64

enum {
    RC_ITEM_META_CACHE = 1u << 0,
    RC_ITEM_META_EQUIP = 1u << 1,
    RC_ITEM_META_EQUIP_STATS = 1u << 2,
    RC_ITEM_META_WEAPON = 1u << 3,
};

// Item definition (loaded from cache/data)
typedef struct {
    int id;
    char name[64];
    int value;
    int highalch;
    int lowalch;
    int buy_limit;
    int weight_grams;
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
    uint16_t equip_conflicts;
    int8_t wearpos1, wearpos2, wearpos3;
    uint16_t metadata_flags;
    char inventory_actions[RC_ITEM_ACTION_COUNT][RC_ITEM_ACTION_LEN];
    char ground_actions[RC_ITEM_ACTION_COUNT][RC_ITEM_ACTION_LEN];
    char examine[RC_ITEM_EXAMINE_LEN];
    int req_count;
    int required_combat_level;
    int req_skill[RC_ITEM_MAX_REQUIREMENTS];
    int req_level[RC_ITEM_MAX_REQUIREMENTS];
    int attack_stab, attack_slash, attack_crush;
    int attack_magic, attack_ranged;
    int defence_stab, defence_slash, defence_crush;
    int defence_magic, defence_ranged;
    int strength_bonus;
    int ranged_strength;
    int magic_damage;  // tenths of a percent
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

int rc_item_result_accepted(RcItemActionResult result);
const char *rc_item_result_name(RcItemResultCode code);

// Low-level fixed-array helpers for fixtures and staged container adapters.
// Production player mutations must use RcItemTransaction/player APIs below.
int  rc_inv_add(RcInvSlot *inv, int item_id, int quantity);
int  rc_inv_add_state(RcInvSlot *inv, int item_id, int quantity,
                      uint32_t state_id);
int  rc_inv_remove_quantity(RcInvSlot *inv, int slot, int quantity);
void rc_inv_remove(RcInvSlot *inv, int slot);
int  rc_inv_find(const RcInvSlot *inv, int item_id);
int  rc_inv_free_slot(const RcInvSlot *inv);

typedef struct {
    RcWorld *world;
    RcInvSlot inventory[RC_INVENTORY_SIZE];
    RcInvSlot equipment[RC_EQUIP_COUNT];
    RcInvSlot rune_pouch[4];
    RcInvSlot original_rune_pouch[4];
    uint32_t inventory_revision;
    uint32_t equipment_revision;
    uint32_t inventory_touched;
    uint16_t equipment_touched;
    bool valid;
} RcItemTransaction;

RcItemActionResult rc_item_tx_begin(RcItemTransaction *tx, RcWorld *world);
RcItemActionResult rc_item_tx_add(RcItemTransaction *tx, int item_id,
                                  int quantity, uint32_t state_id);
RcItemActionResult rc_item_tx_remove_slot(RcItemTransaction *tx, int slot,
                                          int quantity,
                                          uint32_t expected_generation);
RcItemActionResult rc_item_tx_remove_item(RcItemTransaction *tx, int item_id,
                                          int quantity);
RcItemActionResult rc_item_tx_move_slot(RcItemTransaction *tx, int from_slot,
                                        int to_slot, int quantity,
                                        uint32_t expected_generation);
RcItemActionResult rc_item_tx_remove_equipment(RcItemTransaction *tx,
                                               int slot, int quantity,
                                               uint32_t expected_generation);
RcItemActionResult rc_item_tx_commit(RcItemTransaction *tx);

RcItemActionResult rc_player_inventory_add(RcWorld *world, int item_id,
                                           int quantity, uint32_t state_id);
RcItemActionResult rc_player_inventory_remove_item(RcWorld *world, int item_id,
                                                   int quantity);
void rc_item_set_equip_requirement_hook(RcWorld *world,
                                        RcItemEquipRequirementHook hook,
                                        void *ctx);

RcItemActionResult rc_player_move_inventory_item_expected(
    RcWorld *world, int from_slot, int to_slot, uint32_t expected_generation);
RcItemActionResult rc_player_equip_expected(RcWorld *world, int inv_slot,
                                            uint32_t expected_generation);
RcItemActionResult rc_player_unequip_expected(RcWorld *world, int equip_slot,
                                              uint32_t expected_generation);
RcItemActionResult rc_player_drop_item_expected(RcWorld *world, int inv_slot,
                                                uint32_t expected_generation);

enum {
    RC_GROUND_TAKE_INVALID = 0,
    RC_GROUND_TAKE_OK = 1,
    RC_GROUND_TAKE_FULL = -1,
    RC_GROUND_TAKE_STALE = -2,
};

typedef struct {
    int total_rows;
    int matched_filter;
    int spawned;
    int skipped_filtered;
    int skipped_plane;
    int skipped_invalid;
    int skipped_capacity;
    int pages_loaded;
    int rows_loaded;
} RcGroundItemSpawnLoadStats;

int rc_player_take_ground_item(RcWorld *world, int ground_item_idx,
                               int expected_uid, int expected_version);
int rc_ground_item_spawn(RcWorld *world, int item_id, int quantity,
                         int x, int y, int plane, int owner_uid);
typedef struct {
    int reveal_ticks, lifetime_ticks, delay_ticks;
    int untradeable_lifetime_ticks;
} RcGroundPolicy;

typedef enum {
    RC_GROUND_GRANT_OK = 0,
    RC_GROUND_GRANT_DISABLED,
    RC_GROUND_GRANT_INVALID,
    RC_GROUND_GRANT_CAPACITY,
    RC_GROUND_GRANT_CONFLICT,
} RcGroundGrantResult;

// A batch is committed in full or leaves the ground store unchanged.
RcGroundGrantResult rc_ground_grant(RcWorld *world,
    const RcGroundGrant *grants, int count, int x, int y, int plane,
    int owner_uid, RcGroundPolicy policy);
RcGroundGrantResult rc_item_tx_commit_ground(RcItemTransaction *tx,
    const RcGroundGrant *grants, int count, int x, int y, int plane,
    int owner_uid, RcGroundPolicy policy);
int rc_ground_item_visible(const RcGroundItem *item, int owner_uid);
int rc_ground_item_reachable(const RcWorld *world, int x, int y, int plane);
void rc_ground_item_advance(RcGroundItem *item, RcTick elapsed);
void rc_ground_items_tick(RcWorld *world);
RcItemActionResult rc_player_pickup_item_expected(RcWorld *world, int index,
                                                 int uid, int version);
int rc_load_ground_item_spawns_rect_stats(RcWorld *world, const char *path,
                                          int min_x, int min_y,
                                          int max_x, int max_y,
                                          int min_plane, int max_plane,
                                          RcGroundItemSpawnLoadStats *stats);
void rc_clear_static_ground_items(RcWorld *world);

// Equipment bonus recalculation
void rc_recalc_bonuses(RcPlayer *player);

#endif
