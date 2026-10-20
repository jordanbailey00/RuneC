// test_encounter — encounter run ownership and lifecycle on synthetic specs
// installed with rc_encounter_use_specs (stub NPC definitions, no data).
//
// Covers: binding on spawn and respawn (ENC-1), non-binding forms and
// helpers (ENC-2/3), bind versus engage, disengage on calm/leash/death with
// keep and reset policies, per-run deadlines and generations (BE-R003/R004),
// full-width owners (BE-R005), the lethal intercept and transitions
// (BE-R007, ENC-14), revert timers, dormant forms and waking, attack
// selection (weights, adjacency, eligibility, approach), landed-attack
// attribution (ENC-6), warned strikes judged on impact (BE-R010), pools,
// the per-run effect quota (ENC-7), owned helpers and deferred removal
// (BE-R006, BE-R012), the single phase-change owner (ENC-4/5), script
// admission and the content-installer reset contract (ENC-15).

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../rc-core/api.h"
#include "../rc-core/combat.h"
#include "../rc-core/combat_hit.h"
#include "../rc-core/config.h"
#include "../rc-core/encounter.h"
#include "../rc-core/events.h"
#include "../rc-core/npc.h"
#include "../rc-core/prayer.h"
#include "runtime_test_fixture.h"

enum {
    BOSS_A = 65001, BOSS_B = 65002, HELPER = 65003, PLAIN = 12345,
    SLEEPER = 65004, WAKER = 65005, TICKER = 65006,
};
enum { DEF_A, DEF_B, DEF_HELPER, DEF_PLAIN, DEF_SLEEPER, DEF_WAKER, DEF_TICKER };

#define PX 3210
#define PY 3210

static int g_died, g_removed, g_phase_events, g_hits;
static RcPayloadPlayerDamaged g_last_hit;

static void count_event(RcWorld *world, int evt, const void *payload, void *ctx) {
    (void)world;
    (void)payload;
    (void)ctx;
    if (evt == RC_EVT_NPC_DIED) g_died++;
    if (evt == RC_EVT_NPC_REMOVED) g_removed++;
    if (evt == RC_EVT_PHASE_TRANSITION) g_phase_events++;
    if (evt == RC_EVT_PLAYER_DAMAGED) {
        g_last_hit = *(const RcPayloadPlayerDamaged *)payload;
        g_hits++;
    }
}

static void stub_def(int idx, int id, const char *name, int size, int hp,
                     int max_hit) {
    RcNpcDef *d = &g_npc_defs[idx];
    memset(d, 0, sizeof(*d));
    d->id = id;
    snprintf(d->name, sizeof(d->name), "%s", name);
    d->size = size;
    d->hitpoints = hp;
    for (int i = 0; i < 6; i++) d->stats[i] = 50;
    d->stats[3] = hp;
    d->attack_speed = 4;
    d->max_hit = max_hit;
    d->attack_types = 4;
    d->respawn_ticks = 10;
    snprintf(d->options[1], sizeof(d->options[1]), "Attack");
}

static RcWorld *make_world(void) {
    memset(g_npc_defs, 0, sizeof(g_npc_defs));
    g_npc_def_count = 7;
    stub_def(DEF_A, BOSS_A, "Boss form A", 3, 100, 10);
    stub_def(DEF_B, BOSS_B, "Boss form B", 3, 200, 10);
    g_npc_defs[DEF_B].stats[1] = 80;
    stub_def(DEF_HELPER, HELPER, "Helper", 1, 10, 1);
    stub_def(DEF_PLAIN, PLAIN, "Plain", 1, 50, 1);
    stub_def(DEF_SLEEPER, SLEEPER, "Sleeper", 3, 1, 0);
    snprintf(g_npc_defs[DEF_SLEEPER].options[0], 32, "Poke");
    g_npc_defs[DEF_SLEEPER].options[1][0] = '\0';
    stub_def(DEF_WAKER, WAKER, "Waker", 3, 300, 20);
    stub_def(DEF_TICKER, TICKER, "Ticker", 2, 100, 5);
    RcWorldConfig cfg = rc_preset_base_only();
    cfg.subsystems = RC_SUB_COMBAT | RC_SUB_ENCOUNTER | RC_SUB_PRAYER;
    cfg.prayers_path = RC_TEST_SOURCE_DIR "/data/defs/prayers.bin";
    RcWorld *w = rc_test_world_create_with_defs(&cfg, "encounter", 0);
    assert(w);
    rc_test_open_mapsquare(w, PX, PY, 0);
    w->player.x = PX;
    w->player.y = PY;
    w->player.max_hp = w->player.current_hp = 9900;
    w->player.skills.base_level[SKILL_HITPOINTS] = 99;
    w->player.skills.boosted_level[SKILL_HITPOINTS] = 99;
    g_died = g_removed = g_phase_events = 0;
    assert(rc_event_subscribe(w, RC_EVT_NPC_DIED, count_event, NULL) == 0);
    assert(rc_event_subscribe(w, RC_EVT_NPC_REMOVED, count_event, NULL) == 0);
    assert(rc_event_subscribe(w, RC_EVT_PHASE_TRANSITION, count_event, NULL) == 0);
    assert(rc_event_subscribe(w, RC_EVT_PLAYER_DAMAGED, count_event, NULL) == 0);
    g_hits = 0;
    return w;
}

static RcEncounterAttack attack(const char *name, uint8_t style, int adj,
                                int dist) {
    RcEncounterAttack a;
    memset(&a, 0, sizeof(a));
    snprintf(a.name, sizeof(a.name), "%s", name);
    a.style = style;
    a.max_hit = 10;
    a.weight_adjacent = (uint8_t)adj;
    a.weight_distant = (uint8_t)dist;
    return a;
}

static void base_spec(RcEncounterSpec *s, const char *slug) {
    memset(s, 0, sizeof(*s));
    snprintf(s->slug, sizeof(s->slug), "%s", slug);
    s->support = RC_ENC_SUPPORT_FULL;
    s->attack_count = 3;
    s->attacks[0] = attack("Melee", COMBAT_MELEE_CRUSH, 2, 0);
    s->attacks[1] = attack("Ranged", COMBAT_RANGED, 1, 1);
    s->attacks[2] = attack("Magic", COMBAT_MAGIC, 1, 1);
    s->phase_count = 1;
    snprintf(s->phases[0].id, sizeof(s->phases[0].id), "fight");
    s->phases[0].enter_at_hp_pct = 100;
    s->phases[0].allowed_attack_mask = 7;
    s->phases[0].player_targetable = true;
}

static RcEncounterMechanic acid_mechanic(const char *name, uint8_t trigger,
                                         uint16_t period, uint8_t strikes) {
    RcEncounterMechanic m;
    memset(&m, 0, sizeof(m));
    snprintf(m.name, sizeof(m.name), "%s", name);
    m.primitive_id = RC_PRIM_ACID_FIELD;
    m.prim = rc_encounter_prim_lookup(RC_PRIM_ACID_FIELD);
    m.trigger_type = trigger;
    m.period_ticks = period;
    m.phase_idx = 0xFF;
    RcPrimParamsAcidField p = {
        .pool_count = 2, .pool_radius = 2, .pool_damage_max = 5,
        .pool_leech_pct = 100, .pool_ticks = 6, .strike_count = strikes,
        .strike_delay = 2, .strike_min = 3, .strike_max = 3,
        .damage_taken_pct = 50, .strike_style = COMBAT_MAGIC,
    };
    memcpy(m.param_block, &p, sizeof(p));
    return m;
}

static RcEncounterMechanic helper_mechanic(void) {
    RcEncounterMechanic m;
    memset(&m, 0, sizeof(m));
    snprintf(m.name, sizeof(m.name), "Spawn");
    m.primitive_id = RC_PRIM_SPAWN_OWNED_HELPERS;
    m.prim = rc_encounter_prim_lookup(RC_PRIM_SPAWN_OWNED_HELPERS);
    m.trigger_type = RC_ENC_TRIGGER_ATTACK_COUNT;
    m.phase_idx = 0xFF;
    RcPrimParamsSpawnOwnedHelpers p = {
        .helper = 0, .count = 2, .spawn_distance = 4, .explode_max = 0,
        .boss_immune = 1, .freeze_player = 1, .boss_idle = 1,
    };
    memcpy(m.param_block, &p, sizeof(p));
    return m;
}

// 0: transforming boss with a helper; 1: dormant boss with a special
// rotation; 2: periodic acid boss.
static RcEncounterSpec g_specs[3];

static void install_specs(RcWorld *w) {
    RcEncounterSpec *t = &g_specs[0];
    base_spec(t, "transform");
    t->respawn_ticks = 5;
    t->form_count = 2;
    t->forms[0] = (RcEncounterForm){.npc_id = BOSS_A, .name = "Boss form A",
        .binds_on_spawn = true, .on_lethal = RC_ENC_LETHAL_TRANSFORM,
        .next_form = 1, .next_hp = RC_ENC_FORM_HP_FULL, .transition_ticks = 3};
    t->forms[1] = (RcEncounterForm){.npc_id = BOSS_B, .name = "Boss form B",
        .revert_after_ticks = 10, .revert_form = 0,
        .revert_hp = RC_ENC_FORM_HP_KEEP};
    t->helper_count = 1;
    t->helpers[0] = (RcEncounterHelper){.npc_id = HELPER, .name = "Helper"};
    t->attacks[2].effect_id = RC_ENC_ATTACK_EFFECT_DRAIN_PRAYER;
    t->attacks[2].effect_when = RC_ENC_EFFECT_WHEN_ROLLED;
    t->attacks[2].effect_a = 2;
    t->attacks[2].effect_chance = t->attacks[2].effect_chance_protected = 1000;
    t->mechanic_count = 1;
    t->mechanics[0] = helper_mechanic();
    t->mechanics[0].trigger_type = RC_ENC_TRIGGER_PHASE_ENTER;

    RcEncounterSpec *d = &g_specs[1];
    base_spec(d, "dormant");
    d->disengage = RC_ENC_DISENGAGE_RESET;
    d->form_count = 2;
    d->forms[0] = (RcEncounterForm){.npc_id = SLEEPER, .name = "Sleeper",
        .binds_on_spawn = true, .dormant = true, .next_form = 1,
        .next_hp = RC_ENC_FORM_HP_FULL, .transition_ticks = 2};
    d->forms[1] = (RcEncounterForm){.npc_id = WAKER, .name = "Waker"};
    d->helper_count = 1;
    d->helpers[0] = (RcEncounterHelper){.npc_id = HELPER + 100, .name = "Unused"};
    d->mechanic_count = 3;
    RcEncounterMechanic *rot = &d->mechanics[0];
    snprintf(rot->name, sizeof(rot->name), "Rotation");
    rot->primitive_id = RC_PRIM_ATTACK_COUNTER_ALTERNATE_SPECIAL;
    rot->prim = rc_encounter_prim_lookup(rot->primitive_id);
    rot->trigger_type = RC_ENC_TRIGGER_ATTACK_COUNT;
    rot->phase_idx = 0xFF;
    RcPrimParamsAttackCounterAlternateSpecial rp = {
        .every_n_attacks = 3, .special_count = 2, .special_mechanics = {1, 2},
    };
    memcpy(rot->param_block, &rp, sizeof(rp));
    d->mechanics[1] = acid_mechanic("Acid", RC_ENC_TRIGGER_ATTACK_COUNT, 0, 4);
    d->mechanics[2] = acid_mechanic("Acid again", RC_ENC_TRIGGER_ATTACK_COUNT, 0, 1);

    RcEncounterSpec *p = &g_specs[2];
    base_spec(p, "periodic");
    p->form_count = 1;
    p->forms[0] = (RcEncounterForm){.npc_id = TICKER, .name = "Ticker",
        .binds_on_spawn = true};
    p->mechanic_count = 1;
    p->mechanics[0] = acid_mechanic("Acid", RC_ENC_TRIGGER_PERIODIC, 5, 1);
    assert(rc_encounter_use_specs(w, g_specs, 3) == 0);
}

static RcNpc *spawn(RcWorld *w, int def_idx, int x, int y, bool respawns) {
    RcNpcSpawnConfig config = {.spawn_key = 0, .wander_range = 0,
        .flags = respawns ? RC_NPC_SPAWN_RESPAWNS : 0};
    RcNpcSpawnResult r = rc_npc_spawn_ex(w, def_idx, x, y, 0, &config);
    assert(r.status == RC_NPC_SPAWN_CREATED);
    return &w->npcs[r.slot];
}

static RcActiveEncounter *run_of(RcWorld *w, const RcNpc *npc) {
    int slot = rc_encounter_run_for_npc(w, (RcNpcId)npc->uid);
    return slot >= 0 ? &w->encounter.active[slot] : NULL;
}

static void ticks(RcWorld *w, int n) {
    for (int i = 0; i < n; i++) rc_world_tick(w);
}

static void hit_npc(RcWorld *w, RcNpc *npc, int damage) {
    assert(rc_queue_hit_meta(npc->pending_hits, &npc->num_pending_hits, damage,
                             0, COMBAT_MELEE_CRUSH, RC_HIT_SOURCE_PLAYER, 0,
                             w->tick, 0, damage));
}

static void test_binding_and_respawn(void) {
    RcWorld *w = make_world();
    install_specs(w);
    RcNpc *plain = spawn(w, DEF_PLAIN, PX + 5, PY, false);
    RcNpc *form_b = spawn(w, DEF_B, PX + 8, PY, false);
    RcNpc *helper = spawn(w, DEF_HELPER, PX + 12, PY, false);
    assert(!run_of(w, plain) && "unrelated NPCs never bind");
    assert(!run_of(w, form_b) && "only binding forms start a run");
    assert(!run_of(w, helper) && "helper definitions never start a run");
    RcNpc *boss = spawn(w, DEF_A, PX + 2, PY, true);
    RcActiveEncounter *a = run_of(w, boss);
    assert(a && !a->engaged && a->form == 0 && a->def_id == BOSS_A);
    uint32_t first_generation = a->generation;
    assert(w->encounter.started_count == 1);

    // Final death (form B) ends the run and applies the spec respawn time.
    a->form = 1;
    assert(rc_npc_set_form(boss, DEF_B));
    boss->current_hp = 5;
    hit_npc(w, boss, 50);
    ticks(w, 1);
    assert(boss->is_dead && g_died == 1 && !run_of(w, boss));
    assert(w->encounter.finished_count == 1);
    assert(boss->respawn_timer == 5);
    ticks(w, 12);
    assert(!boss->is_dead && boss->form_def == 0 && "each life starts in the base form");
    a = run_of(w, boss);
    assert(a && a->form == 0 && "respawned bosses bind again (ENC-1)");
    assert(a->generation != first_generation);
    rc_world_destroy(w);
}

static void test_use_specs_binds_existing_npcs(void) {
    RcWorld *w = make_world();
    RcNpc *boss = spawn(w, DEF_A, PX + 2, PY, false);
    assert(!run_of(w, boss));
    install_specs(w);
    assert(run_of(w, boss) && "installing specs binds NPCs already spawned");
    assert(rc_encounter_use_specs(w, NULL, 0) == 0);
    assert(!run_of(w, boss) && rc_encounter_spec(w, 0) == NULL);
    assert(rc_encounter_use_specs(w, NULL, 1) == -1);
    assert(rc_encounter_use_specs(w, g_specs, RC_ENC_REGISTRY_CAP + 1) == -1);
    rc_world_destroy(w);
}

static void test_engagement(void) {
    RcWorld *w = make_world();
    install_specs(w);
    RcNpc *boss = spawn(w, DEF_TICKER, PX + 2, PY, false);
    RcActiveEncounter *a = run_of(w, boss);
    ticks(w, 8);
    assert(!a->engaged && a->effect_count == 0 && "bound runs stay quiet");
    w->player.attack_target = boss->uid;
    ticks(w, 1);
    assert(a->engaged);
    // Periodic mechanics first fire one period after engagement.
    RcTick engaged = a->engaged_tick;
    assert(a->next_due[0] == engaged + 5);
    w->player.attack_target = -1;
    boss->target_uid = -1;
    ticks(w, 15);
    assert(a->engaged);
    ticks(w, 1);
    assert(!a->engaged && "16 calm ticks disengage");
    assert(a->effect_count == 0 && "disengage releases hazards");

    boss->target_uid = 0;
    ticks(w, 1);
    assert(a->engaged);
    w->player.x = PX + 40;
    boss->target_uid = -1;
    ticks(w, 1);
    assert(!a->engaged && "leaving the leash disengages");

    w->player.x = PX;
    w->player.attack_target = boss->uid;
    ticks(w, 1);
    assert(a->engaged);
    RcPayloadPlayerDeath death = {0};
    rc_event_fire(w, RC_EVT_PLAYER_DIED, &death);
    assert(!a->engaged && "player death disengages");

    // The reset policy restores health without a form change.
    g_specs[2].disengage = RC_ENC_DISENGAGE_RESET;
    ticks(w, 1);
    assert(a->engaged);
    boss->current_hp = 30;
    rc_event_fire(w, RC_EVT_PLAYER_DIED, &death);
    assert(!a->engaged && boss->current_hp == 100);
    g_specs[2].disengage = RC_ENC_DISENGAGE_KEEP;

    // A boss that vanished without a removal event ends its run.
    boss->active = false;
    ticks(w, 1);
    assert(!a->active && w->encounter.active_mask == 0);
    rc_world_destroy(w);
}

static void test_independent_deadlines_and_slot_reuse(void) {
    RcWorld *w = make_world();
    install_specs(w);
    RcNpc *one = spawn(w, DEF_TICKER, PX + 2, PY + 3, false);
    RcActiveEncounter *a = run_of(w, one);
    rc_encounter_player_attack_committed(w, one->uid);
    ticks(w, 3);
    // Single combat stops a second NPC attacking; engage it directly.
    RcNpc *two = spawn(w, DEF_TICKER, PX + 2, PY - 3, false);
    RcActiveEncounter *b = run_of(w, two);
    rc_encounter_player_attack_committed(w, two->uid);
    assert(a->engaged && b->engaged && a != b);
    assert(a->next_due[0] != b->next_due[0] && "deadlines are per run (BE-R003)");
    assert(g_specs[2].mechanics[0].period_ticks == 5 && "specs stay immutable");
    uint32_t old_generation = b->generation;
    b->attack_count = 9;
    rc_npc_remove(w, (RcNpcId)two->uid);
    assert(!b->active);
    RcNpc *three = spawn(w, DEF_TICKER, PX + 2, PY - 3, false);
    RcActiveEncounter *c = run_of(w, three);
    assert(c == b && c->generation > old_generation);
    assert(c->attack_count == 0 && !c->engaged && c->helper_count == 0
           && "a reused slot starts clean (BE-R004)");
    rc_world_destroy(w);
}

static void test_full_width_owner(void) {
    RcWorld *w = make_world();
    install_specs(w);
    w->next_npc_uid = 70000;
    RcNpc *boss = spawn(w, DEF_TICKER, PX + 2, PY, false);
    int slot = rc_encounter_run_for_npc(w, 70000);
    assert(slot >= 0 && w->encounter.active[slot].boss_id == 70000u);
    int e = rc_encounter_add_effect(w, slot, RC_ENC_EFFECT_ACID_POOL, PX, PY, 0,
                                    PX, PY, 4, COMBAT_NONE, 3, "pool", "");
    assert(e >= 0 && w->encounter_effects[e].source_uid == 70000u
           && "owners are full-width NPC ids (BE-R005)");
    assert(rc_encounter_run_for_npc(w, 70000 & 0xFFFF) < 0);
    (void)boss;
    rc_world_destroy(w);
}

static void test_lethal_transform_and_revert(void) {
    RcWorld *w = make_world();
    install_specs(w);
    RcNpc *boss = spawn(w, DEF_A, PX + 2, PY, true);
    RcActiveEncounter *a = run_of(w, boss);
    w->player.attack_target = boss->uid;
    ticks(w, 1);
    assert(a->engaged);
    boss->stats[1] -= 10;          // a defence drain on the first form
    hit_npc(w, boss, 500);
    ticks(w, 1);
    assert(!boss->is_dead && boss->current_hp == 0 && g_died == 0
           && "an intermediate form never dies (BE-R007)");
    assert(boss->player_untargetable && a->transition_until);
    assert(!rc_encounter_player_can_target_npc(w, boss->uid));
    hit_npc(w, boss, 50);
    ticks(w, 2);
    assert(!boss->is_dead && g_died == 0 && "hits during the transition are absorbed");
    ticks(w, 1);
    const RcNpcDef *def = rc_npc_def_for_npc(w, boss);
    assert(def && def->id == BOSS_B && a->form == 1 && a->def_id == BOSS_B);
    assert(boss->current_hp == 200 && boss->spawn_hp == 200);
    assert(boss->stats[1] == 80 - 10 && "stat drains carry into the next form");
    assert(!boss->player_untargetable && rc_encounter_player_can_target_npc(w, boss->uid));

    // Left alone, the second form reverts and keeps its health.
    boss->current_hp = 150;
    w->player.attack_target = -1;
    boss->target_uid = -1;
    ticks(w, 16);
    assert(!a->engaged && a->form == 1);
    ticks(w, 1);
    assert(a->form == 0 && boss->current_hp == 100 && "kept health is capped by the form");
    rc_world_destroy(w);
}

static void test_dormant_wake_and_reset(void) {
    RcWorld *w = make_world();
    install_specs(w);
    RcNpc *boss = spawn(w, DEF_SLEEPER, PX + 2, PY, false);
    RcActiveEncounter *a = run_of(w, boss);
    const RcEncounterAttack *atk = NULL;
    uint32_t generation = 0;
    assert(a && rc_encounter_select_npc_attack(w, boss->uid, 1, true, &atk,
                                               &generation) == RC_ENC_SELECT_HOLD);
    assert(!rc_encounter_player_can_target_npc(w, boss->uid));
    w->player.attack_target = boss->uid;
    ticks(w, 3);
    assert(!a->engaged && "a dormant form never engages on its own");
    assert(rc_encounter_wake(w, boss->uid));
    assert(!rc_encounter_wake(w, boss->uid) && "waking twice is refused");
    assert(a->transition_until && boss->player_untargetable);
    ticks(w, 2);
    assert(a->form == 0 && "the wake takes its transition ticks");
    ticks(w, 1);
    assert(a->form == 1 && a->engaged && boss->current_hp == 300);
    assert(boss->target_uid == 0 && "the woken boss turns on the player");
    const RcNpcDef *def = rc_npc_def_for_npc(w, boss);
    assert(def && def->id == WAKER);
    boss->current_hp = 120;
    w->player.x = PX + 40;
    w->player.attack_target = boss->uid;
    ticks(w, 1);
    assert(!a->engaged && a->form == 0 && "reset policy returns to the dormant form");
    assert(w->player.attack_target == -1 && "a sleeping boss is not a target");
    def = rc_npc_def_for_npc(w, boss);
    assert(def && def->id == SLEEPER && boss->target_uid == -1);
    assert(rc_encounter_wake(w, boss->uid));
    rc_world_destroy(w);
}

static void test_selection(void) {
    RcWorld *w = make_world();
    install_specs(w);
    RcNpc *boss = spawn(w, DEF_TICKER, PX + 2, PY, false);
    RcActiveEncounter *a = run_of(w, boss);
    const RcEncounterAttack *atk = NULL;
    uint32_t generation = 0;
    int counts[3] = {0};
    for (int i = 0; i < 4000; i++) {
        int idx = rc_encounter_select_npc_attack(w, boss->uid, 1, true, &atk,
                                                 &generation);
        assert(idx >= 0 && idx < 3 && atk == &g_specs[2].attacks[idx]);
        counts[idx]++;
    }
    assert(a->engaged && generation == a->generation && "selection engages");
    assert(counts[0] > 1800 && counts[0] < 2200);
    assert(counts[1] > 850 && counts[1] < 1150);
    for (int i = 0; i < 500; i++)
        assert(rc_encounter_select_npc_attack(w, boss->uid, 5, false, &atk,
                                              &generation) != 0);
    // Only melee allowed: distant selection asks the boss to close in.
    g_specs[2].phases[0].allowed_attack_mask = 1;
    assert(rc_encounter_select_npc_attack(w, boss->uid, 5, false, &atk,
                                          &generation) == RC_ENC_SELECT_APPROACH);
    // Diagonal reach counts a diagonal contact as adjacent.
    g_specs[2].attacks[0].flags = RC_ENC_ATTACK_DIAGONAL_REACH;
    assert(rc_encounter_select_npc_attack(w, boss->uid, 1, false, &atk,
                                          &generation) == 0);
    g_specs[2].attacks[0].flags = RC_ENC_ATTACK_NEEDS_FREE_SLOT;
    for (int i = 0; i < RC_INVENTORY_SIZE; i++) w->player.inventory[i].item_id = 995;
    assert(rc_encounter_select_npc_attack(w, boss->uid, 1, true, &atk,
                                          &generation) == RC_ENC_SELECT_HOLD
           && "space-limited attacks wait for a free slot");
    g_specs[2].attacks[0].flags = 0;
    g_specs[2].attacks[0].npc_id = BOSS_A;
    assert(rc_encounter_select_npc_attack(w, boss->uid, 1, true, &atk,
                                          &generation) == RC_ENC_SELECT_HOLD
           && "form-owned attacks need their form");
    g_specs[2].attacks[0].npc_id = 0;
    g_specs[2].phases[0].allowed_attack_mask = 7;
    a->run_flags |= RC_ENC_RUN_BOSS_IDLE;
    assert(rc_encounter_select_npc_attack(w, boss->uid, 1, true, &atk,
                                          &generation) == RC_ENC_SELECT_HOLD);
    RcNpc *plain = spawn(w, DEF_PLAIN, PX - 3, PY, false);
    assert(rc_encounter_select_npc_attack(w, plain->uid, 1, true, &atk,
                                          &generation) == RC_ENC_SELECT_NONE);
    rc_world_destroy(w);
}

static void test_special_rotation(void) {
    RcWorld *w = make_world();
    install_specs(w);
    RcNpc *boss = spawn(w, DEF_SLEEPER, PX + 2, PY, false);
    RcActiveEncounter *a = run_of(w, boss);
    assert(rc_encounter_wake(w, boss->uid));
    ticks(w, 3);
    assert(a->engaged);
    const RcEncounterAttack *atk = NULL;
    uint32_t generation = 0;
    int specials = 0;
    for (int attack_turn = 1; attack_turn <= 9; attack_turn++) {
        a->run_flags = 0;
        a->active_mechanic_idx = 0xFF;
        int r = rc_encounter_select_npc_attack(w, boss->uid, 1, true, &atk,
                                               &generation);
        if (attack_turn % 3 == 0) {
            assert(r == RC_ENC_SELECT_SPECIAL);
            uint8_t want = specials % 2 == 0 ? 1 : 2;
            assert(a->active_mechanic_idx == want && "specials alternate");
            specials++;
        } else {
            assert(r >= 0);
            RcPayloadNpcAttack launched = {.npc_id = (uint32_t)boss->uid};
            rc_event_fire(w, RC_EVT_NPC_ATTACK, &launched);
        }
    }
    assert(specials == 3 && a->attack_count == 9);
    rc_world_destroy(w);
}

static void test_attack_attribution(void) {
    RcWorld *w = make_world();
    install_specs(w);
    RcNpc *boss = spawn(w, DEF_A, PX + 2, PY, false);
    RcActiveEncounter *a = run_of(w, boss);
    w->player.current_prayer_points = 500;
    RcPayloadPlayerDamaged hit = {
        .source_npc_id = (uint32_t)boss->uid, .damage = 0, .rolled = 7,
        .style = COMBAT_MAGIC, .enc_attack = 3, .enc_generation = a->generation,
    };
    rc_event_fire(w, RC_EVT_PLAYER_DAMAGED, &hit);
    assert(w->player.current_prayer_points == 480
           && "a rolled hit drains even when fully blocked");
    hit.enc_generation = a->generation + 1;
    rc_event_fire(w, RC_EVT_PLAYER_DAMAGED, &hit);
    assert(w->player.current_prayer_points == 480 && "stale generations do nothing");
    hit.enc_generation = a->generation;
    hit.flags = RC_HIT_SUPPRESS_ENCOUNTER_EFFECTS;
    rc_event_fire(w, RC_EVT_PLAYER_DAMAGED, &hit);
    assert(w->player.current_prayer_points == 480 && "hazard hits carry no effects (ENC-6)");
    hit.flags = 0;
    hit.enc_attack = 1;     // melee row has no effect
    rc_event_fire(w, RC_EVT_PLAYER_DAMAGED, &hit);
    hit.enc_attack = 0;     // unattributed hits never borrow the last attack
    rc_event_fire(w, RC_EVT_PLAYER_DAMAGED, &hit);
    assert(w->player.current_prayer_points == 480);
    hit.enc_attack = 3;
    hit.rolled = 0;
    rc_event_fire(w, RC_EVT_PLAYER_DAMAGED, &hit);
    assert(w->player.current_prayer_points == 480 && "a zero roll does not drain");
    g_specs[0].attacks[2].effect_id = RC_ENC_ATTACK_EFFECT_DRAIN_ALL_STATS;
    w->player.skills.boosted_level[SKILL_ATTACK] = 1;
    hit.rolled = 7;
    rc_event_fire(w, RC_EVT_PLAYER_DAMAGED, &hit);
    assert(w->player.skills.boosted_level[SKILL_ATTACK] == 0 && "drains stop at zero");
    g_specs[0].attacks[2].effect_id = RC_ENC_ATTACK_EFFECT_DRAIN_PRAYER;

    // Attack-specific protection uses the hit's attack, not its style.
    g_specs[0].protection_count = 1;
    g_specs[0].protections[0] = (RcEncounterProtection){.attack_idx = 1,
        .style = COMBAT_RANGED, .prayer_flag = PRAYER_PROTECT_RANGE,
        .damage_pct = 40};
    assert(rc_encounter_player_protection_scale_pct(w, boss->uid, 2,
               a->generation, PRAYER_PROTECT_RANGE) == 40);
    assert(rc_encounter_player_protection_scale_pct(w, boss->uid, 3,
               a->generation, PRAYER_PROTECT_RANGE) == -1);
    assert(rc_encounter_player_protection_scale_pct(w, boss->uid, 2,
               a->generation + 1, PRAYER_PROTECT_RANGE) == -1);
    g_specs[0].protection_count = 0;
    rc_world_destroy(w);
}

static void test_strikes_pools_and_quota(void) {
    RcWorld *w = make_world();
    install_specs(w);
    RcNpc *boss = spawn(w, DEF_TICKER, PX + 2, PY, false);
    int slot = rc_encounter_run_for_npc(w, boss->uid);
    RcActiveEncounter *a = &w->encounter.active[slot];
    int hp = w->player.current_hp;
    // Strikes are judged where the player stands on impact (BE-R010).
    assert(rc_encounter_add_strike(w, slot, PX, PY, 0, 2, 10, 10, 50,
                                   COMBAT_MAGIC) >= 0);
    w->player.x = PX + 2;
    ticks(w, 3);
    assert(w->player.current_hp == hp && "stepping away avoids the strike");
    assert(rc_encounter_add_strike(w, slot, PX + 1, PY, 0, 1, 10, 10, 50,
                                   COMBAT_MAGIC) >= 0);
    ticks(w, 2);
    assert(w->player.current_hp == hp - 50 && "half damage beside the impact");
    hp = w->player.current_hp;
    assert(rc_encounter_add_strike(w, slot, PX + 2, PY, 0, 1, 10, 10, 0,
                                   COMBAT_MAGIC) >= 0);
    ticks(w, 2);
    assert(w->player.current_hp == hp - 100 && "full damage on the tile");
    assert(rc_encounter_add_strike(w, slot, PX, PY, 0, -1, 1, 1, 0, COMBAT_MAGIC) < 0);
    assert(rc_encounter_add_strike(w, slot, PX, PY, 0, 1, 5, 1, 0, COMBAT_MAGIC) < 0);

    // Pools hurt the player standing on them and heal their owner.
    boss->current_hp = 50;
    hp = w->player.current_hp;
    int e = rc_encounter_add_effect(w, slot, RC_ENC_EFFECT_ACID_POOL,
                                    PX + 2, PY, 0, PX + 2, PY, 3, COMBAT_NONE,
                                    4, "pool", "");
    w->encounter_effects[e].leech_pct = 100;
    ticks(w, 1);
    int lost = (hp - w->player.current_hp) / 10;
    assert(lost >= 1 && lost <= 4 && boss->current_hp == 50 + lost);
    ticks(w, 3);
    assert(!w->encounter_effects[e].active && "pools expire");

    // The per-run quota bounds hazards (ENC-7); ending the run frees them.
    int claimed = 0;
    for (int i = 0; i < 40; i++) {
        if (rc_encounter_add_effect(w, slot, RC_ENC_EFFECT_ROOM_ATTACK, PX, PY, 0,
                                    PX, PY, -1, COMBAT_NONE, 0, "x", "") >= 0)
            claimed++;
    }
    assert(claimed == RC_ENC_RUN_EFFECT_QUOTA && a->effect_count == claimed);
    RcNpc *other = spawn(w, DEF_TICKER, PX - 6, PY, false);
    int other_slot = rc_encounter_run_for_npc(w, other->uid);
    assert(rc_encounter_add_effect(w, other_slot, RC_ENC_EFFECT_ROOM_ATTACK, PX,
                                   PY, 0, PX, PY, -1, COMBAT_NONE, 0, "y", "") >= 0);
    rc_npc_remove(w, (RcNpcId)boss->uid);
    int live = 0;
    for (int i = 0; i < RC_ENC_MAX_EFFECTS; i++)
        live += w->encounter_effects[i].active;
    assert(live == 1 && "a removed boss leaves no hazards behind");
    rc_world_destroy(w);
}

static void test_owned_helpers(void) {
    RcWorld *w = make_world();
    install_specs(w);
    RcNpc *boss = spawn(w, DEF_A, PX + 2, PY, false);
    RcActiveEncounter *a = run_of(w, boss);
    w->player.attack_target = boss->uid;
    ticks(w, 1);
    // Phase 0 entry spawned two owned helpers and opened the window.
    assert(a->engaged && a->helper_count == 2);
    assert(a->run_flags & RC_ENC_RUN_BOSS_IMMUNE);
    assert(rc_player_is_frozen(w));
    assert(rc_encounter_scale_player_damage(w, boss->uid, COMBAT_MELEE_CRUSH, 9) == 0);
    for (int h = 0; h < a->helper_count; h++) {
        RcNpc *helper = rc_npc_resolve(w, a->helpers[h]);
        assert(helper && helper->owner_controlled && !helper->respawns);
        assert(!run_of(w, helper) && "helpers never start runs");
    }
    // Killing both helpers ends the window: no freeze, no immunity.
    RcNpc *h0 = rc_npc_resolve(w, a->helpers[0]);
    RcNpc *h1 = rc_npc_resolve(w, a->helpers[1]);
    hit_npc(w, h0, 50);
    ticks(w, 1);
    assert(a->helper_count == 1 && (a->run_flags & RC_ENC_RUN_BOSS_IMMUNE));
    hit_npc(w, h1, 50);
    ticks(w, 1);
    assert(a->helper_count == 0 && !(a->run_flags & RC_ENC_RUN_BOSS_IMMUNE));
    assert(!rc_player_is_frozen(w));
    assert(rc_encounter_scale_player_damage(w, boss->uid, COMBAT_MELEE_CRUSH, 9) == 9);
    ticks(w, 4);   // dead helpers leave after their death animation
    assert(!rc_npc_resolve(w, (RcNpcId)h0->uid) || !h0->active);

    // Helpers outliving their run leave on the next encounter tick, with
    // their own removal event (BE-R006, BE-R012).
    w->player.attack_target = -1;
    boss->target_uid = -1;
    RcPayloadPlayerDeath death = {0};
    rc_event_fire(w, RC_EVT_PLAYER_DIED, &death);
    w->player.attack_target = boss->uid;
    ticks(w, 1);
    assert(a->engaged && a->helper_count == 2);
    RcNpcId left0 = a->helpers[0], left1 = a->helpers[1];
    int removed_before = g_removed;
    rc_npc_remove(w, (RcNpcId)boss->uid);
    assert(rc_npc_resolve(w, left0) && "removal is deferred out of the event");
    ticks(w, 1);
    assert(!rc_npc_resolve(w, left0) && !rc_npc_resolve(w, left1));
    assert(g_removed == removed_before + 3);
    assert(!rc_player_is_frozen(w) && "the run's freeze ends with it");
    rc_world_destroy(w);
}

static void test_phase_owner(void) {
    RcWorld *w = make_world();
    install_specs(w);
    RcEncounterSpec *p = &g_specs[2];
    p->phase_count = 2;
    p->phases[1] = p->phases[0];
    snprintf(p->phases[1].id, sizeof(p->phases[1].id), "enraged");
    p->phases[1].enter_at_hp_pct = 50;
    p->mechanic_count = 1;
    p->mechanics[0] = acid_mechanic("Enrage acid", RC_ENC_TRIGGER_PHASE_ENTER, 0, 2);
    p->mechanics[0].phase_mask = 1u << 1;
    RcNpc *boss = spawn(w, DEF_TICKER, PX + 2, PY, false);
    RcActiveEncounter *a = run_of(w, boss);
    w->player.attack_target = boss->uid;
    ticks(w, 1);
    assert(a->engaged && a->current_phase == 0 && a->active_mechanic_idx == 0xFF);
    boss->current_hp = 40;
    ticks(w, 1);
    assert(a->current_phase == 1 && g_phase_events == 1);
    assert(a->effect_count >= 2 && "phase-enter mechanics run through the owner (ENC-5)");
    assert(rc_encounter_set_phase(w, (int)(a - w->encounter.active), "fight") == 1);
    assert(a->current_phase == 0 && g_phase_events == 2);
    assert(rc_encounter_set_phase(w, (int)(a - w->encounter.active), "fight") == 0);
    assert(rc_encounter_set_phase(w, (int)(a - w->encounter.active), "nope") == 0);
    rc_world_destroy(w);
}

static int g_script_calls;
static void phase_script(RcWorld *world, int enc_idx) {
    (void)world;
    (void)enc_idx;
    g_script_calls++;
}

static int install_scripts(RcWorld *world) {
    return rc_encounter_register_script(world, "test_phase", phase_script) >= 0;
}

static void test_script_admission_and_reset(void) {
    RcWorld *w = make_world();
    install_specs(w);
    snprintf(g_specs[2].phases[0].script, sizeof(g_specs[2].phases[0].script),
             "test_phase");
    RcNpc *boss = spawn(w, DEF_TICKER, PX + 2, PY, false);
    assert(!run_of(w, boss) && "a spec with an unregistered script is refused");
    assert(w->encounter.refused_specs & (1u << 2));
    assert(rc_world_install_content(w, install_scripts));
    assert(rc_encounter_use_specs(w, g_specs, 3) == 0);
    assert(run_of(w, boss));
    g_script_calls = 0;
    w->player.attack_target = boss->uid;
    ticks(w, 1);
    assert(g_script_calls == 1 && w->encounter.scripts_called == 1);
    // Reset re-runs the content installer (ENC-15).
    assert(rc_world_reset(w));
    assert(rc_encounter_script_lookup(w, "test_phase") == phase_script);
    g_specs[2].phases[0].script[0] = '\0';
    rc_world_destroy(w);
}


static void test_capacity_and_restart(void) {
    RcWorld *w = make_world();
    install_specs(w);
    RcNpc *bosses[RC_ENC_MAX_ACTIVE + 1];
    for (int i = 0; i <= RC_ENC_MAX_ACTIVE; i++)
        bosses[i] = spawn(w, DEF_TICKER, PX - 30 + (i % 8) * 4, PY - 20 + (i / 8) * 4,
                          false);
    assert(w->encounter.active_mask == 0xFFFF);
    assert(!run_of(w, bosses[RC_ENC_MAX_ACTIVE]) && w->encounter.refused_count == 1
           && "a seventeenth run is refused, not overwritten");
    // The world hazard pool caps two full runs; a third run claims nothing.
    for (int r = 0; r < 3; r++) {
        int slot = rc_encounter_run_for_npc(w, bosses[r]->uid);
        int claimed = 0;
        for (int i = 0; i < RC_ENC_RUN_EFFECT_QUOTA; i++) {
            claimed += rc_encounter_add_effect(w, slot, RC_ENC_EFFECT_ROOM_ATTACK,
                                               PX, PY, 0, PX, PY, -1,
                                               COMBAT_NONE, 0, "x", "") >= 0;
        }
        assert(claimed == (r < 2 ? RC_ENC_RUN_EFFECT_QUOTA : 0));
    }
    assert(rc_encounter_add_effect(w, -1, RC_ENC_EFFECT_ROOM_ATTACK, PX, PY, 0,
                                   PX, PY, -1, COMBAT_NONE, 0, "x", "") < 0);
    // An NPC_SPAWNED event for a missing NPC binds nothing.
    RcPayloadNpcEvent ghost = {.npc_id = 999999};
    rc_event_fire(w, RC_EVT_NPC_SPAWNED, &ghost);
    assert(w->encounter.started_count == RC_ENC_MAX_ACTIVE);

    // Restarting a bound boss starts a fresh run in its first form.
    rc_npc_remove(w, (RcNpcId)bosses[0]->uid);
    RcNpc *boss = spawn(w, DEF_A, PX + 2, PY, false);
    RcActiveEncounter *a = run_of(w, boss);
    uint32_t generation = a->generation;
    a->form = 1;
    assert(rc_encounter_restart(w, boss->uid));
    a = run_of(w, boss);
    assert(a && a->form == 0 && a->generation != generation);
    assert(!rc_encounter_restart(w, 424242));
    rc_world_destroy(w);
}

static void test_immediate_transitions_and_modifiers(void) {
    RcWorld *w = make_world();
    install_specs(w);
    g_specs[0].forms[0].transition_ticks = 0;
    g_specs[1].forms[0].transition_ticks = 0;
    g_specs[2].damage_mod_count = 2;
    g_specs[2].damage_mods[0] = (RcEncounterDamageModifier){.npc_id = TICKER,
        .condition = RC_ENC_DMG_STYLE_NOT, .style = COMBAT_MAGIC, .damage_pct = 50};
    g_specs[2].damage_mods[1] = (RcEncounterDamageModifier){.npc_id = BOSS_A,
        .condition = RC_ENC_DMG_STYLE_IS, .style = COMBAT_RANGED, .damage_pct = 0};

    RcNpc *boss = spawn(w, DEF_A, PX + 2, PY, false);
    RcActiveEncounter *a = run_of(w, boss);
    hit_npc(w, boss, 500);
    ticks(w, 1);
    assert(!boss->is_dead && a->form == 1 && boss->current_hp == 200
           && !a->transition_until && "a zero-tick transform is immediate");

    RcNpc *sleeper = spawn(w, DEF_SLEEPER, PX - 6, PY, false);
    RcActiveEncounter *d = run_of(w, sleeper);
    assert(rc_encounter_wake(w, sleeper->uid));
    assert(d->form == 1 && d->engaged && sleeper->target_uid == 0
           && "a zero-tick wake attacks at once");

    RcNpc *ticker = spawn(w, DEF_TICKER, PX + 2, PY + 6, false);
    assert(rc_encounter_scale_player_damage(w, ticker->uid, COMBAT_MELEE_STAB, 10) == 5);
    assert(rc_encounter_scale_player_damage(w, ticker->uid, COMBAT_MAGIC, 10) == 10);
    assert(rc_encounter_scale_player_damage(w, ticker->uid, COMBAT_RANGED, 10) == 5
           && "modifiers for another form never apply");
    assert(rc_encounter_scale_player_damage(w, PLAIN, COMBAT_MAGIC, 10) == 10);
    assert(rc_encounter_attack(w, ticker->uid, -1, NULL) == NULL);
    assert(rc_encounter_attack(w, ticker->uid, 3, NULL) == NULL);
    assert(rc_encounter_attack(w, ticker->uid, 2, NULL) == &g_specs[2].attacks[2]);
    g_specs[0].forms[0].transition_ticks = 3;
    g_specs[1].forms[0].transition_ticks = 2;
    g_specs[2].damage_mod_count = 0;
    rc_world_destroy(w);
}

static void test_phase_triggers(void) {
    RcWorld *w = make_world();
    install_specs(w);
    RcEncounterSpec *p = &g_specs[2];
    p->phase_count = 2;
    p->phases[1] = p->phases[0];
    snprintf(p->phases[1].id, sizeof(p->phases[1].id), "late");
    p->phases[1].enter_at_hp_pct = 50;
    p->mechanic_count = 2;
    p->mechanics[0] = acid_mechanic("Leave", RC_ENC_TRIGGER_PHASE_EXIT, 0, 1);
    p->mechanics[0].phase_mask = 1u << 0;
    p->mechanics[1] = acid_mechanic("Late acid", RC_ENC_TRIGGER_PHASE_IN, 4, 1);
    p->mechanics[1].phase_mask = 1u << 1;
    RcNpc *boss = spawn(w, DEF_TICKER, PX + 2, PY, false);
    RcActiveEncounter *a = run_of(w, boss);
    w->player.attack_target = boss->uid;
    ticks(w, 6);
    assert(a->engaged && w->encounter.triggered_mechanics == 0
           && "phase_in mechanics wait for their phase");
    boss->current_hp = 40;
    ticks(w, 1);
    assert(a->current_phase == 1 && w->encounter.triggered_mechanics == 1
           && "leaving phase 0 runs its exit mechanic");
    RcTick due = a->next_due[1];
    assert(due == w->tick - 1 + 4);
    for (int i = 0; i < 6; i++) {
        a->run_flags = 0;
        a->active_mechanic_idx = 0xFF;
        ticks(w, 1);
    }
    assert(w->encounter.triggered_mechanics == 2 && "phase_in repeats on its period");
    rc_world_destroy(w);
}

static void test_script_registry(void) {
    RcWorld *w = make_world();
    char long_name[64];
    memset(long_name, 'a', sizeof(long_name) - 1);
    long_name[sizeof(long_name) - 1] = '\0';
    assert(rc_encounter_register_script(w, long_name, phase_script) == -1);
    assert(rc_encounter_register_script(w, "x", NULL) == -1);
    assert(rc_encounter_register_script(w, "", phase_script) == -1);
    int first = rc_encounter_register_script(w, "x", phase_script);
    assert(first >= 0 && rc_encounter_register_script(w, "x", phase_script) == first);
    while (w->encounter.script_count < RC_ENC_SCRIPT_REGISTRY_CAP) {
        char name[16];
        snprintf(name, sizeof(name), "s%d", w->encounter.script_count);
        assert(rc_encounter_register_script(w, name, phase_script) >= 0);
    }
    assert(rc_encounter_register_script(w, "overflow", phase_script) == -1);
    assert(rc_encounter_script_lookup(w, "missing") == NULL);
    rc_world_destroy(w);
}


static void relaunch(RcWorld *w, RcNpc *boss) {
    w->player.num_pending_hits = 0;
    boss->attack_timer = 0;
    boss->combat.attack_prepared = false;
    boss->combat.failure_reason = NULL;
    assert(rc_combat_start_npc_vs_player(w, boss->uid, 0));
    g_hits = 0;
}

// Ticks until the boss's next hit lands (at most `limit` ticks).
static bool landed(RcWorld *w, int limit) {
    for (int i = 0; i < limit && !g_hits; i++) ticks(w, 1);
    return g_hits > 0;
}

static void test_combat_launch_paths(void) {
    RcWorld *w = make_world();
    install_specs(w);
    RcEncounterSpec *t = &g_specs[2];
    t->mechanic_count = 0;
    t->attacks[0].flags = RC_ENC_ATTACK_DIAGONAL_REACH;
    t->phases[0].allowed_attack_mask = 1;
    // Footprint PX+1..PX+2: the player at PX,PY touches only its corner.
    RcNpc *boss = spawn(w, DEF_TICKER, PX + 1, PY + 1, false);
    relaunch(w, boss);
    assert(landed(w, 4) && g_last_hit.enc_attack == 1);
    assert(boss->x == PX + 1 && boss->y == PY + 1 && "diagonal reach melees from the corner");

    // Only melee-range attacks and a distant player: the boss closes in.
    w->player.x = PX - 8;
    relaunch(w, boss);
    ticks(w, 1);
    assert(!g_hits && boss->route_mode == RC_NPC_ROUTE_CHASE);
    w->player.x = PX;

    // Effect-only attacks always land for zero damage.
    t->attacks[1].flags = RC_ENC_ATTACK_NO_DAMAGE;
    t->attacks[1].max_hit = 0;
    t->phases[0].allowed_attack_mask = 2;
    relaunch(w, boss);
    assert(landed(w, 6) && g_last_hit.enc_attack == 2);
    assert(g_last_hit.damage == 0 && g_last_hit.rolled == 0);

    // A tile attack with the run's hazard quota spent is refused.
    int slot = rc_encounter_run_for_npc(w, boss->uid);
    t->attacks[2].flags = RC_ENC_ATTACK_TILE_TARGET;
    t->attacks[2].hit_delay = 2;
    t->phases[0].allowed_attack_mask = 4;
    while (rc_encounter_add_effect(w, slot, RC_ENC_EFFECT_ROOM_ATTACK, PX, PY, 0,
                                   PX, PY, -1, COMBAT_NONE, 0, "x", "") >= 0) {}
    relaunch(w, boss);
    ticks(w, 1);
    assert(boss->combat.failure_reason && boss->target_uid == -1
           && !boss->combat.attack_prepared);

    // Breath without a content rule is refused rather than guessed.
    t->attacks[2].flags = 0;
    t->attacks[2].breath = 1;
    relaunch(w, boss);
    ticks(w, 3);
    assert(!g_hits && boss->combat.failure_reason && boss->target_uid == -1);
    t->attacks[2].breath = 0;

    // A prepared attack from an ended run is dropped, not launched.
    w->player.x = PX - 12;
    relaunch(w, boss);
    ticks(w, 1);
    assert(boss->combat.prepared_enc_attack == 3 && boss->combat.attack_prepared);
    assert(rc_encounter_restart(w, boss->uid));
    w->player.x = PX;
    ticks(w, 1);
    assert(!boss->combat.attack_prepared && !g_hits && "the stale selection never fires");
    rc_world_destroy(w);
}

int main(void) {
    test_binding_and_respawn();
    test_use_specs_binds_existing_npcs();
    test_engagement();
    test_independent_deadlines_and_slot_reuse();
    test_full_width_owner();
    test_lethal_transform_and_revert();
    test_dormant_wake_and_reset();
    test_selection();
    test_special_rotation();
    test_attack_attribution();
    test_strikes_pools_and_quota();
    test_owned_helpers();
    test_phase_owner();
    test_script_admission_and_reset();
    test_capacity_and_restart();
    test_immediate_transitions_and_modifiers();
    test_phase_triggers();
    test_script_registry();
    test_combat_launch_paths();
    printf("test_encounter: ok\n");
    return 0;
}
