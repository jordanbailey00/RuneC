#include "../rc-core/api.h"
#include "../rc-core/combat.h"
#include "../rc-core/items.h"
#include "../rc-core/npc.h"
#include "../rc-core/spells.h"
#include "runtime_test_fixture.h"

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

static RcWorld *phase4_world(int include_spells) {
    memset(g_npc_defs, 0, sizeof(g_npc_defs));
    g_npc_def_count = 1;
    g_npc_defs[0].id = 940400;
    strcpy(g_npc_defs[0].name, "Phase 4 Cooldown Guard");
    g_npc_defs[0].size = 1;
    g_npc_defs[0].combat_level = 2;
    g_npc_defs[0].hitpoints = 200;
    g_npc_defs[0].stats[1] = 1;
    g_npc_defs[0].max_hit = 1;
    g_npc_defs[0].attack_speed = 4;
    g_npc_defs[0].attack_types = 0x04;
    strcpy(g_npc_defs[0].options[1], "Attack");
    RcWorldConfig cfg = rc_preset_base_only();
    cfg.subsystems = RC_SUB_COMBAT;
    cfg.seed = 12345;
    RcWorld *world = rc_test_world_create_with_defs(
        &cfg, "phase4", include_spells);
    assert(world);
    rc_test_open_mapsquare(world, world->player.x, world->player.y,
                           world->player.plane);
    for (int i = 0; i < SKILL_COUNT; i++) {
        world->player.skills.base_level[i] = 99;
        world->player.skills.boosted_level[i] = 99;
    }
    return world;
}

static void apply_attack_style(RcWorld *world, int style) {
    rc_player_set_attack_style(world, style);
    rc_world_tick(world);
}

static void apply_selected_spell(RcWorld *world, int spell) {
    rc_player_select_spell(world, spell);
    rc_world_tick(world);
}

static void apply_autocast_spell(RcWorld *world, int spell, int defensive) {
    rc_player_set_autocast_spell(world, spell, defensive);
    rc_world_tick(world);
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
    def->magic_damage = magic > 0 ? 100 : 0;
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
    fake_weapon(TEST_BOW, "Phase 4 bow", 25, 5, 7, 0, 80, 0);
    fake_weapon(TEST_POWERED_STAFF, "Phase 4 powered staff", 16, 4, 10,
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
    spell->level = 1;
    spell->book = RC_SPELL_BOOK_STANDARD;
    spell->type = RC_SPELL_TYPE_COMBAT;
    spell->max_hit = 16;
    spell->loaded = 1;
}

static void test_slash_sword_table_sets_style_stance_xp_and_metadata(void) {
    RcWorld *world = phase4_world(0);
    ensure_fake_weapons();
    equip_weapon(world, TEST_SLASH_SWORD);

    apply_attack_style(world, 0);
    assert(world->player.combat_style == COMBAT_MELEE_SLASH);
    assert(world->player.attack_stance == RC_ATTACK_STANCE_ACCURATE);
    assert(world->player.combat_xp_mask == RC_COMBAT_XP_ATTACK);
    assert(world->player.combat.weapon_category == 18);
    assert(world->player.combat.combat_class == RC_COMBAT_CLASS_MELEE);
    assert(world->player.combat.attack_type == RC_ATTACK_TYPE_SLASH);
    assert(rc_player_attack_speed(&world->player) == 4);
    assert(rc_player_attack_range(&world->player) == 1);

    apply_attack_style(world, 2);
    assert(world->player.combat_style == COMBAT_MELEE_STAB);
    assert(world->player.attack_stance == RC_ATTACK_STANCE_CONTROLLED);
    assert(world->player.combat_xp_mask == (RC_COMBAT_XP_ATTACK |
                                            RC_COMBAT_XP_STRENGTH |
                                            RC_COMBAT_XP_DEFENCE));
    assert(world->player.combat.attack_type == RC_ATTACK_TYPE_STAB);

    rc_world_destroy(world);
}

static void test_ranged_table_applies_rapid_and_longrange_modifiers(void) {
    RcWorld *world = phase4_world(0);
    ensure_fake_weapons();
    equip_weapon(world, TEST_BOW);

    apply_attack_style(world, 0);
    assert(world->player.combat_style == COMBAT_RANGED);
    assert(world->player.attack_stance == RC_ATTACK_STANCE_ACCURATE);
    assert(world->player.combat.combat_class == RC_COMBAT_CLASS_RANGED);
    assert(world->player.combat.attack_type == RC_ATTACK_TYPE_RANGED);
    assert(rc_player_attack_speed(&world->player) == 5);
    assert(rc_player_attack_range(&world->player) == 7);

    apply_attack_style(world, 1);
    assert(world->player.attack_stance == RC_ATTACK_STANCE_RAPID);
    assert(rc_player_attack_speed(&world->player) == 4);
    assert(rc_player_attack_range(&world->player) == 7);

    apply_attack_style(world, 3);
    assert(world->player.attack_stance == RC_ATTACK_STANCE_LONGRANGE);
    assert(world->player.combat_xp_mask == (RC_COMBAT_XP_RANGED |
                                            RC_COMBAT_XP_DEFENCE));
    assert(rc_player_attack_speed(&world->player) == 5);
    assert(rc_player_attack_range(&world->player) == 9);

    rc_world_destroy(world);
}

static void test_magic_table_uses_powered_staff_without_bonus_guessing(void) {
    RcWorld *world = phase4_world(0);
    ensure_fake_weapons();
    equip_weapon(world, TEST_POWERED_STAFF);

    apply_attack_style(world, 0);
    assert(world->player.combat_style == COMBAT_MAGIC);
    assert(world->player.attack_stance == RC_ATTACK_STANCE_CAST);
    assert(world->player.combat.combat_class == RC_COMBAT_CLASS_MAGIC);
    assert(world->player.combat.attack_type == RC_ATTACK_TYPE_MAGIC);
    assert(rc_player_attack_speed(&world->player) == 4);
    assert(rc_player_attack_range(&world->player) == 10);

    apply_attack_style(world, 3);
    assert(world->player.attack_stance == RC_ATTACK_STANCE_DEFENSIVE_CAST);
    assert(world->player.combat_xp_mask == (RC_COMBAT_XP_MAGIC |
                                            RC_COMBAT_XP_DEFENCE));

    rc_world_destroy(world);
}

static void test_staff_selected_spell_does_not_override_default_attack(void) {
    add_phase4_spell();
    RcWorld *world = phase4_world(1);
    ensure_fake_weapons();

    apply_autocast_spell(world, 0, 0);
    assert(world->player.autocast_spell == -1);

    equip_weapon(world, TEST_STAFF);

    apply_selected_spell(world, 0);
    assert(world->player.selected_spell == 0);
    apply_attack_style(world, 0);
    assert(world->player.combat_style == COMBAT_MELEE_CRUSH);
    assert(world->player.attack_stance == RC_ATTACK_STANCE_ACCURATE);
    assert(rc_player_attack_range(&world->player) == 1);

    apply_autocast_spell(world, 0, 0);
    assert(world->player.autocast_spell == 0);
    assert(!world->player.defensive_autocast);
    assert(world->player.combat_style == COMBAT_MAGIC);
    assert(world->player.attack_stance == RC_ATTACK_STANCE_CAST);
    assert(rc_player_attack_range(&world->player) == 10);

    apply_autocast_spell(world, 0, 1);
    assert(world->player.autocast_spell == 0);
    assert(world->player.defensive_autocast);
    assert(world->player.combat_style == COMBAT_MAGIC);
    assert(world->player.attack_stance == RC_ATTACK_STANCE_DEFENSIVE_CAST);

    apply_autocast_spell(world, -1, 0);
    world->player.manual_spell_cast = 0;
    rc_refresh_player_combat_style(&world->player);
    assert(world->player.combat_style == COMBAT_MAGIC);
    assert(world->player.attack_stance == RC_ATTACK_STANCE_CAST);

    rc_world_destroy(world);
}

static void test_weapon_type_beats_bonus_guessing_for_loaded_weapons(void) {
    RcWorld *world = phase4_world(0);
    ensure_fake_weapons();
    equip_weapon(world, TEST_BONUS_TRAP_BOW);

    apply_attack_style(world, 0);
    assert(world->player.combat_style == COMBAT_RANGED);
    assert(world->player.combat.weapon_category == 25);
    assert(world->player.combat.attack_type == RC_ATTACK_TYPE_RANGED);

    rc_world_destroy(world);
}

static int spawn_phase4_npc(RcWorld *world, int dx, int dy) {
    int idx = rc_npc_spawn(world, 0, world->player.x + dx,
                           world->player.y + dy, world->player.plane);
    assert(idx >= 0);
    world->npcs[idx].wander_timer = 999999;
    return idx;
}

static void test_attack_cycle_uses_selected_style_speed_for_cooldown(void) {
    RcWorld *world = phase4_world(0);
    ensure_fake_weapons();
    equip_weapon(world, TEST_BOW);
    world->player.equipment[EQUIP_AMMO] = (RcInvSlot){TEST_ARROW, 10};
    rc_recalc_bonuses(&world->player);
    apply_attack_style(world, 1);
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

static void test_attack_cycle_does_not_queue_hits_during_cooldown(void) {
    RcWorld *world = phase4_world(0);
    ensure_fake_weapons();
    equip_weapon(world, TEST_SLASH_SWORD);
    apply_attack_style(world, 0);
    int expected_speed = rc_player_attack_speed(&world->player);
    int npc_idx = spawn_phase4_npc(world, 1, 0);
    RcNpc *npc = &world->npcs[npc_idx];
    npc->force_player_max_hit = true;
    int start_hp = npc->current_hp;

    assert(rc_combat_start_player_vs_npc(world, 0, npc->uid));
    rc_world_tick(world);
    int hp_after_first = npc->current_hp;
    assert(hp_after_first < start_hp);
    assert(world->player.attack_timer == expected_speed);

    for (int i = 1; i < expected_speed; i++) {
        rc_world_tick(world);
        assert(npc->current_hp == hp_after_first);
        assert(world->player.attack_timer == expected_speed - i);
    }

    rc_world_tick(world);
    assert(npc->current_hp < hp_after_first);

    rc_world_destroy(world);
}

static void test_extended_melee_checks_walk_blocking(void) {
    RcWorld *w = phase4_world(0);
    fake_weapon(TEST_SLASH_SWORD, "Reach test polearm", 14, 6, 2, 80, 0, 0);
    equip_weapon(w, TEST_SLASH_SWORD);
    int index = spawn_phase4_npc(w, 2, 0);
    RcNpc *n = &w->npcs[index];
    int x = (w->player.x + 1) % 64, y = w->player.y % 64;
    w->map.regions[0].tiles[0][x][y].collision_flags = COL_LOC;
    assert(rc_combat_start_player_vs_npc(w, 0, n->uid));
    assert(rc_player_attack_range(&w->player) == 2);
    rc_combat_tick_player(w);
    assert(n->num_pending_hits == 0 && w->player.route_len > 0);
    w->map.regions[0].tiles[0][x][y].collision_flags = 0;
    rc_combat_tick_player(w);
    assert(n->num_pending_hits == 1);
    rc_world_destroy(w);
}

static void test_category_specific_and_unavailable_slots(void) {
    const struct { int category, slot; RcCombatStyle style; RcAttackStance stance; } cases[] = {
        {0, 2, COMBAT_NONE, 0},
        {0, 3, COMBAT_MELEE_CRUSH, RC_ATTACK_STANCE_DEFENSIVE},
        {8, 2, COMBAT_MELEE_STAB, RC_ATTACK_STANCE_CONTROLLED},
        {13, 2, COMBAT_MELEE_CRUSH, RC_ATTACK_STANCE_AGGRESSIVE},
        {13, 0, COMBAT_MELEE_STAB, RC_ATTACK_STANCE_ACCURATE},
        {10, 1, COMBAT_MELEE_SLASH, RC_ATTACK_STANCE_CONTROLLED},
        {14, 0, COMBAT_MELEE_STAB, RC_ATTACK_STANCE_CONTROLLED},
        {14, 1, COMBAT_MELEE_SLASH, RC_ATTACK_STANCE_AGGRESSIVE},
        {14, 2, COMBAT_NONE, 0},
        {14, 3, COMBAT_MELEE_STAB, RC_ATTACK_STANCE_DEFENSIVE},
        {20, 2, COMBAT_MELEE_STAB, RC_ATTACK_STANCE_CONTROLLED},
        {5, 1, COMBAT_MELEE_CRUSH, RC_ATTACK_STANCE_AGGRESSIVE},
        {5, 2, COMBAT_NONE, 0},
        {6, 0, COMBAT_MELEE_CRUSH, RC_ATTACK_STANCE_ACCURATE},
        {6, 1, COMBAT_NONE, 0},
        {29, 0, COMBAT_MELEE_STAB, RC_ATTACK_STANCE_ACCURATE},
        {29, 1, COMBAT_MELEE_SLASH, RC_ATTACK_STANCE_AGGRESSIVE},
        {29, 2, COMBAT_NONE, 0},
        {29, 3, COMBAT_MELEE_CRUSH, RC_ATTACK_STANCE_DEFENSIVE},
        {25, 2, COMBAT_NONE, 0}, {16, 2, COMBAT_NONE, 0},
        {22, 2, COMBAT_NONE, 0}, {10, 2, COMBAT_NONE, 0},
        {99, 0, COMBAT_NONE, 0}, {18, -1, COMBAT_NONE, 0},
    };
    RcWorld *w = phase4_world(0);
    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        fake_weapon(TEST_SLASH_SWORD, "Category test", cases[i].category, 4, 1, 100, 100, 100);
        equip_weapon(w, TEST_SLASH_SWORD);
        w->player.attack_style_idx = cases[i].slot;
        rc_refresh_player_combat_style(&w->player);
        assert(w->player.combat_style == cases[i].style);
        if (cases[i].style != COMBAT_NONE)
            assert(w->player.attack_stance == cases[i].stance);
    }
    w->player.equipment[EQUIP_WEAPON].item_id = RC_MAX_ITEM_DEFS;
    w->player.attack_style_idx = 0;
    rc_refresh_player_combat_style(&w->player);
    assert(w->player.combat_style == COMBAT_NONE);
    rc_world_destroy(w);
}

int main(void) {
    test_slash_sword_table_sets_style_stance_xp_and_metadata();
    test_ranged_table_applies_rapid_and_longrange_modifiers();
    test_magic_table_uses_powered_staff_without_bonus_guessing();
    test_staff_selected_spell_does_not_override_default_attack();
    test_weapon_type_beats_bonus_guessing_for_loaded_weapons();
    test_attack_cycle_uses_selected_style_speed_for_cooldown();
    test_attack_cycle_does_not_queue_hits_during_cooldown();
    test_extended_melee_checks_walk_blocking();
    test_category_specific_and_unavailable_slots();
    return 0;
}
