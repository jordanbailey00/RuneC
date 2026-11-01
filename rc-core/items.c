#include "items.h"
#include "assets.h"
#include "combat.h"
#include "config.h"
#include "events.h"
#include "interaction.h"
#include "player_actions.h"
#include "player_command.h"
#include "pathfinding.h"
#include "spawn_index.h"
#include "skills.h"
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

RcItemDef g_item_defs[RC_MAX_ITEM_DEFS];
int g_item_def_count = 0;

static const RcItemDef *g_active_item_defs = g_item_defs;
static int g_active_item_def_count = 0;

enum {
    IDEF_MAGIC = 0x49444546,
    IDEF_V1 = 1,
    IDEF_V2 = 2,
    IDEF_V3 = 3,
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

enum {
    GSPI_MAGIC = 0x49505347,
    GSPI_RECORD_SIZE = 26,
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

static int read_i8(const unsigned char **p, const unsigned char *end,
                   int8_t *out) {
    uint8_t raw;
    if (!read_u8(p, end, &raw)) return 0;
    *out = raw == 0xFF ? -1 : (int8_t)raw;
    return 1;
}

static int read_pstr8(const unsigned char **p, const unsigned char *end,
                      char *out, size_t out_size) {
    uint8_t len;
    if (!read_u8(p, end, &len) || *p + len > end || out_size == 0) return 0;
    size_t copy = len < out_size - 1 ? len : out_size - 1;
    memcpy(out, *p, copy);
    out[copy] = '\0';
    *p += len;
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

static int read_i32(const unsigned char **p, const unsigned char *end,
                    int *out) {
    uint32_t raw;
    if (!read_u32(p, end, &raw)) return 0;
    *out = (int)(int32_t)raw;
    return 1;
}

static int id_or_missing(uint32_t value) {
    return value == UINT32_MAX ? -1 : (int)value;
}

void rc_item_use_defs(const RcItemDef *defs, int count) {
    g_active_item_defs = defs ? defs : g_item_defs;
    g_active_item_def_count = defs ? count : 0;
}

void rc_item_reset_defs_if_active(const RcItemDef *defs) {
    if (defs && g_active_item_defs == defs) {
        rc_item_use_defs(g_item_defs, g_item_def_count);
    }
}

static int parse_equipment(RcItemDef *def, const unsigned char **p,
                           const unsigned char *end, uint32_t version) {
    uint8_t slot, req_count;
    if (!read_u8(p, end, &slot) || !read_u8(p, end, &req_count)) return 0;

    def->equip_slot = slot == 0xFF ? -1 : (int)slot;
    if (req_count > RC_ITEM_MAX_REQUIREMENTS) return 0;
    if (version >= IDEF_V3) {
        uint8_t combat_level;
        if (!read_u8(p, end, &combat_level)) return 0;
        def->required_combat_level = combat_level;
    }
    def->req_count = req_count;
    for (int i = 0; i < req_count; i++) {
        uint8_t skill, level;
        if (!read_u8(p, end, &skill) || !read_u8(p, end, &level)) return 0;
        if (skill >= SKILL_COUNT || level == 0) return 0;
        def->req_skill[i] = skill;
        def->req_level[i] = level;
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
                        const unsigned char *end, uint32_t version) {
    uint8_t attack_speed, weapon_type, stance_bits, stance_count;
    if (!read_u8(p, end, &attack_speed) || !read_u8(p, end, &weapon_type)
            || !read_u8(p, end, &stance_bits)
            || !read_u8(p, end, &stance_count)) {
        return 0;
    }
    if (attack_speed == 0 || weapon_type >= RC_WEAPON_TYPE_LIMIT) return 0;
    def->attack_speed = attack_speed;
    def->attack_range = -1;
    def->weapon_type = weapon_type;
    def->stance_bits = stance_bits;
    if (version >= IDEF_V3) {
        int8_t attack_range;
        if (!read_i8(p, end, &attack_range)) return 0;
        def->attack_range = attack_range;
    }
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
    def->wearpos1 = -1;
    def->wearpos2 = -1;
    def->wearpos3 = -1;
    def->attack_range = -1;

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

    if (version >= IDEF_V3) {
        if (!read_i32(&p, end, &def->weight_grams)) return 0;
    } else {
        uint16_t weight_grams;
        if (!read_u16(&p, end, &weight_grams)) return 0;
        def->weight_grams = weight_grams;
    }
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

        int has_player_wear_model = 0;
        for (int i = 0; i < 7; i++) {
            if (!read_u32(&p, end, &tmp)) return 0;
            if (i > 0 && id_or_missing(tmp) >= 0)
                has_player_wear_model = 1;
        }
        if (has_player_wear_model)
            def->equipable_by_player = true;
    }

    if (version >= IDEF_V3) {
        if (!read_u16(&p, end, &def->metadata_flags)
                || !read_i8(&p, end, &def->wearpos1)
                || !read_i8(&p, end, &def->wearpos2)
                || !read_i8(&p, end, &def->wearpos3)
                || !read_u16(&p, end, &def->equip_conflicts)) {
            return 0;
        }
        if (def->equip_conflicts & ~((1u << RC_EQUIP_COUNT) - 1u)) return 0;
        for (int i = 0; i < RC_ITEM_ACTION_COUNT; i++) {
            if (!read_pstr8(&p, end, def->inventory_actions[i],
                            sizeof(def->inventory_actions[i]))) return 0;
        }
        for (int i = 0; i < RC_ITEM_ACTION_COUNT; i++) {
            if (!read_pstr8(&p, end, def->ground_actions[i],
                            sizeof(def->ground_actions[i]))) return 0;
        }
        if (!read_pstr8(&p, end, def->examine, sizeof(def->examine))) return 0;
    }

    def->stackable = (flags & IDEF_STACKABLE) != 0;
    def->tradeable = (flags & IDEF_TRADEABLE) != 0;
    def->members = (flags & IDEF_MEMBERS) != 0;
    def->quest_item = (flags & IDEF_QUEST_ITEM) != 0;
    def->noted = (flags & IDEF_NOTED) != 0;
    def->noteable = (flags & IDEF_NOTEABLE) != 0;
    def->placeholder = (flags & IDEF_PLACEHOLDER) != 0;
    def->equippable = (flags & IDEF_HAS_EQUIPMENT) != 0;
    if (flags & IDEF_EQUIP_PLAYER)
        def->equipable_by_player = true;
    def->equipable_weapon = (flags & IDEF_EQUIP_WEAPON) != 0;

    if ((flags & IDEF_HAS_EQUIPMENT)
            && !parse_equipment(def, &p, end, version)) {
        return 0;
    }
    if ((flags & IDEF_HAS_WEAPON) && !parse_weapon(def, &p, end, version)) {
        return 0;
    }
    def->loaded = true;
    return 1;
}

int rc_load_item_defs_into(const char *path, RcItemDef *defs, int max_defs,
                           int *out_count) {
    if (out_count) *out_count = 0;
    if (!path || !defs || max_defs <= 0) return -1;
    FILE *f = rc_asset_fopen(path, "rb");
    if (!f) return -1;

    uint32_t magic, version, count;
    if (fread(&magic, sizeof(magic), 1, f) != 1
            || fread(&version, sizeof(version), 1, f) != 1
            || fread(&count, sizeof(count), 1, f) != 1) {
        rc_asset_close(f);
        return -1;
    }
    if (magic != IDEF_MAGIC || (version != IDEF_V1 && version != IDEF_V2
            && version != IDEF_V3)) {
        rc_asset_close(f);
        return -1;
    }

    memset(defs, 0, (size_t)max_defs * sizeof(*defs));
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

        if (def.id >= 0 && def.id < max_defs) {
            defs[def.id] = def;
            loaded++;
        }
    }

    rc_asset_close(f);
    if (out_count) *out_count = loaded;
    return loaded;
}

int rc_load_item_defs(const char *path) {
    int loaded = 0;
    int result = rc_load_item_defs_into(path, g_item_defs, RC_MAX_ITEM_DEFS,
                                        &loaded);
    if (result < 0) return -1;
    g_item_def_count = loaded;
    rc_item_use_defs(g_item_defs, g_item_def_count);
    return result;
}

const RcItemDef *rc_item_def_get(int item_id) {
    if (item_id < 0 || item_id >= RC_MAX_ITEM_DEFS) return NULL;
    const RcItemDef *defs = g_active_item_defs ? g_active_item_defs
                                               : g_item_defs;
    int count = defs == g_item_defs ? g_item_def_count
                                    : g_active_item_def_count;
    const RcItemDef *def = &defs[item_id];
    if (def->loaded) return def;
    if (item_id < count && def->id == item_id) return def;
    return NULL;
}

static RcItemActionResult item_result(RcItemResultCode code, int item_id,
                                      int slot) {
    return (RcItemActionResult){
        .code = code,
        .item_id = item_id,
        .slot = slot,
        .required_skill = -1,
        .required_level = 0,
    };
}

static RcItemActionResult record_item_result(RcWorld *world,
                                             RcItemActionResult result) {
    if (world) world->player.last_item_action = result;
    return result;
}

int rc_item_result_accepted(RcItemActionResult result) {
    return result.code == RC_ITEM_RESULT_OK
        || result.code == RC_ITEM_RESULT_QUEUED;
}

const char *rc_item_result_name(RcItemResultCode code) {
    switch (code) {
    case RC_ITEM_RESULT_INVALID: return "invalid";
    case RC_ITEM_RESULT_OK: return "ok";
    case RC_ITEM_RESULT_QUEUED: return "queued";
    case RC_ITEM_RESULT_DISABLED: return "disabled";
    case RC_ITEM_RESULT_ACTION_DENIED: return "action-denied";
    case RC_ITEM_RESULT_EMPTY: return "empty";
    case RC_ITEM_RESULT_STALE: return "stale";
    case RC_ITEM_RESULT_NO_DEFINITION: return "no-definition";
    case RC_ITEM_RESULT_NOT_EQUIPPABLE: return "not-equippable";
    case RC_ITEM_RESULT_REQUIREMENT: return "requirement";
    case RC_ITEM_RESULT_CAPACITY: return "capacity";
    case RC_ITEM_RESULT_STACK_LIMIT: return "stack-limit";
    case RC_ITEM_RESULT_STATE_MISMATCH: return "state-mismatch";
    case RC_ITEM_RESULT_CONFLICT: return "conflict";
    }
    return "unknown";
}

static int slots_add(RcInvSlot *slots, int count, int item_id, int quantity,
                     uint32_t state_id, RcItemResultCode *failure) {
    if (failure) *failure = RC_ITEM_RESULT_INVALID;
    if (!slots || count <= 0 || item_id < 0 || quantity <= 0) return -1;
    const RcItemDef *def = rc_item_def_get(item_id);
    if (!def) {
        if (failure) *failure = RC_ITEM_RESULT_NO_DEFINITION;
        return -1;
    }
    int first_slot = -1;
    if (def->stackable) {
        for (int i = 0; i < count; i++) {
            if (slots[i].item_id != item_id || slots[i].state_id != state_id)
                continue;
            if (slots[i].quantity > INT_MAX - quantity) {
                if (failure) *failure = RC_ITEM_RESULT_STACK_LIMIT;
                return -1;
            }
            slots[i].quantity += quantity;
            if (failure) *failure = RC_ITEM_RESULT_OK;
            return i;
        }
        for (int i = 0; i < count; i++) {
            if (slots[i].item_id >= 0) continue;
            slots[i] = (RcInvSlot){item_id, quantity, state_id, 0};
            if (failure) *failure = RC_ITEM_RESULT_OK;
            return i;
        }
        if (failure) *failure = RC_ITEM_RESULT_CAPACITY;
        return -1;
    }

    int free_count = 0;
    for (int i = 0; i < count; i++) free_count += slots[i].item_id < 0;
    if (free_count < quantity) {
        if (failure) *failure = RC_ITEM_RESULT_CAPACITY;
        return -1;
    }
    for (int i = 0; i < count && quantity > 0; i++) {
        if (slots[i].item_id >= 0) continue;
        if (first_slot < 0) first_slot = i;
        slots[i] = (RcInvSlot){item_id, 1, state_id, 0};
        quantity--;
    }
    if (failure) *failure = RC_ITEM_RESULT_OK;
    return first_slot;
}

int rc_inv_add_state(RcInvSlot *inv, int item_id, int quantity,
                     uint32_t state_id) {
    return slots_add(inv, RC_INVENTORY_SIZE, item_id, quantity, state_id, NULL);
}

int rc_inv_add(RcInvSlot *inv, int item_id, int quantity) {
    return rc_inv_add_state(inv, item_id, quantity, 0);
}

int rc_inv_remove_quantity(RcInvSlot *inv, int slot, int quantity) {
    if (!inv || slot < 0 || slot >= RC_INVENTORY_SIZE || quantity <= 0) {
        return 0;
    }
    if (inv[slot].item_id < 0 || inv[slot].quantity <= 0) return 0;
    int removed = quantity < inv[slot].quantity ? quantity : inv[slot].quantity;
    inv[slot].quantity -= removed;
    if (inv[slot].quantity <= 0) {
        inv[slot] = (RcInvSlot){.item_id = -1};
    }
    return removed;
}

void rc_inv_remove(RcInvSlot *inv, int slot) {
    if (!inv || slot < 0 || slot >= RC_INVENTORY_SIZE) return;
    inv[slot] = (RcInvSlot){.item_id = -1};
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
    int64_t weight = 0;
    for (int i = 0; i < RC_INVENTORY_SIZE; i++) {
        int id = player->inventory[i].item_id;
        int qty = player->inventory[i].quantity;
        const RcItemDef *def = rc_item_def_get(id);
        if (def && qty > 0) weight += (int64_t)def->weight_grams * qty;
    }
    for (int i = 0; i < RC_EQUIP_COUNT; i++) {
        int id = player->equipment[i].item_id;
        int qty = player->equipment[i].quantity;
        const RcItemDef *def = rc_item_def_get(id);
        if (!def) continue;
        if (qty > 0) weight += (int64_t)def->weight_grams * qty;
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
    player->weight = weight > INT_MAX ? INT_MAX
                   : weight < INT_MIN ? INT_MIN : (int)weight;
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

static RcItemActionResult requirements_result(const RcPlayer *player,
                                              const RcItemDef *def) {
    if (def->required_combat_level > 0
            && rc_combat_level(&player->skills) < def->required_combat_level) {
        RcItemActionResult result = item_result(
            RC_ITEM_RESULT_REQUIREMENT, def->id, -1);
        result.required_skill = -1;
        result.required_level = def->required_combat_level;
        return result;
    }
    for (int i = 0; i < def->req_count; i++) {
        int skill = def->req_skill[i];
        if (skill < 0 || skill >= SKILL_COUNT)
            return item_result(RC_ITEM_RESULT_NO_DEFINITION, def->id, -1);
        if (player->skills.base_level[skill] < def->req_level[i]) {
            RcItemActionResult result = item_result(
                RC_ITEM_RESULT_REQUIREMENT, def->id, -1);
            result.required_skill = skill;
            result.required_level = def->req_level[i];
            return result;
        }
    }
    return item_result(RC_ITEM_RESULT_OK, def->id, -1);
}

static int item_player_equippable(const RcItemDef *def) {
    return def && def->equippable && valid_equip_slot(def->equip_slot)
        && def->equipable_by_player;
}

static int slot_same(const RcInvSlot *a, const RcInvSlot *b) {
    return a->item_id == b->item_id && a->quantity == b->quantity
        && a->state_id == b->state_id;
}

static int slots_valid(const RcInvSlot *slots, int count) {
    for (int i = 0; i < count; i++) {
        if (slots[i].item_id < 0) {
            if (slots[i].quantity != 0 || slots[i].state_id != 0) return 0;
            continue;
        }
        const RcItemDef *def = rc_item_def_get(slots[i].item_id);
        if (!def || slots[i].quantity <= 0) return 0;
        if (!def->stackable && slots[i].quantity != 1) return 0;
    }
    return 1;
}

static uint32_t next_item_generation(RcPlayer *player) {
    player->next_item_generation++;
    if (player->next_item_generation == 0) player->next_item_generation++;
    return player->next_item_generation;
}

RcItemActionResult rc_item_tx_begin(RcItemTransaction *tx, RcWorld *world) {
    if (!tx || !world) return item_result(RC_ITEM_RESULT_INVALID, -1, -1);
    memset(tx, 0, sizeof(*tx));
    tx->world = world;
    memcpy(tx->inventory, world->player.inventory, sizeof(tx->inventory));
    memcpy(tx->equipment, world->player.equipment, sizeof(tx->equipment));
    memcpy(tx->rune_pouch, world->player.rune_pouch, sizeof(tx->rune_pouch));
    memcpy(tx->original_rune_pouch, tx->rune_pouch, sizeof(tx->rune_pouch));
    tx->inventory_revision = world->player.inventory_revision;
    tx->equipment_revision = world->player.equipment_revision;
    tx->valid = true;
    return item_result(RC_ITEM_RESULT_OK, -1, -1);
}

static RcItemActionResult tx_fail(RcItemTransaction *tx,
                                  RcItemResultCode code, int item_id,
                                  int slot) {
    if (tx) tx->valid = false;
    return item_result(code, item_id, slot);
}

RcItemActionResult rc_item_tx_add(RcItemTransaction *tx, int item_id,
                                  int quantity, uint32_t state_id) {
    if (!tx || !tx->valid)
        return tx_fail(tx, RC_ITEM_RESULT_CONFLICT, item_id, -1);
    RcItemResultCode failure;
    int slot = slots_add(tx->inventory, RC_INVENTORY_SIZE, item_id, quantity,
                         state_id, &failure);
    if (slot < 0) return tx_fail(tx, failure, item_id, -1);
    tx->inventory_touched |= 1u << slot;
    return item_result(RC_ITEM_RESULT_OK, item_id, slot);
}

RcItemActionResult rc_item_tx_remove_slot(RcItemTransaction *tx, int slot,
                                          int quantity,
                                          uint32_t expected_generation) {
    if (!tx || !tx->valid || !valid_inv_slot(slot) || quantity <= 0)
        return tx_fail(tx, RC_ITEM_RESULT_INVALID, -1, slot);
    RcInvSlot *item = &tx->inventory[slot];
    if (item->item_id < 0 || item->quantity < quantity)
        return tx_fail(tx, RC_ITEM_RESULT_EMPTY, item->item_id, slot);
    if (expected_generation != UINT32_MAX
            && item->generation != expected_generation) {
        return tx_fail(tx, RC_ITEM_RESULT_STALE, item->item_id, slot);
    }
    int item_id = item->item_id;
    tx->inventory_touched |= 1u << slot;
    item->quantity -= quantity;
    if (item->quantity == 0) *item = (RcInvSlot){.item_id = -1};
    return item_result(RC_ITEM_RESULT_OK, item_id, slot);
}

RcItemActionResult rc_item_tx_remove_item(RcItemTransaction *tx, int item_id,
                                          int quantity) {
    if (!tx || !tx->valid || item_id < 0 || quantity <= 0)
        return tx_fail(tx, RC_ITEM_RESULT_INVALID, item_id, -1);
    int available = 0;
    for (int i = 0; i < RC_INVENTORY_SIZE; i++) {
        if (tx->inventory[i].item_id != item_id
                || tx->inventory[i].state_id != 0) continue;
        if (available > INT_MAX - tx->inventory[i].quantity)
            available = INT_MAX;
        else
            available += tx->inventory[i].quantity;
    }
    if (available < quantity)
        return tx_fail(tx, RC_ITEM_RESULT_EMPTY, item_id, -1);
    int left = quantity;
    for (int i = 0; i < RC_INVENTORY_SIZE && left > 0; i++) {
        RcInvSlot *slot = &tx->inventory[i];
        if (slot->item_id != item_id || slot->state_id != 0) continue;
        int remove = slot->quantity < left ? slot->quantity : left;
        tx->inventory_touched |= 1u << i;
        slot->quantity -= remove;
        left -= remove;
        if (slot->quantity == 0) *slot = (RcInvSlot){.item_id = -1};
    }
    return item_result(RC_ITEM_RESULT_OK, item_id, -1);
}

RcItemActionResult rc_item_tx_move_slot(RcItemTransaction *tx, int from_slot,
                                        int to_slot, int quantity,
                                        uint32_t expected_generation) {
    if (!tx || !tx->valid || !valid_inv_slot(from_slot)
            || !valid_inv_slot(to_slot) || from_slot == to_slot
            || quantity <= 0) {
        return tx_fail(tx, RC_ITEM_RESULT_INVALID, -1, from_slot);
    }
    RcInvSlot *source = &tx->inventory[from_slot];
    RcInvSlot *target = &tx->inventory[to_slot];
    if (source->item_id < 0 || source->quantity < quantity)
        return tx_fail(tx, RC_ITEM_RESULT_EMPTY, source->item_id, from_slot);
    if (expected_generation != UINT32_MAX
            && source->generation != expected_generation) {
        return tx_fail(tx, RC_ITEM_RESULT_STALE, source->item_id, from_slot);
    }
    const RcItemDef *def = rc_item_def_get(source->item_id);
    if (!def)
        return tx_fail(tx, RC_ITEM_RESULT_NO_DEFINITION,
                       source->item_id, from_slot);
    if (!def->stackable && quantity != 1)
        return tx_fail(tx, RC_ITEM_RESULT_INVALID,
                       source->item_id, from_slot);

    int item_id = source->item_id;
    if (target->item_id < 0) {
        *target = (RcInvSlot){
            source->item_id, quantity, source->state_id, 0
        };
        source->quantity -= quantity;
        if (source->quantity == 0)
            *source = (RcInvSlot){.item_id = -1};
    } else if (def->stackable && target->item_id == source->item_id
            && target->state_id == source->state_id) {
        if (target->quantity > INT_MAX - quantity)
            return tx_fail(tx, RC_ITEM_RESULT_STACK_LIMIT,
                           item_id, to_slot);
        target->quantity += quantity;
        source->quantity -= quantity;
        if (source->quantity == 0)
            *source = (RcInvSlot){.item_id = -1};
    } else if (quantity == source->quantity) {
        RcInvSlot swap = *target;
        *target = *source;
        *source = swap;
    } else {
        RcItemResultCode code = target->item_id == source->item_id
                              ? RC_ITEM_RESULT_STATE_MISMATCH
                              : RC_ITEM_RESULT_CONFLICT;
        return tx_fail(tx, code, item_id, to_slot);
    }
    tx->inventory_touched |= (1u << from_slot) | (1u << to_slot);
    return item_result(RC_ITEM_RESULT_OK, item_id, to_slot);
}

RcItemActionResult rc_item_tx_remove_equipment(RcItemTransaction *tx,
                                               int slot, int quantity,
                                               uint32_t expected_generation) {
    if (!tx || !tx->valid || !valid_equip_slot(slot) || quantity <= 0)
        return tx_fail(tx, RC_ITEM_RESULT_INVALID, -1, slot);
    RcInvSlot *item = &tx->equipment[slot];
    if (item->item_id < 0 || item->quantity < quantity)
        return tx_fail(tx, RC_ITEM_RESULT_EMPTY, item->item_id, slot);
    if (expected_generation != UINT32_MAX
            && item->generation != expected_generation) {
        return tx_fail(tx, RC_ITEM_RESULT_STALE, item->item_id, slot);
    }
    int item_id = item->item_id;
    tx->equipment_touched |= (uint16_t)(1u << slot);
    item->quantity -= quantity;
    if (item->quantity == 0) *item = (RcInvSlot){.item_id = -1};
    return item_result(RC_ITEM_RESULT_OK, item_id, slot);
}

RcItemActionResult rc_item_tx_commit(RcItemTransaction *tx) {
    if (!tx || !tx->world || !tx->valid)
        return item_result(RC_ITEM_RESULT_CONFLICT, -1, -1);
    RcPlayer *player = &tx->world->player;
    if (player->inventory_revision != tx->inventory_revision
            || player->equipment_revision != tx->equipment_revision
            || memcmp(player->rune_pouch, tx->original_rune_pouch,
                      sizeof(tx->rune_pouch)) != 0) {
        tx->valid = false;
        return item_result(RC_ITEM_RESULT_STALE, -1, -1);
    }
    if (!slots_valid(tx->inventory, RC_INVENTORY_SIZE)
            || !slots_valid(tx->equipment, RC_EQUIP_COUNT)
            || (memcmp(tx->rune_pouch, tx->original_rune_pouch, sizeof(tx->rune_pouch))
                && !slots_valid(tx->rune_pouch, 4))) {
        tx->valid = false;
        return item_result(RC_ITEM_RESULT_INVALID, -1, -1);
    }

    int inventory_changed = 0;
    int equipment_changed = 0;
    for (int i = 0; i < RC_INVENTORY_SIZE; i++) {
        if (!(tx->inventory_touched & (1u << i))
                && slot_same(&player->inventory[i], &tx->inventory[i])) {
            tx->inventory[i].generation = player->inventory[i].generation;
            continue;
        }
        inventory_changed = 1;
        tx->inventory[i].generation = tx->inventory[i].item_id >= 0
                                    ? next_item_generation(player) : 0;
    }
    for (int i = 0; i < RC_EQUIP_COUNT; i++) {
        if (!(tx->equipment_touched & (1u << i))
                && slot_same(&player->equipment[i], &tx->equipment[i])) {
            tx->equipment[i].generation = player->equipment[i].generation;
            continue;
        }
        equipment_changed = 1;
        tx->equipment[i].generation = tx->equipment[i].item_id >= 0
                                    ? next_item_generation(player) : 0;
    }
    if (memcmp(tx->rune_pouch, tx->original_rune_pouch, sizeof(tx->rune_pouch))) {
        memcpy(player->rune_pouch, tx->rune_pouch, sizeof(tx->rune_pouch));
        inventory_changed = 1;
    }
    if (inventory_changed) {
        memcpy(player->inventory, tx->inventory, sizeof(tx->inventory));
        player->inventory_revision++;
        if (player->inventory_revision == 0) player->inventory_revision++;
    }
    if (equipment_changed) {
        memcpy(player->equipment, tx->equipment, sizeof(tx->equipment));
        player->equipment_revision++;
        if (player->equipment_revision == 0) player->equipment_revision++;
    }
    if (inventory_changed || equipment_changed) rc_recalc_bonuses(player);
    tx->valid = false;
    return item_result(RC_ITEM_RESULT_OK, -1, -1);
}

RcItemActionResult rc_player_inventory_add(RcWorld *world, int item_id,
                                           int quantity, uint32_t state_id) {
    if (!inventory_enabled(world))
        return record_item_result(world, item_result(
            RC_ITEM_RESULT_DISABLED, item_id, -1));
    RcItemTransaction tx;
    RcItemActionResult result = rc_item_tx_begin(&tx, world);
    if (result.code == RC_ITEM_RESULT_OK)
        result = rc_item_tx_add(&tx, item_id, quantity, state_id);
    if (result.code == RC_ITEM_RESULT_OK) result = rc_item_tx_commit(&tx);
    return record_item_result(world, result);
}

RcItemActionResult rc_player_inventory_remove_item(RcWorld *world, int item_id,
                                                   int quantity) {
    if (!inventory_enabled(world))
        return record_item_result(world, item_result(
            RC_ITEM_RESULT_DISABLED, item_id, -1));
    RcItemTransaction tx;
    RcItemActionResult result = rc_item_tx_begin(&tx, world);
    if (result.code == RC_ITEM_RESULT_OK)
        result = rc_item_tx_remove_item(&tx, item_id, quantity);
    if (result.code == RC_ITEM_RESULT_OK) result = rc_item_tx_commit(&tx);
    return record_item_result(world, result);
}

void rc_item_set_equip_requirement_hook(RcWorld *world,
                                        RcItemEquipRequirementHook hook,
                                        void *ctx) {
    if (!world) return;
    world->item_equip_requirement_hook = hook;
    world->item_equip_requirement_ctx = ctx;
}

RcItemActionResult rc_player_move_inventory_item_expected(
    RcWorld *world, int from_slot, int to_slot, uint32_t expected_generation) {
    if (!inventory_enabled(world))
        return record_item_result(world, item_result(
            RC_ITEM_RESULT_DISABLED, -1, from_slot));
    if (!valid_inv_slot(from_slot) || !valid_inv_slot(to_slot)
            || from_slot == to_slot) {
        return record_item_result(world, item_result(
            RC_ITEM_RESULT_INVALID, -1, from_slot));
    }
    RcItemTransaction tx;
    (void)rc_item_tx_begin(&tx, world);
    RcInvSlot source = tx.inventory[from_slot];
    if (source.item_id < 0 || source.quantity <= 0)
        return record_item_result(world, item_result(
            RC_ITEM_RESULT_EMPTY, source.item_id, from_slot));
    RcItemActionResult result = rc_item_tx_move_slot(
        &tx, from_slot, to_slot, source.quantity, expected_generation);
    if (result.code == RC_ITEM_RESULT_OK) result = rc_item_tx_commit(&tx);
    if (result.code == RC_ITEM_RESULT_OK) {
        result.item_id = source.item_id;
        result.slot = to_slot;
    }
    return record_item_result(world, result);
}

RcItemActionResult rc_player_move_inventory_item(RcWorld *world,
                                                 int from_slot, int to_slot) {
    if (rc_player_command_should_queue(world)) {
        uint32_t generation = valid_inv_slot(from_slot)
                            ? world->player.inventory[from_slot].generation : 0;
        int args[8] = {from_slot, to_slot, (int)generation, 0, 0, 0, 0, 0};
        RcItemResultCode code = rc_player_command_submit(
            world, RC_PLAYER_COMMAND_MOVE_INVENTORY,
            RC_ACTION_CATEGORY_BACKGROUND, args, 0)
            ? RC_ITEM_RESULT_QUEUED : RC_ITEM_RESULT_ACTION_DENIED;
        return record_item_result(world, item_result(code, -1, from_slot));
    }
    return rc_player_move_inventory_item_expected(
        world, from_slot, to_slot, UINT32_MAX);
}

static void fire_item_event(RcWorld *world, int event, RcInvSlot item,
                            int slot) {
    RcPayloadItemEvent payload = {
        .item_id = (uint32_t)item.item_id,
        .quantity = (uint32_t)item.quantity,
        .ground_uid = -1,
        .slot = (uint8_t)slot,
    };
    rc_event_fire(world, event, &payload);
}

RcItemActionResult rc_player_equip_expected(RcWorld *world, int inv_slot,
                                            uint32_t expected_generation) {
    if (!inventory_enabled(world) || !equipment_enabled(world))
        return record_item_result(world, item_result(
            RC_ITEM_RESULT_DISABLED, -1, inv_slot));
    if (!rc_player_action_allowed(world->enabled, RC_PLAYER_ACTION_EQUIP))
        return record_item_result(world, item_result(
            RC_ITEM_RESULT_ACTION_DENIED, -1, inv_slot));
    if (!valid_inv_slot(inv_slot))
        return record_item_result(world, item_result(
            RC_ITEM_RESULT_INVALID, -1, inv_slot));

    RcItemTransaction tx;
    (void)rc_item_tx_begin(&tx, world);
    RcInvSlot item = tx.inventory[inv_slot];
    if (item.item_id < 0 || item.quantity <= 0)
        return record_item_result(world, item_result(
            RC_ITEM_RESULT_EMPTY, item.item_id, inv_slot));
    if (expected_generation != UINT32_MAX
            && item.generation != expected_generation) {
        return record_item_result(world, item_result(
            RC_ITEM_RESULT_STALE, item.item_id, inv_slot));
    }
    const RcItemDef *def = rc_item_def_get(item.item_id);
    if (!def)
        return record_item_result(world, item_result(
            RC_ITEM_RESULT_NO_DEFINITION, item.item_id, inv_slot));
    if (!item_player_equippable(def))
        return record_item_result(world, item_result(
            RC_ITEM_RESULT_NOT_EQUIPPABLE, item.item_id, inv_slot));
    RcItemActionResult result = requirements_result(&world->player, def);
    if (result.code != RC_ITEM_RESULT_OK)
        return record_item_result(world, result);
    if (world->item_equip_requirement_hook) {
        result = world->item_equip_requirement_hook(
            world, &world->player, item.item_id,
            world->item_equip_requirement_ctx);
        if (result.code != RC_ITEM_RESULT_OK)
            return record_item_result(world, result);
    }

    int target_slot = def->equip_slot;
    RcInvSlot displaced[RC_EQUIP_COUNT];
    int displaced_slots[RC_EQUIP_COUNT];
    int displaced_count = 0;
    tx.inventory[inv_slot] = (RcInvSlot){.item_id = -1};
    tx.inventory_touched |= 1u << inv_slot;

    RcInvSlot *target = &tx.equipment[target_slot];
    const RcItemDef *target_def = rc_item_def_get(target->item_id);
    if (target->item_id == item.item_id && target_def
            && target_def->stackable && target->state_id == item.state_id) {
        if (target->quantity > INT_MAX - item.quantity)
            return record_item_result(world, item_result(
                RC_ITEM_RESULT_STACK_LIMIT, item.item_id, target_slot));
        target->quantity += item.quantity;
        tx.equipment_touched |= (uint16_t)(1u << target_slot);
    } else {
        for (int slot = 0; slot < RC_EQUIP_COUNT; slot++) {
            RcInvSlot worn = tx.equipment[slot];
            if (worn.item_id < 0) continue;
            const RcItemDef *worn_def = rc_item_def_get(worn.item_id);
            int conflicts = slot == target_slot
                || (def->equip_conflicts & (1u << slot)) != 0
                || (worn_def && (worn_def->equip_conflicts
                                  & (1u << target_slot)) != 0);
            if (!conflicts) continue;
            displaced[displaced_count] = worn;
            displaced_slots[displaced_count++] = slot;
            tx.equipment[slot] = (RcInvSlot){.item_id = -1};
            tx.equipment_touched |= (uint16_t)(1u << slot);
        }
        tx.equipment[target_slot] = item;
        tx.equipment_touched |= (uint16_t)(1u << target_slot);
        for (int i = 0; i < displaced_count; i++) {
            result = rc_item_tx_add(&tx, displaced[i].item_id,
                                    displaced[i].quantity,
                                    displaced[i].state_id);
            if (result.code != RC_ITEM_RESULT_OK)
                return record_item_result(world, result);
        }
    }

    result = rc_item_tx_commit(&tx);
    if (result.code == RC_ITEM_RESULT_OK) {
        for (int i = 0; i < displaced_count; i++)
            fire_item_event(world, RC_EVT_ITEM_UNEQUIPPED,
                            displaced[i], displaced_slots[i]);
        fire_item_event(world, RC_EVT_ITEM_EQUIPPED, item, target_slot);
    }
    result.item_id = item.item_id;
    result.slot = target_slot;
    return record_item_result(world, result);
}

RcItemActionResult rc_player_equip(RcWorld *world, int inv_slot) {
    if (rc_player_command_should_queue(world)) {
        uint32_t generation = valid_inv_slot(inv_slot)
                            ? world->player.inventory[inv_slot].generation : 0;
        int args[8] = {inv_slot, (int)generation, 0, 0, 0, 0, 0, 0};
        RcItemResultCode code = rc_player_command_submit(
            world, RC_PLAYER_COMMAND_EQUIP, RC_ACTION_CATEGORY_BACKGROUND,
            args, 0) ? RC_ITEM_RESULT_QUEUED : RC_ITEM_RESULT_ACTION_DENIED;
        return record_item_result(world, item_result(code, -1, inv_slot));
    }
    return rc_player_equip_expected(world, inv_slot, UINT32_MAX);
}

RcItemActionResult rc_player_unequip_expected(RcWorld *world, int equip_slot,
                                              uint32_t expected_generation) {
    if (!inventory_enabled(world) || !equipment_enabled(world))
        return record_item_result(world, item_result(
            RC_ITEM_RESULT_DISABLED, -1, equip_slot));
    if (!rc_player_action_allowed(world->enabled, RC_PLAYER_ACTION_UNEQUIP))
        return record_item_result(world, item_result(
            RC_ITEM_RESULT_ACTION_DENIED, -1, equip_slot));
    if (!valid_equip_slot(equip_slot))
        return record_item_result(world, item_result(
            RC_ITEM_RESULT_INVALID, -1, equip_slot));
    RcItemTransaction tx;
    (void)rc_item_tx_begin(&tx, world);
    RcInvSlot item = tx.equipment[equip_slot];
    if (item.item_id < 0 || item.quantity <= 0)
        return record_item_result(world, item_result(
            RC_ITEM_RESULT_EMPTY, item.item_id, equip_slot));
    if (expected_generation != UINT32_MAX
            && item.generation != expected_generation) {
        return record_item_result(world, item_result(
            RC_ITEM_RESULT_STALE, item.item_id, equip_slot));
    }
    tx.equipment[equip_slot] = (RcInvSlot){.item_id = -1};
    tx.equipment_touched |= (uint16_t)(1u << equip_slot);
    RcItemActionResult result = rc_item_tx_add(
        &tx, item.item_id, item.quantity, item.state_id);
    if (result.code == RC_ITEM_RESULT_OK) result = rc_item_tx_commit(&tx);
    if (result.code == RC_ITEM_RESULT_OK)
        fire_item_event(world, RC_EVT_ITEM_UNEQUIPPED, item, equip_slot);
    result.item_id = item.item_id;
    result.slot = equip_slot;
    return record_item_result(world, result);
}

RcItemActionResult rc_player_unequip(RcWorld *world, int equip_slot) {
    if (rc_player_command_should_queue(world)) {
        uint32_t generation = valid_equip_slot(equip_slot)
                            ? world->player.equipment[equip_slot].generation : 0;
        int args[8] = {equip_slot, (int)generation, 0, 0, 0, 0, 0, 0};
        RcItemResultCode code = rc_player_command_submit(
            world, RC_PLAYER_COMMAND_UNEQUIP, RC_ACTION_CATEGORY_BACKGROUND,
            args, 0) ? RC_ITEM_RESULT_QUEUED : RC_ITEM_RESULT_ACTION_DENIED;
        return record_item_result(world, item_result(code, -1, equip_slot));
    }
    return rc_player_unequip_expected(world, equip_slot, UINT32_MAX);
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
        if (!world->ground_items[i].active
                && !world->ground_items[i].static_spawn) {
            return i;
        }
    if (world->ground_item_count >= RC_MAX_GROUND_ITEMS) return -1;
    return world->ground_item_count++;
}

static int ground_item_next_uid(RcWorld *world) {
    if (world->next_ground_item_uid <= 0) world->next_ground_item_uid = 1;
    return world->next_ground_item_uid++;
}

int rc_ground_item_visible(const RcGroundItem *g, int owner_uid) {
    return g && g->active && g->arrival_timer == 0
        && (g->visibility == RC_GROUND_VIS_PUBLIC || g->owner_uid == owner_uid);
}

int rc_ground_item_reachable(const RcWorld *world, int x, int y, int plane) {
    if (!world || world->player.plane != plane) return 0;
    RcRouteTarget target = rc_route_target_rectangle(x, y, 1, 1, 0, 1, true, false);
    return rc_route_target_reached(&world->map, plane,
        world->player.x, world->player.y, 1, 1, &target);
}

static int inventory_can_add(const RcInvSlot *inv, int item_id,
                             int quantity, uint32_t state_id) {
    if (!inv || item_id < 0 || quantity <= 0) return 0;
    RcInvSlot tmp[RC_INVENTORY_SIZE];
    memcpy(tmp, inv, sizeof(tmp));
    return rc_inv_add_state(tmp, item_id, quantity, state_id) >= 0;
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
        if (!world->ground_items[i].active
                && !world->ground_items[i].static_spawn) {
            free_slots++;
        }
    return free_slots;
}

static int ground_item_is_stackable(int item_id, int quantity) {
    (void)quantity;
    const RcItemDef *def = rc_item_def_get(item_id);
    return def && def->stackable;
}

static int ground_item_is_tradeable(int item_id) {
    const RcItemDef *def = rc_item_def_get(item_id);
    return def && def->tradeable;
}

static uint64_t ground_spawn_instance_key(uint64_t base, uint32_t ordinal) {
    if (base == 0 || ordinal == 0) return base;
    uint64_t key = base ^ (UINT64_C(0x9e3779b97f4a7c15) + ordinal);
    key ^= key >> 30;
    key *= UINT64_C(0xbf58476d1ce4e5b9);
    key ^= key >> 27;
    key *= UINT64_C(0x94d049bb133111eb);
    key ^= key >> 31;
    return key ? key : 1;
}

static int spawn_ground_item_one(RcWorld *world, int item_id, int quantity,
                                 uint32_t state_id,
                                 int x, int y, int plane, int owner_uid,
                                 int original_owner_uid, int visibility,
                                 int reveal_timer, int despawn_timer,
                                 int stackable, int static_spawn,
                                 uint64_t spawn_key, int arrival_timer) {
    if (!world || item_id < 0 || quantity <= 0) return -1;
    if (stackable && !arrival_timer) {
        for (int i = 0; i < world->ground_item_count; i++) {
            RcGroundItem *g = &world->ground_items[i];
            if (!g->active || g->arrival_timer || g->item_id != item_id
                    || g->state_id != state_id || g->x != x
                    || g->y != y || g->plane != plane
                    || g->visibility != visibility
                    || g->owner_uid != owner_uid
                    || g->original_owner_uid != original_owner_uid
                    || g->initial_reveal_ticks != reveal_timer
                    || g->initial_lifetime_ticks != despawn_timer
                    || g->spawn_key != spawn_key
                    || g->static_spawn != (static_spawn ? true : false)) {
                continue;
            }
            if (g->quantity > INT_MAX - quantity) return -1;
            g->quantity += quantity;
            if (static_spawn) g->spawn_quantity += quantity;
            g->despawn_timer = despawn_timer;
            g->reveal_timer = reveal_timer;
            g->timer_start_tick = world->tick + (world->in_tick ? 1u : 0u);
            g->version++;
            return i;
        }
    }

    if (ground_item_count_at(world, x, y, plane) >= RC_GROUND_ITEM_MAX_PER_TILE)
        return -1;
    if (world->next_ground_item_uid == INT_MAX) return -1;
    int idx = ground_item_slot(world);
    if (idx < 0) return -1;
    int version = 1; // A new UID invalidates every reference to the reused slot.
    world->ground_items[idx] = (RcGroundItem){
        .uid = ground_item_next_uid(world),
        .version = version,
        .spawn_key = static_spawn ? spawn_key : 0,
        .item_id = item_id,
        .quantity = quantity,
        .state_id = state_id,
        .spawn_quantity = static_spawn ? quantity : 0,
        .x = x,
        .y = y,
        .plane = plane,
        .owner_uid = owner_uid,
        .original_owner_uid = original_owner_uid,
        .reveal_timer = reveal_timer,
        .despawn_timer = despawn_timer,
        .initial_reveal_ticks = reveal_timer,
        .initial_lifetime_ticks = despawn_timer,
        .arrival_timer = arrival_timer,
        .timer_start_tick = world->tick + (world->in_tick ? 1u : 0u),
        .visibility = (uint8_t)visibility,
        .static_spawn = static_spawn ? true : false,
        .active = true,
    };
    return idx;
}

static int spawn_ground_item_quantity(RcWorld *world, int item_id,
                                      int quantity, uint32_t state_id,
                                      int x, int y, int plane,
                                      int owner_uid, int original_owner_uid,
                                      int visibility, int reveal_timer,
                                      int despawn_timer, int static_spawn,
                                      uint64_t spawn_key, int arrival_timer) {
    if (!world || !rc_item_def_get(item_id) || quantity <= 0
            || !rc_world_tile_valid(x, y, plane)) {
        return 0;
    }
    int stackable = ground_item_is_stackable(item_id, quantity);
    if (stackable) {
        return spawn_ground_item_one(world, item_id, quantity, state_id,
                                     x, y, plane,
                                     owner_uid, original_owner_uid, visibility,
                                     reveal_timer, despawn_timer, 1,
                                     static_spawn, spawn_key, arrival_timer) >= 0;
    }
    if (quantity > ground_item_free_slots(world)
            || quantity >= INT_MAX - world->next_ground_item_uid) return 0;
    if (ground_item_count_at(world, x, y, plane) + quantity
            > RC_GROUND_ITEM_MAX_PER_TILE) {
        return 0;
    }
    for (int i = 0; i < quantity; i++) {
        if (spawn_ground_item_one(world, item_id, 1, state_id,
                                  x, y, plane, owner_uid,
                                  original_owner_uid, visibility,
                                  reveal_timer, despawn_timer, 0,
                                  static_spawn,
                                  ground_spawn_instance_key(
                                      spawn_key, (uint32_t)i), arrival_timer)
                < 0) {
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
    return spawn_ground_item_quantity(world, item_id, quantity, 0,
                                      x, y, plane,
                                      owner_uid, owner_uid, visibility,
                                      reveal_timer,
                                      RC_GROUND_ITEM_DESPAWN_TICKS, 0, 0, 0);
}

static void ground_granted(RcWorld *world, const RcGroundItem *item) {
    RcPayloadItemEvent payload = {
        .item_id = (uint32_t)item->item_id,
        .quantity = (uint32_t)item->quantity,
        .ground_uid = item->uid,
        .slot = UINT8_MAX,
    };
    rc_event_fire(world, RC_EVT_DROP_GRANTED, &payload);
}

static RcGroundGrantResult ground_grant_transaction(RcWorld *world,
    const RcGroundGrant *grants, int count, int x, int y, int plane,
    int owner_uid, RcGroundPolicy policy, RcItemTransaction *tx) {
    if (!loot_enabled(world)) return RC_GROUND_GRANT_DISABLED;
    if (count < 0 || count > RC_MAX_GROUND_ITEMS || (!grants && count)
            || owner_uid < RC_GROUND_OWNER_NONE
            || !rc_world_tile_valid(x, y, plane)
            || policy.delay_ticks < 0 || policy.reveal_ticks < 0
            || policy.lifetime_ticks <= 0
            || policy.untradeable_lifetime_ticks <= 0
            || policy.reveal_ticks > policy.lifetime_ticks)
        return RC_GROUND_GRANT_INVALID;
    if (!count) return !tx || rc_item_tx_commit(tx).code == RC_ITEM_RESULT_OK
        ? RC_GROUND_GRANT_OK : RC_GROUND_GRANT_CONFLICT;
    for (int i = 0; i < count; i++)
        if (!rc_item_def_get(grants[i].item_id) || grants[i].quantity <= 0)
            return RC_GROUND_GRANT_INVALID;

    // No callbacks run until the whole batch fits. Restore only this owned store.
    RcGroundItem before[RC_MAX_GROUND_ITEMS];
    memcpy(before, world->ground_items, sizeof(before));
    int before_count = world->ground_item_count;
    int before_uid = world->next_ground_item_uid;
    for (int i = 0; i < count; i++) {
        int tradeable = ground_item_is_tradeable(grants[i].item_id);
        int visibility = owner_uid == RC_GROUND_OWNER_NONE ? RC_GROUND_VIS_PUBLIC
                       : tradeable ? RC_GROUND_VIS_PRIVATE
                                   : RC_GROUND_VIS_PRIVATE_PERMANENT;
        // Delayed grants must reserve distinct slots, not change visible piles.
        int ok = spawn_ground_item_quantity(world, grants[i].item_id,
            grants[i].quantity, grants[i].state_id, x, y, plane,
            owner_uid, owner_uid, visibility,
            visibility == RC_GROUND_VIS_PRIVATE ? policy.reveal_ticks : 0,
            tradeable ? policy.lifetime_ticks : policy.untradeable_lifetime_ticks,
            0, 0, policy.delay_ticks);
        if (!ok) {
            memcpy(world->ground_items, before, sizeof(before));
            world->ground_item_count = before_count;
            world->next_ground_item_uid = before_uid;
            return RC_GROUND_GRANT_CAPACITY;
        }
    }
    if (tx && rc_item_tx_commit(tx).code != RC_ITEM_RESULT_OK) {
        memcpy(world->ground_items, before, sizeof(before));
        world->ground_item_count = before_count;
        world->next_ground_item_uid = before_uid;
        return RC_GROUND_GRANT_CONFLICT;
    }
    int receipt_count = 0;
    for (int i = 0; i < world->ground_item_count; i++) {
        RcGroundItem receipt = world->ground_items[i];
        if (!receipt.active) continue;
        if (before[i].active && before[i].uid == receipt.uid)
            receipt.quantity -= before[i].quantity;
        if (receipt.quantity > 0) before[receipt_count++] = receipt;
    }
    for (int i = 0; i < receipt_count; i++) ground_granted(world, &before[i]);
    return RC_GROUND_GRANT_OK;
}

RcGroundGrantResult rc_ground_grant(RcWorld *world,
    const RcGroundGrant *grants, int count, int x, int y, int plane,
    int owner_uid, RcGroundPolicy policy) {
    return ground_grant_transaction(world, grants, count, x, y, plane,
                                    owner_uid, policy, NULL);
}

RcGroundGrantResult rc_item_tx_commit_ground(RcItemTransaction *tx,
    const RcGroundGrant *grants, int count, int x, int y, int plane,
    int owner_uid, RcGroundPolicy policy) {
    if (!tx || !tx->valid) return RC_GROUND_GRANT_INVALID;
    return ground_grant_transaction(tx->world, grants, count, x, y, plane,
                                    owner_uid, policy, tx);
}

void rc_ground_item_advance(RcGroundItem *g, RcTick elapsed) {
    if (!g || !elapsed) return;
    if (!g->active) {
        if (!g->static_spawn || g->respawn_timer <= 0) return;
        if (elapsed < (RcTick)g->respawn_timer) {
            g->respawn_timer -= (int)elapsed;
            return;
        }
        g->respawn_timer = 0;
        g->quantity = g->spawn_quantity;
        g->active = true;
        g->version = g->version == INT_MAX ? 1 : g->version + 1;
        return;
    }
    if (g->arrival_timer > 0) {
        if (elapsed <= (RcTick)g->arrival_timer) {
            g->arrival_timer -= (int)elapsed;
            return;
        }
        elapsed -= (RcTick)g->arrival_timer;
        g->arrival_timer = 0;
    }
    if (g->reveal_timer > 0) {
        if (elapsed >= (RcTick)g->reveal_timer) {
            g->reveal_timer = 0;
            g->visibility = RC_GROUND_VIS_PUBLIC;
            g->owner_uid = RC_GROUND_OWNER_NONE;
            g->version = g->version == INT_MAX ? 1 : g->version + 1;
        } else g->reveal_timer -= (int)elapsed;
    }
    if (g->despawn_timer > 0) {
        if (elapsed >= (RcTick)g->despawn_timer) {
            g->despawn_timer = 0;
            g->active = false;
            g->quantity = 0;
            g->version = g->version == INT_MAX ? 1 : g->version + 1;
        } else g->despawn_timer -= (int)elapsed;
    }
}

void rc_ground_items_tick(RcWorld *world) {
    for (int i = 0; i < world->ground_item_count; i++) {
        RcGroundItem *g = &world->ground_items[i];
        if (world->tick < g->timer_start_tick) continue;
        rc_ground_item_advance(g, 1);
    }
}

void rc_clear_static_ground_items(RcWorld *world) {
    if (!world) return;
    for (int i = 0; i < world->ground_item_count; i++) {
        if (world->ground_items[i].static_spawn) {
            world->ground_items[i].active = false;
            world->ground_items[i].quantity = 0;
            world->ground_items[i].version++;
            world->ground_items[i].static_spawn = false;
            world->ground_items[i].spawn_key = 0;
            world->ground_items[i].spawn_quantity = 0;
        }
    }
}

int rc_load_ground_item_spawns_rect_stats(RcWorld *world, const char *path,
                                          int min_x, int min_y,
                                          int max_x, int max_y,
                                          int min_plane, int max_plane,
                                          RcGroundItemSpawnLoadStats *stats) {
    if (stats) memset(stats, 0, sizeof(*stats));
    if (!world || !path || !path[0] || min_x > max_x || min_y > max_y
            || min_plane > max_plane || !rc_plane_valid(min_plane)
            || !rc_plane_valid(max_plane)) {
        return -1;
    }
    RcSpawnIndexSlice slice = {0};
    if (!rc_spawn_index_read(path, GSPI_MAGIC, GSPI_RECORD_SIZE, 1,
                             min_x, min_y, max_x, max_y, &slice)
            || !rc_spawn_index_sort_source_order(&slice)) {
        rc_spawn_index_slice_free(&slice);
        return -1;
    }
    if (stats) {
        stats->total_rows = (int)slice.total_rows;
        stats->pages_loaded = (int)slice.pages_loaded;
        stats->rows_loaded = (int)slice.record_count;
        for (int plane = 0; plane < RC_MAX_PLANES; plane++) {
            if (plane < min_plane || plane > max_plane)
                stats->skipped_plane += (int)slice.source_plane_counts[plane];
        }
    }

    int spawned = 0;
    for (uint32_t i = 0; i < slice.record_count; i++) {
        const unsigned char *record = slice.records
            + (size_t)i * slice.record_size;
        const unsigned char *p = record;
        uint32_t source_order = 0;
        uint32_t item_id_u = 0, quantity_u = 0, x_u = 0, y_u = 0;
        uint32_t respawn_ticks = 0;
        uint8_t plane = 0, flags = 0;
        if (!read_u32(&p, record + slice.record_size, &source_order)
                || !read_u32(&p, record + slice.record_size, &item_id_u)
                || !read_u32(&p, record + slice.record_size, &quantity_u)
                || !read_u32(&p, record + slice.record_size, &x_u)
                || !read_u32(&p, record + slice.record_size, &y_u)
                || !read_u8(&p, record + slice.record_size, &plane)
                || !read_u8(&p, record + slice.record_size, &flags)
                || !read_u32(&p, record + slice.record_size, &respawn_ticks)) {
            rc_spawn_index_slice_free(&slice);
            return -1;
        }
        int32_t x = (int32_t)x_u;
        int32_t y = (int32_t)y_u;
        if (!rc_world_tile_valid(x, y, plane)) {
            if (stats) stats->skipped_invalid++;
            continue;
        }
        if ((int)plane < min_plane || (int)plane > max_plane) {
            continue;
        }
        if (x < min_x || x > max_x || y < min_y || y > max_y) {
            continue;
        }
        if (stats) stats->matched_filter++;
        if (item_id_u > INT_MAX || !rc_item_def_get((int)item_id_u)
                || quantity_u == 0 || quantity_u > INT_MAX || flags
                || respawn_ticks == 0 || respawn_ticks > INT_MAX) {
            if (stats) stats->skipped_invalid++;
            continue;
        }
        int first_uid = world->next_ground_item_uid;
        int ok = spawn_ground_item_quantity(
            world, (int)item_id_u, (int)quantity_u, 0, x, y, (int)plane,
            RC_GROUND_OWNER_NONE, RC_GROUND_OWNER_NONE,
            RC_GROUND_VIS_PUBLIC, 0, 0, 1,
            rc_spawn_index_record_key(path, source_order, 0), 0);
        if (ok) {
            for (int j = 0; j < world->ground_item_count; j++) {
                RcGroundItem *g = &world->ground_items[j];
                if (g->static_spawn && g->uid >= first_uid)
                    g->respawn_ticks = (int)respawn_ticks;
            }
            spawned++;
            if (stats) stats->spawned++;
        } else if (stats) {
            stats->skipped_capacity++;
        }
    }
    if (stats) {
        stats->skipped_filtered = stats->total_rows
                                - stats->skipped_plane
                                - stats->skipped_invalid
                                - stats->matched_filter;
    }
    rc_spawn_index_slice_free(&slice);
    return spawned;
}

RcItemActionResult rc_player_drop_item_expected(RcWorld *world, int inv_slot,
                                                uint32_t expected_generation) {
    if (!inventory_enabled(world) || !loot_enabled(world)
            || !valid_inv_slot(inv_slot))
        return record_item_result(world, item_result(
            RC_ITEM_RESULT_DISABLED, -1, inv_slot));
    if (!rc_player_action_allowed(world->enabled, RC_PLAYER_ACTION_DROP_ITEM))
        return record_item_result(world, item_result(
            RC_ITEM_RESULT_ACTION_DENIED, -1, inv_slot));
    RcPlayer *player = &world->player;
    RcInvSlot item = player->inventory[inv_slot];
    if (item.item_id < 0 || item.quantity <= 0)
        return record_item_result(world, item_result(
            RC_ITEM_RESULT_EMPTY, item.item_id, inv_slot));
    if (expected_generation != UINT32_MAX
            && item.generation != expected_generation)
        return record_item_result(world, item_result(
            RC_ITEM_RESULT_STALE, item.item_id, inv_slot));
    const RcItemDef *def = rc_item_def_get(item.item_id);
    if (!def || strcmp(def->inventory_actions[4], "Drop") != 0)
        return record_item_result(world, item_result(
            RC_ITEM_RESULT_ACTION_DENIED, item.item_id, inv_slot));
    RcItemTransaction tx;
    (void)rc_item_tx_begin(&tx, world);
    RcItemActionResult result = rc_item_tx_remove_slot(
        &tx, inv_slot, item.quantity, expected_generation);
    if (result.code != RC_ITEM_RESULT_OK)
        return record_item_result(world, result);
    RcGroundGrant grant = {item.item_id, item.quantity, item.state_id};
    RcGroundPolicy policy = {RC_GROUND_ITEM_REVEAL_TICKS,
        RC_GROUND_ITEM_DESPAWN_TICKS, 0, RC_GROUND_ITEM_DESPAWN_TICKS};
    RcGroundGrantResult ground = rc_item_tx_commit_ground(&tx, &grant, 1,
        player->x, player->y, player->plane, RC_GROUND_ITEM_LOCAL_OWNER, policy);
    if (ground != RC_GROUND_GRANT_OK) {
        return record_item_result(world, item_result(
            ground == RC_GROUND_GRANT_CAPACITY ? RC_ITEM_RESULT_CAPACITY
            : RC_ITEM_RESULT_CONFLICT, item.item_id, inv_slot));
    }
    RcPayloadItemEvent payload = {
        .item_id = (uint32_t)item.item_id,
        .quantity = (uint32_t)item.quantity,
        .ground_uid = -1,
        .slot = (uint8_t)inv_slot,
    };
    rc_event_fire(world, RC_EVT_ITEM_DROPPED, &payload);
    result.item_id = item.item_id;
    result.slot = inv_slot;
    return record_item_result(world, result);
}

RcItemActionResult rc_player_drop_item(RcWorld *world, int inv_slot) {
    if (rc_player_command_should_queue(world)) {
        uint32_t generation = valid_inv_slot(inv_slot)
                            ? world->player.inventory[inv_slot].generation : 0;
        int args[8] = {inv_slot, (int)generation, 0, 0, 0, 0, 0, 0};
        RcItemResultCode code = rc_player_command_submit(
            world, RC_PLAYER_COMMAND_DROP_ITEM, RC_ACTION_CATEGORY_NORMAL,
            args, 0) ? RC_ITEM_RESULT_QUEUED : RC_ITEM_RESULT_ACTION_DENIED;
        return record_item_result(world, item_result(code, -1, inv_slot));
    }
    return rc_player_drop_item_expected(world, inv_slot, UINT32_MAX);
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
    if (!rc_ground_item_visible(g, RC_GROUND_OWNER_LOCAL_PLAYER))
        return RC_GROUND_TAKE_INVALID;
    if ((expected_uid >= 0 && g->uid != expected_uid) ||
            (expected_version >= 0 && g->version != expected_version)) {
        return RC_GROUND_TAKE_STALE;
    }
    if (!rc_ground_item_reachable(world, g->x, g->y, g->plane))
        return RC_GROUND_TAKE_INVALID;
    if (!inventory_can_add(player->inventory, g->item_id, g->quantity,
                           g->state_id))
        return RC_GROUND_TAKE_FULL;

    int item_id = g->item_id;
    int quantity = g->quantity;
    uint32_t state_id = g->state_id;
    RcItemTransaction tx;
    (void)rc_item_tx_begin(&tx, world);
    RcItemActionResult result = rc_item_tx_add(
        &tx, item_id, quantity, state_id);
    if (result.code != RC_ITEM_RESULT_OK) {
        record_item_result(world, result);
        return result.code == RC_ITEM_RESULT_CAPACITY
             ? RC_GROUND_TAKE_FULL : RC_GROUND_TAKE_INVALID;
    }
    int slot = result.slot;
    result = rc_item_tx_commit(&tx);
    if (result.code != RC_ITEM_RESULT_OK) {
        record_item_result(world, result);
        return result.code == RC_ITEM_RESULT_STALE
             ? RC_GROUND_TAKE_STALE : RC_GROUND_TAKE_INVALID;
    }
    g->active = false;
    g->quantity = 0;
    g->respawn_timer = g->static_spawn ? g->respawn_ticks : 0;
    g->timer_start_tick = world->tick + (world->in_tick ? 1u : 0u);
    g->version++;
    RcPayloadItemEvent payload = {
        .item_id = (uint32_t)item_id,
        .quantity = (uint32_t)quantity,
        .ground_uid = g->uid,
        .slot = (uint8_t)slot,
    };
    rc_event_fire(world, RC_EVT_ITEM_PICKED_UP, &payload);
    result.item_id = item_id;
    result.slot = slot;
    record_item_result(world, result);
    return RC_GROUND_TAKE_OK;
}

RcItemActionResult rc_player_pickup_item_expected(RcWorld *world,
    int ground_item_idx, int uid, int version) {
    if (!world || ground_item_idx < 0
            || ground_item_idx >= world->ground_item_count)
        return record_item_result(world, item_result(
            RC_ITEM_RESULT_INVALID, -1, ground_item_idx));
    const RcGroundItem *expected = &world->ground_items[ground_item_idx];
    if (expected->uid != uid || expected->version != version)
        return record_item_result(world, item_result(
            RC_ITEM_RESULT_STALE, expected->item_id, ground_item_idx));
    if (rc_player_command_should_queue(world)) {
        int args[8] = {ground_item_idx, uid, version, 0, 0, 0, 0, 0};
        RcItemResultCode code = rc_player_command_submit(
            world, RC_PLAYER_COMMAND_PICKUP_ITEM, RC_ACTION_CATEGORY_NORMAL,
            args, 0) ? RC_ITEM_RESULT_QUEUED : RC_ITEM_RESULT_ACTION_DENIED;
        return record_item_result(world, item_result(code, -1,
                                                      ground_item_idx));
    }
    if (!inventory_enabled(world) || !loot_enabled(world)
            || !rc_player_action_allowed(world->enabled,
                                         RC_PLAYER_ACTION_PICKUP_ITEM)
            || ground_item_idx < 0
            || ground_item_idx >= world->ground_item_count) {
        return record_item_result(world, item_result(
            RC_ITEM_RESULT_DISABLED, -1, ground_item_idx));
    }
    RcGroundItem *g = &world->ground_items[ground_item_idx];
    RcPlayer *player = &world->player;
    if (!g->active || g->item_id < 0 || g->quantity <= 0
            || !rc_ground_item_visible(g, RC_GROUND_OWNER_LOCAL_PLAYER)
            || g->plane != player->plane)
        return record_item_result(world, item_result(
            RC_ITEM_RESULT_INVALID, g->item_id, ground_item_idx));
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
                             &target, 1);
        return record_item_result(world, item_result(
            RC_ITEM_RESULT_QUEUED, g->item_id, ground_item_idx));
    }
    int take = rc_player_take_ground_item(
        world, ground_item_idx, g->uid, g->version);
    RcItemActionResult result = world->player.last_item_action;
    if (take == RC_GROUND_TAKE_OK) result.code = RC_ITEM_RESULT_OK;
    else if (take == RC_GROUND_TAKE_FULL) result.code = RC_ITEM_RESULT_CAPACITY;
    else if (take == RC_GROUND_TAKE_STALE) result.code = RC_ITEM_RESULT_STALE;
    else result.code = RC_ITEM_RESULT_INVALID;
    return record_item_result(world, result);
}

RcItemActionResult rc_player_pickup_item(RcWorld *world, int index) {
    const RcGroundItem *g = world && index >= 0 && index < world->ground_item_count
                         ? &world->ground_items[index] : NULL;
    return rc_player_pickup_item_expected(world, index, g ? g->uid : -1,
                                          g ? g->version : -1);
}
