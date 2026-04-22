#include "pathfinding.h"
#include <stdlib.h>

uint32_t rc_get_flags(const RcWorldMap *map, int x, int y, int plane) {
    // Convert world coords to region + local coords
    int region_x = x / RC_REGION_SIZE;
    int region_y = y / RC_REGION_SIZE;
    int local_x = x % RC_REGION_SIZE;
    int local_y = y % RC_REGION_SIZE;

    for (int i = 0; i < map->region_count; i++) {
        if (map->regions[i].loaded &&
            map->regions[i].region_x == region_x &&
            map->regions[i].region_y == region_y) {
            return map->regions[i].tiles[plane][local_x][local_y].collision_flags;
        }
    }
    return 0; // unloaded region = fully walkable (no collision data loaded)
}

bool rc_tile_blocked(const RcWorldMap *map, int x, int y, int plane) {
    uint32_t f = rc_get_flags(map, x, y, plane);
    return (f & (COL_LOC | COL_BLOCK_WALK)) != 0;
}

// Movement checks matching RSMod RouteFinding.kt routeFindSize1().
// For each direction, check the DESTINATION tile for the appropriate composite block flag.
// Diagonals also check the two adjacent cardinal tiles.
bool rc_can_move(const RcWorldMap *map, int x, int y, int dx, int dy, int plane) {
    int nx = x + dx, ny = y + dy;

    // Cardinals: check destination tile only
    if (dx == 0 && dy == 1)  // North
        return !(rc_get_flags(map, nx, ny, plane) & COL_BLOCK_N);
    if (dx == 0 && dy == -1) // South
        return !(rc_get_flags(map, nx, ny, plane) & COL_BLOCK_S);
    if (dx == 1 && dy == 0)  // East
        return !(rc_get_flags(map, nx, ny, plane) & COL_BLOCK_E);
    if (dx == -1 && dy == 0) // West
        return !(rc_get_flags(map, nx, ny, plane) & COL_BLOCK_W);

    // Diagonals: check destination + both adjacent cardinal tiles
    if (dx == 1 && dy == 1)  // NE
        return !(rc_get_flags(map, nx, ny, plane) & COL_BLOCK_NE)
            && !(rc_get_flags(map, nx, y,  plane) & COL_BLOCK_E)
            && !(rc_get_flags(map, x,  ny, plane) & COL_BLOCK_N);
    if (dx == -1 && dy == 1) // NW
        return !(rc_get_flags(map, nx, ny, plane) & COL_BLOCK_NW)
            && !(rc_get_flags(map, nx, y,  plane) & COL_BLOCK_W)
            && !(rc_get_flags(map, x,  ny, plane) & COL_BLOCK_N);
    if (dx == 1 && dy == -1) // SE
        return !(rc_get_flags(map, nx, ny, plane) & COL_BLOCK_SE)
            && !(rc_get_flags(map, nx, y,  plane) & COL_BLOCK_E)
            && !(rc_get_flags(map, x,  ny, plane) & COL_BLOCK_S);
    if (dx == -1 && dy == -1) // SW
        return !(rc_get_flags(map, nx, ny, plane) & COL_BLOCK_SW)
            && !(rc_get_flags(map, nx, y,  plane) & COL_BLOCK_W)
            && !(rc_get_flags(map, x,  ny, plane) & COL_BLOCK_S);

    return false;
}

bool rc_has_los(const RcWorldMap *map, int x0, int y0, int x1, int y1, int plane) {
    // Bresenham line — check proj blocker flags
    int dx = abs(x1 - x0), dy = abs(y1 - y0);
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx - dy;

    while (x0 != x1 || y0 != y1) {
        uint32_t flags = rc_get_flags(map, x0, y0, plane);
        if (flags & COL_PROJ_BLOCK_FULL) return false;

        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 < dx)  { err += dx; y0 += sy; }
    }
    return true;
}

RcRoute rc_find_path(const RcWorldMap *map, int start_x, int start_y,
                     int dest_x, int dest_y, int entity_size, int plane,
                     bool allow_alternative) {
    (void)entity_size; // TODO: multi-size entity pathfinding

    RcRoute route = {0};

    // BFS on collision grid
    // Search area: 128x128 centered on start
    #define SEARCH_SIZE 128
    #define SEARCH_HALF 64

    // Thread-local scratch — each thread has its own copy, so the RL
    // use case (one env per thread, thousands in parallel) is
    // race-free. ~320 KB per thread; reused across every pathfind call
    // on that thread (no per-call alloc).
    static _Thread_local int visited[SEARCH_SIZE][SEARCH_SIZE];
    static _Thread_local int dir_x[SEARCH_SIZE][SEARCH_SIZE];
    static _Thread_local int dir_y[SEARCH_SIZE][SEARCH_SIZE];
    static _Thread_local int queue_x[SEARCH_SIZE * SEARCH_SIZE];
    static _Thread_local int queue_y[SEARCH_SIZE * SEARCH_SIZE];

    for (int i = 0; i < SEARCH_SIZE; i++)
        for (int j = 0; j < SEARCH_SIZE; j++)
            visited[i][j] = 0;

    int origin_x = start_x - SEARCH_HALF;
    int origin_y = start_y - SEARCH_HALF;

    int sx = start_x - origin_x;
    int sy = start_y - origin_y;
    int dx = dest_x - origin_x;
    int dy = dest_y - origin_y;

    if (dx < 0 || dx >= SEARCH_SIZE || dy < 0 || dy >= SEARCH_SIZE) {
        route.success = false;
        return route;
    }

    visited[sx][sy] = 1;
    int head = 0, tail = 0;
    queue_x[tail] = sx;
    queue_y[tail] = sy;
    tail++;

    bool found = false;
    int best_x = sx, best_y = sy;
    int best_dist = abs(dx - sx) + abs(dy - sy);

    int dirs_x[] = {0, 1, 0, -1, 1, 1, -1, -1};
    int dirs_y[] = {1, 0, -1, 0, 1, -1, -1, 1};

    while (head < tail) {
        int cx = queue_x[head];
        int cy = queue_y[head];
        head++;

        if (cx == dx && cy == dy) { found = true; break; }

        for (int d = 0; d < 8; d++) {
            int nx = cx + dirs_x[d];
            int ny = cy + dirs_y[d];
            if (nx < 0 || nx >= SEARCH_SIZE || ny < 0 || ny >= SEARCH_SIZE) continue;
            if (visited[nx][ny]) continue;

            int world_x = cx + origin_x;
            int world_y = cy + origin_y;
            if (!rc_can_move(map, world_x, world_y, dirs_x[d], dirs_y[d], plane)) continue;

            visited[nx][ny] = 1;
            dir_x[nx][ny] = -dirs_x[d];
            dir_y[nx][ny] = -dirs_y[d];
            queue_x[tail] = nx;
            queue_y[tail] = ny;
            tail++;

            int dist = abs(dx - nx) + abs(dy - ny);
            if (dist < best_dist) {
                best_dist = dist;
                best_x = nx;
                best_y = ny;
            }
        }
    }

    int end_x, end_y;
    if (found) {
        end_x = dx; end_y = dy;
        route.success = true;
    } else if (allow_alternative) {
        end_x = best_x; end_y = best_y;
        route.success = false;
        route.alternative = true;
    } else {
        route.success = false;
        return route;
    }

    // Trace path backwards
    int path_x[RC_MAX_ROUTE], path_y[RC_MAX_ROUTE];
    int path_len = 0;
    int tx = end_x, ty = end_y;
    while ((tx != sx || ty != sy) && path_len < RC_MAX_ROUTE) {
        path_x[path_len] = tx + origin_x;
        path_y[path_len] = ty + origin_y;
        path_len++;
        int bx = dir_x[tx][ty];
        int by = dir_y[tx][ty];
        tx += bx;
        ty += by;
    }

    // Reverse into route
    route.length = path_len;
    for (int i = 0; i < path_len; i++) {
        route.waypoints_x[i] = path_x[path_len - 1 - i];
        route.waypoints_y[i] = path_y[path_len - 1 - i];
    }

    return route;
    #undef SEARCH_SIZE
    #undef SEARCH_HALF
}
