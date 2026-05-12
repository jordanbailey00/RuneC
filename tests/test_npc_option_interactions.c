#include "../rc-core/api.h"
#include "../rc-core/npc.h"

#include <assert.h>
#include <string.h>

int main(void) {
    RcWorldConfig cfg = rc_preset_base_only();
    cfg.subsystems = RC_SUB_COMBAT;
    cfg.seed = 12345;

    RcWorld *world = rc_world_create_config(&cfg);
    assert(world);

    int def_idx = g_npc_def_count++;
    assert(def_idx < RC_MAX_NPC_DEFS);
    memset(&g_npc_defs[def_idx], 0, sizeof(g_npc_defs[def_idx]));
    g_npc_defs[def_idx].id = 900002;
    strcpy(g_npc_defs[def_idx].name, "Option Dummy");
    g_npc_defs[def_idx].size = 1;
    g_npc_defs[def_idx].combat_level = 2;
    g_npc_defs[def_idx].hitpoints = 10;
    g_npc_defs[def_idx].stats[3] = 10;
    strcpy(g_npc_defs[def_idx].options[0], "Talk-to");
    strcpy(g_npc_defs[def_idx].options[1], "Attack");
    strcpy(g_npc_defs[def_idx].options[2], "Trade");

    int npc_idx = rc_npc_spawn(world, def_idx, world->player.x + 2,
                               world->player.y, world->player.plane);
    assert(npc_idx >= 0);
    int uid = world->npcs[npc_idx].uid;

    rc_player_interact_npc(world, uid, 0);
    assert(world->player.interaction.active);
    assert(world->player.route_len > 0);
    rc_world_tick(world);
    assert(world->player.interact_type == RC_INTERACT_NPC);
    assert(world->player.interact_target == uid);
    assert(world->player.interact_option == 0);
    assert(world->player.attack_target == -1);

    rc_player_interact_npc(world, uid, 1);
    rc_world_tick(world);
    assert(world->player.interact_type == RC_INTERACT_NPC_ATTACK);
    assert(world->player.interact_target == uid);
    assert(world->player.attack_target == uid);

    world->player.attack_target = -1;
    world->player.interact_type = RC_INTERACT_NONE;
    world->player.interact_target = -1;
    rc_player_interact_npc(world, uid, 4);
    assert(world->player.interact_type == RC_INTERACT_NONE);
    assert(world->player.attack_target == -1);

    rc_world_destroy(world);
    return 0;
}
