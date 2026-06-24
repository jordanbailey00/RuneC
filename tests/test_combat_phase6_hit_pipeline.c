#include "../rc-core/api.h"
#include "../rc-core/combat.h"
#include "../rc-core/combat_hit.h"
#include "../rc-core/events.h"
#include "../rc-core/npc.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    int npc_damaged;
    int npc_died;
    int player_damaged;
    int last_npc_damage;
    int last_player_damage;
} Phase6Events;

static void phase6_event_counter(RcWorld *world, int evt,
                                 const void *payload, void *ctx) {
    (void)world;
    Phase6Events *events = ctx;
    if (evt == RC_EVT_NPC_DAMAGED) {
        const RcPayloadNpcDamaged *p = payload;
        events->npc_damaged++;
        events->last_npc_damage = p->damage;
    } else if (evt == RC_EVT_NPC_DIED) {
        events->npc_died++;
    } else if (evt == RC_EVT_PLAYER_DAMAGED) {
        const RcPayloadPlayerDamaged *p = payload;
        events->player_damaged++;
        events->last_player_damage = p->damage;
    }
}

static int add_phase6_npc_def(int npc_id, int hp, int max_hit) {
    int def_idx = g_npc_def_count++;
    assert(def_idx < RC_MAX_NPC_DEFS);
    memset(&g_npc_defs[def_idx], 0, sizeof(g_npc_defs[def_idx]));
    g_npc_defs[def_idx].id = npc_id;
    strcpy(g_npc_defs[def_idx].name, "Phase 6 Target");
    g_npc_defs[def_idx].size = 1;
    g_npc_defs[def_idx].hitpoints = hp;
    g_npc_defs[def_idx].stats[0] = 99;
    g_npc_defs[def_idx].stats[1] = 1;
    g_npc_defs[def_idx].stats[2] = 99;
    g_npc_defs[def_idx].stats[3] = hp;
    g_npc_defs[def_idx].max_hit = max_hit;
    g_npc_defs[def_idx].attack_speed = max_hit > 0 ? 4 : 0;
    g_npc_defs[def_idx].attack_types = 0x04;
    g_npc_defs[def_idx].respawn_ticks = 9;
    strcpy(g_npc_defs[def_idx].options[1], "Attack");
    return def_idx;
}

static void write_phase6_drop_file(const char *path, int npc_id) {
    FILE *f = fopen(path, "wb");
    assert(f);
    uint32_t magic = 0x504F5244u;
    uint32_t version = 1u;
    uint32_t count = 1u;
    fwrite(&magic, sizeof(magic), 1, f);
    fwrite(&version, sizeof(version), 1, f);
    fwrite(&count, sizeof(count), 1, f);
    uint32_t table_npc = (uint32_t)npc_id;
    fwrite(&table_npc, sizeof(table_npc), 1, f);
    uint8_t always = 1;
    fwrite(&always, sizeof(always), 1, f);
    uint32_t item = 995u;
    uint16_t qmin = 3;
    uint16_t qmax = 3;
    fwrite(&item, sizeof(item), 1, f);
    fwrite(&qmin, sizeof(qmin), 1, f);
    fwrite(&qmax, sizeof(qmax), 1, f);
    uint8_t none = 0;
    fwrite(&none, sizeof(none), 1, f);
    fwrite(&none, sizeof(none), 1, f);
    uint32_t rare_weight = 0;
    fwrite(&rare_weight, sizeof(rare_weight), 1, f);
    fclose(f);
}

static RcWorld *phase6_world(uint32_t subsystems) {
    RcWorldConfig cfg = rc_preset_base_only();
    cfg.subsystems = subsystems;
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
    return world;
}

static void test_npc_hit_pipeline_records_damage_hp_xp_death_and_loot(void) {
    const int npc_id = 9601;
    const char *drops = "/tmp/runec_phase6_drops.bin";
    write_phase6_drop_file(drops, npc_id);

    RcWorldConfig cfg = rc_preset_base_only();
    cfg.subsystems = RC_SUB_COMBAT | RC_SUB_LOOT;
    cfg.seed = 12345;
    cfg.drops_path = drops;
    RcWorld *world = rc_world_create_config(&cfg);
    assert(world);
    for (int i = 0; i < SKILL_COUNT; i++) {
        world->player.skills.base_level[i] = 99;
        world->player.skills.boosted_level[i] = 99;
    }
    rc_player_set_attack_style(world, 0);

    Phase6Events events = {0};
    assert(rc_event_subscribe(world, RC_EVT_NPC_DAMAGED,
                              phase6_event_counter, &events) == 0);
    assert(rc_event_subscribe(world, RC_EVT_NPC_DIED,
                              phase6_event_counter, &events) == 0);

    int def_idx = add_phase6_npc_def(npc_id, 10, 0);
    int npc_idx = rc_npc_spawn(world, def_idx,
                               world->player.x + 1, world->player.y,
                               world->player.plane);
    assert(npc_idx >= 0);
    RcNpc *npc = &world->npcs[npc_idx];
    int xp_before = world->player.skills.xp[SKILL_ATTACK];

    rc_queue_hit_meta(npc->pending_hits, &npc->num_pending_hits,
                      15, 0, COMBAT_MELEE_CRUSH, -1, 0u,
                      world->tick, 0, 15);
    rc_world_tick(world);

    assert(npc->is_dead);
    assert(npc->current_hp == 0);
    assert(npc->death_timer == 3);
    assert(npc->respawn_timer == 9);
    assert(npc->combat.hp_current == 0);
    assert(npc->combat.hp_max == 10);
    assert(npc->combat.recent_hit_count == 1);
    assert(npc->combat.recent_hits[0].damage == 15);
    assert(npc->combat.recent_hits[0].max_hit == 15);
    assert(npc->combat.recent_hits[0].hit_type == RC_HIT_TYPE_MAX);
    assert(events.npc_damaged == 1);
    assert(events.npc_died == 1);
    assert(events.last_npc_damage == 15);
    assert(world->player.skills.xp[SKILL_ATTACK] > xp_before);
    assert(world->ground_item_count > 0);
    assert(world->ground_items[0].active);
    assert(world->ground_items[0].item_id == 995);
    assert(world->ground_items[0].quantity == 3);

    rc_world_destroy(world);
}

static void test_player_hit_pipeline_records_miss_and_damage_state(void) {
    RcWorld *world = phase6_world(RC_SUB_COMBAT);
    Phase6Events events = {0};
    assert(rc_event_subscribe(world, RC_EVT_PLAYER_DAMAGED,
                              phase6_event_counter, &events) == 0);

    int def_idx = add_phase6_npc_def(9602, 50, 10);
    int npc_idx = rc_npc_spawn(world, def_idx,
                               world->player.x + 1, world->player.y,
                               world->player.plane);
    assert(npc_idx >= 0);
    RcNpc *npc = &world->npcs[npc_idx];

    rc_queue_hit_meta(world->player.pending_hits,
                      &world->player.num_pending_hits,
                      0, 0, COMBAT_RANGED, npc->uid, 0u,
                      world->tick, 0, 10);
    rc_world_tick(world);

    assert(world->player.current_hp == 100);
    assert(world->player.combat.hp_current == 100);
    assert(world->player.combat.hp_max == 100);
    assert(world->player.combat.recent_hit_count == 1);
    assert(world->player.combat.recent_hits[0].damage == 0);
    assert(world->player.combat.recent_hits[0].max_hit == 10);
    assert(world->player.combat.recent_hits[0].hit_type == RC_HIT_TYPE_MISS);
    assert(events.player_damaged == 1);
    assert(events.last_player_damage == 0);

    rc_queue_hit_meta(world->player.pending_hits,
                      &world->player.num_pending_hits,
                      4, 0, COMBAT_MAGIC, npc->uid, 0u,
                      world->tick, 0, 10);
    rc_world_tick(world);

    assert(world->player.current_hp == 60);
    assert(world->player.combat.hp_current == 60);
    assert(world->player.combat.recent_hit_count >= 1);
    assert(world->player.combat.recent_hits[
           world->player.combat.recent_hit_count - 1].damage == 4);
    assert(events.player_damaged == 2);
    assert(events.last_player_damage == 4);

    rc_world_destroy(world);
}

static void test_force_max_hit_dummy_queues_max_player_damage(void) {
    RcWorld *world = phase6_world(RC_SUB_COMBAT);
    int def_idx = add_phase6_npc_def(9603, 1000000, 0);
    RcNpc *dummy = NULL;
    int npc_idx = rc_npc_spawn(world, def_idx, world->player.x + 1,
                               world->player.y, world->player.plane);
    assert(npc_idx >= 0);
    dummy = &world->npcs[npc_idx];
    dummy->force_player_max_hit = true;
    dummy->current_hp = 1000000;

    rc_player_attack_npc(world, dummy->uid);
    for (int i = 0; i < 8 && dummy->num_pending_hits == 0
            && dummy->combat.recent_hit_count == 0; i++) {
        rc_world_tick(world);
    }

    int damage = -1;
    int max_hit = -1;
    if (dummy->num_pending_hits > 0) {
        damage = dummy->pending_hits[0].damage;
        max_hit = dummy->pending_hits[0].max_hit;
    } else if (dummy->combat.recent_hit_count > 0) {
        int idx = dummy->combat.recent_hit_count - 1;
        damage = dummy->combat.recent_hits[idx].damage;
        max_hit = dummy->combat.recent_hits[idx].max_hit;
    }
    assert(max_hit > 0);
    assert(damage == max_hit);
    rc_world_destroy(world);
}

int main(void) {
    test_npc_hit_pipeline_records_damage_hp_xp_death_and_loot();
    test_player_hit_pipeline_records_miss_and_damage_state();
    test_force_max_hit_dummy_queues_max_player_damage();
    return 0;
}
