#include <assert.h>
#include <stdio.h>

#include "activity_mechanics.h"
#include "activity_schemas.h"
#include "activity_spawns.h"
#include "activity_states.h"
#include "area_flags.h"
#include "collision.h"
#include "config.h"
#include "dialogue.h"
#include "drops.h"
#include "items.h"
#include "monster_mechanics.h"
#include "normalization.h"
#include "npc.h"
#include "objects.h"
#include "player_actions.h"
#include "prayer.h"
#include "quests.h"
#include "shops.h"
#include "skills.h"
#include "slayer.h"
#include "spells.h"
#include "traversal.h"
#include "varbits.h"
#include "api.h"

static void reset_loader_counts(void) {
    g_npc_def_count = 0;
    g_item_def_count = 0;
    g_rc_item_normalization_count = 0;
    g_rc_npc_normalization_count = 0;
    g_rc_source_normalization_count = 0;
    g_rc_player_action_count = 0;
    g_rc_varbit_count = 0;
    g_rc_varp_count = 0;
    g_rc_drop_table_count = 0;
    g_rc_drop_entry_count = 0;
    g_rc_rdt_entry_count = 0;
    g_rc_gdt_entry_count = 0;
    g_rc_mrdt_entry_count = 0;
    g_rc_prayer_count = 0;
    g_rc_spell_count = 0;
    g_rc_recipe_count = 0;
    g_rc_skill_drop_source_count = 0;
    g_rc_skill_drop_count = 0;
    g_rc_gathering_node_count = 0;
    g_rc_object_def_count = 0;
    g_rc_object_behavior_count = 0;
    g_rc_object_placement_count = 0;
    g_rc_object_transport_count = 0;
    g_rc_collision_region_count = 0;
    g_rc_area_flag_count = 0;
    g_rc_traversal_edge_count = 0;
    g_rc_monster_mechanic_family_count = 0;
    g_rc_slayer_master_count = 0;
    g_shop_count = 0;
    g_shop_stock_count = 0;
    g_rc_quest_count = 0;
    g_rc_dialogue_transcript_count = 0;
    g_rc_dialogue_node_count = 0;
    g_rc_dialogue_child_id_count = 0;
    g_rc_activity_schema_count = 0;
    g_rc_activity_spawn_count = 0;
    g_rc_activity_mechanic_count = 0;
    g_rc_activity_state_count = 0;
}

static void assert_common_off(void) {
    assert(g_rc_prayer_count == 0);
    assert(g_rc_varbit_count == 0);
    assert(g_rc_varp_count == 0);
    assert(g_rc_drop_table_count == 0);
    assert(g_rc_rdt_entry_count == 0);
    assert(g_rc_gdt_entry_count == 0);
    assert(g_rc_mrdt_entry_count == 0);
    assert(g_rc_spell_count == 0);
    assert(g_rc_recipe_count == 0);
    assert(g_rc_skill_drop_source_count == 0);
    assert(g_rc_gathering_node_count == 0);
    assert(g_rc_object_def_count == 0);
    assert(g_rc_object_behavior_count == 0);
    assert(g_rc_object_placement_count == 0);
    assert(g_rc_object_transport_count == 0);
    assert(g_rc_collision_region_count == 0);
    assert(g_rc_area_flag_count == 0);
    assert(g_rc_traversal_edge_count == 0);
    assert(g_rc_monster_mechanic_family_count == 0);
    assert(g_rc_slayer_master_count == 0);
    assert(g_shop_count == 0);
    assert(g_shop_stock_count == 0);
    assert(g_rc_quest_count == 0);
    assert(g_rc_dialogue_transcript_count == 0);
    assert(g_rc_activity_schema_count == 0);
    assert(g_rc_activity_spawn_count == 0);
    assert(g_rc_activity_mechanic_count == 0);
    assert(g_rc_activity_state_count == 0);
}

static void check_base_only(void) {
    reset_loader_counts();
    RcWorldConfig cfg = rc_preset_base_only();
    RcWorld *w = rc_world_create_config(&cfg);
    assert(w != NULL);
    assert(w->enabled == 0);
    assert(w->npc_count == 0);
    assert(g_npc_def_count == 0);
    assert(g_item_def_count == 0);
    assert(g_rc_item_normalization_count == 0);
    assert(g_rc_player_action_count == 13);
    assert_common_off();
    rc_world_destroy(w);
}

static void check_combat_only(void) {
    reset_loader_counts();
    RcWorldConfig cfg = rc_preset_combat_only();
    RcWorld *w = rc_world_create_config(&cfg);
    assert(w != NULL);
    assert(w->enabled & RC_SUB_COMBAT);
    assert(w->enabled & RC_SUB_ENCOUNTER);
    assert(w->npc_count == 0);
    assert(g_npc_def_count > 10000);
    assert(g_item_def_count > 30000);
    assert(g_rc_item_normalization_count > 30000);
    assert(g_rc_player_action_count == 13);
    assert(g_rc_prayer_count == 31);
    assert(g_rc_varbit_count == 0);
    assert(g_rc_varp_count == 0);
    assert(g_rc_drop_table_count == 0);
    assert(g_rc_spell_count == 201);
    assert(g_rc_monster_mechanic_family_count == 16);
    assert(g_rc_activity_schema_count == 66);
    assert(g_rc_activity_spawn_count == 117);
    assert(g_rc_activity_mechanic_count == 96);
    assert(g_rc_activity_state_count == 16);
    assert(w->encounter.registry_count > 0);
    assert(g_rc_recipe_count == 0);
    assert(g_rc_object_def_count == 0);
    assert(g_rc_collision_region_count == 0);
    assert(g_rc_traversal_edge_count == 0);
    assert(g_rc_slayer_master_count == 0);
    assert(g_shop_count == 0);
    assert(g_rc_quest_count == 0);
    assert(g_rc_dialogue_transcript_count == 0);
    rc_world_destroy(w);
}

static void check_skilling_only(void) {
    reset_loader_counts();
    RcWorldConfig cfg = rc_preset_skilling_only();
    RcWorld *w = rc_world_create_config(&cfg);
    assert(w != NULL);
    assert(w->enabled & RC_SUB_SKILLS);
    assert(w->enabled & RC_SUB_OBJECTS);
    assert(w->enabled & RC_SUB_REGIONS);
    assert(w->enabled & RC_SUB_TRAVERSAL);
    assert(w->npc_count == 0);
    assert(g_npc_def_count == 0);
    assert(g_item_def_count > 30000);
    assert(g_rc_item_normalization_count > 30000);
    assert(g_rc_player_action_count == 13);
    assert(g_rc_varbit_count == 18571);
    assert(g_rc_varp_count == 5546);
    assert(g_rc_recipe_count > 3000);
    assert(g_rc_skill_drop_source_count > 900);
    assert(g_rc_gathering_node_count > 30000);
    assert(g_rc_object_def_count > 60000);
    assert(g_rc_object_behavior_count > 8000);
    assert(g_rc_object_placement_count > 4700000);
    assert(g_rc_object_transport_count > 29000);
    assert(g_rc_collision_region_count > 2000);
    assert(g_rc_area_flag_count == 1419);
    assert(g_rc_traversal_edge_count > 45000);
    assert(g_rc_prayer_count == 0);
    assert(g_rc_spell_count == 0);
    assert(g_rc_drop_table_count == 0);
    assert(g_rc_quest_count == 0);
    assert(g_rc_dialogue_transcript_count == 0);
    assert(g_rc_activity_schema_count == 0);
    assert(g_rc_monster_mechanic_family_count == 0);
    rc_world_destroy(w);
}

static void check_full_game(void) {
    reset_loader_counts();
    RcWorldConfig cfg = rc_preset_full_game();
    RcWorld *w = rc_world_create_config(&cfg);
    assert(w != NULL);
    assert(w->enabled & RC_SUB_TRAVERSAL);
    assert(w->npc_count == 0);
    assert(g_npc_def_count > 10000);
    assert(g_item_def_count > 30000);
    assert(g_rc_item_normalization_count > 30000);
    assert(g_rc_varbit_count == 18571);
    assert(g_rc_varp_count == 5546);
    assert(g_rc_drop_table_count == 1052);
    assert(g_rc_rdt_entry_count == 19);
    assert(g_rc_gdt_entry_count == 11);
    assert(g_rc_mrdt_entry_count == 4);
    assert(g_rc_prayer_count == 31);
    assert(g_rc_spell_count == 201);
    assert(g_rc_recipe_count > 3000);
    assert(g_rc_skill_drop_source_count > 900);
    assert(g_rc_object_def_count > 60000);
    assert(g_rc_collision_region_count > 2000);
    assert(g_rc_area_flag_count == 1419);
    assert(g_rc_traversal_edge_count > 45000);
    assert(g_rc_monster_mechanic_family_count == 16);
    assert(g_rc_slayer_master_count == 12);
    assert(g_shop_count == 601);
    assert(g_rc_quest_count == 215);
    assert(g_rc_dialogue_transcript_count == 380);
    assert(g_rc_dialogue_node_count > 150000);
    assert(g_rc_activity_schema_count == 66);
    assert(w->encounter.registry_count > 0);
    rc_world_destroy(w);
}

int main(void) {
    check_base_only();
    check_combat_only();
    check_skilling_only();
    check_full_game();
    printf("test_modular_loading: preset dataset ownership checks passed.\n");
    return 0;
}
