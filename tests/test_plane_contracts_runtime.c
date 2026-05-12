#include <assert.h>
#include <string.h>

#include "api.h"
#include "config.h"
#include "items.h"
#include "npc.h"
#include "objects.h"
#include "player_actions.h"

static RcWorld *make_world(void) {
    RcWorldConfig cfg = rc_preset_base_only();
    cfg.subsystems = RC_SUB_INVENTORY | RC_SUB_LOOT | RC_SUB_OBJECTS;
    cfg.player_actions_path = NULL;
    RcWorld *world = rc_world_create_config(&cfg);
    assert(world != NULL);
    world->player.x = 3200;
    world->player.y = 3200;
    world->player.plane = 0;
    world->player.prev_x = world->player.x;
    world->player.prev_y = world->player.y;
    return world;
}

static void setup_npc_def(void) {
    memset(g_npc_defs, 0, sizeof(g_npc_defs));
    g_npc_def_count = 1;
    g_npc_defs[0].id = 19001;
    strcpy(g_npc_defs[0].name, "Plane guard");
    strcpy(g_npc_defs[0].options[0], "Attack");
    g_npc_defs[0].size = 1;
    g_npc_defs[0].hitpoints = 10;
    g_npc_defs[0].combat_level = 1;
    g_npc_defs[0].max_hit = 1;
    g_npc_defs[0].attack_speed = 4;
}

static void setup_object_def(int obj_id) {
    memset(g_rc_object_defs, 0, sizeof(g_rc_object_defs));
    memset(g_rc_object_behaviors, 0, sizeof(g_rc_object_behaviors));
    g_rc_object_def_count = 1;
    g_rc_object_behavior_count = 1;
    g_rc_object_defs[obj_id].id = obj_id;
    g_rc_object_defs[obj_id].width = 1;
    g_rc_object_defs[obj_id].length = 1;
    g_rc_object_defs[obj_id].loaded = 1;
    strcpy(g_rc_object_defs[obj_id].name, "Plane ladder");
    strcpy(g_rc_object_defs[obj_id].actions[0], "Climb-up");
    g_rc_object_behaviors[obj_id].action_mask = 1u;
    g_rc_object_behaviors[obj_id].loaded = 1;
}

static void test_npc_interaction_rejects_other_plane(void) {
    setup_npc_def();
    RcWorld *world = make_world();
    int idx = rc_npc_spawn(world, 0, world->player.x, world->player.y, 1);
    assert(idx >= 0);
    int uid = world->npcs[idx].uid;

    rc_player_attack_npc(world, uid);
    assert(!world->player.interaction.active);
    rc_player_interact_npc(world, uid, 0);
    assert(!world->player.interaction.active);
    assert(!world->player.combat.active);

    world->npcs[idx].plane = world->player.plane;
    rc_player_interact_npc(world, uid, 0);
    assert(world->player.interaction.active);
    assert(world->player.interaction.target.plane == world->player.plane);
    rc_world_destroy(world);
}

static void test_object_and_ground_item_reject_other_plane(void) {
    RcWorld *world = make_world();
    const int obj_id = 60000;
    setup_object_def(obj_id);
    assert(rc_object_def_get(obj_id) != NULL);
    assert(rc_object_behavior_get(obj_id) != NULL);
    assert(g_rc_player_action_count == 0 ||
           rc_player_action_allowed(world->enabled,
                                    RC_PLAYER_ACTION_INTERACT_OBJECT));

    assert(rc_player_interact_object_at(world, obj_id, world->player.x,
                                        world->player.y, 1, 0) == 0);
    assert(!world->player.interaction.active);
    world->player.inventory[0].item_id = 995;
    world->player.inventory[0].quantity = 1;
    world->ground_item_count = 1;
    world->ground_items[0] = (RcGroundItem){
        .uid = 77,
        .version = 1,
        .item_id = 995,
        .quantity = 1,
        .x = world->player.x,
        .y = world->player.y,
        .plane = 1,
        .visibility = RC_GROUND_VIS_PUBLIC,
        .owner_uid = RC_GROUND_OWNER_NONE,
        .active = true,
    };
    assert(rc_player_use_inventory_item_on_ground_item(world, 0, 0) == 0);
    rc_player_pickup_item(world, 0);
    assert(!world->player.interaction.active);
    rc_world_destroy(world);
}

int main(void) {
    test_npc_interaction_rejects_other_plane();
    test_object_and_ground_item_reject_other_plane();
    return 0;
}
