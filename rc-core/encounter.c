#include "encounter.h"
#include "assets.h"
#include "types.h"
#include "events.h"
#include "config.h"
#include "combat.h"
#include "items.h"
#include "npc.h"     // g_npc_defs
#include "prayer.h"
#include "rng.h"
#include <stdio.h>
#include <stddef.h>
#include <string.h>

// RcPayloadNpcEvent lives in events.h — we just consume it here.

#define ENCT_MAGIC 0x54434E45u     // 'ENCT'

static int find_active_slot(const RcEncounterState *s) {
    for (int i = 0; i < RC_ENC_MAX_ACTIVE; i++) {
        if (!s->active[i].active) return i;
    }
    return -1;
}

static int find_active_by_npc(const RcEncounterState *s, RcNpcId id) {
    for (int i = 0; i < RC_ENC_MAX_ACTIVE; i++) {
        if (s->active[i].active && s->active[i].boss_id == id) return i;
    }
    return -1;
}

static bool phase_matches(const RcEncounterMechanic *m, uint8_t phase) {
    if (m->phase_mask != 0u && phase < 32) {
        return (m->phase_mask & (1u << phase)) != 0u;
    }
    return m->phase_idx == phase;
}

static void run_phase_script(RcWorld *world, int active_idx, uint8_t phase) {
    RcEncounterState *s = &world->encounter;
    if (active_idx < 0 || active_idx >= RC_ENC_MAX_ACTIVE) return;
    RcActiveEncounter *a = &s->active[active_idx];
    if (!a->active || a->spec_idx >= s->registry_count) return;
    const RcEncounterSpec *spec = &s->registry[a->spec_idx];
    if (phase >= spec->phase_count) return;
    const char *name = spec->phases[phase].script;
    if (!name[0]) return;
    RcEncounterScriptFn fn = rc_encounter_script_lookup(world, name);
    if (!fn) {
        s->script_misses++;
        return;
    }
    fn(world, active_idx);
    s->scripts_called++;
}

static void invoke_mechanic(RcWorld *world, int active_idx,
                            const RcEncounterMechanic *mech) {
    RcActiveEncounter *a = NULL;
    if (active_idx >= 0 && active_idx < RC_ENC_MAX_ACTIVE) {
        a = &world->encounter.active[active_idx];
        if (a->active && a->spec_idx < world->encounter.registry_count) {
            const RcEncounterSpec *spec =
                &world->encounter.registry[a->spec_idx];
            ptrdiff_t idx = mech - spec->mechanics;
            a->invoking_mechanic_idx =
                (idx >= 0 && idx < spec->mechanic_count) ? (uint8_t)idx
                                                         : 0xFFu;
        }
    }
    if (mech->prim) mech->prim(world, active_idx, mech->param_block);
    if (a) a->invoking_mechanic_idx = 0xFFu;
    world->encounter.triggered_mechanics++;
}

static void tick_mechanic(RcWorld *world, int active_idx,
                          RcEncounterMechanic *mech) {
    if (mech->period_ticks == 0) return;
    if (mech->ticks_until_next == 0) {
        invoke_mechanic(world, active_idx, mech);
        mech->ticks_until_next = mech->period_ticks;
    } else {
        mech->ticks_until_next--;
    }
}

static bool trigger_ref_matches(const char *have, const char *want) {
    return have && want && want[0] && strcmp(have, want) == 0;
}

static int phase_index_by_id(const RcEncounterSpec *spec, const char *id) {
    if (!spec || !id || !id[0]) return -1;
    for (int i = 0; i < spec->phase_count; i++) {
        if (strcmp(spec->phases[i].id, id) == 0) return i;
    }
    return -1;
}

static int set_active_phase_idx(RcWorld *world, int active_idx, uint8_t next) {
    if (!world || active_idx < 0 || active_idx >= RC_ENC_MAX_ACTIVE) return 0;
    RcActiveEncounter *a = &world->encounter.active[active_idx];
    if (!a->active || a->spec_idx >= world->encounter.registry_count) return 0;
    const RcEncounterSpec *spec = &world->encounter.registry[a->spec_idx];
    if (next >= spec->phase_count || next == a->current_phase) return 0;
    uint8_t old = a->current_phase;
    a->current_phase = next;
    a->mechanic_progress = 0;
    RcPayloadPhaseTransition payload = {
        .npc_id = a->boss_id,
        .old_phase = old,
        .new_phase = next,
    };
    rc_event_fire(world, RC_EVT_PHASE_TRANSITION, &payload);
    return 1;
}

static const char *attack_name_for_active(const RcEncounterSpec *spec,
                                          const RcActiveEncounter *active) {
    if (!spec || !active) return "";
    if (active->last_attack_idx < spec->attack_count) {
        return spec->attacks[active->last_attack_idx].name;
    }
    uint16_t attack_count = active->attack_count;
    if (!spec || spec->attack_count == 0 || attack_count == 0) return "";
    uint16_t idx = (uint16_t)((attack_count - 1) % spec->attack_count);
    return spec->attacks[idx].name;
}

static bool attack_style_is_melee(uint8_t style) {
    return style == COMBAT_MELEE_STAB ||
           style == COMBAT_MELEE_SLASH ||
           style == COMBAT_MELEE_CRUSH;
}

static bool attack_matches_active(const RcEncounterAttack *attack,
                                  const RcActiveEncounter *active,
                                  int distance) {
    if (attack->npc_id && attack->npc_id != active->def_id) return false;
    if ((attack->flags & RC_ENC_ATTACK_REQ_MELEE_RANGE) && distance > 1) {
        return false;
    }
    return true;
}

static int attack_weight(const RcEncounterPhase *phase,
                         const RcEncounterAttack *attack,
                         int distance) {
    const uint8_t *weights = distance <= 1
                             ? phase->adjacent_style_weights
                             : phase->distant_style_weights;
    if (attack->style < 8 && weights[attack->style]) {
        return weights[attack->style];
    }
    return 0;
}

static uint8_t preferred_style_from_phase_id(const RcEncounterPhase *phase) {
    if (!phase) return COMBAT_NONE;
    if (strstr(phase->id, "magic")) return COMBAT_MAGIC;
    if (strstr(phase->id, "ranged")) return COMBAT_RANGED;
    if (strstr(phase->id, "melee")) return COMBAT_MELEE_CRUSH;
    return COMBAT_NONE;
}

static uint32_t phase_attack_mask(const RcEncounterPhase *phase,
                                  int attack_count) {
    uint32_t mask = phase->allowed_attack_mask;
    if (phase->allowed_attack_mask_explicit) return mask;
    if (mask) return mask;
    return attack_count >= 32 ? 0xFFFFFFFFu : ((1u << attack_count) - 1u);
}

static bool respawn_applies(const RcPrimParamsPeriodicRespawnIfDead *p,
                            uint32_t npc_id) {
    if (!p) return false;
    for (int i = 0; i < p->count && i < 8; i++) {
        if (p->npc_ids[i] == npc_id) return true;
    }
    return false;
}

static bool live_npc_with_id(const RcWorld *world, uint32_t npc_id) {
    for (int i = 0; i < world->npc_count; i++) {
        const RcNpc *npc = &world->npcs[i];
        if (!npc->active || npc->is_dead) continue;
        if (npc->def_id < 0 || npc->def_id >= g_npc_def_count) continue;
        if ((uint32_t)g_npc_defs[npc->def_id].id == npc_id) return true;
    }
    return false;
}

static bool live_npc_named(const RcWorld *world, const char *name) {
    if (!name || !name[0]) return false;
    for (int i = 0; i < world->npc_count; i++) {
        const RcNpc *npc = &world->npcs[i];
        if (!npc->active || npc->is_dead) continue;
        if (npc->def_id < 0 || npc->def_id >= g_npc_def_count) continue;
        if (strcmp(g_npc_defs[npc->def_id].name, name) == 0) return true;
    }
    return false;
}

static bool spawned_and_all_dead_named(const RcWorld *world,
                                       const char *name) {
    if (!name || !name[0]) return false;
    bool seen = false;
    for (int i = 0; i < world->npc_count; i++) {
        const RcNpc *npc = &world->npcs[i];
        if (!npc->active) continue;
        if (npc->def_id < 0 || npc->def_id >= g_npc_def_count) continue;
        if (strcmp(g_npc_defs[npc->def_id].name, name) != 0) continue;
        seen = true;
        if (!npc->is_dead) return false;
    }
    return seen;
}

static bool active_spawn_blocker_alive(const RcWorld *world,
                                       const RcActiveEncounter *a,
                                       const RcEncounterSpec *spec) {
    if (!world || !a || !spec || a->active_mechanic_idx >= spec->mechanic_count) {
        return false;
    }
    const RcEncounterMechanic *m = &spec->mechanics[a->active_mechanic_idx];
    if (m->primitive_id != RC_PRIM_SPAWN_NPCS_ONCE &&
            m->primitive_id != RC_PRIM_SPAWN_NPCS) {
        return false;
    }
    const RcPrimParamsSpawnNpcs *p =
        (const RcPrimParamsSpawnNpcs *)m->param_block;
    return p->blocks_boss_damage_until_dead && live_npc_named(world, p->name);
}

static int active_environment_damage_pct(const RcActiveEncounter *a,
                                         const RcEncounterSpec *spec) {
    if (!a || !spec || a->active_mechanic_idx >= spec->mechanic_count) {
        return 100;
    }
    const RcEncounterMechanic *m = &spec->mechanics[a->active_mechanic_idx];
    if (m->primitive_id == RC_PRIM_COVERED_ARENA_ENVIRONMENT) {
        const RcPrimParamsCoveredArenaEnvironment *p =
            (const RcPrimParamsCoveredArenaEnvironment *)m->param_block;
        return p->damage_reduction_pct ? p->damage_reduction_pct : 100;
    }
    if (m->primitive_id == RC_PRIM_DAMAGE_REDUCTION_UNTIL_TRIGGER) {
        const RcPrimParamsDamageGate *p =
            (const RcPrimParamsDamageGate *)m->param_block;
        int reduction = p->damage_reduction_pct;
        if (reduction > 100) reduction = 100;
        return 100 - reduction;
    }
    if (m->primitive_id == RC_PRIM_SPAWN_BUFF_ZONE_NPC) {
        const RcPrimParamsSpawnBuffZoneNpc *p =
            (const RcPrimParamsSpawnBuffZoneNpc *)m->param_block;
        return p->outside_damage_pct ? p->outside_damage_pct : 100;
    }
    return 100;
}

static const RcPrimParamsGroupKillRequired *group_kill_params(
    const RcEncounterSpec *spec) {
    for (int i = 0; i < spec->mechanic_count; i++) {
        const RcEncounterMechanic *m = &spec->mechanics[i];
        if (m->primitive_id == RC_PRIM_GROUP_KILL_REQUIRED) {
            return (const RcPrimParamsGroupKillRequired *)m->param_block;
        }
    }
    return NULL;
}

static const RcPrimParamsPeriodicRespawnIfDead *respawn_params(
    const RcEncounterSpec *spec) {
    for (int i = 0; i < spec->mechanic_count; i++) {
        const RcEncounterMechanic *m = &spec->mechanics[i];
        if (m->primitive_id == RC_PRIM_PERIODIC_RESPAWN_IF_DEAD) {
            return (const RcPrimParamsPeriodicRespawnIfDead *)m->param_block;
        }
    }
    return NULL;
}

static bool group_is_dead(const RcWorld *world,
                          const RcPrimParamsGroupKillRequired *p) {
    if (!p || p->count == 0) return false;
    for (int i = 0; i < p->count && i < 8; i++) {
        if (live_npc_with_id(world, p->npc_ids[i])) return false;
    }
    return true;
}

static RcNpc *find_npc_by_uid(RcWorld *world, RcNpcId uid) {
    for (int i = 0; i < world->npc_count; i++) {
        if (world->npcs[i].active && world->npcs[i].uid == uid) {
            return &world->npcs[i];
        }
    }
    return NULL;
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

static char lower_ascii(char c) {
    return (c >= 'A' && c <= 'Z') ? (char)(c + ('a' - 'A')) : c;
}

static bool contains_ci(const char *s, const char *needle) {
    if (!s || !needle || !needle[0]) return false;
    for (; *s; s++) {
        const char *a = s;
        const char *b = needle;
        while (*a && *b && lower_ascii(*a) == lower_ascii(*b)) {
            a++;
            b++;
        }
        if (!*b) return true;
    }
    return false;
}

static bool style_matches(uint8_t have, uint8_t want) {
    if (want == COMBAT_MELEE_CRUSH) {
        return have == COMBAT_MELEE_STAB ||
               have == COMBAT_MELEE_SLASH ||
               have == COMBAT_MELEE_CRUSH;
    }
    return have == want;
}

static const RcItemDef *equipped_weapon(const RcWorld *world) {
    if (!world) return NULL;
    int id = world->player.equipment[EQUIP_WEAPON].item_id;
    return rc_item_def_get(id);
}

static bool player_has_equipped_item(const RcPlayer *p, int item_id) {
    if (!p || item_id < 0) return false;
    for (int i = 0; i < RC_EQUIP_COUNT; i++) {
        if (p->equipment[i].item_id == item_id) return true;
    }
    return false;
}

static bool player_equipment_contains(const RcPlayer *p, const char *needle) {
    if (!p || !needle || !needle[0]) return false;
    for (int i = 0; i < RC_EQUIP_COUNT; i++) {
        int item_id = p->equipment[i].item_id;
        if (item_id < 0) continue;
        const RcItemDef *def = rc_item_def_get(item_id);
        if (def && contains_ci(def->name, needle)) return true;
    }
    return false;
}

static bool player_has_facemask_protection(const RcPlayer *p) {
    return player_equipment_contains(p, "face mask") ||
           player_equipment_contains(p, "facemask") ||
           player_equipment_contains(p, "slayer helmet");
}

static bool weapon_is_halberd_or_salamander(const RcItemDef *def) {
    if (!def) return false;
    return contains_ci(def->name, "halberd") ||
           contains_ci(def->name, "salamander") ||
           def->weapon_type == 26;
}

static bool weapon_is_halberd(const RcItemDef *def) {
    return def && contains_ci(def->name, "halberd");
}

static bool weapon_is_corpbane_stab(const RcItemDef *def, uint8_t style) {
    if (!def || style != COMBAT_MELEE_STAB) return false;
    return def->weapon_type == 19 ||          // spear
           contains_ci(def->name, "spear") ||
           contains_ci(def->name, "hasta") ||
           contains_ci(def->name, "halberd");
}

static bool damage_modifier_matches(const RcWorld *world,
                                    const RcEncounterDamageModifier *m,
                                    uint32_t npc_id, uint8_t style) {
    if (m->npc_id && m->npc_id != npc_id) return false;
    const RcItemDef *weapon = equipped_weapon(world);
    switch (m->condition) {
        case RC_ENC_DMG_STYLE_NOT:
            return !style_matches(style, m->style);
        case RC_ENC_DMG_STYLE_IS:
            return style_matches(style, m->style);
        case RC_ENC_DMG_NOT_CORPBANE_STAB:
            return !weapon_is_corpbane_stab(weapon, style);
        case RC_ENC_DMG_MELEE_NOT_HALBERD_SALAMANDER:
            return style_matches(style, COMBAT_MELEE_CRUSH) &&
                   !weapon_is_halberd_or_salamander(weapon);
        case RC_ENC_DMG_MELEE_NOT_HALBERD:
            return style_matches(style, COMBAT_MELEE_CRUSH) &&
                   !weapon_is_halberd(weapon);
        case RC_ENC_DMG_PLAYER_NOT_FACEMASK:
            return !player_has_facemask_protection(&world->player);
        case RC_ENC_DMG_CAP_RANGE:
            return true;
        default:
            return false;
    }
}

static void drain_player_skill(RcPlayer *p, int skill, int amount) {
    if (!p || skill < 0 || skill >= SKILL_COUNT || amount <= 0) return;
    p->skills.boosted_level[skill] -= amount;
    if (p->skills.boosted_level[skill] < 0) {
        p->skills.boosted_level[skill] = 0;
    }
}

static void heal_npc_to_def_cap(RcNpc *npc, int amount) {
    if (!npc || amount <= 0 || npc->def_id < 0 ||
            npc->def_id >= g_npc_def_count) {
        return;
    }
    int cap = g_npc_defs[npc->def_id].hitpoints;
    if (cap <= 0) return;
    npc->current_hp += amount;
    if (npc->current_hp > cap) npc->current_hp = cap;
}

static void knock_player_away_from_npc(RcWorld *world, const RcNpc *npc,
                                       int tiles) {
    if (!world || !npc || tiles <= 0) return;
    RcPlayer *p = &world->player;
    int dx = p->x - npc->x;
    int dy = p->y - npc->y;
    if (dx == 0 && dy == 0) {
        dx = 1;
    } else {
        if (dx > 0) dx = 1;
        else if (dx < 0) dx = -1;
        if (dy > 0) dy = 1;
        else if (dy < 0) dy = -1;
    }
    p->prev_x = p->x;
    p->prev_y = p->y;
    p->x += dx * tiles;
    p->y += dy * tiles;
}

static void copy_effect_name(char *dst, int cap, const char *src) {
    if (!dst || cap <= 0) return;
    if (!src) src = "";
    strncpy(dst, src, (size_t)cap - 1);
    dst[cap - 1] = '\0';
}

static void apply_attack_effect(RcWorld *world, RcActiveEncounter *a,
                                const RcEncounterSpec *spec,
                                uint16_t damage) {
    if (!world || !a || !spec) return;
    if (a->last_attack_idx >= spec->attack_count) return;
    const RcEncounterAttack *attack = &spec->attacks[a->last_attack_idx];
    if (damage > 0 && (attack->flags & RC_ENC_ATTACK_HITS_ALL_ROOM)) {
        RcNpc *boss = find_npc_by_uid(world, a->boss_id);
        if (boss) {
            rc_encounter_add_effect(world, RC_ENC_EFFECT_ROOM_ATTACK,
                                    boss->x, boss->y, boss->plane,
                                    world->player.x, world->player.y, 2,
                                    (uint16_t)boss->uid, attack->style,
                                    (uint8_t)(damage > 255 ? 255 : damage),
                                    attack->name, "");
        }
    }
    if (attack->effect_id == RC_ENC_ATTACK_EFFECT_DRAIN_HEAL) {
        if (damage == 0) return;
        int min = attack->effect_min;
        int max = attack->effect_max >= attack->effect_min
                  ? attack->effect_max : attack->effect_min;
        int drain = min;
        if (max > min) drain += rc_rng_range(&world->rng_state, max - min);
        uint8_t flags = attack->effect_flags;
        if ((flags & (RC_ENC_ATTACK_EFFECT_DRAIN_MAGIC |
                      RC_ENC_ATTACK_EFFECT_DRAIN_PRAYER)) ==
                (RC_ENC_ATTACK_EFFECT_DRAIN_MAGIC |
                 RC_ENC_ATTACK_EFFECT_DRAIN_PRAYER)) {
            flags = rc_rng_range(&world->rng_state, 1) == 0
                    ? RC_ENC_ATTACK_EFFECT_DRAIN_MAGIC
                    : RC_ENC_ATTACK_EFFECT_DRAIN_PRAYER;
        }
        if ((flags & RC_ENC_ATTACK_EFFECT_DRAIN_MAGIC) != 0) {
            drain_player_skill(&world->player, SKILL_MAGIC, drain);
        }
        if ((flags & RC_ENC_ATTACK_EFFECT_DRAIN_PRAYER) != 0) {
            world->player.current_prayer_points -= drain;
            if (world->player.current_prayer_points < 0) {
                world->player.current_prayer_points = 0;
            }
        }
        if (attack->effect_pct) {
            RcNpc *boss = find_npc_by_uid(world, a->boss_id);
            heal_npc_to_def_cap(boss, ((int)damage * attack->effect_pct) / 100);
        }
    } else if (attack->effect_id == RC_ENC_ATTACK_EFFECT_SPLIT_PROJECTILES) {
        if (damage == 0) return;
        RcNpc *boss = find_npc_by_uid(world, a->boss_id);
        if (!boss) return;
        for (int i = 0; i < attack->effect_min; i++) {
            int dmg = attack->effect_max > 0
                      ? rc_rng_range(&world->rng_state, attack->effect_max)
                      : 0;
            rc_queue_hit_flags(world->player.pending_hits,
                               &world->player.num_pending_hits,
                               dmg, 1, attack->style, boss->uid,
                               world->player.active_prayers, world->tick,
                               RC_HIT_SUPPRESS_ENCOUNTER_EFFECTS);
        }
    } else if (attack->effect_id == RC_ENC_ATTACK_EFFECT_DRAIN_PRAYER_PCT) {
        if (damage == 0 || attack->effect_pct == 0) return;
        int drain = (world->player.current_prayer_points *
                     attack->effect_pct) / 100;
        if (drain <= 0 && world->player.current_prayer_points > 0) drain = 1;
        world->player.current_prayer_points -= drain;
        if (world->player.current_prayer_points < 0) {
            world->player.current_prayer_points = 0;
        }
    } else if (attack->effect_id == RC_ENC_ATTACK_EFFECT_POISON) {
        if (damage == 0 &&
                (attack->effect_flags &
                 RC_ENC_ATTACK_EFFECT_POISON_PIERCES_PRAY) == 0) {
            return;
        }
        if (attack->effect_pct &&
                rc_rng_range(&world->rng_state, 99) >= attack->effect_pct) {
            return;
        }
        if (world->player.poison_damage < attack->effect_min) {
            world->player.poison_damage = attack->effect_min;
            world->player.poison_tick_counter = 30;
        }
    } else if (attack->effect_id == RC_ENC_ATTACK_EFFECT_KNOCKBACK) {
        if (damage == 0) return;
        RcNpc *boss = find_npc_by_uid(world, a->boss_id);
        knock_player_away_from_npc(world, boss, attack->effect_min);
    } else if (attack->effect_id == RC_ENC_ATTACK_EFFECT_VENOM) {
        int venom = attack->effect_min ? attack->effect_min : 6;
        if (world->player.venom_damage < venom) {
            world->player.venom_damage = venom;
            world->player.venom_tick_counter = 30;
        }
    } else if (attack->effect_id == RC_ENC_ATTACK_EFFECT_DEACTIVATE_PRAYERS) {
        world->player.active_prayers = 0;
    }
}

static uint16_t attack_counter_every(const RcEncounterMechanic *m) {
    if (m->primitive_id == RC_PRIM_ATTACK_COUNTER_SPECIAL) {
        const RcPrimParamsAttackCounterSpecial *p =
            (const RcPrimParamsAttackCounterSpecial *)m->param_block;
        return p->every_n_attacks;
    }
    if (m->primitive_id == RC_PRIM_SPAWN_SOUL_ATTACKERS) {
        const RcPrimParamsSpawnSoulAttackers *p =
            (const RcPrimParamsSpawnSoulAttackers *)m->param_block;
        return p->every_n_attacks;
    }
    if (m->primitive_id == RC_PRIM_DOT_TILE_PLACEMENT) {
        const RcPrimParamsDotTilePlacement *p =
            (const RcPrimParamsDotTilePlacement *)m->param_block;
        return p->every_n_attacks;
    }
    if (m->primitive_id == RC_PRIM_ATTACK_COUNTER_ALTERNATE_SPECIAL) {
        const RcPrimParamsAttackCounterAlternateSpecial *p =
            (const RcPrimParamsAttackCounterAlternateSpecial *)m->param_block;
        return p->every_n_attacks;
    }
    if (m->primitive_id == RC_PRIM_ANIMATION_WARNING_STYLE_SWAP) {
        const RcPrimParamsStyleSwap *p =
            (const RcPrimParamsStyleSwap *)m->param_block;
        return p->every_n_attacks;
    }
    if (m->primitive_id == RC_PRIM_PERIODIC_SPIKE_CLUSTER) {
        const RcPrimParamsPeriodicSpikeCluster *p =
            (const RcPrimParamsPeriodicSpikeCluster *)m->param_block;
        return p->every_n_attacks;
    }
    if (m->primitive_id == RC_PRIM_ATTACK_COUNTER_STYLE_SWAP) {
        const RcPrimParamsAttackCounterStyleSwap *p =
            (const RcPrimParamsAttackCounterStyleSwap *)m->param_block;
        return p->every_n_attacks;
    }
    return 7;
}

static uint8_t attack_counter_priority(const RcEncounterMechanic *m) {
    if (m->primitive_id == RC_PRIM_ATTACK_COUNTER_SPECIAL) {
        const RcPrimParamsAttackCounterSpecial *p =
            (const RcPrimParamsAttackCounterSpecial *)m->param_block;
        return p->priority ? p->priority : 255;
    }
    if (m->primitive_id == RC_PRIM_SPAWN_SOUL_ATTACKERS) {
        const RcPrimParamsSpawnSoulAttackers *p =
            (const RcPrimParamsSpawnSoulAttackers *)m->param_block;
        return p->priority ? p->priority : 255;
    }
    if (m->primitive_id == RC_PRIM_DOT_TILE_PLACEMENT) {
        const RcPrimParamsDotTilePlacement *p =
            (const RcPrimParamsDotTilePlacement *)m->param_block;
        return p->priority ? p->priority : 255;
    }
    if (m->primitive_id == RC_PRIM_ATTACK_COUNTER_ALTERNATE_SPECIAL) return 1;
    if (m->primitive_id == RC_PRIM_ATTACK_COUNTER_STYLE_SWAP) return 1;
    if (m->primitive_id == RC_PRIM_PERIODIC_SPIKE_CLUSTER) return 2;
    if (m->primitive_id == RC_PRIM_ANIMATION_WARNING_STYLE_SWAP) return 250;
    return 255;
}

static bool attack_counter_hp_matches(RcWorld *world,
                                      const RcActiveEncounter *a,
                                      const RcEncounterMechanic *m) {
    RcNpc *boss = find_npc_by_uid(world, a->boss_id);
    if (!boss) return false;
    if (m->primitive_id == RC_PRIM_ATTACK_COUNTER_SPECIAL) {
        const RcPrimParamsAttackCounterSpecial *p =
            (const RcPrimParamsAttackCounterSpecial *)m->param_block;
        return p->max_hp == 0 || boss->current_hp <= p->max_hp;
    }
    if (m->primitive_id == RC_PRIM_SPAWN_SOUL_ATTACKERS) {
        const RcPrimParamsSpawnSoulAttackers *p =
            (const RcPrimParamsSpawnSoulAttackers *)m->param_block;
        if (p->min_hp && boss->current_hp < p->min_hp) return false;
        return p->max_hp == 0 || boss->current_hp <= p->max_hp;
    }
    if (m->primitive_id == RC_PRIM_DOT_TILE_PLACEMENT) {
        const RcPrimParamsDotTilePlacement *p =
            (const RcPrimParamsDotTilePlacement *)m->param_block;
        return p->max_hp == 0 || boss->current_hp <= p->max_hp;
    }
    if (m->primitive_id == RC_PRIM_ATTACK_COUNTER_ALTERNATE_SPECIAL) {
        return true;
    }
    if (m->primitive_id == RC_PRIM_ATTACK_COUNTER_STYLE_SWAP) {
        return true;
    }
    return true;
}

static bool attack_counter_prayer_matches(const RcWorld *world,
                                          const RcEncounterMechanic *m) {
    if (m->primitive_id != RC_PRIM_ATTACK_COUNTER_SPECIAL) return true;
    const RcPrimParamsAttackCounterSpecial *p =
        (const RcPrimParamsAttackCounterSpecial *)m->param_block;
    uint32_t prayer = protect_prayer_for_style(p->condition_prayer_style);
    return prayer == 0 || (world->player.active_prayers & prayer) != 0;
}

static bool leech_spawn_roll(RcWorld *world,
                             const RcPrimParamsSpawnLeechNpc *p) {
    if (!p || p->spawn_chance_denominator <= 1) return true;
    return rc_rng_range(&world->rng_state,
                        p->spawn_chance_denominator - 1) == 0;
}

static void maybe_spawn_leech_from_large_hit(RcWorld *world, int slot,
                                             const RcEncounterSpec *spec,
                                             const RcPayloadNpcDamaged *hit) {
    for (int i = 0; i < spec->mechanic_count; i++) {
        const RcEncounterMechanic *m = &spec->mechanics[i];
        if (m->primitive_id != RC_PRIM_SPAWN_LEECH_NPC || !m->prim) continue;
        const RcPrimParamsSpawnLeechNpc *p =
            (const RcPrimParamsSpawnLeechNpc *)m->param_block;
        if (live_npc_named(world, p->name)) continue;
        if (p->large_hit_threshold == 0 ||
                hit->damage < p->large_hit_threshold) {
            continue;
        }
        if (leech_spawn_roll(world, p)) invoke_mechanic(world, slot, m);
    }
}

static void maybe_spawn_leech_from_boss_attack(RcWorld *world, int slot,
                                               const RcEncounterSpec *spec,
                                               const RcActiveEncounter *a) {
    RcNpc *boss = find_npc_by_uid(world, a->boss_id);
    if (!boss) return;
    for (int i = 0; i < spec->mechanic_count; i++) {
        const RcEncounterMechanic *m = &spec->mechanics[i];
        if (m->primitive_id != RC_PRIM_SPAWN_LEECH_NPC || !m->prim) continue;
        const RcPrimParamsSpawnLeechNpc *p =
            (const RcPrimParamsSpawnLeechNpc *)m->param_block;
        if (live_npc_named(world, p->name)) continue;
        if (p->boss_hp_below == 0 || boss->current_hp >= p->boss_hp_below) {
            continue;
        }
        if (leech_spawn_roll(world, p)) invoke_mechanic(world, slot, m);
    }
}

static bool attack_counter_due(RcWorld *world, const RcActiveEncounter *a,
                               const RcEncounterMechanic *m) {
    bool phase_counter = m->trigger_type == RC_ENC_TRIGGER_PHASE_IN &&
        phase_matches(m, a->current_phase) &&
        (m->primitive_id == RC_PRIM_ANIMATION_WARNING_STYLE_SWAP ||
         m->primitive_id == RC_PRIM_PERIODIC_SPIKE_CLUSTER);
    if (!m->prim) return false;
    if (!phase_counter &&
            m->trigger_type != RC_ENC_TRIGGER_ATTACK_COUNT) {
        return false;
    }
    switch (m->primitive_id) {
        case 0:
        case RC_PRIM_ATTACK_COUNTER_SPECIAL:
        case RC_PRIM_SPAWN_SOUL_ATTACKERS:
        case RC_PRIM_DOT_TILE_PLACEMENT:
        case RC_PRIM_ATTACK_COUNTER_ALTERNATE_SPECIAL:
        case RC_PRIM_ATTACK_COUNTER_STYLE_SWAP:
        case RC_PRIM_ANIMATION_WARNING_STYLE_SWAP:
        case RC_PRIM_PERIODIC_SPIKE_CLUSTER:
            break;
        default:
            return false;
    }
    uint16_t every = attack_counter_every(m);
    bool first = false;
    uint8_t skip = 0;
    if (m->primitive_id == RC_PRIM_ATTACK_COUNTER_SPECIAL) {
        const RcPrimParamsAttackCounterSpecial *p =
            (const RcPrimParamsAttackCounterSpecial *)m->param_block;
        first = p->first_attack_trigger && a->attack_count == 1;
        skip = p->skip_chance_pct;
    } else if (m->primitive_id == RC_PRIM_SPAWN_SOUL_ATTACKERS) {
        const RcPrimParamsSpawnSoulAttackers *p =
            (const RcPrimParamsSpawnSoulAttackers *)m->param_block;
        skip = p->skip_chance_pct;
    } else if (m->primitive_id == RC_PRIM_DOT_TILE_PLACEMENT) {
        const RcPrimParamsDotTilePlacement *p =
            (const RcPrimParamsDotTilePlacement *)m->param_block;
        skip = p->skip_chance_pct;
    }
    if (!first && (every == 0 || (a->attack_count % every) != 0)) {
        return false;
    }
    if (!attack_counter_hp_matches(world, a, m)) return false;
    if (!attack_counter_prayer_matches(world, m)) return false;
    return skip == 0 || rc_rng_range(&world->rng_state, 99) >= skip;
}

static void activate_mechanic_window(RcActiveEncounter *a, uint8_t idx) {
    a->active_mechanic_idx = idx;
    a->active_mechanic_ticks = 24;
}

static void tick_active_mechanic_window(RcWorld *world, int active_idx,
                                        RcActiveEncounter *a,
                                        const RcEncounterSpec *spec) {
    if (a->active_mechanic_idx == 0xFFu || a->active_mechanic_ticks == 0) {
        return;
    }
    const RcEncounterMechanic *active =
        &spec->mechanics[a->active_mechanic_idx];
    for (int i = 0; i < spec->mechanic_count; i++) {
        RcEncounterMechanic *m =
            &((RcEncounterMechanic *)spec->mechanics)[i];
        if (m->trigger_type != RC_ENC_TRIGGER_DURING_MECH) continue;
        if (!trigger_ref_matches(active->name, m->trigger_ref)) continue;
        if (m->period_ticks) {
            tick_mechanic(world, active_idx, m);
        } else {
            invoke_mechanic(world, active_idx, m);
        }
    }
    a->active_mechanic_ticks--;
    if (a->active_mechanic_ticks == 0) {
        const char *advance_to = "";
        if (active->primitive_id == RC_PRIM_NPC_PATHED_MOVEMENT) {
            const RcPrimParamsNpcPathedMovement *p =
                (const RcPrimParamsNpcPathedMovement *)active->param_block;
            advance_to = p->advance_to;
        } else if (active->primitive_id == RC_PRIM_SPAWN_OBJECTIVE_NPCS) {
            const RcPrimParamsSpawnObjectiveNpcs *p =
                (const RcPrimParamsSpawnObjectiveNpcs *)active->param_block;
            advance_to = p->advance_to;
        } else if (active->primitive_id == RC_PRIM_SPAWN_WALL_TENTACLES) {
            const RcPrimParamsSpawnWallTentacles *p =
                (const RcPrimParamsSpawnWallTentacles *)active->param_block;
            advance_to = p->advance_to;
        }
        a->active_mechanic_idx = 0xFFu;
        if (advance_to[0]) {
            rc_encounter_set_phase(world, active_idx, advance_to);
        }
    }
}

static void tick_objective_phase_advances(RcWorld *world, int active_idx,
                                          RcActiveEncounter *a,
                                          const RcEncounterSpec *spec) {
    if (a->active_mechanic_idx < spec->mechanic_count) {
        const RcEncounterMechanic *active =
            &spec->mechanics[a->active_mechanic_idx];
        if (active->primitive_id == RC_PRIM_DAMAGE_REDUCTION_UNTIL_TRIGGER) {
            const RcPrimParamsDamageGate *p =
                (const RcPrimParamsDamageGate *)active->param_block;
            if (p->clear_npc_name[0] &&
                    spawned_and_all_dead_named(world, p->clear_npc_name)) {
                a->active_mechanic_idx = 0xFFu;
                a->active_mechanic_ticks = 0;
            }
        }
    }
    for (int i = 0; i < spec->mechanic_count; i++) {
        const RcEncounterMechanic *m = &spec->mechanics[i];
        if (!phase_matches(m, a->current_phase)) continue;
        const char *npc_name = "";
        const char *advance_to = "";
        if (m->primitive_id == RC_PRIM_SPAWN_OBJECTIVE_NPCS) {
            const RcPrimParamsSpawnObjectiveNpcs *p =
                (const RcPrimParamsSpawnObjectiveNpcs *)m->param_block;
            npc_name = p->npc_name;
            advance_to = p->advance_to;
        } else if (m->primitive_id == RC_PRIM_SPAWN_WALL_TENTACLES) {
            const RcPrimParamsSpawnWallTentacles *p =
                (const RcPrimParamsSpawnWallTentacles *)m->param_block;
            npc_name = p->npc_name[0] ? p->npc_name : "Abyssal tentacle";
            advance_to = p->advance_to;
        } else {
            continue;
        }
        if (advance_to[0] && spawned_and_all_dead_named(world, npc_name)) {
            rc_encounter_set_phase(world, active_idx, advance_to);
            return;
        }
    }
}

// Advance one active encounter. Pass-1 tick: phase HP-threshold
// check + mechanic period countdown. Primitives don't fire yet
// (they're NULL until pass-2 registers them); mechanic timers still
// decrement so the state machine exercises correctly.
static void tick_active(RcWorld *world, RcActiveEncounter *a) {
    RcEncounterState *s = &world->encounter;
    const RcEncounterSpec *spec = &s->registry[a->spec_idx];

    // Find the boss NPC — we reach into the base NPC array, which
    // is part of the always-on base per rc-core/README.md §2.
    RcNpc *boss = NULL;
    for (int i = 0; i < world->npc_count; i++) {
        if (world->npcs[i].active && world->npcs[i].uid == a->boss_id) {
            boss = &world->npcs[i];
            break;
        }
    }
    if (!boss || boss->is_dead) return;   // finish handled by on_died.

    int active_idx = (int)(a - s->active);
    if (a->ticks_since_start == 0) {
        for (int m = 0; m < spec->mechanic_count; m++) {
            const RcEncounterMechanic *mech = &spec->mechanics[m];
            if (mech->trigger_type == RC_ENC_TRIGGER_PHASE_ENTER &&
                    phase_matches(mech, a->current_phase)) {
                invoke_mechanic(world, active_idx, mech);
            } else if (mech->trigger_type == RC_ENC_TRIGGER_PHASE_IN &&
                       mech->period_ticks == 0 &&
                       mech->primitive_id !=
                           RC_PRIM_ANIMATION_WARNING_STYLE_SWAP &&
                       mech->primitive_id != RC_PRIM_PERIODIC_SPIKE_CLUSTER &&
                       phase_matches(mech, a->current_phase)) {
                invoke_mechanic(world, active_idx, mech);
            }
        }
    }
    a->ticks_since_start++;

    // Phase transition check — advance to the next phase whose
    // trigger is met. Only 100→lower HP% transitions are live here;
    // hard hp=0 triggers + timed/event-driven "enter_after" phases
    // remain deferred until the phase model grows beyond the pass-1
    // shell.
    for (int p = a->current_phase + 1; p < spec->phase_count; p++) {
        const RcEncounterPhase *ph = &spec->phases[p];
        if (ph->hard_hp_trigger) continue;
        if (ph->enter_at_hp_pct == 0 || ph->enter_at_hp_pct == 100) continue;
        // Pass-1 shortcut: compute hp_pct off the def's hitpoints
        // (the max-hp at spawn). Pass 2 tracks max_hp on the NPC
        // instance so re-heals after phase revert compute correctly.
        int def_hp = g_npc_defs[boss->def_id].hitpoints;
        if (def_hp <= 0) continue;
        int hp_pct = boss->current_hp * 100 / def_hp;
        if (hp_pct < ph->enter_at_hp_pct) {
            uint8_t old_phase = a->current_phase;
            a->current_phase = (uint8_t)p;
            a->mechanic_progress = 0;
            RcPayloadPhaseTransition payload = {
                .npc_id = a->boss_id,
                .old_phase = old_phase,
                .new_phase = (uint8_t)p,
            };
            rc_event_fire(world, RC_EVT_PHASE_TRANSITION, &payload);
        }
    }

    // Mechanic countdown. Periodic mechanics always tick; phase-in
    // mechanics tick only while their authored phase is current.
    tick_active_mechanic_window(world, active_idx, a, spec);
    tick_objective_phase_advances(world, active_idx, a, spec);
    for (int m = 0; m < spec->mechanic_count; m++) {
        RcEncounterMechanic *mech =
            &((RcEncounterMechanic *)spec->mechanics)[m];
        if (mech->trigger_type == RC_ENC_TRIGGER_PERIODIC) {
            tick_mechanic(world, active_idx, mech);
        } else if (mech->trigger_type == RC_ENC_TRIGGER_PHASE_IN &&
                   phase_matches(mech, a->current_phase)) {
            tick_mechanic(world, active_idx, mech);
        }
    }
}

// ---- Public API --------------------------------------------------------

void rc_encounter_init(RcWorld *world) {
    RcEncounterState *s = &world->encounter;
    memset(s, 0, sizeof(*s));
    rc_event_subscribe(world, RC_EVT_NPC_SPAWNED,
                       rc_encounter_on_npc_spawned, s);
    rc_event_subscribe(world, RC_EVT_NPC_DIED,
                       rc_encounter_on_npc_died, s);
    rc_event_subscribe(world, RC_EVT_PLAYER_DAMAGED,
                       rc_encounter_on_player_damaged, s);
    rc_event_subscribe(world, RC_EVT_PHASE_TRANSITION,
                       rc_encounter_on_phase_transition, s);
    rc_event_subscribe(world, RC_EVT_NPC_ATTACK,
                       rc_encounter_on_npc_attack, s);
    rc_event_subscribe(world, RC_EVT_NPC_DAMAGED,
                       rc_encounter_on_npc_damaged, s);
}

void rc_encounter_tick(RcWorld *world) {
    if (!(world->enabled & RC_SUB_ENCOUNTER)) return;
    RcEncounterState *s = &world->encounter;
    for (int i = 0; i < RC_ENC_MAX_ACTIVE; i++) {
        if (s->active[i].active) tick_active(world, &s->active[i]);
    }
}

int rc_encounter_register(RcWorld *world, const RcEncounterSpec *spec) {
    RcEncounterState *s = &world->encounter;
    if (s->registry_count >= RC_ENC_REGISTRY_CAP) return -1;
    s->registry[s->registry_count] = *spec;
    return s->registry_count++;
}

int rc_encounter_register_script(RcWorld *world, const char *name,
                                 RcEncounterScriptFn fn) {
    if (!world || !name || !name[0] || !fn) return -1;
    RcEncounterState *s = &world->encounter;
    for (int i = 0; i < s->script_count; i++) {
        if (strcmp(s->scripts[i].name, name) == 0) {
            s->scripts[i].fn = fn;
            return i;
        }
    }
    if (s->script_count >= RC_ENC_SCRIPT_REGISTRY_CAP) return -1;
    RcEncounterScriptEntry *entry = &s->scripts[s->script_count];
    memset(entry, 0, sizeof(*entry));
    strncpy(entry->name, name, sizeof(entry->name) - 1);
    entry->fn = fn;
    return s->script_count++;
}

RcEncounterScriptFn rc_encounter_script_lookup(const RcWorld *world,
                                               const char *name) {
    if (!world || !name || !name[0]) return NULL;
    const RcEncounterState *s = &world->encounter;
    for (int i = 0; i < s->script_count; i++) {
        if (strcmp(s->scripts[i].name, name) == 0) {
            return s->scripts[i].fn;
        }
    }
    return NULL;
}

void rc_encounter_script_noop(RcWorld *world, int enc_idx) {
    (void)world;
    (void)enc_idx;
}

int rc_encounter_set_phase(RcWorld *world, int active_idx,
                           const char *phase_id) {
    if (!world || active_idx < 0 || active_idx >= RC_ENC_MAX_ACTIVE) return 0;
    RcActiveEncounter *a = &world->encounter.active[active_idx];
    if (!a->active || a->spec_idx >= world->encounter.registry_count) return 0;
    const RcEncounterSpec *spec = &world->encounter.registry[a->spec_idx];
    int idx = phase_index_by_id(spec, phase_id);
    if (idx < 0) return 0;
    return set_active_phase_idx(world, active_idx, (uint8_t)idx);
}

int rc_encounter_find_spec(const RcWorld *world, uint32_t npc_id) {
    const RcEncounterState *s = &world->encounter;
    for (int i = 0; i < s->registry_count; i++) {
        const RcEncounterSpec *spec = &s->registry[i];
        for (int j = 0; j < spec->npc_id_count; j++) {
            if (spec->npc_ids[j] == npc_id) return i;
        }
    }
    return -1;
}

int rc_encounter_select_npc_attack(RcWorld *world, uint16_t npc_uid,
                                   int distance, uint8_t *style,
                                   uint16_t *min_hit, uint16_t *max_hit,
                                   uint32_t *flags) {
    if (!world || !style || !min_hit || !max_hit || !flags) return -1;
    RcEncounterState *s = &world->encounter;
    int active_idx = find_active_by_npc(s, npc_uid);
    if (active_idx < 0) return -1;
    RcActiveEncounter *active = &s->active[active_idx];
    if (active->spec_idx >= s->registry_count) return -1;
    RcEncounterSpec *spec = &s->registry[active->spec_idx];
    if (spec->attack_count == 0 || active->current_phase >= spec->phase_count) {
        return -1;
    }

    const RcEncounterPhase *phase = &spec->phases[active->current_phase];
    uint32_t mask = phase_attack_mask(phase, spec->attack_count);
    uint8_t preferred_style = preferred_style_from_phase_id(phase);
    if (preferred_style != COMBAT_NONE) {
        for (int i = 0; i < spec->attack_count && i < 32; i++) {
            if ((mask & (1u << i)) == 0) continue;
            if (!style_matches(spec->attacks[i].style, preferred_style)) {
                continue;
            }
            if (!attack_matches_active(&spec->attacks[i], active, distance)) {
                continue;
            }
            active->last_attack_idx = (uint8_t)i;
            *style = spec->attacks[i].style;
            *min_hit = spec->attacks[i].min_hit;
            *max_hit = spec->attacks[i].max_hit;
            *flags = spec->attacks[i].flags;
            return i;
        }
    }
    int total = 0;
    int fallback_total = 0;
    for (int i = 0; i < spec->attack_count && i < 32; i++) {
        if ((mask & (1u << i)) == 0) continue;
        if (!attack_matches_active(&spec->attacks[i], active, distance)) {
            continue;
        }
        total += attack_weight(phase, &spec->attacks[i], distance);
        if (distance <= 1 || !attack_style_is_melee(spec->attacks[i].style)) {
            fallback_total++;
        }
    }

    int pick = total > 0 ? rc_rng_range(&world->rng_state, total - 1)
                         : rc_rng_range(&world->rng_state,
                                        fallback_total > 0
                                        ? fallback_total - 1 : 0);
    int seen = 0;
    for (int i = 0; i < spec->attack_count && i < 32; i++) {
        if ((mask & (1u << i)) == 0) continue;
        if (!attack_matches_active(&spec->attacks[i], active, distance)) {
            continue;
        }
        int weight = total > 0 ? attack_weight(phase, &spec->attacks[i], distance)
                               : (distance <= 1 ||
                                  !attack_style_is_melee(spec->attacks[i].style));
        if (weight <= 0) continue;
        seen += weight;
        if (pick >= seen) continue;
        active->last_attack_idx = (uint8_t)i;
        *style = spec->attacks[i].style;
        *min_hit = spec->attacks[i].min_hit;
        *max_hit = spec->attacks[i].max_hit;
        *flags = spec->attacks[i].flags;
        return i;
    }
    return -1;
}

int rc_encounter_player_protection_scale_pct(const RcWorld *world,
                                             int source_uid, int style,
                                             uint32_t prayer_snapshot) {
    if (!world || source_uid < 0) return -1;
    const RcEncounterState *s = &world->encounter;
    int active_idx = find_active_by_npc(s, (RcNpcId)source_uid);
    if (active_idx < 0) return -1;
    const RcActiveEncounter *active = &s->active[active_idx];
    if (active->spec_idx >= s->registry_count) return -1;
    const RcEncounterSpec *spec = &s->registry[active->spec_idx];
    for (int i = 0; i < spec->protection_count; i++) {
        const RcEncounterProtection *p = &spec->protections[i];
        if (p->style != (uint8_t)style) continue;
        if ((prayer_snapshot & p->prayer_flag) == 0) continue;
        return p->damage_pct;
    }
    return -1;
}

int rc_encounter_scale_player_damage(RcWorld *world,
                                     uint16_t npc_uid, uint8_t style,
                                     int damage) {
    if (!world || damage <= 0) return damage;
    const RcEncounterState *s = &world->encounter;
    int active_idx = find_active_by_npc(s, (RcNpcId)npc_uid);
    if (active_idx < 0) return damage;
    const RcActiveEncounter *active = &s->active[active_idx];
    if (active->spec_idx >= s->registry_count) return damage;
    const RcEncounterSpec *spec = &s->registry[active->spec_idx];
    if (active_spawn_blocker_alive(world, active, spec)) return 0;
    if (active->active_mechanic_idx < spec->mechanic_count) {
        const RcEncounterMechanic *m =
            &spec->mechanics[active->active_mechanic_idx];
        if (m->primitive_id == RC_PRIM_HEAL_BOSS_ON_PLAYER_ATTACK_MISS) {
            const RcPrimParamsHealOnAttack *p =
                (const RcPrimParamsHealOnAttack *)m->param_block;
            RcNpc *boss = find_npc_by_uid(world, active->boss_id);
            if (boss && p->heal_per_attack) {
                heal_npc_to_def_cap(boss, p->heal_per_attack);
            }
            if (p->cancel_player_attack) return 0;
        }
    }
    int out = damage;
    for (int i = 0; i < spec->damage_mod_count; i++) {
        const RcEncounterDamageModifier *m = &spec->damage_mods[i];
        if (!damage_modifier_matches(world, m, active->def_id, style)) {
            continue;
        }
        if (m->condition == RC_ENC_DMG_PLAYER_NOT_FACEMASK) continue;
        if (m->condition == RC_ENC_DMG_CAP_RANGE) {
            int min = m->style;
            int max = m->damage_pct >= m->style ? m->damage_pct : m->style;
            if (out > max) out = min + (int)rc_rng_range(&world->rng_state,
                                                         max - min);
        } else {
            out = (out * m->damage_pct) / 100;
        }
    }
    out = (out * active_environment_damage_pct(active, spec)) / 100;
    return out;
}

int rc_encounter_scale_incoming_damage(RcWorld *world,
                                       int source_uid, uint8_t style,
                                       int damage) {
    if (!world || source_uid < 0 || damage <= 0) return damage;
    const RcEncounterState *s = &world->encounter;
    int active_idx = find_active_by_npc(s, (RcNpcId)source_uid);
    if (active_idx < 0) return damage;
    const RcActiveEncounter *active = &s->active[active_idx];
    if (active->spec_idx >= s->registry_count) return damage;
    const RcEncounterSpec *spec = &s->registry[active->spec_idx];
    int out = damage;
    for (int i = 0; i < spec->damage_mod_count; i++) {
        const RcEncounterDamageModifier *m = &spec->damage_mods[i];
        if (m->condition != RC_ENC_DMG_PLAYER_NOT_FACEMASK) continue;
        if (!damage_modifier_matches(world, m, active->def_id, style)) {
            continue;
        }
        out = (out * m->damage_pct) / 100;
    }
    return out;
}

bool rc_encounter_player_can_target_npc(const RcWorld *world,
                                        uint16_t npc_uid) {
    if (!world) return true;
    const RcEncounterState *s = &world->encounter;
    int active_idx = find_active_by_npc(s, (RcNpcId)npc_uid);
    if (active_idx < 0) return true;
    const RcActiveEncounter *active = &s->active[active_idx];
    if (active->spec_idx >= s->registry_count) return true;
    const RcEncounterSpec *spec = &s->registry[active->spec_idx];
    if (active->current_phase >= spec->phase_count) return true;
    return spec->phases[active->current_phase].player_targetable;
}

int rc_encounter_reveal_hidden_npcs(RcWorld *world, const char *npc_name,
                                    int max_count) {
    if (!world || !npc_name || !npc_name[0] || max_count == 0) return 0;
    int revealed = 0;
    for (int i = 0; i < world->npc_count; i++) {
        RcNpc *npc = &world->npcs[i];
        if (!npc->active || npc->is_dead || !npc->player_untargetable) continue;
        if (npc->def_id < 0 || npc->def_id >= g_npc_def_count) continue;
        if (strcmp(g_npc_defs[npc->def_id].name, npc_name) != 0) continue;
        npc->player_untargetable = false;
        revealed++;
        if (max_count > 0 && revealed >= max_count) break;
    }
    return revealed;
}

int rc_encounter_add_effect(RcWorld *world, uint8_t kind,
                            int x, int y, int plane,
                            int target_x, int target_y,
                            int ticks, uint16_t source_uid,
                            uint8_t style, uint8_t damage_per_tick,
                            const char *name, const char *target_name) {
    if (!world || kind == RC_ENC_EFFECT_NONE) return -1;
    for (int i = 0; i < RC_ENC_MAX_EFFECTS; i++) {
        RcEncounterEffect *e = &world->encounter_effects[i];
        if (e->active) continue;
        memset(e, 0, sizeof(*e));
        e->active = true;
        e->kind = kind;
        e->style = style;
        e->damage_per_tick = damage_per_tick;
        e->source_uid = source_uid;
        e->x = x;
        e->y = y;
        e->plane = plane;
        e->target_x = target_x;
        e->target_y = target_y;
        e->ticks_remaining = ticks;
        copy_effect_name(e->name, sizeof(e->name), name);
        copy_effect_name(e->target_name, sizeof(e->target_name), target_name);
        if (i >= world->encounter_effect_count) {
            world->encounter_effect_count = i + 1;
        }
        return i;
    }
    return -1;
}

void rc_encounter_tick_effects(RcWorld *world) {
    if (!world) return;
    int last_active = -1;
    for (int i = 0; i < world->encounter_effect_count; i++) {
        RcEncounterEffect *e = &world->encounter_effects[i];
        if (!e->active) continue;
        if ((e->kind == RC_ENC_EFFECT_LAVA_POOL ||
             e->kind == RC_ENC_EFFECT_ACID_POOL) &&
                e->damage_per_tick > 0 &&
                world->player.plane == e->plane &&
                world->player.x == e->x &&
                world->player.y == e->y) {
            rc_queue_hit(world->player.pending_hits,
                         &world->player.num_pending_hits,
                         e->damage_per_tick, 0,
                         e->style ? e->style : COMBAT_MAGIC,
                         e->source_uid, world->player.active_prayers,
                         world->tick);
        }
        if (e->ticks_remaining > 0) {
            e->ticks_remaining--;
            if (e->ticks_remaining == 0) {
                if (e->kind == RC_ENC_EFFECT_FORM_DIVE) {
                    RcNpc *npc = find_npc_by_uid(world, e->source_uid);
                    if (npc) npc->player_untargetable = false;
                }
                e->active = false;
                continue;
            }
        }
        last_active = i;
    }
    world->encounter_effect_count = last_active + 1;
}

int rc_encounter_interact_object(RcWorld *world, int obj_id,
                                 const char *object_name,
                                 int x, int y, int plane, int opt) {
    (void)obj_id;
    (void)opt;
    if (!world || !object_name || !object_name[0]) return 0;
    for (int i = 0; i < world->encounter_effect_count; i++) {
        RcEncounterEffect *e = &world->encounter_effects[i];
        if (!e->active || e->kind != RC_ENC_EFFECT_HIDDEN_OBJECT) continue;
        if (strcmp(e->name, object_name) != 0) continue;
        if (x >= 0 && y >= 0 && plane >= 0 &&
                (e->x != x || e->y != y || e->plane != plane)) {
            continue;
        }
        if (rc_encounter_reveal_hidden_npcs(world, e->target_name, 1) <= 0) {
            return 0;
        }
        e->active = false;
        return 1;
    }

    for (int slot = 0; slot < RC_ENC_MAX_ACTIVE; slot++) {
        RcActiveEncounter *a = &world->encounter.active[slot];
        if (!a->active || a->spec_idx >= world->encounter.registry_count) {
            continue;
        }
        RcEncounterSpec *spec = &world->encounter.registry[a->spec_idx];
        for (int i = 0; i < spec->mechanic_count; i++) {
            RcEncounterMechanic *m = &spec->mechanics[i];
            if (!m->prim) continue;
            if (m->primitive_id == RC_PRIM_OBJECT_INTERACTION_TICKED) {
                const RcPrimParamsObjectInteractionTicked *p =
                    (const RcPrimParamsObjectInteractionTicked *)m->param_block;
                bool matches = false;
                for (int n = 0; n < 3; n++) {
                    if (p->object_names[n][0] &&
                            contains_ci(object_name, p->object_names[n])) {
                        matches = true;
                        break;
                    }
                }
                if (!matches) continue;
                if (a->active_mechanic_idx < spec->mechanic_count &&
                        spec->mechanics[a->active_mechanic_idx].primitive_id ==
                            RC_PRIM_DAMAGE_REDUCTION_UNTIL_TRIGGER) {
                    a->active_mechanic_idx = 0xFFu;
                    a->active_mechanic_ticks = 0;
                }
                return 1;
            }
            if (m->primitive_id == RC_PRIM_HEAL_ALTARS_PLAYER_MUST_DISABLE) {
                if (!contains_ci(object_name, "altar")) continue;
                const RcPrimParamsHealAltars *p =
                    (const RcPrimParamsHealAltars *)m->param_block;
                if (!p->disable_on_click) continue;
                uint8_t altar_count = p->altar_count ? p->altar_count : 4;
                if (a->mechanic_progress < altar_count) {
                    a->mechanic_progress++;
                }
                return 1;
            }
            if (m->primitive_id == RC_PRIM_OBJECT_ITEM_INTERACTION) {
                if (!phase_matches(m, a->current_phase)) continue;
                if (!contains_ci(object_name, "herb")) continue;
                const RcPrimParamsObjectItemInteraction *p =
                    (const RcPrimParamsObjectItemInteraction *)m->param_block;
                a->mechanic_progress++;
                uint8_t needed = p->correct_herbs_to_wake
                                 ? p->correct_herbs_to_wake : 1;
                if (a->mechanic_progress >= needed) {
                    a->mechanic_progress = 0;
                    if (p->advance_to[0]) {
                        rc_encounter_set_phase(world, slot, p->advance_to);
                    }
                }
                return 1;
            }
        }
    }
    return 0;
}

void rc_encounter_on_npc_spawned(RcWorld *world, int evt,
                                 const void *payload, void *ctx) {
    (void)evt;
    RcEncounterState *s = (RcEncounterState *)ctx;
    const RcPayloadNpcEvent *p = (const RcPayloadNpcEvent *)payload;
    if (!p) return;

    int spec_idx = rc_encounter_find_spec(world, p->def_id);
    if (spec_idx < 0) return;
    int slot = find_active_slot(s);
    if (slot < 0) return;

    RcActiveEncounter *a = &s->active[slot];
    a->active = true;
    a->spec_idx = (uint16_t)spec_idx;
    a->boss_id = p->npc_id;
    a->def_id = p->def_id;
    a->current_phase = 0;
    a->last_attack_idx = 0xFFu;
    a->active_mechanic_idx = 0xFFu;
    a->invoking_mechanic_idx = 0xFFu;
    a->active_mechanic_ticks = 0;
    a->mechanic_progress = 0;
    a->shield_points = 0;
    a->script_flags = 0;
    a->ticks_since_start = 0;
    s->started_count++;
    run_phase_script(world, slot, 0);
}

void rc_encounter_on_npc_died(RcWorld *world, int evt,
                              const void *payload, void *ctx) {
    (void)evt;
    RcEncounterState *s = (RcEncounterState *)ctx;
    const RcPayloadNpcEvent *p = (const RcPayloadNpcEvent *)payload;
    if (!p) return;

    int slot = find_active_by_npc(s, p->npc_id);
    if (slot < 0) return;
    RcActiveEncounter *a = &s->active[slot];
    const RcEncounterSpec *spec = &s->registry[a->spec_idx];
    const RcPrimParamsGroupKillRequired *group = group_kill_params(spec);
    const RcPrimParamsPeriodicRespawnIfDead *respawn = respawn_params(spec);
    if (group && group->count > 0) {
        if (!group_is_dead(world, group)) {
            if (respawn_applies(respawn, a->def_id)) {
                for (int i = 0; i < world->npc_count; i++) {
                    RcNpc *npc = &world->npcs[i];
                    if (npc->uid == p->npc_id) {
                        npc->respawn_timer = respawn->cooldown_ticks;
                        break;
                    }
                }
            }
            return;
        }
        for (int i = 0; i < RC_ENC_MAX_ACTIVE; i++) {
            if (s->active[i].active &&
                    s->active[i].spec_idx == a->spec_idx) {
                s->active[i].active = false;
            }
        }
        s->finished_count++;
        return;
    }

    for (int i = 0; i < spec->mechanic_count; i++) {
        const RcEncounterMechanic *m = &spec->mechanics[i];
        if (m->trigger_type == RC_ENC_TRIGGER_EVENT &&
                trigger_ref_matches(m->trigger_ref, "zuk_killed")) {
            invoke_mechanic(world, slot, m);
        }
    }
    s->active[slot].active = false;
    s->finished_count++;
}

// PLAYER_DAMAGED → route to event-driven mechanics.
// Pass-2 wiring covers drain_prayer_on_hit (KQ Barbed Spines). When
// the damaging hit's source NPC is a boss in an active encounter, we
// invoke any drain_prayer_on_hit primitive on that spec. Future
// event-driven primitives (e.g. venom on hit, stat-drain on hit)
// route through the same dispatch.
void rc_encounter_on_player_damaged(RcWorld *world, int evt,
                                    const void *payload, void *ctx) {
    (void)evt;
    RcEncounterState *s = (RcEncounterState *)ctx;
    const RcPayloadPlayerDamaged *p = (const RcPayloadPlayerDamaged *)payload;
    if (!p) return;
    if (p->source_npc_id == 0xFFFFu) return;      // non-NPC source

    int slot = find_active_by_npc(s, p->source_npc_id);
    if (slot < 0) return;                         // not a tracked boss

    RcActiveEncounter *a = &s->active[slot];
    const RcEncounterSpec *spec = &s->registry[a->spec_idx];
    apply_attack_effect(world, a, spec, p->damage);
    for (int i = 0; i < spec->mechanic_count; i++) {
        const RcEncounterMechanic *m = &spec->mechanics[i];
        if (p->damage == 0) continue;             // mitigated → no drain
        if (m->primitive_id == RC_PRIM_DRAIN_PRAYER_ON_HIT && m->prim) {
            m->prim(world, slot, m->param_block);
        }
    }
}

void rc_encounter_on_phase_transition(RcWorld *world, int evt,
                                      const void *payload, void *ctx) {
    (void)evt;
    RcEncounterState *s = (RcEncounterState *)ctx;
    const RcPayloadPhaseTransition *p =
        (const RcPayloadPhaseTransition *)payload;
    if (!p) return;

    int slot = find_active_by_npc(s, p->npc_id);
    if (slot < 0) return;

    RcActiveEncounter *a = &s->active[slot];
    const RcEncounterSpec *spec = &s->registry[a->spec_idx];
    for (int i = 0; i < spec->mechanic_count; i++) {
        const RcEncounterMechanic *m = &spec->mechanics[i];
        if (!m->prim) continue;

        if (m->trigger_type == RC_ENC_TRIGGER_PHASE_ENTER &&
            phase_matches(m, p->new_phase)) {
            m->prim(world, slot, m->param_block);
        } else if (m->trigger_type == RC_ENC_TRIGGER_PHASE_EXIT &&
                   phase_matches(m, p->old_phase)) {
            m->prim(world, slot, m->param_block);
        } else if (m->trigger_type == RC_ENC_TRIGGER_PHASE_IN &&
                   m->period_ticks == 0 &&
                   m->primitive_id != RC_PRIM_ANIMATION_WARNING_STYLE_SWAP &&
                   m->primitive_id != RC_PRIM_PERIODIC_SPIKE_CLUSTER &&
                   phase_matches(m, p->new_phase)) {
            m->prim(world, slot, m->param_block);
        }
    }

    run_phase_script(world, slot, p->new_phase);
}

void rc_encounter_on_npc_attack(RcWorld *world, int evt,
                                const void *payload, void *ctx) {
    (void)evt;
    RcEncounterState *s = (RcEncounterState *)ctx;
    const RcPayloadNpcAttack *p = (const RcPayloadNpcAttack *)payload;
    if (!p) return;

    int slot = find_active_by_npc(s, p->npc_id);
    if (slot < 0) return;
    RcActiveEncounter *a = &s->active[slot];
    const RcEncounterSpec *spec = &s->registry[a->spec_idx];
    a->attack_count++;
    maybe_spawn_leech_from_boss_attack(world, slot, spec, a);

    const char *attack_name = attack_name_for_active(spec, a);
    for (int i = 0; i < spec->mechanic_count; i++) {
        const RcEncounterMechanic *m = &spec->mechanics[i];
        if (m->trigger_type == RC_ENC_TRIGGER_AFTER_ATTACK &&
                trigger_ref_matches(attack_name, m->trigger_ref)) {
            invoke_mechanic(world, slot, m);
        }
    }

    int best = -1;
    uint8_t best_priority = 255;
    for (int i = 0; i < spec->mechanic_count; i++) {
        const RcEncounterMechanic *m = &spec->mechanics[i];
        if (!attack_counter_due(world, a, m)) continue;
        uint8_t priority = attack_counter_priority(m);
        if (best < 0 || priority < best_priority) {
            best = i;
            best_priority = priority;
        }
    }
    if (best >= 0) {
        const RcEncounterMechanic *m = &spec->mechanics[best];
        invoke_mechanic(world, slot, m);
        if (m->primitive_id != RC_PRIM_ATTACK_COUNTER_ALTERNATE_SPECIAL &&
                m->primitive_id != RC_PRIM_ANIMATION_WARNING_STYLE_SWAP &&
                m->primitive_id != RC_PRIM_PERIODIC_SPIKE_CLUSTER) {
            activate_mechanic_window(a, (uint8_t)best);
        }
    }
}

void rc_encounter_on_npc_damaged(RcWorld *world, int evt,
                                 const void *payload, void *ctx) {
    (void)evt;
    RcEncounterState *s = (RcEncounterState *)ctx;
    const RcPayloadNpcDamaged *p = (const RcPayloadNpcDamaged *)payload;
    if (!p || p->damage == 0) return;

    int slot = find_active_by_npc(s, p->npc_id);
    if (slot < 0) return;
    RcActiveEncounter *a = &s->active[slot];
    const RcEncounterSpec *spec = &s->registry[a->spec_idx];
    maybe_spawn_leech_from_large_hit(world, slot, spec, p);
    for (int i = 0; i < spec->mechanic_count; i++) {
        const RcEncounterMechanic *m = &spec->mechanics[i];
        if (!m->prim) continue;
        if (m->primitive_id == RC_PRIM_TELEPORT_ON_INCOMING_ATTACK ||
                m->primitive_id == RC_PRIM_TOTEM_CHARGE_PROGRESSION ||
                (m->primitive_id == RC_PRIM_SMITE_DRAIN_SHIELD &&
                 a->shield_points > 0)) {
            invoke_mechanic(world, slot, m);
        }
    }
}

// ---- Binary loader ----------------------------------------------------

// Safe-read helper — returns 0 on short read.
#define RD(ptr, n) (fread((ptr), 1, (n), f) == (size_t)(n))

static int read_pstr(FILE *f, char *out, int cap) {
    uint8_t len;
    if (!RD(&len, 1)) return 0;
    if (len >= cap) return 0;
    if (len && !RD(out, len)) return 0;
    out[len] = '\0';
    return 1;
}

int rc_encounter_load(RcWorld *world, const char *path) {
    FILE *f = rc_asset_fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "encounter_load: can't open %s\n", path);
        return -1;
    }
    uint32_t magic, version, count;
    if (!RD(&magic, 4) || !RD(&version, 4) || !RD(&count, 4)) {
        fprintf(stderr, "encounter_load: bad header\n"); rc_asset_close(f);
        return -1;
    }
    if (magic != ENCT_MAGIC) {
        fprintf(stderr, "encounter_load: bad magic %08x\n", magic);
        rc_asset_close(f); return -1;
    }
    if (version == 0 || version > 12) {
        fprintf(stderr, "encounter_load: unsupported version %u\n", version);
        rc_asset_close(f); return -1;
    }

    int loaded = 0;
    for (uint32_t i = 0; i < count; i++) {
        RcEncounterSpec s;
        memset(&s, 0, sizeof(s));

        if (!read_pstr(f, s.slug, sizeof(s.slug))) break;

        uint8_t nid_count;
        if (!RD(&nid_count, 1)) break;
        s.npc_id_count = nid_count > RC_ENC_MAX_NPC_IDS
                         ? RC_ENC_MAX_NPC_IDS : nid_count;
        for (uint8_t j = 0; j < nid_count; j++) {
            uint32_t nid;
            if (!RD(&nid, 4)) { rc_asset_close(f); return loaded; }
            if (j < RC_ENC_MAX_NPC_IDS) s.npc_ids[j] = nid;
        }

        uint8_t atk_count;
        if (!RD(&atk_count, 1)) break;
        s.attack_count = atk_count > RC_ENC_MAX_ATTACKS
                         ? RC_ENC_MAX_ATTACKS : atk_count;
        for (uint8_t j = 0; j < atk_count; j++) {
            RcEncounterAttack a; memset(&a, 0, sizeof(a));
            if (!read_pstr(f, a.name, sizeof(a.name))) { rc_asset_close(f); return loaded; }
            uint8_t style, warn;
            uint16_t maxhit;
            if (!RD(&style, 1) || !RD(&maxhit, 2) || !RD(&warn, 1)) {
                rc_asset_close(f); return loaded;
            }
            a.style = style;
            a.max_hit = maxhit;
            a.warning_ticks = warn;
            if (version >= 5) {
                if (!RD(&a.npc_id, 4) || !RD(&a.min_hit, 2) ||
                    !RD(&a.flags, 4)) {
                    rc_asset_close(f); return loaded;
                }
            }
            if (version >= 8) {
                if (!RD(&a.effect_id, 1) || !RD(&a.effect_min, 1) ||
                    !RD(&a.effect_max, 1) || !RD(&a.effect_pct, 1) ||
                    !RD(&a.effect_flags, 1)) {
                    rc_asset_close(f); return loaded;
                }
            }
            if (j < RC_ENC_MAX_ATTACKS) s.attacks[j] = a;
        }

        uint8_t ph_count;
        if (!RD(&ph_count, 1)) break;
        s.phase_count = ph_count > RC_ENC_MAX_PHASES
                        ? RC_ENC_MAX_PHASES : ph_count;
        for (uint8_t j = 0; j < ph_count; j++) {
            RcEncounterPhase p; memset(&p, 0, sizeof(p));
            p.player_targetable = true;
            if (!read_pstr(f, p.id, sizeof(p.id))) { rc_asset_close(f); return loaded; }
            uint8_t pct, hard;
            if (!RD(&pct, 1) || !RD(&hard, 1)) { rc_asset_close(f); return loaded; }
            p.enter_at_hp_pct = pct;
            p.hard_hp_trigger = (bool)hard;
            if (version >= 3) {
                if (!read_pstr(f, p.script, sizeof(p.script))) {
                    rc_asset_close(f); return loaded;
                }
            }
            if (version >= 4) {
                if (!RD(&p.allowed_attack_mask, 4) ||
                    !RD(p.adjacent_style_weights,
                        sizeof(p.adjacent_style_weights)) ||
                    !RD(p.distant_style_weights,
                        sizeof(p.distant_style_weights))) {
                    rc_asset_close(f); return loaded;
                }
            }
            if (version >= 7) {
                uint8_t targetable;
                if (!RD(&targetable, 1)) { rc_asset_close(f); return loaded; }
                p.player_targetable = targetable != 0;
            }
            if (version >= 9) {
                uint8_t explicit_mask;
                if (!RD(&explicit_mask, 1)) { rc_asset_close(f); return loaded; }
                p.allowed_attack_mask_explicit = explicit_mask != 0;
            } else {
                p.allowed_attack_mask_explicit = p.allowed_attack_mask != 0;
            }
            if (j < RC_ENC_MAX_PHASES) s.phases[j] = p;
        }

        uint8_t mech_count;
        if (!RD(&mech_count, 1)) break;
        s.mechanic_count = mech_count > RC_ENC_MAX_MECHANICS
                           ? RC_ENC_MAX_MECHANICS : mech_count;
        for (uint8_t j = 0; j < mech_count; j++) {
            RcEncounterMechanic m; memset(&m, 0, sizeof(m));
            if (!read_pstr(f, m.name, sizeof(m.name))) { rc_asset_close(f); return loaded; }
            uint8_t prim;
            uint16_t period;
            if (!RD(&prim, 1) || !RD(&period, 2)) { rc_asset_close(f); return loaded; }
            if (version >= 2) {
                if (!RD(&m.trigger_type, 1) || !RD(&m.phase_idx, 1)) {
                    rc_asset_close(f); return loaded;
                }
            } else {
                m.trigger_type = RC_ENC_TRIGGER_PERIODIC;
                m.phase_idx = 0xFFu;
            }
            if (version >= 3) {
                if (!RD(&m.phase_mask, 4) ||
                    !read_pstr(f, m.trigger_ref, sizeof(m.trigger_ref))) {
                    rc_asset_close(f); return loaded;
                }
            } else if (m.phase_idx < 32) {
                m.phase_mask = 1u << m.phase_idx;
            }
            if (!RD(m.param_block, sizeof(m.param_block))) {
                rc_asset_close(f); return loaded;
            }
            m.primitive_id = prim;
            m.prim = rc_encounter_prim_lookup(prim);
            m.period_ticks = period;
            m.ticks_until_next = 0;
            if (j < RC_ENC_MAX_MECHANICS) s.mechanics[j] = m;
        }

        if (version >= 4) {
            uint8_t prot_count;
            if (!RD(&prot_count, 1)) break;
            s.protection_count = prot_count > RC_ENC_MAX_PROTECTIONS
                                 ? RC_ENC_MAX_PROTECTIONS : prot_count;
            for (uint8_t j = 0; j < prot_count; j++) {
                RcEncounterProtection p;
                memset(&p, 0, sizeof(p));
                if (!RD(&p.attack_idx, 1) || !RD(&p.style, 1) ||
                    !RD(&p.prayer_flag, 4) || !RD(&p.damage_pct, 1)) {
                    rc_asset_close(f); return loaded;
                }
                if (j < RC_ENC_MAX_PROTECTIONS) s.protections[j] = p;
            }
        }

        if (version >= 6) {
            uint8_t mod_count;
            if (!RD(&mod_count, 1)) break;
            s.damage_mod_count = mod_count > RC_ENC_MAX_DAMAGE_MODS
                                 ? RC_ENC_MAX_DAMAGE_MODS : mod_count;
            for (uint8_t j = 0; j < mod_count; j++) {
                RcEncounterDamageModifier m;
                memset(&m, 0, sizeof(m));
                if (!RD(&m.npc_id, 4) || !RD(&m.condition, 1) ||
                    !RD(&m.style, 1) || !RD(&m.damage_pct, 1) ||
                    !RD(&m.flags, 1)) {
                    rc_asset_close(f); return loaded;
                }
                if (j < RC_ENC_MAX_DAMAGE_MODS) s.damage_mods[j] = m;
            }
        }

        if (rc_encounter_register(world, &s) >= 0) loaded++;
    }

    rc_asset_close(f);
    fprintf(stderr, "encounter_load: loaded %d / %u encounters from %s\n",
            loaded, count, path);
    return loaded;
}

#undef RD
