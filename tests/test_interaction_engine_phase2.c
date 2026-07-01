#include "../rc-core/api.h"

#include <assert.h>
#include <string.h>

typedef struct {
    int calls;
    int marker;
    RcInteractionHandlerResult result;
} HandlerCtx;

static RcInteractionTarget target_for(int def_id, int group) {
    RcInteractionTarget target = {0};
    target.kind = RC_INTERACTION_NPC;
    target.entity_uid = 77;
    target.definition_id = def_id;
    target.content_group = group;
    target.tile_x = 3210;
    target.tile_y = 3211;
    target.plane = 0;
    target.footprint_width = 1;
    target.footprint_height = 1;
    target.inventory_slot = -1;
    target.equipment_slot = -1;
    target.widget_id = -1;
    target.component_id = -1;
    target.ground_item_instance = -1;
    return target;
}

static RcInteractionHandlerResult handler(RcWorld *world, RcPlayer *player,
                                          const RcPendingInteraction *pending,
                                          void *ctx) {
    (void)world;
    (void)player;
    HandlerCtx *state = ctx;
    assert(pending);
    state->calls++;
    return state->result;
}

static RcInteractionDispatchKey npc_key(RcInteractionOp op) {
    RcInteractionDispatchKey key = rc_interaction_dispatch_key_any();
    key.kind = RC_INTERACTION_NPC;
    key.op = op;
    return key;
}

static void test_exact_group_and_fallback_priority(void) {
    RcWorldConfig cfg = rc_preset_base_only();
    RcWorld *world = rc_world_create_config(&cfg);
    assert(world);

    HandlerCtx fallback = {
        .marker = 1,
        .result = rc_interaction_result_message("fallback"),
    };
    HandlerCtx group = {
        .marker = 2,
        .result = rc_interaction_result_continue_approach(3),
    };
    HandlerCtx exact = {
        .marker = 3,
        .result = rc_interaction_result_complete(),
    };

    RcInteractionDispatchKey key = npc_key(RC_INTERACTION_OP1);
    assert(rc_interaction_register_world_handler(world, &key, handler,
                                                 &fallback));
    key.content_group = 44;
    assert(rc_interaction_register_world_handler(world, &key, handler,
                                                 &group));
    key.content_group = RC_INTERACTION_KEY_ANY;
    key.definition_id = 900200;
    assert(rc_interaction_register_world_handler(world, &key, handler,
                                                 &exact));
    assert(rc_interaction_world_handler_count(world) == 3);

    RcInteractionTarget target = target_for(900200, 44);
    assert(rc_interaction_begin(&world->player, 0, RC_INTERACTION_OP1,
                                "Talk-to", &target, 1));
    RcInteractionHandlerResult result =
        rc_interaction_dispatch(world, &world->player);
    assert(result.code == RC_INTERACTION_HANDLER_COMPLETE);
    assert(exact.calls == 1);
    assert(group.calls == 0);
    assert(fallback.calls == 0);

    target = target_for(900201, 44);
    assert(rc_interaction_begin(&world->player, 0, RC_INTERACTION_OP1,
                                "Talk-to", &target, 1));
    result = rc_interaction_dispatch(world, &world->player);
    assert(result.code == RC_INTERACTION_HANDLER_CONTINUE_APPROACH);
    assert(result.approach_range == 3);
    assert(group.calls == 1);
    assert(fallback.calls == 0);

    target = target_for(900202, 45);
    assert(rc_interaction_begin(&world->player, 0, RC_INTERACTION_OP1,
                                "Talk-to", &target, 1));
    result = rc_interaction_dispatch(world, &world->player);
    assert(result.code == RC_INTERACTION_HANDLER_MESSAGE);
    assert(strcmp(result.message, "fallback") == 0);
    assert(fallback.calls == 1);

    rc_world_destroy(world);
}

static void test_replacement_missing_and_invalid(void) {
    RcWorldConfig cfg = rc_preset_base_only();
    RcWorld *world = rc_world_create_config(&cfg);
    assert(world);

    HandlerCtx first = {
        .marker = 1,
        .result = rc_interaction_result_message("old"),
    };
    HandlerCtx replacement = {
        .marker = 2,
        .result = rc_interaction_result_combat_handoff(77),
    };
    RcInteractionDispatchKey key = npc_key(RC_INTERACTION_OP2);
    key.definition_id = 900300;
    assert(rc_interaction_register_world_handler(world, &key, handler,
                                                 &first));
    assert(rc_interaction_register_world_handler(world, &key, handler,
                                                 &replacement));
    assert(rc_interaction_world_handler_count(world) == 1);

    RcInteractionTarget target = target_for(900300, -1);
    assert(rc_interaction_begin(&world->player, 0, RC_INTERACTION_OP2,
                                "Attack", &target, 1));
    RcInteractionHandlerResult result =
        rc_interaction_dispatch(world, &world->player);
    assert(result.code == RC_INTERACTION_HANDLER_COMBAT_HANDOFF);
    assert(result.combat_target_uid == 77);
    assert(first.calls == 0);
    assert(replacement.calls == 1);

    target = target_for(900301, -1);
    assert(rc_interaction_begin(&world->player, 0, RC_INTERACTION_OP2,
                                "Attack", &target, 1));
    result = rc_interaction_dispatch(world, &world->player);
    assert(result.code == RC_INTERACTION_HANDLER_FAILURE);
    assert(result.failure == RC_INTERACTION_FAIL_NO_HANDLER);
    assert(world->player.interaction.last_failure
           == RC_INTERACTION_FAIL_NO_HANDLER);

    RcInteractionDispatchKey bad = rc_interaction_dispatch_key_any();
    assert(!rc_interaction_register_world_handler(world, &bad, handler,
                                                  &first));

    rc_interaction_clear(&world->player);
    result = rc_interaction_dispatch(world, &world->player);
    assert(result.code == RC_INTERACTION_HANDLER_FAILURE);
    assert(result.failure == RC_INTERACTION_FAIL_INVALID_SOURCE);

    rc_world_destroy(world);
}

static void test_world_handler_isolation(void) {
    RcWorldConfig cfg = rc_preset_base_only();
    RcWorld *world_a = rc_world_create_config(&cfg);
    RcWorld *world_b = rc_world_create_config(&cfg);
    assert(world_a && world_b);

    HandlerCtx a = {
        .marker = 1,
        .result = rc_interaction_result_message("world-a"),
    };
    HandlerCtx b = {
        .marker = 2,
        .result = rc_interaction_result_message("world-b"),
    };

    RcInteractionDispatchKey key = npc_key(RC_INTERACTION_OP3);
    key.definition_id = 900400;
    assert(rc_interaction_register_world_handler(world_a, &key, handler, &a));
    assert(rc_interaction_register_world_handler(world_b, &key, handler, &b));
    assert(rc_interaction_world_handler_count(world_a) == 1);
    assert(rc_interaction_world_handler_count(world_b) == 1);

    RcInteractionTarget target = target_for(900400, -1);
    assert(rc_interaction_begin(&world_a->player, 0, RC_INTERACTION_OP3,
                                "Use", &target, 1));
    RcInteractionHandlerResult result =
        rc_interaction_dispatch(world_a, &world_a->player);
    assert(result.code == RC_INTERACTION_HANDLER_MESSAGE);
    assert(strcmp(result.message, "world-a") == 0);

    assert(rc_interaction_begin(&world_b->player, 0, RC_INTERACTION_OP3,
                                "Use", &target, 1));
    result = rc_interaction_dispatch(world_b, &world_b->player);
    assert(result.code == RC_INTERACTION_HANDLER_MESSAGE);
    assert(strcmp(result.message, "world-b") == 0);

    assert(a.calls == 1);
    assert(b.calls == 1);

    rc_interaction_clear_world_handlers(world_b);
    assert(rc_interaction_begin(&world_b->player, 0, RC_INTERACTION_OP3,
                                "Use", &target, 1));
    result = rc_interaction_dispatch(world_b, &world_b->player);
    assert(result.code == RC_INTERACTION_HANDLER_FAILURE);
    assert(result.failure == RC_INTERACTION_FAIL_NO_HANDLER);

    rc_world_destroy(world_a);
    rc_world_destroy(world_b);
}

int main(void) {
    test_exact_group_and_fallback_priority();
    test_replacement_missing_and_invalid();
    test_world_handler_isolation();
    return 0;
}
