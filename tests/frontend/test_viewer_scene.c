// Exercise the viewer's real config, menu assembly and command dispatch without a window.
#define main runec_viewer_main
#include "../../rc-viewer/viewer.c"
#undef main
#include "../../rc-core/drops.h"
#include "../world_test_fixture.h"
#include <assert.h>

static void test_installed_reward_delivery(RcWorld *world) {
    const int ids[] = {3010, 3011, 239, 2642};
    for (unsigned n = 0; n < sizeof(ids) / sizeof(ids[0]); n++) {
        const RcDropTable *table = rc_drop_table_for_npc(ids[n]);
        assert(table && !table->rejection_flags);
        RcGroundGrant grants[RC_MAX_LOOT_DROPS] = {0};
        int count = 0;
        const RcDropEntry *always = rc_drop_entries_for(table, RC_DROP_ALWAYS, &count);
        assert(count == (ids[n] == 3010 || ids[n] == 3011 ? 1 : 2));
        assert(always[0].item_id == (count == 1 ? 526u : 536u));
        if (count == 2) assert(always[1].item_id == 1747);
        for (int i = 0; i < count; i++)
            grants[i] = (RcGroundGrant){always[i].item_id, always[i].qmax, 0};
        int notes = 0;
        for (int kind = RC_DROP_MAIN; kind <= RC_DROP_TERTIARY; kind++) {
            int entries = 0;
            const RcDropEntry *rows = rc_drop_entries_for(table, kind, &entries);
            for (int i = 0; i < entries; i++) {
                const RcDropEntry *row = &rows[i];
                assert(row->item_id != 617 && row->item_id != 1515 && row->item_id != 444);
                assert(row->item_id != 23083 && row->item_id != 33251);
                if (!row->item_id) continue;
                if (row->item_id == 1516 || row->item_id == 445) {
                    assert(rc_item_def_get(row->item_id)->stackable);
                    notes++;
                }
                grants[count] = (RcGroundGrant){row->item_id, row->qmax, 0};
                assert(rc_ground_grant(world, grants, count + 1, 3208, 3208, 0, 0,
                    (RcGroundPolicy){100, 200, 0, 200}) == RC_GROUND_GRANT_OK
                    && "every candidate reward must fit alongside guaranteed drops");
                memset(world->ground_items, 0, sizeof(world->ground_items));
                world->ground_item_count = 0;
            }
        }
        if (ids[n] == 239 || ids[n] == 2642) assert(notes == 2);
    }
}

static void test_viewer_death_drops(RcWorld *world) {
    assert(g_rc_drop_table_count > 0 && "viewer must load NPC drop tables");
    const int ids[] = {3010, 3011, 239};
    for (unsigned i = 0; i < sizeof(ids) / sizeof(ids[0]); i++) {
        RcLootDrop rolls[RC_MAX_LOOT_DROPS];
        int count = rc_roll_npc_loot(world, ids[i], rolls, RC_MAX_LOOT_DROPS);
        if (count < 0) fprintf(stderr, "viewer loot: NPC %d roll failed: %d\n", ids[i], count);
        assert(count > 0 && "installed guard/KBD loot must produce rewards");
        int idx = rc_npc_spawn(world, rc_npc_def_find(ids[i]), 3208, 3208, 0);
        assert(idx >= 0);
        RcNpc *npc = &world->npcs[idx];
        npc->pending_hits[0] = (RcPendingHit){.active = true, .accurate = true,
            .source_idx = RC_HIT_SOURCE_PLAYER, .damage = npc->current_hp,
            .apply_tick = world->tick};
        npc->num_pending_hits = 1;
        rc_world_tick(world);
        assert(npc->is_dead && npc->loot_prepared);
        assert(!world->last_loot.result && world->last_loot.count > 0);
        assert(world->last_loot.npc_def_id == ids[i]);
        for (int t = 0; t < 3; t++) rc_world_tick(world);
        int visible = 0;
        for (int g = 0; g < world->ground_item_count; g++)
            visible += rc_ground_item_visible(&world->ground_items[g], 0);
        assert(visible > 0 && "NPC rewards must arrive after death");
        assert(rc_npc_remove(world, npc->uid));
        memset(world->ground_items, 0, sizeof(world->ground_items));
        world->ground_item_count = 0;
    }
}

static void point_context(ViewerState *v, int x, int y) {
    reset_viewer_context(v);
    v->context_plane = viewer_scene_plane(v);
    v->context_tile_x = x;
    v->context_tile_y = y;
    v->context_ray = (Ray){
        {(float)x - g_world_origin_x + 0.5f, 20, -((float)y - g_world_origin_y + 0.5f)},
        {0, -1, 0}};
    v->ui.context_pos = (Vector2){200, 200};
}

static int menu_row(ViewerState *v, int kind, int option, int id) {
    for (int i = 0; i < v->ui.context_action_count; i++) {
        ViewerContextEntry e = v->context_entries[i];
        if (e.kind == kind && e.option == option
                && (id < 0 || (kind == VIEWER_CONTEXT_NPC ? e.npc_uid : e.ground_index) == id))
            return i;
    }
    return -1;
}

static void choose_row(ViewerState *v, int row) {
    assert(row >= 0);
    v->ui.last_intent = (RuneCUiIntent){.kind = RUNEC_UI_INTENT_CONTEXT_ACTION, .primary = row};
    handle_context_intent(v);
}

static void test_overlapping_menu_and_pickup(RcWorld *world) {
    ViewerState *v = calloc(1, sizeof(*v));
    assert(v);
    v->world = world;
    v->scene_plane_override = -1;
    world->player.x = 3208;
    world->player.y = 3207;
    const int npc_ids[] = {3010, 239};
    int indices[2];
    for (int i = 0; i < 2; i++) {
        indices[i] = rc_npc_spawn(world, rc_npc_def_find(npc_ids[i]), 3208, 3208, 0);
        assert(indices[i] >= 0);
        world->npcs[indices[i]].disable_wander = true;
    }
    const int items[] = {995, 554, 555, 556, 557, 558, 559};
    for (int i = 0; i < 7; i++)
        assert(rc_ground_item_spawn(world, items[i], 10, 3208, 3208, 0, 0));
    assert(rc_ground_item_spawn(world, 560, 1, 3208, 3208, 1, 0));
    assert(rc_ground_item_spawn(world, 561, 1, 3208, 3208, 0, 1));
    RcGroundGrant delayed = {562, 1, 0};
    assert(rc_ground_grant(world, &delayed, 1, 3208, 3208, 0, 0,
        (RcGroundPolicy){100, 200, 10, 200}) == RC_GROUND_GRANT_OK);
    point_context(v, 3208, 3208);
    v->context_object = (ViewerPickedObject){.obj_id = 1276, .x = 3208, .y = 3208};
    assert(rc_object_def_get(1276));
    build_scene_context_page(v, 0);
    for (int i = 0; i < 2; i++)
        assert(menu_row(v, VIEWER_CONTEXT_NPC, VIEWER_CONTEXT_EXAMINE,
            world->npcs[indices[i]].uid) >= 0 && "overlapping NPCs must both have actions");
    assert(menu_row(v, VIEWER_CONTEXT_OBJECT, VIEWER_CONTEXT_EXAMINE, -1) >= 0);
    int takes = 0;
    for (int page = 0; ; page++) {
        assert(v->context_page == page && v->ui.context_action_count <= RUNEC_UI_CONTEXT_ACTIONS);
        assert(v->ui.context_pos.x == 200 && v->ui.context_pos.y == 200);
        for (int row = 0; row < v->ui.context_action_count; row++) {
            ViewerContextEntry e = v->context_entries[row];
            if (e.kind != VIEWER_CONTEXT_GROUND_ITEM || e.option != VIEWER_CONTEXT_TAKE) continue;
            assert(e.ground_index < 7 && "hidden, delayed and other-plane piles must not appear");
            assert(strcmp(v->ui.context_targets[row], rc_item_def_get(items[e.ground_index])->name) == 0);
            takes++;
        }
        int more = menu_row(v, VIEWER_CONTEXT_TILE, VIEWER_CONTEXT_MORE, -1);
        if (more < 0) break;
        choose_row(v, more);
    }
    assert(takes == 7 && "NPC/object hits must not hide any ground-item menu entries");
    choose_row(v, menu_row(v, VIEWER_CONTEXT_TILE, VIEWER_CONTEXT_PREVIOUS, -1));
    assert(v->context_page == 0);
    int take = menu_row(v, VIEWER_CONTEXT_GROUND_ITEM, VIEWER_CONTEXT_TAKE, 0);
    choose_row(v, take);
    rc_world_tick(world);
    assert(world->ground_items[0].active && "adjacent pickup keeps its one-tick reach delay");
    rc_world_tick(world);
    if (rc_inv_find(world->player.inventory, 995) < 0)
        fprintf(stderr, "pickup failed: player=%d,%d tick=%llu item_result=%d interaction_failure=%d active=%d ready=%llu ground=%d/%d uid=%d\n",
            world->player.x, world->player.y, (unsigned long long)world->tick,
            world->player.last_item_action.code, world->player.interaction.last_failure,
            rc_interaction_is_active(&world->player), (unsigned long long)world->player.interaction.ready_tick,
            world->ground_items[0].active, world->ground_items[0].quantity, world->ground_items[0].uid);
    assert(rc_inv_find(world->player.inventory, 995) >= 0);
    assert(!world->ground_items[0].active && "Take must reach core while NPC occupies its tile");

    // Captured identity must not select a replacement pile in the same slot.
    point_context(v, 3208, 3208);
    build_scene_context_page(v, 0);
    take = menu_row(v, VIEWER_CONTEXT_GROUND_ITEM, VIEWER_CONTEXT_TAKE, 1);
    assert(take >= 0);
    world->ground_items[1].version++;
    choose_row(v, take);
    rc_world_tick(world);
    assert(world->ground_items[1].active && rc_inv_find(world->player.inventory, 554) < 0);

    // Every tile of a large NPC footprint, not only its southwest anchor.
    assert(rc_ground_item_spawn(world, 563, 1, 3212, 3212, 0, 0));
    point_context(v, 3212, 3212);
    build_scene_context_page(v, 0);
    assert(menu_row(v, VIEWER_CONTEXT_NPC, VIEWER_CONTEXT_EXAMINE,
        world->npcs[indices[1]].uid) >= 0);
    assert(menu_row(v, VIEWER_CONTEXT_GROUND_ITEM, VIEWER_CONTEXT_TAKE, -1) >= 0);

    // Plane changes dismiss stale scene actions instead of targeting another floor.
    v->scene_plane_override = 1;
    choose_row(v, menu_row(v, VIEWER_CONTEXT_GROUND_ITEM, VIEWER_CONTEXT_TAKE, -1));
    assert(v->context_plane == -1 && world->player_commands.count == 0);
    v->scene_plane_override = -1;
    point_context(v, 3220, 3220);
    build_scene_context_page(v, 0);
    assert(v->ui.context_action_count == 2);
    choose_row(v, menu_row(v, VIEWER_CONTEXT_TILE, VIEWER_CONTEXT_CANCEL, -1));
    assert(v->context_plane == -1 && world->player_commands.count == 0);

    point_context(v, 3208, 3208);
    build_scene_context_page(v, 0);
    int uid = world->npcs[indices[0]].uid;
    int attack = -1;
    const RcNpcDef *guard = rc_npc_def_get(rc_npc_def_find(3010));
    for (int i = 0; i < RC_NPC_OPTION_COUNT; i++)
        if (rc_npc_def_option_is_attack(guard, i)) attack = i;
    assert(attack >= 0);
    choose_row(v, menu_row(v, VIEWER_CONTEXT_NPC, attack, uid));
    assert(world->player_commands.count == 1);
    const RcPlayerCommand *command = &world->player_command_storage[0];
    assert(command->kind == RC_PLAYER_COMMAND_INTERACT_NPC
        && command->args[0] == uid && command->args[1] == attack
        && "mixed menus must dispatch the selected NPC operation through core");

    for (int i = 0; i < 2; i++) assert(rc_npc_remove(world, world->npcs[indices[i]].uid));
    free(v);
}

static void test_scene_examine_object_and_walk(RcWorld *world) {
    rc_world_tick(world);
    ViewerState *v = calloc(1, sizeof(*v));
    assert(v);
    v->world = world;
    v->scene_plane_override = -1;
    point_context(v, 3216, 3216);
    build_scene_context_page(v, -1);
    assert(!v->ui.context_open);
    int booth_id = -1;
    for (int id = 0; id < RC_MAX_OBJECT_ID; id++) {
        const RcObjectDef *def = rc_object_def_get(id);
        const RcObjectBehavior *behavior = rc_object_behavior_get(id);
        if (def && !def->transform_count && behavior
                && (behavior->flags & RC_OBJ_BEHAVIOR_BANK) && behavior->action_mask) {
            booth_id = id;
            break;
        }
    }
    assert(booth_id >= 0 && "fixture requires an installed non-morph bank object");
    RcObjectPlacement booth = {.obj_id = booth_id, .x = 3216, .y = 3216, .type = 10};
    uint64_t key = 0;
    assert(rc_world_object_add(world, &booth, 0, &key) == RC_OBJECT_MUTATION_OK);
    for (int op = 0; op < 2; op++) {
        point_context(v, 3216, 3216);
        v->context_object = (ViewerPickedObject){.obj_id = booth_id, .x = 3216,
            .y = 3216, .placement_key = key};
        build_scene_context_page(v, 0);
        int option = op == 0 ? VIEWER_CONTEXT_EXAMINE : object_first_action_option(v, v->context_object);
        assert(op == 0 || option >= 0);
        choose_row(v, menu_row(v, VIEWER_CONTEXT_OBJECT, option, -1));
        assert(world->player_commands.count == op + 1);
        assert(world->player_command_storage[op].kind == (op == 0
            ? RC_PLAYER_COMMAND_EXAMINE_OBJECT : RC_PLAYER_COMMAND_INTERACT_OBJECT));
        assert(world->player_command_storage[op].key == key);
    }
    point_context(v, 3208, 3208);
    build_scene_context_page(v, 0);
    choose_row(v, menu_row(v, VIEWER_CONTEXT_GROUND_ITEM, VIEWER_CONTEXT_EXAMINE, 1));
    assert(world->player_command_storage[2].kind == RC_PLAYER_COMMAND_EXAMINE_GROUND_ITEM);
    point_context(v, 3208, 3208);
    build_scene_context_page(v, 0);
    int row = menu_row(v, VIEWER_CONTEXT_GROUND_ITEM, VIEWER_CONTEXT_EXAMINE, 1);
    world->ground_items[1].version++;
    choose_row(v, row);
    assert(world->player_commands.count == 3 && "stale Examine must not target a replacement");
    int idx = rc_npc_spawn(world, rc_npc_def_find(3010), 3220, 3220, 0);
    assert(idx >= 0);
    point_context(v, 3220, 3220);
    build_scene_context_page(v, 0);
    choose_row(v, menu_row(v, VIEWER_CONTEXT_NPC, VIEWER_CONTEXT_EXAMINE, world->npcs[idx].uid));
    assert(world->player_command_storage[3].kind == RC_PLAYER_COMMAND_EXAMINE_NPC);
    world->npcs[idx].is_dead = true;
    point_context(v, 3220, 3220);
    build_scene_context_page(v, 0);
    assert(menu_row(v, VIEWER_CONTEXT_NPC, VIEWER_CONTEXT_EXAMINE, -1) < 0);
    choose_row(v, menu_row(v, VIEWER_CONTEXT_TILE, VIEWER_CONTEXT_WALK_HERE, -1));
    assert(world->player_commands.count == 5);
    assert(world->player_command_storage[4].kind == (world->player.running
        ? RC_PLAYER_COMMAND_RUN_TO : RC_PLAYER_COMMAND_WALK_TO));
    assert(world->player_command_storage[4].args[0] == 3220);
    assert(rc_world_object_revert(world, key) == RC_OBJECT_MUTATION_OK);
    free(v);
}

static void test_boss_landing_heights(RcWorld *world) {
    ViewerState *v = calloc(1, sizeof(*v));
    assert(v);
    v->world = world;
    v->mapsquare_streaming_active = 1;
    int count = 0;
    const RuneCDevTransport *destinations = runec_dev_validation_transports(&count);
    for (int i = 0; i < count; i++) {
        const RuneCDevTransport *d = &destinations[i];
        int x, y;
        assert(viewer_dev_player_tile(v, d, &x, &y));
        assert(!rc_tile_blocked(&world->map, x, y, d->plane));
        assert(rc_can_move(&world->map, x, y, 1, 0, d->plane)
            || rc_can_move(&world->map, x, y, -1, 0, d->plane)
            || rc_can_move(&world->map, x, y, 0, 1, d->plane)
            || rc_can_move(&world->map, x, y, 0, -1, d->plane));
        if (d->npc_id >= 0) {
            RcRouteTarget target = rc_route_target_rectangle(d->target_x, d->target_y,
                d->npc_size, d->npc_size, 1, 1, false, false);
            RcRoute route = rc_find_route(&world->map, x, y, 1, 1, d->plane, &target, false);
            assert(route.status == RC_ROUTE_EXACT || route.status == RC_ROUTE_ALREADY_ARRIVED);
        }
        char path[128];
        snprintf(path, sizeof(path), "data/regions/%d_%d.p%d.terrain", x / 64, y / 64, d->plane);
        TerrainMesh *terrain = terrain_load_cpu(path);
        assert(terrain);
        assert(viewer_world_transform_set(&g_world_transform, x / 64 * 64 - 64, y / 64 * 64 - 64));
        terrain_offset(terrain, g_world_origin_x, g_world_origin_y);
        terrain->loaded = 1;
        v->mapsquare_chunk_count = 1;
        v->mapsquare_chunks[0] = (ViewerMapsquareChunk){.region_x = x / 64,
            .region_y = y / 64, .plane = d->plane, .terrain = terrain};
        assert(fabsf(ground_yf_plane(v, d->plane, (float)x, (float)y)
            - terrain_height_avg(terrain, LOCAL_X(x), LOCAL_Y(y)) - 0.05f) < 0.0001f);
        terrain->loaded = 0;
        terrain_free(terrain);
    }
    // An entirely blocked destination must not fall back to the desired coordinate.
    for (int r = 0; r < world->map.region_count; r++) {
        for (int x = 0; x < RC_REGION_SIZE; x++) {
            for (int y = 0; y < RC_REGION_SIZE; y++)
                world->map.regions[r].tiles[0][x][y].collision_flags = COL_BLOCK_WALK;
        }
    }
    int x = -123, y = -456;
    assert(!viewer_dev_player_tile(v, &destinations[count - 1], &x, &y));
    assert(x == -123 && y == -456);
    const RuneCDevTransport *last = &destinations[count - 1];
    int pocket_x = last->target_x;
    int pocket_y = last->target_y - last->npc_size - 4;
    for (int r = 0; r < world->map.region_count; r++) {
        RcRegion *region = &world->map.regions[r];
        if (region->region_x != pocket_x / 64 || region->region_y != pocket_y / 64)
            continue;
        region->tiles[0][pocket_x % 64][pocket_y % 64].collision_flags = 0;
        region->tiles[0][(pocket_x + 1) % 64][pocket_y % 64].collision_flags = 0;
    }
    assert(!viewer_dev_player_tile(v, last, &x, &y)
        && "an isolated two-tile pocket is not a reachable boss landing");
    RuneCDevTransport invalid = {.target_x = -1, .target_y = -1};
    assert(!viewer_dev_player_tile(v, &invalid, &x, &y));
    free(v);
}

// A large NPC's projectile leaves the same body point whatever the angle:
// it is locked to the NPC's centre and starts `progress` toward the target
// at the row's start height (not at the footprint tile nearest the target).
static void test_boss_projectile_origins(RcWorld *world) {
    assert(rc_load_combat_visuals("data/defs/combat_visuals.tsv") > 0);
    const RcCombatVisualDef *row = rc_combat_visual_for_npc_action(8061, "Dragonfire");
    assert(row && row->projectile_progress > 64 && row->projectile_start_height > 0);
    ViewerState *v = calloc(1, sizeof(*v));
    assert(v);
    v->world = world;
    int bx = 3216, by = 3216;
    assert(viewer_world_transform_set(&g_world_transform, bx - 64, by - 64));
    int idx = rc_npc_spawn(world, rc_npc_def_find(8061), bx, by, 0);
    assert(idx >= 0);
    const RcNpc *vork = &world->npcs[idx];
    float cx = (float)bx + 3.5f, cy = (float)by + 3.5f;   // size 7
    // Straight in front (south) and on the diagonal past a wing corner.
    const int spots[2][2] = {{bx + 3, by - 6}, {bx + 10, by + 10}};
    float reach[2];
    for (int s = 0; s < 2; s++) {
        world->player.x = spots[s][0];
        world->player.y = spots[s][1];
        world->player.plane = 0;
        v->prev_player_x = (float)world->player.x;
        v->prev_player_y = (float)world->player.y;
        v->combat_projectile_count = 0;
        RcCombatAttackEvent event = {
            .active = true, .source_kind = RC_COMBAT_ACTOR_NPC,
            .target_kind = RC_COMBAT_ACTOR_PLAYER, .style = COMBAT_MAGIC,
            .action_kind = RC_COMBAT_ACTION_ENCOUNTER,
            .source_uid = vork->uid, .target_uid = 0,
            .source_definition_id = 8061, .target_definition_id = -1,
            .source_x = bx, .source_y = by, .target_x = world->player.x,
            .target_y = world->player.y, .hit_delay = 3,
            .world_tick = (uint32_t)world->tick,
        };
        snprintf(event.action_key_name, sizeof(event.action_key_name), "Dragonfire");
        viewer_handle_npc_attack_event(v, &event);
        assert(v->combat_projectile_count == 1);
        ViewerCombatProjectile *proj = &v->combat_projectiles[0];
        assert(fabsf(proj->source_fx - cx) < 0.001f && fabsf(proj->source_fy - cy) < 0.001f
               && "locked to the NPC's centre");
        // First visible frame: the start delay into the flight.
        proj->age_ticks = 0;
        v->tick_frac = (float)row->projectile_delay / 30.0f;
        float angle, pitch;
        int visible = 0;
        Vector3 at = combat_projectile_position(v, proj, 0, &angle, &pitch, &visible);
        assert(visible);
        float wx = at.x + (float)g_world_origin_x, wy = -at.z + (float)g_world_origin_y;
        float dx = (float)world->player.x + 0.5f - cx, dy = (float)world->player.y + 0.5f - cy;
        float along = ((wx - cx) * dx + (wy - cy) * dy) / sqrtf(dx * dx + dy * dy);
        float off = fabsf((wx - cx) * dy - (wy - cy) * dx) / sqrtf(dx * dx + dy * dy);
        reach[s] = along;
        assert(off < 0.01f && "on the line from the centre to the target");
        assert(fabsf(along - (float)row->projectile_progress / 128.0f) < 0.01f);
        assert(fabsf(at.y - (float)row->projectile_start_height / 128.0f) < 0.06f);
    }
    assert(fabsf(reach[0] - reach[1]) < 0.01f && "same mouth point from any angle");
    rc_npc_remove(world, vork->uid);
    free(v);
}

// Where the player is drawn this frame (scene coordinates).
static Vector3 presented_player(ViewerState *v) {
    const RcPlayer *p = &v->world->player;
    float wx = v->prev_player_x + ((float)p->x - v->prev_player_x) * v->tick_frac;
    float wy = v->prev_player_y + ((float)p->y - v->prev_player_y) * v->tick_frac;
    return (Vector3){wx - (float)g_world_origin_x + 0.5f, ground_yf_plane(v, 0, wx, wy),
                     -(wy - (float)g_world_origin_y + 0.5f)};
}

// Flies a projectile ten frames a tick while the player runs two tiles east
// on each of its first two ticks, and returns it at end_time + 1 (the last
// drawn moment). *kink is the largest frame-to-frame change of its step over
// the mean step: small for a smooth curve, large for a jump.
static Vector3 fly_while_player_runs(ViewerState *v, ViewerCombatProjectile *proj,
                                     float *kink) {
    RcPlayer *p = &v->world->player;
    float land = (float)proj->projectile_end_time + 1.0f;
    float angle, pitch, path = 0.0f, worst = 0.0f;
    int visible = 0, seen = 0, steps = 0, tick = -1;
    Vector3 at = {0}, last = {0}, last_step = {0};
    for (float time = 0.0f;; time = fminf(time + 3.0f, land)) {
        while (tick < (int)(time / 30.0f)) {
            tick++;
            v->prev_player_x = (float)p->x;
            v->prev_player_y = (float)p->y;
            if (tick == 1 || tick == 2) p->x += 2;
        }
        proj->age_ticks = tick;
        v->tick_frac = (time - 30.0f * (float)tick) / 30.0f;
        at = combat_projectile_position(v, proj, 0, &angle, &pitch, &visible);
        if (time >= land) break;
        if (!visible) continue;
        if (seen) {
            Vector3 step = {at.x - last.x, 0.0f, at.z - last.z};
            path += hypotf(step.x, step.z);
            if (steps++)
                worst = fmaxf(worst, hypotf(step.x - last_step.x, step.z - last_step.z));
            last_step = step;
        }
        seen = 1;
        last = at;
    }
    assert(visible && steps > 2 && "drawn up to end_time + 1");
    *kink = worst / (path / (float)steps);
    return at;
}

// Fires one of Vorkath's encounter attacks at the player's tile (or, for a
// tile-aimed one, at that tile) and returns its projectile.
static ViewerCombatProjectile *launch_at_player(ViewerState *v, const RcNpc *npc,
                                                const char *action, bool on_tile) {
    const RcPlayer *p = &v->world->player;
    v->prev_player_x = (float)p->x;
    v->prev_player_y = (float)p->y;
    v->combat_projectile_count = 0;
    RcCombatAttackEvent event = {
        .active = true, .source_kind = RC_COMBAT_ACTOR_NPC,
        .target_kind = on_tile ? RC_COMBAT_ACTOR_NONE : RC_COMBAT_ACTOR_PLAYER,
        .style = COMBAT_MAGIC, .action_kind = RC_COMBAT_ACTION_ENCOUNTER,
        .source_uid = npc->uid, .target_uid = on_tile ? -1 : 0,
        .source_definition_id = 8061, .target_definition_id = -1,
        .source_x = npc->x, .source_y = npc->y, .target_x = p->x, .target_y = p->y,
        .hit_delay = on_tile ? 4 : 3, .world_tick = (uint32_t)v->world->tick,
    };
    snprintf(event.action_key_name, sizeof(event.action_key_name), "%s", action);
    viewer_handle_npc_attack_event(v, &event);
    assert(v->combat_projectile_count == 1);
    return &v->combat_projectiles[0];
}

static Vector3 projectile_at(ViewerState *v, ViewerCombatProjectile *proj, float time) {
    proj->age_ticks = (int)(time / 30.0f);
    v->tick_frac = (time - 30.0f * (float)proj->age_ticks) / 30.0f;
    float angle, pitch;
    int visible = 0;
    Vector3 at = combat_projectile_position(v, proj, 0, &angle, &pitch, &visible);
    assert(visible);
    return at;
}

// A projectile aimed at the player ends on the player's current position when
// they move mid-flight, steering smoothly instead of jumping; one aimed at a
// tile still lands on that tile; Jad's falling rock lands on the player.
static void test_projectiles_follow_moving_player(RcWorld *world) {
    ViewerState *v = calloc(1, sizeof(*v));
    assert(v);
    v->world = world;
    int bx = 3216, by = 3216;
    assert(viewer_world_transform_set(&g_world_transform, bx - 64, by - 64));
    int idx = rc_npc_spawn(world, rc_npc_def_find(8061), bx, by, 0);
    assert(idx >= 0);
    const RcNpc *vork = &world->npcs[idx];
    int vork_uid = vork->uid;
    const char *actions[2] = {"Dragonfire", "Deadly dragonfire"};
    for (int a = 0; a < 2; a++) {
        bool on_tile = a == 1;
        world->player.x = bx + 3;
        world->player.y = by - 8;
        world->player.plane = 0;
        int aim_x = world->player.x, aim_y = world->player.y;
        ViewerCombatProjectile *proj = launch_at_player(v, vork, actions[a], on_tile);
        float kink = 0.0f;
        Vector3 at = fly_while_player_runs(v, proj, &kink);
        assert(world->player.x == aim_x + 4 && "the player moved during the flight");
        Vector3 aim = {(float)(aim_x - g_world_origin_x) + 0.5f, 0.0f,
                       -((float)(aim_y - g_world_origin_y) + 0.5f)};
        Vector3 goal = on_tile ? aim : presented_player(v);
        assert(hypotf(at.x - goal.x, at.z - goal.z) < 0.01f
               && (on_tile ? "a tile-aimed projectile lands on its tile"
                           : "a projectile aimed at the player ends on the player"));
        if (!on_tile) {
            assert(hypotf(at.x - aim.x, at.z - aim.z) > 2.0f && "not on the tile it was aimed at");
            assert(fabsf(at.y - (goal.y + (float)proj->projectile_end_height / 128.0f)) < 0.01f);
        }
        assert(kink < 0.5f && "steers smoothly, no jumps");
    }

    // Displaced with nothing to interpolate (a teleport), the player is
    // steered to over the remaining flight, not jumped to.
    world->player.x = bx + 3;
    world->player.y = by - 8;
    ViewerCombatProjectile *proj = launch_at_player(v, vork, "Dragonfire", false);
    assert(proj->projectile_start_time < 54 && proj->projectile_end_time > 66);
    Vector3 before = projectile_at(v, proj, 54.0f);
    Vector3 now = projectile_at(v, proj, 60.0f);
    world->player.x += 3;
    v->prev_player_x = (float)world->player.x;
    Vector3 after = projectile_at(v, proj, 66.0f);
    assert(hypotf(after.x - now.x, after.z - now.z)
           < hypotf(now.x - before.x, now.z - before.z) + 0.6f && "no jump");
    Vector3 landed = projectile_at(v, proj, (float)proj->projectile_end_time + 1.0f);
    Vector3 goal = presented_player(v);
    assert(hypotf(landed.x - goal.x, landed.z - goal.z) < 0.01f);

    // A scene re-centred mid-flight (streaming) leaves it where it was.
    world->player.x = bx + 3;
    proj = launch_at_player(v, vork, "Dragonfire", false);
    before = projectile_at(v, proj, 54.0f);
    float before_x = before.x + (float)g_world_origin_x;
    float before_y = (float)g_world_origin_y - before.z;
    assert(viewer_world_transform_set(&g_world_transform, bx - 128, by - 64));
    now = projectile_at(v, proj, 54.0f);
    assert(fabsf(now.x + (float)g_world_origin_x - before_x) < 0.001f
           && fabsf((float)g_world_origin_y - now.z - before_y) < 0.001f);
    assert(viewer_world_transform_set(&g_world_transform, bx - 64, by - 64));
    rc_npc_remove(world, vork_uid);

    // Jad's rock falls on the player wherever they have moved to.
    idx = rc_npc_spawn(world, rc_npc_def_find(3127), bx, by, 0);
    assert(idx >= 0);
    int jad_uid = world->npcs[idx].uid;
    world->player.x = bx + 2;
    world->player.y = by - 6;
    v->prev_player_x = (float)world->player.x;
    v->prev_player_y = (float)world->player.y;
    v->tick_frac = 0.0f;
    v->combat_projectile_count = 0;
    RcCombatAttackEvent rock = {
        .active = true, .source_kind = RC_COMBAT_ACTOR_NPC,
        .target_kind = RC_COMBAT_ACTOR_PLAYER, .style = COMBAT_RANGED,
        .action_kind = RC_COMBAT_ACTION_NPC, .source_uid = jad_uid, .target_uid = 0,
        .source_definition_id = 3127, .target_definition_id = -1,
        .source_x = bx, .source_y = by, .target_x = world->player.x,
        .target_y = world->player.y, .hit_delay = 3, .world_tick = (uint32_t)world->tick,
    };
    viewer_handle_npc_attack_event(v, &rock);
    assert(v->combat_projectile_count == 1);
    proj = &v->combat_projectiles[0];
    assert(proj->impact_spotanim_id == 451 && !projectile_has_travel_visual(proj)
           && proj->launch_spotanim_id < 0);
    world->player.x += 3;
    world->player.y -= 1;
    v->prev_player_x = (float)world->player.x;
    v->prev_player_y = (float)world->player.y;
    Vector3 hit;
    assert(combat_projectile_impact_position(v, proj, 0, &hit));
    goal = presented_player(v);
    assert(hypotf(hit.x - goal.x, hit.z - goal.z) < 0.01f && "the rock lands on the player");
    rc_npc_remove(world, jad_uid);
    free(v);
}

static void test_ground_item_appearances(void) {
    ViewerState *v = calloc(1, sizeof(*v));
    assert(v);
    load_item_stack_variants(v, "data/sprites/items/item_stack_variants.tsv");
    const int ids[] = {526, 536, 1747, 445, 1516, 554, 561, 995, 4151, 11802, 11212};
    for (unsigned i = 0; i < sizeof(ids) / sizeof(ids[0]); i++) {
        int id = item_display_id_for_quantity(v, ids[i], 10000);
        ModelSet *models = ground_item_models(v, id);
        assert(models && models->count == 1);
        ModelEntry *entry = model_find(models, (uint32_t)id);
        assert(entry && entry->loaded && entry->cpu_mesh.vertexCount > 0);
        assert(!entry->uploaded && !models->owns_atlas_texture);
        assert(ground_item_models(v, id) == models && "repeat draws must not reload models");
    }
    assert(item_display_id_for_quantity(v, 995, 10000) == 1004);
    assert(!ground_item_models(v, 999999));
    assert(!ground_item_models(v, 999999) && "missing assets must not retry every frame");
    for (int i = 0; i < RC_MAX_GROUND_ITEMS; i++) {
        v->ground_models[i].item_id = 90000 + i;
        v->ground_models[i].last_use = 100 + i;
    }
    v->ground_model_clock = 1000;
    assert(ground_item_models(v, 526));
    assert(v->ground_models[0].item_id == 526 && v->ground_models[0].last_use == 1001);
    for (int i = 0; i < RC_MAX_GROUND_ITEMS; i++)
        models_free(v->ground_models[i].models);
    free(v);
}

static void test_bank_projection(RcWorld *world) {
    ViewerState *v = calloc(1,sizeof(*v));
    assert(v);
    v->world = world;
    world->player.storage_kind = RC_STORAGE_DEPOSIT_BOX;
    world->player.storage_session = 42;
    v->ui.bank_amount_source = RUNEC_UI_CONTEXT_BANK;
    v->ui.context_source_kind = RUNEC_UI_CONTEXT_BANK;
    v->ui.context_open = 1;
    sync_ui_items(v);
    assert(v->ui.bank_open && v->ui.bank_kind == RC_STORAGE_DEPOSIT_BOX);
    assert(v->ui.bank_session == 42 && !v->ui.bank_amount_source && !v->ui.context_open);
    world->player.bank[0] = (RcInvSlot){.item_id=995,.quantity=5,.generation=7};
    world->player.bank_revision = 99;
    sync_ui_items(v);
    assert(v->ui.bank[0].generation == 7);
    world->player.bank_result = (RcBankResult){.sequence=1,.code=RC_BANK_CAPACITY};
    sync_ui_player_status(v);
    assert(v->ui.chat_line_count == 1 && strstr(v->ui.chat_lines[0],"not enough space"));
    sync_ui_player_status(v);
    assert(v->ui.chat_line_count == 1);
    rc_storage_close(world);
    sync_ui_items(v);
    assert(!v->ui.bank_open && !v->ui.bank_session);
    world->player.bank[0] = (RcInvSlot){.item_id=-1};
    free(v);
}

static void test_consumable_projection(RcWorld *world) {
    assert(world->enabled & RC_SUB_CONSUMABLES);
    rc_content_consumables_register(world);
    assert(world->consumable_count > 100);
    ViewerState *v = calloc(1,sizeof(*v));
    assert(v);
    v->world = world;
    world->player.skills.base_level[SKILL_HITPOINTS] = 99;
    world->player.max_hp = 990;
    world->player.current_hp = 300;
    assert(rc_player_inventory_add(world,385,1,0).code == RC_ITEM_RESULT_OK);
    int slot = rc_inv_find(world->player.inventory,385);
    RuneCUiSlot item;
    sync_ui_slot(v,&item,&world->player.inventory[slot],UI_ITEM_CONTAINER_INVENTORY);
    assert(!strcmp(item.actions[0],"Eat") && item.action_ops[0] == 0);
    assert(rc_player_interact_inventory_item(world,slot,item.action_ops[0]));
    assert(!world->player.consume_sequence);
    rc_world_tick(world);
    viewer_sync_consumption(v);
    assert(world->player.consume_result == RC_CONSUME_OK && v->player_action_anim_id == 829);
    assert(rc_player_inventory_add(world,2434,1,0).code == RC_ITEM_RESULT_OK);
    slot = rc_inv_find(world->player.inventory,2434);
    sync_ui_slot(v,&item,&world->player.inventory[slot],UI_ITEM_CONTAINER_INVENTORY);
    assert(!strcmp(item.actions[0],"Drink"));
    assert(rc_player_interact_inventory_item(world,slot,item.action_ops[0]));
    rc_world_tick(world);
    viewer_sync_consumption(v);
    assert(v->player_action_anim_id == 830 && world->player.inventory[slot].item_id == 139);
    assert(rc_player_inventory_add(world,10925,1,0).code == RC_ITEM_RESULT_OK);
    slot = rc_inv_find(world->player.inventory,10925);
    assert(!rc_player_drink(world,slot));
    viewer_sync_consumption(v);
    assert(v->consume_outcome_seen == world->player.consume_outcome_sequence);
    assert(v->ui.chat_line_count == 1 && strstr(v->ui.chat_lines[0],"not implemented"));
    assert(runec_hitsplat_catalog_load(&v->hitsplat_catalog, "data/defs/hitsplats.bin") > 0);
    assert(rc_player_inventory_add(world,23685,1,0).code == RC_ITEM_RESULT_OK);
    slot = rc_inv_find(world->player.inventory,23685);
    world->tick = world->player.potion_ready_tick;
    world->player.current_hp = 500;
    assert(rc_player_drink(world,slot));
    rc_world_tick(world);
    viewer_sync_consumption(v);
    viewer_update_combat_overlays(v,0);
    assert(v->player_action_anim_id == 830 && world->player.current_hp == 400);
    RuneCHitsplatSlot *splat = &v->player_overlay.hitsplats.slots[0];
    assert(splat->active && splat->amount == 10 && splat->definition_id == 28);
    assert(runec_health_bar_visible(&v->player_overlay.health_bar));
    assert(v->player_overlay.health_bar.current_hp == 400);
    uint64_t sequence = splat->sequence;
    viewer_update_combat_overlays(v,0);
    assert(splat->sequence == sequence && !v->player_overlay.hitsplats.slots[1].active);
    world->tick = world->player.food_ready_tick;
    assert(rc_player_inventory_add(world,385,1,0).code == RC_ITEM_RESULT_OK);
    assert(rc_player_eat(world,rc_inv_find(world->player.inventory,385)));
    rc_world_tick(world);
    viewer_update_combat_overlays(v,0);
    assert(v->player_overlay.health_bar.current_hp == world->player.current_hp);
    assert(v->player_overlay.hitsplats.last_sequence == sequence);
    AnimCache *anims = anim_cache_load("data/anims/all.anims");
    assert(anims && anim_get_sequence(anims,829) && anim_get_sequence(anims,830));
    anim_cache_free(anims);
    free(v);
}

int main(void) {
    RcWorldConfig cfg = viewer_world_config(rc_world_streaming_config_default(),
        "data/defs/combat_visuals.tsv");
    assert(cfg.drops_path && cfg.rdt_path && cfg.gdt_path && cfg.mrdt_path);
    RcWorld *world = rc_world_create_config(&cfg);
    assert(world && "the actual viewer config must initialize without a window");
    world->player.x = world->player.y = 3208;
    rc_test_open_mapsquare(world, 3208, 3208, 0);
    test_bank_projection(world);
    test_consumable_projection(world);
    assert(runec_dev_validation_set_god_mode(world, true));
    test_installed_reward_delivery(world);
    test_viewer_death_drops(world);
    test_overlapping_menu_and_pickup(world);
    test_scene_examine_object_and_walk(world);
    test_boss_landing_heights(world);
    test_boss_projectile_origins(world);
    test_projectiles_follow_moving_player(world);
    test_ground_item_appearances();
    rc_world_destroy(world);
    puts("viewer scene: installed loot, target selection and dispatch passed");
    return 0;
}
