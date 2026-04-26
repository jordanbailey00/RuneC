// Loads models from .models MDL2 binary for Raylib rendering.
// Ported from runescape-rl/claude fc_npc_models.h

#ifndef RC_MODELS_H
#define RC_MODELS_H

#include "../rc-core/io.h"
#include "raylib.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define MDL2_MAGIC 0x4D444C32
#define MODEL_ID_INDEX_MAX 20000

typedef struct {
    uint32_t model_id;
    Model model;
    int loaded;
    int16_t *base_verts;
    uint8_t *vertex_skins;
    uint16_t *face_indices;
    uint8_t *face_priorities;
    int base_vert_count;
    int face_count;
} ModelEntry;

typedef struct {
    ModelEntry *entries;
    int *index_by_id;
    int count;
    int index_limit;
    int loaded;
} ModelSet;

static void models_free(ModelSet *set);

static int model_id_filter_contains(const uint32_t *ids, int id_count, uint32_t id) {
    if (!ids) return 1;
    if (id_count <= 0) return 0;
    for (int i = 0; i < id_count; i++)
        if (ids[i] == id) return 1;
    return 0;
}

static ModelEntry *model_find(ModelSet *set, uint32_t id) {
    if (!set) return NULL;
    if (id < (uint32_t)set->index_limit && set->index_by_id) {
        int idx = set->index_by_id[id];
        if (idx >= 0 && idx < set->count) return &set->entries[idx];
    }
    for (int i = 0; i < set->count; i++)
        if (set->entries[i].model_id == id && set->entries[i].loaded) return &set->entries[i];
    return NULL;
}

static ModelSet *models_load_filtered(const char *path, const uint32_t *ids, int id_count) {
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "models: can't open %s\n", path); return NULL; }

    uint32_t magic, count;
    if (!rc_read_exact(f, &magic, sizeof(magic), 1, path, "model magic")
            || magic != MDL2_MAGIC) {
        fprintf(stderr, "models: bad magic\n");
        fclose(f);
        return NULL;
    }
    if (!rc_read_exact(f, &count, sizeof(count), 1, path, "model count")) {
        fclose(f);
        return NULL;
    }
    uint32_t *offsets = malloc(count * 4);
    if (!offsets
            || !rc_read_exact(f, offsets, sizeof(offsets[0]), count, path, "model offsets")) {
        free(offsets);
        fclose(f);
        return NULL;
    }

    ModelSet *set = calloc(1, sizeof(ModelSet));
    if (!set) {
        free(offsets);
        fclose(f);
        return NULL;
    }
    set->entries = calloc(count, sizeof(ModelEntry));
    set->index_limit = MODEL_ID_INDEX_MAX;
    set->index_by_id = malloc(sizeof(int) * set->index_limit);
    if (!set->entries || !set->index_by_id) {
        free(offsets);
        fclose(f);
        models_free(set);
        return NULL;
    }
    for (int i = 0; i < set->index_limit; i++) set->index_by_id[i] = -1;
    set->count = (int)count;

    int loaded_count = 0;
    for (uint32_t m = 0; m < count; m++) {
        if (!rc_seek(f, offsets[m], SEEK_SET, path, "model offset table")) {
            free(offsets);
            fclose(f);
            models_free(set);
            return NULL;
        }
        uint32_t mid; uint16_t evc, fc, bvc;
        if (!rc_read_exact(f, &mid, sizeof(mid), 1, path, "model id")
                || !rc_read_exact(f, &evc, sizeof(evc), 1, path, "model expanded vertex count")
                || !rc_read_exact(f, &fc, sizeof(fc), 1, path, "model face count")
                || !rc_read_exact(f, &bvc, sizeof(bvc), 1, path, "model base vertex count")) {
            free(offsets);
            fclose(f);
            models_free(set);
            return NULL;
        }
        if (!model_id_filter_contains(ids, id_count, mid)) continue;

        int vc = (int)evc, tc = (int)fc;
        float *verts = malloc(vc * 3 * sizeof(float));
        if (!verts
                || !rc_read_exact(f, verts, sizeof(float), vc * 3, path, "model vertices")) {
            free(verts);
            free(offsets);
            fclose(f);
            models_free(set);
            return NULL;
        }
        unsigned char *colors = malloc(vc * 4);
        if (!colors
                || !rc_read_exact(f, colors, sizeof(unsigned char), vc * 4, path, "model colors")) {
            free(verts);
            free(colors);
            free(offsets);
            fclose(f);
            models_free(set);
            return NULL;
        }

        // OSRS units -> tile units, flip Z for Raylib
        for (int i = 0; i < vc; i++) {
            verts[i*3]   /=  128.0f;
            verts[i*3+1] /=  128.0f;
            verts[i*3+2] /= -128.0f;
        }

        Mesh mesh = {0};
        mesh.vertexCount = vc;
        mesh.triangleCount = tc;
        mesh.vertices = verts;
        mesh.colors = colors;
        mesh.normals = calloc(vc * 3, sizeof(float));
        for (int i = 0; i < tc; i++) {
            int i0 = i*3, i1 = i*3+1, i2 = i*3+2;
            float ax = verts[i1*3]-verts[i0*3], ay = verts[i1*3+1]-verts[i0*3+1], az = verts[i1*3+2]-verts[i0*3+2];
            float bx = verts[i2*3]-verts[i0*3], by = verts[i2*3+1]-verts[i0*3+1], bz = verts[i2*3+2]-verts[i0*3+2];
            float nx = ay*bz-az*by, ny = az*bx-ax*bz, nz = ax*by-ay*bx;
            float len = sqrtf(nx*nx+ny*ny+nz*nz);
            if (len > 1e-4f) { nx/=len; ny/=len; nz/=len; }
            for (int j = 0; j < 3; j++) {
                mesh.normals[(i*3+j)*3] = nx; mesh.normals[(i*3+j)*3+1] = ny; mesh.normals[(i*3+j)*3+2] = nz;
            }
        }
        UploadMesh(&mesh, false);

        // Animation data
        int16_t *bv = malloc(bvc * 3 * sizeof(int16_t));
        if (!bv
                || !rc_read_exact(f, bv, sizeof(int16_t), bvc * 3, path, "model base vertices")) {
            free(offsets);
            fclose(f);
            models_free(set);
            return NULL;
        }
        uint8_t *skins = malloc(bvc);
        if (!skins
                || !rc_read_exact(f, skins, sizeof(uint8_t), bvc, path, "model vertex skins")) {
            free(offsets);
            fclose(f);
            models_free(set);
            return NULL;
        }
        uint16_t *fi = malloc(tc * 3 * sizeof(uint16_t));
        if (!fi
                || !rc_read_exact(f, fi, sizeof(uint16_t), tc * 3, path, "model face indices")) {
            free(offsets);
            fclose(f);
            models_free(set);
            return NULL;
        }
        uint8_t *pri = malloc(tc);
        if (!pri
                || !rc_read_exact(f, pri, sizeof(uint8_t), tc, path, "model priorities")) {
            free(offsets);
            fclose(f);
            models_free(set);
            return NULL;
        }

        set->entries[m] = (ModelEntry){
            .model_id = mid, .model = LoadModelFromMesh(mesh), .loaded = 1,
            .base_verts = bv, .vertex_skins = skins, .face_indices = fi,
            .face_priorities = pri,
            .base_vert_count = (int)bvc, .face_count = tc,
        };
        if (mid < (uint32_t)set->index_limit) set->index_by_id[mid] = (int)m;
        loaded_count++;
        fprintf(stderr, "  model %u: %d tris, %d base verts\n", mid, tc, (int)bvc);
    }
    free(offsets); fclose(f);
    set->loaded = 1;
    fprintf(stderr, "models: loaded %d from %s\n", loaded_count, path);
    return set;
}

static ModelSet *models_load(const char *path) {
    return models_load_filtered(path, NULL, 0);
}

static void models_free(ModelSet *set) {
    if (!set) return;
    for (int i = 0; i < set->count; i++) {
        if (set->entries[i].loaded) {
            UnloadModel(set->entries[i].model);
            free(set->entries[i].base_verts);
            free(set->entries[i].vertex_skins);
            free(set->entries[i].face_indices);
            free(set->entries[i].face_priorities);
        }
    }
    free(set->entries);
    free(set->index_by_id);
    free(set);
}

#endif
