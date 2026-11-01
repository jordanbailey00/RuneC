#include "api.h"
#include "combat.h"
#include "config.h"
#include "drops.h"
#include "events.h"
#include "items.h"
#include "npc.h"
#include "world_state.h"
#include "world_test_fixture.h"
#include "../rc-content/combat/ammunition.h"
#include "../rc-content/combat/magic.h"

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

static RcWorld *new_world(void) {
    RcWorldConfig cfg = rc_preset_base_only();
    cfg.subsystems = RC_SUB_COMBAT | RC_SUB_LOOT | RC_SUB_INVENTORY | RC_SUB_EQUIPMENT;
    cfg.items_path = RC_TEST_SOURCE_DIR "/data/defs/items.bin";
    cfg.npc_defs_path = RC_TEST_SOURCE_DIR "/data/defs/npc_defs.bin";
    cfg.spells_path = RC_TEST_SOURCE_DIR "/data/defs/spells.bin";
    RcWorld *w = rc_world_create_config(&cfg);
    assert(w && "loot fixture must load the installed B237 definitions");
    w->player.x = w->player.y = 3208;
    rc_test_open_mapsquare(w, 3208, 3208, 0);
    return w;
}

typedef struct { int calls; uint32_t quantity; int uid; } Receipt;
static void receipt(RcWorld *w, int event, const void *payload, void *ctx) {
    (void)w; (void)event;
    Receipt *r = ctx;
    const RcPayloadItemEvent *item = payload;
    r->calls++;
    r->quantity += item->quantity;
    r->uid = item->ground_uid;
}

static void test_atomic_grants_and_identity(void) {
    RcWorld *w = new_world();
    Receipt r = {0};
    assert(rc_event_subscribe(w, RC_EVT_DROP_GRANTED, receipt, &r) == 0);
    RcGroundPolicy policy = {100, 200, 0, 300};
    RcGroundGrant batch[] = {{995, 100000, 0}, {526, 129, 0}};
    uint32_t seed = w->rng_state;
    int uid = w->next_ground_item_uid;
    assert(rc_ground_grant(w, batch, 2, 3208, 3208, 0, 0, policy)
           == RC_GROUND_GRANT_CAPACITY);
    assert(!w->ground_item_count && !r.calls && w->next_ground_item_uid == uid);
    assert(w->rng_state == seed);
    batch[1].item_id = RC_MAX_ITEM_DEFS;
    assert(rc_ground_grant(w, batch, 2, 3208, 3208, 0, 0, policy)
           == RC_GROUND_GRANT_INVALID);
    assert(rc_ground_grant(w, batch, 1, 3208, 3208, 0, 0, policy) == RC_GROUND_GRANT_OK);
    assert(r.calls == 1 && r.quantity == 100000 && r.uid == w->ground_items[0].uid);
    RcGroundItem old = w->ground_items[0];
    assert(rc_event_subscribe(w, RC_EVT_ITEM_PICKED_UP, receipt, &r) == 0);
    assert(rc_player_take_ground_item(w, 0, old.uid, old.version) == RC_GROUND_TAKE_OK);
    assert(r.calls == 2 && r.quantity == 200000);
    int slot = rc_inv_find(w->player.inventory, 995);
    assert(slot >= 0 && w->player.inventory[slot].quantity == 100000);
    assert(rc_ground_grant(w, batch, 1, 3208, 3208, 0, 0, policy) == RC_GROUND_GRANT_OK);
    assert(w->ground_items[0].uid != old.uid);
    assert(rc_player_pickup_item_expected(w, 0, old.uid, old.version).code == RC_ITEM_RESULT_STALE);
    assert(w->ground_items[0].quantity == 100000);
    rc_world_destroy(w);
}

static void test_delays_visibility_and_respawn(void) {
    RcWorld *w = new_world();
    RcGroundGrant coins = {995, 100000, 0};
    RcGroundPolicy policy = {2, 5, 3, 5};
    assert(rc_ground_grant(w, &coins, 1, 3208, 3208, 0, 0, policy) == RC_GROUND_GRANT_OK);
    RcGroundItem *g = &w->ground_items[0];
    assert(!rc_ground_item_visible(g, 0));
    w->in_tick = true;
    assert(!rc_player_examine_ground_item(w, 0));
    w->in_tick = false;
    assert(rc_player_take_ground_item(w, 0, g->uid, g->version) == RC_GROUND_TAKE_INVALID);
    RcGroundItem dormant = *g;
    rc_ground_item_advance(g, 2);
    assert(g->arrival_timer == 1 && g->despawn_timer == 5);
    rc_ground_item_advance(g, 1);
    assert(rc_ground_item_visible(g, 0) && !rc_ground_item_visible(g, 1));
    rc_ground_item_advance(g, 2);
    assert(rc_ground_item_visible(g, 1) && g->original_owner_uid == 0);
    rc_ground_item_advance(&dormant, 5);
    assert(!memcmp(g, &dormant, sizeof(dormant)));
    rc_ground_item_advance(g, 3);
    assert(!g->active && !g->quantity);
    *g = (RcGroundItem){.item_id = 995, .quantity = 10, .spawn_quantity = 10,
        .uid = 100, .version = 1, .active = true, .static_spawn = true,
        .respawn_ticks = 3, .x = 3208, .y = 3208};
    assert(rc_player_take_ground_item(w, 0, 100, 1) == RC_GROUND_TAKE_OK);
    rc_ground_item_advance(g, 2);
    assert(!g->active && g->respawn_timer == 1);
    rc_ground_item_advance(g, 1);
    assert(g->active && g->quantity == 10 && g->version == 3);
    assert(rc_player_take_ground_item(w, 0, 100, 1) == RC_GROUND_TAKE_STALE);
    rc_world_destroy(w);
}

static void test_owner_merges_and_transaction_conflict(void) {
    RcWorld *w = new_world();
    RcGroundGrant coins = {995, 5, 0};
    RcGroundPolicy policy = {1, 10, 0, 10};
    assert(rc_ground_grant(w, &coins, 1, 3208, 3208, 0, 0, policy) == RC_GROUND_GRANT_OK);
    assert(rc_ground_grant(w, &coins, 1, 3208, 3208, 0, 1, policy) == RC_GROUND_GRANT_OK);
    assert(w->ground_item_count == 2);
    for (int i = 0; i < 2; i++) rc_ground_item_advance(&w->ground_items[i], 1);
    assert(rc_ground_grant(w, &coins, 1, 3208, 3208, 0, -1, policy) == RC_GROUND_GRANT_OK);
    assert(w->ground_item_count == 3 && w->ground_items[0].quantity == 5);
    RcItemTransaction tx;
    assert(rc_item_tx_begin(&tx, w).code == RC_ITEM_RESULT_OK);
    assert(rc_item_tx_add(&tx, 995, 1, 0).code == RC_ITEM_RESULT_OK);
    assert(rc_player_inventory_add(w, 526, 1, 0).code == RC_ITEM_RESULT_OK);
    RcGroundItem before[RC_MAX_GROUND_ITEMS];
    memcpy(before, w->ground_items, sizeof(before));
    assert(rc_item_tx_commit_ground(&tx, &coins, 1, 3208, 3208, 0, -1, policy)
           == RC_GROUND_GRANT_CONFLICT);
    assert(!memcmp(before, w->ground_items, sizeof(before)));
    rc_world_destroy(w);
}

static RcLootPolicy replacement(RcWorld *w, const RcNpc *npc,
    RcGroundGrant *out, int capacity, int *count, void *ctx) {
    (void)w; (void)npc;
    assert(capacity >= 1);
    (*(int *)ctx)++;
    out[0] = (RcGroundGrant){995, 100000, 0};
    *count = 1;
    return RC_LOOT_REPLACE;
}

static void hit(RcWorld *w, RcNpc *npc, int damage, int source) {
    npc->pending_hits[0] = (RcPendingHit){.damage = damage, .source_idx = source,
        .active = true, .accurate = damage > 0, .apply_tick = w->tick};
    npc->num_pending_hits = 1;
    rc_world_tick(w);
}

static void test_death_credit_and_detached_delivery(void) {
    RcWorld *w = new_world();
    int calls = 0;
    rc_set_npc_loot_hook(w, replacement, &calls);
    int idx = rc_npc_spawn(w, rc_npc_def_find(3014), 3210, 3208, 0);
    assert(idx >= 0);
    RcNpc *npc = &w->npcs[idx];
    npc->disable_wander = true;
    npc->current_hp = npc->spawn_hp = 2;
    npc->regen_timer = 100;
    hit(w, npc, 0, RC_HIT_SOURCE_PLAYER);
    assert(!npc->player_loot_credit);
    hit(w, npc, 1, RC_HIT_SOURCE_PLAYER);
    assert(npc->player_loot_credit && !calls);
    hit(w, npc, 1, RC_HIT_SOURCE_STATUS);
    assert(npc->is_dead && calls == 1 && w->last_loot.count == 1 && !w->last_loot.result);
    rc_npc_prepare_loot(w, npc);
    assert(calls == 1 && w->ground_item_count == 1);
    int death_x = w->last_loot.x, death_y = w->last_loot.y;
    assert(!rc_ground_item_visible(&w->ground_items[0], 0));
    assert(rc_npc_remove(w, npc->uid));
    for (int i = 0; i < 3; i++) rc_world_tick(w);
    assert(rc_ground_item_visible(&w->ground_items[0], 0));
    assert(w->ground_items[0].x == death_x && w->ground_items[0].y == death_y);
    assert(w->ground_items[0].quantity == 100000);
    assert(rc_world_reset(w));
    assert(!w->ground_item_count && !w->last_loot.count && !w->npc_loot_hook);
    rc_world_destroy(w);
}

static void test_rejected_death_has_receipt_without_grants_or_retry(void) {
    RcWorldConfig cfg = rc_preset_base_only();
    cfg.subsystems = RC_SUB_COMBAT | RC_SUB_LOOT;
    cfg.items_path = RC_TEST_SOURCE_DIR "/data/defs/items.bin";
    cfg.npc_defs_path = RC_TEST_SOURCE_DIR "/data/defs/npc_defs.bin";
    cfg.drops_path = RC_TEST_SOURCE_DIR "/data/defs/drops.bin";
    RcWorld *w = rc_world_create_config(&cfg);
    assert(w);
    w->player.x = w->player.y = 3208;
    rc_test_open_mapsquare(w, 3208, 3208, 0);
    Receipt events = {0};
    assert(rc_event_subscribe(w, RC_EVT_DROP_GRANTED, receipt, &events) == 0);
    const int ids[] = {414, 3106, 469, 3};  // Unpublished, both, outdated, missing binding.
    for (int i = 0; i < 4; i++) {
        int idx = rc_npc_spawn(w, rc_npc_def_find(ids[i]), w->player.x + 2 * i, w->player.y, 0);
        assert(idx >= 0);
        RcNpc *npc = &w->npcs[idx];
        npc->is_dead = true;
        npc->player_loot_credit = true;
        uint32_t rng = w->rng_state;
        rc_npc_prepare_loot(w, npc);
        assert(npc->loot_prepared && w->last_loot.result == (ids[i] == 3
            ? RC_LOOT_ROLL_NO_TABLE : RC_LOOT_ROLL_UNVERIFIED));
        assert(w->last_loot.npc_def_id == ids[i] && w->last_loot.npc_uid == npc->uid);
        assert(!w->last_loot.count && !w->ground_item_count && !events.calls);
        assert(w->rng_state == rng);
        RcLootReceipt result = w->last_loot;
        rc_npc_prepare_loot(w, npc);
        assert(!memcmp(&result, &w->last_loot, sizeof(result)) && !events.calls);
    }
    assert(rc_world_reset(w));
    assert(!w->last_loot.result && !w->ground_item_count);
    rc_world_destroy(w);
}

static void test_recovery_without_duplication(void) {
    RcWorld *w = new_world();
    const char *failure = NULL;
    int ground_count = 0;
    w->player.equipment[EQUIP_AMMO] = (RcInvSlot){892, 1000};
    for (int i = 0; i < 1000; i++) {
        w->ground_item_count = 0;
        memset(w->ground_items, 0, sizeof(w->ground_items));
        assert(rc_content_consume_ammunition(w, EQUIP_AMMO, 1, 3210, 3208, 0, 3, &failure));
        if (w->ground_item_count) {
            ground_count++;
            assert(w->ground_items[0].quantity == 1 && w->ground_items[0].arrival_timer == 3);
        }
    }
    assert(ground_count > 740 && ground_count < 860);
    assert(w->player.equipment[EQUIP_AMMO].quantity == 0);
    rc_world_destroy(w);
}

static void test_lifetime_policy_and_failed_player_drop(void) {
    RcWorld *w = new_world();
    RcGroundGrant coins = {995, 5, 0}, untradeable = {617, 1, 0};
    assert(!rc_item_def_get(untradeable.item_id)->tradeable);
    RcGroundPolicy npc_policy = {100, 200, 0, 300};
    assert(rc_ground_grant(w, &coins, 1, 3208, 3208, 0, 0, npc_policy) == RC_GROUND_GRANT_OK);
    assert(rc_ground_grant(w, &untradeable, 1, 3208, 3208, 0, 0, npc_policy) == RC_GROUND_GRANT_OK);
    assert(w->ground_items[0].despawn_timer == 200);
    assert(w->ground_items[1].despawn_timer == 300 && !w->ground_items[1].reveal_timer);
    assert(rc_player_inventory_add(w, 995, 5, 0).code == RC_ITEM_RESULT_OK);
    int slot = rc_inv_find(w->player.inventory, 995);
    assert(rc_player_drop_item_expected(w, slot, UINT32_MAX).code == RC_ITEM_RESULT_OK);
    assert(w->ground_item_count == 3 && w->ground_items[2].despawn_timer == 300);
    assert(w->ground_items[0].quantity == 5 && "different source lifetimes must not merge");

    assert(rc_ground_item_spawn(w, 526, 125, 3208, 3208, 0, -1));
    assert(rc_player_inventory_add(w, 526, 1, 42).code == RC_ITEM_RESULT_OK);
    slot = rc_inv_find(w->player.inventory, 526);
    RcInvSlot before = w->player.inventory[slot];
    assert(rc_player_drop_item_expected(w, slot, before.generation).code == RC_ITEM_RESULT_CAPACITY);
    assert(!memcmp(&before, &w->player.inventory[slot], sizeof(before)));
    assert(w->ground_item_count == 128);
    rc_world_destroy(w);
}

static void test_delayed_delivery_through_dormancy(void) {
    RcWorld *w = new_world();
    Receipt r = {0};
    assert(rc_event_subscribe(w, RC_EVT_DROP_GRANTED, receipt, &r) == 0);
    RcGroundGrant coins = {995, 5, 0};
    RcGroundPolicy policy = {100, 200, 3, 300};
    assert(rc_ground_grant(w, &coins, 1, 3208, 3208, 0, 0, policy) == RC_GROUND_GRANT_OK);
    int uid = w->ground_items[0].uid;
    assert(rc_world_state_save_ground_items(w, 3300, 3300, 3350, 3350, 0, 0) == 1);
    assert(w->dormant_ground_item_count == 1 && !w->ground_items[0].active);
    w->tick += 10;
    assert(rc_world_state_restore_ground_items(w, 3200, 3200, 3250, 3250, 0, 0) == 1);
    assert(!w->dormant_ground_item_count && r.calls == 1);
    const RcGroundItem *g = &w->ground_items[0];
    assert(g->uid == uid && rc_ground_item_visible(g, 0));
    assert(g->quantity == 5 && g->despawn_timer == 193 && g->reveal_timer == 93);
    rc_world_destroy(w);
}

static void test_smoke_credit_and_recovery_rejection(void) {
    RcWorld *w = new_world();
    int idx = rc_npc_spawn(w, rc_npc_def_find(3014), 3210, 3208, 0);
    assert(idx >= 0);
    RcNpc *npc = &w->npcs[idx];
    int spell = rc_spell_find("Smoke Rush");
    assert(spell >= 0);
    RcPendingHit hit = {.accurate = true, .attack_style = COMBAT_MAGIC,
                        .spell_key = spell + 1};
    for (int i = 0; i < 200 && !npc->toxin_severity; i++)
        rc_content_magic_hit(w, npc, &hit, 0);
    assert(npc->toxin_severity == 10 && npc->player_loot_credit);

    const char *failure = NULL;
    assert(!rc_content_consume_ammunition(w, -1, 1, 3210, 3208, 0, 3, &failure));
    assert(failure);
    assert(!rc_content_consume_ammunition(w, EQUIP_AMMO, 1, 3210, 3208, 0, 3, &failure));
    assert(failure);
    w->player.equipment[EQUIP_AMMO] = (RcInvSlot){892, 10};
    assert(rc_ground_item_spawn(w, 526, 128, 3210, 3208, 0, -1));
    int rejected = 0;
    for (int i = 0; i < 10; i++) {
        RcInvSlot before = w->player.equipment[EQUIP_AMMO];
        uint32_t rng = w->rng_state;
        if (!rc_content_consume_ammunition(w, EQUIP_AMMO, 1, 3210, 3208, 0, 3, &failure)) {
            assert(failure && !memcmp(&before, &w->player.equipment[EQUIP_AMMO], sizeof(before)));
            assert(w->rng_state == rng && w->ground_item_count == 128);
            rejected = 1;
            break;
        }
    }
    assert(rejected && "recoverable ammunition must report full-tile failure without payment");
    rc_world_destroy(w);
}

int main(void) {
    test_rejected_death_has_receipt_without_grants_or_retry();
    test_atomic_grants_and_identity();
    test_delays_visibility_and_respawn();
    test_owner_merges_and_transaction_conflict();
    test_death_credit_and_detached_delivery();
    test_recovery_without_duplication();
    test_lifetime_policy_and_failed_player_drop();
    test_delayed_delivery_through_dormancy();
    test_smoke_credit_and_recovery_rejection();
    puts("loot lifecycle: atomic grants, full receipts, timing, identity, credit and recovery passed");
    return 0;
}
