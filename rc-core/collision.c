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
    FILE *f = fopen(path, "rb");
    if (!f) return -1;

    uint32_t magic, version, row_count, region_count;
    if (!rc_read_exact(f, &magic, sizeof(magic), 1, path, "magic")
            || !rc_read_exact(f, &version, sizeof(version), 1, path, "version")
            || !rc_read_exact(f, &row_count, sizeof(row_count), 1, path, "row count")
            || !rc_read_exact(f, &region_count, sizeof(region_count), 1,
                              path, "region count")
            || magic != CTIL_MAGIC || version != CTIL_VERSION) {
        fclose(f);
        return -1;
    }

    RcCollisionRegion *regions = calloc(region_count, sizeof(*regions));
    int *index = malloc(sizeof(*index) * RC_MAPSQUARE_COUNT);
    if (!regions || !index) {
        free(regions);
        free(index);
        fclose(f);
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
            fclose(f);
            return -1;
        }
        if (!have_ms || ms != last_ms) {
            current++;
            if (current >= (int)region_count) {
                free(regions);
                free(index);
                fclose(f);
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
    fclose(f);

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
