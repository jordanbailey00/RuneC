// Diagnostic: load actual varrock.cmap and verify collision works
#include "../rc-core/types.h"
#include "../rc-core/pathfinding.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CMAP_MAGIC 0x434D4150

static int load_cmap(RcWorldMap *map, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) { printf("FAIL: can't open %s\n", path); return -1; }
    uint32_t magic, version, count;
    fread(&magic, 4, 1, f);
    fread(&version, 4, 1, f);
    fread(&count, 4, 1, f);
    if (magic != CMAP_MAGIC) { printf("FAIL: bad magic\n"); fclose(f); return -1; }
    printf("cmap: %u regions\n", count);

    for (uint32_t i = 0; i < count && map->region_count < RC_MAX_REGIONS; i++) {
        int32_t ms;
        fread(&ms, 4, 1, f);
        int rx = (ms >> 8) & 0xFF, ry = ms & 0xFF;
        RcRegion *reg = &map->regions[map->region_count];
        reg->region_x = rx;
        reg->region_y = ry;
        reg->loaded = 1;
        for (int h = 0; h < 4; h++)
            for (int x = 0; x < 64; x++)
                for (int y = 0; y < 64; y++) {
                    int32_t flags;
                    fread(&flags, 4, 1, f);
                    reg->tiles[h][x][y].collision_flags = (uint32_t)flags;
                }
        map->region_count++;
    }
    fclose(f);
    printf("Loaded %d regions\n", map->region_count);
    return 0;
}

int main(void) {
    RcWorldMap map = {0};
    if (load_cmap(&map, "data/regions/varrock.cmap") < 0) return 1;

    // Test 1: Check region lookup
    printf("\n--- Region Lookup ---\n");
    uint32_t f50_53 = rc_get_flags(&map, 3213, 3428, 0);
    printf("Player start (3213,3428): flags=0x%08X\n", f50_53);

    // Test 2: Find some blocked tiles in region (50,53)
    printf("\n--- Scanning region (50,53) for blocked tiles ---\n");
    int blocked = 0, walls = 0;
    for (int x = 0; x < 64; x++) {
        for (int y = 0; y < 64; y++) {
            uint32_t f = rc_get_flags(&map, 50*64 + x, 53*64 + y, 0);
            if (f & 0x200000) blocked++;
            if (f & 0xFF) walls++;
        }
    }
    printf("Region (50,53): %d blocked tiles, %d wall tiles\n", blocked, walls);

    // Test 3: Check known water area — region (48,53)
    printf("\n--- Scanning region (48,53) for blocked tiles ---\n");
    blocked = 0;
    for (int x = 0; x < 64; x++) {
        for (int y = 0; y < 64; y++) {
            uint32_t f = rc_get_flags(&map, 48*64 + x, 53*64 + y, 0);
            if (f & 0x200000) blocked++;
        }
    }
    printf("Region (48,53): %d blocked tiles\n", blocked);

    // Test 4: Check rc_can_move on a known blocked tile
    // Find first blocked tile near player and test movement into it
    printf("\n--- Movement checks near player (3213,3428) ---\n");
    for (int dx = -5; dx <= 5; dx++) {
        for (int dy = -5; dy <= 5; dy++) {
            int tx = 3213 + dx, ty = 3428 + dy;
            uint32_t f = rc_get_flags(&map, tx, ty, 0);
            if (f != 0) {
                int can_n = rc_can_move(&map, tx, ty-1, 0, 1, 0);
                int can_s = rc_can_move(&map, tx, ty+1, 0, -1, 0);
                int can_e = rc_can_move(&map, tx-1, ty, 1, 0, 0);
                int can_w = rc_can_move(&map, tx+1, ty, -1, 0, 0);
                printf("  (%d,%d) flags=0x%08X  enter_from: N=%d S=%d E=%d W=%d\n",
                       tx, ty, f, can_n, can_s, can_e, can_w);
            }
        }
    }

    // Test 5: Check a tile right next to a building wall
    printf("\n--- Palace wall area (50,54) sample ---\n");
    for (int x = 0; x < 64; x++) {
        for (int y = 0; y < 10; y++) {
            uint32_t f = rc_get_flags(&map, 50*64 + x, 54*64 + y, 0);
            if (f & 0xFF) {
                printf("  world(%d,%d) local(%d,%d) flags=0x%08X\n",
                       50*64+x, 54*64+y, x, y, f);
            }
        }
    }

    printf("\nDone.\n");
    return 0;
}
