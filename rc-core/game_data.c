#include "game_data.h"

#include "assets.h"
#include "activity_mechanics.h"
#include "activity_schemas.h"
#include "activity_spawns.h"
#include "activity_states.h"
#include "area_flags.h"
#include "collision.h"
#include "combat_profiles.h"
#include "dialogue.h"
#include "drops.h"
#include "encounter.h"
#include "items.h"
#include "normalization.h"
#include "npc.h"
#include "monster_mechanics.h"
#include "objects.h"
#include "prayer.h"
#include "player_actions.h"
#include "quests.h"
#include "shops.h"
#include "slayer.h"
#include "skills.h"
#include "spells.h"
#include "traversal.h"
#include "varbits.h"

#include <ctype.h>
#include <dirent.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define RC_VALIDATION_MAX_REQUIRED 512
#define RC_RUNTIME_DATA_VERSION "v1"

typedef struct {
    char **items;
    size_t count;
} RcStringList;

struct RcGameData {
    _Atomic int ref_count;
    int published;
    int owns_load_lock;
    char *identity;
    RcGameDataValidationReport validation;
    RcGameDataStats stats;
    RcNpcDef npc_defs[RC_MAX_NPC_DEFS];
    int npc_def_count;
    int npc_def_by_id[RC_MAX_NPC_ID];
    int32_t *npc_transforms;
    int npc_transform_count;
    RcItemDef item_defs[RC_MAX_ITEM_DEFS];
    int item_def_count;
    RcDropData drop_data;
    RcItemNormalization item_normalization[RC_MAX_ITEM_DEFS];
    int item_normalization_count;
    RcNpcNormalization npc_normalization[RC_MAX_NPC_ID];
    int npc_normalization_count;
    RcSourceNormalization *source_normalization;
    int source_normalization_count;
    RcPrayerDef prayer_defs[RC_MAX_PRAYER_DEFS];
    int prayer_count;
    RcSpellDef spell_defs[RC_MAX_SPELL_DEFS];
    int spell_count;
    RcObjectData object_data;
    RcCollisionData collision_data;
    RcAreaFlagData area_flag_data;
    RcTraversalData traversal_data;
    RcActivitySchemaData activity_schema_data;
    RcActivitySpawnData activity_spawn_data;
    RcActivityMechanicData activity_mechanic_data;
    RcActivityStateData activity_state_data;
    RcEncounterSpec encounter_specs[RC_ENC_REGISTRY_CAP];
    int encounter_spec_count;
    RcVarData var_data;
    RcSkillData skill_data;
    RcShopData shop_data;
    RcQuestData quest_data;
    RcDialogueData dialogue_data;
};

static atomic_flag g_runtime_data_lock = ATOMIC_FLAG_INIT;
static RcGameData *g_runtime_data = NULL;

static void rc_game_data_activate_views(const RcGameData *data,
                                        uint32_t subsystems);

static void rc_runtime_data_lock(void) {
    while (atomic_flag_test_and_set_explicit(&g_runtime_data_lock,
                                              memory_order_acquire)) {
    }
}

static void rc_runtime_data_unlock(void) {
    atomic_flag_clear_explicit(&g_runtime_data_lock, memory_order_release);
}

static int rc_identity_append(char *identity, size_t capacity, size_t *used,
                              const char *format, ...) {
    if (!identity || !used || *used >= capacity) return 0;
    va_list args;
    va_start(args, format);
    int written = vsnprintf(identity + *used, capacity - *used, format, args);
    va_end(args);
    if (written < 0 || (size_t)written >= capacity - *used) return 0;
    *used += (size_t)written;
    return 1;
}

static char *rc_game_data_identity(const RcWorldConfig *cfg) {
    if (!cfg) return NULL;
    size_t capacity = 32768;
    char *identity = calloc(capacity, 1);
    if (!identity) return NULL;
    size_t used = 0;
    if (!rc_identity_append(identity, capacity, &used, "subsystems=%08x\n",
                            cfg->subsystems)) {
        free(identity);
        return NULL;
    }
#define RC_ID_PATH(field) \
    do { \
        if (!rc_identity_append(identity, capacity, &used, #field "=%s\n", \
                                cfg->field ? cfg->field : "")) { \
            free(identity); \
            return NULL; \
        } \
    } while (0)
    RC_ID_PATH(regions_dir);
    RC_ID_PATH(npc_defs_path);
    RC_ID_PATH(spawns_path);
    RC_ID_PATH(varbits_path);
    RC_ID_PATH(varps_path);
    RC_ID_PATH(items_path);
    RC_ID_PATH(normalization_path);
    RC_ID_PATH(drops_path);
    RC_ID_PATH(skill_drops_path);
    RC_ID_PATH(rdt_path);
    RC_ID_PATH(gdt_path);
    RC_ID_PATH(mrdt_path);
    RC_ID_PATH(recipes_path);
    RC_ID_PATH(gathering_nodes_path);
    RC_ID_PATH(quests_path);
    RC_ID_PATH(dialogue_path);
    RC_ID_PATH(shops_path);
    RC_ID_PATH(slayer_path);
    RC_ID_PATH(prayers_path);
    RC_ID_PATH(spells_path);
    RC_ID_PATH(combat_profiles_path);
    RC_ID_PATH(player_actions_path);
    RC_ID_PATH(monster_mechanics_path);
    RC_ID_PATH(activity_schemas_path);
    RC_ID_PATH(activity_spawns_path);
    RC_ID_PATH(activity_mechanics_path);
    RC_ID_PATH(activity_states_path);
    RC_ID_PATH(object_defs_path);
    RC_ID_PATH(object_placements_path);
    RC_ID_PATH(object_behaviors_path);
    RC_ID_PATH(collision_tiles_path);
    RC_ID_PATH(area_flags_path);
    RC_ID_PATH(traversal_edges_path);
    RC_ID_PATH(encounters_path);
#undef RC_ID_PATH
    const char *root = getenv("RUNEC_DATA_ROOT");
    const char *packs = getenv("RUNEC_PACK_DIR");
    const char *backend = getenv("RUNEC_ASSET_BACKEND");
    if (!rc_identity_append(identity, capacity, &used,
                            "data_root=%s\npack_dir=%s\nbackend=%s\n",
                            root ? root : "", packs ? packs : "",
                            backend ? backend : "")) {
        free(identity);
        return NULL;
    }
    return identity;
}

static void rc_report_init(RcGameDataValidationReport *report) {
    if (!report) return;
    memset(report, 0, sizeof(*report));
}

static void rc_report_message(RcGameDataValidationReport *report,
                              const char *message) {
    if (!report || !message) return;
    snprintf(report->message, sizeof(report->message), "%s", message);
}

static int rc_has_suffix(const char *s, const char *suffix) {
    size_t s_len = strlen(s);
    size_t suffix_len = strlen(suffix);
    return s_len >= suffix_len
        && strcmp(s + s_len - suffix_len, suffix) == 0;
}

static int rc_file_exists(const char *path) {
    struct stat st;
    return path && stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static int rc_dir_has_pak(const char *path) {
    DIR *dir = opendir(path);
    if (!dir) return 0;
    int found = 0;
    struct dirent *de;
    while ((de = readdir(dir)) != NULL) {
        if (rc_has_suffix(de->d_name, ".pak")) {
            found = 1;
            break;
        }
    }
    closedir(dir);
    return found;
}

static int rc_join(char *out, size_t cap, const char *a, const char *b) {
    if (!out || cap == 0 || !a || !b) return 0;
    int n = snprintf(out, cap, "%s/%s", a, b);
    return n > 0 && (size_t)n < cap;
}

static void rc_effective_data_root(const char *data_root, char *out, size_t cap) {
    const char *root = data_root && data_root[0] ? data_root : getenv("RUNEC_DATA_ROOT");
    if (!root || !root[0]) root = "data";
    snprintf(out, cap, "%s", root);
}

static void rc_effective_pack_dir(const char *data_root, char *out, size_t cap) {
    const char *pack_dir = getenv("RUNEC_PACK_DIR");
    if (pack_dir && pack_dir[0]) {
        snprintf(out, cap, "%s", pack_dir);
        return;
    }
    rc_join(out, cap, data_root, "packs");
    if (strcmp(data_root, "data") == 0 && !rc_dir_has_pak(out)
            && rc_dir_has_pak("dist-data/packs")) {
        snprintf(out, cap, "%s", "dist-data/packs");
    }
}

static char *rc_read_file(const char *path, size_t *out_size) {
    if (out_size) *out_size = 0;
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    long raw_size = ftell(f);
    if (raw_size < 0 || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }
    size_t size = (size_t)raw_size;
    char *text = (char *)malloc(size + 1);
    if (!text) {
        fclose(f);
        return NULL;
    }
    if (size > 0 && fread(text, 1, size, f) != size) {
        free(text);
        fclose(f);
        return NULL;
    }
    fclose(f);
    text[size] = '\0';
    if (out_size) *out_size = size;
    return text;
}

static const char *rc_skip_ws(const char *p) {
    while (p && *p && isspace((unsigned char)*p)) p++;
    return p;
}

static const char *rc_json_skip_string_value(const char *p) {
    if (!p || *p != '"') return NULL;
    for (p++; *p; p++) {
        if (*p == '"') return p + 1;
        if (*p == '\\') {
            p++;
            if (!*p) return NULL;
            if (*p == 'u') {
                for (int i = 0; i < 4; i++) {
                    p++;
                    if (!isxdigit((unsigned char)*p)) return NULL;
                }
            }
        } else if ((unsigned char)*p < 0x20) {
            return NULL;
        }
    }
    return NULL;
}

static const char *rc_json_skip_value(const char *p) {
    p = rc_skip_ws(p);
    if (!p || !*p) return NULL;
    if (*p == '"') return rc_json_skip_string_value(p);
    if (*p == '[') {
        p = rc_skip_ws(p + 1);
        if (*p == ']') return p + 1;
        for (;;) {
            p = rc_json_skip_value(p);
            if (!p) return NULL;
            p = rc_skip_ws(p);
            if (*p == ']') return p + 1;
            if (*p != ',') return NULL;
            p = rc_skip_ws(p + 1);
        }
    }
    if (*p == '{') {
        p = rc_skip_ws(p + 1);
        if (*p == '}') return p + 1;
        for (;;) {
            const char *key_end = rc_json_skip_string_value(p);
            if (!key_end) return NULL;
            p = rc_skip_ws(key_end);
            if (*p != ':') return NULL;
            p = rc_json_skip_value(p + 1);
            if (!p) return NULL;
            p = rc_skip_ws(p);
            if (*p == '}') return p + 1;
            if (*p != ',') return NULL;
            p = rc_skip_ws(p + 1);
        }
    }
    const char *start = p;
    while (*p && *p != ',' && *p != ']' && *p != '}'
            && !isspace((unsigned char)*p)) {
        p++;
    }
    return p == start ? NULL : p;
}

static int rc_json_top_level_value(const char *json, const char *key,
                                   const char **value_start,
                                   const char **value_end) {
    if (value_start) *value_start = NULL;
    if (value_end) *value_end = NULL;
    const char *p = rc_skip_ws(json);
    if (!p || *p != '{') return 0;
    p = rc_skip_ws(p + 1);
    int found = 0;
    while (*p && *p != '}') {
        if (*p != '"') return 0;
        const char *name = p + 1;
        const char *name_end = rc_json_skip_string_value(p);
        if (!name_end) return 0;
        const char *closing = name_end - 1;
        p = rc_skip_ws(name_end);
        if (*p != ':') return 0;
        const char *start = rc_skip_ws(p + 1);
        const char *end = rc_json_skip_value(start);
        if (!end) return 0;
        size_t name_length = (size_t)(closing - name);
        if (name_length == strlen(key)
                && memcmp(name, key, name_length) == 0) {
            if (found) return 0;
            found = 1;
            if (value_start) *value_start = start;
            if (value_end) *value_end = end;
        }
        p = rc_skip_ws(end);
        if (*p == ',') p = rc_skip_ws(p + 1);
        else if (*p != '}') return 0;
    }
    if (*p != '}') return 0;
    p = rc_skip_ws(p + 1);
    return *p == '\0' && found;
}

static int rc_json_top_level_string_equals(const char *json, const char *key,
                                           const char *expected) {
    const char *start = NULL;
    const char *end = NULL;
    if (!rc_json_top_level_value(json, key, &start, &end)
            || !start || *start != '"') {
        return 0;
    }
    size_t length = strlen(expected);
    return end == start + length + 2
        && memcmp(start + 1, expected, length) == 0
        && start[length + 1] == '"';
}

static int rc_json_string_array(const char *json, const char *key,
                                RcStringList *out) {
    memset(out, 0, sizeof(*out));
    const char *p = NULL;
    const char *array_end = NULL;
    if (!rc_json_top_level_value(json, key, &p, &array_end)) return 0;
    if (*p != '[') return 0;
    p++;

    char **items = NULL;
    size_t count = 0;
    while (p < array_end) {
        p = rc_skip_ws(p);
        if (*p == ']') {
            out->items = items;
            out->count = count;
            return rc_skip_ws(p + 1) == array_end;
        }
        if (*p == ',') {
            p++;
            continue;
        }
        if (*p != '"' || count >= RC_VALIDATION_MAX_REQUIRED) break;
        p++;
        const char *start = p;
        while (p < array_end && *p && *p != '"') {
            if (*p == '\\' && p[1]) p++;
            p++;
        }
        if (p >= array_end || *p != '"') break;
        size_t len = (size_t)(p - start);
        char *item = (char *)malloc(len + 1);
        if (!item) break;
        memcpy(item, start, len);
        item[len] = '\0';

        char **next = (char **)realloc(items, (count + 1) * sizeof(*items));
        if (!next) {
            free(item);
            break;
        }
        items = next;
        items[count++] = item;
        p++;
    }

    for (size_t i = 0; i < count; i++) free(items[i]);
    free(items);
    return 0;
}

static void rc_string_list_free(RcStringList *list) {
    if (!list) return;
    for (size_t i = 0; i < list->count; i++) free(list->items[i]);
    free(list->items);
    list->items = NULL;
    list->count = 0;
}

typedef int (*RcManifestPathFn)(const char *path, void *ctx);

static int rc_json_object_string_field(const char *object,
                                       const char *object_end,
                                       const char *field,
                                       char *out, size_t out_capacity) {
    const char *p = rc_skip_ws(object);
    if (!p || p >= object_end || *p != '{') return 0;
    p = rc_skip_ws(p + 1);
    int found = 0;
    while (p < object_end && *p != '}') {
        if (*p != '"') return 0;
        const char *name = p + 1;
        const char *name_end = rc_json_skip_string_value(p);
        if (!name_end || name_end > object_end) return 0;
        const char *name_close = name_end - 1;
        p = rc_skip_ws(name_end);
        if (p >= object_end || *p != ':') return 0;
        const char *value = rc_skip_ws(p + 1);
        const char *value_end = rc_json_skip_value(value);
        if (!value_end || value_end > object_end) return 0;

        size_t name_length = (size_t)(name_close - name);
        if (name_length == strlen(field)
                && memcmp(name, field, name_length) == 0) {
            if (found || *value != '"'
                    || rc_json_skip_string_value(value) != value_end) {
                return 0;
            }
            size_t value_length = (size_t)(value_end - value - 2);
            if (value_length + 1 > out_capacity
                    || memchr(value + 1, '\\', value_length)) {
                return 0;
            }
            memcpy(out, value + 1, value_length);
            out[value_length] = '\0';
            found = 1;
        }
        p = rc_skip_ws(value_end);
        if (p < object_end && *p == ',') p = rc_skip_ws(p + 1);
        else if (p >= object_end || *p != '}') return 0;
    }
    return found && p + 1 == object_end;
}

static int rc_manifest_each_path(const char *manifest, const char *array_name,
                                 int required, RcManifestPathFn callback,
                                 void *ctx, int *count) {
    if (count) *count = 0;
    const char *array = NULL;
    const char *array_end = NULL;
    if (!rc_json_top_level_value(manifest, array_name, &array, &array_end))
        return !required;
    if (!array || *array != '[') return 0;

    const char *p = rc_skip_ws(array + 1);
    while (p < array_end && *p != ']') {
        const char *object_end = rc_json_skip_value(p);
        char path[2048];
        if (!object_end || object_end > array_end || *p != '{'
                || !rc_json_object_string_field(p, object_end, "path",
                                                path, sizeof(path))
                || (callback && !callback(path, ctx))) {
            return 0;
        }
        if (count) (*count)++;
        p = rc_skip_ws(object_end);
        if (p < array_end && *p == ',') p = rc_skip_ws(p + 1);
        else if (p >= array_end || *p != ']') return 0;
    }
    return p < array_end && *p == ']' && p + 1 == array_end;
}

typedef struct {
    const RcStringList *required;
    unsigned char *found;
} RcManifestAssetStatus;

static int rc_manifest_mark_asset(const char *path, void *opaque) {
    RcManifestAssetStatus *status = opaque;
    for (size_t i = 0; i < status->required->count; i++) {
        if (strcmp(path, status->required->items[i]) == 0)
            status->found[i] = 1;
    }
    return 1;
}

typedef struct {
    const char *pack_dir;
    int missing_count;
    char first_missing[512];
} RcManifestPackStatus;

static int rc_manifest_check_pack(const char *logical, void *opaque) {
    RcManifestPackStatus *status = opaque;
    if (strncmp(logical, "packs/", 6) != 0
            || !rc_has_suffix(logical, ".pak")) {
        return 0;
    }
    char path[2048];
    if (!rc_join(path, sizeof(path), status->pack_dir, logical + 6)
            || !rc_file_exists(path)) {
        status->missing_count++;
        if (!status->first_missing[0]) {
            snprintf(status->first_missing, sizeof(status->first_missing),
                     "%s", logical);
        }
    }
    return 1;
}

static int rc_ascii_lower(int c) {
    if (c >= 'A' && c <= 'Z') return c + ('a' - 'A');
    return c;
}

static int rc_contains_ascii_casefold(const char *haystack, const char *needle) {
    if (!haystack || !needle || !needle[0]) return 0;
    size_t needle_len = strlen(needle);
    for (const char *p = haystack; *p; p++) {
        size_t i = 0;
        while (i < needle_len && p[i]
                && rc_ascii_lower((unsigned char)p[i])
                   == rc_ascii_lower((unsigned char)needle[i])) {
            i++;
        }
        if (i == needle_len) return 1;
    }
    return 0;
}

static int rc_blocked_marker_count(const char *text) {
    static const char *markers[] = {
        "blocked_source",
        "blocked-source",
        "blocked provenance",
        "void" "_rsps",
        "void" "ps:",
        "Void" "PS",
        "2011" "Scape",
        "SCAPE" "_2011",
        "near" "_reality",
        "Near" "-Reality",
        "Map" "Locations.java",
        "zen" "yte",
    };
    int count = 0;
    for (size_t i = 0; i < sizeof(markers) / sizeof(markers[0]); i++) {
        if (rc_contains_ascii_casefold(text, markers[i]))
            count++;
    }
    return count;
}

static int rc_dev_allows_blocked_provenance(void) {
    const char *value = getenv("RUNEC_ALLOW_BLOCKED_SOURCE_PROVENANCE");
    return value && strcmp(value, "1") == 0;
}

static int rc_required_text_asset(const char *logical) {
    return rc_has_suffix(logical, ".tsv")
        || rc_has_suffix(logical, ".json")
        || rc_has_suffix(logical, ".toml")
        || rc_has_suffix(logical, ".txt");
}

static int rc_loose_asset_has_blocked_marker(const char *data_root,
                                             const char *logical) {
    char path[2048];
    if (!rc_join(path, sizeof(path), data_root, logical)
            || !rc_file_exists(path))
        return 0;
    size_t size = 0;
    char *text = rc_read_file(path, &size);
    (void)size;
    if (!text) return 0;
    int count = rc_blocked_marker_count(text);
    free(text);
    return count;
}

static int rc_backend_is_pack(void) {
    const char *backend = getenv("RUNEC_ASSET_BACKEND");
    return backend && strcmp(backend, "pack") == 0;
}

static int rc_backend_is_loose(void) {
    const char *backend = getenv("RUNEC_ASSET_BACKEND");
    return backend && strcmp(backend, "loose") == 0;
}

int rc_game_data_validate_install(const char *data_root,
                                  RcGameDataValidationReport *out) {
    RcGameDataValidationReport local;
    RcGameDataValidationReport *report = out ? out : &local;
    rc_report_init(report);

    char root[1024];
    char manifest_path[2048];
    char pack_dir[2048];
    rc_effective_data_root(data_root, root, sizeof(root));
    rc_effective_pack_dir(root, pack_dir, sizeof(pack_dir));
    if (!rc_join(manifest_path, sizeof(manifest_path), root, "manifest.json")) {
        rc_report_message(report, "data install validation failed: manifest path is too long");
        return 0;
    }

    report->manifest_present = rc_file_exists(manifest_path);
    if (!report->manifest_present) {
        snprintf(report->message, sizeof(report->message),
                 "missing runtime data manifest: %.384s; run setup-data",
                 manifest_path);
        return 0;
    }

    size_t manifest_size = 0;
    char *manifest = rc_read_file(manifest_path, &manifest_size);
    (void)manifest_size;
    if (!manifest) {
        rc_report_message(report, "could not read runtime data manifest");
        return 0;
    }

    report->manifest_format_ok = rc_json_top_level_string_equals(
        manifest, "format", "runec-data-manifest-v1");
    report->data_version_present = rc_json_top_level_string_equals(
        manifest, "data_version", RC_RUNTIME_DATA_VERSION);
    if (!report->manifest_format_ok || !report->data_version_present) {
        free(manifest);
        rc_report_message(
            report,
            "runtime data manifest identity is invalid or incompatible");
        return 0;
    }

    RcStringList required;
    if (!rc_json_string_array(manifest, "required_logical_paths", &required)
            || required.count == 0) {
        free(manifest);
        rc_report_message(report, "runtime data manifest is missing required_logical_paths");
        return 0;
    }
    report->required_path_count = (int)required.count;

    unsigned char *asset_found = calloc(required.count, 1);
    RcManifestAssetStatus asset_status = {&required, asset_found};
    int asset_count = 0;
    RcManifestPackStatus pack_status = {.pack_dir = pack_dir};
    if (!asset_found
            || !rc_manifest_each_path(manifest, "assets", 1,
                                      rc_manifest_mark_asset, &asset_status,
                                      &asset_count)
            || !rc_manifest_each_path(manifest, "packs", 0,
                                      rc_manifest_check_pack, &pack_status,
                                      &report->pack_file_count)) {
        free(asset_found);
        rc_string_list_free(&required);
        free(manifest);
        rc_report_message(report,
                          "runtime data manifest asset catalog is invalid");
        return 0;
    }
    (void)asset_count;
    report->missing_pack_files = pack_status.missing_count;
    int pack_available = report->pack_file_count > 0
                       && report->missing_pack_files == 0
                       && rc_dir_has_pak(pack_dir);
    int force_pack = rc_backend_is_pack();
    int force_loose = rc_backend_is_loose();
    char first_missing[512] = "";
    for (size_t i = 0; i < required.count; i++) {
        const char *logical = rc_asset_logical_path(required.items[i]);
        char loose_path[2048];
        int loose_ok = rc_join(loose_path, sizeof(loose_path), root, logical)
                    && rc_file_exists(loose_path);
        int pack_ok = pack_available && asset_found[i];
        int ok = force_pack ? pack_ok : force_loose ? loose_ok : (loose_ok || pack_ok);
        if (!ok) {
            report->missing_required_paths++;
            if (!first_missing[0])
                snprintf(first_missing, sizeof(first_missing), "%s", logical);
        }
        if (rc_required_text_asset(logical))
            report->blocked_provenance_count +=
                rc_loose_asset_has_blocked_marker(root, logical);
    }

    report->blocked_provenance_count += rc_blocked_marker_count(manifest);
    int allow_blocked = rc_dev_allows_blocked_provenance();

    if (force_pack && report->missing_pack_files > 0) {
        snprintf(report->message, sizeof(report->message),
                 "runtime data missing %d manifest pack file(s); first: %.256s",
                 report->missing_pack_files, pack_status.first_missing);
        free(asset_found);
        rc_string_list_free(&required);
        free(manifest);
        return 0;
    }
    if (report->missing_required_paths > 0) {
        snprintf(report->message, sizeof(report->message),
                 "runtime data missing %d required path(s); first: %.256s",
                 report->missing_required_paths, first_missing);
        free(asset_found);
        rc_string_list_free(&required);
        free(manifest);
        return 0;
    }
    if (report->blocked_provenance_count > 0 && !allow_blocked) {
        snprintf(report->message, sizeof(report->message),
                 "runtime data install contains %d blocked-source provenance marker(s); "
                 "regenerate from approved sources or set RUNEC_ALLOW_BLOCKED_SOURCE_PROVENANCE=1 for dev-only investigation",
                 report->blocked_provenance_count);
        free(asset_found);
        rc_string_list_free(&required);
        free(manifest);
        return 0;
    }

    report->ok = 1;
    snprintf(report->message, sizeof(report->message),
             "runtime data install valid: %d required path(s)",
             report->required_path_count);
    free(asset_found);
    rc_string_list_free(&required);
    free(manifest);
    return 1;
}

static int rc_game_data_validate_identity(RcGameDataValidationReport *report) {
    rc_report_init(report);

    char root[1024];
    char manifest_path[2048];
    rc_effective_data_root(NULL, root, sizeof(root));
    if (!rc_join(manifest_path, sizeof(manifest_path), root, "manifest.json")) {
        rc_report_message(report,
                          "runtime data manifest path is too long");
        return 0;
    }
    report->manifest_present = rc_file_exists(manifest_path);
    if (!report->manifest_present) {
        snprintf(report->message, sizeof(report->message),
                 "missing runtime data manifest: %.384s; run setup-data",
                 manifest_path);
        return 0;
    }

    size_t manifest_size = 0;
    char *manifest = rc_read_file(manifest_path, &manifest_size);
    (void)manifest_size;
    if (!manifest) {
        rc_report_message(report, "could not read runtime data manifest");
        return 0;
    }
    report->manifest_format_ok = rc_json_top_level_string_equals(
        manifest, "format", "runec-data-manifest-v1");
    report->data_version_present = rc_json_top_level_string_equals(
        manifest, "data_version", RC_RUNTIME_DATA_VERSION);
    report->blocked_provenance_count = rc_blocked_marker_count(manifest);
    free(manifest);

    if (!report->manifest_format_ok || !report->data_version_present) {
        rc_report_message(
            report,
            "runtime data manifest identity is invalid or incompatible");
        return 0;
    }
    if (report->blocked_provenance_count > 0
            && !rc_dev_allows_blocked_provenance()) {
        rc_report_message(report,
                          "runtime data manifest has blocked provenance");
        return 0;
    }
    report->ok = 1;
    rc_report_message(report, "runtime data manifest identity is valid");
    return 1;
}

static void rc_load_report_init(RcGameDataLoadReport *report) {
    if (!report) return;
    memset(report, 0, sizeof(*report));
}

static void rc_load_report_message(RcGameDataLoadReport *report,
                                   const char *message) {
    if (!report || !message) return;
    snprintf(report->message, sizeof(report->message), "%s", message);
}

static int rc_validate_before_load(RcGameDataLoadReport *report) {
    const char *validate_data = getenv("RUNEC_VALIDATE_DATA_INSTALL");
    if (validate_data && strcmp(validate_data, "0") == 0) {
        if (report) report->validation_ok = 1;
        return 1;
    }

    RcGameDataValidationReport validation;
    if (!rc_game_data_validate_identity(&validation)) {
        if (report) {
            report->validation = validation;
            report->validation_ok = 0;
            snprintf(report->message, sizeof(report->message),
                     "%s", validation.message);
        }
        return 0;
    }
    if (report) {
        report->validation = validation;
        report->validation_ok = 1;
    }
    return 1;
}

static int rc_load_required_table(const char *name, const char *path,
                                  int already_loaded,
                                  int (*loader)(const char *),
                                  RcGameDataLoadReport *report) {
    if (already_loaded) return 1;
    int loaded = loader(path);
    if (loaded >= 0) return 1;
    if (report) {
        snprintf(report->message, sizeof(report->message),
                 "failed to load %s from %.384s", name,
                 path ? path : "(null)");
    }
    return 0;
}

static int rc_mirror_normalization_to_globals(const RcGameData *data) {
    if (!data) return 0;
    RcSourceNormalization *source_copy = NULL;
    if (data->source_normalization_count > 0) {
        source_copy = malloc((size_t)data->source_normalization_count
                             * sizeof(*source_copy));
        if (!source_copy) return 0;
        memcpy(source_copy, data->source_normalization,
               (size_t)data->source_normalization_count
               * sizeof(*source_copy));
    }

    memcpy(g_rc_item_normalization, data->item_normalization,
           sizeof(g_rc_item_normalization));
    memcpy(g_rc_npc_normalization, data->npc_normalization,
           sizeof(g_rc_npc_normalization));
    free(g_rc_source_normalization);
    g_rc_source_normalization = source_copy;
    g_rc_item_normalization_count = data->item_normalization_count;
    g_rc_npc_normalization_count = data->npc_normalization_count;
    g_rc_source_normalization_count = data->source_normalization_count;
    return 1;
}

static void rc_mirror_item_defs_to_globals(const RcGameData *data) {
    if (!data) return;
    memcpy(g_item_defs, data->item_defs, sizeof(g_item_defs));
    g_item_def_count = data->item_def_count;
}

static RcGameDataStats rc_collect_game_data_stats(const RcWorldConfig *cfg,
                                                  const RcGameData *data) {
    uint32_t subsystems = cfg->subsystems;
    uint32_t npc_users = RC_SUB_COMBAT | RC_SUB_DIALOGUE | RC_SUB_SHOPS
                       | RC_SUB_SLAYER | RC_SUB_ENCOUNTER | RC_SUB_STORAGE;
    uint32_t item_users = RC_SUB_EQUIPMENT | RC_SUB_INVENTORY
                        | RC_SUB_CONSUMABLES | RC_SUB_LOOT
                        | RC_SUB_SKILLS | RC_SUB_SHOPS | RC_SUB_STORAGE;
    uint32_t normalization_users = npc_users | item_users | RC_SUB_LOOT
                                 | RC_SUB_SHOPS | RC_SUB_STORAGE;
    int needs_npcs = (subsystems & npc_users) != 0;
    int needs_items = (subsystems & item_users) != 0;
    int needs_normalization = (subsystems & normalization_users) != 0;
    int needs_loot = (subsystems & RC_SUB_LOOT) != 0;
    int needs_prayer = (subsystems & RC_SUB_PRAYER) != 0;
    int needs_spells = (subsystems & RC_SUB_COMBAT) != 0;
    int needs_objects = (subsystems & RC_SUB_OBJECTS) != 0;
    int needs_regions = (subsystems & RC_SUB_REGIONS) != 0;
    int needs_traversal = (subsystems & RC_SUB_TRAVERSAL) != 0;
    int needs_activity = (subsystems & RC_SUB_ENCOUNTER) != 0;
    int needs_skill_drops = (subsystems & (RC_SUB_LOOT | RC_SUB_SKILLS)) != 0;
    int needs_skills = (subsystems & RC_SUB_SKILLS) != 0;
    int needs_shops = (subsystems & RC_SUB_SHOPS) != 0;
    int needs_quests = (subsystems & RC_SUB_QUESTS) != 0;
    int needs_dialogue = (subsystems & RC_SUB_DIALOGUE) != 0;

    return (RcGameDataStats){
        .subsystems = subsystems,
        .npc_def_count = needs_npcs && data ? data->npc_def_count : 0,
        .item_def_count = needs_items && data ? data->item_def_count : 0,
        .item_normalization_count = needs_normalization
                                  && data ? data->item_normalization_count : 0,
        .npc_normalization_count = needs_normalization
                                 && data ? data->npc_normalization_count : 0,
        .source_normalization_count = needs_normalization
                                    && data ? data->source_normalization_count
                                            : 0,
        .drop_table_count = needs_loot && data ? data->drop_data.table_count
                                                : 0,
        .drop_entry_count = needs_loot && data ? data->drop_data.entry_count
                                                : 0,
        .rdt_entry_count = needs_loot && data
                         ? data->drop_data.rdt_entry_count : 0,
        .gdt_entry_count = needs_loot && data
                         ? data->drop_data.gdt_entry_count : 0,
        .mrdt_entry_count = needs_loot && data
                          ? data->drop_data.mrdt_entry_count : 0,
        .prayer_count = needs_prayer && data ? data->prayer_count : 0,
        .spell_count = needs_spells && data ? data->spell_count : 0,
        .object_def_count = needs_objects && data
                          ? data->object_data.def_count : 0,
        .object_behavior_count = needs_objects && data
                               ? data->object_data.behavior_count : 0,
        .object_placement_count = needs_objects && data
                                ? data->object_data.placement_count : 0,
        .object_param_count = needs_objects && data
                          ? data->object_data.param_count : 0,
        .collision_region_count = needs_regions
                                && data ? data->collision_data.region_count
                                        : 0,
        .area_flag_count = needs_regions && data
                         ? data->area_flag_data.row_count : 0,
        .traversal_edge_count = needs_traversal
                              && data ? data->traversal_data.edge_count : 0,
        .activity_schema_count = needs_activity
                               && data ? data->activity_schema_data.count : 0,
        .activity_spawn_count = needs_activity
                              && data ? data->activity_spawn_data.count : 0,
        .activity_mechanic_count = needs_activity
                                 && data ? data->activity_mechanic_data.count
                                        : 0,
        .activity_state_count = needs_activity
                              && data ? data->activity_state_data.count : 0,
        .varbit_count = data ? data->var_data.varbit_count : 0,
        .varp_count = data ? data->var_data.varp_count : 0,
        .recipe_count = needs_skills && data
                      ? data->skill_data.recipe_count : 0,
        .skill_drop_source_count = needs_skill_drops && data
                                 ? data->skill_data.drop_source_count : 0,
        .skill_drop_count = needs_skill_drops && data
                          ? data->skill_data.drop_count : 0,
        .gathering_node_count = needs_skills && data
                              ? data->skill_data.gathering_node_count : 0,
        .shop_count = needs_shops && data ? data->shop_data.shop_count : 0,
        .shop_stock_count = needs_shops && data
                          ? data->shop_data.stock_count : 0,
        .quest_count = needs_quests && data ? data->quest_data.count : 0,
        .dialogue_transcript_count = needs_dialogue && data
                                   ? data->dialogue_data.transcript_count : 0,
        .dialogue_node_count = needs_dialogue && data
                            ? data->dialogue_data.node_count : 0,
        .dialogue_child_id_count = needs_dialogue && data
                                 ? data->dialogue_data.child_id_count : 0,
    };
}

static uint32_t rc_loaded_capabilities(uint32_t requested,
                                       const RcGameData *data) {
    uint32_t capabilities = requested;
    uint32_t npc_users = RC_SUB_COMBAT | RC_SUB_DIALOGUE | RC_SUB_SHOPS
                       | RC_SUB_SLAYER | RC_SUB_ENCOUNTER | RC_SUB_STORAGE;
    if (data->npc_def_count == 0) capabilities &= ~npc_users;
    if ((requested & RC_SUB_PRAYER) && data->prayer_count == 0)
        capabilities &= ~RC_SUB_PRAYER;
    uint32_t item_users = RC_SUB_EQUIPMENT | RC_SUB_INVENTORY
                        | RC_SUB_CONSUMABLES | RC_SUB_SHOPS | RC_SUB_STORAGE;
    if (data->item_def_count == 0) capabilities &= ~item_users;
    if ((requested & RC_SUB_QUESTS) && data->quest_data.count == 0)
        capabilities &= ~RC_SUB_QUESTS;
    if ((requested & RC_SUB_DIALOGUE)
            && data->dialogue_data.transcript_count == 0) {
        capabilities &= ~RC_SUB_DIALOGUE;
    }
    if ((requested & RC_SUB_SHOPS) && data->shop_data.shop_count == 0)
        capabilities &= ~RC_SUB_SHOPS;
    if ((requested & RC_SUB_SLAYER) && g_rc_slayer_master_count == 0)
        capabilities &= ~RC_SUB_SLAYER;
    if ((requested & RC_SUB_OBJECTS)
            && (data->object_data.def_count == 0
                || data->object_data.behavior_count == 0)) {
        capabilities &= ~RC_SUB_OBJECTS;
    }
    if ((requested & RC_SUB_REGIONS)
            && data->collision_data.region_count == 0
            && data->area_flag_data.row_count == 0) {
        capabilities &= ~RC_SUB_REGIONS;
    }
    if ((requested & RC_SUB_TRAVERSAL)
            && data->traversal_data.edge_count == 0) {
        capabilities &= ~RC_SUB_TRAVERSAL;
    }
    return capabilities;
}

// Definition name for an NPC cache id, or NULL when the id has no definition.
static const char *game_data_npc_name(const void *ctx, uint32_t npc_id) {
    const RcGameData *data = ctx;
    if (npc_id >= RC_MAX_NPC_ID) return NULL;
    int idx = data->npc_def_by_id[npc_id];
    if (idx < 0 || idx >= data->npc_def_count
            || data->npc_defs[idx].id != (int)npc_id) return NULL;
    return data->npc_defs[idx].name;
}

// Definition name for an object cache id, or NULL when it has none.
static const char *game_data_object_name(const void *ctx, uint32_t loc_id) {
    const RcGameData *data = ctx;
    if (loc_id >= RC_MAX_OBJECT_ID || !data->object_data.defs[loc_id].loaded)
        return NULL;
    return data->object_data.defs[loc_id].name;
}

RcGameData *rc_game_data_load(const RcWorldConfig *cfg,
                              RcGameDataLoadReport *out) {
    RcGameDataLoadReport local;
    RcGameDataLoadReport *report = out ? out : &local;
    rc_load_report_init(report);

    if (!cfg) {
        rc_load_report_message(report, "missing RcWorldConfig");
        return NULL;
    }
    char config_error[256];
    if (!rc_world_config_validate(cfg, config_error, sizeof(config_error))) {
        rc_load_report_message(report, config_error);
        return NULL;
    }
    char *identity = rc_game_data_identity(cfg);
    if (!identity) {
        rc_load_report_message(report, "failed to build game-data identity");
        return NULL;
    }

    rc_runtime_data_lock();
    if (g_runtime_data) {
        if (strcmp(g_runtime_data->identity, identity) != 0) {
            rc_runtime_data_unlock();
            free(identity);
            rc_load_report_message(
                report,
                "incompatible RcGameData is already active in this process");
            return NULL;
        }
        rc_game_data_retain(g_runtime_data);
        report->validation = g_runtime_data->validation;
        report->validation_ok = g_runtime_data->validation.ok;
        report->stats = g_runtime_data->stats;
        report->ok = 1;
        snprintf(report->message, sizeof(report->message),
                 "reused immutable game data: npc_defs=%d items=%d",
                 report->stats.npc_def_count, report->stats.item_def_count);
        RcGameData *shared = g_runtime_data;
        rc_runtime_data_unlock();
        free(identity);
        return shared;
    }
    if (!rc_validate_before_load(report)) {
        rc_runtime_data_unlock();
        free(identity);
        return NULL;
    }

    RcGameData *data = (RcGameData *)calloc(1, sizeof(*data));
    if (!data) {
        rc_runtime_data_unlock();
        free(identity);
        rc_load_report_message(report, "failed to allocate RcGameData");
        return NULL;
    }
    atomic_init(&data->ref_count, 1);
    data->owns_load_lock = 1;
    data->identity = identity;
    data->validation = report->validation;
    rc_drop_data_init(&data->drop_data);
    rc_object_data_init(&data->object_data);
    rc_collision_data_init(&data->collision_data);
    rc_area_flag_data_init(&data->area_flag_data);
    rc_traversal_data_init(&data->traversal_data);
    rc_activity_schema_data_init(&data->activity_schema_data);
    rc_activity_spawn_data_init(&data->activity_spawn_data);
    rc_activity_mechanic_data_init(&data->activity_mechanic_data);
    rc_activity_state_data_init(&data->activity_state_data);
    rc_var_data_init(&data->var_data);
    rc_skill_data_init(&data->skill_data);
    rc_shop_data_init(&data->shop_data);
    rc_quest_data_init(&data->quest_data);
    rc_dialogue_data_init(&data->dialogue_data);

    uint32_t npc_users = RC_SUB_COMBAT | RC_SUB_DIALOGUE | RC_SUB_SHOPS
                       | RC_SUB_SLAYER | RC_SUB_ENCOUNTER | RC_SUB_STORAGE;
    if ((cfg->subsystems & npc_users) && cfg->npc_defs_path) {
        int loaded = rc_load_npc_defs_into(
            cfg->npc_defs_path, data->npc_defs, RC_MAX_NPC_DEFS,
            &data->npc_def_count, data->npc_def_by_id, RC_MAX_NPC_ID,
            &data->npc_transforms, &data->npc_transform_count);
        if (loaded < 0) {
            snprintf(report->message, sizeof(report->message),
                     "failed to load npc definitions from %.384s",
                     cfg->npc_defs_path);
            rc_game_data_release(data);
            return NULL;
        }
        if (!rc_npc_mirror_defs_to_globals(
                data->npc_defs, data->npc_def_count, data->npc_def_by_id,
                data->npc_transforms, data->npc_transform_count)) {
            snprintf(report->message, sizeof(report->message),
                     "failed to publish npc definitions");
            rc_game_data_release(data);
            return NULL;
        }
        rc_npc_use_defs(data->npc_defs, data->npc_def_count,
                        data->npc_def_by_id, data->npc_transforms,
                        data->npc_transform_count);
    }

    uint32_t item_users = RC_SUB_EQUIPMENT | RC_SUB_INVENTORY
                        | RC_SUB_CONSUMABLES | RC_SUB_LOOT
                        | RC_SUB_SKILLS | RC_SUB_SHOPS | RC_SUB_STORAGE;
    if ((cfg->subsystems & item_users) && cfg->items_path) {
        int loaded = rc_load_item_defs_into(
            cfg->items_path, data->item_defs, RC_MAX_ITEM_DEFS,
            &data->item_def_count);
        if (loaded < 0) {
            snprintf(report->message, sizeof(report->message),
                     "failed to load item definitions from %.384s",
                     cfg->items_path);
            rc_game_data_release(data);
            return NULL;
        }
        rc_mirror_item_defs_to_globals(data);
        rc_item_use_defs(data->item_defs, data->item_def_count);
    }

    uint32_t normalization_users = npc_users | item_users | RC_SUB_LOOT
                                 | RC_SUB_SHOPS | RC_SUB_STORAGE;
    if ((cfg->subsystems & normalization_users) && cfg->normalization_path) {
        int loaded = rc_load_normalization_into(
            cfg->normalization_path, data->item_normalization,
            RC_MAX_ITEM_DEFS, &data->item_normalization_count,
            data->npc_normalization, RC_MAX_NPC_ID,
            &data->npc_normalization_count, &data->source_normalization,
            &data->source_normalization_count);
        if (loaded < 0) {
            snprintf(report->message, sizeof(report->message),
                     "failed to load normalization from %.384s",
                     cfg->normalization_path);
            rc_game_data_release(data);
            return NULL;
        }
        if (!rc_mirror_normalization_to_globals(data)) {
            rc_load_report_message(report,
                                   "failed to mirror normalization globals");
            rc_game_data_release(data);
            return NULL;
        }
        rc_normalization_use_defs(data->item_normalization,
                                  data->item_normalization_count,
                                  data->npc_normalization,
                                  data->npc_normalization_count,
                                  data->source_normalization,
                                  data->source_normalization_count);
    }

    if (cfg->varbits_path) {
        int loaded = rc_load_varbits_into(cfg->varbits_path, &data->var_data);
        if (loaded < 0) {
            snprintf(report->message, sizeof(report->message),
                     "failed to load varbits from %.384s",
                     cfg->varbits_path);
            rc_game_data_release(data);
            return NULL;
        }
        rc_var_data_mirror_to_globals(&data->var_data);
        rc_varbits_use_data(&data->var_data);
    }
    if (cfg->varps_path) {
        int loaded = rc_load_varps_into(cfg->varps_path, &data->var_data);
        if (loaded < 0) {
            snprintf(report->message, sizeof(report->message),
                     "failed to load varps from %.384s", cfg->varps_path);
            rc_game_data_release(data);
            return NULL;
        }
        rc_var_data_mirror_to_globals(&data->var_data);
        rc_varbits_use_data(&data->var_data);
    }

    if (cfg->subsystems & RC_SUB_LOOT) {
        if (cfg->drops_path
                && rc_load_drops_into(cfg->drops_path,
                                      &data->drop_data) < 0) {
            snprintf(report->message, sizeof(report->message),
                     "failed to load drop tables from %.384s",
                     cfg->drops_path);
            rc_game_data_release(data);
            return NULL;
        }
        if (cfg->rdt_path && rc_load_rdt_into(cfg->rdt_path,
                                              &data->drop_data) < 0) {
            snprintf(report->message, sizeof(report->message),
                     "failed to load rare drop table from %.384s",
                     cfg->rdt_path);
            rc_game_data_release(data);
            return NULL;
        }
        if (cfg->gdt_path && rc_load_gdt_into(cfg->gdt_path,
                                              &data->drop_data) < 0) {
            snprintf(report->message, sizeof(report->message),
                     "failed to load gem drop table from %.384s",
                     cfg->gdt_path);
            rc_game_data_release(data);
            return NULL;
        }
        if (cfg->mrdt_path && rc_load_mrdt_into(cfg->mrdt_path,
                                                &data->drop_data) < 0) {
            snprintf(report->message, sizeof(report->message),
                     "failed to load mega rare drop table from %.384s",
                     cfg->mrdt_path);
            rc_game_data_release(data);
            return NULL;
        }
        if (!rc_drops_mirror_to_globals(&data->drop_data)) {
            rc_load_report_message(report, "failed to mirror drop globals");
            rc_game_data_release(data);
            return NULL;
        }
        rc_drops_use_data(&data->drop_data);
    }
    if ((cfg->subsystems & (RC_SUB_LOOT | RC_SUB_SKILLS))
            && cfg->skill_drops_path) {
        int loaded = rc_load_skill_drops_into(cfg->skill_drops_path,
                                              &data->skill_data);
        if (loaded < 0) {
            snprintf(report->message, sizeof(report->message),
                     "failed to load skill drops from %.384s",
                     cfg->skill_drops_path);
            rc_game_data_release(data);
            return NULL;
        }
        if (!rc_skills_mirror_to_globals(&data->skill_data)) {
            rc_load_report_message(report, "failed to mirror skill globals");
            rc_game_data_release(data);
            return NULL;
        }
        rc_skills_use_data(&data->skill_data);
    }
    if (cfg->subsystems & RC_SUB_SKILLS) {
        if (cfg->recipes_path) {
            int loaded = rc_load_recipes_into(cfg->recipes_path,
                                              &data->skill_data);
            if (loaded < 0) {
                snprintf(report->message, sizeof(report->message),
                         "failed to load recipes from %.384s",
                         cfg->recipes_path);
                rc_game_data_release(data);
                return NULL;
            }
        }
        if (cfg->gathering_nodes_path) {
            int loaded = rc_load_gathering_nodes_into(
                cfg->gathering_nodes_path, &data->skill_data);
            if (loaded < 0) {
                snprintf(report->message, sizeof(report->message),
                         "failed to load gathering nodes from %.384s",
                         cfg->gathering_nodes_path);
                rc_game_data_release(data);
                return NULL;
            }
        }
        if (!rc_skills_mirror_to_globals(&data->skill_data)) {
            rc_load_report_message(report, "failed to mirror skill globals");
            rc_game_data_release(data);
            return NULL;
        }
        rc_skills_use_data(&data->skill_data);
    }
    if ((cfg->subsystems & RC_SUB_PRAYER) && cfg->prayers_path) {
        int loaded = rc_load_prayers_into(cfg->prayers_path, data->prayer_defs,
                                          RC_MAX_PRAYER_DEFS,
                                          &data->prayer_count);
        if (loaded < 0) {
            snprintf(report->message, sizeof(report->message),
                     "failed to load prayers from %.384s",
                     cfg->prayers_path);
            rc_game_data_release(data);
            return NULL;
        }
        memcpy(g_rc_prayer_defs, data->prayer_defs, sizeof(g_rc_prayer_defs));
        g_rc_prayer_count = data->prayer_count;
        rc_prayer_use_defs(data->prayer_defs, data->prayer_count);
    }
    if ((cfg->subsystems & RC_SUB_COMBAT) && cfg->spells_path) {
        int loaded = rc_load_spells_into(cfg->spells_path, data->spell_defs,
                                         RC_MAX_SPELL_DEFS,
                                         &data->spell_count);
        if (loaded < 0) {
            snprintf(report->message, sizeof(report->message),
                     "failed to load spells from %.384s",
                     cfg->spells_path);
            rc_game_data_release(data);
            return NULL;
        }
        if (data->item_def_count > 0) {
            for (int i = 0; i < loaded; i++) {
                const RcSpellDef *spell = &data->spell_defs[i];
                for (int r = 0; r < spell->rune_count; r++) {
                    uint32_t id = spell->runes[r].item_id;
                    // Utility costs can include non-stackable ingredients such as orbs.
                    if (id >= RC_MAX_ITEM_DEFS || !data->item_defs[id].loaded ||
                        data->item_defs[id].noted) {
                        snprintf(report->message, sizeof(report->message),
                            "spell %s has missing or invalid rune item %u", spell->name, id);
                        rc_game_data_release(data);
                        return NULL;
                    }
                }
            }
        }
        memcpy(g_rc_spell_defs, data->spell_defs, sizeof(g_rc_spell_defs));
        g_rc_spell_count = data->spell_count;
        rc_spell_use_defs(data->spell_defs, data->spell_count);
    }
    if ((cfg->subsystems & RC_SUB_SHOPS) && cfg->shops_path) {
        int loaded = rc_load_shops_into(cfg->shops_path, &data->shop_data);
        if (loaded < 0) {
            snprintf(report->message, sizeof(report->message),
                     "failed to load shops from %.384s", cfg->shops_path);
            rc_game_data_release(data);
            return NULL;
        }
        if (!rc_shops_mirror_to_globals(&data->shop_data)) {
            rc_load_report_message(report, "failed to mirror shop globals");
            rc_game_data_release(data);
            return NULL;
        }
        rc_shops_use_data(&data->shop_data);
    }
    if ((cfg->subsystems & RC_SUB_QUESTS) && cfg->quests_path) {
        int loaded = rc_load_quests_into(cfg->quests_path, &data->quest_data);
        if (loaded < 0) {
            snprintf(report->message, sizeof(report->message),
                     "failed to load quests from %.384s", cfg->quests_path);
            rc_game_data_release(data);
            return NULL;
        }
        if (!rc_quests_mirror_to_globals(&data->quest_data)) {
            rc_load_report_message(report, "failed to mirror quest globals");
            rc_game_data_release(data);
            return NULL;
        }
        rc_quests_use_data(&data->quest_data);
    }
    if ((cfg->subsystems & RC_SUB_DIALOGUE) && cfg->dialogue_path) {
        int loaded = rc_load_dialogue_into(cfg->dialogue_path,
                                           &data->dialogue_data);
        if (loaded < 0) {
            snprintf(report->message, sizeof(report->message),
                     "failed to load dialogue from %.384s",
                     cfg->dialogue_path);
            rc_game_data_release(data);
            return NULL;
        }
        if (!rc_dialogue_mirror_to_globals(&data->dialogue_data)) {
            rc_load_report_message(report, "failed to mirror dialogue globals");
            rc_game_data_release(data);
            return NULL;
        }
        rc_dialogue_use_data(&data->dialogue_data);
    }
    if (cfg->subsystems & RC_SUB_OBJECTS) {
        if (cfg->object_defs_path
                && rc_load_object_defs_into(cfg->object_defs_path,
                                            &data->object_data) < 0) {
            snprintf(report->message, sizeof(report->message),
                     "failed to load object definitions from %.384s",
                     cfg->object_defs_path);
            rc_game_data_release(data);
            return NULL;
        }
        if (cfg->object_behaviors_path
                && rc_load_object_behaviors_into(cfg->object_behaviors_path,
                                                 &data->object_data) < 0) {
            snprintf(report->message, sizeof(report->message),
                     "failed to load object behaviors from %.384s",
                     cfg->object_behaviors_path);
            rc_game_data_release(data);
            return NULL;
        }
        if (cfg->object_placements_path
                && rc_load_object_placements_into(cfg->object_placements_path,
                                                  &data->object_data) < 0) {
            snprintf(report->message, sizeof(report->message),
                     "failed to load object placements from %.384s",
                     cfg->object_placements_path);
            rc_game_data_release(data);
            return NULL;
        }
        if (!rc_objects_mirror_to_globals(&data->object_data)) {
            rc_load_report_message(report, "failed to mirror object globals");
            rc_game_data_release(data);
            return NULL;
        }
        rc_objects_use_data(&data->object_data);
    }
    if ((cfg->subsystems & RC_SUB_REGIONS) && cfg->collision_tiles_path) {
        if (rc_load_collision_tiles_into(cfg->collision_tiles_path,
                                         &data->collision_data) < 0) {
            snprintf(report->message, sizeof(report->message),
                     "failed to load collision tiles from %.384s",
                     cfg->collision_tiles_path);
            rc_game_data_release(data);
            return NULL;
        }
        if (!rc_collision_mirror_to_globals(&data->collision_data)) {
            rc_load_report_message(report, "failed to mirror collision globals");
            rc_game_data_release(data);
            return NULL;
        }
        rc_collision_use_data(&data->collision_data);
    }
    if ((cfg->subsystems & RC_SUB_REGIONS) && cfg->area_flags_path) {
        if (rc_load_area_flags_into(cfg->area_flags_path,
                                    &data->area_flag_data) < 0) {
            snprintf(report->message, sizeof(report->message),
                     "failed to load area flags from %.384s",
                     cfg->area_flags_path);
            rc_game_data_release(data);
            return NULL;
        }
        if (!rc_area_flags_mirror_to_globals(&data->area_flag_data)) {
            rc_load_report_message(report, "failed to mirror area globals");
            rc_game_data_release(data);
            return NULL;
        }
        rc_area_flags_use_data(&data->area_flag_data);
    }
    if ((cfg->subsystems & RC_SUB_TRAVERSAL) && cfg->traversal_edges_path) {
        if (rc_load_traversal_edges_into(cfg->traversal_edges_path,
                                         &data->traversal_data) < 0) {
            snprintf(report->message, sizeof(report->message),
                     "failed to load traversal edges from %.384s",
                     cfg->traversal_edges_path);
            rc_game_data_release(data);
            return NULL;
        }
        if (!rc_traversal_mirror_to_globals(&data->traversal_data)) {
            rc_load_report_message(report, "failed to mirror traversal globals");
            rc_game_data_release(data);
            return NULL;
        }
        rc_traversal_use_data(&data->traversal_data);
    }
    if (cfg->subsystems & RC_SUB_ENCOUNTER) {
        if (cfg->activity_schemas_path
                && rc_load_activity_schemas_into(cfg->activity_schemas_path,
                                                 &data->activity_schema_data)
                       < 0) {
            snprintf(report->message, sizeof(report->message),
                     "failed to load activity schemas from %.384s",
                     cfg->activity_schemas_path);
            rc_game_data_release(data);
            return NULL;
        }
        if (cfg->activity_spawns_path
                && rc_load_activity_spawns_into(cfg->activity_spawns_path,
                                                &data->activity_spawn_data)
                       < 0) {
            snprintf(report->message, sizeof(report->message),
                     "failed to load activity spawns from %.384s",
                     cfg->activity_spawns_path);
            rc_game_data_release(data);
            return NULL;
        }
        if (cfg->activity_mechanics_path
                && rc_load_activity_mechanics_into(
                    cfg->activity_mechanics_path,
                    &data->activity_mechanic_data) < 0) {
            snprintf(report->message, sizeof(report->message),
                     "failed to load activity mechanics from %.384s",
                     cfg->activity_mechanics_path);
            rc_game_data_release(data);
            return NULL;
        }
        if (cfg->activity_states_path
                && rc_load_activity_states_into(cfg->activity_states_path,
                                                &data->activity_state_data)
                       < 0) {
            snprintf(report->message, sizeof(report->message),
                     "failed to load activity states from %.384s",
                     cfg->activity_states_path);
            rc_game_data_release(data);
            return NULL;
        }
        if (!rc_activity_schemas_mirror_to_globals(
                &data->activity_schema_data)
                || !rc_activity_spawns_mirror_to_globals(
                    &data->activity_spawn_data)
                || !rc_activity_mechanics_mirror_to_globals(
                    &data->activity_mechanic_data)
                || !rc_activity_states_mirror_to_globals(
                    &data->activity_state_data)) {
            rc_load_report_message(report, "failed to mirror activity globals");
            rc_game_data_release(data);
            return NULL;
        }
        rc_activity_schemas_use_data(&data->activity_schema_data);
        rc_activity_spawns_use_data(&data->activity_spawn_data);
        rc_activity_mechanics_use_data(&data->activity_mechanic_data);
        rc_activity_states_use_data(&data->activity_state_data);
    }
    if ((cfg->subsystems & RC_SUB_ENCOUNTER) && cfg->encounters_path) {
        int loaded = rc_encounter_load_specs(cfg->encounters_path,
                                             data->encounter_specs,
                                             RC_ENC_REGISTRY_CAP);
        if (loaded < 0) {
            snprintf(report->message, sizeof(report->message),
                     "failed to load encounter specs from %.384s",
                     cfg->encounters_path);
            rc_game_data_release(data);
            return NULL;
        }
        char role_error[256];
        if (data->npc_def_count > 0
                && !rc_encounter_validate_roles(
                    data->encounter_specs, loaded, game_data_npc_name,
                    data->object_data.def_count > 0 ? game_data_object_name
                                                    : NULL,
                    data, role_error, sizeof(role_error))) {
            snprintf(report->message, sizeof(report->message), "%s", role_error);
            rc_game_data_release(data);
            return NULL;
        }
        data->encounter_spec_count = loaded;
    }
    if (cfg->player_actions_path
            && rc_load_player_actions(cfg->player_actions_path) < 0) {
        snprintf(report->message, sizeof(report->message),
                 "failed to load player actions from %.384s",
                 cfg->player_actions_path);
        rc_game_data_release(data);
        return NULL;
    }
    if ((cfg->subsystems & RC_SUB_COMBAT) && cfg->combat_profiles_path
            && rc_load_combat_profiles(cfg->combat_profiles_path) < 0) {
        snprintf(report->message, sizeof(report->message),
                 "failed to load combat profiles from %.384s",
                 cfg->combat_profiles_path);
        rc_game_data_release(data);
        return NULL;
    }
    uint32_t mechanics_users = RC_SUB_COMBAT | RC_SUB_SLAYER
                             | RC_SUB_ENCOUNTER;
    if ((cfg->subsystems & mechanics_users) && cfg->monster_mechanics_path
            && rc_load_monster_mechanics(cfg->monster_mechanics_path) < 0) {
        snprintf(report->message, sizeof(report->message),
                 "failed to load monster mechanics from %.384s",
                 cfg->monster_mechanics_path);
        rc_game_data_release(data);
        return NULL;
    }
    if ((cfg->subsystems & RC_SUB_SLAYER) && cfg->slayer_path
            && rc_load_slayer(cfg->slayer_path) < 0) {
        snprintf(report->message, sizeof(report->message),
                 "failed to load slayer data from %.384s", cfg->slayer_path);
        rc_game_data_release(data);
        return NULL;
    }
    data->stats = rc_collect_game_data_stats(cfg, data);
    data->stats.subsystems = rc_loaded_capabilities(cfg->subsystems, data);
    if (data->stats.subsystems != cfg->subsystems) {
        snprintf(report->message, sizeof(report->message),
                 "loaded data is missing subsystem capabilities 0x%08x",
                 cfg->subsystems & ~data->stats.subsystems);
        rc_game_data_release(data);
        return NULL;
    }
    data->stats.encounter_spec_count = data->encounter_spec_count;

    report->stats = data->stats;
    report->ok = 1;
    snprintf(report->message, sizeof(report->message),
             "game data loaded: npc_defs=%d items=%d prayers=%d spells=%d",
             data->stats.npc_def_count, data->stats.item_def_count,
             data->stats.prayer_count, data->stats.spell_count);
    rc_game_data_activate_views(data, cfg->subsystems);
    data->published = 1;
    data->owns_load_lock = 0;
    g_runtime_data = data;
    rc_runtime_data_unlock();
    return data;
}

void rc_game_data_retain(RcGameData *data) {
    if (!data) return;
    atomic_fetch_add_explicit(&data->ref_count, 1, memory_order_relaxed);
}

void rc_game_data_release(RcGameData *data) {
    if (!data) return;
    int load_lock_held = data->owns_load_lock;
    if (data->published) rc_runtime_data_lock();
    int previous = atomic_fetch_sub_explicit(&data->ref_count, 1,
                                             memory_order_acq_rel);
    if (previous != 1) {
        if (data->published) rc_runtime_data_unlock();
        return;
    }
    if (previous == 1) {
        int unlock_after_free = load_lock_held || data->published;
        if (data->published) {
            if (g_runtime_data == data) g_runtime_data = NULL;
            data->published = 0;
        } else if (data->owns_load_lock) {
            data->owns_load_lock = 0;
        }
        rc_item_reset_defs_if_active(data->item_defs);
        rc_npc_reset_defs_if_active(data->npc_defs);
        rc_drops_reset_data_if_active(&data->drop_data);
        rc_normalization_reset_defs_if_active(data->item_normalization,
                                              data->npc_normalization,
                                              data->source_normalization);
        rc_prayer_reset_defs_if_active(data->prayer_defs);
        rc_spell_reset_defs_if_active(data->spell_defs);
        rc_objects_reset_data_if_active(&data->object_data);
        rc_collision_reset_data_if_active(&data->collision_data);
        rc_area_flags_reset_data_if_active(&data->area_flag_data);
        rc_traversal_reset_data_if_active(&data->traversal_data);
        rc_activity_schemas_reset_data_if_active(
            &data->activity_schema_data);
        rc_activity_spawns_reset_data_if_active(&data->activity_spawn_data);
        rc_activity_mechanics_reset_data_if_active(
            &data->activity_mechanic_data);
        rc_activity_states_reset_data_if_active(&data->activity_state_data);
        rc_varbits_reset_data_if_active(&data->var_data);
        rc_skills_reset_data_if_active(&data->skill_data);
        rc_shops_reset_data_if_active(&data->shop_data);
        rc_quests_reset_data_if_active(&data->quest_data);
        rc_dialogue_reset_data_if_active(&data->dialogue_data);
        free(data->source_normalization);
        rc_drop_data_free(&data->drop_data);
        rc_object_data_free(&data->object_data);
        rc_collision_data_free(&data->collision_data);
        rc_area_flag_data_free(&data->area_flag_data);
        rc_traversal_data_free(&data->traversal_data);
        rc_activity_schema_data_free(&data->activity_schema_data);
        rc_activity_spawn_data_free(&data->activity_spawn_data);
        rc_activity_mechanic_data_free(&data->activity_mechanic_data);
        rc_activity_state_data_free(&data->activity_state_data);
        rc_skill_data_free(&data->skill_data);
        rc_shop_data_free(&data->shop_data);
        rc_quest_data_free(&data->quest_data);
        rc_dialogue_data_free(&data->dialogue_data);
        free(data->npc_transforms);
        free(data->identity);
        free(data);
        if (unlock_after_free) rc_runtime_data_unlock();
    }
}

uint32_t rc_game_data_subsystems(const RcGameData *data) {
    return data ? data->stats.subsystems : 0;
}

const RcGameDataStats *rc_game_data_stats(const RcGameData *data) {
    return data ? &data->stats : NULL;
}

static void rc_game_data_activate_views(const RcGameData *data,
                                        uint32_t subsystems) {
    if (!data) return;
    uint32_t npc_users = RC_SUB_COMBAT | RC_SUB_DIALOGUE | RC_SUB_SHOPS
                       | RC_SUB_SLAYER | RC_SUB_ENCOUNTER | RC_SUB_STORAGE;
    uint32_t item_users = RC_SUB_EQUIPMENT | RC_SUB_INVENTORY
                        | RC_SUB_CONSUMABLES | RC_SUB_LOOT
                        | RC_SUB_SKILLS | RC_SUB_SHOPS | RC_SUB_STORAGE;
    uint32_t normalization_users = npc_users | item_users | RC_SUB_LOOT
                                 | RC_SUB_SHOPS | RC_SUB_STORAGE;
    if (subsystems & npc_users) {
        rc_npc_use_defs(data->npc_defs, data->npc_def_count,
                        data->npc_def_by_id, data->npc_transforms,
                        data->npc_transform_count);
    }
    if (subsystems & item_users) {
        rc_item_use_defs(data->item_defs, data->item_def_count);
    }
    if (subsystems & RC_SUB_LOOT) {
        rc_drops_use_data(&data->drop_data);
    }
    if (subsystems & normalization_users) {
        rc_normalization_use_defs(data->item_normalization,
                                  data->item_normalization_count,
                                  data->npc_normalization,
                                  data->npc_normalization_count,
                                  data->source_normalization,
                                  data->source_normalization_count);
    }
    if (subsystems & RC_SUB_PRAYER) {
        rc_prayer_use_defs(data->prayer_defs, data->prayer_count);
    }
    if (subsystems & RC_SUB_COMBAT) {
        rc_spell_use_defs(data->spell_defs, data->spell_count);
    }
    if (subsystems & RC_SUB_OBJECTS) {
        rc_objects_use_data(&data->object_data);
    }
    if (subsystems & RC_SUB_REGIONS) {
        rc_collision_use_data(&data->collision_data);
        rc_area_flags_use_data(&data->area_flag_data);
    }
    if (subsystems & RC_SUB_TRAVERSAL) {
        rc_traversal_use_data(&data->traversal_data);
    }
    if (subsystems & RC_SUB_ENCOUNTER) {
        rc_activity_schemas_use_data(&data->activity_schema_data);
        rc_activity_spawns_use_data(&data->activity_spawn_data);
        rc_activity_mechanics_use_data(&data->activity_mechanic_data);
        rc_activity_states_use_data(&data->activity_state_data);
    }
    if (data->var_data.varbit_count > 0 || data->var_data.varp_count > 0) {
        rc_varbits_use_data(&data->var_data);
    }
    if (subsystems & (RC_SUB_LOOT | RC_SUB_SKILLS)) {
        rc_skills_use_data(&data->skill_data);
    }
    if (subsystems & RC_SUB_SHOPS) {
        rc_shops_use_data(&data->shop_data);
    }
    if (subsystems & RC_SUB_QUESTS) {
        rc_quests_use_data(&data->quest_data);
    }
    if (subsystems & RC_SUB_DIALOGUE) {
        rc_dialogue_use_data(&data->dialogue_data);
    }
}

const RcNpcDef *rc_game_data_npc_defs(const RcGameData *data, int *count) {
    if (count) *count = data ? data->npc_def_count : 0;
    return data && data->npc_def_count > 0 ? data->npc_defs : NULL;
}

const RcItemDef *rc_game_data_item_defs(const RcGameData *data, int *count) {
    if (count) *count = data ? data->item_def_count : 0;
    return data && data->item_def_count > 0 ? data->item_defs : NULL;
}

const RcDropData *rc_game_data_drop_data(const RcGameData *data) {
    return data ? &data->drop_data : NULL;
}

const RcObjectData *rc_game_data_object_data(const RcGameData *data) {
    return data ? &data->object_data : NULL;
}

const RcCollisionData *rc_game_data_collision_data(const RcGameData *data) {
    return data ? &data->collision_data : NULL;
}

const RcAreaFlagData *rc_game_data_area_flag_data(const RcGameData *data) {
    return data ? &data->area_flag_data : NULL;
}

const RcTraversalData *rc_game_data_traversal_data(const RcGameData *data) {
    return data ? &data->traversal_data : NULL;
}

const RcActivitySchemaData *rc_game_data_activity_schema_data(
    const RcGameData *data) {
    return data ? &data->activity_schema_data : NULL;
}

const RcActivitySpawnData *rc_game_data_activity_spawn_data(
    const RcGameData *data) {
    return data ? &data->activity_spawn_data : NULL;
}

const RcActivityMechanicData *rc_game_data_activity_mechanic_data(
    const RcGameData *data) {
    return data ? &data->activity_mechanic_data : NULL;
}

const RcActivityStateData *rc_game_data_activity_state_data(
    const RcGameData *data) {
    return data ? &data->activity_state_data : NULL;
}

const RcItemNormalization *rc_game_data_item_normalization(
    const RcGameData *data, int *count) {
    if (count) *count = data ? data->item_normalization_count : 0;
    return data && data->item_normalization_count > 0
         ? data->item_normalization : NULL;
}

const RcNpcNormalization *rc_game_data_npc_normalization(
    const RcGameData *data, int *count) {
    if (count) *count = data ? data->npc_normalization_count : 0;
    return data && data->npc_normalization_count > 0
         ? data->npc_normalization : NULL;
}

const RcSourceNormalization *rc_game_data_source_normalization(
    const RcGameData *data, int *count) {
    if (count) *count = data ? data->source_normalization_count : 0;
    return data && data->source_normalization_count > 0
         ? data->source_normalization : NULL;
}

const RcPrayerDef *rc_game_data_prayer_defs(const RcGameData *data,
                                            int *count) {
    if (count) *count = data ? data->prayer_count : 0;
    return data && data->prayer_count > 0 ? data->prayer_defs : NULL;
}

const RcSpellDef *rc_game_data_spell_defs(const RcGameData *data,
                                          int *count) {
    if (count) *count = data ? data->spell_count : 0;
    return data && data->spell_count > 0 ? data->spell_defs : NULL;
}

const RcEncounterSpec *rc_game_data_encounter_specs(const RcGameData *data,
                                                    int *count) {
    if (count) *count = data ? data->encounter_spec_count : 0;
    return data && data->encounter_spec_count > 0 ? data->encounter_specs
                                                  : NULL;
}

const RcVarData *rc_game_data_var_data(const RcGameData *data) {
    return data ? &data->var_data : NULL;
}

const RcSkillData *rc_game_data_skill_data(const RcGameData *data) {
    return data ? &data->skill_data : NULL;
}

const RcShopData *rc_game_data_shop_data(const RcGameData *data) {
    return data ? &data->shop_data : NULL;
}

const RcQuestData *rc_game_data_quest_data(const RcGameData *data) {
    return data ? &data->quest_data : NULL;
}

const RcDialogueData *rc_game_data_dialogue_data(const RcGameData *data) {
    return data ? &data->dialogue_data : NULL;
}
