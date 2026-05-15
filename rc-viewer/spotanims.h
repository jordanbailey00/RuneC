#ifndef RC_VIEWER_SPOTANIMS_H
#define RC_VIEWER_SPOTANIMS_H

#include "../rc-core/io.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define SPOTANIM_MAGIC 0x544F5053u
#define SPOTANIM_VERSION 1u

typedef struct {
    uint32_t id;
    int32_t model_id;
    int32_t animation_id;
    uint32_t resize_xy;
    uint32_t resize_z;
    uint32_t rotation;
    int32_t brightness;
    int32_t shadow;
} SpotAnimDef;

typedef struct {
    SpotAnimDef *defs;
    int count;
    int loaded;
} SpotAnimSet;

static void spotanims_free(SpotAnimSet *set);

static SpotAnimSet *spotanims_load(const char *path) {
    FILE *f = rc_asset_fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "spotanims: can't open %s\n", path);
        return NULL;
    }

    uint32_t magic, version, count;
    if (!rc_read_exact(f, &magic, sizeof(magic), 1, path, "spotanim magic")
            || !rc_read_exact(f, &version, sizeof(version), 1, path,
                              "spotanim version")
            || !rc_read_exact(f, &count, sizeof(count), 1, path,
                              "spotanim count")
            || magic != SPOTANIM_MAGIC || version != SPOTANIM_VERSION) {
        rc_asset_close(f);
        return NULL;
    }

    SpotAnimSet *set = (SpotAnimSet *)calloc(1, sizeof(*set));
    if (!set) {
        rc_asset_close(f);
        return NULL;
    }
    set->defs = (SpotAnimDef *)calloc(count, sizeof(*set->defs));
    if (count > 0 && !set->defs) {
        rc_asset_close(f);
        spotanims_free(set);
        return NULL;
    }
    set->count = (int)count;
    for (uint32_t i = 0; i < count; i++) {
        if (!rc_read_exact(f, &set->defs[i], sizeof(set->defs[i]), 1, path,
                           "spotanim row")) {
            rc_asset_close(f);
            spotanims_free(set);
            return NULL;
        }
    }
    rc_asset_close(f);
    set->loaded = 1;
    fprintf(stderr, "spotanims: loaded %d from %s\n", set->count, path);
    return set;
}

static const SpotAnimDef *spotanim_find(const SpotAnimSet *set, int id) {
    if (!set || !set->loaded || id < 0) return NULL;
    for (int i = 0; i < set->count; i++) {
        if ((int)set->defs[i].id == id) return &set->defs[i];
    }
    return NULL;
}

static void spotanims_free(SpotAnimSet *set) {
    if (!set) return;
    free(set->defs);
    free(set);
}

#endif
