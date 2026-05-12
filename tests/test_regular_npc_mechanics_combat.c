#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "api.h"
#include "activity_mechanics.h"
#include "combat.h"
#include "config.h"
#include "content.h"
#include "items.h"
#include "monster_mechanics.h"
#include "npc.h"
#include "prayer.h"

#define RC_TEST_REGULAR_NPC_MECH_BIN \
    RC_TEST_SOURCE_DIR "/data/defs/regular_npc_mechanics.bin"

static void item(int id, const char *name, int slot) {
    RcItemDef *d = &g_item_defs[id];
    memset(d, 0, sizeof(*d));
    d->id = id;
    d->loaded = true;
    d->equippable = true;
    d->equip_slot = slot;
    strncpy(d->name, name, sizeof(d->name) - 1);
}

static void npc_def(int idx, int id, const char *name, int hp) {
    RcNpcDef *d = &g_npc_defs[idx];
    memset(d, 0, sizeof(*d));
    d->id = id;
    d->hitpoints = hp;
    d->stats[3] = hp;
    strncpy(d->name, name, sizeof(d->name) - 1);
}

static void activity_row(int idx, int npc_id, uint64_t bits,
                         uint16_t profile) {
    RcActivityMechanic *row = &g_rc_activity_mechanics[idx];
    memset(row, 0, sizeof(*row));
    row->npc_count = 1;
    row->npc_ids[0] = (uint32_t)npc_id;
    row->behavior_bits = bits;
    row->profile_id = profile;
}

static void reset_player_gear(RcPlayer *p) {
    for (int i = 0; i < RC_EQUIP_COUNT; i++) {
        p->equipment[i].item_id = -1;
        p->equipment[i].quantity = 0;
    }
    for (int i = 0; i < RC_INVENTORY_SIZE; i++) {
        p->inventory[i].item_id = -1;
        p->inventory[i].quantity = 0;
    }
}

int main(void) {
    memset(g_item_defs, 0, sizeof(g_item_defs));
    memset(g_npc_defs, 0, sizeof(g_npc_defs));
    g_item_def_count = 0;
    g_npc_def_count = 20;

    item(1, "Bronze sword", EQUIP_WEAPON);
    item(2, "Leaf-bladed sword", EQUIP_WEAPON);
    item(3, "Rock hammer", EQUIP_WEAPON);
    item(4, "Anti-dragon shield", EQUIP_SHIELD);
    item(5, "Broad bolts", EQUIP_AMMO);
    item(6, "Slayer's staff", EQUIP_WEAPON);
    item(7, "Serpentine helm", EQUIP_HEAD);
    item(8, "Tortugan shield", EQUIP_SHIELD);
    item(9, "Rune halberd", EQUIP_WEAPON);
    item(10, "Mirror shield", EQUIP_SHIELD);
    item(11, "Nose peg", EQUIP_HEAD);
    item(12, "Slayer helmet", EQUIP_HEAD);
    item(1540, "Anti-dragon shield", EQUIP_SHIELD);
    g_item_def_count = 1541;

    npc_def(0, 426, "Turoth", 60);
    npc_def(1, 412, "Gargoyle", 105);
    npc_def(2, 260, "Green dragon", 75);
    npc_def(3, 406, "Cave crawler", 35);
    npc_def(4, 7940, "Revenant dragon", 100);
    npc_def(5, 6766, "Lizardman shaman", 150);
    npc_def(6, 9999, "Spawn", 10);
    npc_def(7, 13668, "Araxxor", 400);
    npc_def(8, 1677, "Verac the Defiled", 120);
    npc_def(9, 3127, "TzTok-Jad", 250);
    npc_def(10, 11246, "Revenant maledictus", 155);
    npc_def(11, 14176, "Shellbane gryphon", 200);
    npc_def(12, 13011, "Blood Moon", 500);
    npc_def(13, 1672, "Ahrim the Blighted", 100);
    npc_def(14, 1673, "Dharok the Wretched", 100);
    npc_def(15, 1674, "Guthan the Infested", 100);
    npc_def(16, 1675, "Karil the Tainted", 100);
    npc_def(17, 1676, "Torag the Corrupted", 100);
    npc_def(18, 19990, "Generic enrage boss", 100);
    npc_def(19, 19991, "Generic drain boss", 100);
    npc_def(20, 7852, "Dawn", 450);
    npc_def(21, 7887, "Dusk", 450);
    npc_def(22, 15626, "Brutus", 600);
    npc_def(23, 15628, "Demonic Brutus", 750);
    npc_def(24, 417, "Basilisk", 75);
    npc_def(25, 2, "Aberrant spectre", 90);
    g_npc_def_count = 26;

    memset(g_rc_activity_mechanics, 0, sizeof(g_rc_activity_mechanics));
    g_rc_activity_mechanic_count = 17;
    activity_row(0, 13668,
                 RC_ACTIVITY_BEHAVIOR_VENOM |
                 RC_ACTIVITY_BEHAVIOR_HEAL_ON_HIT |
                 RC_ACTIVITY_BEHAVIOR_PRAYER_DRAIN |
                 RC_ACTIVITY_BEHAVIOR_STAT_DRAIN |
                 RC_ACTIVITY_BEHAVIOR_PROTECTION_PIERCE |
                 RC_ACTIVITY_BEHAVIOR_AREA_PRESSURE |
                 RC_ACTIVITY_BEHAVIOR_ENRAGE,
                 RC_ACTIVITY_PROFILE_ARAXXOR);
    activity_row(1, 1677, RC_ACTIVITY_BEHAVIOR_PROTECTION_PIERCE,
                 RC_ACTIVITY_PROFILE_BARROWS_VERAC);
    activity_row(2, 3127,
                 RC_ACTIVITY_BEHAVIOR_WAVE_BOSS |
                 RC_ACTIVITY_BEHAVIOR_AREA_PRESSURE |
                 RC_ACTIVITY_BEHAVIOR_PROTECTION_PIERCE,
                 RC_ACTIVITY_PROFILE_TZTOK_JAD);
    activity_row(3, 11246,
                 RC_ACTIVITY_BEHAVIOR_HEAL_ON_HIT |
                 RC_ACTIVITY_BEHAVIOR_FREEZE |
                 RC_ACTIVITY_BEHAVIOR_AREA_PRESSURE,
                 RC_ACTIVITY_PROFILE_REVENANT_MALEDICTUS);
    activity_row(4, 14176,
                 RC_ACTIVITY_BEHAVIOR_ENRAGE |
                 RC_ACTIVITY_BEHAVIOR_AREA_PRESSURE,
                 RC_ACTIVITY_PROFILE_SHELLBANE_GRYPHON);
    activity_row(5, 13011,
                 RC_ACTIVITY_BEHAVIOR_HEAL_ON_HIT |
                 RC_ACTIVITY_BEHAVIOR_AREA_PRESSURE |
                 RC_ACTIVITY_BEHAVIOR_DAMAGE_GATE,
                 RC_ACTIVITY_PROFILE_BLOOD_MOON);
    activity_row(6, 1672, RC_ACTIVITY_BEHAVIOR_STAT_DRAIN,
                 RC_ACTIVITY_PROFILE_BARROWS_AHRIM);
    activity_row(7, 1673, RC_ACTIVITY_BEHAVIOR_ENRAGE,
                 RC_ACTIVITY_PROFILE_BARROWS_DHAROK);
    activity_row(8, 1674, RC_ACTIVITY_BEHAVIOR_HEAL_ON_HIT,
                 RC_ACTIVITY_PROFILE_BARROWS_GUTHAN);
    activity_row(9, 1675, RC_ACTIVITY_BEHAVIOR_STAT_DRAIN,
                 RC_ACTIVITY_PROFILE_BARROWS_KARIL);
    activity_row(10, 1676, RC_ACTIVITY_BEHAVIOR_STAT_DRAIN,
                 RC_ACTIVITY_PROFILE_BARROWS_TORAG);
    activity_row(11, 19990, RC_ACTIVITY_BEHAVIOR_ENRAGE,
                 RC_ACTIVITY_PROFILE_NONE);
    activity_row(12, 19991,
                 RC_ACTIVITY_BEHAVIOR_STAT_DRAIN |
                 RC_ACTIVITY_BEHAVIOR_PRAYER_DRAIN,
                 RC_ACTIVITY_PROFILE_NONE);
    activity_row(13, 7852,
                 RC_ACTIVITY_BEHAVIOR_DAMAGE_GATE |
                 RC_ACTIVITY_BEHAVIOR_AREA_PRESSURE,
                 RC_ACTIVITY_PROFILE_DAWN);
    activity_row(14, 7887,
                 RC_ACTIVITY_BEHAVIOR_AREA_PRESSURE |
                 RC_ACTIVITY_BEHAVIOR_PROTECTION_PIERCE,
                 RC_ACTIVITY_PROFILE_DUSK);
    activity_row(15, 15626,
                 RC_ACTIVITY_BEHAVIOR_AREA_PRESSURE |
                 RC_ACTIVITY_BEHAVIOR_PROTECTION_PIERCE,
                 RC_ACTIVITY_PROFILE_BRUTUS);
    activity_row(16, 15628,
                 RC_ACTIVITY_BEHAVIOR_AREA_PRESSURE |
                 RC_ACTIVITY_BEHAVIOR_PROTECTION_PIERCE,
                 RC_ACTIVITY_PROFILE_DEMONIC_BRUTUS);
    rc_activity_mechanics_rebuild_index();

    assert(rc_load_monster_mechanics(RC_TEST_REGULAR_NPC_MECH_BIN) == 16);

    RcWorldConfig cfg = rc_preset_combat_only();
    cfg.monster_mechanics_path = NULL;
    cfg.encounters_path = NULL;
    RcWorld *w = rc_world_create_config(&cfg);
    assert(w != NULL);
    RcNpc pre_hook_turoth = { .def_id = 0, .current_hp = 60 };
    reset_player_gear(&w->player);
    w->player.combat_style = COMBAT_MELEE_SLASH;
    w->player.equipment[EQUIP_WEAPON].item_id = 1;
    assert(rc_combat_apply_regular_npc_player_damage_rules(
               w, &pre_hook_turoth, 20) == 20);
    rc_content_combat_register(w);

    RcNpc turoth = { .def_id = 0, .current_hp = 60 };
    reset_player_gear(&w->player);
    w->player.combat_style = COMBAT_MELEE_SLASH;
    w->player.equipment[EQUIP_WEAPON].item_id = 1;
    assert(rc_combat_apply_regular_npc_player_damage_rules(w, &turoth, 20)
           == 0);
    w->player.equipment[EQUIP_WEAPON].item_id = 2;
    assert(rc_combat_apply_regular_npc_player_damage_rules(w, &turoth, 20)
           == 20);
    reset_player_gear(&w->player);
    w->player.combat_style = COMBAT_RANGED;
    assert(rc_combat_apply_regular_npc_player_damage_rules(w, &turoth, 20)
           == 0);
    w->player.equipment[EQUIP_AMMO].item_id = 5;
    assert(rc_combat_apply_regular_npc_player_damage_rules(w, &turoth, 20)
           == 20);
    reset_player_gear(&w->player);
    w->player.combat_style = COMBAT_MAGIC;
    assert(rc_combat_apply_regular_npc_player_damage_rules(w, &turoth, 20)
           == 0);
    w->player.equipment[EQUIP_WEAPON].item_id = 6;
    assert(rc_combat_apply_regular_npc_player_damage_rules(w, &turoth, 20)
           == 20);

    RcNpc gargoyle = { .def_id = 1, .current_hp = 10 };
    reset_player_gear(&w->player);
    assert(rc_combat_apply_regular_npc_player_damage_rules(w, &gargoyle, 15)
           == 9);
    w->player.inventory[0].item_id = 3;
    w->player.inventory[0].quantity = 1;
    assert(rc_combat_apply_regular_npc_player_damage_rules(w, &gargoyle, 15)
           == 15);
    reset_player_gear(&w->player);
    w->player.slayer_unlocks = RC_SLAYER_UNLOCK_AUTO_FINISHER;
    assert(rc_combat_apply_regular_npc_player_damage_rules(w, &gargoyle, 15)
           == 15);
    w->player.slayer_unlocks = 0;

    RcNpc dragon = { .def_id = 2, .current_hp = 75 };
    reset_player_gear(&w->player);
    assert(rc_combat_apply_regular_npc_attack_rules(w, &dragon, 20) == 20);
    w->player.equipment[EQUIP_SHIELD].item_id = 4;
    assert(rc_combat_apply_regular_npc_attack_rules(w, &dragon, 20) == 5);
    w->player.equipment[EQUIP_SHIELD].item_id = 1540;
    assert(rc_combat_apply_regular_npc_attack_rules(w, &dragon, 20) == 5);

    RcNpc basilisk = { .def_id = 24, .current_hp = 75 };
    reset_player_gear(&w->player);
    w->player.skills.boosted_level[SKILL_ATTACK] = 10;
    assert(rc_combat_apply_regular_npc_attack_rules(w, &basilisk, 10) == 12);
    assert(w->player.skills.boosted_level[SKILL_ATTACK] == 9);
    w->player.skills.boosted_level[SKILL_ATTACK] = 10;
    w->player.equipment[EQUIP_SHIELD].item_id = 10;
    assert(rc_combat_apply_regular_npc_attack_rules(w, &basilisk, 10) == 10);
    assert(w->player.skills.boosted_level[SKILL_ATTACK] == 10);

    RcNpc spectre = { .def_id = 25, .current_hp = 90 };
    reset_player_gear(&w->player);
    w->player.skills.boosted_level[SKILL_ATTACK] = 10;
    assert(rc_combat_apply_regular_npc_attack_rules(w, &spectre, 10) == 12);
    assert(w->player.skills.boosted_level[SKILL_ATTACK] == 9);
    w->player.skills.boosted_level[SKILL_ATTACK] = 10;
    w->player.equipment[EQUIP_HEAD].item_id = 11;
    assert(rc_combat_apply_regular_npc_attack_rules(w, &spectre, 10) == 10);
    assert(w->player.skills.boosted_level[SKILL_ATTACK] == 10);
    w->player.equipment[EQUIP_HEAD].item_id = 12;
    assert(rc_combat_apply_regular_npc_attack_rules(w, &spectre, 10) == 10);

    g_npc_defs[2].attack_speed = 4;
    g_npc_defs[2].attack_types = 0x08;
    g_npc_defs[2].max_hit = 20;
    g_npc_defs[2].stats[5] = 99;
    dragon.active = true;
    dragon.target_uid = 0;
    dragon.x = w->player.x;
    dragon.y = w->player.y;
    dragon.plane = w->player.plane;
    rc_combat_tick_npc(w, &dragon);
    assert(w->player.num_pending_hits == 1);

    w->player.num_pending_hits = 0;
    w->player.current_hp = 1000;
    int cave_idx = rc_npc_spawn(w, 3, w->player.x, w->player.y, 0);
    assert(cave_idx >= 0);
    RcNpc *cave = &w->npcs[cave_idx];
    rc_queue_hit(w->player.pending_hits, &w->player.num_pending_hits,
                 4, 0, COMBAT_MELEE_CRUSH, cave->uid, 0, w->tick);
    rc_resolve_player_hits(w);
    assert(w->player.poison_damage == 2);
    int hp_before_poison = w->player.current_hp;
    w->player.poison_tick_counter = 0;
    rc_combat_tick_player_status(w);
    assert(w->player.current_hp == hp_before_poison - 20);

    w->player.poison_damage = 0;
    reset_player_gear(&w->player);
    w->player.equipment[EQUIP_HEAD].item_id = 7;
    rc_queue_hit(w->player.pending_hits, &w->player.num_pending_hits,
                 4, 0, COMBAT_MELEE_CRUSH, cave->uid, 0, w->tick);
    rc_resolve_player_hits(w);
    assert(w->player.poison_damage == 0);

    int rev_idx = rc_npc_spawn(w, 4, w->player.x, w->player.y, 0);
    assert(rev_idx >= 0);
    RcNpc *rev = &w->npcs[rev_idx];
    rev->current_hp = 50;
    rc_queue_hit(w->player.pending_hits, &w->player.num_pending_hits,
                 8, 0, COMBAT_MAGIC, rev->uid, 0, w->tick);
    rc_resolve_player_hits(w);
    assert(w->player.teleblock_timer == 500);
    assert(w->player.freeze_timer == 10);
    assert(rev->current_hp == 52);

    int shaman_idx = rc_npc_spawn(w, 5, w->player.x, w->player.y, 0);
    assert(shaman_idx >= 0);
    RcNpc *shaman = &w->npcs[shaman_idx];
    g_npc_defs[5].attack_speed = 4;
    g_npc_defs[5].attack_types = 0x10;
    g_npc_defs[5].max_hit = 10;
    g_npc_defs[5].stats[4] = 99;
    shaman->active = true;
    shaman->target_uid = 0;
    shaman->attack_count = 4;
    int npc_count_before = w->npc_count;
    w->player.num_pending_hits = 0;
    rc_combat_tick_npc(w, shaman);
    assert(shaman->attack_count == 5);
    assert(w->player.num_pending_hits >= 2);
    assert(w->npc_count == npc_count_before + 1);

    int araxxor_idx = rc_npc_spawn(w, 7, w->player.x, w->player.y, 0);
    assert(araxxor_idx >= 0);
    RcNpc *araxxor = &w->npcs[araxxor_idx];
    araxxor->current_hp = 100;
    reset_player_gear(&w->player);
    w->player.current_hp = 1000;
    w->player.current_prayer_points = 30;
    w->player.active_prayers = PRAYER_PROTECT_MELEE;
    w->player.poison_damage = 0;
    w->player.venom_damage = 0;
    w->player.num_pending_hits = 0;
    w->player.skills.boosted_level[SKILL_ATTACK] = 10;
    rc_queue_hit(w->player.pending_hits, &w->player.num_pending_hits,
                 20, 0, COMBAT_MELEE_CRUSH, araxxor->uid,
                 w->player.active_prayers, w->tick);
    rc_resolve_player_hits(w);
    assert(w->player.current_hp == 900);
    assert(w->player.venom_damage == 6);
    assert(w->player.current_prayer_points == 30);
    assert(w->player.skills.boosted_level[SKILL_ATTACK] == 10);
    assert(araxxor->current_hp == 102);

    g_npc_defs[7].attack_speed = 6;
    g_npc_defs[7].attack_types = 0x01;
    g_npc_defs[7].max_hit = 10;
    g_npc_defs[7].stats[0] = 99;
    araxxor->active = true;
    araxxor->target_uid = 0;
    araxxor->attack_count = 4;
    araxxor->current_hp = 90;
    w->player.num_pending_hits = 0;
    w->player.active_prayers = 0;
    rc_combat_tick_npc(w, araxxor);
    assert(araxxor->attack_count == 5);
    assert(w->player.num_pending_hits >= 2);
    assert(araxxor->attack_timer == 4);

    w->player.num_pending_hits = 0;
    w->player.current_hp = 1000;
    w->player.current_prayer_points = 100;
    w->player.skills.boosted_level[SKILL_DEFENCE] = 100;
    rc_queue_hit(w->player.pending_hits, &w->player.num_pending_hits,
                 10, 0, COMBAT_MAGIC, araxxor->uid, 0, w->tick);
    rc_resolve_player_hits(w);
    assert(w->player.current_prayer_points == 91);
    rc_queue_hit(w->player.pending_hits, &w->player.num_pending_hits,
                 10, 0, COMBAT_RANGED, araxxor->uid, 0, w->tick);
    rc_resolve_player_hits(w);
    assert(w->player.skills.boosted_level[SKILL_DEFENCE] == 91);

    int verac_idx = rc_npc_spawn(w, 8, w->player.x, w->player.y, 0);
    assert(verac_idx >= 0);
    RcNpc *verac = &w->npcs[verac_idx];
    w->rng_state = 4;
    w->player.current_hp = 1000;
    w->player.active_prayers = PRAYER_PROTECT_MELEE;
    rc_queue_hit(w->player.pending_hits, &w->player.num_pending_hits,
                 20, 0, COMBAT_MELEE_CRUSH, verac->uid,
                 w->player.active_prayers, w->tick);
    rc_resolve_player_hits(w);
    assert(w->player.current_hp == 800);

    int jad_idx = rc_npc_spawn(w, 9, w->player.x + 10, w->player.y, 0);
    assert(jad_idx >= 0);
    RcNpc *jad = &w->npcs[jad_idx];
    w->player.current_hp = 1000;
    w->player.active_prayers = PRAYER_PROTECT_RANGE;
    rc_queue_hit(w->player.pending_hits, &w->player.num_pending_hits,
                 20, 0, COMBAT_RANGED, jad->uid,
                 w->player.active_prayers, w->tick);
    rc_resolve_player_hits(w);
    assert(w->player.current_hp == 1000);

    int shell_idx = rc_npc_spawn(w, 11, w->player.x, w->player.y, 0);
    assert(shell_idx >= 0);
    RcNpc *shell = &w->npcs[shell_idx];
    reset_player_gear(&w->player);
    assert(rc_combat_apply_regular_npc_attack_rules(w, shell, 20) == 30);
    w->player.equipment[EQUIP_SHIELD].item_id = 8;
    w->player.active_prayers = PRAYER_PROTECT_MELEE;
    assert(rc_combat_apply_regular_npc_attack_rules(w, shell, 20) == 0);

    RcNpc dawn = { .def_id = 20, .current_hp = 450 };
    reset_player_gear(&w->player);
    w->player.combat_style = COMBAT_MELEE_SLASH;
    w->player.equipment[EQUIP_WEAPON].item_id = 1;
    assert(rc_combat_apply_regular_npc_player_damage_rules(w, &dawn, 20)
           == 0);
    w->player.equipment[EQUIP_WEAPON].item_id = 9;
    assert(rc_combat_apply_regular_npc_player_damage_rules(w, &dawn, 20)
           == 20);
    reset_player_gear(&w->player);
    w->player.combat_style = COMBAT_RANGED;
    assert(rc_combat_apply_regular_npc_player_damage_rules(w, &dawn, 20)
           == 20);
    RcNpc blood_gate = { .def_id = 12, .current_hp = 500 };
    reset_player_gear(&w->player);
    w->player.combat_style = COMBAT_MELEE_SLASH;
    w->player.equipment[EQUIP_WEAPON].item_id = 1;
    assert(rc_combat_apply_regular_npc_player_damage_rules(w, &blood_gate, 20)
           == 20);

    int dusk_idx = rc_npc_spawn(w, 21, w->player.x, w->player.y, 0);
    int brutus_idx = rc_npc_spawn(w, 22, w->player.x, w->player.y, 0);
    int demonic_idx = rc_npc_spawn(w, 23, w->player.x, w->player.y, 0);
    assert(dusk_idx >= 0 && brutus_idx >= 0 && demonic_idx >= 0);
    RcNpc *dusk = &w->npcs[dusk_idx];
    RcNpc *brutus = &w->npcs[brutus_idx];
    RcNpc *demonic = &w->npcs[demonic_idx];
    w->player.current_hp = 1000;
    w->player.active_prayers = PRAYER_PROTECT_MELEE;
    rc_queue_hit(w->player.pending_hits, &w->player.num_pending_hits,
                 20, 0, COMBAT_MELEE_CRUSH, dusk->uid,
                 w->player.active_prayers, w->tick);
    rc_resolve_player_hits(w);
    assert(w->player.current_hp == 960);

    w->player.current_hp = 1000;
    w->player.active_prayers = PRAYER_PROTECT_RANGE;
    rc_queue_hit(w->player.pending_hits, &w->player.num_pending_hits,
                 19, 0, COMBAT_RANGED, brutus->uid,
                 w->player.active_prayers, w->tick);
    rc_resolve_player_hits(w);
    assert(w->player.current_hp == 810);

    w->player.current_hp = 1000;
    w->player.active_prayers = PRAYER_PROTECT_MELEE;
    rc_queue_hit(w->player.pending_hits, &w->player.num_pending_hits,
                 20, 0, COMBAT_MELEE_CRUSH, demonic->uid,
                 w->player.active_prayers, w->tick);
    rc_resolve_player_hits(w);
    assert(w->player.current_hp == 960);
    w->player.current_hp = 1000;
    w->player.active_prayers = PRAYER_PROTECT_RANGE;
    rc_queue_hit(w->player.pending_hits, &w->player.num_pending_hits,
                 56, 0, COMBAT_RANGED, demonic->uid,
                 w->player.active_prayers, w->tick);
    rc_resolve_player_hits(w);
    assert(w->player.current_hp == 440);

    g_npc_defs[22].attack_speed = 5;
    g_npc_defs[22].attack_types = 0x01;
    g_npc_defs[22].max_hit = 3;
    g_npc_defs[22].stats[0] = 99;
    brutus->active = true;
    brutus->target_uid = 0;
    brutus->attack_count = 4;
    brutus->attack_timer = 0;
    w->player.num_pending_hits = 0;
    rc_combat_tick_npc(w, brutus);
    assert(brutus->attack_count == 5);
    assert(w->player.num_pending_hits >= 2);
    assert(w->player.pending_hits[0].damage == 19);

    g_npc_defs[23].attack_speed = 5;
    g_npc_defs[23].attack_types = 0x01;
    g_npc_defs[23].max_hit = 4;
    g_npc_defs[23].stats[0] = 99;
    demonic->active = true;
    demonic->target_uid = 0;
    demonic->attack_count = 2;
    demonic->attack_timer = 0;
    w->player.num_pending_hits = 0;
    rc_combat_tick_npc(w, demonic);
    assert(demonic->attack_count == 3);
    assert(w->player.num_pending_hits >= 2);
    assert(w->player.pending_hits[0].damage == 56);

    int blood_idx = rc_npc_spawn(w, 12, w->player.x, w->player.y, 0);
    assert(blood_idx >= 0);
    RcNpc *blood = &w->npcs[blood_idx];
    blood->current_hp = 100;
    blood->attack_count = 3;
    w->player.num_pending_hits = 0;
    rc_queue_hit(w->player.pending_hits, &w->player.num_pending_hits,
                 8, 0, COMBAT_MELEE_CRUSH, blood->uid, 0, w->tick);
    rc_resolve_player_hits(w);
    assert(blood->current_hp == 130);

    int ahrim_idx = rc_npc_spawn(w, 13, w->player.x, w->player.y, 0);
    int dharok_idx = rc_npc_spawn(w, 14, w->player.x, w->player.y, 0);
    int guthan_idx = rc_npc_spawn(w, 15, w->player.x, w->player.y, 0);
    int karil_idx = rc_npc_spawn(w, 16, w->player.x, w->player.y, 0);
    int torag_idx = rc_npc_spawn(w, 17, w->player.x, w->player.y, 0);
    assert(ahrim_idx >= 0 && dharok_idx >= 0 && guthan_idx >= 0);
    assert(karil_idx >= 0 && torag_idx >= 0);
    RcNpc *ahrim = &w->npcs[ahrim_idx];
    RcNpc *dharok = &w->npcs[dharok_idx];
    RcNpc *guthan = &w->npcs[guthan_idx];
    RcNpc *karil = &w->npcs[karil_idx];
    RcNpc *torag = &w->npcs[torag_idx];

    w->rng_state = 4;
    w->player.skills.boosted_level[SKILL_STRENGTH] = 50;
    rc_queue_hit(w->player.pending_hits, &w->player.num_pending_hits,
                 5, 0, COMBAT_MAGIC, ahrim->uid, 0, w->tick);
    rc_resolve_player_hits(w);
    assert(w->player.skills.boosted_level[SKILL_STRENGTH] == 45);

    w->rng_state = 4;
    guthan->current_hp = 50;
    rc_queue_hit(w->player.pending_hits, &w->player.num_pending_hits,
                 20, 0, COMBAT_MELEE_CRUSH, guthan->uid, 0, w->tick);
    rc_resolve_player_hits(w);
    assert(guthan->current_hp == 70);

    w->rng_state = 4;
    w->player.skills.boosted_level[SKILL_AGILITY] = 50;
    rc_queue_hit(w->player.pending_hits, &w->player.num_pending_hits,
                 5, 0, COMBAT_RANGED, karil->uid, 0, w->tick);
    rc_resolve_player_hits(w);
    assert(w->player.skills.boosted_level[SKILL_AGILITY] == 40);

    w->rng_state = 4;
    w->player.run_energy = 10000;
    rc_queue_hit(w->player.pending_hits, &w->player.num_pending_hits,
                 5, 0, COMBAT_MELEE_CRUSH, torag->uid, 0, w->tick);
    rc_resolve_player_hits(w);
    assert(w->player.run_energy == 8000);

    g_npc_defs[14].attack_speed = 6;
    g_npc_defs[14].attack_types = 0x01;
    g_npc_defs[14].max_hit = 100;
    g_npc_defs[14].stats[0] = 999;
    dharok->active = true;
    dharok->target_uid = 0;
    dharok->current_hp = 10;
    dharok->attack_count = 0;
    w->player.num_pending_hits = 0;
    w->rng_state = 4;
    rc_combat_tick_npc(w, dharok);
    assert(dharok->attack_timer == 6);
    assert(w->player.num_pending_hits == 1);
    assert(w->player.pending_hits[0].damage > 54);
    w->player.num_pending_hits = 0;

    int mal_idx = rc_npc_spawn(w, 10, w->player.x, w->player.y, 0);
    assert(mal_idx >= 0);
    RcNpc *mal = &w->npcs[mal_idx];
    mal->current_hp = 50;
    mal->attack_count = 1;
    w->player.freeze_timer = 0;
    w->player.teleblock_timer = 0;
    rc_queue_hit(w->player.pending_hits, &w->player.num_pending_hits,
                 8, 0, COMBAT_MAGIC, mal->uid, 0, w->tick);
    rc_resolve_player_hits(w);
    assert(w->player.freeze_timer == 10);
    assert(w->player.teleblock_timer == 0);
    mal->attack_count = 2;
    rc_queue_hit(w->player.pending_hits, &w->player.num_pending_hits,
                 20, 0, COMBAT_MAGIC, mal->uid, 0, w->tick);
    rc_resolve_player_hits(w);
    assert(mal->current_hp == 60);

    g_npc_defs[10].attack_speed = 5;
    g_npc_defs[10].attack_types = 0x08;
    g_npc_defs[10].max_hit = 10;
    g_npc_defs[10].stats[5] = 99;
    mal->active = true;
    mal->target_uid = 0;
    mal->attack_count = 2;
    mal->attack_timer = 0;
    w->player.num_pending_hits = 0;
    rc_combat_tick_npc(w, mal);
    assert(mal->attack_count == 3);
    assert(w->player.num_pending_hits >= 2);
    assert(w->player.pending_hits[0].damage == 25);

    g_npc_defs[9].attack_speed = 8;
    g_npc_defs[9].attack_types = 0x19;
    g_npc_defs[9].max_hit = 97;
    g_npc_defs[9].stats[4] = 999;
    g_npc_defs[9].stats[5] = 999;
    jad->active = true;
    jad->target_uid = 0;
    jad->attack_timer = 0;
    jad->x = w->player.x + 10;
    jad->y = w->player.y;
    w->player.num_pending_hits = 0;
    rc_combat_tick_npc(w, jad);
    assert(w->player.num_pending_hits == 1);
    assert(w->player.pending_hits[0].attack_style != COMBAT_MELEE_CRUSH);
    jad->x = w->player.x;
    jad->y = w->player.y;
    jad->attack_timer = 0;
    w->player.num_pending_hits = 0;
    rc_combat_tick_npc(w, jad);
    assert(w->player.num_pending_hits == 1);
    assert(w->player.pending_hits[0].attack_style == COMBAT_MELEE_CRUSH);

    g_npc_defs[12].attack_speed = 5;
    g_npc_defs[12].attack_types = 0x01;
    g_npc_defs[12].max_hit = 10;
    g_npc_defs[12].stats[0] = 99;
    blood->active = true;
    blood->target_uid = 0;
    blood->attack_count = 5;
    blood->attack_timer = 0;
    w->player.num_pending_hits = 0;
    rc_combat_tick_npc(w, blood);
    assert(blood->attack_count == 6);
    assert(w->player.num_pending_hits >= 2);
    assert(w->player.pending_hits[0].damage == 12);

    int gen_enrage_idx = rc_npc_spawn(w, 18, w->player.x, w->player.y, 0);
    assert(gen_enrage_idx >= 0);
    RcNpc *gen_enrage = &w->npcs[gen_enrage_idx];
    g_npc_defs[18].attack_speed = 6;
    g_npc_defs[18].attack_types = 0x01;
    g_npc_defs[18].max_hit = 10;
    g_npc_defs[18].stats[0] = 99;
    gen_enrage->active = true;
    gen_enrage->target_uid = 0;
    gen_enrage->current_hp = 20;
    gen_enrage->attack_timer = 0;
    w->player.num_pending_hits = 0;
    rc_combat_tick_npc(w, gen_enrage);
    assert(gen_enrage->attack_timer == 4);

    int gen_drain_idx = rc_npc_spawn(w, 19, w->player.x, w->player.y, 0);
    assert(gen_drain_idx >= 0);
    RcNpc *gen_drain = &w->npcs[gen_drain_idx];
    w->player.current_prayer_points = 20;
    w->player.skills.boosted_level[SKILL_ATTACK] = 10;
    rc_queue_hit(w->player.pending_hits, &w->player.num_pending_hits,
                 7, 0, COMBAT_MAGIC, gen_drain->uid, 0, w->tick);
    rc_resolve_player_hits(w);
    assert(w->player.current_prayer_points == 15);
    assert(w->player.skills.boosted_level[SKILL_ATTACK] == 9);

    rc_world_destroy(w);
    printf("test_regular_npc_mechanics_combat: damage-rule consumers OK.\n");
    return 0;
}
