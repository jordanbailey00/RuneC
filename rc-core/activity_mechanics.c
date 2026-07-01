#include "activity_mechanics.h"
#include "io.h"
#include "types.h"

#include <stdio.h>
#include <string.h>

#define AMCH_MAGIC 0x48434D41u
#define AMCH_VERSION 3u

RcActivityMechanic g_rc_activity_mechanics[RC_ACTIVITY_MECH_MAX_ROWS];
int g_rc_activity_mechanic_count = 0;
static uint64_t g_rc_activity_behavior_by_npc[RC_MAX_NPC_ID];
static uint16_t g_rc_activity_profile_by_npc[RC_MAX_NPC_ID];
static int g_rc_activity_behavior_index_built = 0;
static const RcActivityMechanic *g_active_activity_mechanics =
    g_rc_activity_mechanics;
static int g_active_activity_mechanic_count = 0;
static const uint64_t *g_active_activity_behavior_by_npc =
    g_rc_activity_behavior_by_npc;
static const uint16_t *g_active_activity_profile_by_npc =
    g_rc_activity_profile_by_npc;
static int g_active_activity_behavior_index_built = 0;

static void rebuild_mechanics_index_into(RcActivityMechanicData *data);

void rc_activity_mechanic_data_init(RcActivityMechanicData *data) {
    if (!data) return;
    memset(data->rows, 0, sizeof(data->rows));
    memset(data->behavior_by_npc, 0, sizeof(data->behavior_by_npc));
    memset(data->profile_by_npc, 0, sizeof(data->profile_by_npc));
    data->count = 0;
    data->index_built = 0;
}

void rc_activity_mechanic_data_free(RcActivityMechanicData *data) {
    rc_activity_mechanic_data_init(data);
}

int rc_activity_mechanic_data_import_globals(RcActivityMechanicData *data) {
    if (!data) return 0;
    memcpy(data->rows, g_rc_activity_mechanics, sizeof(data->rows));
    data->count = g_rc_activity_mechanic_count;
    if (g_rc_activity_behavior_index_built) {
        memcpy(data->behavior_by_npc, g_rc_activity_behavior_by_npc,
               sizeof(data->behavior_by_npc));
        memcpy(data->profile_by_npc, g_rc_activity_profile_by_npc,
               sizeof(data->profile_by_npc));
        data->index_built = g_rc_activity_behavior_index_built;
    } else {
        rebuild_mechanics_index_into(data);
    }
    return 1;
}

void rc_activity_mechanics_use_data(const RcActivityMechanicData *data) {
    if (!data) {
        g_active_activity_mechanics = g_rc_activity_mechanics;
        g_active_activity_mechanic_count = g_rc_activity_mechanic_count;
        g_active_activity_behavior_by_npc =
            g_rc_activity_behavior_by_npc;
        g_active_activity_profile_by_npc = g_rc_activity_profile_by_npc;
        g_active_activity_behavior_index_built =
            g_rc_activity_behavior_index_built;
        return;
    }
    g_active_activity_mechanics = data->rows;
    g_active_activity_mechanic_count = data->count;
    g_active_activity_behavior_by_npc = data->behavior_by_npc;
    g_active_activity_profile_by_npc = data->profile_by_npc;
    g_active_activity_behavior_index_built = data->index_built;
}

void rc_activity_mechanics_reset_data_if_active(
    const RcActivityMechanicData *data) {
    if (data && g_active_activity_mechanics == data->rows) {
        rc_activity_mechanics_use_data(NULL);
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

static void rebuild_mechanics_index_into(RcActivityMechanicData *data) {
    if (!data) return;
    memset(data->behavior_by_npc, 0, sizeof(data->behavior_by_npc));
    memset(data->profile_by_npc, 0, sizeof(data->profile_by_npc));
    for (int i = 0; i < data->count; i++) {
        const RcActivityMechanic *row = &data->rows[i];
        for (uint16_t j = 0; j < row->npc_count; j++) {
            uint32_t npc_id = row->npc_ids[j];
            if (npc_id < RC_MAX_NPC_ID) {
                data->behavior_by_npc[npc_id] |= row->behavior_bits;
                if (row->profile_id != RC_ACTIVITY_PROFILE_NONE) {
                    data->profile_by_npc[npc_id] = row->profile_id;
                }
            }
        }
    }
    data->index_built = 1;
}

int rc_load_activity_mechanics_into(const char *path,
                                    RcActivityMechanicData *data) {
    if (!path || !data) return -1;
    FILE *f = rc_asset_fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "activity_mechanics: can't open %s\n", path);
        return -1;
    }

    uint32_t magic, version, count;
    if (!rc_read_exact(f, &magic, sizeof(magic), 1, path, "magic")
            || !rc_read_exact(f, &version, sizeof(version), 1, path, "version")
            || !rc_read_exact(f, &count, sizeof(count), 1, path, "count")) {
        rc_asset_close(f);
        return -1;
    }
    if (magic != AMCH_MAGIC || version == 0 || version > AMCH_VERSION) {
        rc_asset_close(f);
        fprintf(stderr, "activity_mechanics: bad header\n");
        return -1;
    }

    memset(data->rows, 0, sizeof(data->rows));
    data->count = 0;

    int loaded = 0;
    for (uint32_t i = 0; i < count; i++) {
        RcActivityMechanic row;
        memset(&row, 0, sizeof(row));
        uint16_t npc_count, section_count;
        if (!rc_read_exact(f, &row.status, sizeof(row.status), 1,
                           path, "status")
                || !rc_read_exact(f, &row.flags, sizeof(row.flags), 1,
                                  path, "flags")
                || !rc_read_exact(f, &npc_count, sizeof(npc_count), 1,
                                  path, "npc count")
                || !rc_read_exact(f, &section_count, sizeof(section_count),
                                  1, path, "section count")
                || !read_pstr(f, row.slug, sizeof(row.slug), path, "slug")
                || !read_pstr(f, row.name, sizeof(row.name), path, "name")
                || !read_pstr(f, row.source_pages, sizeof(row.source_pages),
                              path, "source pages")
                || !read_pstr(f, row.encounter_slug,
                              sizeof(row.encounter_slug),
                              path, "encounter slug")) {
            rc_asset_close(f);
            return -1;
        }
        if (version >= 2 &&
                !rc_read_exact(f, &row.behavior_bits,
                               sizeof(row.behavior_bits), 1,
                               path, "behavior bits")) {
            rc_asset_close(f);
            return -1;
        }
        if (version >= 3 &&
                !rc_read_exact(f, &row.profile_id,
                               sizeof(row.profile_id), 1,
                               path, "profile id")) {
            rc_asset_close(f);
            return -1;
        }

        for (uint16_t j = 0; j < npc_count; j++) {
            uint32_t npc_id;
            if (!rc_read_exact(f, &npc_id, sizeof(npc_id), 1,
                               path, "npc id")) {
                rc_asset_close(f);
                return -1;
            }
            if (j < RC_ACTIVITY_MECH_MAX_NPC_IDS) {
                row.npc_ids[row.npc_count++] = npc_id;
            }
        }

        for (uint16_t j = 0; j < section_count; j++) {
            RcActivityMechanicSection sec;
            memset(&sec, 0, sizeof(sec));
            if (!read_pstr(f, sec.title, sizeof(sec.title),
                           path, "section title")
                    || !rc_read_exact(f, &sec.text_hash,
                                      sizeof(sec.text_hash), 1,
                                      path, "section hash")
                    || !rc_read_exact(f, &sec.text_len,
                                      sizeof(sec.text_len), 1,
                                      path, "section length")) {
                rc_asset_close(f);
                return -1;
            }
            if (j < RC_ACTIVITY_MECH_MAX_SECTIONS) {
                row.sections[row.section_count++] = sec;
            }
        }

        if (data->count < RC_ACTIVITY_MECH_MAX_ROWS) {
            data->rows[data->count++] = row;
            loaded++;
        }
    }

    rc_asset_close(f);
    rebuild_mechanics_index_into(data);
    fprintf(stderr, "activity_mechanics: loaded %d rows from %s\n",
            loaded, path);
    return loaded;
}

void rc_activity_mechanics_rebuild_index(void) {
    memset(g_rc_activity_behavior_by_npc, 0,
           sizeof(g_rc_activity_behavior_by_npc));
    memset(g_rc_activity_profile_by_npc, 0,
           sizeof(g_rc_activity_profile_by_npc));
    for (int i = 0; i < g_rc_activity_mechanic_count; i++) {
        const RcActivityMechanic *row = &g_rc_activity_mechanics[i];
        for (uint16_t j = 0; j < row->npc_count; j++) {
            uint32_t npc_id = row->npc_ids[j];
            if (npc_id < RC_MAX_NPC_ID) {
                g_rc_activity_behavior_by_npc[npc_id] |= row->behavior_bits;
                if (row->profile_id != RC_ACTIVITY_PROFILE_NONE) {
                    g_rc_activity_profile_by_npc[npc_id] = row->profile_id;
                }
            }
        }
    }
    g_rc_activity_behavior_index_built = 1;
    rc_activity_mechanics_use_data(NULL);
}

int rc_activity_mechanics_mirror_to_globals(
    const RcActivityMechanicData *data) {
    if (!data) return 0;
    memcpy(g_rc_activity_mechanics, data->rows,
           sizeof(g_rc_activity_mechanics));
    g_rc_activity_mechanic_count = data->count;
    memcpy(g_rc_activity_behavior_by_npc, data->behavior_by_npc,
           sizeof(g_rc_activity_behavior_by_npc));
    memcpy(g_rc_activity_profile_by_npc, data->profile_by_npc,
           sizeof(g_rc_activity_profile_by_npc));
    g_rc_activity_behavior_index_built = data->index_built;
    return 1;
}

int rc_load_activity_mechanics(const char *path) {
    RcActivityMechanicData data;
    rc_activity_mechanic_data_init(&data);
    int loaded = rc_load_activity_mechanics_into(path, &data);
    if (loaded >= 0 && !rc_activity_mechanics_mirror_to_globals(&data))
        loaded = -1;
    rc_activity_mechanic_data_free(&data);
    if (loaded >= 0) rc_activity_mechanics_use_data(NULL);
    return loaded;
}

int rc_activity_mechanics_find_slug(const char *slug) {
    if (!slug) return -1;
    const RcActivityMechanic *rows = g_active_activity_mechanics
                                   ? g_active_activity_mechanics
                                   : g_rc_activity_mechanics;
    int count = g_active_activity_mechanics ? g_active_activity_mechanic_count
                                            : g_rc_activity_mechanic_count;
    for (int i = 0; rows && i < count; i++) {
        if (strcmp(rows[i].slug, slug) == 0) return i;
    }
    return -1;
}

uint64_t rc_activity_mechanics_behavior_for_npc(uint32_t npc_id) {
    int built = g_active_activity_mechanics
              ? g_active_activity_behavior_index_built
              : g_rc_activity_behavior_index_built;
    const uint64_t *by_npc = g_active_activity_behavior_by_npc
                           ? g_active_activity_behavior_by_npc
                           : g_rc_activity_behavior_by_npc;
    if (!built || npc_id >= RC_MAX_NPC_ID) {
        return 0;
    }
    return by_npc[npc_id];
}

uint16_t rc_activity_mechanics_profile_for_npc(uint32_t npc_id) {
    int built = g_active_activity_mechanics
              ? g_active_activity_behavior_index_built
              : g_rc_activity_behavior_index_built;
    const uint16_t *by_npc = g_active_activity_profile_by_npc
                           ? g_active_activity_profile_by_npc
                           : g_rc_activity_profile_by_npc;
    if (!built || npc_id >= RC_MAX_NPC_ID) {
        return RC_ACTIVITY_PROFILE_NONE;
    }
    return by_npc[npc_id];
}

uint64_t rc_activity_mechanics_behavior_for_owner(const char *owner) {
    if (!owner) return 0;
    uint64_t bits = 0;
    const RcActivityMechanic *rows = g_active_activity_mechanics
                                   ? g_active_activity_mechanics
                                   : g_rc_activity_mechanics;
    int count = g_active_activity_mechanics ? g_active_activity_mechanic_count
                                            : g_rc_activity_mechanic_count;
    for (int i = 0; rows && i < count; i++) {
        const RcActivityMechanic *row = &rows[i];
        if (row->behavior_bits == 0) continue;
        if (strcmp(row->encounter_slug, owner) == 0) bits |= row->behavior_bits;
    }
    return bits;
}

bool rc_activity_mechanics_has_npc(int idx, uint32_t npc_id) {
    const RcActivityMechanic *rows = g_active_activity_mechanics
                                   ? g_active_activity_mechanics
                                   : g_rc_activity_mechanics;
    int count = g_active_activity_mechanics ? g_active_activity_mechanic_count
                                            : g_rc_activity_mechanic_count;
    if (idx < 0 || idx >= count || !rows) return false;
    const RcActivityMechanic *row = &rows[idx];
    for (uint16_t i = 0; i < row->npc_count; i++) {
        if (row->npc_ids[i] == npc_id) return true;
    }
    return false;
}
