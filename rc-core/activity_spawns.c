#include "activity_spawns.h"
#include "io.h"
#include "npc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ASPN_MAGIC 0x4E505341u
#define ASPN_VERSION 1u

RcActivitySpawn *g_rc_activity_spawns = NULL;
int g_rc_activity_spawn_count = 0;

static RcActivitySpawnRange *g_spawn_index = NULL;
static int g_spawn_index_count = 0;
static const RcActivitySpawn *g_active_activity_spawns = NULL;
static int g_active_activity_spawn_count = 0;
static const RcActivitySpawnRange *g_active_spawn_index = NULL;
static int g_active_spawn_index_count = 0;

static void rebuild_spawn_index_into(RcActivitySpawnData *data);

void rc_activity_spawn_data_init(RcActivitySpawnData *data) {
    if (!data) return;
    data->rows = NULL;
    data->count = 0;
    data->index = NULL;
    data->index_count = 0;
}

void rc_activity_spawn_data_free(RcActivitySpawnData *data) {
    if (!data) return;
    free(data->rows);
    free(data->index);
    rc_activity_spawn_data_init(data);
}

int rc_activity_spawn_data_import_globals(RcActivitySpawnData *data) {
    if (!data) return 0;
    free(data->rows);
    free(data->index);
    data->rows = NULL;
    data->index = NULL;
    data->count = 0;
    data->index_count = 0;
    if (g_rc_activity_spawn_count > 0) {
        if (!g_rc_activity_spawns) return 0;
        data->rows = malloc((size_t)g_rc_activity_spawn_count *
                            sizeof(*data->rows));
        if (!data->rows) return 0;
        memcpy(data->rows, g_rc_activity_spawns,
               (size_t)g_rc_activity_spawn_count * sizeof(*data->rows));
        data->count = g_rc_activity_spawn_count;
    }
    if (g_spawn_index_count > 0) {
        if (!g_spawn_index) return 0;
        data->index = malloc((size_t)g_spawn_index_count *
                             sizeof(*data->index));
        if (!data->index) return 0;
        memcpy(data->index, g_spawn_index,
               (size_t)g_spawn_index_count * sizeof(*data->index));
        data->index_count = g_spawn_index_count;
    } else {
        rebuild_spawn_index_into(data);
    }
    return 1;
}

void rc_activity_spawns_use_data(const RcActivitySpawnData *data) {
    if (!data) {
        g_active_activity_spawns = g_rc_activity_spawns;
        g_active_activity_spawn_count = g_rc_activity_spawn_count;
        g_active_spawn_index = g_spawn_index;
        g_active_spawn_index_count = g_spawn_index_count;
        return;
    }
    g_active_activity_spawns = data->rows;
    g_active_activity_spawn_count = data->count;
    g_active_spawn_index = data->index;
    g_active_spawn_index_count = data->index_count;
}

void rc_activity_spawns_reset_data_if_active(
    const RcActivitySpawnData *data) {
    if (data && g_active_activity_spawns == data->rows) {
        rc_activity_spawns_use_data(NULL);
    }
}

static int read_pstr(FILE *f, char *out, int cap,
                     const char *path, const char *what) {
    uint8_t len;
    if (!rc_read_exact(f, &len, sizeof(len), 1, path, what)) return 0;
    if (len >= cap) return 0;
    if (len && !rc_read_exact(f, out, sizeof(char), len, path, what)) return 0;
    out[len] = '\0';
    return 1;
}

static void rebuild_spawn_index_into(RcActivitySpawnData *data) {
    if (!data) return;
    free(data->index);
    data->index = NULL;
    data->index_count = 0;

    int n = data->count;
    if (n <= 0) return;

    int groups = 0;
    for (int i = 0; i < n; i++) {
        if (i == 0 || strcmp(data->rows[i - 1].slug,
                             data->rows[i].slug) != 0) {
            groups++;
        }
    }
    data->index = calloc((size_t)groups, sizeof(*data->index));
    if (!data->index) return;

    for (int i = 0; i < n; i++) {
        if (i == 0 || strcmp(data->rows[i - 1].slug,
                             data->rows[i].slug) != 0) {
            RcActivitySpawnRange *idx = &data->index[data->index_count++];
            snprintf(idx->slug, sizeof(idx->slug), "%s",
                     data->rows[i].slug);
            idx->first = i;
            idx->count = 1;
        } else {
            data->index[data->index_count - 1].count++;
        }
    }
}

void rc_activity_spawns_rebuild_index(void) {
    RcActivitySpawnData data = {
        .rows = g_rc_activity_spawns,
        .count = g_rc_activity_spawn_count,
        .index = g_spawn_index,
        .index_count = g_spawn_index_count,
    };
    rebuild_spawn_index_into(&data);
    g_spawn_index = data.index;
    g_spawn_index_count = data.index_count;
    rc_activity_spawns_use_data(NULL);
}

int rc_load_activity_spawns_into(const char *path,
                                 RcActivitySpawnData *data) {
    if (!path || !data) return -1;
    FILE *f = rc_asset_fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "activity_spawns: can't open %s\n", path);
        return -1;
    }

    uint32_t magic, version, count;
    if (!rc_read_exact(f, &magic, sizeof(magic), 1, path, "magic")
            || !rc_read_exact(f, &version, sizeof(version), 1,
                              path, "version")
            || !rc_read_exact(f, &count, sizeof(count), 1,
                              path, "count")) {
        rc_asset_close(f);
        return -1;
    }
    if (magic != ASPN_MAGIC || version == 0 || version > ASPN_VERSION) {
        rc_asset_close(f);
        fprintf(stderr, "activity_spawns: bad header\n");
        return -1;
    }

    RcActivitySpawn *rows = calloc(count ? count : 1, sizeof(*rows));
    if (!rows) {
        rc_asset_close(f);
        return -1;
    }

    for (uint32_t i = 0; i < count; i++) {
        RcActivitySpawn row;
        memset(&row, 0, sizeof(row));
        row.npc_id = RC_ACTIVITY_SPAWN_NO_ID;
        row.object_id = RC_ACTIVITY_SPAWN_NO_ID;
        row.local_x = UINT16_MAX;
        row.local_y = UINT16_MAX;

        if (!rc_read_exact(f, &row.kind, sizeof(row.kind), 1,
                           path, "kind")
                || !rc_read_exact(f, &row.plane, sizeof(row.plane), 1,
                                  path, "plane")
                || !rc_read_exact(f, &row.rotation, sizeof(row.rotation), 1,
                                  path, "rotation")
                || !rc_read_exact(f, &row.random_offset,
                                  sizeof(row.random_offset), 1,
                                  path, "random offset")
                || !rc_read_exact(f, &row.flags, sizeof(row.flags), 1,
                                  path, "flags")
                || !rc_read_exact(f, &row.wave, sizeof(row.wave), 1,
                                  path, "wave")
                || !rc_read_exact(f, &row.x, sizeof(row.x), 1,
                                  path, "x")
                || !rc_read_exact(f, &row.y, sizeof(row.y), 1,
                                  path, "y")
                || !rc_read_exact(f, &row.min_x, sizeof(row.min_x), 1,
                                  path, "min x")
                || !rc_read_exact(f, &row.max_x, sizeof(row.max_x), 1,
                                  path, "max x")
                || !rc_read_exact(f, &row.min_y, sizeof(row.min_y), 1,
                                  path, "min y")
                || !rc_read_exact(f, &row.max_y, sizeof(row.max_y), 1,
                                  path, "max y")
                || !rc_read_exact(f, &row.local_x, sizeof(row.local_x), 1,
                                  path, "local x")
                || !rc_read_exact(f, &row.local_y, sizeof(row.local_y), 1,
                                  path, "local y")
                || !rc_read_exact(f, &row.npc_id, sizeof(row.npc_id), 1,
                                  path, "npc id")
                || !rc_read_exact(f, &row.object_id, sizeof(row.object_id),
                                  1, path, "object id")
                || !read_pstr(f, row.slug, sizeof(row.slug), path, "slug")
                || !read_pstr(f, row.key, sizeof(row.key), path, "key")
                || !read_pstr(f, row.entity, sizeof(row.entity), path,
                              "entity")
                || !read_pstr(f, row.ref, sizeof(row.ref), path, "ref")) {
            free(rows);
            rc_asset_close(f);
            return -1;
        }
        rows[i] = row;
    }

    rc_asset_close(f);
    free(data->rows);
    data->rows = rows;
    data->count = (int)count;
    rebuild_spawn_index_into(data);
    fprintf(stderr, "activity_spawns: loaded %d rows from %s\n",
            data->count, path);
    return data->count;
}

int rc_activity_spawns_mirror_to_globals(
    const RcActivitySpawnData *data) {
    if (!data) return 0;
    RcActivitySpawn *rows = NULL;
    RcActivitySpawnRange *index = NULL;
    if (data->count > 0) {
        if (!data->rows) return 0;
        rows = malloc((size_t)data->count * sizeof(*rows));
        if (!rows) return 0;
        memcpy(rows, data->rows, (size_t)data->count * sizeof(*rows));
    }
    if (data->index_count > 0) {
        if (!data->index) {
            free(rows);
            return 0;
        }
        index = malloc((size_t)data->index_count * sizeof(*index));
        if (!index) {
            free(rows);
            return 0;
        }
        memcpy(index, data->index, (size_t)data->index_count * sizeof(*index));
    }
    free(g_rc_activity_spawns);
    free(g_spawn_index);
    g_rc_activity_spawns = rows;
    g_rc_activity_spawn_count = data->count;
    g_spawn_index = index;
    g_spawn_index_count = data->index_count;
    return 1;
}

int rc_load_activity_spawns(const char *path) {
    RcActivitySpawnData data;
    rc_activity_spawn_data_init(&data);
    int loaded = rc_load_activity_spawns_into(path, &data);
    if (loaded >= 0 && !rc_activity_spawns_mirror_to_globals(&data))
        loaded = -1;
    rc_activity_spawn_data_free(&data);
    if (loaded >= 0) rc_activity_spawns_use_data(NULL);
    return loaded;
}

const RcActivitySpawn *rc_activity_spawns_for(const char *slug, int *count) {
    if (count) *count = 0;
    if (!slug) return NULL;
    const RcActivitySpawn *rows = g_active_activity_spawns
                                ? g_active_activity_spawns
                                : g_rc_activity_spawns;
    const RcActivitySpawnRange *index = g_active_spawn_index
                                      ? g_active_spawn_index : g_spawn_index;
    int index_count = g_active_spawn_index ? g_active_spawn_index_count
                                           : g_spawn_index_count;
    for (int i = 0; index && i < index_count; i++) {
        if (strcmp(index[i].slug, slug) == 0) {
            if (count) *count = index[i].count;
            return rows ? &rows[index[i].first] : NULL;
        }
    }
    return NULL;
}

const RcActivitySpawn *rc_activity_spawn_find_key(const char *slug,
                                                  uint8_t kind,
                                                  const char *key) {
    if (!key) return NULL;
    int count = 0;
    const RcActivitySpawn *rows = rc_activity_spawns_for(slug, &count);
    for (int i = 0; rows && i < count; i++) {
        if (rows[i].kind == kind && strcmp(rows[i].key, key) == 0) {
            return &rows[i];
        }
    }
    return NULL;
}

const RcActivitySpawn *rc_activity_spawn_find_object_at(const char *slug,
                                                        uint32_t object_id,
                                                        int x, int y,
                                                        int plane) {
    int count = 0;
    const RcActivitySpawn *rows = rc_activity_spawns_for(slug, &count);
    for (int i = 0; rows && i < count; i++) {
        const RcActivitySpawn *row = &rows[i];
        if (row->kind != RC_ACTIVITY_SPAWN_OBJECT_ANCHOR) continue;
        if ((row->flags & RC_ACTIVITY_SPAWN_HAS_OBJECT) == 0) continue;
        if (row->object_id == object_id && row->x == x && row->y == y
                && row->plane == plane) {
            return row;
        }
    }
    return NULL;
}

const RcActivitySpawn *rc_activity_spawn_wave_region(const char *slug,
                                                     uint16_t wave,
                                                     int rotation) {
    if (!slug || wave == 0 || rotation < 0) return NULL;
    int count = 0;
    const RcActivitySpawn *rows = rc_activity_spawns_for(slug, &count);
    if (!rows) return NULL;

    int matches = 0;
    const RcActivitySpawn *ref_row = NULL;
    for (int i = 0; i < count; i++) {
        const RcActivitySpawn *row = &rows[i];
        if (row->kind != RC_ACTIVITY_SPAWN_WAVE_REGION_REF ||
                row->wave != wave) {
            continue;
        }
        if (matches == rotation) ref_row = row;
        matches++;
    }
    if (!ref_row && matches > 0) {
        int idx = rotation % matches;
        for (int i = 0; i < count; i++) {
            const RcActivitySpawn *row = &rows[i];
            if (row->kind == RC_ACTIVITY_SPAWN_WAVE_REGION_REF &&
                    row->wave == wave && idx-- == 0) {
                ref_row = row;
                break;
            }
        }
    }
    return ref_row
        ? rc_activity_spawn_find_key(slug, RC_ACTIVITY_SPAWN_REGION,
                                     ref_row->ref)
        : NULL;
}

bool rc_activity_spawn_region_contains(const char *slug, const char *key,
                                       int x, int y, int plane) {
    int count = 0;
    const RcActivitySpawn *rows = rc_activity_spawns_for(slug, &count);
    for (int i = 0; rows && i < count; i++) {
        const RcActivitySpawn *row = &rows[i];
        if (row->kind != RC_ACTIVITY_SPAWN_REGION) continue;
        if (key && key[0] && strcmp(row->key, key) != 0) continue;
        if (row->plane == plane && x >= row->min_x && x <= row->max_x
                && y >= row->min_y && y <= row->max_y) {
            return true;
        }
    }
    return false;
}

int rc_activity_spawn_count_kind(const char *slug, uint8_t kind) {
    int count = 0;
    const RcActivitySpawn *rows = rc_activity_spawns_for(slug, &count);
    if (!rows) return 0;

    int matches = 0;
    for (int i = 0; i < count; i++) {
        if (rows[i].kind == kind) matches++;
    }
    return matches;
}

bool rc_activity_spawn_has_unresolved(const char *slug) {
    int count = 0;
    const RcActivitySpawn *rows = rc_activity_spawns_for(slug, &count);
    if (!rows) return false;

    for (int i = 0; i < count; i++) {
        if (rows[i].kind == RC_ACTIVITY_SPAWN_UNRESOLVED
                || (rows[i].flags & RC_ACTIVITY_SPAWN_REQUIRED)) {
            return true;
        }
    }
    return false;
}

int rc_activity_spawn_materialize_npcs(RcWorld *world, const char *slug,
                                       uint8_t kind) {
    if (!world || !slug) return -1;
    int count = 0;
    const RcActivitySpawn *rows = rc_activity_spawns_for(slug, &count);
    if (!rows) return 0;

    int spawned = 0;
    for (int i = 0; i < count; i++) {
        const RcActivitySpawn *row = &rows[i];
        if (kind && row->kind != kind) continue;
        if ((row->flags & RC_ACTIVITY_SPAWN_HAS_POINT) == 0) continue;
        if ((row->flags & RC_ACTIVITY_SPAWN_HAS_NPC) == 0) continue;
        int def_idx = rc_npc_def_find((int)row->npc_id);
        if (def_idx < 0) continue;
        if (rc_npc_spawn(world, def_idx, (int)row->x, (int)row->y,
                         (int)row->plane) >= 0) {
            spawned++;
        }
    }
    return spawned;
}

int rc_activity_spawn_materialize_wave_npcs(RcWorld *world, const char *slug,
                                            uint16_t wave) {
    if (!world || !slug || wave == 0) return -1;
    int count = 0;
    const RcActivitySpawn *rows = rc_activity_spawns_for(slug, &count);
    if (!rows) return 0;

    int spawned = 0;
    for (int i = 0; i < count; i++) {
        const RcActivitySpawn *row = &rows[i];
        if (row->kind != RC_ACTIVITY_SPAWN_WAVE_POINT ||
                row->wave != wave ||
                (row->flags & RC_ACTIVITY_SPAWN_HAS_POINT) == 0 ||
                (row->flags & RC_ACTIVITY_SPAWN_HAS_NPC) == 0) {
            continue;
        }
        int def_idx = rc_npc_def_find((int)row->npc_id);
        if (def_idx < 0) continue;
        if (rc_npc_spawn(world, def_idx, (int)row->x, (int)row->y,
                         (int)row->plane) >= 0) {
            spawned++;
        }
    }
    return spawned;
}
