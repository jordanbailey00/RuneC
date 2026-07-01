#include "quests.h"
#include "io.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define QEST_MAGIC 0x54534551u
#define QEST_VERSION 1u

RcQuestDef *g_rc_quest_defs = NULL;
int g_rc_quest_count = 0;

static const RcQuestDef *g_active_quest_defs = NULL;
static int g_active_quest_count = 0;

void rc_quest_data_init(RcQuestData *data) {
    if (!data) return;
    memset(data, 0, sizeof(*data));
}

void rc_quest_data_free(RcQuestData *data) {
    if (!data) return;
    free(data->defs);
    rc_quest_data_init(data);
}

static int read_name(FILE *f, char *out, int cap, const char *path) {
    uint8_t len;
    if (!rc_read_exact(f, &len, sizeof(len), 1, path, "name len")) return 0;
    int keep = len < (uint8_t)(cap - 1) ? len : cap - 1;
    if (keep && !rc_read_exact(f, out, 1, (size_t)keep, path, "name")) {
        return 0;
    }
    out[keep] = '\0';
    if (len > keep
            && !rc_seek(f, (long)(len - keep), SEEK_CUR, path, "name")) {
        return 0;
    }
    return 1;
}

int rc_load_quests_into(const char *path, RcQuestData *out) {
    if (!out) return -1;
    rc_quest_data_free(out);
    if (!path) return -1;
    FILE *f = rc_asset_fopen(path, "rb");
    if (!f) return -1;

    uint32_t magic, version, count;
    if (!rc_read_exact(f, &magic, sizeof(magic), 1, path, "magic")
            || !rc_read_exact(f, &version, sizeof(version), 1, path,
                              "version")
            || !rc_read_exact(f, &count, sizeof(count), 1, path, "count")
            || magic != QEST_MAGIC || version != QEST_VERSION) {
        rc_asset_close(f);
        return -1;
    }
    RcQuestDef *rows = calloc(count ? count : 1u, sizeof(*rows));
    if (!rows) {
        rc_asset_close(f);
        return -1;
    }
    for (uint32_t i = 0; i < count; i++) {
        RcQuestDef *row = &rows[i];
        uint8_t req_count;
        if (!read_name(f, row->name, sizeof(row->name), path)
                || !rc_read_exact(f, &row->difficulty,
                                  sizeof(row->difficulty), 1, path,
                                  "difficulty")
                || !rc_read_exact(f, &row->length, sizeof(row->length), 1,
                                  path, "length")
                || !rc_read_exact(f, &req_count, sizeof(req_count), 1, path,
                                  "req count")) {
            free(rows);
            rc_asset_close(f);
            return -1;
        }
        row->req_count = req_count < RC_QUEST_MAX_SKILL_REQS
                       ? req_count : RC_QUEST_MAX_SKILL_REQS;
        for (uint8_t r = 0; r < req_count; r++) {
            uint8_t skill_id, level;
            if (!rc_read_exact(f, &skill_id, sizeof(skill_id), 1, path,
                               "skill id")
                    || !rc_read_exact(f, &level, sizeof(level), 1, path,
                                      "level")) {
                free(rows);
                rc_asset_close(f);
                return -1;
            }
            if (r < RC_QUEST_MAX_SKILL_REQS) {
                row->reqs[r] = (RcQuestSkillReq){skill_id, level};
            }
        }
    }
    rc_asset_close(f);
    out->defs = rows;
    out->count = (int)count;
    return out->count;
}

int rc_quest_data_import_globals(RcQuestData *out) {
    if (!out) return 0;
    rc_quest_data_free(out);
    if (g_rc_quest_count <= 0) return 1;
    if (!g_rc_quest_defs) return 0;
    out->defs = malloc((size_t)g_rc_quest_count * sizeof(*out->defs));
    if (!out->defs) return 0;
    memcpy(out->defs, g_rc_quest_defs,
           (size_t)g_rc_quest_count * sizeof(*out->defs));
    out->count = g_rc_quest_count;
    return 1;
}

int rc_quests_mirror_to_globals(const RcQuestData *data) {
    RcQuestDef *defs = NULL;
    int count = data ? data->count : 0;
    if (count > 0) {
        if (!data->defs) return 0;
        defs = malloc((size_t)count * sizeof(*defs));
        if (!defs) return 0;
        memcpy(defs, data->defs, (size_t)count * sizeof(*defs));
    }
    free(g_rc_quest_defs);
    g_rc_quest_defs = defs;
    g_rc_quest_count = count;
    rc_quests_use_data(NULL);
    return 1;
}

void rc_quests_use_data(const RcQuestData *data) {
    if (!data || data->count <= 0) {
        g_active_quest_defs = g_rc_quest_defs;
        g_active_quest_count = g_rc_quest_count;
        return;
    }
    g_active_quest_defs = data->defs;
    g_active_quest_count = data->count;
}

void rc_quests_reset_data_if_active(const RcQuestData *data) {
    if (!data) return;
    if (g_active_quest_defs == data->defs) rc_quests_use_data(NULL);
}

int rc_load_quests(const char *path) {
    RcQuestData data;
    rc_quest_data_init(&data);
    int loaded = rc_load_quests_into(path, &data);
    if (loaded < 0) return -1;
    if (!rc_quests_mirror_to_globals(&data)) {
        rc_quest_data_free(&data);
        return -1;
    }
    rc_quest_data_free(&data);
    return g_rc_quest_count;
}

const RcQuestDef *rc_quest_def_get(int quest_idx) {
    if (quest_idx < 0 || quest_idx >= g_active_quest_count
            || !g_active_quest_defs) return NULL;
    return &g_active_quest_defs[quest_idx];
}

int rc_quest_find(const char *name) {
    if (!name || !g_active_quest_defs) return -1;
    for (int i = 0; i < g_active_quest_count; i++) {
        if (strcmp(g_active_quest_defs[i].name, name) == 0) return i;
    }
    return -1;
}

int rc_quest_get_state(const RcQuestProgress *progress, int quest_id) {
    if (!progress) return 0;
    if (quest_id < 0 || quest_id >= RC_MAX_QUESTS) return 0;
    return progress->state[quest_id];
}

void rc_quest_set_state(RcQuestProgress *progress, int quest_id, int state) {
    if (!progress) return;
    if (quest_id < 0 || quest_id >= RC_MAX_QUESTS) return;
    progress->state[quest_id] = state;
}
