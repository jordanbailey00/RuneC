#ifndef RC_NPC_H
#define RC_NPC_H

#include "types.h"

#define RC_NPC_OPTION_COUNT 5
#define RC_NPC_OPTION_LEN 32
#define RC_NPC_SPAWN_WANDER_USE_DEF 255

typedef enum {
    RC_NPC_HUNT_NONE = 0,
    RC_NPC_HUNT_PLAYER,
} RcNpcHuntTarget;

typedef enum {
    RC_NPC_HUNT_VIS_NONE = 0,
    RC_NPC_HUNT_VIS_LINE_OF_SIGHT,
    RC_NPC_HUNT_VIS_LINE_OF_WALK,
} RcNpcHuntVisibility;

typedef enum {
    RC_NPC_HUNT_STRENGTH_ANY = 0,
    RC_NPC_HUNT_STRENGTH_OUTSIDE_WILDERNESS,
} RcNpcHuntStrength;

enum {
    RC_NPC_HUNT_CHECK_NOT_BUSY = 1u << 0,
    RC_NPC_HUNT_KEEP_HUNTING = 1u << 1,
};

typedef struct {
    uint8_t target;
    uint8_t visibility;
    uint8_t strength;
    uint8_t flags;
    uint8_t range;
    uint8_t rate;
} RcNpcHuntPolicy;

// NPC definition loaded from npc_defs.bin (NDEF format).
// Fully data-driven — no hardcoded NPC logic.
typedef struct {
    int id;                 // b237 cache NPC ID (sparse)
    char name[64];
    int size;               // tile footprint (1-5)
    int combat_level;       // -1 = no combat level
    int hitpoints;          // max HP
    int stats[6];           // att, def, str, hp, rng, mag
    // RuneC-owned lifecycle and behavior policy. Zero is explicit.
    int wander_range;       // 0 = stationary
    int respawn_ticks;      // 0 = no automatic respawn
    int regen_ticks;        // 0 = no live stat regeneration
    RcNpcHuntPolicy hunt;
    int transform_varbit;
    int transform_varp;
    uint32_t transform_offset;
    uint16_t transform_count;
    // Combat fields carried by NDEF v2+ data.
    int max_hit;            // 0 = non-combat
    int attack_speed;       // ticks between attacks; 0 = non-combat
    int slayer_level;       // level required to damage; 1 = always
    char options[RC_NPC_OPTION_COUNT][RC_NPC_OPTION_LEN]; // cache action slots
    int attack_types;       // bitfield: 0x1 stab 0x2 slash 0x4 crush 0x8 magic 0x10 ranged
    int weakness;           // bitfield: 0x1 fire 0x2 water 0x4 earth 0x8 air
                            //           0x10 stab 0x20 slash 0x40 crush 0x80 ranged/magic
    bool poison_immune;
    bool venom_immune;
} RcNpcDef;

typedef struct {
    int total_rows;
    int matched_filter;
    int skipped_filter;
    int skipped_invalid;
    int skipped_instance;
    int skipped_missing_def;
    int skipped_capacity;
    int reused_existing;
    int spawned;
    int pages_loaded;
    int rows_loaded;
    int source_plane_counts[RC_MAX_PLANES];
    int matched_plane_counts[RC_MAX_PLANES];
    int spawned_plane_counts[RC_MAX_PLANES];
} RcNpcSpawnLoadStats;

enum {
    RC_NPC_SPAWN_LOAD_INCLUDE_INSTANCE = 1u << 0,
};

// Global NPC definitions table — loaded once at startup
extern RcNpcDef g_npc_defs[RC_MAX_NPC_DEFS];
extern int g_npc_def_count;

// Load NPC definitions using schema/defs/npc_defs.schema.toml.
int rc_load_npc_defs(const char *path);
int rc_load_npc_defs_into(const char *path, RcNpcDef *defs, int max_defs,
                          int *out_count, int *def_by_id,
                          int max_def_by_id, int32_t **out_transforms,
                          int *out_transform_count);
void rc_npc_use_defs(const RcNpcDef *defs, int count,
                     const int *def_by_id, const int32_t *transforms,
                     int transform_count);
int rc_npc_mirror_defs_to_globals(const RcNpcDef *defs, int count,
                                  const int *def_by_id,
                                  const int32_t *transforms,
                                  int transform_count);
void rc_npc_reset_defs_if_active(const RcNpcDef *defs);

// Find a def by NPC ID (b237 cache ID). Returns -1 if not found.
int rc_npc_def_find(int npc_id);
int rc_npc_def_find_name(const char *name);
const RcNpcDef *rc_npc_def_get(int def_idx);
const RcNpcDef *rc_npc_base_def_for_npc(const RcNpc *npc);
const RcNpcDef *rc_npc_def_for_npc(const RcWorld *world, const RcNpc *npc);
// Encounter-owned form override (def index, -1 = base). Each new life
// starts in the base form; HP and other life state stay with the caller.
int rc_npc_set_form(RcNpc *npc, int def_idx);
int rc_npc_def_collect_form_ids(const RcNpcDef *def, int *out_ids,
                                int max_ids);
const RcNpcDef *rc_npc_defs_all(int *count);
const char *rc_npc_def_option(const RcNpcDef *def, int option_idx);
bool rc_npc_def_option_is_attack(const RcNpcDef *def, int option_idx);

// Load NPCs from the mapsquare-indexed NSPI binary.
int rc_load_npc_spawns(RcWorld *world, const char *path);
int rc_load_npc_spawns_rect(RcWorld *world, const char *path,
                            int min_x, int min_y, int max_x, int max_y,
                            int min_plane, int max_plane);
int rc_load_npc_spawns_rect_stats(RcWorld *world, const char *path,
                                  int min_x, int min_y,
                                  int max_x, int max_y,
                                  int min_plane, int max_plane,
                                  RcNpcSpawnLoadStats *stats);
int rc_load_npc_spawns_rect_stats_flags(RcWorld *world, const char *path,
                                        int min_x, int min_y,
                                        int max_x, int max_y,
                                        int min_plane, int max_plane,
                                        uint32_t load_flags,
                                        RcNpcSpawnLoadStats *stats);
int rc_load_npc_spawns_near(RcWorld *world, const char *path,
                            int center_x, int center_y, int radius,
                            int plane);

// Spawn a single NPC. Returns NPC array index or -1.
int rc_npc_spawn(RcWorld *world, int def_idx, int world_x, int world_y, int plane);

typedef enum {
    RC_NPC_SPAWN_CREATED = 0,
    RC_NPC_SPAWN_EXISTING,
    RC_NPC_SPAWN_INVALID,
    RC_NPC_SPAWN_MISSING_DEF,
    RC_NPC_SPAWN_CAPACITY,
} RcNpcSpawnStatus;

enum {
    RC_NPC_SPAWN_RESPAWNS = 1u << 0,
};

typedef struct {
    uint64_t spawn_key;
    int wander_range;       // -1 = definition value; 0 = stationary
    uint8_t direction;      // RSMod/cache direction id 0..7
    uint8_t flags;
} RcNpcSpawnConfig;

typedef struct {
    RcNpcSpawnStatus status;
    int slot;
    RcNpcId uid;
} RcNpcSpawnResult;

RcNpcSpawnResult rc_npc_spawn_ex(RcWorld *world, int def_idx, int world_x,
                                 int world_y, int plane,
                                 const RcNpcSpawnConfig *config);
RcNpc *rc_npc_resolve(RcWorld *world, RcNpcId uid);
const RcNpc *rc_npc_resolve_const(const RcWorld *world, RcNpcId uid);
int rc_npc_remove(RcWorld *world, RcNpcId uid);
void rc_npc_clear_references(RcWorld *world, RcNpcId uid);
RcNpcLifePhase rc_npc_life_phase(const RcNpc *npc);
void rc_npc_reset_life(RcWorld *world, RcNpc *npc);
// Drops any face entity and turns the NPC to its spawn direction.
void rc_npc_face_spawn_direction(RcNpc *npc);

// Ordinary movement uses direct steps; only explicit scripted routes use BFS.
bool rc_npc_route_request(RcWorld *world, RcNpc *npc,
                          const RcRouteTarget *target, RcNpcRouteMode mode,
                          bool allow_alternative);
void rc_npc_route_clear(RcNpc *npc, RcMovementResult result);
// Whether an NPC of `size` at (x, y) walking straight at `target` (the
// direct steps NPCs take: never a detour) arrives, as its own steps would,
// in at most `max_steps` steps. A melee target is arrived at only from a
// side with no wall between.
bool rc_npc_direct_walk_reaches(const RcWorldMap *map, int plane, int x,
                                int y, int size, const RcRouteTarget *target,
                                int max_steps);
void rc_npc_movement_tick(RcWorld *world, RcNpc *npc);
bool rc_npc_apply_poison(RcWorld *world, RcNpc *npc, int damage);
void rc_npc_status_tick(RcWorld *world, RcNpc *npc);

// Per-tick NPC processing (wander AI, respawn, movement)
void rc_npc_tick(RcWorld *world, RcNpc *npc);

#endif
