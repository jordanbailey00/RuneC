#include <assert.h>
#include <stdio.h>

#include "api.h"
#include "config.h"
#include "interaction.h"
#include "items.h"

#define ITEM_PATH RC_TEST_SOURCE_DIR "/data/defs/items.bin"

static RcWorld *phase1_world(void) {
    RcWorldConfig cfg = rc_preset_base_only();
    cfg.subsystems = RC_SUB_INVENTORY | RC_SUB_LOOT;
    cfg.items_path = ITEM_PATH;
    RcWorld *world = rc_world_create_config(&cfg);
    assert(world != NULL);
    return world;
}

static int active_ground_items(const RcWorld *world, int item_id) {
    int count = 0;
    for (int i = 0; i < world->ground_item_count; i++) {
        const RcGroundItem *g = &world->ground_items[i];
        if (g->active && (item_id < 0 || g->item_id == item_id))
            count++;
    }
    return count;
}

static void test_stackable_drop_merges_and_reveals(void) {
    RcWorld *world = phase1_world();
    RcPlayer *p = &world->player;

    int slot = rc_inv_add(p->inventory, 995, 100);
    assert(slot >= 0);
    rc_player_drop_item(world, slot);
    assert(active_ground_items(world, 995) == 1);
    assert(world->ground_items[0].quantity == 100);
    assert(world->ground_items[0].uid > 0);
    assert(world->ground_items[0].version > 0);
    assert(world->ground_items[0].visibility == RC_GROUND_VIS_PRIVATE);
    assert(world->ground_items[0].owner_uid == RC_GROUND_OWNER_LOCAL_PLAYER);
    int uid = world->ground_items[0].uid;
    int version = world->ground_items[0].version;

    slot = rc_inv_add(p->inventory, 995, 200);
    assert(slot >= 0);
    rc_player_drop_item(world, slot);
    assert(active_ground_items(world, 995) == 1);
    assert(world->ground_items[0].uid == uid);
    assert(world->ground_items[0].version == version + 1);
    assert(world->ground_items[0].quantity == 300);
    assert(world->ground_items[0].despawn_timer == 300);

    for (int i = 0; i < 100; i++)
        rc_world_tick(world);
    assert(world->ground_items[0].active);
    assert(world->ground_items[0].visibility == RC_GROUND_VIS_PUBLIC);
    assert(world->ground_items[0].owner_uid == RC_GROUND_OWNER_NONE);
    assert(world->ground_items[0].reveal_timer == 0);

    for (int i = 0; i < 200; i++)
        rc_world_tick(world);
    assert(!world->ground_items[0].active);
    assert(world->ground_items[0].quantity == 0);

    rc_world_destroy(world);
}

static void test_nonstackable_quantity_splits(void) {
    RcWorld *world = phase1_world();
    RcPlayer *p = &world->player;

    p->inventory[0].item_id = 1048;
    p->inventory[0].quantity = 3;
    rc_player_drop_item(world, 0);
    assert(p->inventory[0].item_id == -1);
    assert(active_ground_items(world, 1048) == 3);
    assert(world->ground_item_count == 3);
    assert(world->ground_items[0].uid != world->ground_items[1].uid);
    assert(world->ground_items[1].uid != world->ground_items[2].uid);
    for (int i = 0; i < 3; i++) {
        assert(world->ground_items[i].active);
        assert(world->ground_items[i].item_id == 1048);
        assert(world->ground_items[i].quantity == 1);
        assert(world->ground_items[i].visibility == RC_GROUND_VIS_PRIVATE);
    }

    rc_world_destroy(world);
}

static void test_stale_generation_cancels_pickup(void) {
    RcWorld *world = phase1_world();
    RcPlayer *p = &world->player;

    world->ground_item_count = 1;
    world->ground_items[0] = (RcGroundItem){
        .uid = 42,
        .version = 7,
        .item_id = 995,
        .quantity = 100,
        .x = p->x + 2,
        .y = p->y,
        .plane = p->plane,
        .owner_uid = RC_GROUND_OWNER_NONE,
        .original_owner_uid = RC_GROUND_OWNER_NONE,
        .despawn_timer = 300,
        .visibility = RC_GROUND_VIS_PUBLIC,
        .active = true,
    };

    rc_player_pickup_item(world, 0);
    assert(rc_interaction_is_active(p));
    assert(p->interaction.target.entity_uid == 42);
    assert(p->interaction.target.entity_generation == 7);
    world->ground_items[0].version++;
    rc_world_tick(world);
    assert(!rc_interaction_is_active(p));
    assert(p->interaction.last_failure ==
           RC_INTERACTION_FAIL_TARGET_VERSION_CHANGED);
    assert(world->ground_items[0].active);
    assert(rc_inv_find(p->inventory, 995) < 0);

    rc_world_destroy(world);
}

static void test_private_foreign_item_is_not_pickable(void) {
    RcWorld *world = phase1_world();
    RcPlayer *p = &world->player;

    world->ground_item_count = 1;
    world->ground_items[0] = (RcGroundItem){
        .uid = 100,
        .version = 1,
        .item_id = 995,
        .quantity = 50,
        .x = p->x,
        .y = p->y,
        .plane = p->plane,
        .owner_uid = 99,
        .original_owner_uid = 99,
        .despawn_timer = 300,
        .visibility = RC_GROUND_VIS_PRIVATE,
        .active = true,
    };

    rc_player_pickup_item(world, 0);
    assert(world->ground_items[0].active);
    assert(rc_inv_find(p->inventory, 995) < 0);

    rc_world_destroy(world);
}

int main(void) {
    test_stackable_drop_merges_and_reveals();
    test_nonstackable_quantity_splits();
    test_stale_generation_cancels_pickup();
    test_private_foreign_item_is_not_pickable();
    printf("test_ground_items_phase1: existing ground item path hardened.\n");
    return 0;
}
