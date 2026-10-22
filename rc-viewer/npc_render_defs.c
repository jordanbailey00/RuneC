#include "npc_render_defs.h"

#include "../rc-core/assets.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    NDEF_MAGIC = 0x4E444546,
    NDEF_V1 = 1,
    NDEF_V2 = 2,
    NDEF_V3 = 3,
    NDEF_V4 = 4,
    NDEF_V5 = 5,
};

static int read_exact(FILE *f, void *out, size_t size, size_t count) {
    return fread(out, size, count, f) == count;
}

void runec_npc_render_defs_free(RuneCNpcRenderDefs *defs) {
    if (!defs) return;
    free(defs->scales);
    defs->scales = NULL;
    defs->scale_count = 0;
}

void runec_npc_render_defs_init(RuneCNpcRenderDefs *defs) {
    if (!defs) return;
    memset(defs, 0, sizeof(*defs));
    for (int i = 0; i < RC_MAX_NPC_ID; i++)
        defs->by_id[i] = -1;
}

int runec_npc_render_defs_load(RuneCNpcRenderDefs *defs, const char *path) {
    if (!defs || !path) return 0;
    runec_npc_render_defs_init(defs);

    FILE *f = rc_asset_fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "npc_render_defs: can't open %s\n", path);
        return 0;
    }

    uint32_t magic, version, count;
    if (!read_exact(f, &magic, sizeof(magic), 1)
            || !read_exact(f, &version, sizeof(version), 1)
            || !read_exact(f, &count, sizeof(count), 1)) {
        rc_asset_close(f);
        return 0;
    }
    if (magic != NDEF_MAGIC || (version != NDEF_V1 && version != NDEF_V2
            && version != NDEF_V3 && version != NDEF_V4
            && version != NDEF_V5)) {
        fprintf(stderr, "npc_render_defs: bad header in %s\n", path);
        rc_asset_close(f);
        return 0;
    }

    int loaded = 0;
    for (uint32_t i = 0; i < count && loaded < RC_MAX_NPC_DEFS; i++) {
        RuneCNpcRenderDef *d = &defs->defs[loaded];
        memset(d, 0, sizeof(*d));

        uint32_t id;
        uint8_t size, name_len;
        int16_t combat_level;
        uint16_t hitpoints;
        uint16_t stats[6];
        int32_t anims[5];

        if (!read_exact(f, &id, sizeof(id), 1)
                || !read_exact(f, &size, sizeof(size), 1)
                || !read_exact(f, &combat_level, sizeof(combat_level), 1)
                || !read_exact(f, &hitpoints, sizeof(hitpoints), 1)
                || !read_exact(f, stats, sizeof(stats[0]), 6)
                || !read_exact(f, anims, sizeof(anims[0]), 5)
                || !read_exact(f, &name_len, sizeof(name_len), 1)) {
            rc_asset_close(f);
            return 0;
        }
        if (fseek(f, (long)name_len, SEEK_CUR) != 0) {
            rc_asset_close(f);
            return 0;
        }

        d->id = (int)id;
        d->stand_anim = anims[0];
        d->walk_anim = anims[1];
        d->run_anim = anims[2];
        d->attack_anim = anims[3];
        d->death_anim = anims[4];

        if (version >= NDEF_V2) {
            uint8_t aggr, atk_spd, aggro_r, atk_types, weak, immu;
            uint16_t max_hit, slayer_lvl;
            if (!read_exact(f, &aggr, sizeof(aggr), 1)
                    || !read_exact(f, &max_hit, sizeof(max_hit), 1)
                    || !read_exact(f, &atk_spd, sizeof(atk_spd), 1)
                    || !read_exact(f, &aggro_r, sizeof(aggro_r), 1)
                    || !read_exact(f, &slayer_lvl, sizeof(slayer_lvl), 1)
                    || !read_exact(f, &atk_types, sizeof(atk_types), 1)
                    || !read_exact(f, &weak, sizeof(weak), 1)
                    || !read_exact(f, &immu, sizeof(immu), 1)) {
                rc_asset_close(f);
                return 0;
            }
        }

        if (version >= NDEF_V3) {
            uint8_t model_count;
            if (!read_exact(f, &model_count, sizeof(model_count), 1)) {
                rc_asset_close(f);
                return 0;
            }
            for (uint8_t j = 0; j < model_count; j++) {
                uint32_t model_id;
                if (!read_exact(f, &model_id, sizeof(model_id), 1)) {
                    rc_asset_close(f);
                    return 0;
                }
                if (j < RUNEC_NPC_RENDER_MAX_MODELS) {
                    d->model_ids[j] = (int)model_id;
                    d->model_count++;
                }
            }
        }

        if (version >= NDEF_V4) {
            for (int j = 0; j < 5; j++) {
                uint8_t option_len;
                if (!read_exact(f, &option_len, sizeof(option_len), 1)
                        || fseek(f, (long)option_len, SEEK_CUR) != 0) {
                    rc_asset_close(f);
                    return 0;
                }
            }
        }

        if (version >= NDEF_V5) {
            uint8_t wander;
            uint16_t respawn, regen, transform_count;
            uint8_t hunt[6];
            int32_t transform_varbit, transform_varp;
            if (!read_exact(f, &wander, sizeof(wander), 1)
                    || !read_exact(f, &respawn, sizeof(respawn), 1)
                    || !read_exact(f, &regen, sizeof(regen), 1)
                    || !read_exact(f, hunt, sizeof(hunt[0]), 6)
                    || !read_exact(f, &transform_varbit,
                                   sizeof(transform_varbit), 1)
                    || !read_exact(f, &transform_varp,
                                   sizeof(transform_varp), 1)
                    || !read_exact(f, &transform_count,
                                   sizeof(transform_count), 1)
                    || fseek(f, (long)transform_count * 4L, SEEK_CUR) != 0) {
                rc_asset_close(f);
                return 0;
            }
        }

        d->loaded = 1;
        if (id < RC_MAX_NPC_ID)
            defs->by_id[id] = loaded;
        loaded++;
    }

    rc_asset_close(f);
    defs->count = loaded;
    defs->loaded = 1;
    fprintf(stderr, "npc_render_defs: loaded %d defs from %s\n", loaded, path);
    return loaded;
}

static int compare_scale(const void *a, const void *b) {
    const RuneCNpcScale *x = a, *y = b;
    return (x->npc_id > y->npc_id) - (x->npc_id < y->npc_id);
}

int runec_npc_render_defs_apply_scales(RuneCNpcRenderDefs *defs,
                                       const char *path) {
    if (!defs || !path) return -1;
    FILE *f = rc_asset_fopen(path, "r");
    if (!f) {
        fprintf(stderr, "npc_render_defs: can't open NPC scales %s\n", path);
        return -1;
    }
    RuneCNpcScale *rows = NULL;
    int count = 0, cap = 0, line_number = 0;
    char line[128];
    while (fgets(line, sizeof(line), f)) {
        line_number++;
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') continue;
        int id = 0, h = 0, v = 0;
        char extra = 0;
        if (sscanf(line, "%d %d %d %c", &id, &h, &v, &extra) != 3
                || id < 0 || h <= 0 || v <= 0 || h > 0xFFFF || v > 0xFFFF) {
            fprintf(stderr, "npc_render_defs: malformed NPC scale at %s:%d\n",
                    path, line_number);
            free(rows);
            rc_asset_close(f);
            return -1;
        }
        if (count == cap) {
            cap = cap ? cap * 2 : 64;
            RuneCNpcScale *grown = realloc(rows, (size_t)cap * sizeof(*rows));
            if (!grown) {
                free(rows);
                rc_asset_close(f);
                return -1;
            }
            rows = grown;
        }
        rows[count++] = (RuneCNpcScale){id, (uint16_t)h, (uint16_t)v};
    }
    rc_asset_close(f);
    qsort(rows, (size_t)count, sizeof(*rows), compare_scale);
    free(defs->scales);
    defs->scales = rows;
    defs->scale_count = count;
    fprintf(stderr, "npc_render_defs: loaded %d NPC scales from %s\n",
            count, path);
    return count;
}

void runec_npc_render_scale(const RuneCNpcRenderDefs *defs, int npc_id,
                            int *resize_h, int *resize_v) {
    int h = 128, v = 128;
    if (defs && defs->scales && defs->scale_count > 0) {
        RuneCNpcScale key = {npc_id, 0, 0};
        const RuneCNpcScale *hit = bsearch(&key, defs->scales,
                                           (size_t)defs->scale_count,
                                           sizeof(key), compare_scale);
        if (hit) {
            h = hit->resize_h;
            v = hit->resize_v;
        }
    }
    if (resize_h) *resize_h = h;
    if (resize_v) *resize_v = v;
}

int runec_npc_render_defs_apply_attack_anims(RuneCNpcRenderDefs *defs,
                                              const char *path) {
    if (!defs || !defs->loaded || !path) return 0;
    FILE *f = rc_asset_fopen(path, "r");
    if (!f) {
        fprintf(stderr, "npc_render_defs: can't open attack animations %s\n",
                path);
        return -1;
    }

    char line[256];
    int rows = 0;
    int applied = 0;
    long previous_stand_anim = -1;
    int saw_header = 0;
    int line_number = 0;
    while (fgets(line, sizeof(line), f)) {
        line_number++;
        if (!strchr(line, '\n') && !feof(f)) {
            fprintf(stderr,
                    "npc_render_defs: attack animation line %d is too long\n",
                    line_number);
            rc_asset_close(f);
            return -1;
        }
        char *cursor = line;
        while (*cursor == ' ' || *cursor == '\t') cursor++;
        if (!*cursor || *cursor == '\n' || *cursor == '#') continue;
        cursor[strcspn(cursor, "\r\n")] = '\0';
        if (strcmp(cursor, "stand_anim|attack_anim|note") == 0) {
            if (saw_header || rows > 0) {
                fprintf(stderr,
                        "npc_render_defs: misplaced attack animation header "
                        "at line %d\n", line_number);
                rc_asset_close(f);
                return -1;
            }
            saw_header = 1;
            continue;
        }
        if (!saw_header) {
            fprintf(stderr,
                    "npc_render_defs: missing attack animation header before "
                    "line %d\n", line_number);
            rc_asset_close(f);
            return -1;
        }

        char *first_separator = strchr(cursor, '|');
        char *second_separator = first_separator
                               ? strchr(first_separator + 1, '|') : NULL;
        if (!first_separator || !second_separator
                || strchr(second_separator + 1, '|')
                || !second_separator[1]) {
            fprintf(stderr,
                    "npc_render_defs: malformed attack animation row at "
                    "line %d\n", line_number);
            rc_asset_close(f);
            return -1;
        }
        *first_separator = '\0';
        *second_separator = '\0';
        char *end = NULL;
        long stand_anim = strtol(cursor, &end, 10);
        if (!end || *end != '\0') {
            fprintf(stderr,
                    "npc_render_defs: invalid stand animation at line %d\n",
                    line_number);
            rc_asset_close(f);
            return -1;
        }
        cursor = first_separator + 1;
        long attack_anim = strtol(cursor, &end, 10);
        if (!end || *end != '\0'
                || stand_anim < 0 || stand_anim > INT32_MAX
                || attack_anim <= 0 || attack_anim > INT32_MAX
                || stand_anim <= previous_stand_anim) {
            fprintf(stderr,
                    "npc_render_defs: invalid or unsorted attack animation "
                    "at line %d\n", line_number);
            rc_asset_close(f);
            return -1;
        }
        previous_stand_anim = stand_anim;

        rows++;
        for (int i = 0; i < defs->count; i++) {
            RuneCNpcRenderDef *def = &defs->defs[i];
            if (def->attack_anim < 0 && def->stand_anim == stand_anim) {
                def->attack_anim = (int)attack_anim;
                applied++;
            }
        }
    }
    rc_asset_close(f);
    if (!saw_header || rows <= 0) {
        fprintf(stderr, "npc_render_defs: empty attack animation table %s\n",
                path);
        return -1;
    }
    fprintf(stderr,
            "npc_render_defs: applied %d attack animations from %d stances in %s\n",
            applied, rows, path);
    return applied;
}

const RuneCNpcRenderDef *runec_npc_render_find(
    const RuneCNpcRenderDefs *defs, int npc_id) {
    if (!defs || !defs->loaded) return NULL;
    if (npc_id >= 0 && npc_id < RC_MAX_NPC_ID) {
        int idx = defs->by_id[npc_id];
        if (idx >= 0 && idx < defs->count)
            return &defs->defs[idx];
    }
    for (int i = 0; i < defs->count; i++) {
        if (defs->defs[i].id == npc_id)
            return &defs->defs[i];
    }
    return NULL;
}
