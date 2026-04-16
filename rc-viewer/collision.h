// Loads collision map from .cmap binary into RcWorldMap regions.
// Format: uint32 magic (0x434D4150), uint32 version, uint32 region_count
// Per region: int32 mapsquare, int32[4][64][64] flags

#ifndef RC_COLLISION_H
#define RC_COLLISION_H

#include "../rc-core/types.h"
#include <stdio.h>
#include <stdlib.h>

#define CMAP_MAGIC 0x434D4150

// Loads collision regions as-is (world coordinates).
// Player must use world coords for pathfinding, local coords for rendering.
static int collision_load(RcWorldMap *map, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "collision: can't open %s\n", path); return -1; }

    uint32_t magic, version, count;
    fread(&magic, 4, 1, f);
    if (magic != CMAP_MAGIC) { fprintf(stderr, "collision: bad magic\n"); fclose(f); return -1; }
    fread(&version, 4, 1, f);
    fread(&count, 4, 1, f);

    int loaded = 0;
    for (uint32_t i = 0; i < count && map->region_count < RC_MAX_REGIONS; i++) {
        int32_t mapsquare;
        fread(&mapsquare, 4, 1, f);
        int rx = (mapsquare >> 8) & 0xFF;
        int ry = mapsquare & 0xFF;

        RcRegion *reg = &map->regions[map->region_count];
        reg->region_x = rx;
        reg->region_y = ry;
        reg->loaded = 1;

        for (int h = 0; h < RC_MAX_PLANES; h++) {
            for (int x = 0; x < RC_REGION_SIZE; x++) {
                for (int y = 0; y < RC_REGION_SIZE; y++) {
                    int32_t flags;
                    fread(&flags, 4, 1, f);
                    reg->tiles[h][x][y].collision_flags = (uint32_t)flags;
                }
            }
        }
        map->region_count++;
        loaded++;
    }
    fclose(f);
    fprintf(stderr, "collision: loaded %d regions from %s\n", loaded, path);
    return loaded;
}

#endif
