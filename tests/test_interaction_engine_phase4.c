#include "../rc-core/api.h"
#include "../rc-core/npc.h"

#include <assert.h>
#include <string.h>

static int spawn_phase4_npc(RcWorld *world, int cache_id, int dx) {
    int def_idx = g_npc_def_count++;
    assert(def_idx < RC_MAX_NPC_DEFS);
    memset(&g_npc_defs[def_idx], 0, sizeof(g_npc_defs[def_idx]));
    g_npc_defs[def_idx].id = cache_id;
    strcpy(g_npc_defs[def_idx].name, "Phase 4 Dummy");
    g_npc_defs[def_idx].size = 1;
    g_npc_defs[def_idx].combat_level = 2;
    g_npc_defs[def_idx].hitpoints = 10;
    g_npc_defs[def_idx].wander_range = 0;
    strcpy(g_npc_defs[def_idx].options[0], "Talk-to");
    strcpy(g_npc_defs[def_idx].options[1], "Attack");
    int npc_idx = rc_npc_spawn(world, def_idx, world->player.x + dx,
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
    RcWorldConfig cfg = rc_preset_base_only();
    cfg.subsystems = RC_SUB_COMBAT;
    RcWorld *world = rc_world_create_config(&cfg);
    assert(world);

    int npc_idx = spawn_phase4_npc(world, 901400, 5);
    int uid = world->npcs[npc_idx].uid;
    int start_x = world->player.x;

    rc_player_interact_npc(world, uid, 0);
    assert(rc_interaction_is_active(&world->player));
    assert(world->player.interact_type == RC_INTERACT_NONE);

    rc_world_tick(world);
    assert(world->player.x != start_x);
    assert(rc_interaction_is_active(&world->player));

    tick_until_inactive(world, 16);
    assert(!rc_interaction_is_active(&world->player));
    assert(world->player.interact_type == RC_INTERACT_NPC);
    assert(world->player.interact_target == uid);
    assert(world->player.interact_option == 0);
    assert(world->player.facing_entity == uid);
    assert(world->player.facing_x == world->npcs[npc_idx].x);
    assert(world->player.facing_y == world->npcs[npc_idx].y);
    assert(world->player.interaction.flags & RC_INTERACTION_COMPLETED);

    rc_world_destroy(world);
}

static void test_attack_range_dispatch_and_stale_target_failure(void) {
    RcWorldConfig cfg = rc_preset_base_only();
    cfg.subsystems = RC_SUB_COMBAT;
    RcWorld *world = rc_world_create_config(&cfg);
    assert(world);

    int npc_idx = spawn_phase4_npc(world, 901401, 1);
    int uid = world->npcs[npc_idx].uid;
    rc_player_interact_npc(world, uid, 1);
    assert(rc_interaction_is_active(&world->player));
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
