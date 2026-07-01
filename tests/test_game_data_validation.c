#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "game_data.h"

static void write_file(const char *path, const char *text) {
    FILE *f = fopen(path, "wb");
    assert(f != NULL);
    assert(fputs(text, f) >= 0);
    assert(fclose(f) == 0);
}

static void make_dir(const char *path) {
    assert(mkdir(path, 0755) == 0);
}

static void make_fixture_root(char *out, size_t cap) {
    snprintf(out, cap, "/tmp/runec_game_data_validation_XXXXXX");
    assert(mkdtemp(out) != NULL);
}

static void create_minimal_manifest(const char *root, const char *extra) {
    char defs[512];
    char npc_defs[512];
    char manifest[512];
    snprintf(defs, sizeof(defs), "%s/defs", root);
    snprintf(npc_defs, sizeof(npc_defs), "%s/defs/npc_defs.bin", root);
    snprintf(manifest, sizeof(manifest), "%s/manifest.json", root);
    make_dir(defs);
    write_file(npc_defs, "test");

    FILE *f = fopen(manifest, "wb");
    assert(f != NULL);
    fprintf(f,
            "{\n"
            "  \"format\": \"runec-data-manifest-v1\",\n"
            "  \"data_version\": \"test\",\n"
            "  \"required_logical_paths\": [\"defs/npc_defs.bin\"],\n"
            "  \"assets\": [{\"path\": \"defs/npc_defs.bin\"}]%s\n"
            "}\n",
            extra ? extra : "");
    assert(fclose(f) == 0);
}

int main(void) {
    RcGameDataValidationReport report;

    assert(rc_game_data_validate_install(RC_TEST_SOURCE_DIR "/data", &report) == 1);
    assert(report.ok == 1);
    assert(report.manifest_present == 1);
    assert(report.required_path_count > 0);

    char missing_root[128];
    make_fixture_root(missing_root, sizeof(missing_root));
    char missing_manifest[256];
    snprintf(missing_manifest, sizeof(missing_manifest), "%s/manifest.json",
             missing_root);
    write_file(missing_manifest,
               "{\n"
               "  \"format\": \"runec-data-manifest-v1\",\n"
               "  \"data_version\": \"test\",\n"
               "  \"required_logical_paths\": [\"defs/npc_defs.bin\"],\n"
               "  \"assets\": [{\"path\": \"defs/npc_defs.bin\"}]\n"
               "}\n");
    assert(rc_game_data_validate_install(missing_root, &report) == 0);
    assert(report.missing_required_paths == 1);
    assert(strstr(report.message, "defs/npc_defs.bin") != NULL);

    char pack_root[128];
    make_fixture_root(pack_root, sizeof(pack_root));
    create_minimal_manifest(pack_root,
                            ",\n  \"packs\": [{\"path\": \"packs/test.pak\"}]");
    setenv("RUNEC_ASSET_BACKEND", "pack", 1);
    assert(rc_game_data_validate_install(pack_root, &report) == 0);
    assert(report.missing_pack_files == 1);
    assert(strstr(report.message, "packs/test.pak") != NULL);
    unsetenv("RUNEC_ASSET_BACKEND");

    char blocked_root[128];
    make_fixture_root(blocked_root, sizeof(blocked_root));
    create_minimal_manifest(blocked_root,
                            ",\n  \"provenance\": \"blocked_source\"");
    unsetenv("RUNEC_ALLOW_BLOCKED_SOURCE_PROVENANCE");
    assert(rc_game_data_validate_install(blocked_root, &report) == 0);
    assert(report.blocked_provenance_count > 0);
    setenv("RUNEC_ALLOW_BLOCKED_SOURCE_PROVENANCE", "1", 1);
    assert(rc_game_data_validate_install(blocked_root, &report) == 1);
    unsetenv("RUNEC_ALLOW_BLOCKED_SOURCE_PROVENANCE");

    printf("test_game_data_validation: manifest validation checks passed.\n");
    return 0;
}
