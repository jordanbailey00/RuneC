#include "api.h"
#include "combat.h"
#include "config.h"
#include "items.h"
#include "npc.h"
#include "storage.h"
#include "storage_fixture.h"
#include "world_test_fixture.h"
#include "../rc-content/content.h"
#include "../rc-viewer/combat_visuals.h"

#include <assert.h>
#include <stdio.h>

static int reject_charge(RcWorld *world, int weapon_id) {
    (void)world;
    (void)weapon_id;
    return 0;
}

static void equip(RcWorld *w, int id, uint32_t charges) {
    RcPlayer *p = &w->player;
    p->equipment[EQUIP_WEAPON] = (RcInvSlot){.item_id = -1};
    for (int i = 0; i < RC_INVENTORY_SIZE; i++)
        p->inventory[i] = (RcInvSlot){.item_id = -1};
    assert(rc_inv_add_state(p->inventory, id, 1, charges) == 0);
    assert(rc_item_result_accepted(rc_player_equip(w, 0)));
    rc_world_tick(w);
    assert(p->equipment[EQUIP_WEAPON].item_id == id);
}

static void launch(RcWorld *w, RcNpc *n, bool special, int hits) {
    n->num_pending_hits = 0;
    w->combat_attack_event_count = 0;
    w->player.attack_timer = 0;
    assert(rc_combat_start_player_vs_npc(w, 0, n->uid));
    w->player.combat.special_pending = special;
    rc_combat_tick_player(w);
    assert(n->num_pending_hits == hits);
    assert(w->combat_attack_event_count == (hits > 0 ? 1 : 0));
    assert((w->player.combat.failure_reason == NULL) == (hits > 0));
}

int main(void) {
    assert(rc_load_combat_visuals(RC_TEST_SOURCE_DIR "/data/defs/combat_visuals.tsv") > 0);
    const RcCombatVisualDef *normal = rc_combat_visual_for_item_stance(28922, COMBAT_RANGED, 0);
    const RcCombatVisualDef *special = rc_combat_visual_for_special_item(28922, COMBAT_RANGED);
    assert(normal && normal->attack_anim_id == 10923 && normal->projectile_count == 2);
    assert(normal->projectile_model_id == 51229 && normal->travel_spotanim_id == 2729);
    assert(special && special->attack_anim_id == 10914 && special->travel_spotanim_id == 2727);
    RcWorldConfig cfg = rc_preset_base_only();
    cfg.subsystems = RC_SUB_COMBAT | RC_SUB_INVENTORY | RC_SUB_EQUIPMENT | RC_SUB_STORAGE;
    cfg.items_path = RC_TEST_SOURCE_DIR "/data/defs/items.bin";
    cfg.npc_defs_path = RC_TEST_SOURCE_DIR "/data/defs/npc_defs.bin";
    cfg.combat_profiles_path = RC_TEST_SOURCE_DIR "/data/defs/combat_visuals.tsv";
    RcWorld *w = rc_world_create_config(&cfg);
    assert(w);
    rc_content_combat_register(w);
    RcPlayer *p = &w->player;
    for (int i = 0; i < SKILL_COUNT; i++)
        p->skills.base_level[i] = p->skills.boosted_level[i] = 99;
    p->x = p->y = 3208;
    rc_test_open_mapsquare(w, p->x, p->y, 0);
    int index = rc_npc_spawn(w, rc_npc_def_find(3014), p->x + 3, p->y, 0);
    assert(index >= 0);
    RcNpc *n = &w->npcs[index];
    n->current_hp = n->spawn_hp = 10000;
    n->disable_wander = true;
    n->force_player_max_hit = true;
    n->stats[1] = n->stats[5] = 100;
    equip(w, 28919, 0);
    p->attack_style_idx = 0;
    rc_refresh_player_combat_style(p);
    assert(p->combat_style == COMBAT_RANGED);
    assert(rc_player_attack_speed(p) == 7 && rc_player_attack_range(p) == 6);
    p->attack_style_idx = 1;
    rc_refresh_player_combat_style(p);
    assert(rc_player_attack_speed(p) == 6);
    p->attack_style_idx = 3;
    rc_refresh_player_combat_style(p);
    assert(rc_player_attack_range(p) == 8);
    p->attack_style_idx = 0;
    rc_refresh_player_combat_style(p);
    launch(w, n, false, 1);
    RcCombatCalc base = rc_calc_ranged(p, n, false);
    assert(n->pending_hits[0].damage == base.max_hit * 3 / 4);
    assert(p->equipment[EQUIP_WEAPON].quantity == 1);
    n->num_pending_hits = 0;
    assert(rc_item_result_accepted(rc_player_unequip(w, EQUIP_WEAPON)));
    rc_world_tick(w);
    int weapon = rc_inv_find(p->inventory, 28919);
    assert(weapon >= 0);
    int splinters = rc_inv_add(p->inventory, 28924, 3);
    assert(splinters >= 0);
    assert(rc_player_use_inventory_item_on_inventory_item(w, splinters, weapon));
    rc_world_tick(w);
    assert(p->inventory[weapon].item_id == 28922);
    assert(p->inventory[weapon].state_id == 3);
    assert(rc_inv_find(p->inventory, 28924) == -1);
    test_storage_open(w);
    assert(rc_bank_deposit_slot(w, weapon, 1) == 1);
    rc_world_tick(w);
    int bank = -1;
    for (int i = 0; i < RC_BANK_SIZE; i++)
        if (p->bank[i].item_id == 28922) bank = i;
    assert(bank >= 0 && p->bank[bank].state_id == 3);
    assert(rc_bank_withdraw_slot(w, bank, 1) == 1);
    rc_world_tick(w);
    weapon = rc_inv_find(p->inventory, 28922);
    assert(weapon >= 0 && p->inventory[weapon].state_id == 3);
    assert(rc_item_result_accepted(rc_player_equip(w, weapon)));
    rc_world_tick(w);
    launch(w, n, false, 2);
    assert(n->pending_hits[0].damage == n->pending_hits[1].damage);
    assert(p->equipment[EQUIP_WEAPON].state_id == 2);
    p->special_energy = 10000;
    n->stats[1] = n->stats[5] = 100;
    launch(w, n, true, 2);
    assert(p->special_energy == 5000 && p->equipment[EQUIP_WEAPON].state_id == 1);
    assert(n->pending_hits[0].defence_drain == 12 && n->pending_hits[1].defence_drain == 12);
    assert(n->stats[1] == 100 && "drains are applied on impact, not selection");
    RcTick deadline = n->pending_hits[0].apply_tick;
    p->attack_target = -1;
    while (w->tick <= deadline) rc_world_tick(w);
    assert(n->stats[1] == 76);
    launch(w, n, false, 2);
    assert(p->equipment[EQUIP_WEAPON].item_id == 28919);
    assert(p->equipment[EQUIP_WEAPON].state_id == 0);
    launch(w, n, false, 1);

    equip(w, 28922, 2);
    RcPendingHit single_slot[1] = {{0}};
    const char *failure = NULL;
    base = rc_calc_ranged(p, n, false);
    assert(w->combat_hooks.prepare_player_hits(w, n, &base, false,
            single_slot, 1, &failure) == -1);
    assert(failure && !single_slot[0].active);
    n->num_pending_hits = RC_MAX_PENDING_HITS - 1;
    w->combat_attack_event_count = 0;
    p->attack_timer = 0;
    assert(rc_combat_start_player_vs_npc(w, 0, n->uid));
    uint32_t rng = w->rng_state;
    rc_combat_tick_player(w);
    assert(n->num_pending_hits == RC_MAX_PENDING_HITS - 1);
    assert(w->combat_attack_event_count == 0 && w->rng_state == rng);
    assert(p->equipment[EQUIP_WEAPON].state_id == 2 && p->attack_timer == 0);
    n->num_pending_hits = 0;

    int (*consume_charge)(RcWorld *, int) = w->combat_hooks.consume_weapon_charge;
    w->combat_hooks.consume_weapon_charge = reject_charge;
    rng = w->rng_state;
    launch(w, n, true, 0);
    assert(p->equipment[EQUIP_WEAPON].state_id == 2 && w->rng_state == rng);
    assert(p->special_energy == 5000 && p->attack_timer == 0);
    w->combat_hooks.consume_weapon_charge = consume_charge;

    n->force_player_max_hit = false;
    n->stats[1] = 100;
    int split_rolls = 0, misses = 0;
    for (int i = 0; i < 64; i++) {
        p->equipment[EQUIP_WEAPON].state_id = 2;
        p->special_energy = 10000;
        launch(w, n, true, 2);
        const RcPendingHit *a = &n->pending_hits[0], *b = &n->pending_hits[1];
        split_rolls += a->damage != b->damage || a->accurate != b->accurate;
        for (int j = 0; j < 2; j++) {
            if (!n->pending_hits[j].accurate) {
                misses++;
                assert(n->pending_hits[j].damage == 0);
                assert(n->pending_hits[j].defence_drain == 0);
            }
        }
    }
    assert(split_rolls > 0 && misses > 0);
    n->force_player_max_hit = true;
    n->stats[1] = 5;
    p->special_energy = 10000;
    launch(w, n, true, 2);
    assert(n->pending_hits[0].defence_drain == 5);
    assert(n->pending_hits[1].defence_drain == 0);
    n->num_pending_hits = 0;
    equip(w, 28922, 0);
    launch(w, n, false, 0);
    equip(w, 28922, 20001);
    launch(w, n, false, 0);
    equip(w, 28922, 4);
    assert(rc_item_result_accepted(rc_player_unequip(w, EQUIP_WEAPON)));
    rc_world_tick(w);
    weapon = rc_inv_find(p->inventory, 28922);
    assert(rc_player_interact_inventory_item(w, weapon, 4));
    rc_world_tick(w);
    assert(p->inventory[weapon].item_id == 28919 && p->inventory[weapon].state_id == 0);
    assert(p->inventory[rc_inv_find(p->inventory, 28924)].quantity == 4);

    assert(rc_player_use_inventory_item_on_inventory_item(w, weapon,
            rc_inv_find(p->inventory, 28924)));
    rc_world_tick(w);
    assert(p->inventory[weapon].state_id == 4);
    assert(rc_player_interact_inventory_item(w, weapon, 2));
    rc_world_tick(w);
    assert(p->inventory[weapon].state_id == 4);
    assert(rc_player_interact_inventory_item(w, weapon, 3));
    rc_world_tick(w);
    assert(p->inventory[weapon].state_id == 4 && "missing splinters must not create charges");
    splinters = rc_inv_add(p->inventory, 28924, 20000);
    assert(splinters >= 0);
    assert(rc_player_interact_inventory_item(w, weapon, 3));
    rc_world_tick(w);
    assert(p->inventory[weapon].state_id == 20000);
    assert(p->inventory[splinters].quantity == 4);
    assert(rc_player_interact_inventory_item(w, weapon, 3));
    rc_world_tick(w);
    assert(p->inventory[splinters].quantity == 4);

    // Returning charges must not destroy them when the inventory is full.
    p->inventory[splinters] = (RcInvSlot){.item_id = -1};
    for (int i = 0; i < RC_INVENTORY_SIZE; i++) {
        if (p->inventory[i].item_id < 0)
            assert(rc_inv_add(p->inventory, 1351, 1) >= 0);
    }
    assert(rc_player_interact_inventory_item(w, weapon, 4));
    rc_world_tick(w);
    assert(p->inventory[weapon].item_id == 28922);
    assert(p->inventory[weapon].state_id == 20000);
    assert(rc_inv_find(p->inventory, 28924) == -1);
    p->inventory[weapon].state_id = 20001;
    assert(rc_player_interact_inventory_item(w, weapon, 2));
    rc_world_tick(w);
    assert(p->inventory[weapon].state_id == 20001);

    // Existing Partisan categories must offer their four real attack modes.
    const int partisans[] = {25979, 25981, 27287, 27291};
    for (unsigned i = 0; i < sizeof(partisans) / sizeof(partisans[0]); i++) {
        equip(w, partisans[i], 0);
        for (int stance = 0; stance < 4; stance++) {
            p->attack_style_idx = stance;
            rc_refresh_player_combat_style(p);
            assert(p->combat_style == (stance == 2 ? COMBAT_MELEE_CRUSH : COMBAT_MELEE_STAB));
        }
    }

    rc_world_destroy(w);
    puts("ralos: equipment, charge persistence, launch atomicity, hits, speed/range and Division passed");
    return 0;
}
