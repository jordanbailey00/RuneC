#include "spells.h"
#include "io.h"

#include <stdio.h>
#include <string.h>

#define SPEL_MAGIC 0x4C455053u
#define TELE_MAGIC 0x454C4554u
#define SPEL_V1 1u
#define SPEL_V2 2u

RcSpellDef g_rc_spell_defs[RC_MAX_SPELL_DEFS];
int g_rc_spell_count = 0;

static const RcSpellDef *g_active_spell_defs = g_rc_spell_defs;
static int g_active_spell_count = 0;

void rc_spell_use_defs(const RcSpellDef *defs, int count) {
    g_active_spell_defs = defs ? defs : g_rc_spell_defs;
    g_active_spell_count = defs ? count : 0;
}

void rc_spell_reset_defs_if_active(const RcSpellDef *defs) {
    if (defs && g_active_spell_defs == defs) {
        rc_spell_use_defs(g_rc_spell_defs, g_rc_spell_count);
    }
}

static int read_name(FILE *f, RcSpellDef *row, const char *path) {
    uint8_t len;
    if (!rc_read_exact(f, &len, sizeof(len), 1, path, "spell name len")) {
        return 0;
    }
    uint8_t keep = len < sizeof(row->name) ? len : (uint8_t)sizeof(row->name) - 1;
    if (keep && !rc_read_exact(f, row->name, 1, keep, path, "spell name")) {
        return 0;
    }
    row->name[keep] = '\0';
    if (len > keep && !rc_seek(f, len - keep, SEEK_CUR, path, "spell name")) {
        return 0;
    }
    return 1;
}

int rc_load_spells_into(const char *path, RcSpellDef *defs, int max_defs,
                        int *out_count) {
    if (out_count) *out_count = 0;
    if (!path || !defs || max_defs <= 0) return -1;
    FILE *f = rc_asset_fopen(path, "rb");
    if (!f) return -1;

    uint32_t magic, version, count;
    if (!rc_read_exact(f, &magic, sizeof(magic), 1, path, "magic")
            || !rc_read_exact(f, &version, sizeof(version), 1, path, "version")
            || !rc_read_exact(f, &count, sizeof(count), 1, path, "count")
            || (magic != SPEL_MAGIC && magic != TELE_MAGIC)
            || (version != SPEL_V1 && version != SPEL_V2)) {
        rc_asset_close(f);
        return -1;
    }

    memset(defs, 0, (size_t)max_defs * sizeof(*defs));
    int loaded = 0;
    for (uint32_t i = 0; i < count; i++) {
        RcSpellDef row;
        memset(&row, 0, sizeof(row));
        if (!read_name(f, &row, path)
                || !rc_read_exact(f, &row.book, sizeof(row.book), 1, path, "book")
                || !rc_read_exact(f, &row.type, sizeof(row.type), 1, path, "type")
                || !rc_read_exact(f, &row.level, sizeof(row.level), 1, path, "level")
                || !rc_read_exact(f, &row.slayer_level, sizeof(row.slayer_level),
                                  1, path, "slayer level")
                || !rc_read_exact(f, &row.xp_q1, sizeof(row.xp_q1), 1, path, "xp")
                || !rc_read_exact(f, &row.flags, sizeof(row.flags), 1, path, "flags")) {
            rc_asset_close(f);
            return -1;
        }
        if (version >= SPEL_V2) {
            if (!rc_read_exact(f, &row.max_hit, sizeof(row.max_hit), 1, path, "max hit")
                    || !rc_read_exact(f, &row.effect_flags, sizeof(row.effect_flags),
                                      1, path, "effect flags")) {
                rc_asset_close(f);
                return -1;
            }
        }
        uint8_t rune_count;
        if (!rc_read_exact(f, &rune_count, sizeof(rune_count), 1, path, "rune count")) {
            rc_asset_close(f);
            return -1;
        }
        row.rune_count = rune_count < RC_SPELL_MAX_RUNES
                       ? rune_count : RC_SPELL_MAX_RUNES;
        for (uint8_t r = 0; r < rune_count; r++) {
            uint32_t item_id;
            uint8_t qty;
            if (!rc_read_exact(f, &item_id, sizeof(item_id), 1, path, "rune item")
                    || !rc_read_exact(f, &qty, sizeof(qty), 1, path, "rune qty")) {
                rc_asset_close(f);
                return -1;
            }
            if (r < RC_SPELL_MAX_RUNES) {
                row.runes[r] = (RcSpellRune){item_id, qty};
            }
        }
        if (loaded < max_defs) {
            row.loaded = 1;
            defs[loaded++] = row;
        }
    }
    rc_asset_close(f);
    if (out_count) *out_count = loaded;
    return loaded;
}

int rc_load_spells(const char *path) {
    int loaded = 0;
    int result = rc_load_spells_into(path, g_rc_spell_defs, RC_MAX_SPELL_DEFS,
                                     &loaded);
    if (result >= 0) {
        g_rc_spell_count = loaded;
        rc_spell_use_defs(g_rc_spell_defs, g_rc_spell_count);
    }
    return result;
}

const RcSpellDef *rc_spell_def_get(int spell_idx) {
    const RcSpellDef *defs = g_active_spell_defs
                           ? g_active_spell_defs : g_rc_spell_defs;
    int count = defs == g_rc_spell_defs ? g_rc_spell_count
                                        : g_active_spell_count;
    if (defs != g_rc_spell_defs && spell_idx >= 0
            && spell_idx < count && spell_idx < g_rc_spell_count
            && g_rc_spell_defs[spell_idx].loaded
            && memcmp(&defs[spell_idx], &g_rc_spell_defs[spell_idx],
                      sizeof(defs[spell_idx])) != 0) {
        return &g_rc_spell_defs[spell_idx];
    }
    if (spell_idx >= 0 && spell_idx < count && defs[spell_idx].loaded) {
        return &defs[spell_idx];
    }
    if (defs != g_rc_spell_defs && spell_idx >= 0
            && spell_idx < g_rc_spell_count
            && g_rc_spell_defs[spell_idx].loaded) {
        return &g_rc_spell_defs[spell_idx];
    }
    return NULL;
}

int rc_spell_find(const char *name) {
    if (!name) return -1;
    const RcSpellDef *defs = g_active_spell_defs
                           ? g_active_spell_defs : g_rc_spell_defs;
    int count = defs == g_rc_spell_defs ? g_rc_spell_count
                                        : g_active_spell_count;
    for (int i = 0; i < count; i++) {
        if (defs != g_rc_spell_defs && i < g_rc_spell_count
                && g_rc_spell_defs[i].loaded
                && memcmp(&defs[i], &g_rc_spell_defs[i], sizeof(defs[i])) != 0
                && strcmp(g_rc_spell_defs[i].name, name) == 0) {
            return i;
        }
        if (strcmp(defs[i].name, name) == 0) return i;
    }
    if (defs != g_rc_spell_defs) {
        for (int i = 0; i < g_rc_spell_count; i++) {
            if (strcmp(g_rc_spell_defs[i].name, name) == 0) return i;
        }
    }
    return -1;
}
