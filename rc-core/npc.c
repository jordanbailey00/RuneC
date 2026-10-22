#include "combat.h"
#include "combat_hit.h"
#include "npc.h"
#include "storage.h"
#include "io.h"
#include "rng.h"
#include "pathfinding.h"
#include "interaction.h"
#include "spawn_index.h"
#include "varbits.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

RcNpcDef g_npc_defs[RC_MAX_NPC_DEFS];
int g_npc_def_count = 0;
static int g_npc_def_by_id[RC_MAX_NPC_ID];
static bool g_npc_def_index_ready = false;
static const RcNpcDef *g_active_npc_defs = g_npc_defs;
static int g_active_npc_def_count = 0;
static const int *g_active_npc_def_by_id = g_npc_def_by_id;
static int32_t *g_npc_transforms = NULL;
static int g_npc_transform_count = 0;
static const int32_t *g_active_npc_transforms = NULL;
static int g_active_npc_transform_count = 0;

static void rc_npc_def_index_reset(void) {
    for (int i = 0; i < RC_MAX_NPC_ID; i++) g_npc_def_by_id[i] = -1;
    g_npc_def_index_ready = true;
}

static void npc_def_index_reset_into(int *def_by_id, int max_def_by_id) {
    if (!def_by_id || max_def_by_id <= 0) return;
    for (int i = 0; i < max_def_by_id; i++) def_by_id[i] = -1;
}

void rc_npc_use_defs(const RcNpcDef *defs, int count,
                     const int *def_by_id, const int32_t *transforms,
                     int transform_count) {
    g_active_npc_defs = defs ? defs : g_npc_defs;
    g_active_npc_def_count = defs ? count : 0;
    g_active_npc_def_by_id = defs ? def_by_id : g_npc_def_by_id;
    g_active_npc_transforms = defs ? transforms : g_npc_transforms;
    g_active_npc_transform_count = defs && transforms
                                 ? transform_count : g_npc_transform_count;
}

int rc_npc_mirror_defs_to_globals(const RcNpcDef *defs, int count,
                                  const int *def_by_id,
                                  const int32_t *transforms,
                                  int transform_count) {
    if (!defs || count < 0 || count > RC_MAX_NPC_DEFS
            || transform_count < 0
            || (transform_count > 0 && !transforms)) return 0;
    int32_t *transform_copy = NULL;
    if (transform_count > 0) {
        transform_copy = malloc(
            (size_t)transform_count * sizeof(*transform_copy));
        if (!transform_copy) return 0;
        memcpy(transform_copy, transforms,
               (size_t)transform_count * sizeof(*transform_copy));
    }
    memset(g_npc_defs, 0, sizeof(g_npc_defs));
    memcpy(g_npc_defs, defs, (size_t)count * sizeof(*defs));
    npc_def_index_reset_into(g_npc_def_by_id, RC_MAX_NPC_ID);
    if (def_by_id) {
        memcpy(g_npc_def_by_id, def_by_id, sizeof(g_npc_def_by_id));
    } else {
        for (int i = 0; i < count; i++) {
            int id = defs[i].id;
            if (id >= 0 && id < RC_MAX_NPC_ID) g_npc_def_by_id[id] = i;
        }
    }
    free(g_npc_transforms);
    g_npc_transforms = transform_copy;
    g_npc_transform_count = transform_count;
    g_npc_def_count = count;
    g_npc_def_index_ready = true;
    return 1;
}

void rc_npc_reset_defs_if_active(const RcNpcDef *defs) {
    if (defs && g_active_npc_defs == defs) {
        rc_npc_use_defs(g_npc_defs, g_npc_def_count,
                        g_npc_def_index_ready ? g_npc_def_by_id : NULL,
                        g_npc_transforms, g_npc_transform_count);
    }
}

// NDEF runtime schema: schema/defs/npc_defs.schema.toml.
#define NDEF_MAGIC 0x4E444546
#define NDEF_V1 1
#define NDEF_V2 2
#define NDEF_V3 3
#define NDEF_V4 4
#define NDEF_V5 5

static int append_transform(int32_t **values, int *count, int *capacity,
                            int32_t value) {
    if (!values || !count || !capacity || *count < 0 || *capacity < 0)
        return 0;
    if (*count >= *capacity) {
        int next = *capacity > 0 ? *capacity * 2 : 1024;
        if (next <= *capacity || next > INT_MAX / (int)sizeof(**values))
            return 0;
        int32_t *grown = realloc(*values, (size_t)next * sizeof(**values));
        if (!grown) return 0;
        *values = grown;
        *capacity = next;
    }
    (*values)[(*count)++] = value;
    return 1;
}

int rc_load_npc_defs_into(const char *path, RcNpcDef *defs, int max_defs,
                          int *out_count, int *def_by_id,
                          int max_def_by_id, int32_t **out_transforms,
                          int *out_transform_count) {
    if (out_count) *out_count = 0;
    if (out_transforms) *out_transforms = NULL;
    if (out_transform_count) *out_transform_count = 0;
    if (!path || !defs || max_defs <= 0 || !out_transforms
            || !out_transform_count) return -1;
    FILE *f = rc_asset_fopen(path, "rb");
    if (!f) { fprintf(stderr, "npc_defs: can't open %s\n", path); return -1; }

    uint32_t magic, version, count;
    if (!rc_read_exact(f, &magic, sizeof(magic), 1, path, "npc defs magic")
            || magic != NDEF_MAGIC) {
        rc_asset_close(f);
        fprintf(stderr, "npc_defs: bad magic\n");
        return -1;
    }
    if (!rc_read_exact(f, &version, sizeof(version), 1, path, "npc defs version")
            || !rc_read_exact(f, &count, sizeof(count), 1, path, "npc defs count")) {
        rc_asset_close(f);
        return -1;
    }
    if (version != NDEF_V1 && version != NDEF_V2
            && version != NDEF_V3 && version != NDEF_V4
            && version != NDEF_V5) {
        rc_asset_close(f);
        fprintf(stderr, "npc_defs: unsupported version %u\n", version);
        return -1;
    }
    if (count > (uint32_t)max_defs) {
        rc_asset_close(f);
        fprintf(stderr, "npc_defs: %u rows exceed capacity %d\n",
                count, max_defs);
        return -1;
    }

    memset(defs, 0, (size_t)max_defs * sizeof(*defs));
    npc_def_index_reset_into(def_by_id, max_def_by_id);
    int32_t *transforms = NULL;
    int transform_count = 0;
    int transform_capacity = 0;
    int loaded = 0;
    for (uint32_t i = 0; i < count; i++) {
        RcNpcDef *d = &defs[loaded];
        memset(d, 0, sizeof(RcNpcDef));
        uint32_t id;
        uint8_t size, name_len;
        int16_t cl;
        uint16_t hp;
        uint16_t stats[6];
        int32_t anims[5];

        if (!rc_read_exact(f, &id, sizeof(id), 1, path, "npc id")
                || !rc_read_exact(f, &size, sizeof(size), 1, path, "npc size")
                || !rc_read_exact(f, &cl, sizeof(cl), 1, path, "npc combat level")
                || !rc_read_exact(f, &hp, sizeof(hp), 1, path, "npc hitpoints")
                || !rc_read_exact(f, stats, sizeof(stats[0]), 6, path, "npc stats")
                || !rc_read_exact(f, anims, sizeof(anims[0]), 5, path, "npc anims")
                || !rc_read_exact(f, &name_len, sizeof(name_len), 1, path, "npc name length")) {
            goto fail;
        }
        if (name_len >= sizeof(d->name)) {
            fprintf(stderr, "npc_defs: name too long for id %u\n", id);
            goto fail;
        }
        if (!rc_read_exact(f, d->name, sizeof(char), name_len, path, "npc name")) {
            goto fail;
        }
        d->name[name_len] = 0;

        d->id = (int)id;
        d->size = size > 0 ? size : 1;
        d->combat_level = cl;
        d->hitpoints = hp;
        for (int j = 0; j < 6; j++) d->stats[j] = stats[j];
        d->wander_range = 5;
        d->respawn_ticks = 25;
        d->regen_ticks = version < NDEF_V5 ? 0 : 100;
        d->transform_varbit = -1;
        d->transform_varp = -1;
        uint8_t legacy_aggressive = 0;
        uint8_t legacy_aggro_range = 0;

        if (version >= NDEF_V2) {
            uint8_t aggr, atk_spd, aggro_r, atk_types, weak, immu;
            uint16_t max_hit, slayer_lvl;
            if (!rc_read_exact(f, &aggr, sizeof(aggr), 1, path, "npc aggression")
                    || !rc_read_exact(f, &max_hit, sizeof(max_hit), 1, path, "npc max hit")
                    || !rc_read_exact(f, &atk_spd, sizeof(atk_spd), 1, path, "npc attack speed")
                    || !rc_read_exact(f, &aggro_r, sizeof(aggro_r), 1, path, "npc aggro range")
                    || !rc_read_exact(f, &slayer_lvl, sizeof(slayer_lvl), 1, path, "npc slayer level")
                    || !rc_read_exact(f, &atk_types, sizeof(atk_types), 1, path, "npc attack types")
                    || !rc_read_exact(f, &weak, sizeof(weak), 1, path, "npc weakness")
                    || !rc_read_exact(f, &immu, sizeof(immu), 1, path, "npc immunities")) {
                goto fail;
            }
            legacy_aggressive    = aggr;
            d->max_hit           = (int)max_hit;
            d->attack_speed      = (int)atk_spd;
            legacy_aggro_range   = aggro_r;
            d->slayer_level      = (int)slayer_lvl;
            d->attack_types      = (int)atk_types;
            d->weakness          = (int)weak;
            d->poison_immune     = (immu & 1) != 0;
            d->venom_immune      = (immu & 2) != 0;
        }
        if (version >= NDEF_V3) {
            uint8_t model_count;
            if (!rc_read_exact(f, &model_count, sizeof(model_count), 1,
                               path, "npc model count")) {
                goto fail;
            }
            for (uint8_t j = 0; j < model_count; j++) {
                uint32_t model_id;
                if (!rc_read_exact(f, &model_id, sizeof(model_id), 1,
                                   path, "npc model id")) {
                    goto fail;
                }
            }
        }
        if (version >= NDEF_V4) {
            for (int j = 0; j < RC_NPC_OPTION_COUNT; j++) {
                uint8_t option_len;
                char option_buf[256];
                if (!rc_read_exact(f, &option_len, sizeof(option_len), 1,
                                   path, "npc option length")) {
                    goto fail;
                }
                if (option_len == 0) {
                    d->options[j][0] = '\0';
                    continue;
                }
                if (!rc_read_exact(f, option_buf, sizeof(char), option_len,
                                   path, "npc option")) {
                    goto fail;
                }
                int copy_len = option_len;
                if (copy_len >= RC_NPC_OPTION_LEN)
                    copy_len = RC_NPC_OPTION_LEN - 1;
                memcpy(d->options[j], option_buf, (size_t)copy_len);
                d->options[j][copy_len] = '\0';
            }
        }
        if (version >= NDEF_V5) {
            uint8_t wander, hunt_target, hunt_visibility, hunt_strength;
            uint8_t hunt_flags, hunt_range, hunt_rate;
            uint16_t respawn, regen, row_transform_count;
            int32_t transform_varbit, transform_varp;
            if (!rc_read_exact(f, &wander, sizeof(wander), 1, path,
                               "npc wander range")
                    || !rc_read_exact(f, &respawn, sizeof(respawn), 1, path,
                                      "npc respawn ticks")
                    || !rc_read_exact(f, &regen, sizeof(regen), 1, path,
                                      "npc regen ticks")
                    || !rc_read_exact(f, &hunt_target, sizeof(hunt_target), 1,
                                      path, "npc hunt target")
                    || !rc_read_exact(f, &hunt_visibility,
                                      sizeof(hunt_visibility), 1, path,
                                      "npc hunt visibility")
                    || !rc_read_exact(f, &hunt_strength,
                                      sizeof(hunt_strength), 1, path,
                                      "npc hunt strength")
                    || !rc_read_exact(f, &hunt_flags, sizeof(hunt_flags), 1,
                                      path, "npc hunt flags")
                    || !rc_read_exact(f, &hunt_range, sizeof(hunt_range), 1,
                                      path, "npc hunt range")
                    || !rc_read_exact(f, &hunt_rate, sizeof(hunt_rate), 1,
                                      path, "npc hunt rate")
                    || !rc_read_exact(f, &transform_varbit,
                                      sizeof(transform_varbit), 1, path,
                                      "npc transform varbit")
                    || !rc_read_exact(f, &transform_varp,
                                      sizeof(transform_varp), 1, path,
                                      "npc transform varp")
                    || !rc_read_exact(f, &row_transform_count,
                                      sizeof(row_transform_count), 1, path,
                                      "npc transform count")) {
                goto fail;
            }
            if (hunt_target > RC_NPC_HUNT_PLAYER
                    || hunt_visibility > RC_NPC_HUNT_VIS_LINE_OF_WALK
                    || hunt_strength
                        > RC_NPC_HUNT_STRENGTH_OUTSIDE_WILDERNESS
                    || (hunt_flags & ~(RC_NPC_HUNT_CHECK_NOT_BUSY
                                     | RC_NPC_HUNT_KEEP_HUNTING)) != 0) {
                fprintf(stderr, "npc_defs: invalid NPC policy for id %u\n",
                        id);
                goto fail;
            }
            d->wander_range = wander;
            d->respawn_ticks = respawn;
            d->regen_ticks = regen;
            d->hunt = (RcNpcHuntPolicy){
                .target = hunt_target,
                .visibility = hunt_visibility,
                .strength = hunt_strength,
                .flags = hunt_flags,
                .range = hunt_range,
                .rate = hunt_rate,
            };
            d->transform_varbit = transform_varbit;
            d->transform_varp = transform_varp;
            d->transform_offset = (uint32_t)transform_count;
            d->transform_count = row_transform_count;
            for (uint16_t j = 0; j < row_transform_count; j++) {
                int32_t transform;
                if (!rc_read_exact(f, &transform, sizeof(transform), 1, path,
                                   "npc transform id")
                        || !append_transform(&transforms, &transform_count,
                                             &transform_capacity, transform)) {
                    goto fail;
                }
            }
        } else if (legacy_aggressive) {
            d->hunt = (RcNpcHuntPolicy){
                .target = RC_NPC_HUNT_PLAYER,
                .visibility = RC_NPC_HUNT_VIS_LINE_OF_SIGHT,
                .strength = RC_NPC_HUNT_STRENGTH_OUTSIDE_WILDERNESS,
                .flags = RC_NPC_HUNT_KEEP_HUNTING,
                .range = legacy_aggro_range > 0 ? legacy_aggro_range : 1,
                .rate = 1,
            };
        }

        if (def_by_id && id < (uint32_t)max_def_by_id)
            def_by_id[id] = loaded;
        loaded++;
    }
    rc_asset_close(f);
    if (transform_count == 0) {
        free(transforms);
        transforms = NULL;
    } else {
        int32_t *trimmed = realloc(
            transforms, (size_t)transform_count * sizeof(*transforms));
        if (trimmed) transforms = trimmed;
    }
    if (out_count) *out_count = loaded;
    *out_transforms = transforms;
    *out_transform_count = transform_count;
    fprintf(stderr, "npc_defs: loaded %d defs (NDEF v%u) from %s\n",
            loaded, version, path);
    return loaded;

fail:
    free(transforms);
    rc_asset_close(f);
    return -1;
}

int rc_load_npc_defs(const char *path) {
    rc_npc_def_index_reset();
    int loaded = 0;
    int32_t *transforms = NULL;
    int transform_count = 0;
    int result = rc_load_npc_defs_into(
        path, g_npc_defs, RC_MAX_NPC_DEFS, &loaded, g_npc_def_by_id,
        RC_MAX_NPC_ID, &transforms, &transform_count);
    if (result < 0) {
        free(transforms);
        g_npc_def_count = 0;
        free(g_npc_transforms);
        g_npc_transforms = NULL;
        g_npc_transform_count = 0;
        rc_npc_use_defs(g_npc_defs, g_npc_def_count, g_npc_def_by_id,
                        NULL, 0);
        return -1;
    }
    free(g_npc_transforms);
    g_npc_transforms = transforms;
    g_npc_transform_count = transform_count;
    g_npc_def_count = loaded;
    g_npc_def_index_ready = true;
    rc_npc_use_defs(g_npc_defs, g_npc_def_count, g_npc_def_by_id,
                    g_npc_transforms, g_npc_transform_count);
    return result;
}

int rc_npc_def_find(int npc_id) {
    const RcNpcDef *defs = g_active_npc_defs ? g_active_npc_defs
                                             : g_npc_defs;
    int count = defs == g_npc_defs ? g_npc_def_count
                                   : g_active_npc_def_count;
    const int *by_id = defs == g_npc_defs
                     ? (g_npc_def_index_ready ? g_npc_def_by_id : NULL)
                     : g_active_npc_def_by_id;
    if (npc_id >= 0 && npc_id < RC_MAX_NPC_ID && by_id) {
        int idx = by_id[npc_id];
        if (idx >= 0 && idx < count && defs[idx].id == npc_id) {
            return idx;
        }
    }
    for (int i = 0; i < count; i++) {
        if (defs[i].id == npc_id) return i;
    }
    return -1;
}

const RcNpcDef *rc_npc_defs_all(int *count) {
    const RcNpcDef *defs = g_active_npc_defs ? g_active_npc_defs
                                             : g_npc_defs;
    int n = defs == g_npc_defs ? g_npc_def_count : g_active_npc_def_count;
    if (count) *count = n;
    return n > 0 ? defs : NULL;
}

const RcNpcDef *rc_npc_def_get(int def_idx) {
    const RcNpcDef *defs = g_active_npc_defs ? g_active_npc_defs
                                             : g_npc_defs;
    int count = defs == g_npc_defs ? g_npc_def_count
                                   : g_active_npc_def_count;
    if (def_idx >= 0 && def_idx < count) return &defs[def_idx];
    return NULL;
}

const RcNpcDef *rc_npc_base_def_for_npc(const RcNpc *npc) {
    return npc ? rc_npc_def_get(npc->def_id) : NULL;
}

static int npc_transform_value(const RcWorld *world, const RcNpcDef *def) {
    if (!world || !def) return -1;
    if (def->transform_varbit >= 0)
        return (int)rc_varbit_get(world, def->transform_varbit);
    if (def->transform_varp >= 0 && def->transform_varp < RC_MAX_VARPS)
        return world->varps[def->transform_varp];
    return -1;
}

int rc_npc_set_form(RcNpc *npc, int def_idx) {
    if (!npc || (def_idx >= 0 && !rc_npc_def_get(def_idx))) return 0;
    npc->form_def = def_idx >= 0 ? def_idx + 1 : 0;
    npc->combat.attack_prepared = false;
    return 1;
}

// Varbit/varp morphs; kept out of line so the per-tick lookup of the many
// definitions without transforms stays a bare index.
static __attribute__((noinline)) const RcNpcDef *resolve_npc_transforms(
    const RcWorld *world, const RcNpcDef *def) {
    for (int depth = 0; def && def->transform_count > 0 && depth < 32;
         depth++) {
        uint64_t end = (uint64_t)def->transform_offset
                     + def->transform_count;
        if (!g_active_npc_transforms
                || end > (uint64_t)g_active_npc_transform_count) {
            return NULL;
        }
        int value = npc_transform_value(world, def);
        int selected = value >= 0 && value < def->transform_count - 1
                     ? value : def->transform_count - 1;
        int npc_id = g_active_npc_transforms[def->transform_offset + selected];
        if (npc_id < 0) return NULL;
        int next = rc_npc_def_find(npc_id);
        if (next < 0) return NULL;
        const RcNpcDef *resolved = rc_npc_def_get(next);
        if (!resolved || resolved == def) return NULL;
        def = resolved;
    }
    return def && def->transform_count > 0 ? NULL : def;
}

const RcNpcDef *rc_npc_def_for_npc(const RcWorld *world, const RcNpc *npc) {
    if (!npc) return NULL;
    // An encounter form overrides the spawned definition.
    const RcNpcDef *def = rc_npc_def_get(npc->form_def > 0 ? npc->form_def - 1
                                                          : npc->def_id);
    if (!def || def->transform_count <= 0) return def;
    return resolve_npc_transforms(world, def);
}

int rc_npc_def_collect_form_ids(const RcNpcDef *def, int *out_ids,
                                int max_ids) {
    if (!def || !out_ids || max_ids <= 0) return 0;
    int count = 1;
    out_ids[0] = def->id;
    for (int at = 0; at < count; at++) {
        int def_idx = rc_npc_def_find(out_ids[at]);
        const RcNpcDef *form = rc_npc_def_get(def_idx);
        if (!form || form->transform_count == 0) continue;
        uint64_t end = (uint64_t)form->transform_offset
                     + form->transform_count;
        if (!g_active_npc_transforms
                || end > (uint64_t)g_active_npc_transform_count) {
            return -1;
        }
        for (uint16_t i = 0; i < form->transform_count; i++) {
            int id = g_active_npc_transforms[form->transform_offset + i];
            if (id < 0) continue;
            bool present = false;
            for (int j = 0; j < count; j++) {
                if (out_ids[j] == id) {
                    present = true;
                    break;
                }
            }
            if (present) continue;
            if (count >= max_ids) return -1;
            out_ids[count++] = id;
        }
    }
    return count;
}

int rc_npc_def_find_name(const char *name) {
    if (!name || !name[0]) return -1;
    int count = 0;
    (void)rc_npc_defs_all(&count);
    for (int i = 0; i < count; i++) {
        const RcNpcDef *def = rc_npc_def_get(i);
        if (def && strcmp(def->name, name) == 0) return i;
    }
    return -1;
}

const char *rc_npc_def_option(const RcNpcDef *def, int option_idx) {
    if (!def || option_idx < 0 || option_idx >= RC_NPC_OPTION_COUNT)
        return "";
    return def->options[option_idx];
}

bool rc_npc_def_option_is_attack(const RcNpcDef *def, int option_idx) {
    const char *option = rc_npc_def_option(def, option_idx);
    return strcmp(option, "Attack") == 0;
}

#define NSPI_MAGIC 0x4950534Eu
#define NSPI_RECORD_SIZE 20u
#define NPC_SPAWN_FLAG_INSTANCE 0x01

static uint32_t spawn_read_u32(const unsigned char **p) {
    const unsigned char *v = *p;
    *p += 4;
    return (uint32_t)v[0] | ((uint32_t)v[1] << 8)
         | ((uint32_t)v[2] << 16) | ((uint32_t)v[3] << 24);
}

static uint8_t spawn_read_u8(const unsigned char **p) {
    return *(*p)++;
}

static int load_npc_spawns_filtered(RcWorld *world, const char *path,
                                    int use_filter, int min_x, int min_y,
                                    int max_x, int max_y,
                                    int min_plane, int max_plane,
                                    uint32_t load_flags,
                                    RcNpcSpawnLoadStats *stats) {
    if (!world || !path) return -1;
    if (stats) memset(stats, 0, sizeof(*stats));
    RcSpawnIndexSlice slice = {0};
    if (!rc_spawn_index_read(path, NSPI_MAGIC, NSPI_RECORD_SIZE, use_filter,
                             min_x, min_y, max_x, max_y, &slice)
            || !rc_spawn_index_sort_source_order(&slice)) {
        rc_spawn_index_slice_free(&slice);
        fprintf(stderr, "npc_spawns: invalid indexed spawn file %s\n", path);
        return -1;
    }
    if (stats) {
        stats->total_rows = (int)slice.total_rows;
        stats->pages_loaded = (int)slice.pages_loaded;
        stats->rows_loaded = (int)slice.record_count;
        for (int plane = 0; plane < RC_MAX_PLANES; plane++) {
            stats->source_plane_counts[plane]
                = (int)slice.source_plane_counts[plane];
        }
    }

    for (uint32_t i = 0; i < slice.record_count; i++) {
        const unsigned char *record = slice.records
            + (size_t)i * slice.record_size;
        const unsigned char *p = record;
        uint32_t source_order = spawn_read_u32(&p);
        uint32_t nid = spawn_read_u32(&p);
        int32_t x = (int32_t)spawn_read_u32(&p);
        int32_t y = (int32_t)spawn_read_u32(&p);
        uint8_t plane = spawn_read_u8(&p);
        uint8_t direction = spawn_read_u8(&p);
        uint8_t wander_range = spawn_read_u8(&p);
        uint8_t flags = spawn_read_u8(&p);
        if (!rc_world_tile_valid(x, y, plane)) {
            if (stats) stats->skipped_invalid++;
            continue;
        }
        if (use_filter && (x < min_x || x > max_x || y < min_y || y > max_y
                || plane < min_plane || plane > max_plane)) {
            continue;
        }
        if (stats) {
            stats->matched_filter++;
            if (plane < RC_MAX_PLANES)
                stats->matched_plane_counts[plane]++;
        }

        // Instance-marked NPCs are skipped by default for conservative static
        // world loading. Viewers/validators can opt in when the active scene
        // window is the instance/dungeon map being inspected.
        if ((flags & NPC_SPAWN_FLAG_INSTANCE)
                && !(load_flags & RC_NPC_SPAWN_LOAD_INCLUDE_INSTANCE)) {
            if (stats) stats->skipped_instance++;
            continue;
        }

        int def_idx = rc_npc_def_find((int)nid);
        if (def_idx < 0) {
            if (stats) stats->skipped_missing_def++;
            continue;
        }
        RcNpcSpawnConfig config = {
            .spawn_key = rc_spawn_index_record_key(path, source_order, 0),
            .wander_range = wander_range == RC_NPC_SPAWN_WANDER_USE_DEF
                          ? -1 : wander_range,
            .direction = direction,
            .flags = rc_npc_def_get(def_idx)->respawn_ticks > 0
                   ? RC_NPC_SPAWN_RESPAWNS : 0,
        };
        RcNpcSpawnResult result = rc_npc_spawn_ex(
            world, def_idx, x, y, plane, &config);
        if (result.status == RC_NPC_SPAWN_CREATED) {
            if (stats) {
                stats->spawned++;
                if (plane < RC_MAX_PLANES)
                    stats->spawned_plane_counts[plane]++;
            }
        } else if (result.status == RC_NPC_SPAWN_EXISTING) {
            if (stats) stats->reused_existing++;
        } else if (stats && result.status == RC_NPC_SPAWN_CAPACITY) {
            stats->skipped_capacity++;
        } else if (stats && result.status == RC_NPC_SPAWN_MISSING_DEF) {
            stats->skipped_missing_def++;
        } else if (stats) {
            stats->skipped_invalid++;
        }
    }
    if (stats && use_filter) {
        stats->skipped_filter = stats->total_rows - stats->matched_filter
                              - stats->skipped_invalid;
    }
    rc_spawn_index_slice_free(&slice);
    fprintf(stderr, "npc_spawns: spawned %d NPCs from %s"
            " (pages %d, rows %d/%d, matched %d, skipped %d filtered,"
            " %d invalid, %d instance-only,"
            " %d missing-def, %d existing, %d capacity)\n",
            stats ? stats->spawned : 0, path,
            stats ? stats->pages_loaded : 0,
            stats ? stats->rows_loaded : 0,
            stats ? stats->total_rows : 0,
            stats ? stats->matched_filter : 0,
            stats ? stats->skipped_filter : 0,
            stats ? stats->skipped_invalid : 0,
            stats ? stats->skipped_instance : 0,
            stats ? stats->skipped_missing_def : 0,
            stats ? stats->reused_existing : 0,
            stats ? stats->skipped_capacity : 0);
    return stats ? stats->spawned : 0;
}

int rc_load_npc_spawns(RcWorld *world, const char *path) {
    RcNpcSpawnLoadStats stats;
    return load_npc_spawns_filtered(world, path, 0, 0, 0, 0, 0, 0, 0,
                                    0, &stats);
}

int rc_load_npc_spawns_rect(RcWorld *world, const char *path,
                            int min_x, int min_y, int max_x, int max_y,
                            int min_plane, int max_plane) {
    if (!world || !path || min_x > max_x || min_y > max_y
            || min_plane > max_plane || !rc_plane_valid(min_plane)
            || !rc_plane_valid(max_plane)) {
        return -1;
    }
    RcNpcSpawnLoadStats stats;
    return load_npc_spawns_filtered(world, path, 1, min_x, min_y, max_x,
                                    max_y, min_plane, max_plane, 0, &stats);
}

int rc_load_npc_spawns_rect_stats(RcWorld *world, const char *path,
                                  int min_x, int min_y,
                                  int max_x, int max_y,
                                  int min_plane, int max_plane,
                                  RcNpcSpawnLoadStats *stats) {
    if (!world || !path || !stats || min_x > max_x || min_y > max_y
            || min_plane > max_plane || !rc_plane_valid(min_plane)
            || !rc_plane_valid(max_plane)) {
        return -1;
    }
    return load_npc_spawns_filtered(world, path, 1, min_x, min_y, max_x,
                                    max_y, min_plane, max_plane, 0, stats);
}

int rc_load_npc_spawns_rect_stats_flags(RcWorld *world, const char *path,
                                        int min_x, int min_y,
                                        int max_x, int max_y,
                                        int min_plane, int max_plane,
                                        uint32_t load_flags,
                                        RcNpcSpawnLoadStats *stats) {
    if (!world || !path || !stats || min_x > max_x || min_y > max_y
            || min_plane > max_plane || !rc_plane_valid(min_plane)
            || !rc_plane_valid(max_plane)) {
        return -1;
    }
    return load_npc_spawns_filtered(world, path, 1, min_x, min_y, max_x,
                                    max_y, min_plane, max_plane, load_flags,
                                    stats);
}

int rc_load_npc_spawns_near(RcWorld *world, const char *path,
                            int center_x, int center_y, int radius,
                            int plane) {
    RcTileRect rect;
    if (!rc_plane_valid(plane)
            || !rc_tile_rect_around(center_x, center_y, radius, &rect)) {
        return -1;
    }
    return rc_load_npc_spawns_rect(world, path, rect.min_x, rect.min_y,
                                   rect.max_x, rect.max_y, plane, plane);
}

// A spawn faces its spawn direction at once, as the NPC add block's
// orientation does.
void rc_npc_face_spawn_direction(RcNpc *npc) {
    static const int8_t dx[8] = {-1, 0, 1, -1, 1, -1, 0, 1};
    static const int8_t dy[8] = { 1, 1, 1,  0, 0, -1,-1,-1};
    if (!npc) return;
    npc->facing_entity = -1;
    if (npc->spawn_direction >= 8) return;
    npc->face_angle = (int16_t)rc_direction_face_angle(rc_direction_from_delta(
        dx[npc->spawn_direction], dy[npc->spawn_direction]));
    npc->face_angle_seq++;
}

RcNpcLifePhase rc_npc_life_phase(const RcNpc *npc) {
    if (!npc || !npc->active) return RC_NPC_LIFE_REMOVED;
    if (!npc->is_dead) return RC_NPC_LIFE_ALIVE;
    return npc->death_timer > 0 ? RC_NPC_LIFE_DYING : RC_NPC_LIFE_HIDDEN;
}

void rc_npc_route_clear(RcNpc *npc, RcMovementResult result) {
    if (!npc) return;
    npc->route_len = 0;
    npc->route_idx = 0;
    npc->route_continue = false;
    npc->route_status = result == RC_MOVEMENT_BLOCKED
                      ? RC_ROUTE_BLOCKED : RC_ROUTE_FAILED;
    npc->route_mode = RC_NPC_ROUTE_NONE;
    npc->movement_result = result;
}

void rc_npc_reset_life(RcWorld *world, RcNpc *npc) {
    if (!npc) return;
    if (world && npc->spawn_hp > 0)
        rc_npc_clear_references(world, (RcNpcId)npc->uid);
    int def_id = npc->def_id;
    int uid = npc->uid;
    uint64_t spawn_key = npc->spawn_key;
    int spawn_x = npc->spawn_x;
    int spawn_y = npc->spawn_y;
    int spawn_plane = npc->spawn_plane;
    int spawn_wander_range = npc->spawn_wander_range;
    uint8_t spawn_direction = npc->spawn_direction;
    // The sequence keeps counting, so a reset's face angle always reads
    // as new.
    uint32_t face_angle_seq = npc->face_angle_seq;
    bool respawns = npc->respawns;
    bool disable_wander = npc->disable_wander;
    bool force_player_max_hit = npc->force_player_max_hit;
    bool player_untargetable = npc->player_untargetable;
    bool active = npc->active;

    memset(npc, 0, sizeof(*npc));
    npc->def_id = def_id;
    npc->uid = uid;
    npc->spawn_key = spawn_key;
    npc->spawn_x = spawn_x;
    npc->spawn_y = spawn_y;
    npc->spawn_plane = spawn_plane;
    npc->spawn_wander_range = spawn_wander_range;
    npc->spawn_direction = spawn_direction;
    npc->face_angle_seq = face_angle_seq;
    npc->respawns = respawns;
    npc->disable_wander = disable_wander;
    npc->force_player_max_hit = force_player_max_hit;
    npc->player_untargetable = player_untargetable;
    npc->active = active;
    npc->x = spawn_x;
    npc->y = spawn_y;
    npc->plane = spawn_plane;
    npc->prev_x = spawn_x;
    npc->prev_y = spawn_y;
    npc->target_uid = -1;
    npc->facing_entity = -1;
    npc->last_hit = -1;
    npc->route_status = RC_ROUTE_FAILED;
    npc->movement_result = RC_MOVEMENT_NONE;
    const RcNpcDef *def = rc_npc_def_for_npc(world, npc);
    if (!def) def = rc_npc_base_def_for_npc(npc);
    npc->spawn_hp = def && def->hitpoints > 0 ? def->hitpoints : 1;
    npc->current_hp = npc->spawn_hp;
    for (int i = 0; i < 6; i++) npc->stats[i] = def ? def->stats[i] : 1;
    rc_npc_face_spawn_direction(npc);
    rc_combat_init_npc_state(npc);
}

static int npc_slot_for_spawn_key(const RcWorld *world, uint64_t spawn_key) {
    if (!world || spawn_key == 0) return -1;
    for (int i = 0; i < world->npc_count; i++) {
        if (world->npcs[i].active && world->npcs[i].spawn_key == spawn_key)
            return i;
    }
    return -1;
}

RcNpcSpawnResult rc_npc_spawn_ex(RcWorld *world, int def_idx, int world_x,
                                 int world_y, int plane,
                                 const RcNpcSpawnConfig *config) {
    RcNpcSpawnResult result = {
        .status = RC_NPC_SPAWN_INVALID,
        .slot = -1,
        .uid = RC_NPC_NONE,
    };
    if (!world || !world->npcs || !config || config->wander_range < -1
            || config->direction >= 8 || world->next_npc_uid < 0
            || world->next_npc_uid == INT_MAX
            || !rc_world_tile_valid(world_x, world_y, plane)) {
        return result;
    }
    const RcNpcDef *def = rc_npc_def_get(def_idx);
    if (!def) {
        result.status = RC_NPC_SPAWN_MISSING_DEF;
        return result;
    }
    RcTileBounds footprint;
    int size = def->size > 0 ? def->size : 1;
    if (!rc_tile_bounds_from_origin_size(world_x, world_y, size, size,
                                         plane, &footprint)) {
        return result;
    }
    int existing = npc_slot_for_spawn_key(world, config->spawn_key);
    if (existing >= 0) {
        result.status = RC_NPC_SPAWN_EXISTING;
        result.slot = existing;
        result.uid = (RcNpcId)world->npcs[existing].uid;
        return result;
    }
    int slot = -1;
    for (int i = 0; i < world->npc_count; i++) {
        if (!world->npcs[i].active) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        if (world->npc_count >= world->npc_capacity) {
            result.status = RC_NPC_SPAWN_CAPACITY;
            return result;
        }
        slot = world->npc_count++;
    }

    RcNpc *npc = &world->npcs[slot];
    memset(npc, 0, sizeof(*npc));
    npc->def_id = def_idx;
    npc->uid = world->next_npc_uid++;
    npc->spawn_key = config->spawn_key;
    npc->spawn_x = world_x;
    npc->spawn_y = world_y;
    npc->spawn_plane = plane;
    npc->spawn_wander_range = config->wander_range >= 0
                            ? config->wander_range : def->wander_range;
    npc->spawn_direction = config->direction;
    npc->respawns = (config->flags & RC_NPC_SPAWN_RESPAWNS) != 0;
    npc->active = true;
    rc_npc_reset_life(world, npc);

    RcPayloadNpcEvent payload = {
        .npc_id = (uint32_t)npc->uid,
        .def_id = (uint32_t)def->id,
        .spawn_key = npc->spawn_key,
    };
    rc_event_fire(world, RC_EVT_NPC_SPAWNED, &payload);
    result.status = RC_NPC_SPAWN_CREATED;
    result.slot = slot;
    result.uid = (RcNpcId)npc->uid;
    return result;
}

int rc_npc_spawn(RcWorld *world, int def_idx, int world_x, int world_y,
                 int plane) {
    const RcNpcDef *def = rc_npc_def_get(def_idx);
    RcNpcSpawnConfig config = {
        .spawn_key = 0,
        .wander_range = -1,
        .direction = 6,
        .flags = def && def->respawn_ticks > 0 ? RC_NPC_SPAWN_RESPAWNS : 0,
    };
    RcNpcSpawnResult result = rc_npc_spawn_ex(
        world, def_idx, world_x, world_y, plane, &config);
    return result.status == RC_NPC_SPAWN_CREATED ? result.slot : -1;
}

RcNpc *rc_npc_resolve(RcWorld *world, RcNpcId uid) {
    if (!world || uid == RC_NPC_NONE || uid > INT_MAX) return NULL;
    for (int i = 0; i < world->npc_count; i++) {
        RcNpc *npc = &world->npcs[i];
        if (npc->active && npc->uid == (int)uid) return npc;
    }
    return NULL;
}

const RcNpc *rc_npc_resolve_const(const RcWorld *world, RcNpcId uid) {
    if (!world || uid == RC_NPC_NONE || uid > INT_MAX) return NULL;
    for (int i = 0; i < world->npc_count; i++) {
        const RcNpc *npc = &world->npcs[i];
        if (npc->active && npc->uid == (int)uid) return npc;
    }
    return NULL;
}

static void clear_actor_npc_reference(RcCombatActorState *state, int uid) {
    if (!state) return;
    if (state->target.kind == RC_COMBAT_ACTOR_NPC && state->target.uid == uid) {
        state->target = (RcCombatTargetRef){0};
        state->active = false;
    }
    if (state->primary_attacker.kind == RC_COMBAT_ACTOR_NPC
            && state->primary_attacker.uid == uid) {
        state->primary_attacker = (RcCombatActorRef){0};
    }
    int write = 0;
    for (int i = 0; i < state->attacker_count; i++) {
        if (state->attackers[i].kind == RC_COMBAT_ACTOR_NPC
                && state->attackers[i].uid == uid) continue;
        state->attackers[write] = state->attackers[i];
        state->attacker_ticks[write++] = state->attacker_ticks[i];
    }
    state->attacker_count = write;
    write = 0;
    for (int i = 0; i < state->recent_hit_count; i++) {
        if (state->recent_hits[i].source_uid == uid) continue;
        state->recent_hits[write++] = state->recent_hits[i];
    }
    state->recent_hit_count = write;
}

void rc_npc_clear_references(RcWorld *world, RcNpcId uid_value) {
    if (!world || uid_value == RC_NPC_NONE || uid_value > INT_MAX) return;
    int uid = (int)uid_value;
    RcPlayer *player = &world->player;
    if (player->storage_session && player->storage_source.kind == RC_INTERACTION_NPC
            && player->storage_source.entity_uid == uid) rc_storage_close(world);
    if (player->attack_target == uid) {
        player->attack_target = -1;
        player->attack_target_def_id = -1;
    }
    if (player->facing_entity == uid) player->facing_entity = -1;
    if (player->interact_target == uid) {
        player->interact_type = RC_INTERACT_NONE;
        player->interact_target = -1;
        player->interact_option = -1;
    }
    if (player->interaction.active
            && player->interaction.target.kind == RC_INTERACTION_NPC
            && player->interaction.target.entity_uid == uid) {
        rc_interaction_clear(player);
    }
    int pending = 0;
    for (int i = 0; i < player->num_pending_hits; i++) {
        if (player->pending_hits[i].active && player->pending_hits[i].source_idx == uid) {
            if (player->pending_hits[i].attack_style != COMBAT_RANGED
                    && player->pending_hits[i].attack_style != COMBAT_MAGIC)
                player->pending_hits[i].active = 0;
            else player->pending_hits[i].flags |= RC_HIT_SOURCE_EXPIRED;
        }
        if (player->pending_hits[i].active)
            player->pending_hits[pending++] = player->pending_hits[i];
    }
    player->num_pending_hits = pending;
    clear_actor_npc_reference(&player->combat, uid);
    for (int i = 0; i < world->npc_count; i++) {
        RcNpc *other = &world->npcs[i];
        // NPC target/facing IDs refer to players, not the NPC being removed.
        for (int h = 0; h < other->num_pending_hits; h++) {
            if (other->pending_hits[h].active
                    && other->pending_hits[h].source_idx == uid)
                other->pending_hits[h].active = 0;
        }
        clear_actor_npc_reference(&other->combat, uid);
    }
}

int rc_npc_remove(RcWorld *world, RcNpcId uid) {
    RcNpc *npc = rc_npc_resolve(world, uid);
    if (!npc) return 0;
    const RcNpcDef *def = rc_npc_base_def_for_npc(npc);
    RcPayloadNpcEvent payload = {
        .npc_id = (uint32_t)npc->uid,
        .def_id = def ? (uint32_t)def->id : UINT32_MAX,
        .spawn_key = npc->spawn_key,
    };
    rc_npc_clear_references(world, uid);
    memset(npc, 0, sizeof(*npc));
    while (world->npc_count > 0
            && !world->npcs[world->npc_count - 1].active)
        world->npc_count--;
    rc_event_fire(world, RC_EVT_NPC_REMOVED, &payload);
    return 1;
}

bool rc_npc_route_request(RcWorld *world, RcNpc *npc,
                          const RcRouteTarget *target, RcNpcRouteMode mode,
                          bool allow_alternative) {
    if (!world || !npc || !target || !npc->active || npc->is_dead
            || mode == RC_NPC_ROUTE_NONE || mode > RC_NPC_ROUTE_SCRIPTED
            || (allow_alternative && mode != RC_NPC_ROUTE_SCRIPTED)) return false;
    const RcNpcDef *def = rc_npc_def_for_npc(world, npc);
    if (!def) return false;
    int size = def->size > 0 ? def->size : 1;
    if (mode != RC_NPC_ROUTE_SCRIPTED) {
        RcTileBounds bounds;
        if (!rc_tile_bounds_from_origin_size(target->x, target->y,
                target->width, target->height, npc->plane, &bounds)
                || target->kind > RC_ROUTE_REACH_RECTANGLE
                || target->min_distance < 0
                || target->max_distance < target->min_distance) {
            rc_npc_route_clear(npc, RC_MOVEMENT_NO_ROUTE);
            return false;
        }
        if (rc_route_target_reached(&world->map, npc->plane,
                                    npc->x, npc->y, size, size, target)) {
            rc_npc_route_clear(npc, RC_MOVEMENT_ARRIVED);
            return true;
        }
        npc->route_target = *target;
        npc->route_x[0] = target->x;
        npc->route_y[0] = target->y;
        npc->route_idx = 0;
        npc->route_len = 1;
        npc->route_continue = false;
        npc->route_status = RC_ROUTE_EXACT;
        npc->route_mode = (uint8_t)mode;
        npc->movement_result = RC_MOVEMENT_ROUTE_ADMITTED;
        return true;
    }
    RcRoute route = rc_find_route(&world->map, npc->x, npc->y, size, size,
                                  npc->plane, target, allow_alternative);
    if (!rc_route_status_admitted(route.status)) {
        rc_npc_route_clear(npc, RC_MOVEMENT_NO_ROUTE);
        return false;
    }
    if (route.status == RC_ROUTE_ALREADY_ARRIVED) {
        rc_npc_route_clear(npc, RC_MOVEMENT_ARRIVED);
        return true;
    }
    npc->route_len = route.length < RC_MAX_ROUTE
                   ? route.length : RC_MAX_ROUTE;
    npc->route_idx = 0;
    memcpy(npc->route_x, route.waypoints_x,
           (size_t)npc->route_len * sizeof(*npc->route_x));
    memcpy(npc->route_y, route.waypoints_y,
           (size_t)npc->route_len * sizeof(*npc->route_y));
    npc->route_target = *target;
    npc->route_continue = route.status == RC_ROUTE_PARTIAL;
    npc->route_status = route.status;
    npc->route_mode = (uint8_t)mode;
    npc->movement_result = RC_MOVEMENT_ROUTE_ADMITTED;
    return true;
}

static int continue_npc_route(RcWorld *world, RcNpc *npc) {
    if (!npc->route_continue) return 0;
    RcNpcRouteMode mode = (RcNpcRouteMode)npc->route_mode;
    RcRouteTarget target = npc->route_target;
    return rc_npc_route_request(world, npc, &target, mode, false)
        && npc->route_idx < npc->route_len;
}

static int npc_overlaps_target(int x, int y, int size,
                               const RcRouteTarget *target) {
    return x < target->x + target->width && x + size > target->x
        && y < target->y + target->height && y + size > target->y;
}

static void npc_arrive(RcNpc *npc) {
    RcNpcRouteMode mode = (RcNpcRouteMode)npc->route_mode;
    rc_npc_route_clear(npc, RC_MOVEMENT_ARRIVED);
    if (mode == RC_NPC_ROUTE_WANDER || mode == RC_NPC_ROUTE_RETURN)
        npc->wander_timer = 0;
}

// The heading of a direct step toward a target the NPC does not overlap.
static void npc_direct_heading(int x, int y, int size,
                               const RcRouteTarget *target, int *dx, int *dy) {
    if (target->allow_inside) {
        *dx = (target->x > x) - (target->x < x);
        *dy = (target->y > y) - (target->y < y);
        return;
    }
    *dx = x + size <= target->x ? 1 : x >= target->x + target->width ? -1 : 0;
    *dy = y + size <= target->y ? 1 : y >= target->y + target->height ? -1 : 0;
}

// Ordinary travel tries both axes, then X, then Y. It never searches a
// detour. Returns 0 when every step is blocked. Inline: it is every moving
// NPC's step, and a call costs about a sixth of it.
static inline int npc_try_direct_step(const RcWorldMap *map, int plane, int x,
                                      int y, int size,
                                      const RcRouteTarget *target, int dx,
                                      int dy, int overlap, int *step_x,
                                      int *step_y) {
    for (int i = 0; i < 3; i++) {
        int sx = i == 2 ? 0 : dx, sy = i == 1 ? 0 : dy;
        if ((!sx && !sy) || (i > 0 && (!dx || !dy))) continue;
        if (!overlap && !target->allow_inside
                && npc_overlaps_target(x + sx, y + sy, size, target))
            continue;
        if (!rc_can_move_rect(map, x, y, size, size, sx, sy, plane)) continue;
        *step_x = sx;
        *step_y = sy;
        return 1;
    }
    return 0;
}

bool rc_npc_direct_walk_reaches(const RcWorldMap *map, int plane, int x,
                                int y, int size, const RcRouteTarget *target,
                                int max_steps) {
    if (!map || !target || size < 1) return false;
    for (int step = 0; step <= max_steps; step++) {
        if (rc_route_target_reached(map, plane, x, y, size, size, target))
            return true;
        // Stepping out of an overlapped target is a random cardinal.
        if (!target->allow_inside && npc_overlaps_target(x, y, size, target))
            return false;
        int dx, dy, sx, sy;
        npc_direct_heading(x, y, size, target, &dx, &dy);
        if (!npc_try_direct_step(map, plane, x, y, size, target, dx, dy, 0,
                                 &sx, &sy))
            return false;
        x += sx;
        y += sy;
    }
    return false;
}

static void npc_direct_step(RcWorld *world, RcNpc *npc, int size) {
    const RcRouteTarget *target = &npc->route_target;
    if (rc_route_target_reached(&world->map, npc->plane,
                                npc->x, npc->y, size, size, target)) {
        npc_arrive(npc);
        return;
    }
    int dx, dy;
    int overlap = !target->allow_inside
        && npc_overlaps_target(npc->x, npc->y, size, target);
    if (overlap) {
        // An overlapped target requires a cardinal step away, not a path search.
        static const int directions[4][2] = {{0, -1}, {-1, 0}, {0, 1}, {1, 0}};
        int direction = rc_rng_range(&world->rng_state, 3);
        dx = directions[direction][0];
        dy = directions[direction][1];
    } else {
        npc_direct_heading(npc->x, npc->y, size, target, &dx, &dy);
    }
    int sx, sy;
    if (npc_try_direct_step(&world->map, npc->plane, npc->x, npc->y, size,
                            target, dx, dy, overlap, &sx, &sy)) {
        npc->x += sx;
        npc->y += sy;
        npc->movement_result = RC_MOVEMENT_MOVED;
        if (rc_route_target_reached(&world->map, npc->plane,
                                    npc->x, npc->y, size, size, target))
            npc_arrive(npc);
        return;
    }
    npc->movement_result = RC_MOVEMENT_BLOCKED;
    // Pursuit may resume when the obstruction moves. Idle wandering can reselect.
    if (npc->route_mode == RC_NPC_ROUTE_WANDER)
        rc_npc_route_clear(npc, RC_MOVEMENT_BLOCKED);
}

void rc_npc_movement_tick(RcWorld *world, RcNpc *npc) {
    if (!world || !npc || rc_npc_life_phase(npc) != RC_NPC_LIFE_ALIVE)
        return;
    if (world->tick < npc->immobilized_until) return;
    if (npc->route_idx >= npc->route_len && !continue_npc_route(world, npc))
        return;
    const RcNpcDef *def = rc_npc_def_for_npc(world, npc);
    // An anchored boss form never walks: whoever asked for the route (chase,
    // wander, return), it is dropped, as rsmod drops a NoMove NPC's.
    if (def && rc_encounter_def_anchored(&world->encounter,
                                         (uint32_t)def->id)) {
        rc_npc_route_clear(npc, RC_MOVEMENT_BLOCKED);
        return;
    }
    int size = def && def->size > 0 ? def->size : 1;
    if (npc->route_mode != RC_NPC_ROUTE_SCRIPTED) {
        npc_direct_step(world, npc, size);
        return;
    }
    int next_x = npc->route_x[npc->route_idx];
    int next_y = npc->route_y[npc->route_idx];
    int dx = next_x - npc->x;
    int dy = next_y - npc->y;
    if (dx > 1) dx = 1;
    if (dx < -1) dx = -1;
    if (dy > 1) dy = 1;
    if (dy < -1) dy = -1;
    if (!dx && !dy) {
        npc->route_idx++;
        return;
    }
    if (!rc_can_move_rect(&world->map, npc->x, npc->y, size, size,
                          dx, dy, npc->plane)) {
        rc_npc_route_clear(npc, RC_MOVEMENT_BLOCKED);
        return;
    }
    npc->x += dx;
    npc->y += dy;
    npc->movement_result = RC_MOVEMENT_MOVED;
    if (npc->x == next_x && npc->y == next_y) npc->route_idx++;
    if (npc->route_idx >= npc->route_len && !npc->route_continue) {
        RcRouteStatus status = npc->route_status;
        rc_npc_route_clear(npc, status == RC_ROUTE_ALTERNATIVE
                               ? RC_MOVEMENT_NO_ROUTE : RC_MOVEMENT_ARRIVED);
    }
}

bool rc_npc_apply_poison(RcWorld *world, RcNpc *npc, int damage) {
    if (!world || !npc || npc->is_dead || damage <= 0) return false;
    const RcNpcDef *def = rc_npc_def_for_npc(world, npc);
    if (!def || def->poison_immune)
        return false;
    if (damage > npc->poison_damage) npc->poison_damage = damage;
    npc->poison_tick_counter = 30;
    return true;
}

void rc_npc_status_tick(RcWorld *world, RcNpc *npc) {
    if (!world || !npc || rc_npc_life_phase(npc) != RC_NPC_LIFE_ALIVE)
        return;
    const RcNpcDef *def = rc_npc_def_for_npc(world, npc);
    if (!def) return;
    if (def->regen_ticks > 0) {
        if (npc->regen_timer > 0) {
            npc->regen_timer--;
        } else {
            npc->regen_timer = def->regen_ticks;
            for (int i = 0; i < 6; i++) {
                if (npc->stats[i] < def->stats[i]) npc->stats[i]++;
                else if (npc->stats[i] > def->stats[i]) npc->stats[i]--;
            }
            if (npc->current_hp < npc->spawn_hp) npc->current_hp++;
        }
    }
    if (npc->poison_damage > 0) {
        if (npc->poison_tick_counter > 0) {
            npc->poison_tick_counter--;
        } else {
            if (!rc_queue_hit_meta(npc->pending_hits, &npc->num_pending_hits,
                              npc->poison_damage, 0, COMBAT_NONE,
                              RC_HIT_SOURCE_STATUS, 0,
                              world->tick, 0, npc->poison_damage)) return;
            npc->poison_damage--;
            npc->poison_tick_counter = npc->poison_damage > 0 ? 30 : 0;
        }
    }
}

static void fire_npc_lifecycle(RcWorld *world, int event, const RcNpc *npc) {
    const RcNpcDef *def = rc_npc_base_def_for_npc(npc);
    RcPayloadNpcEvent payload = {
        .npc_id = (uint32_t)npc->uid,
        .def_id = def ? (uint32_t)def->id : UINT32_MAX,
        .spawn_key = npc->spawn_key,
    };
    rc_event_fire(world, event, &payload);
}

void rc_npc_tick(RcWorld *world, RcNpc *npc) {
    if (!world || !npc || !npc->active) return;
    int moved_last = npc->x != npc->prev_x || npc->y != npc->prev_y;
    npc->prev_x = npc->x;
    npc->prev_y = npc->y;
    if (npc->last_hit_timer > 0) npc->last_hit_timer--;
    rc_combat_actor_tick_recent_hits(&npc->combat);
    rc_combat_tick_actor_threat(&npc->combat);

    const RcNpcDef *def = rc_npc_def_for_npc(world, npc);
    if (!def) def = rc_npc_base_def_for_npc(npc);
    if (!def) return;
    if (npc->despawn_tick && world->tick >= npc->despawn_tick && !npc->is_dead) {
        rc_npc_remove(world, (RcNpcId)npc->uid);
        return;
    }
    if (npc->is_dead) {
        if (npc->death_timer > 0) {
            npc->death_timer--;
            if (npc->death_timer == 0) {
                fire_npc_lifecycle(world, RC_EVT_NPC_HIDDEN, npc);
                if (!npc->respawns) {
                    RcNpcId uid = (RcNpcId)npc->uid;
                    rc_npc_remove(world, uid);
                }
            }
            return;
        }
        if (npc->respawn_timer > 0) {
            npc->respawn_timer--;
            if (npc->respawn_timer > 0) return;
        }
        if (!npc->respawns) {
            rc_npc_remove(world, (RcNpcId)npc->uid);
            return;
        }
        if (npc->respawns) {
            rc_npc_reset_life(world, npc);
            fire_npc_lifecycle(world, RC_EVT_NPC_RESPAWNED, npc);
        }
        return;
    }

    if (moved_last) npc->wander_timer = 0;
    else npc->wander_timer++;
    if (npc->target_uid >= 0 || npc->disable_wander || npc->owner_controlled
            || npc->spawn_wander_range <= 0) return;
    if (npc->route_mode == RC_NPC_ROUTE_WANDER
            || npc->route_mode == RC_NPC_ROUTE_RETURN) return;
    if (npc->wander_timer >= 500) {
        if (npc->x == npc->spawn_x && npc->y == npc->spawn_y) {
            npc->wander_timer = 0;
            return;
        }
        RcRouteTarget target = rc_route_target_point(
            npc->spawn_x, npc->spawn_y);
        if (!rc_npc_route_request(world, npc, &target,
                                  RC_NPC_ROUTE_RETURN, false)) {
            npc->x = npc->spawn_x;
            npc->y = npc->spawn_y;
            npc->prev_x = npc->x;
            npc->prev_y = npc->y;
            npc->wander_timer = 0;
        }
        return;
    }
    if (rc_rng_range(&world->rng_state, 7) == 0) {
        int range = npc->spawn_wander_range;
        int dx = rc_rng_range(&world->rng_state, 2 * range) - range;
        int dy = rc_rng_range(&world->rng_state, 2 * range) - range;
        RcRouteTarget target = rc_route_target_point(
            npc->spawn_x + dx, npc->spawn_y + dy);
        (void)rc_npc_route_request(world, npc, &target,
                                   RC_NPC_ROUTE_WANDER, false);
    }
}
