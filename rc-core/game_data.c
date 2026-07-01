#include "game_data.h"

#include "assets.h"
#include "activity_mechanics.h"
#include "activity_schemas.h"
#include "activity_spawns.h"
#include "activity_states.h"
#include "area_flags.h"
#include "collision.h"
#include "dialogue.h"
#include "drops.h"
#include "encounter.h"
#include "items.h"
#include "normalization.h"
#include "npc.h"
#include "objects.h"
#include "prayer.h"
#include "quests.h"
#include "shops.h"
#include "skills.h"
#include "spells.h"
#include "traversal.h"
#include "varbits.h"

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define RC_VALIDATION_MAX_REQUIRED 512

typedef struct {
    char **items;
    size_t count;
} RcStringList;

struct RcGameData {
    int ref_count;
    RcGameDataStats stats;
    RcNpcDef npc_defs[RC_MAX_NPC_DEFS];
    int npc_def_count;
    int npc_def_by_id[RC_MAX_NPC_ID];
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

static const char *rc_find_json_key(const char *json, const char *key) {
    char needle[256];
    int n = snprintf(needle, sizeof(needle), "\"%s\"", key);
    if (n <= 0 || (size_t)n >= sizeof(needle)) return NULL;
    return strstr(json, needle);
}

static int rc_json_key_string_equals(const char *json, const char *key,
                                     const char *value) {
    const char *p = json;
    while ((p = rc_find_json_key(p, key)) != NULL) {
        p = strchr(p, ':');
        if (!p) return 0;
        p = rc_skip_ws(p + 1);
        if (*p == '"') {
            p++;
            size_t value_len = strlen(value);
            if (strncmp(p, value, value_len) == 0 && p[value_len] == '"')
                return 1;
        }
        p++;
    }
    return 0;
}

static int rc_json_next_key_string(const char **cursor, const char *key,
                                   char *out, size_t cap) {
    const char *p = *cursor;
    while ((p = rc_find_json_key(p, key)) != NULL) {
        p = strchr(p, ':');
        if (!p) return 0;
        p = rc_skip_ws(p + 1);
        if (*p != '"') {
            p++;
            continue;
        }
        p++;
        const char *start = p;
        while (*p && *p != '"') {
            if (*p == '\\' && p[1]) p++;
            p++;
        }
        if (*p != '"') return 0;
        size_t len = (size_t)(p - start);
        if (out && cap > 0) {
            size_t copy_len = len < cap - 1 ? len : cap - 1;
            memcpy(out, start, copy_len);
            out[copy_len] = '\0';
        }
        *cursor = p + 1;
        return 1;
    }
    return 0;
}

static int rc_json_string_array(const char *json, const char *key,
                                RcStringList *out) {
    memset(out, 0, sizeof(*out));
    const char *p = rc_find_json_key(json, key);
    if (!p) return 0;
    p = strchr(p, ':');
    if (!p) return 0;
    p = rc_skip_ws(p + 1);
    if (*p != '[') return 0;
    p++;

    char **items = NULL;
    size_t count = 0;
    while (*p) {
        p = rc_skip_ws(p);
        if (*p == ']') {
            out->items = items;
            out->count = count;
            return 1;
        }
        if (*p == ',') {
            p++;
            continue;
        }
        if (*p != '"' || count >= RC_VALIDATION_MAX_REQUIRED) break;
        p++;
        const char *start = p;
        while (*p && *p != '"') {
            if (*p == '\\' && p[1]) p++;
            p++;
        }
        if (*p != '"') break;
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

static int rc_manifest_has_asset_path(const char *manifest, const char *logical) {
    return rc_json_key_string_equals(manifest, "path", logical);
}

static void rc_manifest_pack_file_status(const char *manifest,
                                         const char *pack_dir,
                                         int *pack_count,
                                         int *missing_count,
                                         char *first_missing,
                                         size_t first_missing_cap) {
    if (pack_count) *pack_count = 0;
    if (missing_count) *missing_count = 0;
    if (first_missing && first_missing_cap > 0) first_missing[0] = '\0';

    const char *cursor = manifest;
    char value[1024];
    while (rc_json_next_key_string(&cursor, "path", value, sizeof(value))) {
        if (strncmp(value, "packs/", 6) != 0 || !rc_has_suffix(value, ".pak"))
            continue;
        if (pack_count) (*pack_count)++;
        const char *name = value + 6;
        char path[2048];
        int exists = rc_join(path, sizeof(path), pack_dir, name)
                  && rc_file_exists(path);
        if (!exists) {
            if (missing_count) (*missing_count)++;
            if (first_missing && first_missing_cap > 0 && !first_missing[0])
                snprintf(first_missing, first_missing_cap, "%s", value);
        }
    }
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

    report->manifest_format_ok = rc_json_key_string_equals(
        manifest, "format", "runec-data-manifest-v1");
    report->data_version_present = rc_find_json_key(manifest, "data_version") != NULL;
    if (!report->manifest_format_ok || !report->data_version_present) {
        free(manifest);
        rc_report_message(report, "runtime data manifest is missing required identity fields");
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

    char first_missing_pack[512];
    rc_manifest_pack_file_status(manifest, pack_dir,
                                 &report->pack_file_count,
                                 &report->missing_pack_files,
                                 first_missing_pack,
                                 sizeof(first_missing_pack));
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
        int pack_ok = pack_available && rc_manifest_has_asset_path(manifest, logical);
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
                 report->missing_pack_files, first_missing_pack);
        rc_string_list_free(&required);
        free(manifest);
        return 0;
    }
    if (report->missing_required_paths > 0) {
        snprintf(report->message, sizeof(report->message),
                 "runtime data missing %d required path(s); first: %.256s",
                 report->missing_required_paths, first_missing);
        rc_string_list_free(&required);
        free(manifest);
        return 0;
    }
    if (report->blocked_provenance_count > 0 && !allow_blocked) {
        snprintf(report->message, sizeof(report->message),
                 "runtime data install contains %d blocked-source provenance marker(s); "
                 "regenerate from approved sources or set RUNEC_ALLOW_BLOCKED_SOURCE_PROVENANCE=1 for dev-only investigation",
                 report->blocked_provenance_count);
        rc_string_list_free(&required);
        free(manifest);
        return 0;
    }

    report->ok = 1;
    snprintf(report->message, sizeof(report->message),
             "runtime data install valid: %d required path(s)",
             report->required_path_count);
    rc_string_list_free(&required);
    free(manifest);
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
    if (!rc_game_data_validate_install(NULL, &validation)) {
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

static void rc_mirror_npc_defs_to_globals(const RcGameData *data) {
    if (!data) return;
    memcpy(g_npc_defs, data->npc_defs, sizeof(g_npc_defs));
    g_npc_def_count = data->npc_def_count;
}

static void rc_mirror_item_defs_to_globals(const RcGameData *data) {
    if (!data) return;
    memcpy(g_item_defs, data->item_defs, sizeof(g_item_defs));
    g_item_def_count = data->item_def_count;
}

static void rc_game_data_build_npc_index(RcGameData *data) {
    if (!data) return;
    for (int i = 0; i < RC_MAX_NPC_ID; i++) data->npc_def_by_id[i] = -1;
    for (int i = 0; i < data->npc_def_count; i++) {
        int npc_id = data->npc_defs[i].id;
        if (npc_id >= 0 && npc_id < RC_MAX_NPC_ID) {
            data->npc_def_by_id[npc_id] = i;
        }
    }
}

static RcGameDataStats rc_collect_game_data_stats(const RcWorldConfig *cfg,
                                                  const RcGameData *data) {
    uint32_t subsystems = cfg->subsystems;
    uint32_t npc_users = RC_SUB_COMBAT | RC_SUB_DIALOGUE | RC_SUB_SHOPS
                       | RC_SUB_SLAYER | RC_SUB_ENCOUNTER;
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
        .object_transport_count = needs_objects && data
                                ? data->object_data.transport_count : 0,
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

RcGameData *rc_game_data_load(const RcWorldConfig *cfg,
                              RcGameDataLoadReport *out) {
    RcGameDataLoadReport local;
    RcGameDataLoadReport *report = out ? out : &local;
    rc_load_report_init(report);

    if (!cfg) {
        rc_load_report_message(report, "missing RcWorldConfig");
        return NULL;
    }
    if (!rc_validate_before_load(report)) {
        return NULL;
    }

    RcGameData *data = (RcGameData *)calloc(1, sizeof(*data));
    if (!data) {
        rc_load_report_message(report, "failed to allocate RcGameData");
        return NULL;
    }
    data->ref_count = 1;
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
                       | RC_SUB_SLAYER | RC_SUB_ENCOUNTER;
    if ((cfg->subsystems & npc_users) && cfg->npc_defs_path) {
        if (g_npc_def_count > 0) {
            memcpy(data->npc_defs, g_npc_defs, sizeof(data->npc_defs));
            data->npc_def_count = g_npc_def_count;
            rc_game_data_build_npc_index(data);
        } else {
            int loaded = rc_load_npc_defs_into(
                cfg->npc_defs_path, data->npc_defs, RC_MAX_NPC_DEFS,
                &data->npc_def_count, data->npc_def_by_id, RC_MAX_NPC_ID);
            if (loaded < 0) {
                snprintf(report->message, sizeof(report->message),
                         "failed to load npc definitions from %.384s",
                         cfg->npc_defs_path);
                rc_game_data_release(data);
                return NULL;
            }
            rc_mirror_npc_defs_to_globals(data);
        }
        rc_npc_use_defs(data->npc_defs, data->npc_def_count,
                        data->npc_def_by_id);
    }

    uint32_t item_users = RC_SUB_EQUIPMENT | RC_SUB_INVENTORY
                        | RC_SUB_CONSUMABLES | RC_SUB_LOOT
                        | RC_SUB_SKILLS | RC_SUB_SHOPS | RC_SUB_STORAGE;
    if ((cfg->subsystems & item_users) && cfg->items_path) {
        if (g_item_def_count > 0) {
            memcpy(data->item_defs, g_item_defs, sizeof(data->item_defs));
            data->item_def_count = g_item_def_count;
        } else {
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
        }
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
    int needs_skill_data = (cfg->subsystems & (RC_SUB_LOOT | RC_SUB_SKILLS)) != 0;
    int had_skill_globals = g_rc_recipe_count > 0
                          || g_rc_skill_drop_source_count > 0
                          || g_rc_skill_drop_count > 0
                          || g_rc_gathering_node_count > 0;
    if (needs_skill_data && had_skill_globals
            && !rc_skill_data_import_globals(&data->skill_data)) {
        rc_load_report_message(report, "failed to import skill globals");
        rc_game_data_release(data);
        return NULL;
    }
    if ((cfg->subsystems & (RC_SUB_LOOT | RC_SUB_SKILLS))
            && cfg->skill_drops_path) {
        if (data->skill_data.drop_source_count == 0) {
            int loaded = rc_load_skill_drops_into(cfg->skill_drops_path,
                                                  &data->skill_data);
            if (loaded < 0) {
                snprintf(report->message, sizeof(report->message),
                         "failed to load skill drops from %.384s",
                         cfg->skill_drops_path);
                rc_game_data_release(data);
                return NULL;
            }
        }
        if (!had_skill_globals
                && !rc_skills_mirror_to_globals(&data->skill_data)) {
            rc_load_report_message(report, "failed to mirror skill globals");
            rc_game_data_release(data);
            return NULL;
        }
        rc_skills_use_data(&data->skill_data);
    }
    if (cfg->subsystems & RC_SUB_SKILLS) {
        if (cfg->recipes_path && data->skill_data.recipe_count == 0) {
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
        if (cfg->gathering_nodes_path
                && data->skill_data.gathering_node_count == 0) {
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
        if (!had_skill_globals
                && !rc_skills_mirror_to_globals(&data->skill_data)) {
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
        if ((g_rc_object_def_count > 0 || g_rc_object_behavior_count > 0
                || g_rc_object_placement_count > 0
                || g_rc_object_transport_count > 0
                || g_rc_object_param_count > 0)
                && !rc_object_data_import_globals(&data->object_data)) {
            rc_load_report_message(report, "failed to import object globals");
            rc_game_data_release(data);
            return NULL;
        }
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
        if (cfg->object_transports_path
                && rc_load_object_transports_into(cfg->object_transports_path,
                                                  &data->object_data) < 0) {
            snprintf(report->message, sizeof(report->message),
                     "failed to load object transports from %.384s",
                     cfg->object_transports_path);
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
        if (g_rc_activity_schema_count > 0) {
            if (!rc_activity_schema_data_import_globals(
                    &data->activity_schema_data)) {
                rc_load_report_message(report,
                                       "failed to import activity schemas");
                rc_game_data_release(data);
                return NULL;
            }
        } else if (cfg->activity_schemas_path
                && rc_load_activity_schemas_into(cfg->activity_schemas_path,
                                                 &data->activity_schema_data)
                       < 0) {
            snprintf(report->message, sizeof(report->message),
                     "failed to load activity schemas from %.384s",
                     cfg->activity_schemas_path);
            rc_game_data_release(data);
            return NULL;
        }
        if (g_rc_activity_spawn_count > 0) {
            if (!rc_activity_spawn_data_import_globals(
                    &data->activity_spawn_data)) {
                rc_load_report_message(report,
                                       "failed to import activity spawns");
                rc_game_data_release(data);
                return NULL;
            }
        } else if (cfg->activity_spawns_path
                && rc_load_activity_spawns_into(cfg->activity_spawns_path,
                                                &data->activity_spawn_data)
                       < 0) {
            snprintf(report->message, sizeof(report->message),
                     "failed to load activity spawns from %.384s",
                     cfg->activity_spawns_path);
            rc_game_data_release(data);
            return NULL;
        }
        if (g_rc_activity_mechanic_count > 0) {
            if (!rc_activity_mechanic_data_import_globals(
                    &data->activity_mechanic_data)) {
                rc_load_report_message(report,
                                       "failed to import activity mechanics");
                rc_game_data_release(data);
                return NULL;
            }
        } else if (cfg->activity_mechanics_path
                && rc_load_activity_mechanics_into(
                    cfg->activity_mechanics_path,
                    &data->activity_mechanic_data) < 0) {
            snprintf(report->message, sizeof(report->message),
                     "failed to load activity mechanics from %.384s",
                     cfg->activity_mechanics_path);
            rc_game_data_release(data);
            return NULL;
        }
        if (g_rc_activity_state_count > 0) {
            if (!rc_activity_state_data_import_globals(
                    &data->activity_state_data)) {
                rc_load_report_message(report,
                                       "failed to import activity states");
                rc_game_data_release(data);
                return NULL;
            }
        } else if (cfg->activity_states_path
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
        data->encounter_spec_count = loaded;
    }
    data->stats = rc_collect_game_data_stats(cfg, data);
    data->stats.encounter_spec_count = data->encounter_spec_count;

    report->stats = data->stats;
    report->ok = 1;
    snprintf(report->message, sizeof(report->message),
             "game data loaded: npc_defs=%d items=%d prayers=%d spells=%d",
             data->stats.npc_def_count, data->stats.item_def_count,
             data->stats.prayer_count, data->stats.spell_count);
    return data;
}

void rc_game_data_retain(RcGameData *data) {
    if (!data) return;
    data->ref_count++;
}

void rc_game_data_release(RcGameData *data) {
    if (!data) return;
    data->ref_count--;
    if (data->ref_count <= 0) {
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
        free(data);
    }
}

uint32_t rc_game_data_subsystems(const RcGameData *data) {
    return data ? data->stats.subsystems : 0;
}

const RcGameDataStats *rc_game_data_stats(const RcGameData *data) {
    return data ? &data->stats : NULL;
}

void rc_game_data_activate_views(const RcGameData *data,
                                 uint32_t subsystems) {
    if (!data) return;
    uint32_t npc_users = RC_SUB_COMBAT | RC_SUB_DIALOGUE | RC_SUB_SHOPS
                       | RC_SUB_SLAYER | RC_SUB_ENCOUNTER;
    uint32_t item_users = RC_SUB_EQUIPMENT | RC_SUB_INVENTORY
                        | RC_SUB_CONSUMABLES | RC_SUB_LOOT
                        | RC_SUB_SKILLS | RC_SUB_SHOPS | RC_SUB_STORAGE;
    uint32_t normalization_users = npc_users | item_users | RC_SUB_LOOT
                                 | RC_SUB_SHOPS | RC_SUB_STORAGE;
    if (subsystems & npc_users) {
        rc_npc_use_defs(data->npc_defs, data->npc_def_count,
                        data->npc_def_by_id);
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
