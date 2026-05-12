#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "api.h"
#include "config.h"
#include "events.h"
#include "interaction.h"
#include "items.h"

#define ITEM_PATH RC_TEST_SOURCE_DIR "/data/defs/items.bin"

typedef struct {
    int count;
    RcPayloadItemEvent last;
} PickupSpy;

static void on_pickup(RcWorld *world, int evt, const void *payload,
                      void *ctx) {
    (void)world;
    assert(evt == RC_EVT_ITEM_PICKED_UP);
    PickupSpy *spy = (PickupSpy *)ctx;
    spy->count++;
    memcpy(&spy->last, payload, sizeof(spy->last));
}

static RcWorld *phase2_world(void) {
    RcWorldConfig cfg = rc_preset_base_only();
    cfg.subsystems = RC_SUB_INVENTORY | RC_SUB_LOOT;
    cfg.items_path = ITEM_PATH;
    RcWorld *world = rc_world_create_config(&cfg);
    assert(world != NULL);
    return world;
}

static void fill_inventory(RcPlayer *p) {
    for (int i = 0; i < RC_INVENTORY_SIZE; i++) {
        p->inventory[i].item_id = 1048;
        p->inventory[i].quantity = 1;
    }
}

static void tick_until_inactive(RcWorld *world, int max_ticks) {
    for (int i = 0; i < max_ticks && rc_interaction_is_active(&world->player);
            i++) {
        rc_world_tick(world);
    }
}

static void put_ground_item(RcWorld *world, int x, int y) {
    world->ground_item_count = 1;
    world->ground_items[0] = (RcGroundItem){
        .uid = 77,
        .version = 3,
        .item_id = 995,
        .quantity = 250,
        .x = x,
        .y = y,
        .plane = world->player.plane,
        .owner_uid = RC_GROUND_OWNER_NONE,
        .original_owner_uid = RC_GROUND_OWNER_NONE,
        .despawn_timer = 300,
        .visibility = RC_GROUND_VIS_PUBLIC,
        .active = true,
    };
}

static void test_take_helper_fires_pickup_event(void) {
    RcWorld *world = phase2_world();
    PickupSpy spy = {0};
    assert(rc_event_subscribe(world, RC_EVT_ITEM_PICKED_UP, on_pickup,
                              &spy) == 0);
    put_ground_item(world, world->player.x, world->player.y);

    int result = rc_player_take_ground_item(world, 0, 77, 3);
    assert(result == RC_GROUND_TAKE_OK);
    assert(!world->ground_items[0].active);
    assert(world->ground_items[0].version == 4);
    assert(rc_inv_find(world->player.inventory, 995) >= 0);
    assert(spy.count == 1);
    assert(spy.last.item_id == 995);
    assert(spy.last.quantity == 250);

    rc_world_destroy(world);
}

static void test_take_helper_rejects_stale_generation(void) {
    RcWorld *world = phase2_world();
    put_ground_item(world, world->player.x, world->player.y);

    int result = rc_player_take_ground_item(world, 0, 77, 2);
    assert(result == RC_GROUND_TAKE_STALE);
    assert(world->ground_items[0].active);
    assert(world->ground_items[0].quantity == 250);
    assert(rc_inv_find(world->player.inventory, 995) < 0);

    rc_world_destroy(world);
}

static void test_full_inventory_leaves_item_on_ground(void) {
    RcWorld *world = phase2_world();
    fill_inventory(&world->player);
    put_ground_item(world, world->player.x, world->player.y);

    int result = rc_player_take_ground_item(world, 0, 77, 3);
    assert(result == RC_GROUND_TAKE_FULL);
    assert(world->ground_items[0].active);
    assert(world->ground_items[0].quantity == 250);
    assert(rc_inv_find(world->player.inventory, 995) < 0);

    rc_world_destroy(world);
}

static void test_default_take_handler_reports_full_inventory(void) {
    rc_interaction_clear_handlers();
    RcWorld *world = phase2_world();
    put_ground_item(world, world->player.x + 2, world->player.y);
    fill_inventory(&world->player);

    rc_player_pickup_item(world, 0);
    assert(rc_interaction_is_active(&world->player));
    tick_until_inactive(world, 16);
    assert(!rc_interaction_is_active(&world->player));
    assert(world->ground_items[0].active);
    assert(world->ground_items[0].quantity == 250);
    assert(world->player.interaction.last_failure ==
           RC_INTERACTION_FAIL_INVALID_TARGET);

    rc_world_destroy(world);
}

int main(void) {
    test_take_helper_fires_pickup_event();
    test_take_helper_rejects_stale_generation();
    test_full_inventory_leaves_item_on_ground();
    test_default_take_handler_reports_full_inventory();
    printf("test_ground_items_phase2: default Take handler deepened.\n");
    return 0;
}
