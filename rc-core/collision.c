#include "collision.h"
#include "io.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CTIL_MAGIC 0x4C495443u
#define CTIL_VERSION 1u

RcCollisionRegion *g_rc_collision_regions = NULL;
int g_rc_collision_region_count = 0;

static int g_collision_index[RC_COLLISION_MAPSQUARE_COUNT];
static const RcCollisionRegion *g_active_collision_regions = NULL;
static const int *g_active_collision_index = g_collision_index;
static int g_active_collision_region_count = 0;

static void reset_index(int *index) {
    for (int i = 0; i < RC_COLLISION_MAPSQUARE_COUNT; i++) index[i] = -1;
}

void rc_collision_data_init(RcCollisionData *data) {
    if (!data) return;
    data->regions = NULL;
    data->region_count = 0;
    reset_index(data->index);
}

void rc_collision_data_free(RcCollisionData *data) {
    if (!data) return;
    free(data->regions);
    rc_collision_data_init(data);
}

void rc_collision_use_data(const RcCollisionData *data) {
    if (!data) {
        g_active_collision_regions = g_rc_collision_regions;
        g_active_collision_region_count = g_rc_collision_region_count;
        g_active_collision_index = g_collision_index;
        return;
    }
    g_active_collision_regions = data->regions;
    g_active_collision_region_count = data->region_count;
    g_active_collision_index = data->index;
}

void rc_collision_reset_data_if_active(const RcCollisionData *data) {
    if (data && g_active_collision_regions == data->regions) {
        rc_collision_use_data(NULL);
    }
}

int rc_load_collision_tiles_into(const char *path, RcCollisionData *data) {
    if (!path || !data) return -1;
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
    int *index = malloc(sizeof(*index) * RC_COLLISION_MAPSQUARE_COUNT);
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

    free(data->regions);
    data->regions = regions;
    data->region_count = current + 1;
    memcpy(data->index, index, sizeof(data->index));
    free(index);
    return data->region_count;
}

int rc_collision_mirror_to_globals(const RcCollisionData *data) {
    if (!data) return 0;
    RcCollisionRegion *regions = NULL;
    if (data->region_count > 0) {
        if (!data->regions) return 0;
        regions = malloc((size_t)data->region_count * sizeof(*regions));
        if (!regions) return 0;
        memcpy(regions, data->regions,
               (size_t)data->region_count * sizeof(*regions));
    }
    free(g_rc_collision_regions);
    g_rc_collision_regions = regions;
    g_rc_collision_region_count = data->region_count;
    memcpy(g_collision_index, data->index, sizeof(g_collision_index));
    return 1;
}

int rc_load_collision_tiles(const char *path) {
    RcCollisionData data;
    rc_collision_data_init(&data);
    int loaded = rc_load_collision_tiles_into(path, &data);
    if (loaded >= 0 && !rc_collision_mirror_to_globals(&data)) loaded = -1;
    rc_collision_data_free(&data);
    if (loaded >= 0) rc_collision_use_data(NULL);
    return loaded;
}

int rc_collision_is_loaded(void) {
    const RcCollisionRegion *regions = g_active_collision_regions
                                     ? g_active_collision_regions
                                     : g_rc_collision_regions;
    int count = g_active_collision_regions ? g_active_collision_region_count
                                           : g_rc_collision_region_count;
    return regions && count > 0;
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
            const int *index = g_active_collision_index
                             ? g_active_collision_index : g_collision_index;
            const RcCollisionRegion *regions = g_active_collision_regions
                                             ? g_active_collision_regions
                                             : g_rc_collision_regions;
            int region_count = g_active_collision_regions
                             ? g_active_collision_region_count
                             : g_rc_collision_region_count;
            int idx = index[ms];
            if (idx < 0 || idx >= region_count)
                continue;

            RcRegion *dst = &map->regions[map->region_count++];
            dst->region_x = rx;
            dst->region_y = ry;
            dst->loaded = 1;
            for (int plane = 0; plane < RC_MAX_PLANES; plane++) {
                for (int x = 0; x < RC_REGION_SIZE; x++) {
                    for (int y = 0; y < RC_REGION_SIZE; y++) {
                        dst->tiles[plane][x][y].collision_flags =
                            regions[idx].flags[plane][x][y];
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
    const int *index = g_active_collision_index
                     ? g_active_collision_index : g_collision_index;
    const RcCollisionRegion *regions = g_active_collision_regions
                                     ? g_active_collision_regions
                                     : g_rc_collision_regions;
    int region_count = g_active_collision_regions
                     ? g_active_collision_region_count
                     : g_rc_collision_region_count;
    int idx = index[ms];
    if (idx < 0 || idx >= region_count) return 0;
    if (found) *found = 1;
    return regions[idx].flags[plane][x & 63][y & 63];
}
