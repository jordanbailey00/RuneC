#include "encounter.h"
#include "types.h"
#include "combat.h"
#include "items.h"
#include "npc.h"
#include "rng.h"
#include <string.h>
#include <stddef.h>

// Generic encounter primitives — mechanism only, never content-specific.
// See data/curated/encounters/_primitives.md for semantics.
//
// This file holds ONLY primitives that are reusable across multiple
// bosses. Boss-specific scripts (one-offs like scurrius_heal_at_food_pile
// or kq_shed_exoskeleton) belong in rc-content/encounters/<boss>.c —
// not here. See rc-core/README.md §18 for the engine/content boundary.
//
// Comments below mention specific bosses as usage examples (Scurrius
// Falling Bricks, KQ Barbed Spines, etc.) — those are just canonical
// example configurations of these generic primitives, not hardcoded
// content. The primitive code never branches on specific NPC ids.
//
// Pass 2 scope: bounded generic primitive slice. Periodic primitives
// execute per-tick via the existing mechanic scheduler. Event-driven
// `drain_prayer_on_hit` fires through rc_encounter_on_player_damaged.
// Simple named phase-enter / phase-exit mechanics fire through
// rc_encounter_on_phase_transition for the existing HP%-threshold
// phase model. Richer phase semantics (hard-hp zero, timed
// transitions, payload-aware incoming-hit scripts) remain deferred.

// XORshift RNG matches rng.h macro expectations; world->rng_state is
// already mutated elsewhere, we share it deterministically.

// ---- Helpers -----------------------------------------------------------

static RcNpc *find_boss(RcWorld *world, RcNpcId uid) {
    for (int i = 0; i < world->npc_count; i++) {
        if (world->npcs[i].active && world->npcs[i].uid == uid) {
            return &world->npcs[i];
        }
    }
    return NULL;
}

static int find_npc_def_idx_by_name(const char *name) {
    for (int i = 0; i < g_npc_def_count; i++) {
        if (strcmp(g_npc_defs[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

static bool any_live_npc_named(const RcWorld *world, const char *name) {
    int def_idx = find_npc_def_idx_by_name(name);
    if (def_idx < 0) return false;
    for (int i = 0; i < world->npc_count; i++) {
        const RcNpc *npc = &world->npcs[i];
        if (!npc->active || npc->is_dead) continue;
        if (npc->def_id == def_idx) return true;
    }
    return false;
}

// ---- Pilot primitives --------------------------------------------------

// telegraphed_aoe_tile: Scurrius Falling Bricks.
// If the player is standing on the boss's target tile OR within an
// N-tile square around the boss when the mechanic fires, queue a
// delayed hit after `warning_ticks`. Damage rolls uniform between
// damage_min and damage_max (solo uses solo_damage_max).
static void prim_telegraphed_aoe_tile(RcWorld *world, int enc_idx,
                                      const void *params) {
    const RcPrimParamsTelegraphedAoe *p = params;
    RcActiveEncounter *a = &world->encounter.active[enc_idx];
    RcNpc *boss = find_boss(world, a->boss_id);
    if (!boss) return;

    RcPlayer *pl = &world->player;

    // Primary tile = player's current tile (per wiki: "always target
    // the player's current tile"). Secondary tiles are random — for
    // pass 2 we approximate by checking if player is within radius
    // (extra_random_tiles is used as arena-radius proxy).
    bool on_primary = p->target_current_tile ? true : false;
    bool on_extra = false;
    int r = p->extra_random_tiles;
    if (r > 0) {
        int dx = pl->x - boss->x;
        int dy = pl->y - boss->y;
        if (dx < 0) dx = -dx;
        if (dy < 0) dy = -dy;
        if (dx <= r && dy <= r) on_extra = true;
    }
    if (!on_primary && !on_extra) return;

    uint16_t dmax = p->damage_max ? p->damage_max : 1;
    // RuneC is single-player today — always apply the solo-mode cap
    // when the spec provides one. When multiplayer lands, gate this
    // on a world-level `is_solo_mode` flag instead of assuming always.
    if (p->solo_damage_max) dmax = p->solo_damage_max;
    uint16_t dmin = p->damage_min;
    int dmg = dmin;
    int span = (int)dmax - (int)dmin;
    if (span > 0) dmg += (int)(rc_rng_next(&world->rng_state) % (uint32_t)(span + 1));

    rc_queue_hit(pl->pending_hits, &pl->num_pending_hits,
                 dmg, p->warning_ticks,
                 COMBAT_MAGIC, boss->uid,
                 pl->active_prayers, world->tick);
}

// spawn_npcs: Scurrius Minions.
// Resolve npc name at call time via g_npc_defs lookup, then spawn
// `count` instances distributed around the boss. Pass 2 spawns them
// in a simple ring; Phase-aware distribution is pass 3.
static void prim_spawn_npcs(RcWorld *world, int enc_idx,
                            const void *params) {
    const RcPrimParamsSpawnNpcs *p = params;
    if (p->count == 0) return;

    int def_idx = find_npc_def_idx_by_name(p->name);
    if (def_idx < 0) return;       // name didn't resolve — no-op

    RcActiveEncounter *a = &world->encounter.active[enc_idx];
    RcNpc *boss = find_boss(world, a->boss_id);
    if (!boss) return;

    // Ring offsets around the boss. 8 pre-baked tile deltas covering
    // an arena radius of ~4 tiles; if count > 8 we wrap.
    static const int ring_dx[8] = { 3, 3, 0, -3, -3, -3, 0, 3 };
    static const int ring_dy[8] = { 0, 3, 3, 3, 0, -3, -3, -3 };
    for (uint8_t i = 0; i < p->count && i < 16; i++) {
        int slot = i & 7;
        rc_npc_spawn(world, def_idx,
                     boss->x + ring_dx[slot],
                     boss->y + ring_dy[slot],
                     boss->plane);
    }
}

static void prim_spawn_npcs_once(RcWorld *world, int enc_idx,
                                 const void *params) {
    prim_spawn_npcs(world, enc_idx, params);
}

// periodic_heal_boss: Scorpia guardian-heal style mechanic.
// Fires on the mechanic's normal period, but only heals while at
// least one matching support NPC is still alive.
static void prim_periodic_heal_boss(RcWorld *world, int enc_idx,
                                    const void *params) {
    const RcPrimParamsPeriodicHealBoss *p = params;
    RcActiveEncounter *a = &world->encounter.active[enc_idx];
    RcNpc *boss = find_boss(world, a->boss_id);
    if (!boss) return;
    if (!any_live_npc_named(world, p->alive_npc_name)) return;

    int def_hp = g_npc_defs[boss->def_id].hitpoints;
    boss->current_hp += p->heal_per_tick;
    if (boss->current_hp > def_hp) boss->current_hp = def_hp;
}

// heal_at_object: Scurrius Food Heal.
// Auto-fired by the simple phase-enter plumbing when the fight enters
// the named heal phase. Richer object-walk / animation semantics stay
// in the future content-script layer.
static void prim_heal_at_object(RcWorld *world, int enc_idx,
                                const void *params) {
    const RcPrimParamsHealAtObject *p = params;
    RcActiveEncounter *a = &world->encounter.active[enc_idx];
    RcNpc *boss = find_boss(world, a->boss_id);
    if (!boss) return;

    int def_hp = g_npc_defs[boss->def_id].hitpoints;
    int heal = p->heal_per_player;
    boss->current_hp += heal;
    if (boss->current_hp > def_hp) boss->current_hp = def_hp;
}

// drain_prayer_on_hit: KQ Barbed Spines prayer drain.
// Fired from rc_encounter_on_player_damaged when the boss lands a hit
// that deals post-mitigation damage > 0.
static void prim_drain_prayer_on_hit(RcWorld *world, int enc_idx,
                                     const void *params) {
    (void)enc_idx;
    const RcPrimParamsDrainPrayerOnHit *p = params;
    RcPlayer *pl = &world->player;
    if (pl->current_prayer_points >= p->points) {
        pl->current_prayer_points -= p->points;
    } else {
        pl->current_prayer_points = 0;
    }
}

// chain_magic_to_nearest_player: KQ Magic Bounce.
// Solo-only runtime (RuneC is single-player for now) — always a
// no-op. Registered so the spec is callable and multi-player pass
// can swap the implementation without schema changes.
static void prim_chain_magic_to_nearest(RcWorld *world, int enc_idx,
                                        const void *params) {
    (void)world; (void)enc_idx; (void)params;
}

// preserve_stat_drains_across_transition: KQ stat persistence.
// Still a callable stub. KQ's actual multi-form transition model needs
// hard-hp zero / timed transition support before this can auto-fire in
// a real fight.
static void prim_preserve_stat_drains(RcWorld *world, int enc_idx,
                                      const void *params) {
    (void)world; (void)enc_idx; (void)params;
}

// teleport_player_nearby: Chaos Elemental Confusion.
// Repositions the player around the boss on a random tile within the
// requested distance band. Arena-constrained placement remains a later
// extension; pass 2 just uses boss-relative tile offsets.
static void prim_teleport_player_nearby(RcWorld *world, int enc_idx,
                                        const void *params) {
    const RcPrimParamsTeleportPlayerNearby *p = params;
    RcActiveEncounter *a = &world->encounter.active[enc_idx];
    RcNpc *boss = find_boss(world, a->boss_id);
    if (!boss) return;

    int min_d = p->min_distance;
    int max_d = p->max_distance;
    if (max_d < min_d) {
        int tmp = min_d;
        min_d = max_d;
        max_d = tmp;
    }
    if (max_d <= 0) return;

    RcPlayer *pl = &world->player;
    for (int tries = 0; tries < 16; tries++) {
        int dx = rc_rng_range(&world->rng_state, max_d * 2 + 1) - max_d;
        int dy = rc_rng_range(&world->rng_state, max_d * 2 + 1) - max_d;
        int cheb = dx < 0 ? -dx : dx;
        int abs_dy = dy < 0 ? -dy : dy;
        if (abs_dy > cheb) cheb = abs_dy;
        if (cheb < min_d || cheb > max_d) continue;
        pl->prev_x = pl->x;
        pl->prev_y = pl->y;
        pl->x = boss->x + dx;
        pl->y = boss->y + dy;
        return;
    }
}

static void unequip_slot(RcPlayer *pl, int equip_slot) {
    if (equip_slot < 0 || equip_slot >= RC_EQUIP_COUNT) return;
    RcInvSlot *eq = &pl->equipment[equip_slot];
    if (eq->item_id < 0) return;
    int inv_slot = rc_inv_free_slot(pl->inventory);
    if (inv_slot < 0) return;
    pl->inventory[inv_slot] = *eq;
    eq->item_id = -1;
    eq->quantity = 0;
}

// unequip_player_items: Chaos Elemental Madness.
// Moves equipped items back into inventory, prioritising the weapon
// slot when requested.
static void prim_unequip_player_items(RcWorld *world, int enc_idx,
                                      const void *params) {
    (void)enc_idx;
    const RcPrimParamsUnequipPlayerItems *p = params;
    RcPlayer *pl = &world->player;
    uint16_t mask = p->slot_mask;
    uint8_t removed = 0;

    if (p->weapon_priority &&
        (mask & (1u << EQUIP_WEAPON)) &&
        pl->equipment[EQUIP_WEAPON].item_id >= 0) {
        int before = pl->equipment[EQUIP_WEAPON].item_id;
        unequip_slot(pl, EQUIP_WEAPON);
        if (pl->equipment[EQUIP_WEAPON].item_id != before) removed++;
    }

    for (int slot = 0; slot < RC_EQUIP_COUNT && removed < p->count; slot++) {
        if (!(mask & (1u << slot))) continue;
        if (p->weapon_priority && slot == EQUIP_WEAPON) continue;
        if (pl->equipment[slot].item_id < 0) continue;
        int before = pl->equipment[slot].item_id;
        unequip_slot(pl, slot);
        if (pl->equipment[slot].item_id != before) removed++;
    }

    rc_recalc_bonuses(pl);
}

// ---- Registry ----------------------------------------------------------

// Indexed by primitive_id (see encounter.h enum). NULL entries are
// unimplemented-in-C primitives — the loader leaves the mechanic's
// prim pointer NULL and the tick loop skips it.
static const RcEncounterPrimFn PRIM_TABLE[RC_PRIM_MAX] = {
    [RC_PRIM_TELEGRAPHED_AOE_TILE]   = prim_telegraphed_aoe_tile,
    [RC_PRIM_SPAWN_NPCS]             = prim_spawn_npcs,
    [RC_PRIM_SPAWN_NPCS_ONCE]        = prim_spawn_npcs_once,
    [RC_PRIM_HEAL_AT_OBJECT]         = prim_heal_at_object,
    [RC_PRIM_PERIODIC_HEAL_BOSS]     = prim_periodic_heal_boss,
    [RC_PRIM_DRAIN_PRAYER_ON_HIT]    = prim_drain_prayer_on_hit,
    [RC_PRIM_CHAIN_MAGIC_TO_NEAREST] = prim_chain_magic_to_nearest,
    [RC_PRIM_PRESERVE_STAT_DRAINS]   = prim_preserve_stat_drains,
    [RC_PRIM_TELEPORT_PLAYER_NEARBY] = prim_teleport_player_nearby,
    [RC_PRIM_UNEQUIP_PLAYER_ITEMS]   = prim_unequip_player_items,
};

RcEncounterPrimFn rc_encounter_prim_lookup(uint8_t primitive_id) {
    if (primitive_id >= RC_PRIM_MAX) return NULL;
    return PRIM_TABLE[primitive_id];
}
