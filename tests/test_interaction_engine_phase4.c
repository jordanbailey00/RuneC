#include "../rc-core/api.h"
#include "../rc-core/npc.h"
#include "runtime_test_fixture.h"

#include <assert.h>
#include <string.h>

static RcWorld *phase4_world(int cache_id) {
    g_npc_def_count = 1;
    memset(&g_npc_defs[0], 0, sizeof(g_npc_defs[0]));
    g_npc_defs[0].id = cache_id;
    strcpy(g_npc_defs[0].name, "Phase 4 Dummy");
    g_npc_defs[0].size = 1;
    g_npc_defs[0].combat_level = 2;
    g_npc_defs[0].hitpoints = 10;
    strcpy(g_npc_defs[0].options[0], "Talk-to");
    strcpy(g_npc_defs[0].options[1], "Attack");
    RcWorldConfig cfg = rc_preset_base_only();
    cfg.subsystems = RC_SUB_COMBAT;
    RcWorld *world = rc_test_world_create_with_defs(&cfg, "interaction4", 0);
    assert(world);
    rc_test_open_mapsquare(world, world->player.x, world->player.y,
                           world->player.plane);
    return world;
}

static int spawn_phase4_npc(RcWorld *world, int dx) {
    int npc_idx = rc_npc_spawn(world, 0, world->player.x + dx,
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

static void test_route_face_and_dispatch_noncombat_npc(void) {
    RcWorld *world = phase4_world(901400);
    int npc_idx = spawn_phase4_npc(world, 5);
    int uid = world->npcs[npc_idx].uid;
    int start_x = world->player.x;

    rc_player_interact_npc(world, uid, 0);
    assert(!rc_interaction_is_active(&world->player));
    assert(rc_player_pending_command_count(world) == 1);
    assert(world->player.interact_type == RC_INTERACT_NONE);

    rc_world_tick(world);
    assert(world->player.x != start_x);
    assert(rc_interaction_is_active(&world->player));
    // The player faces the NPC from the click, while walking to it.
    assert(world->player.facing_entity == uid);

    tick_until_inactive(world, 16);
    assert(!rc_interaction_is_active(&world->player));
    assert(world->player.interact_type == RC_INTERACT_NPC);
    assert(world->player.interact_target == uid);
    assert(world->player.interact_option == 0);
    assert(world->player.facing_entity == uid);
    assert(world->player.interaction.flags & RC_INTERACTION_COMPLETED);
    // With the interaction over, the face entity resets on the next tick.
    rc_world_tick(world);
    assert(world->player.facing_entity == -1);

    rc_world_destroy(world);
}

static void test_attack_range_dispatch_and_stale_target_failure(void) {
    RcWorld *world = phase4_world(901401);
    int npc_idx = spawn_phase4_npc(world, 1);
    int uid = world->npcs[npc_idx].uid;
    rc_player_interact_npc(world, uid, 1);
    assert(!rc_interaction_is_active(&world->player));
    rc_world_tick(world);
    assert(!rc_interaction_is_active(&world->player));
    assert(world->player.attack_target == uid);
    assert(world->player.facing_entity == uid);

    rc_interaction_clear(&world->player);
    world->player.attack_target = -1;
    world->npcs[npc_idx].active = false;
    RcInteractionTarget target = {0};
    target.kind = RC_INTERACTION_NPC;
    target.entity_uid = uid;
    target.definition_id = 901401;
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
    rc_world_tick(world);
    assert(!rc_interaction_is_active(&world->player));
    assert(world->player.interaction.last_failure
           == RC_INTERACTION_FAIL_TARGET_MISSING);

    rc_world_destroy(world);
}

int main(void) {
    test_route_face_and_dispatch_noncombat_npc();
    test_attack_range_dispatch_and_stale_target_failure();
    return 0;
}
