#include "../rc-core/api.h"
#include "../rc-core/items.h"
#include "../rc-core/spells.h"
#include "../rc-core/storage.h"

#include <assert.h>
#include <string.h>

enum {
    DEFAULT_HANDLER_COUNT = 20,
    EXAMINE_ITEM_ID = 995,
};

typedef struct {
    int calls;
    int replacement_component;
    RcInteractionHandlerResult result;
} HandlerCtx;

static RcInteractionTarget widget_target(int widget, int component,
                                          int action) {
    RcInteractionTarget target = {0};
    target.kind = RC_INTERACTION_WIDGET;
    target.entity_uid = -1;
    target.entity_generation = -1;
    target.definition_id = action;
    target.content_group = RC_INTERACTION_KEY_ANY;
    target.tile_x = -1;
    target.tile_y = -1;
    target.plane = -1;
    target.inventory_slot = -1;
    target.equipment_slot = -1;
    target.widget_id = widget;
    target.component_id = component;
    target.ground_item_instance = -1;
    return target;
}

static RcInteractionDispatchKey widget_key(int widget, int component,
                                           int action) {
    RcInteractionDispatchKey key = rc_interaction_dispatch_key_any();
    key.kind = RC_INTERACTION_WIDGET;
    key.op = RC_INTERACTION_WIDGET_ACTION;
    key.definition_id = action;
    key.widget_id = widget;
    key.component_id = component;
    return key;
}

static RcInteractionHandlerResult fixed_handler(
    RcWorld *world, RcPlayer *player, const RcPendingInteraction *pending,
    void *ctx) {
    (void)world;
    (void)player;
    (void)pending;
    HandlerCtx *state = ctx;
    state->calls++;
    return state->result;
}

static RcInteractionHandlerResult replacing_handler(
    RcWorld *world, RcPlayer *player, const RcPendingInteraction *pending,
    void *ctx) {
    (void)world;
    HandlerCtx *state = ctx;
    state->calls++;
    RcInteractionTarget replacement = widget_target(
        pending->target.widget_id, state->replacement_component,
        pending->target.definition_id);
    assert(rc_interaction_begin(
        player, pending->source_actor_uid, RC_INTERACTION_WIDGET_ACTION,
        "Replacement", &replacement, 0));
    return rc_interaction_result_complete();
}

static void test_defaults_supported_vocabulary_and_capacity(void) {
    RcWorldConfig cfg = rc_preset_base_only();
    RcWorld *world = rc_world_create_config(&cfg);
    assert(world);
    assert(rc_interaction_world_handler_count(world)
           == DEFAULT_HANDLER_COUNT);

    RcInteractionTarget player_target = {0};
    player_target.kind = RC_INTERACTION_PLAYER;
    player_target.entity_uid = 1;
    player_target.tile_x = world->player.x;
    player_target.tile_y = world->player.y;
    player_target.plane = world->player.plane;
    player_target.footprint_width = 1;
    player_target.footprint_height = 1;
    assert(!rc_interaction_kind_valid(RC_INTERACTION_PLAYER));
    assert(!rc_interaction_target_valid(&player_target));
    assert(!rc_interaction_begin(&world->player, 0, RC_INTERACTION_OP1,
                                 "Follow", &player_target, 1));
    const RcInteractionOutcome *outcome =
        rc_interaction_last_outcome(&world->player);
    assert(outcome->failure == RC_INTERACTION_FAIL_INVALID_TARGET);

    HandlerCtx handler = {
        .result = rc_interaction_result_complete(),
    };
    int remaining = RC_MAX_INTERACTION_HANDLERS - DEFAULT_HANDLER_COUNT;
    for (int i = 0; i < remaining; i++) {
        RcInteractionDispatchKey key = widget_key(700, i, i + 1000);
        assert(rc_interaction_register_world_handler(
            world, &key, fixed_handler, &handler));
    }
    assert(rc_interaction_world_handler_count(world)
           == RC_MAX_INTERACTION_HANDLERS);
    RcInteractionDispatchKey overflow = widget_key(701, 1, 9999);
    assert(!rc_interaction_register_world_handler(
        world, &overflow, fixed_handler, &handler));

    assert(rc_world_reset(world));
    assert(rc_interaction_world_handler_count(world)
           == DEFAULT_HANDLER_COUNT);
    rc_world_destroy(world);
}

static void test_generation_owned_reentrant_replacement(void) {
    RcWorldConfig cfg = rc_preset_base_only();
    RcWorld *world = rc_world_create_config(&cfg);
    assert(world);
    HandlerCtx first = {
        .replacement_component = 2,
        .result = rc_interaction_result_complete(),
    };
    HandlerCtx replacement = {
        .result = rc_interaction_result_complete(),
    };
    RcInteractionDispatchKey key = widget_key(548, 1, 7);
    assert(rc_interaction_register_world_handler(
        world, &key, replacing_handler, &first));
    key = widget_key(548, 2, 7);
    assert(rc_interaction_register_world_handler(
        world, &key, fixed_handler, &replacement));

    RcInteractionTarget target = widget_target(548, 1, 7);
    assert(rc_interaction_begin(&world->player, 0,
                                RC_INTERACTION_WIDGET_ACTION,
                                "First", &target, 0));
    uint64_t first_generation = world->player.interaction.generation;
    rc_world_tick(world);

    assert(first.calls == 1);
    assert(replacement.calls == 0);
    assert(rc_interaction_is_active(&world->player));
    assert(world->player.interaction.generation != first_generation);
    assert(world->player.interaction.target.component_id == 2);

    rc_world_tick(world);
    assert(replacement.calls == 1);
    assert(!rc_interaction_is_active(&world->player));
    rc_world_destroy(world);
}

static void test_deterministic_precedence(void) {
    RcWorldConfig cfg = rc_preset_base_only();
    RcWorld *world = rc_world_create_config(&cfg);
    assert(world);
    HandlerCtx component = {
        .result = rc_interaction_result_message("component"),
    };
    HandlerCtx widget = {
        .result = rc_interaction_result_message("widget"),
    };

    RcInteractionDispatchKey component_key =
        widget_key(RC_INTERACTION_KEY_ANY, 77, 4);
    RcInteractionDispatchKey widget_key_only =
        widget_key(548, RC_INTERACTION_KEY_ANY, 4);
    assert(rc_interaction_register_world_handler(
        world, &component_key, fixed_handler, &component));
    assert(rc_interaction_register_world_handler(
        world, &widget_key_only, fixed_handler, &widget));

    RcInteractionTarget target = widget_target(548, 77, 4);
    assert(rc_interaction_begin(&world->player, 0,
                                RC_INTERACTION_WIDGET_ACTION,
                                "Choose", &target, 0));
    rc_world_tick(world);
    assert(widget.calls == 1);
    assert(component.calls == 0);
    assert(strcmp(world->player.interaction_outcome.message, "widget") == 0);
    rc_world_destroy(world);
}

static void test_terminal_outcomes_and_actor_state(void) {
    RcWorldConfig cfg = rc_preset_base_only();
    RcWorld *world = rc_world_create_config(&cfg);
    assert(world);
    HandlerCtx message = {
        .result = rc_interaction_result_message("Handled."),
    };
    RcInteractionDispatchKey key = widget_key(548, 10, 1);
    assert(rc_interaction_register_world_handler(
        world, &key, fixed_handler, &message));

    RcInteractionTarget target = widget_target(548, 10, 1);
    assert(rc_interaction_begin(&world->player, 0,
                                RC_INTERACTION_WIDGET_ACTION,
                                "Message", &target, 0));
    uint64_t generation = world->player.interaction.generation;
    rc_world_tick(world);
    const RcInteractionOutcome *outcome =
        rc_interaction_last_outcome(&world->player);
    assert(outcome->interaction_generation == generation);
    assert(outcome->code == RC_INTERACTION_HANDLER_MESSAGE);
    assert(strcmp(outcome->message, "Handled.") == 0);

    target = widget_target(548, 11, 1);
    assert(rc_interaction_begin(&world->player, 0,
                                RC_INTERACTION_WIDGET_ACTION,
                                "Missing", &target, 0));
    rc_world_tick(world);
    outcome = rc_interaction_last_outcome(&world->player);
    assert(outcome->failure == RC_INTERACTION_FAIL_NO_HANDLER);
    assert(outcome->message[0] != '\0');

    assert(rc_interaction_begin(&world->player, 0,
                                RC_INTERACTION_WIDGET_ACTION,
                                "Dead", &target, 0));
    world->player.current_hp = 0;
    world->player.is_dead = true;
    rc_world_tick(world);
    outcome = rc_interaction_last_outcome(&world->player);
    assert(outcome->failure == RC_INTERACTION_FAIL_ACTOR_DEAD);

    world->player.current_hp = 10;
    world->player.is_dead = false;
    assert(rc_interaction_begin(&world->player, 0,
                                RC_INTERACTION_WIDGET_ACTION,
                                "Busy", &target, 0));
    world->player_action.active = true;
    world->player_action.owner = RC_ACTION_OWNER_MODAL;
    world->player_action.category = RC_ACTION_CATEGORY_STRONG;
    world->player_action.ready_tick = world->tick + 10;
    rc_world_tick(world);
    outcome = rc_interaction_last_outcome(&world->player);
    assert(outcome->failure == RC_INTERACTION_FAIL_ACTOR_BUSY);

    uint64_t sequence = outcome->sequence;
    rc_interaction_cancel(&world->player, RC_INTERACTION_FAIL_CANCELLED);
    assert(world->player.interaction_outcome.sequence == sequence);
    rc_world_destroy(world);
}

static void test_source_revalidation_and_non_disruptive_examine(void) {
    RcWorldConfig cfg = rc_preset_base_only();
    RcWorld *world = rc_world_create_config(&cfg);
    assert(world);

    static RcSpellDef spells[2];
    memset(spells, 0, sizeof(spells));
    spells[1].loaded = 1;
    spells[1].book = RC_SPELL_BOOK_STANDARD;
    strcpy(spells[1].name, "Test spell");
    rc_spell_use_defs(spells, 2);

    RcInteractionTarget target = widget_target(548, 20, 1);
    assert(rc_interaction_begin_with_source(
        &world->player, 0, RC_INTERACTION_SPELL_ON, "Cast", &target, 0,
        RC_INTERACTION_KEY_ANY, 1, RC_INTERACTION_KEY_ANY,
        RC_INTERACTION_KEY_ANY));
    world->player.current_spellbook = RC_SPELL_BOOK_ANCIENT;
    rc_world_tick(world);
    const RcInteractionOutcome *outcome =
        rc_interaction_last_outcome(&world->player);
    assert(outcome->failure == RC_INTERACTION_FAIL_INVALID_SOURCE);

    world->player.current_spellbook = RC_SPELL_BOOK_STANDARD;
    spells[1].loaded = 1;
    assert(rc_interaction_begin_with_source(
        &world->player, 0, RC_INTERACTION_SPELL_ON, "Cast", &target, 0,
        RC_INTERACTION_KEY_ANY, 1, RC_INTERACTION_KEY_ANY,
        RC_INTERACTION_KEY_ANY));
    spells[1].loaded = 0;
    rc_world_tick(world);
    outcome = rc_interaction_last_outcome(&world->player);
    assert(outcome->failure == RC_INTERACTION_FAIL_INVALID_SOURCE);
    spells[1].loaded = 1;

    static RcItemDef items[EXAMINE_ITEM_ID + 1];
    memset(items, 0, sizeof(items));
    items[EXAMINE_ITEM_ID].id = EXAMINE_ITEM_ID;
    items[EXAMINE_ITEM_ID].loaded = true;
    strcpy(items[EXAMINE_ITEM_ID].name, "Test item");
    strcpy(items[EXAMINE_ITEM_ID].examine, "A useful test item.");
    rc_item_use_defs(items, EXAMINE_ITEM_ID + 1);
    world->player.inventory[0].item_id = EXAMINE_ITEM_ID;
    world->player.inventory[0].quantity = 1;

    HandlerCtx continuing = {
        .result = rc_interaction_result_continue_approach(0),
    };
    RcInteractionDispatchKey key = widget_key(548, 30, 1);
    assert(rc_interaction_register_world_handler(
        world, &key, fixed_handler, &continuing));
    target = widget_target(548, 30, 1);
    assert(rc_interaction_begin(&world->player, 0,
                                RC_INTERACTION_WIDGET_ACTION,
                                "Continue", &target, 0));
    uint64_t generation = world->player.interaction.generation;
    assert(rc_player_examine_inventory_item(world, 0));
    rc_world_tick(world);

    assert(continuing.calls == 1);
    assert(rc_interaction_is_active(&world->player));
    assert(world->player.interaction.generation == generation);
    outcome = rc_interaction_last_outcome(&world->player);
    assert(outcome->code == RC_INTERACTION_HANDLER_MESSAGE);
    assert(strcmp(outcome->message, "A useful test item.") == 0);

    rc_item_reset_defs_if_active(items);
    rc_spell_reset_defs_if_active(spells);
    rc_world_destroy(world);
}

static void test_command_admission_preserves_or_replaces(void) {
    RcWorldConfig cfg = rc_preset_base_only();
    RcWorld *world = rc_world_create_config(&cfg);
    assert(world);
    HandlerCtx continuing = {
        .result = rc_interaction_result_continue_approach(0),
    };
    RcInteractionDispatchKey key = widget_key(548, 40, 1);
    assert(rc_interaction_register_world_handler(
        world, &key, fixed_handler, &continuing));
    key = widget_key(548, 41, 1);
    assert(rc_interaction_register_world_handler(
        world, &key, fixed_handler, &continuing));

    RcInteractionTarget target = widget_target(548, 40, 1);
    assert(rc_interaction_begin(&world->player, 0,
                                RC_INTERACTION_WIDGET_ACTION,
                                "Existing", &target, 0));
    uint64_t generation = world->player.interaction.generation;

    assert(rc_player_widget_action(world, -1, -1, 1));
    rc_world_tick(world);
    assert(rc_interaction_is_active(&world->player));
    assert(world->player.interaction.generation == generation);
    assert(world->player.interaction.target.component_id == 40);
    assert(world->player_commands.last_result
           == RC_COMMAND_RESULT_REJECTED_INVALID);
    const RcInteractionOutcome *outcome =
        rc_interaction_last_outcome(&world->player);
    assert(outcome->failure == RC_INTERACTION_FAIL_INVALID_TARGET);
    assert(strcmp(outcome->message,
                  "That action is no longer available.") == 0);

    world->player.skill_action = 7;
    world->player.skill_ready_tick = world->tick + 10;
    world->player.storage_kind = RC_STORAGE_BANK;
    world->player.storage_target = 123;
    world->player.storage_option = 0;
    int start_x = world->player.x;
    int start_y = world->player.y;
    world->player.traversal = (RcTraversalState){
        .active = true,
        .generation = 1,
        .phase = RC_TRAVERSAL_PHASE_TAKEOFF,
        .destination_x = start_x + 10,
        .destination_y = start_y + 10,
        .destination_plane = world->player.plane,
        .ready_tick = world->tick,
    };
    world->player_action.active = true;
    world->player_action.owner = RC_ACTION_OWNER_TRAVERSAL;
    world->player_action.category = RC_ACTION_CATEGORY_STRONG;
    world->player_action.ready_tick = world->tick;
    assert(rc_player_widget_action(world, 548, 41, 1));
    rc_world_tick(world);
    assert(rc_interaction_is_active(&world->player));
    assert(world->player.interaction.generation != generation);
    assert(world->player.interaction.target.component_id == 41);
    assert(world->player.skill_action == 0);
    assert(world->player.storage_kind == RC_STORAGE_NONE);
    assert(!world->player.traversal.active);
    assert(world->player.x == start_x);
    assert(world->player.y == start_y);
    assert(world->player_commands.last_result
           == RC_COMMAND_RESULT_EXECUTED);
    rc_world_destroy(world);
}

int main(void) {
    test_defaults_supported_vocabulary_and_capacity();
    test_generation_owned_reentrant_replacement();
    test_deterministic_precedence();
    test_terminal_outcomes_and_actor_state();
    test_source_revalidation_and_non_disruptive_examine();
    test_command_admission_preserves_or_replaces();
    return 0;
}
