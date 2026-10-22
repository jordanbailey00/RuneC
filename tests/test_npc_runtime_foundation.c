#include "api.h"
#include "combat_hit.h"
#include "combat.h"
#include "config.h"
#include "npc.h"
#include "pathfinding.h"
#include "skills.h"
#include "varbits.h"
#include "runtime_test_fixture.h"
#include "world_test_fixture.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    int spawned;
    int died;
    int hidden;
    int respawned;
    int removed;
    RcPayloadNpcEvent last;
} EventLog;

static void record_lifecycle(RcWorld *world, int event, const void *payload,
                             void *ctx) {
    EventLog *log = ctx;
    const RcPayloadNpcEvent *npc = payload;
    assert(world && log && npc);
    log->last = *npc;
    if (event == RC_EVT_NPC_SPAWNED) log->spawned++;
    if (event == RC_EVT_NPC_DIED) log->died++;
    if (event == RC_EVT_NPC_HIDDEN) log->hidden++;
    if (event == RC_EVT_NPC_RESPAWNED) log->respawned++;
    if (event == RC_EVT_NPC_REMOVED) log->removed++;
}

static void seed_defs(void) {
    memset(g_npc_defs, 0, sizeof(g_npc_defs));
    g_npc_def_count = 3;
    for (int i = 0; i < g_npc_def_count; i++) {
        RcNpcDef *def = &g_npc_defs[i];
        def->id = 920000 + i;
        def->size = 1;
        def->combat_level = 10;
        def->hitpoints = 20 + i;
        def->stats[0] = 10;
        def->stats[1] = 10;
        def->stats[2] = 10;
        def->stats[3] = def->hitpoints;
        def->stats[4] = 10;
        def->stats[5] = 10;
        def->respawn_ticks = 2;
        def->regen_ticks = 2;
        def->slayer_level = 1;
        strcpy(def->name, i == 0 ? "Foundation base" :
                          i == 1 ? "Foundation form" : "Foundation other");
    }
    rc_npc_use_defs(g_npc_defs, g_npc_def_count, NULL, NULL, 0);
}

static RcWorld *make_world(uint32_t subsystems) {
    RcWorldConfig config = rc_preset_base_only();
    config.seed = 9917;
    config.subsystems = subsystems;
    config.npc_capacity = 8;
    RcWorld *world = (subsystems & RC_SUB_COMBAT)
                   ? rc_test_world_create_with_defs(
                        &config, "npc_foundation", 0)
                   : rc_world_create_config(&config);
    assert(world);
    rc_test_open_mapsquare(world, 3200, 3200, 0);
    world->player.x = 3205;
    world->player.y = 3200;
    world->player.prev_x = world->player.x;
    world->player.prev_y = world->player.y;
    world->player.plane = 0;
    return world;
}

static uint32_t *flags_at(RcWorld *world, int x, int y) {
    for (int i = 0; i < world->map.region_count; i++) {
        RcRegion *region = &world->map.regions[i];
        if (region->region_x == x / RC_REGION_SIZE
                && region->region_y == y / RC_REGION_SIZE) {
            return &region->tiles[0][x % RC_REGION_SIZE]
                                    [y % RC_REGION_SIZE].collision_flags;
        }
    }
    assert(0 && "missing test region");
    return NULL;
}

static void subscribe_lifecycle(RcWorld *world, EventLog *log) {
    const int events[] = {
        RC_EVT_NPC_SPAWNED, RC_EVT_NPC_DIED, RC_EVT_NPC_HIDDEN,
        RC_EVT_NPC_RESPAWNED, RC_EVT_NPC_REMOVED,
    };
    for (int i = 0; i < (int)(sizeof(events) / sizeof(events[0])); i++) {
        assert(rc_event_subscribe(world, events[i], record_lifecycle, log)
               == 0);
    }
}

static void test_spawn_identity_policy_and_slot_reuse(void) {
    seed_defs();
    RcWorld *world = make_world(0);
    EventLog log = {0};
    subscribe_lifecycle(world, &log);
    RcNpcSpawnConfig config = {
        .spawn_key = 0x12345678u,
        .wander_range = 0,
        .direction = 6,
        .flags = RC_NPC_SPAWN_RESPAWNS,
    };
    RcNpcSpawnConfig invalid = config;
    invalid.direction = 8;
    RcNpcSpawnResult rejected = rc_npc_spawn_ex(
        world, 0, 3200, 3200, 0, &invalid);
    assert(rejected.status == RC_NPC_SPAWN_INVALID);
    assert(world->npc_count == 0 && log.spawned == 0);
    RcNpcSpawnResult first = rc_npc_spawn_ex(world, 0, 3200, 3200, 0,
                                             &config);
    assert(first.status == RC_NPC_SPAWN_CREATED && first.slot == 0);
    RcNpc *npc = &world->npcs[first.slot];
    assert(npc->spawn_key == config.spawn_key);
    assert(npc->spawn_wander_range == 0);
    assert(npc->spawn_direction == 6);
    assert(npc->face_angle == 0 && npc->face_angle_seq == 1);
    assert(log.spawned == 1 && log.last.spawn_key == config.spawn_key);

    RcNpcSpawnResult duplicate = rc_npc_spawn_ex(
        world, 0, 3200, 3200, 0, &config);
    assert(duplicate.status == RC_NPC_SPAWN_EXISTING);
    assert(duplicate.slot == first.slot && duplicate.uid == first.uid);
    assert(log.spawned == 1 && world->npc_count == 1);
    for (int i = 0; i < 20; i++) rc_world_tick(world);
    assert(npc->x == 3200 && npc->y == 3200);

    world->player.attack_target = (int)first.uid;
    world->player.interact_target = (int)first.uid;
    assert(rc_npc_remove(world, first.uid));
    assert(rc_npc_resolve(world, first.uid) == NULL);
    assert(world->player.attack_target == -1);
    assert(world->player.interact_target == -1);
    assert(log.removed == 1 && world->npc_count == 0);

    int slot = rc_npc_spawn(world, 0, 3201, 3200, 0);
    assert(slot == first.slot);
    assert(world->npcs[slot].uid != (int)first.uid);
    assert(rc_npc_resolve(world, first.uid) == NULL);
    RcNpcId last_uid = (RcNpcId)world->npcs[slot].uid;
    assert(rc_npc_remove(world, last_uid));
    for (int i = 0; i < 64; i++) {
        int reused = rc_npc_spawn(world, 0, 3201, 3200, 0);
        assert(reused == first.slot);
        RcNpcId next_uid = (RcNpcId)world->npcs[reused].uid;
        assert(next_uid != last_uid);
        assert(rc_npc_resolve(world, last_uid) == NULL);
        assert(rc_npc_remove(world, next_uid));
        last_uid = next_uid;
    }
    assert(world->npc_count == 0);
    rc_world_destroy(world);
}

static void test_death_hide_respawn_and_full_reset(void) {
    seed_defs();
    RcWorld *world = make_world(RC_SUB_COMBAT);
    EventLog log = {0};
    subscribe_lifecycle(world, &log);
    int slot = rc_npc_spawn(world, 0, 3201, 3200, 0);
    assert(slot >= 0);
    RcNpc *npc = &world->npcs[slot];
    npc->current_hp = 1;
    npc->stats[0] = 3;
    npc->attack_timer = 8;
    npc->attack_count = 9;
    npc->wander_timer = 77;
    npc->facing_entity = 0;
    npc->poison_damage = 4;
    npc->poison_tick_counter = 12;
    RcRouteTarget target = rc_route_target_point(3204, 3200);
    assert(rc_npc_route_request(world, npc, &target, RC_NPC_ROUTE_CHASE,
                                false));
    assert(rc_queue_hit_meta(npc->pending_hits, &npc->num_pending_hits,
                             2, 0, COMBAT_MELEE_CRUSH,
                             RC_HIT_SOURCE_PLAYER, 0,
                             world->tick, 0, 2));
    rc_world_tick(world);
    assert(rc_npc_life_phase(npc) == RC_NPC_LIFE_DYING);
    assert(log.died == 1 && log.hidden == 0);

    int guard = 20;
    while (log.respawned == 0 && guard-- > 0) rc_world_tick(world);
    assert(guard > 0);
    assert(log.hidden == 1 && log.respawned == 1);
    assert(rc_npc_life_phase(npc) == RC_NPC_LIFE_ALIVE);
    assert(npc->x == npc->spawn_x && npc->y == npc->spawn_y);
    assert(npc->current_hp == npc->spawn_hp);
    assert(npc->stats[0] == g_npc_defs[0].stats[0]);
    assert(npc->attack_timer == 0 && npc->attack_count == 0);
    assert(npc->wander_timer == 0 && npc->route_len == 0);
    assert(npc->target_uid == -1 && npc->facing_entity == -1);
    assert(npc->poison_damage == 0 && npc->num_pending_hits == 0);

    RcNpcSpawnConfig temporary = {
        .wander_range = 0,
        .direction = 6,
        .flags = 0,
    };
    RcNpcSpawnResult transient = rc_npc_spawn_ex(
        world, 1, 3202, 3200, 0, &temporary);
    assert(transient.status == RC_NPC_SPAWN_CREATED);
    RcNpc *nonrespawning = rc_npc_resolve(world, transient.uid);
    assert(nonrespawning);
    assert(rc_queue_hit_meta(
        nonrespawning->pending_hits, &nonrespawning->num_pending_hits,
        nonrespawning->current_hp, 0, COMBAT_MELEE_CRUSH,
        RC_HIT_SOURCE_PLAYER, 0, world->tick, 0,
        nonrespawning->current_hp));
    rc_world_tick(world);
    guard = 10;
    while (rc_npc_resolve(world, transient.uid) && guard-- > 0)
        rc_world_tick(world);
    assert(guard > 0);
    assert(log.hidden == 2 && log.removed == 1 && log.respawned == 1);
    rc_world_destroy(world);
}

static void test_transforms_use_world_var_state(void) {
    seed_defs();
    static const int32_t transforms[] = {920001, -1, 920000, -1};
    g_npc_defs[0].transform_varp = 10;
    g_npc_defs[0].transform_varbit = -1;
    g_npc_defs[0].transform_offset = 0;
    g_npc_defs[0].transform_count = 2;
    rc_npc_use_defs(g_npc_defs, g_npc_def_count, NULL,
                    transforms, 4);
    int form_ids[4] = {0};
    int form_count = rc_npc_def_collect_form_ids(
        &g_npc_defs[0], form_ids, 4);
    assert(form_count == 2);
    assert(form_ids[0] == 920000 && form_ids[1] == 920001);
    RcWorld *world = make_world(0);
    int slot = rc_npc_spawn(world, 0, 3200, 3200, 0);
    assert(slot >= 0);
    const RcNpcDef *active = rc_npc_def_for_npc(world, &world->npcs[slot]);
    assert(active && active->id == 920001);
    world->varps[10] = 1;
    assert(rc_npc_def_for_npc(world, &world->npcs[slot]) == NULL);
    world->varps[10] = 0;
    g_npc_defs[1].transform_varp = 11;
    g_npc_defs[1].transform_offset = 2;
    g_npc_defs[1].transform_count = 2;
    assert(rc_npc_def_for_npc(world, &world->npcs[slot]) == NULL);
    form_count = rc_npc_def_collect_form_ids(&g_npc_defs[0], form_ids, 4);
    assert(form_count == 2);
    assert(rc_npc_base_def_for_npc(&world->npcs[slot])->id == 920000);
    rc_world_destroy(world);
    rc_npc_use_defs(g_npc_defs, g_npc_def_count, NULL, NULL, 0);
}

static void test_direct_step_directions_and_footprints(void) {
    seed_defs();
    RcWorld *world = make_world(0);
    int slot = rc_npc_spawn(world, 0, 3208, 3208, 0);
    RcNpc *npc = &world->npcs[slot];
    RcRouteTarget target = rc_route_target_point(3212, 3212);
    assert(rc_npc_route_request(world, npc, &target, RC_NPC_ROUTE_WANDER, false));
    rc_npc_movement_tick(world, npc);
    assert(npc->x == 3209 && npc->y == 3209 && "legal diagonals are one tick");
    *flags_at(world, 3210, 3210) = COL_LOC;
    rc_npc_movement_tick(world, npc);
    assert(npc->x == 3210 && npc->y == 3209 && "blocked diagonal tries X first");
    *flags_at(world, 3210, 3210) = 0;
    *flags_at(world, 3211, 3209) = COL_LOC;
    rc_npc_movement_tick(world, npc);
    assert(npc->x == 3210 && npc->y == 3210 && "then try Y without cutting corners");
    *flags_at(world, 3211, 3209) = 0;
    g_npc_defs[0].size = 2;
    npc->x = 3208;
    npc->y = 3208;
    target = rc_route_target_rectangle(3210, 3210, 1, 1, 1, 1, false, false);
    assert(rc_npc_route_request(world, npc, &target, RC_NPC_ROUTE_CHASE, false));
    rc_npc_movement_tick(world, npc);
    assert(npc->x == 3209 && npc->y == 3208
           && "large melee approaches a side without walking onto its target");
    assert(npc->movement_result == RC_MOVEMENT_ARRIVED);
    npc->x = 3208;
    npc->y = 3208;
    target = rc_route_target_point(3212, 3208);
    *flags_at(world, 3210, 3209) = COL_LOC;
    assert(rc_npc_route_request(world, npc, &target, RC_NPC_ROUTE_CHASE, false));
    rc_npc_movement_tick(world, npc);
    assert(npc->x == 3208 && npc->y == 3208 && "entire leading edge must be clear");
    *flags_at(world, 3210, 3209) = 0;
    rc_npc_movement_tick(world, npc);
    assert(npc->x == 3209 && npc->y == 3208 && "blocked chase resumes without requeueing");
    assert(!rc_npc_route_request(world, npc, &target, RC_NPC_ROUTE_WANDER, true));
    target.x = -1;
    assert(!rc_npc_route_request(world, npc, &target, RC_NPC_ROUTE_CHASE, false));
    g_npc_defs[0].size = 1;
    npc->x = 3208;
    npc->y = 3208;
    target = rc_route_target_rectangle(3208, 3208, 1, 1, 1, 1, false, false);
    assert(rc_npc_route_request(world, npc, &target, RC_NPC_ROUTE_CHASE, false));
    rc_npc_movement_tick(world, npc);
    assert(abs(npc->x - 3208) + abs(npc->y - 3208) == 1
           && "an overlapped target requires a cardinal step out");
    target = rc_route_target_point(npc->x + 1, npc->y);
    assert(rc_npc_route_request(world, npc, &target, RC_NPC_ROUTE_RETURN, false));
    npc->wander_timer = 12;
    rc_npc_movement_tick(world, npc);
    assert(npc->movement_result == RC_MOVEMENT_ARRIVED && !npc->wander_timer);
    target = rc_route_target_point(npc->x + 1, npc->y);
    assert(rc_npc_route_request(world, npc, &target, RC_NPC_ROUTE_CHASE, false));
    npc->x = target.x;
    rc_npc_movement_tick(world, npc);
    assert(npc->movement_result == RC_MOVEMENT_ARRIVED);
    rc_world_destroy(world);
}

static void test_direct_route_and_status_clocks(void) {
    seed_defs();
    g_npc_defs[1].regen_ticks = 0;
    g_npc_defs[2].regen_ticks = 0;
    g_npc_defs[2].poison_immune = true;
    RcWorld *world = make_world(RC_SUB_COMBAT);
    int slot = rc_npc_spawn(world, 0, 3200, 3200, 0);
    assert(slot >= 0);
    RcNpc *npc = &world->npcs[slot];
    RcRouteTarget current = rc_route_target_point(npc->x, npc->y);
    assert(rc_npc_route_request(world, npc, &current, RC_NPC_ROUTE_RETURN,
                                false));
    assert(npc->route_mode == RC_NPC_ROUTE_NONE);
    assert(npc->movement_result == RC_MOVEMENT_ARRIVED);
    *flags_at(world, 3201, 3200) = COL_BLOCK_WALK;
    RcRouteTarget target = rc_route_target_point(3202, 3200);
    assert(rc_npc_route_request(world, npc, &target, RC_NPC_ROUTE_WANDER,
                                false));
    rc_npc_movement_tick(world, npc);
    assert(npc->x == 3200 && npc->y == 3200
           && "ordinary wandering must not search around a blocker");
    assert(npc->movement_result == RC_MOVEMENT_BLOCKED);
    assert(rc_npc_route_request(world, npc, &target, RC_NPC_ROUTE_CHASE,
                                false));
    for (int i = 0; i < 8; i++) rc_npc_movement_tick(world, npc);
    assert(npc->x == 3200 && npc->y == 3200
           && npc->route_mode == RC_NPC_ROUTE_CHASE);
    // Content must explicitly request obstacle-searching scripted travel.
    assert(rc_npc_route_request(world, npc, &target, RC_NPC_ROUTE_SCRIPTED,
                                false));
    for (int i = 0; i < 8 && (npc->x != 3202 || npc->y != 3200); i++) {
        rc_npc_movement_tick(world, npc);
    }
    assert(npc->x == 3202 && npc->y == 3200);
    assert(npc->movement_result == RC_MOVEMENT_ARRIVED);

    npc->current_hp = 10;
    npc->stats[0] = 8;
    rc_npc_status_tick(world, npc);
    rc_npc_status_tick(world, npc);
    rc_npc_status_tick(world, npc);
    assert(npc->current_hp == 11 && npc->stats[0] == 9);

    int poison_slot = rc_npc_spawn(world, 1, 3203, 3202, 0);
    assert(poison_slot >= 0);
    RcNpc *poisoned = &world->npcs[poison_slot];
    assert(rc_npc_apply_poison(world, poisoned, 3));
    for (int i = 0; i <= 30; i++) rc_npc_status_tick(world, poisoned);
    int hp_before_poison = poisoned->current_hp;
    rc_world_tick(world);
    assert(poisoned->current_hp == hp_before_poison - 3);

    int immune_slot = rc_npc_spawn(world, 2, 3204, 3202, 0);
    assert(immune_slot >= 0);
    RcNpc *immune = &world->npcs[immune_slot];
    assert(!rc_npc_apply_poison(world, immune, 4));
    assert(immune->poison_damage == 0);
    rc_world_destroy(world);
}

static void test_hunt_policy_checks_visibility_rate_and_strength(void) {
    seed_defs();
    RcNpcDef *def = &g_npc_defs[0];
    def->max_hit = 1;
    def->attack_speed = 4;
    def->attack_types = 0x04;
    def->hunt = (RcNpcHuntPolicy){
        .target = RC_NPC_HUNT_PLAYER,
        .visibility = RC_NPC_HUNT_VIS_LINE_OF_SIGHT,
        .strength = RC_NPC_HUNT_STRENGTH_OUTSIDE_WILDERNESS,
        .flags = RC_NPC_HUNT_KEEP_HUNTING,
        .range = 8,
        .rate = 2,
    };
    RcWorld *world = make_world(RC_SUB_COMBAT);
    world->player.x = 3203;
    world->player.y = 3200;
    for (int i = 0; i < SKILL_COUNT; i++) {
        world->player.skills.base_level[i] = 1;
        world->player.skills.boosted_level[i] = 1;
    }
    int slot = rc_npc_spawn(world, 0, 3200, 3200, 0);
    assert(slot >= 0);
    RcNpc *npc = &world->npcs[slot];
    *flags_at(world, 3201, 3200) = COL_PROJ_BLOCK_FULL;
    rc_world_tick(world);
    rc_world_tick(world);
    assert(npc->target_uid == -1);
    *flags_at(world, 3201, 3200) = 0;
    rc_world_tick(world);
    assert(npc->target_uid == -1);
    rc_world_tick(world);
    assert(npc->target_uid == 0);

    npc->target_uid = -1;
    npc->hunt_timer = 0;
    rc_npc_route_clear(npc, RC_MOVEMENT_NONE);
    for (int i = 0; i < SKILL_COUNT; i++) {
        world->player.skills.base_level[i] = 99;
        world->player.skills.boosted_level[i] = 99;
    }
    rc_world_tick(world);
    rc_world_tick(world);
    assert(rc_combat_level(&world->player.skills) > def->combat_level * 2);
    assert(npc->target_uid == -1);
    rc_world_destroy(world);
}

static void test_hunt_line_of_walk_busy_and_keep_policy(void) {
    seed_defs();
    RcNpcDef *def = &g_npc_defs[0];
    def->max_hit = 1;
    def->attack_speed = 4;
    def->attack_types = 0x04;
    def->hunt = (RcNpcHuntPolicy){
        .target = RC_NPC_HUNT_PLAYER,
        .visibility = RC_NPC_HUNT_VIS_LINE_OF_WALK,
        .flags = RC_NPC_HUNT_CHECK_NOT_BUSY,
        .range = 8,
        .rate = 1,
    };
    RcWorld *world = make_world(RC_SUB_COMBAT);
    world->player.x = 3203;
    world->player.y = 3200;
    int slot = rc_npc_spawn(world, 0, 3200, 3200, 0);
    assert(slot >= 0);
    RcNpc *npc = &world->npcs[slot];

    world->player.interaction.active = true;
    rc_combat_tick_npc(world, npc);
    assert(npc->target_uid == -1);
    world->player.interaction.active = false;

    *flags_at(world, 3201, 3200) = COL_BLOCK_WALK;
    rc_combat_tick_npc(world, npc);
    assert(npc->target_uid == -1);
    *flags_at(world, 3201, 3200) = 0;
    rc_combat_tick_npc(world, npc);
    assert(npc->target_uid == 0);

    npc->target_uid = -1;
    rc_combat_tick_npc(world, npc);
    assert(npc->target_uid == -1);
    assert(npc->combat.aggro_state == 2);
    rc_world_destroy(world);
}

int main(void) {
    test_spawn_identity_policy_and_slot_reuse();
    test_death_hide_respawn_and_full_reset();
    test_transforms_use_world_var_state();
    test_direct_step_directions_and_footprints();
    test_direct_route_and_status_clocks();
    test_hunt_policy_checks_visibility_rate_and_strength();
    test_hunt_line_of_walk_busy_and_keep_policy();
    printf("test_npc_runtime_foundation: NPC foundation contract passed.\n");
    return 0;
}
