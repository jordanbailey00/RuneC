#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "api.h"

int main(void) {
    RcWorldConfig base_cfg = rc_preset_base_only();
    RcGameDataLoadReport base_report;
    RcGameData *base_data = rc_game_data_load(&base_cfg, &base_report);
    assert(base_data != NULL);
    assert(base_report.ok == 1);
    assert(rc_game_data_subsystems(base_data) == 0);

    RcWorldConfig full_cfg = rc_preset_full_game();
    RcWorld *too_wide = rc_world_create_with_data(base_data, &full_cfg);
    assert(too_wide == NULL);
    rc_game_data_release(base_data);

    RcGameDataLoadReport full_report;
    RcGameData *full_data = rc_game_data_load(&full_cfg, &full_report);
    assert(full_data != NULL);
    assert(full_report.ok == 1);

    const RcGameDataStats *stats = rc_game_data_stats(full_data);
    assert(stats != NULL);
    assert(stats->npc_def_count > 10000);
    assert(stats->item_def_count > 30000);
    assert(stats->item_normalization_count > 30000);
    assert(stats->npc_normalization_count > 10000);
    assert(stats->source_normalization_count > 5000);
    assert(stats->drop_table_count > 1000);
    assert(stats->prayer_count == 31);
    assert(stats->spell_count == 201);
    assert(stats->object_def_count > 60000);
    assert(stats->object_placement_count > 4700000);
    assert(stats->collision_region_count > 2000);
    assert(stats->area_flag_count == 1419);
    assert(stats->traversal_edge_count > 45000);
    assert(stats->activity_schema_count == 66);
    assert(stats->activity_spawn_count == 117);
    assert(stats->activity_mechanic_count == 96);
    assert(stats->activity_state_count == 16);
    assert(stats->encounter_spec_count > 0);
    assert(stats->varbit_count == 18571);
    assert(stats->varp_count == 5546);
    assert(stats->recipe_count > 3000);
    assert(stats->skill_drop_source_count > 900);
    assert(stats->skill_drop_count > 1000);
    assert(stats->gathering_node_count > 30000);
    assert(stats->shop_count == 601);
    assert(stats->shop_stock_count == 6209);
    assert(stats->quest_count == 215);
    assert(stats->dialogue_transcript_count == 380);
    assert(stats->dialogue_node_count > 150000);

    int npc_def_count = 0;
    const RcNpcDef *npc_defs = rc_game_data_npc_defs(full_data,
                                                    &npc_def_count);
    assert(npc_defs != NULL);
    assert(npc_def_count == stats->npc_def_count);
    int active_npc_def_count = 0;
    assert(rc_npc_defs_all(&active_npc_def_count) == npc_defs);
    assert(active_npc_def_count == npc_def_count);
    int jad_def = rc_npc_def_find(3127);
    assert(jad_def >= 0);
    assert(rc_npc_def_get(jad_def) == &npc_defs[jad_def]);
    assert(strcmp(npc_defs[jad_def].name, "TzTok-Jad") == 0);

    int item_def_count = 0;
    const RcItemDef *item_defs = rc_game_data_item_defs(full_data,
                                                       &item_def_count);
    assert(item_defs != NULL);
    assert(item_def_count == stats->item_def_count);
    const RcItemDef *whip = rc_item_def_get(4151);
    assert(whip == &item_defs[4151]);
    assert(strcmp(whip->name, "Abyssal whip") == 0);

    int item_norm_count = 0;
    const RcItemNormalization *item_norm =
        rc_game_data_item_normalization(full_data, &item_norm_count);
    assert(item_norm != NULL);
    assert(item_norm_count == stats->item_normalization_count);
    assert(rc_normalize_item_id(4152) == 4151);
    assert(rc_item_noted_id(4151) == 4152);
    assert(item_norm[4151].key_hash
           == rc_normalization_hash_key("Abyssal whip"));

    int npc_norm_count = 0;
    const RcNpcNormalization *npc_norm =
        rc_game_data_npc_normalization(full_data, &npc_norm_count);
    assert(npc_norm != NULL);
    assert(npc_norm_count == stats->npc_normalization_count);
    assert(rc_normalize_npc_id(7222) == 7221);

    int source_norm_count = 0;
    const RcSourceNormalization *source_norm =
        rc_game_data_source_normalization(full_data, &source_norm_count);
    assert(source_norm != NULL);
    assert(source_norm_count == stats->source_normalization_count);
    assert(rc_normalization_find_source(
               3, rc_normalization_hash_key("Diango")) >= 0);

    const RcDropData *drops = rc_game_data_drop_data(full_data);
    assert(drops != NULL);
    assert(drops->table_count == stats->drop_table_count);
    assert(drops->entry_count == stats->drop_entry_count);
    assert(drops->rdt_entry_count == stats->rdt_entry_count);
    assert(drops->gdt_entry_count == stats->gdt_entry_count);
    assert(drops->mrdt_entry_count == stats->mrdt_entry_count);
    const RcDropTable *obor = rc_drop_table_for_npc(7416);
    assert(obor != NULL);
    assert(obor >= drops->tables
           && obor < drops->tables + drops->table_count);

    int prayer_count = 0;
    const RcPrayerDef *prayers =
        rc_game_data_prayer_defs(full_data, &prayer_count);
    assert(prayers != NULL);
    assert(prayer_count == stats->prayer_count);
    assert(rc_prayer_def_get(RC_PRAYER_PIETY) == &prayers[RC_PRAYER_PIETY]);

    int spell_count = 0;
    const RcSpellDef *spells = rc_game_data_spell_defs(full_data, &spell_count);
    assert(spells != NULL);
    assert(spell_count == stats->spell_count);
    int fire_blast = rc_spell_find("Fire Blast");
    assert(fire_blast >= 0);
    assert(rc_spell_def_get(fire_blast) == &spells[fire_blast]);

    const RcVarData *vars = rc_game_data_var_data(full_data);
    assert(vars != NULL);
    assert(vars->varbit_count == stats->varbit_count);
    assert(vars->varp_count == stats->varp_count);
    int holy_grail = rc_varbit_find("VARBIT_5");
    assert(holy_grail == 5);
    assert(rc_varbit_def_get(holy_grail) == &vars->varbits[holy_grail]);
    assert(rc_varp_def_get(318) == &vars->varps[318]);

    const RcSkillData *skills = rc_game_data_skill_data(full_data);
    assert(skills != NULL);
    assert(skills->recipe_count == stats->recipe_count);
    assert(skills->drop_source_count == stats->skill_drop_source_count);
    assert(skills->drop_count == stats->skill_drop_count);
    assert(skills->gathering_node_count == stats->gathering_node_count);
    const RcRecipe *recipe = rc_recipe_find_output(2349);
    assert(recipe != NULL);
    assert(recipe >= skills->recipes
           && recipe < skills->recipes + skills->recipe_count);
    const RcSkillDropSource *source = rc_skill_drop_source_find("Tree");
    assert(source != NULL);
    assert(source >= skills->drop_sources
           && source < skills->drop_sources + skills->drop_source_count);
    int skill_drop_count = 0;
    const RcSkillDrop *skill_drops =
        rc_skill_drops_for(source, &skill_drop_count);
    assert(skill_drops != NULL && skill_drop_count > 0);
    assert(skill_drops >= skills->drops
           && skill_drops < skills->drops + skills->drop_count);
    int node_count = 0;
    const RcGatheringNode *nodes =
        rc_gathering_nodes_in_region(4921, &node_count);
    assert(nodes != NULL && node_count > 0);
    assert(nodes >= skills->gathering_nodes
           && nodes < skills->gathering_nodes + skills->gathering_node_count);

    const RcShopData *shops = rc_game_data_shop_data(full_data);
    assert(shops != NULL);
    assert(shops->shop_count == stats->shop_count);
    assert(shops->stock_count == stats->shop_stock_count);
    int bob_idx = rc_shop_find_by_name("Bob's Brilliant Axes.");
    assert(bob_idx >= 0);
    const RcShop *bob = rc_shop_get(bob_idx);
    assert(bob == &shops->shops[bob_idx]);
    int stock_count = 0;
    const RcShopStock *stock = rc_shop_stock_rows(bob, &stock_count);
    assert(stock != NULL && stock_count > 0);
    assert(stock >= shops->stock
           && stock < shops->stock + shops->stock_count);

    const RcQuestData *quests = rc_game_data_quest_data(full_data);
    assert(quests != NULL);
    assert(quests->count == stats->quest_count);
    int ds2 = rc_quest_find("Dragon Slayer II");
    assert(ds2 >= 0);
    assert(rc_quest_def_get(ds2) == &quests->defs[ds2]);

    const RcDialogueData *dialogue = rc_game_data_dialogue_data(full_data);
    assert(dialogue != NULL);
    assert(dialogue->transcript_count == stats->dialogue_transcript_count);
    assert(dialogue->node_count == stats->dialogue_node_count);
    assert(dialogue->child_id_count == stats->dialogue_child_id_count);
    int christmas = rc_dialogue_find_transcript("2013_Christmas_event");
    assert(christmas >= 0);
    const RcDialogueTranscriptDef *transcript =
        rc_dialogue_transcript_get(christmas);
    assert(transcript == &dialogue->transcripts[christmas]);
    int dialogue_node_count = 0;
    const RcDialogueDefNode *dialogue_nodes =
        rc_dialogue_nodes_for(transcript, &dialogue_node_count);
    assert(dialogue_nodes != NULL);
    assert(dialogue_nodes >= dialogue->nodes
           && dialogue_nodes < dialogue->nodes + dialogue->node_count);
    assert(rc_dialogue_string(dialogue_nodes[0].text_off)[0] != '\0');

    const RcObjectData *objects = rc_game_data_object_data(full_data);
    assert(objects != NULL);
    assert(objects->def_count == stats->object_def_count);
    assert(objects->behavior_count == stats->object_behavior_count);
    assert(objects->placement_count == stats->object_placement_count);
    assert(objects->transport_count == stats->object_transport_count);
    assert(objects->param_count == stats->object_param_count);
    assert(objects->placements != NULL);
    assert(objects->transports != NULL);
    assert(objects->params != NULL);
    const RcObjectDef *tree = rc_object_def_get(1276);
    assert(tree == &objects->defs[1276]);
    const RcObjectBehavior *tree_behavior = rc_object_behavior_get(1276);
    assert(tree_behavior == &objects->behaviors[1276]);
    int placement_count = 0;
    const RcObjectPlacement *placements =
        rc_object_region_placements(4921, &placement_count);
    assert(placements != NULL && placement_count > 0);
    assert(placements >= objects->placements
           && placements < objects->placements + objects->placement_count);
    const RcObjectTransport *transport =
        rc_object_transport_find(16683, 2465, 3495, 0, 0);
    assert(transport != NULL);
    assert(transport >= objects->transports
           && transport < objects->transports + objects->transport_count);

    const RcCollisionData *collision = rc_game_data_collision_data(full_data);
    assert(collision != NULL);
    assert(collision->region_count == stats->collision_region_count);
    assert(collision->regions != NULL);
    int found = 0;
    uint32_t blocked = rc_collision_flags_at(3072, 3395, 0, &found);
    uint16_t collision_ms = (uint16_t)(((3072 >> 6) << 8) | (3395 >> 6));
    int collision_idx = collision->index[collision_ms];
    assert(found == 1);
    assert(collision_idx >= 0 && collision_idx < collision->region_count);
    assert(blocked ==
           collision->regions[collision_idx].flags[0][3072 & 63][3395 & 63]);

    const RcAreaFlagData *area = rc_game_data_area_flag_data(full_data);
    assert(area != NULL);
    assert(area->row_count == stats->area_flag_count);
    assert(area->rows != NULL);
    assert(area->points != NULL);
    assert(area->refs != NULL);
    assert(area->tiles != NULL);
    uint32_t falador = rc_area_flags_at(2950, 3400, 0);
    uint16_t area_ms = (uint16_t)(((2950 >> 6) << 8) | (3400 >> 6));
    int area_tile_idx = area->tile_index[area_ms];
    assert(area_tile_idx >= 0 && area_tile_idx < area->tile_count);
    assert(falador ==
           area->tiles[area_tile_idx].flags[0][2950 & 63][3400 & 63]);

    const RcTraversalData *traversal =
        rc_game_data_traversal_data(full_data);
    assert(traversal != NULL);
    assert(traversal->edge_count == stats->traversal_edge_count);
    assert(traversal->edges != NULL);
    int traversal_count = 0;
    const RcTraversalEdge *item_edges =
        rc_traversal_edges_for(RC_TRAVERSAL_ITEM, 8013, &traversal_count);
    assert(item_edges != NULL && traversal_count > 300);
    assert(item_edges >= traversal->edges
           && item_edges < traversal->edges + traversal->edge_count);

    const RcActivitySchemaData *schemas =
        rc_game_data_activity_schema_data(full_data);
    assert(schemas != NULL);
    assert(schemas->count == stats->activity_schema_count);
    assert(schemas->rows != NULL);
    int fight_caves_schema = rc_activity_schema_find_slug("tzhaar_fight_cave");
    assert(fight_caves_schema >= 0);
    assert(schemas->rows[fight_caves_schema].npc_ids[0] != 0);

    const RcActivitySpawnData *spawns =
        rc_game_data_activity_spawn_data(full_data);
    assert(spawns != NULL);
    assert(spawns->count == stats->activity_spawn_count);
    assert(spawns->rows != NULL);
    int spawn_count = 0;
    const RcActivitySpawn *zulrah_spawns =
        rc_activity_spawns_for("zulrah", &spawn_count);
    assert(zulrah_spawns != NULL && spawn_count > 0);
    assert(zulrah_spawns >= spawns->rows
           && zulrah_spawns < spawns->rows + spawns->count);

    const RcActivityMechanicData *mechanics =
        rc_game_data_activity_mechanic_data(full_data);
    assert(mechanics != NULL);
    assert(mechanics->count == stats->activity_mechanic_count);
    int jad_mechanics = rc_activity_mechanics_find_slug("TzTok-Jad");
    assert(jad_mechanics >= 0);
    assert(rc_activity_mechanics_profile_for_npc(3127)
           == RC_ACTIVITY_PROFILE_TZTOK_JAD);

    const RcActivityStateData *states =
        rc_game_data_activity_state_data(full_data);
    assert(states != NULL);
    assert(states->count == stats->activity_state_count);
    int fight_caves_state = rc_activity_state_find_slug("tzhaar_fight_cave");
    assert(fight_caves_state >= 0);
    assert(rc_activity_state_find_for_npc(3127) == fight_caves_state);

    RcEncounterSpec specs[RC_ENC_REGISTRY_CAP];
    assert(rc_encounter_load_specs(RC_TEST_SOURCE_DIR "/data/defs/encounters.bin",
                                   specs, RC_ENC_REGISTRY_CAP)
           == stats->encounter_spec_count);
    assert(rc_encounter_load_specs(RC_TEST_SOURCE_DIR "/data/defs/encounters.bin",
                                   specs, 0) == -1);
    assert(rc_encounter_load_specs(RC_TEST_SOURCE_DIR "/missing/encounters.bin",
                                   specs, RC_ENC_REGISTRY_CAP) == -1);

    RcWorldConfig world_cfg = rc_preset_combat_only();
    world_cfg.seed = 17;
    RcWorld *w1 = rc_world_create_with_data(full_data, &world_cfg);
    assert(w1 != NULL);
    assert(rc_world_get_game_data(w1) == full_data);
    assert(w1->rng_state == 17);
    assert(w1->encounter.registry_count == stats->encounter_spec_count);

    world_cfg.seed = 29;
    RcWorld *w2 = rc_world_create_with_data(full_data, &world_cfg);
    assert(w2 != NULL);
    assert(rc_world_get_game_data(w2) == full_data);
    assert(w2->rng_state == 29);
    assert(w2->encounter.registry_count == stats->encounter_spec_count);
    assert(w1 != w2);

    rc_world_destroy(w1);
    rc_world_destroy(w2);
    rc_game_data_release(full_data);

    printf("test_game_data_shared: shared RcGameData lifecycle checks passed.\n");
    return 0;
}
