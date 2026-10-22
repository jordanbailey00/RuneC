#include "../rc-core/coordinates.h"

#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>

static void test_world_and_spatial_keys(void) {
    assert(rc_world_tile_valid(0, 0, 0));
    assert(rc_world_tile_valid(RC_WORLD_MAX, RC_WORLD_MAX,
                               RC_MAX_PLANES - 1));
    assert(!rc_world_tile_valid(-1, 0, 0));
    assert(!rc_world_tile_valid(RC_WORLD_SIZE, 0, 0));
    assert(!rc_world_tile_valid(0, RC_WORLD_SIZE, 0));
    assert(!rc_world_tile_valid(0, 0, RC_MAX_PLANES));

    static const int coordinates[] = {0, 7, 8, 63, 64, RC_WORLD_MAX};
    for (size_t i = 0; i < sizeof(coordinates) / sizeof(coordinates[0]); i++) {
        int x = coordinates[i];
        int y = RC_WORLD_MAX - x;
        uint16_t mapsquare;
        int local_x, local_y;
        assert(rc_world_to_mapsquare(x, y, &mapsquare, &local_x, &local_y));
        int mapsquare_x = mapsquare >> 8;
        int mapsquare_y = mapsquare & 0xff;
        assert(mapsquare_x * RC_MAPSQUARE_SIZE + local_x == x);
        assert(mapsquare_y * RC_MAPSQUARE_SIZE + local_y == y);

        for (int plane = 0; plane < RC_MAX_PLANES; plane++) {
            uint32_t zone;
            assert(rc_world_to_zone(x, y, plane, &zone, &local_x, &local_y));
            int zone_x = (int)((zone >> 11) & 0x7ffu);
            int zone_y = (int)(zone & 0x7ffu);
            assert((int)(zone >> 22) == plane);
            assert(zone_x * RC_ZONE_SIZE + local_x == x);
            assert(zone_y * RC_ZONE_SIZE + local_y == y);
        }
    }

    uint16_t key = 0;
    assert(rc_mapsquare_key(255, 255, &key) && key == UINT16_MAX);
    assert(!rc_mapsquare_key(256, 0, &key));
    assert(!rc_world_to_mapsquare(RC_WORLD_SIZE, 0, &key, NULL, NULL));
    assert(!rc_world_to_zone(0, 0, 4, NULL, NULL, NULL));
}

static void test_checked_rectangles(void) {
    RcTileRect rect;
    assert(rc_tile_rect_from_origin_size(0, 0, 1, 1, &rect));
    assert(rect.min_x == 0 && rect.max_x == 0);
    assert(rc_tile_rect_from_origin_size(RC_WORLD_MAX, RC_WORLD_MAX,
                                         1, 1, &rect));
    assert(!rc_tile_rect_from_origin_size(RC_WORLD_MAX, 0, 2, 1, &rect));
    assert(!rc_tile_rect_from_origin_size(0, 0, INT_MAX, 1, &rect));
    assert(!rc_tile_rect_from_origin_size(INT_MAX, 0, 2, 1, &rect));

    assert(rc_tile_rect_intersect_world(-100, -10, 20, 30, &rect));
    assert(rect.min_x == 0 && rect.min_y == 0
           && rect.max_x == 20 && rect.max_y == 30);
    assert(rc_tile_rect_intersect_world(RC_WORLD_MAX - 2,
                                        RC_WORLD_MAX - 3,
                                        INT_MAX, INT_MAX, &rect));
    assert(rect.max_x == RC_WORLD_MAX && rect.max_y == RC_WORLD_MAX);
    assert(!rc_tile_rect_intersect_world(INT_MIN, 0, -1, 10, &rect));

    assert(rc_tile_rect_around(0, 0, INT_MAX, &rect));
    assert(rect.min_x == 0 && rect.min_y == 0
           && rect.max_x == RC_WORLD_MAX && rect.max_y == RC_WORLD_MAX);
    assert(rc_tile_rect_around(RC_WORLD_MAX, RC_WORLD_MAX, 4, &rect));
    assert(rect.min_x == RC_WORLD_MAX - 4 && rect.max_x == RC_WORLD_MAX);
}

static void test_footprint_geometry(void) {
    RcTileBounds actor, target;
    assert(rc_tile_bounds_from_origin_size(10, 10, 1, 1, 0, &actor));
    assert(rc_tile_bounds_from_origin_size(11, 11, 3, 2, 0, &target));
    assert(rc_tile_bounds_distance(&actor, &target) == 1);
    assert(rc_tile_rect_overlaps(&target.rect, &target.rect));

    int closest_x, closest_y;
    assert(rc_tile_rect_closest_point(&target.rect, 8, 20,
                                      &closest_x, &closest_y));
    assert(closest_x == 11 && closest_y == 12);

    RcTileBounds overlap;
    assert(rc_tile_bounds_from_origin_size(12, 10, 2, 3, 0, &overlap));
    assert(rc_tile_bounds_distance(&target, &overlap) == 0);
    overlap.plane = 1;
    assert(rc_tile_bounds_distance(&target, &overlap) == -1);
    assert(rc_tile_distance(0, 0, 0, 3, 7, 0) == 7);
    assert(rc_tile_distance(0, 0, 0, 3, 7, 1) == -1);
    assert(!rc_tile_bounds_from_origin_size(RC_WORLD_MAX, RC_WORLD_MAX,
                                            2, 1, 0, &overlap));
}

static void test_directions(void) {
    static const int deltas[][2] = {
        {0, 0}, {0, 9}, {5, 3}, {8, 0}, {4, -7},
        {0, -2}, {-9, -1}, {-3, 0}, {-2, 6},
    };
    for (int direction = RC_DIRECTION_NONE;
         direction <= RC_DIRECTION_NORTH_WEST; direction++) {
        assert(rc_direction_from_delta(deltas[direction][0],
                                       deltas[direction][1]) == direction);
        int dx, dy;
        assert(rc_direction_delta((RcDirection)direction, &dx, &dy));
        assert(dx == (deltas[direction][0] > 0)
                   - (deltas[direction][0] < 0));
        assert(dy == (deltas[direction][1] > 0)
                   - (deltas[direction][1] < 0));
    }
    assert(!rc_direction_delta((RcDirection)99, NULL, NULL));
}

// Face angles: 0 south, 512 west, 1024 north, 1536 east, between footprint
// centres; steps take the client's exact yaws.
static void test_face_angles(void) {
    assert(rc_face_angle(10, 10, 1, 1, 10, 9, 1, 1) == 0);
    assert(rc_face_angle(10, 10, 1, 1, 9, 10, 1, 1) == 512);
    assert(rc_face_angle(10, 10, 1, 1, 10, 11, 1, 1) == 1024);
    assert(rc_face_angle(10, 10, 1, 1, 11, 10, 1, 1) == 1536);
    assert(rc_face_angle(10, 10, 1, 1, 11, 11, 1, 1) == 1280);
    // A 3x3 footprint beside the player is faced at its centre.
    assert(rc_face_angle(10, 10, 1, 1, 11, 10, 3, 3) == rc_face_angle(
        10, 10, 1, 1, 12, 11, 1, 1));
    assert(rc_face_angle(10, 10, 1, 1, 10, 10, 1, 1) == -1);
    assert(rc_face_angle(11, 11, 1, 1, 10, 10, 3, 3) == -1);
    static const int16_t yaws[9] = {-1, 1024, 1280, 1536, 1792, 0, 256, 512,
                                    768};
    for (int d = RC_DIRECTION_NONE; d <= RC_DIRECTION_NORTH_WEST; d++)
        assert(rc_direction_face_angle((RcDirection)d) == yaws[d]);
    assert(rc_direction_face_angle((RcDirection)99) == -1);
}

int main(void) {
    test_world_and_spatial_keys();
    test_checked_rectangles();
    test_footprint_geometry();
    test_directions();
    test_face_angles();
    printf("test_coordinates_world_primitives: coordinate contract verified.\n");
    return 0;
}
