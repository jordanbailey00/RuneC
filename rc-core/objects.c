#include "objects.h"
#include "io.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ODEF_MAGIC 0x4645444Fu
#define OPLC_MAGIC 0x434C504Fu
#define OBHV_MAGIC 0x5648424Fu
#define OTRP_MAGIC 0x5052544Fu
#define OBJ_VERSION 1u
#define OPLC_VERSION_MIN 1u
#define OPLC_VERSION_MAX 2u
#define ODEF_VERSION_MIN 1u
#define ODEF_VERSION_MAX 2u
#define OBHV_VERSION_MIN 1u
#define OBHV_VERSION_MAX 2u

typedef struct {
    uint32_t first, count;
} RcObjectIndex;

RcObjectDef g_rc_object_defs[RC_MAX_OBJECT_ID];
RcObjectBehavior g_rc_object_behaviors[RC_MAX_OBJECT_ID];
RcObjectPlacement *g_rc_object_placements = NULL;
RcObjectTransport *g_rc_object_transports = NULL;
RcObjectParam *g_rc_object_params = NULL;
int g_rc_object_def_count = 0;
int g_rc_object_behavior_count = 0;
int g_rc_object_placement_count = 0;
int g_rc_object_transport_count = 0;
int g_rc_object_param_count = 0;

static RcObjectIndex g_region_index[RC_MAX_OBJECT_ID];
static RcObjectIndex g_transport_index[RC_MAX_OBJECT_ID];

static int read_pstr(FILE *f, char *out, int cap,
                     const char *path, const char *what) {
    uint16_t len;
    if (!rc_read_exact(f, &len, sizeof(len), 1, path, what)) return 0;
    int keep = len < (uint16_t)(cap - 1) ? (int)len : cap - 1;
    if (keep && !rc_read_exact(f, out, 1, (size_t)keep, path, what)) return 0;
    out[keep] = '\0';
    if (len > (uint16_t)keep &&
            !rc_seek(f, (long)(len - (uint16_t)keep), SEEK_CUR, path, what)) {
        return 0;
    }
    return 1;
}

static int read_pstr8(FILE *f, char *out, int cap,
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

static int read_header(FILE *f, const char *path, uint32_t expect_magic,
                       uint32_t *count) {
    uint32_t magic, version;
    if (!rc_read_exact(f, &magic, sizeof(magic), 1, path, "magic")
            || !rc_read_exact(f, &version, sizeof(version), 1, path, "version")
            || !rc_read_exact(f, count, sizeof(*count), 1, path, "count")) {
        return 0;
    }
    return magic == expect_magic && version == OBJ_VERSION;
}

static int read_header_version(FILE *f, const char *path,
                               uint32_t expect_magic, uint32_t min_version,
                               uint32_t max_version, uint32_t *version_out,
                               uint32_t *count) {
    uint32_t magic, version;
    if (!rc_read_exact(f, &magic, sizeof(magic), 1, path, "magic")
            || !rc_read_exact(f, &version, sizeof(version), 1, path, "version")
            || !rc_read_exact(f, count, sizeof(*count), 1, path, "count")) {
        return 0;
    }
    if (version_out) *version_out = version;
    return magic == expect_magic
        && version >= min_version
        && version <= max_version;
}

static int append_object_param(uint32_t obj_id, uint32_t key, int32_t value) {
    if (g_rc_object_param_count < 0) return 0;
    size_t next_count = (size_t)g_rc_object_param_count + 1u;
    RcObjectParam *next = realloc(g_rc_object_params,
                                  next_count * sizeof(*next));
    if (!next) return 0;
    g_rc_object_params = next;
    g_rc_object_params[g_rc_object_param_count] = (RcObjectParam){
        .obj_id = obj_id,
        .key = key,
        .value = value,
    };
    g_rc_object_param_count++;
    return 1;
}

static uint64_t fnv1a_u64(uint64_t hash, const void *data, size_t len) {
    const unsigned char *p = (const unsigned char *)data;
    for (size_t i = 0; i < len; i++) {
        hash ^= (uint64_t)p[i];
        hash *= 1099511628211ull;
    }
    return hash;
}

static uint64_t computed_placement_key(uint32_t obj_id, uint16_t x, uint16_t y,
                                       uint16_t mapsquare, uint8_t plane,
                                       uint8_t type, uint8_t rotation,
                                       uint8_t flags, uint32_t ordinal) {
    uint64_t h = 1469598103934665603ull;
    h = fnv1a_u64(h, &obj_id, sizeof(obj_id));
    h = fnv1a_u64(h, &x, sizeof(x));
    h = fnv1a_u64(h, &y, sizeof(y));
    h = fnv1a_u64(h, &mapsquare, sizeof(mapsquare));
    h = fnv1a_u64(h, &plane, sizeof(plane));
    h = fnv1a_u64(h, &type, sizeof(type));
    h = fnv1a_u64(h, &rotation, sizeof(rotation));
    h = fnv1a_u64(h, &flags, sizeof(flags));
    h = fnv1a_u64(h, &ordinal, sizeof(ordinal));
    return h ? h : 1ull;
}

int rc_load_object_defs(const char *path) {
    if (!path) return -1;
    FILE *f = rc_asset_fopen(path, "rb");
    if (!f) return -1;
    uint32_t version, count;
    if (!read_header_version(f, path, ODEF_MAGIC, ODEF_VERSION_MIN,
                             ODEF_VERSION_MAX, &version, &count)) {
        rc_asset_close(f);
        return -1;
    }
    memset(g_rc_object_defs, 0, sizeof(g_rc_object_defs));
    free(g_rc_object_params);
    g_rc_object_params = NULL;
    g_rc_object_param_count = 0;
    g_rc_object_def_count = 0;
    int loaded = 0;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t obj_id, flags;
        uint16_t width, length;
        uint8_t interact_type, action_count, model_count, transform_count;
        uint8_t force_approach;
        uint8_t supports_items = 255, clip_flags = 0;
        uint16_t param_count = 0;
        uint16_t ambient_sound_distance = 0, ambient_sound_retain = 0;
        int32_t varbit, varp, animation_id;
        int32_t ambient_sound_id = -1;
        uint32_t map_icon;
        if (!rc_read_exact(f, &obj_id, sizeof(obj_id), 1, path, "object id")
                || !rc_read_exact(f, &width, sizeof(width), 1, path, "width")
                || !rc_read_exact(f, &length, sizeof(length), 1, path, "length")
                || !rc_read_exact(f, &interact_type, sizeof(interact_type), 1,
                                  path, "interact type")
                || !rc_read_exact(f, &action_count, sizeof(action_count), 1,
                                  path, "action count")
                || !rc_read_exact(f, &model_count, sizeof(model_count), 1,
                                  path, "model count")
                || !rc_read_exact(f, &transform_count, sizeof(transform_count),
                                  1, path, "transform count")
                || !rc_read_exact(f, &force_approach,
                                  sizeof(force_approach), 1, path,
                                  "force approach")
                || !rc_read_exact(f, &varbit, sizeof(varbit), 1, path, "varbit")
                || !rc_read_exact(f, &varp, sizeof(varp), 1, path, "varp")
                || !rc_read_exact(f, &animation_id, sizeof(animation_id), 1,
                                  path, "animation")
                || !rc_read_exact(f, &map_icon, sizeof(map_icon), 1,
                                  path, "map icon")
                || !rc_read_exact(f, &flags, sizeof(flags), 1, path, "flags")) {
            rc_asset_close(f);
            return -1;
        }
        if (version >= 2u) {
            if (!rc_read_exact(f, &supports_items, sizeof(supports_items), 1,
                               path, "supports items")
                    || !rc_read_exact(f, &clip_flags, sizeof(clip_flags), 1,
                                      path, "clip flags")
                    || !rc_read_exact(f, &param_count, sizeof(param_count), 1,
                                      path, "param count")
                    || !rc_read_exact(f, &ambient_sound_id,
                                      sizeof(ambient_sound_id), 1, path,
                                      "ambient sound")
                    || !rc_read_exact(f, &ambient_sound_distance,
                                      sizeof(ambient_sound_distance), 1, path,
                                      "ambient sound distance")
                    || !rc_read_exact(f, &ambient_sound_retain,
                                      sizeof(ambient_sound_retain), 1, path,
                                      "ambient sound retain")) {
                rc_asset_close(f);
                return -1;
            }
        }
        RcObjectDef row;
        memset(&row, 0, sizeof(row));
        for (int t = 0; t < RC_OBJECT_MAX_TRANSFORMS; t++) {
            row.transforms[t] = -1;
        }
        row.id = (int)obj_id;
        row.width = width;
        row.length = length;
        row.interact_type = interact_type;
        row.action_count = action_count;
        row.transform_count = transform_count;
        row.force_approach = force_approach;
        row.varbit = varbit;
        row.varp = varp;
        row.flags = flags;
        row.supports_items = supports_items;
        row.clip_flags = clip_flags;
        if (!read_pstr(f, row.name, sizeof(row.name), path, "name")) {
            rc_asset_close(f);
            return -1;
        }
        for (int a = 0; a < RC_OBJECT_ACTIONS; a++) {
            if (!read_pstr(f, row.actions[a], sizeof(row.actions[a]),
                           path, "action")) {
                rc_asset_close(f);
                return -1;
            }
        }
        long model_skip = (long)model_count * 4L;
        if (model_skip && !rc_seek(f, model_skip, SEEK_CUR, path, "models")) {
            rc_asset_close(f);
            return -1;
        }
        for (int t = 0; t < transform_count; t++) {
            int32_t transform;
            if (!rc_read_exact(f, &transform, sizeof(transform), 1, path,
                               "transform")) {
                rc_asset_close(f);
                return -1;
            }
            if (t < RC_OBJECT_MAX_TRANSFORMS) {
                row.transforms[t] = transform;
            }
        }
        row.param_first = (uint32_t)g_rc_object_param_count;
        row.param_count = param_count;
        for (uint16_t p = 0; p < param_count; p++) {
            uint32_t key;
            int32_t value;
            if (!rc_read_exact(f, &key, sizeof(key), 1, path, "param key")
                    || !rc_read_exact(f, &value, sizeof(value), 1, path,
                                      "param value")) {
                rc_asset_close(f);
                return -1;
            }
            if (obj_id < RC_MAX_OBJECT_ID
                    && !append_object_param(obj_id, key, value)) {
                rc_asset_close(f);
                return -1;
            }
        }
        if (obj_id >= RC_MAX_OBJECT_ID) {
            row.param_count = 0;
        }
        if (obj_id < RC_MAX_OBJECT_ID) {
            row.loaded = 1;
            g_rc_object_defs[obj_id] = row;
            loaded++;
        }
    }
    rc_asset_close(f);
    g_rc_object_def_count = loaded;
    return loaded;
}

int rc_load_object_behaviors(const char *path) {
    if (!path) return -1;
    FILE *f = rc_asset_fopen(path, "rb");
    if (!f) return -1;
    uint32_t version, count;
    if (!read_header_version(f, path, OBHV_MAGIC, OBHV_VERSION_MIN,
                             OBHV_VERSION_MAX, &version, &count)) {
        rc_asset_close(f);
        return -1;
    }
    memset(g_rc_object_behaviors, 0, sizeof(g_rc_object_behaviors));
    g_rc_object_behavior_count = 0;
    int loaded = 0;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t obj_id, flags;
        int32_t next_loc_stage = -1;
        int32_t open_sound = -1;
        int32_t close_sound = -1;
        int32_t climb_anim = -1;
        uint8_t action_mask, skill;
        uint16_t pad;
        if (!rc_read_exact(f, &obj_id, sizeof(obj_id), 1, path, "object id")
                || !rc_read_exact(f, &flags, sizeof(flags), 1, path, "flags")) {
            rc_asset_close(f);
            return -1;
        }
        if (version >= 2u) {
            if (!rc_read_exact(f, &next_loc_stage, sizeof(next_loc_stage), 1,
                               path, "next loc stage")
                    || !rc_read_exact(f, &open_sound, sizeof(open_sound), 1,
                                      path, "open sound")
                    || !rc_read_exact(f, &close_sound, sizeof(close_sound), 1,
                                      path, "close sound")
                    || !rc_read_exact(f, &climb_anim, sizeof(climb_anim), 1,
                                      path, "climb animation")) {
                rc_asset_close(f);
                return -1;
            }
        }
        if (!rc_read_exact(f, &action_mask, sizeof(action_mask), 1,
                           path, "action mask")
                || !rc_read_exact(f, &skill, sizeof(skill), 1, path, "skill")
                || !rc_read_exact(f, &pad, sizeof(pad), 1, path, "pad")) {
            rc_asset_close(f);
            return -1;
        }
        if (obj_id < RC_MAX_OBJECT_ID) {
            g_rc_object_behaviors[obj_id] = (RcObjectBehavior){
                .flags = flags,
                .next_loc_stage = next_loc_stage,
                .action_mask = action_mask,
                .skill = skill,
                .loaded = 1,
            };
            loaded++;
        }
    }
    rc_asset_close(f);
    g_rc_object_behavior_count = loaded;
    return loaded;
}

int rc_load_object_placements(const char *path) {
    if (!path) return -1;
    FILE *f = rc_asset_fopen(path, "rb");
    if (!f) return -1;
    uint32_t version, count, region_count;
    if (!read_header_version(f, path, OPLC_MAGIC, OPLC_VERSION_MIN,
                             OPLC_VERSION_MAX, &version, &count)
            || !rc_read_exact(f, &region_count, sizeof(region_count), 1,
                              path, "region count")) {
        rc_asset_close(f);
        return -1;
    }
    (void)region_count;
    RcObjectPlacement *rows = malloc((size_t)count * sizeof(*rows));
    if (!rows) {
        rc_asset_close(f);
        return -1;
    }
    for (int i = 0; i < RC_MAX_OBJECT_ID; i++) {
        g_region_index[i].first = UINT32_MAX;
        g_region_index[i].count = 0;
    }
    for (uint32_t i = 0; i < count; i++) {
        RcObjectPlacement *row = &rows[i];
        if (!rc_read_exact(f, &row->obj_id, sizeof(row->obj_id), 1, path,
                           "object id")) {
            free(rows);
            rc_asset_close(f);
            return -1;
        }
        if (version >= 2) {
            if (!rc_read_exact(f, &row->key, sizeof(row->key), 1, path,
                               "placement key")) {
                free(rows);
                rc_asset_close(f);
                return -1;
            }
        } else {
            row->key = 0;
        }
        if (!rc_read_exact(f, &row->x, sizeof(row->x), 1, path, "x")
                || !rc_read_exact(f, &row->y, sizeof(row->y), 1, path, "y")
                || !rc_read_exact(f, &row->mapsquare, sizeof(row->mapsquare),
                                  1, path, "mapsquare")
                || !rc_read_exact(f, &row->plane, sizeof(row->plane), 1,
                                  path, "plane")
                || !rc_read_exact(f, &row->type, sizeof(row->type), 1,
                                  path, "type")
                || !rc_read_exact(f, &row->rotation, sizeof(row->rotation),
                                  1, path, "rotation")
                || !rc_read_exact(f, &row->flags, sizeof(row->flags), 1,
                                  path, "flags")) {
            free(rows);
            rc_asset_close(f);
            return -1;
        }
        if (row->key == 0) {
            row->key = computed_placement_key(row->obj_id, row->x, row->y,
                                              row->mapsquare, row->plane,
                                              row->type, row->rotation,
                                              row->flags, i);
        }
        RcObjectIndex *idx = &g_region_index[row->mapsquare];
        if (idx->first == UINT32_MAX) idx->first = i;
        idx->count++;
    }
    rc_asset_close(f);
    free(g_rc_object_placements);
    g_rc_object_placements = rows;
    g_rc_object_placement_count = (int)count;
    return (int)count;
}

int rc_load_object_transports(const char *path) {
    if (!path) return -1;
    FILE *f = rc_asset_fopen(path, "rb");
    if (!f) return -1;
    uint32_t count;
    if (!read_header(f, path, OTRP_MAGIC, &count)) {
        rc_asset_close(f);
        return -1;
    }
    RcObjectTransport *rows = malloc((size_t)count * sizeof(*rows));
    if (!rows) {
        rc_asset_close(f);
        return -1;
    }
    for (int i = 0; i < RC_MAX_OBJECT_ID; i++) {
        g_transport_index[i].first = UINT32_MAX;
        g_transport_index[i].count = 0;
    }
    for (uint32_t i = 0; i < count; i++) {
        RcObjectTransport *row = &rows[i];
        uint16_t planes;
        uint8_t pad0, pad1;
        if (!rc_read_exact(f, &row->obj_id, sizeof(row->obj_id), 1, path,
                           "object id")
                || !rc_read_exact(f, &row->start_x, sizeof(row->start_x), 1,
                                  path, "start x")
                || !rc_read_exact(f, &row->start_y, sizeof(row->start_y), 1,
                                  path, "start y")
                || !rc_read_exact(f, &row->dest_x, sizeof(row->dest_x), 1,
                                  path, "dest x")
                || !rc_read_exact(f, &row->dest_y, sizeof(row->dest_y), 1,
                                  path, "dest y")
                || !rc_read_exact(f, &planes, sizeof(planes), 1, path,
                                  "planes")
                || !rc_read_exact(f, &row->option, sizeof(row->option), 1,
                                  path, "option")
                || !rc_read_exact(f, &row->flags, sizeof(row->flags), 1,
                                  path, "flags")
                || !rc_read_exact(f, &pad0, sizeof(pad0), 1, path, "pad0")
                || !rc_read_exact(f, &pad1, sizeof(pad1), 1, path, "pad1")
                || !read_pstr8(f, row->action, sizeof(row->action), path,
                               "action")
                || !read_pstr8(f, row->target, sizeof(row->target), path,
                               "target")) {
            free(rows);
            rc_asset_close(f);
            return -1;
        }
        row->start_plane = (uint8_t)((planes >> 14) & 0x3);
        row->dest_plane = (uint8_t)((planes >> 12) & 0x3);
        if (row->obj_id < RC_MAX_OBJECT_ID) {
            RcObjectIndex *idx = &g_transport_index[row->obj_id];
            if (idx->first == UINT32_MAX) idx->first = i;
            idx->count++;
        }
    }
    rc_asset_close(f);
    free(g_rc_object_transports);
    g_rc_object_transports = rows;
    g_rc_object_transport_count = (int)count;
    return (int)count;
}

const RcObjectDef *rc_object_def_get(int obj_id) {
    if (obj_id < 0 || obj_id >= RC_MAX_OBJECT_ID) return NULL;
    return g_rc_object_defs[obj_id].loaded ? &g_rc_object_defs[obj_id] : NULL;
}

int rc_object_def_param_int(int obj_id, int key, int default_value) {
    const RcObjectDef *def = rc_object_def_get(obj_id);
    if (!def || key < 0 || !g_rc_object_params) return default_value;
    uint32_t first = def->param_first;
    uint32_t end = first + def->param_count;
    if (first >= (uint32_t)g_rc_object_param_count
            || end > (uint32_t)g_rc_object_param_count
            || end < first) {
        return default_value;
    }
    for (uint32_t i = first; i < end; i++) {
        const RcObjectParam *param = &g_rc_object_params[i];
        if ((int)param->key == key) return param->value;
    }
    return default_value;
}

const RcObjectBehavior *rc_object_behavior_get(int obj_id) {
    if (obj_id < 0 || obj_id >= RC_MAX_OBJECT_ID) return NULL;
    return g_rc_object_behaviors[obj_id].loaded
         ? &g_rc_object_behaviors[obj_id] : NULL;
}

const RcObjectPlacement *rc_object_region_placements(uint16_t mapsquare,
                                                     int *count) {
    RcObjectIndex idx = g_region_index[mapsquare];
    if (count) *count = idx.first == UINT32_MAX ? 0 : (int)idx.count;
    if (idx.first == UINT32_MAX || !g_rc_object_placements) return NULL;
    return &g_rc_object_placements[idx.first];
}

int rc_object_placements_at(int x, int y, int plane,
                            RcObjectPlacement *out, int max_out) {
    if (x < 0 || y < 0 || plane < 0 || plane >= 4 || max_out <= 0) return 0;
    uint16_t ms = (uint16_t)(((x >> 6) << 8) | (y >> 6));
    int count = 0;
    const RcObjectPlacement *rows = rc_object_region_placements(ms, &count);
    int n = 0;
    for (int i = 0; rows && i < count && n < max_out; i++) {
        if (rows[i].x == x && rows[i].y == y && rows[i].plane == plane) {
            out[n++] = rows[i];
        }
    }
    return n;
}

const RcObjectTransport *rc_object_transport_find(int obj_id, int x, int y,
                                                  int plane, int option) {
    if (obj_id < 0 || obj_id >= RC_MAX_OBJECT_ID) return NULL;
    RcObjectIndex idx = g_transport_index[obj_id];
    if (idx.first == UINT32_MAX || !g_rc_object_transports) return NULL;
    for (uint32_t i = 0; i < idx.count; i++) {
        RcObjectTransport *row = &g_rc_object_transports[idx.first + i];
        if (row->obj_id != (uint32_t)obj_id) continue;
        if ((int)row->start_x == x && (int)row->start_y == y
                && (int)row->start_plane == plane
                && (option < 0 || (int)row->option == option)) {
            return row;
        }
    }
    return NULL;
}
