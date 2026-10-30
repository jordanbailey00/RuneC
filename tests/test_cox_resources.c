// test_cox_resources — Chambers of Xeric resource systems on installed data:
// the resource room (old tools, gourd tree, geyser, herb patches growing on
// the room clock, inspecting, clearing and level checks), herblore
// (cleaning, gourd potions, overloads), raid potion effects (overload damage
// and boosts, prayer enhance pulses), raid food, fires and cooking, fishing
// (and the cave snake) and bat catching, scavenger beasts, their drops and
// respawns, the large scavenger room's shortcut, saplings and kindling, tool
// spawns, raking weeds, and building and upgrading storage units.

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../rc-content/content.h"
#include "../rc-content/raids/cox.h"
#include "../rc-core/api.h"
#include "../rc-core/dialogue.h"
#include "../rc-core/combat_hit.h"
#include "../rc-core/coordinates.h"
#include "../rc-core/objects.h"
#include "../rc-core/interaction.h"
#include "../rc-core/items.h"
#include "../rc-core/object_runtime.h"

static RcWorld *g_w;
static CoxRaid *g_raid;

static const struct { int16_t y; uint8_t plane; } g_rows[] = {
    [COX_ROOM_SCAVENGERS] = {5216, 0}, [COX_ROOM_SCAVENGERS_LARGE] = {5216, 1},
    [COX_ROOM_FARMING_FISH] = {5440, 0},
    [COX_ROOM_FARMING_BATS] = {5440, 1}, [COX_ROOM_ICE_DEMON] = {5344, 0},
};

static void tick(int n) {
    for (int i = 0; i < n; i++) rc_world_tick(g_w);
}

static void interact(int obj_id, int x, int y, int plane, int option) {
    assert(rc_player_interact_object_at(g_w, obj_id, x, y, plane, option));
    tick(1);
    for (int i = 0; i < 60 && rc_interaction_is_active(&g_w->player); i++) tick(1);
}

static void use_on_object(int item_id, int obj_id, int x, int y, int plane) {
    int slot = -1;
    for (int i = 0; i < RC_INVENTORY_SIZE && slot < 0; i++)
        if (g_w->player.inventory[i].item_id == item_id) slot = i;
    assert(slot >= 0);
    assert(rc_player_use_inventory_item_on_object(g_w, slot, obj_id, x, y, plane));
    tick(1);
    for (int i = 0; i < 60 && rc_interaction_is_active(&g_w->player); i++) tick(1);
}

static int slot_of(int item_id) {
    for (int i = 0; i < RC_INVENTORY_SIZE; i++)
        if (g_w->player.inventory[i].item_id == item_id) return i;
    return -1;
}

static void use_on_item(int item_id, int target_id) {
    int a = slot_of(item_id), b = slot_of(target_id);
    assert(a >= 0 && b >= 0);
    assert(rc_player_use_inventory_item_on_inventory_item(g_w, a, b));
    tick(2);
}

static void give(int item_id, int quantity) {
    assert(rc_player_inventory_add(g_w, item_id, quantity, 0).code == RC_ITEM_RESULT_OK);
}

static void clear_inventory(void) {
    for (int i = 0; i < RC_INVENTORY_SIZE; i++)
        g_w->player.inventory[i] = (RcInvSlot){.item_id = -1};
    g_w->player.inventory_revision++;
}

static CoxRoom *find_room(CoxRaid *raid, int a, int b) {
    for (int i = 0; i < raid->room_count; i++)
        if (raid->rooms[i].type == a || raid->rooms[i].type == b)
            return &raid->rooms[i];
    return NULL;
}

static void goto_room(CoxRoom *room, int lx, int ly) {
    assert(rc_world_relocate_player(g_w, room->x + lx, room->y + ly, room->plane));
    tick(1);
}

// Moves next to the first placement of `loc` in the room.
static void near_loc(CoxRoom *room, int loc, int *x, int *y) {
    assert(cox_find_loc(g_w, room, loc, x, y, NULL, 1) >= 1);
    static const int d[8][2] = {{0, -1}, {-1, 0}, {1, 0}, {0, 1},
                                {-1, -1}, {1, -1}, {-1, 1}, {1, 1}};
    for (int k = 0; k < 8; k++) {
        int px = *x + d[k][0], py = *y + d[k][1];
        if (cox_tile_walkable(g_w, px, py, room->plane)) {
            assert(rc_world_relocate_player(g_w, px, py, room->plane));
            tick(1);
            return;
        }
    }
    assert(!"no tile beside the loc");
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

// Puts `type` (variant `v`) in raid slot 1 and walks the player in.
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

static void interact_npc(const RcNpc *npc) {
    rc_player_interact_npc(g_w, npc->uid, 0);
    tick(1);
    for (int i = 0; i < 40 && rc_interaction_is_active(&g_w->player); i++) tick(1);
}

static bool has_free_side(const CoxRoom *room, int x, int y) {
    static const int d[4][2] = {{0, -1}, {-1, 0}, {1, 0}, {0, 1}};
    for (int k = 0; k < 4; k++)
        if (cox_tile_walkable(g_w, x + d[k][0], y + d[k][1], room->plane)) return true;
    return false;
}

static void near_loc_xy(const CoxRoom *room, int x, int y) {
    static const int d[8][2] = {{0, -1}, {-1, 0}, {1, 0}, {0, 1},
                                {-1, -1}, {1, -1}, {-1, 1}, {1, 1}};
    for (int k = 0; k < 8; k++)
        if (cox_tile_walkable(g_w, x + d[k][0], y + d[k][1], room->plane)) {
            assert(rc_world_relocate_player(g_w, x + d[k][0], y + d[k][1], room->plane));
            tick(1);
            return;
        }
    assert(!"no tile beside");
}

static void clear_kindling(void) {
    for (int i = 0; i < RC_INVENTORY_SIZE; i++)
        if (g_w->player.inventory[i].item_id == 20799)
            g_w->player.inventory[i] = (RcInvSlot){.item_id = -1};
    g_w->player.inventory_revision++;
}

static void setup(uint32_t seed) {
    RcWorldConfig cfg = rc_preset_full_game();
    cfg.seed = seed;
    g_w = rc_world_create_config(&cfg);
    assert(g_w && rc_world_install_content(g_w, rc_content_register_all));
    RcPlayer *p = &g_w->player;
    for (int s = 0; s < SKILL_COUNT; s++)
        p->skills.base_level[s] = p->skills.boosted_level[s] = 99;
    p->max_hp = p->current_hp = 990;
    rc_prayer_reset(p);
    CoxRaid *raid = cox_raid(g_w);
    g_raid = raid;
    assert(rc_world_relocate_player(g_w, 1233, 3572, 0));
    interact(29777, 1232, 3573, 0, 0);
    assert(raid->in_raid);
    raid->started = true;
    raid->start_tick = g_w->tick;
}

static void test_farming_and_herblore(void) {
    CoxRoom *farm = force_room(COX_ROOM_FARMING_FISH, 1);
    goto_room(farm, 16, 2);
    clear_inventory();
    int x, y;

    near_loc(farm, 29771, &x, &y);
    interact(29771, x, y, farm->plane, 0);
    assert(cox_has_item(&g_w->player, 5341) && cox_has_item(&g_w->player, 952)
           && cox_has_item(&g_w->player, 5343));

    near_loc(farm, 29772, &x, &y);
    interact(29772, x, y, farm->plane, 0);
    assert(cox_inventory_count(&g_w->player, 20800) == 1);
    near_loc(farm, 29878, &x, &y);
    interact(29878, x, y, farm->plane, 0);
    assert(cox_inventory_count(&g_w->player, 20801) == 1);

    // Plant golpar, watch it grow on the room clock, pick it.
    give(20906, 2);
    int px[2], py[2];
    assert(cox_find_loc(g_w, farm, 29765, px, py, NULL, 2) == 2);
    near_loc(farm, 29765, &x, &y);
    use_on_object(20906, 29765, px[0], py[0], farm->plane);
    assert(rc_world_object_active_id(g_w, 29765, px[0], py[0], farm->plane) == 29998);
    tick(60);
    int grown = rc_world_object_active_id(g_w, 29765, px[0], py[0], farm->plane);
    assert(grown == 30010);
    interact(30010, px[0], py[0], farm->plane, 0);
    tick(20);
    int herbs = cox_inventory_count(&g_w->player, 20904);
    assert(herbs >= 3);
    assert(rc_world_object_active_id(g_w, 29765, px[0], py[0], farm->plane) == 29765);

    // Clean, then mix an Elder (+) at 99 Herblore.
    int slot = slot_of(20904);
    assert(rc_player_interact_inventory_item(g_w, slot, 0));
    tick(2);
    assert(cox_has_item(&g_w->player, 20905));
    give(20910, 1);
    use_on_item(20905, 20801);
    tick(3);
    assert(cox_has_item(&g_w->player, 20924) && "Elder (+)(4)");

    // Drink it: +6 + 16% Attack.
    slot = slot_of(20924);
    assert(rc_player_drink(g_w, slot));
    tick(2);
    // 6 + 16% of 99; the regular decay may already have taken a level.
    assert(g_w->player.skills.boosted_level[SKILL_ATTACK] >= 99 + 6 + 15 - 1);
    assert(cox_has_item(&g_w->player, 20923));
}

static void test_overload_and_enhance(void) {
    CoxRaid *raid = cox_raid(g_w);
    RcPlayer *p = &g_w->player;
    clear_inventory();
    for (int s = 0; s < SKILL_COUNT; s++) p->skills.boosted_level[s] = 99;
    give(20902, 1);
    give(20801, 1);
    give(20924, 1);   // Elder (+)(4)
    give(20936, 1);   // Twisted (+)(4)
    give(20948, 1);   // Kodai (+)(4)
    use_on_item(20902, 20801);
    tick(3);
    assert(cox_has_item(p, 20996) && "Overload (+)(4)");
    assert(!cox_has_item(p, 20924) && !cox_has_item(p, 20936) && !cox_has_item(p, 20948));

    int hp = p->current_hp;
    assert(rc_player_drink(g_w, slot_of(20996)));
    tick(14);
    assert(hp - p->current_hp == 500 && "five hits of 10");
    assert(p->skills.boosted_level[SKILL_RANGED] >= 99 + 6 + 15 - 1);
    // The boost is reapplied after it drains.
    p->skills.boosted_level[SKILL_STRENGTH] = 99;
    tick(30);
    assert(p->skills.boosted_level[SKILL_STRENGTH] >= 99 + 6 + 15 - 1);
    tick(500);
    assert(raid->overload_until == 0 && p->current_hp == hp && "healed back 50");

    // Below 51 HP it cannot be drunk.
    p->current_hp = 400;
    assert(!rc_player_drink(g_w, slot_of(20995)) || (tick(2), cox_has_item(p, 20995)));
    p->current_hp = p->max_hp;

    give(20972, 1);   // Prayer enhance (+)(4)
    p->current_prayer_points = 100;
    assert(rc_player_drink(g_w, slot_of(20972)));
    tick(200);
    assert(p->current_prayer_points > 100 && raid->enhance_until > 0);
}

static void test_fire_cooking_and_catching(void) {
    RcPlayer *p = &g_w->player;
    CoxRoom *farm = force_room(COX_ROOM_FARMING_FISH, 2);
    clear_inventory();
    goto_room(farm, 16, 2);
    int raw = -1;
    if (farm->type == COX_ROOM_FARMING_FISH) {
        give(307, 1);
        give(20853, 30);
        int x, y;
        near_loc(farm, 29889, &x, &y);
        interact(29889, x, y, farm->plane, 0);
        for (int i = 0; i < 200 && raw < 0; i++) {
            tick(1);
            for (int t = 0; t < 7; t++)
                if (cox_has_item(p, 20855 + 2 * t)) raw = 20855 + 2 * t;
        }
    } else {
        give(10010, 1);
        RcNpc *bat = NULL;
        for (int i = 0; i < 40 && !bat; i++) {
            tick(1);
            int count = 0;
            const RcNpc *npcs = rc_get_npcs(g_w, &count);
            for (int n = 0; n < count && !bat; n++) {
                const RcNpcDef *def = rc_npc_base_def_for_npc(&npcs[n]);
                if (npcs[n].active && def && def->id >= 7587 && def->id <= 7593)
                    bat = (RcNpc *)&npcs[n];
            }
        }
        assert(bat);
        for (int attempt = 0; attempt < 40 && raw < 0; attempt++) {
            if (!rc_npc_resolve(g_w, bat->uid)) break;
            rc_player_interact_npc(g_w, bat->uid, 0);
            tick(6);
            for (int t = 0; t < 7; t++)
                if (cox_has_item(p, 20870 + 2 * t)) raw = 20870 + 2 * t;
        }
    }
    assert(raw > 0 && "caught raw raid food");

    // Light a ground fire from kindling and cook on it.
    give(590, 1);
    give(20799, 1);
    goto_room(farm, 16, 3);
    int fx = p->x, fy = p->y;
    use_on_item(590, 20799);
    tick(1);
    assert(rc_world_object_active_id(g_w, 26185, fx, fy, farm->plane) == 26185);
    use_on_object(raw, 26185, fx, fy, farm->plane);
    tick(8);
    assert(!cox_has_item(p, raw));
    int burnt = raw < 20870 ? 20854 : 20869;
    assert(cox_has_item(p, raw + 1) || cox_has_item(p, burnt));
}

static void test_scavengers_weeds_storage(void) {
    CoxRaid *raid = cox_raid(g_w);
    RcPlayer *p = &g_w->player;
    CoxRoom *scav = force_room(COX_ROOM_SCAVENGERS, 1);
    goto_room(scav, 8, 17);     // open floor: the beast spawns 3-7 tiles away
    tick(6);
    RcNpc *beast = cox_room_npc(g_w, scav, 1);
    assert(beast && beast->spawn_hp == 30);
    int bx = beast->x, by = beast->y;
    beast->current_hp = 0;
    beast->is_dead = true;
    RcPayloadNpcEvent died = {.npc_id = (uint32_t)beast->uid, .def_id = 7548};
    rc_event_fire(g_w, RC_EVT_NPC_DIED, &died);
    bool bones = false;
    for (int i = 0; i < g_w->ground_item_count; i++)
        bones |= g_w->ground_items[i].active && g_w->ground_items[i].item_id == 526
              && g_w->ground_items[i].x == bx && g_w->ground_items[i].y == by;
    assert(bones);

    // Weeds in the end room: 1-3 seeds per rake.
    clear_inventory();
    give(5341, 1);
    CoxRoom *end = &raid->rooms[raid->olm_room - 1];
    int x, y;
    near_loc(end, 29773, &x, &y);
    interact(29773, x, y, end->plane, 0);
    tick(10);
    int seeds = cox_inventory_count(p, 20903) + cox_inventory_count(p, 20906)
              + cox_inventory_count(p, 20909);
    assert(seeds >= 2);

    // Building the end room's storage unit upgrades every hotspot.
    give(2347, 1);
    give(21036, 2);
    near_loc(end, 29769, &x, &y);
    int sx = x, sy = y;
    interact(29769, sx, sy, end->plane, 4);
    assert(raid->storage_tier == 1);
    assert(rc_world_object_active_id(g_w, 29769, sx, sy, end->plane) == 29770);
    // The units only show their options for now.
    interact(29770, sx, sy, end->plane, 0);
    assert(strstr(last_outcome(), "can't hold items"));
    // Upgrading needs the next Construction level.
    give(21036, 2);
    p->skills.boosted_level[SKILL_CONSTRUCTION] = 50;
    interact(29770, sx, sy, end->plane, 4);
    assert(raid->storage_tier == 1 && strstr(last_outcome(), "level of 60"));
    p->skills.boosted_level[SKILL_CONSTRUCTION] = 99;
    interact(29770, sx, sy, end->plane, 4);
    assert(raid->storage_tier == 2);
    assert(rc_world_object_active_id(g_w, 29769, sx, sy, end->plane) == 29779);
}

static void test_patch_checks(void) {
    CoxRoom *farm = force_room(COX_ROOM_FARMING_FISH, 1);
    RcPlayer *p = &g_w->player;
    clear_inventory();
    give(5343, 1);
    give(20903, 1);
    int px[2], py[2], x, y;
    assert(cox_find_loc(g_w, farm, 29765, px, py, NULL, 2) == 2);
    near_loc(farm, 29765, &x, &y);
    interact(29765, px[0], py[0], farm->plane, 1);
    assert(strstr(last_outcome(), "This patch is empty"));
    p->skills.boosted_level[SKILL_FARMING] = 20;
    use_on_object(20903, 29765, px[0], py[0], farm->plane);
    assert(strstr(last_outcome(), "level of 55") && cox_has_item(p, 20903));
    p->skills.boosted_level[SKILL_FARMING] = 99;
    use_on_object(20903, 29765, px[0], py[0], farm->plane);
    assert(!cox_has_item(p, 20903));
    int growing = rc_world_object_active_id(g_w, 29765, px[0], py[0], farm->plane);
    assert(growing == 29997);
    interact(growing, px[0], py[0], farm->plane, 1);
    assert(strstr(last_outcome(), "growing noxifer"));
    tick(60);
    int grown = rc_world_object_active_id(g_w, 29765, px[0], py[0], farm->plane);
    assert(grown == 29997 + 12);
    interact(grown, px[0], py[0], farm->plane, 2);
    assert(strstr(last_outcome(), "You clear the patch"));
    assert(rc_world_object_active_id(g_w, 29765, px[0], py[0], farm->plane) == 29765);
}

static void test_fishing_and_snake(void) {
    CoxRoom *farm = force_room(COX_ROOM_FARMING_FISH, 0);
    RcPlayer *p = &g_w->player;
    clear_inventory();
    give(307, 1);
    give(20853, 200);
    int sx[3], sy[3];
    int spots = cox_find_loc(g_w, farm, 29889, sx, sy, NULL, 3);
    assert(spots == 3);
    // One of the spots hides the cave snake. The chatbox ring keeps only the
    // latest messages, so the bite is noted as it arrives.
    int fish = 0;
    bool bitten = false;
    for (int k = 0; k < spots && !bitten; k++) {
        static const int d[8][2] = {{0, -1}, {-1, 0}, {1, 0}, {0, 1},
                                    {-1, -1}, {1, -1}, {-1, 1}, {1, 1}};
        for (int n = 0; n < 8; n++)
            if (cox_tile_walkable(g_w, sx[k] + d[n][0], sy[k] + d[n][1], farm->plane)) {
                assert(rc_world_relocate_player(g_w, sx[k] + d[n][0], sy[k] + d[n][1],
                                                farm->plane));
                break;
            }
        assert(rc_player_interact_object_at(g_w, 29889, sx[k], sy[k], farm->plane, 0));
        for (int t = 0; t < 120; t++) {
            tick(1);
            p->current_hp = p->max_hp;
            bitten |= has_message("A cave snake bites you!");
        }
        for (int t = 0; t < 7; t++) fish += cox_inventory_count(p, 20855 + 2 * t);
    }
    assert(fish > 0 && bitten);
}

static void test_bats(void) {
    CoxRoom *farm = force_room(COX_ROOM_FARMING_BATS, 0);
    RcPlayer *p = &g_w->player;
    clear_inventory();
    // Three bats fly around the room; a net catches them.
    RcNpc *bat = NULL;
    for (int i = 0; i < 3; i++) {
        RcNpc *b = cox_room_npc(g_w, farm, 1 + i);
        assert(b);
        if (!bat) bat = b;
    }
    // Below 99 Hunter they need a net (base 98 too, or the walk restores it).
    p->skills.base_level[SKILL_HUNTER] = p->skills.boosted_level[SKILL_HUNTER] = 98;
    interact_npc(bat);
    assert(strstr(last_outcome(), "butterfly net"));
    give(10010, 1);
    int caught = 0;
    for (int attempt = 0; attempt < 60 && !caught; attempt++) {
        bat = NULL;
        for (int i = 0; i < 3 && !bat; i++) bat = cox_room_npc(g_w, farm, 1 + i);
        if (!bat) {
            tick(1);
            continue;
        }
        interact_npc(bat);
        for (int t = 0; t < 7; t++) caught += cox_inventory_count(p, 20870 + 2 * t);
    }
    assert(caught > 0);
    // Caught bats are replaced 10-20 ticks later.
    tick(22);
    for (int i = 0; i < 3; i++) assert(cox_room_npc(g_w, farm, 1 + i));
}

static void test_shortcut_and_beasts(void) {
    CoxRoom *room = force_room(COX_ROOM_SCAVENGERS_LARGE, 1);
    RcPlayer *p = &g_w->player;
    static const int kinds[3] = {29736, 29738, 29740};
    static const int skills[3] = {SKILL_WOODCUTTING, SKILL_MINING, SKILL_STRENGTH};
    int sx = room->x + 18, sy = room->y + 22, kind = -1;
    for (int k = 0; k < 3; k++)
        if (cox_loc_present(g_w, kinds[k], sx, sy, room->plane)) kind = k;
    assert(kind >= 0 && "a shortcut blocks the large room");
    clear_inventory();
    give(1351, 1);
    give(1265, 1);
    near_loc_xy(room, sx, sy);
    // The room's beast would turn the clearing into a fight.
    p->auto_retaliate = false;
    p->skills.boosted_level[skills[kind]] = 1;
    interact(kinds[kind], sx, sy, room->plane, 0);
    assert(strstr(last_outcome(), "level of"));
    p->skills.boosted_level[skills[kind]] = 99;
    int points = g_raid->personal_points;
    interact(kinds[kind], sx, sy, room->plane, 0);
    tick(4);
    assert(g_raid->personal_points > points);
    if (kind == 2) {
        assert(!cox_loc_present(g_w, 29740, sx, sy, room->plane) && "pushed aside");
    } else {
        int cleared = kind == 0 ? 29737 : 29739;
        assert(cox_loc_present(g_w, cleared, sx, sy, room->plane));
        // Cross to the far side of the cleared shortcut.
        int fx = p->x, fy = p->y;
        interact(cleared, sx, sy, room->plane, 0);
        assert(p->x == 2 * sx - fx && p->y == 2 * sy - fy);
    }
    // A beast spawns near the raider and fights; another follows each kill.
    p->auto_retaliate = true;
    p->current_hp = p->max_hp;
    int hp = p->current_hp;
    RcNpc *beast = NULL;
    for (int t = 0; t < 10 && !beast; t++) {
        tick(1);
        beast = cox_room_npc(g_w, room, 1);
    }
    assert(beast);
    // The shortcut may stand between them: meet it on its side.
    near_loc_xy(room, beast->x, beast->y);
    for (int t = 0; t < 30 && p->current_hp >= hp; t++) tick(1);
    assert(p->current_hp < hp);
    RcNpcId uid = (RcNpcId)beast->uid;
    rc_queue_hit_meta(beast->pending_hits, &beast->num_pending_hits, beast->current_hp, 0,
                      COMBAT_MELEE_CRUSH, RC_HIT_SOURCE_PLAYER, 0, g_w->tick, 0,
                      beast->current_hp);
    tick(2);
    RcNpc *next = NULL;
    for (int t = 0; t < 10 && !next; t++) {
        tick(1);
        RcNpc *b = cox_room_npc(g_w, room, 1);
        if (b && (RcNpcId)b->uid != uid) next = b;
    }
    assert(next && "a new beast");
}

static void test_saplings_and_tools(void) {
    CoxRoom *room = force_room(COX_ROOM_ICE_DEMON, 0);
    RcPlayer *p = &g_w->player;
    clear_inventory();
    // Tool spawns: one of each at a time.
    int x, y;
    near_loc(room, 31862, &x, &y);
    interact(31862, x, y, room->plane, 0);
    assert(cox_has_item(p, 1351));
    interact(31862, x, y, room->plane, 0);
    assert(strstr(last_outcome(), "already have one"));
    near_loc(room, 31634, &x, &y);
    interact(31634, x, y, room->plane, 0);
    assert(cox_has_item(p, 590));
    // Saplings give kindling until one falls (then it regrows).
    int kx[16], ky[16];
    int n = cox_find_loc(g_w, room, 29763, kx, ky, NULL, 16);
    assert(n > 0);
    bool fell = false;
    for (int k = 0; k < n && k < 16 && !fell; k++) {
        if (!has_free_side(room, kx[k], ky[k])) continue;
        near_loc_xy(room, kx[k], ky[k]);
        assert(rc_player_interact_object_at(g_w, 29763, kx[k], ky[k], room->plane, 0));
        for (int t = 0; t < 200 && !fell; t++) {
            tick(1);
            fell = cox_loc_present(g_w, 29764, kx[k], ky[k], room->plane);
            if (cox_inventory_count(p, 20799) >= 20) clear_kindling();
        }
        if (fell) {
            tick(52);
            assert(!cox_loc_present(g_w, 29764, kx[k], ky[k], room->plane)
                   && "the sapling grows back");
        }
    }
    assert(fell && has_message("You get some kindling."));
}

int main(void) {
    setup(91);
    test_farming_and_herblore();
    test_overload_and_enhance();
    test_fire_cooking_and_catching();
    test_scavengers_weeds_storage();
    test_patch_checks();
    test_fishing_and_snake();
    test_bats();
    test_shortcut_and_beasts();
    test_saplings_and_tools();
    rc_world_destroy(g_w);
    printf("test_cox_resources: ok\n");
    return 0;
}
