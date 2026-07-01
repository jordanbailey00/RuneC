#include "traversal.h"
#include "config.h"
#include "io.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TRAV_MAGIC 0x56415254u
#define TRAV_VERSION 1u
#define NO_TILE 0xFFFFu

RcTraversalEdge *g_rc_traversal_edges = NULL;
int g_rc_traversal_edge_count = 0;

static RcTraversalRange g_traversal_index[RC_TRAVERSAL_KIND_COUNT]
                                          [RC_TRAVERSAL_MAX_SOURCE_ID];
static const RcTraversalEdge *g_active_traversal_edges = NULL;
static const RcTraversalRange (*g_active_traversal_index)
    [RC_TRAVERSAL_MAX_SOURCE_ID] = g_traversal_index;
static int g_active_traversal_edge_count = 0;

static int read_str8(FILE *f, char *out, int cap,
                     const char *path, const char *what) {
    uint8_t len;
    if (!rc_read_exact(f, &len, sizeof(len), 1, path, what)) return 0;
    int keep = len < (uint8_t)(cap - 1) ? (int)len : cap - 1;
    if (keep && !rc_read_exact(f, out, 1, (size_t)keep, path, what)) return 0;
    out[keep] = '\0';
    if (len > (uint8_t)keep &&
            !rc_seek(f, (long)(len - (uint8_t)keep), SEEK_CUR, path, what)) {
        return 0;
    }
    return 1;
}

static int valid_key(int kind, uint32_t source_id) {
    return kind > 0 && kind < RC_TRAVERSAL_KIND_COUNT
        && source_id < RC_TRAVERSAL_MAX_SOURCE_ID;
}

void rc_traversal_data_init(RcTraversalData *data) {
    if (!data) return;
    data->edges = NULL;
    data->edge_count = 0;
    memset(data->index, 0xFF, sizeof(data->index));
    for (int k = 0; k < RC_TRAVERSAL_KIND_COUNT; k++) {
        for (int i = 0; i < RC_TRAVERSAL_MAX_SOURCE_ID; i++) {
            data->index[k][i].count = 0;
        }
    }
}

void rc_traversal_data_free(RcTraversalData *data) {
    if (!data) return;
    free(data->edges);
    rc_traversal_data_init(data);
}

void rc_traversal_use_data(const RcTraversalData *data) {
    if (!data) {
        g_active_traversal_edges = g_rc_traversal_edges;
        g_active_traversal_edge_count = g_rc_traversal_edge_count;
        g_active_traversal_index = g_traversal_index;
        return;
    }
    g_active_traversal_edges = data->edges;
    g_active_traversal_edge_count = data->edge_count;
    g_active_traversal_index = data->index;
}

void rc_traversal_reset_data_if_active(const RcTraversalData *data) {
    if (data && g_active_traversal_edges == data->edges) {
        rc_traversal_use_data(NULL);
    }
}

int rc_load_traversal_edges_into(const char *path, RcTraversalData *data) {
    if (!path || !data) return -1;
    FILE *f = rc_asset_fopen(path, "rb");
    if (!f) return -1;

    uint32_t magic, version, count;
    if (!rc_read_exact(f, &magic, sizeof(magic), 1, path, "magic")
            || !rc_read_exact(f, &version, sizeof(version), 1, path, "version")
            || !rc_read_exact(f, &count, sizeof(count), 1, path, "count")
            || magic != TRAV_MAGIC || version != TRAV_VERSION) {
        rc_asset_close(f);
        return -1;
    }

    RcTraversalEdge *rows = calloc(count ? count : 1u, sizeof(*rows));
    if (!rows) {
        rc_asset_close(f);
        return -1;
    }
    memset(data->index, 0xFF, sizeof(data->index));
    for (int k = 0; k < RC_TRAVERSAL_KIND_COUNT; k++)
        for (int j = 0; j < RC_TRAVERSAL_MAX_SOURCE_ID; j++)
            data->index[k][j].count = 0;

    for (uint32_t i = 0; i < count; i++) {
        RcTraversalEdge *row = &rows[i];
        uint16_t planes;
        uint8_t pad[4];
        if (!rc_read_exact(f, &row->kind, sizeof(row->kind), 1, path, "kind")
                || !rc_read_exact(f, &row->option, sizeof(row->option), 1,
                                  path, "option")
                || !rc_read_exact(f, &planes, sizeof(planes), 1, path, "planes")
                || !rc_read_exact(f, &row->source_id, sizeof(row->source_id),
                                  1, path, "source id")
                || !rc_read_exact(f, &row->flags, sizeof(row->flags), 1,
                                  path, "flags")
                || !rc_read_exact(f, &row->start_x, sizeof(row->start_x), 1,
                                  path, "start x")
                || !rc_read_exact(f, &row->start_y, sizeof(row->start_y), 1,
                                  path, "start y")
                || !rc_read_exact(f, &row->dest_x, sizeof(row->dest_x), 1,
                                  path, "dest x")
                || !rc_read_exact(f, &row->dest_y, sizeof(row->dest_y), 1,
                                  path, "dest y")
                || !rc_read_exact(f, pad, sizeof(pad), 1, path, "pad")
                || !read_str8(f, row->action, sizeof(row->action), path,
                              "action")
                || !read_str8(f, row->target, sizeof(row->target), path,
                              "target")) {
            free(rows);
            rc_asset_close(f);
            return -1;
        }
        row->start_plane = (uint8_t)((planes >> 8) & 0xFF);
        row->dest_plane = (uint8_t)(planes & 0xFF);
        if (valid_key(row->kind, row->source_id)) {
            RcTraversalRange *idx =
                &data->index[row->kind][row->source_id];
            if (idx->first == UINT32_MAX) idx->first = i;
            idx->count++;
        }
    }

    rc_asset_close(f);
    free(data->edges);
    data->edges = rows;
    data->edge_count = (int)count;
    return data->edge_count;
}

int rc_traversal_mirror_to_globals(const RcTraversalData *data) {
    if (!data) return 0;
    if (data->edge_count == g_rc_traversal_edge_count
            && data->edge_count > 0 && data->edges && g_rc_traversal_edges
            && memcmp(g_rc_traversal_edges, data->edges,
                      (size_t)data->edge_count * sizeof(*data->edges)) == 0) {
        memcpy(g_traversal_index, data->index, sizeof(g_traversal_index));
        return 1;
    }
    RcTraversalEdge *rows = NULL;
    if (data->edge_count > 0) {
        if (!data->edges) return 0;
        rows = malloc((size_t)data->edge_count * sizeof(*rows));
        if (!rows) return 0;
        memcpy(rows, data->edges, (size_t)data->edge_count * sizeof(*rows));
    }
    free(g_rc_traversal_edges);
    g_rc_traversal_edges = rows;
    g_rc_traversal_edge_count = data->edge_count;
    memcpy(g_traversal_index, data->index, sizeof(g_traversal_index));
    return 1;
}

int rc_load_traversal_edges(const char *path) {
    RcTraversalData *data = calloc(1, sizeof(*data));
    if (!data) return -1;
    rc_traversal_data_init(data);
    int loaded = rc_load_traversal_edges_into(path, data);
    if (loaded >= 0 && !rc_traversal_mirror_to_globals(data)) loaded = -1;
    rc_traversal_data_free(data);
    free(data);
    if (loaded >= 0) rc_traversal_use_data(NULL);
    return loaded;
}

const RcTraversalEdge *rc_traversal_edges_for(int kind, int source_id,
                                              int *count) {
    const RcTraversalEdge *edges = g_active_traversal_edges
                                 ? g_active_traversal_edges
                                 : g_rc_traversal_edges;
    const RcTraversalRange (*index)[RC_TRAVERSAL_MAX_SOURCE_ID] =
        g_active_traversal_index ? g_active_traversal_index
                                 : g_traversal_index;
    if (source_id < 0 || !valid_key(kind, (uint32_t)source_id)
            || !edges) {
        if (count) *count = 0;
        return NULL;
    }
    RcTraversalRange idx = index[kind][source_id];
    if (idx.first == UINT32_MAX) {
        if (count) *count = 0;
        return NULL;
    }
    if (count) *count = (int)idx.count;
    return &edges[idx.first];
}

const RcTraversalEdge *rc_traversal_edges_all(int *count) {
    const RcTraversalEdge *edges = g_active_traversal_edges
                                 ? g_active_traversal_edges
                                 : g_rc_traversal_edges;
    int edge_count = g_active_traversal_edges ? g_active_traversal_edge_count
                                              : g_rc_traversal_edge_count;
    if (count) *count = edge_count;
    return edge_count > 0 ? edges : NULL;
}

const RcTraversalEdge *rc_traversal_find(int kind, int source_id,
                                         int x, int y, int plane, int option) {
    int count = 0;
    const RcTraversalEdge *rows = rc_traversal_edges_for(kind, source_id, &count);
    for (int i = 0; rows && i < count; i++) {
        const RcTraversalEdge *row = &rows[i];
        if (row->kind != (uint8_t)kind || row->source_id != (uint32_t)source_id)
            continue;
        if (option >= 0 && row->option != (uint8_t)option) continue;
        if (x >= 0 && row->start_x != (uint16_t)x) continue;
        if (y >= 0 && row->start_y != (uint16_t)y) continue;
        if (plane >= 0 && row->start_plane != (uint8_t)plane) continue;
        return row;
    }
    return NULL;
}

const RcTraversalEdge *rc_traversal_find_target(int kind, const char *target) {
    const RcTraversalEdge *edges = g_active_traversal_edges
                                 ? g_active_traversal_edges
                                 : g_rc_traversal_edges;
    int edge_count = g_active_traversal_edges ? g_active_traversal_edge_count
                                              : g_rc_traversal_edge_count;
    if (!target || !edges) return NULL;
    for (int i = 0; i < edge_count; i++) {
        const RcTraversalEdge *row = &edges[i];
        if (row->kind == (uint8_t)kind && strcmp(row->target, target) == 0) {
            return row;
        }
    }
    return NULL;
}

int rc_player_apply_traversal(RcWorld *world, const RcTraversalEdge *edge) {
    if (!world || !edge || !(world->enabled & RC_SUB_TRAVERSAL)) return 0;
    world->player.prev_x = world->player.x;
    world->player.prev_y = world->player.y;
    world->player.x = edge->dest_x;
    world->player.y = edge->dest_y;
    world->player.plane = edge->dest_plane;
    world->player.route_len = 0;
    world->player.route_idx = 0;
    return 1;
}
