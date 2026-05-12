#include "activity_schemas.h"
#include "io.h"
#include "types.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ASCH_MAGIC 0x48435341u
#define ASCH_VERSION 2u

RcActivitySchema *g_rc_activity_schemas = NULL;
int g_rc_activity_schema_count = 0;
static int g_rc_activity_schema_by_npc[RC_MAX_NPC_ID];
static int g_rc_activity_schema_index_built = 0;

static int read_pstr(FILE *f, char *out, int cap,
                     const char *path, const char *what) {
    uint8_t len;
    if (!rc_read_exact(f, &len, sizeof(len), 1, path, what)) return 0;
    if (len >= cap) return 0;
    if (len && !rc_read_exact(f, out, sizeof(char), len, path, what)) return 0;
    out[len] = '\0';
    return 1;
}

int rc_load_activity_schemas(const char *path) {
    if (!path) return -1;
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "activity_schemas: can't open %s\n", path);
        return -1;
    }

    uint32_t magic, version, count;
    if (!rc_read_exact(f, &magic, sizeof(magic), 1, path, "magic")
            || !rc_read_exact(f, &version, sizeof(version), 1,
                              path, "version")
            || !rc_read_exact(f, &count, sizeof(count), 1, path, "count")) {
        fclose(f);
        return -1;
    }
    if (magic != ASCH_MAGIC || version == 0 || version > ASCH_VERSION) {
        fclose(f);
        fprintf(stderr, "activity_schemas: bad header\n");
        return -1;
    }

    RcActivitySchema *rows = calloc(count ? count : 1, sizeof(*rows));
    if (!rows) {
        fclose(f);
        return -1;
    }

    int loaded = 0;
    for (uint32_t i = 0; i < count; i++) {
        RcActivitySchema row;
        memset(&row, 0, sizeof(row));
        uint16_t npc_count;
        if (!rc_read_exact(f, &row.status, sizeof(row.status), 1,
                           path, "status")
                || !rc_read_exact(f, &row.class_id, sizeof(row.class_id), 1,
                                  path, "class")
                || !rc_read_exact(f, &row.kind, sizeof(row.kind), 1,
                                  path, "kind")
                || !rc_read_exact(f, &row.flags, sizeof(row.flags), 1,
                                  path, "flags")
                || !rc_read_exact(f, &npc_count, sizeof(npc_count), 1,
                                  path, "npc count")
                || !rc_read_exact(f, &row.object_count,
                                  sizeof(row.object_count), 1,
                                  path, "object count")
                || !rc_read_exact(f, &row.spawn_point_count,
                                  sizeof(row.spawn_point_count), 1,
                                  path, "spawn point count")
                || !rc_read_exact(f, &row.spawn_region_count,
                                  sizeof(row.spawn_region_count), 1,
                                  path, "spawn region count")
                || !rc_read_exact(f, &row.dynamic_spawn_count,
                                  sizeof(row.dynamic_spawn_count), 1,
                                  path, "dynamic spawn count")
                || !rc_read_exact(f, &row.wave_spawn_count,
                                  sizeof(row.wave_spawn_count), 1,
                                  path, "wave spawn count")
                || !rc_read_exact(f, &row.object_anchor_count,
                                  sizeof(row.object_anchor_count), 1,
                                  path, "object anchor count")
                || !rc_read_exact(f, &row.safe_tile_count,
                                  sizeof(row.safe_tile_count), 1,
                                  path, "safe tile count")
                || !rc_read_exact(f, &row.unresolved_count,
                                  sizeof(row.unresolved_count), 1,
                                  path, "unresolved count")
                || !rc_read_exact(f, &row.state_count,
                                  sizeof(row.state_count), 1,
                                  path, "state count")
                || !rc_read_exact(f, &row.transition_count,
                                  sizeof(row.transition_count), 1,
                                  path, "transition count")
                || !rc_read_exact(f, &row.param_count,
                                  sizeof(row.param_count), 1,
                                  path, "param count")
                || !rc_read_exact(f, &row.attack_count,
                                  sizeof(row.attack_count), 1,
                                  path, "attack count")
                || !rc_read_exact(f, &row.phase_count,
                                  sizeof(row.phase_count), 1,
                                  path, "phase count")
                || !rc_read_exact(f, &row.mechanic_count,
                                  sizeof(row.mechanic_count), 1,
                                  path, "mechanic count")
                || !rc_read_exact(f, &row.room_count,
                                  sizeof(row.room_count), 1,
                                  path, "room count")
                || !rc_read_exact(f, &row.reward_count,
                                  sizeof(row.reward_count), 1,
                                  path, "reward count")
                || !rc_read_exact(f, &row.requirement_count,
                                  sizeof(row.requirement_count), 1,
                                  path, "requirement count")
                || !rc_read_exact(f, &row.min_x, sizeof(row.min_x), 1,
                                  path, "min x")
                || !rc_read_exact(f, &row.max_x, sizeof(row.max_x), 1,
                                  path, "max x")
                || !rc_read_exact(f, &row.min_y, sizeof(row.min_y), 1,
                                  path, "min y")
                || !rc_read_exact(f, &row.max_y, sizeof(row.max_y), 1,
                                  path, "max y")
                || !rc_read_exact(f, &row.min_plane,
                                  sizeof(row.min_plane), 1,
                                  path, "min plane")
                || !rc_read_exact(f, &row.max_plane,
                                  sizeof(row.max_plane), 1,
                                  path, "max plane")
                || !read_pstr(f, row.slug, sizeof(row.slug), path, "slug")
                || !read_pstr(f, row.name, sizeof(row.name), path, "name")) {
            free(rows);
            fclose(f);
            return -1;
        }
        for (uint16_t j = 0; j < npc_count; j++) {
            uint32_t npc_id;
            if (!rc_read_exact(f, &npc_id, sizeof(npc_id), 1,
                               path, "npc id")) {
                free(rows);
                fclose(f);
                return -1;
            }
            if (j < RC_ACTIVITY_SCHEMA_MAX_NPCS) {
                row.npc_ids[row.npc_count++] = npc_id;
            }
        }
        if (version >= 2) {
            for (uint16_t j = 0; j < row.object_count; j++) {
                uint32_t object_id;
                if (!rc_read_exact(f, &object_id, sizeof(object_id), 1,
                                   path, "object id")) {
                    free(rows);
                    fclose(f);
                    return -1;
                }
                if (j < RC_ACTIVITY_SCHEMA_MAX_OBJECTS) {
                    row.object_ids[row.object_id_count++] = object_id;
                }
            }
        }
        rows[loaded++] = row;
    }

    fclose(f);
    free(g_rc_activity_schemas);
    g_rc_activity_schemas = rows;
    g_rc_activity_schema_count = loaded;
    rc_activity_schemas_rebuild_index();
    fprintf(stderr, "activity_schemas: loaded %d rows from %s\n",
            loaded, path);
    return loaded;
}

void rc_activity_schemas_rebuild_index(void) {
    for (int i = 0; i < RC_MAX_NPC_ID; i++) g_rc_activity_schema_by_npc[i] = -1;
    for (int i = 0; i < g_rc_activity_schema_count; i++) {
        const RcActivitySchema *row = &g_rc_activity_schemas[i];
        for (uint16_t j = 0; j < row->npc_count; j++) {
            uint32_t npc_id = row->npc_ids[j];
            if (npc_id < RC_MAX_NPC_ID && g_rc_activity_schema_by_npc[npc_id] < 0) {
                g_rc_activity_schema_by_npc[npc_id] = i;
            }
        }
    }
    g_rc_activity_schema_index_built = 1;
}

int rc_activity_schema_find_slug(const char *slug) {
    if (!slug) return -1;
    for (int i = 0; i < g_rc_activity_schema_count; i++) {
        if (strcmp(g_rc_activity_schemas[i].slug, slug) == 0) return i;
    }
    return -1;
}

int rc_activity_schema_find_for_npc(uint32_t npc_id) {
    if (!g_rc_activity_schema_index_built || npc_id >= RC_MAX_NPC_ID) {
        return -1;
    }
    return g_rc_activity_schema_by_npc[npc_id];
}

bool rc_activity_schema_has_npc(int idx, uint32_t npc_id) {
    if (idx < 0 || idx >= g_rc_activity_schema_count) return false;
    const RcActivitySchema *row = &g_rc_activity_schemas[idx];
    for (uint16_t i = 0; i < row->npc_count; i++) {
        if (row->npc_ids[i] == npc_id) return true;
    }
    return false;
}

bool rc_activity_schema_has_object(int idx, uint32_t object_id) {
    if (idx < 0 || idx >= g_rc_activity_schema_count) return false;
    const RcActivitySchema *row = &g_rc_activity_schemas[idx];
    for (uint16_t i = 0; i < row->object_id_count; i++) {
        if (row->object_ids[i] == object_id) return true;
    }
    return false;
}

int rc_activity_schema_find_for_object(uint32_t object_id) {
    for (int i = 0; i < g_rc_activity_schema_count; i++) {
        if (rc_activity_schema_has_object(i, object_id)) return i;
    }
    return -1;
}

bool rc_activity_schema_blocks_parity(int idx) {
    if (idx < 0 || idx >= g_rc_activity_schema_count) return true;
    const RcActivitySchema *row = &g_rc_activity_schemas[idx];
    return row->status == RC_ACTIVITY_SCHEMA_BLOCKS_PARITY ||
           (row->flags & RC_ACTIVITY_SCHEMA_UNRESOLVED) != 0;
}
