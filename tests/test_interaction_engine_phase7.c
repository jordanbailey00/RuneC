#include "../rc-core/api.h"
#include "../rc-core/items.h"
#include "../rc-core/objects.h"

#include <assert.h>
#include <string.h>

#define ITEM_PATH RC_TEST_SOURCE_DIR "/data/defs/items.bin"

typedef struct {
    int calls;
    int seen_def;
    RcInteractionOp seen_op;
} Phase7HandlerCtx;

static RcWorldConfig phase7_skilling_config(void) {
    RcWorldConfig cfg = rc_preset_skilling_only();
    cfg.collision_tiles_path = NULL;
    cfg.area_flags_path = NULL;
    cfg.object_placements_path = NULL;
    return cfg;
}

static RcInteractionHandlerResult phase7_complete_handler(
    RcWorld *world, RcPlayer *player, const RcPendingInteraction *pending,
    void *ctx) {
    (void)world;
    (void)player;
    Phase7HandlerCtx *state = ctx;
    state->calls++;
    state->seen_def = pending->target.definition_id;
    state->seen_op = pending->op;
    return rc_interaction_result_complete();
}

static void install_object(int obj_id, const char *action, int width,
                           int length, uint32_t flags) {
    memset(&g_rc_object_defs[obj_id], 0, sizeof(g_rc_object_defs[obj_id]));
    g_rc_object_defs[obj_id].id = obj_id;
    strcpy(g_rc_object_defs[obj_id].name, "Phase 7 Object");
    strcpy(g_rc_object_defs[obj_id].actions[0], action);
    g_rc_object_defs[obj_id].width = width;
    g_rc_object_defs[obj_id].length = length;
    g_rc_object_defs[obj_id].loaded = 1;

    memset(&g_rc_object_behaviors[obj_id], 0,
           sizeof(g_rc_object_behaviors[obj_id]));
    g_rc_object_behaviors[obj_id].action_mask = 1u << 0;
    g_rc_object_behaviors[obj_id].flags = flags;
    g_rc_object_behaviors[obj_id].loaded = 1;
}

static void tick_until_inactive(RcWorld *world, int max_ticks) {
    for (int i = 0; i < max_ticks && rc_interaction_is_active(&world->player);
            i++) {
        rc_world_tick(world);
    }
}

static void test_object_routes_faces_and_dispatches_custom_handler(void) {
    rc_interaction_clear_handlers();
    RcWorldConfig cfg = phase7_skilling_config();
    RcWorld *world = rc_world_create_config(&cfg);
    assert(world);

    int obj_id = 41000;
    int obj_x = world->player.x + 4;
    int obj_y = world->player.y;
    install_object(obj_id, "Open", 1, 1, 0);

    Phase7HandlerCtx state = {0};
    RcInteractionDispatchKey key = rc_interaction_dispatch_key_any();
    key.kind = RC_INTERACTION_OBJECT;
    key.op = RC_INTERACTION_OP1;
    key.definition_id = obj_id;
    assert(rc_interaction_register_handler(&key, phase7_complete_handler,
                                           &state));

    assert(rc_player_interact_object_at(world, obj_id, obj_x, obj_y,
                                        world->player.plane, 0));
    assert(rc_interaction_is_active(&world->player));
    assert(world->player.interact_type == RC_INTERACT_NONE);

    rc_world_tick(world);
    assert(world->player.x > obj_x - 4);
    tick_until_inactive(world, 16);
    assert(!rc_interaction_is_active(&world->player));
    assert(state.calls == 1);
    assert(state.seen_def == obj_id);
    assert(state.seen_op == RC_INTERACTION_OP1);
    assert(world->player.facing_entity == -1);
    assert(world->player.facing_x == obj_x);
    assert(world->player.facing_y == obj_y);
    assert(world->player.interaction.flags & RC_INTERACTION_COMPLETED);

    rc_world_destroy(world);
}

static void test_default_object_handler_runs_after_arrival(void) {
    rc_interaction_clear_handlers();
    RcWorldConfig cfg = phase7_skilling_config();
    RcWorld *world = rc_world_create_config(&cfg);
    assert(world);

    int obj_id = 41001;
    int obj_x = world->player.x + 1;
    int obj_y = world->player.y;
    install_object(obj_id, "Open", 1, 1, 0);

    assert(rc_player_interact_object_at(world, obj_id, obj_x, obj_y,
                                        world->player.plane, 0));
    assert(world->player.interact_type == RC_INTERACT_NONE);
    rc_world_tick(world);
    assert(!rc_interaction_is_active(&world->player));
    assert(world->player.interact_type == RC_INTERACT_OBJECT);
    assert(world->player.interact_target == obj_id);
    assert(world->player.interact_option == 0);

    rc_world_destroy(world);
}

static void test_ground_item_routes_faces_and_takes(void) {
    rc_interaction_clear_handlers();
    RcWorldConfig cfg = rc_preset_base_only();
    cfg.subsystems = RC_SUB_INVENTORY | RC_SUB_LOOT;
    cfg.items_path = ITEM_PATH;
    RcWorld *world = rc_world_create_config(&cfg);
    assert(world);

    int item_x = world->player.x + 3;
    int item_y = world->player.y;
    world->ground_item_count = 1;
    world->ground_items[0] = (RcGroundItem){
        .item_id = 995,
        .quantity = 100,
        .x = item_x,
        .y = item_y,
        .plane = world->player.plane,
        .despawn_timer = 300,
        .active = true,
    };

    rc_player_pickup_item(world, 0);
    assert(rc_interaction_is_active(&world->player));
    assert(rc_inv_find(world->player.inventory, 995) < 0);
    tick_until_inactive(world, 16);
    assert(!world->ground_items[0].active);
    assert(rc_inv_find(world->player.inventory, 995) >= 0);
    assert(world->player.facing_entity == -1);
    assert(world->player.facing_x == item_x);
    assert(world->player.facing_y == item_y);

    rc_world_destroy(world);
}

static void test_stale_ground_item_cancels_cleanly(void) {
    rc_interaction_clear_handlers();
    RcWorldConfig cfg = rc_preset_base_only();
    cfg.subsystems = RC_SUB_INVENTORY | RC_SUB_LOOT;
    cfg.items_path = ITEM_PATH;
    RcWorld *world = rc_world_create_config(&cfg);
    assert(world);

    world->ground_item_count = 1;
    world->ground_items[0] = (RcGroundItem){
        .item_id = 995,
        .quantity = 100,
        .x = world->player.x + 2,
        .y = world->player.y,
        .plane = world->player.plane,
        .despawn_timer = 300,
        .active = true,
    };
    rc_player_pickup_item(world, 0);
    assert(rc_interaction_is_active(&world->player));
    world->ground_items[0].active = false;
    rc_world_tick(world);
    assert(!rc_interaction_is_active(&world->player));
    assert(world->player.interaction.last_failure
           == RC_INTERACTION_FAIL_TARGET_MISSING);

    rc_world_destroy(world);
}

static void test_object_no_handler_fallback_still_exists(void) {
    rc_interaction_clear_handlers();
    RcWorldConfig cfg = rc_preset_base_only();
    RcWorld *world = rc_world_create_config(&cfg);
    assert(world);

    RcInteractionTarget target = {0};
    target.kind = RC_INTERACTION_OBJECT;
    target.entity_uid = -1;
    target.definition_id = 41002;
    target.content_group = -1;
    target.tile_x = world->player.x + 1;
    target.tile_y = world->player.y;
    target.plane = world->player.plane;
    target.footprint_width = 1;
    target.footprint_height = 1;
    target.inventory_slot = -1;
    target.equipment_slot = -1;
    target.widget_id = -1;
    target.component_id = -1;
    target.ground_item_instance = -1;
    assert(rc_interaction_begin(&world->player, 0, RC_INTERACTION_OP1,
                                "Open", &target, 1));
    RcInteractionHandlerResult result =
        rc_interaction_dispatch(world, &world->player);
    assert(result.code == RC_INTERACTION_HANDLER_FAILURE);
    assert(result.failure == RC_INTERACTION_FAIL_NO_HANDLER);

    rc_world_destroy(world);
}

int main(void) {
    test_object_routes_faces_and_dispatches_custom_handler();
    test_default_object_handler_runs_after_arrival();
    test_ground_item_routes_faces_and_takes();
    test_stale_ground_item_cancels_cleanly();
    test_object_no_handler_fallback_still_exists();
    rc_interaction_clear_handlers();
    return 0;
}
