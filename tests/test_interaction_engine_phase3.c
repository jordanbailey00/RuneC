#include "../rc-core/api.h"
#include "../rc-core/npc.h"

#include <assert.h>
#include <string.h>

typedef struct {
    int calls;
    int seen_uid;
    RcInteractionOp seen_op;
} HandlerCtx;

static RcInteractionHandlerResult exact_talk_handler(
    RcWorld *world, RcPlayer *player, const RcPendingInteraction *pending,
    void *ctx) {
    (void)world;
    (void)player;
    HandlerCtx *state = ctx;
    state->calls++;
    state->seen_uid = pending->target.entity_uid;
    state->seen_op = pending->op;
    return rc_interaction_result_complete();
}

static int spawn_option_npc(RcWorld *world, int cache_id) {
    int def_idx = g_npc_def_count++;
    assert(def_idx < RC_MAX_NPC_DEFS);
    memset(&g_npc_defs[def_idx], 0, sizeof(g_npc_defs[def_idx]));
    g_npc_defs[def_idx].id = cache_id;
    strcpy(g_npc_defs[def_idx].name, "Phase 3 Dummy");
    g_npc_defs[def_idx].size = 1;
    g_npc_defs[def_idx].combat_level = 2;
    g_npc_defs[def_idx].hitpoints = 10;
    g_npc_defs[def_idx].wander_range = 0;
    strcpy(g_npc_defs[def_idx].options[0], "Talk-to");
    strcpy(g_npc_defs[def_idx].options[1], "Attack");
    strcpy(g_npc_defs[def_idx].options[2], "Trade");

    int npc_idx = rc_npc_spawn(world, def_idx, world->player.x + 1,
                               world->player.y, world->player.plane);
    assert(npc_idx >= 0);
    return npc_idx;
}

static void tick_until_inactive(RcWorld *world, int max_ticks) {
    for (int i = 0; i < max_ticks && rc_interaction_is_active(&world->player);
            i++) {
        rc_world_tick(world);
    }
}

static void test_exact_handler_overrides_default_npc_option(void) {
    rc_interaction_clear_handlers();
    RcWorldConfig cfg = rc_preset_base_only();
    cfg.subsystems = RC_SUB_COMBAT;
    RcWorld *world = rc_world_create_config(&cfg);
    assert(world);

    int npc_idx = spawn_option_npc(world, 901300);
    int uid = world->npcs[npc_idx].uid;
    HandlerCtx state = {0};

    RcInteractionDispatchKey key = rc_interaction_dispatch_key_any();
    key.kind = RC_INTERACTION_NPC;
    key.op = RC_INTERACTION_OP1;
    key.definition_id = 901300;
    assert(rc_interaction_register_handler(&key, exact_talk_handler, &state));

    rc_player_interact_npc(world, uid, 0);
    tick_until_inactive(world, 16);
    assert(state.calls == 1);
    assert(state.seen_uid == uid);
    assert(state.seen_op == RC_INTERACTION_OP1);
    assert(!rc_interaction_is_active(&world->player));
    assert(world->player.interaction.target.entity_uid == uid);
    assert(strcmp(world->player.interaction.option_text, "Talk-to") == 0);
    assert(world->player.attack_target == -1);

    rc_world_destroy(world);
}

static void test_default_attack_and_noncombat_handlers_preserve_behavior(void) {
    rc_interaction_clear_handlers();
    RcWorldConfig cfg = rc_preset_base_only();
    cfg.subsystems = RC_SUB_COMBAT;
    RcWorld *world = rc_world_create_config(&cfg);
    assert(world);

    int npc_idx = spawn_option_npc(world, 901301);
    int uid = world->npcs[npc_idx].uid;

    rc_player_interact_npc(world, uid, 2);
    tick_until_inactive(world, 16);
    assert(!rc_interaction_is_active(&world->player));
    assert(world->player.interaction.op == RC_INTERACTION_OP3);
    assert(strcmp(world->player.interaction.option_text, "Trade") == 0);
    assert(world->player.interact_type == RC_INTERACT_NPC);
    assert(world->player.interact_target == uid);
    assert(world->player.interact_option == 2);
    assert(world->player.attack_target == -1);

    rc_player_interact_npc(world, uid, 1);
    tick_until_inactive(world, 16);
    assert(!rc_interaction_is_active(&world->player));
    assert(world->player.interaction.op == RC_INTERACTION_OP2);
    assert(strcmp(world->player.interaction.option_text, "Attack") == 0);
    assert(world->player.interact_type == RC_INTERACT_NPC_ATTACK);
    assert(world->player.interact_target == uid);
    assert(world->player.interact_option == 1);
    assert(world->player.attack_target == uid);
    assert(world->npcs[npc_idx].target_uid == 0);

    world->player.attack_target = -1;
    world->player.interact_type = RC_INTERACT_NONE;
    world->player.interact_target = -1;
    world->player.interact_option = -1;
    rc_interaction_clear(&world->player);

    rc_player_attack_npc(world, uid);
    tick_until_inactive(world, 16);
    assert(!rc_interaction_is_active(&world->player));
    assert(world->player.interaction.op == RC_INTERACTION_OP2);
    assert(strcmp(world->player.interaction.option_text, "Attack") == 0);
    assert(world->player.interact_type == RC_INTERACT_NPC_ATTACK);
    assert(world->player.attack_target == uid);

    rc_world_destroy(world);
}

static void test_missing_option_and_no_handler_are_deterministic(void) {
    rc_interaction_clear_handlers();
    RcWorldConfig cfg = rc_preset_base_only();
    cfg.subsystems = RC_SUB_COMBAT;
    RcWorld *world = rc_world_create_config(&cfg);
    assert(world);

    int npc_idx = spawn_option_npc(world, 901302);
    int uid = world->npcs[npc_idx].uid;

    rc_player_interact_npc(world, uid, 4);
    assert(!rc_interaction_is_active(&world->player));
    assert(world->player.attack_target == -1);

    RcInteractionTarget target = {0};
    target.kind = RC_INTERACTION_NPC;
    target.entity_uid = uid;
    target.definition_id = 901302;
    target.tile_x = world->npcs[npc_idx].x;
    target.tile_y = world->npcs[npc_idx].y;
    target.plane = world->npcs[npc_idx].plane;
    target.footprint_width = 1;
    target.footprint_height = 1;
    target.inventory_slot = -1;
    target.equipment_slot = -1;
    target.widget_id = -1;
    target.component_id = -1;
    target.ground_item_instance = -1;
    assert(rc_interaction_begin(&world->player, 0, RC_INTERACTION_OP1,
                                "Talk-to", &target, 1));
    RcInteractionHandlerResult result =
        rc_interaction_dispatch(world, &world->player);
    assert(result.code == RC_INTERACTION_HANDLER_FAILURE);
    assert(result.failure == RC_INTERACTION_FAIL_NO_HANDLER);
    assert(world->player.interaction.last_failure
           == RC_INTERACTION_FAIL_NO_HANDLER);

    rc_world_destroy(world);
}

int main(void) {
    test_exact_handler_overrides_default_npc_option();
    test_default_attack_and_noncombat_handlers_preserve_behavior();
    test_missing_option_and_no_handler_are_deterministic();
    rc_interaction_clear_handlers();
    return 0;
}
