#include "encounter.h"
#include "types.h"
#include "combat.h"
#include "events.h"
#include "items.h"
#include "npc.h"
#include "prayer.h"
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

static int find_npc_def_idx_by_cache_id(uint32_t npc_id) {
    for (int i = 0; i < g_npc_def_count; i++) {
        if (g_npc_defs[i].id == npc_id) return i;
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

static RcNpc *find_live_npc_named(RcWorld *world, const char *name) {
    int def_idx = find_npc_def_idx_by_name(name);
    if (def_idx < 0) return NULL;
    for (int i = 0; i < world->npc_count; i++) {
        RcNpc *npc = &world->npcs[i];
        if (!npc->active || npc->is_dead) continue;
        if (npc->def_id == def_idx) return npc;
    }
    return NULL;
}

static int spawn_named_around_boss(RcWorld *world, RcNpc *boss,
                                   const char *name, uint8_t count,
                                   int radius) {
    if (!world || !boss || !name || !name[0] || count == 0) return 0;
    int def_idx = find_npc_def_idx_by_name(name);
    if (def_idx < 0) return 0;
    static const int ring_dx[8] = { 3, 3, 0, -3, -3, -3, 0, 3 };
    static const int ring_dy[8] = { 0, 3, 3, 3, 0, -3, -3, -3 };
    if (radius <= 0) radius = 3;
    int spawned = 0;
    for (uint8_t i = 0; i < count && i < 16; i++) {
        int slot = i & 7;
        int x = boss->x + (ring_dx[slot] / 3) * radius;
        int y = boss->y + (ring_dy[slot] / 3) * radius;
        rc_npc_spawn(world, def_idx, x, y, boss->plane);
        spawned++;
    }
    return spawned;
}

static bool spawned_and_all_dead_named(const RcWorld *world,
                                       const char *name) {
    int def_idx = find_npc_def_idx_by_name(name);
    if (def_idx < 0) return false;
    bool seen = false;
    for (int i = 0; i < world->npc_count; i++) {
        const RcNpc *npc = &world->npcs[i];
        if (!npc->active || npc->def_id != def_idx) continue;
        seen = true;
        if (!npc->is_dead) return false;
    }
    return seen;
}

static int phase_index_by_id(const RcEncounterSpec *spec, const char *id) {
    for (int i = 0; i < spec->phase_count; i++) {
        if (strcmp(spec->phases[i].id, id) == 0) return i;
    }
    return -1;
}

static uint32_t protect_prayer_for_style(uint8_t style) {
    switch (style) {
        case COMBAT_MELEE_STAB:
        case COMBAT_MELEE_SLASH:
        case COMBAT_MELEE_CRUSH:
            return PRAYER_PROTECT_MELEE;
        case COMBAT_RANGED:
            return PRAYER_PROTECT_RANGE;
        case COMBAT_MAGIC:
            return PRAYER_PROTECT_MAGIC;
        default:
            return 0;
    }
}

static int roll_damage(RcWorld *world, int min, int max) {
    if (max < min) max = min;
    if (max <= 0) return 0;
    return min + rc_rng_range(&world->rng_state, max - min);
}

static void drain_prayer_percent(RcPlayer *pl, uint8_t pct) {
    if (pct == 0 || pl->current_prayer_points <= 0) return;
    int drain = (pl->current_prayer_points * pct) / 100;
    if (drain <= 0) drain = 1;
    pl->current_prayer_points -= drain;
    if (pl->current_prayer_points < 0) pl->current_prayer_points = 0;
}

static void drain_prayer_points(RcPlayer *pl, uint8_t points) {
    if (points == 0 || pl->current_prayer_points <= 0) return;
    pl->current_prayer_points -= points;
    if (pl->current_prayer_points < 0) pl->current_prayer_points = 0;
}

static bool player_has_equipped_item(const RcPlayer *pl, int item_id) {
    if (!pl || item_id < 0) return false;
    for (int i = 0; i < RC_EQUIP_COUNT; i++) {
        if (pl->equipment[i].item_id == item_id) return true;
    }
    return false;
}

static uint8_t soul_prayer_drain(const RcPlayer *pl,
                                 const RcPrimParamsSpawnSoulAttackers *p) {
    bool ward = pl->ward_of_arceuus_timer > 0;
    bool spectral = p->spectral_item_id &&
        player_has_equipped_item(pl, p->spectral_item_id);
    if (ward && spectral && p->prayer_drain_with_both) {
        return p->prayer_drain_with_both;
    }
    if (ward && p->prayer_drain_with_ward) {
        return p->prayer_drain_with_ward;
    }
    if (spectral && p->prayer_drain_with_spectral) {
        return p->prayer_drain_with_spectral;
    }
    return p->prayer_drain_if_blocked;
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
        int x = boss->x + ring_dx[slot];
        int y = boss->y + ring_dy[slot];
        if (p->freeze_player_ticks && i == 0) {
            x = world->player.x + (world->player.x >= boss->x ? 10 : -10);
            y = world->player.y;
        }
        rc_npc_spawn(world, def_idx,
                     x, y, boss->plane);
    }
    if (p->freeze_player_ticks) world->player.freeze_timer = p->freeze_player_ticks;
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

// teleport_on_incoming_attack: chance-based escape after a player hit.
// Destination is a local arena placeholder until exact lair floor
// anchors are available.
static void prim_teleport_on_incoming_attack(RcWorld *world, int enc_idx,
                                             const void *params) {
    const RcPrimParamsTeleportOnIncomingAttack *p = params;
    RcActiveEncounter *a = &world->encounter.active[enc_idx];
    RcNpc *boss = find_boss(world, a->boss_id);
    if (!boss || boss->def_id < 0 || boss->def_id >= g_npc_def_count) return;
    int max_hp = g_npc_defs[boss->def_id].hitpoints;
    if (max_hp <= 0) return;
    int hp_pct = boss->current_hp * 100 / max_hp;
    if (hp_pct < p->hp_min_pct || hp_pct > p->hp_max_pct) return;
    if (p->chance_pct == 0 ||
            rc_rng_range(&world->rng_state, 99) >= p->chance_pct) {
        return;
    }

    boss->prev_x = boss->x;
    boss->prev_y = boss->y;
    for (int tries = 0; tries < 8; tries++) {
        int dx = rc_rng_range(&world->rng_state, 24) - 12;
        int dy = rc_rng_range(&world->rng_state, 24) - 12;
        if (dx == 0 && dy == 0) continue;
        boss->x += dx;
        boss->y += dy;
        break;
    }
    if (p->drops_aggression) boss->target_uid = -1;
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

// positional_aoe: fire when the player is standing under the boss.
static void prim_positional_aoe(RcWorld *world, int enc_idx,
                                const void *params) {
    const RcPrimParamsPositionalAoe *p = params;
    RcActiveEncounter *a = &world->encounter.active[enc_idx];
    RcNpc *boss = find_boss(world, a->boss_id);
    if (!boss || boss->def_id < 0 || boss->def_id >= g_npc_def_count) return;

    int size = g_npc_defs[boss->def_id].size;
    if (size <= 0) size = 1;
    RcPlayer *pl = &world->player;
    if (pl->plane != boss->plane) return;
    if (pl->x < boss->x || pl->x >= boss->x + size ||
            pl->y < boss->y || pl->y >= boss->y + size) {
        return;
    }

    int min = p->damage_min;
    int max = p->damage_max >= p->damage_min ? p->damage_max : p->damage_min;
    int dmg = min;
    if (max > min) dmg += rc_rng_range(&world->rng_state, max - min);
    rc_queue_hit(pl->pending_hits, &pl->num_pending_hits, dmg, 0,
                 COMBAT_MELEE_CRUSH, boss->uid,
                 p->prayer_ignorable ? 0u : pl->active_prayers,
                 world->tick);
}

static void prim_spawn_leech_npc(RcWorld *world, int enc_idx,
                                 const void *params) {
    const RcPrimParamsSpawnLeechNpc *p = params;
    if (!p->name[0]) return;
    RcActiveEncounter *a = &world->encounter.active[enc_idx];
    RcNpc *boss = find_boss(world, a->boss_id);
    if (!boss) return;

    RcNpc *leech = find_live_npc_named(world, p->name);
    if (!leech) {
        if (p->spawn_cooldown_ticks &&
                (world->tick % p->spawn_cooldown_ticks) != 0) {
            return;
        }
        int def_idx = find_npc_def_idx_by_name(p->name);
        if (def_idx < 0) return;
        int idx = rc_npc_spawn(world, def_idx, boss->x + 1, boss->y,
                               boss->plane);
        if (idx < 0) return;
        leech = &world->npcs[idx];
    }

    RcPlayer *pl = &world->player;
    if (pl->plane != leech->plane) return;
    int dx = pl->x - leech->x;
    int dy = pl->y - leech->y;
    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;
    if (dx <= 1 && dy <= 1) {
        int amount = p->leech_per_tick;
        if (amount <= 0) return;
        if (p->poison_weak && leech->poison_damage > 0 &&
                p->poisoned_leech_period_ticks > 1 &&
                (world->tick % p->poisoned_leech_period_ticks) != 0) {
            return;
        }
        pl->current_hp -= amount * 10;
        if (pl->current_hp < 0) pl->current_hp = 0;
        if (p->heals_boss) {
            int cap = g_npc_defs[boss->def_id].hitpoints;
            boss->current_hp += amount;
            if (boss->current_hp > cap) boss->current_hp = cap;
        }
        return;
    }

    leech->prev_x = leech->x;
    leech->prev_y = leech->y;
    leech->x = pl->x + (pl->x >= boss->x ? 1 : -1);
    leech->y = pl->y;
}

static void prim_attack_counter_special(RcWorld *world, int enc_idx,
                                        const void *params) {
    const RcPrimParamsAttackCounterSpecial *p = params;
    RcActiveEncounter *a = &world->encounter.active[enc_idx];
    RcNpc *boss = find_boss(world, a->boss_id);
    if (!boss) return;
    RcPlayer *pl = &world->player;

    if (p->sequence_count) {
        int max = p->max_hit;
        for (int i = 0; i < p->sequence_count && i < 4; i++) {
            int style = p->sequence_styles[i];
            if (style == COMBAT_NONE) continue;
            rc_queue_hit(pl->pending_hits, &pl->num_pending_hits,
                         roll_damage(world, p->min_hit, max),
                         p->sequence_delays[i], style, boss->uid,
                         p->prayer_ignorable ? 0u : pl->active_prayers,
                         world->tick);
        }
    } else if (p->max_hit) {
        int style = p->style ? p->style : COMBAT_MELEE_CRUSH;
        rc_queue_hit(pl->pending_hits, &pl->num_pending_hits,
                     roll_damage(world, p->min_hit, p->max_hit), 0,
                     style, boss->uid,
                     p->prayer_ignorable ? 0u : pl->active_prayers,
                     world->tick);
    }

    drain_prayer_percent(pl, p->drain_prayer_pct);
}

static void prim_spawn_soul_attackers(RcWorld *world, int enc_idx,
                                      const void *params) {
    const RcPrimParamsSpawnSoulAttackers *p = params;
    RcActiveEncounter *a = &world->encounter.active[enc_idx];
    RcNpc *boss = find_boss(world, a->boss_id);
    if (!boss) return;
    RcPlayer *pl = &world->player;

    for (int i = 0; i < p->style_count && i < 3; i++) {
        uint8_t style = p->styles[i];
        rc_encounter_add_effect(world, RC_ENC_EFFECT_TRAVELLING_SOUL,
                                boss->x + i - 1, boss->y - 3, boss->plane,
                                pl->x, pl->y, 5, (uint16_t)boss->uid,
                                style, 0, "Summoned Soul", "");
        uint32_t prayer = protect_prayer_for_style(style);
        if (prayer && (pl->active_prayers & prayer)) {
            drain_prayer_points(pl, soul_prayer_drain(pl, p));
            continue;
        }
        rc_queue_hit(pl->pending_hits, &pl->num_pending_hits,
                     p->damage_if_unblocked, 0, style, boss->uid,
                     pl->active_prayers, world->tick);
    }
}

static void prim_dot_tile_placement(RcWorld *world, int enc_idx,
                                    const void *params) {
    const RcPrimParamsDotTilePlacement *p = params;
    RcActiveEncounter *a = &world->encounter.active[enc_idx];
    RcNpc *boss = find_boss(world, a->boss_id);
    if (!boss || p->dot_per_tick == 0) return;
    int duration = p->duration_ticks ? p->duration_ticks : 10;
    int count = p->pool_count;
    int on_player = p->pools_on_player;
    if (on_player > count) on_player = count;
    for (int i = 0; i < count; i++) {
        int x = world->player.x;
        int y = world->player.y;
        if (i >= on_player) {
            x += rc_rng_range(&world->rng_state, 6) - 3;
            y += rc_rng_range(&world->rng_state, 6) - 3;
        }
        rc_encounter_add_effect(world, RC_ENC_EFFECT_LAVA_POOL,
                                x, y, world->player.plane,
                                x, y, duration, (uint16_t)boss->uid,
                                COMBAT_MAGIC, p->dot_per_tick,
                                "Lava Pool", "");
    }
}

static void prim_static_dot_line(RcWorld *world, int enc_idx,
                                 const void *params) {
    const RcPrimParamsStaticDotLine *p = params;
    RcActiveEncounter *a = &world->encounter.active[enc_idx];
    RcNpc *boss = find_boss(world, a->boss_id);
    if (!boss || p->damage_per_cross == 0) return;
    rc_queue_hit(world->player.pending_hits, &world->player.num_pending_hits,
                 p->damage_per_cross, 0, COMBAT_MAGIC, boss->uid,
                 world->player.active_prayers, world->tick);
}

static void prim_regen_when_no_player(RcWorld *world, int enc_idx,
                                      const void *params) {
    (void)world; (void)enc_idx; (void)params;
}

// spawn_hidden_minions: spawns hidden backing NPCs plus click-target
// presentation entries. The object/action layer consumes the entries.
static void prim_spawn_hidden_minions(RcWorld *world, int enc_idx,
                                      const void *params) {
    const RcPrimParamsSpawnHiddenMinions *p = params;
    if (!p->name[0] || p->count == 0) return;

    int def_idx = find_npc_def_idx_by_name(p->name);
    if (def_idx < 0) return;
    RcActiveEncounter *a = &world->encounter.active[enc_idx];
    RcNpc *boss = find_boss(world, a->boss_id);
    if (!boss) return;

    static const int ring_dx[8] = { 3, 3, 0, -3, -3, -3, 0, 3 };
    static const int ring_dy[8] = { 0, 3, 3, 3, 0, -3, -3, -3 };
    for (uint8_t i = 0; i < p->count && i < 16; i++) {
        int slot = i & 7;
        int idx = rc_npc_spawn(world, def_idx,
                               boss->x + ring_dx[slot],
                               boss->y + ring_dy[slot],
                               boss->plane);
        if (idx < 0) continue;
        world->npcs[idx].player_untargetable = true;
        world->npcs[idx].target_uid = -1;
        if (p->object_name[0]) {
            rc_encounter_add_effect(world, RC_ENC_EFFECT_HIDDEN_OBJECT,
                                    world->npcs[idx].x, world->npcs[idx].y,
                                    world->npcs[idx].plane,
                                    world->npcs[idx].x, world->npcs[idx].y,
                                    -1, (uint16_t)boss->uid,
                                    COMBAT_NONE, 0,
                                    p->object_name, p->name);
        }
    }
}

static void prim_phase_advance_on_condition(RcWorld *world, int enc_idx,
                                            const void *params) {
    const RcPrimParamsPhaseAdvanceOnCondition *p = params;
    if (!spawned_and_all_dead_named(world, p->npc_name)) return;
    RcActiveEncounter *a = &world->encounter.active[enc_idx];
    const RcEncounterSpec *spec = &world->encounter.registry[a->spec_idx];
    int next = phase_index_by_id(spec, p->target_phase);
    if (next < 0 || next == a->current_phase) return;

    uint8_t old = a->current_phase;
    a->current_phase = (uint8_t)next;
    RcPayloadPhaseTransition payload = {
        .npc_id = a->boss_id,
        .old_phase = old,
        .new_phase = (uint8_t)next,
    };
    rc_event_fire(world, RC_EVT_PHASE_TRANSITION, &payload);
}

static void prim_form_transition_dive(RcWorld *world, int enc_idx,
                                      const void *params) {
    const RcPrimParamsFormTransitionDive *p = params;
    RcActiveEncounter *a = &world->encounter.active[enc_idx];
    const RcEncounterSpec *spec = &world->encounter.registry[a->spec_idx];
    RcNpc *boss = find_boss(world, a->boss_id);
    if (!boss) return;

    if (p->untargetable_during_dive) boss->player_untargetable = true;
    int ticks = p->dive_ticks + p->resurface_ticks;
    if (ticks <= 0) ticks = 1;
    rc_encounter_add_effect(world, RC_ENC_EFFECT_FORM_DIVE,
                            boss->x, boss->y, boss->plane,
                            boss->x, boss->y, ticks,
                            (uint16_t)boss->uid, COMBAT_NONE, 0,
                            "Form Dive", "");

    if (spec->npc_id_count > 1) {
        a->attack_special_toggle =
            (uint8_t)((a->attack_special_toggle + 1) % spec->npc_id_count);
        int def_idx = find_npc_def_idx_by_cache_id(
            spec->npc_ids[a->attack_special_toggle]);
        if (def_idx >= 0) {
            boss->def_id = def_idx;
            a->def_id = spec->npc_ids[a->attack_special_toggle];
        }
    }

    static const int dx[4] = { 0, -5, 5, 0 };
    static const int dy[4] = { -5, 0, 0, 5 };
    int slot = a->attack_special_toggle & 3;
    boss->prev_x = boss->x;
    boss->prev_y = boss->y;
    boss->x = boss->spawn_x + dx[slot];
    boss->y = boss->spawn_y + dy[slot];
}

static void prim_attack_counter_alternate_special(RcWorld *world, int enc_idx,
                                                  const void *params) {
    const RcPrimParamsAttackCounterAlternateSpecial *p = params;
    RcActiveEncounter *a = &world->encounter.active[enc_idx];
    const RcEncounterSpec *spec = &world->encounter.registry[a->spec_idx];
    if (p->special_count == 0) return;

    uint8_t slot = a->attack_special_toggle % p->special_count;
    const char *name = p->special_names[slot];
    a->attack_special_toggle++;
    for (int i = 0; i < spec->mechanic_count; i++) {
        const RcEncounterMechanic *m = &spec->mechanics[i];
        if (strcmp(m->name, name) != 0 || !m->prim) continue;
        a->invoking_mechanic_idx = (uint8_t)i;
        m->prim(world, enc_idx, m->param_block);
        a->invoking_mechanic_idx = 0xFFu;
        a->active_mechanic_idx = (uint8_t)i;
        a->active_mechanic_ticks = 24;
        if (m->primitive_id == RC_PRIM_COVERED_ARENA_ENVIRONMENT) {
            const RcPrimParamsCoveredArenaEnvironment *env =
                (const RcPrimParamsCoveredArenaEnvironment *)m->param_block;
            if (env->duration_ticks) a->active_mechanic_ticks = env->duration_ticks;
        }
        return;
    }
}

static void prim_covered_arena_environment(RcWorld *world, int enc_idx,
                                           const void *params) {
    const RcPrimParamsCoveredArenaEnvironment *p = params;
    RcActiveEncounter *a = &world->encounter.active[enc_idx];
    RcNpc *boss = find_boss(world, a->boss_id);
    if (!boss || p->dot_per_tick == 0) return;
    int duration = p->duration_ticks ? p->duration_ticks : 24;
    int count = p->pool_count ? p->pool_count : 10;
    static const int dx[10] = { 0, 2, -2, 4, -4, 0, 3, -3, 5, -5 };
    static const int dy[10] = { 0, 2, -2, -2, 2, 4, 0, 0, 3, -3 };
    for (int i = 0; i < count && i < 10; i++) {
        int x = world->player.x + dx[i];
        int y = world->player.y + dy[i];
        rc_encounter_add_effect(world, RC_ENC_EFFECT_ACID_POOL,
                                x, y, world->player.plane,
                                x, y, duration, (uint16_t)boss->uid,
                                COMBAT_MAGIC, p->dot_per_tick,
                                "Acid Pool", "");
    }
}

static void prim_damage_reduction_until_trigger(RcWorld *world, int enc_idx,
                                                const void *params) {
    (void)params;
    if (enc_idx < 0 || enc_idx >= RC_ENC_MAX_ACTIVE) return;
    RcActiveEncounter *a = &world->encounter.active[enc_idx];
    if (!a->active || a->invoking_mechanic_idx == 0xFFu) return;
    a->active_mechanic_idx = a->invoking_mechanic_idx;
    a->active_mechanic_ticks = 0;
}

static void prim_animation_warning_style_swap(RcWorld *world, int enc_idx,
                                              const void *params) {
    const RcPrimParamsStyleSwap *p = params;
    RcActiveEncounter *a = &world->encounter.active[enc_idx];
    RcNpc *boss = find_boss(world, a->boss_id);
    if (!boss) return;
    rc_encounter_add_effect(world, RC_ENC_EFFECT_ROOM_ATTACK,
                            boss->x, boss->y, boss->plane,
                            world->player.x, world->player.y,
                            p->warning_ticks ? p->warning_ticks : 1,
                            (uint16_t)boss->uid, COMBAT_NONE, 0,
                            "Style Warning", "");
}

static void prim_converging_aoe(RcWorld *world, int enc_idx,
                                const void *params) {
    const RcPrimParamsConvergingAoe *p = params;
    RcActiveEncounter *a = &world->encounter.active[enc_idx];
    RcNpc *boss = find_boss(world, a->boss_id);
    if (!boss || p->damage_per_bolt == 0) return;
    int count = p->bolt_count ? p->bolt_count : 3;
    static const int dx[4] = { -6, 6, 0, 0 };
    static const int dy[4] = { 0, 0, -6, 6 };
    for (int i = 0; i < count && i < 4; i++) {
        rc_encounter_add_effect(world, RC_ENC_EFFECT_ROOM_ATTACK,
                                world->player.x + dx[i],
                                world->player.y + dy[i],
                                world->player.plane,
                                world->player.x, world->player.y,
                                p->warning_ticks ? p->warning_ticks : 2,
                                (uint16_t)boss->uid, COMBAT_MAGIC,
                                p->damage_per_bolt,
                                "Converging AOE", "");
    }
    rc_queue_hit(world->player.pending_hits, &world->player.num_pending_hits,
                 p->damage_per_bolt, p->warning_ticks ? p->warning_ticks : 2,
                 COMBAT_MAGIC, boss->uid, world->player.active_prayers,
                 world->tick);
}

static void prim_stun_then_fire_walls(RcWorld *world, int enc_idx,
                                      const void *params) {
    const RcPrimParamsStunFireWalls *p = params;
    RcActiveEncounter *a = &world->encounter.active[enc_idx];
    RcNpc *boss = find_boss(world, a->boss_id);
    if (!boss) return;
    if (p->stun_ticks > world->player.freeze_timer) {
        world->player.freeze_timer = p->stun_ticks;
    }
    int duration = p->duration_ticks ? p->duration_ticks : 8;
    static const int dx[4] = { -1, 1, 0, 0 };
    static const int dy[4] = { 0, 0, -1, 1 };
    for (int i = 0; i < 4; i++) {
        rc_encounter_add_effect(world, RC_ENC_EFFECT_LAVA_POOL,
                                world->player.x + dx[i],
                                world->player.y + dy[i],
                                world->player.plane,
                                world->player.x, world->player.y,
                                duration, (uint16_t)boss->uid,
                                COMBAT_MAGIC, p->fire_wall_damage,
                                "Fire Wall", "");
    }
}

static void prim_moving_dot_line(RcWorld *world, int enc_idx,
                                 const void *params) {
    const RcPrimParamsMovingDotLine *p = params;
    RcActiveEncounter *a = &world->encounter.active[enc_idx];
    RcNpc *boss = find_boss(world, a->boss_id);
    if (!boss || p->dot_per_tick == 0) return;
    int duration = p->duration_ticks ? p->duration_ticks : 8;
    int len = p->trail_length ? p->trail_length : 4;
    int step_x = world->player.x >= boss->x ? -1 : 1;
    for (int i = 0; i < len && i < 12; i++) {
        rc_encounter_add_effect(world, RC_ENC_EFFECT_LAVA_POOL,
                                world->player.x + step_x * i,
                                world->player.y,
                                world->player.plane,
                                world->player.x, world->player.y,
                                duration, (uint16_t)boss->uid,
                                COMBAT_MAGIC, p->dot_per_tick,
                                "Moving DOT Line", "");
    }
    if (p->slows_player && world->player.freeze_timer < 2) {
        world->player.freeze_timer = 2;
    }
}

static void prim_object_interaction_ticked(RcWorld *world, int enc_idx,
                                           const void *params) {
    (void)world;
    (void)enc_idx;
    (void)params;
}

static void prim_totem_charge_progression(RcWorld *world, int enc_idx,
                                          const void *params) {
    const RcPrimParamsTotemCharge *p = params;
    RcActiveEncounter *a = &world->encounter.active[enc_idx];
    RcNpc *boss = find_boss(world, a->boss_id);
    if (!boss) return;
    int total = p->totem_count * p->total_charge_per_totem;
    if (total <= 0) total = 400;
    a->mechanic_progress += p->charge_per_attack ? p->charge_per_attack : 10;
    if (a->mechanic_progress < (uint16_t)total) return;
    a->mechanic_progress = 0;
    int dmg = p->advance_damage ? p->advance_damage : 333;
    boss->current_hp -= dmg;
    if (boss->current_hp < 1) boss->current_hp = 1;
    const RcEncounterSpec *spec = &world->encounter.registry[a->spec_idx];
    if (a->current_phase + 1 < spec->phase_count) {
        rc_encounter_set_phase(world, enc_idx,
                               spec->phases[a->current_phase + 1].id);
    }
}

static void prim_telegraphed_portal_aoe(RcWorld *world, int enc_idx,
                                        const void *params) {
    const RcPrimParamsTelegraphedPortalAoe *p = params;
    RcActiveEncounter *a = &world->encounter.active[enc_idx];
    RcNpc *boss = find_boss(world, a->boss_id);
    if (!boss || p->damage_max == 0) return;
    int delay = p->warning_ticks ? p->warning_ticks : 2;
    rc_encounter_add_effect(world, RC_ENC_EFFECT_ROOM_ATTACK,
                            boss->x, boss->y, boss->plane,
                            world->player.x, world->player.y,
                            delay, (uint16_t)boss->uid, COMBAT_MAGIC,
                            p->damage_max, "Portal AOE", "");
    rc_queue_hit(world->player.pending_hits, &world->player.num_pending_hits,
                 p->damage_max, delay, COMBAT_MAGIC, boss->uid,
                 p->prayer_ignorable ? 0u : world->player.active_prayers,
                 world->tick);
}

static void prim_spawn_paired_husks(RcWorld *world, int enc_idx,
                                    const void *params) {
    const RcPrimParamsSpawnPairedHusks *p = params;
    RcActiveEncounter *a = &world->encounter.active[enc_idx];
    RcNpc *boss = find_boss(world, a->boss_id);
    if (!boss) return;
    if (p->immobilizes_target && world->player.freeze_timer < 8) {
        world->player.freeze_timer = 8;
    }
    uint8_t styles[2] = {
        p->blue_style ? p->blue_style : COMBAT_MAGIC,
        p->green_style ? p->green_style : COMBAT_RANGED,
    };
    for (int i = 0; i < 2; i++) {
        rc_queue_hit(world->player.pending_hits, &world->player.num_pending_hits,
                     12, 2 + i, styles[i], boss->uid,
                     world->player.active_prayers, world->tick);
    }
}

static void prim_quadrant_safe_zone_dot(RcWorld *world, int enc_idx,
                                        const void *params) {
    const RcPrimParamsQuadrantSafeZoneDot *p = params;
    RcActiveEncounter *a = &world->encounter.active[enc_idx];
    RcNpc *boss = find_boss(world, a->boss_id);
    if (!boss || p->unsafe_dot_per_tick == 0) return;
    int safe = rc_rng_range(&world->rng_state, 3);
    int player_quad = (world->player.x >= boss->x ? 1 : 0) |
                      (world->player.y >= boss->y ? 2 : 0);
    if (player_quad != safe) {
        rc_queue_hit(world->player.pending_hits, &world->player.num_pending_hits,
                     p->unsafe_dot_per_tick, 0, COMBAT_MAGIC, boss->uid,
                     world->player.active_prayers, world->tick);
        int heal = p->unsafe_dot_per_tick *
                   (p->heals_boss_multiplier_x10 ?
                    p->heals_boss_multiplier_x10 : 10) / 10;
        int cap = g_npc_defs[boss->def_id].hitpoints;
        boss->current_hp += heal;
        if (boss->current_hp > cap) boss->current_hp = cap;
    }
    rc_encounter_add_effect(world, RC_ENC_EFFECT_LAVA_POOL,
                            world->player.x, world->player.y,
                            world->player.plane,
                            world->player.x, world->player.y,
                            p->duration_ticks ? p->duration_ticks : 12,
                            (uint16_t)boss->uid, COMBAT_MAGIC,
                            p->unsafe_dot_per_tick, "Unsafe Quadrant", "");
}

static void prim_shuffle_player_prayers(RcWorld *world, int enc_idx,
                                        const void *params) {
    (void)enc_idx;
    const RcPrimParamsShufflePlayerPrayers *p = params;
    uint32_t prayers = world->player.active_prayers;
    uint32_t out = prayers;
    if (prayers & PRAYER_PROTECT_MAGIC) {
        out &= ~PRAYER_PROTECT_MAGIC;
        out |= PRAYER_PROTECT_RANGE;
    }
    if (prayers & PRAYER_PROTECT_RANGE) {
        out &= ~PRAYER_PROTECT_RANGE;
        out |= PRAYER_PROTECT_MELEE;
    }
    if (prayers & PRAYER_PROTECT_MELEE) {
        out &= ~PRAYER_PROTECT_MELEE;
        out |= PRAYER_PROTECT_MAGIC;
    }
    world->player.active_prayers = out;
    world->player.attack_timer += p->duration_attacks ? 1 : 0;
}

static void prim_infectious_dot_with_cure(RcWorld *world, int enc_idx,
                                          const void *params) {
    const RcPrimParamsInfectiousDot *p = params;
    RcActiveEncounter *a = &world->encounter.active[enc_idx];
    RcNpc *boss = find_boss(world, a->boss_id);
    if (!boss || p->burst_damage == 0) return;
    rc_queue_hit(world->player.pending_hits, &world->player.num_pending_hits,
                 p->burst_damage,
                 p->incubation_ticks ? p->incubation_ticks : 18,
                 COMBAT_MAGIC, boss->uid, world->player.active_prayers,
                 world->tick);
}

static void prim_spawn_convergent_minions(RcWorld *world, int enc_idx,
                                          const void *params) {
    const RcPrimParamsSpawnConvergentMinions *p = params;
    RcActiveEncounter *a = &world->encounter.active[enc_idx];
    RcNpc *boss = find_boss(world, a->boss_id);
    if (!boss) return;
    int count = p->count_per_player ? p->count_per_player : 1;
    int def_idx = p->npc_name[0] ? find_npc_def_idx_by_name(p->npc_name) : -1;
    for (int i = 0; i < count && i < 8; i++) {
        if (def_idx >= 0) {
            rc_npc_spawn(world, def_idx, boss->x + i - 1, boss->y - 4,
                         boss->plane);
        }
    }
    if (p->power_blast_on_absorption) {
        int dmg = p->power_blast_min +
                  count * p->absorb_damage_per_walker;
        rc_queue_hit(world->player.pending_hits, &world->player.num_pending_hits,
                     dmg, 6, COMBAT_MAGIC, boss->uid,
                     world->player.active_prayers, world->tick);
    }
}

static void prim_aoe_tile_debuff(RcWorld *world, int enc_idx,
                                 const void *params) {
    const RcPrimParamsAoeTileDebuff *p = params;
    RcActiveEncounter *a = &world->encounter.active[enc_idx];
    RcNpc *boss = find_boss(world, a->boss_id);
    if (!boss) return;
    if (p->run_disabled) world->player.running = false;
    if (p->attack_speed_penalty) world->player.attack_timer += p->attack_speed_penalty;
    if (p->debuff_ticks > world->player.freeze_timer) {
        world->player.freeze_timer = p->debuff_ticks;
    }
    rc_encounter_add_effect(world, RC_ENC_EFFECT_LAVA_POOL,
                            world->player.x, world->player.y,
                            world->player.plane,
                            world->player.x, world->player.y,
                            p->debuff_ticks ? p->debuff_ticks : 10,
                            (uint16_t)boss->uid, COMBAT_NONE, 0,
                            "Tile Debuff", "");
}

static void prim_line_dash(RcWorld *world, int enc_idx,
                           const void *params) {
    const RcPrimParamsLineDash *p = params;
    RcActiveEncounter *a = &world->encounter.active[enc_idx];
    RcNpc *boss = find_boss(world, a->boss_id);
    if (!boss || p->damage_max == 0) return;
    rc_encounter_add_effect(world, RC_ENC_EFFECT_ROOM_ATTACK,
                            boss->x, boss->y, boss->plane,
                            world->player.x, world->player.y,
                            2, (uint16_t)boss->uid, COMBAT_MELEE_CRUSH,
                            p->damage_max, "Line Dash", "");
    if (world->player.x == boss->x || world->player.y == boss->y) {
        rc_queue_hit(world->player.pending_hits, &world->player.num_pending_hits,
                     p->damage_max, 1, COMBAT_MELEE_CRUSH, boss->uid,
                     0u, world->tick);
    }
}

static void prim_spawn_objective_npcs(RcWorld *world, int enc_idx,
                                      const void *params) {
    const RcPrimParamsSpawnObjectiveNpcs *p = params;
    RcActiveEncounter *a = &world->encounter.active[enc_idx];
    RcNpc *boss = find_boss(world, a->boss_id);
    if (!boss || p->count == 0) return;
    int spawned = spawn_named_around_boss(world, boss, p->npc_name,
                                          p->count, 5);
    if (a->invoking_mechanic_idx != 0xFFu && spawned == 0 &&
            p->advance_to[0]) {
        a->active_mechanic_idx = a->invoking_mechanic_idx;
        a->active_mechanic_ticks = p->fallback_ticks
                                   ? p->fallback_ticks : 20;
    }
    rc_encounter_add_effect(world, RC_ENC_EFFECT_ROOM_ATTACK,
                            boss->x, boss->y, boss->plane,
                            world->player.x, world->player.y,
                            4, (uint16_t)boss->uid, COMBAT_NONE, 0,
                            "Objective NPCs", p->npc_name);
}

static void prim_npc_pathed_movement(RcWorld *world, int enc_idx,
                                     const void *params) {
    const RcPrimParamsNpcPathedMovement *p = params;
    RcActiveEncounter *a = &world->encounter.active[enc_idx];
    RcNpc *boss = find_boss(world, a->boss_id);
    if (!boss || a->invoking_mechanic_idx == 0xFFu) return;
    a->active_mechanic_idx = a->invoking_mechanic_idx;
    a->active_mechanic_ticks = p->ticks_to_complete
                               ? p->ticks_to_complete : 20;
    boss->prev_x = boss->x;
    boss->prev_y = boss->y;
    boss->x += world->player.x >= boss->x ? 3 : -3;
    boss->y += world->player.y >= boss->y ? 3 : -3;
    rc_encounter_add_effect(world, RC_ENC_EFFECT_ROOM_ATTACK,
                            boss->prev_x, boss->prev_y, boss->plane,
                            boss->x, boss->y,
                            a->active_mechanic_ticks,
                            (uint16_t)boss->uid, COMBAT_NONE, 0,
                            "Pathed Movement", "");
}

static void prim_spawn_wall_tentacles(RcWorld *world, int enc_idx,
                                      const void *params) {
    const RcPrimParamsSpawnWallTentacles *p = params;
    RcActiveEncounter *a = &world->encounter.active[enc_idx];
    RcNpc *boss = find_boss(world, a->boss_id);
    if (!boss || p->tentacle_count == 0) return;
    const char *name = p->npc_name[0] ? p->npc_name : "Abyssal tentacle";
    int spawned = spawn_named_around_boss(world, boss, name,
                                          p->tentacle_count, 6);
    if (a->invoking_mechanic_idx != 0xFFu && spawned == 0 &&
            p->advance_to[0]) {
        a->active_mechanic_idx = a->invoking_mechanic_idx;
        a->active_mechanic_ticks = p->fallback_ticks
                                   ? p->fallback_ticks : 20;
    }
    for (int i = 0; i < p->tentacle_count && i < 8; i++) {
        rc_encounter_add_effect(world, RC_ENC_EFFECT_ROOM_ATTACK,
                                boss->x + i - 2, boss->y + 6,
                                boss->plane,
                                world->player.x, world->player.y,
                                12, (uint16_t)boss->uid,
                                COMBAT_MELEE_CRUSH, 0,
                                "Wall Tentacle", name);
    }
}

static void prim_teleporting_tile_aoe(RcWorld *world, int enc_idx,
                                      const void *params) {
    const RcPrimParamsTeleportingTileAoe *p = params;
    RcActiveEncounter *a = &world->encounter.active[enc_idx];
    RcNpc *boss = find_boss(world, a->boss_id);
    if (!boss || p->damage_max == 0) return;
    if (p->boss_untargetable_during) boss->player_untargetable = true;
    int count = p->cloud_count ? p->cloud_count : 4;
    for (int i = 0; i < count && i < 12; i++) {
        int x = world->player.x + (i % 4) - 1;
        int y = world->player.y + (i / 4) - 1;
        rc_encounter_add_effect(world, RC_ENC_EFFECT_ACID_POOL,
                                x, y, world->player.plane,
                                x, y, 12, (uint16_t)boss->uid,
                                COMBAT_MAGIC, p->damage_max,
                                "Lightning Cloud", "");
    }
    boss->prev_x = boss->x;
    boss->prev_y = boss->y;
    boss->x = boss->spawn_x + 5;
    boss->y = boss->spawn_y + 5;
}

static void prim_homing_projectiles_with_walls(RcWorld *world, int enc_idx,
                                               const void *params) {
    const RcPrimParamsHomingProjectiles *p = params;
    RcActiveEncounter *a = &world->encounter.active[enc_idx];
    RcNpc *boss = find_boss(world, a->boss_id);
    if (!boss || p->damage_on_hit == 0) return;
    int min = p->spike_count_min;
    int max = p->spike_count_max >= min ? p->spike_count_max : min;
    int count = max > min ? min + rc_rng_range(&world->rng_state, max - min)
                          : (min ? min : 2);
    for (int i = 0; i < count && i < 8; i++) {
        rc_encounter_add_effect(world, RC_ENC_EFFECT_HIDDEN_OBJECT,
                                world->player.x + i - 1,
                                world->player.y + 2,
                                world->player.plane,
                                world->player.x, world->player.y,
                                p->harden_ticks ? p->harden_ticks : 33,
                                (uint16_t)boss->uid, COMBAT_NONE, 0,
                                "Muspah Spike", "");
    }
    rc_queue_hit(world->player.pending_hits, &world->player.num_pending_hits,
                 p->damage_on_hit, 4, COMBAT_RANGED, boss->uid,
                 world->player.active_prayers, world->tick);
}

static void prim_smite_drain_shield(RcWorld *world, int enc_idx,
                                    const void *params) {
    const RcPrimParamsSmiteDrainShield *p = params;
    RcActiveEncounter *a = &world->encounter.active[enc_idx];
    if (!a->shield_points) {
        a->shield_points = p->shield_hp ? p->shield_hp : 200;
        return;
    }
    int drain = p->hits_to_drain ? (p->shield_hp / p->hits_to_drain) : 10;
    if (world->player.active_prayers & (1u << RC_PRAYER_SMITE)) {
        drain += drain / 2;
    }
    if (drain <= 0) drain = 10;
    a->shield_points = a->shield_points > drain
                       ? (uint16_t)(a->shield_points - drain) : 0;
    if (a->shield_points == 0) {
        const RcEncounterSpec *spec = &world->encounter.registry[a->spec_idx];
        int idx = phase_index_by_id(spec, "final_combat");
        if (idx >= 0) rc_encounter_set_phase(world, enc_idx, "final_combat");
    }
}

static void prim_periodic_spike_cluster(RcWorld *world, int enc_idx,
                                        const void *params) {
    const RcPrimParamsPeriodicSpikeCluster *p = params;
    RcActiveEncounter *a = &world->encounter.active[enc_idx];
    RcNpc *boss = find_boss(world, a->boss_id);
    if (!boss || p->damage_on_hit == 0) return;
    int count = p->cluster_tiles ? p->cluster_tiles : 6;
    for (int i = 0; i < count && i < 12; i++) {
        int x = p->one_on_player && i == 0
                ? world->player.x : world->player.x + (i % 4) - 2;
        int y = p->one_on_player && i == 0
                ? world->player.y : world->player.y + (i / 4) - 1;
        rc_encounter_add_effect(world, RC_ENC_EFFECT_HIDDEN_OBJECT,
                                x, y, world->player.plane,
                                x, y, 24, (uint16_t)boss->uid,
                                COMBAT_NONE, 0, "Spike Cluster", "");
    }
    rc_queue_hit(world->player.pending_hits, &world->player.num_pending_hits,
                 p->damage_on_hit, 3, COMBAT_RANGED, boss->uid,
                 world->player.active_prayers, world->tick);
}

static void prim_moving_rotational_hazards(RcWorld *world, int enc_idx,
                                           const void *params) {
    const RcPrimParamsMovingRotationalHazards *p = params;
    RcActiveEncounter *a = &world->encounter.active[enc_idx];
    RcNpc *boss = find_boss(world, a->boss_id);
    if (!boss || p->damage_on_hit == 0) return;
    int count = p->hazard_count ? p->hazard_count : 4;
    int radius = p->orbit_radius ? p->orbit_radius : 3;
    static const int sx[4] = { 1, 0, -1, 0 };
    static const int sy[4] = { 0, 1, 0, -1 };
    for (int i = 0; i < count && i < 8; i++) {
        int slot = (i + (world->tick / (p->rotation_ticks ? p->rotation_ticks : 4))) & 3;
        int x = boss->x + sx[slot] * radius;
        int y = boss->y + sy[slot] * radius;
        rc_encounter_add_effect(world, RC_ENC_EFFECT_ROOM_ATTACK,
                                x, y, boss->plane,
                                world->player.x, world->player.y,
                                2, (uint16_t)boss->uid, COMBAT_MELEE_SLASH,
                                p->damage_on_hit, "Rotational Hazard", "");
        if (world->player.plane == boss->plane &&
                world->player.x == x && world->player.y == y) {
            rc_queue_hit(world->player.pending_hits,
                         &world->player.num_pending_hits,
                         p->damage_on_hit, 0, COMBAT_MELEE_SLASH,
                         boss->uid, 0u, world->tick);
        }
    }
}

static void prim_heal_boss_on_player_attack_miss(RcWorld *world, int enc_idx,
                                                 const void *params) {
    const RcPrimParamsHealOnAttack *p = params;
    RcActiveEncounter *a = &world->encounter.active[enc_idx];
    RcNpc *boss = find_boss(world, a->boss_id);
    if (!boss || a->invoking_mechanic_idx == 0xFFu) return;
    a->active_mechanic_idx = a->invoking_mechanic_idx;
    a->active_mechanic_ticks = p->warning_ticks ? p->warning_ticks : 4;
    rc_encounter_add_effect(world, RC_ENC_EFFECT_ROOM_ATTACK,
                            boss->x, boss->y, boss->plane,
                            world->player.x, world->player.y,
                            a->active_mechanic_ticks,
                            (uint16_t)boss->uid, COMBAT_MAGIC, 0,
                            "Mind Flay", "");
}

static void prim_tile_debuff_on_stand(RcWorld *world, int enc_idx,
                                      const void *params) {
    const RcPrimParamsTileDebuffOnStand *p = params;
    RcActiveEncounter *a = &world->encounter.active[enc_idx];
    RcNpc *boss = find_boss(world, a->boss_id);
    if (!boss) return;
    int count = p->tile_count ? p->tile_count : 3;
    for (int i = 0; i < count && i < 8; i++) {
        int x = world->player.x + (i % 3) - 1;
        int y = world->player.y + (i / 3) - 1;
        rc_encounter_add_effect(world, RC_ENC_EFFECT_LAVA_POOL,
                                x, y, world->player.plane,
                                x, y, 8, (uint16_t)boss->uid,
                                COMBAT_MAGIC, p->damage_on_step,
                                "Debuff Tile", "");
    }
    if (p->damage_on_step) {
        rc_queue_hit(world->player.pending_hits, &world->player.num_pending_hits,
                     p->damage_on_step, 0, COMBAT_MAGIC, boss->uid,
                     world->player.active_prayers, world->tick);
    }
    if (p->debuff_amount) {
        int amount = p->debuff_amount < 0 ? -p->debuff_amount : p->debuff_amount;
        world->player.skills.boosted_level[SKILL_DEFENCE] -= amount;
        if (world->player.skills.boosted_level[SKILL_DEFENCE] < 0) {
            world->player.skills.boosted_level[SKILL_DEFENCE] = 0;
        }
    }
}

static void prim_spawn_buff_zone_npc(RcWorld *world, int enc_idx,
                                     const void *params) {
    const RcPrimParamsSpawnBuffZoneNpc *p = params;
    RcActiveEncounter *a = &world->encounter.active[enc_idx];
    RcNpc *boss = find_boss(world, a->boss_id);
    if (!boss || !p->npc_name[0]) return;
    if (!find_live_npc_named(world, p->npc_name)) {
        int def_idx = find_npc_def_idx_by_name(p->npc_name);
        if (def_idx >= 0) {
            rc_npc_spawn(world, def_idx, boss->x - 4, boss->y + 4,
                         boss->plane);
        }
    }
    if (a->invoking_mechanic_idx != 0xFFu) {
        a->active_mechanic_idx = a->invoking_mechanic_idx;
        a->active_mechanic_ticks = 0;
    }
    rc_encounter_add_effect(world, RC_ENC_EFFECT_HIDDEN_OBJECT,
                            boss->x - 4, boss->y + 4, boss->plane,
                            boss->x - 4, boss->y + 4,
                            -1, (uint16_t)boss->uid, COMBAT_NONE, 0,
                            "Buff Zone", p->npc_name);
}

static void prim_one_shot_arena_effect(RcWorld *world, int enc_idx,
                                       const void *params) {
    const RcPrimParamsOneShotArenaEffect *p = params;
    RcActiveEncounter *a = &world->encounter.active[enc_idx];
    RcNpc *boss = find_boss(world, a->boss_id);
    if (!boss || p->damage_max == 0) return;
    int count = p->tile_count ? p->tile_count : 6;
    for (int i = 0; i < count && i < 12; i++) {
        rc_encounter_add_effect(world, RC_ENC_EFFECT_ROOM_ATTACK,
                                world->player.x + (i % 4) - 2,
                                world->player.y + (i / 4) - 1,
                                world->player.plane,
                                world->player.x, world->player.y,
                                2, (uint16_t)boss->uid,
                                COMBAT_RANGED, p->damage_max,
                                "Arena Effect", "");
    }
    rc_queue_hit(world->player.pending_hits, &world->player.num_pending_hits,
                 p->damage_max, 2, COMBAT_RANGED, boss->uid,
                 world->player.active_prayers, world->tick);
}

static void prim_player_sanity_tracker(RcWorld *world, int enc_idx,
                                       const void *params) {
    const RcPrimParamsPlayerSanityTracker *p = params;
    RcActiveEncounter *a = &world->encounter.active[enc_idx];
    if (!a->mechanic_progress) {
        a->mechanic_progress = p->max_sanity ? p->max_sanity : 100;
        return;
    }
    if ((world->player.active_prayers & (PRAYER_PROTECT_MAGIC |
                                         PRAYER_PROTECT_RANGE)) != 0) {
        uint16_t max = p->max_sanity ? p->max_sanity : 100;
        uint16_t regen = p->regen_per_correct_prayer_tick;
        a->mechanic_progress += regen;
        if (a->mechanic_progress > max) a->mechanic_progress = max;
    } else if (p->drain_per_wrong_prayer_tick) {
        uint16_t drain = p->drain_per_wrong_prayer_tick;
        a->mechanic_progress = a->mechanic_progress > drain
                               ? (uint16_t)(a->mechanic_progress - drain)
                               : 0;
    }
    if (a->mechanic_progress <= p->insane_threshold) {
        world->player.current_hp = 0;
    }
}

static void prim_spawn_tentacle_projectiles(RcWorld *world, int enc_idx,
                                            const void *params) {
    const RcPrimParamsSpawnTentacles *p = params;
    RcActiveEncounter *a = &world->encounter.active[enc_idx];
    RcNpc *boss = find_boss(world, a->boss_id);
    if (!boss || p->damage_on_hit == 0) return;
    int count = p->tentacle_count ? p->tentacle_count : 3;
    for (int i = 0; i < count && i < 8; i++) {
        rc_encounter_add_effect(world, RC_ENC_EFFECT_ROOM_ATTACK,
                                world->player.x + i - 1,
                                world->player.y + 1,
                                world->player.plane,
                                world->player.x, world->player.y,
                                p->warning_ticks ? p->warning_ticks : 2,
                                (uint16_t)boss->uid, COMBAT_MAGIC,
                                p->damage_on_hit, "Tentacle", "");
    }
    rc_queue_hit(world->player.pending_hits, &world->player.num_pending_hits,
                 p->damage_on_hit, p->warning_ticks ? p->warning_ticks : 2,
                 COMBAT_MAGIC, boss->uid, world->player.active_prayers,
                 world->tick);
    if (a->mechanic_progress) {
        a->mechanic_progress =
            a->mechanic_progress > p->sanity_drain_on_hit
            ? (uint16_t)(a->mechanic_progress - p->sanity_drain_on_hit) : 0;
    }
}

static void prim_aoe_prayer_swap_demand(RcWorld *world, int enc_idx,
                                        const void *params) {
    const RcPrimParamsPrayerDemand *p = params;
    RcActiveEncounter *a = &world->encounter.active[enc_idx];
    RcNpc *boss = find_boss(world, a->boss_id);
    if (!boss || p->wrong_prayer_damage == 0) return;
    bool ok = (world->player.active_prayers &
               (PRAYER_PROTECT_MAGIC | PRAYER_PROTECT_RANGE)) != 0;
    rc_encounter_add_effect(world, RC_ENC_EFFECT_ROOM_ATTACK,
                            boss->x, boss->y, boss->plane,
                            world->player.x, world->player.y,
                            p->warning_ticks ? p->warning_ticks : 3,
                            (uint16_t)boss->uid, COMBAT_MAGIC,
                            p->wrong_prayer_damage, "Prayer Demand", "");
    if (!ok) {
        rc_queue_hit(world->player.pending_hits,
                     &world->player.num_pending_hits,
                     p->wrong_prayer_damage,
                     p->warning_ticks ? p->warning_ticks : 3,
                     COMBAT_MAGIC, boss->uid, 0u, world->tick);
        if (a->mechanic_progress) {
            a->mechanic_progress =
                a->mechanic_progress > p->wrong_prayer_sanity_drain
                ? (uint16_t)(a->mechanic_progress -
                             p->wrong_prayer_sanity_drain) : 0;
        }
    }
}

static void prim_audio_visual_disruption(RcWorld *world, int enc_idx,
                                         const void *params) {
    const RcPrimParamsAudioVisualDisruption *p = params;
    RcActiveEncounter *a = &world->encounter.active[enc_idx];
    RcNpc *boss = find_boss(world, a->boss_id);
    if (!boss) return;
    rc_encounter_add_effect(world, RC_ENC_EFFECT_ROOM_ATTACK,
                            boss->x, boss->y, boss->plane,
                            world->player.x, world->player.y,
                            p->duration_ticks ? p->duration_ticks : 15,
                            (uint16_t)boss->uid, COMBAT_NONE, 0,
                            "Audio Visual Disruption", "");
}

static void prim_object_item_interaction(RcWorld *world, int enc_idx,
                                         const void *params) {
    (void)world;
    (void)enc_idx;
    (void)params;
}

static void prim_passive_heal_during_phase(RcWorld *world, int enc_idx,
                                           const void *params) {
    const RcPrimParamsPassiveHealDuringPhase *p = params;
    RcActiveEncounter *a = &world->encounter.active[enc_idx];
    RcNpc *boss = find_boss(world, a->boss_id);
    if (!boss || p->heal_per_tick == 0) return;
    int cap = g_npc_defs[boss->def_id].hitpoints;
    boss->current_hp += p->heal_per_tick;
    if (boss->current_hp > cap) boss->current_hp = cap;
}

static void prim_attack_counter_style_swap(RcWorld *world, int enc_idx,
                                           const void *params) {
    const RcPrimParamsAttackCounterStyleSwap *p = params;
    RcActiveEncounter *a = &world->encounter.active[enc_idx];
    const RcEncounterSpec *spec = &world->encounter.registry[a->spec_idx];
    if (p->style_count == 0 || spec->phase_count == 0) return;
    uint8_t next_style = p->styles[a->attack_special_toggle % p->style_count];
    a->attack_special_toggle++;
    const char *target = "";
    if (next_style == COMBAT_MAGIC) target = "hunllef_magic";
    else if (next_style == COMBAT_RANGED) target = "hunllef_ranged";
    if (target[0]) rc_encounter_set_phase(world, enc_idx, target);
}

static void prim_spawn_tracking_tornadoes(RcWorld *world, int enc_idx,
                                          const void *params) {
    const RcPrimParamsSpawnTrackingTornadoes *p = params;
    RcActiveEncounter *a = &world->encounter.active[enc_idx];
    RcNpc *boss = find_boss(world, a->boss_id);
    if (!boss || p->damage_on_contact == 0) return;
    int count = p->tornado_count ? p->tornado_count : 2;
    int duration = p->duration_ticks ? p->duration_ticks : 30;
    for (int i = 0; i < count && i < 8; i++) {
        rc_encounter_add_effect(world, RC_ENC_EFFECT_ROOM_ATTACK,
                                world->player.x + i - 1,
                                world->player.y + 2,
                                world->player.plane,
                                world->player.x, world->player.y,
                                duration, (uint16_t)boss->uid,
                                COMBAT_MAGIC, p->damage_on_contact,
                                "Tracking Tornado", "");
    }
    rc_queue_hit(world->player.pending_hits, &world->player.num_pending_hits,
                 p->damage_on_contact, 1, COMBAT_MAGIC, boss->uid,
                 world->player.active_prayers, world->tick);
}

static void prim_forced_prayer_switch_on_style_swap(RcWorld *world,
                                                    int enc_idx,
                                                    const void *params) {
    (void)params;
    RcActiveEncounter *a = &world->encounter.active[enc_idx];
    RcNpc *boss = find_boss(world, a->boss_id);
    if (!boss) return;
    rc_encounter_add_effect(world, RC_ENC_EFFECT_ROOM_ATTACK,
                            boss->x, boss->y, boss->plane,
                            world->player.x, world->player.y,
                            3, (uint16_t)boss->uid, COMBAT_NONE, 0,
                            "Prayer Switch", "");
}

static void prim_multi_limb_boss(RcWorld *world, int enc_idx,
                                 const void *params) {
    const RcPrimParamsMultiLimbBoss *p = params;
    RcActiveEncounter *a = &world->encounter.active[enc_idx];
    RcNpc *boss = find_boss(world, a->boss_id);
    if (!boss || p->limb_count == 0) return;
    for (int i = 0; i < p->limb_count && i < 3; i++) {
        if (!p->limb_names[i][0] || find_live_npc_named(world, p->limb_names[i])) {
            continue;
        }
        int def_idx = find_npc_def_idx_by_name(p->limb_names[i]);
        if (def_idx >= 0) {
            rc_npc_spawn(world, def_idx, boss->x + (i - 1) * 3,
                         boss->y + 2, boss->plane);
        }
    }
    rc_encounter_add_effect(world, RC_ENC_EFFECT_ROOM_ATTACK,
                            boss->x, boss->y, boss->plane,
                            world->player.x, world->player.y,
                            5, (uint16_t)boss->uid, COMBAT_NONE, 0,
                            "Multi-Limb Boss", "");
}

static void prim_player_position_swap(RcWorld *world, int enc_idx,
                                      const void *params) {
    const RcPrimParamsPlayerPositionSwap *p = params;
    RcActiveEncounter *a = &world->encounter.active[enc_idx];
    RcNpc *boss = find_boss(world, a->boss_id);
    if (!boss) return;
    int dist = p->min_distance ? p->min_distance : 6;
    world->player.prev_x = world->player.x;
    world->player.prev_y = world->player.y;
    world->player.x = boss->x + (world->player.x >= boss->x ? dist : -dist);
    world->player.y = boss->y + (world->player.y >= boss->y ? dist : -dist);
}

static void prim_environmental_wall_spawn(RcWorld *world, int enc_idx,
                                          const void *params) {
    const RcPrimParamsEnvironmentalWall *p = params;
    RcActiveEncounter *a = &world->encounter.active[enc_idx];
    RcNpc *boss = find_boss(world, a->boss_id);
    if (!boss || p->damage_on_cross == 0) return;
    int len = p->wall_length ? p->wall_length : 6;
    for (int i = 0; i < len && i < 12; i++) {
        int x = boss->x - len / 2 + i;
        int y = boss->y;
        rc_encounter_add_effect(world, RC_ENC_EFFECT_LAVA_POOL,
                                x, y, boss->plane,
                                x, y, 10, (uint16_t)boss->uid,
                                COMBAT_MAGIC, p->damage_on_cross,
                                "Environmental Wall", "");
    }
    if (world->player.y == boss->y) {
        rc_queue_hit(world->player.pending_hits, &world->player.num_pending_hits,
                     p->damage_on_cross, 0, COMBAT_MAGIC, boss->uid,
                     world->player.active_prayers, world->tick);
    }
}

static void prim_tile_telegraph_lightning(RcWorld *world, int enc_idx,
                                          const void *params) {
    const RcPrimParamsTileLightning *p = params;
    RcActiveEncounter *a = &world->encounter.active[enc_idx];
    RcNpc *boss = find_boss(world, a->boss_id);
    if (!boss || p->damage_on_tile == 0) return;
    int count = p->tiles_per_volley ? p->tiles_per_volley : 3;
    int warn = p->telegraph_ticks ? p->telegraph_ticks : 2;
    for (int i = 0; i < count && i < 12; i++) {
        int x = world->player.x + (i % 3) - 1;
        int y = world->player.y + (i / 3) - 1;
        rc_encounter_add_effect(world, RC_ENC_EFFECT_ROOM_ATTACK,
                                x, y, world->player.plane,
                                x, y, warn, (uint16_t)boss->uid,
                                COMBAT_MAGIC, p->damage_on_tile,
                                "Telegraphed Lightning", "");
    }
    rc_queue_hit(world->player.pending_hits, &world->player.num_pending_hits,
                 p->damage_on_tile, warn, COMBAT_MAGIC, boss->uid,
                 world->player.active_prayers, world->tick);
}

static void prim_continuous_heal_unless_interrupted(RcWorld *world,
                                                    int enc_idx,
                                                    const void *params) {
    const RcPrimParamsContinuousHeal *p = params;
    RcActiveEncounter *a = &world->encounter.active[enc_idx];
    RcNpc *boss = find_boss(world, a->boss_id);
    if (!boss || p->heal_per_tick == 0) return;
    int cap = g_npc_defs[boss->def_id].hitpoints;
    boss->current_hp += p->heal_per_tick;
    if (boss->current_hp > cap) boss->current_hp = cap;
}

static void prim_one_shot_weapon_provided(RcWorld *world, int enc_idx,
                                          const void *params) {
    const RcPrimParamsOneShotWeapon *p = params;
    RcActiveEncounter *a = &world->encounter.active[enc_idx];
    RcNpc *boss = find_boss(world, a->boss_id);
    if (!boss) return;
    a->mechanic_progress = p->effective_max_hit;
    rc_encounter_add_effect(world, RC_ENC_EFFECT_HIDDEN_OBJECT,
                            world->player.x, world->player.y,
                            world->player.plane,
                            world->player.x, world->player.y,
                            -1, (uint16_t)boss->uid, COMBAT_NONE,
                            p->effective_max_hit,
                            "Provided Weapon", p->item);
}

static void prim_destructible_pillars(RcWorld *world, int enc_idx,
                                      const void *params) {
    const RcPrimParamsDestructiblePillars *p = params;
    RcActiveEncounter *a = &world->encounter.active[enc_idx];
    RcNpc *boss = find_boss(world, a->boss_id);
    if (!boss) return;
    int count = p->pillar_count ? p->pillar_count : 4;
    for (int i = 0; i < count && i < 8; i++) {
        rc_encounter_add_effect(world, RC_ENC_EFFECT_HIDDEN_OBJECT,
                                boss->x + i - 2, boss->y - 4,
                                boss->plane,
                                boss->x + i - 2, boss->y - 4,
                                -1, (uint16_t)boss->uid, COMBAT_NONE,
                                p->damaged_per_electrify,
                                "Destructible Pillar", "");
    }
}

static void prim_spawn_web_tiles(RcWorld *world, int enc_idx,
                                 const void *params) {
    const RcPrimParamsSpawnWebTiles *p = params;
    RcActiveEncounter *a = &world->encounter.active[enc_idx];
    RcNpc *boss = find_boss(world, a->boss_id);
    if (!boss) return;
    int count = p->web_tile_count ? p->web_tile_count : 3;
    for (int i = 0; i < count && i < 8; i++) {
        rc_encounter_add_effect(world, RC_ENC_EFFECT_HIDDEN_OBJECT,
                                world->player.x + i - 1,
                                world->player.y, world->player.plane,
                                world->player.x, world->player.y,
                                8, (uint16_t)boss->uid, COMBAT_NONE, 0,
                                "Web Tile", "");
    }
    if (p->immobilizes_player_ticks > world->player.freeze_timer) {
        world->player.freeze_timer = p->immobilizes_player_ticks;
    }
}

static void prim_spawn_colored_nylocas(RcWorld *world, int enc_idx,
                                       const void *params) {
    const RcPrimParamsSpawnColoredNylocas *p = params;
    RcActiveEncounter *a = &world->encounter.active[enc_idx];
    RcNpc *boss = find_boss(world, a->boss_id);
    if (!boss) return;
    int count = p->color_count ? p->color_count : 3;
    for (int i = 0; i < count && i < 6; i++) {
        rc_encounter_add_effect(world, RC_ENC_EFFECT_ROOM_ATTACK,
                                boss->x + i - 1, boss->y + 3,
                                boss->plane,
                                world->player.x, world->player.y,
                                10, (uint16_t)boss->uid, COMBAT_NONE,
                                0, "Colored Nylocas", "");
    }
    if (p->heals_boss && p->heal_per_tick) {
        int cap = g_npc_defs[boss->def_id].hitpoints;
        boss->current_hp += p->heal_per_tick;
        if (boss->current_hp > cap) boss->current_hp = cap;
    }
}

static void prim_persistent_dot_tile_pool(RcWorld *world, int enc_idx,
                                          const void *params) {
    const RcPrimParamsPersistentDotTilePool *p = params;
    RcActiveEncounter *a = &world->encounter.active[enc_idx];
    RcNpc *boss = find_boss(world, a->boss_id);
    if (!boss || p->dot_per_tick == 0) return;
    int count = p->tile_count_per_spawn ? p->tile_count_per_spawn : 2;
    int duration = p->duration_ticks ? p->duration_ticks : -1;
    for (int i = 0; i < count && i < 8; i++) {
        rc_encounter_add_effect(world, RC_ENC_EFFECT_LAVA_POOL,
                                world->player.x + i - 1,
                                world->player.y + 1,
                                world->player.plane,
                                world->player.x, world->player.y,
                                duration, (uint16_t)boss->uid,
                                COMBAT_MAGIC, p->dot_per_tick,
                                "Persistent Tile Pool", "");
    }
}

static void prim_obelisk_dps_check(RcWorld *world, int enc_idx,
                                   const void *params) {
    const RcPrimParamsObeliskDpsCheck *p = params;
    RcActiveEncounter *a = &world->encounter.active[enc_idx];
    RcNpc *boss = find_boss(world, a->boss_id);
    if (!boss) return;
    a->shield_points = p->obelisk_hp;
    a->active_mechanic_ticks = p->time_limit_ticks;
    rc_encounter_add_effect(world, RC_ENC_EFFECT_ROOM_ATTACK,
                            boss->x, boss->y, boss->plane,
                            world->player.x, world->player.y,
                            p->time_limit_ticks,
                            (uint16_t)boss->uid, COMBAT_NONE, 0,
                            "Obelisk DPS Check", "");
}

static void prim_spawn_energized_pylons(RcWorld *world, int enc_idx,
                                        const void *params) {
    const RcPrimParamsSpawnPylons *p = params;
    RcActiveEncounter *a = &world->encounter.active[enc_idx];
    RcNpc *boss = find_boss(world, a->boss_id);
    if (!boss) return;
    int count = p->pylon_count ? p->pylon_count : 4;
    for (int i = 0; i < count && i < 8; i++) {
        rc_encounter_add_effect(world, RC_ENC_EFFECT_HIDDEN_OBJECT,
                                boss->x + i - 2, boss->y + 4,
                                boss->plane,
                                boss->x + i - 2, boss->y + 4,
                                -1, (uint16_t)boss->uid, COMBAT_NONE,
                                p->buff_damage_pct, "Energized Pylon", "");
    }
}

static void prim_periodic_death_tile_wave(RcWorld *world, int enc_idx,
                                          const void *params) {
    const RcPrimParamsDeathTileWave *p = params;
    RcActiveEncounter *a = &world->encounter.active[enc_idx];
    RcNpc *boss = find_boss(world, a->boss_id);
    if (!boss || p->damage_on_tile == 0) return;
    int count = p->wave_tile_count ? p->wave_tile_count : 8;
    for (int i = 0; i < count && i < 16; i++) {
        rc_encounter_add_effect(world, RC_ENC_EFFECT_ROOM_ATTACK,
                                world->player.x + (i % 5) - 2,
                                world->player.y + (i / 5) - 1,
                                world->player.plane,
                                world->player.x, world->player.y,
                                2, (uint16_t)boss->uid, COMBAT_NONE,
                                p->damage_on_tile > 255
                                    ? 255 : (uint8_t)p->damage_on_tile,
                                "Death Tile Wave", "");
    }
    rc_queue_hit(world->player.pending_hits, &world->player.num_pending_hits,
                 p->damage_on_tile, 2, COMBAT_NONE, boss->uid,
                 0u, world->tick);
}

static void prim_heal_altars_player_must_disable(RcWorld *world, int enc_idx,
                                                 const void *params) {
    const RcPrimParamsHealAltars *p = params;
    RcActiveEncounter *a = &world->encounter.active[enc_idx];
    RcNpc *boss = find_boss(world, a->boss_id);
    if (!boss || p->heal_per_altar_per_tick == 0) return;
    uint8_t altar_count = p->altar_count ? p->altar_count : 4;
    uint16_t disabled = a->mechanic_progress;
    if (disabled > altar_count) disabled = altar_count;
    int active_altars = altar_count - disabled;
    if (active_altars <= 0) return;
    int cap = g_npc_defs[boss->def_id].hitpoints;
    boss->current_hp += active_altars * p->heal_per_altar_per_tick;
    if (boss->current_hp > cap) boss->current_hp = cap;
}

static void prim_unimplemented_noop(RcWorld *world, int enc_idx,
                                   const void *params) {
    (void)world;
    (void)enc_idx;
    (void)params;
}

// ---- Registry ----------------------------------------------------------

// Indexed by primitive_id (see encounter.h enum). Implemented entries
// point to concrete behavior. IDs above the currently-supported core
// slice intentionally resolve to prim_unimplemented_noop so the tick loop
// still advances mechanics while keeping those encounters runnable.
static const RcEncounterPrimFn PRIM_TABLE[RC_PRIM_MAX] = {
    [RC_PRIM_TELEGRAPHED_AOE_TILE]   = prim_telegraphed_aoe_tile,
    [RC_PRIM_SPAWN_NPCS]             = prim_spawn_npcs,
    [RC_PRIM_SPAWN_NPCS_ONCE]        = prim_spawn_npcs_once,
    [RC_PRIM_HEAL_AT_OBJECT]         = prim_heal_at_object,
    [RC_PRIM_PERIODIC_HEAL_BOSS]     = prim_periodic_heal_boss,
    [RC_PRIM_DRAIN_PRAYER_ON_HIT]    = prim_drain_prayer_on_hit,
    [RC_PRIM_CHAIN_MAGIC_TO_NEAREST] = prim_chain_magic_to_nearest,
    [RC_PRIM_PRESERVE_STAT_DRAINS]   = prim_preserve_stat_drains,
    [RC_PRIM_TELEPORT_ON_INCOMING_ATTACK] = prim_teleport_on_incoming_attack,
    [RC_PRIM_TELEPORT_PLAYER_NEARBY] = prim_teleport_player_nearby,
    [RC_PRIM_UNEQUIP_PLAYER_ITEMS]   = prim_unequip_player_items,
    [RC_PRIM_POSITIONAL_AOE]         = prim_positional_aoe,
    [RC_PRIM_SPAWN_LEECH_NPC]        = prim_spawn_leech_npc,
    [RC_PRIM_REGEN_WHEN_NO_PLAYER]   = prim_regen_when_no_player,
    [RC_PRIM_ATTACK_COUNTER_SPECIAL] = prim_attack_counter_special,
    [RC_PRIM_SPAWN_SOUL_ATTACKERS]   = prim_spawn_soul_attackers,
    [RC_PRIM_DOT_TILE_PLACEMENT]     = prim_dot_tile_placement,
    [RC_PRIM_STATIC_DOT_LINE]        = prim_static_dot_line,
    [RC_PRIM_SPAWN_HIDDEN_MINIONS]   = prim_spawn_hidden_minions,
    [RC_PRIM_PHASE_ADVANCE_ON_CONDITION] = prim_phase_advance_on_condition,
    [RC_PRIM_FORM_TRANSITION_DIVE]   = prim_form_transition_dive,
    [RC_PRIM_ATTACK_COUNTER_ALTERNATE_SPECIAL] =
        prim_attack_counter_alternate_special,
    [RC_PRIM_COVERED_ARENA_ENVIRONMENT] = prim_covered_arena_environment,
    [RC_PRIM_DAMAGE_REDUCTION_UNTIL_TRIGGER] =
        prim_damage_reduction_until_trigger,
    [RC_PRIM_ANIMATION_WARNING_STYLE_SWAP] = prim_animation_warning_style_swap,
    [RC_PRIM_CONVERGING_AOE] = prim_converging_aoe,
    [RC_PRIM_STUN_THEN_FIRE_WALLS] = prim_stun_then_fire_walls,
    [RC_PRIM_MOVING_DOT_LINE] = prim_moving_dot_line,
    [RC_PRIM_OBJECT_INTERACTION_TICKED] = prim_object_interaction_ticked,
    [RC_PRIM_TOTEM_CHARGE_PROGRESSION] = prim_totem_charge_progression,
    [RC_PRIM_TELEGRAPHED_PORTAL_AOE] = prim_telegraphed_portal_aoe,
    [RC_PRIM_SPAWN_PAIRED_HUSKS] = prim_spawn_paired_husks,
    [RC_PRIM_QUADRANT_SAFE_ZONE_DOT] = prim_quadrant_safe_zone_dot,
    [RC_PRIM_SHUFFLE_PLAYER_PRAYERS] = prim_shuffle_player_prayers,
    [RC_PRIM_INFECTIOUS_DOT_WITH_CURE] = prim_infectious_dot_with_cure,
    [RC_PRIM_SPAWN_CONVERGENT_MINIONS] = prim_spawn_convergent_minions,
    [RC_PRIM_AOE_TILE_DEBUFF] = prim_aoe_tile_debuff,
    [RC_PRIM_LINE_DASH] = prim_line_dash,
    [RC_PRIM_SPAWN_OBJECTIVE_NPCS] = prim_spawn_objective_npcs,
    [RC_PRIM_NPC_PATHED_MOVEMENT] = prim_npc_pathed_movement,
    [RC_PRIM_SPAWN_WALL_TENTACLES] = prim_spawn_wall_tentacles,
    [RC_PRIM_TELEPORTING_TILE_AOE] = prim_teleporting_tile_aoe,
    [RC_PRIM_HOMING_PROJECTILES_WITH_WALLS] =
        prim_homing_projectiles_with_walls,
    [RC_PRIM_SMITE_DRAIN_SHIELD] = prim_smite_drain_shield,
    [RC_PRIM_PERIODIC_SPIKE_CLUSTER] = prim_periodic_spike_cluster,
    [RC_PRIM_MOVING_ROTATIONAL_HAZARDS] = prim_moving_rotational_hazards,
    [RC_PRIM_HEAL_BOSS_ON_PLAYER_ATTACK_MISS] =
        prim_heal_boss_on_player_attack_miss,
    [RC_PRIM_TILE_DEBUFF_ON_STAND] = prim_tile_debuff_on_stand,
    [RC_PRIM_SPAWN_BUFF_ZONE_NPC] = prim_spawn_buff_zone_npc,
    [RC_PRIM_ONE_SHOT_ARENA_EFFECT] = prim_one_shot_arena_effect,
    [RC_PRIM_PLAYER_SANITY_TRACKER] = prim_player_sanity_tracker,
    [RC_PRIM_SPAWN_TENTACLE_PROJECTILES] = prim_spawn_tentacle_projectiles,
    [RC_PRIM_AOE_PRAYER_SWAP_DEMAND] = prim_aoe_prayer_swap_demand,
    [RC_PRIM_AUDIO_VISUAL_DISRUPTION] = prim_audio_visual_disruption,
    [RC_PRIM_OBJECT_ITEM_INTERACTION] = prim_object_item_interaction,
    [RC_PRIM_PASSIVE_HEAL_DURING_PHASE] = prim_passive_heal_during_phase,
    [RC_PRIM_ATTACK_COUNTER_STYLE_SWAP] = prim_attack_counter_style_swap,
    [RC_PRIM_SPAWN_TRACKING_TORNADOES] = prim_spawn_tracking_tornadoes,
    [RC_PRIM_FORCED_PRAYER_SWITCH_ON_STYLE_SWAP] =
        prim_forced_prayer_switch_on_style_swap,
    [RC_PRIM_MULTI_LIMB_BOSS] = prim_multi_limb_boss,
    [RC_PRIM_PLAYER_POSITION_SWAP] = prim_player_position_swap,
    [RC_PRIM_ENVIRONMENTAL_WALL_SPAWN] = prim_environmental_wall_spawn,
    [RC_PRIM_TILE_TELEGRAPH_LIGHTNING] = prim_tile_telegraph_lightning,
    [RC_PRIM_CONTINUOUS_HEAL_UNLESS_INTERRUPTED] =
        prim_continuous_heal_unless_interrupted,
    [RC_PRIM_ONE_SHOT_WEAPON_PROVIDED] = prim_one_shot_weapon_provided,
    [RC_PRIM_DESTRUCTIBLE_PILLARS] = prim_destructible_pillars,
    [RC_PRIM_SPAWN_WEB_TILES] = prim_spawn_web_tiles,
    [RC_PRIM_SPAWN_COLORED_NYLOCAS] = prim_spawn_colored_nylocas,
    [RC_PRIM_PERSISTENT_DOT_TILE_POOL] = prim_persistent_dot_tile_pool,
    [RC_PRIM_OBELISK_DPS_CHECK] = prim_obelisk_dps_check,
    [RC_PRIM_SPAWN_ENERGIZED_PYLONS] = prim_spawn_energized_pylons,
    [RC_PRIM_PERIODIC_DEATH_TILE_WAVE] = prim_periodic_death_tile_wave,
    [RC_PRIM_HEAL_ALTARS_PLAYER_MUST_DISABLE] =
        prim_heal_altars_player_must_disable,
};

RcEncounterPrimFn rc_encounter_prim_lookup(uint8_t primitive_id) {
    if (primitive_id >= RC_PRIM_MAX) return NULL;
    if (primitive_id == 0) return NULL;
    RcEncounterPrimFn fn = PRIM_TABLE[primitive_id];
    return fn ? fn : prim_unimplemented_noop;
}
