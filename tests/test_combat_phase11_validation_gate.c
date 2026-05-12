#include "api.h"
#include "combat.h"
#include "items.h"
#include "npc.h"
#include "spells.h"

#include <assert.h>
#include <string.h>

enum {
    TEST_BOW = 1100,
    TEST_ARROW = 1101,
    TEST_FIRE_RUNE = 1102,
    TEST_AIR_RUNE = 1103,
};

static void reset_defs(void) {
    memset(g_item_defs, 0, sizeof(g_item_defs));
    g_item_def_count = 0;
    memset(g_rc_spell_defs, 0, sizeof(g_rc_spell_defs));
    g_rc_spell_count = 0;
    memset(g_npc_defs, 0, sizeof(g_npc_defs));
    g_npc_def_count = 0;
}

static RcItemDef *add_item(int id, const char *name) {
    RcItemDef *def = &g_item_defs[id];
    memset(def, 0, sizeof(*def));
    def->id = id;
    def->equip_slot = -1;
    def->loaded = true;
    strncpy(def->name, name, sizeof(def->name) - 1);
    g_item_def_count++;
    return def;
}

static void add_resource_defs(void) {
    RcItemDef *bow = add_item(TEST_BOW, "Phase 11 bow");
    bow->equippable = true;
    bow->equipable_by_player = true;
    bow->equipable_weapon = true;
    bow->equip_slot = EQUIP_WEAPON;
    bow->attack_ranged = 1000;
    bow->attack_speed = 4;
    bow->attack_range = 7;
    bow->weapon_type = 25;

    RcItemDef *arrow = add_item(TEST_ARROW, "Phase 11 arrow");
    arrow->stackable = true;
    arrow->equippable = true;
    arrow->equipable_by_player = true;
    arrow->equip_slot = EQUIP_AMMO;
    arrow->ranged_strength = 100;

    RcItemDef *fire = add_item(TEST_FIRE_RUNE, "Phase 11 fire rune");
    fire->stackable = true;
    RcItemDef *air = add_item(TEST_AIR_RUNE, "Phase 11 air rune");
    air->stackable = true;

    g_rc_spell_count = 1;
    RcSpellDef *spell = &g_rc_spell_defs[0];
    memset(spell, 0, sizeof(*spell));
    strcpy(spell->name, "Phase 11 Fire Spell");
    spell->type = RC_SPELL_TYPE_COMBAT;
    spell->max_hit = 20;
    spell->rune_count = 2;
    spell->runes[0] = (RcSpellRune){TEST_FIRE_RUNE, 2};
    spell->runes[1] = (RcSpellRune){TEST_AIR_RUNE, 1};
    spell->loaded = 1;
}

static int add_target_def(void) {
    int idx = g_npc_def_count++;
    assert(idx < RC_MAX_NPC_DEFS);
    RcNpcDef *def = &g_npc_defs[idx];
    memset(def, 0, sizeof(*def));
    def->id = 911100;
    strcpy(def->name, "Phase 11 LOS Target");
    def->size = 1;
    def->hitpoints = 100;
    def->stats[1] = 1;
    def->max_hit = 1;
    def->attack_speed = 4;
    def->attack_types = 0x04;
    strcpy(def->options[1], "Attack");
    return idx;
}

static RcWorld *make_world(void) {
    RcWorldConfig cfg = rc_preset_base_only();
    cfg.subsystems = RC_SUB_COMBAT;
    cfg.seed = 911;
    RcWorld *world = rc_world_create_config(&cfg);
    assert(world != NULL);
    world->player.x = 3200;
    world->player.y = 3200;
    world->player.plane = 0;
    for (int i = 0; i < SKILL_COUNT; i++) {
        world->player.skills.base_level[i] = 99;
        world->player.skills.boosted_level[i] = 99;
    }
    world->map.region_count = 1;
    world->map.regions[0].region_x = 50;
    world->map.regions[0].region_y = 50;
    world->map.regions[0].loaded = 1;
    return world;
}

static int spawn_target(RcWorld *world) {
    int def_idx = add_target_def();
    int idx = rc_npc_spawn(world, def_idx, 3204, 3200, 0);
    assert(idx >= 0);
    return idx;
}

static void block_projectile_line(RcWorld *world) {
    world->map.regions[0].tiles[0][3202 % RC_REGION_SIZE]
                             [3200 % RC_REGION_SIZE].collision_flags =
        COL_PROJ_BLOCK_FULL;
}

static void test_los_block_prevents_ranged_swing_without_consuming_ammo(void) {
    reset_defs();
    add_resource_defs();
    RcWorld *world = make_world();
    int npc_idx = spawn_target(world);
    block_projectile_line(world);

    world->player.equipment[EQUIP_WEAPON] = (RcInvSlot){TEST_BOW, 1};
    world->player.equipment[EQUIP_AMMO] = (RcInvSlot){TEST_ARROW, 5};
    rc_recalc_bonuses(&world->player);
    rc_refresh_player_combat_style(&world->player);
    assert(world->player.combat_style == COMBAT_RANGED);

    assert(rc_combat_start_player_vs_npc(world, 0, world->npcs[npc_idx].uid));
    rc_combat_tick_player(world);

    assert(world->npcs[npc_idx].num_pending_hits == 0);
    assert(world->player.attack_timer == 0);
    assert(world->player.equipment[EQUIP_AMMO].quantity == 5);
    assert(world->player.combat.line_of_sight == 0);
    assert(!(world->player.combat.flags & RC_COMBAT_STATE_IN_RANGE));
    assert(world->player.route_len > 0);
    rc_world_destroy(world);
}

static void test_los_block_prevents_magic_swing_without_consuming_runes(void) {
    reset_defs();
    add_resource_defs();
    RcWorld *world = make_world();
    int npc_idx = spawn_target(world);
    block_projectile_line(world);

    world->player.selected_spell = 0;
    world->player.inventory[0] = (RcInvSlot){TEST_FIRE_RUNE, 4};
    world->player.inventory[1] = (RcInvSlot){TEST_AIR_RUNE, 3};
    rc_refresh_player_combat_style(&world->player);
    assert(world->player.combat_style == COMBAT_MAGIC);

    assert(rc_combat_start_player_vs_npc(world, 0, world->npcs[npc_idx].uid));
    rc_combat_tick_player(world);

    assert(world->npcs[npc_idx].num_pending_hits == 0);
    assert(world->player.attack_timer == 0);
    assert(world->player.inventory[0].quantity == 4);
    assert(world->player.inventory[1].quantity == 3);
    assert(world->player.combat.line_of_sight == 0);
    assert(!(world->player.combat.flags & RC_COMBAT_STATE_IN_RANGE));
    assert(world->player.route_len > 0);
    rc_world_destroy(world);
}

int main(void) {
    test_los_block_prevents_ranged_swing_without_consuming_ammo();
    test_los_block_prevents_magic_swing_without_consuming_runes();
    return 0;
}
