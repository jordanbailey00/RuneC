// Loads terrain mesh from .terrain binary into raylib Model.
// Binary: TERR magic, vertex_count, region_count, min_world_x/y, vertices, colors, heightmap
// Ported from runescape-rl/claude fc_terrain_loader.h

#ifndef RC_TERRAIN_H
#define RC_TERRAIN_H

#include "raylib.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define TERR_MAGIC 0x54455252

typedef struct {
    Model model;
    int vertex_count;
    int min_world_x, min_world_y;
    int loaded;
    float *heightmap;
    int hm_min_x, hm_min_y, hm_width, hm_height;
} TerrainMesh;

static TerrainMesh *terrain_load(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "terrain_load: can't open %s\n", path); return NULL; }

    uint32_t magic, vert_count, region_count;
    int32_t min_wx, min_wy;
    fread(&magic, 4, 1, f);
    if (magic != TERR_MAGIC) { fprintf(stderr, "terrain: bad magic\n"); fclose(f); return NULL; }
    fread(&vert_count, 4, 1, f);
    fread(&region_count, 4, 1, f);
    fread(&min_wx, 4, 1, f);
    fread(&min_wy, 4, 1, f);
    fprintf(stderr, "terrain: %u verts, %u regions, origin (%d,%d)\n",
            vert_count, region_count, min_wx, min_wy);

    float *raw_verts = malloc(vert_count * 3 * sizeof(float));
    fread(raw_verts, sizeof(float), vert_count * 3, f);

    unsigned char *raw_colors = malloc(vert_count * 4);
    fread(raw_colors, 1, vert_count * 4, f);

    Mesh mesh = {0};
    mesh.vertexCount = (int)vert_count;
    mesh.triangleCount = (int)(vert_count / 3);
    mesh.vertices = raw_verts;
    mesh.colors = raw_colors;
    mesh.normals = calloc(vert_count * 3, sizeof(float));

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

    TerrainMesh *tm = calloc(1, sizeof(TerrainMesh));
    tm->model = LoadModelFromMesh(mesh);
    tm->vertex_count = (int)vert_count;
    tm->min_world_x = min_wx;
    tm->min_world_y = min_wy;
    tm->loaded = 1;

    // Heightmap (appended after colors)
    int32_t hx, hy; uint32_t hw, hh;
    if (fread(&hx, 4, 1, f) == 1 && fread(&hy, 4, 1, f) == 1 &&
        fread(&hw, 4, 1, f) == 1 && fread(&hh, 4, 1, f) == 1 &&
        hw > 0 && hh > 0 && hw <= 8192 && hh <= 8192) {
        tm->hm_min_x = hx; tm->hm_min_y = hy;
        tm->hm_width = (int)hw; tm->hm_height = (int)hh;
        tm->heightmap = malloc(hw * hh * sizeof(float));
        fread(tm->heightmap, sizeof(float), hw * hh, f);
        fprintf(stderr, "terrain heightmap: %dx%d origin (%d,%d)\n", hw, hh, hx, hy);
    }
    fclose(f);
    return tm;
}

// Shift terrain so world coords (wx,wy) become origin (0,0)
static void terrain_offset(TerrainMesh *tm, int wx, int wy) {
    if (!tm || !tm->loaded) return;
    float dx = (float)wx, dz = (float)wy;
    float *v = tm->model.meshes[0].vertices;
    for (int i = 0; i < tm->vertex_count; i++) { v[i*3] -= dx; v[i*3+2] += dz; }
    UpdateMeshBuffer(tm->model.meshes[0], 0, v, tm->vertex_count * 3 * sizeof(float), 0);
    tm->min_world_x -= wx; tm->min_world_y -= wy;
    if (tm->heightmap) { tm->hm_min_x -= wx; tm->hm_min_y -= wy; }
}

static float terrain_height_at(TerrainMesh *tm, int x, int y) {
    if (!tm || !tm->heightmap) return -2.0f;
    int lx = x - tm->hm_min_x, ly = y - tm->hm_min_y;
    if (lx < 0 || lx >= tm->hm_width || ly < 0 || ly >= tm->hm_height) return -2.0f;
    return tm->heightmap[lx + ly * tm->hm_width];
}

static float terrain_height_avg(TerrainMesh *tm, int x, int y) {
    return (terrain_height_at(tm,x,y) + terrain_height_at(tm,x+1,y) +
            terrain_height_at(tm,x,y+1) + terrain_height_at(tm,x+1,y+1)) * 0.25f;
}

static void terrain_free(TerrainMesh *tm) {
    if (!tm) return;
    if (tm->loaded) UnloadModel(tm->model);
    free(tm->heightmap);
    free(tm);
}

#endif
