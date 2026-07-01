#include "combat.h"
#include "combat_formula.h"
#include "combat_hit.h"
#include "combat_profiles.h"
#include "encounter.h"
#include "events.h"
#include "items.h"
#include "npc.h"
#include "pathfinding.h"
#include "prayer.h"
#include "rng.h"
#include "skills.h"
#include "spells.h"
#include "types.h"
#include <stddef.h>   // NULL
#include <string.h>

static int npc_can_retaliate(const RcNpc *npc);
static int combat_can_attack_target(RcWorld *world, RcCombatActorRef attacker,
                                    RcCombatActorRef target);

static bool style_is_melee(RcCombatStyle style) {
    return style == COMBAT_MELEE_STAB ||
           style == COMBAT_MELEE_SLASH ||
           style == COMBAT_MELEE_CRUSH;
}

void rc_combat_register_content_hooks(struct RcWorld *world,
                                      const RcCombatContentHooks *hooks) {
    if (!world) return;
    memset(&world->combat_hooks, 0, sizeof(world->combat_hooks));
    if (hooks) world->combat_hooks = *hooks;
}

int rc_combat_apply_regular_npc_player_damage_rules(
    const struct RcWorld *world, const RcNpc *target, int damage) {
    if (!world || !target || damage <= 0) return damage;
    if (world->combat_hooks.apply_player_damage) {
        return world->combat_hooks.apply_player_damage(world, target, damage);
    }
    return damage;
}

int rc_combat_apply_regular_npc_attack_rules(
    struct RcWorld *world, RcNpc *attacker, int damage) {
    if (!world || !attacker || damage <= 0) return damage;
    if (world->combat_hooks.apply_npc_attack_damage) {
        return world->combat_hooks.apply_npc_attack_damage(world, attacker,
                                                           damage);
    }
    return damage;
}

// ---- Auto-attack tick --------------------------------------------------

// Chebyshev distance (tile-space range check).
static int chebyshev(int x1, int y1, int x2, int y2) {
    int dx = x1 > x2 ? x1 - x2 : x2 - x1;
    int dy = y1 > y2 ? y1 - y2 : y2 - y1;
    return dx > dy ? dx : dy;
}

static void nearest_npc_tile_to_point(const RcNpc *npc, int px, int py,
                                      int *tx, int *ty) {
    const RcNpcDef *def = rc_npc_def_for_npc(npc);
    int size = def && def->size > 0 ? def->size : 1;
    int min_x = npc->x;
    int min_y = npc->y;
    int max_x = npc->x + size - 1;
    int max_y = npc->y + size - 1;
    if (px < min_x) *tx = min_x;
    else if (px > max_x) *tx = max_x;
    else *tx = px;
    if (py < min_y) *ty = min_y;
    else if (py > max_y) *ty = max_y;
    else *ty = py;
}

static int npc_footprint_size(const RcNpc *npc);

static void npc_projectile_source_tile(const RcNpc *npc,
                                       const RcPlayer *target,
                                       RcCombatStyle style,
                                       int *sx,
                                       int *sy) {
    if (!npc || !target || !sx || !sy)
        return;
    (void)style;
    nearest_npc_tile_to_point(npc, target->x, target->y, sx, sy);
}

static int point_in_npc_footprint(const RcNpc *npc, int x, int y) {
    if (!npc) return 0;
    int size = npc_footprint_size(npc);
    return x >= npc->x && x < npc->x + size &&
           y >= npc->y && y < npc->y + size;
}

static int distance_player_to_npc(const RcPlayer *p, const RcNpc *npc) {
    int tx, ty;
    nearest_npc_tile_to_point(npc, p->x, p->y, &tx, &ty);
    return chebyshev(p->x, p->y, tx, ty);
}

static void face_player_to_npc(RcPlayer *p, const RcNpc *npc) {
    int tx, ty;
    nearest_npc_tile_to_point(npc, p->x, p->y, &tx, &ty);
    p->facing_entity = npc->uid;
    p->facing_x = tx;
    p->facing_y = ty;
}

static void face_npc_to_player(RcNpc *npc, const RcPlayer *p) {
    npc->facing_entity = 0;
    npc->facing_x = p->x;
    npc->facing_y = p->y;
}

static void face_npc_move_delta(RcNpc *npc, int dx, int dy) {
    if (!npc || (!dx && !dy)) return;
    npc->facing_entity = -1;
    npc->facing_x = npc->x + dx;
    npc->facing_y = npc->y + dy;
}

static int style_needs_los(RcCombatStyle style) {
    return style == COMBAT_RANGED || style == COMBAT_MAGIC;
}

static int default_npc_attack_range(const RcNpcDef *def, RcCombatStyle style) {
    if (style == COMBAT_MAGIC) return 10;
    if (style == COMBAT_RANGED) return 7;
    return def && (def->attack_types & 0x18) ? 7 : 1;
}

static int content_npc_attack_range(const RcWorld *world, const RcNpc *npc,
                                    RcCombatStyle style, int default_range) {
    if (world && world->combat_hooks.npc_attack_range) {
        return world->combat_hooks.npc_attack_range(world, npc, style,
                                                    default_range);
    }
    return default_range;
}

static RcCombatStyle content_select_npc_style(RcWorld *world, RcNpc *npc,
                                              const RcPlayer *player,
                                              RcCombatStyle default_style) {
    if (world && world->combat_hooks.select_npc_style) {
        return world->combat_hooks.select_npc_style(world, npc, player,
                                                   default_style);
    }
    return default_style;
}

static int content_modify_npc_roll_damage(RcWorld *world, RcNpc *npc,
                                          RcCombatStyle style, int damage) {
    if (world && world->combat_hooks.modify_npc_roll_damage) {
        return world->combat_hooks.modify_npc_roll_damage(world, npc, style,
                                                          damage);
    }
    return damage;
}

static void content_after_npc_swing(RcWorld *world, RcNpc *npc,
                                    RcCombatStyle style) {
    if (world && world->combat_hooks.after_npc_swing) {
        world->combat_hooks.after_npc_swing(world, npc, style);
    }
}

static int content_modify_npc_attack_speed(RcWorld *world, RcNpc *npc,
                                           int default_speed) {
    if (world && world->combat_hooks.modify_npc_attack_speed) {
        return world->combat_hooks.modify_npc_attack_speed(world, npc,
                                                           default_speed);
    }
    return default_speed;
}

static int content_modify_incoming_after_protection(RcWorld *world,
                                                    const RcPendingHit *hit,
                                                    int damage) {
    if (world && world->combat_hooks.modify_incoming_damage_after_protection) {
        return world->combat_hooks.modify_incoming_damage_after_protection(
            world, hit, damage);
    }
    return damage;
}

static int content_player_special_energy_cost(const RcWorld *world,
                                              const RcPlayer *player,
                                              const RcNpc *target,
                                              int weapon_id) {
    if (world && world->combat_hooks.player_special_energy_cost) {
        return world->combat_hooks.player_special_energy_cost(
            world, player, target, weapon_id);
    }
    return 0;
}

static int content_modify_player_special_damage(RcWorld *world,
                                                const RcPlayer *player,
                                                const RcNpc *target,
                                                int weapon_id,
                                                RcCombatStyle style,
                                                int damage, int max_hit) {
    if (world && world->combat_hooks.modify_player_special_damage) {
        return world->combat_hooks.modify_player_special_damage(
            world, player, target, weapon_id, style, damage, max_hit);
    }
    return damage;
}

static void content_on_npc_hit_player(RcWorld *world, const RcPendingHit *hit,
                                      int damage) {
    if (world && world->combat_hooks.on_npc_hit_player) {
        world->combat_hooks.on_npc_hit_player(world, hit, damage);
    }
}

static int equipment_consume_one(RcPlayer *p, int slot) {
    if (!p || slot < 0 || slot >= RC_EQUIP_COUNT) return 0;
    RcInvSlot *item = &p->equipment[slot];
    if (item->item_id < 0 || item->quantity <= 0) return 0;
    item->quantity--;
    if (item->quantity <= 0) {
        item->item_id = -1;
        item->quantity = 0;
    }
    rc_recalc_bonuses(p);
    return 1;
}

static int player_has_ranged_resource(const RcPlayer *p) {
    if (!p || p->combat_style != COMBAT_RANGED) return 1;
    const RcInvSlot *weapon_slot = &p->equipment[EQUIP_WEAPON];
    const RcItemDef *weapon = rc_item_def_get(weapon_slot->item_id);
    if (weapon && weapon->stackable && weapon_slot->quantity > 0 &&
            weapon->equip_slot == EQUIP_WEAPON) {
        return 1;
    }
    return p->equipment[EQUIP_AMMO].item_id >= 0 &&
           p->equipment[EQUIP_AMMO].quantity > 0;
}

static int player_consume_ranged_resource(RcPlayer *p) {
    if (!p || p->combat_style != COMBAT_RANGED) return 1;
    const RcInvSlot *weapon_slot = &p->equipment[EQUIP_WEAPON];
    const RcItemDef *weapon = rc_item_def_get(weapon_slot->item_id);
    if (weapon && weapon->stackable && weapon_slot->quantity > 0 &&
            weapon->equip_slot == EQUIP_WEAPON) {
        return equipment_consume_one(p, EQUIP_WEAPON);
    }
    return equipment_consume_one(p, EQUIP_AMMO);
}

static int inventory_quantity(const RcInvSlot *inv, int item_id) {
    if (!inv || item_id < 0) return 0;
    int total = 0;
    for (int i = 0; i < RC_INVENTORY_SIZE; i++) {
        if (inv[i].item_id == item_id && inv[i].quantity > 0) {
            total += inv[i].quantity;
        }
    }
    return total;
}

static int fallback_player_has_spell_runes(const RcPlayer *p,
                                           const RcSpellDef *spell) {
    if (!p || !spell) return 0;
    for (int i = 0; i < spell->rune_count; i++) {
        int item_id = (int)spell->runes[i].item_id;
        int qty = (int)spell->runes[i].qty;
        if (qty > 0 && inventory_quantity(p->inventory, item_id) < qty) {
            return 0;
        }
    }
    return 1;
}

static int content_player_has_spell_runes(const RcWorld *world,
                                          const RcPlayer *p,
                                          const RcSpellDef *spell) {
    if (!world || !p || !spell) return 0;
    if (world->combat_hooks.player_has_spell_runes) {
        return world->combat_hooks.player_has_spell_runes(world, p, spell);
    }
    return fallback_player_has_spell_runes(p, spell);
}

static int fallback_player_consume_spell_runes(RcPlayer *p,
                                               const RcSpellDef *spell) {
    if (!p || !spell || !fallback_player_has_spell_runes(p, spell)) return 0;
    for (int i = 0; i < spell->rune_count; i++) {
        int item_id = (int)spell->runes[i].item_id;
        int left = (int)spell->runes[i].qty;
        for (int slot = 0; slot < RC_INVENTORY_SIZE && left > 0; slot++) {
            if (p->inventory[slot].item_id != item_id) continue;
            int removed = rc_inv_remove_quantity(p->inventory, slot, left);
            left -= removed;
        }
        if (left > 0) return 0;
    }
    rc_recalc_bonuses(p);
    return 1;
}

static int content_player_consume_spell_runes(RcWorld *world, RcPlayer *p,
                                              const RcSpellDef *spell) {
    if (!world || !p || !spell) return 0;
    if (world->combat_hooks.player_consume_spell_runes) {
        return world->combat_hooks.player_consume_spell_runes(world, p,
                                                              spell);
    }
    return fallback_player_consume_spell_runes(p, spell);
}

static const RcSpellDef *player_selected_combat_spell(const RcPlayer *p) {
    if (!p || p->combat_style != COMBAT_MAGIC) return NULL;
    int spell_idx = p->manual_spell_cast >= 0
                  ? p->manual_spell_cast : p->autocast_spell;
    const RcSpellDef *spell = rc_spell_def_get(spell_idx);
    if (!spell || spell->type != RC_SPELL_TYPE_COMBAT ||
            spell->max_hit <= 0 ||
            spell->book != p->current_spellbook) {
        return NULL;
    }
    return spell;
}

static void clear_failed_player_spell_attack(RcPlayer *p) {
    if (!p) return;
    if (p->manual_spell_cast >= 0) {
        p->manual_spell_cast = -1;
    } else if (p->autocast_spell >= 0) {
        p->autocast_spell = -1;
        p->defensive_autocast = false;
    }
    rc_refresh_player_combat_style(p);
}

static int player_ammo_item_id(const RcPlayer *p) {
    if (!p) return -1;
    const RcInvSlot *weapon_slot = &p->equipment[EQUIP_WEAPON];
    const RcItemDef *weapon = rc_item_def_get(weapon_slot->item_id);
    if (weapon && weapon->stackable && weapon_slot->quantity > 0 &&
            weapon->equip_slot == EQUIP_WEAPON) {
        return weapon_slot->item_id;
    }
    return p->equipment[EQUIP_AMMO].item_id;
}

typedef struct {
    const RcCombatProfileDef *weapon;
    const RcCombatProfileDef *projectile;
    const RcCombatProfileDef *effect;
    int ammo_id;
} RcPlayerAttackProfiles;

static int profile_hit_delay(const RcCombatProfileDef *profile,
                             int default_delay) {
    return profile && profile->hit_delay >= 0 ? profile->hit_delay
                                              : default_delay;
}

static const RcCombatProfileDef *player_timing_profile(
    const RcPlayerAttackProfiles *profiles
) {
    if (!profiles) return NULL;
    if (profiles->effect && profiles->effect->hit_delay >= 0)
        return profiles->effect;
    if (profiles->projectile && profiles->projectile->hit_delay >= 0)
        return profiles->projectile;
    return profiles->weapon;
}

static RcCombatAttackEvent *next_attack_event_slot(RcWorld *world) {
    if (!world) return NULL;
    if (world->combat_attack_event_count >= RC_MAX_COMBAT_ATTACK_EVENTS)
        return NULL;
    return &world->combat_attack_events[world->combat_attack_event_count++];
}

static void copy_event_name(char dst[64], const char *src) {
    if (!dst) return;
    dst[0] = '\0';
    if (!src) return;
    strncpy(dst, src, 63);
    dst[63] = '\0';
}

static int event_name_is_numeric_id(const char *name, int id) {
    if (!name || !name[0] || id < 0)
        return 0;
    long value = 0;
    for (const char *p = name; *p; p++) {
        if (*p < '0' || *p > '9')
            return 0;
        value = value * 10 + (*p - '0');
        if (value > 2147483647L)
            return 0;
    }
    return value == id;
}

static void emit_player_attack_event(
    RcWorld *world,
    const RcPlayer *p,
    const RcNpc *target,
    const RcSpellDef *spell,
    int spell_idx,
    int weapon_id,
    bool use_special,
    const RcPlayerAttackProfiles *profiles,
    int hit_delay
) {
    if (!world || !p || !target || !profiles) return;
    RcCombatAttackEvent *event = next_attack_event_slot(world);
    if (!event) return;
    memset(event, 0, sizeof(*event));
    event->active = true;
    event->source_kind = RC_COMBAT_ACTOR_PLAYER;
    event->target_kind = RC_COMBAT_ACTOR_NPC;
    event->style = (uint8_t)p->combat_style;
    event->source_uid = 0;
    event->target_uid = target->uid;
    event->source_definition_id = -1;
    const RcNpcDef *target_def = rc_npc_def_for_npc(target);
    event->target_definition_id = target_def ? target_def->id : -1;
    event->source_x = p->x;
    event->source_y = p->y;
    nearest_npc_tile_to_point(target, p->x, p->y,
                              &event->target_x, &event->target_y);
    event->plane = p->plane;
    event->hit_delay = hit_delay;
    event->weapon_item_id = weapon_id;
    event->ammo_item_id = profiles->ammo_id;
    event->spell_idx = spell_idx;
    event->stance_idx = p->attack_style_idx;
    event->world_tick = world->tick;

    if (use_special) {
        event->action_kind = RC_COMBAT_ACTION_SPECIAL;
        event->action_key_id = weapon_id;
        const RcItemDef *item = rc_item_def_get(weapon_id);
        copy_event_name(event->action_key_name, item ? item->name : "");
    } else if (p->combat_style == COMBAT_MAGIC && spell) {
        event->action_kind = RC_COMBAT_ACTION_SPELL;
        event->action_key_id = spell_idx;
        copy_event_name(event->action_key_name, spell->name);
    } else {
        event->action_kind = RC_COMBAT_ACTION_ITEM;
        event->action_key_id = weapon_id;
        const RcItemDef *item = rc_item_def_get(weapon_id);
        copy_event_name(event->action_key_name, item ? item->name : "");
    }
}

static void emit_npc_attack_event(
    RcWorld *world,
    const RcNpc *npc,
    const RcNpcDef *def,
    const RcPlayer *target,
    RcCombatStyle style,
    int hit_delay
) {
    if (!world || !npc || !def || !target) return;
    RcCombatAttackEvent *event = next_attack_event_slot(world);
    if (!event) return;
    memset(event, 0, sizeof(*event));
    event->active = true;
    event->source_kind = RC_COMBAT_ACTOR_NPC;
    event->target_kind = RC_COMBAT_ACTOR_PLAYER;
    event->style = (uint8_t)style;
    event->source_uid = npc->uid;
    event->target_uid = 0;
    event->source_definition_id = def->id;
    event->target_definition_id = -1;
    npc_projectile_source_tile(npc, target, style,
                               &event->source_x, &event->source_y);
    event->target_x = target->x;
    event->target_y = target->y;
    event->plane = npc->plane;
    event->action_kind = RC_COMBAT_ACTION_NPC;
    event->action_key_id = def->id;
    copy_event_name(event->action_key_name, def->name);
    event->hit_delay = hit_delay;
    event->weapon_item_id = -1;
    event->ammo_item_id = -1;
    event->spell_idx = -1;
    event->stance_idx = -1;
    event->world_tick = world->tick;
}

static RcPlayerAttackProfiles select_player_attack_profiles(
    const RcPlayer *p,
    const RcSpellDef *spell,
    int spell_idx,
    int weapon_id,
    bool use_special
) {
    RcPlayerAttackProfiles out = {
        .weapon = NULL,
        .projectile = NULL,
        .effect = NULL,
        .ammo_id = -1,
    };
    if (!p) return out;
    out.ammo_id = player_ammo_item_id(p);
    const RcCombatProfileDef *weapon_profile =
        rc_combat_profile_for_item_stance(weapon_id, p->combat_style,
                                          p->attack_style_idx);
    const RcCombatProfileDef *special_profile = use_special
        ? rc_combat_profile_for_special_item(weapon_id, p->combat_style)
        : NULL;
    if (special_profile) {
        weapon_profile = special_profile;
        out.effect = special_profile;
    }
    const RcCombatProfileDef *projectile_profile = NULL;
    if (p->combat_style == COMBAT_MAGIC && spell) {
        projectile_profile =
            rc_combat_profile_for_spell_id(spell_idx, spell->name,
                                           p->combat_style);
    } else if (p->combat_style == COMBAT_RANGED) {
        projectile_profile = rc_combat_profile_for_item(out.ammo_id,
                                                        p->combat_style);
        if (!projectile_profile)
            projectile_profile = weapon_profile;
    }
    if (special_profile)
        projectile_profile = special_profile;

    out.weapon = weapon_profile;
    out.projectile = projectile_profile;
    return out;
}

static int has_player_attack_los(const RcWorld *world, const RcPlayer *p,
                                 const RcNpc *npc, RcCombatStyle style) {
    if (!style_needs_los(style)) {
        return 1;
    }
    int tx, ty;
    nearest_npc_tile_to_point(npc, p->x, p->y, &tx, &ty);
    return rc_has_los(&world->map, p->x, p->y, tx, ty, p->plane);
}

static int player_can_attack_npc_now(const RcWorld *world, const RcPlayer *p,
                                     const RcNpc *npc, int range,
                                     RcCombatStyle style, int *distance,
                                     int *los) {
    int dist = distance_player_to_npc(p, npc);
    int line = has_player_attack_los(world, p, npc, style);
    if (distance) *distance = dist;
    if (los) *los = line;
    if (style_is_melee(style) && point_in_npc_footprint(npc, p->x, p->y)) {
        return 0;
    }
    return dist <= range && line;
}

static void set_player_route(RcPlayer *p, const RcRoute *route) {
    if (!p || !route || route->length <= 0) return;
    int n = route->length < RC_MAX_ROUTE ? route->length : RC_MAX_ROUTE;
    for (int i = 0; i < n; i++) {
        p->route_x[i] = route->waypoints_x[i];
        p->route_y[i] = route->waypoints_y[i];
    }
    p->route_len = n;
    p->route_idx = 0;
}

static int choose_player_attack_tile(const RcWorld *world, const RcPlayer *p,
                                     const RcNpc *npc, int range,
                                     RcCombatStyle style,
                                     RcRoute *out_route) {
    int size = npc_footprint_size(npc);
    int min_x = npc->x;
    int min_y = npc->y;
    int max_x = npc->x + size - 1;
    int max_y = npc->y + size - 1;
    int scan = range > 1 ? range : 1;
    int best_score = 0x7FFFFFFF;
    RcRoute best_route;
    memset(&best_route, 0, sizeof(best_route));
    int found = 0;
    for (int y = min_y - scan; y <= max_y + scan; y++) {
        for (int x = min_x - scan; x <= max_x + scan; x++) {
            if (x >= min_x && x <= max_x && y >= min_y && y <= max_y) {
                continue;
            }
            int tx, ty;
            if (x < min_x) tx = min_x;
            else if (x > max_x) tx = max_x;
            else tx = x;
            if (y < min_y) ty = min_y;
            else if (y > max_y) ty = max_y;
            else ty = y;
            int dist = chebyshev(x, y, tx, ty);
            if (dist > range) continue;
            if (style_is_melee(style) && dist != 1) continue;
            if (rc_tile_blocked(&world->map, x, y, p->plane)) continue;
            if (style_needs_los(style) &&
                    !rc_has_los(&world->map, x, y, tx, ty, p->plane)) {
                continue;
            }
            RcRoute route = {0};
            if (p->x != x || p->y != y) {
                route = rc_find_path(&world->map, p->x, p->y, x, y,
                                     1, p->plane, false);
                if (!route.success || route.alternative ||
                        route.length <= 0) {
                    continue;
                }
            } else {
                route.success = true;
                route.length = 0;
            }
            int score = route.length * 4 + chebyshev(p->x, p->y, x, y);
            if (score < best_score) {
                best_score = score;
                best_route = route;
                found = 1;
            }
        }
    }
    if (!found) return 0;
    *out_route = best_route;
    return 1;
}

static void route_player_toward_npc(RcWorld *world, RcPlayer *p,
                                    const RcNpc *npc, int range) {
    RcRoute route = {0};
    if (!choose_player_attack_tile(world, p, npc, range, p->combat_style,
                                   &route)) {
        p->route_len = 0;
        p->route_idx = 0;
        return;
    }
    set_player_route(p, &route);
}

static int npc_can_attack_player_now(const RcWorld *world, const RcNpc *npc,
                                     const RcPlayer *p, int range,
                                     RcCombatStyle style, int *distance,
                                     int *los) {
    int tx, ty;
    nearest_npc_tile_to_point(npc, p->x, p->y, &tx, &ty);
    int dist = chebyshev(p->x, p->y, tx, ty);
    int line = style_needs_los(style)
               ? rc_has_los(&world->map, tx, ty, p->x, p->y, npc->plane)
               : 1;
    if (distance) *distance = dist;
    if (los) *los = line;
    return dist <= range && line;
}

static void step_npc_toward_player(RcWorld *world, RcNpc *npc,
                                   const RcPlayer *p) {
    int tx, ty;
    nearest_npc_tile_to_point(npc, p->x, p->y, &tx, &ty);
    int sx = 0, sy = 0;
    if (p->x > tx) sx = 1;
    else if (p->x < tx) sx = -1;
    if (p->y > ty) sy = 1;
    else if (p->y < ty) sy = -1;
    if (!sx && !sy && point_in_npc_footprint(npc, p->x, p->y)) {
        if (p->x <= npc->x) sx = -1;
        else sx = 1;
        if (p->y <= npc->y) sy = -1;
        else sy = 1;
    }
    int dx = 0, dy = 0;
    if ((sx || sy) && rc_can_move(&world->map, npc->x, npc->y,
                                  sx, sy, npc->plane)) {
        dx = sx;
        dy = sy;
    } else if (sx && rc_can_move(&world->map, npc->x, npc->y,
                                 sx, 0, npc->plane)) {
        dx = sx;
    } else if (sy && rc_can_move(&world->map, npc->x, npc->y,
                                 0, sy, npc->plane)) {
        dy = sy;
    }
    if (dx || dy) {
        npc->prev_x = npc->x;
        npc->prev_y = npc->y;
        npc->x += dx;
        npc->y += dy;
        face_npc_to_player(npc, p);
    }
}

static void step_npc_toward_tile(RcWorld *world, RcNpc *npc, int tx, int ty) {
    int sx = 0, sy = 0;
    if (tx > npc->x) sx = 1;
    else if (tx < npc->x) sx = -1;
    if (ty > npc->y) sy = 1;
    else if (ty < npc->y) sy = -1;
    int dx = 0, dy = 0;
    if ((sx || sy) && rc_can_move(&world->map, npc->x, npc->y,
                                  sx, sy, npc->plane)) {
        dx = sx;
        dy = sy;
    } else if (sx && rc_can_move(&world->map, npc->x, npc->y,
                                 sx, 0, npc->plane)) {
        dx = sx;
    } else if (sy && rc_can_move(&world->map, npc->x, npc->y,
                                 0, sy, npc->plane)) {
        dy = sy;
    }
    if (dx || dy) {
        npc->prev_x = npc->x;
        npc->prev_y = npc->y;
        npc->x += dx;
        npc->y += dy;
        face_npc_move_delta(npc, dx, dy);
    }
}

static int npc_spawn_distance(const RcNpc *npc) {
    if (npc->spawn_x == 0 && npc->spawn_y == 0) return 0;
    int dx = npc->x - npc->spawn_x;
    int dy = npc->y - npc->spawn_y;
    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;
    return dx > dy ? dx : dy;
}

static int npc_leash_range(const RcNpcDef *def) {
    int base = def->aggro_range > 0 ? def->aggro_range : def->wander_range;
    if (base < 1) base = 1;
    return base + 8;
}

static int npc_should_auto_attack_player(const RcWorld *world,
                                         const RcNpc *npc,
                                         const RcNpcDef *def) {
    if (!world || !npc || !def || !def->aggressive) return 0;
    if (!npc_can_retaliate(npc)) return 0;
    if (world->player.current_hp <= 0 || world->player.plane != npc->plane) {
        return 0;
    }
    int range = def->aggro_range > 0 ? def->aggro_range : def->wander_range;
    if (range <= 0) range = 1;
    if (distance_player_to_npc(&world->player, npc) > range) return 0;
    RcCombatActorRef npc_actor = {
        .kind = RC_COMBAT_ACTOR_NPC,
        .uid = npc->uid,
    };
    RcCombatActorRef player_actor = {
        .kind = RC_COMBAT_ACTOR_PLAYER,
        .uid = 0,
    };
    return combat_can_attack_target((RcWorld *)world, npc_actor,
                                    player_actor);
}

static int npc_should_drop_target(const RcWorld *world, const RcNpc *npc,
                                  const RcNpcDef *def) {
    if (!world || !npc || !def) return 1;
    if (!def->aggressive && npc->combat.aggro_state == 0) return 0;
    if (world->player.current_hp <= 0 || world->player.plane != npc->plane) {
        return 1;
    }
    int leash = npc_leash_range(def);
    return npc_spawn_distance(npc) > leash ||
           distance_player_to_npc(&world->player, npc) > leash;
}

void rc_award_player_combat_xp(RcWorld *world, int damage) {
    if (!world || damage <= 0) return;
    RcPlayer *p = &world->player;
    int mask = p->combat_xp_mask;
    int count = 0;
    const int skills[] = {
        SKILL_ATTACK, SKILL_STRENGTH, SKILL_DEFENCE,
        SKILL_RANGED, SKILL_MAGIC,
    };
    for (int i = 0; i < (int)(sizeof(skills) / sizeof(skills[0])); i++) {
        if (mask & (1u << skills[i])) count++;
    }
    if (count <= 0) return;
    int combat_xp = damage * 4;
    int each = combat_xp / count;
    if (each <= 0) each = 1;
    for (int i = 0; i < (int)(sizeof(skills) / sizeof(skills[0])); i++) {
        if (mask & (1u << skills[i]))
            rc_add_xp(&p->skills, (RcSkill)skills[i], each);
    }
    int hp_xp = (damage * 4) / 3;
    if (hp_xp <= 0) hp_xp = 1;
    rc_add_xp(&p->skills, SKILL_HITPOINTS, hp_xp);
}

static RcNpc *find_npc_by_uid(struct RcWorld *world, int uid) {
    for (int i = 0; i < world->npc_count; i++) {
        if (world->npcs[i].active && world->npcs[i].uid == uid) {
            return &world->npcs[i];
        }
    }
    return NULL;
}

static RcCombatActorRef combat_actor_none(void) {
    RcCombatActorRef actor;
    actor.kind = RC_COMBAT_ACTOR_NONE;
    actor.uid = -1;
    return actor;
}

static int combat_actor_equal(RcCombatActorRef a, RcCombatActorRef b) {
    return a.kind == b.kind && a.uid == b.uid;
}

static int combat_actor_live(RcWorld *world, RcCombatActorRef actor) {
    if (!world) return 0;
    if (actor.kind == RC_COMBAT_ACTOR_PLAYER) {
        return actor.uid == 0 && world->player.current_hp > 0;
    }
    if (actor.kind == RC_COMBAT_ACTOR_NPC) {
        RcNpc *npc = find_npc_by_uid(world, actor.uid);
        return npc && !npc->is_dead && !npc->player_untargetable;
    }
    return 0;
}

static RcCombatActorState *combat_state_for_actor(RcWorld *world,
                                                  RcCombatActorRef actor) {
    if (!world) return NULL;
    if (actor.kind == RC_COMBAT_ACTOR_PLAYER && actor.uid == 0) {
        return &world->player.combat;
    }
    if (actor.kind == RC_COMBAT_ACTOR_NPC) {
        RcNpc *npc = find_npc_by_uid(world, actor.uid);
        return npc ? &npc->combat : NULL;
    }
    return NULL;
}

static int npc_can_retaliate(const RcNpc *npc) {
    const RcNpcDef *def = rc_npc_def_for_npc(npc);
    if (!npc || npc->is_dead || npc->player_untargetable || !def) {
        return 0;
    }
    return def->attack_speed > 0 && def->max_hit > 0;
}

static void combat_register_attacker(RcCombatActorState *target,
                                     RcCombatActorRef attacker) {
    if (!target || attacker.kind == RC_COMBAT_ACTOR_NONE ||
            attacker.uid < 0) {
        return;
    }
    target->primary_attacker = attacker;
    target->under_attack_timer = 10;
    for (int i = 0; i < target->attacker_count; i++) {
        if (combat_actor_equal(target->attackers[i], attacker)) return;
    }
    if (target->attacker_count < RC_MAX_COMBAT_ATTACKERS) {
        target->attackers[target->attacker_count++] = attacker;
    } else {
        target->attackers[RC_MAX_COMBAT_ATTACKERS - 1] = attacker;
    }
}

static int combat_target_has_other_live_attacker(RcWorld *world,
                                                 RcCombatActorRef target,
                                                 RcCombatActorRef attacker) {
    RcCombatActorState *state = combat_state_for_actor(world, target);
    if (!state || state->under_attack_timer <= 0) return 0;
    for (int i = 0; i < state->attacker_count; i++) {
        RcCombatActorRef other = state->attackers[i];
        if (combat_actor_equal(other, attacker)) continue;
        if (combat_actor_live(world, other)) return 1;
    }
    return 0;
}

static int combat_can_attack_target(RcWorld *world, RcCombatActorRef attacker,
                                    RcCombatActorRef target) {
    if (!world || world->multi_combat) return 1;
    if (!combat_actor_live(world, attacker)) return 0;
    if (!combat_actor_live(world, target)) return 0;
    return !combat_target_has_other_live_attacker(world, target, attacker);
}

static int player_locked_by_other_npc(RcWorld *world, int npc_uid) {
    if (!world || world->multi_combat ||
            world->player.combat.under_attack_timer <= 0) {
        return 0;
    }
    RcCombatActorRef allowed = {
        .kind = RC_COMBAT_ACTOR_NPC,
        .uid = npc_uid,
    };
    for (int i = 0; i < world->player.combat.attacker_count; i++) {
        RcCombatActorRef other = world->player.combat.attackers[i];
        if (combat_actor_equal(other, allowed)) continue;
        if (combat_actor_live(world, other)) return 1;
    }
    return 0;
}

static RcCombatTargetRef combat_target_none(void) {
    RcCombatTargetRef target;
    memset(&target, 0, sizeof(target));
    target.kind = RC_COMBAT_ACTOR_NONE;
    target.uid = -1;
    target.definition_id = -1;
    target.generation = 0;
    target.tile_x = -1;
    target.tile_y = -1;
    target.plane = -1;
    target.footprint_width = 0;
    target.footprint_height = 0;
    return target;
}

static int npc_footprint_size(const RcNpc *npc) {
    const RcNpcDef *def = rc_npc_def_for_npc(npc);
    if (!def) return 1;
    int size = def->size;
    return size > 0 ? size : 1;
}

static RcCombatTargetRef combat_target_from_npc(const RcNpc *npc) {
    RcCombatTargetRef target = combat_target_none();
    const RcNpcDef *def = rc_npc_def_for_npc(npc);
    if (!npc || !def) {
        return target;
    }
    int size = npc_footprint_size(npc);
    target.kind = RC_COMBAT_ACTOR_NPC;
    target.uid = npc->uid;
    target.definition_id = def->id;
    target.tile_x = npc->x;
    target.tile_y = npc->y;
    target.plane = npc->plane;
    target.footprint_width = size;
    target.footprint_height = size;
    return target;
}

static RcCombatTargetRef combat_target_from_player(const RcPlayer *player) {
    RcCombatTargetRef target = combat_target_none();
    if (!player) return target;
    target.kind = RC_COMBAT_ACTOR_PLAYER;
    target.uid = 0;
    target.definition_id = 0;
    target.tile_x = player->x;
    target.tile_y = player->y;
    target.plane = player->plane;
    target.footprint_width = 1;
    target.footprint_height = 1;
    return target;
}

static void combat_copy_pending_hits(RcCombatActorState *state,
                                     const RcPendingHit *hits, int count) {
    if (!state) return;
    if (count < 0) count = 0;
    if (count > RC_MAX_PENDING_HITS) count = RC_MAX_PENDING_HITS;
    if (count > 0) {
        memcpy(state->pending_hits, hits,
               (size_t)count * sizeof(state->pending_hits[0]));
    }
    state->num_pending_hits = count;
}

void rc_combat_init_player_state(RcPlayer *player) {
    if (!player) return;
    bool special_pending = player->combat.special_pending;
    memset(&player->combat, 0, sizeof(player->combat));
    player->combat.target = combat_target_none();
    player->combat.primary_attacker = combat_actor_none();
    player->combat.distance_to_target = -1;
    player->combat.selected_style_idx = player->attack_style_idx;
    player->combat.style = player->combat_style;
    player->combat.stance = player->attack_stance;
    player->combat.xp_mask = player->combat_xp_mask;
    player->combat.special_energy = player->special_energy;
    player->combat.special_pending = special_pending;
    player->combat.auto_retaliate = player->auto_retaliate;
}

void rc_combat_init_npc_state(RcNpc *npc) {
    if (!npc) return;
    memset(&npc->combat, 0, sizeof(npc->combat));
    npc->combat.target = combat_target_none();
    npc->combat.primary_attacker = combat_actor_none();
    npc->combat.distance_to_target = -1;
    npc->combat.selected_npc_style = COMBAT_NONE;
    npc->combat.last_npc_style = COMBAT_NONE;
}

static void sync_player_combat_state_from_legacy(RcWorld *world,
                                                 RcPlayer *player) {
    if (!world || !player) return;
    bool special_pending = player->combat.special_pending;
    uint32_t flags = 0;
    player->combat.active = false;
    player->combat.target = combat_target_none();
    player->combat.attack_cooldown = player->attack_timer;
    player->combat.action_delay = player->attack_timer;
    player->combat.attack_range = 0;
    player->combat.distance_to_target = -1;
    player->combat.line_of_sight = 0;
    player->combat.hp_current = player->current_hp;
    player->combat.hp_max = player->max_hp;
    player->combat.in_multi_combat = world->multi_combat;
    player->combat.last_hit_timer = player->last_hit_timer;
    player->combat.selected_style_idx = player->attack_style_idx;
    player->combat.style = player->combat_style;
    player->combat.stance = player->attack_stance;
    player->combat.xp_mask = player->combat_xp_mask;
    player->combat.special_energy = player->special_energy;
    player->combat.special_pending = special_pending;
    player->combat.auto_retaliate = player->auto_retaliate;
    combat_copy_pending_hits(&player->combat, player->pending_hits,
                             player->num_pending_hits);
    if (player->attack_target >= 0) {
        RcNpc *target = find_npc_by_uid(world, player->attack_target);
        if (!target || !rc_npc_def_for_npc(target)) {
            flags |= RC_COMBAT_STATE_INVALID_TARGET;
        } else if (target->is_dead) {
            player->combat.target = combat_target_from_npc(target);
            flags |= RC_COMBAT_STATE_DEAD_TARGET;
        } else {
            int range = rc_player_attack_range(player);
            int distance = -1;
            int los = 0;
            player->combat.active = true;
            player->combat.target = combat_target_from_npc(target);
            player->combat.attack_range = range;
            if (player_can_attack_npc_now(world, player, target, range,
                                          player->combat_style, &distance,
                                          &los)) {
                flags |= RC_COMBAT_STATE_IN_RANGE;
            }
            player->combat.distance_to_target = distance;
            player->combat.line_of_sight = los;
            flags |= RC_COMBAT_STATE_ACTIVE | RC_COMBAT_STATE_STARTED;
            if (player->attack_timer > 0) {
                flags |= RC_COMBAT_STATE_WAITING_FOR_COOLDOWN;
            }
        }
    }
    player->combat.flags = flags;
}

static void sync_npc_combat_state_from_legacy(RcWorld *world, RcNpc *npc) {
    if (!world || !npc) return;
    uint32_t flags = 0;
    npc->combat.active = false;
    npc->combat.target = combat_target_none();
    npc->combat.attack_cooldown = npc->attack_timer;
    npc->combat.action_delay = npc->attack_timer;
    npc->combat.attack_range = 0;
    npc->combat.distance_to_target = -1;
    npc->combat.line_of_sight = 0;
    npc->combat.hp_current = npc->current_hp;
    const RcNpcDef *def = rc_npc_def_for_npc(npc);
    npc->combat.hp_max = def ? def->hitpoints : 0;
    npc->combat.retaliates = npc_can_retaliate(npc);
    npc->combat.in_multi_combat = world->multi_combat;
    npc->combat.last_hit_timer = npc->last_hit_timer;
    npc->combat.attack_count = npc->attack_count;
    combat_copy_pending_hits(&npc->combat, npc->pending_hits,
                             npc->num_pending_hits);
    if (npc->target_uid >= 0) {
        if (npc->target_uid != 0) {
            flags |= RC_COMBAT_STATE_INVALID_TARGET;
        } else if (world->player.current_hp <= 0) {
            npc->combat.target = combat_target_from_player(&world->player);
            flags |= RC_COMBAT_STATE_DEAD_TARGET;
        } else if (!def) {
            flags |= RC_COMBAT_STATE_INVALID_TARGET;
        } else {
            RcCombatStyle style = npc->combat.selected_npc_style;
            if (style == COMBAT_NONE) {
                style = rc_combat_npc_preferred_style(def->attack_types);
            }
            int range = content_npc_attack_range(
                world, npc, style, default_npc_attack_range(def, style));
            int distance = -1;
            int los = 0;
            npc->combat.active = true;
            npc->combat.target = combat_target_from_player(&world->player);
            npc->combat.selected_npc_style = style;
            npc->combat.attack_range = range;
            if (npc_can_attack_player_now(world, npc, &world->player, range,
                                          style, &distance, &los)) {
                flags |= RC_COMBAT_STATE_IN_RANGE;
            }
            npc->combat.distance_to_target = distance;
            npc->combat.line_of_sight = los;
            flags |= RC_COMBAT_STATE_ACTIVE | RC_COMBAT_STATE_STARTED;
            if (npc->attack_timer > 0) {
                flags |= RC_COMBAT_STATE_WAITING_FOR_COOLDOWN;
            }
        }
    }
    npc->combat.flags = flags;
}

int rc_combat_start_player_vs_npc(struct RcWorld *world, int player_uid,
                                  int npc_uid) {
    if (!world || player_uid != 0) return 0;
    RcNpc *npc = find_npc_by_uid(world, npc_uid);
    const RcNpcDef *def = rc_npc_def_for_npc(npc);
    if (!npc || npc->is_dead || npc->player_untargetable || !def) {
        return 0;
    }
    if (player_locked_by_other_npc(world, npc->uid)) return 0;
    RcCombatActorRef player_actor = {
        .kind = RC_COMBAT_ACTOR_PLAYER,
        .uid = 0,
    };
    RcCombatActorRef npc_actor = {
        .kind = RC_COMBAT_ACTOR_NPC,
        .uid = npc->uid,
    };
    if (!combat_can_attack_target(world, player_actor, npc_actor)) return 0;
    RcPlayer *player = &world->player;
    player->attack_target = npc->uid;
    player->attack_target_def_id = def->id;
    if (npc_can_retaliate(npc) &&
            combat_can_attack_target(world, npc_actor, player_actor)) {
        npc->target_uid = 0;
        combat_register_attacker(&player->combat, npc_actor);
    }
    combat_register_attacker(&npc->combat, player_actor);
    rc_refresh_player_combat_style(player);
    face_player_to_npc(player, npc);
    face_npc_to_player(npc, player);
    sync_player_combat_state_from_legacy(world, player);
    sync_npc_combat_state_from_legacy(world, npc);
    return 1;
}

int rc_combat_start_npc_vs_player(struct RcWorld *world, int npc_uid,
                                  int player_uid) {
    if (!world || player_uid != 0) return 0;
    RcNpc *npc = find_npc_by_uid(world, npc_uid);
    if (!npc || npc->is_dead || npc->player_untargetable ||
            !rc_npc_def_for_npc(npc)) {
        return 0;
    }
    if (!npc_can_retaliate(npc)) return 0;
    RcCombatActorRef npc_actor = {
        .kind = RC_COMBAT_ACTOR_NPC,
        .uid = npc->uid,
    };
    RcCombatActorRef player_actor = {
        .kind = RC_COMBAT_ACTOR_PLAYER,
        .uid = 0,
    };
    if (!combat_can_attack_target(world, npc_actor, player_actor)) return 0;
    npc->target_uid = 0;
    combat_register_attacker(&world->player.combat, npc_actor);
    face_npc_to_player(npc, &world->player);
    sync_npc_combat_state_from_legacy(world, npc);
    sync_player_combat_state_from_legacy(world, &world->player);
    return 1;
}

void rc_combat_stop_actor(struct RcWorld *world, RcCombatActorRef actor,
                          int reason) {
    (void)reason;
    if (!world) return;
    if (actor.kind == RC_COMBAT_ACTOR_PLAYER && actor.uid == 0) {
        world->player.attack_target = -1;
        world->player.attack_target_def_id = -1;
        rc_combat_init_player_state(&world->player);
        world->player.combat.flags = RC_COMBAT_STATE_CANCELLED;
    } else if (actor.kind == RC_COMBAT_ACTOR_NPC) {
        RcNpc *npc = find_npc_by_uid(world, actor.uid);
        if (!npc) return;
        npc->target_uid = -1;
        rc_combat_init_npc_state(npc);
        npc->combat.flags = RC_COMBAT_STATE_CANCELLED;
    }
}

void rc_combat_tick_world(struct RcWorld *world) {
    if (!world) return;
    for (int i = 0; i < world->npc_count; i++) {
        if (world->npcs[i].active) rc_combat_tick_npc(world, &world->npcs[i]);
    }
    rc_combat_tick_player(world);
}

void rc_combat_set_player_style(struct RcWorld *world, int style_idx) {
    if (!world) return;
    rc_player_set_attack_style(world, style_idx);
    sync_player_combat_state_from_legacy(world, &world->player);
}

void rc_combat_toggle_auto_retaliate(struct RcWorld *world) {
    if (!world) return;
    world->player.auto_retaliate = !world->player.auto_retaliate;
    sync_player_combat_state_from_legacy(world, &world->player);
}

void rc_combat_toggle_special(struct RcWorld *world) {
    if (!world) return;
    world->player.combat.special_pending =
        !world->player.combat.special_pending;
    sync_player_combat_state_from_legacy(world, &world->player);
}

int rc_combat_actor_has_target(const RcCombatActorState *state) {
    return state && state->active &&
           state->target.kind != RC_COMBAT_ACTOR_NONE &&
           state->target.uid >= 0;
}

static void combat_copy_hit_view(RcCombatHitView *dst,
                                 const RcCombatRecentHit *src) {
    dst->damage = src->damage;
    dst->max_hit = src->max_hit;
    dst->style = src->style;
    dst->source_uid = src->source_uid;
    dst->timer = src->timer;
    dst->hit_type = src->hit_type;
    dst->flags = src->flags;
}

int rc_combat_get_player_view(const struct RcWorld *world,
                              RcCombatViewState *out) {
    if (!world || !out) return 0;
    memset(out, 0, sizeof(*out));
    const RcPlayer *p = &world->player;
    out->selected_style_idx = p->combat.selected_style_idx;
    out->weapon_category = p->combat.weapon_category;
    out->attack_type = p->combat.attack_type;
    out->combat_class = p->combat.combat_class;
    out->style = p->combat.style;
    out->stance = p->combat.stance;
    out->xp_mask = p->combat.xp_mask;
    out->auto_retaliate = p->combat.auto_retaliate ? 1 : 0;
    out->special_pending = p->combat.special_pending ? 1 : 0;
    out->special_energy = p->combat.special_energy;
    out->attack_range = p->combat.attack_range;
    out->attack_speed = rc_player_attack_speed(p);
    out->attack_cooldown = p->combat.attack_cooldown;
    out->target = p->combat.target;
    out->player_hp_current = p->combat.hp_current;
    out->player_hp_max = p->combat.hp_max;
    out->player_recent_hit_count = p->combat.recent_hit_count;
    if (out->player_recent_hit_count > 4) out->player_recent_hit_count = 4;
    for (int i = 0; i < out->player_recent_hit_count; i++) {
        combat_copy_hit_view(&out->player_recent_hits[i],
                             &p->combat.recent_hits[i]);
    }
    if (out->target.kind == RC_COMBAT_ACTOR_NPC && out->target.uid >= 0) {
        RcWorld *mutable_world = (RcWorld *)world;
        RcNpc *npc = find_npc_by_uid(mutable_world, out->target.uid);
        if (npc) {
            out->target_hp_current = npc->combat.hp_current;
            out->target_hp_max = npc->combat.hp_max;
            out->target_recent_hit_count = npc->combat.recent_hit_count;
            if (out->target_recent_hit_count > 4)
                out->target_recent_hit_count = 4;
            for (int i = 0; i < out->target_recent_hit_count; i++) {
                combat_copy_hit_view(&out->target_recent_hits[i],
                                     &npc->combat.recent_hits[i]);
            }
        }
    }
    return 1;
}

const RcCombatAttackEvent *rc_combat_attack_events(
    const struct RcWorld *world, int *count) {
    if (count) *count = world ? world->combat_attack_event_count : 0;
    return world ? world->combat_attack_events : NULL;
}

void rc_combat_clear_attack_events(struct RcWorld *world) {
    if (!world) return;
    world->combat_attack_event_count = 0;
}

void rc_combat_set_multi_combat(struct RcWorld *world, bool enabled) {
    if (!world) return;
    world->multi_combat = enabled;
    sync_player_combat_state_from_legacy(world, &world->player);
    for (int i = 0; i < world->npc_count; i++) {
        if (world->npcs[i].active) {
            sync_npc_combat_state_from_legacy(world, &world->npcs[i]);
        }
    }
}

int rc_combat_is_multi_combat(const struct RcWorld *world) {
    return world && world->multi_combat;
}

int rc_combat_actor_attacker_count(const RcCombatActorState *state) {
    return state ? state->attacker_count : 0;
}

int rc_combat_actor_is_under_attack(const RcCombatActorState *state) {
    return state && state->under_attack_timer > 0 &&
           state->attacker_count > 0;
}

void rc_combat_actor_register_attacker(RcCombatActorState *target,
                                       RcCombatActorRef attacker) {
    combat_register_attacker(target, attacker);
}

void rc_combat_tick_actor_threat(RcCombatActorState *state) {
    if (!state) return;
    if (state->under_attack_timer > 0) state->under_attack_timer--;
    if (state->under_attack_timer > 0) return;
    state->primary_attacker = combat_actor_none();
    state->attacker_count = 0;
}

static void combat_tick_player_legacy(struct RcWorld *world) {
    RcPlayer *p = &world->player;
    if (p->attack_timer > 0) p->attack_timer--;
    if (p->attack_target < 0) return;
    RcNpc *target = find_npc_by_uid(world, p->attack_target);
    if (!target || target->is_dead) {
        p->attack_target = -1;
        p->attack_target_def_id = -1;
        return;
    }
    const RcNpcDef *target_def = rc_npc_def_for_npc(target);
    if (!target_def) {
        p->attack_target = -1;
        p->attack_target_def_id = -1;
        return;
    }
    if (p->attack_target_def_id >= 0 &&
            p->attack_target_def_id != target_def->id) {
        p->attack_target = -1;
        p->attack_target_def_id = -1;
        return;
    }
    if (target->player_untargetable) return;
    if (!rc_encounter_player_can_target_npc(world, (uint16_t)target->uid)) {
        return;
    }
    rc_refresh_player_combat_style(p);
    face_player_to_npc(p, target);
    int range = rc_player_attack_range(p);
    if (!player_can_attack_npc_now(world, p, target, range,
                                   p->combat_style, NULL, NULL)) {
        route_player_toward_npc(world, p, target, range);
        return;
    }
    if (p->attack_timer > 0) return;

    int weapon_id = p->equipment[EQUIP_WEAPON].item_id;
    const RcSpellDef *spell = NULL;
    if (p->combat_style == COMBAT_RANGED &&
            !player_has_ranged_resource(p)) {
        return;
    }
    if (p->combat_style == COMBAT_MAGIC) {
        spell = player_selected_combat_spell(p);
        if (!spell || !content_player_has_spell_runes(world, p, spell)) {
            clear_failed_player_spell_attack(p);
            return;
        }
    }

    bool use_special = false;
    int special_cost = 0;
    if (p->combat.special_pending) {
        special_cost = content_player_special_energy_cost(world, p, target,
                                                          weapon_id);
        if (special_cost <= 0) {
            p->combat.special_pending = false;
        } else if (p->special_energy < special_cost) {
            p->combat.special_pending = false;
            return;
        } else {
            use_special = true;
        }
    }

    // Pick the calc based on current combat style.
    int def_idx = target->def_id;
    RcCombatCalc calc;
    switch (p->combat_style) {
        case COMBAT_RANGED:
            calc = rc_calc_ranged(p, def_idx); break;
        case COMBAT_MAGIC:
            calc = rc_calc_magic(p, def_idx, spell->max_hit);
            break;
        default:
            calc = rc_calc_melee(p, def_idx); break;
    }
    int dmg = rc_roll_attack(&calc, &world->rng_state);
    if (target->force_player_max_hit)
        dmg = calc.max_hit;
    if (use_special) {
        p->special_energy -= special_cost;
        if (p->special_energy < 0) p->special_energy = 0;
        p->combat.special_pending = false;
        p->combat.special_energy = p->special_energy;
        dmg = content_modify_player_special_damage(
            world, p, target, weapon_id, p->combat_style, dmg, calc.max_hit);
    }
    dmg = rc_combat_apply_regular_npc_player_damage_rules(world, target, dmg);
    dmg = rc_encounter_scale_player_damage(world, (uint16_t)target->uid,
                                           p->combat_style, dmg);
    int spell_idx = p->manual_spell_cast >= 0
                  ? p->manual_spell_cast : p->autocast_spell;
    RcPlayerAttackProfiles profiles =
        select_player_attack_profiles(p, spell, spell_idx, weapon_id,
                                      use_special);
    const RcCombatProfileDef *timing_profile =
        player_timing_profile(&profiles);
    int delay = profile_hit_delay(
        timing_profile, rc_combat_hit_delay_for_style(p->combat_style));

    if (p->combat_style == COMBAT_RANGED &&
            !player_consume_ranged_resource(p)) {
        return;
    }
    if (p->combat_style == COMBAT_MAGIC &&
            !content_player_consume_spell_runes(world, p, spell)) {
        clear_failed_player_spell_attack(p);
        return;
    }

    emit_player_attack_event(world, p, target, spell, spell_idx, weapon_id,
                             use_special, &profiles, delay);

    // NPCs don't flick prayer (overhead prayer is static per phase),
    // so the snapshot is the NPC's phase prayer flags. For now,
    // NPC overhead prayer is not modelled — snapshot = 0.
    rc_queue_hit_meta(target->pending_hits, &target->num_pending_hits,
                      dmg, delay, p->combat_style,
                      -1 /* player source */,
                      0u /* NPC prayer snapshot */, world->tick,
                      0, calc.max_hit);
    p->manual_spell_cast = -1;
    p->attack_timer = rc_player_attack_speed(p);
    target->target_uid = 0;
    RcPayloadPlayerAttack payload = {
        .target_npc_id = (uint16_t)target->uid,
        .style = (uint8_t)p->combat_style,
    };
    rc_event_fire(world, RC_EVT_PLAYER_ATTACK, &payload);
}

static void combat_tick_npc_legacy(struct RcWorld *world, RcNpc *npc) {
    if (npc->is_dead || !npc->active) return;
    if (npc->player_untargetable) return;
    const RcNpcDef *d = rc_npc_def_for_npc(npc);
    if (!d) return;
    if (npc->target_uid < 0) {
        if (npc_should_auto_attack_player(world, npc, d)) {
            rc_combat_start_npc_vs_player(world, npc->uid, 0);
        } else {
            int spawn_dist = npc_spawn_distance(npc);
            if (spawn_dist > npc_leash_range(d))
                npc->combat.leash_state = 1;
            if (npc->combat.leash_state && spawn_dist > 0) {
                step_npc_toward_tile(world, npc, npc->spawn_x, npc->spawn_y);
                if (npc_spawn_distance(npc) == 0)
                    npc->combat.leash_state = 0;
            } else if (spawn_dist == 0) {
                npc->combat.leash_state = 0;
            }
            return;
        }
    }
    // We only support targeting the player for now.
    RcPlayer *p = &world->player;
    if (npc->target_uid != 0 /* placeholder player uid */) return;
    if (npc_should_drop_target(world, npc, d)) {
        npc->target_uid = -1;
        npc->combat.leash_state = 1;
        step_npc_toward_tile(world, npc, npc->spawn_x, npc->spawn_y);
        return;
    }
    if (d->attack_speed <= 0 || d->max_hit <= 0) return;

    int distance = distance_player_to_npc(p, npc);
    RcCombatStyle style = content_select_npc_style(
        world, npc, p, rc_combat_npc_preferred_style(d->attack_types));
    face_npc_to_player(npc, p);
    uint16_t enc_max_hit = 0;
    uint16_t enc_min_hit = 0;
    uint32_t enc_flags = 0;
    uint8_t enc_style = (uint8_t)style;
    if (rc_encounter_select_npc_attack(world, (uint16_t)npc->uid, distance,
                                       &enc_style, &enc_min_hit,
                                       &enc_max_hit, &enc_flags) >= 0) {
        style = (RcCombatStyle)enc_style;
        // Encounter data can override style/max-hit without adding
        // boss-specific branches here.
    }
    int range = content_npc_attack_range(
        world, npc, style, default_npc_attack_range(d, style));
    npc->combat.selected_npc_style = style;
    npc->combat.last_npc_style = style;
    npc->combat.aggro_state = d->aggressive ? 1 : 0;
    npc->combat.leash_state = 0;
    if (!npc_can_attack_player_now(world, npc, p, range, style, NULL, NULL)) {
        step_npc_toward_player(world, npc, p);
        if (npc->attack_timer > 0) npc->attack_timer--;
        return;
    }
    if (npc->attack_timer > 0) { npc->attack_timer--; return; }

    RcCombatCalc calc = rc_calc_npc_attack_style(npc->def_id, p, style);
    if (enc_max_hit > 0) calc.max_hit = enc_max_hit;
    int dmg = rc_roll_attack(&calc, &world->rng_state);
    if (dmg > 0 && enc_min_hit > 0 && dmg < enc_min_hit) dmg = enc_min_hit;
    dmg = content_modify_npc_roll_damage(world, npc, style, dmg);
    dmg = rc_combat_apply_regular_npc_attack_rules(world, npc, dmg);
    const RcCombatProfileDef *npc_profile =
        rc_combat_profile_for_npc(d->id, style);
    int delay = profile_hit_delay(npc_profile,
                                  rc_combat_hit_delay_for_style(style));
    npc->attack_count++;
    content_after_npc_swing(world, npc, style);

    // Prayer snapshot at queue tick — per FC lesson memory, protection
    // prayer must be active NOW (queue tick) to block this hit, even
    // if the player turns it off before impact.
    rc_queue_hit_meta(p->pending_hits, &p->num_pending_hits,
                      dmg, delay, style,
                      npc->uid,
                      (enc_flags & RC_ENC_ATTACK_PRAYER_IGNORABLE)
                      ? 0u : p->active_prayers,
                      world->tick, 0, calc.max_hit);
    emit_npc_attack_event(world, npc, d, p, style, delay);

    int speed = d->attack_speed;
    npc->attack_timer = content_modify_npc_attack_speed(world, npc, speed);
    RcPayloadNpcAttack payload = {
        .npc_id = (uint16_t)npc->uid,
        .style = (uint8_t)style,
    };
    rc_event_fire(world, RC_EVT_NPC_ATTACK, &payload);
}

void rc_combat_tick_player(struct RcWorld *world) {
    combat_tick_player_legacy(world);
    sync_player_combat_state_from_legacy(world, &world->player);
}

void rc_combat_tick_npc(struct RcWorld *world, RcNpc *npc) {
    combat_tick_npc_legacy(world, npc);
    sync_npc_combat_state_from_legacy(world, npc);
}

// Resolve pending hits on the player. Fires RC_EVT_PLAYER_DAMAGED
// per landing hit so subsystems (encounter, prayer debuffs, etc.)
// can react to the source + style + final (post-protection) damage.
void rc_resolve_player_hits(struct RcWorld *world) {
    RcPlayer *p = &world->player;
    int total = 0;
    for (int i = 0; i < p->num_pending_hits; i++) {
        RcPendingHit *h = &p->pending_hits[i];
        if (!h->active) continue;
        if (h->ticks_remaining > 0) { h->ticks_remaining--; continue; }

        int scale = rc_encounter_player_protection_scale_pct(
            world, h->source_idx, h->attack_style,
            (uint32_t)h->prayer_snapshot);
        int dmg = scale >= 0
                  ? (h->damage * scale) / 100
                  : rc_combat_apply_protection(
                        h->damage, h->attack_style,
                        (uint32_t)h->prayer_snapshot,
                        true /* player defender */);
        dmg = content_modify_incoming_after_protection(world, h, dmg);
        dmg = rc_encounter_scale_incoming_damage(world, h->source_idx,
                                                 h->attack_style, dmg);
        total += dmg;
        uint8_t hit_type = dmg <= 0 ? RC_HIT_TYPE_MISS :
                           (h->max_hit > 0 && dmg >= h->max_hit
                            ? RC_HIT_TYPE_MAX : RC_HIT_TYPE_NORMAL);
        rc_combat_actor_record_hit(&p->combat, dmg, h->max_hit,
                                   h->attack_style, h->source_idx,
                                   hit_type, h->flags, 4);
        if (h->source_idx >= 0) {
            RcCombatActorRef attacker = {
                .kind = RC_COMBAT_ACTOR_NPC,
                .uid = h->source_idx,
            };
            combat_register_attacker(&p->combat, attacker);
            if (dmg > 0 && p->auto_retaliate && p->attack_target < 0 &&
                    p->current_hp - total * 10 > 0) {
                rc_combat_start_player_vs_npc(world, 0, h->source_idx);
            }
        }
        if (dmg > 0) {
            p->last_hit = dmg;
            p->last_hit_timer = 4;
        }
        if ((h->flags & RC_HIT_SUPPRESS_ENCOUNTER_EFFECTS) == 0) {
            content_on_npc_hit_player(world, h, dmg);

            RcPayloadPlayerDamaged payload = {
                .source_npc_id = h->source_idx >= 0
                                 ? (uint16_t)h->source_idx
                                 : 0xFFFFu,   // 0xFFFF = self / non-NPC source
                .damage = (uint16_t)(dmg & 0xFFFF),
                .current_hp = p->current_hp - total * 10 > 0
                            ? (uint16_t)((p->current_hp - total * 10)
                                         & 0xFFFF)
                            : 0,
                .max_hp = (uint16_t)(p->max_hp & 0xFFFF),
                .style = (uint8_t)h->attack_style,
            };
            rc_event_fire(world, RC_EVT_PLAYER_DAMAGED, &payload);
        }
        h->active = 0;
    }
    int w = 0;
    for (int r = 0; r < p->num_pending_hits; r++) {
        if (p->pending_hits[r].active) {
            if (w != r) p->pending_hits[w] = p->pending_hits[r];
            w++;
        }
    }
    p->num_pending_hits = w;

    if (total > 0) {
        p->current_hp -= total * 10;   // hp stored in tenths
        if (p->current_hp < 0) p->current_hp = 0;
    }
    sync_player_combat_state_from_legacy(world, p);
}

void rc_combat_tick_player_status(struct RcWorld *world) {
    if (!world) return;
    RcPlayer *p = &world->player;
    if (p->last_hit_timer > 0) p->last_hit_timer--;
    rc_combat_actor_tick_recent_hits(&p->combat);
    rc_combat_tick_actor_threat(&p->combat);
    if (p->ward_of_arceuus_timer > 0) p->ward_of_arceuus_timer--;
    if (p->poison_damage > 0) {
        if (p->poison_tick_counter > 0) {
            p->poison_tick_counter--;
        } else {
            p->current_hp -= p->poison_damage * 10;
            if (p->current_hp < 0) p->current_hp = 0;
            p->poison_damage--;
            p->poison_tick_counter = p->poison_damage > 0 ? 30 : 0;
        }
    }
    if (p->venom_damage > 0) {
        if (p->venom_tick_counter > 0) {
            p->venom_tick_counter--;
        } else {
            p->current_hp -= p->venom_damage * 10;
            if (p->current_hp < 0) p->current_hp = 0;
            if (p->venom_damage < 20) p->venom_damage += 2;
            p->venom_tick_counter = 30;
        }
    }
    if (p->disease_tick_counter > 0) {
        p->disease_tick_counter--;
    }
    if (p->teleblock_timer > 0) p->teleblock_timer--;
    if (p->freeze_timer > 0) p->freeze_timer--;
    if (p->special_energy < RC_SPECIAL_ENERGY_MAX) {
        p->special_recover_counter++;
        if (p->special_recover_counter >= RC_SPECIAL_RECOVER_TICKS) {
            p->special_recover_counter = 0;
            p->special_energy += RC_SPECIAL_RECOVER_AMOUNT;
            if (p->special_energy > RC_SPECIAL_ENERGY_MAX) {
                p->special_energy = RC_SPECIAL_ENERGY_MAX;
            }
        }
    } else {
        p->special_energy = RC_SPECIAL_ENERGY_MAX;
        p->special_recover_counter = 0;
    }
    p->combat.special_energy = p->special_energy;
}
