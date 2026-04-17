#ifndef RC_NPC_H
#define RC_NPC_H

#include "types.h"

// NPC definition loaded from npc_defs.bin (NDEF format).
// Fully data-driven — no hardcoded NPC logic.
typedef struct {
    int id;                 // b237 cache NPC ID (sparse)
    char name[64];
    int size;               // tile footprint (1-5)
    int combat_level;       // -1 = no combat level
    int hitpoints;          // max HP
    int stats[6];           // att, def, str, hp, rng, mag
    int stand_anim;         // idle animation
    int walk_anim;
    int run_anim;
    int attack_anim;
    int death_anim;
    // Per-NPC AI parameters (set from def + Void data)
    int wander_range;       // max tiles from spawn (default 5)
    int respawn_ticks;      // ticks before respawn after death (default 25)
    bool aggressive;
    int aggro_range;
} RcNpcDef;

// Global NPC definitions table — loaded once at startup
extern RcNpcDef g_npc_defs[RC_MAX_NPC_DEFS];
extern int g_npc_def_count;

// Load NPC definitions from binary NDEF file
int rc_load_npc_defs(const char *path);

// Find a def by NPC ID (b237 cache ID). Returns -1 if not found.
int rc_npc_def_find(int npc_id);

// Load and spawn all NPCs from binary NSPN file
int rc_load_npc_spawns(RcWorld *world, const char *path);

// Spawn a single NPC. Returns NPC array index or -1.
int rc_npc_spawn(RcWorld *world, int def_idx, int world_x, int world_y, int plane);

// Per-tick NPC processing (wander AI, respawn, movement)
void rc_npc_tick(RcWorld *world, RcNpc *npc);

#endif
