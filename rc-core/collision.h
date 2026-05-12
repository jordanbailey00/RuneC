#ifndef RC_COLLISION_H
#define RC_COLLISION_H

#include "types.h"

typedef struct {
    uint16_t mapsquare;
    uint32_t flags[RC_MAX_PLANES][RC_REGION_SIZE][RC_REGION_SIZE];
} RcCollisionRegion;

extern RcCollisionRegion *g_rc_collision_regions;
extern int g_rc_collision_region_count;

int rc_load_collision_tiles(const char *path);
int rc_collision_is_loaded(void);
uint32_t rc_collision_flags_at(int x, int y, int plane, int *found);

#endif
