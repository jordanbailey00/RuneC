#include "npc.h"
#include "rng.h"
#include "pathfinding.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

RcNpcDef g_npc_defs[RC_MAX_NPC_DEFS];
int g_npc_def_count = 0;

// Load NDEF binary: magic(u32) version(u32) count(u32)
// Per NPC: id(u32) size(u8) combat_level(i16) hitpoints(u16)
//          stats[6](u16) anims(i32 x 5) name_len(u8) name[name_len]
#define NDEF_MAGIC 0x4E444546

int rc_load_npc_defs(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "npc_defs: can't open %s\n", path); return -1; }

    uint32_t magic, version, count;
    if (fread(&magic, 4, 1, f) != 1 || magic != NDEF_MAGIC) {
        fclose(f);
        fprintf(stderr, "npc_defs: bad magic\n");
        return -1;
    }
    fread(&version, 4, 1, f);
    fread(&count, 4, 1, f);

    int loaded = 0;
    for (uint32_t i = 0; i < count && g_npc_def_count < RC_MAX_NPC_DEFS; i++) {
        RcNpcDef *d = &g_npc_defs[g_npc_def_count];
        memset(d, 0, sizeof(RcNpcDef));
        uint32_t id;
        uint8_t size, name_len;
        int16_t cl;
        uint16_t hp;
        uint16_t stats[6];
        int32_t anims[5];

        fread(&id, 4, 1, f);
        fread(&size, 1, 1, f);
        fread(&cl, 2, 1, f);
        fread(&hp, 2, 1, f);
        fread(stats, 2, 6, f);
        fread(anims, 4, 5, f);
        fread(&name_len, 1, 1, f);
        if (name_len > 63) name_len = 63;
        fread(d->name, 1, name_len, f);
        d->name[name_len] = 0;

        d->id = (int)id;
        d->size = size > 0 ? size : 1;
        d->combat_level = cl;
        d->hitpoints = hp;
        for (int j = 0; j < 6; j++) d->stats[j] = stats[j];
        d->stand_anim = anims[0];
        d->walk_anim = anims[1];
        d->run_anim = anims[2];
        d->attack_anim = anims[3];
        d->death_anim = anims[4];
        d->wander_range = 5;
        d->respawn_ticks = 25;
        d->aggressive = false;
        d->aggro_range = 0;

        g_npc_def_count++;
        loaded++;
    }
    fclose(f);
    fprintf(stderr, "npc_defs: loaded %d defs from %s\n", loaded, path);
    return loaded;
}

int rc_npc_def_find(int npc_id) {
    // Linear scan — defs are sparse sorted by ID.
    // ~300 defs, called once per spawn at startup, so no binary search needed.
    for (int i = 0; i < g_npc_def_count; i++) {
        if (g_npc_defs[i].id == npc_id) return i;
    }
    return -1;
}

// Load NSPN binary: magic(u32) version(u32) count(u32)
// Per spawn: npc_id(u32) x(i32) y(i32) plane(u8) direction(u8) wander_range(u8)
#define NSPN_MAGIC 0x4E53504E

int rc_load_npc_spawns(RcWorld *world, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "npc_spawns: can't open %s\n", path); return -1; }

    uint32_t magic, version, count;
    if (fread(&magic, 4, 1, f) != 1 || magic != NSPN_MAGIC) {
        fclose(f);
        fprintf(stderr, "npc_spawns: bad magic\n");
        return -1;
    }
    fread(&version, 4, 1, f);
    fread(&count, 4, 1, f);

    int spawned = 0;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t nid;
        int32_t x, y;
        uint8_t plane, direction, wander_range;
        fread(&nid, 4, 1, f);
        fread(&x, 4, 1, f);
        fread(&y, 4, 1, f);
        fread(&plane, 1, 1, f);
        fread(&direction, 1, 1, f);
        fread(&wander_range, 1, 1, f);

        int def_idx = rc_npc_def_find((int)nid);
        if (def_idx < 0) continue;
        // Override wander_range from spawn if specified
        if (wander_range > 0) g_npc_defs[def_idx].wander_range = wander_range;

        if (rc_npc_spawn(world, def_idx, x, y, plane) >= 0) {
            spawned++;
        }
    }
    fclose(f);
    fprintf(stderr, "npc_spawns: spawned %d NPCs from %s\n", spawned, path);
    return spawned;
}

int rc_npc_spawn(RcWorld *world, int def_idx, int world_x, int world_y, int plane) {
    if (world->npc_count >= RC_MAX_NPCS) return -1;
    if (def_idx < 0 || def_idx >= g_npc_def_count) return -1;

    RcNpc *npc = &world->npcs[world->npc_count];
    memset(npc, 0, sizeof(RcNpc));
    npc->def_id = def_idx;
    npc->uid = world->npc_count;
    npc->x = world_x;
    npc->y = world_y;
    npc->plane = plane;
    npc->spawn_x = world_x;
    npc->spawn_y = world_y;
    npc->prev_x = world_x;
    npc->prev_y = world_y;
    npc->current_hp = g_npc_defs[def_idx].hitpoints;
    npc->target_uid = -1;
    npc->active = true;

    return world->npc_count++;
}

// Wander AI matches RSMod NpcWanderModeProcessor.
// Each tick: 1/8 chance to pick random destination within wander_range,
// walk 1 step toward it. After 500 idle ticks, respawn at spawn coords.
void rc_npc_tick(RcWorld *world, RcNpc *npc) {
    if (!npc->active) return;

    npc->prev_x = npc->x;
    npc->prev_y = npc->y;

    RcNpcDef *def = &g_npc_defs[npc->def_id];

    // Dead: decrement death timer, then start respawn timer
    if (npc->is_dead) {
        if (npc->death_timer > 0) {
            npc->death_timer--;
        } else if (npc->respawn_timer > 0) {
            npc->respawn_timer--;
        } else {
            npc->x = npc->spawn_x;
            npc->y = npc->spawn_y;
            npc->prev_x = npc->x;
            npc->prev_y = npc->y;
            npc->current_hp = def->hitpoints;
            npc->is_dead = false;
            npc->target_uid = -1;
            npc->num_pending_hits = 0;
        }
        return;
    }

    // Decrement timers
    if (npc->attack_timer > 0) npc->attack_timer--;

    // Wander AI matches RSMod NpcWanderModeProcessor.
    int wander_range = def->wander_range > 0 ? def->wander_range : 5;
    if (wander_range > 0 && npc->target_uid < 0) {
        // Track idle cycles
        bool moved_last = (npc->x != npc->prev_x || npc->y != npc->prev_y);
        if (moved_last) {
            npc->wander_timer = 0;
        } else {
            npc->wander_timer++;
        }

        // Respawn if idle for 500 ticks
        if (npc->wander_timer >= 500) {
            npc->x = npc->spawn_x;
            npc->y = npc->spawn_y;
            npc->wander_timer = 0;
            return;
        }

        // 1/8 chance per tick to pick a new wander destination
        if (rc_rng_range(&world->rng_state, 7) == 0) {
            // Random offset within wander_range of spawn point
            int dx = rc_rng_range(&world->rng_state, 2 * wander_range) - wander_range;
            int dy = rc_rng_range(&world->rng_state, 2 * wander_range) - wander_range;
            int target_x = npc->spawn_x + dx;
            int target_y = npc->spawn_y + dy;

            // Step toward target: 1 tile in direction
            int step_x = 0, step_y = 0;
            if (target_x > npc->x) step_x = 1;
            else if (target_x < npc->x) step_x = -1;
            if (target_y > npc->y) step_y = 1;
            else if (target_y < npc->y) step_y = -1;

            if ((step_x || step_y) &&
                rc_can_move(&world->map, npc->x, npc->y, step_x, step_y, npc->plane)) {
                npc->x += step_x;
                npc->y += step_y;
            }
        }
    }
}
