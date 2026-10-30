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

static void put_texel(uint8_t *atlas, int width, int x, int y,
                      uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    uint8_t *p = &atlas[((size_t)y * width + x) * 4];
    p[0] = r; p[1] = g; p[2] = b; p[3] = a;
}

static int cell_opaque(uint8_t *mips, int width, int height, int level,
                       int cx, int cy) {
    int lw, lh, n = 0, size = ATLAS_MIP_CELL >> level;
    atlas_mip_level_size(width, height, level, &lw, &lh);
    uint8_t *texels = atlas_mip_level(mips, width, height, level);
    for (int y = 0; y < size; y++)
        for (int x = 0; x < size; x++)
            n += texels[((size_t)((cy >> level) + y) * lw + (cx >> level) + x)
                        * 4 + 3] == 255;
    return n;
}

// Mip levels stay inside their cell, keep each cell's opaque share and
// scroll with texture animations like a rebuilt chain.
static void test_atlas_mips(void) {
    enum { W = 512, H = 384 };
    static uint8_t atlas[W * H * 4];
    assert(atlas_mip_level_count(2048, 1792) == 12);
    assert(atlas_mip_chain_bytes(4, 2) == (4 * 2 + 2 * 1 + 1) * 4);
    assert(!atlas_mips_build(atlas, 200, 128) && !atlas_mips_supported(64, 64));
    for (int y = 0; y < 128; y++) {
        for (int x = 0; x < 128; x++) {
            int dot = x % 2 == 0 && y % 2 == 0;
            put_texel(atlas, W, x, y, 255, 0, 0, 255);
            put_texel(atlas, W, 128 + x, y, 0, 255, 0, 255);
            put_texel(atlas, W, 256 + x, y, dot ? 255 : 0, 0, dot ? 0 : 255,
                      dot ? 255 : 0);
            put_texel(atlas, W, 384 + x, y, 200, 200, 200, x % 4 ? 0 : 255);
            for (int copy = 0; copy < 2; copy++)  // a 2-cell scrolling slot
                put_texel(atlas, W, 128 + x, 128 + copy * 128 + y,
                          (uint8_t)(y * 2), (uint8_t)x, 50, 255);
            put_texel(atlas, W, 384 + x, 128 + y, (uint8_t)(x * 2), 9, 9, 255);
        }
    }
    uint8_t *mips = atlas_mips_build(atlas, W, H);
    assert(mips);
    for (int level = 1; level <= ATLAS_MIP_CELL_LEVELS; level++) {
        int lw, lh, size = ATLAS_MIP_CELL >> level;
        atlas_mip_level_size(W, H, level, &lw, &lh);
        uint8_t *texels = atlas_mip_level(mips, W, H, level);
        for (int y = 0; y < size; y++) {
            for (int x = 0; x < size; x++) {
                uint8_t *red = &texels[((size_t)y * lw + x) * 4];
                uint8_t *green = &texels[((size_t)y * lw + size + x) * 4];
                uint8_t *dot = &texels[((size_t)y * lw + 2 * size + x) * 4];
                assert(red[0] == 255 && red[1] == 0 && red[3] == 255);
                assert(green[0] == 0 && green[1] == 255 && green[3] == 255);
                assert(!dot[3] || (dot[0] == 255 && dot[2] == 0));
            }
        }
        if (level < ATLAS_MIP_CELL_LEVELS) {  // 25% of texels stay opaque
            assert(cell_opaque(mips, W, H, level, 256, 0) * 4 == size * size);
            assert(cell_opaque(mips, W, H, level, 384, 0) * 4 == size * size);
        }
        assert(cell_opaque(mips, W, H, level, 0, 128) == 0);  // empty cell
    }

    // Rebuilding one changed cell matches a full build.
    put_texel(atlas, W, 260, 7, 1, 2, 3, 255);
    atlas_mips_update_rect(mips, atlas, W, H, 256, 0, 128, 128);
    uint8_t *full = atlas_mips_build(atlas, W, H);
    size_t lower = atlas_mip_chain_bytes(W, H) - (size_t)W * H * 4;
    assert(full && memcmp(mips, full, lower) == 0);

    // Scrolled levels equal the chain of the scrolled level 0, vertically
    // inside a padded slot (shift 16) and horizontally (shift 32).
    static uint8_t scrolled[W * H * 4];
    static uint8_t out[128 * 384 * 4];
    memcpy(scrolled, atlas, sizeof(scrolled));
    for (int y = 0; y < 384 - 128; y++)
        for (int x = 0; x < 128; x++) {
            int sy = (y + 16) % 128;
            memcpy(&scrolled[((size_t)(128 + y) * W + 128 + x) * 4],
                   &atlas[((size_t)(128 + sy) * W + 128 + x) * 4], 4);
        }
    for (int y = 0; y < 128; y++)
        for (int x = 0; x < 128; x++)
            memcpy(&scrolled[((size_t)(128 + y) * W + 384 + x) * 4],
                   &atlas[((size_t)(128 + y) * W + 384 + (x + 32) % 128) * 4],
                   4);
    uint8_t *expected = atlas_mips_build(scrolled, W, H);
    assert(expected);
    for (int level = 1; level <= 4; level++) {
        int lw, lh;
        atlas_mip_level_size(W, H, level, &lw, &lh);
        uint8_t *want = atlas_mip_level(expected, W, H, level);
        atlas_scroll_level(out, full, W, H, level, 128, 128, 128, 256, 0, 128,
                           1, 16);
        for (int row = 0; row < 256 >> level; row++)
            assert(memcmp(out + (size_t)row * (128 >> level) * 4,
                          want + ((size_t)((128 >> level) + row) * lw
                                  + (128 >> level)) * 4,
                          (size_t)(128 >> level) * 4) == 0);
        atlas_scroll_level(out, full, W, H, level, 384, 128, 128, 128, 0, 128,
                           0, 32);
        for (int row = 0; row < 128 >> level; row++)
            assert(memcmp(out + (size_t)row * (128 >> level) * 4,
                          want + ((size_t)((128 >> level) + row) * lw
                                  + (384 >> level)) * 4,
                          (size_t)(128 >> level) * 4) == 0);
    }
    // Row copies scroll level 0 exactly as the per-texel loops they replace.
    static uint8_t got[128 * 256 * 4], want[128 * 256 * 4];
    for (int vertical = 0; vertical < 2; vertical++) {
        for (int pad = 0; pad <= 64; pad += 64) {
            int center = vertical ? 256 - pad * 2 : 256;
            for (int shift = -70; shift <= 70; shift += 35) {
                atlas_scroll_rect(got, 128, &atlas[((size_t)128 * W + 128) * 4],
                                  W, 128, 256, pad, center, vertical, shift);
                for (int y = 0; y < 256; y++) {
                    for (int x = 0; x < 128; x++) {
                        int sx = x, sy = y;
                        if (vertical) {
                            sy = (y - pad + shift) % center;
                            if (sy < 0) sy += center;
                            sy += pad;
                        } else {
                            sx = (x + shift) % 128;
                            if (sx < 0) sx += 128;
                        }
                        memcpy(&want[((size_t)y * 128 + x) * 4],
                               &atlas[((size_t)(128 + sy) * W + 128 + sx) * 4],
                               4);
                    }
                }
                assert(memcmp(got, want, sizeof(got)) == 0);
            }
        }
    }
    free(expected);
    free(full);
    free(mips);
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
    test_atlas_mips();
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
