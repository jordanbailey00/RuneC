#include "combat_visuals.h"
#include "assets.h"
#include "spells.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

RcCombatVisualDef g_rc_combat_visual_defs[RC_MAX_COMBAT_VISUAL_DEFS];
int g_rc_combat_visual_count = 0;

static char *trim(char *s) {
    if (!s) return s;
    while (*s && isspace((unsigned char)*s)) s++;
    char *end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1])) {
        *--end = '\0';
    }
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
    if (strcmp(s, "item") == 0) return RC_COMBAT_VISUAL_ITEM;
    if (strcmp(s, "spell") == 0) return RC_COMBAT_VISUAL_SPELL;
    if (strcmp(s, "npc") == 0) return RC_COMBAT_VISUAL_NPC;
    if (strcmp(s, "special") == 0) return RC_COMBAT_VISUAL_SPECIAL;
    return 0;
}

static int parse_style(const char *s) {
    if (!s || !s[0] || strcmp(s, "any") == 0 || strcmp(s, "-") == 0)
        return RC_COMBAT_VISUAL_ANY;
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

int rc_load_combat_visuals(const char *path) {
    g_rc_combat_visual_count = 0;
    if (!path || !path[0]) return -1;
    FILE *f = rc_asset_fopen(path, "r");
    if (!f) return -1;

    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        char *s = trim(line);
        if (!s[0] || s[0] == '#') continue;
        char *parts[20] = {0};
        int n = split_pipe(s, parts, 20);
        if (n < 11) continue;
        if (strcmp(parts[0], "kind") == 0) continue;
        if (g_rc_combat_visual_count >= RC_MAX_COMBAT_VISUAL_DEFS) break;

        RcCombatVisualDef *def =
            &g_rc_combat_visual_defs[g_rc_combat_visual_count];
        memset(def, 0, sizeof(*def));
        def->kind = (uint8_t)parse_kind(parts[0]);
        if (!def->kind) continue;
        def->key_id = def->kind == RC_COMBAT_VISUAL_ITEM ||
                      def->kind == RC_COMBAT_VISUAL_NPC ||
                      def->kind == RC_COMBAT_VISUAL_SPECIAL
                    ? parse_int_field(parts[1]) : -1;
        strncpy(def->key_name, parts[1], sizeof(def->key_name) - 1);
        if (def->kind == RC_COMBAT_VISUAL_SPELL) {
            def->key_id = parse_strict_int_field(parts[1]);
            if (def->key_id < 0)
                def->key_id = rc_spell_find(def->key_name);
        }
        def->style = parse_style(parts[2]);
        def->attack_anim_id = parse_int_field(parts[3]);
        def->launch_spotanim_id = parse_int_field(parts[4]);
        def->travel_spotanim_id = parse_int_field(parts[5]);
        def->impact_spotanim_id = parse_int_field(parts[6]);
        def->projectile_model_id = parse_int_field(parts[7]);
        def->projectile_anim_id = parse_int_field(parts[8]);
        def->hit_delay = parse_int_field(parts[9]);
        def->client_delay = parse_int_field(parts[10]);
        def->projectile_start_height = n > 11 ? parse_int_field(parts[11]) : -1;
        def->projectile_end_height = n > 12 ? parse_int_field(parts[12]) : -1;
        def->projectile_delay = n > 13 ? parse_int_field(parts[13]) : -1;
        def->projectile_angle = n > 14 ? parse_int_field(parts[14]) : -1;
        def->projectile_length_adjustment = n > 15
                                          ? parse_int_field(parts[15]) : -1;
        def->projectile_progress = n > 16 ? parse_int_field(parts[16]) : -1;
        def->projectile_step_multiplier = n > 17
                                        ? parse_int_field(parts[17]) : -1;
        g_rc_combat_visual_count++;
    }

    rc_asset_close(f);
    return g_rc_combat_visual_count;
}

static int visual_style_matches(const RcCombatVisualDef *def,
                                RcCombatStyle style) {
    if (!def) return 0;
    return def->style == RC_COMBAT_VISUAL_ANY || def->style == (int)style;
}

const RcCombatVisualDef *rc_combat_visual_for_item(int item_id,
                                                   RcCombatStyle style) {
    const RcCombatVisualDef *fallback = NULL;
    for (int i = 0; i < g_rc_combat_visual_count; i++) {
        const RcCombatVisualDef *def = &g_rc_combat_visual_defs[i];
        if (def->kind != RC_COMBAT_VISUAL_ITEM || def->key_id != item_id)
            continue;
        if (def->style == (int)style) return def;
        if (!fallback && visual_style_matches(def, style)) fallback = def;
    }
    return fallback;
}

const RcCombatVisualDef *rc_combat_visual_for_spell(const char *spell_name,
                                                    RcCombatStyle style) {
    return rc_combat_visual_for_spell_id(-1, spell_name, style);
}

const RcCombatVisualDef *rc_combat_visual_for_spell_id(
    int spell_idx, const char *fallback_name, RcCombatStyle style) {
    if (spell_idx < 0 && (!fallback_name || !fallback_name[0])) return NULL;
    const RcCombatVisualDef *fallback = NULL;
    const RcCombatVisualDef *name_fallback = NULL;
    for (int i = 0; i < g_rc_combat_visual_count; i++) {
        const RcCombatVisualDef *def = &g_rc_combat_visual_defs[i];
        if (def->kind != RC_COMBAT_VISUAL_SPELL)
            continue;
        if (spell_idx >= 0 && def->key_id == spell_idx) {
            if (def->style == (int)style) return def;
            if (!fallback && visual_style_matches(def, style))
                fallback = def;
            continue;
        }
        if (fallback_name && fallback_name[0] &&
                strcmp(def->key_name, fallback_name) == 0) {
            if (def->style == (int)style)
                name_fallback = def;
            if (!name_fallback && visual_style_matches(def, style))
                name_fallback = def;
        }
    }
    return fallback ? fallback : name_fallback;
}

const RcCombatVisualDef *rc_combat_visual_for_npc(int npc_id,
                                                  RcCombatStyle style) {
    const RcCombatVisualDef *fallback = NULL;
    for (int i = 0; i < g_rc_combat_visual_count; i++) {
        const RcCombatVisualDef *def = &g_rc_combat_visual_defs[i];
        if (def->kind != RC_COMBAT_VISUAL_NPC || def->key_id != npc_id)
            continue;
        if (def->style == (int)style) return def;
        if (!fallback && visual_style_matches(def, style)) fallback = def;
    }
    return fallback;
}

const RcCombatVisualDef *rc_combat_visual_for_special_item(int item_id,
                                                           RcCombatStyle style) {
    const RcCombatVisualDef *fallback = NULL;
    for (int i = 0; i < g_rc_combat_visual_count; i++) {
        const RcCombatVisualDef *def = &g_rc_combat_visual_defs[i];
        if (def->kind != RC_COMBAT_VISUAL_SPECIAL || def->key_id != item_id)
            continue;
        if (def->style == (int)style) return def;
        if (!fallback && visual_style_matches(def, style)) fallback = def;
    }
    return fallback;
}
