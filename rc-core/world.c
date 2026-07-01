#include "api.h"
#include "combat.h"
#include "combat_profiles.h"
#include "area_flags.h"
#include "activity_mechanics.h"
#include "activity_schemas.h"
#include "activity_spawns.h"
#include "activity_states.h"
#include "rng.h"
#include "skills.h"
#include "config.h"
#include "collision.h"
#include "dialogue.h"
#include "drops.h"
#include "events.h"
#include "encounter.h"
#include "game_data.h"
#include "items.h"
#include "interaction.h"
#include "monster_mechanics.h"
#include "npc.h"
#include "normalization.h"
#include "objects.h"
#include "player_actions.h"
#include "prayer.h"
#include "quests.h"
#include "slayer.h"
#include "spells.h"
#include "shops.h"
#include "traversal.h"
#include "varbits.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static void copy_world_path(char *dst, size_t cap, const char *src) {
    if (!dst || cap == 0) return;
    if (!src) {
        dst[0] = '\0';
        return;
    }
    snprintf(dst, cap, "%s", src);
}

static void init_player_defaults(RcPlayer *p) {
    // Varrock centre start for legacy-seeded worlds. RL configs
    // typically override player position via their own init step.
    p->x = 3213;
    p->y = 3428;
    p->plane = 0;
    p->prev_x = p->x;
    p->prev_y = p->y;
    p->attack_target = -1;
    p->attack_target_def_id = -1;
    p->interact_type = RC_INTERACT_NONE;
    p->interact_target = -1;
    p->interact_option = -1;
    p->storage_target = -1;
    p->storage_option = -1;
    p->skill_target_x = -1;
    p->skill_target_y = -1;
    p->pending_traversal_x = -1;
    p->pending_traversal_y = -1;
    p->pending_traversal_plane = -1;
    rc_interaction_clear(p);
    p->combat_style = COMBAT_MELEE_CRUSH;
    p->attack_style_idx = 0;
    p->attack_stance = RC_ATTACK_STANCE_ACCURATE;
    p->combat_xp_mask = RC_COMBAT_XP_ATTACK;
    p->special_energy = 10000;
    p->current_spellbook = RC_SPELL_BOOK_STANDARD;
    p->selected_spell = -1;
    p->manual_spell_cast = -1;
    p->autocast_spell = -1;
    p->defensive_autocast = false;
    for (int i = 0; i < 4; i++) {
        p->rune_pouch[i].item_id = -1;
        p->rune_pouch[i].quantity = 0;
    }
    p->last_hit = -1;
    p->facing_entity = -1;
    p->facing_x = -1;
    p->facing_y = -1;
    p->slayer_master_idx = -1;
    p->slayer_task_idx = -1;
    p->run_energy = 10000;
    p->auto_retaliate = true;
    rc_combat_init_player_state(p);

    // Default stats: level 1 everything, 10 HP.
    for (int i = 0; i < SKILL_COUNT; i++) {
        p->skills.base_level[i] = 1;
        p->skills.boosted_level[i] = 1;
        p->skills.xp[i] = 0;
    }
    p->skills.base_level[SKILL_HITPOINTS] = 10;
    p->skills.boosted_level[SKILL_HITPOINTS] = 10;
    p->skills.xp[SKILL_HITPOINTS] = 1154;   // XP for level 10
    p->current_hp = 100;                    // tenths-of-HP
    p->max_hp = 100;

    // Empty inventory + equipment.
    for (int i = 0; i < RC_INVENTORY_SIZE; i++) {
        p->inventory[i].item_id = -1;
    }
    for (int i = 0; i < RC_BANK_SIZE; i++) {
        p->bank[i].item_id = -1;
        p->bank_tab[i] = 0;
    }
    for (int i = 0; i < RC_EQUIP_COUNT; i++) {
        p->equipment[i].item_id = -1;
    }
}

RcWorld *rc_world_create_config(const RcWorldConfig *cfg) {
    if (!cfg) return NULL;

    RcGameDataLoadReport data_report;
    RcGameData *data = rc_game_data_load(cfg, &data_report);
    if (!data) {
        fprintf(stderr, "rc-core: %s\n", data_report.message);
        return NULL;
    }

    RcWorld *world = rc_world_create_with_data(data, cfg);
    rc_game_data_release(data);
    return world;
}

RcWorld *rc_world_create_with_data(RcGameData *data,
                                   const RcWorldConfig *cfg) {
    if (!data || !cfg) return NULL;
    uint32_t data_subsystems = rc_game_data_subsystems(data);
    if ((cfg->subsystems & ~data_subsystems) != 0) {
        fprintf(stderr,
                "rc-core: RcGameData missing requested subsystem bits 0x%08x\n",
                cfg->subsystems & ~data_subsystems);
        return NULL;
    }

    // calloc is fine here — world_create is not on the tick path
    // (see rc-core/README.md §10).
    RcWorld *world = calloc(1, sizeof(RcWorld));
    if (!world) return NULL;
    world->game_data = data;
    rc_game_data_retain(data);
    rc_game_data_activate_views(data, cfg->subsystems);

    world->rng_state = cfg->seed;
    world->tick = 0;
    world->enabled = cfg->subsystems;
    copy_world_path(world->npc_spawns_path, sizeof(world->npc_spawns_path),
                    cfg->spawns_path);

    rc_events_init(&world->events);
    if (cfg->player_actions_path && g_rc_player_action_count == 0) {
        rc_load_player_actions(cfg->player_actions_path);
    }
    if ((cfg->subsystems & RC_SUB_COMBAT) && cfg->combat_profiles_path
            && g_rc_combat_profile_count == 0) {
        rc_load_combat_profiles(cfg->combat_profiles_path);
    }
    uint32_t mechanics_users = RC_SUB_COMBAT | RC_SUB_SLAYER
                             | RC_SUB_ENCOUNTER;
    if ((cfg->subsystems & mechanics_users) && cfg->monster_mechanics_path
            && g_rc_monster_mechanic_family_count == 0) {
        rc_load_monster_mechanics(cfg->monster_mechanics_path);
    }
    if ((cfg->subsystems & RC_SUB_SLAYER) && cfg->slayer_path
            && g_rc_slayer_master_count == 0) {
        rc_load_slayer(cfg->slayer_path);
    }
    if (cfg->subsystems & RC_SUB_SLAYER) {
        rc_slayer_init(world);
    }
    // Only enabled subsystems subscribe to the event bus — keeps
    // disabled-subsystem worlds event-free (README §7 + §8).
    if (cfg->subsystems & RC_SUB_ENCOUNTER) {
        rc_encounter_init(world);
        int spec_count = 0;
        const RcEncounterSpec *specs =
            rc_game_data_encounter_specs(data, &spec_count);
        for (int i = 0; specs && i < spec_count; i++) {
            rc_encounter_register(world, &specs[i]);
        }
    }
    init_player_defaults(&world->player);
    // Full world spawns stay explicit: callers load a world slice with
    // rc_load_npc_spawns_rect/near after choosing the active area.

    return world;
}

// Legacy create — used by viewer + tests that haven't been updated
// to pass a full config. Defaults to the full-game preset.
RcWorld *rc_world_create(uint32_t seed) {
    RcWorldConfig cfg = rc_preset_full_game();
    cfg.seed = seed;
    return rc_world_create_config(&cfg);
}

void rc_world_destroy(RcWorld *world) {
    if (!world) return;
    rc_game_data_release(world->game_data);
    free(world);
}

const RcGameData *rc_world_get_game_data(const RcWorld *world) {
    return world ? world->game_data : NULL;
}

const RcPlayer *rc_get_player(const RcWorld *world) {
    return &world->player;
}

const RcNpc *rc_get_npcs(const RcWorld *world, int *count) {
    if (count) *count = world->npc_count;
    return world->npcs;
}

static int valid_active_area_request(const RcActiveAreaRequest *request) {
    return request && request->width > 0 && request->height > 0
        && request->min_plane >= 0 && request->max_plane < RC_MAX_PLANES
        && request->min_plane <= request->max_plane;
}

static void clear_active_npcs(RcWorld *world) {
    memset(world->npcs, 0, sizeof(world->npcs));
    world->npc_count = 0;
}

int rc_world_activate_area(RcWorld *world, const RcActiveAreaRequest *request,
                           RcActiveAreaStats *stats) {
    if (stats) memset(stats, 0, sizeof(*stats));
    if (!world || !valid_active_area_request(request))
        return -1;

    uint32_t flags = request->flags;
    if (flags == 0) {
        flags = RC_ACTIVE_AREA_LOAD_COLLISION
              | RC_ACTIVE_AREA_LOAD_NPCS
              | RC_ACTIVE_AREA_CLEAR_NPCS;
    }

    int min_x = request->origin_x;
    int min_y = request->origin_y;
    int max_x = request->origin_x + request->width - 1;
    int max_y = request->origin_y + request->height - 1;

    int collision_regions = world->map.region_count;
    if (flags & RC_ACTIVE_AREA_LOAD_COLLISION) {
        collision_regions = rc_collision_populate_map_rect(&world->map,
                                                           min_x, min_y,
                                                           max_x, max_y);
        if (collision_regions < 0)
            return -1;
    }

    if (flags & RC_ACTIVE_AREA_CLEAR_NPCS)
        clear_active_npcs(world);

    RcNpcSpawnLoadStats npc_stats;
    memset(&npc_stats, 0, sizeof(npc_stats));
    int spawned = 0;
    if (flags & RC_ACTIVE_AREA_LOAD_NPCS) {
        const char *path = request->npc_spawns_path && request->npc_spawns_path[0]
                         ? request->npc_spawns_path : world->npc_spawns_path;
        if (path && path[0]) {
            spawned = rc_load_npc_spawns_rect_stats(
                world, path, min_x, min_y, max_x, max_y,
                request->min_plane, request->max_plane, &npc_stats);
            if (spawned < 0)
                return -1;
        }
    }

    uint32_t generation = world->active_area.generation + 1;
    world->active_area = (RcActiveArea){
        .active = true,
        .origin_x = request->origin_x,
        .origin_y = request->origin_y,
        .width = request->width,
        .height = request->height,
        .min_plane = request->min_plane,
        .max_plane = request->max_plane,
        .generation = generation ? generation : 1,
    };

    if (stats) {
        stats->collision_regions = collision_regions;
        stats->spawned_npcs = spawned;
        stats->npc_stats = npc_stats;
        stats->active_area = world->active_area;
    }
    return 1;
}

const RcActiveArea *rc_world_get_active_area(const RcWorld *world) {
    return world ? &world->active_area : NULL;
}

int rc_world_find_npc_near(const RcWorld *world, int npc_id, int x, int y,
                           int plane, int radius) {
    if (!world || npc_id < 0 || plane < 0 || plane >= RC_MAX_PLANES
            || radius < 0)
        return -1;
    for (int i = 0; i < world->npc_count; i++) {
        const RcNpc *npc = &world->npcs[i];
        const RcNpcDef *def = rc_npc_def_for_npc(npc);
        if (!npc->active || !def)
            continue;
        if (npc->plane != plane || def->id != npc_id)
            continue;
        if (abs(npc->x - x) <= radius && abs(npc->y - y) <= radius)
            return i;
    }
    return -1;
}

int rc_world_ensure_npc_near(RcWorld *world, int npc_id, int x, int y,
                             int plane, int radius,
                             RcNpcEnsureResult *result) {
    if (result) {
        result->index = -1;
        result->uid = -1;
        result->spawned = 0;
    }
    if (!world)
        return -1;

    int idx = rc_world_find_npc_near(world, npc_id, x, y, plane, radius);
    if (idx >= 0) {
        if (result) {
            result->index = idx;
            result->uid = world->npcs[idx].uid;
        }
        return idx;
    }

    int def_idx = rc_npc_def_find(npc_id);
    if (def_idx < 0)
        return -1;
    idx = rc_npc_spawn(world, def_idx, x, y, plane);
    if (idx < 0)
        return -1;
    if (result) {
        result->index = idx;
        result->uid = world->npcs[idx].uid;
        result->spawned = 1;
    }
    return idx;
}
