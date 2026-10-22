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
#include "object_runtime.h"
#include "player_actions.h"
#include "player_command.h"
#include "prayer.h"
#include "quests.h"
#include "slayer.h"
#include "spells.h"
#include "shops.h"
#include "traversal.h"
#include "varbits.h"
#include "world_state.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static double monotonic_ms(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        return 0.0;
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1000000.0;
}

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
    p->route_entity_width = 1;
    p->route_entity_height = 1;
    p->route_status = RC_ROUTE_FAILED;
    p->movement_result = RC_MOVEMENT_NONE;
    p->attack_target = -1;
    p->attack_target_def_id = -1;
    p->interact_type = RC_INTERACT_NONE;
    p->interact_target = -1;
    p->interact_option = -1;
    p->storage_target = -1;
    p->storage_option = -1;
    p->skill_target_x = -1;
    p->skill_target_y = -1;
    rc_traversal_clear(p);
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
    rc_prayer_reset(p);

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

static int init_world_state(RcWorld *world) {
    if (!world || !world->game_data || !world->npcs
            || world->npc_capacity <= 0) {
        return 0;
    }
    world->rng_state = world->initial_seed;
    world->tick = 0;
    world->next_ground_item_uid = 0;
    world->next_npc_uid = 0;
    world->next_dynamic_object_key = UINT64_MAX;
    world->player_commands.next_sequence = 0;
    world->player_commands.last_result = RC_COMMAND_RESULT_NONE;
    rc_events_init(&world->events);
    if ((world->enabled & RC_SUB_SLAYER) && rc_slayer_init(world) != 0)
        return 0;
    if (world->enabled & RC_SUB_ENCOUNTER) {
        int spec_count = 0;
        const RcEncounterSpec *specs =
            rc_game_data_encounter_specs(world->game_data, &spec_count);
        if (rc_encounter_init(world) != 0
                || rc_encounter_use_specs(world, specs, spec_count) != 0)
            return 0;
    }
    init_player_defaults(&world->player);
    return rc_interaction_install_world_defaults(world);
}

RcWorld *rc_world_create_config(const RcWorldConfig *cfg) {
    if (!cfg) return NULL;

    char config_error[256];
    if (!rc_world_config_validate(cfg, config_error, sizeof(config_error))) {
        fprintf(stderr, "rc-core: %s\n", config_error);
        return NULL;
    }

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
    char config_error[256];
    if (!rc_world_config_validate(cfg, config_error, sizeof(config_error))) {
        fprintf(stderr, "rc-core: %s\n", config_error);
        return NULL;
    }
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
    world->npcs = calloc((size_t)cfg->npc_capacity, sizeof(*world->npcs));
    if (!world->npcs) {
        free(world);
        return NULL;
    }
    world->npc_capacity = cfg->npc_capacity;
    world->game_data = data;
    rc_game_data_retain(data);

    world->initial_seed = cfg->seed ? cfg->seed : RC_DEFAULT_SEED;
    world->enabled = cfg->subsystems;
    world->members_world = cfg->members_world;
    world->streaming = cfg->streaming;
    rc_world_streaming_config_sanitize(&world->streaming);
    copy_world_path(world->npc_spawns_path, sizeof(world->npc_spawns_path),
                    cfg->spawns_path);
    copy_world_path(world->ground_item_spawns_path,
                    sizeof(world->ground_item_spawns_path),
                    cfg->ground_item_spawns_path);

    if (!init_world_state(world)) {
        rc_world_destroy(world);
        return NULL;
    }
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
    rc_world_state_destroy(world);
    rc_game_data_release(world->game_data);
    free(world->npcs);
    free(world);
}

int rc_world_install_content(RcWorld *world, RcWorldContentInstaller install) {
    if (!world || !install) return 0;
    world->content_installer = install;
    return install(world) == 1;
}

int rc_world_reset(RcWorld *world) {
    if (!world || !world->game_data || !world->npcs) return 0;

    RcGameData *game_data = world->game_data;
    RcNpc *npcs = world->npcs;
    int npc_capacity = world->npc_capacity;
    int next_npc_uid = world->next_npc_uid;
    uint32_t initial_seed = world->initial_seed;
    uint32_t enabled = world->enabled;
    bool members_world = world->members_world;
    RcWorldStreamingConfig streaming = world->streaming;
    RcWorldContentInstaller installer = world->content_installer;
    char spawns_path[sizeof(world->npc_spawns_path)];
    memcpy(spawns_path, world->npc_spawns_path, sizeof(spawns_path));
    char ground_spawns_path[sizeof(world->ground_item_spawns_path)];
    memcpy(ground_spawns_path, world->ground_item_spawns_path,
           sizeof(ground_spawns_path));

    rc_world_state_destroy(world);
    memset(npcs, 0, (size_t)npc_capacity * sizeof(*npcs));
    memset(world, 0, sizeof(*world));
    world->game_data = game_data;
    world->npcs = npcs;
    world->npc_capacity = npc_capacity;
    world->initial_seed = initial_seed;
    world->enabled = enabled;
    world->members_world = members_world;
    world->streaming = streaming;
    memcpy(world->npc_spawns_path, spawns_path, sizeof(spawns_path));
    memcpy(world->ground_item_spawns_path, ground_spawns_path,
           sizeof(ground_spawns_path));
    if (!init_world_state(world)) return 0;
    world->next_npc_uid = next_npc_uid;
    return !installer || rc_world_install_content(world, installer);
}

const RcGameData *rc_world_get_game_data(const RcWorld *world) {
    return world ? world->game_data : NULL;
}

const RcPlayer *rc_get_player(const RcWorld *world) {
    return world ? &world->player : NULL;
}

RcTick rc_world_get_tick(const RcWorld *world) {
    return world ? world->tick : 0;
}

RcPlayerCommandResult rc_player_last_command_result(const RcWorld *world,
                                                     uint64_t *sequence) {
    if (sequence) {
        *sequence = world ? world->player_commands.last_sequence : 0;
    }
    return world ? world->player_commands.last_result
                 : RC_COMMAND_RESULT_REJECTED_INVALID;
}

int rc_player_pending_command_count(const RcWorld *world) {
    return world ? world->player_commands.count : 0;
}

int rc_world_relocate_player(RcWorld *world, int x, int y, int plane) {
    if (!world || !rc_world_tile_valid(x, y, plane)) {
        return 0;
    }
    if (rc_world_activate_area_around(world, x, y, plane, NULL) < 0)
        return 0;
    rc_player_cancel_action(world, RC_ACTION_CANCEL_RELOCATED);
    RcPlayer *player = &world->player;
    player->prev_x = player->x;
    player->prev_y = player->y;
    player->x = x;
    player->y = y;
    player->plane = plane;
    return 1;
}

int rc_world_respawn_player(RcWorld *world, int x, int y, int plane) {
    if (!world || !world->player.is_dead) return 0;
    if (!rc_world_relocate_player(world, x, y, plane)) return 0;
    rc_combat_reset_player_life(world);
    world->player.current_hp = world->player.max_hp;
    world->player.is_dead = false;
    world->player.death_tick = 0;
    return 1;
}

const RcNpc *rc_get_npcs(const RcWorld *world, int *count) {
    if (count) *count = world ? world->npc_count : 0;
    return world ? world->npcs : NULL;
}

static int valid_active_area_request(const RcActiveAreaRequest *request) {
    RcTileRect rect;
    return request && rc_plane_valid(request->min_plane)
        && rc_plane_valid(request->max_plane)
        && request->min_plane <= request->max_plane
        && (request->options & ~RC_ACTIVE_AREA_INCLUDE_INSTANCE_NPCS) == 0
        && rc_tile_rect_from_origin_size(
            request->origin_x, request->origin_y,
            request->width, request->height, &rect);
}

static uint32_t active_area_components(const RcWorld *world) {
    uint32_t components = 0;
    const uint32_t npc_backed = RC_SUB_COMBAT | RC_SUB_DIALOGUE | RC_SUB_SHOPS
                              | RC_SUB_SLAYER | RC_SUB_ENCOUNTER;
    if (world->enabled & RC_SUB_REGIONS)
        components |= RC_ACTIVE_AREA_COMPONENT_COLLISION;
    if ((world->enabled & npc_backed) && world->npc_spawns_path[0])
        components |= RC_ACTIVE_AREA_COMPONENT_NPCS;
    if ((world->enabled & RC_SUB_LOOT) && world->ground_item_spawns_path[0])
        components |= RC_ACTIVE_AREA_COMPONENT_GROUND_ITEMS;
    if (world->enabled & RC_SUB_OBJECTS)
        components |= RC_ACTIVE_AREA_COMPONENT_OBJECT_CACHE;
    return components;
}

static int same_active_area(const RcWorld *world,
                            const RcActiveAreaRequest *request,
                            uint32_t components) {
    const RcActiveArea *area = &world->active_area;
    return area->active && area->origin_x == request->origin_x
        && area->origin_y == request->origin_y
        && area->width == request->width && area->height == request->height
        && area->min_plane == request->min_plane
        && area->max_plane == request->max_plane
        && area->options == request->options
        && area->components == components;
}

static int point_in_active_request(const RcActiveAreaRequest *request,
                                   int x, int y, int plane) {
    RcTileRect bounds;
    return rc_tile_rect_from_origin_size(
               request->origin_x, request->origin_y,
               request->width, request->height, &bounds)
        && rc_tile_rect_contains(&bounds, x, y)
        && plane >= request->min_plane && plane <= request->max_plane;
}

static void retain_dynamic_npcs(RcWorld *world,
                                const RcActiveAreaRequest *request) {
    int write = 0;
    for (int i = 0; i < world->npc_count; i++) {
        RcNpc *npc = &world->npcs[i];
        if (!npc->active || npc->spawn_key != 0
                || !point_in_active_request(
                    request, npc->x, npc->y, npc->plane)) {
            continue;
        }
        if (write != i) world->npcs[write] = *npc;
        write++;
    }
    if (write < world->npc_count) {
        memset(&world->npcs[write], 0,
               (size_t)(world->npc_count - write) * sizeof(world->npcs[0]));
    }
    world->npc_count = write;
}

static RcWorld *clone_area_stage(const RcWorld *world) {
    RcWorld *stage = malloc(sizeof(*stage));
    if (!stage) return NULL;
    memcpy(stage, world, sizeof(*stage));
    stage->npcs = calloc((size_t)world->npc_capacity, sizeof(*stage->npcs));
    if (!stage->npcs) {
        free(stage);
        return NULL;
    }
    memcpy(stage->npcs, world->npcs,
           (size_t)world->npc_count * sizeof(*stage->npcs));
    if (!rc_world_state_clone_dormant(stage, world)) {
        free(stage->npcs);
        free(stage);
        return NULL;
    }
    rc_events_init(&stage->events);
    return stage;
}

static void discard_area_stage(RcWorld *stage) {
    if (!stage) return;
    rc_world_state_discard_dormant(stage);
    free(stage->npcs);
    free(stage);
}

static int npc_uid_present(const RcWorld *world, int uid) {
    for (int i = 0; world && i < world->npc_count; i++)
        if (world->npcs[i].active && world->npcs[i].uid == uid) return 1;
    return 0;
}

static int commit_area_stage(RcWorld *world, RcWorld *stage) {
    RcPayloadNpcEvent *removed = world->npc_count > 0
        ? calloc((size_t)world->npc_count, sizeof(*removed)) : NULL;
    RcPayloadNpcEvent *spawned = stage->npc_count > 0
        ? calloc((size_t)stage->npc_count, sizeof(*spawned)) : NULL;
    if ((world->npc_count > 0 && !removed)
            || (stage->npc_count > 0 && !spawned)) {
        free(removed);
        free(spawned);
        return 0;
    }
    int removed_count = 0;
    int spawned_count = 0;
    for (int i = 0; i < world->npc_count; i++) {
        const RcNpc *npc = &world->npcs[i];
        if (!npc->active || npc_uid_present(stage, npc->uid)) continue;
        const RcNpcDef *def = rc_npc_base_def_for_npc(npc);
        if (removed) {
            removed[removed_count++] = (RcPayloadNpcEvent){
                .npc_id = (uint32_t)npc->uid,
                .def_id = def ? (uint32_t)def->id : UINT32_MAX,
                .spawn_key = npc->spawn_key,
            };
        }
    }
    for (int i = 0; i < stage->npc_count; i++) {
        const RcNpc *npc = &stage->npcs[i];
        if (!npc->active || npc_uid_present(world, npc->uid)) continue;
        const RcNpcDef *def = rc_npc_base_def_for_npc(npc);
        if (spawned) {
            spawned[spawned_count++] = (RcPayloadNpcEvent){
                .npc_id = (uint32_t)npc->uid,
                .def_id = def ? (uint32_t)def->id : UINT32_MAX,
                .spawn_key = npc->spawn_key,
            };
        }
    }

    if (world->npc_count > stage->npc_count) {
        memset(&world->npcs[stage->npc_count], 0,
               (size_t)(world->npc_count - stage->npc_count)
                   * sizeof(*world->npcs));
    }
    memcpy(world->npcs, stage->npcs,
           (size_t)stage->npc_count * sizeof(*world->npcs));
    world->npc_count = stage->npc_count;
    world->next_npc_uid = stage->next_npc_uid;
    world->map = stage->map;
    memcpy(world->ground_items, stage->ground_items,
           sizeof(world->ground_items));
    world->ground_item_count = stage->ground_item_count;
    world->next_ground_item_uid = stage->next_ground_item_uid;
    rc_world_state_discard_dormant(world);
    world->dormant_npcs = stage->dormant_npcs;
    world->dormant_npc_count = stage->dormant_npc_count;
    world->dormant_npc_capacity = stage->dormant_npc_capacity;
    world->dormant_ground_items = stage->dormant_ground_items;
    world->dormant_ground_item_count = stage->dormant_ground_item_count;
    world->dormant_ground_item_capacity = stage->dormant_ground_item_capacity;
    stage->dormant_npcs = NULL;
    stage->dormant_ground_items = NULL;
    world->active_area = stage->active_area;
    world->streaming_telemetry = stage->streaming_telemetry;

    for (int i = 0; i < removed_count; i++) {
        rc_npc_clear_references(world, (RcNpcId)removed[i].npc_id);
        rc_event_fire(world, RC_EVT_NPC_REMOVED, &removed[i]);
    }
    for (int i = 0; i < spawned_count; i++)
        rc_event_fire(world, RC_EVT_NPC_SPAWNED, &spawned[i]);
    free(removed);
    free(spawned);
    return 1;
}

static int active_npc_count(const RcWorld *world) {
    int count = 0;
    for (int i = 0; world && i < world->npc_count; i++)
        if (world->npcs[i].active)
            count++;
    return count;
}

static int active_ground_item_count(const RcWorld *world) {
    int count = 0;
    for (int i = 0; world && i < world->ground_item_count; i++)
        if (world->ground_items[i].active)
            count++;
    return count;
}

int rc_world_activate_area(RcWorld *world, const RcActiveAreaRequest *request,
                           RcActiveAreaStats *stats) {
    if (stats) memset(stats, 0, sizeof(*stats));
    if (!world || !valid_active_area_request(request))
        return -1;

    uint32_t components = active_area_components(world);
    if (same_active_area(world, request, components)) {
        if (stats) {
            stats->collision_regions = world->map.region_count;
            stats->active_area = world->active_area;
            stats->streaming = world->streaming_telemetry;
            stats->unchanged = true;
        }
        return 1;
    }

    double area_load_started_ms = monotonic_ms();
    RcTileRect bounds;
    if (!rc_tile_rect_from_origin_size(request->origin_x, request->origin_y,
                                       request->width, request->height,
                                       &bounds)) {
        return -1;
    }
    int min_x = bounds.min_x;
    int min_y = bounds.min_y;
    int max_x = bounds.max_x;
    int max_y = bounds.max_y;

    RcWorld *stage = clone_area_stage(world);
    if (!stage) return -1;
    stage->streaming_telemetry.expired_ground_items = 0;

    int collision_regions = stage->map.region_count;
    RcCollisionLoadStats collision_stats = {0};
    int pages_loaded = 0;
    double page_load_ms = 0.0;
    if (components & RC_ACTIVE_AREA_COMPONENT_COLLISION) {
        if (rc_collision_set_cache_limit(
                world->streaming.max_cached_regions) < 0) {
            goto fail;
        }
        double page_load_started_ms = monotonic_ms();
        collision_regions = rc_collision_populate_map_rect_stats(
            &stage->map, min_x, min_y, max_x, max_y, &collision_stats);
        page_load_ms = monotonic_ms() - page_load_started_ms;
        if (collision_regions < 0)
            goto fail;
        pages_loaded += (int)collision_stats.pages_loaded;
    }

    RcObjectPlacementLoadStats object_placement_stats = {0};
    if (components & RC_ACTIVE_AREA_COMPONENT_OBJECT_CACHE) {
        if (rc_object_placements_set_cache_limit(
                world->streaming.max_cached_regions) < 0) {
            goto fail;
        }
        double page_load_started_ms = monotonic_ms();
        int object_pages = rc_object_placements_prefetch_rect(
            min_x, min_y, max_x, max_y, &object_placement_stats);
        page_load_ms += monotonic_ms() - page_load_started_ms;
        if (object_pages < 0) goto fail;
        pages_loaded += object_placement_stats.pages_loaded;
    }
    if ((components & RC_ACTIVE_AREA_COMPONENT_COLLISION)
            && !rc_world_objects_replay_collision(stage)) {
        goto fail;
    }

    int saved_npc_states = 0;
    int restored_npc_states = 0;
    int saved_ground_items = 0;
    int restored_ground_items = 0;
    RcNpcSpawnLoadStats npc_stats = {0};
    int spawned = 0;
    if (components & RC_ACTIVE_AREA_COMPONENT_NPCS) {
        saved_npc_states = rc_world_state_save_npcs(stage);
        if (saved_npc_states < 0) goto fail;
        retain_dynamic_npcs(stage, request);
        uint32_t npc_load_flags = 0;
        if (request->options & RC_ACTIVE_AREA_INCLUDE_INSTANCE_NPCS)
            npc_load_flags |= RC_NPC_SPAWN_LOAD_INCLUDE_INSTANCE;
        double page_load_started_ms = monotonic_ms();
        spawned = rc_load_npc_spawns_rect_stats_flags(
            stage, stage->npc_spawns_path, min_x, min_y, max_x, max_y,
            request->min_plane, request->max_plane, npc_load_flags,
            &npc_stats);
        page_load_ms += monotonic_ms() - page_load_started_ms;
        if (spawned < 0 || npc_stats.skipped_capacity > 0) goto fail;
        pages_loaded += npc_stats.pages_loaded;
        restored_npc_states = rc_world_state_restore_npcs(stage);
        if (restored_npc_states < 0) goto fail;
    }

    RcGroundItemSpawnLoadStats ground_item_stats = {0};
    int spawned_ground_items = 0;
    if (components & RC_ACTIVE_AREA_COMPONENT_GROUND_ITEMS) {
        saved_ground_items = rc_world_state_save_ground_items(
            stage, min_x, min_y, max_x, max_y,
            request->min_plane, request->max_plane);
        if (saved_ground_items < 0) goto fail;
        rc_clear_static_ground_items(stage);
        double page_load_started_ms = monotonic_ms();
        spawned_ground_items = rc_load_ground_item_spawns_rect_stats(
            stage, stage->ground_item_spawns_path,
            min_x, min_y, max_x, max_y,
            request->min_plane, request->max_plane, &ground_item_stats);
        page_load_ms += monotonic_ms() - page_load_started_ms;
        if (spawned_ground_items < 0
                || ground_item_stats.skipped_invalid > 0
                || ground_item_stats.skipped_capacity > 0) {
            goto fail;
        }
        pages_loaded += ground_item_stats.pages_loaded;
        restored_ground_items = rc_world_state_restore_ground_items(
            stage, min_x, min_y, max_x, max_y,
            request->min_plane, request->max_plane);
        if (restored_ground_items < 0) goto fail;
    }

    uint32_t generation = world->active_area.generation + 1;
    stage->active_area = (RcActiveArea){
        .active = true,
        .origin_x = request->origin_x,
        .origin_y = request->origin_y,
        .width = request->width,
        .height = request->height,
        .min_plane = request->min_plane,
        .max_plane = request->max_plane,
        .options = request->options,
        .components = components,
        .generation = generation ? generation : 1,
    };

    double area_load_ms = monotonic_ms() - area_load_started_ms;
    RcWorldStreamingTelemetry *telemetry = &stage->streaming_telemetry;
    telemetry->active_area_load_count++;
    telemetry->active_area_load_ms = area_load_ms;
    telemetry->active_area_load_total_ms += area_load_ms;
    telemetry->backend_page_load_ms = page_load_ms;
    telemetry->backend_page_load_total_ms += page_load_ms;
    telemetry->backend_pages_loaded = pages_loaded;
    telemetry->active_npcs = active_npc_count(stage);
    telemetry->active_ground_items = active_ground_item_count(stage);
    telemetry->dormant_npc_states = stage->dormant_npc_count;
    telemetry->dormant_ground_items = stage->dormant_ground_item_count;
    telemetry->saved_npc_states = saved_npc_states;
    telemetry->restored_npc_states = restored_npc_states;
    telemetry->saved_ground_items = saved_ground_items;
    telemetry->restored_ground_items = restored_ground_items;

    if (!commit_area_stage(world, stage)) goto fail;

    if (stats) {
        stats->collision_regions = collision_regions;
        stats->collision_stats = collision_stats;
        stats->spawned_npcs = spawned;
        stats->spawned_ground_items = spawned_ground_items;
        stats->object_placement_stats = object_placement_stats;
        stats->npc_stats = npc_stats;
        stats->ground_item_stats = ground_item_stats;
        stats->active_area = world->active_area;
        stats->streaming = world->streaming_telemetry;
    }
    discard_area_stage(stage);
    return 1;

fail:
    discard_area_stage(stage);
    return -1;
}

int rc_world_activate_area_around(RcWorld *world, int x, int y, int plane,
                                  RcActiveAreaStats *stats) {
    if (!world || !rc_world_tile_valid(x, y, plane)) return -1;
    int radius = world->streaming.active_radius_regions;
    int center_x = x / RC_MAPSQUARE_SIZE;
    int center_y = y / RC_MAPSQUARE_SIZE;
    int min_region_x = center_x - radius;
    int min_region_y = center_y - radius;
    int max_region_x = center_x + radius;
    int max_region_y = center_y + radius;
    if (min_region_x < 0) min_region_x = 0;
    if (min_region_y < 0) min_region_y = 0;
    if (max_region_x >= RC_MAPSQUARE_AXIS)
        max_region_x = RC_MAPSQUARE_AXIS - 1;
    if (max_region_y >= RC_MAPSQUARE_AXIS)
        max_region_y = RC_MAPSQUARE_AXIS - 1;
    RcActiveAreaRequest request = {
        .origin_x = min_region_x * RC_MAPSQUARE_SIZE,
        .origin_y = min_region_y * RC_MAPSQUARE_SIZE,
        .width = (max_region_x - min_region_x + 1) * RC_MAPSQUARE_SIZE,
        .height = (max_region_y - min_region_y + 1) * RC_MAPSQUARE_SIZE,
        .min_plane = 0,
        .max_plane = RC_MAX_PLANES - 1,
        .options = RC_ACTIVE_AREA_INCLUDE_INSTANCE_NPCS,
    };
    return rc_world_activate_area(world, &request, stats);
}

const RcActiveArea *rc_world_get_active_area(const RcWorld *world) {
    return world ? &world->active_area : NULL;
}

const RcWorldStreamingConfig *rc_world_get_streaming_config(
    const RcWorld *world) {
    return world ? &world->streaming : NULL;
}

int rc_world_get_streaming_telemetry(const RcWorld *world,
                                     RcWorldStreamingTelemetry *out) {
    if (!world || !out)
        return -1;
    *out = world->streaming_telemetry;
    out->active_npcs = active_npc_count(world);
    out->active_ground_items = active_ground_item_count(world);
    out->dormant_npc_states = world->dormant_npc_count;
    out->dormant_ground_items = world->dormant_ground_item_count;
    return 1;
}

int rc_world_find_npc_near(const RcWorld *world, int npc_id, int x, int y,
                           int plane, int radius) {
    RcTileRect search;
    if (!world || npc_id < 0 || !rc_plane_valid(plane)
            || !rc_tile_rect_around(x, y, radius, &search)) {
        return -1;
    }
    for (int i = 0; i < world->npc_count; i++) {
        const RcNpc *npc = &world->npcs[i];
        const RcNpcDef *def = rc_npc_def_for_npc(world, npc);
        const RcNpcDef *base = rc_npc_base_def_for_npc(npc);
        if (!npc->active || !def)
            continue;
        // A boss in another form (Vorkath awake) is still that spawn.
        if (npc->plane != plane
                || (def->id != npc_id && (!base || base->id != npc_id)))
            continue;
        if (rc_tile_rect_contains(&search, npc->x, npc->y))
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
