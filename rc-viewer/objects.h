// Loads placed map objects from .objects binary into raylib Model.
// Supports OBJS (vertex colors only) and OBJ2 (+ texcoords with .atlas companion).
// Ported from runescape-rl/claude fc_objects_loader.h

#ifndef RC_OBJECTS_H
#define RC_OBJECTS_H

#include "raylib.h"
#include "rlgl.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define OBJS_MAGIC 0x4F424A53
#define OBJ2_MAGIC 0x4F424A32
#define ATLS_MAGIC 0x41544C53

typedef struct {
    Model model;
    Texture2D atlas_texture;
    int total_vertex_count;
    int min_world_x, min_world_y;
    int has_textures;
    int loaded;
} ObjectMesh;

static Texture2D objects_load_atlas(const char *atlas_path) {
    Texture2D tex = {0};
    FILE *f = fopen(atlas_path, "rb");
    if (!f) { fprintf(stderr, "atlas: can't open %s\n", atlas_path); return tex; }

    uint32_t magic, width, height;
    fread(&magic, 4, 1, f);
    if (magic != ATLS_MAGIC) { fclose(f); return tex; }
    fread(&width, 4, 1, f);
    fread(&height, 4, 1, f);

    size_t sz = (size_t)width * height * 4;
    unsigned char *pixels = malloc(sz);
    fread(pixels, 1, sz, f);
    fclose(f);

    Image img = { .data = pixels, .width = (int)width, .height = (int)height,
                  .mipmaps = 1, .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };
    tex = LoadTextureFromImage(img);
    SetTextureFilter(tex, TEXTURE_FILTER_BILINEAR);
    free(pixels);
    fprintf(stderr, "atlas: %ux%u loaded\n", width, height);
    return tex;
}

static ObjectMesh *objects_load(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "objects: can't open %s\n", path); return NULL; }

    uint32_t magic, placement_count, total_verts;
    int32_t min_wx, min_wy;
    fread(&magic, 4, 1, f);

    int has_tex = (magic == OBJ2_MAGIC);
    if (!has_tex && magic != OBJS_MAGIC) { fprintf(stderr, "objects: bad magic\n"); fclose(f); return NULL; }

    fread(&placement_count, 4, 1, f);
    fread(&min_wx, 4, 1, f);
    fread(&min_wy, 4, 1, f);
    fread(&total_verts, 4, 1, f);
    fprintf(stderr, "objects: %u placements, %u verts, %s\n",
            placement_count, total_verts, has_tex ? "OBJ2" : "OBJS");

    float *raw_verts = malloc(total_verts * 3 * sizeof(float));
    fread(raw_verts, sizeof(float), total_verts * 3, f);

    unsigned char *raw_colors = malloc(total_verts * 4);
    fread(raw_colors, 1, total_verts * 4, f);

    float *raw_tc = NULL;
    if (has_tex) {
        raw_tc = malloc(total_verts * 2 * sizeof(float));
        fread(raw_tc, sizeof(float), total_verts * 2, f);
    }
    fclose(f);

    Mesh mesh = {0};
    mesh.vertexCount = (int)total_verts;
    mesh.triangleCount = (int)(total_verts / 3);
    mesh.vertices = raw_verts;
    mesh.colors = raw_colors;
    mesh.texcoords = raw_tc;
    mesh.normals = calloc(total_verts * 3, sizeof(float));

    for (int i = 0; i < mesh.triangleCount; i++) {
        int b = i * 9;
        float e1x = raw_verts[b+3]-raw_verts[b], e1y = raw_verts[b+4]-raw_verts[b+1], e1z = raw_verts[b+5]-raw_verts[b+2];
        float e2x = raw_verts[b+6]-raw_verts[b], e2y = raw_verts[b+7]-raw_verts[b+1], e2z = raw_verts[b+8]-raw_verts[b+2];
        float nx = e1y*e2z - e1z*e2y, ny = e1z*e2x - e1x*e2z, nz = e1x*e2y - e1y*e2x;
        float len = sqrtf(nx*nx + ny*ny + nz*nz);
        if (len > 1e-4f) { nx /= len; ny /= len; nz /= len; }
        for (int v = 0; v < 3; v++) {
            mesh.normals[i*9+v*3] = nx; mesh.normals[i*9+v*3+1] = ny; mesh.normals[i*9+v*3+2] = nz;
        }
    }
    UploadMesh(&mesh, false);

    ObjectMesh *om = calloc(1, sizeof(ObjectMesh));
    om->model = LoadModelFromMesh(mesh);
    om->total_vertex_count = (int)total_verts;
    om->min_world_x = min_wx;
    om->min_world_y = min_wy;
    om->has_textures = has_tex;
    om->loaded = 1;

    if (has_tex) {
        char atlas_path[1024];
        strncpy(atlas_path, path, sizeof(atlas_path) - 1);
        char *dot = strrchr(atlas_path, '.');
        if (dot) strcpy(dot, ".atlas");
        om->atlas_texture = objects_load_atlas(atlas_path);
        if (om->atlas_texture.id > 0)
            om->model.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = om->atlas_texture;
    }
    return om;
}

static void objects_offset(ObjectMesh *om, int wx, int wy) {
    if (!om || !om->loaded) return;
    float dx = (float)wx, dz = (float)wy;
    float *v = om->model.meshes[0].vertices;
    for (int i = 0; i < om->total_vertex_count; i++) { v[i*3] -= dx; v[i*3+2] += dz; }
    UpdateMeshBuffer(om->model.meshes[0], 0, v, om->total_vertex_count * 3 * sizeof(float), 0);
    om->min_world_x -= wx; om->min_world_y -= wy;
}

static void objects_free(ObjectMesh *om) {
    if (!om) return;
    if (om->loaded) {
        if (om->atlas_texture.id > 0) UnloadTexture(om->atlas_texture);
        UnloadModel(om->model);
    }
    free(om);
}

#endif
