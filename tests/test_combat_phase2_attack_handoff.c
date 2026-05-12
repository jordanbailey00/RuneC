#include "../rc-core/api.h"
#include "../rc-core/combat.h"
#include "../rc-core/npc.h"

#include <assert.h>
#include <string.h>

typedef struct {
    int calls;
} Phase2HandlerCtx;

static RcInteractionHandlerResult phase2_complete_handler(
    RcWorld *world, RcPlayer *player, const RcPendingInteraction *pending,
    void *ctx) {
    (void)world;
    (void)player;
    (void)pending;
    Phase2HandlerCtx *state = ctx;
    state->calls++;
    return rc_interaction_result_complete();
}

static int spawn_phase2_npc(RcWorld *world, int cache_id, int dx) {
    int def_idx = g_npc_def_count++;
    assert(def_idx < RC_MAX_NPC_DEFS);
    memset(&g_npc_defs[def_idx], 0, sizeof(g_npc_defs[def_idx]));
    g_npc_defs[def_idx].id = cache_id;
    strcpy(g_npc_defs[def_idx].name, "Phase 2 Combat Handoff Guard");
    g_npc_defs[def_idx].size = 1;
    g_npc_defs[def_idx].combat_level = 2;
    g_npc_defs[def_idx].hitpoints = 20;
    g_npc_defs[def_idx].stats[0] = 1;
    g_npc_defs[def_idx].stats[1] = 1;
    g_npc_defs[def_idx].stats[2] = 1;
    g_npc_defs[def_idx].stats[3] = 20;
    g_npc_defs[def_idx].max_hit = 1;
    g_npc_defs[def_idx].attack_speed = 4;
    g_npc_defs[def_idx].attack_types = 0x04;
    strcpy(g_npc_defs[def_idx].options[0], "Talk-to");
    strcpy(g_npc_defs[def_idx].options[1], "Attack");
    int idx = rc_npc_spawn(world, def_idx, world->player.x + dx,
                           world->player.y, world->player.plane);
    assert(idx >= 0);
    world->npcs[idx].wander_timer = 999999;
    return idx;
}

static RcWorld *phase2_world(void) {
    RcWorldConfig cfg = rc_preset_base_only();
    cfg.subsystems = RC_SUB_COMBAT;
    cfg.seed = 12345;
    RcWorld *world = rc_world_create_config(&cfg);
    assert(world);
    return world;
}

static void tick_until_inactive(RcWorld *world, int max_ticks) {
    for (int i = 0; i < max_ticks && rc_interaction_is_active(&world->player);
            i++) {
        rc_world_tick(world);
    }
}

static void test_default_attack_handler_enters_new_combat_state(void) {
    rc_interaction_clear_handlers();
    RcWorld *world = phase2_world();
    int npc_idx = spawn_phase2_npc(world, 920200, 1);
    RcNpc *npc = &world->npcs[npc_idx];

    rc_player_interact_npc(world, npc->uid, 1);
    tick_until_inactive(world, 8);

    assert(!rc_interaction_is_active(&world->player));
    assert(world->player.interact_type == RC_INTERACT_NPC_ATTACK);
    assert(world->player.interact_target == npc->uid);
    assert(world->player.interact_option == 1);
    assert(world->player.attack_target == npc->uid);
    assert(world->player.attack_target_def_id == 920200);
    assert(npc->target_uid == 0);
    assert(rc_combat_actor_has_target(&world->player.combat));
    assert(world->player.combat.target.kind == RC_COMBAT_ACTOR_NPC);
    assert(world->player.combat.target.uid == npc->uid);
    assert(world->player.combat.target.definition_id == 920200);
    assert(rc_combat_actor_has_target(&npc->combat));
    assert(npc->combat.target.kind == RC_COMBAT_ACTOR_PLAYER);
    assert(npc->combat.target.uid == 0);

    rc_world_destroy(world);
}

static void test_custom_attack_handler_preserves_content_priority(void) {
    rc_interaction_clear_handlers();
    RcWorld *world = phase2_world();
    int npc_idx = spawn_phase2_npc(world, 920201, 1);
    RcNpc *npc = &world->npcs[npc_idx];
    Phase2HandlerCtx state = {0};

    RcInteractionDispatchKey key = rc_interaction_dispatch_key_any();
    key.kind = RC_INTERACTION_NPC;
    key.op = RC_INTERACTION_OP2;
    key.content_group = RC_INTERACTION_CONTENT_GROUP_NPC_ATTACK;
    assert(rc_interaction_register_handler(&key, phase2_complete_handler,
                                           &state));

    rc_player_interact_npc(world, npc->uid, 1);
    tick_until_inactive(world, 8);

    assert(state.calls == 1);
    assert(world->player.attack_target == -1);
    assert(!rc_combat_actor_has_target(&world->player.combat));
    assert(npc->target_uid == -1);
    assert(!rc_combat_actor_has_target(&npc->combat));

    rc_world_destroy(world);
}

static void test_noncombat_actions_cancel_combat_through_new_state(void) {
    rc_interaction_clear_handlers();
    RcWorld *world = phase2_world();
    int npc_idx = spawn_phase2_npc(world, 920202, 1);
    RcNpc *npc = &world->npcs[npc_idx];

    rc_player_interact_npc(world, npc->uid, 1);
    tick_until_inactive(world, 8);
    assert(rc_combat_actor_has_target(&world->player.combat));
    assert(rc_combat_actor_has_target(&npc->combat));

    rc_player_interact_npc(world, npc->uid, 0);
    assert(world->player.attack_target == -1);
    assert(world->player.attack_target_def_id == -1);
    assert(!rc_combat_actor_has_target(&world->player.combat));
    assert(npc->target_uid == -1);
    assert(!rc_combat_actor_has_target(&npc->combat));
    tick_until_inactive(world, 8);
    assert(world->player.interact_type == RC_INTERACT_NPC);
    assert(world->player.interact_target == npc->uid);
    assert(world->player.interact_option == 0);

    rc_player_interact_npc(world, npc->uid, 1);
    tick_until_inactive(world, 8);
    assert(rc_combat_actor_has_target(&world->player.combat));
    rc_player_walk_to(world, world->player.x + 1, world->player.y);
    assert(world->player.attack_target == -1);
    assert(!rc_combat_actor_has_target(&world->player.combat));
    assert(npc->target_uid == -1);
    assert(!rc_combat_actor_has_target(&npc->combat));

    rc_world_destroy(world);
}

int main(void) {
    test_default_attack_handler_enters_new_combat_state();
    test_custom_attack_handler_preserves_content_priority();
    test_noncombat_actions_cancel_combat_through_new_state();
    rc_interaction_clear_handlers();
    return 0;
}
