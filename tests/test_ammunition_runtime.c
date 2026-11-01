#include "api.h"
#include "combat.h"
#include "config.h"
#include "items.h"
#include "npc.h"
#include "world_test_fixture.h"
#include "../rc-content/content.h"
#include "../rc-content/combat/ammunition.h"
#include "../rc-content/combat/magic.h"

#include "combat/gear.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static int invalid_delay(const RcWorld *world, const RcNpc *npc, const RcSpellDef *spell, int hit) {
    (void)world; (void)npc; (void)spell; (void)hit;
    return 256;
}

static void check_pair(RcPlayer *p, int weapon, int ammo, int expected) {
    assert(rc_item_def_get(weapon) && "weapon must exist in installed B237 data");
    assert(ammo < 0 || rc_item_def_get(ammo));
    p->equipment[EQUIP_WEAPON] = (RcInvSlot){weapon, 1};
    p->equipment[EQUIP_AMMO] = (RcInvSlot){ammo, ammo < 0 ? 0 : 10};
    const char *failure = NULL;
    int result = rc_content_ranged_resource_slot(p, &failure);
    if (result != expected)
        fprintf(stderr, "ammo rule failed: weapon=%d ammo=%d expected=%d got=%d reason=%s\n",
                weapon, ammo, expected, result, failure ? failure : "none");
    assert(result == expected);
    assert(result != RC_RANGED_RESOURCE_INVALID || (failure && *failure));
}

int main(void) {
    RcWorldConfig cfg = rc_preset_base_only();
    cfg.subsystems = RC_SUB_COMBAT | RC_SUB_INVENTORY | RC_SUB_EQUIPMENT;
    cfg.items_path = RC_TEST_SOURCE_DIR "/data/defs/items.bin";
    cfg.npc_defs_path = RC_TEST_SOURCE_DIR "/data/defs/npc_defs.bin";
    RcWorld *w = rc_world_create_config(&cfg);
    assert(w);
    rc_content_combat_register(w);
    RcPlayer *p = &w->player;
    check_pair(p, 841, 882, EQUIP_AMMO);
    check_pair(p, 841, 892, RC_RANGED_RESOURCE_INVALID);
    check_pair(p, 861, 892, EQUIP_AMMO);
    check_pair(p, 861, 21326, EQUIP_AMMO);
    check_pair(p, 861, 11212, RC_RANGED_RESOURCE_INVALID);
    check_pair(p, 20997, 11212, EQUIP_AMMO);
    check_pair(p, 27612, 11212, EQUIP_AMMO);
    check_pair(p, 27610, 11212, EQUIP_AMMO);
    check_pair(p, 9185, 9144, EQUIP_AMMO);
    check_pair(p, 9185, 9243, EQUIP_AMMO);
    check_pair(p, 9185, 892, RC_RANGED_RESOURCE_INVALID);
    check_pair(p, 9185, 21905, RC_RANGED_RESOURCE_INVALID);
    check_pair(p, 11785, 21905, EQUIP_AMMO);
    check_pair(p, 8880, 8882, EQUIP_AMMO);
    check_pair(p, 8880, 9140, EQUIP_AMMO);
    check_pair(p, 8880, 9145, RC_RANGED_RESOURCE_INVALID);
    check_pair(p, 8880, 9236, RC_RANGED_RESOURCE_INVALID);
    check_pair(p, 9185, 8882, RC_RANGED_RESOURCE_INVALID);
    check_pair(p, 4734, 4740, EQUIP_AMMO);
    check_pair(p, 4734, 9144, RC_RANGED_RESOURCE_INVALID);
    check_pair(p, 19478, 825, EQUIP_AMMO);
    check_pair(p, 19481, 19484, EQUIP_AMMO);
    check_pair(p, 19481, 11212, RC_RANGED_RESOURCE_INVALID);
    check_pair(p, 10156, 10158, EQUIP_AMMO);
    check_pair(p, 28869, 28878, EQUIP_AMMO);
    check_pair(p, 29000, 28991, EQUIP_AMMO);
    check_pair(p, 2883, 2866, EQUIP_AMMO);
    check_pair(p, 2883, 4803, RC_RANGED_RESOURCE_INVALID);
    check_pair(p, 4827, 4803, EQUIP_AMMO);
    check_pair(p, 10149, 10142, EQUIP_AMMO);
    check_pair(p, 10149, 10143, RC_RANGED_RESOURCE_INVALID);
    check_pair(p, 806, -1, EQUIP_WEAPON);
    check_pair(p, 10034, -1, EQUIP_WEAPON);
    check_pair(p, 28919, -1, RC_RANGED_RESOURCE_NONE);
    check_pair(p, 28922, 892, RC_RANGED_RESOURCE_NONE);
    check_pair(p, 25867, 892, RC_RANGED_RESOURCE_NONE);
    check_pair(p, 12926, 892, RC_RANGED_RESOURCE_INVALID);
    check_pair(p, 25865, 892, RC_RANGED_RESOURCE_INVALID);
    check_pair(p, 861, -1, RC_RANGED_RESOURCE_INVALID);
    check_pair(p, 861, 995, RC_RANGED_RESOURCE_INVALID);
    check_pair(p, 4151, 892, RC_RANGED_RESOURCE_INVALID);
    const char *failure = NULL;
    assert(rc_content_ranged_resource_slot(NULL, &failure) == RC_RANGED_RESOURCE_INVALID);

    p->x = p->y = 3208;
    rc_test_open_mapsquare(w, p->x, p->y, 0);
    int def_index = rc_npc_def_find(3014);
    assert(def_index >= 0);
    int index = rc_npc_spawn(w, def_index, p->x + 3, p->y, 0);
    assert(index >= 0);
    RcNpc *npc = &w->npcs[index];
    npc->current_hp = npc->spawn_hp = 10000;
    npc->disable_wander = true;
    for (int i = 0; i < SKILL_COUNT; i++)
        p->skills.base_level[i] = p->skills.boosted_level[i] = 99;
    check_pair(p, 9185, 892, RC_RANGED_RESOURCE_INVALID);
    rc_recalc_bonuses(p);
    assert(rc_combat_start_player_vs_npc(w, 0, npc->uid));
    uint32_t rng = w->rng_state;
    rc_combat_tick_player(w);
    assert(p->combat.failure_reason && w->combat_attack_event_count == 0);
    assert(w->rng_state == rng && p->equipment[EQUIP_AMMO].quantity == 10);
    check_pair(p, 9185, 9144, EQUIP_AMMO);
    rc_recalc_bonuses(p);
    assert(rc_combat_start_player_vs_npc(w, 0, npc->uid));
    rc_combat_tick_player(w);
    assert(w->combat_attack_event_count == 1 && p->equipment[EQUIP_AMMO].quantity == 9);

    check_pair(p, 25867, 11212, RC_RANGED_RESOURCE_NONE);
    rc_recalc_bonuses(p);
    RcCombatCalc no_arrows = rc_calc_ranged(p, npc, false);
    RcCombatCalc with_arrows = rc_calc_ranged(p, npc, true);
    assert(no_arrows.max_hit < with_arrows.max_hit);
    p->attack_timer = 0;
    assert(rc_combat_start_player_vs_npc(w, 0, npc->uid));
    rc_combat_tick_player(w);
    assert(w->combat_attack_event_count == 2 && p->equipment[EQUIP_AMMO].quantity == 10);
    check_pair(p, 806, 892, EQUIP_WEAPON);
    p->equipment[EQUIP_WEAPON].quantity = 2;
    p->attack_timer = 0;
    rc_recalc_bonuses(p);
    assert(rc_combat_start_player_vs_npc(w, 0, npc->uid));
    rc_combat_tick_player(w);
    assert(w->combat_attack_event_count == 3 && p->equipment[EQUIP_WEAPON].quantity == 1);
    assert(p->equipment[EQUIP_AMMO].quantity == 10);
    for (int special = 0; special < 2; special++) {
        for (int arrows = 1; arrows <= 2; arrows++) {
            if (special && arrows == 1) continue;
            check_pair(p, 11235, 11212, EQUIP_AMMO);
            p->equipment[EQUIP_AMMO].quantity = arrows;
            rc_recalc_bonuses(p);
            p->attack_timer = 0;
            p->special_energy = 10000;
            p->combat.special_pending = special;
            npc->num_pending_hits = 0;
            w->combat_attack_event_count = 0;
            assert(rc_combat_start_player_vs_npc(w, 0, npc->uid));
            rc_combat_tick_player(w);
            assert(npc->num_pending_hits == arrows);
            assert(w->combat_attack_events[0].hit_count == arrows);
            for (int h = 0; h < arrows; h++)
                assert(npc->pending_hits[h].apply_tick == w->tick + w->combat_attack_events[0].hit_delays[h]);
            if (arrows == 2) assert(npc->pending_hits[1].apply_tick >= npc->pending_hits[0].apply_tick);
            assert(p->equipment[EQUIP_AMMO].item_id == -1);
            assert(p->special_energy == (special ? 4500 : 10000));
            if (special)
                for (int i = 0; i < arrows; i++) assert(npc->pending_hits[i].damage >= 8);
        }
    }
    check_pair(p, 20997, 11212, EQUIP_AMMO);
    rc_recalc_bonuses(p);
    p->attack_style_idx = 3;
    rc_refresh_player_combat_style(p);
    assert(rc_player_attack_range(p) == 10);
    p->equipment[EQUIP_WEAPON] = (RcInvSlot){.item_id = 33330, .quantity = 1};
    rc_recalc_bonuses(p);
    assert(rc_player_attack_range(p) == 1 && "staff melee cannot use its casting reach");
    for (int mode = 0; mode < 3; mode++) {
        check_pair(p, 10148, 10145, EQUIP_AMMO);
        p->attack_style_idx = mode;
        rc_recalc_bonuses(p);
        assert(rc_player_attack_range(p) == 1);
        npc->x = p->x + 1;
        npc->num_pending_hits = w->combat_attack_event_count = 0;
        p->attack_timer = 0;
        assert(rc_combat_start_player_vs_npc(w, 0, npc->uid));
        rc_combat_tick_player(w);
        assert(w->combat_attack_event_count == 1 && p->equipment[EQUIP_AMMO].quantity == 9);
    }
    check_pair(p, 29000, 28991, EQUIP_AMMO);
    p->attack_style_idx = 0;
    rc_recalc_bonuses(p);
    p->equipment_bonuses[EQ_STR] = 64;
    p->skills.boosted_level[SKILL_STRENGTH] = 1;
    RcCombatCalc low = rc_calc_ranged(p, npc, true);
    RcGearAttack gear;
    rc_content_gear_attack(&gear, w, npc, NULL, false);
    rc_content_gear_ranged(&gear, &low);
    p->skills.boosted_level[SKILL_STRENGTH] = 99;
    RcCombatCalc high = rc_calc_ranged(p, npc, true);
    rc_content_gear_attack(&gear, w, npc, NULL, false);
    rc_content_gear_ranged(&gear, &high);
    assert(low.max_hit == 2 && high.max_hit == 22);
    assert(low.attack_roll == high.attack_roll);
    check_pair(p, 11235, 11212, EQUIP_AMMO);
    p->attack_style_idx = 0;
    rc_recalc_bonuses(p);
    npc->x = p->x + 3;
    assert(rc_content_player_hit_delay(w, npc, NULL, 0) == 3);
    assert(rc_content_player_hit_delay(w, npc, NULL, 1) == 3);
    npc->x = p->x + 6;
    assert(rc_content_player_hit_delay(w, npc, NULL, 0) == 3);
    assert(rc_content_player_hit_delay(w, npc, NULL, 1) == 4);
    npc->num_pending_hits = w->combat_attack_event_count = 0;
    p->attack_timer = 0;
    w->combat_hooks.player_hit_delay = invalid_delay;
    rng = w->rng_state;
    assert(rc_combat_start_player_vs_npc(w, 0, npc->uid));
    rc_combat_tick_player(w);
    assert(w->rng_state == rng && !npc->num_pending_hits && !w->combat_attack_event_count);
    assert(p->equipment[EQUIP_AMMO].quantity == 10 && p->combat.failure_reason);
    w->combat_hooks.player_hit_delay = rc_content_player_hit_delay;
    check_pair(p, 10034, -1, EQUIP_WEAPON);
    rc_recalc_bonuses(p);
    npc->x = p->x + 8;
    for (int fuse = 0; fuse < 3; fuse++) {
        p->attack_style_idx = fuse;
        RcCombatCalc calc = {.attack_roll = 400, .defence_roll = 100};
        rc_content_ranged_calc(w, npc, &calc);
        assert(calc.attack_roll == 200 + fuse * 100);
    }
    rc_world_destroy(w);
    puts("ammunition: B237 compatibility, UI-independent launch and resource ownership passed");
    return 0;
}
