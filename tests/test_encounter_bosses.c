// test_encounter_bosses — the admitted bosses on installed B237 data, the
// installed specs and the real content hooks (one combat world, reset
// between scenarios, so the reset/installer contract is exercised too).
//
//   Kalphite Queen: adjacency odds, always-accurate ranged/magic, spine
//     prayer drain, the 20-tick nonterminal transform, final death/respawn.
//   King Black Dragon: the dragonfire table, protected effect odds,
//     breath bypassing prayer, melee only in melee distance.
//   Vorkath: dormant binding, the Poke handler, waking, the 6+1 special
//     rotation (acid pools landing as loc 32000, the spawn landing after its
//     flight), deadly dragonfire as a warned strike, the named attack and
//     mechanic-part events presentation reads, death returning it to sleep.
//   Chaos Elemental: Madness (weapon first, space-limited, never with a full
//     inventory) and Confusion (2-5 tiles from the footprint).
//   General Graardor: approach and 2:1 adjacent odds; his room (he and his
//     bodyguards fight only a player inside it) and bodyguards that return
//     only when he respawns.
//   Kalphite Queen: per-form defence bonuses; workers hatching from a nearby
//     bulging cocoon (real map placements) and despawning.

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../rc-content/combat/dragonfire.h"
#include "../rc-content/content.h"
#include "../rc-core/api.h"
#include "../rc-core/combat.h"
#include "../rc-core/combat_hit.h"
#include "../rc-core/combat_formula.h"
#include "../rc-core/config.h"
#include "../rc-core/encounter.h"
#include "../rc-core/events.h"
#include "../rc-core/interaction.h"
#include "../rc-core/npc.h"
#include "../rc-core/object_runtime.h"
#include "../rc-core/prayer.h"
#include "world_test_fixture.h"

#define BX 3230
#define BY 3230

static RcWorld *g_world;
static int g_died;

static void count_deaths(RcWorld *world, int evt, const void *payload, void *ctx) {
    (void)world;
    (void)evt;
    (void)payload;
    (void)ctx;
    g_died++;
}

static RcWorld *setup(void) {
    if (!g_world) {
        RcWorldConfig cfg = rc_preset_combat_only();
        RcWorldConfig full = rc_preset_full_game();
        cfg.seed = 4242;
        cfg.spawns_path = NULL;
        cfg.ground_item_spawns_path = NULL;
        // Object definitions without placements: encounter locs only.
        cfg.subsystems |= RC_SUB_OBJECTS;
        cfg.object_defs_path = full.object_defs_path;
        cfg.object_behaviors_path = full.object_behaviors_path;
        g_world = rc_world_create_config(&cfg);
        assert(g_world);
        assert(rc_world_install_content(g_world, rc_content_register_all));
    } else {
        assert(rc_world_reset(g_world));
    }
    RcWorld *w = g_world;
    assert(w->combat_hooks.npc_breath_max_hit && "content survives reset");
    rc_test_open_mapsquare(w, BX, BY, 0);
    w->player.x = BX + 2;
    w->player.y = BY - 1;
    w->player.max_hp = w->player.current_hp = 99000;
    w->player.skills.base_level[SKILL_HITPOINTS] = 99;
    w->player.skills.boosted_level[SKILL_HITPOINTS] = 99;
    w->player.auto_retaliate = false;
    g_died = 0;
    assert(rc_event_subscribe(w, RC_EVT_NPC_DIED, count_deaths, NULL) == 0);
    return w;
}

static RcNpc *spawn_on(RcWorld *w, int npc_id, int x, int y, int plane) {
    int def_idx = rc_npc_def_find(npc_id);
    assert(def_idx >= 0);
    RcNpcSpawnConfig config = {.spawn_key = 0, .wander_range = 0,
                               .flags = RC_NPC_SPAWN_RESPAWNS};
    RcNpcSpawnResult r = rc_npc_spawn_ex(w, def_idx, x, y, plane, &config);
    assert(r.status == RC_NPC_SPAWN_CREATED);
    return &w->npcs[r.slot];
}

static RcNpc *spawn_boss(RcWorld *w, int npc_id, int x, int y) {
    return spawn_on(w, npc_id, x, y, 0);
}

static RcActiveEncounter *run_of(RcWorld *w, const RcNpc *npc) {
    int slot = rc_encounter_run_for_npc(w, (RcNpcId)npc->uid);
    return slot >= 0 ? &w->encounter.active[slot] : NULL;
}

static const RcEncounterSpec *spec_of(RcWorld *w, const RcActiveEncounter *a) {
    return rc_encounter_spec(w, a->spec_idx);
}

static void ticks(RcWorld *w, int n) {
    for (int i = 0; i < n; i++) rc_world_tick(w);
}

static void sample(RcWorld *w, const RcNpc *boss, bool adjacent, int *counts,
                   int n) {
    const RcEncounterAttack *atk = NULL;
    uint32_t generation = 0;
    for (int i = 0; i < n; i++) {
        int idx = rc_encounter_select_npc_attack(w, (RcNpcId)boss->uid,
                                                 adjacent ? 1 : 8, adjacent,
                                                 &atk, &generation);
        assert(idx >= 0);
        counts[idx]++;
    }
}

// This tick's encounter events with `key`, aimed at `target_kind`, landing
// after `hit_delay` ticks.
static int count_events(RcWorld *w, const char *key, int target_kind,
                        int hit_delay) {
    int count = 0, n = 0;
    const RcCombatAttackEvent *events = rc_combat_attack_events(w, &n);
    for (int i = 0; i < n; i++) {
        const RcCombatAttackEvent *e = &events[i];
        if (e->active && e->action_kind == RC_COMBAT_ACTION_ENCOUNTER
                && !strcmp(e->action_key_name, key)
                && e->target_kind == target_kind && e->hit_delay == hit_delay)
            count++;
    }
    return count;
}

// Form-change events recorded by definition `def_id`, holding `hold` ticks.
static int count_form_events(RcWorld *w, const char *key, int def_id, int hold) {
    int count = 0, n = 0;
    const RcCombatAttackEvent *events = rc_combat_attack_events(w, &n);
    for (int i = 0; i < n; i++) {
        const RcCombatAttackEvent *e = &events[i];
        count += e->active && e->action_kind == RC_COMBAT_ACTION_ENCOUNTER
              && !strcmp(e->action_key_name, key)
              && e->source_definition_id == def_id
              && e->target_kind == RC_COMBAT_ACTOR_NONE && e->hit_delay == hold;
    }
    return count;
}

static int count_pools(RcWorld *w, bool landed) {
    int count = 0;
    for (int i = 0; i < RC_ENC_MAX_EFFECTS; i++) {
        const RcEncounterEffect *e = &w->encounter_effects[i];
        count += e->active && e->kind == RC_ENC_EFFECT_ACID_POOL
              && e->landed == landed;
    }
    return count;
}

// Spawned loc states for `loc_id`; with `pools`, each must sit on a pool.
static int count_locs(RcWorld *w, int loc_id, bool pools) {
    int count = 0;
    for (int i = 0; i < w->object_state_count; i++) {
        const RcObjectState *s = &w->object_states[i];
        if (s->active_obj_id != loc_id || !(s->flags & RC_OBJECT_STATE_SPAWNED))
            continue;
        bool on_pool = false;
        for (int j = 0; j < RC_ENC_MAX_EFFECTS && pools; j++) {
            const RcEncounterEffect *e = &w->encounter_effects[j];
            on_pool |= e->active && e->kind == RC_ENC_EFFECT_ACID_POOL
                    && e->object_key == s->placement_key && e->x == s->active_x
                    && e->y == s->active_y && s->active_type == 10;
        }
        assert(!pools || on_pool);
        count++;
    }
    return count;
}

static int count_drops(RcWorld *w) {
    int count = 0;
    for (int i = 0; i < RC_ENC_MAX_EFFECTS; i++) {
        const RcEncounterEffect *e = &w->encounter_effects[i];
        count += e->active && e->kind == RC_ENC_EFFECT_HELPER_DROP;
    }
    return count;
}

// Every event from a run's boss names one of its attacks or mechanic parts
// (rc_encounter_prim_parts); returns how many attacks were named.
static int check_named_events(RcWorld *w, const RcNpc *boss,
                              const RcEncounterSpec *spec) {
    int attacks = 0, n = 0;
    const RcCombatAttackEvent *events = rc_combat_attack_events(w, &n);
    for (int i = 0; i < n; i++) {
        const RcCombatAttackEvent *e = &events[i];
        if (!e->active || e->source_uid != boss->uid) continue;
        assert(e->action_kind == RC_COMBAT_ACTION_ENCOUNTER);
        bool known = false;
        for (int a = 0; a < spec->attack_count && !known; a++) {
            known = !strcmp(e->action_key_name, spec->attacks[a].name);
            attacks += known;
        }
        for (int m = 0; m < spec->mechanic_count && !known; m++) {
            const RcEncounterMechanic *mech = &spec->mechanics[m];
            const char *const *parts = rc_encounter_prim_parts(mech->primitive_id);
            for (int p = 0; parts && parts[p] && !known; p++) {
                char key[64];
                if (parts[p][0])
                    snprintf(key, sizeof(key), "%s:%s", mech->name, parts[p]);
                else
                    snprintf(key, sizeof(key), "%s", mech->name);
                known = !strcmp(e->action_key_name, key);
            }
        }
        if (!known) fprintf(stderr, "unnamed event '%s'\n", e->action_key_name);
        assert(known);
    }
    return attacks;
}

static void hit_boss(RcWorld *w, RcNpc *npc, int damage) {
    assert(rc_queue_hit_meta(npc->pending_hits, &npc->num_pending_hits, damage,
                             0, COMBAT_MELEE_CRUSH, RC_HIT_SOURCE_PLAYER, 0,
                             w->tick, 0, damage));
}

static void test_kalphite_queen(void) {
    RcWorld *w = setup();
    RcNpc *kq = spawn_boss(w, 963, BX, BY);
    RcActiveEncounter *a = run_of(w, kq);
    assert(a && a->form == 0 && !strcmp(spec_of(w, a)->slug, "kalphite_queen"));
    // Her protections are defence: the crawling form resists magic and
    // ranged (+100), the airborne form melee.
    const int16_t crawling[5] = {50, 50, 10, 100, 100};
    const int16_t airborne[5] = {100, 100, 100, 10, 10};
    assert(!memcmp(kq->defence_bonus, crawling, sizeof(crawling)));
    assert(rc_npc_defensive_roll(kq, COMBAT_MAGIC) == (kq->stats[5] + 9) * 164);
    assert(rc_npc_defensive_roll(kq, COMBAT_MELEE_CRUSH) == (kq->stats[1] + 9) * 74);
    assert(!run_of(w, spawn_boss(w, 4303, BX + 20, BY + 20))
           && "quest copies of the Queen are not bosses");

    int adjacent[3] = {0}, distant[3] = {0};
    sample(w, kq, true, adjacent, 4000);
    sample(w, kq, false, distant, 4000);
    assert(adjacent[0] > 1800 && adjacent[0] < 2200 && "1/2 melee when adjacent");
    assert(adjacent[1] > 850 && adjacent[1] < 1150);
    assert(distant[0] == 0 && distant[1] > 1800 && distant[1] < 2200);

    // Ranged and magic always hit; spines drain prayer when they roll above 0.
    w->player.current_prayer_points = 990;
    assert(rc_combat_start_npc_vs_player(w, kq->uid, 0));
    int ranged_magic = 0, drained_hits = 0;
    for (int t = 0; t < 120; t++) {
        int before = w->player.current_prayer_points;
        ticks(w, 1);
        for (int i = 0; i < w->player.num_pending_hits; i++) {
            const RcPendingHit *h = &w->player.pending_hits[i];
            if (!h->active || h->source_idx != kq->uid) continue;
            assert(h->enc_attack >= 1 && h->enc_attack <= 3);
            assert(h->enc_generation == a->generation);
            if (h->enc_attack >= 2) {
                assert(h->accurate && "KQ ranged and magic never miss");
                ranged_magic++;
            }
        }
        if (w->player.current_prayer_points < before) {
            assert(before - w->player.current_prayer_points == 10);
            drained_hits++;
        }
    }
    assert(ranged_magic > 0 && drained_hits > 0);

    // The crawling form dies into the airborne form without loot or death.
    kq->stats[1] -= 20;
    kq->current_hp = 1;
    hit_boss(w, kq, 60);
    ticks(w, 1);
    assert(!kq->is_dead && kq->current_hp == 0 && g_died == 0);
    assert(kq->player_untargetable && a->transition_until);
    assert(count_form_events(w, RC_ENC_FORM_LEAVE_KEY, 963, 20) == 1
           && "the crawling form presents its 20-tick transition");
    ticks(w, 19);
    assert(a->form == 0);
    ticks(w, 1);
    const RcNpcDef *def = rc_npc_def_for_npc(w, kq);
    assert(a->form == 1 && def->id == 965 && kq->current_hp == 255);
    assert(!memcmp(kq->defence_bonus, airborne, sizeof(airborne)));
    assert(rc_npc_defensive_roll(kq, COMBAT_MELEE_SLASH) == (kq->stats[1] + 9) * 164);
    assert(count_form_events(w, RC_ENC_FORM_ENTER_KEY, 965, 0) == 1);
    assert(kq->stats[1] == def->stats[1] - 20 && "defence drains carry over");
    kq->current_hp = 1;
    hit_boss(w, kq, 60);
    ticks(w, 1);
    assert(kq->is_dead && g_died == 1 && kq->respawn_timer == 50);
    ticks(w, 60);
    a = run_of(w, kq);
    assert(!kq->is_dead && a && a->form == 0);
    assert(rc_npc_def_for_npc(w, kq)->id == 963 && kq->current_hp == 255);
    assert(!memcmp(kq->defence_bonus, crawling, sizeof(crawling))
           && "the respawned Queen crawls again");
}

// The real lair: placements for the bulging cocoons (loc 23611). Game data
// with placements is a different data set, so the shared world goes first.
static void test_kq_worker_hatching(void) {
    rc_world_destroy(g_world);
    g_world = NULL;
    RcWorldConfig cfg = rc_preset_combat_only();
    RcWorldConfig full = rc_preset_full_game();
    cfg.seed = 777;
    cfg.spawns_path = NULL;
    cfg.ground_item_spawns_path = NULL;
    cfg.subsystems |= RC_SUB_OBJECTS;
    cfg.object_defs_path = full.object_defs_path;
    cfg.object_behaviors_path = full.object_behaviors_path;
    cfg.object_placements_path = full.object_placements_path;
    RcWorld *w = rc_world_create_config(&cfg);
    assert(w && "placement world");
    assert(rc_world_install_content(w, rc_content_register_all));
    rc_test_open_mapsquare(w, 3474, 9496, 0);
    w->player.x = 3476;
    w->player.y = 9506;
    w->player.max_hp = w->player.current_hp = 99000;
    w->player.skills.base_level[SKILL_HITPOINTS] = 99;
    w->player.auto_retaliate = false;
    rc_combat_set_multi_combat(w, true);   // the Queen's chamber is multi-combat

    // At her spawn the nearest cocoon (3482,9502) is 4 tiles away: no hatch.
    RcNpc *kq = spawn_boss(w, 963, 3474, 9496);
    RcActiveEncounter *a = run_of(w, kq);
    assert(a && rc_combat_start_npc_vs_player(w, kq->uid, 0));
    ticks(w, 1);
    assert(a->engaged);
    int npcs_before = w->npc_count;
    for (int i = 0; i < 200; i++) {
        hit_boss(w, kq, 0);
        ticks(w, 1);
        kq->current_hp = kq->spawn_hp;
    }
    assert(w->npc_count == npcs_before && "no cocoon within 3 tiles");

    // Against the cocoon: hits (even of 0) hatch workers 1/20 of the time.
    kq->x = 3477;
    kq->y = 9500;
    int worker_def = rc_npc_def_find(961);
    int seen[64], hatched = 0;
    for (int i = 0; i < 400; i++) {
        hit_boss(w, kq, 0);
        ticks(w, 1);
        kq->current_hp = kq->spawn_hp;
        kq->x = 3477;
        kq->y = 9500;
        w->player.current_hp = w->player.max_hp;
        for (int n = 0; n < w->npc_count; n++) {
            const RcNpc *worker = &w->npcs[n];
            if (!worker->active || worker->def_id != worker_def
                    || !worker->despawn_tick) continue;
            bool counted = false;
            for (int k = 0; k < hatched && k < 64; k++) counted |= seen[k] == worker->uid;
            if (counted) continue;
            if (hatched < 64) seen[hatched] = worker->uid;
            hatched++;
            // Hatched last tick, aggressive, temporary, beside the cocoon
            // at (3482,9502) 2x2.
            assert(!worker->respawns && worker->despawn_tick == w->tick - 1 + 100);
            assert(worker->target_uid == 0 && "it attacks the player");
            int dx = worker->x + 1 < 3482 ? 3482 - (worker->x + 1)
                   : worker->x > 3483 ? worker->x - 3483 : 0;
            int dy = worker->y + 1 < 9502 ? 9502 - (worker->y + 1)
                   : worker->y > 9503 ? worker->y - 9503 : 0;
            assert((dx > dy ? dx : dy) <= 2 && "hatched beside the cocoon");
        }
    }
    assert(hatched >= 8 && hatched <= 40 && "about 1 in 20 of 400 hits");
    // Each despawns 100 ticks after hatching, fight or not.
    ticks(w, 101);
    for (int n = 0; n < w->npc_count; n++)
        assert(!(w->npcs[n].active && w->npcs[n].def_id == worker_def
                 && w->npcs[n].despawn_tick) && "hatched workers despawn");
    rc_world_destroy(w);
}

static void test_dragonfire_table(void) {
    RcWorld *w = setup();
    RcPlayer *p = &w->player;
    // Rows: no potion, antifire, super; columns: none, shield, PfM, both.
    static const int fiery[3][4] = {{65, 15, 20, 15}, {50, 0, 5, 0}, {0, 0, 0, 0}};
    static const int special[3][4] = {{50, 10, 15, 10}, {50, 10, 15, 10},
                                      {50, 10, 15, 10}};
    static const int burnt[3][4] = {{80, 20, 30, 20}, {70, 10, 20, 10}, {60, 0, 10, 0}};
    static const int resisted[3][4] = {{50, 20, 30, 20}, {40, 10, 20, 10},
                                       {30, 0, 10, 0}};
    for (int potion = 0; potion < 3; potion++) {
        for (int column = 0; column < 4; column++) {
            p->antifire_until = potion == 1 ? w->tick + 10 : 0;
            p->super_antifire_until = potion == 2 ? w->tick + 10 : 0;
            p->equipment[EQUIP_SHIELD].item_id = column & 1 ? 1540 : -1;
            p->active_prayers = column & 2 ? PRAYER_PROTECT_MAGIC : 0;
            bool shielded = false;
            int (*max)(const RcWorld *, const RcNpc *, uint8_t, bool, bool *) =
                w->combat_hooks.npc_breath_max_hit;
            assert(max(w, NULL, RC_DRAGONFIRE_KBD_FIERY, false, &shielded)
                   == fiery[potion][column]);
            assert(shielded == (column != 0));
            assert(max(w, NULL, RC_DRAGONFIRE_KBD_SPECIAL, true, &shielded)
                   == special[potion][column]);
            assert(max(w, NULL, RC_DRAGONFIRE_VORKATH, false, &shielded)
                   == burnt[potion][column]);
            assert(max(w, NULL, RC_DRAGONFIRE_VORKATH, true, &shielded)
                   == resisted[potion][column]);
            int coloured = burnt[potion][column] > 5 ? burnt[potion][column] - 5 : 0;
            assert(max(w, NULL, RC_DRAGONFIRE_VORKATH_COLOURED, false, &shielded)
                   == coloured);
        }
    }
    p->equipment[EQUIP_SHIELD].item_id = 11283;   // dragonfire shield
    p->antifire_until = p->super_antifire_until = 0;
    p->active_prayers = 0;
    assert(w->combat_hooks.npc_breath_max_hit(w, NULL, RC_DRAGONFIRE_KBD_FIERY,
                                              false, NULL) == 15);
    p->equipment[EQUIP_SHIELD].item_id = 2890;    // elemental: wyverns only
    assert(w->combat_hooks.npc_breath_max_hit(w, NULL, RC_DRAGONFIRE_KBD_FIERY,
                                              false, NULL) == 65);
    assert(w->combat_hooks.npc_breath_max_hit(w, NULL, 9, false, NULL) == -1);
    p->equipment[EQUIP_SHIELD].item_id = -1;
}

static void test_king_black_dragon(void) {
    RcWorld *w = setup();
    RcNpc *kbd = spawn_boss(w, 239, BX, BY);
    RcActiveEncounter *a = run_of(w, kbd);
    assert(a && !strcmp(spec_of(w, a)->slug, "king_black_dragon"));
    int adjacent[5] = {0}, distant[5] = {0};
    sample(w, kbd, true, adjacent, 9000);
    sample(w, kbd, false, distant, 6000);
    assert(adjacent[0] > 2700 && adjacent[0] < 3300 && "1/3 melee in melee distance");
    assert(adjacent[1] > 2700 && adjacent[1] < 3300 && "1/3 fiery breath");
    for (int i = 2; i < 5; i++) assert(adjacent[i] > 850 && adjacent[i] < 1150);
    assert(distant[0] == 0 && distant[1] > 2700 && distant[1] < 3300);

    // Super antifire + anti-dragon shield: fiery breath 0, specials up to 10;
    // breath ignores the prayer snapshot and marks shield protection.
    RcPlayer *p = &w->player;
    p->super_antifire_until = UINT64_MAX;
    p->equipment[EQUIP_SHIELD].item_id = 1540;
    p->active_prayers = PRAYER_PROTECT_MAGIC;
    p->current_prayer_points = 990;
    assert(rc_combat_start_npc_vs_player(w, kbd->uid, 0));
    int breaths = 0;
    for (int t = 0; t < 160; t++) {
        p->current_prayer_points = 990;
        p->active_prayers = PRAYER_PROTECT_MAGIC;
        ticks(w, 1);
        for (int i = 0; i < p->num_pending_hits; i++) {
            const RcPendingHit *h = &p->pending_hits[i];
            if (!h->active || h->source_idx != kbd->uid || h->enc_attack < 2)
                continue;
            assert(h->prayer_snapshot == 0 && (h->flags & RC_HIT_BREATH_PROTECTED));
            assert(h->damage <= (h->enc_attack == 2 ? 0 : 10));
            breaths++;
        }
    }
    assert(breaths > 0);
    p->super_antifire_until = 0;

    // Special breath effects: 1/2 unprotected, 1/8 behind shield or prayer.
    RcPayloadPlayerDamaged hit = {
        .source_npc_id = (uint32_t)kbd->uid, .enc_generation = a->generation,
    };
    int drained[2] = {0}, frozen = 0, poisoned = 0;
    for (int protect = 0; protect < 2; protect++) {
        hit.flags = protect ? RC_HIT_BREATH_PROTECTED : 0;
        for (int i = 0; i < 800; i++) {
            for (int k = 0; k < SKILL_COUNT; k++) p->skills.boosted_level[k] = 50;
            hit.enc_attack = 4;     // Shocking breath
            rc_event_fire(w, RC_EVT_PLAYER_DAMAGED, &hit);
            if (p->skills.boosted_level[SKILL_ATTACK] == 48) {
                drained[protect]++;
                assert(p->skills.boosted_level[SKILL_PRAYER] == 48);
                assert(p->skills.boosted_level[SKILL_HITPOINTS] == 50);
            }
        }
    }
    assert(drained[0] > 330 && drained[0] < 470 && "about half without protection");
    assert(drained[1] > 60 && drained[1] < 140 && "about 1/8 when protected");
    hit.flags = 0;
    for (int i = 0; i < 64 && !(frozen && poisoned); i++) {
        hit.enc_attack = 5;     // Icy breath
        rc_event_fire(w, RC_EVT_PLAYER_DAMAGED, &hit);
        frozen |= rc_player_freeze_ticks_remaining(w) >= 9;
        hit.enc_attack = 3;     // Toxic breath
        rc_event_fire(w, RC_EVT_PLAYER_DAMAGED, &hit);
        poisoned |= p->toxin_severity == 40 && !p->toxin_venom;
    }
    assert(frozen && poisoned && "icy freezes 10 ticks, toxic poisons from 8");
}

static void test_vorkath(void) {
    RcWorld *w = setup();
    RcNpc *vork = spawn_boss(w, 8059, BX, BY);
    RcActiveEncounter *a = run_of(w, vork);
    assert(a && a->form == 0 && !a->engaged);
    assert(!run_of(w, spawn_boss(w, 8058, BX + 30, BY + 30))
           && "the option-less copy never binds");
    RcInteractionDispatchKey key = rc_interaction_dispatch_key_any();
    key.kind = RC_INTERACTION_NPC;
    key.op = rc_interaction_op_from_option(0);
    key.definition_id = 8059;
    assert(rc_interaction_has_specific_world_handler(w, &key) && "Poke wakes it");
    assert(!rc_encounter_player_can_target_npc(w, vork->uid));

    w->player.x = BX + 3;
    w->player.y = BY - 6;
    int handler = rc_interaction_find_world_handler(w, &key);
    assert(handler >= 0);
    RcPendingInteraction poke = {.active = true, .op = key.op};
    poke.target.kind = RC_INTERACTION_NPC;
    poke.target.entity_uid = vork->uid;
    poke.target.definition_id = 8059;
    RcInteractionHandlerResult r = w->interaction_handlers[handler].fn(
        w, &w->player, &poke, w->interaction_handlers[handler].ctx);
    assert(r.code == RC_INTERACTION_HANDLER_COMPLETE && a->transition_until);
    assert(count_form_events(w, RC_ENC_FORM_LEAVE_KEY, 8059, 5) == 1
           && "waking is the sleeping form's 5-tick transition");
    r = w->interaction_handlers[handler].fn(w, &w->player, &poke,
                                           w->interaction_handlers[handler].ctx);
    assert(r.code == RC_INTERACTION_HANDLER_FAILURE && "a waking Vorkath is not asleep");
    poke.target.kind = RC_INTERACTION_OBJECT;
    r = w->interaction_handlers[handler].fn(w, &w->player, &poke,
                                           w->interaction_handlers[handler].ctx);
    assert(r.code == RC_INTERACTION_HANDLER_FAILURE);
    for (int i = 0; i < 6 && a->form == 0; i++) ticks(w, 1);
    assert(count_form_events(w, RC_ENC_FORM_ENTER_KEY, 8061, 0) == 1);
    ticks(w, 1);
    assert(a->form == 1 && a->engaged && vork->current_hp == 750);
    assert(rc_npc_def_for_npc(w, vork)->id == 8061 && vork->target_uid == 0);

    // Six standard attacks, then a special; specials alternate.
    int seen_acid = 0, seen_spawn = 0, strikes = 0, named = 0;
    uint8_t first = 0xFF, second = 0xFF;
    const RcEncounterSpec *spec = spec_of(w, a);
    for (int t = 0; t < 1500 && !(seen_acid && seen_spawn && strikes); t++) {
        ticks(w, 1);
        named += check_named_events(w, vork, spec);
        if (a->active_mechanic_idx == 1 && !seen_acid) {
            seen_acid = a->attack_count;
            assert(a->run_flags & RC_ENC_RUN_BOSS_IDLE);
            assert(rc_encounter_scale_player_damage(w, vork->uid, COMBAT_RANGED, 40) == 20);
            // The spit, one projectile per pool tile, and the first strike.
            int pools = count_pools(w, false);
            assert(count_events(w, "Acid phase", RC_COMBAT_ACTOR_PLAYER, 0) == 1);
            assert(pools > 0 && count_events(w, "Acid phase:pool",
                                             RC_COMBAT_ACTOR_NONE, 2) == pools);
            assert(count_events(w, "Acid phase:strike", RC_COMBAT_ACTOR_NONE, 2) == 1);
            assert(!count_pools(w, true) && !count_locs(w, 32000, true)
                   && "acid in flight neither hurts nor shows a pool");
            ticks(w, 2);
            assert(count_pools(w, true) == pools && !count_pools(w, false));
            assert(count_locs(w, 32000, true) == pools && "each landed pool is loc 32000");
            assert(count_events(w, "Acid phase:strike", RC_COMBAT_ACTOR_NONE, 2) == 1);
            w->player.current_hp = w->player.max_hp;
            if (first == 0xFF) first = 1; else second = 1;
        }
        if (a->active_mechanic_idx == 2 && !seen_spawn) {
            seen_spawn = a->attack_count;
            assert(rc_player_is_frozen(w) && a->run_flags & RC_ENC_RUN_BOSS_IMMUNE);
            assert(count_events(w, "Zombified spawn", RC_COMBAT_ACTOR_PLAYER, 0) == 1);
            assert(count_events(w, "Zombified spawn:helper", RC_COMBAT_ACTOR_NONE, 3) == 1);
            assert(a->helper_count == 0 && count_drops(w) == 1 && "the spawn is in flight");
            ticks(w, 2);
            assert(a->helper_count == 0 && rc_player_is_frozen(w));
            ticks(w, 1);
            assert(a->helper_count == 1 && !count_drops(w) && rc_player_is_frozen(w));
            RcNpc *spawn = rc_npc_resolve(w, a->helpers[0]);
            assert(spawn && rc_npc_def_for_npc(w, spawn)->id == 8063);
            assert(rc_encounter_scale_player_damage(w, vork->uid, COMBAT_RANGED, 40) == 0);
            // Crumble Undead, cast through combat, kills the spawn outright.
            int crumble = rc_spell_find("Crumble Undead");
            assert(rc_encounter_player_instakill(w, (RcNpcId)spawn->uid, crumble + 1));
            RcPlayer *p = &w->player;
            p->skills.base_level[SKILL_MAGIC] = p->skills.boosted_level[SKILL_MAGIC] = 99;
            for (int i = 0; i < RC_INVENTORY_SIZE; i++) p->inventory[i].item_id = -1;
            p->inventory[0] = (RcInvSlot){.item_id = 556, .quantity = 100};
            p->inventory[1] = (RcInvSlot){.item_id = 557, .quantity = 100};
            p->inventory[2] = (RcInvSlot){.item_id = 562, .quantity = 100};
            assert(rc_player_cast_spell_on_npc(w, crumble, spawn->uid));
            assert(rc_encounter_same_fight(w, vork->uid, spawn->uid));
            for (int i = 0; i < 12 && a->helper_count; i++) {
                ticks(w, 1);
                w->player.current_hp = w->player.max_hp;
            }
            assert(a->helper_count == 0 && spawn->is_dead && "one cast kills it");
            assert(!rc_player_is_frozen(w) && a->run_flags == 0);
            if (first == 0xFF) first = 2; else second = 2;
        }
        for (int i = 0; i < RC_ENC_MAX_EFFECTS; i++) {
            const RcEncounterEffect *e = &w->encounter_effects[i];
            if (e->active && e->kind == RC_ENC_EFFECT_TILE_STRIKE
                    && e->max_damage == 121) {
                assert(e->adjacent_pct == 50 && "deadly dragonfire is a warned strike");
                // Seen first on its launch tick, aimed at the launch tile.
                assert(strikes || count_events(w, "Deadly dragonfire",
                                               RC_COMBAT_ACTOR_NONE, 4) == 1);
                strikes++;
            }
        }
        w->player.current_hp = w->player.max_hp;
    }
    assert(seen_acid && seen_spawn && first != second);
    assert((seen_acid == 7 || seen_acid == 14) && (seen_spawn == 7 || seen_spawn == 14)
           && "a special replaces every seventh attack");
    assert(strikes > 0 && "deadly dragonfire was launched as a warned strike");
    assert(named >= 12 && "attack launches carry their attack names");

    // Death returns Vorkath to sleep, ready to be poked again.
    int died_before = g_died;
    vork->current_hp = 1;
    hit_boss(w, vork, 60);
    ticks(w, 1);
    assert(vork->is_dead && g_died == died_before + 1 && vork->respawn_timer == 1);
    assert(!count_locs(w, 32000, false) && "the run's pools leave with it");
    ticks(w, 5);
    a = run_of(w, vork);
    assert(!vork->is_dead && a && a->form == 0 && !a->engaged);
    assert(rc_npc_def_for_npc(w, vork)->id == 8059);
    assert(rc_encounter_wake(w, vork->uid));
}

static void test_chaos_elemental(void) {
    RcWorld *w = setup();
    RcNpc *ce = spawn_boss(w, 2054, BX, BY);
    RcActiveEncounter *a = run_of(w, ce);
    const RcEncounterSpec *spec = spec_of(w, a);
    assert(!strcmp(spec->slug, "chaos_elemental"));
    int madness = -1, confusion = -1;
    for (int i = 0; i < spec->attack_count; i++) {
        if (!strcmp(spec->attacks[i].name, "Madness")) madness = i;
        if (!strcmp(spec->attacks[i].name, "Confusion")) confusion = i;
    }
    assert(madness >= 0 && confusion >= 0);
    RcPlayer *p = &w->player;
    for (int i = 0; i < RC_INVENTORY_SIZE; i++)
        p->inventory[i] = (RcInvSlot){.item_id = 995, .quantity = 1};
    int counts[8] = {0};
    sample(w, ce, false, counts, 3000);
    assert(counts[madness] == 0 && "Madness never fires at a full inventory");

    // Madness: weapon first, one item per free slot.
    p->inventory[27] = (RcInvSlot){.item_id = -1};
    p->equipment[EQUIP_WEAPON] = (RcInvSlot){.item_id = 4587, .quantity = 1};
    p->equipment[EQUIP_SHIELD] = (RcInvSlot){.item_id = 1540, .quantity = 1};
    RcPayloadPlayerDamaged hit = {
        .source_npc_id = (uint32_t)ce->uid, .enc_attack = (uint8_t)(madness + 1),
        .enc_generation = a->generation,
    };
    rc_event_fire(w, RC_EVT_PLAYER_DAMAGED, &hit);
    assert(p->equipment[EQUIP_WEAPON].item_id == -1 && p->inventory[27].item_id == 4587);
    assert(p->equipment[EQUIP_SHIELD].item_id == 1540 && "no space for a second item");

    // Confusion: 2-5 tiles from the footprint.
    for (int round = 0; round < 20; round++) {
        p->x = BX + 1;
        p->y = BY - 1;
        hit.enc_attack = (uint8_t)(confusion + 1);
        rc_event_fire(w, RC_EVT_PLAYER_DAMAGED, &hit);
        int dx = p->x < BX ? BX - p->x : p->x > BX + 2 ? p->x - (BX + 2) : 0;
        int dy = p->y < BY ? BY - p->y : p->y > BY + 2 ? p->y - (BY + 2) : 0;
        int d = dx > dy ? dx : dy;
        assert(d >= 2 && d <= 5);
    }
}

static void test_graardor(void) {
    RcWorld *w = setup();
    RcNpc *gg = spawn_boss(w, 2215, BX, BY);
    RcActiveEncounter *a = run_of(w, gg);
    const RcEncounterSpec *spec = spec_of(w, a);
    assert(spec->support == RC_ENC_SUPPORT_FULL && spec->guard_count == 3);
    assert(rc_encounter_npc_may_engage(w, 2215, BX, BY, 0)
           && "outside his room the room rule does not apply");
    const RcEncounterAttack *atk = NULL;
    uint32_t generation = 0;
    assert(rc_encounter_select_npc_attack(w, (RcNpcId)gg->uid, 6, false, &atk,
                                          &generation) == RC_ENC_SELECT_APPROACH);
    int counts[2] = {0};
    sample(w, gg, true, counts, 3000);
    assert(counts[0] > 1850 && counts[0] < 2150 && "punch 2/3, shockwave 1/3");
    // An accurate shockwave never rolls below 15.
    assert(rc_combat_start_npc_vs_player(w, gg->uid, 0));
    for (int t = 0; t < 200; t++) {
        ticks(w, 1);
        for (int i = 0; i < w->player.num_pending_hits; i++) {
            const RcPendingHit *h = &w->player.pending_hits[i];
            if (h->active && h->source_idx == gg->uid && h->enc_attack == 2
                    && h->accurate)
                assert(h->damage >= 15 && h->damage <= 35);
        }
        w->player.current_hp = w->player.max_hp;
    }
}

// The Bandos room: Graardor and his sergeants at their world spawns.
static void test_graardor_room(void) {
    RcWorld *w = setup();
    rc_test_open_mapsquare(w, 2872, 5358, 2);
    RcPlayer *p = &w->player;
    p->plane = 2;
    p->x = 2862;   // two tiles west of the room
    p->y = 5358;
    RcNpc *gg = spawn_on(w, 2215, 2872, 5358, 2);
    const int sgt_ids[3] = {2216, 2217, 2218};
    const int sgt_xy[3][2] = {{2866, 5358}, {2872, 5352}, {2868, 5362}};
    RcNpc *sgt[3];
    for (int i = 0; i < 3; i++)
        sgt[i] = spawn_on(w, sgt_ids[i], sgt_xy[i][0], sgt_xy[i][1], 2);
    RcActiveEncounter *a = run_of(w, gg);
    assert(a && !run_of(w, sgt[0]) && "bodyguards are not bosses");
    rc_combat_set_multi_combat(w, true);   // the boss rooms are multi-combat

    // Outside the room nobody engages, even when started.
    assert(!rc_encounter_npc_may_engage(w, 2216, 2866, 5358, 2));
    ticks(w, 20);
    assert(gg->target_uid < 0);
    for (int i = 0; i < 3; i++) assert(sgt[i]->target_uid < 0);
    // Inside, the room fights the player; leaving drops it again.
    p->x = 2865;
    p->y = 5360;
    assert(rc_encounter_npc_may_engage(w, 2216, 2866, 5358, 2));
    ticks(w, 20);
    int engaged = gg->target_uid == 0;
    for (int i = 0; i < 3; i++) engaged += sgt[i]->target_uid == 0;
    assert(engaged == 4 && "the aggressive room turns on the player");
    p->x = 2862;
    p->y = 5358;
    ticks(w, 3);
    assert(gg->target_uid < 0);
    for (int i = 0; i < 3; i++) assert(sgt[i]->target_uid < 0);

    // A bodyguard killed while Graardor lives stays dead.
    RcNpc *strongstack = sgt[0];
    int strongstack_uid = strongstack->uid;
    strongstack->current_hp = 1;
    hit_boss(w, strongstack, 5);
    ticks(w, 1);
    assert(strongstack->is_dead && strongstack->respawn_timer > 100000);
    ticks(w, 300);
    assert(strongstack->active && strongstack->is_dead
           && strongstack->uid == strongstack_uid && "held until Graardor returns");
    // Graardor dies and respawns; the held bodyguard returns with him.
    gg->current_hp = 1;
    hit_boss(w, gg, 60);
    ticks(w, 1);
    assert(gg->is_dead && gg->respawn_timer == 150 && "the wiki's 150 ticks");
    int t = 0;
    while (gg->is_dead && t++ < 2000) ticks(w, 1);
    assert(!gg->is_dead && run_of(w, gg) && "Graardor respawned and bound");
    ticks(w, 2);
    assert(!strongstack->is_dead && strongstack->x == 2866 && strongstack->y == 5358);
}

int main(void) {
    test_kalphite_queen();
    test_dragonfire_table();
    test_king_black_dragon();
    test_vorkath();
    test_chaos_elemental();
    test_graardor();
    test_graardor_room();
    test_kq_worker_hatching();
    rc_world_destroy(g_world);
    printf("test_encounter_bosses: ok\n");
    return 0;
}
