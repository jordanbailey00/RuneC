// Loads placed map objects from .objects binary into raylib Model.
// Supports OBJS (vertex colors only) and OBJ2 (+ texcoords with .atlas companion).
// RuneC raylib object loader.

#ifndef RC_VIEWER_OBJECTS_H
#define RC_VIEWER_OBJECTS_H

#include "../rc-core/io.h"
#include "raylib.h"
#include "rlgl.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define OBJS_MAGIC 0x4F424A53
#define OBJ2_MAGIC 0x4F424A32
#define ATLS_MAGIC 0x41544C53
#define TANM_MAGIC 0x4D4E4154
#define TANM_VERSION 1
#define OANM_MAGIC 0x4D4E414F
#define OANM_VERSION 1
#define OANM_FLAG_DYNAMIC_BASE (1u << 0)
#define OANM_FLAG_DYNAMIC_REPLACEMENT (1u << 1)
#define RUNEC_DEFAULT_OBJECT_VERTEX_LIMIT 0

typedef struct {
    uint32_t texture_id;
    uint16_t x, y, w, h;
    uint8_t direction;
    uint8_t speed;
    uint16_t pad;
} TextureAnimRow;

typedef struct {
    uint32_t model_id;
    uint32_t obj_id;
    int32_t animation_id;
    int32_t world_x;
    int32_t world_y;
    uint8_t plane;
    uint8_t obj_type;
    uint8_t rotation;
    uint8_t pad;
    float pos_x;
    float pos_y;
    float pos_z;
    float phase_ticks;
} ObjectAnimRow;

typedef struct {
    Model model;
    Texture2D atlas_texture;
    unsigned char *atlas_base_pixels;
    unsigned char *atlas_pixels;
    int atlas_width;
    int atlas_height;
    TextureAnimRow *texture_anims;
    int texture_anim_count;
    float texture_anim_ticks;
    ObjectAnimRow *object_anims;
    int object_anim_count;
    int total_vertex_count;
    int min_world_x, min_world_y;
    int has_textures;
    int owns_atlas_texture;
    int loaded;
} ObjectMesh;

static int objects_vertex_limit(void) {
    const char *value = getenv("RUNEC_OBJECT_VERTEX_LIMIT");
    if (!value || !value[0])
        return RUNEC_DEFAULT_OBJECT_VERTEX_LIMIT;
    int parsed = atoi(value);
    return parsed < 0 ? 0 : parsed;
}

static int objects_skip_oversized_asset(const char *path, int vertex_limit) {
    if (!path || vertex_limit <= 0)
        return 0;
    uint64_t size = 0;
    if (!rc_asset_size(path, &size))
        return 0;
    uint64_t min_bytes_for_limit = 20u + (uint64_t)vertex_limit * 16u;
    if (size <= min_bytes_for_limit)
        return 0;
    fprintf(stderr,
            "objects: skipping %s because asset is %llu bytes and exceeds "
            "RUNEC_OBJECT_VERTEX_LIMIT=%d; set limit to 0 for full scene\n",
            path, (unsigned long long)size, vertex_limit);
    return 1;
}

static int objects_companion_path(char *out, size_t cap, const char *path,
                                  const char *suffix) {
    if (!out || cap == 0 || !path || !suffix) return 0;
    const char *dot = strrchr(path, '.');
    size_t stem_len = dot ? (size_t)(dot - path) : strlen(path);
    int n = snprintf(out, cap, "%.*s%s", (int)stem_len, path, suffix);
    return n > 0 && (size_t)n < cap;
}

static void objects_load_texture_anims(ObjectMesh *om, const char *atlas_path) {
    if (!om) return;
    char tanm_path[1024];
    strncpy(tanm_path, atlas_path, sizeof(tanm_path) - 1);
    tanm_path[sizeof(tanm_path) - 1] = '\0';
    char *dot = strrchr(tanm_path, '.');
    if (dot) strcpy(dot, ".tanim");

    FILE *f = rc_asset_fopen(tanm_path, "rb");
    if (!f) return;
    uint32_t magic, version, count;
    if (!rc_read_exact(f, &magic, sizeof(magic), 1, tanm_path, "tanim magic")
            || !rc_read_exact(f, &version, sizeof(version), 1, tanm_path,
                              "tanim version")
            || !rc_read_exact(f, &count, sizeof(count), 1, tanm_path,
                              "tanim count")
            || magic != TANM_MAGIC || version != TANM_VERSION) {
        rc_asset_close(f);
        return;
    }
    om->texture_anims = calloc(count, sizeof(*om->texture_anims));
    if (count > 0 && !om->texture_anims) {
        rc_asset_close(f);
        return;
    }
    om->texture_anim_count = (int)count;
    for (uint32_t i = 0; i < count; i++) {
        if (!rc_read_exact(f, &om->texture_anims[i],
                           sizeof(om->texture_anims[i]), 1, tanm_path,
                           "tanim row")) {
            free(om->texture_anims);
            om->texture_anims = NULL;
            om->texture_anim_count = 0;
            rc_asset_close(f);
            return;
        }
    }
    rc_asset_close(f);
    fprintf(stderr, "atlas anim: %d animated cells loaded\n",
            om->texture_anim_count);
}

static void objects_load_object_anims_path(ObjectMesh *om, const char *path) {
    if (!om || !path) return;
    FILE *f = rc_asset_fopen(path, "rb");
    if (!f) return;
    uint32_t magic, version, count;
    if (!rc_read_exact(f, &magic, sizeof(magic), 1, path, "object anim magic")
            || !rc_read_exact(f, &version, sizeof(version), 1, path,
                              "object anim version")
            || !rc_read_exact(f, &count, sizeof(count), 1, path,
                              "object anim count")
            || magic != OANM_MAGIC || version != OANM_VERSION) {
        rc_asset_close(f);
        return;
    }
    om->object_anims = calloc(count, sizeof(*om->object_anims));
    if (count > 0 && !om->object_anims) {
        rc_asset_close(f);
        return;
    }
    om->object_anim_count = (int)count;
    for (uint32_t i = 0; i < count; i++) {
        ObjectAnimRow *row = &om->object_anims[i];
        if (!rc_read_exact(f, &row->model_id, sizeof(row->model_id), 1, path,
                           "object anim model id")
                || !rc_read_exact(f, &row->obj_id, sizeof(row->obj_id), 1,
                                  path, "object anim obj id")
                || !rc_read_exact(f, &row->animation_id,
                                  sizeof(row->animation_id), 1, path,
                                  "object anim sequence")
                || !rc_read_exact(f, &row->world_x, sizeof(row->world_x), 1,
                                  path, "object anim x")
                || !rc_read_exact(f, &row->world_y, sizeof(row->world_y), 1,
                                  path, "object anim y")
                || !rc_read_exact(f, &row->plane, sizeof(row->plane), 1,
                                  path, "object anim plane")
                || !rc_read_exact(f, &row->obj_type, sizeof(row->obj_type), 1,
                                  path, "object anim type")
                || !rc_read_exact(f, &row->rotation, sizeof(row->rotation), 1,
                                  path, "object anim rotation")
                || !rc_read_exact(f, &row->pad, sizeof(row->pad), 1, path,
                                  "object anim pad")
                || !rc_read_exact(f, &row->pos_x, sizeof(row->pos_x), 1, path,
                                  "object anim pos x")
                || !rc_read_exact(f, &row->pos_y, sizeof(row->pos_y), 1, path,
                                  "object anim pos y")
                || !rc_read_exact(f, &row->pos_z, sizeof(row->pos_z), 1, path,
                                  "object anim pos z")
                || !rc_read_exact(f, &row->phase_ticks,
                                  sizeof(row->phase_ticks), 1, path,
                                  "object anim phase")) {
            free(om->object_anims);
            om->object_anims = NULL;
            om->object_anim_count = 0;
            rc_asset_close(f);
            return;
        }
    }
    rc_asset_close(f);
    fprintf(stderr, "object anim: %d placements loaded\n",
            om->object_anim_count);
}

static void objects_load_object_anims(ObjectMesh *om, const char *objects_path) {
    if (!om || !objects_path) return;
    char path[1024];
    if (!objects_companion_path(path, sizeof(path), objects_path, ".oanim"))
        return;
    objects_load_object_anims_path(om, path);
}

static Texture2D objects_load_atlas(ObjectMesh *om, const char *atlas_path) {
    Texture2D tex = {0};
    FILE *f = rc_asset_fopen(atlas_path, "rb");
    if (!f) { fprintf(stderr, "atlas: can't open %s\n", atlas_path); return tex; }

    uint32_t magic, width, height;
    if (!rc_read_exact(f, &magic, sizeof(magic), 1, atlas_path, "atlas magic")
            || magic != ATLS_MAGIC) {
        rc_asset_close(f);
        return tex;
    }
    if (!rc_read_exact(f, &width, sizeof(width), 1, atlas_path, "atlas width")
            || !rc_read_exact(f, &height, sizeof(height), 1, atlas_path, "atlas height")) {
        rc_asset_close(f);
        return tex;
    }

    size_t sz = (size_t)width * height * 4;
    unsigned char *pixels = malloc(sz);
    if (!pixels
            || !rc_read_exact(f, pixels, sizeof(unsigned char), sz, atlas_path, "atlas pixels")) {
        free(pixels);
        rc_asset_close(f);
        return tex;
    }
    rc_asset_close(f);

    Image img = { .data = pixels, .width = (int)width, .height = (int)height,
                  .mipmaps = 1, .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };
    tex = LoadTextureFromImage(img);
    SetTextureFilter(tex, TEXTURE_FILTER_POINT);
    if (om && tex.id > 0) {
        size_t sz = (size_t)width * height * 4;
        om->atlas_width = (int)width;
        om->atlas_height = (int)height;
        om->atlas_base_pixels = malloc(sz);
        om->atlas_pixels = malloc(sz);
        if (om->atlas_base_pixels && om->atlas_pixels) {
            memcpy(om->atlas_base_pixels, pixels, sz);
            memcpy(om->atlas_pixels, pixels, sz);
            objects_load_texture_anims(om, atlas_path);
        } else {
            free(om->atlas_base_pixels);
            free(om->atlas_pixels);
            om->atlas_base_pixels = NULL;
            om->atlas_pixels = NULL;
        }
    }
    free(pixels);
    fprintf(stderr, "atlas: %ux%u loaded\n", width, height);
    return tex;
}

static void objects_update_texture_anims(ObjectMesh *om, float dt) {
    if (!om || !om->atlas_pixels || !om->atlas_base_pixels
            || om->atlas_texture.id <= 0 || om->texture_anim_count <= 0)
        return;
    om->texture_anim_ticks += dt * 50.0f;
    size_t total = (size_t)om->atlas_width * om->atlas_height * 4;
    memcpy(om->atlas_pixels, om->atlas_base_pixels, total);

    for (int r = 0; r < om->texture_anim_count; r++) {
        TextureAnimRow *row = &om->texture_anims[r];
        if (row->w == 0 || row->h == 0 || row->x + row->w > om->atlas_width
                || row->y + row->h > om->atlas_height || row->speed == 0)
            continue;
        int shift = (int)(om->texture_anim_ticks * (float)row->speed);
        if (row->direction == 1 || row->direction == 3) {
            int pad = row->pad;
            if (pad * 2 >= row->h) pad = 0;
            int center_h = row->h - pad * 2;
            if (center_h <= 0) center_h = row->h;
            shift %= center_h;
            if (row->direction == 1) shift = -shift;
            for (int y = 0; y < row->h; y++) {
                int sy = (y - pad + shift) % center_h;
                if (sy < 0) sy += center_h;
                sy += pad;
                for (int x = 0; x < row->w; x++) {
                    size_t di = ((size_t)(row->y + y) * om->atlas_width
                               + (row->x + x)) * 4;
                    size_t si = ((size_t)(row->y + sy) * om->atlas_width
                               + (row->x + x)) * 4;
                    memcpy(&om->atlas_pixels[di], &om->atlas_base_pixels[si], 4);
                }
            }
        } else if (row->direction == 2 || row->direction == 4) {
            shift %= row->w;
            if (row->direction == 2) shift = -shift;
            for (int y = 0; y < row->h; y++) {
                for (int x = 0; x < row->w; x++) {
                    int sx = (x + shift) % row->w;
                    if (sx < 0) sx += row->w;
                    size_t di = ((size_t)(row->y + y) * om->atlas_width
                               + (row->x + x)) * 4;
                    size_t si = ((size_t)(row->y + y) * om->atlas_width
                               + (row->x + sx)) * 4;
                    memcpy(&om->atlas_pixels[di], &om->atlas_base_pixels[si], 4);
                }
            }
        }
    }
    UpdateTexture(om->atlas_texture, om->atlas_pixels);
}

static ObjectMesh *objects_load_with_resources(const char *path,
                                               const char *atlas_override,
                                               Texture2D shared_atlas) {
    int vertex_limit = objects_vertex_limit();
    if (objects_skip_oversized_asset(path, vertex_limit))
        return NULL;

    FILE *f = rc_asset_fopen(path, "rb");
    if (!f) { fprintf(stderr, "objects: can't open %s\n", path); return NULL; }

    uint32_t magic, placement_count, total_verts;
    int32_t min_wx, min_wy;
    if (!rc_read_exact(f, &magic, sizeof(magic), 1, path, "object magic")) {
        rc_asset_close(f);
        return NULL;
    }

    int has_tex = (magic == OBJ2_MAGIC);
    if (!has_tex && magic != OBJS_MAGIC) { fprintf(stderr, "objects: bad magic\n"); rc_asset_close(f); return NULL; }

    if (!rc_read_exact(f, &placement_count, sizeof(placement_count), 1, path, "object placement count")
            || !rc_read_exact(f, &min_wx, sizeof(min_wx), 1, path, "object min world x")
            || !rc_read_exact(f, &min_wy, sizeof(min_wy), 1, path, "object min world y")
            || !rc_read_exact(f, &total_verts, sizeof(total_verts), 1, path, "object vertex count")) {
        rc_asset_close(f);
        return NULL;
    }
    fprintf(stderr, "objects: %u placements, %u verts, %s\n",
            placement_count, total_verts, has_tex ? "OBJ2" : "OBJS");

    if (vertex_limit > 0 && total_verts > (uint32_t)vertex_limit) {
        fprintf(stderr,
                "objects: skipping %s because %u verts exceeds "
                "RUNEC_OBJECT_VERTEX_LIMIT=%d; set limit to 0 for full scene\n",
                path, total_verts, vertex_limit);
        rc_asset_close(f);
        return NULL;
    }

    float *raw_verts = malloc(total_verts * 3 * sizeof(float));
    if (!raw_verts
            || !rc_read_exact(f, raw_verts, sizeof(float), total_verts * 3, path, "object vertices")) {
        free(raw_verts);
        rc_asset_close(f);
        return NULL;
    }

    unsigned char *raw_colors = malloc(total_verts * 4);
    if (!raw_colors
            || !rc_read_exact(f, raw_colors, sizeof(unsigned char), total_verts * 4, path, "object colors")) {
        free(raw_verts);
        free(raw_colors);
        rc_asset_close(f);
        return NULL;
    }

    float *raw_tc = NULL;
    if (has_tex) {
        raw_tc = malloc(total_verts * 2 * sizeof(float));
        if (!raw_tc
                || !rc_read_exact(f, raw_tc, sizeof(float), total_verts * 2, path, "object texcoords")) {
            free(raw_verts);
            free(raw_colors);
            free(raw_tc);
            rc_asset_close(f);
            return NULL;
        }
    }
    rc_asset_close(f);

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
        if (shared_atlas.id > 0) {
            om->atlas_texture = shared_atlas;
            om->owns_atlas_texture = 0;
            om->model.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture =
                shared_atlas;
        } else {
            char atlas_path[1024];
            if (atlas_override && atlas_override[0]) {
                snprintf(atlas_path, sizeof(atlas_path), "%s", atlas_override);
            } else {
                strncpy(atlas_path, path, sizeof(atlas_path) - 1);
                char *dot = strrchr(atlas_path, '.');
                if (dot) strcpy(dot, ".atlas");
            }
            om->atlas_texture = objects_load_atlas(om, atlas_path);
            om->owns_atlas_texture = om->atlas_texture.id > 0;
            if (om->atlas_texture.id > 0)
                om->model.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture =
                    om->atlas_texture;
        }
    }
    objects_load_object_anims(om, path);
    return om;
}

static ObjectMesh *objects_load_with_atlas(const char *path,
                                           const char *atlas_override) {
    return objects_load_with_resources(path, atlas_override, (Texture2D){0});
}

static ObjectMesh *objects_load_with_shared_atlas(const char *path,
                                                  Texture2D atlas_texture) {
    return objects_load_with_resources(path, NULL, atlas_texture);
}

static ObjectMesh *objects_load(const char *path) {
    return objects_load_with_atlas(path, NULL);
}

static void objects_offset(ObjectMesh *om, int wx, int wy) {
    if (!om || !om->loaded) return;
    float dx = (float)wx, dz = (float)wy;
    float *v = om->model.meshes[0].vertices;
    for (int i = 0; i < om->total_vertex_count; i++) { v[i*3] -= dx; v[i*3+2] += dz; }
    UpdateMeshBuffer(om->model.meshes[0], 0, v, om->total_vertex_count * 3 * sizeof(float), 0);
    om->min_world_x -= wx; om->min_world_y -= wy;
}

static void objects_set_shader(ObjectMesh *om, Shader shader) {
    if (!om || !om->loaded || shader.id <= 0) return;
    for (int i = 0; i < om->model.materialCount; i++)
        om->model.materials[i].shader = shader;
}

static void objects_free(ObjectMesh *om) {
    if (!om) return;
    if (om->loaded) {
        if (om->owns_atlas_texture && om->atlas_texture.id > 0)
            UnloadTexture(om->atlas_texture);
        UnloadModel(om->model);
    }
    free(om->atlas_base_pixels);
    free(om->atlas_pixels);
    free(om->texture_anims);
    free(om->object_anims);
    free(om);
}

#endif
