#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "assets.h"

#ifndef RC_TEST_SOURCE_DIR
#define RC_TEST_SOURCE_DIR "."
#endif

#define ACQS_MAGIC 0x53514341u
#define ACQS_VERSION 1u

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

int main(void) {
    char path[512];
    snprintf(path, sizeof(path), "%s/data/defs/acquisition_sources.bin",
             RC_TEST_SOURCE_DIR);
    FILE *f = rc_asset_fopen(path, "rb");
    if (!f) abort();

    if (read_u32(f) != ACQS_MAGIC) abort();
    if (read_u32(f) != ACQS_VERSION) abort();
    uint32_t count = read_u32(f);
    if (count <= 1000) abort();

    uint32_t kind_counts[6] = {0};
    for (uint32_t i = 0; i < count; i++) {
        uint8_t kind = read_u8(f);
        uint8_t class_len = read_u8(f);
        (void)read_u32(f); /* ref_id */
        uint32_t entries = read_u32(f);
        uint16_t name_len = read_u16(f);
        if (kind < 1 || kind > 5) abort();
        if (entries >= 1000000u) abort();
        kind_counts[kind]++;
        if (fseek(f, (long)class_len + (long)name_len, SEEK_CUR) != 0)
            abort();
    }

    if (kind_counts[1] <= 500) abort();  /* NPC drops */
    if (kind_counts[2] <= 500) abort();  /* non-NPC acquisition */
    if (kind_counts[3] <= 400) abort();  /* shops */
    if (kind_counts[4] <= 1000) abort(); /* recipes */
    if (kind_counts[5] != 3) abort();    /* RDT/GDT/MRDT */

    rc_asset_close(f);
    return 0;
}
