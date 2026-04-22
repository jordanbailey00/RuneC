#include "api.h"
#include "config.h"
#include "events.h"
#include "encounter.h"
#include "npc.h"
#include "combat.h"
#include "prayer.h"
#include "skills.h"
#include "pathfinding.h"

// Stub input processing — will be filled in as systems are built
static void process_player_input(RcWorld *world) {
    (void)world;
}

static void process_player_route(RcWorld *world) {
    (void)world;
}

static void process_player_movement(RcWorld *world) {
    (void)world;
}

static void process_player_combat(RcWorld *world) {
    rc_combat_tick_player(world);
}

static void process_player_skilling(RcWorld *world) {
    (void)world;
}

// (Player-side hit resolution lives in combat.c as
// rc_resolve_player_hits — it fires RC_EVT_PLAYER_DAMAGED per hit.)

static void resolve_npc_hits(RcWorld *world, RcNpc *npc) {
    bool was_alive = !npc->is_dead;
    int damage = rc_resolve_pending(npc->pending_hits,
                                    &npc->num_pending_hits,
                                    false /* npc defender */);
    if (damage > 0) {
        npc->current_hp -= damage;
        if (npc->current_hp <= 0) {
            npc->current_hp = 0;
            npc->is_dead = true;
        }
    }
    // Fire death event once on the transition alive → dead.
    if (was_alive && npc->is_dead) {
        RcPayloadNpcEvent payload = {
            .npc_id = (uint16_t)npc->uid,
            .def_id = (uint32_t)g_npc_defs[npc->def_id].id,
        };
        rc_event_fire(world, RC_EVT_NPC_DIED, &payload);
    }
}

static void check_deaths(RcWorld *world) {
    (void)world;
}

static void tick_respawns(RcWorld *world) {
    (void)world;
}

static void tick_ground_items(RcWorld *world) {
    (void)world;
}

// Tick dispatcher. Per rc-core/README.md §3, per-subsystem ticks are
// gated by a cache-resident bitmask-AND; the base (player position,
// NPC position, pathfinding, tick counter) always runs.
void rc_world_tick(RcWorld *world) {
    const uint32_t on = world->enabled;

    // Phase 1 — input (base): always runs.
    process_player_input(world);

    // Phase 2 — route planning (base): always runs.
    process_player_route(world);

    // Phase 3 — NPC tick (base): position + wander + route advance.
    // Encounter mechanics dispatch happens inside npc_tick when
    // RC_SUB_ENCOUNTER is enabled.
    for (int i = 0; i < world->npc_count; i++) {
        if (world->npcs[i].active) {
            rc_npc_tick(world, &world->npcs[i]);
        }
    }

    // Phase 3.5 — NPC combat (COMBAT subsystem): after positions
    // resolve, each NPC may queue an attack on its target.
    if (on & RC_SUB_COMBAT) {
        for (int i = 0; i < world->npc_count; i++) {
            if (world->npcs[i].active) {
                rc_combat_tick_npc(world, &world->npcs[i]);
            }
        }
    }

    // Phase 3.6 — encounter mechanic dispatcher.
    if (on & RC_SUB_ENCOUNTER) rc_encounter_tick(world);

    // Phase 4 — player action resolution (gated per subsystem).
    process_player_movement(world);
    if (on & RC_SUB_COMBAT)   process_player_combat(world);
    if (on & RC_SUB_SKILLS)   process_player_skilling(world);

    // Phase 5 — pending-hit resolution (combat subsystem).
    if (on & RC_SUB_COMBAT) {
        rc_resolve_player_hits(world);
        for (int i = 0; i < world->npc_count; i++) {
            if (world->npcs[i].active) {
                resolve_npc_hits(world, &world->npcs[i]);
            }
        }
    }

    // Phase 6 — prayer drain (prayer subsystem).
    if (on & RC_SUB_PRAYER)   rc_prayer_drain_tick(&world->player);

    // Phase 7 — stat regen (skills subsystem, but hp regen baseline
    // runs in base since it's part of the base player model).
    rc_stat_restore_tick(&world->player.skills);

    // Phase 8 — deaths / respawns / ground items.
    if (on & RC_SUB_COMBAT)   check_deaths(world);
    tick_respawns(world);     // base — NPC wander reset clock
    if (on & RC_SUB_LOOT)     tick_ground_items(world);

    world->tick++;
}

// Input stubs — filled in as we build each system
void rc_player_walk_to(RcWorld *world, int x, int y) { (void)world; (void)x; (void)y; }
void rc_player_run_to(RcWorld *world, int x, int y) { (void)world; (void)x; (void)y; }
void rc_player_attack_npc(RcWorld *world, int npc_uid) { (void)world; (void)npc_uid; }
void rc_player_set_prayer(RcWorld *world, int prayer_id) { (void)world; (void)prayer_id; }
void rc_player_eat(RcWorld *world, int inv_slot) { (void)world; (void)inv_slot; }
void rc_player_drink(RcWorld *world, int inv_slot) { (void)world; (void)inv_slot; }
void rc_player_equip(RcWorld *world, int inv_slot) { (void)world; (void)inv_slot; }
void rc_player_unequip(RcWorld *world, int equip_slot) { (void)world; (void)equip_slot; }
void rc_player_interact_npc(RcWorld *world, int npc_uid, int opt) { (void)world; (void)npc_uid; (void)opt; }
void rc_player_interact_object(RcWorld *world, int obj_id, int opt) { (void)world; (void)obj_id; (void)opt; }
void rc_player_drop_item(RcWorld *world, int inv_slot) { (void)world; (void)inv_slot; }
void rc_player_pickup_item(RcWorld *world, int idx) { (void)world; (void)idx; }
