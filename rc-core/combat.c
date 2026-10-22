#include "combat.h"
#include "consumables.h"
#include "combat_formula.h"
#include "combat_hit.h"
#include "combat_profiles.h"
#include "area_flags.h"
#include "encounter.h"
#include "events.h"
#include "items.h"
#include "npc.h"
#include "pathfinding.h"
#include "player_command.h"
#include "prayer.h"
#include "rng.h"
#include "skills.h"
#include "spells.h"
#include "types.h"
#include "world_state.h"
#include <limits.h>
#include <stddef.h>   // NULL
#include <string.h>
#include <stdio.h>

static int npc_can_retaliate(const RcWorld *world, const RcNpc *npc);
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

static int npc_footprint_size(const RcWorld *world, const RcNpc *npc);

static int npc_tile_bounds(const RcWorld *world, const RcNpc *npc,
                           RcTileBounds *bounds) {
    if (!npc || !bounds) return 0;
    int size = npc_footprint_size(world, npc);
    return rc_tile_bounds_from_origin_size(
        npc->x, npc->y, size, size, npc->plane, bounds);
}

static int nearest_npc_tile_to_point(const RcWorld *world, const RcNpc *npc,
                                     int px, int py,
                                     int *tx, int *ty) {
    RcTileBounds bounds;
    if (!tx || !ty || !npc_tile_bounds(world, npc, &bounds)) return 0;
    return rc_tile_rect_closest_point(&bounds.rect, px, py, tx, ty);
}

static int point_in_npc_footprint(const RcWorld *world, const RcNpc *npc,
                                  int x, int y) {
    RcTileBounds bounds;
    return npc_tile_bounds(world, npc, &bounds)
        && rc_tile_rect_contains(&bounds.rect, x, y);
}

static int distance_player_to_npc(const RcWorld *world, const RcPlayer *p,
                                  const RcNpc *npc) {
    int target_x, target_y;
    if (!p || !npc || p->plane != npc->plane
            || !nearest_npc_tile_to_point(
                world, npc, p->x, p->y, &target_x, &target_y)) {
        return INT_MAX;
    }
    int distance = rc_tile_distance(p->x, p->y, p->plane,
                                    target_x, target_y, npc->plane);
    return distance >= 0 ? distance : INT_MAX;
}

static int npc_projectile_source_tile(const RcWorld *world, const RcNpc *npc,
                                      const RcPlayer *target,
                                      RcCombatStyle style,
                                      int *sx,
                                      int *sy) {
    if (!npc || !target || !sx || !sy)
        return 0;
    (void)style;
    return nearest_npc_tile_to_point(world, npc, target->x, target->y,
                                     sx, sy);
}

// The client turns each actor toward its face entity's centre every frame.
static void face_player_to_npc(RcPlayer *p, const RcNpc *npc) {
    p->facing_entity = npc->uid;
}

static void face_npc_to_player(RcNpc *npc) {
    npc->facing_entity = 0;
}

static int style_needs_los(RcCombatStyle style) {
    return style == COMBAT_RANGED || style == COMBAT_MAGIC;
}

static int default_npc_attack_range(const RcNpcDef *def, RcCombatStyle style) {
    (void)def;
    if (style == COMBAT_MAGIC) return 10;
    if (style == COMBAT_RANGED) return 7;
    return 1;
}

static int content_npc_attack_range(const RcWorld *world, const RcNpc *npc,
                                    RcCombatStyle style, int default_range) {
    if (world && world->combat_hooks.npc_attack_range) {
        return world->combat_hooks.npc_attack_range(world, npc, style,
                                                    default_range);
    }
    return default_range;
}

// The prepared encounter attack, or NULL when there is none or the run that
// selected it has since ended.
static const RcEncounterAttack *prepared_encounter_attack(const RcWorld *world,
                                                          const RcNpc *npc) {
    if (!npc->combat.prepared_enc_attack) return NULL;
    uint32_t generation = 0;
    const RcEncounterAttack *attack = rc_encounter_attack(
        world, (RcNpcId)npc->uid, npc->combat.prepared_enc_attack - 1,
        &generation);
    return generation == npc->combat.prepared_enc_generation ? attack : NULL;
}

static int npc_attack_range_for(const RcWorld *world, const RcNpc *npc,
                                const RcNpcDef *def, RcCombatStyle style) {
    const RcEncounterAttack *attack = prepared_encounter_attack(world, npc);
    if (attack && attack->range) return attack->range;
    if (world->encounter.active_mask
            && rc_encounter_run_for_npc(world, (RcNpcId)npc->uid) >= 0)
        return default_npc_attack_range(def, style);
    return content_npc_attack_range(world, npc, style,
                                    default_npc_attack_range(def, style));
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

static int equipment_consume(RcWorld *world, int slot, int quantity) {
    if (!world || slot < 0 || slot >= RC_EQUIP_COUNT) return 0;
    RcItemTransaction tx;
    RcItemActionResult result = rc_item_tx_begin(&tx, world);
    if (result.code == RC_ITEM_RESULT_OK)
        result = rc_item_tx_remove_equipment(&tx, slot, quantity, UINT32_MAX);
    if (result.code == RC_ITEM_RESULT_OK) result = rc_item_tx_commit(&tx);
    return result.code == RC_ITEM_RESULT_OK;
}

static int player_ranged_resource_slot(const RcWorld *world,
                                       const char **failure_reason) {
    const RcPlayer *p = &world->player;
    if (world->combat_hooks.player_ranged_resource_slot)
        return world->combat_hooks.player_ranged_resource_slot(p, failure_reason);
    const RcInvSlot *weapon_slot = &p->equipment[EQUIP_WEAPON];
    const RcItemDef *weapon = rc_item_def_get(weapon_slot->item_id);
    if (weapon && weapon->stackable && weapon_slot->quantity > 0 &&
            weapon->equip_slot == EQUIP_WEAPON) {
        return EQUIP_WEAPON;
    }
    return EQUIP_AMMO;
}

static int inventory_has_spell_runes(const RcPlayer *p,
                                           const RcSpellDef *spell) {
    if (!p || !spell || spell->rune_count > RC_SPELL_MAX_RUNES) return 0;
    RcInvSlot staged[RC_INVENTORY_SIZE];
    memcpy(staged, p->inventory, sizeof(staged));
    for (int i = 0; i < spell->rune_count; i++) {
        int item_id = (int)spell->runes[i].item_id;
        int qty = (int)spell->runes[i].qty;
        if (item_id <= 0 || qty <= 0) return 0;
        for (int j = 0; j < RC_INVENTORY_SIZE && qty; j++)
            if (staged[j].item_id == item_id && !staged[j].state_id)
                qty -= rc_inv_remove_quantity(staged, j, qty);
        if (qty) return 0;
    }
    return 1;
}

int rc_combat_has_spell_runes(const RcWorld *world,
                                          const RcPlayer *p,
                                          const RcSpellDef *spell) {
    if (!world || !p || !spell) return 0;
    if (world->combat_hooks.player_has_spell_runes) {
        return world->combat_hooks.player_has_spell_runes(world, p, spell);
    }
    return inventory_has_spell_runes(p, spell);
}

static int inventory_consume_spell_runes(RcWorld *world,
                                               const RcSpellDef *spell) {
    RcPlayer *p = world ? &world->player : NULL;
    if (!p || !spell || !inventory_has_spell_runes(p, spell)) return 0;
    RcItemTransaction tx;
    RcItemActionResult result = rc_item_tx_begin(&tx, world);
    for (int i = 0; i < spell->rune_count; i++) {
        int item_id = (int)spell->runes[i].item_id;
        int quantity = (int)spell->runes[i].qty;
        if (quantity > 0 && result.code == RC_ITEM_RESULT_OK)
            result = rc_item_tx_remove_item(&tx, item_id, quantity);
    }
    if (result.code == RC_ITEM_RESULT_OK) result = rc_item_tx_commit(&tx);
    return result.code == RC_ITEM_RESULT_OK;
}

static int content_player_consume_spell_runes(RcWorld *world, RcPlayer *p,
                                              const RcSpellDef *spell) {
    if (!world || !p || !spell) return 0;
    if (world->combat_hooks.player_consume_spell_runes) {
        return world->combat_hooks.player_consume_spell_runes(world, p,
                                                              spell);
    }
    return inventory_consume_spell_runes(world, spell);
}

static const RcSpellDef *player_selected_combat_spell(const RcPlayer *p) {
    if (!p || p->combat_style != COMBAT_MAGIC) return NULL;
    int spell_idx = p->manual_spell_cast >= 0
                  ? p->manual_spell_cast : p->autocast_spell;
    const RcSpellDef *spell = rc_spell_def_get(spell_idx);
    if (!rc_spell_is_combat(spell) ||
            (spell->book != p->current_spellbook && spell->book != RC_SPELL_BOOK_ALL)) {
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
    const int *delays, const RcPendingHit *hits, RcCombatStyle style, int stance_idx, int hit_count
) {
    if (!world || !p || !target || !profiles) return;
    int target_x, target_y;
    if (!nearest_npc_tile_to_point(world, target, p->x, p->y,
                                   &target_x, &target_y)) {
        return;
    }
    RcCombatAttackEvent *event = next_attack_event_slot(world);
    if (!event) return;
    memset(event, 0, sizeof(*event));
    event->active = true;
    event->accurate = hits[0].accurate;
    event->hit_count = hit_count;
    event->source_kind = RC_COMBAT_ACTOR_PLAYER;
    event->target_kind = RC_COMBAT_ACTOR_NPC;
    event->style = (uint8_t)style;
    event->source_uid = 0;
    event->target_uid = target->uid;
    event->source_definition_id = -1;
    const RcNpcDef *target_def = rc_npc_def_for_npc(world, target);
    event->target_definition_id = target_def ? target_def->id : -1;
    event->source_x = p->x;
    event->source_y = p->y;
    event->target_x = target_x;
    event->target_y = target_y;
    event->plane = p->plane;
    event->hit_delay = delays[0];
    for (int i = 0; i < hit_count; i++) {
        event->hit_delays[i] = delays[i];
        event->hit_accurate[i] = hits[i].accurate;
    }
    event->weapon_item_id = weapon_id;
    event->ammo_item_id = profiles->ammo_id;
    event->spell_idx = spell_idx;
    event->stance_idx = stance_idx;
    event->world_tick = world->tick;

    if (use_special) {
        event->action_kind = RC_COMBAT_ACTION_SPECIAL;
        event->action_key_id = weapon_id;
        const RcItemDef *item = rc_item_def_get(weapon_id);
        copy_event_name(event->action_key_name, item ? item->name : "");
    } else if (style == COMBAT_MAGIC && spell) {
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

// Presentation-facing record of an NPC action. Encounter actions carry the
// attack or mechanic key; a target kind of RC_COMBAT_ACTOR_NONE is a tile.
static void record_npc_action(RcWorld *world, const RcNpc *npc,
                              const RcNpcDef *def, uint8_t target_kind,
                              int target_x, int target_y, RcCombatStyle style,
                              int hit_delay, const char *key) {
    if (!world || !npc || !def) return;
    int source_x, source_y;
    if (!nearest_npc_tile_to_point(world, npc, target_x, target_y,
                                   &source_x, &source_y)) {
        return;
    }
    RcCombatAttackEvent *event = next_attack_event_slot(world);
    if (!event) return;
    memset(event, 0, sizeof(*event));
    event->active = true;
    event->source_kind = RC_COMBAT_ACTOR_NPC;
    event->target_kind = target_kind;
    event->style = (uint8_t)style;
    event->source_uid = npc->uid;
    event->target_uid = target_kind == RC_COMBAT_ACTOR_PLAYER ? 0 : -1;
    event->source_definition_id = def->id;
    event->target_definition_id = -1;
    event->source_x = source_x;
    event->source_y = source_y;
    event->target_x = target_x;
    event->target_y = target_y;
    event->plane = npc->plane;
    event->action_kind = key ? RC_COMBAT_ACTION_ENCOUNTER : RC_COMBAT_ACTION_NPC;
    event->action_key_id = def->id;
    copy_event_name(event->action_key_name, key ? key : def->name);
    event->hit_delay = hit_delay;
    event->weapon_item_id = -1;
    event->ammo_item_id = -1;
    event->spell_idx = -1;
    event->stance_idx = -1;
    event->world_tick = world->tick;
}

// Presentation-only records never take the slots attack launches need.
#define RC_COMBAT_LAUNCH_EVENT_RESERVE 16

void rc_combat_record_encounter_action(RcWorld *world, const RcNpc *npc,
                                       bool target_player, int target_x,
                                       int target_y, RcCombatStyle style,
                                       int hit_delay, const char *key) {
    if (!world || !npc || !key || !key[0]
            || world->combat_attack_event_count
               >= RC_MAX_COMBAT_ATTACK_EVENTS - RC_COMBAT_LAUNCH_EVENT_RESERVE)
        return;
    record_npc_action(world, npc, rc_npc_def_for_npc(world, npc),
                      target_player ? RC_COMBAT_ACTOR_PLAYER : RC_COMBAT_ACTOR_NONE,
                      target_x, target_y, style, hit_delay, key);
}

static RcPlayerAttackProfiles select_player_attack_profiles(
    const RcPlayer *p,
    const RcSpellDef *spell,
    int spell_idx,
    int weapon_id,
    int ammo_id,
    bool use_special
) {
    RcPlayerAttackProfiles out = {
        .weapon = NULL,
        .projectile = NULL,
        .effect = NULL,
        .ammo_id = -1,
    };
    if (!p) return out;
    out.ammo_id = ammo_id;
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
                                 const RcNpc *npc, int size,
                                 RcCombatStyle style) {
    if (style_is_melee(style))
        return rc_has_line_of_walk_rect(&world->map, p->x, p->y, 1, 1,
                                         npc->x, npc->y, size, size, p->plane);
    return rc_has_los_rect(&world->map, p->x, p->y, 1, 1,
                           npc->x, npc->y, size, size, p->plane);
}

// Whether the player can attack the NPC from where they stand. It runs up to
// three times a tick (state sync, approach, attack), so the footprint is
// resolved once and distance and line of sight only when they decide it or
// the caller asks.
static int player_can_attack_npc_now(const RcWorld *world, const RcPlayer *p,
                                     const RcNpc *npc, int range,
                                     RcCombatStyle style, int *distance,
                                     int *los) {
    if (distance) *distance = INT_MAX;
    if (los) *los = 0;
    int size = npc_footprint_size(world, npc);
    RcTileBounds bounds;
    if (!rc_tile_bounds_from_origin_size(npc->x, npc->y, size, size,
                                         npc->plane, &bounds))
        return 0;
    bool melee = style_is_melee(style);
    bool adjacent_melee = melee && range == 1;
    int dist = INT_MAX;
    int tx, ty;
    if ((distance || !adjacent_melee) && p->plane == npc->plane
            && rc_tile_rect_closest_point(&bounds.rect, p->x, p->y, &tx, &ty)) {
        int d = rc_tile_distance(p->x, p->y, p->plane, tx, ty, npc->plane);
        if (d >= 0) dist = d;
    }
    if (distance) *distance = dist;
    if (melee && rc_tile_rect_contains(&bounds.rect, p->x, p->y))
        return 0;
    if (adjacent_melee) {
        RcRouteTarget target = rc_route_target_rectangle(
            npc->x, npc->y, size, size, 1, 1, false, false);
        int reached = p->plane == npc->plane && rc_route_target_reached(
            &world->map, p->plane, p->x, p->y, 1, 1, &target);
        if (los) *los = reached;
        return reached;
    }
    if (!los && dist > range) return 0;
    int line = has_player_attack_los(world, p, npc, size, style);
    if (los) *los = line;
    return dist <= range && line;
}

// The tiles the player may attack the NPC from: within `range` of its whole
// footprint, with line of sight for ranged and magic and line of walk for
// extended melee.
static RcRouteTarget player_attack_target(const RcWorld *world,
                                          const RcPlayer *p, const RcNpc *npc,
                                          int range) {
    int size = npc_footprint_size(world, npc);
    RcRouteTarget target = rc_route_target_rectangle(
        npc->x, npc->y, size, size, 1, range, false,
        style_needs_los(p->combat_style));
    target.require_line_of_walk = style_is_melee(p->combat_style) && range > 1;
    return target;
}

static int route_player_toward_npc(RcWorld *world, RcPlayer *p,
                                   const RcNpc *npc, int range) {
    RcRouteTarget target = player_attack_target(world, p, npc, range);
    RcRoute route = {0};
    if (range >= 1 && range < RC_WORLD_SIZE)
        route = rc_find_route(&world->map, p->x, p->y, 1, 1, p->plane,
                              &target, false);
    if (!rc_route_status_admitted(route.status)) {
        rc_player_route_clear(p, RC_MOVEMENT_NO_ROUTE);
        p->attack_unroutable_tick = world->tick + 1;
        return 0;
    }
    return rc_player_route_admit(p, &route, &target, 1, 1, false);
}

static bool same_attack_target(const RcRouteTarget *a, const RcRouteTarget *b) {
    return a->kind == b->kind && a->x == b->x && a->y == b->y
        && a->width == b->width && a->height == b->height
        && a->min_distance == b->min_distance
        && a->max_distance == b->max_distance
        && a->require_los == b->require_los
        && a->require_line_of_walk == b->require_line_of_walk;
}

static RcNpc *find_npc_by_uid(struct RcWorld *world, int uid);

// The attack records what decided an in-reach verdict. While that still
// matches on the next tick, a standing attacker is in reach without a
// re-check; the attack re-checks after movement regardless, so a change the
// key misses (a door, say) only delays the next step by a tick. A new form
// of the target ends the attack, so it needs no key.
static uint32_t reach_tile_key(int x, int y, int plane) {
    return (uint32_t)x | (uint32_t)y << RC_WORLD_COORD_BITS
         | (uint32_t)plane << (2 * RC_WORLD_COORD_BITS);
}

static void stamp_attack_reach(const RcWorld *world, RcPlayer *p,
                               const RcNpc *npc) {
    p->attack_reach.tick = world->tick + 1;
    p->attack_reach.npc_tile = reach_tile_key(npc->x, npc->y, npc->plane);
    p->attack_reach.tile = reach_tile_key(p->x, p->y, p->plane);
    p->attack_reach.npc_slot = (int)(npc - world->npcs);
    p->attack_reach.weapon = p->equipment[EQUIP_WEAPON].item_id;
    p->attack_reach.style_idx = p->attack_style_idx;
    p->attack_reach.autocast = p->autocast_spell;
    p->attack_reach.manual_cast = p->manual_spell_cast;
}

static bool attack_reach_unchanged(const RcWorld *world, const RcPlayer *p) {
    int slot = p->attack_reach.npc_slot;
    if (p->attack_reach.tick != world->tick || p->attack_reach.tick == 0
            || slot < 0 || slot >= world->npc_count)
        return false;
    const RcNpc *npc = &world->npcs[slot];
    return npc->active && npc->uid == p->attack_target
        && p->attack_reach.npc_tile == reach_tile_key(npc->x, npc->y,
                                                      npc->plane)
        && p->attack_reach.tile == reach_tile_key(p->x, p->y, p->plane)
        && p->attack_reach.weapon == p->equipment[EQUIP_WEAPON].item_id
        && p->attack_reach.style_idx == p->attack_style_idx
        && p->attack_reach.autocast == p->autocast_spell
        && p->attack_reach.manual_cast == p->manual_spell_cast;
}

bool rc_combat_player_approach(RcWorld *world) {
    if (!world) return true;
    RcPlayer *p = &world->player;
    if (p->attack_target < 0) return true;
    bool walking = p->route_idx < p->route_len;
    if (!walking && attack_reach_unchanged(world, p)) return true;
    RcNpc *target = find_npc_by_uid(world, p->attack_target);
    if (!target || !rc_npc_def_for_npc(world, target)) return true;
    rc_refresh_player_combat_style(p);
    if (p->combat_style == COMBAT_NONE) return true;
    int range = rc_player_attack_range(p);
    if (player_can_attack_npc_now(world, p, target, range, p->combat_style,
                                  NULL, NULL)) {
        if (walking) rc_player_route_clear(p, RC_MOVEMENT_ARRIVED);
        return false;
    }
    RcRouteTarget want = player_attack_target(world, p, target, range);
    if (!walking || !same_attack_target(&p->route_target, &want))
        (void)route_player_toward_npc(world, p, target, range);
    return true;
}

static int npc_can_attack_player_now(const RcWorld *world, const RcNpc *npc,
                                     const RcPlayer *p, int range,
                                     RcCombatStyle style, int *distance,
                                     int *los) {
    int tx, ty;
    if (!nearest_npc_tile_to_point(world, npc, p->x, p->y, &tx, &ty))
        return 0;
    int dist = rc_tile_distance(p->x, p->y, p->plane,
                                tx, ty, npc->plane);
    if (dist < 0) return 0;
    const RcNpcDef *def = rc_npc_def_for_npc(world, npc);
    int size = def && def->size > 0 ? def->size : 1;
    int line = style_needs_los(style)
               ? rc_has_los_rect(&world->map, npc->x, npc->y, size, size,
                                 p->x, p->y, 1, 1, npc->plane)
               : 1;
    if (distance) *distance = dist;
    if (los) *los = line;
    if (style_is_melee(style)) {
        RcRouteTarget target = rc_route_target_rectangle(
            p->x, p->y, 1, 1, 1, range, false, false);
        target.require_line_of_walk = range > 1;
        return rc_route_target_reached(&world->map, npc->plane,
                                       npc->x, npc->y, size, size, &target);
    }
    return dist <= range && line;
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
    int base = def->hunt.range > 0 ? def->hunt.range : def->wander_range;
    if (base < 1) base = 1;
    return base + 8;
}

static int npc_hunt_interval_ready(RcNpc *npc, const RcNpcDef *def) {
    int rate = def->hunt.rate > 0 ? def->hunt.rate : 1;
    npc->hunt_timer++;
    if (npc->hunt_timer < rate) return 0;
    npc->hunt_timer = 0;
    return 1;
}

static int npc_hunt_player_visible(const RcWorld *world, const RcNpc *npc,
                                   const RcNpcDef *def) {
    int size = def->size > 0 ? def->size : 1;
    if (def->hunt.visibility == RC_NPC_HUNT_VIS_NONE) return 1;
    if (def->hunt.visibility == RC_NPC_HUNT_VIS_LINE_OF_SIGHT) {
        return rc_has_los_rect(&world->map, npc->x, npc->y, size, size,
                               world->player.x, world->player.y, 1, 1,
                               npc->plane);
    }
    return rc_has_line_of_walk_rect(
        &world->map, npc->x, npc->y, size, size,
        world->player.x, world->player.y, 1, 1, npc->plane);
}

static int npc_hunt_target_not_busy(const RcWorld *world, const RcNpc *npc,
                                    const RcNpcDef *def) {
    if (!(def->hunt.flags & RC_NPC_HUNT_CHECK_NOT_BUSY)) return 1;
    if (world->player_action.active
            && world->player_action.owner != RC_ACTION_OWNER_COMBAT) {
        return 0;
    }
    if (world->player.interaction.active) return 0;
    return world->player.attack_target < 0
        || world->player.attack_target == npc->uid;
}

enum {
    NPC_HUNT_READY = 0,
    NPC_HUNT_ACTIVE = 1,
    NPC_HUNT_CONSUMED = 2,
};

static int npc_should_auto_attack_player(const RcWorld *world,
                                         RcNpc *npc,
                                         const RcNpcDef *def) {
    if (!world || !npc || !def
            || def->hunt.target != RC_NPC_HUNT_PLAYER
            || npc->combat.aggro_state == NPC_HUNT_CONSUMED) return 0;
    if (!npc_hunt_interval_ready(npc, def)) return 0;
    if (!npc_can_retaliate(world, npc)) return 0;
    if (world->player.current_hp <= 0 || world->player.plane != npc->plane) {
        return 0;
    }
    if (!rc_encounter_npc_may_engage(world, (uint32_t)def->id, npc->spawn_x,
                                     npc->spawn_y, npc->spawn_plane))
        return 0;
    int range = def->hunt.range;
    if (range <= 0
            || distance_player_to_npc(world, &world->player, npc) > range) {
        return 0;
    }
    if (!npc_hunt_player_visible(world, npc, def)
            || !npc_hunt_target_not_busy(world, npc, def)) return 0;
    if (def->hunt.strength == RC_NPC_HUNT_STRENGTH_OUTSIDE_WILDERNESS
            && rc_wilderness_level_at(world->player.x, world->player.y,
                                      world->player.plane) <= 0
            && rc_combat_level(&world->player.skills)
                > def->combat_level * 2) {
        return 0;
    }
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
    if (world->player.current_hp <= 0 || world->player.plane != npc->plane) {
        return 1;
    }
    if (!rc_encounter_npc_may_engage(world, (uint32_t)def->id, npc->spawn_x,
                                     npc->spawn_y, npc->spawn_plane))
        return 1;
    if (def->hunt.target == RC_NPC_HUNT_NONE
            && npc->combat.aggro_state == NPC_HUNT_READY) return 0;
    int leash = npc_leash_range(def);
    return npc_spawn_distance(npc) > leash ||
           distance_player_to_npc(world, &world->player, npc) > leash;
}

static void route_npc_to_spawn(RcWorld *world, RcNpc *npc) {
    RcRouteTarget target = rc_route_target_point(npc->spawn_x, npc->spawn_y);
    if (npc->route_mode != RC_NPC_ROUTE_RETURN
            || npc->route_target.x != target.x
            || npc->route_target.y != target.y) {
        (void)rc_npc_route_request(world, npc, &target,
                                   RC_NPC_ROUTE_RETURN, false);
    }
}

static void route_npc_to_player(RcWorld *world, RcNpc *npc, int range,
                                RcCombatStyle style) {
    RcRouteTarget target = rc_route_target_rectangle(
        world->player.x, world->player.y, 1, 1, 1, range, false,
        style_needs_los(style));
    target.require_line_of_walk = style_is_melee(style) && range > 1;
    if (npc->route_mode != RC_NPC_ROUTE_CHASE
            || npc->route_target.x != target.x
            || npc->route_target.y != target.y
            || npc->route_target.max_distance != target.max_distance
            || npc->route_target.require_los != target.require_los
            || npc->route_target.require_line_of_walk != target.require_line_of_walk) {
        (void)rc_npc_route_request(world, npc, &target,
                                   RC_NPC_ROUTE_CHASE, false);
    }
}

void rc_award_player_combat_xp(RcWorld *world, int damage, int mask) {
    if (!world || damage <= 0 || mask == 0) return;
    RcPlayer *p = &world->player;
    const int skills[] = {
        SKILL_ATTACK, SKILL_STRENGTH, SKILL_DEFENCE, SKILL_RANGED, SKILL_MAGIC,
    };
    int selected = 0;
    for (unsigned i = 0; i < sizeof(skills) / sizeof(skills[0]); i++)
        if (mask & (1u << skills[i])) selected++;
    for (unsigned i = 0; i < sizeof(skills) / sizeof(skills[0]); i++) {
        int skill = skills[i];
        if (!(mask & (1u << skill))) continue;
        int rate = selected == 3 ? 133 : selected == 2 ? 200 : 400;
        if (mask & RC_COMBAT_XP_MAGIC)
            rate = skill == SKILL_MAGIC ? (selected == 2 ? 133 : 200) : 100;
        rc_add_xp_hundredths(&p->skills, (RcSkill)skill, (int64_t)damage * rate);
    }
    int previous_level = p->skills.base_level[SKILL_HITPOINTS];
    rc_add_xp_hundredths(&p->skills, SKILL_HITPOINTS, (int64_t)damage * 133);
    int gained = p->skills.base_level[SKILL_HITPOINTS] - previous_level;
    p->max_hp += gained * 10;
    p->current_hp += gained * 10;
}
static RcNpc *find_npc_by_uid(struct RcWorld *world, int uid) {
    return rc_npc_resolve(world, uid);
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
        return actor.uid == 0 && !world->player.is_dead && world->player.current_hp > 0;
    }
    if (actor.kind == RC_COMBAT_ACTOR_NPC) {
        RcNpc *npc = find_npc_by_uid(world, actor.uid);
        return npc && !npc->is_dead && npc->current_hp > 0 && !npc->player_untargetable;
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

static int npc_can_retaliate(const RcWorld *world, const RcNpc *npc) {
    const RcNpcDef *def = rc_npc_def_for_npc(world, npc);
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
        if (combat_actor_equal(target->attackers[i], attacker)) {
            target->attacker_ticks[i] = 10;
            return;
        }
    }
    if (target->attacker_count < RC_MAX_COMBAT_ATTACKERS) {
        int index = target->attacker_count++;
        target->attackers[index] = attacker;
        target->attacker_ticks[index] = 10;
    } else {
        int oldest = 0;
        for (int i = 1; i < RC_MAX_COMBAT_ATTACKERS; i++)
            if (target->attacker_ticks[i] < target->attacker_ticks[oldest]) oldest = i;
        target->attackers[oldest] = attacker;
        target->attacker_ticks[oldest] = 10;
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
    if (!world) return 0;
    if (!combat_actor_live(world, attacker)) return 0;
    if (!combat_actor_live(world, target)) return 0;
    if (rc_combat_is_multi_combat(world)) return 1;
    return !combat_target_has_other_live_attacker(world, target, attacker);
}

static int player_locked_by_other_npc(RcWorld *world, int npc_uid) {
    if (!world || rc_combat_is_multi_combat(world) ||
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
        // A boss and the helpers its run owns are one opponent.
        if (other.kind == RC_COMBAT_ACTOR_NPC
                && rc_encounter_same_fight(world, (RcNpcId)other.uid,
                                           (RcNpcId)npc_uid)) continue;
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

static int npc_footprint_size(const RcWorld *world, const RcNpc *npc) {
    const RcNpcDef *def = rc_npc_def_for_npc(world, npc);
    if (!def) return 1;
    int size = def->size;
    return size > 0 ? size : 1;
}

static RcCombatTargetRef combat_target_from_npc(const RcWorld *world,
                                                const RcNpc *npc) {
    RcCombatTargetRef target = combat_target_none();
    const RcNpcDef *def = rc_npc_def_for_npc(world, npc);
    if (!npc || !def) {
        return target;
    }
    int size = npc_footprint_size(world, npc);
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
    player->combat.attack_range = 0;
    player->combat.distance_to_target = -1;
    player->combat.line_of_sight = 0;
    player->combat.hp_current = player->current_hp;
    player->combat.hp_max = player->max_hp;
    player->combat.in_multi_combat = rc_combat_is_multi_combat(world);
    player->combat.last_hit_timer = player->last_hit_timer;
    player->combat.selected_style_idx = player->attack_style_idx;
    player->combat.style = player->combat_style;
    player->combat.stance = player->attack_stance;
    player->combat.xp_mask = player->combat_xp_mask;
    player->combat.special_energy = player->special_energy;
    player->combat.special_pending = special_pending;
    player->combat.auto_retaliate = player->auto_retaliate;
    if (player->attack_target >= 0) {
        RcNpc *target = find_npc_by_uid(world, player->attack_target);
        if (!target || !rc_npc_def_for_npc(world, target)) {
            flags |= RC_COMBAT_STATE_INVALID_TARGET;
        } else if (target->is_dead) {
            player->combat.target = combat_target_from_npc(world, target);
            flags |= RC_COMBAT_STATE_DEAD_TARGET;
        } else {
            int range = rc_player_attack_range(player);
            int distance = -1;
            int los = 0;
            player->combat.active = true;
            player->combat.target = combat_target_from_npc(world, target);
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
    npc->combat.attack_range = 0;
    npc->combat.distance_to_target = -1;
    npc->combat.line_of_sight = 0;
    npc->combat.hp_current = npc->current_hp;
    const RcNpcDef *def = rc_npc_def_for_npc(world, npc);
    npc->combat.hp_max = def ? def->hitpoints : 0;
    npc->combat.retaliates = npc_can_retaliate(world, npc);
    npc->combat.in_multi_combat = rc_combat_is_multi_combat(world);
    npc->combat.last_hit_timer = npc->last_hit_timer;
    npc->combat.attack_count = npc->attack_count;
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
            int range = npc_attack_range_for(world, npc, def, style);
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
    const RcNpcDef *def = rc_npc_def_for_npc(world, npc);
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
    rc_refresh_player_combat_style(player);
    face_player_to_npc(player, npc);
    sync_player_combat_state_from_legacy(world, player);
    sync_npc_combat_state_from_legacy(world, npc);
    rc_player_action_refresh(world);
    return 1;
}

int rc_combat_start_npc_vs_player(struct RcWorld *world, int npc_uid,
                                  int player_uid) {
    if (!world || player_uid != 0) return 0;
    RcNpc *npc = find_npc_by_uid(world, npc_uid);
    if (!npc || npc->is_dead || npc->player_untargetable ||
            !rc_npc_def_for_npc(world, npc)) {
        return 0;
    }
    if (!npc_can_retaliate(world, npc)
            || rc_encounter_npc_dormant(world, (RcNpcId)npc->uid)) return 0;
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
    face_npc_to_player(npc);
    sync_npc_combat_state_from_legacy(world, npc);
    sync_player_combat_state_from_legacy(world, &world->player);
    return 1;
}

void rc_combat_stop_actor(struct RcWorld *world, RcCombatActorRef actor,
                          int reason) {
    if (!world) return;
    if (actor.kind == RC_COMBAT_ACTOR_PLAYER && actor.uid == 0) {
        world->player.attack_target = -1;
        world->player.attack_target_def_id = -1;
        sync_player_combat_state_from_legacy(world, &world->player);
        world->player.combat.flags |= reason | RC_COMBAT_STATE_CANCELLED;
    } else if (actor.kind == RC_COMBAT_ACTOR_NPC) {
        RcNpc *npc = find_npc_by_uid(world, actor.uid);
        if (!npc) return;
        npc->target_uid = -1;
        npc->combat.attack_prepared = false;
        // Leaving combat resets the face entity, as rsmod's resetMode does.
        if (npc->facing_entity == 0) npc->facing_entity = -1;
        if (npc->route_mode == RC_NPC_ROUTE_CHASE)
            rc_npc_route_clear(npc, RC_MOVEMENT_NONE);
        sync_npc_combat_state_from_legacy(world, npc);
        npc->combat.flags |= reason | RC_COMBAT_STATE_CANCELLED;
    }
}

void rc_combat_set_player_style(struct RcWorld *world, int style_idx) {
    if (!world) return;
    rc_player_set_attack_style(world, style_idx);
    sync_player_combat_state_from_legacy(world, &world->player);
}

int rc_combat_set_auto_retaliate(struct RcWorld *world, bool enabled) {
    if (rc_player_command_should_queue(world)) {
        int args[8] = {enabled};
        return rc_player_command_submit(
            world, RC_PLAYER_COMMAND_SET_AUTO_RETALIATE,
            RC_ACTION_CATEGORY_SOFT, args, 0);
    }
    if (!world) return 0;
    world->player.auto_retaliate = enabled;
    sync_player_combat_state_from_legacy(world, &world->player);
    return 1;
}

void rc_combat_toggle_special(struct RcWorld *world) {
    if (rc_player_command_should_queue(world)) {
        int args[8] = {0};
        (void)rc_player_command_submit(world,
                                      RC_PLAYER_COMMAND_TOGGLE_SPECIAL,
                                      RC_ACTION_CATEGORY_SOFT, args, 0);
        return;
    }
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
    dst->sequence = src->sequence;
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
    out->attack_cooldown = p->attack_timer;
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
    return world && (world->multi_combat ||
        (rc_area_flags_at(world->player.x, world->player.y,
                          world->player.plane) & RC_AREA_MULTICOMBAT) != 0);
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
    int write = 0;
    state->under_attack_timer = 0;
    for (int i = 0; i < state->attacker_count; i++) {
        int remaining = state->attacker_ticks[i];
        if (remaining <= 1) continue;
        state->attackers[write] = state->attackers[i];
        state->attacker_ticks[write++] = --remaining;
        if (remaining > state->under_attack_timer)
            state->under_attack_timer = remaining;
    }
    state->attacker_count = write;
    if (!write) state->primary_attacker = combat_actor_none();
}

static void reject_launch(RcWorld *world, RcCombatActorRef actor,
                           const char *reason) {
    RcCombatActorState *state = combat_state_for_actor(world, actor);
    if (state && state->failure_reason != reason)
        fprintf(stderr, "combat: %s %d attack rejected: %s\n",
                actor.kind == RC_COMBAT_ACTOR_PLAYER ? "player" : "npc",
                actor.uid, reason);
    rc_combat_stop_actor(world, actor, RC_COMBAT_STATE_CANCELLED);
    if (state) state->failure_reason = reason;
}

static void combat_tick_player_attack(RcWorld *world) {
    RcPlayer *p = &world->player;
    RcCombatActorRef player_actor = {RC_COMBAT_ACTOR_PLAYER, 0};
    if (p->attack_timer > 0) p->attack_timer--;
    if (p->attack_target < 0) return;
    RcNpc *target = find_npc_by_uid(world, p->attack_target);
    const RcNpcDef *target_def = rc_npc_def_for_npc(world, target);
    RcCombatActorRef npc_actor = {RC_COMBAT_ACTOR_NPC, p->attack_target};
    if (!target_def || !combat_can_attack_target(world, player_actor, npc_actor)
            || player_locked_by_other_npc(world, p->attack_target)
            || target->player_untargetable
            || (p->attack_target_def_id >= 0 && p->attack_target_def_id != target_def->id)) {
        reject_launch(world, player_actor, "target is unavailable or engaged");
        return;
    }
    if (!rc_encounter_player_can_target_npc(world, target->uid)) return;
    rc_refresh_player_combat_style(p);
    if (p->combat_style == COMBAT_NONE) {
        reject_launch(world, player_actor, "weapon attack mode is unavailable");
        return;
    }
    face_player_to_npc(p, target);
    int range = rc_player_attack_range(p);
    if (!player_can_attack_npc_now(world, p, target, range,
                                   p->combat_style, NULL, NULL)) {
        // Keep a route to the target's current footprint; a search the
        // approach already lost this tick (nothing has moved since) fails
        // the same way.
        RcRouteTarget want = player_attack_target(world, p, target, range);
        bool routed = p->route_idx < p->route_len
                   && same_attack_target(&p->route_target, &want);
        bool unroutable = p->attack_unroutable_tick == world->tick + 1;
        if (!routed && (unroutable
                        || !route_player_toward_npc(world, p, target, range)))
            reject_launch(world, player_actor, "no reachable attack tile");
        return;
    }
    stamp_attack_reach(world, p, target);
    // In reach: the approach is over even if its route had steps left.
    if (p->route_idx < p->route_len) rc_player_route_clear(p, RC_MOVEMENT_ARRIVED);
    if (p->attack_timer > 0) return;
    if (target->num_pending_hits >= RC_MAX_PENDING_HITS
            || world->combat_attack_event_count >= RC_MAX_COMBAT_ATTACK_EVENTS) {
        reject_launch(world, player_actor, "pending-hit or attack-event capacity exhausted");
        return;
    }

    const RcCombatStyle style = p->combat_style;
    const int stance = p->attack_style_idx;
    int speed = rc_player_attack_speed(p);
    const int xp_mask = p->combat_xp_mask;
    const int weapon_id = p->equipment[EQUIP_WEAPON].item_id;
    const RcSpellDef *spell = NULL;
    RcCombatCalc magic_calc = {0};
    int resource_slot = RC_RANGED_RESOURCE_NONE;
    const char *resource_failure = NULL;
    if (style == COMBAT_RANGED || world->combat_hooks.player_ranged_resource_slot) {
        resource_slot = player_ranged_resource_slot(world, &resource_failure);
        if (resource_slot < RC_RANGED_RESOURCE_NONE || resource_slot >= RC_EQUIP_COUNT
                || (resource_slot >= 0 && (p->equipment[resource_slot].item_id < 0
                    || p->equipment[resource_slot].quantity <= 0))) {
            reject_launch(world, player_actor, resource_failure
                ? resource_failure : "ammunition is unavailable");
            return;
        }
    }
    if (style == COMBAT_MAGIC) {
        spell = player_selected_combat_spell(p);
        bool selected = p->manual_spell_cast >= 0 || p->autocast_spell >= 0;
        if (selected && !spell) {
            reject_launch(world, player_actor, "This spell is not supported as a combat attack.");
            clear_failed_player_spell_attack(p);
            return;
        }
        RcSpellResult available = spell ? rc_spell_available(world,
            p->manual_spell_cast >= 0 ? p->manual_spell_cast : p->autocast_spell,
            p->manual_spell_cast < 0) : RC_SPELL_OK;
        if (available != RC_SPELL_OK) {
            reject_launch(world, player_actor, rc_spell_result_message(available));
            if (available == RC_SPELL_RUNES) clear_failed_player_spell_attack(p);
            return;
        }
        if (world->combat_hooks.prepare_player_magic) {
            if (!world->combat_hooks.prepare_player_magic(world, target, spell,
                    &magic_calc, &speed, &resource_failure)) {
                reject_launch(world, player_actor, resource_failure);
                return;
            }
        } else if (spell && p->skills.boosted_level[SKILL_MAGIC] >= spell->level) {
            magic_calc = rc_calc_magic(p, target, spell->max_hit);
        } else {
            reject_launch(world, player_actor, "Magic level insufficient or powered attack unavailable.");
            return;
        }
    }
    const bool special = p->combat.special_pending;
    int special_cost = special
        ? content_player_special_energy_cost(world, p, target, weapon_id) : 0;
    if (special && (special_cost <= 0 || p->special_energy < special_cost)) {
        reject_launch(world, player_actor, "special attack unsupported or resources insufficient");
        p->combat.special_pending = false;
        return;
    }
    const int spell_idx = p->manual_spell_cast >= 0 ? p->manual_spell_cast : p->autocast_spell;
    RcPlayerAttackProfiles profiles =
        select_player_attack_profiles(p, spell, spell_idx, weapon_id,
            resource_slot >= 0 ? p->equipment[resource_slot].item_id : -1, special);
    int delay = profile_hit_delay(player_timing_profile(&profiles),
                                   rc_combat_hit_delay_for_style(style));
    RcCombatCalc calc;
    if (style == COMBAT_RANGED)
        calc = rc_calc_ranged(p, target, resource_slot == EQUIP_AMMO);
    else if (style == COMBAT_MAGIC) calc = magic_calc;
    else calc = rc_calc_melee(p, target);
    uint32_t rng_before = world->rng_state;
    RcPendingHit prepared[4] = {0};
    int count = world->combat_hooks.prepare_player_hits
        ? world->combat_hooks.prepare_player_hits(world, target, &calc, special,
                                                  prepared, 4, &resource_failure) : 0;
    if (count == 0) {
        RcCombatRoll roll = rc_roll_attack(&calc, &world->rng_state, true);
        if (target->force_player_max_hit) roll = (RcCombatRoll){true, calc.max_hit};
        if (special)
            roll.damage = content_modify_player_special_damage(
                world, p, target, weapon_id, style, roll.damage, calc.max_hit);
        prepared[0] = (RcPendingHit){.damage = roll.damage,
            .accurate = roll.accurate, .max_hit = calc.max_hit};
        count = 1;
    }
    if (count < 0 || count > 4 || target->num_pending_hits > RC_MAX_PENDING_HITS - count) {
        world->rng_state = rng_before;
        reject_launch(world, player_actor, count < 0 && resource_failure
            ? resource_failure : "pending-hit capacity exhausted; attack not launched");
        return;
    }
    int hit_index = target->num_pending_hits;
    int total_damage = 0;
    int delays[4];
    for (int i = 0; i < count; i++) {
        delays[i] = world->combat_hooks.player_hit_delay
            ? world->combat_hooks.player_hit_delay(world, target, spell, i) : delay;
        if (delays[i] < 0 || delays[i] > UINT8_MAX) {
            world->rng_state = rng_before;
            reject_launch(world, player_actor, "invalid hit arrival delay; attack not launched");
            return;
        }
    }
    for (int i = 0; i < count; i++) {
        RcPendingHit *hit = &prepared[i];
        hit->damage = rc_combat_apply_regular_npc_player_damage_rules(world, target, hit->damage);
        hit->damage = rc_encounter_scale_player_damage(world, target->uid, style, hit->damage);
        if (spell && rc_encounter_player_instakill(world, (RcNpcId)target->uid,
                                                   spell_idx + 1)) {
            hit->damage = target->current_hp;
            hit->accurate = true;
        }
        rc_queue_hit_meta(target->pending_hits, &target->num_pending_hits,
                         hit->damage, delays[i], style, RC_HIT_SOURCE_PLAYER,
                         0, world->tick, 0, hit->max_hit);
        target->pending_hits[hit_index + i].accurate = hit->accurate;
        target->pending_hits[hit_index + i].defence_drain = hit->defence_drain;
        target->pending_hits[hit_index + i].spell_key = spell ? spell_idx + 1 : 0;
        target->pending_hits[hit_index + i].weapon_id = weapon_id > 0 ? weapon_id : 0;
        total_damage += hit->damage;
    }
    int ammo_cost = world->combat_hooks.player_ranged_resource_cost
        ? world->combat_hooks.player_ranged_resource_cost(p, special) : 1;
    bool paid = true;
    if (resource_slot >= 0) {
        paid = ammo_cost > 0 && (world->combat_hooks.consume_ranged_resource
            ? world->combat_hooks.consume_ranged_resource(world, resource_slot,
                ammo_cost, target->x, target->y, target->plane, delays[count - 1],
                &resource_failure)
            : equipment_consume(world, resource_slot, ammo_cost));
    }
    else if (style == COMBAT_MAGIC && spell)
        paid = content_player_consume_spell_runes(world, p, spell);
    else if (world->combat_hooks.consume_weapon_charge)
        paid = world->combat_hooks.consume_weapon_charge(world, weapon_id);
    if (!paid) {
        memset(&target->pending_hits[hit_index], 0, sizeof(RcPendingHit) * count);
        target->num_pending_hits = hit_index;
        world->rng_state = rng_before;
        reject_launch(world, player_actor, resource_failure ? resource_failure
            : "resource transaction rejected; attack not launched");
        return;
    }

    p->combat.failure_reason = NULL;
    if (spell) rc_add_xp_hundredths(&p->skills, SKILL_MAGIC, (int64_t)spell->xp_q1 * 10);
    rc_encounter_player_attack_committed(world, target->uid);
    if (special) {
        p->special_energy -= special_cost;
        p->combat.special_pending = false;
        p->combat.special_energy = p->special_energy;
        if (world->combat_hooks.after_player_special_launch)
            world->combat_hooks.after_player_special_launch(world, weapon_id, total_damage);
    }
    int xp_damage = total_damage < target->current_hp ? total_damage : target->current_hp;
    rc_award_player_combat_xp(world, xp_damage, xp_mask);
    emit_player_attack_event(world, p, target, spell, spell_idx, weapon_id,
                              special, &profiles, delays, prepared, style, stance, count);
    if (p->manual_spell_cast >= 0) {
        p->attack_target = -1;
        p->attack_target_def_id = -1;
    }
    p->manual_spell_cast = -1;
    p->attack_timer = speed;
    combat_register_attacker(&target->combat, player_actor);
    if (npc_can_retaliate(world, target))
        rc_combat_start_npc_vs_player(world, target->uid, 0);
    RcPayloadPlayerAttack payload = {
        .target_npc_id = (uint32_t)target->uid, .style = (uint8_t)style,
    };
    rc_event_fire(world, RC_EVT_PLAYER_ATTACK, &payload);
}

// Encounter attacks reaching beyond one tile are projectiles whatever their
// damage style; melee reach rules apply only to adjacent attacks.
static RcCombatStyle encounter_reach_style(const RcEncounterAttack *attack,
                                           RcCombatStyle style) {
    return attack && attack->range > 1 && style_is_melee(style)
         ? COMBAT_RANGED : style;
}

static int npc_attack_in_reach(const RcWorld *world, const RcNpc *npc,
                               const RcPlayer *p,
                               const RcEncounterAttack *attack, int range,
                               RcCombatStyle style) {
    if (attack && (attack->flags & RC_ENC_ATTACK_DIAGONAL_REACH)
            && style_is_melee(style) && range <= 1)
        return distance_player_to_npc(world, p, npc) == 1;
    return npc_can_attack_player_now(world, npc, p, range,
                                     encounter_reach_style(attack, style),
                                     NULL, NULL);
}

// `enc` is the launched encounter attack (NULL for ordinary attacks); tile
// attacks are presented aimed at the launch tile.
static void finish_npc_launch(RcWorld *world, RcNpc *npc, const RcNpcDef *d,
                              RcPlayer *p, RcCombatStyle style, int delay,
                              bool encounter, const RcEncounterAttack *enc) {
    RcCombatActorRef npc_actor = {RC_COMBAT_ACTOR_NPC, npc->uid};
    npc->combat.failure_reason = NULL;
    npc->combat.last_npc_style = style;
    npc->combat.leash_state = 0;
    combat_register_attacker(&p->combat, npc_actor);
    bool tile = enc && (enc->flags & RC_ENC_ATTACK_TILE_TARGET);
    record_npc_action(world, npc, d,
                      tile ? RC_COMBAT_ACTOR_NONE : RC_COMBAT_ACTOR_PLAYER,
                      p->x, p->y, style, delay, enc ? enc->name : NULL);
    npc->attack_timer = encounter ? d->attack_speed
                      : content_modify_npc_attack_speed(world, npc, d->attack_speed);
    npc->attack_count++;
    npc->combat.attack_prepared = false;
    if (!encounter) content_after_npc_swing(world, npc, style);
    RcPayloadNpcAttack payload = {.npc_id = (uint32_t)npc->uid, .style = (uint8_t)style};
    rc_event_fire(world, RC_EVT_NPC_ATTACK, &payload);
}

static void combat_tick_npc_attack(RcWorld *world, RcNpc *npc) {
    if (npc->is_dead || !npc->active) return;
    if (npc->attack_timer > 0) npc->attack_timer--;
    if (npc->player_untargetable || npc->owner_controlled) return;
    const RcNpcDef *d = rc_npc_def_for_npc(world, npc);
    if (!d) return;
    if (npc->target_uid < 0) {
        if (npc->combat.aggro_state == NPC_HUNT_ACTIVE)
            npc->combat.aggro_state = (d->hunt.flags & RC_NPC_HUNT_KEEP_HUNTING)
                ? NPC_HUNT_READY : NPC_HUNT_CONSUMED;
        if (npc_should_auto_attack_player(world, npc, d)) {
            if (rc_combat_start_npc_vs_player(world, npc->uid, 0))
                npc->combat.aggro_state = NPC_HUNT_ACTIVE;
        } else {
            int distance = npc_spawn_distance(npc);
            if (distance > npc_leash_range(d)) npc->combat.leash_state = 1;
            if (npc->combat.leash_state && distance > 0) route_npc_to_spawn(world, npc);
            else if (distance == 0) {
                npc->combat.leash_state = 0;
                if (npc->route_mode == RC_NPC_ROUTE_RETURN)
                    rc_npc_route_clear(npc, RC_MOVEMENT_ARRIVED);
            }
            return;
        }
    }
    RcPlayer *p = &world->player;
    if (npc->target_uid != 0) return;
    RcCombatActorRef npc_actor = {RC_COMBAT_ACTOR_NPC, npc->uid};
    RcCombatActorRef player_actor = {RC_COMBAT_ACTOR_PLAYER, 0};
    if (npc_should_drop_target(world, npc, d)
            || !combat_can_attack_target(world, npc_actor, player_actor)) {
        rc_combat_stop_actor(world, npc_actor, RC_COMBAT_STATE_CANCELLED);
        npc->combat.leash_state = 1;
        route_npc_to_spawn(world, npc);
        return;
    }
    if (d->attack_speed <= 0 || d->max_hit <= 0) return;

    // Bosses with an encounter run use only their encounter attack table;
    // regular-NPC content rules never apply to them.
    bool encounter = rc_encounter_run_for_npc(world, (RcNpcId)npc->uid) >= 0;
    RcCombatStyle style = npc->combat.selected_npc_style;
    if (style == COMBAT_NONE) style = rc_combat_npc_preferred_style(d->attack_types);
    const RcEncounterAttack *enc = prepared_encounter_attack(world, npc);
    int range = npc_attack_range_for(world, npc, d, style);
    face_npc_to_player(npc);
    if (npc->attack_timer > 0) {
        if (!npc_attack_in_reach(world, npc, p, enc, range, style))
            route_npc_to_player(world, npc, range, encounter_reach_style(enc, style));
        return;
    }
    int extra = !encounter && world->combat_hooks.extra_npc_hit_count
        ? world->combat_hooks.extra_npc_hit_count(world, npc) : 0;
    if (extra < 0 || extra >= RC_MAX_PENDING_HITS
            || p->num_pending_hits > RC_MAX_PENDING_HITS - 1 - extra
            || world->combat_attack_event_count >= RC_MAX_COMBAT_ATTACK_EVENTS) {
        reject_launch(world, npc_actor, "pending-hit or attack-event capacity exhausted");
        return;
    }

    if (!npc->combat.attack_prepared) {
        style = encounter ? rc_combat_npc_preferred_style(d->attack_types)
              : content_select_npc_style(world, npc, p,
                                         rc_combat_npc_preferred_style(d->attack_types));
        npc->combat.prepared_min_hit = npc->combat.prepared_max_hit = 0;
        npc->combat.prepared_attack_flags = 0;
        npc->combat.prepared_enc_attack = 0;
        npc->combat.prepared_enc_generation = 0;
        uint32_t generation = 0;
        int selected = rc_encounter_select_npc_attack(world, (RcNpcId)npc->uid,
            distance_player_to_npc(world, p, npc),
            npc_can_attack_player_now(world, npc, p, 1, COMBAT_MELEE_SLASH,
                                      NULL, NULL),
            &enc, &generation);
        if (selected == RC_ENC_SELECT_HOLD) return;
        if (selected == RC_ENC_SELECT_APPROACH) {
            route_npc_to_player(world, npc, 1, COMBAT_MELEE_SLASH);
            return;
        }
        if (selected == RC_ENC_SELECT_SPECIAL) {
            npc->attack_timer = d->attack_speed;
            npc->attack_count++;
            npc->combat.failure_reason = NULL;
            return;
        }
        if (selected >= 0) {
            style = (RcCombatStyle)enc->style;
            npc->combat.prepared_min_hit = enc->min_hit;
            npc->combat.prepared_max_hit = enc->max_hit;
            npc->combat.prepared_attack_flags = enc->flags;
            npc->combat.prepared_enc_attack = (uint8_t)(selected + 1);
            npc->combat.prepared_enc_generation = generation;
        }
        npc->combat.selected_npc_style = style;
        npc->combat.attack_prepared = true;
    }
    enc = prepared_encounter_attack(world, npc);
    if (npc->combat.prepared_enc_attack && !enc) {
        npc->combat.attack_prepared = false;
        return;
    }
    uint16_t max_hit = npc->combat.prepared_max_hit;
    uint16_t min_hit = npc->combat.prepared_min_hit;
    uint32_t flags = npc->combat.prepared_attack_flags;
    range = npc_attack_range_for(world, npc, d, style);
    npc->combat.selected_npc_style = style;
    if (!npc_attack_in_reach(world, npc, p, enc, range, style)) {
        route_npc_to_player(world, npc, range, encounter_reach_style(enc, style));
        return;
    }
    if (npc->route_mode == RC_NPC_ROUTE_CHASE)
        rc_npc_route_clear(npc, RC_MOVEMENT_ARRIVED);
    int delay = enc && enc->hit_delay ? enc->hit_delay
              : profile_hit_delay(rc_combat_profile_for_npc(d->id, style),
                                  rc_combat_hit_delay_for_style(style));
    if (enc && (flags & RC_ENC_ATTACK_TILE_TARGET)) {
        // Launched at the player's tile, judged where they stand on impact.
        int run = rc_encounter_run_for_npc(world, (RcNpcId)npc->uid);
        if (rc_encounter_add_strike(world, run, p->x, p->y, p->plane, delay,
                                    min_hit, max_hit, enc->adjacent_pct,
                                    (uint8_t)style) < 0) {
            reject_launch(world, npc_actor, "encounter hazard capacity exhausted");
            npc->combat.attack_prepared = false;
            return;
        }
        finish_npc_launch(world, npc, d, p, style, delay, encounter, enc);
        return;
    }
    RcCombatCalc calc = rc_calc_npc_attack_style(npc, d, p, style);
    if (max_hit > 0) calc.max_hit = max_hit;
    bool protectable = !(flags & RC_ENC_ATTACK_IGNORES_PROTECTION);
    uint8_t hit_flags = 0;
    RcCombatRoll roll;
    if (enc && enc->breath) {
        // Breath rolls magic accuracy to pick its table row, then damage up
        // to the content max for the player's protection; that max already
        // includes Protect from Magic.
        RcCombatCalc magic = rc_calc_npc_attack_style(npc, d, p, COMBAT_MAGIC);
        RcCombatRoll accuracy = rc_roll_attack(&magic, &world->rng_state, false);
        bool shielded = false;
        int breath_max = world->combat_hooks.npc_breath_max_hit
            ? world->combat_hooks.npc_breath_max_hit(world, npc, enc->breath,
                                                     !accuracy.accurate, &shielded)
            : -1;
        if (breath_max < 0) {
            reject_launch(world, npc_actor, "breath attack has no content rule");
            npc->combat.attack_prepared = false;
            return;
        }
        calc.max_hit = breath_max;
        roll.accurate = true;
        roll.damage = rc_rng_range(&world->rng_state, breath_max);
        if (shielded) hit_flags |= RC_HIT_BREATH_PROTECTED;
        protectable = false;
    } else if (flags & RC_ENC_ATTACK_NO_DAMAGE) {
        roll.accurate = true;
        roll.damage = 0;
    } else if (flags & RC_ENC_ATTACK_ALWAYS_ACCURATE) {
        roll.accurate = true;
        roll.damage = min_hit + rc_rng_range(&world->rng_state,
                                             calc.max_hit - min_hit);
    } else {
        roll = rc_roll_attack(&calc, &world->rng_state, false);
        if (roll.accurate && min_hit > 0 && roll.damage < min_hit) roll.damage = min_hit;
    }
    if (!encounter) {
        roll.damage = content_modify_npc_roll_damage(world, npc, style, roll.damage);
        roll.damage = rc_combat_apply_regular_npc_attack_rules(world, npc, roll.damage);
    }
    int index = p->num_pending_hits;
    if (!rc_queue_hit_meta(p->pending_hits, &p->num_pending_hits,
            roll.damage, delay, style, npc->uid,
            protectable ? p->active_prayers : 0,
            world->tick, hit_flags, calc.max_hit)) {
        reject_launch(world, npc_actor, "pending-hit admission failed");
        return;
    }
    p->pending_hits[index].accurate = roll.accurate;
    p->pending_hits[index].enc_attack = npc->combat.prepared_enc_attack;
    p->pending_hits[index].enc_generation = npc->combat.prepared_enc_generation;
    finish_npc_launch(world, npc, d, p, style, delay, encounter, enc);
}
void rc_combat_tick_player(struct RcWorld *world) {
    combat_tick_player_attack(world);
    sync_player_combat_state_from_legacy(world, &world->player);
}

void rc_combat_tick_npc(struct RcWorld *world, RcNpc *npc) {
    combat_tick_npc_attack(world, npc);
    sync_npc_combat_state_from_legacy(world, npc);
}

void rc_combat_reset_player_life(RcWorld *world) {
    if (!world) return;
    RcPlayer *p = &world->player;
    memset(p->pending_hits, 0, sizeof(p->pending_hits));
    p->num_pending_hits = 0;
    p->attack_target = p->attack_target_def_id = -1;
    p->facing_entity = -1;
    p->attack_timer = 0;
    p->manual_spell_cast = -1;
    p->combat.special_pending = false;
    rc_combat_init_player_state(p);
    rc_prayer_reset(p);
    rc_consumables_reset(p);
    p->disease_tick_counter = 0;
    p->freeze_start_tick = p->freeze_expire_tick = 0;
    p->teleblock_start_tick = p->teleblock_expire_tick = 0;
    p->ward_of_arceuus_timer = p->hp_regen_counter = 0;
    p->stat_restore_counter = p->stat_boost_counter = 0;
    p->last_hit = p->last_hit_timer = 0;
    for (int i = 0; i < SKILL_COUNT; i++)
        p->skills.boosted_level[i] = p->skills.base_level[i];
    rc_world_state_clear_player_combat(world);
}

int rc_combat_apply_player_hit(RcWorld *world, const RcPendingHit *hit, int damage) {
    if (!world || !hit) return 0;
    RcPlayer *p = &world->player;
    if (p->is_dead || p->current_hp <= 0) return 0;
    if (damage < 0) damage = 0;
    int lost = damage > p->current_hp / 10 ? p->current_hp : damage * 10;
    p->current_hp -= lost;
    p->skills.boosted_level[SKILL_HITPOINTS] = (p->current_hp + 9) / 10;
    uint8_t type = damage <= 0 ? RC_HIT_TYPE_MISS
        : hit->max_hit > 0 && damage >= hit->max_hit ? RC_HIT_TYPE_MAX : RC_HIT_TYPE_NORMAL;
    rc_combat_actor_record_hit(&p->combat, damage, hit->max_hit,
        hit->attack_style, hit->source_idx, type, hit->flags, 4);
    p->last_hit = damage;
    p->last_hit_timer = 4;
    int applied = (lost + 9) / 10;
    if (!hit->enc_attack
            && !(hit->flags & (RC_HIT_SUPPRESS_ENCOUNTER_EFFECTS | RC_HIT_SOURCE_EXPIRED)))
        content_on_npc_hit_player(world, hit, applied);
    RcPayloadPlayerDamaged payload = {
        .source_npc_id = hit->source_idx >= 0 ? (uint32_t)hit->source_idx : UINT32_MAX,
        .damage = (uint16_t)(applied > UINT16_MAX ? UINT16_MAX : applied),
        .damage_tenths = lost,
        .current_hp = (uint16_t)(p->current_hp > UINT16_MAX ? UINT16_MAX : p->current_hp),
        .max_hp = (uint16_t)(p->max_hp > UINT16_MAX ? UINT16_MAX : p->max_hp),
        .style = (uint8_t)hit->attack_style, .flags = hit->flags,
        .rolled = (uint16_t)(hit->damage < 0 ? 0
                             : hit->damage > UINT16_MAX ? UINT16_MAX : hit->damage),
        .enc_attack = hit->enc_attack,
        .enc_generation = hit->enc_generation,
    };
    rc_event_fire(world, RC_EVT_PLAYER_DAMAGED, &payload);
    return applied;
}

void rc_resolve_player_hits(RcWorld *world) {
    RcPlayer *p = &world->player;
    for (int i = 0; i < p->num_pending_hits; i++) {
        RcPendingHit hit = p->pending_hits[i];
        if (!hit.active || world->tick < hit.apply_tick) continue;
        p->pending_hits[i].active = 0;
        if (p->is_dead || p->current_hp <= 0) continue;
        RcNpc *source = hit.source_idx >= 0 ? find_npc_by_uid(world, hit.source_idx) : NULL;
        if (hit.source_idx >= 0 && style_is_melee(hit.attack_style)
                && (!source || source->is_dead || source->current_hp <= 0)) continue;
        if (hit.source_idx >= 0 && (!source || source->is_dead || source->current_hp <= 0
                || (hit.flags & RC_HIT_SOURCE_EXPIRED))) {
            source = NULL;
            hit.flags |= RC_HIT_SOURCE_EXPIRED;
        }

        int scale = source ? rc_encounter_player_protection_scale_pct(world,
            hit.source_idx, hit.enc_attack, hit.enc_generation,
            (uint32_t)hit.prayer_snapshot) : -1;
        int damage = scale >= 0 ? (hit.damage * scale) / 100
            : rc_combat_apply_protection(hit.damage, hit.attack_style,
                                          (uint32_t)hit.prayer_snapshot, true);
        if (source && !hit.enc_attack
                && !(hit.flags & RC_HIT_SUPPRESS_ENCOUNTER_EFFECTS))
            damage = content_modify_incoming_after_protection(world, &hit, damage);
        rc_combat_apply_player_hit(world, &hit, damage);
        // Encounter hazards (pools, warned strikes) are not attacks: they
        // never start auto-retaliation.
        if (source && !source->is_dead
                && !(hit.flags & RC_HIT_SUPPRESS_ENCOUNTER_EFFECTS)) {
            RcCombatActorRef attacker = {RC_COMBAT_ACTOR_NPC, hit.source_idx};
            combat_register_attacker(&p->combat, attacker);
            if (p->auto_retaliate && p->attack_target < 0 && p->current_hp > 0
                    && p->route_idx >= p->route_len && p->movement_step_count == 0
                    && !p->interaction.active && !p->skill_action
                    && !world->player_action.active) {
                if (rc_combat_start_player_vs_npc(world, 0, hit.source_idx)) {
                    int delay = rc_player_attack_speed(p) / 2;
                    if (p->attack_timer < delay) p->attack_timer = delay;
                }
            }
        }
    }
    int write = 0;
    for (int i = 0; i < p->num_pending_hits; i++)
        if (p->pending_hits[i].active) p->pending_hits[write++] = p->pending_hits[i];
    p->num_pending_hits = write;
    sync_player_combat_state_from_legacy(world, p);
}
static void apply_status_deadline(RcWorld *world, int ticks,
                                  RcTick *start_tick,
                                  RcTick *expire_tick) {
    if (!world || ticks <= 0 || !start_tick || !expire_tick) return;
    RcTick start = world->tick + (world->in_tick ? 1u : 0u);
    RcTick duration = (RcTick)ticks;
    RcTick expire = UINT64_MAX - start < duration
                  ? UINT64_MAX : start + duration;
    if (*expire_tick <= world->tick || start < *start_tick)
        *start_tick = start;
    if (expire > *expire_tick) *expire_tick = expire;
}

static int status_ticks_remaining(RcTick now, RcTick start_tick,
                                  RcTick expire_tick) {
    if (expire_tick <= now || expire_tick <= start_tick) return 0;
    RcTick remaining = expire_tick - (now < start_tick ? start_tick : now);
    return remaining > INT_MAX ? INT_MAX : (int)remaining;
}

void rc_player_apply_freeze(RcWorld *world, int ticks) {
    if (!world) return;
    apply_status_deadline(world, ticks,
                          &world->player.freeze_start_tick,
                          &world->player.freeze_expire_tick);
}

void rc_player_apply_teleblock(RcWorld *world, int ticks) {
    if (!world) return;
    apply_status_deadline(world, ticks,
                          &world->player.teleblock_start_tick,
                          &world->player.teleblock_expire_tick);
}

int rc_player_freeze_ticks_remaining(const RcWorld *world) {
    return world ? status_ticks_remaining(
        world->tick, world->player.freeze_start_tick,
        world->player.freeze_expire_tick) : 0;
}

int rc_player_teleblock_ticks_remaining(const RcWorld *world) {
    return world ? status_ticks_remaining(
        world->tick, world->player.teleblock_start_tick,
        world->player.teleblock_expire_tick) : 0;
}

bool rc_player_is_frozen(const RcWorld *world) {
    return world && world->tick >= world->player.freeze_start_tick
        && world->tick < world->player.freeze_expire_tick;
}

void rc_combat_tick_player_status(struct RcWorld *world) {
    if (!world) return;
    RcPlayer *p = &world->player;
    if (p->is_dead || p->current_hp <= 0) return;
    if (p->last_hit_timer > 0) p->last_hit_timer--;
    rc_combat_actor_tick_recent_hits(&p->combat);
    rc_combat_tick_actor_threat(&p->combat);
    if (p->ward_of_arceuus_timer > 0) p->ward_of_arceuus_timer--;
    rc_player_toxin_tick(world);
    if (p->disease_tick_counter > 0) {
        p->disease_tick_counter--;
    }
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
