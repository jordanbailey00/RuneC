#include <assert.h>
#include <stdio.h>

#include "api.h"
#include "combat.h"
#include "config.h"
#include "items.h"

#define ITEM_PATH RC_TEST_SOURCE_DIR "/data/defs/items.bin"

static void max_melee_stats(RcPlayer *p) {
    p->skills.base_level[SKILL_ATTACK] = 99;
    p->skills.base_level[SKILL_DEFENCE] = 99;
    p->skills.base_level[SKILL_STRENGTH] = 99;
}

int main(void) {
    RcWorldConfig cfg = rc_preset_base_only();
    cfg.subsystems = RC_SUB_INVENTORY | RC_SUB_EQUIPMENT | RC_SUB_LOOT;
    cfg.items_path = ITEM_PATH;
    RcWorld *world = rc_world_create_config(&cfg);
    assert(world != NULL);
    assert(g_item_def_count > 30000);

    RcPlayer *p = &world->player;
    max_melee_stats(p);

    int coins = rc_inv_add(p->inventory, 995, 100);
    assert(coins == 0);
    assert(rc_inv_add(p->inventory, 995, 50) == 0);
    assert(p->inventory[0].quantity == 150);

    int whip = rc_inv_add(p->inventory, 4151, 1);
    assert(whip >= 0);
    rc_player_equip(world, whip);
    assert(p->equipment[EQUIP_WEAPON].item_id == 4151);
    assert(p->equipment_bonuses[EQ_SLASH_ATK] == 82);
    assert(p->equipment_bonuses[EQ_STR] == 82);

    rc_player_unequip(world, EQUIP_WEAPON);
    assert(p->equipment[EQUIP_WEAPON].item_id == -1);
    assert(rc_inv_find(p->inventory, 4151) >= 0);
    assert(p->equipment_bonuses[EQ_STR] == 0);

    int shield = rc_inv_add(p->inventory, 10352, 1);
    assert(shield >= 0);
    rc_player_equip(world, shield);
    assert(p->equipment[EQUIP_SHIELD].item_id == 10352);

    int ags = rc_inv_add(p->inventory, 11802, 1);
    assert(ags >= 0);
    rc_player_equip(world, ags);
    assert(p->equipment[EQUIP_WEAPON].item_id == 11802);
    assert(p->equipment[EQUIP_SHIELD].item_id == -1);
    assert(rc_inv_find(p->inventory, 10352) >= 0);

    int old_from = rc_inv_add(p->inventory, 1048, 1);
    int old_to = rc_inv_free_slot(p->inventory);
    assert(old_from >= 0 && old_to >= 0);
    assert(rc_player_move_inventory_item(world, old_from, old_to));
    assert(p->inventory[old_to].item_id == 1048);
    assert(p->inventory[old_from].item_id == -1);

    int drop_slot = rc_inv_find(p->inventory, 995);
    assert(drop_slot >= 0);
    rc_player_drop_item(world, drop_slot);
    assert(p->inventory[drop_slot].item_id == -1);
    assert(world->ground_item_count == 1);
    assert(world->ground_items[0].active);
    assert(world->ground_items[0].item_id == 995);
    rc_player_pickup_item(world, 0);
    assert(!world->ground_items[0].active);
    assert(rc_inv_find(p->inventory, 995) >= 0);

    RcWorld *low = rc_world_create_config(&cfg);
    assert(low != NULL);
    int low_whip = rc_inv_add(low->player.inventory, 4151, 1);
    assert(low_whip >= 0);
    rc_player_equip(low, low_whip);
    assert(low->player.equipment[EQUIP_WEAPON].item_id == -1);
    low->player.skills.base_level[SKILL_ATTACK] = 70;
    rc_player_equip(low, low_whip);
    assert(low->player.equipment[EQUIP_WEAPON].item_id == 4151);
    rc_world_destroy(low);

    rc_world_destroy(world);
    printf("test_inventory_equipment_runtime: inventory/equipment runtime OK.\n");
    return 0;
}
