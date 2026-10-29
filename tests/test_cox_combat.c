// test_cox_combat — Chambers of Xeric combat rooms on installed data. Each
// room type is forced into a raid slot, then: solo-scaled stats, waking,
// the attacks a raider takes, the room's mechanic (Tekton's anvil, the
// Vanguards' shuffle, Vasa's teleport, siphons and crystals, the Guardians'
// pickaxe rule, shaman spawns and jumps, the mystics' spells, the meat tree
// and the Muttadile emerging, Vespula's grubs, blossoms, soldiers and
// portal), crossing the entry gates, and the exit gate opening when the room
// is cleared.

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../rc-content/content.h"
#include "../rc-content/raids/cox.h"
#include "../rc-core/api.h"
#include "../rc-core/combat.h"
#include "../rc-core/combat_hit.h"
#include "../rc-core/dialogue.h"
#include "../rc-core/interaction.h"
#include "../rc-core/items.h"
#include "../rc-core/object_runtime.h"

static RcWorld *g_w;
static CoxRaid *g_raid;

static const struct { int16_t y; uint8_t plane; } g_rows[] = {
    [COX_ROOM_TEKTON] = {5280, 1}, [COX_ROOM_MUTTADILES] = {5312, 1},
    [COX_ROOM_GUARDIANS] = {5248, 2}, [COX_ROOM_VESPULA] = {5280, 2},
    [COX_ROOM_SHAMANS] = {5248, 0}, [COX_ROOM_VASA] = {5280, 0},
    [COX_ROOM_VANGUARDS] = {5312, 0}, [COX_ROOM_MYSTICS] = {5248, 1},
};

static void tick(int n) {
    for (int i = 0; i < n; i++) rc_world_tick(g_w);
}

static void setup(uint32_t seed) {
    RcWorldConfig cfg = rc_preset_full_game();
    cfg.seed = seed;
    g_w = rc_world_create_config(&cfg);
    assert(g_w && rc_world_install_content(g_w, rc_content_register_all));
    RcPlayer *p = &g_w->player;
    for (int s = 0; s < SKILL_COUNT; s++)
        p->skills.base_level[s] = p->skills.boosted_level[s] = 99;
    // A deep HP pool: the bosses hit for real and a death would respawn.
    p->max_hp = p->current_hp = 99000;
    g_raid = cox_raid(g_w);
    assert(rc_world_relocate_player(g_w, 1233, 3572, 0));
    assert(rc_player_interact_object_at(g_w, 29777, 1232, 3573, 0, 0));
    tick(8);
    assert(rc_player_widget_action(g_w, RC_CHOICE_WIDGET, 0, 0));   // normal raid
    tick(2);
    assert(g_raid->in_raid);
    g_raid->started = true;
    g_raid->start_tick = g_w->tick;
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
    g_w->player.current_hp = g_w->player.max_hp;
    assert(rc_world_relocate_player(g_w, room->x + 15, room->y + 1, room->plane));
    tick(2);
    assert(g_raid->current_room == 1);
    return room;
}

static void stand(CoxRoom *room, int lx, int ly) {
    for (int r = 0; r < 6; r++)
        for (int dy = -r; dy <= r; dy++)
            for (int dx = -r; dx <= r; dx++)
                if (cox_tile_walkable(g_w, room->x + lx + dx, room->y + ly + dy,
                                      room->plane)) {
                    assert(rc_world_relocate_player(g_w, room->x + lx + dx,
                                                    room->y + ly + dy, room->plane));
                    return;
                }
    assert(!"no walkable tile");
}

// Stands on the tile nearest a room-local point whose 3x3 is open.
static void stand_open(CoxRoom *room, int lx, int ly) {
    for (int r = 0; r < 8; r++)
        for (int dy = -r; dy <= r; dy++)
            for (int dx = -r; dx <= r; dx++) {
                int x = room->x + lx + dx, y = room->y + ly + dy;
                bool open = true;
                for (int ty = -1; ty <= 1 && open; ty++)
                    for (int tx = -1; tx <= 1 && open; tx++)
                        open = cox_tile_walkable(g_w, x + tx, y + ty, room->plane);
                if (open) {
                    assert(rc_world_relocate_player(g_w, x, y, room->plane));
                    return;
                }
            }
    assert(!"no open 3x3");
}

// Lands a player hit of `damage` on the NPC and lets it resolve.
static void hit(RcNpc *npc, int damage) {
    rc_queue_hit_meta(npc->pending_hits, &npc->num_pending_hits, damage, 0,
                      COMBAT_MELEE_CRUSH, RC_HIT_SOURCE_PLAYER, 0, g_w->tick, 0,
                      damage);
    tick(1);
}

static void kill(RcNpc *npc) {
    RcNpcId uid = (RcNpcId)npc->uid;
    hit(npc, npc->current_hp);
    RcNpc *still = rc_npc_resolve(g_w, uid);
    assert(!still || still->is_dead || !still->active);
}

static bool loc_at(int loc, int x, int y, int plane) {
    return cox_loc_present(g_w, loc, x, y, plane);
}

static int exit_blocker(CoxRoom *room, int loc) {
    // Gate is the first member of every combat room state.
    const int16_t *g = (const int16_t *)room->state;
    return loc_at(loc, g[0], g[1], room->plane);
}

static int player_hp(void) {
    return g_w->player.current_hp;
}

static void settle(void) {
    tick(1);
    for (int i = 0; i < 60 && rc_interaction_is_active(&g_w->player); i++) tick(1);
}

static bool has_beside(const CoxRoom *room, int x, int y) {
    for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++)
            if ((dx || dy) && cox_tile_walkable(g_w, x + dx, y + dy, room->plane))
                return true;
    return false;
}

// Stands on a walkable tile touching an NPC's footprint.
static void beside_npc(CoxRoom *room, const RcNpc *npc) {
    const RcNpcDef *def = rc_npc_def_for_npc(g_w, npc);
    int size = def && def->size > 0 ? def->size : 1;
    for (int y = npc->y - 1; y <= npc->y + size; y++)
        for (int x = npc->x - 1; x <= npc->x + size; x++) {
            bool inside = x >= npc->x && x < npc->x + size && y >= npc->y
                       && y < npc->y + size;
            if (!inside && cox_tile_walkable(g_w, x, y, room->plane)) {
                assert(rc_world_relocate_player(g_w, x, y, room->plane));
                return;
            }
        }
    assert(!"nothing walkable beside the NPC");
}

// Stands beside a tile (a loc or an NPC's corner).
static void beside(CoxRoom *room, int x, int y) {
    static const int d[8][2] = {{0, -1}, {-1, 0}, {1, 0}, {0, 1},
                                {-1, -1}, {1, -1}, {-1, 1}, {1, 1}};
    for (int k = 0; k < 8; k++)
        if (cox_tile_walkable(g_w, x + d[k][0], y + d[k][1], room->plane)) {
            assert(rc_world_relocate_player(g_w, x + d[k][0], y + d[k][1], room->plane));
            return;
        }
    assert(!"nothing walkable beside the tile");
}

static int slot_of(int item) {
    for (int i = 0; i < RC_INVENTORY_SIZE; i++)
        if (g_w->player.inventory[i].item_id == item) return i;
    return -1;
}

static void give(int item, int qty) {
    assert(rc_player_inventory_add(g_w, item, qty, 0).code == RC_ITEM_RESULT_OK);
}

// From the room before: with the entry gate up, the raider arrives in front
// of it, on the entry row.
static void arrive_from_previous(CoxRoom *room) {
    const CoxRoom *prev = &g_raid->rooms[0];
    for (int a = 0; a < 32; a++) {
        int lx = prev->variant == COX_EXIT_WEST ? 0 : prev->variant == COX_EXIT_EAST ? 31 : a;
        int ly = prev->variant == COX_EXIT_NORTH ? 31 : a;
        if (!cox_tile_walkable(g_w, prev->x + lx, prev->y + ly, prev->plane)) continue;
        // Step off a held edge tile first, as a player walking back would.
        assert(rc_world_relocate_player(g_w, prev->x + 15, prev->y + 15, prev->plane));
        tick(1);
        assert(rc_world_relocate_player(g_w, prev->x + lx, prev->y + ly, prev->plane));
        tick(2);
        if (!cox_player_in_room(g_w, room) || g_w->player.y != room->y)
            fprintf(stderr, "arrive: player %d,%d p%d room %d,%d p%d cur %d prev %d,%d v%d"
                    " entry15=%d,%d row1=%d,%d\n",
                    g_w->player.x, g_w->player.y, g_w->player.plane, room->x, room->y,
                    room->plane, g_raid->current_room, prev->x + lx, prev->y + ly,
                    prev->variant,
                    cox_tile_walkable(g_w, room->x + 15, room->y, room->plane),
                    cox_tile_walkable(g_w, room->x + 16, room->y, room->plane),
                    cox_tile_walkable(g_w, room->x + 15, room->y + 1, room->plane),
                    cox_tile_walkable(g_w, room->x + 16, room->y + 1, room->plane));
        assert(cox_player_in_room(g_w, room) && g_w->player.y == room->y);
        return;
    }
    assert(!"no exit edge tile");
}

static bool has_message(const char *text) {
    const RcGameMessage *m;
    for (uint64_t seq = 0; (m = rc_player_next_game_message(&g_w->player, seq));
         seq = m->sequence)
        if (strstr(m->text, text)) return true;
    return false;
}

static void test_tekton(void) {
    CoxRoom *room = force_room(COX_ROOM_TEKTON, 1);
    RcNpc *tek = cox_room_npc(g_w, room, 1);
    assert(tek && tek->spawn_hp == 300 && tek->stats[0] == 390 && tek->stats[1] == 205);
    assert(tek->owner_controlled && exit_blocker(room, 30017));
    // Walking close wakes him; he raises the fire and comes to fight.
    stand(room, 11, 10);
    int hp = player_hp();
    tick(40);
    assert(room->flags & COX_ROOM_STARTED);
    assert(loc_at(30021, room->x + 15, room->y + 1, room->plane));
    assert(player_hp() < hp && "Tekton's swings land");
    // Out of reach on his scan, he returns to the anvil and is immune there.
    stand(room, 15, 5);
    for (int i = 0; i < 60; i++) {
        tick(1);
        const RcNpcDef *d = rc_npc_def_for_npc(g_w, tek);
        if (d && d->id == 7545) break;
    }
    assert(rc_npc_def_for_npc(g_w, tek)->id == 7545);
    assert(cox_modify_player_hit(g_w, tek, COMBAT_MELEE_CRUSH, -1, -1, 40) == 0);
    tick(30);
    assert(cox_modify_player_hit(g_w, tek, COMBAT_RANGED, -1, -1, 40) == 0);
    assert(cox_modify_player_hit(g_w, tek, COMBAT_MAGIC, -1, -1, 40) == 8);
    // Coming back in, the raider must cross the fire: 15% of base Hitpoints.
    arrive_from_previous(room);
    hp = player_hp();
    assert(rc_player_interact_object_at(g_w, 30021, room->x + 15, room->y + 1,
                                        room->plane, 0));
    settle();
    assert(g_w->player.y == room->y + 3 && hp - player_hp() >= 140);
    // Out of the area he was dropped; he is back awake, not in his
    // dormant spawn form.
    tek = cox_room_npc(g_w, room, 1);
    int form = tek ? rc_npc_def_for_npc(g_w, tek)->id : -1;
    assert(form >= 7541 && form <= 7545);
    kill(tek);
    tick(2);
    assert(room->flags & COX_ROOM_COMPLETE);
    assert(!exit_blocker(room, 30017));
    assert(!loc_at(30021, room->x + 15, room->y + 1, room->plane));
}

static void test_vanguards(void) {
    CoxRoom *room = force_room(COX_ROOM_VANGUARDS, 2);
    RcNpc *v[3];
    for (int i = 0; i < 3; i++) {
        v[i] = cox_room_npc(g_w, room, i + 1);
        if (!v[i] || v[i]->spawn_hp != 180 || !v[i]->player_untargetable)
            fprintf(stderr, "vanguard %d: %p hp %d untarget %d count %d\n", i, (void *)v[i],
                    v[i] ? v[i]->spawn_hp : -1, v[i] ? v[i]->player_untargetable : -1,
                    room->npc_count);
        assert(v[i] && v[i]->spawn_hp == 180 && v[i]->player_untargetable);
    }
    stand(room, 15, 9);
    tick(12);
    for (int i = 0; i < 3; i++) assert(!v[i]->player_untargetable);
    assert(rc_npc_def_for_npc(g_w, v[0])->id == 7527);
    // A 40% HP gap resets and shuffles them.
    hit(v[1], 80);
    tick(1);
    assert(v[1]->player_untargetable && v[1]->current_hp == v[1]->spawn_hp);
    tick(25);
    assert(!v[1]->player_untargetable);
    for (int i = 0; i < 3; i++) {
        RcNpc *n = cox_room_npc(g_w, room, i + 1);
        assert(n);
        kill(n);
        tick(1);
    }
    assert(room->flags & COX_ROOM_COMPLETE);
    assert(!exit_blocker(room, 30017));
}

static void test_vasa(void) {
    CoxRoom *room = force_room(COX_ROOM_VASA, 0);
    RcNpc *vasa = cox_room_npc(g_w, room, 1);
    assert(vasa && vasa->spawn_hp == 300);
    stand(room, 15, 9);
    tick(14);
    // The opening special pulls the raider next to him and stuns them.
    assert(rc_player_is_frozen(g_w));
    int hp = player_hp();
    tick(12);
    assert(player_hp() == 50 && hp > 60 && "the blast leaves 5 HP");
    g_w->player.current_hp = g_w->player.max_hp;
    // He walks to a crystal and siphons, immune.
    for (int i = 0; i < 40; i++) {
        tick(1);
        if (rc_npc_def_for_npc(g_w, vasa)->id == 7567) break;
    }
    assert(rc_npc_def_for_npc(g_w, vasa)->id == 7567);
    assert(cox_modify_player_hit(g_w, vasa, COMBAT_RANGED, -1, -1, 30) == 0);
    // Breaking the active crystal sends him back.
    RcNpc *crystal = NULL;
    for (int i = 0; i < 4 && !crystal; i++) {
        RcNpc *c = cox_room_npc(g_w, room, 2 + i);
        if (c && !c->player_untargetable) crystal = c;
    }
    assert(crystal);
    assert(cox_modify_player_hit(g_w, crystal, COMBAT_RANGED, -1, -1, 30) == 0);
    assert(cox_modify_player_hit(g_w, crystal, COMBAT_MAGIC, -1, -1, 30) == 10);
    kill(crystal);
    tick(2);
    assert(rc_npc_def_for_npc(g_w, vasa)->id == 7566);
    // Left alone, a full siphon heals him and the special comes again.
    bool siphoned = false;
    for (int i = 0; i < 60 && !siphoned; i++) {
        tick(1);
        siphoned = rc_npc_def_for_npc(g_w, vasa)->id == 7567;
    }
    assert(siphoned);
    vasa->current_hp = vasa->spawn_hp / 2;
    int low = vasa->current_hp;
    stand(room, 15, 9);
    bool special = false;
    for (int i = 0; i < 120 && !special; i++) {
        tick(1);
        special = rc_player_is_frozen(g_w);
    }
    assert(special && vasa->current_hp > low && "the siphon's stored heal");
    tick(12);
    g_w->player.current_hp = g_w->player.max_hp;
    // Break every crystal: he stays and keeps throwing boulders.
    for (int i = 0; i < 4; i++) {
        RcNpc *c = cox_room_npc(g_w, room, 2 + i);
        if (c) kill(c);
    }
    tick(30);
    stand(room, 15, 9);
    hp = player_hp();
    tick(30);
    assert(player_hp() < hp && "boulders keep coming");
    kill(vasa);
    tick(2);
    assert(room->flags & COX_ROOM_COMPLETE);
    assert(!exit_blocker(room, 30016));
}

static void test_guardians(void) {
    CoxRoom *room = force_room(COX_ROOM_GUARDIANS, 1);
    RcNpc *g1 = cox_room_npc(g_w, room, 1), *g2 = cox_room_npc(g_w, room, 2);
    assert(g1 && g2 && g1->spawn_hp == 250);
    // Only pickaxe melee hurts them, scaled by Mining and the pickaxe.
    assert(cox_modify_player_hit(g_w, g1, COMBAT_MELEE_CRUSH, -1, 4151, 30) == 0);
    assert(cox_modify_player_hit(g_w, g1, COMBAT_RANGED, -1, 1275, 30) == 0);
    assert(cox_modify_player_hit(g_w, g1, COMBAT_MELEE_CRUSH, -1, 1275, 30)
           == 30 * (50 + 99 + 41) / 150);
    stand(room, 14, 15);
    int hp = player_hp();
    tick(30);
    assert(player_hp() < hp && "melee or rockfall");
    kill(g1);
    tick(6);
    assert(cox_room_npc(g_w, room, 3) && "the broken statue stays");
    kill(g2);
    tick(6);
    assert(room->flags & COX_ROOM_COMPLETE);
}

static void test_shamans_and_mystics(void) {
    CoxRoom *room = force_room(COX_ROOM_SHAMANS, 1);
    RcNpc *s1 = cox_room_npc(g_w, room, 1), *s2 = cox_room_npc(g_w, room, 2);
    assert(s1 && s2 && s1->spawn_hp == 190);
    stand(room, 15, 8);
    int hp = player_hp();
    tick(30);
    assert(room->flags & COX_ROOM_STARTED);
    assert(player_hp() < hp);
    // Tendrils close the entry; the first crossing is free, later ones hurt.
    arrive_from_previous(room);
    hp = player_hp();
    assert(rc_player_interact_object_at(g_w, 29768, room->x + 15, room->y + 1,
                                        room->plane, 0));
    settle();
    assert(g_w->player.y == room->y + 2 && player_hp() == hp);
    // Passing back out leads to the room before.
    assert(rc_player_interact_object_at(g_w, 29768, room->x + 15, room->y + 1,
                                        room->plane, 0));
    settle();
    tick(1);
    assert(!cox_player_in_room(g_w, room));
    arrive_from_previous(room);
    assert(rc_player_interact_object_at(g_w, 29768, room->x + 15, room->y + 1,
                                        room->plane, 0));
    settle();
    assert(g_w->player.y == room->y + 2 && hp - player_hp() >= 140);
    // Spawns appear at the raider and burst; shamans jump onto them where
    // there is room for their 3x3.
    stand_open(room, 15, 12);
    bool spawned = false, jumped = false, burst = false;
    for (int i = 0; i < 400 && !(spawned && burst && jumped); i++) {
        tick(1);
        g_w->player.current_hp = g_w->player.max_hp;
        bool minion = false;
        for (int m = 0; m < 6; m++) minion |= cox_room_npc(g_w, room, 3 + m) != NULL;
        burst |= spawned && !minion;
        spawned |= minion;
        for (int k = 0; k < 2; k++) {
            RcNpc *sh = cox_room_npc(g_w, room, 1 + k);
            jumped |= sh && sh->player_untargetable;
        }
    }
    assert(spawned && burst && jumped);
    kill(cox_room_npc(g_w, room, 1));
    kill(cox_room_npc(g_w, room, 2));
    tick(10);
    assert(room->flags & COX_ROOM_COMPLETE);
}

static void test_mystics(void) {
    CoxRoom *room = force_room(COX_ROOM_MYSTICS, 0);
    int alive = 0;
    for (int i = 0; i < 3; i++) {
        RcNpc *m = cox_room_npc(g_w, room, i + 1);
        if (m) {
            alive++;
            assert(m->spawn_hp == 160);
        }
    }
    assert(alive == 3 && exit_blocker(room, 29741));
    // In sight they cast fire blast and vulnerability (a Defence drain),
    // and close in to melee.
    RcNpc *m = cox_room_npc(g_w, room, 1);
    beside(room, m->x, m->y);
    int hp = player_hp();
    bool drained = false;
    for (int i = 0; i < 200 && !(drained && player_hp() < hp); i++) {
        tick(1);
        drained |= g_w->player.skills.boosted_level[SKILL_DEFENCE] < 99;
    }
    assert(room->flags & COX_ROOM_STARTED);
    assert(drained && player_hp() < hp);
    for (int i = 0; i < 3; i++) kill(cox_room_npc(g_w, room, i + 1));
    tick(2);
    assert(room->flags & COX_ROOM_COMPLETE && !exit_blocker(room, 29741));
}

static void test_muttadiles(void) {
    CoxRoom *room = force_room(COX_ROOM_MUTTADILES, 2);
    RcNpc *small = cox_room_npc(g_w, room, 1), *large = cox_room_npc(g_w, room, 2);
    assert(small && large && large->player_untargetable);
    stand(room, 16, 6);
    tick(4);
    // Below half HP the small one walks to the meat tree and eats.
    hit(small, 140);
    int before = small->current_hp;
    tick(40);
    assert(small->current_hp > before);
    // Chopping the tree down ends the meals (auto-retaliate would swap the
    // chop for a fight with the Muttadile).
    RcNpc *tree = cox_room_npc(g_w, room, 3);
    assert(tree);
    g_w->player.auto_retaliate = false;
    g_w->player.attack_target = -1;
    give(1351, 1);
    beside(room, tree->x, tree->y);
    rc_player_interact_npc(g_w, tree->uid, 0);
    for (int i = 0; i < 200 && cox_room_npc(g_w, room, 3); i++) {
        tick(1);
        g_w->player.current_hp = g_w->player.max_hp;
    }
    assert(!cox_room_npc(g_w, room, 3) && has_message("You chop down the meat tree."));
    g_w->player.auto_retaliate = true;
    kill(small);
    tick(12);
    assert(!large->player_untargetable && rc_npc_def_for_npc(g_w, large)->id == 7563);
    // The large one fights in melee and at range.
    stand(room, 16, 6);
    int hp = player_hp();
    tick(40);
    assert(player_hp() < hp);
    kill(large);
    tick(2);
    assert(room->flags & COX_ROOM_COMPLETE && !exit_blocker(room, 30018));
}

static void test_vespula(void) {
    CoxRoom *room = force_room(COX_ROOM_VESPULA, 0);
    RcNpc *vesp = cox_room_npc(g_w, room, 1), *portal = cox_room_npc(g_w, room, 2);
    assert(vesp && portal && portal->spawn_hp == 250);
    assert(cox_modify_player_hit(g_w, portal, COMBAT_MELEE_SLASH, -1, -1, 30) == 0);
    assert(cox_modify_player_hit(g_w, vesp, COMBAT_MELEE_SLASH, -1, -1, 30) == 0);
    stand(room, 15, 6);
    // She stings grubs; an infected grub drains until a soldier hatches.
    bool stung = false;
    for (int i = 0; i < 120 && !stung; i++) {
        tick(1);
        for (int g = 0; g < 4; g++) {
            RcNpc *grub = cox_room_npc(g_w, room, 3 + g);
            stung |= grub && grub->current_hp < grub->spawn_hp;
        }
    }
    assert(stung);
    // The roots flower: a blossom heals a grub, and the root flowers again.
    int roots_x[6], roots_y[6], rx = -1, ry = -1;
    int roots = cox_find_loc(g_w, room, 30069, roots_x, roots_y, NULL, 6);
    for (int t = 0; t < 20 && rx < 0; t++) {
        tick(1);
        for (int i = 0; i < roots && i < 6 && rx < 0; i++)
            if (loc_at(30068, roots_x[i], roots_y[i], room->plane)
                    && has_beside(room, roots_x[i], roots_y[i])) {
                rx = roots_x[i];
                ry = roots_y[i];
            }
    }
    assert(rx >= 0 && "a flowered root");
    beside(room, rx, ry);
    assert(rc_player_interact_object_at(g_w, 30068, rx, ry, room->plane, 0));
    settle();
    assert(slot_of(20892) >= 0 && !loc_at(30068, rx, ry, room->plane));
    RcNpc *grub = NULL;
    int gi = -1;
    for (int g = 0; g < 4 && !grub; g++) {
        RcNpc *c = cox_room_npc(g_w, room, 3 + g);
        if (c && c->current_hp < c->spawn_hp - 40) {
            grub = c;
            gi = g;
        }
    }
    assert(grub && rc_npc_def_for_npc(g_w, grub)->id == 7536 && "infected");
    beside_npc(room, grub);
    int fed = grub->current_hp;
    assert(rc_player_use_inventory_item_on_npc(g_w, slot_of(20892), grub->uid));
    settle();
    assert(grub->current_hp >= fed + 30 && slot_of(20892) < 0);
    tick(32);
    assert(loc_at(30068, rx, ry, room->plane) && "the root flowers again");
    // A drained grub hatches a soldier, which fights and heals the portal.
    stand(room, 15, 6);
    grub->current_hp = 1;
    RcNpc *soldier = NULL;
    for (int i = 0; i < 12 && !soldier; i++) {
        tick(1);
        soldier = cox_room_npc(g_w, room, 7 + gi);
    }
    assert(soldier && rc_npc_def_for_npc(g_w, soldier)->id == 7538);
    int hp = player_hp();
    portal->current_hp = portal->spawn_hp - 50;
    tick(30);
    assert(player_hp() < hp && portal->current_hp > portal->spawn_hp - 50);
    // Raid NPCs never get the generic 2-damage poison (severity 10).
    assert(g_w->player.toxin_severity == 0 || g_w->player.toxin_severity > 10);
    // At 6 HP or less it lands, and bursts 21 ticks later.
    RcNpcId soldier_uid = (RcNpcId)soldier->uid;
    hit(soldier, soldier->current_hp - 5);
    tick(1);
    assert(rc_npc_def_for_npc(g_w, soldier)->id == 7539 && soldier->owner_controlled);
    tick(22);
    assert(!rc_npc_resolve(g_w, soldier_uid) && !cox_room_npc(g_w, room, 7 + gi));
    // Provoked, she shoots back.
    g_w->player.current_hp = g_w->player.max_hp;
    hit(vesp, 5);
    hp = player_hp();
    tick(12);
    assert(player_hp() < hp);
    // Hitting the portal enrages her and drains the attacker's prayer.
    g_w->player.current_prayer_points = 990;
    hit(portal, 5);
    assert(rc_npc_def_for_npc(g_w, vesp)->id == 7531);
    tick(6);
    assert(g_w->player.current_prayer_points < 990);
    tick(22);
    assert(rc_npc_def_for_npc(g_w, vesp)->id == 7530 && "the rage passes");
    // Hitting her low makes her land.
    hit(vesp, vesp->current_hp - vesp->spawn_hp / 5);
    assert(rc_npc_def_for_npc(g_w, vesp)->id == 7532);
    kill(portal);
    tick(2);
    assert(!cox_room_npc(g_w, room, 1) && "the portal takes Vespula with it");
    assert(room->flags & COX_ROOM_COMPLETE);
}

int main(void) {
    setup(5);
    test_tekton();
    test_vanguards();
    test_vasa();
    test_guardians();
    test_shamans_and_mystics();
    test_mystics();
    test_muttadiles();
    test_vespula();
    rc_world_destroy(g_w);
    printf("test_cox_combat: ok\n");
    return 0;
}
