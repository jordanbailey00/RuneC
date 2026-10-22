// test_encounter_prims — the three admitted encounter primitives on
// synthetic specs (stub NPC definitions, installed spells).
//
// Proves:
//   1. acid_field places its pools (one under the player, distinct open
//      tiles within the radius), opens an idle, damage-scaled window and
//      fires one warned strike per tick at the player's current tile, then
//      restores the boss when the window ends.
//   2. spawn_owned_helpers lands helpers on the ring `spawn_distance` from
//      the player, holds the boss immune and idle and the player frozen while
//      any lives, walks them to the player and explodes them for damage
//      scaled by their remaining health.
//   3. The instakill spell is matched by name for owned helpers only.
//   4. The rotation marker and the window dispatcher do nothing on their own
//      and tolerate invalid runs.
//   5. Delays: spat pools neither hurt nor show a loc until they land; a
//      launched helper lands after spawn_delay, a failed landing releases the
//      window, and drops leave with their run.
//   6. Each part records its presentation event ("<mechanic>[:<part>]",
//      rc_encounter_prim_parts) without taking the slots launches need.

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../rc-core/api.h"
#include "../rc-core/combat.h"
#include "../rc-core/config.h"
#include "../rc-core/encounter.h"
#include "../rc-core/npc.h"
#include "../rc-core/pathfinding.h"
#include "../rc-core/spells.h"
#include "runtime_test_fixture.h"

enum { BOSS = 66001, SPAWN = 66002 };
enum { DEF_BOSS, DEF_SPAWN };

#define PX 3230
#define PY 3230

static RcEncounterSpec g_spec;

static RcWorld *make_world(void) {
    memset(g_npc_defs, 0, sizeof(g_npc_defs));
    g_npc_def_count = 2;
    RcNpcDef *boss = &g_npc_defs[DEF_BOSS];
    boss->id = BOSS;
    snprintf(boss->name, sizeof(boss->name), "Acid boss");
    boss->size = 3;
    boss->hitpoints = 400;
    for (int i = 0; i < 6; i++) boss->stats[i] = 60;
    boss->stats[3] = 400;
    boss->attack_speed = 5;
    boss->max_hit = 10;
    boss->attack_types = 4;
    snprintf(boss->options[1], sizeof(boss->options[1]), "Attack");
    RcNpcDef *spawn = &g_npc_defs[DEF_SPAWN];
    spawn->id = SPAWN;
    snprintf(spawn->name, sizeof(spawn->name), "Spawn");
    spawn->size = 1;
    spawn->hitpoints = 40;
    spawn->stats[3] = 40;
    snprintf(spawn->options[1], sizeof(spawn->options[1]), "Attack");
    RcWorldConfig cfg = rc_preset_base_only();
    cfg.subsystems = RC_SUB_COMBAT | RC_SUB_ENCOUNTER;
    cfg.spells_path = RC_TEST_SOURCE_DIR "/data/defs/spells.bin";
    RcWorld *w = rc_test_world_create_with_defs(&cfg, "encounter_prims", 0);
    assert(w);
    rc_test_open_mapsquare(w, PX, PY, 0);
    w->player.x = PX;
    w->player.y = PY;
    w->player.max_hp = w->player.current_hp = 9900;
    w->player.auto_retaliate = false;
    return w;
}

static void install(RcWorld *w, const RcEncounterMechanic *mech) {
    memset(&g_spec, 0, sizeof(g_spec));
    snprintf(g_spec.slug, sizeof(g_spec.slug), "prims");
    g_spec.support = RC_ENC_SUPPORT_FULL;
    g_spec.form_count = 1;
    g_spec.forms[0] = (RcEncounterForm){.npc_id = BOSS, .name = "Acid boss",
                                        .binds_on_spawn = true};
    g_spec.helper_count = 1;
    g_spec.helpers[0] = (RcEncounterHelper){.npc_id = SPAWN, .name = "Spawn"};
    g_spec.attack_count = 1;
    snprintf(g_spec.attacks[0].name, sizeof(g_spec.attacks[0].name), "Hit");
    g_spec.attacks[0].style = COMBAT_MAGIC;
    g_spec.attacks[0].max_hit = 10;
    g_spec.attacks[0].weight_adjacent = g_spec.attacks[0].weight_distant = 1;
    g_spec.phase_count = 1;
    snprintf(g_spec.phases[0].id, sizeof(g_spec.phases[0].id), "fight");
    g_spec.phases[0].enter_at_hp_pct = 100;
    g_spec.phases[0].allowed_attack_mask = 1;
    g_spec.phases[0].player_targetable = true;
    g_spec.mechanic_count = 1;
    g_spec.mechanics[0] = *mech;
    assert(rc_encounter_use_specs(w, &g_spec, 1) == 0);
}

static RcEncounterMechanic mechanic(uint8_t primitive, const void *params,
                                    size_t size) {
    RcEncounterMechanic m;
    memset(&m, 0, sizeof(m));
    snprintf(m.name, sizeof(m.name), "Mechanic");
    m.primitive_id = primitive;
    m.prim = rc_encounter_prim_lookup(primitive);
    assert(m.prim);
    m.trigger_type = RC_ENC_TRIGGER_ATTACK_COUNT;
    m.phase_idx = 0xFF;
    memcpy(m.param_block, params, size);
    return m;
}

static int engaged_run(RcWorld *w, RcNpc **boss_out) {
    RcNpcSpawnConfig config = {.spawn_key = 0, .wander_range = 0};
    RcNpcSpawnResult r = rc_npc_spawn_ex(w, DEF_BOSS, PX + 3, PY + 3, 0, &config);
    assert(r.status == RC_NPC_SPAWN_CREATED);
    int slot = rc_encounter_run_for_npc(w, r.uid);
    assert(slot >= 0);
    rc_encounter_player_attack_committed(w, r.uid);
    assert(w->encounter.active[slot].engaged);
    if (boss_out) *boss_out = &w->npcs[r.slot];
    return slot;
}

// Runs mechanic 0 the way a special does: through the run's invocation.
static void invoke(RcWorld *w, int slot) {
    RcActiveEncounter *a = &w->encounter.active[slot];
    a->invoking_mechanic_idx = 0;
    g_spec.mechanics[0].prim(w, slot, g_spec.mechanics[0].param_block);
    a->invoking_mechanic_idx = 0xFF;
}

static int count_effects(const RcWorld *w, uint8_t kind) {
    int n = 0;
    for (int i = 0; i < RC_ENC_MAX_EFFECTS; i++) {
        const RcEncounterEffect *e = &w->encounter_effects[i];
        n += e->active && e->kind == kind;
    }
    return n;
}

static void test_acid_field(void) {
    RcWorld *w = make_world();
    RcPrimParamsAcidField p = {
        .pool_count = 6, .pool_radius = 3, .pool_damage_max = 10,
        .pool_leech_pct = 100, .pool_ticks = 12, .strike_count = 5,
        .strike_delay = 2, .strike_min = 7, .strike_max = 7,
        .damage_taken_pct = 50, .strike_style = COMBAT_MAGIC,
    };
    RcEncounterMechanic m = mechanic(RC_PRIM_ACID_FIELD, &p, sizeof(p));
    install(w, &m);
    // A blocked tile next to the player never receives a pool.
    w->map.regions[0].tiles[0][(PX + 1) % 64][PY % 64].collision_flags = COL_BLOCK_WALK;
    RcNpc *boss = NULL;
    int slot = engaged_run(w, &boss);
    RcActiveEncounter *a = &w->encounter.active[slot];
    invoke(w, slot);

    assert(count_effects(w, RC_ENC_EFFECT_ACID_POOL) == 6);
    bool under_player = false;
    for (int i = 0; i < RC_ENC_MAX_EFFECTS; i++) {
        const RcEncounterEffect *e = &w->encounter_effects[i];
        if (!e->active || e->kind != RC_ENC_EFFECT_ACID_POOL) continue;
        under_player |= e->x == PX && e->y == PY;
        assert(!(e->x == PX + 1 && e->y == PY) && "pools land on open tiles");
        assert(e->x >= PX - 3 && e->x <= PX + 3 && e->y >= PY - 3 && e->y <= PY + 3);
        assert(e->ticks_remaining == 12 && e->leech_pct == 100);
        assert(e->damage_per_tick == 10 && e->source_uid == a->boss_id);
        for (int j = i + 1; j < RC_ENC_MAX_EFFECTS; j++) {
            const RcEncounterEffect *f = &w->encounter_effects[j];
            assert(!(f->active && f->kind == RC_ENC_EFFECT_ACID_POOL
                     && f->x == e->x && f->y == e->y) && "one pool per tile");
        }
    }
    assert(under_player && "one pool lands under the player");

    // The window: idle boss, halved damage, one strike per tick.
    assert(a->active_mechanic_idx == 0 && a->active_mechanic_ticks == 5);
    assert(a->run_flags & RC_ENC_RUN_BOSS_IDLE);
    assert(rc_encounter_scale_player_damage(w, boss->uid, COMBAT_RANGED, 20) == 10);
    const RcEncounterAttack *atk = NULL;
    uint32_t generation = 0;
    assert(rc_encounter_select_npc_attack(w, boss->uid, 3, false, &atk,
                                          &generation) == RC_ENC_SELECT_HOLD);
    w->player.x = PX - 2;   // step off the pool; strikes follow the player
    for (int t = 0; t < 5; t++) {
        rc_encounter_tick(w);
        int strikes = 0;
        for (int i = 0; i < RC_ENC_MAX_EFFECTS; i++) {
            const RcEncounterEffect *e = &w->encounter_effects[i];
            if (!e->active || e->kind != RC_ENC_EFFECT_TILE_STRIKE) continue;
            assert(e->min_damage == 7 && e->max_damage == 7 && e->adjacent_pct == 0);
            strikes++;
            if (e->impact_tick == w->tick + 2)
                assert(e->x == w->player.x && e->y == w->player.y);
        }
        // Delay 2: each strike lands two ticks after launch.
        assert(strikes == (t < 2 ? t + 1 : 3));
        rc_encounter_tick_effects(w);
        w->tick++;
        w->player.x += t % 2 ? 1 : -1;
    }
    assert(a->active_mechanic_idx == 0xFF && "the window closes after its strikes");
    assert(!(a->run_flags & RC_ENC_RUN_BOSS_IDLE));
    assert(rc_encounter_scale_player_damage(w, boss->uid, COMBAT_RANGED, 20) == 20);
    // Not invoked through a run: nothing happens.
    RcEncounterEffect before[RC_ENC_MAX_EFFECTS];
    memcpy(before, w->encounter_effects, sizeof(before));
    g_spec.mechanics[0].prim(w, slot, g_spec.mechanics[0].param_block);
    g_spec.mechanics[0].prim(w, -1, g_spec.mechanics[0].param_block);
    assert(!memcmp(before, w->encounter_effects, sizeof(before)));
    rc_world_destroy(w);
}

static RcPrimParamsSpawnOwnedHelpers helper_params(uint8_t explode_max) {
    RcPrimParamsSpawnOwnedHelpers p = {
        .helper = 0, .count = 1, .spawn_distance = 6, .explode_max = explode_max,
        .boss_immune = 1, .freeze_player = 1, .boss_idle = 1,
        .instakill_spell = "Crumble Undead",
    };
    return p;
}

static int chebyshev(int ax, int ay, int bx, int by) {
    int dx = ax > bx ? ax - bx : bx - ax;
    int dy = ay > by ? ay - by : by - ay;
    return dx > dy ? dx : dy;
}

static void test_owned_helpers_explode(void) {
    RcWorld *w = make_world();
    RcPrimParamsSpawnOwnedHelpers p = helper_params(30);
    RcEncounterMechanic m = mechanic(RC_PRIM_SPAWN_OWNED_HELPERS, &p, sizeof(p));
    install(w, &m);
    RcNpc *boss = NULL;
    int slot = engaged_run(w, &boss);
    RcActiveEncounter *a = &w->encounter.active[slot];
    invoke(w, slot);
    assert(a->helper_count == 1);
    RcNpc *helper = rc_npc_resolve(w, a->helpers[0]);
    assert(helper && helper->owner_controlled && !helper->respawns);
    assert(chebyshev(helper->x, helper->y, PX, PY) == 6 && "lands on the ring");
    assert(a->run_flags == (RC_ENC_RUN_BOSS_IMMUNE | RC_ENC_RUN_PLAYER_FROZEN
                            | RC_ENC_RUN_BOSS_IDLE));
    assert(a->active_mechanic_ticks == 0 && "held until the helpers are gone");
    assert(rc_player_is_frozen(w));
    assert(rc_encounter_scale_player_damage(w, boss->uid, COMBAT_MAGIC, 50) == 0);

    // It walks to the player, then explodes for at most its scaled max.
    int start = chebyshev(helper->x, helper->y, PX, PY);
    helper->current_hp = 20;    // half health: at most 15 damage
    int hp = w->player.current_hp;
    int helper_uid = helper->uid, explosions = 0;
    for (int t = 0; t < 20 && a->helper_count; t++) {
        rc_world_tick(w);
        // The explosion is the helper's own part, recorded as it goes.
        int count = 0;
        const RcCombatAttackEvent *events = rc_combat_attack_events(w, &count);
        for (int i = 0; i < count; i++) {
            const RcCombatAttackEvent *e = &events[i];
            if (strcmp(e->action_key_name, "Mechanic:explode") != 0) continue;
            assert(e->action_kind == RC_COMBAT_ACTION_ENCOUNTER);
            assert(e->source_kind == RC_COMBAT_ACTOR_NPC && e->source_uid == helper_uid);
            assert(e->target_kind == RC_COMBAT_ACTOR_PLAYER && e->hit_delay == 0);
            assert(a->helper_count == 0 && "recorded in the tick it explodes");
            explosions++;
        }
    }
    assert(start > 1 && a->helper_count == 0 && "it reached the player and exploded");
    assert(explosions == 1);
    int lost = hp - w->player.current_hp;
    assert(lost >= 0 && lost <= 150);
    assert(a->active_mechanic_idx == 0xFF && a->run_flags == 0);
    assert(!rc_player_is_frozen(w) && "the freeze ends with the window");
    rc_world_destroy(w);
}

static void test_owned_helpers_instakill(void) {
    RcWorld *w = make_world();
    RcPrimParamsSpawnOwnedHelpers p = helper_params(0);   // no seeking
    RcEncounterMechanic m = mechanic(RC_PRIM_SPAWN_OWNED_HELPERS, &p, sizeof(p));
    install(w, &m);
    RcNpc *boss = NULL;
    int slot = engaged_run(w, &boss);
    RcActiveEncounter *a = &w->encounter.active[slot];
    invoke(w, slot);
    RcNpc *helper = rc_npc_resolve(w, a->helpers[0]);
    int hx = helper->x, hy = helper->y;
    for (int t = 0; t < 4; t++) rc_world_tick(w);
    assert(helper->x == hx && helper->y == hy && "explode_max 0 never seeks");

    int crumble = rc_spell_find("Crumble Undead");
    int other = rc_spell_find("Wind Strike");
    assert(crumble >= 0 && other >= 0);
    assert(rc_encounter_player_instakill(w, (RcNpcId)helper->uid, crumble + 1));
    assert(!rc_encounter_player_instakill(w, (RcNpcId)helper->uid, other + 1));
    assert(!rc_encounter_player_instakill(w, (RcNpcId)helper->uid, 0));
    assert(!rc_encounter_player_instakill(w, (RcNpcId)boss->uid, crumble + 1)
           && "only owned helpers die outright");
    rc_npc_remove(w, (RcNpcId)helper->uid);
    assert(a->helper_count == 0 && a->run_flags == 0 && !rc_player_is_frozen(w));
    rc_world_destroy(w);
}

// This tick's encounter events named `key` aimed at `target_kind`.
static int events_named(RcWorld *w, const char *key, int target_kind,
                        int hit_delay) {
    int n = 0, count = 0;
    const RcCombatAttackEvent *events = rc_combat_attack_events(w, &n);
    for (int i = 0; i < n; i++) {
        count += events[i].action_kind == RC_COMBAT_ACTION_ENCOUNTER
              && !strcmp(events[i].action_key_name, key)
              && events[i].target_kind == target_kind
              && events[i].hit_delay == hit_delay;
    }
    return count;
}

static void test_acid_field_delayed(void) {
    RcWorld *w = make_world();
    RcPrimParamsAcidField p = {
        .pool_count = 4, .pool_radius = 2, .pool_damage_max = 10,
        .pool_leech_pct = 100, .pool_ticks = 6, .strike_count = 3,
        .strike_delay = 1, .strike_min = 1, .strike_max = 1,
        .damage_taken_pct = 100, .strike_style = COMBAT_MAGIC,
        .pool_object = 0, .pool_delay = 2,
    };
    RcEncounterMechanic m = mechanic(RC_PRIM_ACID_FIELD, &p, sizeof(p));
    install(w, &m);
    // Specs are read through the run, so the object role applies at once.
    g_spec.object_count = 1;
    g_spec.objects[0] = (RcEncounterObject){.loc_id = 32000, .name = "Acid pool",
                                            .loc_type = 10};
    int slot = engaged_run(w, NULL);
    rc_combat_clear_attack_events(w);
    invoke(w, slot);
    assert(events_named(w, "Mechanic", RC_COMBAT_ACTOR_PLAYER, 0) == 1);
    assert(events_named(w, "Mechanic:pool", RC_COMBAT_ACTOR_NONE, 2) == 4);
    int hp = w->player.current_hp;
    for (int t = 0; t < 2; t++) {
        for (int i = 0; i < RC_ENC_MAX_EFFECTS; i++) {
            const RcEncounterEffect *e = &w->encounter_effects[i];
            if (e->active && e->kind == RC_ENC_EFFECT_ACID_POOL)
                assert(!e->landed && e->ticks_remaining == 6 && e->loc_id == 32000);
        }
        rc_encounter_tick_effects(w);
        assert(w->player.num_pending_hits == 0 && "acid in flight does not hurt");
        w->tick++;
    }
    rc_encounter_tick_effects(w);
    int landed = 0;
    for (int i = 0; i < RC_ENC_MAX_EFFECTS; i++) {
        const RcEncounterEffect *e = &w->encounter_effects[i];
        if (!e->active || e->kind != RC_ENC_EFFECT_ACID_POOL) continue;
        assert(e->landed && e->ticks_remaining == 5);
        assert(!e->object_key && "no object subsystem: the pool works without its loc");
        landed++;
    }
    assert(landed == 4 && w->player.num_pending_hits == 1 && hp == w->player.current_hp);
    // Strikes record their own part, aimed at the player's tile.
    rc_combat_clear_attack_events(w);
    rc_encounter_tick(w);
    assert(events_named(w, "Mechanic:strike", RC_COMBAT_ACTOR_NONE, 1) == 1);
    // Presentation never takes the slots attack launches need.
    rc_combat_clear_attack_events(w);
    w->combat_attack_event_count = RC_MAX_COMBAT_ATTACK_EVENTS - 16;
    rc_encounter_tick(w);
    assert(w->combat_attack_event_count == RC_MAX_COMBAT_ATTACK_EVENTS - 16);
    rc_combat_clear_attack_events(w);
    // A run at its effect quota spits only the pools it can still own.
    RcActiveEncounter *a = &w->encounter.active[slot];
    for (int i = 0; i < RC_ENC_MAX_EFFECTS; i++) w->encounter_effects[i].active = false;
    a->effect_count = RC_ENC_RUN_EFFECT_QUOTA - 2;
    invoke(w, slot);
    assert(count_effects(w, RC_ENC_EFFECT_ACID_POOL) == 2);
    assert(events_named(w, "Mechanic:pool", RC_COMBAT_ACTOR_NONE, 2) == 2);
    a->effect_count = 0;
    rc_combat_clear_attack_events(w);
    assert(rc_encounter_add_pool(w, slot, PX, PY, 0, -1, 5, 5, 0, -1, 0) == -1);
    assert(rc_encounter_add_pool(w, slot, PX, PY, 0, 0, 0, 5, 0, -1, 0) == -1);
    assert(rc_encounter_add_pool(w, slot, PX, PY, 0, 0, 5, 0, 0, -1, 0) == -1);
    assert(rc_encounter_add_pool(w, slot, PX, PY, 0, 0, 5, 5, 101, -1, 0) == -1);
    rc_world_destroy(w);
}

static void test_owned_helpers_delayed(void) {
    RcWorld *w = make_world();
    RcPrimParamsSpawnOwnedHelpers p = helper_params(0);
    p.spawn_delay = 2;
    RcEncounterMechanic m = mechanic(RC_PRIM_SPAWN_OWNED_HELPERS, &p, sizeof(p));
    install(w, &m);
    int slot = engaged_run(w, NULL);
    RcActiveEncounter *a = &w->encounter.active[slot];
    rc_combat_clear_attack_events(w);
    invoke(w, slot);
    assert(events_named(w, "Mechanic", RC_COMBAT_ACTOR_PLAYER, 0) == 1);
    assert(events_named(w, "Mechanic:helper", RC_COMBAT_ACTOR_NONE, 2) == 1);
    assert(a->helper_count == 0 && count_effects(w, RC_ENC_EFFECT_HELPER_DROP) == 1);
    assert(a->run_flags & RC_ENC_RUN_PLAYER_FROZEN && "the window opens at launch");
    rc_encounter_tick_effects(w);
    w->tick++;
    rc_encounter_tick_effects(w);
    assert(a->helper_count == 0);
    w->tick++;
    rc_encounter_tick_effects(w);
    assert(a->helper_count == 1 && !count_effects(w, RC_ENC_EFFECT_HELPER_DROP));
    RcNpc *helper = rc_npc_resolve(w, a->helpers[0]);
    assert(helper && chebyshev(helper->x, helper->y, PX, PY) == 6);
    rc_npc_remove(w, (RcNpcId)helper->uid);
    assert(a->run_flags == 0 && !rc_player_is_frozen(w));

    // A landing that cannot spawn releases a window nothing else holds.
    invoke(w, slot);
    for (int i = 0; i < RC_ENC_MAX_EFFECTS; i++) {
        RcEncounterEffect *e = &w->encounter_effects[i];
        if (e->active && e->kind == RC_ENC_EFFECT_HELPER_DROP) e->x = -100;
    }
    assert(a->run_flags & RC_ENC_RUN_BOSS_IMMUNE);
    w->tick += 2;
    rc_encounter_tick_effects(w);
    assert(a->helper_count == 0 && a->run_flags == 0 && !rc_player_is_frozen(w));

    // A live helper does not end the window while another is in flight.
    assert(rc_encounter_add_helper_drop(w, slot, 0, PX + 6, PY, 0, 0) == 1);
    rc_encounter_begin_window(w, slot, 0, 0, RC_ENC_RUN_BOSS_IMMUNE, 100);
    assert(rc_encounter_add_helper_drop(w, slot, 0, PX - 6, PY, 0, 3) == 1);
    rc_npc_remove(w, a->helpers[0]);
    assert(a->run_flags & RC_ENC_RUN_BOSS_IMMUNE && "held by the helper in flight");
    assert(rc_encounter_add_helper_drop(w, slot, 1, PX, PY + 6, 0, 3) == 0);
    assert(rc_encounter_add_helper_drop(w, slot, 0, PX, PY + 6, 0, -1) == 0);
    // Ending the run drops whatever is still in flight.
    rc_encounter_restart(w, a->boss_id);
    assert(!count_effects(w, RC_ENC_EFFECT_HELPER_DROP));
    rc_world_destroy(w);
}

static void test_parts(void) {
    const char *const *acid = rc_encounter_prim_parts(RC_PRIM_ACID_FIELD);
    assert(acid && !strcmp(acid[0], "") && !strcmp(acid[1], "pool")
           && !strcmp(acid[2], "strike") && !acid[3]);
    const char *const *helpers = rc_encounter_prim_parts(RC_PRIM_SPAWN_OWNED_HELPERS);
    assert(helpers && !strcmp(helpers[0], "") && !strcmp(helpers[1], "helper")
           && !helpers[2]);
    assert(!rc_encounter_prim_parts(RC_PRIM_ATTACK_COUNTER_ALTERNATE_SPECIAL));
    assert(!rc_encounter_prim_parts(RC_PRIM_TELEGRAPHED_AOE_TILE));
}

static void test_marker_and_dispatch(void) {
    RcWorld *w = make_world();
    RcPrimParamsAttackCounterAlternateSpecial rp = {
        .every_n_attacks = 3, .special_count = 1, .special_mechanics = {0},
    };
    RcEncounterMechanic m = mechanic(RC_PRIM_ATTACK_COUNTER_ALTERNATE_SPECIAL,
                                     &rp, sizeof(rp));
    install(w, &m);
    int slot = engaged_run(w, NULL);
    RcActiveEncounter before = w->encounter.active[slot];
    invoke(w, slot);
    rc_encounter_prim_window_tick(w, slot, &g_spec.mechanics[0]);
    rc_encounter_prim_window_tick(w, -1, &g_spec.mechanics[0]);
    rc_encounter_prim_window_tick(w, slot, NULL);
    rc_encounter_prim_window_tick(NULL, slot, &g_spec.mechanics[0]);
    assert(!memcmp(&before, &w->encounter.active[slot], sizeof(before)));
    rc_world_destroy(w);
}

int main(void) {
    test_acid_field();
    test_owned_helpers_explode();
    test_owned_helpers_instakill();
    test_marker_and_dispatch();
    test_acid_field_delayed();
    test_owned_helpers_delayed();
    test_parts();
    printf("test_encounter_prims: ok\n");
    return 0;
}
