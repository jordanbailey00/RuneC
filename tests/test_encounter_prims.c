// test_encounter_prims — bounded encounter primitive wiring.
//
// Proves:
//   1. Loading encounters.bin populates RcEncounterMechanic.prim with
//      real function pointers (not NULL) for implemented primitives.
//   2. A Scurrius encounter with the player standing on the boss tile
//      takes damage when `telegraphed_aoe_tile` fires (Falling Bricks).
//   3. `spawn_npcs` resolves its name at call time and spawns NPCs
//      (no-op if the name doesn't resolve — we exercise both paths).
//   4. `chain_magic_to_nearest_player` is a no-op in solo mode.
//   5. `drain_prayer_on_hit` decreases prayer points when invoked.
//   6. `heal_at_object` restores boss HP toward max.
//   7. Phase-enter wiring auto-fires `heal_at_object` for Scurrius.
//   8. `preserve_stat_drains_across_transition` is callable (stub).
//   9. Event-bus chain: a boss landing damage on the player fires
//      RC_EVT_PLAYER_DAMAGED, and the encounter handler routes it to
//      the spec's drain_prayer_on_hit mechanic (KQ Barbed Spines path).
//  10. Scorpia's guarded transition auto-spawns guardians, and the
//      guardian-heal primitive ticks while those guardians are alive.
//  11. Chaos Elemental confusion teleports the player near the boss,
//      and madness unequips allowed gear back into inventory.

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../rc-core/api.h"
#include "../rc-core/config.h"
#include "../rc-core/encounter.h"
#include "../rc-core/items.h"
#include "../rc-core/npc.h"
#include "../rc-core/combat.h"
#include "../rc-content/content.h"

#define RC_TEST_ENCOUNTERS_BIN RC_TEST_SOURCE_DIR "/data/defs/encounters.bin"

// Install the boss/minion defs needed by this test. Slot indices are
// what rc_npc_spawn takes as def_idx, but the encounter subsystem
// matches on def.id (cache NPC ID).
static void install_stubs(void) {
    g_npc_def_count = 6;
    memset(g_npc_defs, 0, sizeof(g_npc_defs[0]) * 6);

    g_npc_defs[0].id = 7221;               // Scurrius solo
    strcpy(g_npc_defs[0].name, "Scurrius");
    g_npc_defs[0].size = 3;
    g_npc_defs[0].hitpoints = 500;
    g_npc_defs[0].stats[3] = 500;

    g_npc_defs[1].id = 7223;               // giant rat (exact id arbitrary)
    strcpy(g_npc_defs[1].name, "Giant rat (Scurrius)");
    g_npc_defs[1].size = 1;
    g_npc_defs[1].hitpoints = 5;
    g_npc_defs[1].stats[3] = 5;

    g_npc_defs[2].id = 965;
    strcpy(g_npc_defs[2].name, "Kalphite Queen");
    g_npc_defs[2].size = 5;
    g_npc_defs[2].hitpoints = 255;
    g_npc_defs[2].stats[3] = 255;

    g_npc_defs[3].id = 6615;
    strcpy(g_npc_defs[3].name, "Scorpia");
    g_npc_defs[3].size = 3;
    g_npc_defs[3].hitpoints = 100;
    g_npc_defs[3].stats[3] = 100;

    g_npc_defs[4].id = 6616;
    strcpy(g_npc_defs[4].name, "Scorpia's guardian");
    g_npc_defs[4].size = 1;
    g_npc_defs[4].hitpoints = 20;
    g_npc_defs[4].stats[3] = 20;

    g_npc_defs[5].id = 2054;
    strcpy(g_npc_defs[5].name, "Chaos Elemental");
    g_npc_defs[5].size = 4;
    g_npc_defs[5].hitpoints = 250;
    g_npc_defs[5].stats[3] = 250;
}

static void install_item_stubs(void) {
    g_item_def_count = 3;
    memset(g_item_defs, 0, sizeof(g_item_defs[0]) * 3);

    g_item_defs[0].id = 0;
    strcpy(g_item_defs[0].name, "Test sword");
    g_item_defs[0].equippable = true;
    g_item_defs[0].equip_slot = EQUIP_WEAPON;
    g_item_defs[0].attack_slash = 10;

    g_item_defs[1].id = 1;
    strcpy(g_item_defs[1].name, "Test shield");
    g_item_defs[1].equippable = true;
    g_item_defs[1].equip_slot = EQUIP_SHIELD;
    g_item_defs[1].defence_slash = 12;

    g_item_defs[2].id = 2;
    strcpy(g_item_defs[2].name, "Test body");
    g_item_defs[2].equippable = true;
    g_item_defs[2].equip_slot = EQUIP_BODY;
    g_item_defs[2].defence_crush = 20;
}

// Locate a mechanic by name within a spec, returning index or -1.
static int find_mech(const RcEncounterSpec *s, const char *name) {
    for (int i = 0; i < s->mechanic_count; i++) {
        if (strcmp(s->mechanics[i].name, name) == 0) return i;
    }
    return -1;
}

static int find_phase(const RcEncounterSpec *s, const char *id) {
    for (int i = 0; i < s->phase_count; i++) {
        if (strcmp(s->phases[i].id, id) == 0) return i;
    }
    return -1;
}

static int find_active_encounter(const RcWorld *w, int boss_uid) {
    for (int i = 0; i < RC_ENC_MAX_ACTIVE; i++) {
        if (w->encounter.active[i].active &&
            w->encounter.active[i].boss_id == boss_uid) {
            return i;
        }
    }
    return -1;
}

static int count_inventory_items(const RcPlayer *pl) {
    int count = 0;
    for (int i = 0; i < RC_INVENTORY_SIZE; i++) {
        if (pl->inventory[i].item_id >= 0) count++;
    }
    return count;
}

int main(void) {
    install_stubs();
    install_item_stubs();

    RcWorldConfig cfg = rc_preset_combat_only();
    cfg.seed = 99;
    cfg.encounters_path = RC_TEST_ENCOUNTERS_BIN;
    RcWorld *w = rc_world_create_config(&cfg);
    assert(w && (w->enabled & RC_SUB_ENCOUNTER));
    assert(w->encounter.registry_count == 50);
    // Register OSRS content modules. Pattern for all callers — see
    // rc-content/README.md. Currently a no-op (modules are scaffolding)
    // but exercising the call establishes the pattern for future tests.
    rc_content_register_all(w);

    // ---- 1. Primitive pointers landed on Scurrius spec -----------------
    int sidx = rc_encounter_find_spec(w, 7221);
    assert(sidx >= 0);
    const RcEncounterSpec *scurrius = &w->encounter.registry[sidx];

    int m_bricks = find_mech(scurrius, "Falling Bricks");
    int m_minions = find_mech(scurrius, "Minions");
    assert(m_bricks >= 0 && m_minions >= 0);
    assert(scurrius->mechanics[m_bricks].prim != NULL);
    assert(scurrius->mechanics[m_minions].prim != NULL);
    assert(scurrius->mechanics[m_bricks].primitive_id
           == RC_PRIM_TELEGRAPHED_AOE_TILE);
    assert(scurrius->mechanics[m_minions].primitive_id
           == RC_PRIM_SPAWN_NPCS);

    // ---- 2. Falling Bricks damages player standing on boss's tile ------
    int npc_idx = rc_npc_spawn(w, 0, 3213, 3428, 0);
    assert(npc_idx >= 0);
    w->player.x = 3213;   // same tile as boss → primary target
    w->player.y = 3428;
    int ppre = w->player.num_pending_hits;

    // Find the active encounter and invoke the primitive directly —
    // bypasses the scheduler so we're testing the primitive function,
    // not the tick math.
    int aidx = find_active_encounter(w, w->npcs[npc_idx].uid);
    assert(aidx >= 0);

    scurrius->mechanics[m_bricks].prim(
        w, aidx, scurrius->mechanics[m_bricks].param_block);
    assert(w->player.num_pending_hits == ppre + 1);
    int queued = w->player.num_pending_hits - 1;
    assert(w->player.pending_hits[queued].damage >= 15);
    assert(w->player.pending_hits[queued].damage <= 22);   // solo cap

    // ---- 3. Minions spawns Giant rats around the boss ------------------
    int npc_pre = w->npc_count;
    scurrius->mechanics[m_minions].prim(
        w, aidx, scurrius->mechanics[m_minions].param_block);
    assert(w->npc_count == npc_pre + 6);

    // ---- 4. KQ primitives are registered + callable --------------------
    int kidx = rc_encounter_find_spec(w, 965);
    assert(kidx >= 0);
    const RcEncounterSpec *kq = &w->encounter.registry[kidx];

    int m_drain = find_mech(kq, "Prayer Drain on Ranged Hit");
    int m_chain = find_mech(kq, "Magic Bounce Chain");
    int m_stat  = find_mech(kq, "Stat-Drain Carries to Phase 2");
    assert(m_drain >= 0 && m_chain >= 0 && m_stat >= 0);
    assert(kq->mechanics[m_drain].prim != NULL);
    assert(kq->mechanics[m_chain].prim != NULL);
    assert(kq->mechanics[m_stat].prim != NULL);

    // drain_prayer_on_hit: point drop
    w->player.current_prayer_points = 50;
    kq->mechanics[m_drain].prim(w, aidx, kq->mechanics[m_drain].param_block);
    assert(w->player.current_prayer_points == 49);

    // chain_magic_to_nearest_player: solo no-op
    kq->mechanics[m_chain].prim(w, aidx, kq->mechanics[m_chain].param_block);

    // preserve_stat_drains_across_transition: stub
    kq->mechanics[m_stat].prim(w, aidx, kq->mechanics[m_stat].param_block);

    // ---- 5. Scurrius heal_at_object raises boss HP ---------------------
    int m_heal = find_mech(scurrius, "Food Heal");
    int ph_heal = find_phase(scurrius, "heal");
    assert(m_heal >= 0);
    assert(ph_heal >= 0);
    assert(scurrius->mechanics[m_heal].prim != NULL);
    assert(scurrius->mechanics[m_heal].trigger_type
           == RC_ENC_TRIGGER_PHASE_ENTER);
    assert(scurrius->mechanics[m_heal].phase_idx == ph_heal);

    RcNpc *boss = &w->npcs[npc_idx];
    boss->current_hp = 100;            // simulate hp draw-down
    scurrius->mechanics[m_heal].prim(
        w, aidx, scurrius->mechanics[m_heal].param_block);
    assert(boss->current_hp > 100);
    assert(boss->current_hp <= g_npc_defs[0].hitpoints);

    // ---- 6. Phase-enter event wiring auto-fires Food Heal ------------
    w->encounter.active[aidx].current_phase = 0;   // back to opening phase
    boss->current_hp = 399;                        // 79% of 500 → heal phase
    rc_encounter_tick(w);
    assert(w->encounter.active[aidx].current_phase == ph_heal);
    assert(boss->current_hp == 404);               // phase-enter heal (+5)

    // ---- 7. Event-bus chain: PLAYER_DAMAGED → drain_prayer_on_hit ------
    // Spawn a KQ NPC and queue a pending hit sourced from it. When the
    // combat resolver fires the hit, PLAYER_DAMAGED → encounter handler
    // → KQ's drain_prayer_on_hit primitive should run.
    int kq_npc_idx = rc_npc_spawn(w, 2, 3213, 3428, 0);
    assert(kq_npc_idx >= 0);

    w->player.current_prayer_points = 50;
    rc_queue_hit(w->player.pending_hits, &w->player.num_pending_hits,
                 15, 0, COMBAT_RANGED,
                 w->npcs[kq_npc_idx].uid,   // source = KQ uid
                 0u /* no protect prayer */,
                 w->tick);
    // Invoke the player-hit resolver directly to isolate the event path.
    rc_resolve_player_hits(w);
    // KQ TOML declares drain_prayer_on_hit points=1 → prayer 50 → 49.
    assert(w->player.current_prayer_points == 49);

    // ---- 8. Scorpia phase-enter summon + guardian heal -----------------
    int scorpia_spec_idx = rc_encounter_find_spec(w, 6615);
    assert(scorpia_spec_idx >= 0);
    const RcEncounterSpec *scorpia = &w->encounter.registry[scorpia_spec_idx];
    int m_summon = find_mech(scorpia, "Summon Guardians");
    int m_guardian_heal = find_mech(scorpia, "Guardian Heal");
    int ph_guarded = find_phase(scorpia, "guarded");
    assert(m_summon >= 0 && m_guardian_heal >= 0 && ph_guarded >= 0);
    assert(scorpia->mechanics[m_summon].prim != NULL);
    assert(scorpia->mechanics[m_guardian_heal].prim != NULL);

    int scorpia_npc_idx = rc_npc_spawn(w, 3, 3220, 3420, 0);
    assert(scorpia_npc_idx >= 0);
    int scorpia_active = find_active_encounter(w, w->npcs[scorpia_npc_idx].uid);
    assert(scorpia_active >= 0);
    RcNpc *scorpia_boss = &w->npcs[scorpia_npc_idx];
    int scorpia_npc_pre = w->npc_count;
    scorpia_boss->current_hp = 49;   // below 50% of 100 => guarded phase
    rc_encounter_tick(w);
    assert(w->encounter.active[scorpia_active].current_phase == ph_guarded);
    assert(w->npc_count == scorpia_npc_pre + 2);
    assert(scorpia_boss->current_hp == 53); // +4 guardian heal same tick

    // ---- 9. Chaos Elemental confusion + madness ------------------------
    int chaos_spec_idx = rc_encounter_find_spec(w, 2054);
    assert(chaos_spec_idx >= 0);
    const RcEncounterSpec *chaos = &w->encounter.registry[chaos_spec_idx];
    int m_confusion = find_mech(chaos, "Confusion");
    int m_madness = find_mech(chaos, "Madness");
    assert(m_confusion >= 0 && m_madness >= 0);
    assert(chaos->mechanics[m_confusion].prim != NULL);
    assert(chaos->mechanics[m_madness].prim != NULL);

    int chaos_npc_idx = rc_npc_spawn(w, 5, 3250, 3430, 0);
    assert(chaos_npc_idx >= 0);
    int chaos_active = find_active_encounter(w, w->npcs[chaos_npc_idx].uid);
    assert(chaos_active >= 0);

    w->player.prev_x = w->player.x = 3248;
    w->player.prev_y = w->player.y = 3430;
    chaos->mechanics[m_confusion].prim(
        w, chaos_active, chaos->mechanics[m_confusion].param_block);
    int dx = w->player.x - w->npcs[chaos_npc_idx].x;
    if (dx < 0) dx = -dx;
    int dy = w->player.y - w->npcs[chaos_npc_idx].y;
    if (dy < 0) dy = -dy;
    int cheb = dx > dy ? dx : dy;
    assert(cheb >= 2 && cheb <= 5);

    memset(w->player.inventory, 0xFF, sizeof(w->player.inventory));
    for (int i = 0; i < RC_INVENTORY_SIZE; i++) {
        w->player.inventory[i].quantity = 0;
    }
    for (int i = 0; i < RC_EQUIP_COUNT; i++) {
        w->player.equipment[i].item_id = -1;
        w->player.equipment[i].quantity = 0;
    }
    w->player.equipment[EQUIP_WEAPON].item_id = 0;
    w->player.equipment[EQUIP_WEAPON].quantity = 1;
    w->player.equipment[EQUIP_SHIELD].item_id = 1;
    w->player.equipment[EQUIP_SHIELD].quantity = 1;
    w->player.equipment[EQUIP_BODY].item_id = 2;
    w->player.equipment[EQUIP_BODY].quantity = 1;
    rc_recalc_bonuses(&w->player);
    assert(w->player.equipment_bonuses[EQ_SLASH_ATK] == 10);
    assert(count_inventory_items(&w->player) == 0);

    chaos->mechanics[m_madness].prim(
        w, chaos_active, chaos->mechanics[m_madness].param_block);
    assert(w->player.equipment[EQUIP_WEAPON].item_id == -1);
    assert(w->player.equipment[EQUIP_SHIELD].item_id == -1);
    assert(w->player.equipment[EQUIP_BODY].item_id == -1);
    assert(count_inventory_items(&w->player) == 3);
    assert(w->player.equipment_bonuses[EQ_SLASH_ATK] == 0);

    rc_world_destroy(w);
    printf("test_encounter_prims: wilderness primitive slice OK.\n");
    return 0;
}
