#include "../rc-core/api.h"
#include "../rc-core/combat.h"
#include "../rc-core/consumables.h"
#include "../rc-core/combat_formula.h"
#include "../rc-core/npc.h"
#include "../rc-core/combat_hit.h"
#include "../rc-core/skills.h"
#include "../rc-core/world_state.h"
#include "../rc-core/pathfinding.h"
#include "runtime_test_fixture.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static RcWorld *combat_world_with(uint32_t subsystems) {
    memset(g_npc_defs, 0, sizeof(g_npc_defs));
    g_npc_def_count = 1;
    RcNpcDef *def = &g_npc_defs[0];
    def->id = 940000;
    def->size = 1;
    def->hitpoints = 1000;
    def->stats[0] = 99;
    def->stats[1] = 1;
    def->stats[2] = 99;
    def->stats[3] = 1000;
    def->stats[4] = 80;
    def->stats[5] = 70;
    def->max_hit = 1;
    def->attack_speed = 4;
    def->attack_types = 4;
    strcpy(def->options[0], "Talk-to");
    strcpy(def->options[1], "Attack");
    RcWorldConfig cfg = rc_preset_base_only();
    cfg.subsystems = subsystems;
    RcWorld *world = rc_test_world_create_with_defs(&cfg, "combat-foundation", 0);
    assert(world);
    world->player.x = 3208;
    world->player.y = 3208;
    rc_test_open_mapsquare(world, 3208, 3208, 0);
    int index = rc_npc_spawn(world, 0, 3209, 3208, 0);
    assert(index == 0);
    world->npcs[0].disable_wander = true;
    return world;
}

static RcWorld *combat_world(void) {
    return combat_world_with(RC_SUB_COMBAT);
}

static void test_selection_and_cancellation_ownership(void) {
    RcWorld *w = combat_world();
    RcNpc *n = &w->npcs[0];
    assert(rc_combat_start_player_vs_npc(w, 0, n->uid));
    assert(n->target_uid == -1 && "selection must not start opponent combat");
    assert(n->combat.attacker_count == 0);
    rc_combat_tick_player(w);
    assert(n->target_uid == 0 && "a committed attack permits retaliation");
    rc_combat_tick_npc(w, n);
    RcCombatActorRef player = {RC_COMBAT_ACTOR_PLAYER, 0};
    rc_combat_stop_actor(w, player, RC_COMBAT_STATE_CANCELLED);
    assert(n->target_uid == 0 && "cancellation belongs to the selected actor");
    assert(w->player.combat.attacker_count == 1);
    rc_world_destroy(w);
}

static void test_npc_repeat_interval(void) {
    RcWorld *w = combat_world();
    RcNpc *n = &w->npcs[0];
    assert(rc_combat_start_npc_vs_player(w, n->uid, 0));
    for (int tick = 0; tick < 16; tick++) {
        rc_world_tick(w);
        assert(n->attack_count == tick / 4 + 1 && "speed four must repeat every four ticks");
    }
    rc_world_destroy(w);
}

static void test_ordinary_protection_snapshot(void) {
    RcWorld *w = combat_world();
    RcNpc *n = &w->npcs[0];
    assert(rc_combat_start_npc_vs_player(w, n->uid, 0));
    for (int protected = 0; protected < 2; protected++) {
        w->player.active_prayers = protected ? PRAYER_PROTECT_MELEE : 0;
        w->player.current_hp = 1000;
        n->attack_timer = 0;
        rc_combat_tick_npc(w, n);
        assert(w->player.num_pending_hits == 1);
        RcPendingHit *hit = &w->player.pending_hits[0];
        assert(hit->prayer_snapshot == w->player.active_prayers);
        // Isolate protection timing from the random accuracy/damage roll.
        hit->damage = 10;
        hit->apply_tick = w->tick;
        w->player.active_prayers = protected ? 0 : PRAYER_PROTECT_MELEE;
        rc_resolve_player_hits(w);
        assert(w->player.current_hp == (protected ? 1000 : 900));
    }
    rc_world_destroy(w);
}

static void test_interaction_cancels_only_player_attack(void) {
    RcWorld *w = combat_world();
    RcNpc *n = &w->npcs[0];
    assert(rc_combat_start_player_vs_npc(w, 0, n->uid));
    rc_combat_tick_player(w);
    assert(n->target_uid == 0);
    rc_player_interact_npc(w, n->uid, 0);
    rc_world_tick(w);
    assert(w->player.attack_target == -1);
    assert(n->target_uid == 0 && "a new interaction must not cancel the opponent");
    rc_world_destroy(w);
}

static void test_retaliation_setting_and_existing_attack(void) {
    RcWorld *w = combat_world();
    RcNpc *n = &w->npcs[0];
    w->player.current_hp = w->player.max_hp = 10000;
    w->player.auto_retaliate = true;
    assert(rc_combat_set_auto_retaliate(w, false));
    assert(rc_combat_set_auto_retaliate(w, false));
    assert(rc_queue_hit(w->player.pending_hits, &w->player.num_pending_hits,
                        0, 0, COMBAT_MELEE_CRUSH, n->uid, 0, w->tick));
    rc_world_tick(w);
    assert(!w->player.auto_retaliate && !w->player.combat.auto_retaliate
           && "repeated off commands must not toggle retaliation back on");
    for (int i = 0; i < 12; i++) {
        assert(rc_queue_hit(w->player.pending_hits, &w->player.num_pending_hits,
                            1, 0, COMBAT_RANGED, n->uid, 0, w->tick));
        rc_world_tick(w);
        assert(w->player.attack_target == -1 && "off must never initiate an attack");
    }
    assert(rc_combat_start_player_vs_npc(w, 0, n->uid));
    rc_world_tick(w);
    assert(w->player.attack_target == n->uid && "off still permits explicit Attack");
    assert(rc_combat_set_auto_retaliate(w, false));
    rc_world_tick(w);
    assert(w->player.attack_target == n->uid && "off does not cancel existing combat in OSRS");
    assert(!rc_combat_set_auto_retaliate(NULL, false));
    rc_world_destroy(w);
}

static void test_melee_reach(void) {
    RcWorld *w = combat_world();
    RcNpc *n = &w->npcs[0];
    w->map.regions[0].tiles[0][8][8].collision_flags = COL_WALL_E;
    w->map.regions[0].tiles[0][9][8].collision_flags = COL_WALL_W;
    assert(rc_combat_start_player_vs_npc(w, 0, n->uid));
    assert(rc_combat_start_npc_vs_player(w, n->uid, 0));
    rc_combat_tick_player(w);
    rc_combat_tick_npc(w, n);
    assert(w->combat_attack_event_count == 0 && "melee cannot cross a blocked edge");
    w->map.regions[0].tiles[0][8][8].collision_flags = 0;
    w->map.regions[0].tiles[0][9][8].collision_flags = 0;
    n->y++;
    rc_combat_tick_player(w);
    rc_combat_tick_npc(w, n);
    assert(w->combat_attack_event_count == 0 && "ordinary melee requires a side, not a corner");
    n->x = w->player.x;
    n->y = w->player.y;
    rc_combat_tick_npc(w, n);
    assert(n->attack_count == 0 && "overlapping actors must step apart");
    RcRouteTarget target = rc_route_target_rectangle(3210, 3208, 1, 1, 1, 2, false, false);
    target.require_line_of_walk = true;
    assert(rc_route_target_reached(&w->map, 0, 3208, 3208, 1, 1, &target));
    w->map.regions[0].tiles[0][9][8].collision_flags = COL_LOC;
    assert(!rc_route_target_reached(&w->map, 0, 3208, 3208, 1, 1, &target));
    assert(rc_has_los_rect(&w->map, 3208, 3208, 1, 1, 3210, 3208, 1, 1, 0));
    rc_world_destroy(w);
}

static void test_npc_combat_blocked_until_target_moves(void) {
    RcWorld *w = combat_world();
    RcNpc *n = &w->npcs[0];
    w->player.auto_retaliate = false;
    w->map.regions[0].tiles[0][8][8].collision_flags = COL_WALL_E;
    w->map.regions[0].tiles[0][9][8].collision_flags = COL_WALL_W;
    assert(rc_combat_start_npc_vs_player(w, n->uid, 0));
    for (int i = 0; i < 8; i++) {
        rc_world_tick(w);
        assert(n->x == 3209 && n->y == 3208 && n->attack_count == 0
               && "ordinary combat must not find a detour around the wall");
    }
    w->player.y += 2;
    rc_world_tick(w);
    assert(n->x == 3209 && n->y == 3209
           && "pursuit must update when the target moves beside the wall");
    rc_world_tick(w);
    rc_world_tick(w);
    assert(n->attack_count == 1 && "attack resumes after reaching a clear side");
    rc_world_destroy(w);
}

static void test_full_queue_rejects_launch(void) {
    RcWorld *w = combat_world();
    RcNpc *n = &w->npcs[0];
    for (int i = 0; i < RC_MAX_PENDING_HITS; i++)
        assert(rc_queue_hit(n->pending_hits, &n->num_pending_hits,
                            1, 100, COMBAT_RANGED, RC_HIT_SOURCE_PLAYER, 0, 0));
    assert(rc_combat_start_player_vs_npc(w, 0, n->uid));
    uint32_t rng = w->rng_state;
    rc_combat_tick_player(w);
    assert(w->combat_attack_event_count == 0 && "a rejected hit is not an attack");
    assert(w->player.attack_timer == 0 && w->rng_state == rng);
    assert(n->target_uid == -1);
    rc_world_destroy(w);
}

static void test_new_life_and_dead_admission(void) {
    RcWorld *w = combat_world();
    w->player.current_hp = 0;
    w->player.is_dead = true;
    rc_combat_set_multi_combat(w, true);
    assert(!rc_combat_start_player_vs_npc(w, 0, w->npcs[0].uid));
    assert(rc_queue_hit(w->player.pending_hits, &w->player.num_pending_hits,
                        5, 2, COMBAT_RANGED, w->npcs[0].uid, 0, w->tick));
    rc_player_apply_toxin(w, 6, false);
    assert(rc_world_respawn_player(w, 3208, 3208, 0));
    assert(w->player.num_pending_hits == 0 && w->player.toxin_severity == 0);
    assert(w->player.current_prayer_points == w->player.skills.base_level[SKILL_PRAYER] * 10);
    rc_world_tick(w);
    rc_world_tick(w);
    rc_world_tick(w);
    assert(w->player.current_hp == w->player.max_hp);
    rc_world_destroy(w);
}

static void test_unavailable_attack_reasons(void) {
    RcWorld *w = combat_world();
    RcNpc *n = &w->npcs[0];
    assert(rc_combat_start_player_vs_npc(w, 0, n->uid));
    n->player_untargetable = true;
    rc_combat_tick_player(w);
    assert(w->player.attack_target == -1 && w->player.combat.failure_reason);
    n->player_untargetable = false;
    assert(rc_combat_start_player_vs_npc(w, 0, n->uid));
    w->player.attack_style_idx = 2;
    rc_combat_tick_player(w);
    assert(w->player.attack_target == -1 && w->player.combat.failure_reason);
    w->player.attack_style_idx = 0;
    n->x += 2;
    assert(rc_combat_start_player_vs_npc(w, 0, n->uid));
    for (int x = 7; x <= 9; x++)
        for (int y = 7; y <= 9; y++)
            if (x != 8 || y != 8)
                w->map.regions[0].tiles[0][x][y].collision_flags = COL_LOC;
    rc_combat_tick_player(w);
    assert(w->player.attack_target == -1 && w->player.combat.failure_reason);
    assert(w->combat_attack_event_count == 0 && n->num_pending_hits == 0);
    rc_world_destroy(w);
}

static int selections;
static RcCombatStyle select_melee(RcWorld *w, RcNpc *n,
                                  const RcPlayer *p, RcCombatStyle style) {
    (void)w; (void)n; (void)p; (void)style;
    selections++;
    return COMBAT_MELEE_CRUSH;
}

static int two_extra_hits(const RcWorld *w, const RcNpc *n) {
    (void)w; (void)n;
    return 2;
}

static void queue_extra_hits(RcWorld *w, RcNpc *n, RcCombatStyle style) {
    for (int i = 0; i < 2; i++)
        assert(rc_queue_hit(w->player.pending_hits, &w->player.num_pending_hits,
                            1, 1, style, n->uid, 0, w->tick));
}

static void test_prepared_attack_and_batch_capacity(void) {
    RcWorld *w = combat_world();
    RcNpc *n = &w->npcs[0];
    RcCombatContentHooks hooks = {.select_npc_style = select_melee,
        .extra_npc_hit_count = two_extra_hits, .after_npc_swing = queue_extra_hits};
    rc_combat_register_content_hooks(w, &hooks);
    selections = 0;
    for (int i = 0; i < RC_MAX_PENDING_HITS - 2; i++)
        assert(rc_queue_hit(w->player.pending_hits, &w->player.num_pending_hits,
                            1, 100, COMBAT_RANGED, n->uid, 0, w->tick));
    assert(rc_combat_start_npc_vs_player(w, n->uid, 0));
    uint32_t rng = w->rng_state;
    rc_combat_tick_npc(w, n);
    assert(selections == 0 && n->attack_count == 0 && w->rng_state == rng);
    assert(n->combat.failure_reason && "the whole multi-hit attack must fit");
    w->player.num_pending_hits--;
    n->x += 2;
    assert(rc_combat_start_npc_vs_player(w, n->uid, 0));
    for (int i = 0; i < 3; i++) rc_combat_tick_npc(w, n);
    assert(selections == 1 && n->attack_count == 0 && n->combat.attack_prepared);
    n->x -= 2;
    rc_combat_tick_npc(w, n);
    assert(selections == 1 && n->attack_count == 1);
    assert(w->player.num_pending_hits == RC_MAX_PENDING_HITS);
    assert(w->combat_attack_event_count == 1);
    n->x += 2;
    for (int i = 0; i < 3; i++) rc_combat_tick_npc(w, n);
    assert(selections == 1 && "cooldown must not advance attack selection");
    assert(n->route_mode == RC_NPC_ROUTE_CHASE);
    rc_world_destroy(w);
}

static void test_live_stats_accuracy_and_fractional_xp(void) {
    RcWorld *w = combat_world();
    RcNpc *n = &w->npcs[0];
    int attack = rc_npc_offensive_roll(n, COMBAT_MELEE_CRUSH);
    n->stats[0] = 1;
    assert(rc_npc_offensive_roll(n, COMBAT_MELEE_CRUSH) < attack);
    n->stats[1] = 1;
    n->stats[5] = 80;
    assert(rc_calc_magic(&w->player, n, 10).defence_roll == 89 * 64);
    assert(rc_calc_melee(&w->player, n).defence_roll == 10 * 64);
    for (int a = -5; a <= 5; a++) {
        for (int d = -5; d <= 5; d++) {
            float chance = rc_hit_chance(a, d);
            assert(chance >= 0 && chance <= 1);
            assert(rc_hit_chance_scaled(a, d) == (int)(chance * 10000));
        }
    }
    assert(rc_hit_chance_scaled(10, -10) == 9898);
    assert(rc_hit_chance_scaled(-10, 10) == 0);
    assert(rc_hit_chance_scaled(-10, -20) == 7368);
    assert(rc_hit_chance_scaled(-20, -10) == 2105);
    RcCombatCalc calc = {.hit_chance = 1, .max_hit = 1};
    bool npc_zero = false;
    for (uint32_t seed = 1; seed <= 32; seed++) {
        uint32_t a = seed, b = seed;
        RcCombatRoll player = rc_roll_attack(&calc, &a, true);
        RcCombatRoll npc = rc_roll_attack(&calc, &b, false);
        assert(player.accurate && player.damage == 1);
        assert(npc.accurate);
        npc_zero |= npc.damage == 0;
    }
    assert(npc_zero);
    calc.max_hit = 0;
    RcCombatRoll zero = rc_roll_attack(&calc, &w->rng_state, true);
    assert(zero.accurate && zero.damage == 0);
    int magic = w->player.skills.xp[SKILL_MAGIC];
    int defence = w->player.skills.xp[SKILL_DEFENCE];
    int hp = w->player.skills.xp[SKILL_HITPOINTS];
    for (int i = 0; i < 100; i++)
        rc_award_player_combat_xp(w, 1, RC_COMBAT_XP_MAGIC | RC_COMBAT_XP_DEFENCE);
    assert(w->player.skills.xp[SKILL_MAGIC] == magic + 133);
    assert(w->player.skills.xp[SKILL_DEFENCE] == defence + 100);
    assert(w->player.skills.xp[SKILL_HITPOINTS] == hp + 133);
    assert(w->player.skills.xp_hundredths[SKILL_MAGIC] == 0);
    rc_add_xp_hundredths(&w->player.skills, SKILL_STRENGTH, INT64_MAX);
    assert(w->player.skills.xp[SKILL_STRENGTH] == 200000000);
    assert(w->player.skills.xp_hundredths[SKILL_STRENGTH] == 0);
    rc_world_destroy(w);
}

static void test_launch_xp_not_impact_stance(void) {
    RcWorld *w = combat_world();
    RcNpc *n = &w->npcs[0];
    n->current_hp = 2;
    n->force_player_max_hit = true;
    w->player.skills.boosted_level[SKILL_STRENGTH] = 99;
    int attack = w->player.skills.xp[SKILL_ATTACK];
    int strength = w->player.skills.xp[SKILL_STRENGTH];
    assert(rc_combat_start_player_vs_npc(w, 0, n->uid));
    rc_combat_tick_player(w);
    assert(n->current_hp == 2 && w->player.skills.xp[SKILL_ATTACK] == attack + 8);
    assert(rc_queue_hit(n->pending_hits, &n->num_pending_hits,
                        5, 0, COMBAT_MELEE_CRUSH, RC_HIT_SOURCE_PLAYER, 0, w->tick));
    w->player.attack_style_idx = 1;
    rc_world_tick(w);
    assert(n->current_hp == 0);
    assert(n->combat.recent_hit_count == 1 && "later hits must not hit a corpse");
    assert(w->player.skills.xp[SKILL_ATTACK] == attack + 8);
    assert(w->player.skills.xp[SKILL_STRENGTH] == strength);
    rc_world_destroy(w);
}

static void observe_damage(RcWorld *w, int event, const void *payload, void *ctx) {
    assert(event == RC_EVT_PLAYER_DAMAGED);
    const RcPayloadPlayerDamaged *hit = payload;
    assert(hit->current_hp == w->player.current_hp && "callbacks observe post-hit HP");
    *(int *)ctx += hit->damage_tenths;
}

static void test_damage_owner_regen_and_projectile_lifetime(void) {
    RcWorld *w = combat_world();
    int lost = 0;
    assert(rc_event_subscribe(w, RC_EVT_PLAYER_DAMAGED, observe_damage, &lost) == 0);
    w->player.current_hp = 15;
    RcPendingHit status = {.source_idx = RC_HIT_SOURCE_STATUS,
        .flags = RC_HIT_SUPPRESS_ENCOUNTER_EFFECTS};
    assert(rc_combat_apply_player_hit(w, &status, 99) == 2);
    assert(lost == 15 && w->player.current_hp == 0);
    for (int i = 0; i < 100; i++) rc_stat_restore_tick(&w->player);
    assert(w->player.current_hp == 0 && "regeneration cannot resurrect");
    w->player.is_dead = true;
    assert(rc_world_respawn_player(w, 3208, 3208, 0));
    w->player.current_hp -= 10;
    for (int i = 0; i < 100; i++) rc_stat_restore_tick(&w->player);
    assert(w->player.current_hp == w->player.max_hp);
    w->player.current_hp += 5;
    w->player.skills.boosted_level[SKILL_ATTACK] += 2;
    w->player.hp_regen_counter = 99;
    // Boost decay has its own timer; changing HP regeneration must not reset it.
    w->player.stat_boost_counter = 99;
    rc_stat_restore_tick(&w->player);
    assert(w->player.current_hp == w->player.max_hp);
    assert(w->player.skills.boosted_level[SKILL_ATTACK]
           == w->player.skills.base_level[SKILL_ATTACK] + 1);
    RcNpcId uid = w->npcs[0].uid;
    assert(rc_queue_hit(w->player.pending_hits, &w->player.num_pending_hits,
                        1, 1, COMBAT_MELEE_CRUSH, uid, 0, w->tick));
    assert(rc_queue_hit(w->player.pending_hits, &w->player.num_pending_hits,
                        1, 1, COMBAT_RANGED, uid, 0, w->tick));
    assert(rc_npc_remove(w, uid));
    assert(w->player.num_pending_hits == 1 && "launched ranged hits survive source removal");
    w->tick++;
    rc_resolve_player_hits(w);
    assert(w->player.current_hp == w->player.max_hp - 10);
    assert(lost == 25);
    rc_world_destroy(w);
}

static void test_dormant_new_life_and_independent_threat_expiry(void) {
    RcWorld *w = combat_world();
    RcNpc *n = &w->npcs[0];
    n->spawn_key = 123;
    n->current_hp = 7;
    assert(rc_combat_start_npc_vs_player(w, n->uid, 0));
    RcRouteTarget route = rc_route_target_rectangle(3212, 3208, 1, 1, 1, 1, false, false);
    assert(rc_npc_route_request(w, n, &route, RC_NPC_ROUTE_CHASE, false));
    rc_combat_actor_register_attacker(&n->combat, (RcCombatActorRef){RC_COMBAT_ACTOR_PLAYER, 0});
    assert(rc_world_state_save_npcs(w) == 1);
    w->player.is_dead = true;
    rc_combat_reset_player_life(w);
    assert(rc_world_state_restore_npcs(w) == 1);
    assert(n->current_hp == 7 && n->target_uid == -1 && n->combat.attacker_count == 0);
    assert(n->route_mode == RC_NPC_ROUTE_NONE);
    RcCombatActorState state = {0};
    rc_combat_actor_register_attacker(&state, (RcCombatActorRef){RC_COMBAT_ACTOR_NPC, 1});
    for (int i = 0; i < 5; i++) rc_combat_tick_actor_threat(&state);
    rc_combat_actor_register_attacker(&state, (RcCombatActorRef){RC_COMBAT_ACTOR_NPC, 2});
    for (int i = 0; i < 5; i++) rc_combat_tick_actor_threat(&state);
    assert(state.attacker_count == 1 && state.attackers[0].uid == 2);
    for (int i = 0; i < 5; i++) rc_combat_tick_actor_threat(&state);
    assert(state.attacker_count == 0);
    rc_world_destroy(w);
}

static void test_status_damage_and_poison_capacity(void) {
    RcWorld *w = combat_world();
    int lost = 0;
    assert(rc_event_subscribe(w, RC_EVT_PLAYER_DAMAGED, observe_damage, &lost) == 0);
    w->player.current_hp = w->player.max_hp = 1000;
    rc_player_apply_toxin(w, 3, false);
    rc_player_apply_toxin(w, 6, true);
    w->tick = w->player.toxin_tick;
    rc_combat_tick_player_status(w);
    assert(lost == 60 && w->player.current_hp == 940);
    assert(w->player.toxin_venom && w->player.toxin_severity == 8);
    rc_combat_tick_player_status(w);
    assert(lost == 60 && "one toxin owns damage and its repeat timer");
    RcNpc *n = &w->npcs[0];
    n->poison_damage = 3;
    for (int i = 0; i < RC_MAX_PENDING_HITS; i++)
        assert(rc_queue_hit(n->pending_hits, &n->num_pending_hits,
                            1, 100, COMBAT_RANGED, RC_HIT_SOURCE_PLAYER, 0, 0));
    rc_npc_status_tick(w, n);
    assert(n->poison_damage == 3 && n->poison_tick_counter == 0);
    n->num_pending_hits--;
    rc_npc_status_tick(w, n);
    assert(n->poison_damage == 2 && n->num_pending_hits == RC_MAX_PENDING_HITS);
    assert(n->pending_hits[RC_MAX_PENDING_HITS - 1].damage == 3);
    rc_world_destroy(w);
}

static void test_encounter_full_width_identity_and_targeting(void) {
    RcWorld *w = combat_world_with(RC_SUB_COMBAT | RC_SUB_ENCOUNTER);
    static RcEncounterSpec spec;
    memset(&spec, 0, sizeof(spec));
    strcpy(spec.slug, "full_width");
    spec.support = RC_ENC_SUPPORT_FULL;
    spec.form_count = 1;
    spec.forms[0].npc_id = 940000;
    spec.forms[0].binds_on_spawn = true;
    spec.attack_count = 1;
    spec.attacks[0].style = COMBAT_MELEE_CRUSH;
    spec.attacks[0].max_hit = 1;
    spec.attacks[0].weight_adjacent = 1;
    spec.phase_count = 1;
    spec.phases[0].allowed_attack_mask = 1;
    spec.phases[0].player_targetable = false;
    assert(rc_encounter_use_specs(w, &spec, 1) == 0);
    // The fixture boss spawned before the specs were installed binds too.
    assert(rc_encounter_run_for_npc(w, (RcNpcId)w->npcs[0].uid) >= 0);
    w->next_npc_uid = 70000;
    int index = rc_npc_spawn(w, 0, 3208, 3209, 0);
    assert(index >= 0);
    RcNpc *n = &w->npcs[index];
    assert(n->uid == 70000);
    int run = rc_encounter_run_for_npc(w, (RcNpcId)n->uid);
    assert(run >= 0 && w->encounter.active[run].boss_id == 70000u);
    n->current_hp = 50;
    assert(!rc_encounter_player_can_target_npc(w, n->uid));
    assert(rc_combat_start_player_vs_npc(w, 0, n->uid));
    rc_combat_tick_player(w);
    assert(n->num_pending_hits == 0 && "untargetable phase refuses the launch");
    spec.phases[0].player_targetable = true;
    w->encounter.active[run].run_flags |= RC_ENC_RUN_BOSS_IMMUNE;
    assert(rc_encounter_scale_player_damage(w, n->uid, COMBAT_MELEE_CRUSH, 5) == 0);
    w->encounter.active[run].run_flags = 0;
    w->encounter.active[run].damage_taken_pct = 50;
    assert(rc_encounter_scale_player_damage(w, n->uid, COMBAT_MELEE_CRUSH, 9) == 4);
    assert(n->current_hp == 50);
    rc_world_destroy(w);
}

static void test_projectile_cannot_target_reborn_source(void) {
    RcWorld *w = combat_world();
    RcNpc *n = &w->npcs[0];
    assert(rc_queue_hit(w->player.pending_hits, &w->player.num_pending_hits,
                        1, 1, COMBAT_RANGED, n->uid, 0, w->tick));
    n->is_dead = true;
    n->current_hp = 0;
    rc_npc_reset_life(w, n);
    assert(w->player.num_pending_hits == 1);
    assert(w->player.pending_hits[0].flags & RC_HIT_SOURCE_EXPIRED);
    int hp = w->player.current_hp;
    w->tick++;
    rc_resolve_player_hits(w);
    assert(w->player.current_hp == hp - 10);
    assert(w->player.attack_target == -1 && n->target_uid == -1);
    rc_world_destroy(w);
}

int main(void) {
    test_ordinary_protection_snapshot();
    test_selection_and_cancellation_ownership();
    test_npc_repeat_interval();
    test_interaction_cancels_only_player_attack();
    test_retaliation_setting_and_existing_attack();
    test_melee_reach();
    test_npc_combat_blocked_until_target_moves();
    test_full_queue_rejects_launch();
    test_new_life_and_dead_admission();
    test_unavailable_attack_reasons();
    test_prepared_attack_and_batch_capacity();
    test_live_stats_accuracy_and_fractional_xp();
    test_launch_xp_not_impact_stance();
    test_damage_owner_regen_and_projectile_lifetime();
    test_status_damage_and_poison_capacity();
    test_dormant_new_life_and_independent_threat_expiry();
    test_encounter_full_width_identity_and_targeting();
    test_projectile_cannot_target_reborn_source();
    puts("combat foundation: ownership, cadence, reach, capacity, lifecycle passed");
    return 0;
}
