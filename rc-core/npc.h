#ifndef RC_NPC_H
#define RC_NPC_H

#include "types.h"

// NPC definition (loaded from cache/data at startup)
typedef struct {
    int id;
    char name[64];
    int combat_level;
    int size;
    int hitpoints;
    int stats[6];           // atk, def, str, hp, rng, mag
    int attack_speed;
    int attack_style;
    int attack_range;
    int max_hit;
    int attack_anim;
    int death_anim;
    int walk_anim;
    int idle_anim;
    int model_ids[8];
    int model_count;
    char options[5][32];
    bool aggressive;
    int aggro_range;
    int wander_range;
    int respawn_ticks;
} RcNpcDef;

// Global NPC definitions table
extern RcNpcDef g_npc_defs[RC_MAX_NPC_DEFS];
extern int g_npc_def_count;

// Load NPC definitions from binary file
int rc_load_npc_defs(const char *path);

// Per-tick NPC processing
void rc_npc_tick(RcWorld *world, RcNpc *npc);

// Spawn an NPC at a location
int rc_npc_spawn(RcWorld *world, int def_id, int x, int y, int plane);

#endif
