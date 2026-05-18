#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "api.h"
#include "config.h"
#include "interaction.h"
#include "npc.h"
#include "objects.h"
#include "pathfinding.h"
#include "spells.h"

#define ODEF_PATH RC_TEST_SOURCE_DIR "/data/defs/object_defs.bin"
#define OPLC_PATH RC_TEST_SOURCE_DIR "/data/defs/object_placements.bin"
#define OBHV_PATH RC_TEST_SOURCE_DIR "/data/defs/object_behaviors.bin"
#define OTRP_PATH RC_TEST_SOURCE_DIR "/data/defs/object_transports.bin"
#define TRAV_PATH RC_TEST_SOURCE_DIR "/data/defs/traversal_edges.bin"
#define CTIL_PATH RC_TEST_SOURCE_DIR "/data/defs/collision_tiles.bin"

enum {
    TEST_NPC_ID = 990123,
    TEST_SOURCE_ITEM = 995,
};

static void init_single_region_map(RcWorld *world, int region_x,
                                   int region_y) {
    assert(world != NULL);
    world->map.region_count = 1;
    RcRegion *region = &world->map.regions[0];
    memset(region, 0, sizeof(*region));
    region->loaded = true;
    region->region_x = region_x;
    region->region_y = region_y;
}

static int object_at(int obj_id, int x, int y, int plane,
                     RcObjectPlacement *out) {
    RcObjectPlacement rows[16];
    int n = rc_object_placements_at(x, y, plane, rows, 16);
    for (int i = 0; i < n; i++) {
        if ((int)rows[i].obj_id != obj_id)
            continue;
        if (out) *out = rows[i];
        return 1;
    }
    return 0;
}

static RcWorld *make_world(uint32_t seed) {
    RcWorldConfig cfg = rc_preset_base_only();
    cfg.seed = seed;
    cfg.subsystems = RC_SUB_OBJECTS | RC_SUB_TRAVERSAL | RC_SUB_REGIONS
                   | RC_SUB_COMBAT | RC_SUB_INVENTORY;
    cfg.object_defs_path = ODEF_PATH;
    cfg.object_placements_path = OPLC_PATH;
    cfg.object_behaviors_path = OBHV_PATH;
    cfg.object_transports_path = OTRP_PATH;
    cfg.traversal_edges_path = TRAV_PATH;
    cfg.collision_tiles_path = CTIL_PATH;
    RcWorld *world = rc_world_create_config(&cfg);
    assert(world != NULL);
    return world;
}

static int add_test_npc_def(void) {
    memset(g_npc_defs, 0, sizeof(g_npc_defs));
    g_npc_def_count = 1;
    RcNpcDef *def = &g_npc_defs[0];
    def->id = TEST_NPC_ID;
    strcpy(def->name, "Headless Target");
    def->size = 1;
    def->hitpoints = 64;
    def->stats[1] = 1;
    def->stats[5] = 1;
    def->max_hit = 1;
    def->attack_speed = 4;
    strcpy(def->options[0], "Talk-to");
    strcpy(def->options[1], "Attack");
    return 0;
}

static void add_test_spell(void) {
    memset(g_rc_spell_defs, 0, sizeof(g_rc_spell_defs));
    g_rc_spell_count = 1;
    RcSpellDef *spell = &g_rc_spell_defs[0];
    strcpy(spell->name, "Headless Strike");
    spell->book = RC_SPELL_BOOK_STANDARD;
    spell->type = RC_SPELL_TYPE_COMBAT;
    spell->max_hit = 4;
    spell->loaded = 1;
}

int main(void) {
    RcWorld *world = make_world(123);
    init_single_region_map(world, 50, 53);
    add_test_npc_def();
    add_test_spell();

    world->player.x = 3200;
    world->player.y = 3392;
    world->player.plane = 0;
    rc_player_walk_to(world, 3202, 3392);
    assert(world->player.route_len > 0);
    rc_world_tick(world);
    assert(world->player.x != 3200 || world->player.y != 3392);

    RcObjectPlacement door;
    assert(object_at(11780, 3196, 3384, 0, &door));
    world->player.x = 3197;
    world->player.y = 3384;
    world->player.plane = 0;
    world->player.route_len = 0;
    world->player.route_idx = 0;
    assert(rc_player_interact_object_placement(
        world, 11780, 3196, 3384, 0, door.key, 0) == 1);
    rc_world_tick(world);
    RcObjectState door_state;
    assert(rc_world_object_active_state_by_key(world, door.key,
                                               &door_state) == 1);
    assert(door_state.flags & RC_OBJECT_STATE_OPEN);
    assert(door_state.placement_key == door.key);
    world->player.action_lock_timer = 0;
    rc_interaction_clear(&world->player);

    int npc_idx = rc_npc_spawn(world, 0, world->player.x + 1,
                               world->player.y, world->player.plane);
    assert(npc_idx >= 0);
    RcNpc *npc = &world->npcs[npc_idx];
    rc_player_interact_npc(world, npc->uid, 0);
    rc_world_tick(world);
    assert(world->player.interact_type == RC_INTERACT_NPC);
    assert(world->player.interact_target == npc->uid);

    rc_interaction_clear(&world->player);
    world->player.inventory[0] = (RcInvSlot){TEST_SOURCE_ITEM, 1};
    world->player.x = door_state.active_x;
    world->player.y = door_state.active_y + 1;
    world->player.plane = door_state.active_plane;
    assert(rc_player_use_inventory_item_on_object_placement(
        world, 0, door_state.active_obj_id, door_state.active_x,
        door_state.active_y, door_state.active_plane, door.key) == 1);
    assert(world->player.interaction.active);
    assert(world->player.interaction.source_item_id == TEST_SOURCE_ITEM);
    assert(world->player.interaction.target.placement_key == door.key);

    rc_interaction_clear(&world->player);
    assert(rc_player_cast_spell_on_object_placement(
        world, 0, door_state.active_obj_id, door_state.active_x,
        door_state.active_y, door_state.active_plane, door.key) == 1);
    assert(world->player.interaction.active);
    assert(world->player.interaction.source_spell_id == 0);
    assert(world->player.interaction.target.placement_key == door.key);

    rc_interaction_clear(&world->player);
    world->player.x = npc->x - 1;
    world->player.y = npc->y;
    assert(rc_player_cast_spell_on_npc(world, 0, npc->uid) == 1);
    assert(world->player.interaction.active);
    assert(world->player.interaction.source_spell_id == 0);
    rc_world_tick(world);
    assert(world->player.interact_type == RC_INTERACT_NPC_ATTACK);

    RcActiveAreaRequest area = {
        .origin_x = 3072,
        .origin_y = 3264,
        .width = 64,
        .height = 64,
        .min_plane = 0,
        .max_plane = 0,
        .flags = RC_ACTIVE_AREA_LOAD_COLLISION,
    };
    RcActiveAreaStats stats;
    assert(rc_world_activate_area(world, &area, &stats) == 1);
    assert(world->active_area.active);
    assert(stats.collision_regions > 0);

    rc_player_attack_npc(world, npc->uid);
    rc_world_tick(world);
    assert(world->player.interact_type == RC_INTERACT_NPC_ATTACK);

    rc_world_destroy(world);

    RcWorld *reloaded = make_world(124);
    assert(reloaded->object_state_count == 0);
    assert(rc_world_activate_area(reloaded, &area, &stats) == 1);
    assert(reloaded->active_area.generation == 1);
    rc_world_destroy(reloaded);

    printf("test_headless_action_runtime: viewer-equivalent headless actions passed.\n");
    return 0;
}
