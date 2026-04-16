#include "api.h"
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
    (void)world;
}

static void process_player_skilling(RcWorld *world) {
    (void)world;
}

static void resolve_player_hits(RcWorld *world) {
    (void)world;
}

static void resolve_npc_hits(RcWorld *world, RcNpc *npc) {
    (void)world;
    (void)npc;
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

void rc_world_tick(RcWorld *world) {
    // Phase 1: Process queued player input
    process_player_input(world);

    // Phase 2: Compute player route
    process_player_route(world);

    // Phase 3: NPC processing
    for (int i = 0; i < world->npc_count; i++) {
        if (world->npcs[i].active) {
            rc_npc_tick(world, &world->npcs[i]);
        }
    }

    // Phase 4: Player processing
    process_player_movement(world);
    process_player_combat(world);
    process_player_skilling(world);

    // Phase 5: Resolve pending hits
    resolve_player_hits(world);
    for (int i = 0; i < world->npc_count; i++) {
        if (world->npcs[i].active) {
            resolve_npc_hits(world, &world->npcs[i]);
        }
    }

    // Phase 6: Prayer drain
    rc_prayer_drain_tick(&world->player);

    // Phase 7: Stat regen
    rc_stat_restore_tick(&world->player.skills);

    // Phase 8: Death checks, respawns, ground items
    check_deaths(world);
    tick_respawns(world);
    tick_ground_items(world);

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
