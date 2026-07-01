#include "types.h"
#include "normalization.h"
#include "io.h"

#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NORM_MAGIC 0x4D524F4Eu
#define NORM_VERSION 1u

RcItemNormalization g_rc_item_normalization[RC_MAX_ITEM_DEFS];
RcNpcNormalization g_rc_npc_normalization[RC_MAX_NPC_ID];
RcSourceNormalization *g_rc_source_normalization = NULL;
int g_rc_item_normalization_count = 0;
int g_rc_npc_normalization_count = 0;
int g_rc_source_normalization_count = 0;

static const RcItemNormalization *g_active_item_normalization =
    g_rc_item_normalization;
static const RcNpcNormalization *g_active_npc_normalization =
    g_rc_npc_normalization;
static const RcSourceNormalization *g_active_source_normalization = NULL;
static int g_active_item_normalization_count = 0;
static int g_active_npc_normalization_count = 0;
static int g_active_source_normalization_count = 0;

static int id_or_missing(uint32_t value) {
    return value == UINT32_MAX ? -1 : (int)value;
}

void rc_normalization_use_defs(const RcItemNormalization *item_defs,
                               int item_count,
                               const RcNpcNormalization *npc_defs,
                               int npc_count,
                               const RcSourceNormalization *source_defs,
                               int source_count) {
    int use_globals = !item_defs && !npc_defs && !source_defs;
    if (use_globals) {
        g_active_item_normalization = g_rc_item_normalization;
        g_active_item_normalization_count = g_rc_item_normalization_count;
        g_active_npc_normalization = g_rc_npc_normalization;
        g_active_npc_normalization_count = g_rc_npc_normalization_count;
        g_active_source_normalization = g_rc_source_normalization;
        g_active_source_normalization_count = g_rc_source_normalization_count;
        return;
    }
    g_active_item_normalization = item_defs;
    g_active_item_normalization_count = item_defs ? item_count : 0;
    g_active_npc_normalization = npc_defs;
    g_active_npc_normalization_count = npc_defs ? npc_count : 0;
    g_active_source_normalization = source_defs;
    g_active_source_normalization_count = source_defs ? source_count : 0;
}

void rc_normalization_reset_defs_if_active(
    const RcItemNormalization *item_defs,
    const RcNpcNormalization *npc_defs,
    const RcSourceNormalization *source_defs) {
    if ((item_defs && g_active_item_normalization == item_defs)
            || (npc_defs && g_active_npc_normalization == npc_defs)
            || (source_defs && g_active_source_normalization == source_defs)) {
        rc_normalization_use_defs(NULL, 0, NULL, 0, NULL, 0);
    }
}

uint32_t rc_normalization_hash_key(const char *name) {
    char buf[128];
    int out = 0, in_paren = 0, last_space = 1;
    if (!name) name = "";
    for (const unsigned char *p = (const unsigned char *)name; *p; p++) {
        unsigned char c = *p;
        if (c == '(') { in_paren = 1; continue; }
        if (c == ')') { in_paren = 0; continue; }
        if (in_paren) continue;
        if (isalnum(c)) {
            if (out < (int)sizeof(buf) - 1) {
                buf[out++] = (char)tolower(c);
                last_space = 0;
            }
        } else if (!last_space && out < (int)sizeof(buf) - 1) {
            buf[out++] = ' ';
            last_space = 1;
        }
    }
    while (out > 0 && buf[out - 1] == ' ') out--;
    buf[out] = '\0';

    uint32_t h = 0x811C9DC5u;
    for (int i = 0; i < out; i++) {
        h ^= (uint8_t)buf[i];
        h *= 0x01000193u;
    }
    return h;
}

int rc_load_normalization_into(const char *path,
                               RcItemNormalization *item_defs,
                               int max_item_defs, int *out_item_count,
                               RcNpcNormalization *npc_defs,
                               int max_npc_defs, int *out_npc_count,
                               RcSourceNormalization **source_defs,
                               int *out_source_count) {
    if (out_item_count) *out_item_count = 0;
    if (out_npc_count) *out_npc_count = 0;
    if (out_source_count) *out_source_count = 0;
    if (source_defs) *source_defs = NULL;
    if (!path || !item_defs || max_item_defs <= 0 || !npc_defs
            || max_npc_defs <= 0 || !source_defs) {
        return -1;
    }

    FILE *f = rc_asset_fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "normalization: can't open %s\n", path);
        return -1;
    }

    uint32_t magic, version, raw_item_count, raw_npc_count, raw_source_count;
    if (!rc_read_exact(f, &magic, sizeof(magic), 1, path, "magic")
            || !rc_read_exact(f, &version, sizeof(version), 1,
                              path, "version")
            || !rc_read_exact(f, &raw_item_count, sizeof(raw_item_count), 1,
                              path, "item count")
            || !rc_read_exact(f, &raw_npc_count, sizeof(raw_npc_count), 1,
                              path, "npc count")
            || !rc_read_exact(f, &raw_source_count, sizeof(raw_source_count), 1,
                              path, "source count")) {
        rc_asset_close(f);
        return -1;
    }
    if (magic != NORM_MAGIC || version == 0 || version > NORM_VERSION) {
        rc_asset_close(f);
        fprintf(stderr, "normalization: bad header\n");
        return -1;
    }
    if (raw_source_count > (uint32_t)INT_MAX) {
        rc_asset_close(f);
        fprintf(stderr, "normalization: too many source rows\n");
        return -1;
    }

    memset(item_defs, 0, (size_t)max_item_defs * sizeof(*item_defs));
    memset(npc_defs, 0, (size_t)max_npc_defs * sizeof(*npc_defs));

    RcSourceNormalization *sources = NULL;
    if (raw_source_count) {
        sources = calloc(raw_source_count, sizeof(*sources));
        if (!sources) {
            rc_asset_close(f);
            return -1;
        }
    }

    int loaded_items = 0;
    int loaded_npcs = 0;
    int loaded_sources = 0;

    for (uint32_t i = 0; i < raw_item_count; i++) {
        uint32_t item_id, canonical_id, noted_id, placeholder_id, key_hash;
        uint16_t flags;
        if (!rc_read_exact(f, &item_id, sizeof(item_id), 1, path, "item id")
                || !rc_read_exact(f, &canonical_id, sizeof(canonical_id), 1,
                                  path, "item canonical")
                || !rc_read_exact(f, &noted_id, sizeof(noted_id), 1,
                                  path, "item noted")
                || !rc_read_exact(f, &placeholder_id,
                                  sizeof(placeholder_id), 1,
                                  path, "item placeholder")
                || !rc_read_exact(f, &key_hash, sizeof(key_hash), 1,
                                  path, "item key")
                || !rc_read_exact(f, &flags, sizeof(flags), 1,
                                  path, "item flags")) {
            rc_asset_close(f);
            free(sources);
            return -1;
        }
        if (item_id < (uint32_t)max_item_defs) {
            item_defs[item_id] = (RcItemNormalization){
                .canonical_id = id_or_missing(canonical_id),
                .noted_id = id_or_missing(noted_id),
                .placeholder_id = id_or_missing(placeholder_id),
                .key_hash = key_hash,
                .flags = flags,
                .loaded = 1,
            };
            loaded_items++;
        }
    }

    for (uint32_t i = 0; i < raw_npc_count; i++) {
        uint32_t npc_id, canonical_id, key_hash;
        uint16_t flags;
        if (!rc_read_exact(f, &npc_id, sizeof(npc_id), 1, path, "npc id")
                || !rc_read_exact(f, &canonical_id, sizeof(canonical_id), 1,
                                  path, "npc canonical")
                || !rc_read_exact(f, &key_hash, sizeof(key_hash), 1,
                                  path, "npc key")
                || !rc_read_exact(f, &flags, sizeof(flags), 1,
                                  path, "npc flags")) {
            rc_asset_close(f);
            free(sources);
            return -1;
        }
        if (npc_id < (uint32_t)max_npc_defs) {
            npc_defs[npc_id] = (RcNpcNormalization){
                .canonical_id = id_or_missing(canonical_id),
                .key_hash = key_hash,
                .flags = flags,
                .loaded = 1,
            };
            loaded_npcs++;
        }
    }
    for (uint32_t i = 0; i < raw_source_count; i++) {
        RcSourceNormalization row;
        if (!rc_read_exact(f, &row.kind, sizeof(row.kind), 1,
                           path, "source kind")
                || !rc_read_exact(f, &row.key_hash, sizeof(row.key_hash), 1,
                                  path, "source key")
                || !rc_read_exact(f, &row.ref_id, sizeof(row.ref_id), 1,
                                  path, "source ref")) {
            rc_asset_close(f);
            free(sources);
            return -1;
        }
        sources[loaded_sources++] = row;
    }

    rc_asset_close(f);
    if (out_item_count) *out_item_count = loaded_items;
    if (out_npc_count) *out_npc_count = loaded_npcs;
    if (out_source_count) *out_source_count = loaded_sources;
    *source_defs = sources;
    return loaded_items + loaded_npcs + loaded_sources;
}

int rc_load_normalization(const char *path) {
    RcSourceNormalization *sources = NULL;
    int item_count = 0;
    int npc_count = 0;
    int source_count = 0;
    int loaded = rc_load_normalization_into(
        path, g_rc_item_normalization, RC_MAX_ITEM_DEFS, &item_count,
        g_rc_npc_normalization, RC_MAX_NPC_ID, &npc_count,
        &sources, &source_count);
    if (loaded < 0) return -1;

    free(g_rc_source_normalization);
    g_rc_source_normalization = sources;
    g_rc_item_normalization_count = item_count;
    g_rc_npc_normalization_count = npc_count;
    g_rc_source_normalization_count = source_count;
    rc_normalization_use_defs(g_rc_item_normalization,
                              g_rc_item_normalization_count,
                              g_rc_npc_normalization,
                              g_rc_npc_normalization_count,
                              g_rc_source_normalization,
                              g_rc_source_normalization_count);
    fprintf(stderr, "normalization: loaded %d item, %d npc, %d source rows from %s\n",
            g_rc_item_normalization_count, g_rc_npc_normalization_count,
            g_rc_source_normalization_count, path);
    return loaded;
}

int rc_normalize_item_id(int item_id) {
    const RcItemNormalization *defs = g_active_item_normalization;
    int count = defs == g_rc_item_normalization ? g_rc_item_normalization_count
                                                : g_active_item_normalization_count;
    if (item_id < 0 || item_id >= RC_MAX_ITEM_DEFS ||
            count <= 0 || !defs || !defs[item_id].loaded) {
        return item_id;
    }
    return defs[item_id].canonical_id;
}

int rc_item_noted_id(int item_id) {
    const RcItemNormalization *defs = g_active_item_normalization;
    int count = defs == g_rc_item_normalization ? g_rc_item_normalization_count
                                                : g_active_item_normalization_count;
    if (item_id < 0 || item_id >= RC_MAX_ITEM_DEFS ||
            count <= 0 || !defs || !defs[item_id].loaded) {
        return -1;
    }
    return defs[item_id].noted_id;
}

int rc_item_placeholder_id(int item_id) {
    const RcItemNormalization *defs = g_active_item_normalization;
    int count = defs == g_rc_item_normalization ? g_rc_item_normalization_count
                                                : g_active_item_normalization_count;
    if (item_id < 0 || item_id >= RC_MAX_ITEM_DEFS ||
            count <= 0 || !defs || !defs[item_id].loaded) {
        return -1;
    }
    return defs[item_id].placeholder_id;
}

int rc_normalize_npc_id(int npc_id) {
    const RcNpcNormalization *defs = g_active_npc_normalization;
    int count = defs == g_rc_npc_normalization ? g_rc_npc_normalization_count
                                               : g_active_npc_normalization_count;
    if (npc_id < 0 || npc_id >= RC_MAX_NPC_ID ||
            count <= 0 || !defs || !defs[npc_id].loaded) {
        return npc_id;
    }
    return defs[npc_id].canonical_id;
}

int rc_normalization_find_source(uint8_t kind, uint32_t key_hash) {
    const RcSourceNormalization *sources = g_active_source_normalization;
    int count = sources == g_rc_source_normalization
              ? g_rc_source_normalization_count
              : g_active_source_normalization_count;
    if (!sources || count <= 0) return -1;
    for (int i = 0; i < count; i++) {
        const RcSourceNormalization *row = &sources[i];
        if (row->kind == kind && row->key_hash == key_hash) return i;
    }
    return -1;
}
