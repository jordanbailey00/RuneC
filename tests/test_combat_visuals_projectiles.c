#include "api.h"
#include "combat.h"
#include "combat_visuals.h"
#include "items.h"
#include "npc.h"
#include "spells.h"

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

    g_rc_spell_count = 2;
    RcSpellDef *spell = &g_rc_spell_defs[0];
    memset(spell, 0, sizeof(*spell));
    strcpy(spell->name, "Fire Blast");
    spell->type = RC_SPELL_TYPE_COMBAT;
    spell->max_hit = 16;
    spell->rune_count = 2;
    spell->runes[0] = (RcSpellRune){TEST_FIRE_RUNE, 1};
    spell->runes[1] = (RcSpellRune){TEST_AIR_RUNE, 1};
    spell->loaded = 1;

    spell = &g_rc_spell_defs[1];
    memset(spell, 0, sizeof(*spell));
    strcpy(spell->name, "Saradomin Strike");
    spell->type = RC_SPELL_TYPE_COMBAT;
    spell->max_hit = 20;
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

static int add_jad_def(int attack_types) {
    int idx = g_npc_def_count++;
    RcNpcDef *npc = &g_npc_defs[idx];
    memset(npc, 0, sizeof(*npc));
    npc->id = TEST_JAD_ID;
    strcpy(npc->name, "TzTok-Jad");
    npc->size = 5;
    npc->hitpoints = 250;
    npc->stats[0] = 640;
    npc->stats[1] = 960;
    npc->stats[3] = 480;
    npc->stats[4] = 960;
    npc->stats[5] = 480;
    npc->max_hit = 1;
    npc->attack_speed = 8;
    npc->attack_types = attack_types;
    strcpy(npc->options[1], "Attack");
    return idx;
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
          "double_launch_spotanim\n", f);
    fputs("item|861|ranged|426|-|-|-|-|-|3|3|163|146|41|15|5|11|5|bow\n", f);
    fputs("item|892|ranged|-|24|15|-|3136|-|2|2|-|-|-|-|-|-|-|arrow|-|-|-|-|-|-|-|-|-|-|-|-|-|1109\n", f);
    fputs("spell|Fire Blast|magic|1162|129|130|131|3087|663|3|3|172|124|51|16|-5|64|10|spell\n", f);
    fputs("spell|Saradomin Strike|magic|811|-|-|76|-|-|2|2|-|-|-|-|-|-|-|impact\n", f);
    fputs("npc|990001|magic|711|200|201|202|5555|777|3|3|172|124|51|16|-5|64|10|npc\n", f);
    fputs("special|1305|slash|1058|248|-|-|-|-|-|-|-|-|-|-|-|-|-|special\n", f);
    fputs("special|861|ranged|426|-|-|-|-|-|3|3|163|146|41|5|5|11|5|darkbow|2|163|146|41|25|14|11|10|880|881|9900|9901|1\n", f);
    fclose(f);
}

static void write_jad_visuals_file(const char *path) {
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
          "double_launch_spotanim|stance_idx|primitive_type|"
          "source_attachment|target_attachment|launch_attachment|"
          "impact_attachment|authority\n", f);
    fputs("npc|3127|magic|2656|439|445|446|9335|2648|3|3|172|124|41|16|0|64|5|jad_magic|-|-|-|-|-|-|-|-|-|-|-|-|-|-|-|travel_projectile|source_center|target_actor|source_actor|target_actor|test\n", f);
    fputs("npc|3127|melee|2655|-|-|-|-|-|-|-|-|-|-|-|-|-|-|jad_melee|-|-|-|-|-|-|-|-|-|-|-|-|-|-|-|none|none|none|none|none|test\n", f);
    fputs("npc|3127|ranged|2652|440|-|451|-|-|3|3|768|52|60|0|0|0|0|jad_ranged|-|-|-|-|-|-|-|-|-|-|-|-|-|-|-|fixed_tile_impact|target_tile|target_tile|none|fixed_tile|test\n", f);
    fclose(f);
}

static void write_stance_visuals_file(const char *path) {
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
          "double_launch_spotanim|stance_idx\n", f);
    fputs("item|11804|slash|7045|-|-|-|-|-|-|-|-|-|-|-|-|-|-|bgs stance 1|-|-|-|-|-|-|-|-|-|-|-|-|-|-|0\n", f);
    fputs("item|11804|slash|7055|-|-|-|-|-|-|-|-|-|-|-|-|-|-|bgs stance 4|-|-|-|-|-|-|-|-|-|-|-|-|-|-|3\n", f);
    fputs("item|11804|crush|7054|-|-|-|-|-|-|-|-|-|-|-|-|-|-|bgs stance 3|-|-|-|-|-|-|-|-|-|-|-|-|-|-|2\n", f);
    fputs("item|11804|any|7045|-|-|-|-|-|-|-|-|-|-|-|-|-|-|bgs fallback\n", f);
    fclose(f);
}

static int test_visual_special_cost(const RcWorld *world,
                                    const RcPlayer *player,
                                    const RcNpc *target,
                                    int weapon_id) {
    (void)world;
    (void)player;
    (void)target;
    return weapon_id == TEST_BOW ? 5000 : 0;
}

static RcWorld *make_world(void) {
    RcWorldConfig cfg = rc_preset_base_only();
    cfg.subsystems = RC_SUB_COMBAT;
    cfg.combat_visuals_path = NULL;
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

static void test_ranged_attack_emits_data_backed_projectile(void) {
    reset_defs();
    add_defs();
    write_visuals_file("/tmp/runec_combat_visuals_test.tsv");
    assert(rc_load_combat_visuals("/tmp/runec_combat_visuals_test.tsv") == 7);
    RcWorld *world = make_world();
    int npc_idx = spawn_target(world);
    world->player.equipment[EQUIP_WEAPON] = (RcInvSlot){TEST_BOW, 1};
    world->player.equipment[EQUIP_AMMO] = (RcInvSlot){TEST_ARROW, 5};
    rc_recalc_bonuses(&world->player);
    rc_refresh_player_combat_style(&world->player);
    assert(rc_combat_start_player_vs_npc(world, 0, world->npcs[npc_idx].uid));

    rc_combat_tick_player(world);

    int count = 0;
    const RcCombatProjectile *projectiles =
        rc_combat_projectiles(world, &count);
    assert(count == 1);
    assert(projectiles[0].active);
    assert(projectiles[0].style == COMBAT_RANGED);
    assert(projectiles[0].weapon_item_id == TEST_BOW);
    assert(projectiles[0].ammo_item_id == TEST_ARROW);
    assert(projectiles[0].launch_spotanim_id == 24);
    assert(projectiles[0].travel_spotanim_id == 15);
    assert(projectiles[0].projectile_model_id == 3136);
    assert(projectiles[0].launch_spotanim_height == 96);
    assert(projectiles[0].impact_spotanim_height == 146);
    assert(projectiles[0].duration_ticks == 3);
    assert(projectiles[0].impact_duration_ticks == 0);
    assert(projectiles[0].projectile_start_height == 163);
    assert(projectiles[0].projectile_end_height == 146);
    assert(projectiles[0].projectile_start_time == 41);
    assert(projectiles[0].projectile_end_time == 66);
    assert(projectiles[0].projectile_angle == 15);
    assert(projectiles[0].projectile_progress == 11);
    assert(world->player.combat.attack_animation_id == 426);
    const RcCombatVisualEvent *events =
        rc_combat_visual_events(world, &count);
    assert(count == 1);
    assert(events[0].source_kind == RC_COMBAT_ACTOR_PLAYER);
    assert(events[0].target_kind == RC_COMBAT_ACTOR_NPC);
    assert(events[0].target_uid == world->npcs[npc_idx].uid);
    assert(events[0].style == COMBAT_RANGED);
    assert(events[0].action_kind == RC_COMBAT_VISUAL_ACTION_ITEM);
    assert(events[0].action_key_id == TEST_BOW);
    assert(strcmp(events[0].action_key_name, "Magic shortbow") == 0);
    assert(events[0].profile_kind == RC_COMBAT_VISUAL_ITEM);
    assert(events[0].profile_key_id == TEST_ARROW);
    assert(events[0].selected_attack_anim_id == 426);
    assert(events[0].hit_delay == 3);
    assert(events[0].client_delay == 3);
    rc_combat_tick_projectiles(world);
    projectiles = rc_combat_projectiles(world, &count);
    assert(count == 1);
    assert(projectiles[0].age_ticks == 0);
    world->tick++;
    rc_combat_tick_projectiles(world);
    projectiles = rc_combat_projectiles(world, &count);
    assert(count == 1);
    assert(projectiles[0].age_ticks == 1);
    rc_world_destroy(world);
}

static void test_ranged_special_emits_multi_projectile_visual_events(void) {
    reset_defs();
    add_defs();
    write_visuals_file("/tmp/runec_combat_visuals_test.tsv");
    assert(rc_load_combat_visuals("/tmp/runec_combat_visuals_test.tsv") == 7);
    RcWorld *world = make_world();
    RcCombatContentHooks hooks = {
        .player_special_energy_cost = test_visual_special_cost,
    };
    rc_combat_register_content_hooks(world, &hooks);
    int npc_idx = spawn_target(world);
    world->player.equipment[EQUIP_WEAPON] = (RcInvSlot){TEST_BOW, 1};
    world->player.equipment[EQUIP_AMMO] = (RcInvSlot){TEST_ARROW, 5};
    rc_recalc_bonuses(&world->player);
    rc_refresh_player_combat_style(&world->player);
    rc_combat_toggle_special(world);
    assert(rc_combat_start_player_vs_npc(world, 0, world->npcs[npc_idx].uid));

    rc_combat_tick_player(world);

    int count = 0;
    const RcCombatProjectile *projectiles =
        rc_combat_projectiles(world, &count);
    assert(count == 4);
    assert(projectiles[0].launch_spotanim_id == -1);
    assert(projectiles[0].travel_spotanim_id == 880);
    assert(projectiles[0].projectile_model_id == 9900);
    assert(projectiles[0].projectile_anim_id == 9901);
    assert(projectiles[0].impact_spotanim_id == -1);
    assert(projectiles[0].sequence_index == 0);
    assert(projectiles[0].sequence_count == 2);
    assert(projectiles[1].launch_spotanim_id == 1109);
    assert(projectiles[1].travel_spotanim_id == 15);
    assert(projectiles[1].projectile_model_id == 3136);
    assert(projectiles[1].projectile_angle == 5);
    assert(projectiles[1].sequence_index == 0);
    assert(projectiles[2].launch_spotanim_id == -1);
    assert(projectiles[2].travel_spotanim_id == 880);
    assert(projectiles[2].impact_spotanim_id == 881);
    assert(projectiles[2].projectile_angle == 25);
    assert(projectiles[2].projectile_end_time > projectiles[0].projectile_end_time);
    assert(projectiles[2].sequence_index == 1);
    assert(projectiles[3].travel_spotanim_id == 15);
    assert(projectiles[3].projectile_angle == 25);
    assert(projectiles[3].sequence_index == 1);
    assert(world->player.special_energy == 5000);
    const RcCombatVisualEvent *events =
        rc_combat_visual_events(world, &count);
    assert(count == 1);
    assert(events[0].action_kind == RC_COMBAT_VISUAL_ACTION_SPECIAL);
    assert(events[0].action_key_id == TEST_BOW);
    assert(events[0].profile_kind == RC_COMBAT_VISUAL_SPECIAL);
    assert(events[0].profile_key_id == TEST_BOW);
    assert(events[0].style == COMBAT_RANGED);
    assert(events[0].hit_delay == 3);
    assert(events[0].client_delay == 3);
    rc_world_destroy(world);
}

static void test_magic_attack_emits_spell_projectile(void) {
    reset_defs();
    add_defs();
    write_visuals_file("/tmp/runec_combat_visuals_test.tsv");
    assert(rc_load_combat_visuals("/tmp/runec_combat_visuals_test.tsv") == 7);
    strcpy(g_rc_spell_defs[0].name, "Renamed Fire Blast");
    assert(rc_combat_visual_for_spell("Renamed Fire Blast",
                                      COMBAT_MAGIC) == NULL);
    assert(rc_combat_visual_for_spell_id(0, "Renamed Fire Blast",
                                         COMBAT_MAGIC) != NULL);
    RcWorld *world = make_world();
    int npc_idx = spawn_target(world);
    world->player.inventory[0] = (RcInvSlot){TEST_FIRE_RUNE, 5};
    world->player.inventory[1] = (RcInvSlot){TEST_AIR_RUNE, 5};
    world->player.manual_spell_cast = 0;
    rc_refresh_player_combat_style(&world->player);
    assert(rc_combat_start_player_vs_npc(world, 0, world->npcs[npc_idx].uid));

    rc_combat_tick_player(world);

    int count = 0;
    const RcCombatProjectile *projectiles =
        rc_combat_projectiles(world, &count);
    assert(count == 1);
    assert(projectiles[0].style == COMBAT_MAGIC);
    assert(projectiles[0].spell_idx == 0);
    assert(projectiles[0].launch_spotanim_id == 129);
    assert(projectiles[0].travel_spotanim_id == 130);
    assert(projectiles[0].impact_spotanim_id == 131);
    assert(projectiles[0].projectile_model_id == 3087);
    assert(projectiles[0].projectile_anim_id == 663);
    assert(projectiles[0].launch_spotanim_height == 92);
    assert(projectiles[0].impact_spotanim_height == 124);
    assert(projectiles[0].duration_ticks == 3);
    assert(projectiles[0].impact_duration_ticks == 3);
    assert(projectiles[0].projectile_start_height == 172);
    assert(projectiles[0].projectile_end_height == 124);
    assert(projectiles[0].projectile_start_time == 51);
    assert(projectiles[0].projectile_end_time == 86);
    assert(projectiles[0].projectile_angle == 16);
    assert(projectiles[0].projectile_progress == 64);
    assert(world->player.combat.attack_animation_id == 1162);
    const RcCombatVisualEvent *events =
        rc_combat_visual_events(world, &count);
    assert(count == 1);
    assert(events[0].source_kind == RC_COMBAT_ACTOR_PLAYER);
    assert(events[0].target_kind == RC_COMBAT_ACTOR_NPC);
    assert(events[0].style == COMBAT_MAGIC);
    assert(events[0].action_kind == RC_COMBAT_VISUAL_ACTION_SPELL);
    assert(events[0].action_key_id == 0);
    assert(strcmp(events[0].action_key_name, "Renamed Fire Blast") == 0);
    assert(events[0].profile_kind == RC_COMBAT_VISUAL_SPELL);
    assert(events[0].profile_key_id == 0);
    assert(strcmp(events[0].profile_key_name, "Fire Blast") == 0);
    assert(events[0].selected_attack_anim_id == 1162);
    assert(events[0].hit_delay == 3);
    assert(events[0].client_delay == 3);
    for (int i = 0; i < 5; i++) {
        world->tick++;
        rc_combat_tick_projectiles(world);
    }
    projectiles = rc_combat_projectiles(world, &count);
    assert(count == 1);
    assert(projectiles[0].age_ticks == 5);
    for (int i = 0; i < 2; i++) {
        world->tick++;
        rc_combat_tick_projectiles(world);
    }
    projectiles = rc_combat_projectiles(world, &count);
    assert(count == 0);
    rc_world_destroy(world);
}

static void test_spell_on_npc_routes_to_magic_combat_projectile(void) {
    reset_defs();
    add_defs();
    write_visuals_file("/tmp/runec_combat_visuals_test.tsv");
    assert(rc_load_combat_visuals("/tmp/runec_combat_visuals_test.tsv") == 7);
    RcWorld *world = make_world();
    int npc_idx = spawn_target(world);
    RcNpc *npc = &world->npcs[npc_idx];
    world->player.equipment[EQUIP_WEAPON] = (RcInvSlot){TEST_BOW, 1};
    world->player.inventory[0] = (RcInvSlot){TEST_FIRE_RUNE, 5};
    world->player.inventory[1] = (RcInvSlot){TEST_AIR_RUNE, 5};

    assert(rc_player_cast_spell_on_npc(world, 0, npc->uid));
    rc_world_tick(world);

    int count = 0;
    const RcCombatProjectile *projectiles =
        rc_combat_projectiles(world, &count);
    assert(count == 1);
    assert(projectiles[0].active);
    assert(projectiles[0].target_kind == RC_COMBAT_ACTOR_NPC);
    assert(projectiles[0].target_uid == npc->uid);
    assert(projectiles[0].style == COMBAT_MAGIC);
    assert(projectiles[0].spell_idx == 0);
    assert(projectiles[0].travel_spotanim_id == 130);
    assert(projectiles[0].projectile_model_id == 3087);
    assert(world->player.manual_spell_cast == -1);
    assert(world->player.combat_style == COMBAT_MAGIC);
    const RcCombatVisualEvent *events =
        rc_combat_visual_events(world, &count);
    assert(count == 1);
    assert(events[0].action_kind == RC_COMBAT_VISUAL_ACTION_SPELL);
    assert(events[0].action_key_id == 0);
    assert(events[0].style == COMBAT_MAGIC);
    assert(events[0].hit_delay == projectiles[0].hit_delay);
    assert(events[0].client_delay == projectiles[0].client_delay);
    rc_world_destroy(world);
}

static void test_npc_attack_emits_data_backed_projectile(void) {
    reset_defs();
    add_defs();
    write_visuals_file("/tmp/runec_combat_visuals_test.tsv");
    assert(rc_load_combat_visuals("/tmp/runec_combat_visuals_test.tsv") == 7);
    RcWorld *world = make_world();
    int npc_idx = spawn_target(world);
    RcNpc *npc = &world->npcs[npc_idx];
    assert(rc_combat_start_npc_vs_player(world, npc->uid, 0));

    rc_combat_tick_npc(world, npc);

    int count = 0;
    const RcCombatProjectile *projectiles =
        rc_combat_projectiles(world, &count);
    assert(count == 1);
    assert(projectiles[0].active);
    assert(projectiles[0].source_kind == RC_COMBAT_ACTOR_NPC);
    assert(projectiles[0].target_kind == RC_COMBAT_ACTOR_PLAYER);
    assert(projectiles[0].source_uid == npc->uid);
    assert(projectiles[0].target_uid == 0);
    assert(projectiles[0].style == COMBAT_MAGIC);
    assert(projectiles[0].launch_spotanim_id == 200);
    assert(projectiles[0].travel_spotanim_id == 201);
    assert(projectiles[0].impact_spotanim_id == 202);
    assert(projectiles[0].projectile_model_id == 5555);
    assert(projectiles[0].projectile_anim_id == 777);
    assert(projectiles[0].launch_spotanim_height == 92);
    assert(projectiles[0].impact_spotanim_height == 124);
    assert(projectiles[0].hit_delay == 3);
    assert(projectiles[0].client_delay == 3);
    assert(projectiles[0].duration_ticks == 3);
    assert(projectiles[0].impact_duration_ticks == 3);
    assert(world->player.pending_hits[0].ticks_remaining == 3);
    assert(world->player.pending_hits[0].client_delay == 3);
    assert(npc->combat.attack_animation_id == 711);
    const RcCombatVisualEvent *events =
        rc_combat_visual_events(world, &count);
    assert(count == 1);
    assert(events[0].source_kind == RC_COMBAT_ACTOR_NPC);
    assert(events[0].target_kind == RC_COMBAT_ACTOR_PLAYER);
    assert(events[0].source_uid == npc->uid);
    assert(events[0].target_uid == 0);
    assert(events[0].style == COMBAT_MAGIC);
    assert(events[0].action_kind == RC_COMBAT_VISUAL_ACTION_NPC);
    assert(events[0].action_key_id == TEST_NPC_ID);
    assert(strcmp(events[0].action_key_name, "Projectile Target") == 0);
    assert(events[0].profile_kind == RC_COMBAT_VISUAL_NPC);
    assert(events[0].profile_key_id == TEST_NPC_ID);
    assert(strcmp(events[0].profile_key_name, "Projectile Target") == 0);
    assert(events[0].selected_attack_anim_id == 711);
    assert(events[0].hit_delay == 3);
    assert(events[0].client_delay == 3);
    rc_world_destroy(world);
}

static RcNpc *spawn_jad_attacker(RcWorld *world, int def_idx) {
    int npc_idx = rc_npc_spawn(world, def_idx, 3204, 3200, 0);
    assert(npc_idx >= 0);
    RcNpc *npc = &world->npcs[npc_idx];
    assert(rc_combat_start_npc_vs_player(world, npc->uid, 0));
    return npc;
}

static void test_jad_magic_projectile_starts_from_jad_center(void) {
    reset_defs();
    int jad_def = add_jad_def(0x08);
    write_jad_visuals_file("/tmp/runec_jad_visuals_test.tsv");
    assert(rc_load_combat_visuals("/tmp/runec_jad_visuals_test.tsv") == 3);
    RcWorld *world = make_world();
    RcNpc *jad = spawn_jad_attacker(world, jad_def);

    rc_combat_tick_npc(world, jad);

    int count = 0;
    const RcCombatProjectile *projectiles =
        rc_combat_projectiles(world, &count);
    assert(count == 1);
    assert(projectiles[0].style == COMBAT_MAGIC);
    assert(projectiles[0].primitive_type ==
           RC_COMBAT_VISUAL_PRIMITIVE_TRAVEL_PROJECTILE);
    assert(projectiles[0].source_attachment ==
           RC_COMBAT_VISUAL_ATTACH_SOURCE_CENTER);
    assert(projectiles[0].impact_attachment ==
           RC_COMBAT_VISUAL_ATTACH_TARGET_ACTOR);
    assert(projectiles[0].source_x == jad->x + 2);
    assert(projectiles[0].source_y == jad->y + 2);
    assert(projectiles[0].target_x == world->player.x);
    assert(projectiles[0].target_y == world->player.y);
    assert(projectiles[0].launch_spotanim_id == 439);
    assert(projectiles[0].travel_spotanim_id == 445);
    assert(projectiles[0].impact_spotanim_id == 446);
    assert(projectiles[0].projectile_model_id == 9335);
    assert(projectiles[0].projectile_anim_id == 2648);
    assert(projectiles[0].launch_spotanim_height == 172);
    assert(projectiles[0].projectile_start_height == 172);
    assert(projectiles[0].projectile_end_height == 124);
    assert(projectiles[0].projectile_start_time == 41);
    assert(projectiles[0].projectile_end_time == 71);
    assert(jad->combat.attack_animation_id == 2656);
    assert(jad->attack_anim_timer == 5);
    assert(jad->combat.attack_animation_timer == 5);
    rc_world_destroy(world);
}

static void test_jad_ranged_projectile_drops_from_ceiling(void) {
    reset_defs();
    int jad_def = add_jad_def(0x10);
    write_jad_visuals_file("/tmp/runec_jad_visuals_test.tsv");
    assert(rc_load_combat_visuals("/tmp/runec_jad_visuals_test.tsv") == 3);
    RcWorld *world = make_world();
    RcNpc *jad = spawn_jad_attacker(world, jad_def);

    rc_combat_tick_npc(world, jad);

    int count = 0;
    const RcCombatProjectile *projectiles =
        rc_combat_projectiles(world, &count);
    assert(count == 1);
    assert(projectiles[0].style == COMBAT_RANGED);
    assert(projectiles[0].primitive_type ==
           RC_COMBAT_VISUAL_PRIMITIVE_FIXED_TILE_IMPACT);
    assert(projectiles[0].source_attachment ==
           RC_COMBAT_VISUAL_ATTACH_TARGET_TILE);
    assert(projectiles[0].target_attachment ==
           RC_COMBAT_VISUAL_ATTACH_TARGET_TILE);
    assert(projectiles[0].impact_attachment ==
           RC_COMBAT_VISUAL_ATTACH_FIXED_TILE);
    assert(projectiles[0].source_x == world->player.x);
    assert(projectiles[0].source_y == world->player.y);
    assert(projectiles[0].target_x == world->player.x);
    assert(projectiles[0].target_y == world->player.y);
    assert(projectiles[0].launch_spotanim_id == -1);
    assert(projectiles[0].travel_spotanim_id == -1);
    assert(projectiles[0].impact_spotanim_id == 451);
    assert(projectiles[0].projectile_model_id == -1);
    assert(projectiles[0].projectile_anim_id == -1);
    assert(projectiles[0].projectile_start_height == 768);
    assert(projectiles[0].projectile_end_height == 52);
    assert(projectiles[0].projectile_start_time == 0);
    assert(projectiles[0].projectile_end_time == 60);
    assert(projectiles[0].projectile_angle == 0);
    assert(projectiles[0].projectile_progress == 0);
    assert(projectiles[0].impact_spotanim_height == 52);
    assert(projectiles[0].duration_ticks == 3);
    assert(projectiles[0].impact_duration_ticks == 3);
    assert(jad->combat.attack_animation_id == 2652);
    assert(jad->attack_anim_timer == 5);
    assert(jad->combat.attack_animation_timer == 5);
    for (int i = 0; i < 6; i++) {
        world->tick++;
        rc_combat_tick_projectiles(world);
        rc_combat_tick_npc(world, jad);
        projectiles = rc_combat_projectiles(world, &count);
        assert(count <= 1);
        assert(jad->attack_count == 1);
    }
    rc_world_destroy(world);
}

static void test_jad_melee_uses_bite_animation_without_projectile(void) {
    reset_defs();
    int jad_def = add_jad_def(0x04);
    write_jad_visuals_file("/tmp/runec_jad_visuals_test.tsv");
    assert(rc_load_combat_visuals("/tmp/runec_jad_visuals_test.tsv") == 3);
    RcWorld *world = make_world();
    int npc_idx = rc_npc_spawn(world, jad_def, 3201, 3200, 0);
    assert(npc_idx >= 0);
    RcNpc *jad = &world->npcs[npc_idx];
    assert(rc_combat_start_npc_vs_player(world, jad->uid, 0));

    rc_combat_tick_npc(world, jad);

    int count = 0;
    (void)rc_combat_projectiles(world, &count);
    assert(count == 0);
    assert(jad->combat.attack_animation_id == 2655);
    assert(jad->attack_anim_timer == 3);
    assert(jad->combat.attack_animation_timer == 3);
    const RcCombatVisualEvent *events =
        rc_combat_visual_events(world, &count);
    assert(count == 1);
    assert(events[0].source_kind == RC_COMBAT_ACTOR_NPC);
    assert(events[0].target_kind == RC_COMBAT_ACTOR_PLAYER);
    assert(events[0].style == COMBAT_MELEE_CRUSH);
    assert(events[0].action_kind == RC_COMBAT_VISUAL_ACTION_NPC);
    assert(events[0].action_key_id == TEST_JAD_ID);
    assert(strcmp(events[0].action_key_name, "TzTok-Jad") == 0);
    assert(events[0].profile_kind == RC_COMBAT_VISUAL_NPC);
    assert(events[0].profile_key_id == TEST_JAD_ID);
    assert(strcmp(events[0].profile_key_name, "TzTok-Jad") == 0);
    assert(events[0].primitive_type == RC_COMBAT_VISUAL_PRIMITIVE_NONE);
    assert(events[0].selected_attack_anim_id == 2655);
    assert(events[0].hit_delay == 0);
    assert(events[0].client_delay == 1);
    rc_world_destroy(world);
}

static void test_impact_only_spell_emits_timed_impact(void) {
    reset_defs();
    add_defs();
    write_visuals_file("/tmp/runec_combat_visuals_test.tsv");
    assert(rc_load_combat_visuals("/tmp/runec_combat_visuals_test.tsv") == 7);
    RcWorld *world = make_world();
    int npc_idx = spawn_target(world);
    world->player.manual_spell_cast = 1;
    rc_refresh_player_combat_style(&world->player);
    assert(rc_combat_start_player_vs_npc(world, 0, world->npcs[npc_idx].uid));

    rc_combat_tick_player(world);

    int count = 0;
    const RcCombatProjectile *projectiles =
        rc_combat_projectiles(world, &count);
    assert(count == 1);
    assert(projectiles[0].style == COMBAT_MAGIC);
    assert(projectiles[0].spell_idx == 1);
    assert(projectiles[0].travel_spotanim_id == -1);
    assert(projectiles[0].impact_spotanim_id == 76);
    assert(projectiles[0].duration_ticks == 2);
    assert(projectiles[0].impact_duration_ticks == 3);
    assert(world->player.combat.attack_animation_id == 811);
    rc_world_destroy(world);
}

static void test_special_visual_lookup_uses_special_kind(void) {
    reset_defs();
    write_visuals_file("/tmp/runec_combat_visuals_test.tsv");
    assert(rc_load_combat_visuals("/tmp/runec_combat_visuals_test.tsv") == 7);

    const RcCombatVisualDef *visual =
        rc_combat_visual_for_special_item(1305, COMBAT_MELEE_SLASH);
    assert(visual != NULL);
    assert(visual->kind == RC_COMBAT_VISUAL_SPECIAL);
    assert(visual->attack_anim_id == 1058);
    assert(visual->launch_spotanim_id == 248);
    assert(rc_combat_visual_for_item(1305, COMBAT_MELEE_SLASH) == NULL);
}

static void test_item_visual_lookup_uses_combat_stance(void) {
    reset_defs();
    write_stance_visuals_file("/tmp/runec_combat_visuals_stance_test.tsv");
    assert(rc_load_combat_visuals("/tmp/runec_combat_visuals_stance_test.tsv") == 4);

    const RcCombatVisualDef *slash_one =
        rc_combat_visual_for_item_stance(11804, COMBAT_MELEE_SLASH, 0);
    const RcCombatVisualDef *slash_four =
        rc_combat_visual_for_item_stance(11804, COMBAT_MELEE_SLASH, 3);
    const RcCombatVisualDef *crush =
        rc_combat_visual_for_item_stance(11804, COMBAT_MELEE_CRUSH, 2);
    const RcCombatVisualDef *fallback =
        rc_combat_visual_for_item(11804, COMBAT_MELEE_STAB);
    assert(slash_one && slash_one->attack_anim_id == 7045);
    assert(slash_four && slash_four->attack_anim_id == 7055);
    assert(crush && crush->attack_anim_id == 7054);
    assert(fallback && fallback->attack_anim_id == 7045);
}

static void test_generated_visuals_include_spell_projectile_profiles(void) {
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
    assert(fire_blast->projectile_delay == 51);
    assert(fire_blast->projectile_angle == 16);
    assert(fire_blast->projectile_length_adjustment == -5);
    assert(fire_blast->projectile_progress == 64);
    assert(fire_blast->projectile_step_multiplier == 10);
    assert(fire_blast->primitive_type ==
           RC_COMBAT_VISUAL_PRIMITIVE_TRAVEL_PROJECTILE);
    assert(strcmp(fire_blast->authority, "rsmod") == 0);

    const RcCombatVisualDef *iban =
        rc_combat_visual_for_spell("Iban Blast", COMBAT_MAGIC);
    assert(iban != NULL);
    assert(iban->travel_spotanim_id == 88);
    assert(iban->projectile_delay == 60);
    assert(iban->projectile_length_adjustment == -14);

    const RcCombatVisualDef *ice_barrage =
        rc_combat_visual_for_spell("Ice Barrage", COMBAT_MAGIC);
    assert(ice_barrage != NULL);
    assert(ice_barrage->travel_spotanim_id == 368);
    assert(ice_barrage->impact_spotanim_id == 369);
    assert(ice_barrage->projectile_end_height == 0);

    const RcCombatVisualDef *ghostly =
        rc_combat_visual_for_spell("Ghostly Grasp", COMBAT_MAGIC);
    assert(ghostly != NULL);
    assert(ghostly->launch_spotanim_id == 1856);
    assert(ghostly->impact_spotanim_id == 1857);
    assert(ghostly->primitive_type ==
           RC_COMBAT_VISUAL_PRIMITIVE_TARGET_IMPACT);

    const RcCombatVisualDef *vorkath =
        rc_combat_visual_for_npc(8061, COMBAT_MAGIC);
    assert(vorkath != NULL);
    assert(vorkath->travel_spotanim_id == 1479);
    assert(vorkath->impact_spotanim_id == 1480);

    const RcCombatVisualDef *graardor =
        rc_combat_visual_for_npc(2215, COMBAT_RANGED);
    assert(graardor != NULL);
    assert(graardor->travel_spotanim_id == 1202);

    const RcCombatVisualDef *jad_ranged =
        rc_combat_visual_for_npc(TEST_JAD_ID, COMBAT_RANGED);
    assert(jad_ranged != NULL);
    assert(jad_ranged->primitive_type ==
           RC_COMBAT_VISUAL_PRIMITIVE_FIXED_TILE_IMPACT);
    assert(jad_ranged->source_attachment ==
           RC_COMBAT_VISUAL_ATTACH_TARGET_TILE);
    assert(jad_ranged->impact_attachment ==
           RC_COMBAT_VISUAL_ATTACH_FIXED_TILE);

    const RcCombatVisualDef *jad_magic =
        rc_combat_visual_for_npc(TEST_JAD_ID, COMBAT_MAGIC);
    assert(jad_magic != NULL);
    assert(jad_magic->primitive_type ==
           RC_COMBAT_VISUAL_PRIMITIVE_TRAVEL_PROJECTILE);
    assert(jad_magic->source_attachment ==
           RC_COMBAT_VISUAL_ATTACH_SOURCE_CENTER);

    const RcCombatVisualDef *dlong =
        rc_combat_visual_for_special_item(1305, COMBAT_MELEE_SLASH);
    assert(dlong != NULL);
    assert(dlong->attack_anim_id == 1058);
    assert(dlong->launch_spotanim_id == 248);

    const RcCombatVisualDef *darkbow =
        rc_combat_visual_for_special_item(11235, COMBAT_RANGED);
    assert(darkbow != NULL);
    assert(darkbow->attack_anim_id == 426);
    assert(darkbow->projectile_count == 2);
    assert(darkbow->projectile_angle == 5);
    assert(darkbow->alt_projectile_angle == 25);
    assert(darkbow->aux_travel_spotanim_id == 1101);
    assert(darkbow->aux_impact_spotanim_id == 1103);
    assert(darkbow->impact_on_last_only == 1);
    assert(darkbow->primitive_type ==
           RC_COMBAT_VISUAL_PRIMITIVE_MULTI_PROJECTILE);

    const RcCombatVisualDef *rune_arrow =
        rc_combat_visual_for_item(892, COMBAT_RANGED);
    assert(rune_arrow != NULL);
    assert(rune_arrow->launch_spotanim_id == 24);
    assert(rune_arrow->double_launch_spotanim_id == 1109);
}

int main(void) {
    test_ranged_attack_emits_data_backed_projectile();
    test_ranged_special_emits_multi_projectile_visual_events();
    test_magic_attack_emits_spell_projectile();
    test_impact_only_spell_emits_timed_impact();
    test_spell_on_npc_routes_to_magic_combat_projectile();
    test_npc_attack_emits_data_backed_projectile();
    test_jad_magic_projectile_starts_from_jad_center();
    test_jad_ranged_projectile_drops_from_ceiling();
    test_jad_melee_uses_bite_animation_without_projectile();
    test_special_visual_lookup_uses_special_kind();
    test_item_visual_lookup_uses_combat_stance();
    test_generated_visuals_include_spell_projectile_profiles();
    return 0;
}
