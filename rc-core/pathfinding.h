#ifndef RC_PATHFINDING_H
#define RC_PATHFINDING_H

#include "types.h"

RcRouteTarget rc_route_target_point(int x, int y);
RcRouteTarget rc_route_target_rectangle(int x, int y, int width, int height,
                                        int min_distance, int max_distance,
                                        bool allow_inside, bool require_los);

// One bounded BFS for point, footprint, wall-shape, and ranged destinations.
RcRoute rc_find_route(const RcWorldMap *map, int start_x, int start_y,
                      int entity_width, int entity_height, int plane,
                      const RcRouteTarget *target, bool allow_alternative);
bool rc_route_status_admitted(RcRouteStatus status);
bool rc_route_status_has_path(RcRouteStatus status);
bool rc_route_target_reached(const RcWorldMap *map, int plane,
                             int x, int y, int width, int height,
                             const RcRouteTarget *target);
void rc_player_route_clear(RcPlayer *player, RcMovementResult result);
bool rc_player_route_admit(RcPlayer *player, const RcRoute *route,
                           const RcRouteTarget *target, int entity_width,
                           int entity_height, bool allow_alternative);

// Collision queries
bool rc_can_move(const RcWorldMap *map, int x, int y, int dx, int dy, int plane);
bool rc_can_move_rect(const RcWorldMap *map, int x, int y,
                      int width, int height, int dx, int dy, int plane);
bool rc_tile_blocked(const RcWorldMap *map, int x, int y, int plane);

bool rc_has_los(const RcWorldMap *map, int x0, int y0, int x1, int y1, int plane);
bool rc_has_los_rect(const RcWorldMap *map,
                     int src_x, int src_y, int src_width, int src_height,
                     int dest_x, int dest_y, int dest_width, int dest_height,
                     int plane);
bool rc_has_line_of_walk_rect(
    const RcWorldMap *map,
    int src_x, int src_y, int src_width, int src_height,
    int dest_x, int dest_y, int dest_width, int dest_height, int plane);

// Get collision flags for a world tile
uint32_t rc_get_flags(const RcWorldMap *map, int x, int y, int plane);
// Whether the map holds the tile's region, so its flags include runtime
// loc changes; elsewhere only the static collision data is known.
bool rc_map_tile_loaded(const RcWorldMap *map, int x, int y, int plane);

#endif
