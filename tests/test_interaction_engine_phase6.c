#include "../rc-core/api.h"
#include "../rc-core/combat.h"
#include "../rc-core/items.h"
#include "../rc-core/npc.h"
#include "../rc-core/spells.h"

#include <assert.h>
#include <string.h>

typedef struct {
    int player_attacks;
    int npc_attacks;
    int npc_damaged;
    int player_damaged;
    int npc_died;
    int last_npc_hp;
    int last_npc_max_hp;
    int last_player_hp;
    int last_player_max_hp;
    int last_player_attack_target;
} Phase6Events;

enum {
    TEST_PHASE6_STAFF = 9020,
};

static void phase6_event_handler(RcWorld *world, int evt,
                                 const void *payload, void *ctx) {
    (void)world;
    Phase6Events *events = ctx;
    if (evt == RC_EVT_PLAYER_ATTACK) {
        const RcPayloadPlayerAttack *p = payload;
        events->player_attacks++;
        events->last_player_attack_target = p->target_npc_id;
    } else if (evt == RC_EVT_NPC_ATTACK) {
        events->npc_attacks++;
    } else if (evt == RC_EVT_NPC_DAMAGED) {
        const RcPayloadNpcDamaged *p = payload;
        events->npc_damaged++;
        events->last_npc_hp = p->current_hp;
        events->last_npc_max_hp = p->max_hp;
    } else if (evt == RC_EVT_PLAYER_DAMAGED) {
        const RcPayloadPlayerDamaged *p = payload;
        events->player_damaged++;
        events->last_player_hp = p->current_hp;
        events->last_player_max_hp = p->max_hp;
    } else if (evt == RC_EVT_NPC_DIED) {
        events->npc_died++;
    }
}

static int spawn_phase6_npc(RcWorld *world, int cache_id, int dx, int hp) {
    int def_idx = g_npc_def_count++;
    assert(def_idx < RC_MAX_NPC_DEFS);
    memset(&g_npc_defs[def_idx], 0, sizeof(g_npc_defs[def_idx]));
    g_npc_defs[def_idx].id = cache_id;
    strcpy(g_npc_defs[def_idx].name, "Phase 6 Guard");
    g_npc_defs[def_idx].size = 1;
    g_npc_defs[def_idx].combat_level = 2;
    g_npc_defs[def_idx].hitpoints = hp;
    g_npc_defs[def_idx].stats[0] = 99;
    g_npc_defs[def_idx].stats[1] = 1;
    g_npc_defs[def_idx].stats[2] = 99;
    g_npc_defs[def_idx].stats[3] = hp;
    g_npc_defs[def_idx].max_hit = 1;
    g_npc_defs[def_idx].attack_speed = 4;
    g_npc_defs[def_idx].attack_types = 0x04;
    g_npc_defs[def_idx].respawn_ticks = 3;
    strcpy(g_npc_defs[def_idx].options[0], "Talk-to");
    strcpy(g_npc_defs[def_idx].options[1], "Attack");
    int npc_idx = rc_npc_spawn(world, def_idx, world->player.x + dx,
                               world->player.y, world->player.plane);
    assert(npc_idx >= 0);
    world->npcs[npc_idx].wander_timer = 999999;
    return npc_idx;
}

static RcWorld *phase6_world(Phase6Events *events) {
    RcWorldConfig cfg = rc_preset_base_only();
    cfg.subsystems = RC_SUB_COMBAT;
    cfg.seed = 12345;
    RcWorld *world = rc_world_create_config(&cfg);
    assert(world);
    for (int i = 0; i < SKILL_COUNT; i++) {
        world->player.skills.base_level[i] = 99;
        world->player.skills.boosted_level[i] = 99;
    }
    world->player.equipment_bonuses[EQ_CRUSH_ATK] = 10000;
    world->player.equipment_bonuses[EQ_STR] = 100;
    rc_player_set_attack_style(world, 0);
    assert(rc_event_subscribe(world, RC_EVT_PLAYER_ATTACK,
                              phase6_event_handler, events) == 0);
    assert(rc_event_subscribe(world, RC_EVT_NPC_ATTACK,
                              phase6_event_handler, events) == 0);
    assert(rc_event_subscribe(world, RC_EVT_NPC_DAMAGED,
                              phase6_event_handler, events) == 0);
    assert(rc_event_subscribe(world, RC_EVT_PLAYER_DAMAGED,
                              phase6_event_handler, events) == 0);
    assert(rc_event_subscribe(world, RC_EVT_NPC_DIED,
                              phase6_event_handler, events) == 0);
    return world;
}

static void add_phase6_staff(void) {
    RcItemDef *def = &g_item_defs[TEST_PHASE6_STAFF];
    memset(def, 0, sizeof(*def));
    def->id = TEST_PHASE6_STAFF;
    def->loaded = true;
    strcpy(def->name, "Phase 6 staff");
    def->equippable = true;
    def->equipable_by_player = true;
    def->equipable_weapon = true;
    def->equip_slot = EQUIP_WEAPON;
    def->attack_speed = 5;
    def->attack_range = 1;
    def->weapon_type = 22;
    def->attack_magic = 100;
}

static void test_interaction_attack_enters_repeating_combat(void) {
    rc_interaction_clear_handlers();
    Phase6Events events = {0};
    RcWorld *world = phase6_world(&events);
    int npc_idx = spawn_phase6_npc(world, 901600, 1, 200);
    int uid = world->npcs[npc_idx].uid;
    int speed = rc_player_attack_speed(&world->player);

    rc_player_interact_npc(world, uid, 1);
    rc_world_tick(world);
    assert(world->player.attack_target == uid);
    assert(world->player.attack_target_def_id == 901600);
    assert(world->player.facing_entity == uid);
    assert(world->player.attack_anim_timer > 0);
    assert(events.player_attacks == 1);
    assert(events.last_player_attack_target == uid);
    assert(events.npc_damaged >= 1);
    assert(events.last_npc_max_hp == 200);
    assert(events.last_npc_hp < 200);

    for (int i = 1; i < speed; i++) rc_world_tick(world);
    assert(events.player_attacks == 1);
    rc_world_tick(world);
    assert(events.player_attacks == 2);

    rc_world_destroy(world);
}

static void test_magic_attack_queues_delayed_hit_and_npc_retaliates(void) {
    rc_interaction_clear_handlers();
    Phase6Events events = {0};
    RcWorld *world = phase6_world(&events);
    memset(g_rc_spell_defs, 0, sizeof(g_rc_spell_defs));
    g_rc_spell_count = 1;
    strcpy(g_rc_spell_defs[0].name, "Phase 6 Test Spell");
    g_rc_spell_defs[0].type = RC_SPELL_TYPE_COMBAT;
    g_rc_spell_defs[0].max_hit = 13;
    g_rc_spell_defs[0].loaded = 1;
    add_phase6_staff();
    world->player.equipment[EQUIP_WEAPON] = (RcInvSlot){TEST_PHASE6_STAFF, 1};
    rc_recalc_bonuses(&world->player);
    rc_player_set_autocast_spell(world, 0, 0);
    int npc_idx = spawn_phase6_npc(world, 901601, 4, 200);

    rc_player_interact_npc(world, world->npcs[npc_idx].uid, 1);
    rc_world_tick(world);
    assert(world->player.combat_style == COMBAT_MAGIC);
    assert(events.player_attacks == 1);
    assert(world->npcs[npc_idx].num_pending_hits == 1);
    assert(world->npcs[npc_idx].pending_hits[0].attack_style == COMBAT_MAGIC);
    assert(world->npcs[npc_idx].pending_hits[0].ticks_remaining > 0);
    assert(world->npcs[npc_idx].target_uid == 0);

    for (int i = 0; i < 16 &&
            (events.npc_attacks == 0 || events.player_damaged == 0 ||
             events.npc_damaged == 0); i++) {
        rc_world_tick(world);
    }
    assert(events.npc_attacks > 0);
    assert(events.player_damaged > 0);
    assert(events.npc_damaged > 0);
    assert(events.last_player_max_hp == world->player.max_hp);
    assert(world->npcs[npc_idx].facing_entity == 0);
    assert(world->npcs[npc_idx].facing_x == world->player.x);
    assert(world->npcs[npc_idx].facing_y == world->player.y);

    rc_world_destroy(world);
}

static void test_death_clears_target_and_respawn_is_stable(void) {
    rc_interaction_clear_handlers();
    Phase6Events events = {0};
    RcWorld *world = phase6_world(&events);
    int npc_idx = spawn_phase6_npc(world, 901602, 1, 5);

    rc_player_interact_npc(world, world->npcs[npc_idx].uid, 1);
    for (int i = 0; i < 40 && !world->npcs[npc_idx].is_dead; i++) {
        rc_world_tick(world);
    }
    assert(world->npcs[npc_idx].is_dead);
    assert(world->player.attack_target == -1);
    assert(world->player.attack_target_def_id == -1);
    assert(events.npc_died == 1);

    for (int i = 0; i < 12 && world->npcs[npc_idx].is_dead; i++) {
        rc_world_tick(world);
    }
    assert(!world->npcs[npc_idx].is_dead);
    assert(world->npcs[npc_idx].current_hp == 5);
    assert(world->npcs[npc_idx].target_uid == -1);
    assert(world->player.attack_target == -1);

    rc_world_destroy(world);
}

int main(void) {
    test_interaction_attack_enters_repeating_combat();
    test_magic_attack_queues_delayed_hit_and_npc_retaliates();
    test_death_clears_target_and_respawn_is_stable();
    rc_interaction_clear_handlers();
    return 0;
}
