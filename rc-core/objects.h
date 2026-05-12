#ifndef RC_OBJECTS_H
#define RC_OBJECTS_H

#include <stdint.h>

#define RC_MAX_OBJECT_ID 65536
#define RC_OBJECT_ACTIONS 5
#define RC_OBJECT_MAX_TRANSFORMS 8

enum {
    RC_OBJ_BEHAVIOR_DOOR      = 1u << 0,
    RC_OBJ_BEHAVIOR_LADDER    = 1u << 1,
    RC_OBJ_BEHAVIOR_STAIR     = 1u << 2,
    RC_OBJ_BEHAVIOR_BANK      = 1u << 3,
    RC_OBJ_BEHAVIOR_ALTAR     = 1u << 4,
    RC_OBJ_BEHAVIOR_RESOURCE  = 1u << 5,
    RC_OBJ_BEHAVIOR_TRANSPORT = 1u << 6,
    RC_OBJ_BEHAVIOR_STORAGE   = 1u << 7,
    RC_OBJ_BEHAVIOR_PAIR_LEFT = 1u << 8,
    RC_OBJ_BEHAVIOR_PAIR_RIGHT = 1u << 9,
};

enum {
    RC_OBJ_SKILL_NONE = 0,
    RC_OBJ_SKILL_WOODCUTTING = 1,
    RC_OBJ_SKILL_MINING = 2,
    RC_OBJ_SKILL_FISHING = 3,
    RC_OBJ_SKILL_FARMING = 4,
    RC_OBJ_SKILL_PRAYER = 5,
};

enum {
    RC_OBJECT_CLIP_BLOCKS_PROJECTILE = 1u << 0,
    RC_OBJECT_CLIP_MODEL_CLIPPED = 1u << 1,
    RC_OBJECT_CLIP_HOLLOW = 1u << 2,
    RC_OBJECT_CLIP_RANDOMIZE_ANIM_START = 1u << 3,
    RC_OBJECT_CLIP_DEFER_ANIM_CHANGE = 1u << 4,
};

typedef struct {
    uint32_t obj_id;
    uint32_t key;
    int32_t value;
} RcObjectParam;

typedef struct {
    int id;
    char name[64];
    char actions[RC_OBJECT_ACTIONS][32];
    int transforms[RC_OBJECT_MAX_TRANSFORMS];
    uint16_t width, length;
    uint8_t interact_type, action_count, model_count, transform_count;
    uint8_t force_approach;
    int varbit, varp, animation_id, map_icon;
    uint32_t flags;
    uint32_t param_first;
    uint16_t param_count;
    uint8_t supports_items;
    uint8_t clip_flags;
    int ambient_sound_id;
    uint16_t ambient_sound_distance;
    uint16_t ambient_sound_retain;
    uint8_t loaded;
} RcObjectDef;

typedef struct {
    uint32_t obj_id;
    uint64_t key;
    uint16_t x, y, mapsquare;
    uint8_t plane, type, rotation, flags;
} RcObjectPlacement;

typedef struct {
    uint32_t flags;
    int next_loc_stage;
    int open_sound;
    int close_sound;
    int climb_anim;
    uint8_t action_mask;
    uint8_t skill;
    uint8_t loaded;
} RcObjectBehavior;

typedef struct {
    uint32_t obj_id;
    uint16_t start_x, start_y, dest_x, dest_y;
    uint8_t start_plane, dest_plane, option, flags;
    char action[32];
    char target[48];
} RcObjectTransport;

extern RcObjectDef g_rc_object_defs[RC_MAX_OBJECT_ID];
extern RcObjectBehavior g_rc_object_behaviors[RC_MAX_OBJECT_ID];
extern RcObjectPlacement *g_rc_object_placements;
extern RcObjectTransport *g_rc_object_transports;
extern RcObjectParam *g_rc_object_params;
extern int g_rc_object_def_count;
extern int g_rc_object_behavior_count;
extern int g_rc_object_placement_count;
extern int g_rc_object_transport_count;
extern int g_rc_object_param_count;

int rc_load_object_defs(const char *path);
int rc_load_object_placements(const char *path);
int rc_load_object_behaviors(const char *path);
int rc_load_object_transports(const char *path);

const RcObjectDef *rc_object_def_get(int obj_id);
int rc_object_def_param_int(int obj_id, int key, int default_value);
const RcObjectBehavior *rc_object_behavior_get(int obj_id);
const RcObjectPlacement *rc_object_region_placements(uint16_t mapsquare,
                                                     int *count);
int rc_object_placements_at(int x, int y, int plane,
                            RcObjectPlacement *out, int max_out);
const RcObjectTransport *rc_object_transport_find(int obj_id, int x, int y,
                                                  int plane, int option);

#endif
