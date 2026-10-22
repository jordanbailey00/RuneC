#ifndef RC_COORDINATES_H
#define RC_COORDINATES_H

#include <stdint.h>

enum {
    RC_WORLD_COORD_BITS = 14,
    RC_WORLD_SIZE = 1 << RC_WORLD_COORD_BITS,
    RC_WORLD_MAX = RC_WORLD_SIZE - 1,
    RC_MAPSQUARE_SIZE = 64,
    RC_MAPSQUARE_AXIS = RC_WORLD_SIZE / RC_MAPSQUARE_SIZE,
    RC_MAPSQUARE_COUNT = RC_MAPSQUARE_AXIS * RC_MAPSQUARE_AXIS,
    RC_ZONE_SIZE = 8,
    RC_ZONE_AXIS = RC_WORLD_SIZE / RC_ZONE_SIZE,
    RC_MAX_PLANES = 4,
};

/* Compatibility name for existing 64-by-64 map storage. */
#define RC_REGION_SIZE RC_MAPSQUARE_SIZE

typedef struct RcTileRect {
    int min_x, min_y;
    int max_x, max_y;
} RcTileRect;

typedef struct RcTileBounds {
    RcTileRect rect;
    int plane;
} RcTileBounds;

typedef enum RcDirection {
    RC_DIRECTION_NONE = 0,
    RC_DIRECTION_NORTH,
    RC_DIRECTION_NORTH_EAST,
    RC_DIRECTION_EAST,
    RC_DIRECTION_SOUTH_EAST,
    RC_DIRECTION_SOUTH,
    RC_DIRECTION_SOUTH_WEST,
    RC_DIRECTION_WEST,
    RC_DIRECTION_NORTH_WEST,
} RcDirection;

extern const int8_t rc_direction_dx[9];
extern const int8_t rc_direction_dy[9];

static inline int rc_world_coord_valid(int value) {
    return (unsigned)value < RC_WORLD_SIZE;
}

static inline int rc_plane_valid(int plane) {
    return (unsigned)plane < RC_MAX_PLANES;
}

static inline int rc_world_tile_valid(int x, int y, int plane) {
    return rc_world_coord_valid(x) && rc_world_coord_valid(y)
        && rc_plane_valid(plane);
}

static inline int rc_mapsquare_coord_valid(int value) {
    return (unsigned)value < RC_MAPSQUARE_AXIS;
}

static inline int rc_tile_distance(int first_x, int first_y, int first_plane,
                                   int second_x, int second_y,
                                   int second_plane) {
    if (!rc_world_tile_valid(first_x, first_y, first_plane)
            || !rc_world_tile_valid(second_x, second_y, second_plane)
            || first_plane != second_plane) {
        return -1;
    }
    int dx = first_x > second_x ? first_x - second_x : second_x - first_x;
    int dy = first_y > second_y ? first_y - second_y : second_y - first_y;
    return dx > dy ? dx : dy;
}

int rc_mapsquare_key(int mapsquare_x, int mapsquare_y, uint16_t *key);
int rc_world_to_mapsquare(int x, int y, uint16_t *key,
                          int *local_x, int *local_y);
int rc_world_to_zone(int x, int y, int plane, uint32_t *key,
                     int *local_x, int *local_y);

static inline int rc_tile_rect_make(int min_x, int min_y,
                                    int max_x, int max_y,
                                    RcTileRect *out) {
    if (!out || !rc_world_coord_valid(min_x)
            || !rc_world_coord_valid(min_y)
            || !rc_world_coord_valid(max_x)
            || !rc_world_coord_valid(max_y)
            || min_x > max_x || min_y > max_y) {
        return 0;
    }
    *out = (RcTileRect){min_x, min_y, max_x, max_y};
    return 1;
}

static inline int rc_tile_rect_from_origin_size(int origin_x, int origin_y,
                                                int width, int height,
                                                RcTileRect *out) {
    if (!out || width <= 0 || height <= 0
            || !rc_world_coord_valid(origin_x)
            || !rc_world_coord_valid(origin_y)) {
        return 0;
    }
    int64_t max_x = (int64_t)origin_x + width - 1;
    int64_t max_y = (int64_t)origin_y + height - 1;
    if (max_x > RC_WORLD_MAX || max_y > RC_WORLD_MAX) return 0;
    return rc_tile_rect_make(origin_x, origin_y, (int)max_x, (int)max_y,
                             out);
}

int rc_tile_rect_intersect_world(int min_x, int min_y, int max_x, int max_y,
                                 RcTileRect *out);
int rc_tile_rect_around(int center_x, int center_y, int radius,
                        RcTileRect *out);

static inline int rc_tile_rect_contains(const RcTileRect *rect,
                                        int x, int y) {
    return rect && x >= rect->min_x && x <= rect->max_x
        && y >= rect->min_y && y <= rect->max_y;
}

static inline int rc_tile_rect_overlaps(const RcTileRect *a,
                                        const RcTileRect *b) {
    return a && b && a->min_x <= b->max_x && b->min_x <= a->max_x
        && a->min_y <= b->max_y && b->min_y <= a->max_y;
}

static inline int rc_tile_rect_closest_point(const RcTileRect *rect,
                                             int x, int y,
                                             int *closest_x,
                                             int *closest_y) {
    if (!rect || !closest_x || !closest_y) return 0;
    *closest_x = x < rect->min_x ? rect->min_x
               : x > rect->max_x ? rect->max_x : x;
    *closest_y = y < rect->min_y ? rect->min_y
               : y > rect->max_y ? rect->max_y : y;
    return 1;
}

static inline int rc_tile_rect_axis_separation(const RcTileRect *a,
                                               const RcTileRect *b,
                                               int *separation_x,
                                               int *separation_y) {
    if (!a || !b || !separation_x || !separation_y) return 0;
    if (a->max_x < b->min_x) *separation_x = b->min_x - a->max_x;
    else if (b->max_x < a->min_x) *separation_x = a->min_x - b->max_x;
    else *separation_x = 0;
    if (a->max_y < b->min_y) *separation_y = b->min_y - a->max_y;
    else if (b->max_y < a->min_y) *separation_y = a->min_y - b->max_y;
    else *separation_y = 0;
    return 1;
}

static inline int rc_tile_bounds_make(int min_x, int min_y,
                                      int max_x, int max_y,
                                      int plane, RcTileBounds *out) {
    if (!out || !rc_plane_valid(plane)
            || !rc_tile_rect_make(min_x, min_y, max_x, max_y,
                                  &out->rect)) {
        return 0;
    }
    out->plane = plane;
    return 1;
}

static inline int rc_tile_bounds_from_origin_size(int origin_x, int origin_y,
                                                  int width, int height,
                                                  int plane,
                                                  RcTileBounds *out) {
    if (!out || !rc_plane_valid(plane)
            || !rc_tile_rect_from_origin_size(origin_x, origin_y,
                                              width, height, &out->rect)) {
        return 0;
    }
    out->plane = plane;
    return 1;
}

static inline int rc_tile_bounds_distance(const RcTileBounds *a,
                                          const RcTileBounds *b) {
    if (!a || !b || a->plane != b->plane) return -1;
    int dx, dy;
    if (!rc_tile_rect_axis_separation(&a->rect, &b->rect, &dx, &dy))
        return -1;
    return dx > dy ? dx : dy;
}

RcDirection rc_direction_from_delta(int dx, int dy);
int rc_direction_delta(RcDirection direction, int *dx, int *dy);

// OSRS face angle (0 = south, 512 west, 1024 north, 1536 east; 2048 per
// turn) from one footprint's centre toward another's, or -1 when the centres
// coincide.
int rc_face_angle(int from_x, int from_y, int from_width, int from_length,
                  int to_x, int to_y, int to_width, int to_length);
// The face angle of a step direction, or -1 for RC_DIRECTION_NONE.
int rc_direction_face_angle(RcDirection direction);

#endif
