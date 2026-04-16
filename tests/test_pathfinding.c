#include "../rc-core/pathfinding.h"
#include <stdio.h>
#include <assert.h>
#include <string.h>

int main(void) {
    // Create a small world map with one region, all walkable
    RcWorldMap map = {0};
    map.region_count = 1;
    map.regions[0].region_x = 0;
    map.regions[0].region_y = 0;
    map.regions[0].loaded = 1;
    // All tiles default to 0 flags (fully walkable)

    // Block a tile
    map.regions[0].tiles[0][5][5].collision_flags = COL_BLOCK_WALK;

    // Test tile blocked
    assert(rc_tile_blocked(&map, 5, 5, 0) == true);
    assert(rc_tile_blocked(&map, 6, 6, 0) == false);

    // Test pathfinding around blocked tile
    RcRoute route = rc_find_path(&map, 3, 5, 7, 5, 1, 0, true);
    assert(route.length > 0);
    // Path should not go through (5,5)
    for (int i = 0; i < route.length; i++) {
        assert(!(route.waypoints_x[i] == 5 && route.waypoints_y[i] == 5));
    }

    printf("All pathfinding tests passed.\n");
    return 0;
}
