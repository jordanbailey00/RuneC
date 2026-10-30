// test_cox_olm — the Great Olm on installed data: passing the barrier
// raises it in a wall with both claws, autos land on a raider in its view,
// each part's style weakness, killing both claws sinks it under a crystal
// shower and raises it on the other wall, and the final stand ends the raid
// with the exit opened. A second fight stands in view through every phase:
// spheres, each power's attacks (acid, flame, crystal), the left claw's
// specials and clench, dousing a fire, the penultimate claw recovery and
// heal, and the final stand's life siphon.

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../rc-content/content.h"
#include "../rc-content/raids/cox.h"
#include "../rc-core/api.h"
#include "../rc-core/combat_hit.h"
#include "../rc-core/dialogue.h"
#include "../rc-core/interaction.h"

static RcWorld *g_w;
static CoxRaid *g_raid;

// Messages read as they arrive (the ring keeps only the latest few).
static const char *const g_wanted[] = {
    "fires a sphere", "rises with the power of acid", "smothered you in acid",
    "rises with the power of flame", "rises with the power of crystal",
    "sounds a cry", "clenches to protect itself",
};
#define WANTED (int)(sizeof(g_wanted) / sizeof(g_wanted[0]))
static bool g_seen[WANTED];
static uint64_t g_read;

static void read_messages(void) {
    const RcGameMessage *m;
    while ((m = rc_player_next_game_message(&g_w->player, g_read))) {
        g_read = m->sequence;
        for (int i = 0; i < WANTED; i++) g_seen[i] |= strstr(m->text, g_wanted[i]) != NULL;
    }
}

static bool has_message(const char *text) {
    read_messages();
    for (int i = 0; i < WANTED; i++)
        if (strcmp(g_wanted[i], text) == 0) return g_seen[i];
    assert(!"unknown message");
    return false;
}

static bool loc_active(int loc) {
    for (int i = 0; i < g_w->object_state_count; i++)
        if (g_w->object_states[i].active_obj_id == loc) return true;
    return false;
}

static void read_messages(void);

static void tick(int n) {
    for (int i = 0; i < n; i++) {
        rc_world_tick(g_w);
        read_messages();
    }
}

static void kill(RcNpc *npc) {
    rc_queue_hit_meta(npc->pending_hits, &npc->num_pending_hits, npc->current_hp,
                      0, COMBAT_RANGED, RC_HIT_SOURCE_PLAYER, 0, g_w->tick, 0,
                      npc->current_hp);
    tick(1);
}

static CoxRoom *olm_room(void) {
    return &g_raid->rooms[g_raid->olm_room];
}

static RcNpc *part(int role) {
    return cox_room_npc(g_w, olm_room(), role);
}

static int form(const RcNpc *npc) {
    return rc_npc_def_for_npc(g_w, npc)->id;
}

static void wait_active(void) {
    for (int i = 0; i < 80; i++) {
        tick(1);
        RcNpc *head = part(1);
        if (head && form(head) == 7554) return;
    }
    assert(!"Olm never rose");
}

static void start_fight(uint32_t seed) {
    RcWorldConfig cfg = rc_preset_full_game();
    cfg.seed = seed;
    g_w = rc_world_create_config(&cfg);
    assert(g_w && rc_world_install_content(g_w, rc_content_register_all));
    RcPlayer *p = &g_w->player;
    for (int s = 0; s < SKILL_COUNT; s++)
        p->skills.base_level[s] = p->skills.boosted_level[s] = 99;
    p->max_hp = p->current_hp = 99000;
    g_raid = cox_raid(g_w);
    assert(rc_world_relocate_player(g_w, 1233, 3572, 0));
    assert(rc_player_interact_object_at(g_w, 29777, 1232, 3573, 0, 0));
    tick(8);
    assert(g_raid->in_raid);
    g_raid->started = true;
    g_raid->start_tick = g_w->tick;

    // Into the antechamber, then through the barrier.
    assert(rc_world_relocate_player(g_w, 3232, 5727, 0));
    tick(2);
    assert(g_raid->current_room == g_raid->olm_room);
    assert(rc_player_interact_object_at(g_w, 29879, 3231, 5729, 0, 0));
    tick(4);
    assert(p->y == 5730 && "through the barrier");
}

static void test_olm_fight(void) {
    start_fight(23);
    RcPlayer *p = &g_w->player;
    RcNpc *head = part(1), *melee = part(2), *mage = part(3);
    assert(head && melee && mage);
    assert(head->spawn_hp == 800 && melee->spawn_hp == 600 && mage->spawn_hp == 600);
    bool west = head->x < 3230;
    // There is no way back through the barrier while it lives.
    assert(rc_player_interact_object_at(g_w, 29879, 3231, 5729, 0, 0));
    tick(3);
    assert(p->y >= 5730
           && strstr(rc_interaction_last_outcome(p)->message, "can't leave"));
    wait_active();
    assert(!melee->player_untargetable && !mage->player_untargetable);
    assert(head->player_untargetable && "the head waits for its final stand");
    // Only the weakness lands in full.
    assert(cox_modify_player_hit(g_w, melee, COMBAT_MELEE_SLASH, -1, -1, 30) == 30);
    assert(cox_modify_player_hit(g_w, melee, COMBAT_MAGIC, -1, -1, 30) == 10);
    assert(cox_modify_player_hit(g_w, mage, COMBAT_MAGIC, -1, -1, 30) == 30);
    assert(cox_modify_player_hit(g_w, head, COMBAT_RANGED, -1, -1, 30) == 30);
    assert(cox_modify_player_hit(g_w, head, COMBAT_MELEE_CRUSH, -1, -1, 30) == 10);
    // A raider in its view is attacked.
    assert(rc_world_relocate_player(g_w, 3232, 5738, 0));
    int hp = p->current_hp;
    tick(24);
    assert(p->current_hp < hp);

    // Three claw phases, switching walls after each.
    for (int phase = 1; phase <= 3; phase++) {
        melee = part(2);
        mage = part(3);
        assert(melee && mage);
        kill(mage);
        tick(2);
        kill(part(2));
        // Sink, shower, rise.
        for (int i = 0; i < 60; i++) {
            tick(1);
            RcNpc *h = part(1);
            bool on_west = h && h->x < 3230;
            if (h && form(h) == 7551 && on_west != west) break;
        }
        head = part(1);
        if (phase < 3) {
            assert(head && (head->x < 3230) != west && "it rises on the other wall");
            west = !west;
        }
        wait_active();
        assert(p->current_hp > 0);
        p->current_hp = p->max_hp;
    }
    // Final stand: the head alone, now targetable.
    head = part(1);
    assert(head && !head->player_untargetable);
    assert(!part(2) && !part(3));
    kill(head);
    tick(3);
    assert(g_raid->olm_dead && (olm_room()->flags & COX_ROOM_COMPLETE));
    assert(!cox_loc_present(g_w, 30018, 3232, 5749, 0) && "the exit crystal shatters");
    assert(cox_loc_present(g_w, 30028, 3233, 5751, 0));
    // No loot: the chest is empty.
    assert(rc_world_relocate_player(g_w, 3232, 5750, 0));
    assert(rc_player_interact_object_at(g_w, 30028, 3233, 5751, 0, 0));
    tick(1);
    for (int i = 0; i < 20 && rc_interaction_is_active(p); i++) tick(1);
    assert(strstr(rc_interaction_last_outcome(p)->message, "The chest is empty."));
    // The steps out end the raid.
    assert(rc_world_relocate_player(g_w, 3232, 5754, 0));
    assert(rc_player_interact_object_at(g_w, 29778, 3231, 5755, 0, 0));
    tick(6);
    assert(!g_raid->in_raid && p->x == 1233);
    rc_world_destroy(g_w);
}

static void test_olm_mechanics(void) {
    start_fight(41);
    RcPlayer *p = &g_w->player;
    wait_active();
    bool acid = false, fire = false, bomb = false, burst = false, moved = false;
    bool burn = false, doused = false, clench = false, recovered = false;
    bool healed = false;
    for (int phase = 1; phase <= 3; phase++) {
        assert(rc_world_relocate_player(g_w, 3232, 5738, 0));
        int clench_hits = 0;
        for (int t = 0; t < 400; t++) {
            int px = p->x, py = p->y;
            // Phases 1-2: big hits on the left claw now and then make it
            // clench (after a null step). Penultimate: during its heal,
            // damage heals the claw instead.
            RcNpc *melee = part(2);
            int before = melee ? melee->current_hp : 0;
            if (melee && phase < 3 && !clench && clench_hits < 16 && t % 3 == 0) {
                rc_queue_hit_meta(melee->pending_hits, &melee->num_pending_hits, 30, 0,
                                  COMBAT_MELEE_SLASH, RC_HIT_SOURCE_PLAYER, 0, g_w->tick,
                                  0, 30);
                clench_hits++;
            }
            if (melee && phase == 3 && t % 2 == 0 && melee->current_hp > 100)
                rc_queue_hit_meta(melee->pending_hits, &melee->num_pending_hits, 5, 0,
                                  COMBAT_MELEE_SLASH, RC_HIT_SOURCE_PLAYER, 0, g_w->tick,
                                  0, 5);
            tick(1);
            p->current_hp = p->max_hp;
            healed |= phase == 3 && melee && part(2) == melee && melee->current_hp > before;
            clench |= has_message("clenches to protect itself");
            moved |= p->x != px || p->y != py;
            acid |= loc_active(30032);
            bomb |= loc_active(29766);
            burst |= loc_active(30033) || loc_active(30034);
            burn |= p->skills.boosted_level[SKILL_ATTACK] < 99;
            for (int f = 0; f < 20 && !doused; f++) {
                RcNpc *flame = cox_room_npc(g_w, olm_room(), 4 + f);
                if (!flame) continue;
                fire = true;
                // Dousing needs water: a water-filled gourd vial.
                RcNpcId uid = (RcNpcId)flame->uid;
                rc_player_interact_npc(g_w, flame->uid, 0);
                for (int i = 0; i < 4; i++) tick(1);
                assert(rc_npc_resolve(g_w, uid) == NULL
                       || strstr(rc_interaction_last_outcome(p)->message, "need some water"));
                if (!rc_npc_resolve(g_w, uid)) continue;
                assert(rc_player_inventory_add(g_w, 20801, 1, 0).code == RC_ITEM_RESULT_OK);
                rc_player_interact_npc(g_w, flame->uid, 0);
                for (int i = 0; i < 6 && rc_npc_resolve(g_w, uid); i++) tick(1);
                doused = !rc_npc_resolve(g_w, uid) && cox_has_item(p, 20800);
                assert(doused);
            }
        }
        if (phase == 3) {
            // Penultimate: a claw left down for about 30 seconds returns.
            kill(part(3));
            assert(!part(3));
            tick(55);
            RcNpc *mage = part(3);
            recovered = mage && mage->current_hp == mage->spawn_hp;
        }
        assert(part(2) && part(3));
        kill(part(3));
        tick(2);
        // During its Heal the left claw turns damage into health.
        for (int i = 0; i < 40 && part(2); i++) kill(part(2));
        // Sink, shower, rise.
        for (int i = 0; i < 20 && form(part(1)) == 7554; i++) tick(1);
        assert(form(part(1)) == 7551);
        wait_active();
    }
    assert(has_message("fires a sphere"));
    assert(has_message("rises with the power of acid") && acid);
    assert(has_message("smothered you in acid"));
    assert(has_message("rises with the power of flame") && fire && doused && burn);
    assert(has_message("rises with the power of crystal") && bomb);
    assert(has_message("sounds a cry"));
    assert(burst && moved && "crystal burst and the teleport portal");
    assert(clench && recovered && healed);
    // Final stand: off the pools, the life siphon heals the head.
    RcNpc *head = part(1);
    assert(head && !head->player_untargetable);
    rc_queue_hit_meta(head->pending_hits, &head->num_pending_hits, 400, 0, COMBAT_RANGED,
                      RC_HIT_SOURCE_PLAYER, 0, g_w->tick, 0, 400);
    tick(1);
    int low = head->current_hp;
    bool siphoned = false;
    for (int t = 0; t < 300 && !siphoned; t++) {
        tick(1);
        p->current_hp = p->max_hp;
        siphoned = head->current_hp > low;
    }
    assert(siphoned);
    rc_world_destroy(g_w);
}

int main(void) {
    test_olm_fight();
    test_olm_mechanics();
    printf("test_cox_olm: ok\n");
    return 0;
}
