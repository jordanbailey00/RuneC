#ifndef RC_AREA_FLAGS_H
#define RC_AREA_FLAGS_H

#include <stdint.h>

#define RC_AREA_SOURCE_NEAR_REALITY_ZENYTE 1u
#define RC_AREA_MAPSQUARE_COUNT 65536

enum {
    RC_AREA_MULTICOMBAT           = 1u << 0,
    RC_AREA_WILDERNESS            = 1u << 1,
    RC_AREA_WILDERNESS_LEVEL_LINE = 1u << 2,
    RC_AREA_DEADMAN_SAFE          = 1u << 3,
    RC_AREA_PVP_SAFE              = 1u << 4,
    RC_AREA_SINGLES_PLUS          = 1u << 5,
};

typedef struct {
    uint16_t x, y;
} RcAreaPoint;

typedef struct {
    uint8_t plane;
    uint8_t authoritative_osrs;
    uint16_t vertex_count;
    uint32_t set_flags;
    uint32_t clear_flags;
    uint16_t min_x, min_y, max_x, max_y;
    uint16_t value;
    uint16_t source_id;
    RcAreaPoint *points;
} RcAreaFlagRow;

typedef struct {
    uint32_t first, count;
} RcAreaRange;

typedef struct {
    uint16_t mapsquare;
    uint8_t flags[4][64][64];
} RcAreaTileRegion;

typedef struct {
    RcAreaFlagRow *rows;
    RcAreaPoint *points;
    uint32_t point_count;
    uint32_t *refs;
    uint32_t ref_count;
    RcAreaRange index[RC_AREA_MAPSQUARE_COUNT];
    RcAreaTileRegion *tiles;
    int tile_count;
    int tile_index[RC_AREA_MAPSQUARE_COUNT];
    int row_count;
} RcAreaFlagData;

extern RcAreaFlagRow *g_rc_area_flags;
extern int g_rc_area_flag_count;

int rc_load_area_flags(const char *path);
void rc_area_flag_data_init(RcAreaFlagData *data);
void rc_area_flag_data_free(RcAreaFlagData *data);
int rc_load_area_flags_into(const char *path, RcAreaFlagData *data);
int rc_area_flags_mirror_to_globals(const RcAreaFlagData *data);
void rc_area_flags_use_data(const RcAreaFlagData *data);
void rc_area_flags_reset_data_if_active(const RcAreaFlagData *data);
int rc_area_flags_is_loaded(void);
uint32_t rc_area_flags_at(int x, int y, int plane);
int rc_area_flag_rows_at(int x, int y, int plane);
int rc_wilderness_level_at(int x, int y, int plane);
int rc_area_flags_all_provisional(void);

#endif
