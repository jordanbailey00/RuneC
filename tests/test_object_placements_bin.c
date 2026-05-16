#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "assets.h"

#ifndef RC_TEST_SOURCE_DIR
#define RC_TEST_SOURCE_DIR "."
#endif

#define OPLC_MAGIC 0x434c504fu
#define OPLC_VERSION_MIN 1u
#define OPLC_VERSION_MAX 2u

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

static uint64_t read_u64(FILE *f) {
    uint64_t lo = read_u32(f);
    uint64_t hi = read_u32(f);
    return lo | (hi << 32);
}

int main(void) {
    char path[512];
    snprintf(path, sizeof(path), "%s/data/defs/object_placements.bin",
             RC_TEST_SOURCE_DIR);
    FILE *f = rc_asset_fopen(path, "rb");
    if (!f) abort();

    if (read_u32(f) != OPLC_MAGIC) abort();
    uint32_t version = read_u32(f);
    if (version < OPLC_VERSION_MIN || version > OPLC_VERSION_MAX) abort();
    uint32_t count = read_u32(f);
    uint32_t region_count = read_u32(f);
    if (count < 4700000u) abort();
    if (region_count < 2400u) abort();

    uint32_t plane_counts[4] = {0};
    uint32_t type_counts[64] = {0};
    uint32_t tree_count = 0, bank_count = 0, door_count = 0, ladder_count = 0;
    uint32_t museum_stair_scene0 = 0, museum_stair_raw_plane1 = 0;
    uint8_t seen_ids[70000] = {0};
    uint32_t unique_ids = 0;

    for (uint32_t i = 0; i < count; i++) {
        uint32_t obj_id = read_u32(f);
        uint64_t placement_key = version >= 2u ? read_u64(f) : 0u;
        uint16_t x = read_u16(f);
        uint16_t y = read_u16(f);
        uint16_t mapsquare = read_u16(f);
        uint8_t plane = read_u8(f);
        uint8_t type = read_u8(f);
        uint8_t rotation = read_u8(f);
        (void)read_u8(f);

        if (x == 0 || y == 0 || mapsquare == 0) abort();
        if (version >= 2u && placement_key == 0u) abort();
        if (plane > 3 || type > 63 || rotation > 3) abort();
        plane_counts[plane]++;
        type_counts[type]++;

        if (obj_id < sizeof(seen_ids) && !seen_ids[obj_id]) {
            seen_ids[obj_id] = 1;
            unique_ids++;
        }
        if (obj_id == 1276u) tree_count++;
        if (obj_id == 10355u) bank_count++;
        if (obj_id == 1535u) door_count++;
        if (obj_id == 42207u) ladder_count++;
        if (obj_id == 24427u && x == 1758u && y == 4959u) {
            if (plane == 0u) museum_stair_scene0++;
            if (plane == 1u) museum_stair_raw_plane1++;
        }
    }
    rc_asset_close(f);

    if (unique_ids < 39000u) abort();
    if (plane_counts[0] < 3200000u) abort();
    if (plane_counts[1] < 900000u) abort();
    if (type_counts[10] < 2200000u) abort();
    if (type_counts[22] < 1500000u) abort();
    if (tree_count < 3500u) abort();
    if (bank_count < 50u) abort();
    if (door_count < 250u) abort();
    if (ladder_count < 280u) abort();
    if (museum_stair_scene0 != 1u) abort();
    if (museum_stair_raw_plane1 != 0u) abort();

    return 0;
}
