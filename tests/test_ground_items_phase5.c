#include <assert.h>
#include <stdio.h>

#include "api.h"
#include "config.h"
#include "interaction.h"
#include "items.h"

#define ITEM_PATH RC_TEST_SOURCE_DIR "/data/defs/items.bin"

typedef struct {
    int calls;
    RcInteractionOp seen_op;
    int seen_target_item;
    int seen_source_item;
    int seen_source_spell;
    int seen_ground_idx;
} GroundHookCtx;

static RcInteractionHandlerResult ground_hook_handler(
    RcWorld *world, RcPlayer *player, const RcPendingInteraction *pending,
    void *ctx) {
    (void)world;
    (void)player;
    GroundHookCtx *state = ctx;
    state->calls++;
    state->seen_op = pending->op;
    state->seen_target_item = pending->target.definition_id;
    state->seen_source_item = pending->source_item_id;
    state->seen_source_spell = pending->source_spell_id;
    state->seen_ground_idx = pending->target.ground_item_instance;
    return rc_interaction_result_complete();
}

static RcWorld *phase5_world(void) {
    RcWorldConfig cfg = rc_preset_base_only();
    cfg.subsystems = RC_SUB_INVENTORY | RC_SUB_LOOT;
    cfg.items_path = ITEM_PATH;
    RcWorld *world = rc_world_create_config(&cfg);
    assert(world != NULL);
    return world;
}

static int spawn_public_ground_item(RcWorld *world, int item_id, int quantity,
                                    int dx) {
    assert(rc_ground_item_spawn(world, item_id, quantity,
                                world->player.x + dx, world->player.y,
                                world->player.plane,
                                RC_GROUND_OWNER_NONE));
    return world->ground_item_count - 1;
}

static RcInteractionDispatchKey ground_key(RcInteractionOp op,
                                           int target_item) {
    RcInteractionDispatchKey key = rc_interaction_dispatch_key_any();
    key.kind = RC_INTERACTION_GROUND_ITEM;
    key.op = op;
    key.definition_id = target_item;
    return key;
}

static RcInteractionTarget target_for_ground_item(const RcWorld *world,
                                                  int ground_idx) {
    const RcGroundItem *g = &world->ground_items[ground_idx];
    RcInteractionTarget target = {0};
    target.kind = RC_INTERACTION_GROUND_ITEM;
    target.entity_uid = g->uid;
    target.entity_generation = g->version;
    target.definition_id = g->item_id;
    target.content_group = -1;
    target.tile_x = g->x;
    target.tile_y = g->y;
    target.plane = g->plane;
    target.footprint_width = 1;
    target.footprint_height = 1;
    target.inventory_slot = -1;
    target.equipment_slot = -1;
    target.widget_id = -1;
    target.component_id = -1;
    target.ground_item_instance = ground_idx;
    return target;
}

static void test_item_on_ground_item_dispatches_source_item(void) {
    RcWorld *world = phase5_world();
    GroundHookCtx ctx = {0};
    int src_slot = rc_inv_add(world->player.inventory, 4151, 1);
    assert(src_slot >= 0);
    int ground_idx = spawn_public_ground_item(world, 995, 100, 0);

    RcInteractionDispatchKey key =
        ground_key(RC_INTERACTION_USE_ON, 995);
    key.source_item_id = 4151;
    assert(rc_interaction_register_world_handler(world, &key,
                                                 ground_hook_handler, &ctx));

    assert(rc_player_use_inventory_item_on_ground_item(world, src_slot,
                                                       ground_idx));
    rc_world_tick(world);

    assert(ctx.calls == 1);
    assert(ctx.seen_op == RC_INTERACTION_USE_ON);
    assert(ctx.seen_target_item == 995);
    assert(ctx.seen_source_item == 4151);
    assert(ctx.seen_ground_idx == ground_idx);
    assert(world->ground_items[ground_idx].active);
    assert(world->ground_items[ground_idx].quantity == 100);
    assert(world->player.inventory[src_slot].item_id == 4151);

    rc_world_destroy(world);
}

static void test_spell_on_ground_item_dispatches_source_spell(void) {
    RcWorld *world = phase5_world();
    GroundHookCtx ctx = {0};
    int ground_idx = spawn_public_ground_item(world, 995, 100, 2);

    RcInteractionDispatchKey key =
        ground_key(RC_INTERACTION_SPELL_ON, 995);
    key.source_spell_id = 900;
    assert(rc_interaction_register_world_handler(world, &key,
                                                 ground_hook_handler, &ctx));

    assert(rc_player_cast_spell_on_ground_item(world, 900, ground_idx));
    rc_world_tick(world);

    assert(ctx.calls == 1);
    assert(ctx.seen_op == RC_INTERACTION_SPELL_ON);
    assert(ctx.seen_target_item == 995);
    assert(ctx.seen_source_spell == 900);
    assert(ctx.seen_ground_idx == ground_idx);
    assert(world->ground_items[ground_idx].active);
    assert(world->ground_items[ground_idx].quantity == 100);

    rc_world_destroy(world);
}

static void test_unsupported_item_on_ground_item_fails_without_mutation(void) {
    RcWorld *world = phase5_world();
    int src_slot = rc_inv_add(world->player.inventory, 4151, 1);
    assert(src_slot >= 0);
    int ground_idx = spawn_public_ground_item(world, 995, 100, 0);
    int uid = world->ground_items[ground_idx].uid;
    int version = world->ground_items[ground_idx].version;

    assert(rc_player_use_inventory_item_on_ground_item(world, src_slot,
                                                       ground_idx));
    rc_world_tick(world);

    assert(!rc_interaction_is_active(&world->player));
    assert(world->player.interaction.last_failure ==
           RC_INTERACTION_FAIL_NO_HANDLER);
    assert(world->ground_items[ground_idx].active);
    assert(world->ground_items[ground_idx].uid == uid);
    assert(world->ground_items[ground_idx].version == version);
    assert(world->ground_items[ground_idx].item_id == 995);
    assert(world->ground_items[ground_idx].quantity == 100);
    assert(world->player.inventory[src_slot].item_id == 4151);
    assert(world->player.inventory[src_slot].quantity == 1);

    rc_world_destroy(world);
}

static void test_unsupported_spell_on_ground_item_fails_without_mutation(void) {
    RcWorld *world = phase5_world();
    int ground_idx = spawn_public_ground_item(world, 995, 100, 2);
    int uid = world->ground_items[ground_idx].uid;
    int version = world->ground_items[ground_idx].version;

    assert(rc_player_cast_spell_on_ground_item(world, 901, ground_idx));
    rc_world_tick(world);

    assert(!rc_interaction_is_active(&world->player));
    assert(world->player.interaction.last_failure ==
           RC_INTERACTION_FAIL_NO_HANDLER);
    assert(world->ground_items[ground_idx].active);
    assert(world->ground_items[ground_idx].uid == uid);
    assert(world->ground_items[ground_idx].version == version);
    assert(world->ground_items[ground_idx].item_id == 995);
    assert(world->ground_items[ground_idx].quantity == 100);

    rc_world_destroy(world);
}

static void test_default_ground_item_fallback_invalid_sources(void) {
    RcWorld *world = phase5_world();
    int ground_idx = spawn_public_ground_item(world, 995, 100, 0);
    RcInteractionTarget target = target_for_ground_item(world, ground_idx);

    assert(rc_interaction_begin_with_source(
        &world->player, 0, RC_INTERACTION_USE_ON, "Use", &target, 0,
        RC_INTERACTION_KEY_ANY, RC_INTERACTION_KEY_ANY,
        RC_INTERACTION_KEY_ANY, RC_INTERACTION_KEY_ANY));
    rc_world_tick(world);
    assert(!rc_interaction_is_active(&world->player));
    assert(world->player.interaction.last_failure ==
           RC_INTERACTION_FAIL_INVALID_SOURCE);
    assert(world->ground_items[ground_idx].active);

    assert(rc_interaction_begin_with_source(
        &world->player, 0, RC_INTERACTION_SPELL_ON, "Cast", &target, 0,
        RC_INTERACTION_KEY_ANY, RC_INTERACTION_KEY_ANY,
        RC_INTERACTION_KEY_ANY, RC_INTERACTION_KEY_ANY));
    rc_world_tick(world);
    assert(!rc_interaction_is_active(&world->player));
    assert(world->player.interaction.last_failure ==
           RC_INTERACTION_FAIL_INVALID_SOURCE);
    assert(world->ground_items[ground_idx].active);

    rc_world_destroy(world);
}

int main(void) {
    test_item_on_ground_item_dispatches_source_item();
    test_spell_on_ground_item_dispatches_source_spell();
    test_unsupported_item_on_ground_item_fails_without_mutation();
    test_unsupported_spell_on_ground_item_fails_without_mutation();
    test_default_ground_item_fallback_invalid_sources();
    printf("test_ground_items_phase5: item/spell ground hooks covered.\n");
    return 0;
}
