#include "types.h"
#include "normalization.h"
#include "io.h"

#include <ctype.h>
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

static int id_or_missing(uint32_t value) {
    return value == UINT32_MAX ? -1 : (int)value;
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

int rc_load_normalization(const char *path) {
    if (!path) return -1;
    FILE *f = rc_asset_fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "normalization: can't open %s\n", path);
        return -1;
    }

    uint32_t magic, version, item_count, npc_count, source_count;
    if (!rc_read_exact(f, &magic, sizeof(magic), 1, path, "magic")
            || !rc_read_exact(f, &version, sizeof(version), 1,
                              path, "version")
            || !rc_read_exact(f, &item_count, sizeof(item_count), 1,
                              path, "item count")
            || !rc_read_exact(f, &npc_count, sizeof(npc_count), 1,
                              path, "npc count")
            || !rc_read_exact(f, &source_count, sizeof(source_count), 1,
                              path, "source count")) {
        rc_asset_close(f);
        return -1;
    }
    if (magic != NORM_MAGIC || version == 0 || version > NORM_VERSION) {
        rc_asset_close(f);
        fprintf(stderr, "normalization: bad header\n");
        return -1;
    }

    memset(g_rc_item_normalization, 0, sizeof(g_rc_item_normalization));
    memset(g_rc_npc_normalization, 0, sizeof(g_rc_npc_normalization));
    free(g_rc_source_normalization);
    g_rc_source_normalization = NULL;
    g_rc_item_normalization_count = 0;
    g_rc_npc_normalization_count = 0;
    g_rc_source_normalization_count = 0;

    for (uint32_t i = 0; i < item_count; i++) {
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
            return -1;
        }
        if (item_id < RC_MAX_ITEM_DEFS) {
            g_rc_item_normalization[item_id] = (RcItemNormalization){
                .canonical_id = id_or_missing(canonical_id),
                .noted_id = id_or_missing(noted_id),
                .placeholder_id = id_or_missing(placeholder_id),
                .key_hash = key_hash,
                .flags = flags,
                .loaded = 1,
            };
            g_rc_item_normalization_count++;
        }
    }

    for (uint32_t i = 0; i < npc_count; i++) {
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
            return -1;
        }
        if (npc_id < RC_MAX_NPC_ID) {
            g_rc_npc_normalization[npc_id] = (RcNpcNormalization){
                .canonical_id = id_or_missing(canonical_id),
                .key_hash = key_hash,
                .flags = flags,
                .loaded = 1,
            };
            g_rc_npc_normalization_count++;
        }
    }

    if (source_count) {
        g_rc_source_normalization = calloc(source_count,
                                           sizeof(*g_rc_source_normalization));
        if (!g_rc_source_normalization) {
            rc_asset_close(f);
            return -1;
        }
    }
    for (uint32_t i = 0; i < source_count; i++) {
        RcSourceNormalization row;
        if (!rc_read_exact(f, &row.kind, sizeof(row.kind), 1,
                           path, "source kind")
                || !rc_read_exact(f, &row.key_hash, sizeof(row.key_hash), 1,
                                  path, "source key")
                || !rc_read_exact(f, &row.ref_id, sizeof(row.ref_id), 1,
                                  path, "source ref")) {
            rc_asset_close(f);
            return -1;
        }
        g_rc_source_normalization[g_rc_source_normalization_count++] = row;
    }

    rc_asset_close(f);
    fprintf(stderr, "normalization: loaded %d item, %d npc, %d source rows from %s\n",
            g_rc_item_normalization_count, g_rc_npc_normalization_count,
            g_rc_source_normalization_count, path);
    return g_rc_item_normalization_count + g_rc_npc_normalization_count
         + g_rc_source_normalization_count;
}

int rc_normalize_item_id(int item_id) {
    if (item_id < 0 || item_id >= RC_MAX_ITEM_DEFS ||
            !g_rc_item_normalization[item_id].loaded) {
        return item_id;
    }
    return g_rc_item_normalization[item_id].canonical_id;
}

int rc_item_noted_id(int item_id) {
    if (item_id < 0 || item_id >= RC_MAX_ITEM_DEFS ||
            !g_rc_item_normalization[item_id].loaded) {
        return -1;
    }
    return g_rc_item_normalization[item_id].noted_id;
}

int rc_item_placeholder_id(int item_id) {
    if (item_id < 0 || item_id >= RC_MAX_ITEM_DEFS ||
            !g_rc_item_normalization[item_id].loaded) {
        return -1;
    }
    return g_rc_item_normalization[item_id].placeholder_id;
}

int rc_normalize_npc_id(int npc_id) {
    if (npc_id < 0 || npc_id >= RC_MAX_NPC_ID ||
            !g_rc_npc_normalization[npc_id].loaded) {
        return npc_id;
    }
    return g_rc_npc_normalization[npc_id].canonical_id;
}

int rc_normalization_find_source(uint8_t kind, uint32_t key_hash) {
    for (int i = 0; i < g_rc_source_normalization_count; i++) {
        const RcSourceNormalization *row = &g_rc_source_normalization[i];
        if (row->kind == kind && row->key_hash == key_hash) return i;
    }
    return -1;
}
