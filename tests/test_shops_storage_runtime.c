#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "api.h"
#include "config.h"
#include "items.h"
#include "npc.h"
#include "objects.h"
#include "shops.h"
#include "storage.h"

#define SHOP_PATH RC_TEST_SOURCE_DIR "/data/defs/shops.bin"
#define ITEM_PATH RC_TEST_SOURCE_DIR "/data/defs/items.bin"
#define ODEF_PATH RC_TEST_SOURCE_DIR "/data/defs/object_defs.bin"
#define OBHV_PATH RC_TEST_SOURCE_DIR "/data/defs/object_behaviors.bin"
#define BAD_PATH "/tmp/runec_bad_shop.bin"

static void write_bad_header(void) {
    uint32_t bad[3] = {0, 1, 0};
    FILE *f = fopen(BAD_PATH, "wb");
    assert(f != NULL);
    assert(fwrite(bad, sizeof(bad), 1, f) == 1);
    fclose(f);
}

int main(void) {
    int stock_count = 0;
    write_bad_header();
    assert(rc_load_shops(NULL) == -1);
    assert(rc_load_shops("/missing/shops.bin") == -1);
    assert(rc_load_shops(BAD_PATH) == -1);
    RcWorldConfig shop_cfg = rc_preset_base_only();
    shop_cfg.subsystems = RC_SUB_SHOPS;
    shop_cfg.shops_path = SHOP_PATH;
    RcWorld *shop_world = rc_world_create_config(&shop_cfg);
    assert(shop_world != NULL);
    assert(g_shop_count == 601);
    rc_world_destroy(shop_world);

    assert(rc_load_shops(SHOP_PATH) == 601);
    assert(g_shop_stock_count == 6209);
    assert(rc_shop_get(-1) == NULL);
    stock_count = -1;
    assert(rc_shop_stock_rows(NULL, &stock_count) == NULL);
    assert(stock_count == 0);

    int bob_idx = rc_shop_find_by_name("Bob's Brilliant Axes.");
    assert(bob_idx >= 0);
    const RcShop *bob = rc_shop_get(bob_idx);
    assert(bob != NULL);
    assert(strcmp(bob->owner, "Bob") == 0);
    assert(strcmp(bob->location, "Lumbridge") == 0);
    assert(strcmp(bob->specialty, "Axes") == 0);
    assert(rc_shop_find_by_name("missing shop") == -1);
    assert(rc_shop_has_item(bob_idx, 1351));
    assert(!rc_shop_has_item(bob_idx, 995));

    stock_count = 0;
    const RcShopStock *stock = rc_shop_stock_rows(bob, &stock_count);
    assert(stock && stock_count == 7);
    assert(stock[1].item_id == 1351);
    assert(stock[1].base_stock == 10);
    assert(stock[1].restock_ticks == 100);

    assert(rc_load_object_defs(ODEF_PATH) > 60000);
    assert(rc_load_object_behaviors(OBHV_PATH) > 8000);
    assert(rc_load_item_defs(ITEM_PATH) > 10000);
    const RcObjectBehavior *deposit_b = rc_object_behavior_get(10529);
    assert(deposit_b && (deposit_b->flags & RC_OBJ_BEHAVIOR_STORAGE));
    assert(rc_storage_kind_for_object(10355, 1) == RC_STORAGE_BANK);
    assert(rc_storage_kind_for_object(4483, 0) == RC_STORAGE_BANK);
    assert(rc_storage_kind_for_object(10529, 0) == RC_STORAGE_DEPOSIT_BOX);
    assert(rc_storage_kind_for_object(10355, 2) == RC_STORAGE_COLLECTION);
    assert(rc_storage_kind_for_object(30987, 0) == RC_STORAGE_CONTAINER);
    assert(rc_storage_kind_for_object(1276, 0) == RC_STORAGE_NONE);

    RcWorldConfig cfg = rc_preset_base_only();
    cfg.subsystems = RC_SUB_INVENTORY | RC_SUB_SHOPS | RC_SUB_STORAGE
                   | RC_SUB_OBJECTS;
    cfg.items_path = ITEM_PATH;
    cfg.shops_path = SHOP_PATH;
    cfg.object_defs_path = ODEF_PATH;
    cfg.object_behaviors_path = OBHV_PATH;
    RcWorld *world = rc_world_create_config(&cfg);
    assert(world != NULL);
    assert(world->enabled & RC_SUB_SHOPS);
    assert(world->enabled & RC_SUB_STORAGE);

    rc_player_interact_object(world, 10355, 1);
    assert(world->player.storage_kind == RC_STORAGE_BANK);
    assert(world->player.interact_type == 3);

    assert(rc_inv_add(world->player.inventory, 1351, 3) >= 0);
    assert(world->player.inventory[0].item_id == 1351);
    assert(world->player.inventory[1].item_id == 1351);
    assert(world->player.inventory[2].item_id == 1351);
    assert(rc_bank_deposit_slot(world, 0, 2) == 1);
    assert(world->player.inventory[0].item_id == -1);
    assert(world->player.bank[0].item_id == 1351);
    assert(world->player.bank[0].quantity == 1);
    assert(world->player.bank_tab[0] == 0);
    assert(rc_bank_withdraw_slot(world, 0, 1) == 1);
    assert(world->player.bank[0].item_id == -1);
    assert(world->player.bank_tab[0] == 0);
    world->player.bank[5].item_id = 995;
    world->player.bank[5].quantity = 10;
    assert(rc_bank_withdraw_slot(world, 5, 4) == 4);
    assert(world->player.bank[5].quantity == 6);
    int tab_slot = rc_bank_add_item_tab(world, 995, 10, 1);
    assert(tab_slot >= 0);
    assert(world->player.bank_tab[tab_slot] == 1);
    assert(rc_bank_add_item_tab(world, 995, 5, 1) == tab_slot);
    assert(world->player.bank[tab_slot].quantity == 15);
    int other_tab_slot = rc_bank_add_item_tab(world, 995, 7, 2);
    assert(other_tab_slot >= 0 && other_tab_slot != tab_slot);
    assert(world->player.bank_tab[other_tab_slot] == 2);

    rc_player_interact_object(world, 10529, 0);
    assert(world->player.storage_kind == RC_STORAGE_DEPOSIT_BOX);
    assert(rc_bank_withdraw_slot(world, 0, 1) == -1);
    assert(rc_bank_deposit_slot(world, 0, 0) == 1);
    assert(world->player.inventory[0].item_id == -1);
    assert(rc_bank_deposit_slot(world, 1, 0) == 1);
    assert(world->player.inventory[1].item_id == -1);
    assert(rc_bank_deposit_slot(world, 2, 0) == 1);
    assert(world->player.inventory[2].item_id == -1);
    int axe_bank_slot = -1;
    for (int i = 0; i < RC_BANK_SIZE; i++) {
        if (world->player.bank[i].item_id == 1351) {
            axe_bank_slot = i;
            break;
        }
    }
    assert(axe_bank_slot >= 0);
    assert(world->player.bank[axe_bank_slot].quantity == 3);
    assert(rc_bank_add_item(world, 4151, 1) >= 0);
    assert(rc_player_close_storage(world) == 1);
    assert(world->player.storage_kind == RC_STORAGE_NONE);

    int def_idx = g_npc_def_count++;
    assert(def_idx < RC_MAX_NPC_DEFS);
    memset(&g_npc_defs[def_idx], 0, sizeof(g_npc_defs[def_idx]));
    g_npc_defs[def_idx].id = 990001;
    strcpy(g_npc_defs[def_idx].name, "Storage Banker");
    g_npc_defs[def_idx].size = 1;
    strcpy(g_npc_defs[def_idx].options[1], "Bank");
    assert(rc_storage_kind_for_npc(&g_npc_defs[def_idx], 1)
           == RC_STORAGE_BANK);
    int npc_idx = rc_npc_spawn(world, def_idx, world->player.x + 1,
                               world->player.y, world->player.plane);
    assert(npc_idx >= 0);
    rc_player_interact_npc(world, world->npcs[npc_idx].uid, 1);
    rc_world_tick(world);
    assert(world->player.storage_kind == RC_STORAGE_BANK);
    assert(world->player.storage_target == world->npcs[npc_idx].uid);
    rc_world_destroy(world);

    RcWorldConfig base_cfg = rc_preset_base_only();
    RcWorld *base = rc_world_create_config(&base_cfg);
    assert(base != NULL);
    rc_player_interact_object(base, 10355, 1);
    assert(base->player.storage_kind == RC_STORAGE_NONE);
    assert(rc_bank_deposit_slot(base, 0, 1) == -1);
    rc_world_destroy(base);
    return 0;
}
