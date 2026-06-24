#ifndef RC_NPC_H
#define RC_NPC_H

#include "types.h"

#define RC_NPC_OPTION_COUNT 5
#define RC_NPC_OPTION_LEN 32

// NPC definition loaded from npc_defs.bin (NDEF format).
// Fully data-driven — no hardcoded NPC logic.
typedef struct {
    int id;                 // b237 cache NPC ID (sparse)
    char name[64];
    int size;               // tile footprint (1-5)
    int combat_level;       // -1 = no combat level
    int hitpoints;          // max HP
    int stats[6];           // att, def, str, hp, rng, mag
    // Per-NPC AI parameters (set from def + Void data)
    int wander_range;       // max tiles from spawn (default 5)
    int respawn_ticks;      // ticks before respawn after death (default 25)
    // Combat / behaviour fields merged from osrsreboxed-db (NDEF v2 onward)
    bool aggressive;
    int aggro_range;
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
    int skipped_instance;
    int skipped_missing_def;
    int skipped_capacity;
    int spawned;
    int source_plane_counts[RC_MAX_PLANES];
    int matched_plane_counts[RC_MAX_PLANES];
    int spawned_plane_counts[RC_MAX_PLANES];
} RcNpcSpawnLoadStats;

// Global NPC definitions table — loaded once at startup
extern RcNpcDef g_npc_defs[RC_MAX_NPC_DEFS];
extern int g_npc_def_count;

// Load NPC definitions from binary NDEF file
int rc_load_npc_defs(const char *path);

// Find a def by NPC ID (b237 cache ID). Returns -1 if not found.
int rc_npc_def_find(int npc_id);
const char *rc_npc_def_option(const RcNpcDef *def, int option_idx);
bool rc_npc_def_option_is_attack(const RcNpcDef *def, int option_idx);

// Load and spawn all NPCs from binary NSPN file
int rc_load_npc_spawns(RcWorld *world, const char *path);
int rc_load_npc_spawns_rect(RcWorld *world, const char *path,
                            int min_x, int min_y, int max_x, int max_y,
                            int min_plane, int max_plane);
int rc_load_npc_spawns_rect_stats(RcWorld *world, const char *path,
                                  int min_x, int min_y,
                                  int max_x, int max_y,
                                  int min_plane, int max_plane,
                                  RcNpcSpawnLoadStats *stats);
int rc_load_npc_spawns_near(RcWorld *world, const char *path,
                            int center_x, int center_y, int radius,
                            int plane);

// Spawn a single NPC. Returns NPC array index or -1.
int rc_npc_spawn(RcWorld *world, int def_idx, int world_x, int world_y, int plane);

// Per-tick NPC processing (wander AI, respawn, movement)
void rc_npc_tick(RcWorld *world, RcNpc *npc);

#endif
