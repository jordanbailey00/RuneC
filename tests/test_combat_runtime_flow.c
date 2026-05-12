#include "../rc-core/api.h"
#include "../rc-core/combat.h"
#include "../rc-core/npc.h"

#include <assert.h>
#include <string.h>

int main(void) {
    RcWorldConfig cfg = rc_preset_base_only();
    cfg.subsystems = RC_SUB_COMBAT;
    cfg.seed = 12345;

    RcWorld *world = rc_world_create_config(&cfg);
    assert(world);

    RcPlayer *p = &world->player;
    for (int i = 0; i < SKILL_COUNT; i++) {
        p->skills.base_level[i] = 99;
        p->skills.boosted_level[i] = 99;
    }
    p->equipment_bonuses[EQ_CRUSH_ATK] = 10000;
    p->equipment_bonuses[EQ_STR] = 100;
    rc_player_set_attack_style(world, 0);

    int def_idx = g_npc_def_count++;
    assert(def_idx < RC_MAX_NPC_DEFS);
    memset(&g_npc_defs[def_idx], 0, sizeof(g_npc_defs[def_idx]));
    g_npc_defs[def_idx].id = 900001;
    g_npc_defs[def_idx].size = 1;
    g_npc_defs[def_idx].combat_level = 2;
    g_npc_defs[def_idx].hitpoints = 5;
    g_npc_defs[def_idx].stats[1] = 1;
    g_npc_defs[def_idx].max_hit = 1;
    g_npc_defs[def_idx].attack_speed = 4;
    g_npc_defs[def_idx].attack_types = 0x04;
    g_npc_defs[def_idx].respawn_ticks = 8;
    strcpy(g_npc_defs[def_idx].options[1], "Attack");

    int npc_idx = rc_npc_spawn(world, def_idx, p->x + 3, p->y, p->plane);
    assert(npc_idx >= 0);
    int uid = world->npcs[npc_idx].uid;

    int xp_before = p->skills.xp[SKILL_ATTACK];
    rc_player_attack_npc(world, uid);
    for (int i = 0; i < 40 && !world->npcs[npc_idx].is_dead; i++) {
        rc_world_tick(world);
    }

    assert(world->npcs[npc_idx].is_dead);
    assert(world->npcs[npc_idx].death_timer > 0 ||
           world->npcs[npc_idx].respawn_timer > 0);
    assert(p->skills.xp[SKILL_ATTACK] > xp_before);
    assert(p->attack_target == -1);

    rc_world_destroy(world);
    return 0;
}
