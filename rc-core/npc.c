#include "npc.h"
#include "rng.h"
#include <string.h>

RcNpcDef g_npc_defs[RC_MAX_NPC_DEFS];
int g_npc_def_count = 0;

int rc_load_npc_defs(const char *path) {
    // TODO: load from binary file
    (void)path;
    return 0;
}

int rc_npc_spawn(RcWorld *world, int def_id, int x, int y, int plane) {
    if (world->npc_count >= RC_MAX_NPCS) return -1;
    if (def_id < 0 || def_id >= g_npc_def_count) return -1;

    RcNpc *npc = &world->npcs[world->npc_count];
    memset(npc, 0, sizeof(RcNpc));
    npc->def_id = def_id;
    npc->uid = world->npc_count;
    npc->x = x;
    npc->y = y;
    npc->plane = plane;
    npc->spawn_x = x;
    npc->spawn_y = y;
    npc->prev_x = x;
    npc->prev_y = y;
    npc->current_hp = g_npc_defs[def_id].hitpoints;
    npc->target_uid = -1;
    npc->active = true;

    return world->npc_count++;
}

void rc_npc_tick(RcWorld *world, RcNpc *npc) {
    if (!npc->active) return;

    npc->prev_x = npc->x;
    npc->prev_y = npc->y;

    // Dead: count down death timer, then start respawn
    if (npc->is_dead) {
        if (npc->death_timer > 0) {
            npc->death_timer--;
        } else if (npc->respawn_timer > 0) {
            npc->respawn_timer--;
        } else {
            // Respawn
            npc->x = npc->spawn_x;
            npc->y = npc->spawn_y;
            npc->prev_x = npc->x;
            npc->prev_y = npc->y;
            npc->current_hp = g_npc_defs[npc->def_id].hitpoints;
            npc->is_dead = false;
            npc->target_uid = -1;
            npc->num_pending_hits = 0;
        }
        return;
    }

    // Decrement attack timer
    if (npc->attack_timer > 0) npc->attack_timer--;

    // TODO: aggro, wander, chase, attack
    (void)world;
}
