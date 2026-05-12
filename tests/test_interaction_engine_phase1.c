#include "../rc-core/api.h"
#include "../rc-core/npc.h"
#include "../rc-core/objects.h"

#include <assert.h>
#include <string.h>

static RcInteractionTarget npc_target(int uid) {
    RcInteractionTarget target = {0};
    target.kind = RC_INTERACTION_NPC;
    target.entity_uid = uid;
    target.definition_id = 900100;
    target.tile_x = 3200;
    target.tile_y = 3201;
    target.plane = 0;
    target.footprint_width = 2;
    target.footprint_height = 2;
    target.inventory_slot = -1;
    target.equipment_slot = -1;
    target.widget_id = -1;
    target.component_id = -1;
    target.ground_item_instance = -1;
    return target;
}

static RcInteractionTarget inv_target(int item_id, int slot) {
    RcInteractionTarget target = {0};
    target.kind = RC_INTERACTION_INVENTORY_ITEM;
    target.definition_id = item_id;
    target.tile_x = -1;
    target.tile_y = -1;
    target.plane = -1;
    target.footprint_width = 0;
    target.footprint_height = 0;
    target.inventory_slot = slot;
    target.equipment_slot = -1;
    target.widget_id = -1;
    target.component_id = -1;
    target.ground_item_instance = -1;
    return target;
}

static RcInteractionTarget object_target(int obj_id) {
    RcInteractionTarget target = {0};
    target.kind = RC_INTERACTION_OBJECT;
    target.entity_uid = -1;
    target.definition_id = obj_id;
    target.tile_x = 3202;
    target.tile_y = 3203;
    target.plane = 0;
    target.footprint_width = 1;
    target.footprint_height = 2;
    target.inventory_slot = -1;
    target.equipment_slot = -1;
    target.widget_id = -1;
    target.component_id = -1;
    target.ground_item_instance = -1;
    return target;
}

static RcInteractionTarget widget_target(int widget_id, int component_id) {
    RcInteractionTarget target = {0};
    target.kind = RC_INTERACTION_WIDGET;
    target.entity_uid = -1;
    target.definition_id = -1;
    target.tile_x = -1;
    target.tile_y = -1;
    target.plane = -1;
    target.inventory_slot = -1;
    target.equipment_slot = -1;
    target.widget_id = widget_id;
    target.component_id = component_id;
    target.ground_item_instance = -1;
    return target;
}

static void test_basic_lifecycle(void) {
    RcWorldConfig cfg = rc_preset_base_only();
    RcWorld *world = rc_world_create_config(&cfg);
    assert(world);
    RcPlayer *p = &world->player;

    assert(!rc_interaction_is_active(p));
    assert(rc_interaction_target_kind(p) == RC_INTERACTION_NONE);
    assert(rc_interaction_op_from_option(0) == RC_INTERACTION_OP1);
    assert(rc_interaction_op_from_option(4) == RC_INTERACTION_OP5);
    assert(rc_interaction_op_from_option(5) == RC_INTERACTION_OP_NONE);

    RcInteractionTarget npc = npc_target(44);
    assert(rc_interaction_target_valid(&npc));
    assert(rc_interaction_begin(p, 0, RC_INTERACTION_OP2, "Attack",
                                &npc, 1));

    const RcPendingInteraction *pending = rc_interaction_get(p);
    assert(pending && pending->active);
    assert(pending->source_actor_uid == 0);
    assert(pending->op == RC_INTERACTION_OP2);
    assert(strcmp(pending->option_text, "Attack") == 0);
    assert(pending->target.kind == RC_INTERACTION_NPC);
    assert(pending->target.entity_uid == 44);
    assert(pending->target.definition_id == 900100);
    assert(pending->target.footprint_width == 2);
    assert(pending->flags & RC_INTERACTION_STARTED);
    assert(pending->last_failure == RC_INTERACTION_FAIL_NONE);

    RcInteractionTarget widget = widget_target(548, 66);
    assert(rc_interaction_begin(p, 0, RC_INTERACTION_WIDGET_ACTION,
                                "Select", &widget, 0));
    pending = rc_interaction_get(p);
    assert(pending->target.kind == RC_INTERACTION_WIDGET);
    assert(pending->target.widget_id == 548);
    assert(pending->target.component_id == 66);
    assert(strcmp(pending->option_text, "Select") == 0);

    rc_interaction_cancel(p, RC_INTERACTION_FAIL_CANCELLED);
    pending = rc_interaction_get(p);
    assert(pending && !pending->active);
    assert(pending->flags & RC_INTERACTION_CANCELLED);
    assert(pending->last_failure == RC_INTERACTION_FAIL_CANCELLED);

    rc_interaction_clear(p);
    assert(!rc_interaction_is_active(p));
    assert(rc_interaction_target_kind(p) == RC_INTERACTION_NONE);

    rc_world_destroy(world);
}

static void test_structural_validation(void) {
    RcWorldConfig cfg = rc_preset_base_only();
    RcWorld *world = rc_world_create_config(&cfg);
    assert(world);

    RcInteractionTarget inv = inv_target(995, 0);
    assert(rc_interaction_target_valid(&inv));
    assert(rc_interaction_begin(&world->player, 0, RC_INTERACTION_OP1,
                                "Use", &inv, 0));

    RcInteractionTarget obj = object_target(40000);
    assert(rc_interaction_target_valid(&obj));
    assert(rc_interaction_begin(&world->player, 0, RC_INTERACTION_OP1,
                                "Open", &obj, 1));
    assert(world->player.interaction.target.kind == RC_INTERACTION_OBJECT);
    assert(world->player.interaction.target.footprint_height == 2);

    RcInteractionTarget bad_inv = inv_target(995, RC_INVENTORY_SIZE);
    assert(!rc_interaction_target_valid(&bad_inv));
    assert(!rc_interaction_begin(&world->player, 0, RC_INTERACTION_OP1,
                                 "Use", &bad_inv, 0));
    assert(!rc_interaction_is_active(&world->player));
    assert(world->player.interaction.last_failure
           == RC_INTERACTION_FAIL_INVALID_TARGET);

    RcInteractionTarget bad_npc = npc_target(-1);
    assert(!rc_interaction_target_valid(&bad_npc));
    assert(!rc_interaction_begin(&world->player, 0,
                                 RC_INTERACTION_OP_NONE, "Bad",
                                 &bad_npc, 1));

    rc_world_destroy(world);
}

static void test_existing_npc_api_populates_pending_state(void) {
    RcWorldConfig cfg = rc_preset_base_only();
    cfg.subsystems = RC_SUB_COMBAT;
    RcWorld *world = rc_world_create_config(&cfg);
    assert(world);

    int def_idx = g_npc_def_count++;
    assert(def_idx < RC_MAX_NPC_DEFS);
    memset(&g_npc_defs[def_idx], 0, sizeof(g_npc_defs[def_idx]));
    g_npc_defs[def_idx].id = 900101;
    strcpy(g_npc_defs[def_idx].name, "Interaction Dummy");
    g_npc_defs[def_idx].size = 1;
    g_npc_defs[def_idx].combat_level = 2;
    g_npc_defs[def_idx].hitpoints = 10;
    strcpy(g_npc_defs[def_idx].options[0], "Talk-to");
    strcpy(g_npc_defs[def_idx].options[1], "Attack");

    int npc_idx = rc_npc_spawn(world, def_idx, world->player.x + 1,
                               world->player.y, world->player.plane);
    assert(npc_idx >= 0);
    int uid = world->npcs[npc_idx].uid;

    rc_player_interact_npc(world, uid, 0);
    assert(rc_interaction_is_active(&world->player));
    assert(world->player.interaction.target.kind == RC_INTERACTION_NPC);
    assert(world->player.interaction.target.entity_uid == uid);
    assert(world->player.interaction.op == RC_INTERACTION_OP1);
    assert(strcmp(world->player.interaction.option_text, "Talk-to") == 0);
    assert(world->player.attack_target == -1);

    rc_player_interact_npc(world, uid, 1);
    assert(rc_interaction_is_active(&world->player));
    assert(world->player.interaction.op == RC_INTERACTION_OP2);
    assert(strcmp(world->player.interaction.option_text, "Attack") == 0);
    assert(world->player.attack_target == -1);

    rc_world_destroy(world);
}

static void test_existing_object_api_with_coords_populates_pending_state(void) {
    RcWorldConfig cfg = rc_preset_skilling_only();
    cfg.object_placements_path = NULL;
    RcWorld *world = rc_world_create_config(&cfg);
    assert(world);

    const int obj_id = 40001;
    memset(&g_rc_object_defs[obj_id], 0, sizeof(g_rc_object_defs[obj_id]));
    g_rc_object_defs[obj_id].id = obj_id;
    g_rc_object_defs[obj_id].width = 1;
    g_rc_object_defs[obj_id].length = 2;
    g_rc_object_defs[obj_id].loaded = 1;
    strcpy(g_rc_object_defs[obj_id].name, "Interaction Door");
    strcpy(g_rc_object_defs[obj_id].actions[0], "Open");

    memset(&g_rc_object_behaviors[obj_id], 0,
           sizeof(g_rc_object_behaviors[obj_id]));
    g_rc_object_behaviors[obj_id].action_mask = 1u << 0;
    g_rc_object_behaviors[obj_id].loaded = 1;

    assert(rc_player_interact_object_at(world, obj_id, 3205, 3206, 0, 0));
    assert(rc_interaction_is_active(&world->player));
    assert(world->player.interaction.target.kind == RC_INTERACTION_OBJECT);
    assert(world->player.interaction.target.definition_id == obj_id);
    assert(world->player.interaction.target.tile_x == 3205);
    assert(world->player.interaction.target.tile_y == 3206);
    assert(world->player.interaction.target.footprint_height == 2);
    assert(world->player.interaction.op == RC_INTERACTION_OP1);
    assert(strcmp(world->player.interaction.option_text, "Open") == 0);

    rc_world_destroy(world);
}

int main(void) {
    test_basic_lifecycle();
    test_structural_validation();
    test_existing_npc_api_populates_pending_state();
    test_existing_object_api_with_coords_populates_pending_state();
    return 0;
}
