#ifndef RC_GAME_DATA_H
#define RC_GAME_DATA_H

#include "config.h"
#include "activity_mechanics.h"
#include "activity_schemas.h"
#include "activity_spawns.h"
#include "activity_states.h"
#include "area_flags.h"
#include "collision.h"
#include "dialogue.h"
#include "drops.h"
#include "encounter.h"
#include "items.h"
#include "normalization.h"
#include "npc.h"
#include "objects.h"
#include "prayer.h"
#include "quests.h"
#include "shops.h"
#include "skills.h"
#include "spells.h"
#include "traversal.h"
#include "varbits.h"

#include <stddef.h>
#include <stdint.h>

enum {
    RC_GAME_DATA_MESSAGE_CAP = 512,
};

typedef struct RcGameData RcGameData;

typedef struct {
    int ok;
    int manifest_present;
    int manifest_format_ok;
    int data_version_present;
    int required_path_count;
    int pack_file_count;
    int missing_pack_files;
    int missing_required_paths;
    int blocked_provenance_count;
    char message[RC_GAME_DATA_MESSAGE_CAP];
} RcGameDataValidationReport;

int rc_game_data_validate_install(const char *data_root,
                                  RcGameDataValidationReport *out);

typedef struct {
    uint32_t subsystems;
    int npc_def_count;
    int item_def_count;
    int item_normalization_count;
    int npc_normalization_count;
    int source_normalization_count;
    int drop_table_count;
    int drop_entry_count;
    int rdt_entry_count;
    int gdt_entry_count;
    int mrdt_entry_count;
    int prayer_count;
    int spell_count;
    int object_def_count;
    int object_behavior_count;
    int object_placement_count;
    int object_transport_count;
    int object_param_count;
    int collision_region_count;
    int area_flag_count;
    int traversal_edge_count;
    int activity_schema_count;
    int activity_spawn_count;
    int activity_mechanic_count;
    int activity_state_count;
    int encounter_spec_count;
    int varbit_count;
    int varp_count;
    int recipe_count;
    int skill_drop_source_count;
    int skill_drop_count;
    int gathering_node_count;
    int shop_count;
    int shop_stock_count;
    int quest_count;
    int dialogue_transcript_count;
    int dialogue_node_count;
    int dialogue_child_id_count;
} RcGameDataStats;

typedef struct {
    int ok;
    int validation_ok;
    RcGameDataValidationReport validation;
    RcGameDataStats stats;
    char message[RC_GAME_DATA_MESSAGE_CAP];
} RcGameDataLoadReport;

RcGameData *rc_game_data_load(const RcWorldConfig *cfg,
                              RcGameDataLoadReport *out);
void rc_game_data_retain(RcGameData *data);
void rc_game_data_release(RcGameData *data);
uint32_t rc_game_data_subsystems(const RcGameData *data);
const RcGameDataStats *rc_game_data_stats(const RcGameData *data);
void rc_game_data_activate_views(const RcGameData *data,
                                 uint32_t subsystems);
const RcNpcDef *rc_game_data_npc_defs(const RcGameData *data, int *count);
const RcItemDef *rc_game_data_item_defs(const RcGameData *data, int *count);
const RcDropData *rc_game_data_drop_data(const RcGameData *data);
const RcObjectData *rc_game_data_object_data(const RcGameData *data);
const RcCollisionData *rc_game_data_collision_data(const RcGameData *data);
const RcAreaFlagData *rc_game_data_area_flag_data(const RcGameData *data);
const RcTraversalData *rc_game_data_traversal_data(const RcGameData *data);
const RcActivitySchemaData *rc_game_data_activity_schema_data(
    const RcGameData *data);
const RcActivitySpawnData *rc_game_data_activity_spawn_data(
    const RcGameData *data);
const RcActivityMechanicData *rc_game_data_activity_mechanic_data(
    const RcGameData *data);
const RcActivityStateData *rc_game_data_activity_state_data(
    const RcGameData *data);
const RcItemNormalization *rc_game_data_item_normalization(
    const RcGameData *data, int *count);
const RcNpcNormalization *rc_game_data_npc_normalization(
    const RcGameData *data, int *count);
const RcSourceNormalization *rc_game_data_source_normalization(
    const RcGameData *data, int *count);
const RcPrayerDef *rc_game_data_prayer_defs(const RcGameData *data,
                                            int *count);
const RcSpellDef *rc_game_data_spell_defs(const RcGameData *data,
                                          int *count);
const RcEncounterSpec *rc_game_data_encounter_specs(const RcGameData *data,
                                                    int *count);
const RcVarData *rc_game_data_var_data(const RcGameData *data);
const RcSkillData *rc_game_data_skill_data(const RcGameData *data);
const RcShopData *rc_game_data_shop_data(const RcGameData *data);
const RcQuestData *rc_game_data_quest_data(const RcGameData *data);
const RcDialogueData *rc_game_data_dialogue_data(const RcGameData *data);

#endif
