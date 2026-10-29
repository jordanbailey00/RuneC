#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "api.h"
#include "config.h"
#include "object_runtime.h"
#include "objects.h"
#include "pathfinding.h"

#define ODEF_PATH RC_TEST_SOURCE_DIR "/data/defs/object_defs.bin"
#define OPLI_PATH \
    RC_TEST_SOURCE_DIR "/data/regions/world.object-placements.indexed.bin"
#define OBHV_PATH RC_TEST_SOURCE_DIR "/data/defs/object_behaviors.bin"
#define VARBIT_PATH RC_TEST_SOURCE_DIR "/data/defs/varbits.bin"

enum {
    TEST_OBJECT_HAS_ACTIONS = 1u << 4,
    TEST_OBJECT_SOLID = 1u << 5,
};

static RcWorld *create_world(void) {
    RcWorldConfig cfg = rc_preset_base_only();
    cfg.subsystems = RC_SUB_OBJECTS;
    cfg.object_defs_path = ODEF_PATH;
    cfg.object_placements_path = OPLI_PATH;
    cfg.object_behaviors_path = OBHV_PATH;
    cfg.varbits_path = VARBIT_PATH;
    RcWorld *world = rc_world_create_config(&cfg);
    assert(world != NULL);
    return world;
}

static void init_region(RcWorld *world, int x, int y) {
    memset(&world->map, 0, sizeof(world->map));
    world->map.region_count = 1;
    world->map.region_width = 1;
    world->map.region_height = 1;
    world->map.base_region_x = x / RC_REGION_SIZE;
    world->map.base_region_y = y / RC_REGION_SIZE;
    world->map.regions[0].region_x = world->map.base_region_x;
    world->map.regions[0].region_y = world->map.base_region_y;
    world->map.regions[0].loaded = 1;
}

static uint32_t flags_at(RcWorld *world, int x, int y) {
    RcRegion *region = &world->map.regions[0];
    assert(x / RC_REGION_SIZE == region->region_x);
    assert(y / RC_REGION_SIZE == region->region_y);
    return region->tiles[0][x % RC_REGION_SIZE][y % RC_REGION_SIZE]
        .collision_flags;
}

static void test_complete_transform_resolution(RcWorld *world) {
    const RcObjectDef *def = rc_object_def_get(15061);
    assert(def != NULL && def->transform_count == 3);
    int first = -1, second = -1, fallback = 0;
    assert(rc_object_def_transform_at(def, 0, &first));
    assert(rc_object_def_transform_at(def, 1, &second));
    assert(rc_object_def_transform_at(def, 2, &fallback));
    assert(first == 15052 && second >= 0 && fallback == -1);
    assert(!rc_object_def_transform_at(def, 3, &first));

    if (def->varbit >= 0) {
        assert(rc_varbit_set(world, def->varbit, 0) == 0);
    } else {
        assert(def->varp >= 0 && def->varp < RC_MAX_VARPS);
        world->varps[def->varp] = 0;
    }
    int resolved = -1;
    assert(rc_world_object_def_resolve(world, 15061, &resolved));
    assert(resolved == 15052);
    if (def->varbit >= 0) {
        assert(rc_varbit_set(world, def->varbit, 1) == 0);
    } else {
        world->varps[def->varp] = 1;
    }
    assert(rc_world_object_def_resolve(world, 15061, &resolved));
    assert(resolved == second);

    int absent_id = -1;
    const RcObjectDef *absent = NULL;
    for (int id = 0; id < RC_MAX_OBJECT_ID; id++) {
        const RcObjectDef *candidate = rc_object_def_get(id);
        int last = 0;
        if (!candidate || candidate->transform_count == 0
                || !rc_object_def_transform_at(
                    candidate, candidate->transform_count - 1, &last)
                || last >= 0) {
            continue;
        }
        if (candidate->varp >= 0 && candidate->varp < RC_MAX_VARPS) {
            absent_id = id;
            absent = candidate;
            break;
        }
        const RcVarbitDef *varbit = rc_varbit_def_get(candidate->varbit);
        int width = varbit ? varbit->msb - varbit->lsb + 1 : 0;
        uint32_t max_selector = width > 0 && width < 31
                              ? (1u << width) - 1u : 0u;
        if (max_selector >= candidate->transform_count - 1) {
            absent_id = id;
            absent = candidate;
            break;
        }
    }
    assert(absent_id >= 0 && absent != NULL);
    if (absent->varp >= 0) {
        world->varps[absent->varp] = absent->transform_count - 1;
    } else {
        assert(rc_varbit_set(world, absent->varbit,
                             absent->transform_count - 1) == 0);
    }
    assert(!rc_world_object_def_resolve(world, absent_id, &resolved));
}

static void test_exact_static_placement_identity(void) {
    RcObjectPlacement rows[16];
    int count = rc_object_placements_at(3196, 3384, 0, rows, 16);
    assert(count > 0);
    for (int i = 0; i < count; i++) {
        RcObjectPlacement exact = {0};
        assert(rows[i].key != 0);
        assert(rc_object_placement_find_key(
            rows[i].key, rows[i].x, rows[i].y, rows[i].plane, &exact));
        assert(memcmp(&exact, &rows[i], sizeof(exact)) == 0);
    }
}

static void test_layer_identity_and_reclamation(RcWorld *world) {
    RcObjectPlacement wall = {
        .obj_id = 11780, .x = 100, .y = 100, .plane = 0,
        .type = 0, .rotation = 0,
    };
    RcObjectPlacement prop = wall;
    prop.type = 10;
    uint64_t wall_key = 0, prop_key = 0;
    assert(rc_world_object_add(world, &wall, 0, &wall_key)
           == RC_OBJECT_MUTATION_OK);
    assert(rc_world_object_add(world, &prop, 0, &prop_key)
           == RC_OBJECT_MUTATION_OK);
    assert(wall_key != 0 && prop_key != 0 && wall_key != prop_key);
    assert(rc_world_object_state_find_key(world, wall_key) != NULL);
    assert(rc_world_object_state_find_key(world, prop_key) != NULL);
    assert(rc_world_object_state_find(world, 11780, 100, 100, 0) == NULL);
    assert(rc_world_object_active_id(world, 11780, 100, 100, 0) == -1);
    RcObjectState layer_state;
    assert(rc_world_object_active_state_layer(
        world, 11780, 100, 100, 0, 0, &layer_state));
    assert(layer_state.placement_key == wall_key);
    assert(rc_world_object_active_state_layer(
        world, 11780, 100, 100, 0, 2, &layer_state));
    assert(layer_state.placement_key == prop_key);
    assert(rc_world_object_revert(world, wall_key) == RC_OBJECT_MUTATION_OK);
    assert(rc_world_object_revert(world, prop_key) == RC_OBJECT_MUTATION_OK);
    assert(world->object_state_count == 0);

    uint64_t keys[RC_MAX_OBJECT_STATES];
    for (int i = 0; i < RC_MAX_OBJECT_STATES; i++) {
        RcObjectPlacement object = {
            .obj_id = 11780,
            .x = (uint16_t)(100 + i),
            .y = 1,
            .plane = 0,
            .type = 10,
            .rotation = 0,
        };
        assert(rc_world_object_add(world, &object, 0, &keys[i])
               == RC_OBJECT_MUTATION_OK);
    }
    RcObjectPlacement overflow = {
        .obj_id = 11780, .x = 700, .y = 1, .plane = 0,
        .type = 10, .rotation = 0,
    };
    assert(rc_world_object_add(world, &overflow, 0, NULL)
           == RC_OBJECT_MUTATION_CAPACITY);
    for (int i = 0; i < RC_MAX_OBJECT_STATES; i++)
        assert(rc_world_object_revert(world, keys[i])
               == RC_OBJECT_MUTATION_OK);
    assert(world->object_state_count == 0);

    for (int i = 0; i < RC_MAX_OBJECT_STATES * 2; i++) {
        uint64_t key = 0;
        assert(rc_world_object_add(world, &overflow, 1, &key)
               == RC_OBJECT_MUTATION_OK);
        world->tick++;
        rc_world_objects_tick(world);
        assert(world->object_state_count == 0);
    }
}

static void test_collision_mutation(RcWorld *world) {
    init_region(world, 100, 100);
    static const struct {
        uint8_t type;
        uint8_t rotation;
        uint32_t base_flags;
        int neighbor_dx;
        int neighbor_dy;
        uint32_t neighbor_flags;
    } wall_cases[] = {
        {0, 0, COL_WALL_W,  -1,  0, COL_WALL_E},
        {0, 1, COL_WALL_N,   0,  1, COL_WALL_S},
        {0, 2, COL_WALL_E,   1,  0, COL_WALL_W},
        {0, 3, COL_WALL_S,   0, -1, COL_WALL_N},
        {1, 0, COL_WALL_NW, -1,  1, COL_WALL_SE},
        {1, 1, COL_WALL_NE,  1,  1, COL_WALL_SW},
        {1, 2, COL_WALL_SE,  1, -1, COL_WALL_NW},
        {1, 3, COL_WALL_SW, -1, -1, COL_WALL_NE},
        {2, 0, COL_WALL_W | COL_WALL_N, -1, 0, COL_WALL_E},
        {2, 1, COL_WALL_E | COL_WALL_N,  1, 0, COL_WALL_W},
        {2, 2, COL_WALL_E | COL_WALL_S,  1, 0, COL_WALL_W},
        {2, 3, COL_WALL_W | COL_WALL_S, -1, 0, COL_WALL_E},
        {3, 0, COL_WALL_W, -1, 0, COL_WALL_E},
    };
    for (size_t i = 0; i < sizeof(wall_cases) / sizeof(wall_cases[0]); i++) {
        RcObjectPlacement wall = {
            .obj_id = 11780,
            .x = 100,
            .y = 100,
            .plane = 0,
            .type = wall_cases[i].type,
            .rotation = wall_cases[i].rotation,
        };
        uint64_t wall_key = 0;
        assert(rc_world_object_add(world, &wall, 0, &wall_key)
               == RC_OBJECT_MUTATION_OK);
        assert((flags_at(world, 100, 100) & wall_cases[i].base_flags)
               == wall_cases[i].base_flags);
        assert((flags_at(world, 100, 100)
                & (wall_cases[i].base_flags << 9))
               == (wall_cases[i].base_flags << 9));
        assert((flags_at(world, 100 + wall_cases[i].neighbor_dx,
                         100 + wall_cases[i].neighbor_dy)
                & wall_cases[i].neighbor_flags)
               == wall_cases[i].neighbor_flags);
        assert(rc_world_object_revert(world, wall_key)
               == RC_OBJECT_MUTATION_OK);
        assert(flags_at(world, 100, 100) == 0);
        assert(flags_at(world, 100 + wall_cases[i].neighbor_dx,
                        100 + wall_cases[i].neighbor_dy) == 0);
    }

    RcObjectPlacement corner = {
        .obj_id = 11780, .x = 100, .y = 100, .plane = 0,
        .type = 2, .rotation = 0,
    };
    uint64_t key = 0;
    assert(rc_world_object_add(world, &corner, 0, &key)
           == RC_OBJECT_MUTATION_OK);
    assert(flags_at(world, 100, 100) & (COL_WALL_W | COL_WALL_N));
    assert(flags_at(world, 99, 100) & COL_WALL_E);
    assert(flags_at(world, 100, 101) & COL_WALL_S);
    assert(flags_at(world, 100, 100)
           & (COL_PROJ_WALL_W | COL_PROJ_WALL_N));
    memset(world->map.regions[0].tiles, 0,
           sizeof(world->map.regions[0].tiles));
    assert(rc_world_objects_replay_collision(world));
    assert(flags_at(world, 100, 100) & (COL_WALL_W | COL_WALL_N));
    assert(flags_at(world, 99, 100) & COL_WALL_E);
    assert(flags_at(world, 100, 101) & COL_WALL_S);
    assert(rc_world_object_revert(world, key) == RC_OBJECT_MUTATION_OK);
    assert((flags_at(world, 100, 100)
            & (COL_WALL_W | COL_WALL_N | COL_PROJ_WALL_W |
               COL_PROJ_WALL_N)) == 0);

    int footprint_id = -1;
    for (int id = 0; id < RC_MAX_OBJECT_ID; id++) {
        const RcObjectDef *def = rc_object_def_get(id);
        if (def && (def->flags & TEST_OBJECT_SOLID) && def->interact_type
                && def->width == 2 && def->length == 3) {
            footprint_id = id;
            break;
        }
    }
    assert(footprint_id >= 0);
    RcObjectPlacement footprint = {
        .obj_id = (uint32_t)footprint_id,
        .x = 104, .y = 104, .plane = 0, .type = 10, .rotation = 1,
    };
    assert(rc_world_object_add(world, &footprint, 0, &key)
           == RC_OBJECT_MUTATION_OK);
    for (int dx = 0; dx < 3; dx++)
        for (int dy = 0; dy < 2; dy++)
            assert(flags_at(world, 104 + dx, 104 + dy) & COL_BLOCK_WALK);
    assert(rc_world_object_revert(world, key) == RC_OBJECT_MUTATION_OK);

    int ground_id = -1;
    for (int id = 0; id < RC_MAX_OBJECT_ID; id++) {
        const RcObjectDef *def = rc_object_def_get(id);
        if (def && (def->flags & TEST_OBJECT_SOLID) && def->interact_type
                && (def->flags & TEST_OBJECT_HAS_ACTIONS)) {
            ground_id = id;
            break;
        }
    }
    assert(ground_id >= 0);
    RcObjectPlacement ground = {
        .obj_id = (uint32_t)ground_id,
        .x = 110, .y = 110, .plane = 0, .type = 22, .rotation = 0,
    };
    assert(rc_world_object_add(world, &ground, 0, &key)
           == RC_OBJECT_MUTATION_OK);
    assert(flags_at(world, 110, 110) & COL_BLOCK_WALK);
    assert(rc_world_object_revert(world, key) == RC_OBJECT_MUTATION_OK);

    // Interact type 0 never clips, though its solid flag is set: Vorkath's
    // acid pool is walked over. Removing one leaves the tile's own flags.
    const RcObjectDef *acid = rc_object_def_get(32000);
    assert(acid && (acid->flags & TEST_OBJECT_SOLID) && !acid->interact_type);
    world->map.regions[0].tiles[0][112 % RC_REGION_SIZE][112 % RC_REGION_SIZE]
        .collision_flags = COL_BLOCK_WALK;
    RcObjectPlacement pools[2] = {
        {.obj_id = 32000, .x = 111, .y = 111, .plane = 0, .type = 10},
        {.obj_id = 32000, .x = 112, .y = 112, .plane = 0, .type = 10},
    };
    uint64_t pool_keys[2] = {0};
    for (int i = 0; i < 2; i++)
        assert(rc_world_object_add(world, &pools[i], 0, &pool_keys[i])
               == RC_OBJECT_MUTATION_OK);
    assert(flags_at(world, 111, 111) == 0);
    assert(rc_can_move(&world->map, 110, 111, 1, 0, 0) && "walked onto");
    for (int i = 0; i < 2; i++)
        assert(rc_world_object_revert(world, pool_keys[i])
               == RC_OBJECT_MUTATION_OK);
    assert(flags_at(world, 112, 112) == COL_BLOCK_WALK);
}

// A loc added without an expiry gets one later and reverts on that tick.
static void test_expiry_set_later(RcWorld *world) {
    RcObjectPlacement object = {
        .obj_id = 11780, .x = 700, .y = 3, .plane = 0, .type = 10, .rotation = 0,
    };
    uint64_t key = 0;
    assert(rc_world_object_add(world, &object, 0, &key) == RC_OBJECT_MUTATION_OK);
    RcTick at = world->tick + 3;
    assert(rc_world_object_expire(world, key, at) == RC_OBJECT_MUTATION_OK);
    assert(rc_world_object_expire(world, key + 1000, at) == RC_OBJECT_MUTATION_NOT_FOUND);
    assert(rc_world_object_expire(world, 0, at) == RC_OBJECT_MUTATION_INVALID);
    while (world->tick < at - 1) {
        world->tick++;
        rc_world_objects_tick(world);
        assert(world->object_state_count == 1);
    }
    world->tick++;
    rc_world_objects_tick(world);
    assert(world->object_state_count == 0);
}

int main(void) {
    RcWorld *world = create_world();
    test_complete_transform_resolution(world);
    test_exact_static_placement_identity();
    test_layer_identity_and_reclamation(world);
    test_expiry_set_later(world);
    test_collision_mutation(world);
    assert(!rc_world_object_option_supported(world, 1276, -1, -1, -1, 0, 0));
    rc_world_destroy(world);
    printf("test_objects_dynamic_locs_foundation: object contract verified.\n");
    return 0;
}
