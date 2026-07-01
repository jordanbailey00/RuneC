#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "api.h"
#include "combat.h"
#include "config.h"
#include "npc.h"
#include "player_actions.h"
#include "prayer.h"
#include "spells.h"

#define PRAY_PATH RC_TEST_SOURCE_DIR "/data/defs/prayers.bin"
#define SPEL_PATH RC_TEST_SOURCE_DIR "/data/defs/spells.bin"
#define PACT_PATH RC_TEST_SOURCE_DIR "/data/defs/player_actions.bin"
#define NDEF_PATH RC_TEST_SOURCE_DIR "/data/defs/npc_defs.bin"
#define BAD_PATH "/tmp/runec_bad_pray_spell_action.bin"

static void write_bad_header(void) {
    uint32_t bad[3] = {0, 1, 0};
    FILE *f = fopen(BAD_PATH, "wb");
    assert(f != NULL);
    assert(fwrite(bad, sizeof(bad), 1, f) == 1);
    fclose(f);
}

int main(void) {
    write_bad_header();
    assert(rc_load_prayers(NULL) == -1);
    assert(rc_load_spells(NULL) == -1);
    assert(rc_load_player_actions(NULL) == -1);
    assert(rc_load_prayers(BAD_PATH) == -1);
    assert(rc_load_spells(BAD_PATH) == -1);
    assert(rc_load_player_actions(BAD_PATH) == -1);

    assert(rc_load_prayers(PRAY_PATH) == 31);
    const RcPrayerDef *augury = rc_prayer_def_get(RC_PRAYER_AUGURY);
    const RcPrayerDef *deadeye = rc_prayer_def_get(RC_PRAYER_DEADEYE);
    const RcPrayerDef *thick_skin = rc_prayer_def_get(RC_PRAYER_THICK_SKIN);
    assert(augury && augury->level == 77 && augury->magic_damage == 4);
    assert(deadeye && deadeye->defence == 5 && deadeye->ranged_strength == 18);
    assert(thick_skin && thick_skin->drain == 3);
    assert(rc_prayer_attack_bonus(PRAYER_PIETY) == 20);
    assert(rc_prayer_strength_bonus(PRAYER_PIETY) == 23);
    assert(rc_prayer_ranged_attack_bonus(PRAYER_RIGOUR) == 20);
    assert(rc_prayer_ranged_strength_bonus(PRAYER_RIGOUR) == 23);
    assert(rc_prayer_magic_attack_bonus(PRAYER_AUGURY) == 25);
    assert(rc_prayer_magic_damage_bonus(PRAYER_AUGURY) == 4);

    assert(rc_load_spells(SPEL_PATH) == 201);
    int fire_blast = rc_spell_find("Fire Blast");
    int lumbridge = rc_spell_find("Lumbridge Home Teleport");
    assert(fire_blast >= 0);
    assert(rc_spell_find("Definitely Missing Spell") == -1);
    assert(rc_spell_def_get(fire_blast)->max_hit == 0);
    assert(lumbridge >= 0);
    assert(rc_spell_def_get(lumbridge)->effect_flags & RC_SPELL_EFFECT_TELEPORT);

    assert(rc_load_player_actions(PACT_PATH) == 13);
    assert(!rc_player_action_allowed(0, RC_PLAYER_ACTION_SET_PRAYER));
    assert(rc_player_action_allowed(RC_SUB_PRAYER, RC_PLAYER_ACTION_SET_PRAYER));
    assert(rc_player_action_allowed(RC_SUB_COMBAT, RC_PLAYER_ACTION_SELECT_SPELL));

    RcWorldConfig denied_cfg = rc_preset_base_only();
    denied_cfg.player_actions_path = PACT_PATH;
    RcWorld *denied = rc_world_create_config(&denied_cfg);
    assert(denied != NULL);
    rc_player_select_spell(denied, fire_blast);
    assert(denied->player.selected_spell == -1);
    rc_world_destroy(denied);

    RcWorldConfig cfg = rc_preset_base_only();
    cfg.subsystems = RC_SUB_COMBAT | RC_SUB_PRAYER;
    cfg.npc_defs_path = NDEF_PATH;
    cfg.prayers_path = PRAY_PATH;
    cfg.spells_path = SPEL_PATH;
    cfg.player_actions_path = PACT_PATH;
    RcWorld *world = rc_world_create_config(&cfg);
    assert(world != NULL);

    rc_player_set_prayer(world, RC_PRAYER_RIGOUR);
    assert(world->player.active_prayers == 0);

    world->player.current_prayer_points = 1000;
    rc_player_set_prayer(world, RC_PRAYER_RIGOUR);
    assert(world->player.active_prayers == 0);

    world->player.skills.base_level[SKILL_PRAYER] = 99;
    rc_player_set_prayer(world, RC_PRAYER_THICK_SKIN);
    rc_player_set_prayer(world, RC_PRAYER_DEADEYE);
    assert(world->player.active_prayers & PRAYER_THICK_SKIN);
    assert(world->player.active_prayers & PRAYER_DEADEYE);
    assert(rc_prayer_defence_bonus(world->player.active_prayers) == 10);

    rc_player_set_prayer(world, RC_PRAYER_RIGOUR);
    assert(world->player.active_prayers == PRAYER_RIGOUR);
    for (int i = 0; i < 3; i++) rc_prayer_drain_tick(&world->player);
    assert(world->player.current_prayer_points == 990);
    rc_player_set_prayer(world, RC_PRAYER_RIGOUR);
    assert(world->player.active_prayers == 0);

    rc_player_select_spell(world, fire_blast);
    assert(world->player.selected_spell == fire_blast);
    rc_refresh_player_combat_style(&world->player);
    assert(world->player.combat_style != COMBAT_MAGIC);
    const RcSpellDef *fire_blast_def = rc_spell_def_get(fire_blast);
    assert(fire_blast_def != NULL);
    for (int i = 0; i < fire_blast_def->rune_count; i++) {
        world->player.inventory[i].item_id =
            (int)fire_blast_def->runes[i].item_id;
        world->player.inventory[i].quantity =
            fire_blast_def->runes[i].qty + 1;
    }

    int jad_def = rc_npc_def_find(3127);
    assert(jad_def >= 0);
    world->player.skills.boosted_level[SKILL_RANGED] = 99;
    world->player.equipment_bonuses[EQ_RANGED_STR] = 64;
    world->player.active_prayers = PRAYER_RIGOUR;
    RcCombatCalc ranged = rc_calc_ranged(&world->player, jad_def);
    assert(ranged.attack_roll > 0 && ranged.max_hit > 0);

    int npc_idx = rc_npc_spawn(world, jad_def, world->player.x, world->player.y, 0);
    assert(npc_idx >= 0);
    world->player.attack_target = world->npcs[npc_idx].uid;
    world->player.manual_spell_cast = fire_blast;
    world->player.skills.boosted_level[SKILL_MAGIC] = 99;
    world->player.active_prayers = PRAYER_AUGURY;
    rc_combat_tick_player(world);
    assert(world->npcs[npc_idx].num_pending_hits == 0);

    rc_world_destroy(world);
    return 0;
}
