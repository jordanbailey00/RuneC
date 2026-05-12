#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef RC_TEST_SOURCE_DIR
#define RC_TEST_SOURCE_DIR "."
#endif

#define ODEF_MAGIC 0x4645444fu
#define ODEF_MIN_VERSION 1u
#define ODEF_MAX_VERSION 2u

#define FLAG_WIKI_OBJECT_ID (1u << 1)
#define FLAG_WIKI_SCENERY   (1u << 2)
#define FLAG_HAS_MODELS     (1u << 3)
#define FLAG_HAS_ACTIONS    (1u << 4)
#define FLAG_TRANSFORMS     (1u << 8)
#define FLAG_INTERACTIVE    (1u << 13)
#define FLAG_MODEL_CLIPPED  (1u << 15)
#define FLAG_HOLLOW         (1u << 16)
#define FLAG_HAS_PARAMS     (1u << 19)
#define FLAG_HAS_AMBIENT_SOUND (1u << 20)

static uint8_t read_u8(FILE *f) {
    int c = fgetc(f);
    if (c == EOF) abort();
    return (uint8_t)c;
}

static uint16_t read_u16(FILE *f) {
    uint16_t b0 = read_u8(f), b1 = read_u8(f);
    return (uint16_t)(b0 | (uint16_t)(b1 << 8));
}

static uint32_t read_u32(FILE *f) {
    uint32_t b0 = read_u8(f), b1 = read_u8(f);
    uint32_t b2 = read_u8(f), b3 = read_u8(f);
    return b0 | (b1 << 8) | (b2 << 16) | (b3 << 24);
}

static int32_t read_i32(FILE *f) {
    return (int32_t)read_u32(f);
}

static void read_pstr(FILE *f, char *out, size_t out_sz) {
    uint16_t len = read_u16(f);
    size_t keep = len < out_sz - 1 ? len : out_sz - 1;
    if (keep && fread(out, 1, keep, f) != keep) abort();
    out[keep] = '\0';
    if (fseek(f, (long)(len - keep), SEEK_CUR) != 0) abort();
}

int main(void) {
    char path[512];
    snprintf(path, sizeof(path), "%s/data/defs/object_defs.bin",
             RC_TEST_SOURCE_DIR);
    FILE *f = fopen(path, "rb");
    if (!f) abort();

    if (read_u32(f) != ODEF_MAGIC) abort();
    uint32_t version = read_u32(f);
    if (version < ODEF_MIN_VERSION || version > ODEF_MAX_VERSION) abort();
    uint32_t count = read_u32(f);
    if (count < 60000u) abort();

    uint32_t named = 0, model_rows = 0, action_rows = 0, transform_rows = 0;
    uint32_t wiki_object_rows = 0, wiki_scenery_rows = 0, interactive = 0;
    uint32_t param_rows = 0, param_objects = 0, ambient_rows = 0;
    uint32_t model_clipped_rows = 0, hollow_rows = 0;
    int saw_tree = 0, saw_bank = 0, saw_fairy_tree = 0;

    for (uint32_t i = 0; i < count; i++) {
        uint32_t obj_id = read_u32(f);
        uint16_t width = read_u16(f);
        uint16_t length = read_u16(f);
        uint8_t interact_type = read_u8(f);
        uint8_t action_count = read_u8(f);
        uint8_t model_count = read_u8(f);
        uint8_t transform_count = read_u8(f);
        (void)read_u8(f);
        (void)read_i32(f);
        (void)read_i32(f);
        (void)read_i32(f);
        (void)read_u32(f);
        uint32_t flags = read_u32(f);
        uint16_t param_count = 0;
        if (version >= 2u) {
            (void)read_u8(f);
            (void)read_u8(f);
            param_count = read_u16(f);
            (void)read_i32(f);
            (void)read_u16(f);
            (void)read_u16(f);
        }

        char name[128], actions[5][64];
        read_pstr(f, name, sizeof(name));
        for (int a = 0; a < 5; a++) read_pstr(f, actions[a], sizeof(actions[a]));
        if (fseek(f, (long)model_count * 4L + (long)transform_count * 4L,
                  SEEK_CUR) != 0) abort();
        if (fseek(f, (long)param_count * 8L, SEEK_CUR) != 0) abort();

        if (name[0]) named++;
        if (flags & FLAG_HAS_MODELS) model_rows++;
        if (flags & FLAG_HAS_ACTIONS) action_rows++;
        if (flags & FLAG_TRANSFORMS) transform_rows++;
        if (flags & FLAG_WIKI_OBJECT_ID) wiki_object_rows++;
        if (flags & FLAG_WIKI_SCENERY) wiki_scenery_rows++;
        if (flags & FLAG_INTERACTIVE) interactive++;
        if (flags & FLAG_HAS_PARAMS) param_objects++;
        if (flags & FLAG_HAS_AMBIENT_SOUND) ambient_rows++;
        if (flags & FLAG_MODEL_CLIPPED) model_clipped_rows++;
        if (flags & FLAG_HOLLOW) hollow_rows++;
        param_rows += param_count;

        if (obj_id == 1276u) {
            if (strcmp(name, "Tree") != 0) abort();
            if (strcmp(actions[0], "Chop down") != 0) abort();
            if (width != 2 || length != 2 || model_count != 1) abort();
            saw_tree = 1;
        } else if (obj_id == 50901u) {
            if (strcmp(name, "Bank booth") != 0) abort();
            if (strcmp(actions[1], "Bank") != 0) abort();
            if (strcmp(actions[2], "Collect") != 0) abort();
            saw_bank = 1;
        } else if (obj_id == 27097u) {
            if (strcmp(name, "Spiritual Fairy Tree") != 0) abort();
            if (action_count != 5 || interact_type != 1) abort();
            if (strcmp(actions[3], "Ring-last-destination") != 0) abort();
            saw_fairy_tree = 1;
        }
    }
    fclose(f);

    if (named < 29000u) abort();
    if (model_rows < 55000u) abort();
    if (action_rows < 16000u) abort();
    if (transform_rows < 4500u) abort();
    if (wiki_object_rows < 15000u) abort();
    if (wiki_scenery_rows < 14000u) abort();
    if (interactive < 35000u) abort();
    if (version >= 2u) {
        if (param_rows < 1000u) abort();
        if (param_objects < 500u) abort();
        if (ambient_rows < 1000u) abort();
        if (model_clipped_rows < 300u) abort();
        if (hollow_rows < 50u) abort();
    }
    if (!saw_tree || !saw_bank || !saw_fairy_tree) abort();

    return 0;
}
