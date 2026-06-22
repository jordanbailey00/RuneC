#include "combat_visuals.h"
#include "assets.h"
#include "spells.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

RcCombatVisualDef g_rc_combat_visual_defs[RC_MAX_COMBAT_VISUAL_DEFS];
int g_rc_combat_visual_count = 0;

typedef struct {
    int primitive_type;
    int source_attachment;
    int target_attachment;
    int launch_attachment;
    int impact_attachment;
    int authority;
} RcCombatVisualRichColumns;

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

static int parse_primitive_type(const char *s) {
    if (!s || !s[0] || strcmp(s, "-") == 0) return -1;
    if (strcmp(s, "none") == 0) return RC_COMBAT_VISUAL_PRIMITIVE_NONE;
    if (strcmp(s, "launch_effect") == 0)
        return RC_COMBAT_VISUAL_PRIMITIVE_LAUNCH_EFFECT;
    if (strcmp(s, "travel_projectile") == 0)
        return RC_COMBAT_VISUAL_PRIMITIVE_TRAVEL_PROJECTILE;
    if (strcmp(s, "target_impact") == 0)
        return RC_COMBAT_VISUAL_PRIMITIVE_TARGET_IMPACT;
    if (strcmp(s, "fixed_tile_impact") == 0)
        return RC_COMBAT_VISUAL_PRIMITIVE_FIXED_TILE_IMPACT;
    if (strcmp(s, "ground_effect") == 0)
        return RC_COMBAT_VISUAL_PRIMITIVE_GROUND_EFFECT;
    if (strcmp(s, "area_effect") == 0)
        return RC_COMBAT_VISUAL_PRIMITIVE_AREA_EFFECT;
    if (strcmp(s, "multi_projectile") == 0)
        return RC_COMBAT_VISUAL_PRIMITIVE_MULTI_PROJECTILE;
    return -1;
}

const char *rc_combat_visual_primitive_name(int primitive_type) {
    switch (primitive_type) {
        case RC_COMBAT_VISUAL_PRIMITIVE_NONE: return "none";
        case RC_COMBAT_VISUAL_PRIMITIVE_LAUNCH_EFFECT: return "launch_effect";
        case RC_COMBAT_VISUAL_PRIMITIVE_TRAVEL_PROJECTILE:
            return "travel_projectile";
        case RC_COMBAT_VISUAL_PRIMITIVE_TARGET_IMPACT: return "target_impact";
        case RC_COMBAT_VISUAL_PRIMITIVE_FIXED_TILE_IMPACT:
            return "fixed_tile_impact";
        case RC_COMBAT_VISUAL_PRIMITIVE_GROUND_EFFECT: return "ground_effect";
        case RC_COMBAT_VISUAL_PRIMITIVE_AREA_EFFECT: return "area_effect";
        case RC_COMBAT_VISUAL_PRIMITIVE_MULTI_PROJECTILE:
            return "multi_projectile";
        default: return "unknown";
    }
}

static int parse_attachment_rule(const char *s) {
    if (!s || !s[0] || strcmp(s, "-") == 0) return -1;
    if (strcmp(s, "none") == 0) return RC_COMBAT_VISUAL_ATTACH_NONE;
    if (strcmp(s, "source_actor") == 0)
        return RC_COMBAT_VISUAL_ATTACH_SOURCE_ACTOR;
    if (strcmp(s, "source_center") == 0)
        return RC_COMBAT_VISUAL_ATTACH_SOURCE_CENTER;
    if (strcmp(s, "source_tile") == 0)
        return RC_COMBAT_VISUAL_ATTACH_SOURCE_TILE;
    if (strcmp(s, "target_actor") == 0)
        return RC_COMBAT_VISUAL_ATTACH_TARGET_ACTOR;
    if (strcmp(s, "target_tile") == 0)
        return RC_COMBAT_VISUAL_ATTACH_TARGET_TILE;
    if (strcmp(s, "fixed_tile") == 0)
        return RC_COMBAT_VISUAL_ATTACH_FIXED_TILE;
    if (strcmp(s, "ground_tile") == 0)
        return RC_COMBAT_VISUAL_ATTACH_GROUND_TILE;
    return -1;
}

const char *rc_combat_visual_attachment_name(int attachment_rule) {
    switch (attachment_rule) {
        case RC_COMBAT_VISUAL_ATTACH_NONE: return "none";
        case RC_COMBAT_VISUAL_ATTACH_SOURCE_ACTOR: return "source_actor";
        case RC_COMBAT_VISUAL_ATTACH_SOURCE_CENTER: return "source_center";
        case RC_COMBAT_VISUAL_ATTACH_SOURCE_TILE: return "source_tile";
        case RC_COMBAT_VISUAL_ATTACH_TARGET_ACTOR: return "target_actor";
        case RC_COMBAT_VISUAL_ATTACH_TARGET_TILE: return "target_tile";
        case RC_COMBAT_VISUAL_ATTACH_FIXED_TILE: return "fixed_tile";
        case RC_COMBAT_VISUAL_ATTACH_GROUND_TILE: return "ground_tile";
        default: return "unknown";
    }
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

static int find_header_col(char **parts, int count, const char *name) {
    for (int i = 0; i < count; i++) {
        if (strcmp(parts[i], name) == 0)
            return i;
    }
    return -1;
}

static void capture_rich_columns(RcCombatVisualRichColumns *cols,
                                 char **parts,
                                 int count) {
    if (!cols) return;
    cols->primitive_type = find_header_col(parts, count, "primitive_type");
    cols->source_attachment =
        find_header_col(parts, count, "source_attachment");
    cols->target_attachment =
        find_header_col(parts, count, "target_attachment");
    cols->launch_attachment =
        find_header_col(parts, count, "launch_attachment");
    cols->impact_attachment =
        find_header_col(parts, count, "impact_attachment");
    cols->authority = find_header_col(parts, count, "authority");
}

static const char *optional_col(char **parts, int count, int col) {
    if (col < 0 || col >= count)
        return NULL;
    return parts[col];
}

static int visual_has_aux_projectile(const RcCombatVisualDef *def) {
    return def && (def->aux_travel_spotanim_id >= 0 ||
                   def->aux_projectile_model_id >= 0 ||
                   def->aux_projectile_anim_id >= 0);
}

static int visual_has_travel_asset(const RcCombatVisualDef *def) {
    return def && (def->travel_spotanim_id >= 0 ||
                   def->projectile_model_id >= 0 ||
                   def->projectile_anim_id >= 0);
}

static int visual_looks_like_fixed_tile_impact(
    const RcCombatVisualDef *def
) {
    return def && !visual_has_travel_asset(def) &&
           def->impact_spotanim_id >= 0 &&
           def->projectile_start_height >= 256 &&
           def->projectile_end_height >= 0;
}

static int infer_primitive_type(const RcCombatVisualDef *def) {
    if (!def) return RC_COMBAT_VISUAL_PRIMITIVE_NONE;
    if (def->projectile_count > 1 || visual_has_aux_projectile(def))
        return RC_COMBAT_VISUAL_PRIMITIVE_MULTI_PROJECTILE;
    if (visual_looks_like_fixed_tile_impact(def))
        return RC_COMBAT_VISUAL_PRIMITIVE_FIXED_TILE_IMPACT;
    if (visual_has_travel_asset(def))
        return RC_COMBAT_VISUAL_PRIMITIVE_TRAVEL_PROJECTILE;
    if (def->impact_spotanim_id >= 0)
        return RC_COMBAT_VISUAL_PRIMITIVE_TARGET_IMPACT;
    if (def->launch_spotanim_id >= 0 || def->double_launch_spotanim_id >= 0)
        return RC_COMBAT_VISUAL_PRIMITIVE_LAUNCH_EFFECT;
    return RC_COMBAT_VISUAL_PRIMITIVE_NONE;
}

static int default_source_attachment(int primitive_type) {
    if (primitive_type == RC_COMBAT_VISUAL_PRIMITIVE_NONE)
        return RC_COMBAT_VISUAL_ATTACH_NONE;
    if (primitive_type == RC_COMBAT_VISUAL_PRIMITIVE_FIXED_TILE_IMPACT)
        return RC_COMBAT_VISUAL_ATTACH_TARGET_TILE;
    return RC_COMBAT_VISUAL_ATTACH_SOURCE_ACTOR;
}

static int default_target_attachment(int primitive_type) {
    switch (primitive_type) {
        case RC_COMBAT_VISUAL_PRIMITIVE_NONE:
        case RC_COMBAT_VISUAL_PRIMITIVE_LAUNCH_EFFECT:
            return RC_COMBAT_VISUAL_ATTACH_NONE;
        case RC_COMBAT_VISUAL_PRIMITIVE_FIXED_TILE_IMPACT:
            return RC_COMBAT_VISUAL_ATTACH_TARGET_TILE;
        default:
            return RC_COMBAT_VISUAL_ATTACH_TARGET_ACTOR;
    }
}

static int default_launch_attachment(const RcCombatVisualDef *def) {
    if (!def || def->launch_spotanim_id < 0 ||
            def->primitive_type == RC_COMBAT_VISUAL_PRIMITIVE_FIXED_TILE_IMPACT) {
        return RC_COMBAT_VISUAL_ATTACH_NONE;
    }
    return RC_COMBAT_VISUAL_ATTACH_SOURCE_ACTOR;
}

static int default_impact_attachment(int primitive_type) {
    switch (primitive_type) {
        case RC_COMBAT_VISUAL_PRIMITIVE_FIXED_TILE_IMPACT:
            return RC_COMBAT_VISUAL_ATTACH_FIXED_TILE;
        case RC_COMBAT_VISUAL_PRIMITIVE_TRAVEL_PROJECTILE:
        case RC_COMBAT_VISUAL_PRIMITIVE_TARGET_IMPACT:
        case RC_COMBAT_VISUAL_PRIMITIVE_MULTI_PROJECTILE:
            return RC_COMBAT_VISUAL_ATTACH_TARGET_ACTOR;
        case RC_COMBAT_VISUAL_PRIMITIVE_GROUND_EFFECT:
            return RC_COMBAT_VISUAL_ATTACH_GROUND_TILE;
        case RC_COMBAT_VISUAL_PRIMITIVE_AREA_EFFECT:
            return RC_COMBAT_VISUAL_ATTACH_TARGET_TILE;
        default:
            return RC_COMBAT_VISUAL_ATTACH_NONE;
    }
}

static void copy_field(char dst[64], const char *src) {
    if (!dst) return;
    dst[0] = '\0';
    if (!src || !src[0] || strcmp(src, "-") == 0) return;
    strncpy(dst, src, 63);
    dst[63] = '\0';
}

static void infer_authority(char dst[64], const char *note) {
    if (!dst) return;
    dst[0] = '\0';
    if (!note || !note[0] || strcmp(note, "-") == 0) return;
    size_t len = 0;
    while (note[len] && note[len] != ':' && len < 63)
        len++;
    if (len == 0) return;
    memcpy(dst, note, len);
    dst[len] = '\0';
}

int rc_load_combat_visuals(const char *path) {
    g_rc_combat_visual_count = 0;
    if (!path || !path[0]) return -1;
    FILE *f = rc_asset_fopen(path, "r");
    if (!f) return -1;

    RcCombatVisualRichColumns rich_cols = {
        .primitive_type = -1,
        .source_attachment = -1,
        .target_attachment = -1,
        .launch_attachment = -1,
        .impact_attachment = -1,
        .authority = -1,
    };
    char line[2048];
    while (fgets(line, sizeof(line), f)) {
        char *s = trim(line);
        if (!s[0] || s[0] == '#') continue;
        char *parts[64] = {0};
        int n = split_pipe(s, parts, 64);
        if (n < 11) continue;
        if (strcmp(parts[0], "kind") == 0) {
            capture_rich_columns(&rich_cols, parts, n);
            continue;
        }
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
        def->stance_idx = n > 33 ? parse_int_field(parts[33]) : -1;
        def->attack_anim_id = parse_int_field(parts[3]);
        def->launch_spotanim_id = parse_int_field(parts[4]);
        def->double_launch_spotanim_id = -1;
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
        def->projectile_count = n > 19 ? parse_int_field(parts[19]) : 1;
        if (def->projectile_count < 1) def->projectile_count = 1;
        if (def->projectile_count > 4) def->projectile_count = 4;
        def->alt_projectile_start_height =
            n > 20 ? parse_int_field(parts[20]) : -1;
        def->alt_projectile_end_height =
            n > 21 ? parse_int_field(parts[21]) : -1;
        def->alt_projectile_delay = n > 22 ? parse_int_field(parts[22]) : -1;
        def->alt_projectile_angle = n > 23 ? parse_int_field(parts[23]) : -1;
        def->alt_projectile_length_adjustment =
            n > 24 ? parse_int_field(parts[24]) : -1;
        def->alt_projectile_progress =
            n > 25 ? parse_int_field(parts[25]) : -1;
        def->alt_projectile_step_multiplier =
            n > 26 ? parse_int_field(parts[26]) : -1;
        def->aux_travel_spotanim_id =
            n > 27 ? parse_int_field(parts[27]) : -1;
        def->aux_impact_spotanim_id =
            n > 28 ? parse_int_field(parts[28]) : -1;
        def->aux_projectile_model_id =
            n > 29 ? parse_int_field(parts[29]) : -1;
        def->aux_projectile_anim_id =
            n > 30 ? parse_int_field(parts[30]) : -1;
        def->impact_on_last_only = n > 31 ? parse_int_field(parts[31]) : 0;
        def->double_launch_spotanim_id =
            n > 32 ? parse_int_field(parts[32]) : -1;
        int primitive_type = parse_primitive_type(
            optional_col(parts, n, rich_cols.primitive_type));
        if (primitive_type < 0)
            primitive_type = infer_primitive_type(def);
        def->primitive_type = (uint8_t)primitive_type;

        int source_attachment = parse_attachment_rule(
            optional_col(parts, n, rich_cols.source_attachment));
        int target_attachment = parse_attachment_rule(
            optional_col(parts, n, rich_cols.target_attachment));
        int launch_attachment = parse_attachment_rule(
            optional_col(parts, n, rich_cols.launch_attachment));
        int impact_attachment = parse_attachment_rule(
            optional_col(parts, n, rich_cols.impact_attachment));
        if (source_attachment < 0)
            source_attachment = default_source_attachment(primitive_type);
        if (target_attachment < 0)
            target_attachment = default_target_attachment(primitive_type);
        if (launch_attachment < 0)
            launch_attachment = default_launch_attachment(def);
        if (impact_attachment < 0)
            impact_attachment = default_impact_attachment(primitive_type);
        def->source_attachment = (uint8_t)source_attachment;
        def->target_attachment = (uint8_t)target_attachment;
        def->launch_attachment = (uint8_t)launch_attachment;
        def->impact_attachment = (uint8_t)impact_attachment;

        copy_field(def->authority, optional_col(parts, n,
                                                rich_cols.authority));
        if (!def->authority[0])
            infer_authority(def->authority, n > 18 ? parts[18] : NULL);
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
    return rc_combat_visual_for_item_stance(item_id, style, -1);
}

const RcCombatVisualDef *rc_combat_visual_for_item_stance(
    int item_id, RcCombatStyle style, int stance_idx
) {
    const RcCombatVisualDef *fallback = NULL;
    const RcCombatVisualDef *style_fallback = NULL;
    const RcCombatVisualDef *stance_fallback = NULL;
    for (int i = 0; i < g_rc_combat_visual_count; i++) {
        const RcCombatVisualDef *def = &g_rc_combat_visual_defs[i];
        if (def->kind != RC_COMBAT_VISUAL_ITEM || def->key_id != item_id)
            continue;
        int exact_style = def->style == (int)style;
        int any_style = def->style == RC_COMBAT_VISUAL_ANY;
        int exact_stance = stance_idx >= 0 && def->stance_idx == stance_idx;
        int any_stance = def->stance_idx < 0;
        if (exact_style && exact_stance) return def;
        if (!style_fallback && exact_style && any_stance)
            style_fallback = def;
        if (!stance_fallback && any_style && exact_stance)
            stance_fallback = def;
        if (!fallback && visual_style_matches(def, style) && any_stance)
            fallback = def;
    }
    return style_fallback ? style_fallback
         : (stance_fallback ? stance_fallback : fallback);
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
