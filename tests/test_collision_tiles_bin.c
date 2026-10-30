#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "assets.h"

#define CTPI_MAGIC 0x49505443u
#define CTPI_VERSION 1u
#define CTPI_RECORD_SIZE 8u
#define MAPSQUARE_COUNT 65536u

static uint8_t read_u8(FILE *f) {
    int c = fgetc(f);
    if (c == EOF) abort();
    return (uint8_t)c;
}

static uint32_t read_u32(FILE *f) {
    uint32_t b0 = read_u8(f), b1 = read_u8(f);
    uint32_t b2 = read_u8(f), b3 = read_u8(f);
    return b0 | (b1 << 8) | (b2 << 16) | (b3 << 24);
}

int main(void) {
    char path[512];
    snprintf(path, sizeof(path),
             "%s/data/regions/world.collision-tiles.indexed.bin",
             RC_TEST_SOURCE_DIR);
    FILE *f = rc_asset_fopen(path, "rb");
    if (!f) abort();

    if (read_u32(f) != CTPI_MAGIC || read_u32(f) != CTPI_VERSION) abort();
    uint32_t row_count = read_u32(f);
    if (read_u32(f) != CTPI_RECORD_SIZE) abort();
    uint32_t occupied_pages = read_u32(f);
    uint32_t expected_plane_counts[4];
    for (int plane = 0; plane < 4; plane++)
        expected_plane_counts[plane] = read_u32(f);
    // b241's map plus the squares composed for the CoX layout.
    if (row_count != 6510902u || occupied_pages != 2411u) abort();

    static uint32_t page_counts[MAPSQUARE_COUNT];
    uint32_t expected_first = 0;
    uint32_t counted_pages = 0;
    for (uint32_t mapsquare = 0; mapsquare < MAPSQUARE_COUNT; mapsquare++) {
        uint32_t first = read_u32(f);
        uint32_t count = read_u32(f);
        if (first != expected_first || count > 4u * 64u * 64u) abort();
        page_counts[mapsquare] = count;
        expected_first += count;
        counted_pages += count > 0;
    }
    if (expected_first != row_count || counted_pages != occupied_pages) abort();

    uint32_t plane_counts[4] = {0};
    uint32_t known_blocked = 0, known_bridge_wall = 0;
    for (uint32_t mapsquare = 0; mapsquare < MAPSQUARE_COUNT; mapsquare++) {
        uint32_t last_key = 0;
        for (uint32_t i = 0; i < page_counts[mapsquare]; i++) {
            uint8_t local_x = read_u8(f);
            uint8_t local_y = read_u8(f);
            uint8_t plane = read_u8(f);
            uint8_t pad = read_u8(f);
            uint32_t flags = read_u32(f);
            uint32_t key = (uint32_t)plane * 4096u
                         + (uint32_t)local_x * 64u + local_y;
            if (local_x >= 64 || local_y >= 64 || plane >= 4
                    || pad != 0 || flags == 0 || (i > 0 && key <= last_key)) {
                abort();
            }
            last_key = key;
            plane_counts[plane]++;
            if (mapsquare == ((48u << 8) | 53u)
                    && local_x == 0 && local_y == 3 && plane == 0
                    && (flags & 0x200000u)) {
                known_blocked++;
            }
            if (mapsquare == ((48u << 8) | 53u)
                    && local_x == 33 && local_y == 28 && plane == 0
                    && (flags & 0x20u)) {
                known_bridge_wall++;
            }
        }
    }
    if (fgetc(f) != EOF) abort();
    rc_asset_close(f);
    for (int plane = 0; plane < 4; plane++)
        if (plane_counts[plane] != expected_plane_counts[plane]) abort();
    if (expected_plane_counts[0] != 3911503u
            || expected_plane_counts[1] != 1427173u
            || expected_plane_counts[2] != 778099u
            || expected_plane_counts[3] != 394127u
            || known_blocked != 1u || known_bridge_wall != 1u) {
        abort();
    }
    return 0;
}
