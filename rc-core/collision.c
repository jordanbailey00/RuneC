#include "collision.h"
#include "io.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CTIL_MAGIC 0x4C495443u
#define CTIL_VERSION 1u
#define RC_MAPSQUARE_COUNT 65536

RcCollisionRegion *g_rc_collision_regions = NULL;
int g_rc_collision_region_count = 0;

static int g_collision_index[RC_MAPSQUARE_COUNT];

static void reset_index(int *index) {
    for (int i = 0; i < RC_MAPSQUARE_COUNT; i++) index[i] = -1;
}

int rc_load_collision_tiles(const char *path) {
    if (!path) return -1;
    FILE *f = rc_asset_fopen(path, "rb");
    if (!f) return -1;

    uint32_t magic, version, row_count, region_count;
    if (!rc_read_exact(f, &magic, sizeof(magic), 1, path, "magic")
            || !rc_read_exact(f, &version, sizeof(version), 1, path, "version")
            || !rc_read_exact(f, &row_count, sizeof(row_count), 1, path, "row count")
            || !rc_read_exact(f, &region_count, sizeof(region_count), 1,
                              path, "region count")
            || magic != CTIL_MAGIC || version != CTIL_VERSION) {
        rc_asset_close(f);
        return -1;
    }

    RcCollisionRegion *regions = calloc(region_count, sizeof(*regions));
    int *index = malloc(sizeof(*index) * RC_MAPSQUARE_COUNT);
    if (!regions || !index) {
        free(regions);
        free(index);
        rc_asset_close(f);
        return -1;
    }
    reset_index(index);

    uint16_t last_ms = 0;
    int have_ms = 0;
    int current = -1;
    for (uint32_t i = 0; i < row_count; i++) {
        uint16_t ms;
        uint8_t lx, ly, plane, pad;
        uint32_t flags;
        if (!rc_read_exact(f, &ms, sizeof(ms), 1, path, "mapsquare")
                || !rc_read_exact(f, &lx, sizeof(lx), 1, path, "local x")
                || !rc_read_exact(f, &ly, sizeof(ly), 1, path, "local y")
                || !rc_read_exact(f, &plane, sizeof(plane), 1, path, "plane")
                || !rc_read_exact(f, &pad, sizeof(pad), 1, path, "pad")
                || !rc_read_exact(f, &flags, sizeof(flags), 1, path, "flags")) {
            free(regions);
            free(index);
            rc_asset_close(f);
            return -1;
        }
        if (!have_ms || ms != last_ms) {
            current++;
            if (current >= (int)region_count) {
                free(regions);
                free(index);
                rc_asset_close(f);
                return -1;
            }
            regions[current].mapsquare = ms;
            index[ms] = current;
            last_ms = ms;
            have_ms = 1;
        }
        if (plane < RC_MAX_PLANES && lx < RC_REGION_SIZE && ly < RC_REGION_SIZE) {
            regions[current].flags[plane][lx][ly] = flags;
        }
    }
    rc_asset_close(f);

    free(g_rc_collision_regions);
    g_rc_collision_regions = regions;
    g_rc_collision_region_count = current + 1;
    memcpy(g_collision_index, index, sizeof(g_collision_index));
    free(index);
    return g_rc_collision_region_count;
}

int rc_collision_is_loaded(void) {
    return g_rc_collision_regions && g_rc_collision_region_count > 0;
}

int rc_collision_populate_map_rect(RcWorldMap *map, int min_x, int min_y,
                                   int max_x, int max_y) {
    if (!map || min_x > max_x || min_y > max_y || !rc_collision_is_loaded())
        return -1;

    int min_rx = min_x / RC_REGION_SIZE;
    int max_rx = max_x / RC_REGION_SIZE;
    int min_ry = min_y / RC_REGION_SIZE;
    int max_ry = max_y / RC_REGION_SIZE;

    memset(map, 0, sizeof(*map));
    map->base_region_x = min_rx;
    map->base_region_y = min_ry;

    for (int rx = min_rx; rx <= max_rx; rx++) {
        for (int ry = min_ry; ry <= max_ry; ry++) {
            if (map->region_count >= RC_MAX_REGIONS)
                return -1;

            uint16_t ms = (uint16_t)((rx << 8) | (ry & 0xFF));
            int idx = g_collision_index[ms];
            if (idx < 0 || idx >= g_rc_collision_region_count)
                continue;

            RcRegion *dst = &map->regions[map->region_count++];
            dst->region_x = rx;
            dst->region_y = ry;
            dst->loaded = 1;
            for (int plane = 0; plane < RC_MAX_PLANES; plane++) {
                for (int x = 0; x < RC_REGION_SIZE; x++) {
                    for (int y = 0; y < RC_REGION_SIZE; y++) {
                        dst->tiles[plane][x][y].collision_flags =
                            g_rc_collision_regions[idx].flags[plane][x][y];
                    }
                }
            }
        }
    }
    return map->region_count;
}

uint32_t rc_collision_flags_at(int x, int y, int plane, int *found) {
    if (found) *found = 0;
    if (!rc_collision_is_loaded() || x < 0 || y < 0
            || plane < 0 || plane >= RC_MAX_PLANES) {
        return 0;
    }
    uint16_t ms = (uint16_t)(((x >> 6) << 8) | (y >> 6));
    int idx = g_collision_index[ms];
    if (idx < 0 || idx >= g_rc_collision_region_count) return 0;
    if (found) *found = 1;
    return g_rc_collision_regions[idx].flags[plane][x & 63][y & 63];
}
