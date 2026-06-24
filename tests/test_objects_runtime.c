#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "api.h"
#include "config.h"
#include "objects.h"
#include "pathfinding.h"
#include "traversal.h"
#include "../rc-viewer/object_action_visuals.h"

#define ODEF_PATH RC_TEST_SOURCE_DIR "/data/defs/object_defs.bin"
#define OPLC_PATH RC_TEST_SOURCE_DIR "/data/defs/object_placements.bin"
#define OBHV_PATH RC_TEST_SOURCE_DIR "/data/defs/object_behaviors.bin"
#define OTRP_PATH RC_TEST_SOURCE_DIR "/data/defs/object_transports.bin"
#define GNOD_PATH RC_TEST_SOURCE_DIR "/data/defs/gathering_nodes.bin"
#define BAD_PATH "/tmp/runec_bad_object.bin"

static void write_bad_header(void) {
    uint32_t bad[3] = {0, 1, 0};
    FILE *f = fopen(BAD_PATH, "wb");
    assert(f != NULL);
    assert(fwrite(bad, sizeof(bad), 1, f) == 1);
    fclose(f);
}

static int has_object_at(int obj_id, int x, int y, int plane) {
    RcObjectPlacement rows[16];
    int n = rc_object_placements_at(x, y, plane, rows, 16);
    for (int i = 0; i < n; i++) {
        if ((int)rows[i].obj_id == obj_id) return 1;
    }
    return 0;
}

static int object_at(int obj_id, int x, int y, int plane,
                     RcObjectPlacement *out) {
    RcObjectPlacement rows[16];
    int n = rc_object_placements_at(x, y, plane, rows, 16);
    for (int i = 0; i < n; i++) {
        if ((int)rows[i].obj_id != obj_id)
            continue;
        if (out) *out = rows[i];
        return 1;
    }
    return 0;
}

static void pair_delta(int rotation, int *dx, int *dy) {
    int r = rotation & 3;
    *dx = 0;
    *dy = 0;
    if (r == 0) *dy = 1;
    else if (r == 1) *dx = 1;
    else if (r == 2) *dy = -1;
    else *dx = -1;
}

static int find_dynamic_pair(RcObjectPlacement *left,
                             RcObjectPlacement *right) {
    for (int i = 0; i < g_rc_object_placement_count; i++) {
        RcObjectPlacement a = g_rc_object_placements[i];
        const RcObjectBehavior *ab = rc_object_behavior_get((int)a.obj_id);
        if (!ab || !(ab->flags & RC_OBJ_BEHAVIOR_PAIR_LEFT)
                || ab->next_loc_stage < 0)
            continue;
        int dx, dy;
        pair_delta(a.rotation, &dx, &dy);
        RcObjectPlacement rows[16];
        int n = rc_object_placements_at(a.x + dx, a.y + dy, a.plane,
                                        rows, 16);
        for (int j = 0; j < n; j++) {
            const RcObjectBehavior *bb =
                rc_object_behavior_get((int)rows[j].obj_id);
            if (bb && (bb->flags & RC_OBJ_BEHAVIOR_PAIR_RIGHT)
                    && bb->next_loc_stage >= 0
                    && rows[j].type == a.type
                    && (rows[j].rotation & 3) == (a.rotation & 3)) {
                *left = a;
                *right = rows[j];
                return 1;
            }
        }
    }
    return 0;
}

static RcObjectState *find_state(RcWorld *world, int obj_id, int x, int y,
                                 int plane) {
    for (int i = 0; i < world->object_state_count; i++) {
        RcObjectState *st = &world->object_states[i];
        if (st->base_obj_id == obj_id && st->x == x && st->y == y
                && st->plane == plane)
            return st;
    }
    return NULL;
}

static void place_player_adjacent(RcWorld *world, int x, int y, int plane) {
    assert(world != NULL);
    world->player.x = x + 1;
    world->player.y = y;
    world->player.plane = plane;
    world->player.prev_x = world->player.x;
    world->player.prev_y = world->player.y;
}

static void init_single_region_map(RcWorld *world, int region_x,
                                   int region_y) {
    assert(world != NULL);
    memset(&world->map, 0, sizeof(world->map));
    world->map.region_count = 1;
    world->map.regions[0].region_x = region_x;
    world->map.regions[0].region_y = region_y;
    world->map.regions[0].loaded = 1;
}

int main(void) {
    write_bad_header();
    assert(rc_load_object_defs(NULL) == -1);
    assert(rc_load_object_behaviors(NULL) == -1);
    assert(rc_load_object_placements(NULL) == -1);
    assert(rc_load_object_transports(NULL) == -1);
    assert(rc_load_object_defs("/missing/object_defs.bin") == -1);
    assert(rc_load_object_defs(BAD_PATH) == -1);
    assert(rc_load_object_behaviors(BAD_PATH) == -1);
    assert(rc_load_object_placements(BAD_PATH) == -1);
    assert(rc_load_object_transports(BAD_PATH) == -1);
    assert(rc_load_object_defs(ODEF_PATH) > 60000);
    assert(g_rc_object_param_count > 1000);
    assert(rc_object_def_param_int((int)g_rc_object_params[0].obj_id,
                                   (int)g_rc_object_params[0].key,
                                   -12345) == g_rc_object_params[0].value);
    assert(rc_object_def_param_int(1276, -1, 42) == 42);
    assert(rc_load_object_behaviors(OBHV_PATH) > 8000);
    RuneCObjectActionVisualMap object_visuals;
    assert(runec_object_action_visuals_load(&object_visuals, OBHV_PATH)
           > 8000);
    const RuneCObjectActionVisualRecord *ladder_visual =
        runec_object_action_visual_find(&object_visuals, 42207);
    assert(ladder_visual && ladder_visual->climb_anim == 828);
    assert(rc_load_object_placements(OPLC_PATH) > 4700000);
    assert(rc_load_object_transports(OTRP_PATH) > 29000);

    const RcObjectDef *tree = rc_object_def_get(1276);
    const RcObjectDef *bank = rc_object_def_get(10355);
    const RcObjectDef *ladder = rc_object_def_get(42207);
    const RcObjectDef *force_loc = rc_object_def_get(2);
    const RcObjectDef *transforming = rc_object_def_get(15061);
    assert(rc_object_def_get(-1) == NULL);
    assert(rc_object_behavior_get(-1) == NULL);
    assert(tree && bank && ladder && force_loc && transforming);
    assert(strcmp(tree->name, "Tree") == 0);
    assert(strcmp(tree->actions[0], "Chop down") == 0);
    assert(strcmp(bank->actions[1], "Bank") == 0);
    assert(strcmp(ladder->actions[0], "Climb-up") == 0);
    assert(force_loc->force_approach == 27);
    assert(transforming->transform_count == 2);
    assert(transforming->transforms[0] == 15052);

    const RcObjectBehavior *tree_b = rc_object_behavior_get(1276);
    const RcObjectBehavior *altar_b = rc_object_behavior_get(409);
    const RcObjectBehavior *bank_b = rc_object_behavior_get(10355);
    const RcObjectBehavior *door_b = rc_object_behavior_get(1535);
    const RcObjectBehavior *varrock_door_b = rc_object_behavior_get(11780);
    const RcObjectBehavior *ladder_b = rc_object_behavior_get(42207);
    const RcObjectBehavior *lever_b = rc_object_behavior_get(26761);
    assert(tree_b && altar_b && bank_b && door_b && varrock_door_b && ladder_b);
    assert(tree_b->flags & RC_OBJ_BEHAVIOR_RESOURCE);
    assert(tree_b->skill == RC_OBJ_SKILL_WOODCUTTING);
    assert(altar_b->flags & RC_OBJ_BEHAVIOR_ALTAR);
    assert(altar_b->action_mask & 1u);
    assert(bank_b->flags & RC_OBJ_BEHAVIOR_BANK);
    assert(door_b->flags & RC_OBJ_BEHAVIOR_DOOR);
    assert(door_b->next_loc_stage == 1536);
    assert(varrock_door_b->next_loc_stage == 11778);
    assert(ladder_b->flags & RC_OBJ_BEHAVIOR_LADDER);
    assert(ladder_b->flags & RC_OBJ_BEHAVIOR_TRANSPORT);
    assert(lever_b && (lever_b->flags & RC_OBJ_BEHAVIOR_TRANSPORT));
    assert(lever_b->action_mask == 0);

    const RcObjectBehavior *fence_l_b = rc_object_behavior_get(1558);
    const RcObjectBehavior *fence_r_b = rc_object_behavior_get(1560);
    assert(fence_l_b && fence_r_b);
    assert(fence_l_b->flags & RC_OBJ_BEHAVIOR_PAIR_LEFT);
    assert(fence_r_b->flags & RC_OBJ_BEHAVIOR_PAIR_RIGHT);
    assert(fence_l_b->next_loc_stage == 1559);
    assert(fence_r_b->next_loc_stage == 1567);

    int region_count = 0;
    const RcObjectPlacement *region = rc_object_region_placements(4921,
                                                                  &region_count);
    assert(region && region_count > 0);
    assert(region[0].key != 0);
    if (region_count > 1) assert(region[0].key != region[1].key);
    assert(rc_object_region_placements(65535, &region_count) == NULL);
    assert(region_count == 0);
    assert(has_object_at(1276, 1239, 3672, 0));
    assert(rc_object_placements_at(-1, 3672, 0, NULL, 0) == 0);

    const RcObjectTransport *tr = rc_object_transport_find(16683, 2465, 3495,
                                                           0, 0);
    assert(tr != NULL);
    assert(tr->dest_plane == 1);
    assert(strcmp(tr->action, "Climb-up") == 0);
    assert(rc_object_transport_find(16683, 2465, 3495, 0, 4) == NULL);
    assert(rc_object_transport_find(34810, 3185, 3436, 0, 1) == NULL);

    RcWorldConfig cfg = rc_preset_base_only();
    cfg.subsystems = RC_SUB_OBJECTS;
    cfg.object_defs_path = ODEF_PATH;
    cfg.object_placements_path = OPLC_PATH;
    cfg.object_behaviors_path = OBHV_PATH;
    cfg.object_transports_path = OTRP_PATH;
    RcWorld *world = rc_world_create_config(&cfg);
    assert(world != NULL);
    assert(world->enabled & RC_SUB_OBJECTS);
    rc_player_interact_object(world, 1276, 0);
    assert(world->player.interact_type == RC_INTERACT_OBJECT);
    assert(world->player.interact_target == 1276);
    assert(world->player.interact_option == 0);
    rc_player_interact_object(world, 1276, 4);
    assert(world->player.interact_option == 0);
    rc_world_destroy(world);

    RcWorldConfig door_cfg = rc_preset_base_only();
    door_cfg.subsystems = RC_SUB_OBJECTS;
    door_cfg.object_defs_path = ODEF_PATH;
    door_cfg.object_behaviors_path = OBHV_PATH;
    RcWorld *door = rc_world_create_config(&door_cfg);
    assert(door != NULL);
    RcObjectPlacement door_placement;
    assert(object_at(11780, 3196, 3384, 0, &door_placement));
    place_player_adjacent(door, 3196, 3384, 0);
    assert(rc_player_interact_object_placement(
        door, 11780, 3196, 3384, 0, UINT64_MAX, 0) == 0);
    assert(rc_player_interact_object_placement(
        door, 11780, 3196, 3384, 0, door_placement.key, 0) == 1);
    for (int i = 0; i < 4 && door->object_state_count == 0; i++) {
        rc_world_tick(door);
    }
    assert(door->object_state_count == 1);
    assert(door->object_states[0].flags & RC_OBJECT_STATE_OPEN);
    assert(door->object_states[0].flags & RC_OBJECT_STATE_DYNAMIC);
    assert(door->object_states[0].active_obj_id == 11778);
    assert(door->object_states[0].active_x == 3195);
    assert(door->object_states[0].active_y == 3384);
    assert(door->object_states[0].active_plane == 0);
    assert(door->object_states[0].active_rotation == 1);
    assert(door->object_states[0].revert_tick > door->tick);
    assert(rc_world_object_active_id(door, 11780, 3196, 3384, 0) == 11778);
    RcObjectState active_state;
    assert(rc_world_object_active_state(door, 11780, 3196, 3384, 0,
                                        &active_state) == 1);
    assert(rc_world_object_active_state_by_key(
        door, door_placement.key, &active_state) == 1);
    assert(active_state.placement_key != 0);
    assert(active_state.placement_key == door_placement.key);
    assert(active_state.base_obj_id == 11780);
    assert(active_state.active_obj_id == 11778);
    rc_world_tick(door);
    door->player.x = active_state.active_x;
    door->player.y = active_state.active_y + 1;
    door->player.plane = active_state.active_plane;
    assert(rc_player_interact_object_placement(
        door, active_state.active_obj_id, active_state.active_x,
        active_state.active_y, active_state.active_plane,
        active_state.placement_key, 0) == 1);
    rc_world_tick(door);
    assert(rc_world_object_active_state(door, 11780, 3196, 3384, 0,
                                        &active_state) == 1);
    assert((active_state.flags & RC_OBJECT_STATE_OPEN) == 0);
    assert(active_state.active_obj_id == active_state.base_obj_id);
    assert(active_state.active_x == active_state.x);
    assert(active_state.active_y == active_state.y);
    rc_world_destroy(door);

    RcObjectPlacement gate_left, gate_right;
    assert(find_dynamic_pair(&gate_left, &gate_right));
    RcWorldConfig gate_cfg = rc_preset_base_only();
    gate_cfg.subsystems = RC_SUB_OBJECTS;
    gate_cfg.object_defs_path = ODEF_PATH;
    gate_cfg.object_placements_path = OPLC_PATH;
    gate_cfg.object_behaviors_path = OBHV_PATH;
    RcWorld *gate = rc_world_create_config(&gate_cfg);
    assert(gate != NULL);
    gate->player.x = gate_left.x;
    gate->player.y = gate_left.y;
    gate->player.plane = gate_left.plane;
    assert(rc_player_interact_object_at(gate, (int)gate_left.obj_id,
                                        gate_left.x, gate_left.y,
                                        gate_left.plane, 0) == 1);
    for (int i = 0; i < 4 && gate->object_state_count < 2; i++)
        rc_world_tick(gate);
    RcObjectState *left_state =
        find_state(gate, (int)gate_left.obj_id, gate_left.x, gate_left.y,
                   gate_left.plane);
    RcObjectState *right_state =
        find_state(gate, (int)gate_right.obj_id, gate_right.x, gate_right.y,
                   gate_right.plane);
    const RcObjectBehavior *left_b =
        rc_object_behavior_get((int)gate_left.obj_id);
    const RcObjectBehavior *right_b =
        rc_object_behavior_get((int)gate_right.obj_id);
    assert(left_state && right_state && left_b && right_b);
    assert(left_state->flags & RC_OBJECT_STATE_OPEN);
    assert(right_state->flags & RC_OBJECT_STATE_OPEN);
    assert(left_state->active_obj_id == left_b->next_loc_stage);
    assert(right_state->active_obj_id == right_b->next_loc_stage);
    assert(gate->object_state_count == 2);
    rc_world_tick(gate);
    gate->player.x = left_state->active_x;
    gate->player.y = left_state->active_y;
    gate->player.plane = left_state->active_plane;
    assert(rc_player_interact_object_at(gate, left_state->active_obj_id,
                                        left_state->active_x,
                                        left_state->active_y,
                                        left_state->active_plane, 0) == 1);
    rc_world_tick(gate);
    assert((left_state->flags & RC_OBJECT_STATE_OPEN) == 0);
    assert((right_state->flags & RC_OBJECT_STATE_OPEN) == 0);
    assert(left_state->active_obj_id == left_state->base_obj_id);
    assert(right_state->active_obj_id == right_state->base_obj_id);
    assert(left_state->active_x == left_state->x);
    assert(right_state->active_x == right_state->x);
    assert(left_state->active_y == left_state->y);
    assert(right_state->active_y == right_state->y);
    rc_world_destroy(gate);

    RcWorld *same_id_gate = rc_world_create_config(&gate_cfg);
    assert(same_id_gate != NULL);
    const int same_id_left_obj = 15510;
    const int same_id_right_obj = 15512;
    const int same_id_x = 3141;
    const int same_id_y = 3457;
    assert(has_object_at(same_id_left_obj, same_id_x, same_id_y, 0));
    assert(has_object_at(same_id_right_obj, same_id_x + 1, same_id_y, 0));
    same_id_gate->player.x = same_id_x;
    same_id_gate->player.y = same_id_y;
    same_id_gate->player.plane = 0;
    assert(rc_player_interact_object_at(same_id_gate, same_id_left_obj,
                                        same_id_x, same_id_y, 0, 0) == 1);
    for (int i = 0; i < 4 && same_id_gate->object_state_count < 2; i++)
        rc_world_tick(same_id_gate);
    RcObjectState *same_id_left =
        find_state(same_id_gate, same_id_left_obj, same_id_x, same_id_y, 0);
    RcObjectState *same_id_right =
        find_state(same_id_gate, same_id_right_obj, same_id_x + 1,
                   same_id_y, 0);
    assert(same_id_left && same_id_right);
    assert(same_id_left->flags & RC_OBJECT_STATE_OPEN);
    assert(same_id_right->flags & RC_OBJECT_STATE_OPEN);
    assert(same_id_left->active_obj_id == same_id_left_obj);
    assert(same_id_right->active_obj_id == same_id_right_obj);
    assert(same_id_left->active_x != same_id_left->x
           || same_id_left->active_y != same_id_left->y
           || same_id_left->active_rotation != same_id_left->base_rotation);
    assert(same_id_right->active_x != same_id_right->x
           || same_id_right->active_y != same_id_right->y
           || same_id_right->active_rotation != same_id_right->base_rotation);
    rc_world_destroy(same_id_gate);

    RcWorldConfig door_collision_cfg = rc_preset_base_only();
    door_collision_cfg.subsystems = RC_SUB_OBJECTS;
    door_collision_cfg.object_defs_path = ODEF_PATH;
    door_collision_cfg.object_placements_path = OPLC_PATH;
    door_collision_cfg.object_behaviors_path = OBHV_PATH;
    RcWorld *door_collision = rc_world_create_config(&door_collision_cfg);
    assert(door_collision != NULL);
    const int door_obj = 11780;
    const int door_x = 3196;
    const int door_y = 3384;
    assert(has_object_at(door_obj, door_x, door_y, 0));
    init_single_region_map(door_collision, door_x >> 6, door_y >> 6);
    int lx = door_x & 63;
    int ly = door_y & 63;
    door_collision->map.regions[0].tiles[0][lx][ly].collision_flags =
        COL_WALL_W;
    door_collision->map.regions[0].tiles[0][lx - 1][ly].collision_flags =
        COL_WALL_E;
    door_collision->player.x = door_x - 1;
    door_collision->player.y = door_y;
    door_collision->player.plane = 0;
    assert(!rc_can_move(&door_collision->map, door_x - 1, door_y, 1, 0, 0));
    assert(rc_player_interact_object_at(door_collision, door_obj, door_x,
                                        door_y, 0, 0) == 1);
    for (int i = 0; i < 4 && door_collision->object_state_count == 0; i++)
        rc_world_tick(door_collision);
    assert(door_collision->object_state_count == 1);
    assert(door_collision->object_states[0].flags & RC_OBJECT_STATE_OPEN);
    assert(rc_can_move(&door_collision->map, door_x - 1, door_y, 1, 0, 0));
    int revert_tick = door_collision->object_states[0].revert_tick;
    while (door_collision->tick <= revert_tick) {
        rc_world_tick(door_collision);
    }
    assert((door_collision->object_states[0].flags & RC_OBJECT_STATE_OPEN)
           == 0);
    assert(!rc_can_move(&door_collision->map, door_x - 1, door_y, 1, 0, 0));
    rc_world_destroy(door_collision);

    RcWorldConfig route_cfg = rc_preset_base_only();
    route_cfg.subsystems = RC_SUB_OBJECTS;
    route_cfg.object_defs_path = ODEF_PATH;
    route_cfg.object_placements_path = OPLC_PATH;
    route_cfg.object_behaviors_path = OBHV_PATH;
    RcWorld *route = rc_world_create_config(&route_cfg);
    assert(route != NULL);
    const int route_obj = 1276;
    const int route_x = 1239;
    const int route_y = 3672;
    assert(has_object_at(route_obj, route_x, route_y, 0));
    init_single_region_map(route, route_x >> 6, route_y >> 6);
    route->player.x = route_x - 3;
    route->player.y = route_y;
    route->player.prev_x = route->player.x;
    route->player.prev_y = route->player.y;
    route->player.plane = 0;
    route->map.regions[0].tiles[0][(route_x - 1) & 63][route_y & 63]
        .collision_flags = COL_BLOCK_WALK;
    assert(rc_player_interact_object_at(route, route_obj, route_x, route_y,
                                        0, 0) == 1);
    assert(route->player.interaction.active);
    assert(route->player.route_len > 0);
    for (int i = 0; i < 16 && route->player.interact_type != RC_INTERACT_OBJECT; i++) {
        rc_world_tick(route);
    }
    assert(route->player.interact_type == RC_INTERACT_OBJECT);
    assert(route->player.interact_target == route_obj);
    assert(!(route->player.x == route_x - 1 && route->player.y == route_y));
    int route_dx = route->player.x - route_x;
    int route_dy = route->player.y - route_y;
    if (route_dx < 0) route_dx = -route_dx;
    if (route_dy < 0) route_dy = -route_dy;
    assert((route_dx > route_dy ? route_dx : route_dy) == 1);
    rc_world_destroy(route);

    RcWorld *blocked_route = rc_world_create_config(&route_cfg);
    assert(blocked_route != NULL);
    init_single_region_map(blocked_route, route_x >> 6, route_y >> 6);
    blocked_route->player.x = route_x - 3;
    blocked_route->player.y = route_y;
    blocked_route->player.prev_x = blocked_route->player.x;
    blocked_route->player.prev_y = blocked_route->player.y;
    blocked_route->player.plane = 0;
    for (int bx = route_x - 2; bx <= route_x + 2; bx++) {
        for (int by = route_y - 2; by <= route_y + 2; by++) {
            blocked_route->map.regions[0].tiles[0][bx & 63][by & 63]
                .collision_flags = COL_BLOCK_WALK;
        }
    }
    assert(rc_player_interact_object_at(blocked_route, route_obj, route_x,
                                        route_y, 0, 0) == 0);
    assert(!blocked_route->player.interaction.active);
    assert(blocked_route->player.route_len == 0);
    assert(blocked_route->player.x == route_x - 3);
    assert(blocked_route->player.y == route_y);
    assert(blocked_route->player.interaction.last_failure
           == RC_INTERACTION_FAIL_CANNOT_REACH);
    rc_world_destroy(blocked_route);

    RcWorldConfig altar_cfg = rc_preset_base_only();
    altar_cfg.subsystems = RC_SUB_OBJECTS | RC_SUB_PRAYER;
    altar_cfg.object_defs_path = ODEF_PATH;
    altar_cfg.object_behaviors_path = OBHV_PATH;
    RcWorld *altar = rc_world_create_config(&altar_cfg);
    assert(altar != NULL);
    altar->player.skills.base_level[SKILL_PRAYER] = 43;
    altar->player.current_prayer_points = 0;
    altar->player.prayer_drain_counter = 17;
    assert(rc_player_interact_object_at(altar, 409, -1, -1, -1, 0) == 1);
    assert(altar->player.current_prayer_points == 430);
    assert(altar->player.prayer_drain_counter == 0);
    assert(altar->player.interact_target == 409);
    rc_world_destroy(altar);

    RcWorldConfig skill_cfg = rc_preset_base_only();
    skill_cfg.subsystems = RC_SUB_OBJECTS | RC_SUB_SKILLS;
    skill_cfg.object_defs_path = ODEF_PATH;
    skill_cfg.object_behaviors_path = OBHV_PATH;
    skill_cfg.gathering_nodes_path = GNOD_PATH;
    RcWorld *skill = rc_world_create_config(&skill_cfg);
    assert(skill != NULL);
    place_player_adjacent(skill, 1239, 3672, 0);
    assert(rc_player_interact_object_at(skill, 1276, 1239, 3672, 0, 0) == 1);
    for (int i = 0; i < 4 && skill->object_state_count == 0; i++) {
        rc_world_tick(skill);
    }
    assert(skill->object_state_count == 1);
    assert(skill->object_states[0].flags & RC_OBJECT_STATE_DEPLETED);
    assert(skill->object_states[0].x == 1239);
    assert(skill->object_states[0].y == 3672);
    assert(rc_player_interact_object_at(skill, 1276, 1239, 3672, 0, 0) == 0);
    for (int i = 0; i < 9; i++) rc_world_tick(skill);
    assert((skill->object_states[0].flags & RC_OBJECT_STATE_DEPLETED) == 0);
    rc_world_destroy(skill);

    RcWorldConfig travel_cfg = rc_preset_base_only();
    travel_cfg.subsystems = RC_SUB_OBJECTS | RC_SUB_TRAVERSAL;
    travel_cfg.object_defs_path = ODEF_PATH;
    travel_cfg.object_placements_path = OPLC_PATH;
    travel_cfg.object_behaviors_path = OBHV_PATH;
    travel_cfg.object_transports_path = OTRP_PATH;
    travel_cfg.traversal_edges_path = RC_TEST_SOURCE_DIR "/data/defs/traversal_edges.bin";
    RcWorld *travel = rc_world_create_config(&travel_cfg);
    assert(travel != NULL);
    assert(!has_object_at(16683, 2465, 3495, 0));
    place_player_adjacent(travel, 2465, 3495, 0);
    assert(rc_player_interact_object_at(travel, 16683, 2465, 3495, 0, 0) == 0);

    const RcObjectTransport *dungeon_source_only =
        rc_object_transport_find(17384, 3116, 3451, 0, 0);
    const RcObjectTransport *dungeon_tr =
        rc_object_transport_find(17384, 3117, 3452, 0, 0);
    assert(dungeon_source_only != NULL);
    assert(dungeon_tr != NULL);
    assert(!has_object_at(17384, 3116, 3451, 0));
    assert(has_object_at(17384, 3116, 3452, 0));
    place_player_adjacent(travel, 3116, 3451, 0);
    assert(rc_player_interact_object_at(travel, 17384, 3116, 3451, 0, 0) == 0);
    place_player_adjacent(travel, 3116, 3452, 0);
    assert(rc_player_interact_object_at(travel, 17384, 3116, 3452, 0, 0) == 1);
    for (int i = 0; i < 4 && travel->player.y != dungeon_tr->dest_y; i++) {
        rc_world_tick(travel);
    }
    assert(travel->player.x == dungeon_tr->dest_x);
    assert(travel->player.y == dungeon_tr->dest_y);
    assert(travel->player.plane == dungeon_tr->dest_plane);

    const RcObjectTransport *varrock_rat_pits =
        rc_object_transport_find(10321, 3268, 3400, 0, 0);
    const RcObjectTransport *varrock_rat_pits_return =
        rc_object_transport_find(10309, 2894, 5097, 0, 0);
    assert(varrock_rat_pits != NULL);
    assert(varrock_rat_pits_return != NULL);
    assert(has_object_at(10321, 3267, 3400, 0));
    assert(!has_object_at(10321, 3268, 3400, 0));
    place_player_adjacent(travel, 3268, 3400, 0);
    assert(rc_player_interact_object_at(travel, 10321, 3268, 3400, 0, 0)
           == 0);
    place_player_adjacent(travel, 3267, 3400, 0);
    assert(rc_player_interact_object_at(travel, 10321, 3267, 3400, 0, 0)
           == 1);
    for (int i = 0; i < 4 && travel->player.y != varrock_rat_pits->dest_y; i++) {
        rc_world_tick(travel);
    }
    assert(travel->player.x == varrock_rat_pits->dest_x);
    assert(travel->player.y == varrock_rat_pits->dest_y);
    assert(travel->player.plane == varrock_rat_pits->dest_plane);
    assert(has_object_at(10309, 2895, 5097, 0));
    assert(!has_object_at(10309, 2894, 5097, 0));
    place_player_adjacent(travel, 2894, 5097, 0);
    assert(rc_player_interact_object_at(travel, 10309, 2894, 5097, 0, 0)
           == 0);
    place_player_adjacent(travel, 2895, 5097, 0);
    assert(rc_player_interact_object_at(travel, 10309, 2895, 5097, 0, 0)
           == 1);
    for (int i = 0;
            i < 4 && travel->player.y != varrock_rat_pits_return->dest_y;
            i++) {
        rc_world_tick(travel);
    }
    assert(travel->player.x == varrock_rat_pits_return->dest_x);
    assert(travel->player.y == varrock_rat_pits_return->dest_y);
    assert(travel->player.plane == varrock_rat_pits_return->dest_plane);

    const RcObjectTransport *varrock_manhole =
        rc_object_transport_find(882, 3236, 3458, 0, 0);
    assert(varrock_manhole != NULL);
    assert(has_object_at(881, 3237, 3458, 0));
    travel->player.x = 3236;
    travel->player.y = 3458;
    travel->player.plane = 0;
    travel->player.action_lock_timer = 0;
    travel->player.pending_traversal_active = 0;
    assert(rc_player_interact_object_at(travel, 881, 3237, 3458, 0, 0)
           == 1);
    rc_world_tick(travel);
    RcObjectState manhole_state;
    assert(rc_world_object_active_state(travel, 881, 3237, 3458, 0,
                                        &manhole_state) == 1);
    assert(manhole_state.flags & RC_OBJECT_STATE_OPEN);
    assert(manhole_state.active_obj_id == 882);
    assert(manhole_state.active_x == 3237);
    assert(manhole_state.active_y == 3458);
    assert(manhole_state.active_rotation == manhole_state.base_rotation);
    rc_world_tick(travel);
    assert(rc_player_interact_object_at(travel, 882, manhole_state.active_x,
                                        manhole_state.active_y,
                                        manhole_state.active_plane, 0) == 1);
    for (int i = 0; i < 6 && travel->player.y != varrock_manhole->dest_y; i++)
        rc_world_tick(travel);
    assert(travel->player.x == varrock_manhole->dest_x);
    assert(travel->player.y == varrock_manhole->dest_y);
    assert(travel->player.plane == varrock_manhole->dest_plane);
    assert(rc_world_object_active_state(travel, 881, 3237, 3458, 0,
                                        &manhole_state) == 1);
    assert(manhole_state.flags & RC_OBJECT_STATE_OPEN);

    const RcObjectTransport *rat_pits_enter =
        rc_object_transport_find(24842, 2899, 3469, 0, 0);
    assert(rat_pits_enter != NULL);
    assert(has_object_at(24842, 2899, 3469, 0));
    place_player_adjacent(travel, 2899, 3469, 0);
    assert(rc_player_interact_object_at(travel, 24842, 2899, 3469, 0, 0)
           == 1);
    for (int i = 0; i < 4 && travel->player.y != rat_pits_enter->dest_y; i++) {
        rc_world_tick(travel);
    }
    assert(travel->player.x == rat_pits_enter->dest_x);
    assert(travel->player.y == rat_pits_enter->dest_y);
    assert(travel->player.plane == rat_pits_enter->dest_plane);

    const RcObjectTransport *rat_pits_exit =
        rc_object_transport_find(24687, 2901, 9867, 0, 0);
    const RcTraversalEdge *rat_pits_exit_edge =
        rc_traversal_find(RC_TRAVERSAL_OBJECT, 24687, 2901, 9867, 0, 0);
    assert(rat_pits_exit != NULL);
    assert(rat_pits_exit_edge != NULL);
    assert(has_object_at(24687, 2898, 9867, 0));
    place_player_adjacent(travel, 2898, 9867, 0);
    assert(rc_player_interact_object_at(travel, 24687, 2898, 9867, 0, 0)
           == 1);
    for (int i = 0; i < 4 && travel->player.y != rat_pits_exit->dest_y; i++) {
        rc_world_tick(travel);
    }
    assert(travel->player.x == rat_pits_exit->dest_x);
    assert(travel->player.y == rat_pits_exit->dest_y);
    assert(travel->player.plane == rat_pits_exit->dest_plane);

    const RcObjectTransport *far_tr =
        rc_object_transport_find(398, 3090, 3475, 0, 0);
    assert(far_tr != NULL);
    assert(!has_object_at(398, 3090, 3475, 0));
    assert(has_object_at(398, 3090, 3476, 0));
    place_player_adjacent(travel, 3090, 3475, 0);
    assert(rc_player_interact_object_at(travel, 398, 3090, 3475, 0, 0) == 0);
    place_player_adjacent(travel, 3090, 3476, 0);
    assert(rc_player_interact_object_at(travel, 398, 3090, 3476, 0, 0) == 1);
    for (int i = 0; i < 4 && travel->player.y != far_tr->dest_y; i++) {
        rc_world_tick(travel);
    }
    assert(travel->player.x == far_tr->dest_x);
    assert(travel->player.y == far_tr->dest_y);
    assert(travel->player.plane == far_tr->dest_plane);

    const RcObjectTransport *lever_tr =
        rc_object_transport_find(26761, 3090, 3475, 0, 0);
    assert(lever_tr != NULL);
    place_player_adjacent(travel, 3090, 3475, 0);
    assert(rc_player_interact_object_at(travel, 26761, 3090, 3475, 0, 0) == 1);
    for (int i = 0; i < 4 && travel->player.x != lever_tr->dest_x; i++) {
        rc_world_tick(travel);
    }
    assert(travel->player.x == lever_tr->dest_x);
    assert(travel->player.y == lever_tr->dest_y);
    assert(travel->player.plane == lever_tr->dest_plane);

    const RcTraversalEdge *varrock_ladder =
        rc_traversal_find(RC_TRAVERSAL_OBJECT, 11794, 3214, 3411, 0, 0);
    assert(varrock_ladder != NULL);
    assert(has_object_at(11794, 3214, 3410, 0));
    place_player_adjacent(travel, 3214, 3410, 0);
    assert(rc_player_interact_object_at(travel, 11794, 3214, 3410, 0, 0)
           == 1);
    rc_world_tick(travel);
    assert(travel->player.action_lock_timer > 0);
    assert(travel->player.pending_traversal_active);
    for (int i = 0; i < 4 && travel->player.plane != varrock_ladder->dest_plane; i++) {
        rc_world_tick(travel);
    }
    assert(travel->player.x == varrock_ladder->dest_x);
    assert(travel->player.y == varrock_ladder->dest_y);
    assert(travel->player.plane == varrock_ladder->dest_plane);

    assert(has_object_at(11801, 3261, 3459, 0));
    assert(has_object_at(11802, 3261, 3459, 1));
    travel->player.x = 3261;
    travel->player.y = 3459;
    travel->player.plane = 0;
    travel->player.action_lock_timer = 0;
    travel->player.pending_traversal_active = 0;
    assert(rc_player_interact_object_at(travel, 11801, 3261, 3459, 0, 0)
           == 1);
    for (int i = 0; i < 6 && travel->player.plane != 1; i++)
        rc_world_tick(travel);
    assert(travel->player.x == 3261);
    assert(travel->player.y == 3459);
    assert(travel->player.plane == 1);
    travel->player.action_lock_timer = 0;
    travel->player.pending_traversal_active = 0;
    assert(rc_player_interact_object_at(travel, 11802, 3261, 3459, 1, 0)
           == 1);
    for (int i = 0; i < 6 && travel->player.plane != 0; i++)
        rc_world_tick(travel);
    assert(travel->player.x == 3261);
    assert(travel->player.y == 3459);
    assert(travel->player.plane == 0);

    assert(has_object_at(60731, 3217, 3398, 1));
    assert(has_object_at(60732, 3217, 3398, 3));
    travel->player.x = 3217;
    travel->player.y = 3398;
    travel->player.plane = 1;
    travel->player.action_lock_timer = 0;
    travel->player.pending_traversal_active = 0;
    assert(rc_player_interact_object_at(travel, 60731, 3217, 3398, 1, 0)
           == 1);
    for (int i = 0; i < 6 && travel->player.plane != 3; i++)
        rc_world_tick(travel);
    assert(travel->player.x == 3217);
    assert(travel->player.y == 3398);
    assert(travel->player.plane == 3);

    assert(has_object_at(11797, 3230, 3383, 0));
    travel->player.x = 3230;
    travel->player.y = 3386;
    travel->player.plane = 0;
    travel->player.action_lock_timer = 0;
    travel->player.pending_traversal_active = 0;
    assert(rc_player_interact_object_at(travel, 11797, 3230, 3383, 0, 0)
           == 1);
    for (int i = 0; i < 6 && travel->player.plane != 1; i++)
        rc_world_tick(travel);
    assert(travel->player.x == 3231);
    assert(travel->player.y == 3382);
    assert(travel->player.plane == 1);

    assert(has_object_at(56230, 3204, 3207, 0));
    travel->player.x = 3206;
    travel->player.y = 3208;
    travel->player.plane = 0;
    travel->player.action_lock_timer = 0;
    travel->player.pending_traversal_active = 0;
    assert(rc_player_interact_object_at(travel, 56230, 3204, 3207, 0, 0)
           == 1);
    for (int i = 0; i < 6 && travel->player.plane != 1; i++)
        rc_world_tick(travel);
    assert((travel->player.x == 3205 && travel->player.y == 3209)
           || (travel->player.x == 3206 && travel->player.y == 3208));
    assert(travel->player.plane == 1);

    assert(has_object_at(24427, 1758, 4959, 0));
    assert(!has_object_at(24427, 1758, 4959, 1));
    travel->player.x = 1759;
    travel->player.y = 4958;
    travel->player.plane = 0;
    travel->player.action_lock_timer = 0;
    travel->player.pending_traversal_active = 0;
    assert(rc_player_interact_object_at(travel, 24427, 1758, 4959, 0, 0)
           == 1);
    for (int i = 0; i < 4 && travel->player.x != 3258; i++)
        rc_world_tick(travel);
    assert(travel->player.x == 3258);
    assert(travel->player.y == 3452);
    assert(travel->player.plane == 0);

    const int cooking_stair = 2609;
    const int cooking_anchor_x = 3144;
    const int cooking_anchor_y = 3447;
    assert(has_object_at(cooking_stair, cooking_anchor_x, cooking_anchor_y, 1));
    travel->player.x = 3144;
    travel->player.y = 3449;
    travel->player.plane = 1;
    assert(rc_player_interact_object_at(travel, cooking_stair,
                                        cooking_anchor_x, cooking_anchor_y,
                                        1, 1) == 1);
    for (int i = 0; i < 6 && travel->player.plane != 2; i++)
        rc_world_tick(travel);
    assert(travel->player.x == 3144);
    assert(travel->player.y == 3446);
    assert(travel->player.plane == 2);
    travel->player.x = 3144;
    travel->player.y = 3449;
    travel->player.plane = 1;
    travel->player.action_lock_timer = 0;
    travel->player.pending_traversal_active = 0;
    assert(rc_player_interact_object_at(travel, cooking_stair,
                                        cooking_anchor_x, cooking_anchor_y,
                                        1, 2) == 1);
    for (int i = 0; i < 6 && travel->player.plane != 0; i++)
        rc_world_tick(travel);
    assert(travel->player.x == 3144);
    assert(travel->player.y == 3449);
    assert(travel->player.plane == 0);
    rc_world_destroy(travel);

    RcWorldConfig shortcut_cfg = rc_preset_base_only();
    shortcut_cfg.subsystems = RC_SUB_OBJECTS | RC_SUB_TRAVERSAL
                            | RC_SUB_SKILLS;
    shortcut_cfg.object_defs_path = ODEF_PATH;
    shortcut_cfg.object_placements_path = OPLC_PATH;
    shortcut_cfg.object_behaviors_path = OBHV_PATH;
    shortcut_cfg.object_transports_path = OTRP_PATH;
    shortcut_cfg.traversal_edges_path =
        RC_TEST_SOURCE_DIR "/data/defs/traversal_edges.bin";
    RcWorld *shortcut = rc_world_create_config(&shortcut_cfg);
    assert(shortcut != NULL);
    const int rail_obj = 2186;
    const int rail_x = 2515;
    const int rail_y = 3161;
    const RcTraversalEdge *rail_edge =
        rc_traversal_find(RC_TRAVERSAL_OBJECT, rail_obj, rail_x, rail_y,
                          0, 0);
    assert(rail_edge != NULL);
    assert(has_object_at(rail_obj, rail_x, rail_y, 0));
    place_player_adjacent(shortcut, rail_x, rail_y, 0);
    shortcut->player.skills.base_level[SKILL_AGILITY] = 0;
    shortcut->player.skills.boosted_level[SKILL_AGILITY] = 0;
    assert(rc_player_interact_object_at(shortcut, rail_obj, rail_x, rail_y,
                                        0, 0) == 0);
    assert(!shortcut->player.interaction.active);
    assert(shortcut->player.pending_traversal_active == 0);
    assert(shortcut->player.x == rail_x + 1);
    assert(shortcut->player.y == rail_y);
    shortcut->player.skills.base_level[SKILL_AGILITY] = 1;
    shortcut->player.skills.boosted_level[SKILL_AGILITY] = 1;
    int agility_xp = shortcut->player.skills.xp[SKILL_AGILITY];
    assert(rc_player_interact_object_at(shortcut, rail_obj, rail_x, rail_y,
                                        0, 0) == 1);
    rc_world_tick(shortcut);
    assert(shortcut->player.pending_traversal_active);
    assert(shortcut->player.action_lock_timer > 0);
    assert(shortcut->player.skills.xp[SKILL_AGILITY] > agility_xp);
    for (int i = 0; i < 4 && shortcut->player.pending_traversal_active; i++)
        rc_world_tick(shortcut);
    assert(shortcut->player.x == rail_edge->dest_x);
    assert(shortcut->player.y == rail_edge->dest_y);
    assert(shortcut->player.plane == rail_edge->dest_plane);
    rc_world_destroy(shortcut);

    RcWorldConfig base_cfg = rc_preset_base_only();
    RcWorld *base = rc_world_create_config(&base_cfg);
    assert(base != NULL);
    rc_player_interact_object(base, 1276, 0);
    assert(base->player.interact_type == 0);
    assert(rc_player_interact_object_at(base, 16683, 2465, 3495, 0, 0) == 0);
    rc_world_destroy(base);

    return 0;
}
