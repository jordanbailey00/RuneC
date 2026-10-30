// test_cox_raid — Chambers of Xeric controller on installed data: entering
// through the steps with no party (straight into a normal raid), the one
// fixed layout (composed map rooms joined exit to
// entry), starting at the lobby passage, crossing passages, walking between
// rooms both ways, the hole down to Great Olm and the rope back, death and
// respawn, Captain Rimor, and leaving with raid-only items removed.

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

// The entrance's steps start a normal raid at once: no party, no prompt.
static void enter_chambers(RcWorld *w) {
    assert(rc_world_relocate_player(w, 1233, 3572, 0));
    interact_until(w, 29777, 1232, 3573, 0, 0, 6);
    assert(!w->choice.active);
    CoxRaid *raid = cox_raid(w);
    assert(raid->in_raid);
    assert(raid->current_room == 0);
    assert(cox_player_in_room(w, &raid->rooms[0]));
}

// The one layout: the start room, the rooms easiest to hardest and the end
// room stacked north from (3472, 5440), then Great Olm's own chamber.
static void check_layout(const CoxRaid *raid) {
    static const uint8_t types[7] = {
        COX_ROOM_START, COX_ROOM_TIGHTROPE, COX_ROOM_CRABS, COX_ROOM_SHAMANS,
        COX_ROOM_MUTTADILES, COX_ROOM_TEKTON, COX_ROOM_END};
    assert(raid->room_count == 8 && raid->olm_room == 7);
    for (int i = 0; i < 7; i++) {
        const CoxRoom *room = &raid->rooms[i];
        assert(room->type == types[i] && room->variant == COX_EXIT_NORTH);
        assert(room->x == 3472 && room->y == 5440 + 32 * i && room->plane == 0);
    }
    const CoxRoom *olm = &raid->rooms[7];
    assert(olm->type == COX_ROOM_OLM && olm->x == 3200 && olm->y == 5696);
}

// Walks to a tile and waits to arrive.
static void walk(RcWorld *w, int x, int y) {
    assert(rc_player_walk_to(w, x, y));
    for (int i = 0; i < 20 && (w->player.x != x || w->player.y != y); i++)
        tick(w, 1);
    tick(w, 1);
    if (w->player.x != x || w->player.y != y)
        fprintf(stderr, "walk to %d,%d stopped at %d,%d\n", x, y, w->player.x,
                w->player.y);
    assert(w->player.x == x && w->player.y == y);
}

static void test_enter_and_chain(void) {
    RcWorld *w = create_world(77);
    CoxRaid *raid = cox_raid(w);
    assert(raid && !raid->in_raid);
    // No party is needed: the steps start the raid.
    enter_chambers(w);
    check_layout(raid);
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

    // The rooms join: from each room's exit, walking north crosses into the
    // next room's entry, and walking back returns. Every room's opening is
    // local x 15-16; x 15 stays open a tile further in.
    for (int i = 0; i + 1 < raid->olm_room; i++) {
        const CoxRoom *room = &raid->rooms[i], *next = &raid->rooms[i + 1];
        assert(rc_world_relocate_player(w, room->x + 16, room->y + 31, 0));
        tick(w, 1);
        assert(raid->current_room == i);
        walk(w, next->x + 15, next->y + 1);
        assert(raid->current_room == i + 1);
        walk(w, room->x + 15, room->y + 31);
        assert(raid->current_room == i);
    }

    // The end room's hole leads to the Olm chamber's antechamber and
    // completes the level; the chamber's rope climbs back beside the hole.
    CoxRoom *end = &raid->rooms[raid->olm_room - 1];
    int hx[1], hy[1];
    assert(cox_find_loc(w, end, 29735, hx, hy, NULL, 1) == 1);
    assert(rc_world_relocate_player(w, end->x + 12, end->y + 21, end->plane));
    tick(w, 1);
    interact_until(w, 29735, hx[0], hy[0], 0, 0, 8);
    assert(raid->current_room == raid->olm_room && raid->reached_olm);
    assert(w->player.x == 3232 && w->player.y == 5721);
    assert(has_message(w, "Lower level complete! Duration:"));
    assert(rc_varbit_get(w, 5425) == 2);
    CoxRoom *olm = &raid->rooms[raid->olm_room];
    int rx[1], ry[1];
    assert(cox_find_loc(w, olm, 29996, rx, ry, NULL, 1) == 1);
    interact_until(w, 29996, rx[0], ry[0], 0, 0, 12);
    assert(raid->current_room == raid->olm_room - 1);
    assert(w->player.x == end->x + 12 && w->player.y == end->y + 21);
    interact_until(w, 29735, hx[0], hy[0], 0, 0, 8);
    assert(raid->current_room == raid->olm_room);

    // Dying past the level respawns beside the end room's hole.
    assert(rc_combat_damage_player(w, 200, 0, COMBAT_NONE, RC_HIT_SOURCE_STATUS));
    for (int i = 0; i < 4 && !w->player.is_dead; i++) tick(w, 1);
    assert(w->player.is_dead);
    tick(w, 7);
    assert(!w->player.is_dead && cox_player_in_room(w, end));
    assert(w->player.x == end->x + 12 && w->player.y == end->y + 21);

    // Leaving through the Olm chamber steps removes raid-only items.
    interact_until(w, 29735, hx[0], hy[0], 0, 0, 8);
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
    // Outside a raid the end room's hole and Olm's rope do nothing.
    assert(rc_world_relocate_player(w, 3472 + 12, 5632 + 21, 0));
    interact_until(w, 29735, 3472 + 11, 5632 + 22, 0, 0, 6);
    assert(strstr(last_outcome(w), "Nothing interesting happens"));
    CoxRoom chamber = {.type = COX_ROOM_OLM, .x = 3200, .y = 5696};
    int rx, ry;
    assert(cox_find_loc(w, &chamber, 29996, &rx, &ry, NULL, 1) == 1);
    assert(rc_world_relocate_player(w, rx, ry - 1, 0));
    interact_until(w, 29996, rx, ry, 0, 0, 6);
    assert(strstr(last_outcome(w), "Nothing interesting happens"));
    // Leaving and entering again plays the same layout.
    enter_chambers(w);
    check_layout(raid);
    leave_by_lobby_steps(w);
    enter_chambers(w);
    check_layout(raid);
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

    // In: with one layout, the leader's steps have nothing to reload.
    enter_chambers(w);
    assert(raid->scale_combat == 126 && raid->scale_hp == 99);
    CoxRoom *lobby = &raid->rooms[0];
    int sx, sy;
    assert(cox_find_loc(w, lobby, 49999, &sx, &sy, NULL, 1) == 1);
    assert(!rc_world_object_option_supported(w, 50000, sx, sy, 0, 0, 1));

    // Start; an energy well tops up run energy.
    int px[1], py[1];
    assert(cox_find_loc(w, lobby, 29789, px, py, NULL, 1) == 1);
    interact_until(w, 29789, px[0], py[0], 0, 0, 12);
    choose(w, 0);
    assert(raid->started);
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
    // the raider respawns in the lobby (beside the end room's hole, past it).
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

    // Teleporting out of the raid's rooms ends it, as the steps do.
    assert(rc_player_inventory_add(w, 20801, 1, 0).code == RC_ITEM_RESULT_OK);
    assert(rc_world_relocate_player(w, 1233, 3572, 0));
    tick(w, 1);
    assert(!raid->in_raid && !cox_has_item(p, 20801));
    assert(rc_varbit_get(w, 5432) == 0 && rc_varbit_get(w, 5425) == 0);
    rc_world_destroy(w);
}

// Solo scaling at 126 combat and 99 HP keeps the wiki's values; level
// scaling cuts HP below 126 combat.
static void test_scaling(void) {
    CoxRaid raid;
    memset(&raid, 0, sizeof(raid));
    raid.scale_combat = 126;
    raid.scale_hp = 99;
    RcNpcCombatOverride o;
    const CoxStats head = {.hp = 800, .att = 250, .str = 250, .def = 150, .mag = 250,
                           .rng = 250};
    cox_scale(&raid, &head, &o);
    assert(o.hitpoints == 800 && o.stats[0] == 250 && o.stats[1] == 150);
    const CoxStats tekton = {.hp = 300, .att = 390, .str = 390, .def = 205, .mag = 205,
                             .rng = 1, .flags = COX_STATS_MAGIC_DEFENSIVE};
    cox_scale(&raid, &tekton, &o);
    assert(o.hitpoints == 300 && o.stats[0] == 390 && o.stats[1] == 205 && o.stats[5] == 205);
    raid.scale_combat = 90;
    cox_scale(&raid, &tekton, &o);
    assert(o.hitpoints == 300 * 90 / 126);
}

int main(void) {
    test_scaling();
    test_enter_and_chain();
    test_lobby_options_and_death();
    printf("test_cox_raid: ok\n");
    return 0;
}
