// test_cox_puzzles — Chambers of Xeric puzzle rooms on installed data: the
// Ice demon's braziers and thaw, the Thieving room's chests (grubs, bats,
// gas, relocking), the creature keeper's journal and the corrupted
// scavenger, the Tightrope crossing with its keystone, and the Jewelled
// crabs: the orb's travel and rules (crabs turn it and colour a white orb,
// clashing colours end it, it hits raiders in its path), Smash rooting a
// crab, crab aggression, and the orb lighting crystals.

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../rc-content/content.h"
#include "../rc-content/raids/cox.h"
#include "../rc-core/api.h"
#include "../rc-core/dialogue.h"
#include "../rc-core/combat_hit.h"
#include "../rc-core/interaction.h"
#include "../rc-core/items.h"

static RcWorld *g_w;
static CoxRaid *g_raid;

static const struct { int16_t y; uint8_t plane; } g_rows[] = {
    [COX_ROOM_ICE_DEMON] = {5344, 0}, [COX_ROOM_THIEVING] = {5376, 0},
    [COX_ROOM_TIGHTROPE] = {5344, 1}, [COX_ROOM_CRABS] = {5344, 2},
};

static void tick(int n) {
    for (int i = 0; i < n; i++) rc_world_tick(g_w);
}

static void give(int item, int qty) {
    assert(rc_player_inventory_add(g_w, item, qty, 0).code == RC_ITEM_RESULT_OK);
}

static void interact(int obj, int x, int y, int plane, int option) {
    assert(rc_player_interact_object_at(g_w, obj, x, y, plane, option));
    tick(1);
    for (int i = 0; i < 60 && rc_interaction_is_active(&g_w->player); i++) tick(1);
}

static void setup(void) {
    RcWorldConfig cfg = rc_preset_full_game();
    cfg.seed = 17;
    g_w = rc_world_create_config(&cfg);
    assert(g_w && rc_world_install_content(g_w, rc_content_register_all));
    RcPlayer *p = &g_w->player;
    for (int s = 0; s < SKILL_COUNT; s++)
        p->skills.base_level[s] = p->skills.boosted_level[s] = 99;
    p->max_hp = p->current_hp = 99000;
    g_raid = cox_raid(g_w);
    assert(rc_world_relocate_player(g_w, 1233, 3572, 0));
    interact(29777, 1232, 3573, 0, 0);
    assert(rc_player_widget_action(g_w, RC_CHOICE_WIDGET, 0, 0));   // normal raid
    tick(2);
    assert(g_raid->in_raid);
    g_raid->started = true;
}

static CoxRoom *force_room(int type, int v) {
    CoxRoom *room = &g_raid->rooms[1];
    cox_remove_room_npcs(g_w, room);
    for (int i = 0; i < g_raid->room_count; i++)
        if (i != 1 && g_raid->rooms[i].type == type) g_raid->rooms[i].type = COX_ROOM_NONE;
    memset(room, 0, sizeof(*room));
    room->type = (uint8_t)type;
    room->variant = (uint8_t)v;
    room->x = (int16_t)(3264 + 32 * v);
    room->y = g_rows[type].y;
    room->plane = g_rows[type].plane;
    g_raid->current_room = COX_NO_ROOM;
    assert(rc_world_relocate_player(g_w, room->x + 15, room->y + 1, room->plane));
    tick(2);
    assert(g_raid->current_room == 1);
    return room;
}

// Stands beside the loc and interacts with it.
static void beside(CoxRoom *room, int x, int y) {
    static const int d[8][2] = {{0, -1}, {-1, 0}, {1, 0}, {0, 1},
                                {-1, -1}, {1, -1}, {-1, 1}, {1, 1}};
    for (int k = 0; k < 8; k++)
        if (cox_tile_walkable(g_w, x + d[k][0], y + d[k][1], room->plane)) {
            assert(rc_world_relocate_player(g_w, x + d[k][0], y + d[k][1], room->plane));
            return;
        }
    assert(!"nothing walkable beside the loc");
}

static bool has_message(const char *text) {
    const RcGameMessage *m;
    for (uint64_t seq = 0; (m = rc_player_next_game_message(&g_w->player, seq));
         seq = m->sequence)
        if (strstr(m->text, text)) return true;
    return false;
}

static const char *last_outcome(void) {
    return rc_interaction_last_outcome(&g_w->player)->message;
}

static void kill(RcNpc *npc) {
    rc_queue_hit_meta(npc->pending_hits, &npc->num_pending_hits, npc->current_hp,
                      0, COMBAT_MAGIC, RC_HIT_SOURCE_PLAYER, 0, g_w->tick, 0,
                      npc->current_hp);
    tick(2);
}

static void test_ice_demon(void) {
    CoxRoom *room = force_room(COX_ROOM_ICE_DEMON, 1);
    RcNpc *demon = cox_room_npc(g_w, room, 1);
    assert(demon && demon->player_untargetable && demon->spawn_hp == 140);
    int bx[4], by[4];
    assert(cox_find_loc(g_w, room, 29747, bx, by, NULL, 4) == 4);
    give(590, 1);
    // Light every brazier, then keep them fed until the demon thaws.
    for (int b = 0; b < 4; b++) {
        give(20799, 1);
        beside(room, bx[b], by[b]);
        interact(29747, bx[b], by[b], room->plane, 0);
        for (int i = 0; i < 80 && !cox_loc_present(g_w, 29748, bx[b], by[b], room->plane); i++)
            tick(1);
        assert(cox_loc_present(g_w, 29748, bx[b], by[b], room->plane));
    }
    for (int round = 0; round < 40 && rc_npc_def_for_npc(g_w, demon)->id != 7585; round++) {
        for (int b = 0; b < 4; b++) {
            if (!cox_loc_present(g_w, 29748, bx[b], by[b], room->plane)) continue;
            give(20799, 4);
            beside(room, bx[b], by[b]);
            interact(29748, bx[b], by[b], room->plane, 0);
        }
        tick(10);
    }
    tick(12);
    assert(rc_npc_def_for_npc(g_w, demon)->id == 7585 && !demon->player_untargetable);
    assert(cox_modify_player_hit(g_w, demon, COMBAT_MELEE_SLASH, -1, -1, 30) == 9);
    int fire = rc_spell_find("Fire Wave");
    if (fire >= 0)
        assert(cox_modify_player_hit(g_w, demon, COMBAT_MAGIC, fire, -1, 20) == 50);
    kill(demon);
    tick(2);
    assert(room->flags & COX_ROOM_COMPLETE);
}

static void test_thieving(void) {
    CoxRoom *room = force_room(COX_ROOM_THIEVING, 0);
    int cx[80], cy[80];
    int n = cox_find_loc(g_w, room, 29742, cx, cy, NULL, 80);
    assert(n > 50);
    give(1523, 1);
    int grubs = 0;
    for (int c = 0; c < n && grubs == 0; c++) {
        beside(room, cx[c], cy[c]);
        interact(29742, cx[c], cy[c], room->plane, 0);
        tick(20);
        grubs = cox_inventory_count(&g_w->player, 20885);
    }
    assert(grubs > 0);
    // Feed it the 30 grubs a solo raid needs; it eats one every 3 ticks.
    give(20885, 30 - grubs > 0 ? 30 - grubs : 0);
    int tx, ty;
    assert(cox_find_loc(g_w, room, 29746, &tx, &ty, NULL, 1) == 1);
    beside(room, tx, ty);
    int points = g_raid->personal_points;
    interact(29746, tx, ty, room->plane, 0);
    assert(g_raid->personal_points >= points + 115 * 30);
    tick(100);
    assert(room->flags & COX_ROOM_COMPLETE);
    RcNpc *scav = cox_room_npc(g_w, room, 1);
    assert(scav && rc_npc_def_for_npc(g_w, scav)->id == 7603);
}

static void test_tightrope(void) {
    CoxRoom *room = force_room(COX_ROOM_TIGHTROPE, 0);
    int guards = 0;
    for (int g = 0; g < 4; g++) guards += cox_room_npc(g_w, room, g + 1) != NULL;
    assert(guards == 4);
    int rx[2], ry[2];
    assert(cox_find_loc(g_w, room, 29750, rx, ry, NULL, 2) == 2);
    beside(room, rx[0], ry[0]);
    int hp = g_w->player.current_hp;
    assert(rc_player_interact_object_at(g_w, 29750, rx[0], ry[0], room->plane, 0));
    tick(14);
    assert(cox_chebyshev(g_w->player.x, g_w->player.y, 1, rx[1], ry[1], 1) <= 1);
    assert(g_w->player.current_hp < hp && "the guards fire during the crossing");
    int kx, ky;
    assert(cox_find_loc(g_w, room, 29751, &kx, &ky, NULL, 1) == 1);
    beside(room, kx, ky);
    interact(29751, kx, ky, room->plane, 0);
    assert(cox_has_item(&g_w->player, 20884));
    // The barrier is a spawned loc.
    int bx = -1, by = -1;
    for (int i = 0; i < g_w->object_state_count; i++)
        if (g_w->object_states[i].active_obj_id == 29749) {
            bx = g_w->object_states[i].active_x;
            by = g_w->object_states[i].active_y;
        }
    assert(bx >= 0);
    beside(room, bx, by);
    interact(29749, bx, by, room->plane, 0);
    assert(room->flags & COX_ROOM_COMPLETE);
    assert(!cox_room_npc(g_w, room, 1) && "the guards die with the barrier");
}

static void test_crabs(void) {
    CoxRoom *room = force_room(COX_ROOM_CRABS, 1);
    RcNpc *crab = cox_room_npc(g_w, room, 2);
    assert(crab);
    assert(cox_modify_player_hit(g_w, crab, COMBAT_MELEE_SLASH, -1, -1, 30) == 0);
    // A melee hit turns the crab red.
    g_w->player.combat_style = COMBAT_MELEE_SLASH;
    g_w->player.manual_spell_cast = g_w->player.autocast_spell = -1;
    rc_queue_hit_meta(crab->pending_hits, &crab->num_pending_hits, 0, 0,
                      COMBAT_MELEE_SLASH, RC_HIT_SOURCE_PLAYER, 0, g_w->tick, 0, 1);
    tick(1);
    assert(rc_npc_def_for_npc(g_w, crab)->id == 7577);
    // The orb leaves the carving and travels; a new one follows each.
    RcNpc *orb = NULL;
    for (int t = 0; t < 20 && !orb; t++) {
        tick(1);
        orb = cox_room_npc(g_w, room, 1);
    }
    assert(orb);
    // Steer orbs straight into each crystal in its colour.
    static const int locs[4] = {29758, 29759, 29760, 29761};
    static const int colours[4] = {0, 1, 2, 3};
    for (int i = 0; i < 4; i++) {
        int x, y;
        assert(cox_find_loc(g_w, room, locs[i], &x, &y, NULL, 1) == 1);
        for (int t = 0; t < 10 && !(orb = cox_room_npc(g_w, room, 1)); t++) tick(1);
        assert(orb);
        // Put it on the tile south of the crystal, heading north in the
        // crystal's colour. CrabState starts relief (4 bytes), crystal
        // tiles (16), lit (4), lit_count, then orb_dir and orb_colour.
        orb->x = x;
        orb->y = y - 1;
        room->state[25] = 0;
        room->state[26] = (uint8_t)colours[i];
        tick(2);
        assert(cox_loc_present(g_w, 29762, x, y, room->plane));
    }
    tick(10);
    assert(room->flags & COX_ROOM_COMPLETE);
}

static void test_chests_and_keeper(void) {
    CoxRoom *room = force_room(COX_ROOM_THIEVING, 1);
    RcPlayer *p = &g_w->player;
    for (int i = 0; i < RC_INVENTORY_SIZE; i++) p->inventory[i] = (RcInvSlot){.item_id = -1};
    p->inventory_revision++;
    give(1523, 1);
    // The creature keeper carries a journal (its fifth option, Search).
    int kx, ky;
    assert(cox_find_loc(g_w, room, 29875, &kx, &ky, NULL, 1) == 1);
    beside(room, kx, ky);
    interact(29875, kx, ky, room->plane, 4);
    assert(cox_has_item(p, 20886));
    interact(29875, kx, ky, room->plane, 4);
    assert(strstr(last_outcome(), "nothing of interest"));
    // Every chest opens in the end: grubs, the bats, or gas.
    int cx[80], cy[80];
    int n = cox_find_loc(g_w, room, 29742, cx, cy, NULL, 80);
    int opened = -1;
    bool bats = false, gas = false;
    uint64_t read = p->messages.next_sequence;
    for (int c = 0; c < n && c < 80; c++) {
        beside(room, cx[c], cy[c]);
        interact(29742, cx[c], cy[c], room->plane, 0);
        // The picking goes on, an attempt a tick, until the chest opens.
        for (int t = 0; t < 40 && g_raid->action.kind; t++) tick(1);
        p->current_hp = p->max_hp;
        const RcGameMessage *m;
        while ((m = rc_player_next_game_message(p, read))) {
            read = m->sequence;
            bats |= strstr(m->text, "You find some bats in the chest.") != NULL;
            gas |= strstr(m->text, "A cloud of gas bursts out of the chest!") != NULL;
        }
        if (opened < 0 && rc_world_object_active_id(g_w, 29742, cx[c], cy[c], room->plane)
                != 29742)
            opened = c;
        // Keep room for the bats.
        for (int i = 0; i < RC_INVENTORY_SIZE; i++)
            if (p->inventory[i].item_id == 20885) p->inventory[i] = (RcInvSlot){.item_id = -1};
        p->inventory_revision++;
    }
    assert(bats && cox_inventory_count(p, 20883) == 5);
    assert(gas);
    // Opened chests lock again.
    assert(opened >= 0);
    tick(16);
    assert(rc_world_object_active_id(g_w, 29742, cx[opened], cy[opened], room->plane) == 29742);
}

static void test_tightrope_refusals(void) {
    CoxRoom *room = force_room(COX_ROOM_TIGHTROPE, 1);
    int bx = -1, by = -1;
    for (int i = 0; i < g_w->object_state_count; i++)
        if (g_w->object_states[i].active_obj_id == 29749) {
            bx = g_w->object_states[i].active_x;
            by = g_w->object_states[i].active_y;
        }
    assert(bx >= 0);
    beside(room, bx, by);
    interact(29749, bx, by, room->plane, 0);
    assert(strstr(last_outcome(), "needs a keystone"));
    assert(!(room->flags & COX_ROOM_COMPLETE));
    int kx, ky;
    assert(cox_find_loc(g_w, room, 29751, &kx, &ky, NULL, 1) == 1);
    beside(room, kx, ky);
    interact(29751, kx, ky, room->plane, 0);
    interact(29751, kx, ky, room->plane, 0);
    assert(strstr(last_outcome(), "already have a keystone"));
    // A guard can be killed without ending anything.
    kill(cox_room_npc(g_w, room, 1));
    assert(!cox_room_npc(g_w, room, 1) && !(room->flags & COX_ROOM_COMPLETE));
}

// CrabState: relief (4 bytes), crystal tiles (16), lit (4), lit_count, then
// orb_dir and orb_colour.
enum { ORB_DIR = 25, ORB_COLOUR = 26 };
static const int g_step[4][2] = {{0, 1}, {1, 0}, {0, -1}, {-1, 0}};   // N E S W

static RcNpc *next_orb(CoxRoom *room) {
    RcNpc *orb = NULL;
    for (int t = 0; t < 12 && !orb; t++) {
        orb = cox_room_npc(g_w, room, 1);
        if (!orb) tick(1);
    }
    assert(orb);
    return orb;
}

// Puts the orb beside `x,y`, heading at it, in `colour`.
static void aim_orb(CoxRoom *room, RcNpc *orb, int x, int y, int dir, int colour) {
    orb->x = orb->prev_x = x - g_step[dir][0];
    orb->y = orb->prev_y = y - g_step[dir][1];
    room->state[ORB_DIR] = (uint8_t)dir;
    room->state[ORB_COLOUR] = (uint8_t)colour;
}

static void test_crab_rules(void) {
    CoxRoom *room = force_room(COX_ROOM_CRABS, 0);
    RcPlayer *p = &g_w->player;
    // The orb travels at most a tile a tick (a crab bounce turns it in place).
    RcNpc *orb = next_orb(room);
    RcNpcId first = (RcNpcId)orb->uid;
    int steps = 0;
    for (int t = 0; t < 6; t++) {
        int x0 = orb->x, y0 = orb->y;
        tick(1);
        orb = cox_room_npc(g_w, room, 1);
        if (!orb || (RcNpcId)orb->uid != first) break;
        int d = abs(orb->x - x0) + abs(orb->y - y0);
        assert(d <= 1);
        steps += d;
    }
    assert(steps > 0);
    // A raider in its path takes 22 and the orb is spent.
    bool struck = false;
    for (int attempt = 0; attempt < 20 && !struck; attempt++) {
        orb = next_orb(room);
        int dir = room->state[ORB_DIR];
        int px = orb->x + g_step[dir][0], py = orb->y + g_step[dir][1];
        if (!cox_tile_walkable(g_w, px, py, room->plane)) {
            tick(1);
            continue;
        }
        assert(rc_world_relocate_player(g_w, px, py, room->plane));
        int hp = p->current_hp;
        RcNpcId uid = (RcNpcId)orb->uid;
        tick(2);
        struck = !rc_npc_resolve(g_w, uid) && hp - p->current_hp >= 220;
        p->current_hp = p->max_hp;
    }
    assert(struck);
    // Smash needs a hammer; it roots the crab and turns it red.
    RcNpc *crab = cox_room_npc(g_w, room, 2);
    assert(crab);
    beside(room, crab->x, crab->y);
    rc_player_interact_npc(g_w, crab->uid, 2);
    tick(1);
    for (int i = 0; i < 20 && rc_interaction_is_active(p); i++) tick(1);
    assert(strstr(last_outcome(), "need a hammer"));
    give(2347, 1);
    rc_player_interact_npc(g_w, crab->uid, 2);
    tick(1);
    for (int i = 0; i < 20 && rc_interaction_is_active(p); i++) tick(1);
    assert(rc_npc_def_for_npc(g_w, crab)->id == 7577);
    int cx = crab->x, cy = crab->y;
    assert(rc_world_relocate_player(g_w, cx + 2, cy, room->plane)
           || rc_world_relocate_player(g_w, cx - 2, cy, room->plane));
    tick(8);
    assert(crab->x == cx && crab->y == cy && "rooted");
    // A white orb bouncing off the red crab turns red and turns right.
    orb = next_orb(room);
    aim_orb(room, orb, cx, cy, 0, 0);
    tick(1);
    assert(room->state[ORB_COLOUR] == 1 && room->state[ORB_DIR] == 1);
    assert(rc_npc_def_for_npc(g_w, orb)->id == 7581);
    // A green orb meeting the red crab fizzles out.
    aim_orb(room, orb, cx, cy, 0, 2);
    RcNpcId uid = (RcNpcId)orb->uid;
    tick(1);
    assert(!rc_npc_resolve(g_w, uid));
    // An unrooted crab follows a nearby raider and swings at them.
    RcNpc *other = cox_room_npc(g_w, room, 3);
    assert(other);
    beside(room, other->x, other->y);
    int hp = p->current_hp;
    for (int t = 0; t < 30 && p->current_hp >= hp; t++) tick(1);
    assert(p->current_hp < hp);
}

int main(void) {
    setup();
    test_ice_demon();
    test_thieving();
    test_chests_and_keeper();
    test_tightrope();
    test_tightrope_refusals();
    test_crab_rules();
    test_crabs();
    rc_world_destroy(g_w);
    printf("test_cox_puzzles: ok\n");
    return 0;
}
