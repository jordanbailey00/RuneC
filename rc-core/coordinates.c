#include "coordinates.h"

#include <limits.h>
#include <math.h>

const int8_t rc_direction_dx[9] = {0, 0, 1, 1, 1, 0, -1, -1, -1};
const int8_t rc_direction_dy[9] = {0, 1, 1, 0, -1, -1, -1, 0, 1};

static int clamp_int(int value, int minimum, int maximum) {
    if (value < minimum) return minimum;
    if (value > maximum) return maximum;
    return value;
}

int rc_mapsquare_key(int mapsquare_x, int mapsquare_y, uint16_t *key) {
    if (!key || !rc_mapsquare_coord_valid(mapsquare_x)
            || !rc_mapsquare_coord_valid(mapsquare_y)) {
        return 0;
    }
    *key = (uint16_t)((mapsquare_x << 8) | mapsquare_y);
    return 1;
}

int rc_world_to_mapsquare(int x, int y, uint16_t *key,
                          int *local_x, int *local_y) {
    if (!rc_world_coord_valid(x) || !rc_world_coord_valid(y)
            || !rc_mapsquare_key(x / RC_MAPSQUARE_SIZE,
                                 y / RC_MAPSQUARE_SIZE, key)) {
        return 0;
    }
    if (local_x) *local_x = x % RC_MAPSQUARE_SIZE;
    if (local_y) *local_y = y % RC_MAPSQUARE_SIZE;
    return 1;
}

int rc_world_to_zone(int x, int y, int plane, uint32_t *key,
                     int *local_x, int *local_y) {
    if (!key || !rc_world_tile_valid(x, y, plane)) return 0;
    uint32_t zone_x = (uint32_t)(x / RC_ZONE_SIZE);
    uint32_t zone_y = (uint32_t)(y / RC_ZONE_SIZE);
    *key = ((uint32_t)plane << 22) | (zone_x << 11) | zone_y;
    if (local_x) *local_x = x % RC_ZONE_SIZE;
    if (local_y) *local_y = y % RC_ZONE_SIZE;
    return 1;
}

int rc_tile_rect_intersect_world(int min_x, int min_y, int max_x, int max_y,
                                 RcTileRect *out) {
    if (!out || min_x > max_x || min_y > max_y
            || max_x < 0 || max_y < 0
            || min_x > RC_WORLD_MAX || min_y > RC_WORLD_MAX) {
        return 0;
    }
    return rc_tile_rect_make(clamp_int(min_x, 0, RC_WORLD_MAX),
                             clamp_int(min_y, 0, RC_WORLD_MAX),
                             clamp_int(max_x, 0, RC_WORLD_MAX),
                             clamp_int(max_y, 0, RC_WORLD_MAX), out);
}

int rc_tile_rect_around(int center_x, int center_y, int radius,
                        RcTileRect *out) {
    if (!out || radius < 0
            || !rc_world_coord_valid(center_x)
            || !rc_world_coord_valid(center_y)) {
        return 0;
    }
    int64_t min_x = (int64_t)center_x - radius;
    int64_t min_y = (int64_t)center_y - radius;
    int64_t max_x = (int64_t)center_x + radius;
    int64_t max_y = (int64_t)center_y + radius;
    return rc_tile_rect_make(
        min_x < 0 ? 0 : (int)min_x,
        min_y < 0 ? 0 : (int)min_y,
        max_x > RC_WORLD_MAX ? RC_WORLD_MAX : (int)max_x,
        max_y > RC_WORLD_MAX ? RC_WORLD_MAX : (int)max_y, out);
}

RcDirection rc_direction_from_delta(int dx, int dy) {
    int sx = (dx > 0) - (dx < 0);
    int sy = (dy > 0) - (dy < 0);
    if (sx == 0 && sy == 0) return RC_DIRECTION_NONE;
    if (sx == 0) return sy > 0 ? RC_DIRECTION_NORTH : RC_DIRECTION_SOUTH;
    if (sy == 0) return sx > 0 ? RC_DIRECTION_EAST : RC_DIRECTION_WEST;
    if (sx > 0) {
        return sy > 0 ? RC_DIRECTION_NORTH_EAST
                      : RC_DIRECTION_SOUTH_EAST;
    }
    return sy > 0 ? RC_DIRECTION_NORTH_WEST : RC_DIRECTION_SOUTH_WEST;
}

int rc_direction_delta(RcDirection direction, int *dx, int *dy) {
    if (!dx || !dy || direction < RC_DIRECTION_NONE
            || direction > RC_DIRECTION_NORTH_WEST) {
        return 0;
    }
    *dx = rc_direction_dx[direction];
    *dy = rc_direction_dy[direction];
    return 1;
}

int rc_face_angle(int from_x, int from_y, int from_width, int from_length,
                  int to_x, int to_y, int to_width, int to_length) {
    // Centres in half tiles (2x + width), as rsmod Direction.angleTo. The
    // client truncates atan2 * 325.949, leaving due east at 1537; rounding
    // 1024/pi per radian keeps the exact step yaws.
    int dx = (2 * from_x + from_width) - (2 * to_x + to_width);
    int dy = (2 * from_y + from_length) - (2 * to_y + to_length);
    if (!dx && !dy) return -1;
    return (int)lround(atan2((double)dx, (double)dy) * (1024.0 / M_PI))
         & 2047;
}

int rc_direction_face_angle(RcDirection direction) {
    // The client's step yaws (Client3 updateMovement), not atan2's.
    static const int16_t angles[9] = {
        -1, 1024, 1280, 1536, 1792, 0, 256, 512, 768,
    };
    if (direction < RC_DIRECTION_NONE || direction > RC_DIRECTION_NORTH_WEST)
        return -1;
    return angles[direction];
}
