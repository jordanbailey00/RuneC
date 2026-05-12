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
//  10. Encounter attack tables select ranged/magic attacks by phase
//      range rules, and data-backed protection rows can override
//      default full-block prayers.
//  11. Giant Mole's incoming-hit burrow primitive fires from
//      RC_EVT_NPC_DAMAGED.
//  12. GWD owner-specific attack rows and group kill/respawn work.
//  13. Corp stomp queues prayer-ignoring positional damage.
//  14. Kraken phase-enter minions gate the surfaced phase.
//  15. Corp dark-core, K'ril attack-counter, Cerberus triple,
//      DKS owner attack rows, and generic damage modifiers are
//      runtime-wired.
//  16. Scorpia's guarded transition auto-spawns guardians, and the
//      guardian-heal primitive ticks while those guardians are alive.
//  17. Chaos Elemental confusion teleports the player near the boss,
//      and madness unequips allowed gear back into inventory.
//  18. Zulrah/Vorkath first runtime slice: form dive, damage cap,
//      alternating specials, acid pools, venom, and prayer-off effects.

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../rc-core/api.h"
#include "../rc-core/config.h"
#include "../rc-core/encounter.h"
#include "../rc-core/events.h"
#include "../rc-core/items.h"
#include "../rc-core/npc.h"
#include "../rc-core/combat.h"
#include "../rc-core/objects.h"
#include "../rc-core/prayer.h"
#include "../rc-content/content.h"

#define RC_TEST_ENCOUNTERS_BIN RC_TEST_SOURCE_DIR "/data/defs/encounters.bin"

// Install the boss/minion defs needed by this test. Slot indices are
// what rc_npc_spawn takes as def_idx, but the encounter subsystem
// matches on def.id (cache NPC ID).
static void install_stubs(void) {
    g_npc_def_count = 29;
    memset(g_npc_defs, 0, sizeof(g_npc_defs[0]) * 29);

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

    g_npc_defs[6].id = 7416;
    strcpy(g_npc_defs[6].name, "Obor");
    g_npc_defs[6].size = 3;
    g_npc_defs[6].hitpoints = 120;
    g_npc_defs[6].stats[0] = 90;
    g_npc_defs[6].stats[3] = 120;
    g_npc_defs[6].stats[4] = 90;
    g_npc_defs[6].attack_types = 0x14; // crush + ranged
    g_npc_defs[6].max_hit = 21;
    g_npc_defs[6].attack_speed = 4;

    g_npc_defs[7].id = 8195;
    strcpy(g_npc_defs[7].name, "Bryophyta");
    g_npc_defs[7].size = 3;
    g_npc_defs[7].hitpoints = 115;
    g_npc_defs[7].stats[0] = 90;
    g_npc_defs[7].stats[3] = 115;
    g_npc_defs[7].stats[5] = 90;
    g_npc_defs[7].attack_types = 0x0C; // crush + magic
    g_npc_defs[7].max_hit = 16;
    g_npc_defs[7].attack_speed = 4;

    g_npc_defs[8].id = 5779;
    strcpy(g_npc_defs[8].name, "Giant Mole");
    g_npc_defs[8].size = 3;
    g_npc_defs[8].hitpoints = 120;
    g_npc_defs[8].stats[0] = 90;
    g_npc_defs[8].stats[3] = 120;
    g_npc_defs[8].attack_types = 0x02; // slash
    g_npc_defs[8].max_hit = 21;
    g_npc_defs[8].attack_speed = 4;

    g_npc_defs[9].id = 2215;
    strcpy(g_npc_defs[9].name, "General Graardor");
    g_npc_defs[9].size = 4;
    g_npc_defs[9].hitpoints = 255;
    g_npc_defs[9].stats[0] = 280;
    g_npc_defs[9].stats[3] = 255;
    g_npc_defs[9].stats[4] = 280;
    g_npc_defs[9].attack_types = 0x14;
    g_npc_defs[9].max_hit = 60;
    g_npc_defs[9].attack_speed = 6;

    g_npc_defs[10].id = 2217;
    strcpy(g_npc_defs[10].name, "Sergeant Steelwill");
    g_npc_defs[10].size = 1;
    g_npc_defs[10].hitpoints = 127;
    g_npc_defs[10].stats[3] = 127;
    g_npc_defs[10].stats[5] = 100;
    g_npc_defs[10].attack_types = 0x08;
    g_npc_defs[10].max_hit = 20;
    g_npc_defs[10].attack_speed = 5;

    g_npc_defs[11].id = 319;
    strcpy(g_npc_defs[11].name, "Corporeal Beast");
    g_npc_defs[11].size = 5;
    g_npc_defs[11].hitpoints = 2000;
    g_npc_defs[11].stats[0] = 320;
    g_npc_defs[11].stats[3] = 2000;
    g_npc_defs[11].stats[5] = 320;
    g_npc_defs[11].attack_types = 0x0A;
    g_npc_defs[11].max_hit = 65;
    g_npc_defs[11].attack_speed = 4;

    g_npc_defs[12].id = 494;
    strcpy(g_npc_defs[12].name, "Kraken");
    g_npc_defs[12].size = 5;
    g_npc_defs[12].hitpoints = 255;
    g_npc_defs[12].stats[3] = 255;
    g_npc_defs[12].stats[4] = 255;
    g_npc_defs[12].attack_types = 0x10;
    g_npc_defs[12].max_hit = 28;
    g_npc_defs[12].attack_speed = 4;

    g_npc_defs[13].id = 496;
    strcpy(g_npc_defs[13].name, "Whirlpool");
    g_npc_defs[13].size = 5;
    g_npc_defs[13].hitpoints = 255;
    g_npc_defs[13].stats[3] = 255;

    g_npc_defs[14].id = 5535;
    strcpy(g_npc_defs[14].name, "Enormous Tentacle");
    g_npc_defs[14].size = 1;
    g_npc_defs[14].hitpoints = 1;
    g_npc_defs[14].stats[3] = 1;
    g_npc_defs[14].stats[4] = 1;
    g_npc_defs[14].attack_types = 0x10;
    g_npc_defs[14].max_hit = 2;
    g_npc_defs[14].attack_speed = 4;

    g_npc_defs[15].id = 388;
    strcpy(g_npc_defs[15].name, "Dark energy core");
    g_npc_defs[15].size = 1;
    g_npc_defs[15].hitpoints = 25;
    g_npc_defs[15].stats[3] = 25;

    g_npc_defs[16].id = 3129;
    strcpy(g_npc_defs[16].name, "K'ril Tsutsaroth");
    g_npc_defs[16].size = 5;
    g_npc_defs[16].hitpoints = 255;
    g_npc_defs[16].stats[3] = 255;
    g_npc_defs[16].stats[5] = 200;
    g_npc_defs[16].attack_types = 0x0A;
    g_npc_defs[16].max_hit = 46;
    g_npc_defs[16].attack_speed = 4;

    g_npc_defs[17].id = 5862;
    strcpy(g_npc_defs[17].name, "Cerberus");
    g_npc_defs[17].size = 5;
    g_npc_defs[17].hitpoints = 600;
    g_npc_defs[17].stats[3] = 600;
    g_npc_defs[17].attack_types = 0x1A;
    g_npc_defs[17].max_hit = 23;
    g_npc_defs[17].attack_speed = 4;

    g_npc_defs[18].id = 2883;
    strcpy(g_npc_defs[18].name, "Dagannoth Rex");
    g_npc_defs[18].size = 3;
    g_npc_defs[18].hitpoints = 255;
    g_npc_defs[18].stats[3] = 255;
    g_npc_defs[18].attack_types = 0x04;
    g_npc_defs[18].max_hit = 26;
    g_npc_defs[18].attack_speed = 4;

    g_npc_defs[19].id = 2882;
    strcpy(g_npc_defs[19].name, "Dagannoth Supreme");
    g_npc_defs[19].size = 3;
    g_npc_defs[19].hitpoints = 255;
    g_npc_defs[19].stats[3] = 255;
    g_npc_defs[19].attack_types = 0x10;
    g_npc_defs[19].max_hit = 26;
    g_npc_defs[19].attack_speed = 4;

    g_npc_defs[20].id = 2881;
    strcpy(g_npc_defs[20].name, "Dagannoth Prime");
    g_npc_defs[20].size = 3;
    g_npc_defs[20].hitpoints = 255;
    g_npc_defs[20].stats[3] = 255;
    g_npc_defs[20].attack_types = 0x08;
    g_npc_defs[20].max_hit = 26;
    g_npc_defs[20].attack_speed = 4;

    g_npc_defs[21].id = 3162;
    strcpy(g_npc_defs[21].name, "Kree'arra");
    g_npc_defs[21].size = 5;
    g_npc_defs[21].hitpoints = 255;
    g_npc_defs[21].stats[3] = 255;
    g_npc_defs[21].attack_types = 0x10;
    g_npc_defs[21].max_hit = 21;
    g_npc_defs[21].attack_speed = 4;

    g_npc_defs[22].id = 8061;
    strcpy(g_npc_defs[22].name, "Vorkath");
    g_npc_defs[22].size = 7;
    g_npc_defs[22].hitpoints = 750;
    g_npc_defs[22].stats[3] = 750;
    g_npc_defs[22].attack_types = 0x1C;
    g_npc_defs[22].max_hit = 32;
    g_npc_defs[22].attack_speed = 4;

    g_npc_defs[23].id = 8062;
    strcpy(g_npc_defs[23].name, "Zombified Spawn");
    g_npc_defs[23].size = 1;
    g_npc_defs[23].hitpoints = 38;
    g_npc_defs[23].stats[3] = 38;

    g_npc_defs[24].id = 2042;
    strcpy(g_npc_defs[24].name, "Zulrah");
    g_npc_defs[24].size = 5;
    g_npc_defs[24].hitpoints = 500;
    g_npc_defs[24].stats[3] = 500;
    g_npc_defs[24].attack_types = 0x18;
    g_npc_defs[24].max_hit = 41;
    g_npc_defs[24].attack_speed = 4;

    g_npc_defs[25] = g_npc_defs[24];
    g_npc_defs[25].id = 2043;
    strcpy(g_npc_defs[25].name, "Zulrah (magma)");
    g_npc_defs[26] = g_npc_defs[24];
    g_npc_defs[26].id = 2044;
    strcpy(g_npc_defs[26].name, "Zulrah (tanzanite)");
    g_npc_defs[27] = g_npc_defs[24];
    g_npc_defs[27].id = 2045;
    strcpy(g_npc_defs[27].name, "Zulrah (jad)");

    g_npc_defs[28].id = 2046;
    strcpy(g_npc_defs[28].name, "Snakeling");
    g_npc_defs[28].size = 1;
    g_npc_defs[28].hitpoints = 1;
    g_npc_defs[28].stats[3] = 1;
}

static void install_item_stubs(void) {
    g_item_def_count = 6;
    memset(g_item_defs, 0, sizeof(g_item_defs[0]) * 6);

    g_item_defs[0].id = 0;
    strcpy(g_item_defs[0].name, "Test sword");
    g_item_defs[0].equippable = true;
    g_item_defs[0].equip_slot = EQUIP_WEAPON;
    g_item_defs[0].attack_slash = 10;
    g_item_defs[0].weapon_type = 18;

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

    g_item_defs[3].id = 3;
    strcpy(g_item_defs[3].name, "Zamorakian spear");
    g_item_defs[3].equippable = true;
    g_item_defs[3].equip_slot = EQUIP_WEAPON;
    g_item_defs[3].attack_stab = 20;
    g_item_defs[3].weapon_type = 19;

    g_item_defs[4].id = 4;
    strcpy(g_item_defs[4].name, "Rune halberd");
    g_item_defs[4].equippable = true;
    g_item_defs[4].equip_slot = EQUIP_WEAPON;
    g_item_defs[4].attack_slash = 15;

    g_item_defs[5].id = 5;
    strcpy(g_item_defs[5].name, "Black salamander");
    g_item_defs[5].equippable = true;
    g_item_defs[5].equip_slot = EQUIP_WEAPON;
    g_item_defs[5].weapon_type = 26;
}

// Locate a mechanic by name within a spec, returning index or -1.
static int find_mech(const RcEncounterSpec *s, const char *name) {
    for (int i = 0; i < s->mechanic_count; i++) {
        if (strcmp(s->mechanics[i].name, name) == 0) return i;
    }
    return -1;
}

static int find_attack(const RcEncounterSpec *s, const char *name) {
    for (int i = 0; i < s->attack_count; i++) {
        if (strcmp(s->attacks[i].name, name) == 0) return i;
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

static int count_live_npcs_by_cache_id(const RcWorld *w, int npc_id) {
    int count = 0;
    for (int i = 0; i < w->npc_count; i++) {
        if (!w->npcs[i].active || w->npcs[i].is_dead) continue;
        if (g_npc_defs[w->npcs[i].def_id].id == npc_id) count++;
    }
    return count;
}

static int count_targetable_npcs_by_cache_id(const RcWorld *w, int npc_id) {
    int count = 0;
    for (int i = 0; i < w->npc_count; i++) {
        if (!w->npcs[i].active || w->npcs[i].is_dead) continue;
        if (w->npcs[i].player_untargetable) continue;
        if (g_npc_defs[w->npcs[i].def_id].id == npc_id) count++;
    }
    return count;
}

static int count_effects(const RcWorld *w, int kind) {
    int count = 0;
    for (int i = 0; i < w->encounter_effect_count; i++) {
        if (w->encounter_effects[i].active &&
                w->encounter_effects[i].kind == kind) {
            count++;
        }
    }
    return count;
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

    // ---- 8. Encounter attack table + protection override --------------
    int obor_spec_idx = rc_encounter_find_spec(w, 7416);
    int bryo_spec_idx = rc_encounter_find_spec(w, 8195);
    assert(obor_spec_idx >= 0 && bryo_spec_idx >= 0);
    const RcEncounterSpec *obor = &w->encounter.registry[obor_spec_idx];
    const RcEncounterSpec *bryo = &w->encounter.registry[bryo_spec_idx];
    assert(obor->protection_count == 1);

    int obor_npc_idx = rc_npc_spawn(w, 6, 3250, 3430, 0);
    int bryo_npc_idx = rc_npc_spawn(w, 7, 3260, 3430, 0);
    assert(obor_npc_idx >= 0 && bryo_npc_idx >= 0);
    uint8_t enc_style = 0;
    uint16_t enc_min = 0;
    uint16_t enc_max = 0;
    uint32_t enc_flags = 0;
    assert(rc_encounter_select_npc_attack(
               w, w->npcs[obor_npc_idx].uid, 4, &enc_style, &enc_min,
               &enc_max, &enc_flags) >= 0);
    assert(enc_style == COMBAT_RANGED && enc_min == 0 && enc_max == 21);
    assert(rc_encounter_select_npc_attack(
               w, w->npcs[bryo_npc_idx].uid, 4, &enc_style, &enc_min,
               &enc_max, &enc_flags) >= 0);
    assert(enc_style == COMBAT_MAGIC && enc_max == 16);

    w->player.current_hp = 990;
    w->player.active_prayers = PRAYER_PROTECT_RANGE;
    w->player.num_pending_hits = 0;
    rc_queue_hit(w->player.pending_hits, &w->player.num_pending_hits,
                 20, 0, COMBAT_RANGED, w->npcs[obor_npc_idx].uid,
                 w->player.active_prayers, w->tick);
    rc_resolve_player_hits(w);
    assert(w->player.current_hp == 890); // Obor ranged prayer scales 20 → 10.

    int mole_spec_idx = rc_encounter_find_spec(w, 5779);
    assert(mole_spec_idx >= 0);
    const RcEncounterSpec *mole = &w->encounter.registry[mole_spec_idx];
    int m_burrow = find_mech(mole, "Burrow Escape");
    assert(m_burrow >= 0);
    assert(mole->mechanics[m_burrow].prim != NULL);
    assert(mole->mechanics[m_burrow].trigger_type == RC_ENC_TRIGGER_NONE);

    int mole_npc_idx = rc_npc_spawn(w, 8, 3000, 3400, 0);
    assert(mole_npc_idx >= 0);
    RcNpc *mole_boss = &w->npcs[mole_npc_idx];
    mole_boss->current_hp = 60; // 50% after no further damage.
    mole_boss->target_uid = 0;
    int mole_x = mole_boss->x;
    int mole_y = mole_boss->y;
    RcPayloadNpcDamaged mole_hit = {
        .npc_id = (uint16_t)mole_boss->uid,
        .source_npc_id = 0xFFFFu,
        .damage = 1,
        .style = COMBAT_MELEE_SLASH,
    };
    for (int i = 0; i < 64 && mole_boss->x == mole_x &&
            mole_boss->y == mole_y; i++) {
        rc_event_fire(w, RC_EVT_NPC_DAMAGED, &mole_hit);
    }
    assert(mole_boss->x != mole_x || mole_boss->y != mole_y);
    assert(mole_boss->target_uid == -1);

    // ---- 9. Multi-boss owner attacks + group kill/respawn -------------
    int graardor_spec_idx = rc_encounter_find_spec(w, 2215);
    assert(graardor_spec_idx >= 0);
    const RcEncounterSpec *graardor =
        &w->encounter.registry[graardor_spec_idx];
    int gra_group = find_mech(graardor, "Group Kill Required");
    assert(gra_group >= 0);
    const RcPrimParamsGroupKillRequired *gra_group_params =
        (const RcPrimParamsGroupKillRequired *)
        graardor->mechanics[gra_group].param_block;
    assert(gra_group_params->drop_count == 1);
    assert(gra_group_params->drop_npc_ids[0] == 2215);
    int graardor_npc_idx = rc_npc_spawn(w, 9, 3300, 3400, 0);
    int steelwill_npc_idx = rc_npc_spawn(w, 10, 3304, 3400, 0);
    assert(graardor_npc_idx >= 0 && steelwill_npc_idx >= 0);

    int gra_attack = rc_encounter_select_npc_attack(
        w, w->npcs[graardor_npc_idx].uid, 1, &enc_style, &enc_min,
        &enc_max, &enc_flags);
    assert(gra_attack >= 0);
    assert(graardor->attacks[gra_attack].npc_id == 2215);
    int steel_attack = rc_encounter_select_npc_attack(
        w, w->npcs[steelwill_npc_idx].uid, 4, &enc_style, &enc_min,
        &enc_max, &enc_flags);
    assert(steel_attack >= 0);
    assert(graardor->attacks[steel_attack].npc_id == 2217);
    assert(enc_style == COMBAT_MAGIC && enc_max == 20);

    int finished_before = w->encounter.finished_count;
    w->npcs[graardor_npc_idx].is_dead = true;
    RcPayloadNpcEvent gra_dead = {
        .npc_id = (uint16_t)w->npcs[graardor_npc_idx].uid,
        .def_id = 2215,
    };
    rc_event_fire(w, RC_EVT_NPC_DIED, &gra_dead);
    assert(w->encounter.finished_count == finished_before);
    assert(w->npcs[graardor_npc_idx].respawn_timer == 75);

    w->npcs[steelwill_npc_idx].is_dead = true;
    RcPayloadNpcEvent steel_dead = {
        .npc_id = (uint16_t)w->npcs[steelwill_npc_idx].uid,
        .def_id = 2217,
    };
    rc_event_fire(w, RC_EVT_NPC_DIED, &steel_dead);
    assert(w->encounter.finished_count == finished_before + 1);

    // ---- 10. Corp stomp primitive queues prayer-ignoring damage --------
    int corp_spec_idx = rc_encounter_find_spec(w, 319);
    assert(corp_spec_idx >= 0);
    const RcEncounterSpec *corp = &w->encounter.registry[corp_spec_idx];
    int m_stomp = find_mech(corp, "Stomp");
    assert(m_stomp >= 0 && corp->mechanics[m_stomp].prim != NULL);
    int corp_npc_idx = rc_npc_spawn(w, 11, 3320, 3400, 0);
    int corp_active = find_active_encounter(w, w->npcs[corp_npc_idx].uid);
    assert(corp_npc_idx >= 0 && corp_active >= 0);
    w->player.x = 3321;
    w->player.y = 3401;
    w->player.plane = 0;
    w->player.active_prayers = PRAYER_PROTECT_MELEE;
    w->player.num_pending_hits = 0;
    corp->mechanics[m_stomp].prim(
        w, corp_active, corp->mechanics[m_stomp].param_block);
    assert(w->player.num_pending_hits == 1);
    assert(w->player.pending_hits[0].damage >= 30);
    assert(w->player.pending_hits[0].damage <= 51);
    assert(w->player.pending_hits[0].prayer_snapshot == 0);

    int corp_drain_attack = find_attack(corp, "Magic Drain-Heal");
    assert(corp_drain_attack >= 0);
    assert(corp->attacks[corp_drain_attack].effect_id
           == RC_ENC_ATTACK_EFFECT_DRAIN_HEAL);
    RcNpc *corp_boss = &w->npcs[corp_npc_idx];
    corp_boss->current_hp = 1000;
    w->player.current_prayer_points = 50;
    w->player.skills.boosted_level[SKILL_MAGIC] = 99;
    w->encounter.active[corp_active].last_attack_idx =
        (uint8_t)corp_drain_attack;
    RcPayloadPlayerDamaged corp_drain_hit = {
        .source_npc_id = (uint16_t)corp_boss->uid,
        .damage = 20,
        .style = COMBAT_MAGIC,
    };
    rc_event_fire(w, RC_EVT_PLAYER_DAMAGED, &corp_drain_hit);
    int magic_drain = 99 - w->player.skills.boosted_level[SKILL_MAGIC];
    int prayer_drain = 50 - w->player.current_prayer_points;
    assert(corp_boss->current_hp == 1010);
    assert(magic_drain + prayer_drain >= 1);
    assert(magic_drain + prayer_drain <= 2);

    int corp_split_attack = find_attack(corp, "Magic AoE Split");
    assert(corp_split_attack >= 0);
    assert(corp->attacks[corp_split_attack].effect_id
           == RC_ENC_ATTACK_EFFECT_SPLIT_PROJECTILES);
    w->player.num_pending_hits = 0;
    w->encounter.active[corp_active].last_attack_idx =
        (uint8_t)corp_split_attack;
    RcPayloadPlayerDamaged corp_split_hit = {
        .source_npc_id = (uint16_t)corp_boss->uid,
        .damage = 20,
        .style = COMBAT_MAGIC,
    };
    rc_event_fire(w, RC_EVT_PLAYER_DAMAGED, &corp_split_hit);
    assert(w->player.num_pending_hits == 6);
    for (int i = 0; i < w->player.num_pending_hits; i++) {
        assert(w->player.pending_hits[i].damage <= 30);
        assert((w->player.pending_hits[i].flags &
                RC_HIT_SUPPRESS_ENCOUNTER_EFFECTS) != 0);
    }

    // ---- 11. Kraken opening phase spawns tentacles and can surface -----
    int kraken_spec_idx = rc_encounter_find_spec(w, 494);
    assert(kraken_spec_idx >= 0);
    const RcEncounterSpec *kraken = &w->encounter.registry[kraken_spec_idx];
    int ph_surfaced = find_phase(kraken, "surfaced");
    assert(ph_surfaced >= 0);
    int pre_kraken_npcs = w->npc_count;
    int tentacles_before = count_live_npcs_by_cache_id(w, 5535);
    int kraken_npc_idx = rc_npc_spawn(w, 12, 2280, 10034, 0);
    assert(kraken_npc_idx >= 0);
    assert(w->npc_count == pre_kraken_npcs + 1);
    int kraken_active = find_active_encounter(w, w->npcs[kraken_npc_idx].uid);
    assert(kraken_active >= 0);
    assert(!rc_encounter_player_can_target_npc(
               w, (uint16_t)w->npcs[kraken_npc_idx].uid));
    assert(rc_encounter_select_npc_attack(
               w, w->npcs[kraken_npc_idx].uid, 4, &enc_style, &enc_min,
               &enc_max, &enc_flags) < 0);
    rc_encounter_tick(w);
    assert(count_live_npcs_by_cache_id(w, 5535) == tentacles_before + 4);
    assert(count_targetable_npcs_by_cache_id(w, 5535) == 0);
    assert(count_effects(w, RC_ENC_EFFECT_HIDDEN_OBJECT) == 4);
    RcEncounterEffect *whirlpool = NULL;
    for (int i = 0; i < w->encounter_effect_count; i++) {
        if (w->encounter_effects[i].active &&
                w->encounter_effects[i].kind == RC_ENC_EFFECT_HIDDEN_OBJECT) {
            whirlpool = &w->encounter_effects[i];
            break;
        }
    }
    assert(whirlpool != NULL);
    assert(rc_encounter_interact_object(w, 0, "Whirlpool",
                                        whirlpool->x, whirlpool->y,
                                        whirlpool->plane, 0) == 1);
    assert(count_targetable_npcs_by_cache_id(w, 5535) == 1);
    assert(rc_encounter_reveal_hidden_npcs(w, "Enormous Tentacle", -1) == 3);
    assert(count_targetable_npcs_by_cache_id(w, 5535) == 4);
    for (int i = 0; i < w->npc_count; i++) {
        if (g_npc_defs[w->npcs[i].def_id].id == 5535) {
            w->npcs[i].is_dead = true;
        }
    }
    rc_encounter_tick(w);
    rc_encounter_tick(w);
    assert(w->encounter.active[kraken_active].current_phase == ph_surfaced);
    assert(rc_encounter_player_can_target_npc(
               w, (uint16_t)w->npcs[kraken_npc_idx].uid));

    // ---- 12. Corp dark core and later-batch attack counters ------------
    int m_core = find_mech(corp, "Dark Energy Core");
    assert(m_core >= 0 && corp->mechanics[m_core].prim != NULL);
    assert(corp->mechanics[m_core].trigger_type == RC_ENC_TRIGGER_EVENT);
    const RcPrimParamsSpawnLeechNpc *core_params =
        (const RcPrimParamsSpawnLeechNpc *)
        corp->mechanics[m_core].param_block;
    assert(core_params->large_hit_threshold == 32);
    assert(core_params->boss_hp_below == 1000);
    assert(core_params->spawn_chance_denominator == 8);
    assert(core_params->poisoned_leech_period_ticks == 10);
    RcPrimParamsSpawnLeechNpc *core_params_mut =
        (RcPrimParamsSpawnLeechNpc *)corp->mechanics[m_core].param_block;
    core_params_mut->spawn_chance_denominator = 1;
    for (int i = 0; i < w->npc_count; i++) {
        if (g_npc_defs[w->npcs[i].def_id].id == 388) {
            w->npcs[i].active = false;
        }
    }
    int core_pre = w->npc_count;
    w->player.x = 3330;
    w->player.y = 3400;
    w->player.current_hp = 990;
    corp_boss->current_hp = 1000;
    RcPayloadNpcDamaged corp_small_hit = {
        .npc_id = (uint16_t)corp_boss->uid,
        .source_npc_id = 0xFFFFu,
        .damage = 31,
        .style = COMBAT_MELEE_STAB,
    };
    rc_event_fire(w, RC_EVT_NPC_DAMAGED, &corp_small_hit);
    assert(w->npc_count == core_pre);
    RcPayloadNpcDamaged corp_large_hit = corp_small_hit;
    corp_large_hit.damage = 32;
    rc_event_fire(w, RC_EVT_NPC_DAMAGED, &corp_large_hit);
    assert(w->npc_count == core_pre + 1);
    assert(g_npc_defs[w->npcs[core_pre].def_id].id == 388);
    corp->mechanics[m_core].prim(
        w, corp_active, corp->mechanics[m_core].param_block);
    assert(w->npc_count == core_pre + 1);
    assert(w->npcs[core_pre].x == w->player.x + 1);
    int hp_before_core_leech = w->player.current_hp;
    int boss_hp_before_core_leech = corp_boss->current_hp;
    corp->mechanics[m_core].prim(
        w, corp_active, corp->mechanics[m_core].param_block);
    assert(w->player.current_hp == hp_before_core_leech - 10);
    assert(corp_boss->current_hp == boss_hp_before_core_leech + 1);
    w->npcs[core_pre].poison_damage = 2;
    hp_before_core_leech = w->player.current_hp;
    boss_hp_before_core_leech = corp_boss->current_hp;
    w->tick = 41;
    corp->mechanics[m_core].prim(
        w, corp_active, corp->mechanics[m_core].param_block);
    assert(w->player.current_hp == hp_before_core_leech);
    assert(corp_boss->current_hp == boss_hp_before_core_leech);
    w->tick = 50;
    corp->mechanics[m_core].prim(
        w, corp_active, corp->mechanics[m_core].param_block);
    assert(w->player.current_hp == hp_before_core_leech - 10);
    assert(corp_boss->current_hp == boss_hp_before_core_leech + 1);
    w->npcs[core_pre].active = false;
    core_pre = w->npc_count;
    corp_boss->current_hp = 999;
    RcPayloadNpcAttack corp_attack = {
        .npc_id = (uint16_t)corp_boss->uid,
        .style = COMBAT_MAGIC,
    };
    rc_event_fire(w, RC_EVT_NPC_ATTACK, &corp_attack);
    assert(w->npc_count == core_pre + 1);
    core_params_mut->spawn_chance_denominator = 8;

    int kril_spec_idx = rc_encounter_find_spec(w, 3129);
    assert(kril_spec_idx >= 0);
    const RcEncounterSpec *kril = &w->encounter.registry[kril_spec_idx];
    int m_kril_special = find_mech(kril, "Prayer-Pierce Special");
    assert(m_kril_special >= 0);
    assert(kril->mechanics[m_kril_special].prim != NULL);
    assert(kril->mechanics[m_kril_special].trigger_type
           == RC_ENC_TRIGGER_ATTACK_COUNT);
    int kril_npc_idx = rc_npc_spawn(w, 16, 3330, 3400, 0);
    int kril_active = find_active_encounter(w, w->npcs[kril_npc_idx].uid);
    assert(kril_npc_idx >= 0 && kril_active >= 0);
    w->encounter.active[kril_active].attack_count = 8;
    w->player.active_prayers = PRAYER_PROTECT_MELEE;
    w->player.current_prayer_points = 50;
    RcPayloadNpcAttack kril_attack = {
        .npc_id = (uint16_t)w->npcs[kril_npc_idx].uid,
        .style = COMBAT_MELEE_SLASH,
    };
    rc_event_fire(w, RC_EVT_NPC_ATTACK, &kril_attack);
    assert(w->player.current_prayer_points == 25);

    int kril_melee = find_attack(kril, "Melee Scimitars");
    int kril_drain = find_attack(kril, "Enraged Melee (Prayer-Pierce)");
    assert(kril_melee >= 0 && kril_drain >= 0);
    w->player.poison_damage = 0;
    w->encounter.active[kril_active].last_attack_idx = (uint8_t)kril_melee;
    RcPayloadPlayerDamaged kril_blocked_hit = {
        .source_npc_id = (uint16_t)w->npcs[kril_npc_idx].uid,
        .damage = 0,
        .style = COMBAT_MELEE_SLASH,
    };
    rc_event_fire(w, RC_EVT_PLAYER_DAMAGED, &kril_blocked_hit);
    assert(w->player.poison_damage == 16);

    w->player.current_prayer_points = 50;
    w->encounter.active[kril_active].last_attack_idx = (uint8_t)kril_drain;
    RcPayloadPlayerDamaged kril_drain_hit = {
        .source_npc_id = (uint16_t)w->npcs[kril_npc_idx].uid,
        .damage = 20,
        .style = COMBAT_MELEE_SLASH,
    };
    rc_event_fire(w, RC_EVT_PLAYER_DAMAGED, &kril_drain_hit);
    assert(w->player.current_prayer_points == 25);

    int cerb_spec_idx = rc_encounter_find_spec(w, 5862);
    assert(cerb_spec_idx >= 0);
    const RcEncounterSpec *cerb = &w->encounter.registry[cerb_spec_idx];
    int m_triple = find_mech(cerb, "Triple Attack");
    int m_souls = find_mech(cerb, "Summoned Souls");
    int m_lava = find_mech(cerb, "Lava Pools");
    int m_fire_line = find_mech(cerb, "Fire Line on Re-entry");
    assert(m_triple >= 0 && m_souls >= 0 && m_lava >= 0);
    assert(m_fire_line >= 0);
    assert(cerb->mechanics[m_triple].prim != NULL);
    assert(cerb->mechanics[m_souls].prim != NULL);
    assert(cerb->mechanics[m_lava].prim != NULL);
    assert(cerb->mechanics[m_fire_line].prim != NULL);
    assert(cerb->mechanics[m_triple].trigger_type
           == RC_ENC_TRIGGER_ATTACK_COUNT);
    assert(cerb->mechanics[m_souls].trigger_type
           == RC_ENC_TRIGGER_ATTACK_COUNT);
    assert(cerb->mechanics[m_lava].trigger_type
           == RC_ENC_TRIGGER_ATTACK_COUNT);

    int cerb_npc_idx = rc_npc_spawn(w, 17, 3340, 3400, 0);
    int cerb_active = find_active_encounter(w, w->npcs[cerb_npc_idx].uid);
    assert(cerb_npc_idx >= 0 && cerb_active >= 0);
    w->player.num_pending_hits = 0;
    RcPayloadNpcAttack cerb_attack = {
        .npc_id = (uint16_t)w->npcs[cerb_npc_idx].uid,
        .style = COMBAT_MAGIC,
    };
    rc_event_fire(w, RC_EVT_NPC_ATTACK, &cerb_attack);
    assert(w->player.num_pending_hits == 3);
    assert(w->player.pending_hits[0].attack_style == COMBAT_MAGIC);
    assert(w->player.pending_hits[1].attack_style == COMBAT_RANGED);
    assert(w->player.pending_hits[2].attack_style == COMBAT_MELEE_CRUSH);
    assert(w->player.pending_hits[0].ticks_remaining == 0);
    assert(w->player.pending_hits[1].ticks_remaining == 2);
    assert(w->player.pending_hits[2].ticks_remaining == 4);
    w->player.num_pending_hits = 0;
    w->player.current_prayer_points = 90;
    w->player.active_prayers = PRAYER_PROTECT_MAGIC;
    int soul_effects_before =
        count_effects(w, RC_ENC_EFFECT_TRAVELLING_SOUL);
    cerb->mechanics[m_souls].prim(
        w, cerb_active, cerb->mechanics[m_souls].param_block);
    assert(count_effects(w, RC_ENC_EFFECT_TRAVELLING_SOUL)
           == soul_effects_before + 3);
    assert(w->player.current_prayer_points == 60);
    assert(w->player.num_pending_hits == 2);
    w->player.num_pending_hits = 0;
    w->player.current_prayer_points = 90;
    w->player.ward_of_arceuus_timer = 100;
    w->player.equipment[EQUIP_SHIELD].item_id = -1;
    cerb->mechanics[m_souls].prim(
        w, cerb_active, cerb->mechanics[m_souls].param_block);
    assert(w->player.current_prayer_points == 70);
    w->player.num_pending_hits = 0;
    w->player.current_prayer_points = 90;
    w->player.ward_of_arceuus_timer = 0;
    w->player.equipment[EQUIP_SHIELD].item_id = 12821;
    cerb->mechanics[m_souls].prim(
        w, cerb_active, cerb->mechanics[m_souls].param_block);
    assert(w->player.current_prayer_points == 75);
    w->player.num_pending_hits = 0;
    w->player.current_prayer_points = 90;
    w->player.ward_of_arceuus_timer = 100;
    cerb->mechanics[m_souls].prim(
        w, cerb_active, cerb->mechanics[m_souls].param_block);
    assert(w->player.current_prayer_points == 80);
    w->player.ward_of_arceuus_timer = 0;
    w->player.equipment[EQUIP_SHIELD].item_id = -1;
    w->player.num_pending_hits = 0;
    int lava_before = count_effects(w, RC_ENC_EFFECT_LAVA_POOL);
    cerb->mechanics[m_lava].prim(
        w, cerb_active, cerb->mechanics[m_lava].param_block);
    assert(count_effects(w, RC_ENC_EFFECT_LAVA_POOL) == lava_before + 3);
    assert(w->player.num_pending_hits == 0);
    rc_encounter_tick_effects(w);
    assert(w->player.num_pending_hits == 1);
    assert(w->player.pending_hits[0].damage == 8);
    w->player.num_pending_hits = 0;
    cerb->mechanics[m_fire_line].prim(
        w, cerb_active, cerb->mechanics[m_fire_line].param_block);
    assert(w->player.num_pending_hits == 1);
    assert(w->player.pending_hits[0].damage == 5);

    int dks_spec_idx = rc_encounter_find_spec(w, 2883);
    assert(dks_spec_idx >= 0);
    const RcEncounterSpec *dks = &w->encounter.registry[dks_spec_idx];
    int dks_group = find_mech(dks, "All Three Must Die");
    assert(dks_group >= 0);
    const RcPrimParamsGroupKillRequired *dks_group_params =
        (const RcPrimParamsGroupKillRequired *)
        dks->mechanics[dks_group].param_block;
    assert(dks_group_params->drop_count == 3);
    assert(dks_group_params->drop_npc_ids[0] == 2881);
    assert(dks_group_params->drop_npc_ids[1] == 2882);
    assert(dks_group_params->drop_npc_ids[2] == 2883);
    int rex_idx = rc_npc_spawn(w, 18, 3350, 3400, 0);
    int supreme_idx = rc_npc_spawn(w, 19, 3353, 3400, 0);
    int prime_idx = rc_npc_spawn(w, 20, 3356, 3400, 0);
    assert(rex_idx >= 0 && supreme_idx >= 0 && prime_idx >= 0);
    int rex_attack = rc_encounter_select_npc_attack(
        w, w->npcs[rex_idx].uid, 1, &enc_style, &enc_min, &enc_max,
        &enc_flags);
    int supreme_attack = rc_encounter_select_npc_attack(
        w, w->npcs[supreme_idx].uid, 4, &enc_style, &enc_min, &enc_max,
        &enc_flags);
    int prime_attack = rc_encounter_select_npc_attack(
        w, w->npcs[prime_idx].uid, 4, &enc_style, &enc_min, &enc_max,
        &enc_flags);
    assert(rex_attack >= 0 && dks->attacks[rex_attack].npc_id == 2883);
    assert(supreme_attack >= 0 &&
           dks->attacks[supreme_attack].npc_id == 2882);
    assert(prime_attack >= 0 && dks->attacks[prime_attack].npc_id == 2881);
    assert(dks->damage_mod_count == 3);
    assert(rc_encounter_scale_player_damage(
               w, w->npcs[rex_idx].uid, COMBAT_MAGIC, 10) == 10);
    assert(rc_encounter_scale_player_damage(
               w, w->npcs[rex_idx].uid, COMBAT_RANGED, 10) == 0);
    assert(rc_encounter_scale_player_damage(
               w, w->npcs[supreme_idx].uid, COMBAT_MELEE_SLASH, 10) == 10);
    assert(rc_encounter_scale_player_damage(
               w, w->npcs[supreme_idx].uid, COMBAT_MAGIC, 10) == 0);
    assert(rc_encounter_scale_player_damage(
               w, w->npcs[prime_idx].uid, COMBAT_RANGED, 10) == 10);
    assert(rc_encounter_scale_player_damage(
               w, w->npcs[prime_idx].uid, COMBAT_MELEE_CRUSH, 10) == 0);

    assert(corp->damage_mod_count == 1);
    w->player.equipment[EQUIP_WEAPON].item_id = 0;
    assert(rc_encounter_scale_player_damage(
               w, w->npcs[corp_npc_idx].uid, COMBAT_MELEE_STAB, 100) == 50);
    w->player.equipment[EQUIP_WEAPON].item_id = 3;
    assert(rc_encounter_scale_player_damage(
               w, w->npcs[corp_npc_idx].uid, COMBAT_MELEE_STAB, 100) == 100);

    int kree_spec_idx = rc_encounter_find_spec(w, 3162);
    assert(kree_spec_idx >= 0);
    const RcEncounterSpec *kree = &w->encounter.registry[kree_spec_idx];
    assert(kree->damage_mod_count == 4);
    int kree_idx = rc_npc_spawn(w, 21, 3360, 3400, 0);
    assert(kree_idx >= 0);
    w->player.equipment[EQUIP_WEAPON].item_id = 0;
    assert(rc_encounter_scale_player_damage(
               w, w->npcs[kree_idx].uid, COMBAT_MELEE_SLASH, 100) == 0);
    w->player.equipment[EQUIP_WEAPON].item_id = 4;
    assert(rc_encounter_scale_player_damage(
               w, w->npcs[kree_idx].uid, COMBAT_MELEE_SLASH, 100) == 100);
    w->player.equipment[EQUIP_WEAPON].item_id = 5;
    assert(rc_encounter_scale_player_damage(
               w, w->npcs[kree_idx].uid, COMBAT_MELEE_CRUSH, 100) == 100);
    int kree_active = find_active_encounter(w, w->npcs[kree_idx].uid);
    int kree_wind = find_attack(kree, "Wind Gust");
    assert(kree_active >= 0 && kree_wind >= 0);
    w->player.x = w->npcs[kree_idx].x + 2;
    w->player.y = w->npcs[kree_idx].y;
    w->encounter.active[kree_active].last_attack_idx = (uint8_t)kree_wind;
    int room_effects_before = count_effects(w, RC_ENC_EFFECT_ROOM_ATTACK);
    RcPayloadPlayerDamaged kree_wind_hit = {
        .source_npc_id = (uint16_t)w->npcs[kree_idx].uid,
        .damage = 1,
        .style = COMBAT_RANGED,
    };
    rc_event_fire(w, RC_EVT_PLAYER_DAMAGED, &kree_wind_hit);
    assert(w->player.x == w->npcs[kree_idx].x + 3);
    assert(count_effects(w, RC_ENC_EFFECT_ROOM_ATTACK)
           == room_effects_before + 1);

    // ---- 13. Zulrah/Vorkath first runtime slice -----------------------
    int vorkath_spec_idx = rc_encounter_find_spec(w, 8061);
    assert(vorkath_spec_idx >= 0);
    const RcEncounterSpec *vorkath = &w->encounter.registry[vorkath_spec_idx];
    int vork_idx = rc_npc_spawn(w, 22, 2272, 4052, 0);
    assert(vork_idx >= 0);
    int vork_active = find_active_encounter(w, w->npcs[vork_idx].uid);
    assert(vork_active >= 0);
    int vork_venom = find_attack(vorkath, "Venomous Dragonfire");
    int vork_corrupt = find_attack(vorkath, "Corrupting Dragonfire");
    assert(vork_venom >= 0 && vork_corrupt >= 0);
    w->encounter.active[vork_active].last_attack_idx = (uint8_t)vork_venom;
    w->player.venom_damage = 0;
    RcPayloadPlayerDamaged vork_hit = {
        .source_npc_id = (uint16_t)w->npcs[vork_idx].uid,
        .damage = 0,
        .style = COMBAT_MAGIC,
    };
    rc_event_fire(w, RC_EVT_PLAYER_DAMAGED, &vork_hit);
    assert(w->player.venom_damage == 6);
    w->encounter.active[vork_active].last_attack_idx = (uint8_t)vork_corrupt;
    w->player.active_prayers = PRAYER_PROTECT_MAGIC | PRAYER_PROTECT_RANGE;
    rc_event_fire(w, RC_EVT_PLAYER_DAMAGED, &vork_hit);
    assert(w->player.active_prayers == 0);

    int spawn_before = w->npc_count;
    for (int i = 0; i < 7; i++) {
        RcPayloadNpcAttack atk = {
            .npc_id = (uint16_t)w->npcs[vork_idx].uid,
            .style = COMBAT_MAGIC,
        };
        rc_event_fire(w, RC_EVT_NPC_ATTACK, &atk);
    }
    assert(w->npc_count == spawn_before + 1);
    assert(g_npc_defs[w->npcs[spawn_before].def_id].id == 8062);
    assert(w->player.freeze_timer == 24);
    assert(rc_encounter_scale_player_damage(
               w, w->npcs[vork_idx].uid, COMBAT_RANGED, 100) == 0);
    w->npcs[spawn_before].is_dead = true;
    assert(rc_encounter_scale_player_damage(
               w, w->npcs[vork_idx].uid, COMBAT_RANGED, 100) == 100);

    int acid_before = count_effects(w, RC_ENC_EFFECT_ACID_POOL);
    for (int i = 0; i < 7; i++) {
        RcPayloadNpcAttack atk = {
            .npc_id = (uint16_t)w->npcs[vork_idx].uid,
            .style = COMBAT_MAGIC,
        };
        rc_event_fire(w, RC_EVT_NPC_ATTACK, &atk);
    }
    assert(count_effects(w, RC_ENC_EFFECT_ACID_POOL) == acid_before + 10);
    assert(rc_encounter_scale_player_damage(
               w, w->npcs[vork_idx].uid, COMBAT_RANGED, 100) == 50);
    w->player.num_pending_hits = 0;
    rc_encounter_tick_effects(w);
    assert(w->player.num_pending_hits == 1);
    assert(w->player.pending_hits[0].damage == 10);
    w->player.num_pending_hits = 0;
    rc_encounter_tick(w);
    assert(w->player.num_pending_hits >= 1);
    w->player.num_pending_hits = 0;

    int zulrah_spec_idx = rc_encounter_find_spec(w, 2042);
    assert(zulrah_spec_idx >= 0);
    const RcEncounterSpec *zulrah = &w->encounter.registry[zulrah_spec_idx];
    int zul_idx = rc_npc_spawn(w, 24, 2200, 3056, 0);
    assert(zul_idx >= 0);
    int zul_active = find_active_encounter(w, w->npcs[zul_idx].uid);
    int z_dive = find_mech(zulrah, "Dive Between Forms");
    assert(zul_active >= 0 && z_dive >= 0);
    w->player.equipment[EQUIP_WEAPON].item_id = 0;
    assert(rc_encounter_scale_player_damage(
               w, w->npcs[zul_idx].uid, COMBAT_RANGED, 100) == 25);
    int capped = rc_encounter_scale_player_damage(
        w, w->npcs[zul_idx].uid, COMBAT_MAGIC, 100);
    assert(capped >= 45 && capped <= 50);
    assert(rc_encounter_scale_player_damage(
               w, w->npcs[zul_idx].uid, COMBAT_MELEE_SLASH, 100) == 0);
    w->player.equipment[EQUIP_WEAPON].item_id = 4;
    capped = rc_encounter_scale_player_damage(
        w, w->npcs[zul_idx].uid, COMBAT_MELEE_SLASH, 100);
    assert(capped >= 45 && capped <= 50);

    int dive_effects = count_effects(w, RC_ENC_EFFECT_FORM_DIVE);
    zulrah->mechanics[z_dive].prim(
        w, zul_active, zulrah->mechanics[z_dive].param_block);
    assert(count_effects(w, RC_ENC_EFFECT_FORM_DIVE) == dive_effects + 1);
    assert(w->npcs[zul_idx].player_untargetable);
    assert(g_npc_defs[w->npcs[zul_idx].def_id].id == 2043);
    for (int i = 0; i < 5; i++) rc_encounter_tick_effects(w);
    assert(!w->npcs[zul_idx].player_untargetable);
    w->encounter.active[vork_active].active = false;
    w->encounter.active[zul_active].active = false;

    // ---- 14. Scorpia phase-enter summon + guardian heal ----------------
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

    // ---- 14. Chaos Elemental confusion + madness -----------------------
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
