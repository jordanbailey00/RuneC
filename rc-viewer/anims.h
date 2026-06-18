/**
 * @fileoverview OSRS animation runtime — loads .anims binary, applies vertex-group
 * transforms to model base geometry, re-expands into raylib mesh for rendering.
 *
 * OSRS animations use vertex-group-based transforms (not bones). Each vertex has a
 * skin label (group index). FrameBase defines transform slots with types + label arrays.
 * Each frame provides per-slot {dx,dy,dz} values. Transform types:
 *   0 = origin (compute centroid of referenced vertex groups → set pivot)
 *   1 = translate (add dx/dy/dz to all vertices in referenced groups)
 *   2 = rotate (euler Z-X-Y around pivot, raw*8 → 2048-entry sine table)
 *   3 = scale (relative to pivot, 128 = 1.0x identity)
 *   5 = alpha (face transparency)
 *
 * Binary format (.anims) produced by tools/cache_pipeline/export_animations.py:
 *   v2 header: char[4] "ANM2", uint16 version, uint16 header_size,
 *              uint32 framebase_count, uint32 sequence_count,
 *              uint32 sequence_frame_count, uint32 flags
 *   followed by framebases and sequences with inlined normal-frame transforms.
 * Legacy v1 files with the old little-endian "MINA" magic are still accepted
 * so stale local data fails gracefully during transitions.
 */

#ifndef OSRS_PVP_ANIM_H
#define OSRS_PVP_ANIM_H

#include "../rc-core/io.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ANIM_LEGACY_MAGIC 0x414E494D /* legacy bytes are "MINA" */
#define ANIM2_MAGIC 0x324D4E41       /* bytes are "ANM2" */
#define ANIM_FORMAT_VERSION 2
#define ANIM_HEADER_SIZE_V2 24
#define ANIM_MAX_SLOTS 256
#define ANIM_MAX_LABELS 256
#define ANIM_SINE_COUNT 2048
#define ANIM_MAX_BASES 65535
#define ANIM_MAX_SEQUENCES 65535

/* ======================================================================== */
/* sine/cosine table (matches OSRS Rasterizer3D, fixed-point scale 65536)     */
/* ======================================================================== */

static int anim_sine[ANIM_SINE_COUNT];
static int anim_cosine[ANIM_SINE_COUNT];
static int anim_trig_initialized = 0;

static void anim_init_trig(void) {
    if (anim_trig_initialized) return;
    for (int i = 0; i < ANIM_SINE_COUNT; i++) {
        double angle = (double)i * (2.0 * 3.14159265358979323846 / ANIM_SINE_COUNT);
        anim_sine[i] = (int)(65536.0 * sin(angle));
        anim_cosine[i] = (int)(65536.0 * cos(angle));
    }
    anim_trig_initialized = 1;
}

/* ======================================================================== */
/* data structures                                                            */
/* ======================================================================== */

typedef struct {
    uint16_t base_id;
    uint8_t  slot_count;
    uint8_t* types;             /* [slot_count] transform type per slot */
    uint8_t* map_lengths;       /* [slot_count] label count per slot */
    uint8_t** frame_maps;       /* [slot_count][map_lengths[i]] label indices */
} AnimFrameBase;

typedef struct {
    uint8_t  slot_index;
    int16_t  dx, dy, dz;
} AnimTransform;

typedef struct {
    uint16_t       framebase_id;
    uint8_t        transform_count;
    AnimTransform* transforms;
} AnimFrameData;

typedef struct {
    uint16_t delay;             /* game ticks (600ms each) */
    AnimFrameData frame;
} AnimSequenceFrame;

typedef struct {
    uint16_t           seq_id;
    uint16_t           frame_count;
    uint8_t            interleave_count;
    uint8_t*           interleave_order;
    int8_t             walk_flag;  /* -1=default (no stall), 0=stall movement during anim */
    AnimSequenceFrame* frames;
} AnimSequence;

typedef struct {
    AnimFrameBase* bases;
    int            base_count;
    uint16_t*      base_ids;    /* for lookup by id */

    AnimSequence*  sequences;
    int            seq_count;
} AnimCache;

/* per-model animation working state */
typedef struct {
    /* transformed vertex positions (working copy of base_vertices) */
    int16_t* verts;             /* [base_vert_count * 3] */
    int      vert_count;

    /* vertex group lookup: groups[label] = { vertex indices } */
    int**    groups;            /* [ANIM_MAX_LABELS] arrays of vertex indices */
    int*     group_counts;      /* [ANIM_MAX_LABELS] count per group */

    /* face alpha working state for transform type 5 */
    uint8_t* base_face_alphas;  /* [face_count], cache alpha: 0=opaque */
    uint8_t* face_alphas;       /* [face_count], working copy */
    int      face_count;
    int**    face_groups;       /* [ANIM_MAX_LABELS] arrays of face indices */
    int*     face_group_counts; /* [ANIM_MAX_LABELS] count per group */
} AnimModelState;

static void anim_cache_free(AnimCache* cache);
static void anim_model_state_free(AnimModelState* state);

/* ======================================================================== */
/* loading                                                                    */
/* ======================================================================== */

typedef struct {
    const uint8_t* p;
    const uint8_t* end;
    const char* path;
    int ok;
} AnimReader;

static int anim_reader_need(AnimReader* r, size_t n) {
    if (!r->ok) return 0;
    if ((size_t)(r->end - r->p) < n) {
        fprintf(stderr, "anim_cache_load: truncated %s\n", r->path);
        r->ok = 0;
        return 0;
    }
    return 1;
}

static uint8_t anim_read_u8(AnimReader* r) {
    if (!anim_reader_need(r, 1)) return 0;
    uint8_t v = r->p[0];
    r->p++;
    return v;
}

static int8_t anim_read_i8(AnimReader* r) {
    return (int8_t)anim_read_u8(r);
}

static uint16_t anim_read_u16(AnimReader* r) {
    if (!anim_reader_need(r, 2)) return 0;
    uint16_t v = (uint16_t)(r->p[0]) | ((uint16_t)(r->p[1]) << 8);
    r->p += 2;
    return v;
}

static int16_t anim_read_i16(AnimReader* r) {
    return (int16_t)anim_read_u16(r);
}

static uint32_t anim_read_u32(AnimReader* r) {
    if (!anim_reader_need(r, 4)) return 0;
    uint32_t v = (uint32_t)(r->p[0])
              | ((uint32_t)(r->p[1]) << 8)
              | ((uint32_t)(r->p[2]) << 16)
              | ((uint32_t)(r->p[3]) << 24);
    r->p += 4;
    return v;
}

static void anim_skip(AnimReader* r, size_t n) {
    if (anim_reader_need(r, n)) r->p += n;
}

static AnimCache* anim_cache_load(const char* path) {
    FILE* f = rc_asset_fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "anim_cache_load: cannot open %s\n", path);
        return NULL;
    }

    long size = rc_file_size(f, path, "animation cache");
    if (size <= 0) {
        rc_asset_close(f);
        return NULL;
    }

    uint8_t* buf = (uint8_t*)malloc(size);
    if (!buf
            || !rc_read_exact(f, buf, sizeof(uint8_t), (size_t)size, path, "animation cache bytes")) {
        free(buf);
        rc_asset_close(f);
        return NULL;
    }
    rc_asset_close(f);

    AnimReader r = { buf, buf + size, path, 1 };
    uint32_t magic = anim_read_u32(&r);
    uint32_t base_count = 0;
    uint32_t seq_count = 0;
    uint32_t declared_sequence_frames = 0;
    uint32_t flags = 0;
    int format_version = 1;

    if (magic == ANIM2_MAGIC) {
        uint16_t version = anim_read_u16(&r);
        uint16_t header_size = anim_read_u16(&r);
        if (version != ANIM_FORMAT_VERSION || header_size < ANIM_HEADER_SIZE_V2) {
            fprintf(stderr,
                    "anim_cache_load: unsupported ANM2 header version=%u size=%u in %s\n",
                    version, header_size, path);
            free(buf);
            return NULL;
        }
        format_version = version;
        base_count = anim_read_u32(&r);
        seq_count = anim_read_u32(&r);
        declared_sequence_frames = anim_read_u32(&r);
        flags = anim_read_u32(&r);
        if (header_size > ANIM_HEADER_SIZE_V2)
            anim_skip(&r, (size_t)(header_size - ANIM_HEADER_SIZE_V2));
    } else if (magic == ANIM_LEGACY_MAGIC) {
        base_count = anim_read_u16(&r);
        seq_count = anim_read_u16(&r);
    } else {
        fprintf(stderr, "anim_cache_load: bad magic 0x%08X in %s\n", magic, path);
        free(buf);
        return NULL;
    }

    if (!r.ok || base_count > ANIM_MAX_BASES || seq_count > ANIM_MAX_SEQUENCES) {
        fprintf(stderr,
                "anim_cache_load: invalid counts bases=%u sequences=%u in %s\n",
                base_count, seq_count, path);
        free(buf);
        return NULL;
    }

    AnimCache* cache = (AnimCache*)calloc(1, sizeof(AnimCache));
    if (!cache) {
        free(buf);
        return NULL;
    }
    cache->base_count = (int)base_count;
    cache->seq_count = (int)seq_count;

    /* load framebases */
    cache->bases = (AnimFrameBase*)calloc(cache->base_count, sizeof(AnimFrameBase));
    cache->base_ids = (uint16_t*)malloc(cache->base_count * sizeof(uint16_t));
    if ((cache->base_count > 0 && (!cache->bases || !cache->base_ids))) {
        anim_cache_free(cache);
        free(buf);
        return NULL;
    }

    for (int i = 0; i < cache->base_count; i++) {
        AnimFrameBase* fb = &cache->bases[i];
        fb->base_id = anim_read_u16(&r);
        cache->base_ids[i] = fb->base_id;
        fb->slot_count = anim_read_u8(&r);

        fb->types = (uint8_t*)malloc(fb->slot_count);
        for (int s = 0; s < fb->slot_count; s++) {
            fb->types[s] = anim_read_u8(&r);
        }

        fb->map_lengths = (uint8_t*)malloc(fb->slot_count);
        fb->frame_maps = (uint8_t**)calloc(fb->slot_count, sizeof(uint8_t*));
        if (fb->slot_count > 0 && (!fb->types || !fb->map_lengths || !fb->frame_maps)) {
            r.ok = 0;
            break;
        }
        for (int s = 0; s < fb->slot_count; s++) {
            uint8_t ml = anim_read_u8(&r);
            fb->map_lengths[s] = ml;
            fb->frame_maps[s] = (uint8_t*)malloc(ml);
            if (ml > 0 && !fb->frame_maps[s]) {
                r.ok = 0;
                break;
            }
            for (int j = 0; j < ml; j++) {
                fb->frame_maps[s][j] = anim_read_u8(&r);
            }
            if (!r.ok) break;
        }
        if (!r.ok) break;
    }

    /* load sequences */
    cache->sequences = (AnimSequence*)calloc(cache->seq_count, sizeof(AnimSequence));
    if (cache->seq_count > 0 && !cache->sequences)
        r.ok = 0;
    uint32_t sequence_frames_read = 0;
    for (int i = 0; i < cache->seq_count; i++) {
        AnimSequence* seq = &cache->sequences[i];
        seq->seq_id = anim_read_u16(&r);
        seq->frame_count = anim_read_u16(&r);

        seq->interleave_count = anim_read_u8(&r);
        if (seq->interleave_count > 0) {
            seq->interleave_order = (uint8_t*)malloc(seq->interleave_count);
            if (!seq->interleave_order) {
                r.ok = 0;
                break;
            }
            for (int j = 0; j < seq->interleave_count; j++) {
                seq->interleave_order[j] = anim_read_u8(&r);
            }
        }

        seq->walk_flag = anim_read_i8(&r);

        seq->frames = (AnimSequenceFrame*)calloc(seq->frame_count, sizeof(AnimSequenceFrame));
        if (seq->frame_count > 0 && !seq->frames) {
            r.ok = 0;
            break;
        }
        for (int fi = 0; fi < seq->frame_count; fi++) {
            AnimSequenceFrame* sf = &seq->frames[fi];
            sf->delay = anim_read_u16(&r);
            sf->frame.framebase_id = anim_read_u16(&r);
            sf->frame.transform_count = anim_read_u8(&r);
            sequence_frames_read++;

            if (sf->frame.transform_count > 0) {
                sf->frame.transforms = (AnimTransform*)malloc(
                    sf->frame.transform_count * sizeof(AnimTransform));
                if (!sf->frame.transforms) {
                    r.ok = 0;
                    break;
                }
                for (int t = 0; t < sf->frame.transform_count; t++) {
                    sf->frame.transforms[t].slot_index = anim_read_u8(&r);
                    sf->frame.transforms[t].dx = anim_read_i16(&r);
                    sf->frame.transforms[t].dy = anim_read_i16(&r);
                    sf->frame.transforms[t].dz = anim_read_i16(&r);
                }
            }
            if (!r.ok) break;
        }
        if (!r.ok) break;
    }

    if (!r.ok || (format_version == ANIM_FORMAT_VERSION
            && declared_sequence_frames != sequence_frames_read)) {
        if (r.ok) {
            fprintf(stderr,
                    "anim_cache_load: ANM2 frame count mismatch declared=%u read=%u in %s\n",
                    declared_sequence_frames, sequence_frames_read, path);
        }
        anim_cache_free(cache);
        free(buf);
        return NULL;
    }

    if (r.p != r.end) {
        fprintf(stderr, "anim_cache_load: ignored %ld trailing bytes in %s\n",
                (long)(r.end - r.p), path);
    }

    free(buf);
    anim_init_trig();

    fprintf(stderr,
            "anim_cache_load: loaded ANM%d flags=0x%08X %d framebases, "
            "%d sequences, %u sequence frames from %s\n",
            format_version, flags, cache->base_count, cache->seq_count,
            sequence_frames_read, path);
    return cache;
}

/* ======================================================================== */
/* lookup                                                                     */
/* ======================================================================== */

static AnimSequence* anim_get_sequence(AnimCache* cache, uint16_t seq_id) {
    if (!cache) return NULL;
    for (int i = 0; i < cache->seq_count; i++) {
        if (cache->sequences[i].seq_id == seq_id) {
            return &cache->sequences[i];
        }
    }
    return NULL;
}

static AnimFrameBase* anim_get_framebase(AnimCache* cache, uint16_t base_id) {
    if (!cache) return NULL;
    for (int i = 0; i < cache->base_count; i++) {
        if (cache->bases[i].base_id == base_id) {
            return &cache->bases[i];
        }
    }
    return NULL;
}

/* ======================================================================== */
/* per-model animation state                                                  */
/* ======================================================================== */

static AnimModelState* anim_model_state_create_with_faces(
    const uint8_t* vertex_skins,
    int base_vert_count,
    const uint8_t* face_skins,
    int face_count,
    const uint8_t* base_face_alphas
) {
    AnimModelState* state = (AnimModelState*)calloc(1, sizeof(AnimModelState));
    if (!state) return NULL;
    state->vert_count = base_vert_count;
    state->verts = (int16_t*)calloc(base_vert_count * 3, sizeof(int16_t));
    if (base_vert_count > 0 && !state->verts) {
        free(state);
        return NULL;
    }

    /* build vertex group lookup from skin labels */
    state->groups = (int**)calloc(ANIM_MAX_LABELS, sizeof(int*));
    state->group_counts = (int*)calloc(ANIM_MAX_LABELS, sizeof(int));
    if (!state->groups || !state->group_counts) {
        free(state->verts);
        free(state->groups);
        free(state->group_counts);
        free(state);
        return NULL;
    }

    /* first pass: count vertices per label */
    int label_counts[ANIM_MAX_LABELS] = {0};
    for (int v = 0; v < base_vert_count; v++) {
        uint8_t label = vertex_skins[v];
        label_counts[label]++;
    }

    /* allocate per-label arrays */
    for (int l = 0; l < ANIM_MAX_LABELS; l++) {
        if (label_counts[l] > 0) {
            state->groups[l] = (int*)malloc(label_counts[l] * sizeof(int));
            state->group_counts[l] = 0;
        }
    }

    /* second pass: fill vertex indices */
    for (int v = 0; v < base_vert_count; v++) {
        uint8_t label = vertex_skins[v];
        state->groups[label][state->group_counts[label]++] = v;
    }

    if (face_skins && face_count > 0) {
        state->face_count = face_count;
        state->base_face_alphas = (uint8_t*)calloc(face_count, sizeof(uint8_t));
        state->face_alphas = (uint8_t*)calloc(face_count, sizeof(uint8_t));
        state->face_groups = (int**)calloc(ANIM_MAX_LABELS, sizeof(int*));
        state->face_group_counts = (int*)calloc(ANIM_MAX_LABELS, sizeof(int));
        if (!state->base_face_alphas || !state->face_alphas
                || !state->face_groups || !state->face_group_counts) {
            anim_model_state_free(state);
            return NULL;
        }
        if (base_face_alphas) {
            memcpy(state->base_face_alphas, base_face_alphas,
                   (size_t)face_count * sizeof(uint8_t));
            memcpy(state->face_alphas, base_face_alphas,
                   (size_t)face_count * sizeof(uint8_t));
        }

        int face_label_counts[ANIM_MAX_LABELS] = {0};
        for (int f = 0; f < face_count; f++) {
            uint8_t label = face_skins[f];
            face_label_counts[label]++;
        }
        for (int l = 0; l < ANIM_MAX_LABELS; l++) {
            if (face_label_counts[l] > 0) {
                state->face_groups[l] =
                    (int*)malloc((size_t)face_label_counts[l] * sizeof(int));
                if (!state->face_groups[l]) {
                    anim_model_state_free(state);
                    return NULL;
                }
                state->face_group_counts[l] = 0;
            }
        }
        for (int f = 0; f < face_count; f++) {
            uint8_t label = face_skins[f];
            state->face_groups[label][state->face_group_counts[label]++] = f;
        }
    }

    return state;
}

static AnimModelState* anim_model_state_create(
    const uint8_t* vertex_skins,
    int base_vert_count
) {
    return anim_model_state_create_with_faces(
        vertex_skins, base_vert_count, NULL, 0, NULL);
}

static void anim_model_state_free(AnimModelState* state) {
    if (!state) return;
    free(state->verts);
    for (int l = 0; l < ANIM_MAX_LABELS; l++) {
        free(state->groups[l]);
        if (state->face_groups) free(state->face_groups[l]);
    }
    free(state->groups);
    free(state->group_counts);
    free(state->base_face_alphas);
    free(state->face_alphas);
    free(state->face_groups);
    free(state->face_group_counts);
    free(state);
}

/* ======================================================================== */
/* transform application (mirrors OSRS Model.transform)                       */
/* ======================================================================== */

static void anim_reset_face_alphas(AnimModelState* state) {
    if (!state || !state->face_alphas || !state->base_face_alphas
            || state->face_count <= 0)
        return;
    memcpy(state->face_alphas, state->base_face_alphas,
           (size_t)state->face_count * sizeof(uint8_t));
}

static void anim_apply_alpha_transform(
    AnimModelState* state,
    const uint8_t* labels,
    uint8_t map_len,
    int dx
) {
    if (!state || !state->face_alphas || !state->face_groups
            || !state->face_group_counts)
        return;
    int delta = dx * 8;
    for (int m = 0; m < map_len; m++) {
        uint8_t label = labels[m];
        for (int fi = 0; fi < state->face_group_counts[label]; fi++) {
            int face = state->face_groups[label][fi];
            int alpha = (int)state->face_alphas[face] + delta;
            if (alpha < 0) alpha = 0;
            if (alpha > 255) alpha = 255;
            state->face_alphas[face] = (uint8_t)alpha;
        }
    }
}

static void anim_apply_frame(
    AnimModelState* state,
    const int16_t* base_verts_src,
    const AnimFrameData* frame,
    const AnimFrameBase* fb
) {
    /* reset to base pose */
    memcpy(state->verts, base_verts_src, state->vert_count * 3 * sizeof(int16_t));
    anim_reset_face_alphas(state);

    /* pivot point for rotate/scale */
    int pivot_x = 0, pivot_y = 0, pivot_z = 0;

    for (int t = 0; t < frame->transform_count; t++) {
        uint8_t slot_idx = frame->transforms[t].slot_index;
        if (slot_idx >= fb->slot_count) continue;

        int type = fb->types[slot_idx];
        int dx = frame->transforms[t].dx;
        int dy = frame->transforms[t].dy;
        int dz = frame->transforms[t].dz;

        uint8_t map_len = fb->map_lengths[slot_idx];
        const uint8_t* labels = fb->frame_maps[slot_idx];

        if (type == 0) {
            /* origin: compute centroid of referenced vertex groups */
            int count = 0;
            int sum_x = 0, sum_y = 0, sum_z = 0;
            for (int m = 0; m < map_len; m++) {
                uint8_t label = labels[m];
                /* label is uint8_t, always < 256 = ANIM_MAX_LABELS */
                for (int vi = 0; vi < state->group_counts[label]; vi++) {
                    int v = state->groups[label][vi];
                    sum_x += state->verts[v * 3];
                    sum_y += state->verts[v * 3 + 1];
                    sum_z += state->verts[v * 3 + 2];
                    count++;
                }
            }
            if (count > 0) {
                pivot_x = sum_x / count + dx;
                pivot_y = sum_y / count + dy;
                pivot_z = sum_z / count + dz;
            } else {
                pivot_x = dx;
                pivot_y = dy;
                pivot_z = dz;
            }
        } else if (type == 1) {
            /* translate: add dx/dy/dz to all vertices in referenced groups */
            for (int m = 0; m < map_len; m++) {
                uint8_t label = labels[m];
                /* label is uint8_t, always < 256 = ANIM_MAX_LABELS */
                for (int vi = 0; vi < state->group_counts[label]; vi++) {
                    int v = state->groups[label][vi];
                    state->verts[v * 3]     += (int16_t)dx;
                    state->verts[v * 3 + 1] += (int16_t)dy;
                    state->verts[v * 3 + 2] += (int16_t)dz;
                }
            }
        } else if (type == 2) {
            /* rotate: euler Z-X-Y around pivot.
             * raw value * 8 → index into 2048-entry sine table.
             * rotation order: Z first, then X, then Y. */
            int ax = (dx & 0xFF) * 8;
            int ay = (dy & 0xFF) * 8;
            int az = (dz & 0xFF) * 8;

            int sin_x = anim_sine[ax & 2047];
            int cos_x = anim_cosine[ax & 2047];
            int sin_y = anim_sine[ay & 2047];
            int cos_y = anim_cosine[ay & 2047];
            int sin_z = anim_sine[az & 2047];
            int cos_z = anim_cosine[az & 2047];

            for (int m = 0; m < map_len; m++) {
                uint8_t label = labels[m];
                /* label is uint8_t, always < 256 = ANIM_MAX_LABELS */
                for (int vi = 0; vi < state->group_counts[label]; vi++) {
                    int v = state->groups[label][vi];
                    int vx = state->verts[v * 3]     - pivot_x;
                    int vy = state->verts[v * 3 + 1] - pivot_y;
                    int vz = state->verts[v * 3 + 2] - pivot_z;

                    /* Z rotation */
                    int rx = (vx * cos_z + vy * sin_z) >> 16;
                    int ry = (vy * cos_z - vx * sin_z) >> 16;
                    vx = rx; vy = ry;

                    /* X rotation */
                    ry = (vy * cos_x - vz * sin_x) >> 16;
                    int rz = (vy * sin_x + vz * cos_x) >> 16;
                    vy = ry; vz = rz;

                    /* Y rotation */
                    rx = (vz * sin_y + vx * cos_y) >> 16;
                    rz = (vz * cos_y - vx * sin_y) >> 16;
                    vx = rx; vz = rz;

                    state->verts[v * 3]     = (int16_t)(vx + pivot_x);
                    state->verts[v * 3 + 1] = (int16_t)(vy + pivot_y);
                    state->verts[v * 3 + 2] = (int16_t)(vz + pivot_z);
                }
            }
        } else if (type == 3) {
            /* scale: relative to pivot, 128 = 1.0x identity */
            for (int m = 0; m < map_len; m++) {
                uint8_t label = labels[m];
                /* label is uint8_t, always < 256 = ANIM_MAX_LABELS */
                for (int vi = 0; vi < state->group_counts[label]; vi++) {
                    int v = state->groups[label][vi];
                    int vx = state->verts[v * 3]     - pivot_x;
                    int vy = state->verts[v * 3 + 1] - pivot_y;
                    int vz = state->verts[v * 3 + 2] - pivot_z;

                    vx = (vx * dx) / 128;
                    vy = (vy * dy) / 128;
                    vz = (vz * dz) / 128;

                    state->verts[v * 3]     = (int16_t)(vx + pivot_x);
                    state->verts[v * 3 + 1] = (int16_t)(vy + pivot_y);
                    state->verts[v * 3 + 2] = (int16_t)(vz + pivot_z);
                }
            }
        } else if (type == 5) {
            anim_apply_alpha_transform(state, labels, map_len, dx);
        }
    }
}

/* ======================================================================== */
/* two-track interleaved animation (matches OSRS Model.applyAnimationFrames)  */
/* ======================================================================== */

/**
 * Apply a single transform slot to the vertex state (extracted from anim_apply_frame
 * to allow per-slot interleave filtering).
 *
 * pivot_x/y/z are read/written through pointers — they persist across slots
 * within a pass, exactly like the reference's transformTempX/Y/Z.
 */
static void anim_apply_single_transform(
    AnimModelState* state,
    int type, const uint8_t* labels, uint8_t map_len,
    int dx, int dy, int dz,
    int* pivot_x, int* pivot_y, int* pivot_z
) {
    if (type == 0) {
        /* origin: compute centroid of referenced vertex groups */
        int count = 0, sx = 0, sy = 0, sz = 0;
        for (int m = 0; m < map_len; m++) {
            uint8_t label = labels[m];
            for (int vi = 0; vi < state->group_counts[label]; vi++) {
                int v = state->groups[label][vi];
                sx += state->verts[v * 3];
                sy += state->verts[v * 3 + 1];
                sz += state->verts[v * 3 + 2];
                count++;
            }
        }
        if (count > 0) {
            *pivot_x = sx / count + dx;
            *pivot_y = sy / count + dy;
            *pivot_z = sz / count + dz;
        } else {
            *pivot_x = dx;
            *pivot_y = dy;
            *pivot_z = dz;
        }
    } else if (type == 1) {
        for (int m = 0; m < map_len; m++) {
            uint8_t label = labels[m];
            for (int vi = 0; vi < state->group_counts[label]; vi++) {
                int v = state->groups[label][vi];
                state->verts[v * 3]     += (int16_t)dx;
                state->verts[v * 3 + 1] += (int16_t)dy;
                state->verts[v * 3 + 2] += (int16_t)dz;
            }
        }
    } else if (type == 2) {
        int ax = (dx & 0xFF) * 8, ay = (dy & 0xFF) * 8, az = (dz & 0xFF) * 8;
        int sin_x = anim_sine[ax & 2047], cos_x = anim_cosine[ax & 2047];
        int sin_y = anim_sine[ay & 2047], cos_y = anim_cosine[ay & 2047];
        int sin_z = anim_sine[az & 2047], cos_z = anim_cosine[az & 2047];
        for (int m = 0; m < map_len; m++) {
            uint8_t label = labels[m];
            for (int vi = 0; vi < state->group_counts[label]; vi++) {
                int v = state->groups[label][vi];
                int vx = state->verts[v * 3]     - *pivot_x;
                int vy = state->verts[v * 3 + 1] - *pivot_y;
                int vz = state->verts[v * 3 + 2] - *pivot_z;
                int rx = (vx * cos_z + vy * sin_z) >> 16;
                int ry = (vy * cos_z - vx * sin_z) >> 16;
                vx = rx; vy = ry;
                ry = (vy * cos_x - vz * sin_x) >> 16;
                int rz = (vy * sin_x + vz * cos_x) >> 16;
                vy = ry; vz = rz;
                rx = (vz * sin_y + vx * cos_y) >> 16;
                rz = (vz * cos_y - vx * sin_y) >> 16;
                state->verts[v * 3]     = (int16_t)(rx + *pivot_x);
                state->verts[v * 3 + 1] = (int16_t)(vy + *pivot_y);
                state->verts[v * 3 + 2] = (int16_t)(rz + *pivot_z);
            }
        }
    } else if (type == 3) {
        for (int m = 0; m < map_len; m++) {
            uint8_t label = labels[m];
            for (int vi = 0; vi < state->group_counts[label]; vi++) {
                int v = state->groups[label][vi];
                int vx = state->verts[v * 3]     - *pivot_x;
                int vy = state->verts[v * 3 + 1] - *pivot_y;
                int vz = state->verts[v * 3 + 2] - *pivot_z;
                state->verts[v * 3]     = (int16_t)((vx * dx) / 128 + *pivot_x);
                state->verts[v * 3 + 1] = (int16_t)((vy * dy) / 128 + *pivot_y);
                state->verts[v * 3 + 2] = (int16_t)((vz * dz) / 128 + *pivot_z);
            }
        }
    } else if (type == 5) {
        anim_apply_alpha_transform(state, labels, map_len, dx);
    }
}

/**
 * Apply two animation frames with body-part interleaving.
 *
 * Mirrors OSRS Model.applyAnimationFrames():
 *   - interleave_order lists framebase SLOT INDICES owned by SECONDARY (walk)
 *   - Pass 1: apply primary transforms for slots NOT in interleave_order
 *   - Pass 2: apply secondary transforms for slots IN interleave_order
 *   - Type-0 (pivot) transforms always execute in both passes
 *
 * CRITICAL: interleave_order contains framebase SLOT INDICES, not vertex labels!
 * The reference code (Model.java:1322-1343) walks both the frame's slot list and
 * the interleave_order simultaneously, comparing slot indices directly.
 *
 * Both passes operate on the same vertex state with independent pivot tracking,
 * exactly as the reference does with transformTempX/Y/Z reset between passes.
 */
static void anim_apply_frame_interleaved(
    AnimModelState* state,
    const int16_t* base_verts_src,
    const AnimFrameData* secondary_frame, const AnimFrameBase* secondary_fb,
    const AnimFrameData* primary_frame, const AnimFrameBase* primary_fb,
    const uint8_t* interleave_order, int interleave_count
) {
    /* reset to base pose */
    memcpy(state->verts, base_verts_src, state->vert_count * 3 * sizeof(int16_t));
    anim_reset_face_alphas(state);

    /* build boolean mask: interleave_order lists SLOT INDICES the SECONDARY owns.
       index by slot index (0-244 for our 245-slot framebase), NOT vertex labels. */
    uint8_t secondary_slot[256];
    memset(secondary_slot, 0, sizeof(secondary_slot));
    for (int i = 0; i < interleave_count; i++) {
        secondary_slot[interleave_order[i]] = 1;
    }

    /* pass 1: primary frame — apply transforms for slots NOT in interleave_order.
     * type-0 (pivot) always executes regardless of ownership.
     * matches reference: if (k1 != i1 || class18.types[k1] == 0) */
    int pivot_x = 0, pivot_y = 0, pivot_z = 0;
    for (int t = 0; t < primary_frame->transform_count; t++) {
        uint8_t slot_idx = primary_frame->transforms[t].slot_index;
        if (slot_idx >= primary_fb->slot_count) continue;

        int type = primary_fb->types[slot_idx];
        int in_interleave = secondary_slot[slot_idx];

        if (!in_interleave || type == 0) {
            anim_apply_single_transform(
                state, type,
                primary_fb->frame_maps[slot_idx],
                primary_fb->map_lengths[slot_idx],
                primary_frame->transforms[t].dx,
                primary_frame->transforms[t].dy,
                primary_frame->transforms[t].dz,
                &pivot_x, &pivot_y, &pivot_z);
        }
    }

    /* pass 2: secondary frame — apply transforms for slots IN interleave_order.
     * type-0 (pivot) always executes.
     * matches reference: if (i2 == i1 || class18.types[i2] == 0) */
    pivot_x = 0; pivot_y = 0; pivot_z = 0;
    for (int t = 0; t < secondary_frame->transform_count; t++) {
        uint8_t slot_idx = secondary_frame->transforms[t].slot_index;
        if (slot_idx >= secondary_fb->slot_count) continue;

        int type = secondary_fb->types[slot_idx];
        int in_interleave = secondary_slot[slot_idx];

        if (in_interleave || type == 0) {
            anim_apply_single_transform(
                state, type,
                secondary_fb->frame_maps[slot_idx],
                secondary_fb->map_lengths[slot_idx],
                secondary_frame->transforms[t].dx,
                secondary_frame->transforms[t].dy,
                secondary_frame->transforms[t].dz,
                &pivot_x, &pivot_y, &pivot_z);
        }
    }
}

/* ======================================================================== */
/* mesh re-expansion (apply animated base verts → expanded rendering verts)   */
/* ======================================================================== */

/**
 * Re-expand animated base vertices into the raylib mesh's expanded vertex buffer.
 * This mirrors the NPC exporter expansion without priority displacement.
 * Face priorities are draw-order metadata; moving faces creates visible seams.
 *
 * The mesh has face_count*3 expanded vertices. Each triplet (i*3, i*3+1, i*3+2)
 * corresponds to face_indices[i*3], face_indices[i*3+1], face_indices[i*3+2]
 * pointing into base_vertices.
 *
 * OSRS Y is negated for rendering (negative-up → positive-up).
 */
static void anim_update_mesh(
    float* mesh_vertices,
    const AnimModelState* state,
    const uint16_t* face_indices,
    const uint8_t* face_priorities,
    int face_count
) {
    (void)face_priorities;

    for (int fi = 0; fi < face_count; fi++) {
        int a = face_indices[fi * 3];
        int b = face_indices[fi * 3 + 1];
        int c = face_indices[fi * 3 + 2];

        int vi = fi * 9; /* 3 verts * 3 coords */
        mesh_vertices[vi]     = (float)state->verts[a * 3];
        mesh_vertices[vi + 1] = (float)(-state->verts[a * 3 + 1]); /* negate Y */
        mesh_vertices[vi + 2] = (float)state->verts[a * 3 + 2];

        mesh_vertices[vi + 3] = (float)state->verts[b * 3];
        mesh_vertices[vi + 4] = (float)(-state->verts[b * 3 + 1]);
        mesh_vertices[vi + 5] = (float)state->verts[b * 3 + 2];

        mesh_vertices[vi + 6] = (float)state->verts[c * 3];
        mesh_vertices[vi + 7] = (float)(-state->verts[c * 3 + 1]);
        mesh_vertices[vi + 8] = (float)state->verts[c * 3 + 2];
    }
}

static void anim_update_mesh_colors(
    unsigned char* mesh_colors,
    const unsigned char* rest_colors,
    const AnimModelState* state,
    int face_count,
    int vertex_count
) {
    if (!mesh_colors || !rest_colors || vertex_count <= 0)
        return;

    memcpy(mesh_colors, rest_colors, (size_t)vertex_count * 4);
    if (!state || !state->face_alphas || state->face_count <= 0)
        return;

    int count = face_count;
    if (state->face_count < count)
        count = state->face_count;
    if (vertex_count / 3 < count)
        count = vertex_count / 3;
    for (int fi = 0; fi < count; fi++) {
        uint8_t alpha = (uint8_t)(255 - state->face_alphas[fi]);
        for (int j = 0; j < 3; j++)
            mesh_colors[(fi * 3 + j) * 4 + 3] = alpha;
    }
}

/* ======================================================================== */
/* cleanup                                                                    */
/* ======================================================================== */

static void anim_cache_free(AnimCache* cache) {
    if (!cache) return;

    if (cache->bases) {
        for (int i = 0; i < cache->base_count; i++) {
            AnimFrameBase* fb = &cache->bases[i];
            free(fb->types);
            free(fb->map_lengths);
            if (fb->frame_maps) {
                for (int s = 0; s < fb->slot_count; s++) {
                    free(fb->frame_maps[s]);
                }
            }
            free(fb->frame_maps);
        }
    }
    free(cache->bases);
    free(cache->base_ids);

    if (cache->sequences) {
        for (int i = 0; i < cache->seq_count; i++) {
            AnimSequence* seq = &cache->sequences[i];
            free(seq->interleave_order);
            if (seq->frames) {
                for (int fi = 0; fi < seq->frame_count; fi++) {
                    free(seq->frames[fi].frame.transforms);
                }
            }
            free(seq->frames);
        }
    }
    free(cache->sequences);
    free(cache);
}

#endif /* OSRS_PVP_ANIM_H */
