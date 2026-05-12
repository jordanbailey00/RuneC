#include "../rc-core/api.h"
#include "../rc-core/combat.h"
#include "../rc-core/npc.h"

#include <assert.h>
#include <string.h>

static int spawn_phase3_npc(RcWorld *world, int cache_id, int dx, int dy,
                            int size, int attack_types) {
    int def_idx = g_npc_def_count++;
    assert(def_idx < RC_MAX_NPC_DEFS);
    memset(&g_npc_defs[def_idx], 0, sizeof(g_npc_defs[def_idx]));
    g_npc_defs[def_idx].id = cache_id;
    strcpy(g_npc_defs[def_idx].name, "Phase 3 Movement Guard");
    g_npc_defs[def_idx].size = size;
    g_npc_defs[def_idx].combat_level = 2;
    g_npc_defs[def_idx].hitpoints = 200;
    g_npc_defs[def_idx].stats[0] = 99;
    g_npc_defs[def_idx].stats[1] = 1;
    g_npc_defs[def_idx].stats[2] = 99;
    g_npc_defs[def_idx].stats[3] = 200;
    g_npc_defs[def_idx].max_hit = 1;
    g_npc_defs[def_idx].attack_speed = 4;
    g_npc_defs[def_idx].attack_types = attack_types;
    strcpy(g_npc_defs[def_idx].options[1], "Attack");
    int idx = rc_npc_spawn(world, def_idx, world->player.x + dx,
                           world->player.y + dy, world->player.plane);
    assert(idx >= 0);
    world->npcs[idx].wander_timer = 999999;
    return idx;
}

static RcWorld *phase3_world(void) {
    RcWorldConfig cfg = rc_preset_base_only();
    cfg.subsystems = RC_SUB_COMBAT;
    cfg.seed = 12345;
    RcWorld *world = rc_world_create_config(&cfg);
    assert(world);
    for (int i = 0; i < SKILL_COUNT; i++) {
        world->player.skills.base_level[i] = 99;
        world->player.skills.boosted_level[i] = 99;
    }
    world->player.equipment_bonuses[EQ_CRUSH_ATK] = 10000;
    world->player.equipment_bonuses[EQ_STR] = 100;
    rc_player_set_attack_style(world, 0);
    return world;
}

static void test_player_in_range_flag_and_facing_for_large_npc(void) {
    RcWorld *world = phase3_world();
    int npc_idx = spawn_phase3_npc(world, 930300, 1, 0, 3, 0x04);
    RcNpc *npc = &world->npcs[npc_idx];

    assert(rc_combat_start_player_vs_npc(world, 0, npc->uid));
    rc_combat_tick_player(world);

    assert(world->player.facing_entity == npc->uid);
    assert(world->player.facing_x == npc->x);
    assert(world->player.facing_y == world->player.y);
    assert(world->player.combat.flags & RC_COMBAT_STATE_IN_RANGE);
    assert(world->player.combat.attack_range == 1);
    assert(world->player.combat.distance_to_target == 1);
    assert(world->player.combat.line_of_sight == 1);

    rc_world_destroy(world);
}

static void test_player_routes_toward_valid_attack_tile_when_out_of_range(void) {
    RcWorld *world = phase3_world();
    int npc_idx = spawn_phase3_npc(world, 930301, 5, 0, 2, 0x04);
    RcNpc *npc = &world->npcs[npc_idx];

    assert(rc_combat_start_player_vs_npc(world, 0, npc->uid));
    rc_combat_tick_player(world);

    assert(!(world->player.combat.flags & RC_COMBAT_STATE_IN_RANGE));
    assert(world->player.combat.distance_to_target > 1);
    assert(world->player.route_len > 0);
    int rx = world->player.route_x[world->player.route_len - 1];
    int ry = world->player.route_y[world->player.route_len - 1];
    assert(!(rx >= npc->x && rx < npc->x + 2 &&
             ry >= npc->y && ry < npc->y + 2));
    int tx = rx < npc->x ? npc->x : (rx >= npc->x + 2 ? npc->x + 1 : rx);
    int ty = ry < npc->y ? npc->y : (ry >= npc->y + 2 ? npc->y + 1 : ry);
    int dx = rx > tx ? rx - tx : tx - rx;
    int dy = ry > ty ? ry - ty : ty - ry;
    assert((dx > dy ? dx : dy) == 1);
    assert(world->player.facing_entity == npc->uid);

    rc_world_destroy(world);
}

static void test_player_under_large_target_steps_out_before_attacking(void) {
    RcWorld *world = phase3_world();
    int npc_idx = spawn_phase3_npc(world, 930302, 0, 0, 3, 0x04);
    RcNpc *npc = &world->npcs[npc_idx];

    assert(rc_combat_start_player_vs_npc(world, 0, npc->uid));
    rc_combat_tick_player(world);

    assert(!(world->player.combat.flags & RC_COMBAT_STATE_IN_RANGE));
    assert(world->player.combat.distance_to_target == 0);
    assert(world->player.route_len > 0);
    assert(world->player.attack_anim_timer == 0);

    rc_world_destroy(world);
}

static void test_npc_chases_faces_and_sets_range_state(void) {
    RcWorld *world = phase3_world();
    int npc_idx = spawn_phase3_npc(world, 930303, 4, 0, 1, 0x04);
    RcNpc *npc = &world->npcs[npc_idx];
    int start_x = npc->x;

    assert(rc_combat_start_npc_vs_player(world, npc->uid, 0));
    rc_combat_tick_npc(world, npc);

    assert(npc->x < start_x);
    assert(npc->facing_entity == 0);
    assert(npc->facing_x == world->player.x);
    assert(npc->facing_y == world->player.y);
    assert(!(npc->combat.flags & RC_COMBAT_STATE_IN_RANGE));
    assert(npc->combat.attack_range == 1);
    assert(npc->combat.distance_to_target > 1);

    rc_world_destroy(world);
}

int main(void) {
    test_player_in_range_flag_and_facing_for_large_npc();
    test_player_routes_toward_valid_attack_tile_when_out_of_range();
    test_player_under_large_target_steps_out_before_attacking();
    test_npc_chases_faces_and_sets_range_state();
    return 0;
}
