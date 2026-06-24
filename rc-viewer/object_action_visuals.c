#include "object_action_visuals.h"

#include "../rc-core/assets.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum {
    OBHV_MAGIC = 0x5648424F,
    OBHV_VERSION_MIN = 1,
    OBHV_VERSION_MAX = 2,
};

static int read_exact(FILE *f, void *ptr, size_t size, const char *path,
                      const char *field) {
    if (fread(ptr, size, 1, f) == 1)
        return 1;
    fprintf(stderr, "object_action_visuals: short read %s in %s\n",
            field, path ? path : "(null)");
    return 0;
}

void runec_object_action_visuals_clear(RuneCObjectActionVisualMap *map) {
    if (!map) return;
    memset(map, 0, sizeof(*map));
    for (int i = 0; i < RC_MAX_OBJECT_ID; i++)
        map->records[i].climb_anim = -1;
}

int runec_object_action_visuals_load(RuneCObjectActionVisualMap *map,
                                     const char *path) {
    if (!map || !path) return 0;
    runec_object_action_visuals_clear(map);

    FILE *f = rc_asset_fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "object_action_visuals: can't open %s\n", path);
        return 0;
    }

    uint32_t magic, version, count;
    if (!read_exact(f, &magic, sizeof(magic), path, "magic")
            || !read_exact(f, &version, sizeof(version), path, "version")
            || !read_exact(f, &count, sizeof(count), path, "count")) {
        rc_asset_close(f);
        return 0;
    }
    if (magic != OBHV_MAGIC || version < OBHV_VERSION_MIN
            || version > OBHV_VERSION_MAX) {
        fprintf(stderr, "object_action_visuals: bad header in %s\n", path);
        rc_asset_close(f);
        return 0;
    }

    int loaded = 0;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t obj_id, flags;
        int32_t next_loc_stage = -1;
        int32_t open_sound = -1;
        int32_t close_sound = -1;
        int32_t climb_anim = -1;
        uint8_t action_mask, skill;
        uint16_t pad;
        if (!read_exact(f, &obj_id, sizeof(obj_id), path, "object id")
                || !read_exact(f, &flags, sizeof(flags), path, "flags")) {
            rc_asset_close(f);
            return 0;
        }
        if (version >= 2u) {
            if (!read_exact(f, &next_loc_stage, sizeof(next_loc_stage), path,
                            "next loc stage")
                    || !read_exact(f, &open_sound, sizeof(open_sound), path,
                                   "open sound")
                    || !read_exact(f, &close_sound, sizeof(close_sound), path,
                                   "close sound")
                    || !read_exact(f, &climb_anim, sizeof(climb_anim), path,
                                   "climb animation")) {
                rc_asset_close(f);
                return 0;
            }
        }
        if (!read_exact(f, &action_mask, sizeof(action_mask), path,
                        "action mask")
                || !read_exact(f, &skill, sizeof(skill), path, "skill")
                || !read_exact(f, &pad, sizeof(pad), path, "pad")) {
            rc_asset_close(f);
            return 0;
        }

        (void)flags;
        (void)next_loc_stage;
        (void)open_sound;
        (void)close_sound;
        (void)action_mask;
        (void)skill;
        (void)pad;
        if (obj_id < RC_MAX_OBJECT_ID) {
            map->records[obj_id].climb_anim = climb_anim;
            map->records[obj_id].loaded = 1;
            loaded++;
        }
    }

    rc_asset_close(f);
    map->loaded = 1;
    fprintf(stderr, "object_action_visuals: loaded %d records from %s\n",
            loaded, path);
    return loaded;
}

const RuneCObjectActionVisualRecord *runec_object_action_visual_find(
    const RuneCObjectActionVisualMap *map, int obj_id) {
    if (!map || !map->loaded || obj_id < 0 || obj_id >= RC_MAX_OBJECT_ID)
        return NULL;
    const RuneCObjectActionVisualRecord *rec = &map->records[obj_id];
    return rec->loaded ? rec : NULL;
}
