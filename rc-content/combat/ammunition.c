#include "ammunition.h"

#include "gear.h"
#include "magic.h"
#include "items.h"
#include "combat.h"
#include "prayer.h"
#include "npc.h"
#include "config.h"
#include "pathfinding.h"
#include "rng.h"
#include <string.h>

static bool recoverable_ammunition(const RcItemDef *def) {
    if (!def) return false;
    if (def->equip_slot == EQUIP_WEAPON && def->weapon_type == 23
            && (strstr(def->name, "dart") || strstr(def->name, "knife"))) return true;
    int kind = rc_content_gear_row(def->id)->ammo_kind;
    return kind == AMMO_ARROW || kind == AMMO_BOLT || kind == AMMO_ASCENSION;
}

// Ava's devices keep shots from landing; the rest drop or break as usual.
static int ava_keep_percent(const RcPlayer *player) {
    switch (rc_content_gear_row(player->equipment[EQUIP_CAPE].item_id)->family) {
    case GEAR_AVAS_ASSEMBLER: return 80;
    case GEAR_AVAS_ACCUMULATOR: return 72;
    case GEAR_AVAS_ATTRACTOR: return 60;
    default: return 0;
    }
}

int rc_content_consume_ammunition(RcWorld *world, int slot, int count,
    int x, int y, int plane, int delay, const char **failure) {
    *failure = NULL;
    if (!world || slot < 0 || slot >= RC_EQUIP_COUNT || count <= 0 || count > 4) {
        *failure = "Invalid ammunition payment.";
        return 0;
    }
    RcInvSlot ammo = world->player.equipment[slot];
    RcItemTransaction tx;
    if (rc_item_tx_begin(&tx, world).code != RC_ITEM_RESULT_OK
            || rc_item_tx_remove_equipment(&tx, slot, count, ammo.generation).code
                != RC_ITEM_RESULT_OK) {
        *failure = "Ammunition changed before the attack could launch.";
        return 0;
    }
    uint32_t rng = world->rng_state;
    int recovered = 0;
    if ((world->enabled & RC_SUB_LOOT)
            && recoverable_ammunition(rc_item_def_get(ammo.item_id))
            && !rc_tile_blocked(&world->map, x, y, plane))
        for (int i = 0; i < count; i++)
            if (rc_rng_range(&world->rng_state, 4) != 0) recovered++;
    int keep = ava_keep_percent(&world->player);
    if (keep && rc_content_roll(&world->rng_state, 100) < keep) {
        // The device caught the shot: pay nothing.
        world->rng_state = rng;
        (void)rc_content_roll(&world->rng_state, 100);
        return 1;
    }
    if (!recovered) {
        if (rc_item_tx_commit(&tx).code == RC_ITEM_RESULT_OK) return 1;
    } else {
        RcGroundGrant grant = {ammo.item_id, recovered, ammo.state_id};
        RcGroundPolicy policy = {100, 200, delay, 300};
        RcGroundGrantResult result = rc_item_tx_commit_ground(&tx, &grant, 1,
            x, y, plane, RC_GROUND_OWNER_LOCAL_PLAYER, policy);
        if (result == RC_GROUND_GRANT_OK) return 1;
    }
    world->rng_state = rng;
    *failure = "Cannot reserve recovered ammunition; attack and payment cancelled.";
    return 0;
}

int rc_content_ranged_resource_slot(const RcPlayer *player, const char **failure) {
    const RcItemDef *weapon = player
        ? rc_item_def_get(player->equipment[EQUIP_WEAPON].item_id) : NULL;
    *failure = "Ranged weapon or ammunition rules are unavailable.";
    if (!weapon) return RC_RANGED_RESOURCE_INVALID;
    const RcGearRow *rule = rc_content_gear_row(weapon->id);
    if (rule->family == GEAR_TONALZTICS || rule->family == GEAR_TONALZTICS_UNCHARGED)
        return RC_RANGED_RESOURCE_NONE;
    // Eclipse is magic-based: no darts needed.
    if (rule->special == SPECIAL_ECLIPSE && player->combat.special_pending)
        return RC_RANGED_RESOURCE_NONE;
    if (weapon->stackable && weapon->equip_slot == EQUIP_WEAPON
            && (weapon->weapon_type == 7 || weapon->weapon_type == 23))
        return EQUIP_WEAPON;
    if (rule->ammo_kind == AMMO_INTERNAL) {
        if (rc_content_internal_ammo_ready(&player->equipment[EQUIP_WEAPON], failure))
            return RC_RANGED_RESOURCE_NONE;
        return RC_RANGED_RESOURCE_INVALID;
    }
    if (rule->ammo_kind == AMMO_NONE) return RC_RANGED_RESOURCE_INVALID;
    const RcInvSlot *ammo = &player->equipment[EQUIP_AMMO];
    if (ammo->item_id < 0 || ammo->quantity <= 0) {
        *failure = "There is no ammunition in your ammo slot.";
        return RC_RANGED_RESOURCE_INVALID;
    }
    const RcGearRow *shot = rc_content_gear_row(ammo->item_id);
    if (weapon->id == 8880) {  // Dorgeshuun crossbow
        static const int fits[] = {
            8882, 877, 878, 6061, 6062, 9139, 9286, 9293, 9300,
            9140, 9287, 9294, 9301,
        };
        for (unsigned i = 0; i < sizeof(fits) / sizeof(fits[0]); i++)
            if (ammo->item_id == fits[i]) return EQUIP_AMMO;
        *failure = "The bone crossbow cannot fire silver or gem-tipped bolts, or metal bolts above iron.";
        return RC_RANGED_RESOURCE_INVALID;
    }
    if (shot->ammo_kind == AMMO_NONE) {
        *failure = "That item is not supported ammunition for this weapon.";
        return RC_RANGED_RESOURCE_INVALID;
    }
    if (shot->ammo_kind == rule->ammo_kind && shot->ammo_tier <= rule->ammo_tier
            && (rule->ammo_kind != AMMO_TAR || shot->ammo_tier == rule->ammo_tier))
        return EQUIP_AMMO;
    *failure = "That ammunition is incompatible with this weapon or exceeds its ammunition tier.";
    return RC_RANGED_RESOURCE_INVALID;
}

void rc_content_ranged_calc(const RcWorld *world, const RcNpc *target, RcCombatCalc *calc) {
    const RcPlayer *p = &world->player;
    const RcItemDef *weapon = rc_item_def_get(p->equipment[EQUIP_WEAPON].item_id);
    if (!weapon) return;
    if (weapon->weapon_type == 7) {
        const RcNpcDef *def = rc_npc_def_for_npc(world, target);
        int size = def && def->size > 0 ? def->size : 1;
        int dx = p->x - (target->x + size / 2), dy = p->y - (target->y + size / 2);
        if (dx < 0) dx = -dx;
        if (dy < 0) dy = -dy;
        int distance = dx > dy ? dx : dy;
        int factor = p->attack_style_idx == 0 ? (distance < 4 ? 4 : distance < 7 ? 3 : 2)
            : p->attack_style_idx == 1 ? (distance < 4 || distance >= 7 ? 3 : 4)
            : (distance < 4 ? 2 : distance < 7 ? 3 : 4);
        calc->attack_roll = calc->attack_roll * factor / 4;
        calc->hit_chance = rc_hit_chance(calc->attack_roll, calc->defence_roll);
    }
}
