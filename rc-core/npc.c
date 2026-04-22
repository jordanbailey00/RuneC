#include "npc.h"
#include "io.h"
#include "rng.h"
#include "pathfinding.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

RcNpcDef g_npc_defs[RC_MAX_NPC_DEFS];
int g_npc_def_count = 0;

// NDEF binary — v1 (cache only) and v2 (cache + osrsreboxed merge).
// v1 layout: magic u32 | version u32 | count u32 |
//   per NPC: id u32, size u8, combat_level i16, hitpoints u16,
//            stats[6] u16, anims[5] i32,
//            name_len u8, name[name_len]
// v2 appends after the name, per NPC:
//            aggressive u8, max_hit u16, attack_speed u8, aggro_range u8,
//            slayer_level u16, attack_types_bitfield u8, weakness_bitfield u8,
//            immunities u8 (bit0=poison, bit1=venom)
#define NDEF_MAGIC 0x4E444546
#define NDEF_V1 1
#define NDEF_V2 2

int rc_load_npc_defs(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "npc_defs: can't open %s\n", path); return -1; }
    int orig_count = g_npc_def_count;

    uint32_t magic, version, count;
    if (!rc_read_exact(f, &magic, sizeof(magic), 1, path, "npc defs magic")
            || magic != NDEF_MAGIC) {
        fclose(f);
        fprintf(stderr, "npc_defs: bad magic\n");
        return -1;
    }
    if (!rc_read_exact(f, &version, sizeof(version), 1, path, "npc defs version")
            || !rc_read_exact(f, &count, sizeof(count), 1, path, "npc defs count")) {
        fclose(f);
        g_npc_def_count = orig_count;
        return -1;
    }
    if (version != NDEF_V1 && version != NDEF_V2) {
        fclose(f);
        fprintf(stderr, "npc_defs: unsupported version %u\n", version);
        return -1;
    }

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

        if (!rc_read_exact(f, &id, sizeof(id), 1, path, "npc id")
                || !rc_read_exact(f, &size, sizeof(size), 1, path, "npc size")
                || !rc_read_exact(f, &cl, sizeof(cl), 1, path, "npc combat level")
                || !rc_read_exact(f, &hp, sizeof(hp), 1, path, "npc hitpoints")
                || !rc_read_exact(f, stats, sizeof(stats[0]), 6, path, "npc stats")
                || !rc_read_exact(f, anims, sizeof(anims[0]), 5, path, "npc anims")
                || !rc_read_exact(f, &name_len, sizeof(name_len), 1, path, "npc name length")) {
            fclose(f);
            g_npc_def_count = orig_count;
            return -1;
        }
        if (name_len > 63) name_len = 63;
        if (!rc_read_exact(f, d->name, sizeof(char), name_len, path, "npc name")) {
            fclose(f);
            g_npc_def_count = orig_count;
            return -1;
        }
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

        if (version == NDEF_V2) {
            uint8_t aggr, atk_spd, aggro_r, atk_types, weak, immu;
            uint16_t max_hit, slayer_lvl;
            if (!rc_read_exact(f, &aggr, sizeof(aggr), 1, path, "npc aggression")
                    || !rc_read_exact(f, &max_hit, sizeof(max_hit), 1, path, "npc max hit")
                    || !rc_read_exact(f, &atk_spd, sizeof(atk_spd), 1, path, "npc attack speed")
                    || !rc_read_exact(f, &aggro_r, sizeof(aggro_r), 1, path, "npc aggro range")
                    || !rc_read_exact(f, &slayer_lvl, sizeof(slayer_lvl), 1, path, "npc slayer level")
                    || !rc_read_exact(f, &atk_types, sizeof(atk_types), 1, path, "npc attack types")
                    || !rc_read_exact(f, &weak, sizeof(weak), 1, path, "npc weakness")
                    || !rc_read_exact(f, &immu, sizeof(immu), 1, path, "npc immunities")) {
                fclose(f);
                g_npc_def_count = orig_count;
                return -1;
            }
            d->aggressive        = (aggr != 0);
            d->max_hit           = (int)max_hit;
            d->attack_speed      = (int)atk_spd;
            d->aggro_range       = (int)aggro_r;
            d->slayer_level      = (int)slayer_lvl;
            d->attack_types      = (int)atk_types;
            d->weakness          = (int)weak;
            d->poison_immune     = (immu & 1) != 0;
            d->venom_immune      = (immu & 2) != 0;
        }

        g_npc_def_count++;
        loaded++;
    }
    fclose(f);
    fprintf(stderr, "npc_defs: loaded %d defs (NDEF v%u) from %s\n",
            loaded, version, path);
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
// v1 per spawn: npc_id(u32) x(i32) y(i32) plane(u8) direction(u8) wander_range(u8)
// v2 per spawn: v1 fields + flags(u8) — bit0=instance_only (boss arena etc.)
#define NSPN_MAGIC 0x4E53504E
#define NSPN_FLAG_INSTANCE 0x01

int rc_load_npc_spawns(RcWorld *world, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "npc_spawns: can't open %s\n", path); return -1; }

    uint32_t magic, version, count;
    if (!rc_read_exact(f, &magic, sizeof(magic), 1, path, "npc spawn magic")
            || magic != NSPN_MAGIC) {
        fclose(f);
        fprintf(stderr, "npc_spawns: bad magic\n");
        return -1;
    }
    if (!rc_read_exact(f, &version, sizeof(version), 1, path, "npc spawn version")
            || !rc_read_exact(f, &count, sizeof(count), 1, path, "npc spawn count")) {
        fclose(f);
        return -1;
    }

    int spawned = 0;
    int skipped_instance = 0;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t nid;
        int32_t x, y;
        uint8_t plane, direction, wander_range, flags = 0;
        if (!rc_read_exact(f, &nid, sizeof(nid), 1, path, "spawn npc id")
                || !rc_read_exact(f, &x, sizeof(x), 1, path, "spawn x")
                || !rc_read_exact(f, &y, sizeof(y), 1, path, "spawn y")
                || !rc_read_exact(f, &plane, sizeof(plane), 1, path, "spawn plane")
                || !rc_read_exact(f, &direction, sizeof(direction), 1, path, "spawn direction")
                || !rc_read_exact(f, &wander_range, sizeof(wander_range), 1, path, "spawn wander range")) {
            fclose(f);
            return -1;
        }
        if (version >= 2
                && !rc_read_exact(f, &flags, sizeof(flags), 1, path, "spawn flags")) {
            fclose(f);
            return -1;
        }

        // Instance-only NPCs live in dynamic arenas created on player
        // entry (Zulrah shrine, Vorkath, Fight Cave, etc.). Skip them
        // during static world-spawn loading; runtime code spawns them
        // when the player enters the instance.
        if (flags & NSPN_FLAG_INSTANCE) { skipped_instance++; continue; }

        int def_idx = rc_npc_def_find((int)nid);
        if (def_idx < 0) continue;
        // Override wander_range from spawn if specified
        if (wander_range > 0) g_npc_defs[def_idx].wander_range = wander_range;

        if (rc_npc_spawn(world, def_idx, x, y, plane) >= 0) {
            spawned++;
        }
    }
    fclose(f);
    fprintf(stderr, "npc_spawns: spawned %d NPCs from %s"
            " (skipped %d instance-only)\n",
            spawned, path, skipped_instance);
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

    int idx = world->npc_count++;

    // Fire spawn event — encounter subsystem matches NPCs to specs
    // here. Fires regardless of enabled subsystems; no-op if nothing
    // subscribed (per README §7).
    RcPayloadNpcEvent payload = {
        .npc_id = (uint16_t)npc->uid,
        .def_id = (uint32_t)g_npc_defs[def_idx].id,
    };
    rc_event_fire(world, RC_EVT_NPC_SPAWNED, &payload);

    return idx;
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

    // Attack-timer decrement moved to combat.c::rc_combat_tick_npc
    // (tick.c calls it per-NPC after the position pass).

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
