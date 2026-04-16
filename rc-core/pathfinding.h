#ifndef RC_PATHFINDING_H
#define RC_PATHFINDING_H

#include "types.h"

// BFS pathfinding with directional collision flags
RcRoute rc_find_path(const RcWorldMap *map, int start_x, int start_y,
                     int dest_x, int dest_y, int entity_size, int plane,
                     bool allow_alternative);

// Collision queries
bool rc_can_move(const RcWorldMap *map, int x, int y, int dx, int dy, int plane);
bool rc_tile_blocked(const RcWorldMap *map, int x, int y, int plane);

// Line of sight (Bresenham)
bool rc_has_los(const RcWorldMap *map, int x0, int y0, int x1, int y1, int plane);

// Get collision flags for a world tile
uint32_t rc_get_flags(const RcWorldMap *map, int x, int y, int plane);

#endif
