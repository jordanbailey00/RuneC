#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "api.h"
#include "config.h"
#include "events.h"
#include "items.h"

#define ITEM_PATH RC_TEST_SOURCE_DIR "/data/defs/items.bin"

typedef struct {
    int count;
    RcPayloadItemEvent last;
} DropSpy;

static void on_drop(RcWorld *world, int evt, const void *payload,
                    void *ctx) {
    (void)world;
    assert(evt == RC_EVT_ITEM_DROPPED);
    DropSpy *spy = (DropSpy *)ctx;
    spy->count++;
    memcpy(&spy->last, payload, sizeof(spy->last));
}

static RcWorld *phase4_world(void) {
    RcWorldConfig cfg = rc_preset_base_only();
    cfg.subsystems = RC_SUB_INVENTORY | RC_SUB_LOOT;
    cfg.items_path = ITEM_PATH;
    RcWorld *world = rc_world_create_config(&cfg);
    assert(world != NULL);
    return world;
}

static int first_untradeable_item(void) {
    for (int i = 0; i < RC_MAX_ITEM_DEFS; i++) {
        if (g_item_defs[i].loaded && !g_item_defs[i].tradeable) {
            return g_item_defs[i].id;
        }
    }
    assert(0 && "expected at least one untradeable item definition");
    return -1;
}

static void test_drop_event_and_inventory_mutation(void) {
    RcWorld *world = phase4_world();
    DropSpy spy = {0};
    assert(rc_event_subscribe(world, RC_EVT_ITEM_DROPPED, on_drop,
                              &spy) == 0);

    int slot = rc_inv_add(world->player.inventory, 995, 1000);
    assert(slot >= 0);
    rc_player_drop_item(world, slot);

    assert(spy.count == 1);
    assert(spy.last.item_id == 995);
    assert(spy.last.quantity == 1000);
    assert(spy.last.slot == slot);
    assert(world->player.inventory[slot].item_id == -1);
    assert(world->ground_item_count == 1);
    assert(world->ground_items[0].item_id == 995);
    assert(world->ground_items[0].quantity == 1000);
    assert(world->ground_items[0].owner_uid == RC_GROUND_OWNER_LOCAL_PLAYER);
    assert(world->ground_items[0].visibility == RC_GROUND_VIS_PRIVATE);

    rc_world_destroy(world);
}

static void test_tradeable_drop_reveals_then_despawns(void) {
    RcWorld *world = phase4_world();
    int slot = rc_inv_add(world->player.inventory, 995, 25);
    assert(slot >= 0);
    rc_player_drop_item(world, slot);

    assert(world->ground_items[0].visibility == RC_GROUND_VIS_PRIVATE);
    assert(world->ground_items[0].reveal_timer == 100);
    for (int i = 0; i < 100; i++) rc_world_tick(world);
    assert(world->ground_items[0].active);
    assert(world->ground_items[0].visibility == RC_GROUND_VIS_PUBLIC);
    assert(world->ground_items[0].owner_uid == RC_GROUND_OWNER_NONE);
    for (int i = 0; i < 200; i++) rc_world_tick(world);
    assert(!world->ground_items[0].active);

    rc_world_destroy(world);
}

static void test_untradeable_drop_stays_private_until_despawn(void) {
    RcWorld *world = phase4_world();
    int item_id = first_untradeable_item();
    int slot = rc_inv_add(world->player.inventory, item_id, 1);
    assert(slot >= 0);
    rc_player_drop_item(world, slot);

    assert(world->ground_item_count == 1);
    assert(world->ground_items[0].item_id == item_id);
    assert(world->ground_items[0].visibility ==
           RC_GROUND_VIS_PRIVATE_PERMANENT);
    assert(world->ground_items[0].owner_uid == RC_GROUND_OWNER_LOCAL_PLAYER);
    assert(world->ground_items[0].reveal_timer == 0);
    for (int i = 0; i < 100; i++) rc_world_tick(world);
    assert(world->ground_items[0].active);
    assert(world->ground_items[0].visibility ==
           RC_GROUND_VIS_PRIVATE_PERMANENT);
    assert(world->ground_items[0].owner_uid == RC_GROUND_OWNER_LOCAL_PLAYER);
    for (int i = 0; i < 200; i++) rc_world_tick(world);
    assert(!world->ground_items[0].active);

    rc_world_destroy(world);
}

int main(void) {
    test_drop_event_and_inventory_mutation();
    test_tradeable_drop_reveals_then_despawns();
    test_untradeable_drop_stays_private_until_despawn();
    printf("test_ground_items_phase4: player drop cleanup covered.\n");
    return 0;
}
