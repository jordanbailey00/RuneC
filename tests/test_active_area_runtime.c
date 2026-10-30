#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "api.h"
#include "collision.h"
#include "config.h"
#include "encounter.h"
#include "npc.h"
#include "pathfinding.h"

#define NPC_PATH RC_TEST_SOURCE_DIR "/data/defs/npc_defs.bin"
#define ODEF_PATH RC_TEST_SOURCE_DIR "/data/defs/object_defs.bin"
#define OBHV_PATH RC_TEST_SOURCE_DIR "/data/defs/object_behaviors.bin"
#define SPAWN_PATH \
    RC_TEST_SOURCE_DIR "/data/spawns/world.npc-spawns.indexed.bin"
#define CTPI_PATH \
    RC_TEST_SOURCE_DIR "/data/regions/world.collision-tiles.indexed.bin"
#define OPLI_PATH \
    RC_TEST_SOURCE_DIR "/data/regions/world.object-placements.indexed.bin"
#define STATIC_GROUND_ITEM_TEST_PATH "/tmp/runec_static_ground_items_test.bin"

enum {
    GSPI_TEST_MAGIC = 0x49505347,
    GSPI_TEST_VERSION = 1,
    GSPI_TEST_RECORD_SIZE = 26,
    GSPI_TEST_MAPSQUARES = 65536,
};

static void write_u32(FILE *f, uint32_t v) {
    assert(fwrite(&v, sizeof(v), 1, f) == 1);
}

static void write_i32(FILE *f, int32_t v) {
    assert(fwrite(&v, sizeof(v), 1, f) == 1);
}

static void write_u8(FILE *f, uint8_t v) {
    assert(fwrite(&v, sizeof(v), 1, f) == 1);
}

static void write_ground_item_row(FILE *f, uint32_t source_order,
                                  uint32_t item_id,
                                  uint32_t quantity, int32_t x, int32_t y,
                                  uint8_t plane) {
    write_u32(f, source_order);
    write_u32(f, item_id);
    write_u32(f, quantity);
    write_i32(f, x);
    write_i32(f, y);
    write_u8(f, plane);
    write_u8(f, 0);
    write_u32(f, 100);
}

static void write_static_ground_item_fixture(void) {
    FILE *f = fopen(STATIC_GROUND_ITEM_TEST_PATH, "wb");
    assert(f != NULL);
    write_u32(f, GSPI_TEST_MAGIC);
    write_u32(f, GSPI_TEST_VERSION);
    write_u32(f, 4);
    write_u32(f, GSPI_TEST_RECORD_SIZE);
    write_u32(f, 2);
    write_u32(f, 3);
    write_u32(f, 1);
    write_u32(f, 0);
    write_u32(f, 0);

    uint32_t first_row = 0;
    for (uint32_t mapsquare = 0; mapsquare < GSPI_TEST_MAPSQUARES;
         mapsquare++) {
        uint32_t count = 0;
        if (mapsquare == ((50u << 8) | 53u)) count = 3;
        if (mapsquare == ((62u << 8) | 62u)) count = 1;
        write_u32(f, first_row);
        write_u32(f, count);
        first_row += count;
    }
    assert(first_row == 4);
    write_ground_item_row(f, 0, 995, 100, 3213, 3428, 0);
    write_ground_item_row(f, 1, 995, 50, 3213, 3428, 0);
    write_ground_item_row(f, 2, 995, 25, 3214, 3428, 1);
    write_ground_item_row(f, 3, 995, 10, 4000, 4000, 0);
    assert(fclose(f) == 0);
}

static int active_ground_items(const RcWorld *world, int static_only) {
    int count = 0;
    for (int i = 0; i < world->ground_item_count; i++) {
        const RcGroundItem *g = &world->ground_items[i];
        if (g->active && (!static_only || g->static_spawn))
            count++;
    }
    return count;
}

static int active_encounter_for(const RcWorld *world, int npc_uid) {
    for (int i = 0; i < RC_ENC_MAX_ACTIVE; i++) {
        if (world->encounter.active[i].active
                && world->encounter.active[i].boss_id == npc_uid) {
            return i;
        }
    }
    return -1;
}

typedef struct {
    int target_uid;
    int total;
    int target_seen;
} RemovedEvents;

static void count_removed_npc(RcWorld *world, int event,
                              const void *payload, void *ctx) {
    (void)world;
    assert(event == RC_EVT_NPC_REMOVED);
    assert(payload != NULL);
    RemovedEvents *events = ctx;
    const RcPayloadNpcEvent *npc = payload;
    events->total++;
    if ((int)npc->npc_id == events->target_uid) events->target_seen++;
}

int main(void) {
    g_npc_def_count = 0;
    g_rc_collision_region_count = 0;
    write_static_ground_item_fixture();

    RcWorldConfig cfg = rc_preset_base_only();
    cfg.npc_capacity = RC_WORLD_NPC_CAPACITY_SIM;
    cfg.subsystems = RC_SUB_COMBAT | RC_SUB_REGIONS | RC_SUB_LOOT
                   | RC_SUB_OBJECTS | RC_SUB_ENCOUNTER;
    cfg.npc_defs_path = NPC_PATH;
    cfg.items_path = RC_TEST_SOURCE_DIR "/data/defs/items.bin";
    cfg.spawns_path = SPAWN_PATH;
    cfg.ground_item_spawns_path = STATIC_GROUND_ITEM_TEST_PATH;
    cfg.object_defs_path = ODEF_PATH;
    cfg.object_behaviors_path = OBHV_PATH;
    cfg.collision_tiles_path = CTPI_PATH;
    cfg.object_placements_path = OPLI_PATH;
    RcWorld *world = rc_world_create_config(&cfg);
    assert(world != NULL);
    assert(g_npc_def_count > 10000);
    assert(rc_collision_is_loaded());
    assert(rc_world_get_streaming_config(NULL) == NULL);

    const RcWorldStreamingConfig *streaming =
        rc_world_get_streaming_config(world);
    assert(streaming != NULL);
    assert(streaming->active_radius_regions == 2);
    assert(streaming->max_cached_regions == 64);
    RcWorldStreamingTelemetry telemetry;
    assert(rc_world_get_streaming_telemetry(NULL, &telemetry) == -1);
    assert(rc_world_get_streaming_telemetry(world, NULL) == -1);
    assert(rc_world_get_streaming_telemetry(world, &telemetry) == 1);
    assert(telemetry.active_area_load_count == 0);

    RcActiveAreaRequest bad = {0};
    assert(rc_world_activate_area(world, &bad, NULL) == -1);
    bad.origin_x = RC_WORLD_MAX;
    bad.origin_y = 0;
    bad.width = 2;
    bad.height = 1;
    bad.min_plane = 0;
    bad.max_plane = 0;
    assert(rc_world_activate_area(world, &bad, NULL) == -1);
    bad.origin_x = 0;
    bad.width = INT_MAX;
    assert(rc_world_activate_area(world, &bad, NULL) == -1);

    RcActiveAreaRequest req = {
        .origin_x = 3072,
        .origin_y = 3264,
        .width = 320,
        .height = 320,
        .min_plane = 0,
        .max_plane = RC_MAX_PLANES - 1,
    };
    RcActiveAreaStats stats;
    assert(rc_world_activate_area(world, &req, &stats) == 1);
    assert(stats.collision_regions > 0);
    assert(stats.collision_regions <= RC_MAX_REGIONS);
    assert(stats.collision_stats.total_rows == 6510902);
    assert(stats.collision_stats.occupied_pages == 2411);
    assert(stats.collision_stats.pages_requested
           == (uint32_t)stats.collision_regions);
    assert(stats.collision_stats.pages_loaded
           == stats.collision_stats.pages_requested);
    assert(stats.collision_stats.rows_loaded > 0);
    assert(stats.collision_stats.pages_resident
           == stats.collision_stats.pages_loaded);
    assert(stats.npc_stats.total_rows == 24110);
    assert(stats.npc_stats.matched_filter == 840);
    assert(stats.spawned_npcs == 837);
    assert(stats.streaming.active_area_load_count == 1);
    assert(stats.streaming.active_area_load_ms >= 0.0);
    assert(stats.streaming.backend_page_load_ms >= 0.0);
    assert(stats.npc_stats.pages_loaded > 0);
    assert(stats.npc_stats.pages_loaded <= stats.collision_regions);
    assert(stats.npc_stats.rows_loaded < stats.npc_stats.total_rows);
    assert(stats.object_placement_stats.total_rows > 4700000);
    assert(stats.object_placement_stats.pages_requested > 0);
    assert(stats.object_placement_stats.pages_loaded
           == stats.object_placement_stats.pages_requested);
    assert(stats.object_placement_stats.rows_loaded > 0);
    assert(stats.object_placement_stats.pages_resident
           == stats.object_placement_stats.pages_loaded);
    assert(stats.streaming.backend_pages_loaded
           == (int)stats.collision_stats.pages_loaded
            + stats.npc_stats.pages_loaded
            + stats.ground_item_stats.pages_loaded
            + (int)stats.object_placement_stats.pages_loaded);
    assert(stats.streaming.active_npcs == 837);
    assert(stats.streaming.active_ground_items == 3);
    assert(world->npc_count == 837);
    assert(world->active_area.active);
    const RcActiveArea *active = rc_world_get_active_area(world);
    assert(active == &world->active_area);
    assert(world->active_area.origin_x == req.origin_x);
    assert(world->active_area.origin_y == req.origin_y);
    assert(world->active_area.width == req.width);
    assert(world->active_area.height == req.height);
    assert(world->active_area.min_plane == 0);
    assert(world->active_area.max_plane == RC_MAX_PLANES - 1);
    assert(world->active_area.components
           == (RC_ACTIVE_AREA_COMPONENT_COLLISION
             | RC_ACTIVE_AREA_COMPONENT_NPCS
             | RC_ACTIVE_AREA_COMPONENT_GROUND_ITEMS
             | RC_ACTIVE_AREA_COMPONENT_OBJECT_CACHE));
    assert(rc_get_flags(&world->map, 3213, 3428, 0)
           == rc_collision_flags_at(3213, 3428, 0, NULL));

    uint32_t first_gen = world->active_area.generation;
    static RcEncounterSpec area_encounter;
    memset(&area_encounter, 0, sizeof(area_encounter));
    strcpy(area_encounter.slug, "area_test");
    area_encounter.support = RC_ENC_SUPPORT_FULL;
    area_encounter.form_count = 1;
    area_encounter.forms[0].npc_id = 2215;
    area_encounter.forms[0].binds_on_spawn = true;
    area_encounter.attack_count = 1;
    area_encounter.attacks[0].style = COMBAT_MELEE_CRUSH;
    area_encounter.attacks[0].max_hit = 1;
    area_encounter.attacks[0].weight_adjacent = 1;
    area_encounter.phase_count = 1;
    area_encounter.phases[0].allowed_attack_mask = 1;
    area_encounter.phases[0].player_targetable = true;
    assert(rc_encounter_use_specs(world, &area_encounter, 1) == 0);
    RcNpcEnsureResult ensured;
    assert(rc_world_find_npc_near(NULL, 2215, 2872, 5358, 2, 8) == -1);
    assert(rc_world_ensure_npc_near(NULL, 2215, 2872, 5358, 2, 8,
                                    &ensured) == -1);
    assert(ensured.index == -1 && ensured.uid == -1 && ensured.spawned == 0);
    assert(rc_world_find_npc_near(world, 2215, 2872, 5358, 2, 8) == -1);
    assert(rc_world_ensure_npc_near(world, 2215, 2872, 5358, 2, 8,
                                    &ensured) >= 0);
    assert(ensured.index >= 0);
    assert(ensured.uid >= 0);
    assert(ensured.spawned == 1);
    assert(world->npc_count == 838);
    assert(active_encounter_for(world, ensured.uid) >= 0);
    int encounter_effect = rc_encounter_add_effect(
        world, active_encounter_for(world, ensured.uid),
        RC_ENC_EFFECT_ROOM_ATTACK, 2872, 5358, 2, 2872, 5358, 20,
        COMBAT_MAGIC, 1, "test", "");
    assert(encounter_effect >= 0);
    assert(rc_world_find_npc_near(world, 2215, 2872, 5358, 2, 8)
           == ensured.index);
    RcNpcEnsureResult reused;
    assert(rc_world_ensure_npc_near(world, 2215, 2872, 5358, 2, 8,
                                    &reused) >= 0);
    assert(reused.index == ensured.index);
    assert(reused.uid == ensured.uid);
    assert(reused.spawned == 0);
    assert(world->npc_count == 838);
    assert(rc_world_ensure_npc_near(world, -1, 2872, 5358, 2, 8,
                                    &reused) == -1);
    assert(reused.index == -1 && reused.uid == -1 && reused.spawned == 0);
    RemovedEvents removed_events = {.target_uid = ensured.uid};
    assert(rc_event_subscribe(world, RC_EVT_NPC_REMOVED,
                              count_removed_npc, &removed_events) == 0);
    world->player.attack_target = ensured.uid;
    world->player.attack_target_def_id = 2215;
    world->player.interact_type = RC_INTERACT_NPC;
    world->player.interact_target = ensured.uid;
    assert(rc_world_activate_area(world, &req, &stats) == 1);
    assert(stats.unchanged);
    assert(world->active_area.generation == first_gen);
    assert(world->npc_count == 838);
    assert(stats.streaming.active_area_load_count == 1);
    assert(stats.streaming.active_area_load_total_ms
           >= stats.streaming.active_area_load_ms);
    assert(stats.object_placement_stats.pages_requested == 0);
    assert(stats.collision_stats.pages_requested == 0);

    RcActiveAreaRequest shifted = req;
    shifted.origin_x = 3200;
    shifted.origin_y = 3392;
    shifted.width = 64;
    shifted.height = 64;
    assert(rc_world_activate_area(world, &shifted, &stats) == 1);
    assert(!stats.unchanged);
    assert(stats.spawned_npcs > 0);
    assert(world->active_area.origin_x == 3200);
    assert(world->active_area.width == 64);
    assert(removed_events.total > 0);
    assert(removed_events.target_seen == 1);
    assert(active_encounter_for(world, ensured.uid) == -1);
    assert(!world->encounter_effects[encounter_effect].active);
    assert(world->player.attack_target == -1);
    assert(world->player.interact_target == -1);
    assert(world->active_area.components
           == (RC_ACTIVE_AREA_COMPONENT_COLLISION
             | RC_ACTIVE_AREA_COMPONENT_NPCS
             | RC_ACTIVE_AREA_COMPONENT_GROUND_ITEMS
             | RC_ACTIVE_AREA_COMPONENT_OBJECT_CACHE));

    assert(rc_world_activate_area(world, &req, &stats) == 1);
    assert(stats.spawned_npcs == 837);

    RcGroundItemSpawnLoadStats empty_ground_stats;
    assert(rc_load_ground_item_spawns_rect_stats(
        world, STATIC_GROUND_ITEM_TEST_PATH,
        -128, -128, -1, -1, 0, RC_MAX_PLANES - 1,
        &empty_ground_stats) == 0);
    assert(empty_ground_stats.total_rows == 4);
    assert(empty_ground_stats.pages_loaded == 0);
    assert(empty_ground_stats.rows_loaded == 0);

    RcActiveAreaRequest ground_items = req;
    ground_items.max_plane = 0;
    assert(rc_world_activate_area(world, &ground_items, &stats) == 1);
    assert(stats.ground_item_stats.total_rows == 4);
    assert(stats.ground_item_stats.pages_loaded == 1);
    assert(stats.ground_item_stats.rows_loaded == 3);
    assert(stats.ground_item_stats.matched_filter == 2);
    assert(stats.ground_item_stats.skipped_plane == 1);
    assert(stats.ground_item_stats.skipped_filtered == 1);
    assert(stats.ground_item_stats.spawned == 2);
    assert(stats.spawned_ground_items == 2);
    assert(stats.streaming.active_ground_items == 2);
    assert(active_ground_items(world, 1) == 2);
    assert(world->ground_items[0].active);
    assert(world->ground_items[0].static_spawn);
    assert(world->ground_items[0].spawn_key != 0);
    assert(world->ground_items[0].item_id == 995);
    assert(world->ground_items[0].quantity == 100);
    assert(world->ground_items[0].spawn_quantity == 100);
    assert(world->ground_items[1].quantity == 50);
    assert(world->ground_items[0].spawn_key != world->ground_items[1].spawn_key);

    assert(rc_ground_item_spawn(world, 995, 1, 3213, 3428, 0,
                                RC_GROUND_OWNER_NONE));
    assert(active_ground_items(world, 0) == 3);
    assert(rc_world_activate_area(world, &ground_items, &stats) == 1);
    assert(stats.unchanged);
    assert(active_ground_items(world, 1) == 2);
    assert(active_ground_items(world, 0) == 3);
    assert(!world->ground_items[2].static_spawn);
    assert(rc_world_get_streaming_telemetry(world, &telemetry) == 1);
    assert(telemetry.active_ground_items == 3);
    char valid_ground_path[sizeof(world->ground_item_spawns_path)];
    memcpy(valid_ground_path, world->ground_item_spawns_path,
           sizeof(valid_ground_path));
    FILE *bad_ground_items = fopen("/tmp/runec_bad_ground_items.bin", "wb");
    assert(bad_ground_items != NULL);
    write_u32(bad_ground_items, 0);
    assert(fclose(bad_ground_items) == 0);
    snprintf(world->ground_item_spawns_path,
             sizeof(world->ground_item_spawns_path), "%s",
             "/tmp/runec_bad_ground_items.bin");
    uint32_t rollback_generation = world->active_area.generation;
    int rollback_npc_count = world->npc_count;
    int rollback_ground_count = world->ground_item_count;
    RcWorldMap rollback_map = world->map;
    RcActiveAreaRequest failed = ground_items;
    failed.origin_x = 3136;
    assert(rc_world_activate_area(world, &failed, &stats) == -1);
    assert(world->active_area.generation == rollback_generation);
    assert(world->npc_count == rollback_npc_count);
    assert(world->ground_item_count == rollback_ground_count);
    assert(memcmp(&world->map, &rollback_map, sizeof(rollback_map)) == 0);
    memcpy(world->ground_item_spawns_path, valid_ground_path,
           sizeof(world->ground_item_spawns_path));
    assert(remove("/tmp/runec_bad_ground_items.bin") == 0);

    RcActiveAreaRequest stronghold = req;
    stronghold.origin_x = 1792;
    stronghold.origin_y = 5120;
    stronghold.width = 192;
    stronghold.height = 192;
    assert(rc_world_activate_area(world, &stronghold, &stats) == 1);
    assert(stats.npc_stats.matched_filter == 150);
    assert(stats.npc_stats.skipped_instance == 40);
    assert(stats.spawned_npcs == 110);

    stronghold.options = RC_ACTIVE_AREA_INCLUDE_INSTANCE_NPCS;
    assert(rc_world_activate_area(world, &stronghold, &stats) == 1);
    assert(stats.npc_stats.matched_filter == 150);
    assert(stats.npc_stats.skipped_instance == 0);
    assert(stats.spawned_npcs == 150);

    assert(rc_world_relocate_player(world, 3184, 3440, 0));
    assert(world->player.x == 3184 && world->player.y == 3440);
    assert(world->active_area.origin_x == 3008);
    assert(world->active_area.origin_y == 3264);

    // Script movement and delays (an agility obstacle's walk): a move of up
    // to two tiles keeps the delay, a farther or invalid one is refused, and
    // the delayed player's queue refuses walks until the delay ends.
    assert(!rc_world_delay_player(world, 0));
    assert(rc_world_delay_player(world, 3));
    assert(rc_world_force_move_player(world, 3186, 3441));
    assert(world->player.x == 3186 && world->player.y == 3441);
    assert(world->player.prev_x == 3184 && world->player.prev_y == 3440);
    assert(!rc_world_force_move_player(world, 3189, 3441));
    assert(!rc_world_force_move_player(world, 3186, -1));
    assert(world->player.x == 3186 && world->player.y == 3441);
    assert(!rc_player_walk_to(world, 3180, 3441));
    for (int i = 0; i < 3; i++) rc_world_tick(world);
    assert(world->player.x == 3186 && world->player.y == 3441);
    assert(rc_player_walk_to(world, 3185, 3441));

    rc_world_destroy(world);

    cfg.npc_capacity = RC_WORLD_NPC_CAPACITY_BASE;
    RcWorld *capacity_limited = rc_world_create_config(&cfg);
    assert(capacity_limited != NULL);
    int original_x = capacity_limited->player.x;
    int original_y = capacity_limited->player.y;
    assert(rc_world_activate_area(capacity_limited, &req, &stats) == -1);
    assert(!capacity_limited->active_area.active);
    assert(capacity_limited->map.region_count == 0);
    assert(capacity_limited->npc_count == 0);
    assert(capacity_limited->ground_item_count == 0);
    assert(!rc_world_relocate_player(capacity_limited, 3184, 3440, 0));
    assert(capacity_limited->player.x == original_x);
    assert(capacity_limited->player.y == original_y);
    capacity_limited->player.x = 3199;
    capacity_limited->player.y = 3428;
    capacity_limited->player.plane = 0;
    capacity_limited->player.route_x[0] = 3200;
    capacity_limited->player.route_y[0] = 3428;
    capacity_limited->player.route_len = 1;
    capacity_limited->player.route_idx = 0;
    rc_world_tick(capacity_limited);
    assert(capacity_limited->player.x == 3199);
    assert(capacity_limited->player.y == 3428);
    assert(capacity_limited->player.route_len == 0);
    assert(capacity_limited->player.route_idx == 0);
    // A script move into a mapsquare that cannot load is refused too.
    assert(!rc_world_force_move_player(capacity_limited, 3200, 3428));
    assert(capacity_limited->player.x == 3199);
    rc_world_destroy(capacity_limited);
    assert(remove(STATIC_GROUND_ITEM_TEST_PATH) == 0);
    printf("test_active_area_runtime: core-owned active area loaded.\n");
    return 0;
}
