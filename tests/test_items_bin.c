#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "api.h"
#include "config.h"
#include "items.h"
#include "item_render_defs.h"

static void path_join(char *out, size_t out_sz, const char *rel) {
#ifdef RC_TEST_SOURCE_DIR
    snprintf(out, out_sz, "%s/%s", RC_TEST_SOURCE_DIR, rel);
#else
    snprintf(out, out_sz, "%s", rel);
#endif
}

int main(void) {
    char path[512];
    path_join(path, sizeof(path), "data/defs/items.bin");

    assert(rc_load_item_defs("/does/not/exist/items.bin") == -1);
    assert(rc_item_def_get(-1) == NULL);
    assert(rc_item_def_get(RC_MAX_ITEM_DEFS) == NULL);

    g_item_def_count = 0;
    int loaded = rc_load_item_defs(path);
    assert(loaded > 30000);

    const RcItemDef *coins = rc_item_def_get(995);
    const RcItemDef *whip = rc_item_def_get(4151);
    const RcItemDef *adamant_javelin = rc_item_def_get(829);
    const RcItemDef *agility_cape = rc_item_def_get(9771);
    const RcItemDef *spotted_cape = rc_item_def_get(10073);
    const RcItemDef *armadyl_godsword = rc_item_def_get(11802);
    const RcItemDef *combat_requirement_item = rc_item_def_get(11781);
    const RcItemDef *high_id = rc_item_def_get(30388);
    const RcItemDef *cache_fallback_equipment = rc_item_def_get(286);
    const RcItemDef *wiki_only = rc_item_def_get(33368);
    assert(coins && whip && adamant_javelin && agility_cape && spotted_cape);
    assert(armadyl_godsword && combat_requirement_item);
    assert(high_id && cache_fallback_equipment && wiki_only);
    assert(strcmp(coins->name, "Coins") == 0);
    assert(coins->stackable);
    assert(strcmp(whip->name, "Abyssal whip") == 0);
    assert(whip->equippable);
    assert(whip->equip_slot == EQUIP_WEAPON);
    assert(whip->req_count > 0);
    assert(whip->req_skill[0] == SKILL_ATTACK);
    assert(whip->req_level[0] == 70);
    assert(whip->attack_slash == 82);
    assert(whip->strength_bonus == 82);
    assert(whip->attack_speed == 4);
    assert(whip->attack_range == 1);
    assert((whip->metadata_flags & RC_ITEM_META_CACHE) != 0);
    assert((whip->metadata_flags & RC_ITEM_META_EQUIP_STATS) != 0);
    assert((whip->metadata_flags & RC_ITEM_META_WEAPON) != 0);
    assert(strcmp(whip->inventory_actions[1], "Wield") == 0);
    assert(strcmp(whip->inventory_actions[4], "Drop") == 0);
    assert(whip->examine[0] != '\0');
    assert(whip->linked_id_noted == 4152);
    assert(whip->linked_id_placeholder == 14032);
    assert(adamant_javelin->ranged_strength == 102);
    assert(agility_cape->prayer_bonus == 0);
    assert(spotted_cape->weight_grams == -2267);
    assert((armadyl_godsword->equip_conflicts
            & (1u << EQUIP_SHIELD)) != 0);
    assert(combat_requirement_item->required_combat_level == 40);
    assert(cache_fallback_equipment->equippable);
    assert(cache_fallback_equipment->equip_slot == EQUIP_BODY);
    assert(cache_fallback_equipment->equipable_by_player);
    assert((cache_fallback_equipment->metadata_flags & RC_ITEM_META_CACHE) != 0);
    assert((cache_fallback_equipment->metadata_flags & RC_ITEM_META_EQUIP) != 0);
    assert((cache_fallback_equipment->metadata_flags
            & RC_ITEM_META_EQUIP_STATS) == 0);
    assert((cache_fallback_equipment->metadata_flags
            & RC_ITEM_META_WEAPON) == 0);
    assert(cache_fallback_equipment->attack_ranged == 0);
    assert(cache_fallback_equipment->ranged_strength == 0);
    assert(cache_fallback_equipment->attack_speed == 0);
    assert(strcmp(wiki_only->name, "Demonic pacts demon butler scroll") == 0);
    assert(wiki_only->members);
    assert(wiki_only->value == 25000);

    int max_requirements = 0;
    for (int item_id = 0; item_id < RC_MAX_ITEM_DEFS; item_id++) {
        const RcItemDef *def = rc_item_def_get(item_id);
        if (def && def->noted)
            assert(def->stackable && "all banknotes must stack in ground and inventory containers");
        if (def && def->req_count > max_requirements)
            max_requirements = def->req_count;
    }
    assert(max_requirements > 4);

    RuneCItemDefRenderMap render_map = {0};
    assert(runec_item_def_render_map_load(&render_map, path) > 30000);
    const RuneCItemDefRenderRecord *coins_render =
        runec_item_def_render_find(&render_map, 995);
    const RuneCItemDefRenderRecord *whip_render =
        runec_item_def_render_find(&render_map, 4151);
    const RuneCItemDefRenderRecord *wiki_render =
        runec_item_def_render_find(&render_map, 33368);
    assert(coins_render);
    assert(coins_render->ground_model_id != RUNEC_RENDER_MODEL_MISSING);
    assert(whip_render);
    assert(whip_render->ground_model_id != RUNEC_RENDER_MODEL_MISSING);
    assert(whip_render->male_model_ids[0] != RUNEC_RENDER_MODEL_MISSING);
    assert(whip_render->female_model_ids[0] != RUNEC_RENDER_MODEL_MISSING);
    assert(wiki_render);
    assert(wiki_render->ground_model_id == 60186);
    assert(wiki_render->male_model_ids[0] == 60287);
    runec_item_def_render_map_free(&render_map);

    RcInvSlot inv[RC_INVENTORY_SIZE];
    for (int i = 0; i < RC_INVENTORY_SIZE; i++) {
        inv[i].item_id = -1;
        inv[i].quantity = 0;
    }
    assert(rc_inv_add(inv, 995, 10) == 0);
    assert(rc_inv_add(inv, 995, 5) == 0);
    assert(inv[0].quantity == 15);
    assert(rc_inv_add(inv, 30388, 1) == 1);

    g_item_def_count = 0;
    RcWorldConfig cfg = rc_preset_base_only();
    cfg.subsystems = RC_SUB_INVENTORY | RC_SUB_EQUIPMENT;
    cfg.items_path = path;
    RcWorld *world = rc_world_create_config(&cfg);
    assert(world != NULL);
    assert(g_item_def_count > 30000);
    assert(rc_item_def_get(30388) != NULL);
    assert(rc_item_def_get(33368) != NULL);
    rc_world_destroy(world);

    printf("test_items_bin: loaded full item definition binary.\n");
    return 0;
}
