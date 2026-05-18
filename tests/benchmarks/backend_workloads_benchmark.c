#define _POSIX_C_SOURCE 200809L

#include "../../rc-core/api.h"
#include "../../rc-core/combat.h"
#include "../../rc-core/config.h"
#include "../../rc-core/npc.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef RC_TEST_SOURCE_DIR
#define RC_TEST_SOURCE_DIR "."
#endif

#define CTIL_PATH RC_TEST_SOURCE_DIR "/data/defs/collision_tiles.bin"
#define ODEF_PATH RC_TEST_SOURCE_DIR "/data/defs/object_defs.bin"
#define OPLC_PATH RC_TEST_SOURCE_DIR "/data/defs/object_placements.bin"
#define OBHV_PATH RC_TEST_SOURCE_DIR "/data/defs/object_behaviors.bin"
#define OTRP_PATH RC_TEST_SOURCE_DIR "/data/defs/object_transports.bin"
#define TRAV_PATH RC_TEST_SOURCE_DIR "/data/defs/traversal_edges.bin"
#define NPC_PATH RC_TEST_SOURCE_DIR "/data/defs/npc_defs.bin"
#define SPAWN_PATH RC_TEST_SOURCE_DIR "/data/spawns/world.npc-spawns.bin"

typedef enum RcWorkloadMode {
    RC_WORKLOAD_ALL = 0,
    RC_WORKLOAD_PATH,
    RC_WORKLOAD_OBJECT,
    RC_WORKLOAD_SPAWN_SLICE,
    RC_WORKLOAD_LOAD_FULL,
    RC_WORKLOAD_RESET_FULL,
    RC_WORKLOAD_EDGE_ACTIONS,
    RC_WORKLOAD_MIXED_AGENT,
    RC_WORKLOAD_NPC_IDLE,
    RC_WORKLOAD_NPC_COMBAT,
    RC_WORKLOAD_PROJECTILES,
} RcWorkloadMode;

typedef struct RcBenchConfig {
    RcWorkloadMode mode;
    int envs;
    int ops;
    int warmup;
    int active;
} RcBenchConfig;

static double now_seconds(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1000000000.0;
}

static int parse_positive(const char *arg, const char *value, int fallback) {
    char *end = NULL;
    long parsed = strtol(value, &end, 10);
    if (end == value || *end != '\0' || parsed <= 0 || parsed > 1000000000L) {
        fprintf(stderr, "invalid %s: %s\n", arg, value);
        exit(2);
    }
    (void)fallback;
    return (int)parsed;
}

static const char *mode_name(RcWorkloadMode mode) {
    switch (mode) {
    case RC_WORKLOAD_ALL: return "all";
    case RC_WORKLOAD_PATH: return "path-actions";
    case RC_WORKLOAD_OBJECT: return "object-interactions";
    case RC_WORKLOAD_SPAWN_SLICE: return "spawn-slice";
    case RC_WORKLOAD_LOAD_FULL: return "load-full";
    case RC_WORKLOAD_RESET_FULL: return "reset-full";
    case RC_WORKLOAD_EDGE_ACTIONS: return "edge-actions";
    case RC_WORKLOAD_MIXED_AGENT: return "mixed-agent";
    case RC_WORKLOAD_NPC_IDLE: return "npc-idle";
    case RC_WORKLOAD_NPC_COMBAT: return "npc-combat";
    case RC_WORKLOAD_PROJECTILES: return "projectiles";
    }
    return "unknown";
}

static RcWorkloadMode parse_mode(const char *s) {
    if (strcmp(s, "all") == 0) return RC_WORKLOAD_ALL;
    if (strcmp(s, "path") == 0 || strcmp(s, "path-actions") == 0)
        return RC_WORKLOAD_PATH;
    if (strcmp(s, "object") == 0 || strcmp(s, "object-interactions") == 0)
        return RC_WORKLOAD_OBJECT;
    if (strcmp(s, "spawn") == 0 || strcmp(s, "spawn-slice") == 0)
        return RC_WORKLOAD_SPAWN_SLICE;
    if (strcmp(s, "load") == 0 || strcmp(s, "load-full") == 0)
        return RC_WORKLOAD_LOAD_FULL;
    if (strcmp(s, "reset") == 0 || strcmp(s, "reset-full") == 0)
        return RC_WORKLOAD_RESET_FULL;
    if (strcmp(s, "edge") == 0 || strcmp(s, "edge-actions") == 0)
        return RC_WORKLOAD_EDGE_ACTIONS;
    if (strcmp(s, "mixed") == 0 || strcmp(s, "mixed-agent") == 0)
        return RC_WORKLOAD_MIXED_AGENT;
    if (strcmp(s, "npc-idle") == 0) return RC_WORKLOAD_NPC_IDLE;
    if (strcmp(s, "npc-combat") == 0) return RC_WORKLOAD_NPC_COMBAT;
    if (strcmp(s, "projectiles") == 0) return RC_WORKLOAD_PROJECTILES;
    fprintf(stderr, "unknown mode: %s\n", s);
    exit(2);
}

static void print_usage(const char *argv0) {
    fprintf(stderr,
            "usage: %s [--mode all|path|object|spawn|load|reset|edge|mixed|"
            "npc-idle|npc-combat|projectiles]"
            " [--envs N] [--ops N] [--warmup N] [--active N]\n",
            argv0);
}

static void print_rate(const char *name, int units, double elapsed) {
    double per_second = elapsed > 0.0 ? (double)units / elapsed : 0.0;
    double ns_per = units > 0 ? elapsed * 1000000000.0 / (double)units : 0.0;
    printf("workload: %s\n", name);
    printf("units: %d\n", units);
    printf("elapsed_seconds: %.6f\n", elapsed);
    printf("units_per_second: %.0f\n", per_second);
    printf("ns_per_unit: %.2f\n", ns_per);
}

static void ensure_npc_defs_loaded(void) {
    if (g_npc_def_count <= 0 && rc_load_npc_defs(NPC_PATH) <= 0) {
        fprintf(stderr, "failed to load npc defs for benchmark\n");
        exit(1);
    }
}

static int choose_combat_npc_def(void) {
    ensure_npc_defs_loaded();
    for (int i = 0; i < g_npc_def_count; i++) {
        RcNpcDef *def = &g_npc_defs[i];
        if (def->hitpoints > 0 && def->attack_speed > 0 && def->max_hit > 0)
            return i;
    }
    fprintf(stderr, "no usable combat npc definition found\n");
    exit(1);
}

static int spawn_active_npcs(RcWorld *world, int def_idx, int active,
                             int combat) {
    if (active > RC_MAX_NPCS) active = RC_MAX_NPCS;
    int spawned = 0;
    for (int i = 0; i < active; i++) {
        int dx = (i % 31) - 15;
        int dy = ((i / 31) % 31) - 15;
        if (dx == 0 && dy == 0) dx = 1;
        int idx = rc_npc_spawn(world, def_idx, world->player.x + dx,
                               world->player.y + dy, world->player.plane);
        if (idx < 0) break;
        RcNpc *npc = &world->npcs[idx];
        npc->target_uid = combat ? 0 : -1;
        npc->attack_timer = combat ? (i & 3) : 0;
        spawned++;
    }
    return spawned;
}

static RcWorld *make_region_world(uint32_t seed) {
    RcWorldConfig cfg = rc_preset_base_only();
    cfg.seed = seed;
    cfg.subsystems = RC_SUB_REGIONS;
    cfg.collision_tiles_path = CTIL_PATH;
    cfg.area_flags_path = NULL;
    RcWorld *world = rc_world_create_config(&cfg);
    if (!world) {
        fprintf(stderr, "failed to create region benchmark world\n");
        exit(1);
    }
    world->player.x = 3213;
    world->player.y = 3428;
    world->player.plane = 0;
    return world;
}

static void bench_path_actions(int envs, int ops, int warmup) {
    static const int targets[][2] = {
        {3213, 3428}, {3220, 3428}, {3205, 3435}, {3232, 3421},
        {3189, 3436}, {3240, 3460}, {3174, 3405}, {3251, 3429},
        {3208, 3500}, {3228, 3394}, {3166, 3485}, {3261, 3435},
    };
    const int target_count = (int)(sizeof(targets) / sizeof(targets[0]));
    RcWorld **worlds = calloc((size_t)envs, sizeof(*worlds));
    if (!worlds) {
        fprintf(stderr, "failed to allocate path worlds\n");
        exit(1);
    }
    for (int i = 0; i < envs; i++)
        worlds[i] = make_region_world(1000u + (uint32_t)i);

    for (int step = 0; step < warmup; step++) {
        for (int env = 0; env < envs; env++) {
            const int *t = targets[(step + env) % target_count];
            rc_player_walk_to(worlds[env], t[0], t[1]);
            rc_world_tick(worlds[env]);
        }
    }

    int successful_routes = 0;
    double start = now_seconds();
    for (int step = 0; step < ops; step++) {
        for (int env = 0; env < envs; env++) {
            const int *t = targets[(step + env * 3) % target_count];
            rc_player_walk_to(worlds[env], t[0], t[1]);
            if (worlds[env]->player.route_len > 0)
                successful_routes++;
            rc_world_tick(worlds[env]);
        }
    }
    double elapsed = now_seconds() - start;
    int units = envs * ops;
    print_rate("path-actions", units, elapsed);
    printf("successful_routes: %d\n", successful_routes);
    printf("envs: %d\nops_per_env: %d\nwarmup_ops_per_env: %d\n",
           envs, ops, warmup);

    for (int i = 0; i < envs; i++)
        rc_world_destroy(worlds[i]);
    free(worlds);
}

static RcWorld *make_object_world(uint32_t seed) {
    RcWorldConfig cfg = rc_preset_base_only();
    cfg.seed = seed;
    cfg.subsystems = RC_SUB_OBJECTS | RC_SUB_TRAVERSAL | RC_SUB_REGIONS;
    cfg.object_defs_path = ODEF_PATH;
    cfg.object_placements_path = OPLC_PATH;
    cfg.object_behaviors_path = OBHV_PATH;
    cfg.object_transports_path = OTRP_PATH;
    cfg.traversal_edges_path = TRAV_PATH;
    cfg.collision_tiles_path = CTIL_PATH;
    cfg.area_flags_path = NULL;
    RcWorld *world = rc_world_create_config(&cfg);
    if (!world) {
        fprintf(stderr, "failed to create object benchmark world\n");
        exit(1);
    }
    return world;
}

static void reset_door_world(RcWorld *world) {
    world->player.x = 3197;
    world->player.y = 3384;
    world->player.plane = 0;
    world->player.prev_x = world->player.x;
    world->player.prev_y = world->player.y;
    world->player.route_len = 0;
    world->player.route_idx = 0;
    world->player.action_lock_timer = 0;
    world->player.interaction.active = 0;
    world->object_state_count = 0;
}

static void bench_object_interactions(int envs, int ops, int warmup) {
    RcWorld **worlds = calloc((size_t)envs, sizeof(*worlds));
    if (!worlds) {
        fprintf(stderr, "failed to allocate object worlds\n");
        exit(1);
    }
    for (int i = 0; i < envs; i++) {
        worlds[i] = make_object_world(2000u + (uint32_t)i);
        reset_door_world(worlds[i]);
    }

    for (int step = 0; step < warmup; step++) {
        for (int env = 0; env < envs; env++) {
            reset_door_world(worlds[env]);
            rc_player_interact_object_at(worlds[env], 11780, 3196, 3384, 0, 0);
            for (int i = 0; i < 4; i++)
                rc_world_tick(worlds[env]);
        }
    }

    int applied = 0;
    double start = now_seconds();
    for (int step = 0; step < ops; step++) {
        for (int env = 0; env < envs; env++) {
            reset_door_world(worlds[env]);
            applied += rc_player_interact_object_at(worlds[env], 11780,
                                                    3196, 3384, 0, 0);
            for (int i = 0; i < 4; i++)
                rc_world_tick(worlds[env]);
        }
    }
    double elapsed = now_seconds() - start;
    int units = envs * ops;
    print_rate("object-interactions", units, elapsed);
    printf("applied_interactions: %d\n", applied);
    printf("envs: %d\nops_per_env: %d\nwarmup_ops_per_env: %d\n",
           envs, ops, warmup);

    for (int i = 0; i < envs; i++)
        rc_world_destroy(worlds[i]);
    free(worlds);
}

static void bench_edge_actions(int envs, int ops, int warmup) {
    RcWorld **path_worlds = calloc((size_t)envs, sizeof(*path_worlds));
    RcWorld **object_worlds = calloc((size_t)envs, sizeof(*object_worlds));
    if (!path_worlds || !object_worlds) {
        fprintf(stderr, "failed to allocate edge worlds\n");
        exit(1);
    }
    for (int i = 0; i < envs; i++) {
        path_worlds[i] = make_region_world(6000u + (uint32_t)i);
        object_worlds[i] = make_object_world(7000u + (uint32_t)i);
        reset_door_world(object_worlds[i]);
    }

    for (int step = 0; step < warmup; step++) {
        for (int env = 0; env < envs; env++) {
            rc_player_walk_to(path_worlds[env], 9999, 9999);
            rc_player_interact_object_at(object_worlds[env], 11780,
                                         3196, 3384, 1, 0);
        }
    }

    int attempted = 0;
    int rejected_or_unrouted = 0;
    double start = now_seconds();
    for (int step = 0; step < ops; step++) {
        for (int env = 0; env < envs; env++) {
            RcWorld *pw = path_worlds[env];
            RcWorld *ow = object_worlds[env];
            switch (step & 3) {
            case 0:
                rc_player_walk_to(pw, 9999, 9999);
                attempted++;
                if (pw->player.route_len == 0) rejected_or_unrouted++;
                break;
            case 1:
                rc_player_walk_to(pw, 3072, 3395);
                attempted++;
                if (pw->player.route_len == 0) rejected_or_unrouted++;
                break;
            case 2:
                attempted++;
                if (!rc_player_interact_object_at(ow, 11780, 3196, 3384,
                                                  1, 0)) {
                    rejected_or_unrouted++;
                }
                break;
            default:
                attempted++;
                if (!rc_player_interact_object_at(ow, 999999, 3196, 3384,
                                                  0, 0)) {
                    rejected_or_unrouted++;
                }
                break;
            }
            rc_world_tick(pw);
            rc_world_tick(ow);
        }
    }
    double elapsed = now_seconds() - start;
    int units = envs * ops;
    print_rate("edge-actions", units, elapsed);
    printf("attempted_actions: %d\n", attempted);
    printf("rejected_or_unrouted: %d\n", rejected_or_unrouted);
    printf("envs: %d\nops_per_env: %d\nwarmup_ops_per_env: %d\n",
           envs, ops, warmup);

    for (int i = 0; i < envs; i++) {
        rc_world_destroy(path_worlds[i]);
        rc_world_destroy(object_worlds[i]);
    }
    free(path_worlds);
    free(object_worlds);
}

static RcWorld *make_mixed_agent_world(uint32_t seed, int *npc_uid_out) {
    int def_idx = choose_combat_npc_def();
    RcWorldConfig cfg = rc_preset_base_only();
    cfg.seed = seed;
    cfg.subsystems = RC_SUB_COMBAT | RC_SUB_OBJECTS | RC_SUB_TRAVERSAL
                   | RC_SUB_REGIONS;
    cfg.npc_defs_path = NPC_PATH;
    cfg.object_defs_path = ODEF_PATH;
    cfg.object_placements_path = OPLC_PATH;
    cfg.object_behaviors_path = OBHV_PATH;
    cfg.object_transports_path = OTRP_PATH;
    cfg.traversal_edges_path = TRAV_PATH;
    cfg.collision_tiles_path = CTIL_PATH;
    cfg.area_flags_path = NULL;
    RcWorld *world = rc_world_create_config(&cfg);
    if (!world) {
        fprintf(stderr, "failed to create mixed agent world\n");
        exit(1);
    }
    world->player.x = 3197;
    world->player.y = 3384;
    world->player.plane = 0;
    int npc_idx = rc_npc_spawn(world, def_idx, 3200, 3384, 0);
    if (npc_idx < 0) {
        fprintf(stderr, "failed to spawn mixed agent npc\n");
        exit(1);
    }
    if (npc_uid_out) *npc_uid_out = world->npcs[npc_idx].uid;
    return world;
}

static void bench_mixed_agent(int envs, int ops, int warmup) {
    static const int targets[][2] = {
        {3197, 3384}, {3204, 3384}, {3212, 3390}, {3188, 3380},
    };
    const int target_count = (int)(sizeof(targets) / sizeof(targets[0]));
    RcWorld **worlds = calloc((size_t)envs, sizeof(*worlds));
    int *npc_uids = calloc((size_t)envs, sizeof(*npc_uids));
    if (!worlds || !npc_uids) {
        fprintf(stderr, "failed to allocate mixed agent worlds\n");
        exit(1);
    }
    for (int i = 0; i < envs; i++)
        worlds[i] = make_mixed_agent_world(8000u + (uint32_t)i, &npc_uids[i]);

    for (int step = 0; step < warmup; step++) {
        for (int env = 0; env < envs; env++) {
            rc_player_walk_to(worlds[env], targets[step % target_count][0],
                              targets[step % target_count][1]);
            rc_world_tick(worlds[env]);
        }
    }

    int attacks = 0, objects = 0, paths = 0, rejected = 0;
    double start = now_seconds();
    for (int step = 0; step < ops; step++) {
        for (int env = 0; env < envs; env++) {
            RcWorld *world = worlds[env];
            switch (step % 5) {
            case 0:
            case 1: {
                const int *t = targets[(step + env) % target_count];
                rc_player_walk_to(world, t[0], t[1]);
                paths++;
                break;
            }
            case 2:
                rc_player_attack_npc(world, npc_uids[env]);
                attacks++;
                break;
            case 3:
                if (rc_player_interact_object_at(world, 11780, 3196, 3384,
                                                 0, 0)) {
                    objects++;
                } else {
                    rejected++;
                }
                break;
            default:
                if (!rc_player_interact_object_at(world, 11780, 3196, 3384,
                                                  1, 0)) {
                    rejected++;
                }
                break;
            }
            rc_world_tick(world);
        }
    }
    double elapsed = now_seconds() - start;
    int units = envs * ops;
    print_rate("mixed-agent-actions", units, elapsed);
    printf("path_actions: %d\nattack_actions: %d\nobject_actions: %d\n"
           "rejected_actions: %d\n",
           paths, attacks, objects, rejected);
    printf("envs: %d\nops_per_env: %d\nwarmup_ops_per_env: %d\n",
           envs, ops, warmup);

    for (int i = 0; i < envs; i++)
        rc_world_destroy(worlds[i]);
    free(worlds);
    free(npc_uids);
}

static RcWorld *make_spawn_world(uint32_t seed) {
    RcWorldConfig cfg = rc_preset_base_only();
    cfg.seed = seed;
    cfg.subsystems = RC_SUB_COMBAT | RC_SUB_REGIONS;
    cfg.npc_defs_path = NPC_PATH;
    cfg.spawns_path = SPAWN_PATH;
    cfg.collision_tiles_path = CTIL_PATH;
    RcWorld *world = rc_world_create_config(&cfg);
    if (!world) {
        fprintf(stderr, "failed to create spawn benchmark world\n");
        exit(1);
    }
    return world;
}

static void bench_spawn_slice(int ops) {
    RcWorld *world = make_spawn_world(3000u);
    RcActiveAreaRequest req = {
        .origin_x = 3072,
        .origin_y = 3264,
        .width = 320,
        .height = 320,
        .min_plane = 0,
        .max_plane = RC_MAX_PLANES - 1,
        .flags = RC_ACTIVE_AREA_LOAD_COLLISION
               | RC_ACTIVE_AREA_LOAD_NPCS
               | RC_ACTIVE_AREA_CLEAR_NPCS,
    };
    RcActiveAreaStats stats = {0};

    double start = now_seconds();
    int spawned_total = 0;
    for (int i = 0; i < ops; i++) {
        if (rc_world_activate_area(world, &req, &stats) < 0) {
            fprintf(stderr, "active area load failed\n");
            exit(1);
        }
        spawned_total += stats.spawned_npcs;
    }
    double elapsed = now_seconds() - start;
    print_rate("active-area-loads", ops, elapsed);
    printf("spawned_total: %d\n", spawned_total);
    printf("last_collision_regions: %d\n", stats.collision_regions);
    printf("last_total_rows: %d\n", stats.npc_stats.total_rows);
    printf("last_matched_filter: %d\n", stats.npc_stats.matched_filter);
    printf("last_spawned: %d\n", stats.npc_stats.spawned);
    rc_world_destroy(world);
}

static void bench_load_full(void) {
    RcWorldConfig cfg = rc_preset_full_game();
    double start = now_seconds();
    RcWorld *world = rc_world_create_config(&cfg);
    double elapsed = now_seconds() - start;
    if (!world) {
        fprintf(stderr, "full-game load failed\n");
        exit(1);
    }
    print_rate("load-full-game-cold-process", 1, elapsed);
    printf("enabled_mask: %u\n", world->enabled);
    printf("npc_count: %d\n", world->npc_count);
    rc_world_destroy(world);
}

static void bench_reset_full(int ops) {
    RcWorldConfig cfg = rc_preset_full_game();
    RcWorld *preload = rc_world_create_config(&cfg);
    if (!preload) {
        fprintf(stderr, "full-game preload failed\n");
        exit(1);
    }
    rc_world_destroy(preload);

    double start = now_seconds();
    for (int i = 0; i < ops; i++) {
        RcWorld *world = rc_world_create_config(&cfg);
        if (!world) {
            fprintf(stderr, "full-game reset create failed\n");
            exit(1);
        }
        rc_world_destroy(world);
    }
    double elapsed = now_seconds() - start;
    print_rate("reset-full-game-warm-data", ops, elapsed);
}

static RcWorld *make_many_npc_world(uint32_t seed, int active, int combat,
                                    int *spawned_out) {
    int def_idx = choose_combat_npc_def();
    RcWorldConfig cfg = rc_preset_base_only();
    cfg.seed = seed;
    cfg.subsystems = combat ? RC_SUB_COMBAT : 0;
    RcWorld *world = rc_world_create_config(&cfg);
    if (!world) {
        fprintf(stderr, "failed to create many-npc world\n");
        exit(1);
    }
    world->player.x = 3200;
    world->player.y = 3200;
    world->player.plane = 0;
    world->player.current_hp = 1000000000;
    world->player.max_hp = 1000000000;
    world->player.auto_retaliate = false;
    int spawned = spawn_active_npcs(world, def_idx, active, combat);
    if (spawned_out) *spawned_out = spawned;
    return world;
}

static void bench_many_npc(int envs, int ticks, int active, int combat) {
    RcWorld **worlds = calloc((size_t)envs, sizeof(*worlds));
    if (!worlds) {
        fprintf(stderr, "failed to allocate many-npc worlds\n");
        exit(1);
    }
    int spawned = 0;
    for (int i = 0; i < envs; i++) {
        int n = 0;
        worlds[i] = make_many_npc_world(9000u + (uint32_t)i, active,
                                        combat, &n);
        if (n > spawned) spawned = n;
    }

    double start = now_seconds();
    for (int step = 0; step < ticks; step++) {
        for (int env = 0; env < envs; env++)
            rc_world_tick(worlds[env]);
    }
    double elapsed = now_seconds() - start;
    int units = envs * ticks;
    print_rate(combat ? "many-npc-combat-ticks" : "many-npc-idle-ticks",
               units, elapsed);
    printf("envs: %d\nticks_per_env: %d\nactive_npcs_per_env: %d\n",
           envs, ticks, spawned);

    for (int i = 0; i < envs; i++)
        rc_world_destroy(worlds[i]);
    free(worlds);
}

static RcWorld *make_projectile_world(uint32_t seed, int active) {
    RcWorldConfig cfg = rc_preset_base_only();
    cfg.seed = seed;
    cfg.subsystems = RC_SUB_COMBAT;
    RcWorld *world = rc_world_create_config(&cfg);
    if (!world) {
        fprintf(stderr, "failed to create projectile world\n");
        exit(1);
    }
    if (active > RC_MAX_COMBAT_PROJECTILES)
        active = RC_MAX_COMBAT_PROJECTILES;
    world->combat_projectile_count = active;
    for (int i = 0; i < active; i++) {
        RcCombatProjectile *p = &world->combat_projectiles[i];
        memset(p, 0, sizeof(*p));
        p->active = true;
        p->source_kind = RC_COMBAT_ACTOR_PLAYER;
        p->target_kind = RC_COMBAT_ACTOR_NPC;
        p->source_uid = 0;
        p->target_uid = i + 1;
        p->start_tick = 0;
        p->duration_ticks = 1000000;
        p->impact_duration_ticks = 3;
        p->style = COMBAT_MAGIC;
    }
    return world;
}

static void bench_projectiles(int envs, int ticks, int active) {
    RcWorld **worlds = calloc((size_t)envs, sizeof(*worlds));
    if (!worlds) {
        fprintf(stderr, "failed to allocate projectile worlds\n");
        exit(1);
    }
    for (int i = 0; i < envs; i++)
        worlds[i] = make_projectile_world(10000u + (uint32_t)i, active);

    double start = now_seconds();
    for (int step = 0; step < ticks; step++) {
        for (int env = 0; env < envs; env++) {
            rc_combat_tick_projectiles(worlds[env]);
            worlds[env]->tick++;
        }
    }
    double elapsed = now_seconds() - start;
    int units = envs * ticks;
    print_rate("projectile-heavy-ticks", units, elapsed);
    printf("envs: %d\nticks_per_env: %d\nactive_projectiles_per_env: %d\n",
           envs, ticks, active > RC_MAX_COMBAT_PROJECTILES
                         ? RC_MAX_COMBAT_PROJECTILES : active);

    for (int i = 0; i < envs; i++)
        rc_world_destroy(worlds[i]);
    free(worlds);
}

static void run_all(const RcBenchConfig *cfg) {
    bench_load_full();
    bench_path_actions(cfg->envs, cfg->ops, cfg->warmup);
    bench_object_interactions(cfg->envs, cfg->ops, cfg->warmup);
    bench_edge_actions(cfg->envs, cfg->ops, cfg->warmup);
    bench_mixed_agent(cfg->envs, cfg->ops, cfg->warmup);
    bench_many_npc(cfg->envs, cfg->ops, cfg->active, 0);
    bench_many_npc(cfg->envs, cfg->ops, cfg->active, 1);
    bench_projectiles(cfg->envs, cfg->ops, cfg->active);
    bench_spawn_slice(cfg->ops);
    bench_reset_full(cfg->ops);
}

int main(int argc, char **argv) {
    RcBenchConfig cfg = {
        .mode = RC_WORKLOAD_ALL,
        .envs = 8,
        .ops = 100,
        .warmup = 10,
        .active = 512,
    };

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--mode") == 0 && i + 1 < argc) {
            cfg.mode = parse_mode(argv[++i]);
        } else if (strcmp(argv[i], "--envs") == 0 && i + 1 < argc) {
            cfg.envs = parse_positive("--envs", argv[++i], cfg.envs);
        } else if (strcmp(argv[i], "--ops") == 0 && i + 1 < argc) {
            cfg.ops = parse_positive("--ops", argv[++i], cfg.ops);
        } else if (strcmp(argv[i], "--warmup") == 0 && i + 1 < argc) {
            cfg.warmup = parse_positive("--warmup", argv[++i], cfg.warmup);
        } else if (strcmp(argv[i], "--active") == 0 && i + 1 < argc) {
            cfg.active = parse_positive("--active", argv[++i], cfg.active);
        } else if (strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else {
            print_usage(argv[0]);
            return 2;
        }
    }

    printf("RuneC backend workload benchmark\n");
    printf("mode: %s\n", mode_name(cfg.mode));
    switch (cfg.mode) {
    case RC_WORKLOAD_ALL:
        run_all(&cfg);
        break;
    case RC_WORKLOAD_PATH:
        bench_path_actions(cfg.envs, cfg.ops, cfg.warmup);
        break;
    case RC_WORKLOAD_OBJECT:
        bench_object_interactions(cfg.envs, cfg.ops, cfg.warmup);
        break;
    case RC_WORKLOAD_SPAWN_SLICE:
        bench_spawn_slice(cfg.ops);
        break;
    case RC_WORKLOAD_LOAD_FULL:
        bench_load_full();
        break;
    case RC_WORKLOAD_RESET_FULL:
        bench_reset_full(cfg.ops);
        break;
    case RC_WORKLOAD_EDGE_ACTIONS:
        bench_edge_actions(cfg.envs, cfg.ops, cfg.warmup);
        break;
    case RC_WORKLOAD_MIXED_AGENT:
        bench_mixed_agent(cfg.envs, cfg.ops, cfg.warmup);
        break;
    case RC_WORKLOAD_NPC_IDLE:
        bench_many_npc(cfg.envs, cfg.ops, cfg.active, 0);
        break;
    case RC_WORKLOAD_NPC_COMBAT:
        bench_many_npc(cfg.envs, cfg.ops, cfg.active, 1);
        break;
    case RC_WORKLOAD_PROJECTILES:
        bench_projectiles(cfg.envs, cfg.ops, cfg.active);
        break;
    }
    return 0;
}
