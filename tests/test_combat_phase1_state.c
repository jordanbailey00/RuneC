#include "../rc-core/api.h"
#include "../rc-core/combat.h"
#include "../rc-core/npc.h"

#include <assert.h>
#include <string.h>

static int spawn_phase1_npc(RcWorld *world, int cache_id, int dx) {
    int def_idx = g_npc_def_count++;
    assert(def_idx < RC_MAX_NPC_DEFS);
    memset(&g_npc_defs[def_idx], 0, sizeof(g_npc_defs[def_idx]));
    g_npc_defs[def_idx].id = cache_id;
    strcpy(g_npc_defs[def_idx].name, "Phase 1 Combat State Guard");
    g_npc_defs[def_idx].size = 2;
    g_npc_defs[def_idx].combat_level = 2;
    g_npc_defs[def_idx].hitpoints = 20;
    g_npc_defs[def_idx].stats[0] = 1;
    g_npc_defs[def_idx].stats[1] = 1;
    g_npc_defs[def_idx].stats[2] = 1;
    g_npc_defs[def_idx].stats[3] = 20;
    g_npc_defs[def_idx].max_hit = 1;
    g_npc_defs[def_idx].attack_speed = 4;
    g_npc_defs[def_idx].attack_types = 0x04;
    strcpy(g_npc_defs[def_idx].options[1], "Attack");
    int idx = rc_npc_spawn(world, def_idx, world->player.x + dx,
                           world->player.y, world->player.plane);
    assert(idx >= 0);
    return idx;
}

static RcWorld *phase1_world(void) {
    RcWorldConfig cfg = rc_preset_base_only();
    cfg.subsystems = RC_SUB_COMBAT;
    cfg.seed = 12345;
    RcWorld *world = rc_world_create_config(&cfg);
    assert(world);
    return world;
}

static void test_start_and_stop_mirror_legacy_fields(void) {
    RcWorld *world = phase1_world();
    int npc_idx = spawn_phase1_npc(world, 910100, 1);
    RcNpc *npc = &world->npcs[npc_idx];

    assert(!rc_combat_actor_has_target(&world->player.combat));
    assert(!rc_combat_actor_has_target(&npc->combat));
    assert(world->player.combat.target.uid == -1);
    assert(npc->combat.target.uid == -1);

    assert(rc_combat_start_player_vs_npc(world, 0, npc->uid));
    assert(world->player.attack_target == npc->uid);
    assert(world->player.attack_target_def_id == 910100);
    assert(npc->target_uid == 0);

    assert(rc_combat_actor_has_target(&world->player.combat));
    assert(world->player.combat.target.kind == RC_COMBAT_ACTOR_NPC);
    assert(world->player.combat.target.uid == npc->uid);
    assert(world->player.combat.target.definition_id == 910100);
    assert(world->player.combat.target.tile_x == npc->x);
    assert(world->player.combat.target.footprint_width == 2);
    assert(world->player.combat.flags & RC_COMBAT_STATE_ACTIVE);

    assert(rc_combat_actor_has_target(&npc->combat));
    assert(npc->combat.target.kind == RC_COMBAT_ACTOR_PLAYER);
    assert(npc->combat.target.uid == 0);
    assert(npc->combat.target.tile_x == world->player.x);

    RcCombatActorRef player_ref = {
        .kind = RC_COMBAT_ACTOR_PLAYER,
        .uid = 0,
    };
    rc_combat_stop_actor(world, player_ref, RC_COMBAT_STATE_CANCELLED);
    assert(world->player.attack_target == -1);
    assert(world->player.attack_target_def_id == -1);
    assert(!rc_combat_actor_has_target(&world->player.combat));
    assert(world->player.combat.flags == RC_COMBAT_STATE_CANCELLED);

    RcCombatActorRef npc_ref = {
        .kind = RC_COMBAT_ACTOR_NPC,
        .uid = npc->uid,
    };
    rc_combat_stop_actor(world, npc_ref, RC_COMBAT_STATE_CANCELLED);
    assert(npc->target_uid == -1);
    assert(!rc_combat_actor_has_target(&npc->combat));
    assert(npc->combat.flags == RC_COMBAT_STATE_CANCELLED);

    rc_world_destroy(world);
}

static void test_style_and_toggle_state(void) {
    RcWorld *world = phase1_world();
    assert(world->player.auto_retaliate);
    assert(!world->player.combat.special_pending);

    rc_combat_set_player_style(world, 2);
    assert(world->player.attack_style_idx == 2);
    assert(world->player.combat.selected_style_idx == 2);
    assert(world->player.combat.stance == world->player.attack_stance);
    assert(world->player.combat.xp_mask == world->player.combat_xp_mask);

    rc_combat_toggle_auto_retaliate(world);
    assert(!world->player.auto_retaliate);
    assert(!world->player.combat.auto_retaliate);

    rc_combat_toggle_special(world);
    assert(world->player.combat.special_pending);
    rc_combat_toggle_special(world);
    assert(!world->player.combat.special_pending);

    rc_world_destroy(world);
}

static void test_legacy_tick_syncs_state(void) {
    RcWorld *world = phase1_world();
    int npc_idx = spawn_phase1_npc(world, 910101, 1);
    RcNpc *npc = &world->npcs[npc_idx];

    world->player.attack_target = npc->uid;
    world->player.attack_target_def_id = 910101;
    rc_combat_tick_player(world);
    assert(rc_combat_actor_has_target(&world->player.combat));
    assert(world->player.combat.target.uid == npc->uid);

    npc->target_uid = 0;
    rc_combat_tick_npc(world, npc);
    assert(rc_combat_actor_has_target(&npc->combat));
    assert(npc->combat.target.uid == 0);

    rc_world_destroy(world);
}

int main(void) {
    test_start_and_stop_mirror_legacy_fields();
    test_style_and_toggle_state();
    test_legacy_tick_syncs_state();
    return 0;
}
