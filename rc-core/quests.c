#include "quests.h"
#include "io.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define QEST_MAGIC 0x54534551u
#define QEST_VERSION 1u

RcQuestDef *g_rc_quest_defs = NULL;
int g_rc_quest_count = 0;

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

int rc_load_quests(const char *path) {
    if (!path) return -1;
    FILE *f = fopen(path, "rb");
    if (!f) return -1;

    uint32_t magic, version, count;
    if (!rc_read_exact(f, &magic, sizeof(magic), 1, path, "magic")
            || !rc_read_exact(f, &version, sizeof(version), 1, path,
                              "version")
            || !rc_read_exact(f, &count, sizeof(count), 1, path, "count")
            || magic != QEST_MAGIC || version != QEST_VERSION) {
        fclose(f);
        return -1;
    }
    RcQuestDef *rows = calloc(count ? count : 1u, sizeof(*rows));
    if (!rows) {
        fclose(f);
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
            fclose(f);
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
                fclose(f);
                return -1;
            }
            if (r < RC_QUEST_MAX_SKILL_REQS) {
                row->reqs[r] = (RcQuestSkillReq){skill_id, level};
            }
        }
    }
    fclose(f);
    free(g_rc_quest_defs);
    g_rc_quest_defs = rows;
    g_rc_quest_count = (int)count;
    return g_rc_quest_count;
}

const RcQuestDef *rc_quest_def_get(int quest_idx) {
    if (quest_idx < 0 || quest_idx >= g_rc_quest_count) return NULL;
    return &g_rc_quest_defs[quest_idx];
}

int rc_quest_find(const char *name) {
    if (!name || !g_rc_quest_defs) return -1;
    for (int i = 0; i < g_rc_quest_count; i++) {
        if (strcmp(g_rc_quest_defs[i].name, name) == 0) return i;
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
