#ifndef RC_ACTIVITY_SPAWNS_H
#define RC_ACTIVITY_SPAWNS_H

#include "types.h"

#include <stdbool.h>
#include <stdint.h>

#define RC_ACTIVITY_SPAWN_NO_ID UINT32_MAX

enum {
    RC_ACTIVITY_SPAWN_POINT = 1,
    RC_ACTIVITY_SPAWN_REGION = 2,
    RC_ACTIVITY_SPAWN_DYNAMIC = 3,
    RC_ACTIVITY_SPAWN_WAVE_POINT = 4,
    RC_ACTIVITY_SPAWN_WAVE_REGION_REF = 5,
    RC_ACTIVITY_SPAWN_OBJECT_ANCHOR = 6,
    RC_ACTIVITY_SPAWN_SAFE_TILE = 7,
    RC_ACTIVITY_SPAWN_UNRESOLVED = 8,
};

enum {
    RC_ACTIVITY_SPAWN_HAS_POINT = 1u << 0,
    RC_ACTIVITY_SPAWN_HAS_REGION = 1u << 1,
    RC_ACTIVITY_SPAWN_HAS_NPC = 1u << 2,
    RC_ACTIVITY_SPAWN_HAS_OBJECT = 1u << 3,
    RC_ACTIVITY_SPAWN_HAS_WAVE = 1u << 4,
    RC_ACTIVITY_SPAWN_IS_DYNAMIC = 1u << 5,
    RC_ACTIVITY_SPAWN_REQUIRED = 1u << 6,
};

typedef struct {
    char slug[64];
    char key[48];
    char entity[64];
    char ref[48];
    uint8_t kind;
    uint8_t plane;
    uint8_t rotation;
    uint8_t random_offset;
    uint16_t flags;
    uint16_t wave;
    uint16_t x, y;
    uint16_t min_x, max_x, min_y, max_y;
    uint16_t local_x, local_y;
    uint32_t npc_id;
    uint32_t object_id;
} RcActivitySpawn;

extern RcActivitySpawn *g_rc_activity_spawns;
extern int g_rc_activity_spawn_count;

int rc_load_activity_spawns(const char *path);
void rc_activity_spawns_rebuild_index(void);
const RcActivitySpawn *rc_activity_spawns_for(const char *slug, int *count);
const RcActivitySpawn *rc_activity_spawn_find_key(const char *slug,
                                                  uint8_t kind,
                                                  const char *key);
const RcActivitySpawn *rc_activity_spawn_find_object_at(const char *slug,
                                                        uint32_t object_id,
                                                        int x, int y,
                                                        int plane);
const RcActivitySpawn *rc_activity_spawn_wave_region(const char *slug,
                                                     uint16_t wave,
                                                     int rotation);
bool rc_activity_spawn_region_contains(const char *slug, const char *key,
                                       int x, int y, int plane);
int rc_activity_spawn_count_kind(const char *slug, uint8_t kind);
bool rc_activity_spawn_has_unresolved(const char *slug);
int rc_activity_spawn_materialize_npcs(RcWorld *world, const char *slug,
                                       uint8_t kind);
int rc_activity_spawn_materialize_wave_npcs(RcWorld *world, const char *slug,
                                            uint16_t wave);

#endif
