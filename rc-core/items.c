#include "items.h"
#include "assets.h"
#include "combat.h"
#include "config.h"
#include "events.h"
#include "interaction.h"
#include "player_actions.h"
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

RcItemDef g_item_defs[RC_MAX_ITEM_DEFS];
int g_item_def_count = 0;

enum {
    IDEF_MAGIC = 0x49444546,
    IDEF_V1 = 1,
    IDEF_V2 = 2,
};

enum {
    IDEF_STACKABLE       = 1u << 0,
    IDEF_TRADEABLE       = 1u << 1,
    IDEF_MEMBERS         = 1u << 2,
    IDEF_QUEST_ITEM      = 1u << 3,
    IDEF_HAS_EQUIPMENT   = 1u << 4,
    IDEF_HAS_WEAPON      = 1u << 5,
    IDEF_NOTED           = 1u << 6,
    IDEF_PLACEHOLDER     = 1u << 7,
    IDEF_NOTEABLE        = 1u << 8,
    IDEF_EQUIP_PLAYER    = 1u << 9,
    IDEF_EQUIP_WEAPON    = 1u << 10,
};

static int read_u8(const unsigned char **p, const unsigned char *end,
                   uint8_t *out) {
    if (*p + 1 > end) return 0;
    *out = **p;
    *p += 1;
    return 1;
}

static int read_u16(const unsigned char **p, const unsigned char *end,
                    uint16_t *out) {
    if (*p + 2 > end) return 0;
    *out = (uint16_t)(*p)[0] | ((uint16_t)(*p)[1] << 8);
    *p += 2;
    return 1;
}

static int read_i16(const unsigned char **p, const unsigned char *end,
                    int *out) {
    uint16_t raw;
    if (!read_u16(p, end, &raw)) return 0;
    *out = (int)(int16_t)raw;
    return 1;
}

static int read_u32(const unsigned char **p, const unsigned char *end,
                    uint32_t *out) {
    if (*p + 4 > end) return 0;
    *out = (uint32_t)(*p)[0] | ((uint32_t)(*p)[1] << 8)
         | ((uint32_t)(*p)[2] << 16) | ((uint32_t)(*p)[3] << 24);
    *p += 4;
    return 1;
}

static int id_or_missing(uint32_t value) {
    return value == UINT32_MAX ? -1 : (int)value;
}

static int parse_equipment(RcItemDef *def, const unsigned char **p,
                           const unsigned char *end) {
    uint8_t slot, req_count;
    if (!read_u8(p, end, &slot) || !read_u8(p, end, &req_count)) return 0;

    def->equip_slot = slot == 0xFF ? -1 : (int)slot;
    def->req_count = 0;
    for (int i = 0; i < req_count; i++) {
        uint8_t skill, level;
        if (!read_u8(p, end, &skill) || !read_u8(p, end, &level)) return 0;
        if (skill < SKILL_COUNT && def->req_count < RC_ITEM_MAX_REQUIREMENTS) {
            int idx = def->req_count++;
            def->req_skill[idx] = skill;
            def->req_level[idx] = level;
        }
    }

    int *bonuses[] = {
        &def->attack_stab, &def->attack_slash, &def->attack_crush,
        &def->attack_magic, &def->attack_ranged,
        &def->defence_stab, &def->defence_slash, &def->defence_crush,
        &def->defence_magic, &def->defence_ranged,
        &def->strength_bonus, &def->ranged_strength,
        &def->magic_damage, &def->prayer_bonus,
    };
    for (int i = 0; i < 14; i++) {
        if (!read_i16(p, end, bonuses[i])) return 0;
    }
    return 1;
}

static int parse_weapon(RcItemDef *def, const unsigned char **p,
                        const unsigned char *end) {
    uint8_t attack_speed, weapon_type, stance_bits, stance_count;
    if (!read_u8(p, end, &attack_speed) || !read_u8(p, end, &weapon_type)
            || !read_u8(p, end, &stance_bits)
            || !read_u8(p, end, &stance_count)) {
        return 0;
    }
    def->attack_speed = attack_speed;
    def->attack_range = 1;
    def->weapon_type = weapon_type;
    def->stance_bits = stance_bits;
    for (int i = 0; i < stance_count; i++) {
        uint8_t len;
        if (!read_u8(p, end, &len)) return 0;
        if (*p + len > end) return 0;
        *p += len;
    }
    return 1;
}

static int parse_record(RcItemDef *def, const unsigned char *buf,
                        uint32_t len, uint32_t version) {
    const unsigned char *p = buf;
    const unsigned char *end = buf + len;
    uint32_t id;
    uint32_t tmp;
    uint8_t flags8, name_len;
    uint16_t flags16;

    memset(def, 0, sizeof(*def));
    def->id = -1;
    def->equip_slot = -1;
    def->linked_id_item = -1;
    def->linked_id_noted = -1;
    def->linked_id_placeholder = -1;
    def->ground_model_id = -1;
    for (int i = 0; i < 3; i++) {
        def->male_model_ids[i] = -1;
        def->female_model_ids[i] = -1;
    }

    if (!read_u32(&p, end, &id)) return 0;
    uint32_t flags;
    if (version == IDEF_V1) {
        if (!read_u8(&p, end, &flags8)) return 0;
        flags = flags8;
        def->kind = 0;
    } else {
        uint8_t kind;
        if (!read_u16(&p, end, &flags16) || !read_u8(&p, end, &kind)) {
            return 0;
        }
        flags = flags16;
        def->kind = kind;
    }
    if (!read_u8(&p, end, &name_len)) return 0;
    if (p + name_len > end) return 0;

    def->id = (int)id;
    size_t copy_len = name_len < sizeof(def->name) - 1
                    ? name_len : sizeof(def->name) - 1;
    memcpy(def->name, p, copy_len);
    def->name[copy_len] = '\0';
    p += name_len;

    uint16_t weight_cg;
    if (!read_u16(&p, end, &weight_cg)) return 0;
    def->weight_cg = weight_cg;
    if (!read_u32(&p, end, &tmp)) return 0;
    def->highalch = (int)tmp;
    if (!read_u32(&p, end, &tmp)) return 0;
    def->lowalch = (int)tmp;
    if (!read_u32(&p, end, &tmp)) return 0;
    def->value = (int)tmp;

    if (version == IDEF_V1) {
        if (!read_u32(&p, end, &tmp)) return 0;
        def->linked_id_noted = id_or_missing(tmp);
    } else {
        if (!read_u32(&p, end, &tmp)) return 0;
        def->linked_id_item = id_or_missing(tmp);
        if (!read_u32(&p, end, &tmp)) return 0;
        def->linked_id_noted = id_or_missing(tmp);
        if (!read_u32(&p, end, &tmp)) return 0;
        def->linked_id_placeholder = id_or_missing(tmp);
        if (!read_u32(&p, end, &tmp)) return 0;
        def->buy_limit = id_or_missing(tmp);

        int *models[] = {
            &def->ground_model_id,
            &def->male_model_ids[0], &def->male_model_ids[1],
            &def->male_model_ids[2],
            &def->female_model_ids[0], &def->female_model_ids[1],
            &def->female_model_ids[2],
        };
        for (int i = 0; i < 7; i++) {
            if (!read_u32(&p, end, &tmp)) return 0;
            *models[i] = id_or_missing(tmp);
        }
    }

    def->stackable = (flags & IDEF_STACKABLE) != 0;
    def->tradeable = (flags & IDEF_TRADEABLE) != 0;
    def->members = (flags & IDEF_MEMBERS) != 0;
    def->quest_item = (flags & IDEF_QUEST_ITEM) != 0;
    def->noted = (flags & IDEF_NOTED) != 0;
    def->noteable = (flags & IDEF_NOTEABLE) != 0;
    def->placeholder = (flags & IDEF_PLACEHOLDER) != 0;
    def->equippable = (flags & IDEF_HAS_EQUIPMENT) != 0;
    def->equipable_by_player = (flags & IDEF_EQUIP_PLAYER) != 0;
    def->equipable_weapon = (flags & IDEF_EQUIP_WEAPON) != 0;

    if ((flags & IDEF_HAS_EQUIPMENT) && !parse_equipment(def, &p, end)) {
        return 0;
    }
    if ((flags & IDEF_HAS_WEAPON) && !parse_weapon(def, &p, end)) {
        return 0;
    }
    def->loaded = true;
    return 1;
}

int rc_load_item_defs(const char *path) {
    if (!path) return -1;
    FILE *f = rc_asset_fopen(path, "rb");
    if (!f) return -1;

    uint32_t magic, version, count;
    if (fread(&magic, sizeof(magic), 1, f) != 1
            || fread(&version, sizeof(version), 1, f) != 1
            || fread(&count, sizeof(count), 1, f) != 1) {
        rc_asset_close(f);
        return -1;
    }
    if (magic != IDEF_MAGIC || (version != IDEF_V1 && version != IDEF_V2)) {
        rc_asset_close(f);
        return -1;
    }

    memset(g_item_defs, 0, sizeof(g_item_defs));
    g_item_def_count = 0;
    int loaded = 0;

    for (uint32_t i = 0; i < count; i++) {
        uint32_t len;
        if (fread(&len, sizeof(len), 1, f) != 1) {
            rc_asset_close(f);
            return -1;
        }
        unsigned char *buf = malloc(len);
        if (!buf) {
            rc_asset_close(f);
            return -1;
        }
        if (fread(buf, 1, len, f) != len) {
            free(buf);
            rc_asset_close(f);
            return -1;
        }
        RcItemDef def;
        if (!parse_record(&def, buf, len, version)) {
            free(buf);
            rc_asset_close(f);
            return -1;
        }
        free(buf);

        if (def.id >= 0 && def.id < RC_MAX_ITEM_DEFS) {
            g_item_defs[def.id] = def;
            loaded++;
        }
    }

    rc_asset_close(f);
    g_item_def_count = loaded;
    return loaded;
}

const RcItemDef *rc_item_def_get(int item_id) {
    if (item_id < 0 || item_id >= RC_MAX_ITEM_DEFS) return NULL;
    const RcItemDef *def = &g_item_defs[item_id];
    if (def->loaded) return def;
    if (item_id < g_item_def_count && def->id == item_id) return def;
    return NULL;
}

int rc_inv_add(RcInvSlot *inv, int item_id, int quantity) {
    if (!inv || item_id < 0 || quantity <= 0) return -1;
    const RcItemDef *def = rc_item_def_get(item_id);
    int stackable = def && def->stackable;
    int first_slot = -1;

    if (stackable) {
        for (int i = 0; i < RC_INVENTORY_SIZE; i++) {
            if (inv[i].item_id == item_id) {
                inv[i].quantity += quantity;
                return i;
            }
        }
        for (int i = 0; i < RC_INVENTORY_SIZE; i++) {
            if (inv[i].item_id == -1) {
                inv[i].item_id = item_id;
                inv[i].quantity = quantity;
                return i;
            }
        }
        return -1;
    }

    int free_count = 0;
    for (int i = 0; i < RC_INVENTORY_SIZE; i++)
        free_count += inv[i].item_id == -1;
    if (free_count < quantity) return -1;

    for (int i = 0; i < RC_INVENTORY_SIZE && quantity > 0; i++) {
        if (inv[i].item_id != -1) continue;
        if (first_slot < 0) first_slot = i;
        inv[i].item_id = item_id;
        inv[i].quantity = 1;
        quantity--;
    }
    return first_slot;
}

int rc_inv_remove_quantity(RcInvSlot *inv, int slot, int quantity) {
    if (!inv || slot < 0 || slot >= RC_INVENTORY_SIZE || quantity <= 0) {
        return 0;
    }
    if (inv[slot].item_id < 0 || inv[slot].quantity <= 0) return 0;
    int removed = quantity < inv[slot].quantity ? quantity : inv[slot].quantity;
    inv[slot].quantity -= removed;
    if (inv[slot].quantity <= 0) {
        inv[slot].item_id = -1;
        inv[slot].quantity = 0;
    }
    return removed;
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
    if (!player) return;
    memset(player->equipment_bonuses, 0, sizeof(player->equipment_bonuses));
    player->weight = 0;
    for (int i = 0; i < RC_INVENTORY_SIZE; i++) {
        int id = player->inventory[i].item_id;
        int qty = player->inventory[i].quantity > 0
                ? player->inventory[i].quantity : 1;
        const RcItemDef *def = rc_item_def_get(id);
        if (def) player->weight += def->weight_cg * qty;
    }
    for (int i = 0; i < RC_EQUIP_COUNT; i++) {
        int id = player->equipment[i].item_id;
        int qty = player->equipment[i].quantity > 0
                ? player->equipment[i].quantity : 1;
        const RcItemDef *def = rc_item_def_get(id);
        if (!def) continue;
        player->weight += def->weight_cg * qty;
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
    rc_refresh_player_combat_style(player);
}

static int valid_inv_slot(int slot) {
    return slot >= 0 && slot < RC_INVENTORY_SIZE;
}

static int valid_equip_slot(int slot) {
    return slot >= 0 && slot < RC_EQUIP_COUNT;
}

static int inventory_enabled(const RcWorld *world) {
    return world && (world->enabled & RC_SUB_INVENTORY);
}

static int equipment_enabled(const RcWorld *world) {
    return world && (world->enabled & RC_SUB_EQUIPMENT);
}

static int loot_enabled(const RcWorld *world) {
    return world && (world->enabled & RC_SUB_LOOT);
}

static int requirements_met(const RcPlayer *player, const RcItemDef *def) {
    for (int i = 0; i < def->req_count; i++) {
        int skill = def->req_skill[i];
        if (skill < 0 || skill >= SKILL_COUNT) return 0;
        if (player->skills.base_level[skill] < def->req_level[i]) return 0;
    }
    return 1;
}

static int item_two_handed(const RcItemDef *def) {
    if (!def || def->equip_slot != EQUIP_WEAPON) return 0;
    return def->weapon_type == 1   /* 2h_sword */
        || def->weapon_type == 17  /* scythe */
        || def->weapon_type == 19  /* spear */
        || def->weapon_type == 24  /* two-handed_sword */
        || def->weapon_type == 25; /* bow */
}

static int add_return_item(RcPlayer *player, RcInvSlot item) {
    if (item.item_id < 0 || item.quantity <= 0) return 1;
    return rc_inv_add(player->inventory, item.item_id, item.quantity) >= 0;
}

int rc_player_move_inventory_item(RcWorld *world, int from_slot, int to_slot) {
    if (!inventory_enabled(world) || !valid_inv_slot(from_slot)
            || !valid_inv_slot(to_slot) || from_slot == to_slot) {
        return 0;
    }
    RcInvSlot *inv = world->player.inventory;
    if (inv[from_slot].item_id < 0 || inv[from_slot].quantity <= 0) return 0;

    const RcItemDef *from_def = rc_item_def_get(inv[from_slot].item_id);
    if (inv[to_slot].item_id == inv[from_slot].item_id
            && from_def && from_def->stackable) {
        inv[to_slot].quantity += inv[from_slot].quantity;
        rc_inv_remove(inv, from_slot);
        rc_recalc_bonuses(&world->player);
        return 1;
    }

    RcInvSlot tmp = inv[to_slot];
    inv[to_slot] = inv[from_slot];
    inv[from_slot] = tmp;
    rc_recalc_bonuses(&world->player);
    return 1;
}

void rc_player_equip(RcWorld *world, int inv_slot) {
    if (!inventory_enabled(world) || !equipment_enabled(world)
            || !rc_player_action_allowed(world->enabled,
                                         RC_PLAYER_ACTION_EQUIP)
            || !valid_inv_slot(inv_slot)) {
        return;
    }

    RcPlayer *player = &world->player;
    RcInvSlot item = player->inventory[inv_slot];
    if (item.item_id < 0 || item.quantity <= 0) return;
    const RcItemDef *def = rc_item_def_get(item.item_id);
    if (!def || !def->equippable || !def->equipable_by_player
            || !valid_equip_slot(def->equip_slot)
            || !requirements_met(player, def)) {
        return;
    }

    int target = def->equip_slot;
    const RcItemDef *target_def =
        rc_item_def_get(player->equipment[target].item_id);
    if (target_def && target_def->stackable
            && player->equipment[target].item_id == item.item_id) {
        player->equipment[target].quantity += item.quantity;
        rc_inv_remove(player->inventory, inv_slot);
        rc_recalc_bonuses(player);
        return;
    }

    RcInvSlot old_target = player->equipment[target];
    RcInvSlot old_shield = {-1, 0};
    int clear_shield = item_two_handed(def)
                    && player->equipment[EQUIP_SHIELD].item_id >= 0;
    if (clear_shield) old_shield = player->equipment[EQUIP_SHIELD];

    int returns = (old_target.item_id >= 0) + (old_shield.item_id >= 0);
    int free_slots = 1; /* source inventory slot becomes free */
    for (int i = 0; i < RC_INVENTORY_SIZE; i++)
        free_slots += i != inv_slot && player->inventory[i].item_id == -1;
    if (returns > free_slots) return;

    rc_inv_remove(player->inventory, inv_slot);
    player->equipment[target] = item;
    if (clear_shield) {
        player->equipment[EQUIP_SHIELD].item_id = -1;
        player->equipment[EQUIP_SHIELD].quantity = 0;
    }
    if (!add_return_item(player, old_target)
            || !add_return_item(player, old_shield)) {
        player->inventory[inv_slot] = item;
        player->equipment[target] = old_target;
        if (clear_shield) player->equipment[EQUIP_SHIELD] = old_shield;
        return;
    }
    rc_recalc_bonuses(player);
}

void rc_player_unequip(RcWorld *world, int equip_slot) {
    if (!inventory_enabled(world) || !equipment_enabled(world)
            || !rc_player_action_allowed(world->enabled,
                                         RC_PLAYER_ACTION_UNEQUIP)
            || !valid_equip_slot(equip_slot)) {
        return;
    }
    RcPlayer *player = &world->player;
    RcInvSlot item = player->equipment[equip_slot];
    if (item.item_id < 0 || item.quantity <= 0) return;
    if (rc_inv_add(player->inventory, item.item_id, item.quantity) < 0) {
        return;
    }
    player->equipment[equip_slot].item_id = -1;
    player->equipment[equip_slot].quantity = 0;
    rc_recalc_bonuses(player);
}

enum {
    RC_GROUND_ITEM_NO_OWNER = -1,
    RC_GROUND_ITEM_LOCAL_OWNER = 0,
    RC_GROUND_ITEM_REVEAL_TICKS = 100,
    RC_GROUND_ITEM_DESPAWN_TICKS = 300,
    RC_GROUND_ITEM_MAX_PER_TILE = 128,
};

static int ground_item_slot(RcWorld *world) {
    for (int i = 0; i < world->ground_item_count; i++)
        if (!world->ground_items[i].active) return i;
    if (world->ground_item_count >= RC_MAX_GROUND_ITEMS) return -1;
    return world->ground_item_count++;
}

static int ground_item_next_uid(RcWorld *world) {
    if (world->next_ground_item_uid <= 0) world->next_ground_item_uid = 1;
    return world->next_ground_item_uid++;
}

static int ground_item_visible_to_local(const RcGroundItem *g) {
    return g->visibility == RC_GROUND_VIS_PUBLIC
        || g->owner_uid == RC_GROUND_ITEM_LOCAL_OWNER;
}

static int inventory_can_add(const RcInvSlot *inv, int item_id,
                             int quantity) {
    if (!inv || item_id < 0 || quantity <= 0) return 0;
    RcInvSlot tmp[RC_INVENTORY_SIZE];
    memcpy(tmp, inv, sizeof(tmp));
    return rc_inv_add(tmp, item_id, quantity) >= 0;
}

static int ground_item_count_at(const RcWorld *world, int x, int y,
                                int plane) {
    int count = 0;
    for (int i = 0; i < world->ground_item_count; i++) {
        const RcGroundItem *g = &world->ground_items[i];
        if (g->active && g->x == x && g->y == y && g->plane == plane)
            count++;
    }
    return count;
}

static int ground_item_free_slots(const RcWorld *world) {
    int free_slots = RC_MAX_GROUND_ITEMS - world->ground_item_count;
    for (int i = 0; i < world->ground_item_count; i++)
        if (!world->ground_items[i].active) free_slots++;
    return free_slots;
}

static int ground_item_is_stackable(int item_id, int quantity) {
    const RcItemDef *def = rc_item_def_get(item_id);
    return def ? def->stackable : quantity > 1;
}

static int ground_item_is_tradeable(int item_id) {
    const RcItemDef *def = rc_item_def_get(item_id);
    return !def || def->tradeable;
}

static int spawn_ground_item_one(RcWorld *world, int item_id, int quantity,
                                 int x, int y, int plane, int owner_uid,
                                 int original_owner_uid, int visibility,
                                 int reveal_timer, int despawn_timer,
                                 int stackable) {
    if (!world || item_id < 0 || quantity <= 0) return -1;
    if (stackable) {
        for (int i = 0; i < world->ground_item_count; i++) {
            RcGroundItem *g = &world->ground_items[i];
            if (!g->active || g->item_id != item_id || g->x != x
                    || g->y != y || g->plane != plane
                    || g->visibility != visibility
                    || g->owner_uid != owner_uid) {
                continue;
            }
            if (g->quantity > INT_MAX - quantity) return -1;
            g->quantity += quantity;
            g->despawn_timer = despawn_timer;
            g->reveal_timer = reveal_timer;
            g->version++;
            return i;
        }
    }

    if (ground_item_count_at(world, x, y, plane) >= RC_GROUND_ITEM_MAX_PER_TILE)
        return -1;
    int idx = ground_item_slot(world);
    if (idx < 0) return -1;
    int version = world->ground_items[idx].version + 1;
    if (version <= 0) version = 1;
    world->ground_items[idx] = (RcGroundItem){
        .uid = ground_item_next_uid(world),
        .version = version,
        .item_id = item_id,
        .quantity = quantity,
        .x = x,
        .y = y,
        .plane = plane,
        .owner_uid = owner_uid,
        .original_owner_uid = original_owner_uid,
        .reveal_timer = reveal_timer,
        .despawn_timer = despawn_timer,
        .visibility = (uint8_t)visibility,
        .active = true,
    };
    return idx;
}

static int spawn_ground_item_quantity(RcWorld *world, int item_id,
                                      int quantity, int x, int y, int plane,
                                      int owner_uid, int original_owner_uid,
                                      int visibility, int reveal_timer,
                                      int despawn_timer) {
    if (!world || item_id < 0 || quantity <= 0) return 0;
    int stackable = ground_item_is_stackable(item_id, quantity);
    if (stackable) {
        return spawn_ground_item_one(world, item_id, quantity, x, y, plane,
                                     owner_uid, original_owner_uid, visibility,
                                     reveal_timer, despawn_timer, 1) >= 0;
    }
    if (quantity > ground_item_free_slots(world)) return 0;
    if (ground_item_count_at(world, x, y, plane) + quantity
            > RC_GROUND_ITEM_MAX_PER_TILE) {
        return 0;
    }
    for (int i = 0; i < quantity; i++) {
        if (spawn_ground_item_one(world, item_id, 1, x, y, plane, owner_uid,
                                  original_owner_uid, visibility,
                                  reveal_timer, despawn_timer, 0) < 0) {
            return 0;
        }
    }
    return 1;
}

int rc_ground_item_spawn(RcWorld *world, int item_id, int quantity,
                         int x, int y, int plane, int owner_uid) {
    if (!loot_enabled(world)) return 0;
    int tradeable = ground_item_is_tradeable(item_id);
    int visibility = RC_GROUND_VIS_PUBLIC;
    int reveal_timer = 0;
    if (owner_uid != RC_GROUND_OWNER_NONE) {
        visibility = tradeable ? RC_GROUND_VIS_PRIVATE
                               : RC_GROUND_VIS_PRIVATE_PERMANENT;
        reveal_timer = tradeable ? RC_GROUND_ITEM_REVEAL_TICKS : 0;
    }
    return spawn_ground_item_quantity(world, item_id, quantity, x, y, plane,
                                      owner_uid, owner_uid, visibility,
                                      reveal_timer,
                                      RC_GROUND_ITEM_DESPAWN_TICKS);
}

void rc_player_drop_item(RcWorld *world, int inv_slot) {
    if (!inventory_enabled(world) || !loot_enabled(world)
            || !rc_player_action_allowed(world->enabled,
                                         RC_PLAYER_ACTION_DROP_ITEM)
            || !valid_inv_slot(inv_slot)) {
        return;
    }
    RcPlayer *player = &world->player;
    RcInvSlot item = player->inventory[inv_slot];
    if (item.item_id < 0 || item.quantity <= 0) return;
    int tradeable = ground_item_is_tradeable(item.item_id);
    int visibility = tradeable ? RC_GROUND_VIS_PRIVATE
                               : RC_GROUND_VIS_PRIVATE_PERMANENT;
    int reveal_timer = tradeable ? RC_GROUND_ITEM_REVEAL_TICKS : 0;
    if (!spawn_ground_item_quantity(world, item.item_id, item.quantity,
                                    player->x, player->y, player->plane,
                                    RC_GROUND_ITEM_LOCAL_OWNER,
                                    RC_GROUND_ITEM_LOCAL_OWNER,
                                    visibility, reveal_timer,
                                    RC_GROUND_ITEM_DESPAWN_TICKS)) {
        return;
    }
    rc_inv_remove(player->inventory, inv_slot);
    rc_recalc_bonuses(player);
    RcPayloadItemEvent payload = {
        .item_id = (uint32_t)item.item_id,
        .quantity = (uint16_t)(item.quantity > 65535 ? 65535
                                                     : item.quantity),
        .slot = (uint8_t)inv_slot,
    };
    rc_event_fire(world, RC_EVT_ITEM_DROPPED, &payload);
}

int rc_player_take_ground_item(RcWorld *world, int ground_item_idx,
                               int expected_uid, int expected_version) {
    if (!inventory_enabled(world) || !loot_enabled(world)
            || !rc_player_action_allowed(world->enabled,
                                         RC_PLAYER_ACTION_PICKUP_ITEM)
            || ground_item_idx < 0
            || ground_item_idx >= world->ground_item_count) {
        return RC_GROUND_TAKE_INVALID;
    }
    RcGroundItem *g = &world->ground_items[ground_item_idx];
    RcPlayer *player = &world->player;
    if (!g->active || g->item_id < 0 || g->quantity <= 0)
        return RC_GROUND_TAKE_INVALID;
    if (!ground_item_visible_to_local(g)) return RC_GROUND_TAKE_INVALID;
    if ((expected_uid >= 0 && g->uid != expected_uid) ||
            (expected_version >= 0 && g->version != expected_version)) {
        return RC_GROUND_TAKE_STALE;
    }
    if (g->x != player->x || g->y != player->y || g->plane != player->plane)
        return RC_GROUND_TAKE_INVALID;
    if (!inventory_can_add(player->inventory, g->item_id, g->quantity))
        return RC_GROUND_TAKE_FULL;

    int item_id = g->item_id;
    int quantity = g->quantity;
    g->active = false;
    g->quantity = 0;
    g->version++;
    int slot = rc_inv_add(player->inventory, item_id, quantity);
    if (slot < 0) {
        g->active = true;
        g->quantity = quantity;
        g->version++;
        return RC_GROUND_TAKE_FULL;
    }
    rc_recalc_bonuses(player);
    RcPayloadItemEvent payload = {
        .item_id = (uint32_t)item_id,
        .quantity = (uint16_t)(quantity > 65535 ? 65535 : quantity),
        .slot = (uint8_t)slot,
    };
    rc_event_fire(world, RC_EVT_ITEM_PICKED_UP, &payload);
    return RC_GROUND_TAKE_OK;
}

void rc_player_pickup_item(RcWorld *world, int ground_item_idx) {
    if (!inventory_enabled(world) || !loot_enabled(world)
            || !rc_player_action_allowed(world->enabled,
                                         RC_PLAYER_ACTION_PICKUP_ITEM)
            || ground_item_idx < 0
            || ground_item_idx >= world->ground_item_count) {
        return;
    }
    RcGroundItem *g = &world->ground_items[ground_item_idx];
    RcPlayer *player = &world->player;
    if (!g->active || g->item_id < 0 || g->quantity <= 0) return;
    if (!ground_item_visible_to_local(g)) return;
    if (g->plane != player->plane) return;
    if (g->x != player->x || g->y != player->y
            || g->plane != player->plane) {
        RcInteractionTarget target = {0};
        target.kind = RC_INTERACTION_GROUND_ITEM;
        target.entity_uid = g->uid;
        target.entity_generation = g->version;
        target.definition_id = g->item_id;
        target.content_group = -1;
        target.tile_x = g->x;
        target.tile_y = g->y;
        target.plane = g->plane;
        target.footprint_width = 1;
        target.footprint_height = 1;
        target.inventory_slot = -1;
        target.equipment_slot = -1;
        target.widget_id = -1;
        target.component_id = -1;
        target.ground_item_instance = ground_item_idx;
        rc_interaction_begin(player, 0, RC_INTERACTION_OP1, "Take",
                             &target, 0);
        return;
    }
    rc_player_take_ground_item(world, ground_item_idx, g->uid, g->version);
}
