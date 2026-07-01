#ifndef RC_SKILLS_H
#define RC_SKILLS_H

#include "types.h"

#define RC_RECIPE_MAX_REQS 8
#define RC_RECIPE_MAX_INPUTS 16
#define RC_RECIPE_MAX_TOOLS 8
#define RC_SKILL_NODE_REGION_COUNT 65536

typedef struct {
    uint8_t skill, level;
    uint16_t xp_q1;
} RcRecipeReq;

typedef struct {
    uint32_t item_id;
    uint16_t quantity;
} RcRecipeItemQty;

typedef struct {
    char name[64];
    char facility[48];
    RcRecipeReq reqs[RC_RECIPE_MAX_REQS];
    RcRecipeItemQty inputs[RC_RECIPE_MAX_INPUTS];
    uint32_t tools[RC_RECIPE_MAX_TOOLS];
    uint32_t output_item;
    uint16_t output_qty, ticks;
    uint8_t req_count, input_count, tool_count, flags;
} RcRecipe;

typedef struct {
    uint32_t item_id, rarity_inv;
    uint16_t qmin, qmax;
} RcSkillDrop;

typedef struct {
    char name[64];
    uint32_t first_drop;
    uint16_t drop_count;
} RcSkillDropSource;

typedef struct {
    uint32_t obj_id;
    uint16_t x, y, mapsquare;
    uint8_t plane, skill, action_mask, type, rotation, flags;
} RcGatheringNode;

typedef struct {
    uint32_t first, count;
} RcSkillRegionIndex;

typedef struct {
    RcRecipe *recipes;
    int recipe_count;
    int *recipe_next;
    int recipe_by_output[RC_MAX_ITEM_DEFS];
    RcSkillDropSource *drop_sources;
    RcSkillDrop *drops;
    int drop_source_count;
    int drop_count;
    RcGatheringNode *gathering_nodes;
    int gathering_node_count;
    RcSkillRegionIndex gathering_region_index[RC_SKILL_NODE_REGION_COUNT];
} RcSkillData;

extern RcRecipe *g_rc_recipes;
extern RcSkillDropSource *g_rc_skill_drop_sources;
extern RcSkillDrop *g_rc_skill_drops;
extern RcGatheringNode *g_rc_gathering_nodes;
extern int g_rc_recipe_count;
extern int g_rc_skill_drop_source_count;
extern int g_rc_skill_drop_count;
extern int g_rc_gathering_node_count;

// Precomputed XP table (xp_table[level] = xp required for that level)
extern const int RC_XP_TABLE[100];

int rc_load_recipes(const char *path);
int rc_load_skill_drops(const char *path);
int rc_load_gathering_nodes(const char *path);
void rc_skill_data_init(RcSkillData *data);
void rc_skill_data_free(RcSkillData *data);
int rc_load_recipes_into(const char *path, RcSkillData *out);
int rc_load_skill_drops_into(const char *path, RcSkillData *out);
int rc_load_gathering_nodes_into(const char *path, RcSkillData *out);
int rc_skill_data_import_globals(RcSkillData *out);
int rc_skills_mirror_to_globals(const RcSkillData *data);
void rc_skills_use_data(const RcSkillData *data);
void rc_skills_reset_data_if_active(const RcSkillData *data);

const RcRecipe *rc_recipe_get(int idx);
const RcRecipe *rc_recipe_find_output(int item_id);
const RcSkillDropSource *rc_skill_drop_source_find(const char *name);
const RcSkillDrop *rc_skill_drops_for(const RcSkillDropSource *source,
                                      int *count);
const RcGatheringNode *rc_gathering_nodes_in_region(uint16_t mapsquare,
                                                    int *count);
const RcGatheringNode *rc_gathering_node_find(int obj_id, int x, int y,
                                              int plane);
int rc_recipe_player_can_make(const RcWorld *world, const RcRecipe *recipe);
int rc_player_apply_recipe(RcWorld *world, const RcRecipe *recipe);

// Level from XP (binary search)
int rc_level_for_xp(int xp);

// Add XP to a skill, update base level if it changed
void rc_add_xp(RcSkills *skills, RcSkill skill, int xp);

// Combat level formula
int rc_combat_level(const RcSkills *skills);

// Stat restore: boosted stats decay toward base (every 60 ticks)
void rc_stat_restore_tick(RcSkills *skills);

#endif
