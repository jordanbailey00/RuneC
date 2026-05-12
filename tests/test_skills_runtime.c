#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "api.h"
#include "config.h"
#include "items.h"
#include "skills.h"

#define ITEMS_PATH RC_TEST_SOURCE_DIR "/data/defs/items.bin"
#define RECIPES_PATH RC_TEST_SOURCE_DIR "/data/defs/recipes.bin"
#define SKILL_DROPS_PATH RC_TEST_SOURCE_DIR "/data/defs/skill_drops.bin"
#define GATHERING_PATH RC_TEST_SOURCE_DIR "/data/defs/gathering_nodes.bin"
#define BAD_PATH "/tmp/runec_bad_skills.bin"

static void write_bad_header(void) {
    uint32_t bad[3] = {0, 1, 0};
    FILE *f = fopen(BAD_PATH, "wb");
    assert(f != NULL);
    assert(fwrite(bad, sizeof(bad), 1, f) == 1);
    fclose(f);
}

int main(void) {
    write_bad_header();
    assert(rc_load_recipes(NULL) == -1);
    assert(rc_load_skill_drops(NULL) == -1);
    assert(rc_load_gathering_nodes(NULL) == -1);
    assert(rc_load_recipes("/missing/recipes.bin") == -1);
    assert(rc_load_recipes(BAD_PATH) == -1);
    assert(rc_load_skill_drops(BAD_PATH) == -1);
    assert(rc_load_gathering_nodes(BAD_PATH) == -1);

    assert(rc_load_item_defs(ITEMS_PATH) > 30000);
    assert(rc_load_recipes(RECIPES_PATH) == 3413);
    assert(rc_load_skill_drops(SKILL_DROPS_PATH) == 961);
    assert(rc_load_gathering_nodes(GATHERING_PATH) == 34096);

    const RcRecipe *bronze = rc_recipe_find_output(2349);
    assert(bronze != NULL);
    assert(rc_recipe_get(0) == bronze);
    assert(rc_recipe_get(-1) == NULL);
    assert(rc_recipe_find_output(-1) == NULL);
    assert(rc_recipe_find_output(999999) == NULL);
    assert(strcmp(bronze->name, "Bronze bar") == 0);
    assert(bronze->req_count >= 1);
    assert(bronze->reqs[0].skill == SKILL_SMITHING);
    assert(bronze->reqs[0].level == 1);
    assert(bronze->input_count == 2);
    assert(bronze->inputs[0].item_id == 436);
    assert(bronze->inputs[1].item_id == 438);

    const RcSkillDropSource *tree = rc_skill_drop_source_find("Tree");
    assert(tree != NULL);
    assert(rc_skill_drop_source_find(NULL) == NULL);
    int drop_count = 0;
    const RcSkillDrop *drops = rc_skill_drops_for(tree, &drop_count);
    assert(drops != NULL && drop_count == 3);
    assert(drops[0].item_id == 1511);
    assert(rc_skill_drops_for(NULL, &drop_count) == NULL);
    assert(drop_count == 0);
    assert(rc_skill_drop_source_find("Not A Skill Source") == NULL);

    int node_count = 0;
    const RcGatheringNode *region = rc_gathering_nodes_in_region(4921,
                                                                 &node_count);
    assert(region != NULL && node_count == 85);
    assert(rc_gathering_nodes_in_region(65535, &node_count) == NULL);
    assert(node_count == 0);
    const RcGatheringNode *node = rc_gathering_node_find(1161, 1239, 3692, 0);
    assert(node != NULL);
    assert(node->skill == 4);
    assert(node->action_mask != 0);
    assert(rc_gathering_node_find(1161, 1, 1, 0) == NULL);
    assert(rc_gathering_node_find(1161, -1, 3692, 0) == NULL);

    RcWorldConfig cfg = rc_preset_base_only();
    cfg.subsystems = RC_SUB_SKILLS | RC_SUB_INVENTORY | RC_SUB_EQUIPMENT;
    cfg.items_path = ITEMS_PATH;
    cfg.recipes_path = RECIPES_PATH;
    cfg.skill_drops_path = SKILL_DROPS_PATH;
    cfg.gathering_nodes_path = GATHERING_PATH;
    RcWorld *world = rc_world_create_config(&cfg);
    assert(world != NULL);
    assert(world->enabled & RC_SUB_SKILLS);
    assert(rc_recipe_player_can_make(world, bronze) == 0);
    assert(rc_inv_add(world->player.inventory, 436, 1) >= 0);
    assert(rc_inv_add(world->player.inventory, 438, 1) >= 0);
    assert(rc_recipe_player_can_make(world, bronze) == 1);
    assert(rc_player_apply_recipe(world, bronze) == 1);
    assert(rc_inv_find(world->player.inventory, 436) == -1);
    assert(rc_inv_find(world->player.inventory, 438) == -1);
    assert(rc_inv_find(world->player.inventory, 2349) >= 0);
    assert(world->player.skills.xp[SKILL_SMITHING] >= 6);
    assert(world->player.skill_action == 2349);
    assert(world->player.skill_timer == bronze->ticks);

    const RcRecipe *helm = rc_recipe_find_output(1139);
    assert(helm != NULL);
    assert(rc_recipe_player_can_make(world, helm) == 0);
    world->player.skills.base_level[SKILL_SMITHING] = 99;
    world->player.skills.boosted_level[SKILL_SMITHING] = 99;
    assert(rc_recipe_player_can_make(world, helm) == 0);
    assert(rc_inv_add(world->player.inventory, 2347, 1) >= 0);
    assert(rc_recipe_player_can_make(world, helm) == 1);
    rc_add_xp(&world->player.skills, SKILL_SMITHING, 1000);
    assert(rc_combat_level(&world->player.skills) > 0);
    rc_world_destroy(world);

    RcWorldConfig base_cfg = rc_preset_base_only();
    RcWorld *base = rc_world_create_config(&base_cfg);
    assert(base != NULL);
    assert(rc_recipe_player_can_make(base, bronze) == 0);
    assert(rc_player_apply_recipe(base, bronze) == 0);
    rc_world_destroy(base);

    return 0;
}
