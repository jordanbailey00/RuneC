#include "encounter.h"
#include "api.h"
#include "assets.h"
#include "combat.h"
#include "combat_hit.h"
#include "config.h"
#include "consumables.h"
#include "coordinates.h"
#include "events.h"
#include "items.h"
#include "npc.h"
#include "object_runtime.h"
#include "objects.h"
#include "pathfinding.h"
#include "prayer.h"
#include "rng.h"
#include "spells.h"
#include "types.h"
#include <limits.h>
#include <stdio.h>
#include <string.h>

#define ENCT_MAGIC 0x54434E45u     // 'ENCT'
#define ENCT_VERSION 15u
// Engine policy, not OSRS constants: an engaged fight ends after this many
// consecutive ticks in which neither side targets the other, or when the
// player is further than the leash from the boss footprint.
#define RC_ENC_CALM_TICKS 16
#define RC_ENC_LEASH_TILES 32
#define RC_ENC_HELPER_SLOTS (RC_ENC_MAX_ACTIVE * RC_ENC_MAX_RUN_HELPERS)
#define RC_ENC_NO_MECHANIC 0xFFu
// A dead guard waits on this respawn timer until its boss next binds.
#define RC_ENC_GUARD_HOLD_TICKS (1 << 30)

// ---- Lookup ------------------------------------------------------------

static const RcEncounterSpec *run_spec(const RcWorld *world,
                                       const RcActiveEncounter *a) {
    const RcEncounterState *s = &world->encounter;
    if (!a->active || !s->specs || a->spec_idx >= s->spec_count) return NULL;
    return &s->specs[a->spec_idx];
}

static bool in_arena(const RcEncounterSpec *spec, int x, int y, int plane) {
    return spec->has_arena && plane == spec->arena_plane
        && x >= spec->arena_min_x && x <= spec->arena_max_x
        && y >= spec->arena_min_y && y <= spec->arena_max_y;
}

static bool spec_has_guard(const RcEncounterSpec *spec, uint32_t def_id) {
    for (int g = 0; g < spec->guard_count; g++) {
        if (spec->guards[g].npc_id == def_id) return true;
    }
    return false;
}

static bool spec_has_form(const RcEncounterSpec *spec, uint32_t def_id) {
    for (int f = 0; f < spec->form_count; f++) {
        if (spec->forms[f].npc_id == def_id) return true;
    }
    return false;
}

// Spec whose arena holds this guard's spawn, or -1.
static int guard_spec(const RcWorld *world, uint32_t def_id,
                      const RcNpc *npc) {
    const RcEncounterState *s = &world->encounter;
    for (int i = 0; s->specs && i < s->spec_count; i++) {
        if (!(s->arena_specs & (1ull << i))) continue;
        const RcEncounterSpec *spec = &s->specs[i];
        if (spec_has_guard(spec, def_id)
                && in_arena(spec, npc->spawn_x, npc->spawn_y, npc->spawn_plane))
            return i;
    }
    return -1;
}

static void apply_form_defence(RcNpc *npc, const RcEncounterForm *form) {
    for (int i = 0; i < 5; i++)
        npc->defence_bonus[i] = form ? form->defence_bonus[i] : 0;
}

const RcEncounterSpec *rc_encounter_spec(const RcWorld *world, int spec_idx) {
    if (!world || !world->encounter.specs || spec_idx < 0
            || spec_idx >= world->encounter.spec_count) return NULL;
    return &world->encounter.specs[spec_idx];
}

// Combat asks this for every NPC each tick; the mask keeps worlds without
// runs from touching the slots at all.
int rc_encounter_run_for_npc(const RcWorld *world, RcNpcId npc_uid) {
    if (!world || npc_uid == RC_NPC_NONE) return -1;
    uint16_t mask = world->encounter.active_mask;
    for (int i = 0; mask; i++, mask >>= 1) {
        if ((mask & 1u) && world->encounter.active[i].boss_id == npc_uid)
            return i;
    }
    return -1;
}

static int run_for_helper(const RcWorld *world, RcNpcId uid, int *helper_at) {
    if (uid == RC_NPC_NONE) return -1;
    uint16_t mask = world->encounter.active_mask;
    for (int i = 0; mask; i++, mask >>= 1) {
        const RcActiveEncounter *a = &world->encounter.active[i];
        if (!(mask & 1u)) continue;
        for (int h = 0; h < a->helper_count; h++) {
            if (a->helpers[h] != uid) continue;
            if (helper_at) *helper_at = h;
            return i;
        }
    }
    return -1;
}

// NPC slots never move while an NPC lives, so the bound slot is the boss
// until it is removed; a mismatch means the boss is gone.
static RcNpc *run_boss(RcWorld *world, RcActiveEncounter *a) {
    if (a->boss_slot < 0 || a->boss_slot >= world->npc_count) return NULL;
    RcNpc *npc = &world->npcs[a->boss_slot];
    return npc->active && (RcNpcId)npc->uid == a->boss_id ? npc : NULL;
}

static int form_def_index(const RcEncounterSpec *spec, uint8_t form) {
    return form < spec->form_count
         ? rc_npc_def_find((int)spec->forms[form].npc_id) : -1;
}

static bool form_dormant(const RcEncounterSpec *spec,
                         const RcActiveEncounter *a) {
    return a->form < spec->form_count && spec->forms[a->form].dormant;
}

static bool mechanic_in_phase(const RcEncounterMechanic *m, uint8_t phase) {
    if (m->phase_mask) return phase < 32 && (m->phase_mask & (1u << phase));
    return m->phase_idx == 0xFFu || m->phase_idx == phase;
}

static int chebyshev(int ax, int ay, int bx, int by) {
    int dx = ax > bx ? ax - bx : bx - ax;
    int dy = ay > by ? ay - by : by - ay;
    return dx > dy ? dx : dy;
}

// Distance from a point to an NPC footprint (0 = inside).
static int footprint_distance(const RcWorld *world, const RcNpc *npc,
                              int x, int y) {
    const RcNpcDef *def = rc_npc_def_for_npc(world, npc);
    int size = def && def->size > 0 ? def->size : 1;
    int nx = x < npc->x ? npc->x : x > npc->x + size - 1 ? npc->x + size - 1 : x;
    int ny = y < npc->y ? npc->y : y > npc->y + size - 1 ? npc->y + size - 1 : y;
    return chebyshev(x, y, nx, ny);
}

// ---- Mechanics and phases ---------------------------------------------

static void invoke_mechanic(RcWorld *world, int slot,
                            const RcEncounterMechanic *mech) {
    RcActiveEncounter *a = &world->encounter.active[slot];
    const RcEncounterSpec *spec = run_spec(world, a);
    if (!spec || !mech->prim) return;
    a->invoking_mechanic_idx = (uint8_t)(mech - spec->mechanics);
    mech->prim(world, slot, mech->param_block);
    if (a->active) a->invoking_mechanic_idx = RC_ENC_NO_MECHANIC;
    world->encounter.triggered_mechanics++;
}

static void run_phase_script(RcWorld *world, int slot, uint8_t phase) {
    RcEncounterState *s = &world->encounter;
    const RcEncounterSpec *spec = run_spec(world, &s->active[slot]);
    if (!spec || phase >= spec->phase_count || !spec->phases[phase].script[0])
        return;
    // Binding refuses specs whose scripts are not registered.
    RcEncounterScriptFn fn =
        rc_encounter_script_lookup(world, spec->phases[phase].script);
    if (!fn) return;
    fn(world, slot);
    s->scripts_called++;
}

// The single phase-change owner. Exit and enter mechanics and the phase
// script run here directly; the event only notifies observers.
static void enter_phase(RcWorld *world, int slot, uint8_t next, bool initial) {
    RcActiveEncounter *a = &world->encounter.active[slot];
    const RcEncounterSpec *spec = run_spec(world, a);
    if (!spec || next >= spec->phase_count) return;
    uint8_t old = a->current_phase;
    if (!initial && next == old) return;
    for (int m = 0; !initial && m < spec->mechanic_count && a->active; m++) {
        const RcEncounterMechanic *mech = &spec->mechanics[m];
        if (mech->trigger_type == RC_ENC_TRIGGER_PHASE_EXIT
                && mechanic_in_phase(mech, old))
            invoke_mechanic(world, slot, mech);
    }
    if (!a->active) return;
    a->current_phase = next;
    a->mechanic_progress = 0;
    for (int m = 0; m < spec->mechanic_count; m++) {
        const RcEncounterMechanic *mech = &spec->mechanics[m];
        if (mech->trigger_type == RC_ENC_TRIGGER_PHASE_IN
                && mechanic_in_phase(mech, next))
            a->next_due[m] = world->tick + mech->period_ticks;
    }
    for (int m = 0; m < spec->mechanic_count && a->active; m++) {
        const RcEncounterMechanic *mech = &spec->mechanics[m];
        if (mech->trigger_type == RC_ENC_TRIGGER_PHASE_ENTER
                && mechanic_in_phase(mech, next))
            invoke_mechanic(world, slot, mech);
    }
    if (!a->active) return;
    run_phase_script(world, slot, next);
    if (!initial && a->active) {
        RcPayloadPhaseTransition payload = {
            .npc_id = a->boss_id, .old_phase = old, .new_phase = next,
        };
        rc_event_fire(world, RC_EVT_PHASE_TRANSITION, &payload);
    }
}

int rc_encounter_set_phase(RcWorld *world, int active_idx,
                           const char *phase_id) {
    if (!world || active_idx < 0 || active_idx >= RC_ENC_MAX_ACTIVE
            || !phase_id) return 0;
    RcActiveEncounter *a = &world->encounter.active[active_idx];
    const RcEncounterSpec *spec = run_spec(world, a);
    if (!spec || !a->engaged) return 0;
    for (int i = 0; i < spec->phase_count; i++) {
        if (strcmp(spec->phases[i].id, phase_id) != 0) continue;
        if (i == a->current_phase) return 0;
        enter_phase(world, active_idx, (uint8_t)i, false);
        return 1;
    }
    return 0;
}

// ---- Effects -----------------------------------------------------------

static void release_effect(RcWorld *world, RcEncounterEffect *e) {
    if (!e->active) return;
    e->active = false;
    if (e->object_key) (void)rc_world_object_revert(world, e->object_key);
    e->object_key = 0;
    if (e->run_slot < RC_ENC_MAX_ACTIVE) {
        RcActiveEncounter *a = &world->encounter.active[e->run_slot];
        if (a->active && a->generation == e->run_generation
                && a->effect_count > 0)
            a->effect_count--;
    }
}

static int claim_effect(RcWorld *world, int slot, uint8_t kind) {
    if (!world || kind == RC_ENC_EFFECT_NONE || slot < 0
            || slot >= RC_ENC_MAX_ACTIVE) return -1;
    RcActiveEncounter *a = &world->encounter.active[slot];
    if (!a->active || a->effect_count >= RC_ENC_RUN_EFFECT_QUOTA) return -1;
    for (int i = 0; i < RC_ENC_MAX_EFFECTS; i++) {
        RcEncounterEffect *e = &world->encounter_effects[i];
        if (e->active) continue;
        memset(e, 0, sizeof(*e));
        e->active = true;
        e->kind = kind;
        e->loc_id = -1;
        e->run_slot = (uint8_t)slot;
        e->run_generation = a->generation;
        e->source_uid = a->boss_id;
        a->effect_count++;
        if (i >= world->encounter_effect_count)
            world->encounter_effect_count = i + 1;
        return i;
    }
    return -1;
}

static void copy_name(char *dst, size_t cap, const char *src) {
    size_t n = 0;
    while (src && n + 1 < cap && src[n]) n++;
    if (n) memcpy(dst, src, n);
    dst[n] = '\0';
}

int rc_encounter_add_effect(RcWorld *world, int enc_idx, uint8_t kind,
                            int x, int y, int plane,
                            int target_x, int target_y,
                            int ticks, uint8_t style, uint8_t damage_per_tick,
                            const char *name, const char *target_name) {
    int i = claim_effect(world, enc_idx, kind);
    if (i < 0) return -1;
    RcEncounterEffect *e = &world->encounter_effects[i];
    e->style = style;
    e->damage_per_tick = damage_per_tick;
    e->x = x;
    e->y = y;
    e->plane = plane;
    e->target_x = target_x;
    e->target_y = target_y;
    e->ticks_remaining = ticks;
    copy_name(e->name, sizeof(e->name), name);
    copy_name(e->target_name, sizeof(e->target_name), target_name);
    return i;
}

int rc_encounter_add_strike(RcWorld *world, int enc_idx, int x, int y,
                            int plane, int delay, uint16_t min_damage,
                            uint16_t max_damage, uint8_t adjacent_pct,
                            uint8_t style) {
    if (delay < 0 || min_damage > max_damage || adjacent_pct > 100) return -1;
    int i = claim_effect(world, enc_idx, RC_ENC_EFFECT_TILE_STRIKE);
    if (i < 0) return -1;
    RcEncounterEffect *e = &world->encounter_effects[i];
    e->x = e->target_x = x;
    e->y = e->target_y = y;
    e->plane = plane;
    e->style = style;
    e->impact_tick = world->tick + (RcTick)delay;
    e->min_damage = min_damage;
    e->max_damage = max_damage;
    e->adjacent_pct = adjacent_pct;
    e->ticks_remaining = -1;
    return i;
}

// A landed pool shows its loc unless the tile's layer already holds one;
// the pool works either way.
static void land_pool(RcWorld *world, RcEncounterEffect *e) {
    e->landed = true;
    if (e->loc_id < 0 || !(world->enabled & RC_SUB_OBJECTS)) return;
    RcObjectPlacement loc = {
        .obj_id = (uint32_t)e->loc_id, .x = (uint16_t)e->x,
        .y = (uint16_t)e->y, .plane = (uint8_t)e->plane, .type = e->loc_type,
    };
    RcObjectPlacement base;
    if (rc_object_placement_find_layer(e->x, e->y, e->plane,
            rc_object_layer_for_type(e->loc_type), &base) != 0) return;
    uint64_t key = 0;
    if (rc_world_object_add(world, &loc, 0, &key) == RC_OBJECT_MUTATION_OK)
        e->object_key = key;
}

int rc_encounter_add_pool(RcWorld *world, int enc_idx, int x, int y,
                          int plane, int delay, int ticks, uint8_t damage_max,
                          uint8_t leech_pct, int loc_id, uint8_t loc_type) {
    if (delay < 0 || ticks <= 0 || !damage_max || leech_pct > 100) return -1;
    int i = claim_effect(world, enc_idx, RC_ENC_EFFECT_ACID_POOL);
    if (i < 0) return -1;
    RcEncounterEffect *e = &world->encounter_effects[i];
    e->x = e->target_x = x;
    e->y = e->target_y = y;
    e->plane = plane;
    e->impact_tick = world->tick + (RcTick)delay;
    e->ticks_remaining = ticks;
    e->damage_per_tick = damage_max;
    e->leech_pct = leech_pct;
    e->loc_id = loc_id;
    e->loc_type = loc_type;
    copy_name(e->name, sizeof(e->name), "Pool");
    if (delay == 0) land_pool(world, e);
    return i;
}

int rc_encounter_add_helper_drop(RcWorld *world, int enc_idx, uint8_t helper,
                                 int x, int y, int plane, int delay) {
    if (delay < 0) return 0;
    if (delay == 0)
        return rc_encounter_spawn_helper(world, enc_idx, helper, x, y, plane,
                                         NULL);
    const RcEncounterSpec *spec = enc_idx >= 0 && enc_idx < RC_ENC_MAX_ACTIVE
        ? run_spec(world, &world->encounter.active[enc_idx]) : NULL;
    if (!spec || helper >= spec->helper_count) return 0;
    int i = claim_effect(world, enc_idx, RC_ENC_EFFECT_HELPER_DROP);
    if (i < 0) return 0;
    RcEncounterEffect *e = &world->encounter_effects[i];
    e->x = e->target_x = x;
    e->y = e->target_y = y;
    e->plane = plane;
    e->helper = helper;
    e->impact_tick = world->tick + (RcTick)delay;
    e->ticks_remaining = -1;
    return 1;
}

static bool run_has_drops(const RcWorld *world, int slot) {
    const RcActiveEncounter *a = &world->encounter.active[slot];
    for (int i = 0; i < world->encounter_effect_count; i++) {
        const RcEncounterEffect *e = &world->encounter_effects[i];
        if (e->active && e->kind == RC_ENC_EFFECT_HELPER_DROP
                && e->run_slot == slot && e->run_generation == a->generation)
            return true;
    }
    return false;
}

static void end_window(RcWorld *world, RcActiveEncounter *a);

// A helper-held window (ticks 0) ends once no helper lives or is in flight.
static void release_helper_window(RcWorld *world, int slot) {
    RcActiveEncounter *a = &world->encounter.active[slot];
    if (a->active && a->helper_count == 0
            && a->active_mechanic_idx != RC_ENC_NO_MECHANIC
            && a->active_mechanic_ticks == 0 && !run_has_drops(world, slot))
        end_window(world, a);
}

static void land_drop(RcWorld *world, RcEncounterEffect *e) {
    int slot = e->run_slot;
    const RcActiveEncounter *a = slot < RC_ENC_MAX_ACTIVE
        ? &world->encounter.active[slot] : NULL;
    bool live = a && a->active && a->generation == e->run_generation;
    bool spawned = live && rc_encounter_spawn_helper(world, slot, e->helper,
                                                     e->x, e->y, e->plane, NULL);
    release_effect(world, e);
    if (live && !spawned) release_helper_window(world, slot);
}

// Hazard damage bypasses prayer and never applies attack effects.
static void queue_hazard_hit(RcWorld *world, const RcEncounterEffect *e,
                             int damage, int max_hit) {
    RcPlayer *p = &world->player;
    if (damage <= 0 || p->is_dead || p->current_hp <= 0) return;
    (void)rc_queue_hit_meta(p->pending_hits, &p->num_pending_hits, damage, 0,
                            e->style ? e->style : COMBAT_MAGIC,
                            (int)e->source_uid, 0, world->tick,
                            RC_HIT_SUPPRESS_ENCOUNTER_EFFECTS, max_hit);
}

static void tick_pool(RcWorld *world, RcEncounterEffect *e) {
    const RcPlayer *p = &world->player;
    if (!e->damage_per_tick || p->plane != e->plane || p->x != e->x
            || p->y != e->y) return;
    int damage = 1 + rc_rng_range(&world->rng_state, e->damage_per_tick - 1);
    queue_hazard_hit(world, e, damage, e->damage_per_tick);
    RcNpc *owner = e->leech_pct ? rc_npc_resolve(world, e->source_uid) : NULL;
    if (owner && !owner->is_dead && owner->current_hp > 0) {
        int heal = damage * e->leech_pct / 100;
        int cap = owner->spawn_hp > 0 ? owner->spawn_hp : owner->current_hp;
        owner->current_hp = owner->current_hp + heal > cap
                          ? cap : owner->current_hp + heal;
    }
}

// Warned strikes are judged where the player stands on impact.
static void resolve_strike(RcWorld *world, const RcEncounterEffect *e) {
    const RcPlayer *p = &world->player;
    if (p->plane != e->plane) return;
    int distance = chebyshev(p->x, p->y, e->x, e->y);
    if (distance > 1 || (distance == 1 && !e->adjacent_pct)) return;
    int damage = (int)e->min_damage + rc_rng_range(
        &world->rng_state, (int)e->max_damage - (int)e->min_damage);
    if (distance == 1) damage = damage * e->adjacent_pct / 100;
    queue_hazard_hit(world, e, damage, e->max_damage);
}

void rc_encounter_tick_effects(RcWorld *world) {
    if (!world) return;
    int last_active = -1;
    for (int i = 0; i < world->encounter_effect_count; i++) {
        RcEncounterEffect *e = &world->encounter_effects[i];
        if (!e->active) continue;
        if (e->kind == RC_ENC_EFFECT_TILE_STRIKE) {
            if (world->tick >= e->impact_tick) {
                resolve_strike(world, e);
                release_effect(world, e);
                continue;
            }
        } else if (e->kind == RC_ENC_EFFECT_HELPER_DROP) {
            if (world->tick >= e->impact_tick) {
                land_drop(world, e);
                continue;
            }
        } else if (e->kind == RC_ENC_EFFECT_ACID_POOL) {
            if (!e->landed && world->tick < e->impact_tick) {
                last_active = i;
                continue;
            }
            if (!e->landed) land_pool(world, e);
            tick_pool(world, e);
        }
        if (e->ticks_remaining > 0 && --e->ticks_remaining == 0) {
            release_effect(world, e);
            continue;
        }
        last_active = i;
    }
    world->encounter_effect_count = last_active + 1;
}

// ---- Run lifecycle -----------------------------------------------------

static void end_window(RcWorld *world, RcActiveEncounter *a) {
    if (a->run_flags & RC_ENC_RUN_PLAYER_FROZEN)
        world->player.freeze_expire_tick = world->tick;
    a->run_flags &= (uint8_t)~(RC_ENC_RUN_BOSS_IMMUNE
                               | RC_ENC_RUN_PLAYER_FROZEN
                               | RC_ENC_RUN_BOSS_IDLE);
    a->damage_taken_pct = 100;
    a->active_mechanic_idx = RC_ENC_NO_MECHANIC;
    a->active_mechanic_ticks = 0;
}

void rc_encounter_begin_window(RcWorld *world, int enc_idx,
                               uint8_t mechanic_idx, uint16_t ticks,
                               uint8_t run_flags, uint8_t damage_taken_pct) {
    if (!world || enc_idx < 0 || enc_idx >= RC_ENC_MAX_ACTIVE) return;
    RcActiveEncounter *a = &world->encounter.active[enc_idx];
    const RcEncounterSpec *spec = run_spec(world, a);
    if (!spec || mechanic_idx >= spec->mechanic_count) return;
    end_window(world, a);
    a->active_mechanic_idx = mechanic_idx;
    a->active_mechanic_ticks = ticks;
    a->run_flags |= run_flags;
    a->damage_taken_pct = damage_taken_pct > 100 ? 100 : damage_taken_pct;
    if (run_flags & RC_ENC_RUN_PLAYER_FROZEN)
        rc_player_apply_freeze(world, INT_MAX);
}

static void release_run_effects(RcWorld *world, int slot) {
    const RcActiveEncounter *a = &world->encounter.active[slot];
    for (int i = 0; i < world->encounter_effect_count; i++) {
        RcEncounterEffect *e = &world->encounter_effects[i];
        if (e->active && e->run_slot == slot
                && e->run_generation == a->generation)
            release_effect(world, e);
    }
}

// Helpers leave on the next encounter tick, so a run ended inside an NPC
// lifecycle event never removes NPCs re-entrantly.
static void queue_helper_removals(RcWorld *world, RcActiveEncounter *a) {
    RcEncounterState *s = &world->encounter;
    while (a->helper_count > 0 && s->helper_removal_count < RC_ENC_HELPER_SLOTS)
        s->helper_removals[s->helper_removal_count++] = a->helpers[--a->helper_count];
}

static void flush_helper_removals(RcWorld *world) {
    RcEncounterState *s = &world->encounter;
    while (s->helper_removal_count > 0)
        rc_npc_remove(world, s->helper_removals[--s->helper_removal_count]);
}

static void end_run(RcWorld *world, int slot, bool finished) {
    RcEncounterState *s = &world->encounter;
    RcActiveEncounter *a = &s->active[slot];
    if (!a->active) return;
    RcNpc *boss = run_boss(world, a);
    if (boss && a->transition_until) boss->player_untargetable = false;
    if (boss) apply_form_defence(boss, NULL);
    end_window(world, a);
    release_run_effects(world, slot);
    queue_helper_removals(world, a);
    if (finished) s->finished_count++;
    s->active_mask &= (uint16_t)~(1u << slot);
    memset(a, 0, sizeof(*a));
}

static void stop_boss_combat(RcWorld *world, RcNpc *boss) {
    RcCombatActorRef actor = {RC_COMBAT_ACTOR_NPC, boss->uid};
    rc_combat_stop_actor(world, actor, RC_COMBAT_STATE_CANCELLED);
    boss->target_uid = -1;
    boss->combat.attack_prepared = false;
    if (world->player.attack_target == boss->uid) {
        world->player.attack_target = -1;
        world->player.attack_target_def_id = -1;
    }
}

// Changes the boss definition. Stat drains carry into the new form.
static void record_form(RcWorld *world, const RcNpc *npc, const char *key,
                        int hold_ticks) {
    rc_combat_record_encounter_action(world, npc, false, npc->x, npc->y,
                                      COMBAT_NONE, hold_ticks, key);
}

static void enter_form(RcWorld *world, int slot, uint8_t form,
                       uint8_t hp_policy) {
    RcActiveEncounter *a = &world->encounter.active[slot];
    const RcEncounterSpec *spec = run_spec(world, a);
    RcNpc *npc = spec ? run_boss(world, a) : NULL;
    int def_idx = spec ? form_def_index(spec, form) : -1;
    if (!npc || def_idx < 0) return;
    const RcNpcDef *old_def = rc_npc_def_for_npc(world, npc);
    rc_npc_set_form(npc, def_idx == npc->def_id ? -1 : def_idx);
    apply_form_defence(npc, &spec->forms[form]);
    const RcNpcDef *def = rc_npc_def_for_npc(world, npc);
    if (!def) return;
    for (int i = 0; old_def && i < 6; i++) {
        int drained = old_def->stats[i] - npc->stats[i];
        npc->stats[i] = def->stats[i] - (drained > 0 ? drained : 0);
        if (npc->stats[i] < 0) npc->stats[i] = 0;
    }
    npc->spawn_hp = def->hitpoints > 0 ? def->hitpoints : 1;
    if (hp_policy == RC_ENC_FORM_HP_FULL || npc->current_hp <= 0
            || npc->current_hp > npc->spawn_hp)
        npc->current_hp = npc->spawn_hp;
    npc->player_untargetable = false;
    a->form = form;
    a->def_id = spec->forms[form].npc_id;
    a->form_entered_tick = world->tick;
    a->transition_until = 0;
    a->run_flags &= (uint8_t)~RC_ENC_RUN_BOSS_IDLE;
    if (spec->forms[form].dormant) {
        a->engaged = false;
        stop_boss_combat(world, npc);
        rc_npc_face_spawn_direction(npc);
    }
    if (def != old_def) record_form(world, npc, RC_ENC_FORM_ENTER_KEY, 0);
}

static int bind_run(RcWorld *world, int spec_idx, RcNpc *npc, int npc_slot,
                    uint8_t form) {
    RcEncounterState *s = &world->encounter;
    int slot = -1;
    for (int i = 0; i < RC_ENC_MAX_ACTIVE && slot < 0; i++) {
        if (!s->active[i].active) slot = i;
    }
    if (slot < 0) {
        s->refused_count++;
        return -1;
    }
    RcActiveEncounter *a = &s->active[slot];
    memset(a, 0, sizeof(*a));
    if (++s->next_generation == 0) s->next_generation = 1;
    s->active_mask |= (uint16_t)(1u << slot);
    a->active = true;
    a->generation = s->next_generation;
    a->spec_idx = (uint16_t)spec_idx;
    a->boss_id = (RcNpcId)npc->uid;
    a->boss_slot = npc_slot;
    a->form = form;
    a->def_id = s->specs[spec_idx].forms[form].npc_id;
    a->bound_tick = world->tick;
    a->form_entered_tick = world->tick;
    a->last_attack_idx = RC_ENC_NO_MECHANIC;
    a->active_mechanic_idx = RC_ENC_NO_MECHANIC;
    a->invoking_mechanic_idx = RC_ENC_NO_MECHANIC;
    a->damage_taken_pct = 100;
    s->started_count++;
    apply_form_defence(npc, &s->specs[spec_idx].forms[form]);
    // Guards held since they died return with their boss.
    for (int i = 0; i < world->npc_count; i++) {
        RcNpc *g = &world->npcs[i];
        if (!g->active || !g->is_dead
                || g->respawn_timer < RC_ENC_GUARD_HOLD_TICKS / 2) continue;
        const RcNpcDef *gdef = rc_npc_base_def_for_npc(g);
        if (gdef && guard_spec(world, (uint32_t)gdef->id, g) == spec_idx)
            g->respawn_timer = 1;
    }
    return slot;
}

static void engage_run(RcWorld *world, int slot) {
    RcActiveEncounter *a = &world->encounter.active[slot];
    const RcEncounterSpec *spec = run_spec(world, a);
    if (!spec || a->engaged || a->transition_until || form_dormant(spec, a))
        return;
    a->engaged = true;
    a->engaged_tick = world->tick;
    a->calm_ticks = 0;
    a->attack_count = 0;
    a->attack_special_toggle = 0;
    a->ticks_since_start = 0;
    for (int m = 0; m < spec->mechanic_count; m++) {
        const RcEncounterMechanic *mech = &spec->mechanics[m];
        a->next_due[m] = world->tick + mech->period_ticks;
        if (mech->primitive_id != RC_PRIM_ATTACK_COUNTER_ALTERNATE_SPECIAL)
            continue;
        const RcPrimParamsAttackCounterAlternateSpecial *p =
            (const void *)mech->param_block;
        if (p->random_first && p->special_count > 1)
            a->attack_special_toggle = (uint8_t)rc_rng_bounded(
                &world->rng_state, p->special_count);
    }
    enter_phase(world, slot, 0, true);
}

static void disengage_run(RcWorld *world, int slot) {
    RcActiveEncounter *a = &world->encounter.active[slot];
    const RcEncounterSpec *spec = run_spec(world, a);
    if (!spec || !a->engaged) return;
    end_window(world, a);
    release_run_effects(world, slot);
    queue_helper_removals(world, a);
    a->engaged = false;
    a->calm_ticks = 0;
    a->attack_count = 0;
    a->current_phase = 0;
    a->mechanic_progress = 0;
    if (spec->disengage != RC_ENC_DISENGAGE_RESET) return;
    RcNpc *boss = run_boss(world, a);
    if (a->form != 0 || a->transition_until) {
        enter_form(world, slot, 0, RC_ENC_FORM_HP_FULL);
    } else if (boss && boss->spawn_hp > 0) {
        boss->current_hp = boss->spawn_hp;
    }
}

static bool spec_ready(RcWorld *world, int spec_idx) {
    RcEncounterState *s = &world->encounter;
    const RcEncounterSpec *spec = &s->specs[spec_idx];
    uint64_t bit = 1ull << spec_idx;
    if (s->refused_specs & bit) return false;
    for (int i = 0; i < spec->phase_count; i++) {
        const char *name = spec->phases[i].script;
        if (!name[0] || rc_encounter_script_lookup(world, name)) continue;
        s->refused_specs |= bit;
        s->refused_count++;
        fprintf(stderr, "encounter: %s refused: phase '%s' script '%s' "
                "is not registered\n", spec->slug, spec->phases[i].id, name);
        return false;
    }
    return true;
}

// Spec and form for a live definition id. Only binding forms start a run,
// except an NPC an earlier run left in a later form.
static int spec_form_for(const RcWorld *world, uint32_t npc_id,
                         bool restored, uint8_t *form_out) {
    const RcEncounterState *s = &world->encounter;
    for (int i = 0; s->specs && i < s->spec_count; i++) {
        const RcEncounterSpec *spec = &s->specs[i];
        for (uint8_t f = 0; f < spec->form_count; f++) {
            if (spec->forms[f].npc_id != npc_id) continue;
            if (!spec->forms[f].binds_on_spawn && !restored) return -1;
            if (form_out) *form_out = f;
            return i;
        }
    }
    return -1;
}

static void try_bind(RcWorld *world, int npc_slot) {
    RcNpc *npc = &world->npcs[npc_slot];
    if (!npc->active || npc->is_dead || npc->owner_controlled
            || rc_encounter_run_for_npc(world, (RcNpcId)npc->uid) >= 0
            || run_for_helper(world, (RcNpcId)npc->uid, NULL) >= 0) return;
    const RcNpcDef *def = rc_npc_def_for_npc(world, npc);
    uint8_t form = 0;
    int spec_idx = def ? spec_form_for(world, (uint32_t)def->id,
                                       npc->form_def > 0, &form) : -1;
    if (spec_idx < 0 || !spec_ready(world, spec_idx)) return;
    (void)bind_run(world, spec_idx, npc, npc_slot, form);
}

static int npc_slot_of(const RcWorld *world, RcNpcId uid) {
    for (int i = 0; i < world->npc_count; i++) {
        if (world->npcs[i].active && (RcNpcId)world->npcs[i].uid == uid)
            return i;
    }
    return -1;
}

int rc_encounter_spawn_helper(RcWorld *world, int enc_idx, uint8_t helper,
                              int x, int y, int plane, RcNpcId *out_uid) {
    if (!world || enc_idx < 0 || enc_idx >= RC_ENC_MAX_ACTIVE) return 0;
    RcActiveEncounter *a = &world->encounter.active[enc_idx];
    const RcEncounterSpec *spec = run_spec(world, a);
    if (!spec || helper >= spec->helper_count
            || a->helper_count >= RC_ENC_MAX_RUN_HELPERS) return 0;
    int def_idx = rc_npc_def_find((int)spec->helpers[helper].npc_id);
    if (def_idx < 0) return 0;
    RcNpcSpawnConfig config = {.spawn_key = 0, .wander_range = 0};
    RcNpcSpawnResult result = rc_npc_spawn_ex(world, def_idx, x, y, plane,
                                              &config);
    if (result.status != RC_NPC_SPAWN_CREATED || !a->active) return 0;
    world->npcs[result.slot].owner_controlled = true;
    a->helpers[a->helper_count++] = result.uid;
    if (out_uid) *out_uid = result.uid;
    return 1;
}

static void forget_helper(RcWorld *world, int slot, int helper_at) {
    RcActiveEncounter *a = &world->encounter.active[slot];
    a->helpers[helper_at] = a->helpers[--a->helper_count];
    release_helper_window(world, slot);
}

// ---- Tick --------------------------------------------------------------

static bool player_fighting_boss(const RcWorld *world, const RcNpc *boss) {
    const RcPlayer *p = &world->player;
    if (p->is_dead || p->plane != boss->plane) return false;
    return boss->target_uid == 0 || p->attack_target == boss->uid;
}

static void tick_phase_thresholds(RcWorld *world, int slot,
                                  const RcEncounterSpec *spec,
                                  const RcNpc *boss) {
    RcActiveEncounter *a = &world->encounter.active[slot];
    if (boss->spawn_hp <= 0) return;
    int hp_pct = boss->current_hp * 100 / boss->spawn_hp;
    for (int p = a->current_phase + 1; p < spec->phase_count; p++) {
        uint8_t enter = spec->phases[p].enter_at_hp_pct;
        if (enter < 100 && hp_pct < enter) {
            enter_phase(world, slot, (uint8_t)p, false);
            return;
        }
    }
}

static void tick_window(RcWorld *world, int slot, const RcEncounterSpec *spec) {
    RcActiveEncounter *a = &world->encounter.active[slot];
    if (a->active_mechanic_idx >= spec->mechanic_count) return;
    rc_encounter_prim_window_tick(world, slot,
                                  &spec->mechanics[a->active_mechanic_idx]);
    // Helper-held windows (ticks 0) end in forget_helper.
    if (a->active && a->active_mechanic_ticks > 0
            && --a->active_mechanic_ticks == 0)
        end_window(world, a);
}

static void tick_run(RcWorld *world, int slot) {
    RcActiveEncounter *a = &world->encounter.active[slot];
    const RcEncounterSpec *spec = run_spec(world, a);
    RcNpc *boss = spec ? run_boss(world, a) : NULL;
    if (!boss) {
        end_run(world, slot, false);
        return;
    }
    if (boss->is_dead) return;   // death and teardown are event-owned
    uint8_t hits = a->boss_hits;  // since the last tick; used once engaged
    a->boss_hits = 0;
    if (a->transition_until) {
        boss->current_hp = 0;
        if (world->tick < a->transition_until) return;
        uint8_t next = a->transition_form;
        bool woke = spec->forms[a->form].dormant;
        enter_form(world, slot, next, a->transition_hp);
        if (!spec->forms[next].dormant) engage_run(world, slot);
        // A woken boss turns on the player that woke it.
        if (woke && a->engaged) rc_combat_start_npc_vs_player(world, boss->uid, 0);
        return;
    }
    const RcEncounterForm *form = &spec->forms[a->form];
    if (!a->engaged && form->revert_after_ticks
            && world->tick - a->form_entered_tick >= form->revert_after_ticks) {
        enter_form(world, slot, form->revert_form, form->revert_hp);
        return;
    }
    if (form->dormant) return;
    bool fighting = player_fighting_boss(world, boss);
    if (!a->engaged) {
        if (!fighting) return;
        engage_run(world, slot);
        if (!a->active || !a->engaged) return;
    }
    const RcPlayer *p = &world->player;
    a->calm_ticks = fighting ? 0 : (uint8_t)(a->calm_ticks + 1);
    if (p->is_dead || p->plane != boss->plane
            || footprint_distance(world, boss, p->x, p->y) > RC_ENC_LEASH_TILES
            || a->calm_ticks >= RC_ENC_CALM_TICKS) {
        disengage_run(world, slot);
        return;
    }
    a->ticks_since_start++;
    tick_phase_thresholds(world, slot, spec, boss);
    if (!a->active) return;
    tick_window(world, slot, spec);
    for (int m = 0; m < spec->mechanic_count && a->active && a->engaged; m++) {
        const RcEncounterMechanic *mech = &spec->mechanics[m];
        bool periodic = mech->trigger_type == RC_ENC_TRIGGER_PERIODIC
            || (mech->trigger_type == RC_ENC_TRIGGER_PHASE_IN
                && mech->period_ticks > 0);
        if (!periodic || !mechanic_in_phase(mech, a->current_phase)
                || world->tick < a->next_due[m]) continue;
        a->next_due[m] = world->tick + mech->period_ticks;
        invoke_mechanic(world, slot, mech);
    }
    for (int h = 0; h < hits && a->active && a->engaged; h++) {
        for (int m = 0; m < spec->mechanic_count && a->active && a->engaged;
                m++) {
            const RcEncounterMechanic *mech = &spec->mechanics[m];
            if (mech->trigger_type == RC_ENC_TRIGGER_BOSS_HIT
                    && mechanic_in_phase(mech, a->current_phase))
                invoke_mechanic(world, slot, mech);
        }
    }
}

void rc_encounter_tick(RcWorld *world) {
    if (!world || !(world->enabled & RC_SUB_ENCOUNTER)) return;
    flush_helper_removals(world);
    for (int i = 0; i < RC_ENC_MAX_ACTIVE; i++) {
        if (world->encounter.active_mask & (1u << i)) tick_run(world, i);
    }
}

// ---- Public API --------------------------------------------------------

int rc_encounter_use_specs(RcWorld *world, const RcEncounterSpec *specs,
                           int count) {
    if (!world || count < 0 || count > RC_ENC_REGISTRY_CAP
            || (count > 0 && !specs)) return -1;
    RcEncounterState *s = &world->encounter;
    for (int i = 0; i < RC_ENC_MAX_ACTIVE; i++) end_run(world, i, false);
    flush_helper_removals(world);
    s->specs = count > 0 ? specs : NULL;
    s->spec_count = (uint8_t)count;
    s->refused_specs = 0;
    s->arena_specs = 0;
    s->anchored_count = 0;
    for (int i = 0; i < count; i++) {
        if (specs[i].has_arena) s->arena_specs |= 1ull << i;
        for (int f = 0; f < specs[i].form_count; f++)
            if (specs[i].forms[f].anchored)
                s->anchored_ids[s->anchored_count++] = specs[i].forms[f].npc_id;
    }
    if (!(world->enabled & RC_SUB_ENCOUNTER)) return 0;
    for (int i = 0; i < world->npc_count; i++) try_bind(world, i);
    return 0;
}

int rc_encounter_init(RcWorld *world) {
    if (!world) return -1;
    RcEncounterState *s = &world->encounter;
    memset(s, 0, sizeof(*s));
    static const int events[] = {
        RC_EVT_NPC_SPAWNED, RC_EVT_NPC_RESPAWNED, RC_EVT_NPC_REMOVED,
        RC_EVT_NPC_DIED, RC_EVT_PLAYER_DAMAGED, RC_EVT_NPC_ATTACK,
        RC_EVT_PLAYER_DIED, RC_EVT_NPC_DAMAGED,
    };
    static const RcEventFn handlers[] = {
        rc_encounter_on_npc_spawned, rc_encounter_on_npc_spawned,
        rc_encounter_on_npc_removed, rc_encounter_on_npc_died,
        rc_encounter_on_player_damaged, rc_encounter_on_npc_attack,
        rc_encounter_on_player_died, rc_encounter_on_npc_damaged,
    };
    for (size_t i = 0; i < sizeof(events) / sizeof(events[0]); i++) {
        if (rc_event_subscribe(world, events[i], handlers[i], s) != 0)
            return -1;
    }
    return 0;
}

int rc_encounter_register_script(RcWorld *world, const char *name,
                                 RcEncounterScriptFn fn) {
    if (!world || !name || !name[0] || !fn
            || strlen(name) >= sizeof(world->encounter.scripts[0].name))
        return -1;
    RcEncounterState *s = &world->encounter;
    for (int i = 0; i < s->script_count; i++) {
        if (strcmp(s->scripts[i].name, name) != 0) continue;
        s->scripts[i].fn = fn;
        return i;
    }
    if (s->script_count >= RC_ENC_SCRIPT_REGISTRY_CAP) return -1;
    RcEncounterScriptEntry *entry = &s->scripts[s->script_count];
    memset(entry, 0, sizeof(*entry));
    memcpy(entry->name, name, strlen(name));
    entry->fn = fn;
    return s->script_count++;
}

RcEncounterScriptFn rc_encounter_script_lookup(const RcWorld *world,
                                               const char *name) {
    if (!world || !name || !name[0]) return NULL;
    const RcEncounterState *s = &world->encounter;
    for (int i = 0; i < s->script_count; i++) {
        if (strcmp(s->scripts[i].name, name) == 0) return s->scripts[i].fn;
    }
    return NULL;
}

// ---- Combat integration ------------------------------------------------

static bool player_has_free_slot(const RcPlayer *p) {
    for (int i = 0; i < RC_INVENTORY_SIZE; i++) {
        if (p->inventory[i].item_id < 0) return true;
    }
    return false;
}

static bool style_is_melee_style(uint8_t style) {
    return style == COMBAT_MELEE_STAB || style == COMBAT_MELEE_SLASH
        || style == COMBAT_MELEE_CRUSH;
}

static bool attack_eligible(const RcWorld *world, const RcActiveEncounter *a,
                            const RcEncounterSpec *spec, int idx) {
    const RcEncounterAttack *atk = &spec->attacks[idx];
    if (!(spec->phases[a->current_phase].allowed_attack_mask & (1u << idx)))
        return false;
    if (atk->npc_id && atk->npc_id != a->def_id) return false;
    return !(atk->flags & RC_ENC_ATTACK_NEEDS_FREE_SLOT)
        || player_has_free_slot(&world->player);
}

static const RcEncounterMechanic *special_rotation(const RcEncounterSpec *spec) {
    for (int m = 0; m < spec->mechanic_count; m++) {
        if (spec->mechanics[m].primitive_id
                == RC_PRIM_ATTACK_COUNTER_ALTERNATE_SPECIAL)
            return &spec->mechanics[m];
    }
    return NULL;
}

int rc_encounter_select_npc_attack(RcWorld *world, RcNpcId npc_uid,
                                   int distance, bool side_adjacent,
                                   const RcEncounterAttack **attack,
                                   uint32_t *generation) {
    if (attack) *attack = NULL;
    if (!world || !attack || !generation) return RC_ENC_SELECT_NONE;
    int slot = rc_encounter_run_for_npc(world, npc_uid);
    if (slot < 0) return RC_ENC_SELECT_NONE;
    RcActiveEncounter *a = &world->encounter.active[slot];
    const RcEncounterSpec *spec = run_spec(world, a);
    if (!spec || form_dormant(spec, a) || a->transition_until
            || (a->run_flags & RC_ENC_RUN_BOSS_IDLE))
        return RC_ENC_SELECT_HOLD;
    if (!a->engaged) {
        engage_run(world, slot);
        if (!a->active || !a->engaged) return RC_ENC_SELECT_HOLD;
    }
    const RcEncounterMechanic *rotation = special_rotation(spec);
    if (rotation) {
        const RcPrimParamsAttackCounterAlternateSpecial *p =
            (const void *)rotation->param_block;
        if ((a->attack_count + 1) % p->every_n_attacks == 0) {
            uint8_t pick = p->special_mechanics[a->attack_special_toggle
                                                % p->special_count];
            a->attack_special_toggle =
                (uint8_t)((a->attack_special_toggle + 1) % p->special_count);
            a->attack_count++;
            invoke_mechanic(world, slot, &spec->mechanics[pick]);
            return RC_ENC_SELECT_SPECIAL;
        }
    }
    bool diagonal_reach = false;
    for (int i = 0; i < spec->attack_count; i++) {
        const RcEncounterAttack *atk = &spec->attacks[i];
        diagonal_reach |= (atk->flags & RC_ENC_ATTACK_DIAGONAL_REACH)
                       && style_is_melee_style(atk->style)
                       && attack_eligible(world, a, spec, i);
    }
    bool adjacent = side_adjacent || (diagonal_reach && distance == 1);
    int total = 0, total_adjacent = 0;
    for (int i = 0; i < spec->attack_count; i++) {
        if (!attack_eligible(world, a, spec, i)) continue;
        total += adjacent ? spec->attacks[i].weight_adjacent
                          : spec->attacks[i].weight_distant;
        total_adjacent += spec->attacks[i].weight_adjacent;
    }
    if (total <= 0)
        return !adjacent && total_adjacent > 0 ? RC_ENC_SELECT_APPROACH
                                               : RC_ENC_SELECT_HOLD;
    int pick = (int)rc_rng_bounded(&world->rng_state, (uint32_t)total);
    for (int i = 0; i < spec->attack_count; i++) {
        if (!attack_eligible(world, a, spec, i)) continue;
        pick -= adjacent ? spec->attacks[i].weight_adjacent
                         : spec->attacks[i].weight_distant;
        if (pick >= 0) continue;
        a->last_attack_idx = (uint8_t)i;
        *attack = &spec->attacks[i];
        *generation = a->generation;
        return i;
    }
    return RC_ENC_SELECT_HOLD;
}

const RcEncounterAttack *rc_encounter_attack(const RcWorld *world,
                                             RcNpcId npc_uid, int attack_idx,
                                             uint32_t *generation) {
    int slot = rc_encounter_run_for_npc(world, npc_uid);
    if (slot < 0) return NULL;
    const RcActiveEncounter *a = &world->encounter.active[slot];
    const RcEncounterSpec *spec = run_spec(world, a);
    if (!spec || attack_idx < 0 || attack_idx >= spec->attack_count)
        return NULL;
    if (generation) *generation = a->generation;
    return &spec->attacks[attack_idx];
}

int rc_encounter_player_protection_scale_pct(const RcWorld *world,
                                             int source_uid, int attack,
                                             uint32_t generation,
                                             uint32_t prayer_snapshot) {
    if (!world || source_uid < 0 || attack <= 0) return -1;
    int slot = rc_encounter_run_for_npc(world, (RcNpcId)source_uid);
    if (slot < 0) return -1;
    const RcActiveEncounter *a = &world->encounter.active[slot];
    const RcEncounterSpec *spec = run_spec(world, a);
    if (!spec || a->generation != generation) return -1;
    for (int i = 0; i < spec->protection_count; i++) {
        const RcEncounterProtection *p = &spec->protections[i];
        if (p->attack_idx + 1 == attack && (prayer_snapshot & p->prayer_flag))
            return p->damage_pct;
    }
    return -1;
}

int rc_encounter_scale_player_damage(RcWorld *world, RcNpcId npc_uid,
                                     uint8_t style, int damage) {
    if (!world || damage <= 0) return damage;
    int slot = rc_encounter_run_for_npc(world, npc_uid);
    if (slot < 0) return damage;
    const RcActiveEncounter *a = &world->encounter.active[slot];
    const RcEncounterSpec *spec = run_spec(world, a);
    if (!spec) return damage;
    if (a->run_flags & RC_ENC_RUN_BOSS_IMMUNE) return 0;
    int out = damage;
    for (int i = 0; i < spec->damage_mod_count; i++) {
        const RcEncounterDamageModifier *m = &spec->damage_mods[i];
        if (m->npc_id && m->npc_id != a->def_id) continue;
        bool match = style == m->style;
        if (m->condition == RC_ENC_DMG_STYLE_NOT ? !match : match)
            out = out * m->damage_pct / 100;
    }
    return out * a->damage_taken_pct / 100;
}

bool rc_encounter_player_instakill(const RcWorld *world, RcNpcId npc_uid,
                                   int spell_key) {
    if (!world || spell_key <= 0) return false;
    int slot = run_for_helper(world, npc_uid, NULL);
    const RcNpc *npc = slot >= 0 ? rc_npc_resolve_const(world, npc_uid) : NULL;
    const RcNpcDef *def = npc ? rc_npc_def_for_npc(world, npc) : NULL;
    const RcSpellDef *spell = rc_spell_def_get(spell_key - 1);
    if (!def || !spell) return false;
    const RcEncounterSpec *spec = run_spec(world, &world->encounter.active[slot]);
    for (int m = 0; spec && m < spec->mechanic_count; m++) {
        const RcEncounterMechanic *mech = &spec->mechanics[m];
        if (mech->primitive_id != RC_PRIM_SPAWN_OWNED_HELPERS) continue;
        const RcPrimParamsSpawnOwnedHelpers *p = (const void *)mech->param_block;
        if (spec->helpers[p->helper].npc_id == (uint32_t)def->id
                && p->instakill_spell[0]
                && strcmp(p->instakill_spell, spell->name) == 0)
            return true;
    }
    return false;
}

void rc_encounter_player_attack_committed(RcWorld *world, RcNpcId npc_uid) {
    int slot = world ? rc_encounter_run_for_npc(world, npc_uid) : -1;
    if (slot >= 0) engage_run(world, slot);
}

bool rc_encounter_npc_may_engage(const RcWorld *world, uint32_t def_id,
                                 int spawn_x, int spawn_y, int spawn_plane) {
    if (!world || !(world->enabled & RC_SUB_ENCOUNTER)) return true;
    const RcEncounterState *s = &world->encounter;
    if (!s->arena_specs) return true;
    for (int i = 0; s->specs && i < s->spec_count; i++) {
        if (!(s->arena_specs & (1ull << i))) continue;
        const RcEncounterSpec *spec = &s->specs[i];
        if (!in_arena(spec, spawn_x, spawn_y, spawn_plane)
                || (!spec_has_form(spec, def_id) && !spec_has_guard(spec, def_id)))
            continue;
        const RcPlayer *p = &world->player;
        return in_arena(spec, p->x, p->y, p->plane);
    }
    return true;
}

bool rc_encounter_player_can_target_npc(const RcWorld *world,
                                        RcNpcId npc_uid) {
    int slot = world ? rc_encounter_run_for_npc(world, npc_uid) : -1;
    if (slot < 0) return true;
    const RcActiveEncounter *a = &world->encounter.active[slot];
    const RcEncounterSpec *spec = run_spec(world, a);
    if (!spec || a->transition_until || form_dormant(spec, a)) return false;
    return spec->phases[a->current_phase].player_targetable;
}

bool rc_encounter_npc_dormant(const RcWorld *world, RcNpcId npc_uid) {
    int slot = world ? rc_encounter_run_for_npc(world, npc_uid) : -1;
    if (slot < 0) return false;
    const RcActiveEncounter *a = &world->encounter.active[slot];
    const RcEncounterSpec *spec = run_spec(world, a);
    return spec && form_dormant(spec, a);
}

int rc_encounter_intercept_lethal(RcWorld *world, RcNpcId npc_uid) {
    if (!world || !(world->enabled & RC_SUB_ENCOUNTER)) return 0;
    int slot = rc_encounter_run_for_npc(world, npc_uid);
    if (slot < 0) return 0;
    RcActiveEncounter *a = &world->encounter.active[slot];
    const RcEncounterSpec *spec = run_spec(world, a);
    if (!spec) return 0;
    if (a->transition_until) return 1;
    const RcEncounterForm *form = &spec->forms[a->form];
    RcNpc *npc = run_boss(world, a);
    if (form->on_lethal != RC_ENC_LETHAL_TRANSFORM || !npc) return 0;
    end_window(world, a);
    release_run_effects(world, slot);
    npc->combat.attack_prepared = false;
    if (world->player.attack_target == npc->uid) {
        world->player.attack_target = -1;
        world->player.attack_target_def_id = -1;
    }
    record_form(world, npc, RC_ENC_FORM_LEAVE_KEY, form->transition_ticks);
    if (form->transition_ticks == 0) {
        enter_form(world, slot, form->next_form, form->next_hp);
        return 1;
    }
    npc->current_hp = 0;
    npc->player_untargetable = true;
    a->transition_until = world->tick + form->transition_ticks;
    a->transition_form = form->next_form;
    a->transition_hp = form->next_hp;
    return 1;
}

int rc_encounter_wake(RcWorld *world, RcNpcId npc_uid) {
    int slot = world ? rc_encounter_run_for_npc(world, npc_uid) : -1;
    if (slot < 0) return 0;
    RcActiveEncounter *a = &world->encounter.active[slot];
    const RcEncounterSpec *spec = run_spec(world, a);
    RcNpc *npc = spec ? run_boss(world, a) : NULL;
    if (!npc || npc->is_dead || !form_dormant(spec, a) || a->transition_until)
        return 0;
    const RcEncounterForm *form = &spec->forms[a->form];
    record_form(world, npc, RC_ENC_FORM_LEAVE_KEY, form->transition_ticks);
    if (form->transition_ticks) {
        npc->player_untargetable = true;
        a->transition_until = world->tick + form->transition_ticks;
        a->transition_form = form->next_form;
        a->transition_hp = form->next_hp;
        return 1;
    }
    enter_form(world, slot, form->next_form, form->next_hp);
    engage_run(world, slot);
    rc_combat_start_npc_vs_player(world, npc->uid, 0);
    return 1;
}

static int run_of_member(const RcWorld *world, RcNpcId uid) {
    int slot = rc_encounter_run_for_npc(world, uid);
    return slot >= 0 ? slot : run_for_helper(world, uid, NULL);
}

bool rc_encounter_same_fight(const RcWorld *world, RcNpcId a, RcNpcId b) {
    if (!world || !world->encounter.active_mask) return false;
    int slot = run_of_member(world, a);
    return slot >= 0 && slot == run_of_member(world, b);
}

int rc_encounter_restart(RcWorld *world, RcNpcId npc_uid) {
    if (!world) return 0;
    int slot = rc_encounter_run_for_npc(world, npc_uid);
    if (slot >= 0) end_run(world, slot, false);
    int npc_slot = npc_slot_of(world, npc_uid);
    if (npc_slot < 0 || !(world->enabled & RC_SUB_ENCOUNTER)) return 0;
    try_bind(world, npc_slot);
    return rc_encounter_run_for_npc(world, npc_uid) >= 0;
}

// ---- Attack effects ----------------------------------------------------

static void drain_all_stats(RcPlayer *p, int levels) {
    for (int skill = 0; skill < SKILL_COUNT; skill++) {
        if (skill == SKILL_HITPOINTS) continue;
        p->skills.boosted_level[skill] -= levels;
        if (p->skills.boosted_level[skill] < 0)
            p->skills.boosted_level[skill] = 0;
    }
}

// Moves the player to a random open tile `min_d..max_d` tiles from the
// attacker's footprint; does nothing when no such tile is found.
static void teleport_away(RcWorld *world, const RcNpc *boss, int min_d,
                          int max_d) {
    RcPlayer *p = &world->player;
    if (!boss || max_d <= 0 || min_d > max_d) return;
    for (int tries = 0; tries < 32; tries++) {
        int x = p->x + rc_rng_range(&world->rng_state, max_d * 2) - max_d;
        int y = p->y + rc_rng_range(&world->rng_state, max_d * 2) - max_d;
        int d = footprint_distance(world, boss, x, y);
        if (d < min_d || d > max_d || (x == p->x && y == p->y)
                || !rc_world_tile_valid(x, y, p->plane)
                || rc_tile_blocked(&world->map, x, y, p->plane)) continue;
        (void)rc_world_relocate_player(world, x, y, p->plane);
        return;
    }
}

// Unequips up to `count` items, weapon first. Each needs a free inventory
// slot, so space limits the result.
static void unequip_items(RcWorld *world, int count) {
    RcPlayer *p = &world->player;
    int removed = 0;
    for (int pass = 0; pass < 2; pass++) {
        for (int slot = 0; slot < RC_EQUIP_COUNT && removed < count; slot++) {
            if ((pass == 0) != (slot == EQUIP_WEAPON)
                    || p->equipment[slot].item_id < 0) continue;
            RcItemActionResult r = rc_player_unequip_expected(
                world, slot, p->equipment[slot].generation);
            if (r.code == RC_ITEM_RESULT_OK) removed++;
        }
    }
}

static void apply_attack_effect(RcWorld *world, const RcNpc *boss,
                                const RcEncounterAttack *atk,
                                const RcPayloadPlayerDamaged *hit) {
    bool due = atk->effect_when == RC_ENC_EFFECT_WHEN_ALWAYS
        || (atk->effect_when == RC_ENC_EFFECT_WHEN_ROLLED && hit->rolled > 0)
        || (atk->effect_when == RC_ENC_EFFECT_WHEN_DAMAGED && hit->damage > 0);
    if (!due || atk->effect_id == RC_ENC_ATTACK_EFFECT_NONE) return;
    uint16_t chance = (hit->flags & RC_HIT_BREATH_PROTECTED)
                    ? atk->effect_chance_protected : atk->effect_chance;
    if (chance < 1000 && rc_rng_bounded(&world->rng_state, 1000) >= chance)
        return;
    switch (atk->effect_id) {
        case RC_ENC_ATTACK_EFFECT_POISON:
            rc_player_apply_toxin(world, atk->effect_a, false);
            break;
        case RC_ENC_ATTACK_EFFECT_VENOM:
            rc_player_apply_toxin(world, atk->effect_a, true);
            break;
        case RC_ENC_ATTACK_EFFECT_DEACTIVATE_PRAYERS:
            rc_prayer_disable_all(world, RC_PRAYER_FORCED_OFF);
            break;
        case RC_ENC_ATTACK_EFFECT_DRAIN_PRAYER:
            rc_prayer_drain_points(world, atk->effect_a);
            break;
        case RC_ENC_ATTACK_EFFECT_DRAIN_ALL_STATS:
            drain_all_stats(&world->player, atk->effect_a);
            break;
        case RC_ENC_ATTACK_EFFECT_FREEZE:
            rc_player_apply_freeze(world, atk->effect_b);
            break;
        case RC_ENC_ATTACK_EFFECT_TELEPORT_AWAY:
            teleport_away(world, boss, atk->effect_a, atk->effect_b);
            break;
        case RC_ENC_ATTACK_EFFECT_UNEQUIP:
            unequip_items(world, atk->effect_b);
            break;
        default:
            break;
    }
}

// ---- Events ------------------------------------------------------------

void rc_encounter_on_npc_spawned(RcWorld *world, int evt, const void *payload,
                                 void *ctx) {
    (void)evt;
    (void)ctx;
    const RcPayloadNpcEvent *p = payload;
    if (!world || !p || !(world->enabled & RC_SUB_ENCOUNTER)) return;
    int slot = npc_slot_of(world, p->npc_id);
    if (slot >= 0) try_bind(world, slot);
}

void rc_encounter_on_npc_removed(RcWorld *world, int evt, const void *payload,
                                 void *ctx) {
    (void)evt;
    (void)ctx;
    const RcPayloadNpcEvent *p = payload;
    if (!world || !p) return;
    int slot = rc_encounter_run_for_npc(world, p->npc_id);
    if (slot >= 0) end_run(world, slot, false);
    int helper_at = -1;
    int owner = run_for_helper(world, p->npc_id, &helper_at);
    if (owner >= 0) forget_helper(world, owner, helper_at);
}

void rc_encounter_on_npc_died(RcWorld *world, int evt, const void *payload,
                              void *ctx) {
    (void)evt;
    (void)ctx;
    const RcPayloadNpcEvent *p = payload;
    if (!world || !p) return;
    int helper_at = -1;
    int owner = run_for_helper(world, p->npc_id, &helper_at);
    if (owner >= 0) {
        forget_helper(world, owner, helper_at);
        return;
    }
    if (world->encounter.arena_specs) {
        int npc_slot = npc_slot_of(world, p->npc_id);
        RcNpc *dead = npc_slot >= 0 ? &world->npcs[npc_slot] : NULL;
        if (dead && dead->respawns
                && guard_spec(world, p->def_id, dead) >= 0) {
            dead->respawn_timer = RC_ENC_GUARD_HOLD_TICKS;
            return;
        }
    }
    int slot = rc_encounter_run_for_npc(world, p->npc_id);
    if (slot < 0) return;
    const RcEncounterSpec *spec = run_spec(world, &world->encounter.active[slot]);
    RcNpc *npc = run_boss(world, &world->encounter.active[slot]);
    if (spec && npc && npc->respawns && spec->respawn_ticks)
        npc->respawn_timer = spec->respawn_ticks;
    end_run(world, slot, true);
}

void rc_encounter_on_player_died(RcWorld *world, int evt, const void *payload,
                                 void *ctx) {
    (void)evt;
    (void)payload;
    (void)ctx;
    if (!world) return;
    for (int i = 0; i < RC_ENC_MAX_ACTIVE; i++) disengage_run(world, i);
}

void rc_encounter_on_player_damaged(RcWorld *world, int evt,
                                    const void *payload, void *ctx) {
    (void)evt;
    (void)ctx;
    const RcPayloadPlayerDamaged *p = payload;
    if (!world || !p || p->enc_attack == 0 || p->source_npc_id == UINT32_MAX
            || (p->flags & (RC_HIT_SUPPRESS_ENCOUNTER_EFFECTS
                            | RC_HIT_SOURCE_EXPIRED))) return;
    int slot = rc_encounter_run_for_npc(world, p->source_npc_id);
    if (slot < 0) return;
    RcActiveEncounter *a = &world->encounter.active[slot];
    const RcEncounterSpec *spec = run_spec(world, a);
    if (!spec || a->generation != p->enc_generation
            || p->enc_attack > spec->attack_count) return;
    apply_attack_effect(world, run_boss(world, a),
                        &spec->attacks[p->enc_attack - 1], p);
}

void rc_encounter_on_npc_damaged(RcWorld *world, int evt, const void *payload,
                                 void *ctx) {
    (void)evt;
    (void)ctx;
    const RcPayloadNpcDamaged *p = payload;
    if (!world || !p) return;
    int slot = rc_encounter_run_for_npc(world, p->npc_id);
    if (slot < 0 || !world->encounter.active[slot].engaged) return;
    RcActiveEncounter *a = &world->encounter.active[slot];
    if (a->boss_hits < 0xFF) a->boss_hits++;
}

void rc_encounter_on_npc_attack(RcWorld *world, int evt, const void *payload,
                                void *ctx) {
    (void)evt;
    (void)ctx;
    const RcPayloadNpcAttack *p = payload;
    if (!world || !p) return;
    int slot = rc_encounter_run_for_npc(world, p->npc_id);
    if (slot >= 0 && world->encounter.active[slot].engaged)
        world->encounter.active[slot].attack_count++;
}

// ---- Binary loader (ENCT v14, all-or-fail) -----------------------------

typedef struct {
    const unsigned char *at;
    size_t left;
    const char *error;
} RcEnctReader;

static void fail(RcEnctReader *r, const char *why) {
    if (!r->error) r->error = why;
}

static int take(RcEnctReader *r, void *out, size_t n) {
    if (r->error) return 0;
    if (r->left < n) {
        r->error = "truncated";
        return 0;
    }
    memcpy(out, r->at, n);
    r->at += n;
    r->left -= n;
    return 1;
}

static uint8_t take_u8(RcEnctReader *r) {
    uint8_t v = 0;
    take(r, &v, 1);
    return v;
}

static uint16_t take_u16(RcEnctReader *r) {
    uint16_t v = 0;
    take(r, &v, 2);
    return v;
}

static uint32_t take_u32(RcEnctReader *r) {
    uint32_t v = 0;
    take(r, &v, 4);
    return v;
}

static void take_str(RcEnctReader *r, char *out, size_t cap, bool required) {
    uint8_t len = take_u8(r);
    if (r->error) return;
    if (len >= cap || (required && len == 0)) {
        fail(r, "string length");
        return;
    }
    if (len && !take(r, out, len)) return;
    out[len] = '\0';
    if (strlen(out) != len) fail(r, "embedded NUL");
}

static void read_form(RcEnctReader *r, RcEncounterForm *f) {
    f->npc_id = take_u32(r);
    take_str(r, f->name, sizeof(f->name), true);
    uint8_t binds = take_u8(r), dormant = take_u8(r);
    f->on_lethal = take_u8(r);
    f->next_form = take_u8(r);
    f->next_hp = take_u8(r);
    f->transition_ticks = take_u16(r);
    f->revert_after_ticks = take_u16(r);
    f->revert_form = take_u8(r);
    f->revert_hp = take_u8(r);
    for (int i = 0; i < 5; i++) f->defence_bonus[i] = (int16_t)take_u16(r);
    uint8_t anchored = take_u8(r);
    if (binds > 1 || dormant > 1 || anchored > 1
            || f->on_lethal > RC_ENC_LETHAL_TRANSFORM
            || f->next_hp > RC_ENC_FORM_HP_KEEP
            || f->revert_hp > RC_ENC_FORM_HP_KEEP)
        fail(r, "form field");
    f->binds_on_spawn = binds;
    f->dormant = dormant;
    f->anchored = anchored;
}

static void read_attack(RcEnctReader *r, RcEncounterAttack *a) {
    take_str(r, a->name, sizeof(a->name), true);
    a->npc_id = take_u32(r);
    a->style = take_u8(r);
    a->min_hit = take_u16(r);
    a->max_hit = take_u16(r);
    a->weight_adjacent = take_u8(r);
    a->weight_distant = take_u8(r);
    a->range = take_u8(r);
    a->hit_delay = take_u8(r);
    a->breath = take_u8(r);
    a->adjacent_pct = take_u8(r);
    a->flags = take_u32(r);
    a->effect_id = take_u8(r);
    a->effect_when = take_u8(r);
    a->effect_a = take_u8(r);
    a->effect_b = take_u8(r);
    a->effect_chance = take_u16(r);
    a->effect_chance_protected = take_u16(r);
    const uint32_t known = RC_ENC_ATTACK_ALWAYS_ACCURATE
        | RC_ENC_ATTACK_IGNORES_PROTECTION | RC_ENC_ATTACK_TILE_TARGET
        | RC_ENC_ATTACK_NO_DAMAGE | RC_ENC_ATTACK_NEEDS_FREE_SLOT
        | RC_ENC_ATTACK_DIAGONAL_REACH;
    bool no_damage = (a->flags & RC_ENC_ATTACK_NO_DAMAGE) != 0;
    if (a->style == COMBAT_NONE || a->style > COMBAT_MAGIC
            || a->min_hit > a->max_hit || (a->flags & ~known)
            || no_damage == (a->max_hit > 0)
            || a->effect_id >= RC_ENC_ATTACK_EFFECT_COUNT
            || a->effect_when >= RC_ENC_EFFECT_WHEN_COUNT
            || a->effect_chance > 1000 || a->effect_chance_protected > 1000
            || a->adjacent_pct > 100
            || (a->weight_adjacent == 0 && a->weight_distant == 0)
            || ((a->flags & RC_ENC_ATTACK_TILE_TARGET)
                && (a->breath || a->effect_id || !a->hit_delay))
            || (a->breath && (a->flags & (RC_ENC_ATTACK_ALWAYS_ACCURATE
                                          | RC_ENC_ATTACK_NO_DAMAGE))))
        fail(r, "attack field");
}

static void read_phase(RcEnctReader *r, RcEncounterPhase *p,
                       uint8_t attack_count) {
    take_str(r, p->id, sizeof(p->id), true);
    p->enter_at_hp_pct = take_u8(r);
    p->allowed_attack_mask = take_u32(r);
    take_str(r, p->script, sizeof(p->script), false);
    uint8_t targetable = take_u8(r);
    uint32_t valid = attack_count >= 32 ? 0xFFFFFFFFu
                                        : (1u << attack_count) - 1u;
    if (p->enter_at_hp_pct > 100 || targetable > 1
            || p->allowed_attack_mask == 0
            || (p->allowed_attack_mask & ~valid))
        fail(r, "phase field");
    p->player_targetable = targetable != 0;
}

static bool trigger_supported(uint8_t t) {
    return t == RC_ENC_TRIGGER_PERIODIC || t == RC_ENC_TRIGGER_PHASE_ENTER
        || t == RC_ENC_TRIGGER_PHASE_EXIT || t == RC_ENC_TRIGGER_PHASE_IN
        || t == RC_ENC_TRIGGER_ATTACK_COUNT || t == RC_ENC_TRIGGER_BOSS_HIT;
}

static void read_mechanic(RcEnctReader *r, RcEncounterMechanic *m,
                          uint8_t phase_count) {
    take_str(r, m->name, sizeof(m->name), true);
    m->primitive_id = take_u8(r);
    m->period_ticks = take_u16(r);
    m->trigger_type = take_u8(r);
    m->phase_idx = take_u8(r);
    m->phase_mask = take_u32(r);
    take_str(r, m->trigger_ref, sizeof(m->trigger_ref), false);
    take(r, m->param_block, sizeof(m->param_block));
    if (r->error) return;
    m->prim = rc_encounter_prim_lookup(m->primitive_id);
    uint32_t valid = (1u << phase_count) - 1u;
    bool periodic = m->trigger_type == RC_ENC_TRIGGER_PERIODIC;
    if (!m->prim) fail(r, "unsupported primitive");
    else if (!trigger_supported(m->trigger_type)) fail(r, "unsupported trigger");
    else if ((m->phase_idx != 0xFFu && m->phase_idx >= phase_count)
             || (m->phase_mask & ~valid))
        fail(r, "mechanic phase");
    else if (periodic && m->period_ticks == 0) fail(r, "period-0 periodic");
    else if (!periodic && m->trigger_type != RC_ENC_TRIGGER_PHASE_IN
             && m->period_ticks)
        fail(r, "period on a non-periodic trigger");
}

static bool special_target(const RcEncounterSpec *s, int mech) {
    for (int j = 0; j < s->mechanic_count; j++) {
        const RcEncounterMechanic *m = &s->mechanics[j];
        if (m->primitive_id != RC_PRIM_ATTACK_COUNTER_ALTERNATE_SPECIAL)
            continue;
        const RcPrimParamsAttackCounterAlternateSpecial *p =
            (const void *)m->param_block;
        for (int k = 0; k < p->special_count && k < 2; k++) {
            if (p->special_mechanics[k] == mech) return true;
        }
    }
    return false;
}

static bool cstr_in(const char *s, size_t cap) {
    return memchr(s, '\0', cap) != NULL;
}

// Primitive parameters reference other spec rows; check them once.
static const char *check_params(const RcEncounterSpec *s) {
    int rotations = 0;
    for (int i = 0; i < s->mechanic_count; i++) {
        const RcEncounterMechanic *m = &s->mechanics[i];
        bool rotation = m->primitive_id
                     == RC_PRIM_ATTACK_COUNTER_ALTERNATE_SPECIAL;
        if (m->trigger_type == RC_ENC_TRIGGER_BOSS_HIT
                && m->primitive_id != RC_PRIM_HATCH_HELPERS_ON_HIT)
            return "boss_hit drives hatch_helpers_on_hit only";
        if (m->trigger_type == RC_ENC_TRIGGER_ATTACK_COUNT
                && !rotation && !special_target(s, i))
            return "attack-count mechanic outside a special rotation";
        if (rotation) {
            const RcPrimParamsAttackCounterAlternateSpecial *p =
                (const void *)m->param_block;
            rotations++;
            if (m->trigger_type != RC_ENC_TRIGGER_ATTACK_COUNT
                    || p->every_n_attacks < 2 || p->special_count < 1
                    || p->special_count > 2 || p->random_first > 1)
                return "special rotation";
            for (int k = 0; k < p->special_count; k++) {
                uint8_t t = p->special_mechanics[k];
                if (t >= s->mechanic_count || t == i
                        || s->mechanics[t].trigger_type
                           != RC_ENC_TRIGGER_ATTACK_COUNT
                        || s->mechanics[t].primitive_id
                           == RC_PRIM_ATTACK_COUNTER_ALTERNATE_SPECIAL)
                    return "special rotation target";
            }
        } else if (m->primitive_id == RC_PRIM_ACID_FIELD) {
            const RcPrimParamsAcidField *p = (const void *)m->param_block;
            if (p->pool_count > RC_ENC_RUN_EFFECT_QUOTA / 2
                    || (p->pool_count && (!p->pool_ticks
                                          || !p->pool_damage_max))
                    || p->strike_count == 0 || p->strike_min > p->strike_max
                    || p->pool_radius > RC_ENC_POOL_RADIUS_MAX
                    || p->pool_leech_pct > 100 || p->damage_taken_pct > 100
                    || p->strike_style == COMBAT_NONE
                    || p->strike_style > COMBAT_MAGIC
                    || p->strike_delay + p->pool_count
                       >= RC_ENC_RUN_EFFECT_QUOTA
                    || (p->pool_object != RC_ENC_NO_OBJECT
                        && p->pool_object >= s->object_count))
                return "acid field";
        } else if (m->primitive_id == RC_PRIM_HATCH_HELPERS_ON_HIT) {
            const RcPrimParamsHatchHelpersOnHit *p = (const void *)m->param_block;
            if (m->trigger_type != RC_ENC_TRIGGER_BOSS_HIT
                    || p->helper >= s->helper_count || p->chance_denominator == 0
                    || p->near_loc == 0 || p->near_loc > 0xFFFF
                    || p->near_distance > 32 || p->despawn_ticks == 0)
                return "hatch helpers";
        } else if (m->primitive_id == RC_PRIM_SPAWN_OWNED_HELPERS) {
            const RcPrimParamsSpawnOwnedHelpers *p = (const void *)m->param_block;
            if (p->helper >= s->helper_count || p->count == 0
                    || p->count > RC_ENC_MAX_RUN_HELPERS
                    || p->boss_immune > 1 || p->freeze_player > 1
                    || p->boss_idle > 1 || p->spawn_distance == 0
                    || p->spawn_distance > RC_ENC_SPAWN_DISTANCE_MAX
                    || !cstr_in(p->instakill_spell, sizeof(p->instakill_spell)))
                return "owned helpers";
        }
    }
    return rotations > 1 ? "more than one special rotation" : NULL;
}

static const char *check_spec(const RcEncounterSpec *s) {
    bool binds = false;
    for (int f = 0; f < s->form_count; f++) {
        const RcEncounterForm *form = &s->forms[f];
        binds |= form->binds_on_spawn;
        if (form->npc_id == 0) return "form id";
        bool moves = form->on_lethal == RC_ENC_LETHAL_TRANSFORM || form->dormant;
        if (moves && (form->next_form >= s->form_count || form->next_form == f))
            return "form link";
        if (!moves && (form->next_form || form->transition_ticks))
            return "unused form link";
        if (form->revert_after_ticks
                && (form->revert_form >= s->form_count || form->revert_form == f))
            return "form revert";
        if (form->dormant && form->on_lethal == RC_ENC_LETHAL_TRANSFORM)
            return "dormant form";
        for (int g = 0; g < f; g++) {
            if (s->forms[g].npc_id == form->npc_id) return "duplicate form";
        }
        for (int h = 0; h < s->helper_count; h++) {
            if (s->helpers[h].npc_id == form->npc_id) return "helper is a form";
        }
    }
    if (!binds) return "no binding form";
    if (s->has_arena && (s->arena_min_x > s->arena_max_x
                         || s->arena_min_y > s->arena_max_y
                         || s->arena_plane > 3))
        return "arena";
    if (s->guard_count && !s->has_arena) return "guards without an arena";
    for (int g = 0; g < s->guard_count; g++) {
        uint32_t id = s->guards[g].npc_id;
        if (id == 0 || spec_has_form(s, id)) return "guard id";
        for (int h = 0; h < s->helper_count; h++) {
            if (s->helpers[h].npc_id == id) return "guard id";
        }
        for (int o = 0; o < g; o++) {
            if (s->guards[o].npc_id == id) return "duplicate guard";
        }
    }
    for (int i = 0; i < s->object_count; i++) {
        if (s->objects[i].loc_type > 22) return "object type";
        for (int j = 0; j < i; j++) {
            if (s->objects[j].loc_id == s->objects[i].loc_id)
                return "duplicate object";
        }
    }
    for (int i = 0; i < s->attack_count; i++) {
        if (strchr(s->attacks[i].name, ':')) return "attack name";
    }
    for (int i = 0; i < s->mechanic_count; i++) {
        if (strchr(s->mechanics[i].name, ':')
                || strcmp(s->mechanics[i].name, "form") == 0)
            return "mechanic name";
    }
    for (int i = 0; i < s->attack_count; i++) {
        uint32_t owner = s->attacks[i].npc_id;
        bool known = owner == 0;
        for (int f = 0; f < s->form_count && !known; f++)
            known = s->forms[f].npc_id == owner;
        if (!known) return "attack owner is not a form";
    }
    if (s->support == RC_ENC_SUPPORT_ATTACKS_ONLY
            && (s->form_count != 1 || s->phase_count != 1
                || s->helper_count != 0 || s->mechanic_count != 0
                || s->object_count != 0 || s->guard_count != 0
                || s->has_arena
                || s->phases[0].script[0]
                || s->forms[0].on_lethal != RC_ENC_LETHAL_DIE
                || s->forms[0].dormant || s->forms[0].revert_after_ticks))
        return "attacks-only spec declares more than attacks";
    for (int i = 0; i < s->protection_count; i++) {
        const RcEncounterProtection *p = &s->protections[i];
        if (p->attack_idx >= s->attack_count || p->prayer_flag == 0
                || p->damage_pct > 100)
            return "protection";
    }
    for (int i = 0; i < s->damage_mod_count; i++) {
        const RcEncounterDamageModifier *m = &s->damage_mods[i];
        if ((m->condition != RC_ENC_DMG_STYLE_NOT
                && m->condition != RC_ENC_DMG_STYLE_IS)
                || m->damage_pct > 100 || m->style == COMBAT_NONE
                || m->style > COMBAT_MAGIC)
            return "damage modifier";
    }
    return check_params(s);
}

static void read_spec(RcEnctReader *r, RcEncounterSpec *s) {
    memset(s, 0, sizeof(*s));
    take_str(r, s->slug, sizeof(s->slug), true);
    take_str(r, s->name, sizeof(s->name), true);
    s->support = take_u8(r);
    s->disengage = take_u8(r);
    s->respawn_ticks = take_u16(r);
    uint8_t has_arena = take_u8(r);
    s->arena_min_x = take_u16(r);
    s->arena_min_y = take_u16(r);
    s->arena_max_x = take_u16(r);
    s->arena_max_y = take_u16(r);
    s->arena_plane = take_u8(r);
    if (has_arena > 1) fail(r, "arena flag");
    s->has_arena = has_arena == 1;
    if (!s->has_arena && (s->arena_min_x || s->arena_min_y || s->arena_max_x
                          || s->arena_max_y || s->arena_plane))
        fail(r, "arena bounds without an arena");
    if (s->support != RC_ENC_SUPPORT_FULL
            && s->support != RC_ENC_SUPPORT_ATTACKS_ONLY)
        fail(r, "support level");
    if (s->disengage > RC_ENC_DISENGAGE_RESET) fail(r, "disengage policy");
    s->form_count = take_u8(r);
    if (s->form_count == 0 || s->form_count > RC_ENC_MAX_FORMS)
        fail(r, "form count");
    for (int i = 0; !r->error && i < s->form_count; i++)
        read_form(r, &s->forms[i]);
    s->helper_count = take_u8(r);
    if (s->helper_count > RC_ENC_MAX_HELPERS) fail(r, "helper count");
    for (int i = 0; !r->error && i < s->helper_count; i++) {
        s->helpers[i].npc_id = take_u32(r);
        take_str(r, s->helpers[i].name, sizeof(s->helpers[i].name), true);
    }
    s->object_count = take_u8(r);
    if (s->object_count > RC_ENC_MAX_OBJECTS) fail(r, "object count");
    for (int i = 0; !r->error && i < s->object_count; i++) {
        s->objects[i].loc_id = take_u32(r);
        take_str(r, s->objects[i].name, sizeof(s->objects[i].name), true);
        s->objects[i].loc_type = take_u8(r);
    }
    s->guard_count = take_u8(r);
    if (s->guard_count > RC_ENC_MAX_GUARDS) fail(r, "guard count");
    for (int i = 0; !r->error && i < s->guard_count; i++) {
        s->guards[i].npc_id = take_u32(r);
        take_str(r, s->guards[i].name, sizeof(s->guards[i].name), true);
    }
    s->attack_count = take_u8(r);
    if (s->attack_count == 0 || s->attack_count > RC_ENC_MAX_ATTACKS)
        fail(r, "attack count");
    for (int i = 0; !r->error && i < s->attack_count; i++)
        read_attack(r, &s->attacks[i]);
    s->phase_count = take_u8(r);
    if (s->phase_count == 0 || s->phase_count > RC_ENC_MAX_PHASES)
        fail(r, "phase count");
    for (int i = 0; !r->error && i < s->phase_count; i++)
        read_phase(r, &s->phases[i], s->attack_count);
    s->mechanic_count = take_u8(r);
    if (s->mechanic_count > RC_ENC_MAX_MECHANICS) fail(r, "mechanic count");
    for (int i = 0; !r->error && i < s->mechanic_count; i++)
        read_mechanic(r, &s->mechanics[i], s->phase_count);
    s->protection_count = take_u8(r);
    if (s->protection_count > RC_ENC_MAX_PROTECTIONS)
        fail(r, "protection count");
    for (int i = 0; !r->error && i < s->protection_count; i++) {
        RcEncounterProtection *p = &s->protections[i];
        p->attack_idx = take_u8(r);
        p->style = take_u8(r);
        p->prayer_flag = take_u32(r);
        p->damage_pct = take_u8(r);
    }
    s->damage_mod_count = take_u8(r);
    if (s->damage_mod_count > RC_ENC_MAX_DAMAGE_MODS) fail(r, "modifier count");
    for (int i = 0; !r->error && i < s->damage_mod_count; i++) {
        RcEncounterDamageModifier *m = &s->damage_mods[i];
        m->npc_id = take_u32(r);
        m->condition = take_u8(r);
        m->style = take_u8(r);
        m->damage_pct = take_u8(r);
        m->flags = take_u8(r);
    }
    if (!r->error) {
        const char *why = check_spec(s);
        if (why) fail(r, why);
    }
}

static const char *check_unique(const RcEncounterSpec *out, int i) {
    for (int j = 0; j < i; j++) {
        if (strcmp(out[j].slug, out[i].slug) == 0) return "duplicate slug";
        for (int f = 0; f < out[i].form_count + out[i].helper_count; f++) {
            uint32_t id = f < out[i].form_count
                        ? out[i].forms[f].npc_id
                        : out[i].helpers[f - out[i].form_count].npc_id;
            for (int g = 0; g < out[j].form_count; g++) {
                if (out[j].forms[g].npc_id == id)
                    return "NPC id claimed by two specs";
            }
            for (int h = 0; f < out[i].form_count && h < out[j].helper_count;
                    h++) {
                if (out[j].helpers[h].npc_id == id)
                    return "NPC id claimed by two specs";
            }
        }
        // A guard is room-scoped, but never another spec's boss form.
        for (int g = 0; g < out[i].guard_count; g++) {
            if (spec_has_form(&out[j], out[i].guards[g].npc_id))
                return "NPC id claimed by two specs";
        }
        for (int g = 0; g < out[j].guard_count; g++) {
            if (spec_has_form(&out[i], out[j].guards[g].npc_id))
                return "NPC id claimed by two specs";
        }
    }
    return NULL;
}

int rc_encounter_load_specs(const char *path, RcEncounterSpec *out, int max) {
    if (!path || !out || max <= 0) return -1;
    RcAssetBytes bytes = rc_asset_read_all(path);
    if (!bytes.data) {
        fprintf(stderr, "encounter_load: can't read %s\n", path);
        return -1;
    }
    RcEnctReader r = {bytes.data, bytes.size, NULL};
    uint32_t magic = take_u32(&r);
    uint32_t version = take_u32(&r);
    uint32_t count = take_u32(&r);
    if (!r.error && magic != ENCT_MAGIC) fail(&r, "magic");
    if (!r.error && version != ENCT_VERSION) fail(&r, "version");
    if (!r.error && (count > (uint32_t)max || count > RC_ENC_REGISTRY_CAP))
        fail(&r, "spec count");
    int loaded = 0;
    for (uint32_t i = 0; !r.error && i < count; i++) {
        read_spec(&r, &out[i]);
        if (!r.error) {
            const char *why = check_unique(out, (int)i);
            if (why) fail(&r, why);
        }
        if (!r.error) loaded++;
    }
    if (!r.error && r.left) fail(&r, "trailing bytes");
    rc_asset_bytes_free(&bytes);
    if (r.error) {
        fprintf(stderr, "encounter_load: %s: %s (spec %d)\n", path, r.error,
                loaded);
        memset(out, 0, sizeof(*out) * (size_t)max);
        return -1;
    }
    return loaded;
}

static bool role_named(RcEncounterDefNameFn name_of, const void *ctx,
                       const RcEncounterSpec *s, const char *role,
                       uint32_t id, const char *want, char *message,
                       int message_capacity) {
    const char *have = name_of(ctx, id);
    if (have && strcmp(have, want) == 0) return true;
    if (message && message_capacity > 0) {
        snprintf(message, (size_t)message_capacity,
                 "encounter %s %s %u is '%s', expected '%s'", s->slug, role,
                 id, have ? have : "<missing>", want);
    }
    return false;
}

int rc_encounter_validate_roles(const RcEncounterSpec *specs, int count,
                                RcEncounterDefNameFn npc_name,
                                RcEncounterDefNameFn object_name,
                                const void *ctx, char *message,
                                int message_capacity) {
    if (!npc_name || count < 0 || (count && !specs)) return 0;
    for (int i = 0; i < count; i++) {
        const RcEncounterSpec *s = &specs[i];
        for (int f = 0; f < s->form_count; f++) {
            if (!role_named(npc_name, ctx, s, "form", s->forms[f].npc_id,
                            s->forms[f].name, message, message_capacity))
                return 0;
        }
        for (int h = 0; h < s->helper_count; h++) {
            if (!role_named(npc_name, ctx, s, "helper", s->helpers[h].npc_id,
                            s->helpers[h].name, message, message_capacity))
                return 0;
        }
        for (int g = 0; g < s->guard_count; g++) {
            if (!role_named(npc_name, ctx, s, "guard", s->guards[g].npc_id,
                            s->guards[g].name, message, message_capacity))
                return 0;
        }
        for (int o = 0; object_name && o < s->object_count; o++) {
            if (!role_named(object_name, ctx, s, "object", s->objects[o].loc_id,
                            s->objects[o].name, message, message_capacity))
                return 0;
        }
    }
    return 1;
}
