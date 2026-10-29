// test_cox_raid — Chambers of Xeric controller on installed data: entering
// through the steps with no party (the entrance's raid choice, a core
// chatbox prompt), generated layouts (normal pools and the fixed Challenge
// Mode order), starting at the lobby passage, crossing passages, the room
// chain's forward and backward edge moves, floor holes and ropes, death and
// respawn, Captain Rimor, reloading, and leaving with raid-only items
// removed.

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../rc-content/content.h"
#include "../rc-content/raids/cox.h"
#include "../rc-core/api.h"
#include "../rc-core/combat.h"
#include "../rc-core/combat_hit.h"
#include "../rc-core/dialogue.h"
#include "../rc-core/interaction.h"
#include "../rc-core/items.h"
#include "../rc-core/varbits.h"

static RcWorld *create_world(uint32_t seed) {
    RcWorldConfig cfg = rc_preset_full_game();
    cfg.seed = seed;
    RcWorld *w = rc_world_create_config(&cfg);
    assert(w);
    assert(rc_world_install_content(w, rc_content_register_all));
    RcPlayer *p = &w->player;
    for (int s = 0; s < SKILL_COUNT; s++) {
        p->skills.base_level[s] = p->skills.boosted_level[s] = 99;
    }
    p->max_hp = p->current_hp = 990;
    return w;
}

// True when a kept chatbox message contains `text`.
static bool has_message(const RcWorld *w, const char *text) {
    const RcGameMessage *m;
    for (uint64_t seq = 0; (m = rc_player_next_game_message(&w->player, seq));
         seq = m->sequence)
        if (strstr(m->text, text)) return true;
    return false;
}

static void tick(RcWorld *w, int n) {
    for (int i = 0; i < n; i++) rc_world_tick(w);
}

static void choose(RcWorld *w, int option) {
    assert(w->choice.active);
    assert(rc_player_widget_action(w, RC_CHOICE_WIDGET, option, 0));
    tick(w, 2);
}

// Interacts and ticks until the interaction ends (at most `ticks` + 40).
static void interact_until(RcWorld *w, int obj_id, int x, int y, int plane,
                           int option, int ticks) {
    assert(rc_player_interact_object_at(w, obj_id, x, y, plane, option));
    tick(w, 1);
    for (int i = 0; i < ticks + 40 && rc_interaction_is_active(&w->player); i++)
        tick(w, 1);
    tick(w, ticks > 2 ? 2 : ticks);
}

// The entrance asks which raid to start: 0-2 normal (small, large, full
// layout), 3 Challenge Mode, 4 Cancel.
static void open_entry(RcWorld *w) {
    assert(rc_world_relocate_player(w, 1233, 3572, 0));
    interact_until(w, 29777, 1232, 3573, 0, 0, 6);
    assert(w->choice.active && strcmp(w->choice.title, "Chambers of Xeric") == 0);
    assert(w->choice.option_count == 5);
}

static void enter_chambers(RcWorld *w, int option) {
    open_entry(w);
    choose(w, option);
    CoxRaid *raid = cox_raid(w);
    assert(raid->in_raid);
    assert(raid->current_room == 0);
    assert(cox_player_in_room(w, &raid->rooms[0]));
}

static void check_layout(const CoxRaid *raid, int floors) {
    assert(raid->floor_count == floors);
    assert(raid->rooms[raid->olm_room].type == COX_ROOM_OLM);
    assert(raid->olm_room == raid->room_count - 1);
    int combat_seen[COX_ROOM_TYPE_COUNT] = {0};
    for (int f = 0; f < floors; f++) {
        const CoxRoom *start = &raid->rooms[raid->floor_start[f]];
        const CoxRoom *end = &raid->rooms[raid->floor_end[f]];
        assert(start->type == COX_ROOM_START && start->floor == f);
        assert(end->type == COX_ROOM_END && end->floor == f);
        bool scav = false, farm = false;
        for (int i = raid->floor_start[f] + 1; i < raid->floor_end[f]; i++) {
            const CoxRoom *room = &raid->rooms[i];
            assert(room->floor == f && room->type > COX_ROOM_END
                   && room->type < COX_ROOM_OLM);
            assert(room->variant <= COX_EXIT_EAST);
            assert(room->x == 3264 + 32 * room->variant);
            scav |= room->type == COX_ROOM_SCAVENGERS
                 || room->type == COX_ROOM_SCAVENGERS_LARGE;
            farm |= room->type == COX_ROOM_FARMING_FISH
                 || room->type == COX_ROOM_FARMING_BATS;
            if (room->type >= COX_ROOM_TEKTON && room->type <= COX_ROOM_MYSTICS)
                assert(combat_seen[room->type]++ == 0 && "combat rooms never repeat");
        }
        assert(scav && farm);
    }
    // No template block serves two rooms.
    for (int i = 0; i < raid->room_count; i++)
        for (int j = i + 1; j < raid->room_count; j++)
            assert(raid->rooms[i].x != raid->rooms[j].x
                   || raid->rooms[i].y != raid->rooms[j].y
                   || raid->rooms[i].plane != raid->rooms[j].plane);
}

// The walkable tile on a room's exit edge nearest its middle.
static void exit_edge_tile(const RcWorld *w, const CoxRoom *room, int *x, int *y) {
    for (int k = 0; k < 32; k++) {
        int a = 15 + (k % 2 ? (k + 1) / 2 : -(k / 2));
        int lx = room->variant == COX_EXIT_WEST ? 0
               : room->variant == COX_EXIT_EAST ? 31 : a;
        int ly = room->variant == COX_EXIT_NORTH ? 31 : a;
        if (cox_tile_walkable(w, room->x + lx, room->y + ly, room->plane)) {
            *x = room->x + lx;
            *y = room->y + ly;
            return;
        }
    }
    assert(!"room has no exit edge tile");
}

static void test_enter_and_chain(void) {
    RcWorld *w = create_world(77);
    CoxRaid *raid = cox_raid(w);
    assert(raid && !raid->in_raid);
    // No party is needed: the steps ask which raid to start, and Cancel
    // stays outside.
    open_entry(w);
    choose(w, 4);
    assert(!raid->in_raid && !w->choice.active);

    enter_chambers(w, 0);
    assert(raid->map_pool == COX_POOL_SMALL && !raid->cm);
    check_layout(raid, 2);
    assert(!raid->started && rc_varbit_get(w, 5432) == 1);
    assert(rc_varbit_get(w, 5431) == 0 && rc_varbit_get(w, 6386) == 0);

    // The lobby passage asks to start the raid.
    CoxRoom *lobby = &raid->rooms[0];
    int px[1], py[1], prot[1];
    assert(cox_find_loc(w, lobby, 29789, px, py, prot, 1) == 1);
    interact_until(w, 29789, px[0], py[0], 0, 0, 12);
    if (!w->choice.active)
        fprintf(stderr, "passage %d,%d r%d: player %d,%d outcome '%s' supported=%d\n",
                px[0], py[0], prot[0], w->player.x, w->player.y,
                rc_interaction_last_outcome(&w->player)->message,
                rc_world_object_option_supported(w, 29789, px[0], py[0], 0, 0, 0));
    assert(w->choice.active);
    choose(w, 0);
    assert(raid->started);
    assert(has_message(w, "The raid has begun!"));

    // Crossing the passage moves the player 4 tiles out of the lobby.
    int before_x = w->player.x, before_y = w->player.y;
    interact_until(w, 29789, px[0], py[0], 0, 0, 6);
    assert(abs(w->player.x - before_x) + abs(w->player.y - before_y) == 4);

    // The overlay's variables follow the timer (set during the last tick,
    // before the tick counter advanced) and the points.
    assert(rc_varbit_get(w, 6386) + 1 == w->tick - raid->start_tick);
    cox_add_points(raid, 1234);
    tick(w, 1);
    assert(rc_varbit_get(w, 5431) == (uint32_t)raid->total_points);
    assert(rc_varp_get(w, 4609) == raid->personal_points);

    // Walking onto the lobby's exit edge enters room 1 at its south entry.
    int ex, ey;
    exit_edge_tile(w, lobby, &ex, &ey);
    assert(rc_world_relocate_player(w, ex, ey, 0));
    tick(w, 2);
    assert(raid->current_room == 1);
    CoxRoom *first = &raid->rooms[1];
    assert(cox_player_in_room(w, first));
    assert(w->player.y == first->y + 1);

    // Stepping onto room 1's south edge goes back to the lobby's exit edge,
    // and standing there does not bounce the player forward again.
    assert(rc_world_relocate_player(w, w->player.x, first->y, first->plane));
    tick(w, 2);
    assert(raid->current_room == 0);
    tick(w, 3);
    assert(raid->current_room == 0);

    // Floor end hole: down to floor 2's start, rope back up to the hole.
    CoxRoom *end = &raid->rooms[raid->floor_end[0]];
    int hx[1], hy[1];
    assert(cox_find_loc(w, end, 29734, hx, hy, NULL, 1) == 1);
    assert(rc_world_relocate_player(w, end->x + 14, end->y + 16, end->plane));
    tick(w, 1);
    interact_until(w, 29734, hx[0], hy[0], 0, 0, 6);
    CoxRoom *start2 = &raid->rooms[raid->floor_start[1]];
    assert(cox_player_in_room(w, start2) && raid->furthest_floor == 1);
    int rx[1], ry[1];
    assert(cox_find_loc(w, start2, 29995, rx, ry, NULL, 1) == 1);
    interact_until(w, 29995, rx[0], ry[0], 0, 0, 6);
    assert(cox_player_in_room(w, end));

    // The last floor's hole leads to the Olm chamber's antechamber.
    CoxRoom *end2 = &raid->rooms[raid->floor_end[1]];
    assert(cox_find_loc(w, end2, 29735, hx, hy, NULL, 1) == 1);
    assert(rc_world_relocate_player(w, end2->x + 12, end2->y + 21, end2->plane));
    tick(w, 1);
    interact_until(w, 29735, hx[0], hy[0], 0, 0, 8);
    assert(raid->current_room == raid->olm_room);
    assert(w->player.x == 3232 && w->player.y == 5721);

    // Dying past the last floor respawns at the end of the floor before.
    assert(rc_combat_damage_player(w, 200, 0, COMBAT_NONE, RC_HIT_SOURCE_STATUS));
    for (int i = 0; i < 4 && !w->player.is_dead; i++) tick(w, 1);
    assert(w->player.is_dead);
    tick(w, 7);
    assert(!w->player.is_dead && cox_player_in_room(w, end2));
    assert(w->player.x == end2->x + 12 && w->player.y == end2->y + 21);

    // Leaving through the Olm chamber steps removes raid-only items.
    assert(rc_player_inventory_add(w, 20801, 1, 0).code == RC_ITEM_RESULT_OK);
    assert(rc_player_inventory_add(w, 20886, 1, 0).code == RC_ITEM_RESULT_OK);
    assert(rc_world_relocate_player(w, 3232, 5754, 0));
    interact_until(w, 29778, 3231, 5755, 0, 0, 8);
    assert(!raid->in_raid && w->player.x == 1233 && w->player.y == 3571);
    assert(!cox_has_item(&w->player, 20801) && "gourd vials stay in the raid");
    assert(cox_has_item(&w->player, 20886) && "lore books leave with you");
    assert(rc_varbit_get(w, 5432) == 0 && rc_varbit_get(w, 5431) == 0);
    assert(rc_varp_get(w, 4609) == 0 && rc_varbit_get(w, 6386) == 0);
    rc_world_destroy(w);
}

static void test_layout_pools(void) {
    for (uint32_t seed = 1; seed <= 40; seed++) {
        RcWorld *w = create_world(seed);
        CoxRaid *raid = cox_raid(w);
        int pool = (int)(seed % 3);
        enter_chambers(w, pool == COX_POOL_FULL && (seed & 1) ? 3 : pool);
        assert(raid->map_pool == pool);
        assert(raid->challenge_mode == (pool == COX_POOL_FULL && (seed & 1)));
        check_layout(raid, raid->map_pool == COX_POOL_FULL ? 3 : 2);
        if (raid->map_pool == COX_POOL_FULL) {
            static const uint8_t first[3] = {COX_ROOM_TEKTON, COX_ROOM_VANGUARDS,
                                             COX_ROOM_GUARDIANS};
            for (int f = 0; f < 3; f++)
                assert(raid->rooms[raid->floor_start[f] + 1].type == first[f]);
            assert(raid->cm == raid->challenge_mode);
        }
        rc_world_destroy(w);
    }
}

// Climbs the lobby's steps (the leader's multiloc) back out.
static void leave_by_lobby_steps(RcWorld *w) {
    CoxRaid *raid = cox_raid(w);
    int x, y;
    assert(cox_find_loc(w, &raid->rooms[0], 49999, &x, &y, NULL, 1) == 1);
    interact_until(w, 50000, x, y, 0, 0, 4);
    assert(!raid->in_raid && w->player.x == 1233);
}

static RcNpc *find_npc(RcWorld *w, int id) {
    int count = 0;
    const RcNpc *npcs = rc_get_npcs(w, &count);
    for (int i = 0; i < count; i++) {
        const RcNpcDef *def = rc_npc_base_def_for_npc(&npcs[i]);
        if (npcs[i].active && def && def->id == id) return (RcNpc *)&npcs[i];
    }
    return NULL;
}

static const char *last_outcome(const RcWorld *w) {
    return rc_interaction_last_outcome(&w->player)->message;
}

static void test_lobby_options_and_death(void) {
    RcWorld *w = create_world(5);
    CoxRaid *raid = cox_raid(w);
    RcPlayer *p = &w->player;
    // The recruiting board no longer does anything: there is no party.
    assert(!rc_world_object_option_supported(w, 29776, 1224, 3562, 0, 0, 0));
    // Challenge Mode from the entrance plays the fixed three-floor layout;
    // leaving and re-entering can pick again.
    enter_chambers(w, 3);
    assert(raid->cm && raid->map_pool == COX_POOL_FULL);
    check_layout(raid, 3);
    leave_by_lobby_steps(w);
    enter_chambers(w, 1);
    assert(!raid->cm && raid->map_pool == COX_POOL_LARGE);
    check_layout(raid, 2);
    leave_by_lobby_steps(w);

    // Captain Rimor turns level scaling off and on.
    RcNpc *rimor = find_npc(w, 7595);
    assert(rimor && "Captain Rimor stands by the entrance");
    static const int talk_ops[2] = {0, 2};
    for (int k = 0; k < 2; k++) {
        rc_player_interact_npc(w, rimor->uid, talk_ops[k]);
        tick(w, 1);
        for (int i = 0; i < 30 && rc_interaction_is_active(p); i++) tick(w, 1);
        assert(strstr(last_outcome(w), "Captain Rimor"));
    }
    rc_player_interact_npc(w, rimor->uid, 3);
    tick(w, 1);
    for (int i = 0; i < 30 && rc_interaction_is_active(p); i++) tick(w, 1);
    assert(raid->full_difficulty && strstr(last_outcome(w), "no longer scale"));

    // In: the leader's steps reload the layout until the raid starts.
    enter_chambers(w, 0);
    assert(raid->scale_combat == 126 && raid->scale_hp == 99);
    CoxRoom *lobby = &raid->rooms[0];
    int sx, sy;
    assert(cox_find_loc(w, lobby, 49999, &sx, &sy, NULL, 1) == 1);
    uint32_t seed = raid->layout_seed;
    interact_until(w, 50000, sx, sy, 0, 1, 4);
    assert(w->choice.active && strcmp(w->choice.title, "Reload the raid?") == 0);
    choose(w, 0);
    assert(has_message(w, "reloaded with a new layout") && raid->layout_seed != seed);
    check_layout(raid, 2);

    // Start; an energy well tops up run energy. The new layout may use
    // another lobby variant.
    lobby = &raid->rooms[0];
    assert(cox_find_loc(w, lobby, 49999, &sx, &sy, NULL, 1) == 1);
    int px[1], py[1];
    assert(cox_find_loc(w, lobby, 29789, px, py, NULL, 1) == 1);
    interact_until(w, 29789, px[0], py[0], 0, 0, 12);
    choose(w, 0);
    assert(raid->started);
    interact_until(w, 50000, sx, sy, 0, 1, 4);
    assert(strstr(last_outcome(w), "can't reload"));
    bool well = false;
    for (int i = 0; i < raid->room_count && !well; i++) {
        int wx, wy;
        if (cox_find_loc(w, &raid->rooms[i], 30066, &wx, &wy, NULL, 1) != 1) continue;
        static const int d[4][2] = {{0, -1}, {-1, 0}, {1, 0}, {0, 1}};
        for (int k = 0; k < 4; k++)
            if (cox_tile_walkable(w, wx + d[k][0], wy + d[k][1], raid->rooms[i].plane)) {
                assert(rc_world_relocate_player(w, wx + d[k][0], wy + d[k][1],
                                                raid->rooms[i].plane));
                break;
            }
        p->run_energy = 0;
        interact_until(w, 30066, wx, wy, raid->rooms[i].plane, 0, 4);
        assert(p->run_energy == 10000);
        well = true;
    }
    assert(well);

    // Death: raid items drop where the raider fell, 40% of points go, and
    // the raider respawns in the lobby (the end of the floor before, later).
    assert(rc_player_inventory_add(w, 20801, 1, 0).code == RC_ITEM_RESULT_OK);
    assert(rc_player_inventory_add(w, 20886, 1, 0).code == RC_ITEM_RESULT_OK);
    raid->personal_points = raid->total_points = 1000;
    int dx = p->x, dy = p->y;
    assert(rc_combat_damage_player(w, 200, 0, COMBAT_NONE, RC_HIT_SOURCE_STATUS));
    for (int i = 0; i < 4 && !p->is_dead; i++) tick(w, 1);
    assert(p->is_dead && raid->personal_points == 600);
    bool dropped = false;
    for (int i = 0; i < w->ground_item_count; i++)
        dropped |= w->ground_items[i].active && w->ground_items[i].item_id == 20801
                && w->ground_items[i].x == dx && w->ground_items[i].y == dy;
    assert(dropped && !cox_has_item(p, 20801) && cox_has_item(p, 20886));
    tick(w, 7);
    assert(!p->is_dead && raid->current_room == 0 && cox_player_in_room(w, lobby));
    assert(has_message(w, "Oh dear, you are dead!"));
    rc_world_destroy(w);
}

// Challenge Mode: +50% to everything but Olm's HP, Vasa's crystals' Magic
// alone, Tekton's Defence +20%; level scaling cuts HP below 126 combat.
static void test_cm_scaling(void) {
    CoxRaid raid;
    memset(&raid, 0, sizeof(raid));
    raid.scale_combat = 126;
    raid.scale_hp = 99;
    RcNpcCombatOverride o;
    const CoxStats head = {.hp = 800, .att = 250, .str = 250, .def = 150, .mag = 250,
                           .rng = 250, .flags = COX_STATS_NO_CM_HP};
    cox_scale(&raid, &head, &o);
    assert(o.hitpoints == 800 && o.stats[0] == 250 && o.stats[1] == 150);
    raid.cm = true;
    cox_scale(&raid, &head, &o);
    assert(o.hitpoints == 800 && o.stats[0] == 375 && o.stats[1] == 225);
    const CoxStats crystal = {.hp = 120, .att = 1, .str = 1, .def = 100, .mag = 100,
                              .rng = 1,
                              .flags = COX_STATS_NO_CM_HP | COX_STATS_CM_MAGIC_ONLY};
    cox_scale(&raid, &crystal, &o);
    assert(o.hitpoints == 120 && o.stats[1] == 100 && o.stats[5] == 150 && o.stats[0] == 1);
    const CoxStats tekton = {.hp = 300, .att = 390, .str = 390, .def = 205, .mag = 205,
                             .rng = 1,
                             .flags = COX_STATS_MAGIC_DEFENSIVE | COX_STATS_TEKTON_DEF};
    cox_scale(&raid, &tekton, &o);
    assert(o.hitpoints == 450 && o.stats[0] == 585 && o.stats[1] == 246 && o.stats[5] == 307);
    raid.cm = false;
    raid.scale_combat = 90;
    cox_scale(&raid, &tekton, &o);
    assert(o.hitpoints == 300 * 90 / 126);
}

int main(void) {
    test_cm_scaling();
    test_enter_and_chain();
    test_layout_pools();
    test_lobby_options_and_death();
    printf("test_cox_raid: ok\n");
    return 0;
}
