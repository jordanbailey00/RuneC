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
    rc_interaction_clear_handlers();
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
    assert(rc_interaction_register_handler(&key, handler, &fallback));
    key.content_group = 44;
    assert(rc_interaction_register_handler(&key, handler, &group));
    key.content_group = RC_INTERACTION_KEY_ANY;
    key.definition_id = 900200;
    assert(rc_interaction_register_handler(&key, handler, &exact));
    assert(rc_interaction_handler_count() == 3);

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
    rc_interaction_clear_handlers();
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
    assert(rc_interaction_register_handler(&key, handler, &first));
    assert(rc_interaction_register_handler(&key, handler, &replacement));
    assert(rc_interaction_handler_count() == 1);

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
    assert(!rc_interaction_register_handler(&bad, handler, &first));

    rc_interaction_clear(&world->player);
    result = rc_interaction_dispatch(world, &world->player);
    assert(result.code == RC_INTERACTION_HANDLER_FAILURE);
    assert(result.failure == RC_INTERACTION_FAIL_INVALID_SOURCE);

    rc_world_destroy(world);
}

int main(void) {
    test_exact_group_and_fallback_priority();
    test_replacement_missing_and_invalid();
    rc_interaction_clear_handlers();
    return 0;
}
