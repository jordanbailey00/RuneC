// Loads models from .models MDL2 binary for Raylib rendering.
// Ported from runescape-rl/claude fc_npc_models.h

#ifndef RC_MODELS_H
#define RC_MODELS_H

#include "raylib.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define MDL2_MAGIC 0x4D444C32
#define MODEL_SET_MAX 32

typedef struct {
    uint32_t model_id;
    Model model;
    int loaded;
    int16_t *base_verts;
    uint8_t *vertex_skins;
    uint16_t *face_indices;
    int base_vert_count;
    int face_count;
} ModelEntry;

typedef struct {
    ModelEntry entries[MODEL_SET_MAX];
    int count;
    int loaded;
} ModelSet;

static ModelEntry *model_find(ModelSet *set, uint32_t id) {
    for (int i = 0; i < set->count; i++)
        if (set->entries[i].model_id == id && set->entries[i].loaded) return &set->entries[i];
    return NULL;
}

static ModelSet *models_load(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "models: can't open %s\n", path); return NULL; }

    uint32_t magic, count;
    fread(&magic, 4, 1, f);
    if (magic != MDL2_MAGIC) { fprintf(stderr, "models: bad magic\n"); fclose(f); return NULL; }
    fread(&count, 4, 1, f);
    if (count > MODEL_SET_MAX) count = MODEL_SET_MAX;

    uint32_t *offsets = malloc(count * 4);
    fread(offsets, 4, count, f);

    ModelSet *set = calloc(1, sizeof(ModelSet));
    set->count = (int)count;

    for (uint32_t m = 0; m < count; m++) {
        fseek(f, offsets[m], SEEK_SET);
        uint32_t mid; uint16_t evc, fc, bvc;
        fread(&mid, 4, 1, f);
        fread(&evc, 2, 1, f);
        fread(&fc, 2, 1, f);
        fread(&bvc, 2, 1, f);

        int vc = (int)evc, tc = (int)fc;
        float *verts = malloc(vc * 3 * sizeof(float));
        fread(verts, sizeof(float), vc * 3, f);
        unsigned char *colors = malloc(vc * 4);
        fread(colors, 1, vc * 4, f);

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
        fread(bv, sizeof(int16_t), bvc * 3, f);
        uint8_t *skins = malloc(bvc);
        fread(skins, 1, bvc, f);
        uint16_t *fi = malloc(tc * 3 * sizeof(uint16_t));
        fread(fi, sizeof(uint16_t), tc * 3, f);
        fseek(f, tc, SEEK_CUR); // skip priorities

        set->entries[m] = (ModelEntry){
            .model_id = mid, .model = LoadModelFromMesh(mesh), .loaded = 1,
            .base_verts = bv, .vertex_skins = skins, .face_indices = fi,
            .base_vert_count = (int)bvc, .face_count = tc,
        };
        fprintf(stderr, "  model %u: %d tris, %d base verts\n", mid, tc, (int)bvc);
    }
    free(offsets); fclose(f);
    set->loaded = 1;
    fprintf(stderr, "models: loaded %d from %s\n", set->count, path);
    return set;
}

static void models_free(ModelSet *set) {
    if (!set) return;
    for (int i = 0; i < set->count; i++) {
        if (set->entries[i].loaded) {
            UnloadModel(set->entries[i].model);
            free(set->entries[i].base_verts);
            free(set->entries[i].vertex_skins);
            free(set->entries[i].face_indices);
        }
    }
    free(set);
}

#endif
