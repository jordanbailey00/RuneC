#ifndef RC_ACTIVITY_SCHEMAS_H
#define RC_ACTIVITY_SCHEMAS_H

#include <stdbool.h>
#include <stdint.h>

#define RC_ACTIVITY_SCHEMA_MAX_NPCS 64
#define RC_ACTIVITY_SCHEMA_MAX_OBJECTS 64

enum {
    RC_ACTIVITY_SCHEMA_READY = 1,
    RC_ACTIVITY_SCHEMA_READY_SIMPLIFIED = 2,
    RC_ACTIVITY_SCHEMA_BLOCKS_PARITY = 3,
};

enum {
    RC_ACTIVITY_SCHEMA_CLASS_UNKNOWN = 0,
    RC_ACTIVITY_SCHEMA_CLASS_ENCOUNTER = 1,
    RC_ACTIVITY_SCHEMA_CLASS_ARENA_LOCAL = 2,
    RC_ACTIVITY_SCHEMA_CLASS_WAVE = 3,
    RC_ACTIVITY_SCHEMA_CLASS_OBJECT = 4,
    RC_ACTIVITY_SCHEMA_CLASS_SCRIPT_SPAWN = 5,
};

enum {
    RC_ACTIVITY_SCHEMA_ENCOUNTER      = 1u << 0,
    RC_ACTIVITY_SCHEMA_STATE_MACHINE  = 1u << 1,
    RC_ACTIVITY_SCHEMA_SPAWN_DATA     = 1u << 2,
    RC_ACTIVITY_SCHEMA_MECHANICS      = 1u << 3,
    RC_ACTIVITY_SCHEMA_WAVES          = 1u << 4,
    RC_ACTIVITY_SCHEMA_ROOMS          = 1u << 5,
    RC_ACTIVITY_SCHEMA_REWARDS        = 1u << 6,
    RC_ACTIVITY_SCHEMA_REQUIREMENTS   = 1u << 7,
    RC_ACTIVITY_SCHEMA_COMPLETION     = 1u << 8,
    RC_ACTIVITY_SCHEMA_INSTANCE       = 1u << 9,
    RC_ACTIVITY_SCHEMA_OBJECTS        = 1u << 10,
    RC_ACTIVITY_SCHEMA_UNRESOLVED     = 1u << 11,
    RC_ACTIVITY_SCHEMA_V1_REQUIRED    = 1u << 12,
};

typedef struct {
    char slug[64];
    char name[64];
    uint8_t status;
    uint8_t class_id;
    uint8_t kind;
    uint32_t flags;
    uint16_t npc_count;
    uint16_t object_count;
    uint16_t object_id_count;
    uint16_t spawn_point_count;
    uint16_t spawn_region_count;
    uint16_t dynamic_spawn_count;
    uint16_t wave_spawn_count;
    uint16_t object_anchor_count;
    uint16_t safe_tile_count;
    uint16_t unresolved_count;
    uint16_t state_count;
    uint16_t transition_count;
    uint16_t param_count;
    uint16_t attack_count;
    uint16_t phase_count;
    uint16_t mechanic_count;
    uint16_t room_count;
    uint16_t reward_count;
    uint16_t requirement_count;
    uint16_t min_x, max_x, min_y, max_y;
    uint8_t min_plane, max_plane;
    uint32_t npc_ids[RC_ACTIVITY_SCHEMA_MAX_NPCS];
    uint32_t object_ids[RC_ACTIVITY_SCHEMA_MAX_OBJECTS];
} RcActivitySchema;

extern RcActivitySchema *g_rc_activity_schemas;
extern int g_rc_activity_schema_count;

int rc_load_activity_schemas(const char *path);
void rc_activity_schemas_rebuild_index(void);
int rc_activity_schema_find_slug(const char *slug);
int rc_activity_schema_find_for_npc(uint32_t npc_id);
bool rc_activity_schema_has_npc(int idx, uint32_t npc_id);
int rc_activity_schema_find_for_object(uint32_t object_id);
bool rc_activity_schema_has_object(int idx, uint32_t object_id);
bool rc_activity_schema_blocks_parity(int idx);

#endif
