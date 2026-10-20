#define _POSIX_C_SOURCE 200809L
#include "api.h"
#include "combat.h"
#include "consumables.h"
#include "content.h"
#include "items.h"
#include "player_command.h"
#include "prayer.h"
#include "skills.h"
#include "storage.h"
#include "pathfinding.h"
#include "world_test_fixture.h"

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static RcWorld *make_world(void) {
    RcWorldConfig cfg = rc_preset_base_only();
    cfg.subsystems = RC_SUB_INVENTORY | RC_SUB_CONSUMABLES;
    cfg.items_path = RC_TEST_SOURCE_DIR "/data/defs/items.bin";
    cfg.members_world = true;
    RcWorld *w = rc_world_create_config(&cfg);
    assert(w && "consumable fixture must load B237 items");
    rc_content_consumables_register(w);
    assert(w->consumable_count > 100 && "all reviewed definitions must register");
    for (int i = 0; i < SKILL_COUNT; i++)
        w->player.skills.base_level[i] = w->player.skills.boosted_level[i] = 99;
    w->player.current_hp = w->player.max_hp = 990;
    return w;
}

static int add(RcWorld *w, int id) {
    assert(rc_player_inventory_add(w, id, 1, 0).code == RC_ITEM_RESULT_OK);
    int slot = rc_inv_find(w->player.inventory, id);
    assert(slot >= 0);
    return slot;
}

static RcConsumeResult consume(RcWorld *w, int slot, int drink) {
    assert(rc_consume_accepted(rc_player_consume_expected(w, slot,
        w->player.inventory[slot].generation, drink)));
    rc_world_tick(w);
    return w->player.consume_result;
}

static void doses_and_catalog(void) {
    RcWorld *w = make_world();
    int slot = add(w, 2434);
    for (int i = 1; i < RC_INVENTORY_SIZE; i++) add(w, 385);
    const int next[] = {139,141,143,229};
    for (int i = 0; i < 4; i++) {
        w->tick = w->player.potion_ready_tick;
        w->player.current_prayer_points = 0;
        uint32_t generation = w->player.inventory[slot].generation;
        assert(consume(w, slot, 1) == RC_CONSUME_OK);
        assert(w->player.inventory[slot].item_id == next[i]);
        assert(w->player.inventory[slot].generation != generation);
        assert(w->player.current_prayer_points == 310);
    }
    // Every reviewed identity executes and changes exactly one source slot.
    const RcConsumableDef *defs = w->consumables;
    int count = w->consumable_count;
    rc_world_destroy(w);
    w = make_world();
    for (int i = 0; i < count; i++) {
        rc_consumables_reset(&w->player);
        w->player.current_hp = 500;
        for (int s = 0; s < RC_INVENTORY_SIZE; s++)
            w->player.inventory[s] = (RcInvSlot){.item_id=-1};
        slot = add(w, defs[i].item_id);
        assert(consume(w, slot, defs[i].kind == RC_CONSUME_DRINK) == RC_CONSUME_OK);
        assert(w->player.inventory[slot].item_id == defs[i].replacement_id);
    }
    rc_world_destroy(w);
}

static void timing_and_order(void) {
    const int orders[6][3] = {{0,1,2},{0,2,1},{1,0,2},{1,2,0},{2,0,1},{2,1,0}};
    const int accepted[] = {3,2,2,2,1,1};
    for (int o = 0; o < 6; o++) {
        RcWorld *w = make_world();
        int slots[] = {add(w,385),add(w,2434),add(w,3144)};
        w->player.attack_timer = 4;
        for (int j = 0; j < 3; j++) {
            int k = orders[o][j];
            assert(rc_player_consume_expected(w,slots[k],
                w->player.inventory[slots[k]].generation,k == 1) == RC_CONSUME_QUEUED);
        }
        rc_world_tick(w);
        assert(w->player.consume_sequence == (uint64_t)accepted[o]);
        if (!o) assert(w->player.attack_timer == 9 && "food plus combo adds five, drink adds zero");
        rc_world_destroy(w);
    }
    RcWorld *w = make_world();
    int a = add(w,385), b = add(w,385);
    (void)b;
    assert(consume(w,a,0) == RC_CONSUME_OK);
    assert(!w->player.attack_timer && "idle food must not delay a ready attack");
    a = rc_inv_find(w->player.inventory,385);
    assert(consume(w,a,0) == RC_CONSUME_COOLDOWN);
    w->tick = w->player.food_ready_tick;
    w->player.attack_timer = 1;
    assert(consume(w,a,0) == RC_CONSUME_OK && w->player.attack_timer == 1);
    a = add(w,2289);
    w->tick = w->player.food_ready_tick;
    RcTick now = w->tick;
    assert(consume(w,a,0) == RC_CONSUME_OK);
    assert(w->player.inventory[a].item_id == 2291 && w->player.food_ready_tick == now+1);
    assert(consume(w,a,0) == RC_CONSUME_OK && w->player.food_ready_tick == now+3);
    rc_world_destroy(w);
}

static void effects_and_expiry(void) {
    RcWorld *w = make_world();
    RcPlayer *p = &w->player;
    p->current_hp = 500;
    assert(consume(w,add(w,385),0) == RC_CONSUME_OK && p->current_hp == 700);
    w->tick = p->food_ready_tick;
    p->current_hp = 990;
    assert(consume(w,add(w,13441),0) == RC_CONSUME_OK && p->current_hp == 1210);
    assert(consume(w,add(w,6685),1) == RC_CONSUME_OK);
    assert(p->current_hp == 1210 && "weaker overheal cannot lower HP");
    assert(p->skills.boosted_level[SKILL_DEFENCE] == 120);
    assert(p->skills.boosted_level[SKILL_ATTACK] == 88);
    w->tick = p->potion_ready_tick;
    assert(consume(w,add(w,3024),1) == RC_CONSUME_OK);
    assert(p->skills.boosted_level[SKILL_ATTACK] == 99);
    assert(p->skills.boosted_level[SKILL_DEFENCE] == 120);
    w->tick = p->potion_ready_tick;
    assert(consume(w,add(w,12695),1) == RC_CONSUME_OK);
    assert(p->skills.boosted_level[SKILL_ATTACK] == 118);
    w->tick = p->potion_ready_tick;
    assert(consume(w,add(w,2428),1) == RC_CONSUME_OK);
    assert(p->skills.boosted_level[SKILL_ATTACK] == 118);
    w->tick = p->potion_ready_tick;
    p->current_prayer_points = 0;
    add(w,6714);
    assert(consume(w,add(w,2434),1) == RC_CONSUME_OK && p->current_prayer_points == 330);
    p->equipment[EQUIP_RING] = (RcInvSlot){.item_id=13202,.quantity=1};
    assert(w->consumable_prayer_bonus(p) == 2 && "wrench bonuses do not stack");

    w->tick = p->potion_ready_tick;
    int divine = add(w,23685);
    p->current_hp = 100;
    assert(consume(w,divine,1) == RC_CONSUME_LOW_HP);
    assert(p->inventory[divine].item_id == 23685);
    p->current_hp = 500;
    RcTick start = w->tick;
    assert(consume(w,divine,1) == RC_CONSUME_OK && p->current_hp == 400);
    assert(p->divine_until[SKILL_ATTACK] == start+500);
    p->stat_boost_counter = 99;
    rc_stat_restore_tick(p);
    assert(p->skills.boosted_level[SKILL_ATTACK] == 118);
    w->tick = p->potion_ready_tick;
    assert(consume(w,add(w,6685),1) == RC_CONSUME_OK);
    assert(p->skills.boosted_level[SKILL_ATTACK] == 105 && p->divine_mask);
    w->tick = start+499;
    rc_consumables_tick(w);
    assert(p->divine_mask);
    w->tick++;
    rc_consumables_tick(w);
    assert(!p->divine_mask && p->skills.boosted_level[SKILL_ATTACK] == 99);
    p->run_energy = 1000;
    start = w->tick;
    assert(consume(w,add(w,12625),1) == RC_CONSUME_OK);
    assert(p->run_energy == 3024 && p->stamina_until == start+200); // sip plus ordinary recovery

    w->tick = p->food_ready_tick;
    p->current_hp = 200;
    start = w->tick;
    assert(consume(w,add(w,29143),0) == RC_CONSUME_OK && p->current_hp == 340);
    w->tick = start+6;
    rc_consumables_tick(w);
    assert(p->current_hp == 340);
    w->tick++;
    rc_consumables_tick(w);
    assert(p->current_hp == 460 && !p->delayed_food_heal);
    assert(consume(w,add(w,29143),0) == RC_CONSUME_OK);
    w->tick = p->food_ready_tick;
    assert(consume(w,add(w,29140),0) == RC_CONSUME_OK);
    assert(p->delayed_food_heal == 9 && "latest hunter food replaces pending heal");
    w->tick = p->delayed_food_tick;
    int before = p->current_hp;
    // Expiring delayed effects run before fresh input, so this heal is not lost.
    assert(consume(w,add(w,29143),0) == RC_CONSUME_OK && p->current_hp == before+230);
    rc_world_destroy(w);
}

// Antifire families feed the dragonfire tables (rc-content/combat/dragonfire.c).
static void antifire_timers(void) {
    RcWorld *w = make_world();
    RcPlayer *p = &w->player;
    RcTick start = w->tick;
    int potion = add(w,2452);
    assert(consume(w,potion,1) == RC_CONSUME_OK);
    assert(p->antifire_until == start+600 && !p->super_antifire_until);
    assert(p->inventory[potion].item_id == 2454 && "one dose is used");
    w->tick = p->potion_ready_tick;
    start = w->tick;
    assert(consume(w,add(w,22209),1) == RC_CONSUME_OK);
    assert(p->super_antifire_until == start+600 && p->antifire_until > w->tick);
    w->tick = p->potion_ready_tick;
    start = w->tick;
    int last = add(w,11957);
    assert(consume(w,last,1) == RC_CONSUME_OK);
    assert(p->antifire_until == start+1200 && p->inventory[last].item_id == 229);
    w->tick = p->potion_ready_tick;
    start = w->tick;
    assert(consume(w,add(w,21987),1) == RC_CONSUME_OK);
    assert(p->super_antifire_until == start+300);
    rc_combat_reset_player_life(w);
    assert(!p->antifire_until && !p->super_antifire_until);
    rc_world_destroy(w);
}

static void toxins_and_lifecycle(void) {
    RcWorld *w = make_world();
    RcPlayer *p = &w->player;
    rc_player_apply_toxin(w,2,false);
    assert(p->toxin_severity == 10 && p->toxin_tick == w->tick+30);
    rc_player_toxin_tick(w);
    assert(p->current_hp == 990);
    for (int i = 0; i < 5; i++) {
        w->tick = p->toxin_tick;
        rc_player_toxin_tick(w);
    }
    assert(p->current_hp == 890 && p->toxin_severity == 5);
    w->tick = p->toxin_tick;
    p->storage_kind = RC_STORAGE_BANK;
    rc_player_toxin_tick(w);
    assert(p->current_hp == 890);
    p->storage_kind = RC_STORAGE_NONE;
    rc_player_toxin_tick(w);
    assert(p->current_hp == 880);
    rc_player_apply_toxin(w,6,true);
    rc_player_apply_toxin(w,100,false);
    assert(p->toxin_venom && p->toxin_severity == 6);
    int anti = add(w,2446);
    assert(consume(w,anti,1) == RC_CONSUME_OK);
    assert(!p->toxin_venom && p->toxin_severity == 30 && !p->poison_immune_until);
    w->tick = p->potion_ready_tick;
    RcTick start = w->tick;
    assert(consume(w,anti,1) == RC_CONSUME_OK && !p->toxin_severity);
    assert(p->poison_immune_until == start+150);
    rc_player_apply_toxin(w,2,false);
    assert(!p->toxin_severity);
    w->tick = p->poison_immune_until;
    rc_player_apply_toxin(w,2,false);
    assert(p->toxin_severity == 10);
    rc_player_apply_toxin(w,6,true);
    assert(consume(w,add(w,12913),1) == RC_CONSUME_OK && !p->toxin_severity);
    rc_player_apply_toxin(w,6,true);
    assert(!p->toxin_severity);
    p->stamina_until = w->tick+200;
    p->divine_mask = 1;
    p->delayed_food_heal = 12;
    rc_combat_reset_player_life(w);
    assert(!p->stamina_until && !p->divine_mask && !p->delayed_food_heal);
    assert(!p->poison_immune_until && !p->venom_immune_until);
    assert(rc_world_reset(w));
    assert(!w->player.consume_sequence && !w->consumable_count);
    rc_content_consumables_register(w);
    assert(w->consumable_count > 100);
    rc_world_destroy(w);
}

static void failures_are_atomic(void) {
    RcWorld *w = make_world();
    int slot = add(w,385);
    uint32_t gen = w->player.inventory[slot].generation;
    assert(rc_player_consume_expected(NULL,slot,gen,0) == RC_CONSUME_INVALID);
    assert(rc_player_consume_expected(w,-1,gen,0) == RC_CONSUME_INVALID);
    assert(rc_player_consume_expected(w,slot,gen+1,0) == RC_CONSUME_STALE);
    assert(rc_player_consume_expected(w,slot,gen,1) == RC_CONSUME_INVALID);
    w->enabled &= ~RC_SUB_CONSUMABLES;
    assert(!rc_player_eat(w,slot) && w->player.consume_result == RC_CONSUME_DISABLED);
    w->enabled |= RC_SUB_CONSUMABLES;
    w->player.storage_kind = RC_STORAGE_BANK;
    assert(!rc_player_eat(w,slot) && w->player.consume_result == RC_CONSUME_BUSY);
    w->player.storage_kind = RC_STORAGE_NONE;
    assert(rc_player_eat(w,slot));
    RcItemTransaction tx;
    assert(rc_item_tx_begin(&tx,w).code == RC_ITEM_RESULT_OK);
    assert(rc_item_tx_remove_slot(&tx,slot,1,gen).code == RC_ITEM_RESULT_OK);
    assert(rc_item_tx_commit(&tx).code == RC_ITEM_RESULT_OK);
    add(w,385);
    rc_world_tick(w);
    assert(w->player.consume_result == RC_CONSUME_STALE && !w->player.consume_sequence);
    int unsupported = add(w,10925); // Sanfew needs disease rules; it must not silently consume.
    assert(!rc_player_drink(w,unsupported));
    assert(w->player.consume_result == RC_CONSUME_UNSUPPORTED);
    assert(w->player.inventory[unsupported].item_id == 10925);
    RcConsumableDef bad = *rc_consumable_get(w,385);
    bad.replacement_id = INT_MAX;
    assert(!rc_consumables_register(w,&bad,1));
    assert(rc_consumable_get(w,385) && "failed replacement must not overwrite valid registry");
    bad = *rc_consumable_get(w,385);
    bad.stats[SKILL_HITPOINTS].op = RC_STAT_BOOST;
    assert(!rc_consumables_register(w,&bad,1));
    w->members_world = false;
    assert(!rc_player_eat(w,slot) && w->player.consume_result == RC_CONSUME_MEMBERS);
    w->members_world = true;
    w->player.is_dead = true;
    assert(!rc_player_eat(w,slot) && w->player.consume_result == RC_CONSUME_BUSY);
    w->player.is_dead = false;
    w->player_action.active = true;
    w->player_action.category = RC_ACTION_CATEGORY_STRONG;
    w->player_action.ready_tick = w->tick+3;
    assert(!rc_player_eat(w,slot) && w->player.consume_result == RC_CONSUME_BUSY);
    w->player_action.active = false;
    w->player.skills.base_level[SKILL_ATTACK] = INT_MAX;
    assert(consume(w,slot,0) == RC_CONSUME_BAD_DATA);
    assert(w->player.inventory[slot].item_id == 385);
    w->player.skills.base_level[SKILL_ATTACK] = 99;
    w->player.equipment[EQUIP_HEAD] = (RcInvSlot){.item_id=INT_MAX,.quantity=1};
    int old_hp = w->player.current_hp;
    assert(consume(w,slot,0) == RC_CONSUME_ITEM_FAILURE);
    assert(w->player.current_hp == old_hp && !w->player.food_ready_tick);
    assert(w->player.inventory[slot].item_id == 385 && !w->player.consume_sequence);
    w->player.equipment[EQUIP_HEAD] = (RcInvSlot){.item_id=-1};
    for (int i = 0; i < RC_MAX_PLAYER_COMMANDS; i++) assert(rc_player_eat(w,slot));
    assert(!rc_player_eat(w,slot) && w->player.consume_result == RC_CONSUME_BUSY);
    rc_world_tick(w);
    assert(w->player.consume_sequence == 1 && "duplicate clicks consume at most once");
    for (int i = RC_CONSUME_OK; i <= RC_CONSUME_MEMBERS; i++)
        assert(rc_consume_result_message(i)[0] || i == RC_CONSUME_QUEUED);
    rc_world_destroy(w);
}

static void boundaries_and_refresh(void) {
    RcWorld *w = make_world();
    RcPlayer *p = &w->player;
    const int levels[] = {10,25,50,75,99}, heals[] = {3,6,11,15,22};
    for (int i = 0; i < 5; i++) {
        p->skills.base_level[SKILL_HITPOINTS] = levels[i];
        p->current_hp = p->max_hp = levels[i]*10;
        w->tick = p->food_ready_tick;
        assert(consume(w,add(w,13441),0) == RC_CONSUME_OK);
        assert(p->current_hp == (levels[i]+heals[i])*10);
    }
    p->active_prayers = 1u << RC_PRAYER_RAPID_HEAL;
    p->hp_regen_counter = 49;
    rc_stat_restore_tick(p);
    assert(p->current_hp == 1200 && "overheal shares the HP timer, not Preserve's stat timer");
    p->active_prayers = 0;
    p->current_hp = p->max_hp;
    int divine = add(w,23685);
    assert(consume(w,divine,1) == RC_CONSUME_OK);
    w->tick = p->potion_ready_tick;
    RcTick refresh = w->tick;
    assert(consume(w,divine,1) == RC_CONSUME_OK);
    assert(p->divine_until[SKILL_ATTACK] == refresh+500);
    p->skills.boosted_level[SKILL_ATTACK] = 90;
    w->tick = refresh+500;
    rc_consumables_tick(w);
    assert(p->skills.boosted_level[SKILL_ATTACK] == 90 && "expiry cannot restore a drained stat");
    int stamina = add(w,12625);
    assert(consume(w,stamina,1) == RC_CONSUME_OK);
    w->tick = p->potion_ready_tick;
    refresh = w->tick;
    assert(consume(w,stamina,1) == RC_CONSUME_OK && p->stamina_until == refresh+200);
    const int capes[] = {9759,9760,13280,13342};
    for (int i = 0; i < 4; i++) {
        p->equipment[EQUIP_CAPE] = (RcInvSlot){.item_id=capes[i],.quantity=1};
        assert(w->consumable_prayer_bonus(p) == 2);
    }
    p->equipment[EQUIP_CAPE] = (RcInvSlot){.item_id=-1};
    const int rings[] = {13202,25252,26764};
    for (int i = 0; i < 3; i++) {
        p->equipment[EQUIP_RING] = (RcInvSlot){.item_id=rings[i],.quantity=1};
        assert(w->consumable_prayer_bonus(p) == 2);
    }
    p->equipment[EQUIP_RING] = (RcInvSlot){.item_id=12601,.quantity=1};
    assert(!w->consumable_prayer_bonus(p) && "unimbued ring has no wrench effect");
    p->equipment[EQUIP_RING] = (RcInvSlot){.item_id=-1};
    rc_player_apply_toxin(w,6,true);
    for (int i = 0; i < 9; i++) {
        p->current_hp = 990;
        w->tick = p->toxin_tick;
        rc_player_toxin_tick(w);
    }
    assert(p->toxin_severity == 20 && p->current_hp == 790);
    rc_consumables_reset(p);
    p->current_hp = 500;
    assert(consume(w,add(w,29143),0) == RC_CONSUME_OK);
    rc_player_apply_toxin(w,2,false);
    w->tick = p->delayed_food_tick;
    rc_consumables_tick(w);
    assert(!p->toxin_severity && !p->poison_immune_until);
    rc_world_destroy(w);
}

static void movement_and_energy(void) {
    RcWorld *w = make_world();
    RcPlayer *p = &w->player;
    p->x = p->y = 3200;
    rc_test_open_mapsquare(w,3200,3200,0);
    assert(rc_player_run_to(w,3220,3200));
    rc_world_tick(w);
    p->run_energy = 5000;
    p->combat.target.kind = RC_COMBAT_ACTOR_NPC;
    p->combat.target.uid = 123;
    p->skill_action = 1;
    int food = add(w,385);
    assert(rc_player_eat(w,food));
    rc_world_tick(w);
    assert(p->x == 3204 && p->movement_step_count == 2);
    assert(p->combat.target.uid == 123 && !p->skill_action);
    int ordinary = 5000-p->run_energy;
    int energy = p->run_energy;
    assert(rc_player_drink(w,add(w,12625)));
    RcTick start = w->tick;
    rc_world_tick(w);
    assert(p->x == 3206 && p->run_energy == energy+2000-ordinary*30/100);
    assert(p->stamina_until == start+200);
    w->tick = p->stamina_until;
    energy = p->run_energy;
    rc_world_tick(w);
    assert(p->run_energy == energy-ordinary && "stamina expiry affects the actual movement reader");
    p->stamina_until = w->tick+200;
    RcTick expiry = p->stamina_until;
    assert(rc_world_relocate_player(w,3208,3200,0));
    assert(p->stamina_until == expiry && "relocation preserves player consumable state");
    rc_world_destroy(w);
}

static void support_report(void) {
    RcWorld *w = make_world();
    puts("item_id\tname\tsupport\treplacement_id");
    for (int id = 0; id < RC_MAX_ITEM_DEFS; id++) {
        const RcItemDef *item = rc_item_def_get(id);
        int action = 0;
        for (int op = 0; op < RC_ITEM_ACTION_COUNT; op++) action |= rc_consumable_action(id,op);
        if (!action) continue;
        const RcConsumableDef *d = rc_consumable_get(w,id);
        printf("%d\t%s\t%s\t%d\n",id,item->name,
            d ? "reviewed" : "unsupported: needs reviewed complete effect/area/quest rules",
            d ? d->replacement_id : -1);
    }
    rc_world_destroy(w);
}

static void deterministic_replay(void) {
    RcWorld *worlds[] = {make_world(),make_world()};
    const int ids[] = {385,6685,3024,23685,12625,29143,12913};
    for (int tick = 0; tick < 520; tick++) {
        for (int n = 0; n < 2; n++) {
            RcWorld *w = worlds[n];
            if (tick == 4) rc_player_apply_toxin(w,6,true);
            if (tick <= 18 && tick % 3 == 0) {
                int id = ids[tick/3], slot = add(w,id);
                assert(rc_player_consume_expected(w,slot,w->player.inventory[slot].generation,
                    rc_consumable_get(w,id)->kind == RC_CONSUME_DRINK) == RC_CONSUME_QUEUED);
            }
            rc_world_tick(w);
        }
        const RcPlayer *a = &worlds[0]->player, *b = &worlds[1]->player;
        assert(a->current_hp == b->current_hp && a->run_energy == b->run_energy);
        assert(a->toxin_severity == b->toxin_severity && a->toxin_tick == b->toxin_tick);
        assert(a->consume_sequence == b->consume_sequence && a->divine_mask == b->divine_mask);
        assert(!memcmp(a->inventory,b->inventory,sizeof(a->inventory)));
        assert(!memcmp(a->skills.boosted_level,b->skills.boosted_level,sizeof(a->skills.boosted_level)));
    }
    assert(worlds[0]->player.consume_sequence == 7);
    rc_world_destroy(worlds[1]);
    rc_world_destroy(worlds[0]);
}

static void self_damage_event(RcWorld *w, int event, const void *payload, void *ctx) {
    const RcPayloadPlayerDamaged *damage = payload;
    assert(event == RC_EVT_PLAYER_DAMAGED && damage->source_npc_id == UINT32_MAX);
    assert(damage->damage == 10 && damage->damage_tenths == 100);
    assert(damage->current_hp == w->player.current_hp);
    assert(damage->flags & RC_HIT_SUPPRESS_ENCOUNTER_EFFECTS);
    (*(int *)ctx)++;
}

static void consumption_damage_delivery(void) {
    RcWorld *w = make_world();
    int events = 0;
    assert(!rc_event_subscribe(w, RC_EVT_PLAYER_DAMAGED, self_damage_event, &events));
    int slot = add(w, 23685);
    w->player.current_hp = 100;
    assert(consume(w, slot, 1) == RC_CONSUME_LOW_HP);
    assert(!events && !w->player.combat.recent_hit_count);
    w->player.current_hp = 500;
    w->player.auto_retaliate = true;
    assert(consume(w, slot, 1) == RC_CONSUME_OK);
    assert(events == 1 && w->player.current_hp == 400);
    assert(w->player.combat.recent_hit_count == 1 && w->player.attack_target == -1);
    const RcCombatRecentHit *hit = &w->player.combat.recent_hits[0];
    assert(hit->damage == 10 && hit->source_uid == RC_HIT_SOURCE_PLAYER);
    assert(hit->hit_type == RC_HIT_TYPE_NORMAL && hit->style == COMBAT_NONE);
    assert(consume(w, slot, 1) == RC_CONSUME_COOLDOWN && events == 1);
    w->tick = w->player.food_ready_tick;
    assert(consume(w, add(w, 385), 0) == RC_CONSUME_OK);
    assert(events == 1 && w->player.current_hp == 600 && "healing must not emit damage");
    rc_world_destroy(w);
}

int main(int argc, char **argv) {
    if (argc == 2 && !strcmp(argv[1],"--report")) { support_report(); return 0; }
    doses_and_catalog();
    timing_and_order();
    effects_and_expiry();
    antifire_timers();
    toxins_and_lifecycle();
    failures_are_atomic();
    movement_and_energy();
    boundaries_and_refresh();
    deterministic_replay();
    consumption_damage_delivery();
    puts("consumables: doses, ordering, effects, antifire, toxins, expiry and atomic failures passed");
    return 0;
}
