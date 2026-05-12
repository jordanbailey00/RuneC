#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "api.h"
#include "combat.h"
#include "config.h"
#include "drops.h"
#include "items.h"
#include "npc.h"

#define ITEM_PATH RC_TEST_SOURCE_DIR "/data/defs/items.bin"
#define DROPS_PATH RC_TEST_SOURCE_DIR "/data/defs/drops.bin"
#define RDT_PATH RC_TEST_SOURCE_DIR "/data/defs/rdt.bin"
#define GDT_PATH RC_TEST_SOURCE_DIR "/data/defs/gdt.bin"
#define MRDT_PATH RC_TEST_SOURCE_DIR "/data/defs/mrdt.bin"

static RcWorld *phase3_world(void) {
    RcWorldConfig cfg = rc_preset_base_only();
    cfg.subsystems = RC_SUB_COMBAT | RC_SUB_INVENTORY | RC_SUB_LOOT;
    cfg.items_path = ITEM_PATH;
    cfg.drops_path = DROPS_PATH;
    cfg.rdt_path = RDT_PATH;
    cfg.gdt_path = GDT_PATH;
    cfg.mrdt_path = MRDT_PATH;
    RcWorld *world = rc_world_create_config(&cfg);
    assert(world != NULL);
    world->rng_state = 1;
    return world;
}

static void install_npc_def(int npc_id) {
    memset(g_npc_defs, 0, sizeof(g_npc_defs));
    g_npc_def_count = 1;
    g_npc_defs[0].id = npc_id;
    g_npc_defs[0].hitpoints = 1;
    g_npc_defs[0].size = 1;
    g_npc_defs[0].respawn_ticks = 25;
}

static void kill_spawned_npc(RcWorld *world, int npc_idx) {
    RcNpc *npc = &world->npcs[npc_idx];
    npc->current_hp = 1;
    npc->pending_hits[0] = (RcPendingHit){
        .damage = 1,
        .ticks_remaining = 0,
        .attack_style = COMBAT_MELEE_SLASH,
        .source_idx = -1,
        .active = true,
    };
    npc->num_pending_hits = 1;
    rc_world_tick(world);
}

static int active_items_at(const RcWorld *world, int x, int y, int plane) {
    int count = 0;
    for (int i = 0; i < world->ground_item_count; i++) {
        const RcGroundItem *g = &world->ground_items[i];
        if (g->active && g->x == x && g->y == y && g->plane == plane) {
            count++;
        }
    }
    return count;
}

static void test_roll_returns_obor_always_drops(void) {
    RcWorld *world = phase3_world();
    RcLootDrop drops[RC_MAX_LOOT_DROPS];
    int count = rc_roll_npc_loot(world, 7416, drops, RC_MAX_LOOT_DROPS);
    assert(count >= 2);
    assert(count < RC_MAX_LOOT_DROPS);
    assert(drops[0].kind == RC_DROP_ALWAYS);
    assert(drops[1].kind == RC_DROP_ALWAYS);
    assert(drops[0].item_id > 0 && drops[0].quantity > 0);
    assert(drops[1].item_id > 0 && drops[1].quantity > 0);
    rc_world_destroy(world);
}

static void test_roll_handles_variable_quantities(void) {
    RcWorld *world = phase3_world();
    int saw_range_drop = 0;
    for (int i = 0; i < 512 && !saw_range_drop; i++) {
        RcLootDrop drops[RC_MAX_LOOT_DROPS];
        int count = rc_roll_npc_loot(world, 1, drops, RC_MAX_LOOT_DROPS);
        for (int j = 0; j < count; j++) {
            if (drops[j].item_id == 555) {
                assert(drops[j].quantity >= 1);
                assert(drops[j].quantity <= 14);
                saw_range_drop = 1;
            }
        }
    }
    assert(saw_range_drop);
    rc_world_destroy(world);
}

static void test_npc_death_spawns_private_ground_loot(void) {
    RcWorld *world = phase3_world();
    install_npc_def(7416);
    int x = world->player.x + 1;
    int y = world->player.y;
    int idx = rc_npc_spawn(world, 0, x, y, world->player.plane);
    assert(idx >= 0);

    kill_spawned_npc(world, idx);
    assert(world->npcs[idx].is_dead);
    assert(active_items_at(world, x, y, world->player.plane) >= 2);
    for (int i = 0; i < world->ground_item_count; i++) {
        const RcGroundItem *g = &world->ground_items[i];
        if (!g->active) continue;
        assert(g->uid > 0);
        assert(g->version > 0);
        assert(g->owner_uid == RC_GROUND_OWNER_LOCAL_PLAYER);
        assert(g->original_owner_uid == RC_GROUND_OWNER_LOCAL_PLAYER);
        assert(g->visibility == RC_GROUND_VIS_PRIVATE ||
               g->visibility == RC_GROUND_VIS_PRIVATE_PERMANENT);
        assert(g->despawn_timer == 299);
    }
    rc_world_destroy(world);
}

static void test_npc_without_table_spawns_no_loot(void) {
    RcWorld *world = phase3_world();
    install_npc_def(999999);
    int idx = rc_npc_spawn(world, 0, world->player.x + 1, world->player.y,
                           world->player.plane);
    assert(idx >= 0);

    kill_spawned_npc(world, idx);
    assert(world->npcs[idx].is_dead);
    assert(world->ground_item_count == 0);
    rc_world_destroy(world);
}

int main(void) {
    test_roll_returns_obor_always_drops();
    test_roll_handles_variable_quantities();
    test_npc_death_spawns_private_ground_loot();
    test_npc_without_table_spawns_no_loot();
    printf("test_ground_items_phase3: NPC death loot uses drop DB.\n");
    return 0;
}
