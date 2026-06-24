#include "item_render_defs.h"

#include "../rc-core/assets.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    IDEF_MAGIC = 0x49444546,
    IDEF_V1 = 1,
    IDEF_V2 = 2,
};

static int read_u8(const unsigned char **p, const unsigned char *end,
                   uint8_t *out) {
    if (*p + 1 > end) return 0;
    *out = **p;
    *p += 1;
    return 1;
}

static int read_u16(const unsigned char **p, const unsigned char *end,
                    uint16_t *out) {
    if (*p + 2 > end) return 0;
    *out = (uint16_t)(*p)[0] | ((uint16_t)(*p)[1] << 8);
    *p += 2;
    return 1;
}

static int read_u32(const unsigned char **p, const unsigned char *end,
                    uint32_t *out) {
    if (*p + 4 > end) return 0;
    *out = (uint32_t)(*p)[0] | ((uint32_t)(*p)[1] << 8)
         | ((uint32_t)(*p)[2] << 16) | ((uint32_t)(*p)[3] << 24);
    *p += 4;
    return 1;
}

static uint32_t render_id_or_missing(uint32_t value) {
    return value == UINT32_MAX ? RUNEC_RENDER_MODEL_MISSING : value;
}

static int parse_render_record(RuneCItemDefRenderMap *map,
                               const unsigned char *buf,
                               uint32_t len, uint32_t version) {
    const unsigned char *p = buf;
    const unsigned char *end = buf + len;
    uint32_t id, tmp;
    uint8_t flags8, name_len;
    uint16_t flags16;

    if (!read_u32(&p, end, &id)) return 0;
    if (version == IDEF_V1) {
        if (!read_u8(&p, end, &flags8)) return 0;
    } else {
        uint8_t kind;
        if (!read_u16(&p, end, &flags16) || !read_u8(&p, end, &kind))
            return 0;
    }
    if (!read_u8(&p, end, &name_len)) return 0;
    if (p + name_len > end) return 0;
    p += name_len;

    if (!read_u16(&p, end, &flags16)
            || !read_u32(&p, end, &tmp)
            || !read_u32(&p, end, &tmp)
            || !read_u32(&p, end, &tmp)) {
        return 0;
    }

    if (version == IDEF_V1) {
        if (!read_u32(&p, end, &tmp)) return 0;
        return 1;
    }

    for (int i = 0; i < 4; i++) {
        if (!read_u32(&p, end, &tmp)) return 0;
    }

    if (id >= RC_MAX_ITEM_DEFS)
        return 1;

    RuneCItemDefRenderRecord *rec = &map->records[id];
    if (!read_u32(&p, end, &tmp)) return 0;
    rec->ground_model_id = render_id_or_missing(tmp);
    for (int i = 0; i < 3; i++) {
        if (!read_u32(&p, end, &tmp)) return 0;
        rec->male_model_ids[i] = render_id_or_missing(tmp);
    }
    for (int i = 0; i < 3; i++) {
        if (!read_u32(&p, end, &tmp)) return 0;
        rec->female_model_ids[i] = render_id_or_missing(tmp);
    }
    rec->loaded = 1;
    return 1;
}

void runec_item_def_render_map_free(RuneCItemDefRenderMap *map) {
    if (!map) return;
    free(map->records);
    memset(map, 0, sizeof(*map));
}

int runec_item_def_render_map_load(RuneCItemDefRenderMap *map,
                                   const char *path) {
    if (!map || !path) return 0;
    runec_item_def_render_map_free(map);
    map->records = calloc(RC_MAX_ITEM_DEFS, sizeof(*map->records));
    if (!map->records) return 0;
    for (int i = 0; i < RC_MAX_ITEM_DEFS; i++) {
        map->records[i].ground_model_id = RUNEC_RENDER_MODEL_MISSING;
        for (int j = 0; j < 3; j++) {
            map->records[i].male_model_ids[j] = RUNEC_RENDER_MODEL_MISSING;
            map->records[i].female_model_ids[j] = RUNEC_RENDER_MODEL_MISSING;
        }
    }

    FILE *f = rc_asset_fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "item_render_defs: can't open %s\n", path);
        runec_item_def_render_map_free(map);
        return 0;
    }

    uint32_t magic, version, count;
    if (fread(&magic, sizeof(magic), 1, f) != 1
            || fread(&version, sizeof(version), 1, f) != 1
            || fread(&count, sizeof(count), 1, f) != 1) {
        rc_asset_close(f);
        runec_item_def_render_map_free(map);
        return 0;
    }
    if (magic != IDEF_MAGIC || (version != IDEF_V1 && version != IDEF_V2)) {
        fprintf(stderr, "item_render_defs: bad header in %s\n", path);
        rc_asset_close(f);
        runec_item_def_render_map_free(map);
        return 0;
    }

    int loaded = 0;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t len;
        if (fread(&len, sizeof(len), 1, f) != 1) {
            rc_asset_close(f);
            runec_item_def_render_map_free(map);
            return 0;
        }
        unsigned char *buf = malloc(len);
        if (!buf) {
            rc_asset_close(f);
            runec_item_def_render_map_free(map);
            return 0;
        }
        if (fread(buf, 1, len, f) != len) {
            free(buf);
            rc_asset_close(f);
            runec_item_def_render_map_free(map);
            return 0;
        }
        if (!parse_render_record(map, buf, len, version)) {
            free(buf);
            rc_asset_close(f);
            runec_item_def_render_map_free(map);
            return 0;
        }
        free(buf);
        loaded++;
    }

    rc_asset_close(f);
    map->loaded = 1;
    fprintf(stderr, "item_render_defs: loaded %d records from %s\n",
            loaded, path);
    return loaded;
}

const RuneCItemDefRenderRecord *runec_item_def_render_find(
    const RuneCItemDefRenderMap *map, int item_id) {
    if (!map || !map->loaded || item_id < 0 || item_id >= RC_MAX_ITEM_DEFS)
        return NULL;
    const RuneCItemDefRenderRecord *rec = &map->records[item_id];
    return rec->loaded ? rec : NULL;
}
