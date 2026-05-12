#include "../rc-core/api.h"
#include "../rc-core/combat.h"
#include "../rc-core/combat_hit.h"
#include "../rc-core/npc.h"

#include <assert.h>
#include <string.h>

static int add_phase8_npc_def(void) {
    int def_idx = g_npc_def_count++;
    assert(def_idx < RC_MAX_NPC_DEFS);
    memset(&g_npc_defs[def_idx], 0, sizeof(g_npc_defs[def_idx]));
    g_npc_defs[def_idx].id = 9801;
    strcpy(g_npc_defs[def_idx].name, "Phase 8 View Target");
    g_npc_defs[def_idx].size = 1;
    g_npc_defs[def_idx].combat_level = 10;
    g_npc_defs[def_idx].hitpoints = 30;
    g_npc_defs[def_idx].stats[0] = 99;
    g_npc_defs[def_idx].stats[1] = 1;
    g_npc_defs[def_idx].stats[2] = 99;
    g_npc_defs[def_idx].stats[3] = 30;
    g_npc_defs[def_idx].max_hit = 3;
    g_npc_defs[def_idx].attack_speed = 4;
    g_npc_defs[def_idx].attack_types = 0x04;
    strcpy(g_npc_defs[def_idx].options[1], "Attack");
    return def_idx;
}

static RcWorld *phase8_world(void) {
    RcWorldConfig cfg = rc_preset_base_only();
    cfg.subsystems = RC_SUB_COMBAT;
    cfg.seed = 888;
    RcWorld *world = rc_world_create_config(&cfg);
    assert(world);
    world->player.x = 3200;
    world->player.y = 3200;
    world->player.current_hp = 990;
    world->player.max_hp = 990;
    for (int i = 0; i < SKILL_COUNT; i++) {
        world->player.skills.base_level[i] = 99;
        world->player.skills.boosted_level[i] = 99;
    }
    rc_combat_set_player_style(world, 2);
    return world;
}

static void test_player_view_exposes_ui_combat_state_and_target_hits(void) {
    RcWorld *world = phase8_world();
    int def_idx = add_phase8_npc_def();
    int npc_idx = rc_npc_spawn(world, def_idx, 3201, 3200, 0);
    assert(npc_idx >= 0);
    RcNpc *npc = &world->npcs[npc_idx];

    RcCombatViewState view;
    assert(rc_combat_get_player_view(world, &view));
    assert(view.selected_style_idx == 2);
    assert(view.auto_retaliate == 1);
    assert(view.special_pending == 0);
    assert(view.special_energy == world->player.special_energy);
    assert(view.target.kind == RC_COMBAT_ACTOR_NONE);

    rc_combat_toggle_special(world);
    assert(rc_combat_start_player_vs_npc(world, 0, npc->uid));
    world->player.attack_timer = 99;
    npc->attack_timer = 99;
    assert(rc_combat_get_player_view(world, &view));
    assert(view.special_pending == 1);
    assert(view.target.kind == RC_COMBAT_ACTOR_NPC);
    assert(view.target.uid == npc->uid);
    assert(view.target_hp_current == 30);
    assert(view.target_hp_max == 30);

    rc_queue_hit_meta(npc->pending_hits, &npc->num_pending_hits,
                      5, 0, COMBAT_MELEE_CRUSH, -1, 0u,
                      world->tick, 0, 10, 0);
    rc_world_tick(world);
    assert(rc_combat_get_player_view(world, &view));
    assert(view.target_recent_hit_count == 1);
    assert(view.target_recent_hits[0].damage == 5);
    assert(view.target_recent_hits[0].max_hit == 10);
    assert(view.target_hp_current == 25);
    assert(view.target_hp_max == 30);

    rc_queue_hit_meta(world->player.pending_hits,
                      &world->player.num_pending_hits,
                      2, 0, COMBAT_MELEE_CRUSH, npc->uid, 0u,
                      world->tick, 0, 3, 0);
    rc_world_tick(world);
    assert(rc_combat_get_player_view(world, &view));
    assert(view.player_recent_hit_count >= 1);
    assert(view.player_recent_hits[
           view.player_recent_hit_count - 1].damage == 2);
    assert(view.player_hp_current == 970);
    assert(view.player_hp_max == 990);

    rc_world_destroy(world);
}

static void test_player_view_tracks_auto_retaliate_toggle(void) {
    RcWorld *world = phase8_world();
    RcCombatViewState view;
    assert(rc_combat_get_player_view(world, &view));
    assert(view.auto_retaliate == 1);
    rc_combat_toggle_auto_retaliate(world);
    assert(rc_combat_get_player_view(world, &view));
    assert(view.auto_retaliate == 0);
    rc_world_destroy(world);
}

int main(void) {
    test_player_view_exposes_ui_combat_state_and_target_hits();
    test_player_view_tracks_auto_retaliate_toggle();
    return 0;
}
