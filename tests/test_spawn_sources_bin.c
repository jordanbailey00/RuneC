#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef RC_TEST_SOURCE_DIR
#define RC_TEST_SOURCE_DIR "."
#endif

#define SSPR_MAGIC 0x52505353u
#define SSPR_VERSION 1u

static uint8_t read_u8(FILE *f) {
    int c = fgetc(f);
    if (c == EOF) abort();
    return (uint8_t)c;
}

static uint16_t read_u16(FILE *f) {
    uint8_t b0 = read_u8(f), b1 = read_u8(f);
    return (uint16_t)(b0 | ((uint16_t)b1 << 8));
}

static uint32_t read_u32(FILE *f) {
    uint32_t b0 = read_u8(f), b1 = read_u8(f);
    uint32_t b2 = read_u8(f), b3 = read_u8(f);
    return b0 | (b1 << 8) | (b2 << 16) | (b3 << 24);
}

static int32_t read_i32(FILE *f) {
    return (int32_t)read_u32(f);
}

int main(void) {
    char path[512];
    snprintf(path, sizeof(path), "%s/data/defs/spawn_sources.bin",
             RC_TEST_SOURCE_DIR);
    FILE *f = fopen(path, "rb");
    if (!f) abort();

    if (read_u32(f) != SSPR_MAGIC) abort();
    if (read_u32(f) != SSPR_VERSION) abort();
    uint32_t count = read_u32(f);
    if (count <= 40000u) abort();

    uint32_t kind_counts[10] = {0};
    uint32_t instance_rows = 0;
    for (uint32_t i = 0; i < count; i++) {
        uint8_t kind = read_u8(f);
        uint8_t flags = read_u8(f);
        (void)read_u16(f);
        (void)read_u32(f);
        (void)read_u32(f);
        (void)read_i32(f);
        (void)read_i32(f);
        (void)read_u8(f);
        (void)read_i32(f);
        uint8_t activity_len = read_u8(f);
        uint8_t name_len = read_u8(f);
        if (kind < 1 || kind > 9) abort();
        if (flags & 1u) instance_rows++;
        kind_counts[kind]++;
        if (fseek(f, (long)activity_len + (long)name_len, SEEK_CUR) != 0)
            abort();
    }

    if (kind_counts[1] <= 20000u) abort(); /* world_static */
    if (kind_counts[2] <= 10000u) abort(); /* wiki_locline */
    if (kind_counts[3] < 20u) abort();     /* activity_point */
    if (kind_counts[4] < 5u) abort();      /* activity_wave */
    if (kind_counts[5] < 5u) abort();      /* activity_dynamic */
    if (kind_counts[6] < 20u) abort();     /* object_anchor */
    if (kind_counts[7] != 0u) abort();     /* unresolved_required */
    if (kind_counts[8] < 15u) abort();     /* activity_region */
    if (kind_counts[9] < 10u) abort();     /* encounter_dynamic */
    if (instance_rows <= 3000u) abort();

    fclose(f);
    return 0;
}
