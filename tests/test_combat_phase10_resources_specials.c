#include "api.h"
#include "combat.h"
#include "content.h"
#include "items.h"
#include "npc.h"
#include "spells.h"

#include <assert.h>
#include <string.h>

enum {
    TEST_BOW = 100,
    TEST_ARROW = 101,
    TEST_SPECIAL_WEAPON = 102,
    TEST_FIRE_RUNE = 103,
    TEST_AIR_RUNE = 104,
    OSRS_FIRE_RUNE = 554,
    OSRS_AIR_RUNE = 556,
    OSRS_SMOKE_RUNE = 4697,
    OSRS_STAFF_OF_FIRE = 1387,
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
    assert(id >= 0 && id < RC_MAX_ITEM_DEFS);
    RcItemDef *def = &g_item_defs[id];
    memset(def, 0, sizeof(*def));
    def->id = id;
    def->equip_slot = -1;
    def->loaded = true;
    strncpy(def->name, name, sizeof(def->name) - 1);
    g_item_def_count++;
    return def;
}

static void add_ranged_defs(void) {
    RcItemDef *bow = add_item(TEST_BOW, "Phase 10 bow");
    bow->equippable = true;
    bow->equipable_by_player = true;
    bow->equipable_weapon = true;
    bow->equip_slot = EQUIP_WEAPON;
    bow->attack_ranged = 1000;
    bow->attack_speed = 4;
    bow->attack_range = 7;
    bow->weapon_type = 25;

    RcItemDef *arrow = add_item(TEST_ARROW, "Phase 10 arrow");
    arrow->stackable = true;
    arrow->equippable = true;
    arrow->equipable_by_player = true;
    arrow->equip_slot = EQUIP_AMMO;
    arrow->ranged_strength = 100;
}

static void add_magic_defs(void) {
    RcItemDef *fire = add_item(TEST_FIRE_RUNE, "Fire rune");
    fire->stackable = true;
    RcItemDef *air = add_item(TEST_AIR_RUNE, "Air rune");
    air->stackable = true;

    g_rc_spell_count = 1;
    RcSpellDef *spell = &g_rc_spell_defs[0];
    memset(spell, 0, sizeof(*spell));
    strcpy(spell->name, "Phase 10 Fire Spell");
    spell->type = RC_SPELL_TYPE_COMBAT;
    spell->max_hit = 20;
    spell->rune_count = 2;
    spell->runes[0] = (RcSpellRune){TEST_FIRE_RUNE, 2};
    spell->runes[1] = (RcSpellRune){TEST_AIR_RUNE, 1};
    spell->loaded = 1;
}

static void add_osrs_rune_source_defs(void) {
    RcItemDef *fire = add_item(OSRS_FIRE_RUNE, "Fire rune");
    fire->stackable = true;
    RcItemDef *air = add_item(OSRS_AIR_RUNE, "Air rune");
    air->stackable = true;
    RcItemDef *smoke = add_item(OSRS_SMOKE_RUNE, "Smoke rune");
    smoke->stackable = true;

    RcItemDef *staff = add_item(OSRS_STAFF_OF_FIRE, "Staff of fire");
    staff->equippable = true;
    staff->equipable_by_player = true;
    staff->equipable_weapon = true;
    staff->equip_slot = EQUIP_WEAPON;
    staff->attack_speed = 5;
    staff->attack_range = 1;
    staff->weapon_type = 22;

    g_rc_spell_count = 1;
    RcSpellDef *spell = &g_rc_spell_defs[0];
    memset(spell, 0, sizeof(*spell));
    strcpy(spell->name, "Phase 10 OSRS Fire Spell");
    spell->type = RC_SPELL_TYPE_COMBAT;
    spell->book = RC_SPELL_BOOK_STANDARD;
    spell->max_hit = 20;
    spell->rune_count = 2;
    spell->runes[0] = (RcSpellRune){OSRS_FIRE_RUNE, 2};
    spell->runes[1] = (RcSpellRune){OSRS_AIR_RUNE, 2};
    spell->loaded = 1;
}

static void add_special_weapon_def(void) {
    RcItemDef *weapon = add_item(TEST_SPECIAL_WEAPON, "Phase 10 spec mace");
    weapon->equippable = true;
    weapon->equipable_by_player = true;
    weapon->equipable_weapon = true;
    weapon->equip_slot = EQUIP_WEAPON;
    weapon->attack_crush = 1000;
    weapon->strength_bonus = 100;
    weapon->attack_speed = 4;
    weapon->attack_range = 1;
    weapon->weapon_type = 4;
}

static int add_npc_def(void) {
    int idx = g_npc_def_count++;
    assert(idx < RC_MAX_NPC_DEFS);
    RcNpcDef *def = &g_npc_defs[idx];
    memset(def, 0, sizeof(*def));
    def->id = 910010;
    strcpy(def->name, "Phase 10 Target");
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
    cfg.seed = 123;
    RcWorld *world = rc_world_create_config(&cfg);
    assert(world != NULL);
    for (int i = 0; i < SKILL_COUNT; i++) {
        world->player.skills.base_level[i] = 99;
        world->player.skills.boosted_level[i] = 99;
    }
    return world;
}

static int spawn_target(RcWorld *world, int dx) {
    int def_idx = add_npc_def();
    int idx = rc_npc_spawn(world, def_idx, world->player.x + dx,
                           world->player.y, world->player.plane);
    assert(idx >= 0);
    return idx;
}

static void test_ranged_requires_and_consumes_ammo(void) {
    reset_defs();
    add_ranged_defs();
    RcWorld *world = make_world();
    world->player.equipment[EQUIP_WEAPON] = (RcInvSlot){TEST_BOW, 1};
    rc_recalc_bonuses(&world->player);
    rc_refresh_player_combat_style(&world->player);
    assert(world->player.combat_style == COMBAT_RANGED);
    int npc_idx = spawn_target(world, 4);
    assert(rc_combat_start_player_vs_npc(world, 0, world->npcs[npc_idx].uid));
    rc_combat_tick_player(world);
    assert(world->npcs[npc_idx].num_pending_hits == 0);
    assert(world->player.attack_timer == 0);

    world->player.equipment[EQUIP_AMMO] = (RcInvSlot){TEST_ARROW, 3};
    rc_recalc_bonuses(&world->player);
    rc_combat_tick_player(world);
    assert(world->npcs[npc_idx].num_pending_hits == 1);
    assert(world->npcs[npc_idx].pending_hits[0].attack_style == COMBAT_RANGED);
    assert(world->player.equipment[EQUIP_AMMO].quantity == 2);
    assert(world->player.attack_timer > 0);
    rc_world_destroy(world);
}

static void test_magic_requires_spell_and_consumes_runes(void) {
    reset_defs();
    add_magic_defs();
    RcWorld *world = make_world();
    int npc_idx = spawn_target(world, 4);
    world->player.manual_spell_cast = 0;
    rc_refresh_player_combat_style(&world->player);
    assert(world->player.combat_style == COMBAT_MAGIC);
    assert(rc_combat_start_player_vs_npc(world, 0, world->npcs[npc_idx].uid));
    rc_combat_tick_player(world);
    assert(world->npcs[npc_idx].num_pending_hits == 0);

    world->player.inventory[0] = (RcInvSlot){TEST_FIRE_RUNE, 3};
    world->player.inventory[1] = (RcInvSlot){TEST_AIR_RUNE, 2};
    world->player.manual_spell_cast = 0;
    rc_refresh_player_combat_style(&world->player);
    rc_combat_tick_player(world);
    assert(world->npcs[npc_idx].num_pending_hits == 1);
    assert(world->npcs[npc_idx].pending_hits[0].attack_style == COMBAT_MAGIC);
    assert(world->player.inventory[0].quantity == 1);
    assert(world->player.inventory[1].quantity == 1);
    rc_world_destroy(world);
}

static RcWorld *make_osrs_rune_world(int *npc_idx) {
    reset_defs();
    add_osrs_rune_source_defs();
    RcWorld *world = make_world();
    rc_content_combat_register(world);
    *npc_idx = spawn_target(world, 4);
    return world;
}

static void tick_manual_spell(RcWorld *world, int npc_idx) {
    world->player.manual_spell_cast = 0;
    rc_refresh_player_combat_style(&world->player);
    assert(rc_combat_start_player_vs_npc(world, 0,
                                         world->npcs[npc_idx].uid));
    rc_combat_tick_player(world);
}

static void test_osrs_rune_sources_cover_staff_pouch_and_combos(void) {
    int npc_idx = -1;
    RcWorld *world = make_osrs_rune_world(&npc_idx);
    world->player.equipment[EQUIP_WEAPON] =
        (RcInvSlot){OSRS_STAFF_OF_FIRE, 1};
    world->player.inventory[0] = (RcInvSlot){OSRS_AIR_RUNE, 2};
    world->player.inventory[1] = (RcInvSlot){OSRS_SMOKE_RUNE, 2};
    rc_recalc_bonuses(&world->player);
    tick_manual_spell(world, npc_idx);
    assert(world->npcs[npc_idx].num_pending_hits == 1);
    assert(world->player.inventory[0].item_id == -1);
    assert(world->player.inventory[1].item_id == OSRS_SMOKE_RUNE);
    assert(world->player.inventory[1].quantity == 2);
    rc_world_destroy(world);

    world = make_osrs_rune_world(&npc_idx);
    world->player.inventory[0] = (RcInvSlot){OSRS_SMOKE_RUNE, 2};
    tick_manual_spell(world, npc_idx);
    assert(world->npcs[npc_idx].num_pending_hits == 1);
    assert(world->player.inventory[0].item_id == -1);
    rc_world_destroy(world);

    world = make_osrs_rune_world(&npc_idx);
    world->player.inventory[0] = (RcInvSlot){OSRS_AIR_RUNE, 2};
    world->player.rune_pouch[0] = (RcInvSlot){OSRS_FIRE_RUNE, 2};
    tick_manual_spell(world, npc_idx);
    assert(world->npcs[npc_idx].num_pending_hits == 1);
    assert(world->player.inventory[0].item_id == -1);
    assert(world->player.rune_pouch[0].item_id == -1);
    rc_world_destroy(world);
}

static int test_special_cost(const RcWorld *world, const RcPlayer *player,
                             const RcNpc *target, int weapon_id) {
    (void)world;
    (void)player;
    (void)target;
    return weapon_id == TEST_SPECIAL_WEAPON ? 5000 : 0;
}

static int test_special_damage(RcWorld *world, const RcPlayer *player,
                               const RcNpc *target, int weapon_id,
                               RcCombatStyle style, int damage,
                               int max_hit) {
    (void)world;
    (void)player;
    (void)target;
    (void)style;
    (void)damage;
    return weapon_id == TEST_SPECIAL_WEAPON ? max_hit : damage;
}

static void test_special_spends_energy_and_recovers(void) {
    reset_defs();
    add_special_weapon_def();
    RcWorld *world = make_world();
    RcCombatContentHooks hooks = {
        .player_special_energy_cost = test_special_cost,
        .modify_player_special_damage = test_special_damage,
    };
    rc_combat_register_content_hooks(world, &hooks);
    world->player.equipment[EQUIP_WEAPON] =
        (RcInvSlot){TEST_SPECIAL_WEAPON, 1};
    rc_recalc_bonuses(&world->player);
    int npc_idx = spawn_target(world, 1);
    assert(rc_combat_start_player_vs_npc(world, 0, world->npcs[npc_idx].uid));
    rc_combat_toggle_special(world);
    assert(world->player.combat.special_pending);
    rc_combat_tick_player(world);
    assert(world->player.special_energy == 5000);
    assert(!world->player.combat.special_pending);
    assert(world->npcs[npc_idx].num_pending_hits == 1);
    assert(world->npcs[npc_idx].pending_hits[0].damage ==
           world->npcs[npc_idx].pending_hits[0].max_hit);

    for (int i = 0; i < RC_SPECIAL_RECOVER_TICKS - 1; i++) {
        rc_combat_tick_player_status(world);
    }
    assert(world->player.special_energy == 5000);
    rc_combat_tick_player_status(world);
    assert(world->player.special_energy == 6000);
    rc_world_destroy(world);
}

int main(void) {
    test_ranged_requires_and_consumes_ammo();
    test_magic_requires_spell_and_consumes_runes();
    test_osrs_rune_sources_cover_staff_pouch_and_combos();
    test_special_spends_energy_and_recovers();
    return 0;
}
