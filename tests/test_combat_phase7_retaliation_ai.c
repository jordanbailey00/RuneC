#include "../rc-core/api.h"
#include "../rc-core/combat.h"
#include "../rc-core/combat_hit.h"
#include "../rc-core/npc.h"

#include <assert.h>
#include <string.h>

static int add_phase7_npc_def(int npc_id, const char *name, int hp,
                              int max_hit, int attack_speed,
                              bool aggressive, int aggro_range) {
    int def_idx = g_npc_def_count++;
    assert(def_idx < RC_MAX_NPC_DEFS);
    memset(&g_npc_defs[def_idx], 0, sizeof(g_npc_defs[def_idx]));
    g_npc_defs[def_idx].id = npc_id;
    strcpy(g_npc_defs[def_idx].name, name);
    g_npc_defs[def_idx].size = 1;
    g_npc_defs[def_idx].combat_level = max_hit > 0 ? 50 : -1;
    g_npc_defs[def_idx].hitpoints = hp;
    g_npc_defs[def_idx].stats[0] = 99;
    g_npc_defs[def_idx].stats[1] = 1;
    g_npc_defs[def_idx].stats[2] = 99;
    g_npc_defs[def_idx].stats[3] = hp;
    g_npc_defs[def_idx].stats[4] = 99;
    g_npc_defs[def_idx].stats[5] = 99;
    g_npc_defs[def_idx].max_hit = max_hit;
    g_npc_defs[def_idx].attack_speed = attack_speed;
    g_npc_defs[def_idx].attack_types = 0x04;
    g_npc_defs[def_idx].aggressive = aggressive;
    g_npc_defs[def_idx].aggro_range = aggro_range;
    g_npc_defs[def_idx].wander_range = aggro_range > 0 ? aggro_range : 5;
    g_npc_defs[def_idx].respawn_ticks = 8;
    strcpy(g_npc_defs[def_idx].options[1], "Attack");
    return def_idx;
}

static RcWorld *phase7_world(void) {
    RcWorldConfig cfg = rc_preset_base_only();
    cfg.subsystems = RC_SUB_COMBAT;
    cfg.seed = 777;
    RcWorld *world = rc_world_create_config(&cfg);
    assert(world);
    world->player.x = 3200;
    world->player.y = 3200;
    world->player.plane = 0;
    world->player.current_hp = 990;
    world->player.max_hp = 990;
    world->player.auto_retaliate = true;
    for (int i = 0; i < SKILL_COUNT; i++) {
        world->player.skills.base_level[i] = 99;
        world->player.skills.boosted_level[i] = 99;
    }
    rc_player_set_attack_style(world, 0);
    return world;
}

static void test_player_hit_causes_npc_retaliation_and_threat_tracking(void) {
    RcWorld *world = phase7_world();
    int def_idx = add_phase7_npc_def(9701, "Phase 7 Retaliator",
                                     30, 3, 4, false, 0);
    int npc_idx = rc_npc_spawn(world, def_idx, 3201, 3200, 0);
    assert(npc_idx >= 0);
    RcNpc *npc = &world->npcs[npc_idx];

    rc_queue_hit_meta(npc->pending_hits, &npc->num_pending_hits,
                      1, 0, COMBAT_MELEE_CRUSH, -1, 0u,
                      world->tick, 0, 10, 0);
    rc_world_tick(world);

    assert(npc->target_uid == 0);
    assert(rc_combat_actor_is_under_attack(&npc->combat));
    assert(rc_combat_actor_attacker_count(&npc->combat) == 1);
    assert(npc->combat.primary_attacker.kind == RC_COMBAT_ACTOR_PLAYER);
    assert(npc->combat.primary_attacker.uid == 0);
    assert(rc_combat_actor_is_under_attack(&world->player.combat));
    assert(world->player.combat.primary_attacker.kind == RC_COMBAT_ACTOR_NPC);
    assert(world->player.combat.primary_attacker.uid == npc->uid);

    rc_world_tick(world);
    assert(world->player.combat.recent_hit_count > 0);
    assert(world->player.combat.recent_hits[
           world->player.combat.recent_hit_count - 1].source_uid == npc->uid);

    rc_world_destroy(world);
}

static void test_single_combat_blocks_second_npc_until_multi_enabled(void) {
    RcWorld *world = phase7_world();
    int def_idx = add_phase7_npc_def(9702, "Phase 7 Single",
                                     30, 3, 4, false, 0);
    int a_idx = rc_npc_spawn(world, def_idx, 3201, 3200, 0);
    int b_idx = rc_npc_spawn(world, def_idx, 3200, 3201, 0);
    assert(a_idx >= 0 && b_idx >= 0);
    RcNpc *a = &world->npcs[a_idx];
    RcNpc *b = &world->npcs[b_idx];

    assert(!rc_combat_is_multi_combat(world));
    assert(rc_combat_start_npc_vs_player(world, a->uid, 0));
    assert(!rc_combat_start_npc_vs_player(world, b->uid, 0));
    assert(a->target_uid == 0);
    assert(b->target_uid == -1);
    assert(rc_combat_actor_attacker_count(&world->player.combat) == 1);

    rc_combat_set_multi_combat(world, true);
    assert(rc_combat_is_multi_combat(world));
    assert(rc_combat_start_npc_vs_player(world, b->uid, 0));
    assert(b->target_uid == 0);
    assert(rc_combat_actor_attacker_count(&world->player.combat) == 2);

    rc_world_destroy(world);
}

static void test_aggressive_npc_acquires_target_and_respects_leash(void) {
    RcWorld *world = phase7_world();
    int aggro_def = add_phase7_npc_def(9703, "Phase 7 Aggressive",
                                       30, 2, 4, true, 4);
    int passive_def = add_phase7_npc_def(9704, "Phase 7 Passive",
                                         30, 2, 4, false, 4);
    int aggro_idx = rc_npc_spawn(world, aggro_def, 3203, 3200, 0);
    int passive_idx = rc_npc_spawn(world, passive_def, 3203, 3201, 0);
    assert(aggro_idx >= 0 && passive_idx >= 0);
    RcNpc *aggro = &world->npcs[aggro_idx];
    RcNpc *passive = &world->npcs[passive_idx];

    rc_world_tick(world);
    assert(aggro->target_uid == 0);
    assert(passive->target_uid == -1);
    assert(aggro->combat.aggro_state == 1);

    world->player.x = aggro->spawn_x + 30;
    world->player.y = aggro->spawn_y + 30;
    rc_world_tick(world);
    assert(aggro->target_uid == -1);
    assert(aggro->combat.leash_state == 1);

    rc_world_destroy(world);
}

static void test_passive_wander_is_not_combat_leashed(void) {
    RcWorld *world = phase7_world();
    int def_idx = add_phase7_npc_def(9705, "Phase 7 Passive Wander",
                                     30, 0, 0, false, 4);
    g_npc_defs[def_idx].wander_range = 0;
    int npc_idx = rc_npc_spawn(world, def_idx, 3202, 3200, 0);
    assert(npc_idx >= 0);
    RcNpc *npc = &world->npcs[npc_idx];
    npc->spawn_x = 3200;
    npc->spawn_y = 3200;
    npc->prev_x = npc->x;
    npc->prev_y = npc->y;
    npc->combat.leash_state = 0;

    rc_world_tick(world);
    assert(npc->x == 3202);
    assert(npc->y == 3200);
    assert(npc->prev_x == 3202);
    assert(npc->prev_y == 3200);
    assert(npc->combat.leash_state == 0);

    npc->combat.leash_state = 1;
    rc_world_tick(world);
    assert(npc->x == 3201);
    assert(npc->y == 3200);
    assert(npc->prev_x == 3202);
    assert(npc->prev_y == 3200);
    assert(npc->combat.leash_state == 1);

    rc_world_destroy(world);
}

int main(void) {
    test_player_hit_causes_npc_retaliation_and_threat_tracking();
    test_single_combat_blocks_second_npc_until_multi_enabled();
    test_aggressive_npc_acquires_target_and_respects_leash();
    test_passive_wander_is_not_combat_leashed();
    return 0;
}
