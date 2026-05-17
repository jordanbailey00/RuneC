#include "../rc-core/api.h"
#include "../rc-core/combat.h"
#include "../rc-core/items.h"
#include "../rc-core/npc.h"
#include "../rc-core/spells.h"

#include <assert.h>
#include <string.h>

enum {
    TEST_SLASH_SWORD = 1001,
    TEST_BOW = 1002,
    TEST_POWERED_STAFF = 1003,
    TEST_BONUS_TRAP_BOW = 1004,
    TEST_ARROW = 1005,
    TEST_STAFF = 1006,
};

static RcWorld *phase4_world(void) {
    RcWorldConfig cfg = rc_preset_base_only();
    cfg.subsystems = RC_SUB_COMBAT;
    cfg.seed = 12345;
    RcWorld *world = rc_world_create_config(&cfg);
    assert(world);
    for (int i = 0; i < SKILL_COUNT; i++) {
        world->player.skills.base_level[i] = 99;
        world->player.skills.boosted_level[i] = 99;
    }
    return world;
}

static void fake_weapon(int id, const char *name, int weapon_type,
                        int speed, int range, int slash, int ranged,
                        int magic) {
    RcItemDef *def = &g_item_defs[id];
    memset(def, 0, sizeof(*def));
    def->id = id;
    def->loaded = true;
    strncpy(def->name, name, sizeof(def->name) - 1);
    def->equippable = true;
    def->equipable_weapon = true;
    def->equip_slot = EQUIP_WEAPON;
    def->attack_speed = speed;
    def->attack_range = range;
    def->weapon_type = weapon_type;
    def->attack_slash = slash;
    def->attack_ranged = ranged;
    def->attack_magic = magic;
    def->strength_bonus = slash;
    def->ranged_strength = ranged;
    def->magic_damage = magic > 0 ? 10 : 0;
}

static void equip_weapon(RcWorld *world, int item_id) {
    const RcItemDef *def = rc_item_def_get(item_id);
    assert(def);
    assert(def->loaded);
    assert(def->equipable_weapon);
    world->player.equipment[EQUIP_WEAPON].item_id = item_id;
    world->player.equipment[EQUIP_WEAPON].quantity = 1;
    rc_recalc_bonuses(&world->player);
}

static void ensure_fake_weapons(void) {
    fake_weapon(TEST_SLASH_SWORD, "Phase 4 slash sword", 18, 4, 1,
                80, 0, 0);
    fake_weapon(TEST_BOW, "Phase 4 bow", 25, 5, 1, 0, 80, 0);
    fake_weapon(TEST_POWERED_STAFF, "Phase 4 powered staff", 16, 4, 1,
                0, 0, 80);
    fake_weapon(TEST_STAFF, "Phase 4 staff", 22, 5, 1, 0, 0, 80);
    fake_weapon(TEST_BONUS_TRAP_BOW, "Phase 4 weird bow", 25, 5, 1,
                10000, 1, 0);
    RcItemDef *arrow = &g_item_defs[TEST_ARROW];
    memset(arrow, 0, sizeof(*arrow));
    arrow->id = TEST_ARROW;
    arrow->loaded = true;
    strcpy(arrow->name, "Phase 4 arrow");
    arrow->stackable = true;
    arrow->equippable = true;
    arrow->equipable_by_player = true;
    arrow->equip_slot = EQUIP_AMMO;
    arrow->ranged_strength = 80;
}

static void add_phase4_spell(void) {
    memset(g_rc_spell_defs, 0, sizeof(g_rc_spell_defs));
    g_rc_spell_count = 1;
    RcSpellDef *spell = &g_rc_spell_defs[0];
    strcpy(spell->name, "Phase 4 Fire Spell");
    spell->book = RC_SPELL_BOOK_STANDARD;
    spell->type = RC_SPELL_TYPE_COMBAT;
    spell->max_hit = 16;
    spell->loaded = 1;
}

static void test_slash_sword_table_sets_style_stance_xp_and_metadata(void) {
    RcWorld *world = phase4_world();
    ensure_fake_weapons();
    equip_weapon(world, TEST_SLASH_SWORD);

    rc_player_set_attack_style(world, 0);
    assert(world->player.combat_style == COMBAT_MELEE_SLASH);
    assert(world->player.attack_stance == RC_ATTACK_STANCE_ACCURATE);
    assert(world->player.combat_xp_mask == RC_COMBAT_XP_ATTACK);
    assert(world->player.combat.weapon_category == 18);
    assert(world->player.combat.combat_class == RC_COMBAT_CLASS_MELEE);
    assert(world->player.combat.attack_type == RC_ATTACK_TYPE_SLASH);
    assert(rc_player_attack_speed(&world->player) == 4);
    assert(rc_player_attack_range(&world->player) == 1);

    rc_player_set_attack_style(world, 2);
    assert(world->player.combat_style == COMBAT_MELEE_STAB);
    assert(world->player.attack_stance == RC_ATTACK_STANCE_CONTROLLED);
    assert(world->player.combat_xp_mask == (RC_COMBAT_XP_ATTACK |
                                            RC_COMBAT_XP_STRENGTH |
                                            RC_COMBAT_XP_DEFENCE));
    assert(world->player.combat.attack_type == RC_ATTACK_TYPE_STAB);

    rc_world_destroy(world);
}

static void test_ranged_table_applies_rapid_and_longrange_modifiers(void) {
    RcWorld *world = phase4_world();
    ensure_fake_weapons();
    equip_weapon(world, TEST_BOW);

    rc_player_set_attack_style(world, 0);
    assert(world->player.combat_style == COMBAT_RANGED);
    assert(world->player.attack_stance == RC_ATTACK_STANCE_ACCURATE);
    assert(world->player.combat.combat_class == RC_COMBAT_CLASS_RANGED);
    assert(world->player.combat.attack_type == RC_ATTACK_TYPE_RANGED);
    assert(rc_player_attack_speed(&world->player) == 5);
    assert(rc_player_attack_range(&world->player) == 7);

    rc_player_set_attack_style(world, 1);
    assert(world->player.attack_stance == RC_ATTACK_STANCE_RAPID);
    assert(rc_player_attack_speed(&world->player) == 4);
    assert(rc_player_attack_range(&world->player) == 7);

    rc_player_set_attack_style(world, 2);
    assert(world->player.attack_stance == RC_ATTACK_STANCE_LONGRANGE);
    assert(world->player.combat_xp_mask == (RC_COMBAT_XP_RANGED |
                                            RC_COMBAT_XP_DEFENCE));
    assert(rc_player_attack_speed(&world->player) == 5);
    assert(rc_player_attack_range(&world->player) == 9);

    rc_world_destroy(world);
}

static void test_magic_table_uses_powered_staff_without_bonus_guessing(void) {
    RcWorld *world = phase4_world();
    ensure_fake_weapons();
    equip_weapon(world, TEST_POWERED_STAFF);

    rc_player_set_attack_style(world, 0);
    assert(world->player.combat_style == COMBAT_MAGIC);
    assert(world->player.attack_stance == RC_ATTACK_STANCE_CAST);
    assert(world->player.combat.combat_class == RC_COMBAT_CLASS_MAGIC);
    assert(world->player.combat.attack_type == RC_ATTACK_TYPE_MAGIC);
    assert(rc_player_attack_speed(&world->player) == 4);
    assert(rc_player_attack_range(&world->player) == 10);

    rc_player_set_attack_style(world, 3);
    assert(world->player.attack_stance == RC_ATTACK_STANCE_DEFENSIVE_CAST);
    assert(world->player.combat_xp_mask == (RC_COMBAT_XP_MAGIC |
                                            RC_COMBAT_XP_DEFENCE));

    rc_world_destroy(world);
}

static void test_staff_selected_spell_does_not_override_default_attack(void) {
    RcWorld *world = phase4_world();
    ensure_fake_weapons();
    add_phase4_spell();

    rc_player_set_autocast_spell(world, 0, 0);
    assert(world->player.autocast_spell == -1);

    equip_weapon(world, TEST_STAFF);

    rc_player_select_spell(world, 0);
    assert(world->player.selected_spell == 0);
    rc_player_set_attack_style(world, 0);
    assert(world->player.combat_style == COMBAT_MELEE_CRUSH);
    assert(world->player.attack_stance == RC_ATTACK_STANCE_ACCURATE);
    assert(rc_player_attack_range(&world->player) == 1);

    rc_player_set_autocast_spell(world, 0, 0);
    assert(world->player.autocast_spell == 0);
    assert(!world->player.defensive_autocast);
    assert(world->player.combat_style == COMBAT_MAGIC);
    assert(world->player.attack_stance == RC_ATTACK_STANCE_CAST);
    assert(rc_player_attack_range(&world->player) == 10);

    rc_player_set_autocast_spell(world, 0, 1);
    assert(world->player.autocast_spell == 0);
    assert(world->player.defensive_autocast);
    assert(world->player.combat_style == COMBAT_MAGIC);
    assert(world->player.attack_stance == RC_ATTACK_STANCE_DEFENSIVE_CAST);

    rc_player_set_autocast_spell(world, -1, 0);
    world->player.manual_spell_cast = 0;
    rc_refresh_player_combat_style(&world->player);
    assert(world->player.combat_style == COMBAT_MAGIC);
    assert(world->player.attack_stance == RC_ATTACK_STANCE_CAST);

    rc_world_destroy(world);
}

static void test_weapon_type_beats_bonus_guessing_for_loaded_weapons(void) {
    RcWorld *world = phase4_world();
    ensure_fake_weapons();
    equip_weapon(world, TEST_BONUS_TRAP_BOW);

    rc_player_set_attack_style(world, 0);
    assert(world->player.combat_style == COMBAT_RANGED);
    assert(world->player.combat.weapon_category == 25);
    assert(world->player.combat.attack_type == RC_ATTACK_TYPE_RANGED);

    rc_world_destroy(world);
}

static int spawn_phase4_npc(RcWorld *world, int dx, int dy) {
    int def_idx = g_npc_def_count++;
    assert(def_idx < RC_MAX_NPC_DEFS);
    memset(&g_npc_defs[def_idx], 0, sizeof(g_npc_defs[def_idx]));
    g_npc_defs[def_idx].id = 940400;
    strcpy(g_npc_defs[def_idx].name, "Phase 4 Cooldown Guard");
    g_npc_defs[def_idx].size = 1;
    g_npc_defs[def_idx].combat_level = 2;
    g_npc_defs[def_idx].hitpoints = 200;
    g_npc_defs[def_idx].stats[1] = 1;
    g_npc_defs[def_idx].max_hit = 1;
    g_npc_defs[def_idx].attack_speed = 4;
    g_npc_defs[def_idx].attack_types = 0x04;
    strcpy(g_npc_defs[def_idx].options[1], "Attack");
    int idx = rc_npc_spawn(world, def_idx, world->player.x + dx,
                           world->player.y + dy, world->player.plane);
    assert(idx >= 0);
    world->npcs[idx].wander_timer = 999999;
    return idx;
}

static void test_attack_cycle_uses_selected_style_speed_for_cooldown(void) {
    RcWorld *world = phase4_world();
    ensure_fake_weapons();
    equip_weapon(world, TEST_BOW);
    world->player.equipment[EQUIP_AMMO] = (RcInvSlot){TEST_ARROW, 10};
    rc_recalc_bonuses(&world->player);
    rc_player_set_attack_style(world, 1);
    int expected_speed = rc_player_attack_speed(&world->player);
    int npc_idx = spawn_phase4_npc(world, 3, 0);
    RcNpc *npc = &world->npcs[npc_idx];

    assert(rc_combat_start_player_vs_npc(world, 0, npc->uid));
    rc_combat_tick_player(world);

    assert(world->player.attack_timer == expected_speed);
    assert(world->player.combat.attack_range == 7);
    assert(world->player.combat.flags & RC_COMBAT_STATE_IN_RANGE);

    rc_world_destroy(world);
}

int main(void) {
    test_slash_sword_table_sets_style_stance_xp_and_metadata();
    test_ranged_table_applies_rapid_and_longrange_modifiers();
    test_magic_table_uses_powered_staff_without_bonus_guessing();
    test_staff_selected_spell_does_not_override_default_attack();
    test_weapon_type_beats_bonus_guessing_for_loaded_weapons();
    test_attack_cycle_uses_selected_style_speed_for_cooldown();
    return 0;
}
