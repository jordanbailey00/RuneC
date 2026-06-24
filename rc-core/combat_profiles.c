#include "combat_profiles.h"
#include "assets.h"
#include "spells.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

RcCombatProfileDef g_rc_combat_profile_defs[RC_MAX_COMBAT_PROFILE_DEFS];
int g_rc_combat_profile_count = 0;

static char *trim(char *s) {
    if (!s) return s;
    while (*s && isspace((unsigned char)*s)) s++;
    char *end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1]))
        *--end = '\0';
    return s;
}

static int parse_int_field(const char *s) {
    if (!s || !s[0] || strcmp(s, "-") == 0) return -1;
    return atoi(s);
}

static int parse_strict_int_field(const char *s) {
    if (!s || !s[0] || strcmp(s, "-") == 0) return -1;
    char *end = NULL;
    long value = strtol(s, &end, 10);
    return end && *end == '\0' ? (int)value : -1;
}

static int parse_kind(const char *s) {
    if (!s) return 0;
    if (strcmp(s, "item") == 0) return RC_COMBAT_PROFILE_ITEM;
    if (strcmp(s, "spell") == 0) return RC_COMBAT_PROFILE_SPELL;
    if (strcmp(s, "npc") == 0) return RC_COMBAT_PROFILE_NPC;
    if (strcmp(s, "special") == 0) return RC_COMBAT_PROFILE_SPECIAL;
    return 0;
}

static int parse_style(const char *s) {
    if (!s || !s[0] || strcmp(s, "any") == 0 || strcmp(s, "-") == 0)
        return RC_COMBAT_PROFILE_ANY;
    if (strcmp(s, "melee") == 0) return COMBAT_MELEE_CRUSH;
    if (strcmp(s, "stab") == 0) return COMBAT_MELEE_STAB;
    if (strcmp(s, "slash") == 0) return COMBAT_MELEE_SLASH;
    if (strcmp(s, "crush") == 0) return COMBAT_MELEE_CRUSH;
    if (strcmp(s, "ranged") == 0) return COMBAT_RANGED;
    if (strcmp(s, "magic") == 0) return COMBAT_MAGIC;
    return atoi(s);
}

static int split_pipe(char *line, char **parts, int max_parts) {
    int count = 0;
    char *p = line;
    while (count < max_parts) {
        parts[count++] = trim(p);
        char *sep = strchr(p, '|');
        if (!sep) break;
        *sep = '\0';
        p = sep + 1;
    }
    for (int i = 0; i < count; i++)
        parts[i] = trim(parts[i]);
    return count;
}

static int find_header_col(char **parts, int count, const char *name) {
    for (int i = 0; i < count; i++) {
        if (strcmp(parts[i], name) == 0)
            return i;
    }
    return -1;
}

int rc_load_combat_profiles(const char *path) {
    g_rc_combat_profile_count = 0;
    if (!path || !path[0]) return -1;
    FILE *f = rc_asset_fopen(path, "r");
    if (!f) return -1;

    int stance_col = 33;
    char line[2048];
    while (fgets(line, sizeof(line), f)) {
        char *s = trim(line);
        if (!s[0] || s[0] == '#') continue;
        char *parts[64] = {0};
        int n = split_pipe(s, parts, 64);
        if (n < 11) continue;
        if (strcmp(parts[0], "kind") == 0) {
            int found = find_header_col(parts, n, "stance_idx");
            if (found >= 0)
                stance_col = found;
            continue;
        }
        if (g_rc_combat_profile_count >= RC_MAX_COMBAT_PROFILE_DEFS)
            break;

        RcCombatProfileDef *def =
            &g_rc_combat_profile_defs[g_rc_combat_profile_count];
        memset(def, 0, sizeof(*def));
        def->kind = (uint8_t)parse_kind(parts[0]);
        if (!def->kind) continue;
        def->key_id = def->kind == RC_COMBAT_PROFILE_ITEM ||
                      def->kind == RC_COMBAT_PROFILE_NPC ||
                      def->kind == RC_COMBAT_PROFILE_SPECIAL
                    ? parse_int_field(parts[1]) : -1;
        strncpy(def->key_name, parts[1], sizeof(def->key_name) - 1);
        if (def->kind == RC_COMBAT_PROFILE_SPELL) {
            def->key_id = parse_strict_int_field(parts[1]);
            if (def->key_id < 0)
                def->key_id = rc_spell_find(def->key_name);
        }
        def->style = parse_style(parts[2]);
        def->stance_idx = stance_col >= 0 && stance_col < n
                        ? parse_int_field(parts[stance_col]) : -1;
        def->hit_delay = parse_int_field(parts[9]);
        g_rc_combat_profile_count++;
    }

    rc_asset_close(f);
    return g_rc_combat_profile_count;
}

static int profile_style_matches(const RcCombatProfileDef *def,
                                 RcCombatStyle style) {
    if (!def) return 0;
    return def->style == RC_COMBAT_PROFILE_ANY || def->style == (int)style;
}

const RcCombatProfileDef *rc_combat_profile_for_item(int item_id,
                                                     RcCombatStyle style) {
    return rc_combat_profile_for_item_stance(item_id, style, -1);
}

const RcCombatProfileDef *rc_combat_profile_for_item_stance(
    int item_id, RcCombatStyle style, int stance_idx
) {
    const RcCombatProfileDef *fallback = NULL;
    const RcCombatProfileDef *style_fallback = NULL;
    const RcCombatProfileDef *stance_fallback = NULL;
    for (int i = 0; i < g_rc_combat_profile_count; i++) {
        const RcCombatProfileDef *def = &g_rc_combat_profile_defs[i];
        if (def->kind != RC_COMBAT_PROFILE_ITEM || def->key_id != item_id)
            continue;
        int exact_style = def->style == (int)style;
        int any_style = def->style == RC_COMBAT_PROFILE_ANY;
        int exact_stance = stance_idx >= 0 && def->stance_idx == stance_idx;
        int any_stance = def->stance_idx < 0;
        if (exact_style && exact_stance) return def;
        if (!style_fallback && exact_style && any_stance)
            style_fallback = def;
        if (!stance_fallback && any_style && exact_stance)
            stance_fallback = def;
        if (!fallback && profile_style_matches(def, style) && any_stance)
            fallback = def;
    }
    return style_fallback ? style_fallback
         : (stance_fallback ? stance_fallback : fallback);
}

const RcCombatProfileDef *rc_combat_profile_for_spell(
    const char *spell_name, RcCombatStyle style
) {
    return rc_combat_profile_for_spell_id(-1, spell_name, style);
}

const RcCombatProfileDef *rc_combat_profile_for_spell_id(
    int spell_idx, const char *fallback_name, RcCombatStyle style
) {
    if (spell_idx < 0 && (!fallback_name || !fallback_name[0])) return NULL;
    const RcCombatProfileDef *fallback = NULL;
    const RcCombatProfileDef *name_fallback = NULL;
    for (int i = 0; i < g_rc_combat_profile_count; i++) {
        const RcCombatProfileDef *def = &g_rc_combat_profile_defs[i];
        if (def->kind != RC_COMBAT_PROFILE_SPELL)
            continue;
        if (spell_idx >= 0 && def->key_id == spell_idx) {
            if (def->style == (int)style) return def;
            if (!fallback && profile_style_matches(def, style))
                fallback = def;
            continue;
        }
        if (fallback_name && fallback_name[0] &&
                strcmp(def->key_name, fallback_name) == 0) {
            if (def->style == (int)style)
                name_fallback = def;
            if (!name_fallback && profile_style_matches(def, style))
                name_fallback = def;
        }
    }
    return fallback ? fallback : name_fallback;
}

const RcCombatProfileDef *rc_combat_profile_for_npc(int npc_id,
                                                    RcCombatStyle style) {
    const RcCombatProfileDef *fallback = NULL;
    for (int i = 0; i < g_rc_combat_profile_count; i++) {
        const RcCombatProfileDef *def = &g_rc_combat_profile_defs[i];
        if (def->kind != RC_COMBAT_PROFILE_NPC || def->key_id != npc_id)
            continue;
        if (def->style == (int)style) return def;
        if (!fallback && profile_style_matches(def, style)) fallback = def;
    }
    return fallback;
}

const RcCombatProfileDef *rc_combat_profile_for_special_item(
    int item_id, RcCombatStyle style
) {
    const RcCombatProfileDef *fallback = NULL;
    for (int i = 0; i < g_rc_combat_profile_count; i++) {
        const RcCombatProfileDef *def = &g_rc_combat_profile_defs[i];
        if (def->kind != RC_COMBAT_PROFILE_SPECIAL || def->key_id != item_id)
            continue;
        if (def->style == (int)style) return def;
        if (!fallback && profile_style_matches(def, style)) fallback = def;
    }
    return fallback;
}
