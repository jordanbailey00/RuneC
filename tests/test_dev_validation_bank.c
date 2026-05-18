#include <assert.h>
#include <string.h>

#include "../rc-core/storage.h"
#include "../rc-viewer/dev_validation.c"

#define ITEM_PATH RC_TEST_SOURCE_DIR "/data/defs/items.bin"
#define SPELL_PATH RC_TEST_SOURCE_DIR "/data/defs/spells.bin"
#define VISUALS_PATH RC_TEST_SOURCE_DIR "/data/defs/combat_visuals.tsv"

static int find_item_by_name(const char *name) {
    for (int i = 0; i < RC_MAX_ITEM_DEFS; i++) {
        const RcItemDef *def = rc_item_def_get(i);
        if (def && strcmp(def->name, name) == 0 && !def->noted
                && !def->placeholder) {
            return def->id;
        }
    }
    return -1;
}

static void assert_validation_item_equips(RcWorld *world, const char *name,
                                          int equip_slot) {
    int item_id = find_item_by_name(name);
    assert(item_id >= 0);
    const RcItemDef *def = rc_item_def_get(item_id);
    assert(def);
    assert(def->equippable);
    assert(def->equip_slot == equip_slot);
    memset(world->player.inventory, 0xff, sizeof(world->player.inventory));
    memset(world->player.equipment, 0xff, sizeof(world->player.equipment));
    assert(rc_inv_add(world->player.inventory, item_id, 1) == 0);
    rc_player_equip(world, 0);
    assert(world->player.inventory[0].item_id == -1);
    assert(world->player.equipment[equip_slot].item_id == item_id);
}

int main(void) {
    RcWorldConfig cfg = rc_preset_base_only();
    cfg.subsystems = RC_SUB_INVENTORY | RC_SUB_EQUIPMENT |
                     RC_SUB_STORAGE | RC_SUB_COMBAT;
    cfg.items_path = ITEM_PATH;
    cfg.spells_path = SPELL_PATH;
    cfg.combat_visuals_path = VISUALS_PATH;
    cfg.seed = 12345;
    RcWorld *world = rc_world_create_config(&cfg);
    assert(world);
    for (int i = 0; i < SKILL_COUNT; i++)
        world->player.skills.base_level[i] = 99;

    runec_dev_validation_seed_bank(world);

    int tab_seen[5] = {0};
    int used = 0;
    int stack_slot = -1;
    int gear_slot = -1;
    for (int i = 0; i < RC_BANK_SIZE; i++) {
        RcInvSlot *slot = &world->player.bank[i];
        if (slot->item_id < 0)
            continue;
        const RcItemDef *def = rc_item_def_get(slot->item_id);
        assert(def);
        assert(!def->noted);
        assert(!def->placeholder);
        assert(slot->quantity >= (def->stackable ? 1000 : 2));
        assert(world->player.bank_tab[i] < 5);
        tab_seen[world->player.bank_tab[i]]++;
        used++;
        if (def->stackable && stack_slot < 0)
            stack_slot = i;
        if (!def->stackable && gear_slot < 0)
            gear_slot = i;
    }
    assert(used > 300);
    for (int i = 0; i < 5; i++)
        assert(tab_seen[i] > 0);

    world->player.storage_kind = RC_STORAGE_BANK;
    assert(stack_slot >= 0);
    int stack_before = world->player.bank[stack_slot].quantity;
    int stack_withdraw =
        runec_dev_validation_bank_withdraw_quantity(world, stack_slot);
    assert(stack_withdraw == stack_before - 1);
    assert(rc_bank_withdraw_slot(world, stack_slot, stack_withdraw)
           == stack_withdraw);
    assert(world->player.bank[stack_slot].quantity == 1);

    memset(world->player.inventory, 0xff, sizeof(world->player.inventory));
    assert(gear_slot >= 0);
    int gear_before = world->player.bank[gear_slot].quantity;
    int gear_withdraw =
        runec_dev_validation_bank_withdraw_quantity(world, gear_slot);
    assert(gear_withdraw == gear_before - 1);
    assert(rc_bank_withdraw_slot(world, gear_slot, gear_withdraw)
           == gear_withdraw);
    assert(world->player.bank[gear_slot].quantity == 1);

    assert_validation_item_equips(world, "Oathplate helm", EQUIP_HEAD);
    assert_validation_item_equips(world, "Oathplate chest", EQUIP_BODY);
    assert_validation_item_equips(world, "Oathplate legs", EQUIP_LEGS);
    assert_validation_item_equips(world, "Avernic treads", EQUIP_BOOTS);
    assert_validation_item_equips(world, "Confliction gauntlets", EQUIP_GLOVES);
    assert_validation_item_equips(world, "Twinflame staff", EQUIP_WEAPON);

    int dummy_idx = runec_dev_validation_spawn_varrock_bank_dummy(world);
    assert(dummy_idx >= 0);
    RcNpc *dummy = &world->npcs[dummy_idx];
    int dummy_x = dummy->x;
    int dummy_y = dummy->y;
    assert(dummy->disable_wander);
    assert(dummy->force_player_max_hit);
    for (int i = 0; i < 64; i++)
        rc_npc_tick(world, dummy);
    assert(dummy->x == dummy_x);
    assert(dummy->y == dummy_y);

    const RuneCDevTransport *kbd =
        runec_dev_validation_find_transport("kbd");
    assert(kbd && kbd->npc_id == 2266);

    const RuneCDevTransport *graardor =
        runec_dev_validation_find_transport("graardor");
    assert(graardor);
    int encounter_count = 0;
    const RuneCDevEncounterNpc *encounter =
        runec_dev_validation_encounter_npcs(graardor, &encounter_count);
    assert(encounter && encounter_count == 4);
    assert(encounter[0].npc_id == 2215);
    assert(encounter[1].npc_id == 2216);
    assert(encounter[2].npc_id == 2217);
    assert(encounter[3].npc_id == 2218);

    world->player.x = 2872;
    world->player.y = 5350;
    world->player.plane = 2;
    int prepared = runec_dev_validation_prepare_encounter(world, graardor);
    assert(prepared == 4);
    assert(rc_combat_is_multi_combat(world));
    for (int i = 0; i < encounter_count; i++) {
        int idx = rc_world_find_npc_near(world, encounter[i].npc_id,
                                         encounter[i].x, encounter[i].y,
                                         encounter[i].plane, 0);
        assert(idx >= 0);
        RcNpc *npc = &world->npcs[idx];
        assert(npc->disable_wander);
        assert(npc->target_uid == 0);
        assert(npc->attack_timer == 0);
    }

    rc_world_destroy(world);
    return 0;
}
