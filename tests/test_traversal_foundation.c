#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "api.h"
#include "config.h"
#include "interaction.h"
#include "player_command.h"
#include "traversal.h"

static RcWorld *make_world(uint32_t seed) {
    RcWorldConfig cfg = rc_preset_base_only();
    cfg.seed = seed;
    RcWorld *world = rc_world_create_config(&cfg);
    assert(world);
    world->enabled |= RC_SUB_TRAVERSAL;
    return world;
}

static void begin_object_interaction(RcWorld *world, int object_id,
                                     int option, uint64_t key) {
    RcInteractionTarget target = {
        .kind = RC_INTERACTION_OBJECT,
        .entity_uid = -1,
        .definition_id = object_id,
        .placement_key = key,
        .content_group = -1,
        .tile_x = world->player.x,
        .tile_y = world->player.y,
        .plane = world->player.plane,
        .footprint_width = 1,
        .footprint_height = 1,
        .inventory_slot = -1,
        .equipment_slot = -1,
        .widget_id = -1,
        .component_id = -1,
        .ground_item_instance = -1,
    };
    assert(rc_interaction_begin(
        &world->player, 0, rc_interaction_op_from_option(option),
        "Travel", &target, 1));
}

static void test_destination_policies(void) {
    RcWorld *first = make_world(1234);
    RcWorld *second = make_world(1234);
    RcTraversalDestination source = {3200, 3400, 1};
    RcTraversalDestination out = {0};
    RcTraversalDestinationSpec spec = {
        .policy = RC_TRAVERSAL_POLICY_EXACT,
        .destination = {3210, 3410, 2},
    };
    assert(rc_traversal_resolve_destination(first, &spec, &source, &out));
    assert(out.x == 3210 && out.y == 3410 && out.plane == 2);

    spec = (RcTraversalDestinationSpec){
        .policy = RC_TRAVERSAL_POLICY_RELATIVE,
        .delta_x = -3,
        .delta_y = 4,
        .delta_plane = -1,
    };
    assert(rc_traversal_resolve_destination(first, &spec, &source, &out));
    assert(out.x == 3197 && out.y == 3404 && out.plane == 0);

    spec = (RcTraversalDestinationSpec){
        .policy = RC_TRAVERSAL_POLICY_RANDOM_AREA,
        .area_min_x = 3153,
        .area_min_y = 3923,
        .area_max_x = 3154,
        .area_max_y = 3924,
        .area_plane = 0,
    };
    RcTraversalDestination random_first;
    RcTraversalDestination random_second;
    assert(rc_traversal_resolve_destination(
        first, &spec, &source, &random_first));
    assert(rc_traversal_resolve_destination(
        second, &spec, &source, &random_second));
    assert(memcmp(&random_first, &random_second,
                  sizeof(random_first)) == 0);
    assert(random_first.x >= 3153 && random_first.x <= 3154);
    assert(random_first.y >= 3923 && random_first.y <= 3924);

    RcTraversalDestination choices[] = {
        {3000, 3000, 0}, {3010, 3010, 0},
    };
    spec = (RcTraversalDestinationSpec){
        .policy = RC_TRAVERSAL_POLICY_CHOICE,
        .choices = choices,
        .choice_count = 2,
        .choice_index = 1,
    };
    assert(rc_traversal_resolve_destination(first, &spec, &source, &out));
    assert(out.x == 3010 && out.y == 3010);
    spec.choice_index = 2;
    assert(!rc_traversal_resolve_destination(first, &spec, &source, &out));

    spec = (RcTraversalDestinationSpec){
        .policy = RC_TRAVERSAL_POLICY_CONTEXTUAL_RETURN,
        .destination = {3087, 3496, 0},
    };
    assert(!rc_traversal_resolve_destination(first, &spec, &source, &out));
    spec.context_available = true;
    assert(rc_traversal_resolve_destination(first, &spec, &source, &out));

    spec.policy = RC_TRAVERSAL_POLICY_INSTANCE_MAPPED;
    spec.context_available = false;
    assert(!rc_traversal_resolve_destination(first, &spec, &source, &out));
    spec.instance_mapping_available = true;
    assert(rc_traversal_resolve_destination(first, &spec, &source, &out));

    rc_world_destroy(second);
    rc_world_destroy(first);
}

static void test_owned_lifecycle_and_outcome(void) {
    RcWorld *world = make_world(77);
    int source_x = world->player.x;
    int source_y = world->player.y;
    begin_object_interaction(world, 100, 0, 55);
    uint64_t interaction_generation = world->player.interaction.generation;
    RcTraversalEdge edge = {
        .kind = RC_TRAVERSAL_OBJECT,
        .option = 0,
        .source_semantics = RC_TRAVERSAL_SOURCE_PLAYER_TILE,
        .start_plane = 0,
        .dest_plane = 0,
        .source_id = 100,
        .start_x = (uint16_t)source_x,
        .start_y = (uint16_t)source_y,
        .dest_x = (uint16_t)(source_x + 8),
        .dest_y = (uint16_t)(source_y + 8),
    };
    RcTraversalDestinationSpec destination = {
        .policy = RC_TRAVERSAL_POLICY_EXACT,
        .destination = {
            .x = edge.dest_x,
            .y = edge.dest_y,
            .plane = edge.dest_plane,
        },
    };
    RcTraversalMotionSpec motion = {
        .presentation = RC_TRAVERSAL_PRESENTATION_TELEPORT,
        .takeoff_ticks = 1,
        .landing_ticks = 1,
    };
    assert(rc_traversal_plan_object(
        world, &edge, 55, interaction_generation, &destination, &motion));
    const RcTraversalState *state = rc_traversal_state(&world->player);
    assert(state->active && state->phase == RC_TRAVERSAL_PHASE_APPROACH);
    uint64_t generation = state->generation;
    const RcTraversalEvent *event =
        rc_traversal_last_event(&world->player);
    assert(event->generation == generation);
    assert(event->phase == RC_TRAVERSAL_PHASE_APPROACH);
    assert(event->destination_x == edge.dest_x);

    assert(rc_traversal_start(world, interaction_generation));
    assert(rc_interaction_apply_result(
        &world->player, interaction_generation,
        rc_interaction_result_handoff()));
    assert(!world->player.interaction.active);
    assert(world->player.traversal.phase == RC_TRAVERSAL_PHASE_TAKEOFF);

    rc_world_tick(world);
    assert(world->player.x == source_x && world->player.y == source_y);
    rc_world_tick(world);
    assert(world->player.x == edge.dest_x);
    assert(world->player.y == edge.dest_y);
    assert(world->player.traversal.active);
    assert(world->player.traversal.phase == RC_TRAVERSAL_PHASE_LANDING);
    assert(rc_traversal_last_outcome(&world->player)->sequence == 0);

    rc_world_tick(world);
    assert(!world->player.traversal.active);
    const RcTraversalOutcome *outcome =
        rc_traversal_last_outcome(&world->player);
    assert(outcome->generation == generation);
    assert(outcome->code == RC_TRAVERSAL_RESULT_SUCCESS);
    assert(outcome->message[0] == '\0');
    rc_world_destroy(world);
}

static void test_source_ownership_and_cancellation(void) {
    RcWorld *world = make_world(88);
    begin_object_interaction(world, 200, 1, 99);
    uint64_t interaction_generation = world->player.interaction.generation;
    RcTraversalEdge edge = {
        .kind = RC_TRAVERSAL_ITEM,
        .option = 1,
        .source_semantics = RC_TRAVERSAL_SOURCE_PLAYER_TILE,
        .dest_plane = 0,
        .source_id = 200,
        .dest_x = 3200,
        .dest_y = 3400,
    };
    RcTraversalDestinationSpec destination = {
        .policy = RC_TRAVERSAL_POLICY_EXACT,
        .destination = {3200, 3400, 0},
    };
    RcTraversalMotionSpec motion = {
        .presentation = RC_TRAVERSAL_PRESENTATION_INSTANT,
    };
    assert(!rc_traversal_plan_object(
        world, &edge, 99, interaction_generation, &destination, &motion));
    edge.kind = RC_TRAVERSAL_OBJECT;
    edge.flags = 2;
    assert(!rc_traversal_plan_object(
        world, &edge, 99, interaction_generation, &destination, &motion));
    edge.flags = 0;
    edge.source_semantics = RC_TRAVERSAL_SOURCE_NONE;
    assert(!rc_traversal_plan_object(
        world, &edge, 99, interaction_generation, &destination, &motion));
    edge.source_semantics = RC_TRAVERSAL_SOURCE_PLAYER_TILE;
    motion.takeoff_ticks = 1;
    assert(!rc_traversal_plan_object(
        world, &edge, 99, interaction_generation, &destination, &motion));
    motion.takeoff_ticks = 0;
    assert(rc_traversal_plan_object(
        world, &edge, 99, interaction_generation, &destination, &motion));
    uint64_t replaced_generation = world->player.traversal.generation;
    begin_object_interaction(world, 201, 1, 100);
    interaction_generation = world->player.interaction.generation;
    edge.source_id = 201;
    assert(rc_traversal_plan_object(
        world, &edge, 100, interaction_generation, &destination, &motion));
    const RcTraversalOutcome *replaced =
        rc_traversal_last_outcome(&world->player);
    assert(replaced->generation == replaced_generation);
    assert(replaced->code == RC_TRAVERSAL_RESULT_CANCELLED);
    assert(replaced->cancel_reason == RC_ACTION_CANCEL_REPLACED);
    uint64_t generation = world->player.traversal.generation;
    assert(generation != replaced_generation);
    rc_player_cancel_action(world, RC_ACTION_CANCEL_FRONTEND);
    assert(!world->player.traversal.active);
    const RcTraversalOutcome *outcome =
        rc_traversal_last_outcome(&world->player);
    assert(outcome->generation == generation);
    assert(outcome->code == RC_TRAVERSAL_RESULT_CANCELLED);
    uint64_t sequence = outcome->sequence;
    rc_player_cancel_action(world, RC_ACTION_CANCEL_FRONTEND);
    assert(rc_traversal_last_outcome(&world->player)->sequence == sequence);
    rc_world_destroy(world);
}

static void test_exact_motion_timing_and_facing(void) {
    RcWorld *world = make_world(99);
    int source_x = world->player.x;
    int source_y = world->player.y;
    begin_object_interaction(world, 300, 0, 101);
    uint64_t interaction_generation = world->player.interaction.generation;
    RcTraversalEdge edge = {
        .kind = RC_TRAVERSAL_OBJECT,
        .option = 0,
        .source_semantics = RC_TRAVERSAL_SOURCE_PLAYER_TILE,
        .source_id = 300,
        .start_x = (uint16_t)source_x,
        .start_y = (uint16_t)source_y,
        .start_plane = 0,
        .dest_x = (uint16_t)(source_x + 3),
        .dest_y = (uint16_t)(source_y + 1),
        .dest_plane = 0,
    };
    RcTraversalDestinationSpec destination = {
        .policy = RC_TRAVERSAL_POLICY_RELATIVE,
        .delta_x = 3,
        .delta_y = 1,
    };
    RcTraversalMotionSpec motion = {
        .presentation = RC_TRAVERSAL_PRESENTATION_EXACT_MOVE,
        .takeoff_ticks = 1,
        .transit_ticks = 2,
        .landing_ticks = 1,
        .has_facing = true,
        .facing_x = source_x + 1,
        .facing_y = source_y,
    };
    assert(rc_traversal_plan_object(
        world, &edge, 101, interaction_generation, &destination, &motion));
    assert(world->player.traversal.policy == RC_TRAVERSAL_POLICY_RELATIVE);
    assert(world->player.traversal.approach_x == source_x);
    assert(world->player.traversal.approach_y == source_y);
    assert(rc_traversal_start(world, interaction_generation));
    assert(rc_interaction_apply_result(
        &world->player, interaction_generation,
        rc_interaction_result_handoff()));
    assert(world->player.facing_x == source_x + 1);
    assert(world->player.facing_y == source_y);

    rc_world_tick(world);
    assert(world->player.traversal.phase == RC_TRAVERSAL_PHASE_TAKEOFF);
    assert(world->player.x == source_x && world->player.y == source_y);
    // Standing still, the pending face square goes out as an east angle.
    assert(world->player.facing_x == -1 && world->player.facing_y == -1);
    assert(world->player.face_angle == 1536);
    rc_world_tick(world);
    assert(world->player.traversal.phase == RC_TRAVERSAL_PHASE_TRANSIT);
    const RcTraversalEvent *event =
        rc_traversal_last_event(&world->player);
    assert(event->phase == RC_TRAVERSAL_PHASE_TRANSIT);
    assert(event->transit_ticks == 2);
    assert(event->has_facing);
    assert(world->player.x == source_x && world->player.y == source_y);
    rc_world_tick(world);
    assert(world->player.x == source_x && world->player.y == source_y);
    rc_world_tick(world);
    assert(world->player.x == source_x + 3);
    assert(world->player.y == source_y + 1);
    assert(world->player.traversal.phase == RC_TRAVERSAL_PHASE_LANDING);
    rc_world_tick(world);
    assert(!world->player.traversal.active);
    assert(rc_traversal_last_outcome(&world->player)->code
           == RC_TRAVERSAL_RESULT_SUCCESS);
    rc_world_destroy(world);
}

static void test_relative_policy_uses_approach_tile(void) {
    RcWorld *world = make_world(101);
    int object_x = world->player.x;
    int object_y = world->player.y;
    begin_object_interaction(world, 350, 0, 151);
    uint64_t interaction_generation = world->player.interaction.generation;
    RcTraversalEdge edge = {
        .kind = RC_TRAVERSAL_OBJECT,
        .option = 0,
        .source_semantics = RC_TRAVERSAL_SOURCE_PLAYER_TILE,
        .source_id = 350,
        .start_x = (uint16_t)(object_x - 1),
        .start_y = (uint16_t)(object_y + 1),
        .start_plane = 0,
        .dest_x = (uint16_t)(object_x + 1),
        .dest_y = (uint16_t)(object_y + 1),
        .dest_plane = 0,
    };
    RcTraversalDestinationSpec destination = {
        .policy = RC_TRAVERSAL_POLICY_RELATIVE,
        .delta_x = 2,
    };
    RcTraversalMotionSpec motion = {
        .presentation = RC_TRAVERSAL_PRESENTATION_INSTANT,
    };
    assert(rc_traversal_plan_object(
        world, &edge, 151, interaction_generation, &destination, &motion));
    assert(world->player.traversal.source_x == object_x);
    assert(world->player.traversal.source_y == object_y);
    assert(world->player.traversal.approach_x == object_x - 1);
    assert(world->player.traversal.approach_y == object_y + 1);
    assert(world->player.traversal.destination_x == object_x + 1);
    assert(world->player.traversal.destination_y == object_y + 1);
    rc_world_destroy(world);
}

static void test_relocation_failure_is_terminal(void) {
    RcWorld *world = make_world(111);
    int source_x = world->player.x;
    int source_y = world->player.y;
    world->enabled |= RC_SUB_LOOT;
    snprintf(world->ground_item_spawns_path,
             sizeof(world->ground_item_spawns_path), "%s",
             "/missing/runec-ground-items.bin");
    begin_object_interaction(world, 400, 0, 202);
    uint64_t interaction_generation = world->player.interaction.generation;
    RcTraversalEdge edge = {
        .kind = RC_TRAVERSAL_OBJECT,
        .option = 0,
        .source_semantics = RC_TRAVERSAL_SOURCE_PLAYER_TILE,
        .source_id = 400,
        .start_x = (uint16_t)source_x,
        .start_y = (uint16_t)source_y,
        .start_plane = 0,
        .dest_x = (uint16_t)(source_x + 64),
        .dest_y = (uint16_t)source_y,
        .dest_plane = 0,
    };
    RcTraversalDestinationSpec destination = {
        .policy = RC_TRAVERSAL_POLICY_EXACT,
        .destination = {source_x + 64, source_y, 0},
    };
    RcTraversalMotionSpec motion = {
        .presentation = RC_TRAVERSAL_PRESENTATION_INSTANT,
    };
    assert(rc_traversal_plan_object(
        world, &edge, 202, interaction_generation, &destination, &motion));
    uint64_t generation = world->player.traversal.generation;
    assert(rc_traversal_start(world, interaction_generation));
    assert(rc_interaction_apply_result(
        &world->player, interaction_generation,
        rc_interaction_result_handoff()));
    rc_world_tick(world);
    assert(world->player.x == source_x && world->player.y == source_y);
    assert(!world->player.traversal.active);
    const RcTraversalOutcome *outcome =
        rc_traversal_last_outcome(&world->player);
    assert(outcome->generation == generation);
    assert(outcome->code == RC_TRAVERSAL_RESULT_DESTINATION_UNAVAILABLE);
    assert(outcome->message[0] != '\0');
    uint64_t outcome_sequence = outcome->sequence;
    rc_world_tick(world);
    assert(rc_traversal_last_outcome(&world->player)->sequence
           == outcome_sequence);
    rc_world_destroy(world);
}

int main(void) {
    test_destination_policies();
    test_owned_lifecycle_and_outcome();
    test_source_ownership_and_cancellation();
    test_exact_motion_timing_and_facing();
    test_relative_policy_uses_approach_tile();
    test_relocation_failure_is_terminal();
    return 0;
}
