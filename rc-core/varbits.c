#include "varbits.h"
#include "io.h"

#include <stdio.h>
#include <string.h>

#define VBIT_MAGIC 0x54494256u
#define VBIT_VERSION 2u
#define VARP_MAGIC 0x50524156u
#define VARP_VERSION 1u

RcVarbitDef g_rc_varbits[RC_MAX_VARBITS];
RcVarpDef g_rc_varps[RC_MAX_VARPS];
int g_rc_varbit_count = 0;
int g_rc_varp_count = 0;

static int read_header(FILE *f, const char *path, uint32_t magic,
                       uint32_t version, uint32_t *count) {
    uint32_t got_magic, got_version;
    if (!rc_read_exact(f, &got_magic, sizeof(got_magic), 1, path, "magic")
            || !rc_read_exact(f, &got_version, sizeof(got_version), 1, path,
                              "version")
            || !rc_read_exact(f, count, sizeof(*count), 1, path, "count")) {
        return 0;
    }
    return got_magic == magic && got_version == version;
}

static int read_name(FILE *f, char *out, int cap, uint8_t len,
                     const char *path) {
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

int rc_load_varbits(const char *path) {
    if (!path) return -1;
    FILE *f = fopen(path, "rb");
    if (!f) return -1;

    uint32_t count;
    if (!read_header(f, path, VBIT_MAGIC, VBIT_VERSION, &count)) {
        fclose(f);
        return -1;
    }
    memset(g_rc_varbits, 0, sizeof(g_rc_varbits));
    g_rc_varbit_count = 0;
    int loaded = 0;
    for (uint32_t i = 0; i < count; i++) {
        uint16_t idx, base;
        uint8_t len, lsb, msb;
        char name[64];
        if (!rc_read_exact(f, &idx, sizeof(idx), 1, path, "varbit id")
                || !rc_read_exact(f, &len, sizeof(len), 1, path, "name len")
                || !read_name(f, name, sizeof(name), len, path)
                || !rc_read_exact(f, &base, sizeof(base), 1, path, "base varp")
                || !rc_read_exact(f, &lsb, sizeof(lsb), 1, path, "lsb")
                || !rc_read_exact(f, &msb, sizeof(msb), 1, path, "msb")) {
            fclose(f);
            return -1;
        }
        if (idx < RC_MAX_VARBITS) {
            RcVarbitDef *row = &g_rc_varbits[idx];
            strcpy(row->name, name);
            row->base_varp = base;
            row->lsb = lsb;
            row->msb = msb;
            row->loaded = 1;
            loaded++;
        }
    }
    fclose(f);
    g_rc_varbit_count = loaded;
    return loaded;
}

int rc_load_varps(const char *path) {
    if (!path) return -1;
    FILE *f = fopen(path, "rb");
    if (!f) return -1;

    uint32_t count;
    if (!read_header(f, path, VARP_MAGIC, VARP_VERSION, &count)) {
        fclose(f);
        return -1;
    }
    memset(g_rc_varps, 0, sizeof(g_rc_varps));
    g_rc_varp_count = 0;
    int loaded = 0;
    for (uint32_t i = 0; i < count; i++) {
        uint16_t idx, type;
        uint8_t len;
        char name[32];
        if (!rc_read_exact(f, &idx, sizeof(idx), 1, path, "varp id")
                || !rc_read_exact(f, &type, sizeof(type), 1, path, "varp type")
                || !rc_read_exact(f, &len, sizeof(len), 1, path, "name len")
                || !read_name(f, name, sizeof(name), len, path)) {
            fclose(f);
            return -1;
        }
        if (idx < RC_MAX_VARPS) {
            RcVarpDef *row = &g_rc_varps[idx];
            strcpy(row->name, name);
            row->type = type;
            row->loaded = 1;
            loaded++;
        }
    }
    fclose(f);
    g_rc_varp_count = loaded;
    return loaded;
}

const RcVarbitDef *rc_varbit_def_get(int varbit_id) {
    if (varbit_id < 0 || varbit_id >= RC_MAX_VARBITS
            || !g_rc_varbits[varbit_id].loaded) {
        return NULL;
    }
    return &g_rc_varbits[varbit_id];
}

const RcVarpDef *rc_varp_def_get(int varp_id) {
    if (varp_id < 0 || varp_id >= RC_MAX_VARPS
            || !g_rc_varps[varp_id].loaded) {
        return NULL;
    }
    return &g_rc_varps[varp_id];
}

int rc_varbit_find(const char *name) {
    if (!name) return -1;
    for (int i = 0; i < RC_MAX_VARBITS; i++) {
        if (g_rc_varbits[i].loaded && strcmp(g_rc_varbits[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

int rc_varp_find(const char *name) {
    if (!name) return -1;
    for (int i = 0; i < RC_MAX_VARPS; i++) {
        if (g_rc_varps[i].loaded && strcmp(g_rc_varps[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

uint32_t rc_varbit_get(const RcWorld *world, int varbit_id) {
    const RcVarbitDef *def = rc_varbit_def_get(varbit_id);
    if (!world || !def || def->base_varp >= RC_MAX_VARPS
            || def->msb < def->lsb || def->msb >= 32) {
        return 0;
    }
    uint32_t width = (uint32_t)(def->msb - def->lsb + 1);
    uint32_t mask = width >= 32 ? 0xFFFFFFFFu : ((1u << width) - 1u);
    return (((uint32_t)world->varps[def->base_varp]) >> def->lsb) & mask;
}

int rc_varbit_set(RcWorld *world, int varbit_id, uint32_t value) {
    const RcVarbitDef *def = rc_varbit_def_get(varbit_id);
    if (!world || !def || def->base_varp >= RC_MAX_VARPS
            || def->msb < def->lsb || def->msb >= 32) {
        return -1;
    }
    uint32_t width = (uint32_t)(def->msb - def->lsb + 1);
    uint32_t mask = width >= 32 ? 0xFFFFFFFFu : ((1u << width) - 1u);
    uint32_t shifted = mask << def->lsb;
    uint32_t cur = (uint32_t)world->varps[def->base_varp];
    cur = (cur & ~shifted) | ((value & mask) << def->lsb);
    world->varps[def->base_varp] = (int32_t)cur;
    return 0;
}
