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
    const RcItemDef *high_id = rc_item_def_get(30388);
    const RcItemDef *cache_fallback_equipment = rc_item_def_get(30390);
    const RcItemDef *wiki_only = rc_item_def_get(33368);
    assert(coins && whip && adamant_javelin && agility_cape);
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
    assert(whip->linked_id_noted == 4152);
    assert(whip->linked_id_placeholder == 14032);
    assert(adamant_javelin->ranged_strength == 107);
    assert(agility_cape->prayer_bonus == 0);
    assert(cache_fallback_equipment->equippable);
    assert(cache_fallback_equipment->equip_slot == EQUIP_WEAPON);
    assert(cache_fallback_equipment->attack_ranged == 0);
    assert(cache_fallback_equipment->ranged_strength == 0);
    assert(strcmp(wiki_only->name, "Demonic pacts demon butler scroll") == 0);
    assert(wiki_only->members);
    assert(wiki_only->value == 25000);

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
