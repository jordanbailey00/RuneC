#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "assets.h"
#include "models.h"
#include "objects.h"
#include "terrain.h"

typedef struct {
    int terrain_vertices;
    int object_vertices;
    int object_anims;
    int model_count;
    size_t terrain_upload_bytes;
    size_t object_upload_bytes;
    size_t model_upload_bytes;
} CpuAssetSnapshot;

// One MDL2 model of two faces over three base vertices, with or without an
// MOF1 block stacking face 1 half a unit in front.
static void write_stacked_model(FILE *f, uint32_t id, int stacked) {
    uint16_t counts[] = {6, 2, 3};
    float verts[18] = {0};
    unsigned char colors[24];
    int16_t base[9] = {0, 0, 0, 100, 0, 0, 0, 0, 100};
    uint8_t skins[3] = {0};
    uint16_t faces[6] = {0, 1, 2, 0, 1, 2};
    uint8_t priorities[2] = {0, 1};
    memset(colors, 255, sizeof(colors));
    fwrite(&id, sizeof(id), 1, f);
    fwrite(counts, sizeof(counts), 1, f);
    fwrite(verts, sizeof(verts), 1, f);
    fwrite(colors, sizeof(colors), 1, f);
    fwrite(base, sizeof(base), 1, f);
    fwrite(skins, sizeof(skins), 1, f);
    fwrite(faces, sizeof(faces), 1, f);
    fwrite(priorities, sizeof(priorities), 1, f);
    if (stacked) {
        uint32_t block[] = {MOF1_MAGIC, 1};
        uint16_t entry[] = {1, 0};
        float offset = 0.5f;
        fwrite(block, sizeof(block), 1, f);
        fwrite(entry, sizeof(entry), 1, f);
        fwrite(&offset, sizeof(offset), 1, f);
    }
}

static void test_stacked_face_offsets_load(void) {
    char path[] = "/tmp/runec_mof1_XXXXXX";
    int fd = mkstemp(path);
    FILE *f = fd >= 0 ? fdopen(fd, "wb") : NULL;
    assert(f != NULL);
    uint32_t header[] = {0x4D444C32u, 2, 0, 0};
    fwrite(header, sizeof(header), 1, f);
    header[2] = (uint32_t)ftell(f);
    write_stacked_model(f, 41, 1);
    header[3] = (uint32_t)ftell(f);
    write_stacked_model(f, 42, 0);
    fseek(f, 0, SEEK_SET);
    fwrite(header, sizeof(header), 1, f);
    fclose(f);

    rc_asset_reset();
    rc_asset_set_backend(RC_ASSET_BACKEND_LOOSE);
    ModelSet *set = models_load_cpu_with_shared_atlas(path);
    assert(set != NULL && set->count == 2);
    ModelEntry *stacked = model_find(set, 41);
    ModelEntry *plain = model_find(set, 42);
    assert(stacked && stacked->face_offsets);
    assert(stacked->face_offsets[0] == 0.0f && stacked->face_offsets[1] == 0.5f);
    assert(plain && plain->loaded && !plain->face_offsets);
    models_free(set);
    remove(path);
}

static CpuAssetSnapshot load_snapshot(RcAssetBackend backend) {
    rc_asset_reset();
    rc_asset_set_backend(backend);

    TerrainMesh *terrain = terrain_load_cpu(
        "data/regions/49_53.p0.terrain");
    ObjectMesh *objects = objects_load_cpu_with_shared_atlas(
        "data/regions/49_53.p0.objects");
    assert(terrain != NULL && !terrain->loaded);
    assert(objects != NULL && !objects->loaded);

    ModelSet *models = models_load_cpu_with_shared_atlas(
        "data/regions/49_53.p0.object_anim.models");
    assert(models != NULL && models->loaded);

    TerrainMesh *empty = terrain_load_cpu(
        "data/regions/28_80.p0.terrain");
    ObjectMesh *empty_objects = objects_load_cpu_with_shared_atlas(
        "data/regions/28_80.p0.objects");
    assert(empty != NULL && !empty->loaded && empty->vertex_count == 0);
    assert(empty_objects != NULL && !empty_objects->loaded
           && empty_objects->total_vertex_count == 0
           && empty_objects->object_anim_count > 0);
    assert(empty->heightmap != NULL);
    int empty_origin_x = empty->hm_min_x;
    int empty_origin_y = empty->hm_min_y;
    terrain_offset(empty, empty_origin_x, empty_origin_y);
    assert(empty->hm_min_x == 0 && empty->hm_min_y == 0);
    assert(empty->min_world_x == 0 && empty->min_world_y == 0);

    CpuAssetSnapshot snapshot = {
        .terrain_vertices = terrain->vertex_count,
        .object_vertices = objects->total_vertex_count,
        .object_anims = objects->object_anim_count,
        .model_count = models->count,
        .terrain_upload_bytes = terrain_upload_bytes(terrain),
        .object_upload_bytes = objects_upload_bytes(objects),
        .model_upload_bytes = models_upload_bytes(models),
    };
    assert(snapshot.terrain_vertices > 0);
    assert(snapshot.object_vertices > 0);
    assert(snapshot.object_anims > 0);
    assert(snapshot.terrain_upload_bytes > 0);
    assert(snapshot.object_upload_bytes > 0);
    assert(snapshot.model_upload_bytes > 0);

    objects_free(empty_objects);
    terrain_free(empty);
    models_free(models);
    objects_free(objects);
    terrain_free(terrain);
    return snapshot;
}

int main(void) {
    test_stacked_face_offsets_load();
    CpuAssetSnapshot loose = load_snapshot(RC_ASSET_BACKEND_LOOSE);
    CpuAssetSnapshot packed = load_snapshot(RC_ASSET_BACKEND_PACK);
    assert(loose.terrain_vertices == packed.terrain_vertices);
    assert(loose.object_vertices == packed.object_vertices);
    assert(loose.object_anims == packed.object_anims);
    assert(loose.model_count == packed.model_count);
    assert(loose.terrain_upload_bytes == packed.terrain_upload_bytes);
    assert(loose.object_upload_bytes == packed.object_upload_bytes);
    assert(loose.model_upload_bytes == packed.model_upload_bytes);
    rc_asset_reset();
    printf("test_streaming_cpu_assets: loose/pack CPU assets match.\n");
    return 0;
}
