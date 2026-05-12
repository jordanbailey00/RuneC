#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "api.h"
#include "collision.h"
#include "config.h"
#include "pathfinding.h"

#define CTIL_PATH RC_TEST_SOURCE_DIR "/data/defs/collision_tiles.bin"
#define BAD_PATH "/tmp/runec_bad_collision.bin"

static void write_bad_header(void) {
    uint32_t bad[4] = {0, 1, 0, 0};
    FILE *f = fopen(BAD_PATH, "wb");
    assert(f != NULL);
    assert(fwrite(bad, sizeof(bad), 1, f) == 1);
    fclose(f);
}

int main(void) {
    RcWorldMap empty = {0};
    assert(rc_get_flags(&empty, 0, 0, 0) == 0);

    write_bad_header();
    assert(rc_load_collision_tiles(NULL) == -1);
    assert(rc_load_collision_tiles("/missing/collision_tiles.bin") == -1);
    assert(rc_load_collision_tiles(BAD_PATH) == -1);
    assert(rc_load_collision_tiles(CTIL_PATH) > 2000);
    assert(rc_collision_is_loaded());

    int found = 0;
    uint32_t blocked = rc_collision_flags_at(3072, 3395, 0, &found);
    assert(found == 1);
    assert(blocked & COL_BLOCK_WALK);
    assert(rc_get_flags(&empty, 3072, 3395, 0) == blocked);

    uint32_t open = rc_collision_flags_at(3213, 3428, 0, &found);
    assert(found == 1);
    assert(open == rc_get_flags(&empty, 3213, 3428, 0));

    uint32_t bridge_south_rail = rc_collision_flags_at(3105, 3420, 0, &found);
    assert(found == 1);
    assert(bridge_south_rail & COL_WALL_S);
    assert(rc_can_move(&empty, 3104, 3420, 1, 0, 0));
    assert(!rc_can_move(&empty, 3105, 3419, 0, 1, 0));
    assert(!rc_can_move(&empty, 3105, 3421, 0, 1, 0));

    (void)rc_collision_flags_at(0, 0, 0, &found);
    assert(found == 0);
    assert(rc_get_flags(&empty, 0, 0, 0) == COL_BLOCK_WALK);
    assert(rc_get_flags(&empty, -1, 0, 0) == COL_BLOCK_WALK);

    RcWorldConfig cfg = rc_preset_base_only();
    cfg.subsystems = RC_SUB_REGIONS;
    cfg.collision_tiles_path = CTIL_PATH;
    RcWorld *world = rc_world_create_config(&cfg);
    assert(world != NULL);
    assert(world->enabled & RC_SUB_REGIONS);
    assert(g_rc_collision_region_count > 2000);
    rc_world_destroy(world);

    return 0;
}
