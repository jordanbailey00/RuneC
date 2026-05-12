#include "../rc-core/api.h"
#include "../rc-core/items.h"
#include "../rc-core/npc.h"
#include "../rc-core/objects.h"

#include <assert.h>
#include <string.h>

typedef struct {
    int calls;
    RcInteractionKind seen_kind;
    RcInteractionOp seen_op;
    int seen_def;
    int seen_source_item;
    int seen_source_spell;
    int seen_widget;
    int seen_component;
    int seen_inventory_slot;
    int seen_equipment_slot;
    int system_handoff;
    int system_target;
} Phase8Ctx;

static RcInteractionHandlerResult phase8_complete_handler(
    RcWorld *world, RcPlayer *player, const RcPendingInteraction *pending,
    void *ctx) {
    (void)world;
    (void)player;
    Phase8Ctx *state = ctx;
    state->calls++;
    state->seen_kind = pending->target.kind;
    state->seen_op = pending->op;
    state->seen_def = pending->target.definition_id;
    state->seen_source_item = pending->source_item_id;
    state->seen_source_spell = pending->source_spell_id;
    state->seen_widget = pending->target.widget_id;
    state->seen_component = pending->target.component_id;
    state->seen_inventory_slot = pending->target.inventory_slot;
    state->seen_equipment_slot = pending->target.equipment_slot;
    return rc_interaction_result_complete();
}

static RcInteractionHandlerResult phase8_system_handler(
    RcWorld *world, RcPlayer *player, const RcPendingInteraction *pending,
    void *ctx) {
    (void)world;
    (void)player;
    Phase8Ctx *state = ctx;
    RcInteractionHandlerResult result =
        rc_interaction_result_system_handoff(
            RC_INTERACTION_SYSTEM_BANK, pending->target.definition_id);
    state->calls++;
    state->system_handoff = result.system_handoff;
    state->system_target = result.system_target_id;
    return result;
}

static int spawn_phase8_npc(RcWorld *world, int cache_id, int dx) {
    int def_idx = g_npc_def_count++;
    assert(def_idx < RC_MAX_NPC_DEFS);
    memset(&g_npc_defs[def_idx], 0, sizeof(g_npc_defs[def_idx]));
    g_npc_defs[def_idx].id = cache_id;
    strcpy(g_npc_defs[def_idx].name, "Phase 8 NPC");
    g_npc_defs[def_idx].size = 1;
    g_npc_defs[def_idx].combat_level = 2;
    g_npc_defs[def_idx].hitpoints = 10;
    strcpy(g_npc_defs[def_idx].options[0], "Talk-to");
    int npc_idx = rc_npc_spawn(world, def_idx, world->player.x + dx,
                               world->player.y, world->player.plane);
    assert(npc_idx >= 0);
    world->npcs[npc_idx].wander_timer = 999999;
    return npc_idx;
}

static void install_phase8_object(int obj_id) {
    memset(&g_rc_object_defs[obj_id], 0, sizeof(g_rc_object_defs[obj_id]));
    g_rc_object_defs[obj_id].id = obj_id;
    strcpy(g_rc_object_defs[obj_id].name, "Phase 8 Object");
    g_rc_object_defs[obj_id].width = 1;
    g_rc_object_defs[obj_id].length = 1;
    g_rc_object_defs[obj_id].loaded = 1;
}

static void tick_until_inactive(RcWorld *world, int max_ticks) {
    for (int i = 0; i < max_ticks && rc_interaction_is_active(&world->player);
            i++) {
        rc_world_tick(world);
    }
}

static RcInteractionDispatchKey key_for(RcInteractionKind kind,
                                        RcInteractionOp op) {
    RcInteractionDispatchKey key = rc_interaction_dispatch_key_any();
    key.kind = kind;
    key.op = op;
    return key;
}

static void test_inventory_equipment_and_widget_hooks(void) {
    rc_interaction_clear_handlers();
    RcWorldConfig cfg = rc_preset_base_only();
    RcWorld *world = rc_world_create_config(&cfg);
    assert(world);
    Phase8Ctx inv = {0};
    Phase8Ctx item_on_inv = {0};
    Phase8Ctx spell_on_inv = {0};
    Phase8Ctx equip = {0};
    Phase8Ctx widget = {0};
    Phase8Ctx item_on_widget = {0};
    Phase8Ctx spell_on_widget = {0};

    world->player.inventory[4].item_id = 995;
    world->player.inventory[4].quantity = 10;
    world->player.inventory[5].item_id = 556;
    world->player.inventory[5].quantity = 1;
    world->player.equipment[EQUIP_WEAPON].item_id = 4151;
    world->player.equipment[EQUIP_WEAPON].quantity = 1;

    RcInteractionDispatchKey key =
        key_for(RC_INTERACTION_INVENTORY_ITEM, RC_INTERACTION_OP2);
    key.definition_id = 995;
    key.source_item_id = 995;
    assert(rc_interaction_register_handler(&key, phase8_complete_handler,
                                           &inv));

    key = key_for(RC_INTERACTION_INVENTORY_ITEM, RC_INTERACTION_USE_ON);
    key.definition_id = 995;
    key.source_item_id = 556;
    assert(rc_interaction_register_handler(&key, phase8_complete_handler,
                                           &item_on_inv));

    key = key_for(RC_INTERACTION_INVENTORY_ITEM, RC_INTERACTION_SPELL_ON);
    key.definition_id = 995;
    key.source_spell_id = 321;
    assert(rc_interaction_register_handler(&key, phase8_complete_handler,
                                           &spell_on_inv));

    key = key_for(RC_INTERACTION_EQUIPMENT_ITEM, RC_INTERACTION_OP1);
    key.definition_id = 4151;
    key.source_item_id = 4151;
    assert(rc_interaction_register_handler(&key, phase8_complete_handler,
                                           &equip));

    key = key_for(RC_INTERACTION_WIDGET, RC_INTERACTION_WIDGET_ACTION);
    key.definition_id = 3;
    key.widget_id = 548;
    key.component_id = 44;
    assert(rc_interaction_register_handler(&key, phase8_complete_handler,
                                           &widget));

    key = key_for(RC_INTERACTION_WIDGET, RC_INTERACTION_USE_ON);
    key.widget_id = 548;
    key.component_id = 45;
    key.source_item_id = 556;
    assert(rc_interaction_register_handler(&key, phase8_complete_handler,
                                           &item_on_widget));

    key = key_for(RC_INTERACTION_WIDGET, RC_INTERACTION_SPELL_ON);
    key.widget_id = 548;
    key.component_id = 46;
    key.source_spell_id = 321;
    assert(rc_interaction_register_handler(&key, phase8_complete_handler,
                                           &spell_on_widget));

    assert(rc_player_interact_inventory_item(world, 4, 1));
    rc_world_tick(world);
    assert(inv.calls == 1);
    assert(inv.seen_kind == RC_INTERACTION_INVENTORY_ITEM);
    assert(inv.seen_op == RC_INTERACTION_OP2);
    assert(inv.seen_source_item == 995);
    assert(inv.seen_inventory_slot == 4);

    assert(rc_player_use_inventory_item_on_inventory_item(world, 5, 4));
    rc_world_tick(world);
    assert(item_on_inv.calls == 1);
    assert(item_on_inv.seen_kind == RC_INTERACTION_INVENTORY_ITEM);
    assert(item_on_inv.seen_op == RC_INTERACTION_USE_ON);
    assert(item_on_inv.seen_source_item == 556);
    assert(item_on_inv.seen_inventory_slot == 4);

    assert(rc_player_cast_spell_on_inventory_item(world, 321, 4));
    rc_world_tick(world);
    assert(spell_on_inv.calls == 1);
    assert(spell_on_inv.seen_kind == RC_INTERACTION_INVENTORY_ITEM);
    assert(spell_on_inv.seen_op == RC_INTERACTION_SPELL_ON);
    assert(spell_on_inv.seen_source_spell == 321);
    assert(spell_on_inv.seen_inventory_slot == 4);

    assert(rc_player_interact_equipment_item(world, EQUIP_WEAPON, 0));
    rc_world_tick(world);
    assert(equip.calls == 1);
    assert(equip.seen_kind == RC_INTERACTION_EQUIPMENT_ITEM);
    assert(equip.seen_source_item == 4151);
    assert(equip.seen_equipment_slot == EQUIP_WEAPON);

    assert(rc_player_widget_action(world, 548, 44, 3));
    rc_world_tick(world);
    assert(widget.calls == 1);
    assert(widget.seen_kind == RC_INTERACTION_WIDGET);
    assert(widget.seen_widget == 548);
    assert(widget.seen_component == 44);
    assert(widget.seen_def == 3);

    assert(rc_player_use_inventory_item_on_widget(world, 5, 548, 45));
    rc_world_tick(world);
    assert(item_on_widget.calls == 1);
    assert(item_on_widget.seen_kind == RC_INTERACTION_WIDGET);
    assert(item_on_widget.seen_op == RC_INTERACTION_USE_ON);
    assert(item_on_widget.seen_source_item == 556);
    assert(item_on_widget.seen_widget == 548);
    assert(item_on_widget.seen_component == 45);

    assert(rc_player_cast_spell_on_widget(world, 321, 548, 46));
    rc_world_tick(world);
    assert(spell_on_widget.calls == 1);
    assert(spell_on_widget.seen_kind == RC_INTERACTION_WIDGET);
    assert(spell_on_widget.seen_op == RC_INTERACTION_SPELL_ON);
    assert(spell_on_widget.seen_source_spell == 321);
    assert(spell_on_widget.seen_widget == 548);
    assert(spell_on_widget.seen_component == 46);

    rc_world_destroy(world);
}

static void test_item_on_npc_and_spell_on_object_hooks(void) {
    rc_interaction_clear_handlers();
    RcWorldConfig cfg = rc_preset_base_only();
    RcWorld *world = rc_world_create_config(&cfg);
    assert(world);
    Phase8Ctx item = {0};
    Phase8Ctx spell = {0};

    int npc_idx = spawn_phase8_npc(world, 901800, 1);
    int uid = world->npcs[npc_idx].uid;
    world->player.inventory[2].item_id = 995;
    world->player.inventory[2].quantity = 1;

    RcInteractionDispatchKey key =
        key_for(RC_INTERACTION_NPC, RC_INTERACTION_USE_ON);
    key.definition_id = 901800;
    key.source_item_id = 995;
    assert(rc_interaction_register_handler(&key, phase8_complete_handler,
                                           &item));

    assert(rc_player_use_inventory_item_on_npc(world, 2, uid));
    rc_world_tick(world);
    assert(item.calls == 1);
    assert(item.seen_kind == RC_INTERACTION_NPC);
    assert(item.seen_op == RC_INTERACTION_USE_ON);
    assert(item.seen_source_item == 995);
    assert(item.seen_def == 901800);

    int obj_id = 41800;
    install_phase8_object(obj_id);
    key = key_for(RC_INTERACTION_OBJECT, RC_INTERACTION_SPELL_ON);
    key.definition_id = obj_id;
    key.source_spell_id = 321;
    assert(rc_interaction_register_handler(&key, phase8_complete_handler,
                                           &spell));

    assert(rc_player_cast_spell_on_object(world, 321, obj_id,
                                          world->player.x + 1,
                                          world->player.y,
                                          world->player.plane));
    rc_world_tick(world);
    assert(spell.calls == 1);
    assert(spell.seen_kind == RC_INTERACTION_OBJECT);
    assert(spell.seen_op == RC_INTERACTION_SPELL_ON);
    assert(spell.seen_source_spell == 321);
    assert(spell.seen_def == obj_id);

    rc_world_destroy(world);
}

static void test_system_handoff_result_and_no_handler_fallback(void) {
    rc_interaction_clear_handlers();
    RcWorldConfig cfg = rc_preset_base_only();
    RcWorld *world = rc_world_create_config(&cfg);
    assert(world);
    Phase8Ctx bank = {0};

    int obj_id = 41801;
    install_phase8_object(obj_id);
    RcInteractionDispatchKey key =
        key_for(RC_INTERACTION_OBJECT, RC_INTERACTION_SPELL_ON);
    key.definition_id = obj_id;
    key.source_spell_id = 900;
    assert(rc_interaction_register_handler(&key, phase8_system_handler,
                                           &bank));
    assert(rc_player_cast_spell_on_object(world, 900, obj_id,
                                          world->player.x + 1,
                                          world->player.y,
                                          world->player.plane));
    rc_world_tick(world);
    assert(bank.calls == 1);
    assert(bank.system_handoff == RC_INTERACTION_SYSTEM_BANK);
    assert(bank.system_target == obj_id);
    assert(!rc_interaction_is_active(&world->player));
    assert(world->player.interaction.flags & RC_INTERACTION_COMPLETED);

    assert(rc_player_widget_action(world, 548, 77, 1));
    rc_world_tick(world);
    assert(!rc_interaction_is_active(&world->player));
    assert(world->player.interaction.last_failure
           == RC_INTERACTION_FAIL_NO_HANDLER);

    rc_world_destroy(world);
}

int main(void) {
    test_inventory_equipment_and_widget_hooks();
    test_item_on_npc_and_spell_on_object_hooks();
    test_system_handoff_result_and_no_handler_fallback();
    rc_interaction_clear_handlers();
    return 0;
}
