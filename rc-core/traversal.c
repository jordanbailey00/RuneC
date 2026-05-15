#include "traversal.h"
#include "config.h"
#include "io.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TRAV_MAGIC 0x56415254u
#define TRAV_VERSION 1u
#define NO_TILE 0xFFFFu

typedef struct {
    uint32_t first, count;
} RcTraversalIndex;

RcTraversalEdge *g_rc_traversal_edges = NULL;
int g_rc_traversal_edge_count = 0;

static RcTraversalIndex g_traversal_index[RC_TRAVERSAL_KIND_COUNT]
                                          [RC_TRAVERSAL_MAX_SOURCE_ID];

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

int rc_load_traversal_edges(const char *path) {
    if (!path) return -1;
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
    memset(g_traversal_index, 0xFF, sizeof(g_traversal_index));
    for (int k = 0; k < RC_TRAVERSAL_KIND_COUNT; k++) {
        for (int i = 0; i < RC_TRAVERSAL_MAX_SOURCE_ID; i++) {
            g_traversal_index[k][i].count = 0;
        }
    }

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
            RcTraversalIndex *idx = &g_traversal_index[row->kind][row->source_id];
            if (idx->first == UINT32_MAX) idx->first = i;
            idx->count++;
        }
    }

    rc_asset_close(f);
    free(g_rc_traversal_edges);
    g_rc_traversal_edges = rows;
    g_rc_traversal_edge_count = (int)count;
    return g_rc_traversal_edge_count;
}

const RcTraversalEdge *rc_traversal_edges_for(int kind, int source_id,
                                              int *count) {
    if (source_id < 0 || !valid_key(kind, (uint32_t)source_id)
            || !g_rc_traversal_edges) {
        if (count) *count = 0;
        return NULL;
    }
    RcTraversalIndex idx = g_traversal_index[kind][source_id];
    if (idx.first == UINT32_MAX) {
        if (count) *count = 0;
        return NULL;
    }
    if (count) *count = (int)idx.count;
    return &g_rc_traversal_edges[idx.first];
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
    if (!target || !g_rc_traversal_edges) return NULL;
    for (int i = 0; i < g_rc_traversal_edge_count; i++) {
        const RcTraversalEdge *row = &g_rc_traversal_edges[i];
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
