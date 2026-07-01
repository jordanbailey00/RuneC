#include "combat.h"
#include "combat_hit.h"
#include "npc.h"
#include "io.h"
#include "rng.h"
#include "pathfinding.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

RcNpcDef g_npc_defs[RC_MAX_NPC_DEFS];
int g_npc_def_count = 0;
static int g_npc_def_by_id[RC_MAX_NPC_ID];
static bool g_npc_def_index_ready = false;
static const RcNpcDef *g_active_npc_defs = g_npc_defs;
static int g_active_npc_def_count = 0;
static const int *g_active_npc_def_by_id = g_npc_def_by_id;

static void rc_npc_def_index_reset(void) {
    for (int i = 0; i < RC_MAX_NPC_ID; i++) g_npc_def_by_id[i] = -1;
    g_npc_def_index_ready = true;
}

static void npc_def_index_reset_into(int *def_by_id, int max_def_by_id) {
    if (!def_by_id || max_def_by_id <= 0) return;
    for (int i = 0; i < max_def_by_id; i++) def_by_id[i] = -1;
}

void rc_npc_use_defs(const RcNpcDef *defs, int count,
                     const int *def_by_id) {
    g_active_npc_defs = defs ? defs : g_npc_defs;
    g_active_npc_def_count = defs ? count : 0;
    g_active_npc_def_by_id = defs ? def_by_id : g_npc_def_by_id;
}

void rc_npc_reset_defs_if_active(const RcNpcDef *defs) {
    if (defs && g_active_npc_defs == defs) {
        rc_npc_use_defs(g_npc_defs, g_npc_def_count,
                        g_npc_def_index_ready ? g_npc_def_by_id : NULL);
    }
}

// NDEF runtime schema: schema/defs/npc_defs.schema.toml.
#define NDEF_MAGIC 0x4E444546
#define NDEF_V1 1
#define NDEF_V2 2
#define NDEF_V3 3
#define NDEF_V4 4

int rc_load_npc_defs_into(const char *path, RcNpcDef *defs, int max_defs,
                          int *out_count, int *def_by_id,
                          int max_def_by_id) {
    if (out_count) *out_count = 0;
    if (!path || !defs || max_defs <= 0) return -1;
    FILE *f = rc_asset_fopen(path, "rb");
    if (!f) { fprintf(stderr, "npc_defs: can't open %s\n", path); return -1; }

    uint32_t magic, version, count;
    if (!rc_read_exact(f, &magic, sizeof(magic), 1, path, "npc defs magic")
            || magic != NDEF_MAGIC) {
        rc_asset_close(f);
        fprintf(stderr, "npc_defs: bad magic\n");
        return -1;
    }
    if (!rc_read_exact(f, &version, sizeof(version), 1, path, "npc defs version")
            || !rc_read_exact(f, &count, sizeof(count), 1, path, "npc defs count")) {
        rc_asset_close(f);
        return -1;
    }
    if (version != NDEF_V1 && version != NDEF_V2
            && version != NDEF_V3 && version != NDEF_V4) {
        rc_asset_close(f);
        fprintf(stderr, "npc_defs: unsupported version %u\n", version);
        return -1;
    }

    memset(defs, 0, (size_t)max_defs * sizeof(*defs));
    npc_def_index_reset_into(def_by_id, max_def_by_id);
    int loaded = 0;
    for (uint32_t i = 0; i < count && loaded < max_defs; i++) {
        RcNpcDef *d = &defs[loaded];
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
            rc_asset_close(f);
            return -1;
        }
        if (name_len > 63) name_len = 63;
        if (!rc_read_exact(f, d->name, sizeof(char), name_len, path, "npc name")) {
            rc_asset_close(f);
            return -1;
        }
        d->name[name_len] = 0;

        d->id = (int)id;
        d->size = size > 0 ? size : 1;
        d->combat_level = cl;
        d->hitpoints = hp;
        for (int j = 0; j < 6; j++) d->stats[j] = stats[j];
        d->wander_range = 5;
        d->respawn_ticks = 25;
        d->aggressive = false;
        d->aggro_range = 0;

        if (version >= NDEF_V2) {
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
                rc_asset_close(f);
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
        if (version >= NDEF_V3) {
            uint8_t model_count;
            if (!rc_read_exact(f, &model_count, sizeof(model_count), 1,
                               path, "npc model count")) {
                rc_asset_close(f);
                return -1;
            }
            for (uint8_t j = 0; j < model_count; j++) {
                uint32_t model_id;
                if (!rc_read_exact(f, &model_id, sizeof(model_id), 1,
                                   path, "npc model id")) {
                    rc_asset_close(f);
                    return -1;
                }
            }
        }
        if (version >= NDEF_V4) {
            for (int j = 0; j < RC_NPC_OPTION_COUNT; j++) {
                uint8_t option_len;
                char option_buf[256];
                if (!rc_read_exact(f, &option_len, sizeof(option_len), 1,
                                   path, "npc option length")) {
                    rc_asset_close(f);
                    return -1;
                }
                if (option_len == 0) {
                    d->options[j][0] = '\0';
                    continue;
                }
                if (!rc_read_exact(f, option_buf, sizeof(char), option_len,
                                   path, "npc option")) {
                    rc_asset_close(f);
                    return -1;
                }
                int copy_len = option_len;
                if (copy_len >= RC_NPC_OPTION_LEN)
                    copy_len = RC_NPC_OPTION_LEN - 1;
                memcpy(d->options[j], option_buf, (size_t)copy_len);
                d->options[j][copy_len] = '\0';
            }
        }

        if (def_by_id && id < (uint32_t)max_def_by_id)
            def_by_id[id] = loaded;
        loaded++;
    }
    rc_asset_close(f);
    if (out_count) *out_count = loaded;
    fprintf(stderr, "npc_defs: loaded %d defs (NDEF v%u) from %s\n",
            loaded, version, path);
    return loaded;
}

int rc_load_npc_defs(const char *path) {
    rc_npc_def_index_reset();
    int loaded = 0;
    int result = rc_load_npc_defs_into(path, g_npc_defs, RC_MAX_NPC_DEFS,
                                       &loaded, g_npc_def_by_id,
                                       RC_MAX_NPC_ID);
    if (result < 0) {
        g_npc_def_count = 0;
        rc_npc_use_defs(g_npc_defs, g_npc_def_count, g_npc_def_by_id);
        return -1;
    }
    g_npc_def_count = loaded;
    g_npc_def_index_ready = true;
    rc_npc_use_defs(g_npc_defs, g_npc_def_count, g_npc_def_by_id);
    return result;
}

int rc_npc_def_find(int npc_id) {
    const RcNpcDef *defs = g_active_npc_defs ? g_active_npc_defs
                                             : g_npc_defs;
    int count = defs == g_npc_defs ? g_npc_def_count
                                   : g_active_npc_def_count;
    const int *by_id = defs == g_npc_defs
                     ? (g_npc_def_index_ready ? g_npc_def_by_id : NULL)
                     : g_active_npc_def_by_id;
    if (npc_id >= 0 && npc_id < RC_MAX_NPC_ID && by_id) {
        int idx = by_id[npc_id];
        if (defs != g_npc_defs && idx >= 0 && idx < count
                && idx < g_npc_def_count && g_npc_defs[idx].id == npc_id
                && memcmp(&defs[idx], &g_npc_defs[idx],
                          sizeof(defs[idx])) != 0) {
            return idx;
        }
        if (idx >= 0 && idx < count && defs[idx].id == npc_id) {
            return idx;
        }
    }
    // Tests may inject synthetic defs without rebuilding the ID index.
    for (int i = 0; i < count; i++) {
        if (defs[i].id == npc_id) return i;
    }
    if (defs != g_npc_defs) {
        for (int i = 0; i < g_npc_def_count; i++) {
            if (g_npc_defs[i].id == npc_id) return i;
        }
    }
    return -1;
}

const RcNpcDef *rc_npc_defs_all(int *count) {
    const RcNpcDef *defs = g_active_npc_defs ? g_active_npc_defs
                                             : g_npc_defs;
    int n = defs == g_npc_defs ? g_npc_def_count : g_active_npc_def_count;
    if (count) *count = n;
    return n > 0 ? defs : NULL;
}

const RcNpcDef *rc_npc_def_get(int def_idx) {
    const RcNpcDef *defs = g_active_npc_defs ? g_active_npc_defs
                                             : g_npc_defs;
    int count = defs == g_npc_defs ? g_npc_def_count
                                   : g_active_npc_def_count;
    if (def_idx >= 0 && def_idx < count) {
        if (defs != g_npc_defs && def_idx < g_npc_def_count
                && memcmp(&defs[def_idx], &g_npc_defs[def_idx],
                          sizeof(defs[def_idx])) != 0) {
            return &g_npc_defs[def_idx];
        }
        return &defs[def_idx];
    }
    if (defs != g_npc_defs && def_idx >= 0 && def_idx < g_npc_def_count) {
        return &g_npc_defs[def_idx];
    }
    return NULL;
}

const RcNpcDef *rc_npc_def_for_npc(const RcNpc *npc) {
    return npc ? rc_npc_def_get(npc->def_id) : NULL;
}

int rc_npc_def_find_name(const char *name) {
    if (!name || !name[0]) return -1;
    int count = 0;
    (void)rc_npc_defs_all(&count);
    for (int i = 0; i < count; i++) {
        const RcNpcDef *def = rc_npc_def_get(i);
        if (def && strcmp(def->name, name) == 0) return i;
    }
    if (g_active_npc_defs != g_npc_defs) {
        for (int i = 0; i < g_npc_def_count; i++) {
            if (strcmp(g_npc_defs[i].name, name) == 0) return i;
        }
    }
    return -1;
}

const char *rc_npc_def_option(const RcNpcDef *def, int option_idx) {
    if (!def || option_idx < 0 || option_idx >= RC_NPC_OPTION_COUNT)
        return "";
    return def->options[option_idx];
}

bool rc_npc_def_option_is_attack(const RcNpcDef *def, int option_idx) {
    const char *option = rc_npc_def_option(def, option_idx);
    return strcmp(option, "Attack") == 0;
}

// Load NSPN binary: magic(u32) version(u32) count(u32)
// v1 per spawn: npc_id(u32) x(i32) y(i32) plane(u8) direction(u8) wander_range(u8)
// v2 per spawn: v1 fields + flags(u8) — bit0=instance_only (boss arena etc.)
#define NSPN_MAGIC 0x4E53504E
#define NSPN_FLAG_INSTANCE 0x01

static void npc_face_move_delta(RcNpc *npc, int dx, int dy) {
    if (!npc || (!dx && !dy)) return;
    npc->facing_entity = -1;
    npc->facing_x = npc->x + dx;
    npc->facing_y = npc->y + dy;
}

static int load_npc_spawns_filtered(RcWorld *world, const char *path,
                                    int use_filter, int min_x, int min_y,
                                    int max_x, int max_y,
                                    int min_plane, int max_plane,
                                    RcNpcSpawnLoadStats *stats) {
    if (!world || !path) return -1;
    if (stats) memset(stats, 0, sizeof(*stats));
    FILE *f = rc_asset_fopen(path, "rb");
    if (!f) { fprintf(stderr, "npc_spawns: can't open %s\n", path); return -1; }

    uint32_t magic, version, count;
    if (!rc_read_exact(f, &magic, sizeof(magic), 1, path, "npc spawn magic")
            || magic != NSPN_MAGIC) {
        rc_asset_close(f);
        fprintf(stderr, "npc_spawns: bad magic\n");
        return -1;
    }
    if (!rc_read_exact(f, &version, sizeof(version), 1, path, "npc spawn version")
            || !rc_read_exact(f, &count, sizeof(count), 1, path, "npc spawn count")) {
        rc_asset_close(f);
        return -1;
    }
    if (version < 1 || version > 2) {
        rc_asset_close(f);
        fprintf(stderr, "npc_spawns: unsupported version %u\n", version);
        return -1;
    }

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
            rc_asset_close(f);
            return -1;
        }
        if (version >= 2
                && !rc_read_exact(f, &flags, sizeof(flags), 1, path, "spawn flags")) {
            rc_asset_close(f);
            return -1;
        }

        if (stats) {
            stats->total_rows++;
            if (plane < RC_MAX_PLANES)
                stats->source_plane_counts[plane]++;
        }
        if (use_filter && (x < min_x || x > max_x || y < min_y || y > max_y
                || plane < min_plane || plane > max_plane)) {
            if (stats) stats->skipped_filter++;
            continue;
        }
        if (stats) {
            stats->matched_filter++;
            if (plane < RC_MAX_PLANES)
                stats->matched_plane_counts[plane]++;
        }

        // Instance-only NPCs live in dynamic arenas created on player
        // entry (Zulrah shrine, Vorkath, Fight Cave, etc.). Skip them
        // during static world-spawn loading; runtime code spawns them
        // when the player enters the instance.
        if (flags & NSPN_FLAG_INSTANCE) {
            if (stats) stats->skipped_instance++;
            continue;
        }

        int def_idx = rc_npc_def_find((int)nid);
        if (def_idx < 0) {
            if (stats) stats->skipped_missing_def++;
            continue;
        }
        int spawned_idx = rc_npc_spawn(world, def_idx, x, y, plane);
        if (spawned_idx >= 0) {
            if (wander_range > 0)
                world->npcs[spawned_idx].spawn_wander_range = wander_range;
            if (stats) {
                stats->spawned++;
                if (plane < RC_MAX_PLANES)
                    stats->spawned_plane_counts[plane]++;
            }
        } else if (stats) {
            stats->skipped_capacity++;
        }
    }
    rc_asset_close(f);
    fprintf(stderr, "npc_spawns: spawned %d NPCs from %s"
            " (matched %d, skipped %d filtered, %d instance-only,"
            " %d missing-def, %d capacity)\n",
            stats ? stats->spawned : 0, path,
            stats ? stats->matched_filter : 0,
            stats ? stats->skipped_filter : 0,
            stats ? stats->skipped_instance : 0,
            stats ? stats->skipped_missing_def : 0,
            stats ? stats->skipped_capacity : 0);
    return stats ? stats->spawned : 0;
}

int rc_load_npc_spawns(RcWorld *world, const char *path) {
    RcNpcSpawnLoadStats stats;
    return load_npc_spawns_filtered(world, path, 0, 0, 0, 0, 0, 0, 0,
                                    &stats);
}

int rc_load_npc_spawns_rect(RcWorld *world, const char *path,
                            int min_x, int min_y, int max_x, int max_y,
                            int min_plane, int max_plane) {
    if (!world || !path || min_x > max_x || min_y > max_y
            || min_plane > max_plane) {
        return -1;
    }
    RcNpcSpawnLoadStats stats;
    return load_npc_spawns_filtered(world, path, 1, min_x, min_y, max_x,
                                    max_y, min_plane, max_plane, &stats);
}

int rc_load_npc_spawns_rect_stats(RcWorld *world, const char *path,
                                  int min_x, int min_y,
                                  int max_x, int max_y,
                                  int min_plane, int max_plane,
                                  RcNpcSpawnLoadStats *stats) {
    if (!world || !path || !stats || min_x > max_x || min_y > max_y
            || min_plane > max_plane) {
        return -1;
    }
    return load_npc_spawns_filtered(world, path, 1, min_x, min_y, max_x,
                                    max_y, min_plane, max_plane, stats);
}

int rc_load_npc_spawns_near(RcWorld *world, const char *path,
                            int center_x, int center_y, int radius,
                            int plane) {
    if (radius < 0) return -1;
    return rc_load_npc_spawns_rect(world, path, center_x - radius,
                                   center_y - radius, center_x + radius,
                                   center_y + radius, plane, plane);
}

int rc_npc_spawn(RcWorld *world, int def_idx, int world_x, int world_y, int plane) {
    if (world->npc_count >= RC_MAX_NPCS) return -1;
    const RcNpcDef *def = rc_npc_def_get(def_idx);
    if (!def) return -1;

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
    npc->current_hp = def->hitpoints;
    npc->spawn_wander_range = 0;
    npc->target_uid = -1;
    npc->facing_entity = -1;
    npc->facing_x = -1;
    npc->facing_y = -1;
    npc->last_hit = -1;
    npc->active = true;
    rc_combat_init_npc_state(npc);

    int idx = world->npc_count++;

    // Fire spawn event — encounter subsystem matches NPCs to specs
    // here. Fires regardless of enabled subsystems; no-op if nothing
    // subscribed (per README §7).
    RcPayloadNpcEvent payload = {
        .npc_id = (uint16_t)npc->uid,
        .def_id = (uint32_t)def->id,
    };
    rc_event_fire(world, RC_EVT_NPC_SPAWNED, &payload);

    return idx;
}

// Wander AI matches RSMod NpcWanderModeProcessor.
// Each tick: 1/8 chance to pick random destination within wander_range,
// walk 1 step toward it. After 500 idle ticks, respawn at spawn coords.
void rc_npc_tick(RcWorld *world, RcNpc *npc) {
    if (!npc->active) return;

    int moved_last = (npc->x != npc->prev_x || npc->y != npc->prev_y);
    npc->prev_x = npc->x;
    npc->prev_y = npc->y;
    if (npc->last_hit_timer > 0) npc->last_hit_timer--;
    rc_combat_actor_tick_recent_hits(&npc->combat);
    rc_combat_tick_actor_threat(&npc->combat);

    const RcNpcDef *def = rc_npc_def_get(npc->def_id);
    if (!def) return;

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
            rc_combat_init_npc_state(npc);
            npc->num_pending_hits = 0;
            npc->poison_damage = 0;
            npc->poison_tick_counter = 0;
            npc->last_hit = -1;
            npc->last_hit_timer = 0;
        }
        return;
    }

    // Attack-timer decrement moved to combat.c::rc_combat_tick_npc
    // (tick.c calls it per-NPC after the position pass).

    // Wander AI matches RSMod NpcWanderModeProcessor.
    int def_wander_range = def->wander_range > 0 ? def->wander_range : 5;
    int wander_range = npc->disable_wander ? 0
                     : (npc->spawn_wander_range > 0
                        ? npc->spawn_wander_range
                        : def_wander_range);
    if (wander_range > 0 && npc->target_uid < 0) {
        if (moved_last) {
            npc->wander_timer = 0;
        } else {
            npc->wander_timer++;
        }

        // After long idle, walk back to spawn instead of snapping there.
        if (npc->wander_timer >= 500) {
            int step_x = 0, step_y = 0;
            if (npc->spawn_x > npc->x) step_x = 1;
            else if (npc->spawn_x < npc->x) step_x = -1;
            if (npc->spawn_y > npc->y) step_y = 1;
            else if (npc->spawn_y < npc->y) step_y = -1;
            if (!step_x && !step_y) {
                npc->wander_timer = 0;
            } else if (rc_can_move(&world->map, npc->x, npc->y,
                                   step_x, step_y, npc->plane)) {
                npc->x += step_x;
                npc->y += step_y;
                npc_face_move_delta(npc, step_x, step_y);
            }
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
                npc_face_move_delta(npc, step_x, step_y);
            }
        }
    }
}
