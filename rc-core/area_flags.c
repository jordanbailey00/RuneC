#include "area_flags.h"
#include "io.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#define AFLG_MAGIC 0x474C4641u
#define AFLG_VERSION 1u

RcAreaFlagRow *g_rc_area_flags = NULL;
int g_rc_area_flag_count = 0;

static RcAreaPoint *g_rc_area_points = NULL;
static uint32_t *g_area_refs = NULL;
static RcAreaRange g_area_index[RC_AREA_MAPSQUARE_COUNT];
static RcAreaTileRegion *g_area_tiles = NULL;
static int g_area_tile_count = 0;
static int g_area_tile_index[RC_AREA_MAPSQUARE_COUNT];
static const RcAreaFlagRow *g_active_area_flags = NULL;
static int g_active_area_flag_count = 0;
static const uint32_t *g_active_area_refs = NULL;
static const RcAreaRange *g_active_area_index = g_area_index;
static const RcAreaTileRegion *g_active_area_tiles = NULL;
static int g_active_area_tile_count = 0;
static const int *g_active_area_tile_index = g_area_tile_index;

static void reset_index_into(RcAreaFlagData *data) {
    if (!data) return;
    memset(data->index, 0, sizeof(data->index));
    free(data->refs);
    data->refs = NULL;
    data->ref_count = 0;
    free(data->tiles);
    data->tiles = NULL;
    data->tile_count = 0;
    for (int i = 0; i < RC_AREA_MAPSQUARE_COUNT; i++) {
        data->tile_index[i] = -1;
    }
}

void rc_area_flag_data_init(RcAreaFlagData *data) {
    if (!data) return;
    data->rows = NULL;
    data->points = NULL;
    data->point_count = 0;
    data->refs = NULL;
    data->ref_count = 0;
    data->tiles = NULL;
    data->tile_count = 0;
    data->row_count = 0;
    memset(data->index, 0, sizeof(data->index));
    for (int i = 0; i < RC_AREA_MAPSQUARE_COUNT; i++)
        data->tile_index[i] = -1;
}

void rc_area_flag_data_free(RcAreaFlagData *data) {
    if (!data) return;
    free(data->rows);
    free(data->points);
    free(data->refs);
    free(data->tiles);
    rc_area_flag_data_init(data);
}

void rc_area_flags_use_data(const RcAreaFlagData *data) {
    if (!data) {
        g_active_area_flags = g_rc_area_flags;
        g_active_area_flag_count = g_rc_area_flag_count;
        g_active_area_refs = g_area_refs;
        g_active_area_index = g_area_index;
        g_active_area_tiles = g_area_tiles;
        g_active_area_tile_count = g_area_tile_count;
        g_active_area_tile_index = g_area_tile_index;
        return;
    }
    g_active_area_flags = data->rows;
    g_active_area_flag_count = data->row_count;
    g_active_area_refs = data->refs;
    g_active_area_index = data->index;
    g_active_area_tiles = data->tiles;
    g_active_area_tile_count = data->tile_count;
    g_active_area_tile_index = data->tile_index;
}

void rc_area_flags_reset_data_if_active(const RcAreaFlagData *data) {
    if (data && g_active_area_flags == data->rows) {
        rc_area_flags_use_data(NULL);
    }
}

static uint16_t mapsquare_for(int x, int y) {
    return (uint16_t)(((x >> 6) << 8) | (y >> 6));
}

static void free_area_data(RcAreaFlagRow *rows, RcAreaPoint *points) {
    free(rows);
    free(points);
}

static int on_segment(const RcAreaPoint *a, const RcAreaPoint *b, int x, int y) {
    int64_t cross = (int64_t)(x - a->x) * (int64_t)(b->y - a->y)
                  - (int64_t)(y - a->y) * (int64_t)(b->x - a->x);
    if (cross != 0) return 0;
    return x >= (a->x < b->x ? a->x : b->x)
        && x <= (a->x > b->x ? a->x : b->x)
        && y >= (a->y < b->y ? a->y : b->y)
        && y <= (a->y > b->y ? a->y : b->y);
}

static int point_in_row(const RcAreaFlagRow *row, int x, int y) {
    if (x < row->min_x || x > row->max_x || y < row->min_y || y > row->max_y) {
        return 0;
    }
    int inside = 0;
    for (int i = 0, j = row->vertex_count - 1; i < row->vertex_count; j = i++) {
        const RcAreaPoint *a = &row->points[i];
        const RcAreaPoint *b = &row->points[j];
        if (on_segment(a, b, x, y)) return 1;
        int yi = a->y, yj = b->y;
        if ((yi > y) != (yj > y)) {
            int64_t lhs = (int64_t)(b->x - a->x) * (int64_t)(y - yi);
            int64_t rhs = (int64_t)(x - a->x) * (int64_t)(yj - yi);
            if ((yj > yi && lhs > rhs) || (yj < yi && lhs < rhs)) {
                inside = !inside;
            }
        }
    }
    return inside;
}

static int build_index(RcAreaFlagData *data) {
    if (!data) return 0;
    reset_index_into(data);
    if (!data->rows || data->row_count <= 0) return 1;

    uint32_t *counts = calloc(RC_AREA_MAPSQUARE_COUNT, sizeof(*counts));
    if (!counts) return 0;
    uint32_t total = 0;
    for (int i = 0; i < data->row_count; i++) {
        const RcAreaFlagRow *row = &data->rows[i];
        int min_rx = row->min_x >> 6, max_rx = row->max_x >> 6;
        int min_ry = row->min_y >> 6, max_ry = row->max_y >> 6;
        for (int rx = min_rx; rx <= max_rx && rx < 256; rx++) {
            if (rx < 0) continue;
            for (int ry = min_ry; ry <= max_ry && ry < 256; ry++) {
                if (ry < 0) continue;
                counts[(uint16_t)((rx << 8) | ry)]++;
                total++;
            }
        }
    }
    data->refs = calloc(total ? total : 1u, sizeof(*data->refs));
    if (!data->refs) {
        free(counts);
        return 0;
    }
    data->ref_count = total;
    uint32_t first = 0;
    for (int ms = 0; ms < RC_AREA_MAPSQUARE_COUNT; ms++) {
        data->index[ms].first = first;
        data->index[ms].count = counts[ms];
        first += counts[ms];
        counts[ms] = 0;
    }
    for (int i = 0; i < data->row_count; i++) {
        const RcAreaFlagRow *row = &data->rows[i];
        int min_rx = row->min_x >> 6, max_rx = row->max_x >> 6;
        int min_ry = row->min_y >> 6, max_ry = row->max_y >> 6;
        for (int rx = min_rx; rx <= max_rx && rx < 256; rx++) {
            if (rx < 0) continue;
            for (int ry = min_ry; ry <= max_ry && ry < 256; ry++) {
                if (ry < 0) continue;
                uint16_t ms = (uint16_t)((rx << 8) | ry);
                uint32_t slot = data->index[ms].first + counts[ms]++;
                data->refs[slot] = (uint32_t)i;
            }
        }
    }
    int tile_count = 0;
    for (int ms = 0; ms < RC_AREA_MAPSQUARE_COUNT; ms++) {
        if (data->index[ms].count) tile_count++;
    }
    data->tiles = calloc(tile_count ? tile_count : 1u,
                         sizeof(*data->tiles));
    if (!data->tiles) {
        free(counts);
        return 0;
    }
    data->tile_count = tile_count;
    int tile_idx = 0;
    for (int ms = 0; ms < RC_AREA_MAPSQUARE_COUNT; ms++) {
        if (!data->index[ms].count) continue;
        data->tile_index[ms] = tile_idx;
        data->tiles[tile_idx++].mapsquare = (uint16_t)ms;
    }
    free(counts);
    return 1;
}

static void raster_row(RcAreaFlagData *data, const RcAreaFlagRow *row,
                       int clear) {
    if (!data || !row) return;
    uint8_t bits = (uint8_t)(clear ? row->clear_flags : row->set_flags);
    if (!bits) return;
    int min_rx = row->min_x >> 6, max_rx = row->max_x >> 6;
    int min_ry = row->min_y >> 6, max_ry = row->max_y >> 6;
    for (int rx = min_rx; rx <= max_rx && rx < 256; rx++) {
        if (rx < 0) continue;
        for (int ry = min_ry; ry <= max_ry && ry < 256; ry++) {
            if (ry < 0) continue;
            uint16_t ms = (uint16_t)((rx << 8) | ry);
            int idx = data->tile_index[ms];
            if (idx < 0) continue;
            RcAreaTileRegion *tile = &data->tiles[idx];
            int lx0 = rx == min_rx ? (row->min_x & 63) : 0;
            int ly0 = ry == min_ry ? (row->min_y & 63) : 0;
            int lx1 = rx == max_rx ? (row->max_x & 63) : 63;
            int ly1 = ry == max_ry ? (row->max_y & 63) : 63;
            for (int lx = lx0; lx <= lx1; lx++) {
                int x = (rx << 6) + lx;
                for (int ly = ly0; ly <= ly1; ly++) {
                    int y = (ry << 6) + ly;
                    if (!point_in_row(row, x, y)) continue;
                    if (clear) tile->flags[row->plane][lx][ly] &= (uint8_t)~bits;
                    else tile->flags[row->plane][lx][ly] |= bits;
                }
            }
        }
    }
}

static void raster_area_flags(RcAreaFlagData *data) {
    if (!data) return;
    for (int i = 0; i < data->row_count; i++) {
        raster_row(data, &data->rows[i], 0);
    }
    for (int i = 0; i < data->row_count; i++) {
        raster_row(data, &data->rows[i], 1);
    }
}

int rc_load_area_flags_into(const char *path, RcAreaFlagData *data) {
    if (!path || !data) return -1;
    FILE *f = rc_asset_fopen(path, "rb");
    if (!f) return -1;

    uint32_t magic, version, row_count, point_count;
    if (!rc_read_exact(f, &magic, sizeof(magic), 1, path, "magic")
            || !rc_read_exact(f, &version, sizeof(version), 1, path, "version")
            || !rc_read_exact(f, &row_count, sizeof(row_count), 1, path, "rows")
            || !rc_read_exact(f, &point_count, sizeof(point_count), 1, path,
                              "points")
            || magic != AFLG_MAGIC || version != AFLG_VERSION) {
        rc_asset_close(f);
        return -1;
    }

    RcAreaFlagRow *rows = calloc(row_count ? row_count : 1u, sizeof(*rows));
    RcAreaPoint *points = calloc(point_count ? point_count : 1u,
                                 sizeof(*points));
    if (!rows || !points) {
        free_area_data(rows, points);
        rc_asset_close(f);
        return -1;
    }

    uint32_t point_pos = 0;
    for (uint32_t i = 0; i < row_count; i++) {
        RcAreaFlagRow *row = &rows[i];
        if (!rc_read_exact(f, &row->plane, sizeof(row->plane), 1, path, "plane")
                || !rc_read_exact(f, &row->authoritative_osrs,
                                  sizeof(row->authoritative_osrs), 1, path,
                                  "authority")
                || !rc_read_exact(f, &row->vertex_count,
                                  sizeof(row->vertex_count), 1, path,
                                  "vertex count")
                || !rc_read_exact(f, &row->set_flags, sizeof(row->set_flags),
                                  1, path, "set flags")
                || !rc_read_exact(f, &row->clear_flags,
                                  sizeof(row->clear_flags), 1, path,
                                  "clear flags")
                || !rc_read_exact(f, &row->min_x, sizeof(row->min_x), 1,
                                  path, "min x")
                || !rc_read_exact(f, &row->min_y, sizeof(row->min_y), 1,
                                  path, "min y")
                || !rc_read_exact(f, &row->max_x, sizeof(row->max_x), 1,
                                  path, "max x")
                || !rc_read_exact(f, &row->max_y, sizeof(row->max_y), 1,
                                  path, "max y")
                || !rc_read_exact(f, &row->value, sizeof(row->value), 1,
                                  path, "value")
                || !rc_read_exact(f, &row->source_id, sizeof(row->source_id),
                                  1, path, "source id")) {
            free_area_data(rows, points);
            rc_asset_close(f);
            return -1;
        }
        if (point_pos + row->vertex_count > point_count
                || row->plane >= 4 || row->vertex_count < 3) {
            free_area_data(rows, points);
            rc_asset_close(f);
            return -1;
        }
        row->points = &points[point_pos];
        if (!rc_read_exact(f, row->points, sizeof(*points), row->vertex_count,
                           path, "points")) {
            free_area_data(rows, points);
            rc_asset_close(f);
            return -1;
        }
        point_pos += row->vertex_count;
    }
    rc_asset_close(f);
    if (point_pos != point_count) {
        free_area_data(rows, points);
        return -1;
    }

    free(data->rows);
    free(data->points);
    data->rows = rows;
    data->points = points;
    data->point_count = point_count;
    data->row_count = (int)row_count;
    if (!build_index(data)) {
        free_area_data(data->rows, data->points);
        data->rows = NULL;
        data->points = NULL;
        data->point_count = 0;
        data->row_count = 0;
        return -1;
    }
    raster_area_flags(data);
    return data->row_count;
}

static int copy_area_rows_and_points(const RcAreaFlagData *data,
                                     RcAreaFlagRow **out_rows,
                                     RcAreaPoint **out_points) {
    *out_rows = NULL;
    *out_points = NULL;
    if (!data) return 0;
    if (data->row_count > 0) {
        if (!data->rows) return 0;
        *out_rows = malloc((size_t)data->row_count * sizeof(**out_rows));
        if (!*out_rows) return 0;
        memcpy(*out_rows, data->rows,
               (size_t)data->row_count * sizeof(**out_rows));
    }
    if (data->point_count > 0) {
        if (!data->points) {
            free(*out_rows);
            *out_rows = NULL;
            return 0;
        }
        *out_points = malloc((size_t)data->point_count * sizeof(**out_points));
        if (!*out_points) {
            free(*out_rows);
            *out_rows = NULL;
            return 0;
        }
        memcpy(*out_points, data->points,
               (size_t)data->point_count * sizeof(**out_points));
        for (int i = 0; i < data->row_count; i++) {
            if (!data->rows[i].points) {
                (*out_rows)[i].points = NULL;
                continue;
            }
            ptrdiff_t offset = data->rows[i].points - data->points;
            if (offset < 0 || (uint32_t)offset >= data->point_count) {
                free(*out_rows);
                free(*out_points);
                *out_rows = NULL;
                *out_points = NULL;
                return 0;
            }
            (*out_rows)[i].points = *out_points + offset;
        }
    }
    return 1;
}

int rc_area_flags_mirror_to_globals(const RcAreaFlagData *data) {
    if (!data) return 0;
    RcAreaFlagRow *rows = NULL;
    RcAreaPoint *points = NULL;
    uint32_t *refs = NULL;
    RcAreaTileRegion *tiles = NULL;
    if (!copy_area_rows_and_points(data, &rows, &points)) return 0;
    if (data->ref_count > 0) {
        if (!data->refs) goto fail;
        refs = malloc((size_t)data->ref_count * sizeof(*refs));
        if (!refs) goto fail;
        memcpy(refs, data->refs, (size_t)data->ref_count * sizeof(*refs));
    }
    if (data->tile_count > 0) {
        if (!data->tiles) goto fail;
        tiles = malloc((size_t)data->tile_count * sizeof(*tiles));
        if (!tiles) goto fail;
        memcpy(tiles, data->tiles, (size_t)data->tile_count * sizeof(*tiles));
    }
    free(g_rc_area_flags);
    free(g_rc_area_points);
    free(g_area_refs);
    free(g_area_tiles);
    g_rc_area_flags = rows;
    g_rc_area_points = points;
    g_area_refs = refs;
    g_area_tiles = tiles;
    g_rc_area_flag_count = data->row_count;
    g_area_tile_count = data->tile_count;
    memcpy(g_area_index, data->index, sizeof(g_area_index));
    memcpy(g_area_tile_index, data->tile_index, sizeof(g_area_tile_index));
    return 1;

fail:
    free(rows);
    free(points);
    free(refs);
    free(tiles);
    return 0;
}

int rc_load_area_flags(const char *path) {
    RcAreaFlagData data;
    rc_area_flag_data_init(&data);
    int loaded = rc_load_area_flags_into(path, &data);
    if (loaded >= 0 && !rc_area_flags_mirror_to_globals(&data)) loaded = -1;
    rc_area_flag_data_free(&data);
    if (loaded >= 0) rc_area_flags_use_data(NULL);
    return loaded;
}

int rc_area_flags_is_loaded(void) {
    const RcAreaFlagRow *rows = g_active_area_flags
                              ? g_active_area_flags : g_rc_area_flags;
    int count = g_active_area_flags ? g_active_area_flag_count
                                    : g_rc_area_flag_count;
    return rows && count > 0;
}

static int row_matches(const RcAreaFlagRow *row, int x, int y, int plane) {
    return row->plane == (uint8_t)plane && point_in_row(row, x, y);
}

uint32_t rc_area_flags_at(int x, int y, int plane) {
    if (!rc_area_flags_is_loaded() || x < 0 || y < 0 || plane < 0
            || plane >= 4) {
        return 0;
    }
    const int *tile_index = g_active_area_tile_index
                          ? g_active_area_tile_index : g_area_tile_index;
    const RcAreaTileRegion *tiles = g_active_area_tiles
                                  ? g_active_area_tiles : g_area_tiles;
    int tile_count = g_active_area_tiles ? g_active_area_tile_count
                                         : g_area_tile_count;
    int idx = tile_index[mapsquare_for(x, y)];
    if (idx < 0 || idx >= tile_count || !tiles) return 0;
    return tiles[idx].flags[plane][x & 63][y & 63];
}

int rc_area_flag_rows_at(int x, int y, int plane) {
    if (!rc_area_flags_is_loaded() || x < 0 || y < 0 || plane < 0
            || plane >= 4) {
        return 0;
    }
    const RcAreaFlagRow *rows = g_active_area_flags
                              ? g_active_area_flags : g_rc_area_flags;
    const uint32_t *refs = g_active_area_refs
                         ? g_active_area_refs : g_area_refs;
    const RcAreaRange *index = g_active_area_index
                             ? g_active_area_index : g_area_index;
    RcAreaRange idx = index[mapsquare_for(x, y)];
    int count = 0;
    for (uint32_t i = 0; i < idx.count; i++) {
        if (!rows || !refs) break;
        const RcAreaFlagRow *row = &rows[refs[idx.first + i]];
        if (row_matches(row, x, y, plane)) count++;
    }
    return count;
}

int rc_wilderness_level_at(int x, int y, int plane) {
    if ((rc_area_flags_at(x, y, plane) & RC_AREA_WILDERNESS) == 0) return 0;
    const RcAreaFlagRow *rows = g_active_area_flags
                              ? g_active_area_flags : g_rc_area_flags;
    const uint32_t *refs = g_active_area_refs
                         ? g_active_area_refs : g_area_refs;
    const RcAreaRange *index = g_active_area_index
                             ? g_active_area_index : g_area_index;
    RcAreaRange idx = index[mapsquare_for(x, y)];
    for (uint32_t i = 0; i < idx.count; i++) {
        if (!rows || !refs) break;
        const RcAreaFlagRow *row = &rows[refs[idx.first + i]];
        if ((row->set_flags & RC_AREA_WILDERNESS_LEVEL_LINE) != 0
                && row_matches(row, x, y, plane)) {
            return row->value;
        }
    }
    return 0;
}

int rc_area_flags_all_provisional(void) {
    if (!rc_area_flags_is_loaded()) return 0;
    const RcAreaFlagRow *rows = g_active_area_flags
                              ? g_active_area_flags : g_rc_area_flags;
    int count = g_active_area_flags ? g_active_area_flag_count
                                    : g_rc_area_flag_count;
    for (int i = 0; rows && i < count; i++) {
        if (rows[i].authoritative_osrs) return 0;
    }
    return 1;
}
