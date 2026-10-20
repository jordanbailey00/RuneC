#define _POSIX_C_SOURCE 200809L

#include "../../rc-core/api.h"
#include "../../rc-core/combat.h"
#include "../../rc-core/npc.h"
#include "../../rc-core/prayer.h"
#include "../../rc-core/items.h"
#include "../../rc-content/content.h"
#include "../world_test_fixture.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef enum RcBenchMode {
    RC_BENCH_IDLE = 0,
    RC_BENCH_COMBAT = 1,
    RC_BENCH_PRAYER = 2,
    RC_BENCH_MAGIC = 3,
    RC_BENCH_RANGED = 4,
    RC_BENCH_CONSUMABLES = 5,
    RC_BENCH_ENCOUNTER = 6,   // 16 bound KBD runs, one fighting, ~1,000 NPCs
} RcBenchMode;

static double rc_now_seconds(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1000000000.0;
}

static int rc_parse_positive(const char *arg, const char *value, int fallback) {
    char *end = NULL;
    long parsed = strtol(value, &end, 10);
    if (end == value || *end != '\0' || parsed <= 0 || parsed > 1000000000L) {
        fprintf(stderr, "invalid %s: %s\n", arg, value);
        exit(2);
    }
    return (int)parsed;
}

static const char *rc_mode_name(RcBenchMode mode) {
    return mode == RC_BENCH_PRAYER ? "prayer"
         : mode == RC_BENCH_ENCOUNTER ? "encounter"
         : mode == RC_BENCH_CONSUMABLES ? "consumables"
         : mode == RC_BENCH_MAGIC ? "magic" : mode == RC_BENCH_RANGED ? "ranged"
         : mode == RC_BENCH_COMBAT ? "combat" : "idle";
}

static int rc_find_bench_npc_def(void) {
    int count = 0;
    const RcNpcDef *defs = rc_npc_defs_all(&count);
    for (int i = 0; defs && i < count; i++) {
        if (defs[i].hitpoints > 0 && defs[i].max_hit == 0
                && strcmp(defs[i].options[1], "Attack") == 0) {
            return i;
        }
    }
    return -1;
}

static RcWorld *rc_make_world(int seed, RcBenchMode mode, int *npc_def_idx) {
    RcWorldConfig cfg = rc_preset_base_only();
    cfg.subsystems = mode != RC_BENCH_IDLE ? RC_SUB_COMBAT : 0;
    if (mode == RC_BENCH_PRAYER) {
        cfg.subsystems |= RC_SUB_PRAYER;
        cfg.prayers_path = "data/defs/prayers.bin";
        cfg.player_actions_path = "data/defs/player_actions.bin";
    }
    cfg.seed = (uint64_t)seed;
    if (mode == RC_BENCH_MAGIC || mode == RC_BENCH_RANGED || mode == RC_BENCH_CONSUMABLES) {
        cfg.subsystems |= RC_SUB_INVENTORY | RC_SUB_EQUIPMENT;
        cfg.items_path = "data/defs/items.bin";
        cfg.spells_path = "data/defs/spells.bin";
    }
    if (mode == RC_BENCH_CONSUMABLES) cfg.subsystems |= RC_SUB_CONSUMABLES;
    if (mode == RC_BENCH_ENCOUNTER) {
        cfg.subsystems |= RC_SUB_ENCOUNTER;
        cfg.encounters_path = "data/defs/encounters.bin";
        cfg.npc_capacity = RC_WORLD_NPC_CAPACITY_SIM;
    }

    RcWorld *world = rc_world_create_config(&cfg);
    if (!world) {
        fprintf(stderr, "failed to create benchmark world\n");
        exit(1);
    }

    world->player.x = 3200;
    world->player.y = 3200;
    world->player.plane = 0;
    world->player.current_hp = 1000000000;
    world->player.max_hp = 1000000000;
    world->player.auto_retaliate = true;
    for (int i = 0; i < SKILL_COUNT; i++) {
        world->player.skills.base_level[i] = 99;
        world->player.skills.boosted_level[i] = 99;
    }
    rc_player_set_attack_style(world, 0);
    if (mode == RC_BENCH_CONSUMABLES) rc_content_consumables_register(world);
    if (mode == RC_BENCH_MAGIC || mode == RC_BENCH_RANGED) {
        rc_content_combat_register(world);
        world->player.equipment[EQUIP_WEAPON] = (RcInvSlot){.item_id = mode == RC_BENCH_MAGIC ? 1381 : 861, .quantity = 1};
        if (mode == RC_BENCH_MAGIC) {
            if (rc_inv_add(world->player.inventory, 558, 1000000000) < 0) {
                fprintf(stderr, "magic benchmark could not stock mind runes\n");
                exit(1);
            }
            int spell = rc_spell_find("Wind Strike");
            if (spell < 0) { fprintf(stderr, "magic benchmark missing Wind Strike\n"); exit(1); }
            rc_player_set_autocast_spell(world, spell, 0);
        } else {
            world->player.equipment[EQUIP_AMMO] = (RcInvSlot){.item_id = 892, .quantity = 1000000000};
        }
        rc_recalc_bonuses(&world->player);
    }

    if (mode == RC_BENCH_PRAYER) {
        world->player.current_prayer_points = 1000000000;
        rc_player_set_prayer(world, RC_PRAYER_PROTECT_FROM_MELEE);
        rc_player_set_prayer(world, RC_PRAYER_ULTIMATE_STRENGTH);
    }
    if (mode == RC_BENCH_ENCOUNTER) {
        rc_content_combat_register(world);
        rc_test_open_mapsquare(world, 3200, 3200, 0);
        int kbd = rc_npc_def_find(239);
        if (*npc_def_idx < 0) *npc_def_idx = rc_find_bench_npc_def();
        for (int i = 0; kbd >= 0 && i < 16; i++) {
            int idx = rc_npc_spawn(world, kbd, 3204 + (i % 4) * 12,
                                   3204 + (i / 4) * 12, 0);
            if (idx < 0) break;
            world->npcs[idx].disable_wander = true;
        }
        for (int i = 0; world->npc_count < world->npc_capacity - 8 && i < 960; i++)
            rc_npc_spawn(world, *npc_def_idx, 3200 + i % 64, 3250 + i / 64, 0);
        if (kbd < 0 || world->npc_count < 900) {
            fprintf(stderr, "failed to spawn encounter benchmark NPCs\n");
            exit(1);
        }
        world->npcs[0].current_hp = world->npcs[0].spawn_hp = 1000000000;
        world->player.x = 3206;
        world->player.y = 3203;
        if (!rc_combat_start_player_vs_npc(world, 0, world->npcs[0].uid)) {
            fprintf(stderr, "failed to start encounter benchmark combat\n");
            exit(1);
        }
    } else if (mode != RC_BENCH_IDLE) {
        rc_test_open_mapsquare(world, 3200, 3200, 0);
        if (*npc_def_idx < 0) *npc_def_idx = rc_find_bench_npc_def();
        int npc_idx = rc_npc_spawn(world, *npc_def_idx, 3201, 3200, 0);
        if (npc_idx < 0) {
            fprintf(stderr, "failed to spawn benchmark npc\n");
            exit(1);
        }
        world->npcs[npc_idx].current_hp = 1000000000;
        world->npcs[npc_idx].spawn_hp = 1000000000;
        world->npcs[npc_idx].disable_wander = true;
        if (!rc_combat_start_player_vs_npc(world, 0, world->npcs[npc_idx].uid)) {
            fprintf(stderr, "failed to start benchmark combat\n");
            exit(1);
        }
    }

    return world;
}

static void rc_print_usage(const char *argv0) {
    fprintf(stderr,
            "usage: %s [--mode idle|combat|prayer|magic|ranged|consumables|encounter] [--envs N] [--steps N] [--warmup N]\n",
            argv0);
}

int main(int argc, char **argv) {
    RcBenchMode mode = RC_BENCH_COMBAT;
    int envs = 64;
    int steps = 20000;
    int warmup = 1000;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--mode") == 0 && i + 1 < argc) {
            i++;
            if (strcmp(argv[i], "combat") == 0) {
                mode = RC_BENCH_COMBAT;
            } else if (strcmp(argv[i], "prayer") == 0) {
                mode = RC_BENCH_PRAYER;
            } else if (strcmp(argv[i], "idle") == 0) {
                mode = RC_BENCH_IDLE;
            } else if (strcmp(argv[i], "magic") == 0) {
                mode = RC_BENCH_MAGIC;
            } else if (strcmp(argv[i], "ranged") == 0) {
                mode = RC_BENCH_RANGED;
            } else if (strcmp(argv[i], "consumables") == 0) {
                mode = RC_BENCH_CONSUMABLES;
            } else if (strcmp(argv[i], "encounter") == 0) {
                mode = RC_BENCH_ENCOUNTER;
            } else {
                rc_print_usage(argv[0]);
                return 2;
            }
        } else if (strcmp(argv[i], "--envs") == 0 && i + 1 < argc) {
            envs = rc_parse_positive("--envs", argv[++i], envs);
        } else if (strcmp(argv[i], "--steps") == 0 && i + 1 < argc) {
            steps = rc_parse_positive("--steps", argv[++i], steps);
        } else if (strcmp(argv[i], "--warmup") == 0 && i + 1 < argc) {
            warmup = rc_parse_positive("--warmup", argv[++i], warmup);
        } else if (strcmp(argv[i], "--help") == 0) {
            rc_print_usage(argv[0]);
            return 0;
        } else {
            rc_print_usage(argv[0]);
            return 2;
        }
    }

    int npc_def_idx = -1;
    RcWorld **worlds = calloc((size_t)envs, sizeof(*worlds));
    if (!worlds) {
        fprintf(stderr, "failed to allocate %d benchmark worlds\n", envs);
        return 1;
    }

    for (int i = 0; i < envs; i++) {
        worlds[i] = rc_make_world(9001 + i, mode, &npc_def_idx);
    }

    for (int step = 0; step < warmup; step++) {
        for (int env = 0; env < envs; env++) {
            if (mode == RC_BENCH_CONSUMABLES && step % 10 == 0) {
                RcWorld *w = worlds[env];
                if (rc_player_inventory_add(w,385,1,0).code != RC_ITEM_RESULT_OK
                        || !rc_player_eat(w,rc_inv_find(w->player.inventory,385))) {
                    fprintf(stderr,"consumables benchmark: stocking or eating failed\n");
                    return 1;
                }
            }
            rc_world_tick(worlds[env]);
        }
    }

    double start = rc_now_seconds();
    for (int step = 0; step < steps; step++) {
        for (int env = 0; env < envs; env++) {
            if (mode == RC_BENCH_CONSUMABLES && step % 10 == 0) {
                RcWorld *w = worlds[env];
                if (rc_player_inventory_add(w,385,1,0).code != RC_ITEM_RESULT_OK
                        || !rc_player_eat(w,rc_inv_find(w->player.inventory,385))) {
                    fprintf(stderr,"consumables benchmark: stocking or eating failed\n");
                    return 1;
                }
            }
            rc_world_tick(worlds[env]);
        }
    }
    double elapsed = rc_now_seconds() - start;

    for (int env = 0; mode != RC_BENCH_IDLE && env < envs; env++) {
        if (mode == RC_BENCH_CONSUMABLES && (!worlds[env]->player.consume_sequence
                || rc_inv_find(worlds[env]->player.inventory,385) >= 0)) {
            fprintf(stderr,"consumables benchmark: queued consumption did not commit\n");
            return 1;
        }
        if (worlds[env]->npcs[0].current_hp >= 1000000000) {
            fprintf(stderr, "combat benchmark failed: env %d inflicted no damage\n", env);
            return 1;
        }
        if (mode == RC_BENCH_PRAYER && worlds[env]->player.active_prayers
                != (PRAYER_PROTECT_MELEE | PRAYER_ULTIMATE_STR)) {
            fprintf(stderr, "prayer benchmark failed: env %d prayers inactive\n", env);
            return 1;
        }
        if ((mode == RC_BENCH_MAGIC && (worlds[env]->player.autocast_spell < 0 ||
                worlds[env]->player.inventory[0].quantity >= 1000000000)) ||
            (mode == RC_BENCH_RANGED && worlds[env]->player.equipment[EQUIP_AMMO].quantity >= 1000000000)) {
            fprintf(stderr, "resource benchmark failed: env %d did not sustain paid attacks\n", env);
            return 1;
        }
    }

    uint64_t total_steps = (uint64_t)(uint32_t)envs * (uint64_t)(uint32_t)steps;
    double sps = (double)total_steps / elapsed;
    double ns_per_step = (elapsed * 1000000000.0) / (double)total_steps;

    printf("RuneC SPS benchmark\n");
    printf("mode: %s\n", rc_mode_name(mode));
    printf("envs: %d\n", envs);
    printf("warmup_steps_per_env: %d\n", warmup);
    printf("timed_steps_per_env: %d\n", steps);
    printf("total_env_steps: %llu\n", (unsigned long long)total_steps);
    printf("elapsed_seconds: %.6f\n", elapsed);
    printf("sps: %.0f\n", sps);
    printf("ns_per_env_step: %.2f\n", ns_per_step);

    for (int i = 0; i < envs; i++) {
        rc_world_destroy(worlds[i]);
    }
    free(worlds);
    return 0;
}
