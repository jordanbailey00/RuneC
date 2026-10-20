#include "pathfinding.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

enum {
    SEARCH_SIZE = 512,
    SEARCH_HALF = SEARCH_SIZE / 2,
    MAX_ALTERNATIVE_DISTANCE = 10,
};

typedef struct {
    uint8_t visited[SEARCH_SIZE][SEARCH_SIZE];
    int8_t parent_x[SEARCH_SIZE][SEARCH_SIZE];
    int8_t parent_y[SEARCH_SIZE][SEARCH_SIZE];
    uint16_t distance[SEARCH_SIZE][SEARCH_SIZE];
    uint16_t queue_x[SEARCH_SIZE * SEARCH_SIZE];
    uint16_t queue_y[SEARCH_SIZE * SEARCH_SIZE];
} RcRouteScratch;

static _Thread_local RcRouteScratch route_scratch;

static const RcRegion *map_region_at(const RcWorldMap *map, int region_x,
                                     int region_y) {
    if (!map || map->region_count <= 0) return NULL;
    for (int i = 0; i < map->region_count; i++) {
        const RcRegion *region = &map->regions[i];
        if (region->loaded && region->region_x == region_x
                && region->region_y == region_y) {
            return region;
        }
    }
    return NULL;
}

static int map_contains_region(const RcWorldMap *map, int region_x,
                               int region_y) {
    return map && map->region_width > 0 && map->region_height > 0
        && region_x >= map->base_region_x
        && region_y >= map->base_region_y
        && region_x < map->base_region_x + map->region_width
        && region_y < map->base_region_y + map->region_height;
}

uint32_t rc_get_flags(const RcWorldMap *map, int x, int y, int plane) {
    if (!rc_world_tile_valid(x, y, plane))
        return COL_BLOCK_WALK | COL_PROJ_BLOCK_FULL;

    int region_x = x / RC_REGION_SIZE;
    int region_y = y / RC_REGION_SIZE;
    const RcRegion *region = map_region_at(map, region_x, region_y);
    if (region) {
        return region->tiles[plane][x % RC_REGION_SIZE][y % RC_REGION_SIZE]
            .collision_flags;
    }

    /* An explicitly loaded empty mapsquare is open. Unknown data is closed. */
    return map_contains_region(map, region_x, region_y)
        ? 0u : COL_BLOCK_WALK | COL_PROJ_BLOCK_FULL;
}

bool rc_tile_blocked(const RcWorldMap *map, int x, int y, int plane) {
    return (rc_get_flags(map, x, y, plane)
            & (COL_LOC | COL_BLOCK_WALK | COL_GROUND_DECOR)) != 0;
}

static bool tile_clear(const RcWorldMap *map, int x, int y, int plane,
                       uint32_t mask) {
    return (rc_get_flags(map, x, y, plane) & mask) == 0;
}

bool rc_can_move_rect(const RcWorldMap *map, int x, int y,
                      int width, int height, int dx, int dy, int plane) {
    RcTileBounds source, destination;
    if ((dx == 0 && dy == 0) || dx < -1 || dx > 1 || dy < -1 || dy > 1
            || !rc_tile_bounds_from_origin_size(x, y, width, height, plane,
                                                &source)
            || !rc_tile_bounds_from_origin_size(x + dx, y + dy,
                                                width, height, plane,
                                                &destination)) {
        return false;
    }

    if (dx == 0 && dy == -1) {
        int edge_y = y - 1;
        if (width == 1) return tile_clear(map, x, edge_y, plane, COL_BLOCK_S);
        if (!tile_clear(map, x, edge_y, plane, COL_BLOCK_SW)
                || !tile_clear(map, x + width - 1, edge_y, plane,
                               COL_BLOCK_SE)) return false;
        for (int edge_x = x + 1; edge_x < x + width - 1; edge_x++)
            if (!tile_clear(map, edge_x, edge_y, plane, COL_BLOCK_N_EW))
                return false;
        return true;
    }
    if (dx == 0 && dy == 1) {
        int edge_y = y + height;
        if (width == 1) return tile_clear(map, x, edge_y, plane, COL_BLOCK_N);
        if (!tile_clear(map, x, edge_y, plane, COL_BLOCK_NW)
                || !tile_clear(map, x + width - 1, edge_y, plane,
                               COL_BLOCK_NE)) return false;
        for (int edge_x = x + 1; edge_x < x + width - 1; edge_x++)
            if (!tile_clear(map, edge_x, edge_y, plane, COL_BLOCK_S_EW))
                return false;
        return true;
    }
    if (dx == -1 && dy == 0) {
        int edge_x = x - 1;
        if (height == 1) return tile_clear(map, edge_x, y, plane, COL_BLOCK_W);
        if (!tile_clear(map, edge_x, y, plane, COL_BLOCK_SW)
                || !tile_clear(map, edge_x, y + height - 1, plane,
                               COL_BLOCK_NW)) return false;
        for (int edge_y = y + 1; edge_y < y + height - 1; edge_y++)
            if (!tile_clear(map, edge_x, edge_y, plane, COL_BLOCK_E_NS))
                return false;
        return true;
    }
    if (dx == 1 && dy == 0) {
        int edge_x = x + width;
        if (height == 1) return tile_clear(map, edge_x, y, plane, COL_BLOCK_E);
        if (!tile_clear(map, edge_x, y, plane, COL_BLOCK_SE)
                || !tile_clear(map, edge_x, y + height - 1, plane,
                               COL_BLOCK_NE)) return false;
        for (int edge_y = y + 1; edge_y < y + height - 1; edge_y++)
            if (!tile_clear(map, edge_x, edge_y, plane, COL_BLOCK_W_NS))
                return false;
        return true;
    }

    if (width == 1 && height == 1) {
        int nx = x + dx, ny = y + dy;
        uint32_t corner = dx < 0
            ? (dy < 0 ? COL_BLOCK_SW : COL_BLOCK_NW)
            : (dy < 0 ? COL_BLOCK_SE : COL_BLOCK_NE);
        uint32_t horizontal = dx < 0 ? COL_BLOCK_W : COL_BLOCK_E;
        uint32_t vertical = dy < 0 ? COL_BLOCK_S : COL_BLOCK_N;
        return tile_clear(map, nx, ny, plane, corner)
            && tile_clear(map, nx, y, plane, horizontal)
            && tile_clear(map, x, ny, plane, vertical);
    }

    if (dx < 0 && dy < 0) {
        if (!tile_clear(map, x - 1, y - 1, plane, COL_BLOCK_SW)) return false;
        for (int i = 1; i < height; i++)
            if (!tile_clear(map, x - 1, y + i - 1, plane, COL_BLOCK_E_NS))
                return false;
        for (int i = 1; i < width; i++)
            if (!tile_clear(map, x + i - 1, y - 1, plane, COL_BLOCK_N_EW))
                return false;
        return true;
    }
    if (dx < 0 && dy > 0) {
        if (!tile_clear(map, x - 1, y + height, plane, COL_BLOCK_NW))
            return false;
        for (int i = 1; i < height; i++)
            if (!tile_clear(map, x - 1, y + i, plane, COL_BLOCK_E_NS))
                return false;
        for (int i = 1; i < width; i++)
            if (!tile_clear(map, x + i - 1, y + height, plane,
                            COL_BLOCK_S_EW)) return false;
        return true;
    }
    if (dx > 0 && dy < 0) {
        if (!tile_clear(map, x + width, y - 1, plane, COL_BLOCK_SE))
            return false;
        for (int i = 1; i < height; i++)
            if (!tile_clear(map, x + width, y + i - 1, plane,
                            COL_BLOCK_W_NS)) return false;
        for (int i = 1; i < width; i++)
            if (!tile_clear(map, x + i, y - 1, plane, COL_BLOCK_N_EW))
                return false;
        return true;
    }
    if (!tile_clear(map, x + width, y + height, plane, COL_BLOCK_NE))
        return false;
    for (int i = 1; i < width; i++)
        if (!tile_clear(map, x + i, y + height, plane, COL_BLOCK_S_EW))
            return false;
    for (int i = 1; i < height; i++)
        if (!tile_clear(map, x + width, y + i, plane, COL_BLOCK_W_NS))
            return false;
    return true;
}

bool rc_can_move(const RcWorldMap *map, int x, int y, int dx, int dy,
                 int plane) {
    return rc_can_move_rect(map, x, y, 1, 1, dx, dy, plane);
}

static int line_coordinate(int a, int b, int size) {
    if (a >= b) return a;
    if (a + size - 1 <= b) return a + size - 1;
    return b;
}

bool rc_has_los_rect(const RcWorldMap *map,
                     int src_x, int src_y, int src_width, int src_height,
                     int dest_x, int dest_y, int dest_width, int dest_height,
                     int plane) {
    RcTileBounds source, destination;
    if (!rc_tile_bounds_from_origin_size(src_x, src_y, src_width, src_height,
                                         plane, &source)
            || !rc_tile_bounds_from_origin_size(dest_x, dest_y, dest_width,
                                                dest_height, plane,
                                                &destination)) {
        return false;
    }

    int start_x = line_coordinate(src_x, dest_x, src_width);
    int start_y = line_coordinate(src_y, dest_y, src_height);
    int end_x = line_coordinate(dest_x, src_x, dest_width);
    int end_y = line_coordinate(dest_y, src_y, dest_height);
    if (start_x == end_x && start_y == end_y) return true;
    if (rc_get_flags(map, start_x, start_y, plane) & COL_LOC) return false;

    int delta_x = end_x - start_x;
    int delta_y = end_y - start_y;
    int abs_x = abs(delta_x);
    int abs_y = abs(delta_y);
    bool east = delta_x >= 0;
    bool north = delta_y >= 0;
    uint32_t x_mask = east ? COL_PROJ_SIGHT_W : COL_PROJ_SIGHT_E;
    uint32_t y_mask = north ? COL_PROJ_SIGHT_S : COL_PROJ_SIGHT_N;

    if (abs_x > abs_y) {
        int step_x = east ? 1 : -1;
        int offset_y = north ? 0 : -1;
        int64_t scaled_y = ((int64_t)start_y << 16) + 32768 + offset_y;
        int64_t tangent = (int64_t)delta_y * 65536 / abs_x;   // deltas may be negative
        int current_x = start_x;
        while (current_x != end_x) {
            current_x += step_x;
            int current_y = (int)(scaled_y >> 16);
            uint32_t current_x_mask = x_mask;
            if (current_x == end_x && current_y == end_y)
                current_x_mask &= ~COL_PROJ_BLOCK_FULL;
            if (rc_get_flags(map, current_x, current_y, plane)
                    & current_x_mask) return false;
            scaled_y += tangent;
            int next_y = (int)(scaled_y >> 16);
            uint32_t current_y_mask = y_mask;
            if (current_x == end_x && next_y == end_y)
                current_y_mask &= ~COL_PROJ_BLOCK_FULL;
            if (next_y != current_y
                    && (rc_get_flags(map, current_x, next_y, plane)
                        & current_y_mask)) return false;
        }
    } else {
        int offset_x = east ? 0 : -1;
        int step_y = north ? 1 : -1;
        int64_t scaled_x = ((int64_t)start_x << 16) + 32768 + offset_x;
        int64_t tangent = (int64_t)delta_x * 65536 / abs_y;
        int current_y = start_y;
        while (current_y != end_y) {
            current_y += step_y;
            int current_x = (int)(scaled_x >> 16);
            uint32_t current_y_mask = y_mask;
            if (current_x == end_x && current_y == end_y)
                current_y_mask &= ~COL_PROJ_BLOCK_FULL;
            if (rc_get_flags(map, current_x, current_y, plane)
                    & current_y_mask) return false;
            scaled_x += tangent;
            int next_x = (int)(scaled_x >> 16);
            uint32_t current_x_mask = x_mask;
            if (next_x == end_x && current_y == end_y)
                current_x_mask &= ~COL_PROJ_BLOCK_FULL;
            if (next_x != current_x
                    && (rc_get_flags(map, next_x, current_y, plane)
                        & current_x_mask)) return false;
        }
    }
    return true;
}

bool rc_has_los(const RcWorldMap *map, int x0, int y0, int x1, int y1,
                int plane) {
    return rc_has_los_rect(map, x0, y0, 1, 1, x1, y1, 1, 1, plane);
}

bool rc_has_line_of_walk_rect(
    const RcWorldMap *map,
    int src_x, int src_y, int src_width, int src_height,
    int dest_x, int dest_y, int dest_width, int dest_height, int plane) {
    RcTileBounds source, destination;
    if (!rc_tile_bounds_from_origin_size(src_x, src_y, src_width, src_height,
                                         plane, &source)
            || !rc_tile_bounds_from_origin_size(dest_x, dest_y, dest_width,
                                                dest_height, plane,
                                                &destination)) {
        return false;
    }
    int x = line_coordinate(src_x, dest_x, src_width);
    int y = line_coordinate(src_y, dest_y, src_height);
    int end_x = line_coordinate(dest_x, src_x, dest_width);
    int end_y = line_coordinate(dest_y, src_y, dest_height);
    int delta_x = abs(end_x - x);
    int delta_y = abs(end_y - y);
    int step_x = x < end_x ? 1 : -1;
    int step_y = y < end_y ? 1 : -1;
    int error = delta_x - delta_y;

    while (x != end_x || y != end_y) {
        int twice_error = error * 2;
        int dx = 0;
        int dy = 0;
        if (twice_error > -delta_y) {
            error -= delta_y;
            dx = step_x;
        }
        if (twice_error < delta_x) {
            error += delta_x;
            dy = step_y;
        }
        if (!rc_can_move(map, x, y, dx, dy, plane)) return false;
        x += dx;
        y += dy;
    }
    return true;
}

RcRouteTarget rc_route_target_point(int x, int y) {
    RcRouteTarget target = {
        .x = x,
        .y = y,
        .width = 1,
        .height = 1,
        .min_distance = 0,
        .max_distance = 0,
        .kind = RC_ROUTE_REACH_EXACT,
        .allow_inside = true,
    };
    return target;
}

RcRouteTarget rc_route_target_rectangle(int x, int y, int width, int height,
                                        int min_distance, int max_distance,
                                        bool allow_inside, bool require_los) {
    RcRouteTarget target = {
        .x = x,
        .y = y,
        .width = width,
        .height = height,
        .min_distance = min_distance,
        .max_distance = max_distance,
        .kind = RC_ROUTE_REACH_RECTANGLE,
        .allow_inside = allow_inside,
        .require_los = require_los,
    };
    return target;
}

bool rc_route_status_admitted(RcRouteStatus status) {
    return status == RC_ROUTE_ALREADY_ARRIVED || status == RC_ROUTE_EXACT
        || status == RC_ROUTE_ALTERNATIVE || status == RC_ROUTE_PARTIAL;
}

bool rc_route_status_has_path(RcRouteStatus status) {
    return status == RC_ROUTE_EXACT || status == RC_ROUTE_ALTERNATIVE
        || status == RC_ROUTE_PARTIAL;
}

void rc_player_route_clear(RcPlayer *player, RcMovementResult result) {
    if (!player) return;
    player->route_len = 0;
    player->route_idx = 0;
    player->route_continue = false;
    player->route_status = result == RC_MOVEMENT_BLOCKED
        ? RC_ROUTE_BLOCKED : RC_ROUTE_FAILED;
    player->movement_result = result;
}

bool rc_player_route_admit(RcPlayer *player, const RcRoute *route,
                           const RcRouteTarget *target, int entity_width,
                           int entity_height, bool allow_alternative) {
    if (!player || !route || !target || entity_width <= 0
            || entity_height <= 0
            || !rc_route_status_admitted(route->status)) {
        return false;
    }

    player->route_len = 0;
    player->route_idx = 0;
    player->route_target = *target;
    player->route_entity_width = entity_width;
    player->route_entity_height = entity_height;
    player->route_allow_alternative = allow_alternative;
    player->route_continue = route->status == RC_ROUTE_PARTIAL;
    player->route_status = route->status;
    int count = route->length < RC_MAX_ROUTE ? route->length : RC_MAX_ROUTE;
    for (int i = 0; i < count; i++) {
        player->route_x[i] = route->waypoints_x[i];
        player->route_y[i] = route->waypoints_y[i];
    }
    player->route_len = count;
    player->movement_result = route->status == RC_ROUTE_ALREADY_ARRIVED
        ? RC_MOVEMENT_ARRIVED : RC_MOVEMENT_ROUTE_ADMITTED;
    return true;
}

static bool rectangles_overlap(int ax, int ay, int aw, int ah,
                               int bx, int by, int bw, int bh) {
    return ax < bx + bw && bx < ax + aw && ay < by + bh && by < ay + ah;
}

static int rectangle_gap(int ax, int ay, int aw, int ah,
                         int bx, int by, int bw, int bh, int *gap_x,
                         int *gap_y) {
    int dx = 0, dy = 0;
    if (ax + aw <= bx) dx = bx - (ax + aw - 1);
    else if (bx + bw <= ax) dx = ax - (bx + bw - 1);
    if (ay + ah <= by) dy = by - (ay + ah - 1);
    else if (by + bh <= ay) dy = ay - (by + bh - 1);
    if (gap_x) *gap_x = dx;
    if (gap_y) *gap_y = dy;
    return dx > dy ? dx : dy;
}

static bool rectangle_side_reached(const RcWorldMap *map, int plane,
                                   int sx, int sy, int sw, int sh,
                                   const RcRouteTarget *target) {
    int se = sx + sw;
    int sn = sy + sh;
    int de = target->x + target->width;
    int dn = target->y + target->height;
    if (se == target->x
            && !(target->block_access & RC_BLOCK_ACCESS_WEST)) {
        int from = sy > target->y ? sy : target->y;
        int to = sn < dn ? sn : dn;
        for (int side_y = from; side_y < to; side_y++)
            if (!(rc_get_flags(map, target->x, side_y, plane) & COL_WALL_W))
                return true;
    } else if (sx == de
            && !(target->block_access & RC_BLOCK_ACCESS_EAST)) {
        int from = sy > target->y ? sy : target->y;
        int to = sn < dn ? sn : dn;
        for (int side_y = from; side_y < to; side_y++)
            if (!(rc_get_flags(map, de - 1, side_y, plane) & COL_WALL_E))
                return true;
    } else if (sn == target->y
            && !(target->block_access & RC_BLOCK_ACCESS_SOUTH)) {
        int from = sx > target->x ? sx : target->x;
        int to = se < de ? se : de;
        for (int side_x = from; side_x < to; side_x++)
            if (!(rc_get_flags(map, side_x, target->y, plane) & COL_WALL_S))
                return true;
    } else if (sy == dn
            && !(target->block_access & RC_BLOCK_ACCESS_NORTH)) {
        int from = sx > target->x ? sx : target->x;
        int to = se < de ? se : de;
        for (int side_x = from; side_x < to; side_x++)
            if (!(rc_get_flags(map, side_x, dn - 1, plane) & COL_WALL_N))
                return true;
    }
    return false;
}

static bool wall_point_reached(const RcWorldMap *map, int plane, int sx,
                               int sy, const RcRouteTarget *target) {
    int dx = sx - target->x;
    int dy = sy - target->y;
    int rotation = target->rotation & 3;
    int shape = target->shape;
    if (dx == 0 && dy == 0) return true;
    if (shape == 1 || shape == 3) {
        static const int corner_x[4] = {-1, 1, 1, -1};
        static const int corner_y[4] = {1, 1, -1, -1};
        return dx == corner_x[rotation] && dy == corner_y[rotation];
    }
    if (shape == 9) {
        if (dx == 0 && dy == 1)
            return !(rc_get_flags(map, sx, sy, plane) & COL_WALL_S);
        if (dx == 0 && dy == -1)
            return !(rc_get_flags(map, sx, sy, plane) & COL_WALL_N);
        if (dx == -1 && dy == 0)
            return !(rc_get_flags(map, sx, sy, plane) & COL_WALL_E);
        if (dx == 1 && dy == 0)
            return !(rc_get_flags(map, sx, sy, plane) & COL_WALL_W);
        return false;
    }

    bool west = dx == -1 && dy == 0;
    bool east = dx == 1 && dy == 0;
    bool south = dx == 0 && dy == -1;
    bool north = dx == 0 && dy == 1;
    if (shape == 0) {
        if ((rotation == 0 && west) || (rotation == 1 && north)
                || (rotation == 2 && east) || (rotation == 3 && south))
            return true;
        if (north && (rotation == 0 || rotation == 2))
            return !(rc_get_flags(map, sx, sy, plane) & COL_BLOCK_N);
        if (south && (rotation == 0 || rotation == 2))
            return !(rc_get_flags(map, sx, sy, plane) & COL_BLOCK_S);
        if (west && (rotation == 1 || rotation == 3))
            return !(rc_get_flags(map, sx, sy, plane) & COL_BLOCK_W);
        if (east && (rotation == 1 || rotation == 3))
            return !(rc_get_flags(map, sx, sy, plane) & COL_BLOCK_E);
        return false;
    }
    if (shape == 2) {
        bool direct = (rotation == 0 && (west || north))
                   || (rotation == 1 && (north || east))
                   || (rotation == 2 && (east || south))
                   || (rotation == 3 && (south || west));
        if (direct) return true;
        if (west) return !(rc_get_flags(map, sx, sy, plane) & COL_BLOCK_W);
        if (east) return !(rc_get_flags(map, sx, sy, plane) & COL_BLOCK_E);
        if (south) return !(rc_get_flags(map, sx, sy, plane) & COL_BLOCK_S);
        if (north) return !(rc_get_flags(map, sx, sy, plane) & COL_BLOCK_N);
    }
    return false;
}

static bool wall_reached(const RcWorldMap *map, int plane, int sx, int sy,
                         int sw, int sh, const RcRouteTarget *target) {
    if (rectangles_overlap(sx, sy, sw, sh, target->x, target->y, 1, 1))
        return true;
    for (int edge_x = sx; edge_x < sx + sw; edge_x++) {
        if (wall_point_reached(map, plane, edge_x, sy, target)
                || (sh > 1 && wall_point_reached(
                    map, plane, edge_x, sy + sh - 1, target))) return true;
    }
    for (int edge_y = sy + 1; edge_y < sy + sh - 1; edge_y++) {
        if (wall_point_reached(map, plane, sx, edge_y, target)
                || (sw > 1 && wall_point_reached(
                    map, plane, sx + sw - 1, edge_y, target))) return true;
    }
    return false;
}

bool rc_route_target_reached(const RcWorldMap *map, int plane,
                                 int x, int y, int width, int height,
                                 const RcRouteTarget *target) {
    if (target->kind == RC_ROUTE_REACH_EXACT)
        return x == target->x && y == target->y;
    if (target->kind == RC_ROUTE_REACH_WALL)
        return wall_reached(map, plane, x, y, width, height, target);

    bool overlap = rectangles_overlap(x, y, width, height,
                                      target->x, target->y,
                                      target->width, target->height);
    if (overlap) return target->allow_inside && target->min_distance == 0;
    int gap = rectangle_gap(x, y, width, height,
                            target->x, target->y,
                            target->width, target->height, NULL, NULL);
    if (gap < target->min_distance || gap > target->max_distance)
        return false;
    if (target->max_distance == 1
            && !rectangle_side_reached(map, plane, x, y, width, height,
                                       target)) return false;
    if (target->require_line_of_walk
            && !rc_has_line_of_walk_rect(map, x, y, width, height,
                                          target->x, target->y,
                                          target->width, target->height, plane))
        return false;
    return !target->require_los
        || rc_has_los_rect(map, x, y, width, height,
                           target->x, target->y,
                           target->width, target->height, plane);
}

static bool route_target_valid(const RcRouteTarget *target, int plane) {
    RcTileBounds bounds;
    return target && target->kind <= RC_ROUTE_REACH_WALL
        && target->width > 0 && target->height > 0
        && target->min_distance >= 0
        && target->max_distance >= target->min_distance
        && rc_tile_bounds_from_origin_size(target->x, target->y,
                                           target->width, target->height,
                                           plane, &bounds);
}

static int alternative_score(int x, int y, int width, int height,
                             const RcRouteTarget *target) {
    int dx, dy;
    rectangle_gap(x, y, width, height, target->x, target->y,
                  target->width, target->height, &dx, &dy);
    if (dx > MAX_ALTERNATIVE_DISTANCE || dy > MAX_ALTERNATIVE_DISTANCE)
        return INT_MAX;
    return dx * dx + dy * dy;
}

static RcRoute build_route(int origin_x, int origin_y, int source_x,
                           int source_y, int end_x, int end_y,
                           RcRouteStatus terminal_status) {
    RcRoute route = {0};
    RcRouteScratch *scratch = &route_scratch;
    int local_source_x = source_x - origin_x;
    int local_source_y = source_y - origin_y;
    int local_x = end_x - origin_x;
    int local_y = end_y - origin_y;
    route.cost = scratch->distance[local_x][local_y];

    int reverse_x[RC_MAX_ROUTE], reverse_y[RC_MAX_ROUTE];
    int reverse_count = 0;
    int turn_count = 0;
    int8_t previous_x = 2, previous_y = 2;
    while (local_x != local_source_x || local_y != local_source_y) {
        int8_t parent_x = scratch->parent_x[local_x][local_y];
        int8_t parent_y = scratch->parent_y[local_x][local_y];
        if (parent_x != previous_x || parent_y != previous_y) {
            if (reverse_count == RC_MAX_ROUTE) {
                memmove(reverse_x, reverse_x + 1,
                        (RC_MAX_ROUTE - 1) * sizeof(*reverse_x));
                memmove(reverse_y, reverse_y + 1,
                        (RC_MAX_ROUTE - 1) * sizeof(*reverse_y));
                reverse_count--;
            }
            reverse_x[reverse_count] = local_x + origin_x;
            reverse_y[reverse_count] = local_y + origin_y;
            reverse_count++;
            turn_count++;
            previous_x = parent_x;
            previous_y = parent_y;
        }
        local_x += parent_x;
        local_y += parent_y;
    }

    route.length = reverse_count;
    for (int i = 0; i < reverse_count; i++) {
        route.waypoints_x[i] = reverse_x[reverse_count - i - 1];
        route.waypoints_y[i] = reverse_y[reverse_count - i - 1];
    }
    if (route.length > 0) {
        route.reached_x = route.waypoints_x[route.length - 1];
        route.reached_y = route.waypoints_y[route.length - 1];
        route.segment_cost = scratch->distance[route.reached_x - origin_x]
                                             [route.reached_y - origin_y];
    } else {
        route.reached_x = source_x;
        route.reached_y = source_y;
    }
    route.status = turn_count > RC_MAX_ROUTE
        ? RC_ROUTE_PARTIAL : terminal_status;
    return route;
}

RcRoute rc_find_route(const RcWorldMap *map, int start_x, int start_y,
                      int entity_width, int entity_height, int plane,
                      const RcRouteTarget *target, bool allow_alternative) {
    RcRoute route = {0};
    RcTileBounds source;
    if (!map || !rc_tile_bounds_from_origin_size(
            start_x, start_y, entity_width, entity_height, plane, &source)
            || !route_target_valid(target, plane)) {
        route.status = RC_ROUTE_FAILED;
        return route;
    }
    if (rc_route_target_reached(map, plane, start_x, start_y,
                             entity_width, entity_height, target)) {
        route.status = RC_ROUTE_ALREADY_ARRIVED;
        route.reached_x = start_x;
        route.reached_y = start_y;
        return route;
    }

    RcRouteScratch *scratch = &route_scratch;
    memset(scratch->visited, 0, sizeof(scratch->visited));
    int origin_x = start_x - SEARCH_HALF;
    int origin_y = start_y - SEARCH_HALF;
    int local_source_x = SEARCH_HALF;
    int local_source_y = SEARCH_HALF;
    scratch->visited[local_source_x][local_source_y] = 1;
    scratch->distance[local_source_x][local_source_y] = 0;
    int head = 0, tail = 1;
    scratch->queue_x[0] = local_source_x;
    scratch->queue_y[0] = local_source_y;

    int end_x = -1, end_y = -1;
    int best_x = -1, best_y = -1;
    int best_score = INT_MAX;
    int best_cost = INT_MAX;
    static const int step_x[8] = {0, 1, 0, -1, 1, 1, -1, -1};
    static const int step_y[8] = {1, 0, -1, 0, 1, -1, -1, 1};

    while (head < tail) {
        int local_x = scratch->queue_x[head];
        int local_y = scratch->queue_y[head];
        head++;
        int world_x = local_x + origin_x;
        int world_y = local_y + origin_y;
        if (rc_route_target_reached(map, plane, world_x, world_y,
                                 entity_width, entity_height, target)) {
            end_x = world_x;
            end_y = world_y;
            break;
        }
        if (allow_alternative) {
            int score = alternative_score(world_x, world_y,
                                          entity_width, entity_height,
                                          target);
            int cost = scratch->distance[local_x][local_y];
            if (score < best_score || (score == best_score && cost < best_cost)) {
                best_x = world_x;
                best_y = world_y;
                best_score = score;
                best_cost = cost;
            }
        }

        uint16_t next_distance = scratch->distance[local_x][local_y] + 1;
        for (int direction = 0; direction < 8; direction++) {
            int next_x = local_x + step_x[direction];
            int next_y = local_y + step_y[direction];
            if (next_x < 0 || next_x >= SEARCH_SIZE
                    || next_y < 0 || next_y >= SEARCH_SIZE
                    || scratch->visited[next_x][next_y]
                    || !rc_can_move_rect(map, world_x, world_y,
                                         entity_width, entity_height,
                                         step_x[direction], step_y[direction],
                                         plane)) continue;
            scratch->visited[next_x][next_y] = 1;
            scratch->parent_x[next_x][next_y] = (int8_t)-step_x[direction];
            scratch->parent_y[next_x][next_y] = (int8_t)-step_y[direction];
            scratch->distance[next_x][next_y] = next_distance;
            scratch->queue_x[tail] = (uint16_t)next_x;
            scratch->queue_y[tail] = (uint16_t)next_y;
            tail++;
        }
    }

    RcRouteStatus terminal_status = RC_ROUTE_EXACT;
    if (end_x < 0) {
        if (!allow_alternative || best_x < 0
                || (best_x == start_x && best_y == start_y)) {
            route.status = RC_ROUTE_BLOCKED;
            route.reached_x = start_x;
            route.reached_y = start_y;
            return route;
        }
        end_x = best_x;
        end_y = best_y;
        terminal_status = RC_ROUTE_ALTERNATIVE;
    }
    return build_route(origin_x, origin_y, start_x, start_y,
                       end_x, end_y, terminal_status);
}
