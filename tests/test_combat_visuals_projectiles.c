#include "api.h"
#include "combat.h"
#include "combat_profiles.h"
#include "items.h"
#include "npc.h"
#include "spells.h"
#include "../rc-viewer/combat_visuals.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

enum {
    TEST_BOW = 861,
    TEST_ARROW = 892,
    TEST_FIRE_RUNE = 554,
    TEST_AIR_RUNE = 556,
    TEST_NPC_ID = 990001,
    TEST_JAD_ID = 3127,
};

static void reset_defs(void) {
    memset(g_item_defs, 0, sizeof(g_item_defs));
    g_item_def_count = 0;
    memset(g_rc_spell_defs, 0, sizeof(g_rc_spell_defs));
    g_rc_spell_count = 0;
    memset(g_npc_defs, 0, sizeof(g_npc_defs));
    g_npc_def_count = 0;
    memset(g_rc_combat_profile_defs, 0, sizeof(g_rc_combat_profile_defs));
    g_rc_combat_profile_count = 0;
    memset(g_rc_combat_visual_defs, 0, sizeof(g_rc_combat_visual_defs));
    g_rc_combat_visual_count = 0;
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

static void add_defs(void) {
    RcItemDef *bow = add_item(TEST_BOW, "Magic shortbow");
    bow->equippable = true;
    bow->equipable_by_player = true;
    bow->equipable_weapon = true;
    bow->equip_slot = EQUIP_WEAPON;
    bow->attack_ranged = 1000;
    bow->attack_speed = 4;
    bow->attack_range = 7;
    bow->weapon_type = 25;

    RcItemDef *arrow = add_item(TEST_ARROW, "Rune arrow");
    arrow->stackable = true;
    arrow->equippable = true;
    arrow->equipable_by_player = true;
    arrow->equip_slot = EQUIP_AMMO;
    arrow->ranged_strength = 100;

    RcItemDef *fire = add_item(TEST_FIRE_RUNE, "Fire rune");
    fire->stackable = true;
    RcItemDef *air = add_item(TEST_AIR_RUNE, "Air rune");
    air->stackable = true;

    g_rc_spell_count = 1;
    RcSpellDef *spell = &g_rc_spell_defs[0];
    memset(spell, 0, sizeof(*spell));
    strcpy(spell->name, "Fire Blast");
    spell->type = RC_SPELL_TYPE_COMBAT;
    spell->max_hit = 16;
    spell->rune_count = 2;
    spell->runes[0] = (RcSpellRune){TEST_FIRE_RUNE, 1};
    spell->runes[1] = (RcSpellRune){TEST_AIR_RUNE, 1};
    spell->loaded = 1;

    int idx = g_npc_def_count++;
    RcNpcDef *npc = &g_npc_defs[idx];
    memset(npc, 0, sizeof(*npc));
    npc->id = TEST_NPC_ID;
    strcpy(npc->name, "Projectile Target");
    npc->size = 1;
    npc->hitpoints = 100;
    npc->stats[1] = 1;
    npc->stats[5] = 99;
    npc->max_hit = 1;
    npc->attack_speed = 4;
    npc->attack_types = 0x08;
    strcpy(npc->options[1], "Attack");
}

static void write_visuals_file(const char *path) {
    FILE *f = fopen(path, "w");
    assert(f != NULL);
    fputs("kind|key|style|attack_anim|launch_spotanim|travel_spotanim|"
          "impact_spotanim|projectile_model|projectile_anim|hit_delay|"
          "client_delay|proj_start_height|proj_end_height|proj_delay|"
          "proj_angle|proj_length_adjustment|proj_progress|"
          "proj_step_multiplier|note|projectile_count|alt_proj_start_height|"
          "alt_proj_end_height|alt_proj_delay|alt_proj_angle|"
          "alt_proj_length_adjustment|alt_proj_progress|"
          "alt_proj_step_multiplier|aux_travel_spotanim|aux_impact_spotanim|"
          "aux_projectile_model|aux_projectile_anim|impact_on_last_only|"
          "double_launch_spotanim|stance_idx|attack_key|"
          "launch_gfx_height|impact_gfx_height|impact_gfx_delay|"
          "impact_gfx_rotation|primitive_type|source_attachment|"
          "target_attachment|launch_attachment|impact_attachment|"
          "authority\n", f);
    fputs("item|861|ranged|426|-|-|-|-|-|3|3|163|146|41|15|5|11|5|bow|-|-|-|-|-|-|-|-|-|-|-|-|-|-|-|Rapid|-|-|-|-|none|none|none|none|none|test\n", f);
    fputs("item|892|ranged|-|24|15|-|3136|-|2|2|-|-|-|-|-|-|-|arrow|-|-|-|-|-|-|-|-|-|-|-|-|-|1109|-|Rune arrows|-|146|-|-|travel_projectile|source_actor|target_actor|source_actor|target_actor|test\n", f);
    fputs("spell|Fire Blast|magic|1162|129|130|131|3087|663|3|3|172|124|51|16|-5|64|10|spell|-|-|-|-|-|-|-|-|-|-|-|-|-|-|-|Cast|92|124|-|-|travel_projectile|source_actor|target_actor|source_actor|target_actor|test\n", f);
    fputs("npc|990001|magic|711|200|201|202|5555|777|3|3|172|124|51|16|-5|64|10|npc|-|-|-|-|-|-|-|-|-|-|-|-|-|-|-|Magic|92|124|-|-|travel_projectile|source_actor|target_actor|source_actor|target_actor|test\n", f);
    fputs("special|861|ranged|426|-|-|-|-|-|3|3|163|146|41|5|5|11|5|darkbow|2|163|146|41|25|14|11|10|880|881|9900|9901|1|-|-|Spec|-|-|-|-|multi_projectile|source_actor|target_actor|source_actor|target_actor|test\n", f);
    fputs("npc|3127|magic|2656|439|445|446|9335|2648|3|3|172|124|41|16|0|64|5|jad_magic|-|-|-|-|-|-|-|-|-|-|-|-|-|-|-|Magic|92|-|-|-|travel_projectile|source_center|target_actor|source_actor|target_actor|test\n", f);
    fputs("npc|3127|ranged|2652|440|-|451|-|-|3|3|768|52|60|0|0|0|0|jad_ranged|-|-|-|-|-|-|-|-|-|-|-|-|-|-|-|Ranged|-|52|-|-|fixed_tile_impact|target_tile|target_tile|none|fixed_tile|test\n", f);
    fclose(f);
}

static RcWorld *make_world(void) {
    RcWorldConfig cfg = rc_preset_base_only();
    cfg.subsystems = RC_SUB_COMBAT;
    cfg.combat_profiles_path = NULL;
    cfg.seed = 42;
    RcWorld *world = rc_world_create_config(&cfg);
    assert(world != NULL);
    world->player.x = 3200;
    world->player.y = 3200;
    for (int i = 0; i < SKILL_COUNT; i++) {
        world->player.skills.base_level[i] = 99;
        world->player.skills.boosted_level[i] = 99;
    }
    return world;
}

static int spawn_target(RcWorld *world) {
    int idx = rc_npc_spawn(world, 0, 3204, 3200, 0);
    assert(idx >= 0);
    return idx;
}

static void load_test_profiles(void) {
    write_visuals_file("/tmp/runec_combat_visuals_test.tsv");
    assert(rc_load_combat_profiles("/tmp/runec_combat_visuals_test.tsv") == 7);
    assert(rc_load_combat_visuals("/tmp/runec_combat_visuals_test.tsv") == 7);
}

static void test_core_ranged_attack_emits_logical_event_only(void) {
    reset_defs();
    add_defs();
    load_test_profiles();
    RcWorld *world = make_world();
    int npc_idx = spawn_target(world);
    world->player.equipment[EQUIP_WEAPON] = (RcInvSlot){TEST_BOW, 1};
    world->player.equipment[EQUIP_AMMO] = (RcInvSlot){TEST_ARROW, 5};
    rc_recalc_bonuses(&world->player);
    rc_refresh_player_combat_style(&world->player);
    assert(rc_combat_start_player_vs_npc(world, 0, world->npcs[npc_idx].uid));

    rc_combat_tick_player(world);

    int count = 0;
    const RcCombatAttackEvent *events =
        rc_combat_attack_events(world, &count);
    assert(count == 1);
    assert(events[0].source_kind == RC_COMBAT_ACTOR_PLAYER);
    assert(events[0].target_kind == RC_COMBAT_ACTOR_NPC);
    assert(events[0].target_uid == world->npcs[npc_idx].uid);
    assert(events[0].style == COMBAT_RANGED);
    assert(events[0].action_kind == RC_COMBAT_ACTION_ITEM);
    assert(events[0].action_key_id == TEST_BOW);
    assert(strcmp(events[0].action_key_name, "Magic shortbow") == 0);
    assert(events[0].weapon_item_id == TEST_BOW);
    assert(events[0].ammo_item_id == TEST_ARROW);
    assert(events[0].hit_delay == 2);
    assert(world->npcs[npc_idx].pending_hits[0].ticks_remaining == 2);

    const RcCombatVisualDef *arrow =
        rc_combat_visual_for_item(TEST_ARROW, COMBAT_RANGED);
    assert(arrow != NULL);
    assert(arrow->launch_spotanim_id == 24);
    assert(arrow->projectile_model_id == 3136);
    assert(arrow->primitive_type == RC_COMBAT_VISUAL_PRIMITIVE_TRAVEL_PROJECTILE);
    rc_world_destroy(world);
}

static void test_core_spell_profile_uses_spell_id_after_rename(void) {
    reset_defs();
    add_defs();
    load_test_profiles();
    strcpy(g_rc_spell_defs[0].name, "Renamed Fire Blast");

    RcWorld *world = make_world();
    int npc_idx = spawn_target(world);
    world->player.inventory[0] = (RcInvSlot){TEST_FIRE_RUNE, 5};
    world->player.inventory[1] = (RcInvSlot){TEST_AIR_RUNE, 5};
    world->player.manual_spell_cast = 0;
    rc_refresh_player_combat_style(&world->player);
    assert(rc_combat_start_player_vs_npc(world, 0, world->npcs[npc_idx].uid));

    rc_combat_tick_player(world);

    int count = 0;
    const RcCombatAttackEvent *events =
        rc_combat_attack_events(world, &count);
    assert(count == 1);
    assert(events[0].action_kind == RC_COMBAT_ACTION_SPELL);
    assert(events[0].action_key_id == 0);
    assert(strcmp(events[0].action_key_name, "Renamed Fire Blast") == 0);
    assert(events[0].spell_idx == 0);
    assert(events[0].hit_delay == 3);
    assert(world->npcs[npc_idx].pending_hits[0].ticks_remaining == 3);

    assert(rc_combat_visual_for_spell("Renamed Fire Blast",
                                      COMBAT_MAGIC) == NULL);
    assert(rc_combat_visual_for_spell_id(0, "Renamed Fire Blast",
                                         COMBAT_MAGIC) != NULL);
    rc_world_destroy(world);
}

static void test_core_npc_attack_event_is_backend_only(void) {
    reset_defs();
    add_defs();
    load_test_profiles();
    RcWorld *world = make_world();
    int npc_idx = spawn_target(world);
    RcNpc *npc = &world->npcs[npc_idx];
    assert(rc_combat_start_npc_vs_player(world, npc->uid, 0));

    rc_combat_tick_npc(world, npc);

    int count = 0;
    const RcCombatAttackEvent *events =
        rc_combat_attack_events(world, &count);
    assert(count == 1);
    assert(events[0].source_kind == RC_COMBAT_ACTOR_NPC);
    assert(events[0].target_kind == RC_COMBAT_ACTOR_PLAYER);
    assert(events[0].source_uid == npc->uid);
    assert(events[0].action_kind == RC_COMBAT_ACTION_NPC);
    assert(events[0].action_key_id == TEST_NPC_ID);
    assert(strcmp(events[0].action_key_name, "Projectile Target") == 0);
    assert(events[0].style == COMBAT_MAGIC);
    assert(events[0].hit_delay == 3);
    assert(world->player.pending_hits[0].ticks_remaining == 3);
    rc_world_destroy(world);
}

static void test_viewer_visual_parser_owns_rich_projectile_metadata(void) {
    reset_defs();
    add_defs();
    load_test_profiles();

    const RcCombatVisualDef *jad_magic =
        rc_combat_visual_for_npc(TEST_JAD_ID, COMBAT_MAGIC);
    assert(jad_magic != NULL);
    assert(jad_magic->launch_spotanim_id == 439);
    assert(jad_magic->travel_spotanim_id == 445);
    assert(jad_magic->impact_spotanim_id == 446);
    assert(jad_magic->projectile_model_id == 9335);
    assert(jad_magic->launch_spotanim_height == 92);
    assert(jad_magic->source_attachment ==
           RC_COMBAT_VISUAL_ATTACH_SOURCE_CENTER);

    const RcCombatVisualDef *jad_ranged =
        rc_combat_visual_for_npc(TEST_JAD_ID, COMBAT_RANGED);
    assert(jad_ranged != NULL);
    assert(jad_ranged->primitive_type ==
           RC_COMBAT_VISUAL_PRIMITIVE_FIXED_TILE_IMPACT);
    assert(jad_ranged->source_attachment ==
           RC_COMBAT_VISUAL_ATTACH_TARGET_TILE);
    assert(jad_ranged->impact_attachment ==
           RC_COMBAT_VISUAL_ATTACH_FIXED_TILE);
    assert(jad_ranged->impact_spotanim_height == 52);
}

static void test_generated_visuals_still_load_in_viewer_module(void) {
    FILE *f = fopen("data/defs/combat_visuals.tsv", "r");
    if (!f) return;
    fclose(f);

    memset(g_rc_combat_visual_defs, 0, sizeof(g_rc_combat_visual_defs));
    g_rc_combat_visual_count = 0;
    int count = rc_load_combat_visuals("data/defs/combat_visuals.tsv");
    assert(count > 1200);

    const RcCombatVisualDef *fire_blast =
        rc_combat_visual_for_spell("Fire Blast", COMBAT_MAGIC);
    assert(fire_blast != NULL);
    assert(fire_blast->travel_spotanim_id == 130);
    assert(fire_blast->projectile_model_id == 3087);
    assert(fire_blast->projectile_anim_id == 663);
    assert(fire_blast->projectile_start_height == 172);
    assert(fire_blast->projectile_end_height == 124);
    assert(fire_blast->primitive_type ==
           RC_COMBAT_VISUAL_PRIMITIVE_TRAVEL_PROJECTILE);

    const RcCombatVisualDef *darkbow =
        rc_combat_visual_for_special_item(11235, COMBAT_RANGED);
    assert(darkbow != NULL);
    assert(darkbow->projectile_count == 2);
    assert(darkbow->aux_travel_spotanim_id >= 0);
    assert(darkbow->primitive_type ==
           RC_COMBAT_VISUAL_PRIMITIVE_MULTI_PROJECTILE);
}

int main(void) {
    test_core_ranged_attack_emits_logical_event_only();
    test_core_spell_profile_uses_spell_id_after_rename();
    test_core_npc_attack_event_is_backend_only();
    test_viewer_visual_parser_owns_rich_projectile_metadata();
    test_generated_visuals_still_load_in_viewer_module();
    return 0;
}
